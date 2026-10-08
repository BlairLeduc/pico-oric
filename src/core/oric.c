/* oric.c — machine assembly: page table, run loop (design.md §4.2, §6). */

#include "oric.h"

#include <string.h>

#include "bus.h"
#include "hot.h"

void oric_config_default(oric_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->ram = ORIC_RAM_48K;   /* the Atmos 48K (§18 item 3) */
}

static void map_open(oric_t *m, unsigned first_page, unsigned last_page) {
    for (unsigned p = first_page; p <= last_page; p++) {
        m->page[p].read  = NULL;
        m->page[p].write = NULL;
        m->page_flags[p] = PAGE_OPEN;
    }
}

/* Pages first..last onto RAM starting at `base`, so a mirror is the same
 * RAM under another address and costs nothing (§6.2). */
static void map_rw(oric_t *m, unsigned first_page, unsigned last_page, uint32_t base) {
    for (unsigned p = first_page; p <= last_page; p++) {
        uint8_t *mem = &m->ram[base + (p - first_page) * ORIC_PAGE_SIZE];
        m->page[p].read  = mem;
        m->page[p].write = mem;
        m->page_flags[p] = 0;
    }
}

void oric_init(oric_t *m, const oric_config_t *cfg) {
    /* Zero-filled RAM (§6.3, EL §9.2). */
    memset(m, 0, sizeof(*m));
    m->cfg = *cfg;
    m->open_bus = 0xFFu;

    map_open(m, 0x00u, 0xFFu);

    if (m->cfg.ram == ORIC_RAM_16K) {
        /* Believed: the 16 KiB repeats through the 48 KiB below the ROM,
         * which is how the ULA's fixed fetch addresses find the screen
         * (§2.2, §6.2). §16 rates it medium-low; M3 settles it. */
        for (unsigned mirror = 0; mirror < 3u; mirror++)
            map_rw(m, mirror * 0x40u, mirror * 0x40u + 0x3Fu, 0);
    } else {
        map_rw(m, 0x00u, 0xBFu, 0);
    }

    /* Page #03 is I/O whatever the RAM (§2.2, §6.4). */
    m->page[ORIC_IO_PAGE].read  = NULL;
    m->page[ORIC_IO_PAGE].write = NULL;
    m->page_flags[ORIC_IO_PAGE] = PAGE_IO;

    /* #C000-#FFFF stays open until oric_load_rom fills the socket: the
     * repository ships no ROM (§10.2). */

    via6522_reset(&m->via);

    m6502_init(&m->cpu);
    m->budget = 0;
    oric_reset(m);
}

void oric_reset(oric_t *m) {
    m->cpu.reset_pending = false;
    /* The VIA is on the reset line with the CPU (§6.3; §16 to confirm
     * from the schematic). The pins are the machine's, not the chip's,
     * and keep their levels. */
    uint8_t in_a = m->via.in_a, in_b = m->via.in_b;
    via6522_reset(&m->via);
    m->via.in_a = in_a;
    m->via.in_b = in_b;
    m6502_set_irq(&m->cpu, M6502_IRQ_VIA, false);
    m6502_set_nmi(&m->cpu, false);
    m6502_reset(&m->cpu, m);
}

/* Move one page pointer from src's struct to dst's, whichever of its
 * arrays it points into. */
static uint8_t *rebase(oric_t *dst, const oric_t *src, uint8_t *p) {
    if (!p) return NULL;
    if (p >= src->rom && p < src->rom + ORIC_ROM_SIZE) return dst->rom + (p - src->rom);
    return dst->ram + (p - src->ram);
}

void oric_copy(oric_t *dst, const oric_t *src) {
    if (dst == src) return;
    memcpy(dst, src, sizeof(*dst));
    for (unsigned p = 0; p < ORIC_PAGE_COUNT; p++) {
        dst->page[p].read  = rebase(dst, src, src->page[p].read);
        dst->page[p].write = rebase(dst, src, src->page[p].write);
    }
}

bool oric_load_rom(oric_t *m, const uint8_t *data, size_t len) {
    if (len != ORIC_ROM_SIZE) return false;
    memcpy(m->rom, data, len);
    /* Writes to the ROM's pages are ignored while it is enabled; whether
     * they reach the overlay RAM beneath is M14's to settle (§16). */
    for (unsigned p = ORIC_ROM_BASE / ORIC_PAGE_SIZE; p < ORIC_PAGE_COUNT; p++) {
        m->page[p].read  = &m->rom[(p - ORIC_ROM_BASE / ORIC_PAGE_SIZE) * ORIC_PAGE_SIZE];
        m->page[p].write = NULL;
        m->page_flags[p] = PAGE_ROM;
    }
    return true;
}

void oric_map_ram(oric_t *m, uint16_t addr, uint32_t len) {
    /* A zero length maps nothing. Without this, addr + len - 1 underflows
     * and an oric_map_ram(m, 0, 0) quietly maps the whole address space. */
    if (len == 0) return;

    uint32_t end = (uint32_t)addr + len;
    if (end > ORIC_ADDR_SPACE) end = ORIC_ADDR_SPACE;

    unsigned first = (unsigned)(addr / ORIC_PAGE_SIZE);
    unsigned last  = (unsigned)((end - 1u) / ORIC_PAGE_SIZE);
    map_rw(m, first, last, first * ORIC_PAGE_SIZE);
}

uint32_t ORIC_HOT2(oric_run)(oric_t *m, uint32_t cycles) {
    uint32_t done = 0, n = 0;
    /* Whole instructions until at least `cycles` have elapsed; the caller
     * carries the overshoot forward as debt (§4.2, §5.3). The VIA is
     * ticked per instruction, a countdown that costs the same whatever
     * the chip is doing (§3.2, via6522.h). */
    while (done < cycles) {
        uint32_t c = m6502_step(m);
        n++;
        done += c;
        via6522_tick(&m->via, c);
        m6502_set_irq(&m->cpu, M6502_IRQ_VIA, via6522_irq(&m->via));
    }
    m->instructions += n;
    return done;
}
