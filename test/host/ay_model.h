/* ay_model.h — the AY, modelled a cycle at a time (design.md §8.5).
 *
 * The reference for test_audio and test_audio_rom: every counter of the
 * chip stepped cycle by cycle from the register writes alone, and the
 * level box-filtered with the pcm's rational sample period and scale.
 * Written from the datasheet's register map and MAME's account of the
 * counters (ay8912.h), not from ay8912.c.
 */
#ifndef PICO_ORIC_TEST_AY_MODEL_H
#define PICO_ORIC_TEST_AY_MODEL_H

#include <stdint.h>
#include <string.h>

#include "ay8912.h"
#include "pcm.h"

typedef struct {
    uint8_t  reg[16];
    unsigned tc[3], tout[3];
    unsigned nc, pre;
    uint32_t rng;
    unsigned ec;
    int      step;
    unsigned attack, hold, alt, holding;
} model_t;

static const uint8_t model_mask[16] = { 0xFF, 0x0F, 0xFF, 0x0F, 0xFF, 0x0F, 0x1F, 0xFF,
                                        0x1F, 0x1F, 0x1F, 0xFF, 0xFF, 0x0F, 0xFF, 0xFF };

static inline void model_shape(model_t *s, uint8_t v) {
    s->attack = (v & 4) ? 15 : 0;
    if (!(v & 8)) {
        s->hold = 1;
        s->alt = s->attack ? 1 : 0;
    } else {
        s->hold = v & 1;
        s->alt = (v >> 1) & 1;
    }
    s->step = 15;
    s->holding = 0;
}

static inline void model_reset(model_t *s) {
    memset(s, 0, sizeof *s);
    s->rng = 1;
    model_shape(s, 0);
}

static inline void model_write(model_t *s, unsigned r, uint8_t v) {
    s->reg[r] = v & model_mask[r];
    if (r == 13) model_shape(s, s->reg[13]);
}

static inline uint32_t model_lfsr(uint32_t r) {
    return (r >> 1) | ((((r >> 0) ^ (r >> 3)) & 1u) << 16);
}

static inline void model_tick(model_t *s) {
    for (int i = 0; i < 3; i++) {
        unsigned tp = s->reg[2 * i] | (s->reg[2 * i + 1] << 8);
        if (++s->tc[i] >= (tp ? tp : 1u)) {
            s->tc[i] = 0;
            s->tout[i] ^= 1;
        }
    }
    unsigned np = s->reg[6];
    if (++s->nc >= (np ? np : 1u)) {
        s->nc = 0;
        s->pre ^= 1;
        if (!s->pre) s->rng = model_lfsr(s->rng);
    }
    if (!s->holding) {
        unsigned ep = s->reg[11] | (s->reg[12] << 8);
        if (++s->ec >= 2u * ep) {
            s->ec = 0;
            if (--s->step < 0) {
                if (s->hold) {
                    if (s->alt) s->attack ^= 15;
                    s->holding = 1;
                    s->step = 0;
                } else {
                    if (s->alt) s->attack ^= 15;
                    s->step = 15;
                }
            }
        }
    }
}

/* Twice the sum of the channels' amplitudes while high (AY_LEVEL_MAX). */
static inline unsigned model_level(const model_t *s) {
    unsigned level = 0;
    unsigned env = (unsigned)(s->step ^ (int)s->attack) & 15u;
    for (int i = 0; i < 3; i++) {
        unsigned a = s->reg[8 + i];
        unsigned vol = (a & 0x10) ? env : (a & 15);
        unsigned tone = s->tout[i] | ((s->reg[7] >> i) & 1);
        unsigned noise = (s->rng & 1) | ((s->reg[7] >> (3 + i)) & 1);
        level += 2u * ay8912_amplitude(vol) * (tone & noise);
    }
    return level;
}

typedef struct {
    uint64_t at;
    uint8_t  reg, val;
} aw_t;

#define MAX_WRITES  200000u
#define MAX_SAMPLES 600000u

static aw_t     writes[MAX_WRITES];
static size_t   n_writes;
static int16_t  core_out[MAX_SAMPLES];
static size_t   n_core;
static int16_t  model_out[MAX_SAMPLES];

/* The model from a reset at `reset_at` to `end`, each write applied at its
 * cycle before that cycle's tick, the level box-filtered over samples of
 * num/den cycles from cycle 0, scaled as pcm.h scales, no DC blocker.
 * The writes are in time order; one stamped in the past is applied late,
 * where the comparison will see it. */
static inline size_t model_render(const aw_t *w, size_t nw, uint64_t reset_at, uint64_t end,
                                  uint32_t num, uint32_t den, int16_t *out, size_t max) {
    model_t s;
    model_reset(&s);
    uint64_t whole = (uint64_t)num * AY_LEVEL_MAX;
    uint64_t scale = (((uint64_t)PCM_FULL_SCALE << 32) + whole / 2u) / whole;
    uint64_t boundary = num, acc = 0;
    size_t n = 0, wi = 0;
    for (uint64_t c = 0; c < end; c++) {
        if (c == reset_at) model_reset(&s);
        while (wi < nw && w[wi].at <= c) {
            model_write(&s, w[wi].reg, w[wi].val);
            wi++;
        }
        if ((c & 7u) == 0) model_tick(&s);
        uint64_t level = model_level(&s);
        uint64_t u0 = c * den, u1 = u0 + den;
        while (u1 >= boundary) {
            acc += level * (boundary - u0);
            int64_t x = (int64_t)((acc * scale + 0x80000000u) >> 32);
            if (x > INT16_MAX) x = INT16_MAX;
            if (n < max) out[n++] = (int16_t)x;
            acc = 0;
            u0 = boundary;
            boundary += num;
        }
        acc += level * (u1 - u0);
    }
    return n;
}

#endif /* PICO_ORIC_TEST_AY_MODEL_H */
