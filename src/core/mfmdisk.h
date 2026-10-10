/* mfmdisk.h — Oricutron's MFM_DISK image format, the archive's (design.md
 * §10.5, §16).
 *
 *   header  256 bytes: "MFM_DISK", then sides, tracks per side and
 *           geometry, each four bytes little-endian; the rest zero
 *   tracks  sides x tracks raw MFM tracks of ORIC_DISC_TRACK_LEN bytes,
 *           every track of side 0, then every track of side 1
 *
 * A track holds what the WD1793 sees after decoding: gaps, the sync
 * marks as A1 bytes, ID fields (A1 A1 A1 FE, track, side, sector, size,
 * CRC) and data fields (A1 A1 A1 FB, or F8 deleted, the data and its
 * CRC). The marks are found as Oricutron's loader finds them, walking the
 * track and stepping over each data field, so data bytes that happen to
 * look like a mark are never taken for one. Geometry 1 is the only one
 * the archive uses and the only one Oricutron loads (M14's survey of
 * TOSEC's 221 images, out/m14).
 */
#ifndef PICO_ORIC_MFMDISK_H
#define PICO_ORIC_MFMDISK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"

typedef struct {
    uint8_t sides;     /* 1 or 2                                  */
    uint8_t tracks;    /* per side, up to ORIC_DISC_TRACKS_MAX    */
} mfm_geom_t;

/* The header of a file `size` bytes long. Returns NULL and fills g, or
 * says why the image is refused. A file shorter than its header says is
 * refused: TOSEC has ten, and Oricutron reads them as blank tracks where
 * the bytes are missing. */
const char *mfm_parse(const uint8_t *hdr, size_t hdr_len, uint32_t size, mfm_geom_t *g);

/* A header for g, ORIC_DISC_HEADER_LEN bytes. */
void mfm_header(uint8_t *hdr, mfm_geom_t g);

/* Where a track starts in the file. */
uint32_t mfm_track_offset(mfm_geom_t g, unsigned side, unsigned track);

/* The WD1793's CRC: CCITT, x^16 + x^12 + x^5 + 1, from #FFFF over the
 * marks' three A1s, the mark and the field. */
uint16_t mfm_crc(uint16_t crc, uint8_t b);
uint16_t mfm_crc_block(uint16_t crc, const uint8_t *p, size_t n);

/* One sector a track holds: where its ID mark (the FE) and its data
 * mark (FB or F8) are, as byte positions in the track, and the ID. */
typedef struct {
    uint16_t id;       /* the FE                                     */
    uint16_t data;     /* the FB or F8; MFM_NO_DATA if none follows   */
    uint8_t  c, h, r, n;
} mfm_sector_t;

#define MFM_NO_DATA 0xFFFFu

/* A data mark further than this past its ID's CRC belongs to no ID: the
 * WD1793 gives up after 43 bytes in MFM. The archive's are all 34. */
#define MFM_DAM_WINDOW 43u

/* Bytes in a sector of size code n (128, 256, 512 or 1024). */
static inline uint16_t mfm_size(uint8_t n) { return (uint16_t)(128u << (n & 3u)); }

/* The sectors of one raw track, in the order they pass the head, up to
 * max. Returns how many. */
unsigned mfm_index(const uint8_t *trk, mfm_sector_t *out, unsigned max);

/* A formatted track as Sedoric's INIT lays it out, for tests and blank
 * images: `count` sectors of 256 bytes numbered from 1, each filled with
 * `fill`, with correct CRCs. */
void mfm_format_track(uint8_t *trk, uint8_t track, uint8_t side, unsigned count, uint8_t fill);

#endif /* PICO_ORIC_MFMDISK_H */
