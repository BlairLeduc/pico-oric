/* roms.h — the boot's ROM, and the page shown without it (design.md
 * §7.6, §10.2).
 *
 * Core 1, at boot, with core 0 waiting: card_roms has read the card and
 * loaded what it could into g_boot (handoff.h). A missing ROM shows a
 * page naming the missing file, never a blank screen; if the other
 * machine's ROM is there, the page says so and offers that machine.
 *
 * Adapted from pico-atom's roms_explain.
 */
#ifndef PICO_ORIC_ROMS_H
#define PICO_ORIC_ROMS_H

#include <stdint.h>

#include "card.h"
#include "config.h"
#include "oric.h"
#include "ula.h"

/* "Atmos 48K", "Oric-1 16K": the machine a ROM and a RAM make (§2.1). */
const char *roms_machine_name(rom_id_t rom, oric_ram_t ram);

/* The emulator's font (§7.6): the standard set from the ROM the job
 * loaded, whichever it is, or with none the public-domain fallback. */
void roms_charset(const card_job_t *job, const uint8_t image[ORIC_ROM_SIZE],
                  uint8_t out[ORIC_CHARSET_BYTES]);

/* The page for a machine whose ROM, `want`, the job did not load: what
 * the card has, and, when job->loaded is the other BASIC ROM, the key
 * that starts that machine instead. */
void roms_page(const card_job_t *job, rom_id_t want, oric_ram_t ram,
               const uint8_t charset[ORIC_CHARSET_BYTES], oric_frame_t *f);

#endif /* PICO_ORIC_ROMS_H */
