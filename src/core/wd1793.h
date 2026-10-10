/* wd1793.h — the Western Digital 1793 floppy disc controller the
 * Microdisc carries (design.md §10.5, §16).
 *
 * The chip as the EPROM and Sedoric drive it: command and status at
 * #0310, track #0311, sector #0312, data #0313; INTRQ ends every command
 * and DRQ asks for each byte. MAME's Microdisc clocks it at 1 MHz (8 MHz
 * / 8), so every time in the datasheet's 2 MHz column is doubled here:
 * steps of 6, 12, 20 or 30 ms, a 30 ms head settle. All four types of
 * command are modelled, and the data-mark, record-not-found and
 * lost-data rules; CRCs are not checked (below).
 *
 * The disc turns. A track is ORIC_DISC_TRACK_LEN bytes of MFM_DISK
 * (mfmdisk.h), a byte every WD_BYTE_CYCLES, so a revolution is 204,800
 * cycles, 293 rpm; byte 0 is the index hole. The byte under the head at
 * cycle t is byte (t / 32) mod 6400 of the track, for every drive alike.
 * A search waits for its ID to come round, a transfer moves a byte every
 * 32 cycles and loses one the CPU has not taken in time, and a sector
 * that is not there is given up after five index pulses: rotation is
 * what makes a real Microdisc as slow as it is, and nothing is sped up.
 *
 * A sector's INTRQ comes as its CRC passes the head; a read address's
 * and a read track's, whose last byte is their last, a byte's time
 * after it, so that a CPU ending its loop on INTRQ, as the EPROM's IRQ
 * handler ends it (#E3C0), has taken that byte. The datasheet gives no
 * figure.
 *
 * Every timed step is an event at an exact cycle: `due` is the next, and
 * the run loop ends its slice there (oric.c), as it does for the
 * cassette. Register accesses bring the chip to their own cycle first.
 * Events run at their own times, not the boundary's, so a byte is never
 * late because an instruction ran past it.
 *
 * The chip holds no disc. It knows each drive's geometry and holds one
 * raw track, the one it last needed. A command that needs another posts
 * a request and waits, busy, as a drive does while its head settles; the
 * port serves it at the next field boundary with the guest parked
 * (design.md §10.5, EL §8.4), and the search starts from the cycle it is
 * served at. A track the chip has written to is posted back once the
 * command ends, and before any other track is read into its place.
 * Tracks beyond an image's geometry, and the second side of a one-sided
 * image, are unformatted: nothing is found there, and nothing written
 * there is kept.
 *
 * Not modelled: CRC errors, since 24 of the archive's 221 images carry
 * CRCs that tools never computed, and Oricutron loads them; density, as
 * every image is MFM; READY, which the Microdisc ties active (MAME's
 * force_ready), so a drive with no disc in it has no index pulses, and
 * a command on it runs until a Force Interrupt, as a real one does.
 */
#ifndef PICO_ORIC_WD1793_H
#define PICO_ORIC_WD1793_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"
#include "mfmdisk.h"

/* Status bits. Type I commands show the drive's own; types II and III
 * show the transfer's. */
#define WD_ST_NOT_READY  0x80u
#define WD_ST_PROTECT    0x40u
#define WD_ST_HEAD       0x20u   /* type I: head loaded                 */
#define WD_ST_RECTYPE    0x20u   /* read sector: a deleted data mark     */
#define WD_ST_WRITE_FAULT 0x20u  /* write: unused here                   */
#define WD_ST_SEEK_ERR   0x10u   /* type I                               */
#define WD_ST_RNF        0x10u   /* types II, III                        */
#define WD_ST_CRC        0x08u
#define WD_ST_TRACK0     0x04u   /* type I                               */
#define WD_ST_LOST       0x04u   /* types II, III                        */
#define WD_ST_INDEX      0x02u   /* type I                               */
#define WD_ST_DRQ        0x02u   /* types II, III                        */
#define WD_ST_BUSY       0x01u

#define WD_BYTE_CYCLES   32u
#define WD_REV_CYCLES    (ORIC_DISC_TRACK_LEN * WD_BYTE_CYCLES)
#define WD_NEVER         UINT64_MAX

typedef struct {
    bool       loaded;
    bool       protect;
    mfm_geom_t geom;
    uint8_t    cyl;        /* the head, physically               */
} wd_drive_t;

/* What the chip is waiting on the port for. Either or both: a written
 * track goes back first, then the wanted one comes in. */
typedef struct {
    bool    put;            /* buf back to put_drive/side/cyl      */
    uint8_t put_drive, put_side, put_cyl;
    bool    get;            /* then drive/side/cyl into buf        */
    uint8_t drive, side, cyl;
} wd_req_t;

typedef struct {
    /* The registers, INTRQ and DRQ. */
    uint8_t status, track, sector, data, cmd;
    bool    intrq, drq;
    bool    type1;          /* the last command was type I: live status */
    bool    hold_intrq;     /* Force Interrupt's immediate: until the next */
    bool    index_intrq;    /* Force Interrupt's: INTRQ at every index */
    bool    step_in;        /* the last step's direction           */
    uint64_t head_until;    /* the head unloads at this cycle      */

    /* The drive and side the Microdisc's latch selects (microdisc.c). */
    uint8_t drive, side;
    wd_drive_t drv[ORIC_DISC_DRIVES];

    /* The command in progress. */
    uint8_t  phase;
    uint64_t due;           /* the next event, or WD_NEVER          */
    uint64_t from;          /* a search starts at this cycle        */
    uint16_t pos;           /* the next byte of a transfer          */
    uint16_t len;           /* its length                           */
    uint16_t at;            /* where it starts in buf               */
    uint64_t k0;            /* the byte under the head when it began */
    uint16_t crc;           /* write track's generator              */
    bool     crc_low;       /* write track: the CRC's second byte next */

    /* The track in hand, which sectors it holds, and the request. */
    uint8_t  buf[ORIC_DISC_TRACK_LEN];
    bool     buf_ok, buf_void, buf_dirty;
    uint8_t  buf_drive, buf_side, buf_cyl;
    mfm_sector_t sec[ORIC_DISC_SECTORS_MAX];
    uint8_t  nsec;
    wd_req_t req;

    /* For the heartbeat: sectors moved, and tracks formatted. */
    uint32_t sectors_read, sectors_written, tracks_written;
} wd1793_t;

/* Power on: no discs, heads at track 0, nothing in progress. */
void wd_init(wd1793_t *f);

/* The MR pin, on the Oric's RESET line: the command in progress is
 * dropped. Discs, heads and the track in hand stay. */
void wd_reset(wd1793_t *f);

uint8_t wd_read(wd1793_t *f, unsigned reg, uint64_t now);
void    wd_write(wd1793_t *f, unsigned reg, uint8_t v, uint64_t now);

/* The events due at or before now, each at its own cycle. */
void wd_run(wd1793_t *f, uint64_t now);

/* The latch's drive and side, from now. */
void wd_select(wd1793_t *f, unsigned drive, unsigned side, uint64_t now);

/* The request the chip is waiting on, or NULL. */
static inline const wd_req_t *wd_request(const wd1793_t *f) {
    return (f->req.put || f->req.get) ? &f->req : NULL;
}

/* The port has served the request at cycle now: put back, and filled
 * buf. ok false if the card failed it: a failed read is an unformatted
 * track, a failed write is lost. */
void wd_served(wd1793_t *f, bool ok, uint64_t now);

/* A disc in a drive, or none. The port puts back a written track first
 * (wd_request); the track in hand is dropped if it was that drive's. */
void wd_insert(wd1793_t *f, unsigned drive, mfm_geom_t g, bool protect, uint64_t now);
void wd_eject(wd1793_t *f, unsigned drive, uint64_t now);

/* Busy on a command: a snapshot now would lose it. */
static inline bool wd_busy(const wd1793_t *f) { return (f->status & WD_ST_BUSY) != 0; }

#endif /* PICO_ORIC_WD1793_H */
