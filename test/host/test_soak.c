/* test_soak.c — the soak program, tools/soak.bas, run on the Atmos 48K
 * before the board runs it for 30 minutes (design.md §13.5, §15 M12).
 *
 * Typed as tools/soak.sh types it, a line at a time and RUN; then, as the
 * soak does, H and later J typed while it runs. What soak-check.py reads
 * off the board must be here first: the line "C <count> K <key>" with the
 * count rising and both keys read (72, 74), the hires screen drawn, and
 * the AY's envelope started once a pass. A control types a program with
 * a mistake in it, which stops at an error: the count must not rise, and
 * the envelope starts only for the ROM's own sounds, the keys' clicks and
 * the error's, which the program's run exceeds by a start a pass.
 *
 *   test_soak --write DIR    also writes the program as DIR/SOAK.tap,
 *                            saved by CSAVE"SOAK",AUTO through the tape
 *                            trap, for the release build's soak, which
 *                            has no UART to type it (§15.2 M15)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "guest.h"
#include "tap.h"
#include "tape.h"
#include "test_util.h"

static guest_t g;
static const char *s_write_dir;

/* The count and key on "C n K k", on any row; false if none shows. */
static bool status(long *n, long *k) {
    for (int r = 0; r < 28; r++) {
        const char *s = strstr(guest_row(&g.m, r), "C ");
        if (s && sscanf(s, "C %ld K %ld", n, k) == 2) return true;
    }
    return false;
}

/* Run up to `fields`, watching the status line; the last count and key
 * seen, and whether hires showed. */
static void watch(int fields, long *n, long *k, bool *hires) {
    for (int f = 0; f < fields; f++) {
        guest_fields(&g, 1);
        long a, b;
        if (status(&a, &b)) {
            *n = a;
            *k = b;
        }
        if (g.m.frame_mode & ULA_MODE_HIRES) *hires = true;
    }
}

static void type_program(const char *path, const char *mistake) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "%s: cannot open\n", path);
        exit(1);
    }
    char line[256];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0]) continue;
        if (mistake && !strncmp(line, "210 ", 4)) snprintf(line, sizeof line, "%s", mistake);
        guest_type(&g, line);
        guest_press(&g, PICOCALC_KEY_ENTER, false);
        /* The ROM stores the line before it reads the next key: a key
         * typed at once is lost, so soak.sh waits a second (and the line
         * holds 78 characters, which soak.bas keeps under). */
        guest_fields(&g, 25);
    }
    fclose(f);
}

/* CSAVE"SOAK",AUTO, the trap's save served into DIR/SOAK.tap as the
 * port's tapeio writes one (tape.h); then the ROM finishes. */
static bool save_tap(const char *dir) {
    static uint8_t buf[TAP_HEADER_MAX + 0x10000];
    size_t len = 0;
    guest_type(&g, "CSAVE\"SOAK\",AUTO");
    guest_press(&g, PICOCALC_KEY_ENTER, false);
    for (int f = 0; f < 500 && !len; f++) {
        const tape_t *t = oric_tape_pending(&g.m);
        if (t && t->op == TAPE_SAVE) {
            len = tap_encode_header(t->raw, t->name, t->name_len, buf);
            for (uint32_t i = 0; i < t->len; i++)
                buf[len++] = oric_peek(&g.m, (uint16_t)(t->start + i));
            oric_tape_save_end(&g.m);
        }
        guest_fields(&g, 1);
    }
    if (!len) return false;
    guest_fields(&g, 50);
    char path[512];
    snprintf(path, sizeof path, "%s/SOAK.tap", dir);
    FILE *f = fopen(path, "wb");
    bool ok = f && fwrite(buf, 1, len, f) == len;
    if (f) ok = fclose(f) == 0 && ok;
    if (ok) printf("  %s: %zu bytes\n", path, len);
    return ok;
}

/* One run: the count after `fields`, and after `fields` more, with H and
 * then J typed between. */
static bool run(const char *mistake, long *n0, long *n1, long *kh, long *kj, bool *hires,
                uint32_t *ay, uint32_t *env) {
    if (!guest_boot(&g, ROM_BASIC11, ORIC_RAM_48K)) return false;
    type_program(PICO_ORIC_SOURCE_DIR "/tools/soak.bas", mistake);
    if (s_write_dir && !mistake) CHECK(save_tap(s_write_dir), "SOAK.tap not written");
    /* The keyboard's scan writes port A at every column, which counts as
     * an AY write (ay8912.h); only a sound starts the envelope. */
    uint32_t w0 = g.m.ay.writes, e0 = g.m.ay.env_starts;
    guest_type(&g, "RUN");
    guest_press(&g, PICOCALC_KEY_ENTER, false);
    long k = -1;
    *n0 = *n1 = -1;
    *hires = false;
    watch(500, n0, &k, hires);
    guest_type(&g, "H");
    watch(500, n1, kh, hires);
    guest_type(&g, "J");
    watch(500, n1, kj, hires);
    *ay = g.m.ay.writes - w0;
    *env = g.m.ay.env_starts - e0;
    return true;
}

int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "--write") == 0) s_write_dir = argv[2];
    const char *dir;
    if (!guest_find_roms(&dir)) {
        printf("test_soak: SKIP (no BASIC ROMs in %s)\n", dir);
        return 77;
    }

    long n0, n1, kh, kj;
    bool hires;
    uint32_t ay, env;
    CHECK(run(NULL, &n0, &n1, &kh, &kj, &hires, &ay, &env), "the Atmos 48K did not boot");
    printf("  soak.bas: count %ld then %ld, keys %ld %ld, hires %s, %u AY writes, "
           "%u envelope starts\n", n0, n1, kh, kj, hires ? "yes" : "no", (unsigned)ay,
           (unsigned)env);
    if (n1 <= n0) guest_dump(&g.m, stderr);
    CHECK(n0 > 0, "no status line after 500 fields");
    CHECK(n1 > n0 + 2, "the count did not rise (%ld to %ld)", n0, n1);
    CHECK(kh == 'H', "H typed, the program read %ld", kh);
    CHECK(kj == 'J', "J typed, the program read %ld", kj);
    CHECK(hires, "hires never drawn");
    CHECK(ay > 100, "%u AY writes", (unsigned)ay);
    uint32_t env_run = env;
    long passes = n1 - n0;

    /* The control: line 210 misspelt, so the program stops at its first
     * GOSUB and the count never shows. */
    CHECK(run("210 PRONT \"C\";N;\"K\";K", &n0, &n1, &kh, &kj, &hires, &ay, &env),
          "the Atmos 48K did not boot");
    CHECK(n1 <= 0, "a program stopped at an error showed a count (%ld)", n1);
    printf("  control: stopped at an error, %u AY writes (the keyboard's scan), "
           "%u envelope starts (the ROM's clicks and error)\n", (unsigned)ay, (unsigned)env);
    CHECK(env_run >= env + (uint32_t)passes, "the program's %u envelope starts are not the "
          "control's %u and one a pass for %ld passes", (unsigned)env_run, (unsigned)env, passes);

    TEST_DONE();
}
