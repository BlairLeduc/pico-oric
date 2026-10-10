/* snap_util.h — what test_snapshot and test_snapshot_rom share: a
 * stream in memory, and the comparison of two machines (design.md
 * §10.6, §15.2 M11).
 *
 * The comparison reads oric_t, not the format, so a field the format
 * leaves out shows as a difference rather than as two equal omissions.
 * It covers every field that is machine state: what is not (the port's
 * pcm, the counters for the heartbeat, the keys) is named where it is
 * skipped.
 */
#ifndef PICO_ORIC_TEST_SNAP_UTIL_H
#define PICO_ORIC_TEST_SNAP_UTIL_H

#include <stdio.h>
#include <string.h>

#include "oric.h"
#include "snapshot.h"

typedef struct {
    uint8_t buf[SNAP_FILE_LEN + 16u];
    size_t  len, pos;
    size_t  fail_at;     /* fail the call that would cross this; 0 never */
} mem_t;

static inline bool mem_write(void *ctx, const uint8_t *src, size_t n) {
    mem_t *s = ctx;
    if (s->len + n > sizeof s->buf) return false;
    if (s->fail_at && s->len + n > s->fail_at) return false;
    memcpy(s->buf + s->len, src, n);
    s->len += n;
    return true;
}

static inline bool mem_read(void *ctx, uint8_t *dst, size_t n) {
    mem_t *s = ctx;
    if (s->pos + n > s->len) return false;
    memcpy(dst, s->buf + s->pos, n);
    s->pos += n;
    return true;
}

static inline snap_status_t mem_save(mem_t *s, const oric_t *m) {
    s->len = 0;
    return snapshot_save(m, NULL, mem_write, s);
}

static inline snap_status_t mem_check(mem_t *s, const oric_t *m, snap_info_t *info) {
    s->pos = 0;
    return snapshot_check(m, mem_read, s, info, NULL);
}

static inline snap_status_t mem_load(mem_t *s, oric_t *m) {
    s->pos = 0;
    return snapshot_load(m, mem_read, s);
}

/* The state section's CRC again, after a test has changed a byte of it
 * on purpose (snapshot.h's header). */
static inline void mem_recrc(mem_t *s) {
    uint32_t crc = snapshot_crc32(0, s->buf + SNAP_HEADER_LEN, s->len - SNAP_HEADER_LEN);
    for (unsigned i = 0; i < 4; i++) s->buf[16 + i] = (uint8_t)(crc >> (8 * i));
}

/* The sound, compared sample for sample: a restore starts the sample
 * grid at the restored clock (oric.h), so the original's grid is started
 * there too, after draining what it had made, and the DC blocker is off
 * in both, its state being the port's and not the machine's. */
#define SNAP_SOUND_MAX (160u * 800u)

static inline void snap_sound_start(oric_t *m) {
    int16_t scratch[ORIC_AUDIO_BUF_LEN];
    while (oric_audio_drain(m, scratch, ORIC_AUDIO_BUF_LEN)) {}
    pcm_restart(&m->pcm, (uint32_t)m->cpu.cycles);
}

/* After a field: what it made, onto out at *n. */
static inline void snap_sound_take(oric_t *m, int16_t *out, size_t *n) {
    int16_t scratch[ORIC_AUDIO_BUF_LEN];
    size_t k;
    while ((k = oric_audio_drain(m, scratch, ORIC_AUDIO_BUF_LEN)) != 0) {
        if (out && *n + k <= SNAP_SOUND_MAX) memcpy(out + *n, scratch, k * sizeof scratch[0]);
        *n += k;
    }
}

static inline bool snap_same_sound(const int16_t *a, size_t na, const int16_t *b, size_t nb,
                                   const char *what) {
    if (na != nb || na > SNAP_SOUND_MAX) {
        fprintf(stderr, "    %s: %zu samples vs %zu\n", what, na, nb);
        return false;
    }
    for (size_t i = 0; i < na; i++) {
        if (a[i] != b[i]) {
            fprintf(stderr, "    %s: sample %zu of %zu: %d vs %d\n", what, i, na, a[i], b[i]);
            return false;
        }
    }
    return true;
}

#define SNAP_DIFF(cond, ...) \
    do { if (cond) { fprintf(stderr, "    %s: ", what); fprintf(stderr, __VA_ARGS__); \
                     fputc('\n', stderr); return false; } } while (0)

/* Every byte of machine state the same. */
static inline bool snap_same(const oric_t *a, const oric_t *b, const char *what) {
    for (size_t i = 0; i < ORIC_ADDR_SPACE; i++)
        SNAP_DIFF(a->ram[i] != b->ram[i], "RAM #%04zX %02X vs %02X", i, a->ram[i], b->ram[i]);

    const m6502_t *x = &a->cpu, *y = &b->cpu;
    SNAP_DIFF(x->pc != y->pc || x->a != y->a || x->x != y->x || x->y != y->y || x->s != y->s ||
              x->p != y->p, "CPU PC %04X/%04X A %02X/%02X S %02X/%02X P %02X/%02X",
              x->pc, y->pc, x->a, y->a, x->s, y->s, x->p, y->p);
    SNAP_DIFF(x->cycles != y->cycles, "cycles %llu vs %llu",
              (unsigned long long)x->cycles, (unsigned long long)y->cycles);
    SNAP_DIFF(x->irq_lines != y->irq_lines || x->nmi_pending != y->nmi_pending ||
              x->nmi_line != y->nmi_line || x->reset_pending != y->reset_pending,
              "interrupt lines");
    SNAP_DIFF(x->i_old != y->i_old || x->i_old_at != y->i_old_at, "the delayed I");

    /* T2 and the shift clock as of now in both (via6522.h). The pulse
     * counts are for tests of the part, and nothing reads them here. */
    via6522_t v = a->via, w = b->via;
    via6522_sync(&v);
    via6522_sync(&w);
    SNAP_DIFF(v.orb != w.orb || v.ora != w.ora || v.ddrb != w.ddrb || v.ddra != w.ddra ||
              v.in_a != w.in_a || v.in_b != w.in_b || v.ira != w.ira || v.irb != w.irb ||
              v.sr != w.sr || v.acr != w.acr || v.pcr != w.pcr || v.ifr != w.ifr ||
              v.ier != w.ier, "VIA registers");
    SNAP_DIFF(v.t1_latch != w.t1_latch || v.t2_latch_lo != w.t2_latch_lo || v.t1 != w.t1 ||
              v.t2 != w.t2 || v.t1_armed != w.t1_armed || v.t2_armed != w.t2_armed,
              "VIA timers T1 %d/%d T2 %d/%d", (int)v.t1, (int)w.t1, (int)v.t2, (int)w.t2);
    SNAP_DIFF(v.pb7 != w.pb7 || v.ca1 != w.ca1 || v.ca2 != w.ca2 || v.cb1 != w.cb1 ||
              v.cb2 != w.cb2 || v.sr_halves != w.sr_halves || v.sr_timer != w.sr_timer ||
              v.ev != w.ev || v.ev_span != w.ev_span, "VIA lines or shift register");
    SNAP_DIFF(a->via_early != b->via_early, "VIA early");

    /* The AY, less the counters for the heartbeat (writes, env_starts,
     * events) and its wiring to the pcm (avg_tp, avg_off). */
    const ay8912_t *p = &a->ay, *q = &b->ay;
    SNAP_DIFF(memcmp(p->reg, q->reg, AY_REG_COUNT) != 0, "AY registers");
    SNAP_DIFF(p->addr != q->addr || p->selected != q->selected || p->mode != q->mode ||
              p->driving != q->driving || p->bus_out != q->bus_out ||
              p->port_a_in != q->port_a_in, "AY bus");
    SNAP_DIFF(p->t != q->t, "AY t %llu vs %llu", (unsigned long long)p->t,
              (unsigned long long)q->t);
    for (unsigned g = 0; g < AY_GEN_COUNT; g++)
        SNAP_DIFF(p->next[g] != q->next[g] || p->per[g] != q->per[g],
                  "AY generator %u next %lld/%lld per %u/%u", g, (long long)p->next[g],
                  (long long)q->next[g], (unsigned)p->per[g], (unsigned)q->per[g]);
    SNAP_DIFF(p->tone_out != q->tone_out || p->noise_pre != q->noise_pre || p->rng != q->rng,
              "AY tone, noise or LFSR");
    SNAP_DIFF(p->env_step != q->env_step || p->env_attack != q->env_attack ||
              p->env_hold != q->env_hold || p->env_alt != q->env_alt ||
              p->env_holding != q->env_holding, "AY envelope");
    SNAP_DIFF(p->stepped != q->stepped || p->averaged != q->averaged, "AY stepped or averaged");

    SNAP_DIFF(a->ula_mode != b->ula_mode || a->frame_mode != b->frame_mode, "ULA mode");
    SNAP_DIFF(a->fields != b->fields, "fields %u vs %u", (unsigned)a->fields, (unsigned)b->fields);
    SNAP_DIFF(a->open_bus != b->open_bus, "open bus");
    SNAP_DIFF(a->budget != b->budget, "budget %d vs %d", (int)a->budget, (int)b->budget);

    const tape_t *s = &a->tape, *t = &b->tape;
    SNAP_DIFF(s->op != t->op || s->header_kept != t->header_kept || s->pass != t->pass ||
              (s->pass && s->pass_pc != t->pass_pc), "tape trap");
    SNAP_DIFF(s->header_kept && (memcmp(s->raw, t->raw, sizeof s->raw) != 0 ||
                                 s->name_len != t->name_len ||
                                 memcmp(s->name, t->name, s->name_len) != 0), "kept header");

    /* The Microdisc (microdisc.h): the latch, the map it makes, and the
     * controller between commands. */
    const wd1793_t *f = &a->fdc, *g = &b->fdc;
    SNAP_DIFF(a->md_latch != b->md_latch, "Microdisc latch %02X vs %02X", a->md_latch, b->md_latch);
    for (unsigned pg = 0; pg < ORIC_PAGE_COUNT; pg++)
        SNAP_DIFF(a->page_flags[pg] != b->page_flags[pg] ||
                  (a->page[pg].read == NULL) != (b->page[pg].read == NULL), "page #%02X", pg);
    SNAP_DIFF(f->status != g->status || f->track != g->track || f->sector != g->sector ||
              f->data != g->data || f->cmd != g->cmd, "WD1793 registers");
    SNAP_DIFF(f->intrq != g->intrq || f->drq != g->drq || f->type1 != g->type1 ||
              f->hold_intrq != g->hold_intrq || f->index_intrq != g->index_intrq ||
              f->step_in != g->step_in || f->drive != g->drive || f->side != g->side,
              "WD1793 lines");
    for (unsigned d = 0; d < ORIC_DISC_DRIVES; d++)
        SNAP_DIFF(f->drv[d].cyl != g->drv[d].cyl, "drive %u's head %u vs %u", d,
                  f->drv[d].cyl, g->drv[d].cyl);
    SNAP_DIFF(f->head_until != g->head_until || f->due != g->due, "WD1793 times");
    return true;
}

#endif /* PICO_ORIC_TEST_SNAP_UTIL_H */
