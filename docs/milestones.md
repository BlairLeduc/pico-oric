# Milestone log

What each milestone verified, on which board, on what date, and what was
not checked, newest first. `design.md` §15.2 holds each milestone's scope
and done-when criteria; this file keeps the full record.

**M4, video on the host** (`src/core/ula.*`, `snappool.*`, `font.*`,
`font_fallback.c`, `oric.*`; `test_ula`, `test_snappool`, `test_font`,
`test_golden`, `test_field`; `test/host/golden/`; `tools/mkfont.py`,
`tools/render-diff.sh`, `tools/trace/oricutron-render.c`), built
2026-10-08 on the workstation (macOS, Apple clang, Debug and Release). The
decode turns the window `#9800–#BFFF`, the mode at the field's start and
the blink phase into a cell per byte (ink, paper, six pattern bits); the
row generator draws cells as RGB565; the presenter's shadow holds decoded
cells. `oric_run_field` runs the mode scan at each field's end and takes the
next field's length from it; `oric_video_take` fills a frame for the pool,
which is pico-ace's, renamed. The rules follow Oricutron's `ula.c`, and
MAME's `oric.cpp` read at `e4c1c2b` agrees with it in everything but the
blink period (§16). **Checked:** `test_ula` decodes a window built for
each rule (colour and text attributes from their own cell, the line reset,
inverse on attribute cells too, both sets, double height on even and odd
rows, blink in both phases, hires pixels including `#20–#3F`, the text
window's `#9800` set, a mode attribute moving the fetch from the next
cell and lasting into the next field); the scan agrees with the decode on
2,000 random screens, where a scan of the text screen alone, the control,
is wrong on 31; 400 random edits (bytes, attributes, mode attributes,
character-set rows, the blink phase, the start mode) presented from the
dirty bands alone match a full render after each, and the VRAM-byte diff,
the control, leaves stale pixels. `test_snappool`: pico-ace's 100,000
random transitions, and frames taken on 48K and on 16K through the mirror.
`test_font`: each ROM's expanded table equals what it wrote to
`#B500–#B7FF`, on both fits, and fails one glyph out of place (the two
ROMs' fonts are identical, so the other ROM's could not be the control).
`test_field`: a 60 Hz mode attribute written to the screen leaves its own
field at 19,968 cycles and makes the next 16,896. **The goldens:** nine
scenes (colours, inverse, both sets, double height, blink in both phases,
hires with its text window, a mode change at row 10, col 20, and the 16K
mirror against the colours image), drawn with the fallback font so that
CI runs them and no image holds a ROM's glyphs. Each was viewed, as a PNG
at 3×, before it was committed; the first `mode_split` was wrong (its
drawing overwrote the text character set at `#B400`, which is the
bitmap's lines 128–175) and was redrawn. `oricutron-render`, Oricutron's
own `ula_doraster` behind a headless driver, draws all nine frames
identically to the goldens, pixel for pixel (`tools/render-diff.sh`).
**Planted bugs**, each caught and removed: attribute cells never inverted
(Brown's rule; `test_ula`, `test_golden`, and 1,056 pixels of `inverse`
against Oricutron); double height's halves swapped; a mode attribute
moving the fetch only from the next line; a band's span taken from its
first line only (`test_ula`); the scan's skip not reset after a walked
line; the scan's word test checking 32 of 40 bytes, and matching text
attributes in place of mode ones; blink hidden in the shown phase. A word
test that matched a superset of mode attributes was not caught, rightly:
it only walks more lines. **The trace diff** still agrees to the end on
all four machines, with M3's counts. **Measured** (`out/m4-video-cost.txt`,
M1 Pro, per frame): Release, decode 10–17 µs, rows 9 µs, the mode scan
1.1 µs on a BASIC text screen, 0.56 µs on hires and 14 µs on a random
screen of mode attributes; Debug, decode 44–64 µs, rows 68–204 µs. The
scan's word test was kept against a control build in the same sitting:
without it the scan took 4.7, 28.5 and 23 µs. **Not checked:** anything
on the board, where the port does not use the ULA yet (M7); the panel's
byte order; any rule against hardware, rather than against two emulators;
the blink period (configuration, 32 fields); the first active line; the
emulator's text pages, which arrive with M9; the frame's status bytes
(M7). The owner has not yet looked at the goldens. CI green for PR #4 on
both jobs, `test_golden` among the passes and `test_font` skipped (no ROMs
in CI); its first run failed on `clock_gettime`, which strict C11 hides on
glibc, and `test_ula` now times with `timespec_get`.

**M3, the Oric on the host** (`src/core/ay8912.*`, `romset.*`, `sha1.*`,
`oric.*`, `bus.*`; `test/host/guest.*`, `test_boot`, `test_ay8912`,
`test_field`, `test_romset`, `test_test_rom`; `test/asm/oric_test_rom.s`;
`tools/trace/`, `tools/trace-diff.py`), built 2026-10-08 on the
workstation (macOS, Apple clang, Debug), with the owner's `basic10.rom`
and `basic11b.rom` (§10.2's hashes). `sha1` copied from pico-ace; `romset`
after pico-atom's, by SHA-1. The VIA wired as both ROMs drive it, read
from their disassembly first: CA2 = BC1, CB2 = BDIR; AY port A's zero bits
enable keyboard columns, PB0–PB2 pick the row, PB3 high is a key down. The
AY as a register file behind its bus (no sound). `oric_power_on`,
`oric_nmi`, `oric_key_set`, `oric_run_field` with the line and field
lengths in `oric_config_t`. The harness types through each ROM's own key
table (`#FF70`, `#FF78`). **Checked by executing the ROMs** (`test_boot`):
both reach Ready on 16K and 48K with their banners and 15,102, 47,870,
4,863 and 37,631 bytes free; `PRINT 2+2` typed into the matrix prints 4 on
all four; T1 is free-running at latch 10,000 and the handler runs 99–100
times a second; RND gives the same three values after every power-on; NMI
warm-starts and keeps a program, RESET cold-starts; no undocumented opcode
runs. The control, a 16K machine with nothing above `#3FFF`, stops both
ROMs in their RAM test. §16 is updated with each of these.
**The trace diff:** Oricutron at `002279f` (2026-01-23), built by
`tools/trace/build-oricutron.sh` with a hook in its 6502 step and a
headless driver. It first parted from us at T1's first interrupt, and that
found two bugs in our core, both fixed: (1) the VIA was ticked through an
instruction after it ran, so a write on its last cycle started T1 three
cycles early. Now the 6502 records which cycle a page-`#03` access falls
on, on the slow path only, and the VIA is brought up to it first. (2) The
IRQ was polled with the VIA at the instruction's end; a 6502 decides at
the end of the penultimate cycle, so the VIA now runs two cycles behind at
boundaries (§5.3). It also found two errata in Oricutron, corrected by
name in its build: `ORICUTRON_NO_I_DELAY` and `ORICUTRON_BRANCH_PAGE`
(§13.4). After that, boot and `PRINT 2+2` agree line for line, cycles
included, after the reset's S and P: 2,093,293, 2,169,408, 2,132,110 and
2,144,512 instructions on 1.0 16K, 1.0 48K, 1.1 16K and 1.1 48K
(`out/m3-trace.log`). **The test ROM**, assembled by CMake when ca65 is
present, runs in CI: RAM size by aliasing, AY registers written and read
back through the VIA, a full keyboard scan, text, font and hires bytes,
100 T1 interrupts and PB7 edges a second, one NMI per press. **Planted
bugs**, each in a clean rebuild (one first run gave false results because
a source edited within the build's second was not recompiled):
CA2/CB2 swapped fails `test_ay8912`, `test_boot` and `test_test_rom`;
PB3 inverted fails the same three; no 16K mirror fails `test_boot` and
`test_test_rom`; NMI never raised fails `test_boot` and `test_test_rom`;
the field's debt dropped fails `test_field`, whose control (no debt)
drifts as it should. **Measured:** power-on to Ready, to within 100
cycles: 1,075,974 (1.0 16K), 2,846,747 (1.0 48K), 886,330 (1.1 16K),
2,460,543 (1.1 48K). **Decided:** the VIA stays ticked per instruction, as
M2 measured it, with the catch-up above, rather than slicing at its next
event (M1's open question). **Not checked:** the schematic, for the
mirror, the VIA's decode and the reset lines (the ROMs reach the VIA only
at `#0300–#030F`); the run loop's added subtraction on the board (the
bench's host counts are unchanged; M7 measures the whole machine); a taken
branch that does not cross a page polls IRQ a cycle early on a 6502, which
neither emulator models and no trace has shown; `ORICUTRON_VIA_AHEAD`,
which boot never exposes; real key timing and FUNCT (M5); anything
visible (M4). ROM 1.0 takes a key on its first scan, so the harness holds
SHIFT a field before the key; a SHIFT pressed in the same instant turned
`(` into `9`. M5's replay must do the same. **CI** green for PR #3 on
2026-10-08: ca65 assembled the test ROM and `test_test_rom` passed;
`test_boot` reported skipped, CI having no ROMs.

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
