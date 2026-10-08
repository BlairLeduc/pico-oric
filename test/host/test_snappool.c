/* test_snappool.c — the frame pool (design.md §4.4, §13.2).
 *
 * pico-ace's pool tests under the new names, with a frame taken from a
 * machine: the state machine has no lock of its own, so both cores'
 * transitions can be interleaved here at random. No ROM: the frame is
 * checked against bytes written through the bus, in both RAM fits.
 */

#include <string.h>

#include "bus.h"
#include "oric.h"
#include "snappool.h"
#include "test_util.h"

static snappool_t g_pool;
static oric_t g_m;

static unsigned rng_state = 12345;
static unsigned rng(void) {
    rng_state = rng_state * 1103515245u + 12345u;
    return (rng_state >> 16) & 0x7FFFu;
}

int main(void) {
    snappool_t *p = &g_pool;

    /* ---- the transitions --------------------------------------------- */
    snappool_init(p);
    CHECK(snappool_take(p) < 0, "nothing to take from an empty pool");

    int a = snappool_claim(p);
    CHECK(a >= 0, "claim from an empty pool should succeed");
    CHECK(snappool_take(p) < 0, "a filling buffer is invisible to core 1");
    snappool_publish(p, a);

    int r = snappool_take(p);
    CHECK(r == a, "core 1 should take the published buffer");

    /* Core 1 is a full field behind: core 0 publishes twice while it
     * renders. The first is superseded, and core 0 never runs dry. */
    int b = snappool_claim(p);
    snappool_publish(p, b);
    int c = snappool_claim(p);
    CHECK(c >= 0, "core 0 must never run out of buffers (the third buffer)");
    snappool_publish(p, c);
    CHECK(p->dropped == 1, "the older ready snapshot should be dropped, dropped=%u",
          (unsigned)p->dropped);
    int d = snappool_claim(p);
    CHECK(d == b, "the superseded buffer should be free again for core 0");

    snappool_release(p, r);
    CHECK(snappool_take(p) == c, "core 1 should get the newest snapshot, not the oldest");

    /* Out-of-state calls change nothing. */
    snappool_publish(p, r);
    snappool_release(p, d);
    CHECK(p->state[r] == SNAP_FREE && p->state[d] == SNAP_FILLING,
          "publishing a free buffer or releasing a filling one must be ignored");

    /* ---- a long randomised interleaving ------------------------------ */
    /* Core 0 always gets a buffer, never one core 1 holds, and every
     * publish is either taken or counted as dropped. */
    snappool_init(p);
    int filling = snappool_claim(p), rendering = -1;
    uint32_t taken = 0;
    for (unsigned step = 0; step < 100000; step++) {
        if (rng() & 1u) {
            snappool_publish(p, filling);
            filling = snappool_claim(p);
            CHECK(filling >= 0 && filling != rendering,
                  "step %u: core 0 claimed %d while core 1 renders %d", step, filling,
                  rendering);
        } else if (rendering < 0) {
            rendering = snappool_take(p);
            if (rendering >= 0) taken++;
        } else {
            snappool_release(p, rendering);
            rendering = -1;
        }
        unsigned ready = 0;
        for (unsigned i = 0; i < ORIC_SNAPSHOT_COUNT; i++) ready += p->state[i] == SNAP_READY;
        CHECK(ready <= 1, "step %u: %u buffers ready at once", step, ready);
    }
    unsigned pending = 0;
    for (unsigned i = 0; i < ORIC_SNAPSHOT_COUNT; i++) pending += p->state[i] == SNAP_READY;
    CHECK(taken + p->dropped + pending == p->published,
          "published %u = taken %u + dropped %u + pending %u", (unsigned)p->published,
          (unsigned)taken, (unsigned)p->dropped, pending);
    printf("%u published, %u taken, %u dropped\n", (unsigned)p->published, (unsigned)taken,
           (unsigned)p->dropped);

    /* ---- a frame is the window, the start mode and the blink phase -- */
    for (int fit = 0; fit < 2; fit++) {
        oric_config_t cfg;
        oric_config_default(&cfg);
        cfg.ram = fit ? ORIC_RAM_16K : ORIC_RAM_48K;
        oric_init(&g_m, &cfg);
        oric_map_ram(&g_m, 0xC000, 0x4000);
        /* #C000: JMP #C000, with the reset vector pointing at it. */
        bus_write(&g_m, 0xC000, 0x4C);
        bus_write(&g_m, 0xC001, 0x00);
        bus_write(&g_m, 0xC002, 0xC0);
        g_m.cpu.pc = 0xC000;

        /* On a 16K machine, written at #3B80 and fetched at #BB80. */
        uint16_t screen = fit ? 0x3B80 : 0xBB80;
        bus_write(&g_m, screen, 'O');
        bus_write(&g_m, (uint16_t)(screen + 27 * 40 + 39), 0x1E);   /* hires, 50 Hz */
        bus_write(&g_m, 0x9800, 0x5A);
        bus_write(&g_m, 0xBFFF, 0xA5);

        oric_frame_t *f = &p->buf[0];
        oric_run_field(&g_m);
        oric_video_take(&g_m, f);
        CHECK(f->window[ORIC_TEXT_BASE - ORIC_VIDEO_BASE] == 'O' && f->window[0] == 0x5A &&
                  f->window[ORIC_VIDEO_BYTES - 1] == 0xA5,
              "%s: the window is #9800-#BFFF as the ULA fetches it", fit ? "16K" : "48K");
        CHECK(f->mode == ULA_MODE_POWER_ON && f->field == 1 && f->blink_on,
              "%s: the first frame starts in the power-on mode, blink shown", fit ? "16K" : "48K");
        CHECK(g_m.ula_mode == 0x06, "%s: and leaves the mode the scan found for the next",
              fit ? "16K" : "48K");
        oric_run_field(&g_m);
        oric_video_take(&g_m, f);
        CHECK(f->mode == 0x06, "%s: the second frame starts in hires", fit ? "16K" : "48K");
        for (unsigned i = 2; i <= cfg.blink_fields; i++) oric_run_field(&g_m);
        oric_video_take(&g_m, f);
        CHECK(!f->blink_on, "after %u fields, the blink phase turns", cfg.blink_fields);
    }

    TEST_DONE();
}
