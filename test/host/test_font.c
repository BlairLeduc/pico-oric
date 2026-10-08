/* test_font.c — the emulator's fonts (design.md §7.6, §15.2 M4).
 *
 * The ROM-font expansion must equal what each ROM writes into character
 * RAM at power-on, read back from a booted machine in both RAM fits: the
 * expansion reads the ROM's table, the check reads what the ROM's own
 * code put in RAM. The control: the same comparison one glyph out of
 * place, which must fail. (The other ROM's table cannot be the control:
 * the two fonts are identical.)
 *
 * Needs the ROMs; skips without them. The fallback font, which CI has,
 * is drawn by test_golden.
 */

#include <string.h>

#include "font.h"
#include "guest.h"
#include "test_util.h"

static guest_t g;

int main(void) {
    const char *dir;
    if (!guest_find_roms(&dir)) {
        printf("skipped: basic10.rom and basic11b.rom are not both in %s\n", dir);
        return TEST_SKIP_CODE;
    }

    static const rom_id_t roms[2] = { ROM_BASIC10, ROM_BASIC11 };
    uint8_t font[2][ORIC_CHARSET_BYTES];
    for (int i = 0; i < 2; i++)
        CHECK(font_from_rom(roms[i], guest_rom_image(roms[i]), font[i]), "font_from_rom %d", i);
    uint8_t none[ORIC_CHARSET_BYTES];
    CHECK(!font_from_rom(ROM_MICRODISC, guest_rom_image(ROM_BASIC11), none),
          "the Microdisc's EPROM has no font");

    const unsigned first = FONT_FIRST * ORIC_GLYPH_H;
    for (int i = 0; i < 2; i++) {
        for (int fit = 0; fit < 2; fit++) {
            CHECK(guest_boot(&g, roms[i], fit ? ORIC_RAM_16K : ORIC_RAM_48K), "boot");
            uint8_t ram[ORIC_CHARSET_BYTES];
            for (unsigned a = 0; a < ORIC_CHARSET_BYTES; a++)
                ram[a] = oric_peek(&g.m, (uint16_t)(ORIC_CHARSET_TEXT + a));
            const char *n = i ? "1.1" : "1.0";
            CHECK(memcmp(ram + first, font[i] + first, ORIC_CHARSET_BYTES - first) == 0,
                  "%s %s: the expansion should equal #B500-#B7FF as the ROM wrote it", n,
                  fit ? "16K" : "48K");
            CHECK(memcmp(ram + first + 8u, font[i] + first, ORIC_CHARSET_BYTES - first - 8u) != 0,
                  "%s: the control, one glyph out of place, should differ", n);

            /* #B400-#B4FF is attribute codes, which the copy does not
             * write: it holds #55 after boot (§16). */
            unsigned untouched = 0;
            for (unsigned a = 0; a < first; a++) untouched += ram[a] == 0x55u;
            CHECK(untouched == first, "%s: #B400-#B4FF should hold #55, unwritten by the copy",
                  n);
        }
    }

    /* For the record: where the two ROMs' fonts differ. */
    unsigned differ = 0;
    for (unsigned code = FONT_FIRST; code < 128u; code++)
        differ += memcmp(font[0] + code * 8u, font[1] + code * 8u, 8) != 0;
    printf("the two ROMs' fonts differ in %u glyph(s)\n", differ);

    TEST_DONE();
}
