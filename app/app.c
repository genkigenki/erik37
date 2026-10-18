/* SPDX-License-Identifier: MPL-2.0
 * Erik37: Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* Erik37: a grand piano for the M-Vave SMK-37. The keys (velocity through the touch curve), the pedals (the PEDAL
 * jack, the pads, SEQ REC, the mod wheel, MIDI), the settings and their pages; the engine (dsp/erk.c) renders in
 * the audio interrupt. Part of fm1-base's unity build (device/unity.c, host/plat_host.c): gfx.c and announce.c are
 * in scope; draw.c, ui.c and boot.c are this app's. */
#include "dsp/erk.h"
#include "erk_bank_data.h"
#include "draw.c"

/* ---------------------------------------------------------------- settings */
#define SET_MAGIC 0x314B5245u            /* "ERK1" */
typedef struct {
    uint32_t magic;
    uint8_t tone, room, curve, una;      /* tone 0..3, room 0..3 (dry, chamber, hall, church), touch 0..3, una 0..100 */
    uint8_t size, amount, width, brill;  /* 0..100 */
    uint8_t reso, reso_on, bpm, rsv;     /* resonance 0..100; the metronome's tempo (bpm) */
    int8_t octave;                       /* -2 .. +2 */
    uint8_t rsv2[3];
} settings_t;
static settings_t set;
static uint8_t set_dirty;
static uint32_t set_changed_at;

static void settings_defaults(void)
{
    memset(&set, 0, sizeof set);
    set.magic = SET_MAGIC;
    set.tone = 0;
    set.room = 2;                        /* hall */
    set.curve = 1;
    set.una = 50;
    set.size = 50;
    set.amount = 38;
    set.width = 80;
    set.brill = 50;
    set.reso = 50;
    set.reso_on = 1;
    set.bpm = 96;
    set.octave = 0;
}

static int settings_ok(void)
{
    return set.magic == SET_MAGIC && set.tone < 4 && set.room < 4 && set.curve < 4 && set.una <= 100 &&
           set.size <= 100 && set.amount <= 100 && set.width <= 100 && set.brill <= 100 && set.reso <= 100 &&
           set.reso_on <= 1 && set.bpm >= 30 && set.bpm <= 240 && set.octave >= -2 && set.octave <= 2;
}

static const char *const CURVE_NAME[4] = {"Soft", "Normal", "Hard", "Fixed"};
static const float CURVE_EXP[4] = {0.55f, 0.95f, 1.7f, 0.0f};

static float curve_of(int c, float t)
{
    return c == 3 ? 0.78f : fpow(t, CURVE_EXP[c]);
}

static uint8_t touch_tab[128];               /* the touch curve: the key's velocity to the hammer's */

static void touch_build(void)
{
    int v;
    for (v = 0; v < 128; v++) {
        float t = (v < 1 ? 1 : v) / 127.0f;
        int o = set.curve == 3 ? 100 : (int)(1 + 126 * curve_of(set.curve, t) + 0.5f);
        touch_tab[v] = (uint8_t)(o < 1 ? 1 : o > 127 ? 127 : o);
    }
}

static int q15_of(int pct) { return pct * 32767 / 100; }

static void apply_settings(void)
{
    touch_build();
    erk_set_tone(set.tone, q15_of(set.brill), q15_of(set.una));
    erk_set_room(set.room, q15_of(set.size), set.amount * 20000 / 100);
    erk_set_width(q15_of(set.width));
    erk_set_resonance(set.reso_on ? set.reso * 2600 / 100 : 0);
}

static void changed(void)
{
    set_dirty = 1;
    set_changed_at = plat_ms();
    apply_settings();
}

/* ---------------------------------------------------------------- the main loop -> the audio interrupt */
enum { EV_SUS_PAD, EV_SOST, EV_SOFT, EV_ALL_OFF, EV_METRO };
#define EVQ 32u
static volatile uint32_t evq[EVQ];
static volatile uint32_t evq_w, evq_r;

static void ev_put(uint32_t type, uint32_t val)
{
    uint32_t w = evq_w;
    if (w - evq_r >= EVQ)
        return;
    evq[w % EVQ] = type << 24 | (val & 0xFFFFFFu);
    evq_w = w + 1u;
}

/* ---------------------------------------------------------------- the audio side */
static struct {
    uint8_t key_note[SMK_NKEYBED];       /* the MIDI note each keybed key started (the octave may change while held) */
    uint64_t held;                       /* keybed keys down, as the render saw them */
    int sus_pad, sus_jack, sus_midi, sus_wheel, sus_now;   /* 0..127 each; the pedal is the largest */
    int sost_pad, sost_midi, soft_pad, soft_midi, sost_now, soft_now;
    int jack_rest, jack_init;
    int metro_on;
    uint32_t metro_acc, metro_beat;
    volatile uint32_t last_vel, notes;   /* the last velocity (after the curve), notes played */
} au;

static int touch(int v) { return touch_tab[v < 0 ? 0 : v > 127 ? 127 : v]; }

static void midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    plat_midi_send((st >> 4) | st << 8 | d1 << 16 | d2 << 24, TO_USB);
}

static void pedals_update(void)
{
    int s = au.sus_pad, so = au.sost_pad || au.sost_midi, sf = au.soft_pad > au.soft_midi ? au.soft_pad : au.soft_midi;
    if (au.sus_jack > s)
        s = au.sus_jack;
    if (au.sus_midi > s)
        s = au.sus_midi;
    if (au.sus_wheel > s)
        s = au.sus_wheel;
    if (s != au.sus_now) {
        if ((s >= 64) != (au.sus_now >= 64) || s == 0 || s == 127)
            midi_out(0xB0u, 64u, (uint32_t)s);
        au.sus_now = s;
        erk_sustain(s);
    }
    if (so != au.sost_now) {
        au.sost_now = so;
        erk_sostenuto(so);
        midi_out(0xB0u, 66u, so ? 127u : 0u);
    }
    if (sf != au.soft_now) {
        au.soft_now = sf;
        erk_soft(sf);
        midi_out(0xB0u, 67u, (uint32_t)sf);
    }
}

static void keybed_poll(void)
{
#if BOARD_SMK37
    uint64_t hits = plat_keybed_hits(), now = plat_keybed(), off = au.held & ~now;
    uint32_t k;
    for (k = 0; (hits | off) && k < SMK_NKEYBED; k++) {   /* (nothing new: no loop) */
        uint64_t b = (uint64_t)1u << k;
        if ((off & b) || ((hits & b) && (au.held & b))) {   /* released (or struck again before we saw it go) */
            erk_note_off(au.key_note[k]);
            midi_out(0x80u, au.key_note[k], 0u);
        }
        if (hits & b) {
            int note = 48 + (int)k + 12 * set.octave, v = touch(plat_keybed_vel((int)k));
            if (note < 21 || note > 108)
                continue;
            au.key_note[k] = (uint8_t)note;
            erk_note_on(note, v);
            midi_out(0x90u, (uint32_t)note, (uint32_t)v);
            au.last_vel = (uint32_t)v;
            au.notes++;
            if (!(now & b)) {                 /* a tap already over */
                erk_note_off(note);
                midi_out(0x80u, (uint32_t)note, 0u);
            }
        }
    }
    au.held = now;
    {   /* the PEDAL jack: its rest reading learnt (either polarity), pressed = away from it; a continuous pedal
         * gives half pedal */
        int raw = plat_pedal_raw(), d;
        if (raw >= 0) {
            if (!au.jack_init) {
                au.jack_rest = raw;
                au.jack_init = 1;
            }
            d = raw - au.jack_rest;
            d = d < 0 ? -d : d;
            if (d < 40)
                au.jack_rest += (raw - au.jack_rest) / 32;   /* the rest drifts slowly */
            d = (d - 80) * 127 / 500;
            au.sus_jack = d < 0 ? 0 : d > 127 ? 127 : d;
        }
    }
    {
        int32_t pw, mw;
        if (plat_wheels(&pw, &mw))
            au.sus_wheel = mw < 0 ? 0 : mw > 127 ? 127 : mw;   /* the mod wheel: a sustain pedal with half pedal */
    }
#endif
}

static void midi_poll(void)
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        if ((st & 0xF0u) == 0x90u && d2) {
            erk_note_on((int)d1, touch((int)d2));
            au.last_vel = d2;
        } else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u) {
            erk_note_off((int)d1);
        } else if ((st & 0xF0u) == 0xB0u) {
            if (d1 == 64u)
                au.sus_midi = (int)d2;
            else if (d1 == 66u)
                au.sost_midi = d2 >= 64u;
            else if (d1 == 67u)
                au.soft_midi = (int)d2;
            else if (d1 == 120u || d1 == 123u)
                erk_all_off();
        }
    }
}

static void events_poll(void)
{
    while (evq_r != evq_w) {
        uint32_t e = evq[evq_r % EVQ], v = e & 0xFFFFFFu;
        evq_r = evq_r + 1u;
        switch (e >> 24) {
        case EV_SUS_PAD: au.sus_pad = (int)v; break;
        case EV_SOST: au.sost_pad = (int)v; break;
        case EV_SOFT: au.soft_pad = (int)v; break;
        case EV_ALL_OFF: erk_all_off(); break;
        case EV_METRO:
            au.metro_on = (int)v;
            au.metro_acc = 0xFFFFFFFFu;       /* the first tick at once */
            au.metro_beat = 0;
            break;
        }
    }
}

#define SUBBLK 64u
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t s;
    for (s = 0; s < n; s += SUBBLK) {         /* the keys every 64 frames: ~1.5 ms */
        events_poll();
        keybed_poll();
        midi_poll();
        pedals_update();
        if (au.metro_on) {
            uint32_t per = 44100u * 60u / (set.bpm ? set.bpm : 96u);
            if (au.metro_acc >= per) {
                au.metro_acc = au.metro_acc == 0xFFFFFFFFu ? 0 : au.metro_acc - per;
                erk_click(au.metro_beat % 4u == 0u);
                au.metro_beat++;
            }
            au.metro_acc += SUBBLK;
        }
        erk_render_gain(lr + 2u * s, (int)(n - s < SUBBLK ? n - s : SUBBLK), (int32_t)master_q12);
    }
}

void app_panic(void) { erk_panic(); }
void app_overload(void) { erk_shed(); }

static const erk_cfg ENGINE = {ERK_REV_HALL, 7600, 1300, 16000, 9000, ERK_SAMPLED, 0};
static erk_bank bank;

void app_init(void)
{
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || !settings_ok())
        settings_defaults();
    erk_bank_open(&bank, ERK_BANK);
    erk_init(&bank, &ENGINE);
    apply_settings();
}

/* ---------------------------------------------------------------- the UI */
enum { PG_PLAY, PG_TONE, PG_ROOM, PG_ABOUT, NPAGES };
enum { ST_BOOT, ST_RUN };
static struct {
    int state, page, drawn_page;
    uint32_t boot_t0;
    int boot_frame;
    int active;                          /* the knob being turned (-1 none) */
    uint32_t active_at;
    uint32_t btn, sbtn;                  /* held at the last input */
    uint32_t pads_prev, pad_down_at[16];
    uint8_t pad_consumed[16];
    int latch_sus, latch_sost, latch_soft, hold_sus, hold_sost, hold_soft, rec_down;
    uint32_t rec_at;
    int metro;
    int fad_last[4];
    uint32_t home_at;
    int debug;
    uint32_t debug_drawn;
    /* what is on the screen (redraw when it changes) */
    uint8_t lv_q[37], held_q[37], damp_q[37];
    int sus_q, sost_q, soft_q, vel_q, oct_q, strip_q, head_q;
    char msg[2][24];
    uint32_t msg_until;
    int full;                            /* the whole page again */
    uint32_t strings_at;                 /* the strings' last redraw */
} ui;

static int ui_note(int i) { return 48 + 12 * set.octave + i; }
static int note_sost(int note)
{
    int k = note - ERK_KEY0;
    return k >= 0 && k < ERK_KEYS && (erk_sost_keys[k >> 5] >> (k & 31) & 1u);
}

void app_say(const char *a, const char *b)
{
    uint32_t i;
    char t[48];
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
    ui.head_q = -1;
    if (ann_on()) {
        t[0] = 0;
        cat_s(t, ui.msg[0]);
        if (ui.msg[1][0]) {
            cat_s(t, " ");
            cat_s(t, ui.msg[1]);
        }
        ann_say(t, 0, ANN_NOW);
    }
}

#include "ui.c"
#include "boot.c"

/* ---------------------------------------------------------------- the knobs of each page */
static void knobs_of(int page, knob_t *k)
{
    int i;
    memset(k, 0, sizeof(knob_t) * 4);
    for (i = 0; i < 4; i++)
        k[i].amt = -1;
    if (page == PG_PLAY) {
        k[0].label = "tone";
        cat_s(k[0].value, TONE_TITLE[set.tone]);
        k[0].n = 4;
        k[0].cur = set.tone;
        k[1].label = "room";
        cat_s(k[1].value, ROOM_TITLE[set.room]);
        k[1].n = 4;
        k[1].cur = set.room;
        k[2].label = "reverb";
        pct_text(k[2].value, set.amount);
        k[2].amt = set.amount / 100.0f;
        k[3].label = "brilliance";
        pct_text(k[3].value, set.brill);
        k[3].amt = set.brill / 100.0f;
    } else if (page == PG_TONE) {
        k[0].label = "touch";
        cat_s(k[0].value, CURVE_NAME[set.curve]);
        k[0].n = 4;
        k[0].cur = set.curve;
        k[1].label = "brilliance";
        pct_text(k[1].value, set.brill);
        k[1].amt = set.brill / 100.0f;
        k[2].label = "resonance";
        if (set.reso_on)
            pct_text(k[2].value, set.reso);
        else
            cat_s(k[2].value, "off");
        k[2].amt = set.reso / 100.0f;
        k[3].label = "una corda";
        pct_text(k[3].value, set.una);
        k[3].amt = set.una / 100.0f;
    } else if (page == PG_ABOUT) {                /* what to check on a unit: the audio's load, the voices */
        static int vmax;
        int v = erk_active();
        vmax = v > vmax ? v : vmax;
        k[0].label = "audio";
        itoa_s((int)plat_cpu_pct(), k[0].value);
        cat_s(k[0].value, " %");
        k[0].amt = plat_cpu_pct() / 100.0f;
        k[1].label = "voices";
        itoa_s(v, k[1].value);
        k[1].amt = v / (float)ERK_NV;
        k[2].label = "most";
        itoa_s(vmax, k[2].value);
        k[2].amt = vmax / (float)ERK_NV;
        k[3].label = "xruns";
        itoa_s((int)plat_xruns(), k[3].value);
        k[3].amt = 0;
    } else {
        float secs = erk_room_seconds(set.room, q15_of(set.size));
        int ds = (int)(secs * 10 + 0.5f);
        char t[8];
        k[0].label = "room";
        cat_s(k[0].value, ROOM_TITLE[set.room]);
        k[0].n = 4;
        k[0].cur = set.room;
        k[1].label = "size";
        itoa_s(ds / 10, t);
        cat_s(k[1].value, t);
        cat_s(k[1].value, ".");
        itoa_s(ds % 10, t);
        cat_s(k[1].value, t);
        cat_s(k[1].value, " s");
        k[1].amt = set.size / 100.0f;
        k[2].label = "amount";
        pct_text(k[2].value, set.amount);
        k[2].amt = set.amount / 100.0f;
        k[3].label = "width";
        pct_text(k[3].value, set.width);
        k[3].amt = set.width / 100.0f;
    }
}

/* a page knob by detents (or a fader's absolute position: abs >= 0, 0..127) */
static void knob_turn(int page, int i, int d, int abs)
{
    uint8_t *v = 0;
    int lo = 0, hi = 100, step = 2, choice = 0;
    if (page == PG_ABOUT)
        return;
    if (page == PG_PLAY) {
        static uint8_t *const P[4] = {&set.tone, &set.room, &set.amount, &set.brill};
        v = P[i];
        choice = i < 2;
    } else if (page == PG_TONE) {
        static uint8_t *const P[4] = {&set.curve, &set.brill, &set.reso, &set.una};
        v = P[i];
        choice = i == 0;
        if (i == 2)
            set.reso_on = 1;
    } else {
        static uint8_t *const P[4] = {&set.room, &set.size, &set.amount, &set.width};
        v = P[i];
        choice = i == 0;
    }
    if (choice) {
        hi = 3;
        step = 1;
    }
    {
        int nv = abs >= 0 ? (choice ? abs * 4 / 128 : abs * 100 / 127) : *v + d * step;
        nv = nv < lo ? lo : nv > hi ? hi : nv;
        if (nv != *v) {
            *v = (uint8_t)nv;
            changed();
        }
    }
    ui.active = i;
    ui.active_at = plat_ms();
    ui.strip_q = -1;
    if (page != PG_PLAY || choice)
        ui.full = page != PG_PLAY;           /* the settings pages draw their pictures from the values */
    if (page == PG_PLAY && i < 2)
        ui.head_q = -1;
}

/* ---------------------------------------------------------------- pads and lights */
enum { LV_OFF, LV_ON, LV_ACTIVE };
static void pad_light(int p, int lv)
{
#if BOARD_SMK37
    static const uint8_t L[3][3] = {{72, 54, 34}, {255, 190, 110}, {255, 140, 16}};
    plat_pad_rgb(p, L[lv][0], L[lv][1], L[lv][2]);
#else
    (void)p;
    (void)lv;
#endif
}

static int brill_step(void) { return set.brill < 38 ? 0 : set.brill < 66 ? 1 : 2; }

static void lights(uint32_t pads_held)
{
    int p;
    for (p = 0; p < 16; p++) {
        int lv = LV_OFF, held = (int)(pads_held >> p & 1u);
        if (p < 4)
            lv = set.tone == p ? LV_ON : LV_OFF;
        else if (p < 8)
            lv = set.room == p - 4 ? LV_ON : LV_OFF;
        else if (p == 8)
            lv = ui.hold_soft ? LV_ACTIVE : ui.latch_soft ? LV_ON : LV_OFF;
        else if (p == 9)
            lv = ui.hold_sost ? LV_ACTIVE : ui.latch_sost ? LV_ON : LV_OFF;
        else if (p == 10)
            lv = ui.hold_sus ? LV_ACTIVE : ui.latch_sus ? LV_ON : LV_OFF;
        else if (p == 11)
            lv = set.reso_on ? LV_ON : LV_OFF;
        else if (p < 15)
            lv = brill_step() == p - 12 ? LV_ON : LV_OFF;
        else
            lv = ui.metro ? LV_ON : LV_OFF;
        if (held && lv != LV_ACTIVE && p >= 8 && p <= 10)
            lv = LV_ACTIVE;
        pad_light(p, held && (p < 8 || p > 10) ? LV_ACTIVE : lv);
    }
    {
        uint32_t b = 0;
        if (ui.latch_sus || ui.rec_down)
            b |= 1u << B_REC;
        if (ui.metro)
            b |= LED_PLAY_GREEN;
        if (set_dirty)
            b |= 1u << B_SAVE;
        if (set.octave < 0)
            b |= 1u << B_OCTDN;
        if (set.octave > 0)
            b |= 1u << B_OCTUP;
        plat_lights(b, 0, 0, 0, 0);
    }
}

/* a pedal pad (or SEQ REC): a tap latches it (the next tap lets go), held it is down while held */
static void pedal_press(int *latch, int *hold, uint8_t *consumed)
{
    if (*latch) {
        *latch = 0;
        *consumed = 1;
    } else {
        *hold = 1;
        *consumed = 0;
    }
}
static void pedal_release(int *latch, int *hold, uint8_t consumed, uint32_t held_ms)
{
    if (!consumed && held_ms < 300u)
        *latch = 1;
    *hold = 0;
}

static void pedals_send(void)
{
    ev_put(EV_SUS_PAD, (ui.latch_sus || ui.hold_sus || ui.rec_down) ? 127u : 0u);
    ev_put(EV_SOST, (uint32_t)(ui.latch_sost || ui.hold_sost));
    ev_put(EV_SOFT, (ui.latch_soft || ui.hold_soft) ? 127u : 0u);
}

static void pads_input(void)
{
#if BOARD_SMK37
    uint32_t hits = plat_pad_hits(), held = plat_pads_held(), up = ui.pads_prev & ~held, now = plat_ms();
    int p;
    for (p = 0; p < 16; p++) {
        uint32_t b = 1u << p;
        if (hits & b) {
            ui.pad_down_at[p] = now;
            if (p < 4) {
                if (set.tone != p) {
                    set.tone = (uint8_t)p;
                    changed();
                    ui.head_q = ui.strip_q = -1;
                    ui.full |= ui.page == PG_TONE;
                }
            } else if (p < 8) {
                if (set.room != p - 4) {
                    set.room = (uint8_t)(p - 4);
                    changed();
                    ui.head_q = ui.strip_q = -1;
                    ui.full |= ui.page == PG_ROOM;
                }
            } else if (p == 8) {
                pedal_press(&ui.latch_soft, &ui.hold_soft, &ui.pad_consumed[p]);
            } else if (p == 9) {
                pedal_press(&ui.latch_sost, &ui.hold_sost, &ui.pad_consumed[p]);
            } else if (p == 10) {
                pedal_press(&ui.latch_sus, &ui.hold_sus, &ui.pad_consumed[p]);
            } else if (p == 11) {
                set.reso_on ^= 1u;
                changed();
                ui.strip_q = -1;
            } else if (p < 15) {
                static const uint8_t B[3] = {25, 50, 80};
                set.brill = B[p - 12];
                changed();
                ui.strip_q = -1;
            } else {
                ui.pad_consumed[15] = 0;        /* the metronome: on the release (held, K1 sets its tempo) */
            }
        }
        if (up & b) {
            uint32_t ms = now - ui.pad_down_at[p];
            if (p == 8)
                pedal_release(&ui.latch_soft, &ui.hold_soft, ui.pad_consumed[p], ms);
            else if (p == 9)
                pedal_release(&ui.latch_sost, &ui.hold_sost, ui.pad_consumed[p], ms);
            else if (p == 10)
                pedal_release(&ui.latch_sus, &ui.hold_sus, ui.pad_consumed[p], ms);
            else if (p == 15 && !ui.pad_consumed[15]) {
                ui.metro = !ui.metro;
                ev_put(EV_METRO, (uint32_t)ui.metro);
            }
        }
    }
    if ((hits | up) & 0x700u)
        pedals_send();
    ui.pads_prev = held;
    lights(held);
#endif
}

static void page_set(int pg)
{
    pg = (pg % NPAGES + NPAGES) % NPAGES;
    if (pg != ui.page) {
        ui.page = pg;
        ui.full = 1;
    }
}

static void input(void)
{
    uint32_t btn = plat_buttons(), down = btn & ~ui.btn, now = plat_ms();
    int i;
    int32_t e;
    if (ui.state == ST_BOOT) {                /* any control ends the boot animation */
        uint32_t sb = 0;
#if BOARD_SMK37
        sb = plat_smk_buttons() | plat_pads_held();
#endif
        if (down || sb) {
            ui.state = ST_RUN;
            ui.full = 1;
        }
    }
    if (down & (1u << B_OCTDN) && set.octave > -2) {
        set.octave--;
        changed();
        ui.oct_q = -99;
    }
    if (down & (1u << B_OCTUP) && set.octave < 2) {
        set.octave++;
        changed();
        ui.oct_q = -99;
    }
    if (down & (1u << B_HOME)) {
        page_set(PG_PLAY);
        ui.home_at = now;
    }
    if (btn & (1u << B_HOME)) {
        if (!ui.debug && now - ui.home_at > 800u) {   /* PATCH held: the audio load, the voices */
            ui.debug = 1;
            ui.head_q = -1;
        }
    } else if (ui.debug) {
        ui.debug = 0;
        ui.head_q = -1;
    }
    if (down & (1u << B_SAVE)) {
        if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0) {
            set_dirty = 0;
            app_say("SAVED", "");
        } else {
            app_say("SAVE", "FAILED");
        }
    }
    if (down & (1u << B_REC)) {               /* SEQ REC: the sustain pedal (tap: latched) */
        ui.rec_at = now;
        if (ui.latch_sus) {
            ui.latch_sus = 0;
            ui.rec_down = 0;
        } else {
            ui.rec_down = 1;
        }
        pedals_send();
    }
    if ((ui.btn & ~btn) & (1u << B_REC)) {
        if (ui.rec_down && now - ui.rec_at < 300u)
            ui.latch_sus = 1;
        ui.rec_down = 0;
        pedals_send();
    }
    if (down & (1u << B_PLAY)) {              /* SEQ PLAY: the metronome */
        ui.metro = !ui.metro;
        ev_put(EV_METRO, (uint32_t)ui.metro);
    }
    if (down & (1u << B_FX))                  /* FX: every note let go */
        ev_put(EV_ALL_OFF, 0);
    ui.btn = btn;
#if BOARD_SMK37
    {
        uint32_t sb = plat_smk_buttons(), sd = sb & ~ui.sbtn;
        if (sd & (1u << SB_LEFT))
            page_set(ui.page - 1);
        if (sd & (1u << SB_RIGHT))
            page_set(ui.page + 1);
        ui.sbtn = sb;
    }
    for (i = 0; i < 4; i++) {                 /* the faders: the page's four knobs, once moved */
        int f = plat_fader(i);
        if (f < 0)
            continue;
        if (ui.fad_last[i] < 0)
            ui.fad_last[i] = f;
        else if (f > ui.fad_last[i] + 2 || f < ui.fad_last[i] - 2) {
            ui.fad_last[i] = f;
            knob_turn(ui.page, i, 0, f);
        }
    }
#endif
    for (i = 0; i < 4; i++)
        if ((e = plat_enc(EN_K1 + i)) != 0) {
#if BOARD_SMK37
            if (i == 0 && (plat_pads_held() >> 15 & 1u)) {   /* pad 16 held: K1 sets the metronome's tempo */
                char t[16];
                set.bpm = (uint8_t)clamp(set.bpm + 2 * e, 30, 240);
                changed();
                ui.pad_consumed[15] = 1;
                itoa_s(set.bpm, t);
                cat_s(t, " BPM");
                app_say("TEMPO", t);
                continue;
            }
#endif
            knob_turn(ui.page, i, e, -1);
        }
    if ((e = plat_enc(EN_SELECT)) != 0)       /* K6 SELECT: the page */
        page_set(ui.page + e);
    if ((e = plat_enc(EN_ALGO)) != 0) {       /* K7 ALGORITHM: the tone */
        int t = clamp(set.tone + e, 0, 3);
        if (t != set.tone) {
            set.tone = (uint8_t)t;
            changed();
            ui.head_q = ui.strip_q = -1;
            ui.full |= ui.page == PG_TONE;
        }
    }
    if ((e = plat_enc(EN_PRESET)) != 0) {     /* K8 PRESETS: the room */
        int r = clamp(set.room + e, 0, 3);
        if (r != set.room) {
            set.room = (uint8_t)r;
            changed();
            ui.head_q = ui.strip_q = -1;
            ui.full |= ui.page == PG_ROOM;
        }
    }
    pads_input();
    if (ui.active >= 0 && now - ui.active_at > 1200u) {
        ui.active = -1;
        ui.strip_q = -1;
    }
    /* saved by itself once the piano is quiet (a flash write stops the sound for a moment) */
    if (set_dirty && now - set_changed_at > 2000u && erk_active() == 0) {
        if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0)
            set_dirty = 0;
    }
}

void app_input_only(void) { input(); }

/* ---------------------------------------------------------------- drawing the pages */
static void draw_header(void)
{
    restore(0, RULE_Y);
    if (ui.msg_until && plat_ms() < ui.msg_until) {
        header_text(ui.msg[0], ui.msg[1][0] ? ui.msg[1] : 0);
    } else if (ui.debug) {
        char t[32], n[12];
        t[0] = 0;
        cat_s(t, "CPU ");
        itoa_s((int)plat_cpu_pct(), n);
        cat_s(t, n);
        cat_s(t, "%  V ");
        itoa_s(erk_active(), n);
        cat_s(t, n);
        cat_s(t, "  X ");
        itoa_s((int)plat_xruns(), n);
        cat_s(t, n);
        header_text(t, 0);
    } else if (ui.page == PG_PLAY) {
        header_text(TONE_NAME[set.tone], ROOM_NAME[set.room]);
    } else {
        header_text(ui.page == PG_TONE ? "TONE & TOUCH" : ui.page == PG_ROOM ? "ROOM" : "ABOUT", 0);
        page_dots(3, ui.page - 1);
    }
}

static void draw_strip(void)
{
    knob_t k[4];
    restore(STRIP_Y + 1, SH);
    knobs_of(ui.page, k);
    knob_strip(k, ui.active);
}

static float lvl_of(int note)
{
    int k = note - ERK_KEY0;
    float g;
    if (k < 0 || k >= ERK_KEYS)
        return 0;
    g = erk_key_lvl[k] / 32768.0f;
    g = esqrt(g) * 1.6f;                      /* the eye hears it: a soft note still shows */
    return g > 1 ? 1 : g;
}

static void draw_play(int force)
{
    float lvl[37], sus = erk_sus_q15 / 32767.0f, sost = au.sost_now ? 1.0f : 0.0f, una = erk_soft_q15 / 32767.0f;
    uint8_t held[37];
    uint64_t kb = 0;
    int i, chg = force;
#if BOARD_SMK37
    kb = plat_keybed();
#endif
    sus = sus < 0.25f ? 0 : sus > 0.9f ? 1 : sus;
    for (i = 0; i < 37; i++) {
        int note = ui_note(i), q, d;
        lvl[i] = lvl_of(note);
        held[i] = (uint8_t)(kb >> i & 1u);
        q = (int)(lvl[i] * 24);
        d = note_sost(note);
        if (q != ui.lv_q[i] || held[i] != ui.held_q[i] || d != ui.damp_q[i])
            chg = 1;
        ui.lv_q[i] = (uint8_t)q;
        ui.held_q[i] = held[i];
        ui.damp_q[i] = (uint8_t)d;
    }
    if ((int)(sus * 8) != ui.sus_q)
        chg = 1;
    if (chg && (force || plat_ms() - ui.strings_at >= 33u)) {   /* the strings: ~30 frames a second at most */
        ui.strings_at = plat_ms();
        restore(27, 151);
        dampers(lvl, held, sus);
        lit_strings(lvl);
        keyboard(lvl, held);
    }
    if (force || (int)(sus * 8) != ui.sus_q || (int)sost != ui.sost_q || (int)(una * 8) != ui.soft_q ||
        (int)au.last_vel != ui.vel_q || set.octave != ui.oct_q) {
        char r[24], t[4];
        ui.sus_q = (int)(sus * 8);
        ui.sost_q = (int)sost;
        ui.soft_q = (int)(una * 8);
        ui.vel_q = (int)au.last_vel;
        ui.oct_q = set.octave;
        restore(151, STRIP_Y - 1);
        pedals(sus, sost, una > 0.5f ? 1.0f : una);
        text(26, PEDAL_Y - 7, &F_DYN17, dynamic_mark(au.last_vel ? (int)au.last_vel : 64), GOLD, 0, 'l', 1.0f);
        r[0] = 'C';
        itoa_s(3 + set.octave, r + 1);
        cat_s(r, " \x80 C");
        itoa_s(6 + set.octave, t);
        cat_s(r, t);
        text(214, PEDAL_Y - 2, &F_TEXT11, r, IVORY_DIM, 0, 'r', 1.0f);
    }
}

static void draw_page(void)
{
    int force = ui.full || ui.drawn_page != ui.page;
    if (force) {
        art_load(ui.page == PG_PLAY ? &ART_PLAY : &ART_SET);
        restore(0, SH);
        ui.drawn_page = ui.page;
        ui.head_q = ui.strip_q = -1;
        ui.full = 0;
        if (ui.page == PG_TONE)
            tone_page(set.curve, au.last_vel ? (int)au.last_vel : 88);
        else if (ui.page == PG_ROOM)
            room_page(set.room, set.size / 100.0f, set.amount / 100.0f, set.width / 100.0f);
        else if (ui.page == PG_ABOUT)
            about_page();
    }
    if (ui.head_q < 0 || (ui.msg_until && plat_ms() >= ui.msg_until) ||
        (ui.debug && plat_ms() - ui.debug_drawn > 500u)) {
        if (ui.msg_until && plat_ms() >= ui.msg_until)
            ui.msg_until = 0;
        ui.debug_drawn = plat_ms();
        ui.head_q = 0;
        draw_header();
    }
    if (ui.strip_q < 0 || (ui.page == PG_ABOUT && plat_ms() - ui.debug_drawn > 500u)) {
        ui.strip_q = 0;
        ui.debug_drawn = plat_ms();
        draw_strip();
    }
    if (ui.page == PG_PLAY)
        draw_play(force);
    else if (ui.page == PG_TONE && (int)au.last_vel != ui.vel_q) {
        ui.vel_q = (int)au.last_vel;
        ui.full = 1;                          /* the dot follows the last note: next frame */
    }
    flush();
}

/* ---------------------------------------------------------------- boot */
static int ui_ready;
static void ui_setup(void)
{
    if (ui_ready)
        return;
    ui_ready = 1;
    draw_init();
    boot_init();
}

void app_splash(void)
{
    ui_setup();
    art_load(&ART_BOOT);
    boot_step(0);
    flush();
}

void app_ui_init(void)
{
    int i;
    ui_setup();
#if BOARD_SMK37
    plat_smk_pads(SMK_PADS_OWN);
    plat_pad_lights(1);
#endif
    ui.active = -1;
    ui.drawn_page = -1;
    ui.full = 1;
    for (i = 0; i < 4; i++)
        ui.fad_last[i] = -1;
    if (ui.state == ST_RUN)                   /* (after an update session: straight back) */
        return;
    ui.state = ST_BOOT;
    ui.boot_t0 = plat_ms();
    ui.boot_frame = -1;
    art_load(&ART_BOOT);
}

#define BOOT_MS 66u
void app_frame(void)
{
    input();
    if (ui.state == ST_BOOT) {
        uint32_t t = plat_ms() - ui.boot_t0;
        int f = (int)(t / BOOT_MS);
        if (f >= BOOT_FRAMES + 12) {          /* the last frame held ~0.8 s */
            ui.state = ST_RUN;
            ui.full = 1;
        } else {
            if (f >= BOOT_FRAMES)
                f = BOOT_FRAMES - 1;
            if (f != ui.boot_frame) {
                ui.boot_frame = f;
                boot_step(f);
                flush();
            }
            return;
        }
    }
    draw_page();
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "page"))
        *got = ui.page;
    else if (!strcmp(what, "boot"))
        *got = ui.state == ST_BOOT;
    else if (!strcmp(what, "tone"))
        *got = set.tone;
    else if (!strcmp(what, "room"))
        *got = set.room;
    else if (!strcmp(what, "octave"))
        *got = set.octave;
    else if (!strcmp(what, "curve"))
        *got = set.curve;
    else if (!strcmp(what, "brill"))
        *got = set.brill;
    else if (!strcmp(what, "reso_on"))
        *got = set.reso_on;
    else if (!strcmp(what, "voices"))
        *got = erk_active();
    else if (!strcmp(what, "sustain"))
        *got = au.sus_now;
    else if (!strcmp(what, "sost"))
        *got = au.sost_now;
    else if (!strcmp(what, "soft"))
        *got = au.soft_now;
    else if (!strcmp(what, "vel"))
        *got = (int)au.last_vel;
    else if (!strcmp(what, "notes"))
        *got = (int)au.notes;
    else if (!strcmp(what, "metro"))
        *got = au.metro_on;
    else if (!strcmp(what, "bpm"))
        *got = set.bpm;
    else if (!strcmp(what, "dirty"))
        *got = set_dirty;
    else
        return 0;
    return 1;
}
#endif
