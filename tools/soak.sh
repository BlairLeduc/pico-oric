#!/usr/bin/env bash
# soak.sh — design.md §13.5's soak: 30 minutes on battery with a BASIC
# program that draws text with serial attributes and hires, plays the
# AY's three channels with noise and an envelope, and reads the keyboard
# through the ROM's matrix scan, while keys are typed over the UART;
# UART1 captured throughout, then soak-check.py over the log (§15 M12).
# Copied from pico-ace, with the Oric's program.
#
#   tools/soak.sh build/pico/pico-oric.elf              # 30 minutes
#   tools/soak.sh build/pico/pico-oric.elf 45 out/m12/soak
#
# Before running: the Debug Probe's SWD and UART connected, the Mac's
# display kept awake (caffeinate -d, hardware-notes.md §2.7), the USB-C
# power lead out so the PicoCalc runs on its batteries, and the power
# switch on. soak-check.py fails a run whose gauge shows charging, which
# is USB power; one that never shows it may still be USB with the charge
# finished, so say which when recording it.
#
# The program is tools/soak.bas, run first on the host by test_soak. It
# prints "C <count> K <key>" twice a pass, the key the last KEY$ read
# (72 for H, 74 for J). This script types H and J at it every few
# seconds through the firmware's key path (keymatrix), and asks for the
# screen now and then, so the log shows the program reading them. The
# southbridge is polled for the real keyboard all the while, which is
# what the I2C error count covers; press a few keys on it during the run
# as well, but not Ctrl+C, which stops the program.
#
# The machine is the build's boot machine, Atmos 48K unless the card's
# settings say otherwise; soak.bas is 1.1 BASIC.
set -euo pipefail

elf="${1:?usage: soak.sh ELF [MINUTES] [OUTDIR]}"
minutes="${2:-30}"
outdir="${3:-out/soak}"
here="$(cd "$(dirname "$0")" && pwd)"
every="${SOAK_KEY_EVERY:-5}"     # seconds between typed keys

mkdir -p "$outdir"
stamp="$(date +%Y%m%d-%H%M%S)"
log="$outdir/soak-$stamp.log"

logger=
stop_logger() {
    [ -n "$logger" ] || return 0
    kill "$logger" 2>/dev/null || true
    wait "$logger" 2>/dev/null || true
    logger=
}
trap stop_logger EXIT

# Capture first, so the banner is in the log (CLAUDE.md).
"$here/uart-log.sh" 0 "$log" &
logger=$!
for _ in $(seq 1 20); do
    sleep 0.5
    [ -e "$log" ] && break
    kill -0 "$logger" 2>/dev/null || { echo "soak.sh: the capture did not start" >&2; exit 1; }
done

"$here/flash.sh" "$elf" >"$outdir/soak-$stamp.flash.log" 2>&1
sleep 5                          # boot to Ready, with or without a card
# One line at a time: the ROM loses keys that arrive while it stores
# the line before (test_soak).
while IFS= read -r line; do
    [ -n "$line" ] || continue
    "$here/uart-type.sh" "$line\r"
    sleep 1
done <"$here/soak.bas"
"$here/uart-type.sh" 'RUN\r'
sleep 2
# Where the program started, for soak-check.py: heartbeats before this
# are boot and typing.
grep -ac 'heartbeat' "$log" >"$outdir/soak-$stamp.start" || true
start=$(date +%s)
echo "soak.sh: running $minutes minutes from $(date +%H:%M:%S) -> $log"

end=$((start + minutes * 60))
n=0
while [ "$(date +%s)" -lt "$end" ]; do
    sleep "$every"
    if [ $((n % 2)) -eq 0 ]; then "$here/uart-type.sh" 'H'; else "$here/uart-type.sh" 'J'; fi
    n=$((n + 1))
    # The screen about once a minute, a few seconds after a key, so that
    # a pass has printed it; every 11th key, an odd count, so the dumps
    # follow H and J in turn.
    if [ $((n % 11)) -eq 0 ]; then
        sleep 3
        "$here/uart-screen.sh"
    fi
done
sleep 10                         # a last heartbeat after the last key
stop_logger

echo "soak.sh: $n keys typed"
"$here/soak-check.py" --minutes "$minutes" "$log" | tee "$outdir/soak-$stamp.txt"
