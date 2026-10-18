/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Boot, audio ISR and main loop adapted from Felucca's main.c / audio.c, Copyright (C) 2026
 * Leo Kuroshita (@kurogedelic), Hugelton Instruments. */
/* The platform's boot and main loop (from X0X's, by way of FoMni-1): boot (WDT first, boot-loop guard, guards), LCD, input (TIMER5
 * 10 kHz), audio (ALNK0), USB; then the main loop: UI at ~60 frames/s, input polled in between. */
extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
extern uint32_t _pool_start[], _pool_end[], _rt_start[], _rt_end[], _rt_load[];

/* --------------------------------------------------------------- audio --- */
/* HALF_FRAMES (unity.c, from APP_BLOCK): the I2S half buffer, 256 frames = 5.8 ms at 44.1 kHz by default */
#define HALF_WORDS (HALF_FRAMES * 2u)
#define FS 44100u
#define REND_FRAMES HALF_FRAMES             /* app_render's frames per call */
static int32_t abuf[2u * HALF_WORDS] __attribute__((aligned(4)));

#define DBG_MAGIC 0x44424731u                       /* "DBG1": read with `fm1t memr` */
struct fm1b_dbg {
    uint32_t magic, halves, max_us, nested, in_audio, late, timer_irqs, ui_frames;
    uint32_t last_us, cpu_q8, boots, stage;
    uint32_t crash_seen;                            /* the crash count already reported at a boot */
} fm1b_dbg __attribute__((section(".noinit")));
static uint8_t safe_mode;                           /* two failed boots: no audio, USB on (safe_main) */
static uint32_t wdt_stage __attribute__((section(".noinit")));   /* the main loop's step when the watchdog fired:
                                                                  * 2 Bluetooth, 3 the app's loop, 4 USB, 5 a frame,
                                                                  * 6 input (fm1b_dbg.stage) */

static void render_block(int32_t *o)                /* ALNK0: one half, measured */
{
    uint32_t t0 = fm1_ticks(), us, budget = HALF_FRAMES * 1000000u / FS;
    fm1b_dbg.in_audio = 1;
#if FELUCCA_UAC
    if (!radio_quiet)                                 /* (USB audio's code is in the flash too: none while the radio
                                                     * calibrates, when a fetch can come back corrupted; a CTL-1 crashed
                                                     * so, returning to a garbled address, 0.6 s into each start) */
        uac_render_start();
#endif
    {
        uint32_t k;
        for (k = 0; k < HALF_FRAMES; k += REND_FRAMES) {
            if (radio_quiet) {                        /* the radio calibrating: no flash reads (radio.c) */
#ifdef APP_QUIET
                app_quiet(o + 2u * k, REND_FRAMES);
#else
                uint32_t i;
                for (i = 0; i < 2u * REND_FRAMES; i++)
                    o[2u * k + i] = 0;
#endif
            } else {
                app_render(o + 2u * k, REND_FRAMES, APP_GAIN(master_pos));
            }
            if (radio_quiet)
                continue;
#if FELUCCA_UAC_OUT
            uac_play(o + 2u * k, REND_FRAMES, master_pos * master_pos >> 12);   /* the computer's audio, at MASTER */
#endif
#if FELUCCA_UAC
            uac_tap(o + 2u * k, REND_FRAMES);       /* the USB audio input: the same master output */
#endif
        }
    }
    us = (fm1_ticks() - t0) / FM1_TICKS_PER_US;
#ifdef APP_OVERLOAD
    if (us > budget * 9u / 10u)
        app_overload();                             /* over 90 % of its time: the app sheds what it can */
#endif
    fm1b_dbg.cpu_q8 = (fm1b_dbg.cpu_q8 * 15u + (us * 256u) / budget) / 16u;
    audio_cpu_pct = (fm1b_dbg.cpu_q8 * 100u) >> 8;
    fm1b_dbg.halves++;
    fm1b_dbg.last_us = us;
    if (us > fm1b_dbg.max_us)
        fm1b_dbg.max_us = us;
    fm1b_dbg.in_audio = 0;
}

void fm1_alnk0_irq(void)                            /* via isr_alnk0 (hal/fm1_isr.S) */
{
    uint8_t p = fm1_audio_pending();
    fm1_audio_ack_aux(p);
    if (p & FM1_AUDIO_HALF) {
        uint32_t half = fm1_audio_free_half();
        render_block(&abuf[half * HALF_WORDS]);
        fm1_audio_ack_half();
        if (fm1_audio_free_half() != half) {
            fm1b_dbg.late++;                         /* the DMA moved on while we rendered */
            audio_xruns++;
        }
    }
}
extern void isr_alnk0(void);

static void audio_init(void)
{
    uint32_t i;
    for (i = 0; i < 2u * HALF_WORDS; i++)
        abuf[i] = 0;
    fm1_audio_init(abuf, HALF_WORDS, isr_alnk0, 3);
}

static void audio_silence(void)                     /* IRQs off (flash erase): the DMA would loop stale audio */
{
    uint32_t i;
    for (i = 0; i < sizeof abuf / sizeof abuf[0]; i++)
        abuf[i] = 0;
}

/* ------------------------------------------------------------ timer --- */
static uint8_t usb_due;
static uint32_t t5_gap_max, t5_late, t5_run_max;                /* the longest gap between ticks (us), and gaps over 300 us:
                                                     * the key and light scan held up (the log reads and clears) */
void fm1_timer5_irq(void)
{
    static uint32_t sub, t5_prev;
    fm1_timer5_ack();
    {
        uint32_t now = fm1_ticks(), gap = (now - t5_prev) / FM1_TICKS_PER_US;
        if (t5_prev) {
            if (gap > t5_gap_max)
                t5_gap_max = gap;
            if (gap > 300u)
                t5_late++;
        }
        t5_prev = now;
    }
    fm1b_dbg.timer_irqs++;
    if (fm1b_dbg.in_audio)
        fm1b_dbg.nested++;
    fm1_input_tick();
    /* 2 kHz: all USB SIE traffic lives here. This ISR now nests into the audio render (so the key
     * matrix keeps its rhythm, below); USB shares the MIDI queues with the audio ISR, so a poll that
     * falls inside the render waits for the first tick outside it, as it did before the nesting. */
    if (sub % 5u == 0u) {
        usb_due = 1;
#if FELUCCA_UAC
        /* the USB audio stream cannot wait for the render (one packet per 1 ms frame, a render takes
         * up to ~11.6 ms): it runs nested too. It touches only EP4 (INDEX is set on every access) and
         * the consumer side of its ring, and usb_poll never runs nested, so the two never interleave */
        if (fm1b_dbg.in_audio)
            uac_service();
#endif
    }
    if (usb_due && !fm1b_dbg.in_audio) {
        usb_due = 0;
        usb_poll();
#if FM1B_TRS
        uart_midi_poll();                           /* the TRS jack: same context, same queue */
#endif
    }
#ifdef APP_TICK
    if (sub == 0u) {                                /* 1 kHz: the app's tick (a clock, a sequencer) */
        in_tick = 1;
        app_tick();
        in_tick = 0;
    }
#endif
    if (++sub == 10u)
        sub = 0;
    {
        uint32_t run = (fm1_ticks() - t5_prev) / FM1_TICKS_PER_US;
        if (run > t5_run_max)
            t5_run_max = run;
    }
    {
        static uint32_t last, acc;
        uint32_t now = fm1_ticks();
        acc += now - last;
        last = now;
        while (acc >= 1000u * FM1_TICKS_PER_US) {
            acc -= 1000u * FM1_TICKS_PER_US;
            fm1_ms++;
        }
    }
}
extern void isr_timer5(void);

/* ------------------------------------------------------------- boot --- */
#define BOOTGUARD_MAGIC 0x42475244u
struct bootguard_s bootguard __attribute__((section(".noinit")));   /* declared in main.c (ota_commit) */

static void hexs(char *b, uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 8u; i++)
        b[i] = "0123456789ABCDEF"[(v >> (28u - 4u * i)) & 15u];
    b[8] = 0;
}

/* a readout for development over USB-MIDI: F0 7D 4C 47 F7 ("LG") asks; the answer is one frame per line,
 * F0 7D 4C 47 <ASCII> F7: the last crash (count, vector, pc, rets, sp, uptime), then Bluetooth's log, the oldest
 * first (it survives a reset) */
static void log_line_send(const char *t)
{
    uint8_t f[48];
    uint32_t n = 0;
    f[n++] = 0xF0; f[n++] = 0x7D; f[n++] = 0x4C; f[n++] = 0x47;
    while (*t && n < sizeof f - 1u)
        f[n++] = (uint8_t)(*t++ & 0x7Fu);
    f[n++] = 0xF7;
    ota_wire_send(f, n);
}
static int log_frame(const uint8_t *p, uint32_t n)
{
    char b[48], *q;
    int k;
    if (n < 3u || p[0] != 0x7Du || p[1] != 0x4Cu || p[2] != 0x47u)
        return 0;
    if (fm1_crash.magic == FM1_CRASH_MAGIC) {
        const uint32_t v[6] = {fm1_crash.count, fm1_crash.vec, fm1_crash.pc, fm1_crash.rets, fm1_crash.sp,
                               fm1_crash.uptime_ms};
        static const char *const L[6] = {"crash n ", " vec ", " pc ", " rets ", " sp ", " ms "};
        uint32_t i;
        q = b;
        for (i = 0; i < 3u; i++) {
            const char *l = L[i];
            while (*l)
                *q++ = *l++;
            hexs(q, v[i]);
            q += 8;
        }
        *q = 0;
        log_line_send(b);
        q = b;
        for (i = 3; i < 6u; i++) {
            const char *l = L[i] + 1;
            while (*l)
                *q++ = *l++;
            hexs(q, v[i]);
            q += 8;
            *q++ = ' ';
        }
        *q = 0;
        log_line_send(b);
        for (k = (int)FM1_CRASH_CTX_N - 1; k >= 0; k--) {   /* the registers at the last crashes, the oldest first */
            const fm1_crash_ctx_t *x = &fm1_crash_ctx[(fm1_crash.count - (uint32_t)k) % FM1_CRASH_CTX_N];
            uint32_t r, j;
            if (x->count != fm1_crash.count - (uint32_t)k || x->count == 0u)
                continue;                            /* (not one of them: an older run's, or never written) */
            q = b;
            for (j = 0; j < 8u; j++)
                *q++ = "regs n  "[j];
            hexs(q, x->count);
            q += 8;
            for (j = 0; j < 4u; j++)
                *q++ = " pc "[j];
            hexs(q, x->pc);
            q += 8;
            for (j = 0; j < 5u; j++)
                *q++ = " usp "[j];
            hexs(q, x->usp);
            log_line_send(b);
            for (r = 0; r < 16u; r += 4u) {
                q = b;
                *q++ = 'r';
                *q++ = (char)('0' + r / 10u);
                *q++ = (char)('0' + r % 10u);
                for (j = 0; j < 4u; j++) {
                    *q++ = ' ';
                    hexs(q, x->r[r + j]);
                    q += 8;
                }
                log_line_send(b);
            }
        }
    } else {
        log_line_send("no crash recorded");
    }
    if (wdt_stage) {
        static const char *const ST[7] = {"?", "?", "Bluetooth", "the app's loop", "USB", "a frame", "input"};
        char *q = b;
        const char *t = "last watchdog reset in ";
        while (*t)
            *q++ = *t++;
        t = ST[wdt_stage < 7u ? wdt_stage : 0];
        while (*t)
            *q++ = *t++;
        *q = 0;
        log_line_send(b);
    }
    for (k = (int)plat_ble_log_total() - 1; k >= 0; k--) {
        if (!plat_ble_log_line(k)[0])
            continue;                                /* (beyond what the log keeps) */
        log_line_send(plat_ble_log_line(k));
    }
    {
        extern uint32_t lock_max, lock_from;
        static const char *const L[5] = {"gap ", " late ", " tick ", "lock ", " from "};
        const uint32_t v[5] = {t5_gap_max, t5_late, t5_run_max, lock_max, lock_from};
        uint32_t i;
        q = b;
        for (i = 0; i < 5u; i++) {
            const char *l = L[i];
            if (i == 3u) {                           /* two lines: a frame holds ~40 characters */
                *q = 0;
                log_line_send(b);
                q = b;
            }
            while (*l)
                *q++ = *l++;
            hexs(q, v[i]);
            q += 8;
        }
        *q = 0;
        log_line_send(b);
        t5_gap_max = t5_late = t5_run_max = lock_max = 0;
    }
#if FM1B_BLE
    {
        extern uint32_t shim_off_max, shim_off_from;
        const char *l = "BT irqs off max us ";
        q = b;
        while (*l)
            *q++ = *l++;
        hexs(q, shim_off_max);
        q += 8;
        for (l = " from "; *l;)
            *q++ = *l++;
        hexs(q, shim_off_from);
        log_line_send(b);
        shim_off_max = 0;
    }
#endif
    log_line_send("end");
    return 1;
}

/* remote control: F0 7D 4C 43 ("LC") op ... F7, 7-bit bytes:
 *   1 b0 b1          the buttons held (B_* bits: b0 the low seven, b1 the next seven)
 *   2 k0 k1 k2 k3    the keys held (bit k = key k, seven bits a byte, low first)
 *   3 role delta     an encoder turned (EN_*; delta - 64 detents)
 *   4 lo hi          MASTER's position (0..4096, seven bits a byte; 4097 or more: the pot's own again)
 * Each is added to the panel's own input (plat_fm1.c remote); the answer is F0 7D 4C 43 op F7. */
static int remote_frame(const uint8_t *p, uint32_t n)
{
    static uint8_t ack[6] = {0xF0, 0x7D, 0x4C, 0x43, 0, 0xF7};
    if (n < 4u || p[0] != 0x7Du || p[1] != 0x4Cu || p[2] != 0x43u)
        return 0;
    switch (p[3]) {
    case 1:
        if (n >= 6u)
            remote.btn = ((uint32_t)p[4] | (uint32_t)p[5] << 7) & ((1u << NB) - 1u);
        break;
    case 2:
        if (n >= 8u)
            remote.keys = ((uint32_t)p[4] | (uint32_t)p[5] << 7 | (uint32_t)p[6] << 14 | (uint32_t)p[7] << 21) &
                          ((1u << NKEYS) - 1u);
        break;
    case 3:
        if (n >= 6u && p[4] < NE)
            remote.enc[p[4]] += (int32_t)p[5] - 64;
        break;
    case 4:
        if (n >= 6u) {
            uint32_t v = (uint32_t)p[4] | (uint32_t)p[5] << 7;
            remote.master = v > 4096u ? -1 : (int32_t)v;
        }
        break;
    default:
        return 1;
    }
    ack[4] = p[3];
    ota_wire_send(ack, sizeof ack);
    return 1;
}

#if FM1B_WIFI
/* WiFi over USB (passwords cannot be typed on the FM-1): F0 7D 4C 57 ("LW") op ... F7. Text goes as nibbles (each
 * byte two: high, low), the answers as plain 7-bit text, a line a frame (F0 7D 4C 57 10 <text> F7), then
 * F0 7D 4C 57 11 F7:
 *   1 on   2 off   3 scan
 *   4 status        "<state> <ssid> <ip> <rssi> <why>"
 *   5 the networks kept, one a line, the most recent first
 *   6 i             forget network i
 *   7 ssid 0 pass   join it with that password (nibbles; kept once joined); no password: a kept one's, or none
 *   8 the scan's networks, "<rssi> <secured> <ssid>" a line, the strongest first */
static void wifi_line(const char *t)
{
    uint8_t f[96];
    uint32_t n = 0;
    f[n++] = 0xF0; f[n++] = 0x7D; f[n++] = 0x4C; f[n++] = 0x57; f[n++] = 0x10;
    while (*t && n < sizeof f - 1u)
        f[n++] = (uint8_t)(*t++ & 0x7Fu);
    f[n++] = 0xF7;
    ota_wire_send(f, n);
}
static char *wifi_cat(char *q, const char *t, char *end)
{
    while (*t && q < end)
        *q++ = *t++;
    *q = 0;
    return q;
}
static char *wifi_num(char *q, int32_t v, char *end)
{
    char t[12];
    int k = 0;
    uint32_t u = v < 0 ? (uint32_t)-v : (uint32_t)v;
    if (v < 0 && q < end)
        *q++ = '-';
    do {
        t[k++] = (char)('0' + u % 10u);
        u /= 10u;
    } while (u);
    while (k && q < end)
        *q++ = t[--k];
    *q = 0;
    return q;
}
static int wifi_frame(const uint8_t *p, uint32_t n)
{
    static const char *const ST[] = {"off", "starting", "on", "joining", "address", "connected", "failed"};
    static const uint8_t end[6] = {0xF0, 0x7D, 0x4C, 0x57, 0x11, 0xF7};
    char b[88], *e = b + sizeof b - 1, *q;            /* (a long name: "Charles Vestal's iPhone" ...) */
    int i;
    if (n < 4u || p[0] != 0x7Du || p[1] != 0x4Cu || p[2] != 0x57u)
        return 0;
    switch (p[3]) {
    case 1: plat_wifi_on(1); break;
    case 2: plat_wifi_on(0); break;
    case 3: plat_wifi_scan(); break;
    case 4: {
        uint32_t ip = plat_wifi_ip();
        int st = plat_wifi_state();
        q = wifi_cat(b, st >= 0 && st <= WIFI_FAILED ? ST[st] : "?", e);
        q = wifi_cat(q, " ", e);
        q = wifi_cat(q, plat_wifi_ssid()[0] ? plat_wifi_ssid() : "-", e);
        for (i = 0; i < 4; i++) {
            q = wifi_cat(q, i ? "." : " ", e);
            q = wifi_num(q, (int32_t)(ip >> (8 * i) & 255u), e);
        }
        q = wifi_cat(q, " ", e);
        q = wifi_num(q, plat_wifi_rssi(), e);
        {
            uint32_t fps;
            int rr;
            plat_wifi_rx(&fps, &rr);
            q = wifi_cat(q, " rx ", e);
            q = wifi_num(q, (int32_t)fps, e);
            q = wifi_cat(q, "/s ", e);
            q = wifi_num(q, rr, e);
        }
        q = wifi_cat(q, " ", e);
        wifi_cat(q, plat_wifi_why(), e);
        wifi_line(b);
        break;
    }
    case 5:
        for (i = 0; plat_wifi_saved(i, b); i++)
            wifi_line(b);
        break;
    case 6:
        if (n >= 5u)
            plat_wifi_forget(p[4]);
        break;
    case 7: {                                    /* ssid 0 pass, as nibbles */
        static char ssid[33], pass[64];
        char *d = ssid;
        uint32_t k, len = 0, max = sizeof ssid - 1u, have_pass = 0;
        for (k = 4; k + 1u < n; k += 2) {
            char c = (char)((p[k] & 15u) << 4 | (p[k + 1] & 15u));
            if (!c && d == ssid) {
                ssid[len] = 0;
                d = pass;
                len = 0;
                max = sizeof pass - 1u;
                have_pass = 1;
                continue;
            }
            if (len < max)
                d[len++] = c;
        }
        d[len] = 0;
        plat_wifi_join(ssid, have_pass ? pass : 0);
        break;
    }
    case 8:
        for (i = 0; ; i++) {
            char ssid[33];
            int rssi, sec;
            if (!plat_wifi_found(i, ssid, &rssi, &sec))
                break;
            q = wifi_num(b, rssi, e);
            q = wifi_cat(q, sec ? " 1 " : " 0 ", e);
            wifi_cat(q, ssid, e);
            wifi_line(b);
        }
        break;
    default:
        break;
    }
    ota_wire_send(end, sizeof end);
    return 1;
}
#endif

static void fm1_fault(const fm1_crash_t *c)
{
    char b[12];
    uint32_t t0;
    fm1_audio_stop();
    lcd_fill(0, 0, 240, 240, RGB(160, 0, 0));
    draw_text_box(0, 8, 240, &FONT_S, APP_NAME " CRASH", C_WHITE, 1);
    hexs(b, c->vec);
    draw_text_box(10, 40, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->pc);
    draw_text_box(10, 60, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->emu);
    draw_text_box(10, 84, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->dbg);
    draw_text_box(10, 102, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->rets);
    draw_text_box(10, 120, 220, &FONT_S, b, C_WHITE, 0);
    t0 = fm1_ticks();
    while ((uint32_t)(fm1_ticks() - t0) < 4000u * 1000u * FM1_TICKS_PER_US)
        ;
    fm1_reboot();
}

static void enter_uboot(const char *why)
{
    fm1_audio_stop();
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 110, 240, &FONT_S, why, C_WHITE, 1);
    fm1_delay_ms(20);
    usb_detach();
    fm1_delay_ms(30);
    bootguard.pending = 0;                          /* intentional reset: not a failed boot */
    radio_clean_exit();
    fm1_enter_uboot();
}

/* SAFE MODE: the app crashed (or hung) twice while starting. Nothing that makes sound runs: no audio,
 * no engine, no UI; USB is on, so the web installer (this firmware's, or the stock one) and M-UPGRADE can
 * reach it. PLAY tries the app again. The chip's own update mode stays the last resort (fm1_cstart). */
static void safe_main(void)
{
    char b[24];
    uint32_t t0, play;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 18, 240, &FONT_B, APP_NAME " SAFE MODE", C_HI, 1);
    draw_text_box(0, 52, 240, &FONT_S, "IT CRASHED TWICE", C_WHITE, 1);
    draw_text_box(0, 72, 240, &FONT_S, "WHILE STARTING.", C_WHITE, 1);
    draw_text_box(0, 102, 240, &FONT_S, "NO SOUND. USB IS ON:", C_WHITE, 1);
    draw_text_box(0, 122, 240, &FONT_S, "REINSTALL FROM THE", C_WHITE, 1);
    draw_text_box(0, 142, 240, &FONT_S, "WEB INSTALLER.", C_WHITE, 1);
#if BOARD_SMK37
    draw_text_box(0, 172, 240, &FONT_S, "SEQ PLAY: TRY AGAIN", C_HI, 1);
#else
    draw_text_box(0, 172, 240, &FONT_S, "PLAY: TRY AGAIN", C_HI, 1);
#endif
    if (fm1_crash.magic == FM1_CRASH_MAGIC) {
        b[0] = 'P';
        b[1] = 'C';
        b[2] = ' ';
        hexs(b + 3, fm1_crash.pc);
        draw_text_box(0, 208, 240, &FONT_S, b, C_GRAY, 1);
    }
    fm1_input_init();
    panel_init();
    usb_start();
    fm1_timer5_start(isr_timer5, 1);
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    play = 1u << panel.btn[B_PLAY];
    t0 = fm1_ms;
    for (;;) {
        fm1_wdt_feed();
        usb_retry(fm1_ms);
        if (bootguard.pending && fm1_ms - t0 > 10000u)
            bootguard.pending = 0;                  /* safe mode itself is up: not another failed boot */
        if ((fm1_in.buttons & play) && fm1_ms - t0 > 500u) {
            bootguard.failed = 0;                   /* try the app again, from scratch */
            bootguard.pending = 0;
            draw_text_box(0, 172, 240, &FONT_S, "STARTING " APP_NAME "...", C_HI, 1);
            usb_detach();
            fm1_delay_ms(30);
            fm1_reboot();
        }
        {   /* the log readout works here too: what crashed is the question safe mode raises */
            const uint8_t *p;
            uint32_t n;
            if (ota_frame_get(&p, &n) && log_frame(p, n))
                ota_frame_done();
        }
        ota_service();
        if (usb.ota_req) {
            usb.ota_req = 0;
            if (flash_ok)
                ota_session();                      /* an install commits and resets; else back here */
        }
        if (usb.uboot_req)
            enter_uboot("UBOOT (USB)");
    }
}

static void splash(void)                           /* the app's (APP_SPLASH), or its name and tagline on paper */
{
#ifdef APP_SPLASH
    app_splash();
#else
    lcd_fill(0, 0, 240, 240, RGB(250, 244, 232));
    cv_begin(240, 64, RGB(250, 244, 232));
    {
        int32_t w = text_w(&APP_BIG_FONT, APP_NAME);
        cv_text((240 - w) / 2, 0, &APP_BIG_FONT, APP_NAME, APP_SPLASH_RGB);
        w = text_w(&FONT_S, APP_TAGLINE);
        cv_text((240 - w) / 2, 42, &FONT_S, APP_TAGLINE, RGB(150, 136, 118));
    }
    cv_blit(0, 92);
#endif
    lcd_sync();
}

static void fm1_main(void)
{
    int32_t knob = 512 * 16;
    uint32_t last_frame = 0;
    {
        uint32_t f = irq_save();
        fm1_vdc14_raise(7);                         /* M-VAVE's own supplies before anything loads them: 1.60 V, */
        fm1_sysvdd_raise(15);                       /* then 1.38 V (fm1_sys.h). At the boot loader's, flash fetches
                                                     * come back corrupted under load: the radio's trim crashed
                                                     * CTL-1 0.6 s into most Bluetooth starts */
#if BOARD_SMK37
        flash_ok = FL_JEDEC_OK(FL_FAR(fl_jedec_ram)());   /* the SMK-37's 1 MiB part (or the FM-1's), else RAM only */
#else
        flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;   /* the expected 1 MiB part, else RAM only */
#endif
        irq_restore(f);
        if (flash_ok) {
            ota_boot_cleanup();
            fl_plain_window_init();                 /* the data area read in place, unencrypted (plat_data) */
#if BOARD_SMK37
            st_owner_check();                       /* the SMK-37's store: this firmware's, or cleared (storage.c) */
#endif
        }
    }
    lcd_init();
    palette_set(APP_PALETTE);
    splash();
    if (fm1b_dbg.magic != DBG_MAGIC) {
        memset(&fm1b_dbg, 0, sizeof fm1b_dbg);
        fm1b_dbg.magic = DBG_MAGIC;
    }
    fm1b_dbg.in_audio = 0;                          /* (.noinit: a crash inside the render left it set, and safe mode,
                                                     * with no render to clear it, then never polled USB) */
    if (safe_mode)
        safe_main();                                /* never returns */
    if (fm1_crash.magic == FM1_CRASH_MAGIC && fm1_crash.count != fm1b_dbg.crash_seen) {
        char b[16];                                 /* a crash since the last boot: say so, a moment */
        fm1b_dbg.crash_seen = fm1_crash.count;
        b[0] = 'P';
        b[1] = 'C';
        b[2] = ' ';
        hexs(b + 3, fm1_crash.pc);
        draw_text_box(0, 176, 240, &FONT_S, "RESTARTED AFTER A CRASH", RGB(255, 80, 60), 1);
        draw_text_box(0, 196, 240, &FONT_S, b, C_GRAY, 1);
        fm1_wdt_feed();
        fm1_delay_ms(2500);
        fm1_wdt_feed();
    }
    fm1b_dbg.boots++;
    fm1b_dbg.max_us = 0;
    fm1_input_init();
    fm1_adc_init();
#if BOARD_SMK37
    smk_trim_init();                                /* the factory key / pad velocity curves (USRTRIM, read only) */
#endif
    panel_init();
    led_pos_init();
    app_init();                                     /* the app's engine and its saved state (or defaults) */
    audio_init();
    usb_start();
#if FM1B_TRS
    uart_midi_init();                               /* before TIMER5: it read-modify-writes port H too */
#endif
    /* above ALNK0 (3): the key / encoder / LED matrix is scanned one column per tick and must keep
     * its rhythm through a render (X0X's finding: below the audio, turns lost steps and LEDs flickered) */
    fm1_timer5_start(isr_timer5, 4);
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    fm1_delay_ms(30);
#if BOARD_SMK37
    if ((fm1_in.buttons & FM1_OCT_BOTH) == FM1_OCT_BOTH)
#else
    if ((fm1_in.buttons & 3u) == 3u)
#endif
        panel_setup();                              /* OCT- + OCT+ held at power-on */
    fm1_delay_ms(400);
    radio_boot_note();                                /* what ended the last Bluetooth run, into its log */
    if (!radio_last_bad && APP_BLE_AT_BOOT)
        ble_start();                                /* Bluetooth is on, as on the stock firmware (unless the app's
                                                     * setting says off: APP_BLE_AT_BOOT) */
#ifdef APP_BOOT
    app_boot();                                     /* the app's own start (a radio), the platform up */
#endif
    app_ui_init();

    for (;;) {
        fm1_wdt_feed();
        usb_retry(fm1_ms);
        if (fm1_ms > 30000u && bootguard.pending) {     /* a crash or hang in the first 30 s counts */
            bootguard.pending = 0;
            bootguard.failed = 0;
        }
        {   /* MASTER: smoothed, 0..4092; with the keys' readings on, the scan owns the converter and reads it too */
#if BOARD_SMK37
            int32_t a = fm1_adc_read(FM1_ADC_MASTER);   /* K5, a virtual pot (boards/smk37/fm1_adc.h) */
#else
            int32_t a = fm1_an.con || fm1_an.pend ? (int32_t)fm1_an.master : fm1_adc_read(FM1_ADC_MASTER);
#endif
            if (a >= 0) {
                knob += (a * 16 - knob) / 8;
                master_pos = remote.master >= 0 ? (uint32_t)remote.master : (uint32_t)(knob / 4);
            }
        }
        fm1b_dbg.stage = 2;                         /* (breadcrumbs: where a watchdog reset found the loop) */
        ble_poll();                                 /* Bluetooth's tasks, once started */
#if FM1B_WIFI
        wifi_poll();                                /* WiFi's tasks, once on */
#endif
        fm1b_dbg.stage = 3;
#ifdef APP_LOOP
        app_loop();                                 /* the app's background (a radio's tasks) */
#endif
        {   /* OCT- + OCT+ held 5 s: update mode (Felucca's, unchanged) */
            static uint32_t t0, shown;
            uint32_t both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
            if ((fm1_in.buttons & both) != both) {
                if (shown)
                    app_say("UPDATE MODE", "CANCELLED");
                shown = 0;
                t0 = fm1_ms;
            } else if (fm1_ms - t0 > 2000u && fm1_ms - t0 <= 5000u) {
                uint32_t left = (5000u - (fm1_ms - t0) + 999u) / 1000u;
                if (left != shown) {
                    char d[4] = {(char)('0' + left), '.', '.', 0};
                    app_say("UPDATE MODE IN", d);
                    shown = left;
                }
            } else if (fm1_ms - t0 > 5000u) {
                enter_uboot("UBOOT");
            }
        }
        {   /* a screen reader's SysEx (announce.c), before the updater looks */
            const uint8_t *p;
            uint32_t n;
            if (ota_frame_get(&p, &n) && (ann_frame(p, n, fm1_ms) || log_frame(p, n) || remote_frame(p, n)
#if FM1B_WIFI
                                          || wifi_frame(p, n)
#endif
#ifdef APP_SYSEX
                                          || app_sysex(p, n)
#endif
                                          ))
                ota_frame_done();
        }
        fm1b_dbg.stage = 4;
        ota_service();                              /* M-UPGRADE handshake (other SysEx is dropped) */
        if (usb.ota_req) {
            usb.ota_req = 0;
            app_panic();                            /* silence: the update rewrites the flash */
            if (flash_ok)
                ota_session();                      /* returns only if nothing was committed */
            app_ui_init();
        }
        if (usb.uboot_req)
            enter_uboot("UBOOT (USB)");
#if FM1B_CDC
        cdc_task();
#endif
        if (fm1_ms - last_frame >= 16u) {           /* ~60 frames/s at most; input in between */
            last_frame = fm1_ms;
            fm1b_dbg.ui_frames++;
            fm1b_dbg.stage = 5;
            app_frame();
#if BOARD_SMK37
            padled_flush();                         /* the pads' colours, between LCD transfers (lcd.c) */
#endif
        } else {
            fm1b_dbg.stage = 6;
            app_input_only();
        }
    }
}

void fm1_cstart(void)
{
    uint32_t *s, *d, p3, src, wdt;
    fm1_time_init();
    fm1_reset_reason();
    p3 = fm1_boot.p3_rst;
    if (p3 & 4u)
        wdt_stage = fm1b_dbg.stage;                 /* a watchdog reset: where the loop was (the log readout) */
    src = fm1_boot.rst_src;
    wdt = fm1_boot.wdt_con;
    fm1_wdt_arm(0x0D);
    if (bootguard.magic != BOOTGUARD_MAGIC) {
        bootguard.magic = BOOTGUARD_MAGIC;
        bootguard.failed = 0;
        bootguard.pending = 0;
    }
    if (bootguard.pending)
        bootguard.failed++;
    bootguard.pending = 1;
    if (bootguard.failed >= 4u) {                   /* safe mode failed too: the chip's own update mode */
        bootguard.failed = 0;
        bootguard.pending = 0;
        fm1_enter_uboot();
    }
    p3 |= (bootguard.failed >= 2u) << 8;            /* two failed boots: safe mode (kept past the .bss clear) */
    fm1_irq_init();
    for (d = _bss_start; d < _bss_end; d++)
        *d = 0;
    for (d = _pool_start; d < _pool_end; d++)
        *d = 0;
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++)
        *d = *s;
    for (s = _rt_load, d = _rt_start; d < _rt_end; s++, d++)
        *d = *s;
    fm1_mailbox_clear();
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);
    safe_mode = (uint8_t)(p3 >> 8);
    fm1_boot.p3_rst = (uint8_t)p3;
    fm1_boot.rst_src = src;
    fm1_boot.wdt_con = (uint8_t)wdt;
    fm1_main();
    for (;;)
        ;
}
