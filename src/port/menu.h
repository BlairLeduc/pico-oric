/* menu.h — the emulator's menu and pause (design.md §12; EL §10).
 *
 * Core 1, with the guest parked (park.h): the machine is core 1's for as
 * long as either lasts. The keyboard is core 1's too: it polls the
 * southbridge and takes the events itself, since core 0, their usual
 * consumer, is parked. What the menu changes for core 0 (the volume, a
 * reset, a restart) goes through g_ui, which core 0 applies when it has
 * the machine back (EL §2.5).
 *
 * The menu is a text page in the Oric's own font through the ULA's row
 * generator (textpage.h), and closing it invalidates the presenter, so
 * the next snapshot is drawn whole. Its pages and rows are the family's,
 * pico-atom's (menu.c says where they differ). Its status row names the
 * first problem: the running ROM unrecognised, then the settings file's
 * first bad line.
 *
 * Adapted from pico-ace's.
 */
#ifndef PICO_ORIC_MENU_H
#define PICO_ORIC_MENU_H

#include <stdbool.h>

#include "oric.h"
#include "settings.h"
#include "ula.h"

/* What the settings file said at boot, kept for the save: what the menu
 * does not set is saved from here (EL §8.7). `page` is the frame the
 * menu draws into, core 1's, shared with the missing-ROM page, which is
 * never up at the same time. `rom` is the running ROM, and `rom_known`
 * whether its SHA-1 was its image's. Core 1, at boot. */
void menu_init(const settings_t *file, oric_frame_t *page, rom_id_t rom, bool rom_known);

/* Run the menu until it is closed. `page` is KM_PAGE_MAIN or the page a
 * function key or Alt+H asked for, which closing then returns from to
 * the guest. `alt` is whether Alt was down when it was asked for, so
 * that Alt+M closes it only as a chord. */
void menu_run(oric_t *m, unsigned page, bool alt);

/* Pause: the guest's last frame stays, the backlight is dimmed, and the
 * status line says Paused, whether or not it is on. Any key resumes and
 * is not typed; a modifier alone does not, nor does the pause chord's
 * own repeat. Alt+M, Alt+H and the function keys go to the menu instead,
 * but for F6, which takes a screenshot and stays paused: returns -1 to
 * resume, or the page, with whether Alt was down in *alt. */
int pause_run(bool *alt);

/* The status line's word on the running ROM (§12): "" when its SHA-1 is
 * its image's, else the file named as unrecognised. */
const char *menu_rom_problem(void);

#endif /* PICO_ORIC_MENU_H */
