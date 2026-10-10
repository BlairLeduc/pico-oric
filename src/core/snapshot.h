/* snapshot.h — our own save states, .sav (design.md §10.6; EL §8.5).
 *
 * pico-ace's format with the 6502's, the VIA's, the AY's and the ULA's
 * fields. Explicit, field by field and little-endian, never a struct
 * dumped from memory: oric_t holds pointers and padding, and a state has
 * to outlive the build that wrote it.
 *
 *   header  20 bytes: "PORCSNAP" (8), version (2), header length (2),
 *           payload length (4), CRC-32 of the payload (4)
 *   payload the state (SNAP_STATE_LEN), then all 64 KiB of RAM as oric_t
 *           holds it, the overlay under the ROM included (§2.1)
 *
 * The ROM is not in a state: what is, is its SHA-1, and a state loads
 * only into a machine whose ROM hashes the same and whose RAM and field
 * are the same, since every count in it is in that machine's cycles.
 * Unused state bytes are written zero and read as reserved, so a later
 * version can add fields whose zero is their reset value: from M14, the
 * Microdisc's.
 *
 * States are saved between fields, where the port parks the guest, so
 * the field always resumes where the snapshot point is (§11.1); the
 * budget carries the last instruction's overshoot across, as it would
 * have. Keys are not state: a load releases them all (§10.6).
 *
 * I/O is through a callback, so the core never sees a file. Loading is
 * two passes: snapshot_check reads the whole stream and verifies the
 * header, the CRC and the machine without touching it; only then does
 * snapshot_load, over the same bytes again, change anything. A torn or
 * foreign file therefore leaves the running machine exactly as it was.
 * The calls share one buffer, so one runs at a time.
 */
#ifndef PICO_ORIC_SNAPSHOT_H
#define PICO_ORIC_SNAPSHOT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "oric.h"

#define SNAP_VERSION     1u
#define SNAP_HEADER_LEN  20u
#define SNAP_STATE_LEN   320u
#define SNAP_PAYLOAD_LEN (SNAP_STATE_LEN + ORIC_ADDR_SPACE)
#define SNAP_FILE_LEN    (SNAP_HEADER_LEN + SNAP_PAYLOAD_LEN)

typedef enum {
    SNAP_OK = 0,
    SNAP_IO,              /* the callback failed, or the file is short   */
    SNAP_NOT_SNAPSHOT,    /* wrong magic or lengths                      */
    SNAP_NEWER,           /* a version this build does not know          */
    SNAP_CORRUPT,         /* the CRC does not match                      */
    SNAP_OTHER_RAM,       /* the other RAM fit                           */
    SNAP_OTHER_ROM,       /* the ROM is not the one it ran on            */
    SNAP_OTHER_MACHINE,   /* the Microdisc fitted, or not                */
    SNAP_OTHER_FIELD,     /* another line length or field shape (§11.1)  */
    SNAP_BUSY,            /* a tape request, or a disc command, waiting  */
} snap_status_t;

/* The machine a state was taken on, for naming it in a refusal. */
typedef struct {
    rom_id_t   rom;       /* by the SHA-1 in the file; ROM_UNKNOWN if none of ours */
    oric_ram_t ram;
    bool       microdisc;
} snap_info_t;

/* Move exactly n bytes; false on any failure. */
typedef bool (*snap_write_fn)(void *ctx, const uint8_t *src, size_t n);
typedef bool (*snap_read_fn)(void *ctx, uint8_t *dst, size_t n);

snap_status_t snapshot_save(const oric_t *m, snap_write_fn write, void *ctx);

/* The whole stream, checked against m; m is not changed. `info`, if not
 * NULL, says what machine the file is for once its state has been read,
 * whether or not that is m. */
snap_status_t snapshot_check(const oric_t *m, snap_read_fn read, void *ctx, snap_info_t *info);

snap_status_t snapshot_load(oric_t *m, snap_read_fn read, void *ctx);

const char *snapshot_status_str(snap_status_t st);

/* CRC-32 (IEEE 802.3, reflected, as zlib computes it), exposed for the
 * test that pins it against a known value. */
uint32_t snapshot_crc32(uint32_t crc, const uint8_t *p, size_t n);

#endif /* PICO_ORIC_SNAPSHOT_H */
