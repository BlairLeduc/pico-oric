/* snappool.h — the core 0 -> core 1 video handoff (design.md §4.4).
 *
 * Three frame buffers, each in one of four states. Core 0 fills one per
 * field and publishes it; core 1 takes the newest published one,
 * renders it, and releases it. Core 1 never reads guest RAM while the
 * 6502 runs; it only ever sees a frame it owns.
 *
 * This file is the state machine and nothing else. It holds no lock:
 * RP2350 has no compare-and-swap and local interrupt masking is not a
 * multicore lock (hardware-notes.md §9.5), so the port calls every
 * transition here under one SIO spinlock. Keeping the lock out is what
 * lets the transitions be tested on a host (EL §2.4).
 *
 * Copied from pico-ace and renamed; the snapshot is the Oric's frame
 * (ula.h), filled by oric_video_take.
 */
#ifndef PICO_ORIC_SNAPPOOL_H
#define PICO_ORIC_SNAPPOOL_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"
#include "ula.h"

typedef enum {
    SNAP_FREE,       /* owned by nobody; core 0 may claim it        */
    SNAP_FILLING,    /* core 0 is writing it; core 1 must not look  */
    SNAP_READY,      /* complete, awaiting collection               */
    SNAP_RENDERING,  /* core 1 owns it                              */
} snap_state_t;

typedef struct {
    oric_frame_t buf[ORIC_SNAPSHOT_COUNT];
    snap_state_t state[ORIC_SNAPSHOT_COUNT];
    uint32_t     published;   /* frames published                   */
    uint32_t     dropped;     /* superseded before core 1 took them */
} snappool_t;

void snappool_init(snappool_t *p);

/* Core 0: take a free buffer to fill. Returns its index, or -1 if none
 * is free — which three buffers make impossible while each side holds
 * at most one, so -1 is a bug, not back-pressure. */
int  snappool_claim(snappool_t *p);

/* Core 0: filling -> ready. Any older ready buffer is superseded and
 * freed on the spot, unrendered, so there is always a free buffer for the
 * next claim and core 0 never has to wait for core 1. */
void snappool_publish(snappool_t *p, int i);

/* Core 1: take the ready buffer, if there is one; ready -> rendering.
 * Returns its index or -1. */
int  snappool_take(snappool_t *p);

/* Core 1: rendering -> free. */
void snappool_release(snappool_t *p, int i);

#endif /* PICO_ORIC_SNAPPOOL_H */
