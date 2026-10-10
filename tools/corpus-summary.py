#!/usr/bin/env python3
"""The corpus run's report (design.md §15, M12), from oric-corpus's lines.

    corpus-summary.py TAPDIR ATMOS.tsv ORIC1.tsv

How many tapes loaded on each machine, those that loaded on neither, the
titles that ran an undocumented opcode and which (for §5.1's decision),
the titles that enabled the CB1 interrupt themselves (M16), and the
largest tape (§3.4's floor for the tape buffer).
"""

import collections
import os
import sys

FIELDS = ["name", "machine", "outcome", "files", "bytes", "undoc", "ops", "cb1", "mode", "note", "by_name", "one_byte"]


def load(path):
    rows = {}
    # A few TOSEC names are not UTF-8: kept as their bytes were.
    with open(path, encoding="utf-8", errors="surrogateescape") as f:
        for line in f:
            r = dict(zip(FIELDS, line.rstrip("\n").split("\t")))
            rows[r["name"]] = r
    return rows


def ops(r):
    """{opcode: (count, first pc)} from "#AB:3@#1234,..."."""
    out = {}
    if r["ops"] != "-":
        for o in r["ops"].split(","):
            op, rest = o.split(":")
            n, pc = rest.split("@")
            out[op] = (int(n), pc)
    return out


def main():
    sys.stdout.reconfigure(errors="surrogateescape")
    tapdir, atmos_tsv, oric1_tsv = sys.argv[1:4]
    runs = {"Atmos 48K (1.1)": load(atmos_tsv), "Oric-1 48K (1.0)": load(oric1_tsv)}
    names = sorted(set().union(*runs.values()))
    print(f"{len(names)} tapes from {tapdir}\n")

    print("Outcome per machine (loaded: every file asked for was found and whole)")
    for m, rows in runs.items():
        c = collections.Counter(r["outcome"] for r in rows.values())
        print(f"  {m:18s} " + ", ".join(f"{k} {c[k]}" for k in ("loaded", "gave-up", "cut-short", "no-load")))
    either = [n for n in names if any(rows.get(n, {}).get("outcome") == "loaded" for rows in runs.values())]
    print(f"  loaded on either   {len(either)}")

    neither = [n for n in names if n not in either]
    print(f"\nNot loaded on either machine ({len(neither)})")
    for n in neither:
        notes = "; ".join(f"{m.split()[0]}: {rows[n]['outcome']}, {rows[n]['files']} files, {rows[n]['note']}"
                          for m, rows in runs.items() if n in rows)
        print(f"  {n}\n      {notes}")

    print("\nUndocumented opcodes (count, first PC), by title and machine")
    by_op = collections.defaultdict(set)
    titles = set()
    for m, rows in runs.items():
        for n in names:
            r = rows.get(n)
            if not r or r["undoc"] == "0":
                continue
            titles.add(n)
            o = ops(r)
            for op in o:
                by_op[op].add(n)
            desc = ", ".join(f"{op} x{c} at {pc}" for op, (c, pc) in sorted(o.items()))
            print(f"  {n} [{m}, {r['outcome']}, {r['mode']}]\n      {r['undoc']}: {desc}")
    if not titles:
        print("  none")
    print(f"\n  {len(titles)} titles; by opcode:")
    for op in sorted(by_op):
        print(f"    {op}: {len(by_op[op])} titles")

    print("\nCB1 interrupt enabled outside the ROM (M16's titles)")
    any_cb1 = False
    for m, rows in runs.items():
        for n in names:
            r = rows.get(n)
            if r and r["cb1"] != "-":
                any_cb1 = True
                print(f"  {n} [{m}], PC {r['cb1']}")
    if not any_cb1:
        print("  none")

    def largest(n):
        """The largest file a title's deck holds: its .tap, or its biggest part."""
        p = os.path.join(tapdir, n)
        if os.path.isdir(p):
            return max(os.path.getsize(os.path.join(p, f)) for f in os.listdir(p))
        return os.path.getsize(p + ".tap")

    sizes = sorted(((largest(n), n) for n in names), reverse=True)
    print("\nLargest tape files (bytes; a title in parts by its largest part)")
    for s, n in sizes[:5]:
        print(f"  {s:7d}  {n}")
    loaded = sorted(((int(r["bytes"]), n) for rows in runs.values() for n, r in rows.items()),
                    reverse=True)
    print("Most bytes loaded by one title, all its files (bytes)")
    for s, n in loaded[:3]:
        print(f"  {s:7d}  {n}")


if __name__ == "__main__":
    main()
