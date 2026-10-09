#!/usr/bin/env bash
# perf-summary.sh — reduce perf-run.sh's logs to one line per workload
# (design.md §14, §15.2 M7).
#
#   tools/perf-summary.sh out/m7/t0 out/m7/t2 ...
#
# Averages the `perf` heartbeats taken after the program was started
# (perf-run.sh records where that was), dropping the first, which
# straddles the start. Core 0 busy is the share that counts: the guest,
# the keys and the snapshot, without the pacing wait. rt must read 1.000
# in every row; from M8, on a build with audio, so must the samples
# consumed a second read the PWM's rate, 36,621 Hz, whatever the guest
# does (EL §6.3), and the underrun samples and late refills are their
# growth over the windows. Presents and dropped snapshots are their
# growth over the averaged windows, from the last heartbeat before them;
# the longest present is since boot.
#
# Adapted from pico-ace's.
set -euo pipefail

# The capture opens with line noise from the reset, which is not UTF-8.
export LC_ALL=C

for dir in "$@"; do
    for log in "$dir"/*.log; do
        case "$log" in *.flash.log) continue ;; esac
        w="$(basename "$log" .log)"
        start="$(cat "$dir/$w.start" 2>/dev/null || echo 0)"
        awk -v dir="$dir" -v w="$w" -v start="$start" '
            function num(re, skip_l, skip_r) {
                if (!match($0, re)) return ""
                return substr($0, RSTART + skip_l, RLENGTH - skip_l - skip_r) + 0
            }
            / heartbeat / {
                p = num("[0-9]+ presents", 0, 9)
                d = num("[0-9]+ dropped", 0, 8)
                f = num("[0-9]+ full", 0, 5)
                if (p == "") next
                # Each heartbeat comes just before the perf line of its window,
                # so n is one behind here: n <= start is a window dropped.
                if (n <= start) { p0 = p; d0 = d; f0 = f; next }
                r = num("rt [0-9.]+", 3, 0)
                sr += r; kr++
                if (kr == 1 || r < rmin) rmin = r
                if (kr == 1 || r > rmax) rmax = r
                p1 = p; d1 = d; f1 = f
                mx = num("max [0-9]+ us", 4, 3)
            }
            / perf / {
                n++
                if (n <= start + 1) next
                b = num("busy [0-9.]+%", 5, 1)
                g = num("guest [0-9.]+%", 6, 1)
                c = num("[0-9.]+ host cycles", 0, 12)
                t = num("[0-9.]+ cycles/insn", 0, 11)
                i = num("[0-9]+ insns", 0, 6)
                s = num("snapshot [0-9.]+%", 9, 1)
                m = num("mode scan [0-9]+ us", 10, 3)
                if (b == "") next
                sb += b; sg += g; sc += c; st += t; si += i; ss += s; sm += m; k++
                if (k == 1 || b < bmin) bmin = b
                if (k == 1 || b > bmax) bmax = b
                if (m > mmax) mmax = m
            }
            / audio .*consumed/ {
                if (n <= start + 1) { u0 = num("underrun samples [0-9]+", 17, 0)
                                      l0 = num("late refills [0-9]+", 13, 0); next }
                hz = num("[0-9]+ Hz consumed", 0, 12)
                ev = num("AY events [0-9]+", 10, 0)
                u1 = num("underrun samples [0-9]+", 17, 0)
                l1 = num("late refills [0-9]+", 13, 0)
                sev += ev; ka++
                if (ka == 1 || hz < hmin) hmin = hz
                if (ka == 1 || hz > hmax) hmax = hz
            }
            END {
                if (!k) { printf "%-14s %-9s no steady heartbeats\n", dir, w; exit }
                printf "%-14s %-9s n=%d  busy %5.1f%% (%.1f-%.1f)  guest %5.1f%%  %6.1f host cyc/insn  %4.2f cyc/insn  %7.0f insns  snap %.2f%%  scan %.0f us (max %d)  rt %.3f-%.3f  presents +%d (full +%d, dropped +%d), max %d us\n",
                       dir, w, k, sb / k, bmin, bmax, sg / k, sc / k, st / k, si / k, ss / k,
                       sm / k, mmax, rmin, rmax, p1 - p0, f1 - f0, d1 - d0, mx
                if (ka)
                    printf "%-14s %-9s audio: %d-%d Hz consumed, underrun samples +%d, late refills +%d, AY events %.0f/s\n",
                           dir, w, hmin, hmax, u1 - u0, l1 - l0, sev / ka
            }' "$log"
    done
done
