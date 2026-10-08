/* test_test_rom.c — the machine's wiring, by our own ROM (design.md §13.3).
 *
 * test/asm/oric_test_rom.s, assembled by the build when ca65 is on the
 * path, so CI runs it on every push without an Oric ROM. Skipped when it
 * was not built. Each check reads back what the ROM's code left.
 */

#include <string.h>

#include "bus.h"
#include "oric.h"
#include "test_util.h"

static oric_t g_m;
static uint8_t rom[ORIC_ROM_SIZE];

/* Where the ROM leaves things (the table at the top of the source). */
#define DONE     0x02u
#define RAMSIZE  0x03u
#define AYBACK   0x04u
#define PORTA    0x12u
#define KEYS     0x18u
#define IRQS     0x20u
#define NMIS     0x22u

/* The keys held through the reset: row, column. */
static const int held[][2] = { { 3, 1 }, { 4, 4 }, { 7, 6 }, { 7, 0 } };

static bool boot(oric_ram_t ram) {
    oric_config_t cfg;
    oric_config_default(&cfg);
    cfg.ram = ram;
    oric_init(&g_m, &cfg);
    oric_load_rom(&g_m, rom, sizeof rom);
    for (unsigned i = 0; i < sizeof held / sizeof held[0]; i++)
        oric_key_set(&g_m, held[i][0], held[i][1], true);
    oric_reset(&g_m);
    for (int i = 0; i < 100 && g_m.ram[DONE] != 0xA5u; i++) oric_run_field(&g_m);
    return g_m.ram[DONE] == 0xA5u;
}

int main(void) {
    FILE *f = fopen(PICO_ORIC_TEST_ROM, "rb");
    if (!f) {
        printf("skipped: %s not built (ca65 and ld65 not found)\n", PICO_ORIC_TEST_ROM);
        return TEST_SKIP_CODE;
    }
    size_t n = fread(rom, 1, sizeof rom, f);
    fclose(f);
    CHECK(n == sizeof rom, "the test ROM is %zu bytes", n);

    for (int r = 0; r < 2; r++) {
        oric_ram_t ram = r ? ORIC_RAM_48K : ORIC_RAM_16K;
        const char *name = r ? "48K" : "16K";
        CHECK(boot(ram), "%s: the ROM never finished", name);
        uint8_t *z = g_m.ram;   /* page zero is not mirrored away on either */

        /* ---- RAM size, by aliasing ---------------------------------------- */
        CHECK(z[RAMSIZE] == (r ? 48 : 16), "%s: the ROM found %uK", name, z[RAMSIZE]);

        /* ---- AY registers, written and read back through the VIA ---------- */
        {
            /* What the ROM writes, and the bits each register keeps
             * (AY-3-8910/8912 data manual). */
            static const uint8_t wrote[14] = { 0xA5, 0x0F, 0x5A, 0x0A, 0x3C, 0x05, 0x1F,
                                               0x3F, 0x10, 0x0F, 0x1A, 0xC3, 0x3C, 0x0E };
            static const uint8_t keep[14] = { 0xFF, 0x0F, 0xFF, 0x0F, 0xFF, 0x0F, 0x1F,
                                              0xFF, 0x1F, 0x1F, 0x1F, 0xFF, 0xFF, 0x0F };
            for (unsigned i = 0; i < 14; i++)
                CHECK(z[AYBACK + i] == (wrote[i] & keep[i]), "%s: AY register %u read %02X", name,
                      i, z[AYBACK + i]);
            CHECK(z[PORTA] == 0x5A, "%s: port A read %02X", name, z[PORTA]);
        }

        /* ---- the keyboard scan ---------------------------------------------- */
        {
            uint8_t want[8] = { 0 };
            for (unsigned i = 0; i < sizeof held / sizeof held[0]; i++)
                want[held[i][0]] |= (uint8_t)(1u << held[i][1]);
            for (unsigned row = 0; row < 8; row++)
                CHECK(z[KEYS + row] == want[row], "%s: row %u scanned %02X, want %02X", name, row,
                      z[KEYS + row], want[row]);
        }

        /* ---- the screen ------------------------------------------------------ */
        {
            static const char t0[] = "ORIC TEST ROM";
            CHECK(oric_peek(&g_m, 0xBB80) == 0x01 && oric_peek(&g_m, 0xBB81) == 0x14,
                  "%s: row 0's attributes", name);
            for (unsigned i = 0; i < sizeof t0 - 1; i++)
                CHECK(oric_peek(&g_m, (uint16_t)(0xBB82 + i)) == (uint8_t)t0[i], "%s: row 0 col %u",
                      name, i + 2);
            CHECK(oric_peek(&g_m, 0xBBA8) == 0x0F, "%s: row 1's attribute", name);
            CHECK(oric_peek(&g_m, 0xBBD0) == 0x07 && oric_peek(&g_m, 0xBBD2) == ('I' | 0x80),
                  "%s: row 2, inverse", name);
            unsigned bad = 0;
            for (unsigned c = 0; c < 128; c++)
                for (unsigned row = 0; row < 8; row++)
                    if (oric_peek(&g_m, (uint16_t)(0xB400 + c * 8 + row)) != ((c + row * 3) & 0x3F)) bad++;
            CHECK(bad == 0, "%s: %u font bytes wrong", name, bad);
            bad = 0;
            for (unsigned line = 0; line < 100; line++)
                for (unsigned x = 0; x < 40; x++) {
                    uint8_t want = (uint8_t)((((line * 7) % 64) ^ x) & 0x3F) | 0x40;
                    if (oric_peek(&g_m, (uint16_t)(0xA000 + line * 40 + x)) != want) bad++;
                }
            CHECK(bad == 0, "%s: %u hires bytes wrong", name, bad);
            if (r == 0)
                CHECK(g_m.ram[0x3B82] == 'O' && g_m.ram[0x3400 + 5 * 8 + 1] == 8,
                      "16K: #BB82 and #B400 land on #3B82 and #3400");
        }

        /* ---- T1: 100 interrupts a second, and PB7 ---------------------------- */
        {
            uint16_t i0 = (uint16_t)(z[IRQS] | z[IRQS + 1] << 8);
            uint64_t t0 = g_m.cpu.cycles;
            unsigned edges = 0;
            bool pb7 = via6522_pb_out(&g_m.via) & 0x80u;
            while (g_m.cpu.cycles - t0 < ORIC_CPU_HZ) {
                oric_run(&g_m, 50);
                bool now = via6522_pb_out(&g_m.via) & 0x80u;
                if (now != pb7) edges++;
                pb7 = now;
            }
            unsigned irqs = (uint16_t)(z[IRQS] | z[IRQS + 1] << 8) - i0;
            CHECK(irqs >= 99 && irqs <= 101, "%s: %u T1 interrupts in a second", name, irqs);
            CHECK(edges >= 99 && edges <= 101, "%s: PB7 changed %u times in a second", name, edges);
        }

        /* ---- NMI ---------------------------------------------------------------- */
        oric_nmi(&g_m);
        oric_run(&g_m, 100);
        CHECK(z[NMIS] == 1, "%s: %u NMIs taken for one press", name, z[NMIS]);
        oric_run(&g_m, 100000);
        CHECK(z[NMIS] == 1, "%s: one press taken %u times", name, z[NMIS]);
    }

    TEST_DONE();
}
