/* m6502.h — MOS 6502 interpreter (design.md §5), from pico-atom.
 *
 * Cycle-correct at instruction granularity: every documented opcode
 * consumes the right number of cycles including page-crossing and branch
 * penalties, and decimal mode is exact.
 *
 * The CPU has no notion of a device. It reaches memory through bus.h,
 * and the bus layer is what advances devices (§5.2).
 */
#ifndef PICO_ORIC_M6502_H
#define PICO_ORIC_M6502_H

#include <stdbool.h>
#include <stdint.h>

struct oric_s;

/* Processor status bits. */
#define M6502_C 0x01u
#define M6502_Z 0x02u
#define M6502_I 0x04u
#define M6502_D 0x08u
#define M6502_B 0x10u
#define M6502_U 0x20u   /* unused, reads as 1 */
#define M6502_V 0x40u
#define M6502_N 0x80u

/* Vectors. */
#define M6502_VEC_NMI 0xFFFAu
#define M6502_VEC_RES 0xFFFCu
#define M6502_VEC_IRQ 0xFFFEu

/* IRQ sources, OR-ed into irq_lines so they compose correctly (§5.2):
 * the VIA, and from M14 the Microdisc's INTRQ. */
#define M6502_IRQ_VIA 0x01u
#define M6502_IRQ_DISC 0x02u

typedef struct {
    uint16_t pc;
    uint8_t  a, x, y, s, p;

    uint64_t cycles;

    uint8_t  irq_lines;      /* bitmask of asserted IRQ sources; 0 deasserts */
    bool     nmi_pending;    /* edge-triggered, latched                     */
    bool     nmi_line;       /* last level seen, for edge detection         */
    bool     reset_pending;  /* the RESET line (§6.3, §12)                 */

    /* CLI, SEI and PLP change I after the 6502 has polled IRQ for the
     * next instruction, so that one poll sees the I from before them
     * (§5.1). `i_old` is it, and it applies while `cycles` still equals
     * `i_old_at`, which is to say until the next instruction has run.
     * RTI's I takes effect at once. */
    uint8_t  i_old;
    uint64_t i_old_at;

    /* Which cycle of the instruction, from 1, a slow-path access falls
     * on, so that the VIA can be brought up to it before the access
     * (§5.3); 0 for an access outside an instruction's operand (a fetch,
     * the stack, a vector, a test's own). Written only on the slow path. */
    uint8_t  io_at;

    /* Undocumented opcodes: trapped and counted (§5.1). */
    uint32_t undoc_count;
    uint16_t undoc_pc;       /* PC of the most recent trapped opcode        */
    uint8_t  undoc_op;
} m6502_t;

void     m6502_init(m6502_t *c);
void     m6502_reset(m6502_t *c, struct oric_s *m);

/* Execute exactly one instruction (servicing a pending reset or interrupt
 * first, at the instruction boundary). Returns the cycles consumed. */
uint32_t m6502_step(struct oric_s *m);

/* Interrupt line control. NMI is edge-triggered: a false->true transition
 * latches. IRQ is level-sensitive. */
void     m6502_set_nmi(m6502_t *c, bool level);
void     m6502_set_irq(m6502_t *c, uint8_t source, bool asserted);

/* Base cycle count for an opcode, before page-cross and branch penalties.
 * Exposed so the host cycle-table test can assert against it (§13.2);
 * undocumented opcodes report 0. */
uint8_t  m6502_base_cycles(uint8_t opcode);
bool     m6502_is_documented(uint8_t opcode);

#endif /* PICO_ORIC_M6502_H */
