/* textpage.h — a page of text for the screens the emulator draws itself:
 * the missing-ROM page now, the menu and About from M9 (design.md §7.6,
 * §12).
 *
 * The page is an Oric frame (ula.h): text mode at 50 Hz, a character set
 * at #B400 and 28 rows of 40 at #BB80, drawn by the same decode and row
 * generator as the guest's, so it costs no drawing code of its own and
 * the presenter's shadow covers it like any frame (§7.3). Codes are
 * ASCII, bit 7 inverse; a code below #20 is an Oric attribute (§2.5),
 * which shows as paper and colours the cells to its right.
 *
 * Adapted from pico-ace's, which wrote the Ace's 768 screen bytes.
 */
#ifndef PICO_ORIC_TEXTPAGE_H
#define PICO_ORIC_TEXTPAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"
#include "ula.h"

#define TEXT_COLS ((int)ORIC_SCREEN_COLS)
#define TEXT_ROWS ((int)ORIC_SCREEN_ROWS)

/* Oric attributes (§2.5): ink, then paper. Each takes a cell, which
 * shows as the paper in force. */
#define INK_RED     0x01
#define INK_GREEN   0x02
#define INK_YELLOW  0x03
#define INK_WHITE   0x07
#define PAPER_BLUE  0x14

/* A blank page in `charset`, the standard set's 128 glyphs (font.h):
 * white on black, every cell a space. */
void textpage_clear(oric_frame_t *f, const uint8_t charset[ORIC_CHARSET_BYTES]);

/* Text from (row, col), cut at the edge. */
void textpage_put(oric_frame_t *f, int row, int col, const char *s, bool inverse);

/* A whole row: the text, then blanks to the edge, all in one video. */
void textpage_line(oric_frame_t *f, int row, const char *s, bool inverse);

/* A title bar: the row on blue paper in white ink, as the Oric's own
 * status row is drawn. The attributes take the first two cells, so the
 * text should start with two spaces. */
void textpage_title(oric_frame_t *f, int row, const char *s);

/* The page's text as the Oric shows it, a row of 40 bytes. */
uint8_t *textpage_row(oric_frame_t *f, int row);

#endif /* PICO_ORIC_TEXTPAGE_H */
