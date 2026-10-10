/* test_keymap.c — the keymap table and the held-key set (design.md §9).
 * Needs no ROM, so CI runs it; test_keyboard checks the same table
 * against both ROMs. Adapted from pico-ace's (§4.6).
 */

#include <stdio.h>
#include <string.h>

#include "keymatrix.h"
#include "oric.h"
#include "test_util.h"

static oric_t      m;
static keymatrix_t k;

static void fresh(void) {
    oric_config_t cfg;
    oric_config_default(&cfg);
    oric_init(&m, &cfg);
    keymatrix_init(&k);
}

static bool cell_down(uint8_t row, uint8_t col) {
    return (m.keys[row] >> col) & 1u;
}

static bool mod_cell(uint8_t row) { return cell_down(row, OK_COL_MODS); }
static bool shift_l(void) { return mod_cell(OK_ROW_SHIFT_L); }
static bool shift_r(void) { return mod_cell(OK_ROW_SHIFT_R); }
static bool ctrl(void)    { return mod_cell(OK_ROW_CTRL); }

static bool matrix_empty(void) {
    for (unsigned r = 0; r < ORIC_KEY_ROWS; r++)
        if (m.keys[r]) return false;
    return true;
}

static const keymap_t *entry_for(uint8_t code, bool alt) {
    for (size_t i = 0; i < keymap_picocalc_len; i++) {
        const keymap_t *e = &keymap_picocalc[i];
        if (e->code == code && !!(e->flags & KM_ALT) == alt) return e;
    }
    return NULL;
}

static void press(uint8_t code)   { keymatrix_event(&k, KEY_EV_PRESSED, code); }
static void release(uint8_t code) { keymatrix_event(&k, KEY_EV_RELEASED, code); }
static void fields(int n) { for (int i = 0; i < n; i++) keymatrix_field(&k, &m); }

int main(void) {
    /* ---- table invariants (§9.2, §9.3) -------------------------------- */
    for (size_t i = 0; i < keymap_picocalc_len; i++) {
        const keymap_t *e = &keymap_picocalc[i];
        if (!(e->flags & KM_NOCELL)) {
            CHECK(e->row < ORIC_KEY_ROWS && e->col < ORIC_KEY_COLS,
                  "code 0x%02X: cell (%u,%u) off the matrix", e->code, e->row, e->col);
            /* SHIFT and CTRL are flags and modifiers, never an entry's
             * cell; FUNCT is the Tab key's. */
            CHECK(e->col != OK_COL_MODS ||
                      (e->row == OK_ROW_FUNCT &&
                       keymap_picocalc_canonical(e->code) == PICOCALC_KEY_TAB),
                  "code 0x%02X bound to column 4, row %u", e->code, e->row);
        }
        if (e->flags & KM_MENU)
            CHECK(e->row <= KM_PAGE_ABOUT, "code 0x%02X opens page %u", e->code, e->row);
        /* The MCU keeps Alt+, . Space and B for itself, and Alt+I is its
         * Insert (hardware-notes.md §6.3): a binding there never fires. */
        if (e->flags & KM_ALT) {
            CHECK(strchr(",. BI", e->code) == NULL, "Alt+'%c' is consumed by the MCU",
                  e->code);
            CHECK(e->code >= 'A' && e->code <= 'Z',
                  "Alt+0x%02X: Alt letters arrive in capitals", e->code);
        }
        CHECK(e->code != PICOCALC_KEY_ALT && e->code != PICOCALC_KEY_CTRL &&
                  e->code != PICOCALC_KEY_SHIFT_L && e->code != PICOCALC_KEY_SHIFT_R,
              "a modifier is bound as a key");
        /* A shifted entry is a code the PicoCalc sends with Shift, so
         * KM_SHIFT never adds a SHIFT a typist did not hold. */
        if ((e->flags & KM_SHIFT) && !(e->flags & KM_ALT))
            CHECK(keymap_picocalc_canonical(e->code) != e->code,
                  "0x%02X asserts SHIFT, but the PicoCalc sends it unshifted", e->code);
        for (size_t j = i + 1; j < keymap_picocalc_len; j++) {
            const keymap_t *f = &keymap_picocalc[j];
            CHECK(!(e->code == f->code && (e->flags & KM_ALT) == (f->flags & KM_ALT)),
                  "code 0x%02X bound twice", e->code);
        }
    }
    /* Every key the PicoCalc sends only as a Shift chord has an entry:
     * canonicalisation pairs its press with its release, but binds
     * nothing (hardware-notes.md §6.3). */
    {
        static const uint8_t alts[] = {
            PICOCALC_KEY_INSERT, PICOCALC_KEY_BREAK, PICOCALC_KEY_HOME,
            PICOCALC_KEY_END, PICOCALC_KEY_PAGE_UP, PICOCALC_KEY_PAGE_DOWN,
            PICOCALC_KEY_F6, PICOCALC_KEY_F10,
        };
        for (unsigned i = 0; i < sizeof alts; i++)
            CHECK(entry_for(alts[i], false) != NULL, "Shift alternate 0x%02X has no entry",
                  alts[i]);
    }

    /* Every character the PicoCalc types reaches the Oric (EL §7.2), but
     * for the two the Oric has no key for. */
    for (unsigned c = 0x20; c < 0x7F; c++) {
        bool none = c == '`' || c == '~';
        CHECK((entry_for((uint8_t)c, false) != NULL) != none, "'%c': %s", c,
              none ? "the Oric has no key for it" : "no entry");
    }
    /* Each cell's two characters share it, and its plain one is unshifted. */
    for (unsigned c = 0x20; c < 0x7F; c++) {
        const keymap_t *e = entry_for((uint8_t)c, false);
        uint8_t base = keymap_picocalc_canonical((uint8_t)c);
        const keymap_t *b = entry_for(base, false);
        if (!e || !b || base == c) continue;
        CHECK(e->row == b->row && e->col == b->col, "'%c' and '%c' are on different cells",
              c, base);
        CHECK((e->flags & KM_SHIFT) && !(b->flags & KM_SHIFT), "'%c' / '%c' shifts", c, base);
    }
    /* No two different keys share a cell, apart from Backspace and Del
     * (with End, its Shift alternate), which are all DEL. */
    for (size_t i = 0; i < keymap_picocalc_len; i++) {
        const keymap_t *e = &keymap_picocalc[i];
        if (e->flags & (KM_NOCELL | KM_ALT)) continue;
        for (size_t j = i + 1; j < keymap_picocalc_len; j++) {
            const keymap_t *f = &keymap_picocalc[j];
            if (f->flags & (KM_NOCELL | KM_ALT)) continue;
            bool same_key = keymap_picocalc_canonical(e->code) ==
                            keymap_picocalc_canonical(f->code);
            bool dels = (e->code == PICOCALC_KEY_BACKSPACE ||
                         keymap_picocalc_canonical(e->code) == PICOCALC_KEY_DEL) &&
                        (f->code == PICOCALC_KEY_BACKSPACE ||
                         keymap_picocalc_canonical(f->code) == PICOCALC_KEY_DEL);
            if (e->row == f->row && e->col == f->col)
                CHECK(same_key || dels, "0x%02X and 0x%02X share (%u,%u)", e->code, f->code,
                      e->row, e->col);
        }
    }

    /* Canonical identity is idempotent, and maps both halves of a key
     * to one physical key. */
    for (unsigned c = 0; c < 256; c++) {
        uint8_t once = keymap_picocalc_canonical((uint8_t)c);
        CHECK(keymap_picocalc_canonical(once) == once, "canonical(0x%02X) not stable", c);
    }
    CHECK(keymap_picocalc_canonical('A') == 'a', "A is a");
    CHECK(keymap_picocalc_canonical('!') == '1', "! is 1");
    CHECK(keymap_picocalc_canonical(':') == ';', ": is ;");
    CHECK(keymap_picocalc_canonical(0xD2u) == PICOCALC_KEY_TAB, "Home is on Tab");

    /* ---- text as PicoCalc events: the UART's path (§9.1) ------------- */
    {
        picocalc_event_t ev[ORIC_KEY_TEXT_EVENTS];
        CHECK(keymap_picocalc_text('a', ev) == 2 && ev[0].state == KEY_EV_PRESSED &&
                  ev[0].code == 'a' && ev[1].state == KEY_EV_RELEASED && ev[1].code == 'a',
              "a is a press and a release");
        CHECK(keymap_picocalc_text(PICOCALC_KEY_F1, ev) == 2 && ev[0].code == PICOCALC_KEY_F1,
              "F1 is itself");
        CHECK(keymap_picocalc_text(PICOCALC_KEY_F6, ev) == 4 &&
              ev[0].code == PICOCALC_KEY_SHIFT_L && ev[1].code == PICOCALC_KEY_F6 &&
              ev[2].code == PICOCALC_KEY_F6 && ev[3].code == PICOCALC_KEY_SHIFT_L,
              "F6 is Shift+F1's code inside Shift");
        CHECK(keymap_picocalc_text(0x8Au, ev) == 0 && keymap_picocalc_text(0x80u, ev) == 0,
              "no key sends 0x8A or 0x80");
        CHECK(keymap_picocalc_text('!', ev) == 4 && ev[0].code == PICOCALC_KEY_SHIFT_L &&
                  ev[1].code == '!' && ev[2].code == '!' && ev[2].state == KEY_EV_RELEASED &&
                  ev[3].code == PICOCALC_KEY_SHIFT_L && ev[3].state == KEY_EV_RELEASED,
              "! is inside Shift, as the PicoCalc types it");
        CHECK(keymap_picocalc_text('A', ev) == 4 && ev[0].code == PICOCALC_KEY_SHIFT_L,
              "A is inside Shift");
        CHECK(keymap_picocalc_text('\r', ev) == 2 && ev[0].code == PICOCALC_KEY_ENTER,
              "CR is Enter");
        CHECK(keymap_picocalc_text('\n', ev) == 2 && ev[0].code == PICOCALC_KEY_ENTER,
              "LF is Enter");
        CHECK(keymap_picocalc_text(0x7Fu, ev) == 2 && ev[0].code == PICOCALC_KEY_BACKSPACE,
              "DEL is Backspace");
        CHECK(keymap_picocalc_text(0x1Bu, ev) == 2 && ev[0].code == PICOCALC_KEY_ESC,
              "ESC is Esc");
        CHECK(keymap_picocalc_text(0x09u, ev) == 2 && ev[0].code == PICOCALC_KEY_TAB,
              "TAB is Tab");
        CHECK(keymap_picocalc_text(0x14u, ev) == 4 && ev[0].code == PICOCALC_KEY_CTRL &&
                  ev[1].code == 't' && ev[3].code == PICOCALC_KEY_CTRL,
              "^T is Ctrl+t");
        CHECK(keymap_picocalc_text(0x00u, ev) == 0 && keymap_picocalc_text(0x1Cu, ev) == 0,
              "bytes no key sends give no events");
        /* Every printable byte's events reach a binding in the table,
         * but the two the Oric has no key for. */
        for (unsigned c = 0x20; c < 0x7F; c++) {
            if (c == '`' || c == '~') continue;
            unsigned n = keymap_picocalc_text((uint8_t)c, ev);
            CHECK(n >= 2 && entry_for(ev[n == 4 ? 1 : 0].code, false),
                  "'%c' sends a code with no binding", c);
        }
    }

    /* ---- the held set: pacing (§9.1) ---------------------------------- */
    {
        /* Press and release in one poll: the key is down for exactly
         * ORIC_KEY_MIN_FIELDS fields. A is (6,5). */
        fresh();
        press('a');
        release('a');
        unsigned down = 0;
        for (int f = 0; f < 20; f++) {
            fields(1);
            if (cell_down(6, 5)) down++;
        }
        CHECK(down == ORIC_KEY_MIN_FIELDS, "held %u fields, want %u", down,
              (unsigned)ORIC_KEY_MIN_FIELDS);

        /* Two keys in one poll are serialised with the gap between. B is
         * (2,2). */
        fresh();
        press('a');
        release('a');
        press('b');
        release('b');
        int a_last = -1, b_first = -1;
        for (int f = 0; f < 40; f++) {
            fields(1);
            CHECK(!(cell_down(6, 5) && cell_down(2, 2)), "a and b overlap at field %d", f);
            if (cell_down(6, 5)) a_last = f;
            if (cell_down(2, 2) && b_first < 0) b_first = f;
        }
        CHECK(b_first - a_last - 1 == (int)ORIC_KEY_GAP_FIELDS,
              "gap of %d fields, want %u", b_first - a_last - 1,
              (unsigned)ORIC_KEY_GAP_FIELDS);

        /* A key held by a human is held here: auto-repeat presses do not
         * release it, and it goes up only when released. J is (1,0). */
        fresh();
        press('j');
        for (int f = 0; f < 30; f++) {
            if (f % 6 == 0) press('j');
            fields(1);
            CHECK(cell_down(1, 0), "j let go at field %d while held", f);
        }
        release('j');
        fields(1);
        CHECK(!cell_down(1, 0), "j should be up after its release");
        CHECK(k.n == 0, "nothing should be held");
    }

    /* ---- the host's Shifts and Ctrl, and column 4 (§2.4, §9.2) -------- */
    {
        /* Each Shift alone is the Oric's SHIFT on its side, which games
         * read on their own; Ctrl alone is CTRL. */
        static const struct { uint8_t code, row; } mods[] = {
            { PICOCALC_KEY_SHIFT_L, OK_ROW_SHIFT_L },
            { PICOCALC_KEY_SHIFT_R, OK_ROW_SHIFT_R },
            { PICOCALC_KEY_CTRL,    OK_ROW_CTRL },
        };
        for (unsigned i = 0; i < 3; i++) {
            fresh();
            press(mods[i].code);
            fields(1);
            CHECK(mod_cell(mods[i].row) && m.keys[mods[i].row] == 1u << OK_COL_MODS,
                  "0x%02X alone is row %u of column 4, alone", mods[i].code, mods[i].row);
            unsigned others = 0;
            for (unsigned r = 0; r < ORIC_KEY_ROWS; r++) others += r != mods[i].row && m.keys[r];
            CHECK(others == 0, "0x%02X reached another row", mods[i].code);
            release(mods[i].code);
            fields(ORIC_KEY_MIN_FIELDS);
            CHECK(matrix_empty(), "and lets it go");
        }

        /* A tap of Shift or Ctrl inside one poll is held as long as a
         * key, not applied and undone before the matrix is driven. */
        for (unsigned i = 0; i < 3; i++) {
            fresh();
            press(mods[i].code);
            release(mods[i].code);
            unsigned down = 0;
            for (int f = 0; f < 20; f++) {
                fields(1);
                if (mod_cell(mods[i].row)) down++;
            }
            CHECK(down == ORIC_KEY_MIN_FIELDS, "a tap of 0x%02X held %u fields, want %u",
                  mods[i].code, down, (unsigned)ORIC_KEY_MIN_FIELDS);
            CHECK(keymatrix_idle(&k) && matrix_empty() && !k.shift && !k.ctrl,
                  "0x%02X left down after its tap", mods[i].code);
        }

        /* A modifier's held events while down do not restart its count. */
        fresh();
        press(PICOCALC_KEY_SHIFT_L);
        fields(ORIC_KEY_MIN_FIELDS);
        keymatrix_event(&k, KEY_EV_HELD, PICOCALC_KEY_SHIFT_L);
        release(PICOCALC_KEY_SHIFT_L);
        fields(1);
        CHECK(!shift_l(), "Shift held its minimum already, and goes up at once");

        /* A modifier goes a field ahead of the key pressed with it, in
         * the same poll (EL §7.1): the ROM takes a key on the first scan
         * that sees it. */
        static const struct { uint8_t mod, key, row; } chords[] = {
            { PICOCALC_KEY_SHIFT_R, 'A', OK_ROW_SHIFT_R },
            { PICOCALC_KEY_SHIFT_L, '!', OK_ROW_SHIFT_L },
            { PICOCALC_KEY_CTRL,    't', OK_ROW_CTRL },
        };
        for (unsigned i = 0; i < 3; i++) {
            fresh();
            press(chords[i].mod);
            press(chords[i].key);
            const keymap_t *e = entry_for(chords[i].key, false);
            fields(1);
            CHECK(mod_cell(chords[i].row) && !cell_down(e->row, e->col),
                  "0x%02X+'%c': the modifier first, alone", chords[i].mod, chords[i].key);
            fields(1);
            CHECK(mod_cell(chords[i].row) && cell_down(e->row, e->col),
                  "0x%02X+'%c': then both", chords[i].mod, chords[i].key);
        }

        /* 'A' with the right Shift is right SHIFT and A: the entry's own
         * SHIFT is the left one, added. */
        fresh();
        press(PICOCALC_KEY_SHIFT_R);
        press('A');
        fields(2);
        CHECK(cell_down(6, 5) && shift_r() && shift_l(), "A is SHIFT+A");

        /* A KM_SHIFT entry with no Shift held, as a release of Shift
         * before the poll leaves it: SHIFT a field ahead of its cell,
         * and the cell still held its minimum. */
        fresh();
        press('!');
        release('!');
        CHECK(entry_for('!', false)->flags & KM_SHIFT, "! asserts SHIFT");
        fields(1);
        CHECK(shift_l() && !cell_down(0, 5), "!: SHIFT first, alone");
        unsigned down = 0;
        for (int f = 0; f < 20; f++) {
            if (cell_down(0, 5)) {
                down++;
                CHECK(shift_l(), "! without SHIFT at field %d", f);
            }
            fields(1);
        }
        CHECK(down == ORIC_KEY_MIN_FIELDS, "! held %u fields, want %u", down,
              (unsigned)ORIC_KEY_MIN_FIELDS);
        CHECK(matrix_empty() && keymatrix_idle(&k), "! left something down");

        /* Releasing Shift first retranslates the release: press '"',
         * release '\'' (hardware-notes.md §6.2). It still lets go. The
         * quote is (3,7). */
        fresh();
        press(PICOCALC_KEY_SHIFT_L);
        press('"');
        release(PICOCALC_KEY_SHIFT_L);
        fields(2);
        CHECK(cell_down(3, 7) && shift_l(), "\" is SHIFT+'");
        release('\'');
        fields(10);
        CHECK(k.n == 0 && matrix_empty(), "a release under the other translation lets go");

        /* Both Shifts: letting one go keeps the other. */
        fresh();
        press(PICOCALC_KEY_SHIFT_L);
        press(PICOCALC_KEY_SHIFT_R);
        release(PICOCALC_KEY_SHIFT_L);
        fields(ORIC_KEY_MIN_FIELDS + 1);
        CHECK(shift_r() && !shift_l(), "the right Shift is still down");

        /* A letter held first, then Shift: SHIFT joins it. Z is (2,5). */
        fresh();
        press('z');
        fields(1);
        CHECK(cell_down(2, 5) && !shift_l(), "z is Z unshifted");
        press(PICOCALC_KEY_SHIFT_R);
        press('Z');   /* the MCU's repeat */
        fields(1);
        CHECK(cell_down(2, 5) && shift_r(), "Z held with Shift");

        /* Ctrl with a letter: CTRL and the letter, and Ctrl let go first
         * leaves the letter. I is (5,1). */
        fresh();
        press(PICOCALC_KEY_CTRL);
        press('i');
        fields(2);
        CHECK(ctrl() && cell_down(5, 1), "Ctrl+i is CTRL+I");
        release(PICOCALC_KEY_CTRL);
        fields(ORIC_KEY_MIN_FIELDS);
        CHECK(!ctrl() && cell_down(5, 1), "Ctrl let go, i still held");

        /* Tab is FUNCT, a held key. */
        fresh();
        press(PICOCALC_KEY_TAB);
        fields(1);
        CHECK(mod_cell(OK_ROW_FUNCT) && !shift_l() && !shift_r() && !ctrl(),
              "Tab is FUNCT alone");
        fields(30);
        CHECK(mod_cell(OK_ROW_FUNCT), "FUNCT held while Tab is");
        release(PICOCALC_KEY_TAB);
        fields(1);
        CHECK(matrix_empty(), "and let go with it");
    }

    /* ---- the Oric's own keys, the Alt layer and the requests ---------- */
    {
        /* The arrows are the Oric's, plain keys on row 4 (§2.4). */
        static const struct { uint8_t code, col; } arrows[] = {
            { PICOCALC_KEY_UP, 3 }, { PICOCALC_KEY_LEFT, 5 },
            { PICOCALC_KEY_DOWN, 6 }, { PICOCALC_KEY_RIGHT, 7 },
        };
        for (unsigned i = 0; i < 4; i++) {
            fresh();
            press(arrows[i].code);
            fields(1);
            CHECK(cell_down(4, arrows[i].col) && !shift_l(), "arrow 0x%02X is (4,%u)",
                  arrows[i].code, arrows[i].col);
        }
        static const struct { uint8_t code, row, col; } own[] = {
            { PICOCALC_KEY_ENTER, 7, 5 }, { PICOCALC_KEY_BACKSPACE, 5, 5 },
            { PICOCALC_KEY_DEL, 5, 5 },   { PICOCALC_KEY_ESC, 1, 5 },
            { ' ', 4, 0 },
        };
        for (unsigned i = 0; i < sizeof own / sizeof own[0]; i++) {
            fresh();
            press(own[i].code);
            fields(1);
            CHECK(cell_down(own[i].row, own[i].col) && !shift_l(), "0x%02X is (%u,%u)",
                  own[i].code, own[i].row, own[i].col);
        }

        /* The binding is fixed at press: Alt+K asks for the reset button,
         * and its release as 'k' after Alt is up presses nothing. */
        fresh();
        press(PICOCALC_KEY_ALT);
        press('K');
        fields(1);
        CHECK(k.reset_request && matrix_empty(), "Alt+K asks for the reset button");
        release(PICOCALC_KEY_ALT);
        fields(1);
        release('k');
        fields(10);
        CHECK(k.n == 0 && matrix_empty(), "K's release under the other translation");

        /* Insert is Alt+I as well as Shift+Enter (hardware-notes.md
         * §6.3). Let Alt go first and I's release arrives as 'i': it must
         * still close the press, or the next Enter is taken for a repeat. */
        fresh();
        press(PICOCALC_KEY_ALT);
        press(PICOCALC_KEY_INSERT);
        release(PICOCALC_KEY_ALT);
        release('i');
        fields(10);
        CHECK(k.n_open == 0 && keymatrix_idle(&k), "Alt+I left %u press(es) open", k.n_open);
        press(PICOCALC_KEY_ENTER);
        fields(1);
        CHECK(cell_down(7, 5), "Enter after Alt+I is not taken for a repeat");

        /* The sequence pico-ace's device sent (its out/m6-soak.log): Alt
         * let go while I was down, then the MCU's auto-repeat
         * retranslated as presses of 'i', then 'i' released. */
        fresh();
        keymatrix_event(&k, KEY_EV_PRESSED, PICOCALC_KEY_ALT);
        keymatrix_event(&k, KEY_EV_HELD, PICOCALC_KEY_ALT);
        press(PICOCALC_KEY_INSERT);
        keymatrix_event(&k, KEY_EV_HELD, PICOCALC_KEY_ALT);
        release(PICOCALC_KEY_ALT);
        for (int i = 0; i < 4; i++) press('i');
        release('i');
        fields(10);
        CHECK(k.n_open == 0 && keymatrix_idle(&k) && matrix_empty(),
              "the captured Alt+I left %u press(es) open", k.n_open);
        CHECK(k.q_len == 0, "the repeats were not absorbed");

        /* Shift+Enter is Insert too: SHIFT+RETURN, as the Oric's typist
         * would press it, and its release under the other translation,
         * Enter, closes it. */
        fresh();
        press(PICOCALC_KEY_SHIFT_L);
        press(PICOCALC_KEY_INSERT);
        fields(2);
        CHECK(cell_down(7, 5) && shift_l(), "Shift+Enter is SHIFT+RETURN");
        release(PICOCALC_KEY_SHIFT_L);
        release(PICOCALC_KEY_ENTER);
        fields(10);
        CHECK(k.n_open == 0 && keymatrix_idle(&k) && matrix_empty(),
              "Shift+Enter left %u press(es) open", k.n_open);

        /* Alt+I is Insert, and reaches nothing: the Alt layer has no I. */
        fresh();
        press(PICOCALC_KEY_ALT);
        press(PICOCALC_KEY_INSERT);
        fields(2);
        CHECK(matrix_empty() && k.n == 0, "Alt+I reached the matrix");

        /* The keys the PicoCalc sends only with Shift are their base
         * key's cell with SHIFT. */
        static const struct { uint8_t code, row, col; } shifted[] = {
            { PICOCALC_KEY_INSERT,    7, 5 },
            { PICOCALC_KEY_BREAK,     1, 5 },
            { PICOCALC_KEY_END,       5, 5 },
            { PICOCALC_KEY_PAGE_UP,   4, 3 },
            { PICOCALC_KEY_PAGE_DOWN, 4, 6 },
            { PICOCALC_KEY_HOME,      OK_ROW_FUNCT, OK_COL_MODS },
        };
        for (unsigned i = 0; i < sizeof shifted / sizeof shifted[0]; i++) {
            fresh();
            press(PICOCALC_KEY_SHIFT_R);
            press(shifted[i].code);
            fields(2);
            CHECK(cell_down(shifted[i].row, shifted[i].col) && shift_r(),
                  "Shift+0x%02X is SHIFT+(%u,%u)", shifted[i].code, shifted[i].row,
                  shifted[i].col);
        }

        /* An Alt chord with no binding reaches nothing. */
        fresh();
        press(PICOCALC_KEY_ALT);
        press('Q');
        fields(1);
        CHECK(k.n == 0 && matrix_empty(), "Alt+Q should do nothing");

        /* Alt+M, Alt+H, Alt+P and Alt+K ask, and touch nothing in the
         * machine (§12). */
        static const struct { uint8_t code; bool *flag; const char *what; } asks[] = {
            { 'M', &k.menu_request,  "menu"  },
            { 'H', &k.menu_request,  "keys page" },
            { 'P', &k.pause_request, "pause" },
            { 'K', &k.reset_request, "reset button" },
        };
        for (unsigned i = 0; i < 4; i++) {
            fresh();
            press(PICOCALC_KEY_ALT);
            press(asks[i].code);
            fields(1);
            CHECK(*asks[i].flag, "Alt+%c requests the %s", asks[i].code, asks[i].what);
            CHECK(k.menu_request + k.pause_request + k.reset_request + k.shot_request == 1,
                  "Alt+%c requests only the %s", asks[i].code, asks[i].what);
            CHECK(matrix_empty(), "Alt+%c reached the matrix", asks[i].code);
        }
        fresh();
        press(PICOCALC_KEY_ALT);
        press('M');
        fields(1);
        CHECK(k.menu_page == KM_PAGE_MAIN, "Alt+M opens the main page");
        fresh();
        press(PICOCALC_KEY_ALT);
        press('H');
        fields(1);
        CHECK(k.menu_page == KM_PAGE_HELP, "Alt+H opens the keys page");

        /* F1-F5 open §12's pages, with or without Alt held; F6, Shift+F1,
         * is a screenshot; F10 is About. */
        static const uint8_t pages[5] = {
            KM_PAGE_TAPE, KM_PAGE_DISC, KM_PAGE_SNAPSHOT, KM_PAGE_SETUP, KM_PAGE_MACHINE,
        };
        for (unsigned alt = 0; alt < 2; alt++) {
            for (unsigned f = 0; f < 5; f++) {
                fresh();
                if (alt) press(PICOCALC_KEY_ALT);
                press((uint8_t)(PICOCALC_KEY_F1 + f));
                fields(1);
                CHECK(k.menu_request && k.menu_page == pages[f], "%sF%u: page %u, want %u",
                      alt ? "Alt+" : "", f + 1, k.menu_page, pages[f]);
                CHECK(matrix_empty(), "F%u reached the matrix", f + 1);
            }
        }
        for (unsigned alt = 0; alt < 2; alt++) {
            fresh();
            if (alt) press(PICOCALC_KEY_ALT);
            press(PICOCALC_KEY_SHIFT_L);
            press(PICOCALC_KEY_F6);
            fields(2);
            CHECK(k.shot_request, "%sF6 requests a screenshot", alt ? "Alt+" : "");
            CHECK(!k.menu_request && !k.pause_request && !k.reset_request,
                  "%sF6 requests only a screenshot", alt ? "Alt+" : "");
            /* The host's Shift is the Oric's SHIFT, held as it is (§9.2). */
            m.keys[OK_ROW_SHIFT_L] &= (uint8_t)~(1u << OK_COL_MODS);
            CHECK(matrix_empty(), "%sF6 reached the matrix", alt ? "Alt+" : "");
        }

        /* F6 is Shift+F1, so a Shift let go first turns its release into
         * F1's (hardware-notes.md §6.2). That release must let go of F6,
         * or the next F6 is taken for its auto-repeat and asks nothing. */
        CHECK(keymap_picocalc_canonical(PICOCALC_KEY_F6) == PICOCALC_KEY_F1 &&
              keymap_picocalc_canonical(PICOCALC_KEY_F10) == PICOCALC_KEY_F1 + 4u,
              "F6 and F10 are F1's and F5's keys");
        fresh();
        press(PICOCALC_KEY_SHIFT_L);
        press(PICOCALC_KEY_F6);
        fields(ORIC_KEY_MIN_FIELDS + 1);
        CHECK(k.shot_request, "F6 requests a screenshot");
        k.shot_request = false;
        release(PICOCALC_KEY_SHIFT_L);
        release(PICOCALC_KEY_F1);
        fields(ORIC_KEY_GAP_FIELDS + 2);
        CHECK(k.n == 0, "F1's release left %u key(s) held", k.n);
        press(PICOCALC_KEY_SHIFT_L);
        press(PICOCALC_KEY_F6);
        fields(2);
        CHECK(k.shot_request, "a second F6 requests another");
        fresh();
        press(PICOCALC_KEY_SHIFT_L);
        press(PICOCALC_KEY_F10);
        fields(2);
        CHECK(k.menu_request && k.menu_page == KM_PAGE_ABOUT, "F10 opens About");
    }

    /* ---- the queue is bounded, and never loses a release -------------- */
    {
        /* Auto-repeat of a key already down is absorbed at once. */
        fresh();
        for (unsigned i = 0; i < ORIC_KEY_EVENT_QUEUE + 5u; i++) press('x');
        CHECK(k.q_len == 1 && k.dropped == 0, "repeats: q_len %u, dropped %u", k.q_len,
              (unsigned)k.dropped);
        release('x');

        /* A burst far faster than the replay: presses are refused once
         * the queue is short of room, but every key that went down comes
         * back up, so nothing is left held. */
        fresh();
        static const char burst[] = "the quick brown fox jumps over the lazy dog 0123456789";
        for (int rep = 0; rep < 3; rep++) {
            for (const char *c = burst; *c; c++) {
                press((uint8_t)*c);
                release((uint8_t)*c);
            }
        }
        CHECK(k.dropped > 0, "the burst should have overflowed");
        CHECK(k.q_len + k.n_open <= ORIC_KEY_EVENT_QUEUE, "a release has no room");
        for (int f = 0; f < 2000 && !keymatrix_idle(&k); f++) fields(1);
        CHECK(keymatrix_idle(&k) && k.n_open == 0, "stuck keys: %u held, %u open", k.n,
              k.n_open);
        CHECK(matrix_empty(), "the matrix is left with keys down");

        /* The same, with Shift, Ctrl and Alt in the burst: a modifier's
         * release is kept like any other. */
        fresh();
        for (int i = 0; i < 100; i++) {
            press(PICOCALC_KEY_ALT);
            press(PICOCALC_KEY_SHIFT_L);
            press(PICOCALC_KEY_CTRL);
            press('P');
            release('P');
            release(PICOCALC_KEY_CTRL);
            release(PICOCALC_KEY_SHIFT_L);
            release(PICOCALC_KEY_ALT);
        }
        for (int f = 0; f < 4000 && !keymatrix_idle(&k); f++) fields(1);
        CHECK(!k.alt && !k.shift && !k.ctrl && k.n == 0 && k.n_open == 0 && matrix_empty(),
              "a modifier left down after a burst");
    }

    /* ---- game layouts: the built-ins (§9.4) --------------------------- */
    CHECK(keylayout_builtin_len == 3 && strcmp(keylayout_builtin[0].name, "ZX") == 0 &&
              strcmp(keylayout_builtin[1].name, "AZ") == 0 &&
              strcmp(keylayout_builtin[2].name, "QAOP") == 0,
          "ZX, AZ and QAOP are the built-in layouts");
    for (size_t li = 0; li < keylayout_builtin_len; li++) {
        const keylayout_t *l = &keylayout_builtin[li];
        CHECK(l->n <= ORIC_KEYMAP_BINDINGS && l->n_files == 0,
              "%s: over config.h's capacities, or names a game", l->name);
        for (unsigned i = 0; i < l->n; i++) {
            const keymap_t *e = &l->bind[i];
            CHECK(keymap_picocalc_canonical(e->code) == e->code,
                  "%s: 0x%02X is not a canonical code, so it would never match", l->name,
                  e->code);
            CHECK(e->flags == 0, "%s: 0x%02X carries flags", l->name, e->code);
            CHECK(e->row < ORIC_KEY_ROWS && e->col < ORIC_KEY_COLS, "%s: off the matrix",
                  l->name);
            for (unsigned j = i + 1; j < l->n; j++) {
                CHECK(l->bind[j].code != e->code, "%s: 0x%02X bound twice", l->name, e->code);
            }
        }
    }

    /* ---- game layouts: the overlay in the held set -------------------- */
    {
        const keylayout_t *zx = &keylayout_builtin[0];

        /* Left is Z: the game sees the cell. The standard map's Left is
         * the Oric's left arrow, the control. */
        fresh();
        keymatrix_set_layout(&k, zx);
        press(PICOCALC_KEY_LEFT);
        fields(1);
        CHECK(cell_down(2, 5) && !cell_down(4, 5) && !shift_l(), "Left under ZX is Z");
        fresh();
        press(PICOCALC_KEY_LEFT);
        fields(1);
        CHECK(cell_down(4, 5) && !cell_down(2, 5), "Left under the standard map is the arrow");

        /* Moving and firing: both cells, and each lets go on its own
         * release. */
        fresh();
        keymatrix_set_layout(&k, zx);
        press(PICOCALC_KEY_RIGHT);
        press(' ');
        fields(1);
        CHECK(cell_down(0, 6) && cell_down(4, 0) && !shift_l(),
              "Right and Space held: X and SPACE together");
        fields(10);
        release(PICOCALC_KEY_RIGHT);
        fields(1);
        CHECK(!cell_down(0, 6) && cell_down(4, 0), "Right let go, Space still held");
        release(' ');
        fields(1);
        CHECK(matrix_empty() && k.n == 0, "both let go");

        /* With Shift, Down arrives as Page Down, and is the same key; a
         * layout's cell takes the host's Shift as it finds it, so under
         * ZX it is SHIFT and /, which types ?. */
        fresh();
        keymatrix_set_layout(&k, zx);
        press(PICOCALC_KEY_SHIFT_L);
        press(PICOCALC_KEY_PAGE_DOWN);
        fields(2);
        CHECK(cell_down(7, 3) && shift_l() && !cell_down(4, 6), "Shift+Down under ZX is ?");

        /* An unmentioned key keeps its standard binding. */
        fresh();
        keymatrix_set_layout(&k, zx);
        press('r');
        fields(1);
        CHECK(cell_down(1, 2) && !shift_l(), "r keeps R");

        /* The Alt layer and the F-keys are never overlaid. */
        static keylayout_t greedy;
        unsigned bad = 0;
        const char *gr = "m = Q\nf = A\n";
        CHECK(keylayout_parse(&greedy, "GREEDY", gr, strlen(gr), &bad) == KL_OK,
              "greedy parses");
        fresh();
        keymatrix_set_layout(&k, &greedy);
        press(PICOCALC_KEY_ALT);
        press('M');
        fields(1);
        CHECK(k.menu_request && !cell_down(1, 6), "Alt+M is the menu under any layout");
        fresh();
        keymatrix_set_layout(&k, &greedy);
        press(PICOCALC_KEY_F1 + 2);
        fields(1);
        CHECK(k.menu_request && k.menu_page == KM_PAGE_SNAPSHOT,
              "F3 is the menu under any layout");
        fresh();
        keymatrix_set_layout(&k, &greedy);
        press('M');   /* Shift+m */
        fields(1);
        CHECK(cell_down(1, 6) && !cell_down(2, 0) && !k.menu_request,
              "a layout binds the key, shifted or not");

        /* A key keeps the binding it went down with: its release undoes
         * that even when the layout changed in between. */
        fresh();
        keymatrix_set_layout(&k, zx);
        press(PICOCALC_KEY_RIGHT);
        fields(1);
        keymatrix_set_layout(&k, NULL);
        fields(1);
        CHECK(cell_down(0, 6) && !cell_down(4, 7), "Right held keeps X across a change");
        release(PICOCALC_KEY_RIGHT);
        fields(10);
        CHECK(matrix_empty() && k.n == 0, "and lets go of it");
        press(PICOCALC_KEY_RIGHT);
        fields(3);
        CHECK(cell_down(4, 7) && !cell_down(0, 6), "the next press takes the standard map");

        /* The pacing and bounds of the held set hold with a layout. */
        fresh();
        keymatrix_set_layout(&k, zx);
        press(PICOCALC_KEY_LEFT);
        release(PICOCALC_KEY_LEFT);
        unsigned down = 0;
        for (int f = 0; f < 20; f++) {
            fields(1);
            if (cell_down(2, 5)) down++;
        }
        CHECK(down == ORIC_KEY_MIN_FIELDS, "a tap of Left is %u fields of Z, want %u", down,
              (unsigned)ORIC_KEY_MIN_FIELDS);
        fresh();
        keymatrix_set_layout(&k, zx);
        for (int rep = 0; rep < 60; rep++) {
            press(PICOCALC_KEY_LEFT);
            press(' ');
            release(PICOCALC_KEY_LEFT);
            press('z');
            release(' ');
            release('z');
        }
        for (int f = 0; f < 4000 && !keymatrix_idle(&k); f++) fields(1);
        CHECK(keymatrix_idle(&k) && k.n_open == 0 && matrix_empty(),
              "stuck keys under a layout: %u held, %u open", k.n, k.n_open);
    }

    /* ---- game layouts: the .map parser -------------------------------- */
    {
        static keylayout_t l;
        unsigned line = 99;

        /* The built-in ZX, written as a file (§9.4). */
        const char *ex = "# ZX: Centipede's keys on the PicoCalc's arrows\n"
                         "name  = ZX\n"
                         "left  = Z\n"
                         "right = X\n"
                         "up    = '\n"
                         "down  = /\n";
        CHECK(keylayout_parse(&l, "mine", ex, strlen(ex), &line) == KL_OK && line == 0,
              "the example parses");
        const keylayout_t *b = &keylayout_builtin[0];
        CHECK(strcmp(l.name, b->name) == 0 && l.n == b->n && l.n_files == b->n_files,
              "the example is the built-in layout");
        CHECK(memcmp(l.bind, b->bind, sizeof(keymap_t) * b->n) == 0, "same bindings");
        CHECK(!keylayout_for_file(&l, "/oric/tapes/ZX.tap") && !keylayout_for_file(&l, ""),
              "no tapes line, no file selects it");

        /* A card file may name its own files, tapes and discs in one
         * list. */
        const char *tp = "left = O\ntapes = Centipede, ROCKET\ndiscs = sedoric3\n";
        CHECK(keylayout_parse(&l, "mine", tp, strlen(tp), &line) == KL_OK && l.n_files == 3,
              "tapes and discs lines parse");
        CHECK(keylayout_for_file(&l, "/oric/tapes/CENTIPEDE.tap") &&
                  keylayout_for_file(&l, "/oric/tapes/rocket.TAP") &&
                  keylayout_for_file(&l, "/oric/discs/SEDORIC3.dsk") &&
                  keylayout_for_file(&l, "centipede"),
              "the files it names select it, ignoring case and the extension");
        CHECK(!keylayout_for_file(&l, "/oric/tapes/CENTIPED.tap") &&
                  !keylayout_for_file(&l, "/oric/tapes/ROCKETS.tap") &&
                  !keylayout_for_file(&l, "/oric/tapes/.tap") && !keylayout_for_file(&l, ""),
              "nothing else does");

        /* '#' is a comment, unless it is the key being bound. */
        const char *hash = "# a comment = not a binding\n#=SPACE\n  # indented comment\n";
        CHECK(keylayout_parse(&l, "hash", hash, strlen(hash), &line) == KL_OK && l.n == 1,
              "# binds once, line %u, %u binding(s)", line, l.n);
        CHECK(l.bind[0].code == keymap_picocalc_canonical('#') && l.bind[0].row == 4 &&
                  l.bind[0].col == 0,
              "# = SPACE binds the # key to SPACE");

        /* CRLF, blank lines, no final newline, the file's own name, and
         * column 4's keys as targets. */
        const char *crlf = "\r\n  A = SPACE\r\n\r\n\tSPACE=z\r\n= = lshift\r\n; = Ctrl\r\n"
                           "tab = rshift\r\nenter = funct";
        CHECK(keylayout_parse(&l, "fire", crlf, strlen(crlf), &line) == KL_OK,
              "CRLF parses, line %u", line);
        CHECK(strcmp(l.name, "FIRE") == 0 && l.n == 6, "name from the file name, 6 bindings");
        CHECK(l.bind[0].code == 'a' && l.bind[0].row == 4 && l.bind[0].col == 0 &&
                  l.bind[0].flags == 0, "A = SPACE");
        CHECK(l.bind[1].code == ' ' && l.bind[1].row == 2 && l.bind[1].col == 5, "space = Z");
        CHECK(l.bind[2].code == '=' && l.bind[2].row == OK_ROW_SHIFT_L &&
                  l.bind[2].col == OK_COL_MODS && l.bind[2].flags == 0,
              "'=' binds the left SHIFT, as a cell");
        CHECK(l.bind[3].code == ';' && l.bind[3].row == OK_ROW_CTRL &&
                  l.bind[3].col == OK_COL_MODS, "; = CTRL");
        CHECK(l.bind[4].row == OK_ROW_SHIFT_R && l.bind[5].row == OK_ROW_FUNCT &&
                  l.bind[5].col == OK_COL_MODS, "Tab = right SHIFT, Enter = FUNCT");

        /* A layout's SHIFT is a cell held like a key: a game reading the
         * right SHIFT alone sees it, and no other cell. */
        fresh();
        keymatrix_set_layout(&k, &l);
        press(PICOCALC_KEY_TAB);
        fields(1);
        CHECK(shift_r() && !shift_l() && !cell_down(OK_ROW_FUNCT, OK_COL_MODS),
              "Tab under the layout is the right SHIFT alone");

        /* Every failure names its line, and nothing is guessed. */
        static const struct { const char *text; keylayout_status_t st; unsigned line; } bad[] = {
            { "left = Z\nright\n",               KL_SYNTAX,     2 },
            { "left =\n",                        KL_SYNTAX,     1 },
            { "left up = Z\n",                   KL_SYNTAX,     1 },
            { "\n\nf1 = Z\n",                    KL_BAD_KEY,    3 },
            { "left = BREAK\n",                  KL_BAD_TARGET, 1 },
            { "left = ?\n",                      KL_BAD_TARGET, 1 },
            { "left = Z X\n",                    KL_BAD_TARGET, 1 },
            { "left = Z\nLEFT = X\n",            KL_DUPLICATE,  2 },
            { "a = Z\nA = X\n",                  KL_DUPLICATE,  2 },
            { "name = A NAME FAR TOO LONG\n",    KL_TOO_LONG,   1 },
            { "tapes = A B C\ndiscs = D E\n",    KL_TOO_MANY,   2 },
            { "tapes = SEVENTEEN_LETTERS\n",     KL_TOO_LONG,   1 },
        };
        for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
            keylayout_status_t st = keylayout_parse(&l, "x", bad[i].text, strlen(bad[i].text),
                                                    &line);
            CHECK(st == bad[i].st && line == bad[i].line,
                  "\"%s\": got %s at line %u, want %s at line %u", bad[i].text,
                  keylayout_status_str(st), line, keylayout_status_str(bad[i].st),
                  bad[i].line);
        }

        /* Every built-in binding, written back as the menu shows it,
         * parses to itself. */
        for (size_t li = 0; li < keylayout_builtin_len; li++) {
            const keylayout_t *bl = &keylayout_builtin[li];
            static char text[512];
            size_t at = 0;
            for (unsigned i = 0; i < bl->n; i++) {
                char one[24];
                keymap_binding_str(&bl->bind[i], one, sizeof one);
                at += (size_t)snprintf(text + at, sizeof text - at, "%s\n", one);
            }
            CHECK(keylayout_parse(&l, bl->name, text, at, &line) == KL_OK && l.n == bl->n &&
                      memcmp(l.bind, bl->bind, sizeof(keymap_t) * bl->n) == 0,
                  "%s written back does not parse to itself:\n%s", bl->name, text);
        }
        /* And every Oric key as a target: each cell by one name. */
        unsigned named = 0;
        for (unsigned r = 0; r < ORIC_KEY_ROWS; r++)
            for (unsigned c = 0; c < ORIC_KEY_COLS; c++) {
                char one[24];
                keymap_t e = { 'a', (uint8_t)r, (uint8_t)c, 0 };
                keymap_binding_str(&e, one, sizeof one);
                if (strcmp(one, "a=?") == 0) continue;
                named++;
                CHECK(keylayout_parse(&l, "x", one, strlen(one), &line) == KL_OK &&
                          l.bind[0].row == r && l.bind[0].col == c,
                      "cell (%u,%u) as \"%s\" does not parse back", r, c, one);
            }
        CHECK(named == 58, "%u cells have names, want the 58 keys", named);
        char one[24];
        keymap_binding_str(&keylayout_builtin[0].bind[0], one, sizeof one);
        CHECK(strcmp(one, "left=Z") == 0, "ZX's first binding reads \"%s\"", one);

        /* One more binding than config.h allows. */
        static char many[1024];
        size_t at = 0;
        for (unsigned i = 0; i <= ORIC_KEYMAP_BINDINGS; i++) {
            at += (size_t)snprintf(many + at, sizeof many - at, "%c = SPACE\n", 'a' + i);
        }
        CHECK(keylayout_parse(&l, "x", many, at, &line) == KL_TOO_MANY &&
                  line == ORIC_KEYMAP_BINDINGS + 1u, "too many bindings");
    }

    TEST_DONE();
}
