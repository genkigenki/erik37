/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Changed for X0X and fm1-base: the object map is the app's (see below).
 * The M-Vave SMK-37's map, store owner and stock mapping: Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* Persistent storage on the SPI NOR.
 *
 * Every object has an A/B sector pair. A save goes to the copy that is not
 * the current one: erase the sector, program the payload (from offset 256),
 * then the 32-byte header at offset 0 last. The header is the commit record;
 * on load the valid copy with the highest seq wins, so a torn write leaves
 * the previous copy in charge.
 *
 * Flash access goes through three hooks (also used by the host test):
 *   st_read(off, dst, n)   st_erase(off)   st_prog(off, src, n)
 */
#define ST_MAGIC 0x30583058u                   /* "X0X0": never mistaken for a Felucca object */
#define ST_SECTOR 4096u
#define ST_PAYLOAD_OFF 256u
#define ST_PAYLOAD_MAX (ST_SECTOR - ST_PAYLOAD_OFF)

/* flash map: the app's (app.h APP_STORE_MAP, an offset per object, plat.h). Nothing between 0xE0000 and 0xFBFFF:
 * that is the update loader's staging area. The magic is X0X's; an app tells its own objects apart by its own magic. */
typedef struct {
    uint32_t magic;
    uint16_t type, slot;
    uint32_t seq, len, crc, rsv[2];
    uint32_t hcrc;
} st_hdr_t;

static int st_read(uint32_t off, void *dst, uint32_t n);
static int st_erase(uint32_t off);
static int st_prog(uint32_t off, const void *src, uint32_t n);

static uint32_t st_crc32(const void *p, uint32_t n)   /* zlib CRC-32, 4 bits per step */
{
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    const uint8_t *b = p;
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *b++;
        c = (c >> 4) ^ T[c & 15u];
        c = (c >> 4) ^ T[c & 15u];
    }
    return ~c;
}

#if BOARD_SMK37
/* The M-Vave SMK-37 (boards/smk37/fm1_flash_map.h): the objects in the stock VM, 0x9D000..0xAEFFF, by number, A/B
 * neighbours (APP_STORE_MAP's FM-1 offsets mean nothing there); never near BTIF / USRTRIM (0xC0000..0xC1FFF) or
 * key_mac (0xFF000). */
_Static_assert(FL_DATA_LO + 2u * OBJ_NOBJ * ST_SECTOR <= FL_OBJ_HI, "SMK-37 store: more objects than 0x9D000..0xAEFFF holds");
static uint32_t st_sector(uint32_t obj, uint32_t copy)  /* flash offset of copy A (0) / B (1) */
{
    return FL_DATA_LO + ((obj % OBJ_NOBJ) * 2u + (copy & 1u)) * ST_SECTOR;
}

/* The owner of the store. The store is the stock firmware's VM, so after an install it may hold the stock firmware's
 * or another firmware's data, which must never be read as this one's. The sector after the objects (0xAF000) holds the
 * tag "SMK37DATAFM1B:" + the app's name (FM1B_DATA_FAMILY, default APP_NAME). Tagged: load as ever. Anything else:
 * clear the objects (0x9D000..0xAEFFF), then write the tag. The app's big data (plat_data, USRFLASH) checks itself.
 * Calibration, BT pairing and key_mac are outside: never touched. ST_IDLE() runs between the erases. */
#ifndef FM1B_DATA_FAMILY
#define FM1B_DATA_FAMILY APP_NAME
#endif
#ifndef ST_IDLE
#define ST_IDLE() ((void)0)
#endif
#define ST_OWNER_OFF FL_OBJ_HI
_Static_assert(sizeof FM1B_DATA_FAMILY >= 2u && sizeof FM1B_DATA_FAMILY <= 32u - 14u, "FM1B_DATA_FAMILY: 1..17 characters");
/* The stock firmware's mapping in USRFLASH (its global settings at 0xC2000, magic A3 3A; 8 presets from 0xC3000 every
 * 0x5000; the end marker "usrdata\0" at 0xEAFF8): the stock firmware trusts the area while the marker is there and
 * never rebuilds it. So an intact stock mapping is kept until the app first writes there, and the marker is released
 * (its sector erased: the stock firmware rebuilds its factory mapping at its next start) before the first erase in
 * USRFLASH (plat_data_erase) and at boot when it stands over anything else (st_owner_check). */
#define ST_STOCKTAG_OFF 0xEAFF8u
_Static_assert(ST_STOCKTAG_OFF + 8u == FL_STOCKTAG_HI, "SMK-37: the stock marker's place");
static int st_stock_tagged(void)
{
    static const uint8_t TAG[8] = {'u', 's', 'r', 'd', 'a', 't', 'a', 0};
    uint8_t t[8];
    uint32_t i;
    if (st_read(ST_STOCKTAG_OFF, t, 8))
        return 0;
    for (i = 0; i < 8u; i++)
        if (t[i] != TAG[i])
            return 0;
    return 1;
}
static int st_stock_intact(void)                /* the marker over the stock layout */
{
    uint8_t b[8];
    uint32_t n, k, i, ff;
    if (!st_stock_tagged() || st_read(FL_BIG_LO, b, 2) || b[0] != 0xA3u || b[1] != 0x3Au)
        return 0;
    for (n = 0; n < 8u; n++)                    /* each preset: its head written, its last two sectors erased */
        for (k = 0; k < 3u; k++) {
            if (st_read(FL_BIG_LO + 0x1000u + n * 0x5000u + (k ? 0x2000u + k * 0x1000u : 0u), b, 8))
                return 0;
            for (ff = 1, i = 0; i < 8u; i++)
                ff &= b[i] == 0xFFu;
            if (ff != (k != 0u))
                return 0;
        }
    return 1;
}
static int st_erase_stocktag(void);             /* (unity.c: the one erase outside the app's windows) */
static void st_stock_release(void)              /* erase the marker's sector, once: later calls find no marker */
{
    if (st_stock_tagged())
        st_erase_stocktag();
}
static uint8_t st_owner_cleared;                /* (the app may say so) */
static void st_owner_check(void)
{
    static const char FAM[] = FM1B_DATA_FAMILY;
    uint8_t tag[32], want[32];
    uint32_t i, ours = 1, s;
    for (i = 0; i < 32u; i++)
        want[i] = i < 14u ? (uint8_t)"SMK37DATAFM1B:"[i] : i - 14u < sizeof FAM - 1u ? (uint8_t)FAM[i - 14u] : 0u;
    if (!st_stock_intact())
        st_stock_release();                     /* the marker over anything but the stock mapping: let it rebuild */
    if (st_read(ST_OWNER_OFF, tag, 32))
        return;
    for (i = 0; i < 32u; i++)
        ours &= tag[i] == want[i];
    if (ours)
        return;
    for (s = FL_DATA_LO; s < FL_OBJ_HI; s += ST_SECTOR) {   /* not ours (or untagged data): clear it */
        ST_IDLE();
        st_erase(s);
    }
    st_owner_cleared = 1;
    ST_IDLE();
    st_erase(ST_OWNER_OFF);
    st_prog(ST_OWNER_OFF, want, 32);
}
#else
static uint32_t st_sector(uint32_t obj, uint32_t copy)  /* flash offset of copy A (0) / B (1) */
{
    static const uint32_t map[OBJ_NOBJ] = APP_STORE_MAP;
    return map[obj] + copy * ST_SECTOR;
}
#endif

static uint8_t st_buf[ST_PAYLOAD_MAX] __attribute__((aligned(4)));

static int st_head(uint32_t obj, uint32_t copy, st_hdr_t *h)   /* commit record valid: 0 */
{
    if (st_read(st_sector(obj, copy), h, sizeof *h))
        return -1;
    if (h->magic != ST_MAGIC || h->type != obj || h->len > ST_PAYLOAD_MAX ||
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

/* the current copy: the valid one with the highest seq (A on a tie), -1 when
 * neither is valid. Headers first, so only the winner's payload is read (it
 * is left in st_buf); *h gets its header. */
static int st_current(uint32_t obj, st_hdr_t *h)
{
    st_hdr_t a, b;
    int va = st_head(obj, 0, &a) == 0, vb = st_head(obj, 1, &b) == 0;
    if (vb && (!va || b.seq > a.seq)) {
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

/* load object into dst (up to max bytes); returns the length, or -1 */
static int st_load(uint32_t obj, void *dst, uint32_t max)
{
    uint32_t i;
    st_hdr_t h;
    if (st_current(obj, &h) < 0)
        return -1;
    if (h.len > max)
        h.len = max;
    for (i = 0; i < h.len; i++)
        ((uint8_t *)dst)[i] = st_buf[i];
    return (int)h.len;
}

static int st_save(uint32_t obj, const void *src, uint32_t len)
{
    uint32_t seq, base, off;
    int cur, rc;
    st_hdr_t h;
    if (len > ST_PAYLOAD_MAX)
        return -1;
    cur = st_current(obj, &h);
    seq = cur < 0 ? 0u : h.seq;
    base = st_sector(obj, cur == 0 ? 1u : 0u);       /* write the other copy */
    for (off = 0; off < len; off++)
        st_buf[off] = ((const uint8_t *)src)[off];    /* the driver wants RAM sources */
    if ((rc = st_erase(base)) != 0)
        return rc;
    for (off = 0; off < len; off += 256u) {
        uint32_t n = len - off > 256u ? 256u : len - off;
        if ((rc = st_prog(base + ST_PAYLOAD_OFF + off, st_buf + off, n)) != 0)
            return rc;
    }
    h.magic = ST_MAGIC;
    h.type = (uint16_t)obj;
    h.slot = (uint16_t)(cur == 0 ? 1 : 0);
    h.seq = seq + 1u;
    h.len = len;
    h.crc = st_crc32(st_buf, len);
    h.rsv[0] = h.rsv[1] = 0xFFFFFFFFu;
    h.hcrc = st_crc32(&h, sizeof h - 4u);
    if ((rc = st_prog(base, &h, sizeof h)) != 0)       /* the commit record, last */
        return rc;
    {   /* read back: a write-protected or failing part must not report SAVED */
        st_hdr_t chk;
        uint32_t c = cur == 0 ? 1u : 0u;
        if (st_head(obj, c, &chk) || chk.seq != h.seq || st_body(obj, c, &chk))
            return -7;
    }
    return 0;
}
