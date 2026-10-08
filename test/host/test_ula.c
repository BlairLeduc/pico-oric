/* test_ula.c — the decode, the row generator, the mode scan and the
 * dirty bands (design.md §7, §13.2 ULA).
 *
 * No ROM: each rule of §2.5 is checked by decoding a window built for it.
 * The mode scan is held to the decode over random screens, with a naive
 * scan as the control that must disagree. The dirty bands are checked by
 * executing them: a simulated panel brought up to date from the bands
 * alone must match a full render after every edit, and the same run with
 * a VRAM-byte diff, the control, must leave stale pixels (§7.3).
 */

#include <string.h>
#include <time.h>

#include "test_util.h"
#include "ula.h"

static oric_frame_t g_f;
static ula_cell_t   g_cells[ORIC_PIXEL_H][ORIC_SCREEN_COLS];
static ula_shadow_t g_shadow;
static uint16_t     g_panel[ORIC_PIXEL_H][ORIC_PIXEL_W];
static uint16_t     g_full[ORIC_PIXEL_H][ORIC_PIXEL_W];
static uint8_t      g_was[ORIC_VIDEO_BYTES];

static unsigned rng_state = 12345;
static unsigned rng(void) {
    rng_state = rng_state * 1103515245u + 12345u;
    return (rng_state >> 16) & 0x7FFFu;
}

static uint8_t *at(uint16_t addr) { return &g_f.window[addr - ORIC_VIDEO_BASE]; }
static uint8_t *text(unsigned row, unsigned col) {
    return at((uint16_t)(ORIC_TEXT_BASE + row * ORIC_SCREEN_COLS + col));
}

/* A glyph row no two (set, code, row) share in their low six bits, with
 * bits 6 and 7 set, which the ULA must ignore. */
static uint8_t glyph(unsigned set, unsigned code, unsigned row) {
    return (uint8_t)(0xC0u | ((set * 17u + code * 5u + row * 3u + 1u) & 0x3Fu));
}

static void fill_charsets(void) {
    static const uint16_t base[4] = { 0xB400, 0xB800, 0x9800, 0x9C00 };
    for (unsigned s = 0; s < 4; s++)
        for (unsigned code = 0; code < 128; code++)
            for (unsigned r = 0; r < 8; r++)
                *at((uint16_t)(base[s] + code * 8u + r)) = glyph(s, code, r);
}

/* A blank text screen of spaces, in text mode, blink shown. */
static void clear(void) {
    memset(g_f.window, 0, sizeof g_f.window);
    fill_charsets();
    for (unsigned r = 0; r < ORIC_SCREEN_ROWS; r++)
        for (unsigned c = 0; c < ORIC_SCREEN_COLS; c++) *text(r, c) = ' ';
    g_f.mode = ULA_MODE_50HZ;
    g_f.blink_on = true;
}

/* A line of prose after the two attribute cells BASIC leaves. */
static void put_row_text(unsigned row) {
    static const char line[] = "10 PRINT \"HELLO, WORLD\": GOTO 10     ";
    for (unsigned c = 2; c < ORIC_SCREEN_COLS; c++) *text(row, c) = (uint8_t)line[(c + row) % 38u];
}

static ula_cell_t cell(unsigned y, unsigned x) {
    uint8_t mode = g_f.mode;
    ula_cell_t line[ORIC_SCREEN_COLS];
    for (unsigned l = 0; l <= y; l++) mode = ula_decode_line(g_f.window, l, mode, g_f.blink_on, line);
    return line[x];
}

static bool is(ula_cell_t c, unsigned ink, unsigned paper, unsigned pattern) {
    return c == ULA_CELL(ink, paper, pattern);
}

/* ---- the panel ----------------------------------------------------------- */

static void render_full(void) {
    ula_decode(&g_f, g_cells);
    for (unsigned y = 0; y < ORIC_PIXEL_H; y++)
        ula_row(g_cells[y], 0, ORIC_SCREEN_COLS - 1u, ula_palette_rgb565, g_full[y]);
}

static void draw(const ula_band_t *bands, ula_cell_t (*cells)[ORIC_SCREEN_COLS]) {
    for (unsigned b = 0; b < ORIC_BAND_COUNT; b++) {
        unsigned c0 = bands[b].c0, c1 = bands[b].c1;
        if (c0 > c1) continue;
        for (unsigned r = 0; r < ORIC_BAND_LINES; r++) {
            unsigned y = b * ORIC_BAND_LINES + r;
            ula_row(cells[y], c0, c1, ula_palette_rgb565, &g_panel[y][c0 * ORIC_GLYPH_W]);
        }
    }
}

/* What the presenter does per frame, minus the wire. */
static unsigned present(void) {
    ula_band_t bands[ORIC_BAND_COUNT];
    unsigned n = ula_diff(&g_shadow, &g_f, bands);
    draw(bands, g_shadow.cell);
    return n;
}

/* The control: bands from the bytes each band fetches in text mode and
 * in hires, as a VRAM diff would mark them, drawn from a fresh decode. */
static void present_vram_diff(void) {
    ula_band_t bands[ORIC_BAND_COUNT];
    for (unsigned b = 0; b < ORIC_BAND_COUNT; b++) {
        int lo = -1, hi = -1;
        for (unsigned x = 0; x < ORIC_SCREEN_COLS; x++) {
            bool d = false;
            unsigned t = ORIC_TEXT_BASE - ORIC_VIDEO_BASE + b * ORIC_SCREEN_COLS + x;
            d |= g_f.window[t] != g_was[t];
            for (unsigned r = 0; r < ORIC_BAND_LINES && b * 8u + r < ORIC_HIRES_LINES; r++) {
                unsigned h = ORIC_HIRES_BASE - ORIC_VIDEO_BASE + (b * 8u + r) * ORIC_SCREEN_COLS + x;
                d |= g_f.window[h] != g_was[h];
            }
            if (d) {
                if (lo < 0) lo = (int)x;
                hi = (int)x;
            }
        }
        bands[b].c0 = lo < 0 ? 1 : (uint8_t)lo;
        bands[b].c1 = lo < 0 ? 0 : (uint8_t)hi;
    }
    ula_decode(&g_f, g_cells);
    draw(bands, g_cells);
    memcpy(g_was, g_f.window, sizeof g_was);
}

/* A byte as the ULA might meet it: a third attributes, mode attributes
 * among them, the rest characters or pixels. */
static uint8_t random_byte(void) {
    unsigned k = rng() % 12u;
    if (k < 3u) return (uint8_t)((rng() & 0x80u) | (rng() % 0x18u));      /* colour, text */
    if (k == 3u) return (uint8_t)((rng() & 0x80u) | 0x18u | (rng() & 7u)); /* mode */
    return (uint8_t)(0x20u + rng() % 0xE0u);
}

static void random_edit(unsigned kind) {
    switch (kind % 6u) {
    case 0: {   /* a text-screen byte, attributes included */
        unsigned r = rng() % ORIC_SCREEN_ROWS, c = rng() % ORIC_SCREEN_COLS;
        *text(r, c) = random_byte();
        break;
    }
    case 1:     /* a hires byte */
        g_f.window[ORIC_HIRES_BASE - ORIC_VIDEO_BASE + rng() % (ORIC_HIRES_LINES * 40u)] =
            random_byte();
        break;
    case 2:     /* a character-set row, in any of the four sets */
        *at((uint16_t)(0x9800u + (rng() & 1u) * 0x1C00u + rng() % 0x800u)) ^=
            (uint8_t)(1u << (rng() % 6u));
        break;
    case 3:     /* the blink phase alone */
        g_f.blink_on = !g_f.blink_on;
        break;
    case 4:     /* the mode at field start alone */
        g_f.mode ^= ULA_MODE_HIRES;
        break;
    default: {  /* a mode attribute somewhere on the text screen */
        unsigned r = rng() % ORIC_SCREEN_ROWS, c = rng() % ORIC_SCREEN_COLS;
        *text(r, c) = (uint8_t)(0x18u | (rng() & 7u));
        break;
    }
    }
}

static void random_screen(void) {
    clear();
    for (unsigned i = 0; i < 6000u; i++)
        g_f.window[ORIC_HIRES_BASE - ORIC_VIDEO_BASE + rng() % (ORIC_HIRES_LINES * 40u)] =
            (rng() % 8u) ? (uint8_t)(0x40u | rng()) : random_byte();
    for (unsigned r = 0; r < ORIC_SCREEN_ROWS; r++)
        for (unsigned c = 0; c < ORIC_SCREEN_COLS; c++)
            *text(r, c) = (rng() % 4u) ? (uint8_t)(0x20u + rng() % 0x60u) : random_byte();
}

/* Random edits, a present after each; true if the panel ever differed
 * from a full render. */
static bool run_edits(bool vram_diff, unsigned steps) {
    rng_state = 777;
    random_screen();
    g_shadow.valid = false;
    present();
    memcpy(g_was, g_f.window, sizeof g_was);

    bool stale = false;
    for (unsigned step = 0; step < steps; step++) {
        unsigned n = 1u + rng() % 4u;
        for (unsigned k = 0; k < n; k++) random_edit(rng());
        if (vram_diff) present_vram_diff();
        else present();
        render_full();
        if (memcmp(g_panel, g_full, sizeof g_panel) != 0) stale = true;
    }
    return stale;
}

/* A scan that walks only the text screen, as if a mode attribute did
 * not move the fetch: the control for the scan's agreement. */
static uint8_t naive_scan(uint8_t mode) {
    for (unsigned r = 0; r < ORIC_SCREEN_ROWS; r++)
        for (unsigned c = 0; c < ORIC_SCREEN_COLS; c++) {
            uint8_t b = *text(r, c);
            if ((b & 0x78u) == 0x18u) mode = b & 7u;
        }
    return mode;
}

/* C11's clock: the build is strict C11, which hides clock_gettime. */
static double now_us(void) {
    struct timespec t;
    timespec_get(&t, TIME_UTC);
    return (double)t.tv_sec * 1e6 + (double)t.tv_nsec / 1e3;
}

int main(void) {
    /* ---- each line starts white on black, standard set (§7.2) --------- */
    clear();
    *text(0, 3) = 'A';
    CHECK(is(cell(2, 3), 7, 0, glyph(0, 'A', 2) & 0x3Fu),
          "'A' on line 2: ink 7, paper 0, its glyph's row 2 with bits 6-7 ignored");
    CHECK(is(cell(0, 0), 7, 0, glyph(0, ' ', 0) & 0x3Fu), "a space is a glyph like any other");

    /* ---- colour attributes apply from their own cell, shown as paper -- */
    clear();
    *text(1, 0) = 0x01;     /* ink red   */
    *text(1, 1) = 0x14;     /* paper blue */
    *text(1, 2) = 'A';
    CHECK(is(cell(8, 0), 1, 0, 0), "ink attribute: a cell of paper, ink 1 from here");
    CHECK(is(cell(8, 1), 1, 4, 0), "paper attribute: its own cell already paper 4");
    CHECK(is(cell(8, 2), 1, 4, glyph(0, 'A', 0) & 0x3Fu), "'A' in red on blue");
    CHECK(is(cell(16, 2), 7, 0, glyph(0, ' ', 0) & 0x3Fu),
          "the next row starts white on black again");
    *text(1, 5) = 0x07;
    CHECK(is(cell(9, 6), 7, 4, glyph(0, ' ', 1) & 0x3Fu), "ink 7 again from col 5, paper kept");

    /* ---- bit 7 inverts any cell, an attribute's too (§16) ------------- */
    clear();
    *text(2, 0) = 'A' | 0x80u;
    *text(2, 1) = 0x94;     /* paper blue, inverted */
    *text(2, 2) = 'B';
    CHECK(is(cell(16, 0), 0, 7, glyph(0, 'A', 0) & 0x3Fu), "inverse 'A': ink 0 on paper 7");
    CHECK(is(cell(16, 1), 0, 3, 0), "an inverted paper attribute shows paper 4 ^ 7");
    CHECK(is(cell(16, 2), 7, 4, glyph(0, 'B', 0) & 0x3Fu), "and leaves the next cell plain");

    /* ---- the alternate set, from the next cell (§2.5) ----------------- */
    clear();
    *text(3, 0) = 0x09;
    *text(3, 1) = 'A';
    *text(3, 2) = 0x08;
    *text(3, 3) = 'A';
    CHECK(is(cell(27, 1), 7, 0, glyph(1, 'A', 3) & 0x3Fu), "#09: 'A' from #B800");
    CHECK(is(cell(27, 3), 7, 0, glyph(0, 'A', 3) & 0x3Fu), "#08: back to #B400");

    /* ---- double height: top half on even rows, bottom on odd (§16) ---- */
    clear();
    *text(4, 0) = 0x0A;
    *text(4, 1) = 'A';
    *text(5, 0) = 0x0A;
    *text(5, 1) = 'A';
    {
        bool ok = true;
        for (unsigned r = 0; r < 8; r++) {
            ok &= is(cell(32 + r, 1), 7, 0, glyph(0, 'A', r / 2u) & 0x3Fu);
            ok &= is(cell(40 + r, 1), 7, 0, glyph(0, 'A', 4u + r / 2u) & 0x3Fu);
        }
        CHECK(ok, "double height: rows 0-3 of the glyph on row 4, rows 4-7 on row 5");
    }

    /* ---- blink, in both phases --------------------------------------- */
    clear();
    *text(6, 0) = 0x0C;
    *text(6, 1) = 'A';
    *text(6, 2) = 'A' | 0x80u;
    CHECK(is(cell(48, 1), 7, 0, glyph(0, 'A', 0) & 0x3Fu), "blink, shown phase: the glyph");
    g_f.blink_on = false;
    CHECK(is(cell(48, 1), 7, 0, 0), "blink, hidden phase: paper");
    CHECK(is(cell(48, 2), 0, 7, 0), "an inverse blinking cell hides to inverse paper");

    /* ---- hires: pixels from bits 0-5, attributes by bits 5-6 (§16) ---- */
    clear();
    g_f.mode = ULA_MODE_HIRES | ULA_MODE_50HZ;
    {
        uint8_t *h = at(ORIC_HIRES_BASE + 10u * 40u);
        h[0] = 0x7F;
        h[1] = 0x20;    /* bits 6, 5 = 01: six pixels of paper, not an attribute */
        h[2] = 0x55;
        h[3] = 0xA5;    /* inverse */
        h[4] = 0x02;    /* ink green */
        h[5] = 0x3F;
        CHECK(is(cell(10, 0), 7, 0, 0x3F), "#7F: six pixels of ink");
        CHECK(is(cell(10, 1), 7, 0, 0x20), "#20 in hires is a pixel, not an attribute");
        CHECK(is(cell(10, 2), 7, 0, 0x15), "#55: pattern #15");
        CHECK(is(cell(10, 3), 0, 7, 0x25), "#A5: pattern #25, inverted");
        CHECK(is(cell(10, 4), 2, 0, 0x00), "#02: an ink attribute in hires too");
        CHECK(is(cell(10, 5), 2, 0, 0x3F), "and green pixels after it");
    }
    *text(25, 0) = 'A';
    *text(25, 1) = 0x09;
    *text(25, 2) = 'A';
    CHECK(is(cell(201, 0), 7, 0, glyph(2, 'A', 1) & 0x3Fu),
          "the hires text window: text row 25, glyphs from #9800");
    CHECK(is(cell(201, 2), 7, 0, glyph(3, 'A', 1) & 0x3Fu), "and its alternate set from #9C00");

    /* ---- a mode attribute moves the fetch from the next cell ---------- */
    clear();
    *text(5, 10) = 0x1E;    /* hires, 50 Hz */
    *text(5, 11) = 'A';
    /* Lines 40-119 only: the bitmap's last lines are the text screen. */
    for (unsigned y = 40; y < 120; y++)
        memset(at((uint16_t)(ORIC_HIRES_BASE + y * 40u)), 0x40 | (y & 0x3F), 40);
    {
        ula_cell_t line[ORIC_SCREEN_COLS];
        uint8_t m = ula_decode_line(g_f.window, 41, ULA_MODE_50HZ, true, line);
        CHECK(m == 0x06 && is(line[10], 7, 0, 0) && is(line[11], 7, 0, 41 & 0x3F) &&
                  is(line[9], 7, 0, glyph(0, ' ', 1) & 0x3Fu),
              "text to hires at col 10: col 11 on is line 41's bitmap, the mode #06 after");
        m = ula_decode_line(g_f.window, 42, m, true, line);
        CHECK(m == 0x06 && is(line[0], 7, 0, 42 & 0x3F) && is(line[11], 7, 0, 42 & 0x3F),
              "the next line starts in hires and stays there");
        oric_frame_t *f = &g_f;
        f->mode = ULA_MODE_50HZ;
        CHECK(ula_decode(f, g_cells) == 0x06, "the mode lasts to the field's end, for the next");
        CHECK(is(g_cells[100][0], 7, 0, 100 & 0x3F) && is(g_cells[210][0], 7, 0, glyph(2, ' ', 2) & 0x3Fu),
              "below: bitmap to line 199, then text with the hires character set");
        CHECK(ula_scan_mode(f->window, ULA_MODE_50HZ) == 0x06, "the scan finds the same mode");
    }

    /* ---- the row generator: bit 5 leftmost ---------------------------- */
    {
        ula_cell_t line[ORIC_SCREEN_COLS];
        for (unsigned x = 0; x < ORIC_SCREEN_COLS; x++) line[x] = ULA_CELL(x & 7u, (x >> 3) & 7u, x);
        line[2] = ULA_CELL(1, 4, 0x21);
        uint16_t row[ORIC_PIXEL_W], part[3 * ORIC_GLYPH_W];
        ula_row(line, 0, ORIC_SCREEN_COLS - 1u, ula_palette_rgb565, row);
        const uint16_t R = 0xF800u, B = 0x001Fu;
        const uint16_t want[6] = { R, B, B, B, B, R };
        CHECK(memcmp(&row[12], want, sizeof want) == 0, "#21, red on blue: R B B B B R");
        ula_row(line, 5, 7, ula_palette_rgb565, part);
        CHECK(memcmp(part, &row[30], sizeof part) == 0, "cells 5-7 alone equal those of the whole row");
    }

    /* ---- the mode scan agrees with the decode, over random screens ---- */
    {
        unsigned disagree = 0, naive_wrong = 0, changed = 0;
        for (unsigned i = 0; i < 2000u; i++) {
            random_screen();
            g_f.mode = (uint8_t)(rng() & 7u);
            uint8_t want = ula_decode(&g_f, g_cells);
            disagree += ula_scan_mode(g_f.window, g_f.mode) != want;
            naive_wrong += naive_scan(g_f.mode) != want;
            changed += want != g_f.mode;
        }
        CHECK(disagree == 0, "the scan disagreed with the decode on %u of 2000 screens", disagree);
        CHECK(naive_wrong > 0, "the naive scan never disagreed: the screens do not test the walk");
        printf("mode scan: 2000 random screens, mode changed on %u; naive scan wrong on %u\n",
               changed, naive_wrong);
    }

    /* ---- dirty bands: incremental presents match a full render -------- */
    {
        clear();
        g_shadow.valid = false;
        CHECK(present() == ORIC_BAND_COUNT, "an invalid shadow should mark every band");
        CHECK(present() == 0, "an unchanged frame should mark no band");
        *text(3, 7) = 0x01;
        ula_band_t bands[ORIC_BAND_COUNT];
        unsigned n = ula_diff(&g_shadow, &g_f, bands);
        CHECK(n == 1 && bands[3].c0 == 7 && bands[3].c1 == 39,
              "an ink attribute at row 3, col 7 dirties cols 7-39 of band 3 alone (%u bands, %u..%u)",
              n, bands[3].c0, bands[3].c1);
        *at((uint16_t)(ORIC_CHARSET_TEXT + ' ' * 8u + 5u)) ^= 0x10u;
        n = ula_diff(&g_shadow, &g_f, bands);
        CHECK(n == ORIC_BAND_COUNT, "redefining space dirties every band, got %u", n);

        bool stale = run_edits(false, 400);
        CHECK(!stale, "the decoded-cell diff left stale pixels");
        bool control = run_edits(true, 400);
        CHECK(control, "the VRAM-diff control never went stale: the test is blind");
    }

    /* ---- cost on the workstation (§15.2 M4: a ratio for M7) ----------- */
    /* Three screens: text as BASIC leaves it (one mode attribute, in the
     * last cell), the bitmap in hires, and the random screen, whose mode
     * attributes make it the scan's worst case. */
    for (unsigned kind = 0; kind < 3u; kind++) {
        static const char *const names[3] = { "text", "hires", "random" };
        clear();
        if (kind == 0) {
            for (unsigned r = 1; r < ORIC_SCREEN_ROWS; r++) {
                *text(r, 0) = 0x17;
                *text(r, 1) = 0x00;
                put_row_text(r);
            }
            *text(27, 39) = 0x1A;
        } else if (kind == 1) {
            g_f.mode = ULA_MODE_HIRES | ULA_MODE_50HZ;
            for (unsigned i = 0; i < ORIC_HIRES_LINES * 40u; i++)
                g_f.window[ORIC_HIRES_BASE - ORIC_VIDEO_BASE + i] = (uint8_t)(0x40u | rng());
        } else {
            rng_state = 99;
            random_screen();
            g_f.mode = ULA_MODE_50HZ;
        }
        const unsigned reps = 2000u;
        volatile unsigned sink = 0;
        double t0 = now_us();
        for (unsigned i = 0; i < reps; i++) sink += ula_decode(&g_f, g_cells);
        double t1 = now_us();
        for (unsigned i = 0; i < reps; i++)
            for (unsigned y = 0; y < ORIC_PIXEL_H; y++)
                ula_row(g_cells[y], 0, ORIC_SCREEN_COLS - 1u, ula_palette_rgb565, g_full[y]);
        double t2 = now_us();
        for (unsigned i = 0; i < reps; i++) sink += ula_scan_mode(g_f.window, g_f.mode);
        double t3 = now_us();
        (void)sink;
        printf("cost per frame, %-6s: decode %6.2f us, rows %6.2f us, mode scan %6.2f us\n",
               names[kind], (t1 - t0) / reps, (t2 - t1) / reps, (t3 - t2) / reps);
    }

    TEST_DONE();
}
