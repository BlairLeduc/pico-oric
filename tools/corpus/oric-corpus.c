/* oric-corpus.c — M12's corpus run: archive tapes loaded and run on the
 * host, one line each (design.md §15, M12; §5.1).
 *
 *   oric-corpus [-r 10|11] [-m 16|48] [-f FIELDS] [-s DIR] [-t] [-v] [-k] [-d DSK] TAPE...
 *
 * Each tape gets a machine of its own, copied from one booted to Ready.
 * A TAPE is a .tap, or a directory of a title's parts, whose deck starts
 * with its first (the shortest .tap name, else the first name). The
 * harness types CLOAD"" and serves the tape as the firmware's tapeio
 * does (design.md §10.3): CLOAD "NAME" puts the file NAME, or NAME.tap,
 * from the tape's own directory in the deck, if there is one; a file one
 * byte short at the tape's end keeps the byte in memory
 * (oric_tape_load_keep); at the end of the tape it rewinds once, and a
 * find that goes round it twice, or a file cut short by more, gives up,
 * back to Ready. When the tape has gone quiet and the ROM is at Ready, a last file
 * without autorun is started, RUN for BASIC and CALL for code; half way
 * through, Space is pressed, for the titles that wait for a key.
 *
 * With -t, fast tape is off (M13, design.md §10.4): the find is served as
 * tapeio's signal_find serves it, the tape put in the cassette whole and
 * the find declined, so that the ROM, or a title's own loader, reads the
 * signal; a ROM left reading an ended tape rewinds it once, then gives
 * up. Files are the headers the cassette has played.
 *
 * A field that traps an undocumented opcode is run again from its start
 * an instruction at a time, so every one is counted by opcode, with the
 * first PC each was seen at. The CB1 interrupt enabled while the PC is
 * outside the ROM is noted: the vertical-sync modification's titles, for
 * M16 (design.md §15). -v fits the modification (vsync.h); with it the
 * tape is still the trap's, which does not read CB1. -d fits the
 * Microdisc with DSK in drive A, a DOS, booted for 15 s and left by X
 * (Sedoric's menu) before the tape, the disc served every field as
 * discio serves the card's and written in memory only (M14).
 *
 * With -k, every field is run again an instruction at a time, and each
 * read of ORB made from RAM notes the key it tests: the row on PB0-PB2
 * and the columns AY port A enables (design.md §2.3). A read with one
 * column enabled tests one key; with more, any of them. These are the
 * keys a title reads itself, past the ROM's scan, for M15's game
 * layouts (design.md §9.4). A title that reads its keys through the ROM
 * notes none. To get past a title's menu, 1 is pressed a third of the
 * way through, Space half way (as without -k) and RETURN at two thirds.
 *
 * One line per tape on stdout, tab-separated:
 *   name  machine  outcome  files  bytes  undoc  opcodes  cb1  mode  note
 *   by-name  one-byte  [keys]
 * keys, with -k: the keys tested alone, then "any:" and the rows read
 * with several columns enabled, as row/columns in hex; "-" for none.
 * With -s, the last frame as DIR/<name>.ppm and the text screen as
 * DIR/<name>.txt.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>

#include "disc_util.h"
#include "guest.h"
#include "m6502.h"
#include "tap.h"
#include "tape.h"
#include "ula.h"

#define DECK_MAX (1u << 20)

typedef struct {
    uint8_t *buf;
    size_t   len, pos, skip;
    bool     wrapped;          /* rewound by a find since the last data */
    char     dir[1024];        /* where CLOAD "NAME" looks              */
    char     name[256];        /* the file in the deck                  */
} deck_t;

typedef struct {
    unsigned finds, files, gave_up;
    unsigned by_name;         /* decks changed by CLOAD's name           */
    unsigned one_byte;        /* files one byte short, the byte kept     */
    uint32_t bytes;
    bool     cut_short;
    char     why[64];
    /* The last file found, for starting it. */
    bool     last_code, last_autorun;
    uint16_t last_start;
    long     last_field;      /* the last field the tape was served at */
} tape_log_t;

/* The instructions before the run's first undocumented opcode: how it
 * was reached, to tell a program's own use from a crash. */
#define LEAD_IN 24u

typedef struct {
    uint16_t pc;
    uint8_t  op[3];
    uint8_t  a, x, y, s, p;
} step_t;

typedef struct {
    uint32_t count[256];
    uint16_t first_pc[256];
    uint32_t total;
    bool     cb1;
    uint16_t cb1_pc;
    long     first_field;     /* the field of the first, -1 for none */
    step_t   lead_in[LEAD_IN];
    unsigned lead_len;
    /* -k: by row, the columns read alone and those read together. */
    uint8_t  key_one[8], key_any[8];
} run_log_t;

static guest_t    s_booted;
static host_disc_t s_disc;
static host_disc_t *s_drives[ORIC_DISC_DRIVES];
static guest_t    g;
static oric_t     s_field_start, s_replay;
static deck_t     s_deck;
static tape_log_t s_tl;
static run_log_t  s_rl;
static long       s_field;
static bool       s_keys;

/* ---- the deck, served as src/port/tapeio.c serves the card's ------------ */

static bool deck_load(const char *dir, const char *name) {
    char path[2048];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    s_deck.len = fread(s_deck.buf, 1, DECK_MAX, f);
    fclose(f);
    snprintf(s_deck.dir, sizeof s_deck.dir, "%s", dir);
    snprintf(s_deck.name, sizeof s_deck.name, "%s", name);
    s_deck.pos = s_deck.skip = 0;
    s_deck.wrapped = false;
    return true;
}

/* The file in the deck's directory called `want`, matched as FAT matches,
 * without case; *out its name. */
static bool find_file(const char *want, char *out, size_t cap) {
    DIR *d = opendir(s_deck.dir);
    if (!d) return false;
    struct dirent *e;
    bool found = false;
    while (!found && (e = readdir(d)) != NULL) {
        if (strcasecmp(e->d_name, want) == 0) {
            snprintf(out, cap, "%s", e->d_name);
            found = true;
        }
    }
    closedir(d);
    return found;
}

/* tapeio's choose_tape: CLOAD's name, its trailing spaces gone, as it
 * is, then as NAME.tap. */
static void choose_tape(const tape_t *t) {
    char want[ORIC_TAP_NAME_MAX + 8], name[256];
    size_t k = 0;
    for (size_t i = 0; t->want[i]; i++) {
        char c = (char)(t->want[i] & 0x7Fu);
        if (c < 0x20 || c == 0x7F || strchr("\"*/:<>?\\|", c)) c = '_';
        want[i] = c;
        if (c != ' ') k = i + 1u;
    }
    want[k] = 0;
    if (!k) return;
    bool got = find_file(want, name, sizeof name);
    if (!got) {
        strcat(want, ".tap");
        got = find_file(want, name, sizeof name);
    }
    if (got && strcasecmp(name, s_deck.name) != 0) {
        char dir[1024];
        snprintf(dir, sizeof dir, "%s", s_deck.dir);
        deck_load(dir, name);
        s_tl.by_name++;
    }
}

static void give_up(oric_t *m, const char *why) {
    oric_tape_give_up(m);
    s_tl.gave_up++;
    if (!s_tl.why[0]) snprintf(s_tl.why, sizeof s_tl.why, "%s", why);
}

/* ---- the signal, served as tapeio's signal_find serves it (-t) ------------ */

static bool s_signal;
static char s_cas_name[256];

static void signal_end(oric_t *m) {
    if (!s_deck.wrapped) {
        s_deck.wrapped = true;
        oric_cassette_rewind(m);
        return;
    }
    give_up(m, "end of the tape, twice");
}

static void signal_serve(oric_t *m) {
    const tape_t *t = oric_tape_pending(m);
    if (t && t->op == TAPE_FIND) {
        s_tl.last_field = s_field;
        choose_tape(t);
        if (!m->cas.loaded || strcmp(s_cas_name, s_deck.name) != 0) {
            oric_cassette_insert(m, s_deck.buf, (uint32_t)s_deck.len);
            snprintf(s_cas_name, sizeof s_cas_name, "%s", s_deck.name);
        } else if (m->cas.ended) {
            signal_end(m);
            if (!oric_tape_pending(m)) return;   /* gave up */
        } else {
            s_deck.wrapped = false;
        }
        s_tl.finds++;
        oric_tape_decline(m);
    } else if (t) {
        oric_tape_decline(m);
    } else if (m->cas.ended && m->cas.motor && oric_tape_reading(m)) {
        signal_end(m);
    }
    /* The files the cassette has begun, and the last one's header. */
    if (m->cas.playing) s_tl.last_field = s_field;
    if (m->cas.files != s_tl.files && m->cas.head_at < m->cas.len) {
        tap_header_t h;
        size_t skip;
        if (tap_scan(m->cas.img + m->cas.head_at, m->cas.len - m->cas.head_at, true, &h,
                     &skip) == TAP_FOUND) {
            s_tl.last_code = (h.raw[TAP_H_TYPE] & 0x80u) != 0;
            s_tl.last_autorun = h.raw[TAP_H_AUTORUN] != 0;
            s_tl.last_start = tap_start(h.raw);
        }
        s_tl.files = m->cas.files;
    }
}

static void serve(oric_t *m) {
    if (s_signal) {
        signal_serve(m);
        return;
    }
    const tape_t *t = oric_tape_pending(m);
    if (!t) return;
    s_tl.last_field = s_field;
    deck_t *d = &s_deck;
    if (t->op == TAPE_FIND) {
        choose_tape(t);
        tap_header_t h;
        size_t skip;
        size_t at = d->pos + d->skip;
        tap_scan_t r = at < d->len ? tap_scan(d->buf + at, d->len - at, true, &h, &skip)
                                   : TAP_NONE;
        if (r != TAP_FOUND && !d->wrapped) {
            d->pos = d->skip = 0;
            d->wrapped = true;
            at = 0;
            r = tap_scan(d->buf, d->len, true, &h, &skip);
        }
        if (r != TAP_FOUND) {
            char why[64];
            if (t->want[0]) snprintf(why, sizeof why, "\"%.40s\" not on the tape", (const char *)t->want);
            else snprintf(why, sizeof why, "end of the tape, twice");
            give_up(m, why);
            return;
        }
        d->pos = at + h.data_at;
        d->skip = tap_data_len(tap_start(h.raw), tap_end(h.raw));
        s_tl.finds++;
        s_tl.last_code = (h.raw[TAP_H_TYPE] & 0x80u) != 0;
        s_tl.last_autorun = h.raw[TAP_H_AUTORUN] != 0;
        s_tl.last_start = tap_start(h.raw);
        oric_tape_found(m, &h);
    } else if (t->op == TAPE_LOAD) {
        size_t n = d->pos < d->len ? d->len - d->pos : 0;
        if (n > t->len) n = t->len;
        oric_tape_load_data(m, d->buf + d->pos, n);
        if (oric_tape_load_keep(m)) s_tl.one_byte++;
        bool whole = t->done == t->len;
        oric_tape_load_end(m);
        d->pos += n;
        d->skip = 0;
        d->wrapped = false;
        s_tl.files++;
        s_tl.bytes += (uint32_t)n;
        if (!whole) {
            s_tl.cut_short = true;
            give_up(m, "a file cut short");
        }
    } else {
        /* A title that saves (a high score, a game's position): the file
         * is taken and dropped. */
        oric_tape_save_end(m);
    }
}

/* ---- running ------------------------------------------------------------ */

static void count_undoc(void) {
    /* The field trapped at least one: run it again from its start, an
     * instruction at a time, and count each (the field's run is the
     * same instructions, so the copy ends where the machine did). */
    oric_copy(&s_replay, &s_field_start);
    uint32_t seen = s_replay.cpu.undoc_count;
    uint64_t end = g.m.cpu.cycles;
    step_t ring[LEAD_IN];
    unsigned n = 0;
    while (s_replay.cpu.cycles < end) {
        const m6502_t *c = &s_replay.cpu;
        step_t *st = &ring[n++ % LEAD_IN];
        st->pc = c->pc;
        for (unsigned i = 0; i < 3; i++) st->op[i] = oric_peek(&s_replay, (uint16_t)(c->pc + i));
        st->a = c->a; st->x = c->x; st->y = c->y; st->s = c->s; st->p = c->p;
        oric_run(&s_replay, 1);
        if (s_replay.cpu.undoc_count != seen && s_rl.first_field < 0) {
            s_rl.first_field = s_field;
            s_rl.lead_len = n < LEAD_IN ? n : LEAD_IN;
            for (unsigned i = 0; i < s_rl.lead_len; i++)
                s_rl.lead_in[i] = ring[(n - s_rl.lead_len + i) % LEAD_IN];
        }
        if (s_replay.cpu.undoc_count != seen) {
            seen = s_replay.cpu.undoc_count;
            uint8_t op = s_replay.cpu.undoc_op;
            if (!s_rl.count[op]) s_rl.first_pc[op] = s_replay.cpu.undoc_pc;
            s_rl.count[op]++;
            s_rl.total++;
        }
    }
}

/* The address the instruction at PC reads, if it is one of the reads
 * that can reach page #03: absolute, indexed, and the indirect modes.
 * Read-modify-write instructions and the zero page cannot be a matrix
 * read worth noting. */
static bool reads_at(const oric_t *m, uint16_t *ea) {
    const m6502_t *c = &m->cpu;
    uint8_t op = oric_peek(m, c->pc);
    uint8_t lo = oric_peek(m, (uint16_t)(c->pc + 1u));
    uint16_t abs = (uint16_t)(lo | oric_peek(m, (uint16_t)(c->pc + 2u)) << 8);
    switch (op) {
    case 0x0D: case 0x2D: case 0x4D: case 0x6D: case 0xAD: case 0xCD: case 0xED:
    case 0xAE: case 0xAC: case 0x2C: case 0xEC: case 0xCC:
        *ea = abs;
        return true;
    case 0x1D: case 0x3D: case 0x5D: case 0x7D: case 0xBD: case 0xDD: case 0xFD:
    case 0xBC:
        *ea = (uint16_t)(abs + c->x);
        return true;
    case 0x19: case 0x39: case 0x59: case 0x79: case 0xB9: case 0xD9: case 0xF9:
    case 0xBE:
        *ea = (uint16_t)(abs + c->y);
        return true;
    case 0x01: case 0x21: case 0x41: case 0x61: case 0xA1: case 0xC1: case 0xE1: {
        uint8_t z = (uint8_t)(lo + c->x);
        *ea = (uint16_t)(oric_peek(m, z) | oric_peek(m, (uint8_t)(z + 1u)) << 8);
        return true;
    }
    case 0x11: case 0x31: case 0x51: case 0x71: case 0xB1: case 0xD1: case 0xF1:
        *ea = (uint16_t)((oric_peek(m, lo) | oric_peek(m, (uint8_t)(lo + 1u)) << 8) + c->y);
        return true;
    default:
        return false;
    }
}

static void note_keys(void) {
    oric_copy(&s_replay, &s_field_start);
    uint64_t end = g.m.cpu.cycles;
    while (s_replay.cpu.cycles < end) {
        uint16_t ea;
        if (s_replay.cpu.pc < 0xC000u && reads_at(&s_replay, &ea) && (ea & 0xFF0Fu) == 0x0300u) {
            uint8_t row = (uint8_t)(via6522_pb_out(&s_replay.via) & 7u);
            uint8_t cols = (uint8_t)~ay8912_port_a(&s_replay.ay);
            if (cols && !(cols & (cols - 1u))) s_rl.key_one[row] |= cols;
            else s_rl.key_any[row] |= cols;
        }
        oric_run(&s_replay, 1);
    }
}

static void field(void) {
    keymatrix_field(&g.k, &g.m);
    oric_copy(&s_field_start, &g.m);
    oric_run_field(&g.m);
    static int16_t pcm[ORIC_AUDIO_BUF_LEN];
    (void)oric_audio_drain(&g.m, pcm, ORIC_AUDIO_BUF_LEN);
    if (g.m.cpu.undoc_count != s_field_start.cpu.undoc_count) count_undoc();
    if (s_keys) note_keys();
    if (!s_rl.cb1 && (g.m.via.ier & VIA_INT_CB1) && g.m.cpu.pc < 0xC000u) {
        s_rl.cb1 = true;
        s_rl.cb1_pc = g.m.cpu.pc;
    }
    serve(&g.m);
    if (s_drives[0]) host_disc_serve(&g.m, s_drives);
    s_field++;
}

static void settle(void) {
    for (int f = 0; f < 100 && !keymatrix_idle(&g.k); f++) field();
    for (unsigned f = 0; f < ORIC_KEY_GAP_FIELDS; f++) field();
}

static void type(const char *s) {
    for (; *s; s++) {
        picocalc_event_t ev[ORIC_KEY_TEXT_EVENTS];
        unsigned n = keymap_picocalc_text((uint8_t)*s, ev);
        for (unsigned i = 0; i < n; i++) keymatrix_event(&g.k, ev[i].state, ev[i].code);
        settle();
    }
}

static void press(uint8_t code) {
    keymatrix_event(&g.k, KEY_EV_PRESSED, code);
    keymatrix_event(&g.k, KEY_EV_RELEASED, code);
    settle();
}

static bool at_ready(void) {
    for (int r = 27; r >= 1; r--) {
        const char *t = guest_row(&g.m, r);
        while (*t == ' ') t++;
        if (*t) return strcmp(t, "Ready") == 0;
    }
    return false;
}

/* ---- the report --------------------------------------------------------- */

/* The matrix by row and column (keymap_picocalc.c), "" for no key. */
static const char *const s_key_names[8][8] = {
    { "7", "N", "5", "V", "", "1", "X", "3" },
    { "J", "T", "R", "F", "", "ESC", "Q", "D" },
    { "M", "6", "B", "4", "CTRL", "Z", "2", "C" },
    { "K", "9", ";", "-", "", "", "\\", "'" },
    { "SPACE", "COMMA", ".", "UP", "LSHIFT", "LEFT", "DOWN", "RIGHT" },
    { "U", "I", "O", "P", "FUNCT", "DEL", "]", "[" },
    { "Y", "H", "G", "E", "", "A", "S", "W" },
    { "8", "L", "0", "/", "RSHIFT", "RETURN", "", "=" },
};

static void keys_str(char *out, size_t cap) {
    size_t o = 0;
    out[0] = 0;
    for (unsigned r = 0; r < 8; r++)
        for (unsigned c = 0; c < 8; c++) {
            if (!(s_rl.key_one[r] >> c & 1u) || o >= cap) continue;
            const char *n = s_key_names[r][c];
            o += (size_t)snprintf(out + o, cap - o, "%s%s", o ? "," : "", n[0] ? n : "?");
        }
    bool any = false;
    for (unsigned r = 0; r < 8 && o < cap; r++) {
        if (!s_rl.key_any[r]) continue;
        o += (size_t)snprintf(out + o, cap - o, "%s%u/%02X", any ? "," : (o ? " any:" : "any:"), r,
                              s_rl.key_any[r]);
        any = true;
    }
    if (!out[0]) snprintf(out, cap, "-");
}

static uint8_t s_ppm[ORIC_PIXEL_W * ORIC_PIXEL_H * 3u];
static oric_frame_t s_frame;
static ula_cell_t s_cells[ORIC_PIXEL_H][ORIC_SCREEN_COLS];

static void shot(const char *dir, const char *name) {
    char path[2048];
    oric_video_take(&g.m, &s_frame);
    ula_decode(&s_frame, s_cells);
    uint16_t row[ORIC_PIXEL_W];
    for (unsigned y = 0; y < ORIC_PIXEL_H; y++) {
        ula_row(s_cells[y], 0, ORIC_SCREEN_COLS - 1u, ula_palette_rgb565, row);
        for (unsigned x = 0; x < ORIC_PIXEL_W; x++) {
            uint16_t v = row[x];
            uint8_t *p = &s_ppm[(y * ORIC_PIXEL_W + x) * 3u];
            p[0] = (uint8_t)(((v >> 11) & 0x1Fu) * 255u / 31u);
            p[1] = (uint8_t)(((v >> 5) & 0x3Fu) * 255u / 63u);
            p[2] = (uint8_t)((v & 0x1Fu) * 255u / 31u);
        }
    }
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    FILE *f = fopen(path, "wb");
    if (f) {
        fprintf(f, "P6\n%u %u\n255\n", ORIC_PIXEL_W, ORIC_PIXEL_H);
        fwrite(s_ppm, 1, sizeof s_ppm, f);
        fclose(f);
    }
    snprintf(path, sizeof path, "%s/%s.txt", dir, name);
    f = fopen(path, "w");
    if (f) {
        guest_dump(&g.m, f);
        if (s_rl.first_field >= 0) {
            fprintf(f, "\nThe first undocumented opcode, at field %ld, and the instructions "
                       "before it (PC, bytes, A X Y S P before):\n", s_rl.first_field);
            for (unsigned i = 0; i < s_rl.lead_len; i++) {
                const step_t *st = &s_rl.lead_in[i];
                fprintf(f, "  %04X  %02X %02X %02X   %02X %02X %02X %02X %02X\n", st->pc,
                        st->op[0], st->op[1], st->op[2], st->a, st->x, st->y, st->s, st->p);
            }
        }
        fclose(f);
    }
}

/* A .tap, or a title's directory of parts: the deck starts with the
 * shortest .tap name in it, else the first name. */
static bool insert(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return false;
    if (!S_ISDIR(st.st_mode)) {
        const char *slash = strrchr(path, '/');
        char dir[1024] = ".";
        if (slash) snprintf(dir, sizeof dir, "%.*s", (int)(slash - path), path);
        return deck_load(dir, slash ? slash + 1 : path);
    }
    DIR *d = opendir(path);
    if (!d) return false;
    char best[256] = "";
    bool best_tap = false;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        size_t n = strlen(e->d_name);
        bool tap = n > 4 && strcasecmp(e->d_name + n - 4, ".tap") == 0;
        bool better = !best[0] || (tap && !best_tap) ||
                      (tap == best_tap && (tap ? n < strlen(best) || (n == strlen(best) &&
                                                  strcmp(e->d_name, best) < 0)
                                               : strcmp(e->d_name, best) < 0));
        if (better) {
            snprintf(best, sizeof best, "%s", e->d_name);
            best_tap = tap;
        }
    }
    closedir(d);
    return best[0] && deck_load(path, best);
}

static const char *base_name(const char *path) {
    static char b[1024];
    snprintf(b, sizeof b, "%s", path);
    size_t n = strlen(b);
    while (n > 1 && b[n - 1] == '/') b[--n] = 0;
    if (n > 4 && strcasecmp(b + n - 4, ".tap") == 0) b[n - 4] = 0;
    const char *s = strrchr(b, '/');
    return s ? s + 1 : b;
}

static int run_one(const char *path, long fields, const char *shots) {
    memset(&s_tl, 0, sizeof s_tl);
    if (!insert(path)) {
        fprintf(stderr, "%s: cannot open\n", path);
        return 1;
    }
    memset(&s_rl, 0, sizeof s_rl);
    s_rl.first_field = -1;
    s_tl.last_field = -1;
    s_field = 0;

    oric_copy(&g.m, &s_booted.m);
    g.k = s_booted.k;
    g.rom = s_booted.rom;
    g.m.cfg.tape_signal = s_signal;
    s_cas_name[0] = 0;

    type("CLOAD\"\"");
    press(PICOCALC_KEY_ENTER);

    bool started = false, spaced = false, oned = false, returned = false;
    while (s_field < fields) {
        field();
        /* Quiet for two seconds after the last file, at Ready: start the
         * last file if the tape did not. */
        if (!started && s_tl.files && s_field - s_tl.last_field > 100 && at_ready()) {
            started = true;
            if (!s_tl.last_autorun) {
                char cmd[16];
                if (s_tl.last_code) snprintf(cmd, sizeof cmd, "CALL#%X", s_tl.last_start);
                else snprintf(cmd, sizeof cmd, "RUN");
                type(cmd);
                press(PICOCALC_KEY_ENTER);
            }
        }
        if (!spaced && s_field >= fields / 2) {
            spaced = true;
            press(' ');
        }
        if (s_keys && !oned && s_field >= fields / 3) {
            oned = true;
            press('1');
        }
        if (s_keys && !returned && s_field >= fields * 2 / 3) {
            returned = true;
            press(PICOCALC_KEY_ENTER);
        }
    }

    const char *name = base_name(path);
    const char *outcome = s_tl.files == 0 ? "no-load"
                        : s_tl.cut_short ? "cut-short"
                        : s_tl.gave_up   ? "gave-up"
                        : "loaded";
    char ops[256 * 16] = "";
    size_t o = 0;
    for (int op = 0; op < 256; op++) {
        if (!s_rl.count[op]) continue;
        o += (size_t)snprintf(ops + o, sizeof ops - o, "%s#%02X:%u@#%04X", o ? "," : "", op,
                              (unsigned)s_rl.count[op], (unsigned)s_rl.first_pc[op]);
        if (o >= sizeof ops) break;
    }
    char cb1[16] = "-";
    if (s_rl.cb1) snprintf(cb1, sizeof cb1, "#%04X", s_rl.cb1_pc);
    char keys[512] = "";
    if (s_keys) {
        keys[0] = '\t';
        keys_str(keys + 1, sizeof keys - 1);
    }
    printf("%s\t%s %s\t%s\t%u\t%u\t%u\t%s\t%s\t%s\t%s\t%u\t%u%s\n", name,
           g.rom == ROM_BASIC10 ? "1.0" : "1.1",
           g.m.cfg.ram == ORIC_RAM_16K ? "16K" : "48K", outcome, s_tl.files,
           (unsigned)s_tl.bytes, (unsigned)s_rl.total, o ? ops : "-", cb1,
           (g.m.ula_mode & ULA_MODE_HIRES) ? "hires" : "text", s_tl.why[0] ? s_tl.why : "-",
           s_tl.by_name, s_tl.one_byte, keys);
    fflush(stdout);
    if (shots) shot(shots, name);
    return 0;
}

int main(int argc, char **argv) {
    rom_id_t rom = ROM_BASIC11;
    oric_ram_t ram = ORIC_RAM_48K;
    long fields = 3000;
    const char *shots = NULL;
    bool vsync_hack = false;
    const char *dsk = NULL;
    int i = 1;
    for (; i < argc && argv[i][0] == '-'; i++) {
        if (!strcmp(argv[i], "-r") && i + 1 < argc)
            rom = atoi(argv[++i]) == 10 ? ROM_BASIC10 : ROM_BASIC11;
        else if (!strcmp(argv[i], "-m") && i + 1 < argc)
            ram = atoi(argv[++i]) == 16 ? ORIC_RAM_16K : ORIC_RAM_48K;
        else if (!strcmp(argv[i], "-f") && i + 1 < argc)
            fields = atol(argv[++i]);
        else if (!strcmp(argv[i], "-s") && i + 1 < argc)
            shots = argv[++i];
        else if (!strcmp(argv[i], "-t"))
            s_signal = true;
        else if (!strcmp(argv[i], "-v"))
            vsync_hack = true;
        else if (!strcmp(argv[i], "-k"))
            s_keys = true;
        else if (!strcmp(argv[i], "-d") && i + 1 < argc)
            dsk = argv[++i];
        else {
            fprintf(stderr, "usage: oric-corpus [-r 10|11] [-m 16|48] [-f FIELDS] [-s DIR] [-t] [-v] [-k] [-d DSK] TAPE...\n");
            return 2;
        }
    }
    if (dsk && ram == ORIC_RAM_16K) {   /* the overlay RAM is a 48K machine's (microdisc.h) */
        fprintf(stderr, "oric-corpus: -d needs -m 48\n");
        return 2;
    }
    const char *dir;
    if (!guest_find_roms(&dir)) {
        fprintf(stderr, "oric-corpus: no BASIC ROMs in %s\n", dir);
        return 77;
    }
    oric_config_t cfg;
    oric_config_default(&cfg);
    cfg.rom = rom;
    cfg.ram = ram;
    cfg.vsync_hack = vsync_hack;
    cfg.microdisc = dsk != NULL;
    oric_init(&s_booted.m, &cfg);
    oric_load_rom(&s_booted.m, guest_rom_image(rom), ORIC_ROM_SIZE);
    if (dsk) {
        const char *why = host_disc_load(&s_disc, dsk);
        if (why || !guest_have_rom(ROM_MICRODISC)) {
            fprintf(stderr, "oric-corpus: %s: %s\n", dsk, why ? why : "no microdis.rom");
            return 2;
        }
        s_drives[0] = &s_disc;
        oric_load_eprom(&s_booted.m, guest_rom_image(ROM_MICRODISC), ORIC_EPROM_SIZE);
    }
    oric_power_on(&s_booted.m);
    if (dsk) {
        host_disc_insert(&s_booted.m, 0, &s_disc);
        s_booted.rom = rom;
        keymatrix_init(&s_booted.k);
        for (int f = 0; f < 750; f++) {
            guest_fields(&s_booted, 1);
            host_disc_serve(&s_booted.m, s_drives);
        }
        guest_type(&s_booted, "X");
        for (int f = 0; f < 250 && !keymatrix_idle(&s_booted.k); f++) {
            guest_fields(&s_booted, 1);
            host_disc_serve(&s_booted.m, s_drives);
        }
        for (int f = 0; f < 100; f++) {
            guest_fields(&s_booted, 1);
            host_disc_serve(&s_booted.m, s_drives);
        }
    } else if (!guest_have_rom(rom) || !guest_boot_machine(&s_booted)) {
        fprintf(stderr, "oric-corpus: the machine did not reach Ready\n");
        return 1;
    }
    s_deck.buf = malloc(DECK_MAX);
    if (!s_deck.buf) return 1;
    int bad = 0;
    for (; i < argc; i++) bad |= run_one(argv[i], fields, shots);
    return bad;
}
