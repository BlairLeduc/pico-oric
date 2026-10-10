#!/usr/bin/env python3
"""M15's layout survey (design.md §9.4): the keys the archive's titles read.

    tools/layout-survey.py TAPDIR KEYS.TSV BASIC11.ROM

Two views, because a title reads its keys one of two ways:

- From oric-corpus -k (KEYS.TSV): the keys a title tests at the matrix
  from its own code, past the ROM's scan. Left out as saying nothing
  about a game's keys: a full scan of every key, and a title that tests
  only 7, the cell a read of ORB tests when port A is left as the ROM's
  scan leaves it.
- From the tapes' BASIC: the single characters compared in an IF
  condition, in programs that read keys with KEY$ or GET. The tokens are
  read from the ROM's own keyword table. Menus compare letters too, so
  each movement shape is listed by title, to be read.
"""
import collections
import os
import re
import sys

SHAPES = [("ZX'/", "ZX'/"), ("ZX", "ZX"), ("AZ,.", "AZ,."), ("QAOP", "QAOP"),
          ("IJKM", "IJKM"), ("WASD", "WASD"), ("5678", "5678")]


def matrix(tsv):
    rows = [l.rstrip("\n").split("\t") for l in open(tsv)]
    loaded = [r for r in rows if r[2] == "loaded"]
    read = [r for r in loaded if len(r) > 12 and r[12] != "-"]
    kept, full, seven = [], 0, 0
    for r in read:
        one = r[12].split(" any:")[0]
        keys = set() if one.startswith("any:") else set(one.split(","))
        if len(keys) >= 40:
            full += 1
        elif keys == {"7"}:
            seven += 1
        elif keys - {"7"}:
            kept.append((r[0], sorted(keys - {"7"}), r[12]))
    print(f"{len(rows)} tapes, {len(loaded)} loaded on the Atmos 48K; "
          f"{len(read)} read the matrix from their own code")
    print(f"  {full} scan every key, {seven} test only 7, "
          f"{len(read) - full - seven - len(kept)} read only several columns at once; "
          f"{len(kept)} test particular keys:\n")
    count = collections.Counter()
    for name, keys, _ in kept:
        count.update(keys)
        print(f"  {name[:58]:58} {' '.join(keys)}")
    print("\n  by titles: " + ", ".join(f"{k} {n}" for k, n in count.most_common(20)))


def tokens(rom):
    b = open(rom, "rb").read()
    i = b.find(b"EN" + bytes([ord("D") | 0x80]))
    toks, w = [], ""
    while len(toks) < 128:
        ch = b[i]
        i += 1
        w += chr(ch & 0x7F)
        if ch & 0x80:
            toks.append(w)
            w = ""
    return {t: 0x80 + n for n, t in enumerate(toks)}


def titles(root):
    for e in sorted(os.listdir(root)):
        p = os.path.join(root, e)
        if os.path.isdir(p):
            yield e, [os.path.join(p, f) for f in sorted(os.listdir(p))
                      if f.lower().endswith(".tap")]
        elif e.lower().endswith(".tap"):
            yield e[:-4], [p]


def basic(tapdir, rom):
    t = tokens(rom)
    if_, then, key, get = (bytes([t[k]]) for k in ("IF", "THEN", "KEY$", "GET"))
    pat = re.compile(re.escape(bytes([t["="]])) + rb'"([^"])"')
    per = {}
    for name, files in titles(tapdir):
        data = b"".join(open(f, "rb").read() for f in files)
        if key not in data and get not in data:
            continue
        found = set()
        for line in data.split(b"\x00"):
            at = line.find(if_)
            while at >= 0:
                end = line.find(then, at)
                for m in pat.finditer(line[at:end if end > 0 else len(line)]):
                    c = m.group(1)[0]
                    if 32 <= c < 127:
                        found.add(chr(c).upper())
                at = line.find(if_, at + 1)
        if found:
            per[name] = found
    print(f"\n{len(per)} titles' BASIC reads keys and compares single characters in IF:")
    for label, keys in SHAPES:
        hit = sorted(n for n, v in per.items() if set(keys) <= v)
        print(f"\n  {label}: {len(hit)}")
        for n in hit:
            print(f"    {n[:58]:58} {''.join(sorted(per[n]))}")


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    matrix(sys.argv[2])
    basic(sys.argv[1], sys.argv[3])


main()
