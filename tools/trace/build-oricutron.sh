#!/bin/sh
# Build oricutron-trace, the reference half of the trace diff (design.md
# §13.4), and oricutron-render, which draws test_golden's frames with
# Oricutron's ULA (§7.7), from an Oricutron checkout. Nothing of
# Oricutron is copied into this tree: the checkout is copied into OUT, a
# trace hook is added to its 6502.c there with perl, its own Makefile
# builds it, and every object but main.o is linked with each headless
# driver.
#
#   git clone https://github.com/pete-gordon/oricutron.git out/oricutron
#   git -C out/oricutron checkout 002279fce9fa756d1d63cdc40ae97939eb7de7ed
#   tools/trace/build-oricutron.sh out/oricutron [OUT]   # OUT: out/trace
#
# Needs SDL2 (sdl2-config). The pinned commit is the one M3 was checked
# against (2026-01-23); another may build, but its traces are not that.
set -eu

PINNED=002279fce9fa756d1d63cdc40ae97939eb7de7ed

src="${1:?usage: $0 ORICUTRON_DIR [OUT]}"
out="${2:-out/trace}"
here="$(cd "$(dirname "$0")" && pwd)"
cc="${CC:-cc}"

[ -f "$src/6502.c" ] && [ -f "$src/machine.c" ] || { echo "$src: not an Oricutron checkout" >&2; exit 1; }
rev="$(git -C "$src" rev-parse HEAD 2>/dev/null || echo unknown)"
[ "$rev" = "$PINNED" ] || echo "warning: $src is at $rev, not the pinned $PINNED" >&2

case "$(uname -s)" in
    Darwin) platform=osx;   frameworks="-framework OpenGL -framework CoreFoundation -framework AppKit" ;;
    *)      platform=linux; frameworks="-lGL -lX11" ;;
esac

build="$out/oricutron"
rm -rf "$build"
mkdir -p "$out"
cp -R "$src" "$build"

# The hook: where Oricutron's own DEBUG_CPU_TRACE call is, after the
# interrupt push and before the opcode runs.
perl -pi -e '
    s/^(  cpu->lastpc = cpu->pc = cpu->calcpc;)$/  oricutron_trace(cpu);\n$1/;
    s/^(SDL_bool m6502_inst\(struct m6502 \*cpu\))$/void oricutron_trace(struct m6502 *cpu);\n$1/;
' "$build/6502.c"
[ "$(grep -c 'oricutron_trace' "$build/6502.c")" = 2 ] || { echo "the trace hook did not apply" >&2; exit 1; }

# Oricutron's errata that part the traces for good are corrected here, each
# by name; tools/trace-diff.py lists them.
#
# ORICUTRON_NO_I_DELAY, corrected: on an NMOS 6502, CLI, SEI and PLP change
# I after the IRQ poll for the next instruction, so that poll sees the old
# I (design.md §5.1; this project's m6502.c, irq_masked). Oricutron applies
# the new I at once. Left in, it moves an interrupt that falls just after
# SEI to after the next CLI, and the traces part for good (M3, 2026-10-08).
# ORIC_TRACE_KEEP_ERRATA=1 builds Oricutron as it is.
if [ "${ORIC_TRACE_KEEP_ERRATA:-0}" != 1 ]; then
    perl -pi -e '
        s/^(SDL_bool m6502_set_icycles\(struct m6502 \*cpu, SDL_bool dobp, char\* bpmsg\))$/static int i_for_poll = -1;  \/* ORICUTRON_NO_I_DELAY *\/\n$1/;
        s/^  else if\(\(cpu->irq\) && \(cpu->f_i == 0\)\)$/  else if((cpu->irq) \&\& ((i_for_poll >= 0 ? i_for_poll : cpu->f_i) == 0))/;
        s/^(  cpu->calcop = cpu->read\(cpu, cpu->calcpc\);)$/  i_for_poll = -1;\n$1/;
        s/^(  oricutron_trace\(cpu\);)$/$1\n  i_for_poll = (cpu->calcop == 0x58 || cpu->calcop == 0x78 || cpu->calcop == 0x28) ? cpu->f_i : -1;/;
    ' "$build/6502.c"
    [ "$(grep -o 'i_for_poll' "$build/6502.c" | wc -l | tr -d " ")" = 5 ] || { echo "the ORICUTRON_NO_I_DELAY patch did not apply" >&2; exit 1; }

    # ORICUTRON_BRANCH_PAGE, corrected: a taken branch costs a cycle more
    # when its target is in another page from the instruction after it
    # (PC + 2), not from the branch's own opcode. Oricutron compares the
    # opcode's address, so a branch whose opcode ends a page is a cycle
    # long: BNE at #C5FF to #C609 in ROM 1.0 (M3, 2026-10-08).
    perl -pi -e '
        s/^#define BPAGECHECK \( \(cpu->baddr&0xff00\) != \(cpu->calcpc&0xff00\) \)$/#define BPAGECHECK ( (cpu->baddr&0xff00) != ((cpu->calcpc+2)&0xff00) )  \/* ORICUTRON_BRANCH_PAGE *\//;
    ' "$build/6502.c"
    grep -q 'ORICUTRON_BRANCH_PAGE' "$build/6502.c" || { echo "the ORICUTRON_BRANCH_PAGE patch did not apply" >&2; exit 1; }

    # ORICUTRON_VSYNC_LINE, corrected: the VSync hack's pulse starts at
    # its raster's wrap, 44 lines before the picture it centres at 50 Hz
    # and 20 at 60. The ULA's sync is lines 256-259 of a counter that is 0
    # at the first active line (Brown's measurement; Clock Signal agrees),
    # and 234-237 at 60 Hz (Clock Signal); Oricutron's raster counts from 0
    # at the same cycle as this project's field. The pulse is put on the
    # sync's line, counted from the line's own cycle, not from the end of
    # the instruction that crossed it (design.md §15.2 M16).
    perl -0pi -e '
        s/\n    oric->vsync = 12 \+ 260;\n/\n    \/* ORICUTRON_VSYNC_LINE: the pulse starts on the sync line, above. *\/\n/;
        s/^(  oric->vid_raster\+\+;)$/$1\n  if(oric->vid_raster == (oric->vid_maxrast == 312 ? 256 : 234))  \/* ORICUTRON_VSYNC_LINE *\/\n    oric->vsync = 12 + 260 + oric->cpu.rastercycles;/m;
    ' "$build/ula.c"
    [ "$(grep -c 'ORICUTRON_VSYNC_LINE' "$build/ula.c")" = 2 ] || { echo "the ORICUTRON_VSYNC_LINE patch did not apply" >&2; exit 1; }

    # ORICUTRON_VSYNC_COARSE, corrected: the hack puts CB1 at the level
    # its pulse has at the start of the instruction about to run, so an
    # edge part-way into one is seen after the next; the VIA's timers,
    # clocked by the same call, are an instruction ahead instead. CB1 is
    # put at the level it has at the instruction's end, as a timer's
    # flag is, and an edge is taken at the first boundary at or after it.
    perl -pi -e '
        s/^(    )unsigned char j = \(0 == oric->vsync \|\| 260 < oric->vsync\);$/$1int k = oric->vsync - cycles;  \/* ORICUTRON_VSYNC_COARSE *\/\n$1unsigned char j = (k <= 0 || 260 < k);/;
    ' "$build/tape.c"
    grep -q 'ORICUTRON_VSYNC_COARSE' "$build/tape.c" || { echo "the ORICUTRON_VSYNC_COARSE patch did not apply" >&2; exit 1; }
fi

# Its objects only: its own link fails without the hook's definition,
# which is the driver's, and that is the one failure allowed.
make -k -C "$build" PLATFORM="$platform" SDL_LIB=sdl2 -j8 >"$out/oricutron-make.log" 2>&1 || true
# BSD make writes "*** [oricutron]"; GNU make 4 writes
# "*** [Makefile:NNN: oricutron]".
if grep '\*\*\* \[' "$out/oricutron-make.log" |
        grep -Ev '\*\*\* \[([^]]*: )?oricutron\]' >&2; then
    echo "Oricutron did not build; see $out/oricutron-make.log" >&2
    exit 1
fi

objs="$(ls "$build"/*.o | grep -v '/main\.o$')"
# Two drivers on the same objects: the trace (§13.4), and the renderer
# that draws test_golden's frames with Oricutron's ULA (§7.7).
for d in oricutron-stubs oricutron-trace oricutron-render; do
    "$cc" -O2 -Wall -Wextra -Wno-unused-parameter $(sdl2-config --cflags) \
        -I "$build" -I "$here" -c "$here/$d.c" -o "$out/$d.o"
done
for d in oricutron-trace oricutron-render; do
    # shellcheck disable=SC2086
    "$cc" -o "$out/$d" "$out/$d.o" "$out/oricutron-stubs.o" $objs $(sdl2-config --libs) -lm $frameworks
    echo "$out/$d"
done
