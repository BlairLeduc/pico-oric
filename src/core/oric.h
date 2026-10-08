/* oric.h — the guest machine (design.md §4.2, §6).
 *
 * All state is here or in statically sized buffers from config.h; core/
 * performs no dynamic allocation, which is what makes the §3.3 budget a
 * link-time fact.
 *
 * M1 builds what the 6502 and the VIA need to run: the page table, RAM,
 * the ROM socket, page #03's decode and the run loop. The rest of §4.2's
 * seam (the field, NMI, the AY, the keyboard, video) arrives with M3.
 */
#ifndef PICO_ORIC_ORIC_H
#define PICO_ORIC_ORIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "m6502.h"
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

/* Configuration, not code (§6.2), applied only by a power-on. */
typedef struct {
    oric_ram_t ram;
} oric_config_t;

typedef struct oric_s {
    m6502_t   cpu;
    via6522_t via;

    /* All 64 KiB, the top 16 KiB being the overlay RAM under the ROM
     * (§2.1). The ROM has its own array, so that RAM stays beneath it. */
    uint8_t ram[ORIC_ADDR_SPACE];
    uint8_t rom[ORIC_ROM_SIZE];
    page_t  page[ORIC_PAGE_COUNT];
    uint8_t page_flags[ORIC_PAGE_COUNT];

    /* Open bus is modelled as the last value on the bus (EL §4.1). */
    uint8_t open_bus;

    oric_config_t cfg;

    /* Cycle debt carried between slices (§4.2). */
    int32_t budget;

    /* Instructions executed, for host cycles per guest instruction
     * (§14). A counter, not machine state: snapshots leave it. */
    uint64_t instructions;
} oric_t;

/* The power-on default is the Atmos 48K (§18 item 3); the ROM is the
 * port's to load. */
void oric_config_default(oric_config_t *cfg);

/* Wire up the page table from cfg, zero RAM and reset every chip (§6.3). */
void oric_init(oric_t *m, const oric_config_t *cfg);

/* The RESET line: the CPU and the VIA, RAM kept (§4.2, §6.3). */
void oric_reset(oric_t *m);

/* Run at least `cycles` guest cycles, finishing whole instructions.
 * Returns the cycles actually run, which the caller carries as debt. */
uint32_t oric_run(oric_t *m, uint32_t cycles);

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
