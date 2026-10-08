/* display.c — what core 1 puts on the panel (design.md §7). */

#include "display.h"

#include "pico/stdlib.h"

#include "config.h"
#include "lcd.h"

#define RGB565(r, g, b) \
    (uint16_t)((((r) & 0xF8u) << 8) | (((g) & 0xFCu) << 3) | ((b) >> 3))

#define SCREEN_W ORIC_PIXEL_W
#define SCREEN_H ORIC_PIXEL_H

_Static_assert(ORIC_SCREEN_X + SCREEN_W <= ORIC_PANEL_W &&
               ORIC_SCREEN_Y + SCREEN_H <= ORIC_PANEL_H,
               "the guest must fit on the panel");

/* DMA ping-pong (hardware-notes.md §4.6), the panel's width so that the
 * perf and status lines can use them too (M7). */
static uint16_t s_line[ORIC_LINEBUF_COUNT][ORIC_LINEBUF_PIXELS];

void display_test_pattern(void) {
    const unsigned x = ORIC_SCREEN_X, y = ORIC_SCREEN_Y;
    const unsigned w = SCREEN_W, h = SCREEN_H;
    const unsigned pw = ORIC_PANEL_W, ph = ORIC_PANEL_H;
    const uint16_t white = RGB565(0xFF, 0xFF, 0xFF);
    const uint16_t grey = RGB565(0x80, 0x80, 0x80);

    lcd_fill(0, 0, pw, ph, 0x0000);

    lcd_fill(0, 0, pw, 1, grey);
    lcd_fill(0, ph - 1u, pw, 1, grey);
    lcd_fill(0, 0, 1, ph, grey);
    lcd_fill(pw - 1u, 0, 1, ph, grey);

    lcd_fill(x, y, w, 1, white);               /* top    */
    lcd_fill(x, y + h - 1u, w, 1, white);      /* bottom */
    lcd_fill(x, y, 1, h, white);               /* left   */
    lcd_fill(x + w - 1u, y, 1, h, white);      /* right  */

    lcd_fill(x + 2u, y + 2u, 16, 16, RGB565(0xFF, 0x00, 0x00));
    lcd_fill(x + w - 18u, y + 2u, 16, 16, RGB565(0x00, 0xFF, 0x00));
    lcd_fill(x + 2u, y + h - 18u, 16, 16, RGB565(0x00, 0x00, 0xFF));
    lcd_fill(x + w - 18u, y + h - 18u, 16, 16, RGB565(0xFF, 0xFF, 0x00));
}

void display_measure(uint32_t *fill_us, uint32_t *blit_us, uint32_t *row_us) {
    const unsigned x = ORIC_SCREEN_X, y = ORIC_SCREEN_Y;

    /* Two different rows, so the blit is seen to happen. */
    for (unsigned i = 0; i < SCREEN_W; i++) {
        s_line[0][i] = RGB565(i, 0x40, 0xFF - i);
        s_line[1][i] = RGB565(0xFF - i, i, 0x40);
    }

    uint32_t t0 = time_us_32();
    lcd_fill(x, y, SCREEN_W, SCREEN_H, RGB565(0x00, 0x00, 0x80));
    *fill_us = time_us_32() - t0;

    /* Rows as the presenter will send them: lcd_blit_row waits out the
     * previous row's DMA before starting this one. */
    t0 = time_us_32();
    lcd_blit_begin(x, y, SCREEN_W, SCREEN_H);
    for (unsigned r = 0; r < SCREEN_H; r++) lcd_blit_row(s_line[r & 1u], SCREEN_W);
    lcd_blit_end();
    *blit_us = time_us_32() - t0;

    t0 = time_us_32();
    lcd_blit_begin(x, y + SCREEN_H / 2u, SCREEN_W, 1);
    lcd_blit_row(s_line[0], SCREEN_W);
    lcd_blit_end();
    *row_us = time_us_32() - t0;

    lcd_fill(x, y, SCREEN_W, SCREEN_H, 0x0000);
}
