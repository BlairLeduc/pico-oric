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
 * on the card plays that file whatever is in the deck; any other reads
 * the deck. When a CLOAD reaches the end of the tape it is rewound once,
 * so a program already passed is found; at the end a second time the
 * request is declined, and the ROM waits for a signal as the real
 * machine would, until the reset button.
 *
 * A save is appended through <file>.new and a rename (EL §8.6), the
 * file whole, header and data; a load that finds only the .new, a save
 * cut off between the two, takes it. With nothing in the deck and no
 * name, CSAVE"" is declined: the ROM writes to a recorder that is not
 * there.
 *
 * Fast tape off is M13's, the signal: until then the trap serves CLOAD
 * and CSAVE either way, and the Setup row is kept for it.
 */
#ifndef PICO_ORIC_TAPEIO_H
#define PICO_ORIC_TAPEIO_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"
#include "oric.h"

#define TAPEIO_DIR "/oric/tapes"

/* Serve the request the CPU is stalled on, or decline it: mounts the
 * card, does the job and unmounts. The time taken is in *us. */
void tapeio_serve(oric_t *m, uint32_t *us);

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
    uint32_t last_us, max_us;
    uint32_t bytes;            /* the last load's or save's */
} tapeio_stats_t;

extern volatile tapeio_stats_t g_tape_stats;

#endif /* PICO_ORIC_TAPEIO_H */
