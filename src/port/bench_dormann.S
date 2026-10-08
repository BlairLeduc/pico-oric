/* bench_dormann.S — Dormann's functional test, as fetched by
 * tools/fetch-test-suites.sh, in the bench firmware's flash
 * (design.md §15.2 M2). Assembled only when the file is there; it is
 * never committed (design.md §5.4). */
    .section .rodata.bench_dormann, "a"
    .global bench_dormann
    .global bench_dormann_end
bench_dormann:
    .incbin PICO_ORIC_DORMANN_PATH
bench_dormann_end:
