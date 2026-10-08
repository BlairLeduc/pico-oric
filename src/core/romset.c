/* romset.c — the ROM images the Oric knows (design.md §10.2). */

#include "romset.h"

#include <stdbool.h>
#include <string.h>

/* MAME's ROM_LOAD hashes, as recorded in §10.2 and roms/README.md. */
const rom_info_t romset_images[ROM_IMAGE_COUNT] = {
    [ROM_BASIC10] = { "basic10.rom", 16384u,
        { 0x33, 0x31, 0x16, 0xe6, 0x88, 0x4d, 0x85, 0xaa, 0xa4, 0xdf,
          0xc7, 0x57, 0x8a, 0x91, 0xcc, 0xee, 0xea, 0x66, 0xd0, 0x16 } },
    [ROM_BASIC11] = { "basic11b.rom", 16384u,
        { 0x94, 0x51, 0xa1, 0xa0, 0x9d, 0x8f, 0x75, 0x94, 0x4d, 0xbd,
          0x6f, 0x91, 0x19, 0x3f, 0xc3, 0x60, 0xf1, 0xde, 0x80, 0xac } },
    [ROM_MICRODISC] = { "microdis.rom", 8192u,
        { 0x0d, 0x2e, 0xf6, 0xe6, 0x73, 0x22, 0xf4, 0x8f, 0x4b, 0x7e,
          0x08, 0xd8, 0xbb, 0xe6, 0x88, 0x27, 0xe2, 0x07, 0x45, 0x61 } },
};

rom_id_t romset_identify(const uint8_t *data, size_t len) {
    uint8_t d[SHA1_DIGEST_LEN];
    bool hashed = false;
    for (int i = 0; i < ROM_IMAGE_COUNT; i++) {
        if (len != romset_images[i].size) continue;
        if (!hashed) { sha1(data, len, d); hashed = true; }
        if (memcmp(d, romset_images[i].sha1, SHA1_DIGEST_LEN) == 0) return (rom_id_t)i;
    }
    return ROM_UNKNOWN;
}
