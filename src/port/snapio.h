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
 * interrupted publish leaves. Nothing changes in the machine until a file
 * has passed.
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
 * time the call took. A load fills *info as snapshot_check does, for
 * naming the machine a refused state needs, and sets *changed when a
 * file passed its check but the second pass then failed, the card going
 * or the file changing between them: the machine is then part old, part
 * new, and must be powered on again rather than resumed. */
snap_status_t snapio_save(const oric_t *m, unsigned slot, uint32_t *us);
snap_status_t snapio_load(oric_t *m, unsigned slot, snap_info_t *info, bool *recovered,
                          bool *changed, uint32_t *us);
bool          snapio_exists(unsigned slot);
bool          snapio_delete(unsigned slot);

#endif /* PICO_ORIC_SNAPIO_H */
