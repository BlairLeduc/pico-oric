/* tapeio.h — .tap files in /oric/tapes/ on the card, serving the tape
 * trap (design.md §10.1, §10.3).
 *
 * The core stalls the CPU on one of the ROM's tape steps and leaves the
 * request in oric_t.tape (tape.h); this serves it. Core 1 only, with
 * core 0 parked and the machine core 1's (park.h): card latency can
 * outlast both the field and the audio deadline (hardware-notes.md §7.1).
 *
 * The deck holds one tape and plays it from where it stands, a file at a
 * time, as a recorder would: a header found and not loaded (CLOAD asked
 * for another name) has its data passed over, by the header's length, on
 * the way to the next. With the deck empty, CLOAD and CSAVE use the file
 * the guest names: CLOAD"SQ" plays /oric/tapes/SQ.tap from its start, or
 * failing that the first tape whose first header is SQ's, and CSAVE"SQ"
 * appends to SQ.tap, making it if need be. A CLOAD whose name is a file
 * on the card, as it is or with .tap after it (CLOAD"GAME.TA1", a title's
 * next part), plays that file whatever is in the deck; any other reads
 * the deck. When a CLOAD reaches the end of the tape it is rewound once,
 * so a program already passed is found. A file one byte short at the
 * tape's end loads, the last address unchanged (oric_tape_load_keep). At
 * the end a second time, or with no tape, no card or a file cut short
 * by more, the emulator presses the
 * reset button and the status line says why: the ROM's warm start,
 * program kept, where the real machine would wait for a signal
 * (oric_tape_give_up).
 *
 * A save is appended through <file>.new and a rename (EL §8.6), the
 * file whole, header and data; a load that finds only the .new, a save
 * cut off between the two, takes it. With nothing in the deck and no
 * name, CSAVE"" is declined: the ROM writes to a recorder that is not
 * there.
 *
 * Fast tape off is the signal (design.md §10.4; cassette.h): the trap is
 * only the cue. A find chooses the tape as above, puts it in the
 * cassette from the deck's place, whole or as a window of its whole
 * files when it is longer than ORIC_TAPE_IMAGE_MAX, and is declined, so
 * that the ROM reads the signal; the next window is put in when the
 * ROM's next find finds the cassette at its end. A header write arms
 * the recorder for the file CSAVE would append to, and is declined; the
 * file recorded, whole, is appended at the next park, as a trapped save
 * would be. A ROM left reading a tape that has ended is the end of the
 * tape as above: rewound once, then the reset button.
 */
#ifndef PICO_ORIC_TAPEIO_H
#define PICO_ORIC_TAPEIO_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"
#include "oric.h"
#include "snapshot.h"

#define TAPEIO_DIR "/oric/tapes"

/* The machine whose cassette holds the signal's image (park_init). */
void tapeio_attach(oric_t *m);

/* Serve the request the CPU is stalled on, or decline it, and write out
 * a recording: mounts the card, does the job and unmounts. The time
 * taken is in *us. */
void tapeio_serve(oric_t *m, uint32_t *us);

/* Whether core 0 should park for tapeio_serve with no request: a
 * recording to write out, unless the card was missing for it and has
 * not changed since; or the ROM reading a tape that has ended. */
bool tapeio_wanted(const oric_t *m);
void tapeio_card_changed(void);

/* The deck, with the card mounted. Insert puts a tape in at its start;
 * NULL or "" empties the deck. NULL, or why not. */
const char *tapeio_insert(const char *path);
const char *tapeio_inserted(void);     /* "" when the deck is empty   */
/* A new empty tape, TAPEnn.tap in TAPEIO_DIR, in the deck. NULL, or why
 * not. */
const char *tapeio_new(void);
bool        tapeio_chosen(void);       /* put in by the menu or boot_tape, not found by name */
void        tapeio_rewind(void);
uint32_t    tapeio_position(void);     /* files passed since the start */

/* The deck in a save state (snapshot.h, design.md §10.6): its file and
 * its place into md, and back from a loaded state's, with the card
 * mounted. A restore puts the file in the deck where the state left it,
 * or at its start if the file is now shorter than that place; "" empties
 * the deck. NULL, or why the file could not be put in, which leaves the
 * deck empty. */
void        tapeio_media(snap_media_t *md);
const char *tapeio_restore(const snap_media_t *md);

/* PLAY by hand with fast tape off, for a loader that never closes the
 * relay, or STOP. NULL, or why not. The card must be mounted. */
const char *tapeio_play(bool on);
bool        tapeio_playing(void);

/* Fast tape changed: on, the cassette's image goes. */
void        tapeio_mode(void);

/* The last thing a load or save did that the user should hear about,
 * for the menu's status row; "" for nothing. Cleared by reading. */
const char *tapeio_said(void);

typedef struct {
    char     path[ORIC_PATH_MAX];
    char     name[17];     /* the first header's, 16 shown, "" if none     */
    bool     code;         /* that header's type: machine code, not BASIC  */
    uint32_t size;
} tapeio_entry_t;

/* Up to max .tap files in TAPEIO_DIR, in directory order. The card must
 * be mounted. */
unsigned tapeio_list(tapeio_entry_t *out, unsigned max);

/* Counters for the heartbeat. */
typedef struct {
    uint32_t finds, loads, saves, declined, errors;
    uint32_t played, recorded;  /* the signal's: tapes put in the cassette, files recorded */
    uint32_t last_us, max_us;
    uint32_t bytes;            /* the last load's or save's */
} tapeio_stats_t;

extern volatile tapeio_stats_t g_tape_stats;

#endif /* PICO_ORIC_TAPEIO_H */
