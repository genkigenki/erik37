/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* The M-Vave SMK-37's store owner and the stock mapping (storage.c st_owner_check, st_stock_release; build with
 * -DBOARD_SMK37=1), on a simulated flash: after the stock firmware (its VM, its USRFLASH mapping with the end marker),
 * after another firmware's store, and this one's own. Only the store's window (0x9D000-0xAFFFF) and, when the mapping
 * is not the stock one, the marker's sector 0xEA000 are ever erased or written; this firmware's store is kept.
 *   cc -DBOARD_SMK37=1 -Ibsp/chip -Ibsp/boards/smk37 -Iinclude -o smk_owner_test tests/smk_owner_test.c */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fm1_cc.h"
#if !BOARD_SMK37
#error "build with -DBOARD_SMK37=1"
#endif
#include "fm1_flash_map.h"
#include "plat.h"

static uint8_t nor[0x100000], before[0x100000];
static uint32_t erases, progs, outside;

static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off)
{
    if (!FL_STORE_OK(off, 0x1000u) || (off & 0xFFFu)) {
        outside++;
        return -8;
    }
    erases++;
    memset(nor + off, 0xFF, 0x1000);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    if (!FL_STORE_OK(off, n) || (off & 0xFFu) + n > 256u) {
        outside++;
        return -8;
    }
    progs++;
    for (i = 0; i < n; i++)
        nor[off + i] &= s[i];
    return 0;
}
static uint32_t tag_erases;
static int st_erase_stocktag(void)
{
    tag_erases++;
    memset(nor + FL_STOCKTAG_LO, 0xFF, 0x1000);
    return 0;
}
#define APP_NAME "RADIO"
enum { OBJ_PANEL, OBJ_BLE, OBJ_WIFI, OBJ_RADIO, OBJ_FAV, OBJ_NOBJ };
#define APP_STORE_MAP {0}
#include "../device/storage.c"

static int bad;
static void check(const char *what, int ok) { printf("smk owner: %-84s %s\n", what, ok ? "ok" : "FAIL"); bad += !ok; }

static void stock_unit(int intact)               /* the stock firmware's VM and USRFLASH, as M-Vave's leaves them */
{
    uint32_t i, n;
    memset(nor, 0xFF, sizeof nor);
    srand(37);
    for (i = 0x9D000; i < 0xC0000; i++)          /* the stock VM: its own records */
        nor[i] = (uint8_t)rand();
    for (i = 0xC0000; i < 0xC2000; i++)          /* BTIF / USRTRIM */
        nor[i] = (uint8_t)(i * 7u);
    memcpy(nor + 0xC1224, "trimdat", 8);
    nor[0xC2000] = 0xA3;                         /* the stock mapping: settings, 8 presets, the end marker */
    nor[0xC2001] = 0x3A;
    for (n = 0; n < 8u; n++)
        for (i = 0; i < 0x29E3u; i++)
            nor[0xC3000 + n * 0x5000u + i] = (uint8_t)(n + i);
    if (!intact)
        nor[0xC3000 + 3u * 0x5000u + 0x4000u] = 0;   /* (a preset's tail written: not the stock layout) */
    memcpy(nor + 0xEAFF8, "usrdata", 8);
    for (i = 0xF4000; i < 0xFE000; i++)          /* USR, key_mac */
        nor[i] = (uint8_t)(i ^ 0x5A);
    for (i = 0xFF000; i < 0x100000; i++)
        nor[i] = (uint8_t)(i ^ 0xA5);
}
static int same_outside(uint32_t lo, uint32_t hi, int marker_may_go)   /* nothing changed outside [lo, hi) */
{
    uint32_t i;
    for (i = 0; i < 0x100000u; i++) {
        if (i >= lo && i < hi)
            continue;
        if (marker_may_go && i >= FL_STOCKTAG_LO && i < FL_STOCKTAG_HI)
            continue;
        if (nor[i] != before[i])
            return 0;
    }
    return 1;
}
static int erased(uint32_t lo, uint32_t hi)
{
    uint32_t i;
    for (i = lo; i < hi; i++)
        if (nor[i] != 0xFF)
            return 0;
    return 1;
}

int main(void)
{
    char a[300], got[300];
    memset(a, 'r', sizeof a);
    /* 1. after the stock firmware, its mapping intact: the store cleared and tagged, the mapping and its marker kept */
    stock_unit(1);
    memcpy(before, nor, sizeof nor);
    st_owner_check();
    check("after stock: the objects 0x9D000-0xAEFFF cleared, the tag written at 0xAF000",
          erased(0x9D000u, 0xAF000u) && !memcmp(nor + 0xAF000, "SMK37DATAFM1B:RADIO", 20) && st_owner_cleared);
    check("after stock: nothing else written (the stock mapping and its marker kept; BTIF, USRTRIM, USR, key_mac)",
          same_outside(0x9D000u, 0xB0000u, 0) && tag_erases == 0 && outside == 0);
    /* 2. this firmware's store: kept, nothing written */
    erases = progs = 0;
    check("a save and a load in the store", st_save(OBJ_WIFI, a, sizeof a) == 0 &&
          st_load(OBJ_WIFI, got, sizeof got) == (int)sizeof a && !memcmp(a, got, sizeof a));
    memcpy(before, nor, sizeof nor);
    erases = progs = 0;
    st_owner_cleared = 0;
    st_owner_check();
    check("its own store at the next start: kept, nothing erased or written",
          erases == 0 && progs == 0 && !st_owner_cleared && !memcmp(nor, before, sizeof nor));
    check("... and still loads", st_load(OBJ_WIFI, got, sizeof got) == (int)sizeof a && !memcmp(a, got, sizeof a));
    /* 3. the first write into the data area releases the marker, once */
    st_stock_release();
    check("st_stock_release: the marker's sector erased (the stock firmware rebuilds its mapping)",
          tag_erases == 1 && erased(FL_STOCKTAG_LO, FL_STOCKTAG_HI));
    st_stock_release();
    check("... once: no marker, no erase", tag_erases == 1);
    /* 4. another firmware's store (another tag): cleared */
    stock_unit(1);
    memcpy(nor + 0xAF000, "SMK37DATADUB1", 14);
    memcpy(before, nor, sizeof nor);
    tag_erases = 0;
    st_owner_cleared = 0;
    st_owner_check();
    check("another firmware's tag: its objects cleared, ours written, nothing else touched",
          st_owner_cleared && erased(0x9D000u, 0xAF000u) && !memcmp(nor + 0xAF000, "SMK37DATAFM1B:RADIO", 20) &&
          same_outside(0x9D000u, 0xB0000u, 0) && outside == 0);
    check("... no object of the other firmware's loads", st_load(OBJ_WIFI, got, sizeof got) < 0);
    /* 5. the marker over anything but the stock mapping: released at the start */
    stock_unit(0);
    memcpy(before, nor, sizeof nor);
    tag_erases = 0;
    st_owner_check();
    check("the marker over another firmware's data in USRFLASH: released at the start, nothing else outside the store",
          tag_erases == 1 && erased(FL_STOCKTAG_LO, FL_STOCKTAG_HI) && same_outside(0x9D000u, 0xB0000u, 1) &&
          outside == 0);
    printf("%s\n", bad ? "SMK OWNER TEST FAILED" : "smk owner test passed");
    return bad != 0;
}
