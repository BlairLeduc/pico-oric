/* guest.c — a real Oric, on the host (guest.h). */

#include "guest.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>

#define ORIC_SCREEN    0xBB80u   /* the text screen, 28 rows of 40 (§2.2) */
#define SCREEN_ROWS    28
#define SCREEN_COLS    40

static uint8_t images[ROM_IMAGE_COUNT][ORIC_ROM_SIZE];
static bool    have[ROM_IMAGE_COUNT];

static void scan_roms(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        static uint8_t buf[ORIC_ROM_SIZE + 1];
        size_t n = fread(buf, 1, sizeof buf, f);
        fclose(f);
        rom_id_t r = romset_identify(buf, n);
        if (r == ROM_UNKNOWN) continue;
        memcpy(images[r], buf, n);
        have[r] = true;
    }
    closedir(d);
}

bool guest_find_roms(const char **dir) {
    const char *env = getenv("PICO_ORIC_ROMS");
    const char *where = env ? env : PICO_ORIC_DEFAULT_ROMS;
    if (dir) *dir = where;
    scan_roms(where);
    return have[ROM_BASIC10] && have[ROM_BASIC11];
}

bool guest_have_rom(rom_id_t r) {
    return r >= 0 && r < ROM_IMAGE_COUNT && have[r];
}

const uint8_t *guest_rom_image(rom_id_t r) {
    return guest_have_rom(r) ? images[r] : NULL;
}

void guest_fields(guest_t *g, int n) {
    for (int i = 0; i < n; i++) oric_run_field(&g->m);
}

static bool ready_on_screen(const oric_t *m) {
    return guest_find_row(m, "Ready", 0) >= 0;
}

bool guest_boot_machine(guest_t *g) {
    g->rom = g->m.cfg.rom;
    g->ready_cycles = 0;
    uint64_t t0 = g->m.cpu.cycles;
    /* Fine steps rather than fields, so the time to Ready is a
     * measurement and not a count of fields (§15.2 M3). */
    while (g->m.cpu.cycles - t0 < 10u * ORIC_CPU_HZ) {
        oric_run(&g->m, GUEST_READY_STEP);
        if (ready_on_screen(&g->m)) {
            g->ready_cycles = g->m.cpu.cycles - t0;
            /* Let the ROM finish what it prints with Ready. */
            guest_fields(g, 10);
            return true;
        }
    }
    return false;
}

bool guest_boot(guest_t *g, rom_id_t rom, oric_ram_t ram) {
    if (!guest_have_rom(rom)) return false;
    oric_config_t cfg;
    oric_config_default(&cfg);
    cfg.rom = rom;
    cfg.ram = ram;
    oric_init(&g->m, &cfg);
    oric_load_rom(&g->m, images[rom], ORIC_ROM_SIZE);
    oric_reset(&g->m);
    return guest_boot_machine(g);
}

/* ---- typing --------------------------------------------------------- */

/* The key table each ROM's decoder indexes (LDA table,X at #F4AE in 1.0
 * and #F509 in 1.1): 64 unshifted entries, column x 8 + row, then 64
 * shifted. Unshifted letters are lower case with bit 7 set, which the
 * ROM turns to capitals while CAPS is on, as it is after a reset. Read
 * from both ROMs 2026-10-08. */
static uint16_t key_table(rom_id_t r) {
    return r == ROM_BASIC10 ? 0xFF70u : 0xFF78u;
}

/* SHIFT, as the decoder knows it: #A4, column 4 row 4 (CPY #A4 at
 * #F49A in 1.0, #F4F5 in 1.1). */
#define SHIFT_ROW 4
#define SHIFT_COL 4

bool guest_key_for(const guest_t *g, char c, int *row, int *col, bool *shift) {
    uint8_t want = (uint8_t)(c == '\n' ? '\r' : c);
    uint8_t letter = (want >= 'A' && want <= 'Z') ? (uint8_t)((want + 0x20u) | 0x80u) : 0;
    uint16_t t = key_table(g->rom);
    for (int half = 0; half < 2; half++) {
        for (int i = 0; i < 64; i++) {
            uint8_t e = oric_peek(&g->m, (uint16_t)(t + half * 64 + i));
            if (e == 0) continue;
            bool hit = half == 0 ? (e == want || (letter && e == letter)) : e == want;
            if (!hit) continue;
            *row = i & 7;
            *col = i >> 3;
            *shift = half == 1;
            return true;
        }
    }
    return false;
}

/* Fields a key is held, and left up after. Generous: the ROM scans at
 * 100 Hz and wants a key seen twice. The minimum is M5's to settle
 * (§16). */
#define HOLD_FIELDS 4
#define GAP_FIELDS  4

void guest_type(guest_t *g, const char *s) {
    for (; *s; s++) {
        int row, col;
        bool shift;
        if (!guest_key_for(g, *s, &row, &col, &shift)) {
            fprintf(stderr, "guest_type: no key for '%c' in this ROM\n", *s);
            continue;
        }
        /* SHIFT goes down a field before the key and up a field after,
         * as a typist's does. Pressed together, a scan already past
         * column 4 finds the key alone, and ROM 1.0 takes it at once:
         * `(` came out as `9` (2026-10-08). */
        if (shift) {
            oric_key_set(&g->m, SHIFT_ROW, SHIFT_COL, true);
            guest_fields(g, 1);
        }
        oric_key_set(&g->m, row, col, true);
        guest_fields(g, HOLD_FIELDS);
        oric_key_set(&g->m, row, col, false);
        if (shift) {
            guest_fields(g, 1);
            oric_key_set(&g->m, SHIFT_ROW, SHIFT_COL, false);
        }
        guest_fields(g, GAP_FIELDS);
    }
    guest_fields(g, 25);
}

/* ---- the screen ------------------------------------------------------- */

const char *guest_row(const oric_t *m, int row) {
    static char s[SCREEN_COLS + 1];
    for (int c = 0; c < SCREEN_COLS; c++) {
        uint8_t v = (uint8_t)(oric_peek(m, (uint16_t)(ORIC_SCREEN + row * SCREEN_COLS + c)) & 0x7Fu);
        /* Bits 6 and 5 both clear: a serial attribute, shown as paper
         * (§2.5). */
        s[c] = (v & 0x60u) ? (char)v : ' ';
        if (s[c] == 0x7F) s[c] = '#';
    }
    s[SCREEN_COLS] = 0;
    for (int c = SCREEN_COLS - 1; c >= 0 && s[c] == ' '; c--) s[c] = 0;
    return s;
}

int guest_find_row(const oric_t *m, const char *s, int from) {
    for (int r = from; r < SCREEN_ROWS; r++) {
        const char *t = guest_row(m, r);
        while (*t == ' ') t++;
        if (strcmp(t, s) == 0) return r;
    }
    return -1;
}

void guest_dump(const oric_t *m, FILE *f) {
    for (int r = 0; r < SCREEN_ROWS; r++) fprintf(f, "  %2d |%s\n", r, guest_row(m, r));
}
