/* status.h — what the perf line says (design.md §7.5, §14).
 *
 * The perf line is one row of text above the guest, for watching; the
 * heartbeat is for measuring (EL §12). Its figures are host counters,
 * not guest state: core 0 writes them once a second as whole 32-bit
 * words, each single-copy atomic, and core 1 reads them. A read across a
 * write can mix two seconds, which shows for a second and is harmless.
 *
 * Copied from pico-ace and adapted: the Oric's line carries the field
 * rate where the Ace's carried the audio counters, which return with M8.
 * The status line's own text, the tape and the disc, arrives with M10
 * and M14.
 */
#ifndef PICO_ORIC_STATUS_H
#define PICO_ORIC_STATUS_H

#include <stdint.h>

#include "config.h"

typedef struct {
    uint32_t busy1000;      /* core 0 outside the pacing wait, thousandths */
    uint32_t head100;       /* times real time it would run unpaced,
                               in hundredths                              */
    uint32_t present_us;    /* the longest present in the second (§7.3)   */
    uint32_t dropped;       /* snapshots dropped in the second            */
    uint32_t hz;            /* the last field's rate, 50 or 60 (§11.1)    */
} perf_line_t;

/* The perf line's text, ORIC_TEXT_COLS characters space-padded and a
 * NUL: `C0 31% 3.20x  LCD 12.6ms  Drop 0  50Hz`. A figure too wide is
 * shown at its widest rather than pushing the rest off. */
void status_perf_format(const perf_line_t *p, char out[ORIC_TEXT_COLS + 1]);

#endif /* PICO_ORIC_STATUS_H */
