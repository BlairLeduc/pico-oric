/* discio.c — Microdisc images on the card (discio.h). */

#include "discio.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "pico/time.h"

#include "ff.h"

#include "handoff.h"
#include "kbd.h"
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

/* The drives' types: the default until the settings set them. */
static mfm_geom_t s_type[ORIC_DISC_DRIVES] = {
    { ORIC_DISC_DRIVE_SIDES, ORIC_DISC_DRIVE_TRACKS }, { ORIC_DISC_DRIVE_SIDES, ORIC_DISC_DRIVE_TRACKS },
    { ORIC_DISC_DRIVE_SIDES, ORIC_DISC_DRIVE_TRACKS }, { ORIC_DISC_DRIVE_SIDES, ORIC_DISC_DRIVE_TRACKS },
};
_Static_assert(ORIC_DISC_DRIVES == 4u, "a type for each drive");

/* A new disc is written this much at a time, polling the keyboard
 * between pieces at the live loop's rate, which also feeds the MCU's
 * 2.5 s bus watchdog (hardware-notes.md §6.1). */
#define NEW_PIECE 1600u
#define POLL_US   33333u
static uint8_t s_piece[NEW_PIECE];

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

const char *discio_probe(const char *path, mfm_geom_t *g, bool *protect) {
    if (strlen(path) >= sizeof s_drive[0].path) return "NAME TOO LONG";
    static FILINFO fi;
    if (f_stat(path, &fi) != FR_OK) return "CANNOT OPEN";
    uint8_t hdr[ORIC_DISC_HEADER_LEN];
    UINT got = 0;
    if (f_open(&s_file, path, FA_READ) != FR_OK) return "CANNOT OPEN";
    FRESULT fr = f_read(&s_file, hdr, sizeof hdr, &got);
    f_close(&s_file);
    if (fr != FR_OK) return "CANNOT READ";
    mfm_geom_t geom;
    const char *why = mfm_parse(hdr, got, (uint32_t)fi.fsize, &geom);
    if (why) return why;
    if (g) *g = geom;
    if (protect) *protect = (fi.fattrib & AM_RDO) != 0;
    return NULL;
}

const char *discio_insert(oric_t *m, unsigned drive, const char *path) {
    if (drive >= ORIC_DISC_DRIVES) return "NO SUCH DRIVE";
    settle_drive(m, drive);
    s_drive[drive].path[0] = 0;
    if (m) oric_disc_eject(m, drive);
    if (!path || !path[0]) return NULL;

    mfm_geom_t g;
    bool protect;
    const char *why = discio_probe(path, &g, &protect);
    if (why) return why;

    strcpy(s_drive[drive].path, path);
    s_drive[drive].geom = g;
    s_drive[drive].protect = protect;
    if (m) oric_disc_insert(m, drive, g, s_drive[drive].protect);
    log_core1("  disc         : drive %c: %s, %u side%s of %u tracks%s\n", '0' + drive, path,
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
    for (unsigned d = 0; d < ORIC_DISC_DRIVES; d++) {
        oric_disc_drive_type(m, d, s_type[d]);
        if (s_drive[d].path[0]) oric_disc_insert(m, d, s_drive[d].geom, s_drive[d].protect);
    }
}

void discio_set_type(unsigned drive, mfm_geom_t type) {
    if (drive < ORIC_DISC_DRIVES) s_type[drive] = type;
}

mfm_geom_t discio_type(unsigned drive) {
    return s_type[drive < ORIC_DISC_DRIVES ? drive : 0];
}

void discio_next_name(char out[ORIC_MEDIA_NAME_MAX + 1]) {
    char path[ORIC_PATH_MAX];
    static FILINFO fi;
    out[0] = 0;
    for (unsigned n = 1; n <= 99u; n++) {
        snprintf(path, sizeof path, "%s/DISC%02u.dsk", DISCIO_DIR, n);
        FRESULT fr = f_stat(path, &fi);
        if (fr == FR_OK) continue;
        if (fr == FR_NO_FILE) snprintf(out, ORIC_MEDIA_NAME_MAX + 1, "DISC%02u", n);
        return;
    }
}

/* The whole of a new disc of g into the open s_file, a piece at a time. */
static FRESULT write_blank(mfm_geom_t g) {
    uint32_t len = mfm_image_len(g), last_poll = time_us_32();
    FRESULT fr = FR_OK;
    for (uint32_t at = 0; fr == FR_OK && at < len; at += NEW_PIECE) {
        UINT n = len - at < NEW_PIECE ? (UINT)(len - at) : NEW_PIECE, put = 0;
        mfm_blank(g, at, s_piece, n);
        fr = f_write(&s_file, s_piece, n, &put);
        if (fr == FR_OK && put != n) fr = FR_DENIED;
        if (time_us_32() - last_poll >= POLL_US) {
            last_poll = time_us_32();
            g_c1.key_events += kbd_poll();
            g_c1.polls++;
        }
    }
    return fr;
}

const char *discio_new(oric_t *m, unsigned drive, const char *name) {
    if (drive >= ORIC_DISC_DRIVES) return "NO SUCH DRIVE";
    char path[ORIC_PATH_MAX], tmp[ORIC_PATH_MAX + 4];
    static FILINFO fi;
    (void)f_mkdir("/oric");
    (void)f_mkdir(DISCIO_DIR);
    snprintf(path, sizeof path, "%s/%.*s.dsk", DISCIO_DIR, (int)ORIC_MEDIA_NAME_MAX, name);
    snprintf(tmp, sizeof tmp, "%s.new", path);
    FRESULT fr = f_stat(path, &fi);
    if (fr == FR_OK) return "already there";
    if (fr != FR_NO_FILE) return "card error";

    mfm_geom_t g = s_type[drive];
    uint32_t t0 = time_us_32();
    /* A .new left by a disc cut short is written over. */
    if (f_open(&s_file, tmp, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return "cannot create";
    fr = write_blank(g);
    FRESULT fc = f_close(&s_file);
    if (fr == FR_OK) fr = fc;
    if (fr == FR_OK) fr = f_rename(tmp, path);
    uint32_t us = time_us_32() - t0;
    if (fr != FR_OK) {
        (void)f_unlink(tmp);
        log_core1("  disc         : %s: FatFs %d after %lu us\n", path, (int)fr, (unsigned long)us);
        return "card error";
    }
    log_core1("  disc         : %s made, %u side%s of %u tracks, %lu bytes in %lu us\n", path,
              g.sides, g.sides > 1 ? "s" : "", g.tracks, (unsigned long)mfm_image_len(g),
              (unsigned long)us);
    return discio_insert(m, drive, path);
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
    if (q.put) snprintf(put, sizeof put, "put %c.%u:%u ", '0' + q.put_drive, q.put_side, q.put_cyl);
    if (q.get) snprintf(get, sizeof get, "get %c.%u:%u ", '0' + q.drive, q.side, q.cyl);
    log_core1("  disc         : %s%s%s, %lu us\n", put, get, ok ? "ok" : "FAILED",
              (unsigned long)*us);
    oric_disc_served(m, ok);
    return ok;
}
