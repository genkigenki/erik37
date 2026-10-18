/* SPDX-License-Identifier: MPL-2.0
 * Erik37: Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* The piano engine. A voice: one or two sample streams (velocity layers, crossfaded during the attack, then the
 * dominant one), each block ADPCM decoded and 4-point (cubic Hermite) interpolated; a one-pole brightness filter
 * (velocity, tone, una corda, darkening with time); a block-rate envelope (the key's two-slope decay, the
 * two-stage damper release, half pedal); constant-power pan by key and width. The bus: a tone EQ (two one-pole
 * shelves), sympathetic resonance (12 string-tuned combs, pedal down) and an 8-line FDN reverb (chamber, hall,
 * church), both at half rate (22.05 kHz), the metronome's tick. Release noise: one-shot voices from the bank.
 * The per-sample loops are 32-bit only; 64-bit products live in noinline helpers (felucca-bsp docs/CPU.md: a
 * 32x32->64 multiply beside a load can leave a register off on the AC79). */
#include <string.h>
#include "erk.h"
#include "erk_codec.h"

#define REL1 83                 /* damper first stage, blocks (~60 ms; tools/build_bank.py REL1_BLOCKS) */
#define DARK_FROM 345           /* filter darkening starts after ~0.25 s (the attack is in the data) */
#define ENV_MIN (1u << 16)      /* -84 dB: a held note ends */
#define ENV_MIN_LET (1u << 19)  /* -66 dB: a let-go one (the undamped treble's aftersound frees its slot) */
#define FADE_M 834000000u       /* -2.2 dB a block: ~25 ms to silence (re-struck / stolen) */
#define MIXSH 12                /* voice out: int16 * Q15 >> 12 -> mix scale 2^18 */
#define XF_AT 300               /* two layers: from ~0.22 s the voice hands over to the dominant one ... */
#define XF_LEN 128              /* ... over ~90 ms (two looped layers would interfere, and it halves the cost) */
#define HY_A 250                /* hybrid: the sampled attack hands over to the partials from ~0.18 s ... */
#define HY_MID 345              /* ... (the partials were measured at 0.25 s: their decay runs from here) ... */
#define HY_B 415                /* ... to ~0.30 s (equal power) */
#define SUS_ON 11500            /* the pedal holds above this (Q15, ~0.35) ... */
#define SUS_OFF 8200            /* ... and lets go below (~0.25) */

enum { V_FREE, V_ON, V_SUST, V_REL, V_RING, V_FADE, V_NOISE };
#define F_NODAMP ERK_NODAMP
#define F_SOST 2

typedef struct {
    erk_dec d;
    const uint8_t *base;
    uint32_t pos, lend, loop, loop_len;
    int16_t h1, h2;
    int32_t xm1, x0, x1, x2;
    uint32_t frac;
    int32_t w, wt;              /* Q15 weight (layer crossfade x the sample's gain): now, the block's target */
    int done;
} stream;

typedef struct {
    stream st[2];
    int nst;
    uint32_t inc;
    int32_t y, a, a_floor;      /* filter state; coefficient Q22 (Q14 << 8) */
    uint32_t dark;
    uint32_t env, m, m2, mr1, mr2;
    uint32_t blocks, rblocks, knee;
    int32_t amp;                /* Q15 */
    int32_t gl, gr;             /* Q30 */
    int32_t pl, pr;
    int32_t w0s, w1s, wend;     /* layer handover: start weights, the kept layer's final weight */
    uint32_t senv;              /* hybrid: the sampled attack's decay (the voice envelope is the damper only) */
    uint8_t key, state, flags, rel, keep;
#if ERK_MODAL
    erkm_voice mv;
#endif
} voice;

static const erk_bank *bk;
static erk_cfg cfg;
static voice vc[ERK_NV];
static int sus, soft, sost;
static int32_t sus_lvl, soft_lvl;   /* Q15 */
static int32_t send, send_t;        /* resonance send, Q15 */
static uint32_t m_half;             /* half pedal: extra decay a block of the notes it holds */
erk_stats erk_st;
volatile uint16_t erk_key_lvl[ERK_KEYS];
volatile uint32_t erk_sost_keys[3];
volatile int32_t erk_sus_q15, erk_soft_q15;

/* the settings (the main loop writes, the render reads; a reverb change is applied at a block's start) */
static struct {
    int32_t bscale;             /* brightness: the velocity filter's coefficient x this, Q12 */
    int32_t g_lo, g_hi;         /* shelf gains - 1, Q12 */
    int32_t res_scale;          /* Q15 */
    int32_t detune;             /* cents x 10, spread over the keys */
    int32_t una;                /* Q15 una corda depth */
    int32_t width;              /* Q15 */
    int32_t rev_type, rev_size, rev_mix;
    volatile int32_t rev_dirty;
    int32_t res_level, rel_level, master;
} st = {4096, 0, 0, 32767, 0, 16384, 26000, ERK_REV_HALL, 16384, 9000, 1, 1300, 9000, 16000};

static const uint8_t silent_blk[ERK_BLKB] = {0, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88,
                                             0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88};

static inline int32_t sat16(int32_t v) { return v < -32768 ? -32768 : v > 32767 ? 32767 : v; }
static inline int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
/* the 64-bit products, out of line */
static __attribute__((noinline)) uint32_t mulq30(uint32_t a, uint32_t b) { return (uint32_t)(((uint64_t)a * b) >> 30); }
static __attribute__((noinline)) int32_t muls(int32_t a, int32_t b, int sh) { return (int32_t)(((int64_t)a * b) >> sh); }

/* 2^x for the setters (control rate, the single-precision FPU): x in -30..30 */
static float exp2_f(float x)
{
    int i = (int)(x < 0 ? x - 1.0f : x);
    float f = x - (float)i, r = 1.0f;
    float p = 1.0f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * (0.0096181f + f * 0.0013334f))));
    if (i > 0)
        while (i--)
            r *= 2.0f;
    else
        while (i++)
            r *= 0.5f;
    return p * r;
}

/* sin(pi/2 x) for x 0..1 in Q15, a 17-point table interpolated */
static int32_t sinq(int32_t x_q15)
{
    static const int16_t T[17] = {0, 3212, 6393, 9512, 12540, 15447, 18205, 20788, 23170, 25330, 27246,
                                  28899, 30274, 31357, 32138, 32610, 32767};
    int32_t x = clampi(x_q15, 0, 32767) * 16, i = x >> 15;
    if (i >= 16)
        return T[16];
    return T[i] + (((T[i + 1] - T[i]) * (x & 32767)) >> 15);
}

/* ---------------------------------------------------------------- streams */

static inline __attribute__((always_inline)) int dec_one(stream *s)
{
    int v = erk_dec_next(&s->d);
    if (++s->pos == s->lend) {
        if (s->loop_len) {
            erk_dec_start(&s->d, s->base + (s->loop / ERK_BLK) * ERK_BLKB, s->h1, s->h2);
            s->pos = s->loop;
        } else {
            erk_dec_start(&s->d, silent_blk, 0, 0);    /* the rest of the block decodes to 0 */
            s->lend = 0xffffffffu;
            s->done = 1;
        }
    }
    return v;
}

static void stream_start(stream *s, const erk_smp *m, int32_t w)
{
    s->base = bk->data + m->off;
    s->loop = m->loop;
    s->loop_len = m->loop_len;
    s->lend = m->loop_len ? m->loop + m->loop_len : m->len;
    s->h1 = m->h1;
    s->h2 = m->h2;
    s->pos = 0;
    s->done = 0;
    erk_dec_start(&s->d, s->base, 0, 0);
    s->xm1 = 0;
    s->x0 = dec_one(s);
    s->x1 = dec_one(s);
    s->x2 = dec_one(s);
    s->frac = 0;
    s->w = s->wt = w;
}

/* a block's source samples: n decoded into out (the decoder's state in registers; erk_codec.h by hand) */
static __attribute__((noinline)) void stream_decode(stream *s, int32_t *out, int n)
{
    const uint8_t *cur = s->d.cur, *next = s->d.next;
    int32_t nib = s->d.nib, sh = s->d.sh, c1 = s->d.c1, c2 = s->d.c2, s1 = s->d.s1, s2 = s->d.s2;
    uint32_t pos = s->pos, lend = s->lend;
    int i;
    for (i = 0; i < n; i++) {
        int32_t q, v;
        if (nib == ERK_BLK) {                         /* the next block's header */
            int h = next[0];
            sh = h & 15;
            c1 = erk_filt[h >> 4][0];
            c2 = erk_filt[h >> 4][1];
            cur = next + 1;
            next += ERK_BLKB;
            nib = 0;
        }
        q = cur[nib >> 1];
        q = (nib & 1) ? q >> 4 : q & 15;
        nib++;
        v = ((c1 * s1 + c2 * s2 + 32) >> 6) + ((q ^ 8) - 8) * (1 << sh);
        v = v < -32768 ? -32768 : v > 32767 ? 32767 : v;
        s2 = s1;
        s1 = v;
        out[i] = v;
        if (++pos == lend) {                          /* the loop's end (or the sample's) */
            if (s->loop_len) {
                next = s->base + (s->loop / ERK_BLK) * ERK_BLKB;
                s1 = s->h1;
                s2 = s->h2;
                pos = s->loop;
            } else {
                next = silent_blk;                    /* the rest decodes to 0 */
                s1 = s2 = 0;
                lend = 0xffffffffu;
                s->done = 1;
            }
            nib = ERK_BLK;
        }
    }
    s->d.cur = cur;
    s->d.next = next;
    s->d.nib = nib;
    s->d.sh = sh;
    s->d.c1 = c1;
    s->d.c2 = c2;
    s->d.s1 = s1;
    s->d.s2 = s2;
    s->pos = pos;
    s->lend = lend;
}

/* one block of a stream into buf: set (add = 0) or accumulate, weighted. inc < 1: at most one source sample an
 * output sample, so a block needs at most 32: decoded first, then interpolated (4-point cubic Hermite) */
static __attribute__((noinline)) void stream_run(stream *s, uint32_t inc, int32_t *buf, int add)
{
    int32_t a[4 + ERK_BLOCK];
    uint32_t f0 = s->frac, f;
    int i, n = (int)((f0 + inc * ERK_BLOCK) >> 16);
    int32_t w = s->w << 8, dw = ((s->wt - s->w) << 8) / ERK_BLOCK;
    a[0] = s->xm1;
    a[1] = s->x0;
    a[2] = s->x1;
    a[3] = s->x2;
    stream_decode(s, a + 4, n);
    f = f0;
    for (i = 0; i < ERK_BLOCK; i++) {
        const int32_t *x;
        int32_t t, c1, c2, c3, y;
        f += inc;
        x = a + (f >> 16);
        t = (int32_t)((f & 0xFFFFu) >> 4);            /* Q12 */
        c1 = (x[2] - x[0]) >> 1;
        c2 = x[0] - ((5 * x[1]) >> 1) + 2 * x[2] - (x[3] >> 1);
        c3 = ((x[3] - x[0]) >> 1) + ((3 * (x[1] - x[2])) >> 1);
        y = ((((((c3 * t) >> 12) + c2) * t >> 12) + c1) * t >> 12) + x[1];
        y = (y * (w >> 8)) >> 15;
        w += dw;
        buf[i] = add ? buf[i] + y : y;
    }
    s->xm1 = a[n];
    s->x0 = a[n + 1];
    s->x1 = a[n + 2];
    s->x2 = a[n + 3];
    s->frac = f & 0xFFFFu;
    s->w = s->wt;
}

/* filter, gain ramp, pan, accumulate */
static __attribute__((noinline)) void voice_out(voice *v, const int32_t *buf, int32_t *L, int32_t *R,
                                               int32_t dgl, int32_t dgr)
{
    int i;
    int32_t y = v->y, a = v->a >> 8, gl = v->gl, gr = v->gr;
    for (i = 0; i < ERK_BLOCK; i++) {
        int32_t x = sat16(buf[i]);
        y += (a * (x - y)) >> 14;
        gl += dgl;
        gr += dgr;
        L[i] += (y * (gl >> 15)) >> MIXSH;
        R[i] += (y * (gr >> 15)) >> MIXSH;
    }
    v->y = y;
    v->gl = gl;
    v->gr = gr;
}

/* ---------------------------------------------------------------- voices */

static uint32_t isqrt(uint32_t x)
{
    uint32_t r = 0, b = 1u << 30;
    while (b > x)
        b >>= 2;
    while (b) {
        if (x >= r + b) {
            x -= r + b;
            r = (r >> 1) + b;
        } else
            r >>= 1;
        b >>= 2;
    }
    return r;
}

static int32_t gain_of(const voice *v) { return (int32_t)(((v->env >> 15) * (uint32_t)v->amp) >> 15); }   /* Q15 */

static voice *alloc(int lo, int hi)
{
    int i;
    for (i = lo; i < hi; i++)
        if (vc[i].state == V_FREE)
            return &vc[i];
    return 0;
}

/* constant-power pan of key k (0..87) at the width */
static void pan_of(int k, int32_t *pl, int32_t *pr)
{
    int32_t p = 16384 + (((st.width * 13107) >> 15) * (k * 2 - 87)) / 87;   /* 0.5 +- 0.4 x width */
    *pr = sinq(p);
    *pl = sinq(32767 - p);
}

static void noise(int key, uint32_t lvl)
{
    const erk_smp *m;
    voice *v = alloc(ERK_VOICES + ERK_DYING, ERK_NV);
    if (!bk->hdr->nrel || lvl < 64 || !v)            /* all busy: drop it (cutting one would click) */
        return;
    m = &bk->rel[bk->key[key].rel < bk->hdr->nrel ? bk->key[key].rel : 0];
    memset(v, 0, sizeof *v);
    v->state = V_NOISE;
    v->key = (uint8_t)key;
    v->inc = (uint32_t)(((uint32_t)m->rate << 15) / ERK_FS * 2u);
    v->nst = 1;
    stream_start(&v->st[0], m, clampi((32767 * (int32_t)m->gain) >> 12, 0, 32767));
    v->env = 1u << 30;
    v->m = v->m2 = 1u << 30;
    v->knee = 0xffffffffu;
    v->amp = (int32_t)lvl;
    v->a = v->a_floor = 16384 << 8;
    v->dark = 65535;
    pan_of(key, &v->pl, &v->pr);
    v->gl = gain_of(v) * v->pl;
    v->gr = gain_of(v) * v->pr;
}

static void release(voice *v)
{
    uint32_t g;
    if (v->flags & F_NODAMP) {
        v->state = V_RING;                            /* no damper: rings on at its decay */
        return;
    }
    v->state = V_REL;
    v->m = v->mr1;
    v->rblocks = 0;
    /* damper noise: louder for loud, still-ringing notes */
    g = (uint32_t)gain_of(v);                         /* Q15 */
    g = ((uint32_t)st.rel_level * (9830u + ((22937u * isqrt(g << 15)) >> 15))) >> 15;
    g = (g * isqrt((uint32_t)v->amp << 15)) >> 15;
    noise(v->key, g);
}

static int key_sost(int k) { return (int)(erk_sost_keys[k >> 5] >> (k & 31) & 1u); }

void erk_note_on(int key, int vel)
{
    const erk_key *k;
    const erk_vel *e;
    const erk_smp *s0, *s1;
    voice *v;
    int i, n = 0, nl;
    int32_t g, a;
    key -= ERK_KEY0;
    if (key < 0 || key >= ERK_KEYS || vel <= 0 || !bk)
        return;
    if (vel > 127)
        vel = 127;
    if (soft)                                         /* una corda: the hammer meets fewer strings */
        vel = 1 + (vel * (32767 - ((st.una * 6553) >> 15))) / 32768;
    k = &bk->key[key];
    e = &bk->vel[vel];
    nl = bk->hdr->nlayers;
    for (i = 0; i < ERK_VOICES + ERK_DYING; i++) {
        voice *o = &vc[i];
        if (o->state == V_FREE)
            continue;
        if (o->key == key && o->state != V_FADE) {    /* the same string struck again */
            o->state = V_FADE;
            o->m = FADE_M;
        }
        if (o->state != V_FADE)
            n++;
    }
    if (n >= ERK_VOICES) {                            /* steal: the quietest, released ones first */
        voice *q = 0;
        int32_t qg = 0x7fffffff;
        for (i = 0; i < ERK_VOICES + ERK_DYING; i++) {
            voice *o = &vc[i];
            int32_t gg;
            if (o->state == V_FREE || o->state == V_FADE)
                continue;
            gg = gain_of(o) >> (o->state == V_ON ? 0 : 2);
            if (gg < qg) {
                qg = gg;
                q = o;
            }
        }
        if (q) {
            q->state = V_FADE;
            q->m = FADE_M;
        }
    }
    v = alloc(0, ERK_VOICES + ERK_DYING);
    if (!v) {                                         /* every slot fading: cut the quietest */
        v = &vc[0];
        for (i = 1; i < ERK_VOICES + ERK_DYING; i++)
            if (gain_of(&vc[i]) < gain_of(v))
                v = &vc[i];
    }
    memset(v, 0, sizeof *v);
    v->key = (uint8_t)key;
    v->flags = (uint8_t)(k->flags | (key_sost(key) ? F_SOST : 0));
    v->rel = k->rel;
    v->inc = k->inc;
    if (st.detune) {                                  /* vintage: the unisons a little apart, by key */
        int32_t c = (int32_t)(((uint32_t)key * 2654435761u) >> 24) - 128;   /* -128..127 */
        v->inc = (uint32_t)((int32_t)v->inc + ((((int32_t)v->inc >> 4) * c * st.detune) >> 17));   /* +-detune/10 cents */
    }
    s0 = &bk->smp[k->zone * nl + e->l0];
    s1 = &bk->smp[k->zone * nl + e->l1];
    {   /* the CPU: at most ERK_XF_MAX notes crossfade two layers at once; the next ones take the nearer layer */
        int x = 0;
        for (i = 0; i < ERK_VOICES + ERK_DYING; i++)
            x += vc[i].state != V_FREE && vc[i].nst == 2;
        if (e->w && e->l1 != e->l0 && x >= ERK_XF_MAX) {
            if (e->w >= 16384)
                s0 = s1;
            s1 = s0;
        }
    }
    if (e->w && e->l1 != e->l0 && s0 != s1) {
        v->nst = 2;
        stream_start(&v->st[0], s0, ((32767 - e->w) * (int32_t)s0->gain) >> 12);
        stream_start(&v->st[1], s1, (e->w * (int32_t)s1->gain) >> 12);
        v->keep = e->w >= 16384;
        v->wend = clampi((int32_t)(v->keep ? s1->gain : s0->gain) << 3, 0, 32767);
    } else {
        v->nst = 1;
        stream_start(&v->st[0], s0, (int32_t)s0->gain << 3);    /* Q12 -> Q15 */
    }
    for (i = 0; i < v->nst; i++)
        v->st[i].w = v->st[i].wt = clampi(v->st[i].w, 0, 32767);
    v->w0s = v->st[0].w;
    v->w1s = v->st[1].w;
    g = e->amp;
    if (soft)
        g = (g * (32767 - ((st.una * 9000) >> 15))) >> 15;
    v->amp = g;
    v->env = 1u << 30;
    v->m = k->m1;
    v->m2 = k->m2;
    v->mr1 = k->mr1;
    v->mr2 = k->mr2;
    v->knee = k->knee;
    a = clampi((e->a * st.bscale) >> 12, 200, 16384);
    if (soft)
        a = (a * (32767 - ((st.una * 12000) >> 15))) >> 15;
    a = a > k->a_floor ? a : k->a_floor;
    v->a = a << 8;
    v->a_floor = (k->a_floor < a ? k->a_floor : a) << 8;
    v->dark = k->dark;
    pan_of(key, &v->pl, &v->pr);
#if ERK_MODAL
    if (cfg.mode != ERK_SAMPLED && cfg.modal) {       /* the partials carry the decay */
        erkm_start(&v->mv, cfg.modal, key, e->l0, e->l1, e->w, vel, soft);
        v->senv = 1u << 30;
        v->m = v->m2 = 1u << 30;
        v->dark = 65535;
        if (cfg.mode == ERK_MODELLED) {
            v->nst = 0;
            v->a = v->a_floor = 16384 << 8;
        }
    }
#endif
    v->gl = gain_of(v) * v->pl;                       /* start at the target: keep the hammer transient */
    v->gr = gain_of(v) * v->pr;
    v->state = V_ON;
}

void erk_note_off(int key)
{
    int i;
    key -= ERK_KEY0;
    for (i = 0; i < ERK_VOICES + ERK_DYING; i++) {
        voice *v = &vc[i];
        if (v->key == key && v->state == V_ON) {
            if (sus || (v->flags & F_SOST))
                v->state = V_SUST;                    /* the pedal (or the sostenuto) holds its damper up */
            else
                release(v);
        }
    }
}

void erk_sustain(int val)
{
    int i, on;
    sus_lvl = clampi(val, 0, 127) * 258;
    on = sus ? sus_lvl > SUS_OFF : sus_lvl > SUS_ON;
    send_t = sus_lvl;
    /* half pedal: between half and full the held notes lose more the less the pedal is down */
    {
        int32_t f = clampi((sus_lvl - SUS_ON) * 32767 / (32767 - SUS_ON), 0, 32767);   /* 0 at the bite, 1 full */
        m_half = mulq30(1u << 30, 1u << 30) - (uint32_t)muls((int32_t)((1u << 30) - (bk ? bk->key[40].mr2 : (1u << 30))),
                                                             32767 - f, 15) / 3u;
    }
    if (sus && !on)
        for (i = 0; i < ERK_VOICES + ERK_DYING; i++)
            if (vc[i].state == V_SUST && !(vc[i].flags & F_SOST))
                release(&vc[i]);
    sus = on;
    erk_sus_q15 = sus_lvl;
}

void erk_sostenuto(int on)
{
    int i;
    on = !!on;
    if (on && !sost) {                                /* the dampers of the notes sounding now stay up */
        uint32_t m[3] = {0, 0, 0};
        for (i = 0; i < ERK_VOICES + ERK_DYING; i++)
            if (vc[i].state == V_ON || (vc[i].state == V_SUST && sus)) {
                vc[i].flags |= F_SOST;
                m[vc[i].key >> 5] |= 1u << (vc[i].key & 31);
            }
        erk_sost_keys[0] = m[0];
        erk_sost_keys[1] = m[1];
        erk_sost_keys[2] = m[2];
    } else if (!on && sost) {
        erk_sost_keys[0] = erk_sost_keys[1] = erk_sost_keys[2] = 0;
        for (i = 0; i < ERK_VOICES + ERK_DYING; i++)
            if (vc[i].flags & F_SOST) {
                vc[i].flags &= (uint8_t)~F_SOST;
                if (vc[i].state == V_SUST && !sus)
                    release(&vc[i]);
            }
    }
    sost = on;
}

void erk_soft(int val)
{
    soft_lvl = clampi(val, 0, 127) * 258;
    soft = soft_lvl >= 16384;
    erk_soft_q15 = soft_lvl;
}

void erk_all_off(void)
{
    int i;
    for (i = 0; i < ERK_VOICES + ERK_DYING; i++)
        if (vc[i].state == V_ON || vc[i].state == V_SUST)
            release(&vc[i]);
}

void erk_panic(void)
{
    int i;
    for (i = 0; i < ERK_NV; i++)
        if (vc[i].state != V_FREE) {
            vc[i].state = V_FADE;
            vc[i].m = FADE_M;
        }
}

void erk_shed(void)
{
    int i, n = 0;
    for (i = 0; i < ERK_VOICES + ERK_DYING && n < 2; i++)
        if (vc[i].state == V_REL || vc[i].state == V_RING || (vc[i].state == V_SUST && gain_of(&vc[i]) < 600)) {
            vc[i].state = V_FADE;
            vc[i].m = FADE_M;
            n++;
        }
}

int erk_active(void)
{
    int i, n = 0;
    for (i = 0; i < ERK_NV; i++)
        n += vc[i].state != V_FREE;
    return n;
}

/* ---------------------------------------------------------------- resonance: 12 combs at 11.025 kHz */

#define NRES 12
/* the strings C1..B1 at a quarter of the rate (their loss filter keeps them below ~2 kHz) */
static const uint16_t res_len[NRES] = {337, 318, 300, 283, 268, 253, 238, 225, 212, 200, 189, 179};
typedef struct {
    int16_t *ln;
    int32_t pos, len, lp, g;
} comb;
static int16_t res_buf[337 + 318 + 300 + 283 + 268 + 253 + 238 + 225 + 212 + 200 + 189 + 179];
static comb res_c[NRES];
static int32_t res_prev_l, res_prev_r;
static int32_t ped;            /* Q15 smoothed pedal: the strings' feedback follows it */
/* feedback a pass for RT60 3.5 s (pedal down) and 0.12 s (dampers on the strings), Q15 */
static const uint16_t res_gdn[NRES] = {30850, 30955, 31055, 31146, 31236, 31320, 31401, 31474, 31548, 31613, 31678, 31737};
static const uint16_t res_gup[NRES] = {5640, 6228, 6842, 7457, 8107, 8768, 9457, 10122, 10832, 11503, 12215, 12903};

static __attribute__((noinline)) void res_block(const int32_t *L, const int32_t *R, int32_t *oL, int32_t *oR)
{
    int j, c, i;
    int32_t sd = send;
    for (j = 0; j < ERK_BLOCK / 4; j++) {
        const int32_t *l4 = L + 4 * j, *r4 = R + 4 * j;
        int32_t in = (l4[0] + l4[1] + l4[2] + l4[3] + r4[0] + r4[1] + r4[2] + r4[3]) >> 8;   /* 2^18 -> 2^13 */
        int32_t a = 0, b = 0, l, r, dl, dr;
        in = (in * sd) >> 15;
        for (c = 0; c < NRES; c++) {
            comb *k = &res_c[c];
            int32_t p = k->pos, o = k->ln[p];
            k->lp += ((o - k->lp) * 9000) >> 15;                               /* string loss: highs die first */
            o = (k->lp * k->g) >> 15;
            k->ln[p] = (int16_t)sat16(in + o);
            k->pos = p + 1 == k->len ? 0 : p + 1;
            if (c & 1)
                b += o;
            else
                a += o;
        }
        l = a + (b >> 1);
        r = b + (a >> 1);
        dl = (l - res_prev_l) >> 2;                   /* back to 44.1 kHz: linear */
        dr = (r - res_prev_r) >> 2;
        for (i = 0; i < 4; i++) {
            res_prev_l += dl;
            res_prev_r += dr;
            oL[4 * j + i] = res_prev_l;
            oR[4 * j + i] = res_prev_r;
        }
        res_prev_l = l;
        res_prev_r = r;
    }
}

/* ---------------------------------------------------------------- reverb: 8-line FDN at 22.05 kHz */

#define NFDN 8
static const uint16_t fdn_hall[NFDN] = {701, 811, 919, 1031, 1153, 1279, 1409, 1543};
static const uint16_t fdn_cham[NFDN] = {433, 503, 571, 641, 709, 787, 877, 953};
static const uint16_t ap_len[4] = {113, 167, 229, 317};
#define PRE_MAX 662             /* 30 ms at 22.05 kHz */
static int16_t fdn_buf[701 + 811 + 919 + 1031 + 1153 + 1279 + 1409 + 1543];
static int16_t ap_buf[113 + 167 + 229 + 317], pre_buf[PRE_MAX];
static int16_t *fdn_line[NFDN], *ap_line[4];
static uint16_t fdn_n[NFDN], fdn_pos[NFDN], ap_pos[4], pre_pos, pre_n;
static int32_t fdn_lp[NFDN], fdn_g[NFDN], fdn_damp, rev_prev_l, rev_prev_r, rev_on, rev_mix, rev_w;

static __attribute__((noinline)) void rev_block(const int32_t *L, const int32_t *R, int32_t *oL, int32_t *oR)
{
    int j, i;
    for (j = 0; j < ERK_BLOCK / 2; j++) {
        int32_t in = (L[2 * j] + R[2 * j] + L[2 * j + 1] + R[2 * j + 1]) >> 8;   /* -> 2^12 scale */
        int32_t o[NFDN], h[NFDN], l, r, m, s;
        int32_t x = pre_buf[pre_pos];
        pre_buf[pre_pos] = (int16_t)sat16(in);
        pre_pos = (uint16_t)(pre_pos + 1 >= pre_n ? 0 : pre_pos + 1);
        for (i = 0; i < 4; i++) {                    /* input diffusion: Schroeder all-passes, g = 0.6 */
            int16_t *ln = ap_line[i];
            int p = ap_pos[i];
            int32_t d = ln[p], v = x - ((d * 19661) >> 15);
            ln[p] = (int16_t)sat16(v);
            x = d + ((v * 19661) >> 15);
            ap_pos[i] = (uint16_t)(p + 1 == ap_len[i] ? 0 : p + 1);
        }
        for (i = 0; i < NFDN; i++) {
            int32_t d = fdn_line[i][fdn_pos[i]];
            fdn_lp[i] += ((d - fdn_lp[i]) * fdn_damp) >> 15;
            o[i] = d;
            h[i] = fdn_lp[i];
        }
        /* fast Walsh-Hadamard (x 1/sqrt 8 folded into fdn_g) */
        for (i = 0; i < 8; i += 2) { int32_t p = h[i], q = h[i + 1]; h[i] = p + q; h[i + 1] = p - q; }
        for (i = 0; i < 8; i += 4) {
            int32_t p0 = h[i], p1 = h[i + 1], q0 = h[i + 2], q1 = h[i + 3];
            h[i] = p0 + q0; h[i + 1] = p1 + q1; h[i + 2] = p0 - q0; h[i + 3] = p1 - q1;
        }
        for (i = 0; i < 4; i++) { int32_t p = h[i], q = h[i + 4]; h[i] = p + q; h[i + 4] = p - q; }
        for (i = 0; i < NFDN; i++) {
            int p = fdn_pos[i];
            fdn_line[i][p] = (int16_t)sat16(((h[i] * fdn_g[i]) >> 15) + ((i & 1) ? -x : x));
            fdn_pos[i] = (uint16_t)(p + 1 >= fdn_n[i] ? 0 : p + 1);
        }
        l = o[0] - o[2] + o[4] - o[6];
        r = o[1] - o[3] + o[5] - o[7];
        m = (l + r) >> 1;                             /* the width: the side scaled */
        s = (((l - r) >> 1) * rev_w) >> 15;
        l = m + s;
        r = m - s;
        oL[2 * j] = (l + rev_prev_l) >> 1;
        oR[2 * j] = (r + rev_prev_r) >> 1;
        oL[2 * j + 1] = l;
        oR[2 * j + 1] = r;
        rev_prev_l = l;
        rev_prev_r = r;
    }
}

static const float ROOM_RT[4] = {0.0f, 1.1f, 2.2f, 3.8f};   /* dry, chamber, hall, church (size 0.5) */

float erk_room_seconds(int type, int size_q15)
{
    if (type <= ERK_REV_OFF || type > ERK_REV_CHURCH)
        return 0.0f;
    return ROOM_RT[type] * (0.5f + (float)size_q15 / 32768.0f);
}

static void rev_apply(void)                           /* the render, at a block's start */
{
    int i, type = st.rev_type;
    int16_t *p = fdn_buf;
    float rt = erk_room_seconds(type, st.rev_size);
    rev_on = type != ERK_REV_OFF;
    for (i = 0; i < NFDN; i++) {
        fdn_n[i] = type == ERK_REV_CHAMBER ? fdn_cham[i] : fdn_hall[i];
        fdn_line[i] = p;
        p += fdn_hall[i];
        fdn_pos[i] = 0;
        fdn_lp[i] = 0;
        /* 10^(-3 n / (fs rt)) / sqrt 8 */
        fdn_g[i] = rev_on ? (int32_t)(exp2_f(-3.0f * 3.3219281f * (float)fdn_n[i] / (22050.0f * rt)) * 11585.2f) : 0;
    }
    fdn_damp = type == ERK_REV_CHURCH ? 12500 : type == ERK_REV_HALL ? 15000 : 19000;
    pre_n = type == ERK_REV_CHURCH ? PRE_MAX : type == ERK_REV_HALL ? 330 : 150;
    pre_pos = 0;
    memset(fdn_buf, 0, sizeof fdn_buf);
    memset(ap_buf, 0, sizeof ap_buf);
    memset(pre_buf, 0, sizeof pre_buf);
    p = ap_buf;
    for (i = 0; i < 4; i++) {
        ap_line[i] = p;
        p += ap_len[i];
        ap_pos[i] = 0;
    }
    rev_prev_l = rev_prev_r = 0;
}

/* ---------------------------------------------------------------- the settings */

void erk_set_room(int type, int size_q15, int amount_q15)
{
    type = clampi(type, ERK_REV_OFF, ERK_REV_CHURCH);
    size_q15 = clampi(size_q15, 0, 32767);
    if (type != st.rev_type || size_q15 != st.rev_size) {
        st.rev_type = type;
        st.rev_size = size_q15;
        st.rev_dirty = 1;
    }
    st.rev_mix = clampi(amount_q15, 0, 32767);
}

/* the pitch wheel: every string's rate times the ratio (Q16, 2^(semitones / 12)), the release noises left alone */
static volatile int32_t bend_q16 = 65536;
void erk_bend(int ratio_q16) { bend_q16 = clampi(ratio_q16, 49152, 87381); }   /* (a fourth either way at most) */
void erk_set_width(int width_q15) { st.width = clampi(width_q15, 0, 32767); }
void erk_set_resonance(int level_q15) { st.res_level = clampi(level_q15, 0, 32767); }
void erk_set_rel_level(int level_q15) { st.rel_level = clampi(level_q15, 0, 32767); }
void erk_set_master(int q15) { st.master = clampi(q15, 0, 32767); }

/* the tones: the velocity filter's brightness, a low and a high shelf (dB), string resonance, unison detune */
static const struct { float bright, lo_db, hi_db, res; int16_t detune; } TONE[ERK_NTONES] = {
    {1.00f, 0.0f, 0.0f, 1.00f, 0},          /* CONCERT: the recording as it is */
    {1.15f, -1.0f, 2.5f, 0.70f, 0},         /* STUDIO: closer, brighter, tighter */
    {0.80f, 1.5f, -3.5f, 1.25f, 22},        /* VINTAGE: darker, warmer, the unisons a little apart */
    {0.88f, 2.5f, -1.5f, 0.90f, 6},         /* JAZZ: round, warm low mids, softer top */
};

void erk_set_tone(int tone, int brilliance_q15, int una_q15)
{
    float b = ((float)clampi(brilliance_q15, 0, 32767) / 32768.0f - 0.5f) * 2.0f;   /* -1 .. +1 */
    tone = clampi(tone, 0, ERK_NTONES - 1);
    st.bscale = (int32_t)(4096.0f * TONE[tone].bright * exp2_f(0.7f * b));
    st.g_lo = (int32_t)(4096.0f * (exp2_f(TONE[tone].lo_db * 0.1660964f) - 1.0f));    /* dB / 20 log2 10 */
    st.g_hi = (int32_t)(4096.0f * (exp2_f((TONE[tone].hi_db + 6.0f * b) * 0.1660964f) - 1.0f));
    st.res_scale = (int32_t)(32767.0f * TONE[tone].res);
    st.detune = TONE[tone].detune;
    st.una = clampi(una_q15, 0, 32767);
}

/* ---------------------------------------------------------------- render */

void erk_init(const erk_bank *b, const erk_cfg *c)
{
    int i;
    int16_t *p = res_buf;
    bk = b;
    cfg = *c;
    memset(vc, 0, sizeof vc);
    memset(res_buf, 0, sizeof res_buf);
    bend_q16 = 65536;
    for (i = 0; i < NRES; i++) {
        res_c[i].ln = p;
        res_c[i].len = res_len[i];
        res_c[i].pos = 0;
        res_c[i].lp = 0;
        res_c[i].g = 0;
        p += res_len[i];
    }
    st.rev_type = cfg.reverb;
    st.rev_mix = cfg.rev_mix;
    st.res_level = cfg.res_level;
    st.rel_level = cfg.rel_level;
    st.master = cfg.master;
    rev_apply();
    st.rev_dirty = 0;
#if ERK_MODAL
    erkm_init();
#endif
    sus = soft = sost = 0;
    sus_lvl = soft_lvl = 0;
    send = send_t = 0;
    ped = 0;
    m_half = 1u << 30;
    erk_sost_keys[0] = erk_sost_keys[1] = erk_sost_keys[2] = 0;
}

#if ERK_MODAL
/* the hybrid's equal-power fade at block k of the handover, interpolated in a 17-point quarter wave */
static int32_t fade(int dir, int32_t k)
{
    int32_t n = HY_B - HY_A;
    int32_t x = k <= 0 ? 0 : k >= n ? 32767 : k * 32767 / n;
    return sinq(dir ? x : 32767 - x);
}
#endif

static uint16_t lvl_acc[ERK_KEYS];

static void voice_block(voice *v, int32_t *L, int32_t *R)
{
    int32_t buf[ERK_BLOCK], g, tl, tr;
    uint32_t env, inc;
    int i;
    /* the envelope at the block's end */
    env = mulq30(v->env, v->m);
    if (v->state == V_SUST && !(v->flags & F_SOST) && m_half < (1u << 30))
        env = mulq30(env, m_half);                    /* half pedal */
    v->blocks++;
    if (v->blocks == v->knee && (v->state == V_ON || v->state == V_SUST || v->state == V_RING))
        v->m = v->m2;
    if (v->state == V_REL && ++v->rblocks == REL1)
        v->m = v->mr2;
    if (v->blocks > DARK_FROM)
        v->a = v->a_floor + muls(v->a - v->a_floor, (int32_t)v->dark, 16);
    v->env = env;
    if (cfg.mode == ERK_SAMPLED && v->nst == 2 && v->blocks >= XF_AT) {   /* layer handover */
        int32_t k = (int32_t)(v->blocks - XF_AT);
        if (k >= XF_LEN) {
            if (v->keep)
                v->st[0] = v->st[1];
            v->st[0].w = v->st[0].wt = v->wend;
            v->nst = 1;
        } else {
            int32_t e0 = v->keep ? 0 : v->wend, e1 = v->keep ? v->wend : 0;
            v->st[0].wt = v->w0s + (e0 - v->w0s) * (k + 1) / XF_LEN;
            v->st[1].wt = v->w1s + (e1 - v->w1s) * (k + 1) / XF_LEN;
        }
    }
    g = gain_of(v);
    tl = g * v->pl;
    tr = g * v->pr;
    if (v->state != V_NOISE && v->key < ERK_KEYS && g > lvl_acc[v->key])
        lvl_acc[v->key] = (uint16_t)g;
#if ERK_MODAL
    if (cfg.mode == ERK_HYBRID && v->nst && v->state != V_NOISE) {
        int32_t k = (int32_t)v->blocks + 1 - HY_A, gs;
        v->senv = mulq30(v->senv, bk->key[v->key].m1);
        if (k > HY_B - HY_A + 1)
            v->nst = 0;                               /* faded to 0 over the last block */
        gs = fade(0, k);
        v->st[0].wt = muls(muls(v->w0s, (int32_t)(v->senv >> 15), 15), gs, 15);
        v->st[1].wt = muls(muls(v->w1s, (int32_t)(v->senv >> 15), 15), gs, 15);
    }
#endif
    inc = v->inc;
    if (bend_q16 != 65536 && v->state != V_NOISE) {
        inc = (inc * (uint32_t)(bend_q16 >> 2)) >> 14;
        if (inc > 0xFFFFu)
            inc = 0xFFFFu;                            /* stream_run: at most one source sample an output sample */
    }
    if (v->nst) {
        stream_run(&v->st[0], inc, buf, 0);
        erk_st.stream_blocks++;
    } else
        memset(buf, 0, sizeof buf);
    for (i = 1; i < v->nst; i++) {
        stream_run(&v->st[i], inc, buf, 1);
        erk_st.stream_blocks++;
    }
#if ERK_MODAL
    if (cfg.mode != ERK_SAMPLED && cfg.modal && v->state != V_NOISE) {
        int32_t gm = 32767, lvl;
        if (cfg.mode == ERK_HYBRID)                   /* the partials fade in as the attack fades out */
            gm = fade(1, (int32_t)v->blocks + 1 - HY_A);
        lvl = erkm_block(&v->mv, buf, gm, cfg.mode == ERK_MODELLED || v->blocks >= HY_MID);
        erk_st.partial_blocks += (uint64_t)v->mv.np;
        if (!v->nst && (int32_t)v->blocks > HY_B && lvl < 2)
            env = 0;                                  /* every partial has died away */
    }
#endif
    voice_out(v, buf, L, R, (tl - v->gl) / ERK_BLOCK, (tr - v->gr) / ERK_BLOCK);
    erk_st.voice_blocks++;
    if (env < (v->state == V_ON ? ENV_MIN : ENV_MIN_LET) || (v->state != V_ON && v->state != V_NOISE && g < 24) ||
        (v->nst && v->st[0].done))
        v->state = V_FREE;                            /* (let go and below -63 dB of a full note: its slot is free) */
}

/* the metronome's tick: a short sine burst (1.6 kHz, the downbeat 2.1 kHz), ~25 ms */
static int32_t clk_c, clk_s, clk_rc, clk_rs, clk_amp, clk_n;

void erk_click(int accent)
{
    clk_c = 0;
    clk_s = 1 << 14;
    clk_rc = accent ? 15657 : 15960;                  /* cos / sin (Q14) of 2 pi f / 44100 */
    clk_rs = accent ? 4830 : 3703;
    clk_amp = accent ? 9000 : 6000;
    clk_n = 1100;
}

static __attribute__((noinline)) void click_block(int32_t *L, int32_t *R)
{
    int i;
    for (i = 0; i < ERK_BLOCK && clk_n > 0; i++, clk_n--) {
        int32_t c = clk_c, s = clk_s, o;
        clk_c = (c * clk_rc - s * clk_rs) >> 14;
        clk_s = (s * clk_rc + c * clk_rs) >> 14;
        o = (((clk_s * clk_amp) >> 14) * ((clk_n * 30) >> 10)) >> 6;   /* ~-35 dB of a full note */
        L[i] += o;
        R[i] += o;
    }
}

/* the tone EQ, on the piano before the room: a low shelf (one-pole at 250 Hz) and a high shelf (at 3 kHz) */
static int32_t eq_l[4], eq_r[4];

static __attribute__((noinline)) void eq_block(int32_t *x, int32_t *s, int32_t glo, int32_t ghi)
{
    int i;
    int32_t lo = s[0], hi = s[1];
    for (i = 0; i < ERK_BLOCK; i++) {
        int32_t v = x[i];
        lo += ((v - lo) >> 6) * 143 >> 6;             /* a = 0.0350 (Q12 143) */
        v += ((lo >> 6) * glo) >> 6;
        hi += ((v - hi) >> 6) * 1425 >> 6;            /* a = 0.348 */
        v += (((v - hi) >> 6) * ghi) >> 6;
        x[i] = v;
    }
    s[0] = lo;
    s[1] = hi;
}

static inline int32_t sat24(int32_t v) { return v < -8388608 ? -8388608 : v > 8388607 ? 8388607 : v; }

void erk_render_gain(int32_t *lr, int frames, int32_t master_q12)
{
    int f, i;
    for (f = 0; f < frames; f += ERK_BLOCK) {
        int32_t L[ERK_BLOCK], R[ERK_BLOCK], rl[ERK_BLOCK], rr[ERK_BLOCK], wl[ERK_BLOCK], wr[ERK_BLOCK];
        int32_t gt, mq, rlev;
        if (st.rev_dirty) {
            st.rev_dirty = 0;
            rev_apply();
        }
        memset(L, 0, sizeof L);
        memset(R, 0, sizeof R);
        memset(lvl_acc, 0, sizeof lvl_acc);
        for (i = 0; i < ERK_NV; i++)
            if (vc[i].state != V_FREE)
                voice_block(&vc[i], L, R);
        for (i = 0; i < ERK_KEYS; i++)
            erk_key_lvl[i] = lvl_acc[i];
        if (st.g_lo || st.g_hi) {
            eq_block(L, eq_l, st.g_lo, st.g_hi);
            eq_block(R, eq_r, st.g_lo, st.g_hi);
        }
        /* resonance: the pedal sets the send and how long the strings ring (RT ~3.5 s down, ~0.1 s up) */
        send += (send_t - send) >> 3;
        gt = sus ? 32767 : 0;
        ped += (gt - ped) >> 3;
        for (i = 0; i < NRES; i++)
            res_c[i].g = res_gup[i] + (((res_gdn[i] - res_gup[i]) * ped) >> 15);
        rlev = (st.res_level * (st.res_scale >> 4)) >> 11;
        if (rlev || ped > 64) {
            res_block(L, R, rl, rr);
            for (i = 0; i < ERK_BLOCK; i++) {
                L[i] += (rl[i] * rlev) >> 10;         /* 2^13 scale -> 2^18 */
                R[i] += (rr[i] * rlev) >> 10;
            }
        }
        if (clk_n > 0)
            click_block(L, R);
        rev_mix += (st.rev_mix - rev_mix) >> 4;
        rev_w = (st.width >> 1) + 16384;
        if (rev_on) {
            rev_block(L, R, wl, wr);
            for (i = 0; i < ERK_BLOCK; i++) {
                L[i] += (wl[i] * rev_mix) >> 9;       /* 2^12 scale -> 2^18 */
                R[i] += (wr[i] * rev_mix) >> 9;
            }
        }
        mq = (st.master * clampi(master_q12, 0, 4096)) >> 15;   /* Q12 */
        for (i = 0; i < ERK_BLOCK; i++) {             /* 2^18 x Q12 -> 24 bit, 32-bit products */
            lr[2 * (f + i)] = sat24(((clampi(L[i], -8388607, 8388607) >> 5) * mq) >> 2);
            lr[2 * (f + i) + 1] = sat24(((clampi(R[i], -8388607, 8388607) >> 5) * mq) >> 2);
        }
        erk_st.blocks++;
    }
}

void erk_render(int32_t *lr, int frames) { erk_render_gain(lr, frames, 4096); }

/* static RAM of the engine (bytes): voices (with / without the modal state), resonance, reverb */
void erk_ram(int *voices, int *modal_part, int *res, int *rev)
{
    *voices = (int)sizeof vc;
#if ERK_MODAL
    *modal_part = (int)(sizeof(erkm_voice) * ERK_NV);
#else
    *modal_part = 0;
#endif
    *res = (int)(sizeof res_buf + sizeof res_c);
    *rev = (int)(sizeof fdn_buf + sizeof ap_buf + sizeof pre_buf + sizeof fdn_lp);
}
