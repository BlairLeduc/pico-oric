/* Host stand-in for hardware/sync.h (sdk_sim.h): masking interrupts is
 * the simulation's IRQ mask. */
#ifndef PICO_ORIC_SIM_SYNC_H
#define PICO_ORIC_SIM_SYNC_H

#include "sdk_sim.h"

static inline uint32_t save_and_disable_interrupts(void) {
    uint32_t was = g_sim.irq_masked;
    g_sim.irq_masked = true;
    return was;
}
static inline void restore_interrupts(uint32_t was) {
    if (!was) sim_unmask();
}

#endif
