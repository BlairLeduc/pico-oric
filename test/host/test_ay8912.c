/* test_ay8912.c — the AY's registers, reached through the VIA, and its
 * port A on the keyboard (design.md §2.3, §13.2).
 *
 * The chip alone first, then the machine: 6502 code doing what both
 * ROMs' register write does (#F535 in 1.0, #F590 in 1.1), and the
 * keyboard's sense on PB3. The control is the same write with CA2 and
 * CB2 the other way round, which must not reach the register.
 */

#include <string.h>

#include "bus.h"
#include "oric.h"
#include "test_util.h"

static oric_t g_m;

/* A bare 48K machine with RAM where the vectors go, running `code` from
 * #1000 until it reaches its trailing JMP to itself. */
static oric_t *run(const uint8_t *code, size_t len) {
    oric_t *m = &g_m;
    oric_config_t cfg;
    oric_config_default(&cfg);
    oric_init(m, &cfg);
    oric_map_ram(m, 0xC000, 0x4000);
    for (size_t i = 0; i < len; i++) bus_write(m, (uint16_t)(0x1000u + i), code[i]);
    m->cpu.pc = 0x1000;
    for (int i = 0; i < 10000 && m->cpu.pc != (uint16_t)(0x1000u + len - 3u); i++) oric_run(m, 1);
    return m;
}

/* The ROM's sequence: the register number to ORA, PCR latch, inactive,
 * the value to ORA, PCR write, inactive. PCR values as the ROM writes
 * them: CA2 and CB2 held at levels, CA1/CB1 on rising edges. */
#define VIA_SETUP  0xA9, 0xFF, 0x8D, 0x03, 0x03,          /* DDRA = #FF  */ \
                   0xA9, 0xDD, 0x8D, 0x0C, 0x03           /* PCR  = #DD  */
#define AY_WRITE(r, v, latch, write)                                       \
    0xA9, (r), 0x8D, 0x0F, 0x03,                                           \
    0xA9, (latch), 0x8D, 0x0C, 0x03,                                       \
    0xA9, 0xCC, 0x8D, 0x0C, 0x03,                                          \
    0xA9, (v), 0x8D, 0x0F, 0x03,                                           \
    0xA9, (write), 0x8D, 0x0C, 0x03,                                       \
    0xA9, 0xCC, 0x8D, 0x0C, 0x03
#define HALT       0x4C, 0x00, 0x00   /* patched to point at itself */

static size_t halt_at(uint8_t *code, size_t len) {
    code[len - 2] = (uint8_t)((0x1000u + len - 3u) & 0xFFu);
    code[len - 1] = (uint8_t)((0x1000u + len - 3u) >> 8);
    return len;
}

int main(void) {
    /* ---- the chip alone ------------------------------------------------ */
    {
        ay8912_t ay;
        ay8912_reset(&ay);
        ay8912_bus(&ay, AY_BUS_LATCH, 1);
        ay8912_bus(&ay, AY_BUS_WRITE, 0xFF);
        ay8912_bus(&ay, AY_BUS_INACTIVE, 0);
        CHECK(ay.reg[1] == 0x0F, "a coarse tone period keeps four bits, got %02X", ay.reg[1]);
        ay8912_bus(&ay, AY_BUS_READ, 0);
        CHECK(ay.driving && ay.bus_out == 0x0F, "a read drives the register onto the bus");
        ay8912_bus(&ay, AY_BUS_INACTIVE, 0);
        CHECK(!ay.driving, "inactive leaves the bus alone");

        /* A select code other than 0000 in the high nibble deselects
         * the chip: writes and reads are ignored until a good latch. */
        ay8912_bus(&ay, AY_BUS_LATCH, 0x12);
        ay8912_bus(&ay, AY_BUS_WRITE, 0x55);
        CHECK(ay.reg[2] == 0, "#12 is not register 2");
        CHECK(ay.reg[1] == 0x0F, "a deselected chip took a write to register 1: %02X", ay.reg[1]);
        ay8912_bus(&ay, AY_BUS_READ, 0);
        CHECK(!ay.driving, "a deselected chip drove the bus");
        ay8912_bus(&ay, AY_BUS_LATCH, 1);
        ay8912_bus(&ay, AY_BUS_WRITE, 0x03);
        CHECK(ay.reg[1] == 0x03, "a good latch selects it again");
        ay8912_bus(&ay, AY_BUS_INACTIVE, 0);

        /* Port A: the register while the mixer says output, else the pins. */
        ay8912_bus(&ay, AY_BUS_LATCH, AY_PORT_A);
        ay8912_bus(&ay, AY_BUS_WRITE, 0x3C);
        ay.port_a_in = 0xA5;
        CHECK(ay8912_port_a(&ay) == 0xA5, "port A as an input shows its pins");
        ay8912_bus(&ay, AY_BUS_READ, 0);
        CHECK(ay.bus_out == 0xA5, "reading port A as an input reads the pins");
        ay8912_bus(&ay, AY_BUS_LATCH, AY_MIXER);
        ay8912_bus(&ay, AY_BUS_WRITE, AY_MIXER_IOA_OUT);
        CHECK(ay8912_port_a(&ay) == 0x3C, "port A as an output shows the register");
    }

    /* ---- through the VIA, as the ROMs do it ---------------------------- */
    {
        uint8_t code[] = { VIA_SETUP, AY_WRITE(7, 0x40, 0xEE, 0xEC), AY_WRITE(14, 0x5A, 0xEE, 0xEC),
                           AY_WRITE(8, 0x1F, 0xEE, 0xEC), HALT };
        oric_t *m = run(code, halt_at(code, sizeof code));
        CHECK(m->ay.reg[7] == 0x40, "mixer: got %02X", m->ay.reg[7]);
        CHECK(m->ay.reg[14] == 0x5A, "port A: got %02X", m->ay.reg[14]);
        CHECK(m->ay.reg[8] == 0x1F, "channel A amplitude: got %02X", m->ay.reg[8]);
        CHECK(m->ay.mode == AY_BUS_INACTIVE, "left inactive");
    }

    /* The control: BC1 on CB2 and BDIR on CA2 would make #EC a read and
     * #CE the write. Under the ROMs' wiring #CE is a read and changes
     * nothing; if this passes, the wiring is the wrong way round. */
    {
        uint8_t code[] = { VIA_SETUP, AY_WRITE(14, 0x5A, 0xEE, 0xCE), HALT };
        oric_t *m = run(code, halt_at(code, sizeof code));
        CHECK(m->ay.reg[14] == 0, "#CE wrote the register: CA2/CB2 swapped");
    }

    /* A read: DDRA to input, PCR #CE (CA2 high = BC1, CB2 low), ORA. */
    {
        uint8_t code[] = { VIA_SETUP, AY_WRITE(9, 0x0C, 0xEE, 0xEC),
                           0xA9, 0x00, 0x8D, 0x03, 0x03,           /* DDRA = 0     */
                           0xA9, 0xCE, 0x8D, 0x0C, 0x03,           /* PCR: read    */
                           0xAD, 0x01, 0x03, 0x85, 0x80,           /* ORA -> #80   */
                           0xA9, 0xCC, 0x8D, 0x0C, 0x03,
                           0xAD, 0x01, 0x03, 0x85, 0x81,           /* idle -> #81  */
                           HALT };
        oric_t *m = run(code, halt_at(code, sizeof code));
        CHECK(m->ram[0x80] == 0x0C, "read register 9 through PA: got %02X", m->ram[0x80]);
        CHECK(m->ram[0x81] == 0xFF, "PA floats high with the AY inactive: got %02X", m->ram[0x81]);
    }

    /* ---- port A and PB3: the keyboard ---------------------------------- */
    {
        /* Row 3 on PB0-PB2 (DDRB #F7, ORB #B8 | row), column 1 enabled
         * by its zero bit in port A, PB3 read back into #80. */
        uint8_t code[] = { VIA_SETUP,
                           0xA9, 0xF7, 0x8D, 0x02, 0x03,           /* DDRB = #F7     */
                           AY_WRITE(7, 0x40, 0xEE, 0xEC), AY_WRITE(14, 0xFD, 0xEE, 0xEC),
                           0xA9, 0xBB, 0x8D, 0x00, 0x03,           /* row 3          */
                           0xAD, 0x00, 0x03, 0x29, 0x08, 0x85, 0x80,
                           0xA9, 0xBA, 0x8D, 0x00, 0x03,           /* row 2          */
                           0xAD, 0x00, 0x03, 0x29, 0x08, 0x85, 0x81,
                           HALT };
        size_t len = halt_at(code, sizeof code);

        oric_t *m = run(code, len);
        CHECK(m->ram[0x80] == 0 && m->ram[0x81] == 0, "PB3 high with no key down");

        /* The same with the key held from the start: run() rebuilds the
         * machine, so press it on a machine built the same way. */
        oric_config_t cfg;
        oric_config_default(&cfg);
        oric_init(&g_m, &cfg);
        oric_map_ram(&g_m, 0xC000, 0x4000);
        oric_key_set(&g_m, 3, 1, true);
        for (size_t i = 0; i < len; i++) bus_write(&g_m, (uint16_t)(0x1000u + i), code[i]);
        g_m.cpu.pc = 0x1000;
        for (int i = 0; i < 10000 && g_m.cpu.pc != (uint16_t)(0x1000u + len - 3u); i++) oric_run(&g_m, 1);
        CHECK(g_m.ram[0x80] == 0x08, "row 3 column 1 down: PB3 should read high");
        CHECK(g_m.ram[0x81] == 0, "row 2 has no key down");

        /* Back to row 3; then column 1 disabled, and port A an input:
         * no sense either way. */
        bus_write(&g_m, 0x0300, 0xBB);
        CHECK(g_m.via.in_b & 0x08u, "row 3 again");
        g_m.ay.reg[AY_PORT_A] = 0xFF;
        oric_io_changed(&g_m);
        CHECK(!(g_m.via.in_b & 0x08u), "a disabled column is not seen");
        g_m.ay.reg[AY_PORT_A] = 0xFD;
        g_m.ay.reg[AY_MIXER] = 0;
        oric_io_changed(&g_m);
        CHECK(!(g_m.via.in_b & 0x08u), "port A as an input enables no column");
        g_m.ay.reg[AY_MIXER] = AY_MIXER_IOA_OUT;
        oric_io_changed(&g_m);
        CHECK(g_m.via.in_b & 0x08u, "and as an output again it does");
        oric_key_set(&g_m, 3, 1, false);
        CHECK(!(g_m.via.in_b & 0x08u), "a release is seen at once");
    }

    TEST_DONE();
}
