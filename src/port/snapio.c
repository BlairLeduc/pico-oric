/* snapio.c — our save states on the card (snapio.h). */

#include "snapio.h"

#include <stdio.h>
#include <string.h>

#include "ff.h"
#include "pico/stdlib.h"

static FIL s_file;

static void path(char *out, size_t cap, unsigned slot, const char *ext) {
    snprintf(out, cap, SNAPIO_STATE_DIR "/slot%u.%s", slot + 1u, ext);
}

static bool fwrite_cb(void *ctx, const uint8_t *src, size_t n) {
    UINT w = 0;
    return f_write((FIL *)ctx, src, (UINT)n, &w) == FR_OK && w == n;
}

static bool fread_cb(void *ctx, uint8_t *dst, size_t n) {
    UINT r = 0;
    return f_read((FIL *)ctx, dst, (UINT)n, &r) == FR_OK && r == n;
}

snap_status_t snapio_save(const oric_t *m, unsigned slot, uint32_t *us) {
    /* Refused before the card is touched (snapshot_save's own check). */
    if (oric_tape_pending(m)) { *us = 0; return SNAP_BUSY; }
    uint32_t t0 = time_us_32();
    char tmp[40], dst[40];
    path(tmp, sizeof tmp, slot, "new");
    path(dst, sizeof dst, slot, "sav");
    (void)f_mkdir("/oric");
    (void)f_mkdir(SNAPIO_STATE_DIR);

    /* A save cut off between the unlink and the rename left only the .new,
     * which snapio_load takes as the slot: make it the slot before it is
     * overwritten, as tapeio does. */
    static FILINFO fi;   /* 270 bytes with long names: not on the stack */
    if (f_stat(dst, &fi) == FR_NO_FILE && f_stat(tmp, &fi) == FR_OK) (void)f_rename(tmp, dst);

    snap_status_t st = SNAP_IO;
    if (f_open(&s_file, tmp, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {
        st = snapshot_save(m, fwrite_cb, &s_file);
        FRESULT fc = f_close(&s_file);
        if (st == SNAP_OK && fc != FR_OK) st = SNAP_IO;
        if (st != SNAP_OK) {
            (void)f_unlink(tmp);
        } else {
            /* The publish. Between the unlink and the rename only
             * slotN.new exists, and snapio_load takes it. */
            (void)f_unlink(dst);
            st = f_rename(tmp, dst) == FR_OK ? SNAP_OK : SNAP_IO;
        }
    }
    *us = time_us_32() - t0;
    return st;
}

/* Two passes over one file: check, then load (snapshot.h). */
static snap_status_t check_file(const oric_t *m, const char *p, snap_info_t *info) {
    if (f_open(&s_file, p, FA_READ) != FR_OK) return SNAP_IO;
    snap_status_t st = snapshot_check(m, fread_cb, &s_file, info);
    f_close(&s_file);
    return st;
}

static snap_status_t load_file(oric_t *m, const char *p) {
    if (f_open(&s_file, p, FA_READ) != FR_OK) return SNAP_IO;
    snap_status_t st = snapshot_load(m, fread_cb, &s_file);
    f_close(&s_file);
    return st;
}

snap_status_t snapio_load(oric_t *m, unsigned slot, snap_info_t *info, bool *recovered,
                          bool *changed, uint32_t *us) {
    uint32_t t0 = time_us_32();
    char main_path[40], tmp[40];
    path(main_path, sizeof main_path, slot, "sav");
    path(tmp, sizeof tmp, slot, "new");
    *recovered = false;
    *changed = false;

    snap_status_t st = check_file(m, main_path, info);
    if (st == SNAP_OK) {
        st = load_file(m, main_path);
        *changed = st != SNAP_OK;
    } else if (st == SNAP_IO || st == SNAP_CORRUPT || st == SNAP_NOT_SNAPSHOT) {
        /* Missing or damaged: an interrupted publish leaves a whole .new.
         * A state that is whole but for another machine is not damage,
         * and is reported as it is. */
        if (check_file(m, tmp, info) == SNAP_OK) {
            *recovered = true;
            st = load_file(m, tmp);
            *changed = st != SNAP_OK;
        }
    }
    *us = time_us_32() - t0;
    return st;
}

bool snapio_exists(unsigned slot) {
    char p[40];
    static FILINFO fi;
    path(p, sizeof p, slot, "sav");
    if (f_stat(p, &fi) == FR_OK) return true;
    path(p, sizeof p, slot, "new");
    return f_stat(p, &fi) == FR_OK;
}

bool snapio_delete(unsigned slot) {
    char p[40];
    path(p, sizeof p, slot, "sav");
    FRESULT a = f_unlink(p);
    path(p, sizeof p, slot, "new");
    FRESULT b = f_unlink(p);
    return a == FR_OK || b == FR_OK;
}
