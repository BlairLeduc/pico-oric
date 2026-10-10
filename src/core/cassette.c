/* cassette.c — tape phase 2: the signal (cassette.h, design.md §10.4).
 *
 * The half-cycles are the ROMs' writer's, read off both 2026-10-09 and
 * held to their recorded signal by test_cassette. In 1.1 (1.0's in
 * brackets) the set-up at #E76A (#E6CA) starts T1 free-running with its
 * output on PB7 (ACR #C0) at latch #00D0. The bit routine #E68B (#E5F3)
 * writes the latch's two bytes at #E6BA (#E621), #D0 for a 1 or #1A0
 * for a 0, and waits at #E6C0 (#E627) for T1's flag: so each wait ends a
 * period and begins one of the latch just written, and a half-cycle is
 * the latch + 2, 210 or 418 cycles, PB7 changing at each. Fast, a bit
 * is a #D0 period then its own; slow (#024D, #67), its own period, then
 * fifteen more waits for a 1 or seven for a 0 (#E69F, #E606). The byte
 * routine #E65E (#E5C6) first waits once at the latch left, which is
 * #D0 after the last stop bit, then sends a 0, the byte from bit 0,
 * parity making the ones odd, and three 1s. The leader #E75A (#E6BA) is
 * 259 #16s.
 *
 * Between the name's zero and the data, T1 runs on at #D0 while the CPU
 * does something else, and the data's first wait ends whichever period
 * it finds: 1.1 waits in a loop at #E628, always six periods more than
 * between two bytes; 1.0 prints "Saving" and the name on the status
 * line, 564 cycles and 19 a letter, measured to 38 letters, which comes
 * to three periods more for up to nine letters and one more for each
 * eleven after (test_cassette).
 */

#include "cassette.h"

#include <string.h>

#include "hot.h"
#include "oric.h"
#include "tap.h"
#include "tape.h"
#include "via6522.h"

/* ---- the walk ------------------------------------------------------------ */

enum { P_SCAN, P_RAW, P_EXTRA, P_HEAD, P_GAP, P_DATA, P_FILE_GAP, P_END };

/* 1.0's gap: the periods its print of the name runs into, the print
 * being 564 + 19n cycles (cassette.c's head). Three periods to n = 9;
 * the slack of 30 is inside what the measured steps allow (22-38). */
#define GAP10_BASE      3u
#define GAP10_SLACK    30u
#define GAP10_PER      19u
#define GAP10_MAX      38u
#define GAP11           6u

uint16_t cassette_gap(const oric_t *m, uint32_t name_len) {
    const tape_rom_t *r = m->tape.rom;
    if (!r || r->name_cap != 0xFFu) return GAP11;   /* 1.1, and any other ROM */
    uint32_t n = name_len < GAP10_MAX ? name_len : GAP10_MAX;
    return (uint16_t)(GAP10_BASE + (GAP10_SLACK + GAP10_PER * n) / CAS_SHORT);
}

/* The bit of `byte` at 1-13: a 0, the byte from bit 0, odd parity,
 * three 1s. */
static bool bit_of(uint8_t byte, uint8_t bit) {
    if (bit == 1u) return false;
    if (bit <= 9u) return (byte >> (bit - 2u)) & 1u;
    if (bit == 10u) return (__builtin_popcount(byte) & 1u) == 0;
    return true;
}

/* Where the walk goes when it is at `pos` between files. */
static void scan(oric_t *m) {
    cassette_t *c = &m->cas;
    if (c->pos >= c->len) {
        c->part = P_END;
        return;
    }
    tap_header_t h;
    size_t skip;
    if (tap_scan(c->img + c->pos, c->len - c->pos, true, &h, &skip) != TAP_FOUND) {
        /* No header in the rest: its bytes as they are. */
        c->head_at = c->len;
        c->part = P_RAW;
        c->part_left = c->len - c->pos;
        return;
    }
    c->head_at = c->pos + (uint32_t)skip;
    c->data_at = c->pos + h.data_at;
    uint32_t want = tap_data_len(tap_start(h.raw), tap_end(h.raw));
    uint32_t have = c->len - c->data_at;
    c->data_n = want < have ? want : have;
    c->gap = cassette_gap(m, h.name_len);
    c->part = skip ? P_RAW : P_EXTRA;
    c->part_left = (uint32_t)skip;
    if (!skip) {
        uint32_t n = 0;
        while (c->img[c->head_at + n] == TAP_SYNC) n++;
        c->part_left = n < CAS_LEADER ? CAS_LEADER - n : 0;
    }
}

/* Move to the part after the one just finished. */
static void next_part(oric_t *m) {
    cassette_t *c = &m->cas;
    switch (c->part) {
    case P_RAW:
        if (c->head_at >= c->len) { c->part = P_END; return; }
        {
            uint32_t n = 0;
            while (c->img[c->head_at + n] == TAP_SYNC) n++;
            c->part = P_EXTRA;
            c->part_left = n < CAS_LEADER ? CAS_LEADER - n : 0;
        }
        return;
    case P_EXTRA:
        c->files++;
        c->part = P_HEAD;
        c->pos = c->head_at;
        c->part_left = c->data_at - c->head_at;
        return;
    case P_HEAD:
        c->part = P_GAP;
        c->part_left = c->gap;
        return;
    case P_GAP:
        c->part = P_DATA;
        c->part_left = c->data_n;
        return;
    case P_DATA:
        c->part = P_FILE_GAP;
        c->part_left = CAS_FILE_GAP;
        return;
    default:
        c->part = P_SCAN;
        scan(m);
        return;
    }
}

/* The byte the part plays next, or false for an idle part. */
static bool part_byte(cassette_t *c, uint8_t *b) {
    switch (c->part) {
    case P_EXTRA: *b = TAP_SYNC; return true;
    case P_RAW:
    case P_HEAD:
    case P_DATA:  *b = c->img[c->pos++]; return true;
    default:      return false;
    }
}

/* The next half-cycle, and move past it. */
static bool step(oric_t *m, bool slow, uint32_t *t) {
    cassette_t *c = &m->cas;
    for (;;) {
        if (c->bit) {
            /* In a byte: bits 1-13, a half at a time. */
            bool one = bit_of(c->byte, c->bit);
            uint8_t halves;
            if (slow) {
                halves = one ? 16u : 8u;
                *t = one ? CAS_SHORT : CAS_LONG;
            } else {
                halves = 2u;
                *t = (c->half == 0 || one) ? CAS_SHORT : CAS_LONG;
            }
            if (++c->half == halves) {
                c->half = 0;
                if (++c->bit == 14u) c->bit = 0;
            }
            return true;
        }
        if (c->part == P_END) return false;
        if (c->part == P_SCAN || !c->part_left) {
            next_part(m);
            continue;
        }
        c->part_left--;
        uint8_t b;
        if (part_byte(c, &b)) {
            /* The byte routine's first wait, at the latch the last stop
             * bit left: one short period. */
            c->byte = b;
            c->bit = 1;
            c->half = 0;
        }
        *t = CAS_SHORT;
        return true;
    }
}

static void rewind_walk(cassette_t *c) {
    c->pos = 0;
    c->part = P_SCAN;
    c->part_left = 0;
    c->bit = c->half = 0;
    c->files = 0;
}

bool cassette_walk(oric_t *m, bool from_start, bool slow, uint32_t *t) {
    if (from_start) rewind_walk(&m->cas);
    return step(m, slow, t);
}

/* ---- the player ------------------------------------------------------------ */

static void set_live(cassette_t *c) {
    uint64_t was = c->due;
    c->live = c->playing || c->rec.on;
    c->due = c->rec.on ? 0 : c->playing ? c->next : UINT64_MAX;
    if (c->due < was) c->cut = true;
}

/* The next change into next, or the end of the tape. */
static void schedule(oric_t *m) {
    cassette_t *c = &m->cas;
    uint32_t t;
    if (!step(m, c->slow, &t)) {
        c->playing = false;
        c->ended = true;
        c->left = 0;
        set_live(c);
        return;
    }
    c->next += t;
}

static void play_to(oric_t *m, uint64_t now) {
    cassette_t *c = &m->cas;
    while (c->playing && now >= c->next) {
        c->level = !c->level;
        c->edges++;
        via6522_set_cb1(&m->via, c->level);
        schedule(m);
    }
    set_live(c);
}

/* The ROM's speed setting, which CLOAD and CSAVE set before the relay. */
static bool rom_slow(const oric_t *m) {
    const tape_rom_t *r = m->tape.rom;
    return r && oric_peek(m, r->slow) != 0;
}

/* The cycle now: inside an instruction, its access's (bus.h). */
static uint64_t now_of(const oric_t *m) {
    return m->cpu.cycles + m->cpu.io_at;
}

static void stop_player(oric_t *m) {
    cassette_t *c = &m->cas;
    if (!c->playing) return;
    uint64_t now = now_of(m);
    play_to(m, now);
    if (!c->playing) return;               /* it ended just now */
    c->left = (uint32_t)(c->next - now);
    c->playing = false;
    set_live(c);
}

/* Run or hold, from the relay, the PLAY key and the recorder. */
static void update(oric_t *m) {
    cassette_t *c = &m->cas;
    bool run = c->loaded && !c->ended && !c->rec.on && (c->motor || c->by_hand);
    if (run && !c->playing) {
        c->slow = rom_slow(m);
        c->next = now_of(m) + c->left;
        c->playing = true;
        set_live(c);
        via6522_set_cb1(&m->via, c->level);
    } else if (!run) {
        stop_player(m);
    }
    set_live(c);
}

static void rewind_deck(oric_t *m) {
    cassette_t *c = &m->cas;
    rewind_walk(c);
    c->ended = false;
    c->playing = false;
    c->level = true;                        /* CB1's idle, pulled up */
    uint32_t t;
    if (c->loaded && step(m, false, &t)) {
        c->left = t;
    } else {
        c->left = 0;
        c->ended = true;
    }
    set_live(c);
}

void oric_cassette_insert(oric_t *m, const uint8_t *img, uint32_t len) {
    cassette_t *c = &m->cas;
    stop_player(m);
    c->img = img;
    c->len = img ? len : 0;
    c->loaded = img != NULL;
    c->by_hand = false;
    rewind_deck(m);
    update(m);
}

void oric_cassette_rewind(oric_t *m) {
    stop_player(m);
    rewind_deck(m);
    update(m);
}

void oric_cassette_play(oric_t *m, bool on) {
    m->cas.by_hand = on;
    update(m);
}

bool oric_cassette_running(const oric_t *m) {
    return m->cas.playing || m->cas.rec.on;
}

/* ---- the recorder ------------------------------------------------------------ *
 * PB7's half-cycles, classed short or long at the midpoint of 210 and
 * 418; anything longer is not the writer's, and ends a byte. A long half
 * after shorts is a start bit: fast, its second half; slow, the first
 * of eight. The bits after it are read as the ROM's reader reads them,
 * eight and parity, and the three 1s after are shorts, which a search
 * for the next start bit passes over. */

#define REC_LONG_MIN   ((CAS_SHORT + CAS_LONG) / 2u)
#define REC_HALF_MAX   (CAS_LONG + CAS_SHORT)

enum { RS_SEEK, RS_START, RS_BITS };
enum { RF_SYNC, RF_SYNCED, RF_HEADER, RF_NAME, RF_DATA };

/* A file is kept only whole: one cut short leaves the buffer as it was
 * before it, and is counted. */
static void rec_cut(cassette_rec_t *r) {
    if (r->fstate >= RF_HEADER) r->errors++;
    r->fstate = RF_SYNC;
}

static void rec_file_byte(cassette_rec_t *r, uint8_t b) {
    switch (r->fstate) {
    case RF_SYNC:
    case RF_SYNCED:
        if (b == TAP_SYNC) r->fstate = RF_SYNCED;
        else if (b == TAP_START && r->fstate == RF_SYNCED) { r->fstate = RF_HEADER; r->got = 0; }
        else r->fstate = RF_SYNC;
        return;
    case RF_HEADER:
        r->raw[r->got++] = b;
        if (r->got == TAP_HEADER_LEN) { r->fstate = RF_NAME; r->name_len = 0; }
        return;
    case RF_NAME:
        if (b) {
            if (r->name_len == ORIC_TAP_NAME_MAX) { rec_cut(r); return; }
            r->name[r->name_len++] = b;
            return;
        }
        r->want = tap_data_len(tap_start(r->raw), tap_end(r->raw));
        if (!r->buf || r->len + TAP_HEADER_MAX + r->want > r->cap) { rec_cut(r); return; }
        r->at = r->len;
        r->got = (uint32_t)tap_encode_header(r->raw, r->name, r->name_len, r->buf + r->at);
        r->want += r->got;
        r->fstate = RF_DATA;
        return;
    default:
        r->buf[r->at + r->got++] = b;
        if (r->got == r->want) {
            r->len = r->at + r->got;
            r->files++;
            r->fstate = RF_SYNC;
        }
        return;
    }
}

static void rec_byte_bit(cassette_rec_t *r, bool one) {
    r->shift |= (uint16_t)(one ? 1u << r->bit : 0u);
    if (++r->bit < 9u) return;
    uint8_t b = (uint8_t)r->shift;
    bool parity = (r->shift >> 8) & 1u;
    r->state = RS_SEEK;
    if (parity != ((__builtin_popcount(b) & 1u) == 0)) {
        r->errors++;
        rec_cut(r);
        return;
    }
    rec_file_byte(r, b);
}

static void rec_half(cassette_rec_t *r, uint64_t h) {
    bool lng = h >= REC_LONG_MIN;
    if (h >= REC_HALF_MAX) {
        if (r->state != RS_SEEK) { r->errors++; rec_cut(r); }
        r->state = RS_SEEK;
        return;
    }
    switch (r->state) {
    case RS_SEEK:
        if (lng) { r->state = RS_START; r->run = 1; }
        return;
    case RS_START:
        if (lng) {
            /* Slow: the start bit is eight long halves. */
            if (++r->run == 8u) {
                r->state = RS_BITS;
                r->slow = true;
                r->bit = 0;
                r->shift = 0;
                r->run = 0;
            }
            return;
        }
        if (r->run != 1u) { r->errors++; rec_cut(r); r->state = RS_SEEK; return; }
        /* Fast: this short is the first half of bit 0. */
        r->state = RS_BITS;
        r->slow = false;
        r->bit = 0;
        r->shift = 0;
        r->run = 1;
        r->run_long = false;
        return;
    default:
        if (!r->slow) {
            if (r->run == 0) {
                if (lng) { r->errors++; rec_cut(r); r->state = RS_SEEK; return; }
                r->run = 1;
                return;
            }
            r->run = 0;
            rec_byte_bit(r, !lng);
            return;
        }
        if (r->run == 0) r->run_long = lng;
        else if (lng != r->run_long) { r->errors++; rec_cut(r); r->state = RS_SEEK; return; }
        if (++r->run == (r->run_long ? 8u : 16u)) {
            r->run = 0;
            rec_byte_bit(r, !r->run_long);
        }
        return;
    }
}

/* PB7's level, and when it last changed: T1's own underflow when T1
 * drives it and reloaded since the last look, which the count since says
 * to the cycle; or else now, as for a write that gave T1 the pin. */
static void rec_sample(oric_t *m, uint64_t now) {
    cassette_rec_t *r = &m->cas.rec;
    const via6522_t *v = &m->via;
    bool level = (via6522_pb_out(v) & 0x80u) != 0;
    uint64_t seen = r->seen;
    r->seen = now;
    if (level == r->level) return;
    uint64_t at = now;
    if ((v->acr & (VIA_ACR_T1_PB7 | VIA_ACR_T1_FREERUN)) ==
        (VIA_ACR_T1_PB7 | VIA_ACR_T1_FREERUN)) {
        /* The VIA runs a little apart from the instruction boundary
         * (oric_via_catch_up), so the count can stand at the latch + 2,
         * a reload one cycle ahead: -1. */
        int64_t since = (int64_t)v->t1_latch + 1 - v->t1;
        if (since <= (int64_t)(now - seen)) at = (uint64_t)((int64_t)at - since);
    }
    /* The first edge ends a half that began before the relay closed. */
    if (r->last) rec_half(r, at - r->last);
    r->last = at ? at : 1u;
    r->level = level;
}

static void rec_start(oric_t *m) {
    cassette_rec_t *r = &m->cas.rec;
    r->on = true;
    r->level = (via6522_pb_out(&m->via) & 0x80u) != 0;
    r->last = 0;
    r->seen = m->cpu.cycles;
    r->state = RS_SEEK;
    r->fstate = RF_SYNC;
}

static void rec_stop(oric_t *m) {
    cassette_rec_t *r = &m->cas.rec;
    if (!r->on) return;
    rec_sample(m, m->cpu.cycles);
    rec_cut(r);
    r->state = RS_SEEK;
    r->on = false;
}

void oric_cassette_record(oric_t *m, uint8_t *buf, uint32_t cap) {
    cassette_rec_t *r = &m->cas.rec;
    rec_stop(m);
    r->armed = buf != NULL;
    if (buf && buf != r->buf) {
        r->buf = buf;
        r->cap = cap;
        r->len = r->mark = 0;
    }
    if (r->armed && m->cas.motor) {
        stop_player(m);
        rec_start(m);
    }
    update(m);
}

bool oric_cassette_unsaved(const oric_t *m, uint32_t *from, uint32_t *to) {
    const cassette_rec_t *r = &m->cas.rec;
    if (r->on || !r->buf || r->len <= r->mark) return false;
    *from = r->mark;
    *to = r->len;
    return true;
}

void oric_cassette_saved(oric_t *m) {
    cassette_rec_t *r = &m->cas.rec;
    r->len = r->mark = 0;
}

/* ---- the machine's ------------------------------------------------------------- */

void ORIC_HOT1(cassette_catch_up)(oric_t *m, uint64_t now) {
    play_to(m, now);
}

void ORIC_HOT1(cassette_run)(oric_t *m, uint64_t now) {
    play_to(m, now);
    if (m->cas.rec.on) rec_sample(m, now);
}

void cassette_motor(oric_t *m, bool on) {
    cassette_t *c = &m->cas;
    c->motor = on;
    if (c->rec.armed) {
        if (on && !c->rec.on) {
            stop_player(m);
            rec_start(m);
        } else if (!on) {
            rec_stop(m);
        }
    }
    update(m);
}

void cassette_stop_all(oric_t *m) {
    cassette_t *c = &m->cas;
    stop_player(m);
    rec_stop(m);
    c->motor = false;
    c->by_hand = false;
    update(m);
}
