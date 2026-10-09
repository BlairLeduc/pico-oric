/* status.c — what the perf line says (status.h, design.md §7.5). */

#include "status.h"

#include <stdio.h>
#include <string.h>

static unsigned clamp(uint64_t v, unsigned max) {
    return v > max ? max : (unsigned)v;
}

/* v / d to the nearest, in 64 bits: rounding a count near the top of
 * its 32 must not wrap it to nothing. */
static uint64_t nearest(uint32_t v, uint32_t d) {
    return ((uint64_t)v + d / 2u) / d;
}

void status_perf_format(const perf_line_t *p, char out[ORIC_TEXT_COLS + 1]) {
    enum { W = ORIC_TEXT_COLS };
    unsigned pct = clamp(nearest(p->busy1000, 10u), 100u);
    unsigned head = clamp(p->head100, 999u * 100u + 99u);
    unsigned ms10 = clamp(nearest(p->present_us, 100u), 9999u);
    /* Every figure clamped, the line is at most 57 characters with two
     * spaces between the groups and 52 with one, which fits in 53. */
    char text[64];
    int n = 0;
    for (unsigned gap = 2; gap >= 1; gap--) {
        const char *sp = gap == 2 ? "  " : " ";
        n = snprintf(text, sizeof text, "C0 %u%% %u.%02ux%sLCD %u.%ums%sDrop %u%sUR %u %u%s%uHz",
                     pct, head / 100u, head % 100u, sp, ms10 / 10u, ms10 % 10u, sp,
                     clamp(p->dropped, 999u), sp, clamp(p->underruns, 9999u),
                     clamp(p->late, 999u), sp, clamp(p->hz, 99u));
        if (n >= 0 && (size_t)n <= W) break;
    }
    size_t len = n < 0 ? 0u : (size_t)n < W ? (size_t)n : W;
    memset(out, ' ', W);
    memcpy(out, text, len);
    out[W] = 0;
}
