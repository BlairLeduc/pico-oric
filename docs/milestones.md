# Milestone log

What each milestone verified, on which board, on what date, and what was
not checked, newest first. `design.md` §15.2 holds each milestone's scope
and done-when criteria; this file keeps the full record.

**M2, the 6502 on the board: the gate** (`src/bench/`,
`src/port/bench_main.c`, `bench_dormann.S`; `test_bench`), 2026-10-07, on
the Plus 2 W (id `7458DC82A89AAC12`, RP2350B rev 2) at 150 MHz, SDK 2.3.1,
gcc 15.2 `-O3`. pico-ace's bench shape: `oric_t` with every page plain
RAM, run by the real `oric_run` (the VIA ticked per instruction) in
20,000-cycle slices with the overshoot carried as debt, for 10,000,000
cycles a run. Two workloads. **basic** is MS BASIC's CHRGET at `#E2`, with
TXTPTR at `#E9`, walking a line of three sums, and an FADD on unpacked
accumulators (zero, swap, bitwise alignment, 32-bit add, renormalise), put
together by a small assembler in `bench.c`. **dormann** is the first 10 M
cycles of Dormann's test, embedded when fetched. **Checked on the host:**
basic's three sums come out as the packed floats 45, 45 and 65 on each of
three passes, with S back at `#FF`. A planted bug, the renormalising shift
without the exponent increment, fails it (`84 3C…` for 45). Dormann is in
test `#29` at the slice's end, not trapped. Neither workload runs an
undocumented opcode. One run counts 10,000,001 cycles and 3,076,774
instructions (basic), and 10,000,002 and 3,196,835 (dormann), and `bench.h`
records those. **On the board** the same counts came back on every pass of
every image. Each image ran 7–9 passes, spread under 0.02 %
(`out/m2-bench-t{0,1,2}.log`), in host cycles per guest instruction:

| SRAM tier | basic | dormann | core 0 at 1 MHz |
|---|---|---|---|
| 0, flash | 142.3 | 140.9 | 29.2 %, 30.0 % |
| 1, callees (1,818 B) | 154.7 | 147.6 | 31.7 %, 31.5 % |
| 2, + interpreter (24,084 B) | 144.7 | 138.8 | 29.7 %, 29.6 % |

Mean cycles per instruction were 3.250 and 3.128. **Gate decision: 150 MHz
is enough**, at a projected ~30–35 % of core 0 for the whole machine
(design.md §3.2). **The tier is not decided:** tier 1 is a loss here,
because the flash interpreter reaches its SRAM callees through veneers, and
tier 2 is level with flash. The bench has the XIP cache to itself, which is
what HW §9.8 says hides placement, so the default stays at tier 0 until M7
and M12 measure the whole machine. Tier 2's image calls three cold functions
in flash through veneers: `oric_reset`, and the VIA's `via6522_sync` and
`due`, both reached from a VIA register read. None runs in the bench; the
VIA's two will once M3 has I/O. Image sizes: 117,396 B text (64 KiB of it
Dormann's image) and 85,196 B bss; `pico-oric` is unchanged at 22,764 and
860. **Not checked:** the ROM's own mix (M3); core 1 sharing the cache;
the VIA's cost apart from the CPU's; the `-DPICO_ORIC_UART=OFF` bench on the
board (it builds). CI green on both jobs for PR #2, where both bench tests
ran and passed: the pinned counts hold under Ubuntu's gcc as well as Apple
clang.
**Review fix** (Codex's review of PR #2): `bench_run` asked every slice for
a near-full 20,000 cycles, so a length that is not a whole number of
slices overshot by up to a slice (25,000 ran 40,001). The last slice now
asks only for what is left; `test_bench` checks six such lengths, which
failed before the change. The 10 M-cycle runs, a whole number of slices,
count the same, so the board's figures stand.

**M1, review fixes** (Codex's review of PR #1, 2026-10-07). Three
findings, all taken. (1) **Delayed IRQ poll**: CLI, SEI and PLP change I
after the 6502 has polled IRQ for the next instruction, so that poll sees
the old I; RTI's takes effect at once. pico-atom tests I directly and so
takes an IRQ one instruction early after CLI or PLP, and misses one that
arrives during SEI. The CPU now keeps the old I for one instruction
(`i_old`, `i_old_at`), asked only while an IRQ is asserted.
`test_m6502_behaviour` gained a case for each of the four, which failed
before the change (all but RTI's) and pass after it; Dormann and Clark
still pass. pico-atom has the same gap, not fixed there (read the
siblings, never edit them). M11's snapshot must carry the two fields. (2)
**`PICO_ORIC_RAM_TIER` reached only the executable**, not the core
library where the marked functions are, so a tier did nothing; a tier-2
build now shows `time_critical.oric_m6502_step` and the rest in the core.
(3) **`oric_copy`** chose ROM or RAM by comparing pointers into different
arrays; it now uses the page's `PAGE_ROM` flag. The new `test_bus` checks
that every page of a copy points into the copy, with a plain struct copy
as the control (446 stray pointers).

**M1, the 6502 and the VIA on the host** (`src/core/m6502.*`,
`via6522.*`, `oric.*`, `bus.*`; `tools/fetch-test-suites.sh`;
`test/asm/6502_decimal_test.s`), built 2026-10-07 on the workstation
(macOS, Apple clang, Debug). pico-atom's `m6502` and `via6522` copied whole
and renamed, with their tests: `test_m6502_behaviour`, `_cycles`,
`_decimal`, `_functional` and `test_via6522`. They run on a minimal
`oric_t`: the page table over 64 KiB of RAM and a separate 16 KiB ROM
socket, page `#03` decoded to the VIA by mask, RESET, `oric_run` with
debt, `oric_copy`. The 16K mirror is mapped as §6.2 believes it, untested
until M3 settles it. **Checked:** Dormann's functional test runs to its
success trap at `#3469` after 96,241,367 cycles; Clark's decimal test ends
with error byte 0, every flag checked; the cycle table passes by execution;
`test_via6522` passes with its machine checks moved from `#B800` to
`#0300`; without the suites, `test_m6502_functional` reports skipped.
**Planted bugs**, each caught and then removed: the decimal high-nibble
fixup at 10 instead of 9 fails `test_m6502_decimal` and Dormann; BPL's base
count 3 instead of 2 fails `test_m6502_cycles`; T1's free-run period
latch + 1 instead of latch + 2 fails `test_via6522`. The core also
compiles clean under `arm-none-eabi-gcc -Werror` in the firmware build.
**Measured:** `test_m6502_functional`, both suites, 1.13 s wall time in a
Debug build (a regression marker, not a performance figure). **Left out,
from pico-atom's tests:** the MOS's printer checks (an Atom matter); the VIA
surviving a snapshot, which returns with `snapshot.c` in M11; the "VIA not
fitted" case, as every Oric has one. **For M3 to decide:** the VIA is
ticked per instruction, as pico-atom measured it (a countdown, §3.2),
where §5.3 speaks of stopping a slice at the VIA's next event. CI green
for PR #1, where `test_m6502_functional` ran and passed (cc65 from
Ubuntu's packages).

**M0, skeleton**, built 2026-10-07 on the workstation (macOS, Apple clang;
Pico SDK 2.3.1, arm-none-eabi-gcc 15.2). The layout of design.md §4.1 with
what M0 needs and no emulation: CMake with a host build
(`-DPICO_ORIC_HOST=ON`, CTest) and a `pico2` firmware build, both under
`-Wall -Wextra -Werror`; `src/core/config.h` and `hot.h`;
`test/host/test_util.h` and `test_skeleton`; the version header from
`git describe` at build time; `arm-none-eabi-size` on every build; CI
building both targets and the no-UART image, and running CTest. Copied from
pico-ace's M0 and renamed: `cmake/`, `board.*`, `hot.h`, `test_util.h`,
`tools/build.sh`, `flash.sh`, `uart-log.sh`, `ci.yml`. The firmware prints a
banner and a heartbeat over UART1 and blinks GP25. **Checked:** both builds
green; `test_skeleton` passes; an `#include "pico/stdlib.h"` planted in
`config.h` fails the host build (`'pico/stdlib.h' file not found`), and was
removed. **Measured:** the image, 22,764 bytes text and 860 bss with the
UART; 20,932 and 844 without. **On the board**, a Plus 2 W (id `7458DC82A89AAC12`,
RP2350B, chip rev 2), 2026-10-07: flashed over SWD with `tools/flash.sh`
and captured with `tools/uart-log.sh` (`out/m0.log`), the banner showed
build target `pico2`, clk_sys and clk_peri at 150 MHz, core rail ~1100 mV,
firmware `e3472e4`, then heartbeats a second apart at die 20 °C. A pico2
image cannot light the LED on a W board. CI green on both jobs for PR #1.
