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
 * sound arrives with M8.
 */
#ifndef PICO_ORIC_ORIC_H
#define PICO_ORIC_ORIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ay8912.h"
#include "config.h"
#include "m6502.h"
#include "romset.h"
#include "ula.h"
#include "via6522.h"

/* Page descriptor flags (§6.1). */
#define PAGE_ROM   0x01u  /* writes ignored                            */
#define PAGE_IO    0x02u  /* reads and writes take the slow path       */
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
} oric_config_t;

typedef struct oric_s {
    m6502_t   cpu;
    via6522_t via;
    ay8912_t  ay;

    /* The keyboard: a bit per column, set while the key is down, for
     * each of the eight rows PB0-PB2 select (§2.3, §2.4). */
    uint8_t keys[ORIC_KEY_ROWS];

    /* All 64 KiB, the top 16 KiB being the overlay RAM under the ROM
     * (§2.1). The ROM has its own array, so that RAM stays beneath it. */
    uint8_t ram[ORIC_ADDR_SPACE];
    uint8_t rom[ORIC_ROM_SIZE];
    page_t  page[ORIC_PAGE_COUNT];
    uint8_t page_flags[ORIC_PAGE_COUNT];

    /* Open bus is modelled as the last value on the bus (EL §4.1). */
    uint8_t open_bus;

    oric_config_t cfg;

    /* The ULA's mode attribute bits (ula.h) after the last frame it drew,
     * which set the next field's length (§11.1), and as they were at that
     * frame's start, which oric_video_take hands on (§7.4). */
    uint8_t ula_mode;
    uint8_t frame_mode;

    /* Cycles of the current instruction the VIA has already been ticked
     * through, to reach an access part-way into it (§5.3). */
    uint32_t via_early;

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

/* Whether blinking cells show in the frame just finished (§16). */
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
 * VIA through the instruction's cycles before the access (§5.3). */
void oric_via_catch_up(oric_t *m);

/* Read a guest address as the CPU would, without its side effects: page
 * #03 reads as the open bus. For tests and the presenter. */
uint8_t oric_peek(const oric_t *m, uint16_t addr);

/* Copy a whole machine. The page table points into the struct, so a
 * plain assignment leaves the copy reading the original's memory; this
 * moves the pointers across with the bytes (§4.2). */
void oric_copy(oric_t *dst, const oric_t *src);

/* Put a 16 KiB BASIC ROM in the socket at #C000 (§2.2). The image is
 * copied; false if it is not exactly ORIC_ROM_SIZE bytes. */
bool oric_load_rom(oric_t *m, const uint8_t *data, size_t len);

/* Make a region plain read/write RAM. Used by the host tests, which need a
 * bare 64 KiB machine for the Dormann and Clark suites. */
void oric_map_ram(oric_t *m, uint16_t addr, uint32_t len);

#endif /* PICO_ORIC_ORIC_H */
