/* handoff.h — what the two cores share (design.md §4.3, §4.4).
 *
 * The snapshot pool, with every transition under one SIO spinlock
 * (snappool.h says why the lock is the port's), and the counters each
 * core keeps for the other's heartbeat and perf line. Each counter is a
 * 32-bit word with one writer, so a torn read is not possible.
 *
 * Copied from pico-ace and renamed. The SWD block is left for M15.
 */
#ifndef PICO_ORIC_HANDOFF_H
#define PICO_ORIC_HANDOFF_H

#include <stdbool.h>
#include <stdint.h>

#include "board.h"
#include "card.h"
#include "config.h"
#include "oric.h"
#include "romset.h"
#include "settings.h"
#include "snappool.h"

extern snappool_t g_pool;

void handoff_init(void);

/* snappool.h's transitions, each under the lock. */
int  pool_claim(void);
void pool_publish(int i);
int  pool_take(void);
void pool_release(int i);

/* Core 1's, read by core 0. */
typedef struct {
    bool     ready;          /* bring-up finished, the fields below valid */
    uint32_t i2c_hz, spi_hz;
    int32_t  sb_version;     /* SB_REG_VER's byte, -1 if unread           */
    uint32_t presents;
    uint32_t full_presents;
    uint32_t last_us;        /* the last present, wall time               */
    uint32_t max_us;         /* the longest since boot                    */
    uint32_t polls;
    uint32_t key_events;
    uint32_t poll_max_us;    /* the longest keyboard poll since boot      */
    int32_t  battery;        /* SB_REG_BAT's byte, -1 until read          */
    int32_t  temp_c;         /* the die, INT32_MIN until read             */
} core1_stats_t;

extern volatile core1_stats_t g_c1;

/* What core 1 measured during bring-up (design.md §15.2 M6, M7),
 * written before g_c1.ready and only read after it, so it needs no
 * lock. Wall times, since boot where it says so. */
typedef struct {
    uint32_t i2c_us;         /* one register read, SB_REG_VER             */
    uint32_t lcd_us;         /* lcd_init: the panel's reset and sleep-out */
    uint32_t card_us;        /* the ROM job: mount, list, hash, load      */
    uint32_t start_us;       /* core 1 started, since boot                */
} bringup_t;

extern bringup_t g_bringup;

/* The boot's ROM (design.md §10.2). main() names the one it wants before
 * core 1 starts; core 1 reads the card and leaves the image here before
 * g_c1.ready.
 *
 * While the missing-ROM page is up the guest has not started, so a card
 * that goes in or out is read again (core1.c), and job and image change
 * under core 0. Two flags keep them apart, each with a barrier between
 * its store and the other's load: core 1 sets `busy`, then runs a job
 * only if `claimed` is clear; core 0 sets `claimed`, then waits for
 * `busy` to clear, and from then on job and image are its own.
 * `generation` counts the jobs, so core 0 knows to look again. */
typedef struct {
    rom_id_t   want;         /* the machine's ROM: the settings' and the
                                build's (boot_machine)                    */
    oric_ram_t ram;          /* and its RAM, for the missing-ROM page     */
    settings_t settings;     /* what the card's file said, over the defaults */
    card_job_t job;          /* what the card had, and which was loaded   */
    uint8_t    image[ORIC_ROM_SIZE];   /* job.loaded's bytes              */
    uint32_t   ready_us;     /* core 1's bring-up done, since boot        */
    volatile bool     busy;        /* core 1: a job is running          */
    volatile bool     claimed;     /* core 0: job and image are mine    */
    volatile uint32_t generation;  /* jobs finished                     */
} boot_report_t;

extern boot_report_t g_boot;

/* Core 0's per-second window for the perf line (design.md §7.5, §14),
 * in thousandths and hundredths so that core 1 formats without floats. */
typedef struct {
    uint32_t seconds;        /* windows closed; 0 until the first        */
    uint32_t busy1000;       /* core 0 outside the pacing wait, of wall  */
    uint32_t head100;        /* times real time it would run unpaced     */
    uint32_t underruns;      /* underrun samples since boot (§8.4)       */
    uint32_t late_refills;   /* late DMA refills since boot              */
    uint32_t hz;             /* the last field's rate, 50 or 60          */
} core0_perf_t;

extern volatile core0_perf_t g_c0;

/* What the menu changes (design.md §12), written by core 1 while the
 * guest is parked and applied by core 0 when it has the machine back
 * (EL §2.5). Core 1 reads perf_line and status to draw the lines. */
typedef struct {
    volatile unsigned volume;      /* 0-8, as settings_t has it            */
    volatile bool     perf_line;   /* the top line (status.h)              */
    volatile bool     status;      /* the bottom line                      */
    volatile unsigned backlight;   /* 1-15 as the Setup page has it; 0 unread */
    volatile bool     fast_tape;   /* the trap, or the signal (M10)        */
    volatile bool     reset;       /* the menu's Reset: core 0 clears it   */
    /* The Machine page's Apply (§12): power on as restart_cfg, with the
     * ROM core 1 has left in g_boot.image. Core 0 clears it. */
    volatile bool     restart;
    oric_config_t     restart_cfg;
} ui_t;

extern ui_t g_ui;

/* The machine a boot starts (design.md §10.7): the defaults, then the
 * settings file's ROM and RAM, then the build's PICO_ORIC_BOOT_ROM and
 * PICO_ORIC_BOOT_RAM, which win over the file (EL §8.7), so that a run
 * driven over the UART knows what it booted. */
void boot_machine(const settings_t *s, oric_config_t *cfg);

/* The board, identified by main() before core 1 starts. */
extern board_info_t g_board;

#endif /* PICO_ORIC_HANDOFF_H */
