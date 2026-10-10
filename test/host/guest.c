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
    for (int i = 0; i < n; i++) {
        keymatrix_field(&g->k, &g->m);
        oric_run_field(&g->m);
        if (g->after_field) g->after_field(g);
    }
}

static bool ready_on_screen(const oric_t *m) {
    return guest_find_row(m, "Ready", 0) >= 0;
}

bool guest_boot_machine(guest_t *g) {
    g->rom = g->m.cfg.rom;
    g->ready_cycles = 0;
    keymatrix_init(&g->k);
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

bool guest_settle(guest_t *g, int max_fields) {
    int f = 0;
    for (; f < max_fields && !keymatrix_idle(&g->k); f++) guest_fields(g, 1);
    guest_fields(g, ORIC_KEY_GAP_FIELDS);
    return keymatrix_idle(&g->k);
}

void guest_press(guest_t *g, uint8_t code, bool alt) {
    /* A code that is not its own key's base is a Shift chord on the
     * PicoCalc (keymap_picocalc_canonical). */
    bool shift = keymap_picocalc_canonical(code) != code;
    if (alt)   keymatrix_event(&g->k, KEY_EV_PRESSED, PICOCALC_KEY_ALT);
    if (shift) keymatrix_event(&g->k, KEY_EV_PRESSED, PICOCALC_KEY_SHIFT_L);
    keymatrix_event(&g->k, KEY_EV_PRESSED, code);
    keymatrix_event(&g->k, KEY_EV_RELEASED, code);
    if (shift) keymatrix_event(&g->k, KEY_EV_RELEASED, PICOCALC_KEY_SHIFT_L);
    if (alt)   keymatrix_event(&g->k, KEY_EV_RELEASED, PICOCALC_KEY_ALT);
    if (!guest_settle(g, 100)) {
        fprintf(stderr, "guest_press: 0x%02X still held after 100 fields\n", code);
        abort();
    }
}

void guest_type(guest_t *g, const char *s) {
    for (; *s; s++) {
        picocalc_event_t ev[ORIC_KEY_TEXT_EVENTS];
        unsigned n = keymap_picocalc_text((uint8_t)*s, ev);
        if (n == 0) {
            fprintf(stderr, "guest_type: no PicoCalc key for 0x%02X\n", (uint8_t)*s);
            abort();
        }
        for (unsigned i = 0; i < n; i++) keymatrix_event(&g->k, ev[i].state, ev[i].code);
        if (!guest_settle(g, 100)) {
            fprintf(stderr, "guest_type: 0x%02X still held after 100 fields\n", (uint8_t)*s);
            abort();
        }
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
