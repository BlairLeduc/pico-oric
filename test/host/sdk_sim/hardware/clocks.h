/* Host stand-in for hardware/clocks.h (sdk_sim.h): clk_sys at 150 MHz,
 * the firmware's clock (hardware-notes.md §3). */
#ifndef PICO_ORIC_SIM_CLOCKS_H
#define PICO_ORIC_SIM_CLOCKS_H

#include "sdk_sim.h"

enum { clk_sys = 5 };

static inline uint32_t clock_get_hz(int clk) {
    (void)clk;
    return 150000000u;
}

#endif
