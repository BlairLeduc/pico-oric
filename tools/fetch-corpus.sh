#!/bin/sh
# Fetch M12's corpus: TOSEC's Oric-1 and Atmos set of 2012-04-23, from the
# Internet Archive (design.md §15, M12). Not committed: the titles are not
# ours to distribute, and out/ is ignored.
#
#   ./tools/fetch-corpus.sh [dir]        # default: out/corpus
#
# Leaves the zip, checked by SHA-1, in DIR/dl, every .tap in DIR/tap and
# every disc image in DIR/dsk (for M14). TOSEC zips each title on its own,
# so the set is a zip of zips, and a tape is named after its title's zip.
# A title in parts (".tap", ".ta1".., or several ".tap"s) is a directory,
# DIR/tap/<title>/, with the parts under their own names: its loaders ask
# for the next part by file name, CLOAD "NAME.TA1", as Euphoric served it.
set -eu

dir="${1:-out/corpus}"
item="Tangerine_Oric_1_and_Atmos_TOSEC_2012_04_23"
sha1="6a105e737804a9e333549f31ecbae9d6314cdfa5"
zip="$dir/dl/$item.zip"

mkdir -p "$dir/dl"
if [ ! -f "$zip" ] || [ "$(shasum "$zip" | cut -d' ' -f1)" != "$sha1" ]; then
    echo "fetching $item.zip -> $dir/dl"
    curl -sSLf -o "$zip" "https://archive.org/download/$item/$item.zip"
fi
got="$(shasum "$zip" | cut -d' ' -f1)"
if [ "$got" != "$sha1" ]; then
    echo "$zip: sha1 $got, expected $sha1" >&2
    exit 1
fi

# The extraction is Python's zipfile, run isolated (-I) from a temporary
# script outside the downloaded tree, and it writes only under DIR.
py="$(mktemp -t fetch-corpus)"
trap 'rm -f "$py"' EXIT
cat >"$py" <<'PY'
import io, os, re, sys, zipfile
zip_path, out = sys.argv[1], sys.argv[2]

def part(name):
    """A tape part's place on the joined tape, or None if not a tape:
    "x.tap" is 0, "x.ta1".."x.ta9" and "x.t10" their number, "x.1" too."""
    ext = os.path.splitext(name)[1].lower()
    if ext == ".tap":
        return 0
    m = re.fullmatch(r"\.(?:ta?)?(\d+)", ext)
    return int(m.group(1)) if m else None

counts = {"tap": 0, "dsk": 0, "joined": 0}
skipped = []
with zipfile.ZipFile(zip_path) as outer:
    for name in outer.namelist():
        if not name.lower().endswith(".zip"):
            continue
        title = os.path.basename(name)[:-4].replace("/", "_")
        with zipfile.ZipFile(io.BytesIO(outer.read(name))) as inner:
            files = [n for n in inner.namelist() if not n.endswith("/")]
            tapes = sorted((part(n), n) for n in files if part(n) is not None)
            for n in files:
                if n.lower().endswith(".dsk"):
                    os.makedirs(os.path.join(out, "dsk"), exist_ok=True)
                    stem = title if len(files) == 1 else f"{title} - {os.path.basename(n)[:-4]}"
                    with open(os.path.join(out, "dsk", stem + ".dsk"), "wb") as f:
                        f.write(inner.read(n))
                    counts["dsk"] += 1
                elif part(n) is None:
                    skipped.append(f"{title}: {n}")
            if len(tapes) == 1:
                data = inner.read(tapes[0][1])
                path = os.path.join(out, "tap", title + ".tap")
                os.makedirs(os.path.dirname(path), exist_ok=True)
                # Two titles are in two sub-sets (Frelon whole, and in
                # parts); the one in parts is kept.
                if os.path.isdir(os.path.join(out, "tap", title)):
                    continue
                if os.path.exists(path):
                    counts["tap"] -= 1
                with open(path, "wb") as f:
                    f.write(data)
                counts["tap"] += 1
            elif tapes:
                # A title in parts keeps them as files of their own, under
                # their own names, for its loaders' CLOAD "NAME.TA1"
                # (design.md §10.3): a directory per title.
                d = os.path.join(out, "tap", title)
                os.makedirs(d, exist_ok=True)
                for _, n in tapes:
                    with open(os.path.join(d, os.path.basename(n)), "wb") as f:
                        f.write(inner.read(n))
                single = os.path.join(out, "tap", title + ".tap")
                if os.path.exists(single):
                    os.remove(single)
                    counts["tap"] -= 1
                counts["tap"] += 1
                counts["joined"] += 1
print(f"{counts['tap']:5d} titles -> {os.path.join(out, 'tap')} ({counts['joined']} in parts, a directory each)")
print(f"{counts['dsk']:5d} discs -> {os.path.join(out, 'dsk')}")
for s in skipped:
    print(f"  not a tape: {s}")
PY
rm -rf "$dir/tap" "$dir/dsk"
python3 -I "$py" "$zip" "$dir"
