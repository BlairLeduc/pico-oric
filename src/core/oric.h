/* oric.h — the guest machine (design.md §4.2, §6).
 *
 * All state is here or in statically sized buffers from config.h; core/
 * performs no dynamic allocation, which is what makes the §3.3 budget a
 * link-time fact.
 *
 * M1 built what the 6502 and the VIA need to run: the page table, RAM,
 * the ROM socket, page #03's decode and the run loop. M3 wires the VIA to
 * the AY's bus and the keyboard (§2.3), and adds NMI, power-on and the
 * field. M4 adds the ULA's mode and the frame handed to the presenter;
 * M8 the AY's sound, box-filtered into PCM a field at a time (§8); M10
 * the tape's traps (§10.3); M11 the restore after a snapshot (§10.6);
 * M13 the signal (§10.4); M14 the Microdisc (§10.5); M16 the
 * vertical-sync modification (vsync.h).
 */
#ifndef PICO_ORIC_ORIC_H
#define PICO_ORIC_ORIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ay8912.h"
#include "cassette.h"
#include "config.h"
#include "m6502.h"
#include "pcm.h"
#include "romset.h"
#include "tape.h"
#include "ula.h"
#include "via6522.h"
#include "vsync.h"
#include "wd1793.h"

/* Page descriptor flags (§6.1). */
#define PAGE_ROM   0x01u  /* writes ignored                            */
#define PAGE_IO    0x02u  /* reads and writes take the slow path       */
#define PAGE_EPROM 0x04u  /* the Microdisc's EPROM: writes ignored     */
#define PAGE_OPEN  0x08u  /* unpopulated: reads return the open bus    */

/* Exactly two pointers. The flags are a separate byte array rather than
 * a third struct field so the descriptor does not pad out to twelve
 * bytes; only the slow path reads them (§6.1, EL §4.1). */
typedef struct {
    uint8_t *read;    /* NULL -> slow path            */
    uint8_t *write;   /* NULL -> not writable, or I/O */
} page_t;

/* The two RAM fits (§2.1, §6.2). */
typedef enum { ORIC_RAM_16K, ORIC_RAM_48K } oric_ram_t;

/* Configuration, not code (§6.2), applied only by a power-on. The
 * field's timing is here, not in config.h, because §16 has not settled
 * it: a timing constant stays runtime configuration until it is
 * (EL §14.2, §11.1). */
typedef struct {
    rom_id_t   rom;            /* ROM_BASIC10 or ROM_BASIC11; the port loads it */
    oric_ram_t ram;
    uint16_t   line_cycles;    /* 64 (§11.1)                                    */
    uint16_t   lines_50hz;     /* 312                                           */
    uint16_t   lines_60hz;     /* 264                                           */
    uint16_t   blink_fields;   /* 32: fields per blink phase (§16: disputed)    */
    /* The tape's traps (tape.h). The port may change it at any boundary;
     * off, CLOAD and CSAVE are the ROM's alone. */
    bool       tape_traps;
    /* Fast tape off: the traps are only the port's cues, and the ROM
     * reads and writes the signal (tape.h, cassette.h). */
    bool       tape_signal;
    /* The Microdisc on the expansion port (§10.5): a 48K machine only;
     * on a 16K one it is left out. */
    bool       microdisc;
    /* The vertical-sync modification: CB1 is the ULA's sync, not the
     * tape (vsync.h). Its pulse, from the first active line: the sync's
     * line at each frequency, then Oricutron's delay and width, all
     * cycles but the lines (§16: the 60 Hz line low, the pulse's shape
     * Oricutron's). */
    bool       vsync_hack;
    uint16_t   vsync_line_50hz;   /* 256 */
    uint16_t   vsync_line_60hz;   /* 234 */
    uint16_t   vsync_delay;       /* 12  */
    uint16_t   vsync_low;         /* 260 */
} oric_config_t;

typedef struct oric_s {
    m6502_t   cpu;
    via6522_t via;
    ay8912_t  ay;

    /* The request the CPU is stalled on, and the ROM's table (tape.h).
     * A snapshot saves only the trap's carry-over, the kept header: a
     * request is served before the field ends (snapshot.h). */
    tape_t    tape;

    /* The signal: the player on CB1, the recorder on PB7, the relay on
     * PB6 (cassette.h). Not machine state: a snapshot leaves it, and a
     * load stops it. */
    cassette_t cas;

    /* The ULA's sync on CB1, while the modification is fitted (vsync.h).
     * Worked out from the field's start, not saved. */
    vsync_t   vs;

    /* The Microdisc (§10.5, microdisc.h): its controller, the latch
     * last written to #0314, and its EPROM, which the port loads. */
    wd1793_t  fdc;
    uint8_t   md_latch;
    bool      eprom_in;
    bool      rom_in;        /* a BASIC ROM is in the socket         */

    /* The AY's level as PCM (§8.2), drained by the port once a field
     * with oric_audio_drain. Not the chip's state: RESET leaves it. */
    pcm_t     pcm;

    /* The keyboard: a bit per column, set while the key is down, for
     * each of the eight rows PB0-PB2 select (§2.3, §2.4). */
    uint8_t keys[ORIC_KEY_ROWS];

    /* All 64 KiB, the top 16 KiB being the overlay RAM under the ROM
     * (§2.1). The ROM has its own array, so that RAM stays beneath it. */
    uint8_t ram[ORIC_ADDR_SPACE];
    uint8_t rom[ORIC_ROM_SIZE];
    uint8_t eprom[ORIC_EPROM_SIZE];
    page_t  page[ORIC_PAGE_COUNT];
    uint8_t page_flags[ORIC_PAGE_COUNT];

    /* Open bus is modelled as the last value on the bus (EL §4.1). */
    uint8_t open_bus;

    oric_config_t cfg;

    /* The ULA's mode attribute bits (ula.h). The field ends where the
     * ULA is about to draw a frame from the top, and that frame is what
     * oric_video_take copies (§11.1): frame_mode is the mode it starts
     * in, and ula_mode the mode the scan finds it leaves, which sets the
     * next field's length and the next frame's start (§7.4). */
    uint8_t ula_mode;
    uint8_t frame_mode;

    /* Cycles of the current instruction the VIA has already been ticked
     * through, to reach an access part-way into it (§5.3). */
    uint32_t via_early;
    /* The VIA's flags a write's extra tick set (oric_via_catch_up): when
     * that tick is past the instruction's end, they are the next
     * instruction's IRQ poll's, not this one's. */
    uint8_t via_late;

    /* A device's next event moved earlier inside a run slice (the
     * relay closed, a disc command began): the slice ends after this
     * instruction, and the run loop clears it. */
    bool cut;

    /* Cycle debt carried between fields (§4.2): what the last field ran
     * past its length, as a negative number. */
    int32_t budget;
    uint32_t fields;

    /* Instructions executed, for host cycles per guest instruction
     * (§14). A counter, not machine state: snapshots leave it. */
    uint64_t instructions;
} oric_t;

/* The power-on default is the Atmos 48K (§18 item 3); the ROM is the
 * port's to load. */
void oric_config_default(oric_config_t *cfg);

/* Wire up the page table from cfg and power on (§6.3). */
void oric_init(oric_t *m, const oric_config_t *cfg);

/* Zero RAM, the overlay included, and reset every chip with the CPU
 * (§6.3). The ROM stays in its socket and the keys stay as held. */
void oric_power_on(oric_t *m);

/* The RESET line: the CPU, the VIA and the AY, RAM kept (§4.2, §6.3). */
void oric_reset(oric_t *m);

/* After a snapshot load has stored the chips' fields (snapshot.h): no
 * tape request, every key up and the lines driven again from the
 * restored registers, the IRQ line from the VIA's flags among them, the
 * AY's derived state rebuilt, and the sound
 * restarted from the restored clock and level (§10.6). The samples not
 * yet drained, the rate and the DC blocker are the port's, and stay. */
void oric_restored(oric_t *m);

/* The reset button under the case, which is NMI (§2.1). */
void oric_nmi(oric_t *m);

/* Run at least `cycles` guest cycles, finishing whole instructions.
 * Returns the cycles actually run, which the caller carries as debt. */
uint32_t oric_run(oric_t *m, uint32_t cycles);

/* Cycles in the next field, from the ULA's 50 or 60 Hz choice (§11.1). */
uint32_t oric_field_cycles(const oric_t *m);

/* One field, less the debt the last one left. Returns the cycles run;
 * never assume 19,968 (§4.2). It ends where the snapshot is taken, the
 * frame the ULA is about to draw (§11.1); which raster line that is, no
 * program can see (§16). The mode scan then runs over the window, for
 * the next field's length. */
uint32_t oric_run_field(oric_t *m);

/* The 10 KiB the ULA can fetch, #9800-#BFFF, as one run of bytes: in a
 * 16K machine, #1800-#3FFF of its RAM through the mirror (§2.2, §4.4). */
const uint8_t *oric_video_window(const oric_t *m);

/* Whether blinking cells show in the frame oric_video_take copies: the
 * one the ULA draws next, frame `fields` counting from 0 at power-on
 * (§11.1). Phases are blink_fields frames long, shown first (§16). */
bool oric_blink_on(const oric_t *m);

/* After oric_run_field: the frame for the presenter, the window, the
 * mode at its start and the blink phase (§4.4). Core 0 calls it into a
 * buffer it has claimed from the pool. */
void oric_video_take(const oric_t *m, oric_frame_t *f);

/* A key at a matrix cell, down or up (§2.4). The row is PB0-PB2's
 * value, the column the bit of AY port A that enables it. */
void oric_key_set(oric_t *m, int row, int col, bool down);

/* After anything outside the run loop changes a VIA register or line:
 * put the outputs on the AY and the keyboard, and back (§2.3). The bus
 * calls it after every access to page #03. */
void oric_io_changed(oric_t *m);

/* Before an access to page #03 part-way into an instruction: tick the
 * VIA through the instruction's cycles before the access, and for a
 * write one more (§5.3). */
void oric_via_catch_up(oric_t *m, bool write);

/* The port's sample rate, as the exact fraction rate_num / rate_den Hz
 * (§8.4). The samples not yet drained are kept. oric_init starts at the
 * nominal ORIC_AUDIO_RATE. */
void oric_audio_set_rate(oric_t *m, uint32_t rate_num, uint32_t rate_den);

/* Move up to `max` samples of the fields run so far out, oldest first.
 * Returns how many. oric_run_field brings the AY to the field's end. */
size_t oric_audio_drain(oric_t *m, int16_t *dst, size_t max);

/* Read a guest address as the CPU would, without its side effects: page
 * #03 reads as the open bus. For tests and the presenter. */
uint8_t oric_peek(const oric_t *m, uint16_t addr);

/* Copy a whole machine. The page table points into the struct, so a
 * plain assignment leaves the copy reading the original's memory; this
 * moves the pointers across with the bytes (§4.2). */
void oric_copy(oric_t *dst, const oric_t *src);

/* Put a 16 KiB BASIC ROM in the socket at #C000 (§2.2). The image is
 * copied; false if it is not exactly ORIC_ROM_SIZE bytes. The CPU took
 * its reset vector when oric_init powered on, from an empty socket, so
 * power on or reset after this: without it the first instruction is at
 * #FFFF, the open bus's vector. */
bool oric_load_rom(oric_t *m, const uint8_t *data, size_t len);

/* Put the Microdisc's 8 KiB EPROM in (§10.5); false if it is not
 * exactly ORIC_EPROM_SIZE bytes. Power on or reset after it, as after
 * oric_load_rom: with the Microdisc on, the reset vector is the
 * EPROM's. */
bool oric_load_eprom(oric_t *m, const uint8_t *data, size_t len);

/* Make a region plain read/write RAM. Used by the host tests, which need a
 * bare 64 KiB machine for the Dormann and Clark suites. */
void oric_map_ram(oric_t *m, uint16_t addr, uint32_t len);

#endif /* PICO_ORIC_ORIC_H */
