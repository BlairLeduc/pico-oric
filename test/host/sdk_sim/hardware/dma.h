/* Host stand-in for hardware/dma.h (sdk_sim.h). */
#ifndef PICO_ORIC_SIM_DMA_H
#define PICO_ORIC_SIM_DMA_H

#include "sdk_sim.h"

#define DMA_SIZE_32 2

typedef struct {
    unsigned ring_bits;
    int      chain_to;
    bool     irq0;
} dma_channel_config;

typedef struct {
    dma_channel_config cfg;
    uint32_t reload;         /* the count last written               */
    uint32_t count;          /* live                                 */
    bool     busy;
} sim_channel_t;
extern sim_channel_t g_sim_ch[SIM_DMA_CHANNELS];

int dma_claim_unused_channel(bool required);
void dma_channel_configure(int ch, const dma_channel_config *c, volatile void *write,
                           const volatile void *read, unsigned count, bool go);

static inline dma_channel_config dma_channel_get_default_config(int ch) {
    dma_channel_config c = { 0, ch, false };
    return c;
}
static inline void channel_config_set_transfer_data_size(dma_channel_config *c, int size) {
    (void)c;
    (void)size;
}
static inline void channel_config_set_read_increment(dma_channel_config *c, bool on) {
    (void)c;
    (void)on;
}
static inline void channel_config_set_write_increment(dma_channel_config *c, bool on) {
    (void)c;
    (void)on;
}
static inline void channel_config_set_dreq(dma_channel_config *c, unsigned dreq) {
    (void)c;
    (void)dreq;
}
static inline void channel_config_set_ring(dma_channel_config *c, bool write, unsigned bits) {
    (void)write;
    c->ring_bits = bits;
}
static inline void channel_config_set_chain_to(dma_channel_config *c, int ch) {
    c->chain_to = ch;
}
static inline void dma_channel_set_irq0_enabled(int ch, bool on) {
    g_sim_ch[ch].cfg.irq0 = on;
}
void sim_trigger(int ch);

static inline void dma_channel_set_read_addr(int ch, const volatile void *a, bool go) {
    dma_hw->ch[ch].read_addr = (uintptr_t)a;
    if (go) sim_trigger(ch);
}
static inline void dma_channel_set_trans_count(int ch, unsigned n, bool go) {
    g_sim_ch[ch].reload = n;
    if (go) sim_trigger(ch);
}
static inline void dma_channel_start(int ch) {
    sim_trigger(ch);
}
static inline bool dma_channel_is_busy(int ch) {
    return g_sim_ch[ch].busy;
}

#endif
