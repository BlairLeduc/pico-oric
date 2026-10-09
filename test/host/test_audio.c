/* test_audio.c — the AY's sound (design.md §8.5, §15.2 M8).
 *
 * The reference is an independent model (ay_model.h): it steps every
 * counter of the chip a cycle at a time, from the register writes alone,
 * and box-filters the level with the same rational sample period. The
 * core runs event to event and catches the rest up by arithmetic; on
 * every sample the two must agree to 1 LSB, except where a tone is
 * averaged (§8.2), where the difference is measured and bounded here.
 *
 * Then the envelope's sixteen shapes against the data manual's drawings,
 * as MAME transcribes them.
 * The chip alone, driven by scripts: no ROM, so CI runs it all.
 * test_audio_rom does the same with the ROMs' own sounds.
 */

#include <stdlib.h>
#include <string.h>

#include "ay_model.h"
#include "test_util.h"

/* ---- the chip alone, driven by a script ------------------------------- */

static ay8912_t g_ay;
static pcm_t    g_pcm;

static void chip_start(bool average) {
    pcm_init(&g_pcm, 0, AY_LEVEL_MAX, 0, 0, 0);
    g_pcm.dc_block = false;
    memset(&g_ay, 0, sizeof g_ay);
    g_ay.avg_off = !average;
    ay8912_set_average(&g_ay, g_pcm.num, g_pcm.den);
    ay8912_reset(&g_ay, 0, &g_pcm);
    n_writes = 0;
    n_core = 0;
}

static void chip_drain(void) {
    n_core += pcm_drain(&g_pcm, core_out + n_core, MAX_SAMPLES - n_core);
}

static void chip_write(uint64_t at, unsigned r, uint8_t v) {
    ay8912_bus(&g_ay, AY_BUS_LATCH, (uint8_t)r, at, &g_pcm);
    ay8912_bus(&g_ay, AY_BUS_WRITE, v, at, &g_pcm);
    ay8912_bus(&g_ay, AY_BUS_INACTIVE, 0, at, &g_pcm);
    if (n_writes < MAX_WRITES) writes[n_writes++] = (aw_t){ at, (uint8_t)r, v };
    chip_drain();
}

/* Up to `at`, draining at least every 20,000 cycles, as fields do: the
 * pcm holds ORIC_AUDIO_BUF_LEN samples. */
static void chip_advance(uint64_t at) {
    while ((int64_t)(at - g_ay.t) > 20000) {
        ay8912_advance(&g_ay, g_ay.t + 20000u, &g_pcm);
        chip_drain();
    }
    ay8912_advance(&g_ay, at, &g_pcm);
    chip_drain();
}

static uint32_t rng_state = 12345u;
static uint32_t rnd(uint32_t n) {
    rng_state = rng_state * 1103515245u + 12345u;
    return (rng_state >> 8) % n;
}

/* Scripts without the noise or the envelope (test_model's breakdown of
 * the averaged path's difference). */
static bool script_plain;

/* A register write the way games and the ROM make them: small periods
 * often, so that events are dense; the envelope fast. `min_tp` keeps
 * every tone at or above it. */
static void random_write(uint64_t at, unsigned min_tp) {
    unsigned r = rnd(14);
    uint8_t v = (uint8_t)rnd(256);
    switch (r) {
    case 0: case 2: case 4:
        v = (uint8_t)(rnd(4) ? rnd(60) : rnd(256));
        break;
    case 1: case 3: case 5:
        v = (uint8_t)(rnd(3) ? 0 : rnd(16));
        break;
    case 7:
        v = (uint8_t)(0x40 | rnd(64) | (script_plain ? 0x38 : 0));
        break;
    case 8: case 9: case 10:
        if (script_plain) v &= 0x0F;
        break;
    case 11:
        v = (uint8_t)rnd(40);
        break;
    case 12:
        v = (uint8_t)(rnd(6) ? 0 : rnd(4));
        break;
    default:
        break;
    }
    if (min_tp && r <= 5) {
        unsigned c = r >> 1;
        uint8_t fine = (r & 1) ? g_ay.reg[2 * c] : v;
        uint8_t coarse = (r & 1) ? v : g_ay.reg[2 * c + 1];
        if (((unsigned)coarse << 8 | fine) < min_tp) {
            /* Raise the fine byte, or the whole period. */
            if (r & 1) chip_write(at, 2 * c, (uint8_t)min_tp);
            else v = (uint8_t)min_tp;
        }
    }
    chip_write(at, r, v);
}

/* One script: `writes` random writes at random gaps, with lazy advances
 * between them, and now and then a long silence. Then the model on the
 * same writes; returns the largest difference, and the largest after a
 * 64-sample moving average of each (what reaches the ear of an averaged
 * tone, §8.2). */
typedef struct {
    int max_diff, max_lp_diff;
    size_t samples;
    uint32_t events;
} script_result_t;

static script_result_t script(unsigned nwrites, bool average, unsigned min_tp) {
    chip_start(average);
    /* RESET leaves every period 0: raise them first, or the mixer's
     * first choice sounds a tone below min_tp. */
    uint64_t t = 0;
    if (min_tp)
        for (unsigned c = 0; c < 3u; c++) chip_write(0, 2u * c, (uint8_t)min_tp);
    for (unsigned i = 0; i < nwrites; i++) {
        uint64_t gap = rnd(100) ? 1u + rnd(3000) : 200000u + rnd(2400000);
        uint64_t next = t + gap;
        /* Advances anywhere up to the next write, as fields end. */
        if (rnd(2)) chip_advance(t + rnd((uint32_t)gap + 1u));
        chip_advance(next);
        t = next;
        random_write(t, min_tp);
    }
    uint64_t end = t + 5000u;
    chip_advance(end);

    size_t n = model_render(writes, n_writes, 0, end, g_pcm.num, g_pcm.den, model_out, MAX_SAMPLES);
    script_result_t res = { 0, 0, n < n_core ? n : n_core, g_ay.events };
    int64_t lp_core = 0, lp_model = 0;
    for (size_t i = 0; i < res.samples; i++) {
        int d = abs(core_out[i] - model_out[i]);
        if (d > res.max_diff) res.max_diff = d;
        lp_core += core_out[i];
        lp_model += model_out[i];
        if (i >= 64) {
            lp_core -= core_out[i - 64];
            lp_model -= model_out[i - 64];
            int lp = (int)(llabs(lp_core - lp_model) / 64);
            if (lp > res.max_lp_diff) res.max_lp_diff = lp;
        }
    }
    return res;
}

static int test_model(void) {
    /* The model's LFSR comes round after 2^17 - 1 shifts, which the
     * core's arithmetic catch-up relies on (ay8912.c). */
    {
        uint32_t r = 1, n = 0;
        do {
            r = model_lfsr(r);
            n++;
        } while (r != 1u && n < 1000000u);
        CHECK(n == 131071u, "the LFSR's period is %u", n);
    }

    /* Every tone stepped: the event-driven core equals the model. */
    uint32_t events = 0;
    size_t samples = 0;
    for (int s = 0; s < 40; s++) {
        script_result_t r = script(400, false, 0);
        CHECK(r.max_diff <= 1, "script %d, every tone stepped: a sample differs by %d", s,
              r.max_diff);
        CHECK(r.samples > 1000u, "script %d made only %zu samples", s, r.samples);
        events += r.events;
        samples += r.samples;
    }
    printf("stepped: %zu samples, %u events, equal to the model\n", samples, events);

    /* Averaging on, but no tone fast enough to be averaged: still equal. */
    for (int s = 0; s < 20; s++) {
        script_result_t r = script(400, true, 4);
        CHECK(r.max_diff <= 1, "script %d, tones of 4 or more: a sample differs by %d", s,
              r.max_diff);
    }

    /* Averaging on with any period: the averaged tones differ sample by
     * sample (what averaging removes is the alias of an ultrasonic
     * tone), and after a 64-sample moving average they do not. */
    int worst = 0, worst_lp = 0;
    for (int plain = 1; plain >= 0; plain--) {
        script_plain = plain;
        for (int s = 0; s < 40; s++) {
            script_result_t r = script(400, true, 0);
            if (r.max_diff > worst) worst = r.max_diff;
            if (r.max_lp_diff > worst_lp) worst_lp = r.max_lp_diff;
        }
        printf("averaged%s: largest difference %d a sample, %d through a 64-sample average "
               "(of %d full scale)\n", plain ? ", tones alone" : "", worst, worst_lp,
               PCM_FULL_SCALE);
    }
    script_plain = false;
    CHECK(worst > 1, "no tone was averaged: the scripts do not reach the threshold");
    /* Each change of level lands somewhere in an averaged tone's half
     * period, at most 24 cycles, which the mean cannot know; and noise
     * or a fast envelope ticks in step with the tone. Measured
     * 2026-10-08: 109 with tones alone, 206 with both, of 32,767. */
    CHECK(worst_lp <= PCM_FULL_SCALE / 100, "an averaged tone's mean is off by %d", worst_lp);

    /* The control: the model with every write one tick late must not
     * match, or the comparison above could not see a stamp. */
    {
        script(200, false, 0);
        for (size_t i = 0; i < n_writes; i++) writes[i].at += AY_TICK_CYCLES;
        uint64_t end = g_ay.t;
        size_t n = model_render(writes, n_writes, 0, end, g_pcm.num, g_pcm.den, model_out,
                                MAX_SAMPLES);
        int d = 0;
        for (size_t i = 0; i < n && i < n_core; i++)
            if (abs(core_out[i] - model_out[i]) > d) d = abs(core_out[i] - model_out[i]);
        CHECK(d > 1, "writes a tick late made no difference: the comparison is blind");
    }
    return 0;
}

/* ---- the envelope's shapes against the datasheet's drawings ----------- */

/* The data manual's drawings, as MAME's ay8910.cpp transcribes its table
 * of shapes, four cycles of each: \ falls 15 to 0, / rises 0 to 15, _
 * holds 0, ^ holds 15. */
static const char *const drawing[16] = {
    "\\___", "\\___", "\\___", "\\___", "/___", "/___", "/___", "/___",
    "\\\\\\\\", "\\___", "\\/\\/", "\\^^^", "////", "/^^^", "/\\/\\", "/___",
};

static unsigned drawn(int shape, unsigned step) {
    char c = drawing[shape][step / 16u];
    unsigned j = step % 16u;
    return c == '\\' ? 15u - j : c == '/' ? j : c == '^' ? 15u : 0u;
}

/* The envelope's output after each of 64 steps, the period 1 (16
 * cycles a step). `jump`: one advance to each, from the start each
 * time, so the arithmetic catch-up is what is tested. */
static void env_sequence(int shape, bool jump, unsigned out[64]) {
    for (unsigned j = 0; j < 64u; j++) {
        if (jump || j == 0) {
            chip_start(true);
            chip_write(0, 7, 0x7F);         /* tones and noise off: high */
            chip_write(0, 8, 0x10);         /* channel A follows it      */
            chip_write(0, 11, 1);
            chip_write(0, 13, (uint8_t)shape);
        }
        /* Steps at ticks 1, 3, 5, ...: between steps j and j + 1. */
        chip_advance(16u * j + 5u);
        out[j] = ay8912_env_volume(&g_ay);
    }
}

static int test_envelope(void) {
    for (int shape = 0; shape < 16; shape++) {
        unsigned stepwise[64], jumped[64];
        env_sequence(shape, false, stepwise);
        env_sequence(shape, true, jumped);
        for (unsigned j = 0; j < 64u; j++) {
            CHECK(stepwise[j] == drawn(shape, j), "shape %d, step %u: %u, drawn %u", shape, j,
                  stepwise[j], drawn(shape, j));
            CHECK(jumped[j] == stepwise[j], "shape %d, step %u caught up: %u, stepped %u", shape,
                  j, jumped[j], stepwise[j]);
        }
    }
    /* The control: a shape's sequence against its neighbour's drawing. */
    {
        unsigned seq[64];
        env_sequence(10, false, seq);
        unsigned same = 0;
        for (unsigned j = 0; j < 64u; j++) same += seq[j] == drawn(14, j);
        CHECK(same < 64u, "shape 10 matched shape 14's drawing: the comparison is blind");
    }
    /* The output follows it: channel A at the envelope's level 15 is
     * level 15's amplitude, held for a whole sample. */
    {
        chip_start(true);
        chip_write(0, 7, 0x7F);
        chip_write(0, 8, 0x10);
        chip_write(0, 11, 0);
        chip_write(0, 12, 0x10);           /* 64 ms a step               */
        chip_write(0, 13, 0x0D);           /* /^^^: up, then 15 for ever */
        chip_advance(2000000u);
        int16_t want = (int16_t)((2u * AY_AMP_MAX * (uint64_t)PCM_FULL_SCALE + AY_LEVEL_MAX / 2u) /
                                 AY_LEVEL_MAX);
        CHECK(n_core > 100u && abs(core_out[n_core - 1] - want) <= 1,
              "a held 15 on one channel: %d, want %d", n_core ? core_out[n_core - 1] : 0, want);
    }
    return 0;
}

/* A new rate keeps what is waiting in place, the level and the DC
 * blocker (pcm_set_rate; oric_audio_set_rate at boot). */
static int test_set_rate(void) {
    pcm_t p;
    pcm_init(&p, 0, 100, 0, 0, 0);
    p.level = 100;
    pcm_advance(&p, 2000);
    uint32_t n = p.count;
    int16_t first = p.buf[0], last = p.buf[n - 1];
    int32_t hp_x = p.hp_x, hp_y = p.hp_y;
    pcm_set_rate(&p, 2000, 100, 1000000u, 48000u, 1u);
    CHECK(n > 50u && p.count == n && p.buf[0] == first && p.buf[n - 1] == last,
          "a new rate lost the %u samples waiting (%u left)", n, p.count);
    CHECK(p.level == 100u && p.dc_block && p.hp_x == hp_x && p.hp_y == hp_y,
          "a new rate reset the level or the DC blocker");
    CHECK(p.num == 125u && p.den == 6u, "1 MHz at 48 kHz is 125/6 cycles, got %u/%u", p.num,
          p.den);
    /* At the new rate, from 2,000: 48 samples in the next 1,000 cycles. */
    pcm_advance(&p, 3000);
    CHECK(p.count == n + 48u, "%u samples at the new rate, want 48", p.count - n);
    return 0;
}

int main(void) {
    if (test_set_rate()) return 1;
    if (test_model()) return 1;
    if (test_envelope()) return 1;
    TEST_DONE();
}
