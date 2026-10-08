/* bench.h — M2's workloads: the 6502 alone on a flat 64 KiB bus
 * (design.md §3.2, §15.2 M2).
 *
 * Portable, like src/core/: no SDK header and no allocation. The host test
 * (test/host/test_bench.c) and the bench firmware (src/port/bench_main.c)
 * load and run the same programs, so the cycles and instruction counts the
 * board reports are the ones the host checked.
 *
 *   basic    MS BASIC's CHRGET walking a line of sums, and a floating-point
 *            add on unpacked accumulators, standing in for the Oric ROM's
 *            interpreter until M3 runs the real one
 *   dormann  the start of Klaus Dormann's functional test: a wide
 *            instruction mix, a control that is not BASIC-shaped
 *
 * The machine is oric_t with every page plain RAM, so the run loop is the
 * real one (oric_run, with the VIA ticked per instruction) and only the
 * bus is flat.
 */
#ifndef PICO_ORIC_BENCH_H
#define PICO_ORIC_BENCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "oric.h"

/* One 50 Hz field (design.md §11.1): the slice each oric_run call is
 * given, so the loop has the shape the machine's will. */
#define BENCH_SLICE_CYCLES   20000u

/* One timed run: 500 fields, ten seconds of a 1 MHz Oric. */
#define BENCH_RUN_CYCLES     (500u * BENCH_SLICE_CYCLES)

/* The basic program's results. Each statement of its line stores its sum
 * as a packed five-byte float at BENCH_BASIC_RESULT + 5n; each pass of
 * the line adds one to the 16-bit count at BENCH_BASIC_PASSES. */
#define BENCH_BASIC_RESULT   0x0900u
#define BENCH_BASIC_PASSES   0x0910u
#define BENCH_BASIC_STMTS    3u

/* What one timed run (BENCH_RUN_CYCLES) of each workload counts, as
 * test_bench measured them on the host. The firmware compares its own
 * counts with these, and they must be equal: the same code ran. */
#define BENCH_BASIC_RUN_CYCLES     10000001u
#define BENCH_BASIC_RUN_INSNS       3076774u
#define BENCH_DORMANN_RUN_CYCLES   10000002u
#define BENCH_DORMANN_RUN_INSNS     3196835u

typedef struct {
    oric_t m;
} bench_t;

/* Clear the machine, load the basic program, and point the CPU at it.
 * Every page takes the fast path. */
void bench_basic_load(bench_t *b);

/* Clear the machine and load Dormann's 64 KiB image, start #0400. False
 * if it is not 64 KiB. Every page takes the fast path. */
bool bench_dormann_load(bench_t *b, const uint8_t *image, size_t len);

/* Run in BENCH_SLICE_CYCLES slices until at least `cycles` have passed,
 * and return the cycles run. */
uint64_t bench_run(bench_t *b, uint64_t cycles);

#endif /* PICO_ORIC_BENCH_H */
