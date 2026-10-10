#!/bin/sh
# M15's layout survey (design.md §9.4): every tape fetched by
# tools/fetch-corpus.sh run on the Atmos 48K with oric-corpus -k, then
# tools/layout-survey.py over its keys and the tapes' BASIC.
#
#   tools/layout-survey.sh [CORPUS] [OUT] [FIELDS]
#       CORPUS  default out/corpus (its tap/ is read)
#       OUT     default out/m15/layouts
#       FIELDS  default 4500 (90 s of guest time at 50 Hz)
#
# Leaves OUT/11.tsv, a line per tape, and OUT/report.txt. Needs
# basic11b.rom in roms/, for the run and for BASIC's tokens.
set -eu

corpus="${1:-out/corpus}"
out="${2:-out/m15/layouts}"
fields="${3:-4500}"
here="$(cd "$(dirname "$0")" && pwd)"
jobs="$(sysctl -n hw.ncpu 2>/dev/null || nproc)"

rom=""
for f in "$here/../roms"/*; do
    [ -f "$f" ] || continue
    if [ "$(shasum "$f" | cut -d' ' -f1)" = 9451a1a09d8f75944dbd6f91193fc360f1de80ac ]; then
        rom="$f"
        break
    fi
done
[ -n "$rom" ] || { echo "layout-survey: no basic11b.rom in roms/" >&2; exit 1; }

cmake -S "$here/.." -B build/corpus -DPICO_ORIC_HOST=ON -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build/corpus -j --target oric-corpus >/dev/null
bin="$(pwd)/build/corpus/oric-corpus"

mkdir -p "$out"
echo "layout-survey: $(ls "$corpus/tap" | wc -l | tr -d ' ') titles on ROM 1.1, 48K, $fields fields, $jobs at a time"
find "$corpus/tap" -mindepth 1 -maxdepth 1 \( -name '*.tap' -o -type d \) -print0 |
    LC_ALL=C sort -z |
    xargs -0 -n 8 -P "$jobs" "$bin" -k -r 11 -m 48 -f "$fields" |
    LC_ALL=C sort >"$out/11.tsv"

python3 -I "$here/layout-survey.py" "$corpus/tap" "$out/11.tsv" "$rom" | tee "$out/report.txt"
