/* vsync.c — the vertical-sync modification (vsync.h, design.md §15.2 M16). */

#include "vsync.h"

#include "hot.h"
#include "oric.h"
#include "via6522.h"

uint32_t vsync_fall_at(const oric_t *m, bool hz50) {
    const oric_config_t *c = &m->cfg;
    return (uint32_t)c->line_cycles * (hz50 ? c->vsync_line_50hz : c->vsync_line_60hz) +
           c->vsync_delay;
}

static uint64_t next_fall(const oric_t *m) {
    return m->vs.frame_at + vsync_fall_at(m, (m->ula_mode & ULA_MODE_50HZ) != 0);
}

void vsync_restart(oric_t *m, uint64_t frame_at) {
    vsync_t *s = &m->vs;
    s->low = false;
    s->frame_at = frame_at;
    s->due = m->cfg.vsync_hack ? next_fall(m) : UINT64_MAX;
}

void vsync_field(oric_t *m) {
    vsync_t *s = &m->vs;
    if (s->due != UINT64_MAX && !s->low) s->due = next_fall(m);
}

void ORIC_HOT1(vsync_run)(oric_t *m, uint64_t now) {
    vsync_t *s = &m->vs;
    while (now >= s->due) {
        if (!s->low) {
            via6522_set_cb1(&m->via, false);
            s->low = true;
            s->pulses++;
            s->due += m->cfg.vsync_low;
        } else {
            via6522_set_cb1(&m->via, true);
            s->low = false;
            /* The field in progress is as long as the mode it began in
             * makes it, which only the boundary changes (oric.c). */
            s->frame_at += oric_field_cycles(m);
            s->due = next_fall(m);
        }
    }
}
