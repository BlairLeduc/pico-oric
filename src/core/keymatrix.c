/* keymatrix.c — the held-key set and the matrix it drives (design.md §9.1). */

#include "keymatrix.h"

#include <string.h>

void keymatrix_init(keymatrix_t *k) {
    memset(k, 0, sizeof(*k));
}

void keymatrix_set_layout(keymatrix_t *k, const keylayout_t *l) {
    k->layout = l;
}

static void enqueue(keymatrix_t *k, uint8_t state, uint8_t code, uint8_t canon) {
    unsigned tail = (k->q_head + k->q_len) % ORIC_KEY_EVENT_QUEUE;
    k->queue[tail] = (keymatrix_event_t){ state, code, canon };
    k->q_len++;
}

/* Held-state identity is the physical key, not the code (EL §7.1). */
static int find_open(const keymatrix_t *k, uint8_t canon) {
    for (int i = 0; i < k->n_open; i++) {
        if (k->open[i] == canon) return i;
    }
    return -1;
}

void keymatrix_event(keymatrix_t *k, uint8_t state, uint8_t code) {
    if (code == PICOCALC_KEY_ALT) k->ev_alt = state != KEY_EV_RELEASED;
    uint8_t canon = (code == PICOCALC_KEY_INSERT && k->ev_alt)
                        ? (uint8_t)'i' : keymap_picocalc_canonical(code);
    int o = find_open(k, canon);

    if (state == KEY_EV_RELEASED) {
        /* A release for a press that was refused, or that came before
         * keymatrix_init, has nothing to undo. */
        if (o < 0) return;
        k->open[o] = k->open[--k->n_open];
        enqueue(k, state, code, canon);   /* its slot was kept (below) */
        return;
    }
    if (state != KEY_EV_PRESSED && state != KEY_EV_HELD) return;
    if (o >= 0) return;            /* already down: auto-repeat, or held */

    /* Room for this press, its release, and every release still owed:
     * q_len + n_open never exceeds the queue. */
    if (k->q_len + k->n_open + 2u > ORIC_KEY_EVENT_QUEUE) {
        k->dropped++;
        return;
    }
    k->open[k->n_open++] = canon;
    enqueue(k, state, code, canon);
}

static int find_held(const keymatrix_t *k, uint8_t canon) {
    for (int i = 0; i < k->n; i++) {
        if (k->held[i].canon == canon) return i;
    }
    return -1;
}

/* The entry for a code, chosen once at press time. With Alt down it is
 * the Alt layer and nothing else; an Alt chord with no binding maps to
 * nothing, so the layer never leaks a shifted letter (§9.2). */
static const keymap_t *find(uint8_t code, uint8_t layer) {
    for (size_t i = 0; i < keymap_picocalc_len; i++) {
        const keymap_t *e = &keymap_picocalc[i];
        if (e->code == code && (e->flags & KM_ALT) == layer) return e;
    }
    return NULL;
}

/* With Alt down the Alt layer only, so no layout can take the menu,
 * pause or reset away. Otherwise the layout first, by physical key, then
 * the standard map (§9.4). */
static const keymap_t *lookup(const keymatrix_t *k, uint8_t code) {
    if (!k->alt && k->layout) {
        uint8_t canon = keymap_picocalc_canonical(code);
        for (unsigned i = 0; i < k->layout->n; i++) {
            if (k->layout->bind[i].code == canon) return &k->layout->bind[i];
        }
    }
    if (!k->alt) return find(code, 0);
    const keymap_t *e = find(code, KM_ALT);
    /* A function key pressed with Alt still held arrives as itself
     * (hardware-notes.md §6.2), so it is matched whatever the Alt state. */
    if (!e && code >= PICOCALC_KEY_F1 && code <= PICOCALC_KEY_F10) e = find(code, 0);
    return e;
}

static bool is_modifier(uint8_t code) {
    return code == PICOCALC_KEY_ALT || code == PICOCALC_KEY_CTRL ||
           code == PICOCALC_KEY_SHIFT_L || code == PICOCALC_KEY_SHIFT_R;
}

/* The modifiers that reach the matrix, as indices into mod_fields, and
 * the rows of column 4 they are (§2.4). */
static int mod_index(uint8_t code) {
    switch (code) {
    case PICOCALC_KEY_SHIFT_L: return 0;
    case PICOCALC_KEY_SHIFT_R: return 1;
    case PICOCALC_KEY_CTRL:    return 2;
    default:                   return -1;
    }
}

static const uint8_t mod_row[3] = { OK_ROW_SHIFT_L, OK_ROW_SHIFT_R, OK_ROW_CTRL };

static bool mod_down(const keymatrix_t *k, int i) {
    return i < 2 ? (k->shift >> i) & 1u : k->ctrl;
}

/* A key whose binding adds a SHIFT the matrix does not already show
 * waits a field behind it (keymatrix_field). */
static uint8_t lead_of(const keymatrix_t *k, const keymap_t *e) {
    return (e->flags & KM_SHIFT) && !k->shift ? 1u : 0u;
}

/* Apply the event at the head of the queue, or say why it must wait. */
static bool apply_head(keymatrix_t *k) {
    keymatrix_event_t ev = k->queue[k->q_head];
    bool down = (ev.state == KEY_EV_PRESSED || ev.state == KEY_EV_HELD);

    if (is_modifier(ev.code)) {
        /* Modifiers report held events while down (hardware-notes.md
         * §6.2). Each Shift is the Oric's SHIFT on its side, which games
         * read on its own, and Ctrl is CTRL (§9.2); all three are held
         * as long as a key, and a key pressed with one waits a field
         * behind it (EL §7.1). */
        int i = mod_index(ev.code);
        if (i >= 0) {
            bool was = mod_down(k, i);
            if (down && !was) {
                k->mod_fields[i] = 0;
                if (k->gap < 1) k->gap = 1;
            }
            if (!down && was && k->mod_fields[i] < ORIC_KEY_MIN_FIELDS) return false;
        }
        if (ev.code == PICOCALC_KEY_ALT)  k->alt  = down;
        if (ev.code == PICOCALC_KEY_CTRL) k->ctrl = down;
        if (ev.code == PICOCALC_KEY_SHIFT_L || ev.code == PICOCALC_KEY_SHIFT_R) {
            uint8_t bit = ev.code == PICOCALC_KEY_SHIFT_L ? 1u : 2u;
            k->shift = down ? (uint8_t)(k->shift | bit) : (uint8_t)(k->shift & ~bit);
        }
        return true;
    }

    uint8_t canon = ev.canon;
    int h = find_held(k, canon);

    if (ev.state == KEY_EV_RELEASED) {
        if (h < 0) return true;                  /* never mapped */
        if (k->held[h].fields < ORIC_KEY_MIN_FIELDS + k->held[h].lead) return false;
        k->held[h] = k->held[--k->n];
        k->gap = ORIC_KEY_GAP_FIELDS;
        return true;
    }
    if (!down) return true;                      /* unknown state */

    /* Auto-repeat arrives as more presses (hardware-notes.md §6.2). The
     * key is already down; the ROM repeats it itself. */
    if (h >= 0) return true;
    if (k->gap > 0) return false;

    const keymap_t *e = lookup(k, ev.code);
    if (!e) return true;
    if (e->flags & KM_MENU) { k->menu_request = true; k->menu_page = e->row; }
    if (e->flags & KM_PAUSE) k->pause_request = true;
    if (e->flags & KM_SHOT) k->shot_request = true;
    if (e->flags & KM_RESET) k->reset_request = true;
    if (k->n >= ORIC_KEY_HELD_MAX) return true;  /* more keys than fingers */

    k->held[k->n++] = (keymatrix_held_t){ .canon = canon, .map = *e, .lead = lead_of(k, e) };
    return true;
}

void keymatrix_field(keymatrix_t *k, oric_t *m) {
    while (k->q_len > 0 && apply_head(k)) {
        k->q_head = (uint8_t)((k->q_head + 1u) % ORIC_KEY_EVENT_QUEUE);
        k->q_len--;
    }

    bool shift = false;
    memset(m->keys, 0, sizeof(m->keys));
    for (uint8_t i = 0; i < k->n; i++) {
        keymatrix_held_t *h = &k->held[i];
        const keymap_t *e = &h->map;
        bool shown = h->fields >= h->lead;
        if (h->fields < UINT8_MAX) h->fields++;

        if (e->flags & KM_SHIFT) shift = true;
        if ((e->flags & KM_NOCELL) || !shown) continue;
        m->keys[e->row] |= (uint8_t)(1u << e->col);
    }
    for (int i = 0; i < 3; i++) {
        if (!mod_down(k, i)) continue;
        if (k->mod_fields[i] < UINT8_MAX) k->mod_fields[i]++;
        m->keys[mod_row[i]] |= 1u << OK_COL_MODS;
    }
    if (shift) m->keys[OK_ROW_SHIFT_L] |= 1u << OK_COL_MODS;
    if (k->gap > 0) k->gap--;
}
