/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal (from fm1-ble and fm1-ctl) */
/* A build with no radio (neither FM1B_BLE nor FM1B_WIFI): what radio.c gives the rest of the firmware, with nothing
 * behind it. Nothing from JieLi's libraries or the shim is linked. Part of the unity build (unity.c). */
static const uint8_t radio_quiet = 0;             /* the render never holds for the radio */
static const uint8_t radio_last_bad = 0;
static void radio_boot_note(void) {}
static void radio_clean_exit(void) {}
#include "ble_none.c"
void plat_log(const char *line) { (void)line; }
const char *plat_ble_log_line(int back) { (void)back; return ""; }
uint32_t plat_ble_log_total(void) { return 0; }
