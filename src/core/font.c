/* font.c — the emulator's own fonts (design.md §7.6). */

#include "font.h"

#include <string.h>

bool font_from_rom(rom_id_t id, const uint8_t *rom, uint8_t out[ORIC_CHARSET_BYTES]) {
    /* Read 2026-10-08: the glyph for 'A' (#08 #14 #22 #22 #3E #22 #22
     * #00) is at #FD78 in 1.0 and #FD80 in 1.1, 33 glyphs into a table
     * that starts at space and ends at each ROM's key table (§16). */
    uint16_t table;
    switch (id) {
    case ROM_BASIC10: table = 0xFC70u; break;
    case ROM_BASIC11: table = 0xFC78u; break;
    default: return false;
    }
    memset(out, 0, ORIC_CHARSET_BYTES);
    memcpy(out + FONT_FIRST * ORIC_GLYPH_H, rom + (table - ORIC_ROM_BASE),
           FONT_GLYPHS * ORIC_GLYPH_H);
    return true;
}

void font_fallback_charset(uint8_t out[ORIC_CHARSET_BYTES]) {
    memset(out, 0, ORIC_CHARSET_BYTES);
    memcpy(out + FONT_FIRST * ORIC_GLYPH_H, font_fallback, sizeof font_fallback);
}
