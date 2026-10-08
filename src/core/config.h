/* config.h — every fixed capacity in the emulator, in one place.
 *
 * Nothing in src/core/ allocates; every buffer is sized from a constant
 * here, so the SRAM budget (design.md §3.3) is a link-time fact.
 *
 * A guest fact becomes a #define only once it is settled (design.md §16).
 * The ones below are this project's own choices, rated high there, or
 * settled; the field's timing stays configuration (oric_config_t) until
 * M4 settles it.
 *
 * Guest addresses are written #XXXX in comments and 0x in code.
 */
#ifndef PICO_ORIC_CONFIG_H
#define PICO_ORIC_CONFIG_H

/* ---- Guest address space (design.md §2.2, §6.1) ---------------------- */

#define ORIC_ADDR_SPACE     65536u
#define ORIC_PAGE_SIZE        256u  /* page_t {read, write} per page      */
#define ORIC_PAGE_COUNT     (ORIC_ADDR_SPACE / ORIC_PAGE_SIZE)

/* BASIC 1.0 or 1.1, 16 KiB at #C000-#FFFF (§2.1, §10.2). */
#define ORIC_ROM_BASE      0xC000u
#define ORIC_ROM_SIZE       16384u

/* The Oric-1 16K's RAM, which repeats through #0000-#BFFF below the ROM
 * (§2.2, §6.2; §16: settled by executing both ROMs, M3). */
#define ORIC_RAM16_SIZE     16384u

/* Page #03 is I/O; the VIA's registers are A3-A0 (§2.2, §6.4). */
#define ORIC_IO_PAGE         0x03u

/* ---- Keyboard (design.md §2.3, §2.4) ---------------------------------- */

/* Rows from PB0-PB2 through a 1-of-8 decoder, columns from AY port A:
 * both ROMs' scans (#F4C8 in 1.0, #F523 in 1.1) walk eight of each. */
#define ORIC_KEY_ROWS           8u
#define ORIC_KEY_COLS           8u

/* ---- Timing (design.md §2.1, §11) ------------------------------------ */

#define ORIC_CPU_HZ       1000000u  /* 12 MHz crystal / 12 (§16: high)     */

#endif /* PICO_ORIC_CONFIG_H */
