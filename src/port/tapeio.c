/* tapeio.c — .tap files serving the tape trap (tapeio.h, design.md §10.3).
 *
 * pico-ace's, with the Oric's .tap (tap.h) and its requests (tape.h):
 * a request is a header to find, data to read or a file to write, not a
 * block, and a file on the card is only ever written whole. The signal
 * (pico-ace's cassette) is M13's.
 */

#include "tapeio.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "ff.h"
#include "pico/stdlib.h"

#include "log.h"
#include "storage.h"
#include "tap.h"

volatile tapeio_stats_t g_tape_stats;

/* The deck. `user` is a tape put in by the menu or boot_tape, which a
 * save appends to; one CLOAD found by name is not. */
static char     s_path[ORIC_PATH_MAX];
static bool     s_user;
static uint32_t s_pos;        /* where the next header is looked for     */
static uint32_t s_skip;       /* the last found file's data, not loaded  */
static uint32_t s_index;      /* files found since the start             */
static bool     s_wrapped;    /* rewound by a load since the last data   */

static char     s_said[40];
static FIL      s_f, s_g;
static uint8_t  s_buf[ORIC_CARD_CHUNK];

static void say(const char *fmt, const char *arg) {
    snprintf(s_said, sizeof s_said, fmt, arg);
}

const char *tapeio_said(void) {
    static char out[sizeof s_said];
    memcpy(out, s_said, sizeof out);
    s_said[0] = 0;
    return out;
}

static const char *base(const char *path) {
    const char *b = strrchr(path, '/');
    return b ? b + 1 : path;
}

/* ---- the deck ------------------------------------------------------------- */

static void set_deck(const char *path, bool user) {
    snprintf(s_path, sizeof s_path, "%s", path ? path : "");
    s_user = user && s_path[0];
    s_pos = s_skip = s_index = 0;
    s_wrapped = false;
}

/* The file, or the .new a save left without its rename. */
static FRESULT open_read(FIL *f, const char *path) {
    FRESULT fr = f_open(f, path, FA_READ);
    if (fr == FR_NO_FILE) {
        char tmp[ORIC_PATH_MAX + 4];
        snprintf(tmp, sizeof tmp, "%s.new", path);
        fr = f_open(f, tmp, FA_READ);
    }
    return fr;
}

const char *tapeio_insert(const char *path) {
    if (!path || !path[0]) {
        set_deck(NULL, false);
        return NULL;
    }
    if (open_read(&s_f, path) != FR_OK) {
        set_deck(NULL, false);
        return "cannot open";
    }
    f_close(&s_f);
    set_deck(path, true);
    log_core1("  tape         : %s in the deck\n", path);
    return NULL;
}

/* A name a tape holds: the file, or the .new an interrupted save left,
 * which a load takes as the tape (open_read). FR_NO_FILE when neither. */
static FRESULT taken(const char *path) {
    FILINFO fi;
    char tmp[ORIC_PATH_MAX + 4];
    FRESULT fr = f_stat(path, &fi);
    if (fr != FR_NO_FILE) return fr;
    snprintf(tmp, sizeof tmp, "%s.new", path);
    return f_stat(tmp, &fi);
}

/* pico-atom's New tape (§12): TAPE01.tap, or the next number free, made
 * empty and put in the deck, where CSAVE appends to it. FatFs makes no
 * missing parent, so /oric first, as a save does. */
const char *tapeio_new(void) {
    char path[ORIC_PATH_MAX];
    (void)f_mkdir("/oric");
    (void)f_mkdir(TAPEIO_DIR);
    for (unsigned n = 1; n <= 99u; n++) {
        snprintf(path, sizeof path, "%s/TAPE%02u.tap", TAPEIO_DIR, n);
        FRESULT fr = taken(path);
        if (fr == FR_OK) continue;
        if (fr != FR_NO_FILE) return "card error";
        if (f_open(&s_f, path, FA_CREATE_NEW | FA_WRITE) != FR_OK) return "cannot create";
        f_close(&s_f);
        log_core1("  tape         : %s made\n", path);
        return tapeio_insert(path);
    }
    return "TAPE99 is the last";
}

const char *tapeio_inserted(void) { return s_path; }
bool tapeio_chosen(void) { return s_user; }
uint32_t tapeio_position(void) { return s_index; }

void tapeio_rewind(void) {
    s_pos = s_skip = s_index = 0;
    s_wrapped = false;
}

/* A name as a file in TAPEIO_DIR: trailing spaces gone, and anything FAT
 * will not take as '_'. False for an empty name. */
static bool name_path(const uint8_t *name, size_t n, char out[ORIC_PATH_MAX]) {
    char s[ORIC_TAP_NAME_MAX + 1];
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        char c = (char)(name[i] & 0x7Fu);
        if (c < 0x20 || c == 0x7F || strchr("\"*/:<>?\\|", c)) c = '_';
        s[i] = c;
        if (c != ' ') k = i + 1u;
    }
    s[k] = 0;
    if (!k) return false;
    snprintf(out, ORIC_PATH_MAX, "%s/%s.tap", TAPEIO_DIR, s);
    return true;
}

/* ---- finding a header --------------------------------------------------------- */

/* The next header in the open file from `pos`, read a window at a time:
 * *h, with h->data_at made the file offset of its data. */
static tap_scan_t scan_file(FIL *f, uint32_t pos, tap_header_t *h) {
    FSIZE_t size = f_size(f);
    for (;;) {
        if (pos >= size) return TAP_NONE;
        UINT got = 0;
        if (f_lseek(f, pos) != FR_OK || f_read(f, s_buf, sizeof s_buf, &got) != FR_OK)
            return TAP_NONE;
        size_t skip = 0;
        tap_scan_t r = tap_scan(s_buf, got, pos + got >= size, h, &skip);
        if (r == TAP_FOUND) {
            h->data_at += pos;
            return r;
        }
        if (r == TAP_NONE) return r;
        /* A window of #16s alone keeps its last. */
        pos += skip ? (uint32_t)skip : (uint32_t)got - 1u;
    }
}

/* What CLOAD asked for is what the header carries, as the ROM compares
 * them (#E790 in 1.1, #E6F0 in 1.0): the name it stored, to its zero.
 * 1.1 stores 16 bytes of a longer one, which a 16-byte name matches; 1.0
 * stores it all, and the byte after the 16th is not the zero. */
static bool name_is(const tap_header_t *h, const tape_t *t) {
    size_t n = strlen((const char *)t->want);
    size_t k = h->name_len < t->rom->name_cap ? h->name_len : t->rom->name_cap;
    return n == k && memcmp(h->name, t->want, n) == 0;
}

/* A file in TAPEIO_DIR as a path, false if it does not fit: FatFs's long
 * names run to 255, past ORIC_PATH_MAX, and a cut path opens nothing. */
static bool dir_path(const char *fname, char out[ORIC_PATH_MAX]) {
    int n = snprintf(out, ORIC_PATH_MAX, "%s/%s", TAPEIO_DIR, fname);
    return n > 0 && n < (int)ORIC_PATH_MAX;
}

/* With the deck empty and no file of that name: the first tape in
 * TAPEIO_DIR whose first header carries the name. Archive files are
 * seldom named after the program they hold. */
static bool find_by_header(const tape_t *t, char out[ORIC_PATH_MAX]) {
    DIR d;
    FILINFO fi;
    bool found = false;
    if (f_opendir(&d, TAPEIO_DIR) != FR_OK) return false;
    while (!found && f_readdir(&d, &fi) == FR_OK && fi.fname[0]) {
        size_t len = strlen(fi.fname);
        if ((fi.fattrib & (AM_DIR | AM_HID)) || strncmp(fi.fname, "._", 2) == 0 ||
            len < 5 || strcasecmp(fi.fname + len - 4, ".tap") != 0) continue;
        if (!dir_path(fi.fname, out) || f_open(&s_f, out, FA_READ) != FR_OK) continue;
        tap_header_t h;
        found = scan_file(&s_f, 0, &h) == TAP_FOUND && name_is(&h, t);
        f_close(&s_f);
    }
    f_closedir(&d);
    return found;
}

static void decline(oric_t *m, const char *why) {
    oric_tape_decline(m);
    g_tape_stats.declined++;
    log_core1("  tape         : declined: %s\n", why);
}

/* A CLOAD the deck cannot answer: back to Ready, program kept, rather
 * than the real machine's wait for a signal (tape.h, the owner's choice
 * 2026-10-09). */
static void give_up(oric_t *m, const char *why) {
    oric_tape_give_up(m);
    g_tape_stats.declined++;
    log_core1("  tape         : given up: %s; the reset button\n", why);
}

/* CLOAD's name chooses a file, if the card has one of that name; with
 * the deck empty, the first tape whose first header has the name. */
static void choose_tape(const tape_t *t) {
    char named[ORIC_PATH_MAX], found[ORIC_PATH_MAX];
    size_t n = strlen((const char *)t->want);
    if (!name_path(t->want, n, named) || strcasecmp(named, s_path) == 0) return;
    FRESULT fr = open_read(&s_f, named);
    if (fr == FR_OK) {
        f_close(&s_f);
        set_deck(named, false);
        log_core1("  tape         : %s found by name\n", named);
    } else if (!s_path[0] && find_by_header(t, found)) {
        set_deck(found, false);
        log_core1("  tape         : %s found by its header\n", found);
    }
}

static void serve_find(oric_t *m, const tape_t *t) {
    log_core1("  tape         : CLOAD \"%s\"%s\n", (const char *)t->want, t->slow ? ",S" : "");
    choose_tape(t);
    if (!s_path[0]) {
        if (t->want[0]) say(" No tape has %.16s", (const char *)t->want);
        else say(" No tape in the deck", "");
        give_up(m, "no tape, and no file by that name");
        return;
    }
    if (open_read(&s_f, s_path) != FR_OK) {
        g_tape_stats.errors++;
        say(" Cannot open %.24s", base(s_path));
        give_up(m, "cannot open the tape");
        return;
    }
    tap_header_t h;
    tap_scan_t r = scan_file(&s_f, s_pos + s_skip, &h);
    if (r != TAP_FOUND && !s_wrapped) {
        log_core1("  tape         : end of %s; rewound\n", s_path);
        s_pos = s_skip = s_index = 0;
        s_wrapped = true;
        r = scan_file(&s_f, 0, &h);
    }
    f_close(&s_f);
    if (r != TAP_FOUND) {
        if (t->want[0])
            snprintf(s_said, sizeof s_said, " %.16s is not on %.11s", (const char *)t->want,
                     base(s_path));
        else
            say(" End of tape %.24s", base(s_path));
        give_up(m, "end of the tape, twice");
        return;
    }
    uint16_t start = tap_start(h.raw), end = tap_end(h.raw);
    s_pos = h.data_at;
    s_skip = tap_data_len(start, end);
    s_index++;
    oric_tape_found(m, &h);
    g_tape_stats.finds++;
    log_core1("  tape         : file %lu of %s: \"%.*s\", %s, #%04X-#%04X%s\n",
              (unsigned long)s_index, base(s_path), (int)h.name_len, (const char *)h.name,
              h.raw[TAP_H_TYPE] & 0x80u ? "code" : "BASIC", start, end,
              h.raw[TAP_H_AUTORUN] ? ", autorun" : "");
}

static void serve_load(oric_t *m, const tape_t *t) {
    if (!s_path[0] || open_read(&s_f, s_path) != FR_OK || f_lseek(&s_f, s_pos) != FR_OK) {
        if (s_path[0]) f_close(&s_f);
        g_tape_stats.errors++;
        say(" Cannot read %.24s", base(s_path));
        give_up(m, "cannot read the tape");
        return;
    }
    uint32_t got_all = 0;
    while (oric_tape_load_wants(m)) {
        UINT want = t->len - t->done < sizeof s_buf ? (UINT)(t->len - t->done)
                                                    : (UINT)sizeof s_buf;
        UINT got = 0;
        if (f_read(&s_f, s_buf, want, &got) != FR_OK || got == 0) break;
        oric_tape_load_data(m, s_buf, got);
        got_all += got;
    }
    f_close(&s_f);
    bool whole = t->done == t->len;
    uint32_t len = t->len;
    bool verify = t->verify;
    uint16_t start = t->start;
    oric_tape_load_end(m);

    s_pos += got_all;
    s_skip = 0;
    s_wrapped = false;
    g_tape_stats.loads++;
    g_tape_stats.bytes = got_all;
    if (!whole) {
        say(" Tape ended: %.24s", base(s_path));
        g_tape_stats.errors++;
        give_up(m, "the file is cut short");
    }
    log_core1("  tape         : %s %lu of %lu bytes at #%04X from %s%s\n",
              verify ? "verified" : "loaded", (unsigned long)got_all, (unsigned long)len,
              start, base(s_path), whole ? "" : "; the tape ends there");
}

/* ---- save ------------------------------------------------------------------- */

static FRESULT write_all(FIL *f, const void *p, UINT n) {
    UINT put = 0;
    FRESULT fr = f_write(f, p, n, &put);
    return fr == FR_OK && put != n ? FR_DENIED : fr;
}

/* path's files into path.new, open in s_g for what follows. */
static FRESULT append_begin(const char *path, char tmp[ORIC_PATH_MAX + 4]) {
    snprintf(tmp, ORIC_PATH_MAX + 4, "%s.new", path);
    (void)f_mkdir("/oric");
    (void)f_mkdir(TAPEIO_DIR);

    /* A save cut off between the unlink and the rename left only the
     * .new, which open_read plays; make it the tape before it is
     * overwritten. Beside the tape, a .new may be partial: the tape wins. */
    FILINFO fi;
    if (f_stat(path, &fi) == FR_NO_FILE && f_stat(tmp, &fi) == FR_OK) {
        FRESULT fr = f_rename(tmp, path);
        if (fr != FR_OK) return fr;
    }

    FRESULT fr = f_open(&s_g, tmp, FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK) return fr;
    bool had = open_read(&s_f, path) == FR_OK;
    UINT got = 0;
    while (fr == FR_OK && had) {
        fr = f_read(&s_f, s_buf, sizeof s_buf, &got);
        if (fr != FR_OK || got == 0) break;
        fr = write_all(&s_g, s_buf, got);
    }
    if (had) f_close(&s_f);
    if (fr != FR_OK) f_close(&s_g);
    return fr;
}

/* Close path.new, and rename it over path if all went well. */
static FRESULT append_end(const char *path, const char *tmp, FRESULT fr) {
    FRESULT fc = f_close(&s_g);
    if (fr == FR_OK) fr = fc;
    if (fr != FR_OK) {
        (void)f_unlink(tmp);
        return fr;
    }
    (void)f_unlink(path);
    return f_rename(tmp, path);
}

/* path's files, then this one, its header and its data from guest
 * memory, into path.new; then the rename. */
static FRESULT append(const oric_t *m, const tape_t *t, const char *path) {
    char tmp[ORIC_PATH_MAX + 4];
    FRESULT fr = append_begin(path, tmp);
    if (fr != FR_OK) return fr;
    uint8_t hdr[TAP_HEADER_MAX];
    fr = write_all(&s_g, hdr, (UINT)tap_encode_header(t->raw, t->name, t->name_len, hdr));
    for (uint32_t done = 0; fr == FR_OK && done < t->len;) {
        UINT k = 0;
        while (k < sizeof s_buf && done < t->len)
            s_buf[k++] = oric_peek(m, (uint16_t)(t->start + done++));
        fr = write_all(&s_g, s_buf, k);
    }
    return append_end(path, tmp, fr);
}

static void serve_save(oric_t *m, const tape_t *t) {
    char named[ORIC_PATH_MAX];
    const char *path = s_user ? s_path : name_path(t->name, t->name_len, named) ? named : "";
    log_core1("  tape         : CSAVE \"%.*s\"%s, #%04X-#%04X\n", (int)t->name_len,
              (const char *)t->name, t->slow ? ",S" : "", t->start, t->end);
    if (!path[0]) {
        say(" CSAVE: no tape and no name", "");
        decline(m, "nowhere to save");
        return;
    }
    FRESULT fr = append(m, t, path);
    if (fr != FR_OK) {
        g_tape_stats.errors++;
        say(" Not saved: card %s", fr == FR_DENIED ? "full" : "error");
        log_core1("  tape         : not saved to %s: FatFs %d\n", path, (int)fr);
        decline(m, "the card refused the save");
        return;
    }
    g_tape_stats.saves++;
    g_tape_stats.bytes = t->len;
    say(" Saved to %.22s", base(path));
    log_core1("  tape         : %lu bytes appended to %s\n", (unsigned long)t->len, path);
    oric_tape_save_end(m);
}

void tapeio_serve(oric_t *m, uint32_t *us) {
    uint32_t t0 = time_us_32();
    const tape_t *t = oric_tape_pending(m);
    if (!t) { *us = 0; return; }
    if (storage_mount() != 0) {
        say(" No card: tape not served", "");
        if (t->op == TAPE_SAVE) decline(m, "no card");
        else give_up(m, "no card");
    } else {
        if (t->op == TAPE_FIND) serve_find(m, t);
        else if (t->op == TAPE_LOAD) serve_load(m, t);
        else serve_save(m, t);
        storage_unmount();
    }
    *us = time_us_32() - t0;
    g_tape_stats.last_us = *us;
    if (*us > g_tape_stats.max_us) g_tape_stats.max_us = *us;
}

/* ---- the menu's list -------------------------------------------------------- */

unsigned tapeio_list(tapeio_entry_t *out, unsigned max) {
    DIR d;
    FILINFO fi;
    unsigned n = 0;
    if (f_opendir(&d, TAPEIO_DIR) != FR_OK) return 0;
    while (n < max && f_readdir(&d, &fi) == FR_OK && fi.fname[0]) {
        /* Directories, and the "._" files macOS writes beside each file
         * it copies (its AppleDouble metadata, not a tape). */
        if ((fi.fattrib & (AM_DIR | AM_HID)) || strncmp(fi.fname, "._", 2) == 0) continue;
        size_t len = strlen(fi.fname);
        if (len < 5 || strcasecmp(fi.fname + len - 4, ".tap") != 0) continue;
        tapeio_entry_t *e = &out[n];
        if (!dir_path(fi.fname, e->path)) {
            log_core1("  tape         : %s: name too long to open; not listed\n", fi.fname);
            continue;
        }
        e->size = (uint32_t)fi.fsize;
        e->name[0] = 0;
        e->code = false;
        if (f_open(&s_f, e->path, FA_READ) == FR_OK) {
            tap_header_t h;
            if (scan_file(&s_f, 0, &h) == TAP_FOUND) {
                e->code = (h.raw[TAP_H_TYPE] & 0x80u) != 0;
                unsigned k = h.name_len < 16u ? h.name_len : 16u;
                for (unsigned i = 0; i < k; i++) {
                    char c = (char)(h.name[i] & 0x7Fu);
                    e->name[i] = (c >= 0x20 && c < 0x7F) ? c : '?';
                }
                e->name[k] = 0;
            }
            f_close(&s_f);
        }
        n++;
    }
    f_closedir(&d);
    return n;
}
