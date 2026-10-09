/* test_status.c — the perf line's text (status.h, design.md §7.5).
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

    TEST_DONE();
}
