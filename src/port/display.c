/* display.c — what core 1 puts on the panel (design.md §7). */

#include "display.h"

#include <string.h>

#include "pico/stdlib.h"

#include "lcd.h"

#define RGB565(r, g, b) \
    (uint16_t)((((r) & 0xF8u) << 8) | (((g) & 0xFCu) << 3) | ((b) >> 3))

#define SCREEN_W ORIC_PIXEL_W
#define SCREEN_H ORIC_PIXEL_H

_Static_assert(ORIC_SCREEN_X + SCREEN_W <= ORIC_PANEL_W &&
               ORIC_SCREEN_Y + SCREEN_H <= ORIC_PANEL_H,
               "the guest must fit on the panel");
_Static_assert(ORIC_PERF_Y + ORIC_GLYPH_H <= ORIC_SCREEN_Y,
               "the perf line must sit above the guest");
_Static_assert(ORIC_STATUS_Y >= ORIC_SCREEN_Y + SCREEN_H &&
               ORIC_STATUS_Y + ORIC_GLYPH_H <= ORIC_PANEL_H,
               "the status line must sit below the guest");
_Static_assert(ORIC_TEXT_X + ORIC_TEXT_COLS * ORIC_GLYPH_W <= ORIC_LINEBUF_PIXELS,
               "a text line must fit a line buffer");

/* What was last sent to the guest's rectangle, as decoded cells (§7.3). */
static ula_shadow_t s_shadow;

/* The emulator's font, for the two lines (§7.6). */
static uint8_t s_font[ORIC_CHARSET_BYTES];

/* DMA ping-pong (hardware-notes.md §4.6), the panel's width so that the
 * text lines can use them too. */
static uint16_t s_line[ORIC_LINEBUF_COUNT][ORIC_LINEBUF_PIXELS];

/* What each line shows now: blank, as lcd_init left the panel. */
typedef struct {
    unsigned y;
    char     text[ORIC_TEXT_COLS];
} text_line_t;

static text_line_t s_perf   = { .y = ORIC_PERF_Y };
static text_line_t s_status = { .y = ORIC_STATUS_Y };

/* Grey, so the lines do not read as the guest's. */
#define TEXT_INK RGB565(0x90, 0x90, 0x90)

void display_init(const uint8_t font[ORIC_CHARSET_BYTES]) {
    memcpy(s_font, font, sizeof s_font);
    memset(s_perf.text, ' ', sizeof s_perf.text);
    memset(s_status.text, ' ', sizeof s_status.text);
    s_shadow.valid = false;
}

void display_invalidate(void) {
    s_shadow.valid = false;
}

void display_present(const oric_frame_t *f, display_stats_t *st) {
    uint32_t t0 = time_us_32();
    display_stats_t s = { .full = !s_shadow.valid };

    ula_band_t bands[ORIC_BAND_COUNT];
    ula_diff(&s_shadow, f, bands);

    /* The shadow now holds this frame's cells, and is what the rows are
     * generated from. */
    unsigned cur = 0;
    for (unsigned b = 0; b < ORIC_BAND_COUNT; b++) {
        unsigned c0 = bands[b].c0, c1 = bands[b].c1;
        if (c0 > c1) continue;
        unsigned w = (c1 - c0 + 1u) * ORIC_GLYPH_W;
        unsigned y0 = b * ORIC_BAND_LINES;
        lcd_blit_begin(ORIC_SCREEN_X + c0 * ORIC_GLYPH_W, ORIC_SCREEN_Y + y0, w,
                       ORIC_BAND_LINES);
        for (unsigned y = y0; y < y0 + ORIC_BAND_LINES; y++) {
            /* The buffer not on the wire: lcd_blit_row waits out the
             * previous row's DMA before starting this one. */
            cur ^= 1u;
            ula_row(s_shadow.cell[y], c0, c1, ula_palette_rgb565, s_line[cur]);
            lcd_blit_row(s_line[cur], w);
        }
        lcd_blit_end();
        s.bands++;
        s.pixels += w * ORIC_BAND_LINES;
    }

    s.us = time_us_32() - t0;
    if (st) *st = s;
}

/* Pixel row r of a text line, the panel's width: the glyph's bits 5-0,
 * bit 5 leftmost, as the Oric's sit (§7.6). */
static void text_row(const text_line_t *l, unsigned r, uint16_t *px) {
    for (unsigned x = 0; x < ORIC_TEXT_X; x++) *px++ = 0x0000;
    for (unsigned c = 0; c < ORIC_TEXT_COLS; c++) {
        uint8_t bits = s_font[(uint8_t)(l->text[c] & 0x7F) * ORIC_GLYPH_H + r];
        for (unsigned b = 0; b < ORIC_GLYPH_W; b++)
            *px++ = (bits & (0x20u >> b)) ? TEXT_INK : 0x0000;
    }
    for (unsigned x = ORIC_TEXT_X + ORIC_TEXT_COLS * ORIC_GLYPH_W; x < ORIC_PANEL_W; x++)
        *px++ = 0x0000;
}

static void draw_line(text_line_t *l, const char *text) {
    char line[ORIC_TEXT_COLS];
    size_t n = strnlen(text, ORIC_TEXT_COLS);
    memcpy(line, text, n);
    memset(line + n, ' ', ORIC_TEXT_COLS - n);
    if (memcmp(line, l->text, ORIC_TEXT_COLS) == 0) return;
    memcpy(l->text, line, ORIC_TEXT_COLS);

    unsigned cur = 0;
    lcd_blit_begin(0, l->y, ORIC_PANEL_W, ORIC_GLYPH_H);
    for (unsigned r = 0; r < ORIC_GLYPH_H; r++) {
        cur ^= 1u;
        text_row(l, r, s_line[cur]);
        lcd_blit_row(s_line[cur], ORIC_PANEL_W);
    }
    lcd_blit_end();
}

void display_perf(const char *text) {
    draw_line(&s_perf, text);
}

void display_status(const char *text) {
    draw_line(&s_status, text);
}

const uint8_t *display_font(void) {
    return s_font;
}

void display_panel_row(unsigned y, uint16_t *px) {
    for (unsigned x = 0; x < ORIC_PANEL_W; x++) px[x] = 0x0000;
    if (y >= ORIC_SCREEN_Y && y < ORIC_SCREEN_Y + SCREEN_H) {
        /* Invalidated, the cells are still what was last sent; before
         * the first present they are zero, black on black. */
        ula_row(s_shadow.cell[y - ORIC_SCREEN_Y], 0, ORIC_SCREEN_COLS - 1u,
                ula_palette_rgb565, px + ORIC_SCREEN_X);
    } else if (y >= s_perf.y && y < s_perf.y + ORIC_GLYPH_H) {
        text_row(&s_perf, y - s_perf.y, px);
    } else if (y >= s_status.y && y < s_status.y + ORIC_GLYPH_H) {
        text_row(&s_status, y - s_status.y, px);
    }
}
