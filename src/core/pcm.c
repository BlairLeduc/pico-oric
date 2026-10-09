/* pcm.c — a level in guest cycles to PCM (scope in pcm.h). */

#include "pcm.h"

#include <string.h>

#include "hot.h"

/* The DC blocker's pole, Q15: 0.995, a corner near 29 Hz at 36.6 kHz,
 * below anything the PicoCalc's speaker reproduces (EL §6.1). */
#define PCM_HP_R 32604

static uint64_t gcd64(uint64_t a, uint64_t b) {
    while (b) {
        uint64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

void pcm_init(pcm_t *p, uint32_t now, uint32_t level_max, uint32_t cpu_hz,
              uint32_t rate_num, uint32_t rate_den) {
    memset(p, 0, sizeof(*p));
    if (rate_num == 0 || rate_den == 0 || cpu_hz == 0) {
        rate_num = ORIC_AUDIO_RATE_NUM;
        rate_den = ORIC_AUDIO_RATE_DEN;
        cpu_hz   = ORIC_CPU_HZ;
    }
    if (level_max == 0) level_max = 1;

    /* Cycles per sample = cpu_hz / (rate_num / rate_den), reduced. At
     * 1 MHz and 150 MHz / 4,096 that is 2,048/75 exactly (§8.4). */
    uint64_t num = (uint64_t)cpu_hz * rate_den;
    uint64_t den = rate_num;
    uint64_t g = gcd64(num, den);
    num /= g;
    den /= g;
    /* A clock with no common factor could leave the fraction too wide
     * for the 32-bit units below; coarsen it rather than overflow. No
     * clk_sys this build uses gets here. */
    while (num > 0x7FFFFFFFu || den > 0x7FFFFFFFu) {
        num >>= 1;
        den >>= 1;
    }
    if (den == 0) den = 1;
    if (num == 0) num = 1;

    p->num = (uint32_t)num;
    p->den = (uint32_t)den;
    p->q   = (uint32_t)(num / den);
    p->r   = (uint32_t)(num % den);
    p->level_max = level_max;
    uint64_t whole = num * level_max;
    p->scale = (((uint64_t)PCM_FULL_SCALE << 32) + whole / 2u) / whole;

    p->start = now;
    p->dc_block = true;
}

void pcm_restart(pcm_t *p, uint32_t now) {
    p->start = now;
    p->start_frac = 0;
    p->pos = 0;
    p->acc = 0;
}

static void ORIC_HOT1(emit)(pcm_t *p, uint64_t acc) {
    int32_t x = (int32_t)((acc * p->scale + 0x80000000u) >> 32);
    int32_t y = x;
    if (p->dc_block) {
        /* y[n] = x[n] - x[n-1] + R y[n-1], with y carried in Q8 so the
         * truncation does not leave a standing offset. */
        p->hp_y = (x - p->hp_x) * 256 +
                  (int32_t)(((int64_t)p->hp_y * PCM_HP_R) >> 15);
        p->hp_x = x;
        y = p->hp_y / 256;
    }
    if (y > INT16_MAX) y = INT16_MAX;
    if (y < INT16_MIN) y = INT16_MIN;

    if (p->count < ORIC_AUDIO_BUF_LEN) p->buf[p->count++] = (int16_t)y;
    else p->overflow++;
}

void ORIC_HOT1(pcm_advance)(pcm_t *p, uint32_t now) {
    /* num is zero only before pcm_init, which would never leave the loop
     * below. The difference is taken in 32 bits so that the cycle
     * counter's wrap cancels; a caller that goes backwards gets nothing. */
    int32_t ahead = (int32_t)(now - p->start);
    if (p->num == 0 || ahead <= 0) return;

    /* Units from the start of the current sample to `now`. */
    uint64_t t = (uint64_t)(uint32_t)ahead * p->den;
    if (t < p->start_frac) return;
    t -= p->start_frac;

    while (t >= p->num) {
        emit(p, p->acc + (uint64_t)p->level * (p->num - p->pos));

        t -= p->num;
        p->start += p->q;
        p->start_frac += p->r;
        if (p->start_frac >= p->den) {
            p->start_frac -= p->den;
            p->start++;
        }
        p->pos = 0;
        p->acc = 0;
    }

    if (t > p->pos) {
        p->acc += (uint64_t)p->level * ((uint32_t)t - p->pos);
        p->pos = (uint32_t)t;
    }
}

size_t pcm_drain(pcm_t *p, int16_t *dst, size_t max) {
    size_t n = p->count < max ? p->count : max;
    memcpy(dst, p->buf, n * sizeof(p->buf[0]));
    p->count -= (uint32_t)n;
    if (p->count) memmove(p->buf, p->buf + n, p->count * sizeof(p->buf[0]));
    return n;
}
