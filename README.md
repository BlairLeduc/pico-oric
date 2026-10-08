# pico-oric

An Oric-1 and Oric Atmos emulator for the ClockworkPi PicoCalc, on a
Raspberry Pi Pico 2 or compatible board (RP2350). Boards based on the
RP2040 are not supported, as the emulator needs more SRAM than they have.

> [!NOTE]
> **Status: early.** The build skeleton (M0) exists; there is no emulator to
> run yet. The plan is in [`docs/design.md`](docs/design.md), and what each
> milestone verified is in [`docs/milestones.md`](docs/milestones.md).

The Oric-1 (Tangerine, 1983) and the Oric Atmos (1984) are British home
computers built around a 6502A at 1 MHz, a 6522 VIA, an AY-3-8912 sound
chip and a custom ULA that draws a 240×224 colour picture using serial
attributes. The emulator will run the original BASIC ROMs on an emulated
6502, in real time, with sound, `.tap` tapes, snapshots and, later,
Microdisc/Sedoric discs. Its menus and keys follow its siblings,
[pico-atom](https://github.com/BlairLeduc/pico-atom) and
[pico-jupiter-ace](https://github.com/BlairLeduc/pico-jupiter-ace).

The Machine page offers the ROM and the RAM as separate choices:

| ROM | RAM | Machine |
|---|---|---|
| 1.0 | 16K or 48K | Oric-1 |
| 1.1 | 48K | Oric Atmos (the default) |
| 1.1 | 16K | an Oric-1 16K fitted with the Atmos ROM |

## Documentation

- [Design](docs/design.md): the Oric as emulated, the architecture,
  budgets, milestones, and every guest fact still to be verified.
- [PicoCalc hardware notes](docs/hardware-notes.md): the host platform.
- [Emulator lessons](docs/emulator-lessons.md): what the earlier emulators
  on the same hardware taught.

## ROM images

The Oric's ROMs are copyrighted and are **not included**. Put them on the
SD card in `/oric/roms/`. The emulator identifies each by its SHA-1, which
should match these hashes (also MAME's):

```
333116e6884d85aaa4dfc7578a91cceeea66d016  basic10.rom    BASIC 1.0, Oric-1
9451a1a09d8f75944dbd6f91193fc360f1de80ac  basic11b.rom   BASIC 1.1, Atmos
0d2ef6e67322f48f4b7e08d8bbe68827e2074561  microdis.rom   Microdisc EPROM (optional)
```

Check yours with `shasum *.rom`. Other dumps (the UK 1.0, localised 1.1s,
the 1.2x rewrites) may boot, but they are not the ROMs the emulator is
tested against. If a ROM is missing, the emulator shows a page naming it
rather than a blank screen.

For development, the host tests look for the same files in `roms/` at the
top of this repository, which git ignores.

## Licence

GPL-3.0; see [`LICENSE`](LICENSE). The ROMs are not covered by it.
