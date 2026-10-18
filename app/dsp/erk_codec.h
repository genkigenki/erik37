/* SPDX-License-Identifier: MPL-2.0
 * Erik37: Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* The sample format: block ADPCM, 32 samples in 17 bytes (4.25 bit/sample). Header byte: filter << 4 | shift;
 * then 16 bytes of signed 4-bit residuals, low nibble first. A sample is
 *   s = clamp16(((c1 * s1 + c2 * s2 + 32) >> 6) + q * 2^shift)
 * with the block's predictor (c1, c2) in Q6 from erk_filt. Filters 0-3 are the classic fixed set (none, first
 * order, two second-order resonators); 4-15 add more second-order shapes, which pay off on the strongly tonal,
 * low-frequency-weighted piano material (see tools/erkcodec.c, the encoder). */
#ifndef ERK_CODEC_H
#define ERK_CODEC_H
#include <stdint.h>

#define ERK_BLK 32
#define ERK_BLKB 17
#define ERK_NFILT 16

static const int16_t erk_filt[ERK_NFILT][2] = {
    {0, 0}, {60, 0}, {122, -60}, {115, -52},
    {126, -63}, {124, -61}, {118, -58}, {108, -52}, {98, -55}, {88, -50}, {76, -46}, {60, -40},
    {40, -36}, {16, -32}, {30, 0}, {-30, -24},
};

typedef struct {
    const uint8_t *cur, *next;   /* this block's residual bytes; the next block's header */
    int nib, sh, c1, c2, s1, s2;
} erk_dec;

static inline void erk_dec_start(erk_dec *d, const uint8_t *blk, int s1, int s2)
{
    d->next = blk;
    d->cur = blk + 1;
    d->nib = ERK_BLK;
    d->sh = d->c1 = d->c2 = 0;
    d->s1 = s1;
    d->s2 = s2;
}

static inline __attribute__((always_inline)) int erk_dec_next(erk_dec *d)
{
    int n, s;
    if (d->nib == ERK_BLK) {
        const uint8_t *b = d->next;
        int h = b[0];
        d->sh = h & 15;
        d->c1 = erk_filt[h >> 4][0];
        d->c2 = erk_filt[h >> 4][1];
        d->cur = b + 1;
        d->next = b + ERK_BLKB;
        d->nib = 0;
    }
    n = d->cur[d->nib >> 1];
    n = (d->nib & 1) ? n >> 4 : n & 15;
    d->nib++;
    s = ((d->c1 * d->s1 + d->c2 * d->s2 + 32) >> 6) + ((n ^ 8) - 8) * (1 << d->sh);
    s = s < -32768 ? -32768 : s > 32767 ? 32767 : s;
    d->s2 = d->s1;
    d->s1 = s;
    return s;
}
#endif
