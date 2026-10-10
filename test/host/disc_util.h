/* disc_util.h — discs held in memory for the host tests, served as the
 * port serves the card's (design.md §10.5, microdisc.h).
 *
 * A disc is a whole MFM_DISK image in a buffer; a request puts a track
 * back into it and fills the controller's buffer from it, as discio.c
 * does from a file. Header-only, as snap_util.h is.
 */
#ifndef PICO_ORIC_TEST_DISC_UTIL_H
#define PICO_ORIC_TEST_DISC_UTIL_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mfmdisk.h"
#include "microdisc.h"
#include "oric.h"

typedef struct {
    uint8_t   *img;
    uint32_t   len;
    mfm_geom_t g;
    bool       protect;
    uint32_t   gets, puts;
} host_disc_t;

/* A blank, formatted disc of g's geometry, 17 sectors a track. */
static inline void host_disc_blank(host_disc_t *d, mfm_geom_t g, unsigned sectors) {
    memset(d, 0, sizeof *d);
    d->g = g;
    d->len = mfm_track_offset(g, g.sides - 1u, g.tracks);
    d->img = (uint8_t *)calloc(1, d->len);
    mfm_header(d->img, g);
    for (unsigned s = 0; s < g.sides; s++)
        for (unsigned t = 0; t < g.tracks; t++)
            mfm_format_track(d->img + mfm_track_offset(g, s, t), (uint8_t)t, (uint8_t)s, sectors, 0xE5);
}

/* An image file; NULL, or why it was refused. */
static inline const char *host_disc_load(host_disc_t *d, const char *path) {
    memset(d, 0, sizeof *d);
    FILE *f = fopen(path, "rb");
    if (!f) return "cannot open";
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    d->img = (uint8_t *)malloc((size_t)n);
    d->len = (uint32_t)n;
    bool ok = fread(d->img, 1, (size_t)n, f) == (size_t)n;
    fclose(f);
    if (!ok) return "cannot read";
    return mfm_parse(d->img, d->len, d->len, &d->g);
}

static inline bool host_disc_save(const host_disc_t *d, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(d->img, 1, d->len, f) == d->len;
    return fclose(f) == 0 && ok;
}

static inline void host_disc_free(host_disc_t *d) {
    free(d->img);
    memset(d, 0, sizeof *d);
}

static inline void host_disc_insert(oric_t *m, unsigned drive, host_disc_t *d) {
    oric_disc_insert(m, drive, d->g, d->protect);
}

/* The request, if any, from discs[drive] (NULL for an empty drive). */
static inline void host_disc_serve(oric_t *m, host_disc_t *discs[ORIC_DISC_DRIVES]) {
    const wd_req_t *r = oric_disc_request(m);
    if (!r) return;
    bool ok = true;
    if (r->put) {
        host_disc_t *d = discs[r->put_drive];
        if (d && !d->protect) {
            memcpy(d->img + mfm_track_offset(d->g, r->put_side, r->put_cyl), m->fdc.buf,
                   ORIC_DISC_TRACK_LEN);
            d->puts++;
        } else {
            ok = false;
        }
    }
    if (r->get) {
        host_disc_t *d = discs[r->drive];
        if (d) {
            memcpy(m->fdc.buf, d->img + mfm_track_offset(d->g, r->side, r->cyl),
                   ORIC_DISC_TRACK_LEN);
            d->gets++;
        } else {
            ok = false;
        }
    }
    oric_disc_served(m, ok);
}

#endif /* PICO_ORIC_TEST_DISC_UTIL_H */
