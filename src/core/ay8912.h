/* ay8912.h — the AY-3-8912's bus and registers (design.md §2.3, §8).
 *
 * M3 builds the part as the VIA sees it: sixteen registers behind an
 * address latch, reached through BDIR and BC1 on a shared data bus, and
 * I/O port A, which drives the keyboard's columns. No sound until M8.
 *
 * The chip knows nothing of the Oric. The machine puts the bus mode and
 * the data lines on it with ay8912_bus(), and reads what the chip drives
 * back from `bus_out` while the mode is a read. The datasheet's modes,
 * BDIR and BC1 (BC2 tied high, as on the Oric):
 *
 *   BDIR BC1
 *    0    0   inactive
 *    0    1   read from the latched register
 *    1    0   write to the latched register
 *    1    1   latch an address
 *
 * The bus is level-sensitive: the mode acts for as long as it is held,
 * so a held write follows the data lines, as the part does.
 */
#ifndef PICO_ORIC_AY8912_H
#define PICO_ORIC_AY8912_H

#include <stdbool.h>
#include <stdint.h>

#define AY_REG_COUNT  16u

/* Registers with a meaning outside the sound generator. */
#define AY_MIXER      7u      /* bit 6: port A is an output        */
#define AY_PORT_A    14u
#define AY_PORT_B    15u      /* the 8912 has no port B pins        */

#define AY_MIXER_IOA_OUT  0x40u

typedef enum {
    AY_BUS_INACTIVE = 0,
    AY_BUS_READ     = 1,
    AY_BUS_WRITE    = 2,
    AY_BUS_LATCH    = 3,
} ay_bus_t;

typedef struct {
    uint8_t reg[AY_REG_COUNT];
    uint8_t addr;           /* the latched register number            */
    uint8_t mode;           /* ay_bus_t, as last put on the pins      */
    bool    driving;        /* in a read: the chip drives the bus     */
    uint8_t bus_out;        /* what it drives while `driving`         */
    uint8_t port_a_in;      /* levels outside on port A's pins        */
    uint32_t writes;        /* register writes, for the heartbeat (§14) */
} ay8912_t;

void ay8912_reset(ay8912_t *ay);

/* Put a bus mode and data on the pins. A read leaves the register's
 * value in bus_out with `driving` set; any other mode clears it. */
void ay8912_bus(ay8912_t *ay, ay_bus_t mode, uint8_t data);

/* Port A's pins as the outside world sees them: the register while the
 * mixer makes the port an output, otherwise whatever pulls them. */
static inline uint8_t ay8912_port_a(const ay8912_t *ay) {
    return (ay->reg[AY_MIXER] & AY_MIXER_IOA_OUT) ? ay->reg[AY_PORT_A] : ay->port_a_in;
}

#endif /* PICO_ORIC_AY8912_H */
