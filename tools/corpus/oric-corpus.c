/* oric-corpus.c — M12's corpus run: archive tapes loaded and run on the
 * host, one line each (design.md §15, M12; §5.1).
 *
 *   oric-corpus [-r 10|11] [-m 16|48] [-f FIELDS] [-s DIR] [-N] [-B] TAPE...
 *
 * Each tape gets a machine of its own, copied from one booted to Ready.
 * A TAPE is a .tap, or a directory of a title's parts, whose deck starts
 * with its first (the shortest .tap name, else the first name). The
 * harness types CLOAD"" and serves the tape as the firmware's tapeio
 * does (design.md §10.3): CLOAD "NAME" puts NAME.tap from the tape's own
 * directory in the deck, if there is one; at the end of the tape it
 * rewinds once, and a find that goes round it twice, or a file cut short,
 * gives up, back to Ready.
 *
 * Two changes to tapeio, measured here before they are made: -N looks
 * for NAME as it is before NAME.tap (TOSEC's parts are "x.ta1", and some
 * loaders ask for "X.TAP"); -B lets a tape's last file end one byte
 * short, the byte in memory kept, as Oricutron allows "for broken tape
 * images" (tape.c). When the tape has gone quiet and the ROM is at Ready, a last file
 * without autorun is started, RUN for BASIC and CALL for code; half way
 * through, Space is pressed, for the titles that wait for a key.
 *
 * A field that traps an undocumented opcode is run again from its start
 * an instruction at a time, so every one is counted by opcode, with the
 * first PC each was seen at. The CB1 interrupt enabled while the PC is
 * outside the ROM is noted: the vertical-sync modification's titles, for
 * M16 (design.md §15).
 *
 * One line per tape on stdout, tab-separated:
 *   name  machine  outcome  files  bytes  undoc  opcodes  cb1  mode  note
 *   by-name  one-byte
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
    unsigned one_byte;        /* files one byte short, let through (-B)  */
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
} run_log_t;

static guest_t    s_booted;
static guest_t    g;
static oric_t     s_field_start, s_replay;
static deck_t     s_deck;
static tape_log_t s_tl;
static run_log_t  s_rl;
static long       s_field;
static bool       s_name_as_is, s_one_byte;

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

/* tapeio's choose_tape: CLOAD's name, its trailing spaces gone, as
 * NAME.tap; with -N, as NAME first. */
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
    bool got = s_name_as_is && find_file(want, name, sizeof name);
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

static void serve(oric_t *m) {
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
            if (t->want[0]) snprintf(why, sizeof why, "\"%s\" not on the tape", (const char *)t->want);
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
        if (s_one_byte && t->done + 1u == t->len) {
            /* The tape's last byte is missing: the one in memory stays. */
            uint8_t b = oric_peek(m, (uint16_t)(t->start + t->done));
            oric_tape_load_data(m, &b, 1);
            s_tl.one_byte++;
        }
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

static void field(void) {
    oric_copy(&s_field_start, &g.m);
    keymatrix_field(&g.k, &g.m);
    oric_run_field(&g.m);
    static int16_t pcm[ORIC_AUDIO_BUF_LEN];
    (void)oric_audio_drain(&g.m, pcm, ORIC_AUDIO_BUF_LEN);
    if (g.m.cpu.undoc_count != s_field_start.cpu.undoc_count) count_undoc();
    if (!s_rl.cb1 && (g.m.via.ier & VIA_INT_CB1) && g.m.cpu.pc < 0xC000u) {
        s_rl.cb1 = true;
        s_rl.cb1_pc = g.m.cpu.pc;
    }
    serve(&g.m);
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

    type("CLOAD\"\"");
    press(PICOCALC_KEY_ENTER);

    bool started = false, spaced = false;
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
    printf("%s\t%s %s\t%s\t%u\t%u\t%u\t%s\t%s\t%s\t%s\t%u\t%u\n", name,
           g.rom == ROM_BASIC10 ? "1.0" : "1.1",
           g.m.cfg.ram == ORIC_RAM_16K ? "16K" : "48K", outcome, s_tl.files,
           (unsigned)s_tl.bytes, (unsigned)s_rl.total, o ? ops : "-", cb1,
           (g.m.ula_mode & ULA_MODE_HIRES) ? "hires" : "text", s_tl.why[0] ? s_tl.why : "-",
           s_tl.by_name, s_tl.one_byte);
    fflush(stdout);
    if (shots) shot(shots, name);
    return 0;
}

int main(int argc, char **argv) {
    rom_id_t rom = ROM_BASIC11;
    oric_ram_t ram = ORIC_RAM_48K;
    long fields = 3000;
    const char *shots = NULL;
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
        else if (!strcmp(argv[i], "-N"))
            s_name_as_is = true;
        else if (!strcmp(argv[i], "-B"))
            s_one_byte = true;
        else {
            fprintf(stderr, "usage: oric-corpus [-r 10|11] [-m 16|48] [-f FIELDS] [-s DIR] [-N] [-B] TAPE...\n");
            return 2;
        }
    }
    const char *dir;
    if (!guest_find_roms(&dir)) {
        fprintf(stderr, "oric-corpus: no BASIC ROMs in %s\n", dir);
        return 77;
    }
    if (!guest_boot(&s_booted, rom, ram)) {
        fprintf(stderr, "oric-corpus: the machine did not reach Ready\n");
        return 1;
    }
    s_deck.buf = malloc(DECK_MAX);
    if (!s_deck.buf) return 1;
    int bad = 0;
    for (; i < argc; i++) bad |= run_one(argv[i], fields, shots);
    return bad;
}
