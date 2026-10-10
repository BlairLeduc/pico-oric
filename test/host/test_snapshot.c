/* test_snapshot.c — our own save states, on our own ROM (design.md
 * §10.6, §15.2 M11; EL §8.5).
 *
 * test/asm/oric_test_rom.s, so that CI runs it on every push without an
 * Oric ROM: it leaves T1 free-running with its IRQ counted and the AY's
 * envelope stepping. test_snapshot_rom does the same over the real ROMs.
 *
 * The property that matters is executed, not compared: a machine saved
 * part-way through, restored into a machine that has been doing
 * something else and run on for 150 fields, must arrive exactly where
 * the original arrives (snap_util.h's comparison). A one-cycle error in
 * the budget must not. Then the refusals — damaged, short, foreign or
 * newer files, another ROM, RAM, Microdisc or field — each leave the
 * machine that refused it untouched.
 */

#include <string.h>

#include "oric.h"
#include "via6522.h"
#include "snap_util.h"
#include "test_util.h"

static uint8_t rom[ORIC_ROM_SIZE];
static oric_t g, h, ahead, before, other, saved;
static mem_t snap;

#define DONE 0x02u   /* #A5 once the test ROM is idling (test_test_rom.c) */
#define IRQS 0x20u

static bool boot(oric_t *m, const oric_config_t *cfg, const uint8_t *image) {
    oric_init(m, cfg);
    oric_load_rom(m, image, ORIC_ROM_SIZE);
    oric_reset(m);
    for (int i = 0; i < 100 && m->ram[DONE] != 0xA5u; i++) oric_run_field(m);
    return m->ram[DONE] == 0xA5u;
}

static int16_t sound_a[SNAP_SOUND_MAX], sound_b[SNAP_SOUND_MAX];

/* n fields, the sound drained after each onto out at *got, if out. */
static void fields_sound(oric_t *m, int n, int16_t *out, size_t *got) {
    size_t none = 0;
    for (int i = 0; i < n; i++) {
        oric_run_field(m);
        snap_sound_take(m, out, out ? got : &none);
    }
}

static void fields(oric_t *m, int n) {
    fields_sound(m, n, NULL, NULL);
}

/* The state section's byte at `off`, changed, with the CRC made good
 * again so that only the field is wrong. */
static void poke_state(unsigned off, uint8_t v) {
    snap.buf[SNAP_HEADER_LEN + off] = v;
    mem_recrc(&snap);
}

int main(void) {
    CHECK(snapshot_crc32(0, (const uint8_t *)"123456789", 9) == 0xCBF43926u,
          "CRC-32 of 123456789 is %08X", (unsigned)snapshot_crc32(0, (const uint8_t *)"123456789", 9));

    FILE *f = fopen(PICO_ORIC_TEST_ROM, "rb");
    if (!f) {
        printf("skipped: %s not built (ca65 and ld65 not found)\n", PICO_ORIC_TEST_ROM);
        return TEST_SKIP_CODE;
    }
    size_t n = fread(rom, 1, sizeof rom, f);
    fclose(f);
    CHECK(n == sizeof rom, "the test ROM is %zu bytes", n);

    oric_config_t cfg;
    oric_config_default(&cfg);

    /* ---- save, run on, restore elsewhere, and meet ------------------ */
    CHECK(boot(&g, &cfg, rom), "boot");
    g.pcm.dc_block = false;
    fields(&g, 37);
    uint16_t irqs = (uint16_t)(g.ram[IRQS] | g.ram[IRQS + 1] << 8);
    CHECK(irqs > 50, "T1 should be interrupting: %u", irqs);
    CHECK(g.ay.stepped & (1u << AY_GEN_ENV), "the envelope should be stepping");

    /* State the run alone would leave at its reset values. The AY held
     * in a write to the envelope's shape, as the ROMs' sequence is for a
     * moment (oric.c's wire): restored as anything else, the next access
     * to the VIA restarts the envelope. */
    uint32_t starts = g.ay.env_starts;
    via6522_write(&g.via, VIA_ORA, AY_ENV_SHAPE);
    via6522_write(&g.via, VIA_PCR, 0xEEu);
    oric_io_changed(&g);
    via6522_write(&g.via, VIA_ORA, g.ay.reg[AY_ENV_SHAPE]);
    via6522_write(&g.via, VIA_PCR, 0xECu);
    oric_io_changed(&g);
    CHECK(g.ay.mode == AY_BUS_WRITE && g.ay.env_starts == starts + 1u,
          "a held write to the shape: mode %u, %u restarts", g.ay.mode,
          (unsigned)(g.ay.env_starts - starts));
    /* On past the first step, so that a restart would show. */
    fields(&g, 21);
    CHECK(g.ay.env_starts == starts + 1u && g.ay.env_step != 15,
          "the envelope should have stepped on: step %d", g.ay.env_step);
    /* The input latches, which this ROM never takes. */
    g.via.ira = 0x5Au;
    g.via.irb = 0xA5u;
    /* And a boundary where T1's IRQ is asserted and the CPU has not yet
     * taken it: restored, it is taken before the next instruction. */
    for (int i = 0; i < 20000 && !(via6522_irq(&g.via) && !(g.cpu.p & M6502_I)); i++)
        oric_run(&g, 1);
    CHECK(via6522_irq(&g.via) && !(g.cpu.p & M6502_I), "no boundary with an IRQ about to be taken");

    /* A budget other than zero, so that carrying it is tested. */
    CHECK(g.budget != 0, "the last field should have overrun");
    CHECK(mem_save(&snap, &g) == SNAP_OK, "save");
    oric_copy(&saved, &g);
    CHECK(snap.len == SNAP_FILE_LEN, "a state is %zu bytes, want %u", snap.len,
          (unsigned)SNAP_FILE_LEN);
    {
        const uint8_t *st = snap.buf + SNAP_HEADER_LEN;
        CHECK(memcmp(snap.buf, "PORCSNAP", 8) == 0, "the magic");
        CHECK(memcmp(st + SNAP_STATE_LEN, g.ram, ORIC_ADDR_SPACE) == 0,
              "RAM follows the state, as it stands");
        bool reserved = true;
        for (unsigned i = 267; i < SNAP_STATE_LEN; i++) reserved = reserved && st[i] == 0;
        CHECK(reserved, "reserved state bytes are written zero");
        /* The ROM's code, which nothing copies to RAM, is not in it. */
        bool found = false;
        for (size_t i = 0; i + 64u <= snap.len && !found; i++)
            found = memcmp(snap.buf + i, rom + 0x0040u, 64u) == 0;
        CHECK(!found, "ROM bytes must not be in a state");
    }

    snap_sound_start(&g);
    size_t na = 0, nb = 0;
    fields_sound(&g, 150, sound_a, &na);
    oric_copy(&ahead, &g);

    /* The other machine is somewhere else, with keys down. */
    CHECK(boot(&h, &cfg, rom), "boot the other");
    h.pcm.dc_block = false;
    fields(&h, 11);
    oric_key_set(&h, 2, 5, true);
    oric_key_set(&h, 7, 4, true);
    CHECK(!snap_same(&ahead, &h, "control"), "control: a machine not restored must differ");
    snap_info_t info = { ROM_BASIC10, ORIC_RAM_16K, true, true };
    CHECK(mem_check(&snap, &h, &info) == SNAP_OK, "check: %s",
          snapshot_status_str(mem_check(&snap, &h, NULL)));
    CHECK(info.rom == ROM_UNKNOWN && info.ram == ORIC_RAM_48K && !info.microdisc && !info.vsync_hack,
          "info: rom %d ram %d",
          (int)info.rom, (int)info.ram);
    CHECK(mem_load(&snap, &h) == SNAP_OK, "load");
    CHECK(snap_same(&saved, &h, "loaded"), "the machine as loaded should be the one saved");
    bool up = true;
    for (unsigned r = 0; r < ORIC_KEY_ROWS; r++) up = up && h.keys[r] == 0;
    CHECK(up, "every key is released by a load");
    /* What the other machine had made and not yet drained is its own. */
    snap_sound_take(&h, NULL, &nb);
    nb = 0;
    fields_sound(&h, 150, sound_b, &nb);
    CHECK(snap_same(&ahead, &h, "resumed"), "a restored machine should run to the same state");
    CHECK(snap_same_sound(sound_a, na, sound_b, nb, "resumed"), "and make the same sound");

    /* Control: the same restore with the budget one cycle out ends
     * elsewhere, so the comparison would see a field that is not carried. */
    CHECK(mem_load(&snap, &h) == SNAP_OK, "load again");
    h.budget += 1;
    fields(&h, 150);
    CHECK(!snap_same(&ahead, &h, "control"), "control: a budget one cycle out must not meet");

    /* The reset button pressed and not yet taken: the restored machine
     * takes it, as the test ROM's NMI count says. */
    {
        CHECK(mem_load(&snap, &h) == SNAP_OK, "load for the NMI");
        oric_nmi(&h);
        static mem_t nmi;
        CHECK(mem_save(&nmi, &h) == SNAP_OK, "save with an NMI pending");
        uint8_t nmis = h.ram[0x22];
        fields(&h, 3);
        CHECK(h.ram[0x22] == (uint8_t)(nmis + 1u), "the NMI should be taken once");
        oric_copy(&ahead, &h);
        CHECK(boot(&other, &cfg, rom), "boot for the NMI");
        CHECK(mem_load(&nmi, &other) == SNAP_OK, "load with an NMI pending");
        fields(&other, 3);
        CHECK(snap_same(&ahead, &other, "NMI"), "a pending NMI is carried");
    }

    /* The trap's kept header, set by hand here (test_snapshot_rom keeps
     * one by running a CSAVE). */
    CHECK(mem_load(&snap, &h) == SNAP_OK, "load once more");
    h.tape.header_kept = true;
    for (unsigned i = 0; i < TAP_HEADER_LEN; i++) h.tape.raw[i] = (uint8_t)(0x11u * i);
    h.tape.name_len = 5;
    memcpy(h.tape.name, "KEPT!", 5);
    static mem_t kept;
    CHECK(mem_save(&kept, &h) == SNAP_OK, "save with a kept header");
    oric_copy(&before, &h);
    CHECK(boot(&other, &cfg, rom), "boot a third");
    CHECK(mem_load(&kept, &other) == SNAP_OK, "load with a kept header");
    CHECK(snap_same(&before, &other, "kept header"), "the kept header is carried");

    /* ---- refusals leave the machine as it was ----------------------- */
    CHECK(mem_load(&snap, &h) == SNAP_OK, "load before the refusals");
    oric_copy(&before, &h);

    snap.buf[SNAP_HEADER_LEN + SNAP_STATE_LEN + 0x950] ^= 0x01u;
    CHECK(mem_check(&snap, &h, NULL) == SNAP_CORRUPT, "a flipped bit: %s",
          snapshot_status_str(mem_check(&snap, &h, NULL)));
    snap.buf[SNAP_HEADER_LEN + SNAP_STATE_LEN + 0x950] ^= 0x01u;

    size_t full = snap.len;
    snap.len = full - 1000;
    CHECK(mem_check(&snap, &h, NULL) == SNAP_IO, "truncated: %s",
          snapshot_status_str(mem_check(&snap, &h, NULL)));
    snap.len = SNAP_HEADER_LEN + 10;
    CHECK(mem_load(&snap, &h) == SNAP_IO, "torn in the state: %s",
          snapshot_status_str(mem_load(&snap, &h)));
    snap.len = full;

    snap.buf[0] = 'X';
    CHECK(mem_check(&snap, &h, NULL) == SNAP_NOT_SNAPSHOT, "magic");
    CHECK(mem_load(&snap, &h) == SNAP_NOT_SNAPSHOT, "load refuses it too");
    snap.buf[0] = 'P';
    snap.buf[8] = SNAP_VERSION + 1;
    CHECK(mem_check(&snap, &h, NULL) == SNAP_NEWER, "version");
    CHECK(mem_load(&snap, &h) == SNAP_NEWER, "load refuses a newer version");
    snap.buf[8] = SNAP_VERSION;
    snap.buf[12] ^= 1u;
    CHECK(mem_check(&snap, &h, NULL) == SNAP_NOT_SNAPSHOT, "another payload length");
    snap.buf[12] ^= 1u;

    /* Fields no machine could have saved, each with a good CRC, each
     * of which would divide by zero, index past a table or hang core 0
     * if loaded (snapshot.c's plausible). The offsets are the state
     * section's (snapshot.c). */
    {
        static const struct { const char *what; unsigned off, len; uint32_t v; } bad[] = {
            { "a tone period of 0",          123, 4, 0 },
            { "an envelope period of 0",     123 + 16, 4, 0 },
            { "a noise period past 31",      123 + 12, 4, 32 },
            { "an AY register of 16",        70, 1, 16 },
            { "an envelope step of 16",      149, 1, 16 },
            { "a dead LFSR",                 145, 4, 0 },
            { "T1 far below zero",           42, 4, 0x80000000u },
            { "T2 far below zero",           46, 4, 0x80000000u },
            { "a positive budget",           158, 4, 1000000u },
            { "the AY a day behind",         75, 8, 0 },
        };
        for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
            uint8_t was[8];
            uint8_t *at = snap.buf + SNAP_HEADER_LEN + bad[i].off;
            memcpy(was, at, bad[i].len);
            memset(at, 0, bad[i].len);
            for (unsigned k = 0; k < bad[i].len && k < 4; k++) at[k] = (uint8_t)(bad[i].v >> (8 * k));
            mem_recrc(&snap);
            CHECK(mem_check(&snap, &h, NULL) == SNAP_NOT_SNAPSHOT, "%s: %s", bad[i].what,
                  snapshot_status_str(mem_check(&snap, &h, NULL)));
            CHECK(mem_load(&snap, &h) == SNAP_NOT_SNAPSHOT, "load refuses %s", bad[i].what);
            memcpy(at, was, bad[i].len);
            mem_recrc(&snap);
        }
        /* The AY's clock at 0 is more than a second behind only if the
         * saved machine had run that long. */
        CHECK(saved.cpu.cycles > ORIC_CPU_HZ, "the AY case needs a clock past a second");
    }

    /* A Microdisc fitted (M14), and a RAM fit no Oric has. */
    poke_state(163, 1);
    CHECK(mem_check(&snap, &h, NULL) == SNAP_OTHER_MACHINE, "a Microdisc: %s",
          snapshot_status_str(mem_check(&snap, &h, NULL)));
    CHECK(mem_load(&snap, &h) == SNAP_OTHER_MACHINE, "load refuses a Microdisc");
    poke_state(163, 0);
    poke_state(162, 7);
    CHECK(mem_check(&snap, &h, NULL) == SNAP_NOT_SNAPSHOT, "RAM fit 7");
    poke_state(162, (uint8_t)ORIC_RAM_48K);
    CHECK(mem_check(&snap, &h, NULL) == SNAP_OK, "made good again");
    CHECK(snap_same(&before, &h, "untouched"), "refused files change nothing");

    /* Another ROM: one byte different. */
    {
        static uint8_t rom2[ORIC_ROM_SIZE];
        memcpy(rom2, rom, sizeof rom2);
        rom2[0x2000] ^= 0xFFu;
        CHECK(boot(&other, &cfg, rom2), "boot another ROM");
        oric_copy(&before, &other);
        CHECK(mem_check(&snap, &other, NULL) == SNAP_OTHER_ROM, "another ROM: %s",
              snapshot_status_str(mem_check(&snap, &other, NULL)));
        CHECK(mem_load(&snap, &other) == SNAP_OTHER_ROM, "load refuses another ROM");
        CHECK(snap_same(&before, &other, "other ROM"), "and is untouched");
    }

    /* The other RAM fit, named. */
    {
        oric_config_t c16 = cfg;
        c16.ram = ORIC_RAM_16K;
        CHECK(boot(&other, &c16, rom), "boot 16K");
        oric_copy(&before, &other);
        info.ram = ORIC_RAM_16K;
        CHECK(mem_check(&snap, &other, &info) == SNAP_OTHER_RAM && info.ram == ORIC_RAM_48K,
              "a 48K state in the 16K: %s", snapshot_status_str(mem_check(&snap, &other, NULL)));
        CHECK(mem_load(&snap, &other) == SNAP_OTHER_RAM, "load refuses the other RAM");
        CHECK(snap_same(&before, &other, "other RAM"), "and is untouched");

        /* And the 16K's own state round-trips. */
        static mem_t own;
        fields(&other, 23);
        CHECK(mem_save(&own, &other) == SNAP_OK, "16K save");
        fields(&other, 150);
        oric_copy(&ahead, &other);
        CHECK(boot(&other, &c16, rom), "16K reboot");
        CHECK(mem_check(&own, &other, NULL) == SNAP_OK, "16K check");
        CHECK(mem_load(&own, &other) == SNAP_OK, "16K load");
        fields(&other, 150);
        CHECK(snap_same(&ahead, &other, "16K"), "the 16K resumes to the same state");
    }

    /* Another field: a line longer at 50 Hz. */
    {
        oric_config_t c = cfg;
        c.lines_50hz++;
        CHECK(boot(&other, &c, rom), "boot another field");
        CHECK(mem_check(&snap, &other, NULL) == SNAP_OTHER_FIELD, "another field: %s",
              snapshot_status_str(mem_check(&snap, &other, NULL)));
        CHECK(mem_load(&snap, &other) == SNAP_OTHER_FIELD, "load refuses another field");
    }

    /* A write that fails part-way reports it. */
    snap.fail_at = 5000;
    CHECK(mem_save(&snap, &g) == SNAP_IO, "a failed write is reported");
    snap.fail_at = 0;

    /* Not while the CPU is stalled on a tape request. */
    CHECK(mem_save(&snap, &g) == SNAP_OK, "save again");
    g.tape.op = TAPE_LOAD;
    static mem_t busy;
    CHECK(mem_save(&busy, &g) == SNAP_BUSY, "busy save");
    CHECK(mem_load(&snap, &g) == SNAP_BUSY, "busy load");
    g.tape.op = TAPE_NONE;

    TEST_DONE();
}
