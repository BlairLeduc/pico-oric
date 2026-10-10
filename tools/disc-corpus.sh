#!/bin/sh
# M14's disc corpus (design.md §15.2 M14): every disc image fetched by
# tools/fetch-corpus.sh booted with the Microdisc on the host, and under
# Oricutron, and their screens held to each other after the same time.
#
#   tools/disc-corpus.sh [CORPUS] [OUT] [FIELDS]
#       CORPUS  default out/corpus (its dsk/ is read)
#       OUT     default out/m14/corpus
#       FIELDS  default 1500 (30 s of guest time at 50 Hz)
#
# Needs out/trace/oricutron-trace (tools/trace/build-oricutron.sh) and the
# ROMs in roms/. Oricutron's disc is far quicker than ours, which turns
# (wd1793.h), so a screen that differs is looked at, not counted as a
# fault: it may only be ours still loading. Leaves OUT/ours.tsv, a line
# per image, OUT/ours/ and OUT/ref/ with each one's text screen, and
# OUT/report.txt.
set -eu

corpus="${1:-out/corpus}"
out="${2:-out/m14/corpus}"
fields="${3:-1500}"
here="$(cd "$(dirname "$0")" && pwd)"
jobs="$(sysctl -n hw.ncpu 2>/dev/null || nproc)"
ref="$(pwd)/out/trace/oricutron-trace"
[ -x "$ref" ] || { echo "disc-corpus: build $ref first (tools/trace/build-oricutron.sh)" >&2; exit 1; }

cmake -S "$here/.." -B build/corpus -DPICO_ORIC_HOST=ON -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build/corpus -j --target oric-discs >/dev/null
bin="$(pwd)/build/corpus/oric-discs"

rm -rf "$out/ours" "$out/ref"
mkdir -p "$out/ours" "$out/ref"
# Oricutron's ROMs beside its binary, found in roms/ by SHA-1; it looks
# for microdis.rom beside the BASIC ROM it is given.
python3 -I - "$(pwd)/roms" "$(dirname "$ref")" <<'PY'
import hashlib, pathlib, sys
want = {"9451a1a09d8f75944dbd6f91193fc360f1de80ac": "basic11b.rom",
        "0d2ef6e67322f48f4b7e08d8bbe68827e2074561": "microdis.rom"}
for f in pathlib.Path(sys.argv[1]).iterdir():
    name = want.get(hashlib.sha1(f.read_bytes()).hexdigest()) if f.is_file() else None
    if name:
        (pathlib.Path(sys.argv[2]) / name).write_bytes(f.read_bytes())
PY
echo "discs: $(ls "$corpus/dsk" | wc -l | tr -d ' ') images, Atmos 48K, $fields fields, $jobs at a time"
find "$corpus/dsk" -name '*.dsk' -print0 | LC_ALL=C sort -z |
    xargs -0 -n 8 -P "$jobs" "$bin" -r 11 -f "$fields" -s "$out/ours" | LC_ALL=C sort >"$out/ours.tsv"

cycles=$((fields * 19968))
find "$corpus/dsk" -name '*.dsk' -print0 | LC_ALL=C sort -z |
    xargs -0 -n 1 -P "$jobs" sh -c '
        n=$(basename "$3")
        "$0" "$(dirname "$0")/basic11b" -m atmos -c "$1" -q -s -d "$3" 2>&1 >/dev/null |
            tail -n 28 >"$2/$n.txt"' "$ref" "$cycles" "$out/ref"

python3 -I - "$out" <<'PY' | tee "$out/report.txt"
import pathlib, sys
out = pathlib.Path(sys.argv[1])
rows = [l.split("\t") for l in (out / "ours.tsv").read_text().splitlines() if l]
same, differ, outcomes = [], [], {}
for r in rows:
    outcomes[r[1]] = outcomes.get(r[1], 0) + 1
    if r[1] == "refused":
        continue
    a = (out / "ours" / (r[0] + ".txt"))
    b = (out / "ref" / (r[0] + ".txt"))
    ta = a.read_text().splitlines() if a.exists() else []
    tb = b.read_text().splitlines() if b.exists() else []
    (same if ta == tb else differ).append(r[0])
print("ours: %d images: %s" % (len(rows), ", ".join("%d %s" % (n, k) for k, n in sorted(outcomes.items()))))
print("undocumented opcodes in %d" % sum(1 for r in rows if r[5] != "0"))
print("screens the same as Oricutron's: %d; different: %d" % (len(same), len(differ)))
for n in differ:
    print("  differs: %s" % n)
PY
