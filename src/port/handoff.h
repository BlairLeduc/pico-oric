/* handoff.h — what the two cores share (design.md §4.3, §4.4).
 *
 * The snapshot pool, with every transition under one SIO spinlock
 * (snappool.h says why the lock is the port's), and the counters each
 * core keeps for the other's heartbeat and perf line. Each counter is a
 * 32-bit word with one writer, so a torn read is not possible.
 *
 * Copied from pico-ace and renamed. The settings, the SWD block and the
 * menu's state are left for M9 and M15.
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
 * g_c1.ready, and core 0 reads it only after, so it needs no lock. */
typedef struct {
    rom_id_t   want;         /* the machine's ROM: main()'s               */
    oric_ram_t ram;          /* and its RAM, for the missing-ROM page     */
    card_job_t job;          /* what the card had, and which was loaded   */
    uint8_t    image[ORIC_ROM_SIZE];   /* job.loaded's bytes              */
    uint32_t   ready_us;     /* core 1's bring-up done, since boot        */
} boot_report_t;

extern boot_report_t g_boot;

/* Core 0's per-second window for the perf line (design.md §7.5, §14),
 * in thousandths and hundredths so that core 1 formats without floats. */
typedef struct {
    uint32_t seconds;        /* windows closed; 0 until the first        */
    uint32_t busy1000;       /* core 0 outside the pacing wait, of wall  */
    uint32_t head100;        /* times real time it would run unpaced     */
    uint32_t hz;             /* the last field's rate, 50 or 60          */
} core0_perf_t;

extern volatile core0_perf_t g_c0;

/* What the menu will change (design.md §12), until M9 brings the menu
 * and the settings file. Core 1 reads it to draw the perf line. */
typedef struct {
    volatile bool perf_line;   /* the top line (status.h)             */
} ui_t;

extern ui_t g_ui;

/* The board, identified by main() before core 1 starts. */
extern board_info_t g_board;

#endif /* PICO_ORIC_HANDOFF_H */
