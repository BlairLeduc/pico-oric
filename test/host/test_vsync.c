/* test_vsync.c — the vertical-sync modification (vsync.h, design.md §15.2
 * M16). No ROM: a program of its own in RAM, so CI runs it.
 *
 * The edges are found by execution. A loop polls the CB1 flag, and each
 * read of it is placed by the cycle of its access: the first read that
 * sees an edge and the read before it bracket the edge's cycle. Run at
 * every phase of the loop against the field, the brackets meet in one
 * cycle, which must be the one vsync.h says, for the fall and the rise,
 * at 50 Hz and at 60. A pulse moved by a cycle, at either end, is the
 * control that must fail.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "oric.h"
#include "snap_util.h"
#include "snapshot.h"
#include "test_util.h"
#include "via6522.h"

static oric_t s_m, s_m2;

#define PROG     0x0400u
#define HANDLER  0x0500u
#define RING     0x0600u
#define LOOP_AT  0x0480u   /* the poll's LDA IFR */

/* A machine with the modification, a ROM of nothing but the vectors,
 * reset to PROG and interrupting to HANDLER, the programs in RAM after
 * power-on and, for 60 Hz, a 60 Hz text attribute at the screen's first
 * cell, which the mode scan finds at the first field's end (§7.4). */
static uint8_t s_rom[ORIC_ROM_SIZE];

static void make(oric_t *m, const oric_config_t *cfg, bool hz50) {
    s_rom[0x3FFCu] = (uint8_t)PROG;
    s_rom[0x3FFDu] = (uint8_t)(PROG >> 8);
    s_rom[0x3FFEu] = (uint8_t)HANDLER;
    s_rom[0x3FFFu] = (uint8_t)(HANDLER >> 8);
    oric_init(m, cfg);
    oric_load_rom(m, s_rom, sizeof s_rom);
    oric_power_on(m);
    if (!hz50) m->ram[0xBB80u] = 0x18u;
}

static void config(oric_config_t *cfg) {
    oric_config_default(cfg);
    cfg->vsync_hack = true;
}

static void put(oric_t *m, uint16_t at, const uint8_t *b, size_t n) {
    memcpy(&m->ram[at], b, n);
}

/* `delay` cycles of NOPs and LDA zp at #0410, then into the poll. */
static void put_delay(oric_t *m, unsigned delay) {
    uint8_t d[64];
    size_t n = 0;
    /* 2s and 3s to make `delay`; 1 cannot be made, but 1 + 9 can. */
    if (delay == 1) delay = 10;
    while (delay >= 2 && delay != 3) { d[n++] = 0xEA; delay -= 2; }
    if (delay == 3) { d[n++] = 0xA5; d[n++] = 0x00; }
    d[n++] = 0x4C;
    d[n++] = (uint8_t)LOOP_AT;
    d[n++] = (uint8_t)(LOOP_AT >> 8);
    put(m, PROG + 0x10u, d, n);
}

/* The poll: CB1's active edge from `pcr`, the flag cleared, the delay,
 * then LDA IFR : AND #10 : BEQ until it is set, clear it and go round
 * again from the delay. */
static void poll_program(oric_t *m, uint8_t pcr, unsigned delay) {
    uint8_t head[] = { 0xA9, pcr, 0x8D, 0x0C, 0x03,      /* LDA #pcr : STA PCR */
                       0xAD, 0x00, 0x03,                 /* LDA ORB            */
                       0x4C, 0x00, 0x00 };               /* JMP #0410          */
    head[9] = 0x10u;
    head[10] = (uint8_t)(PROG >> 8);
    put(m, PROG, head, sizeof head);
    put_delay(m, delay);
    const uint8_t loop[] = { 0xAD, 0x0D, 0x03, 0x29, 0x10, 0xF0, 0xF9,   /* LDA IFR : AND : BEQ */
                             0xAD, 0x00, 0x03,                           /* LDA ORB             */
                             0x4C, 0x10, (uint8_t)(PROG >> 8) };         /* JMP the delay       */
    put(m, LOOP_AT, loop, sizeof loop);
}

/* The cycle of the edge due near `expect`, from one run's reads: the
 * first that saw it, and the one before. */
typedef struct { uint64_t lo, hi; } bracket_t;

static bool bracket(oric_t *m, uint64_t expect, unsigned delay, bracket_t *b) {
    /* Whole fields up to the one the edge is in, so the mode scan sets
     * each field's length as the firmware's loop does; then single
     * steps to 300 cycles short of the edge, where the poll is entered
     * afresh, the flag cleared and the delay run, so that each delay is
     * a phase of the loop against the edge (edge_at). */
    while (m->cpu.cycles + oric_field_cycles(m) + 64u < expect) oric_run_field(m);
    while (m->cpu.cycles + 300u < expect) oric_run(m, 1);
    put_delay(m, delay);
    m->cpu.pc = PROG + 5u;
    uint64_t prev = 0;
    while (m->cpu.cycles < expect + 400u) {
        bool lda = m->cpu.pc == LOOP_AT;
        uint64_t at = m->cpu.cycles + 4u;   /* LDA abs reads in its fourth cycle */
        oric_run(m, 1);
        if (!lda) continue;
        if (m->cpu.a & VIA_INT_CB1) {
            b->lo = prev + 1u;
            b->hi = at;
            return prev != 0;
        }
        prev = at;
    }
    return false;
}

/* The edge's cycle, from runs at every phase of the nine-cycle loop;
 * false unless the brackets meet in one cycle. */
static bool edge_at(const oric_config_t *cfg, bool hz50, uint8_t pcr, uint64_t expect, uint64_t *at) {
    uint64_t lo = 0, hi = UINT64_MAX;
    for (unsigned d = 0; d < 9; d++) {
        make(&s_m, cfg, hz50);
        poll_program(&s_m, pcr, 0);
        bracket_t b;
        if (!bracket(&s_m, expect, d, &b)) return false;
        if (b.lo > lo) lo = b.lo;
        if (b.hi < hi) hi = b.hi;
    }
    *at = lo;
    return lo == hi;
}

/* Where vsync.h puts field f's edges, from the default configuration:
 * the first field at 50 Hz from the first instruction, after the reset's
 * seven cycles, the rest at the frequency asked for. */
#define FIRST_INSN 7u

static uint64_t field_start(bool hz50, unsigned f) {
    return FIRST_INSN + (f == 0 ? 0 : 19968u + (uint64_t)(f - 1u) * (hz50 ? 19968u : 16896u));
}
static uint64_t fall_of(bool hz50, unsigned f) {
    return field_start(hz50, f) + 64u * (f && !hz50 ? 234u : 256u) + 12u;
}

static int test_edges(void) {
    oric_config_t cfg;
    config(&cfg);
    make(&s_m, &cfg, true);
    CHECK(s_m.cpu.cycles == FIRST_INSN, "the first instruction at cycle %llu",
          (unsigned long long)s_m.cpu.cycles);
    for (int hz = 0; hz < 2; hz++) {
        bool hz50 = hz == 1;
        for (unsigned f = 2; f <= 3; f++) {
            uint64_t fall = fall_of(hz50, f), rise = fall + 260u, got = 0;
            CHECK(edge_at(&cfg, hz50, 0x00, fall, &got) && got == fall,
                  "%d Hz field %u: the fall at %llu, expected %llu", hz50 ? 50 : 60, f,
                  (unsigned long long)got, (unsigned long long)fall);
            CHECK(edge_at(&cfg, hz50, 0x10, rise, &got) && got == rise,
                  "%d Hz field %u: the rise at %llu, expected %llu", hz50 ? 50 : 60, f,
                  (unsigned long long)got, (unsigned long long)rise);
        }
    }

    /* The controls: the fall a cycle late, the width kept so the rise is
     * too; then the rise alone a cycle late. */
    uint64_t got = 0;
    oric_config_t late = cfg;
    late.vsync_delay = 13;
    CHECK(edge_at(&late, true, 0x00, fall_of(true, 2), &got) && got == fall_of(true, 2) + 1u,
          "control: a delay of 13 should move the fall by a cycle, to %llu", (unsigned long long)got);
    late = cfg;
    late.vsync_low = 261;
    CHECK(edge_at(&late, false, 0x10, fall_of(false, 2) + 260u, &got) &&
              got == fall_of(false, 2) + 261u,
          "control: a width of 261 should move the rise by a cycle, to %llu", (unsigned long long)got);
    return test_failures;
}

/* Without the modification CB1 is the tape's: nothing moves it here. */
static int test_off(void) {
    oric_config_t cfg;
    oric_config_default(&cfg);
    make(&s_m, &cfg, true);
    poll_program(&s_m, 0x00, 0);
    for (int f = 0; f < 4; f++) oric_run_field(&s_m);
    CHECK(s_m.vs.due == UINT64_MAX && s_m.vs.pulses == 0 && s_m.via.cb1,
          "off: no pulse (due %llu, %u pulses)", (unsigned long long)s_m.vs.due, s_m.vs.pulses);
    return test_failures;
}

/* A tape played by hand, with and without the modification: with it,
 * CB1 changes twice a field, at the sync, and the tape plays on into
 * nothing (cassette.h). */
static const uint8_t s_tap[] = {
    0x16, 0x16, 0x16, 0x16, 0x24, 0x00, 0x00, 0x80, 0x00, 0x05, 0x40, 0x05, 0x00, 0x00, 'V', 0x00,
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,
    27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50,
    51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65,
};

static unsigned cb1_changes(oric_t *m, unsigned fields) {
    unsigned n = 0;
    bool level = m->via.cb1;
    uint64_t end = m->cpu.cycles + (uint64_t)fields * oric_field_cycles(m);
    while (m->cpu.cycles < end) {
        oric_run(m, 1);
        if (m->via.cb1 != level) { level = m->via.cb1; n++; }
    }
    return n;
}

static int test_tape_disconnected(void) {
    for (int hack = 0; hack < 2; hack++) {
        oric_config_t cfg;
        config(&cfg);
        cfg.vsync_hack = hack != 0;
        make(&s_m, &cfg, true);
        poll_program(&s_m, 0x00, 0);
        oric_cassette_insert(&s_m, s_tap, sizeof s_tap);
        oric_cassette_play(&s_m, true);
        unsigned n = cb1_changes(&s_m, 4);
        CHECK(oric_cassette_running(&s_m) && s_m.cas.edges > 250u, "the tape plays (%u edges)",
              s_m.cas.edges);
        if (hack)
            CHECK(n == 8, "with the hack CB1 is the sync's alone: %u changes in four fields", n);
        else
            CHECK(n == s_m.cas.edges, "control: without it CB1 is the tape's: %u changes for %u edges",
                  n, s_m.cas.edges);
    }
    return test_failures;
}

/* An interrupt-driven reader, run by oric_run_field's long slices and by
 * single steps: the slice ends at each edge, so the handler is entered at
 * the same instruction either way. It keeps the interrupted PC's low byte
 * in a ring, which a main loop of 2, 3 and 6-cycle instructions makes
 * tell the cycle. */
static void irq_program(oric_t *m, uint8_t pcr) {
    const uint8_t init[] = {
        0xA9, pcr, 0x8D, 0x0C, 0x03,        /* LDA #pcr : STA PCR      */
        0xA9, 0x7F, 0x8D, 0x0E, 0x03,        /* LDA #7F : STA IER, all off */
        0xA9, 0x90, 0x8D, 0x0E, 0x03,        /* LDA #90 : STA IER, CB1  */
        0xAD, 0x00, 0x03, 0x58,              /* LDA ORB : CLI           */
        0xEA, 0xA5, 0x00, 0xEE, 0x00, 0x07,  /* #0413 NOP : LDA zp : INC #0700 */
        0x4C, 0x13, 0x04,                    /* JMP #0413               */
    };
    put(m, PROG, init, sizeof init);
    const uint8_t h[] = {
        0x48, 0x8A, 0x48, 0xBA,              /* PHA : TXA : PHA : TSX   */
        0xBD, 0x04, 0x01,                    /* LDA #0104,X: PC low     */
        0xA6, 0x10, 0x9D, 0x00, 0x06,        /* LDX #10 : STA RING,X    */
        0xE6, 0x10, 0xAD, 0x00, 0x03,        /* INC #10 : LDA ORB       */
        0x68, 0xAA, 0x68, 0x40,              /* PLA : TAX : PLA : RTI   */
    };
    put(m, HANDLER, h, sizeof h);
}

static int test_slices(void) {
    oric_config_t cfg;
    config(&cfg);
    for (int pcr = 0; pcr < 2; pcr++) {
        make(&s_m, &cfg, false);
        irq_program(&s_m, pcr ? 0x10 : 0x00);
        oric_copy(&s_m2, &s_m);
        for (int f = 0; f < 40; f++) oric_run_field(&s_m);
        /* The same cycles an instruction at a time, the boundaries the
         * other's, so the mode scan happens at the same points. */
        for (int f = 0; f < 40; f++) {
            int32_t want = (int32_t)oric_field_cycles(&s_m2) + s_m2.budget;
            uint64_t end = s_m2.cpu.cycles + (uint64_t)(want > 0 ? want : 0);
            uint32_t done = 0;
            while (s_m2.cpu.cycles < end) done += oric_run(&s_m2, 1);
            s_m2.budget = want - (int32_t)done;
            s_m2.fields++;
            s_m2.frame_mode = s_m2.ula_mode;
            s_m2.ula_mode = ula_scan_mode(oric_video_window(&s_m2), s_m2.frame_mode);
            vsync_field(&s_m2);
        }
        CHECK(s_m.ram[0x10] >= 39 && s_m.ram[0x10] == s_m2.ram[0x10] &&
                  memcmp(&s_m.ram[RING], &s_m2.ram[RING], 256) == 0 && s_m.cpu.cycles == s_m2.cpu.cycles,
              "PCR %02X: the handler entered %u and %u times, at the same instructions",
              pcr ? 0x10 : 0x00, s_m.ram[0x10], s_m2.ram[0x10]);
    }
    return test_failures;
}

/* A state saved with the modification resumes its pulse where it was;
 * one is refused by the other setting, by name, and by another pulse,
 * and loads into a machine powered on as it says. */
static mem_t s_snap;
static oric_t s_ahead;

/* s_snap into a machine that was `from`, powered on as the state says
 * (snapshot_machine): it loads, and runs on with s_m, the original, to
 * the same interrupts. s_m runs on too. */
static int switched(const snap_info_t *info, const oric_config_t *from, const char *what) {
    oric_config_t as = *from;
    snapshot_machine(info, &as);
    make(&s_m2, &as, false);
    CHECK(mem_load(&s_snap, &s_m2) == SNAP_OK, "%s: loads as the state's machine: %s", what,
          snapshot_status_str(mem_check(&s_snap, &s_m2, NULL)));
    CHECK(s_m2.cfg.vsync_hack == info->vsync_hack, "%s: the hack as the state had it", what);
    /* The original from where it was saved, again. */
    make(&s_ahead, &s_m.cfg, false);
    CHECK(mem_load(&s_snap, &s_ahead) == SNAP_OK, "%s: and into its own", what);
    for (int f = 0; f < 20; f++) {
        oric_run_field(&s_ahead);
        oric_run_field(&s_m2);
    }
    CHECK(s_ahead.ram[0x10] == s_m2.ram[0x10] && memcmp(&s_ahead.ram[RING], &s_m2.ram[RING], 256) == 0 &&
              s_m2.cpu.cycles == s_ahead.cpu.cycles && s_m2.vs.due == s_ahead.vs.due,
          "%s: the same interrupts: %u and %u", what, s_ahead.ram[0x10], s_m2.ram[0x10]);
    return 0;
}

static int test_snapshot(void) {
    oric_config_t cfg;
    config(&cfg);
    make(&s_m, &cfg, false);
    irq_program(&s_m, 0x00);
    for (int f = 0; f < 5; f++) oric_run_field(&s_m);
    memset(&s_snap, 0, sizeof s_snap);
    CHECK(mem_save(&s_snap, &s_m) == SNAP_OK, "save");

    make(&s_m2, &cfg, false);
    CHECK(mem_load(&s_snap, &s_m2) == SNAP_OK, "load");
    CHECK(s_m2.vs.due == s_m.vs.due && s_m2.vs.frame_at == s_m.vs.frame_at && !s_m2.vs.low,
          "the pulse is worked out again: due %llu against %llu", (unsigned long long)s_m2.vs.due,
          (unsigned long long)s_m.vs.due);
    for (int f = 0; f < 20; f++) {
        oric_run_field(&s_m);
        oric_run_field(&s_m2);
    }
    CHECK(s_m.ram[0x10] == s_m2.ram[0x10] && memcmp(&s_m.ram[RING], &s_m2.ram[RING], 256) == 0 &&
              s_m2.cpu.cycles == s_m.cpu.cycles,
          "the restored machine takes the same interrupts: %u and %u", s_m.ram[0x10], s_m2.ram[0x10]);

    oric_config_t off = cfg;
    off.vsync_hack = false;
    make(&s_m2, &off, false);
    snap_info_t info;
    memset(&info, 0, sizeof info);
    CHECK(mem_check(&s_snap, &s_m2, &info) == SNAP_OTHER_MACHINE && info.vsync_hack && !info.microdisc,
          "a state with the hack is refused without it, named: %s",
          snapshot_status_str(mem_check(&s_snap, &s_m2, NULL)));
    switched(&info, &off, "without the hack");
    oric_config_t shape = cfg;
    shape.vsync_delay = 13;
    make(&s_m2, &shape, false);
    CHECK(mem_check(&s_snap, &s_m2, &info) == SNAP_OTHER_MACHINE, "and by another pulse: %s",
          snapshot_status_str(mem_check(&s_snap, &s_m2, NULL)));
    switched(&info, &shape, "with another pulse");

    /* And the other way: a state without it, into a machine with it. */
    make(&s_m, &off, false);
    irq_program(&s_m, 0x00);
    oric_run_field(&s_m);
    memset(&s_snap, 0, sizeof s_snap);
    CHECK(mem_save(&s_snap, &s_m) == SNAP_OK, "save without");
    make(&s_m2, &cfg, false);
    memset(&info, 0, sizeof info);
    CHECK(mem_check(&s_snap, &s_m2, &info) == SNAP_OTHER_MACHINE && !info.vsync_hack,
          "a state without the hack is refused with it: %s",
          snapshot_status_str(mem_check(&s_snap, &s_m2, NULL)));
    switched(&info, &cfg, "with the hack");
    return test_failures;
}

int main(void) {
    if (test_edges()) TEST_DONE();
    test_off();
    test_tape_disconnected();
    test_slices();
    test_snapshot();
    TEST_DONE();
}
