/* textpage.c — a page of text (textpage.h). */

#include "textpage.h"

#include <string.h>

/* Where the ULA finds the text screen and the standard set in text mode
 * (§2.2), as offsets into the frame's window. */
#define TEXT_OFF    (ORIC_TEXT_BASE - ORIC_VIDEO_BASE)
#define CHARSET_OFF (ORIC_CHARSET_TEXT - ORIC_VIDEO_BASE)

/* The Oric's codes are ASCII from #20 to #7F (§7.6); an attribute stays
 * itself, so a page can colour its text, and the rest are blanks. */
static uint8_t glyph(char c, bool inverse) {
    uint8_t a = (uint8_t)c;
    if (a > 0x7Fu) a = ' ';
    return inverse ? (uint8_t)(a | 0x80u) : a;
}

uint8_t *textpage_row(oric_frame_t *f, int row) {
    return f->window + TEXT_OFF + (unsigned)row * ORIC_SCREEN_COLS;
}

void textpage_clear(oric_frame_t *f, const uint8_t charset[ORIC_CHARSET_BYTES]) {
    memset(f, 0, sizeof *f);
    memcpy(f->window + CHARSET_OFF, charset, ORIC_CHARSET_BYTES);
    memset(f->window + TEXT_OFF, ' ', ORIC_SCREEN_ROWS * ORIC_SCREEN_COLS);
    f->mode = ULA_MODE_50HZ;
    f->blink_on = true;
}

void textpage_put(oric_frame_t *f, int row, int col, const char *s, bool inverse) {
    if (row < 0 || row >= TEXT_ROWS) return;
    uint8_t *r = textpage_row(f, row);
    for (; *s && col < TEXT_COLS; s++, col++)
        if (col >= 0) r[col] = glyph(*s, inverse);
}

void textpage_line(oric_frame_t *f, int row, const char *s, bool inverse) {
    if (row < 0 || row >= TEXT_ROWS) return;
    uint8_t *r = textpage_row(f, row);
    for (int col = 0; col < TEXT_COLS; col++) {
        char c = *s ? *s++ : ' ';
        r[col] = glyph(c, inverse);
    }
}
