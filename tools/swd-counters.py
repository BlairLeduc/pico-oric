#!/usr/bin/env python3
"""swd-counters.py — the firmware's counters over SWD, for the build that
ships, which has no UART (design.md §13.5, §15.2 M15).

    tools/swd-counters.py read  [ELF] [--screen]
    tools/swd-counters.py soak  [ELF] [--minutes 30] [--every 10] [--out out/m15/soak]
    tools/swd-counters.py check [--minutes 30] out/m15/soak/swd-YYYYMMDD-HHMMSS.log

Core 0 copies its counters into g_swd once a second (src/port/handoff.h);
this reads that block, and the guest's text screen, through the Debug
Probe with both cores running. OpenOCD's read goes through the debug
port's memory access, with no halt and no reset: halting core 0 would
starve the audio queue whose counters are being read. `read` prints one
sample. Copied from pico-ace, with the Oric's block and program.

`soak` does not flash, and the build has no UART to type the program
with. Make the program as a tape first, `test_soak --write DIR`, which
saves tools/soak.bas with CSAVE"SOAK",AUTO; copy DIR/SOAK.tap to the
card's /oric/tapes/; flash the build (tools/flash.sh), boot it, and type
CLOAD"SOAK" on the PicoCalc. `soak` then samples every `--every` seconds
for `--minutes`, writing each sample to the log, and runs `check` over
it. While it runs, hold H for a few seconds, and later J: the screen
dumps must show the program reading both (K 72 and K 74).

`check` holds the samples to soak-check.py's rules: one boot (the block's
updates and the board's clock only rise), samples covering the run with
no gap, rt at least 0.995 in every window and 0.999 on average, every
failure counter 0 at the end, the card's changes and the parks unchanged
from the program's start, presents and polls growing, the AY's envelope
started in every window once the program has started (soak.bas plays
one a pass; the keyboard's scan writes port A, so a count of AY writes
would grow with the guest idle), and the program's count rising from
each screen dump that shows it to the next. The consumed rate, the
control quantity, is reported, as are the gauge and the die's
temperature. The power source is not a condition (§13.5).
"""

import argparse
import glob
import os
import re
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ELF = os.path.join(ROOT, "build/pico-release/pico-oric.elf")

# The block's words, in handoff.h's order. SWD_LAYOUT names this list.
MAGIC, LAYOUT = 0x4F524331, 1
WORDS = ["magic", "layout", "updates", "uptime_ms", "fields", "cycles", "late", "slips",
         "presents", "snapshots_dropped", "key_events", "keys_lost", "polls", "i2c_errors",
         "undoc", "log_dropped", "underrun_samples", "late_refills", "consumed",
         "pcm_overflow", "ay_writes", "env_starts", "card_changes", "parks", "busy1000",
         "battery", "temp_c", "screen"]
SIGNED = {"battery", "temp_c"}
ZERO = ["late", "slips", "snapshots_dropped", "keys_lost", "i2c_errors", "undoc",
        "log_dropped", "underrun_samples", "late_refills", "pcm_overflow"]
SAME = ["card_changes", "parks"]
GREW = ["presents", "polls"]

CPU_HZ = 1000000                    # the guest's cycles (design.md §2.1)
SCREEN_COLS, SCREEN_ROWS = 40, 28   # #BB80 (design.md §2.2)
# soak.bas's "C <count> K <key>", the ROM putting a space before a
# positive number or not.
ROW = re.compile(r"\bC ?(\d+) ?K ?(\d+)\b")


def tool(name, env, pattern):
    if os.environ.get(env):
        return os.environ[env]
    found = shutil.which(name)
    if found:
        return found
    found = sorted(glob.glob(os.path.expanduser(pattern)))
    if not found:
        sys.exit("swd-counters.py: %s not found; put it on PATH or set %s" % (name, env))
    return found[-1]


def symbol(elf, name):
    nm = tool("arm-none-eabi-nm", "NM", "~/.pico-sdk/toolchain/*/bin/arm-none-eabi-nm")
    out = subprocess.run([nm, elf], capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == name:
            return int(parts[0], 16)
    sys.exit("swd-counters.py: no %s in %s; is it a build from M15 on?" % (name, elf))


def openocd(commands):
    exe = tool("openocd", "OPENOCD", "~/.pico-sdk/openocd/*/openocd")
    args = [exe]
    scripts = os.path.join(os.path.dirname(exe), "scripts")
    if os.path.isdir(scripts):
        args += ["-s", scripts]
    args += ["-f", "interface/cmsis-dap.cfg", "-f", "target/rp2350.cfg",
             "-c", "adapter speed %s" % os.environ.get("SWD_KHZ", "5000"), "-c", "init"]
    for c in commands:
        args += ["-c", c]
    args += ["-c", "shutdown"]
    r = subprocess.run(args, capture_output=True, text=True, timeout=30)
    return r.stdout + r.stderr


def read(elf, screen=False):
    """One sample: the block's words by name, and the screen's rows if asked."""
    addr = symbol(elf, "g_swd")
    # Core 0 rewrites the block while it is read, a word at a time, so a
    # read can mix two seconds (seen in pico-ace's soak). Read it twice in
    # one session and keep it only when the two agree.
    cmd = "echo \"SWD [read_memory 0x%08x 32 %d]\"" % (addr, len(WORDS))
    vals = None
    for _ in range(5):
        text = openocd([cmd, cmd])
        reads = re.findall(r"^SWD ([0-9a-fx ]+)$", text, re.M)
        if len(reads) != 2:
            sys.exit("swd-counters.py: no read from the target:\n" + text)
        if reads[0] == reads[1]:
            vals = [int(v, 16) for v in reads[0].split()]
            break
    if vals is None:
        sys.exit("swd-counters.py: the block changed under five reads in a row")
    s = dict(zip(WORDS, vals))
    for k in SIGNED:
        if s[k] >= 1 << 31:
            s[k] -= 1 << 32
    if s["magic"] != MAGIC or s["layout"] != LAYOUT:
        sys.exit("swd-counters.py: g_swd reads magic %08x layout %d, want %08x layout %d"
                 % (s["magic"], s["layout"], MAGIC, LAYOUT))
    s["rows"] = []
    if screen and s["screen"]:
        n = SCREEN_COLS * SCREEN_ROWS
        text = openocd(["echo \"SCR [read_memory 0x%08x 8 %d]\"" % (s["screen"], n)])
        m = re.search(r"^SCR ([0-9a-fx ]+)$", text, re.M)
        if m:
            # An attribute as a space, inverse as the plain character.
            b = [int(v, 16) & 0x7F for v in m.group(1).split()]
            for r in range(SCREEN_ROWS):
                row = b[r * SCREEN_COLS:(r + 1) * SCREEN_COLS]
                s["rows"].append("".join(chr(c) if 32 <= c < 127 else " " for c in row))
    return s


def line(s):
    words = " ".join("%s=%d" % (k, s[k]) for k in WORDS[2:-1])
    return "swd %s %s" % (time.strftime("%H:%M:%S"), words)


def parse(path):
    samples, cur = [], None
    for raw in open(path, errors="replace"):
        raw = raw.rstrip("\n")
        if raw.startswith("swd "):
            cur = {k: int(v) for k, v in re.findall(r"(\w+)=(-?\d+)", raw)}
            cur["rows"] = []
            samples.append(cur)
        elif raw.startswith("|") and raw.endswith("|") and cur is not None:
            cur["rows"].append(raw[1:-1])
    return samples


def check(path, minutes):
    ss = parse(path)
    fails = []
    if len(ss) < 3:
        print("FAIL: %d samples in %s, too few to check" % (len(ss), path))
        return 1

    # One boot: a reset starts the block and the clock again.
    resets = sum(1 for a, b in zip(ss, ss[1:])
                 if b["updates"] <= a["updates"] or b["uptime_ms"] <= a["uptime_ms"]
                 or b["fields"] < a["fields"])
    if resets:
        fails.append("%d samples went backwards: a reset during the run?" % resets)
    span = (ss[-1]["uptime_ms"] - ss[0]["uptime_ms"]) / 60000
    if span < minutes:
        fails.append("samples span %.1f minutes, less than %g" % (span, minutes))
    steps = [(b["uptime_ms"] - a["uptime_ms"]) / 1000 for a, b in zip(ss, ss[1:])]
    typical = sorted(steps)[len(steps) // 2]
    gaps = sum(1 for d in steps if d > 3 * typical)
    if gaps:
        fails.append("%d gaps of more than three samples' time" % gaps)

    # rt per window, guest cycles over the board's own clock; the cycles
    # are the count's low word, so a window's difference is taken mod 2^32.
    rts = [((b["cycles"] - a["cycles"]) % (1 << 32)) / CPU_HZ
           / ((b["uptime_ms"] - a["uptime_ms"]) / 1000)
           for a, b in zip(ss, ss[1:]) if b["uptime_ms"] > a["uptime_ms"]]
    low, mean = min(rts), sum(rts) / len(rts)
    if low < 0.995:
        fails.append("rt fell to %.3f (window %d)" % (low, rts.index(low) + 1))
    if mean < 0.999:
        fails.append("mean rt %.4f" % mean)
    rates = sorted((b["consumed"] - a["consumed"]) * 1000 / (b["uptime_ms"] - a["uptime_ms"])
                   for a, b in zip(ss, ss[1:]) if b["uptime_ms"] > a["uptime_ms"])

    last = ss[-1]
    for k in ZERO:
        if last[k]:
            fails.append("%s: %d" % (k, last[k]))
    for k in GREW:
        if last[k] <= ss[0][k]:
            fails.append("%s did not grow (%d to %d): not exercised" % (k, ss[0][k], last[k]))

    # The program: from the first window in which the envelope started,
    # it starts in every one, and the count on screen rises dump to dump.
    # A dump taken while a pass draws its hires picture has no count, so
    # half of them may lack it.
    env = [s["env_starts"] for s in ss]
    start = next((i for i, (a, b) in enumerate(zip(env, env[1:])) if b > a), None)
    counts, keys, dumps = [], [], 0
    if start is None:
        fails.append("the envelope never started: the program was not running")
    else:
        still = sum(1 for a, b in zip(env[start:], env[start + 1:]) if b <= a)
        if still:
            fails.append("the envelope did not start in %d of %d windows after the program "
                         "started" % (still, len(env) - 1 - start))
        for k in SAME:
            if last[k] != ss[start][k]:
                fails.append("%s: %d at the program's start, %d at the end"
                             % (k, ss[start][k], last[k]))
        for s in ss[start + 1:]:
            if not s["rows"]:
                continue
            dumps += 1
            found = [ROW.search(r) for r in s["rows"]]
            found = [(int(m.group(1)), int(m.group(2))) for m in found if m]
            if found:
                counts.append(max(c for c, _ in found))
                keys += [k for _, k in found]
        stalls = sum(1 for a, b in zip(counts, counts[1:]) if b <= a)
        if len(counts) < 2 or len(counts) * 2 < dumps:
            fails.append("%d of %d screen dumps after the program started show its count"
                         % (len(counts), dumps))
        elif stalls:
            fails.append("the program's count did not rise between %d of %d screen dumps"
                         % (stalls, len(counts) - 1))
        if keys.count(72) == 0 or keys.count(74) == 0:
            fails.append("the screen dumps do not show the program reading both H and J "
                         "(%d H, %d J): hold each for a few seconds" % (keys.count(72),
                                                                      keys.count(74)))

    bats = [s["battery"] for s in ss if s["battery"] >= 0]
    charging = sum(1 for b in bats if b & 0x80)
    if charging:
        power = "charging in %d of %d samples" % (charging, len(ss))
    elif bats:
        power = "never charging, %d%% to %d%%" % (bats[0] & 0x7F, bats[-1] & 0x7F)
    else:
        power = "unknown: the gauge was never read"
    dies = [s["temp_c"] for s in ss if s["temp_c"] > -1000]
    busy = [s["busy1000"] / 10 for s in ss[1:]]

    print("soak over SWD: %s" % path)
    print("  %d samples over %.1f minutes, fields %d to %d, one boot: %s"
          % (len(ss), span, ss[0]["fields"], last["fields"], "no" if resets else "yes"))
    print("  rt min %.3f, mean %.4f" % (low, mean))
    for k in ZERO:
        print("  %-22s %d" % (k, last[k]))
    for k in SAME:
        print("  %-22s %d" % (k, last[k]))
    for k in GREW:
        print("  %-22s %d -> %d" % (k, ss[0][k], last[k]))
    print("  audio consumed         %.0f-%.0f Hz (the control quantity)" % (rates[0], rates[-1]))
    if busy:
        print("  core 0 busy            %.1f-%.1f %%" % (min(busy), max(busy)))
    if start is not None:
        print("  envelope starts        %d -> %d after the program started"
              % (ss[start]["env_starts"], last["env_starts"]))
    if counts:
        print("  program count          %d of %d screen dumps, %d to %d"
              % (len(counts), dumps, counts[0], counts[-1]))
        print("  keys read on screen    %d H, %d J" % (keys.count(72), keys.count(74)))
    print("  PicoCalc key events    %d during the run"
          % (last["key_events"] - ss[0]["key_events"]))
    print("  power                  %s" % power)
    if dies:
        print("  die temperature        %d to %d C, %d C at the end (uncalibrated)"
              % (min(dies), max(dies), dies[-1]))
    if fails:
        print("FAIL")
        for f in fails:
            print("  " + f)
        return 1
    print("PASS")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("read")
    r.add_argument("elf", nargs="?", default=ELF)
    r.add_argument("--screen", action="store_true")
    so = sub.add_parser("soak")
    so.add_argument("elf", nargs="?", default=ELF)
    so.add_argument("--minutes", type=float, default=30)
    so.add_argument("--every", type=float, default=10)
    so.add_argument("--out", default="out/m15/soak")
    c = sub.add_parser("check")
    c.add_argument("log")
    c.add_argument("--minutes", type=float, default=30)
    a = ap.parse_args()

    if a.cmd == "read":
        s = read(a.elf, a.screen)
        print(line(s))
        for row in s["rows"]:
            print("|%s|" % row)
        return 0
    if a.cmd == "check":
        return check(a.log, a.minutes)

    os.makedirs(a.out, exist_ok=True)
    log = os.path.join(a.out, "swd-%s.log" % time.strftime("%Y%m%d-%H%M%S"))
    end = time.time() + a.minutes * 60 + a.every
    print("swd-counters.py: sampling for %g minutes from %s -> %s"
          % (a.minutes, time.strftime("%H:%M:%S"), log))
    with open(log, "w") as f:
        f.write("# %s, every %g s\n" % (os.path.relpath(a.elf, ROOT), a.every))
        while time.time() < end:
            t0 = time.time()
            s = read(a.elf, screen=True)
            f.write(line(s) + "\n")
            for row in s["rows"]:
                f.write("|%s|\n" % row)
            f.flush()
            time.sleep(max(0.0, a.every - (time.time() - t0)))
    return check(log, a.minutes)


if __name__ == "__main__":
    sys.exit(main())
