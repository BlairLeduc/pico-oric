/* main.c — bring-up, then the two cores' loops (design.md §4.1, §15.2 M7).
 *
 * Core 0 owns the 6502 and the machine; core 1 owns the LCD, the
 * southbridge, the card and the log's way out (§4.3). main() sets the
 * clock, logs the banner, names the machine (the Atmos 48K by default,
 * §18 item 3, or PICO_ORIC_BOOT_ROM and _RAM), starts core 1 and waits
 * while it brings up the panel and loads that machine's ROM off the card
 * (§10.2), then powers the machine on and becomes core 0's loop. Without
 * the ROM, core 1 shows the page that says why; with the other machine's
 * ROM there, RETURN starts that machine instead.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "board.h"
#include "card.h"
#include "core0.h"
#include "core1.h"
#include "handoff.h"
#include "kbd.h"
#include "keymatrix.h"
#include "log.h"
#include "oric.h"
#include "pico_oric_version.h"
#include "roms.h"

/* The guest lives in .bss, not the heap: src/core/ has no allocator, and
 * keeping it static is what makes the §3.3 budget a link-time fact. */
static oric_t      g_oric;
static keymatrix_t g_keys;   /* core 0's, like g_oric */

/* The machine a build boots (§4.1): PICO_ORIC_BOOT_ROM 10 or 11 and
 * PICO_ORIC_BOOT_RAM 16 or 48, over the default. M9's settings file
 * comes between the two. */
static void boot_config(oric_config_t *cfg) {
    oric_config_default(cfg);
#ifdef PICO_ORIC_BOOT_ROM
    cfg->rom = PICO_ORIC_BOOT_ROM == 10 ? ROM_BASIC10 : ROM_BASIC11;
#endif
#ifdef PICO_ORIC_BOOT_RAM
    cfg->ram = PICO_ORIC_BOOT_RAM == 16 ? ORIC_RAM_16K : ORIC_RAM_48K;
#endif
}

/* The missing-ROM page is up (core 1's). Wait for RETURN, from the
 * keyboard or the UART, if the page offers the other machine; otherwise
 * for ever, until a reset with the ROM on the card. Returns the ROM to
 * start. */
static rom_id_t wait_for_other(void) {
    rom_id_t other = card_other_basic(g_boot.want);
    bool offered = g_boot.job.loaded == other;
    log_printf("  guest        : not started: no %s for the %s%s\n",
               romset_images[g_boot.want].file, roms_machine_name(g_boot.want, g_boot.ram),
               offered ? "; RETURN starts the other machine" : "");
    for (;;) {
        uint8_t state, code;
        while (kbd_pop(&state, &code)) {
            if (offered && state == KEY_EV_PRESSED && code == PICOCALC_KEY_ENTER) return other;
        }
#if PICO_ORIC_UART
        int c = getchar_timeout_us(0);
        if (offered && (c == '\r' || c == '\n')) return other;
#endif
        sleep_ms(10);
    }
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

    oric_config_t cfg;
    boot_config(&cfg);
    g_boot.want = cfg.rom;
    g_boot.ram = cfg.ram;
    keymatrix_init(&g_keys);
    handoff_init();

    /* Core 1 brings up the panel and reads the card while core 0 waits
     * (§4.5). */
    multicore_launch_core1(core1_main);
    while (!g_c1.ready) sleep_ms(1);
    __dmb();

    /* From here on core 0 logs through the ring core 1 drains. */
    log_printf("  i2c          : %lu Hz, southbridge version %ld, a register read %lu us\n",
               (unsigned long)g_c1.i2c_hz, (long)g_c1.sb_version,
               (unsigned long)g_bringup.i2c_us);
    log_printf("  lcd          : spi %lu Hz\n", (unsigned long)g_c1.spi_hz);
    log_printf("  core 1       : started at %lu.%03lu ms, ready at %lu.%03lu ms: lcd_init %lu us, "
               "the card's ROM %lu us\n",
               (unsigned long)(g_bringup.start_us / 1000u),
               (unsigned long)(g_bringup.start_us % 1000u),
               (unsigned long)(g_boot.ready_us / 1000u), (unsigned long)(g_boot.ready_us % 1000u),
               (unsigned long)g_bringup.lcd_us, (unsigned long)g_bringup.card_us);

    if (g_boot.job.loaded != cfg.rom) {
        cfg.rom = wait_for_other();
        log_printf("  guest        : RETURN: the %s instead\n",
                   roms_machine_name(cfg.rom, cfg.ram));
        /* RETURN's release is the page's, not the guest's. */
        keymatrix_init(&g_keys);
    }

    /* oric_init powered on with the socket empty; power on again with
     * the ROM in it, for the reset vector (oric.h). */
    oric_init(&g_oric, &cfg);
    oric_load_rom(&g_oric, g_boot.image, ORIC_ROM_SIZE);
    oric_power_on(&g_oric);
    log_printf("  guest        : %s, ROM %s%s, %lu cycles a field at %lu Hz, "
               "hot code in SRAM to tier %u (hot.h)\n",
               roms_machine_name(cfg.rom, cfg.ram), romset_images[cfg.rom].file,
               g_boot.job.loaded_known ? "" : " (UNRECOGNISED)",
               (unsigned long)oric_field_cycles(&g_oric), (unsigned long)ORIC_CPU_HZ,
               (unsigned)PICO_ORIC_RAM_TIER);
    /* M8: audio, last in bring-up order (hardware-notes.md §10). */

    core0_run(&g_oric, &g_keys);
}
