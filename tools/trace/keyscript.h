/* keyscript.h — the keys both tracers press, read from one file so the
 * two machines are typed at identically (design.md §13.4).
 *
 * One change per line, at a guest cycle counted from the first
 * instruction, so both machines see it at the same instruction boundary:
 *
 *   <cycle> down|up <row> <col>      a matrix cell: row PB0-PB2, column
 *                                    the bit of AY port A that enables it
 *   <cycle> poke <addr> <hex>        bytes into memory from addr, both in
 *                                    hex: a test program put in place
 *                                    between two instructions (M16)
 *
 * '#' starts a comment. tools/trace-diff.py writes these from text.
 */
#ifndef PICO_ORIC_TRACE_KEYSCRIPT_H
#define PICO_ORIC_TRACE_KEYSCRIPT_H

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KS_MAX_POKE 256

typedef struct {
    unsigned long long cycle;
    bool     down;
    unsigned row, col;
    /* A poke instead of a key when n is not zero. */
    unsigned addr, n;
    unsigned char bytes[KS_MAX_POKE];
} ks_event_t;

#define KS_MAX_EVENTS 4096

typedef struct {
    ks_event_t ev[KS_MAX_EVENTS];
    unsigned   n, next;
} keyscript_t;

/* False, with a message on stderr, if the file cannot be read. The
 * events must be in order of cycle. */
static inline bool ks_load(keyscript_t *ks, const char *path) {
    ks->n = ks->next = 0;
    if (!path) return true;
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "%s: cannot open\n", path); return false; }
    char line[2 * KS_MAX_POKE + 64];
    unsigned lineno = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';
        unsigned long long cycle;
        char what[8];
        unsigned row, col;
        int got = sscanf(line, "%llu %7s %u %u", &cycle, what, &row, &col);
        if (got <= 0) continue;
        if (got >= 2 && !strcmp(what, "poke")) {
            unsigned addr, n = 0;
            char hex[2 * KS_MAX_POKE + 1];
            bool ok = sscanf(line, "%*u %*s %x %512s", &addr, hex) == 2 && addr <= 0xFFFFu &&
                      ks->n < KS_MAX_EVENTS && !(ks->n && cycle < ks->ev[ks->n - 1].cycle);
            ks_event_t *e = &ks->ev[ks->n];
            for (const char *h = hex; ok && h[0]; h += 2, n++) {
                unsigned b = 0;
                ok = h[1] && n < KS_MAX_POKE && sscanf(h, "%2x", &b) == 1;
                e->bytes[n] = (unsigned char)b;
            }
            if (!ok || !n) {
                fprintf(stderr, "%s:%u: bad poke line\n", path, lineno);
                fclose(f);
                return false;
            }
            e->cycle = cycle;
            e->addr = addr;
            e->n = n;
            ks->n++;
            continue;
        }
        bool down = !strcmp(what, "down");
        if (got != 4 || (!down && strcmp(what, "up")) || row > 7 || col > 7 ||
            ks->n == KS_MAX_EVENTS || (ks->n && cycle < ks->ev[ks->n - 1].cycle)) {
            fprintf(stderr, "%s:%u: bad key line\n", path, lineno);
            fclose(f);
            return false;
        }
        ks->ev[ks->n++] = (ks_event_t){ .cycle = cycle, .down = down, .row = row, .col = col };
    }
    fclose(f);
    return true;
}

/* The next event due at or before `cycle`, or NULL. */
static inline const ks_event_t *ks_due(keyscript_t *ks, unsigned long long cycle) {
    if (ks->next < ks->n && ks->ev[ks->next].cycle <= cycle) return &ks->ev[ks->next++];
    return NULL;
}

#endif /* PICO_ORIC_TRACE_KEYSCRIPT_H */
