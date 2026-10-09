/* Host stand-in for hardware/irq.h (sdk_sim.h). */
#ifndef PICO_ORIC_SIM_IRQ_H
#define PICO_ORIC_SIM_IRQ_H

#include "sdk_sim.h"

#define DMA_IRQ_0 10

typedef void (*irq_handler_t)(void);
extern irq_handler_t g_sim_handler;
extern bool g_sim_irq_enabled;

static inline void irq_set_exclusive_handler(unsigned num, irq_handler_t h) {
    (void)num;
    g_sim_handler = h;
}
static inline void irq_set_priority(unsigned num, uint8_t p) {
    (void)num;
    (void)p;
}
static inline void irq_set_enabled(unsigned num, bool on) {
    (void)num;
    g_sim_irq_enabled = on;
}

#endif
