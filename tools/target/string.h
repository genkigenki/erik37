/* SPDX-License-Identifier: MPL-2.0
 * Erik37: Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* freestanding shim for the pi32v2 cost build (tools/cpu_budget.py) */
#ifndef ERK_TARGET_STRING_H
#define ERK_TARGET_STRING_H
typedef __SIZE_TYPE__ size_t;
void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
#endif
