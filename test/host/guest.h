/* guest.h — a real Oric, on the host, for the tests that need its ROM
 * (design.md §13.3).
 *
 * Needs the user's ROM images, which the tree does not ship (§10.2). It
 * looks in PICO_ORIC_ROMS if that is set, else the repository's roms/
 * staging directory, and recognises images by SHA-1 whatever they are
 * called (romset.h).
 *
 * Keys go straight into the matrix (oric_key_set). The cell for each
 * character is looked up in the ROM's own key table, which both ROMs
 * index column x 8 + row, unshifted then shifted (§2.4). That is the
 * ROM's map, not a transcription; M5's sweep settles it and brings the
 * southbridge's event path.
 */
#ifndef PICO_ORIC_TEST_GUEST_H
#define PICO_ORIC_TEST_GUEST_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "oric.h"
#include "romset.h"

typedef struct {
    oric_t   m;
    rom_id_t rom;
    /* Guest cycles from power-on until "Ready" first showed on the
     * screen, to within GUEST_READY_STEP; 0 if it never did (§15.2 M3). */
    uint64_t ready_cycles;
} guest_t;

/* Scan for images. True if both BASIC ROMs were found; *dir is where it
 * looked, for the skip message. */
bool guest_find_roms(const char **dir);
bool guest_have_rom(rom_id_t r);
const uint8_t *guest_rom_image(rom_id_t r);

/* How finely guest_boot looks for "Ready", in cycles. */
#define GUEST_READY_STEP 100u

/* Build the machine, power on, and run until "Ready" shows, or for ten
 * seconds of guest time. True if it showed. */
bool guest_boot(guest_t *g, rom_id_t rom, oric_ram_t ram);

/* The same, on a machine the caller has already built and loaded. */
bool guest_boot_machine(guest_t *g);

void guest_fields(guest_t *g, int n);

/* The matrix cell the ROM's table gives a character, and whether it
 * needs SHIFT. False if the ROM has no key for it. '\n' is RETURN. */
bool guest_key_for(const guest_t *g, char c, int *row, int *col, bool *shift);

/* Type a string, a key at a time, held and released at a pace the ROM
 * keeps up with, then run a few fields for it to act on the last one. */
void guest_type(guest_t *g, const char *s);

/* The text on one of the 28 screen rows at #BB80, as ASCII: attribute
 * bytes as spaces, inverse as the plain character, trailing spaces
 * trimmed. Row 0 is the ROM's status line. The buffer is reused. */
const char *guest_row(const oric_t *m, int row);

/* The first row at or after `from` whose text is `s`, after the paper
 * cells the ROM leaves at the left of each line; -1 if none. */
int guest_find_row(const oric_t *m, const char *s, int from);

/* The whole screen, a row per line, for a failing test's output. */
void guest_dump(const oric_t *m, FILE *f);

#endif /* PICO_ORIC_TEST_GUEST_H */
