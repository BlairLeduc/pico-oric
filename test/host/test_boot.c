/* test_boot.c — both ROMs on all four machines (design.md §15.2 M3).
 *
 * Needs basic10.rom and basic11b.rom in roms/ (§13.3); without them it
 * reports skipped, which is not a pass. What it checks, each by running
 * the ROM:
 *   - power-on to Ready on 1.0 and 1.1, 16K and 48K, and the banner;
 *   - PRINT 2+2 typed into the matrix prints 4;
 *   - the 16K mirror: without it, neither ROM gets past its RAM test;
 *   - T1 is the 100 Hz interrupt, and the ROM's handler runs at that rate;
 *   - RND's sequence after power-on;
 *   - NMI warm-starts and keeps the program; RESET cold-starts;
 *   - no undocumented opcode on the way.
 * It prints the cycles from power-on to Ready for each machine.
 */

#include <string.h>

#include "guest.h"
#include "test_util.h"

static guest_t g;

static const char *name(rom_id_t r, oric_ram_t ram) {
    if (r == ROM_BASIC10) return ram == ORIC_RAM_16K ? "1.0 16K" : "1.0 48K";
    return ram == ORIC_RAM_16K ? "1.1 16K" : "1.1 48K";
}

/* What each machine reports at power-on. The banner is the ROM's; the
 * figures are what these ROMs print on the machine as modelled, and
 * they check against Oricutron's in M3's trace diff. */
static const struct {
    rom_id_t   rom;
    oric_ram_t ram;
    const char *banner, *free;
} machines[] = {
    { ROM_BASIC10, ORIC_RAM_16K, "ORIC EXTENDED BASIC V1.0", "15102 BYTES FREE" },
    { ROM_BASIC10, ORIC_RAM_48K, "ORIC EXTENDED BASIC V1.0", "47870 BYTES FREE" },
    { ROM_BASIC11, ORIC_RAM_16K, "ORIC EXTENDED BASIC V1.1", "4863 BYTES FREE" },
    { ROM_BASIC11, ORIC_RAM_48K, "ORIC EXTENDED BASIC V1.1", "37631 BYTES FREE" },
};

/* The row after the one showing `typed`, or -1. */
static int answer_row(const oric_t *m, const char *typed) {
    int r = guest_find_row(m, typed, 0);
    return r < 0 ? -1 : r + 1;
}

int main(void) {
    const char *dir;
    if (!guest_find_roms(&dir)) {
        printf("skipped: basic10.rom and basic11b.rom are not both in %s\n", dir);
        return TEST_SKIP_CODE;
    }

    for (unsigned i = 0; i < sizeof machines / sizeof machines[0]; i++) {
        const char *n = name(machines[i].rom, machines[i].ram);

        /* ---- power-on to Ready ------------------------------------------ */
        bool ok = guest_boot(&g, machines[i].rom, machines[i].ram);
        CHECK(ok, "%s: no Ready", n);
        if (!ok) { guest_dump(&g.m, stderr); continue; }
        printf("%s: Ready after %llu cycles\n", n, (unsigned long long)g.ready_cycles);
        CHECK(guest_find_row(&g.m, machines[i].banner, 0) >= 0, "%s: no banner", n);
        CHECK(guest_find_row(&g.m, machines[i].free, 0) >= 0, "%s: not \"%s\"", n, machines[i].free);

        /* ---- T1 drives the ROM's interrupt at 100 Hz -------------------- */
        {
            /* Latch 10,000 in free-run: a period of 10,002 cycles (the
             * part's latch + 2), so one second is 99 or 100 IRQs. */
            CHECK(g.m.via.t1_latch == 10000u, "%s: T1 latch %u", n, g.m.via.t1_latch);
            CHECK(g.m.via.acr & VIA_ACR_T1_FREERUN, "%s: T1 not free-running", n);
            CHECK(g.m.via.ier == VIA_INT_T1, "%s: IER %02X, want T1 alone", n, g.m.via.ier);
            uint16_t vec = (uint16_t)(oric_peek(&g.m, 0xFFFE) | oric_peek(&g.m, 0xFFFF) << 8);
            unsigned irqs = 0;
            uint64_t t0 = g.m.cpu.cycles;
            while (g.m.cpu.cycles - t0 < ORIC_CPU_HZ) {
                oric_run(&g.m, 1);
                if (g.m.cpu.pc == vec) irqs++;
            }
            CHECK(irqs == 99 || irqs == 100, "%s: %u IRQs in a second", n, irqs);
        }

        /* ---- PRINT 2+2 through the matrix ------------------------------- */
        guest_type(&g, "PRINT 2+2\n");
        int a = answer_row(&g.m, "PRINT 2+2");
        CHECK(a > 0 && guest_find_row(&g.m, "4", a) == a, "%s: PRINT 2+2 did not print 4", n);
        if (a <= 0) guest_dump(&g.m, stderr);

        /* ---- RND from power-on ----------------------------------------- */
        /* The same three numbers from both ROMs on every power-on,
         * however long the machine idles: the seed is the ROM's, and
         * zeroed RAM does not leave it stuck (§6.3, §16). */
        guest_type(&g, "PRINT RND(1)\n");
        a = answer_row(&g.m, "PRINT RND(1)");
        CHECK(a > 0 && guest_find_row(&g.m, ".270011996", a) == a, "%s: RND(1) first value", n);
        guest_type(&g, "PRINT RND(1)\n");
        int second = a > 0 ? guest_find_row(&g.m, "PRINT RND(1)", a) : -1;
        CHECK(second > 0 && guest_find_row(&g.m, ".139756248", second + 1) == second + 1,
              "%s: RND(1) second value", n);

        /* ---- NMI keeps the program; RESET does not ---------------------- */
        guest_type(&g, "10 PRINT 7*6\n");
        oric_nmi(&g.m);
        guest_fields(&g, 100);
        CHECK(guest_find_row(&g.m, machines[i].banner, 0) < 0, "%s: NMI cold-started", n);
        CHECK(guest_find_row(&g.m, "Ready", 0) >= 0, "%s: no Ready after NMI", n);
        guest_type(&g, "RUN\n");
        a = answer_row(&g.m, "RUN");
        CHECK(a > 0 && guest_find_row(&g.m, "42", a) == a, "%s: the program did not survive NMI", n);

        oric_reset(&g.m);
        guest_fields(&g, 200);
        CHECK(guest_find_row(&g.m, machines[i].banner, 0) >= 0, "%s: RESET did not cold-start", n);
        guest_type(&g, "LIST\n");
        CHECK(answer_row(&g.m, "LIST") > 0, "%s: LIST not typed", n);
        CHECK(guest_find_row(&g.m, "10 PRINT 7*6", 0) < 0, "%s: a program survived RESET", n);

        CHECK(g.m.cpu.undoc_count == 0, "%s: %u undocumented opcodes, the last %02X at %04X", n,
              g.m.cpu.undoc_count, g.m.cpu.undoc_op, g.m.cpu.undoc_pc);
    }

    /* ---- the 16K mirror ------------------------------------------------- */
    /* The control: a 16K machine with nothing above #3FFF. Both ROMs
     * write their screen at #BB80 and size memory by writing and
     * reading back; without the mirror neither reaches Ready. */
    for (int r = ROM_BASIC10; r <= ROM_BASIC11; r++) {
        oric_config_t cfg;
        oric_config_default(&cfg);
        cfg.rom = (rom_id_t)r;
        cfg.ram = ORIC_RAM_16K;
        oric_init(&g.m, &cfg);
        oric_load_rom(&g.m, guest_rom_image((rom_id_t)r), ORIC_ROM_SIZE);
        for (unsigned p = 0x40; p < 0xC0; p++) {
            g.m.page[p].read = g.m.page[p].write = NULL;
            g.m.page_flags[p] = PAGE_OPEN;
        }
        oric_reset(&g.m);
        CHECK(!guest_boot_machine(&g), "%s booted with no mirror",
              name((rom_id_t)r, ORIC_RAM_16K));
    }

    TEST_DONE();
}
