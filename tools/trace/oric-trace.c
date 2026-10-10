/* oric-trace.c — this project's core on the host, printing one line per
 * instruction: our half of the trace diff (design.md §13.4).
 *
 *   oric-trace ROM [-r 16|48] [-n INSNS] [-c CYCLES] [-k KEYS] [-s] [-v]
 *
 * Each line is the state before an instruction: PC A X Y S P, cycles, and
 * the three bytes at PC, as oricutron-trace prints it. Interrupt entry is
 * a step of its own in m6502_step but not an instruction, so it has no
 * line; the handler's first instruction does. P is printed with bits 4
 * and 5 set, because Oricutron keeps neither. -s prints the text screen
 * on stderr at the end. -v fits the vertical-sync modification
 * (vsync.h, design.md §15.2 M16).
 *
 * The machine is oric_config_default's with the RAM asked for, RAM zeroed
 * (§6.3), powered on with the ROM in, run an instruction at a time: oric_run(m, 1) executes exactly
 * one step.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "keyscript.h"
#include "m6502.h"
#include "oric.h"

static oric_t machine;
static keyscript_t ks;

int main(int argc, char **argv) {
    const char *rom = NULL, *keyfile = NULL;
    unsigned long long max_insns = ~0ull, max_cycles = ~0ull;
    oric_ram_t ram = ORIC_RAM_48K;
    bool screen = false, vsync_hack = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-n") && i + 1 < argc)      max_insns = strtoull(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "-c") && i + 1 < argc) max_cycles = strtoull(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "-k") && i + 1 < argc) keyfile = argv[++i];
        else if (!strcmp(argv[i], "-r") && i + 1 < argc) ram = atoi(argv[++i]) == 16 ? ORIC_RAM_16K : ORIC_RAM_48K;
        else if (!strcmp(argv[i], "-s"))                 screen = true;
        else if (!strcmp(argv[i], "-v"))                 vsync_hack = true;
        else if (!rom)                                   rom = argv[i];
        else rom = NULL, argc = 0;
    }
    if (!rom || (max_insns == ~0ull && max_cycles == ~0ull)) {
        fprintf(stderr, "usage: %s ROM [-r 16|48] [-n INSNS] [-c CYCLES] [-k KEYS] [-s] [-v]\n", argv[0]);
        return 2;
    }
    if (!ks_load(&ks, keyfile)) return 2;

    static uint8_t image[ORIC_ROM_SIZE];
    FILE *f = fopen(rom, "rb");
    size_t n = f ? fread(image, 1, sizeof image, f) : 0;
    if (f) fclose(f);
    oric_t *m = &machine;
    oric_config_t cfg;
    oric_config_default(&cfg);
    cfg.ram = ram;
    cfg.vsync_hack = vsync_hack;
    oric_init(m, &cfg);
    if (!oric_load_rom(m, image, n)) {
        fprintf(stderr, "%s: not a 16 KiB ROM\n", rom);
        return 2;
    }
    /* Power on with the ROM in, as the firmware does (core0.c): the
     * field, and the ULA's count with it, starts at the first
     * instruction, as Oricutron's raster does (vsync.h). */
    oric_power_on(m);

    const m6502_t *c = &m->cpu;
    uint64_t base = c->cycles;
    for (unsigned long long insns = 0; insns < max_insns && c->cycles - base < max_cycles;) {
        const ks_event_t *e;
        while ((e = ks_due(&ks, c->cycles - base)) != NULL) {
            if (!e->n) {
                oric_key_set(m, (int)e->row, (int)e->col, e->down);
                continue;
            }
            for (unsigned k = 0; k < e->n; k++) {
                uint16_t a = (uint16_t)(e->addr + k);
                uint8_t *p = m->page[a >> 8].write;
                if (p) p[a & 0xFFu] = e->bytes[k];
            }
        }
        /* As m6502_step decides it: the I that CLI, SEI or PLP left for
         * this one poll (irq_masked in m6502.c). */
        uint8_t i_seen = (c->cycles == c->i_old_at) ? c->i_old : c->p;
        bool entry = c->reset_pending || c->nmi_pending ||
                     (c->irq_lines != 0 && !(i_seen & M6502_I));
        if (!entry) {
            printf("%04X %02X %02X %02X %02X %02X %llu %02X%02X%02X\n", c->pc, c->a, c->x, c->y,
                   c->s, (unsigned)((c->p & 0xCFu) | 0x30u), (unsigned long long)(c->cycles - base),
                   oric_peek(m, c->pc), oric_peek(m, (uint16_t)(c->pc + 1)),
                   oric_peek(m, (uint16_t)(c->pc + 2)));
            insns++;
        }
        oric_run(m, 1);
    }
    fflush(stdout);

    if (screen) {
        for (int row = 0; row < 28; row++) {
            char line[41];
            int end = 0;
            for (int col = 0; col < 40; col++) {
                uint8_t b = oric_peek(m, (uint16_t)(0xBB80 + row * 40 + col)) & 0x7Fu;
                line[col] = (b & 0x60u) && b != 0x7Fu ? (char)b : ' ';
                if (line[col] != ' ') end = col + 1;
            }
            line[end] = 0;
            fprintf(stderr, "%s\n", line);
        }
    }
    return 0;
}
