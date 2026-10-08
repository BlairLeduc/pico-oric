/* test_bench.c — M2's workloads, run on the host (design.md §15.2 M2).
 *
 *   test_bench                               the basic program
 *   test_bench path/6502_functional_test.bin the start of Dormann's
 *                                            test, skipped if not fetched
 *
 * Checks that each program does what it is meant to before the board
 * times it, and that one timed run (BENCH_RUN_CYCLES) counts the cycles
 * and instructions bench.h records: the bench firmware runs the same
 * code, so the board must report the same two counts.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bench.h"
#include "m6502.h"
#include "test_util.h"

static bench_t b;

static uint16_t passes(void) {
    return (uint16_t)(b.m.ram[BENCH_BASIC_PASSES] | b.m.ram[BENCH_BASIC_PASSES + 1] << 8);
}

static void report(const char *name, uint64_t cycles) {
    printf("%s: %llu cycles, %llu instructions, %.3f cycles per instruction\n", name,
           (unsigned long long)cycles, (unsigned long long)b.m.instructions,
           (double)cycles / (double)b.m.instructions);
}

/* 45, 45 and 65, packed. */
static const uint8_t expect[BENCH_BASIC_STMTS][5] = {
    { 0x86, 0x34, 0x00, 0x00, 0x00 },
    { 0x86, 0x34, 0x00, 0x00, 0x00 },
    { 0x87, 0x02, 0x00, 0x00, 0x00 },
};

static int basic(void) {
    /* Three passes of the line, each checked when its count lands: the
     * sums, and the stack back where it started. */
    bench_basic_load(&b);
    for (uint16_t pass = 1; pass <= 3; pass++) {
        memset(b.m.ram + BENCH_BASIC_RESULT, 0xAA, 5 * BENCH_BASIC_STMTS);
        while (passes() < pass && b.m.cpu.cycles < 10000000u)
            m6502_step(&b.m);
        CHECK(passes() == pass, "pass %u never completed", pass);
        for (unsigned s = 0; s < BENCH_BASIC_STMTS; s++) {
            const uint8_t *r = b.m.ram + BENCH_BASIC_RESULT + 5 * s;
            CHECK(memcmp(r, expect[s], 5) == 0,
                  "pass %u statement %u: %02X %02X %02X %02X %02X", pass, s,
                  r[0], r[1], r[2], r[3], r[4]);
        }
        CHECK(b.m.cpu.s == 0xFF, "pass %u: S #%02X", pass, b.m.cpu.s);
    }
    uint64_t c_pass = b.m.cpu.cycles / 3;
    printf("basic: one pass of the line is %llu cycles, %.2f ms of a 1 MHz Oric\n",
           (unsigned long long)c_pass, c_pass / 1000.0);

    /* The timed run, as the board does it. */
    bench_basic_load(&b);
    uint64_t c = bench_run(&b, BENCH_RUN_CYCLES);
    CHECK(c >= BENCH_RUN_CYCLES && c < BENCH_RUN_CYCLES + 7, "ran %llu cycles",
          (unsigned long long)c);
    CHECK(passes() > 3, "%u passes", passes());
    CHECK(b.m.cpu.undoc_count == 0, "%u undocumented opcodes", b.m.cpu.undoc_count);
    report("basic", c);
    CHECK(c == BENCH_BASIC_RUN_CYCLES, "bench.h says %u cycles", BENCH_BASIC_RUN_CYCLES);
    CHECK(b.m.instructions == BENCH_BASIC_RUN_INSNS, "bench.h says %u instructions",
          BENCH_BASIC_RUN_INSNS);
    TEST_DONE();
}

static int dormann(const char *path) {
    static uint8_t image[ORIC_ADDR_SPACE];
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("SKIP: %s not found; run tools/fetch-test-suites.sh\n", path);
        return TEST_SKIP_CODE;
    }
    size_t n = fread(image, 1, sizeof image, f);
    fclose(f);
    CHECK(bench_dormann_load(&b, image, n), "%s: %zu bytes, not 64 KiB", path, n);

    uint64_t c = bench_run(&b, BENCH_RUN_CYCLES);
    report("dormann", c);
    /* Both outcomes of Dormann's test are a jump or branch to itself; one
     * step that leaves PC where it was is a trap, and the slice must end
     * before any. #0200 is the number of the test it is in. */
    uint16_t pc = b.m.cpu.pc;
    printf("dormann: in test #%02X at #%04X\n", b.m.ram[0x0200], pc);
    CHECK(b.m.cpu.undoc_count == 0, "%u undocumented opcodes", b.m.cpu.undoc_count);
    CHECK(c == BENCH_DORMANN_RUN_CYCLES, "bench.h says %u cycles", BENCH_DORMANN_RUN_CYCLES);
    CHECK(b.m.instructions == BENCH_DORMANN_RUN_INSNS, "bench.h says %u instructions",
          BENCH_DORMANN_RUN_INSNS);
    m6502_step(&b.m);
    CHECK(b.m.cpu.pc != pc, "trapped at #%04X within the slice", pc);
    TEST_DONE();
}

int main(int argc, char **argv) {
    return argc > 1 ? dormann(argv[1]) : basic();
}
