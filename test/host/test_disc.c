/* test_disc.c — the Microdisc (design.md §10.5, §15.2 M14).
 *
 * Three parts. The first two need no ROM, so CI runs them:
 *
 *   mfm       the image format: the CRC against its known answer, the
 *             header's refusals, a formatted track indexed
 *   wd1793    every command against an in-memory disc, timed by the
 *             turning disc to the cycle, a CPU polling DRQ at a chosen
 *             pace; each timing has a control that must fail (wd1793.h)
 *   machine   the latch's memory map, the decode of page #03, INTRQ onto
 *             IRQ, the reset vector, and snapshots, on a machine with
 *             made-up ROM and EPROM images
 *
 * The third needs basic10.rom, basic11b.rom, microdis.rom and the
 * Sedoric 3.006 distribution disc, by SHA-1, in roms/ (§13.3), and
 * skips without them, which is not a pass:
 *
 *   sedoric   the disc boots on the Oric-1 48K and the Atmos 48K; SAVE,
 *             DIR, LOAD, RUN and DEL; a write-protected disc refuses
 *             SAVE; a disc swapped between fields is seen; a snapshot is
 *             refused mid-command and taken between; INIT formats a blank
 *             image in drive B, which then holds a file
 *
 *   test_disc --write DIR    also writes the disc with the saved program
 *                            as DIR/saved-<rom>.dsk and the INITed one as
 *                            DIR/init.dsk, for tools/trace-diff.py disc
 */

#include <dirent.h>
#include <string.h>

#include "bus.h"
#include "disc_util.h"
#include "guest.h"
#include "microdisc.h"
#include "sha1.h"
#include "snap_util.h"
#include "test_util.h"

#define L ORIC_DISC_TRACK_LEN

static const char *s_write_dir;

/* ---- mfm --------------------------------------------------------------- */

static int test_mfm(void) {
    /* CRC-16/CCITT-FALSE's check value. */
    CHECK(mfm_crc_block(0xFFFFu, (const uint8_t *)"123456789", 9) == 0x29B1u, "crc %04X",
          mfm_crc_block(0xFFFFu, (const uint8_t *)"123456789", 9));

    static uint8_t trk[L];
    mfm_format_track(trk, 7, 1, 17, 0xE5);
    mfm_sector_t sec[ORIC_DISC_SECTORS_MAX];
    unsigned n = mfm_index(trk, sec, ORIC_DISC_SECTORS_MAX);
    CHECK(n == 17, "%u sectors", n);
    for (unsigned i = 0; i < n; i++) {
        const mfm_sector_t *s = &sec[i];
        CHECK(s->id == 108u + i * 358u + 3u && s->data == s->id + 44u, "sector %u at %u, %u", i,
              s->id, s->data);
        CHECK(s->c == 7 && s->h == 1 && s->r == i + 1u && s->n == 1, "sector %u's ID", i);
        uint16_t c = mfm_crc_block(0xFFFFu, &trk[s->id - 3u], 8);
        CHECK(c == (trk[s->id + 5u] << 8 | trk[s->id + 6u]), "sector %u's ID CRC", i);
        c = mfm_crc_block(0xFFFFu, &trk[s->data - 3u], 4u + 256u);
        CHECK(c == (trk[s->data + 257u] << 8 | trk[s->data + 258u]), "sector %u's data CRC", i);
    }
    /* Data that looks like a mark is stepped over, not taken for an ID. */
    memcpy(&trk[sec[0].data + 10u], "\xA1\xA1\xA1\xFE\x07\x01\x63\x01", 8);
    CHECK(mfm_index(trk, sec, ORIC_DISC_SECTORS_MAX) == 17, "a mark in the data");

    uint8_t hdr[ORIC_DISC_HEADER_LEN];
    mfm_geom_t g = { 2, 42 }, got;
    mfm_header(hdr, g);
    uint32_t size = mfm_track_offset(g, 1, 42);
    CHECK(size == 256u + 84u * 6400u, "size %u", size);
    CHECK(mfm_parse(hdr, sizeof hdr, size, &got) == NULL && got.sides == 2 && got.tracks == 42,
          "a good header");
    CHECK(mfm_parse(hdr, sizeof hdr, size - 1u, &got) != NULL, "cut short");
    hdr[16] = 2;
    CHECK(mfm_parse(hdr, sizeof hdr, size, &got) != NULL, "geometry 2");
    hdr[16] = 1;
    hdr[8] = 3;
    CHECK(mfm_parse(hdr, sizeof hdr, size, &got) != NULL, "three sides");
    memcpy(hdr, "ORICDISK", 8);
    const char *why = mfm_parse(hdr, sizeof hdr, size, &got);
    CHECK(why && strstr(why, "ORICDISK"), "ORICDISK: %s", why ? why : "accepted");
    return 0;
}

/* ---- wd1793 -------------------------------------------------------------- */

static wd1793_t s_f;
static host_disc_t s_disc[2];
static uint64_t t, s_intrq_at, s_serve_at;

/* Every data byte of the disc distinct, so a read that takes the wrong
 * sector or the wrong byte cannot pass; the CRCs made good again. */
static void pattern(host_disc_t *d) {
    for (unsigned s = 0; s < d->g.sides; s++) {
        for (unsigned c = 0; c < d->g.tracks; c++) {
            uint8_t *trk = d->img + mfm_track_offset(d->g, s, c);
            mfm_sector_t sec[ORIC_DISC_SECTORS_MAX];
            unsigned n = mfm_index(trk, sec, ORIC_DISC_SECTORS_MAX);
            for (unsigned i = 0; i < n; i++) {
                uint8_t *p = &trk[sec[i].data + 1u];
                for (unsigned b = 0; b < 256; b++)
                    p[b] = (uint8_t)(c * 7u + s * 101u + sec[i].r * 31u + b);
                uint16_t crc = mfm_crc_block(0xFFFFu, p - 4, 260);
                p[256] = (uint8_t)(crc >> 8);
                p[257] = (uint8_t)crc;
            }
        }
    }
}

static uint8_t expect_byte(unsigned c, unsigned s, unsigned r, unsigned b) {
    return (uint8_t)(c * 7u + s * 101u + r * 31u + b);
}

static void serve(void) {
    const wd_req_t *r = wd_request(&s_f);
    if (!r || t < s_serve_at) return;
    bool ok = true;
    if (r->put) {
        host_disc_t *d = &s_disc[r->put_drive];
        memcpy(d->img + mfm_track_offset(d->g, r->put_side, r->put_cyl), s_f.buf, L);
        d->puts++;
    }
    if (r->get) {
        host_disc_t *d = &s_disc[r->drive];
        if (d->img) {
            memcpy(s_f.buf, d->img + mfm_track_offset(d->g, r->side, r->cyl), L);
            d->gets++;
        } else {
            ok = false;
        }
    }
    wd_served(&s_f, ok, t);
}

/* Run the chip's events up to `to`, each at its own cycle, noting when
 * INTRQ rises. */
static void step_to(uint64_t to) {
    while (s_f.due <= to) {
        bool was = s_f.intrq;
        uint64_t e = s_f.due;
        wd_run(&s_f, e);
        if (!was && s_f.intrq) s_intrq_at = e;
    }
}

/* A CPU polling DRQ every `poll` cycles, as the EPROM's loops do (#E2EF:
 * 21 cycles a byte), moving bytes to dst or from src, until INTRQ or
 * `limit`; with neither, a CPU that never answers. Requests are served
 * as soon as asked, or from s_serve_at. Returns the bytes moved. */
static size_t cpu(unsigned poll, uint8_t *dst, const uint8_t *src, size_t n, uint64_t limit) {
    size_t moved = 0;
    while (!s_f.intrq && t < limit) {
        serve();
        t += poll;
        step_to(t);
        if (s_f.intrq || !s_f.drq || (!dst && !src)) continue;
        if (dst) {
            uint8_t v = wd_read(&s_f, 3, t);
            if (moved < n) dst[moved] = v;
        } else {
            wd_write(&s_f, 3, moved < n ? src[moved] : 0x4Eu, t);
        }
        moved++;
    }
    step_to(t);
    return moved;
}

static void command(uint8_t v) {
    s_intrq_at = 0;
    wd_write(&s_f, 0, v, t);
    step_to(t);
}

static uint64_t kat(uint64_t when) { return (when + 31u) / 32u; }
static uint64_t next_k(uint64_t k0, unsigned pos) { return k0 + (pos + L - k0 % L) % L; }
/* Our formatter's sector r: its ID's sync (mfm_format_track). */
static unsigned sync_of(unsigned r) { return 108u + (r - 1u) * 358u; }

static void wd_fresh(void) {
    wd_init(&s_f);
    for (unsigned d = 0; d < 2; d++) {
        host_disc_free(&s_disc[d]);
        host_disc_blank(&s_disc[d], (mfm_geom_t){ 2, 42 }, 17);
        pattern(&s_disc[d]);
        wd_insert(&s_f, d, s_disc[d].g, false, 0);
    }
    t = 1000;
    s_serve_at = 0;
}

static bool read_sector_ok(const uint8_t *buf, size_t n, unsigned c, unsigned s, unsigned r) {
    if (n < 256) return false;
    for (unsigned b = 0; b < 256; b++)
        if (buf[b] != expect_byte(c, s, r, b)) return false;
    return true;
}

static int test_wd1793(void) {
    static uint8_t buf[2 * L];
    size_t n;

    /* Restore from track 5 at 6 ms a step: five steps, then INTRQ. The
     * control is the slowest rate, which must not finish then. */
    wd_fresh();
    s_f.drv[0].cyl = 5;
    s_f.track = 5;
    uint64_t t0 = t;
    command(0x00);
    cpu(10, NULL, NULL, 0, t + 1000000);
    CHECK(s_intrq_at == t0 + 5u * 6000u, "restore at %llu, want %llu",
          (unsigned long long)(s_intrq_at - t0), 30000ull);
    CHECK(s_f.drv[0].cyl == 0 && s_f.track == 0, "restore: head %u track %u", s_f.drv[0].cyl,
          s_f.track);
    CHECK(wd_read(&s_f, 0, t) & WD_ST_TRACK0, "restore: TRACK 0 bit");
    s_f.drv[0].cyl = 5;
    t0 = t;
    command(0x03);
    cpu(10, NULL, NULL, 0, t + 1000000);
    CHECK(s_intrq_at == t0 + 5u * 30000u && s_intrq_at != t0 + 5u * 6000u, "control: 30 ms a step");

    /* Seek with verify to 10: the steps, the 30 ms settle, then the first
     * ID to come round, which is the first sector's after the index. */
    wd_fresh();
    t0 = t;
    s_f.data = 10;
    command(0x14);
    cpu(10, NULL, NULL, 0, t + 2000000);
    uint64_t settled = t0 + 10u * 6000u + 30000u;
    uint64_t ks = UINT64_MAX;
    for (unsigned r = 1; r <= 17; r++) {
        uint64_t k = next_k(kat(settled), sync_of(r));
        if (k < ks) ks = k;
    }
    CHECK(s_intrq_at == (ks + 10u) * 32u, "verify at %llu, want %llu",
          (unsigned long long)s_intrq_at, (unsigned long long)((ks + 10u) * 32u));
    CHECK(s_f.drv[0].cyl == 10 && !(wd_read(&s_f, 0, t) & WD_ST_SEEK_ERR), "seek 10");
    /* The control: a track register that lies. The head goes to 15, and
     * no ID there says 5, so SEEK ERROR at the fifth index. */
    s_f.track = 0;
    s_f.data = 5;
    t0 = t;
    command(0x14);
    cpu(10, NULL, NULL, 0, t + 3000000);
    settled = t0 + 5u * 6000u + 30000u;
    CHECK(s_f.drv[0].cyl == 15 && (wd_read(&s_f, 0, t) & WD_ST_SEEK_ERR), "seek error");
    CHECK(s_intrq_at == (next_k(kat(settled), 0) + 4u * L) * 32u, "seek error at %llu",
          (unsigned long long)s_intrq_at);

    /* Read sector 5 at the EPROM's pace: every byte, no LOST DATA, and
     * INTRQ as the field's CRC passes. */
    wd_fresh();
    s_f.sector = 5;
    t0 = t;
    command(0x80);
    n = cpu(21, buf, NULL, 256, t + 1000000);
    ks = next_k(kat(t0), sync_of(5));
    CHECK(n == 256 && read_sector_ok(buf, n, 0, 0, 5), "read: %zu bytes", n);
    CHECK(!(wd_read(&s_f, 0, t) & (WD_ST_LOST | WD_ST_RNF)), "read: status");
    CHECK(s_intrq_at == (ks + 306u) * 32u, "read ends at %llu, want %llu",
          (unsigned long long)s_intrq_at, (unsigned long long)((ks + 306u) * 32u));
    CHECK(s_disc[0].gets == 1, "read: %u tracks fetched", s_disc[0].gets);
    /* The control: a CPU slower than the disc loses bytes. */
    t0 = t;
    command(0x80);
    n = cpu(40, buf, NULL, 256, t + 1000000);
    CHECK(n < 256 && (wd_read(&s_f, 0, t) & WD_ST_LOST), "control: slow CPU, %zu bytes", n);

    /* E: the 30 ms settle before the search. */
    t0 = t;
    command(0x84);
    cpu(21, buf, NULL, 256, t + 1000000);
    ks = next_k(kat(t0 + 30000u), sync_of(5));
    CHECK(s_intrq_at == (ks + 306u) * 32u, "read with E");

    /* No such sector: RNF at the fifth index pulse. */
    s_f.sector = 18;
    t0 = t;
    command(0x80);
    cpu(21, buf, NULL, 256, t + 2000000);
    CHECK((wd_read(&s_f, 0, t) & WD_ST_RNF) &&
          s_intrq_at == (next_k(kat(t0), 0) + 4u * L) * 32u, "RNF at %llu",
          (unsigned long long)s_intrq_at);
    /* Side compare (C, S = 1) on side 0's IDs: not there either. */
    s_f.sector = 1;
    command(0x8A);
    cpu(21, buf, NULL, 256, t + 2000000);
    CHECK(wd_read(&s_f, 0, t) & WD_ST_RNF, "side compare");

    /* Multiple: 15, 16 and 17 back to back, then RNF for 18. */
    s_f.sector = 15;
    command(0x90);
    n = cpu(21, buf, NULL, sizeof buf, t + 3000000);
    CHECK(n == 768 && read_sector_ok(buf, 256, 0, 0, 15) && read_sector_ok(buf + 512, 256, 0, 0, 17),
          "multiple: %zu bytes", n);
    CHECK((wd_read(&s_f, 0, t) & WD_ST_RNF) && s_f.sector == 18, "multiple ends at sector %u",
          s_f.sector);

    /* Read address: the next ID, C H R N and its CRC; C into the sector
     * register; INTRQ as the ID passes. */
    t0 = t;
    command(0xC0);
    n = cpu(21, buf, NULL, 6, t + 1000000);
    ks = UINT64_MAX;
    unsigned want_r = 0;
    for (unsigned r = 1; r <= 17; r++) {
        uint64_t k = next_k(kat(t0), sync_of(r));
        if (k < ks) ks = k, want_r = r;
    }
    CHECK(n == 6 && buf[0] == 0 && buf[1] == 0 && buf[2] == want_r && buf[3] == 1,
          "read address: %zu bytes, sector %u, want %u", n, buf[2], want_r);
    /* The last byte gets its byte's time before INTRQ (wd1793.h). */
    CHECK(s_f.sector == 0 && s_intrq_at == (ks + 11u) * 32u, "read address's end");

    /* Read track: from the index, the whole track as it lies. */
    t0 = t;
    command(0xE0);
    n = cpu(21, buf, NULL, sizeof buf, t + 1000000);
    CHECK(n == L && memcmp(buf, s_disc[0].img + 256, L) == 0, "read track: %zu bytes", n);
    CHECK(s_intrq_at == (next_k(kat(t0), 0) + L + 1u) * 32u, "read track ends past the index");

    /* Write sector 3: the bytes, a CRC good again, the track posted back
     * once the command ends; deleted mark (a0), read back as RECTYPE. */
    static uint8_t data[256];
    for (unsigned b = 0; b < 256; b++) data[b] = (uint8_t)(255u - b);
    s_f.sector = 3;
    command(0xA1);
    n = cpu(21, NULL, data, 256, t + 1000000);
    CHECK(n == 256 && !(wd_read(&s_f, 0, t) & (WD_ST_LOST | WD_ST_RNF)), "write: %zu", n);
    CHECK(wd_request(&s_f) && wd_request(&s_f)->put && !wd_request(&s_f)->get, "write posted");
    serve();
    CHECK(s_disc[0].puts == 1, "write: %u tracks put", s_disc[0].puts);
    uint8_t *p = s_disc[0].img + 256 + sync_of(3) + 48u;
    CHECK(memcmp(p, data, 256) == 0 && p[-1] == 0xF8, "write: on the disc");
    uint16_t crc = mfm_crc_block(0xFFFFu, p - 4, 260);
    CHECK(crc == (p[256] << 8 | p[257]), "write: the CRC");
    command(0x80);
    n = cpu(21, buf, NULL, 256, t + 1000000);
    CHECK(n == 256 && memcmp(buf, data, 256) == 0 && (wd_read(&s_f, 0, t) & WD_ST_RECTYPE),
          "write: read back");
    /* The control: a CPU that writes nothing loses the sector before its
     * mark, which stays as it was. */
    s_f.sector = 4;
    uint8_t before[300];
    memcpy(before, s_disc[0].img + 256 + sync_of(4), sizeof before);
    command(0xA0);
    cpu(21, NULL, NULL, 0, t + 1000000);   /* src NULL and dst NULL: never writes */
    CHECK(wd_read(&s_f, 0, t) & WD_ST_LOST, "control: no data, LOST");
    serve();
    CHECK(memcmp(before, s_disc[0].img + 256 + sync_of(4), sizeof before) == 0,
          "control: the sector untouched");

    /* Write-protected: refused at once, nothing posted. */
    wd_fresh();
    s_f.drv[1].protect = true;
    wd_select(&s_f, 1, 0, t);
    s_f.sector = 1;
    t0 = t;
    command(0xA0);
    CHECK(s_f.intrq && s_intrq_at == t0 && (wd_read(&s_f, 0, t) & WD_ST_PROTECT) &&
          !wd_request(&s_f), "write protect");

    /* Another track: a read there fetches it; a written one goes back
     * first. Served late, the search starts from when it was served. */
    wd_fresh();
    s_f.sector = 1;
    command(0xA0);
    cpu(21, NULL, data, 256, t + 1000000);
    s_f.data = 1;
    s_serve_at = UINT64_MAX;
    command(0x10);
    cpu(10, NULL, NULL, 0, t + 1000000);
    s_serve_at = t + 50000u;
    const wd_req_t *rq;
    command(0x80);
    rq = wd_request(&s_f);
    CHECK(rq && rq->put && rq->put_cyl == 0 && rq->get && rq->cyl == 1, "put then get");
    cpu(21, buf, NULL, 256, t + 1000000);
    ks = next_k(kat(s_serve_at), sync_of(1));
    CHECK(read_sector_ok(buf, 256, 1, 0, 1) && s_intrq_at == (ks + 306u) * 32u,
          "served late: at %llu", (unsigned long long)s_intrq_at);
    CHECK(memcmp(s_disc[0].img + 256 + sync_of(1) + 48u, data, 256) == 0, "put back");
    s_serve_at = 0;

    /* Write track: a Sedoric-like format of track 1, side 1, read back
     * by the index as 17 sectors with good CRCs. */
    static uint8_t fmt[8000];
    size_t k = 0;
#define B(v, cnt) do { for (unsigned q_ = 0; q_ < (cnt); q_++) fmt[k++] = (uint8_t)(v); } while (0)
    B(0x4E, 40); B(0x00, 12); B(0xF6, 3); B(0xFC, 1); B(0x4E, 40);
    for (unsigned r = 1; r <= 17; r++) {
        B(0x00, 12); B(0xF5, 3); B(0xFE, 1); B(1, 1); B(1, 1); B(r, 1); B(1, 1); B(0xF7, 1);
        B(0x4E, 22); B(0x00, 12); B(0xF5, 3); B(0xFB, 1); B(0x6C, 256); B(0xF7, 1);
        B(0x4E, 40);
    }
#undef B
    wd_select(&s_f, 0, 1, t);
    t0 = t;
    command(0xF0);
    n = cpu(21, NULL, fmt, k, t + 1000000);
    CHECK(!(wd_read(&s_f, 0, t) & WD_ST_LOST), "write track: no LOST DATA");
    CHECK(s_intrq_at == (next_k(kat(t0), 0) + L) * 32u, "write track ends at the index: %llu, want %llu",
          (unsigned long long)s_intrq_at, (unsigned long long)((next_k(kat(t0), 0) + L) * 32u));
    serve();
    const uint8_t *trk = s_disc[0].img + mfm_track_offset(s_disc[0].g, 1, 1);
    mfm_sector_t sec[ORIC_DISC_SECTORS_MAX];
    unsigned ns = mfm_index(trk, sec, ORIC_DISC_SECTORS_MAX);
    CHECK(ns == 17, "write track: %u sectors", ns);
    for (unsigned i = 0; i < ns; i++) {
        uint16_t c = mfm_crc_block(0xFFFFu, &trk[sec[i].id - 3u], 8);
        CHECK(c == (trk[sec[i].id + 5u] << 8 | trk[sec[i].id + 6u]) && sec[i].r == i + 1u,
              "write track: ID %u", i);
        c = mfm_crc_block(0xFFFFu, &trk[sec[i].data - 3u], 260);
        CHECK(c == (trk[sec[i].data + 257u] << 8 | trk[sec[i].data + 258u]),
              "write track: data %u", i);
    }

    /* Force Interrupt: #D0 ends a read with no INTRQ; #D8 raises it, and
     * a status read leaves it; #D4 raises it at every index. */
    wd_fresh();
    s_f.sector = 9;
    command(0x80);
    cpu(21, buf, NULL, 20, t + 300000);    /* part of the way: INTRQ never comes */
    command(0xD0);
    CHECK(!wd_busy(&s_f) && !s_f.intrq, "#D0");
    command(0xD8);
    (void)wd_read(&s_f, 0, t);
    CHECK(s_f.intrq, "#D8 holds INTRQ through a status read");
    command(0xD0);
    CHECK(!s_f.intrq, "#D0 clears it");
    command(0xD4);
    uint64_t idx = next_k(kat(t), 0) * 32u;
    cpu(10, NULL, NULL, 0, t + 400000);
    CHECK(s_intrq_at == idx, "#D4 at the index: %llu, want %llu",
          (unsigned long long)s_intrq_at, (unsigned long long)idx);
    (void)wd_read(&s_f, 0, t);
    s_intrq_at = 0;
    cpu(10, NULL, NULL, 0, t + 400000);
    CHECK(s_intrq_at == idx + WD_REV_CYCLES, "#D4 again a revolution on");
    command(0xD0);

    /* No disc: no index, nothing comes round; busy until #D0. */
    wd_eject(&s_f, 0, t);
    command(0x80);
    cpu(21, buf, NULL, 256, t + 3000000);
    CHECK(wd_busy(&s_f) && !s_f.intrq && s_f.due == WD_NEVER, "no disc: busy");
    command(0xD0);
    CHECK(!wd_busy(&s_f), "no disc: #D0 ends it");

    /* Beyond the image: unformatted, nothing asked of the port. */
    wd_fresh();
    s_f.data = 50;
    command(0x10);
    cpu(10, NULL, NULL, 0, t + 1000000);
    s_f.sector = 1;
    command(0x80);
    CHECK(!wd_request(&s_f), "track 50: no request");
    cpu(21, buf, NULL, 256, t + 2000000);
    CHECK(wd_read(&s_f, 0, t) & WD_ST_RNF, "track 50: RNF");
    return 0;
}

/* ---- the machine ------------------------------------------------------- */

static oric_t s_m, s_m2;
static uint8_t s_rom[ORIC_ROM_SIZE], s_eprom[ORIC_EPROM_SIZE];

static void machine(oric_t *m, bool md) {
    oric_config_t cfg;
    oric_config_default(&cfg);
    cfg.microdisc = md;
    oric_init(m, &cfg);
    oric_load_rom(m, s_rom, sizeof s_rom);
    oric_load_eprom(m, s_eprom, sizeof s_eprom);
    oric_power_on(m);
}

static int test_machine(void) {
    for (unsigned i = 0; i < sizeof s_rom; i++) s_rom[i] = 0xB0;
    for (unsigned i = 0; i < sizeof s_eprom; i++) s_eprom[i] = 0xE0;
    /* Each its own reset vector, and an endless loop there. */
    s_rom[0x3FFC] = 0x00; s_rom[0x3FFD] = 0xC0; s_rom[0] = 0x4C; s_rom[1] = 0x00; s_rom[2] = 0xC0;
    s_eprom[0x1FFC] = 0x00; s_eprom[0x1FFD] = 0xE0;
    s_eprom[0] = 0x4C; s_eprom[1] = 0x00; s_eprom[2] = 0xE0;

    /* Without the Microdisc: the ROM, as ever. */
    machine(&s_m, false);
    CHECK(s_m.cpu.pc == 0xC000 && oric_peek(&s_m, 0xE010) == 0xB0, "no Microdisc: PC %04X",
          s_m.cpu.pc);

    /* With it, RESET clears the latch: the EPROM's vector, RAM below it. */
    machine(&s_m, true);
    CHECK(s_m.cpu.pc == 0xE000, "Microdisc: PC %04X", s_m.cpu.pc);
    CHECK(oric_peek(&s_m, 0xC000) == 0x00 && oric_peek(&s_m, 0xC080) == 0xFF,
          "overlay RAM in Oricutron's power-on pattern: %02X %02X", oric_peek(&s_m, 0xC000),
          oric_peek(&s_m, 0xC080));
    CHECK(oric_peek(&s_m, 0x8000) == 0, "RAM below zero-filled");
    bus_write(&s_m, 0xC010, 0x11);
    bus_write(&s_m, 0xE010, 0x22);
    CHECK(oric_peek(&s_m, 0xC010) == 0x11 && oric_peek(&s_m, 0xE010) == 0xE0 &&
          s_m.ram[0xE010] == 0x00, "EPROM in: its writes lost");
    /* #84: the EPROM out, all RAM. */
    bus_write(&s_m, 0x0314, 0x84);
    bus_write(&s_m, 0xE010, 0x33);
    CHECK(oric_peek(&s_m, 0xE010) == 0x33 && oric_peek(&s_m, 0xC010) == 0x11, "all RAM");
    /* #86: the BASIC ROM, whose writes reach nothing. */
    bus_write(&s_m, 0x0314, 0x86);
    bus_write(&s_m, 0xC010, 0x44);
    CHECK(oric_peek(&s_m, 0xC010) == 0xB0 && s_m.ram[0xC010] == 0x11, "BASIC ROM in");
    /* #02 with bit 7 clear: the ROM still wins (MAME, Oricutron). */
    bus_write(&s_m, 0x0314, 0x02);
    CHECK(oric_peek(&s_m, 0xE010) == 0xB0, "ROM over EPROM");
    oric_t copy;
    oric_copy(&copy, &s_m);
    bus_write(&s_m, 0x0314, 0x00);
    oric_copy(&copy, &s_m);
    CHECK(copy.page[0xE0].read == copy.eprom && copy.page[0xC0].read == &copy.ram[0xC000],
          "oric_copy moves the EPROM's pages");

    /* Page #03: #0310-#0313 the controller, #0314 and #0318 the lines,
     * #0315 and #031F the VIA's. */
    bus_write(&s_m, 0x0312, 0x09);
    CHECK(bus_read(&s_m, 0x0312) == 0x09, "sector register");
    CHECK(bus_read(&s_m, 0x0314) == 0xFF && bus_read(&s_m, 0x0318) == 0xFF, "lines idle");
    bus_write(&s_m, 0x0315, 0x12);
    CHECK(s_m.via.t1_latch >> 8 == 0x12, "#0315 is the VIA's T1C-H");
    /* INTRQ (#D8) onto IRQ through bit 0 only. */
    bus_write(&s_m, 0x0310, 0xD8);
    CHECK((bus_read(&s_m, 0x0314) & 0x80) == 0, "#0314 shows INTRQ");
    CHECK(!(s_m.cpu.irq_lines & M6502_IRQ_DISC), "IRQ without bit 0");
    bus_write(&s_m, 0x0314, 0x01);
    CHECK(s_m.cpu.irq_lines & M6502_IRQ_DISC, "IRQ with bit 0");
    bus_write(&s_m, 0x0310, 0xD0);
    CHECK(!(s_m.cpu.irq_lines & M6502_IRQ_DISC), "IRQ gone with INTRQ");

    /* Snapshots: the latch, the map and the heads round trip; another
     * fit is refused by name; a command in progress is refused. */
    machine(&s_m, true);
    bus_write(&s_m, 0x0314, 0x84 | 0x20 | 0x10);
    s_m.fdc.drv[1].cyl = 33;
    s_m.fdc.track = 33;
    static mem_t snap;
    CHECK(mem_save(&snap, &s_m) == SNAP_OK, "save");
    machine(&s_m2, true);
    CHECK(mem_load(&snap, &s_m2) == SNAP_OK, "load");
    CHECK(snap_same(&s_m, &s_m2, "Microdisc state"), "round trip");
    machine(&s_m2, false);
    snap_info_t info = { ROM_UNKNOWN, ORIC_RAM_48K, false, false };
    CHECK(mem_check(&snap, &s_m2, &info) == SNAP_OTHER_MACHINE && info.microdisc,
          "refused without the Microdisc");
    bus_write(&s_m, 0x0310, 0x80);   /* a read, drive 1 empty: busy */
    CHECK(mem_save(&snap, &s_m) == SNAP_BUSY, "refused mid-command");
    return 0;
}

/* ---- sedoric ----------------------------------------------------------- */

/* Sedoric 3.006, Ray McLaughlin's 1996 distribution disc, as TOSEC has it. */
static const char SEDORIC_SHA1[] = "4cc7c19ecf04300e49fb0b84ce64a2899632b3ab";

static guest_t g;
static host_disc_t s_sed, s_drive_a, s_drive_b;
static host_disc_t *s_drives[ORIC_DISC_DRIVES];

static void serve_fields(guest_t *gg) { host_disc_serve(&gg->m, s_drives); }

static bool find_sedoric(void) {
    const char *dir = getenv("PICO_ORIC_ROMS");
    char base[512];
    snprintf(base, sizeof base, "%s", dir ? dir : PICO_ORIC_SOURCE_DIR "/roms");
    DIR *d = opendir(base);
    if (!d) return false;
    struct dirent *e;
    bool found = false;
    while (!found && (e = readdir(d)) != NULL) {
        size_t n = strlen(e->d_name);
        if (n < 4 || strcmp(e->d_name + n - 4, ".dsk") != 0) continue;
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", base, e->d_name);
        if (host_disc_load(&s_sed, path) != NULL) continue;
        uint8_t dg[SHA1_DIGEST_LEN];
        sha1(s_sed.img, s_sed.len, dg);
        char hex[41];
        for (int i = 0; i < 20; i++) snprintf(hex + 2 * i, 3, "%02x", dg[i]);
        found = strcmp(hex, SEDORIC_SHA1) == 0;
        if (!found) host_disc_free(&s_sed);
    }
    closedir(d);
    return found;
}

static void copy_disc(host_disc_t *dst, const host_disc_t *src) {
    host_disc_free(dst);
    *dst = *src;
    dst->img = (uint8_t *)malloc(src->len);
    memcpy(dst->img, src->img, src->len);
    dst->gets = dst->puts = 0;
}

static bool row_has(const char *s) {
    for (int r = 0; r < 28; r++)
        if (strstr(guest_row(&g.m, r), s)) return true;
    return false;
}

/* Until the keyboard is read again: the EPROM's routines turn the
 * VIA's T1 interrupt off while they drive the disc (#E3E3, #E3EB), and
 * with it the ROM's scan, so a key typed then is lost, as on a real
 * Oric. Ten quiet fields in a row. */
static void quiet(void) {
    for (int f = 0, run = 0; f < 3000 && run < 10; f++) {
        guest_fields(&g, 1);
        run = (g.m.via.ier & 0x40u) && !oric_disc_busy(&g.m) ? run + 1 : 0;
    }
}

/* A line typed once the guest is listening. */
static void type(const char *s) {
    quiet();
    guest_type(&g, s);
}

static void clear_screen(void) {
    /* So that what a command prints is all that is found. */
    type("CLS\r");
}

/* Fields until `s` is on the screen, or max. Returns fields run, -1 if
 * it never came. */
static int until(const char *s, int max) {
    for (int f = 0; f < max; f++) {
        if (row_has(s)) return f;
        guest_fields(&g, 1);
    }
    return row_has(s) ? max : -1;
}

/* Power on the 48K machine with the Microdisc and the disc in drive A,
 * to Sedoric's menu; then any key to BASIC. Returns guest cycles to the
 * menu, 0 if it never came. */
static uint64_t boot(rom_id_t rom) {
    oric_config_t cfg;
    oric_config_default(&cfg);
    cfg.rom = rom;
    cfg.microdisc = true;
    oric_init(&g.m, &cfg);
    keymatrix_init(&g.k);
    oric_load_rom(&g.m, guest_rom_image(rom), ORIC_ROM_SIZE);
    oric_load_eprom(&g.m, guest_rom_image(ROM_MICRODISC), ORIC_EPROM_SIZE);
    oric_power_on(&g.m);
    g.after_field = serve_fields;
    memset(s_drives, 0, sizeof s_drives);
    copy_disc(&s_drive_a, &s_sed);
    s_drives[0] = &s_drive_a;
    host_disc_insert(&g.m, 0, &s_drive_a);
    if (until("Please select:", 1500) < 0) return 0;
    uint64_t at = g.m.cpu.cycles;
    guest_type(&g, "X");
    return until("Ready", 200) >= 0 ? at : 0;
}

static void write_disc(const host_disc_t *d, const char *name) {
    if (!s_write_dir) return;
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", s_write_dir, name);
    if (host_disc_save(d, path)) printf("  wrote %s\n", path);
    else fprintf(stderr, "  could not write %s\n", path);
}

static int test_sedoric(rom_id_t rom) {
    const char *name = rom == ROM_BASIC10 ? "Oric-1 48K" : "Atmos 48K";
    uint64_t at = boot(rom);
    CHECK(at, "%s: Sedoric's menu never came", name);
    if (!at) { guest_dump(&g.m, stderr); return 0; }
    printf("  %s: Sedoric's menu after %llu cycles, %u sectors read\n", name,
           (unsigned long long)at, g.m.fdc.sectors_read);

    /* SAVE, DIR, NEW, LOAD, RUN. */
    clear_screen();
    type("10 PRINT\"DISC OK\"\r");
    type("SAVE\"M14\"\r");
    CHECK(until("Ready", 400) >= 0, "%s: SAVE", name);
    write_disc(&s_drive_a, rom == ROM_BASIC10 ? "saved-1.0.dsk" : "saved-1.1.dsk");
    clear_screen();
    type("DIR\"M14\"\r");
    CHECK(until("M14      .COM", 400) >= 0, "%s: DIR shows M14", name);
    type("NEW\r");
    clear_screen();
    type("LOAD\"M14\"\r");
    until("Ready", 400);
    type("RUN\r");
    CHECK(until("DISC OK", 100) >= 0, "%s: LOAD and RUN", name);

    /* The disc swapped between fields, as the menu does with the guest
     * paused, for a copy without M14: the DOS sees the new one. */
    copy_disc(&s_drive_b, &s_sed);
    host_disc_t keep = s_drive_a;
    s_drives[0] = &s_drive_b;
    host_disc_insert(&g.m, 0, &s_drive_b);
    clear_screen();
    type("LOAD\"M14\"\r");
    CHECK(until("FILE NOT FOUND", 400) >= 0, "%s: swapped disc seen", name);
    s_drives[0] = &keep;
    host_disc_insert(&g.m, 0, &keep);
    clear_screen();
    type("LOAD\"M14\"\r");
    CHECK(until("Ready", 400) >= 0 && !row_has("FILE NOT FOUND"), "%s: swapped back", name);

    /* DEL. */
    clear_screen();
    type("DEL\"M14.COM\"\r");
    until("Ready", 400);
    clear_screen();
    type("LOAD\"M14\"\r");
    CHECK(until("FILE NOT FOUND", 400) >= 0, "%s: DEL", name);

    /* Write-protected: SAVE refused, the image untouched. */
    s_drive_a = keep;
    s_drive_a.protect = true;
    host_disc_insert(&g.m, 0, &s_drive_a);
    uint8_t *before = (uint8_t *)malloc(s_drive_a.len);
    memcpy(before, s_drive_a.img, s_drive_a.len);
    clear_screen();
    type("SAVE\"P\"\r");
    CHECK(until("WRITE PROTECTED", 400) >= 0, "%s: write protect", name);
    CHECK(memcmp(before, s_drive_a.img, s_drive_a.len) == 0, "%s: protected image changed", name);
    free(before);
    s_drive_a.protect = false;
    host_disc_insert(&g.m, 0, &s_drive_a);

    /* Snapshots: refused at a boundary inside a command, taken at one
     * outside; loaded back, the machine does again what it did. */
    static mem_t snap;
    clear_screen();
    type("DIR\"M14\"");
    picocalc_event_t ev[ORIC_KEY_TEXT_EVENTS];
    unsigned nev = keymap_picocalc_text('\r', ev);
    for (unsigned i = 0; i < nev; i++) keymatrix_event(&g.k, ev[i].state, ev[i].code);
    bool refused = false;
    for (int f = 0; f < 400 && !refused; f++) {
        guest_fields(&g, 1);
        if (oric_disc_busy(&g.m)) refused = mem_save(&snap, &g.m) == SNAP_BUSY;
    }
    CHECK(refused, "%s: a snapshot mid-command", name);
    until("Ready", 400);
    guest_fields(&g, 10);
    clear_screen();
    CHECK(!oric_disc_busy(&g.m) && mem_save(&snap, &g.m) == SNAP_OK, "%s: snapshot", name);
    type("DIR\"M14\"\r");
    until("sectors free", 400);
    guest_fields(&g, 10);
    char first[28][41];
    for (int r = 0; r < 28; r++) snprintf(first[r], sizeof first[r], "%s", guest_row(&g.m, r));
    CHECK(mem_load(&snap, &g.m) == SNAP_OK, "%s: snapshot load", name);
    keymatrix_init(&g.k);
    type("DIR\"M14\"\r");
    until("sectors free", 400);
    guest_fields(&g, 10);
    bool same = true;
    for (int r = 0; r < 28; r++) same = same && strcmp(first[r], guest_row(&g.m, r)) == 0;
    CHECK(same && row_has("sectors free"), "%s: DIR again after the load", name);
    if (!same || !row_has("sectors free")) {
        for (int r = 0; r < 28; r++) fprintf(stderr, "  %2d |%-40s|%s\n", r, first[r], guest_row(&g.m, r));
    }
    return 0;
}

/* INIT on a blank image: unformatted tracks in drive B, formatted by
 * Sedoric's own write tracks, then a file saved there and listed. */
static int test_init(void) {
    uint64_t at = boot(ROM_BASIC11);
    CHECK(at, "INIT: no boot");
    if (!at) return 0;
    mfm_geom_t gb = { 2, 42 };
    host_disc_free(&s_drive_b);
    s_drive_b.g = gb;
    s_drive_b.len = mfm_track_offset(gb, 1, 42);
    s_drive_b.img = (uint8_t *)malloc(s_drive_b.len);
    mfm_header(s_drive_b.img, gb);
    memset(s_drive_b.img + ORIC_DISC_HEADER_LEN, 0x4E, s_drive_b.len - ORIC_DISC_HEADER_LEN);
    s_drives[1] = &s_drive_b;
    host_disc_insert(&g.m, 1, &s_drive_b);

    clear_screen();
    type("INIT B,17,42,D\r");
    CHECK(until("PRESS 'RETURN'", 200) >= 0, "INIT: the master prompt");
    type("\r");
    CHECK(until("Format (Y/N):", 200) >= 0, "INIT: format?");
    type("Y");
    int f = until("Formating complete", 20000);
    CHECK(f >= 0, "INIT: formatting");
    printf("  INIT: 84 tracks formatted in %d fields\n", f);
    CHECK(until("Name:", 200) >= 0, "INIT: name");
    type("M14\r");
    CHECK(until("Init statement:", 200) >= 0, "INIT: statement");
    type("\r");
    CHECK(until("Master disc (Y/N):", 200) >= 0, "INIT: master?");
    type("N");
    CHECK(until("Init another disc", 3000) >= 0, "INIT: another?");
    type("N");
    CHECK(until("Ready", 3000) >= 0, "INIT: done");
    clear_screen();
    type("10 PRINT\"ON B\"\r");
    type("SAVE\"B-M14\"\r");
    until("Ready", 400);
    clear_screen();
    type("DIR B\r");
    CHECK(until("M14      .COM", 400) >= 0, "INIT: a file on the new disc");
    write_disc(&s_drive_b, "init.dsk");
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "--write") == 0) s_write_dir = argv[2];

    if (test_mfm()) return 1;
    if (test_wd1793()) return 1;
    if (test_machine()) return 1;

    const char *dir;
    bool roms = guest_find_roms(&dir) && guest_have_rom(ROM_MICRODISC);
    if (!roms || !find_sedoric()) {
        if (test_failures) TEST_DONE();
        printf("SKIP sedoric: needs basic10.rom, basic11b.rom, microdis.rom and Sedoric 3.006 "
               "(SHA-1 %s) as a .dsk in %s\n", SEDORIC_SHA1, dir);
        return TEST_SKIP_CODE;
    }
    if (test_sedoric(ROM_BASIC11)) return 1;
    if (test_sedoric(ROM_BASIC10)) return 1;
    if (test_init()) return 1;
    TEST_DONE();
}
