/* test_tape.c — the tape trap against the ROMs' own routines
 * (design.md §10.3, §15.2 M10; EL §8.2).
 *
 * Needs basic10.rom and basic11b.rom in roms/ (§13.3); without them it
 * reports skipped, which is not a pass. Each case types its command,
 * runs to the ROM's tape set-up (#E76A in 1.1, #E6CA in 1.0), whose SEI
 * holds IRQs off until the clean-up (#E93D, #E804), and copies the
 * machine there. From the copy it runs to the clean-up up to three ways:
 *
 *   plain    the ROM's routines alone. A save needs nothing more: the ROM
 *            times its bits on T1 and writes them to PB7, which nothing
 *            reads. A load would wait for CB1, so it has no plain run.
 *   hooked   the ROM's routines with only their byte-level ones served
 *            from the tape's bytes (sync and byte, in and out), each
 *            leaving the CPU as tape.c says the ROM's does.
 *   trapped  the trap, served from the same bytes as the port would.
 *
 * and requires the same machine at the clean-up: every byte of RAM below
 * the ROM but page #03 and the stack below SP (tape.h's deliberate
 * differences), and A, X, Y, S, P and the PC. A save's bytes, hooked and
 * trapped, must be the same file. Then the trapped machine runs on: a
 * loaded program lists and runs.
 */

#include <string.h>

#include "guest.h"
#include "tap.h"
#include "tape.h"
#include "test_util.h"

static guest_t g;
static oric_t s_start, s_run, s_ref;

typedef struct {
    uint16_t setup, cleanup;
    uint16_t sync_in, byte_in, sync_out, byte_out;
} rom_pcs_t;

static const rom_pcs_t pcs11 = { 0xE76Au, 0xE93Du, 0xE735u, 0xE6C9u, 0xE75Au, 0xE65Eu };
static const rom_pcs_t pcs10 = { 0xE6CAu, 0xE804u, 0xE696u, 0xE630u, 0xE6BAu, 0xE5C6u };

/* ---- a deck in memory, served as tapeio serves the card's ---------------- */

#define DECK_MAX 65536u

typedef struct {
    uint8_t  buf[DECK_MAX];
    size_t   len, pos;
    size_t   skip;          /* the last found file's data, if not loaded */
} deck_t;

static deck_t s_deck, s_hooked, s_rec;

static void deck_put(deck_t *d, const uint8_t *p, size_t n) {
    if (d->len + n > DECK_MAX) abort();
    memcpy(d->buf + d->len, p, n);
    d->len += n;
}

static void serve(oric_t *m, deck_t *d) {
    const tape_t *t = oric_tape_pending(m);
    if (!t) return;
    if (t->op == TAPE_FIND) {
        d->pos += d->skip;
        d->skip = 0;
        tap_header_t h;
        size_t skip;
        if (d->pos >= d->len ||
            tap_scan(d->buf + d->pos, d->len - d->pos, true, &h, &skip) != TAP_FOUND) {
            oric_tape_decline(m);
            return;
        }
        d->pos += h.data_at;
        d->skip = tap_data_len(tap_start(h.raw), tap_end(h.raw));
        oric_tape_found(m, &h);
    } else if (t->op == TAPE_LOAD) {
        size_t n = d->len - d->pos;
        if (n > t->len) n = t->len;
        oric_tape_load_data(m, d->buf + d->pos, n);
        d->pos += n;
        d->skip = 0;
        oric_tape_load_end(m);
    } else {
        uint8_t hdr[TAP_HEADER_MAX];
        deck_put(d, hdr, tap_encode_header(t->raw, t->name, t->name_len, hdr));
        for (uint32_t i = 0; i < t->len; i++) {
            uint8_t b = oric_peek(m, (uint16_t)(t->start + i));
            deck_put(d, &b, 1);
        }
        oric_tape_save_end(m);
    }
}

/* ---- the byte-level hooks -------------------------------------------------- */

static void rts(oric_t *m) {
    m6502_t *c = &m->cpu;
    uint8_t lo = oric_peek(m, (uint16_t)(0x100u | (uint8_t)(c->s + 1u)));
    uint8_t hi = oric_peek(m, (uint16_t)(0x100u | (uint8_t)(c->s + 2u)));
    c->s = (uint8_t)(c->s + 2u);
    c->pc = (uint16_t)(((hi << 8) | lo) + 1u);
}

static void flags(m6502_t *c, uint8_t set, uint8_t clear, uint8_t nz) {
    c->p = (uint8_t)((c->p | set) & ~clear & ~(M6502_N | M6502_Z));
    c->p |= (uint8_t)((nz & 0x80u) | (nz ? 0 : M6502_Z));
}

static unsigned ones(uint8_t v) {
    unsigned n = 0;
    for (; v; v &= (uint8_t)(v - 1u)) n++;
    return n;
}

/* True if the PC was a hooked routine, now served and returned from.
 * The input stops when the tape does, as the test's tapes never do. */
static bool hook(oric_t *m, const rom_pcs_t *r, deck_t *in, deck_t *out) {
    m6502_t *c = &m->cpu;
    if (c->pc == r->sync_in) {
        /* A #16, then three bytes that must each be #16, or the search
         * starts again after the one that was not (#E735: #E74D); out
         * with the last #16 read, compared, X counted down. */
        unsigned got = 0;
        while (got < 4u) {
            if (in->pos >= in->len) return false;
            uint8_t b = in->buf[in->pos++];
            got = b == TAP_SYNC ? got + 1u : 0;
        }
        c->a = TAP_SYNC;
        c->x = 0;
        flags(c, M6502_C, M6502_V, 0);
        m->ram[TAPE_ZP_BYTE] = TAP_SYNC;
    } else if (c->pc == r->byte_in) {
        if (in->pos >= in->len) return false;
        uint8_t b = in->buf[in->pos++];
        c->a = b;
        flags(c, 0, M6502_C | M6502_V, b);
        m->ram[TAPE_ZP_BYTE] = b;
    } else if (c->pc == r->sync_out) {
        uint8_t lead[TAP_LEADER];
        memset(lead, TAP_SYNC, sizeof lead);
        deck_put(out, lead, sizeof lead);
        /* After 259 #16s: X and Y counted out, A from the last byte. */
        c->a = (uint8_t)((ones(TAP_SYNC) ^ 1u) >> 1);
        c->x = c->y = 0;
        flags(c, M6502_C | M6502_V, 0, 0);
        m->ram[TAPE_ZP_BYTE] = 0;
    } else if (c->pc == r->byte_out) {
        deck_put(out, &c->a, 1);
        c->a = (uint8_t)((ones(c->a) ^ 1u) >> 1);
        flags(c, M6502_C | M6502_V, 0, c->x);
        m->ram[TAPE_ZP_BYTE] = 0;
    } else {
        return false;
    }
    rts(m);
    return true;
}

/* ---- running ------------------------------------------------------------------ */

enum { PLAIN, HOOKED, TRAPPED };

/* An instruction at a time to `stop`, with the keyboard's field every
 * field's worth of cycles. False if it is not reached in max cycles. */
static bool run_to(oric_t *m, keymatrix_t *k, uint16_t stop, int how, const rom_pcs_t *r,
                   deck_t *in, deck_t *out, uint64_t max) {
    m->cfg.tape_traps = how == TRAPPED;
    uint64_t end = m->cpu.cycles + max, field = m->cpu.cycles;
    static int16_t pcm[ORIC_AUDIO_BUF_LEN];
    while (m->cpu.pc != stop) {
        if (m->cpu.cycles >= end) return false;
        if (k && m->cpu.cycles >= field) {
            keymatrix_field(k, m);
            (void)oric_audio_drain(m, pcm, ORIC_AUDIO_BUF_LEN);
            field += oric_field_cycles(m);
        }
        if (how == HOOKED && hook(m, r, in, out)) continue;
        oric_run(m, 1);
        if (how == TRAPPED) serve(m, in ? in : out);
    }
    return true;
}

/* Type `cmd` and Return, and run to the tape set-up: the copy every
 * way starts from. */
static bool start_at_setup(const rom_pcs_t *r, const char *cmd) {
    guest_type(&g, cmd);
    keymatrix_event(&g.k, KEY_EV_PRESSED, PICOCALC_KEY_ENTER);
    keymatrix_event(&g.k, KEY_EV_RELEASED, PICOCALC_KEY_ENTER);
    if (!run_to(&g.m, &g.k, r->setup, PLAIN, r, NULL, NULL, 5000000u)) return false;
    oric_copy(&s_start, &g.m);
    return true;
}

/* The same machine, as far as tape.h promises: the first difference, or
 * NULL. */
static const char *differ(const oric_t *a, const oric_t *b) {
    static char why[96];
    const m6502_t *x = &a->cpu, *y = &b->cpu;
    uint8_t pm = (uint8_t)~(M6502_B | M6502_U);
    if (x->pc != y->pc || x->a != y->a || x->x != y->x || x->y != y->y || x->s != y->s ||
        (x->p & pm) != (y->p & pm)) {
        snprintf(why, sizeof why, "PC %04X/%04X A %02X/%02X X %02X/%02X Y %02X/%02X "
                 "S %02X/%02X P %02X/%02X", x->pc, y->pc, x->a, y->a, x->x, y->x,
                 x->y, y->y, x->s, y->s, x->p & pm, y->p & pm);
        return why;
    }
    for (uint32_t i = 0; i < 0xC000u; i++) {
        if ((i >> 8) == 0x03u) continue;
        if ((i >> 8) == 0x01u && (i & 0xFFu) <= x->s) continue;
        if (a->ram[i] != b->ram[i]) {
            snprintf(why, sizeof why, "#%04X: %02X/%02X", (unsigned)i, a->ram[i], b->ram[i]);
            return why;
        }
    }
    return NULL;
}

/* After a trapped run: the machine to Ready in fields, as the firmware
 * runs it, into g. */
static void carry_on(const oric_t *m, int fields) {
    keymatrix_t k = g.k;
    oric_copy(&g.m, m);
    g.k = k;
    guest_fields(&g, fields);
}

static const char *rom_name(rom_id_t r, oric_ram_t ram) {
    if (r == ROM_BASIC10) return ram == ORIC_RAM_16K ? "1.0 16K" : "1.0 48K";
    return ram == ORIC_RAM_16K ? "1.1 16K" : "1.1 48K";
}

/* ---- the cases ----------------------------------------------------------------- */

/* CSAVE`opts` the ROM's way, hooked and trapped: the same machine, the
 * same file, which is left in s_deck. */
static int save_case(const char *n, const rom_pcs_t *r, const char *cmd) {
    CHECK(start_at_setup(r, cmd), "%s: %s never reached the set-up", n, cmd);

    oric_copy(&s_ref, &s_start);
    CHECK(run_to(&s_ref, NULL, r->cleanup, PLAIN, r, NULL, NULL, 200000000u),
          "%s: %s, the ROM alone, never finished", n, cmd);

    memset(&s_rec, 0, sizeof s_rec);
    oric_copy(&s_run, &s_start);
    CHECK(run_to(&s_run, NULL, r->cleanup, HOOKED, r, NULL, &s_rec, 50000000u),
          "%s: %s, hooked, never finished", n, cmd);
    const char *d = differ(&s_ref, &s_run);
    CHECK(!d, "%s: %s hooked differs from the ROM alone: %s", n, cmd, d);

    memset(&s_deck, 0, sizeof s_deck);
    oric_copy(&s_run, &s_start);
    uint32_t served = s_run.tape.served;
    CHECK(run_to(&s_run, NULL, r->cleanup, TRAPPED, r, NULL, &s_deck, 50000000u),
          "%s: %s, trapped, never finished", n, cmd);
    CHECK(s_run.tape.served == served + 1u, "%s: %s: the trap served %u requests", n, cmd,
          (unsigned)(s_run.tape.served - served));
    d = differ(&s_ref, &s_run);
    CHECK(!d, "%s: %s trapped differs from the ROM alone: %s", n, cmd, d);

    CHECK(s_deck.len == s_rec.len && memcmp(s_deck.buf, s_rec.buf, s_rec.len) == 0,
          "%s: %s: the trap's file (%zu bytes) is not the ROM's (%zu)", n, cmd,
          s_deck.len, s_rec.len);
    carry_on(&s_run, 50);
    return 0;
}

/* CLOAD`opts` from `tape`, hooked and trapped: the same machine. Leaves
 * the trapped one in g, run on to Ready. */
static int load_case(const char *n, const rom_pcs_t *r, const char *cmd,
                     const deck_t *tape, unsigned want_served) {
    CHECK(start_at_setup(r, cmd), "%s: %s never reached the set-up", n, cmd);
    /* As a parity error in an earlier load leaves 1.1's flag: the find
     * clears it (#E4B6). In 1.0 it is any byte of RAM. */
    s_start.ram[0x02B1u] = 0x01u;

    memcpy(&s_hooked, tape, sizeof s_hooked);
    oric_copy(&s_ref, &s_start);
    CHECK(run_to(&s_ref, NULL, r->cleanup, HOOKED, r, &s_hooked, NULL, 50000000u),
          "%s: %s, hooked, never finished", n, cmd);

    static deck_t in;
    memcpy(&in, tape, sizeof in);
    oric_copy(&s_run, &s_start);
    uint32_t served = s_run.tape.served;
    CHECK(run_to(&s_run, NULL, r->cleanup, TRAPPED, r, &in, NULL, 50000000u),
          "%s: %s, trapped, never finished", n, cmd);
    CHECK(s_run.tape.served - served == want_served, "%s: %s: the trap served %u requests, "
          "not %u", n, cmd, (unsigned)(s_run.tape.served - served), want_served);
    const char *d = differ(&s_ref, &s_run);
    CHECK(!d, "%s: %s trapped differs from hooked: %s", n, cmd, d);
    carry_on(&s_run, 50);
    return 0;
}

static int machine(rom_id_t rom, oric_ram_t ram) {
    const char *n = rom_name(rom, ram);
    const rom_pcs_t *r = rom == ROM_BASIC11 ? &pcs11 : &pcs10;
    bool v11 = rom == ROM_BASIC11;

    CHECK(guest_boot(&g, rom, ram), "%s: no Ready", n);
    CHECK(g.m.tape.rom != NULL, "%s: the trap does not know the stock ROM", n);
    guest_type(&g, "10 PRINT \"TAPE OK\"\r");
    guest_type(&g, "20 REM A SECOND LINE\r");

    /* ---- save, fast and slow ----------------------------------------- */
    static deck_t prog, slow;
    if (save_case(n, r, "CSAVE\"PROG\",S")) return 1;
    memcpy(&slow, &s_deck, sizeof slow);
    if (save_case(n, r, "CSAVE\"PROG\"")) return 1;
    memcpy(&prog, &s_deck, sizeof prog);
    CHECK(slow.len == prog.len && memcmp(slow.buf, prog.buf, prog.len) == 0,
          "%s: the slow save's bytes are not the fast one's", n);

    tap_header_t h;
    size_t skip;
    CHECK(tap_scan(prog.buf, prog.len, true, &h, &skip) == TAP_FOUND && skip == 0,
          "%s: the saved file has no header", n);
    CHECK(h.name_len == 4 && memcmp(h.name, "PROG", 4) == 0 && h.raw[TAP_H_TYPE] == 0,
          "%s: the header is not PROG's, a BASIC program", n);
    CHECK(h.data_at + tap_data_len(tap_start(h.raw), tap_end(h.raw)) == prog.len,
          "%s: the file's data is not the header's length", n);

    /* ---- load it back -------------------------------------------------- */
    guest_type(&g, "NEW\r");
    CHECK(guest_find_row(&g.m, "TAPE OK", 0) < 0, "%s: TAPE OK on screen before RUN", n);
    if (load_case(n, r, "CLOAD\"PROG\"", &prog, 2)) return 1;
    guest_type(&g, "RUN\r");
    CHECK(guest_find_row(&g.m, "TAPE OK", 0) >= 0, "%s: the loaded program does not run", n);
    if (!(guest_find_row(&g.m, "TAPE OK", 0) >= 0)) guest_dump(&g.m, stderr);

    /* A slow load, which the trap serves the same way. */
    guest_type(&g, "NEW\r");
    if (load_case(n, r, "CLOAD\"PROG\",S", &slow, 2)) return 1;

    /* ---- two files: by name the second, nameless the first -------------- */
    static deck_t two;
    memset(&two, 0, sizeof two);
    uint8_t other[TAP_HEADER_MAX + 8];
    uint8_t raw[TAP_HEADER_LEN] = { 0, 0, 0x80, 0, 0x50, 0x07, 0x50, 0x00, 0 };
    /* A name longer than the 16 bytes 1.1 keeps of one (1.0 keeps all). */
    const char *long_name = "OTHER WITH A LONG NAME";
    size_t k = tap_encode_header(raw, (const uint8_t *)long_name, strlen(long_name), other);
    for (unsigned i = 0; i < 8; i++) other[k++] = (uint8_t)(0x16u + i * 0x0Eu);  /* #16 #24 ... in the data */
    deck_put(&two, other, k);
    deck_put(&two, prog.buf, prog.len);
    guest_type(&g, "NEW\r");
    /* Found OTHER, the wrong name, its data skipped; then PROG. */
    if (load_case(n, r, "CLOAD\"PROG\"", &two, 3)) return 1;
    guest_type(&g, "RUN\r");
    CHECK(guest_find_row(&g.m, "TAPE OK", 0) >= 0, "%s: PROG, after OTHER, does not run", n);
    /* Nameless: the first file, machine code, loaded at #5000. */
    if (load_case(n, r, "CLOAD\"\"", &two, 2)) return 1;
    for (unsigned i = 0; i < 8; i++)
        CHECK(oric_peek(&g.m, (uint16_t)(0x5000u + i)) == (uint8_t)(0x16u + i * 0x0Eu),
              "%s: CLOAD\"\" did not load OTHER's byte %u at #%04X", n, i, 0x5000u + i);

    /* ---- 1.1's verify ----------------------------------------------------- */
    if (v11) {
        guest_type(&g, "NEW\r");
        if (load_case(n, r, "CLOAD\"PROG\"", &prog, 2)) return 1;
        if (load_case(n, r, "CLOAD\"PROG\",V", &prog, 2)) return 1;
        CHECK(oric_peek(&g.m, 0x025Cu) == 0 && oric_peek(&g.m, 0x025Du) == 0,
              "%s: verifying the same program counted errors", n);
        guest_type(&g, "10 PRINT \"TAPE NO\"\r");
        if (load_case(n, r, "CLOAD\"PROG\",V", &prog, 2)) return 1;
        CHECK(oric_peek(&g.m, 0x025Cu) != 0, "%s: verifying a changed program found no errors", n);
    }

    /* ---- the tape runs out --------------------------------------------- */
    /* An empty deck: the find is declined, and the ROM waits for a
     * signal, running, until the reset button. */
    static deck_t none;
    memset(&none, 0, sizeof none);
    CHECK(start_at_setup(r, "CLOAD\"\""), "%s: CLOAD never reached the set-up", n);
    oric_copy(&s_run, &s_start);
    uint32_t declined = s_run.tape.declined;
    bool ended = run_to(&s_run, NULL, r->cleanup, TRAPPED, r, &none, NULL, 2000000u);
    CHECK(!ended && s_run.tape.declined == declined + 1u && s_run.cpu.pc >= 0xE400u &&
          s_run.cpu.pc < 0xE800u, "%s: an empty deck: ended %d, declined %u, at #%04X", n,
          ended, (unsigned)(s_run.tape.declined - declined), s_run.cpu.pc);

    /* g itself stands in CLOAD's set-up: the reset button brings it
     * back to Ready, program kept (§2.1). */
    oric_nmi(&g.m);
    guest_fields(&g, 50);
    CHECK(guest_find_row(&g.m, "Ready", 0) >= 0, "%s: no Ready after the reset button", n);

    /* A file cut short: what there is is loaded, and the loop waits for
     * the next byte. */
    static deck_t cut;
    memcpy(&cut, &prog, sizeof cut);
    cut.len -= 5;
    CHECK(start_at_setup(r, "CLOAD\"PROG\""), "%s: CLOAD never reached the set-up", n);
    oric_copy(&s_run, &s_start);
    ended = run_to(&s_run, NULL, r->cleanup, TRAPPED, r, &cut, NULL, 2000000u);
    uint16_t ptr = (uint16_t)(oric_peek(&s_run, 0x33u) | (oric_peek(&s_run, 0x34u) << 8));
    uint16_t start = tap_start(h.raw);
    uint32_t len = tap_data_len(start, tap_end(h.raw));
    CHECK(!ended && ptr == start + len - 5u, "%s: a short file: ended %d, #33 = #%04X, "
          "not #%04X", n, ended, ptr, (unsigned)(start + len - 5u));
    return 0;
}

int main(void) {
    const char *dir;
    if (!guest_find_roms(&dir)) {
        printf("skipped: basic10.rom and basic11b.rom are not both in %s\n", dir);
        return TEST_SKIP_CODE;
    }

    /* ---- the format -------------------------------------------------------- */
    {
        uint8_t raw[TAP_HEADER_LEN] = { 0, 0, 0, 0, 0x05, 0x10, 0x05, 0x01, 0 };
        uint8_t buf[64 + TAP_HEADER_MAX];
        memset(buf, 0xAA, 10);
        size_t n = 10 + tap_encode_header(raw, (const uint8_t *)"AB", 2, buf + 10);
        tap_header_t h;
        size_t skip;
        CHECK(n == 10 + 4 + 1 + 9 + 2 + 1, "encoded header is %zu bytes", n - 10);
        CHECK(tap_scan(buf, n, true, &h, &skip) == TAP_FOUND && skip == 10 &&
              h.data_at == n && h.name_len == 2 && tap_start(h.raw) == 0x0501u &&
              tap_end(h.raw) == 0x0510u, "the encoded header does not scan back");
        for (size_t cut = 11; cut < n; cut++)
            CHECK(tap_scan(buf, cut, false, &h, &skip) == TAP_MORE && skip == 10,
                  "a header cut at %zu is not TAP_MORE from 10", cut);
        CHECK(tap_scan(buf, n - 1, true, &h, &skip) == TAP_NONE,
              "a header cut at the file's end is a header");
        CHECK(tap_data_len(0x0501u, 0x0510u) == 16 && tap_data_len(0x0600u, 0x0500u) == 1,
              "data lengths");
        /* #16 #16 then not #24: no header there. */
        uint8_t junk[] = { 0x16, 0x16, 0x00, 0x16, 0x24 };
        CHECK(tap_scan(junk, sizeof junk, true, &h, &skip) == TAP_NONE, "junk is a header");
    }

    for (int r = ROM_BASIC10; r <= ROM_BASIC11; r++) {
        for (int ram = ORIC_RAM_16K; ram <= ORIC_RAM_48K; ram++) {
            if (machine((rom_id_t)r, (oric_ram_t)ram)) return 1;
        }
    }

    /* An unrecognised ROM: the trap stands aside. */
    {
        static uint8_t img[ORIC_ROM_SIZE];
        memcpy(img, guest_rom_image(ROM_BASIC11), sizeof img);
        img[0] ^= 1u;
        oric_load_rom(&g.m, img, sizeof img);
        CHECK(g.m.tape.rom == NULL, "a changed 1.1 is taken as stock");
    }
    TEST_DONE();
}
