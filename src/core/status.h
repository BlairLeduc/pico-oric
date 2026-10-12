/* status.h — what the perf line says (design.md §7.5, §14).
 *
 * The perf line is one row of text above the guest, for watching; the
 * heartbeat is for measuring (EL §12). Its figures are host counters,
 * not guest state: core 0 writes them once a second as whole 32-bit
 * words, each single-copy atomic, and core 1 reads them. A read across a
 * write can mix two seconds, which shows for a second and is harmless.
 *
 * Copied from pico-ace and adapted: the Oric's line carries the field
 * rate as well as the Ace's audio counters, which returned with M8, and
 * the pause line with M9. The status line's own text, the deck on the
 * left and the drive in use on the right, is pico-atom's layout.
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
    uint32_t underruns;     /* underrun samples since boot (§8.4)         */
    uint32_t late;          /* late refills since boot                    */
    uint32_t hz;            /* the last field's rate, 50 or 60 (§11.1)    */
} perf_line_t;

/* The perf line's text, ORIC_TEXT_COLS characters space-padded and a
 * NUL: `C0 31% 3.20x  LCD 12.6ms  Drop 0  UR 0 0  50Hz`, UR being the
 * underrun samples and the late refills. A figure too wide is shown at
 * its widest rather than pushing the rest off, and the gaps close to one
 * space when the counts have grown, so the last figure never goes. */
void status_perf_format(const perf_line_t *p, char out[ORIC_TEXT_COLS + 1]);

/* What the deck is doing, for the status line. */
typedef enum {
    STATUS_DECK_IDLE = 0,
    STATUS_DECK_PLAYING,
    STATUS_DECK_RECORDING,
} status_deck_t;

/* No drive's head is loaded, or there is no Microdisc. */
#define STATUS_NO_DRIVE 0xFFu

/* The status line's text, ORIC_TEXT_COLS characters space-padded and a
 * NUL: the deck on the left, `Tape: NAME.tap, playing`, and on the
 * right the drive whose head is loaded, `Drive 0: NAME.dsk`, as
 * pico-atom has them. `tape` is the deck's path and `disc` the drive's,
 * "" for none, each shown without its folder; the drive's name is
 * capped and the tape's shortened to fit what is left. `drive` is 0-3,
 * or STATUS_NO_DRIVE for nothing on the right. */
void status_format(status_deck_t deck, const char *tape, unsigned drive, const char *disc,
                   char out[ORIC_TEXT_COLS + 1]);

/* The status line while the guest is paused, whether or not the line
 * is on (design.md §12). */
void status_paused_format(char out[ORIC_TEXT_COLS + 1]);

#endif /* PICO_ORIC_STATUS_H */
