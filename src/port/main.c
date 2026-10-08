/* main.c — bring-up, then core 0's loop (design.md §4.1, §15.2 M6).
 *
 * Core 1 owns the LCD, the southbridge, the card and the log's way out
 * (§4.3). main() sets the clock, logs the banner, starts core 1 and waits
 * while it brings up the panel and lists the card's ROMs, then becomes
 * core 0's loop. M6 has no guest: core 0 logs every key event, from the
 * keyboard and from the UART alike, and a heartbeat.
 *
 * M7: the Oric, its ROMs loaded from the card, takes core 0's loop.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "board.h"
#include "core1.h"
#include "handoff.h"
#include "kbd.h"
#include "keymatrix.h"
#include "log.h"
#include "pico_oric_version.h"
#include "southbridge.h"

/* The heartbeat's period (design.md §14). */
#define HEARTBEAT_US 5000000u

static const char *state_str(uint8_t state) {
    switch (state) {
    case KEY_EV_PRESSED:  return "pressed ";
    case KEY_EV_HELD:     return "held    ";
    case KEY_EV_RELEASED: return "released";
    }
    return "?       ";
}

/* One event as the log shows it: where it came from, its state and code,
 * and the cell the standard map gives its key (§9.2), so the map can be
 * checked on the device key by key. */
static void log_key(const char *from, uint8_t state, uint8_t code) {
    char ch[4] = "   ";
    if (code >= 0x20u && code < 0x7Fu) { ch[0] = '\''; ch[1] = (char)code; ch[2] = '\''; }
    /* The modifiers are held lines, not table entries (§9.2). */
    static const struct { uint8_t code, row; const char *name; } mods[] = {
        { PICOCALC_KEY_SHIFT_L, OK_ROW_SHIFT_L, "left SHIFT" },
        { PICOCALC_KEY_SHIFT_R, OK_ROW_SHIFT_R, "right SHIFT" },
        { PICOCALC_KEY_CTRL,    OK_ROW_CTRL,    "CTRL" },
    };
    for (size_t i = 0; i < sizeof mods / sizeof mods[0]; i++) {
        if (code != mods[i].code) continue;
        log_printf("  key          : %s %s 0x%02X     -> row %u col %u, %s\n", from,
                   state_str(state), code, mods[i].row, OK_COL_MODS, mods[i].name);
        return;
    }
    if (code == PICOCALC_KEY_ALT) {
        log_printf("  key          : %s %s 0x%02X     -> no cell, the Alt layer\n", from,
                   state_str(state), code);
        return;
    }
    uint8_t canon = keymap_picocalc_canonical(code);
    for (size_t i = 0; i < keymap_picocalc_len; i++) {
        const keymap_t *e = &keymap_picocalc[i];
        if (e->code != canon || (e->flags & KM_ALT)) continue;
        if (e->flags & KM_NOCELL) {
            log_printf("  key          : %s %s 0x%02X %s -> no cell, flags 0x%02X\n", from,
                       state_str(state), code, ch, e->flags);
        } else {
            log_printf("  key          : %s %s 0x%02X %s -> row %u col %u%s\n", from,
                       state_str(state), code, ch, e->row, e->col,
                       (e->flags & KM_SHIFT) ? " +SHIFT" : "");
        }
        return;
    }
    log_printf("  key          : %s %s 0x%02X %s -> not in the map\n", from,
               state_str(state), code, ch);
}

/* UART1's bytes as the PicoCalc's events for them (keymap_picocalc_text),
 * so that a key typed with tools/uart-type.sh arrives as the same event
 * as the keyboard's (§15.2 M6). */
static void uart_keys(void) {
#if PICO_ORIC_UART
    int c = getchar_timeout_us(0);
    if (c == PICO_ERROR_TIMEOUT) return;
    picocalc_event_t ev[ORIC_KEY_TEXT_EVENTS];
    unsigned n = keymap_picocalc_text((uint8_t)c, ev);
    if (n == 0) log_printf("  key          : uart byte 0x%02X sends no key\n", (unsigned)c);
    for (unsigned i = 0; i < n; i++) log_key("uart", ev[i].state, ev[i].code);
#endif
}

int main(void) {
    /* The clock first, so the UART's divider is worked out from the
     * clock that stays. */
    bool clocks_ok = board_init_clocks();
    stdio_init_all();
    board_identify(&g_board);

    /* A startup banner plus consecutive heartbeats is more useful boot
     * evidence than a single line (hardware-notes.md §2.7). Core 1 is not
     * running yet, so printf may block. */
    board_log_banner(&g_board);
    printf("  firmware     : %s\n", PICO_ORIC_VERSION);
    if (!clocks_ok) {
        printf("  WARNING: clk_sys is not at 150 MHz; SPI and audio rates will "
               "not be the ones this build assumes\n");
    }

    /* Core 1 brings up the panel and reads the card while core 0 waits
     * (§4.5). */
    multicore_launch_core1(core1_main);
    while (!g_c1.ready) sleep_ms(1);
    __dmb();

    /* From here on core 0 logs through the ring core 1 drains. */
    log_printf("  i2c          : %lu Hz, southbridge version %ld, a register read %lu us\n",
               (unsigned long)g_c1.i2c_hz, (long)g_c1.sb_version,
               (unsigned long)g_bringup.i2c_us);
    log_printf("  lcd          : spi %lu Hz; 240x224 fill %lu us, 240x224 rows %lu us, "
               "one 240-pixel row %lu us\n",
               (unsigned long)g_c1.spi_hz, (unsigned long)g_bringup.fill_us,
               (unsigned long)g_bringup.blit_us, (unsigned long)g_bringup.row_us);

    uint32_t next_beat = time_us_32() + HEARTBEAT_US;
    for (unsigned beat = 0;;) {
        uint8_t state, code;
        while (kbd_pop(&state, &code)) log_key("kbd ", state, code);
        uart_keys();

        uint32_t now = time_us_32();
        if ((int32_t)(now - next_beat) >= 0) {
            next_beat += HEARTBEAT_US;
            log_printf("  heartbeat    : %u, polls %lu (longest %lu us), key events %lu, "
                       "i2c errors %lu, ring overflows %lu, log dropped %u, card jobs %lu, "
                       "battery %ld, die %ld C\n",
                       beat++, (unsigned long)g_c1.polls, (unsigned long)g_c1.poll_max_us,
                       (unsigned long)g_c1.key_events, (unsigned long)sb_error_count(),
                       (unsigned long)kbd_overflows(), log_dropped(),
                       (unsigned long)g_c1.card_jobs, (long)g_c1.battery,
                       (long)g_c1.temp_c);
        }
        tight_loop_contents();
    }
}
