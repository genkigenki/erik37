/* SPDX-License-Identifier: MPL-2.0
 * Erik37: Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* Erik37: a grand piano for the M-Vave SMK-37 (and the FM-1). The keys (velocity through the touch curve), the
 * pedals (the PEDAL jack, SEQ REC, SCALE, CHORD, the mod wheel, MIDI), the pitch wheel, the voices and the user
 * presets (the pads), the settings and their pages; the engine (dsp/erk.c) renders in the audio interrupt. Part of
 * fm1-base's unity build (device/unity.c, host/plat_host.c): gfx.c and announce.c are in scope; draw.c, ui.c and
 * boot.c are this app's. */
#include "dsp/erk.h"
#include "erk_bank_data.h"
#include "draw.c"

/* ---------------------------------------------------------------- settings */
#define SET_MAGIC 0x324B5245u            /* "ERK2" (1.2: the release noise, the pad light, the wheel and the jack) */
#define SET_MAGIC1 0x314B5245u           /* "ERK1": 1.0 / 1.1, the same size: kept, the new ones at their defaults */
typedef struct {
    uint32_t magic;
    uint8_t tone, room, curve, una;      /* tone 0..3, room 0..3 (dry, chamber, hall, church), touch 0..3, una 0..100 */
    uint8_t size, amount, width, brill;  /* 0..100 */
    uint8_t reso, reso_on, bpm, pad;     /* resonance 0..100; the metronome's tempo (bpm); the voice or preset chosen
                                          * last (1..16 as the pads, 0 none) */
    int8_t octave;                       /* -2 .. +2 */
    uint8_t relnoise;                    /* the release (damper) noise, 0 (off) .. 100 (1.1's level) */
    uint8_t padlight;                    /* the pads' brightness, 0..3 (PADL_PCT) */
    uint8_t flags;                       /* SF_WHEEL: the mod wheel holds notes; the PEDAL jack (SF_JACK_SH): 0 auto,
                                          * 1 open at rest, 2 closed at rest */
} settings_t;
#define SF_WHEEL 1u
#define SF_JACK_SH 1
#define JACK_MODE() ((set.flags >> SF_JACK_SH) & 3u)
#define RELNOISE_DEF 40                  /* 1.1 had 100 (-8 dB now) */
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
    set.pad = 1;
    set.octave = 0;
    set.relnoise = RELNOISE_DEF;
    set.padlight = 3;
    set.flags = 0;                       /* only the pedal holds notes (the mod wheel not); the jack learnt */
}

static int settings_ok(void)
{
    return set.magic == SET_MAGIC && set.tone < 4 && set.room < 4 && set.curve < 4 && set.una <= 100 &&
           set.size <= 100 && set.amount <= 100 && set.width <= 100 && set.brill <= 100 && set.reso <= 100 &&
           set.reso_on <= 1 && set.bpm >= 30 && set.bpm <= 240 && set.octave >= -2 && set.octave <= 2 && set.pad <= 16 &&
           set.relnoise <= 100 && set.padlight <= 3 && set.flags <= (SF_WHEEL | 2u << SF_JACK_SH);
}

static void settings_load(void)
{
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set)
        set.magic = 0;
    if (set.magic == SET_MAGIC1) {           /* 1.1's: its sound and choices, the new settings at their defaults */
        set.magic = SET_MAGIC;
        set.relnoise = RELNOISE_DEF;
        set.padlight = 3;
        set.flags = 0;
    }
    if (!settings_ok())
        settings_defaults();
}

/* ---------------------------------------------------------------- the voices and the user presets */
/* the four voices (pads 1-4): a tone in the room that suits it */
static const struct {
    uint8_t tone, room, size, amount, brill;
} VOICE[4] = {
    {ERK_CONCERT, 2, 50, 38, 50},        /* the concert grand in the hall */
    {ERK_STUDIO, 1, 35, 22, 58},         /* close and bright in the chamber */
    {ERK_VINTAGE, 1, 55, 30, 42},        /* the old grand, darker, in a warmer room */
    {ERK_JAZZ, 1, 25, 18, 46},           /* round, in a small club */
};

/* the user presets (pads 5-16, USER 1-12): the whole sound, kept in their own store object */
#define NSLOT 12
#define SLOT_PAD0 4                      /* pad 5 (index 4) is USER 1 */
#define SLOT_MAGIC 0x31505245u           /* "ERP1" */
typedef struct {
    uint8_t used, tone, room, curve, una, size, amount, width, brill, reso, reso_on, rel1;   /* rel1: the release
                                          * noise + 1 (0: saved by 1.1, which had none: the current one stays) */
} slot_t;
static struct {
    uint32_t magic;
    slot_t s[NSLOT];
} slots;

static int slots_ok(void)
{
    int i;
    if (slots.magic != SLOT_MAGIC)
        return 0;
    for (i = 0; i < NSLOT; i++) {
        const slot_t *p = &slots.s[i];
        if (p->used > 1 || (p->used && (p->tone > 3 || p->room > 3 || p->curve > 3 || p->una > 100 || p->size > 100 ||
                                        p->amount > 100 || p->width > 100 || p->brill > 100 || p->reso > 100 ||
                                        p->reso_on > 1 || p->rel1 > 101)))
            return 0;
    }
    return 1;
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
#define REL_FULL 9000                    /* the release noise at 100: 1.1's level (Q15) */

static void apply_settings(void)
{
    touch_build();
    erk_set_tone(set.tone, q15_of(set.brill), q15_of(set.una));
    erk_set_room(set.room, q15_of(set.size), set.amount * 20000 / 100);
    erk_set_width(q15_of(set.width));
    erk_set_resonance(set.reso_on ? set.reso * 2600 / 100 : 0);
    erk_set_rel_level(set.relnoise * REL_FULL / 100);
}

static void changed(void)
{
    set_dirty = 1;
    set_changed_at = plat_ms();
    apply_settings();
}

/* ---------------------------------------------------------------- the main loop -> the audio interrupt */
enum { EV_SUS_PAD, EV_SOST, EV_SOFT, EV_ALL_OFF, EV_METRO, EV_NOTE_ON, EV_NOTE_OFF, EV_JACK_LEARN };
#define EVQ 64u
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
#if BOARD_SMK37
    uint8_t key_note[SMK_NKEYBED];       /* the MIDI note each keybed key started (the octave may change while held) */
#endif
    uint64_t held;                       /* keybed keys down, as the render saw them */
    uint32_t kb_note[4];                 /* the MIDI notes the keybed holds (an echo from the computer is ignored) */
    int sus_pad, sus_jack, sus_midi, sus_wheel, sus_now;   /* 0..127 each; the pedal is the strongest */
    int sost_pad, sost_midi, soft_pad, soft_midi, sost_now, soft_now;
    int jack_rest, jack_init, jack_cand, jack_n;   /* the PEDAL jack: its rest reading (auto: learnt once steady) */
    int metro_on;
    int bend, bend_sent;                 /* the pitch wheel, -127..127; what went out over MIDI */
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

/* the pitch wheel (and MIDI pitch bend): +-2 semitones. 2^(x / 6) in Q16 by its series (x -1..1, within half a
 * cent). v of full: the bend */
#define BEND_SEMIS 2
static void bend_set(int v, int full, int send)
{
    int32_t l, r;
    v = v < -full ? -full : v > full ? full : v;
    l = (int32_t)v * (int32_t)(65536.0 * 0.6931472 * BEND_SEMIS / 12.0) / full;   /* x ln 2, Q16 */
    r = 65536 + l + ((l * l) >> 17) + (((l * l) >> 16) * l) / 393216;
    erk_bend(v ? (int)r : 65536);
    au.bend = v * 127 / full;
    /* out over USB-MIDI as pitch bend, 14 bits: on a change of 2 steps or more (and at rest and the ends), so that
     * a wheel held off centre (its reading jitters) does not stream bends to the computer */
    if (send && au.bend != au.bend_sent &&
        (au.bend - au.bend_sent >= 2 || au.bend_sent - au.bend >= 2 || au.bend == 0 || au.bend == 127 || au.bend == -127)) {
        uint32_t b = (uint32_t)(8192 + au.bend * 8191 / 127);
        au.bend_sent = au.bend;
        midi_out(0xE0u, b & 0x7Fu, b >> 7);
    }
}

#if BOARD_SMK37
/* the mod wheel: the sustain pedal, half pedal in between: at rest (and a little above) exactly off, full near the
 * top. The pitch wheel's dead zone keeps a wheel at rest exactly in tune */
#define MOD_DEAD 8
#define MOD_FULL 119
#define PITCH_DEAD 3
static int wheel_sustain(int32_t mw)
{
    if (mw <= MOD_DEAD)
        return 0;
    if (mw >= MOD_FULL)
        return 127;
    return (int)((mw - MOD_DEAD) * 127 / (MOD_FULL - MOD_DEAD));
}
#endif

#if BOARD_SMK37
/* the PEDAL jack, pressed = away from its rest reading; a continuous pedal gives half pedal. Auto: the rest is learnt
 * once the reading has been steady for JACK_STEADY polls (~0.2 s) near either end (1.1 took the very first reading:
 * taken before the scan had read the jack, it held the sustain down for good); a reading in the middle (no jack, a
 * floating input) is never taken. Or fixed: open at rest (most pedals) or closed at rest */
#define JACK_STEADY 128
static void jack_poll(void)
{
    int raw = plat_pedal_raw(), d, mode = (int)JACK_MODE();
    if (raw < 0) {
        au.sus_jack = 0;
        return;
    }
    if (mode == 1) {
        d = 1023 - raw;
    } else if (mode == 2) {
        d = raw;
    } else if (!au.jack_init) {
        d = raw - au.jack_cand;
        if (d < -24 || d > 24) {
            au.jack_cand = raw;
            au.jack_n = 0;
        } else if (++au.jack_n >= JACK_STEADY && (au.jack_cand < 200 || au.jack_cand > 823)) {
            au.jack_rest = au.jack_cand;
            au.jack_init = 1;
        }
        au.sus_jack = 0;
        return;
    } else {
        d = raw - au.jack_rest;
        d = d < 0 ? -d : d;
        if (d < 40)
            au.jack_rest += (raw - au.jack_rest) / 32;   /* the rest drifts slowly */
    }
    d = (d - 80) * 127 / 500;
    au.sus_jack = d < 0 ? 0 : d > 127 ? 127 : d;
}
#endif

static void keybed_poll(void)
{
#if BOARD_SMK37
    uint64_t hits = plat_keybed_hits(), now = plat_keybed(), off = au.held & ~now;
    uint32_t k;
    for (k = 0; (hits | off) && k < SMK_NKEYBED; k++) {   /* (nothing new: no loop) */
        uint64_t b = (uint64_t)1u << k;
        if ((off & b) || ((hits & b) && (au.held & b))) {   /* released (or struck again before we saw it go) */
            au.kb_note[au.key_note[k] >> 5] &= ~(1u << (au.key_note[k] & 31));
            erk_note_off(au.key_note[k]);
            midi_out(0x80u, au.key_note[k], 0u);
        }
        if (hits & b) {
            int note = 48 + (int)k + 12 * set.octave, v = touch(plat_keybed_vel((int)k));
            if (note < 21 || note > 108)
                continue;
            au.key_note[k] = (uint8_t)note;
            au.kb_note[note >> 5] |= 1u << (note & 31);
            erk_note_on(note, v);
            midi_out(0x90u, (uint32_t)note, (uint32_t)v);
            au.last_vel = (uint32_t)v;
            au.notes++;
            if (!(now & b)) {                 /* a tap already over */
                au.kb_note[note >> 5] &= ~(1u << (note & 31));
                erk_note_off(note);
                midi_out(0x80u, (uint32_t)note, 0u);
            }
        }
    }
    au.held = now;
    jack_poll();
    {
        int32_t pw, mw;
        if (plat_wheels(&pw, &mw)) {
            au.sus_wheel = (set.flags & SF_WHEEL) ? wheel_sustain(mw) : 0;   /* (off by default: a wheel left up
                                                                               * held every note) */
            bend_set(pw < -PITCH_DEAD ? (int)pw + PITCH_DEAD : pw > PITCH_DEAD ? (int)pw - PITCH_DEAD : 0,
                     127 - PITCH_DEAD, 1);
        }
    }
#endif
}

static void midi_poll(void)
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        if (((st & 0xF0u) == 0x90u || (st & 0xF0u) == 0x80u) && (au.kb_note[d1 >> 5] >> (d1 & 31) & 1u))
            continue;                         /* a note the keys hold: the computer echoing it back (MIDI thru) would
                                               * strike the string twice */
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
        } else if ((st & 0xF0u) == 0xE0u) {
            bend_set((int)(d1 | d2 << 7) - 8192, 8191, 0);
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
        case EV_NOTE_ON: erk_note_on((int)(v & 0xFFu), (int)(v >> 8 & 0xFFu)); break;
        case EV_NOTE_OFF: erk_note_off((int)v); break;
        case EV_JACK_LEARN:
            au.jack_init = 0;
            au.jack_n = 0;
            break;
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
    settings_load();
    if (plat_store_load(OBJ_PRESETS, &slots, sizeof slots) != (int)sizeof slots || !slots_ok()) {
        memset(&slots, 0, sizeof slots);
        slots.magic = SLOT_MAGIC;
    }
    erk_bank_open(&bank, ERK_BANK);
    erk_init(&bank, &ENGINE);
    apply_settings();
}

/* ---------------------------------------------------------------- the UI */
enum { PG_PLAY, PG_TONE, PG_ROOM, PG_SETUP, PG_ABOUT, NPAGES };
enum { ST_BOOT, ST_RUN };
enum { PD_SOFT, PD_SOST, PD_SUS, NPD };      /* the pedal buttons: SCALE, CHORD, SEQ REC (FM-1: SEL, ENV, REC) */
static const uint8_t PD_BTN[NPD] = {B_SEL, B_ENV, B_REC};
#define LONG_MS 1000u                        /* a long press: save */
#define PAD_DEBOUNCE_MS 180u
static struct {
    int state, page, drawn_page;
    uint32_t boot_t0;
    int boot_frame;
    int active;                          /* the knob being turned (-1 none) */
    uint32_t active_at;
    uint32_t btn, sbtn;                  /* held at the last input */
    uint8_t pd_latch[NPD], pd_hold[NPD]; /* the pedal buttons: a tap latches, held is down while held */
    uint32_t pd_at[NPD];
    uint32_t pads_prev, pads_down, pads_done;   /* pads: held at the last input, presses taken, long presses done */
    uint32_t pad_at[16];
    uint32_t pad_rgb[16];                /* the pads' colours as lit (the SETUP page shows them) */
    uint32_t lit;                        /* the button lights as lit */
    uint32_t setup_q;                    /* what the SETUP page shows live (redrawn when it changes) */
    int metro;
    uint32_t home_at, save_at, quiet_at;
    int save_done;
    int slot_sel;                        /* the FM-1: the user preset PRESETS points at */
    uint32_t fm_keys;                    /* the FM-1: the keys held */
    uint8_t fm_note[NKEYS];
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

/* ---------------------------------------------------------------- the voices and the presets, chosen */
static void sound_changed(void)              /* tone, room ... all at once: every page and the header again */
{
    changed();
    ui.head_q = ui.strip_q = -1;
    ui.full |= ui.page != PG_PLAY;
}

static void voice_select(int v)
{
    set.tone = VOICE[v].tone;
    set.room = VOICE[v].room;
    set.size = VOICE[v].size;
    set.amount = VOICE[v].amount;
    set.brill = VOICE[v].brill;
    set.pad = (uint8_t)(v + 1);
    sound_changed();
    app_say(TONE_NAME[set.tone], ROOM_NAME[set.room]);
}

static void slot_name(char *t, int i)
{
    t[0] = 0;
    cat_s(t, "USER ");
    itoa_s(i + 1, t + 5);
}

static void slot_recall(int i)
{
    const slot_t *p = &slots.s[i];
    char t[12];
    slot_name(t, i);
    if (!p->used) {                          /* (a pad held a second, the FM-1's SAVE held: saved there) */
#if BOARD_SMK37
        app_say(t, "HOLD TO SAVE");
#else
        app_say(t, "EMPTY");
#endif
        return;
    }
    set.tone = p->tone;
    set.room = p->room;
    set.curve = p->curve;
    set.una = p->una;
    set.size = p->size;
    set.amount = p->amount;
    set.width = p->width;
    set.brill = p->brill;
    set.reso = p->reso;
    set.reso_on = p->reso_on;
    if (p->rel1)
        set.relnoise = (uint8_t)(p->rel1 - 1);
    set.pad = (uint8_t)(SLOT_PAD0 + i + 1);
    sound_changed();
    app_say(t, TONE_NAME[set.tone]);
}

static void slot_save(int i)
{
    slot_t *p = &slots.s[i];
    char t[12];
    p->used = 1;
    p->tone = set.tone;
    p->room = set.room;
    p->curve = set.curve;
    p->una = set.una;
    p->size = set.size;
    p->amount = set.amount;
    p->width = set.width;
    p->brill = set.brill;
    p->reso = set.reso;
    p->reso_on = set.reso_on;
    p->rel1 = (uint8_t)(set.relnoise + 1);
    slot_name(t, i);
    if (plat_store_save(OBJ_PRESETS, &slots, sizeof slots) == 0) {
        set.pad = (uint8_t)(SLOT_PAD0 + i + 1);
        set_dirty = 1;
        set_changed_at = plat_ms();
        app_say("SAVED", t);
    } else {
        app_say("SAVE FAILED", t);
    }
}

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
        k[3].label = "tempo";
        itoa_s(set.bpm, k[3].value);
        cat_s(k[3].value, " bpm");
        k[3].amt = (set.bpm - 30) / 210.0f;
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
        k[2].amt = set.reso_on ? set.reso / 100.0f : 0;
        k[3].label = "rel. noise";
        if (set.relnoise)
            pct_text(k[3].value, set.relnoise);
        else
            cat_s(k[3].value, "off");
        k[3].amt = set.relnoise / 100.0f;
    } else if (page == PG_SETUP) {                /* the pedals and the pads */
        k[0].label = "una corda";
        pct_text(k[0].value, set.una);
        k[0].amt = set.una / 100.0f;
#if BOARD_SMK37
        k[1].label = "mod wheel";
        cat_s(k[1].value, (set.flags & SF_WHEEL) ? "sustain" : "off");
        k[1].n = 2;
        k[1].cur = (int)(set.flags & SF_WHEEL);
        k[2].label = "pedal jack";
        cat_s(k[2].value, JACK_NAME[JACK_MODE()]);
        k[2].n = 3;
        k[2].cur = (int)JACK_MODE();
        k[3].label = "pad light";
        itoa_s(PADL_PCT[set.padlight], k[3].value);
        cat_s(k[3].value, " %");
        k[3].n = 4;
        k[3].cur = set.padlight;
#endif
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

/* a page knob by detents. Resonance turned down past 0 is off, turned up again on */
static void knob_turn(int page, int i, int d)
{
    uint8_t *v = 0;
    int lo = 0, hi = 100, step = 2, choice = 0;
    if (page == PG_ABOUT)
        return;
    if (page == PG_PLAY) {
        static uint8_t *const P[4] = {&set.tone, &set.room, &set.amount, &set.bpm};
        v = P[i];
        choice = i < 2;
        if (i == 3) {
            lo = 30;
            hi = 240;
        }
    } else if (page == PG_SETUP) {
        if (i == 0) {
            v = &set.una;
        } else {
#if BOARD_SMK37
            int f = set.flags, old = (int)JACK_MODE();
            if (i == 1)
                f = d > 0 ? f | (int)SF_WHEEL : d < 0 ? f & ~(int)SF_WHEEL : f;
            else if (i == 2)
                f = (f & ~(3 << SF_JACK_SH)) | clamp(old + d, 0, 2) << SF_JACK_SH;
            else
                set.padlight = (uint8_t)clamp(set.padlight + d, 0, 3);
            if (f != set.flags) {
                set.flags = (uint8_t)f;
                if (i == 2 && JACK_MODE() == 0)
                    ev_put(EV_JACK_LEARN, 0);     /* back to auto: the rest learnt again (the pedal up now) */
            }
            changed();
            ui.active = i;
            ui.active_at = plat_ms();
            ui.strip_q = -1;
            ui.full = 1;
#endif
            return;
        }
    } else if (page == PG_TONE) {
        static uint8_t *const P[4] = {&set.curve, &set.brill, &set.reso, &set.relnoise};
        v = P[i];
        choice = i == 0;
        if (i == 3)
            step = 5;
        if (i == 2) {
            if (d < 0 && set.reso_on && set.reso == 0) {
                set.reso_on = 0;
                changed();
            } else if (d > 0 && !set.reso_on) {
                set.reso_on = 1;
                changed();
                d = 0;
            }
        }
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
        int nv = *v + d * step;
        nv = nv < lo ? lo : nv > hi ? hi : nv;
        if (nv != *v) {
            *v = (uint8_t)nv;
            changed();
        }
    }
    ui.active = i;
    ui.active_at = plat_ms();
    ui.strip_q = -1;
    if (page != PG_PLAY)
        ui.full = 1;                          /* the settings pages draw their pictures from the values */
    if (page == PG_PLAY && choice)
        ui.head_q = -1;
}

/* ---------------------------------------------------------------- pads and lights */
#if BOARD_SMK37
/* a pad in colour c, its brightest channel at m (the hue kept) */
static void pad_lit(int p, const uint8_t *c, int m)
{
    int top = c[0] > c[1] ? c[0] : c[1];
    uint32_t r, g, b;
    top = top > c[2] ? top : c[2];
    r = (uint32_t)((c[0] * m + top / 2) / top);
    g = (uint32_t)((c[1] * m + top / 2) / top);
    b = (uint32_t)((c[2] * m + top / 2) / top);
    plat_pad_rgb(p, (uint8_t)r, (uint8_t)g, (uint8_t)b);
    ui.pad_rgb[p] = r << 16 | g << 8 | b;
}
#endif

static void lights(uint32_t pads_held)
{
#if BOARD_SMK37
    /* the voices amber (dim) and bright gold (the one chosen); the presets: empty a pale warm grey, filled a brighter
     * one, the one chosen warm ivory. A pad held is bright. (Little blue: the LEDs turn warm tones cold,
     * design/DESIGN.md.) The pad light sets each level's brightest channel: the chosen pad at 45..100 %, the dim
     * levels less and with a floor, so that every level stays visible and apart from the next at every step (at
     * 100 %: 1.1's colours; an empty preset was dark there) */
    static const uint8_t VDIM[3] = {120, 72, 10}, VON[3] = {255, 140, 16}, SDIM[3] = {72, 54, 34},
                         SON[3] = {255, 190, 110};
    static const uint8_t PADL_ON[4] = {115, 153, 204, 255}, PADL_DIM[4] = {56, 72, 96, 120},
                         PADL_FILLED[4] = {52, 62, 74, 88}, PADL_EMPTY[4] = {26, 29, 32, 36};
    int p, l = set.padlight;
    for (p = 0; p < 16; p++) {
        int on = set.pad == p + 1 || (pads_held >> p & 1u);
        if (p < SLOT_PAD0)
            pad_lit(p, on ? VON : VDIM, on ? PADL_ON[l] : PADL_DIM[l]);
        else if (on)
            pad_lit(p, SON, PADL_ON[l]);
        else
            pad_lit(p, SDIM, slots.s[p - SLOT_PAD0].used ? PADL_FILLED[l] : PADL_EMPTY[l]);
    }
#else
    (void)pads_held;
#endif
    {
        uint32_t b = 0;
        int i;
        for (i = 0; i < NPD; i++)
            if (ui.pd_latch[i] || ui.pd_hold[i])
                b |= 1u << PD_BTN[i];
        if (ui.metro)
            b |= LED_PLAY_GREEN;
        if (set_dirty)
            b |= 1u << B_SAVE;
        if (set.octave < 0)
            b |= 1u << B_OCTDN;
        if (set.octave > 0)
            b |= 1u << B_OCTUP;
        ui.lit = b;
        plat_lights(b, 0, 0, 0, 0);
    }
}

static void pedals_send(void)
{
    ev_put(EV_SUS_PAD, (ui.pd_latch[PD_SUS] || ui.pd_hold[PD_SUS]) ? 127u : 0u);
    ev_put(EV_SOST, (uint32_t)(ui.pd_latch[PD_SOST] || ui.pd_hold[PD_SOST]));
    ev_put(EV_SOFT, (ui.pd_latch[PD_SOFT] || ui.pd_hold[PD_SOFT]) ? 127u : 0u);
}

/* the pedal buttons. SEQ REC (the FM-1's REC), the sustain: a toggle, each press on or off, its light on while on
 * (1.1's tap-or-hold was easily left on: every note then held). SCALE and CHORD: a tap latches the pedal (the next tap
 * lets go), held it is down while held */
static void pedal_buttons(uint32_t down, uint32_t up, uint32_t now)
{
    int i, chg = 0;
    for (i = 0; i < NPD; i++) {
        uint32_t b = 1u << PD_BTN[i];
        if (i == PD_SUS) {
            if ((down & b) && now - ui.pd_at[i] >= PAD_DEBOUNCE_MS) {
                ui.pd_latch[i] = (uint8_t)!ui.pd_latch[i];
                ui.pd_at[i] = now;
                app_say("SUSTAIN", ui.pd_latch[i] ? "ON" : "OFF");
                chg = 1;
            }
            continue;
        }
        if (down & b) {
            if (ui.pd_latch[i]) {
                ui.pd_latch[i] = 0;
            } else {
                ui.pd_hold[i] = 1;
                ui.pd_at[i] = now;
            }
            chg = 1;
        }
        if (up & b) {
            if (ui.pd_hold[i] && now - ui.pd_at[i] < 300u)
                ui.pd_latch[i] = 1;
            ui.pd_hold[i] = 0;
            chg = 1;
        }
    }
    if (chg)
        pedals_send();
}

/* the pads: 1-4 the voices (a press), 5-16 the user presets (a short press recalls, held a second saves). A press
 * counts once, and not again within PAD_DEBOUNCE_MS */
static void pads_input(void)
{
#if BOARD_SMK37
    uint32_t hits = plat_pad_hits(), held = plat_pads_held(), now = plat_ms();
    int p;
    for (p = 0; p < 16; p++) {
        uint32_t b = 1u << p;
        if ((hits & b) && !(ui.pads_down & b) && now - ui.pad_at[p] >= PAD_DEBOUNCE_MS) {
            ui.pads_down |= b;
            ui.pads_done &= ~b;
            ui.pad_at[p] = now;
            if (p < SLOT_PAD0) {
                voice_select(p);
                ui.pads_done |= b;
            }
        }
        if (!(ui.pads_down & b))
            continue;
        if ((held & b) && !(ui.pads_done & b) && now - ui.pad_at[p] >= LONG_MS) {
            slot_save(p - SLOT_PAD0);         /* held a second: the sound into the preset */
            ui.pads_done |= b;
        }
        if (!(held & b)) {                    /* let go (or a tap over before we looked) */
            if (!(ui.pads_done & b))
                slot_recall(p - SLOT_PAD0);
            ui.pads_down &= ~b;
        }
    }
    ui.pads_prev = held;
    lights(held);
#else
    lights(0);
#endif
}

#if !BOARD_SMK37
/* the FM-1's keys: F3..G5, velocity from lib/touch, through the touch curve; to the render as events */
static void keys_input(void)
{
    uint32_t keys, k;
    touch_poll();
    keys = touch_keys();
    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k;
        if ((keys & bit) && !(ui.fm_keys & bit)) {
            int note = 53 + (int)k + 12 * set.octave, v = touch(touch_vel(k));
            ui.fm_note[k] = (uint8_t)note;
            ev_put(EV_NOTE_ON, (uint32_t)note | (uint32_t)v << 8);
            midi_out(0x90u, (uint32_t)note, (uint32_t)v);
            au.last_vel = (uint32_t)v;
            au.notes++;
        } else if (!(keys & bit) && (ui.fm_keys & bit)) {
            ev_put(EV_NOTE_OFF, ui.fm_note[k]);
            midi_out(0x80u, ui.fm_note[k], 0u);
        }
    }
    ui.fm_keys = keys;
}
#endif

static void page_set(int pg)
{
    pg = (pg % NPAGES + NPAGES) % NPAGES;
    if (pg != ui.page) {
        ui.page = pg;
        ui.full = 1;
    }
}

static void settings_save(void)
{
    if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0) {
        set_dirty = 0;
        app_say("SAVED", "");
    } else {
        app_say("SAVE FAILED", "");
    }
}

static void input(void)
{
    uint32_t btn = plat_buttons(), down = btn & ~ui.btn, up = ui.btn & ~btn, now = plat_ms();
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
#if !BOARD_SMK37
    keys_input();
#endif
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
    /* BT (the FM-1's SAVE): the settings saved now. The FM-1: held a second, the sound into the user preset that
     * PRESETS points at */
    if (down & (1u << B_SAVE)) {
        ui.save_at = now;
        ui.save_done = 0;
#if BOARD_SMK37
        settings_save();
        ui.save_done = 1;
#endif
    }
#if !BOARD_SMK37
    if ((btn & (1u << B_SAVE)) && !ui.save_done && now - ui.save_at >= LONG_MS) {
        slot_save(ui.slot_sel);
        ui.save_done = 1;
    }
    if ((up & (1u << B_SAVE)) && !ui.save_done)
        settings_save();
#endif
    pedal_buttons(down, up, now);             /* SCALE una corda, CHORD sostenuto, SEQ REC sustain */
    if (down & (1u << B_PLAY)) {              /* SEQ PLAY: the metronome (its tempo: the play screen's K4) */
        ui.metro = !ui.metro;
        ev_put(EV_METRO, (uint32_t)ui.metro);
    }
    if (down & (1u << B_FX))                  /* FX: every note let go */
        ev_put(EV_ALL_OFF, 0);
    ui.btn = btn;
#if BOARD_SMK37
    {                                         /* < >: the pages */
        uint32_t sb = plat_smk_buttons(), sd = sb & ~ui.sbtn;
        if (sd & (1u << SB_LEFT))
            page_set(ui.page - 1);
        if (sd & (1u << SB_RIGHT))
            page_set(ui.page + 1);
        ui.sbtn = sb;
    }
#endif
    for (i = 0; i < 4; i++)                   /* K1..K4 (the FM-1's KNOB 1..4): the page's four knobs */
        if ((e = plat_enc(EN_K1 + i)) != 0)
            knob_turn(ui.page, i, e);
#if BOARD_SMK37
    /* K6..K8 (SELECT, ALGORITHM, PRESETS) have no job here: read and dropped */
    plat_enc(EN_SELECT);
    plat_enc(EN_ALGO);
    plat_enc(EN_PRESET);
#else
    if ((e = plat_enc(EN_SELECT)) != 0)       /* the FM-1: SELECT the page */
        page_set(ui.page + e);
    if ((e = plat_enc(EN_ALGO)) != 0) {       /* ALGORITHM: the voices */
        int v = set.pad >= 1 && set.pad <= 4 ? set.pad - 1 + e : e > 0 ? 0 : 3;
        voice_select(clamp(v, 0, 3));
    }
    if ((e = plat_enc(EN_PRESET)) != 0) {     /* PRESETS: the user presets, recalled as it lands on them */
        ui.slot_sel = clamp(ui.slot_sel + e, 0, NSLOT - 1);
        slot_recall(ui.slot_sel);
    }
#endif
    pads_input();
    if (ui.active >= 0 && now - ui.active_at > 1200u) {
        ui.active = -1;
        ui.strip_q = -1;
    }
    /* saved by itself once the piano is quiet, the room's tail too (a flash write stops the sound and USB for a
     * moment): no note for 1.5 s */
    if (erk_active())
        ui.quiet_at = now;
    if (set_dirty && now - set_changed_at > 2000u && now - ui.quiet_at > 1500u) {
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
        header_text(TONE_NAME[set.tone], ui.pd_latch[PD_SUS] ? "SUSTAIN ON" : ROOM_NAME[set.room]);
    } else {
        static const char *const TITLE[NPAGES] = {"", "TONE & TOUCH", "ROOM", SETUP_TITLE, "ABOUT"};
        header_text(TITLE[ui.page], 0);
        page_dots(NPAGES - 1, ui.page - 1);
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
    uint64_t kb;
    int i, chg = force;
#if BOARD_SMK37
    kb = plat_keybed();
#else
    kb = (uint64_t)ui.fm_keys << 5;           /* F3 is the screen's sixth key */
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
        restore(RULE_Y, 151);                 /* (from the rule down: the treble strings' glow reaches row 26) */
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
        else if (ui.page == PG_SETUP)
            setup_page();
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
    } else if (ui.page == PG_SETUP) {
        uint32_t q = setup_live();
        if (q != ui.setup_q) {                /* the pedals, the jack, the pads changed: next frame */
            ui.setup_q = q;
            ui.full = 1;
        }
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
    ui_setup();
#if BOARD_SMK37
    plat_smk_pads(SMK_PADS_OWN);
    plat_pad_lights(1);
#else
    touch_on(1);                              /* the FM-1's keys: velocity */
#endif
    ui.active = -1;
    ui.drawn_page = -1;
    ui.full = 1;
    if (set.pad > SLOT_PAD0)
        ui.slot_sel = set.pad - SLOT_PAD0 - 1;
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
    else if (!strcmp(what, "size"))
        *got = set.size;
    else if (!strcmp(what, "amount"))
        *got = set.amount;
    else if (!strcmp(what, "width"))
        *got = set.width;
    else if (!strcmp(what, "una"))
        *got = set.una;
    else if (!strcmp(what, "octave"))
        *got = set.octave;
    else if (!strcmp(what, "curve"))
        *got = set.curve;
    else if (!strcmp(what, "brill"))
        *got = set.brill;
    else if (!strcmp(what, "reso_on"))
        *got = set.reso_on;
    else if (!strcmp(what, "chosen"))
        *got = set.pad;
    else if (!strncmp(what, "user", 4))           /* userN: preset N (1..12) is filled */
        *got = slots.s[clamp(atoi(what + 4) - 1, 0, NSLOT - 1)].used;
    else if (!strcmp(what, "voices"))
        *got = erk_active();
    else if (!strcmp(what, "sustain"))
        *got = au.sus_now;
    else if (!strcmp(what, "sost"))
        *got = au.sost_now;
    else if (!strcmp(what, "soft"))
        *got = au.soft_now;
    else if (!strcmp(what, "bend"))
        *got = au.bend;
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
    else if (!strcmp(what, "relnoise"))
        *got = set.relnoise;
    else if (!strcmp(what, "padlight"))
        *got = set.padlight;
    else if (!strcmp(what, "wheel_sus"))
        *got = (int)(set.flags & SF_WHEEL);
    else if (!strcmp(what, "jack_mode"))
        *got = (int)JACK_MODE();
    else if (!strcmp(what, "jack_learnt"))
        *got = au.jack_init;
    else if (!strcmp(what, "lit_rec"))            /* SEQ REC's light */
        *got = (int)(ui.lit >> B_REC & 1u);
    else
        return 0;
    return 1;
}
#endif
