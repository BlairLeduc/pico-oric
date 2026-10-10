/* cassette.h — tape phase 2: the signal (design.md §10.4; EL §8.3).
 *
 * The player puts a .tap image on CB1 as the square wave the ROM's own
 * CSAVE writes for it, clocked in guest cycles; the recorder reads PB7
 * back into a .tap. Whatever reads CB1, the ROM's CLOAD or a loader of
 * a program's own, sees what it would see from a recorder.
 *
 * Both ROMs write with the same code at different addresses (cassette.c
 * names the instructions): VIA T1 free-runs with its output on PB7, and
 * every half-cycle is one T1 period, the latch + 2, of 210 cycles (#D0)
 * or 418 (#1A0), in the order the writer sets the latch. A byte is one
 * period, then thirteen bits: a 0, eight data bits from bit 0, odd
 * parity and three 1s. Fast, a bit is 210 then 210 for a 1 or 418 for a
 * 0; slow (,S), sixteen 210s for a 1 or eight 418s for a 0. A file is
 * 259 #16s, #24, the header, the name and its zero, a few periods while
 * the ROM is busy (cassette_gap), then the data. test_cassette holds the
 * player to a recording of each ROM's CSAVE, edge for edge, at both
 * speeds.
 *
 * The player plays the image's bytes as they are, so a loader of a
 * program's own reads them as its author wrote them, and adds only what
 * a .tap leaves out: the rest of the ROM's leader before each header,
 * and the ROM's gap between a header and its data. The speed is the
 * ROM's own setting when the motor starts (tape.h's slow), fast for
 * any other ROM: a .tap does not say.
 *
 * The motor relay on PB6 is the deck's cue (§2.3): the player runs while
 * the relay is closed, and holds its place while it is open. PLAY by
 * hand runs it regardless, for a loader that never closes the relay.
 * Nothing is clocked while the deck is idle: while it plays or records,
 * the run loop brings it up to date once an instruction (oric.c), and
 * an access to the VIA brings it to the access's cycle first (bus.c),
 * so the CB1 flag a reader polls is set on the edge's cycle.
 *
 * The recorder is the other half: armed, it decodes PB7 while the relay
 * is closed, as the ROM's own reader would, into files appended to its
 * buffer, each as tap_encode_header writes one and its data. A file is
 * kept only once it is whole: one cut short by the relay, a reset or a
 * full buffer is taken back out, and counted.
 *
 * The image and the recorder's buffer are the port's, and must outlive
 * the insertion (design.md §3.3: one 64 KiB buffer, a file at a time).
 */
#ifndef PICO_ORIC_CASSETTE_H
#define PICO_ORIC_CASSETTE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tap.h"

struct oric_s;

/* The writer's two half-cycles, T1's latch + 2 (cassette.c). */
#define CAS_SHORT        210u
#define CAS_LONG         418u
/* The #16s both ROMs write before #24 (#E75A in 1.1, #E6BA in 1.0). */
#define CAS_LEADER       259u
/* Idle half-cycles the player leaves after a file's data before what
 * follows: its own choice, a tenth of a second, not a count off the
 * ROM, which stops the relay between files. */
#define CAS_FILE_GAP     476u

typedef struct {
    bool     armed;        /* the deck is recording: the relay starts it   */
    bool     on;           /* armed and the relay closed                   */
    bool     level;        /* PB7 as the last edge left it                 */
    uint64_t last;         /* the cycle of that edge, 0 before the first   */
    uint64_t seen;         /* the cycle PB7 was last looked at             */

    /* The bit decoder: seeking a start bit, or in a byte. */
    uint8_t  state;
    bool     slow;         /* this byte's start bit was eight long halves  */
    uint8_t  run;          /* halves of the current bit so far             */
    bool     run_long;     /* and whether they are long ones               */
    uint8_t  bit;          /* bits of the byte after the start bit         */
    uint16_t shift;        /* those bits, from bit 0                       */

    /* The file decoder. */
    uint8_t  fstate;
    uint32_t at;           /* the file's start in the buffer               */
    uint8_t  raw[TAP_HEADER_LEN];
    uint8_t  name[ORIC_TAP_NAME_MAX];
    uint8_t  name_len;
    uint32_t got, want;    /* header bytes, then data bytes                */

    uint8_t *buf;          /* where files go, len of cap                   */
    uint32_t len, cap;
    uint32_t mark;         /* len when last written out                    */
    uint32_t files;        /* kept                                         */
    uint32_t errors;       /* bytes that did not frame, files cut short    */
} cassette_rec_t;

typedef struct {
    /* While the deck plays or records: the run loop's one test. */
    bool     live;

    const uint8_t *img;    /* a .tap image, len bytes                      */
    uint32_t len;
    bool     loaded;
    bool     motor;        /* the relay, PB6 driven high                   */
    bool     by_hand;      /* PLAY pressed                                 */
    bool     playing;      /* running: motor or by hand, and not ended     */
    bool     ended;        /* played to the end since the last rewind      */
    bool     slow;         /* the speed taken when it started              */
    bool     level;        /* CB1 as the player drives it                  */
    uint64_t next;         /* the cycle of the next change, while playing  */
    uint32_t left;         /* next - now, while stopped                    */

    /* The walk: a part of the image, its bytes from pos or its idle
     * halves, and the byte in hand. */
    uint8_t  part;
    uint32_t pos, part_left;
    uint32_t head_at, data_at, data_n;
    uint16_t gap;          /* idle halves after this header                */
    uint8_t  byte;
    uint8_t  bit;          /* 0 for the byte's first period, then 1-13     */
    uint8_t  half;         /* within the bit                               */
    uint32_t files;        /* headers begun since the last rewind          */
    uint32_t edges;        /* changes played, for the heartbeat            */

    cassette_rec_t rec;
} cassette_t;

/* ---- the deck in the machine --------------------------------------------- */

/* Put an image in, rewound and stopped. NULL ejects. */
void oric_cassette_insert(struct oric_s *m, const uint8_t *img, uint32_t len);

/* Rewind to the start, stopped. */
void oric_cassette_rewind(struct oric_s *m);

/* PLAY by hand, or STOP: the relay no longer matters. Starting at the
 * end of the tape does nothing: rewind first, as on a deck. */
void oric_cassette_play(struct oric_s *m, bool on);

/* Arm the recorder into buf, or disarm it (NULL), which ends a file in
 * progress as cut short and keeps the whole ones until
 * oric_cassette_saved. A buffer other than the last starts empty.
 * Recording stops the player. */
void oric_cassette_record(struct oric_s *m, uint8_t *buf, uint32_t cap);

/* Playing or recording now: the guest may run unpaced (§11.2). */
bool oric_cassette_running(const struct oric_s *m);

/* The whole files recorded and not yet written out, [*from, *to) of the
 * recorder's buffer; false when there are none or the relay is closed. */
bool oric_cassette_unsaved(const struct oric_s *m, uint32_t *from, uint32_t *to);

/* They are on the card, or are dropped: the buffer starts again. */
void oric_cassette_saved(struct oric_s *m);

/* ---- oric.c's and bus.c's -------------------------------------------------- */

/* Bring the deck up to `now`: the player's edges onto CB1, PB7's onto
 * the recorder. Called while `live`. */
void cassette_run(struct oric_s *m, uint64_t now);

/* The player alone up to `now`, an access's cycle, before the VIA is
 * read or written there (bus.c). */
void cassette_catch_up(struct oric_s *m, uint64_t now);

/* The relay changed (oric.c's wire). */
void cassette_motor(struct oric_s *m, bool on);

/* RESET, a power-on or a snapshot load: stop, keeping the place, and
 * drop a recording in progress. */
void cassette_stop_all(struct oric_s *m);

/* ---- the walk, on its own -------------------------------------------------- */

/* The half-cycles of the image in order, for tests, at the given speed
 * and for the ROM in m's socket: each call gives the next one's length
 * in cycles; false at the end of the image. Rewinds first when
 * `from_start`. The walk is the player's own. */
bool cassette_walk(struct oric_s *m, bool from_start, bool slow, uint32_t *t);

/* The idle half-cycles the ROM in m's socket leaves between a header
 * whose name is name_len bytes and its data (cassette.c). */
uint16_t cassette_gap(const struct oric_s *m, uint32_t name_len);

#endif /* PICO_ORIC_CASSETTE_H */
