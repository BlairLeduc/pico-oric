/* display.h — what core 1 puts on the panel (design.md §7).
 *
 * Core 1 only. Owns the presenter's shadow of decoded cells and the DMA
 * line buffers (§4.3). There is no framebuffer (§7.1): every pixel sent
 * is generated from a frame as it goes.
 *
 * Adapted from pico-ace's: the dirty-band presenter and the perf and
 * status lines stay; the shadow holds the ULA's decoded cells (§7.3).
 */
#ifndef PICO_ORIC_DISPLAY_H
#define PICO_ORIC_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"
#include "ula.h"

typedef struct {
    uint32_t us;        /* wall time of the present                    */
    uint16_t bands;     /* bands sent (28 for a full redraw)           */
    uint32_t pixels;    /* pixels on the wire                          */
    bool     full;      /* the shadow was invalid: everything was sent */
} display_stats_t;

/* The two text lines in `font`, the standard set's 128 glyphs (font.h),
 * which is copied; the shadow invalid. The panel is as lcd_init left it,
 * black. */
void display_init(const uint8_t font[ORIC_CHARSET_BYTES]);

/* Present one frame: decode it against the shadow (§7.3), send each
 * dirty band's span as one 8-row window in the guest's rectangle at
 * (40, 48) (§7.5), and make its cells the shadow. The frame may go back
 * to the pool as soon as this returns. */
void display_present(const oric_frame_t *f, display_stats_t *st);

/* Forget what is on the panel, so the next present sends everything. */
void display_invalidate(void);

/* The perf line at the panel's top and the status line at its foot
 * (§7.5, §12): ORIC_TEXT_COLS characters in the emulator's font, grey on
 * black, each drawn only when its text differs from what is there.
 * Shorter text is padded with spaces. */
void display_perf(const char *text);
void display_status(const char *text);

/* The emulator's font, as display_init was given it, for the pages core
 * 1 draws itself (textpage.h). */
const uint8_t *display_font(void);

/* Row y of the whole panel as it shows now, ORIC_PANEL_W pixels of
 * RGB565, regenerated from the shadow and the two lines rather than read
 * back: the screenshot's source (shotio.h). Black where nothing is drawn,
 * as lcd_init left it. */
void display_panel_row(unsigned y, uint16_t *px);

#endif /* PICO_ORIC_DISPLAY_H */
