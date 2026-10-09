# Milestone log

What each milestone verified, on which board, on what date, and what was
not checked, newest first. `design.md` §15.2 holds each milestone's scope
and done-when criteria; this file keeps the full record.

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
