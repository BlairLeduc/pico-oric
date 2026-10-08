/* romset.h — the ROM images the Oric knows (design.md §10.2).
 *
 * Two BASIC ROMs for the socket at #C000 and the Microdisc's EPROM,
 * each identified by SHA-1 whatever the file is called: a near-miss dump
 * boots and then misbehaves (EL §8.1). The table is data about images;
 * nothing in the tree contains one (§18 item 1).
 */
#ifndef PICO_ORIC_ROMSET_H
#define PICO_ORIC_ROMSET_H

#include <stddef.h>
#include <stdint.h>

#include "sha1.h"

typedef enum {
    ROM_BASIC10,      /* BASIC 1.0, the Oric-1                 */
    ROM_BASIC11,      /* BASIC 1.1, the Atmos                  */
    ROM_MICRODISC,    /* the Microdisc's 8 KiB EPROM (M14)     */
    ROM_IMAGE_COUNT,
    ROM_UNKNOWN = -1,
} rom_id_t;

typedef struct {
    const char *file;    /* its name under /oric/roms/ (§10.1) */
    uint32_t    size;
    uint8_t     sha1[SHA1_DIGEST_LEN];
} rom_info_t;

extern const rom_info_t romset_images[ROM_IMAGE_COUNT];

/* Which known image is this? ROM_UNKNOWN for anything else, including
 * the right size with the wrong bytes. */
rom_id_t romset_identify(const uint8_t *data, size_t len);

#endif /* PICO_ORIC_ROMSET_H */
