/* oricutron-trace.c — Oricutron's machine, headless, printing one line
 * per instruction: the reference half of the trace diff (design.md
 * §13.4). tools/trace/build-oricutron.sh builds it against a copy of an
 * Oricutron checkout; nothing of Oricutron is in this tree.
 *
 *   oricutron-trace ROMBASE [-m atmos|oric1|o16k] [-n INSNS] [-c CYCLES]
 *                   [-k KEYS] [-s] [-t TAPE] [-q]
 *
 * ROMBASE is the ROM's path without ".rom", as Oricutron names ROMs.
 * "atmos" is a 48K machine with the ROM in the Atmos's socket, "oric1"
 * the same with the Oric-1's, "o16k" a 16K Oric-1; any ROM goes in any.
 *
 * Each line is the state before an instruction, after any interrupt
 * entry: PC A X Y S P, cycles, and the three bytes at PC, as oric-trace
 * prints it. The build script puts a call to oricutron_trace() in
 * m6502_inst() where Oricutron's own DEBUG_CPU_TRACE hook is, after the
 * interrupt push and before the opcode runs. P has bits 4 and 5 set,
 * because Oricutron keeps neither.
 *
 * The loop is main.c's frameloop_normal() without SDL: set_icycles, the
 * VIA and the AY clocked by the instruction's cycles before it runs (so
 * Oricutron's VIA is an instruction ahead of a reader), then the
 * instruction, then the ULA's raster. -t puts a .tap in Oricutron's deck,
 * played as a signal on CB1 with its tape traps off (tape_patches is
 * never called, turbo is off), so the ROM's own routines read it: the
 * check that a tape this project writes loads elsewhere (design.md §15.2
 * M10). -q prints no trace, for a run that long. RAM is zeroed after init_machine()
 * blanks it to Oricutron's pattern, to match this project's power-on
 * (design.md §6.3).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system.h"
#include "6502.h"
#include "via.h"
#include "8912.h"
#include "gui.h"
#include "disk.h"
#include "monitor.h"
#include "6551.h"
#include "machine.h"
#include "main.h"
#include "ula.h"
#include "tape.h"

#include "keyscript.h"

extern char atmosromfile[1024], oric1romfile[1024], mdiscromfile[1024];
void load_diskroms(struct machine *oric);

/* ---- the trace -------------------------------------------------------- */

static struct machine oric;
static keyscript_t ks;
static unsigned long long insns, max_insns = ~0ull, max_cycles = ~0ull;
static int quiet;

static unsigned peek(Uint16 a) {
    /* Code never runs from page #03, so this read has no side effect. */
    return oric.cpu.read(&oric.cpu, a);
}

void oricutron_trace(struct m6502 *cpu) {
    /* m6502_inst has added this step's cycles, the interrupt entry's
     * seven among them; the line is the state after entry. */
    if (quiet) {
        insns++;
        return;
    }
    unsigned long long now = cpu->cycles - cpu->icycles + (cpu->calcint ? 7u : 0u);
    unsigned p = (cpu->f_n ? 0x80u : 0) | (cpu->f_v ? 0x40u : 0) | 0x30u | (cpu->f_d ? 0x08u : 0) |
                 (cpu->f_i ? 0x04u : 0) | (cpu->f_z ? 0x02u : 0) | (cpu->f_c ? 0x01u : 0);
    printf("%04X %02X %02X %02X %02X %02X %llu %02X%02X%02X\n", cpu->calcpc, cpu->a, cpu->x, cpu->y,
           cpu->sp, p, now, peek(cpu->calcpc), peek((Uint16)(cpu->calcpc + 1)),
           peek((Uint16)(cpu->calcpc + 2)));
    insns++;
}

static void press(const ks_event_t *e) {
    struct ay8912 *ay = &oric.ay;
    if (e->down) ay->keystates[e->row] |= (Uint8)(1u << e->col);
    else         ay->keystates[e->row] &= (Uint8)~(1u << e->col);
    ay_update_keybits(ay);
}

int main(int argc, char **argv) {
    const char *rombase = NULL, *keyfile = NULL, *mach = "atmos", *tapefile = NULL;
    const char *discfile = NULL;
    int screen = 0, pattern = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-n") && i + 1 < argc)      max_insns = strtoull(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "-c") && i + 1 < argc) max_cycles = strtoull(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "-k") && i + 1 < argc) keyfile = argv[++i];
        else if (!strcmp(argv[i], "-m") && i + 1 < argc) mach = argv[++i];
        else if (!strcmp(argv[i], "-s"))                 screen = 1;
        else if (!strcmp(argv[i], "-q"))                 quiet = 1;
        else if (!strcmp(argv[i], "-t") && i + 1 < argc) tapefile = argv[++i];
        else if (!strcmp(argv[i], "-d") && i + 1 < argc) discfile = argv[++i];
        else if (!strcmp(argv[i], "-p"))                 pattern = 1;
        else if (!rombase)                               rombase = argv[i];
        else rombase = NULL, argc = 0;
    }
    int type = !strcmp(mach, "atmos") ? MACH_ATMOS : !strcmp(mach, "oric1") ? MACH_ORIC1
             : !strcmp(mach, "o16k") ? MACH_ORIC1_16K : -1;
    if (!rombase || type < 0 || (max_insns == ~0ull && max_cycles == ~0ull)) {
        fprintf(stderr, "usage: %s ROMBASE [-m atmos|oric1|o16k] [-n INSNS] [-c CYCLES] [-k KEYS] [-s] "
                "[-t TAPE] [-d DISC] [-p] [-q]\n", argv[0]);
        return 2;
    }
    if (!ks_load(&ks, keyfile)) return 2;

    /* Audio through SDL's dummy driver; no video at all. */
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    if (SDL_Init(SDL_INIT_AUDIO) < 0) { fprintf(stderr, "SDL_Init failed\n"); return 2; }

    memset(&oric, 0, sizeof oric);
    preinit_ula(&oric);
    preinit_machine(&oric);
    /* -d: the Microdisc, its EPROM microdis.rom beside ROMBASE, and the
     * disc in drive 0 (design.md §10.5, M14). */
    oric.drivetype = discfile ? DRV_MICRODISC : DRV_NONE;
    snprintf(atmosromfile, sizeof atmosromfile, "%s", rombase);
    snprintf(oric1romfile, sizeof oric1romfile, "%s", rombase);
    if (discfile) {
        const char *slash = strrchr(rombase, '/');
        snprintf(mdiscromfile, sizeof mdiscromfile, "%.*smicrodis",
                 slash ? (int)(slash - rombase + 1) : 0, rombase);
        load_diskroms(&oric);
    }
    if (!init_ula(&oric) || !init_machine(&oric, type, SDL_TRUE)) {
        fprintf(stderr, "Oricutron's machine did not initialise\n");
        return 2;
    }
    /* Zeroed RAM below #C000, as this project powers on (§6.3); the
     * overlay RAM above keeps Oricutron's pattern, which is ours there.
     * -p keeps the pattern throughout. */
    if (!pattern) memset(oric.mem, 0, type == MACH_ORIC1_16K ? 16384 : 0xC000);
    if (discfile && !diskimage_load(&oric, (char *)discfile, 0)) {
        fprintf(stderr, "%s: Oricutron would not load it\n", discfile);
        return 2;
    }
    m6502_reset(&oric.cpu);
    oric.tapeturbo = SDL_FALSE;
    if (tapefile && !tape_load_tap(&oric, (char *)tapefile)) {
        fprintf(stderr, "%s: Oricutron would not load it\n", tapefile);
        return 2;
    }

    struct m6502 *cpu = &oric.cpu;
    unsigned long long base = cpu->cycles;
    while (insns < max_insns && cpu->cycles - base < max_cycles) {
        const ks_event_t *e;
        while ((e = ks_due(&ks, cpu->cycles - base)) != NULL) press(e);
        m6502_set_icycles(cpu, SDL_FALSE, NULL);
        via_clock(&oric.via, cpu->icycles);
        ay_ticktock(&oric.ay, cpu->icycles);
        if (discfile) wd17xx_ticktock(&oric.wddisk, cpu->icycles);
        cpu->rastercycles -= cpu->icycles;
        if (m6502_inst(cpu)) { fprintf(stderr, "JAM %02X at %04X\n", cpu->calcop, cpu->lastpc); break; }
        if (cpu->rastercycles <= 0) {
            ula_doraster(&oric);
            cpu->rastercycles += oric.cyclesperraster;
        }
    }
    fflush(stdout);

    if (screen) {
        for (int row = 0; row < 28; row++) {
            char line[41];
            int end = 0;
            for (int col = 0; col < 40; col++) {
                unsigned b = peek((Uint16)(0xBB80 + row * 40 + col)) & 0x7Fu;
                line[col] = (b & 0x60u) && b != 0x7Fu ? (char)b : ' ';
                if (line[col] != ' ') end = col + 1;
            }
            line[end] = 0;
            fprintf(stderr, "%s\n", line);
        }
    }
    return 0;
}
