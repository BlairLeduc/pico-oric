# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repository is

An **Oric-1 and Oric Atmos emulator for the ClockworkPi PicoCalc**, in C
against the Raspberry Pi Pico SDK, on RP2350 boards only. The guest is a 6502A
at 1 MHz with a 16 KiB BASIC ROM (1.0 or 1.1), 16 or 48 KiB of RAM, a 6522
VIA, an AY-3-8912 reached through the VIA, and a ULA drawing 240×224 in
colour with serial attributes.

**Status: M0–M4 are done** (`docs/design.md` §15): the skeleton, its
banner checked on a Plus 2 W; pico-atom's 6502 and VIA passing their tests
here, with Dormann's and Clark's suites; the gate, the 6502 at ~30 % of
core 0 on the board (§3.2: 150 MHz is enough; the SRAM tier is still
open); the Oric on the host, both ROMs booting on 16K and 48K, typed
into, and agreeing with Oricutron line for line (§13.4); and video on the
host, the goldens drawn identically by Oricutron's ULA (§7.7). The record of each milestone
(what was verified, on which board, on what date, and what was not checked)
is in `docs/milestones.md`. Add to it there.

**Settled decisions** (`docs/design.md` §18, 2026-10-07): **ROMs come from the
SD card, identified by SHA-1**, and none ships in the repository or the
firmware; the Microdisc is in scope as M14; ROM and RAM are independent
Machine-page rows, power-on default **Atmos 48K**; the licence is
**GPL-3.0**; one design document. Do not reopen these without the owner.
§18's P1–P4 are proposals the owner has not yet confirmed, and O1 is open.

## The documents

| File | Authority on |
|---|---|
| `docs/design.md` | the **guest** and the shape of this project's code: the Oric's hardware model, architecture, budgets, milestones, unverified constants |
| `docs/hardware-notes.md` | the **host**: PicoCalc wiring, protocols, timing, measured costs, quirks |
| `docs/emulator-lessons.md` | what pico-atom and pico-ace taught about writing any emulator on the PicoCalc |
| `docs/milestones.md` | what each milestone verified, on which board and date, and what it did not check |

Read `docs/design.md` before writing emulator code. Its §4.6 says which
sibling files to reuse, §15 says what each milestone builds and when it is
done, and §16 lists every guest fact that is not yet confirmed.

**Cross-references are load-bearing.** `design.md` writes `HW §N` for
hardware-notes, `EL §N` for emulator-lessons and plain `§N` for itself. Keep
the section numbers accurate when editing. `hardware-notes.md` and
`emulator-lessons.md` are **portable**: they travel between projects, so they
may cite each other but never `design.md`, this file or a source file. New
facts about the PicoCalc belong in `hardware-notes.md`, written so that they
stand alone.

**Notation:** `#XXXX` is a guest (Oric) address or value, in documents and
comments, as Oric BASIC writes hex. `0x` is a host value, and is what C code
uses. A *cycle* is a 6502 cycle at 1 MHz.

Documents are written in British spelling and in the plain, measured style of
the existing three: say what was checked, on what, and when.

## The siblings

`../pico-atom` (Acorn Atom) and `../pico-jupiter-ace` (Jupiter Ace) are
emulators for the same hardware, by the same author, under the same licence.
`design.md` §4.6 says which files to **copy and rename**, which to
**adapt**, and which are not used: the **6502 and the VIA from pico-atom**,
and the **port layer, tools and build from pico-ace**, the newer of the two.

- **Read the siblings, never edit them** from this project.
- Rename `PICO_ATOM_`/`PICO_ACE_` → `PICO_ORIC_`, `ATOM_`/`ACE_` → `ORIC_` and
  `atom_`/`ace_` → `oric_`. Change the `design.md §N` references in comments
  to this project's sections.
- **Bring the tests with it.** A copied module counts as reused only once its
  sibling's tests pass here under the new names.
- A copied file brings its `THIRD-PARTY.md` entry.

## Build and test

```sh
# host: src/core/ with the system compiler, no Pico SDK, under CTest
cmake -S . -B build/host -DPICO_ORIC_HOST=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host -j
tools/fetch-test-suites.sh           # Dormann's binary, and Clark's built with ca65
ctest --test-dir build/host --output-on-failure

# firmware: the newest SDK and toolchain under ~/.pico-sdk, unless
# PICO_SDK_PATH names one that exists
tools/build.sh                                          # build/pico/pico-oric.uf2
tools/build.sh -DPICO_ORIC_UART=OFF build/pico-release  # the build that ships
tools/build.sh -DPICO_ORIC_RAM_TIER=2 build/bench-t2    # one directory per tier

# hardware, with the Debug Probe's SWD and UART both connected, and the
# Mac's display kept awake (caffeinate -d): a sleeping display wedges the
# probe until it is replugged (hardware-notes.md §2.7)
tools/uart-log.sh 30 out/run.log &   # capture UART1 first, so the banner is in it
tools/flash.sh                       # reset halt + resume, never reset run (HW §2.7)
tools/flash.sh build/bench-t2/pico-oric-bench.elf   # M2's bench; its counts must say "= host"

# the trace diff against Oricutron (design.md §13.4), checkout in out/
git clone https://github.com/pete-gordon/oricutron.git out/oricutron
git -C out/oricutron checkout 002279fce9fa756d1d63cdc40ae97939eb7de7ed
tools/trace/build-oricutron.sh out/oricutron     # corrects its errata, by name
cmake --build build/host --target oric-trace
tools/trace-diff.py run --rom 1.0 --ram 16 --keys 'PRINT 2+2\n'
tools/render-diff.sh                             # the goldens, drawn by Oricutron's ULA (§7.7)
```

Both targets build under `-Wall -Wextra -Werror`, and CI builds both on every
push. Run the host build as well as the firmware one: the core building clean
without the SDK is what keeps SDK headers out of `src/core/`.

- **No ROM is committed.** `roms/` is a gitignored staging area (only its
  README is in git). Host tests find `basic10.rom`, `basic11b.rom` and
  `microdis.rom` there by SHA-1 and **skip** without them; CI has none, and
  runs a test ROM of our own instead (design.md §13.3). A skip is not a pass.
- **Our own test ROM** (`test/asm/oric_test_rom.s`) is assembled by CMake
  when ca65 is on the path at configure time; it is what proves the wiring
  in CI. `test/host/guest.c` types through the ROM's own key table, SHIFT
  a field before its key (design.md §16).
- **Test suites are fetched, not committed** (design.md §5.4). A missing
  suite makes its test exit 77, reported as skipped. **A skipped functional
  test is an unverified CPU.**
- **Tests use no framework**: `test/host/test_util.h` with `CHECK` and
  `TEST_DONE`, one binary per area. Assert behaviour by executing it rather
  than by comparing two tables in the repository, and give a timing test a
  control that must fail.
- **A golden image proves nothing until someone has looked at it.** View
  every changed image before committing it, and run `tools/render-diff.sh`:
  Oricutron must draw the same frame identically. `test_golden --write DIR`
  rewrites them.

## Architecture: the parts that are easy to break

- **`src/core/` never includes an SDK header and never allocates.** State
  lives in `oric_t` or in buffers sized in `src/core/config.h`, where every
  fixed capacity lives. `src/port/` is the only place SDK headers belong.
- **`oric_run` is the seam.** It runs whole instructions until at least the
  requested cycles have passed and returns the true count. The caller
  carries the overshoot as debt. Never add a "run exactly N" variant. The
  field's length is the ULA's 50 or 60 Hz choice, never assumed.
- **Copy a machine with `oric_copy`, never `=`.** The page table points into
  the struct.
- **`page_t` is exactly two pointers** (`read`, `write`, NULL for the slow
  path). Per-page flags go in a separate array. Page `#03` is I/O and always
  takes the slow path; decode there by mask, not equality.
- **Every AY access is a VIA sequence, and every keyboard scan is an AY
  write** (design.md §2.3). The AY is wired to the VIA's port A and CA2/CB2,
  not to the CPU's bus: CA2 = BC1, CB2 = BDIR, PB3 high is a key down.
- **The VIA's timing is cycle-exact against Oricutron** (design.md §5.3): an
  access to page `#03` brings the VIA to the access's cycle
  (`bus_read_at`/`m6502_t.io_at`, slow path only), and the VIA runs two
  cycles behind the CPU at boundaries so the IRQ poll is the 6502's. Run the
  trace diff after touching either.
- **There is no framebuffer, and no VRAM-byte diff.** A serial attribute
  changes every cell to its right without changing their bytes; the
  presenter's shadow holds **decoded cells** (design.md §7.3).

## Hardware invariants

From `hardware-notes.md` §10. Each one cost real debugging time on a sibling:

- `spi_set_format()` and the `D/CX` write go **before** CS low (the 40 ns
  CS-high rule).
- Never touch the LCD from an interrupt handler, and never mask interrupts
  around a blit. Audio has a ~3.5 ms refill deadline.
- **Core 1 never calls `sleep_us`/`sleep_ms`**: their alarm IRQ runs on
  core 0, in the middle of the guest. Use `busy_wait_us_32` (HW §9.7).
- **Core 0 never calls `printf` once the guest runs.** It logs into a ring
  that core 1 drains.
- No flash writes: settings go to a text file on the card, written through
  `.new` and a rename.
- `main()` sets the core rail explicitly, because it survives a reset
  (HW §3).
- Build each SRAM tier in its own build directory and check its symbols with
  `arm-none-eabi-nm` (HW §9.8).

## Unverified constants

`docs/design.md` §16 lists every Oric fact written from secondary knowledge,
with a confidence for each. **Do not treat the design as authoritative for a
low or medium row.** Settle the row from a primary source, preferably by
**executing the real ROMs** on the host harness. Record in §16 how and when
it was settled, then make it a `#define`. A timing constant stays runtime
configuration until it is settled.

## Measurement discipline

Every performance figure in `design.md` is an estimate until a measurement
replaces it. Keep the estimate next to the measurement. Measure each feature
against a control build in the same sitting, and write results to a file.
"Built" and "done" are different words: a milestone with a device step is
done only once it has been checked on the device.

## Comments

Comments cite the document next to any decision that looks arbitrary, e.g.
`(design.md §7.3)` or `(hardware-notes.md §4.3)`, rather than restating the
reasoning. A device hook that does not exist yet is marked where its call
belongs, with a comment naming the milestone (`/* M13: ... */`).
