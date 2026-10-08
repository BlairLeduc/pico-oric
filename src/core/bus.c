/* bus.c — the slow path behind the page table (design.md §6.1, §6.4).
 *
 * Page #03 is I/O. The VIA is believed to answer throughout the page,
 * its registers on A3-A0, unless an expansion claims an address with
 * /I/O CONTROL (§6.4, §16: to be read off the schematic). So it is
 * decoded by mask, not equality. M14: the Microdisc takes #0310-#031F.
 */

#include "bus.h"

#include "hot.h"
#include "via6522.h"

#define VIA_REG(a)  ((uint8_t)((a) & 15u))

uint8_t ORIC_HOT1(bus_read_slow)(oric_t *m, uint16_t a) {
    if (m->page_flags[a >> 8] & PAGE_IO) {
        oric_via_catch_up(m);
        /* Reading T1C-L or T2C-L clears a flag, and reading ORA or ORB
         * clears CA/CB flags, so the IRQ line can drop on a read (§6.4). */
        uint8_t v = via6522_read(&m->via, VIA_REG(a));
        oric_io_changed(m);   /* a read can move CA2/CB2 in handshake mode */
        m->open_bus = v;
        return v;
    }

    /* Unpopulated. Open bus is the last value on the bus, not 0xFF. */
    return m->open_bus;
}

void ORIC_HOT1(bus_write_slow)(oric_t *m, uint16_t a, uint8_t v) {
    if (m->page_flags[a >> 8] & PAGE_IO) {
        oric_via_catch_up(m);
        via6522_write(&m->via, VIA_REG(a), v);
        /* A write to ORA, ORB, the DDRs or the PCR is what the AY's bus
         * and the keyboard row see (§2.3). */
        oric_io_changed(m);
        return;
    }

    /* ROM and unpopulated space: the write is discarded. */
    (void)v;
}
