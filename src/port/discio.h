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

/* Put an image in a drive, A to D as 0 to 3; NULL or "" empties it. The
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

/* The drives into a machine just powered on (core0_power_on). */
void discio_attach(oric_t *m);

/* Each drive's reads and writes since boot, for the heartbeat. */
uint32_t discio_tracks(bool written);

#endif /* PICO_ORIC_DISCIO_H */
