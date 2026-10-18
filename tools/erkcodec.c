/* SPDX-License-Identifier: MPL-2.0
 * Erik37: Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* The sample codec, offline side.
 *   erkcodec enc IN.s16 OUT.bin DEC.s16 [NFILT]   block ADPCM (the engine's format); NFILT 16 (default) or 4
 *   erkcodec ima IN.s16 DEC.s16                   IMA ADPCM, for comparison only
 *   erkcodec snr REF.s16 DEC.s16                  SNR and segmental SNR in dB
 * Block ADPCM: 32 samples in 17 bytes (4.25 bit/sample): a header byte (filter << 4 | shift), then 16 bytes of
 * 4-bit residuals, low nibble first. Sample = clamp16(((c1 * s1 + c2 * s2 + 32) >> 6) + (q << shift)) with the
 * filter's (c1, c2) from erk_filt (the table is shared with the engine, src/erk_codec.h). The encoder tries every
 * filter and shift per block, closed loop, and keeps the one with the least squared error. Blocks are independent
 * except for the two history samples, so a loop start on a block boundary needs only (s1, s2) stored. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "../app/dsp/erk_codec.h"

static int16_t *load(const char *p, long *n)
{
    FILE *f = fopen(p, "rb");
    int16_t *x;
    if (!f) {
        perror(p);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    *n = ftell(f) / 2;
    fseek(f, 0, SEEK_SET);
    x = malloc((*n + 64) * 2);
    if (fread(x, 2, *n, f) != (size_t)*n)
        exit(1);
    fclose(f);
    return x;
}

static void save(const char *p, const void *d, long bytes)
{
    FILE *f = fopen(p, "wb");
    if (!f || fwrite(d, 1, bytes, f) != (size_t)bytes) {
        perror(p);
        exit(1);
    }
    fclose(f);
}

static inline int clamp16(int v) { return v < -32768 ? -32768 : v > 32767 ? 32767 : v; }

/* one block, closed loop: returns the squared error, fills nib[32] and the end history */
static double try_block(const int16_t *x, int n, int f, int sh, int *s1, int *s2, int8_t *nib)
{
    int a = *s1, b = *s2, i;
    double e = 0;
    for (i = 0; i < ERK_BLK; i++) {
        int t = i < n ? x[i] : 0;
        int pred = (erk_filt[f][0] * a + erk_filt[f][1] * b + 32) >> 6;
        int r = t - pred, q, s;
        q = sh ? (r + (1 << (sh - 1))) >> sh : r;
        if (q < -8) q = -8;
        if (q > 7) q = 7;
        s = clamp16(pred + q * (1 << sh));
        nib[i] = (int8_t)q;
        if (i < n)
            e += (double)(t - s) * (t - s);
        b = a;
        a = s;
    }
    *s1 = a;
    *s2 = b;
    return e;
}

static int enc(int argc, char **argv)
{
    long n, nb, k;
    int16_t *x = load(argv[2], &n), *y;
    int nf = argc > 5 ? atoi(argv[5]) : ERK_NFILT, s1 = 0, s2 = 0;
    uint8_t *o;
    nb = (n + ERK_BLK - 1) / ERK_BLK;
    o = calloc(nb, ERK_BLKB);
    y = calloc(nb * ERK_BLK, 2);
    for (k = 0; k < nb; k++) {
        int m = (int)(n - k * ERK_BLK < ERK_BLK ? n - k * ERK_BLK : ERK_BLK), f, sh, bf = 0, bs = 0, i;
        double best = 1e300;
        int8_t nib[ERK_BLK];
        for (f = 0; f < nf; f++)
            for (sh = 0; sh <= 12; sh++) {
                int a = s1, b = s2;
                double e = try_block(x + k * ERK_BLK, m, f, sh, &a, &b, nib);
                if (e < best) {
                    best = e;
                    bf = f;
                    bs = sh;
                }
            }
        try_block(x + k * ERK_BLK, m, bf, bs, &s1, &s2, nib);
        o[k * ERK_BLKB] = (uint8_t)(bf << 4 | bs);
        for (i = 0; i < ERK_BLK; i += 2)
            o[k * ERK_BLKB + 1 + i / 2] = (uint8_t)((nib[i] & 15) | (nib[i + 1] & 15) << 4);
    }
    /* decode with the engine's own decoder (the same header), so DEC is bit exact */
    {
        erk_dec d;
        erk_dec_start(&d, o, 0, 0);
        for (k = 0; k < nb * ERK_BLK; k++)
            y[k] = (int16_t)erk_dec_next(&d);
    }
    save(argv[3], o, nb * ERK_BLKB);
    save(argv[4], y, n * 2);
    return 0;
}

static const int ima_step[89] = {7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50,
    55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494,
    544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
    3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
    18500, 20350, 22385, 24623, 27086, 29794, 32767};
static const int ima_idx[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

static int ima(int argc, char **argv)
{
    long n, i;
    int16_t *x = load(argv[2], &n), *y = malloc(n * 2);
    int pred = 0, idx = 0;
    (void)argc;
    for (i = 0; i < n; i++) {
        int st = ima_step[idx], d = x[i] - pred, c = 0, dq = st >> 3;
        if (d < 0) {
            c = 8;
            d = -d;
        }
        if (d >= st) { c |= 4; d -= st; dq += st; }
        if (d >= st >> 1) { c |= 2; d -= st >> 1; dq += st >> 1; }
        if (d >= st >> 2) { c |= 1; dq += st >> 2; }
        pred = clamp16(c & 8 ? pred - dq : pred + dq);
        idx += ima_idx[c];
        idx = idx < 0 ? 0 : idx > 88 ? 88 : idx;
        y[i] = (int16_t)pred;
    }
    save(argv[3], y, n * 2);
    return 0;
}

static int snr(int argc, char **argv)
{
    long n, m, i, fr = 441, segs = 0;
    int16_t *x = load(argv[2], &n), *y = load(argv[3], &m);
    double se = 0, ss = 0, seg = 0;
    (void)argc;
    if (m < n)
        n = m;
    for (i = 0; i < n; i++) {
        double d = (double)x[i] - y[i];
        se += d * d;
        ss += (double)x[i] * x[i];
    }
    for (i = 0; i + fr <= n; i += fr) {
        double a = 0, b = 0;
        long j;
        for (j = i; j < i + fr; j++) {
            double d = (double)x[j] - y[j];
            a += (double)x[j] * x[j];
            b += d * d;
        }
        if (a > fr * 100.0) {        /* frames above -50 dBFS-ish only */
            seg += 10 * log10((a + 1) / (b + 1));
            segs++;
        }
    }
    printf("%.2f %.2f\n", 10 * log10((ss + 1) / (se + 1)), segs ? seg / segs : 0.0);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 5 && !strcmp(argv[1], "enc"))
        return enc(argc, argv);
    if (argc >= 4 && !strcmp(argv[1], "ima"))
        return ima(argc, argv);
    if (argc >= 4 && !strcmp(argv[1], "snr"))
        return snr(argc, argv);
    fprintf(stderr, "usage: erkcodec enc IN.s16 OUT.bin DEC.s16 [NFILT] | ima IN DEC | snr REF DEC\n");
    return 2;
}
