/* card.h — the SD card's jobs and its slot (design.md §4.5, §10.1).
 *
 * Core 1 only. A job mounts the card, does its work and unmounts
 * (storage.h), so a card changed between two jobs is simply a new card.
 * A job runs only with core 0 waiting at boot or parked (design.md §4.5):
 * card latency can outlast both the field and the audio deadline
 * (hardware-notes.md §7.1).
 *
 * The slot's card detect is polled from core 1's loop, and a change is
 * logged and counted. M6 has no guest, so core 1 lists the ROMs again
 * when a card goes in; once the guest runs, card work comes from a
 * park.
 */
#ifndef PICO_ORIC_CARD_H
#define PICO_ORIC_CARD_H

#include <stdbool.h>
#include <stdint.h>

#include "romset.h"

typedef enum {
    CARD_NONE,          /* card detect says the slot is empty      */
    CARD_UNUSABLE,      /* a card, but no FAT volume FatFs mounts  */
    CARD_MOUNTED,       /* a card, mounted for the last job        */
} card_state_t;

typedef struct {
    card_state_t state;
    int          fresult;     /* FatFs's, from the mount; 0 if mounted   */
    uint32_t     mount_us;    /* sd_init and f_mount                     */
    bool         dir;         /* ORIC_ROM_DIR is there                   */
    unsigned     files;       /* files in it, less macOS's "._" ones      */
    unsigned     unknown;     /* of those, the ones no SHA-1 recognises   */
    bool         found[ROM_IMAGE_COUNT];  /* each known image, by hash   */
    uint32_t     read16k_us;  /* the longest 16 KiB file: open, read, hash */
} card_job_t;

/* The ROMs on the card (design.md §10.2, §15.2 M6): list ORIC_ROM_DIR,
 * read each file a sector at a time, hash it, name what it is, and log a
 * line for each. Without a usable card the job says so. */
void card_roms(card_job_t *job);

/* The slot, debounced: true when card detect has settled at a new level
 * since the last call. Polled from core 1's loop. */
bool card_poll(void);
bool card_present(void);       /* the settled level                  */
uint32_t card_changes(void);   /* settled changes since boot         */

const char *card_state_str(card_state_t st);

#endif /* PICO_ORIC_CARD_H */
