/* mfmdisk.c — the MFM_DISK format (mfmdisk.h). */

#include "mfmdisk.h"

#include <string.h>

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put32(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8u * i));
}

const char *mfm_parse(const uint8_t *hdr, size_t hdr_len, uint32_t size, mfm_geom_t *g) {
    if (hdr_len < 20u || memcmp(hdr, "MFM_DISK", 8) != 0) {
        /* The older sector-only format is converted on a computer
         * (design.md §17). */
        if (hdr_len >= 8u && memcmp(hdr, "ORICDISK", 8) == 0) return "ORICDISK: CONVERT IT";
        return "NOT AN MFM_DISK IMAGE";
    }
    uint32_t sides = get32(hdr + 8), tracks = get32(hdr + 12), geometry = get32(hdr + 16);
    if (geometry != 1u) return "UNKNOWN GEOMETRY";
    if (sides < 1u || sides > 2u || tracks < 1u || tracks > ORIC_DISC_TRACKS_MAX)
        return "BAD GEOMETRY";
    g->sides = (uint8_t)sides;
    g->tracks = (uint8_t)tracks;
    if (size < mfm_track_offset(*g, sides - 1u, tracks)) return "IMAGE CUT SHORT";
    return NULL;
}

void mfm_header(uint8_t *hdr, mfm_geom_t g) {
    memset(hdr, 0, ORIC_DISC_HEADER_LEN);
    memcpy(hdr, "MFM_DISK", 8);
    put32(hdr + 8, g.sides);
    put32(hdr + 12, g.tracks);
    put32(hdr + 16, 1u);
}

uint32_t mfm_track_offset(mfm_geom_t g, unsigned side, unsigned track) {
    return ORIC_DISC_HEADER_LEN + ((uint32_t)side * g.tracks + track) * ORIC_DISC_TRACK_LEN;
}

uint16_t mfm_crc(uint16_t crc, uint8_t b) {
    crc ^= (uint16_t)(b << 8);
    for (unsigned i = 0; i < 8; i++)
        crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
    return crc;
}

uint16_t mfm_crc_block(uint16_t crc, const uint8_t *p, size_t n) {
    while (n--) crc = mfm_crc(crc, *p++);
    return crc;
}

static bool sync_at(const uint8_t *t, unsigned i) {
    return t[i] == 0xA1u && t[i + 1u] == 0xA1u && t[i + 2u] == 0xA1u;
}

unsigned mfm_index(const uint8_t *trk, mfm_sector_t *out, unsigned max) {
    const unsigned len = ORIC_DISC_TRACK_LEN;
    unsigned n = 0, i = 0;
    while (n < max && i + 10u <= len) {
        if (!(sync_at(trk, i) && trk[i + 3u] == 0xFEu)) {
            i++;
            continue;
        }
        mfm_sector_t *s = &out[n++];
        s->id = (uint16_t)(i + 3u);
        s->c = trk[i + 4u];
        s->h = trk[i + 5u];
        s->r = trk[i + 6u];
        s->n = trk[i + 7u];
        s->data = MFM_NO_DATA;
        /* After the ID's CRC, the data mark's sync within the window. */
        unsigned from = i + 10u;
        for (unsigned j = from; j <= from + MFM_DAM_WINDOW && j + 4u <= len; j++) {
            if (sync_at(trk, j) && (trk[j + 3u] == 0xFBu || trk[j + 3u] == 0xF8u)) {
                s->data = (uint16_t)(j + 3u);
                break;
            }
        }
        /* Step over the data field, so that its bytes are never taken
         * for a mark (mfmdisk.h). */
        i = s->data != MFM_NO_DATA ? s->data + 1u + mfm_size(s->n) + 2u : from;
    }
    return n;
}

/* The layout of the archive's Sedoric tracks, measured (M14): gap 1 of
 * 40, the index mark, gap 2 of 40 after it, and per sector 12 zeros, the
 * ID, 22 gap bytes, 12 zeros, the data and 40 gap bytes. */
void mfm_format_track(uint8_t *trk, uint8_t track, uint8_t side, unsigned count, uint8_t fill) {
    unsigned p = 0;
#define PUT(b, k) do { for (unsigned q_ = 0; q_ < (k) && p < ORIC_DISC_TRACK_LEN; q_++) trk[p++] = (uint8_t)(b); } while (0)
    memset(trk, 0x4E, ORIC_DISC_TRACK_LEN);
    PUT(0x4E, 40); PUT(0x00, 12); PUT(0xC2, 3); PUT(0xFC, 1); PUT(0x4E, 40);
    for (unsigned s = 0; s < count; s++) {
        PUT(0x00, 12);
        unsigned id = p;
        PUT(0xA1, 3); PUT(0xFE, 1);
        PUT(track, 1); PUT(side, 1); PUT(s + 1u, 1); PUT(1, 1);
        uint16_t c = mfm_crc_block(0xFFFFu, &trk[id], 8);
        PUT(c >> 8, 1); PUT(c & 0xFFu, 1);
        PUT(0x4E, 22); PUT(0x00, 12);
        unsigned dm = p;
        PUT(0xA1, 3); PUT(0xFB, 1); PUT(fill, 256);
        c = mfm_crc_block(0xFFFFu, &trk[dm], 4u + 256u);
        PUT(c >> 8, 1); PUT(c & 0xFFu, 1);
        PUT(0x4E, 40);
    }
#undef PUT
}
