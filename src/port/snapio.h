/* snapio.h — our save states on the card, /oric/states/slotN.sav
 * (design.md §10.1, §10.6).
 *
 * pico-ace's snapio with the Oric's paths, less its .ace import: no
 * community snapshot format is imported (§10.6). Core 1 only, with the
 * guest parked (park.h), like all card work. A save writes slotN.new,
 * closes it, removes slotN.sav and renames the new file into place: a
 * rename alone is not proof of power-loss atomicity (hardware-notes.md
 * §7.1), so the recovery policy is on the load side. A load checks
 * slotN.sav whole — header, CRC, machine, ROM (snapshot.h) — and if it
 * is missing or damaged falls back to a whole slotN.new, which is what an
 * interrupted publish leaves. Nothing changes in the machine, the drives
 * or the deck until a file, and every disc it names, has passed.
 *
 * The two passes are two calls, so that a state for another machine can
 * be loaded into that machine (design.md §10.6): snapio_check names it,
 * the menu powers on as it, and snapio_load loads the file the check
 * passed.
 */
#ifndef PICO_ORIC_SNAPIO_H
#define PICO_ORIC_SNAPIO_H

#include <stdbool.h>
#include <stdint.h>

#include "oric.h"
#include "snapshot.h"

#define SNAPIO_SLOTS     4u
#define SNAPIO_STATE_DIR "/oric/states"

/* The card must be mounted (storage.h) for all of these. *us is the wall
 * time the save took. */
snap_status_t snapio_save(const oric_t *m, unsigned slot, uint32_t *us);

/* The first pass over the slot: fills *info as snapshot_check does, and
 * sets *recovered when the slot was its .new. SNAP_OK, or SNAP_OTHER_ROM,
 * SNAP_OTHER_RAM or SNAP_OTHER_MACHINE for a state that is whole and
 * whose discs are on the card, but for another machine
 * (snapshot_machine); then snapio_load may follow. The machine itself
 * is not checked: one waiting on the tape or busy on a disc command,
 * which snapshot_load refuses, is powered on first (menu.c). */
snap_status_t snapio_check(const oric_t *m, unsigned slot, snap_info_t *info, bool *recovered);

/* The second pass, over the file the last snapio_check passed, and the
 * media put back. *changed is set when it failed after all, the card
 * going or the file changing between the passes: the machine is then
 * part old, part new, and must be powered on again rather than resumed.
 * Into a machine other than the one it needs, it is refused as the check
 * was, and nothing changes. */
snap_status_t snapio_load(oric_t *m, bool *changed);
bool          snapio_exists(unsigned slot);

/* A save records the drives' discs and the deck's tape and place, and a
 * load puts them back (snapshot.h's media), the discs checked before
 * anything changes: a state whose disc is not on the card is refused,
 * SNAP_NO_DISC. What the menu should say about the last load's media,
 * the disc it needs or a tape that has gone; "" for nothing. */
const char   *snapio_said(void);
bool          snapio_delete(unsigned slot);

#endif /* PICO_ORIC_SNAPIO_H */
