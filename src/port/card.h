/* card.h — the SD card's jobs and its slot (design.md §4.5, §10.1).
 *
 * Core 1 only. A job mounts the card, does its work and unmounts
 * (storage.h), so a card changed between two jobs is simply a new card.
 * A job runs only with core 0 waiting at boot or parked (design.md §4.5):
 * card latency can outlast both the field and the audio deadline
 * (hardware-notes.md §7.1).
 *
 * The slot's card detect is polled from core 1's loop, and a change is
 * logged and counted. Before the guest starts, while the missing-ROM page
 * is up, a change runs the ROM job again (core1.c); once it runs, card
 * work comes from a park (park.h), and a change is only logged.
 */
#ifndef PICO_ORIC_CARD_H
#define PICO_ORIC_CARD_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"
#include "oric.h"
#include "romset.h"
#include "settings.h"
#include "sha1.h"

typedef enum {
    CARD_NONE,          /* card detect says the slot is empty      */
    CARD_UNUSABLE,      /* a card, but no FAT volume FatFs mounts  */
    CARD_MOUNTED,       /* a card, mounted for the last job        */
} card_state_t;

/* How the card has a known image (design.md §10.2). */
typedef enum {
    ROMFILE_ABSENT,     /* no file is it                                  */
    ROMFILE_KNOWN,      /* a file whose SHA-1 is the image's              */
    ROMFILE_NAMED,      /* only a file with its name and size, other bytes */
} romfile_t;

typedef struct {
    card_state_t state;
    int          fresult;     /* FatFs's, from the mount; 0 if mounted   */
    uint32_t     mount_us;    /* sd_init and f_mount                     */
    bool         dir;         /* ORIC_ROM_DIR is there                   */
    unsigned     files;       /* files in it, less macOS's "._" ones      */
    unsigned     unknown;     /* of those, the ones no SHA-1 recognises   */
    romfile_t    rom[ROM_IMAGE_COUNT];    /* each known image              */
    char         path[ROM_IMAGE_COUNT][ORIC_PATH_MAX];  /* its file, if any */
    uint8_t      digest[ROM_IMAGE_COUNT][SHA1_DIGEST_LEN];  /* that file's */
    uint32_t     read16k_us;  /* the longest 16 KiB file: open, read, hash */
    rom_id_t     loaded;      /* the BASIC ROM in the image; ROM_UNKNOWN  */
    bool         loaded_known;  /* its SHA-1, read again, is the image's  */
    uint32_t     load_us;     /* reading it into the image                */
    uint32_t     settings_us; /* reading and parsing the settings file    */
} card_job_t;

/* The other machine's ROM: 1.1 for 1.0 and 1.0 for 1.1 (§10.2). */
rom_id_t card_other_basic(rom_id_t id);

/* The ROMs on the card (design.md §10.2, §15.2 M6, M7): list
 * ORIC_ROM_DIR, read each file a sector at a time, hash it, name what it
 * is, and log a line for each. A file is an image if its SHA-1 says so,
 * whatever it is called; failing that, a file with the image's name and
 * size stands for it, loaded and marked unrecognised (§10.2). Then, if
 * `image` is not NULL, load `want` into it, or if the card has no
 * `want`, the other BASIC ROM, so the missing-ROM page can offer the
 * other machine (§10.2). job->loaded says which, if either. Without a
 * usable card the job says so. */
void card_roms(card_job_t *job, rom_id_t want, uint8_t image[ORIC_ROM_SIZE]);

/* card_roms with the card already mounted by the caller, as the menu has
 * it: the job's mount fields are left as they are. */
void card_roms_mounted(card_job_t *job, rom_id_t want, uint8_t image[ORIC_ROM_SIZE]);

/* The boot's job (design.md §10.7, §10.2): the settings file into *s, or
 * the defaults without a card or a file; then the machine they and the
 * build name into *cfg (boot_machine, handoff.h); then card_roms for its
 * ROM, in one mount. */
void card_boot(settings_t *s, card_job_t *job, oric_config_t *cfg,
               uint8_t image[ORIC_ROM_SIZE]);

/* The UART's hold (park.h): the settings file read again into *s, and
 * the ROMs listed and hashed, loading none; everything logged. */
void card_check(settings_t *s, card_job_t *job);

/* The slot, debounced: true when card detect has settled at a new level
 * since the last call. Polled from core 1's loop. */
bool card_poll(void);
bool card_present(void);       /* the settled level                  */
uint32_t card_changes(void);   /* settled changes since boot         */

const char *card_state_str(card_state_t st);

#endif /* PICO_ORIC_CARD_H */
