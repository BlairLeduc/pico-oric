/* test_field.c — the field and its debt (design.md §4.2, §11.1).
 *
 * No ROM: a loop of instructions whose lengths do not divide the field,
 * so every field overshoots and the debt has something to carry.
 */

#include "bus.h"
#include "oric.h"
#include "test_util.h"

static oric_t g_m;

static void build(oric_t *m) {
    oric_config_t cfg;
    oric_config_default(&cfg);
    oric_init(m, &cfg);
    oric_map_ram(m, 0xC000, 0x4000);
    /* #1000: INC #80 (5) ; NOP (2) ; JMP #1000 (3). Ten cycles, which
     * divides neither 19,968 nor 16,896. */
    static const uint8_t code[] = { 0xE6, 0x80, 0xEA, 0x4C, 0x00, 0x10 };
    for (unsigned i = 0; i < sizeof code; i++) bus_write(m, (uint16_t)(0x1000u + i), code[i]);
    m->cpu.pc = 0x1000;
}

int main(void) {
    /* ---- the lengths, as configuration ---------------------------------- */
    build(&g_m);
    CHECK(oric_field_cycles(&g_m) == 19968u, "50 Hz: 312 x 64, got %u", oric_field_cycles(&g_m));
    g_m.ula_mode &= (uint8_t)~ULA_MODE_50HZ;
    CHECK(oric_field_cycles(&g_m) == 16896u, "60 Hz: 264 x 64, got %u", oric_field_cycles(&g_m));
    g_m.cfg.lines_60hz = 263;
    CHECK(oric_field_cycles(&g_m) == 263u * 64u, "the length comes from the configuration");

    /* ---- debt: N fields are N lengths, give or take one instruction ----- */
    {
        build(&g_m);
        uint64_t c0 = g_m.cpu.cycles;   /* the reset sequence's */
        uint64_t sum = 0, want = 0;
        for (int i = 0; i < 1000; i++) {
            want += oric_field_cycles(&g_m);
            sum += oric_run_field(&g_m);
            if (i == 500) g_m.ula_mode &= (uint8_t)~ULA_MODE_50HZ;   /* the ULA switches mid-run */
        }
        int64_t over = (int64_t)(sum - want);
        CHECK(over >= 0 && over < 7, "1000 fields ran %lld cycles past their lengths", (long long)over);
        CHECK(over == -g_m.budget, "the debt is what was overrun: %lld vs %d", (long long)over,
              -g_m.budget);
        CHECK(g_m.fields == 1000u, "fields counted: %u", g_m.fields);
        CHECK(g_m.cpu.cycles - c0 == sum, "the CPU ran what the fields say");
    }

    /* ---- the switch, by a mode attribute on the screen (§7.4, §11.1) -- */
    {
        build(&g_m);
        CHECK(g_m.ula_mode == ULA_MODE_POWER_ON, "power-on: text at 50 Hz");
        bus_write(&g_m, 0xBFDF, 0x18);           /* text, 60 Hz, at the screen's last cell */
        uint32_t a = oric_run_field(&g_m);
        uint32_t b = oric_run_field(&g_m);
        CHECK(a >= 19968u && a < 19968u + 7u, "the field it was written in stays 50 Hz: %u", a);
        CHECK(b + 7u > 16896u && b < 16896u + 7u, "the next is 60 Hz, less the debt: %u", b);
        bus_write(&g_m, 0xBFDF, 0x1A);
        oric_run_field(&g_m);
        CHECK(oric_field_cycles(&g_m) == 19968u, "#1A, as the ROMs write it, is 50 Hz again");
    }

    /* The control: whole fields without the debt drift by an overshoot a
     * field, which the check above would catch. */
    {
        build(&g_m);
        uint64_t sum = 0;
        for (int i = 0; i < 1000; i++) sum += oric_run(&g_m, oric_field_cycles(&g_m));
        CHECK(sum - 1000u * 19968u > 100u, "the control should drift, ran only %llu over",
              (unsigned long long)(sum - 1000u * 19968u));
    }

    TEST_DONE();
}
