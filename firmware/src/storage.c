/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Persistent storage on the SPI NOR.
 *
 * Every object has an A/B sector pair. A save goes to the copy that is not
 * the current one: erase the sector, program the payload pages (from offset
 * 256), then the 32-byte header at offset 0 LAST. The header is the commit
 * record (magic, seq, length, payload CRC, header CRC); on load the valid
 * copy with the newest seq wins (including wrap), so a write torn at any point leaves the
 * previous copy in charge.
 *
 * Flash access goes through three hooks (also used by the host test):
 *   st_read(off, dst, n)   st_erase(off)   st_prog(off, src, n)
 */
#define ST_MAGIC 0x554C4546u                   /* "FELU" */
#define ST_SECTOR 4096u
#define ST_PAYLOAD_OFF 256u
#define ST_PAYLOAD_MAX (ST_SECTOR - ST_PAYLOAD_OFF)

/* flash map (FL_DATA 0x97000..0xDFFFF, FL_GLOB 0xFC000.., FL_AUTO 0xE5000..0xE6FFF, FL_VBANK 0xE7000): settings 0xFC000, projects
 * 0x97000..0x9EFFF, user sample slots 0xA0000..0xDBFFF (eng_sample.c), user preset banks 0xDC000..0xDFFFF (upreset.c),
 * the user presets' FM6 patches (up_fm6.c, since 1.0.3): copy A 0x9F000, copy B 0xFE000; the autosave (project.c,
 * 1.2): A 0xE5000, B 0xE6000, above the OTA staging (0xE0000..0xE4FFF), in the margin the stock firmware's own update
 * stages into (only ever while the stock firmware runs: a later Felucca finds no valid copy and starts as without one).
 * Those two sectors held the FM6 patch bank of 1.0..1.0.2 (OBJ_FM6BANK, retired): both objects use the same pair,
 * told apart by the commit record's type. The bank is only read, once, to move its patches into the user presets
 * (up_fm6.c upf_boot); the first write of the new object goes to the sector that does not hold the bank's newest
 * copy (st_save_to), so a power cut never loses both.
 * The FM6 voice bank (1.4.1, fm6_vbank.c): 0xE7000, the sector above the autosave, with a commit record of the same
 * kind (type OBJ_FM6VB, not an st_load object); a new bank is first staged in the autosave's older copy (fm6_vbank.c). */
/* SQRL-1 with FELUCCA_CMP: each project slot's companion (A..D, then the autosave's) as ST_CMP_NOBJ objects, each an
 * A/B pair from 0xA0000 up, where the user sample slots were (eng_sample.c): 5 x 4 x 8 KB, 0xA0000..0xC7FFF */
#define ST_CMP_NOBJ 4u
#ifndef FELUCCA_CMP
#define FELUCCA_CMP 0                           /* (storage.c on its own, as storage_test builds it: none) */
#endif
#define ST_CMP_N (FELUCCA_CMP ? 5u * ST_CMP_NOBJ : 0u)
enum { OBJ_SETTINGS, OBJ_PROJECT0, OBJ_UPRESET0 = OBJ_PROJECT0 + 4, OBJ_FM6BANK = OBJ_UPRESET0 + 2, OBJ_UPFM6,
       OBJ_AUTOSAVE, OBJ_CMP0, OBJ_COUNT = OBJ_CMP0 + ST_CMP_N };
#define ST_CMP_BASE 0xA0000u
_Static_assert(ST_CMP_BASE + ST_CMP_N * 2u * 4096u <= 0xDC000u, "the companions in the samples' flash, below the user "
               "presets");
#define OBJ_FM6VB 32u                          /* the voice bank's commit records (fm6_vbank.c; >= OBJ_COUNT: st_load
                                                * never takes one, no object here takes its sectors) */
_Static_assert(OBJ_COUNT <= OBJ_FM6VB, "the object ids below the voice bank's commit records");

typedef struct {
    uint32_t magic;
    uint16_t type, slot;
    uint32_t seq, len, crc, rsv[2];
    uint32_t hcrc;
} st_hdr_t;
_Static_assert(sizeof(st_hdr_t) == 32u, "storage commit record layout");

static int st_read(uint32_t off, void *dst, uint32_t n);
static int st_erase(uint32_t off);
static int st_prog(uint32_t off, const void *src, uint32_t n);

static uint32_t st_crc32_add(uint32_t c, const void *p, uint32_t n)   /* zlib CRC-32, 4 bits per step; running:
                                                                         * from 0xFFFFFFFF, the CRC is ~c */
{
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    const uint8_t *b = p;
    while (n--) {
        c ^= *b++;
        c = (c >> 4) ^ T[c & 15u];
        c = (c >> 4) ^ T[c & 15u];
    }
    return c;
}
static uint32_t st_crc32(const void *p, uint32_t n) { return ~st_crc32_add(0xFFFFFFFFu, p, n); }

static uint32_t st_sector(uint32_t obj, uint32_t copy)  /* flash offset of copy A (0) / B (1) */
{
    if (obj == OBJ_SETTINGS)
        return 0xFC000u + copy * ST_SECTOR;
    if (obj == OBJ_FM6BANK || obj == OBJ_UPFM6)          /* (the same pair: see the flash map) */
        return copy ? 0xFE000u : 0x9F000u;
    if (obj == OBJ_AUTOSAVE)
        return 0xE5000u + copy * ST_SECTOR;
#if FELUCCA_CMP
    if (obj >= OBJ_CMP0 && obj < OBJ_CMP0 + ST_CMP_N)
        return ST_CMP_BASE + (obj - OBJ_CMP0) * 2u * ST_SECTOR + copy * ST_SECTOR;
#endif
    if (obj >= OBJ_UPRESET0)
        return 0xDC000u + (obj - OBJ_UPRESET0) * 2u * ST_SECTOR + copy * ST_SECTOR;
    return 0x97000u + (obj - OBJ_PROJECT0) * 2u * ST_SECTOR + copy * ST_SECTOR;
}

static uint8_t st_buf[ST_PAYLOAD_MAX] __attribute__((aligned(4), section(".pool")));   /* (main loop only) */
#if FELUCCA_SHELL
/* SQRL-1's own record in the settings sector, after the settings object: bytes 0xE00..0xFFF of each copy. It is
 * written into the same copy as the settings, before their commit record, so it rides their A/B alternation (a write
 * torn anywhere leaves the older copy, record and all, in charge) and is read only from the committed copy. Stock
 * Felucca and older SQRL-1 read the settings by their length and never look there. The record carries its own magic,
 * version, length and CRC (shell.c), so an absent or corrupt one loads as nothing. st_tail is what every settings
 * write puts there (st_tail_n 0: nothing) */
#define ST_TAIL_OFF 0xE00u
#define ST_TAIL_MAX (ST_SECTOR - ST_TAIL_OFF)
static const uint8_t *st_tail;          /* the record (the shell's own memory, 4-aligned), st_tail_n bytes */
static uint32_t st_tail_n;
static uint8_t st_tail_dirty;           /* a new record: the next settings write goes ahead though they are unchanged */
static void st_tail_set(const void *p, uint32_t n, int dirty)   /* (shell.c, included before this file, declares it;
                                                                 * dirty 0: the record loaded at power-on, kept) */
{
    st_tail = p;
    st_tail_n = n > ST_TAIL_MAX ? ST_TAIL_MAX : n;
    st_tail_dirty |= (uint8_t)(dirty != 0);
}
#endif

static int st_head(uint32_t obj, uint32_t copy, st_hdr_t *h)   /* commit record valid: 0 */
{
    if (obj >= OBJ_COUNT || copy > 1u)
        return -1;
    if (st_read(st_sector(obj, copy), h, sizeof *h))
        return -1;
    if (h->magic != ST_MAGIC || h->type != obj || h->slot != copy || h->len > ST_PAYLOAD_MAX ||
        h->hcrc != st_crc32(h, sizeof *h - 4u))
        return -1;
    return 0;
}

static int st_body(uint32_t obj, uint32_t copy, const st_hdr_t *h)   /* payload -> st_buf, CRC ok: 0 */
{
    if (st_read(st_sector(obj, copy) + ST_PAYLOAD_OFF, st_buf, h->len) || st_crc32(st_buf, h->len) != h->crc)
        return -1;
    return 0;
}

/* the current copy: the newest valid sequence (A on a tie), -1 when
 * neither is valid. Headers first, so only the winner's payload is read (it
 * is left in st_buf); *h gets its header. */
static int st_current(uint32_t obj, st_hdr_t *h)
{
    st_hdr_t a, b;
    int va = st_head(obj, 0, &a) == 0, vb = st_head(obj, 1, &b) == 0;
    if (vb && (!va || (b.seq != a.seq && b.seq - a.seq < 0x80000000u))) {
        if (st_body(obj, 1, &b) == 0) {
            *h = b;
            return 1;
        }
        vb = 0;
    }
    if (va && st_body(obj, 0, &a) == 0) {
        *h = a;
        return 0;
    }
    if (vb && st_body(obj, 1, &b) == 0) {
        *h = b;
        return 1;
    }
    return -1;
}

/* load the whole object into dst; returns its length, or -1 if it does not fit */
static int st_load(uint32_t obj, void *dst, uint32_t max)
{
    uint32_t i;
    st_hdr_t h;
    if (obj >= OBJ_COUNT || st_current(obj, &h) < 0 || h.len > max)
        return -1;
    for (i = 0; i < h.len; i++)
        ((uint8_t *)dst)[i] = st_buf[i];
    return (int)h.len;
}

/* where a save of obj goes: *to (0 A, 1 B; -1 asked: the one that is not the current copy) and the sequence after
 * which it commits. It reads the current copy's body to check it, through st_buf: fill st_buf after this */
static void st_save_prep(uint32_t obj, int *to, uint32_t *seq)
{
    st_hdr_t h;
    int cur = st_current(obj, &h);
    *seq = cur < 0 ? 0u : h.seq;
    if (*to < 0)
        *to = cur == 0 ? 1 : 0;                       /* write the other copy */
}
/* st_buf's first len bytes as obj's copy `to`, committed as seq + 1 (st_save_prep) */
static int st_write(uint32_t obj, uint32_t len, int to, uint32_t seq)
{
    uint32_t base, off;
    int rc;
    st_hdr_t h;
    base = st_sector(obj, (uint32_t)to);
    if ((rc = st_erase(base)) != 0)
        return rc;
    for (off = 0; off < len; off += 256u) {
        uint32_t n = len - off > 256u ? 256u : len - off;
        if ((rc = st_prog(base + ST_PAYLOAD_OFF + off, st_buf + off, n)) != 0)
            return rc;
    }
#if FELUCCA_SHELL
    if (obj == OBJ_SETTINGS && st_tail_n)              /* (SQRL-1's record, before the commit record) */
        for (off = 0; off < st_tail_n; off += 256u) {
            uint32_t n = st_tail_n - off > 256u ? 256u : st_tail_n - off;
            if ((rc = st_prog(base + ST_TAIL_OFF + off, st_tail + off, n)) != 0)
                return rc;
        }
#endif
    h.magic = ST_MAGIC;
    h.type = (uint16_t)obj;
    h.slot = (uint16_t)to;
    h.seq = seq + 1u;
    h.len = len;
    h.crc = st_crc32(st_buf, len);
    h.rsv[0] = h.rsv[1] = 0xFFFFFFFFu;
    h.hcrc = st_crc32(&h, sizeof h - 4u);
    if ((rc = st_prog(base, &h, sizeof h)) != 0)       /* the commit record, last */
        return rc;
    {   /* read back: a write-protected or failing part must not report SAVED */
        st_hdr_t chk;
        uint32_t c = (uint32_t)to;
        if (st_head(obj, c, &chk) || memcmp(&chk, &h, sizeof h) || st_body(obj, c, &chk))
            return -7;
    }
    return 0;
}
/* save into copy `to` (0 A, 1 B; -1: the one that is not the current copy, as st_save); src may be st_buf itself
 * only through st_save_prep and st_write (the current copy's check reads into st_buf) */
static int st_save_to(uint32_t obj, const void *src, uint32_t len, int to)
{
    uint32_t seq, off;
    if (obj >= OBJ_COUNT || len > ST_PAYLOAD_MAX || to > 1)
        return -1;
    st_save_prep(obj, &to, &seq);
    for (off = 0; off < len; off++)
        st_buf[off] = ((const uint8_t *)src)[off];    /* the driver wants RAM sources */
    return st_write(obj, len, to, seq);
}

static int st_save(uint32_t obj, const void *src, uint32_t len) { return st_save_to(obj, src, len, -1); }
#if FELUCCA_SHELL
/* SQRL-1's record from the committed settings copy: n bytes into dst (the caller checks its magic and CRC); -1 when
 * no settings copy is valid */
static int st_tail_load(void *dst, uint32_t n)
{
    st_hdr_t h;
    int c;
    if (!flash_ok || n > ST_TAIL_MAX || (c = st_current(OBJ_SETTINGS, &h)) < 0)
        return -1;
    return st_read(st_sector(OBJ_SETTINGS, (uint32_t)c) + ST_TAIL_OFF, dst, n) ? -1 : 0;
}
#endif
