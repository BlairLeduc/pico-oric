/* ula.c — the ULA's picture: decode, mode scan, rows and dirty bands
 * (design.md §7). */

#include "ula.h"

#include <string.h>

/* R, G, B from bit 0 (§2.5, §16: medium). */
const uint16_t ula_palette_rgb565[8] = {
    0x0000u,  /* black   */
    0xF800u,  /* red     */
    0x07E0u,  /* green   */
    0xFFE0u,  /* yellow  */
    0x001Fu,  /* blue    */
    0xF81Fu,  /* magenta */
    0x07FFu,  /* cyan    */
    0xFFFFu,  /* white   */
};

/* Window offsets of what the ULA fetches (§7.2). */
#define TEXT_OFF   (ORIC_TEXT_BASE - ORIC_VIDEO_BASE)
#define HIRES_OFF  (ORIC_HIRES_BASE - ORIC_VIDEO_BASE)

/* Where line y fetches its bytes in `mode`. Hires covers lines 0-199
 * whatever the attributes; the three text rows below come from the text
 * screen in either mode (§2.2, §16). */
static inline unsigned line_offset(unsigned y, uint8_t mode) {
    if ((mode & ULA_MODE_HIRES) && y < ORIC_HIRES_LINES)
        return HIRES_OFF + y * ORIC_SCREEN_COLS;
    return TEXT_OFF + (y / ORIC_GLYPH_H) * ORIC_SCREEN_COLS;
}

static inline bool is_attribute(uint8_t c) { return (c & 0x60u) == 0; }
static inline bool is_mode(uint8_t c) { return (c & 0x78u) == 0x18u; }

uint8_t ula_decode_line(const uint8_t *window, unsigned y, uint8_t mode, bool blink_on,
                        ula_cell_t out[ORIC_SCREEN_COLS]) {
    unsigned ink = 7, paper = 0, text = 0;
    unsigned off = line_offset(y, mode);
    for (unsigned x = 0; x < ORIC_SCREEN_COLS; x++) {
        uint8_t c = window[off + x];
        unsigned pattern = 0;
        if (is_attribute(c)) {
            /* Applied before its own cell is drawn, which shows paper
             * (§2.5, §16). */
            switch (c & 0x18u) {
            case 0x00u: ink = c & 7u; break;
            case 0x08u: text = c & 7u; break;
            case 0x10u: paper = c & 7u; break;
            default:
                /* From the next cell on, the fetch follows the new mode
                 * (§7.4, §16). */
                mode = c & 7u;
                off = line_offset(y, mode);
                break;
            }
        } else if ((mode & ULA_MODE_HIRES) && y < ORIC_HIRES_LINES) {
            pattern = c & 0x3Fu;
        } else {
            unsigned set = ((mode & ULA_MODE_HIRES) ? ORIC_CHARSET_HIRES : ORIC_CHARSET_TEXT) -
                           ORIC_VIDEO_BASE;
            if (text & ULA_TEXT_ALT) set += ORIC_CHARSET_BYTES;
            /* Double height: the glyph's top half on even text rows, its
             * bottom half on odd ones (§16). */
            unsigned row = ((text & ULA_TEXT_DOUBLE) ? y >> 1 : y) & 7u;
            pattern = window[set + (c & 0x7Fu) * ORIC_GLYPH_H + row] & 0x3Fu;
        }
        if ((text & ULA_TEXT_BLINK) && !blink_on) pattern = 0;
        unsigned i = ink, p = paper;
        if (c & 0x80u) {
            i ^= 7u;
            p ^= 7u;
        }
        out[x] = ULA_CELL(i, p, pattern);
    }
    return mode;
}

uint8_t ula_decode(const oric_frame_t *f, ula_cell_t out[ORIC_PIXEL_H][ORIC_SCREEN_COLS]) {
    uint8_t mode = f->mode;
    for (unsigned y = 0; y < ORIC_PIXEL_H; y++)
        mode = ula_decode_line(f->window, y, mode, f->blink_on, out[y]);
    return mode;
}

/* Whether a line's 40 bytes hold a mode attribute, eight at a time:
 * (b & #78) ^ #18 is zero exactly for one, and the test for a zero byte
 * in a word has no false negatives or positives as a whole. */
static inline bool line_has_mode(const uint8_t *p) {
    for (unsigned i = 0; i < ORIC_SCREEN_COLS; i += 8u) {
        uint64_t w;
        memcpy(&w, p + i, sizeof w);
        uint64_t x = (w & 0x7878787878787878ull) ^ 0x1818181818181818ull;
        if ((x - 0x0101010101010101ull) & ~x & 0x8080808080808080ull) return true;
    }
    return false;
}

uint8_t ula_scan_mode(const uint8_t *window, uint8_t mode) {
    /* A text row's eight lines fetch the same bytes. A line that starts
     * where the last one did, in the same mode, after a line with no mode
     * attribute, cannot hold one either, and is skipped. A line whose
     * bytes hold none is passed over a word at a time. */
    unsigned last_off = ~0u;
    uint8_t  last_mode = 0;
    for (unsigned y = 0; y < ORIC_PIXEL_H; y++) {
        unsigned off = line_offset(y, mode);
        if (off == last_off && mode == last_mode) continue;
        last_off = off;
        last_mode = mode;
        if (!line_has_mode(window + off)) continue;
        for (unsigned x = 0; x < ORIC_SCREEN_COLS; x++) {
            uint8_t c = window[off + x];
            if (is_mode(c)) {
                mode = c & 7u;
                off = line_offset(y, mode);
            }
        }
        last_off = ~0u;
    }
    return mode;
}

void ula_row(const ula_cell_t *line, unsigned c0, unsigned c1, const uint16_t palette[8],
             uint16_t *dst) {
    for (unsigned c = c0; c <= c1; c++) {
        ula_cell_t cell = line[c];
        uint16_t fg = palette[ULA_CELL_INK(cell)];
        uint16_t bg = palette[ULA_CELL_PAPER(cell)];
        unsigned p = ULA_CELL_PATTERN(cell);
        /* Six stores, unrolled (EL §5.2). */
        dst[0] = (p & 0x20u) ? fg : bg;
        dst[1] = (p & 0x10u) ? fg : bg;
        dst[2] = (p & 0x08u) ? fg : bg;
        dst[3] = (p & 0x04u) ? fg : bg;
        dst[4] = (p & 0x02u) ? fg : bg;
        dst[5] = (p & 0x01u) ? fg : bg;
        dst += ORIC_GLYPH_W;
    }
}

unsigned ula_diff(ula_shadow_t *s, const oric_frame_t *f, ula_band_t bands[ORIC_BAND_COUNT]) {
    ula_cell_t line[ORIC_SCREEN_COLS];
    uint8_t mode = f->mode;
    unsigned dirty = 0;
    for (unsigned b = 0; b < ORIC_BAND_COUNT; b++) {
        int lo = -1, hi = -1;
        for (unsigned r = 0; r < ORIC_BAND_LINES; r++) {
            unsigned y = b * ORIC_BAND_LINES + r;
            mode = ula_decode_line(f->window, y, mode, f->blink_on, line);
            ula_cell_t *was = s->cell[y];
            for (unsigned x = 0; x < ORIC_SCREEN_COLS; x++) {
                if (!s->valid || line[x] != was[x]) {
                    if (lo < 0 || (int)x < lo) lo = (int)x;
                    if ((int)x > hi) hi = (int)x;
                }
            }
            memcpy(was, line, sizeof line);
        }
        if (lo < 0) {
            bands[b].c0 = 1;
            bands[b].c1 = 0;
        } else {
            bands[b].c0 = (uint8_t)lo;
            bands[b].c1 = (uint8_t)hi;
            dirty++;
        }
    }
    s->valid = true;
    return dirty;
}
