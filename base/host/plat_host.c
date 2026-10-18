/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* fm1-base on the host: the whole app (UI, engine, saved state) against a simulated FM-1 driven by a script.
 * Audio goes to a WAV, the screen to PNGs, the lights to text, MIDI out to a log. The device-only
 * parts (the BSP, USB, update, flash driver) are replaced by plat.h implemented here. (From X0X's x0x_host, by way
 * of FoMni-1's.) Built by tools/build.py APP_DIR --host into APP_DIR/build/host/NAME_host.
 *
 *   NAME_host SCRIPT [OUTDIR]
 *
 * Script, one command per line (# comments):
 *   wait MS                      run the device for MS milliseconds
 *   press BTN | release BTN | tap BTN     (BTN: FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+)
 *   key K down|up [VEL [PRESS]] | tapkey K [VEL]   (K: 0..26 = F3..G5, or w0..w15 white keys, b0..b10 black keys;
 *                                with APP_KEY_SENSE, the key lands at VEL 1..127, default 100, and is pressed PRESS
 *                                0..100 % more: the app's app_host_key makes the readings)
 *   strum A B [MS]               white keys A..B pressed in turn (down then up), MS apart (default 25)
 *   turn ENC N [MS]              (ENC: SELECT ALGO PRESET K1 K2 K3 K4; N detents, MS apart, default 80)
 *   spin ENC N                   N detents at once, exactly N (no acceleration)
 *   master N                     MASTER pot 0..4096 (then 40 ms, as the device's smoothing settles)
 *   midi [PORT] B0 B1 B2         incoming MIDI message (PORT: USB, the default, BLE or TRS; hex bytes)
 *   ble connect | ble link | ble disconnect   a central listening, or linked and not yet listening (FM1B_BLE: the
 *                                stand-in stack is ready a second after the start)
 *   wav FILE | wavstop           start / stop recording the output
 *   peakreset                    restart the output peak (expect peak_db_min / peak_db_max)
 *   shot FILE.png                save the screen
 *   leds                         print the lit buttons and keys (keys: # bright, + dim)
 *   out                          print the MIDI sent since the last "out" (and forget it; clocks not shown)
 *   expect_out PORT B0 B1 B2     check that this message went out to PORT since the last "out"
 *   expect WHAT VALUE            check state: exit 1 on mismatch; VALUE "<N" / ">N" is a bound
 *   reboot                       re-run boot from the simulated flash (persistence test)
 *   data FILE OFFSET             a file into the simulated data area (plat_data) at OFFSET (0x97000..)
 *   listen                       a screen reader starts listening (HELLO, again every 2 s): each SAY is printed
 *                                ("said: ..."; "said!:" cuts off what was being said)
 *   where                        the reader asks for everything on the screen
 *   said TEXT                    some SAY since the last "said" (or "listen") has TEXT in it: else a failure
 *   unsaid TEXT                  no SAY since the last "said" has TEXT in it: else a failure
 *   speakdump                    every page, value and message as the reader gets them (the app's app_speak_dump)
 * The app may add its own checks for expect (app_expect), the speakdump (app_speak_dump), what a reboot forgets
 * (app_host_reboot) and commands (app_host_cmd: 1 = handled): app.h defines APP_HOST_EXPECT / APP_HOST_SPEAKDUMP /
 * APP_HOST_REBOOT / APP_HOST_CMD when it has them. With APP_KEY_SENSE, app_host_key(k, vel, press) (vel 0 = up)
 * makes the readings a key gives, from app_host_tick (every millisecond) while plat_sense is on.
 * FM1B_SAID_LOG=FILE: a reader listens from boot, quietly, and every SAY is appended to FILE (the audit).
 * The M-Vave SMK-37 (BOARD_SMK37, tools/build.py --board smk37 --host) adds (Copyright (C) 2026 M. Geisler
 * (genkigenki/makkuro)):
 *   press / release / tap KNOB_BANK | FADER_BANK | LEFT | RIGHT   its four buttons of its own
 *   pad P down|up [VEL] | tappad P                                pad P (0..7 the top row, 8..15 the bottom row)
 *   keybed K down|up | tapbed K                                    keybed key K (0 = C3 .. 36 = C6; 5..31 are F3..G5)
 *   fader I V                    fader I (0..3) at V (0..127)
 *   wheel pitch|mod V            the pitch wheel (-127..127, 0 at rest) or the mod wheel (0..127)
 *   pads                         print the pads' colours (RRGGBB, the top row first) and the four buttons' LEDs
 *   expect padN 0xRRGGBB | expect smk_leds N                       a pad's colour, the four LEDs (bits SB_*)
 */
#define PLAT_HOST 1
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L       /* clock_gettime under -std=c99 */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#define __attribute__(x)

/* ------------------------------------------------------------ screen --- */
static uint16_t fb[240 * 240];          /* RGB565, byte-swapped as the panel takes it (gfx.c) */
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    uint16_t sc = (uint16_t)(((c >> 8) & 0xFFu) | ((c & 0xFFu) << 8));
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            fb[j * 240u + i] = sc;
}
static uint32_t blits;
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    blits++;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u)
                fb[(y + j) * 240u + x + i] = px[j * w + i];
}
#include "plat.h"
#include "app.h"                       /* before gfx.c: the app may size the canvas (APP_CANVAS_PX), as on the device */
#include "gfx.c"


/* ---------------------------------------------------------- platform --- */
#ifdef APP_TOUCH
#include "touch.h"                     /* velocity and pressure from the keys (lib/touch.c) */
#endif
#include "fm1b_config.h"
static const uint8_t HOST_WHITE_K[16] = PLAT_WHITE_KEYS, HOST_BLACK_K[11] = PLAT_BLACK_KEYS;
/* "spin": encoder steps exactly as given; an app that accelerates its knobs reads this (#ifdef PLAT_HOST) */
static uint8_t plat_host_exact;

static uint32_t now_ms, held_btn, held_keys, master = 2048;
static int32_t enc_acc[NE];
static uint32_t lit_btn, lit_keys, dim_btn, dim_keys;

uint32_t plat_ms(void) { return now_ms; }
uint32_t plat_buttons(void) { return held_btn; }
#if BOARD_SMK37
/* the SMK-37's pads, keybed, faders, wheels and their lights (plat.h), from the script */
static uint32_t hsmk_sb, hsmk_led_lit, hsmk_pads_held, hsmk_pad_hit, hsmk_pad_rgb[SMK_NPADS];
static uint64_t hsmk_bed, hsmk_bed_hit;
static uint8_t hsmk_bed_vel[SMK_NKEYBED];   /* each key's last velocity ("keybed K down VEL", default 100) */
static int hsmk_pedal = 1023;               /* the PEDAL jack ("pedal RAW"): an open jack reads high */
static int hsmk_layout = SMK_PADS_PIANO, hsmk_pad_app, hsmk_fad[4] = {-1, -1, -1, -1}, hsmk_wh_new;
static int32_t hsmk_wh_pitch, hsmk_wh_mod;
static uint32_t hsmk_pad_white(uint32_t pads)    /* the pads as white keys, as the layout lays them out */
{
    uint32_t w, out = 0;
    for (w = 0; w < 16u; w++)
        if (pads >> (hsmk_layout == SMK_PADS_PIANO ? (w < 8u ? w + 8u : w - 8u) : w) & 1u)
            out |= 1u << HOST_WHITE_K[w];
    return out;
}
uint32_t plat_keys(void) { return held_keys | (hsmk_layout == SMK_PADS_OWN ? 0u : hsmk_pad_white(hsmk_pads_held)); }
void plat_smk_pads(int layout) { hsmk_layout = layout; }
uint32_t plat_smk_buttons(void) { return hsmk_sb; }
void plat_smk_leds(uint32_t lit) { hsmk_led_lit = lit; }
uint64_t plat_keybed(void) { return hsmk_bed; }
int plat_keybed_vel(int k) { return k >= 0 && k < SMK_NKEYBED ? hsmk_bed_vel[k] : 0; }
int plat_pedal_raw(void) { return hsmk_pedal; }
uint64_t plat_keybed_hits(void)
{
    uint64_t h = hsmk_bed_hit;
    hsmk_bed_hit = 0;
    return h;
}
uint32_t plat_pad_hits(void)
{
    uint32_t h = hsmk_pad_hit;
    hsmk_pad_hit = 0;
    return hsmk_layout == SMK_PADS_OWN ? h : 0u;
}
uint32_t plat_pads_held(void) { return hsmk_pads_held; }
int plat_fader(int i) { return i >= 0 && i < 4 ? hsmk_fad[i] : -1; }
int plat_wheels(int32_t *pitch, int32_t *modw)
{
    if (!hsmk_wh_new)
        return 0;
    hsmk_wh_new = 0;
    *pitch = hsmk_wh_pitch;
    *modw = hsmk_wh_mod;
    return 1;
}
void plat_pad_rgb(int pad, uint8_t r, uint8_t g, uint8_t b)
{
    if (pad >= 0 && pad < SMK_NPADS)
        hsmk_pad_rgb[pad] = (uint32_t)r << 16 | (uint32_t)g << 8 | b;
}
void plat_pad_lights(int app) { hsmk_pad_app = app; }
#else
uint32_t plat_keys(void) { return held_keys; }
#endif
static uint32_t plat_remote_keys(void) { return 0; }   /* (the device's remote control: the script is this one's) */
int32_t plat_enc(int role)
{
    int32_t v = enc_acc[role];
    enc_acc[role] = 0;
    return v;
}
uint32_t plat_master(void) { return master; }
/* the lights ("leds"): bright, and dim (glow 0: none); PLAY's green counts as PLAY */
void plat_lights(uint32_t b, uint32_t k, uint32_t db, uint32_t dk, int glow)
{
    lit_btn = (b & ((1u << NB) - 1u)) | (b & LED_PLAY_GREEN ? 1u << B_PLAY : 0u);
    lit_keys = k;
    db = (db & ((1u << NB) - 1u)) | (db & LED_PLAY_GREEN ? 1u << B_PLAY : 0u);
    dim_btn = glow ? db & ~lit_btn : 0u;
    dim_keys = glow ? dk & ~k : 0u;
}
static int glow_on = 1;                 /* the device's button glow (plat_glow) */
void plat_glow(int on) { glow_on = on; }
void plat_play_red(int red) { (void)red; }      /* the device's PLAY colour: green, red while recording */
void plat_leds(uint32_t b, uint32_t k) { plat_lights(b, k, glow_on ? ~b : 0u, 0, glow_on ? 2 : 0); }
uint32_t plat_lock(void) { return 0; }  /* one thread: the tick runs between the main loop's steps (run_ms) */
void plat_unlock(uint32_t f) { (void)f; }
void plat_yield(void)
{
#ifdef APP_YIELD
    app_yield();
#endif
}
static int sense_on;                    /* the keys' readings: the simulator has none to give (sense_on only) */
void plat_sense(int on) { sense_on = on; }
uint32_t plat_analog_mask(void) { return 0xFFFFFu; }    /* the keys on rows PA7 / PA8: keys 0..19 (the top row: 20..26) */

#define MQ 256
static uint32_t min_q[MQ], mi_w, mi_r;
static FILE *midi_log;
static uint32_t midi_out_count, clock_out_count;
static uint32_t min_port[MQ];
static uint32_t out_pkt[4096], out_to[4096], n_out;   /* what went out since the script's last "out" */
static int ble_state, ble_conn, bdev_state;   /* bdev_state: the Bluetooth MIDI device (BLE_DEV_*) */
static uint32_t midi_n[NPORTS][2], midi_t[NPORTS][2];   /* plat_midi_count */
static void midi_counted(int port, int out, uint32_t pkt)
{
    if ((pkt >> 8 & 0xFFu) >= 0xF8u)             /* clock, start, stop, active sensing: not counted, no light */
        return;
    midi_n[port][out]++;
    midi_t[port][out] = plat_ms() | 1u;
}
uint32_t plat_midi_count(int port, int out) { return port >= 0 && port < NPORTS ? midi_n[port][!!out] : 0; }
uint32_t plat_midi_ago(int port, int out)
{
    return port < 0 || port >= NPORTS || !midi_t[port][!!out] ? 0xFFFFFFFFu : plat_ms() - midi_t[port][!!out];
}
int plat_midi_in(uint32_t *pkt, int *port)       /* the script's "midi" */
{
    int p;
    if (mi_r == mi_w)
        return 0;
    p = (int)min_port[mi_r % MQ];
    if (port)
        *port = p;
    *pkt = min_q[mi_r++ % MQ];
    if (p >= 0 && p < NPORTS)
        midi_counted(p, 0, *pkt);
    return 1;
}
void plat_midi_send(uint32_t pkt, uint32_t to)
{
    to &= TO_USB | TO_BLE;
    if ((to & TO_BLE) && !ble_conn && bdev_state != BLE_DEV_READY)   /* as the device: nothing queues for no one */
        to &= ~TO_BLE;
    if (!to)
        return;
    if (to & TO_USB)
        midi_counted(PORT_USB, 1, pkt);
    if (to & TO_BLE)
        midi_counted(PORT_BLE, 1, pkt);
    midi_out_count++;
    if (n_out < 4096) {
        out_pkt[n_out] = pkt;
        out_to[n_out++] = to;
    }
    if (((pkt >> 8) & 0xFF) == 0xF8) {
        clock_out_count++;
        return;
    }
    if (midi_log)
        fprintf(midi_log, "%u ms: %02X %02X %02X ->%s%s\n", now_ms, (pkt >> 8) & 0xFF, (pkt >> 16) & 0xFF,
                (pkt >> 24) & 0xFF, to & TO_USB ? " USB" : "", to & TO_BLE ? " BLE" : "");
}

/* Bluetooth (FM1B_BLE): a stand-in for the stack, ready a second after the start, and its log */
static uint32_t ble_t0;
static char ble_log[8][40];
static uint32_t ble_log_n;
static void ble_say(const char *s) { snprintf(ble_log[ble_log_n++ % 8], 40, "%s", s); }
int plat_ble_state(void) { return ble_state; }
static int ble_link;                            /* "ble link": a computer linked, not listening yet */
int plat_ble_connected(void) { return ble_conn; }
int plat_ble_linked(void) { return ble_conn || ble_link; }
void plat_ble_start(void)
{
    if (ble_state != BLE_OFF || !FM1B_BLE)
        return;
    ble_state = BLE_STARTING;
    ble_t0 = now_ms;
    ble_log_n = 0;
    ble_say("BT: starting");
}
/* Bluetooth MIDI devices (FM1B_BLE_CENTRAL): one stand-in, "BLE MIDI", found half a second into a scan;
 * connecting takes 0.3 s; "ble device off" takes it away */
static uint32_t bdev_t;
static void ble_tick(void)
{
    if (ble_state == BLE_STARTING && now_ms - ble_t0 >= 1000u) {
        ble_state = BLE_READY;
        ble_say("BT init ok: starting BLE");
    }
    if (bdev_state == BLE_DEV_CONNECTING && now_ms - bdev_t >= 300u) {
        bdev_state = BLE_DEV_READY;
        ble_say("central: MIDI ready");
    }
}
void plat_ble_scan(int on)
{
    if (!FM1B_BLE_CENTRAL || ble_state != BLE_READY)
        return;
    if (on && bdev_state == BLE_DEV_OFF) {
        bdev_state = BLE_DEV_SCANNING;
        bdev_t = now_ms;
    } else if (!on && bdev_state == BLE_DEV_SCANNING) {
        bdev_state = BLE_DEV_OFF;
    }
}
int plat_ble_found(int i, char *name, int *rssi)
{
    if (bdev_state != BLE_DEV_SCANNING || i != 0 || now_ms - bdev_t < 500u)
        return 0;
    if (name)
        snprintf(name, 21, "BLE MIDI");
    if (rssi)
        *rssi = -50;
    return 1;
}
int plat_ble_connect(int i)
{
    if (!plat_ble_found(i, 0, 0))
        return -1;
    bdev_state = BLE_DEV_CONNECTING;
    bdev_t = now_ms;
    return 0;
}
void plat_ble_disconnect(void)
{
    if (bdev_state == BLE_DEV_READY)
        ble_say("central: disconnected");
    bdev_state = BLE_DEV_OFF;
}
int plat_ble_device(void) { return bdev_state; }
void plat_ble_enable(int on)
{
    if (!on) {
        bdev_state = BLE_DEV_OFF;
        ble_conn = ble_link = 0;
        ble_state = BLE_OFF;
        ble_say("switched off");
    } else {
        plat_ble_start();
    }
}
#if FM1B_WIFI
/* WiFi (FM1B_WIFI): a stand-in, two networks around ("Home", kept, and "Cafe", open), on 0.5 s after asked, joined
 * 1.5 s after (an address 0.5 s later); the network code (fm1.toml net) is the device's alone: plat_net_task runs
 * nothing here */
static uint8_t wf_st;
static uint32_t wf_t;
static char wf_ssid[33], wf_kept[6][33] = {"Home"};
static void wifi_tick(void)
{
    if (wf_st == WIFI_STARTING && now_ms - wf_t >= 500u) {
        wf_st = wf_kept[0][0] ? WIFI_JOINING : WIFI_ON;
        snprintf(wf_ssid, sizeof wf_ssid, "%s", wf_kept[0]);
        wf_t = now_ms;
    }
    if (wf_st == WIFI_JOINING && now_ms - wf_t >= 1000u) {
        wf_st = WIFI_ADDRESS;
        wf_t = now_ms;
    }
    if (wf_st == WIFI_ADDRESS && now_ms - wf_t >= 500u)
        wf_st = WIFI_CONNECTED;
}
void plat_wifi_on(int on)
{
    if (on && wf_st == WIFI_OFF) {
        wf_st = WIFI_STARTING;
        wf_t = now_ms;
        wf_ssid[0] = 0;
    } else if (!on) {
        wf_st = WIFI_OFF;
    }
}
int plat_wifi_state(void) { return wf_st; }
const char *plat_wifi_why(void) { return ""; }
const char *plat_wifi_ssid(void) { return wf_ssid; }
uint32_t plat_wifi_ip(void) { return wf_st == WIFI_CONNECTED ? 0x2A01A8C0u : 0; }   /* 192.168.1.42 */
int plat_wifi_rssi(void) { return wf_st == WIFI_CONNECTED ? -48 : 0; }
static uint32_t wf_scans;                       /* scans asked (each one's results new, as on the device) */
int plat_wifi_scan(void) { return wf_st >= WIFI_ON ? (wf_scans++, 0) : -1; }
int plat_wifi_found(int i, char *ssid, int *rssi, int *secured)
{
    static const char *const N[2] = {"Home", "Cafe"};
    if (wf_st < WIFI_ON || i < 0 || i > 1)
        return 0;
    if (ssid)
        snprintf(ssid, 33, "%s", N[i]);
    if (rssi)
        *rssi = i ? -71 : -48;
    if (secured)
        *secured = !i;
    return 1;
}
int plat_wifi_join(const char *ssid, const char *pass)
{
    (void)pass;
    if (wf_st < WIFI_ON || !ssid || !ssid[0])
        return -1;
    snprintf(wf_ssid, sizeof wf_ssid, "%s", ssid);
    wf_st = WIFI_JOINING;
    wf_t = now_ms;
    return 0;
}
int plat_wifi_saved(int i, char *ssid)
{
    if (i < 0 || i > 5 || !wf_kept[i][0])
        return 0;
    if (ssid)
        snprintf(ssid, 33, "%s", wf_kept[i]);
    return 1;
}
void plat_wifi_forget(int i)
{
    if (i >= 0 && i < 6)
        memmove(wf_kept[i], wf_kept[i + 1], (size_t)(5 - i) * sizeof wf_kept[0]), memset(wf_kept[5], 0, 33);
}
int plat_wifi_kept(const char *ssid)
{
    int i;
    for (i = 0; ssid && i < 6; i++)
        if (wf_kept[i][0] && !strcmp(wf_kept[i], ssid))
            return 1;
    return 0;
}
uint32_t plat_wifi_scans(void) { return wf_st >= WIFI_ON ? 1u + wf_scans : 0u; }
void plat_wifi_rx(uint32_t *frames, int *rssi)
{
    if (frames)
        *frames = wf_st >= WIFI_ON ? 40u : 0u;
    if (rssi)
        *rssi = wf_st >= WIFI_ON ? -55 : 0;
}
int plat_net_task(void (*fn)(void *), void *arg, const char *name) { (void)fn, (void)arg, (void)name; return -1; }
#endif
uint32_t plat_ble_log_total(void) { return ble_log_n; }
const char *plat_ble_log_line(int back)
{
    if (back < 0 || (uint32_t)back >= ble_log_n || back >= 8)
        return "";
    return ble_log[(ble_log_n - 1u - (uint32_t)back) % 8];
}
void plat_midi_out(uint32_t pkt) { plat_midi_send(pkt, TO_USB); }
void plat_log(const char *line) { ble_say(line); }
/* the data area (plat.h): a simulated flash, erased at the start, kept across a simulated reboot */
static uint8_t data_area[PLAT_DATA_HI - PLAT_DATA_LO];
static uint8_t data_ready;
static void data_init(void)
{
    if (!data_ready) {
        memset(data_area, 0xFF, sizeof data_area);
        data_ready = 1;
    }
}
const uint8_t *plat_data(uint32_t off)
{
    data_init();
    return off >= PLAT_DATA_LO && off < PLAT_DATA_HI ? &data_area[off - PLAT_DATA_LO] : 0;
}
int plat_data_erase(uint32_t off)
{
    data_init();
    if ((off & 0xFFFu) || off < PLAT_DATA_LO || off + 0x1000u > PLAT_DATA_HI)
        return -1;
    memset(&data_area[off - PLAT_DATA_LO], 0xFF, 0x1000u);
    return 0;
}
int plat_data_write(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    data_init();
    if (off < PLAT_DATA_LO || n > PLAT_DATA_HI - off)
        return -1;
    for (i = 0; i < n; i++)                       /* as flash: bits only go from 1 to 0 */
        data_area[off - PLAT_DATA_LO + i] &= ((const uint8_t *)src)[i];
    return 0;
}
void plat_sysex_send(const uint8_t *frame, uint32_t n) { (void)frame, (void)n; }

/* flash: one buffer per object, kept across a simulated reboot */
static uint8_t store[OBJ_NOBJ][PLAT_STORE_MAX];
static int store_len[OBJ_NOBJ];           /* -1 = nothing saved (store_ready) */
static uint32_t store_writes;
static void store_ready(void)             /* the flash starts empty: once, on first use (a "reboot" keeps it) */
{
    static int done;
    uint32_t i;
    if (done)
        return;
    done = 1;
    for (i = 0; i < OBJ_NOBJ; i++)
        store_len[i] = -1;
}
int plat_store_load(uint32_t obj, void *dst, uint32_t max)
{
    int n;
    store_ready();
    if (obj >= OBJ_NOBJ || (n = store_len[obj]) < 0)
        return -1;
    if ((uint32_t)n > max)
        n = (int)max;
    memcpy(dst, store[obj], (size_t)n);
    return n;
}
int plat_store_save(uint32_t obj, const void *src, uint32_t len)
{
    store_ready();
    if (obj >= OBJ_NOBJ || len > PLAT_STORE_MAX)
        return -1;
    memcpy(store[obj], src, len);
    store_len[obj] = (int)len;
    store_writes++;
    return 0;
}

static uint32_t cpu_pct;
uint32_t plat_cpu_pct(void) { return cpu_pct; }
uint32_t plat_xruns(void) { return 0; }

/* -------------------------------------------------------------- app --- */
#define ANN_NAME APP_NAME " " APP_VERSION
#include "ann_words.h"                                  /* the words file's link (tools/gen_words.py) */
#include "announce.c"
/* what the reader is told: every SAY to stdout with "said:" (listen), and kept for the "said" check */
static char said_last[ANN_MAX + 1], said_log[16384];   /* said_log: every SAY since the last "said" check */
static uint32_t said_len;
static int said_print = 1, sim_listen;
static FILE *said_log_f;                                /* FM1B_SAID_LOG=FILE: every SAY, for fm1-reader's audit */
#ifdef PLAT_WEB
static void web_said(const uint8_t *p, uint32_t n);
#endif
static int ann_wire_send(const uint8_t *p, uint32_t n)
{
#ifdef PLAT_WEB
    web_said(p, n);
#endif
    if (n >= 7u && p[4] == ANN_SAY) {
        uint32_t i, k = 0;
        for (i = 6; i + 1u < n && k < ANN_MAX; i++)
            said_last[k++] = (char)p[i];
        said_last[k] = 0;
        if (said_len + k + 2u < sizeof said_log) {
            memcpy(said_log + said_len, said_last, k);
            said_len += k;
            said_log[said_len++] = '\n';
            said_log[said_len] = 0;
        }
#ifndef PLAT_WEB
        if (said_print)
            printf("said%s: %s\n", (p[5] & ANN_NOW) ? "!" : "", said_last);
        if (said_log_f)
            fprintf(said_log_f, "%s\t%s\n", (p[5] & ANN_KEEP) ? "WARN" : "MSG", said_last);
#endif
    }
    return 0;
}
#ifdef APP_TOUCH
#include "touch.c"
#endif
#ifdef APP_BTDEV
#include "btdev.c"
#endif
#include "app.c"

/* ---------------------------------------------------------- outputs --- */
static void png_write(const char *path)
{
    /* RGB PNG with stored (uncompressed) deflate blocks: no zlib needed */
    static uint8_t raw[240 * (1 + 240 * 3)];
    FILE *f = fopen(path, "wb");
    uint32_t crc_t[256], i, j, k, n = 0, a = 1, b = 0, pos, len = sizeof raw;
    uint8_t hdr[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (!f) {
        fprintf(stderr, "cannot write %s\n", path);
        return;
    }
    for (i = 0; i < 256; i++) {
        uint32_t c = i;
        for (k = 0; k < 8; k++)
            c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_t[i] = c;
    }
    for (j = 0; j < 240; j++) {
        raw[n++] = 0;
        for (i = 0; i < 240; i++) {
            uint16_t p = fb[j * 240 + i];
            p = (uint16_t)((p >> 8) | (p << 8));
            raw[n++] = (uint8_t)(((p >> 11) & 31) * 255 / 31);
            raw[n++] = (uint8_t)(((p >> 5) & 63) * 255 / 63);
            raw[n++] = (uint8_t)((p & 31) * 255 / 31);
        }
    }
    for (i = 0; i < len; i++) {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
#define PUT32(v) do { uint8_t q_[4] = {(uint8_t)((v) >> 24), (uint8_t)((v) >> 16), (uint8_t)((v) >> 8), (uint8_t)(v)}; fwrite(q_, 1, 4, f); } while (0)
    fwrite(hdr, 1, 8, f);
    {
        uint8_t ih[17] = {'I', 'H', 'D', 'R', 0, 0, 0, 240, 0, 0, 0, 240, 8, 2, 0, 0, 0};
        uint32_t c = 0xFFFFFFFFu;
        PUT32(13u);
        fwrite(ih, 1, 17, f);
        for (i = 0; i < 17; i++)
            c = crc_t[(c ^ ih[i]) & 255] ^ (c >> 8);
        PUT32(~c);
    }
    {
        uint32_t nblk = (len + 65534) / 65535, zlen = 2 + len + nblk * 5 + 4, c = 0xFFFFFFFFu;
        uint8_t *z = malloc(zlen + 4), *q = z;
        memcpy(q, "IDAT", 4);
        q += 4;
        *q++ = 0x78;
        *q++ = 0x01;
        for (pos = 0; pos < len;) {
            uint32_t m = len - pos > 65535 ? 65535 : len - pos;
            *q++ = pos + m >= len ? 1 : 0;
            *q++ = (uint8_t)m;
            *q++ = (uint8_t)(m >> 8);
            *q++ = (uint8_t)~m;
            *q++ = (uint8_t)(~m >> 8);
            memcpy(q, raw + pos, m);
            q += m;
            pos += m;
        }
        *q++ = (uint8_t)(b >> 8);
        *q++ = (uint8_t)b;
        *q++ = (uint8_t)(a >> 8);
        *q++ = (uint8_t)a;
        PUT32(zlen);
        fwrite(z, 1, zlen + 4, f);
        for (i = 0; i < zlen + 4; i++)
            c = crc_t[(c ^ z[i]) & 255] ^ (c >> 8);
        PUT32(~c);
        free(z);
    }
    {
        uint8_t ie[4] = {'I', 'E', 'N', 'D'};
        uint32_t c = 0xFFFFFFFFu;
        PUT32(0u);
        fwrite(ie, 1, 4, f);
        for (i = 0; i < 4; i++)
            c = crc_t[(c ^ ie[i]) & 255] ^ (c >> 8);
        PUT32(~c);
    }
    fclose(f);
}

static FILE *wav;
static uint32_t wav_frames;
static float peak_out;
static void wav_open(const char *path)
{
    static const uint8_t h[44] = {0};
    wav = fopen(path, "wb");
    if (wav)
        fwrite(h, 1, 44, wav);
    wav_frames = 0;
}
static void wav_close(void)
{
    uint32_t bytes = wav_frames * 4, v;
    if (!wav)
        return;
    fseek(wav, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, wav);
    v = 36 + bytes;
    fwrite(&v, 4, 1, wav);
    fwrite("WAVEfmt ", 1, 8, wav);
    v = 16;
    fwrite(&v, 4, 1, wav);
    {
        uint16_t fmt[2] = {1, 2};
        uint32_t r[2] = {44100, 44100 * 4};
        uint16_t al[2] = {4, 16};
        fwrite(fmt, 2, 2, wav);
        fwrite(r, 4, 2, wav);
        fwrite(al, 2, 2, wav);
    }
    fwrite("data", 1, 4, wav);
    fwrite(&bytes, 4, 1, wav);
    fclose(wav);
    wav = 0;
}

/* --------------------------------------------------------- the device --- */
static double audio_due_ms, render_ns_total, render_budget_ns_total;
static uint32_t ui_due;
static double now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e9 + (double)t.tv_nsec;
}

static void boot(void)
{
    palette_set(APP_PALETTE);                       /* as the device does at boot */
    app_init();
    lcd_fill(0, 0, 240, 240, 0);
    ble_state = BLE_OFF;                            /* Bluetooth starts at every boot, as on the device */
    ble_conn = ble_link = 0;
    bdev_state = BLE_DEV_OFF;
    if (APP_BLE_AT_BOOT)
        plat_ble_start();
#ifdef APP_BOOT
    app_boot();
#endif
    app_ui_init();
}

static void run_ms(uint32_t ms)
{
    static int32_t blk[2 * APP_BLOCK];
    uint32_t i, k;
    for (i = 0; i < ms; i++) {
        now_ms++;
        ble_tick();
#if FM1B_WIFI
        wifi_tick();
#endif
#if defined APP_KEY_SENSE
        if (sense_on)
            app_host_tick();                        /* the keys' readings, as the scan hands them over */
#endif
#ifdef APP_TICK
        app_tick();                                 /* 1 kHz, as the device's timer runs it */
#endif
#ifdef APP_LOOP
        app_loop();
#endif
        while (audio_due_ms <= (double)now_ms) {      /* APP_BLOCK-frame blocks, as the device renders them */
            double t0 = now_ns(), dt;
            app_render(blk, APP_BLOCK, APP_GAIN(master));
#ifdef PLAT_WEB
            web_audio(blk, APP_BLOCK);
#endif
            dt = now_ns() - t0;
            render_ns_total += dt;
            render_budget_ns_total += APP_BLOCK * 1e9 / 44100.0;
            cpu_pct = (uint32_t)(100.0 * dt / (APP_BLOCK * 1e9 / 44100.0));
            for (k = 0; k < APP_BLOCK; k++) {
                float l = (float)blk[2 * k] / 8388608.0f, r = (float)blk[2 * k + 1] / 8388608.0f;
                int16_t s[2];
                if (fabsf(l) > peak_out)
                    peak_out = fabsf(l);
                if (fabsf(r) > peak_out)
                    peak_out = fabsf(r);
                s[0] = (int16_t)(l * 32767.0f);
                s[1] = (int16_t)(r * 32767.0f);
                if (wav) {
                    fwrite(s, 2, 2, wav);
                    wav_frames++;
                }
            }
            audio_due_ms += APP_BLOCK * 1000.0 / 44100.0;
        }
        if (sim_listen && now_ms % 2000u == 0) {        /* the reader's HELLO, as it sends it */
            static const uint8_t HELLO[] = {ANN_H0, ANN_H1, ANN_H2, ANN_HELLO, ANN_VER};
            ann_frame(HELLO, sizeof HELLO, now_ms);
        }
        if (now_ms >= ui_due) {
            ui_due = now_ms + 16;
            app_frame();
        } else {
            app_input_only();
        }
    }
}

/* ---------------------------------------------------------- script --- */
static const char *const BTN_N[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                      "PLAY", "REC", "OCT-", "OCT+"};
static const char *const ENC_N[NE] = {"SELECT", "ALGO", "PRESET", "K1", "K2", "K3", "K4"};

static int find_name(const char *const *t, int n, const char *s)
{
    int i;
    for (i = 0; i < n; i++)
        if (!strcmp(t[i], s))
            return i;
    return -1;
}

static int key_of(const char *s)
{
    if (s[0] == 'w')
        return HOST_WHITE_K[atoi(s + 1) & 15];
    if (s[0] == 'b')
        return HOST_BLACK_K[atoi(s + 1) % 11];
    return atoi(s);
}

static uint32_t mk_pkt(const char *a, const char *b, const char *c)
{
    uint32_t st = (uint32_t)strtoul(a, 0, 16), d1 = b[0] ? (uint32_t)strtoul(b, 0, 16) : 0,
             d2 = c[0] ? (uint32_t)strtoul(c, 0, 16) : 0;
    uint32_t cin = st >= 0xF0 ? 0x0F : st >> 4;
    return cin | st << 8 | d1 << 16 | d2 << 24;
}
static const char *const PORT_N[NPORTS] = {"USB", "BLE", "TRS"};
static int expect_out(const char *port, uint32_t pkt)   /* this message went out to PORT since the last "out" */
{
    int p = find_name(PORT_N, NPORTS, port);
    uint32_t i;
    for (i = 0; p >= 0 && i < n_out; i++)
        if (((out_pkt[i] ^ pkt) & 0xFFFFFF00u) == 0 && (out_to[i] >> p & 1u)) {
            printf("  ok out %02X %02X %02X -> %s\n", (pkt >> 8) & 0xFF, (pkt >> 16) & 0xFF, pkt >> 24, port);
            return 0;
        }
    printf("FAIL expect_out %s %02X %02X %02X: not sent\n", port, (pkt >> 8) & 0xFF, (pkt >> 16) & 0xFF, pkt >> 24);
    return 1;
}

static int expect(const char *what, const char *val)
{
    int got;
#ifdef APP_HOST_EXPECT
    if (app_expect(what, &got))                   /* the app's own state (app.h), first: it may count its own way */
        ;
    else
#endif
    if (!strcmp(what, "store_writes"))
        got = (int)store_writes;
    else if (!strcmp(what, "clocks_out"))
        got = (int)clock_out_count;
    else if (!strcmp(what, "midi_out"))
        got = (int)midi_out_count;
    else if (!strcmp(what, "ble"))
        got = ble_state;
    else if (!strcmp(what, "ble_dev"))              /* the Bluetooth MIDI device: BLE_DEV_* (4 connected) */
        got = bdev_state;
    else if (!strcmp(what, "midi_in_usb"))          /* plat_midi_count: messages in from USB */
        got = (int)plat_midi_count(PORT_USB, 0);
#if BOARD_SMK37
    else if (!strncmp(what, "pad", 3) && what[3] >= '0' && what[3] <= '9' && atoi(what + 3) < SMK_NPADS)
        got = (int)hsmk_pad_rgb[atoi(what + 3)];
    else if (!strcmp(what, "smk_leds"))
        got = (int)hsmk_led_lit;
#endif
    else if (!strcmp(what, "peak_db_max") || !strcmp(what, "peak_db_min")) {
        double db = 20.0 * log10(peak_out > 1e-9f ? peak_out : 1e-9f);
        int bad = what[8] == 'm' && what[9] == 'a' ? db > atof(val) : db < atof(val);
        printf("%s peak %.1f dBFS (%s %s)\n", bad ? "FAIL" : "  ok", db, what + 8, val);
        return bad;
    } else {
        printf("FAIL unknown expect %s\n", what);
        return 1;
    }
    if (val[0] == '<' || val[0] == '>') {
        int lim = (int)strtol(val + 1, 0, 0);
        if (val[0] == '<' ? got >= lim : got <= lim) {
            printf("FAIL expect %s %s, got %d\n", what, val, got);
            return 1;
        }
        printf("  ok %s == %d (%s)\n", what, got, val);
        return 0;
    }
    if (got != (int)strtol(val, 0, 0)) {
        printf("FAIL expect %s == %s, got %d\n", what, val, got);
        return 1;
    }
    printf("  ok %s == %d\n", what, got);
    return 0;
}

#if BOARD_SMK37
static void run_ms(uint32_t ms);
/* the SMK-37's script commands (the header); 1 = handled */
static int hsmk_cmd(const char *cmd, const char *a, const char *b, const char *c)
{
    static const char *const SB_N[NSB] = {"KNOB_BANK", "FADER_BANK", "LEFT", "RIGHT"};
    int i;
    if (!strcmp(cmd, "press") || !strcmp(cmd, "release") || !strcmp(cmd, "tap")) {
        for (i = 0; i < NSB && strcmp(SB_N[i], a); i++)
            ;
        if (i == NSB)
            return 0;
        if (cmd[0] != 'r')
            hsmk_sb |= 1u << i;
        if (cmd[0] == 't')
            run_ms(60);
        if (cmd[0] != 'p')
            hsmk_sb &= ~(1u << i);
        run_ms(cmd[0] == 't' ? 40 : 2);
        return 1;
    }
    if (!strcmp(cmd, "pad") || !strcmp(cmd, "tappad")) {
        uint32_t p = (uint32_t)atoi(a), down = cmd[0] == 't' || !strcmp(b, "down");
        if (p >= SMK_NPADS)
            return 0;
        if (down) {
            hsmk_pads_held |= 1u << p;
            hsmk_pad_hit |= 1u << p;
        }
        if (cmd[0] == 't')
            run_ms(60);
        if (cmd[0] == 't' || !strcmp(b, "up"))
            hsmk_pads_held &= ~(1u << p);
        return 1;
    }
    if (!strcmp(cmd, "keybed") || !strcmp(cmd, "tapbed")) {
        uint32_t k = (uint32_t)atoi(a), down = cmd[0] == 't' || !strcmp(b, "down");
        if (k >= SMK_NKEYBED)
            return 0;
        if (down) {
            int v = atoi(cmd[0] == 't' ? b : c);      /* tapbed K [VEL], keybed K down [VEL] */
            hsmk_bed_vel[k] = (uint8_t)(v > 0 && v < 128 ? v : 100);
            hsmk_bed |= (uint64_t)1u << k;
            hsmk_bed_hit |= (uint64_t)1u << k;
            if (k >= 5u && k < 32u)
                held_keys |= 1u << (k - 5u);
        }
        if (cmd[0] == 't')
            run_ms(60);
        if (cmd[0] == 't' || !strcmp(b, "up")) {
            hsmk_bed &= ~((uint64_t)1u << k);
            if (k >= 5u && k < 32u)
                held_keys &= ~(1u << (k - 5u));
        }
        return 1;
    }
    if (!strcmp(cmd, "pedal")) {                 /* pedal RAW: the PEDAL jack's reading, 0..1023 */
        hsmk_pedal = atoi(a) < 0 ? 0 : atoi(a) > 1023 ? 1023 : atoi(a);
        return 1;
    }
    if (!strcmp(cmd, "fader")) {
        i = atoi(a);
        if (i < 0 || i > 3)
            return 0;
        if (hsmk_fad[i] < 0)                     /* (the device's first readings: where it rests) */
            hsmk_fad[i] = atoi(b) ? 0 : 1, run_ms(20);
        hsmk_fad[i] = atoi(b) < 0 ? 0 : atoi(b) > 127 ? 127 : atoi(b);
        return 1;
    }
    if (!strcmp(cmd, "wheel")) {
        if (!strcmp(a, "pitch"))
            hsmk_wh_pitch = atoi(b);
        else if (!strcmp(a, "mod"))
            hsmk_wh_mod = atoi(b);
        else
            return 0;
        hsmk_wh_new = 1;
        return 1;
    }
    if (!strcmp(cmd, "pads")) {
        printf("  pads:");
        for (i = 0; i < SMK_NPADS; i++)
            printf("%s%06X", i == 8 ? " |" : " ", (unsigned)hsmk_pad_rgb[i]);
        printf("%s  leds:", hsmk_pad_app ? "" : " (the white keys)");
        for (i = 0; i < NSB; i++)
            if (hsmk_led_lit >> i & 1u)
                printf(" %s", SB_N[i]);
        printf("\n");
        return 1;
    }
    return 0;
}
#endif

#ifndef PLAT_WEB
int main(int argc, char **argv)
{
    FILE *sc;
    char line[512], out[400];
    const char *dir = argc > 2 ? argv[2] : ".";
    int lineno = 0, fails = 0;
    if (argc < 2) {
        fprintf(stderr, "usage: %s SCRIPT [OUTDIR]\n", argv[0]);
        return 2;
    }
    sc = fopen(argv[1], "r");
    if (!sc) {
        perror(argv[1]);
        return 2;
    }
    snprintf(out, sizeof out, "%s/midi_out.txt", dir);
    midi_log = fopen(out, "w");
    if (getenv("FM1B_SAID_LOG")) {                       /* a reader listens from the start, quietly: the log only */
        said_log_f = fopen(getenv("FM1B_SAID_LOG"), "a");
        sim_listen = 1;
        said_print = 0;
    }
    boot();
    if (sim_listen) {
        static const uint8_t HELLO[] = {ANN_H0, ANN_H1, ANN_H2, ANN_HELLO, ANN_VER};
        ann_frame(HELLO, sizeof HELLO, now_ms);
    }
    while (fgets(line, sizeof line, sc)) {
        char cmd[32] = {0}, a[256] = {0}, b[64] = {0}, c[64] = {0}, d[64] = {0};
        lineno++;
        if (line[0] == '#' || sscanf(line, "%31s %255s %63s %63s %63s", cmd, a, b, c, d) < 1)
            continue;
#ifdef APP_HOST_CMD
        if (app_host_cmd(cmd, a, b, c, d))          /* the app's own commands (app.h) */
            continue;
#endif
#if BOARD_SMK37
        if (hsmk_cmd(cmd, a, b, c))                  /* the SMK-37's own buttons, pads, keybed, faders, wheels */
            continue;
#endif
        if (!strcmp(cmd, "wait"))
            run_ms((uint32_t)atoi(a));
        else if (!strcmp(cmd, "press") || !strcmp(cmd, "release") || !strcmp(cmd, "tap")) {
            int i = find_name(BTN_N, NB, a);
            if (i < 0) {
                printf("line %d: no button %s\n", lineno, a);
                return 2;
            }
            if (cmd[0] != 'r')
                held_btn |= 1u << i;
            if (cmd[0] == 't')
                run_ms(40);
            if (cmd[0] != 'p')
                held_btn &= ~(1u << i);
            run_ms(cmd[0] == 't' ? 40 : 2);
        } else if (!strcmp(cmd, "key") || !strcmp(cmd, "tapkey")) {
            int k = key_of(a);
            int down = cmd[0] == 't' || !strcmp(b, "down");
            uint32_t vel = (uint32_t)atoi(cmd[0] == 't' ? b : c), press = (uint32_t)atoi(cmd[0] == 't' ? "" : d);
            (void)vel;
            (void)press;
#ifdef APP_KEY_SENSE
            app_host_key((uint32_t)k, down ? (vel ? vel : 100u) : 0u, press);   /* the readings it gives (app.h) */
#endif
            if (down)
                held_keys |= 1u << k;
            else
                held_keys &= ~(1u << k);
            if (cmd[0] == 't') {
                run_ms(60);
#ifdef APP_KEY_SENSE
                app_host_key((uint32_t)k, 0, 0);
#endif
                held_keys &= ~(1u << k);
                run_ms(30);
            } else {
                run_ms(2);
            }
        } else if (!strcmp(cmd, "strum")) {          /* strum A B [MS]: a finger across the white keys */
            int x = atoi(a), y = atoi(b), gap = c[0] ? atoi(c) : 25, s = y >= x ? 1 : -1;
            for (;; x += s) {
                held_keys |= 1u << HOST_WHITE_K[x & 15];
                run_ms((uint32_t)gap);
                held_keys &= ~(1u << HOST_WHITE_K[x & 15]);
                if (x == y)
                    break;
            }
            run_ms(2);
        } else if (!strcmp(cmd, "turn")) {
            int e = find_name(ENC_N, NE, a), n = atoi(b), s = n > 0 ? 1 : -1;
            uint32_t gap = c[0] ? (uint32_t)atoi(c) : 80u;
            if (e < 0) {
                printf("line %d: no encoder %s\n", lineno, a);
                return 2;
            }
            for (; n; n -= s) {
                enc_acc[e] += s;
                run_ms(gap);
            }
        } else if (!strcmp(cmd, "spin")) {
            int e = find_name(ENC_N, NE, a);
            if (e < 0) {
                printf("line %d: no encoder %s\n", lineno, a);
                return 2;
            }
            enc_acc[e] += atoi(b);
            plat_host_exact = 1;
            run_ms(1);
            plat_host_exact = 0;
        } else if (!strcmp(cmd, "master")) {
            master = (uint32_t)atoi(a);
            run_ms(40);                         /* as the device's smoothing settles */
        } else if (!strcmp(cmd, "midi")) {
            int p = find_name(PORT_N, NPORTS, a);
            min_q[mi_w % MQ] = p < 0 ? mk_pkt(a, b, c) : mk_pkt(b, c, d);
            min_port[mi_w++ % MQ] = p < 0 ? PORT_USB : (uint32_t)p;
            run_ms(2);
        } else if (!strcmp(cmd, "ble")) {
            if (!strcmp(a, "connect") && ble_state == BLE_READY) {
                ble_conn = 1;
                ble_say("connected");
            } else if (!strcmp(a, "link") && ble_state == BLE_READY) {   /* linked, its MIDI not subscribed */
                ble_link = 1;
                ble_say("connected");
            } else if (!strcmp(a, "disconnect")) {
                ble_conn = ble_link = 0;
                ble_say("disconnected: reason 13");
            } else {
                printf("line %d: ble %s: the stack is not ready\n", lineno, a);
                fails++;
            }
            run_ms(2);
        } else if (!strcmp(cmd, "out")) {
            uint32_t i;
            for (i = 0; i < n_out; i++)
                if (((out_pkt[i] >> 8) & 0xFF) != 0xF8)
                    printf("  out %02X %02X %02X ->%s%s\n", (out_pkt[i] >> 8) & 0xFF, (out_pkt[i] >> 16) & 0xFF,
                           out_pkt[i] >> 24, out_to[i] & TO_USB ? " USB" : "", out_to[i] & TO_BLE ? " BLE" : "");
            n_out = 0;
        } else if (!strcmp(cmd, "expect_out")) {
            fails += expect_out(a, mk_pkt(b, c, d));
        } else if (!strcmp(cmd, "wav")) {
            snprintf(out, sizeof out, "%s/%s", dir, a);
            wav_open(out);
        } else if (!strcmp(cmd, "wavstop"))
            wav_close();
        else if (!strcmp(cmd, "peakreset"))
            peak_out = 0.0f;
        else if (!strcmp(cmd, "shot")) {
            snprintf(out, sizeof out, "%s/%s", dir, a);
            png_write(out);
        } else if (!strcmp(cmd, "leds")) {
            int i;
            printf("  leds: buttons");
            for (i = 0; i < NB; i++)
                if (lit_btn >> i & 1u)
                    printf(" %s", BTN_N[i]);
            printf(" | white ");
            for (i = 0; i < 16; i++)
                printf("%c", (lit_keys >> HOST_WHITE_K[i] & 1u) ? '#' : (dim_keys >> HOST_WHITE_K[i] & 1u) ? '+' : '.');
            printf(" black ");
            for (i = 0; i < 11; i++)
                printf("%c", (lit_keys >> HOST_BLACK_K[i] & 1u) ? '#' : (dim_keys >> HOST_BLACK_K[i] & 1u) ? '+' : '.');
            printf("\n");
        } else if (!strcmp(cmd, "expect"))
            fails += expect(a, b);
        else if (!strcmp(cmd, "data")) {          /* data FILE OFFSET: a file into the data area (plat_data) */
            FILE *df = fopen(a, "rb");
            uint32_t off = (uint32_t)strtoul(b, 0, 0);
            data_init();
            if (!df || off < PLAT_DATA_LO || off >= PLAT_DATA_HI) {
                printf("FAIL data %s %s: no such file, or outside the data area\n", a, b);
                fails++;
            } else {
                size_t n = fread(&data_area[off - PLAT_DATA_LO], 1, PLAT_DATA_HI - off, df);
                printf("  ok   data: %u B at %#x\n", (unsigned)n, (unsigned)off);
            }
            if (df)
                fclose(df);
        } else if (!strcmp(cmd, "reboot")) {
#ifdef APP_HOST_REBOOT
            app_host_reboot();                  /* the app forgets what RAM would (app.h) */
#endif
            boot();
        } else if (!strcmp(cmd, "listen")) {
            static const uint8_t HELLO[] = {ANN_H0, ANN_H1, ANN_H2, ANN_HELLO, ANN_VER};
            sim_listen = 1;
            said_len = 0;
            said_log[0] = 0;
            ann_frame(HELLO, sizeof HELLO, now_ms);
            run_ms(40);
        } else if (!strcmp(cmd, "speakdump")) {        /* every screen's text for a screen reader, to stdout */
#ifdef APP_HOST_SPEAKDUMP
            app_speak_dump();
#else
            printf("speakdump: this app has none (app_speak_dump)\n");
#endif
        } else if (!strcmp(cmd, "where")) {
            static const uint8_t WHERE[] = {ANN_H0, ANN_H1, ANN_H2, ANN_WHERE};
            ann_frame(WHERE, sizeof WHERE, now_ms);
            run_ms(40);
        } else if (!strcmp(cmd, "said") || !strcmp(cmd, "unsaid")) {
            int un = cmd[0] == 'u';
            char *t = line + strlen(cmd), *e;
            while (*t == ' ')
                t++;
            e = t + strlen(t);
            while (e > t && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' '))
                *--e = 0;
            if ((strstr(said_log, t) == 0) != un) {
                printf("FAIL line %d: %s \"%s\" (since the last check: %s)\n", lineno, un ? "said, not wanted" : "not said",
                       t, said_len ? said_log : "nothing\n");
                fails++;
            } else {
                printf("  ok %s \"%s\"\n", cmd, t);
            }
            said_len = 0;
            said_log[0] = 0;
        } else if (!strcmp(cmd, "echo"))
            printf("%s", line + 5);
        else {
            printf("line %d: unknown command %s\n", lineno, cmd);
            return 2;
        }
    }
    wav_close();
    if (midi_log)
        fclose(midi_log);
    printf("host: %u ms simulated, %u blits, render %.1f%% of real time (host CPU)%s\n", now_ms, blits,
           render_budget_ns_total > 0 ? 100.0 * render_ns_total / render_budget_ns_total : 0.0,
           fails ? ", EXPECTATIONS FAILED" : "");
    return fails ? 1 : 0;
}
#endif /* PLAT_WEB */
