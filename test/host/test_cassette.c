/* test_cassette.c — the signal against the ROMs' own CSAVE and CLOAD
 * (design.md §10.4, §15.2 M13; EL §8.3).
 *
 * Needs basic10.rom and basic11b.rom in roms/ (§13.3); without them it
 * reports skipped, which is not a pass. On the 48K machine of each ROM:
 *
 *   edges     CSAVE with the trap off, PB7 recorded an instruction at a
 *             time, each edge placed on its cycle by T1's count; and the
 *             same CSAVE trapped, for its .tap. The player's walk of
 *             that .tap must be the recording, half-cycle for half-cycle
 *             from the first byte, at both speeds, and in 1.0 for names
 *             either side of each step in its gap (cassette.c).
 *   cload     the .tap in the deck, CLOAD with the trap off: the ROM
 *             reads the signal, at both speeds, at both polarities,
 *             and the program runs. A tape of two files: CLOAD"B" passes
 *             A by, and CLOAD"" twice from the start loads A then B, the
 *             deck holding its place while the relay is open.
 *   record    CSAVE with the trap off and the recorder armed: its file
 *             is the trap's, byte for byte, at both speeds. One cut off
 *             by RESET part-way is not kept, and is counted.
 *   cues      the trap with the signal on, served between fields as the
 *             port serves it: CLOAD's find puts the tape in the deck and
 *             is declined, and the ROM reads the signal; CSAVE's header
 *             write arms the recorder and is declined, and the ROM writes
 *             the signal; the data steps are never requests.
 *
 *   test_cassette --write DIR   also writes each recording, as the
 *                               recorder made it, to DIR/<machine>-
 *                               <speed>.tap, for tools/trace-diff.py tape
 *
 * Without the ROMs, so that CI runs it, one more: the CB1 flag is set on
 * the edge's cycle, seen by the first read of IFR at or after it.
 */

#include <string.h>

#include "cassette.h"
#include "guest.h"
#include "tap.h"
#include "tape.h"
#include "test_util.h"

static guest_t g;
static const char *s_write_dir;
static oric_t s_start, s_run, s_walker;

typedef struct {
    uint16_t setup, cleanup;
} rom_pcs_t;

static const rom_pcs_t pcs11 = { 0xE76Au, 0xE93Du };
static const rom_pcs_t pcs10 = { 0xE6CAu, 0xE804u };

#define TAPE_MAX 65536u

typedef struct {
    uint8_t buf[TAPE_MAX];
    size_t  len;
} tape_buf_t;

#define HALVES_MAX 400000u
static uint32_t s_rec[HALVES_MAX], s_walk[HALVES_MAX];

/* ---- running the machine ----------------------------------------------------- */

/* An instruction at a time to `stop`, with the keyboard's field every
 * field's worth of cycles; PB7's half-cycles into s_rec if `halves`,
 * each edge on its cycle by T1's count (cassette.c's rec_sample). The
 * trapped save is served into `out`. */
static bool run_to(oric_t *m, keymatrix_t *k, uint16_t stop, bool traps, tape_buf_t *out,
                   size_t *halves, uint64_t max) {
    m->cfg.tape_traps = traps;
    uint64_t end = m->cpu.cycles + max, field = m->cpu.cycles, last = 0, seen = m->cpu.cycles;
    bool lvl = (via6522_pb_out(&m->via) & 0x80u) != 0;
    static int16_t pcm[ORIC_AUDIO_BUF_LEN];
    if (halves) *halves = 0;
    while (m->cpu.pc != stop) {
        if (m->cpu.cycles >= end) return false;
        if (m->cpu.cycles >= field) {
            if (k) keymatrix_field(k, m);
            (void)oric_audio_drain(m, pcm, ORIC_AUDIO_BUF_LEN);
            field += oric_field_cycles(m);
        }
        oric_run(m, 1);
        const tape_t *t = oric_tape_pending(m);
        if (t && t->op == TAPE_SAVE && out) {
            uint8_t hdr[TAP_HEADER_MAX];
            size_t n = tap_encode_header(t->raw, t->name, t->name_len, hdr);
            memcpy(out->buf + out->len, hdr, n);
            out->len += n;
            for (uint32_t i = 0; i < t->len; i++)
                out->buf[out->len++] = oric_peek(m, (uint16_t)(t->start + i));
            oric_tape_save_end(m);
        } else if (t) {
            oric_tape_decline(m);
        }
        if (!halves) continue;
        uint64_t prev = seen;
        seen = m->cpu.cycles;
        bool now = (via6522_pb_out(&m->via) & 0x80u) != 0;
        if (now == lvl) continue;
        lvl = now;
        /* T1's underflow, if it reloaded in this instruction; else a
         * write that changed the pin, now. */
        uint64_t at = m->cpu.cycles;
        int64_t since = (int64_t)m->via.t1_latch + 1 - m->via.t1;
        if ((m->via.acr & VIA_ACR_T1_PB7) && since <= (int64_t)(at - prev))
            at = (uint64_t)((int64_t)at - since);
        if (last && *halves < HALVES_MAX) s_rec[(*halves)++] = (uint32_t)(at - last);
        last = at;
    }
    return true;
}

static void enter(void) {
    keymatrix_event(&g.k, KEY_EV_PRESSED, PICOCALC_KEY_ENTER);
    keymatrix_event(&g.k, KEY_EV_RELEASED, PICOCALC_KEY_ENTER);
}

/* Type `cmd` and Return, and run to the tape set-up, into s_start. */
static bool start_at_setup(const rom_pcs_t *r, const char *cmd) {
    guest_type(&g, cmd);
    enter();
    if (!run_to(&g.m, &g.k, r->setup, false, NULL, NULL, 50000000u)) return false;
    oric_copy(&s_start, &g.m);
    return true;
}

/* Fields until the relay has closed and opened again, then a few more
 * for the ROM to finish. False if it never did. */
static bool run_load(int max_fields) {
    bool closed = false;
    for (int i = 0; i < max_fields; i++) {
        guest_fields(&g, 1);
        if (g.m.cas.motor) closed = true;
        else if (closed) {
            guest_fields(&g, 20);
            return true;
        }
    }
    return false;
}

/* The port's part with the signal on (tape.h): a find puts `deck` in
 * the cassette, a header write arms the recorder into `rec`; both are
 * declined. Anything else is a request the signal does not make. */
static unsigned s_finds, s_records, s_others;

static void signal_port(oric_t *m, const tape_buf_t *deck, uint8_t *rec, uint32_t cap) {
    const tape_t *t = oric_tape_pending(m);
    if (!t) return;
    if (t->op == TAPE_FIND) {
        s_finds++;
        if (!m->cas.loaded) oric_cassette_insert(m, deck->buf, (uint32_t)deck->len);
    } else if (t->op == TAPE_RECORD) {
        s_records++;
        oric_cassette_record(m, rec, cap);
    } else {
        s_others++;
    }
    oric_tape_decline(m);
}

/* Fields with the port between them, until the relay has closed and
 * opened again. */
static bool run_signal(const tape_buf_t *deck, uint8_t *rec, uint32_t cap, int max_fields) {
    bool closed = false;
    for (int i = 0; i < max_fields; i++) {
        guest_fields(&g, 1);
        signal_port(&g.m, deck, rec, cap);
        if (g.m.cas.motor) closed = true;
        else if (closed) {
            guest_fields(&g, 20);
            return true;
        }
    }
    return false;
}

static const char *rom_name(rom_id_t r) {
    return r == ROM_BASIC10 ? "1.0 48K" : "1.1 48K";
}

/* ---- the cases -------------------------------------------------------------------- */

/* CSAVE`cmd`'s signal, recorded, against the player's walk of the .tap
 * the trap makes of the same save. The trap's .tap is left in *tap. */
static int edges_case(const char *n, const rom_pcs_t *r, const char *cmd, bool slow,
                      tape_buf_t *tap) {
    CHECK(start_at_setup(r, cmd), "%s: %s never reached the set-up", n, cmd);

    size_t nrec = 0;
    oric_copy(&s_run, &s_start);
    CHECK(run_to(&s_run, NULL, r->cleanup, false, NULL, &nrec, 400000000u),
          "%s: %s, the ROM alone, never finished", n, cmd);

    tap->len = 0;
    oric_copy(&s_run, &s_start);
    CHECK(run_to(&s_run, NULL, r->cleanup, true, tap, NULL, 50000000u),
          "%s: %s, trapped, never finished", n, cmd);
    CHECK(tap->len > 0, "%s: %s: the trap saved nothing", n, cmd);
    /* The trapped machine goes on, as the firmware's would. */
    {
        keymatrix_t k = g.k;
        oric_copy(&g.m, &s_run);
        g.k = k;
        guest_fields(&g, 20);
    }

    /* From the first byte, which starts a period before the start
     * bit's long half (two, fast); before it, T1 was started part-way
     * into a period of the ROM's 100 Hz tick. */
    size_t first = 0;
    while (first < nrec && s_rec[first] != CAS_LONG) first++;
    CHECK(first < nrec, "%s: %s: no start bit in the recording", n, cmd);
    first -= slow ? 1u : 2u;

    oric_copy(&s_walker, &s_start);
    oric_cassette_insert(&s_walker, tap->buf, (uint32_t)tap->len);
    size_t nwalk = 0;
    uint32_t t;
    bool from = true;
    while (nwalk < HALVES_MAX && cassette_walk(&s_walker, from, slow, &t)) {
        s_walk[nwalk++] = t;
        from = false;
    }
    /* The recording stops at the clean-up, part-way into the last stop
     * bit; the walk goes on into the gap after the file. */
    size_t want = nrec - first;
    CHECK(nwalk >= want, "%s: %s: the walk has %zu halves, the recording %zu", n, cmd,
          nwalk, want);
    for (size_t i = 0; i < want; i++)
        CHECK(s_walk[i] == s_rec[first + i], "%s: %s: half %zu of %zu: walk %u, ROM %u", n,
              cmd, i, want, (unsigned)s_walk[i], (unsigned)s_rec[first + i]);
    printf("%s: %s: %zu halves, edge for edge\n", n, cmd, want);
    return 0;
}

/* NEW, then CLOAD`cmd` off the signal of `tap` with the trap off, CB1
 * starting at `level`; then RUN must print `says`. */
static int cload_case(const char *n, const char *cmd, const tape_buf_t *tap, bool level,
                      const char *says, int max_fields) {
    guest_type(&g, "NEW\r");
    g.m.cfg.tape_traps = false;
    oric_cassette_insert(&g.m, tap->buf, (uint32_t)tap->len);
    g.m.cas.level = level;
    guest_type(&g, cmd);
    enter();
    CHECK(run_load(max_fields), "%s: %s: the relay never closed and opened", n, cmd);
    CHECK(!g.m.cas.playing, "%s: %s: the deck plays on with the relay open", n, cmd);
    guest_type(&g, "RUN\r");
    guest_fields(&g, 20);
    if (guest_find_row(&g.m, says, 1) < 0) guest_dump(&g.m, stdout);
    CHECK(guest_find_row(&g.m, says, 1) >= 0, "%s: %s: RUN does not say %s", n, cmd, says);
    return 0;
}

static int machine(rom_id_t rom) {
    const char *n = rom_name(rom);
    const rom_pcs_t *r = rom == ROM_BASIC11 ? &pcs11 : &pcs10;
    static tape_buf_t fast, slow, a, b, both;

    CHECK(guest_boot(&g, rom, ORIC_RAM_48K), "%s: no Ready", n);
    guest_type(&g, "10 PRINT \"TAPE OK\"\r");
    guest_type(&g, "20 REM A SECOND LINE\r");

    /* ---- edges ------------------------------------------------------------ */
    if (edges_case(n, r, "CSAVE\"AB\"", false, &fast)) return 1;
    if (edges_case(n, r, "CSAVE\"AB\",S", true, &slow)) return 1;
    if (rom == ROM_BASIC10) {
        /* Either side of each step in 1.0's gap: 9/10, 20/21 letters. */
        static tape_buf_t x;
        const char *names[] = { "CSAVE\"\"", "CSAVE\"ABCDEFGHI\"", "CSAVE\"ABCDEFGHIJ\"",
                                "CSAVE\"ABCDEFGHIJKLMNOPQRST\"",
                                "CSAVE\"ABCDEFGHIJKLMNOPQRSTU\"" };
        for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
            if (edges_case(n, r, names[i], false, &x)) return 1;
    } else {
        static tape_buf_t x;
        if (edges_case(n, r, "CSAVE\"ABCDEFGHIJKLMNOP\"", false, &x)) return 1;
    }

    /* ---- cload ------------------------------------------------------------ */
    if (cload_case(n, "CLOAD\"\"", &fast, true, "TAPE OK", 400)) return 1;
    if (cload_case(n, "CLOAD\"\"", &fast, false, "TAPE OK", 400)) return 1;
    if (cload_case(n, "CLOAD\"AB\",S", &slow, true, "TAPE OK", 2500)) return 1;

    guest_type(&g, "NEW\r");
    guest_type(&g, "10 PRINT \"FILE A\"\r");
    if (edges_case(n, r, "CSAVE\"A\"", false, &a)) return 1;
    guest_type(&g, "NEW\r");
    guest_type(&g, "10 PRINT \"FILE B\"\r");
    if (edges_case(n, r, "CSAVE\"B\"", false, &b)) return 1;
    memcpy(both.buf, a.buf, a.len);
    memcpy(both.buf + a.len, b.buf, b.len);
    both.len = a.len + b.len;
    if (cload_case(n, "CLOAD\"B\"", &both, true, "FILE B", 800)) return 1;
    if (cload_case(n, "CLOAD\"\"", &both, true, "FILE A", 400)) return 1;
    /* The deck kept its place: the next file is B, without a rewind. */
    guest_type(&g, "NEW\r");
    guest_type(&g, "CLOAD\"\"");
    enter();
    CHECK(run_load(400), "%s: the second CLOAD: the relay never closed and opened", n);
    guest_type(&g, "RUN\r");
    guest_fields(&g, 20);
    CHECK(guest_find_row(&g.m, "FILE B", 1) >= 0, "%s: the second CLOAD did not load B", n);

    /* ---- record ------------------------------------------------------------- */
    static uint8_t rec[TAPE_MAX];
    guest_type(&g, "NEW\r");
    guest_type(&g, "10 PRINT \"TAPE OK\"\r");
    guest_type(&g, "20 REM A SECOND LINE\r");
    for (int s = 0; s < 2; s++) {
        const char *cmd = s ? "CSAVE\"AB\",S" : "CSAVE\"AB\"";
        const tape_buf_t *want = s ? &slow : &fast;
        oric_cassette_insert(&g.m, NULL, 0);
        oric_cassette_record(&g.m, rec, sizeof rec);
        g.m.cfg.tape_traps = false;
        guest_type(&g, cmd);
        enter();
        CHECK(run_load(s ? 2500 : 400), "%s: %s recorded: the relay never closed and opened", n,
              cmd);
        uint32_t from, to;
        CHECK(oric_cassette_unsaved(&g.m, &from, &to), "%s: %s: nothing recorded (%u errors)", n,
              cmd, (unsigned)g.m.cas.rec.errors);
        CHECK(from == 0 && to == want->len && memcmp(rec, want->buf, to) == 0,
              "%s: %s: the recording (%u bytes) is not the trap's file (%zu)", n, cmd,
              (unsigned)to, want->len);
        CHECK(g.m.cas.rec.errors == 0, "%s: %s: %u errors recording", n, cmd,
              (unsigned)g.m.cas.rec.errors);
        if (s_write_dir) {
            char path[512];
            snprintf(path, sizeof path, "%s/%s-%s.tap", s_write_dir,
                     rom == ROM_BASIC11 ? "atmos" : "oric1", s ? "slow" : "fast");
            FILE *f = fopen(path, "wb");
            CHECK(f && fwrite(rec, 1, to, f) == to && fclose(f) == 0, "cannot write %s", path);
            printf("%s: %u bytes recorded\n", path, (unsigned)to);
        }
        oric_cassette_saved(&g.m);
        oric_cassette_record(&g.m, NULL, 0);
    }

    /* Cut off part-way: RESET opens the relay in the data. */
    oric_cassette_record(&g.m, rec, sizeof rec);
    guest_type(&g, "CSAVE\"AB\"");
    enter();
    bool closed = false;
    for (int i = 0; i < 400 && !closed; i++) {
        guest_fields(&g, 1);
        closed = g.m.cas.rec.fstate == 4u;   /* RF_DATA: in the data */
    }
    CHECK(closed, "%s: the cut-off save never reached its data", n);
    uint32_t errors = g.m.cas.rec.errors;
    oric_reset(&g.m);
    guest_fields(&g, 200);
    uint32_t from, to;
    CHECK(!oric_cassette_unsaved(&g.m, &from, &to), "%s: a save cut off by RESET was kept", n);
    CHECK(g.m.cas.rec.errors == errors + 1u, "%s: the cut-off save was not counted", n);
    oric_cassette_record(&g.m, NULL, 0);

    /* ---- cues ---------------------------------------------------------------- */
    g.m.cfg.tape_traps = true;
    g.m.cfg.tape_signal = true;
    s_finds = s_records = s_others = 0;
    oric_cassette_insert(&g.m, NULL, 0);
    /* RESET was a cold start: the program again, as fast saved it. */
    guest_type(&g, "NEW\r");
    guest_type(&g, "10 PRINT \"TAPE OK\"\r");
    guest_type(&g, "20 REM A SECOND LINE\r");
    guest_type(&g, "CSAVE\"AB\"");
    enter();
    CHECK(run_signal(&fast, rec, sizeof rec, 400), "%s: cued CSAVE: the relay never opened", n);
    CHECK(s_records == 1 && s_finds == 0 && s_others == 0,
          "%s: cued CSAVE: %u records, %u finds, %u others", n, s_records, s_finds, s_others);
    CHECK(oric_cassette_unsaved(&g.m, &from, &to) && to == fast.len &&
          memcmp(rec, fast.buf, to) == 0, "%s: cued CSAVE: the recording is not the file", n);
    oric_cassette_saved(&g.m);
    oric_cassette_record(&g.m, NULL, 0);

    guest_type(&g, "NEW\r");
    s_finds = s_records = s_others = 0;
    uint32_t served = g.m.tape.served;
    guest_type(&g, "CLOAD\"\"");
    enter();
    CHECK(run_signal(&fast, rec, sizeof rec, 400), "%s: cued CLOAD: the relay never opened", n);
    CHECK(s_finds == 1 && s_records == 0 && s_others == 0 && g.m.tape.served == served,
          "%s: cued CLOAD: %u finds, %u records, %u others, %u served", n, s_finds, s_records,
          s_others, (unsigned)(g.m.tape.served - served));
    guest_type(&g, "RUN\r");
    guest_fields(&g, 20);
    CHECK(guest_find_row(&g.m, "TAPE OK", 1) >= 0, "%s: cued CLOAD: RUN does not say TAPE OK", n);
    g.m.cfg.tape_signal = false;
    return 0;
}

/* A loop in RAM reading IFR and then ORB, which clears the CB1 flag,
 * while the deck plays by hand: each rising edge's flag must be seen by
 * the first IFR read whose access is at or after its cycle (bus.c's
 * catch-up), not an instruction later. */
static int flag_on_cycle(void) {
    static oric_t m;
    static const uint8_t tape[] = { 0x16, 0x24, 0, 0, 0, 0, 0x05, 0x01, 0x05, 0x00, 0, 'A', 0, 0x55 };
    static const uint8_t prog[] = { 0xAD, 0x0D, 0x03, 0xAD, 0x00, 0x03, 0x4C, 0x00, 0x04 };
    oric_config_t cfg;
    oric_config_default(&cfg);
    oric_init(&m, &cfg);
    memcpy(&m.ram[0x0400], prog, sizeof prog);
    m.cpu.pc = 0x0400u;
    m.cpu.p |= M6502_I;
    via6522_write(&m.via, VIA_PCR, 0x10u);     /* CB1 on its rising edge */
    oric_cassette_insert(&m, tape, sizeof tape);
    oric_cassette_play(&m, true);
    unsigned rises = 0;
    while (rises < 1000u && m.cas.playing) {
        uint16_t pc = m.cpu.pc;
        uint64_t at = m.cpu.cycles + 4u;        /* LDA abs reads on its 4th cycle */
        uint64_t edge = m.cas.next;
        bool rising = !m.cas.level;
        oric_run(&m, 1);
        if (pc != 0x0400u) continue;
        bool seen = (m.cpu.a & VIA_INT_CB1) != 0;
        if (rising && edge <= at) {
            CHECK(seen, "a rise at cycle %llu was not seen by the read at %llu",
                  (unsigned long long)edge, (unsigned long long)at);
            rises++;
        }
        if (test_failures) return 1;
    }
    CHECK(rises == 1000u, "only %u rising edges", rises);
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "--write") == 0) s_write_dir = argv[2];
    if (flag_on_cycle()) return 1;
    const char *dir;
    if (!guest_find_roms(&dir)) {
        printf("skipped: basic10.rom and basic11b.rom are not both in %s\n", dir);
        return TEST_SKIP_CODE;
    }
    for (int r = ROM_BASIC10; r <= ROM_BASIC11; r++)
        if (machine((rom_id_t)r)) return 1;
    TEST_DONE();
}
