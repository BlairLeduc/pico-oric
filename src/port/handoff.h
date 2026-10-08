/* handoff.h — what the two cores share (design.md §4.3, §4.4).
 *
 * The counters each core keeps for the other's heartbeat. Each is a
 * 32-bit word with one writer, so a torn read is not possible.
 *
 * M7: the snapshot pool (snappool.h) and its transitions under one SIO
 * spinlock join these, as pico-ace's handoff.c has them.
 */
#ifndef PICO_ORIC_HANDOFF_H
#define PICO_ORIC_HANDOFF_H

#include <stdbool.h>
#include <stdint.h>

#include "board.h"

/* Core 1's, read by core 0. */
typedef struct {
    bool     ready;          /* bring-up finished, the fields below valid */
    uint32_t i2c_hz, spi_hz;
    int32_t  sb_version;     /* SB_REG_VER's byte, -1 if unread           */
    uint32_t polls;
    uint32_t key_events;
    uint32_t poll_max_us;    /* the longest keyboard poll since boot      */
    int32_t  battery;        /* SB_REG_BAT's byte, -1 until read          */
    int32_t  temp_c;         /* the die, INT32_MIN until read             */
    uint32_t card_jobs;      /* the ROM listing, at boot and on insertion */
} core1_stats_t;

extern volatile core1_stats_t g_c1;

/* What core 1 measured during bring-up (design.md §15.2 M6), written
 * before g_c1.ready and only read after it, so it needs no lock. */
typedef struct {
    uint32_t i2c_us;         /* one register read, SB_REG_VER             */
    uint32_t fill_us;        /* a 240x224 rectangle, one colour, by DMA    */
    uint32_t blit_us;        /* the same rectangle, row by row             */
    uint32_t row_us;         /* one 240-pixel row, window and all          */
} bringup_t;

extern bringup_t g_bringup;

/* The board, identified by main() before core 1 starts. */
extern board_info_t g_board;

#endif /* PICO_ORIC_HANDOFF_H */
