#!/usr/bin/env bash
# perf-run.sh — design.md §14's workloads on the board, one boot each
# (§15.2 M7, M12).
#
#   tools/perf-run.sh build/pico/pico-oric.elf out/m7/t0        # every workload
#   tools/perf-run.sh build/pico/pico-oric.elf out/m7/t0 idle scroll
#
# Each workload is its own boot: capture UART1, flash, type a BASIC
# program at the guest over the same UART and run it, let it run, keep
# the log. The heartbeat's `perf` line is the measurement, its `heartbeat`
# line the presents; perf-summary.sh reduces the logs. The card must hold
# the machine's ROM.
#
# Measured in the mode that ships, with core 1 presenting while the guest
# runs. Until M8 the guest is paced on the microsecond timer, and the
# heartbeat's rt is the control: it must read 1.000 in every row. Every
# program was run on the host first (test/host's guest harness), and none
# returns to the prompt.
#
# Adapted from pico-ace's, which typed Forth.
set -euo pipefail

elf="${1:?usage: perf-run.sh ELF OUTDIR [WORKLOAD...]}"
outdir="${2:?usage: perf-run.sh ELF OUTDIR [WORKLOAD...]}"
shift 2
workloads=("$@")
[ ${#workloads[@]} -gt 0 ] || workloads=(idle compute scroll hires sound glyphs scroll60 glyphs60)

here="$(cd "$(dirname "$0")" && pwd)"
dwell="${PERF_DWELL:-45}"   # seconds each program runs: ~9 heartbeats

# One line per argument, each typed and entered on its own.
# glyphs rewrites the space's eight rows (#B500, the standard set at
# #B400) for ever, so every blank cell changes glyph (design.md §7.3).
# The 60 Hz ones put the mode attribute #18 (text, 60 Hz) in the status
# row's last cell, #BBA7, which nothing scrolls (§11.1).
program() {
    case "$1" in
        idle)     ;;
        compute)  printf '%s\n' '10 A=0:FOR I=1 TO 1000:A=A+I*2.5:NEXT:GOTO 10' RUN ;;
        scroll)   printf '%s\n' '10 PRINT "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789":GOTO 10' RUN ;;
        hires)    printf '%s\n' HIRES '10 FOR Y=0 TO 199:CURSET 0,Y,3:DRAW 239,0,2:NEXT:GOTO 10' RUN ;;
        sound)    printf '%s\n' '10 MUSIC 1,4,1,10:SOUND 4,20,10:PLAY 1,1,7,2000:GOTO 10' RUN ;;
        glyphs)   printf '%s\n' '10 FOR I=0 TO 7:POKE 46336+I,RND(1)*64:NEXT:GOTO 10' RUN ;;
        scroll60) printf '%s\n' 'POKE 48039,24' \
                      '10 PRINT "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789":GOTO 10' RUN ;;
        glyphs60) printf '%s\n' 'POKE 48039,24' \
                      '10 FOR I=0 TO 7:POKE 46336+I,RND(1)*64:NEXT:GOTO 10' RUN ;;
        *) echo "perf-run.sh: unknown workload $1" >&2; exit 2 ;;
    esac
}

mkdir -p "$outdir"

# A failed flash or type exits under set -e; the capture must not outlive
# it, or it holds the port and every later run finds it busy.
logger=
stop_logger() {
    [ -n "$logger" ] || return 0
    kill "$logger" 2>/dev/null || true
    wait "$logger" 2>/dev/null || true
    logger=
}
trap stop_logger EXIT

for w in "${workloads[@]}"; do
    program "$w" >/dev/null           # an unknown name fails before the flash
    log="$outdir/$w.log"
    # The last capture's reader can outlive its kill by a moment, and
    # uart-log.sh refuses a busy port: retry until one is running.
    rm -f "$log"
    for try in 1 2 3 4 5 6 7 8 9 10; do
        "$here/uart-log.sh" 0 "$log" 2>/dev/null &
        logger=$!
        for _ in 1 2 3 4 5 6 7 8 9 10; do
            sleep 0.5
            [ -e "$log" ] && break
            kill -0 "$logger" 2>/dev/null || break
        done
        if kill -0 "$logger" 2>/dev/null && [ -e "$log" ]; then break; fi
        kill "$logger" 2>/dev/null || true
        wait "$logger" 2>/dev/null || true
        logger=
        [ "$try" -lt 10 ] || { echo "perf-run.sh: no capture for $w" >&2; exit 1; }
    done
    "$here/flash.sh" "$elf" >"$outdir/$w.flash.log" 2>&1
    sleep 5                           # the card, then 2.5 s of the ROM's own boot
    while IFS= read -r line; do
        "$here/uart-type.sh" "$line\r"
        sleep 1
    done < <(program "$w")
    # Mark where the program started: heartbeats before this are typing.
    grep -ac ' perf ' "$log" >"$outdir/$w.start" || true
    sleep "$dwell"
    "$here/uart-screen.sh" >/dev/null 2>&1 || true
    sleep 1
    stop_logger
    echo "perf-run.sh: $w -> $log"
done
