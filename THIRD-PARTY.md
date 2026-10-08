# Third-party material

Everything in this repository is this project's own work under the GPL-3.0 in
[`LICENSE`](LICENSE), with the exceptions below.

## The Oric's ROMs

Not in the tree, and not in the firmware (`docs/design.md` §10.2, §18 item 1).
`roms/` holds only a README; the emulator reads the ROMs from the SD card and
identifies them by SHA-1.

## Pico SDK import script — Raspberry Pi

`cmake/pico_sdk_import.cmake` is the Raspberry Pi Pico SDK's own
`external/pico_sdk_import.cmake`, copied as the SDK's instructions direct,
under the SDK's BSD-3-Clause licence.

## Klaus Dormann's 6502 functional tests

Not in the tree. `tools/fetch-test-suites.sh` downloads the assembled binary
from <https://github.com/Klaus2m5/6502_65C02_functional_tests> into
`test/suites/`, which is gitignored; `test_m6502_functional` and
`test_bench_dormann` report as *skipped* when it is absent. A
`pico-oric-bench` image built while it is there carries it in flash
(`src/port/bench_dormann.S`), as this project's own build of a GPL-3.0 work;
that image is for measurement and is not released. The suite is Klaus
Dormann's, published under the GPL-3.0.

## Bruce Clark's decimal mode test

`test/asm/6502_decimal_test.s`, copied from pico-atom, is Bruce Clark's test
of the 6502's decimal mode (<http://www.6502.org/tutorials/decimal_mode.html>),
which he placed in the **public domain**, as translated to ca65 from Klaus
Dormann's copy. Its header records the changes.
