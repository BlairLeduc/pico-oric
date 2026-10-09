/* sdk_sim.c — the simulated DMA and IRQ (sdk_sim.h). */

#include "sdk_sim.h"

#include "hardware/dma.h"
#include "hardware/irq.h"

static dma_hw_t s_dma;
static pwm_hw_t s_pwm;
dma_hw_t *const dma_hw = &s_dma;
pwm_hw_t *const pwm_hw = &s_pwm;
sim_t g_sim;

sim_channel_t g_sim_ch[SIM_DMA_CHANNELS];
static int s_claimed;
irq_handler_t g_sim_handler;
bool g_sim_irq_enabled;

int dma_claim_unused_channel(bool required) {
    (void)required;
    /* The LCD has claimed channel 0 on the device (main.c). */
    return ++s_claimed;
}

void dma_channel_configure(int ch, const dma_channel_config *c, volatile void *write,
                           const volatile void *read, unsigned count, bool go) {
    (void)write;
    g_sim_ch[ch].cfg = *c;
    dma_hw->ch[ch].read_addr = (uintptr_t)read;
    g_sim_ch[ch].reload = count;
    if (go) sim_trigger(ch);
}

void sim_trigger(int ch) {
    g_sim_ch[ch].count = g_sim_ch[ch].reload;
    g_sim_ch[ch].busy = g_sim_ch[ch].count != 0;
}

/* Raised and not yet cleared. INTS0 is write-1-to-clear, which a plain
 * field cannot be: the handler sees one pending bit at a time in
 * dma_hw->ints0, and writing that bit back is the clear. Two bits make
 * two runs, as the NVIC would take the IRQ again. */
static uint32_t s_pending;

static void take_irq(void) {
    if (g_sim.irq_masked || !g_sim_irq_enabled || !g_sim_handler) return;
    while (s_pending) {
        uint32_t bit = s_pending & -s_pending;
        dma_hw->ints0 = bit;
        g_sim.irqs++;
        g_sim_handler();
        if (dma_hw->ints0 != bit) break;    /* not cleared: it would storm */
        s_pending &= ~bit;
        dma_hw->ints0 = 0;
    }
}

void sim_unmask(void) {
    g_sim.irq_masked = false;
    take_irq();
}

static int running(void) {
    for (int ch = 0; ch < SIM_DMA_CHANNELS; ch++)
        if (g_sim_ch[ch].busy) return ch;
    return -1;
}

void sim_play(size_t n) {
    while (n--) {
        int ch = running();
        if (ch < 0) return;    /* stopped: nothing paces the PWM's sink */
        sim_channel_t *c = &g_sim_ch[ch];
        uintptr_t a = dma_hw->ch[ch].read_addr;
        uint32_t w = *(const uint32_t *)a;
        if (g_sim.sink && g_sim.sink_len < g_sim.sink_max) g_sim.sink[g_sim.sink_len++] = w;
        /* The read address wraps in its low ring bits. */
        uintptr_t mask = c->cfg.ring_bits ? (((uintptr_t)1 << c->cfg.ring_bits) - 1u)
                                          : ~(uintptr_t)0;
        dma_hw->ch[ch].read_addr = (a & ~mask) | ((a + 4u) & mask);
        if (--c->count == 0) {
            c->busy = false;
            if (c->cfg.irq0) s_pending |= 1u << ch;
            int next = c->cfg.chain_to;
            if (next != ch) sim_trigger(next);
            take_irq();
        }
    }
}

void sim_wfi(void) {
    sim_play(g_sim.wfi_words ? g_sim.wfi_words : 1u);
}
