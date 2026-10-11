# Milestone log

What each milestone verified, on which board, on what date, and what was
not checked, newest first. `design.md` §15.2 holds each milestone's scope
and done-when criteria; this file keeps the full record.

**After M16, a load powers on as the state's machine** (`snapshot_machine`
in `src/core/snapshot.*`; `snapio_check` and `snapio_load` in
`src/port/snapio.*`; `stage_roms` and `snap_switch` in `menu.c`), on
`main` on 2026-10-10 at the owner's request (design.md §10.6): a state
for another ROM, RAM, Microdisc fit, VSync setting or pulse is no longer
refused by name, but loaded into a machine powered on as it, the ROMs
read from the card first. The core checks the field's shape before
anything a power-on changes, refuses a Microdisc on a 16K as not a
state, and moves a pulse that differs from `SNAP_OTHER_FIELD` to
`SNAP_OTHER_MACHINE`. **Checked on the host:** `test_snapshot` (our
ROM, so in CI) loads a 48K state into a 16K powered on as the state
says, and it runs 150 fields to the state its own machine reaches;
another field is refused even with the RAM different too;
`test_snapshot_rom` does the same on all four machines from the other
RAM, and from the other ROM and RAM, to the original's state 150 fields
on; `test_vsync` from the hack off, from another pulse, and the hack
off into a machine with it, each to the same interrupts and pulse;
`test_disc` with the Microdisc. **Found by the owner** on the board:
every load was refused, the message cut to "the tape or disc is", while
a Microdisc with drive A empty sat in its EPROM's boot read, busy for
ever (reproduced on the host: busy in every field of 3,000). A busy
machine is now powered on before the load, and `test_disc` holds a load
refused mid-command and taken after a power-on; the message is shorter
and the status row's width wider. **Planted bugs**, each caught and
removed: `snapshot_machine` not copying the pulse's delay fails
`test_vsync`'s load; not copying the RAM fails `test_snapshot` and
`test_snapshot_rom`; dropping the Microdisc fails `test_disc`. After
review, a second pass's refusal before anything changes no longer
powers the machine on again: only `SNAP_TORN`, a failed read of the
RAM, does (`test_snapshot`; reporting it as `SNAP_IO` fails); and a
power-on puts back the EPROM in the socket rather than reading the card
for it. Both firmware builds pass, with and without the
UART. **On the board**, by the owner on a Pico 2 W, 2026-10-10: states
saved before the change load as expected, from a machine with the
Microdisc fitted and drive A empty; and a state loads from a machine
with another ROM, another Microdisc setting and another VSync setting,
each powered on as the state's; and a state whose ROM is not on the
card is refused, the status row naming the missing ROM, and the machine
left as it was.

**M15, finish**, built on branch `m15-finish` on 2026-10-10
(Pico SDK 2.3.1, arm-none-eabi-gcc 15.2), on the Plus 2 W
`7458DC82A89AAC12` (RP2350B, chip rev 2), **done** the same day: every
done-when item is checked, the last by the owner. **Built:** game
layouts (`src/core/keylayout.c`, the overlay in `keymatrix.c`, the
built-ins and the `.map` names in `keymap_picocalc.c`;
`src/port/keymapio.*`; the Setup page's Keys row, the main and Help
pages, Save settings, and a tape or disc choosing the layout its file
names); the SWD counters (`g_swd` in `handoff.*`, `swd_update` in
`core0.c`, `tools/swd-counters.py`); `test_soak --write`, which saves
`soak.bas` as `SOAK.tap` with `CSAVE"SOAK",AUTO` for the release build's
soak; `README.md` with the ROM names, hashes and sources. **The
built-ins, chosen by survey** (`tools/layout-survey.sh`, `oric-corpus -k`;
design.md §9.4): of TOSEC's 973 tapes that load on the Atmos 48K, 47
test particular keys from their own code; ZX (Z X ' /) is Centipede's,
with Zebbie, Probe 3, Grendel and Chuckford on Z and X, AZ (A Z , .) Mr
Wimpy's, and QAOP a few BASIC games'; QAOP was the design's guess, and
no machine-code title in the set reads it. **Checked:** `test_keymap`
holds the overlay and the parser (a planted bug, the layout's lookup
skipped, fails seven checks); `test_keyboard` types each built-in's
arrows through both ROMs, the standard map the control; the README's
settings example parses to the defaults; `SOAK.tap` loads and runs by
itself in `oric-corpus`; on the board, `swd-counters.py read --screen`
reads the block and the text screen of the release build at Ready, every
failure counter 0; CI green on both jobs for the branch (run 38073809982). **Found:** the AY's write count includes port A, which
the keyboard's scan writes at every column, so it grows with the guest
idle. M12's soak record cites the AY written in every heartbeat; that
held with or without the program, and the program's running was shown by
its count rising in the screen dumps. The SWD soak and `test_soak` now
tell the program by its envelope starts, one a pass, against a control
stopped at an error (6 starts there, the ROM's key clicks and error; 18
in the program's run). **The power source** is no longer a condition of
a soak (the owner, 2026-10-10): `soak-check.py` reports the gauge and no
longer fails a run that shows charging. **Measured**, in one sitting at
the card's machine, an Oric-1 16K at Ready (`out/m15/perf/`): core 0
30.9–31.1 % busy on the release build and 30.9–31.0 % on the
development build, six SWD reads 5 s apart each, rt 0.9998, no
underruns; the release image 177,672 bytes text and 309,532 bss, a
348,160-byte `.uf2`, against 180,700, 311,608 and 353,792 with the UART.
**States carry their media** (version 2, design.md §10.6), at the owner's
request: each drive's file and the deck's, with its place, put back
after a load. `test_snapshot` holds the section to the CRC, its paths to
their fields, and version 1 states loading as before. On the board, the
Atmos 48K with the Microdisc (`out/m15/media/`): Sedoric booted off the
card, saved to slot 4, drive A emptied, slot 4 loaded (160 ms), drive A
back and `DIR` listing its 52 files; with a tape in the deck too, both
came back from the state, the tape at its place. **Found on the way:** a
load refused as busy (a disc command waiting on an empty drive) was taken
by the port for one that had changed the machine, which it then powered
on again; `snapio_load` now refuses a busy machine before the card is
touched, and on the board the same load was refused with nothing
changed. A disc missing from the card was not tried on the board. The
owner decided against a checksum of each disc in a state (2026-10-10).
**The release soak**, run by the owner on the Plus 2 W the same day
(`out/m15/soak/swd-20261010-170642.log`), the build at `49ff740`,
`SOAK.tap` loaded from the card: 181 samples over 30.0 minutes, one
boot, rt 1.000 at its lowest, every failure counter 0, the card's
changes and the parks unchanged from the program's start, the envelope
started in every window (13 to 546), the count rising from 5 to 534
over 166 of 180 screen dumps, core 0 38.8–39.6 % busy, the audio
consumed at 36,618–36,624 Hz, the gauge at 93 % and never charging, the
die at 20 °C. H and J were not pressed in it, so it failed that rule
alone. The owner then sampled the same boot again for a minute, holding
both (`swd-20261010-174204.log`, which passes `check --minutes 0`): the
block's updates and the board's clock run on from the first log's last
sample (317 updates in 322 s), the count from 534 to 632 and on to 647,
the screen dumps show 1 H and 5 J, and 33 key events came from the
PicoCalc. The two logs checked as one (`swd-20261010-combined.log`) fail
only for the 5.4 minutes between them that nobody sampled, over which
the counters, cumulative from boot, stayed 0 and rt held. Taken as the
release soak passed. **The owner checked the menus on a Pico 2 W** the
same day, and they are right: the build runs on the second board.
**The owner's check** on the Pico 2 W, the same day: the menus' pages,
a game layout chosen on the Setup page and working, and the settings
saved, the PicoCalc power-cycled and the settings read back at boot.
**Not measured:** the release build's core 0 share under §14's
workloads, since `perf-run.sh` types them over the UART the release
build does not have; at idle Ready it matches the development build's
(above), and the soak's program ran at 36.8–39.6 %.

**M16, the vertical-sync modification** (`src/core/vsync.*`; the run
loop's slice and the bus's catch-up in `oric.c` and `bus.c`; the player
off CB1 in `cassette.c`; `oric_config_t`'s `vsync_hack` and the pulse's
four numbers; the hack and the pulse in `snapshot.c`; the `vsync_hack`
setting; the Machine page's row, the Tapes page's note and the refusal
by name in `menu.c`; `PICO_ORIC_BOOT_VSYNC`; the heartbeat's `vsync`
line; `test_vsync`; `oric-trace -v`, `oricutron-trace -v` and `-V`, the
key script's `poke`, `trace-diff.py run --vsync` and `--test`, `tape
--vsync`, `oric-corpus -v`), built 2026-10-10 (Pico SDK 2.3.1,
arm-none-eabi-gcc 15.2) and run on the Plus 2 W `7458DC82A89AAC12`
(RP2350B, chip rev 2) the same day, **done**: every done-when item is
checked, the last by the owner, who played Oricium (Defence Force, 1.2),
a game that wants the vsync cable, with the hack on a Pico 2 W the same
day, and it works. **The first active line, settled** for 50 Hz from
Brown's measured video timing (sheet 2 of his ULA guide): the picture is
lines 0–223 of the counter and the sync 256–259, so the field's boundary
is line 0 and the sync begins 256 lines after it; Clock Signal's
`Video.cpp` agrees, and gives 234–237 of 264 at 60 Hz, taken until
measured (design.md §16). The pulse on CB1 is Oricutron's, 12 µs after
the sync begins, for 260 µs; the modification's own drawing was not
found. The field and the ULA's count both start at the first
instruction, after the reset's seven cycles; Oricutron's raster does
too. **Found on the way, in the VIA**: a read in an instruction's last
cycle caught the VIA up a tick past the instruction's end (M3's
`io_at + 1`), and a write in its last cycle did the same, so a flag
that tick set was taken by this instruction's IRQ poll. A loop polling
T1's flag, and one writing T2's latch seven cycles a turn, each parted
from Oricutron at their first interrupt on that tick; the boot and
typing traces never showed it. A read now catches up to its cycle, a
write a tick further, and the run loop holds back what a write's tick
past the boundary set (`via_late`); both loops then agree with
Oricutron to the end (968 interrupts in the write loop), and the four
boot traces are unchanged (design.md §5.3, EL §4.3). **Checked on the
host** (`test_vsync`, no ROM, so CI runs it): each edge's cycle found by
a loop polling the CB1 flag entered at all nine phases of its nine-cycle
loop, the brackets meeting in one cycle, for the fall and the rise in
two fields at 50 Hz and at 60 (a 60 Hz attribute found by the mode
scan); the controls, the fall a cycle late (delay 13) and the rise
alone a cycle late (width 261), each move the found cycle by one; off,
no pulse; on, a tape played by hand drives CB1 not at all, eight changes
in four fields, where without the hack every edge is CB1's; an
interrupt handler entered at the same instructions by oric_run_field's
long slices and by single steps, both edges, 40 fields; a state saved
with the hack resumes the same pulse and the same interrupts, is
refused without the hack (named) and by another pulse, and one without
it is refused with it. `test_bus`: T1's flag first seen by a polling
read the latch + 2 after its write at all nine phases, and its
interrupt across a VIA write taken at the first boundary at or after
the flag at all seven phases. **Traced line for line against
Oricutron** (its VSync hack on, `ORICUTRON_VSYNC_LINE` and
`ORICUTRON_VSYNC_COARSE` corrected, design.md §13.4) on all four
machines: a test program poked into RAM waits on CB1's fall ten times
with interrupts masked, then installs a handler ahead of the ROM's and
counts the rises, about 180 interrupts a run, 3.3–3.4 M instructions;
the fall a cycle late, the rise kept, parts the traces on both ROMs,
and so does the rise a cycle late. With the hack off the four boot
traces and the goldens are unchanged. **Planted**, each failing a test
and then removed (each file recompiled by deleting its object: an edit
restored inside the same second as a build was left stale, which spoiled
a first run): no slice end at the sync; no catch-up at a VIA read; reads
a tick past the boundary again (`test_bus`); the tape still on CB1; no
restart on load; no 60 Hz line at the boundary; the rise's field from the
power-on length; the snapshot ignoring the hack; the late flag not held
back (`test_bus`); the snapshot keeping no pulse shape. **The archive**
(`out/m16/corpus`): the three titles M12 saw enabling CB1's interrupt
outside the ROM, all on 1.0, are not the modification's: Star crashes
into data with or without it, as do Arpy's MYM player and Zoom.cod, a
part of Oric Demo 1. All 1,061 tapes run on both 48K machines with the
hack and without, screens compared: on 1.1 one title needs it, Dbug's
DOTSRECORD, "Proof Of Concept #1, Double buffered sinus Dots @50hz"
(2006), which says "No input, Please connect Vsync" without it and
"Working: 836 - 1172" with it; Oricutron, loading it by the signal and
then turning its hack on, says "836 - 1171", with its own pulse position
and with ours, so the figure does not tell the line. Three more, Dig Dog
(Oric-1), Galaxy and SEAWOLF, have loaders of their own on CB1 and
follow the sync instead of the tape, as on a modified machine; on 1.0
two (JMOVIE2, Arpy) differ, both already running into data.
**On the board** (`out/m16/board`): `PICO_ORIC_BOOT_VSYNC`, the Atmos 48K,
one pulse a field in the heartbeat (11,045 in 11,045 fields), rt 1.000;
the test program's install typed over the UART as BASIC (`vsync-irq.bas`)
counts 47–51 CB1 interrupts a second at `Ready`, IER `#D0`. DOTSRECORD
was not on the card then (`cannot open`), and was copied to it
afterwards as `/oric/tapes/DOTSRECORD.tap`. **The owner's check**: Oricium
1.2 (`Oricium12.tap`, SHA-1 `675c4669…`, not in TOSEC, which predates it)
played with the hack on a Pico 2 W, the first run of this firmware on a
second board. On the host (`out/m16/oricium`) the Atmos 48K without the
hack stops at "PLUG VSYNC CABLE OR PRESS A KEY", and with it goes
straight to its menu.
**Measured**, one sitting, M12's workloads on the card's Oric-1 16K with
the hack on (`build/m16-vsync`) against the same tree without it
(`out/m16/perf`): idle 31.5% against 31.4%, compute 37.8% and 37.8%,
sound 38.8% against 38.7%, scroll at 60 Hz 36.0% and 36.0%, rt 1.000
and no underruns in all. Image 172,848 bytes text, bss 308,468.
The owner set the hack on the Machine page, with the Microdisc off, and
did not save the settings (the card's file has no `vsync_hack` line), so
the row and *Apply and restart* are checked on the panel. **Not
checked**: DOTSRECORD on the board; the Tapes page's note and the
snapshot refusal on the panel; anything at 60 Hz against a measurement,
or against Oricutron, whose trace never leaves 50 Hz; the release build
on the board.

**M14, the Microdisc** (`src/core/wd1793.*`, `microdisc.*`, `mfmdisk.*`;
the latch's map in `oric.c` and the decode in `bus.c`; the run loop's
slice cut moved from the cassette to the machine; the overlay RAM's
power-on pattern; the Microdisc's fields in `snapshot.c`; `discio.c`,
`PARK_DISC`, the Discs page, the Machine page's row, `boot_disc` and
`microdis.rom` off the card; `test_disc`; `oricutron-trace -d` and `-p`,
`trace-diff.py disc`, `oric-discs` and `tools/disc-corpus.sh`), built
2026-10-10 (Pico SDK 2.3.1, arm-none-eabi-gcc 15.2) and run on the Plus 2
W `7458DC82A89AAC12` (RP2350B, chip rev 2) the same day, **built**: every
done-when item is checked, on the host and on the board, and the owner's
check of the pages on the panel is outstanding. **Read off the
EPROM and agreed by MAME and Oricutron**: the latch's bits, `#0314` and
`#0318` read as INTRQ and DRQ in bit 7, the EPROM over the overlay RAM
from RESET, the WD1793 at 1 MHz (design.md §10.5, §16). **The disc
turns**: a byte every 32 cycles of a 6,400-byte track, each step an event
the run loop stops at; CRCs not checked (24 of TOSEC's 221 images carry
wrong ones). **Found on the way**: Sedoric's loader sums the overlay RAM and
takes zero as the DOS resident, so zero-filled RAM booted four sectors of
sixty and crashed, and Oricutron does the same with its RAM zeroed; the
overlay now powers on in Oricutron's pattern, the rest still zeroed, and
the trace diff agrees to the end on all four machines after it. **Checked
on the host** (`test_disc`): the CRC's known answer; the format's index
and refusals; Restore, Seek with verify, Read Sector (one, with E, with
side compare, multiple), Read Address, Read Track, Write Sector (with a
deleted mark), Write Track (a Sedoric-like format read back with good
CRCs) and Force Interrupt's three forms, each INTRQ at the cycle the
rotation says, with controls: the slowest step rate, a track register
that lies (SEEK ERROR at the fifth index), a CPU too slow for the disc
(LOST DATA), one that writes nothing (the sector untouched), write
protect, a track served late; the latch's map, the decode, INTRQ through
bit 0 and the reset vector on made-up images; snapshots round trip,
refused mid-command and by the other fit. With the ROMs and Sedoric 3.006
(by SHA-1): the disc reaches Sedoric's menu 4.39 M cycles after power-on
in the Atmos 48K and 4.47 M in the Oric-1 48K, 77 sectors read; then in
both, SAVE, DIR, NEW, LOAD and RUN ("DISC OK"), a disc swapped between
fields seen and swapped back, DEL, SAVE refused on a protected disc with
the image untouched, a snapshot refused mid-command and one taken between
that replays the same DIR; INIT formats a blank image in drive B, 84
tracks in 1,774 fields (35 s), and a file saved there lists. The program
saved by the Atmos, read by Oricutron (`trace-diff.py disc`), loads and
prints DISC OK. **Planted**, each failing `test_disc` and then removed: no
LOST DATA on read; RNF at the fourth index; the 2 MHz settle; the 2 MHz
step; INTRQ with a read's last byte; `/ROMDIS` inverted; `#0314` and
`#0318` swapped; no side compare; F5's CRC preset wrong. One that passed
marked dead code, a put before a get that the end of every write already
posts, and it was taken out. MAME's gating of `#0314` by bit 0 fails only
our own unit check: Sedoric cannot tell. **The archive** (`tools/disc-corpus.sh`,
`out/m14/corpus`): of TOSEC's 221 images, 15 refused by name (10 cut short,
5 not `MFM_DISK`, one `ORICDISK`); of the 206, the Atmos 48K leaves the
same screen as Oricutron after 30 s on 193; of the other 13, ten are
animations caught at another moment, Le Masque d'Or a blinking cell in
another phase, Creepy Castle still loading (the same at 60 s), and FTDOS,
a Jasmin disc, on which the EPROM seeks with verify to track 116 and our
chip gives SEEK ERROR where Oricutron ignores the verify. 26 discs whose
loader is a slave's (`#B901` = 1) print "DOS is altered" and run into
junk, as they do in Oricutron; Pulsoids (F) uses two undocumented
opcodes. **On the board** (`out/m14/board`): microdis.rom found by its SHA-1; with
`PICO_ORIC_BOOT_MICRODISC`, the Atmos 48K and no disc, the EPROM boots and
shows "insert system disc", its own prompt for an empty drive, in its DRQ
loop at `#E2EF`. Then with Sedoric 3.006 copied to `/oric/discs/` and
`PICO_ORIC_BOOT_DISC`: Sedoric's menu, 77 sectors in 6 tracks, each read
off the card in 22.9-23.4 ms with the guest parked; over the UART X,
`SAVE"BOARD"`, `NEW`, `LOAD"BOARD"`, `RUN` (BOARD DISC OK) and
`DIR"BOARD"`; tracks put back in 39-45 ms; rt 1.000 and no underruns
throughout. The image, copied off the card, differs from the original in
side 0's track 20 and side 1's track 41 alone, the two tracks the log shows
put back, and Oricutron (`trace-diff.py disc`) loads BOARD from it and
prints BOARD DISC OK. At Sedoric's prompt core 0 was 42.2% busy, against
34.2% for the Atmos 48K's BASIC in the same sitting: Sedoric flips the ROM
in and out through `#0314` about every 15 instructions while it waits for
a key (`#0477`), each a remap of 64 pages. A latch write no longer ends the
run slice, the map is rebuilt only when bits 1 or 7 change, and the access
path and the events are in SRAM: 41.4% (headroom 2.43x). **Measured**, one sitting, M12's workloads on the
Oric-1 16K the card's settings choose, against main as the control
(`out/m14/perf`): idle 29.8% against 30.2%, compute 35.8% against 36.4%,
sound 36.8% against 37.4%, rt 1.000 and no underruns in all; the
Microdisc costs nothing while off. Waiting in the EPROM's DRQ loop, which
is the loop every byte moves through, core 0 is 48.5% busy at rt 1.000
(headroom 2.13x), as a paced tape load was (M13). Image 170,856 bytes
text, bss 308,436 (274,900 before: two EPROM copies and a track).
**Not checked**: the Discs page, the Machine page's row and the status
line on the panel (the boots used the build's flags, not the menu); a
disc swapped or a protected one on the board; INIT on the board; the
release build on the board; the Oric-1 48K with Sedoric on the board.

**M13, signal-level tape** (`src/core/cassette.*`; the trap's cues in
`tape.c`, `oric_tape_reading`; the run slice's end in `oric.c` and the
catch-up in `bus.c`; `tapeio.c` with fast tape off; turbo, the deck's log
line and `g_c0.deck` in `core0.c`; the Tapes page's Play row;
`test_cassette`; the corpus runner's `-t`), built 2026-10-09 (Pico SDK
2.3.1, arm-none-eabi-gcc 15.2) and run on the Plus 2 W `7458DC82A89AAC12`
(RP2350B, chip rev 2) the same day, **built**: every done-when item is
checked, on the host and on the board, and the owner's check is
outstanding. **The writer, read off both ROMs and executed**: T1
free-runs with its output on PB7, and each half-cycle is one T1 period,
the latch + 2, in the order the writer sets it, 210 or 418 cycles; a byte
is a period and thirteen bits; 259 `#16`s; between header and data six
periods more than between bytes in 1.1, and in 1.0 three plus one for
every eleven letters of the name past nine, its print being 564 cycles
and 19 a letter (design.md §16). **Checked on the host**: each ROM's
`CSAVE`, recorded off PB7 with every edge placed by T1's count, equals
the player's walk of the trap's `.tap` of the same save, edge for edge,
fast and slow, and in 1.0 for names of 0, 9, 10, 20 and 21 letters
(8,345 to 47,460 halves a case); `CLOAD` with the trap off reads the
signal on both 48K machines, fast and slow, at both polarities, a tape of
two files by name and in order, the deck holding its place while the
relay is open; the recorder's file is the trap's, byte for byte, at both
speeds, and one cut off by RESET in its data is dropped and counted; with
fast tape off the find and the header write are the only requests, and a
port stand-in serves them; with the tape at its end a `CLOAD` leaves the
ROM in its reader, which at Ready it is not. Without the ROMs, so that CI
runs it, the CB1 flag is seen by the first IFR read at or after its edge.
**Planted**, each failing `test_cassette` and then removed: 1.1's gap 5;
1.0's slack 10; the player's parity, and the recorder's; the leader 258;
no relay; no catch-up before a VIA access (this one the ROM's own load
survives, and only the CB1 case catches); the reader's range cut short.
Both recordings of each ROM, written by `test_cassette --write`, load and
run in Oricutron (`tools/trace-diff.py tape`, its tape traps off, a fast
`CLOAD`: Oricutron plays any `.tap` fast, so its `,S` cannot read one).
The trace diff agrees to the end on all four machines after the change to
`bus.c`'s reads. **The archive**: of the 57 tapes whose bytes hold a `JSR`
to the ROMs' tape-reading routines or a read of the VIA's IFR, L'Immonde Dr Kokus (Jean Francois
Fabre, 1986) loads only by the signal: by the trap its own loader, which
calls 1.1's sync routine, stops at "Je cherche"; by the signal it loads
and plays, its first room drawn (host, 20,000 fields, looked at). Not a
faster format than the ROM's: a loader of its own. All 1,061 tapes run on
the Atmos 48K by the signal for 20,000 fields (`out/m13/corpus-signal`):
ten end still reading, against one by the trap at 3,000; four of them
looked at finish given 60,000 fields, long tapes (Defense Force's is 59
KB). **On the board**, the Oric-1 16K the card's settings chose, fast
tape off from the menu over the UART: `CSAVE"M13SIG"` recorded 65 bytes,
one whole file, appended to `/oric/tapes/M13SIG.tap`; `NEW`,
`CLOAD"M13SIG"` off the signal, `RUN` printed SIGNAL OK; `CLOAD"M13SIG",S`
the same; `CSAVE"BOARD"` recorded 73 bytes, rebuilt from the log and
`CLOAD`ed in Oricutron, which ran it; both recordings on the card,
read on the workstation, are the bytes the log showed. Then, the card
holding the archive's tape as `KOKUS.tap` and the machine switched to the
Atmos 48K from the Machine page (on the host the Oric-1 16K loads it as
garbage): `CLOAD"KOKUS"` off the signal loaded its three files, the
second and third fetched by its own `CLOAD""`, the last 174.0 M cycles in
83.6 s (2.08×); the ROM then read on past the tape's end, which the port
rewound once, as designed, and the relay opened. Its title page and
instructions came up ("L'IMMONDE DR KOKUS", "BONNE CHANCE,JAMES"), and
Space took it to hires, at rt 1.000 with no undocumented opcode
(`out/m13/board/kokus.log`). **Measured**, one sitting each: a
slow `CLOAD` of 14.0 M cycles ran at 2.03× real time with turbo, core 0
98.6% busy; paced (`PICO_ORIC_TURBO=OFF`, the control) at 1.00× with core
0 48.2% busy, which is turbo's ceiling. A recording ran at 1.29×. The
first build reached 1.43×; two changes, each held by the tests and the
trace diff, took a field of loading on the host from 79 to 60 µs: no
re-wiring after a VIA read that changes nothing (IFR, the counters' high
bytes, the latches), and the run slice ending at the deck's next edge
instead of a call every instruction. Idle, the deck costs 0.6–0.8 points
of core 0 against its control (`PICO_ORIC_DECK=OFF`, same sitting, M12's
workloads: idle 30.2% against 29.6%, compute 36.3% against 35.6%, sound
37.3% against 36.5%). Image 155 KB text, bss up 64 KiB for the signal's
image (design.md §3.3). **Not checked**: the game played past its first
room; the Atmos's own CSAVE by the signal on the board; PLAY by hand, the Tapes
page's Play row and the status line's "playing" on the panel; a tape over
64 KiB played a window at a time (no test, and none in the corpus run,
which inserts the tape whole); a ROM left reading an ended tape on the
board (the corpus runner models the port's rule); a recording kept for a
missing card; the release build. A file one byte short does not load by
the signal: the ROM waits for its last byte, as a real one does (§10.4).

**M12, performance, corpus and soak** (`tools/fetch-corpus.sh`,
`tools/corpus/oric-corpus.c`, `tools/corpus-run.sh`,
`tools/corpus-summary.py`; `tools/soak.bas`, `tools/soak.sh`,
`tools/soak-check.py`, `test_soak`; `oric_tape_load_keep` and the name
rule in `tapeio.c`), built 2026-10-09 (Pico SDK 2.3.1, arm-none-eabi-gcc
15.2) and run on the Plus 2 W `7458DC82A89AAC12` (RP2350B, chip rev 2)
the same day, **done** 2026-10-09. **By the owner** on a Pico 2 W,
2026-10-09, with the release build: the seven archive tapes put on the
card for the two tape rules all loaded, Car Maniac, Dedal, Digger and
Mr. Wimpy finding their parts by name, and Fire Flash, TrickShot and
Hyper Olympics with their last byte missing. **The corpus**: TOSEC's *Tangerine Oric 1
and Atmos* (2012-04-23), from the Internet Archive, its zip checked by
SHA-1: 1,061 titles on tape, of which 39 are in parts and kept as a
directory of their files, and 221 disc images for M14. Each title is
loaded on the host with `CLOAD""` on the Atmos 48K and the Oric-1 48K,
served as `tapeio` serves the card, and run for 3,000 fields (a minute),
its last file started with RUN or CALL if it did not autorun and Space
pressed half way; every undocumented opcode is counted by replaying the
field that trapped it an instruction at a time, with the 24 instructions
before the first; both machines in under two minutes
(`out/m12/corpus/report.txt`, a frame and text screen per title beside
it). Frelon, 3D Fongus, Car Maniac, Fire Flash, Mr. Wimpy, Dedal and
TrickShot's frames were looked at, each at its title or in play. **What
it found**, before any change: 879 titles loaded on one machine or the
other. 50 are not tapes (ROM images, ELF tools, an Amiga program and an
`MFM_DISK` filed as TAP). 83 end exactly one byte short of what their
header says, the data start to end exclusive, which the trap gave up on;
Oricutron's `tape.c` allows the byte "for broken tape images". The
titles in parts ask for the next part by file name, `CLOAD"AIRFOX.TA1"`
or `"IOD2.TAP"`, as Euphoric served them, where `tapeio` looked only for
`NAME.tap`. **At the owner's choice** (2026-10-09) `tapeio` now looks for
the name as it is, then `NAME.tap`, and a file one byte short loads with
the last address keeping its byte (`oric_tape_load_keep`); with both,
974 load on the Atmos and 977 on the Oric-1, 980 on one or the other.
Of the 81 that load on neither, 50 are not tapes; the rest give up on a
name not there (some in parts are numbered wrongly: Airfox's `.ta1`
carries the header `AIRFOX.TA2`, which its loader's name check refuses),
or are cut short by more than a byte. `test_tape` loads a file one byte
short on all four machines: kept, the ROM's own load ends, every byte but
the last loaded and the last address unchanged; not kept, the control,
the ROM waits; with the keep planted out, it fails on all four. The name
rule is not on the host (`tapeio.c` is the port's); the corpus runner
models it. **Undocumented opcodes**: 62 titles execute one on the Atmos,
198 on the Oric-1. The instructions before each title's first show none
on purpose: a corrupt dump (Harrier Attack, where what looks like an
`STA`'s `8D` read as `09` throws the stream onto an `#80`), returns and
jumps into data (Arena 3000 [b] by RTI, Video Flipper by RTS, Oricqs [b],
Snake Venom), the ROM's autorun of a data file (`JMP ($02A9)` at `#E8E5`:
Godilloric, Strip 21, Diamant), an Oric-1 title calling 1.0's ROM on the
Atmos, or the harness CALLing a data file. Archeron's Rage loops through
`#7A`, which the trap's one-byte two-cycle NOP runs as an NMOS 6502 does.
**Decided by the owner** (2026-10-09): the stable subset is not
implemented (design.md §5.1, §17). Three titles enable the CB1 interrupt
themselves, all on the Oric-1: Star, Arpy's MYM Player and Zoom.cod
(M16). The largest single file on any tape is 49,152 bytes, the largest
tape 92,160 (design.md §3.4). **Performance**, one sitting on the board,
eight workloads (`tools/perf-run.sh`) on three builds, all on the Oric-1
16K the card's settings chose (`out/m12/perf-summary.txt`): the shipped
tier 2 at 29.3 % of core 0 idle to 36.3 % for the AY loop, now the
heaviest; tier 0, the control for the hot code in SRAM, 33.1-51.6 %,
so tier 2 saves 3.8-15.8 points and is kept; the tape trap compiled out,
the control for its check per instruction, 0.5-1.2 points under tier 2.
Every run: rt 0.999-1.000, no dropped snapshot, no underrun or late
refill, 36,624 Hz consumed. The tiers' symbols were checked with
`arm-none-eabi-nm`: `m6502_step`, `oric_run_field`, `bus_read_slow` and
`via6522_tick` at `0x2000…` in tier 2 and `0x1000…` in tier 0. **The
soak**: `tools/soak.bas`, a text page with serial attributes and a hires
one, the AY's three channels with noise and an envelope, and KEY$
printed with a count, run first on the host by `test_soak` (the count
rises, H and J read, hires drawn, the AY written; a misspelt line, the
control, shows no count). On the board, 2026-10-09, 30 minutes from
18:42 (`out/m12/soak/soak-20261009-183949.txt`), on the Oric-1 16K,
firmware built from the tree committed as `b58efb3` (it reports
`d902e08-dirty`): 386 heartbeats over 32.4 minutes, one boot, rt 0.999
minimum, 0.9996 mean; late fields, slips, dropped snapshots, keys lost,
I²C errors, undocumented opcodes, log lines dropped, underrun samples,
late refills and the core's sample overflow all zero; no park and no
card change after the program started; 323 keys typed over the UART, the
screen dumps showing the program reading H 15 times and J 12, its count
rising from 19 to 523 over 27 of 29 dumps; the AY written in all 359
heartbeats after the start; core 0 busy 30.0-37.5 % but for one
heartbeat at 54.9 % (266.6 host cycles an instruction against about 180,
rt 0.999, nothing late; not explained); the gauge at 96 %, never
charging. The owner says the Pico cannot tell battery from USB power
(2026-10-09). About four fields a pass run at 60 Hz: as `HIRES` switches
the mode, the ULA briefly reads character-set bytes, some of them 60 Hz
mode attributes; taken as the Oric's, not checked against Oricutron.
**Not checked**: the workloads and the soak on the Atmos 48K, the
power-on default; the name rule and the byte kept on the board; titles
past their first minute; whether any title reads only a joystick
(design.md §17); the release soak over SWD (M15).
The owner's check did not capture a log: the release build has no UART.

**M11, snapshots** (`src/core/snapshot.*`, `oric_restored`;
`src/port/snapio.*`; the Snapshots page in `menu.c`; `test_snapshot`,
`test_snapshot_rom`, `snap_util.h`), built 2026-10-09 (Pico SDK 2.3.1,
arm-none-eabi-gcc 15.2) and run on the Plus 2 W `7458DC82A89AAC12`
(RP2350B, chip rev 2) the same day, **done** 2026-10-09. **By the
owner** on a Pico 2 W, 2026-10-09, from the PicoCalc's own keyboard: a
game loaded from tape, a snapshot saved while playing it, the PicoCalc
power-cycled, the snapshot loaded, and the game played on. The format is pico-ace's with the
Oric's fields (design.md §10.6): "PORCSNAP", a 320-byte state section,
then all 64 KiB of `oric_t.ram`, 65,876 bytes for either RAM fit. Besides
§10.6's fields it carries the CPU's delayed I, the VIA's input latches,
the AY's bus and every generator's next tick, and the tape trap's kept
header, since a field boundary can fall between a trapped CSAVE's header
and its data. **Checked on the host**: `test_snapshot` on our own test
ROM, so CI runs it, and `test_snapshot_rom` on all four machines with a
BASIC program playing the AY's tones, noise and envelope while it
scrolls. In each, a machine saved part-way (where the VIA's IRQ is
asserted and not yet taken, and in `test_snapshot` with the AY held in a
write to the envelope's shape, the input latches set and an NMI
pending in its own case) and restored into a machine doing something
else is the saved one, every field compared from `oric_t` rather than
from the format, both straight after the load and after 150 fields
more, with the same AY writes and the same samples, sample for sample
(the original's sample grid restarted at the save, the DC blocker off
in both). The controls: a machine not restored differs, and a budget
one cycle out does not meet. The restored program finishes and the
machine takes typing. A CSAVE saved between its header and its data
asks, restored, for the same file; with the kept header cleared, the
control, no file is asked for. Refused, the machine untouched: a flipped
bit, a short file, a state torn in its state section, wrong magic, a
newer version, another payload length, a Microdisc, a RAM fit no Oric
has, another ROM, the other RAM fit, another field shape, a load with a
tape request waiting; a failed write is reported. The refusal says the
machine the state is for (`snap_info_t`). **Planted bugs**, forty in
three rounds, each caught by one test or both. Of the first round, five
passed both and were dealt with: the IRQ lines and the NMI line's level
and RESET pending were stored but never matter at a boundary (the lines
are now worked out from the VIA on load, the other two dropped); the
VIA's latches were zero in both machines, and the held write had just
restarted the envelope itself, so a second restart was invisible (the
test now sets the latches and runs the envelope on first); leaving out
the AY's remix showed only in the samples, which were not compared then;
and leaving out the wiring on load was hidden by the test ROM's
`JMP idle`, where an IRQ taken one instruction late lands at the same
place and phase, and in the ROMs converged within the 150 fields, so
both tests now also compare straight after the load. **On the board**,
over the UART (`out/m11/board1.log`), the card's settings booting the
Oric-1 16K: a FOR loop playing SOUND and PLAY and printing its count,
saved to slot 1 from F3 at 80 (281 ms), run on to 158, broken into with
NEW and PRINT typed, then slot 1 loaded (142 ms, both passes): the field
count went back and the program counted on from the saved point. The
Machine page restarted as the Atmos 48K, whose load of slot 1 was
refused, "slot 1 is the Oric-1 16K's, ROM basic10.rom", in 78 ms; back to
the Oric-1 16K, the load again (142 ms), and the first heartbeat after
each load counted 124,994 AY events a second. The program ran on to 500
and stopped, as the owner saw on the panel, with "?ILLEGAL QUANTITY
ERROR IN 20": the test program's own `SOUND 2,500-I,8`, which goes
negative at 501. No dropped snapshots, no underruns; real-time ratio
0.999 to 1.003, 1.000 in 34 heartbeats of 40, the others around the
parks. After the error the noise, left playing by PLAY at period 1, is
stepped every AY tick, 125,000 events a second (1 MHz / 8), and core 0
goes from 30 % to 61 %: M8's event-by-event path, not this milestone's.
**Measured**: save 281 ms, load 142 ms, refusal 78 ms (the check pass
alone). The image is 140.4 KB of text and 208.8 KB of bss (M10: 133.4
and 207.1). The state and a 256-byte piece are static in `snapshot.c`,
not on core 1's 2 KiB stack. **Not checked:** Delete, and a refusal, from
the PicoCalc's own keyboard (over the UART only); the `.new` recovery after a
cut publish, and a load with the card pulled between the passes (both
pico-ace's code, unchanged), on the board; a full card.
**Review fixes** (Codex's review of PR #11, 2026-10-09). Two findings,
one taken. (1) **A file the emulator could not have written**: a state
passes its CRC whoever wrote it, and the chips trust their fields. An AY
period of 0 divides by zero; a register number past 15 indexes past the
AY's masks; and, found while fixing it, a T1 far below zero, an AY clock
far behind the CPU's or a large budget keeps core 0 in one loop for
minutes. `plausible()` now refuses, as not a save state, any field
outside the range a running machine keeps it in: the AY's periods (1 to
what a register makes), register, envelope step and direction, prescaler,
tones and LFSR; the VIA's counters; the AY no more than a second behind
the CPU and never ahead, its next ticks within 2^32 of its clock; the
budget from -64 to 0; the ULA's modes. Every state the tests save passes
it. `test_snapshot` refuses ten such files, each with a good CRC; with
the check planted out it accepts every one. (2) **The blink period**
(`cfg.blink_fields`) compared like the field's shape: declined. It sets
which frames show blinking cells and nothing else, so no machine state
depends on it, and it is configuration only until §16 settles it; a
build that settles it should not refuse every earlier state.

**M10, tape by trap** (`src/core/tap.*`, `tape.*`; `src/port/tapeio.*`;
the Tapes page in `menu.c`, the tape's park, `boot_tape`; `test_tape`;
`tools/trace-diff.py tape`), built 2026-10-08 (Pico SDK 2.3.1,
arm-none-eabi-gcc 15.2) and run on the Plus 2 W `7458DC82A89AAC12`
(RP2350B, chip rev 2) the same day, **done** 2026-10-09.
**By the owner** on a Pico 2 W, 2026-10-09: tapes load; but a
`CLOAD"BOB"` with no BOB on the tape went round the tape and never
stopped. As designed (EL §8.2): the deck rewound once, then declined,
and the ROM waited for a signal on "Found B", its last message, until
the reset button. At the owner's choice it now gives up instead
(`oric_tape_give_up`): the end of the tape a second time, no tape, no
card, or a file cut short presses the reset button, back to Ready with
the program kept, and the status line says "BOB is not on TAPE01.tap".
`test_tape` gives up on an empty deck and requires Ready and the
program's bytes; with the reset button planted out it fails. On the
board (`out/m10/bob2.log`): TAPE01 played twice, given up, Ready, and
the program typed before ran. **Archive tapes**, put on the card by the
owner and loaded on the Atmos 48K with `CLOAD""` from the Tapes page
(`out/m10/archive.log`): SNAKE (BASIC, 696 bytes) to Ready; Frelon,
three files, each autorun loader calling CLOAD for the next, to its
title, "Pressez une touche pour jouer"; Oricium12, three files to 38,360
bytes, to "PRESS A KEY"; demospace, 35,652 bytes, to its hires screen.
No undocumented opcodes, no dropped snapshots; four heartbeats of 24
read rt 0.999, around the long loads' parks, the rest 1.000. Both ROMs' tape code read with da65
(design.md §16, now settled): the trap is on four steps per ROM (find a
header, read the data, write the header, write the data) and resumes
inside the ROM's own loop after the last byte, so "Searching..",
"Found", the name compare and looking on, the type checks, the re-link,
autorun and verify's message are the ROM's. It stands aside unless the
ROM is a stock image by SHA-1 and its page is ROM. **Checked on the
host** (`test_tape`, all four machines): from one copy at the tape
set-up, the ROM's own CSAVE, the ROM with only its byte routines hooked,
and the trap leave the same RAM below the ROM (less page #03 and the
stack below SP, tape.h's deliberate differences) and registers at the
clean-up, and the trap's file is the ROM's bytes; fast and slow; CLOAD
by name past a first file with a 22-character name and a `#16 #24` in
its data, and nameless; 1.1's verify, clean and with a changed line;
an empty deck (declined, the ROM waits); a file cut short (the ROM's
loop waits at the byte after). **Planted bugs**, twenty-three in three
rounds: fifteen caught; of the eight that passed, seven marked dead code
(what the byte routines leave in A, V, N, Z and C, and X, Y and `#2F`
after the header write, all of which the ROM overwrites before reading
them), now removed, and one a gap (`#02B1` already zero), now covered
and caught. The test's own sync hook first took one `#16` where the ROM
wants four, and synced on OTHER's data. **In Oricutron**: each machine's
trapped save, written by `test_tape --write`, CLOADed by Oricutron
(`002279f`) from the signal through its ROM's own routines, its traps
off, and RUN printed TAPE OK in all four; the same tape cut after its
header, the control, never returned from CLOAD. **On the board**, over
the UART (`out/m10/board1.log`, `board2.log`): the Oric-1 16K CSAVEd
M10TEST.tap (a 112 ms park), CLOADed it by name after NEW (35 ms and
24 ms parks) and ran it; the Atmos 48K, from the menu's New tape,
CSAVEd A and B,S onto TAPE03, rewound, CLOAD"B",S passed over A and ran
B, CLOAD"B",V said "0 Verify errors detected" after rewinding once, and
the 1.0 save loaded and ran in 1.1. A CLOAD"B" on a tape without it
rewound once and was declined, and the ROM then waits on CB1 at 54.9 %
of core 0. Real-time ratio 1.000, no dropped snapshots, no underruns
throughout. **Measured** (`out/m10/tape`, `notape`, one sitting, the
Oric-1 16K): the run loop's check costs 0.6 to 1.0 points of core 0,
idle 29.3 % against the control's 28.7 %, compute 35.2 % against 34.2 %,
scroll 33.5 % against 32.8 %, 3.5 to 5 host cycles an instruction. The
image is 133.4 KB of text and 207.1 KB of bss (M9: 123.1 and 195.3), the
Tapes page's 64-entry list most of the growth. **Not checked:** the Tapes page and its keys
on the PicoCalc's own keyboard, and the status line's deck, by the
owner; `boot_tape` and PICO_ORIC_BOOT_TAPE on the board; a save to a
full card. **Left for M13:** fast tape off, which the Setup row keeps
and which does nothing yet: the trap serves either way.
**Review fixes** (Codex's review of PR #10, 2026-10-09). Two findings,
both taken. (1) With the deck empty, a CLOAD's name was matched against
16 bytes of a longer header name for both ROMs, and the tape put in the
deck; only 1.1 keeps 16 bytes of a name off tape, and 1.0, which keeps
it all, rejects it, so that tape would stand in the deck unable to
answer. The match now takes the ROM's own count. Settled by execution in
`test_tape`: `CLOAD"OTHER WITH A LON"` against `OTHER WITH A LONG NAME`
loads in 1.1 and is passed over in 1.0, on all four machines; with 15
characters both pass it over. (2) A file name too long for a path of
`ORIC_PATH_MAX` was listed, and searched, under a cut path that opens
nothing; it is now skipped, and the list logs it. Not checked on the
board: neither case is on the card.

**M9, menu and settings** (`src/core/settings.*`, `shot.*`, `status.*`;
`src/port/menu.*`, `park.*`, `settingsio.*`, `shotio.*`, `card.*`,
`core0.c`, `core1.c`, `main.c`, `handoff.*`, `display.*`;
`test_settings`, `test_shot`, `test_status`; `tools/uart-hold.sh`),
built 2026-10-08 (Pico SDK 2.3.1, arm-none-eabi-gcc 15.2) and run on the
Plus 2 W `7458DC82A89AAC12` (RP2350B, chip rev 2) the same day, **not yet
done**: the owner's check of every page and key is outstanding. pico-ace's
settings, rewriter, park, menu and screenshots, renamed, with the Oric's
keys (design.md §10.7) and pico-atom's eight items in its order (§12):
Tapes, Discs and Snapshots say they are not in this firmware yet; Setup
has the status line, the perf line, backlight, volume, keys (standard
only until M15) and fast tape (kept for M10); the Machine page stages ROM
and RAM, shows the Microdisc as off until M14, and applies them by a
power-on that core 0 does, with the ROM checked on the card first and
refused, naming its file, if it is missing or unrecognised; Reset is the
RESET line and Alt+K still the NMI. Core 1's boot job now reads
`/oric/pico-oric.cfg` before it chooses the ROM, and
`PICO_ORIC_BOOT_ROM` and `_RAM` win over the file. A value this firmware
cannot act on yet (`microdisc = on`, a layout, a boot tape or disc) is
kept for the save and named as the first problem. The build option
`PICO_ORIC_PERF_LINE` is gone: the file's `perf` replaces it, and the
perf line is off by default, as the siblings have it. **Checked on the
host:** `test_settings`, pico-ace's test with the Oric's keys, every key
and value, the refusals, and the rewriter's columns, comments, line
endings and second save; `test_shot`, a frame drawn by the ULA's row
generator, encoded as a BMP and read back pixel for pixel, the red paper
attribute where it belongs; the BMP opens in macOS's own reader the right
way up (`out/m9/shot-test.png`). **Checked on the board, driven over the
UART** (`out/m9/*.log`): the menu opens (RS) and draws in 16.1–16.3 ms;
Save settings writes the file through `.new` and a rename (59 bytes, then
80, then 99), and the UART's hold (GS) reads it back with no problem;
Setup's perf line and volume change; F5's Machine page, the ROM to 1.0 and
the RAM to 16K, Apply: basic10.rom read and hashed, the machine powered on
as the Oric-1 16K and Ready at 1,078,282 cycles, M7's figure; F6 from the
guest writes `/oric/shots/SHOT0001.bmp`, 307,254 bytes in 938 ms; pause
(US) dims the backlight and a key resumes it; F10's About reads the card's
ROMs afresh. `PRINT FRE(0)` typed after all of it ran. Rebooted, the file
chose the Oric-1 16K, volume 7, perf line on, Ready at the same cycle
count; saved again unchanged, it stayed 99 bytes. Real-time ratio 1.000,
no dropped snapshots, no I²C errors, idle core 0 29.4 % on the Oric-1 16K.
**Found on the board:** the first save wrote `backlight = 2`, nothing
having set it: the panel's level, read at boot, went into the file, as
pico-ace's does. EL §8.7 keeps the file's backlight unless the user set
one, so the save now writes it only once the Setup page has changed it.
**Measured:** menu open to drawn 16.1–16.3 ms; the screenshot 938 ms; the
settings read at boot 7.1–8.1 ms; the image 123.1 KB of text and 195.3 KB
of bss. **Not checked:** every page and key on the PicoCalc's own
keyboard, Alt+M, Alt+H, Alt+P and Alt+K among them, which is the owner's
check; the Machine page refusing a ROM missing from the card, or
unrecognised, which needs the card changed; the screenshot opening on a
computer, and the file's text as edited, which need the card read on one;
the 60 Hz rate and the timer-paced build on the board (both build).
**By the owner** on a Pico 2 W, the release build, 2026-10-08: the
menus work as expected. At the owner's request the menu then took the
missing-ROM page's colours: the title and key rows on blue paper in white
ink (`textpage_title`, the attributes now in `textpage.h` for both), and
About's OK, ?? and -- in that page's green, yellow and red. Looked at in a
host render of the page before flashing; on the board the About page
opens and draws in 16.7 ms. By the owner's eye on a Pico 2 W, the same day: the
colours look good.
**Review fixes** (Codex's review of PR #9, 2026-10-08). Three findings,
all taken. (1) A refused `backlight = 16` saved with no backlight set was
rewritten as `backlight = 0`, which the parser refuses too, while the
rewrite said OK: 0 is "leave it", which no line can say. Such a line is
now made a comment; `test_settings` has the case, which failed before
the change. (2) Pause assumed Alt was held, so paused by the UART's US a
plain `p` did not resume and `m` opened the menu; it now starts from
whether Alt was down when asked. On the board, US then `p` resumed. (3) A
save that fixed a bad line left the old problem named by the menu and
About until a reboot; the save now says afresh what the file it wrote
holds, values this firmware cannot act on yet included, which moved from
core 1 into `settingsio.c`. Not checked on the board: (1) and (3) need a
bad line planted in the card's file. pico-ace has (1) and (2) as well.

**M8, AY audio** (`src/core/ay8912.*`, `pcm.*`; `src/port/audio.*`,
`core0.c`, `main.c`; `test_audio`, `test_audio_rom`, `test_audio_port`,
`ay_model.h`, `test/host/sdk_sim/`), built 2026-10-08 (Pico SDK 2.3.1,
arm-none-eabi-gcc 15.2) and run on the Plus 2 W `7458DC82A89AAC12`
(RP2350B, chip rev 2) the same day. The AY's three tones, noise and
envelope as event times on its ÷8 tick, run from event to event through
only the generators the level depends on and caught up by arithmetic
otherwise; tones with `TP` below 4 averaged; Westcott's measured volume
table; pico-ace's beeper generalised into `pcm.c`; pico-ace's `audio.c`
renamed, and core 0 paced on its queue, with `PICO_ORIC_AUDIO=OFF` the
timer's control (design.md §8). **Checked on the host:** the core equals an
independent cycle-stepped model on every sample of 100 random scripts
(9,195,946 samples, 6,939,801 events with every tone stepped), the model
with every write a tick late failing as the control; averaged tones differ
by at most 109 of 32,767 through a 1.7 ms average with tones alone, 206
with noise or a fast envelope; all sixteen envelope shapes, stepped and
caught up in one jump, follow the data manual's drawings as MAME
transcribes them, and shape 10 does not pass as 14. Both ROMs booted from
power-on a step at a time, with PING, SHOOT, EXPLODE, ZAP and `MUSIC
1,4,10,15` typed (each key's click included), equal the model on every
sample since power-on with every tone stepped, and with averaging on all
but ZAP, which sweeps its period up from 0 and stays within the averaged
bound (74 and 70). PING (`TP` 24) measures 2,604.166 Hz (1.0) and 2,604.167
Hz (1.1) off the output against 2,604.167 computed, and does not pass as
`TP` 25; the twelve notes of a `MUSIC` scale in octave 3 (`TP` 238 to 126)
match their computed pitch to the third decimal. Two seconds at the
prompt, the scan writing port A throughout, leave the output flat; PING
in the same window does not. `test_audio_port` builds `audio.c` against a
simulated DMA and finds 71,919 pushed samples in the ring as their compare
words, each twice and none wrong, at 1,008.065 Hz for `TP` 62; a sample
out of step fails; a dry queue counts underruns and plays silence; an IRQ
held past its deadline counts 3 late refills and playback carries on;
muted still consumes; half volume scales. **Found on the way:** the
simulated DMA must reload a channel's count on every trigger, as the
RP2350 datasheet's TRANS_COUNT says, for `audio.c`'s late path to behave
as EL §6.4 measured on the board; hardware-notes §5.3 said a chain trigger
reloads neither address nor count, and was corrected, at the owner's word,
to the datasheet's account the same day. A
write held on the bus restarts no envelope: wire() runs after every VIA
access, so the rule is a new write or new data under one. **Checked on
the board:** the Atmos 48K reaches Ready at 2,476,042 cycles, as in M7;
PING, SHOOT, EXPLODE, ZAP and a twelve-note scale typed over the UART ran
with zero underruns and late refills. **Measured** (`out/m8/summary.txt`,
`soak.txt`, `late.txt`), tier 2, one boot each, core 0 busy: idle 29.0 %,
§14's sound workload (`MUSIC`, `SOUND` with noise, `PLAY` with an
envelope, 7,248 AY events a second) 34.8 %, scrolling at 60 Hz 32.8 %;
with the synthesis stubbed in the same sitting, 28.9 % and 33.0 %, so the
AY costs **1.8 points** where it works hardest; on the timer's pacing,
27.8 % and 33.8 %. Samples consumed a second, the control quantity:
36,623–36,624 at 50 and at 60 Hz, the PWM's 36,621.09 to within the count's
128-sample granularity. **Ten minutes** on the sound workload (121
windows): zero underrun samples, zero late refills, rt 1.000, no snapshot
dropped, no I²C error. **The late path forced** (a scratch build masking
core 0's interrupts for 9 ms every 250 fields, as EL §6.4's): each stall
counted 2 or 3 late refills and lost about three halves of samples
(36,546 Hz consumed), with no underrun and no storm, and playback carried
on; the guest, paced on what was consumed, ran at 0.997–0.998. The image
is 102.6 KB of text and 180.1 KB of bss. **Not checked:** how any of it
sounds, which is the owner's ear on the device (PING, ZAP, SHOOT, EXPLODE
and a `MUSIC` scale); the pitch on the board off the speaker (the host
measured it through the core and through `audio.c`'s ring); the loudness,
a channel at 15 being ±170 of the PWM's ±1,024 at full volume (M9 brings
the volume); the build without the UART; the 16K machines and BASIC 1.0
on the board with sound.
**By the owner's ear** on a Pico 2 W, 2026-10-08: the sound commands
(PING, SHOOT, EXPLODE, ZAP) and the `MUSIC` scale work.

**M7, the Oric on the device** (`src/port/core0.*`, `core1.c`, `main.c`,
`handoff.*`, `display.*`, `card.*`, `roms.*`, `textpage.*`;
`src/core/status.*`; `test_status`; `tools/perf-run.sh`,
`perf-summary.sh`, `uart-screen.sh`), built 2026-10-08 (Pico SDK 2.3.1,
arm-none-eabi-gcc 15.2) and run on the Plus 2 W `7458DC82A89AAC12`
(RP2350B, chip rev 2) the same day. Core 1 brings up the southbridge and
the panel, then reads `/oric/roms/`, hashing every file, and loads the
machine's ROM, found by SHA-1 whatever its name, or failing that a file
with its name and size, marked unrecognised (design.md §10.2). Core 0
runs `oric_run_field` paced on `time_us_64()` against an absolute
deadline, with the keyboard's and the UART's events into `keymatrix`
before each field, and publishes a frame to pico-ace's pool, which core 1
presents through `ula_diff` at (40, 48) with the perf line above. Without
the ROM, core 1 presents a page, an Oric frame drawn in the ROM's font or
the fallback, naming what is missing; if the other machine's ROM is
there, RETURN starts that machine. The machine is the Atmos 48K, or
`PICO_ORIC_BOOT_ROM` and `_RAM`. **Found on the board:** the first image
ran an undocumented `#02` at `#FFFF`. `oric_init` powers on, and the CPU
takes its reset vector then, from the empty socket; the harness and the
trace tool reset again after loading the ROM, and `main.c` did not. It
now powers on after loading, and `oric.h` and CLAUDE.md say so. **The
machine on the board equals the host's:** a scratch build logged the
CPU's registers and cycle count at the end of each of the first 150
fields, through the boot to Ready, and a host program running the same
loop printed the same 150 lines. **Checked on the board:** all four
machines boot to Ready at the ROM's own pace, within a field of M3's
cycles from power-on: 1,078,282 (1.0 16K), 2,855,432 (1.0 48K), 898,569
(1.1 16K) and 2,476,042 (1.1 48K); Ready 1.67, 3.45, 1.49 and 3.07 s after
reset, each 0.60 s behind its guest time: 34 ms to start core 1, 424 ms
in `lcd_init` (the panel's reset and sleep-out waits, hardware-notes
§4.4), and 138 ms for the card (mount, three files hashed, one loaded,
and the job's own log lines at 115,200 baud). The criterion "under a
second" was amended by the owner (design.md §15.2). `PRINT 2+2` typed
over the UART prints 4, and the screen read back with `uart-screen.sh`
shows the Atmos's banner and 37,631 bytes free. By the owner's eye:
BASIC typed on the PicoCalc's keyboard runs; a test card (eight papers
with contrasting inks, inverse, double height, blink, the alternate set,
two inks) and a HIRES drawing (red and cyan circles by an ink attribute,
a cross, the text window) each match a render of the same program on the
host (`out/m7/text.png` and `hires.png`, the programs beside them); with
the card out, the page that says so; with `basic11b.rom` moved out of
`/oric/roms/`, the page naming it and offering the Oric-1 48K, and RETURN
on the keyboard booting it, to Ready at 2,855,432 cycles. The pages were
rendered on the host first as well (`out/m7/nocard.png`, `offer.png`,
and `none.png`, with neither BASIC ROM and an unrecognised EPROM).
Real-time ratio 1.000 on every workload (0.999–1.002 a window), zero late
fields, zero slips, zero I²C errors. **Measured** (`out/m7/summary.txt`),
the Atmos 48K, §14's workloads and two at 60 Hz, one boot each, eight or
nine 5 s windows, tier 0 against tier 2, as core 0's share (host cycles
per instruction): idle 34.4 / 27.4 % (194.8 / 155.2), compute 45.4 /
31.2 % (216.0 / 148.8), scroll 39.3 / 30.1 % (196.5 / 150.9), hires 47.5 /
30.1 % (243.6 / 154.4), sound 44.4 / 31.3 % (212.8 / 150.3), glyphs 44.2 /
30.9 % (214.1 / 150.2), scroll at 60 Hz 40.1 / 30.3 % (200.0 / 151.9) and
glyphs at 60 Hz 45.5 / 31.3 % (219.6 / 151.6); 3.19–3.80 guest cycles per
instruction. **Decided** by the owner: tier 2 is the default (design.md
§3.2). At tier 2 GCC had inlined `oric_run` into `oric_run_field`, which
was in flash, so the run loop called the interpreter through a veneer
every instruction; `oric_run_field` is now marked as well
(hardware-notes §9.8), and the tier-2 runs above include that.
The
I/O path that M3 added (`oric_io_changed`, `oric_via_catch_up`, `wire`,
`via6522_sync`, `_set_pa`, `_set_pb`, `ay8912_bus`) was in flash too, and
is now tier 1, as hot.h defines it: against tier 2 without it in the
same sitting, whose figures repeated the runs above to 0.1 point, idle
27.4 to 27.0 %, sound 31.3 to 30.8 % and hires 30.1 to 29.7 %, each run
steady to 0.1 (`summary-io.txt`). Without the perf line (the control), idle, glyphs and
scroll at 60 Hz read the same to 0.1 point. Presents: 2.9–3.0 ms with
almost nothing to send, 3.4–3.5 ms while scrolling, 2.3–2.7 ms in hires,
14.8–15.2 ms with the space glyph redefined every field; **no snapshot
dropped** at 50 or 60 Hz; the one full present at boot 16.0–16.1 ms. The
mode scan, sampled once a second: 37–38 µs a field on text, 66–71 µs with
the 60 Hz attribute, 112–117 µs in hires (0.2–0.6 % of core 0); the
snapshot's copy 0.20–0.31 %. `test_status` holds the perf line's text,
and found an overflow carried over from pico-ace: a count near 2³²
rounded to nothing (fixed here, not in pico-ace). The image is 95.5 KB of
text and 169.6 KB of bss. **Not checked:** the build without the UART on
the board (it builds); a card pulled or put in while the guest runs
(logged only: card work waits for M9's park); the status line's note for
an unrecognised ROM (none to hand); keys held to auto-repeat on the
board; the 16K machines beyond their boot.
**Found by the owner** on a Pico 2 W, with the build that ships
(`-DPICO_ORIC_UART=OFF`), 2026-10-08: typed BASIC programs run, the Ctrl
keys work, and keys auto-repeat; started with the card out, the page
said so, but a card put in left it there until a power cycle, because
the boot read the card once and card work waited for M9's park. The
guest has not started while the page is up, so nothing needs parking:
core 1 now runs the ROM job again on each card change, and core 0 takes
the image only after claiming it, with a barrier each side, so that a job
cannot rewrite it under the copy (`handoff.h`). On the Plus 2 W the same
day, with the card put in at the page: the job ran, and the Atmos 48K
reached Ready at 2,476,042 cycles, without a reset. The page now says
the machine starts when the card goes in. The owner then ran the fix's
release build on the Pico 2 W, the same day, and it works there too.

**M6, board bring-up and the card** (`src/port/southbridge.*`, `kbd.*`,
`log.*`, `lcd.*`, `display.*`, `sd.*`, `diskio.c`, `storage.*`,
`card.*`, `core1.*`, `handoff.*`, `main.c`, `fatfs/ffconf.h`;
`romset_identify_digest`; `tools/uart-type.sh`), built 2026-10-08
(Pico SDK 2.3.1, arm-none-eabi-gcc 15.2) and run on the Plus 2 W
`7458DC82A89AAC12` (RP2350B, chip rev 2) the same day. pico-ace's
southbridge, keyboard ring, log ring, LCD, SD and FatFs layers copied and
renamed; its display reduced to the test pattern and a timing pass, and
its card job to listing `/oric/roms/` and hashing each file a sector at a
time. Core 1 brings up in HW §10's order and then polls the keyboard at
30 Hz, watches the slot and drains the log, with `busy_wait_us_32` only;
core 0 has no guest yet, and logs every key event with the cell §9.2's map
gives it, from the keyboard and from UART bytes alike. **Checked on the
board:** the test pattern, by the owner's eye: the white border on the
guest's 240×224 at (40, 48), red, green, blue and yellow in their corners,
and the grey frame on all four panel edges. Every key pressed by the
owner, twice over, logged with its code and cell: 261 events, each
counted by core 1 and each logged by core 0, presses and releases paired
but one (below), none lost to the ring. UART bytes typed with
`uart-type.sh` (`a`, `A`, `!`, CR, Ctrl-A, F1) arrive as the keyboard's
own events, Shift and Ctrl around the key as the PicoCalc sends them. The
card with no `/oric/roms` says so; pulled, given the three ROMs on the
Mac, and put back without a reset, it was seen out and in, mounted and
listed, and each file was recognised by SHA-1 as its own image. A
655 s run (`out/m6-soak.log`): 19,633 keyboard polls and **zero** I²C
errors, through the key presses and the card swap. **Found:** Caps Lock
sends `0xC1` as an event, which the map leaves unbound, as it does `` `
``, which has no Oric key; and once, Alt pressed and released, then a release of
Space with no press. The owner did not mean to chord them: a rolled
Alt+Space, which the MCU consumes, would give this, but so would a lost
press, and the cause is not established. `keymatrix` ignores a release
for a key it does not hold, so neither needs a change; both are in
hardware-notes §6.2. **Measured** (one run each unless given): an I²C
register read 4.86 ms, the longest keyboard poll 9.75 ms (two FIFO
entries); at 75 MHz, a 240×224 rectangle 12.58 ms row by row and 12.55 ms
as one colour, one 240-pixel row 64 µs; a 16 KiB file opened, read and
hashed 20.2–20.6 ms, the 8 KiB EPROM 11.7 ms; a cold mount 231–238 ms, a
warm one 14.6 ms. The image is 52,620 bytes of text and 8,068 of bss with
the UART, 49,824 and 5,996 without. `test_romset` gained the digest
lookup, a table entry's own digest at its size, at the wrong size and
with a bit changed, and each real ROM hashed in 512-byte pieces. **Not
checked:** F6–F10 and held events (no key was held long enough); a card
pulled during a job; card writes, which nothing does yet; the build
without the UART on the board (it builds); the presenter, the perf and
status lines (M7). The firmware that ran was built from the working tree
before its commit (`87c5c43-dirty`).

**M5, the keyboard on the host** (`src/core/keymatrix.*`,
`keymap_picocalc.c`; `test_keyboard`, `test_keymap`; `test/host/guest.*`),
built 2026-10-08 on the workstation (macOS, Apple clang, Debug), with the
owner's `basic10.rom` and `basic11b.rom`. pico-ace's held set, canonical
codes, binding fixed at press, paced replay and Alt layer, adapted to the
8×8 matrix: each host Shift is the Oric's SHIFT on its side, Ctrl is CTRL,
Tab is FUNCT, and a key pressed with a modifier, or one whose entry asserts
SHIFT, reaches the matrix a field behind it. Game layouts and the `.map`
parser are left for M15. The harness now types as the firmware will,
through PicoCalc events and the held set. **The sweep:** all 64 cells
pressed at the prompt in both ROMs, alone, with each SHIFT, with CTRL and
with FUNCT, the decoded key caught at the handler's `STX #02DF` and read
back from the screen (201 presses a ROM). The two ROMs agree in every
cell and with their own key tables; §2.4 is now the sweep's table, and
`test_keyboard` keeps it. FUNCT is read by neither ROM, and column 4 keeps
one key, so right SHIFT hides FUNCT, FUNCT hides left SHIFT and either
SHIFT hides CTRL (checked in pairs). **Checked:** every one of the
table's 113 entries typed through the held set into both ROMs decodes as
it should (letters as capitals under CAPS, and as typed with CAPS off);
Ctrl with each letter gives `#01–#1A`; the menu's keys reach nothing; DEL,
the four arrows and RETURN edit and run a line. `test_keymap`, which runs
in CI: the table's invariants (one cell a key, the two characters of a
key on one cell, nothing on column 4 but FUNCT, no Alt binding the MCU
keeps) and the held set's pacing, column 4 and queue bounds. **The hold
and the gap**, from six phases against the scan: both ROMs need a key 2
fields down and 2 up, and 1 down or 1 up loses keys (the controls); a key
held 48 fields types once, 50 twice, 60 three times. Type-ahead is one
key: `XYZ` typed during a run leaves `Z` for `KEY$` and nothing at the
prompt. The replay holds 3 and gaps 3: 60 typed program lines at 2 and 2
leave 29 stored in 1.0 (all 60 in 1.1), and at 3 and 3 all 60 in both.
**Planted bugs**, each caught and removed: no modifier lead
(`test_keymap`, and `&` lost in `test_keyboard`); no lead for a SHIFT the
entry asserts (`test_keymap`); left and right SHIFT swapped (the pairs in
`test_keyboard`); `;` and `'` swapped (both); the replay at 2 and 2
(`test_keyboard`'s program); CTRL on FUNCT's row (both). **Found in
review** (PR #5): the keys the MCU sends only as Shift chords (Insert,
Break, End, PgUp, PgDn, Home) had no entries, so Shift+Enter typed
nothing. Each is now its base key's cell with SHIFT; `test_keymap` checks
every one, and fails 13 checks with the entries removed; through both
ROMs they decode as their base keys do (119 entries). **Measured:**
each ROM needs 4 fields a key at the scan; the replay takes 6, and a line
of 10 keys in one poll 59 fields, 5.9 a key; `test_keyboard` runs in 6.8
s (Debug). **Not checked:** anything on the board (M6, M7); which SHIFT is
left, which is Oricutron's word, not yet BN0138's (§16); the southbridge's
events themselves, which arrive with M6; game layouts (M15). **The trace
diff**, rerun, agrees to the end on all four machines with M3's counts;
its tool types through its own script, not the held set.

**M4, video on the host** (`src/core/ula.*`, `snappool.*`, `font.*`,
`font_fallback.c`, `oric.*`; `test_ula`, `test_snappool`, `test_font`,
`test_golden`, `test_field`; `test/host/golden/`; `tools/mkfont.py`,
`tools/render-diff.sh`, `tools/trace/oricutron-render.c`), built
2026-10-08 on the workstation (macOS, Apple clang, Debug and Release). The
decode turns the window `#9800–#BFFF`, the mode at the field's start and
the blink phase into a cell per byte (ink, paper, six pattern bits); the
row generator draws cells as RGB565; the presenter's shadow holds decoded
cells. `oric_run_field` runs the mode scan at each field's end and takes the
next field's length from it; `oric_video_take` fills a frame for the pool,
which is pico-ace's, renamed. The rules follow Oricutron's `ula.c`, and
MAME's `oric.cpp` read at `e4c1c2b` agrees with it in everything but the
blink period (§16). **Checked:** `test_ula` decodes a window built for
each rule (colour and text attributes from their own cell, the line reset,
inverse on attribute cells too, both sets, double height on even and odd
rows, blink in both phases, hires pixels including `#20–#3F`, the text
window's `#9800` set, a mode attribute moving the fetch from the next
cell and lasting into the next field); the scan agrees with the decode on
2,000 random screens, where a scan of the text screen alone, the control,
is wrong on 31; 400 random edits (bytes, attributes, mode attributes,
character-set rows, the blink phase, the start mode) presented from the
dirty bands alone match a full render after each, and the VRAM-byte diff,
the control, leaves stale pixels. `test_snappool`: pico-ace's 100,000
random transitions, and frames taken on 48K and on 16K through the mirror.
`test_font`: each ROM's expanded table equals what it wrote to
`#B500–#B7FF`, on both fits, and fails one glyph out of place (the two
ROMs' fonts are identical, so the other ROM's could not be the control).
`test_field`: a 60 Hz mode attribute written to the screen leaves its own
field at 19,968 cycles and makes the next 16,896. **The goldens:** nine
scenes (colours, inverse, both sets, double height, blink in both phases,
hires with its text window, a mode change at row 10, col 20, and the 16K
mirror against the colours image), drawn with the fallback font so that
CI runs them and no image holds a ROM's glyphs. Each was viewed, as a PNG
at 3×, before it was committed; the first `mode_split` was wrong (its
drawing overwrote the text character set at `#B400`, which is the
bitmap's lines 128–175) and was redrawn. `oricutron-render`, Oricutron's
own `ula_doraster` behind a headless driver, draws all nine frames
identically to the goldens, pixel for pixel (`tools/render-diff.sh`).
**Planted bugs**, each caught and removed: attribute cells never inverted
(Brown's rule; `test_ula`, `test_golden`, and 1,056 pixels of `inverse`
against Oricutron); double height's halves swapped; a mode attribute
moving the fetch only from the next line; a band's span taken from its
first line only (`test_ula`); the scan's skip not reset after a walked
line; the scan's word test checking 32 of 40 bytes, and matching text
attributes in place of mode ones; blink hidden in the shown phase. A word
test that matched a superset of mode attributes was not caught, rightly:
it only walks more lines. **The trace diff** still agrees to the end on
all four machines, with M3's counts. **Measured** (`out/m4-video-cost.txt`,
M1 Pro, per frame): Release, decode 10–17 µs, rows 9 µs, the mode scan
1.1 µs on a BASIC text screen, 0.56 µs on hires and 14 µs on a random
screen of mode attributes; Debug, decode 44–64 µs, rows 68–204 µs. The
scan's word test was kept against a control build in the same sitting:
without it the scan took 4.7, 28.5 and 23 µs. **Not checked:** anything
on the board, where the port does not use the ULA yet (M7); the panel's
byte order; any rule against hardware, rather than against two emulators;
the blink period (configuration, 32 fields); the first active line; the
emulator's text pages, which arrive with M9; the frame's status bytes
(M7). The owner has not yet looked at the goldens. CI green for PR #4 on
both jobs, `test_golden` among the passes and `test_font` skipped (no ROMs
in CI); its first run failed on `clock_gettime`, which strict C11 hides on
glibc, and `test_ula` now times with `timespec_get`.

**M3, the Oric on the host** (`src/core/ay8912.*`, `romset.*`, `sha1.*`,
`oric.*`, `bus.*`; `test/host/guest.*`, `test_boot`, `test_ay8912`,
`test_field`, `test_romset`, `test_test_rom`; `test/asm/oric_test_rom.s`;
`tools/trace/`, `tools/trace-diff.py`), built 2026-10-08 on the
workstation (macOS, Apple clang, Debug), with the owner's `basic10.rom`
and `basic11b.rom` (§10.2's hashes). `sha1` copied from pico-ace; `romset`
after pico-atom's, by SHA-1. The VIA wired as both ROMs drive it, read
from their disassembly first: CA2 = BC1, CB2 = BDIR; AY port A's zero bits
enable keyboard columns, PB0–PB2 pick the row, PB3 high is a key down. The
AY as a register file behind its bus (no sound). `oric_power_on`,
`oric_nmi`, `oric_key_set`, `oric_run_field` with the line and field
lengths in `oric_config_t`. The harness types through each ROM's own key
table (`#FF70`, `#FF78`). **Checked by executing the ROMs** (`test_boot`):
both reach Ready on 16K and 48K with their banners and 15,102, 47,870,
4,863 and 37,631 bytes free; `PRINT 2+2` typed into the matrix prints 4 on
all four; T1 is free-running at latch 10,000 and the handler runs 99–100
times a second; RND gives the same three values after every power-on; NMI
warm-starts and keeps a program, RESET cold-starts; no undocumented opcode
runs. The control, a 16K machine with nothing above `#3FFF`, stops both
ROMs in their RAM test. §16 is updated with each of these.
**The trace diff:** Oricutron at `002279f` (2026-01-23), built by
`tools/trace/build-oricutron.sh` with a hook in its 6502 step and a
headless driver. It first parted from us at T1's first interrupt, and that
found two bugs in our core, both fixed: (1) the VIA was ticked through an
instruction after it ran, so a write on its last cycle started T1 three
cycles early. Now the 6502 records which cycle a page-`#03` access falls
on, on the slow path only, and the VIA is brought up to it first. (2) The
IRQ was polled with the VIA at the instruction's end; a 6502 decides at
the end of the penultimate cycle, so the VIA now runs two cycles behind at
boundaries (§5.3). It also found two errata in Oricutron, corrected by
name in its build: `ORICUTRON_NO_I_DELAY` and `ORICUTRON_BRANCH_PAGE`
(§13.4). After that, boot and `PRINT 2+2` agree line for line, cycles
included, after the reset's S and P: 2,093,293, 2,169,408, 2,132,110 and
2,144,512 instructions on 1.0 16K, 1.0 48K, 1.1 16K and 1.1 48K
(`out/m3-trace.log`). **The test ROM**, assembled by CMake when ca65 is
present, runs in CI: RAM size by aliasing, AY registers written and read
back through the VIA, a full keyboard scan, text, font and hires bytes,
100 T1 interrupts and PB7 edges a second, one NMI per press. **Planted
bugs**, each in a clean rebuild (one first run gave false results because
a source edited within the build's second was not recompiled):
CA2/CB2 swapped fails `test_ay8912`, `test_boot` and `test_test_rom`;
PB3 inverted fails the same three; no 16K mirror fails `test_boot` and
`test_test_rom`; NMI never raised fails `test_boot` and `test_test_rom`;
the field's debt dropped fails `test_field`, whose control (no debt)
drifts as it should. **Measured:** power-on to Ready, to within 100
cycles: 1,075,974 (1.0 16K), 2,846,747 (1.0 48K), 886,330 (1.1 16K),
2,460,543 (1.1 48K). **Decided:** the VIA stays ticked per instruction, as
M2 measured it, with the catch-up above, rather than slicing at its next
event (M1's open question). **Not checked:** the schematic, for the
mirror, the VIA's decode and the reset lines (the ROMs reach the VIA only
at `#0300–#030F`); the run loop's added subtraction on the board (the
bench's host counts are unchanged; M7 measures the whole machine); a taken
branch that does not cross a page polls IRQ a cycle early on a 6502, which
neither emulator models and no trace has shown; `ORICUTRON_VIA_AHEAD`,
which boot never exposes; real key timing and FUNCT (M5); anything
visible (M4). ROM 1.0 takes a key on its first scan, so the harness holds
SHIFT a field before the key; a SHIFT pressed in the same instant turned
`(` into `9`. M5's replay must do the same. **CI** green for PR #3 on
2026-10-08: ca65 assembled the test ROM and `test_test_rom` passed;
`test_boot` reported skipped, CI having no ROMs.

**M2, the 6502 on the board: the gate** (`src/bench/`,
`src/port/bench_main.c`, `bench_dormann.S`; `test_bench`), 2026-10-07, on
the Plus 2 W (id `7458DC82A89AAC12`, RP2350B rev 2) at 150 MHz, SDK 2.3.1,
gcc 15.2 `-O3`. pico-ace's bench shape: `oric_t` with every page plain
RAM, run by the real `oric_run` (the VIA ticked per instruction) in
20,000-cycle slices with the overshoot carried as debt, for 10,000,000
cycles a run. Two workloads. **basic** is MS BASIC's CHRGET at `#E2`, with
TXTPTR at `#E9`, walking a line of three sums, and an FADD on unpacked
accumulators (zero, swap, bitwise alignment, 32-bit add, renormalise), put
together by a small assembler in `bench.c`. **dormann** is the first 10 M
cycles of Dormann's test, embedded when fetched. **Checked on the host:**
basic's three sums come out as the packed floats 45, 45 and 65 on each of
three passes, with S back at `#FF`. A planted bug, the renormalising shift
without the exponent increment, fails it (`84 3C…` for 45). Dormann is in
test `#29` at the slice's end, not trapped. Neither workload runs an
undocumented opcode. One run counts 10,000,001 cycles and 3,076,774
instructions (basic), and 10,000,002 and 3,196,835 (dormann), and `bench.h`
records those. **On the board** the same counts came back on every pass of
every image. Each image ran 7–9 passes, spread under 0.02 %
(`out/m2-bench-t{0,1,2}.log`), in host cycles per guest instruction:

| SRAM tier | basic | dormann | core 0 at 1 MHz |
|---|---|---|---|
| 0, flash | 142.3 | 140.9 | 29.2 %, 30.0 % |
| 1, callees (1,818 B) | 154.7 | 147.6 | 31.7 %, 31.5 % |
| 2, + interpreter (24,084 B) | 144.7 | 138.8 | 29.7 %, 29.6 % |

Mean cycles per instruction were 3.250 and 3.128. **Gate decision: 150 MHz
is enough**, at a projected ~30–35 % of core 0 for the whole machine
(design.md §3.2). **The tier is not decided:** tier 1 is a loss here,
because the flash interpreter reaches its SRAM callees through veneers, and
tier 2 is level with flash. The bench has the XIP cache to itself, which is
what HW §9.8 says hides placement, so the default stays at tier 0 until M7
and M12 measure the whole machine. Tier 2's image calls three cold functions
in flash through veneers: `oric_reset`, and the VIA's `via6522_sync` and
`due`, both reached from a VIA register read. None runs in the bench; the
VIA's two will once M3 has I/O. Image sizes: 117,396 B text (64 KiB of it
Dormann's image) and 85,196 B bss; `pico-oric` is unchanged at 22,764 and
860. **Not checked:** the ROM's own mix (M3); core 1 sharing the cache;
the VIA's cost apart from the CPU's; the `-DPICO_ORIC_UART=OFF` bench on the
board (it builds). CI green on both jobs for PR #2, where both bench tests
ran and passed: the pinned counts hold under Ubuntu's gcc as well as Apple
clang.
**Review fix** (Codex's review of PR #2): `bench_run` asked every slice for
a near-full 20,000 cycles, so a length that is not a whole number of
slices overshot by up to a slice (25,000 ran 40,001). The last slice now
asks only for what is left; `test_bench` checks six such lengths, which
failed before the change. The 10 M-cycle runs, a whole number of slices,
count the same, so the board's figures stand.

**M1, review fixes** (Codex's review of PR #1, 2026-10-07). Three
findings, all taken. (1) **Delayed IRQ poll**: CLI, SEI and PLP change I
after the 6502 has polled IRQ for the next instruction, so that poll sees
the old I; RTI's takes effect at once. pico-atom tests I directly and so
takes an IRQ one instruction early after CLI or PLP, and misses one that
arrives during SEI. The CPU now keeps the old I for one instruction
(`i_old`, `i_old_at`), asked only while an IRQ is asserted.
`test_m6502_behaviour` gained a case for each of the four, which failed
before the change (all but RTI's) and pass after it; Dormann and Clark
still pass. pico-atom has the same gap, not fixed there (read the
siblings, never edit them). M11's snapshot must carry the two fields. (2)
**`PICO_ORIC_RAM_TIER` reached only the executable**, not the core
library where the marked functions are, so a tier did nothing; a tier-2
build now shows `time_critical.oric_m6502_step` and the rest in the core.
(3) **`oric_copy`** chose ROM or RAM by comparing pointers into different
arrays; it now uses the page's `PAGE_ROM` flag. The new `test_bus` checks
that every page of a copy points into the copy, with a plain struct copy
as the control (446 stray pointers).

**M1, the 6502 and the VIA on the host** (`src/core/m6502.*`,
`via6522.*`, `oric.*`, `bus.*`; `tools/fetch-test-suites.sh`;
`test/asm/6502_decimal_test.s`), built 2026-10-07 on the workstation
(macOS, Apple clang, Debug). pico-atom's `m6502` and `via6522` copied whole
and renamed, with their tests: `test_m6502_behaviour`, `_cycles`,
`_decimal`, `_functional` and `test_via6522`. They run on a minimal
`oric_t`: the page table over 64 KiB of RAM and a separate 16 KiB ROM
socket, page `#03` decoded to the VIA by mask, RESET, `oric_run` with
debt, `oric_copy`. The 16K mirror is mapped as §6.2 believes it, untested
until M3 settles it. **Checked:** Dormann's functional test runs to its
success trap at `#3469` after 96,241,367 cycles; Clark's decimal test ends
with error byte 0, every flag checked; the cycle table passes by execution;
`test_via6522` passes with its machine checks moved from `#B800` to
`#0300`; without the suites, `test_m6502_functional` reports skipped.
**Planted bugs**, each caught and then removed: the decimal high-nibble
fixup at 10 instead of 9 fails `test_m6502_decimal` and Dormann; BPL's base
count 3 instead of 2 fails `test_m6502_cycles`; T1's free-run period
latch + 1 instead of latch + 2 fails `test_via6522`. The core also
compiles clean under `arm-none-eabi-gcc -Werror` in the firmware build.
**Measured:** `test_m6502_functional`, both suites, 1.13 s wall time in a
Debug build (a regression marker, not a performance figure). **Left out,
from pico-atom's tests:** the MOS's printer checks (an Atom matter); the VIA
surviving a snapshot, which returns with `snapshot.c` in M11; the "VIA not
fitted" case, as every Oric has one. **For M3 to decide:** the VIA is
ticked per instruction, as pico-atom measured it (a countdown, §3.2),
where §5.3 speaks of stopping a slice at the VIA's next event. CI green
for PR #1, where `test_m6502_functional` ran and passed (cc65 from
Ubuntu's packages).

**M0, skeleton**, built 2026-10-07 on the workstation (macOS, Apple clang;
Pico SDK 2.3.1, arm-none-eabi-gcc 15.2). The layout of design.md §4.1 with
what M0 needs and no emulation: CMake with a host build
(`-DPICO_ORIC_HOST=ON`, CTest) and a `pico2` firmware build, both under
`-Wall -Wextra -Werror`; `src/core/config.h` and `hot.h`;
`test/host/test_util.h` and `test_skeleton`; the version header from
`git describe` at build time; `arm-none-eabi-size` on every build; CI
building both targets and the no-UART image, and running CTest. Copied from
pico-ace's M0 and renamed: `cmake/`, `board.*`, `hot.h`, `test_util.h`,
`tools/build.sh`, `flash.sh`, `uart-log.sh`, `ci.yml`. The firmware prints a
banner and a heartbeat over UART1 and blinks GP25. **Checked:** both builds
green; `test_skeleton` passes; an `#include "pico/stdlib.h"` planted in
`config.h` fails the host build (`'pico/stdlib.h' file not found`), and was
removed. **Measured:** the image, 22,764 bytes text and 860 bss with the
UART; 20,932 and 844 without. **On the board**, a Plus 2 W (id `7458DC82A89AAC12`,
RP2350B, chip rev 2), 2026-10-07: flashed over SWD with `tools/flash.sh`
and captured with `tools/uart-log.sh` (`out/m0.log`), the banner showed
build target `pico2`, clk_sys and clk_peri at 150 MHz, core rail ~1100 mV,
firmware `e3472e4`, then heartbeats a second apart at die 20 °C. A pico2
image cannot light the LED on a W board. CI green on both jobs for PR #1.
