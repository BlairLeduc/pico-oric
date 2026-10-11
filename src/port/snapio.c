/* snapio.c — our save states on the card (snapio.h). */

#include "snapio.h"

#include <stdio.h>
#include <string.h>

#include "ff.h"
#include "pico/stdlib.h"

#include "discio.h"
#include "log.h"
#include "microdisc.h"
#include "tapeio.h"

static FIL s_file;
/* The state's media, from the check to the load; and what the menu
 * should say about them. */
static snap_media_t s_media;
static char s_said[40];

/* The file's name, for the status row. */
static const char *base(const char *path) {
    const char *b = strrchr(path, '/');
    return b ? b + 1 : path;
}

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

    /* The drives and the deck as they are, so a load puts them back. */
    memset(&s_media, 0, sizeof s_media);
    for (unsigned d = 0; d < ORIC_DISC_DRIVES; d++)
        snprintf(s_media.disc[d], sizeof s_media.disc[d], "%s", discio_inserted(d));
    tapeio_media(&s_media);

    snap_status_t st = SNAP_IO;
    if (f_open(&s_file, tmp, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {
        st = snapshot_save(m, &s_media, fwrite_cb, &s_file);
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
    snap_status_t st = snapshot_check(m, fread_cb, &s_file, info, &s_media);
    f_close(&s_file);
    return st;
}

/* Every disc the state names will go in its drive, or the load is
 * refused before anything changes, naming the first that will not: a
 * disc program resumed without its disc goes wrong (design.md §10.6). */
static snap_status_t check_discs(void) {
    if (!s_media.present) return SNAP_OK;
    for (unsigned d = 0; d < ORIC_DISC_DRIVES; d++) {
        const char *p = s_media.disc[d];
        if (!p[0]) continue;
        const char *why = discio_probe(p, NULL, NULL);
        if (!why) continue;
        snprintf(s_said, sizeof s_said, " Needs %c: %.28s", 'A' + d, base(p));
        log_core1("  snapshot     : drive %c's %s: %s\n", 'A' + d, p, why);
        return SNAP_NO_DISC;
    }
    return SNAP_OK;
}

/* The state's media back in the drives and the deck, after the load. A
 * tape that has gone does not stop the load: the program it loaded is in
 * memory, and the deck is left empty with a word on the status row. */
static void restore_media(oric_t *m) {
    if (!s_media.present) return;
    for (unsigned d = 0; d < ORIC_DISC_DRIVES; d++) {
        const char *why = discio_insert(m, d, s_media.disc[d]);
        if (why) log_core1("  snapshot     : drive %c: %s\n", 'A' + d, why);
    }
    const char *why = tapeio_restore(&s_media);
    if (why) {
        snprintf(s_said, sizeof s_said, " Tape %.24s missing", base(s_media.tape));
        log_core1("  snapshot     : the deck's %s: %s; the deck is empty\n", s_media.tape, why);
    }
}

const char *snapio_said(void) {
    return s_said;
}

/* The file snapio_check passed, for snapio_load. */
static char s_from[40];

/* A state for a machine the menu can power on as (snapshot_machine):
 * whole, and refused only for its ROM, RAM, Microdisc or pulse. */
static bool other_machine(snap_status_t st) {
    return st == SNAP_OTHER_ROM || st == SNAP_OTHER_RAM || st == SNAP_OTHER_MACHINE;
}

snap_status_t snapio_check(const oric_t *m, unsigned slot, snap_info_t *info, bool *recovered) {
    char main_path[40], tmp[40];
    path(main_path, sizeof main_path, slot, "sav");
    path(tmp, sizeof tmp, slot, "new");
    *recovered = false;
    s_said[0] = 0;
    s_from[0] = 0;
    snap_status_t st = check_file(m, main_path, info);
    const char *from = main_path;
    if (st == SNAP_IO || st == SNAP_CORRUPT || st == SNAP_NOT_SNAPSHOT) {
        /* Missing or damaged: an interrupted publish leaves a whole .new.
         * A state that is whole but for another machine is not damage,
         * and is reported as it is. */
        snap_info_t alt = *info;
        snap_status_t t = check_file(m, tmp, &alt);
        if (t == SNAP_OK || other_machine(t)) {
            *recovered = true;
            *info = alt;
            st = t;
            from = tmp;
        }
    }
    if (st == SNAP_OK || other_machine(st)) {
        snap_status_t d = check_discs();
        if (d != SNAP_OK) return d;
        snprintf(s_from, sizeof s_from, "%s", from);
    }
    return st;
}

snap_status_t snapio_load(oric_t *m, bool *changed) {
    *changed = false;
    if (!s_from[0]) return SNAP_IO;
    if (f_open(&s_file, s_from, FA_READ) != FR_OK) return SNAP_IO;
    snap_status_t st = snapshot_load(m, fread_cb, &s_file);
    f_close(&s_file);
    s_from[0] = 0;
    /* Every other refusal comes before anything changes (snapshot.h). */
    *changed = st == SNAP_TORN;
    if (st == SNAP_OK) restore_media(m);
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
