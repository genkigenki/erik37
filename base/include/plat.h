/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* fm1-base's platform interface (from X0X's, by way of FoMni-1): everything the app needs from the hardware.
 *
 * The device implementation (plat_fm1.c) wraps Felucca's HAL and panel map; the
 * host implementation (host/plat_host.c) is a simulator fed by a script, which is
 * what lets the whole app — UI, sequencer, engines — run and be tested on a Mac.
 * Drawing goes through Felucca's lcd_fill / lcd_blit (lcd.c on the device, a
 * framebuffer on the host) and gfx.c on top. */
#pragma once
#include <stdint.h>

/* big zero-initialised buffers go in the .pool RAM region on the device */
#ifdef PLAT_HOST
#define PLAT_POOL
#else
#define PLAT_POOL __attribute__((section(".pool")))
#endif

/* panel controls, by printed label (Felucca's panel.c map turns these into matrix ids; Felucca
 * calls the second button SCL, the panel prints SEL) */
enum { B_FX, B_SEL, B_ENV, B_LFO, B_EDIT, B_GLO, B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY, B_REC,
       B_OCTDN, B_OCTUP, NB };
enum { EN_SELECT, EN_ALGO, EN_PRESET, EN_K1, EN_K2, EN_K3, EN_K4, NE };

#define NKEYS 27                       /* F3 .. G5 */
/* key k by colour: the 16 white keys and the 11 black keys, left to right (initialisers for a uint8_t array) */
#define PLAT_WHITE_KEYS {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26}
#define PLAT_BLACK_KEYS {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25}

static inline int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

uint32_t plat_ms(void);
uint32_t plat_buttons(void);           /* held: bit B_* */
uint32_t plat_keys(void);              /* held: bit k = key k (0 = F3) */
int32_t plat_enc(int role);            /* detents since the last call, + = clockwise; EN_* */
uint32_t plat_master(void);            /* MASTER pot's position, 0..4096 (the render gets it as a gain: APP_GAIN) */

/* lights: bright buttons and keys, and dim ones (a short pulse each scan frame; a bright bit wins); glow 0 the dim
 * ones off, 1 low, 2 high. Bit NB in buttons / dim_buttons is PLAY's second, green LED */
void plat_lights(uint32_t buttons, uint32_t keys, uint32_t dim_buttons, uint32_t dim_keys, int glow);
#define LED_PLAY_GREEN (1u << NB)
/* the same, simpler: the lit buttons and keys, the unlit buttons glowing if plat_glow(1); PLAY lights green, red
 * while plat_play_red(1) */
void plat_leds(uint32_t buttons, uint32_t keys);
void plat_glow(int on);
void plat_play_red(int red);

/* MIDI: 4-byte USB-MIDI event packets (byte 0 = cable / CIN, then status, data 1, data 2). In from USB, the TRS jack
 * (in only: the FM-1 has no MIDI out jack) and Bluetooth (FM1B_BLE); out to USB and Bluetooth. */
enum { PORT_USB, PORT_BLE, PORT_TRS, NPORTS };
#define TO_USB (1u << PORT_USB)
#define TO_BLE (1u << PORT_BLE)
int plat_midi_in(uint32_t *pkt, int *port);   /* 1 = got one, from port PORT_* (port may be 0) */
void plat_midi_send(uint32_t pkt, uint32_t to);   /* to: TO_USB | TO_BLE; main loop, the render or the tick */
void plat_midi_out(uint32_t pkt);      /* plat_midi_send(pkt, TO_USB) */
uint32_t plat_midi_count(int port, int out);   /* messages through port PORT_* since the start (out: sent), real-time (clock...) left out */
uint32_t plat_midi_ago(int port, int out);     /* ms since the last one (0xFFFFFFFF: none yet) */

/* Bluetooth MIDI (fm1.toml ble = true: FM1B_BLE 1): JieLi's BT libraries on fm1-base's shim (ble/), started at
 * every boot unless the last Bluetooth run crashed (then by hand: plat_ble_start). The app gives it a store object,
 * OBJ_BLE (the radio's address and pairing). Without it (FM1B_BLE 0, tools/build.py --no-ble) nothing of JieLi's is
 * linked, and the state stays BLE_OFF */
#ifndef FM1B_BLE
#define FM1B_BLE 0
#endif
enum { BLE_OFF, BLE_STARTING, BLE_READY, BLE_FAILED };
int plat_ble_state(void);
int plat_ble_connected(void);          /* a central is connected and listening */
int plat_ble_linked(void);             /* a central is connected, listening or not yet (a Mac whose MIDI driver
                                        * never subscribed holds the link, and the FM-1 stops advertising) */
void plat_ble_start(void);
uint32_t plat_ble_log_total(void);     /* lines written since the last start (survives a reset) */
const char *plat_ble_log_line(int back);   /* 0 = the newest */
/* Bluetooth MIDI devices (fm1.toml ble_central = true: FM1B_BLE_CENTRAL 1): the FM-1 connects to one (a keyboard,
 * a controller), beside a computer connected to it. Its MIDI comes in as PORT_BLE too, and TO_BLE goes to both.
 * Scanning finds the devices that advertise the MIDI service (up to 8); connecting stops the scan. */
#ifndef FM1B_BLE_CENTRAL
#define FM1B_BLE_CENTRAL 0
#endif
enum { BLE_DEV_OFF, BLE_DEV_SCANNING, BLE_DEV_CONNECTING, BLE_DEV_SEARCHING, BLE_DEV_READY };
void plat_ble_scan(int on);
int plat_ble_found(int i, char *name, int *rssi);   /* device i of the scan (name: 21 bytes); 0 = none */
int plat_ble_connect(int i);           /* 0 = connecting */
void plat_ble_disconnect(void);
int plat_ble_device(void);             /* BLE_DEV_*: READY is connected, MIDI both ways */
/* Bluetooth on or off, as the player asks: off ends every link, stops advertising and scanning (the radio quiet);
 * on starts it (or advertises again). Whether it starts at power-on: APP_BLE_AT_BOOT (app.h, e.g. a saved setting;
 * default 1) */
void plat_ble_enable(int on);

/* WiFi (fm1.toml wifi = true: FM1B_WIFI 1): a station on JieLi's WiFi libraries and the SDK's lwIP, on the same shim.
 * Off until the app turns it on; then it joins the networks it joined before (kept in the app's store object OBJ_WIFI,
 * six, the most recent first). The app's network code runs in a task of its own (plat_net_task) with lwIP's sockets
 * (lwip/sockets.h): a socket call waits, and the main loop must not. A build has Bluetooth or WiFi, not both (yet). */
#ifndef FM1B_WIFI
#define FM1B_WIFI 0
#endif
enum { WIFI_OFF, WIFI_STARTING, WIFI_ON, WIFI_JOINING, WIFI_ADDRESS, WIFI_CONNECTED, WIFI_FAILED };
void plat_wifi_on(int on);             /* on: the radio starts (its first second holds the sound) and joins */
int plat_wifi_state(void);             /* WIFI_*: ON is on with no network; ADDRESS, joined and asking for one */
const char *plat_wifi_why(void);       /* WIFI_FAILED: "not found", "wrong password?", "no address"... */
const char *plat_wifi_ssid(void);      /* the network joined, or being joined */
uint32_t plat_wifi_ip(void);           /* connected: the address, its first byte lowest; else 0 */
int plat_wifi_rssi(void);              /* connected: the signal */
int plat_wifi_scan(void);              /* the networks around: plat_wifi_found once it is done (0 = asked) */
int plat_wifi_found(int i, char *ssid, int *rssi, int *secured);   /* the scan's network i (ssid: 33 bytes), the
                                                                     * strongest first; 0 = none */
int plat_wifi_join(const char *ssid, const char *pass);   /* pass 0: the saved one's, or none; kept once joined */
int plat_wifi_saved(int i, char *ssid);   /* the networks kept, the most recent first; 0 = none */
void plat_wifi_forget(int i);
int plat_wifi_kept(const char *ssid);  /* the network is kept (its password known): plat_wifi_join(ssid, 0) joins it */
void plat_wifi_rx(uint32_t *frames, int *rssi);   /* reception: frames received in the last second (anyone's), their
                                                   * average signal */
uint32_t plat_wifi_scans(void);        /* scans done since the start: a change means plat_wifi_found has new results */
int plat_net_task(void (*fn)(void *), void *arg, const char *name);   /* a task for the app's network code (8 KiB
                                                                        * of stack); 0 = made */

/* the 1 kHz tick (app_tick, an app with APP_TICK) runs in the timer interrupt: the main loop locks it out around
 * what they share (it nests) */
uint32_t plat_lock(void);
void plat_unlock(uint32_t f);
void plat_yield(void);
void plat_log(const char *line);
/* the flash's data area, 0x97000..0xDFFFF, for an app's own big data (a table it builds, samples): its store objects
 * live there too, at the offsets it gave them (APP_STORE_MAP), so it keeps to the rest. Main loop only; an erase or a
 * write stops everything for a moment (the sound too: no flash reads meanwhile).
 * The M-Vave SMK-37 (BOARD_SMK37): 0xC2000..0xE9FFF (160 KiB, the stock USRFLASH), the data area alone (the store's
 * objects are elsewhere, boards/smk37/fm1_flash_map.h); the first erase or write there releases the stock firmware's
 * mapping (it rebuilds its factory one at its next start) */
#if defined(BOARD_SMK37) && BOARD_SMK37
#define PLAT_DATA_LO 0xC2000u
#define PLAT_DATA_HI 0xEA000u
#else
#define PLAT_DATA_LO 0x97000u
#define PLAT_DATA_HI 0xE0000u
#endif
const uint8_t *plat_data(uint32_t off);   /* read in place; 0 = outside the area */
int plat_data_erase(uint32_t off);        /* the 4 KiB sector at off (aligned); 0 = done */
int plat_data_write(uint32_t off, const void *src, uint32_t n);   /* into erased flash, src in RAM; 0 = done */       /* a line for the radio's log (fm1_remote.py log; it survives a restart) */
void plat_sysex_send(const uint8_t *frame, uint32_t n);   /* a whole SysEx frame (F0 .. F7) out over USB */                 /* long work in the main loop (drawing) lets the app's background run (app_yield) */

/* the keys' velocity and pressure (an app with APP_KEY_SENSE): on, the scan reads the keys on the analog rows and
 * hands each reading to the app (app_key_sample, app_key_probe); off by default */
void plat_sense(int on);
uint32_t plat_analog_mask(void);       /* bit k: key k is on an analog row (PA7 / PA8; the top row is probed) */

#if defined(BOARD_SMK37) && BOARD_SMK37
/* The M-Vave SMK-37 (BOARD_SMK37): its controls beyond the FM-1's. plat_buttons and plat_enc are the FM-1's by name
 * (the panel map: SCALE = SEL, CHORD = ENV, NOTE REPEAT = LFO, PARA = EDIT, GLOBE = GLO, PATCH = HOME, BT = SAVE,
 * SEQ PLAY = PLAY, SEQ REC = REC; K1..K4 = KNOB 1..4, K5 = MASTER (plat_master), K6..K8 = SELECT, ALGORITHM, PRESETS).
 * plat_keys is the keybed's F3..G5 (the FM-1's 27 keys) and the 16 pads as its 16 white keys (plat_smk_pads). Pads are
 * numbered as the hardware: 0..7 the top row, 8..15 the bottom row, left to right. */
#define SMK_NKEYBED 37                 /* C3 .. C6 */
#define SMK_NPADS 16
enum { SB_KNOB_BANK, SB_FADER_BANK, SB_LEFT, SB_RIGHT, NSB };   /* the four buttons the FM-1 has not */
/* the pads' layout: PIANO the white keys F3..G5, low notes on the bottom row (the default); GRID the white keys in
 * reading order (the top row white keys 1-8); OWN not keys at all (plat_pad_hits) */
enum { SMK_PADS_PIANO, SMK_PADS_GRID, SMK_PADS_OWN };
void plat_smk_pads(int layout);        /* a pad held keeps the key it pressed until it is let go */
uint32_t plat_smk_buttons(void);       /* held: bit SB_* */
void plat_smk_leds(uint32_t lit);      /* their LEDs, bit SB_* (before plat_lights / plat_leds) */
uint64_t plat_keybed(void);            /* keybed keys held: bit k = key k (0 = C3) */
uint64_t plat_keybed_hits(void);       /* keybed presses since the last call */
int plat_keybed_vel(int k);            /* key k's last press: its velocity 1..127 (the factory curves); 0 = none yet */
int plat_pedal_raw(void);              /* the PEDAL jack: the raw reading 0..1023 (polarity is the pedal's), -1 none */
uint32_t plat_pad_hits(void);          /* SMK_PADS_OWN: pad presses since the last call, bit p = pad p */
uint32_t plat_pads_held(void);         /* pads held, bit p = pad p (any layout) */
int plat_fader(int i);                 /* fader 0..3: 0..127, -1 while the readings settle */
int plat_wheels(int32_t *pitch, int32_t *modw);   /* 1 = new values: pitch -127..127 (0 at rest), mod 0..127 */
void plat_pad_rgb(int pad, uint8_t r, uint8_t g, uint8_t b);   /* pad p's light (with plat_pad_lights(1)) */
void plat_pad_lights(int app);         /* 1: the app colours the pads (plat_pad_rgb); 0 (default): plat_lights shows
                                        * the white keys on them (lit light blue, dim dark orange) */
#endif

/* persistent storage (Felucca storage.c objects: A/B sectors, CRC, torn-write safe) */
int plat_store_load(uint32_t obj, void *dst, uint32_t max);   /* bytes loaded, < 0 = none / bad */
int plat_store_save(uint32_t obj, const void *src, uint32_t len);   /* 0 = ok */
#define PLAT_STORE_MAX 3840u           /* bytes per object */
/* The objects are the app's (app.h): an enum that includes OBJ_PANEL (the panel calibration, the platform's) and
 * ends with OBJ_NOBJ, and APP_STORE_MAP, each object's flash offset: two 4 KiB sectors (A/B) from there, inside the
 * data area (0x97000..0xDFFFF) or Felucca's settings sector pair at 0xFC000. An object's number is written in its
 * record, so an app keeps its numbering from one release to the next. */

/* audio load in % of the render budget, and xruns since boot (device: from the ISR) */
uint32_t plat_cpu_pct(void);
uint32_t plat_xruns(void);

/* the app's optional hooks (app.h defines the macro when it has the function):
 *   APP_TICK        void app_tick(void)           1 kHz, in the timer interrupt (a clock, a sequencer)
 *   APP_BOOT        void app_boot(void)           once, after the platform is up, before app_ui_init (a radio)
 *   APP_LOOP        void app_loop(void)           every pass of the main loop (a radio's tasks)
 *   APP_YIELD       void app_yield(void)          from plat_yield, inside long work
 *   APP_OVERLOAD    void app_overload(void)       the render took over 90 % of its time (shed voices)
 *   APP_SPLASH      void app_splash(void)         the first screen at boot, instead of the name
 *   APP_GAIN(m)     the render's gain from MASTER's position m (0..4096; default the square, m * m >> 12)
 *   APP_QUIET       void app_quiet(int32_t *lr, uint32_t n)   the render while the flash must not be read (the
 *                   radio's first second, FM1B_BLE): keep time without sound (default: silence)
 *   APP_KEY_SENSE   void app_key_sample(uint32_t key, uint32_t v, uint32_t ticks, int driven) (rows PA7 / PA8),
 *                   int app_key_drive(uint32_t key) (read it driven), void app_key_probe(uint32_t key, uint32_t v,
 *                   uint32_t pressed) (the top row), int app_key_led_ok(uint32_t key) (0: its column dark when read);
 *                   from the scan's interrupt
 *   APP_SYSEX       int app_sysex(const uint8_t *p, uint32_t n)   a SysEx frame from USB the platform does not take
 *                   (p: the bytes between F0 and F7); 1 = the app's. Its answers: plat_sysex_send. Main loop */
