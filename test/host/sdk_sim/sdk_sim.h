/* sdk_sim.h — just enough of the Pico SDK to run src/port/audio.c on the
 * host, with the DMA simulated (design.md §8.5, test_audio_port).
 *
 * The stand-ins for hardware/clocks.h, dma.h, gpio.h, irq.h, pwm.h and
 * sync.h all include this. The DMA: a channel reads words from its read
 * address, which wraps in the low bits the ring names, until its count
 * runs out; then it raises its interrupt and triggers the channel it
 * chains to. A trigger copies the last count written into the live
 * count, as the RP2350 datasheet's TRANS_COUNT has it, and leaves the
 * address where the last run left it. The PWM's wrap paces it:
 * sim_play() moves the clock on by a number of words, each to the sink,
 * which is what the speaker hears.
 */
#ifndef PICO_ORIC_SDK_SIM_H
#define PICO_ORIC_SDK_SIM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SIM_DMA_CHANNELS 4

/* ---- what audio.c touches directly ------------------------------------ */

typedef struct {
    volatile uintptr_t read_addr;    /* a host pointer, not 32 bits     */
} sim_dma_channel_hw_t;

typedef struct {
    sim_dma_channel_hw_t ch[SIM_DMA_CHANNELS];
    volatile uint32_t ints0;
} dma_hw_t;
extern dma_hw_t *const dma_hw;

typedef struct {
    volatile uint32_t cc;
} sim_pwm_slice_hw_t;
typedef struct {
    sim_pwm_slice_hw_t slice[12];
} pwm_hw_t;
extern pwm_hw_t *const pwm_hw;

#define __not_in_flash_func(f) f
#define __compiler_memory_barrier() __asm__ volatile("" ::: "memory")
/* __wfi is a compiler builtin on an Arm host. */
void sim_wfi(void);
#define __wfi() sim_wfi()

/* ---- the simulation ----------------------------------------------------- */

typedef struct {
    uint32_t *sink;          /* every word the PWM was given          */
    size_t    sink_len, sink_max;
    bool      irq_masked;    /* the IRQ is pended, not taken          */
    uint32_t  wfi_words;     /* words played by each __wfi            */
    uint32_t  irqs;          /* handler runs                          */
} sim_t;
extern sim_t g_sim;

/* Play n words: the running channel's, through completions and chains,
 * taking the IRQ at each completion unless it is masked. */
void sim_play(size_t n);

/* Unmask the IRQ, taking it now if it is pending. */
void sim_unmask(void);

#endif /* PICO_ORIC_SDK_SIM_H */
