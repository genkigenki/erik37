/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal (from CTL-1 and MONK-1) */
/* Velocity and pressure from the FM-1's keys, with no change to the hardware (an app that defines APP_TOUCH in
 * app.h; lib/touch.c, on lib/sense.c). Off by default: touch_on(1) starts the scan's readings (plat_sense).
 *
 * While it is on, the 20 keys on the analog rows (F3 to C5) are sensed: their presses come from here (touch_take,
 * or touch_poll and touch_keys), with the velocity they landed at, and a held one has a pressure. The seven keys
 * of the top row (C#5 to G5) have no converter: plat_keys sees them as usual, and touch_vel_contact gives the
 * velocity they were pressed at, from a probe through the LED line (no pressure).
 *
 * Two ways to read it, pick one:
 *   events    touch_take: each press (TOUCH_PRESS, with its velocity) and release of a sensed key, in order; a key
 *             that bounces open is pressed again without a release (a retrigger). Call touch_update every frame
 *             for the pressure.
 *   state     touch_poll every frame, then touch_keys (every key held, sensed or not: plat_keys for the top row)
 *             and touch_vel (the velocity of a key's last press); touch_poll keeps the pressure too. */
#pragma once
#include <stdint.h>

#define APP_KEY_SENSE                   /* the scan's readings come here (device/unity.c, host/plat_host.c) */
void app_key_sample(uint32_t key, uint32_t v, uint32_t ticks, int driven);
int app_key_drive(uint32_t key);
void app_key_probe(uint32_t key, uint32_t v, uint32_t pressed);
int app_key_led_ok(uint32_t key);

enum { TOUCH_PRESS = 1, TOUCH_RELEASE = 2 };
typedef struct { uint8_t type, key, vel; } touch_ev_t;

void touch_on(int on);                   /* the readings on or off; let every key go first (the keys change hands) */
int touch_is_on(void);
void touch_curve(int curve);             /* velocity: -5 .. +5, 0 as measured; + makes 127 harder to reach */
void touch_sens(int sens);               /* pressure: -5 .. +5, 0 as measured; + full pressure with less lean */
int touch_sensed(uint32_t key);          /* key is on an analog row: its presses come from here while on */
int touch_take(touch_ev_t *e);           /* events: the next one, 0 when none (main loop) */
void touch_update(void);                 /* events: the pressure, every frame (it moves every 5 ms) */
void touch_poll(void);                   /* state: the events into touch_keys / touch_vel, and the pressure */
uint32_t touch_keys(void);               /* state: every key held, bit k = key k */
uint8_t touch_vel(uint32_t key);         /* state: the velocity of the key's last press, 1..127 */
uint8_t touch_vel_contact(uint32_t key); /* a top-row key just pressed: its velocity, 0 when there is no reading */
uint8_t touch_pressure(uint32_t key);    /* a held sensed key: how much harder it is pressed than it rests, 0..127 */
int touch_down(uint32_t key);            /* a sensed key is seated (its pressure counts; lifting, it does not) */

/* the host simulator: what a key's readings would be (host/plat_host.c "key K down VEL PRESS") */
void app_host_key(uint32_t k, uint32_t vel, uint32_t press);
void app_host_tick(void);
