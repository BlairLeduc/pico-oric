/* test_snapshot_rom.c — our own save states, on the real ROMs
 * (design.md §10.6, §15.2 M11).
 *
 * Needs basic10.rom and basic11b.rom in roms/ (§13.3); without them it
 * reports skipped, which is not a pass. test_snapshot does the same on
 * our own ROM for CI, with the refusals.
 *
 * On each of the four machines: a BASIC program playing the AY's tones,
 * noise and envelope while it scrolls is saved part-way, run on for 150
 * fields, and restored into the same machine typing something else,
 * which must arrive at the same state (snap_util.h) with the same AY
 * writes; then the restored program finishes and the machine takes
 * typing. And a CSAVE through the trap is saved between its header and
 * its data: the restored machine must ask for the same file.
 */

#include <string.h>

#include "guest.h"
#include "snap_util.h"
#include "test_util.h"

static guest_t g, h;
static int16_t sound_a[SNAP_SOUND_MAX], sound_b[SNAP_SOUND_MAX];

/* guest_fields, with the sound drained after each field onto out. */
static void fields_sound(guest_t *x, int n, int16_t *out, size_t *got) {
    for (int i = 0; i < n; i++) {
        keymatrix_field(&x->k, &x->m);
        oric_run_field(&x->m);
        snap_sound_take(&x->m, out, got);
    }
}
static oric_t ahead, prev, at, saved;
static mem_t snap;

static const char *const PROGRAM[] = {
    "10 FOR I=1 TO 300\r",
    "20 SOUND 1,10+I,0:SOUND 2,500-I,8\r",
    "30 PLAY 3,1,4,90\r",
    "40 PRINT I;\r",
    "50 NEXT\r",
    "60 PLAY 0,0,0,0:PRINT:PRINT \"DONE\"\r",
};

static const char *name_of(rom_id_t r, oric_ram_t ram) {
    if (r == ROM_BASIC10) return ram == ORIC_RAM_16K ? "1.0 16K" : "1.0 48K";
    return ram == ORIC_RAM_16K ? "1.1 16K" : "1.1 48K";
}

/* Fields until `row` shows, at most max. */
static bool run_until_row(guest_t *x, const char *row, int max) {
    for (int i = 0; i < max; i++) {
        if (guest_find_row(&x->m, row, 0) >= 0) return true;
        guest_fields(x, 1);
    }
    return guest_find_row(&x->m, row, 0) >= 0;
}

/* Fields until the CPU stalls on a tape save, at most max. */
static bool run_to_save(guest_t *x, int max) {
    for (int i = 0; i < max && x->m.tape.op != TAPE_SAVE; i++) guest_fields(x, 1);
    return x->m.tape.op == TAPE_SAVE;
}

/* h, refused the state in snap, powered on as snapshot_machine says
 * and loaded: it must be the machine saved, and run on 150 fields to
 * `ahead`, the original's. */
static int switched(const snap_info_t *info, const char *n, const char *from) {
    oric_config_t cfg = h.m.cfg;
    snapshot_machine(info, &cfg);
    CHECK(guest_boot(&h, cfg.rom, cfg.ram), "%s %s: boot as the state's", n, from);
    h.m.pcm.dc_block = false;
    CHECK(mem_load(&snap, &h.m) == SNAP_OK, "%s %s: load: %s", n, from,
          snapshot_status_str(mem_check(&snap, &h.m, NULL)));
    CHECK(snap_same(&saved, &h.m, n), "%s %s: the machine as loaded", n, from);
    keymatrix_init(&h.k);
    guest_fields(&h, 150);
    CHECK(snap_same(&ahead, &h.m, n), "%s %s: runs on to the same state", n, from);
    return 0;
}

static int program(rom_id_t rom, oric_ram_t ram) {
    const char *n = name_of(rom, ram);
    CHECK(guest_boot(&g, rom, ram), "%s: boot", n);
    g.m.pcm.dc_block = false;
    for (unsigned i = 0; i < sizeof PROGRAM / sizeof PROGRAM[0]; i++) guest_type(&g, PROGRAM[i]);
    guest_type(&g, "RUN\r");
    uint32_t w0 = g.m.ay.writes;
    guest_fields(&g, 37);
    CHECK(g.m.ay.writes > w0, "%s: the program should be writing the AY", n);
    CHECK(g.m.ay.stepped & (1u << AY_GEN_NOISE), "%s: the noise should be heard", n);
    CHECK(guest_find_row(&g.m, "DONE", 0) < 0, "%s: the program ended too soon", n);

    /* Saved where the VIA's IRQ is asserted and the CPU has not yet
     * taken it: the restored machine must take it before the next
     * instruction, as this one does (oric_restored). */
    for (int i = 0; i < 20000 && !(via6522_irq(&g.m.via) && !(g.m.cpu.p & M6502_I)); i++)
        oric_run(&g.m, 1);
    CHECK(via6522_irq(&g.m.via) && !(g.m.cpu.p & M6502_I), "%s: no IRQ about to be taken", n);
    CHECK(mem_save(&snap, &g.m) == SNAP_OK, "%s: save", n);
    oric_copy(&saved, &g.m);
    snap_sound_start(&g.m);
    size_t na = 0, nb = 0;
    uint32_t wa = g.m.ay.writes;
    fields_sound(&g, 150, sound_a, &na);
    wa = g.m.ay.writes - wa;
    oric_copy(&ahead, &g.m);

    CHECK(guest_boot(&h, rom, ram), "%s: boot the other", n);
    h.m.pcm.dc_block = false;
    guest_type(&h, "PRINT 2+2\r");
    CHECK(!snap_same(&ahead, &h.m, "control"), "%s: control: not restored must differ", n);
    snap_info_t info;
    CHECK(mem_check(&snap, &h.m, &info) == SNAP_OK && info.rom == rom && info.ram == ram,
          "%s: check: %s, rom %d ram %d", n, snapshot_status_str(mem_check(&snap, &h.m, NULL)),
          (int)info.rom, (int)info.ram);
    CHECK(mem_load(&snap, &h.m) == SNAP_OK, "%s: load", n);
    CHECK(snap_same(&saved, &h.m, n), "%s: the machine as loaded should be the one saved", n);
    keymatrix_init(&h.k);
    /* What the other machine had made and not yet drained is its own. */
    snap_sound_take(&h.m, NULL, &nb);
    nb = 0;
    uint32_t wb = h.m.ay.writes;
    fields_sound(&h, 150, sound_b, &nb);
    wb = h.m.ay.writes - wb;
    CHECK(snap_same(&ahead, &h.m, n), "%s: a restored machine should run to the same state", n);
    CHECK(snap_same_sound(sound_a, na, sound_b, nb, n), "%s: and make the same sound", n);
    CHECK(wa > 0 && wa == wb, "%s: the same AY writes: %u and %u", n, (unsigned)wa, (unsigned)wb);

    /* The restored program finishes, and the machine takes typing. */
    CHECK(run_until_row(&h, "DONE", 3000), "%s: the restored program never finished", n);
    /* 1.0 drops keys typed before its prompt, restored or not. */
    guest_fields(&h, 50);
    guest_type(&h, "PRINT 6*7\r");
    if (guest_find_row(&h.m, "42", 0) < 0) guest_dump(&h.m, stderr);
    CHECK(guest_find_row(&h.m, "42", 0) >= 0, "%s: typing after a restore", n);

    /* The other ROM or RAM refuses it, by name; powered on as the state
     * says, each loads it, and runs on to where the original arrived. */
    oric_ram_t other_ram = ram == ORIC_RAM_16K ? ORIC_RAM_48K : ORIC_RAM_16K;
    CHECK(guest_boot(&h, rom, other_ram), "%s: boot the other RAM", n);
    CHECK(mem_check(&snap, &h.m, &info) == SNAP_OTHER_RAM && info.ram == ram,
          "%s: in the other RAM: %s", n, snapshot_status_str(mem_check(&snap, &h.m, NULL)));
    switched(&info, n, "from the other RAM");
    rom_id_t other_rom = rom == ROM_BASIC10 ? ROM_BASIC11 : ROM_BASIC10;
    CHECK(guest_boot(&h, other_rom, other_ram), "%s: boot the other ROM", n);
    CHECK(mem_check(&snap, &h.m, &info) == SNAP_OTHER_ROM && info.rom == rom,
          "%s: in the other ROM: %s", n, snapshot_status_str(mem_check(&snap, &h.m, NULL)));
    switched(&info, n, "from the other ROM and RAM");
    return 0;
}

/* A CSAVE through the trap, saved with the header kept and the data not
 * yet asked for (tape.h). Where the ROM is at a field boundary with the
 * header kept, the state is taken there; otherwise at the instruction. */
static int kept_header(rom_id_t rom, oric_ram_t ram) {
    const char *n = name_of(rom, ram);
    CHECK(guest_boot(&g, rom, ram), "%s: boot", n);
    guest_type(&g, "10 REM KEPT ACROSS\r");
    guest_type(&g, "CSAVE\"KEPT\"");
    keymatrix_event(&g.k, KEY_EV_PRESSED, PICOCALC_KEY_ENTER);
    keymatrix_event(&g.k, KEY_EV_RELEASED, PICOCALC_KEY_ENTER);
    int boundary = 0;
    for (int i = 0; i < 500 && g.m.tape.op != TAPE_SAVE; i++) {
        oric_copy(&prev, &g.m);
        guest_fields(&g, 1);
        boundary = i;
    }
    CHECK(g.m.tape.op == TAPE_SAVE, "%s: CSAVE never reached the trap", n);
    tape_t want = g.m.tape;

    oric_copy(&at, &prev);
    const char *where = "a field boundary";
    if (!at.tape.header_kept) {
        where = "an instruction";
        for (int i = 0; i < 200000 && !at.tape.header_kept && at.tape.op == TAPE_NONE; i++)
            oric_run(&at, 1);
    }
    CHECK(at.tape.header_kept && at.tape.op == TAPE_NONE,
          "%s: no point with the header kept and no request", n);
    printf("  %s: header kept at %s, field %d\n", n, where, boundary);
    CHECK(mem_save(&snap, &at) == SNAP_OK, "%s: save with the header kept", n);

    CHECK(guest_boot(&h, rom, ram), "%s: boot the other", n);
    guest_type(&h, "PRINT 2+2\r");
    CHECK(mem_load(&snap, &h.m) == SNAP_OK, "%s: load", n);
    keymatrix_init(&h.k);
    CHECK(snap_same(&at, &h.m, n), "%s: the kept header is carried", n);
    CHECK(run_to_save(&h, 500), "%s: the restored CSAVE never asked for its file", n);
    CHECK(memcmp(h.m.tape.raw, want.raw, TAP_HEADER_LEN) == 0 &&
          h.m.tape.name_len == want.name_len && h.m.tape.name_len == 4 &&
          memcmp(h.m.tape.name, "KEPT", 4) == 0 && h.m.tape.start == want.start &&
          h.m.tape.end == want.end, "%s: the restored CSAVE asks for another file", n);

    /* Control: without the kept header the data goes out through the
     * ROM's own routine, and no file is asked for. The request just
     * made is declined first: a load is refused while one waits. */
    CHECK(mem_load(&snap, &h.m) == SNAP_BUSY, "%s: a load with a request waiting", n);
    oric_tape_decline(&h.m);
    CHECK(mem_load(&snap, &h.m) == SNAP_OK, "%s: load again", n);
    keymatrix_init(&h.k);
    h.m.tape.header_kept = false;
    CHECK(!run_to_save(&h, 500), "%s: control: no kept header, yet a file was asked for", n);
    return 0;
}

int main(void) {
    const char *dir;
    if (!guest_find_roms(&dir)) {
        printf("skipped: basic10.rom and basic11b.rom are not both in %s\n", dir);
        return TEST_SKIP_CODE;
    }
    for (int r = ROM_BASIC10; r <= ROM_BASIC11; r++) {
        for (int ram = ORIC_RAM_16K; ram <= ORIC_RAM_48K; ram++) {
            if (program((rom_id_t)r, (oric_ram_t)ram)) return 1;
            if (kept_header((rom_id_t)r, (oric_ram_t)ram)) return 1;
        }
    }
    TEST_DONE();
}
