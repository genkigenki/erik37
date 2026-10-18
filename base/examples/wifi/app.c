/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* The WiFi example (app.h): one screen. REC turns WiFi on or off (on at start), SEL looks for networks, SELECT picks
 * one of them and SAVE joins it (with the password kept for it, or none), PLAY fetches http://example.com/ from a
 * task of its own (net.c). Part of the unity build (device/unity.c, host/plat_host.c), after the platform: gfx.c is
 * in scope. */

static const char *const STATE[] = {"OFF", "STARTING", "ON", "JOINING", "GETTING AN ADDRESS", "CONNECTED", "FAILED"};

static struct {
    uint32_t btn;                        /* the buttons held at the last frame */
    int pick;                            /* the network SELECT picks in the scan's list */
    char msg[2][24];
    uint32_t msg_until;
    uint8_t redraw;
} ui;

void app_init(void) {}
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i;
    (void)master_q12;
    for (i = 0; i < 2u * n; i++)
        lr[i] = 0;
}
void app_panic(void) {}

void app_say(const char *a, const char *b)
{
    uint32_t i;
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1500u;
    ui.redraw = 1;
}

static char *put(char *d, const char *s)
{
    while (*s)
        *d++ = *s++;
    *d = 0;
    return d;
}
static char *put_int(char *d, int32_t v)
{
    char t[12];
    int k = 0;
    uint32_t u = v < 0 ? (uint32_t)-v : (uint32_t)v;
    if (v < 0)
        *d++ = '-';
    do {
        t[k++] = (char)('0' + u % 10u);
        u /= 10u;
    } while (u);
    while (k)
        *d++ = t[--k];
    *d = 0;
    return d;
}

static void input(void)
{
    uint32_t btn = plat_buttons(), down = btn & ~ui.btn;
    int32_t e = plat_enc(EN_SELECT);
    ui.btn = btn;
    if (e) {
        char ssid[33];
        ui.pick += e;
        if (ui.pick < 0)
            ui.pick = 0;
        while (ui.pick > 0 && !plat_wifi_found(ui.pick, 0, 0, 0))
            ui.pick--;
        if (plat_wifi_found(ui.pick, ssid, 0, 0))
            app_say("NETWORK", ssid);
        ui.redraw = 1;
    }
    if (down >> B_REC & 1u) {
        int on = plat_wifi_state() == WIFI_OFF;
        plat_wifi_on(on);
        app_say("WIFI", on ? "ON" : "OFF");
    }
    if (down >> B_SEL & 1u)
        app_say("WIFI", plat_wifi_scan() == 0 ? "LOOKING" : "NOT ON");
    if (down >> B_SAVE & 1u) {
        char ssid[33];
        if (plat_wifi_found(ui.pick, ssid, 0, 0) && plat_wifi_join(ssid, 0) == 0)
            app_say("JOINING", ssid);
        else
            app_say("NO NETWORK", "");
    }
    if (down >> B_PLAY & 1u)
        app_say("FETCH", net_fetch("example.com", "/") == 0 ? "EXAMPLE.COM" : "BUSY");
}
void app_input_only(void) { input(); }

/* a line of text in a strip of its own (the canvas holds 240 x 48: APP_CANVAS_PX), a second text right-aligned */
static void line(int32_t y, const char *t, uint16_t c, const char *right, int lit)
{
    cv_begin(240, 15, C_BLACK);
    if (lit)
        cv_rect(6, 0, 228, 14, C_LINE);
    cv_text(10, 1, &FONT_XS, t, c);
    if (right)
        cv_text(228 - text_w(&FONT_XS, right), 1, &FONT_XS, right, C_DIM);
    cv_blit(0, (uint32_t)y);
}

static void draw(void)
{
    char t[48], r[16], *q;
    uint32_t ip = plat_wifi_ip();
    int st = plat_wifi_state(), i;
    cv_begin(240, 32, C_BLACK);
    if (plat_ms() < ui.msg_until) {
        int32_t x = cv_text(10, 6, &FONT_B, ui.msg[0], C_WHITE);
        cv_text(x + 8, 6, &FONT_B, ui.msg[1], C_HI);
    } else {
        int32_t x = cv_text(10, 6, &FONT_B, APP_NAME, C_WHITE);
        cv_text(x + 8, 10, &FONT_XS, st >= 0 && st <= WIFI_FAILED ? STATE[st] : "?",
                st == WIFI_CONNECTED ? C_HI : C_GRAY);
    }
    cv_blit(0, 0);
    q = put(t, "Network ");
    put(q, plat_wifi_ssid()[0] ? plat_wifi_ssid() : "none");
    line(34, t, C_WHITE, 0, 0);
    q = put(t, "Address ");
    if (ip)
        for (i = 0; i < 4; i++) {
            q = put_int(q, (int32_t)(ip >> (8 * i) & 255u));
            if (i < 3)
                q = put(q, ".");
        }
    else
        put(q, "none");
    line(49, t, ip ? C_WHITE : C_GRAY, 0, 0);
    q = put(t, "Signal ");
    if (st == WIFI_CONNECTED)
        put_int(q, plat_wifi_rssi());
    else
        put(q, st == WIFI_FAILED ? plat_wifi_why() : "-");
    line(64, t, C_GRAY, 0, 0);
    r[0] = 0;
    if (net_bytes())
        put(put_int(r, (int32_t)net_bytes()), " B");
    line(82, net_result()[0] ? net_result() : "PLAY fetches example.com", net_busy() ? C_HI : C_WHITE, r, 0);
    line(100, "Networks around (SEL looks, SAVE joins)", C_GRAY, 0, 0);
    for (i = 0; i < 7; i++) {
        char ssid[33];
        int rssi = 0, sec = 0, have = plat_wifi_found(i, ssid, &rssi, &sec);
        if (have)
            put(put_int(r, rssi), sec ? " lock" : "");
        line(116 + i * 15, have ? ssid : "", i == ui.pick ? C_WHITE : C_GRAY, have ? r : 0, have && i == ui.pick);
    }
    line(222, "REC on / off   USB: fm1_remote.py wifi", C_DIM, 0, 0);
}

void app_ui_init(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    plat_wifi_on(1);                         /* on at start: it joins the networks kept */
    ui.redraw = 1;
}

void app_frame(void)
{
    static uint32_t last, last_draw;
    uint32_t now = plat_ms(), h;
    input();
    h = ((uint32_t)plat_wifi_state() | plat_wifi_ip() << 3 | (uint32_t)net_busy() << 1 |
         (uint32_t)plat_wifi_found(0, 0, 0, 0) << 2) ^ net_bytes() * 31u;
    if (h != last || (ui.msg_until && now >= ui.msg_until) || now - last_draw > 1000u) {
        last = h;
        if (now >= ui.msg_until)
            ui.msg_until = 0;
        ui.redraw = 1;
    }
    if (ui.redraw) {
        ui.redraw = 0;
        last_draw = now;
        draw();
    }
}

#ifdef PLAT_HOST
/* the host simulator has no lwIP (net.c is the device's): the fetch answers at once */
int net_fetch(const char *host, const char *path) { (void)host, (void)path; return 0; }
int net_busy(void) { return 0; }
const char *net_result(void) { return "HTTP/1.0 200 OK (host)"; }
uint32_t net_bytes(void) { return 1256; }
#endif
