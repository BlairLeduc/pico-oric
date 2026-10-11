/* menu.c — the emulator's menu and pause (menu.h, design.md §12).
 *
 * pico-ace's menu, which is pico-atom's page for page and row for row,
 * so that the three emulators read as one family: the same items in the
 * same order, the same function keys, the same rows on each page, and
 * Esc going back a page, or to the guest from the main page or a page a
 * key opened. The Oric has every page the Atom has, so the Discs page is
 * back, on F2, with the Microdisc's four drives (M14). The Machine page
 * stages the ROM, the RAM, the Microdisc and the VSync hack (§12, M16),
 * and the text is in the Oric's own character set, which has lower case.
 */

#include "menu.h"

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "board.h"
#include "card.h"
#include "core0.h"
#include "core1.h"
#include "discio.h"
#include "display.h"
#include "handoff.h"
#include "kbd.h"
#include "keymapio.h"
#include "keymatrix.h"
#include "log.h"
#include "microdisc.h"
#include "pico_oric_version.h"
#include "roms.h"
#include "sd.h"
#include "settingsio.h"
#include "shotio.h"
#include "snapio.h"
#include "southbridge.h"
#include "status.h"
#include "storage.h"
#include "tapeio.h"
#include "textpage.h"

/* The keyboard at the live loop's 30 Hz, which also feeds the MCU's 2.5 s
 * bus watchdog (hardware-notes.md §6.1). */
#define POLL_US 33333u

/* The MCU refreshes its battery reading every 20 s (hardware-notes.md
 * §6); reading it more often than this would show nothing new. */
#define BAT_POLL_US 5000000u

/* Backlight register values step by 16 and clamp to 16-240
 * (hardware-notes.md §6). */
#define BKL_STEP   16u
#define BKL_LOWEST 16u

/* The page: a title, the rows from row 2, and at the foot the status
 * row and the keys that work here (§12). */
#define ROW_TOP    2
#define ROW_STATUS (TEXT_ROWS - 2)
#define ROW_KEYS   (TEXT_ROWS - 1)

/* pico-atom's items, in its order (§12). The function keys open the
 * first five (keymatrix.h's KM_PAGE_*). */
enum { I_TAPES, I_DISCS, I_SNAPS, I_SETUP, I_MACHINE, I_RESET, I_SAVE, I_ABOUT, I_COUNT };

/* The Setup page, as pico-ace's: the lines, the backlight, the volume
 * and the keys, then fast tape. */
enum { D_STATUS, D_PERF, D_BACKLIGHT, D_VOLUME, D_KEYS, D_FAST, D_COUNT };

/* The Tapes page, as pico-ace's: four things to do with the deck, then
 * the card's tapes, which the cursor moves on to. Play is the signal's
 * (M13), for a loader that never calls the ROM: until then CLOAD plays
 * the tape through the trap. */
enum { T_EJECT, T_PLAY, T_REWIND, T_NEW, T_FIRST };
#define TAPE_ROWS (ROW_STATUS - 1 - (ROW_TOP + 1))

/* The Snapshots page, as pico-ace's less its .ace files (§10.6): the
 * slot, chosen with < > on any of its rows, the three things to do with
 * it, then every slot's state. */
enum { N_SLOT, N_SAVE, N_LOAD, N_DELETE, N_COUNT };

/* The Machine page (§12): the ROM, the RAM, the Microdisc and the VSync
 * hack staged, and the power-on that applies them. */
enum { M_ROM, M_RAM, M_DISC, M_VSYNC, M_APPLY, M_COUNT };

static settings_t    s_file;      /* what the file says, for the save */
static oric_frame_t *s_scr;       /* core 1's page frame (menu_init)  */
static rom_id_t      s_rom;       /* the running ROM                  */
static bool          s_bkl_set;   /* the Setup page set the backlight */
static bool          s_rom_known; /* and it is its image              */
static unsigned      s_slot;      /* the Snapshots page's, kept between openings */

/* The Machine page's and About's look at the card. */
static card_job_t s_job;

static tapeio_entry_t s_list[ORIC_TAPE_LIST_MAX];
static discio_entry_t s_discs[ORIC_DISC_LIST_MAX];

/* The Discs page, as pico-atom's: row 0 empties the drive, the card's
 * images follow; < > choose the drive on any row. */
#define DISC_ROWS TAPE_ROWS
static unsigned s_drive;    /* kept between openings, as the slot is */

static struct {
    oric_t  *m;
    bool     card;
    bool     alt;
    bool     done;
    bool     direct;          /* opened at a page: closing it resumes */
    int      item;
    enum { P_MAIN, P_TAPES, P_DISCS, P_SNAPS, P_SETUP, P_MACHINE, P_ABOUT, P_HELP } page;
    char     status[TEXT_COLS + 1];
    int      battery;         /* SB_REG_BAT's byte, -1 if unread */

    int      setup_sel;

    unsigned n_tapes;
    int      tape_sel, tape_top;

    int      snap_sel;
    bool     used[SNAPIO_SLOTS];

    unsigned n_discs;
    int      disc_sel, disc_top;

    int        machine_sel;
    rom_id_t   st_rom;        /* staged: applied only by a power-on */
    oric_ram_t st_ram;
    bool       st_md;
    bool       st_vs;

    int      sb_ver;          /* the About page's, read as it opens */
    int      temp_c;
    bool     about_job;       /* s_job holds the card's ROMs */
} s;

void menu_init(const settings_t *file, oric_frame_t *page, rom_id_t rom, bool rom_known) {
    s_file = *file;
    s_scr = page;
    s_rom = rom;
    s_rom_known = rom_known;
}

const char *menu_rom_problem(void) {
    static char text[TEXT_COLS + 1];
    if (s_rom_known || s_rom == ROM_UNKNOWN) return "";
    snprintf(text, sizeof text, "%s: unrecognised image", romset_images[s_rom].file);
    return text;
}

static void say(const char *fmt, const char *arg) {
    snprintf(s.status, sizeof s.status, fmt, arg);
}

static const char *on_off(bool v) {
    return v ? "on" : "off";
}

static const char *base(const char *path) {
    const char *b = strrchr(path, '/');
    return b ? b + 1 : path;
}

static const char *ram_name(oric_ram_t ram) {
    return ram == ORIC_RAM_16K ? "16K" : "48K";
}

/* ---- drawing ------------------------------------------------------------- */

static void draw_main(void) {
    static const char *const items[I_COUNT] = {
        " Tapes...", " Discs...", " Snapshots...", " Setup...", " Machine...", " Reset",
        " Save settings", " About...",
    };
    for (int i = 0; i < I_COUNT; i++)
        textpage_line(s_scr, ROW_TOP + i, items[i], i == s.item);

    /* The deck, as pico-ace's main page shows it, and the machine. */
    char line[TEXT_COLS + 1];
    const char *in = tapeio_inserted();
    snprintf(line, sizeof line, " Tape in: %.29s", in[0] ? base(in) : "none");
    textpage_line(s_scr, ROW_TOP + I_COUNT + 1, line, false);
    snprintf(line, sizeof line, " Machine: %s, ROM %s%s",
             roms_machine_name(s.m->cfg.rom, s.m->cfg.ram), settings_rom_str(s.m->cfg.rom),
             s.m->cfg.microdisc ? ", Microdisc" : "");
    textpage_line(s_scr, ROW_TOP + I_COUNT + 2, line, false);
    snprintf(line, sizeof line, " Keys: %s", g_ui.layout ? g_ui.layout->name : "standard");
    textpage_line(s_scr, ROW_TOP + I_COUNT + 3, line, false);
    if (s.m->cfg.microdisc) {
        const char *d = discio_inserted(0);
        snprintf(line, sizeof line, " Disc A: %.30s", d[0] ? base(d) : "none");
        textpage_line(s_scr, ROW_TOP + I_COUNT + 4, line, false);
    }
    if (s.m->cfg.vsync_hack) textpage_line(s_scr, ROW_TOP + I_COUNT + 5, " VSync hack: on", false);
}

static void draw_tapes(void) {
    char line[TEXT_COLS + 1];
    textpage_line(s_scr, ROW_TOP, !s.card ? " No card"
                                : s.n_tapes ? " File                 First file"
                                : " No tapes in /oric/tapes/", false);
    for (int r = 0; r < TAPE_ROWS; r++) {
        int i = s.tape_top + r;
        line[0] = 0;
        if (i == T_EJECT) {
            snprintf(line, sizeof line, " (Eject)");
        } else if (i == T_PLAY) {
            snprintf(line, sizeof line, "%s", g_ui.fast_tape ? " (Play: CLOAD plays the tape)"
                                              : tapeio_playing() ? " (Stop)" : " (Play)");
        } else if (i == T_REWIND) {
            snprintf(line, sizeof line, " (Rewind)");
        } else if (i == T_NEW) {
            snprintf(line, sizeof line, " (New tape)");
        } else if (i < T_FIRST + (int)s.n_tapes) {
            const tapeio_entry_t *e = &s_list[i - T_FIRST];
            bool here = strcmp(e->path, tapeio_inserted()) == 0;
            snprintf(line, sizeof line, "%c%-20.20s %-16.16s", here ? '*' : ' ', base(e->path),
                     e->name[0] ? e->name : "(empty)");
        }
        textpage_line(s_scr, ROW_TOP + 1 + r, line, i == s.tape_sel);
    }
    /* CB1 is the sync's: the signal is not heard (vsync.h, M16). */
    if (s.m->cfg.vsync_hack)
        textpage_line(s_scr, ROW_STATUS - 1, " VSync hack: no signal; fast tape loads", false);
}

static void draw_snaps(void) {
    char line[TEXT_COLS + 1];
    static const char *const rows[N_COUNT] = { NULL, " Save", " Load", " Delete" };
    for (int i = 0; i < N_COUNT; i++) {
        if (i == N_SLOT) snprintf(line, sizeof line, " Slot            < %u >", s_slot + 1u);
        else snprintf(line, sizeof line, "%s", rows[i]);
        textpage_line(s_scr, ROW_TOP + i, line, i == s.snap_sel);
    }
    /* Every slot's state, the chosen one marked. */
    for (unsigned i = 0; i < SNAPIO_SLOTS; i++) {
        snprintf(line, sizeof line, "%cSlot %u: %s", i == s_slot ? '*' : ' ', i + 1u,
                 !s.card ? "no card" : s.used[i] ? "saved" : "empty");
        textpage_line(s_scr, ROW_TOP + N_COUNT + 1 + (int)i, line, false);
    }
    textpage_line(s_scr, ROW_TOP + N_COUNT + 2 + (int)SNAPIO_SLOTS,
                  " In " SNAPIO_STATE_DIR "/. A load powers on", false);
    textpage_line(s_scr, ROW_TOP + N_COUNT + 3 + (int)SNAPIO_SLOTS,
                  " as the machine it was saved on.", false);
}

/* Which drive an image is in: A to D, or a space. */
static char drive_of(const char *path) {
    for (unsigned d = 0; d < ORIC_DISC_DRIVES; d++)
        if (strcmp(path, discio_inserted(d)) == 0) return (char)('A' + d);
    return ' ';
}

static void draw_discs(void) {
    char line[TEXT_COLS + 1];
    snprintf(line, sizeof line, " Drive < %c >  %s", 'A' + s_drive,
             !s.card ? "No card" : s.n_discs ? "Image        In  R/O"
                                              : "No discs in /oric/discs/");
    textpage_line(s_scr, ROW_TOP, line, false);
    for (int r = 0; r < DISC_ROWS; r++) {
        int i = s.disc_top + r;
        line[0] = 0;
        if (i == 0) {
            const char *in = discio_inserted(s_drive);
            snprintf(line, sizeof line, " (Empty drive %c: %.18s)", 'A' + s_drive,
                     in[0] ? base(in) : "empty now");
        } else if (i <= (int)s.n_discs) {
            const discio_entry_t *e = &s_discs[i - 1];
            snprintf(line, sizeof line, " %-26.26s %c  %c", base(e->path), drive_of(e->path),
                     e->protect ? 'P' : ' ');
        }
        textpage_line(s_scr, ROW_TOP + 1 + r, line, i == s.disc_sel);
    }
    if (!s.m->cfg.microdisc)
        textpage_line(s_scr, ROW_STATUS - 1, " The Microdisc is off: Machine page.", false);
}

static void draw_setup(void) {
    char line[TEXT_COLS + 1];
    for (int i = 0; i < D_COUNT; i++) {
        switch (i) {
        case D_STATUS:
            snprintf(line, sizeof line, " Status line     < %s >", on_off(g_ui.status));
            break;
        case D_PERF:
            snprintf(line, sizeof line, " Perf line       < %s >", on_off(g_ui.perf_line));
            break;
        case D_BACKLIGHT:
            snprintf(line, sizeof line, " Backlight       < %u >", g_ui.backlight);
            break;
        case D_VOLUME:
            snprintf(line, sizeof line, " Volume          < %u >", g_ui.volume);
            break;
        case D_KEYS:
            /* A card layout's name is cut to keep the column. */
            snprintf(line, sizeof line, " Keys            < %.11s >",
                     g_ui.layout ? g_ui.layout->name : "standard");
            break;
        case D_FAST:
            snprintf(line, sizeof line, " Fast tape       < %s >", on_off(g_ui.fast_tape));
            break;
        }
        textpage_line(s_scr, ROW_TOP + i, line, i == s.setup_sel);
    }
}

/* Does row r's staged value differ from the running machine? */
static bool machine_changed(int r) {
    switch (r) {
    case M_ROM: return s.st_rom != s.m->cfg.rom;
    case M_RAM: return s.st_ram != s.m->cfg.ram;
    case M_DISC: return s.st_md != s.m->cfg.microdisc;
    case M_VSYNC: return s.st_vs != s.m->cfg.vsync_hack;
    }
    return false;
}

static bool machine_staged(void) {
    for (int r = 0; r < M_APPLY; r++)
        if (machine_changed(r)) return true;
    return false;
}

static void draw_machine(void) {
    char line[TEXT_COLS + 1];
    for (int i = 0; i < M_COUNT; i++) {
        char mark = machine_changed(i) ? '*' : ' ';
        switch (i) {
        case M_ROM:
            snprintf(line, sizeof line, "%cROM             < %s >", mark, settings_rom_str(s.st_rom));
            break;
        case M_RAM:
            snprintf(line, sizeof line, "%cRAM             < %s >", mark, ram_name(s.st_ram));
            break;
        case M_DISC:
            snprintf(line, sizeof line, "%cMicrodisc       < %s >", mark, on_off(s.st_md));
            break;
        case M_VSYNC:
            snprintf(line, sizeof line, "%cVSync hack      < %s >", mark, on_off(s.st_vs));
            break;
        case M_APPLY:
            snprintf(line, sizeof line, " (Apply and restart)");
            break;
        }
        textpage_line(s_scr, ROW_TOP + i, line, i == s.machine_sel);
    }
    snprintf(line, sizeof line, " = the %s%s%s", roms_machine_name(s.st_rom, s.st_ram),
             s.st_md ? ", Microdisc" : "", s.st_vs ? ", VSync hack" : "");
    textpage_line(s_scr, ROW_TOP + M_COUNT + 1, line, false);
    textpage_line(s_scr, ROW_TOP + M_COUNT + 3, " - Program in memory is lost on", false);
    textpage_line(s_scr, ROW_TOP + M_COUNT + 4, "   restart.", false);
    textpage_line(s_scr, ROW_TOP + M_COUNT + 5, " - 1.0 is the Oric-1's BASIC, 1.1", false);
    textpage_line(s_scr, ROW_TOP + M_COUNT + 6, "   the Atmos's, each from the card.", false);
    textpage_line(s_scr, ROW_TOP + M_COUNT + 7, " - The Microdisc needs 48K and", false);
    textpage_line(s_scr, ROW_TOP + M_COUNT + 8, "   microdis.rom; it boots the disc", false);
    textpage_line(s_scr, ROW_TOP + M_COUNT + 9, "   in drive A (F2).", false);
    textpage_line(s_scr, ROW_TOP + M_COUNT + 10, " - The VSync hack puts the sync on", false);
    textpage_line(s_scr, ROW_TOP + M_COUNT + 11, "   the tape input, for the programs", false);
    textpage_line(s_scr, ROW_TOP + M_COUNT + 12, "   that wait for it; fast tape only.", false);
}

/* One image's row on About (§12): its file, OK for the image's SHA-1,
 * ?? for a file with only its name, -- for none, and the first eight
 * digits of what the card has; * marks the running ROM. */
static void about_rom(int row, rom_id_t id) {
    char line[TEXT_COLS + 1], hex[9] = "";
    const char *word = "--";
    if (s.about_job && s_job.rom[id] != ROMFILE_ABSENT) {
        const uint8_t *d = s_job.digest[id];
        snprintf(hex, sizeof hex, "%02X%02X%02X%02X", d[0], d[1], d[2], d[3]);
        word = s_job.rom[id] == ROMFILE_KNOWN ? "OK" : "??";
    } else if (!s.about_job) {
        word = "";
    }
    snprintf(line, sizeof line, "%c%-13s %-2s %s", id == s.m->cfg.rom ? '*' : ' ',
             romset_images[id].file, word, hex);
    textpage_line(s_scr, row, line, false);
    /* The word in the missing-ROM page's colours (roms.c): an ink
     * attribute in the space before it, white again in the one after. */
    if (word[0]) {
        uint8_t *r = textpage_row(s_scr, row);
        r[14] = word[0] == 'O' ? INK_GREEN : word[0] == '?' ? INK_YELLOW : INK_RED;
        r[17] = INK_WHITE;
    }
}

static void draw_about(void) {
    char line[TEXT_COLS + 1];
    const board_info_t *b = &g_board;
    snprintf(line, sizeof line, " Pico-Oric %.28s", PICO_ORIC_VERSION);
    textpage_line(s_scr, 2, line, false);
    snprintf(line, sizeof line, " Board %.32s", b->sdk_board);
    textpage_line(s_scr, 3, line, false);
    char sb[4] = "??";
    if (s.sb_ver >= 0) snprintf(sb, sizeof sb, "%02X", (unsigned)(s.sb_ver & 0xFF));
    /* Whole degrees and uncalibrated (hardware-notes.md §8.1), clamped
     * so the row is never more than TEXT_COLS. */
    int t = s.temp_c < -99 ? -99 : s.temp_c > 999 ? 999 : s.temp_c;
    snprintf(line, sizeof line, " %s rev %X %u MHz SB %s %dC", b->chip ? b->chip : "?",
             b->chip_version & 0xFu, (unsigned)((b->clk_sys_hz / 1000000u) % 1000u), sb, t);
    textpage_line(s_scr, 4, line, false);
    snprintf(line, sizeof line, " %s, Microdisc %s, VSync %s",
             roms_machine_name(s.m->cfg.rom, s.m->cfg.ram), on_off(s.m->cfg.microdisc),
             on_off(s.m->cfg.vsync_hack));
    textpage_line(s_scr, 5, line, false);

    /* The ROMs as the card has them now (§10.2). */
    textpage_line(s_scr, 7, s.about_job ? " " ORIC_ROM_DIR "/:" : " No card: the ROMs not read", false);
    for (int id = 0; id < ROM_IMAGE_COUNT; id++) about_rom(8 + id, (rom_id_t)id);
    if (!s_rom_known) textpage_line(s_scr, 8 + ROM_IMAGE_COUNT, " The running ROM is unrecognised.",
                                    false);

    const char *err = settingsio_error();
    snprintf(line, sizeof line, " Settings %.30s",
             err[0] ? err : settingsio_state_str(settingsio_state()));
    textpage_line(s_scr, 13, line, false);
}

/* The keys the emulator takes for itself, and the Oric's keys that are
 * not on the PicoCalc's caps (§9.2, §12), one a row. */
static void draw_help(void) {
    static const char *const keys[][2] = {
        { "F1",     "Tapes" },
        { "F2",     "Discs" },
        { "F3",     "Snapshots" },
        { "F4",     "Setup" },
        { "F5",     "Machine" },
        { "F10",    "About" },
        { "F6",     "Screenshot" },
        { "Alt+M",  "Menu" },
        { "Alt+H",  "These keys" },
        { "Alt+P",  "Pause" },
        { "Alt+K",  "Reset button (NMI)" },
        { "Tab",    "FUNCT" },
        { "Ctrl",   "CTRL" },
        { "Esc",    "ESC" },
        { "Bksp",   "DEL" },
    };
    char line[TEXT_COLS + 1];
    int row = ROW_TOP;
    for (unsigned i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        snprintf(line, sizeof line, " %-11s %s", keys[i][0], keys[i][1]);
        textpage_line(s_scr, row++, line, false);
    }

    /* The layout in force, two bindings a row as a .map file writes them
     * (§9.4), as many as fit above the status row. */
    const keylayout_t *l = g_ui.layout;
    if (!l) return;
    snprintf(line, sizeof line, " Keys %s:", l->name);
    textpage_line(s_scr, ++row, line, false);
    row++;
    for (unsigned i = 0; i < l->n && row < ROW_STATUS - 1; i += 2, row++) {
        char a[20], b[20] = "";
        keymap_binding_str(&l->bind[i], a, sizeof a);
        if (i + 1 < l->n) keymap_binding_str(&l->bind[i + 1], b, sizeof b);
        snprintf(line, sizeof line, " %-17s %s", a, b);
        textpage_line(s_scr, row, line, false);
    }
}

/* The title row's right end: the charge, and Chg in place of Bat while
 * it charges (bit 7, hardware-notes.md §6). Nothing if it could not be
 * read. */
static void draw_battery(void) {
    if (s.battery < 0) return;
    unsigned pct = (unsigned)s.battery & 0x7Fu;
    char text[12];
    snprintf(text, sizeof text, "%s %u%% ", s.battery & 0x80 ? "Chg" : "Bat",
             pct > 100u ? 100u : pct);
    textpage_put(s_scr, 0, TEXT_COLS - (int)strlen(text), text, false);
}

static bool read_battery(void) {
    uint8_t r[2];
    int was = s.battery;
    s.battery = sb_read(SB_REG_BAT, r) == SB_OK ? r[1] : -1;
    return s.battery != was;
}

static void draw(void) {
    static const char *const title[] = {
        [P_MAIN] = "  Pico-Oric",               [P_TAPES] = "  Pico-Oric: Tapes",
        [P_DISCS] = "  Pico-Oric: Discs",       [P_SNAPS] = "  Pico-Oric: Snapshots",
        [P_SETUP] = "  Pico-Oric: Setup",       [P_MACHINE] = "  Pico-Oric: Machine",
        [P_ABOUT] = "  Pico-Oric: About",       [P_HELP] = "  Pico-Oric: Keys",
    };
    static const char *const keys[] = {
        [P_MAIN] = "  Arrows  Enter  Esc resumes", [P_TAPES] = "  Enter inserts  Esc back",
        [P_DISCS] = "  < > drive  Enter inserts  Esc", [P_SNAPS] = "  < > slot  Enter  Esc back",
        [P_SETUP] = "  < > changes  Esc back",     [P_MACHINE] = "  < > stages  Enter  Esc back",
        [P_ABOUT] = "  Esc back",                  [P_HELP] = "  Esc resumes",
    };
    textpage_clear(s_scr, display_font());
    /* The bars as the missing-ROM page draws its title (roms.h). */
    textpage_title(s_scr, 0, title[s.page]);
    draw_battery();
    switch (s.page) {
    case P_TAPES:   draw_tapes(); break;
    case P_DISCS:   draw_discs(); break;
    case P_SNAPS:   draw_snaps(); break;
    case P_SETUP:   draw_setup(); break;
    case P_MACHINE: draw_machine(); break;
    case P_ABOUT:   draw_about(); break;
    case P_HELP:    draw_help(); break;
    default:        draw_main(); break;
    }
    textpage_line(s_scr, ROW_STATUS, s.status, false);
    textpage_title(s_scr, ROW_KEYS, keys[s.page]);
    display_present(s_scr, NULL);
}

/* ---- opening the pages ------------------------------------------------------ */

static void open_tapes(void) {
    s.n_tapes = s.card ? tapeio_list(s_list, ORIC_TAPE_LIST_MAX) : 0;
    s.page = P_TAPES;
    s.tape_sel = T_EJECT;
    for (unsigned i = 0; i < s.n_tapes; i++)
        if (strcmp(s_list[i].path, tapeio_inserted()) == 0) s.tape_sel = T_FIRST + (int)i;
    s.tape_top = s.tape_sel >= TAPE_ROWS ? s.tape_sel - TAPE_ROWS + 1 : 0;
    /* The list in the log too, for a check driven over the UART. */
    for (unsigned i = 0; i < s.n_tapes; i++)
        log_core1("  tapes        : %2u %s, %lu bytes, first \"%s\"%s\n", i + 1u, s_list[i].path,
                  (unsigned long)s_list[i].size, s_list[i].name, s_list[i].code ? ", code" : "");
}

static void open_discs(void) {
    s.n_discs = s.card ? discio_list(s_discs, ORIC_DISC_LIST_MAX) : 0;
    s.page = P_DISCS;
    s.disc_sel = 0;
    for (unsigned i = 0; i < s.n_discs; i++)
        if (strcmp(s_discs[i].path, discio_inserted(s_drive)) == 0) s.disc_sel = (int)i + 1;
    s.disc_top = s.disc_sel >= DISC_ROWS ? s.disc_sel - DISC_ROWS + 1 : 0;
    log_core1("  discs        : page open, drive %c, %u images in " DISCIO_DIR "%s\n",
              'A' + s_drive, s.n_discs, s.card ? "" : " (no card)");
    for (unsigned i = 0; i < s.n_discs; i++)
        log_core1("  discs        : %2u %s, %lu bytes%s%s%c\n", i + 1u, s_discs[i].path,
                  (unsigned long)s_discs[i].size, s_discs[i].protect ? ", read-only" : "",
                  drive_of(s_discs[i].path) != ' ' ? ", in drive " : "",
                  drive_of(s_discs[i].path) != ' ' ? drive_of(s_discs[i].path) : ' ');
}

static void refresh_slots(void) {
    for (unsigned i = 0; i < SNAPIO_SLOTS; i++) s.used[i] = s.card && snapio_exists(i);
}

static void open_snaps(void) {
    refresh_slots();
    s.page = P_SNAPS;
    s.snap_sel = N_SAVE;
    unsigned used = 0;
    for (unsigned i = 0; i < SNAPIO_SLOTS; i++) used += s.used[i];
    log_core1("  snapshot     : page open, slot %u, %u of %u slots in use\n", s_slot + 1u, used,
              SNAPIO_SLOTS);
}

static void open_machine(void) {
    s.page = P_MACHINE;
    s.machine_sel = M_ROM;
    s.st_rom = s.m->cfg.rom;
    s.st_ram = s.m->cfg.ram;
    s.st_md = s.m->cfg.microdisc;
    s.st_vs = s.m->cfg.vsync_hack;
}

static void open_about(void) {
    uint8_t r[2];
    s.page = P_ABOUT;
    s.sb_ver = sb_read(SB_REG_VER, r) == SB_OK ? r[1] : -1;
    s.temp_c = board_temp_c();
    /* The card's ROMs hashed afresh: a file may have changed on a
     * computer since the boot read them (§10.2). */
    s.about_job = s.card;
    if (s.card) card_roms_mounted(&s_job, s.m->cfg.rom, NULL);
    log_core1("  about        : %s, %s rev %u, id %s, %lu MHz, southbridge %d, die %d C, "
              "%s, ROM %s%s, settings %s\n", PICO_ORIC_VERSION, g_board.chip,
              (unsigned)g_board.chip_version, g_board.unique_id,
              (unsigned long)(g_board.clk_sys_hz / 1000000u), s.sb_ver, s.temp_c,
              roms_machine_name(s.m->cfg.rom, s.m->cfg.ram), romset_images[s.m->cfg.rom].file,
              s_rom_known ? "" : " (unrecognised)",
              settingsio_error()[0] ? settingsio_error() : settingsio_state_str(settingsio_state()));
}

/* ---- actions --------------------------------------------------------------- */

static void save_settings(void) {
    if (!s.card) { say(" No card: not saved", ""); return; }
    settings_t out = s_file;
    /* The machine as it runs, not as the Machine page has it staged
     * (EL §10). */
    out.rom = s.m->cfg.rom;
    out.ram = s.m->cfg.ram;
    out.volume = g_ui.volume;
    out.perf = g_ui.perf_line;
    out.status = g_ui.status;
    out.fast_tape = g_ui.fast_tape;
    out.microdisc = s.m->cfg.microdisc;
    out.vsync_hack = s.m->cfg.vsync_hack;
    /* The panel's level is the MCU's too, set by its own chords: the file
     * keeps what it has unless the Setup page set one, and 0 keeps the
     * file's (settings.h; EL §8.7). */
    if (s_bkl_set) out.backlight = g_ui.backlight;
    /* The deck's tape, if the menu or boot_tape put it there, and drive
     * A's disc. */
    settings_card_name(SETTINGS_TAPE_DIR, tapeio_chosen() ? tapeio_inserted() : "",
                       out.boot_tape);
    settings_card_name(SETTINGS_DISC_DIR, discio_inserted(0), out.boot_disc);
    /* A layout a loaded file chose is not the user's choice, so the file
     * keeps what it had (§9.4). */
    if (!keymapio_chosen_by()[0])
        snprintf(out.layout, sizeof out.layout, "%s", g_ui.layout ? g_ui.layout->name : "");
    const char *err = settingsio_save(&out);
    if (!err) s_file = out;
    say(err ? " Not saved: %.20s" : " Settings saved", err);
}

/* The card's part of a power-on as cfg (§12): its ROM into g_boot.image,
 * with the font, and with the Microdisc its EPROM into g_boot.eprom.
 * False, with the status row saying why, when one is not there; the
 * machine is not touched either way. */
static bool stage_roms(const oric_config_t *cfg) {
    /* Refused while a recording is not on the card yet (§12, EL §10):
     * the power-on would drop it. The trap writes each CSAVE before the
     * guest runs on; the recorder's waits only for a missing card. */
    uint32_t from, to;
    if (oric_cassette_unsaved(s.m, &from, &to)) {
        say(" A recording is not saved: card", "");
        return false;
    }
    /* The Microdisc's overlay RAM is a 48K machine's (§12). */
    if (cfg->microdisc && cfg->ram != ORIC_RAM_48K) { say(" The Microdisc needs 48K", ""); return false; }
    if (cfg->microdisc) {
        if (!s.card) { say(" No card: no microdis.rom", ""); return false; }
        card_roms_mounted(&s_job, cfg->rom, NULL);
        if (!card_eprom(&s_job, g_boot.eprom)) { say(" No microdis.rom on the card", ""); return false; }
    }
    const char *file = romset_images[cfg->rom].file;
    if (cfg->rom != s.m->cfg.rom) {
        if (!s.card) { say(" No card: no %.20s", file); return false; }
        say(" Reading %.20s...", file);
        draw();
        card_roms_mounted(&s_job, cfg->rom, g_boot.image);
        if (s_job.rom[cfg->rom] == ROMFILE_ABSENT) { say(" No %.20s on the card", file); return false; }
        if (s_job.loaded != cfg->rom) { say(" %.20s: cannot be read", file); return false; }
        if (!s_job.loaded_known) { say(" %.20s: unrecognised", file); return false; }
        /* The emulator's font is the running ROM's (§7.6). */
        uint8_t font[ORIC_CHARSET_BYTES];
        roms_charset(&s_job, g_boot.image, font);
        display_init(font);
        s_rom_known = true;
        s.status[0] = 0;
    } else {
        /* The power-on empties the socket (oric.h); the same image goes
         * back in. */
        memcpy(g_boot.image, s.m->rom, ORIC_ROM_SIZE);
    }
    s_rom = cfg->rom;
    return true;
}

/* The machine as a state needs it (§10.6): powered on here, on core 1
 * with the guest parked, as the load that follows changes it, with core
 * 0's own power-on. For a state for another machine, and for one for
 * this machine while it waits on the tape or is busy on a disc command,
 * which a load cannot take over: a Microdisc with no disc waits so for
 * ever, in the EPROM's boot. False, with the status row saying why and
 * the machine as it was, when the card has not the ROMs it needs. */
static bool snap_switch(const snap_info_t *in) {
    oric_config_t cfg = s.m->cfg;
    snapshot_machine(in, &cfg);
    if (!stage_roms(&cfg)) {
        log_core1("  snapshot     : slot %u is the %s's; not loaded:%s\n", s_slot + 1u,
                  roms_machine_name(cfg.rom, cfg.ram), s.status);
        return false;
    }
    log_core1("  snapshot     : slot %u is the %s%s%s's; powering on as it, ROM %s\n", s_slot + 1u,
              roms_machine_name(cfg.rom, cfg.ram), cfg.microdisc ? " with the Microdisc" : "",
              cfg.vsync_hack ? " with the VSync hack" : "", romset_images[cfg.rom].file);
    core0_power_on(s.m, &cfg, g_boot.image);
    return true;
}

/* The state in the slot: on success the menu closes, and the guest
 * resumes from it. A state for another machine powers on as that
 * machine first (snap_switch). A load whose second pass failed has left
 * a machine part old and part new: it is not resumed, but powered on
 * again as it is configured, with its own ROM (snapio.h). */
static void snap_load(void) {
    snap_info_t in = { ROM_UNKNOWN, ORIC_RAM_48K, false, false, 0, 0, 0, 0 };
    bool recovered, changed = false;
    uint32_t t0 = time_us_32();
    snap_status_t st = snapio_check(s.m, s_slot, &in, &recovered);
    /* Another ROM is one to power on as only if the card can have it;
     * for the rest, the ROM is the running one (snapshot.h). */
    bool busy = oric_tape_pending(s.m) || oric_disc_busy(s.m);
    if ((st == SNAP_OTHER_ROM && in.rom != ROM_UNKNOWN) || st == SNAP_OTHER_RAM ||
        st == SNAP_OTHER_MACHINE || (st == SNAP_OK && busy)) {
        if (!snap_switch(&in)) return;
        st = SNAP_OK;
        /* A load that fails now leaves a machine just powered on, not
         * the one the user had: it is powered on again, as below. */
        changed = true;
    }
    if (st == SNAP_OK) {
        bool torn;
        st = snapio_load(s.m, &torn);
        changed = st != SNAP_OK && (changed || torn);
    }
    log_core1("  snapshot     : load slot %u: %s%s, %lu us\n", s_slot + 1u, snapshot_status_str(st),
              recovered ? " (from the unpublished .new)" : "", (unsigned long)(time_us_32() - t0));
    if (st == SNAP_OK) {
        /* A tape the state had in the deck and the card no longer has:
         * said on the status line, since the menu closes (snapio.h). */
        if (snapio_said()[0]) core1_note(snapio_said() + 1);
        s.done = true;
        return;
    }
    if (changed) {
        log_core1("  snapshot     : failed after the machine had changed; powering on again\n");
        core1_note("Not loaded: powered on again");
        memcpy(g_boot.image, s.m->rom, ORIC_ROM_SIZE);
        g_ui.restart_cfg = s.m->cfg;
        g_ui.restart = true;
        s.done = true;
        return;
    }
    if (st == SNAP_NO_DISC) {
        say("%s", snapio_said());
    } else if (st == SNAP_IO && !s.used[s_slot]) {
        snprintf(s.status, sizeof s.status, " Slot %u is empty", s_slot + 1u);
    } else {
        say(" Not loaded: %.26s", snapshot_status_str(st));
    }
}

/* Apply and restart (§12): a power-on of the staged machine, or the
 * running one left as it is, and the status row says why. A new ROM is
 * checked on the card before anything is touched; core 0 does the
 * power-on with the image left in g_boot.image (handoff.h). */
static void apply_machine(void) {
    oric_config_t cfg = s.m->cfg;
    cfg.rom = s.st_rom;
    cfg.ram = s.st_ram;
    cfg.microdisc = s.st_md;
    cfg.vsync_hack = s.st_vs;
    oric_config_t was = s.m->cfg;
    if (!stage_roms(&cfg)) return;
    log_core1("  machine      : the %s%s%s, staged; restarting as the %s%s%s\n",
              roms_machine_name(was.rom, was.ram),
              was.microdisc ? " with the Microdisc" : "",
              was.vsync_hack ? " with the VSync hack" : "",
              roms_machine_name(cfg.rom, cfg.ram), cfg.microdisc ? " with the Microdisc" : "",
              cfg.vsync_hack ? " with the VSync hack" : "");
    g_ui.restart_cfg = cfg;
    g_ui.restart = true;
    s.done = true;             /* straight into the new machine */
}

static void open_item(void) {
    s.status[0] = 0;
    switch (s.item) {
    case I_TAPES:   open_tapes(); break;
    case I_DISCS:   open_discs(); break;
    case I_SNAPS:   open_snaps(); break;
    case I_SETUP:   s.page = P_SETUP; s.setup_sel = D_STATUS; break;
    case I_MACHINE: open_machine(); break;
    case I_RESET:   g_ui.reset = true; s.done = true; break;
    case I_SAVE:    save_settings(); break;
    case I_ABOUT:   open_about(); break;
    }
}

/* ---- keys ------------------------------------------------------------------- */

static void key_tapes(uint8_t c) {
    int last = T_FIRST + (int)s.n_tapes - 1;
    switch (c) {
    case PICOCALC_KEY_UP:   if (s.tape_sel > 0) s.tape_sel--; break;
    case PICOCALC_KEY_DOWN: if (s.tape_sel < last) s.tape_sel++; break;
    case PICOCALC_KEY_ENTER:
        if (s.tape_sel == T_EJECT) {
            (void)tapeio_insert(NULL);
            say(" Tape ejected", "");
        } else if (s.tape_sel == T_PLAY) {
            /* With fast tape off, PLAY by hand, for a loader that never
             * closes the relay (tapeio.h). */
            if (g_ui.fast_tape) { say(" CLOAD plays the tape", ""); return; }
            bool on = !tapeio_playing();
            const char *err = tapeio_play(on);
            if (err) { say(" Not played: %.20s", err); return; }
            say(on ? " Playing" : " Stopped", "");
            return;
        } else if (s.tape_sel == T_REWIND) {
            if (!tapeio_inserted()[0]) { say(" The deck is empty", ""); return; }
            tapeio_rewind();
            say(" Rewound", "");
            return;
        } else if (s.tape_sel == T_NEW) {
            if (!s.card) { say(" No card", ""); return; }
            const char *err = tapeio_new();
            if (err) { say(" No new tape: %.18s", err); return; }
            say(" In: %.14s, CSAVE onto it", base(tapeio_inserted()));
            s.n_tapes = tapeio_list(s_list, ORIC_TAPE_LIST_MAX);
            return;
        } else {
            const tapeio_entry_t *e = &s_list[s.tape_sel - T_FIRST];
            const char *err = tapeio_insert(e->path);
            if (err) { say(" Not inserted: %.16s", err); return; }
            if (e->name[0]) say(" In: CLOAD\"%.16s\"", e->name);
            else say(" In the deck: %.24s", base(e->path));
        }
        s.page = P_MAIN;
        return;
    case PICOCALC_KEY_ESC:
        s.page = P_MAIN;
        return;
    }
    if (s.tape_sel < s.tape_top) s.tape_top = s.tape_sel;
    if (s.tape_sel >= s.tape_top + TAPE_ROWS) s.tape_top = s.tape_sel - TAPE_ROWS + 1;
}

static void key_discs(uint8_t c) {
    switch (c) {
    case PICOCALC_KEY_UP:   if (s.disc_sel > 0) s.disc_sel--; break;
    case PICOCALC_KEY_DOWN: if (s.disc_sel < (int)s.n_discs) s.disc_sel++; break;
    case PICOCALC_KEY_LEFT:
    case PICOCALC_KEY_RIGHT:
        s_drive = (s_drive + (c == PICOCALC_KEY_RIGHT ? 1u : ORIC_DISC_DRIVES - 1u)) %
                  ORIC_DISC_DRIVES;
        s.status[0] = 0;
        break;
    case PICOCALC_KEY_ENTER:
        if (!s.card) { say(" No card", ""); return; }
        if (s.disc_sel == 0) {
            (void)discio_insert(s.m, s_drive, NULL);
            snprintf(s.status, sizeof s.status, " Drive %c empty", 'A' + s_drive);
        } else {
            const discio_entry_t *e = &s_discs[s.disc_sel - 1];
            /* An image is in one drive at a time, as pico-atom's is. */
            char was = drive_of(e->path);
            if (was != ' ' && was != (char)('A' + s_drive))
                (void)discio_insert(s.m, (unsigned)(was - 'A'), NULL);
            const char *err = discio_insert(s.m, s_drive, e->path);
            if (err) { say(" Not inserted: %.24s", err); return; }
            snprintf(s.status, sizeof s.status, " In drive %c: %.24s", 'A' + s_drive,
                     base(e->path));
        }
        s.page = P_MAIN;
        return;
    case PICOCALC_KEY_ESC:
        s.page = P_MAIN;
        return;
    }
    if (s.disc_sel < s.disc_top) s.disc_top = s.disc_sel;
    if (s.disc_sel >= s.disc_top + DISC_ROWS) s.disc_top = s.disc_sel - DISC_ROWS + 1;
}

static void key_snaps(uint8_t c) {
    switch (c) {
    case PICOCALC_KEY_UP:   if (s.snap_sel > 0) s.snap_sel--; break;
    case PICOCALC_KEY_DOWN: if (s.snap_sel < N_COUNT - 1) s.snap_sel++; break;
    case PICOCALC_KEY_LEFT:
    case PICOCALC_KEY_RIGHT:
        s_slot = (s_slot + (c == PICOCALC_KEY_RIGHT ? 1u : SNAPIO_SLOTS - 1u)) % SNAPIO_SLOTS;
        s.status[0] = 0;
        break;
    case PICOCALC_KEY_ENTER:
        if (!s.card) { say(" No card", ""); break; }
        s.status[0] = 0;
        if (s.snap_sel == N_SAVE) {
            say(" Saving...", "");
            draw();
            uint32_t us;
            snap_status_t st = snapio_save(s.m, s_slot, &us);
            log_core1("  snapshot     : save slot %u: %s, %lu us\n", s_slot + 1u,
                      snapshot_status_str(st), (unsigned long)us);
            if (st == SNAP_OK) snprintf(s.status, sizeof s.status, " Saved in slot %u", s_slot + 1u);
            else say(" Not saved: %.24s", snapshot_status_str(st));
            refresh_slots();
        } else if (s.snap_sel == N_LOAD) {
            say(" Loading...", "");
            draw();
            s.status[0] = 0;
            snap_load();
        } else if (s.snap_sel == N_DELETE) {
            bool gone = snapio_delete(s_slot);
            log_core1("  snapshot     : delete slot %u: %s\n", s_slot + 1u,
                      gone ? "deleted" : "nothing there");
            say(gone ? " Deleted" : " Nothing to delete", "");
            refresh_slots();
        }
        break;
    case PICOCALC_KEY_ESC:
        s.page = P_MAIN;
        break;
    }
}

static void key_main(uint8_t c) {
    switch (c) {
    case PICOCALC_KEY_UP:    s.item = (s.item + I_COUNT - 1) % I_COUNT; break;
    case PICOCALC_KEY_DOWN:  s.item = (s.item + 1) % I_COUNT; break;
    case PICOCALC_KEY_ENTER: open_item(); break;
    case PICOCALC_KEY_ESC:   s.done = true; break;
    }
}

/* Standard, then keymapio's list, round and round, as pico-atom's Keys
 * row goes. */
static void cycle_keys(int dir) {
    int n = (int)keymapio_count() + 1, at = 0;
    for (unsigned i = 0; i < keymapio_count(); i++)
        if (keymapio_get(i) == g_ui.layout) at = (int)i + 1;
    at = (at + n + dir) % n;
    keymapio_choose(at ? keymapio_get((unsigned)(at - 1)) : NULL);
}

static void set_backlight(int dir) {
    int v = (int)g_ui.backlight + dir;
    v = v < 1 ? 1 : v > 15 ? 15 : v;
    g_ui.backlight = (unsigned)v;
    s_bkl_set = true;
    (void)sb_write(SB_REG_BKL, (uint8_t)(v * (int)BKL_STEP), NULL);
}

static void key_setup(uint8_t c) {
    int dir = 0;
    switch (c) {
    case PICOCALC_KEY_UP:    s.setup_sel = (s.setup_sel + D_COUNT - 1) % D_COUNT; return;
    case PICOCALC_KEY_DOWN:  s.setup_sel = (s.setup_sel + 1) % D_COUNT; return;
    case PICOCALC_KEY_LEFT:  dir = -1; break;
    case PICOCALC_KEY_RIGHT: dir = 1; break;
    case PICOCALC_KEY_ENTER: dir = 0; break;
    case PICOCALC_KEY_ESC:   s.page = P_MAIN; return;
    default: return;
    }
    s.status[0] = 0;
    switch (s.setup_sel) {
    /* Core 1 draws the lines; they show once the menu closes. */
    case D_STATUS: g_ui.status = !g_ui.status; break;
    case D_PERF:   g_ui.perf_line = !g_ui.perf_line; break;
    case D_BACKLIGHT:
        if (dir) set_backlight(dir);
        break;
    case D_VOLUME:
        if (dir) {
            int v = (int)g_ui.volume + dir;
            g_ui.volume = (unsigned)(v < 0 ? 0 : v > 8 ? 8 : v);
        }
        break;
    case D_KEYS:
        if (dir) cycle_keys(dir);
        break;
    case D_FAST:
        /* Off, the deck plays the signal and the trap is its cue
         * (tapeio.h); core 0 takes it when the menu closes. */
        g_ui.fast_tape = !g_ui.fast_tape;
        tapeio_mode();
        log_core1("  menu         : fast tape %s\n", on_off(g_ui.fast_tape));
        break;
    }
}

static void key_machine(uint8_t c) {
    switch (c) {
    case PICOCALC_KEY_UP:   s.machine_sel = (s.machine_sel + M_COUNT - 1) % M_COUNT; break;
    case PICOCALC_KEY_DOWN: s.machine_sel = (s.machine_sel + 1) % M_COUNT; break;
    case PICOCALC_KEY_LEFT:
    case PICOCALC_KEY_RIGHT:
        switch (s.machine_sel) {
        case M_ROM: s.st_rom = s.st_rom == ROM_BASIC10 ? ROM_BASIC11 : ROM_BASIC10; break;
        case M_RAM: s.st_ram = s.st_ram == ORIC_RAM_16K ? ORIC_RAM_48K : ORIC_RAM_16K; break;
        case M_DISC: s.st_md = !s.st_md; break;
        case M_VSYNC: s.st_vs = !s.st_vs; break;
        default: return;
        }
        say(machine_staged() ? " Apply restarts: program lost" : "", "");
        break;
    case PICOCALC_KEY_ENTER:
        if (s.machine_sel == M_APPLY) {
            if (machine_staged()) apply_machine();
            else say(" Nothing to apply", "");
        }
        break;
    case PICOCALC_KEY_ESC:
        /* Nothing changes until Apply. */
        say(machine_staged() ? " Not applied" : "", "");
        s.page = P_MAIN;
        break;
    }
}

/* F6 takes one screenshot a press. The MCU's auto-repeat arrives as more
 * presses, and the SD write polls the keyboard, so it rearms only on the
 * release (hardware-notes.md §6.2), which is F1's if Shift went first.
 * True when this event is F6 going down afresh. */
static bool s_shot_down;

static bool shot_press(uint8_t st, uint8_t c) {
    if (keymap_picocalc_canonical(c) != keymap_picocalc_canonical(PICOCALC_KEY_F6)) return false;
    if (st == KEY_EV_RELEASED) s_shot_down = false;
    if (st != KEY_EV_PRESSED || c != PICOCALC_KEY_F6 || s_shot_down) return false;
    s_shot_down = true;
    return true;
}

/* Presses only: releases and the MCU's held reports move nothing. Alt is
 * tracked so that Alt+M closes the menu as it opened it. */
static void keys(void) {
    uint8_t st, c;
    while (!s.done && (kbd_pop(&st, &c) || kbd_pop_uart(&st, &c))) {
        if (c == PICOCALC_KEY_ALT) { s.alt = st != KEY_EV_RELEASED; continue; }
        bool shoot = shot_press(st, c);
        if (st != KEY_EV_PRESSED) continue;
        if (s.alt && (c == 'm' || c == 'M')) { s.done = true; break; }
        /* F6 on any page: the page as it is, then its status row says
         * how it went. Its repeats do nothing. */
        if (c == PICOCALC_KEY_F6) {
            if (shoot) {
                say(" %s", shotio_take(s.card));
                draw();
            }
            continue;
        }
        switch (s.page) {
        case P_SETUP:   key_setup(c); break;
        case P_MACHINE: key_machine(c); break;
        case P_TAPES:   key_tapes(c); break;
        case P_SNAPS:   key_snaps(c); break;
        case P_DISCS:   key_discs(c); break;
        case P_ABOUT:
        case P_HELP:
            if (c == PICOCALC_KEY_ESC || c == PICOCALC_KEY_ENTER) s.page = P_MAIN;
            break;
        default:        key_main(c); break;
        }
        /* A page a key opened goes back to the guest, not the main page. */
        if (s.direct && s.page == P_MAIN) s.done = true;
        if (!s.done) draw();
    }
}

void menu_run(oric_t *m, unsigned page, bool alt) {
    memset(&s, 0, sizeof s);
    s.m = m;
    s_shot_down = false;
    s.alt = alt;       /* Alt+M or Alt+H has it held; the function keys do not */

    s.card = sd_present() && storage_mount() == 0;
    if (!s.card) say(" No card: no tapes", "");
    /* The card's layouts afresh: a file may have been added or edited
     * on a computer since (keymapio.h). */
    if (s.card) keymapio_scan();
    /* A layout a file chose says so (§9.4), then the tape's last word,
     * then the first problem (§12): the running ROM, the settings file,
     * the layouts' files. */
    if (g_ui.layout && keymapio_chosen_by()[0]) say(" Keys chosen by %.16s", keymapio_chosen_by());
    const char *t = tapeio_said();
    if (!s.status[0] && t[0]) say("%s", t);
    if (!s.status[0] && !s_rom_known)
        say(" %.18s: unrecognised image", romset_images[m->cfg.rom].file);
    if (!s.status[0] && settingsio_error()[0]) say(" %.38s", settingsio_error());
    if (!s.status[0] && keymapio_error()[0]) say(" %.38s", keymapio_error());

    /* Opened by a function key or Alt+H: that page, keeping what the
     * status row says, and closing it closes the menu. */
    s.direct = page != KM_PAGE_MAIN;
    char said[sizeof s.status];
    memcpy(said, s.status, sizeof said);
    switch (page) {
    case KM_PAGE_TAPE:     s.item = I_TAPES; open_item(); break;
    case KM_PAGE_DISC:     s.item = I_DISCS; open_item(); break;
    case KM_PAGE_SNAPSHOT: s.item = I_SNAPS; open_item(); break;
    case KM_PAGE_SETUP:    s.item = I_SETUP; open_item(); break;
    case KM_PAGE_MACHINE:  s.item = I_MACHINE; open_item(); break;
    case KM_PAGE_ABOUT:    s.item = I_ABOUT; open_item(); break;
    case KM_PAGE_HELP:     s.page = P_HELP; break;
    default: break;
    }
    if (!s.status[0]) memcpy(s.status, said, sizeof said);

    s.battery = -1;
    (void)read_battery();

    /* The lines describe the running machine, and the menu can change
     * what they say, so they are hidden while it is open; each is drawn
     * only when its text changes, so the first present after it closes
     * draws them afresh. */
    display_perf("");
    display_status("");

    log_core1("  menu         : open at page %u%s\n", page, s.card ? "" : " (no card)");
    uint32_t t0 = time_us_32();
    draw();
    log_core1("  menu         : drawn in %lu us\n", (unsigned long)(time_us_32() - t0));

    uint32_t last_poll = time_us_32(), last_bat = last_poll;
    while (!s.done) {
        if (time_us_32() - last_poll >= POLL_US) {
            last_poll = time_us_32();
            g_c1.key_events += kbd_poll();
            g_c1.polls++;
            keys();
            if (!s.done && last_poll - last_bat >= BAT_POLL_US) {
                last_bat = last_poll;
                bool changed = read_battery();
                if (s.page == P_ABOUT) {
                    int tc = board_temp_c();
                    changed |= tc != s.temp_c;
                    s.temp_c = tc;
                }
                if (changed) draw();
            }
        }
        log_pump();
        /* Never sleep_us on core 1 (hardware-notes.md §9.7). */
        busy_wait_us_32(500);
    }

    if (s.card) storage_unmount();
    display_invalidate();
    log_core1("  menu         : closed\n");
}

/* ---- pause ------------------------------------------------------------------ */

/* The menu's page for a key, or -1: Alt+M, Alt+H and the function keys
 * (§12), from the same table the guest's keys come from. */
static int menu_key(bool alt, uint8_t c) {
    for (size_t i = 0; i < keymap_picocalc_len; i++) {
        const keymap_t *e = &keymap_picocalc[i];
        if (!(e->flags & KM_MENU)) continue;
        bool layer = (e->flags & KM_ALT) != 0;
        if (layer && alt && (c == e->code || c == e->code + ('a' - 'A'))) return e->row;
        if (!layer && c == e->code) return e->row;
    }
    return -1;
}

int pause_run(bool alt_held, bool *alt_out) {
    /* The status line says so, whether or not it is on, as pico-atom's
     * does. */
    char line[ORIC_TEXT_COLS + 1];
    status_paused_format(line);
    display_status(line);

    /* The level may be the southbridge's own, so it is read, and written
     * back on resume (§12). */
    uint8_t r[2] = { 0, 0 };
    bool read = sb_read(SB_REG_BKL, r) == SB_OK;
    uint8_t level = read ? r[1] : BKL_LOWEST;
    bool dimmed = read && level != BKL_LOWEST && sb_write(SB_REG_BKL, BKL_LOWEST, NULL) == SB_OK;
    log_core1("  pause        : paused, backlight %s\n",
              !read ? "unread, left" : dimmed ? "dimmed" : "already lowest");

    /* As it was asked for: Alt+P with Alt held, so that P's repeat does
     * not resume; the UART's US without, so that any key does. */
    bool alt = alt_held, done = false;
    s_shot_down = false;
    int page = -1;
    uint32_t last_poll = time_us_32();
    while (!done) {
        if (time_us_32() - last_poll >= POLL_US) {
            last_poll = time_us_32();
            g_c1.key_events += kbd_poll();
            g_c1.polls++;
            uint8_t st, c;
            while (!done && (kbd_pop(&st, &c) || kbd_pop_uart(&st, &c))) {
                if (c == PICOCALC_KEY_ALT) { alt = st != KEY_EV_RELEASED; continue; }
                bool shoot = shot_press(st, c);
                if (st != KEY_EV_PRESSED) continue;
                if (c == PICOCALC_KEY_CTRL || c == PICOCALC_KEY_SHIFT_L ||
                    c == PICOCALC_KEY_SHIFT_R) continue;
                if (alt && (c == 'P' || c == 'p')) continue;
                /* F6 takes the paused frame and stays paused, saying how
                 * it went where Paused was; its repeats neither take
                 * another nor resume. */
                if (c == PICOCALC_KEY_F6) {
                    if (shoot) {
                        char said[ORIC_TEXT_COLS + 1];
                        snprintf(said, sizeof said, "Paused: %s", shotio_take(false));
                        display_status(said);
                    }
                    continue;
                }
                page = menu_key(alt, c);
                done = true;
            }
        }
        log_pump();
        busy_wait_us_32(500);
    }

    if (dimmed) (void)sb_write(SB_REG_BKL, level, NULL);
    display_status("");   /* the next present draws the line as it is */
    log_core1("  pause        : resumed%s\n", page >= 0 ? " into the menu" : "");
    *alt_out = alt;
    return page;
}
