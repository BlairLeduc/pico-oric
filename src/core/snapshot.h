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
 *           holds it, the overlay under the ROM included (§2.1); from
 *           version 2, then the media (SNAP_MEDIA_LEN)
 *
 * The media are the files the port had in the drives and the deck, and
 * the deck's place (snap_media_t): names, not contents, which stay on
 * the card. The core carries them and never opens them; the port fills
 * them on a save and puts them back after a load, so a program resumes
 * with its discs in and its tape where it was. A version 1 state has
 * none, and leaves the port's media as they are.
 *
 * The ROM is not in a state: what is, is its SHA-1, and a state loads
 * only into a machine whose ROM hashes the same and whose RAM, Microdisc,
 * vertical-sync modification and field are the same, since every count
 * in it is in that machine's cycles. The field is the build's; the rest
 * the user can change, and snapshot_machine says what a state needs, so
 * that the port can power on as that machine and load it (snapio.h).
 * Unused state bytes are written zero and read as reserved, so a later
 * version can add fields whose zero is their reset value: from M14, the
 * Microdisc's; from M16, the vertical-sync modification's.
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

#define SNAP_VERSION     2u
#define SNAP_HEADER_LEN  20u
#define SNAP_STATE_LEN   320u
/* Each drive's path and the deck's, ORIC_PATH_MAX bytes NUL-padded, then
 * the deck's place: next header, data passed over, files found (4 bytes
 * each), and flags (1), and 3 reserved. */
#define SNAP_MEDIA_LEN   ((ORIC_DISC_DRIVES + 1u) * ORIC_PATH_MAX + 16u)
#define SNAP_PAYLOAD_V1  (SNAP_STATE_LEN + ORIC_ADDR_SPACE)
#define SNAP_PAYLOAD_LEN (SNAP_PAYLOAD_V1 + SNAP_MEDIA_LEN)
#define SNAP_FILE_LEN    (SNAP_HEADER_LEN + SNAP_PAYLOAD_LEN)

typedef enum {
    SNAP_OK = 0,
    SNAP_IO,              /* the callback failed, or the file is short   */
    SNAP_NOT_SNAPSHOT,    /* wrong magic or lengths                      */
    SNAP_NEWER,           /* a version this build does not know          */
    SNAP_CORRUPT,         /* the CRC does not match                      */
    SNAP_OTHER_RAM,       /* the other RAM fit                           */
    SNAP_OTHER_ROM,       /* the ROM is not the one it ran on            */
    SNAP_OTHER_MACHINE,   /* the Microdisc or the vsync hack, or not, or
                             another pulse                               */
    SNAP_OTHER_FIELD,     /* another line length or field                */
    SNAP_BUSY,            /* a tape request, or a disc command, waiting  */
    SNAP_NO_DISC,         /* the port's: a disc it names is not on the card */
    SNAP_TORN,            /* snapshot_load's read failed once m had begun
                             to change: it must be powered on again      */
} snap_status_t;

/* The port's media at the save (snapshot.h's header). Paths are as the
 * port keeps them, "" for an empty drive or deck. */
typedef struct {
    bool     present;             /* false for a version 1 state        */
    char     disc[ORIC_DISC_DRIVES][ORIC_PATH_MAX];
    char     tape[ORIC_PATH_MAX];
    uint32_t tape_pos;            /* where the next header is looked for */
    uint32_t tape_skip;           /* the last found file's data, not loaded */
    uint32_t tape_index;          /* files found since the start         */
    bool     tape_wrapped;        /* rewound by a load since the last data */
    bool     tape_user;           /* put in the deck by the user         */
} snap_media_t;

/* The machine a state was taken on, for naming it in a refusal and for
 * powering on as it (snapshot_machine). */
typedef struct {
    rom_id_t   rom;       /* by the SHA-1 in the file; ROM_UNKNOWN if none of ours */
    oric_ram_t ram;
    bool       microdisc;
    bool       vsync_hack;
    /* The pulse, with vsync_hack (oric_config_t's fields). */
    uint16_t   vsync_line_50hz, vsync_line_60hz, vsync_delay, vsync_low;
} snap_info_t;

/* Move exactly n bytes; false on any failure. */
typedef bool (*snap_write_fn)(void *ctx, const uint8_t *src, size_t n);
typedef bool (*snap_read_fn)(void *ctx, uint8_t *dst, size_t n);

/* `media` NULL writes empty drives and an empty deck. */
snap_status_t snapshot_save(const oric_t *m, const snap_media_t *media, snap_write_fn write,
                            void *ctx);

/* The whole stream, checked against m; m is not changed. `info`, if not
 * NULL, says what machine the file is for once its state has been read,
 * whether or not that is m. `media`, if not NULL, is the file's media
 * once it has passed, present false for a version 1 state. */
snap_status_t snapshot_check(const oric_t *m, snap_read_fn read, void *ctx, snap_info_t *info,
                             snap_media_t *media);

/* Every refusal leaves m as it was, but SNAP_TORN: the stream failing
 * in its RAM, after snapshot_check had read it whole, which is the card
 * going or the file changing between the passes. */
snap_status_t snapshot_load(oric_t *m, snap_read_fn read, void *ctx);

/* The machine a state needs, onto cfg: its ROM, RAM and Microdisc, and
 * the vertical-sync modification with its pulse. The field, the tape's
 * settings and the rest of cfg stay as they are, and so does the ROM
 * when the state's is none of ours. A state refused SNAP_OTHER_ROM,
 * SNAP_OTHER_RAM or SNAP_OTHER_MACHINE has passed every other check (the
 * field's first), and loads into a machine powered on as this with the
 * state's ROM: for SNAP_OTHER_ROM, one of ours (info->rom); for the
 * others, the ROM the state was checked against, whatever it is. */
void snapshot_machine(const snap_info_t *info, oric_config_t *cfg);

const char *snapshot_status_str(snap_status_t st);

/* CRC-32 (IEEE 802.3, reflected, as zlib computes it), exposed for the
 * test that pins it against a known value. */
uint32_t snapshot_crc32(uint32_t crc, const uint8_t *p, size_t n);

#endif /* PICO_ORIC_SNAPSHOT_H */
