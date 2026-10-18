/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* tests/wifi_scan_test.c: the shim's types and calls device/wifi.c uses (the test defines them) */
#pragma once
#include <stdint.h>
typedef uint8_t u8;
typedef uint32_t u32;
uint32_t shim_ms(void);
void shim_log(const char *s);
void shim_timers(void);
void shim_run(void);
int task_create(void (*fn)(void *), void *p, const char *name);
