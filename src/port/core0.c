/* core0.c — core 0's loop: the guest (design.md §4.3, §11.1, §14).
 *
 * pico-ace's loop, renamed, paced on the audio queue (M8), or with
 * PICO_ORIC_AUDIO=OFF on the timer, the control for audio's cost. The
 * park and the menu (M9) and the tape (M10) are left out, each marked
 * where it will go.
 */

#include "core0.h"

#include <stdio.h>
#include <string.h>

#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#include "audio.h"
#include "card.h"
#include "handoff.h"
#include "kbd.h"
#include "log.h"
#include "southbridge.h"
#include "ula.h"

/* The heartbeat's period (design.md §14). */
#define HEARTBEAT_US 5000000u

#if !PICO_ORIC_AUDIO
/* A field this far behind its deadline is not caught up: the schedule
 * starts again from now, and the slip is counted (EL §6.3). */
#define SLIP_US 60000u
#endif

/* FS, which no key sends, asks for the screen as text in the log
 * (tools/uart-screen.sh), so a run driven over the UART can read back
 * what the panel shows. */
#define UART_SCREEN_DUMP 0x1Cu

/* The screen's text rows: #BB80, 28 of 40 (§2.2). */
static uint8_t screen_byte(const oric_t *m, unsigned row, unsigned col) {
    return oric_peek(m, (uint16_t)(ORIC_TEXT_BASE + row * ORIC_SCREEN_COLS + col));
}

#if PICO_ORIC_UART
/* The text screen as 28 lines: printable codes as themselves, an
 * attribute as '.', and an inverse space (the cursor's cell) as '#'.
 * Other inverse cells show as their character. In hires the top 25 rows
 * are not what the panel shows; the bottom three are. */
static void dump_screen(const oric_t *m) {
    log_printf("  screen       : field %lu, mode #%02X%s\n", (unsigned long)m->fields,
               (unsigned)(0x18u | m->frame_mode),
               (m->frame_mode & ULA_MODE_HIRES) ? ", hires: rows 0-24 are not shown" : "");
    for (unsigned row = 0; row < ORIC_SCREEN_ROWS; row++) {
        char line[ORIC_SCREEN_COLS + 1];
        for (unsigned c = 0; c < ORIC_SCREEN_COLS; c++) {
            uint8_t b = screen_byte(m, row, c), ch = b & 0x7Fu;
            char out = ch >= 0x20u ? (char)ch : '.';
            if (ch == 0x7Fu) out = '~';
            if ((b & 0x80u) && ch == 0x20u) out = '#';
            line[c] = out;
        }
        line[ORIC_SCREEN_COLS] = 0;
        log_printf("  |%s|\n", line);
    }
}
#endif

/* UART1's bytes typed at the guest as the PicoCalc would send them, so a
 * hardware run can be driven from the workstation that captures it
 * (§15.2 M6, tools/uart-type.sh). A byte is taken only while keymatrix
 * has room for its events and their releases, so a fast sender loses
 * characters in the UART's FIFO rather than in the replay. */
static void uart_keys(keymatrix_t *k, const oric_t *m) {
#if PICO_ORIC_UART
    if (k->q_len + k->n_open + 2u * ORIC_KEY_TEXT_EVENTS > ORIC_KEY_EVENT_QUEUE) return;
    int ch = getchar_timeout_us(0);
    if (ch == PICO_ERROR_TIMEOUT) return;
    if (ch == UART_SCREEN_DUMP) {
        dump_screen(m);
        return;
    }
    /* M9: GS holds the guest for a card job, RS opens the menu and US
     * pauses, as pico-ace's do. */
    picocalc_event_t ev[ORIC_KEY_TEXT_EVENTS];
    unsigned n = keymap_picocalc_text((uint8_t)ch, ev);
    for (unsigned i = 0; i < n; i++) keymatrix_event(k, ev[i].state, ev[i].code);
#else
    (void)k;
    (void)m;
#endif
}

/* The ROM's prompt anywhere on the text screen: the boot reaching it,
 * as test_boot has it (§15.2 M3). */
static bool ready_shown(const oric_t *m) {
    static const char word[] = "Ready";
    for (unsigned row = 0; row < ORIC_SCREEN_ROWS; row++) {
        for (unsigned c = 0; c + sizeof word - 1u <= ORIC_SCREEN_COLS; c++) {
            unsigned i = 0;
            while (i < sizeof word - 1u && screen_byte(m, row, c + i) == (uint8_t)word[i]) i++;
            if (i == sizeof word - 1u) return true;
        }
    }
    return false;
}

/* The keys that ask the emulator rather than the guest for something
 * (design.md §12). Alt+K presses the reset button, which is NMI: the
 * ROM's warm start, program kept (§2.1). */
static void requests(keymatrix_t *k, oric_t *m) {
    if (k->reset_request) {
        oric_nmi(m);
        log_printf("  keys         : the reset button (NMI)\n");
    }
    /* M9: the menu, pause and screenshots park the guest. */
    if (k->menu_request || k->pause_request || k->shot_request)
        log_printf("  keys         : %s is not in this firmware yet (M9)\n",
                   k->menu_request ? "the menu" : k->pause_request ? "pause" : "the screenshot");
    k->reset_request = k->pause_request = k->menu_request = k->shot_request = false;
}

/* The heartbeat's battery and die: "87%, 31 C", "87% charging, 31 C",
 * with "?" for either before its first read or after a failed one. Bit 7
 * of the gauge is the charger: set proves USB power, clear proves
 * nothing (hardware-notes.md §6). The readings are core 1's. */
static const char *power_text(void) {
    static char text[48];
    int32_t b = g_c1.battery, t = g_c1.temp_c;
    char bat[20] = "?", die[16] = "?";
    if (b >= 0) snprintf(bat, sizeof bat, "%u%%%s", (unsigned)(b & 0x7F), b & 0x80 ? " charging" : "");
    if (t != INT32_MIN) snprintf(die, sizeof die, "%ld C", (long)t);
    snprintf(text, sizeof text, "%s, %s", bat, die);
    return text;
}

/* v in hundredths or thousandths, as "12.3" with one decimal. */
static void tenths(char *out, size_t n, uint32_t v10) {
    snprintf(out, n, "%lu.%lu", (unsigned long)(v10 / 10u), (unsigned long)(v10 % 10u));
}

static void hundredths(char *out, size_t n, uint32_t v100) {
    snprintf(out, n, "%lu.%02lu", (unsigned long)(v100 / 100u), (unsigned long)(v100 % 100u));
}

/* A window of the loop's own counters, for the heartbeat and the perf
 * line: both start again from the machine as it is now. */
typedef struct {
    uint64_t us;              /* wall time at the start                   */
    uint64_t cycles, insns;   /* the machine's                            */
    uint32_t fields, fields60, late;
    uint64_t run_us;          /* inside oric_run_field                    */
    uint64_t busy_us;         /* outside the pacing wait                  */
    uint64_t take_us;         /* oric_video_take, the snapshot's copy     */
    uint32_t scan_us, scan_max_us;   /* a sampled ula_scan_mode (§7.4)    */
} window_t;

static void window_start(window_t *w, const oric_t *m, uint32_t late) {
    memset(w, 0, sizeof *w);
    w->us = time_us_64();
    w->cycles = m->cpu.cycles;
    w->insns = m->instructions;
    w->fields = m->fields;
    w->late = late;
}

void core0_run(oric_t *m, keymatrix_t *k) {
    const uint32_t clk_mhz = clock_get_hz(clk_sys) / 1000000u;
    /* main() powered the guest on just before this (main.c). */
    const uint32_t power_on_us = time_us_32();
    uint32_t late = 0, slips = 0;   /* the timer's pacing; 0 on audio */
#if PICO_ORIC_AUDIO
    /* A field's samples, drained after it and pushed to the queue. */
    static int16_t pcm[ORIC_AUDIO_BUF_LEN];
    audio_stats_t au_last;
    audio_stats(&au_last, true);
    uint32_t hb_events = m->ay.events;
#else
    /* The schedule: the field that starts after `sched_cycles` guest
     * cycles is due at sched_us plus their duration, worked out from the
     * cycle count each time, so neither the rounding nor the 50/60 Hz
     * choice accumulates (EL §6.3). */
    uint64_t sched_us = time_us_64() + 1000u;   /* the first field on time */
    uint64_t sched_cycles = 0;
#endif

    window_t hb, sec;
    window_start(&hb, m, late);
    window_start(&sec, m, late);
    uint32_t fields60 = 0;   /* since boot */
    bool ready = false;

    for (;;) {
        uint64_t now;
#if !PICO_ORIC_AUDIO
        uint64_t due = sched_us + sched_cycles * 1000000u / ORIC_CPU_HZ;
        now = time_us_64();
        if (now < due) {
            /* Core 0's own alarm: sleeping here interrupts nothing. */
            sleep_until(from_us_since_boot(due));
        } else if (now > due) {
            late++;
            if (now - due > SLIP_US) {
                slips++;
                sched_us = now;
                sched_cycles = 0;
            }
        }
#endif

        /* M9: between two fields is the one place the guest parks
         * (§4.5), and the windows start again after it. */

        uint32_t t_busy = time_us_32();

        /* Keys first, so the matrix the guest scans this field is the
         * one the events describe (§9.1). */
        uart_keys(k, m);
        uint8_t state, code;
        while (kbd_pop(&state, &code)) keymatrix_event(k, state, code);
        keymatrix_field(k, m);
        requests(k, m);

        /* The ULA's choice for this field (§11.1). */
        bool hz60 = !(m->ula_mode & ULA_MODE_50HZ);
        uint32_t t_run = time_us_32();
        uint32_t ran = oric_run_field(m);
        uint32_t run_us = time_us_32() - t_run;
#if PICO_ORIC_AUDIO
        (void)ran;
#else
        sched_cycles += ran;
#endif
        if (hz60) fields60++;

        /* Boot time to the prompt, from reset (§15.2 M7). */
        if (!ready && ready_shown(m)) {
            ready = true;
            uint32_t at = time_us_32();
            uint32_t guest_us = at - power_on_us;
            log_printf("  boot         : Ready at field %lu, %llu cycles and %lu.%03lu ms after "
                       "the guest's power-on, %lu.%03lu ms after the board's reset; core 1 "
                       "ready at %lu.%03lu ms (card %s, mount %lu us, ROM read %lu us)\n",
                       (unsigned long)m->fields, (unsigned long long)m->cpu.cycles,
                       (unsigned long)(guest_us / 1000u), (unsigned long)(guest_us % 1000u),
                       (unsigned long)(at / 1000u), (unsigned long)(at % 1000u),
                       (unsigned long)(g_boot.ready_us / 1000u),
                       (unsigned long)(g_boot.ready_us % 1000u),
                       card_state_str(g_boot.job.state), (unsigned long)g_boot.job.mount_us,
                       (unsigned long)g_boot.job.load_us);
        }

        /* The field ends where the ULA is about to draw the frame from
         * the top, which is what the snapshot holds (§11.1). */
        int i = pool_claim();
        if (i >= 0) {
            uint32_t t_take = time_us_32();
            oric_video_take(m, &g_pool.buf[i]);
            uint32_t take_us = time_us_32() - t_take;
            hb.take_us += take_us;
            sec.take_us += take_us;
            /* Once a second, the mode scan again on the copy, timed: it
             * ran inside the field, where it cannot be told apart
             * (§7.4). */
            if (sec.scan_us == 0) {
                uint32_t t_scan = time_us_32();
                (void)ula_scan_mode(g_pool.buf[i].window, g_pool.buf[i].mode);
                uint32_t scan_us = time_us_32() - t_scan;
                sec.scan_us = scan_us ? scan_us : 1u;
                hb.scan_us = sec.scan_us;
                if (sec.scan_us > hb.scan_max_us) hb.scan_max_us = sec.scan_us;
            }
            pool_publish(i);
        }
#if PICO_ORIC_AUDIO
        size_t n = oric_audio_drain(m, pcm, ORIC_AUDIO_BUF_LEN);
#endif
        uint32_t busy = time_us_32() - t_busy;
#if PICO_ORIC_AUDIO
        /* Blocks while the queue is full: this is the throttle, on the
         * PWM wrap, which shares clk_sys with nothing that drifts (EL
         * §6.3). The conversion to compare words inside is not counted
         * as busy; it is ~731 short loops a field. M10: turbo tops the
         * queue up with silence instead, never blocking (EL §9.3). */
        audio_push(pcm, n);
#endif
        hb.run_us += run_us;
        sec.run_us += run_us;
        hb.busy_us += busy;
        sec.busy_us += busy;

        now = time_us_64();
        if (now - sec.us >= 1000000u) {
            uint64_t wall = now - sec.us;
            uint64_t cyc = m->cpu.cycles - sec.cycles;
            g_c0.busy1000 = (uint32_t)(sec.busy_us * 1000u / wall);
            /* How many times real time the guest would run unpaced. */
            g_c0.head100 = (uint32_t)(cyc * 100u * 1000000u / ORIC_CPU_HZ / (sec.run_us + 1u));
            g_c0.hz = hz60 ? 60u : 50u;
#if PICO_ORIC_AUDIO
            audio_stats_t sec_au;
            audio_stats(&sec_au, false);
            g_c0.underruns = sec_au.underrun_samples;
            g_c0.late_refills = sec_au.late_refills;
#endif
            __dmb();
            g_c0.seconds++;
            window_start(&sec, m, late);
        }

        if (now - hb.us >= HEARTBEAT_US) {
            uint64_t wall = now - hb.us;
            uint64_t cyc = m->cpu.cycles - hb.cycles;
            uint64_t insns = m->instructions - hb.insns;
            uint32_t fields = m->fields - hb.fields;
            /* Guest cycles over wall time at 1 MHz: 1.000 is real time. */
            uint32_t rt1000 = (uint32_t)(cyc * 1000000u / ORIC_CPU_HZ * 1000u / wall);
            uint32_t busy1000 = (uint32_t)(hb.busy_us * 1000u / wall);
            uint32_t guest1000 = (uint32_t)(hb.run_us * 1000u / wall);
            uint32_t take10000 = (uint32_t)(hb.take_us * 10000u / wall);
            /* The sampled scan, as a share of wall time at the window's
             * field rate. */
            uint32_t scan10000 = (uint32_t)((uint64_t)hb.scan_us * fields * 10000u / wall);
            uint32_t head100 = (uint32_t)(cyc * 100u * 1000000u / ORIC_CPU_HZ /
                                          (hb.run_us + 1u));
            uint32_t hpi10 = (uint32_t)(hb.run_us * clk_mhz * 10u / (insns + 1u));
            uint32_t cpi100 = (uint32_t)(cyc * 100u / (insns + 1u));
            char busy_s[12], guest_s[12], hpi_s[12], cpi_s[12], take_s[12], scan_s[12];
            tenths(busy_s, sizeof busy_s, busy1000);
            tenths(guest_s, sizeof guest_s, guest1000);
            tenths(hpi_s, sizeof hpi_s, hpi10);
            hundredths(cpi_s, sizeof cpi_s, cpi100);
            hundredths(take_s, sizeof take_s, take10000);
            hundredths(scan_s, sizeof scan_s, scan10000);
            log_printf("  heartbeat    : %lu fields (%lu at 60 Hz), rt %lu.%03lu, "
                       "late %lu (+%lu), slips %lu | "
                       "%lu presents (%lu full, %lu dropped), last %lu us, max %lu us | "
                       "keys %lu (%lu lost), polls %lu (longest %lu us), i2c errors %lu | "
                       "undoc %lu (last #%02X at #%04X), log dropped %u, battery %s, card %s\n",
                       (unsigned long)m->fields, (unsigned long)fields60,
                       (unsigned long)(rt1000 / 1000u), (unsigned long)(rt1000 % 1000u),
                       (unsigned long)late, (unsigned long)(late - hb.late),
                       (unsigned long)slips,
                       (unsigned long)g_c1.presents, (unsigned long)g_c1.full_presents,
                       (unsigned long)g_pool.dropped,
                       (unsigned long)g_c1.last_us, (unsigned long)g_c1.max_us,
                       (unsigned long)g_c1.key_events,
                       (unsigned long)(kbd_overflows() + k->dropped),
                       (unsigned long)g_c1.polls, (unsigned long)g_c1.poll_max_us,
                       (unsigned long)sb_error_count(), (unsigned long)m->cpu.undoc_count,
                       (unsigned)m->cpu.undoc_op, (unsigned)m->cpu.undoc_pc, log_dropped(), power_text(), card_present() ? "in" : "out");
            log_printf("  perf         : tier %u, %lu MHz, core 0 busy %s%%, guest %s%% of wall, "
                       "headroom %lu.%02lux, %s host cycles/insn, %s cycles/insn, "
                       "%llu insns, %lu fields at %u Hz, snapshot %s%%, "
                       "mode scan %lu us (max %lu, %s%%)\n",
                       (unsigned)PICO_ORIC_RAM_TIER, (unsigned long)clk_mhz, busy_s, guest_s,
                       (unsigned long)(head100 / 100u), (unsigned long)(head100 % 100u),
                       hpi_s, cpi_s, (unsigned long long)insns, (unsigned long)fields,
                       hz60 ? 60u : 50u, take_s, (unsigned long)hb.scan_us,
                       (unsigned long)hb.scan_max_us, scan_s);
#if PICO_ORIC_AUDIO
            /* The consumed-sample rate against the microsecond timer is
             * the control quantity: it is the PWM wrap, measured, and it
             * must not move whatever the guest does (EL §6.3, §14). */
            audio_stats_t au;
            audio_stats(&au, true);
            uint32_t rate = (uint32_t)((uint64_t)(au.consumed - au_last.consumed) *
                                       1000000u / (wall + 1u));
            uint32_t events = m->ay.events - hb_events;
            log_printf("  audio        : %lu Hz consumed, queue %lu (low %lu), "
                       "underrun samples %lu (+%lu), late refills %lu (+%lu), "
                       "core overflow %lu, AY events %lu/s, AY writes %lu%s\n",
                       (unsigned long)rate, (unsigned long)au.level,
                       (unsigned long)au.low_water,
                       (unsigned long)au.underrun_samples,
                       (unsigned long)(au.underrun_samples - au_last.underrun_samples),
                       (unsigned long)au.late_refills,
                       (unsigned long)(au.late_refills - au_last.late_refills),
                       (unsigned long)m->pcm.overflow,
                       (unsigned long)((uint64_t)events * 1000000u / (wall + 1u)),
                       (unsigned long)m->ay.writes, au.started ? "" : " (not started)");
            au_last = au;
            hb_events = m->ay.events;
#endif
            window_start(&hb, m, late);
        }
    }
}
