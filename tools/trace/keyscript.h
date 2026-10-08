/* keyscript.h — the keys both tracers press, read from one file so the
 * two machines are typed at identically (design.md §13.4).
 *
 * One change per line, at a guest cycle counted from the first
 * instruction, so both machines see it at the same instruction boundary:
 *
 *   <cycle> down|up <row> <col>      a matrix cell: row PB0-PB2, column
 *                                    the bit of AY port A that enables it
 *
 * '#' starts a comment. tools/trace-diff.py writes these from text.
 */
#ifndef PICO_ORIC_TRACE_KEYSCRIPT_H
#define PICO_ORIC_TRACE_KEYSCRIPT_H

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned long long cycle;
    bool     down;
    unsigned row, col;
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
    char line[256];
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
        bool down = !strcmp(what, "down");
        if (got != 4 || (!down && strcmp(what, "up")) || row > 7 || col > 7 ||
            ks->n == KS_MAX_EVENTS || (ks->n && cycle < ks->ev[ks->n - 1].cycle)) {
            fprintf(stderr, "%s:%u: bad key line\n", path, lineno);
            fclose(f);
            return false;
        }
        ks->ev[ks->n++] = (ks_event_t){ cycle, down, row, col };
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
