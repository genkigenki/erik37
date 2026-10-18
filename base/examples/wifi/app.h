/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* The WiFi example: fm1-base's WiFi (fm1.toml wifi = true) at its smallest. It turns WiFi on, joins the networks
 * kept, shows the state, the network, the address and the signal, lists the networks around, and fetches a page
 * over HTTP from a task of its own (net.c). Passwords come over USB: tools/fm1_remote.py wifi join SSID PASSWORD. */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "WIFI"
#define APP_SPLASH_RGB RGB(72, 160, 242)
#define APP_BLOCK 256u
#define APP_CANVAS_PX (240u * 48u)       /* a strip at a time: WiFi's buffers need the pool (gfx.c) */

/* the store: the networks kept (OBJ_WIFI, the platform's WiFi), the radio's address and calibration (OBJ_BLE),
 * the panel calibration (the platform's) */
enum { OBJ_PANEL, OBJ_BLE, OBJ_WIFI, OBJ_NOBJ };
#define APP_STORE_MAP {0xDC000u, 0xDE000u, 0xDA000u}

/* the contract: the platform calls these */
void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);

/* net.c (built apart, with lwIP's headers): a page fetched over HTTP in a task of its own */
int net_fetch(const char *host, const char *path);   /* 0 = started; -1 = one is running */
int net_busy(void);
const char *net_result(void);            /* the status line it got, or what went wrong */
uint32_t net_bytes(void);                /* bytes received */
