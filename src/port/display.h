/* display.h — what core 1 puts on the panel (design.md §7).
 *
 * Core 1 only. Owns the DMA line buffers (§4.3). There is no
 * framebuffer (§7.1): every pixel sent is generated as it goes.
 *
 * M7: the presenter, the decoded-cell shadow with its dirty bands
 * (§7.3), and the perf and status lines, adapted from pico-ace's.
 */
#ifndef PICO_ORIC_DISPLAY_H
#define PICO_ORIC_DISPLAY_H

#include <stdint.h>

/* The bring-up pattern (design.md §15.2 M6): a 1-px white border exactly
 * on the guest's 240x224 rectangle at (40,48), with a 16x16 block in each
 * inside corner (red top-left, green top-right, blue bottom-left, yellow
 * bottom-right), so a mirrored axis or swapped R/B shows as the wrong
 * colour in the wrong corner. A 1-px grey frame on the panel's own edge
 * shows that all 320x320 are addressed. Everything else is black. */
void display_test_pattern(void);

/* The blit costs M6 measures, each the wall time of a whole operation on
 * the guest's rectangle: one colour by DMA with the read address held;
 * every row from a line buffer, as the presenter will send them; and one
 * 240-pixel row with its own window. Leaves the rectangle black. */
void display_measure(uint32_t *fill_us, uint32_t *blit_us, uint32_t *row_us);

#endif /* PICO_ORIC_DISPLAY_H */
