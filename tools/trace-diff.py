#!/usr/bin/env python3
"""trace-diff.py — the trace diff against Oricutron (design.md §13.4).

Runs the same ROM and the same keys under this project's core (oric-trace)
and under Oricutron (oricutron-trace), and compares the per-instruction
PC/A/X/Y/S/P/cycles traces line by line:

    tools/trace-diff.py run [--rom 1.0|1.1] [--ram 16|48]
                            [--keys 'PRINT 2+2\\n'] [--cycles N] [--vsync]
    tools/trace-diff.py diff OURS.trace REF.trace
    tools/trace-diff.py tape FILE.tap [--rom 1.0|1.1] [--ram 16|48]
                             [--keys 'CLOAD""\\n'] [--then 'RUN\\n'] [--wait S]
                             [--vsync] [--after S]

`tape` runs Oricutron alone with FILE.tap in its deck, played as a signal
through the ROM's own routines (its tape traps off), types --keys, waits
--wait seconds of guest time, types --then, and prints its screen: the
check that a tape this project wrote loads elsewhere (§15.2 M10). --vsync turns
Oricutron's VSync hack on once --then is typed, the tape having loaded
by the signal the hack disconnects, and --after runs that much longer
(M16).

`run` finds the ROM in roms/ (or $PICO_ORIC_ROMS) by SHA-1, as the tests
do, and writes out/trace/{ours,ref}.trace and keys.txt. --vsync fits the
vertical-sync modification to both (design.md §15.2 M16) and puts
VSYNC_TEST in memory before the keys (--test puts it there alone), typing by default a call of its
poll, a call of its install and a print of its count. Build the two
tracers first:

    cmake --build build/host --target oric-trace
    tools/trace/build-oricutron.sh out/oricutron

Oricutron's VIA and ULA are cycle-based, so the two machines keep the same
time from reset and no resync is attempted: the first difference is
reported with what led to it, and the run fails. The one difference
expected is the reset:

  reset     S and P from reset until the ROM first sets them: Oricutron
            leaves S at #FF and I clear, a 6502 takes S down by three and
            sets I. Expected.

Oricutron's known errata, by name. tools/trace/build-oricutron.sh
corrects the two that would part the traces for good, in its copy of the
checkout (ORIC_TRACE_KEEP_ERRATA=1 leaves them in); check the datasheet
before believing either side of anything else:

  ORICUTRON_NO_I_DELAY   corrected. Oricutron applies CLI's, SEI's and
                         PLP's I at once; a 6502 polls IRQ for the next
                         instruction first. An IRQ just after SEI moved to
                         the next CLI (ROM 1.1, cycle 2,323,082).
  ORICUTRON_BRANCH_PAGE  corrected. Oricutron charges a taken branch's
                         page-crossing cycle against the opcode's address,
                         not the next instruction's (ROM 1.0, BNE at
                         #C5FF).
  ORICUTRON_VSYNC_LINE   corrected. The VSync hack's pulse starts at
                         Oricutron's raster wrap, 44 lines before its
                         picture at 50 Hz; the ULA's sync is lines
                         256-259 from the first active line (Brown), 234
                         at 60 Hz (Clock Signal). Its pulse starts there.
  ORICUTRON_VSYNC_COARSE corrected. The hack's CB1 takes the pulse's
                         level at the start of the instruction about to
                         run, so an edge inside one is seen after the
                         next; it takes the level at the instruction's end.
  ORICUTRON_VIA_AHEAD    Oricutron clocks the VIA by an instruction's
                         cycles before running it, so a timer read
                         part-way into an instruction sees a later count.
                         Not seen in a trace yet.

Exit status is 0 when the traces agree to the end, 1 otherwise.
"""

import argparse
import hashlib
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "out" / "trace"

# romset.c's digests.
ROMS = {
    "1.0": ("basic10", "333116e6884d85aaa4dfc7578a91cceeea66d016", 0xFF70),
    "1.1": ("basic11b", "9451a1a09d8f75944dbd6f91193fc360f1de80ac", 0xFF78),
}

SHIFT = (4, 4)          # row, column: #A4, the decoder's SHIFT (guest.c)
FIELD = 19968           # cycles in a 50 Hz field (§11.1)
BOOT = 300 * FIELD      # six seconds: past Ready on every machine
HOLD = 4 * FIELD        # guest.c's pace
GAP = 4 * FIELD
LINE = 25 * FIELD       # after RETURN, for the ROM to act on it

NAMES = ["PC", "A", "X", "Y", "S", "P", "cycles", "bytes"]

# The CB1 test program for --vsync, at #0400 (M16). #0400 installs a
# handler for the CB1 interrupt ahead of the ROM's, through the JMP the
# IRQ vector points at in page 2 (IRQ_VECTOR), and enables it; the
# handler counts CB1 interrupts at #0440 and returns, passing anything
# else to the ROM. Both ROMs set CB1 to its rising edge at boot (PCR
# #DD) and keep the bit through their AY writes, so that is the pulse's
# end. #0450 takes its start: with interrupts masked, so that the flag is
# the poll's alone, it sets CB1 to its falling edge and waits on it ten times,
# well inside the 25 fields the keys leave a command (the ROM keeps one
# key typed ahead). #0480 waits on T1's flag the same way, the ROM's
# handler racing it. #04A0 writes T2's latch for ever, seven cycles a
# turn, so that T1's interrupts fall on every cycle of the write.
VSYNC_TEST = 0x0400
IRQ_VECTOR = {"1.0": 0x0228, "1.1": 0x0244}
VSYNC_KEYS = "CALL#450\nCALL#400\nPRINTPEEK(#440)\n"


def vsync_test(version):
    v1, v2 = IRQ_VECTOR[version] + 1, IRQ_VECTOR[version] + 2
    lo = lambda a: a & 0xFF
    hi = lambda a: a >> 8
    code = [
        0x78,                                  # #0400 SEI
        0xAD, lo(v1), hi(v1), 0x8D, 0x32, 0x04,  # LDA V+1 : STA #0432
        0xAD, lo(v2), hi(v2), 0x8D, 0x33, 0x04,  # LDA V+2 : STA #0433
        0xA9, 0x20, 0x8D, lo(v1), hi(v1),        # LDA #20 : STA V+1
        0xA9, 0x04, 0x8D, lo(v2), hi(v2),        # LDA #04 : STA V+2
        0xA9, 0x90, 0x8D, 0x0E, 0x03,            # LDA #90 : STA IER, CB1 on
        0x58, 0x60, 0x00, 0x00,                  # CLI : RTS
        0x48,                                  # #0420 PHA
        0xAD, 0x0D, 0x03, 0x29, 0x10,            # LDA IFR : AND #10
        0xF0, 0x08,                              # BEQ #0430
        0xAD, 0x00, 0x03,                        # LDA ORB, clearing the flag
        0xEE, 0x40, 0x04, 0x68, 0x40,            # INC #0440 : PLA : RTI
        0x68, 0x4C, 0x00, 0x00,                  # #0430 PLA : JMP the ROM's
    ]
    code += [0x00] * (0x50 - len(code))
    code += [
        0x78,                                    # #0450 SEI
        0xAD, 0x0C, 0x03, 0x29, 0xEF,            # LDA PCR : AND #EF
        0x8D, 0x0C, 0x03,                        # STA PCR, CB1 falling
        0xAD, 0x00, 0x03, 0xA2, 0x0A,            # LDA ORB : LDX #10
        0xAD, 0x0D, 0x03, 0x29, 0x10, 0xF0, 0xF9,  # #045E LDA IFR : AND #10 : BEQ
        0xAD, 0x00, 0x03, 0xCA, 0xD0, 0xF3,      # LDA ORB : DEX : BNE
        0xAD, 0x0C, 0x03, 0x09, 0x10,            # LDA PCR : ORA #10
        0x8D, 0x0C, 0x03, 0x58, 0x60,            # STA PCR : CLI : RTS
    ]
    code += [0x00] * (0x80 - len(code))
    code += [
        0xA2, 0x0A,                              # #0480 LDX #10
        0xAD, 0x0D, 0x03, 0x29, 0x40, 0xF0, 0xF9,  # LDA IFR : AND #40 : BEQ
        0xCA, 0xD0, 0xF6,                        # DEX : BNE
        0x60,                                    # RTS
    ]
    code += [0x00] * (0xA0 - len(code))
    code += [
        0x8D, 0x08, 0x03, 0x4C, 0xA0, 0x04,      # #04A0 STA T2C-L : JMP #04A0
    ]
    return code


# The Microdisc's EPROM (design.md §10.2), for the disc subcommand.
MICRODISC = "0d2ef6e67322f48f4b7e08d8bbe68827e2074561"


def find_rom(version):
    base, digest, table = ROMS[version]
    return find_image(digest, version)


def find_image(digest, what):
    where = Path(os.environ.get("PICO_ORIC_ROMS", ROOT / "roms"))
    for f in sorted(where.iterdir()) if where.is_dir() else []:
        try:
            data = f.read_bytes()
        except OSError:
            continue
        if hashlib.sha1(data).hexdigest() == digest:
            return data
    sys.exit("trace-diff: no ROM %s (SHA-1 %s) in %s" % (what, digest, where))


def cell_for(rom, table, ch):
    """The matrix cell the ROM's own key table gives `ch` (guest.c)."""
    want = 0x0D if ch == "\n" else ord(ch)
    letter = (want + 0x20) | 0x80 if "A" <= ch <= "Z" else None
    t = table - 0xC000
    for half in (0, 1):
        for i in range(64):
            e = rom[t + half * 64 + i]
            if e and (e == want or (half == 0 and e == letter)):
                return i & 7, i >> 3, half == 1
    sys.exit("trace-diff: no key for %r in this ROM" % ch)


def keyscript(rom, table, text, t=BOOT):
    lines = ["# written by trace-diff.py from %r" % text]
    for ch in text:
        row, col, shifted = cell_for(rom, table, ch)
        if shifted:
            lines.append("%d down %d %d" % (t, *SHIFT))
            t += FIELD
        lines.append("%d down %d %d" % (t, row, col))
        t += HOLD
        lines.append("%d up %d %d" % (t, row, col))
        if shifted:
            t += FIELD
            lines.append("%d up %d %d" % (t, *SHIFT))
        t += LINE if ch == "\n" else GAP
    return "\n".join(lines) + "\n", t


def run(args):
    rom = find_rom(args.rom)
    base, _, table = ROMS[args.rom]
    OUT.mkdir(parents=True, exist_ok=True)
    rom_path = OUT / (base + ".rom")
    rom_path.write_bytes(rom)
    if args.vsync and args.keys is None:
        args.keys = VSYNC_KEYS
    ks, end = keyscript(rom, table, args.keys or "")
    if args.vsync or args.test:
        poke = "%d poke %04X %s\n" % (BOOT - FIELD, VSYNC_TEST,
                                       "".join("%02X" % b for b in vsync_test(args.rom)))
        ks = poke + ks
    (OUT / "keys.txt").write_text(ks)
    cycles = args.cycles or end + 2 * FIELD
    mach = "o16k" if args.ram == 16 else ("oric1" if args.rom == "1.0" else "atmos")
    ours = [str(ROOT / "build" / "host" / "oric-trace"), str(rom_path), "-r", str(args.ram)]
    ref = [str(OUT / "oricutron-trace"), str(OUT / base), "-m", mach]
    if args.vsync:
        ours.append("-v")
        ref.append("-v")
    for cmd, name in ((ours, "ours"), (ref, "ref")):
        cmd += ["-c", str(cycles), "-k", str(OUT / "keys.txt"), "-s"]
        with open(OUT / (name + ".trace"), "w") as out, open(OUT / (name + ".screen"), "w") as err:
            if subprocess.call(cmd, stdout=out, stderr=err):
                sys.exit("trace-diff: %s failed; see %s" % (cmd[0], OUT / (name + ".screen")))
    print("ROM %s, %dK, %d cycles, keys %r%s" % (args.rom, args.ram, cycles, args.keys or "",
                                               ", vsync hack" if args.vsync else ""))
    for name in ("ours", "ref"):
        print("--- %s screen" % name)
        print("\n".join(l for l in (OUT / (name + ".screen")).read_text().split("\n") if l.strip()))
    return diff(OUT / "ours.trace", OUT / "ref.trace")


def resync(a, b, i, window=4000, confirm=200):
    """The nearest (i', j') at or after i where the traces agree exactly
    for `confirm` lines, cycles included; None if none within `window`."""
    index = {}
    for j in range(i, min(len(b), i + window)):
        index.setdefault(b[j], []).append(j)
    best = None
    for k in range(i, min(len(a), i + window)):
        for j in index.get(a[k], ()):
            if best and (k - i) + (j - i) >= best[2]:
                break
            if a[k:k + confirm] == b[j:j + confirm]:
                best = (k, j, (k - i) + (j - i))
                break
        if best and k - i >= best[2]:
            break
    return best[:2] if best else None


def classify(ours, ref):
    """What lay between a divergence and its resync, by name."""
    def entries(seg):
        return sum(1 for l in seg if l.split()[0] in IRQ_ENTRIES)
    def ops(seg):
        return {l.split()[7][:2] for l in seg}
    if entries(ours) != entries(ref):
        late = "Oricutron" if entries(ours) > entries(ref) else "ours"
        if late == "Oricutron" and ops(ref) & {"58", "78", "28"}:
            return "interrupt: Oricutron took it later, after CLI/SEI/PLP (ORICUTRON_NO_I_DELAY)"
        return "interrupt: %s took it later" % late
    return None


def show(a, b, i, j=None):
    j = i if j is None else j
    print("  %-34s %s" % ("ours", "Oricutron"))
    for k in range(-8, 6):
        if 0 <= i + k < len(a) and 0 <= j + k < len(b):
            print("%s %-34s %s" % (">" if k == 0 else " ", a[i + k], b[j + k]))


def diff(ours_path, ref_path):
    a = [l for l in Path(ours_path).read_text().split("\n") if l]
    b = [l for l in Path(ref_path).read_text().split("\n") if l]

    # The reset: S and P until the ROM first sets them.
    i = 0
    agreed = {4: False, 5: False}
    while i < min(len(a), len(b)) and a[i] != b[i]:
        fx, fy = a[i].split(), b[i].split()
        if any(fx[k] != fy[k] for k in range(8) if k not in agreed or agreed[k]):
            break
        for k in agreed:
            agreed[k] = agreed[k] or fx[k] == fy[k]
        i += 1
    reset = i
    i = 0 if reset == 0 else reset
    j = i

    explained = {}
    while i < len(a) and j < len(b):
        if a[i] == b[j]:
            i += 1
            j += 1
            continue
        at = resync(a, b, i) if i == j else None
        if at is None and i != j:
            # Already offset by an earlier resync: search from both.
            at = resync_offset(a, b, i, j)
        why = classify(a[i:at[0]], b[j:at[1]]) if at else None
        if not why:
            fx, fy = a[i].split(), b[j].split()
            bad = [NAMES[k] for k in range(8) if fx[k] != fy[k]]
            print("DIVERGED at our instruction %d, cycle %s: %s differ%s" %
                  (i + 1, fx[6], ", ".join(bad), "" if at else "; no resync within the window"))
            show(a, b, i, j)
            io = [l for l in a[max(0, i - 40):i] if l.split()[7][2:4] == "03" and
                  l.split()[7][:2] in IO_OPS]
            if io:
                print("  last page #03 access before it: %s" % io[-1])
            return 1
        explained[why] = explained.get(why, 0) + 1
        if explained[why] == 1:
            print("explained at cycle %s: %s" % (a[i].split()[6], why))
            show(a, b, i, j)
        i, j = at

    print("agree to the end: %d instructions, after %d lines of reset S/P" % (min(len(a), len(b)), reset))
    for why, n in explained.items():
        print("  %5d x %s" % (n, why))
    if len(a) - i != len(b) - j:
        print("but the traces' tails differ in length: ours %d, Oricutron %d" % (len(a) - i, len(b) - j))
        return 1
    return 0


def resync_offset(a, b, i, j, window=4000, confirm=200):
    for d in range(window):
        for (k, m) in ((i + d, j), (i, j + d)):
            if k < len(a) and m < len(b) and a[k] == b[m] and a[k:k + confirm] == b[m:m + confirm]:
                return (k, m)
    return None


# The ROM's IRQ handler entry: the vector at #FFFE points into page 2.
IRQ_ENTRIES = {"0244", "0228"}


# Absolute-addressed opcodes that read or write their operand: the second
# and third bytes are the address, so "03" in the third byte is page #03.
IO_OPS = {"AD", "AE", "AC", "8D", "8E", "8C", "2C", "0D", "2D", "4D", "6D", "CD", "ED",
          "EC", "CC", "0E", "2E", "4E", "6E", "CE", "EE", "BD", "B9", "BE", "BC", "9D", "99",
          "1D", "19", "3D", "39", "5D", "59", "7D", "79", "DD", "D9", "FD", "F9"}


def tape(args):
    rom = find_rom(args.rom)
    base, _, table = ROMS[args.rom]
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / (base + ".rom")).write_bytes(rom)
    ks, t = keyscript(rom, table, args.keys)
    then, end = keyscript(rom, table, args.then, t + int(args.wait * 1000000))
    (OUT / "tape-keys.txt").write_text(ks + "".join(then.splitlines(True)[1:]))
    mach = "o16k" if args.ram == 16 else ("oric1" if args.rom == "1.0" else "atmos")
    cmd = [str(OUT / "oricutron-trace"), str(OUT / base), "-m", mach, "-q", "-s",
           "-t", str(Path(args.tap).resolve()), "-c", str(end + 2 * FIELD + int(args.after * 1000000)),
           "-k", str(OUT / "tape-keys.txt")]
    if args.vsync:
        # The hack on once the keys are typed, the tape loaded (M16).
        cmd += ["-V", str(end)]
    print("ROM %s, %dK, %s: %r, %g s, %r" % (args.rom, args.ram, args.tap, args.keys,
                                             args.wait, args.then))
    return subprocess.call(cmd, stdout=subprocess.DEVNULL)


def disc(args):
    """Oricutron boots a disc image with its Microdisc (M14) and is typed
    at, as tape does with a tape: a disc we wrote, read back by it."""
    rom = find_rom(args.rom)
    base, _, table = ROMS[args.rom]
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / (base + ".rom")).write_bytes(rom)
    (OUT / "microdis.rom").write_bytes(find_image(MICRODISC, "microdis.rom"))
    ks, t = keyscript(rom, table, args.keys, int(args.boot * 1000000))
    then, end = keyscript(rom, table, args.then, t + int(args.wait * 1000000))
    (OUT / "disc-keys.txt").write_text(ks + "".join(then.splitlines(True)[1:]))
    mach = "oric1" if args.rom == "1.0" else "atmos"
    cmd = [str(OUT / "oricutron-trace"), str(OUT / base), "-m", mach, "-q", "-s",
           "-d", str(Path(args.dsk).resolve()), "-c", str(end + 2 * FIELD),
           "-k", str(OUT / "disc-keys.txt")]
    print("ROM %s, 48K, %s: %g s, %r, %g s, %r" % (args.rom, args.dsk, args.boot, args.keys,
                                                   args.wait, args.then))
    return subprocess.call(cmd, stdout=subprocess.DEVNULL)


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = p.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("--rom", choices=sorted(ROMS), default="1.1")
    r.add_argument("--ram", type=int, choices=(16, 48), default=48)
    r.add_argument("--keys")
    r.add_argument("--cycles", type=int)
    r.add_argument("--vsync", action="store_true")
    r.add_argument("--test", action="store_true")
    d = sub.add_parser("diff")
    d.add_argument("ours")
    d.add_argument("ref")
    t = sub.add_parser("tape")
    t.add_argument("tap")
    t.add_argument("--rom", choices=sorted(ROMS), default="1.1")
    t.add_argument("--ram", type=int, choices=(16, 48), default=48)
    t.add_argument("--keys", default='CLOAD""\\n')
    t.add_argument("--then", default="RUN\\n")
    t.add_argument("--wait", type=float, default=10.0)
    t.add_argument("--vsync", action="store_true")
    t.add_argument("--after", type=float, default=0.0)
    k = sub.add_parser("disc")
    k.add_argument("dsk")
    k.add_argument("--rom", choices=sorted(ROMS), default="1.1")
    k.add_argument("--boot", type=float, default=15.0)
    k.add_argument("--keys", default="X")
    k.add_argument("--then", default="DIR\\n")
    k.add_argument("--wait", type=float, default=5.0)
    args = p.parse_args()
    if args.cmd == "run":
        if args.keys is not None:
            args.keys = args.keys.encode().decode("unicode_escape")
        sys.exit(run(args))
    if args.cmd in ("tape", "disc"):
        args.keys = args.keys.encode().decode("unicode_escape")
        args.then = args.then.encode().decode("unicode_escape")
        sys.exit(tape(args) if args.cmd == "tape" else disc(args))
    sys.exit(diff(args.ours, args.ref))


if __name__ == "__main__":
    main()
