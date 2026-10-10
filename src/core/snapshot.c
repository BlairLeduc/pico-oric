/* snapshot.c — our own save states (design.md §10.6; the format is in
 * snapshot.h). pico-ace's, with the Oric's fields. */

#include "snapshot.h"

#include <string.h>

#include "microdisc.h"
#include "sha1.h"

static const uint8_t magic[8] = { 'P', 'O', 'R', 'C', 'S', 'N', 'A', 'P' };

/* The state section and a piece of RAM in transit, here rather than on
 * the caller's stack: the port calls these on core 1, whose stack is the
 * SDK's 2 KiB, and the card's driver is below them. One call at a time. */
static uint8_t s_st[SNAP_STATE_LEN];
static uint8_t s_piece[256];

/* ---- CRC-32 ---------------------------------------------------------- */

uint32_t snapshot_crc32(uint32_t crc, const uint8_t *p, size_t n) {
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

/* ---- little-endian fields ---------------------------------------------- */

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, (uint16_t)v); put16(p + 2, (uint16_t)(v >> 16)); }
static void put64(uint8_t *p, uint64_t v) { put32(p, (uint32_t)v); put32(p + 4, (uint32_t)(v >> 32)); }
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t get32(const uint8_t *p) { return get16(p) | ((uint32_t)get16(p + 2) << 16); }
static uint64_t get64(const uint8_t *p) { return get32(p) | ((uint64_t)get32(p + 4) << 32); }

/* ---- the state section ------------------------------------------------ */

enum {
    /* The 6502 (m6502.h). */
    S_PC = 0, S_A = 2, S_X, S_Y, S_S, S_P,
    S_CYCLES = 7,                 /* 8 bytes                             */
    S_CPU_FLAGS = 16,             /* bits below; 15 is reserved          */
    S_I_OLD = 17, S_I_OLD_AT = 18, /* 8 bytes: the delayed IRQ poll (§5.1) */
    /* The VIA (via6522.h), T2 and the shift clock synced. */
    S_VIA = 26,                   /* orb ora ddrb ddra in_a in_b ira irb sr acr pcr ifr ier */
    S_T1_LATCH = 39, S_T2_LATCH_LO = 41, S_T1 = 42, S_T2 = 46,
    S_VIA_FLAGS = 50,             /* bits below                          */
    S_SR_HALVES = 51, S_SR_TIMER = 52,
    /* The AY (ay8912.h): the bus, then every count the sound runs on. */
    S_AY_REG = 54,                /* 16 bytes                            */
    S_AY_ADDR = 70, S_AY_MODE = 71, S_AY_FLAGS = 72, S_AY_BUS_OUT = 73, S_AY_PORT_A_IN = 74,
    S_AY_T = 75,                  /* 8 bytes                             */
    S_AY_NEXT = 83,               /* AY_GEN_COUNT x 8 bytes, signed      */
    S_AY_PER = 123,               /* AY_GEN_COUNT x 4 bytes              */
    S_AY_TONE_OUT = 143, S_AY_NOISE_PRE = 144, S_AY_RNG = 145,
    S_AY_ENV_STEP = 149, S_AY_ENV_ATTACK = 150,
    /* The ULA, the field and the bus. */
    S_ULA_MODE = 151, S_FRAME_MODE = 152,
    S_FIELDS = 153,               /* 4 bytes: the blink phase's count    */
    S_OPEN_BUS = 157,
    S_BUDGET = 158,               /* 4 bytes, signed                     */
    /* The machine it runs on. */
    S_RAM = 162,                  /* oric_ram_t                          */
    S_MACHINE = 163,              /* M_MICRODISC, M_VSYNC, or zero       */
    S_LINE_CYCLES = 164, S_LINES_50 = 166, S_LINES_60 = 168,
    S_ROM = 170,                  /* SHA-1 of the ROM, 20 bytes          */
    /* The tape trap's carry-over (tape.h): a header kept for the data
     * write that follows it, which a field boundary can fall between. */
    S_TAPE_FLAGS = 190, S_TAPE_PASS_PC = 191,
    S_TAPE_RAW = 193,             /* TAP_HEADER_LEN bytes                */
    S_TAPE_NAME_LEN = 202,
    S_TAPE_NAME = 203,            /* ORIC_TAP_NAME_MAX bytes             */
    /* The Microdisc (microdisc.h, wd1793.h), from M14, zero without it:
     * the latch, the controller's registers, its lines, the heads, and
     * the next event, which only Force Interrupt's index interrupt
     * leaves while idle. A state is refused mid-command (§10.6). */
    S_MD_LATCH = 267,
    S_WD_REGS = 268,              /* status track sector data cmd        */
    S_WD_FLAGS = 273,             /* bits below                          */
    S_WD_CYL = 274,               /* ORIC_DISC_DRIVES bytes              */
    S_WD_HEAD_UNTIL = 278,        /* 8 bytes                             */
    S_WD_DUE = 286,               /* 8 bytes                             */
    /* The vertical-sync modification's pulse (vsync.h), from M16, zero
     * without it: its lines, delay and width, which every count in the
     * state is in step with. The pulse itself is worked out again from
     * the boundary (oric_restored). */
    S_VS_LINE_50 = 294, S_VS_LINE_60 = 296, S_VS_DELAY = 298, S_VS_LOW = 300,
    S_END = 302,                  /* the rest is reserved, written zero  */
};

_Static_assert(S_VIA + 13 == S_T1_LATCH, "the VIA's registers overrun");
_Static_assert(S_AY_NEXT + 8 * AY_GEN_COUNT == S_AY_PER, "the AY's counts overrun");
_Static_assert(S_AY_PER + 4 * AY_GEN_COUNT == S_AY_TONE_OUT, "the AY's periods overrun");
_Static_assert(S_TAPE_RAW + TAP_HEADER_LEN == S_TAPE_NAME_LEN, "the tape header overruns");
_Static_assert(S_TAPE_NAME + ORIC_TAP_NAME_MAX == S_MD_LATCH, "the tape name overruns");
_Static_assert(S_WD_CYL + ORIC_DISC_DRIVES == S_WD_HEAD_UNTIL, "the heads overrun");
_Static_assert(S_END <= SNAP_STATE_LEN, "the state section has outgrown its length");

/* A press of the reset button latched and not yet taken. The line's
 * level is not saved: a press is a pulse, so it is low at every
 * boundary (oric_nmi); nor is RESET pending, which only the CPU's own
 * reset sets and clears. The IRQ lines follow from the VIA's, and
 * oric_restored puts them back (oric.h). */
#define C_NMI_PENDING  0x01u

/* The VIA's levels as they are, so that zero is not their reset value:
 * every field of version 1 is always written. */
#define V_T1_ARMED  0x01u
#define V_T2_ARMED  0x02u
#define V_PB7       0x04u
#define V_CA1       0x08u
#define V_CA2       0x10u
#define V_CB1       0x20u
#define V_CB2       0x40u

#define A_SELECTED     0x01u
#define A_DRIVING      0x02u
#define A_ENV_HOLD     0x04u
#define A_ENV_ALT      0x08u
#define A_ENV_HOLDING  0x10u

#define M_MICRODISC    0x01u
#define M_VSYNC        0x02u

#define W_INTRQ        0x01u
#define W_DRQ          0x02u
#define W_TYPE1        0x04u
#define W_HOLD_INTRQ   0x08u
#define W_INDEX_INTRQ  0x10u
#define W_STEP_IN      0x20u

#define T_HEADER_KEPT  0x01u
#define T_PASS         0x02u

/* What is fitted besides the ROM and RAM: the Microdisc (M14), the
 * vertical-sync modification (M16). A state is for one set of them. */
static uint8_t machine_of(const oric_t *m) {
    return (uint8_t)((m->cfg.microdisc ? M_MICRODISC : 0) | (m->cfg.vsync_hack ? M_VSYNC : 0));
}

static void rom_hash(const oric_t *m, uint8_t digest[SHA1_DIGEST_LEN]) {
    sha1(m->rom, ORIC_ROM_SIZE, digest);
}

static void state_encode(const oric_t *m, uint8_t st[SNAP_STATE_LEN]) {
    memset(st, 0, SNAP_STATE_LEN);

    const m6502_t *c = &m->cpu;
    put16(st + S_PC, c->pc);
    st[S_A] = c->a; st[S_X] = c->x; st[S_Y] = c->y; st[S_S] = c->s; st[S_P] = c->p;
    put64(st + S_CYCLES, c->cycles);
    st[S_CPU_FLAGS] = c->nmi_pending ? C_NMI_PENDING : 0;
    st[S_I_OLD] = c->i_old;
    put64(st + S_I_OLD_AT, c->i_old_at);

    /* T2 and the shift clock as of now, not as of their last event. */
    via6522_t via_now = m->via;
    via6522_sync(&via_now);
    const via6522_t *v = &via_now;
    uint8_t *q = st + S_VIA;
    *q++ = v->orb; *q++ = v->ora; *q++ = v->ddrb; *q++ = v->ddra;
    *q++ = v->in_a; *q++ = v->in_b; *q++ = v->ira; *q++ = v->irb;
    *q++ = v->sr; *q++ = v->acr; *q++ = v->pcr; *q++ = v->ifr; *q++ = v->ier;
    put16(st + S_T1_LATCH, v->t1_latch);
    st[S_T2_LATCH_LO] = v->t2_latch_lo;
    put32(st + S_T1, (uint32_t)v->t1);
    put32(st + S_T2, (uint32_t)v->t2);
    st[S_VIA_FLAGS] = (uint8_t)((v->t1_armed ? V_T1_ARMED : 0) | (v->t2_armed ? V_T2_ARMED : 0) |
                                (v->pb7 ? V_PB7 : 0) | (v->ca1 ? V_CA1 : 0) | (v->ca2 ? V_CA2 : 0) |
                                (v->cb1 ? V_CB1 : 0) | (v->cb2 ? V_CB2 : 0));
    st[S_SR_HALVES] = v->sr_halves;
    put16(st + S_SR_TIMER, (uint16_t)v->sr_timer);

    const ay8912_t *ay = &m->ay;
    memcpy(st + S_AY_REG, ay->reg, AY_REG_COUNT);
    st[S_AY_ADDR] = ay->addr;
    st[S_AY_MODE] = ay->mode;
    st[S_AY_FLAGS] = (uint8_t)((ay->selected ? A_SELECTED : 0) | (ay->driving ? A_DRIVING : 0) |
                               (ay->env_hold ? A_ENV_HOLD : 0) | (ay->env_alt ? A_ENV_ALT : 0) |
                               (ay->env_holding ? A_ENV_HOLDING : 0));
    st[S_AY_BUS_OUT] = ay->bus_out;
    st[S_AY_PORT_A_IN] = ay->port_a_in;
    put64(st + S_AY_T, ay->t);
    for (unsigned g = 0; g < AY_GEN_COUNT; g++) {
        put64(st + S_AY_NEXT + 8u * g, (uint64_t)ay->next[g]);
        put32(st + S_AY_PER + 4u * g, ay->per[g]);
    }
    st[S_AY_TONE_OUT] = ay->tone_out;
    st[S_AY_NOISE_PRE] = ay->noise_pre;
    put32(st + S_AY_RNG, ay->rng);
    st[S_AY_ENV_STEP] = (uint8_t)ay->env_step;
    st[S_AY_ENV_ATTACK] = ay->env_attack;

    st[S_ULA_MODE] = m->ula_mode;
    st[S_FRAME_MODE] = m->frame_mode;
    put32(st + S_FIELDS, m->fields);
    st[S_OPEN_BUS] = m->open_bus;
    put32(st + S_BUDGET, (uint32_t)m->budget);

    st[S_RAM] = (uint8_t)m->cfg.ram;
    st[S_MACHINE] = machine_of(m);
    put16(st + S_LINE_CYCLES, m->cfg.line_cycles);
    put16(st + S_LINES_50, m->cfg.lines_50hz);
    put16(st + S_LINES_60, m->cfg.lines_60hz);
    rom_hash(m, st + S_ROM);

    const tape_t *t = &m->tape;
    st[S_TAPE_FLAGS] = (uint8_t)((t->header_kept ? T_HEADER_KEPT : 0) | (t->pass ? T_PASS : 0));
    put16(st + S_TAPE_PASS_PC, t->pass_pc);
    memcpy(st + S_TAPE_RAW, t->raw, TAP_HEADER_LEN);
    st[S_TAPE_NAME_LEN] = t->name_len;
    memcpy(st + S_TAPE_NAME, t->name, ORIC_TAP_NAME_MAX);

    if (m->cfg.vsync_hack) {
        put16(st + S_VS_LINE_50, m->cfg.vsync_line_50hz);
        put16(st + S_VS_LINE_60, m->cfg.vsync_line_60hz);
        put16(st + S_VS_DELAY, m->cfg.vsync_delay);
        put16(st + S_VS_LOW, m->cfg.vsync_low);
    }

    if (!m->cfg.microdisc) return;
    const wd1793_t *f = &m->fdc;
    st[S_MD_LATCH] = m->md_latch;
    st[S_WD_REGS + 0] = f->status;
    st[S_WD_REGS + 1] = f->track;
    st[S_WD_REGS + 2] = f->sector;
    st[S_WD_REGS + 3] = f->data;
    st[S_WD_REGS + 4] = f->cmd;
    st[S_WD_FLAGS] = (uint8_t)((f->intrq ? W_INTRQ : 0) | (f->drq ? W_DRQ : 0) |
                               (f->type1 ? W_TYPE1 : 0) | (f->hold_intrq ? W_HOLD_INTRQ : 0) |
                               (f->index_intrq ? W_INDEX_INTRQ : 0) | (f->step_in ? W_STEP_IN : 0));
    for (unsigned d = 0; d < ORIC_DISC_DRIVES; d++) st[S_WD_CYL + d] = f->drv[d].cyl;
    put64(st + S_WD_HEAD_UNTIL, f->head_until);
    put64(st + S_WD_DUE, f->due);
}

/* ---- the stream -------------------------------------------------------- */

snap_status_t snapshot_save(const oric_t *m, snap_write_fn write, void *ctx) {
    if (m->tape.op != TAPE_NONE || oric_disc_busy(m)) return SNAP_BUSY;

    uint8_t *st = s_st;
    state_encode(m, st);

    /* The CRC goes in the header, ahead of what it covers; the machine is
     * the buffer, and nothing changes it in between. */
    uint32_t crc = snapshot_crc32(0, st, SNAP_STATE_LEN);
    crc = snapshot_crc32(crc, m->ram, sizeof m->ram);

    uint8_t hdr[SNAP_HEADER_LEN];
    memcpy(hdr, magic, sizeof magic);
    put16(hdr + 8, SNAP_VERSION);
    put16(hdr + 10, SNAP_HEADER_LEN);
    put32(hdr + 12, SNAP_PAYLOAD_LEN);
    put32(hdr + 16, crc);

    if (!write(ctx, hdr, sizeof hdr)) return SNAP_IO;
    if (!write(ctx, st, SNAP_STATE_LEN)) return SNAP_IO;
    for (uint32_t off = 0; off < sizeof m->ram; off += ORIC_PAGE_SIZE * 16u)
        if (!write(ctx, m->ram + off, ORIC_PAGE_SIZE * 16u)) return SNAP_IO;
    return SNAP_OK;
}

static snap_status_t read_header(snap_read_fn read, void *ctx, uint32_t *crc) {
    uint8_t hdr[SNAP_HEADER_LEN];
    if (!read(ctx, hdr, sizeof hdr)) return SNAP_IO;
    if (memcmp(hdr, magic, sizeof magic) != 0) return SNAP_NOT_SNAPSHOT;
    if (get16(hdr + 8) > SNAP_VERSION) return SNAP_NEWER;
    if (get16(hdr + 8) == 0 || get16(hdr + 10) != SNAP_HEADER_LEN ||
        get32(hdr + 12) != SNAP_PAYLOAD_LEN) {
        return SNAP_NOT_SNAPSHOT;
    }
    *crc = get32(hdr + 16);
    return SNAP_OK;
}

static void info_of(const uint8_t st[SNAP_STATE_LEN], snap_info_t *info) {
    if (!info) return;
    info->rom = romset_identify_digest(st + S_ROM, ORIC_ROM_SIZE);
    info->ram = st[S_RAM] == ORIC_RAM_16K ? ORIC_RAM_16K : ORIC_RAM_48K;
    info->microdisc = (st[S_MACHINE] & M_MICRODISC) != 0;
    info->vsync_hack = (st[S_MACHINE] & M_VSYNC) != 0;
}

/* Could a machine have saved this? A file passes its CRC whoever wrote
 * it, and the chips trust their fields: an AY period of 0 divides by
 * zero, a register number past 15 indexes past the AY's masks, and a T1
 * far below zero, an AY clock far behind the CPU's or a large budget
 * keeps core 0 in one loop for minutes (Codex's review of PR #11). So
 * every field the code divides, indexes or loops by must be in the range
 * a running machine keeps it in. */
static bool plausible(const uint8_t st[SNAP_STATE_LEN]) {
    if (st[S_RAM] > ORIC_RAM_48K || st[S_SR_HALVES] > 16u || (st[S_MACHINE] & ~(M_MICRODISC | M_VSYNC)) ||
        st[S_TAPE_NAME_LEN] > ORIC_TAP_NAME_MAX || st[S_ULA_MODE] > 7u || st[S_FRAME_MODE] > 7u)
        return false;

    /* The VIA's counters after due() and a sync: from just below zero
     * to a full count past the latch. */
    int32_t t1 = (int32_t)get32(st + S_T1), t2 = (int32_t)get32(st + S_T2);
    if (t1 < -2 || t1 > 0x10002 || t2 < -2 || t2 > 0x10002) return false;

    /* The AY: a register below 16, a bus mode, a period of at least one
     * tick and no more than a register can make, the envelope's step and
     * direction, a live LFSR. */
    if (st[S_AY_ADDR] >= AY_REG_COUNT || st[S_AY_MODE] > AY_BUS_LATCH) return false;
    static const uint32_t per_max[AY_GEN_COUNT] = { 0xFFFu, 0xFFFu, 0xFFFu, 0x1Fu, 0x1FFFEu };
    for (unsigned g = 0; g < AY_GEN_COUNT; g++) {
        uint32_t p = get32(st + S_AY_PER + 4u * g);
        if (p < 1u || p > per_max[g]) return false;
    }
    if (st[S_AY_ENV_STEP] > 15u || (st[S_AY_ENV_ATTACK] != 0 && st[S_AY_ENV_ATTACK] != 0x0Fu) ||
        st[S_AY_NOISE_PRE] > 1u || st[S_AY_TONE_OUT] > 7u)
        return false;
    uint32_t rng = get32(st + S_AY_RNG);
    if (rng == 0 || rng > 0x1FFFFu) return false;

    /* The AY is brought up to date at every sound write and every
     * field's end, so it is never more than a field behind the CPU and
     * never ahead; a second is generous. Its next ticks are within a
     * period of its clock, but for a held envelope's, which mean
     * nothing; 2^32 ticks keeps the arithmetic far from overflow. */
    uint64_t cycles = get64(st + S_CYCLES), t = get64(st + S_AY_T);
    if (t > cycles || cycles - t > ORIC_CPU_HZ) return false;
    int64_t k0 = (int64_t)(t / AY_TICK_CYCLES);
    for (unsigned g = 0; g < AY_GEN_COUNT; g++) {
        int64_t nx = (int64_t)get64(st + S_AY_NEXT + 8u * g);
        if (nx < k0 - ((int64_t)1 << 32) || nx > k0 + ((int64_t)1 << 32)) return false;
    }

    /* The budget is the last field's overshoot: never positive, and no
     * more than an interrupt's entry and the longest instruction. */
    int32_t budget = (int32_t)get32(st + S_BUDGET);
    return budget <= 0 && budget >= -64;
}

/* Would this state resume on this machine? The ROM and the RAM first,
 * which the user can change and the refusal names. */
static snap_status_t compatible(const oric_t *m, const uint8_t st[SNAP_STATE_LEN]) {
    if (!plausible(st)) return SNAP_NOT_SNAPSHOT;
    uint8_t rom[SHA1_DIGEST_LEN];
    rom_hash(m, rom);
    if (memcmp(rom, st + S_ROM, sizeof rom) != 0) return SNAP_OTHER_ROM;
    if (st[S_RAM] != (uint8_t)m->cfg.ram) return SNAP_OTHER_RAM;
    if (st[S_MACHINE] != machine_of(m)) return SNAP_OTHER_MACHINE;
    if (get16(st + S_LINE_CYCLES) != m->cfg.line_cycles ||
        get16(st + S_LINES_50) != m->cfg.lines_50hz || get16(st + S_LINES_60) != m->cfg.lines_60hz)
        return SNAP_OTHER_FIELD;
    if (m->cfg.vsync_hack &&
        (get16(st + S_VS_LINE_50) != m->cfg.vsync_line_50hz ||
         get16(st + S_VS_LINE_60) != m->cfg.vsync_line_60hz ||
         get16(st + S_VS_DELAY) != m->cfg.vsync_delay || get16(st + S_VS_LOW) != m->cfg.vsync_low))
        return SNAP_OTHER_FIELD;
    return SNAP_OK;
}

snap_status_t snapshot_check(const oric_t *m, snap_read_fn read, void *ctx, snap_info_t *info) {
    uint32_t want;
    snap_status_t r = read_header(read, ctx, &want);
    if (r != SNAP_OK) return r;

    uint8_t *st = s_st;
    if (!read(ctx, st, SNAP_STATE_LEN)) return SNAP_IO;
    uint32_t crc = snapshot_crc32(0, st, SNAP_STATE_LEN);
    for (uint32_t left = ORIC_ADDR_SPACE; left; left -= sizeof s_piece) {
        if (!read(ctx, s_piece, sizeof s_piece)) return SNAP_IO;
        crc = snapshot_crc32(crc, s_piece, sizeof s_piece);
    }
    if (crc != want) return SNAP_CORRUPT;
    info_of(st, info);
    return compatible(m, st);
}

/* The Microdisc's fields onto m. The discs are the port's and stay in
 * their drives; the heads are where the state left them, so the track in
 * hand is dropped. Without the Microdisc these bytes are zero and are not
 * read: its reset values stay. */
static void md_decode(oric_t *m, const uint8_t *st) {
    wd1793_t *f = &m->fdc;
    m->md_latch = st[S_MD_LATCH];
    f->status = st[S_WD_REGS + 0];
    f->track = st[S_WD_REGS + 1];
    f->sector = st[S_WD_REGS + 2];
    f->data = st[S_WD_REGS + 3];
    f->cmd = st[S_WD_REGS + 4];
    uint8_t wf = st[S_WD_FLAGS];
    f->intrq = wf & W_INTRQ; f->drq = wf & W_DRQ; f->type1 = wf & W_TYPE1;
    f->hold_intrq = wf & W_HOLD_INTRQ; f->index_intrq = wf & W_INDEX_INTRQ;
    f->step_in = wf & W_STEP_IN;
    for (unsigned d = 0; d < ORIC_DISC_DRIVES; d++) f->drv[d].cyl = st[S_WD_CYL + d];
    f->head_until = get64(st + S_WD_HEAD_UNTIL);
    f->due = get64(st + S_WD_DUE);
    f->phase = 0;
    f->buf_ok = false;
}

snap_status_t snapshot_load(oric_t *m, snap_read_fn read, void *ctx) {
    if (m->tape.op != TAPE_NONE || oric_disc_busy(m)) return SNAP_BUSY;
    uint32_t want;
    snap_status_t r = read_header(read, ctx, &want);
    if (r != SNAP_OK) return r;
    uint8_t *st = s_st;
    if (!read(ctx, st, SNAP_STATE_LEN)) return SNAP_IO;
    r = compatible(m, st);
    if (r != SNAP_OK) return r;

    /* From here the machine changes. snapshot_check has read these same
     * bytes and found them whole; a read failing now is the card going
     * away between the passes, and leaves a machine that needs a power-on. */
    if (!read(ctx, m->ram, sizeof m->ram)) return SNAP_IO;

    m6502_t *c = &m->cpu;
    c->pc = get16(st + S_PC);
    c->a = st[S_A]; c->x = st[S_X]; c->y = st[S_Y]; c->s = st[S_S]; c->p = st[S_P];
    c->cycles = get64(st + S_CYCLES);
    c->irq_lines = 0;
    c->nmi_pending = (st[S_CPU_FLAGS] & C_NMI_PENDING) != 0;
    c->nmi_line = false;
    c->reset_pending = false;
    c->i_old = st[S_I_OLD];
    c->i_old_at = get64(st + S_I_OLD_AT);
    c->io_at = 0;

    via6522_t *v = &m->via;
    const uint8_t *q = st + S_VIA;
    v->orb = *q++; v->ora = *q++; v->ddrb = *q++; v->ddra = *q++;
    v->in_a = *q++; v->in_b = *q++; v->ira = *q++; v->irb = *q++;
    v->sr = *q++; v->acr = *q++; v->pcr = *q++; v->ifr = *q++; v->ier = *q++;
    v->t1_latch = get16(st + S_T1_LATCH);
    v->t2_latch_lo = st[S_T2_LATCH_LO];
    v->t1 = (int32_t)get32(st + S_T1);
    v->t2 = (int32_t)get32(st + S_T2);
    uint8_t vf = st[S_VIA_FLAGS];
    v->t1_armed = vf & V_T1_ARMED; v->t2_armed = vf & V_T2_ARMED;
    v->pb7 = vf & V_PB7;
    v->ca1 = vf & V_CA1; v->ca2 = vf & V_CA2; v->cb1 = vf & V_CB1; v->cb2 = vf & V_CB2;
    v->sr_halves = st[S_SR_HALVES];
    v->sr_timer = (int16_t)get16(st + S_SR_TIMER);
    v->ca2_pulses = v->cb2_pulses = 0;
    via6522_rearm(v);
    m->via_early = 0;

    ay8912_t *ay = &m->ay;
    memcpy(ay->reg, st + S_AY_REG, AY_REG_COUNT);
    ay->addr = st[S_AY_ADDR];
    ay->mode = st[S_AY_MODE];
    uint8_t af = st[S_AY_FLAGS];
    ay->selected = af & A_SELECTED; ay->driving = af & A_DRIVING;
    ay->env_hold = af & A_ENV_HOLD; ay->env_alt = af & A_ENV_ALT;
    ay->env_holding = af & A_ENV_HOLDING;
    ay->bus_out = st[S_AY_BUS_OUT];
    ay->port_a_in = st[S_AY_PORT_A_IN];
    ay->t = get64(st + S_AY_T);
    for (unsigned g = 0; g < AY_GEN_COUNT; g++) {
        ay->next[g] = (int64_t)get64(st + S_AY_NEXT + 8u * g);
        ay->per[g] = get32(st + S_AY_PER + 4u * g);
    }
    ay->tone_out = st[S_AY_TONE_OUT];
    ay->noise_pre = st[S_AY_NOISE_PRE];
    ay->rng = get32(st + S_AY_RNG);
    ay->env_step = (int8_t)st[S_AY_ENV_STEP];
    ay->env_attack = st[S_AY_ENV_ATTACK];

    m->ula_mode = st[S_ULA_MODE];
    m->frame_mode = st[S_FRAME_MODE];
    m->fields = get32(st + S_FIELDS);
    m->open_bus = st[S_OPEN_BUS];
    m->budget = (int32_t)get32(st + S_BUDGET);

    tape_t *t = &m->tape;
    t->header_kept = (st[S_TAPE_FLAGS] & T_HEADER_KEPT) != 0;
    t->pass = (st[S_TAPE_FLAGS] & T_PASS) != 0;
    t->pass_pc = get16(st + S_TAPE_PASS_PC);
    memcpy(t->raw, st + S_TAPE_RAW, TAP_HEADER_LEN);
    t->name_len = st[S_TAPE_NAME_LEN];
    memcpy(t->name, st + S_TAPE_NAME, ORIC_TAP_NAME_MAX);

    if (m->cfg.microdisc) md_decode(m, st);

    oric_restored(m);
    return SNAP_OK;
}

const char *snapshot_status_str(snap_status_t st) {
    switch (st) {
    case SNAP_OK:            return "OK";
    case SNAP_IO:            return "read or write failed";
    case SNAP_NOT_SNAPSHOT:  return "not a save state";
    case SNAP_NEWER:         return "from a newer version";
    case SNAP_CORRUPT:       return "damaged (CRC)";
    case SNAP_OTHER_RAM:     return "for the other RAM";
    case SNAP_OTHER_ROM:     return "for another ROM";
    case SNAP_OTHER_MACHINE: return "for another machine";
    case SNAP_OTHER_FIELD:   return "another field timing";
    case SNAP_BUSY:          return "the tape or the disc is busy";
    }
    return "?";
}
