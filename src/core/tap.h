/* tap.h — the Oric's .tap tape image (design.md §10.3, §16).
 *
 * A .tap is the bytes both ROMs write to tape, without the signal: a
 * leader of #16s, #24, nine header bytes, the name and a zero, then the
 * data, for each file in turn. The header, in the order it comes off
 * tape, is two unused bytes, the type (#00 BASIC, #80 machine code,
 * bit 6 an array), the autorun flag, the end address and the start
 * address, each high byte first, and one more unused byte. 1.1 stores
 * it from #02B0 down to #02A8 (#E4BB), 1.0 from #66 down to #5E
 * (#E4BE). The data runs from the start address to the end address
 * inclusive, at least one byte (§16).
 *
 * Nothing here touches the machine or the card: the port reads a window
 * of the file and scans it here, and the save is written from what
 * tap_encode_header makes.
 */
#ifndef PICO_ORIC_TAP_H
#define PICO_ORIC_TAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"

#define TAP_SYNC        0x16u
#define TAP_START       0x24u
#define TAP_HEADER_LEN     9u

/* What the recorder writes before #24: four #16s, as Oricutron writes
 * and at least the three its loader asks for (tape.c, read 2026-10-08). */
#define TAP_LEADER         4u

/* The header's fields, by their place in the nine bytes. */
#define TAP_H_TYPE         2u
#define TAP_H_AUTORUN      3u
#define TAP_H_END_HI       4u
#define TAP_H_END_LO       5u
#define TAP_H_START_HI     6u
#define TAP_H_START_LO     7u

/* The longest .tap header: the leader, #24, the header, the name and its
 * zero. */
#define TAP_HEADER_MAX (TAP_LEADER + 1u + TAP_HEADER_LEN + ORIC_TAP_NAME_MAX + 1u)

typedef struct {
    uint8_t  raw[TAP_HEADER_LEN];   /* in tape order                     */
    uint8_t  name[ORIC_TAP_NAME_MAX];
    uint8_t  name_len;              /* the name's bytes, its zero not counted */
    uint32_t data_at;               /* the first data byte, from the window's start */
} tap_header_t;

static inline uint16_t tap_start(const uint8_t raw[TAP_HEADER_LEN]) {
    return (uint16_t)((raw[TAP_H_START_HI] << 8) | raw[TAP_H_START_LO]);
}

static inline uint16_t tap_end(const uint8_t raw[TAP_HEADER_LEN]) {
    return (uint16_t)((raw[TAP_H_END_HI] << 8) | raw[TAP_H_END_LO]);
}

/* The last address both ROMs' data loops reach from `start` towards
 * `end`: they move a byte, then stop once the pointer was at or past the
 * end (#E56C in 1.1, #E554 in 1.0), so an end below the start moves one
 * byte. */
static inline uint16_t tap_last(uint16_t start, uint16_t end) {
    return end >= start ? end : start;
}

static inline uint32_t tap_data_len(uint16_t start, uint16_t end) {
    return (uint32_t)tap_last(start, end) - start + 1u;
}

typedef enum {
    TAP_FOUND,     /* *h holds a header; its data starts at h->data_at    */
    TAP_MORE,      /* a header may start at *skip: read on from there     */
    TAP_NONE,      /* no header in the rest of the file                   */
} tap_scan_t;

/* Look for the next file's header in a window of n bytes read from the
 * file, `eof` if the window reaches the file's end. A header is at least
 * one #16, #24, nine bytes, and a name ending in zero; anything before
 * it is skipped, as the ROM skips it. A name longer than
 * ORIC_TAP_NAME_MAX is taken as no header. On TAP_MORE, *skip is how
 * many bytes may be dropped from the front of the window; on TAP_NONE,
 * all of them. */
tap_scan_t tap_scan(const uint8_t *p, size_t n, bool eof, tap_header_t *h, size_t *skip);

/* The leader, #24, header, name and zero the recorder writes before a
 * file's data, into out (TAP_HEADER_MAX bytes). Returns its length. */
size_t tap_encode_header(const uint8_t raw[TAP_HEADER_LEN], const uint8_t *name,
                         size_t name_len, uint8_t *out);

#endif /* PICO_ORIC_TAP_H */
