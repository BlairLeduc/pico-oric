/* bus.h — guest memory dispatch (design.md §6.1).
 *
 * Fast path is a single indexed load or store through a 256-entry page
 * table; everything else falls into bus_read_slow / bus_write_slow.
 */
#ifndef PICO_ORIC_BUS_H
#define PICO_ORIC_BUS_H

#include <stdint.h>

#include "oric.h"

uint8_t bus_read_slow(oric_t *m, uint16_t a);
void    bus_write_slow(oric_t *m, uint16_t a, uint8_t v);

/* `at` is the cycle of the current instruction the access falls on,
 * from 1, or 0 outside one (m6502_t.io_at). Only the slow path keeps it:
 * page #03 brings the VIA up to that cycle first (§5.3). */
static inline uint8_t bus_read_at(oric_t *m, uint16_t a, uint32_t at) {
    const page_t *p = &m->page[a >> 8];
    if (__builtin_expect(p->read != NULL, 1)) {
        uint8_t v = p->read[a & 0xFFu];
        m->open_bus = v;
        return v;
    }
    m->cpu.io_at = (uint8_t)at;
    return bus_read_slow(m, a);
}

static inline void bus_write_at(oric_t *m, uint16_t a, uint8_t v, uint32_t at) {
    page_t *p = &m->page[a >> 8];
    m->open_bus = v;
    if (__builtin_expect(p->write != NULL, 1)) {
        p->write[a & 0xFFu] = v;
    } else {
        m->cpu.io_at = (uint8_t)at;
        bus_write_slow(m, a, v);      /* ROM (ignored) or I/O */
    }
}

static inline uint8_t bus_read(oric_t *m, uint16_t a) {
    return bus_read_at(m, a, 0);
}

static inline void bus_write(oric_t *m, uint16_t a, uint8_t v) {
    bus_write_at(m, a, v, 0);
}

#endif /* PICO_ORIC_BUS_H */
