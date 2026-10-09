/* ay8912.c — the AY-3-8912 (scope in ay8912.h). */

#include "ay8912.h"

#include <string.h>

#include "hot.h"

/* Bits a register keeps; the rest read back as zero (General
 * Instrument AY-3-8910/8912 data manual, the register array). */
static const uint8_t reg_mask[AY_REG_COUNT] = {
    0xFF, 0x0F, 0xFF, 0x0F, 0xFF, 0x0F,   /* tone periods, fine and coarse */
    0x1F,                                 /* noise period                  */
    0xFF,                                 /* mixer and I/O enable          */
    0x1F, 0x1F, 0x1F,                     /* amplitudes                    */
    0xFF, 0xFF,                           /* envelope period               */
    0x0F,                                 /* envelope shape                */
    0xFF, 0xFF,                           /* I/O ports A and B             */
};

/* The amplitude of each 4-bit level, in mV above level 0: Matthew
 * Westcott's measurements of an AY-3-8912 in a Spectrum 128, channel C
 * held high by the mixer and register 10 stepped, placed by him in the
 * public domain (comp.sys.sinclair, December 2001; quoted in MAME's
 * ay8910.cpp). Measured: 1.147, 1.162, 1.169, 1.178, 1.192, 1.213,
 * 1.238, 1.299, 1.336, 1.457, 1.573, 1.707, 1.882, 2.06, 2.32, 2.58 V
 * (design.md §8.3, §16). */
static const uint16_t amp_mv[16] = {
       0,   15,   22,   31,   45,   66,   91,  152,
     189,  310,  426,  560,  735,  913, 1173, 1433,
};
_Static_assert(AY_AMP_MAX == 1433u, "AY_AMP_MAX is level 15's amplitude");

/* The LFSR's period: x^17 + x^14 + 1 is primitive, so every state but 0
 * comes round again after 2^17 - 1 shifts (test_ay8912 counts them). */
#define AY_LFSR_PERIOD 131071u

uint16_t ay8912_amplitude(unsigned level) {
    return amp_mv[level & 0x0Fu];
}

/* The first tick at or after cycle t. */
static inline int64_t first_tick(uint64_t t) {
    return (int64_t)((t + (AY_TICK_CYCLES - 1u)) / AY_TICK_CYCLES);
}

/* Shift the LFSR n times. Fourteen shifts at once: each new bit is
 * bit j XOR bit j + 3 of the old state, and for j < 14 both are old
 * bits. */
static void ORIC_HOT1(lfsr_run)(ay8912_t *ay, uint32_t n) {
    uint32_t r = ay->rng;
    while (n >= 14u) {
        r = (r >> 14) | (((r ^ (r >> 3)) & 0x3FFFu) << 3);
        n -= 14u;
    }
    while (n--) r = (r >> 1) | (((r ^ (r >> 3)) & 1u) << 16);
    ay->rng = r;
}

/* n envelope steps from where it is. Each counts the step down; past 0
 * it holds, or starts again from 15, with the attack inverted if it
 * alternates (MAME's ay8910.cpp, which settles the shapes' drawings
 * into these three bits). */
static void ORIC_HOT1(env_run)(ay8912_t *ay, uint64_t n) {
    uint64_t total = (uint64_t)(15 - ay->env_step) + n;
    if (total <= 15u) {
        ay->env_step = (int8_t)(15u - total);
    } else if (ay->env_hold) {
        if (ay->env_alt) ay->env_attack ^= 0x0Fu;
        ay->env_holding = true;
        ay->env_step = 0;
    } else {
        if (ay->env_alt && ((total / 16u) & 1u)) ay->env_attack ^= 0x0Fu;
        ay->env_step = (int8_t)(15u - total % 16u);
    }
}

/* Run generator g through every tick before k_end. */
static void ORIC_HOT1(catch_up)(ay8912_t *ay, unsigned g, int64_t k_end) {
    if (g == AY_GEN_ENV && ay->env_holding) return;
    int64_t nx = ay->next[g];
    if (nx >= k_end) return;
    uint32_t p = ay->per[g];
    uint64_t span = (uint64_t)(k_end - 1 - nx);
    /* One event, the common case in the event loop, without dividing. */
    uint64_t n = span < p ? 1u : 1u + span / p;
    ay->next[g] = nx + (int64_t)(n * p);

    if (g <= AY_GEN_TONE_C) {
        if (n & 1u) ay->tone_out ^= (uint8_t)(1u << g);
    } else if (g == AY_GEN_NOISE) {
        /* The LFSR shifts each time the prescaler comes back to 0. */
        uint64_t shifts = ay->noise_pre ? (n + 1u) / 2u : n / 2u;
        ay->noise_pre ^= (uint8_t)(n & 1u);
        lfsr_run(ay, (uint32_t)(shifts % AY_LFSR_PERIOD));
    } else {
        env_run(ay, n);
    }
}

/* The level, and which generators it depends on (ay8912.h): a tone is
 * stepped where its channel can sound and its half period is a sample
 * or longer, averaged where it is shorter; the noise where any channel
 * that can sound takes it; the envelope where any channel follows it
 * and it is not holding. */
static uint32_t ORIC_HOT1(mix)(const ay8912_t *ay, uint8_t *stepped, uint8_t *averaged) {
    uint8_t m = ay->reg[AY_MIXER];
    uint32_t level = 0;
    uint8_t st = 0, av = 0;
    bool noise = false, env = false;
    for (unsigned i = 0; i < 3u; i++) {
        uint8_t a = ay->reg[8u + i];
        bool follows = (a & 0x10u) != 0;
        if (!follows && !(a & 0x0Fu)) continue;
        env |= follows;
        unsigned f;
        if (m & (1u << i)) {
            f = 2u;                                /* tone off: high */
        } else if (ay->per[i] < ay->avg_tp && !ay->avg_off) {
            f = 1u;                                /* its mean       */
            av |= (uint8_t)(1u << i);
        } else {
            f = (ay->tone_out >> i & 1u) * 2u;
            st |= (uint8_t)(1u << i);
        }
        if (!(m & (8u << i))) {
            noise = true;
            if (!(ay->rng & 1u)) f = 0;
        }
        level += amp_mv[follows ? ay8912_env_volume(ay) : (a & 0x0Fu)] * f;
    }
    if (noise) st |= 1u << AY_GEN_NOISE;
    if (env && !ay->env_holding) st |= 1u << AY_GEN_ENV;
    *stepped = st;
    *averaged = av;
    return level;
}

static uint32_t ORIC_HOT1(remix)(ay8912_t *ay) {
    return mix(ay, &ay->stepped, &ay->averaged);
}

uint32_t ay8912_level(const ay8912_t *ay) {
    uint8_t st, av;
    return mix(ay, &st, &av);
}

void ORIC_HOT1(ay8912_advance)(ay8912_t *ay, uint64_t now, pcm_t *out) {
    if ((int64_t)(now - ay->t) <= 0) return;
    int64_t k_end = first_tick(now);

    /* Event to event through the generators the level depends on. */
    for (;;) {
        uint8_t st = ay->stepped;
        int64_t e = INT64_MAX;
        for (unsigned g = 0; g < AY_GEN_COUNT; g++)
            if ((st >> g & 1u) && ay->next[g] < e) e = ay->next[g];
        if (e >= k_end) break;

        uint32_t at = (uint32_t)((uint64_t)e * AY_TICK_CYCLES);
        pcm_advance(out, at);
        for (unsigned g = 0; g < AY_GEN_COUNT; g++)
            if ((st >> g & 1u) && ay->next[g] == e) catch_up(ay, g, e + 1);
        ay->events++;
        pcm_set_level(out, at, remix(ay));
    }
    pcm_advance(out, (uint32_t)now);

    /* The rest, which the level does not depend on, arithmetically. */
    for (unsigned g = 0; g < AY_GEN_COUNT; g++) catch_up(ay, g, k_end);
    ay->t = now;
}

/* A new period p for generator g, from the first tick not yet counted,
 * k0. The count carries on: it acts when the count reaches p, or on the
 * next tick if the count has passed it already. */
static void set_period(ay8912_t *ay, unsigned g, uint32_t p, int64_t k0) {
    if (p == 0) p = 1;
    if (!(g == AY_GEN_ENV && ay->env_holding)) {
        int64_t nx = ay->next[g] - (int64_t)ay->per[g] + (int64_t)p;
        ay->next[g] = nx < k0 ? k0 : nx;
    }
    ay->per[g] = p;
}

/* A write to register 13 starts the envelope from its first step. The
 * count runs on, unless it was holding, when it starts from 0. A shape
 * without Continue is the one with Continue that holds at 0 (MAME). */
static void set_shape(ay8912_t *ay, uint8_t v, int64_t k0) {
    ay->env_attack = (v & 0x04u) ? 0x0Fu : 0;
    if (!(v & 0x08u)) {
        ay->env_hold = true;
        ay->env_alt = ay->env_attack != 0;
    } else {
        ay->env_hold = (v & 0x01u) != 0;
        ay->env_alt = (v & 0x02u) != 0;
    }
    ay->env_step = 15;
    if (ay->env_holding) {
        ay->env_holding = false;
        ay->next[AY_GEN_ENV] = k0 - 1 + (int64_t)ay->per[AY_GEN_ENV];
    }
}

static void ORIC_HOT1(write_sound)(ay8912_t *ay, unsigned r, uint8_t v, uint64_t now, pcm_t *out) {
    ay8912_advance(ay, now, out);
    int64_t k0 = first_tick(ay->t);
    ay->reg[r] = v;
    switch (r) {
    case 0: case 1: case 2: case 3: case 4: case 5: {
        unsigned c = r >> 1;
        set_period(ay, c, (uint32_t)ay->reg[2u * c] | (uint32_t)ay->reg[2u * c + 1u] << 8, k0);
        break;
    }
    case 6:
        set_period(ay, AY_GEN_NOISE, ay->reg[6], k0);
        break;
    case 11: case 12:
        set_period(ay, AY_GEN_ENV, 2u * ((uint32_t)ay->reg[11] | (uint32_t)ay->reg[12] << 8), k0);
        break;
    case AY_ENV_SHAPE:
        set_shape(ay, v, k0);
        break;
    default:
        break;      /* the mixer and the amplitudes: the level alone */
    }
    pcm_set_level(out, (uint32_t)ay->t, remix(ay));
}

void ay8912_reset(ay8912_t *ay, uint64_t now, pcm_t *out) {
    /* RESET clears every register; the pins float high. */
    uint16_t avg_tp = ay->avg_tp;
    bool avg_off = ay->avg_off;
    memset(ay, 0, sizeof(*ay));
    ay->avg_tp = avg_tp;
    ay->avg_off = avg_off;
    ay->port_a_in = 0xFFu;
    ay->selected = true;    /* register 0, as RESET leaves the latch */

    /* Every count at 0 and every period 1, so each acts on the first
     * tick; the LFSR at 1 and the envelope as shape 0 leaves it, as
     * MAME resets the part (§16). */
    ay->t = now;
    int64_t k0 = first_tick(now);
    for (unsigned g = 0; g < AY_GEN_COUNT; g++) {
        ay->per[g] = 1;
        ay->next[g] = k0;
    }
    ay->rng = 1;
    set_shape(ay, 0, k0);
    pcm_set_level(out, (uint32_t)now, remix(ay));
}

void ay8912_set_average(ay8912_t *ay, uint32_t num, uint32_t den) {
    /* 8 TP den < num, as a least TP that is stepped. */
    uint64_t d = (uint64_t)AY_TICK_CYCLES * den;
    uint64_t tp = d ? ((uint64_t)num + d - 1u) / d : 0;
    ay->avg_tp = (uint16_t)(tp > 0xFFFFu ? 0xFFFFu : tp);
    (void)remix(ay);
}

void ORIC_HOT1(ay8912_bus)(ay8912_t *ay, ay_bus_t mode, uint8_t data, uint64_t now, pcm_t *out) {
    bool held = ay->mode == AY_BUS_WRITE;
    ay->mode = (uint8_t)mode;
    ay->driving = false;
    switch (mode) {
    case AY_BUS_LATCH:
        /* The high nibble is the chip's select code, 0000 on the 8912:
         * any other value deselects it, and it ignores reads and writes
         * until an address with the right code is latched. */
        ay->selected = (data & 0xF0u) == 0;
        if (ay->selected) ay->addr = data;
        break;
    case AY_BUS_WRITE: {
        if (!ay->selected) break;
        uint8_t v = (uint8_t)(data & reg_mask[ay->addr]);
        /* A write held while something else on the VIA changes is the
         * same write, and restarts no envelope; a new one, or new data
         * under it, is a write. */
        bool restart = ay->addr == AY_ENV_SHAPE && (!held || ay->reg[AY_ENV_SHAPE] != v);
        if (ay->reg[ay->addr] == v && !restart) break;
        if (ay->reg[ay->addr] != v) ay->writes++;
        if (restart) ay->env_starts++;
        /* The ports make no sound: the keyboard's scan, a write to
         * port A for every column, never wakes the generators (§8.5). */
        if (ay->addr < AY_PORT_A) write_sound(ay, ay->addr, v, now, out);
        else ay->reg[ay->addr] = v;
        break;
    }
    case AY_BUS_READ:
        if (!ay->selected) break;    /* the bus stays undriven */
        ay->driving = true;
        /* An input port reads its pins, not the register. The 8912 has
         * no port B pins; the register reads back. */
        ay->bus_out = (ay->addr == AY_PORT_A) ? ay8912_port_a(ay) : ay->reg[ay->addr];
        break;
    case AY_BUS_INACTIVE:
        break;
    }
}
