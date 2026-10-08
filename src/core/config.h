/* config.h — every fixed capacity in the emulator, in one place.
 *
 * Nothing in src/core/ allocates; every buffer is sized from a constant
 * here, so the SRAM budget (design.md §3.3) is a link-time fact.
 *
 * A guest fact becomes a #define only once it is settled (design.md §16).
 * The ones below are this project's own choices or rated high there; the
 * mirrors, the field's timing and the rest arrive as M3 and M4 settle them.
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

/* Page #03 is I/O; the VIA's registers are A3-A0 (§2.2, §6.4). */
#define ORIC_IO_PAGE         0x03u

/* ---- Timing (design.md §2.1, §11) ------------------------------------ */

#define ORIC_CPU_HZ       1000000u  /* 12 MHz crystal / 12 (§16: high)     */

#endif /* PICO_ORIC_CONFIG_H */
