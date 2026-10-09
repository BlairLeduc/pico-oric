#!/usr/bin/env bash
# uart-hold.sh — park the guest, or let it go again (design.md §4.5,
# §15.2 M9).
#
#   tools/uart-log.sh 60 out/hold.log &
#   tools/uart-hold.sh        # parked: the card is checked, and again on a change
#   tools/uart-hold.sh        # resumed
#
# Sends GS (0x1D), which no key sends. Core 0 parks at the next field
# boundary and feeds the audio queue silence; core 1 mounts the card,
# reads the settings file, hashes the ROMs and logs what it found, and
# does so again whenever card detect settles at a new level. The next GS
# hands the machine back. Start uart-log.sh first: this only writes.
#
# Copied from pico-ace. RS (\036) opens the menu and US (\037) pauses,
# as Alt+M and Alt+P do; while either is up, uart-type.sh's bytes are the
# menu's keys, ^P ^N ^B ^F its arrows (park.c).
set -euo pipefail
UART_TYPE_DELAY=0 exec "$(dirname "$0")/uart-type.sh" '\035'
