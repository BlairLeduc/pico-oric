/* oric.c — machine assembly: page table, run loop (design.md §4.2, §6). */

#include "oric.h"

#include <string.h>

#include "bus.h"
#include "hot.h"

/* The firmware's PICO_ORIC_TAPE=OFF compiles the trap's check out, the
 * control for its cost (§15.2 M10). */
#ifndef PICO_ORIC_TAPE_TRAP
#define PICO_ORIC_TAPE_TRAP 1
#endif

void oric_config_default(oric_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    /* The Atmos 48K (§18 item 3). */
    cfg->rom = ROM_BASIC11;
    cfg->ram = ORIC_RAM_48K;
    /* Believed, not settled (§11.1, §16: medium). */
    cfg->line_cycles = 64;
    cfg->lines_50hz = 312;
    cfg->lines_60hz = 264;
    /* MAME's 32 and Brown's, against Oricutron's 16 (§16: disputed). */
    cfg->blink_fields = 32;
    cfg->tape_traps = true;
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
    memset(m, 0, sizeof(*m));
    m->cfg = *cfg;
    pcm_init(&m->pcm, 0, AY_LEVEL_MAX, 0, 0, 0);
    ay8912_set_average(&m->ay, m->pcm.num, m->pcm.den);

    map_open(m, 0x00u, 0xFFu);

    if (m->cfg.ram == ORIC_RAM_16K) {
        /* The 16 KiB repeats through the 48 KiB below the ROM, which is
         * how the ULA's fixed fetch addresses find the screen; neither ROM
         * boots without it (§2.2, §6.2, §16). */
        const unsigned pages = ORIC_RAM16_SIZE / ORIC_PAGE_SIZE;
        for (unsigned first = 0; first < ORIC_ROM_BASE / ORIC_PAGE_SIZE; first += pages)
            map_rw(m, first, first + pages - 1u, 0);
    } else {
        map_rw(m, 0x00u, 0xBFu, 0);
    }

    /* Page #03 is I/O whatever the RAM (§2.2, §6.4). */
    m->page[ORIC_IO_PAGE].read  = NULL;
    m->page[ORIC_IO_PAGE].write = NULL;
    m->page_flags[ORIC_IO_PAGE] = PAGE_IO;

    /* #C000-#FFFF stays open until oric_load_rom fills the socket: the
     * repository ships no ROM (§10.2). */

    oric_power_on(m);
}

void oric_power_on(oric_t *m) {
    /* Zero-filled RAM (§6.3, EL §9.2). */
    memset(m->ram, 0, sizeof(m->ram));
    m->open_bus = 0xFFu;
    m->ula_mode = ULA_MODE_POWER_ON;
    m->frame_mode = ULA_MODE_POWER_ON;
    m->budget = 0;
    m->fields = 0;
    m6502_init(&m->cpu);
    /* The cycle count starts again, and the next sample with it. */
    pcm_restart(&m->pcm, (uint32_t)m->cpu.cycles);
    oric_reset(m);
}

/* ---- The VIA's wiring (§2.3) ------------------------------------------- */

/* Put the VIA's outputs on what they drive, and what that drives back on
 * the VIA's inputs. Called after anything that can change a line: a
 * register access, a key, a reset.
 *
 * The AY: BC1 is CA2 and BDIR is CB2. Both ROMs' register write (#F535
 * in 1.0, #F590 in 1.1) sets the PCR to #EE, both high, to latch the
 * register number from PA, then #EC, CA2 low and CB2 high, to write
 * (§16, settled 2026-10-08 by reading both ROMs).
 *
 * The keyboard: PB0-PB2 select a row and AY port A's zero bits enable
 * columns; PB3 reads high while a key is down in an enabled column of
 * that row. Both scans write a column mask with one zero bit (#7F, #BF,
 * ...) and take PB3 set as a key (#F506 in 1.0, #F561 in 1.1; §16). */
static void ORIC_HOT1(wire)(oric_t *m) {
    via6522_t *v = &m->via;

    ay_bus_t mode = (ay_bus_t)((v->cb2 ? 2u : 0u) | (v->ca2 ? 1u : 0u));
    /* Inside an instruction the count is still its start, which is
     * where a write is stamped (EL §6.1). */
    ay8912_bus(&m->ay, mode, via6522_pa_out(v), m->cpu.cycles, &m->pcm);
    via6522_set_pa(v, m->ay.driving ? m->ay.bus_out : 0xFFu);

    uint8_t row = (uint8_t)(via6522_pb_out(v) & 7u);
    uint8_t enabled = (uint8_t)~ay8912_port_a(&m->ay);
    bool down = (m->keys[row] & enabled) != 0;
    via6522_set_pb(v, down ? 0xFFu : 0xF7u);

    m6502_set_irq(&m->cpu, M6502_IRQ_VIA, via6522_irq(v));
}

void ORIC_HOT1(oric_io_changed)(oric_t *m) {
    wire(m);
}

void ORIC_HOT1(oric_via_catch_up)(oric_t *m) {
    /* The VIA runs two cycles behind the CPU: at an instruction boundary
     * it has been ticked to the start of the instruction's penultimate
     * cycle. A 6502 decides whether to take an IRQ at the end of that
     * cycle, so an IRQ asserted in the last cycle waits for the next
     * instruction (§5.3; settled against Oricutron by trace, M3). From
     * there, the start of the instruction's cycle k is k + 1 ticks on;
     * the run loop ticks the rest. */
    uint32_t before = m->cpu.io_at ? m->cpu.io_at + 1u : 0u;
    if (before > m->via_early) {
        via6522_tick(&m->via, before - m->via_early);
        m->via_early = before;
    }
}

void oric_reset(oric_t *m) {
    m->cpu.reset_pending = false;
    /* The VIA and the AY are on the reset line with the CPU (§6.3; §16
     * to confirm from the schematic). */
    via6522_reset(&m->via);
    ay8912_reset(&m->ay, m->cpu.cycles, &m->pcm);
    tape_reset(m);
    wire(m);
    m6502_set_nmi(&m->cpu, false);
    m6502_reset(&m->cpu, m);
}

void oric_nmi(oric_t *m) {
    /* A press is one falling edge on /NMI; the CPU latches it (§5.2). */
    m6502_set_nmi(&m->cpu, true);
    m6502_set_nmi(&m->cpu, false);
}

void oric_key_set(oric_t *m, int row, int col, bool down) {
    if (row < 0 || row >= (int)ORIC_KEY_ROWS || col < 0 || col >= (int)ORIC_KEY_COLS) return;
    uint8_t bit = (uint8_t)(1u << col);
    if (down) m->keys[row] |= bit;
    else      m->keys[row] = (uint8_t)(m->keys[row] & ~bit);
    wire(m);
}

uint8_t oric_peek(const oric_t *m, uint16_t addr) {
    const uint8_t *p = m->page[addr >> 8].read;
    return p ? p[addr & 0xFFu] : m->open_bus;
}

/* Move one page pointer from src's struct to dst's. Only oric_load_rom
 * points a page into rom[], and it flags the page PAGE_ROM, so the flag
 * says which array without comparing pointers into different arrays. */
static uint8_t *rebase(oric_t *dst, const oric_t *src, unsigned page, const uint8_t *p) {
    if (!p) return NULL;
    if (src->page_flags[page] & PAGE_ROM) return dst->rom + (p - src->rom);
    return dst->ram + (p - src->ram);
}

void oric_copy(oric_t *dst, const oric_t *src) {
    if (dst == src) return;
    memcpy(dst, src, sizeof(*dst));
    for (unsigned p = 0; p < ORIC_PAGE_COUNT; p++) {
        dst->page[p].read  = rebase(dst, src, p, src->page[p].read);
        dst->page[p].write = rebase(dst, src, p, src->page[p].write);
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
    tape_rom_loaded(m, data, len);
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
     * the chip is doing (§3.2, via6522.h), and up to the cycle of any
     * access to it part-way through one (bus.c). */
    while (done < cycles) {
        uint32_t c;
        if (PICO_ORIC_TAPE_TRAP && __builtin_expect(tape_pc_lo[m->cpu.pc & 0xFFu], 0) &&
            tape_at(m)) {
            /* Stalled on a tape request, like a 6502 with RDY low: the
             * rest of the run passes with no instruction (§10.3). */
            c = cycles - done;
            m->cpu.cycles += c;
        } else {
            c = m6502_step(m);
            n++;
        }
        done += c;
        /* Less what an access to page #03 already brought it through. */
        via6522_tick(&m->via, c - m->via_early);
        m->via_early = 0;
        m6502_set_irq(&m->cpu, M6502_IRQ_VIA, via6522_irq(&m->via));
    }
    m->instructions += n;
    return done;
}

uint32_t oric_field_cycles(const oric_t *m) {
    bool hz50 = (m->ula_mode & ULA_MODE_50HZ) != 0;
    return (uint32_t)m->cfg.line_cycles * (hz50 ? m->cfg.lines_50hz : m->cfg.lines_60hz);
}

/* Hot like oric_run, which the compiler inlines here: otherwise tier 2
 * leaves the run loop in flash, calling into SRAM through a veneer every
 * instruction (hot.h). */
uint32_t ORIC_HOT2(oric_run_field)(oric_t *m) {
    /* The field began where the last one's overshoot says (§4.2). */
    int32_t want = (int32_t)oric_field_cycles(m) + m->budget;
    uint32_t done = want > 0 ? oric_run(m, (uint32_t)want) : 0;
    m->budget = want - (int32_t)done;
    m->fields++;
    ay8912_advance(&m->ay, m->cpu.cycles, &m->pcm);

    /* The frame about to be drawn starts in the mode the last one left;
     * what it leaves sets the next field's length, from the next field
     * (§7.4, §11.1, §16: low). */
    m->frame_mode = m->ula_mode;
    m->ula_mode = ula_scan_mode(oric_video_window(m), m->frame_mode);
    return done;
}

void oric_audio_set_rate(oric_t *m, uint32_t rate_num, uint32_t rate_den) {
    pcm_t *p = &m->pcm;
    ay8912_advance(&m->ay, m->cpu.cycles, p);
    /* What is waiting and the DC blocker stay as they are, so the
     * change makes no step. */
    pcm_set_rate(p, (uint32_t)m->cpu.cycles, AY_LEVEL_MAX, ORIC_CPU_HZ, rate_num, rate_den);
    ay8912_set_average(&m->ay, p->num, p->den);
    p->level = ay8912_level(&m->ay);
}

size_t oric_audio_drain(oric_t *m, int16_t *dst, size_t max) {
    return pcm_drain(&m->pcm, dst, max);
}

const uint8_t *oric_video_window(const oric_t *m) {
    /* The page table already says where #9800 is, mirror or not, and
     * the window's 40 pages are contiguous in both fits (§6.2). */
    return m->page[ORIC_VIDEO_BASE / ORIC_PAGE_SIZE].read;
}

bool oric_blink_on(const oric_t *m) {
    /* Shown in the first phase, hidden in the second, as MAME's counter
     * runs (§16). */
    unsigned n = m->cfg.blink_fields ? m->cfg.blink_fields : 1u;
    return ((m->fields / n) & 1u) == 0;
}

void oric_video_take(const oric_t *m, oric_frame_t *f) {
    memcpy(f->window, oric_video_window(m), ORIC_VIDEO_BYTES);
    f->mode = m->frame_mode;
    f->blink_on = oric_blink_on(m);
    f->field = m->fields;
}
