/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal (from CTL-1 and MONK-1) */
/* Velocity and pressure from the keys (touch.h): sense.c's readings with CTL-1's settings, tuned by playing, and
 * MONK-1's pressure. Part of the unity build (device/unity.c, host/plat_host.c) before app.c, for an app with
 * APP_TOUCH. */
#include "sense.c"

/* the scale, tuned by playing (CTL-1, 2026-10-15): the analog keys' driven reading for 127 and for 1, the curve's
 * exponent x 100 at curve 0; the top row's probe 0 / 43, its exponent 2.40 */
#define TOUCH_FIRM 530u
#define TOUCH_LIGHT 896u
#define TOUCH_CURVE 70.0f

static uint8_t touch_live;
static int8_t touch_c, touch_s;
static uint32_t touch_held;              /* state: the sensed keys held (from the events) */
static uint8_t touch_v[NKEYS];

/* Velo's settings, as MONK-1 and CTL-1 use them; the curve's exponent x 2 at +5, / 2 at -5 */
static void touch_config(void)
{
    sn_cfg.touch = 900;
    sn_cfg.press = 400;
    sn_cfg.release = 600;
    sn_cfg.wait = 2;
    sn_cfg.v_lo = 0;
    sn_cfg.v_hi = 72;
    sn_cfg.vmax = 127;
    sn_cfg.at_range = 32;
    sn_cfg.drive = 2;
    sn_cfg.d_curve = (uint16_t)(TOUCH_CURVE * fm_exp2f((float)touch_c * 0.2f) + 0.5f);
    sn_cfg.d_ceil = TOUCH_LIGHT;
    sn_cfg.d_floor = TOUCH_FIRM;
    sn_cfg.d_at_range = 64;
    sn_cfg.t_lo = 0;                     /* the top row's probe: */
    sn_cfg.t_hi = 43;                    /* firm presses pile up at 43..45 */
    sn_cfg.t_curve = 240;
    sn_cfg.learn = 0;
}

void touch_on(int on)
{
    on = on != 0;
    touch_config();
    if (on == touch_live)
        return;
    if (on) {
        sense_init(plat_analog_mask());  /* fresh key states: nothing was sampled while off */
        touch_config();
        sense_cal_reset();
    }
    touch_held = 0;
    plat_sense(on);
    touch_live = (uint8_t)on;
}
int touch_is_on(void) { return touch_live; }
void touch_curve(int curve)
{
    touch_c = (int8_t)clamp(curve, -5, 5);
    touch_config();
}
void touch_sens(int sens) { touch_s = (int8_t)clamp(sens, -5, 5); }
int touch_sensed(uint32_t key) { return key < NKEYS && sense_is_analog(key); }

int touch_take(touch_ev_t *e)
{
    sense_ev_t s;
    while (touch_live && sense_take(&s)) {
        if (s.key >= NKEYS || (s.type != EV_ON && s.type != EV_OFF))
            continue;
        e->type = s.type == EV_ON ? TOUCH_PRESS : TOUCH_RELEASE;
        e->key = s.key;
        e->vel = s.vel ? s.vel : 1u;
        return 1;
    }
    return 0;
}

/* pressure, every 5 ms (MONK-1's): counted from where the key rests, not from where it landed (a key goes on
 * settling after the note), so the zero follows the driven reading down for P_SETTLE ms, then creeps after it,
 * and comes back up at once when the key is eased. The lean is the pill's resistance below its rest on a log
 * scale: P_DEAD of an octave is noise, P_SPAN more is 127. Smoothed over ~30 ms. */
#define P_SETTLE 250u                    /* ms */
#define P_DEAD 0.04f                     /* octaves of resistance */
#define P_SPAN(s) (0.8f * (1.0f - 0.85f * (s)))   /* 0..1: 0.8 .. 0.12 octave */
static float p_base[NKEYS], p_s[NKEYS];
static uint32_t p_t0[NKEYS], p_t;
static uint8_t p_on[NKEYS], p_val[NKEYS];
void touch_update(void)
{
    uint32_t i, now = plat_ms();
    float sens = 0.5f + (float)touch_s * 0.09f;
    if (!touch_live || now - p_t < 5u)
        return;
    p_t = now;
    for (i = 0; i < NKEYS; i++) {
        float d, lean, x;
        if (!sense_is_analog(i) || sn_key[i].st != SN_DOWN || !sn_key[i].dn) {
            p_on[i] = 0;
            p_s[i] = 0.0f;
            p_val[i] = 0;
            continue;
        }
        d = (float)(sn_key[i].d > SN_DBASE + 1u ? sn_key[i].d - SN_DBASE : 1u);
        if (!p_on[i]) {
            p_on[i] = 1;
            p_base[i] = d;
            p_t0[i] = now;
        }
        if (d > p_base[i])
            p_base[i] = d;                                     /* eased off: that is the rest now */
        else
            p_base[i] += (d - p_base[i]) * (now - p_t0[i] < P_SETTLE ? 0.25f : 0.0008f);
        lean = fm_log2f(p_base[i] / d);
        x = fm_clampf((lean - P_DEAD) / P_SPAN(sens), 0.0f, 1.0f);
        p_s[i] += (x - p_s[i]) * 0.17f;
        p_val[i] = (uint8_t)(p_s[i] * 127.0f + 0.5f);
    }
}
uint8_t touch_pressure(uint32_t key) { return key < NKEYS ? p_val[key] : 0; }
int touch_down(uint32_t key) { return key < NKEYS && touch_live && sn_key[key].st == SN_DOWN && sn_key[key].dn; }

void touch_poll(void)
{
    touch_ev_t e;
    while (touch_take(&e)) {
        if (e.type == TOUCH_PRESS) {
            touch_held |= 1u << e.key;
            touch_v[e.key] = e.vel;
        } else {
            touch_held &= ~(1u << e.key);
        }
    }
    touch_update();
}
uint32_t touch_keys(void)
{
    uint32_t k = plat_keys(), mask = 0, i;
    if (!touch_live)
        return k;
    for (i = 0; i < NKEYS; i++)
        mask |= (uint32_t)sense_is_analog(i) << i;
    return (k & ~mask) | touch_held | plat_remote_keys();   /* (remote keys: as pressed, no reading) */
}
uint8_t touch_vel(uint32_t key)
{
    if (key >= NKEYS)
        return 0;
    if (!sense_is_analog(key) && touch_live) {
        uint8_t v = sense_probe_velocity(key);
        if (v)
            touch_v[key] = v;
    }
    return touch_v[key] ? touch_v[key] : 100u;
}
uint8_t touch_vel_contact(uint32_t key) { return touch_live ? sense_probe_velocity(key) : 0; }

/* ---- the scan's readings, by key (from its interrupt) */
void app_key_sample(uint32_t key, uint32_t v, uint32_t ticks, int driven) { sense_sample(key, v, ticks, driven); }
int app_key_drive(uint32_t key) { return sense_want_drive(key); }
void app_key_probe(uint32_t key, uint32_t v, uint32_t pressed) { sense_probe(key, v, pressed); }
int app_key_led_ok(uint32_t key) { return key >= NKEYS || sn_key[key].st == SN_OPEN; }   /* open: its column may light */

#ifdef PLAT_HOST
/* ---- the host simulator: what the sensing would read from the FM-1 (CTL-1's and MONK-1's emulators): a
 * pulled-up sample under PRESS, a driven one at the depth the key's calibration maps to its velocity, then deeper
 * by the lean that touch_update reads as its pressure; the top seven keys through the probe */
#define TOUCH_PULL_DOWN 300u
static uint8_t h_vel[NKEYS];             /* 0 = up; else the velocity it lands with */
static float h_press[NKEYS];             /* 0..1 */
static uint16_t h_land[NKEYS];
static uint32_t h_t0[NKEYS];

static uint16_t touch_land_for(uint32_t k, uint32_t v)   /* sense_velocity_d, inverted */
{
    const sense_key_t *s = &sn_key[k];
    float r0 = (float)(s->dfloor > SN_DBASE ? s->dfloor - SN_DBASE : 1u), r1 = (float)(s->dceil - SN_DBASE);
    float c = (float)(sn_cfg.d_curve ? sn_cfg.d_curve : 1u) * 0.01f, t;
    if (v >= 127u)
        return (uint16_t)(s->dfloor > 1u ? s->dfloor - 1u : 1u);
    t = 1.0f - fm_powf(((float)v - 1.0f) / 126.0f, 1.0f / c);
    return (uint16_t)(SN_DBASE + (uint32_t)(r0 * fm_powf(r1 / r0, t) + 0.5f));
}

void app_host_tick(void)                 /* every 2 ms: each sensed key's samples */
{
    uint32_t k, now = plat_ms();
    if (now & 1u)
        return;
    for (k = 0; k < NKEYS; k++) {
        if (!sense_is_analog(k))
            continue;
        if (!h_vel[k]) {
            if (sn_key[k].st != SN_OPEN)
                sense_sample(k, 1023u, now * 24000u, 0);
            continue;
        }
        if (sn_key[k].st == SN_OPEN) {
            h_land[k] = touch_land_for(k, h_vel[k]);
            h_t0[k] = 0;
        }
        sense_sample(k, TOUCH_PULL_DOWN, now * 24000u, 0);
        if (sense_want_drive(k)) {
            uint32_t d = h_land[k];
            if (sn_key[k].st == SN_DOWN && !h_t0[k])
                h_t0[k] = now;
            if (h_t0[k] && now - h_t0[k] > P_SETTLE + 20u && p_on[k] && h_press[k] > 0.0f) {
                float lean = P_DEAD + h_press[k] * P_SPAN(0.5f + (float)touch_s * 0.09f);
                float r = p_base[k] / fm_exp2f(lean);
                d = SN_DBASE + (uint32_t)(r > 2.0f ? r : 2.0f);
            }
            sense_sample(k, d, now * 24000u, 1);
        }
    }
}

/* key k: vel 0 = up, else held, landed at vel; press 0..100 % */
void app_host_key(uint32_t k, uint32_t vel, uint32_t press)
{
    if (k >= NKEYS)
        return;
    h_vel[k] = (uint8_t)(vel > 127u ? 127u : vel);
    h_press[k] = (float)(press > 100u ? 100u : press) * 0.01f;
    if (sense_is_analog(k))
        return;
    if (vel) {                           /* the probe's rise above its open reading that maps to vel (the curve inverted) */
        uint32_t lo = sn_cfg.t_lo, hi = sn_cfg.t_hi > lo ? sn_cfg.t_hi : lo + 1u, i;
        float c = (float)(sn_cfg.t_curve ? sn_cfg.t_curve : sn_cfg.d_curve ? sn_cfg.d_curve : 1u) * 0.01f;
        float x = fm_powf(((float)h_vel[k] - 1.0f) / 126.0f, 1.0f / c);
        for (i = 0; i < 8u; i++)
            sense_probe(k, 100u, 0);
        sense_probe(k, 100u + lo + (uint32_t)((float)(hi - lo) * x + 0.5f), 1);
    } else {
        sense_probe(k, 100u, 0);
    }
}
#endif
