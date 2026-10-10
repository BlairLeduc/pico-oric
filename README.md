# pico-oric

An Oric-1 and Oric Atmos emulator for the ClockworkPi PicoCalc, on a
Raspberry Pi Pico 2 or compatible board (RP2350). Boards based on the
RP2040 are not supported, as the emulator needs more SRAM than they have.

The Oric-1 (Tangerine, 1983) and the Oric Atmos (1984) are British home
computers built around a 6502A at 1 MHz, a 6522 VIA, an AY-3-8912 sound
chip and a custom ULA that draws a 240×224 colour picture using serial
attributes. The emulator runs the original BASIC ROMs on an emulated
6502, in real time, with sound, `.tap` tapes, the Microdisc with
Sedoric, snapshots and game layouts. Its menus and keys follow its
siblings, [pico-atom](https://github.com/BlairLeduc/pico-atom) and
[pico-jupiter-ace](https://github.com/BlairLeduc/pico-jupiter-ace).

> [!NOTE]
> It does not emulate the Jasmin disc interface, the printer, joystick
> interfaces, `.wav` and `.tzx` tapes, or effects that change the picture
> part-way down the screen; [`docs/design.md`](docs/design.md) §17 gives
> the reason for each.

The Machine page offers the ROM and the RAM as separate choices:

| ROM | RAM | Machine |
|---|---|---|
| 1.0 | 16K or 48K | Oric-1 |
| 1.1 | 48K | Oric Atmos (the default) |
| 1.1 | 16K | an Oric-1 16K fitted with the Atmos ROM |

## Documentation

- [Design](docs/design.md): the Oric as emulated, the architecture,
  budgets, milestones, and every guest fact and how each was settled.
- [Milestones](docs/milestones.md): what each milestone verified, on
  which board and when.
- [PicoCalc hardware notes](docs/hardware-notes.md): the host platform.
- [Emulator lessons](docs/emulator-lessons.md): what the earlier emulators
  on the same hardware taught.
- [Third-party material](THIRD-PARTY.md): what in here is not ours, and
  under what terms.

## ROM images

The Oric's ROMs are copyrighted and are **not included**, neither in this
repository nor in the firmware. Put them on the SD card in
`/oric/roms/`. The emulator identifies each by its SHA-1, whatever the
file is called, and these are the hashes it knows (also MAME's):

```
333116e6884d85aaa4dfc7578a91cceeea66d016  basic10.rom    BASIC 1.0, Oric-1
9451a1a09d8f75944dbd6f91193fc360f1de80ac  basic11b.rom   BASIC 1.1, Atmos
0d2ef6e67322f48f4b7e08d8bbe68827e2074561  microdis.rom   Microdisc EPROM (optional)
```

Check yours with `shasum *.rom`. Other dumps (the UK 1.0, localised 1.1s,
the 1.2x rewrites) may boot, and the *About* page marks them
*unrecognised*, but they are not the ROMs the emulator is tested
against, and fast tape stands aside for them. If the ROM the machine
needs is missing, the emulator shows a page naming it rather than a
blank screen, and offers the other machine if its ROM is there.

For development, the host tests look for the same files in `roms/` at the
top of this repository, which git ignores ([`roms/README.md`](roms/README.md)).

## Building and flashing

You need the Raspberry Pi Pico SDK (2.x) and `arm-none-eabi-gcc`. The
build script finds the newest under `~/.pico-sdk`, or uses
`PICO_SDK_PATH`:

```sh
tools/build.sh -DPICO_ORIC_UART=OFF build/pico-release
```

This makes `build/pico-release/pico-oric.uf2`. Hold BOOTSEL on the Pico
while connecting it to a computer, and copy the `.uf2` onto the drive
that appears. One image runs on the Pico 2, the Pico 2 W and the Pimoroni
Pico Plus 2 W.

## The SD card

Everything the emulator reads or writes is under `/oric/`, beside
pico-atom's `/atom/` and pico-ace's `/ace/`, so the three can share a
card:

```
/oric/
  pico-oric.cfg         the settings, read at power-on (below)
  roms/                 basic10.rom  basic11b.rom  microdis.rom (above)
  tapes/*.tap           tape images; CSAVE writes here too
  discs/*.dsk           Microdisc images
  states/slot1.sav      the emulator's own saved states, slots 1 to 4
  keymaps/*.map         game layouts (below)
  shots/SHOT0001.bmp    screenshots, numbered in order
```

> [!WARNING]
> The card must be formatted as FAT32, or FAT16 for small cards.

## Software

TOSEC's Oric collection, on the Internet Archive as
[*Tangerine Oric 1 and Atmos TOSEC 2012-04-23*](https://archive.org/details/Tangerine_Oric_1_and_Atmos_TOSEC_2012_04_23),
holds the archive's tapes and discs. Put `.tap` files in `/oric/tapes/`
and `.dsk` files in `/oric/discs/`. The emulator has been run against
every tape in that set (`docs/milestones.md`, M12).

## Using it

The PicoCalc boots straight to the Oric's `Ready`. Type `PRINT 2+2` and
Enter, and it prints `4`.

### Keys

The PicoCalc's keys type what is printed on them, and the emulator
presses the Oric keys that give the same character. The ones that differ:

| On the Oric | On the PicoCalc |
|---|---|
| `RETURN` | `Enter` |
| `DEL` (deletes to the left) | `Backspace` or `Del` |
| `CTRL` | `Ctrl`, held with a key |
| `FUNCT` (Atmos) | `Tab` |
| left and right `SHIFT` | left and right `Shift` |
| the arrows | the arrow keys |
| `£` | `_` (the Oric's font draws `#5F` as `£`) |
| ↑ | `^` |

The Oric has no `` ` `` or `~`, so those keys type nothing. The ROM's
`CAPS` (`Ctrl`+`T`) decides the case of letters: on, as after a reset,
every letter is a capital; off, `Shift` gives capitals, as on the Oric.

Use the following keys for the emulator itself:

| | |
|---|---|
| the emulator's menu | `Alt`+`M` |
| a menu page, then back to the Oric | `F1` Tapes, `F2` Discs, `F3` Snapshots, `F4` Setup, `F5` Machine |
| help: these keys, and the layout in force | `Alt`+`H` |
| the About page | `F10` |
| screenshot of the display, to the card | `F6` |
| pause | `Alt`+`P` |
| the Oric's reset button (a warm start, program kept) | `Alt`+`K` |

A page opened with a function key or `Alt`+`H` goes back to the Oric when
you leave it. In the menu the arrows move, `Enter` chooses, left and
right change a value, and `Esc` goes back a page, or to the Oric from the
main page. The row at the bottom names the first problem the emulator
has found, if there is one, and the title row shows the battery's charge.
The menu's *Reset* is the Oric's RESET line, a cold start; *Apply and
restart* on the Machine page turns it off and on again.

**Pause.** `Alt`+`P` stops the Oric where it is and dims the screen. The
bottom line says so. Any key carries on, and that key is not typed.

**The lines above and below the screen.** The line along the bottom, the
status line, shows the tape in the deck, and whether it is playing or
recording; with the Microdisc, the disc in drive A and the drive in use.
The line along the top, the perf line, shows the emulator's own figures:
how much of the PicoCalc's first core the Oric takes, how many times real
time it could run, the slowest screen update in the last second,
snapshots dropped, sound underruns and late refills since power-on, and
the field rate, 50 or 60 Hz. The *Setup* page turns each on or off; the
status line starts on and the perf line off.

### Tapes

Tapes are `.tap` files in `/oric/tapes/`. With the deck empty,
`CLOAD"NAME"` loads `NAME.tap`, or failing that the first tape whose
first file is called `NAME`, and `CSAVE"NAME"` adds to `NAME.tap`,
making it if need be. `CLOAD""` needs a tape in the deck. A `CLOAD`
whose name is a file on the card, as it is or with `.tap` after it,
plays that file whatever is in the deck, so a title that loads its next
part by name finds it.

To use a tape whose file names do not match the program's, put it in the
deck on the *Tapes* page (`F1`), which shows each tape's first file. With
a tape in the deck, `CLOAD` reads it from where it stands, as a cassette
would, and `CSAVE` adds to its end; at the end of the tape it is rewound
once. *Rewind* takes it back to the start and *Eject* empties the deck.
*New tape* makes `TAPE01.tap` (or the next free number) and puts it in
the deck. When a `CLOAD` finds nothing, the emulator presses the reset
button, keeping the program in memory, and the status line says why,
where a real Oric would wait for a signal for ever.

**Fast tape** is on unless you turn it off on the *Setup* page. With it
on, a load or save takes a fraction of a second. With it off, the Oric
reads and writes the tape's signal, as a real one does, and a load takes
about as long as it did in 1983, divided by two, since the emulator runs
the Oric as fast as it can while a tape plays, without sound.

> [!TIP]
> A few titles load through a loader of their own and need fast tape off
> and *Play* on the *Tapes* page.

### Discs

The Microdisc, Oric's disc interface, is a row on the *Machine* page. It
needs a 48K machine and `microdis.rom` on the card, and boots the disc in
drive A. The *Discs* page (`F2`) puts an image in a drive: left and right
choose the drive, A to D, and the first row empties it. Images are
Oricutron's `MFM_DISK` `.dsk` files, as TOSEC has them; a file marked
read-only on the card is a write-protected disc. Sedoric 3 is the DOS
the emulator is tested with: it boots, saves, loads, deletes and
formats, and a disc it writes reads in Oricutron.

### Snapshots

The *Snapshots* page (`F3`) saves the whole machine to one of four slots
in `/oric/states/`, and loads it back. A state records which discs were
in the drives and which tape was in the deck, and where; loading it puts
them back, so a disc program resumes with its discs in. The discs and
tapes themselves stay on the card: a state whose disc is no longer there
is refused, naming it, and one whose tape has gone loads with the deck
empty. A disc written to after the save goes back in as it now is.

> [!IMPORTANT]
> A saved state is in this emulator's own format, and loads only into
> the machine that saved it: the same ROM, RAM, Microdisc and VSync
> hack. The page names the machine a refused state needs.

### The machine

The *Machine* page (`F5`) has four rows, each changed with left and
right: **ROM** (1.0 or 1.1), **RAM** (16K or 48K), **Microdisc** (off or
on) and **VSync hack** (off or on). *Apply and restart* turns the Oric
off and on again as the new machine. The program in memory is lost, so
save it first. To start as another machine every time, choose *Save
settings* afterwards.

**The VSync hack** is a modification some owners made: a wire from the
ULA's vertical sync to the tape input, which a few programs wait on to
draw without flicker. With it on, the tape input is the sync, so tapes
load only by fast tape.

### Game layouts

Many Oric games read the keyboard themselves, and move on keys that do
not sit well on the PicoCalc. A game layout puts the PicoCalc's arrows on
a game's keys, and every other key types as before. The *Keys* row of
the *Setup* page (`F4`) chooses one, and the *Help* page (`Alt`+`H`)
shows the one in force.

Three are built in, chosen from the keys the archive's games read
(`docs/design.md` §9.4):

| Layout | `Left` | `Right` | `Up` | `Down` | Games |
|---|---|---|---|---|---|
| *ZX* | `Z` | `X` | `'` | `/` | Centipede, Zebbie, Probe 3 |
| *AZ* | `,` | `.` | `A` | `Z` | Mr Wimpy |
| *QAOP* | `O` | `P` | `Q` | `A` | a few BASIC games |

Space and `Enter` are the Oric's own `SPACE` and `RETURN`, the fire keys
these games use. Games that move on the Oric's arrows need no layout.

Further layouts are text files in `/oric/keymaps/`, one key a line:

```
# Centipede: Z X ' / on the arrows, fire on Return
name  = CENTIPEDE
left  = Z
right = X
up    = '
down  = /
tapes = centipede
```

On the left is a PicoCalc key: `left`, `right`, `up`, `down`, `space`,
`enter`, `backspace`, `tab`, `del`, `esc`, or any single character,
shifted or not. On the right is an Oric key: `A`–`Z`, `0`–`9`, the
unshifted punctuation on its keys (`- = [ ] \ ; ' , . /`), `SPACE`,
`RETURN`, `ESC`, `DEL`, `LEFT`, `RIGHT`, `UP`, `DOWN`, `LSHIFT`,
`RSHIFT`, `CTRL` or `FUNCT`. The Oric sees the key alone, and the
PicoCalc's `Shift` as it is held. `name` is what the menu shows, at most
16 characters, and must differ from every other layout's; without it the
file's own name is used. The optional `tapes` and `discs` lines name up
to four files between them, without their `.tap` or `.dsk`: putting one
of them in the deck or a drive chooses the layout, and the menu says so.
A line starting with `#` is a comment, except `# = ...`, which binds the
`#` key. A file that does not parse is left out, and the menu names the
file and line.

### Settings

At power-on the emulator reads `/oric/pico-oric.cfg`, if the card has
one. Each line sets one thing, and anything the file leaves out keeps its
default. This file sets everything to its default:

```
# /oric/pico-oric.cfg
rom        = 1.1        # 1.0 or 1.1
ram        = 48         # 16 or 48
microdisc  = off        # on needs 48 and microdis.rom
vsync_hack = off        # the vertical-sync modification
volume     = 8          # 0-8
layout     = standard   # or a layout's name, as the Setup page shows it
boot_tape  =            # a tape in /oric/tapes/, in the deck at power-on
boot_disc  =            # a disc in /oric/discs/, in drive A at power-on
perf       = off        # the emulator's own figures along the top; or on
status     = on         # the tape and disc along the bottom; or off
backlight  = 8          # 1-15, as the menu shows it; leave out to keep the last
fast_tape  = on         # off: the Oric reads and writes the tape's signal
```

> [!NOTE]
> The backlight is the one exception: it is left alone unless the file
> sets it.

Upper and lower case are the same. A `#` at the start of a line, or after
a space, starts a comment. A line that is wrong is skipped and the rest
are used, and so is a tape, disc or layout the card does not have; the
menu's bottom row and the *About* page name the first problem.

*Save settings* on the menu's first page writes what is in force: the
machine as it is running, the volume, the backlight if you changed it,
the status and perf lines, fast tape, the layout you chose, the tape you
put in the deck and the disc in drive A. It edits the file rather than
replacing it. A line it changes keeps its place and its comment, and a
setting the file does not mention is added at the end, only if it
differs from its default. Anything else is left as it is. A layout
chosen by loading a file is not saved, since you did not choose it. With
no file on the card, the save makes one.

### About

The *About* page (`F10`) shows the firmware's version, the board, the
chip and its clock, the keyboard controller's version, the chip's
temperature, the machine, each ROM's file with the first eight digits of
its SHA-1, to check against the ones above, and the state of the
settings file.

## Related

pico-atom, an Acorn Atom emulator, and pico-jupiter-ace, a Jupiter Ace
emulator, for the same hardware by the same author, under the same
licence. The 6502 and the VIA are pico-atom's, the PicoCalc drivers and
tools pico-ace's.

## Licence

GPL-3.0, in [`LICENSE`](LICENSE), except the material
[`THIRD-PARTY.md`](THIRD-PARTY.md) lists. The ROMs are not covered by it.
