/* test_bus.c — the page table and oric_copy (design.md §4.2, §6.1).
 * M3 adds the configurations, the mirrors and page #03's side effects. */

#include <string.h>

#include "bus.h"
#include "oric.h"
#include "test_util.h"

static oric_t g_a, g_b;
static uint8_t g_rom[ORIC_ROM_SIZE];

int main(void) {
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
