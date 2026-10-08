#!/bin/sh
# Draw every golden scene with Oricutron's ULA and compare it, pixel for
# pixel, with the committed golden image (design.md §7.7, §13.4): the
# check that the goldens are not only our own opinion.
#
#   tools/trace/build-oricutron.sh out/oricutron        # out/trace/oricutron-render
#   cmake --build build/host --target test_golden
#   tools/render-diff.sh [BUILD] [OUT]                  # build/host, out/render
#
# Writes OUT/<scene>.frame (ours) and OUT/<scene>.ppm (Oricutron's), and
# reports each scene as same or differing, with a count of pixels.
set -eu

build="${1:-build/host}"
out="${2:-out/render}"
here="$(cd "$(dirname "$0")" && pwd)"
golden="$here/../test/host/golden"
render="${ORICUTRON_RENDER:-out/trace/oricutron-render}"
[ -x "$render" ] || { echo "$render: build it with tools/trace/build-oricutron.sh" >&2; exit 1; }

mkdir -p "$out"
"$build/test/host/test_golden" --frames "$out" >/dev/null
fail=0
for frame in "$out"/*.frame; do
    name="$(basename "$frame" .frame)"
    "$render" "$frame" "$out/$name.ppm"
    want="$golden/$name.ppm"
    [ "$name" = mirror16k ] && want="$golden/colours.ppm"    # it shares colours' golden
    n="$(python3 -I - "$out/$name.ppm" "$want" <<'PY'
import sys
a, b = (open(p, "rb").read() for p in sys.argv[1:3])
if len(a) != len(b):
    print("size"); sys.exit()
h = len(b"P6\n240 224\n255\n")
print(sum(a[i:i+3] != b[i:i+3] for i in range(h, len(a), 3)))
PY
)"
    if [ "$n" = 0 ]; then
        echo "same       $name"
    else
        echo "DIFFERENT  $name: $n pixels"
        fail=1
    fi
done
exit $fail
