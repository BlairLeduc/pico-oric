/* microdisc.h — the Microdisc on the expansion port (design.md §10.5,
 * §16).
 *
 * A WD1793 (wd1793.h), an 8 KiB EPROM and a latch. Read off the EPROM
 * and agreed by MAME's microdisc.cpp and Oricutron's disk.c:
 *
 *   #0310-#0313  the WD1793's registers
 *   #0314 write  the latch:
 *       bit 0  INTRQ onto the 6502's IRQ           MD_IRQEN
 *       bit 1  set: the BASIC ROM at #C000; clear: /ROMDIS, the ROM
 *              switched out and the overlay RAM in its place   MD_BASIC
 *       bit 2  (the EPROM always sets it; MAME's "DDS"; unused here)
 *       bit 3  density (MAME's DDEN; every image is MFM)
 *       bit 4  side                                MD_SIDE
 *       bits 5-6 drive                             MD_DRIVE
 *       bit 7  set: the EPROM off; clear, with bit 1 clear, the EPROM
 *              at #E000-#FFFF over the overlay RAM   MD_EPROM_OFF
 *   #0314 read   bit 7 clear while INTRQ is (Oricutron; MAME gates it
 *                with bit 0, §16), the rest set
 *   #0318 read   bit 7 clear while DRQ is, the rest set
 *
 * Everything else in page #03 is the VIA's, #0318's writes included
 * (MAME). RESET clears the latch, so the 6502 takes its reset vector
 * from the EPROM (#EB7E), which boots the DOS off the disc; the EPROM's
 * IRQ handler (#E3C0) ends each command by INTRQ, and its loops move a
 * byte per DRQ by polling #0318 (#E2EF, #E309). Writes to the EPROM's
 * addresses while it is in are lost, as are writes to the BASIC ROM's.
 *
 * A 48K machine only: the overlay RAM is the top 16 KiB of its 64.
 */
#ifndef PICO_ORIC_MICRODISC_H
#define PICO_ORIC_MICRODISC_H

#include <stdbool.h>
#include <stdint.h>

#include "oric.h"

#define MD_IRQEN      0x01u
#define MD_BASIC      0x02u
#define MD_SIDE       0x10u
#define MD_DRIVE      0x60u
#define MD_EPROM_OFF  0x80u

/* bus.c's: whether the Microdisc answers this address in page #03. */
static inline bool microdisc_decodes(const oric_t *m, uint16_t a, bool write) {
    uint8_t lo = (uint8_t)a;
    return m->cfg.microdisc && (lo - 0x10u <= 4u || (!write && lo == 0x18u));
}

uint8_t microdisc_read(oric_t *m, uint16_t a);
void    microdisc_write(oric_t *m, uint16_t a, uint8_t v);

/* #C000-#FFFF as the latch has them, or the BASIC ROM without the
 * Microdisc. */
void microdisc_map(oric_t *m);

/* The RESET line: the latch cleared, the chip's command dropped. */
void microdisc_reset(oric_t *m);

/* INTRQ onto IRQ, through the latch's bit 0. */
void microdisc_irq(oric_t *m);

/* ---- the port's -------------------------------------------------------- */

/* The track the controller is waiting on, or NULL; served at a field
 * boundary with the guest parked (design.md §10.5). */
const wd_req_t *oric_disc_request(const oric_t *m);

/* buf has been put back and filled (wd_served). */
void oric_disc_served(oric_t *m, bool ok);

/* A disc in a drive, or out; the port puts a written track back first. */
void oric_disc_insert(oric_t *m, unsigned drive, mfm_geom_t g, bool protect);
void oric_disc_eject(oric_t *m, unsigned drive);

/* A command in progress or a track waiting to go: a snapshot now would
 * lose it (design.md §10.6). */
bool oric_disc_busy(const oric_t *m);

#endif /* PICO_ORIC_MICRODISC_H */
