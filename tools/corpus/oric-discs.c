/* oric-discs.c — M14's disc corpus: archive disc images booted on the
 * host with the Microdisc, one line each (design.md §15.2 M14).
 *
 *   oric-discs [-r 10|11] [-f FIELDS] [-s DIR] DSK...
 *
 * Each image goes in drive A of a 48K machine with the Microdisc, which
 * is powered on and run for FIELDS fields (1,500 by default, 30 s at 50
 * Hz) with no key pressed, the disc served as discio serves the card's.
 * The screen is then written as tools/trace/oricutron-trace -s prints
 * it, to DIR/<name>.txt, so that tools/disc-corpus.sh can hold it to
 * Oricutron's after the same time. A disc that is written to is written
 * in memory only.
 *
 * One line per image on stdout, tab-separated:
 *   name  outcome  sectors-read  sectors-written  tracks-in  undoc  note
 * where outcome is "loaded" (more sectors read than the EPROM's own boot,
 * eight), "booted" (some), "nothing" (none) or "refused" (the image
 * would not go in).
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "disc_util.h"
#include "guest.h"
#include "microdisc.h"

static void screen(const oric_t *m, FILE *f) {
    /* oricutron-trace.c's -s, character for character. */
    for (int row = 0; row < 28; row++) {
        char line[41];
        int end = 0;
        for (int col = 0; col < 40; col++) {
            unsigned b = oric_peek(m, (uint16_t)(0xBB80 + row * 40 + col)) & 0x7Fu;
            line[col] = (b & 0x60u) && b != 0x7Fu ? (char)b : ' ';
            if (line[col] != ' ') end = col + 1;
        }
        line[end] = 0;
        fprintf(f, "%s\n", line);
    }
}

static const char *base(const char *p) {
    const char *b = strrchr(p, '/');
    return b ? b + 1 : p;
}

int main(int argc, char **argv) {
    rom_id_t rom = ROM_BASIC11;
    long fields = 1500;
    const char *sdir = NULL;
    int i = 1;
    for (; i < argc && argv[i][0] == '-'; i++) {
        if (!strcmp(argv[i], "-r") && i + 1 < argc) rom = atoi(argv[++i]) == 10 ? ROM_BASIC10 : ROM_BASIC11;
        else if (!strcmp(argv[i], "-f") && i + 1 < argc) fields = atol(argv[++i]);
        else if (!strcmp(argv[i], "-s") && i + 1 < argc) sdir = argv[++i];
        else { fprintf(stderr, "usage: %s [-r 10|11] [-f FIELDS] [-s DIR] DSK...\n", argv[0]); return 2; }
    }
    const char *dir;
    if (!guest_find_roms(&dir) || !guest_have_rom(ROM_MICRODISC)) {
        fprintf(stderr, "oric-discs: needs basic10.rom, basic11b.rom and microdis.rom in %s\n", dir);
        return 2;
    }
    static guest_t g;
    for (; i < argc; i++) {
        static host_disc_t d;
        host_disc_t *drives[ORIC_DISC_DRIVES] = { &d };
        const char *why = host_disc_load(&d, argv[i]);
        if (why) {
            printf("%s\trefused\t0\t0\t0\t0\t%s\n", base(argv[i]), why);
            host_disc_free(&d);
            continue;
        }
        oric_config_t cfg;
        oric_config_default(&cfg);
        cfg.rom = rom;
        cfg.microdisc = true;
        oric_init(&g.m, &cfg);
        oric_load_rom(&g.m, guest_rom_image(rom), ORIC_ROM_SIZE);
        oric_load_eprom(&g.m, guest_rom_image(ROM_MICRODISC), ORIC_EPROM_SIZE);
        oric_power_on(&g.m);
        host_disc_insert(&g.m, 0, &d);
        for (long f = 0; f < fields; f++) {
            oric_run_field(&g.m);
            host_disc_serve(&g.m, drives);
        }
        const wd1793_t *c = &g.m.fdc;
        const char *outcome = c->sectors_read > 8 ? "loaded" : c->sectors_read ? "booted" : "nothing";
        printf("%s\t%s\t%u\t%u\t%u\t%u\t%s\n", base(argv[i]), outcome, (unsigned)c->sectors_read,
               (unsigned)c->sectors_written, (unsigned)d.gets, (unsigned)g.m.cpu.undoc_count,
               wd_busy(c) ? "busy at the end" : "");
        fflush(stdout);
        if (sdir) {
            char path[1024];
            snprintf(path, sizeof path, "%s/%s.txt", sdir, base(argv[i]));
            FILE *f = fopen(path, "w");
            if (f) {
                screen(&g.m, f);
                fclose(f);
            }
        }
        host_disc_free(&d);
    }
    return 0;
}
