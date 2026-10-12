/* test_status.c — the perf and status lines' text (status.h, design.md §7.5).
 * Needs no ROM.
 */

#include <string.h>

#include "status.h"
#include "test_util.h"

static char line[ORIC_TEXT_COLS + 1];

/* The line with its padding taken off, for comparing. */
static const char *trimmed(void) {
    static char t[ORIC_TEXT_COLS + 1];
    memcpy(t, line, sizeof t);
    for (int i = ORIC_TEXT_COLS - 1; i >= 0 && t[i] == ' '; i--) t[i] = 0;
    return t;
}

int main(void) {
    perf_line_t p = { .busy1000 = 314, .head100 = 320, .present_us = 12570, .dropped = 0,
                      .hz = 50 };
    status_perf_format(&p, line);
    CHECK(strlen(line) == ORIC_TEXT_COLS, "one whole line: %zu", strlen(line));
    CHECK(strcmp(trimmed(), "C0 31% 3.20x  LCD 12.6ms  Drop 0  UR 0 0  50Hz") == 0,
          "a field at 50 Hz: [%s]", line);

    p.hz = 60;
    p.dropped = 7;
    p.present_us = 940;
    p.underruns = 128;
    p.late = 3;
    status_perf_format(&p, line);
    CHECK(strcmp(trimmed(), "C0 31% 3.20x  LCD 0.9ms  Drop 7  UR 128 3  60Hz") == 0,
          "a field at 60 Hz, dropping: [%s]", line);

    /* Rounded, not truncated: 31.5 % is 32 %, 12.55 ms is 12.6 ms. */
    p.busy1000 = 315;
    p.present_us = 12550;
    status_perf_format(&p, line);
    CHECK(strncmp(line, "C0 32% 3.20x  LCD 12.6ms", 24) == 0, "rounded: [%s]", line);

    /* Every figure at its widest, and past it: the line keeps its last
     * figure and its length. */
    perf_line_t big = { .busy1000 = 0xFFFFFFFFu, .head100 = 0xFFFFFFFFu,
                        .present_us = 0xFFFFFFFFu,
                        .dropped = 0xFFFFFFFFu, .underruns = 0xFFFFFFFFu,
                        .late = 0xFFFFFFFFu, .hz = 0xFFFFFFFFu };
    status_perf_format(&big, line);
    CHECK(strlen(line) == ORIC_TEXT_COLS, "still one line: [%s]", line);
    CHECK(strcmp(trimmed(), "C0 100% 999.99x LCD 999.9ms Drop 999 UR 9999 999 99Hz") == 0,
          "clamped: [%s]", line);

    perf_line_t zero = { 0 };
    status_perf_format(&zero, line);
    CHECK(strcmp(trimmed(), "C0 0% 0.00x  LCD 0.0ms  Drop 0  UR 0 0  0Hz") == 0, "before the first "
          "second: [%s]", line);

    /* ---- The status line: the deck left, the drive right ---------------- */
    status_format(STATUS_DECK_IDLE, "", STATUS_NO_DRIVE, "", line);
    CHECK(strlen(line) == ORIC_TEXT_COLS && trimmed()[0] == 0, "nothing to say: [%s]", line);

    status_format(STATUS_DECK_PLAYING, "/oric/tapes/Xenon.tap", STATUS_NO_DRIVE, "", line);
    CHECK(strcmp(trimmed(), "Tape: Xenon.tap, playing") == 0, "the deck alone: [%s]", line);

    status_format(STATUS_DECK_IDLE, "", 0, "/oric/discs/sedoric3.dsk", line);
    CHECK(strlen(line) == ORIC_TEXT_COLS, "one whole line: [%s]", line);
    CHECK(strcmp(line + ORIC_TEXT_COLS - 21, "Drive 0: sedoric3.dsk") == 0 && line[0] == ' ',
          "the drive alone, on the right: [%s]", line);

    status_format(STATUS_DECK_IDLE, "/oric/tapes/Xenon.tap", 1, "/oric/discs/game.dsk", line);
    CHECK(strncmp(line, "Tape: Xenon.tap ", 16) == 0 &&
          strcmp(line + ORIC_TEXT_COLS - 17, "Drive 1: game.dsk") == 0,
          "both, the deck left and the drive right: [%s]", line);

    status_format(STATUS_DECK_IDLE, "", 3, "", line);
    CHECK(strcmp(line + ORIC_TEXT_COLS - 13, "Drive 3: none") == 0, "an empty drive: [%s]", line);

    status_format(STATUS_DECK_RECORDING, "/oric/tapes/Xenon.tap", STATUS_NO_DRIVE, "", line);
    CHECK(strcmp(trimmed(), "Tape: recording") == 0, "recording: [%s]", line);

    /* Long names: the drive's is capped at 16, the tape's gives way to
     * it, and a space always parts them. */
    const char *long_tape = "/t/Une Tres Longue Histoire De Cassette Pour Oric Atmos.tap";
    const char *long_disc = "/d/A Disc Whose Name Goes On And On.dsk";
    status_format(STATUS_DECK_PLAYING, long_tape, 2, long_disc, line);
    CHECK(strlen(line) == ORIC_TEXT_COLS, "still one line: [%s]", line);
    CHECK(strcmp(line, "Tape: Une Tres Lon, playing Drive 2: A Disc Whose Nam") == 0,
          "the drive capped, the tape shortened: [%s]", line);

    /* ---- Paused --------------------------------------------------------- */
    status_paused_format(line);
    CHECK(strlen(line) == ORIC_TEXT_COLS && strncmp(trimmed(), "Paused", 6) == 0,
          "the paused line: '%s'", trimmed());

    TEST_DONE();
}
