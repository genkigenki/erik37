/* SPDX-License-Identifier: MPL-2.0
 * Erik37: Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* Erik37: a grand piano for the M-Vave SMK-37 (and the FM-1), on fm1-base. This header is the app's half of the contract with the
 * platform (base/device/unity.c, base/host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "ERIK37"
#define APP_SPLASH_RGB RGB(201, 164, 92)
#define APP_TAGLINE "a grand piano"
#ifndef APP_VERSION
#define APP_VERSION "1.2"                /* (tools/build.py --release passes its own) */
#endif
#define APP_BLOCK 256u                   /* frames per audio half: 5.8 ms; the keys are read every 64 frames */
#define APP_CANVAS_PX (240u * 48u)       /* fm1-base's canvas (its own screens): the pool goes to Erik37's frame */
#define APP_BIG_FONT FONT_M              /* fm1-base's splash and panel screens: L's 20 KB go to the sound */
#define APP_SPLASH                       /* the first screen: Erik37's own (the boot animation starts after it) */
void app_splash(void);
#define APP_OVERLOAD                     /* the render over 90 % of its time: the quietest released notes go */
void app_overload(void);
#define APP_GAIN(m) ((m) * (m) >> 12)

/* the store: the settings, the panel calibration (the platform's) and the user presets; the SMK-37 build puts them
 * in its own window (the stock VM, 0x9D000..0xAEFFF, by number: the numbers stay) */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_PRESETS, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u, 0xDE000u}
#if !BOARD_SMK37
#define APP_TOUCH                        /* the FM-1's keys: velocity (lib/touch.h) */
#endif

/* the contract: the platform calls these */
void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);

#ifdef PLAT_HOST
#define APP_HOST_EXPECT                  /* the host simulator's checks (tests/ *.txt): page, tone, room, voices ... */
int app_expect(const char *what, int *got);
#endif
