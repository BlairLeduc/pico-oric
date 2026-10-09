/* ay8912.h — the AY-3-8912 (design.md §2.3, §8).
 *
 * The part as the VIA sees it (M3): sixteen registers behind an address
 * latch, reached through BDIR and BC1 on a shared data bus, and I/O port
 * A, which drives the keyboard's columns. And its sound (M8): three tone
 * generators, one noise generator and one envelope, mixed and turned
 * into a level that pcm.h box-filters into samples.
 *
 * The chip knows nothing of the Oric. The machine puts the bus mode and
 * the data lines on it with ay8912_bus(), and reads what the chip drives
 * back from `bus_out` while the mode is a read. The datasheet's modes,
 * BDIR and BC1 (BC2 tied high, as on the Oric):
 *
 *   BDIR BC1
 *    0    0   inactive
 *    0    1   read from the latched register
 *    1    0   write to the latched register
 *    1    1   latch an address
 *
 * The bus is level-sensitive: the mode acts for as long as it is held,
 * so a held write follows the data lines, as the part does.
 *
 * The generators count the chip's clock divided by 8, a "tick" every
 * eight cycles, at cycles that are multiples of 8 (§16). Each counts up
 * and acts when its count reaches its period, then starts again from 0,
 * so a period shortened below the count acts on the next tick (§16, as
 * MAME's ay8910.cpp has it):
 *
 *   tone      toggles every max(1, TP) ticks: f = 1 MHz / (16 TP)
 *   noise     every max(1, NP) ticks flips a prescaler, and the 17-bit
 *             LFSR (bit 0 XOR bit 3 in at the top, bit 0 out) shifts
 *             when the prescaler returns to 0: f = 1 MHz / (16 NP)
 *   envelope  steps every max(1, 2 EP) ticks, sixteen steps a cycle:
 *             f = 1 MHz / (256 EP), and EP = 0 is twice EP = 1
 *
 * A channel is (tone | tone off) & (noise | noise off) times its
 * amplitude, register 8-10's low four bits or, with bit 4, the
 * envelope's. Within a cycle, a register write stamped at it acts
 * first, then the tick if there is one, then the level holds for the
 * cycle.
 *
 * The sound is brought up to date lazily (§8.2, EL §4.3): at a write to
 * a sound register, and at the field's end. Between those it runs from
 * event to event, and only through the events that can change the level:
 * a generator nobody can hear is caught up arithmetically when it next
 * matters. A tone whose half period is shorter than a sample is not
 * stepped at all: its mean, half its amplitude, stands in (§8.2).
 */
#ifndef PICO_ORIC_AY8912_H
#define PICO_ORIC_AY8912_H

#include <stdbool.h>
#include <stdint.h>

#include "pcm.h"

#define AY_REG_COUNT  16u

/* Registers with a meaning outside the sound generator. */
#define AY_MIXER      7u      /* bit 6: port A is an output        */
#define AY_ENV_SHAPE 13u
#define AY_PORT_A    14u
#define AY_PORT_B    15u      /* the 8912 has no port B pins        */

#define AY_MIXER_IOA_OUT  0x40u

/* A tick is eight cycles of the chip's clock, which is the 6502's (§16). */
#define AY_TICK_CYCLES 8u

/* The amplitude of level 15 in the volume table (ay8912.c), in mV above
 * level 0, and the mixed level of all three channels at it: what
 * pcm_init is given as the full scale. Levels carry twice the amplitude,
 * so that a tone's mean, half of it, is exact. */
#define AY_AMP_MAX    1433u
#define AY_LEVEL_MAX  (3u * 2u * AY_AMP_MAX)

/* The generators, as indices into next[] and per[]. */
enum { AY_GEN_TONE_A, AY_GEN_TONE_B, AY_GEN_TONE_C, AY_GEN_NOISE, AY_GEN_ENV, AY_GEN_COUNT };

typedef enum {
    AY_BUS_INACTIVE = 0,
    AY_BUS_READ     = 1,
    AY_BUS_WRITE    = 2,
    AY_BUS_LATCH    = 3,
} ay_bus_t;

typedef struct {
    uint8_t reg[AY_REG_COUNT];
    uint8_t addr;           /* the latched register number            */
    bool    selected;       /* the last latch carried select code 0000 */
    uint8_t mode;           /* ay_bus_t, as last put on the pins      */
    bool    driving;        /* in a read: the chip drives the bus     */
    uint8_t bus_out;        /* what it drives while `driving`         */
    uint8_t port_a_in;      /* levels outside on port A's pins        */
    uint32_t writes;        /* register writes, for the heartbeat (§14) */
    uint32_t env_starts;    /* writes to the shape, which restart it   */

    /* ---- the sound (§8) -------------------------------------------- */

    /* Every cycle before `t` is accounted for: its writes applied, its
     * ticks counted and its level given to the pcm. Tick k is at cycle
     * 8k; the first not yet counted is (t + 7) / 8. */
    uint64_t t;

    /* The tick at which each generator next acts, and its period in
     * ticks. A held envelope's next[] means nothing. */
    int64_t  next[AY_GEN_COUNT];
    uint32_t per[AY_GEN_COUNT];

    uint8_t  tone_out;      /* bit per channel                        */
    uint8_t  noise_pre;     /* the noise prescaler                    */
    uint32_t rng;           /* the 17-bit LFSR; bit 0 is the noise    */
    int8_t   env_step;      /* 15 down to 0                           */
    uint8_t  env_attack;    /* 0 or 15, XORed with the step           */
    bool     env_hold, env_alt, env_holding;

    uint8_t  stepped;       /* bit per generator run event by event   */
    uint8_t  averaged;      /* bit per tone whose mean stands in      */
    uint16_t avg_tp;        /* tone periods below this are averaged   */
    bool     avg_off;       /* tests: step every tone, however fast   */

    uint32_t events;        /* ticks run event by event, for §14      */
} ay8912_t;

/* RESET: every register to 0 and every generator to its start, at cycle
 * `now`; the pins float high. The level goes to `out` at once. The
 * averaging threshold is the chip's wiring to the pcm, not its state,
 * and is kept. */
void ay8912_reset(ay8912_t *ay, uint64_t now, pcm_t *out);

/* Put a bus mode and data on the pins at cycle `now`: for a write from
 * the 6502, the start of the writing instruction (EL §6.1). A read
 * leaves the register's value in bus_out with `driving` set; any other
 * mode clears it. A write to a sound register brings the sound up to
 * `now` first. */
void ay8912_bus(ay8912_t *ay, ay_bus_t mode, uint8_t data, uint64_t now, pcm_t *out);

/* Bring the sound up to cycle `now`, into `out`. */
void ay8912_advance(ay8912_t *ay, uint64_t now, pcm_t *out);

/* Average tones whose half period, 8 TP cycles, is shorter than a sample
 * of num/den cycles (pcm.h); the pcm's own num and den. */
void ay8912_set_average(ay8912_t *ay, uint32_t num, uint32_t den);

/* The level now, in the units AY_LEVEL_MAX is in. */
uint32_t ay8912_level(const ay8912_t *ay);

/* The envelope's output now, 0-15. */
static inline unsigned ay8912_env_volume(const ay8912_t *ay) {
    return (unsigned)((ay->env_step ^ ay->env_attack) & 0x0F);
}

/* The amplitude of a 4-bit level, in mV above level 0 (ay8912.c). */
uint16_t ay8912_amplitude(unsigned level);

/* Port A's pins as the outside world sees them: the register while the
 * mixer makes the port an output, otherwise whatever pulls them. */
static inline uint8_t ay8912_port_a(const ay8912_t *ay) {
    return (ay->reg[AY_MIXER] & AY_MIXER_IOA_OUT) ? ay->reg[AY_PORT_A] : ay->port_a_in;
}

#endif /* PICO_ORIC_AY8912_H */
