/* sha1.h — SHA-1, for identifying ROM images (design.md §10.2) and,
 * from M11, the ROM a save state ran on (§10.6; EL §8.5).
 *
 * Not for security: a ROM is recognised by its hash whatever it is
 * called, so a near-miss dump is reported rather than booted unnoticed.
 */
#ifndef PICO_ORIC_SHA1_H
#define PICO_ORIC_SHA1_H

#include <stddef.h>
#include <stdint.h>

#define SHA1_DIGEST_LEN 20u

typedef struct {
    uint32_t h[5];
    uint64_t length;      /* bytes hashed so far */
    uint8_t  block[64];
    uint32_t used;        /* bytes waiting in block */
} sha1_t;

void sha1_init(sha1_t *s);
void sha1_update(sha1_t *s, const void *data, size_t len);
void sha1_final(sha1_t *s, uint8_t digest[SHA1_DIGEST_LEN]);

/* One-shot convenience. */
void sha1(const void *data, size_t len, uint8_t digest[SHA1_DIGEST_LEN]);

#endif /* PICO_ORIC_SHA1_H */
