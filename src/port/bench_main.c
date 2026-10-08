/* bench_main.c — M2's firmware: the 6502 alone on the board, timed
 * (design.md §3.2, §15.2 M2).
 *
 * No LCD, keyboard or audio, and one core. Each workload in src/bench/ is
 * loaded afresh and run for BENCH_RUN_CYCLES, ten seconds of a 1 MHz
 * Oric, with nothing printed while the clock runs. Each run reports host
 * cycles per guest instruction, mean guest cycles per instruction, and
 * the share of core 0 a 1 MHz guest would take at that rate. The cycle
 * and instruction counts must equal test_bench's on the host, which
 * bench.h records: the same code ran.
 *
 * Host cycles are microseconds times clk_sys, which is fixed at 150 MHz
 * (design.md §3.1) and reported in the banner.
 */
#include <stdbool.h>
#include <stdio.h>

#include "hardware/clocks.h"
#include "pico/stdlib.h"

#include "bench.h"
#include "board.h"
#include "pico_oric_version.h"

#if PICO_ORIC_BENCH_DORMANN
extern const uint8_t bench_dormann[], bench_dormann_end[];
#endif

static board_info_t g_board;
static bench_t      g_bench;

static void run(const char *name, unsigned pass, uint32_t host_cycles,
                uint32_t host_insns) {
    uint64_t t0 = time_us_64();
    uint64_t c = bench_run(&g_bench, BENCH_RUN_CYCLES);
    uint64_t us = time_us_64() - t0;

    uint64_t insns = g_bench.m.instructions;
    double cycles = (double)us * clock_get_hz(clk_sys) / 1e6;
    double guest_hz = (double)c / ((double)us / 1e6);
    bool same = c == host_cycles && insns == host_insns;
    printf("bench %-7s pass %u: %llu cyc %llu insn %llu us | %.1f host cyc/insn "
           "%.3f cyc/insn %.2f MHz guest, core 0 %.1f %% at 1 MHz | %s host\n",
           name, pass, (unsigned long long)c, (unsigned long long)insns,
           (unsigned long long)us, cycles / (double)insns, (double)c / (double)insns,
           guest_hz / 1e6, 100.0 * 1e6 / guest_hz, same ? "=" : "NOT =");
}

int main(void) {
    bool clocks_ok = board_init_clocks();
    stdio_init_all();
    board_temp_init();
    board_identify(&g_board);

    board_log_banner(&g_board);
    printf("  firmware     : %s (M2 bench)\n", PICO_ORIC_VERSION);
    printf("  SRAM tier    : %d\n", PICO_ORIC_RAM_TIER);
    if (!clocks_ok)
        printf("  WARNING: clk_sys is not at 150 MHz; the numbers below are not M2's\n");

    /* One core and no deadline: a blocking printf between runs costs
     * nothing that is timed (hardware-notes.md §2.7). */
    for (unsigned pass = 1;; pass++) {
        bench_basic_load(&g_bench);
        run("basic", pass, BENCH_BASIC_RUN_CYCLES, BENCH_BASIC_RUN_INSNS);

#if PICO_ORIC_BENCH_DORMANN
        if (bench_dormann_load(&g_bench, bench_dormann,
                               (size_t)(bench_dormann_end - bench_dormann)))
            run("dormann", pass, BENCH_DORMANN_RUN_CYCLES, BENCH_DORMANN_RUN_INSNS);
        else if (pass == 1)
            printf("bench dormann: the embedded image is not 64 KiB\n");
#else
        if (pass == 1)
            printf("bench dormann: not in this image; run tools/fetch-test-suites.sh, "
                   "then reconfigure CMake and rebuild\n");
#endif
        printf("bench die %d C\n", board_temp_c());
    }
}
