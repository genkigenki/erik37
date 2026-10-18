/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* The M-Vave SMK-37's flash map: which ranges the RAM flash driver (chip/fm1_flash.h), the update entry
 * (chip/ota.c) and the update loader (loader/) may erase or program. Macros and compile-time checks only, so host
 * tests can include it. Hardware notes: https://github.com/genkigenki/fm1-to-smk37
 *
 *   0x00000-0x03FFF  head (SPL, boot config)                        never
 *   0x04000-0x9CFFF  app area (the stock PRCT)                      the update loader only (FL_APP)
 *   0x9D000-0xAEFFF  the store's objects, A/B (fm1-base storage.c)   FL_DATA
 *   0xAF000-0xAFFFF  the store's owner tag                          FL_DATA
 *   0xB0000-0xBAFFF  (stock VM) not written
 *   0xBB000-0xBFFFF  M-UPGRADE loader staging + update record       FL_OTA (the loader: records only)
 *   0xC0000-0xC0FFF  BTIF: Bluetooth pairing                        never
 *   0xC1000-0xC1FFF  USRTRIM: key, pad and wheel calibration        never (read only: "trimdat" at 0xC1224)
 *   0xC2000-0xE9FFF  USRFLASH: the app's own big data (plat_data)   FL_BIG
 *   0xEA000-0xEAFFF  the stock firmware's USRFLASH end marker       erased only, never programmed (FL_STOCKTAG)
 *   0xEB000-0xF3FFF  not in the stock region table                  not written
 *   0xF4000-0xFDFFF  USR: stock resources (kept for a return)       never
 *   0xFE000-0xFEFFF  not listed                                     not written
 *   0xFF000-0xFFFFF  key_mac                                        never
 *
 * Everything writable is an explicit allow-list; the asserts keep each window clear of the protected ranges, and
 * the RAM driver checks every erase and program against the protected ranges a second time (FL_GUARD,
 * FL_ERASE_GUARD), also when a caller supplies its own FL_RANGE_OK (the update loader does). */
#pragma once
#include <stdint.h>

/* [off, off + n) inside [lo, hi), without wrapping: off + n can overflow, and the 1 MiB part ignores the high
 * address bits, so a wrapped range lands low */
#define FL_IN(off, n, lo, hi) ((uint32_t)(off) >= (lo) && (uint32_t)(off) <= (hi) && \
                               (uint32_t)(n) <= (hi) - (uint32_t)(off))
/* the flash parts the app accepts (JEDEC id, 0x9F): the SMK-37's (0xEB6014, read from a unit over mask-ROM UBOOT)
 * and the FM-1's; the driver uses only generic SPI NOR commands (9F, 05/35, 06, 20, 02, 0B) */
#define FL_JEDEC_OK(id) ((id) == 0xEB6014u || (id) == 0x856014u)

#define FL_HEAD_HI      0x00004000u                /* protected: [0, FL_HEAD_HI) */
#define FL_CAL_LO       0x000C0000u                /* protected: BTIF + USRTRIM */
#define FL_CAL_HI       0x000C2000u
#define FL_USRRES_LO    0x000F4000u                /* protected: the stock USR resources */
#define FL_USRRES_HI    0x000FE000u
#define FL_KEYMAC_LO    0x000FF000u                /* protected: key_mac and above */

#define FL_APP_LO       0x00004000u                /* the update loader: the app area */
#define FL_APP_HI       0x0009D000u
#define FL_REC_LO       0x0009D000u                /* the update loader: where it may erase records (the VM) */
#define FL_REC_HI       0x000C0000u
#define FL_PLAIN_LO     0x0009D000u                /* the unencrypted XIP window, from the VM up */
#define FL_DATA_LO      0x0009D000u                /* the store: objects and owner tag */
#define FL_DATA_HI      0x000B0000u
#define FL_OBJ_HI       0x000AF000u                /* the objects end here; the owner tag is the sector after */
#define FL_OTA_LO       0x000BB000u                /* M-UPGRADE loader staging and the update record (ota.c) */
#define FL_OTA_HI       0x000C0000u
#define FL_BIG_LO       0x000C2000u                /* the app's own big data (plat_data): the stock USRFLASH */
#define FL_BIG_HI       0x000EA000u
#define FL_STOCKTAG_LO  0x000EA000u                /* the stock USRFLASH's last sector: its end marker ("usrdata" */
#define FL_STOCKTAG_HI  0x000EB000u                /* at 0xEAFF8). Erased, never programmed: before the first write */
                                                   /* into FL_BIG, so the stock firmware rebuilds its mapping */

/* a window [lo, hi) is allowed: sector aligned, non-empty, and clear of every protected range */
#define FL_WIN_SAFE(lo, hi) ((lo) % 0x1000u == 0u && (hi) % 0x1000u == 0u && (lo) < (hi) && \
                             (lo) >= FL_HEAD_HI && (hi) <= FL_KEYMAC_LO && \
                             ((hi) <= FL_CAL_LO || (lo) >= FL_CAL_HI) && \
                             ((hi) <= FL_USRRES_LO || (lo) >= FL_USRRES_HI))
_Static_assert(FL_WIN_SAFE(FL_DATA_LO, FL_DATA_HI), "SMK: FL_DATA overlaps a protected range");
_Static_assert(FL_WIN_SAFE(FL_OTA_LO, FL_OTA_HI), "SMK: FL_OTA overlaps a protected range");
_Static_assert(FL_WIN_SAFE(FL_APP_LO, FL_APP_HI), "SMK: FL_APP overlaps a protected range");
_Static_assert(FL_WIN_SAFE(FL_REC_LO, FL_REC_HI), "SMK: FL_REC overlaps a protected range");
_Static_assert(FL_WIN_SAFE(FL_BIG_LO, FL_BIG_HI), "SMK: FL_BIG overlaps a protected range");
_Static_assert(FL_WIN_SAFE(FL_STOCKTAG_LO, FL_STOCKTAG_HI) && FL_STOCKTAG_HI - FL_STOCKTAG_LO == 0x1000u,
               "SMK: the stock marker window is one sector, clear of the protected ranges");
_Static_assert(FL_REC_HI == FL_CAL_LO && FL_OTA_HI <= FL_REC_HI && FL_OTA_LO >= FL_REC_LO,
               "SMK: the update record (OTA area) lies in the loader's record window, below BTIF");
_Static_assert(FL_APP_HI <= FL_DATA_LO && FL_DATA_HI <= FL_OTA_LO && FL_OTA_HI <= FL_CAL_LO &&
               FL_CAL_HI <= FL_BIG_LO && FL_BIG_HI <= FL_STOCKTAG_LO && FL_STOCKTAG_HI <= FL_USRRES_LO,
               "SMK: the windows overlap or are out of order");
_Static_assert(FL_OBJ_HI > FL_DATA_LO && FL_OBJ_HI + 0x1000u == FL_DATA_HI, "SMK: the owner tag is FL_DATA's last sector");

/* the update loader (loader/loader.c): the app area, and records only in the VM below BTIF */
#define FL_LDR_RANGE_OK(off, n) (FL_IN(off, n, FL_APP_LO, FL_APP_HI) || FL_IN(off, n, FL_REC_LO, FL_REC_HI))
/* the app's flash: the store and its big data */
#define FL_STORE_OK(off, n) (FL_IN(off, n, FL_DATA_LO, FL_DATA_HI) || FL_IN(off, n, FL_BIG_LO, FL_BIG_HI))
#ifndef FL_RANGE_OK
#define FL_RANGE_OK(off, n) (FL_STORE_OK(off, n) || FL_IN(off, n, FL_OTA_LO, FL_OTA_HI))
#endif
/* the stock marker: erase only (the app's FL_RANGE_OK never covers it; FL_ERASE_GUARD adds it for erases) */
#ifndef FL_STOCKTAG_OK
#define FL_STOCKTAG_OK(off, n) ((uint32_t)(off) == FL_STOCKTAG_LO && (uint32_t)(n) == 0x1000u)
#endif

/* the second, independent net in the RAM driver: [off, off + n) touches a protected range */
#define FL_HITS(off, n, lo, hi) ((uint32_t)(off) < (hi) && \
                                 ((uint32_t)(off) >= (lo) || (uint32_t)(n) > (lo) - (uint32_t)(off)))
#define FL_PROTECTED(off, n) (FL_HITS(off, n, 0u, FL_HEAD_HI) || FL_HITS(off, n, FL_CAL_LO, FL_CAL_HI) || \
                              FL_HITS(off, n, FL_USRRES_LO, FL_USRRES_HI) || (uint32_t)(off) >= FL_KEYMAC_LO || \
                              (uint32_t)(n) > FL_KEYMAC_LO - (uint32_t)(off))
#define FL_GUARD(off, n) (FL_RANGE_OK(off, n) && !FL_PROTECTED(off, n))
#define FL_ERASE_GUARD(off, n) ((FL_RANGE_OK(off, n) || FL_STOCKTAG_OK(off, n)) && !FL_PROTECTED(off, n))
