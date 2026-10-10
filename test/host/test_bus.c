/* test_bus.c — the page table and oric_copy (design.md §4.2, §6.1).
 * M3 adds the configurations, the mirrors and page #03's side effects. */

#include <string.h>

#include "bus.h"
#include "oric.h"
#include "test_util.h"

static oric_t g_a, g_b;
static uint8_t g_rom[ORIC_ROM_SIZE];

/* ---- T1's flag as a loop polling IFR sees it (§5.3, M16) ----------------
 * A program starts T1 one-shot with STA T1C-H, waits `delay` cycles, then
 * polls IFR with LDA abs : AND #40 : BEQ, a nine-cycle loop. Each read is
 * placed by its access's cycle; over all nine delays the first read that
 * sees the flag and the one before meet in one cycle, counted from the
 * write's. */
#define T1_PROG   0x0400u
#define T1_LOOP   0x0420u
#define T1_LATCH  0x0100u

static bool t1_seen_at(unsigned delay, uint64_t *lo, uint64_t *hi) {
    oric_config_t cfg;
    oric_config_default(&cfg);
    memset(g_rom, 0, sizeof g_rom);
    g_rom[0x3FFCu] = (uint8_t)T1_PROG;
    g_rom[0x3FFDu] = (uint8_t)(T1_PROG >> 8);
    oric_init(&g_a, &cfg);
    oric_load_rom(&g_a, g_rom, sizeof g_rom);
    oric_power_on(&g_a);
    const uint8_t start[] = { 0xA9, (uint8_t)T1_LATCH, 0x8D, 0x04, 0x03,         /* T1C-L */
                              0xA9, (uint8_t)(T1_LATCH >> 8), 0x8D, 0x05, 0x03 }; /* T1C-H */
    memcpy(&g_a.ram[T1_PROG], start, sizeof start);
    uint8_t *d = &g_a.ram[T1_PROG + sizeof start];
    if (delay == 1) delay = 10;   /* 2s and 3s; 1 + 9 for 1 */
    while (delay >= 2 && delay != 3) { *d++ = 0xEA; delay -= 2; }
    if (delay == 3) { *d++ = 0xA5; *d++ = 0x00; }
    *d++ = 0x4C; *d++ = (uint8_t)T1_LOOP; *d++ = (uint8_t)(T1_LOOP >> 8);
    const uint8_t loop[] = { 0xAD, 0x0D, 0x03, 0x29, 0x40, 0xF0, 0xF9, 0x4C, 0x27, 0x04 };
    memcpy(&g_a.ram[T1_LOOP], loop, sizeof loop);

    uint64_t write = 0, prev = 0;
    while (g_a.cpu.cycles < 2000u) {
        uint16_t pc = g_a.cpu.pc;
        uint64_t at = g_a.cpu.cycles + 4u;   /* STA abs and LDA abs: the fourth cycle */
        oric_run(&g_a, 1);
        if (pc == T1_PROG + 7u) write = at;
        if (pc != T1_LOOP || !write) continue;
        if (g_a.cpu.a & 0x40u) {
            *lo = prev + 1u - write;
            *hi = at - write;
            return prev != 0;
        }
        prev = at;
    }
    return false;
}

static int test_t1_poll(void) {
    uint64_t lo = 0, hi = UINT64_MAX;
    for (unsigned d = 0; d < 9; d++) {
        uint64_t l, h;
        CHECK(t1_seen_at(d, &l, &h), "delay %u: the flag was seen", d);
        if (l > lo) lo = l;
        if (h < hi) hi = h;
    }
    /* The latch + 2 after the write: what agrees with Oricutron's trace
     * of a ROM's own T1 polled this way (M16), where a read in the last
     * cycle seeing the VIA a tick past the instruction saw it one sooner. */
    CHECK(lo == hi && lo == T1_LATCH + 2u, "T1's flag is first seen %llu-%llu cycles after the write",
          (unsigned long long)lo, (unsigned long long)hi);
    return test_failures;
}

/* ---- and T1's interrupt across a VIA write (§5.3, M16) ------------------
 * The same start, IER T1 on and CLI, then STA T2C-L : JMP, a seven-cycle
 * loop whose write is its STA's last cycle: the write's tick past the
 * instruction's end sets T1's flag for some phase. At every phase the
 * interrupt is taken at the first instruction boundary at or after the
 * flag's cycle, the latch + 2 after the write, as the poll above sees it
 * and as Oricutron's trace of a loop like this takes it. */
#define T1_HANDLER 0x0500u

static bool t1_irq_ok(unsigned delay, uint64_t *flag, uint64_t *before, uint64_t *entry) {
    oric_config_t cfg;
    oric_config_default(&cfg);
    memset(g_rom, 0, sizeof g_rom);
    g_rom[0x3FFCu] = (uint8_t)T1_PROG;
    g_rom[0x3FFDu] = (uint8_t)(T1_PROG >> 8);
    g_rom[0x3FFEu] = (uint8_t)T1_HANDLER;
    g_rom[0x3FFFu] = (uint8_t)(T1_HANDLER >> 8);
    oric_init(&g_a, &cfg);
    oric_load_rom(&g_a, g_rom, sizeof g_rom);
    oric_power_on(&g_a);
    const uint8_t start[] = { 0xA9, 0xC0, 0x8D, 0x0E, 0x03,                     /* IER T1 */
                              0xA9, (uint8_t)T1_LATCH, 0x8D, 0x04, 0x03,         /* T1C-L */
                              0xA9, (uint8_t)(T1_LATCH >> 8), 0x8D, 0x05, 0x03,  /* T1C-H */
                              0x58 };                                            /* CLI   */
    memcpy(&g_a.ram[T1_PROG], start, sizeof start);
    uint8_t *d = &g_a.ram[T1_PROG + sizeof start];
    if (delay == 1) delay = 8;   /* 2s and 3s; 1 + 7 for 1 */
    while (delay >= 2 && delay != 3) { *d++ = 0xEA; delay -= 2; }
    if (delay == 3) { *d++ = 0xA5; *d++ = 0x00; }
    *d++ = 0x4C; *d++ = (uint8_t)T1_LOOP; *d++ = (uint8_t)(T1_LOOP >> 8);
    const uint8_t loop[] = { 0x8D, 0x08, 0x03, 0x4C, (uint8_t)T1_LOOP, (uint8_t)(T1_LOOP >> 8) };
    memcpy(&g_a.ram[T1_LOOP], loop, sizeof loop);
    g_a.ram[T1_HANDLER] = 0x4C;                      /* JMP *: stop there */
    g_a.ram[T1_HANDLER + 1u] = (uint8_t)T1_HANDLER;
    g_a.ram[T1_HANDLER + 2u] = (uint8_t)(T1_HANDLER >> 8);

    uint64_t write = 0, start_of = 0;
    while (g_a.cpu.cycles < 2000u) {
        uint16_t pc = g_a.cpu.pc;
        uint64_t s = g_a.cpu.cycles;
        oric_run(&g_a, 1);
        if (pc == T1_PROG + 12u) write = s + 4u;     /* STA T1C-H's fourth cycle */
        if (g_a.cpu.pc == T1_HANDLER) {
            /* The entry's seven cycles began at the boundary. */
            *flag = write + T1_LATCH + 2u;
            *before = start_of;
            *entry = g_a.cpu.cycles - 7u;
            return write != 0;
        }
        start_of = s;
    }
    return false;
}

static int test_t1_irq_write(void) {
    for (unsigned d = 0; d < 7; d++) {
        uint64_t flag = 0, before = 0, entry = 0;
        bool ok = t1_irq_ok(d, &flag, &before, &entry);
        CHECK(ok && before < flag && flag <= entry,
              "delay %u: the flag at %llu, the interrupt at the boundary %llu after the "
              "instruction from %llu", d, (unsigned long long)flag, (unsigned long long)entry,
              (unsigned long long)before);
    }
    return test_failures;
}

int main(void) {
    test_t1_poll();
    test_t1_irq_write();

    /* ---- oric_copy moves every page pointer into the copy ------------ */
    {
        oric_config_t cfg;
        oric_config_default(&cfg);
        oric_init(&g_a, &cfg);
        for (unsigned i = 0; i < ORIC_ROM_SIZE; i++) g_rom[i] = (uint8_t)(i * 7u + 1u);
        CHECK(oric_load_rom(&g_a, g_rom, sizeof g_rom), "a 16 KiB ROM loads");
        bus_write(&g_a, 0x1234, 0x5A);
        oric_copy(&g_b, &g_a);

        unsigned stray = 0;
        for (unsigned p = 0; p < ORIC_PAGE_COUNT; p++) {
            const uint8_t *r = g_b.page[p].read, *w = g_b.page[p].write;
            const uint8_t *lo = (const uint8_t *)&g_b, *hi = lo + sizeof g_b;
            if (r && (r < lo || r >= hi)) stray++;
            if (w && (w < lo || w >= hi)) stray++;
        }
        CHECK(stray == 0, "%u page pointers still point outside the copy", stray);
        CHECK(bus_read(&g_b, 0x1234) == 0x5A, "RAM reads through the copy");
        CHECK(bus_read(&g_b, 0xC000) == g_rom[0] && bus_read(&g_b, 0xFFFF) == g_rom[0x3FFF],
              "ROM reads through the copy");

        bus_write(&g_b, 0x1234, 0xA5);
        CHECK(bus_read(&g_a, 0x1234) == 0x5A, "a write to the copy reached the original");
        bus_write(&g_b, 0xC000, 0x00);
        CHECK(bus_read(&g_b, 0xC000) == g_rom[0], "a write to the ROM changed it");
    }

    TEST_DONE();
}
