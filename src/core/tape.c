/* tape.c — tape phase 1: the traps (tape.h, design.md §10.3).
 *
 * The addresses are the stock ROMs', read off them 2026-10-08 (§16):
 *
 *   1.1  #E4AC JSR #E735 (sync), then #E6C9 a byte at a time until #24,
 *        nine into #02B0 down to #02A8, #02B1 cleared by the STX between,
 *        then the name into #0293, 16 bytes kept, until a zero, stored
 *        at #E4D3. #E4D6 prints "Found", #E4D9 compares the name with
 *        #027F (#E790), and a different one goes round again.
 *        #E4E0 the data: #33 from #02A9, Y = 0, then #E6C9, LDX #025B
 *        (verify), STA (#33),Y or CMP and count into #025C, and #E505
 *        JSR #E56C, which compares #33 with #02AB, steps it, and the
 *        loop ends with carry set. #E607 the header out: #E75A's sync,
 *        #24, #02B0 down to #02A8, #027F to its zero, the zero, at #E628
 *        a delay. #E62E the data out, round #E63F's JSR #E56C.
 *   1.0  the same steps in one load routine from #E4A8 and one save from
 *        #E57B: the header into #66 down to #5E, the name into #49 with
 *        nothing kept back, the asked-for name at #35, the addresses at
 *        #5F and #61, the step at #E554; no verify, no error flag.
 *
 * Each byte routine leaves the byte in #2F: the input shifts it in, the
 * output shifts it out, leaving zero. Of the registers the byte routines
 * leave, a step's resume uses only those set below; the ROM overwrites
 * the rest, flags included, before it reads them, which planting each
 * wrong showed (test_tape, 2026-10-08).
 */

#include "tape.h"

#include <string.h>

#include "bus.h"
#include "oric.h"

static const tape_rom_t rom11 = {
    .find_pc = 0xE4ACu,  .find_resume = 0xE4D3u,
    .load_pc = 0xE4E0u,  .load_resume = 0xE505u, .load_wait = 0xE4ECu,
    .hsave_pc = 0xE607u, .hsave_resume = 0xE628u,
    .dsave_pc = 0xE62Eu, .dsave_resume = 0xE63Fu,
    .header = 0x02B0u, .name = 0x0293u, .name_cap = 16u, .want = 0x027Fu,
    .start = 0x02A9u, .end = 0x02ABu, .errors = 0x02B1u,
    .verify = 0x025Bu, .verify_errors = 0x025Cu, .slow = 0x024Du,
};

static const tape_rom_t rom10 = {
    .find_pc = 0xE4B2u,  .find_resume = 0xE4D0u,
    .load_pc = 0xE4EBu,  .load_resume = 0xE4FDu, .load_wait = 0xE4F5u,
    .hsave_pc = 0xE57Bu, .hsave_resume = 0xE59Au,
    .dsave_pc = 0xE5A7u, .dsave_resume = 0xE5B6u,
    .header = 0x0066u, .name = 0x0049u, .name_cap = 0xFFu, .want = 0x0035u,
    .start = 0x005Fu, .end = 0x0061u, .errors = 0,
    .verify = 0, .verify_errors = 0, .slow = 0x0067u,
};

uint8_t tape_pc_lo[256] = {
    [0xACu] = 1, [0xE0u] = 1, [0x07u] = 1, [0x2Eu] = 1,    /* 1.1 */
    [0xB2u] = 1, [0xEBu] = 1, [0x7Bu] = 1, [0xA7u] = 1,    /* 1.0 */
};

void tape_reset(oric_t *m) {
    m->tape.op = TAPE_NONE;
    m->tape.pass = false;
    m->tape.header_kept = false;
}

void tape_rom_loaded(oric_t *m, const uint8_t *image, size_t len) {
    rom_id_t id = romset_identify(image, len);
    m->tape.rom = id == ROM_BASIC11 ? &rom11 : id == ROM_BASIC10 ? &rom10 : NULL;
    tape_reset(m);
}

/* ---- guest access ------------------------------------------------------ */

static uint16_t peek16(const oric_t *m, uint16_t a) {
    return (uint16_t)(oric_peek(m, a) | (oric_peek(m, (uint16_t)(a + 1u)) << 8));
}

/* Through the bus, as the ROM's STA: ROM ignores it, page #03 is the VIA. */
static void poke(oric_t *m, uint16_t a, uint8_t v) {
    bus_write(m, a, v);
}

static void poke16(oric_t *m, uint16_t a, uint16_t v) {
    poke(m, a, (uint8_t)v);
    poke(m, (uint16_t)(a + 1u), (uint8_t)(v >> 8));
}

/* The name at `a`, to its zero, at most ORIC_TAP_NAME_MAX bytes. In zero
 * page it wraps as LDA zp,X does. */
static uint8_t name_at(const oric_t *m, uint16_t a, uint8_t *out) {
    unsigned n = 0;
    for (; n < ORIC_TAP_NAME_MAX; n++) {
        uint16_t at = a < 0x100u ? (uint8_t)(a + n) : (uint16_t)(a + n);
        uint8_t c = oric_peek(m, at);
        if (!c) break;
        out[n] = c;
    }
    return (uint8_t)n;
}

/* ---- the trap ----------------------------------------------------------- */

static void keep_header(oric_t *m) {
    tape_t *t = &m->tape;
    const tape_rom_t *r = t->rom;
    for (unsigned i = 0; i < TAP_HEADER_LEN; i++)
        t->raw[i] = oric_peek(m, (uint16_t)(r->header - i));
    t->name_len = name_at(m, r->want, t->name);
    t->header_kept = true;

    /* On from where the name's zero has gone out. The data write that
     * follows, the trap's or the ROM's, leaves #2F as the ROM would. */
    m->cpu.pc = r->hsave_resume;
}

bool tape_at(oric_t *m) {
    tape_t *t = &m->tape;
    m6502_t *c = &m->cpu;

    /* The reset button or the RESET line wins: it is taken at this
     * boundary, and the request goes with the program that made it. */
    if (c->reset_pending || c->nmi_pending) {
        t->op = TAPE_NONE;
        t->pass = false;
        return false;
    }
    if (t->op != TAPE_NONE) return true;

    const tape_rom_t *r = t->rom;
    uint16_t pc = c->pc;
    if (!r || !m->cfg.tape_traps || !(m->page_flags[pc >> 8] & PAGE_ROM)) return false;
    if (pc != r->find_pc && pc != r->load_pc && pc != r->hsave_pc && pc != r->dsave_pc)
        return false;
    if (t->pass && pc == t->pass_pc) {
        t->pass = false;
        if (pc == r->dsave_pc) t->header_kept = false;
        return false;
    }

    t->slow = oric_peek(m, r->slow) != 0;
    if (pc == r->hsave_pc) {
        keep_header(m);
        return false;
    }
    if (pc == r->dsave_pc && !t->header_kept) {
        /* The header went out through the ROM's own routine. */
        return false;
    }

    t->pass_pc = pc;
    if (pc == r->find_pc) {
        uint8_t n = name_at(m, r->want, t->want);
        t->want[n] = 0;
        t->op = TAPE_FIND;
        return true;
    }
    t->start = peek16(m, r->start);
    t->end = peek16(m, r->end);
    t->len = tap_data_len(t->start, t->end);
    t->done = 0;
    t->mismatches = 0;
    t->verify = pc == r->load_pc && r->verify && oric_peek(m, r->verify) != 0;
    t->op = pc == r->load_pc ? TAPE_LOAD : TAPE_SAVE;
    return true;
}

const tape_t *oric_tape_pending(const oric_t *m) {
    return m->tape.op != TAPE_NONE ? &m->tape : NULL;
}

void oric_tape_decline(oric_t *m) {
    tape_t *t = &m->tape;
    if (t->op == TAPE_NONE) return;
    t->op = TAPE_NONE;
    t->pass = true;
    t->declined++;
}

void oric_tape_give_up(oric_t *m) {
    tape_t *t = &m->tape;
    if (t->op != TAPE_NONE) t->declined++;
    t->op = TAPE_NONE;
    t->pass = false;
    oric_nmi(m);
}

/* ---- load ---------------------------------------------------------------- */

void oric_tape_found(oric_t *m, const tap_header_t *h) {
    tape_t *t = &m->tape;
    if (t->op != TAPE_FIND) return;
    const tape_rom_t *r = t->rom;
    m6502_t *c = &m->cpu;

    for (unsigned i = 0; i < TAP_HEADER_LEN; i++)
        poke(m, (uint16_t)(r->header - i), h->raw[i]);
    /* 1.1's STX #02B1 between the #24 and the header, X being the sync
     * finder's zero; no byte after it had a parity error. */
    if (r->errors) poke(m, r->errors, 0);

    /* The name, as the ROM's loop stores it: X counts the bytes kept,
     * and the ROM's own STA puts the zero after them. */
    uint8_t x = 0;
    for (unsigned i = 0; i < h->name_len; i++) {
        if (x >= r->name_cap) continue;
        poke(m, (uint16_t)(r->name + x), h->name[i]);
        x++;
    }
    c->a = 0;
    c->x = x;
    poke(m, TAPE_ZP_BYTE, 0);
    c->pc = r->find_resume;

    t->op = TAPE_NONE;
    t->served++;
}

bool oric_tape_load_wants(const oric_t *m) {
    const tape_t *t = &m->tape;
    return t->op == TAPE_LOAD && t->done < t->len;
}

void oric_tape_load_data(oric_t *m, const uint8_t *src, size_t n) {
    tape_t *t = &m->tape;
    for (size_t i = 0; i < n && oric_tape_load_wants(m); i++) {
        uint16_t a = (uint16_t)(t->start + t->done);
        if (t->verify) {
            if (oric_peek(m, a) != src[i]) t->mismatches++;
        } else {
            poke(m, a, src[i]);
        }
        t->last = src[i];
        t->done++;
    }
}

void oric_tape_load_end(oric_t *m) {
    tape_t *t = &m->tape;
    if (t->op != TAPE_LOAD) return;
    const tape_rom_t *r = t->rom;
    m6502_t *c = &m->cpu;

    if (t->mismatches) {
        /* INC #025C, carrying into #025D, once for each (#E4FD). */
        uint16_t e = (uint16_t)(peek16(m, r->verify_errors) + t->mismatches);
        poke16(m, r->verify_errors, e);
    }
    if (t->done) poke(m, TAPE_ZP_BYTE, t->last);
    c->y = 0;
    if (t->done == t->len) {
        /* At the last address, the ROM steps past it and ends. 1.1's
         * LDX #025B is the last thing before. */
        poke16(m, TAPE_ZP_PTR, tap_last(t->start, t->end));
        if (r->verify) c->x = oric_peek(m, r->verify);
        c->pc = r->load_resume;
    } else {
        /* The tape ran out: the loop waits for the next byte. */
        poke16(m, TAPE_ZP_PTR, (uint16_t)(t->start + t->done));
        c->pc = r->load_wait;
    }
    t->op = TAPE_NONE;
    t->served++;
}

/* ---- save ---------------------------------------------------------------- */

void oric_tape_save_end(oric_t *m) {
    tape_t *t = &m->tape;
    if (t->op != TAPE_SAVE) return;
    const tape_rom_t *r = t->rom;
    m6502_t *c = &m->cpu;

    uint16_t last = tap_last(t->start, t->end);
    poke16(m, TAPE_ZP_PTR, last);
    c->y = 0;
    poke(m, TAPE_ZP_BYTE, 0);
    c->pc = r->dsave_resume;

    t->header_kept = false;
    t->op = TAPE_NONE;
    t->served++;
}
