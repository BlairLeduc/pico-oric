/* main.c — bring-up, then the two cores' loops (design.md §4.1, §15.2 M7).
 *
 * Core 0 owns the 6502 and the machine; core 1 owns the LCD, the
 * southbridge, the card and the log's way out (§4.3). main() sets the
 * clock, logs the banner, starts core 1 and waits while it brings up the
 * panel, reads the card's settings, names the machine from them (the
 * Atmos 48K by default, §18 item 3; PICO_ORIC_BOOT_ROM and _RAM over the
 * file, handoff.h) and loads its ROM off the card (§10.2), then powers
 * the machine on and becomes core 0's loop. Without
 * the ROM, core 1 shows the page that says why, and reads the card again
 * when one goes in; with the other machine's ROM there, RETURN starts
 * that machine instead.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "audio.h"
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
#include "settingsio.h"

/* The guest lives in .bss, not the heap: src/core/ has no allocator, and
 * keeping it static is what makes the §3.3 budget a link-time fact. */
static oric_t      g_oric;
static keymatrix_t g_keys;   /* core 0's, like g_oric */

/* Whether RETURN came, from the keyboard or the UART. */
static bool return_pressed(void) {
    bool hit = false;
    uint8_t state, code;
    while (kbd_pop(&state, &code))
        if (state == KEY_EV_PRESSED && code == PICOCALC_KEY_ENTER) hit = true;
#if PICO_ORIC_UART
    int c = getchar_timeout_us(0);
    if (c == '\r' || c == '\n') hit = true;
#endif
    return hit;
}

/* Wait for the machine's ROM: at once if the card had it, else, with the
 * missing-ROM page up (core 1's), until a card put in has it, or, while the page offers the other machine,
 * for RETURN; then claim the image from core 1 (handoff.h). Returns the
 * ROM to start. */
static rom_id_t wait_for_rom(void) {
    rom_id_t want = g_boot.want, other = card_other_basic(want);
    uint32_t seen = g_boot.generation - 1u;
    for (;;) {
        uint32_t gen = g_boot.generation;
        __dmb();
        rom_id_t loaded = g_boot.job.loaded;
        if (gen != seen && loaded != want) {
            seen = gen;
            log_printf("  guest        : not started: no %s for the %s%s\n",
                       romset_images[want].file, roms_machine_name(want, g_boot.ram),
                       loaded == other ? "; RETURN starts the other machine" : "");
        }
        bool enter = return_pressed();
        rom_id_t pick = loaded == want ? want : (enter && loaded == other) ? other : ROM_UNKNOWN;
        if (pick != ROM_UNKNOWN) {
            g_boot.claimed = true;
            __dmb();
            while (g_boot.busy) tight_loop_contents();
            __dmb();
            /* A job may have finished between the look and the claim. */
            if (g_boot.job.loaded == pick) return pick;
            g_boot.claimed = false;
            __dmb();
        }
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

    /* The machine core 1 named from the settings (handoff.h). */
    const settings_t *st = &g_boot.settings;
    const char *err = settingsio_error();
    log_printf("  settings     : card %s, file %s%s%s, %lu us; rom %s, ram %s, microdisc %s, "
               "vsync_hack %s, volume %u, perf %s, status %s, backlight %u, layout %s, fast_tape %s, "
               "boot_tape %s, boot_disc %s; booting the %s%s\n",
               card_state_str(g_boot.job.state), settingsio_state_str(settingsio_state()),
               err[0] ? ": " : "", err, (unsigned long)g_boot.job.settings_us,
               settings_rom_str(st->rom), settings_ram_str(st->ram), st->microdisc ? "on" : "off",
               st->vsync_hack ? "on" : "off", st->volume, st->perf ? "on" : "off", st->status ? "on" : "off", st->backlight,
               g_ui.layout ? g_ui.layout->name : "standard", st->fast_tape ? "on" : "off",
               st->boot_tape[0] ? st->boot_tape : "none", st->boot_disc[0] ? st->boot_disc : "none",
               roms_machine_name(g_boot.want, g_boot.ram),
               g_boot.want != st->rom || g_boot.ram != st->ram ? " (the build's, over the file)" : "");
    oric_config_t cfg;
    oric_config_default(&cfg);
    cfg.rom = g_boot.want;
    cfg.ram = g_boot.ram;
    cfg.microdisc = g_boot.microdisc;
    cfg.vsync_hack = g_boot.vsync_hack;

    /* Returns at once when the ROM is already here. */
    bool page = g_boot.job.loaded != cfg.rom;
    cfg.rom = wait_for_rom();
    if (page) {
        log_printf("  guest        : %s\n", cfg.rom == g_boot.want
                   ? "its ROM is on the card now" : "RETURN: the other machine instead");
        /* The page's keys are not the guest's. */
        keymatrix_init(&g_keys);
    }

    /* Audio last in bring-up order (hardware-notes.md §10), on core 0,
     * whose IRQ the refill is, and after core 1's LCD has claimed its
     * fixed DMA channel; before the power-on, which gives the pcm the
     * rate the PWM really has. */
#if PICO_ORIC_AUDIO
    audio_init();
#endif
    core0_power_on(&g_oric, &cfg, g_boot.image);
    log_printf("  guest        : %s%s%s, ROM %s%s, %lu cycles a field at %lu Hz, "
               "hot code in SRAM to tier %u (hot.h)\n",
               roms_machine_name(cfg.rom, cfg.ram), cfg.microdisc ? " with the Microdisc" : "",
               cfg.vsync_hack ? " with the VSync hack" : "",
               romset_images[cfg.rom].file, g_boot.job.loaded_known ? "" : " (UNRECOGNISED)",
               (unsigned long)oric_field_cycles(&g_oric), (unsigned long)ORIC_CPU_HZ,
               (unsigned)PICO_ORIC_RAM_TIER);

#if PICO_ORIC_AUDIO
    uint32_t rate_num, rate_den;
    audio_rate(&rate_num, &rate_den);
    log_printf("  audio        : PWM GP26/GP27, %lu/%lu Hz (%lu.%02lu kHz), "
               "%u cycles per %u samples, tones below TP %u averaged, ring %u slots, "
               "queue %u\n",
               (unsigned long)rate_num, (unsigned long)rate_den,
               (unsigned long)(rate_num / rate_den / 1000u),
               (unsigned long)(rate_num / rate_den % 1000u / 10u),
               (unsigned)g_oric.pcm.num, (unsigned)g_oric.pcm.den, (unsigned)g_oric.ay.avg_tp,
               (unsigned)ORIC_DMA_RING_SLOTS, (unsigned)ORIC_PCM_QUEUE_LEN);
#else
    log_printf("  audio        : off; pacing on the microsecond timer\n");
#endif

    core0_run(&g_oric, &g_keys);
}
