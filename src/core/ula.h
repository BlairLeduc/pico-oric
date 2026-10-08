/* ula.h — the ULA's picture: decode, mode scan, rows and dirty bands
 * (design.md §7).
 *
 * The Oric's image is a function of the 10 KiB window #9800-#BFFF, the
 * mode at the field's start and the blink phase (§4.4), so there is no
 * framebuffer (§7.1). The decode turns the window into one cell per byte
 * (§7.2); the row generator turns cells into RGB565; the presenter's
 * shadow holds decoded cells, not VRAM bytes, so serial attributes, mode
 * changes, character-set edits, double height and blink are all caught
 * by one comparison (§7.3).
 *
 * The decode and the row generator belong to the presenter on core 1;
 * only ula_scan_mode runs on core 0, over the window, to learn the next
 * field's length (§4.3, §7.4). Nothing here reads guest RAM: it reads a
 * frame, or a window the caller owns.
 *
 * The rules follow §2.5 and §16, and agree with Oricutron's and MAME's
 * ULAs (§13.4): an attribute takes effect from its own cell, which shows
 * as paper; bit 7 inverts any cell, an attribute's too; a mode attribute
 * changes the fetch from the next cell and lasts across lines and fields.
 */
#ifndef PICO_ORIC_ULA_H
#define PICO_ORIC_ULA_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"

/* The mode attribute's three bits, #18-#1F (§2.5). Bit 0 is not known to
 * do anything. The ROMs write #1A: text at 50 Hz. */
#define ULA_MODE_HIRES  0x04u
#define ULA_MODE_50HZ   0x02u

/* The ULA's mode before anything sets it: Oricutron's power-up default,
 * text at 50 Hz (§16). */
#define ULA_MODE_POWER_ON  ULA_MODE_50HZ

/* The text attribute's bits, #08-#0F (§2.5). */
#define ULA_TEXT_ALT     0x01u   /* the alternate character set */
#define ULA_TEXT_DOUBLE  0x02u   /* double height               */
#define ULA_TEXT_BLINK   0x04u

/* What the presenter is handed each field (§4.4). */
typedef struct {
    uint8_t  window[ORIC_VIDEO_BYTES];  /* #9800-#BFFF as the ULA addresses it */
    uint8_t  mode;                      /* the mode attribute's bits at field start */
    bool     blink_on;                  /* blinking cells show this field          */
    uint32_t field;
    /* M10, M14: the status line's bytes (tape position, disc activity). */
} oric_frame_t;

/* One cell: everything its six pixels depend on (§7.2). Inverse and
 * blink are already folded in. */
typedef uint16_t ula_cell_t;

#define ULA_CELL(ink, paper, pattern) \
    ((ula_cell_t)(((unsigned)(ink) << 9) | ((unsigned)(paper) << 6) | (unsigned)(pattern)))
#define ULA_CELL_INK(c)      (((c) >> 9) & 7u)
#define ULA_CELL_PAPER(c)    (((c) >> 6) & 7u)
#define ULA_CELL_PATTERN(c)  ((c) & 0x3Fu)

/* Colours are three bits, R G B from bit 0 (§2.5), at full level. */
extern const uint16_t ula_palette_rgb565[8];

/* ---- Decode (§7.2) ---------------------------------------------------- */

/* Decode raster line y (0-223) into 40 cells, starting with ink 7, paper
 * 0, the standard set, single height and no blink, in `mode`. Returns
 * the mode after the line, which the next line starts in. */
uint8_t ula_decode_line(const uint8_t *window, unsigned y, uint8_t mode, bool blink_on,
                        ula_cell_t out[ORIC_SCREEN_COLS]);

/* The whole picture. Returns the mode after its last line. */
uint8_t ula_decode(const oric_frame_t *f, ula_cell_t out[ORIC_PIXEL_H][ORIC_SCREEN_COLS]);

/* ---- The mode scan (§7.4) -------------------------------------------- */

/* The same raster walk as the decode, testing only for mode attributes:
 * the mode after the ULA draws `window` from `mode`. Core 0 runs it at
 * each field's end for the next field's length and the next frame's
 * start mode. */
uint8_t ula_scan_mode(const uint8_t *window, uint8_t mode);

/* ---- Rows (§7.2) ------------------------------------------------------ */

/* Cells c0..c1 of a line as (c1 - c0 + 1) * 6 pixels into dst: per cell,
 * six pixels from palette[ink] or palette[paper] by the pattern's bits,
 * bit 5 leftmost. */
void ula_row(const ula_cell_t *line, unsigned c0, unsigned c1, const uint16_t palette[8],
             uint16_t *dst);

/* ---- Dirty bands (§7.3) ---------------------------------------------- */

/* What the presenter last sent to the panel, as decoded cells: 17.5 KiB.
 * `valid` false means the panel's contents are unknown, and the next diff
 * marks everything. */
typedef struct {
    ula_cell_t cell[ORIC_PIXEL_H][ORIC_SCREEN_COLS];
    bool       valid;
} ula_shadow_t;

/* A band's span, cells c0..c1; c0 > c1 when the band is clean. */
typedef struct {
    uint8_t c0, c1;
} ula_band_t;

/* Decode the frame a line at a time against the shadow, widen each
 * 8-line band to the span of cells that differ, and write the new cells
 * into the shadow. Returns the number of dirty bands; the presenter then
 * draws each band's span from the shadow's cells with ula_row. */
unsigned ula_diff(ula_shadow_t *s, const oric_frame_t *f, ula_band_t bands[ORIC_BAND_COUNT]);

#endif /* PICO_ORIC_ULA_H */
