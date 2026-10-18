/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal (from CTL-1 and the template) */
/* Bluetooth MIDI devices for an app's page (an app that defines APP_BTDEV in app.h; lib/btdev.c, on plat_ble_*).
 * The app draws the page its own way; this keeps what every app would write again: the scan while the page is up,
 * the devices found and the one picked, SAVE's connect or let go, and what happened, in words for the screen and
 * a screen reader (put them in the app's words.tsv).
 *
 * Every frame: btdev_page(up), then btdev_event() until it says BTDEV_NONE. Without FM1B_BLE_CENTRAL (fm1.toml
 * ble_central) nothing is found and only the computer's events come. */
#pragma once
#include <stdint.h>

enum {
    BTDEV_NONE,
    BTDEV_CONNECTING,     /* SAVE: asked the pick to connect */
    BTDEV_CONNECTED,      /* the device is connected, MIDI both ways */
    BTDEV_NO_ANSWER,      /* it never answered, or its MIDI was not found: given up */
    BTDEV_LOST,           /* a connected device went away */
    BTDEV_LET_GO,         /* SAVE: disconnected */
    BTDEV_NO_DEVICE,      /* SAVE with nothing found */
    BTDEV_COMPUTER,       /* a computer connected to the FM-1 */
    BTDEV_COMPUTER_GONE,  /* and went */
};

void btdev_page(int up);             /* every frame: the app's device page is up (it scans while it is, unconnected) */
int btdev_event(void);               /* every frame, after btdev_page: what happened (BTDEV_*), each once */
const char *btdev_word(int ev);      /* for the screen and the reader: "CONNECTED", "NO ANSWER", "DEVICE LOST"... */
int btdev_count(void);               /* devices found (up to 8) */
int btdev_found(int i, char *name, int *rssi);   /* device i (name: 21 bytes), 0 = none */
int btdev_pick(void);                /* the one SAVE connects to */
void btdev_choose(int i);            /* (kept within what was found) */
int btdev_busy(void);                /* connecting or connected: SAVE lets it go */
int btdev_ready(void);               /* connected, MIDI both ways */
const char *btdev_name(void);        /* the device connecting or connected, or the last one asked ("" none) */
int btdev_save(void);                /* SAVE: connect the pick, or let the device go; returns BTDEV_CONNECTING,
                                      * BTDEV_LET_GO or BTDEV_NO_DEVICE (not queued: the app says it) */
void btdev_title(char *t);           /* 32 bytes: "Connected: X", "Connecting: X", "MIDI devices",
                                      * "Looking for MIDI devices" */
