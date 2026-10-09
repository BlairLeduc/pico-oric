/* config.h — every fixed capacity in the emulator, in one place.
 *
 * Nothing in src/core/ allocates; every buffer is sized from a constant
 * here, so the SRAM budget (design.md §3.3) is a link-time fact.
 *
 * A guest fact becomes a #define only once it is settled (design.md §16).
 * The ones below are this project's own choices, rated high there, or
 * settled; the field's timing stays configuration (oric_config_t) until
 * M4 settles it.
 *
 * Guest addresses are written #XXXX in comments and 0x in code.
 */
#ifndef PICO_ORIC_CONFIG_H
#define PICO_ORIC_CONFIG_H

/* ---- Guest address space (design.md §2.2, §6.1) ---------------------- */

#define ORIC_ADDR_SPACE     65536u
#define ORIC_PAGE_SIZE        256u  /* page_t {read, write} per page      */
#define ORIC_PAGE_COUNT     (ORIC_ADDR_SPACE / ORIC_PAGE_SIZE)

/* BASIC 1.0 or 1.1, 16 KiB at #C000-#FFFF (§2.1, §10.2). */
#define ORIC_ROM_BASE      0xC000u
#define ORIC_ROM_SIZE       16384u

/* The Oric-1 16K's RAM, which repeats through #0000-#BFFF below the ROM
 * (§2.2, §6.2; §16: settled by executing both ROMs, M3). */
#define ORIC_RAM16_SIZE     16384u

/* Page #03 is I/O; the VIA's registers are A3-A0 (§2.2, §6.4). */
#define ORIC_IO_PAGE         0x03u

/* ---- Keyboard (design.md §2.3, §2.4) ---------------------------------- */

/* Rows from PB0-PB2 through a 1-of-8 decoder, columns from AY port A:
 * both ROMs' scans (#F4C8 in 1.0, #F523 in 1.1) walk eight of each. */
#define ORIC_KEY_ROWS           8u
#define ORIC_KEY_COLS           8u

/* The PicoCalc's events, replayed into the matrix (design.md §9.1). */
#define ORIC_KEY_EVENT_QUEUE   64u  /* southbridge FIFO holds 31 (HW §6.2) */
#define ORIC_KEY_HELD_MAX       8u  /* keys down at once                   */
#define ORIC_KEY_TEXT_EVENTS    4u  /* one ASCII byte: a modifier around a key */
#define ORIC_KEY_RING          ORIC_KEY_EVENT_QUEUE  /* core 1 to core 0 */

/* Both ROMs scan every third T1 interrupt, 30 ms, take a key on the
 * first scan that sees it, and see a key typed twice only after a scan
 * with it up, so it needs 2 fields down and 2 up; they repeat a key held
 * 48 fields (executed 2026-10-08, §16). The replay holds and gaps one
 * field longer: at 2 and 2, BASIC 1.0 loses keys that come while it
 * stores a line and scrolls (test_keyboard). */
#define ORIC_KEY_MIN_FIELDS     3u
#define ORIC_KEY_GAP_FIELDS     3u

/* ---- Video (design.md §2.5, §7) --------------------------------------- */

/* 240x224: 40 cells of 6 pixels a line, 28 text rows of 8 lines, the
 * hires bitmap's 200 lines above the last three (§2.1, §16: high). */
#define ORIC_SCREEN_COLS       40u
#define ORIC_SCREEN_ROWS       28u
#define ORIC_GLYPH_W            6u
#define ORIC_GLYPH_H            8u
#define ORIC_PIXEL_W  (ORIC_SCREEN_COLS * ORIC_GLYPH_W)   /* 240 */
#define ORIC_PIXEL_H  (ORIC_SCREEN_ROWS * ORIC_GLYPH_H)   /* 224 */
#define ORIC_HIRES_LINES      200u

/* Everything the ULA can fetch in either mode, #9800-#BFFF: both
 * character-set locations, the bitmap and the text screen (§4.4). On a
 * 16K machine the same window is #1800-#3FFF of its RAM (§2.2). */
#define ORIC_VIDEO_BASE    0x9800u
#define ORIC_VIDEO_BYTES    10240u
#define ORIC_TEXT_BASE     0xBB80u  /* 28 rows of 40                    */
#define ORIC_HIRES_BASE    0xA000u  /* 200 lines of 40                  */
#define ORIC_CHARSET_TEXT  0xB400u  /* standard; alternate 1 KiB above  */
#define ORIC_CHARSET_HIRES 0x9800u  /* the same pair, in hires mode     */
#define ORIC_CHARSET_BYTES   1024u  /* 128 glyphs of 8 rows             */

/* The presenter's dirty bands: one per text row, 8 lines (§7.3). */
#define ORIC_BAND_COUNT  ORIC_SCREEN_ROWS
#define ORIC_BAND_LINES  ORIC_GLYPH_H

/* The frame pool: three buffers, so core 0 never waits (§4.4). */
#define ORIC_SNAPSHOT_COUNT     3u

/* ---- The panel (design.md §7.5; hardware-notes.md §4) ---------------- */

#define ORIC_PANEL_W          320u
#define ORIC_PANEL_H          320u
#define ORIC_SCREEN_X          40u  /* the guest 1:1, centred across        */
#define ORIC_SCREEN_Y          48u  /* with a 48-row band above and below    */
#define ORIC_LINEBUF_COUNT      2u  /* DMA ping-pong (HW §4.6)              */
#define ORIC_LINEBUF_PIXELS  ORIC_PANEL_W

/* The perf line at the panel's top and the status line at its foot,
 * each a row of 6x8 glyphs across the panel, centred in its 48-row band
 * (§7.5). */
#define ORIC_TEXT_COLS  (ORIC_PANEL_W / ORIC_GLYPH_W)          /* 53 */
#define ORIC_TEXT_X     ((ORIC_PANEL_W - ORIC_TEXT_COLS * ORIC_GLYPH_W) / 2u)
#define ORIC_PERF_Y     ((ORIC_SCREEN_Y - ORIC_GLYPH_H) / 2u)  /* 20 */
#define ORIC_STATUS_Y   (ORIC_PANEL_H - ORIC_SCREEN_Y + ORIC_PERF_Y)  /* 292 */

/* ---- Port buffers (design.md §3.3) ------------------------------------ */

/* Core 0's log, drained by core 1 (EL §2.3): the ring, a power of two,
 * and the longest line formatted onto core 0's stack. */
#define ORIC_LOG_RING        2048u
#define ORIC_LOG_LINE         512u

/* ---- The card (design.md §10) ----------------------------------------- */

#define ORIC_PATH_MAX         128u  /* a path on the card, with its NUL    */
#define ORIC_ROM_DIR     "/oric/roms" /* §10.1                              */
#define ORIC_CARD_CHUNK       512u  /* a file read a sector at a time      */

/* The settings file (§10.7; EL §8.7), read whole, and its longest line:
 * "boot_tape = " and a path. */
#define ORIC_SETTINGS_FILE_MAX 2048u
#define ORIC_SETTINGS_LINE_MAX (ORIC_PATH_MAX + 32u)
#define ORIC_KEYMAP_NAME_LEN    16u  /* a layout's name (§9.4)              */

/* The longest name a .tap header may carry (§10.3). Both ROMs take 16
 * characters from CLOAD and CSAVE; 1.1 stores 16 of a longer name read
 * off tape and 1.0 all of it. Anything longer is taken as no header. */
#define ORIC_TAP_NAME_MAX       64u
#define ORIC_TAPE_LIST_MAX      64u  /* .tap files the Tapes page lists     */

/* ---- Timing (design.md §2.1, §11) ------------------------------------ */

#define ORIC_CPU_HZ       1000000u  /* 12 MHz crystal / 12 (§16: high)     */

/* ---- Audio (design.md §8; EL §6; hardware-notes.md §5) ---------------- */

#define ORIC_PWM_TOP          2047u  /* 11 bits, 73.2 kHz carrier          */
#define ORIC_PWM_OVERSAMPLE      2u  /* each frame written twice           */

/* The nominal sample rate as a fraction, 150,000,000 / 4,096 Hz, which
 * makes a sample 2,048/75 cycles (§8.4). The port recomputes it from
 * clock_get_hz(clk_sys) and hands the real one to oric_audio_set_rate;
 * this is what a host build runs at. */
#define ORIC_AUDIO_RATE_NUM  150000000u
#define ORIC_AUDIO_RATE_DEN  ((ORIC_PWM_TOP + 1u) * ORIC_PWM_OVERSAMPLE)

/* Samples the core holds between drains: a 50 Hz field is 731.25, a
 * 60 Hz one 618.75. The port drains after every field (§8.4). */
#define ORIC_AUDIO_BUF_LEN    1024u

#define ORIC_PCM_QUEUE_LEN    1024u  /* SPSC, ~28 ms (EL §6.2)             */
#define ORIC_PCM_QUEUE_START   768u  /* start streaming at this depth      */

/* Power-of-two AND aligned, with the hardware read wrap (HW §5.3). At
 * oversample 2 a frame is two slots; a half is 3.5 ms of sound. */
#define ORIC_DMA_FRAMES_PER_HALF 128u
#define ORIC_DMA_SLOTS_PER_HALF  (ORIC_DMA_FRAMES_PER_HALF * ORIC_PWM_OVERSAMPLE)
#define ORIC_DMA_RING_SLOTS      (ORIC_DMA_SLOTS_PER_HALF * 2u)

#endif /* PICO_ORIC_CONFIG_H */
