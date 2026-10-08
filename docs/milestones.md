# Milestone log

What each milestone verified, on which board, on what date, and what was
not checked, newest first. `design.md` §15.2 holds each milestone's scope
and done-when criteria; this file keeps the full record.

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
UART; 20,932 and 844 without. **Not checked:** the banner in a UART capture
from a board (no Debug Probe attached); CI on GitHub.
