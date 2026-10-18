/* SPDX-License-Identifier: MPL-2.0
 * Erik37: Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* Modal sustain (see erk_modal.h). A partial a sample: phase += inc; out += sin[phase >> 20] * a; a += da. A block:
 * the decay (two slopes), the beat (a block-rate rotator), the next block's amplitude target. */
#include <string.h>
#include "erk_modal.h"
#include "erk.h"

#define SINBITS 12
static int16_t sintab[1 << SINBITS];

static inline uint32_t mulq30(uint32_t a, uint32_t b) { return (uint32_t)(((uint64_t)a * b) >> 30); }
static inline int32_t smul30(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 30); }

/* sin(2 pi i / N) without floating point: a Taylor series in Q30 on the first octant pair, mirrored */
void erkm_init(void)
{
    int i, n = 1 << SINBITS;
    for (i = 0; i <= n / 4; i++) {
        /* x = 2 pi i / n in Q30; pi in Q30 = 3373259426 */
        int64_t x = (int64_t)3373259426LL * 2 * i / n, x2 = (x * x) >> 30, term = x, sum = x;
        int k;
        for (k = 1; k < 12; k++) {
            term = -((term * x2) >> 30) / ((2 * k) * (2 * k + 1));
            sum += term;
        }
        {
            int32_t v = (int32_t)((sum * 32767 + (1 << 29)) >> 30);
            sintab[i] = (int16_t)v;
            sintab[n / 2 - i] = (int16_t)v;
            sintab[(n / 2 + i) & (n - 1)] = (int16_t)-v;
            sintab[(n - i) & (n - 1)] = (int16_t)-v;
        }
    }
}

int erkm_open(erkm_bank *b, const void *img)
{
    const uint8_t *p = (const uint8_t *)img;
    b->hdr = (const erkm_hdr *)p;
    if (b->hdr->magic != ERKM_MAGIC || b->hdr->np > ERKM_NP)
        return -1;
    p += sizeof(erkm_hdr);
    b->f0inc = (const uint32_t *)p;
    p += 4 * ERK_KEYS;
    b->zone = p;
    p += ERK_KEYS;
    b->p = (const erkm_partial *)p;
    return 0;
}

/* cos / sin (Q30) of the beat's angle a block: theta = 2 pi f 32 / 44100, f = beat / 256 Hz */
static void rot(uint16_t beat, int32_t *c, int32_t *s)
{
    int64_t th = (int64_t)beat * 4895393 / 256;       /* 2 pi 32 / 44100 = 0.0045592 = 4895393 in Q30 */
    int64_t t2 = (th * th) >> 30;
    *c = (int32_t)((1LL << 30) - t2 / 2 + ((t2 * t2) >> 30) / 24);
    *s = (int32_t)(th - ((th * t2) >> 30) / 6);
}

void erkm_start(erkm_voice *m, const erkm_bank *b, int key, int l0, int l1, int w, int vel, int soft)
{
    int z = b->zone[key], np = b->hdr->np, nl = b->hdr->nlayers, i, n = 0;
    const erkm_partial *p0 = &b->p[(z * nl + l0) * np], *p1 = &b->p[(z * nl + l1) * np];
    memset(m, 0, sizeof *m);
    for (i = 0; i < np; i++) {
        const erkm_partial *p = (w >= 16384) ? &p1[i] : &p0[i];
        int32_t A = (p0[i].amp * (32767 - w) + p1[i].amp * w) >> 15;
        uint64_t inc = ((uint64_t)b->f0inc[key] * p0[i].ratio) >> 20;
        if (A <= 0 || inc >= 1288490189u)             /* empty, or above 0.3 fs (13 kHz) after the key's shift */
            continue;
        if (soft)                                     /* una corda: the upper partials lose most */
            A = (A * (32767 - 600 * (n < 20 ? n : 20))) >> 15;
        m->inc[n] = (uint32_t)inc;
        m->ph[n] = 0;
        m->A[n] = A;
        m->e[n] = 1u << 30;
        m->d1[n] = p->d1;
        m->d2[n] = p->d2;
        m->knee[n] = p->knee;
        m->depth[n] = p->depth;
        m->bc[n] = 0;                                 /* the beat starts at its mean (1), not its crest */
        m->bs[n] = 1 << 30;
        rot(p->beat, &m->rc[n], &m->rs[n]);
        n++;
    }
    m->np = n;
    if (b->hdr->flags & 1) {                          /* modelled: the hammer's felt noise */
        m->noise = 1u << 30;
        m->nlfsr = 0x1234567u + (uint32_t)key * 7919u;
        m->ngain = 600 + vel * 22;
        m->nfc = 1500 + vel * 50;                     /* ~250 Hz .. 2.2 kHz: brighter for harder blows */
    }
}

int32_t erkm_block(erkm_voice *m, int32_t *buf, int32_t g, int run)
{
    int i, p;
    int32_t level = 0;
    for (p = 0; p < m->np; p++) {
        int32_t at, da, a, fac;
        uint32_t ph = m->ph[p], inc = m->inc[p];
        if (run) {
            int32_t c = m->bc[p], s = m->bs[p];
            m->e[p] = mulq30(m->e[p], m->blocks >= m->knee[p] ? m->d2[p] : m->d1[p]);
            m->bc[p] = smul30(c, m->rc[p]) - smul30(s, m->rs[p]);
            m->bs[p] = smul30(s, m->rc[p]) + smul30(c, m->rs[p]);
        }
        fac = 32768 + ((m->depth[p] * (m->bc[p] >> 15)) >> 15);                  /* Q15, 1 +- depth */
        at = (int32_t)(((((int64_t)m->A[p] * (m->e[p] >> 15)) >> 15) * fac >> 15) * g >> 15);
        level += at;
        at <<= 16;
        a = m->a[p];
        da = (at - a) / ERK_BLOCK;
        for (i = 0; i < ERK_BLOCK; i++) {
            ph += inc;
            buf[i] += (sintab[ph >> (32 - SINBITS)] * (a >> 16)) >> 15;
            a += da;
        }
        m->a[p] = a;
        m->ph[p] = ph;
    }
    if (m->noise > (1u << 16)) {                      /* hammer noise: LFSR, one-pole low-pass, ~6 ms decay */
        int32_t lp = m->nlp, gn = (int32_t)(((int64_t)(m->noise >> 15) * m->ngain >> 15) * g >> 15);
        uint32_t r = m->nlfsr;
        for (i = 0; i < ERK_BLOCK; i++) {
            r = r * 1664525u + 1013904223u;
            lp += ((((int32_t)r >> 16) - lp) * m->nfc) >> 15;
            buf[i] += (lp * gn) >> 15;
        }
        m->nlp = lp;
        m->nlfsr = r;
        m->noise = mulq30(m->noise, 950000000u);      /* 0.885 a block */
        level += gn;
    }
    if (run)
        m->blocks++;
    return level;
}
