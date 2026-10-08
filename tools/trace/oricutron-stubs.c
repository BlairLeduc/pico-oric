/* oricutron-stubs.c — what Oricutron's main.c would have defined, for
 * the headless drivers that link every other object of it (design.md
 * §13.4): oricutron-trace and oricutron-render. Nothing of Oricutron is
 * in this tree; tools/trace/build-oricutron.sh builds against a copy of a
 * checkout.
 */

#include <stdarg.h>
#include <stdio.h>

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

SDL_bool need_sdl_quit = SDL_FALSE;
SDL_bool fullscreen;
Uint32 frametimeave;

void error_printf(char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}
const char *get_fileprefix(void) { return ""; }
SDL_bool read_config_string(char *buf, char *token, char *dest, Sint32 maxlen) {
    (void)buf; (void)token; (void)dest; (void)maxlen; return SDL_FALSE;
}
SDL_bool read_config_bool(char *buf, char *token, SDL_bool *dest) {
    (void)buf; (void)token; (void)dest; return SDL_FALSE;
}
SDL_bool read_config_option(char *buf, char *token, Sint32 *dest, char **options) {
    (void)buf; (void)token; (void)dest; (void)options; return SDL_FALSE;
}
SDL_bool read_config_int(char *buf, char *token, int *dest, int min, int max) {
    (void)buf; (void)token; (void)dest; (void)min; (void)max; return SDL_FALSE;
}
void shut(struct machine *oric) { (void)oric; }
