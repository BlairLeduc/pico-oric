/* ay8912.c — the AY-3-8912's bus and registers (scope in ay8912.h). */

#include "ay8912.h"

#include <string.h>

/* Bits a register keeps; the rest read back as zero (General
 * Instrument AY-3-8910/8912 data manual, the register array). */
static const uint8_t reg_mask[AY_REG_COUNT] = {
    0xFF, 0x0F, 0xFF, 0x0F, 0xFF, 0x0F,   /* tone periods, fine and coarse */
    0x1F,                                 /* noise period                  */
    0xFF,                                 /* mixer and I/O enable          */
    0x1F, 0x1F, 0x1F,                     /* amplitudes                    */
    0xFF, 0xFF,                           /* envelope period               */
    0x0F,                                 /* envelope shape                */
    0xFF, 0xFF,                           /* I/O ports A and B             */
};

void ay8912_reset(ay8912_t *ay) {
    /* RESET clears every register; the pins float high. */
    memset(ay, 0, sizeof(*ay));
    ay->port_a_in = 0xFFu;
    ay->selected = true;    /* register 0, as RESET leaves the latch */
}

void ay8912_bus(ay8912_t *ay, ay_bus_t mode, uint8_t data) {
    ay->mode = (uint8_t)mode;
    ay->driving = false;
    switch (mode) {
    case AY_BUS_LATCH:
        /* The high nibble is the chip's select code, 0000 on the 8912:
         * any other value deselects it, and it ignores reads and writes
         * until an address with the right code is latched. */
        ay->selected = (data & 0xF0u) == 0;
        if (ay->selected) ay->addr = data;
        break;
    case AY_BUS_WRITE:
        if (!ay->selected) break;
        if (ay->reg[ay->addr] != (uint8_t)(data & reg_mask[ay->addr])) ay->writes++;
        ay->reg[ay->addr] = (uint8_t)(data & reg_mask[ay->addr]);
        /* M8: bring the generator up to now before the change. */
        break;
    case AY_BUS_READ:
        if (!ay->selected) break;    /* the bus stays undriven */
        ay->driving = true;
        /* An input port reads its pins, not the register. The 8912 has
         * no port B pins; the register reads back. */
        ay->bus_out = (ay->addr == AY_PORT_A) ? ay8912_port_a(ay) : ay->reg[ay->addr];
        break;
    case AY_BUS_INACTIVE:
        break;
    }
}
