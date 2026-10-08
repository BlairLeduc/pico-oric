/* keymap_picocalc.c — PicoCalc key codes to Oric matrix cells (design.md §9.2).
 *
 * The cells are the ROMs', not a transcription (§9.3). Each of the 64
 * cells was pressed at the prompt in both ROMs, alone and with each
 * SHIFT, CTRL and FUNCT, and what the ROM decoded was read back
 * (2026-10-08); test_keyboard repeats that sweep and types every entry
 * below through both ROMs. The table is §2.4's.
 *
 *   row \ col  0      1      2      3      4        5       6      7
 *   0           7 &    N      5 %    V      -        1 !     X      3 #
 *   1           J      T      R      F      -        ESC     Q      D
 *   2           M      6 ^    B      4 $    CTRL     Z       2 @    C
 *   3           K      9 (    ; :    - _    -        -       \ |    ' "
 *   4           SPACE  , <    . >    UP     L.SHIFT  LEFT    DOWN   RIGHT
 *   5           U      I      O      P      FUNCT    DEL     ] }    [ {
 *   6           Y      H      G      E      -        A       S      W
 *   7           8 *    L      0 )    / ?    R.SHIFT  RETURN  -      = +
 *
 * A second character is what SHIFT gives; '-' is a cell with no key.
 * The Oric pairs its characters on keys as a US keyboard does, the
 * PicoCalc's included, so every character is its key's cell, with SHIFT
 * where the PicoCalc types it with Shift. Letters are capitals unshifted while
 * the ROM's CAPS is on, as it is after a reset; CTRL-T turns it off,
 * and then the PicoCalc's Shift gives capitals as on the Oric. '_' is
 * #5F, which the Oric's font draws as '£', and '^' is #5E, drawn as an
 * up arrow: mapped by code, which is what BASIC sees. '`' and '~' have
 * no key on the Oric, and no entry here.
 *
 * Code values are the southbridge firmware's (hardware-notes.md §6;
 * keyboard.h in the PicoCalc repository). Adapted from pico-ace (§4.6).
 */

#include "keymatrix.h"

/* Oric keys as "row, col". */
#define OK_7      0, 0
#define OK_N      0, 1
#define OK_5      0, 2
#define OK_V      0, 3
#define OK_1      0, 5
#define OK_X      0, 6
#define OK_3      0, 7
#define OK_J      1, 0
#define OK_T      1, 1
#define OK_R      1, 2
#define OK_F      1, 3
#define OK_ESC    1, 5
#define OK_Q      1, 6
#define OK_D      1, 7
#define OK_M      2, 0
#define OK_6      2, 1
#define OK_B      2, 2
#define OK_4      2, 3
#define OK_Z      2, 5
#define OK_2      2, 6
#define OK_C      2, 7
#define OK_K      3, 0
#define OK_9      3, 1
#define OK_SEMI   3, 2
#define OK_MINUS  3, 3
#define OK_BSLASH 3, 6
#define OK_QUOTE  3, 7
#define OK_SPACE  4, 0
#define OK_COMMA  4, 1
#define OK_DOT    4, 2
#define OK_UP     4, 3
#define OK_LEFT   4, 5
#define OK_DOWN   4, 6
#define OK_RIGHT  4, 7
#define OK_U      5, 0
#define OK_I      5, 1
#define OK_O      5, 2
#define OK_P      5, 3
#define OK_FUNCT  OK_ROW_FUNCT, OK_COL_MODS
#define OK_DEL    5, 5
#define OK_RBRACK 5, 6
#define OK_LBRACK 5, 7
#define OK_Y      6, 0
#define OK_H      6, 1
#define OK_G      6, 2
#define OK_E      6, 3
#define OK_A      6, 5
#define OK_S      6, 6
#define OK_W      6, 7
#define OK_8      7, 0
#define OK_L      7, 1
#define OK_0      7, 2
#define OK_SLASH  7, 3
#define OK_RETURN 7, 5
#define OK_EQUALS 7, 7

#define NOCELL 0, 0

/* A key and what Shift makes of it, on both keyboards. */
#define KEY(plain, shifted, cell) \
    { plain, cell, 0 }, { shifted, cell, KM_SHIFT }

const keymap_t keymap_picocalc[] = {
    KEY('a', 'A', OK_A), KEY('b', 'B', OK_B), KEY('c', 'C', OK_C),
    KEY('d', 'D', OK_D), KEY('e', 'E', OK_E), KEY('f', 'F', OK_F),
    KEY('g', 'G', OK_G), KEY('h', 'H', OK_H), KEY('i', 'I', OK_I),
    KEY('j', 'J', OK_J), KEY('k', 'K', OK_K), KEY('l', 'L', OK_L),
    KEY('m', 'M', OK_M), KEY('n', 'N', OK_N), KEY('o', 'O', OK_O),
    KEY('p', 'P', OK_P), KEY('q', 'Q', OK_Q), KEY('r', 'R', OK_R),
    KEY('s', 'S', OK_S), KEY('t', 'T', OK_T), KEY('u', 'U', OK_U),
    KEY('v', 'V', OK_V), KEY('w', 'W', OK_W), KEY('x', 'X', OK_X),
    KEY('y', 'Y', OK_Y), KEY('z', 'Z', OK_Z),

    KEY('1', '!', OK_1), KEY('2', '@', OK_2), KEY('3', '#', OK_3),
    KEY('4', '$', OK_4), KEY('5', '%', OK_5), KEY('6', '^', OK_6),
    KEY('7', '&', OK_7), KEY('8', '*', OK_8), KEY('9', '(', OK_9),
    KEY('0', ')', OK_0),

    KEY('-', '_', OK_MINUS),  KEY('=', '+', OK_EQUALS),
    KEY('[', '{', OK_LBRACK), KEY(']', '}', OK_RBRACK),
    KEY('\\', '|', OK_BSLASH), KEY(';', ':', OK_SEMI),
    KEY('\'', '"', OK_QUOTE), KEY(',', '<', OK_COMMA),
    KEY('.', '>', OK_DOT),    KEY('/', '?', OK_SLASH),
    { ' ', OK_SPACE, 0 },

    { PICOCALC_KEY_ENTER,     OK_RETURN, 0 },
    { PICOCALC_KEY_BACKSPACE, OK_DEL,    0 },
    { PICOCALC_KEY_DEL,       OK_DEL,    0 },
    { PICOCALC_KEY_ESC,       OK_ESC,    0 },

    /* The Oric's arrows are plain keys, so the host's swallowed
     * Shift+arrow chords cost nothing (hardware-notes.md §6.3). */
    { PICOCALC_KEY_LEFT,  OK_LEFT,  0 },
    { PICOCALC_KEY_UP,    OK_UP,    0 },
    { PICOCALC_KEY_DOWN,  OK_DOWN,  0 },
    { PICOCALC_KEY_RIGHT, OK_RIGHT, 0 },

    /* FUNCT is a held modifier on the Atmos, so a plain key, not an Alt
     * chord (EL §7.2). Neither ROM reads it (§16); programs may. */
    { PICOCALC_KEY_TAB, OK_FUNCT, 0 },

    /* The Alt layer (§9.2). The MCU skips its lower-casing when Alt is
     * down, so these arrive as capitals. Alt+, . Space and B never
     * arrive, and Alt+I is the MCU's Insert (hardware-notes.md §6.3). */
    { 'M', KM_PAGE_MAIN, 0, KM_ALT | KM_MENU },
    { 'H', KM_PAGE_HELP, 0, KM_ALT | KM_MENU },
    { 'P', NOCELL,          KM_ALT | KM_PAUSE },
    { 'K', NOCELL,          KM_ALT | KM_RESET },

    /* The function keys open the menu at a page (§12). The Oric has no
     * function keys, so they are the menu's everywhere. F6, Shift+F1,
     * takes a screenshot. */
    { PICOCALC_KEY_F1 + 0, KM_PAGE_TAPE,     0, KM_MENU },
    { PICOCALC_KEY_F1 + 1, KM_PAGE_DISC,     0, KM_MENU },
    { PICOCALC_KEY_F1 + 2, KM_PAGE_SNAPSHOT, 0, KM_MENU },
    { PICOCALC_KEY_F1 + 3, KM_PAGE_SETUP,    0, KM_MENU },
    { PICOCALC_KEY_F1 + 4, KM_PAGE_MACHINE,  0, KM_MENU },
    { PICOCALC_KEY_F6,     NOCELL,              KM_SHOT },
    { PICOCALC_KEY_F10,    KM_PAGE_ABOUT,    0, KM_MENU },
};

const size_t keymap_picocalc_len = sizeof keymap_picocalc / sizeof keymap_picocalc[0];

/* PicoCalc keys that exist only as another's shifted alternate
 * (hardware-notes.md §6.3), by the key they are on. */
#define PC_HOME      0xD2u
#define PC_END       0xD5u
#define PC_PAGE_UP   0xD6u
#define PC_PAGE_DOWN 0xD7u

uint8_t keymap_picocalc_canonical(uint8_t code) {
    if (code >= 'A' && code <= 'Z') return (uint8_t)(code + ('a' - 'A'));

    /* Each key's shifted alternate back to its base (keyboard.ino). */
    switch (code) {
    case '!': return '1';  case '@': return '2';  case '#': return '3';
    case '$': return '4';  case '%': return '5';  case '^': return '6';
    case '&': return '7';  case '*': return '8';  case '(': return '9';
    case ')': return '0';  case '_': return '-';  case '+': return '=';
    case '|': return '\\'; case '?': return '/';  case ':': return ';';
    case '"': return '\''; case '<': return ',';  case '>': return '.';
    case '{': return '[';  case '}': return ']';  case '~': return '`';
    case PC_END:             return PICOCALC_KEY_DEL;
    case PC_HOME:            return PICOCALC_KEY_TAB;
    case PICOCALC_KEY_BREAK: return PICOCALC_KEY_ESC;
    case PICOCALC_KEY_INSERT: return PICOCALC_KEY_ENTER;
    case PC_PAGE_UP:         return PICOCALC_KEY_UP;
    case PC_PAGE_DOWN:       return PICOCALC_KEY_DOWN;
    case PICOCALC_KEY_F10:   return PICOCALC_KEY_F1 + 4u;
    default:
        /* F6-F9 are Shift+F1-F4. */
        if (code >= PICOCALC_KEY_F6 && code <= PICOCALC_KEY_F1 + 8u)
            return (uint8_t)(code - 5u);
        return code;
    }
}

unsigned keymap_picocalc_text(uint8_t ch, picocalc_event_t out[ORIC_KEY_TEXT_EVENTS]) {
    uint8_t mod = 0, code;
    if (ch == '\r' || ch == '\n') {
        code = PICOCALC_KEY_ENTER;
    } else if (ch == 0x08u || ch == 0x7Fu) {
        code = PICOCALC_KEY_BACKSPACE;
    } else if (ch == 0x09u) {
        code = PICOCALC_KEY_TAB;
    } else if (ch == 0x1Bu) {
        code = PICOCALC_KEY_ESC;
    } else if (ch >= 0x01u && ch <= 0x1Au) {
        /* Ctrl passes the key through unchanged (hardware-notes.md §6.3). */
        mod = PICOCALC_KEY_CTRL;
        code = (uint8_t)('a' + ch - 1u);
    } else if (ch >= 0x20u && ch <= 0x7Eu) {
        code = ch;
        /* A code that is not its own key's base is a Shift chord. */
        if (keymap_picocalc_canonical(ch) != ch) mod = PICOCALC_KEY_SHIFT_L;
    } else if ((ch >= PICOCALC_KEY_F1 && ch <= PICOCALC_KEY_F1 + 8u) || ch == PICOCALC_KEY_F10) {
        /* The function keys as their own codes; F6-F10 are Shift+F1-F5. */
        code = ch;
        if (keymap_picocalc_canonical(ch) != ch) mod = PICOCALC_KEY_SHIFT_L;
    } else {
        return 0;
    }

    unsigned n = 0;
    if (mod) out[n++] = (picocalc_event_t){ KEY_EV_PRESSED, mod };
    out[n++] = (picocalc_event_t){ KEY_EV_PRESSED, code };
    out[n++] = (picocalc_event_t){ KEY_EV_RELEASED, code };
    if (mod) out[n++] = (picocalc_event_t){ KEY_EV_RELEASED, mod };
    return n;
}
