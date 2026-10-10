/* tape.h — tape phase 1: the ROM's tape routines served by a trap
 * (design.md §10.3; EL §8.2).
 *
 * Both ROMs load a file in two steps and save it in two: the header
 * (sync, #24, nine bytes, the name), then the data from the start
 * address to the end. The trap is on each step's first instruction, in
 * a table per ROM, and stands aside unless the ROM in the socket is one
 * of the two stock images by SHA-1 (romset.h) and its page is mapped as
 * ROM. Everything around the steps is the ROM's own: CLOAD's options,
 * the VIA's set-up, "Searching..", "Found", comparing the name and
 * looking on when it is not the one asked for, the type checks, the
 * BASIC re-link, autorun, and the clean-up.
 *
 *   1.1                              1.0
 *   #E4AC  find a header             #E4B2
 *   #E4E0  read the data             #E4EB
 *   #E607  write the header          #E57B
 *   #E62E  write the data            #E5A7
 *
 * When the PC reaches a load step, the CPU stalls there as if RDY were
 * held low (EL §8.2): guest time passes, the VIA ticks, no instruction
 * runs, and the request waits in oric_t.tape for the port, which serves
 * it from a .tap file at the next field boundary, or declines it. A
 * declined step runs as the ROM's, which waits for a signal on CB1 that
 * nothing sends, as a real Oric does with no tape playing, until the
 * reset button.
 *
 * A served step does not return. The trap leaves the machine where the
 * ROM's loop is as it finishes the last byte it would have moved, and
 * lets the ROM run the rest: the header's zero is stored by the ROM's
 * own STA, the data's last address is compared and stepped past by its
 * own #E56C (1.0: #E554), and the routine's RTS is its own. test_tape
 * holds what is left to the ROM's routine run with only its byte
 * routines hooked. The deliberate differences: the stack below SP, where
 * the bit and byte routines the trap does not run leave their return
 * addresses and saved registers; and the VIA, whose T1 and T2 those
 * routines program and whose flags they clear.
 *
 * The header write is not a request: the trap keeps the header and name
 * and the ROM goes on at once. The data write that follows is, and the
 * port writes the whole file, header and data, so that a tape on the
 * card only ever holds whole files.
 */
#ifndef PICO_ORIC_TAPE_H
#define PICO_ORIC_TAPE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "tap.h"

struct oric_s;

typedef enum {
    TAPE_NONE = 0,
    TAPE_FIND,        /* find the next header                             */
    TAPE_LOAD,        /* read the data, or verify it (1.1's CLOAD "",V)   */
    TAPE_SAVE,        /* write the file: the kept header, then the data   */
} tape_op_t;

/* One ROM's routines and variables, read off the ROM (§16). */
typedef struct {
    uint16_t find_pc, find_resume;   /* the ROM's STA of the name's zero        */
    uint16_t load_pc, load_resume;   /* the ROM's JSR to its address step       */
    uint16_t load_wait;              /* the loop's byte read: a short file      */
    uint16_t hsave_pc, hsave_resume; /* after the name's zero went out          */
    uint16_t dsave_pc, dsave_resume; /* the address step after a byte went out  */
    uint16_t header;     /* where the header's first byte goes; the rest below  */
    uint16_t name;       /* where a name read off tape goes                     */
    uint8_t  name_cap;   /* how many of its bytes the ROM stores                */
    uint16_t want;       /* the name CLOAD and CSAVE were given, ending in zero */
    uint16_t start, end; /* the data's addresses, low byte first                */
    uint16_t errors;     /* 1.1's parity error flag, cleared by the find; or 0  */
    uint16_t verify;     /* 1.1's verify flag; 0 for 1.0, which has none        */
    uint16_t verify_errors;
    uint16_t slow;       /* nonzero for CLOAD and CSAVE's ,S                    */
} tape_rom_t;

/* Both ROMs' byte routines leave the byte in zero page #2F, and their
 * data loops step a pointer in #33/#34. */
#define TAPE_ZP_BYTE  0x2Fu
#define TAPE_ZP_PTR   0x33u

typedef struct {
    tape_op_t op;            /* the request the CPU is stalled on        */
    const tape_rom_t *rom;   /* the ROM in the socket's, NULL for any other */

    /* TAPE_FIND: the name asked for, "" for the next file. */
    uint8_t   want[ORIC_TAP_NAME_MAX + 1];
    bool      slow;          /* the ROM's speed, ,S: for the log         */

    /* TAPE_LOAD and TAPE_SAVE: the ROM's start and end addresses. */
    uint16_t  start, end;
    uint32_t  len;           /* bytes, tap_data_len                       */
    bool      verify;

    /* A load being served. */
    uint32_t  done;
    uint8_t   last;
    uint16_t  mismatches;    /* a verify's, added to the ROM's count      */

    /* The header the ROM has just written, kept for TAPE_SAVE. */
    bool      header_kept;
    uint8_t   raw[TAP_HEADER_LEN];
    uint8_t   name[ORIC_TAP_NAME_MAX];
    uint8_t   name_len;

    bool      pass;          /* declined: let the ROM run this step once  */
    uint16_t  pass_pc;       /* the step's PC                             */
    uint32_t  served, declined;
} tape_t;

/* The low bytes of every trapped PC, so the run loop pays one load per
 * instruction (tape_at). Not const, so that it sits in SRAM beside the
 * loop (EL §8.2). */
extern uint8_t tape_pc_lo[256];

/* oric.c's: which ROM's table applies, from the image just loaded; and
 * the PC offered at an instruction boundary whose low byte is in
 * tape_pc_lo. True if the CPU is to stall this boundary. */
void tape_rom_loaded(struct oric_s *m, const uint8_t *image, size_t len);
bool tape_at(struct oric_s *m);

/* The RESET line or a power-on: any request goes with the program that
 * made it. */
void tape_reset(struct oric_s *m);

/* The request the CPU is stalled on, or NULL. */
const tape_t *oric_tape_pending(const struct oric_s *m);

/* No file answers: the ROM's own step runs, once, and waits for a
 * signal. */
void oric_tape_decline(struct oric_s *m);

/* No file answers a load, and the port will not leave the ROM waiting
 * for a signal: the request goes, and the reset button is pressed, the
 * ROM's warm start, program kept (§2.1). The owner's choice over the
 * real machine's wait (2026-10-09). */
void oric_tape_give_up(struct oric_s *m);

/* Serving TAPE_FIND with the next header on the tape. */
void oric_tape_found(struct oric_s *m, const tap_header_t *h);

/* Serving TAPE_LOAD: the data, in as many pieces as suit the port, while
 * oric_tape_load_wants; then end. A load writes through the bus, as the
 * ROM's STA would, ROM and I/O included; a verify compares, reading
 * without side effects. A file shorter than the header says leaves the
 * ROM's loop waiting for its next byte, as a tape that stopped would. */
void oric_tape_load_data(struct oric_s *m, const uint8_t *src, size_t n);
bool oric_tape_load_wants(const struct oric_s *m);
void oric_tape_load_end(struct oric_s *m);

/* A load the tape ends one byte short of, as 8% of the archive's tapes
 * do (design.md §10.3, M12's corpus): the last address keeps the byte it
 * holds, as if read off tape, so that the ROM finishes the load; then
 * end. False, and nothing done, unless exactly one byte is wanted.
 * Oricutron allows the same byte "for broken tape images" (tape.c). */
bool oric_tape_load_keep(struct oric_s *m);

/* Serving TAPE_SAVE: the file is the kept header (tap_encode_header of
 * raw and name) and the request's len bytes from start, read with
 * oric_peek; once it is on the card, end. */
void oric_tape_save_end(struct oric_s *m);

#endif /* PICO_ORIC_TAPE_H */
