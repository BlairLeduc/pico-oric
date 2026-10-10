/* microdisc.c — the Microdisc on the expansion port (microdisc.h). */

#include "microdisc.h"

#include "m6502.h"

/* The cycle now: inside an instruction, its access's (bus.h). */
static uint64_t now_of(const oric_t *m) {
    return m->cpu.cycles + m->cpu.io_at;
}

void microdisc_irq(oric_t *m) {
    m6502_set_irq(&m->cpu, M6502_IRQ_DISC, m->fdc.intrq && (m->md_latch & MD_IRQEN));
}

static void map_pages(oric_t *m, unsigned first, unsigned last, uint8_t *base, uint8_t flags) {
    for (unsigned p = first; p <= last; p++) {
        uint8_t *mem = base ? base + (p - first) * ORIC_PAGE_SIZE : NULL;
        m->page[p].read = mem;
        m->page[p].write = (flags || !mem) ? NULL : mem;
        m->page_flags[p] = mem ? flags : PAGE_OPEN;
    }
}

void microdisc_map(oric_t *m) {
    const unsigned c0 = ORIC_ROM_BASE / ORIC_PAGE_SIZE, e0 = ORIC_EPROM_BASE / ORIC_PAGE_SIZE;
    uint8_t latch = m->md_latch;
    if (!m->cfg.microdisc || (latch & MD_BASIC)) {
        /* The socket, or the open bus until a ROM is in it (§10.2). */
        map_pages(m, c0, 0xFFu, m->rom_in ? m->rom : NULL, PAGE_ROM);
        return;
    }
    /* /ROMDIS: the overlay RAM, under the EPROM's 8 KiB while it is in
     * (§16, settled from the EPROM, MAME and Oricutron). */
    map_pages(m, c0, e0 - 1u, &m->ram[ORIC_ROM_BASE], 0);
    if (!(latch & MD_EPROM_OFF) && m->eprom_in)
        map_pages(m, e0, 0xFFu, m->eprom, PAGE_EPROM);
    else
        map_pages(m, e0, 0xFFu, &m->ram[ORIC_EPROM_BASE], 0);
}

void microdisc_reset(oric_t *m) {
    m->md_latch = 0;
    wd_reset(&m->fdc);
    wd_select(&m->fdc, 0, 0, m->cpu.cycles);
    microdisc_map(m);
    microdisc_irq(m);
}

uint8_t microdisc_read(oric_t *m, uint16_t a) {
    uint64_t now = now_of(m);
    uint8_t lo = (uint8_t)a, v;
    if (lo < 0x14u) {
        v = wd_read(&m->fdc, lo & 3u, now);
    } else {
        wd_run(&m->fdc, now);
        bool line = lo == 0x14u ? m->fdc.intrq : m->fdc.drq;
        v = line ? 0x7Fu : 0xFFu;
    }
    microdisc_irq(m);
    return v;
}

void microdisc_write(oric_t *m, uint16_t a, uint8_t v) {
    uint64_t now = now_of(m);
    if ((uint8_t)a < 0x14u) {
        wd_write(&m->fdc, a & 3u, v, now);
    } else {
        m->md_latch = v;
        wd_select(&m->fdc, (v & MD_DRIVE) >> 5, (v & MD_SIDE) ? 1u : 0u, now);
        microdisc_map(m);
    }
    microdisc_irq(m);
    /* A command's first event may fall inside this run slice. */
    m->cut = true;
}

/* ---- the port's -------------------------------------------------------- */

const wd_req_t *oric_disc_request(const oric_t *m) {
    return wd_request(&m->fdc);
}

void oric_disc_served(oric_t *m, bool ok) {
    wd_served(&m->fdc, ok, m->cpu.cycles);
    microdisc_irq(m);
}

void oric_disc_insert(oric_t *m, unsigned drive, mfm_geom_t g, bool protect) {
    wd_insert(&m->fdc, drive, g, protect, m->cpu.cycles);
    microdisc_irq(m);
}

void oric_disc_eject(oric_t *m, unsigned drive) {
    wd_eject(&m->fdc, drive, m->cpu.cycles);
    microdisc_irq(m);
}

bool oric_disc_busy(const oric_t *m) {
    return wd_busy(&m->fdc) || wd_request(&m->fdc) != NULL;
}
