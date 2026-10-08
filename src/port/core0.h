/* core0.h — core 0's loop: the guest (design.md §4.3, §11.1). */
#ifndef PICO_ORIC_CORE0_H
#define PICO_ORIC_CORE0_H

#include "keymatrix.h"
#include "oric.h"

/* Run the machine a field at a time for ever: keys first, then the
 * field, then the snapshot. Until M8 brings audio, pace on time_us_64()
 * against an absolute field deadline, so that a late field does not
 * accumulate (EL §6.3). Core 1 must be up: from here on core 0 logs only
 * through the ring. */
void core0_run(oric_t *m, keymatrix_t *k) __attribute__((noreturn));

#endif /* PICO_ORIC_CORE0_H */
