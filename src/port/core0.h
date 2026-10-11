/* core0.h — core 0's loop: the guest (design.md §4.3, §11.1). */
#ifndef PICO_ORIC_CORE0_H
#define PICO_ORIC_CORE0_H

#include "keymatrix.h"
#include "oric.h"

/* Run the machine a field at a time for ever: keys first, then the
 * field, then the snapshot, then the field's samples into the audio
 * queue, which paces it (EL §6.3). With PICO_ORIC_AUDIO=OFF, pace on
 * time_us_64() against an absolute field deadline instead, so that a
 * late field does not accumulate. Core 1 must be up: from here on core 0
 * logs only through the ring. */
void core0_run(oric_t *m, keymatrix_t *k) __attribute__((noreturn));

/* Power the machine on as cfg with the ROM in `image`, at the audio's
 * real rate: main()'s first power-on, the Machine page's restart, and a
 * state's for another machine, which core 1 does with the guest parked,
 * as it does the load that follows (menu.c). */
void core0_power_on(oric_t *m, const oric_config_t *cfg, const uint8_t image[ORIC_ROM_SIZE]);

#endif /* PICO_ORIC_CORE0_H */
