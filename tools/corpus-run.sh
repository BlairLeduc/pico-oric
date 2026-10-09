#!/bin/sh
# M12's corpus run (design.md §15, M12): every tape fetched by
# tools/fetch-corpus.sh, loaded and run on the host on the Atmos 48K and
# the Oric-1 48K, then summarised by tools/corpus-summary.py.
#
#   tools/corpus-run.sh [CORPUS] [OUT] [FIELDS] [FLAG...]
#       CORPUS  default out/corpus (its tap/ is read: .tap files, and a
#               directory for each title in parts)
#       OUT     default out/m12/corpus
#       FIELDS  default 3000 (a minute of guest time at 50 Hz)
#       FLAG    oric-corpus's -N and -B: tapeio's proposed changes
#
# Builds oric-corpus optimised in build/corpus, so that the run takes
# minutes, not an hour. Leaves OUT/<machine>.tsv, a line per tape,
# OUT/<machine>/ with each tape's last frame (.png if sips is there,
# else .ppm) and text screen, and OUT/report.txt.
set -eu

corpus="${1:-out/corpus}"
out="${2:-out/m12/corpus}"
fields="${3:-3000}"
[ $# -gt 3 ] && shift 3 || set --
here="$(cd "$(dirname "$0")" && pwd)"
jobs="$(sysctl -n hw.ncpu 2>/dev/null || nproc)"

cmake -S "$here/.." -B build/corpus -DPICO_ORIC_HOST=ON -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build/corpus -j --target oric-corpus >/dev/null
bin="$(pwd)/build/corpus/oric-corpus"

mkdir -p "$out"
for machine in 11 10; do
    shots="$out/$machine"
    rm -rf "$shots"
    mkdir -p "$shots"
    echo "corpus: $(ls "$corpus/tap" | wc -l | tr -d ' ') titles on ROM $machine, 48K, $fields fields, $jobs at a time${1:+, $*}"
    find "$corpus/tap" -mindepth 1 -maxdepth 1 \( -name '*.tap' -o -type d \) -print0 |
        LC_ALL=C sort -z |
        xargs -0 -n 8 -P "$jobs" "$bin" -r "$machine" -m 48 -f "$fields" -s "$shots" "$@" \
        | LC_ALL=C sort >"$out/$machine.tsv"
    if command -v sips >/dev/null 2>&1; then
        for f in "$shots"/*.ppm; do
            sips -s format png "$f" --out "${f%.ppm}.png" >/dev/null 2>&1 && rm -f "$f"
        done
    fi
done

python3 -I "$here/corpus-summary.py" "$corpus/tap" "$out/11.tsv" "$out/10.tsv" | tee "$out/report.txt"
