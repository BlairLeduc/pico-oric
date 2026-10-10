/* wd1793.c — the Microdisc's floppy disc controller (wd1793.h). */

#include "wd1793.h"

#include <string.h>

#define L ORIC_DISC_TRACK_LEN

/* The datasheet's times at 2 MHz, doubled for the Microdisc's 1 MHz
 * clock (wd1793.h): the step rates r1 r0, and the head settle of a type I
 * verify or a type II or III command's E flag. */
static const uint32_t step_cycles[4] = { 6000u, 12000u, 20000u, 30000u };
#define SETTLE_CYCLES   30000u
/* The head unloads after 15 idle revolutions. */
#define UNLOAD_REVS     15u
/* The index hole's pulse, as bytes under the head: about 2 ms. */
#define INDEX_BYTES     64u
/* Write sector: the bytes after the ID by which the first data byte must
 * be in the data register (MFM). */
#define WRITE_GRACE     22u
/* Five index pulses without the sector, and it is not there. */
#define RNF_REVS        5u

enum {
    PH_IDLE = 0,
    PH_STEPPED,     /* type I: the steps are done                      */
    PH_SETTLED,     /* the head settled: look for the ID               */
    PH_TRACK,       /* waiting on the port for the track               */
    PH_NODISC,      /* no disc: no index, nothing ever comes round      */
    PH_READ,        /* a byte assembled at due                          */
    PH_WFOUND,      /* write sector: the ID found, DRQ at due           */
    PH_WFIRST,      /* write sector: the first byte's deadline          */
    PH_WSTART,      /* write track: the index pulse                     */
    PH_WRITE,       /* a byte's slot begins at due                      */
    PH_DATA_END,    /* the data field and its CRC have passed           */
    PH_END,         /* INTRQ at due                                     */
};

/* Commands, by their top bits. */
#define IS_TYPE1(c)     (((c) & 0x80u) == 0)
#define IS_READ_SEC(c)  (((c) & 0xE0u) == 0x80u)
#define IS_WRITE_SEC(c) (((c) & 0xE0u) == 0xA0u)
#define IS_READ_ADDR(c) (((c) & 0xF0u) == 0xC0u)
#define IS_FORCE(c)     (((c) & 0xF0u) == 0xD0u)
#define IS_READ_TRK(c)  (((c) & 0xF0u) == 0xE0u)
#define IS_WRITE_TRK(c) (((c) & 0xF0u) == 0xF0u)

/* The first whole byte under the head at or after cycle t, as a count of
 * bytes since cycle 0; and the first such count at or after k0 that is
 * byte `pos` of the track. */
static uint64_t k_at(uint64_t t) { return (t + WD_BYTE_CYCLES - 1u) / WD_BYTE_CYCLES; }
static uint64_t next_k(uint64_t k0, unsigned pos) {
    unsigned r = (unsigned)(k0 % L);
    return k0 + (pos + L - r) % L;
}
static uint64_t t_of(uint64_t k) { return k * WD_BYTE_CYCLES; }

static wd_drive_t *sel(wd1793_t *f) { return &f->drv[f->drive]; }

void wd_init(wd1793_t *f) {
    memset(f, 0, sizeof *f);
    f->due = WD_NEVER;
    f->type1 = true;
}

static void idle(wd1793_t *f) {
    f->phase = PH_IDLE;
    f->due = WD_NEVER;
}

void wd_reset(wd1793_t *f) {
    /* MR: the command register gets #03 (Restore) on a real chip once
     * MR rises, which a Microdisc's EPROM issues itself anyway; here the
     * chip is left idle, its registers cleared. */
    f->status = 0;
    f->cmd = 0;
    f->track = 0;
    f->sector = 1;
    f->data = 0;
    f->intrq = f->drq = false;
    f->hold_intrq = f->index_intrq = false;
    f->type1 = true;
    f->head_until = 0;
    idle(f);
}

/* ---- the track in hand ------------------------------------------------- */

static void make_void(wd1793_t *f, uint8_t d, uint8_t s, uint8_t c) {
    memset(f->buf, 0x4E, sizeof f->buf);
    f->buf_ok = true;
    f->buf_void = true;
    f->buf_dirty = false;
    f->buf_drive = d;
    f->buf_side = s;
    f->buf_cyl = c;
    f->nsec = 0;
}

/* The selected drive's track under the head, in buf: true if it is
 * there now; otherwise the request is posted. */
static bool need_track(wd1793_t *f) {
    uint8_t d = f->drive, s = f->side, c = sel(f)->cyl;
    if (f->buf_ok && f->buf_drive == d && f->buf_side == s && f->buf_cyl == c) return true;
    const mfm_geom_t *g = &sel(f)->geom;
    bool unformatted = s >= g->sides || c >= g->tracks;
    /* A track written to is already posted back, by the command that
     * wrote it (post_put), so the port puts it before this get. */
    if (!unformatted) {
        f->req.get = true;
        f->req.drive = d;
        f->req.side = s;
        f->req.cyl = c;
    }
    if (wd_request(f)) return false;
    make_void(f, d, s, c);
    return true;
}

static void reindex(wd1793_t *f) {
    f->nsec = (uint8_t)mfm_index(f->buf, f->sec, ORIC_DISC_SECTORS_MAX);
}

/* ---- commands ------------------------------------------------------------ */

static void begin_search(wd1793_t *f);

/* A track written to goes back to the card once the command ends
 * (wd1793.h). */
static void post_put(wd1793_t *f) {
    if (!f->buf_dirty || f->buf_void) return;
    f->req.put = true;
    f->req.put_drive = f->buf_drive;
    f->req.put_side = f->buf_side;
    f->req.put_cyl = f->buf_cyl;
}

static void finish(wd1793_t *f, uint64_t t) {
    f->status &= (uint8_t)~WD_ST_BUSY;
    f->intrq = true;
    f->head_until = t + (uint64_t)UNLOAD_REVS * WD_REV_CYCLES;
    idle(f);
    post_put(f);
}

static void end_at(wd1793_t *f, uint64_t t, uint8_t bits) {
    f->status |= bits;
    f->phase = PH_END;
    f->due = t;
}

/* The sector a command wants: the first matching ID to come round from
 * f->from, its sync's byte count in *ks. -1 if none on the track. */
static int find(wd1793_t *f, uint64_t *ks) {
    uint64_t k0 = k_at(f->from), best = UINT64_MAX;
    int found = -1;
    uint8_t c = f->cmd;
    for (int i = 0; i < f->nsec; i++) {
        const mfm_sector_t *s = &f->sec[i];
        if (IS_TYPE1(c)) {
            if (s->c != f->track) continue;
        } else if (IS_READ_SEC(c) || IS_WRITE_SEC(c)) {
            if (s->c != f->track || s->r != f->sector || s->data == MFM_NO_DATA) continue;
            /* C: compare the side with S (1791/1793). */
            if ((c & 0x02u) && s->h != ((c >> 3) & 1u)) continue;
        }
        uint64_t k = next_k(k0, (s->id + L - 3u) % L);
        if (k < best) {
            best = k;
            found = i;
        }
    }
    *ks = best;
    return found;
}

/* No such sector: given up at the fifth index pulse. */
static uint64_t rnf_time(const wd1793_t *f) {
    return t_of(next_k(k_at(f->from), 0) + (uint64_t)(RNF_REVS - 1u) * L);
}

static void begin_search(wd1793_t *f) {
    if (!sel(f)->loaded) {
        f->phase = PH_NODISC;
        f->due = WD_NEVER;
        return;
    }
    if (!need_track(f)) {
        f->phase = PH_TRACK;
        f->due = WD_NEVER;
        return;
    }
    uint8_t c = f->cmd;
    if (IS_READ_TRK(c) || IS_WRITE_TRK(c)) {
        uint64_t k = next_k(k_at(f->from), 0);
        f->at = 0;
        f->len = L;
        f->pos = 0;
        f->k0 = k;
        if (IS_READ_TRK(c)) {
            f->phase = PH_READ;
            f->due = t_of(k + 1u);
        } else {
            /* DRQ at once; writing starts at the index (wd1793.h). */
            f->drq = true;
            f->crc_low = false;
            f->crc = 0xFFFFu;
            f->phase = PH_WSTART;
            f->due = t_of(k);
        }
        return;
    }

    uint64_t ks;
    int i = find(f, &ks);
    if (i < 0) {
        end_at(f, rnf_time(f), IS_TYPE1(c) ? WD_ST_SEEK_ERR : WD_ST_RNF);
        return;
    }
    const mfm_sector_t *s = &f->sec[i];
    uint64_t id_end = ks + 10u;    /* A1 A1 A1 FE C H R N and the CRC */
    if (IS_TYPE1(c)) {
        end_at(f, t_of(id_end), 0);
        return;
    }
    if (IS_READ_ADDR(c)) {
        /* The ID's track goes into the sector register (datasheet). */
        f->sector = s->c;
        f->at = (uint16_t)((s->id + 1u) % L);
        f->len = 6u;
        f->pos = 0;
        f->k0 = ks + 4u;
        f->phase = PH_READ;
        f->due = t_of(f->k0 + 1u);
        return;
    }
    uint64_t kd = ks + (s->data + L - (s->id + L - 3u) % L) % L;   /* the mark */
    f->at = (uint16_t)((s->data + 1u) % L);
    f->len = mfm_size(s->n);
    f->pos = 0;
    f->k0 = kd + 1u;
    if (IS_READ_SEC(c)) {
        if (f->buf[s->data] == 0xF8u) f->status |= WD_ST_RECTYPE;
        f->phase = PH_READ;
        f->due = t_of(f->k0 + 1u);
    } else {
        f->phase = PH_WFOUND;
        f->due = t_of(id_end);
    }
}

static void type1(wd1793_t *f, uint8_t v, uint64_t now) {
    wd_drive_t *d = sel(f);
    unsigned steps = 0;
    int dir = 0;
    switch (v & 0xF0u) {
    case 0x00:                       /* Restore: out until track 0 */
        steps = d->cyl;
        dir = -1;
        f->track = 0;
        break;
    case 0x10: {                     /* Seek: to the data register */
        unsigned from = f->track, to = f->data;
        dir = to > from ? 1 : -1;
        steps = to > from ? to - from : from - to;
        f->track = f->data;
        break;
    }
    default:                         /* Step, Step-in, Step-out */
        if ((v & 0x60u) == 0x40u) f->step_in = true;
        if ((v & 0x60u) == 0x60u) f->step_in = false;
        dir = f->step_in ? 1 : -1;
        steps = 1;
        if (v & 0x10u) f->track = (uint8_t)(f->track + dir);
        break;
    }
    if (dir) f->step_in = dir > 0;
    int cyl = (int)d->cyl + dir * (int)steps;
    if (cyl < 0) cyl = 0;
    if (cyl > (int)ORIC_DISC_TRACKS_MAX - 1) cyl = (int)ORIC_DISC_TRACKS_MAX - 1;
    d->cyl = (uint8_t)cyl;
    f->phase = PH_STEPPED;
    f->due = now + (uint64_t)steps * step_cycles[v & 3u];
}

static void force(wd1793_t *f, uint8_t v, uint64_t now) {
    if (wd_busy(f)) {
        f->status &= (uint8_t)~WD_ST_BUSY;
        f->drq = false;
        f->head_until = now + (uint64_t)UNLOAD_REVS * WD_REV_CYCLES;
        idle(f);
        post_put(f);
    } else {
        f->type1 = true;
    }
    f->hold_intrq = (v & 0x08u) != 0;
    f->index_intrq = (v & 0x04u) != 0;
    if (f->hold_intrq) f->intrq = true;
    if (f->index_intrq && sel(f)->loaded) f->due = t_of(next_k(k_at(now), 0));
}

static void command(wd1793_t *f, uint8_t v, uint64_t now) {
    /* Loading the command register clears INTRQ (datasheet). */
    if (!f->hold_intrq) f->intrq = false;
    if (IS_FORCE(v)) {
        f->intrq = false;
        force(f, v, now);
        return;
    }
    if (wd_busy(f)) return;          /* only a Force Interrupt is taken */
    f->cmd = v;
    f->hold_intrq = f->index_intrq = false;
    f->drq = false;
    f->type1 = IS_TYPE1(v);
    f->status = WD_ST_BUSY;
    f->from = now;
    f->head_until = WD_NEVER;
    if (f->type1) {
        type1(f, v, now);
        return;
    }
    if ((IS_WRITE_SEC(v) || IS_WRITE_TRK(v)) && sel(f)->protect) {
        /* Refused at once, before any byte (datasheet). */
        f->status |= WD_ST_PROTECT;
        end_at(f, now, 0);
        return;
    }
    f->phase = PH_SETTLED;
    f->due = now + ((v & 0x04u) ? SETTLE_CYCLES : 0u);
}

/* ---- events ---------------------------------------------------------------- */

static void write_track_byte(wd1793_t *f, uint8_t *out) {
    if (f->crc_low) {
        *out = (uint8_t)f->crc;
        f->crc_low = false;
        return;
    }
    uint8_t v = f->data;
    if (f->drq) {
        f->status |= WD_ST_LOST;
        v = 0;
    }
    f->drq = true;
    switch (v) {
    case 0xF5:
        /* A1 with the CRC preset: as if the generator had already taken
         * two A1s, so three F5s give the standard CRC (Oricutron does the
         * same, disk.c). */
        f->crc = mfm_crc(mfm_crc(0xFFFFu, 0xA1u), 0xA1u);
        f->crc = mfm_crc(f->crc, 0xA1u);
        *out = 0xA1u;
        break;
    case 0xF6:
        f->crc = mfm_crc(f->crc, 0xC2u);
        *out = 0xC2u;
        break;
    case 0xF7:
        *out = (uint8_t)(f->crc >> 8);
        f->crc_low = true;
        break;
    default:
        f->crc = mfm_crc(f->crc, v);
        *out = v;
        break;
    }
}

static void event(wd1793_t *f) {
    uint64_t t = f->due;
    uint8_t c = f->cmd;
    switch (f->phase) {
    case PH_IDLE:
        /* The only idle event: Force Interrupt's index interrupt. */
        if (f->index_intrq) {
            f->intrq = true;
            f->due = t + WD_REV_CYCLES;
        } else {
            f->due = WD_NEVER;
        }
        return;
    case PH_STEPPED:
        if (c & 0x04u) {             /* V: settle, then verify */
            f->phase = PH_SETTLED;
            f->due = t + SETTLE_CYCLES;
        } else {
            finish(f, t);
        }
        return;
    case PH_SETTLED:
        f->from = t;
        begin_search(f);
        return;
    case PH_READ:
        /* A byte assembled: one the CPU has not taken is lost. */
        if (f->drq) f->status |= WD_ST_LOST;
        f->data = f->buf[(f->at + f->pos) % L];
        f->drq = true;
        f->pos++;
        if (f->pos < f->len) {
            f->due = t + WD_BYTE_CYCLES;
        } else if (IS_READ_SEC(c)) {
            f->phase = PH_DATA_END;
            f->due = t_of(f->k0 + f->len + 2u);
        } else {
            /* An address's CRC, or the track's last byte: INTRQ a byte
             * later, so that the CPU can take it (wd1793.h). */
            f->phase = PH_END;
            f->due = t + WD_BYTE_CYCLES;
        }
        return;
    case PH_WFOUND:
        f->drq = true;
        f->phase = PH_WFIRST;
        f->due = t + WRITE_GRACE * WD_BYTE_CYCLES;
        return;
    case PH_WFIRST:
        if (f->drq) {
            f->drq = false;
            finish(f, t);
            f->status |= WD_ST_LOST;
            return;
        }
        f->phase = PH_WRITE;
        f->due = t_of(f->k0);
        return;
    case PH_WSTART:
        if (f->drq) {
            f->drq = false;
            f->status |= WD_ST_LOST;
            finish(f, t);
            return;
        }
        f->phase = PH_WRITE;
        f->due = t_of(f->k0);
        /* fall through: the first slot begins at the index */
        /* FALLTHRU */
    case PH_WRITE: {
        uint8_t *p = &f->buf[(f->at + f->pos) % L];
        if (IS_WRITE_TRK(c)) {
            write_track_byte(f, p);
        } else {
            uint8_t v = f->data;
            if (f->pos > 0 && f->drq) {
                f->status |= WD_ST_LOST;
                v = 0;
            }
            /* The mark goes out with the first byte: a write given up
             * before it leaves the sector as it was. a0 is a deleted one. */
            if (f->pos == 0) f->buf[(f->at + L - 1u) % L] = (c & 1u) ? 0xF8u : 0xFBu;
            *p = v;
            if (f->pos + 1u < f->len) f->drq = true;
        }
        if (!f->buf_void) f->buf_dirty = true;
        f->pos++;
        if (f->pos < f->len) {
            f->due = t_of(f->k0 + f->pos);
        } else if (IS_WRITE_TRK(c)) {
            /* The last slot ends at the index. */
            f->drq = false;
            reindex(f);
            f->tracks_written++;
            f->phase = PH_END;
            f->due = t_of(f->k0 + f->len);
        } else {
            /* The data's CRC, over the mark's sync, the mark and the data. */
            unsigned mark = (f->at + L - 1u) % L;
            uint16_t crc = mfm_crc_block(0xFFFFu, (const uint8_t *)"\xA1\xA1\xA1", 3);
            crc = mfm_crc(crc, f->buf[mark]);
            for (unsigned i = 0; i < f->len; i++) crc = mfm_crc(crc, f->buf[(f->at + i) % L]);
            f->buf[(f->at + f->len) % L] = (uint8_t)(crc >> 8);
            f->buf[(f->at + f->len + 1u) % L] = (uint8_t)crc;
            f->phase = PH_DATA_END;
            f->due = t_of(f->k0 + f->len + 2u);
        }
        return;
    }
    case PH_DATA_END:
        if (IS_READ_SEC(c)) f->sectors_read++;
        else f->sectors_written++;
        if (c & 0x10u) {             /* m: on to the next sector */
            f->sector++;
            f->from = t;
            begin_search(f);
        } else {
            finish(f, t);
        }
        return;
    case PH_END:
        finish(f, t);
        return;
    default:
        f->due = WD_NEVER;
        return;
    }
}

void wd_run(wd1793_t *f, uint64_t now) {
    while (f->due <= now) event(f);
}

/* ---- registers --------------------------------------------------------- */

static uint8_t status_now(wd1793_t *f, uint64_t now) {
    if (!f->type1) return (uint8_t)((f->status & (uint8_t)~WD_ST_DRQ) | (f->drq ? WD_ST_DRQ : 0));
    const wd_drive_t *d = sel(f);
    uint8_t s = f->status & (WD_ST_BUSY | WD_ST_SEEK_ERR | WD_ST_CRC);
    if (d->protect) s |= WD_ST_PROTECT;
    if (now < f->head_until || (wd_busy(f) && (f->cmd & 0x08u))) s |= WD_ST_HEAD;
    if (d->cyl == 0) s |= WD_ST_TRACK0;
    if (d->loaded && (now / WD_BYTE_CYCLES) % L < INDEX_BYTES) s |= WD_ST_INDEX;
    return s;
}

uint8_t wd_read(wd1793_t *f, unsigned reg, uint64_t now) {
    wd_run(f, now);
    switch (reg & 3u) {
    case 0: {
        uint8_t s = status_now(f, now);
        /* Reading the status clears INTRQ, but not an immediate one
         * (datasheet). */
        if (!f->hold_intrq) f->intrq = false;
        return s;
    }
    case 1: return f->track;
    case 2: return f->sector;
    default:
        f->drq = false;
        return f->data;
    }
}

void wd_write(wd1793_t *f, unsigned reg, uint8_t v, uint64_t now) {
    wd_run(f, now);
    switch (reg & 3u) {
    case 0: command(f, v, now); break;
    case 1: f->track = v; break;
    case 2: f->sector = v; break;
    default:
        f->data = v;
        f->drq = false;
        break;
    }
}

void wd_select(wd1793_t *f, unsigned drive, unsigned side, uint64_t now) {
    wd_run(f, now);
    f->drive = (uint8_t)(drive % ORIC_DISC_DRIVES);
    f->side = (uint8_t)(side & 1u);
}

/* ---- the port ------------------------------------------------------------ */

void wd_served(wd1793_t *f, bool ok, uint64_t now) {
    wd_req_t r = f->req;
    memset(&f->req, 0, sizeof f->req);
    if (r.put) {
        /* Kept or lost, it is no longer the chip's to keep. */
        f->buf_dirty = false;
    }
    if (r.get) {
        f->buf_ok = true;
        f->buf_void = !ok;
        f->buf_dirty = false;
        f->buf_drive = r.drive;
        f->buf_side = r.side;
        f->buf_cyl = r.cyl;
        if (ok) {
            reindex(f);
        } else {
            memset(f->buf, 0x4E, sizeof f->buf);
            f->nsec = 0;
        }
    }
    if (f->phase == PH_TRACK) {
        if (f->from < now) f->from = now;
        begin_search(f);
    }
}

static void drive_changed(wd1793_t *f, unsigned drive, uint64_t now) {
    if (f->buf_ok && f->buf_drive == drive) {
        f->buf_ok = false;
        f->buf_dirty = false;
    }
    if (f->req.get && f->req.drive == drive) f->req.get = false;
    if (f->req.put && f->req.put_drive == drive) f->req.put = false;
    /* A command on that drive looks again from now: one waiting for a
     * disc finds this one, one in the middle of a transfer starts its
     * search again. */
    if (wd_busy(f) && f->drive == drive && f->phase != PH_STEPPED && f->phase != PH_END) {
        f->from = now;
        f->drq = false;
        begin_search(f);
    }
}

void wd_insert(wd1793_t *f, unsigned drive, mfm_geom_t g, bool protect, uint64_t now) {
    if (drive >= ORIC_DISC_DRIVES) return;
    wd_drive_t *d = &f->drv[drive];
    d->loaded = true;
    d->protect = protect;
    d->geom = g;
    drive_changed(f, drive, now);
}

void wd_eject(wd1793_t *f, unsigned drive, uint64_t now) {
    if (drive >= ORIC_DISC_DRIVES) return;
    f->drv[drive].loaded = false;
    f->drv[drive].protect = false;
    drive_changed(f, drive, now);
}
