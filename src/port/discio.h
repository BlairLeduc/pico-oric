/* discio.h — Microdisc images in /oric/discs/ on the card (design.md
 * §10.1, §10.5).
 *
 * The WD1793 in the core knows each drive's geometry and holds one raw
 * track; when a command needs another, or has written to the one it
 * holds, it posts a request (wd1793.h) and waits, busy, as a drive does
 * while its head settles. This serves it from the image file: the
 * written track put back, then the wanted one read in, 6,400 bytes at
 * their place in the file (mfmdisk.h). Core 1 only, with core 0 parked
 * (park.h): card latency can outlast both the field and the audio
 * deadline (hardware-notes.md §7.1).
 *
 * The drives are the port's, not the machine's: which file is in each,
 * its geometry and whether it is protected stay across a power-on, the
 * Machine page's restart and a snapshot's load, and discio_attach puts
 * them back into a machine that has just been built. Images are
 * Oricutron's MFM_DISK; one that is not, or is cut short, is refused by
 * name. A file with the read-only attribute is a write-protected disc.
 */
#ifndef PICO_ORIC_DISCIO_H
#define PICO_ORIC_DISCIO_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"
#include "mfmdisk.h"
#include "oric.h"

#define DISCIO_DIR "/oric/discs"

/* The request oric_disc_request names, put and got: mounts the card,
 * does the work and unmounts. False if the card failed it, in which case
 * the track read is unformatted and the track written is lost
 * (wd_served). The time taken is in *us. */
bool discio_serve(oric_t *m, uint32_t *us);

typedef struct {
    char     path[ORIC_PATH_MAX];
    uint32_t size;
    bool     protect;
} discio_entry_t;

/* Up to max images in DISCIO_DIR, in directory order. The card must be
 * mounted (storage.h). */
unsigned discio_list(discio_entry_t *out, unsigned max);

/* Put an image in a drive, 0 to 3; NULL or "" empties it. The
 * card must be mounted, and core 0 parked or not yet running. m is the
 * machine to tell, or NULL at boot, before it exists. A track the drive
 * has written and not yet put back goes first. Returns NULL, or why the
 * disc did not go in, and the drive is then empty. */
const char *discio_insert(oric_t *m, unsigned drive, const char *path);

/* Would this image go in a drive? As discio_insert judges it, changing
 * nothing: a save state's discs are checked before the load (snapio.h).
 * g and protect, either may be NULL, are the image's. NULL, or why not. */
const char *discio_probe(const char *path, mfm_geom_t *g, bool *protect);
const char *discio_inserted(unsigned drive);   /* "" when none          */
bool        discio_protected(unsigned drive);

/* The drives into a machine just powered on (core0_power_on): their
 * types, then their discs. */
void discio_attach(oric_t *m);

/* Each drive's type, its sides and tracks (wd1793.h), from the settings
 * at boot and the Machine page; a power-on puts it in. Two sides of 82
 * tracks until set. */
void       discio_set_type(unsigned drive, mfm_geom_t type);
mfm_geom_t discio_type(unsigned drive);

/* A new disc for a drive, name.dsk in DISCIO_DIR, of the drive's type and
 * unformatted (mfm_blank), for the DOS to format; then put in that drive.
 * name is the menu's, up to ORIC_MEDIA_NAME_MAX characters; a disc of
 * that name already there is refused. Written through name.dsk.new and a
 * rename, polling the keyboard as it goes, since the largest takes
 * seconds (hardware-notes.md §6.1, §7.1). The card must be mounted, and
 * core 0 parked. NULL, or why not. discio_next_name gives the menu its
 * first offer, DISCnn, the first number free; "" if none is. */
const char *discio_new(oric_t *m, unsigned drive, const char *name);
void        discio_next_name(char out[ORIC_MEDIA_NAME_MAX + 1]);

/* Each drive's reads and writes since boot, for the heartbeat. */
uint32_t discio_tracks(bool written);

#endif /* PICO_ORIC_DISCIO_H */
