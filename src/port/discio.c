/* discio.c — Microdisc images on the card (discio.h). */

#include "discio.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "pico/time.h"

#include "ff.h"

#include "keymapio.h"
#include "log.h"
#include "mfmdisk.h"
#include "microdisc.h"
#include "storage.h"

static FIL s_file;

static struct {
    char       path[ORIC_PATH_MAX];
    mfm_geom_t geom;
    bool       protect;
} s_drive[ORIC_DISC_DRIVES];

static uint32_t s_got, s_put;

static bool serve_mounted(oric_t *m, bool mounted, uint32_t t0, uint32_t *us);

/* Neither a directory nor macOS's AppleDouble "._" shadow of a file. */
static bool is_disc(const char *fname) {
    if (fname[0] == '.') return false;
    size_t n = strlen(fname);
    return n > 4 && strcasecmp(fname + n - 4, ".dsk") == 0;
}

unsigned discio_list(discio_entry_t *out, unsigned max) {
    DIR dir;
    static FILINFO fi;
    unsigned n = 0;
    if (f_opendir(&dir, DISCIO_DIR) != FR_OK) return 0;
    while (n < max && f_readdir(&dir, &fi) == FR_OK && fi.fname[0]) {
        if ((fi.fattrib & AM_DIR) || !is_disc(fi.fname)) continue;
        discio_entry_t *e = &out[n];
        int len = snprintf(e->path, sizeof e->path, DISCIO_DIR "/%s", fi.fname);
        if (len < 0 || (size_t)len >= sizeof e->path) continue;
        e->size = (uint32_t)fi.fsize;
        e->protect = (fi.fattrib & AM_RDO) != 0;
        n++;
    }
    f_closedir(&dir);
    return n;
}

/* Put back what the drive has written, and drop its request. */
static void settle_drive(oric_t *m, unsigned drive) {
    if (!m) return;
    const wd_req_t *r = oric_disc_request(m);
    if (r && ((r->put && r->put_drive == drive) || (r->get && r->drive == drive))) {
        uint32_t us;
        (void)serve_mounted(m, true, time_us_32(), &us);
    }
}

const char *discio_insert(oric_t *m, unsigned drive, const char *path) {
    if (drive >= ORIC_DISC_DRIVES) return "NO SUCH DRIVE";
    settle_drive(m, drive);
    s_drive[drive].path[0] = 0;
    if (m) oric_disc_eject(m, drive);
    if (!path || !path[0]) return NULL;
    if (strlen(path) >= sizeof s_drive[drive].path) return "NAME TOO LONG";

    static FILINFO fi;
    if (f_stat(path, &fi) != FR_OK) return "CANNOT OPEN";
    uint8_t hdr[ORIC_DISC_HEADER_LEN];
    UINT got = 0;
    if (f_open(&s_file, path, FA_READ) != FR_OK) return "CANNOT OPEN";
    FRESULT fr = f_read(&s_file, hdr, sizeof hdr, &got);
    f_close(&s_file);
    if (fr != FR_OK) return "CANNOT READ";
    mfm_geom_t g;
    const char *why = mfm_parse(hdr, got, (uint32_t)fi.fsize, &g);
    if (why) return why;

    strcpy(s_drive[drive].path, path);
    s_drive[drive].geom = g;
    s_drive[drive].protect = (fi.fattrib & AM_RDO) != 0;
    if (m) oric_disc_insert(m, drive, g, s_drive[drive].protect);
    log_core1("  disc         : drive %c: %s, %u side%s of %u tracks%s\n", 'A' + drive, path,
              g.sides, g.sides > 1 ? "s" : "", g.tracks,
              s_drive[drive].protect ? ", write-protected" : "");
    keymapio_file_loaded(path);   /* a layout may name it (design.md §9.4) */
    return NULL;
}

const char *discio_inserted(unsigned drive) {
    return drive < ORIC_DISC_DRIVES ? s_drive[drive].path : "";
}

bool discio_protected(unsigned drive) {
    return drive < ORIC_DISC_DRIVES && s_drive[drive].protect;
}

void discio_attach(oric_t *m) {
    for (unsigned d = 0; d < ORIC_DISC_DRIVES; d++)
        if (s_drive[d].path[0]) oric_disc_insert(m, d, s_drive[d].geom, s_drive[d].protect);
}

uint32_t discio_tracks(bool written) {
    return written ? s_put : s_got;
}

/* One track at its place in the drive's file, into or out of buf. */
static bool transfer(unsigned drive, unsigned side, unsigned cyl, uint8_t *buf, bool put) {
    if (drive >= ORIC_DISC_DRIVES || !s_drive[drive].path[0]) return false;
    const mfm_geom_t *g = &s_drive[drive].geom;
    if (side >= g->sides || cyl >= g->tracks) return false;
    if (put && s_drive[drive].protect) return false;
    FSIZE_t at = mfm_track_offset(*g, side, cyl);
    if (f_open(&s_file, s_drive[drive].path, put ? (FA_READ | FA_WRITE) : FA_READ) != FR_OK)
        return false;
    UINT done = 0;
    bool ok = f_lseek(&s_file, at) == FR_OK &&
              (put ? f_write(&s_file, buf, ORIC_DISC_TRACK_LEN, &done)
                   : f_read(&s_file, buf, ORIC_DISC_TRACK_LEN, &done)) == FR_OK &&
              done == ORIC_DISC_TRACK_LEN;
    return f_close(&s_file) == FR_OK && ok;
}

bool discio_serve(oric_t *m, uint32_t *us) {
    *us = 0;
    if (!oric_disc_request(m)) return true;
    uint32_t t0 = time_us_32();
    bool mounted = storage_mount() == 0;
    bool ok = serve_mounted(m, mounted, t0, us);
    if (mounted) storage_unmount();
    return ok;
}

/* The request, with the card mounted or not: without, it fails. */
static bool serve_mounted(oric_t *m, bool mounted, uint32_t t0, uint32_t *us) {
    wd_req_t q = *oric_disc_request(m);
    bool ok = mounted;
    if (ok && q.put) {
        ok = transfer(q.put_drive, q.put_side, q.put_cyl, m->fdc.buf, true);
        if (ok) s_put++;
    }
    if (ok && q.get) {
        ok = transfer(q.drive, q.side, q.cyl, m->fdc.buf, false);
        if (ok) s_got++;
    }
    *us = time_us_32() - t0;
    char put[24] = "", get[24] = "";
    if (q.put) snprintf(put, sizeof put, "put %c%u:%u ", 'A' + q.put_drive, q.put_side, q.put_cyl);
    if (q.get) snprintf(get, sizeof get, "get %c%u:%u ", 'A' + q.drive, q.side, q.cyl);
    log_core1("  disc         : %s%s%s, %lu us\n", put, get, ok ? "ok" : "FAILED",
              (unsigned long)*us);
    oric_disc_served(m, ok);
    return ok;
}
