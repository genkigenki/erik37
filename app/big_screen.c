/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Patrick F. Kellogg
 *
 * Erik37 + FM-1 Big Screen (USB): while an app asks (F0 7D 'F' 'S' 00/01/02 F7, about once a second),
 * every rectangle sent to the LCD (fm1-base device/lcd.c lcd_fill / lcd_blit: bs_lcd) also goes out over USB as
 * SysEx, so FM-1 Mirror (github.com/pfkellogg/fm1-mirror) and FM-1 Mirror + Spyglass show the screen big, with the
 * voice playing ("ERIK37 CONCERT") on top of the spyglass. screen_mirror.c makes the messages; this is the glue.
 *
 *   in:   the hello comes through app_sysex (app.c, APP_SYSEX).
 *   out:  plat_sysex_try (device/unity.c): into the USB SysEx ring, never waiting on it, so the piano never waits on
 *         the screen. A row that doesn't fit is forgotten and drawn again later.
 * Nobody asking: nothing is done (bs_lcd returns at once).
 */
#define SM_BUFFER 2048u                         /* 2 KB of queued messages */
#include "screen_mirror.c"

static uint16_t bs_row[240];                     /* one row, back in RGB565 order (the panel's buffers are swapped) */
static uint8_t bs_new;                           /* a viewer just came: the whole screen for it */
/* What the viewer has: a 16-bit hash of each 40-pixel segment of each row (0: not known, send it). Erik37 draws
 * whole 240-pixel bands where something changed: only the segments that changed go out. */
#define BS_SEGW 40u
#define BS_NSEG 6u
static uint16_t bs_hash[240][BS_NSEG];
static uint8_t bs_redo;                          /* segments dropped: the app draws everything again */
static uint32_t bs_redo_at;                      /* the last such redraw asked for */

/* app.c app_sysex: a SysEx frame from USB (the bytes between F0 and F7); 1 = a hello, taken */
int bs_usb_sysex(const uint8_t *p, uint32_t n)
{
    uint8_t m[6];
    if (n != 4u || p[0] != 0x7Du || p[1] != 'F' || p[2] != 'S')
        return 0;
    m[0] = 0xF0; m[1] = 0x7D; m[2] = 'F'; m[3] = 'S'; m[4] = p[3]; m[5] = 0xF7;
    sm_sysex(m, 6);                              /* (a new viewer, or 01: sm_take_full_request, the whole screen) */
    return 1;
}

/* one SysEx message (F0 .. F7) out over USB: 0 sent, -2 busy (again later), -1 nobody there */
static int bs_send(const uint8_t *m, unsigned n)
{
    return plat_sysex_try(m, n);
}

static uint16_t bs_seg_hash(const uint16_t *p, uint32_t n)
{
    uint32_t i, h = 2166136261u;                 /* FNV-1a over the pixels */
    for (i = 0; i < n; i++)
        h = (h ^ p[i]) * 16777619u;
    h = (h ^ (h >> 16)) & 0xFFFFu;
    return (uint16_t)(h ? h : 1u);
}

/* queue pixels a .. b-1 of row y (bs_row holds x ..): 1 queued. Waits a little for room (USB empties fast), never
 * long: the drawing (and so the keys' response) goes on */
static int bs_queue(uint32_t x, uint32_t a, uint32_t b, uint32_t y, uint32_t start)
{
    uint32_t t0 = plat_ms();
    while (!sm_rows(bs_row + (a - x), a, y, b - a, 1)) {
        if (plat_ms() - t0 > 20u || plat_ms() - start > 60u)
            return 0;
        sm_poll(plat_ms(), bs_send);
    }
    return 1;
}

static void bs_run(uint32_t x, uint32_t a0, uint32_t b0, uint32_t yy, uint32_t start, const uint16_t *nh)
{
    uint32_t k, ok = bs_queue(x, a0, b0, yy, start);
    for (k = a0 / BS_SEGW; k <= (b0 - 1u) / BS_SEGW; k++)
        bs_hash[yy][k] = ok ? nh[k] : 0u;
    if (!ok)
        bs_redo = 1;
}

/* lcd.c: a rectangle about to go to the LCD; px (the panel's byte order, w pixels a row) or, for a fill, NULL and
 * the colour c. Each row's segments that changed go out; a segment only partly covered goes out as drawn, and the
 * viewer's copy of it is then unknown */
void bs_lcd(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px, uint16_t c)
{
    uint32_t r, i, start = plat_ms();
    if (!sm_on() || !w || x >= 240u || y >= 240u)
        return;
    if (x + w > 240u)
        w = 240u - x;
    if (y + h > 240u)
        h = 240u - y;
    for (r = 0; r < h; r++) {
        uint32_t yy = y + r, s, a0 = 0, b0 = 0, run = 0;
        uint16_t nh[BS_NSEG];
        for (i = 0; i < w; i++)
            bs_row[i] = px ? (uint16_t)((px[r * w + i] >> 8) | (px[r * w + i] << 8)) : c;
        for (s = x / BS_SEGW; s <= (x + w - 1u) / BS_SEGW; s++) {
            uint32_t a = s * BS_SEGW > x ? s * BS_SEGW : x, b = (s + 1u) * BS_SEGW < x + w ? (s + 1u) * BS_SEGW : x + w;
            int whole = a == s * BS_SEGW && b == (s + 1u) * BS_SEGW, changed = 1;
            nh[s] = 0;
            if (whole) {
                nh[s] = bs_seg_hash(bs_row + (a - x), BS_SEGW);
                changed = nh[s] != bs_hash[yy][s];
            }
            if (changed && run && a == b0) {     /* (joins the run before it) */
                b0 = b;
                continue;
            }
            if (run) {                           /* the run before it goes */
                bs_run(x, a0, b0, yy, start, nh);
                run = 0;
            }
            if (changed) {
                a0 = a;
                b0 = b;
                run = 1;
            }
        }
        if (run)
            bs_run(x, a0, b0, yy, start, nh);
    }
}

/* app.c app_frame (the main loop): sending what's queued, the voice's name, and the whole screen for a new viewer
 * (or, after segments were dropped, what's missing: at most five times a second) */
void bs_service(void)
{
    static char name[24];
    uint32_t now = plat_ms();
    sm_poll(now, bs_send);
    if (sm_on()) {
        const char *v = TONE_NAME[set.tone & 3];
        uint32_t i = 0, k;
        for (k = 0; "ERIK37 "[k]; k++)
            name[i++] = "ERIK37 "[k];
        for (k = 0; v[k] && i < sizeof name - 1u; k++)
            name[i++] = v[k];
        name[i] = 0;
        sm_name(name, now, bs_send);
    }
    if (sm_take_full_request() | bs_new) {      /* a new viewer: it has nothing yet */
        uint32_t k;
        for (k = 0; k < 240u * BS_NSEG; k++)
            bs_hash[k / BS_NSEG][k % BS_NSEG] = 0;
        bs_new = 0;
        bs_redo = 1;
        bs_redo_at = now - 1000u;
    }
    if (bs_redo && sm_on() && ui.state == ST_RUN && now - bs_redo_at >= 200u) {
        bs_redo = 0;                             /* every band again: what the viewer has is skipped */
        bs_redo_at = now;
        ui.full = 1;
    }
}
