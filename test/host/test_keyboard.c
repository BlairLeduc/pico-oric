/* test_keyboard.c — the keyboard, settled by executing both ROMs (design.md
 * §9.3, §15.2 M5).
 *
 * 1. The sweep: every cell of the matrix pressed at the prompt alone,
 *    with each SHIFT, with CTRL and with FUNCT, and what the ROM decoded.
 * 2. Every entry of the PicoCalc table typed through keymatrix into the
 *    ROM, and Ctrl with every letter.
 * 3. The hold and gap the ROM's scan needs, with controls that must fail,
 *    and its type-ahead.
 *
 * Adapted from pico-ace's (§4.6). With --print it prints the sweep as a
 * table, for §2.4.
 */

#include <string.h>

#include "guest.h"
#include "test_util.h"

static guest_t g;
static oric_t  booted;
static int     input_row;   /* the row the prompt's cursor is on */

/* Where each ROM's interrupt handler stores the key its decoder returns,
 * bit 7 set: STX #02DF (#FC64 in 1.0, #EE68 in 1.1; read 2026-10-08).
 * BASIC takes it from there and clears it. */
static const char *rom_label(rom_id_t r) {
    return r == ROM_BASIC10 ? "BASIC 1.0" : "BASIC 1.1";
}

static uint16_t store_pc(rom_id_t r) {
    return r == ROM_BASIC10 ? 0xFC64u : 0xEE68u;
}

/* ---- running with the decoder watched ---------------------------------- */

#define LOG_MAX 64
static uint8_t decoded[LOG_MAX];   /* bit 7 stripped */
static int     n_decoded;

/* One field's cycles, an instruction at a time, noting every key the
 * ROM stores. The field's bookkeeping (oric_run_field) is not needed
 * here: the screen stays in text mode. */
static void field_watched(void) {
    uint16_t at = store_pc(g.rom);
    uint32_t want = oric_field_cycles(&g.m), done = 0;
    while (done < want) {
        if (g.m.cpu.pc == at && n_decoded < LOG_MAX) decoded[n_decoded++] = g.m.cpu.x & 0x7Fu;
        done += oric_run(&g.m, 1);
    }
    g.m.fields++;
}

static void fields_raw(int n) {
    for (int i = 0; i < n; i++) field_watched();
}

/* Through the held set, as guest_fields, but watched. */
static void fields_k(int n) {
    for (int i = 0; i < n; i++) {
        keymatrix_field(&g.k, &g.m);
        field_watched();
    }
}

static bool settle_k(int max) {
    for (int f = 0; f < max && !keymatrix_idle(&g.k); f++) fields_k(1);
    fields_k(ORIC_KEY_GAP_FIELDS + 2);
    return keymatrix_idle(&g.k);
}

static void fresh(void) {
    oric_copy(&g.m, &booted);
    keymatrix_init(&g.k);
    n_decoded = 0;
}

/* A screen row without the paper cells at its left. */
static const char *row_text(int row) {
    const char *s = guest_row(&g.m, row);
    while (*s == ' ') s++;
    return s;
}

/* The input line as typed. */
static const char *input(void) {
    return row_text(input_row);
}

/* ---- 1. the sweep ------------------------------------------------------ */

enum { M_ALONE, M_SHIFT_L, M_SHIFT_R, M_CTRL, M_FUNCT, M_COUNT };
static const char *const mode_name[M_COUNT] = {
    "alone", "with left SHIFT", "with right SHIFT", "with CTRL", "with FUNCT",
};
static const uint8_t mode_row[M_COUNT] = {
    0, OK_ROW_SHIFT_L, OK_ROW_SHIFT_R, OK_ROW_CTRL, OK_ROW_FUNCT,
};

/* What each cell decodes to, read off both ROMs by this sweep and kept
 * as its regression (§2.4, §9.3): bit 7 stripped, 0 for nothing. The
 * two ROMs agree in every cell (2026-10-08). Column 4 decodes nothing
 * alone: its keys are the modifiers. */
static const uint8_t sweep_want[ORIC_KEY_ROWS][ORIC_KEY_COLS][M_COUNT] = {
    {   /* row 0 */
        { 0x37, 0x26, 0x26, 0x37, 0x37 },   /* 7 & & 7 7 */
        { 0x4E, 0x4E, 0x4E, 0x0E, 0x4E },   /* N N N #0E N */
        { 0x35, 0x25, 0x25, 0x35, 0x35 },   /* 5 % % 5 5 */
        { 0x56, 0x56, 0x56, 0x16, 0x56 },   /* V V V #16 V */
        { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* - - - - - */
        { 0x31, 0x21, 0x21, 0x31, 0x31 },   /* 1 ! ! 1 1 */
        { 0x58, 0x58, 0x58, 0x18, 0x58 },   /* X X X #18 X */
        { 0x33, 0x23, 0x23, 0x33, 0x33 },   /* 3 # # 3 3 */
    },
    {   /* row 1 */
        { 0x4A, 0x4A, 0x4A, 0x0A, 0x4A },   /* J J J #0A J */
        { 0x54, 0x54, 0x54, 0x14, 0x54 },   /* T T T #14 T */
        { 0x52, 0x52, 0x52, 0x12, 0x52 },   /* R R R #12 R */
        { 0x46, 0x46, 0x46, 0x06, 0x46 },   /* F F F #06 F */
        { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* - - - - - */
        { 0x1B, 0x1B, 0x1B, 0x1B, 0x1B },   /* #1B #1B #1B #1B #1B */
        { 0x51, 0x51, 0x51, 0x11, 0x51 },   /* Q Q Q #11 Q */
        { 0x44, 0x44, 0x44, 0x04, 0x44 },   /* D D D #04 D */
    },
    {   /* row 2 */
        { 0x4D, 0x4D, 0x4D, 0x0D, 0x4D },   /* M M M #0D M */
        { 0x36, 0x5E, 0x5E, 0x36, 0x36 },   /* 6 ^ ^ 6 6 */
        { 0x42, 0x42, 0x42, 0x02, 0x42 },   /* B B B #02 B */
        { 0x34, 0x24, 0x24, 0x34, 0x34 },   /* 4 $ $ 4 4 */
        { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* - - - - - */
        { 0x5A, 0x5A, 0x5A, 0x1A, 0x5A },   /* Z Z Z #1A Z */
        { 0x32, 0x40, 0x40, 0x32, 0x32 },   /* 2 @ @ 2 2 */
        { 0x43, 0x43, 0x43, 0x03, 0x43 },   /* C C C #03 C */
    },
    {   /* row 3 */
        { 0x4B, 0x4B, 0x4B, 0x0B, 0x4B },   /* K K K #0B K */
        { 0x39, 0x28, 0x28, 0x39, 0x39 },   /* 9 ( ( 9 9 */
        { 0x3B, 0x3A, 0x3A, 0x3B, 0x3B },   /* ; : : ; ; */
        { 0x2D, 0x5F, 0x5F, 0x2D, 0x2D },   /* - _ _ - - */
        { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* - - - - - */
        { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* - - - - - */
        { 0x5C, 0x7C, 0x7C, 0x1C, 0x5C },   /* \ | | #1C \ */
        { 0x27, 0x22, 0x22, 0x27, 0x27 },   /* ' " " ' ' */
    },
    {   /* row 4 */
        { 0x20, 0x20, 0x20, 0x20, 0x20 },   /* SPACE SPACE SPACE SPACE SPACE */
        { 0x2C, 0x3C, 0x3C, 0x2C, 0x2C },   /* , < < , , */
        { 0x2E, 0x3E, 0x3E, 0x2E, 0x2E },   /* . > > . . */
        { 0x0B, 0x0B, 0x0B, 0x0B, 0x0B },   /* #0B #0B #0B #0B #0B */
        { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* - - - - - */
        { 0x08, 0x08, 0x08, 0x08, 0x08 },   /* #08 #08 #08 #08 #08 */
        { 0x0A, 0x0A, 0x0A, 0x0A, 0x0A },   /* #0A #0A #0A #0A #0A */
        { 0x09, 0x09, 0x09, 0x09, 0x09 },   /* #09 #09 #09 #09 #09 */
    },
    {   /* row 5 */
        { 0x55, 0x55, 0x55, 0x15, 0x55 },   /* U U U #15 U */
        { 0x49, 0x49, 0x49, 0x09, 0x49 },   /* I I I #09 I */
        { 0x4F, 0x4F, 0x4F, 0x0F, 0x4F },   /* O O O #0F O */
        { 0x50, 0x50, 0x50, 0x10, 0x50 },   /* P P P #10 P */
        { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* - - - - - */
        { 0x7F, 0x7F, 0x7F, 0x1F, 0x7F },   /* #7F #7F #7F #1F #7F */
        { 0x5D, 0x7D, 0x7D, 0x1D, 0x5D },   /* ] } } #1D ] */
        { 0x5B, 0x7B, 0x7B, 0x1B, 0x5B },   /* [ { { #1B [ */
    },
    {   /* row 6 */
        { 0x59, 0x59, 0x59, 0x19, 0x59 },   /* Y Y Y #19 Y */
        { 0x48, 0x48, 0x48, 0x08, 0x48 },   /* H H H #08 H */
        { 0x47, 0x47, 0x47, 0x07, 0x47 },   /* G G G #07 G */
        { 0x45, 0x45, 0x45, 0x05, 0x45 },   /* E E E #05 E */
        { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* - - - - - */
        { 0x41, 0x41, 0x41, 0x01, 0x41 },   /* A A A #01 A */
        { 0x53, 0x53, 0x53, 0x13, 0x53 },   /* S S S #13 S */
        { 0x57, 0x57, 0x57, 0x17, 0x57 },   /* W W W #17 W */
    },
    {   /* row 7 */
        { 0x38, 0x2A, 0x2A, 0x38, 0x38 },   /* 8 * * 8 8 */
        { 0x4C, 0x4C, 0x4C, 0x0C, 0x4C },   /* L L L #0C L */
        { 0x30, 0x29, 0x29, 0x30, 0x30 },   /* 0 ) ) 0 0 */
        { 0x2F, 0x3F, 0x3F, 0x2F, 0x2F },   /* / ? ? / / */
        { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* - - - - - */
        { 0x0D, 0x0D, 0x0D, 0x0D, 0x0D },   /* #0D #0D #0D #0D #0D */
        { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* - - - - - */
        { 0x3D, 0x2B, 0x2B, 0x3D, 0x3D },   /* = + + = = */
    },
};

/* Press a cell, with a modifier held a field first, and return what the
 * ROM decoded, or 0. */
static uint8_t press_cell(unsigned r, unsigned c, unsigned md) {
    fresh();
    if (md != M_ALONE) {
        oric_key_set(&g.m, mode_row[md], OK_COL_MODS, true);
        fields_raw(2);
    }
    oric_key_set(&g.m, (int)r, (int)c, true);
    fields_raw(3);
    memset(g.m.keys, 0, sizeof g.m.keys);
    fields_raw(4);
    return n_decoded > 0 ? decoded[0] : 0;
}

/* Two to a message, so the buffers take turns. */
static const char *show(uint8_t v) {
    static char buf[2][8];
    static unsigned turn;
    char *s = buf[turn++ & 1u];
    if (v == 0) return "-";
    if (v > 0x20u && v < 0x7Fu) snprintf(s, sizeof buf[0], "'%c'", v);
    else snprintf(s, sizeof buf[0], "#%02X", v);
    return s;
}

static int run_sweep(bool print) {
    unsigned typed = 0;
    for (unsigned r = 0; r < ORIC_KEY_ROWS; r++) {
        for (unsigned c = 0; c < ORIC_KEY_COLS; c++) {
            if (print) printf("    { ");
            for (unsigned md = 0; md < M_COUNT; md++) {
                bool own_mod = c == OK_COL_MODS && r == mode_row[md] && md != M_ALONE;
                uint8_t got = own_mod ? 0 : press_cell(r, c, md);
                if (print) {
                    printf("0x%02X%s", got, md + 1 < M_COUNT ? ", " : "");
                    continue;
                }
                CHECK(n_decoded <= 1, "(%u,%u) %s decoded %d keys", r, c, mode_name[md],
                      n_decoded);
                uint8_t want = sweep_want[r][c][md];
                CHECK(got == want, "(%u,%u) %s decodes %s, want %s", r, c, mode_name[md],
                      show(got), show(want));
                /* What it decoded is what reaches the screen. */
                if (want > 0x20u && want < 0x7Fu) {
                    char w[2] = { (char)want, 0 };
                    CHECK(strcmp(input(), w) == 0, "(%u,%u) %s types \"%s\", want \"%s\"",
                          r, c, mode_name[md], input(), w);
                    typed++;
                }
            }
            if (print) printf(" },   /* row %u col %u */\n", r, c);
        }
    }
    if (!print) printf("  sweep: %u presses read back from the screen\n", typed);
    return 0;
}

/* Column 4 keeps one key, the highest row down (§2.4): right SHIFT hides
 * FUNCT, FUNCT hides left SHIFT, either SHIFT hides CTRL. */
static int column_4(void) {
    static const struct { uint8_t a, b; uint8_t want; const char *what; } pairs[] = {
        { OK_ROW_FUNCT, OK_ROW_SHIFT_L, 'a', "FUNCT hides left SHIFT" },
        { OK_ROW_FUNCT, OK_ROW_SHIFT_R, 'A', "right SHIFT hides FUNCT" },
        { OK_ROW_CTRL,  OK_ROW_SHIFT_L, 'A', "left SHIFT hides CTRL" },
        { OK_ROW_CTRL,  OK_ROW_SHIFT_R, 'A', "right SHIFT hides CTRL" },
        { OK_ROW_CTRL,  OK_ROW_FUNCT,   'a', "FUNCT hides CTRL" },
    };
    for (unsigned i = 0; i < sizeof pairs / sizeof pairs[0]; i++) {
        fresh();
        /* CAPS off, so a lower-case 'a' says SHIFT was not seen. */
        guest_type(&g, "\x14");
        n_decoded = 0;
        oric_key_set(&g.m, pairs[i].a, OK_COL_MODS, true);
        oric_key_set(&g.m, pairs[i].b, OK_COL_MODS, true);
        fields_raw(2);
        oric_key_set(&g.m, 6, 5, true);   /* A */
        fields_raw(3);
        memset(g.m.keys, 0, sizeof g.m.keys);
        fields_raw(4);
        uint8_t got = n_decoded ? decoded[0] : 0;
        CHECK(got == pairs[i].want, "%s: A decodes %s, want %s", pairs[i].what, show(got),
              show(pairs[i].want));
    }
    return 0;
}

/* ---- 2. the PicoCalc table through the ROM ----------------------------- */

/* What the ROM should decode for a table entry's code: the code, with
 * letters in capitals while CAPS is on, as it is after a reset, and the
 * Oric's codes for its own keys. 0 for none. */
static uint8_t want_for(uint8_t code) {
    if (code >= 'a' && code <= 'z') return (uint8_t)(code - 0x20u);
    if (code >= 0x20u && code <= 0x7Eu) return code;
    switch (code) {
    case PICOCALC_KEY_ENTER:     return 0x0Du;
    case PICOCALC_KEY_BACKSPACE: return 0x7Fu;
    case PICOCALC_KEY_DEL:       return 0x7Fu;
    case PICOCALC_KEY_ESC:       return 0x1Bu;
    case PICOCALC_KEY_LEFT:      return 0x08u;
    case PICOCALC_KEY_RIGHT:     return 0x09u;
    case PICOCALC_KEY_DOWN:      return 0x0Au;
    case PICOCALC_KEY_UP:        return 0x0Bu;
    default:                     return 0;   /* FUNCT, the menu's keys */
    }
}

static void press_k(uint8_t code, bool alt) {
    bool shift = keymap_picocalc_canonical(code) != code;
    if (alt)   keymatrix_event(&g.k, KEY_EV_PRESSED, PICOCALC_KEY_ALT);
    if (shift) keymatrix_event(&g.k, KEY_EV_PRESSED, PICOCALC_KEY_SHIFT_L);
    keymatrix_event(&g.k, KEY_EV_PRESSED, code);
    keymatrix_event(&g.k, KEY_EV_RELEASED, code);
    if (shift) keymatrix_event(&g.k, KEY_EV_RELEASED, PICOCALC_KEY_SHIFT_L);
    if (alt)   keymatrix_event(&g.k, KEY_EV_RELEASED, PICOCALC_KEY_ALT);
    settle_k(100);
}

static int every_entry(void) {
    unsigned n = 0;
    for (size_t i = 0; i < keymap_picocalc_len; i++) {
        const keymap_t *e = &keymap_picocalc[i];
        fresh();
        press_k(e->code, (e->flags & KM_ALT) != 0);
        uint8_t want = (e->flags & KM_ALT) ? 0 : want_for(e->code);
        uint8_t got = n_decoded ? decoded[0] : 0;
        CHECK(n_decoded == (want ? 1 : 0) && got == want,
              "0x%02X%s decodes %d key(s), %s, want %s", e->code,
              (e->flags & KM_ALT) ? " with Alt" : "", n_decoded, show(got), show(want));
        if (want > 0x20u && want < 0x7Fu) {
            char w[2] = { (char)want, 0 };
            CHECK(strcmp(input(), w) == 0, "'%c' types \"%s\"", e->code, input());
        }
        n++;
    }
    printf("  %u table entries typed through each ROM\n", n);

    /* With CAPS off (CTRL-T), the PicoCalc's Shift gives capitals and
     * its plain letters lower case, as on the Oric. */
    fresh();
    guest_type(&g, "\x14");
    n_decoded = 0;
    for (const char *c = "aZq"; *c; c++) press_k((uint8_t)*c, false);
    CHECK(n_decoded == 3 && decoded[0] == 'a' && decoded[1] == 'Z' && decoded[2] == 'q',
          "CAPS off: a Z q decode %s ...", show(decoded[0]));
    CHECK(strcmp(input(), "aZq") == 0, "CAPS off: a Z q types \"%s\"", input());

    /* Ctrl with every letter is the Oric's CTRL with it, #01-#1A. */
    for (uint8_t c = 'a'; c <= 'z'; c++) {
        fresh();
        keymatrix_event(&g.k, KEY_EV_PRESSED, PICOCALC_KEY_CTRL);
        keymatrix_event(&g.k, KEY_EV_PRESSED, c);
        keymatrix_event(&g.k, KEY_EV_RELEASED, c);
        keymatrix_event(&g.k, KEY_EV_RELEASED, PICOCALC_KEY_CTRL);
        settle_k(100);
        uint8_t want = (uint8_t)(c - 'a' + 1u);
        CHECK(n_decoded == 1 && decoded[0] == want, "Ctrl+%c decodes %s, want #%02X", c,
              n_decoded ? show(decoded[0]) : "nothing", want);
    }

    /* The requests reach nothing in the guest. */
    static const uint8_t asks[] = { 'M', 'H', 'P', 'K' };
    for (unsigned i = 0; i < 4; i++) {
        fresh();
        press_k(asks[i], true);
        CHECK(n_decoded == 0 && (g.k.menu_request || g.k.pause_request || g.k.reset_request),
              "Alt+%c: %d key(s) reached the guest, or nothing was asked", asks[i], n_decoded);
    }
    return 0;
}

/* Editing at the prompt, by what the keys do to the screen. */
static int editing_keys(void) {
    /* Backspace and Del are DEL, which deletes to the left. */
    static const uint8_t deletes[] = { PICOCALC_KEY_BACKSPACE, PICOCALC_KEY_DEL };
    for (unsigned i = 0; i < 2; i++) {
        fresh();
        guest_type(&g, "AB");
        guest_press(&g, deletes[i], false);
        guest_fields(&g, 10);
        CHECK(strcmp(input(), "A") == 0, "AB, 0x%02X: \"%s\"", deletes[i], input());
    }

    /* Left, then a key, types over the B. */
    fresh();
    guest_type(&g, "AB");
    guest_press(&g, PICOCALC_KEY_LEFT, false);
    guest_type(&g, "C");
    CHECK(strcmp(input(), "AC") == 0, "AB, left, C: \"%s\"", input());
    fresh();
    guest_type(&g, "AB");
    guest_press(&g, PICOCALC_KEY_LEFT, false);
    guest_press(&g, PICOCALC_KEY_LEFT, false);
    guest_press(&g, PICOCALC_KEY_RIGHT, false);
    guest_type(&g, "C");
    CHECK(strcmp(input(), "AC") == 0, "AB, left, left, right, C: \"%s\"", input());

    /* Up and down move a row. */
    fresh();
    guest_press(&g, PICOCALC_KEY_UP, false);
    guest_type(&g, "Q");
    const char *above = guest_row(&g.m, input_row - 1);
    CHECK(strstr(above, "Q") != NULL && strchr(input(), 'Q') == NULL,
          "up, Q: the row above reads \"%s\"", above);
    fresh();
    guest_press(&g, PICOCALC_KEY_DOWN, false);
    guest_type(&g, "Q");
    const char *below = guest_row(&g.m, input_row + 1);
    CHECK(strstr(below, "Q") != NULL, "down, Q: the row below reads \"%s\"", below);

    /* Enter runs the line. */
    fresh();
    guest_type(&g, "PRINT 6*7\n");
    CHECK(strcmp(row_text(input_row + 1), "42") == 0, "PRINT 6*7 printed \"%s\"",
          row_text(input_row + 1));
    return 0;
}

/* ---- 3. the hold and gap the ROM needs ---------------------------------- */

/* "AABBAB112" straight into the matrix, each key held `hold` fields with
 * `gap` up between, from each of six starting phases against the scan's
 * 30 ms; true if the ROM read it back every time. */
static bool types_at(unsigned hold, unsigned gap) {
    static const uint8_t keys[][2] = {
        { 6, 5 }, { 6, 5 }, { 2, 2 }, { 2, 2 }, { 6, 5 }, { 2, 2 }, { 0, 5 }, { 0, 5 }, { 2, 6 },
    };
    for (unsigned phase = 0; phase < 6; phase++) {
        fresh();
        oric_run(&g.m, phase * 5000u);
        for (unsigned i = 0; i < sizeof keys / sizeof keys[0]; i++) {
            oric_key_set(&g.m, keys[i][0], keys[i][1], true);
            for (unsigned f = 0; f < hold; f++) oric_run_field(&g.m);
            oric_key_set(&g.m, keys[i][0], keys[i][1], false);
            for (unsigned f = 0; f < gap; f++) oric_run_field(&g.m);
        }
        for (unsigned f = 0; f < 10; f++) oric_run_field(&g.m);
        if (strcmp(input(), "AABBAB112") != 0) return false;
    }
    return true;
}

/* How many times one key held `hold` fields is typed. */
static size_t typed_by_hold(unsigned hold) {
    fresh();
    oric_key_set(&g.m, 6, 5, true);
    for (unsigned f = 0; f < hold; f++) oric_run_field(&g.m);
    oric_key_set(&g.m, 6, 5, false);
    for (unsigned f = 0; f < 10; f++) oric_run_field(&g.m);
    return strlen(input());
}

static int hold_and_gap(int *need_hold, int *need_gap) {
    /* The least of each that types the line, the other held generous. */
    int hold = -1, gap = -1;
    for (int h = 1; h <= 8 && hold < 0; h++)
        if (types_at((unsigned)h, 4)) hold = h;
    for (int gp = 0; gp <= 4 && hold > 0 && gap < 0; gp++)
        if (types_at((unsigned)hold, (unsigned)gp)) gap = gp;
    printf("  %s: the ROM needs a key held %d fields and %d up, %d fields a key; "
           "the replay uses %u and %u\n",
           rom_label(g.rom), hold, gap, hold + gap, (unsigned)ORIC_KEY_MIN_FIELDS,
           (unsigned)ORIC_KEY_GAP_FIELDS);
    *need_hold = hold;
    *need_gap = gap;

    /* Settled: two fields down and two up (§16). The controls must fail. */
    CHECK(hold == 2 && gap == 2, "the ROM needs %d held and %d up, expected 2 and 2", hold,
          gap);
    CHECK(!types_at(1, 4), "control: 1 field held should lose keys");
    CHECK(!types_at(2, 1), "control: 1 field up should lose keys");
    CHECK(types_at(ORIC_KEY_MIN_FIELDS, ORIC_KEY_GAP_FIELDS), "the replay's own timing fails");

    /* The ceiling: a key held 32 scans repeats, then every 4 (§16). */
    size_t r48 = typed_by_hold(48), r50 = typed_by_hold(50), r60 = typed_by_hold(60);
    printf("  %s: a key held 48 fields types %zu, 50 types %zu, 60 types %zu\n",
           rom_label(g.rom), r48, r50, r60);
    /* 60 fields is 40 scans: typed at the first, the 33rd and the 37th. */
    CHECK(r48 == 1 && r50 == 2 && r60 == 3, "repeat: 48 fields typed %zu, 50 typed %zu, "
          "60 typed %zu", r48, r50, r60);
    CHECK(ORIC_KEY_MIN_FIELDS < 48u, "the replay's hold would repeat");

    /* Through keymatrix, a line arriving in one poll, as a fast typist's
     * would: the replay's own rate, in fields per key. */
    fresh();
    const char *line = "PRINT 2+2";
    for (const char *c = line; *c; c++) {
        picocalc_event_t ev[ORIC_KEY_TEXT_EVENTS];
        unsigned n = keymap_picocalc_text((uint8_t)*c, ev);
        for (unsigned i = 0; i < n; i++) keymatrix_event(&g.k, ev[i].state, ev[i].code);
    }
    keymatrix_event(&g.k, KEY_EV_PRESSED, PICOCALC_KEY_ENTER);
    keymatrix_event(&g.k, KEY_EV_RELEASED, PICOCALC_KEY_ENTER);
    uint32_t f0 = g.m.fields;
    for (int f = 0; f < 400 && !keymatrix_idle(&g.k); f++) guest_fields(&g, 1);
    CHECK(keymatrix_idle(&g.k), "the line was not replayed in 400 fields");
    unsigned keys = (unsigned)strlen(line) + 1u;
    unsigned took = (unsigned)(g.m.fields - f0);
    printf("  %s: a line of %u keys in one poll replays in %u fields, %.1f a key\n",
           rom_label(g.rom), keys, took, (double)took / keys);
    guest_fields(&g, 25);
    CHECK(strcmp(row_text(input_row + 1), "4") == 0, "PRINT 2+2 printed \"%s\"",
          row_text(input_row + 1));
    return 0;
}

/* The text screen without bit 7, which the cursor's blink toggles. */
static uint32_t screen_hash(void) {
    uint32_t h = 2166136261u;
    for (unsigned i = 0; i < ORIC_SCREEN_ROWS * ORIC_SCREEN_COLS; i++)
        h = (h ^ (oric_peek(&g.m, (uint16_t)(ORIC_TEXT_BASE + i)) & 0x7Fu)) * 16777619u;
    return h;
}

/* Type a command and wait until the screen has been still for 20 fields:
 * the row of the bottom Ready, or -1. Both ROMs print a blank line
 * before it. */
static int command(const char *cmd) {
    guest_type(&g, cmd);
    uint32_t last = 0;
    int still = 0;
    for (int f = 0; f < 2000 && still < 20; f++) {
        guest_fields(&g, 1);
        uint32_t h = screen_hash();
        still = h == last ? still + 1 : 0;
        last = h;
    }
    for (int r = ORIC_SCREEN_ROWS - 1; r > 0; r--)
        if (strcmp(row_text(r), "Ready") == 0) return r;
    return -1;
}

/* A program typed at the replay's own rate, each line arriving in one
 * poll as a fast typist's would, through the scroll: with one key of
 * type-ahead, a key that came while BASIC stored a line and scrolled
 * would be lost. Every line must be stored, and run. */
static int program_at_speed(void) {
    fresh();
    for (int n = 1; n <= 30; n++) {
        char line[32];
        snprintf(line, sizeof line, "%d PRINT %d*%d\n", n, n, n);
        for (const char *c = line; *c; c++) {
            picocalc_event_t ev[ORIC_KEY_TEXT_EVENTS];
            unsigned k = keymap_picocalc_text((uint8_t)*c, ev);
            for (unsigned i = 0; i < k; i++) keymatrix_event(&g.k, ev[i].state, ev[i].code);
        }
        for (int f = 0; f < 400 && !keymatrix_idle(&g.k); f++) guest_fields(&g, 1);
    }
    int row = command("RUN\n");
    CHECK(row >= 3 && strcmp(row_text(row - 2), "900") == 0 &&
              strcmp(row_text(row - 3), "841") == 0,
          "the typed program did not run whole: \"%s\" above Ready",
          row >= 2 ? row_text(row - 2) : "");
    /* The listing, in two halves that each fit on the screen. */
    static const struct { const char *cmd; int first, last; } halves[] = {
        { "LIST -15\n", 1, 15 }, { "LIST 16-\n", 16, 30 },
    };
    for (unsigned h = 0; h < 2; h++) {
        row = command(halves[h].cmd);
        for (int n = halves[h].first; n <= halves[h].last; n++) {
            char want[32];
            snprintf(want, sizeof want, "%d PRINT %d*%d", n, n, n);
            int r = row - 2 - (halves[h].last - n);
            CHECK(r > 0 && strcmp(row_text(r), want) == 0, "LIST: \"%s\", want \"%s\"",
                  r > 0 ? row_text(r) : "", want);
        }
    }
    if (test_failures) guest_dump(&g.m, stdout);
    return 0;
}

/* Lines of a program BASIC has stored: its chain of line links from
 * #0501, where both ROMs put the program. */
static int lines_stored(void) {
    int n = 0;
    uint16_t p = 0x0501u;
    while (n < 1000 && (oric_peek(&g.m, p) | oric_peek(&g.m, (uint16_t)(p + 1u)))) {
        p = (uint16_t)(oric_peek(&g.m, p) | oric_peek(&g.m, (uint16_t)(p + 1u)) << 8);
        n++;
    }
    return n;
}

/* 60 lines straight into the matrix, each key held `hold` fields with
 * `gap` up, a SHIFT a field ahead of its key: how many BASIC stored. */
static int program_at(unsigned hold, unsigned gap) {
    fresh();
    for (int n = 1; n <= 60; n++) {
        char line[32];
        snprintf(line, sizeof line, "%d PRINT %d*%d\n", n, n, n);
        for (const char *c = line; *c; c++) {
            uint8_t code = *c == '\n' ? PICOCALC_KEY_ENTER : (uint8_t)*c;
            const keymap_t *e = NULL;
            for (size_t i = 0; i < keymap_picocalc_len && !e; i++)
                if (keymap_picocalc[i].code == code && !(keymap_picocalc[i].flags & KM_ALT))
                    e = &keymap_picocalc[i];
            if (e->flags & KM_SHIFT) {
                oric_key_set(&g.m, OK_ROW_SHIFT_L, OK_COL_MODS, true);
                oric_run_field(&g.m);
            }
            oric_key_set(&g.m, e->row, e->col, true);
            for (unsigned f = 0; f < hold; f++) oric_run_field(&g.m);
            memset(g.m.keys, 0, sizeof g.m.keys);
            for (unsigned f = 0; f < gap; f++) oric_run_field(&g.m);
        }
    }
    for (unsigned f = 0; f < 50; f++) oric_run_field(&g.m);
    return lines_stored();
}

/* Why the replay holds and gaps a field more than the scan needs: BASIC
 * 1.0 drops keys that come while it stores a line and scrolls. */
static int program_margin(void) {
    int bare = program_at(2, 2), ours = program_at(ORIC_KEY_MIN_FIELDS, ORIC_KEY_GAP_FIELDS);
    printf("  %s: 60 lines typed at 2 held and 2 up store %d; at the replay's %u and %u, %d\n",
           rom_label(g.rom), bare, (unsigned)ORIC_KEY_MIN_FIELDS,
           (unsigned)ORIC_KEY_GAP_FIELDS, ours);
    CHECK(ours == 60, "at the replay's rate %d of 60 lines were stored", ours);
    if (g.rom == ROM_BASIC10)
        CHECK(bare < 60, "control: at the scan's bare minimum 1.0 should lose lines");
    return 0;
}

/* Keys typed while a program runs (§16). #02DF holds one key: each
 * overwrites the last, KEY$ takes the last one and empties it, and the
 * way back to Ready empties it too, so nothing reaches the prompt. */
static int type_ahead(void) {
    fresh();
    guest_type(&g, "FOR I=1 TO 3000:NEXT:PRINT \"K\";KEY$;KEY$\n");
    guest_type(&g, "XYZ");
    CHECK(guest_find_row(&g.m, "Ready", input_row + 1) < 0,
          "the loop finished before the keys were typed");
    uint32_t f0 = g.m.fields;
    int row = -1;
    for (int f = 0; f < 1000 && row < 0; f++) {
        guest_fields(&g, 1);
        row = guest_find_row(&g.m, "Ready", input_row + 1);
    }
    CHECK(row > 0, "the loop did not finish");
    guest_fields(&g, 10);
    printf("  %s: XYZ typed %u fields before the loop ends: KEY$ reads \"%s\"",
           rom_label(g.rom), (unsigned)(g.m.fields - f0), row_text(row - 2) + 1);
    printf(", the prompt \"%s\"\n", row_text(row + 1));
    CHECK(strcmp(row_text(row - 2), "KZ") == 0, "KEY$ after XYZ: \"%s\", want \"KZ\"",
          row_text(row - 2));
    CHECK(strcmp(row_text(row + 1), "") == 0, "the prompt kept \"%s\"", row_text(row + 1));
    return 0;
}

int main(int argc, char **argv) {
    const char *dir;
    if (!guest_find_roms(&dir)) {
        printf("SKIP: both BASIC ROMs are needed in %s\n", dir);
        return 77;
    }
    bool print = argc > 1 && strcmp(argv[1], "--print") == 0;

    static const rom_id_t roms[] = { ROM_BASIC10, ROM_BASIC11 };
    int hold[2], gap[2];
    for (unsigned i = 0; i < 2; i++) {
        CHECK(guest_boot(&g, roms[i], ORIC_RAM_48K), "%s did not boot", rom_label(roms[i]));
        oric_copy(&booted, &g.m);
        input_row = guest_find_row(&g.m, "Ready", 0) + 1;
        printf("%s\n", rom_label(roms[i]));
        if (print) {
            run_sweep(true);
            continue;
        }
        if (run_sweep(false) || column_4() || every_entry() || editing_keys() ||
            hold_and_gap(&hold[i], &gap[i]) || program_at_speed() || program_margin() ||
            type_ahead())
            return 1;
    }
    if (print) return 0;
    TEST_DONE();
}
