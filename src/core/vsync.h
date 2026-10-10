/* vsync.h — the vertical-sync modification (design.md §2.6, §15.2 M16).
 *
 * An owner's modification wires the ULA's sync to the tape input, and so
 * to CB1 (§2.3): with it, a program can wait for the field's vertical
 * sync, by polling the CB1 flag or by its interrupt. The machine has it
 * when oric_config_t.vsync_hack is set; CB1 is then the sync and not the
 * tape, whose player no longer drives it (cassette.h). The trap does not
 * read CB1 and keeps working.
 *
 * The ULA's line counter is 0 at the first active line, where a field
 * ends (§11.1), and its sync is lines 256-259 of the counter at 50 Hz
 * (Brown's measurement; Clock Signal agrees) and 234-237 at 60 Hz (Clock
 * Signal alone; §16). The pulse on CB1 is Oricutron's: low 12 us after
 * the sync begins, for 260 us, the tape input's filter having taken the
 * line syncs out. Every one of those numbers is oric_config_t's until
 * §16 settles it.
 *
 * Two edges a field, each an event the run loop's slice ends at, as the
 * player's are (§5.3, cassette.h); an access to the VIA brings the
 * device to the access's cycle first (bus.c). The edges are placed from
 * the field's start, which the device keeps itself, a field of the
 * length oric_field_cycles gives at the time: so the frame chain is the
 * same whether the machine runs by oric_run_field or by oric_run alone,
 * as the trace does. The 60 Hz line is the frame's own frequency's, which
 * oric_run_field settles at the boundary (vsync_field).
 *
 * Not machine state: a snapshot is taken at a field boundary, with CB1
 * high between the pulse and the next, and the device is worked out
 * again from the boundary on load (vsync_restart).
 */
#ifndef PICO_ORIC_VSYNC_H
#define PICO_ORIC_VSYNC_H

#include <stdbool.h>
#include <stdint.h>

struct oric_s;

typedef struct {
    /* The cycle of the next edge; never while the modification is off. */
    uint64_t due;
    uint64_t frame_at;     /* the cycle the field in progress began     */
    bool     low;          /* the next edge is the rise                 */
    uint32_t pulses;       /* falls put on CB1, for the heartbeat       */
} vsync_t;

/* The first field's start, or a restored one's (snapshot.h): CB1 high,
 * the next fall scheduled from `frame_at`. Does nothing while the
 * modification is off. */
void vsync_restart(struct oric_s *m, uint64_t frame_at);

/* oric_run_field's, after the mode scan: the field just begun takes its
 * sync line from the frequency the scan found. */
void vsync_field(struct oric_s *m);

/* The edges due by `now` onto CB1: the run loop's, after an instruction,
 * and the bus's, at an access's cycle. */
void vsync_run(struct oric_s *m, uint64_t now);

/* The cycles from a field's start to its sync's fall on CB1, for the
 * given frequency. */
uint32_t vsync_fall_at(const struct oric_s *m, bool hz50);

#endif /* PICO_ORIC_VSYNC_H */
