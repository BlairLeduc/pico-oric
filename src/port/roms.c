/* roms.c — the boot's ROM, and the page shown without it (roms.h). */

#include "roms.h"

#include <stdio.h>
#include <string.h>

#include "font.h"
#include "textpage.h"

/* Oric attributes (§2.5): ink, then paper. */
#define INK_RED     0x01
#define INK_GREEN   0x02
#define INK_YELLOW  0x03
#define INK_WHITE   0x07
#define PAPER_BLUE  0x14

const char *roms_machine_name(rom_id_t rom, oric_ram_t ram) {
    if (rom == ROM_BASIC10) return ram == ORIC_RAM_16K ? "Oric-1 16K" : "Oric-1 48K";
    return ram == ORIC_RAM_16K ? "Atmos 16K" : "Atmos 48K";
}

void roms_charset(const card_job_t *job, const uint8_t image[ORIC_ROM_SIZE],
                  uint8_t out[ORIC_CHARSET_BYTES]) {
    /* An unrecognised image's table may not be a font at all (§10.2). */
    if (job->loaded != ROM_UNKNOWN && job->loaded_known &&
        font_from_rom(job->loaded, image, out))
        return;
    font_fallback_charset(out);
}

static const char *rom_title(rom_id_t id) {
    switch (id) {
    case ROM_BASIC10:   return "BASIC 1.0";
    case ROM_BASIC11:   return "BASIC 1.1";
    case ROM_MICRODISC: return "Microdisc";
    default:            return "?";
    }
}

/* One file's row: its name, what it is, and its state in colour. */
static void file_row(oric_frame_t *f, int row, const card_job_t *job, rom_id_t id) {
    static const struct { uint8_t ink; const char *word; } state[] = {
        [ROMFILE_ABSENT] = { INK_RED,    "missing" },
        [ROMFILE_KNOWN]  = { INK_GREEN,  "found" },
        [ROMFILE_NAMED]  = { INK_YELLOW, "unrecognised" },
    };
    textpage_put(f, row, 2, romset_images[id].file, false);
    textpage_put(f, row, 16, rom_title(id), false);
    uint8_t *r = textpage_row(f, row);
    r[26] = state[job->rom[id]].ink;
    textpage_put(f, row, 27, state[job->rom[id]].word, false);
}

void roms_page(const card_job_t *job, rom_id_t want, oric_ram_t ram,
               const uint8_t charset[ORIC_CHARSET_BYTES], oric_frame_t *f) {
    char s[TEXT_COLS + 1];
    textpage_clear(f, charset);

    /* The title on blue, as the Oric's own status row is drawn. */
    snprintf(s, sizeof s, "  pico-oric: no ROM for the %s", roms_machine_name(want, ram));
    textpage_line(f, 0, s, false);
    textpage_row(f, 0)[0] = PAPER_BLUE;
    textpage_row(f, 0)[1] = INK_WHITE;

    int row = 3;
    if (job->state != CARD_MOUNTED) {
        textpage_put(f, row++, 1, job->state == CARD_NONE ? "There is no SD card."
                                                         : "The SD card did not mount.", false);
        row++;
        textpage_put(f, row++, 1, "On a FAT card, put the ROMs in", false);
        textpage_put(f, row++, 1, "/oric/roms/, then reset.", false);
    } else {
        snprintf(s, sizeof s, "The %s needs %s,", roms_machine_name(want, ram),
                 rom_title(want));
        textpage_put(f, row++, 1, s, false);
        snprintf(s, sizeof s, "%s, in /oric/roms/.", romset_images[want].file);
        textpage_put(f, row++, 1, s, false);
        row++;
        textpage_put(f, row++, 1, "On the card:", false);
        row++;
        for (int id = 0; id < ROM_IMAGE_COUNT; id++) file_row(f, row++, job, (rom_id_t)id);
        row++;
        rom_id_t other = card_other_basic(want);
        if (job->loaded == other) {
            snprintf(s, sizeof s, "%s is here: press RETURN", romset_images[other].file);
            textpage_put(f, row++, 1, s, false);
            snprintf(s, sizeof s, "to start the %s instead.", roms_machine_name(other, ram));
            textpage_put(f, row++, 1, s, false);
        } else {
            textpage_put(f, row++, 1, "Put it on the card, then reset.", false);
        }
    }

    row = 22;
    textpage_put(f, row++, 1, "The ROMs are identified by SHA-1.", false);
    textpage_put(f, row++, 1, "README.md says where to get them", false);
    textpage_put(f, row++, 1, "and how to check them.", false);
}
