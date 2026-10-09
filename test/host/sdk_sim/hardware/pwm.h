/* Host stand-in for hardware/pwm.h (sdk_sim.h). */
#ifndef PICO_ORIC_SIM_PWM_H
#define PICO_ORIC_SIM_PWM_H

#include "sdk_sim.h"

typedef struct {
    uint32_t div, top;
} pwm_config;

static inline unsigned pwm_gpio_to_slice_num(unsigned gpio) {
    return (gpio >> 1) & 7u;
}
static inline pwm_config pwm_get_default_config(void) {
    pwm_config c = { 1, 0xFFFF };
    return c;
}
static inline void pwm_config_set_clkdiv_int(pwm_config *c, uint32_t div) {
    c->div = div;
}
static inline void pwm_config_set_wrap(pwm_config *c, uint32_t top) {
    c->top = top;
}
static inline void pwm_init(unsigned slice, pwm_config *c, bool start) {
    (void)slice;
    (void)c;
    (void)start;
}
static inline void pwm_set_both_levels(unsigned slice, uint32_t a, uint32_t b) {
    pwm_hw->slice[slice].cc = a | b << 16;
}
static inline unsigned pwm_get_dreq(unsigned slice) {
    return slice;
}
static inline void pwm_set_enabled(unsigned slice, bool on) {
    (void)slice;
    (void)on;
}

#endif
