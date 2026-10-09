/* tap.c — the .tap format (tap.h, design.md §10.3). */

#include "tap.h"

#include <string.h>

tap_scan_t tap_scan(const uint8_t *p, size_t n, bool eof, tap_header_t *h, size_t *skip) {
    size_t i = 0;
    for (;;) {
        /* The first #16 from here; nothing before it is a header. */
        while (i < n && p[i] != TAP_SYNC) i++;
        if (i == n) {
            *skip = n;
            return TAP_NONE;
        }
        size_t from = i;
        while (i < n && p[i] == TAP_SYNC) i++;
        size_t at = i + 1u;
        size_t k = 0;
        bool whole = i < n;
        if (whole && p[i] != TAP_START) continue;   /* not a header: look on */
        whole = whole && at + TAP_HEADER_LEN <= n;
        if (whole) {
            at += TAP_HEADER_LEN;
            while (at + k < n && p[at + k] != 0 && k < ORIC_TAP_NAME_MAX) k++;
            whole = at + k < n;
        }
        if (!whole) {
            /* The window ends inside what may be a header. */
            *skip = eof ? n : from;
            return eof ? TAP_NONE : TAP_MORE;
        }
        if (p[at + k] != 0) continue;   /* too long a name to be one: look on */

        memcpy(h->raw, p + at - TAP_HEADER_LEN, TAP_HEADER_LEN);
        memcpy(h->name, p + at, k);
        h->name_len = (uint8_t)k;
        h->data_at = (uint32_t)(at + k + 1u);
        *skip = from;
        return TAP_FOUND;
    }
}

size_t tap_encode_header(const uint8_t raw[TAP_HEADER_LEN], const uint8_t *name,
                         size_t name_len, uint8_t *out) {
    if (name_len > ORIC_TAP_NAME_MAX) name_len = ORIC_TAP_NAME_MAX;
    size_t n = 0;
    for (unsigned i = 0; i < TAP_LEADER; i++) out[n++] = TAP_SYNC;
    out[n++] = TAP_START;
    memcpy(out + n, raw, TAP_HEADER_LEN);
    n += TAP_HEADER_LEN;
    memcpy(out + n, name, name_len);
    n += name_len;
    out[n++] = 0;
    return n;
}
