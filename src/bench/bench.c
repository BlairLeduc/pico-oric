/* bench.c — M2's workloads (design.md §15.2 M2). See bench.h. */
#include "bench.h"

#include <string.h>

/* ---- The flat bus ------------------------------------------------------ */

/* Every page plain RAM, page #03 included: the 6502 and the run loop with
 * nothing behind the slow path (design.md §15.2 M2). */
static void machine_clear(bench_t *b) {
    oric_config_t cfg;
    oric_config_default(&cfg);
    oric_init(&b->m, &cfg);
    oric_map_ram(&b->m, 0x0000, ORIC_ADDR_SPACE);
    memset(b->m.ram, 0, sizeof b->m.ram);
    m6502_init(&b->m.cpu);
    b->m.cpu.s = 0xFF;
    b->m.instructions = 0;
}

/* ---- basic: a minimal assembler ---------------------------------------- */

enum {
    L_CHRGET, L_CHRGOT, L_CHRDONE,
    L_START, L_STMT, L_NEXT, L_NOTDIG,
    L_LOADARG, L_STOREFAC,
    L_FADD, L_FNZ, L_COPY, L_SWAP, L_SHIFT, L_SH, L_ADD, L_FDONE,
    L_COUNT
};

#define FIXUPS 32

typedef struct { uint16_t at; uint8_t label; bool rel; } fixup_t;

typedef struct {
    uint8_t *mem;
    uint16_t at;
    uint16_t label[L_COUNT];
    fixup_t  fix[FIXUPS];
    unsigned nfix;
} asm_t;

static void b8(asm_t *a, uint8_t v) { a->mem[a->at++] = v; }

static void b16(asm_t *a, uint16_t v) {
    b8(a, (uint8_t)v);
    b8(a, (uint8_t)(v >> 8));
}

static void here(asm_t *a, unsigned l) { a->label[l] = a->at; }

static void org(asm_t *a, uint16_t at) { a->at = at; }

/* A branch: opcode, then a relative offset to the label, patched later. */
static void br(asm_t *a, uint8_t op, unsigned l) {
    b8(a, op);
    if (a->nfix < FIXUPS)
        a->fix[a->nfix++] = (fixup_t){ a->at, (uint8_t)l, true };
    b8(a, 0);
}

/* JSR or JMP to a label. */
static void jabs(asm_t *a, uint8_t op, unsigned l) {
    b8(a, op);
    if (a->nfix < FIXUPS)
        a->fix[a->nfix++] = (fixup_t){ a->at, (uint8_t)l, false };
    b16(a, 0);
}

static void resolve(asm_t *a) {
    for (unsigned i = 0; i < a->nfix; i++) {
        uint16_t at = a->fix[i].at, to = a->label[a->fix[i].label];
        if (a->fix[i].rel) {
            a->mem[at] = (uint8_t)(to - (uint16_t)(at + 1));
        } else {
            a->mem[at] = (uint8_t)to;
            a->mem[at + 1] = (uint8_t)(to >> 8);
        }
    }
}

#define OP(a, ...)                                                         \
    do {                                                                   \
        static const uint8_t code_[] = { __VA_ARGS__ };                    \
        for (size_t i_ = 0; i_ < sizeof code_; i_++)                       \
            b8((a), code_[i_]);                                            \
    } while (0)

/* Opcodes, by name, as the listing below uses them. */
enum {
    ADC_IMM = 0x69, ADC_ZP = 0x65, AND_IMM = 0x29, ASL_A = 0x0A,
    BCC = 0x90, BCS = 0xB0, BEQ = 0xF0, BNE = 0xD0, BPL = 0x10,
    CLC = 0x18, CMP_IMM = 0xC9, DEX = 0xCA, INC_ABS = 0xEE, INC_ZP = 0xE6,
    JMP = 0x4C, JSR = 0x20, LDA_ABS = 0xAD, LDA_ABSX = 0xBD, LDA_IMM = 0xA9,
    LDA_ZP = 0xA5, LDA_ZPX = 0xB5, LDX_IMM = 0xA2, LDY_ZP = 0xA4,
    LDY_ZPX = 0xB4, LSR_ZP = 0x46, ORA_IMM = 0x09, PHA = 0x48, PLA = 0x68,
    ROR_ZP = 0x66, RTS = 0x60, SBC_IMM = 0xE9, SBC_ZP = 0xE5, SEC = 0x38,
    STA_ABSY = 0x99, STA_ZP = 0x85, STA_ZPX = 0x95, STY_ZPX = 0x94,
    TAX = 0xAA, TYA = 0x98,
};

/* Zero page, as the Oric's BASIC lays it out where it matters: CHRGET at
 * #E2 with TXTPTR the operand of its LDA at #E9; FAC and ARG each an
 * exponent and four mantissa bytes, most significant first. */
#define CHRGET   0xE2u
#define TXTPTR   0xE9u
#define FACEXP   0xD0u
#define FACM1    0xD1u
#define ARGEXP   0xD8u
#define ARGM1    0xD9u
#define TMP      0xDDu
#define STMT     0xDEu

#define CODE     0x0400u
#define LINE     0x0800u
#define TABLE    0x0A00u

/* The digits 0-9 as packed MS BASIC floats: exponent + #80, then the
 * mantissa with its hidden top bit replaced by the sign (0, positive). */
static const uint8_t digits[10][5] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x81, 0x00, 0x00, 0x00, 0x00 },
    { 0x82, 0x00, 0x00, 0x00, 0x00 },
    { 0x82, 0x40, 0x00, 0x00, 0x00 },
    { 0x83, 0x00, 0x00, 0x00, 0x00 },
    { 0x83, 0x20, 0x00, 0x00, 0x00 },
    { 0x83, 0x40, 0x00, 0x00, 0x00 },
    { 0x83, 0x60, 0x00, 0x00, 0x00 },
    { 0x84, 0x00, 0x00, 0x00, 0x00 },
    { 0x84, 0x10, 0x00, 0x00, 0x00 },
};

/* Three statements, with spaces for CHRGET to skip: 45, 45 and 65. */
static const char line[] =
    "1+2+3+4+5+6+7+8+9:"
    "9 + 8 + 7 + 6 + 5 + 4 + 3 + 2 + 1:"
    "5+5+5+5+5+5+5+5+5+5+5+5+5";

/* The program walks the line for ever. CHRGET fetches each character;
 * a digit is loaded from the table into ARG and added into FAC; a ':' or
 * the end of the line packs FAC into the statement's result slot. The
 * add handles two non-negative numbers: zero, the swap that puts the
 * larger exponent in FAC, the bitwise alignment shift, the 32-bit add and
 * the renormalising shift on a carry. Signs, rounding and the byte-wise
 * shift of the real FADD are left out.
 *
 * It is a stand-in, written to the shape of the ROM's inner loop, not a
 * copy of it: M3 runs the real one (design.md §3.2, §13.3). */
void bench_basic_load(bench_t *b) {
    machine_clear(b);
    uint8_t *mem = b->m.ram;
    memcpy(mem + LINE, line, sizeof line);          /* with its 0 */
    memcpy(mem + TABLE, digits, sizeof digits);

    asm_t a = { .mem = mem };

    /* CHRGET: the next character in A; C clear for a digit, Z
     * set for ':' or the end of the line, spaces skipped. */
    org(&a, CHRGET);
    here(&a, L_CHRGET);
    OP(&a, INC_ZP, TXTPTR);
    br(&a, BNE, L_CHRGOT);
    OP(&a, INC_ZP, TXTPTR + 1);
    here(&a, L_CHRGOT);
    OP(&a, LDA_ABS, 0x00, 0x00);                    /* operand is TXTPTR */
    OP(&a, CMP_IMM, ':');
    br(&a, BCS, L_CHRDONE);
    OP(&a, CMP_IMM, ' ');
    br(&a, BEQ, L_CHRGET);
    OP(&a, SEC, SBC_IMM, '0', SEC, SBC_IMM, 0xD0);
    here(&a, L_CHRDONE);
    OP(&a, RTS);

    org(&a, CODE);
    here(&a, L_START);
    OP(&a, LDA_IMM, (uint8_t)(LINE - 1), STA_ZP, TXTPTR,
           LDA_IMM, (uint8_t)((LINE - 1) >> 8), STA_ZP, TXTPTR + 1,
           LDA_IMM, 0, STA_ZP, STMT);
    here(&a, L_STMT);
    OP(&a, LDA_IMM, 0, STA_ZP, FACEXP);
    here(&a, L_NEXT);
    jabs(&a, JSR, L_CHRGET);
    br(&a, BCS, L_NOTDIG);
    OP(&a, AND_IMM, 0x0F,                           /* CHRGET leaves ASCII */
           STA_ZP, TMP, ASL_A, ASL_A, ADC_ZP, TMP, TAX);   /* X = 5 digit */
    jabs(&a, JSR, L_LOADARG);
    jabs(&a, JSR, L_FADD);
    jabs(&a, JMP, L_NEXT);
    here(&a, L_NOTDIG);
    br(&a, BNE, L_NEXT);                            /* '+', and the rest */
    OP(&a, PHA);
    jabs(&a, JSR, L_STOREFAC);
    OP(&a, PLA);
    br(&a, BNE, L_STMT);                            /* ':' */
    OP(&a, INC_ABS, (uint8_t)BENCH_BASIC_PASSES, BENCH_BASIC_PASSES >> 8);
    br(&a, BNE, L_START);
    OP(&a, INC_ABS, (uint8_t)(BENCH_BASIC_PASSES + 1), (BENCH_BASIC_PASSES + 1) >> 8);
    jabs(&a, JMP, L_START);

    /* LOADARG: unpack the table's float at X into ARG. */
    here(&a, L_LOADARG);
    OP(&a, LDA_ABSX, (uint8_t)TABLE, TABLE >> 8, STA_ZP, ARGEXP,
           LDA_ABSX, (uint8_t)(TABLE + 1), TABLE >> 8, ORA_IMM, 0x80, STA_ZP, ARGM1,
           LDA_ABSX, (uint8_t)(TABLE + 2), TABLE >> 8, STA_ZP, ARGM1 + 1,
           LDA_ABSX, (uint8_t)(TABLE + 3), TABLE >> 8, STA_ZP, ARGM1 + 2,
           LDA_ABSX, (uint8_t)(TABLE + 4), TABLE >> 8, STA_ZP, ARGM1 + 3,
           RTS);

    /* STOREFAC: pack FAC into the statement's slot, and move to the next. */
    here(&a, L_STOREFAC);
    OP(&a, LDY_ZP, STMT,
           LDA_ZP, FACEXP, STA_ABSY, (uint8_t)BENCH_BASIC_RESULT, BENCH_BASIC_RESULT >> 8,
           LDA_ZP, FACM1, AND_IMM, 0x7F,
           STA_ABSY, (uint8_t)(BENCH_BASIC_RESULT + 1), BENCH_BASIC_RESULT >> 8,
           LDA_ZP, FACM1 + 1, STA_ABSY, (uint8_t)(BENCH_BASIC_RESULT + 2), BENCH_BASIC_RESULT >> 8,
           LDA_ZP, FACM1 + 2, STA_ABSY, (uint8_t)(BENCH_BASIC_RESULT + 3), BENCH_BASIC_RESULT >> 8,
           LDA_ZP, FACM1 + 3, STA_ABSY, (uint8_t)(BENCH_BASIC_RESULT + 4), BENCH_BASIC_RESULT >> 8,
           TYA, CLC, ADC_IMM, 5, STA_ZP, STMT,
           RTS);

    /* FADD: FAC += ARG. */
    here(&a, L_FADD);
    OP(&a, LDA_ZP, ARGEXP);
    br(&a, BEQ, L_FDONE);                           /* ARG is zero */
    OP(&a, LDA_ZP, FACEXP);
    br(&a, BNE, L_FNZ);
    OP(&a, LDX_IMM, 4);                             /* FAC is zero: FAC = ARG */
    here(&a, L_COPY);
    OP(&a, LDA_ZPX, ARGEXP, STA_ZPX, FACEXP, DEX);
    br(&a, BPL, L_COPY);
    OP(&a, RTS);
    here(&a, L_FNZ);
    OP(&a, SEC, LDA_ZP, FACEXP, SBC_ZP, ARGEXP);
    br(&a, BEQ, L_ADD);
    br(&a, BCS, L_SHIFT);
    OP(&a, LDX_IMM, 4);                             /* ARG is larger: swap */
    here(&a, L_SWAP);
    OP(&a, LDA_ZPX, FACEXP, LDY_ZPX, ARGEXP, STA_ZPX, ARGEXP, STY_ZPX, FACEXP, DEX);
    br(&a, BPL, L_SWAP);
    OP(&a, SEC, LDA_ZP, FACEXP, SBC_ZP, ARGEXP);
    here(&a, L_SHIFT);                              /* A = the difference */
    OP(&a, CMP_IMM, 33);
    br(&a, BCS, L_FDONE);                           /* ARG is negligible */
    OP(&a, TAX);
    here(&a, L_SH);
    OP(&a, LSR_ZP, ARGM1, ROR_ZP, ARGM1 + 1, ROR_ZP, ARGM1 + 2, ROR_ZP, ARGM1 + 3, DEX);
    br(&a, BNE, L_SH);
    here(&a, L_ADD);
    OP(&a, CLC,
           LDA_ZP, FACM1 + 3, ADC_ZP, ARGM1 + 3, STA_ZP, FACM1 + 3,
           LDA_ZP, FACM1 + 2, ADC_ZP, ARGM1 + 2, STA_ZP, FACM1 + 2,
           LDA_ZP, FACM1 + 1, ADC_ZP, ARGM1 + 1, STA_ZP, FACM1 + 1,
           LDA_ZP, FACM1,     ADC_ZP, ARGM1,     STA_ZP, FACM1);
    br(&a, BCC, L_FDONE);
    OP(&a, ROR_ZP, FACM1, ROR_ZP, FACM1 + 1, ROR_ZP, FACM1 + 2, ROR_ZP, FACM1 + 3,
           INC_ZP, FACEXP);
    here(&a, L_FDONE);
    OP(&a, RTS);

    resolve(&a);
    b->m.cpu.pc = CODE;
}

/* ---- dormann ----------------------------------------------------------- */

bool bench_dormann_load(bench_t *b, const uint8_t *image, size_t len) {
    machine_clear(b);
    if (len != ORIC_ADDR_SPACE)
        return false;
    memcpy(b->m.ram, image, len);
    b->m.cpu.pc = 0x0400;
    return true;
}

/* ---- Running ----------------------------------------------------------- */

/* Each slice's overshoot is carried as debt into the next, as the
 * machine's loop does (oric.h), so a run ends within one instruction of
 * its length. */
uint64_t bench_run(bench_t *b, uint64_t cycles) {
    uint64_t done = 0;
    uint32_t debt = 0;
    while (done < cycles) {
        uint32_t ran = oric_run(&b->m, BENCH_SLICE_CYCLES - debt);
        debt = ran - (BENCH_SLICE_CYCLES - debt);
        done += ran;
    }
    return done;
}
