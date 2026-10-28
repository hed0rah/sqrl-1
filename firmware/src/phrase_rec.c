/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 hed0rah */
/* MOUTH's phrases U1..U8 (eng_mouth.c mouth_user) kept over power-off, SQRL-1: a record of their own in the settings
 * sector (storage.c st_tail), written with the settings and read back at power-on (shell.c), and an object of the full
 * backup (editor_backup.c, id 10). A wrong magic, version, length or CRC loads no phrases. Needs FELUCCA_FLASH */

#define PHR_MAGIC 0x31505553u                   /* "SUP1" */
#define PHR_VER 1u
typedef struct {
    uint32_t magic;
    uint16_t ver, len;                          /* len: sizeof u */
    uint32_t crc;                               /* st_crc32 of u */
    mouth_user_t u[MOUTH_NUSER];
} phr_rec_t;
typedef char phr_rec_fits[sizeof(phr_rec_t) <= 0x200u ? 1 : -1];   /* (storage.c ST_TAIL_MAX) */
static phr_rec_t phr_rec __attribute__((aligned(4)));   /* what every settings write carries (st_tail) */
static void st_tail_set(const void *p, uint32_t n, int dirty);   /* (storage.c, included after shell.c) */
static int st_tail_load(void *dst, uint32_t n);
static uint32_t st_crc32(const void *p, uint32_t n);

static void phr_pack(void)                      /* the record from the phrases now */
{
    memcpy(phr_rec.u, mouth_user, sizeof phr_rec.u);
    phr_rec.magic = PHR_MAGIC;
    phr_rec.ver = PHR_VER;
    phr_rec.len = sizeof phr_rec.u;
    phr_rec.crc = st_crc32(phr_rec.u, sizeof phr_rec.u);
}

static int phr_valid(const phr_rec_t *r)
{
    return r->magic == PHR_MAGIC && r->ver == PHR_VER && r->len == sizeof r->u && r->crc == st_crc32(r->u, sizeof r->u);
}

/* a valid record's phrases into mouth_user (a phrase longer than MOUTH_UMAX loads empty), and kept as the record the
 * settings carry; dirty: write it with the next settings write */
static void phr_apply(const phr_rec_t *r, int dirty)
{
    uint32_t i;
    if (r != &phr_rec)
        memcpy(&phr_rec, r, sizeof phr_rec);
    for (i = 0; i < MOUTH_NUSER; i++)
        if (phr_rec.u[i].n > MOUTH_UMAX)
            phr_rec.u[i].n = 0;
    fm1_irq_off();                              /* (a voice may be singing one) */
    memcpy(mouth_user, phr_rec.u, sizeof phr_rec.u);
    fm1_irq_on();
    phr_rec.crc = st_crc32(phr_rec.u, sizeof phr_rec.u);   /* (a phrase emptied above: its CRC again) */
    st_tail_set(&phr_rec, sizeof phr_rec, dirty);
}

static void phr_load(void)                      /* power-on: the record from the committed settings copy */
{
    if (!st_tail_load(&phr_rec, sizeof phr_rec) && phr_valid(&phr_rec))
        phr_apply(&phr_rec, 0);
    else
        memset(&phr_rec, 0, sizeof phr_rec);    /* (none, or not one: the settings carry none) */
}
