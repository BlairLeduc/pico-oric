/* card.c — the SD card's jobs and its slot (card.h, design.md §10.1). */

#include "card.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "pico/stdlib.h"

#include "config.h"
#include "ff.h"
#include "log.h"
#include "sd.h"
#include "sha1.h"
#include "storage.h"

/* Card detect must hold a new level this long before it counts: the
 * contacts bounce as a card goes in, and a card half in does not answer
 * (hardware-notes.md §7.1). */
#define CARD_SETTLE_US 250000u

static volatile bool s_present;   /* settled; core 0 reads it */
static bool     s_raw;            /* last read */
static uint32_t s_raw_since;
static volatile uint32_t s_changes;
static bool     s_polled;

static uint8_t s_chunk[ORIC_CARD_CHUNK];

/* Read one file whole, a sector at a time, into a digest. Returns
 * FatFs's result; *len is the bytes read. */
static FRESULT hash_file(const char *path, uint32_t *len, uint8_t digest[SHA1_DIGEST_LEN]) {
    FIL f;
    FRESULT fr = f_open(&f, path, FA_READ);
    if (fr != FR_OK) return fr;
    sha1_t s;
    sha1_init(&s);
    *len = 0;
    for (;;) {
        UINT got = 0;
        fr = f_read(&f, s_chunk, sizeof s_chunk, &got);
        if (fr != FR_OK || got == 0) break;
        sha1_update(&s, s_chunk, got);
        *len += got;
    }
    f_close(&f);
    sha1_final(&s, digest);
    return fr;
}

static void one_rom(card_job_t *j, const FILINFO *fi) {
    char path[ORIC_PATH_MAX];
    j->files++;
    if (snprintf(path, sizeof path, "%s/%s", ORIC_ROM_DIR, fi->fname) >= (int)sizeof path) {
        j->unknown++;
        log_core1("  rom          : %s: name too long\n", fi->fname);
        return;
    }

    uint8_t d[SHA1_DIGEST_LEN];
    uint32_t len = 0;
    uint32_t t0 = time_us_32();
    FRESULT fr = hash_file(path, &len, d);
    uint32_t us = time_us_32() - t0;
    if (fr != FR_OK) {
        j->unknown++;
        log_core1("  rom          : %s: unreadable (FatFs %d)\n", fi->fname, (int)fr);
        return;
    }
    if (len == 16384u && us > j->read16k_us) j->read16k_us = us;

    char hex[2 * SHA1_DIGEST_LEN + 1];
    for (unsigned i = 0; i < SHA1_DIGEST_LEN; i++) snprintf(hex + 2 * i, 3, "%02x", d[i]);

    rom_id_t id = romset_identify_digest(d, len);
    const char *what = "unrecognised";
    if (id == ROM_UNKNOWN) {
        j->unknown++;
        /* A file with an image's name and size stands for it while no
         * file is the image itself (§10.2). */
        for (int i = 0; i < ROM_IMAGE_COUNT; i++) {
            if (j->rom[i] != ROMFILE_ABSENT || len != romset_images[i].size ||
                strcasecmp(fi->fname, romset_images[i].file) != 0)
                continue;
            j->rom[i] = ROMFILE_NAMED;
            memcpy(j->path[i], path, sizeof path);
        }
    } else {
        /* The image itself wins over a file that only has its name. */
        if (j->rom[id] != ROMFILE_KNOWN) {
            j->rom[id] = ROMFILE_KNOWN;
            memcpy(j->path[id], path, sizeof path);
        }
        what = romset_images[id].file;
    }
    log_core1("  rom          : %s, %lu bytes, sha1 %s, %s%s, %lu us\n", fi->fname,
              (unsigned long)len, hex, id == ROM_UNKNOWN ? "" : "recognised as ", what,
              (unsigned long)us);
}

rom_id_t card_other_basic(rom_id_t id) {
    return id == ROM_BASIC10 ? ROM_BASIC11 : ROM_BASIC10;
}

/* Read the file the listing found for `id` into image: false unless it
 * is still exactly a ROM's size. */
static bool load_rom(card_job_t *j, rom_id_t id, uint8_t image[ORIC_ROM_SIZE]) {
    uint32_t t0 = time_us_32();
    FIL f;
    if (f_open(&f, j->path[id], FA_READ) != FR_OK) return false;
    UINT got = 0;
    FRESULT fr = f_read(&f, image, ORIC_ROM_SIZE, &got);
    bool whole = fr == FR_OK && got == ORIC_ROM_SIZE && f_size(&f) == ORIC_ROM_SIZE;
    f_close(&f);
    if (!whole) return false;
    j->loaded = id;
    j->loaded_known = romset_identify(image, ORIC_ROM_SIZE) == id;
    j->load_us = time_us_32() - t0;
    return true;
}

void card_roms(card_job_t *j, rom_id_t want, uint8_t image[ORIC_ROM_SIZE]) {
    memset(j, 0, sizeof *j);
    j->loaded = ROM_UNKNOWN;
    if (!sd_present()) {
        j->state = CARD_NONE;
        log_core1("  card         : no card\n");
        return;
    }

    uint32_t t0 = time_us_32();
    j->fresult = storage_mount();
    j->mount_us = time_us_32() - t0;
    if (j->fresult != 0) {
        j->state = CARD_UNUSABLE;
        log_core1("  card         : no FAT volume (FatFs %d) after %lu us\n", j->fresult,
                  (unsigned long)j->mount_us);
        return;
    }
    j->state = CARD_MOUNTED;
    log_core1("  card         : mounted in %lu us\n", (unsigned long)j->mount_us);

    DIR dir;
    FRESULT fr = f_opendir(&dir, ORIC_ROM_DIR);
    if (fr != FR_OK) {
        log_core1("  card         : no %s (FatFs %d)\n", ORIC_ROM_DIR, (int)fr);
        storage_unmount();
        return;
    }
    j->dir = true;
    FILINFO fi;
    while (f_readdir(&dir, &fi) == FR_OK && fi.fname[0]) {
        /* macOS's AppleDouble files are not the user's (hardware-notes.md
         * §7.1), and a directory is not a ROM. */
        if (fi.fattrib & AM_DIR) continue;
        if (fi.fname[0] == '.' && fi.fname[1] == '_') continue;
        one_rom(j, &fi);
    }
    f_closedir(&dir);

    log_core1("  card         : %s has %u files, %u unrecognised; missing:%s%s%s\n",
              ORIC_ROM_DIR, j->files, j->unknown,
              j->rom[ROM_BASIC10] == ROMFILE_KNOWN ? "" : " basic10.rom",
              j->rom[ROM_BASIC11] == ROMFILE_KNOWN ? "" : " basic11b.rom",
              j->rom[ROM_MICRODISC] == ROMFILE_KNOWN ? "" : " microdis.rom");

    /* The machine's ROM, or failing it the other machine's (§10.2). */
    if (image) {
        rom_id_t pick[2] = { want, card_other_basic(want) };
        for (unsigned i = 0; i < 2 && j->loaded == ROM_UNKNOWN; i++) {
            if (j->rom[pick[i]] == ROMFILE_ABSENT) continue;
            if (!load_rom(j, pick[i], image)) {
                log_core1("  rom          : %s: could not be read whole\n", j->path[pick[i]]);
                continue;
            }
            log_core1("  rom          : %s loaded as %s%s, %lu us\n", j->path[pick[i]],
                      romset_images[pick[i]].file,
                      j->loaded_known ? "" : ", UNRECOGNISED: a near-miss boots and then "
                                             "misbehaves (design.md §10.2)",
                      (unsigned long)j->load_us);
        }
    }
    storage_unmount();
}

bool card_poll(void) {
    uint32_t now = time_us_32();
    if (!s_polled) {
        s_polled = true;
        s_present = s_raw = sd_present();
        s_raw_since = now;
        return false;
    }
    bool raw = sd_present();
    if (raw != s_raw) {
        s_raw = raw;
        s_raw_since = now;
        return false;
    }
    if (raw == s_present || now - s_raw_since < CARD_SETTLE_US) return false;
    s_present = raw;
    s_changes++;
    return true;
}

bool card_present(void) {
    return s_present;
}

uint32_t card_changes(void) {
    return s_changes;
}

const char *card_state_str(card_state_t st) {
    switch (st) {
    case CARD_NONE:     return "none";
    case CARD_UNUSABLE: return "unusable";
    case CARD_MOUNTED:  return "mounted";
    }
    return "?";
}
