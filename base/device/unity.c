/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* An fm1-base firmware for the M-VAVE FM-1: one compilation unit for the platform and the app
 * (Felucca's layout: the chip and board layers are header-only, so everything that touches them is here).
 * An app's heavy code (its DSP) may be separate units at -O2 (fm1.toml: o2); this one is -Os. Order matters.
 *
 * The app provides, on the include path:
 *   app.h  its configuration (below) and the contract: app_init, app_render, app_panic, app_ui_init,
 *          app_frame, app_input_only, app_say; its store objects (OBJ_PANEL among them, OBJ_NOBJ) and their
 *          flash map APP_STORE_MAP
 *   app.c  the app itself (its own unity build), compiled here after the platform
 * Configuration, each with a default:
 *   APP_NAME "TEMPLATE"     on the splash, crash and safe-mode screens and for the screen reader
 *   APP_VERSION "DEV"       a release build passes its own (tools/build.py --release)
 *   APP_BLOCK 256           frames per audio half (5.8 ms at 44.1 kHz), the render's block
 *   FM1B_CDC 0              USB serial console (a second USB function)
 *   FM1B_UAC 1              USB audio input: the master output, recordable over the cable at 44.1 kHz (checked on an
 *                           FM-1 2026-10-16: no lost frame); FM1B_UAC_48K 0 (48 kHz too: needs the resampler's filter)
 *   FM1B_UAC_OUT 1          USB audio output: the computer plays through the FM-1, mixed after the app, at MASTER
 *   FM1B_TRS 1              MIDI in on the TRS jack
 *   APP_TOUCH               velocity and pressure from the keys (lib/touch.h)
 *   FM1B_BLE 0              Bluetooth MIDI (fm1.toml ble = true; tools/build.py --no-ble leaves it out)
 * BOARD_SMK37 1 (tools/build.py BOARD=smk37) builds it for the M-Vave SMK-37: felucca-bsp's boards/smk37 is on the
 * include path in place of boards/fm1 (the same header names), and the platform's few board-specific places are
 * marked BOARD_SMK37 (the panel, the lights, the store's place, the flash part). */
#include <stdint.h>
#include "plat.h"
#include "app.h"                  /* first: the scan's hooks below may call the app */
#ifdef APP_TOUCH
#include "touch.h"                /* velocity and pressure from the keys (lib/touch.c) */
#endif
#ifdef APP_KEY_SENSE              /* the keys' readings (plat_sense) to the app, by key: 0..26 */
#define FM1_KEY_OF(col, rowbit) ((uint32_t)FM1_KEYMAP[rowbit][col] - 14u)
#define FM1_INPUT_ANALOG(col, rowbit, v, driven) \
    (FM1_KEYMAP[rowbit][col] >= 14 ? app_key_sample(FM1_KEY_OF(col, rowbit), (v), fm1_ticks(), (int)(driven)) : (void)0)
#define FM1_INPUT_DRIVE(col, rowbit) (FM1_KEYMAP[rowbit][col] >= 14 && app_key_drive(FM1_KEY_OF(col, rowbit)))
#define FM1_INPUT_PROBE(col, rowbit, v, pressed) \
    (FM1_KEYMAP[rowbit][col] >= 14 ? app_key_probe(FM1_KEY_OF(col, rowbit), (v), (pressed)) : (void)0)
#define FM1_INPUT_LED_OK(col, rowbit) (FM1_KEYMAP[rowbit][col] < 14 || app_key_led_ok(FM1_KEY_OF(col, rowbit)))
#endif
#include "fm1_time.h"
#include "fm1_sys.h"
#include "fm1_irq.h"
#include "fm1_guard.h"
#include "fm1_input.h"
#include "fm1_timer.h"
#include "fm1_audio.h"
#include "fm1_adc.h"
#include "fm1_lcd_hw.h"
#include "fm1_flash.h"
#include "fm1_xip.h"
#include "fm1_perf.h"

#include "libc.c"
#include "lcd.c"
#include "gfx.c"

static volatile uint32_t fm1_ms;  /* milliseconds since boot (TIMER4-based, TIMER5 ISR) */
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")

#include "fm1b_config.h"          /* APP_NAME, APP_VERSION, APP_BLOCK ...: the app's, or the defaults */
#define HALF_FRAMES APP_BLOCK     /* the audio half, as felucca-bsp's usb.c expects it */

#define FELUCCA_OTA 1             /* M-UPGRADE update entry: keeps the web installer working */
#ifndef FM1B_CDC
#define FM1B_CDC 0
#endif
#define FELUCCA_CDC FM1B_CDC
#ifndef FM1B_UAC
#define FM1B_UAC 1
#endif
#define FELUCCA_UAC FM1B_UAC
#ifndef FM1B_UAC_48K
#define FM1B_UAC_48K 0            /* 48 kHz too: needs uac_fir.h, the resampler's filter (not in fm1-base yet) */
#endif
#define FELUCCA_UAC_48K FM1B_UAC_48K
#ifndef FM1B_UAC_OUT
#define FM1B_UAC_OUT 1            /* the computer plays through the FM-1 (USB audio output) */
#endif
#define FELUCCA_UAC_OUT FM1B_UAC_OUT
#ifndef FELUCCA_ID
#define FELUCCA_ID "FM-1_000"     /* package identity (tools/build.py passes the firmware's) */
#endif
#include "usb.c"
#if BOARD_SMK37 && !defined(FM1B_TRS)
#define FM1B_TRS 0                /* the SMK-37's TRS jack is MIDI OUT (UART1 TX, PC7), and PH8, the FM-1's MIDI IN pin,
                                   * is a key-matrix row there: it must never become UART RX */
#endif
#if BOARD_SMK37 && FM1B_TRS
#error "BOARD_SMK37: midi_uart.c is the FM-1's MIDI IN (RX on PH8, a key-matrix row on the SMK-37)"
#endif
#ifndef FM1B_TRS
#define FM1B_TRS 1
#endif
#if FM1B_TRS
#include "midi_uart.c"
#endif

static uint8_t flash_ok;          /* JEDEC id matched at boot */
static void audio_silence(void);
static int st_read(uint32_t off, void *dst, uint32_t n)   /* 256-byte IRQ-off windows: audio keeps up */
{
    uint8_t *d = dst;
    while (n) {
        uint32_t k = n > 256u ? 256u : n, f = irq_save();
        int rc = FL_FAR(fl_read_ram)(off, d, k);
        irq_restore(f);
        if (rc)
            return rc;
        off += k;
        d += k;
        n -= k;
    }
    return 0;
}
static int st_erase(uint32_t off)
{
    uint32_t took;
    if (!FL_STORE_OK(off, 0x1000u))
        return -8;
    audio_silence();
    return fl_erase4k(off, &took);
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    if (!FL_STORE_OK(off, n))
        return -8;
    return fl_write(off, src, n);
}
#if BOARD_SMK37
static int st_erase_stocktag(void)              /* the stock firmware's USRFLASH end marker: erased, never written */
{
    uint32_t took;
    audio_silence();
    return fl_erase4k(FL_STOCKTAG_LO, &took);
}
#define ST_IDLE() fm1_wdt_feed()  /* (storage.c: between the erases of a store take-over) */
#endif
#include "storage.c"

struct bootguard_s { uint32_t magic, failed, pending; };
extern struct bootguard_s bootguard;
#include "ota.c"
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void) { fm1_wdt_feed(); }
static int ota_in_area(uint32_t off, uint32_t n) { return FL_IN(off, n, OTA_AREA, OTA_AREA + OTA_AREA_LEN); }
static int ota_erase(uint32_t off)
{
    uint32_t took;
    if (!ota_in_area(off, 0x1000u) || (off & 0xFFFu))
        return -8;
    audio_silence();
    return fl_erase4k(off, &took);
}
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    if (!ota_in_area(off, n))
        return -8;
    return fl_write(off, p, n);
}
static int ota_fread(uint32_t off, void *p, uint32_t n) { return st_read(off, p, n); }
static void ota_show(uint32_t step, int32_t code)
{
    static const char *const STEP[] = {"", "PACKAGE", "CHECK HEAD", "LOADER", "CONFIRM", "RESTART"};
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 92, 240, &FONT_S, "UPDATE", C_WHITE, 1);
    if (step < 9u) {
        draw_text_box(0, 124, 240, &FONT_S, STEP[step < 6u ? step : 0], C_HI, 1);
        return;
    }
    draw_text_box(0, 124, 240, &FONT_S, code == 1 ? "DRY RUN OK" : "FAILED", C_HI, 1);
    fm1_delay_ms(1500);
}
static void radio_clean_exit(void);
static void ota_commit(const uint8_t *parm)
{
    radio_clean_exit();                                   /* an update, not a crash: Bluetooth starts again after it */
    bootguard.pending = 0;                              /* intentional reset */
    bootguard.failed = 0;                               /* a new install starts with a clean slate */
    usb_detach();
    fm1_delay_ms(30);
    fm1_enter_update(parm);
}

#define ANN_NAME APP_NAME " " APP_VERSION
#define ANN_DATA PLAT_POOL
#include "ann_words.h"            /* the link to the app's words file (tools/gen_words.py, from the app's words.tsv) */
#include "announce.c"             /* the screen as text for a screen reader (fm1-reader) */
static int ann_wire_send(const uint8_t *p, uint32_t n)   /* never waits: no room (nobody reading), not sent */
{
    if (!usb.config || usb.suspended || SXQ - (so_w - so_r) < (n + 2u) / 3u)
        return -1;
    return ota_wire_send(p, n);
}
#include "panel.c"
#if FM1B_BLE || FM1B_WIFI
#include "radio.c"                /* the shim's platform, a radio's start; Bluetooth MIDI (FM1B_BLE, ble/: the shim, the
                                   * MIDI service) */
#else
#include "radio_none.c"           /* the build with no radio: nothing of JieLi's linked */
#endif
#if FM1B_WIFI
#include "wifi.c"                 /* WiFi (ble/wifi/, the SDK's lwIP) */
#endif
#include "plat_fm1.c"
#ifdef APP_TOUCH
#include "touch.c"
#endif
#ifdef APP_BTDEV
#include "btdev.c"                /* Bluetooth MIDI devices for an app's page (lib/btdev.h) */
#endif
#include "app.c"
#include "main_fm1.c"
