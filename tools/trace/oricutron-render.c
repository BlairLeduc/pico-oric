/* oricutron-render.c — Oricutron's ULA drawing a frame of ours: the
 * independent check of the golden images (design.md §7.7, §13.4).
 * tools/trace/build-oricutron.sh builds it against a copy of an
 * Oricutron checkout; nothing of Oricutron is in this tree.
 *
 *   oricutron-render FRAME OUT.ppm
 *
 * FRAME is what test_golden --frames writes: the 10 KiB window
 * #9800-#BFFF, then the mode byte and the blink byte. The window goes
 * into a 64 KiB memory at #9800; the ULA's state at the field's start is
 * set as its own mode attribute would leave it; then ula_doraster draws
 * the 224 visible lines, as main.c's loop calls it. The picture is
 * written with Oricutron's palette (machine.c), which is ours: R, G, B
 * from bit 0 at full level.
 *
 * Oricutron shows blinking cells while bit 4 of its frame counter is set,
 * so the blink byte picks a counter of #10 or 0.
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

#define WINDOW_BASE  0x9800
#define WINDOW_BYTES 10240

extern Uint8 oricpalette[];

/* 6502.c calls the trace hook the build script adds; nothing to trace. */
void oricutron_trace(struct m6502 *cpu) { (void)cpu; }

static struct machine oric;

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s FRAME OUT.ppm\n", argv[0]);
        return 2;
    }
    static unsigned char frame[WINDOW_BYTES + 2];
    FILE *f = fopen(argv[1], "rb");
    if (!f || fread(frame, 1, sizeof frame, f) != sizeof frame || fgetc(f) != EOF) {
        fprintf(stderr, "%s: not a frame\n", argv[1]);
        return 2;
    }
    fclose(f);
    unsigned mode = frame[WINDOW_BYTES] & 7u, blink_on = frame[WINDOW_BYTES + 1];

    memset(&oric, 0, sizeof oric);
    preinit_ula(&oric);
    if (!init_ula(&oric)) return 2;
    oric.mem = calloc(65536, 1);
    if (!oric.mem) return 2;
    memcpy(&oric.mem[WINDOW_BASE], frame, WINDOW_BYTES);
    oric.vidbases[0] = 0xa000;
    oric.vidbases[1] = 0x9800;
    oric.vidbases[2] = 0xbb80;
    oric.vidbases[3] = 0xb400;

    /* What ula_decode_attr does for a mode attribute; it is static. */
    oric.vid_mode = (int)mode;
    oric.vid_addr = (mode & 4) ? oric.vidbases[0] : oric.vidbases[2];
    oric.vid_ch_base = &oric.mem[(mode & 4) ? oric.vidbases[1] : oric.vidbases[3]];
    oric.vid_ch_data = oric.vid_ch_base;
    oric.frames = blink_on ? 0x10 : 0;

    /* 50 Hz timing, as its own wrap sets it; the raster starts on the
     * line before the first visible one, and stops before the wrap. */
    oric.vid_freq = (int)(mode & 2);
    oric.cyclesperraster = 64;
    oric.vid_maxrast = 312;
    oric.vid_start = (oric.vid_maxrast - 224) / 2;
    oric.vid_end = oric.vid_start + 224;
    oric.vid_raster = oric.vid_start - 1;
    ula_set_dirty(&oric);
    for (int y = 0; y < 224; y++) ula_doraster(&oric);

    f = fopen(argv[2], "wb");
    if (!f) return 2;
    fprintf(f, "P6\n240 224\n255\n");
    for (int i = 0; i < 240 * 224; i++) {
        unsigned c = oric.scr[i] & 7u;
        fputc(oricpalette[c * 3], f);
        fputc(oricpalette[c * 3 + 1], f);
        fputc(oricpalette[c * 3 + 2], f);
    }
    return fclose(f) == 0 ? 0 : 2;
}
