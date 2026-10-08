/* test_romset.c — SHA-1 and the ROM table (design.md §10.2).
 *
 * The digest and the table run everywhere. Identifying the real images
 * needs them in roms/, and those checks are left out without them; the
 * rest still runs, so this test passes in CI on what it can check.
 */

#include <string.h>

#include "guest.h"
#include "romset.h"
#include "sha1.h"
#include "test_util.h"

static bool hex_digest_is(const void *msg, size_t len, const char *hex) {
    uint8_t d[SHA1_DIGEST_LEN];
    sha1(msg, len, d);
    char got[2 * SHA1_DIGEST_LEN + 1];
    for (unsigned i = 0; i < SHA1_DIGEST_LEN; i++) {
        static const char digits[] = "0123456789abcdef";
        got[2 * i] = digits[d[i] >> 4];
        got[2 * i + 1] = digits[d[i] & 15u];
    }
    got[2 * SHA1_DIGEST_LEN] = 0;
    return strcmp(got, hex) == 0;
}

static void to_hex(const uint8_t *d, char *out) {
    for (unsigned i = 0; i < SHA1_DIGEST_LEN; i++) sprintf(out + 2 * i, "%02x", d[i]);
}

static uint8_t buf[ORIC_ROM_SIZE];

int main(void) {
    /* ---- SHA-1: pico-atom's cases, which came with sha1.c ------------- */
    CHECK(hex_digest_is("", 0, "da39a3ee5e6b4b0d3255bfef95601890afd80709"), "empty");
    CHECK(hex_digest_is("abc", 3, "a9993e364706816aba3e25717850c26c9cd0d89d"), "abc");
    {
        const char *two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
        CHECK(hex_digest_is(two, strlen(two), "84983e441c3bd26ebaae4aa1f95129e5e54670f1"),
              "the two-block message");
        for (unsigned i = 0; i < sizeof buf; i++) buf[i] = (uint8_t)(i * 31u + (i >> 8));
        uint8_t one[SHA1_DIGEST_LEN], parts[SHA1_DIGEST_LEN];
        sha1(buf, sizeof buf, one);
        sha1_t s;
        sha1_init(&s);
        for (unsigned off = 0, step = 1; off < sizeof buf; off += step, step = step * 3u % 97u + 1u)
            sha1_update(&s, buf + off, off + step > sizeof buf ? sizeof buf - off : step);
        sha1_final(&s, parts);
        CHECK(memcmp(one, parts, SHA1_DIGEST_LEN) == 0, "streamed digest differs");
    }

    /* ---- the table says what README.md says --------------------------- */
    {
        char path[1024];
        snprintf(path, sizeof path, "%s/README.md", PICO_ORIC_SOURCE_DIR);
        FILE *f = fopen(path, "r");
        CHECK(f != NULL, "cannot open %s", path);
        static char readme[65536];
        size_t n = f ? fread(readme, 1, sizeof readme - 1, f) : 0;
        if (f) fclose(f);
        readme[n] = 0;
        for (int i = 0; i < ROM_IMAGE_COUNT; i++) {
            char line[128], hex[2 * SHA1_DIGEST_LEN + 1];
            to_hex(romset_images[i].sha1, hex);
            snprintf(line, sizeof line, "%s  %s", hex, romset_images[i].file);
            CHECK(strstr(readme, line) != NULL, "README.md has no line \"%s\"", line);
        }
    }

    /* ---- identify: wrong bytes and wrong sizes are unknown ------------ */
    CHECK(romset_identify(buf, sizeof buf) == ROM_UNKNOWN, "a made-up 16 KiB is unknown");
    CHECK(romset_identify(buf, 8192) == ROM_UNKNOWN, "a made-up 8 KiB is unknown");

    /* ---- identify by digest, as the card's files are hashed ----------- */
    for (int i = 0; i < ROM_IMAGE_COUNT; i++) {
        uint8_t d[SHA1_DIGEST_LEN];
        memcpy(d, romset_images[i].sha1, SHA1_DIGEST_LEN);
        CHECK(romset_identify_digest(d, romset_images[i].size) == (rom_id_t)i,
              "%s's own digest not identified", romset_images[i].file);
        CHECK(romset_identify_digest(d, romset_images[i].size + 1u) == ROM_UNKNOWN,
              "%s's digest at the wrong size passed", romset_images[i].file);
        d[SHA1_DIGEST_LEN - 1] ^= 0x01u;
        CHECK(romset_identify_digest(d, romset_images[i].size) == ROM_UNKNOWN,
              "%s's digest with a bit changed passed", romset_images[i].file);
    }

    const char *dir;
    guest_find_roms(&dir);
    int found = 0;
    for (int i = ROM_BASIC10; i <= ROM_BASIC11; i++) {
        const uint8_t *img = guest_rom_image((rom_id_t)i);
        if (!img) continue;
        found++;
        memcpy(buf, img, ORIC_ROM_SIZE);
        CHECK(romset_identify(buf, ORIC_ROM_SIZE) == (rom_id_t)i, "%s not identified",
              romset_images[i].file);
        /* A near-miss: one byte different. */
        buf[0x1234] ^= 0x01u;
        CHECK(romset_identify(buf, ORIC_ROM_SIZE) == ROM_UNKNOWN, "%s with a byte changed passed",
              romset_images[i].file);
        CHECK(romset_identify(img, ORIC_ROM_SIZE - 1) == ROM_UNKNOWN, "%s one byte short passed",
              romset_images[i].file);
        /* Hashed a sector at a time, as the firmware reads the card. */
        sha1_t s;
        uint8_t d[SHA1_DIGEST_LEN];
        sha1_init(&s);
        for (unsigned off = 0; off < ORIC_ROM_SIZE; off += ORIC_CARD_CHUNK)
            sha1_update(&s, img + off, ORIC_CARD_CHUNK);
        sha1_final(&s, d);
        CHECK(romset_identify_digest(d, ORIC_ROM_SIZE) == (rom_id_t)i,
              "%s hashed in pieces not identified", romset_images[i].file);
    }
    if (found < 2) printf("note: %d of 2 BASIC ROMs in %s; identifying them not checked\n", found, dir);

    TEST_DONE();
}
