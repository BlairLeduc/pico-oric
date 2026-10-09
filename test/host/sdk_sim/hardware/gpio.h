/* Host stand-in for hardware/gpio.h (sdk_sim.h). */
#ifndef PICO_ORIC_SIM_GPIO_H
#define PICO_ORIC_SIM_GPIO_H

#include "sdk_sim.h"

#define GPIO_FUNC_PWM 4

static inline void gpio_set_function(unsigned gpio, int fn) {
    (void)gpio;
    (void)fn;
}

#endif
