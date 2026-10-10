/* bus.c — the slow path behind the page table (design.md §6.1, §6.4).
 *
 * Page #03 is I/O. The VIA answers throughout the page, its registers
 * on A3-A0, unless an expansion claims an address with /I/O CONTROL
 * (§6.4, §16). So it is decoded by mask, not equality. The Microdisc
 * takes #0310-#0314 and #0318's reads (microdisc.h).
 */

#include "bus.h"

#include "cassette.h"
#include "hot.h"
#include "microdisc.h"
#include "via6522.h"
#include "vsync.h"

#define VIA_REG(a)  ((uint8_t)((a) & 15u))

/* PICO_ORIC_DECK=OFF: the control for the signal's cost (oric.c). */
#ifndef PICO_ORIC_DECK
#define PICO_ORIC_DECK 1
#endif

uint8_t ORIC_HOT1(bus_read_slow)(oric_t *m, uint16_t a) {
    if (m->page_flags[a >> 8] & PAGE_IO) {
        if (microdisc_decodes(m, a, false)) {
            uint8_t v = microdisc_read(m, a);
            m->open_bus = v;
            return v;
        }
        /* CB1 as the tape has it at this cycle (cassette.h). */
        if (PICO_ORIC_DECK && m->cpu.cycles + m->cpu.io_at >= m->cas.due)
            cassette_catch_up(m, m->cpu.cycles + m->cpu.io_at);
        /* And as the sync has it (vsync.h). */
        if (m->cpu.cycles + m->cpu.io_at >= m->vs.due) vsync_run(m, m->cpu.cycles + m->cpu.io_at);
        oric_via_catch_up(m, false);
        /* Reading T1C-L or T2C-L clears a flag, and reading ORA or ORB
         * clears CA/CB flags, so the IRQ line can drop on a read (§6.4). */
        uint8_t reg = VIA_REG(a);
        uint8_t v = via6522_read(&m->via, reg);
        /* A read of ORA, ORB, T1C-L, T2C-L or SR clears a flag or moves
         * CA2/CB2 in handshake mode; the rest change nothing, and a
         * tape's reader is IFR reads end to end (§10.4). */
        if ((1u << reg) & ((1u << VIA_ORB) | (1u << VIA_ORA) | (1u << VIA_T1CL) |
                           (1u << VIA_T2CL) | (1u << VIA_SR)))
            oric_io_changed(m);
        m->open_bus = v;
        return v;
    }

    /* Unpopulated. Open bus is the last value on the bus, not 0xFF. */
    return m->open_bus;
}

void ORIC_HOT1(bus_write_slow)(oric_t *m, uint16_t a, uint8_t v) {
    if (m->page_flags[a >> 8] & PAGE_IO) {
        if (microdisc_decodes(m, a, true)) {
            microdisc_write(m, a, v);
            return;
        }
        /* CB1 as the tape has it at this cycle (cassette.h). */
        if (PICO_ORIC_DECK && m->cpu.cycles + m->cpu.io_at >= m->cas.due)
            cassette_catch_up(m, m->cpu.cycles + m->cpu.io_at);
        /* And as the sync has it (vsync.h). */
        if (m->cpu.cycles + m->cpu.io_at >= m->vs.due) vsync_run(m, m->cpu.cycles + m->cpu.io_at);
        oric_via_catch_up(m, true);
        via6522_write(&m->via, VIA_REG(a), v);
        /* A write to ORA, ORB, the DDRs or the PCR is what the AY's bus
         * and the keyboard row see (§2.3). */
        oric_io_changed(m);
        return;
    }

    /* ROM and unpopulated space: the write is discarded. */
    (void)v;
}
