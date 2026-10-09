/* pcm.h — a level in guest cycles to PCM: the box filter (design.md §8.2;
 * EL §6.1).
 *
 * pico-ace's beeper, generalised from a speaker bit to any level: the
 * AY's three channels summed. A sample is the time average of the level
 * over the guest cycles it covers, a box filter at exactly the sample
 * period, not a point sample of it, which aliases audibly. The cost is
 * per change of level plus a short loop per sample.
 *
 * Time is kept as a rational, never a truncated rate (EL §6.1, HW §5.2):
 * cycles per sample is num/den, 2,048/75 at 1 MHz and 150 MHz / 4,096,
 * and the sample boundaries are tracked in units of 1/den of a cycle, so
 * they land where they should for ever rather than drifting.
 *
 * Times are the low 32 bits of the 6502's cycle count, which wraps at
 * 2^32 (~72 minutes). Only differences from the current sample's start
 * are taken, never more than a field's worth, so the wrap does no harm.
 *
 * Output is signed 16-bit mono through a one-pole DC blocker, so the
 * AY's unipolar output comes to rest at 0, which is the silence value
 * the port pads an underrun with.
 */
#ifndef PICO_ORIC_PCM_H
#define PICO_ORIC_PCM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"

/* The level `level_max` held for a whole sample, before the DC blocker.
 * The blocker's output for an input in [0, M] stays in [-M, M], so this
 * cannot wrap int16 (design.md §8.3: headroom by construction). */
#define PCM_FULL_SCALE 32767

typedef struct {
    /* Cycles per sample = num / den = q + r / den. */
    uint32_t num, den, q, r;
    uint32_t level_max;
    uint64_t scale;          /* (FULL_SCALE << 32) / (num * level_max)   */

    /* The current sample began at cycle `start` plus start_frac / den. */
    uint32_t start;
    uint32_t start_frac;
    uint32_t pos;            /* units (1/den cycle) of it accounted for   */
    uint64_t acc;            /* level x units over them                   */
    uint32_t level;

    bool     dc_block;       /* on by default; tests turn it off          */
    int32_t  hp_x;           /* last input                                */
    int32_t  hp_y;           /* last output, Q8                           */

    int16_t  buf[ORIC_AUDIO_BUF_LEN];
    uint32_t count;
    uint32_t overflow;       /* samples lost because nobody drained       */
} pcm_t;

/* Start at cycle `now` with the level at 0, for levels up to level_max.
 * The sample rate is rate_num / rate_den Hz: on the device clk_sys over
 * divider x (TOP + 1) x oversample, passed as that fraction so nothing
 * is rounded. Zero in any rate argument means the nominal
 * ORIC_AUDIO_RATE at ORIC_CPU_HZ. */
void pcm_init(pcm_t *p, uint32_t now, uint32_t level_max, uint32_t cpu_hz,
              uint32_t rate_num, uint32_t rate_den);

/* A new rate, from a sample starting at `now`, as pcm_init takes it.
 * Everything else is kept: the level, the DC blocker's state, and the
 * samples not yet drained, in place (no copy, and nothing on the stack:
 * the device's main stack is 2 KiB). */
void pcm_set_rate(pcm_t *p, uint32_t now, uint32_t level_max, uint32_t cpu_hz,
                  uint32_t rate_num, uint32_t rate_den);

/* The guest's clock jumped (a power-on, or from M11 a state restored):
 * the next sample starts at `now`. The rate, the level, the DC blocker's
 * state and the samples not yet drained are kept. */
void pcm_restart(pcm_t *p, uint32_t now);

/* Emit every sample that ends at or before `now`, at the current level. */
void pcm_advance(pcm_t *p, uint32_t now);

/* The level changes to `level` at `now`. */
static inline void pcm_set_level(pcm_t *p, uint32_t now, uint32_t level) {
    if (level == p->level) return;
    pcm_advance(p, now);
    p->level = level;
}

/* Move up to `max` samples out, oldest first. Returns how many. */
size_t pcm_drain(pcm_t *p, int16_t *dst, size_t max);

#endif /* PICO_ORIC_PCM_H */
