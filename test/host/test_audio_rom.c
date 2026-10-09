/* test_audio_rom.c — the ROMs' own sounds (design.md §8.5, §15.2 M8).
 *
 * Each ROM booted from power-on a cycle at a time, every AY write it
 * makes logged with its stamp, and BASIC's sound commands typed at it:
 * PING, SHOOT, EXPLODE, ZAP and MUSIC, with the key clicks the ROM makes
 * for each key typed. Every sample since power-on is then held against
 * the independent model (ay_model.h) run on the logged writes.
 *
 * Then the pitch of PING and of a MUSIC scale, measured off the output
 * against the datasheet's f = 1 MHz / (16 TP) for the period the ROM
 * wrote; and the keyboard's scan at the prompt, which writes port A for
 * every column every 30 ms and must not move the output.
 *
 * Needs basic10.rom and basic11b.rom (guest.h); skips without them.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "ay_model.h"
#include "guest.h"
#include "test_util.h"

static guest_t g;
static uint64_t reset_at;
static uint8_t last_regs[AY_PORT_A];
static uint8_t last_ports[2];
static uint32_t last_writes, last_starts;
static unsigned doubled;      /* instructions that wrote the AY twice */

/* One instruction, and any write it made to a sound register. A write
 * brings the AY up to its stamp, so ay.t is the stamp afterwards (EL
 * §6.1); a shape written again with the same value counts as a write
 * through env_starts. */
static void logged_step(void) {
    oric_t *m = &g.m;
    oric_run(m, 1);
    unsigned found = 0;
    for (unsigned r = 0; r < AY_PORT_A; r++) {
        bool again = r == AY_ENV_SHAPE && m->ay.env_starts != last_starts;
        if (m->ay.reg[r] != last_regs[r] || again) {
            if (n_writes < MAX_WRITES)
                writes[n_writes++] = (aw_t){ m->ay.t, (uint8_t)r, m->ay.reg[r] };
            last_regs[r] = m->ay.reg[r];
            found++;
        }
    }
    /* `writes` counts changes to any register, the ports' too. */
    for (unsigned p = 0; p < 2u; p++) {
        if (m->ay.reg[AY_PORT_A + p] != last_ports[p]) found++;
        last_ports[p] = m->ay.reg[AY_PORT_A + p];
    }
    uint32_t made = (m->ay.writes - last_writes) + (m->ay.env_starts - last_starts);
    if (made > found) doubled++;
    last_writes = m->ay.writes;
    last_starts = m->ay.env_starts;
}

/* A field the firmware's way, a step at a time: keys, the field's cycles,
 * the AY to the field's end, the samples drained. */
static void logged_field(void) {
    oric_t *m = &g.m;
    keymatrix_field(&g.k, m);
    uint64_t end = m->cpu.cycles + oric_field_cycles(m);
    while (m->cpu.cycles < end) logged_step();
    m->fields++;
    ay8912_advance(&m->ay, m->cpu.cycles, &m->pcm);
    n_core += oric_audio_drain(m, core_out + n_core, MAX_SAMPLES - n_core);
}

static void logged_fields(int n) {
    for (int i = 0; i < n; i++) logged_field();
}

/* Typed as guest_type types, a key at a time through the held set. */
static void logged_type(const char *s) {
    for (; *s; s++) {
        picocalc_event_t ev[ORIC_KEY_TEXT_EVENTS];
        unsigned n = keymap_picocalc_text((uint8_t)*s, ev);
        for (unsigned i = 0; i < n; i++) keymatrix_event(&g.k, ev[i].state, ev[i].code);
        for (int f = 0; f < 200 && !keymatrix_idle(&g.k); f++) logged_field();
        logged_fields((int)ORIC_KEY_GAP_FIELDS);
    }
}

/* Power on with the ROM, as the firmware does, and run to Ready. */
static bool boot(rom_id_t rom, bool average) {
    oric_config_t cfg;
    oric_config_default(&cfg);
    cfg.rom = rom;
    oric_init(&g.m, &cfg);
    g.m.pcm.dc_block = false;
    g.m.ay.avg_off = !average;
    oric_load_rom(&g.m, guest_rom_image(rom), ORIC_ROM_SIZE);
    oric_reset(&g.m);
    reset_at = g.m.ay.t;
    keymatrix_init(&g.k);
    n_writes = n_core = 0;
    memcpy(last_regs, g.m.ay.reg, sizeof last_regs);
    memcpy(last_ports, &g.m.ay.reg[AY_PORT_A], sizeof last_ports);
    last_writes = g.m.ay.writes;
    last_starts = g.m.ay.env_starts;
    doubled = 0;
    for (int f = 0; f < 400 && guest_find_row(&g.m, "Ready", 0) < 0; f++) logged_field();
    if (guest_find_row(&g.m, "Ready", 0) < 0) return false;
    logged_fields(10);
    return true;
}

/* The model over everything since power-on, against the core's samples
 * from `from` to `to`: the largest difference, and the largest through a
 * 64-sample moving average. */
static void compare(size_t from, size_t to, int *max_diff, int *max_lp) {
    size_t n = model_render(writes, n_writes, reset_at, g.m.ay.t, g.m.pcm.num, g.m.pcm.den,
                            model_out, MAX_SAMPLES);
    if (to > n) to = n;
    if (to > n_core) to = n_core;
    *max_diff = *max_lp = 0;
    int64_t a = 0, b = 0;
    for (size_t i = from; i < to; i++) {
        int d = abs(core_out[i] - model_out[i]);
        if (d > *max_diff) *max_diff = d;
        a += core_out[i];
        b += model_out[i];
        if (i >= from + 64u) {
            a -= core_out[i - 64];
            b -= model_out[i - 64];
            int lp = (int)(llabs(a - b) / 64);
            if (lp > *max_lp) *max_lp = lp;
        }
    }
}

/* The frequency of the one tone sounding, from its rising edges between
 * samples `from` and `to`: the output is 0 while the tone is low (the
 * raw level, no DC blocker), and a sample that straddles an edge is high
 * for the fraction of it that it holds of the next sample's level. */
static double measure_hz(size_t from, size_t to, unsigned *edges) {
    double first = -1, last = -1;
    unsigned n = 0;
    for (size_t i = from + 1u; i + 1u < to; i++) {
        if (core_out[i - 1] != 0 || core_out[i] <= 0 || core_out[i + 1] <= 0) continue;
        double frac = (double)core_out[i] / core_out[i + 1];
        if (frac > 1) frac = 1;
        double at = (double)i + 1.0 - frac;
        if (first < 0) first = at;
        last = at;
        n++;
    }
    *edges = n;
    if (n < 2) return 0;
    double rate = (double)ORIC_CPU_HZ * g.m.pcm.den / g.m.pcm.num;
    return (n - 1) / (last - first) * rate;
}

/* The tone period channel A has from the last write before `at`. */
static unsigned tp_at(uint64_t at) {
    unsigned fine = 0, coarse = 0;
    for (size_t i = 0; i < n_writes && writes[i].at <= at; i++) {
        if (writes[i].reg == 0) fine = writes[i].val;
        if (writes[i].reg == 1) coarse = writes[i].val;
    }
    return coarse << 8 | fine;
}

/* The sample in which cycle `at` falls. */
static size_t sample_at(uint64_t at) {
    return (size_t)(at * g.m.pcm.den / g.m.pcm.num);
}

static const char *const commands[] = { "PING", "SHOOT", "EXPLODE", "ZAP", "MUSIC 1,4,10,15" };
#define N_COMMANDS (sizeof commands / sizeof commands[0])

static int test_rom(rom_id_t rom) {
    const char *name = rom == ROM_BASIC10 ? "1.0" : "1.1";

    /* ---- every sample against the model, each tone stepped -------------- */
    for (int average = 0; average <= 1; average++) {
        CHECK(boot(rom, average), "%s: no Ready", name);
        size_t start[N_COMMANDS + 1];
        int quiet_diff, quiet_lp;
        compare(0, n_core, &quiet_diff, &quiet_lp);
        CHECK(quiet_diff <= 1, "%s: the boot differs from the model by %d", name, quiet_diff);
        for (size_t c = 0; c < N_COMMANDS; c++) {
            start[c] = n_core;
            logged_type(commands[c]);
            logged_type("\n");
            logged_fields(100);
        }
        start[N_COMMANDS] = n_core;
        for (size_t c = 0; c < N_COMMANDS; c++) {
            int d, lp;
            compare(start[c], start[c + 1], &d, &lp);
            printf("%s, %s, %s: %zu samples, largest difference %d, %d averaged\n", name,
                   average ? "averaging" : "stepped", commands[c], start[c + 1] - start[c], d, lp);
            /* ZAP sweeps its period up from 0, through the tones that
             * are averaged (§8.2): its bound is test_audio's. */
            bool averaged = average && strcmp(commands[c], "ZAP") == 0;
            if (averaged) {
                CHECK(d > 1, "%s: ZAP was not averaged", name);
                CHECK(lp <= PCM_FULL_SCALE / 100, "%s: ZAP's mean is off by %d", name, lp);
            } else {
                CHECK(d <= 1, "%s, %s: %s differs from the model by %d", name,
                      average ? "averaging" : "stepped", commands[c], d);
            }
        }
        CHECK(doubled == 0, "%s: %u instructions wrote the AY twice, which the log cannot "
              "stamp", name, doubled);
    }

    /* ---- the scan at the prompt is silent ------------------------------- */
    {
        CHECK(boot(rom, true), "%s: no Ready", name);
        uint32_t w0 = g.m.ay.writes;
        size_t from = n_core;
        logged_fields(100);                   /* 2 s at the prompt */
        bool flat = true;
        for (size_t i = from; i < n_core; i++) flat &= core_out[i] == core_out[from];
        CHECK(g.m.ay.writes - w0 > 100u, "%s: the scan wrote port A %u times in 2 s", name,
              g.m.ay.writes - w0);
        CHECK(flat, "%s: the output moved at the prompt", name);
        /* The control: PING in the same window is not flat. */
        from = n_core;
        logged_type("PING\n");
        logged_fields(20);
        flat = true;
        for (size_t i = from; i < n_core; i++) flat &= core_out[i] == core_out[from];
        CHECK(!flat, "%s: PING was flat as well: the check cannot hear", name);
    }

    /* ---- pitch ---------------------------------------------------------- */
    {
        /* PING: the last period it writes, held while its envelope
         * decays over a second. */
        CHECK(boot(rom, true), "%s: no Ready", name);
        logged_type("PING");
        size_t w0 = n_writes;
        logged_type("\n");
        size_t from = n_core;
        logged_fields(40);
        uint64_t last_tp_write = 0;
        for (size_t i = w0; i < n_writes; i++)
            if (writes[i].reg <= 1) last_tp_write = writes[i].at;
        unsigned tp = tp_at(g.m.ay.t);
        size_t s0 = sample_at(last_tp_write) + 50u;
        if (s0 < from) s0 = from;
        unsigned edges;
        double hz = measure_hz(s0, n_core, &edges);
        double want = (double)ORIC_CPU_HZ / (16.0 * tp);
        printf("%s, PING: TP %u, %.3f Hz computed, %.3f Hz measured over %u edges\n", name, tp,
               want, hz, edges);
        CHECK(edges > 500u && fabs(hz / want - 1) < 2e-4, "%s: PING at %.3f Hz, TP %u computes "
              "%.3f", name, hz, tp, want);
        /* The control: the next period's pitch, 4 % away, must not pass. */
        double next = (double)ORIC_CPU_HZ / (16.0 * (tp + 1u));
        CHECK(fabs(hz / next - 1) >= 2e-4, "%s: PING passes as TP %u too", name, tp + 1u);

        /* A scale: twelve notes in octave 3, a fifth of a second each. */
        logged_type("10 FOR N=1 TO 12:MUSIC 1,3,N,15:WAIT 20:NEXT:PLAY 0,0,0,0\n");
        size_t ws = n_writes;
        logged_type("RUN\n");
        logged_fields(150);
        /* Each note is from its period's write to the next. */
        uint64_t at[17];
        unsigned tps[16], notes = 0;
        for (size_t i = ws; i < n_writes && notes < 16u; i++) {
            if (writes[i].reg > 1) continue;
            /* The fine and coarse writes of one note come together. */
            if (notes && writes[i].at - at[notes - 1] < 2000u) {
                tps[notes - 1] = tp_at(writes[i].at);
                continue;
            }
            at[notes] = writes[i].at;
            tps[notes] = tp_at(writes[i].at);
            notes++;
        }
        /* The last is RUN's own Return click, ahead of the program. */
        CHECK(notes >= 12u, "%s: the scale wrote %u periods", name, notes);
        unsigned first = notes > 12u ? notes - 12u : 0u;
        /* The last note ends at PLAY's mixer write. */
        if (notes < 16u) {
            at[notes] = g.m.ay.t;
            for (size_t i = ws; i < n_writes; i++)
                if (writes[i].reg == AY_MIXER && writes[i].at > at[notes - 1]) {
                    at[notes] = writes[i].at;
                    break;
                }
        }
        double prev = 0;
        for (unsigned k = first; k < notes && k < 15u; k++) {
            unsigned e;
            double f = measure_hz(sample_at(at[k]) + 50u, sample_at(at[k + 1]), &e);
            double w = (double)ORIC_CPU_HZ / (16.0 * tps[k]);
            printf("%s, MUSIC 1,3,%u,15: TP %u, %.3f Hz computed, %.3f Hz measured over %u "
                   "edges\n", name, k - first + 1u, tps[k], w, f, e);
            CHECK(e > 20u && fabs(f / w - 1) < 5e-4, "%s: note %u at %.3f Hz, computed %.3f",
                  name, k - first + 1u, f, w);
            CHECK(w > prev, "%s: note %u is not above the last", name, k - first + 1u);
            prev = w;
        }
    }
    return 0;
}

int main(void) {
    const char *dir;
    if (!guest_find_roms(&dir)) {
        printf("skipped: basic10.rom and basic11b.rom are not both in %s\n", dir);
        return TEST_SKIP_CODE;
    }
    if (test_rom(ROM_BASIC10)) return 1;
    if (test_rom(ROM_BASIC11)) return 1;
    TEST_DONE();
}
