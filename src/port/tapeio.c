/* tapeio.c — .tap files serving the tape trap (tapeio.h, design.md §10.3).
 *
 * pico-ace's, with the Oric's .tap (tap.h) and its requests (tape.h):
 * a request is a header to find, data to read or a file to write, not a
 * block, and a file on the card is only ever written whole. With fast
 * tape off the trap is the signal's cue (pico-ace's), its image a window
 * of the tape's whole files where pico-ace's was the tape.
 */

#include "tapeio.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "ff.h"
#include "pico/stdlib.h"

#include "cassette.h"
#include "handoff.h"
#include "keymapio.h"
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

/* The signal (design.md §10.4): the image the cassette plays, from
 * s_img_at in s_img_path, to the file's end if s_img_whole; or the
 * recorder's buffer, for s_rec_path, when s_img_path is "". */
static oric_t  *s_m;
static uint8_t  s_img[ORIC_TAPE_IMAGE_MAX];
static char     s_img_path[ORIC_PATH_MAX];
static uint32_t s_img_at, s_img_len;
static bool     s_img_whole;
static char     s_rec_path[ORIC_PATH_MAX];

/* A recording is waiting for the card: core 0 stops asking for a park
 * until the card changes (tapeio_card_changed). */
static volatile bool s_flush_wait;

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

/* ---- the cassette's image ---------------------------------------------------- */

void tapeio_attach(oric_t *m) {
    s_m = m;
}

/* The cassette holds the deck's image. A machine powered on again has an
 * empty cassette whatever was in it. */
static bool img_in(void) {
    return s_m && s_m->cas.loaded && s_m->cas.img == s_img && s_img_path[0];
}

static void cassette_out(void) {
    if (img_in()) oric_cassette_insert(s_m, NULL, 0);
    s_img_path[0] = 0;
}

/* ---- the deck ------------------------------------------------------------- */

static void set_deck(const char *path, bool user) {
    cassette_out();
    snprintf(s_path, sizeof s_path, "%s", path ? path : "");
    s_user = user && s_path[0];
    s_pos = s_skip = s_index = 0;
    s_wrapped = false;
    /* A tape a layout's tapes line names chooses it (design.md §9.4). */
    if (s_path[0]) keymapio_file_loaded(s_path);
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

void tapeio_media(snap_media_t *md) {
    snprintf(md->tape, sizeof md->tape, "%s", s_path);
    md->tape_pos = s_pos;
    md->tape_skip = s_skip;
    md->tape_index = s_index;
    md->tape_wrapped = s_wrapped;
    md->tape_user = s_user;
}

const char *tapeio_restore(const snap_media_t *md) {
    if (!md->tape[0]) {
        set_deck(NULL, false);
        return NULL;
    }
    if (open_read(&s_f, md->tape) != FR_OK) {
        set_deck(NULL, false);
        return "cannot open";
    }
    uint32_t size = (uint32_t)f_size(&s_f);
    f_close(&s_f);
    set_deck(md->tape, md->tape_user);
    /* A file changed since the state: a place past its end is not one,
     * and the tape goes back to its start. */
    if (md->tape_pos <= size && md->tape_skip <= size - md->tape_pos) {
        s_pos = md->tape_pos;
        s_skip = md->tape_skip;
        s_index = md->tape_index;
        s_wrapped = md->tape_wrapped;
    }
    log_core1("  tape         : %s in the deck from a state, at %lu (file %lu)\n", s_path,
              (unsigned long)s_pos, (unsigned long)s_index);
    return NULL;
}

const char *tapeio_inserted(void) { return s_path; }
bool tapeio_chosen(void) { return s_user; }
uint32_t tapeio_position(void) { return s_index; }

void tapeio_rewind(void) {
    s_pos = s_skip = s_index = 0;
    s_wrapped = false;
    /* The image from the start again, or the first window of it. */
    if (img_in() && s_img_at == 0) oric_cassette_rewind(s_m);
    else cassette_out();
}

/* A name as a file in TAPEIO_DIR, with `ext` after it: trailing spaces
 * gone, and anything FAT will not take as '_'. False for an empty name. */
static bool name_path(const uint8_t *name, size_t n, const char *ext, char out[ORIC_PATH_MAX]) {
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
    snprintf(out, ORIC_PATH_MAX, "%s/%s%s", TAPEIO_DIR, s, ext);
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

/* CLOAD's name chooses a file, if the card has one of that name: the
 * name as it is, then NAME.tap. A title in parts asks for the next by
 * its file's name, CLOAD "GAME.TA1" or "GAME2.TAP", as the emulators
 * its tapes were made for served it (design.md §10.3, M12's corpus).
 * With the deck empty, the first tape whose first header has the name. */
static bool exists(const char *path) {
    if (open_read(&s_f, path) != FR_OK) return false;
    f_close(&s_f);
    return true;
}

static void choose_tape(const tape_t *t) {
    char named[ORIC_PATH_MAX], found[ORIC_PATH_MAX];
    size_t n = strlen((const char *)t->want);
    if (!name_path(t->want, n, "", named) || strcasecmp(named, s_path) == 0) return;
    bool have = exists(named);
    if (!have) {
        (void)name_path(t->want, n, ".tap", named);
        if (strcasecmp(named, s_path) == 0) return;
        have = exists(named);
    }
    if (have) {
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
    bool failed = false;
    while (oric_tape_load_wants(m)) {
        UINT want = t->len - t->done < sizeof s_buf ? (UINT)(t->len - t->done)
                                                    : (UINT)sizeof s_buf;
        UINT got = 0;
        if (f_read(&s_f, s_buf, want, &got) != FR_OK) {
            failed = true;
            break;
        }
        if (got == 0) break;
        oric_tape_load_data(m, s_buf, got);
        got_all += got;
    }
    f_close(&s_f);
    /* The file ends one byte short, as many archive tapes do: the byte
     * in memory is kept, and the load ends whole (tape.h). Only at the
     * file's end: a card that failed part-way is not a short tape. */
    bool kept = !failed && oric_tape_load_keep(m);
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
        say(failed ? " Cannot read %.24s" : " Tape ended: %.24s", base(s_path));
        g_tape_stats.errors++;
        give_up(m, failed ? "cannot read the tape" : "the file is cut short");
    }
    log_core1("  tape         : %s %lu of %lu bytes at #%04X from %s%s\n",
              verify ? "verified" : "loaded", (unsigned long)got_all, (unsigned long)len,
              start, base(s_path), failed ? "; a read failed" : !whole ? "; the tape ends there"
                                   : kept ? "; the last byte missing, kept" : "");
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
    const char *path = s_user ? s_path : name_path(t->name, t->name_len, ".tap", named) ? named : "";
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

/* ---- the signal (design.md §10.4) ------------------------------------------ *
 * The trap still stalls the CPU at the find and the header write, and
 * the port finds the tape as it does for the trap, but then declines:
 * the ROM's routine runs, and reads or writes the signal (cassette.h). */

/* The whole files at the front of n bytes of a tape: where the last one
 * that fits ends. */
static uint32_t whole_files(const uint8_t *p, uint32_t n) {
    uint32_t pos = 0;
    for (;;) {
        tap_header_t h;
        size_t skip;
        if (tap_scan(p + pos, n - pos, false, &h, &skip) != TAP_FOUND) return pos;
        uint32_t end = pos + h.data_at + tap_data_len(tap_start(h.raw), tap_end(h.raw));
        if (end > n) return pos;
        pos = end;
    }
}

/* The tape at `path` from `at` into the cassette: to its end, or the
 * whole files that fit. NULL, or why not. */
static const char *image_load(const char *path, uint32_t at) {
    uint32_t from, to;
    if (!s_m) return "no machine";
    if (oric_cassette_unsaved(s_m, &from, &to)) return "a recording is not saved";
    cassette_out();
    if (open_read(&s_f, path) != FR_OK) return "cannot open";
    FSIZE_t size = f_size(&s_f);
    UINT got = 0;
    FRESULT fr = f_lseek(&s_f, at);
    if (fr == FR_OK) fr = f_read(&s_f, s_img, sizeof s_img, &got);
    f_close(&s_f);
    if (fr != FR_OK) return "cannot read";
    bool whole = (FSIZE_t)at + got >= size;
    uint32_t len = whole ? got : whole_files(s_img, got);
    if (!len && !whole) return "a file over 64K";
    oric_cassette_insert(s_m, s_img, len);
    snprintf(s_img_path, sizeof s_img_path, "%s", path);
    s_img_at = at;
    s_img_len = len;
    s_img_whole = whole;
    g_tape_stats.played++;
    log_core1("  tape         : %s from byte %lu, %lu bytes%s, in the cassette\n", path,
              (unsigned long)at, (unsigned long)len, whole ? "" : " of whole files");
    return NULL;
}

/* The cassette at its end with the ROM still looking: the next window,
 * or the start once, as the trap rewinds; false at the end a second
 * time, when the caller gives up. */
static bool play_on(void) {
    if (!s_img_whole) {
        const char *err = image_load(s_path, s_img_at + s_img_len);
        return !err;
    }
    if (s_wrapped) return false;
    s_wrapped = true;
    log_core1("  tape         : end of %s; rewound\n", s_path);
    if (s_img_at == 0) {
        oric_cassette_rewind(s_m);
        return true;
    }
    return !image_load(s_path, 0);
}

static void signal_find(oric_t *m, const tape_t *t) {
    log_core1("  tape         : CLOAD \"%s\"%s, off the signal\n", (const char *)t->want,
              t->slow ? ",S" : "");
    choose_tape(t);
    if (!s_path[0]) {
        if (t->want[0]) say(" No tape has %.16s", (const char *)t->want);
        else say(" No tape in the deck", "");
        give_up(m, "no tape, and no file by that name");
        return;
    }
    if (!img_in() || strcmp(s_img_path, s_path) != 0) {
        const char *err = image_load(s_path, s_pos + s_skip);
        if (err) {
            g_tape_stats.errors++;
            say(" Tape not played: %.18s", err);
            give_up(m, err);
            return;
        }
    } else if (m->cas.ended && !play_on()) {
        say(" End of tape %.24s", base(s_path));
        give_up(m, "end of the tape, twice");
        return;
    } else if (!m->cas.ended) {
        s_wrapped = false;
    }
    g_tape_stats.finds++;
    oric_tape_decline(m);
}

/* The ROM reads on past the cassette's end: as at a find. */
static void signal_ran_out(oric_t *m) {
    if (play_on()) return;
    say(" End of tape %.24s", base(s_path));
    give_up(m, "the tape ended under the ROM's reader");
}

static void signal_record(oric_t *m, const tape_t *t) {
    char named[ORIC_PATH_MAX];
    const char *path = s_user ? s_path : name_path(t->name, t->name_len, ".tap", named) ? named : "";
    log_core1("  tape         : CSAVE \"%.*s\"%s, #%04X-#%04X, by the signal\n", (int)t->name_len,
              (const char *)t->name, t->slow ? ",S" : "", t->start, t->end);
    if (!path[0]) {
        say(" CSAVE: no tape and no name", "");
        decline(m, "nowhere to save");
        return;
    }
    uint32_t from, to;
    if (oric_cassette_unsaved(m, &from, &to)) {
        say(" The last recording is not saved", "");
        decline(m, "the last recording waits for the card");
        return;
    }
    cassette_out();
    snprintf(s_rec_path, sizeof s_rec_path, "%s", path);
    oric_cassette_record(m, s_img, sizeof s_img);
    log_core1("  tape         : recording for %s\n", path);
    oric_tape_decline(m);
}

/* path's files, then n bytes from p, into path.new; then the rename. */
static FRESULT append_bytes(const char *path, const uint8_t *p, uint32_t n) {
    char tmp[ORIC_PATH_MAX + 4];
    FRESULT fr = append_begin(path, tmp);
    if (fr != FR_OK) return fr;
    fr = write_all(&s_g, p, (UINT)n);
    return append_end(path, tmp, fr);
}

/* What the recorder took, appended to its file. */
static void signal_flush(oric_t *m) {
    uint32_t from, to;
    if (!oric_cassette_unsaved(m, &from, &to)) return;
    FRESULT fr = append_bytes(s_rec_path, s_img + from, to - from);
    if (fr == FR_OK) {
        g_tape_stats.saves++;
        g_tape_stats.recorded += m->cas.rec.files;
        g_tape_stats.bytes = to - from;
        say(" Saved to %.22s", base(s_rec_path));
        log_core1("  tape         : %lu recorded bytes appended to %s\n",
                  (unsigned long)(to - from), s_rec_path);
        /* The recording's start in the log, so that a run over the UART
         * can check it off the board (design.md §15.2 M13). */
        for (uint32_t i = from; i < to && i - from < 256u; i += 32u) {
            char hex[32 * 2 + 1];
            uint32_t k = 0;
            for (; k < 32u && i + k < to; k++) snprintf(hex + 2u * k, 3, "%02X", s_img[i + k]);
            log_core1("  tape rec     : %s\n", hex);
        }
        oric_cassette_saved(m);
    } else {
        /* Kept, as for a missing card: this is the only copy. Tried
         * again when the card changes or the tape is next served. */
        g_tape_stats.errors++;
        say(" Not saved: card %s", fr == FR_DENIED ? "full" : "error");
        log_core1("  tape         : recording not saved to %s: FatFs %d; %lu bytes kept\n",
                  s_rec_path, (int)fr, (unsigned long)(to - from));
        s_flush_wait = true;
    }
    oric_cassette_record(m, NULL, 0);
}

const char *tapeio_play(bool on) {
    if (!s_m) return "no machine";
    if (!on) {
        oric_cassette_play(s_m, false);
        return NULL;
    }
    if (g_ui.fast_tape) return "fast tape is on";
    if (!s_path[0]) return "the deck is empty";
    if (!img_in() || strcmp(s_img_path, s_path) != 0) {
        const char *err = image_load(s_path, s_pos + s_skip);
        if (err) return err;
    }
    if (s_m->cas.ended) return "at the end: rewind";
    oric_cassette_play(s_m, true);
    log_core1("  tape         : %s playing by hand\n", s_path);
    return NULL;
}

bool tapeio_playing(void) {
    return s_m && s_m->cas.by_hand;
}

void tapeio_mode(void) {
    if (g_ui.fast_tape) cassette_out();
}

void tapeio_card_changed(void) { s_flush_wait = false; }

/* The ROM is reading a cassette that has ended (tape.h). */
static bool ran_out(const oric_t *m) {
    return m->cfg.tape_signal && img_in() && m->cas.ended && m->cas.motor &&
           !m->cas.by_hand && oric_tape_reading(m);
}

bool tapeio_wanted(const oric_t *m) {
    uint32_t from, to;
    return (oric_cassette_unsaved(m, &from, &to) && !s_flush_wait) || ran_out(m);
}

void tapeio_serve(oric_t *m, uint32_t *us) {
    uint32_t t0 = time_us_32();
    s_m = m;
    const tape_t *t = oric_tape_pending(m);
    uint32_t from, to;
    bool unsaved = oric_cassette_unsaved(m, &from, &to);
    bool out = !t && ran_out(m);
    if (!t && !unsaved && !out) { *us = 0; return; }
    if (storage_mount() != 0) {
        say(" No card: tape not served", "");
        if (unsaved) {
            /* Kept, not dropped: the guest's CSAVE is over, and this is
             * the only copy. Written when the card is back. */
            say(" Recording kept: put the card in", "");
            log_core1("  tape         : no card; %lu recorded bytes kept for %s\n",
                      (unsigned long)(to - from), s_rec_path);
            s_flush_wait = true;
            oric_cassette_record(m, NULL, 0);
        }
        if (t && t->op == TAPE_SAVE) decline(m, "no card");
        else if (t && t->op == TAPE_RECORD) decline(m, "no card");
        else if (t || out) give_up(m, "no card");
    } else {
        s_flush_wait = false;
        if (unsaved) signal_flush(m);
        if (!t) {
            if (out) signal_ran_out(m);
        } else if (t->op == TAPE_FIND) {
            if (m->cfg.tape_signal) signal_find(m, t);
            else serve_find(m, t);
        } else if (t->op == TAPE_LOAD) {
            serve_load(m, t);
        } else if (t->op == TAPE_RECORD) {
            signal_record(m, t);
        } else {
            serve_save(m, t);
        }
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
