#!/usr/bin/env python3
"""soak-check.py — hold a soak log to design.md §13.5.

    tools/soak-check.py [--minutes 30] out/soak/soak-YYYYMMDD-HHMMSS.log

Passes when the log shows one boot, heartbeats covering the whole run with
no gap, and, on every heartbeat:

  rt at least 0.995, with the run's mean at least 0.999;

and, since every counter is cumulative from boot, these zero on the last:

  late fields, slips, dropped snapshots, keys lost, i2c errors,
  undocumented opcodes, log lines dropped, underrun samples, late
  refills, the core's sample overflow;

and these unchanged from the program's start to the end: the card's
changes, and the parks (a park pauses the guest, and the soak is of the
guest running).

rt is guest seconds per wall second over a heartbeat's five seconds. The
guest is paced by the audio queue (design.md §8), so it reads 1.000 or
0.999 with the last digit truncated; a guest falling behind shows as a
run of lower figures, which the floor catches.

Presents, keyboard polls and AY writes must grow: a soak that exercised
nothing proves nothing. The workload must also run to the end: after the
program starts (soak.sh writes how many heartbeats came before, beside
the log), the AY must be written in every heartbeat, and the count
tools/soak.bas prints ("C<count> K<key>", the ROM putting no space before a positive number) must rise from each screen
dump that shows it to the next. A dump taken while a pass draws its text
page has no such line, so half of them may lack it. Keys typed over the
UART go to the guest's matrix without passing the southbridge, so they
are not key events; the screen dumps must show the program reading both
of them instead (K 72 for H, K 74 for J). Copied from pico-ace, with the
Oric's heartbeat and program.
Keys pressed on the PicoCalc during the run are reported, not required.
The consumed rate, the control quantity, is reported.

Power: each heartbeat carries the southbridge's gauge, and the run's
charge and charging bit are reported. The power source is not a
condition (design.md §13.5): battery and USB power are not told apart.
"""

import argparse
import re
import sys

HB = re.compile(
    r"heartbeat\s*: (\d+) fields \(\d+ at 60 Hz\), rt (\d+)\.(\d+), late (\d+) \(\+\d+\), "
    r"slips (\d+) \| (\d+) presents \((\d+) full, (\d+) dropped\).*?\| keys (\d+) \((\d+) lost\), "
    r"polls (\d+) \(longest \d+ us\), i2c errors (\d+) \| undoc (\d+) \(last [^)]*\), "
    r"log dropped (\d+), battery (\?|(\d+)%?( charging)?), (\?|-?\d+ C), "
    r"card \w+ \((\d+) changes\).*?parks (\d+)")
AU = re.compile(
    r"audio\s*: (\d+) Hz consumed, queue \d+ \(low (\d+)\), underrun samples (\d+) \(\+\d+\), "
    r"late refills (\d+) \(\+\d+\), core overflow (\d+), AY events \d+/s, AY writes (\d+)")
PERF = re.compile(r"perf\s*:.*?core 0 busy ([0-9.]+)%")
SCREEN_ROW = re.compile(r"^\s*\|.*?C ?(\d+) ?K ?(\d+)\b.*\|$", re.M)
FIELD_HZ = 1000000 / 19968          # the 50 Hz field (design.md §11.1)


def _start(log, text):
    """Heartbeats before the program started, as soak.sh wrote them beside
    the log; without the record, those before the first AY write."""
    try:
        return int(open(re.sub(r"\.log$", "", log) + ".start").read())
    except (OSError, ValueError):
        hb_pos = [m.start() for m in HB.finditer(text)]
        au = [(m.start(), int(m.group(6))) for m in AU.finditer(text)]
        i = next((k for k, (_, w) in enumerate(au) if w), len(au) - 1)
        return sum(1 for p in hb_pos if p < au[i][0])


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("log")
    ap.add_argument("--minutes", type=float, default=30)
    a = ap.parse_args()

    text = open(a.log, errors="replace").read()
    hbs = [m.groups() for m in HB.finditer(text)]
    aus = [m.groups() for m in AU.finditer(text)]
    fails = []

    boots = len(re.findall(r"firmware\s*:", text))
    if boots != 1:
        fails.append("%d boots in the log, not 1 (a reset during the run?)" % boots)
    if len(hbs) < 2 or not aus:
        print("FAIL: %d heartbeats in %s, too few to check" % (len(hbs), a.log))
        return 1
    last_hb = list(HB.finditer(text))[-1].start()
    if list(AU.finditer(text))[-1].start() < last_hb:
        fails.append("the last heartbeat has no audio line after it, so the "
                     "audio counters do not cover the end of the run")

    # Heartbeats come every five wall seconds; a capture can garble one,
    # which then fails to parse. The field count between two that did
    # parse says how many fell between them.
    fields = [int(h[0]) for h in hbs]
    span = (fields[-1] - fields[0]) / FIELD_HZ / 60
    if span < a.minutes:
        fails.append("heartbeats span %.1f minutes, less than %g" % (span, a.minutes))
    step = 5 * FIELD_HZ
    gaps, garbled = [], 0
    for x, y in zip(fields, fields[1:]):
        k = round((y - x) / step)
        if k < 1 or abs((y - x) - k * step) > 0.02 * step * k:
            gaps.append((x, y))
        else:
            garbled += k - 1
    if gaps:
        fails.append("%d irregular steps between heartbeats, first after field %d"
                     % (len(gaps), gaps[0][0]))
    if garbled > 2:
        fails.append("%d heartbeats missing from the capture" % garbled)

    rts = [int(h[1]) + int(h[2]) / 1000 for h in hbs[1:]]   # the first straddles the boot
    low = min(rts)
    mean = sum(rts) / len(rts)
    if low < 0.995:
        fails.append("rt fell to %.3f (heartbeat %d)" % (low, rts.index(low) + 1))
    if mean < 0.999:
        fails.append("mean rt %.4f" % mean)

    h, u = hbs[-1], aus[-1]
    zero = [
        ("late fields", int(h[3])),
        ("slips", int(h[4])),
        ("dropped snapshots", int(h[7])),
        ("keys lost", int(h[9])),
        ("i2c errors", int(h[11])),
        ("undocumented opcodes", int(h[12])),
        ("log lines dropped", int(h[13])),
        ("underrun samples", int(u[2])),
        ("late refills", int(u[3])),
        ("core sample overflow", int(u[4])),
    ]
    for name, v in zero:
        if v:
            fails.append("%s: %d" % (name, v))

    grew = [
        ("presents", int(hbs[0][5]), int(h[5])),
        ("keyboard polls", int(hbs[0][10]), int(h[10])),
        ("AY writes", int(aus[0][5]), int(u[5])),
    ]
    for name, first, final in grew:
        if final <= first:
            fails.append("%s did not grow (%d to %d): not exercised" % (name, first, final))

    # The guest must run throughout: no park, no card change, after the
    # program started.
    hb_start = hbs[min(_start(a.log, text), len(hbs) - 1)]
    for name, i in (("card changes", 18), ("parks", 19)):
        if int(h[i]) != int(hb_start[i]):
            fails.append("%s: %s at the program's start, %s at the end"
                         % (name, hb_start[i], h[i]))

    # The workload must run for the whole soak, not only at its start:
    # from the heartbeat after the program was started (soak.sh records
    # how many came before; without the record, the first with an edge),
    # the speaker must move in every heartbeat's window, and each screen
    # dump must show the program's count higher than the last one did.
    hb_pos = [m.start() for m in HB.finditer(text)]
    au_pos = [(m.start(), int(m.group(6))) for m in AU.finditer(text)]
    start = _start(a.log, text)
    begin = hb_pos[min(start + 1, len(hb_pos) - 1)]
    edges = [e for p, e in au_pos if p > begin]
    still = sum(1 for x, y in zip(edges, edges[1:]) if y <= x)
    if len(edges) < 2:
        fails.append("no audio lines after the program started")
    elif still:
        fails.append("the AY was not written in %d of %d heartbeats after the "
                     "program started: it stopped" % (still, len(edges) - 1))
    dumps = [d for d in re.split(r"screen\s*:", text[begin:])[1:]]
    all_counts = [max((int(m.group(1)) for m in SCREEN_ROW.finditer(d)), default=0) for d in dumps]
    counts = [c for c in all_counts if c]
    stalls = sum(1 for x, y in zip(counts, counts[1:]) if y <= x)
    if len(counts) < 2 or 2 * len(counts) < len(all_counts):
        fails.append("%d of %d screen dumps after the program started show its count"
                     % (len(counts), len(all_counts)))
    elif stalls:
        fails.append("the program's count did not rise between %d of %d screen dumps"
                     % (stalls, len(counts) - 1))

    charging = sum(1 for b in hbs if b[16])
    levels = [int(b[15]) for b in hbs if b[15]]
    if charging:
        power = "charging on %d of %d heartbeats" % (charging, len(hbs))
    elif levels:
        power = "never charging, %d%% to %d%%" % (levels[0], levels[-1])
    else:
        power = "unknown: the gauge was never read"

    dies = [int(b[17].split()[0]) for b in hbs if b[17] != "?"]
    rates = sorted(int(x[0]) for x in aus[1:]) or [int(aus[0][0])]
    lows = [int(x[1]) for x in aus[1:]]
    busy = [float(m.group(1)) for m in PERF.finditer(text)][1:]
    keys = [int(m.group(2)) for m in SCREEN_ROW.finditer(text)]
    if keys.count(72) == 0 or keys.count(74) == 0:
        fails.append("the screen dumps do not show the program reading both typed keys "
                     "(%d H, %d J)" % (keys.count(72), keys.count(74)))
    pressed = int(h[8]) - int(hbs[0][8])

    print("soak: %s" % a.log)
    print("  %d heartbeats over %.1f minutes, fields %d to %d, one boot: %s"
          % (len(hbs), span, fields[0], fields[-1], "yes" if boots == 1 else "no (%d)" % boots))
    print("  rt min %.3f, mean %.4f" % (low, mean))
    for name, v in zero:
        print("  %-22s %d" % (name, v))
    for name, first, final in grew:
        print("  %-22s %d -> %d" % (name, first, final))
    print("  audio consumed         %d-%d Hz (the control quantity), queue low %d"
          % (rates[0], rates[-1], min(lows) if lows else -1))
    if busy:
        print("  core 0 busy            %.1f-%.1f %%" % (min(busy), max(busy)))
    if keys:
        print("  keys read on screen    %d rows dumped: %d H, %d J"
              % (len(keys), keys.count(72), keys.count(74)))
    if counts:
        print("  program count          %d of %d screen dumps after the start, %d to %d"
              % (len(counts), len(all_counts), counts[0], counts[-1]))
    print("  card changes, parks    %s, %s (%s, %s at the start)"
          % (h[18], h[19], hb_start[18], hb_start[19]))
    print("  AY written             %d of %d heartbeats after the start"
          % (len(edges) - 1 - still if len(edges) > 1 else 0, max(len(edges) - 1, 0)))
    print("  PicoCalc key events    %d during the run (pressed by hand; not required)" % pressed)
    print("  power                  %s" % power)
    if dies:
        print("  die temperature        %d to %d C, %d C at the end (uncalibrated)"
              % (min(dies), max(dies), dies[-1]))
    if garbled:
        print("  %d heartbeat lines garbled in the capture; the counters are cumulative, "
              "so the later ones cover them" % garbled)
    if fails:
        print("FAIL")
        for f in fails:
            print("  " + f)
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
