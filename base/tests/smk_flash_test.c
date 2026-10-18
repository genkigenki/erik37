/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* The M-Vave SMK-37's flash map (felucca-bsp boards/smk37/fm1_flash_map.h; build with -DBOARD_SMK37=1): every 4 KiB
 * sector of the 1 MiB part through the app's allow-list (FL_RANGE_OK, FL_GUARD, FL_ERASE_GUARD: the RAM driver's
 * checks), the update loader's (FL_LDR_RANGE_OK) and the store's: none may reach the head 0x0-0x3FFF, BTIF /
 * USRTRIM 0xC0000-0xC1FFF, USR 0xF4000-0xFDFFF or key_mac 0xFF000-; the stock marker 0xEA000 only as an erase; the
 * store's objects (storage.c, as many as an app may have), its owner tag, the data area (plat_data) and the OTA
 * staging lie inside their windows; ranges that wrap or straddle a boundary are refused.
 *   cc -DBOARD_SMK37=1 -Ibsp/chip -Ibsp/boards/smk37 -Iinclude -o smk_flash_test tests/smk_flash_test.c */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "fm1_cc.h"
#if !BOARD_SMK37
#error "build with -DBOARD_SMK37=1"
#endif
#include "fm1_flash_map.h"
#include "plat.h"

static int st_read(uint32_t off, void *dst, uint32_t n) { (void)off; (void)dst; (void)n; return -1; }
static int st_erase(uint32_t off) { (void)off; return -1; }
static int st_prog(uint32_t off, const void *src, uint32_t n) { (void)off; (void)src; (void)n; return -1; }
static int st_erase_stocktag(void) { return -1; }
#define APP_NAME "TEST"
enum { OBJ_PANEL, OBJ_1, OBJ_2, OBJ_3, OBJ_4, OBJ_5, OBJ_6, OBJ_7, OBJ_8, OBJ_NOBJ };   /* the most the store holds */
#define APP_STORE_MAP {0}
#include "../device/storage.c"
#define OTA_AREA_T 0xBB000u                        /* bsp chip/ota.c on the SMK-37 */
#define OTA_RES_T 0xBFF00u

static int bad;
static void check(const char *what, int ok) { printf("smk flash: %-86s %s\n", what, ok ? "ok" : "FAIL"); bad += !ok; }

static int protected_sector(uint32_t s)
{
    return s < 0x4000u || (s >= 0xC0000u && s < 0xC2000u) || (s >= 0xF4000u && s < 0xFE000u) || s >= 0xFF000u;
}

int main(void)
{
    uint32_t s, o, c, leak_app = 0, leak_ldr = 0, leak_guard = 0, allowed = 0, erasable = 0, in_store = 1, distinct = 1;
    (void)st_load;
    (void)st_save;
    (void)st_owner_check;
    for (s = 0; s < 0x100000u; s += 0x1000u) {
        if (!protected_sector(s))
            continue;
        leak_app += FL_RANGE_OK(s, 0x1000u) || FL_STORE_OK(s, 0x1000u) || FL_RANGE_OK(s, 256u) || FL_RANGE_OK(s + 0xF00u, 256u) ||
                    FL_GUARD(s, 0x1000u) || FL_ERASE_GUARD(s, 0x1000u) || FL_GUARD(s + 0xF00u, 256u);
        leak_ldr += FL_LDR_RANGE_OK(s, 0x1000u) || FL_LDR_RANGE_OK(s + 0xF00u, 256u);
        leak_guard += !FL_PROTECTED(s, 0x1000u) || !FL_PROTECTED(s + 0xF00u, 256u);
    }
    for (s = 0; s < 0x100000u; s += 0x1000u) {
        allowed += FL_GUARD(s, 0x1000u);
        erasable += FL_ERASE_GUARD(s, 0x1000u);
    }
    check("no protected sector passes the app's allow-list or the RAM driver's guards", leak_app == 0);
    check("no protected sector passes the update loader's allow-list", leak_ldr == 0);
    check("the RAM driver's own net (FL_PROTECTED) covers every protected sector", leak_guard == 0);
    check("straddling ranges refused: 0xBF000+0x2000, 0xE9000+0x2000, 0x3000+0x2000, 0xFE000+0x2000",
          !FL_GUARD(0xBF000u, 0x2000u) && !FL_GUARD(0xE9000u, 0x2000u) && !FL_LDR_RANGE_OK(0xBF000u, 0x2000u) &&
          !FL_LDR_RANGE_OK(0x3000u, 0x2000u) && FL_PROTECTED(0xBF000u, 0x2000u) && FL_PROTECTED(0x3F00u, 0x200u) &&
          FL_PROTECTED(0xFEF00u, 0x200u));
    check("wrapping ranges refused (off + n past 4 GiB)",
          !FL_GUARD(0xBB000u, 0xFFFFF000u) && !FL_LDR_RANGE_OK(0x9D000u, 0xFFFFFF00u) && FL_PROTECTED(0xBB000u, 0xFFFFF000u));
    check("app windows: the store 0x9D000-0xAFFFF, the OTA staging 0xBB000-0xBFFFF, the data area 0xC2000-0xE9FFF",
          FL_GUARD(0x9D000u, 0x1000u) && FL_GUARD(0xAF000u, 0x1000u) && !FL_GUARD(0xB0000u, 0x1000u) &&
          !FL_GUARD(0xBA000u, 0x1000u) && FL_GUARD(0xBB000u, 0x1000u) && FL_GUARD(0xBF000u, 0x1000u) &&
          FL_GUARD(0xC2000u, 0x1000u) && FL_GUARD(0xE9000u, 0x1000u) && !FL_GUARD(0xEA000u, 0x1000u) &&
          !FL_GUARD(0xEB000u, 0x1000u) && !FL_GUARD(0x9C000u, 0x1000u) &&
          allowed == (0xB0000u - 0x9D000u + 0xC0000u - 0xBB000u + 0xEA000u - 0xC2000u) / 0x1000u);
    check("the stock marker 0xEA000: erased as a whole sector only, never programmed",
          FL_ERASE_GUARD(0xEA000u, 0x1000u) && !FL_GUARD(0xEA000u, 0x1000u) && !FL_GUARD(0xEA000u, 256u) &&
          !FL_GUARD(0xEAF00u, 256u) && !FL_ERASE_GUARD(0xEA000u, 0x2000u) && !FL_ERASE_GUARD(0xE9000u, 0x2000u) &&
          erasable == allowed + 1u && ST_STOCKTAG_OFF == 0xEAFF8u);
    check("the data area (plat_data) is FL_BIG: 0xC2000-0xE9FFF, 160 KiB",
          PLAT_DATA_LO == 0xC2000u && PLAT_DATA_HI == 0xEA000u && PLAT_DATA_LO == FL_BIG_LO && PLAT_DATA_HI == FL_BIG_HI &&
          FL_STORE_OK(PLAT_DATA_LO, PLAT_DATA_HI - PLAT_DATA_LO));
    check("update loader: the app area 0x4000-0x9CFFF, records 0x9D000-0xBFFFF only",
          FL_LDR_RANGE_OK(0x4000u, 0x1000u) && FL_LDR_RANGE_OK(0x9C000u, 0x1000u) && FL_LDR_RANGE_OK(0xBF000u, 0x1000u) &&
          !FL_LDR_RANGE_OK(0xC0000u, 0x1000u) && !FL_LDR_RANGE_OK(0xEA000u, 0x1000u) && FL_REC_HI == 0xC0000u);
    check("the OTA staging and its record (ota.c) in the app's window and the loader's record window",
          FL_GUARD(OTA_AREA_T, 0x5000u) && FL_LDR_RANGE_OK(OTA_AREA_T, 0x5000u) && FL_LDR_RANGE_OK(OTA_RES_T, 0x100u) &&
          OTA_RES_T + 0x100u == FL_OTA_HI);
    for (o = 0; o < OBJ_NOBJ; o++)
        for (c = 0; c < 2u; c++) {
            uint32_t a = st_sector(o, c), o2, c2;
            in_store &= FL_GUARD(a, 0x1000u) && a >= FL_DATA_LO && a < FL_OBJ_HI;
            for (o2 = 0; o2 < OBJ_NOBJ; o2++)
                for (c2 = 0; c2 < 2u; c2++)
                    if ((o != o2 || c != c2) && a == st_sector(o2, c2))
                        distinct = 0;
        }
    check("storage.c: every object's two copies in 0x9D000-0xAEFFF, no two share a sector (9 objects)",
          in_store && distinct && st_sector(0, 0) == 0x9D000u && st_sector(OBJ_NOBJ - 1u, 1) == 0xAE000u);
    check("storage.c: the owner tag 0xAF000, after the objects, in the store's window",
          ST_OWNER_OFF == 0xAF000u && FL_GUARD(ST_OWNER_OFF, 32u));
    check("FL_PLAIN_LO = the VM start 0x9D000 (the data area and USRTRIM read in place)", FL_PLAIN_LO == 0x9D000u);
    check("FL_JEDEC_OK: the SMK-37's part 0xEB6014 and the FM-1's 0x856014 only",
          FL_JEDEC_OK(0xEB6014u) && FL_JEDEC_OK(0x856014u) && !FL_JEDEC_OK(0u) && !FL_JEDEC_OK(0xFFFFFFu) &&
          !FL_JEDEC_OK(0xEB6015u));
    printf("%s\n", bad ? "SMK FLASH TEST FAILED" : "smk flash test passed");
    return bad != 0;
}
