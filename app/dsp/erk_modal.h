/* SPDX-License-Identifier: MPL-2.0
 * Erik37: Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* Modal sustain: a voice as up to ERKM_NP decaying partials (table sines, stretched tuning, a two-slope decay and
 * a two-string beat each). The tables (tools/modal_fit.py), little endian:
 *   erkm_hdr | uint32 f0inc[88] | uint8 zone[88] | erkm_partial[nzones][nlayers][np] */
#ifndef ERK_MODAL_H
#define ERK_MODAL_H
#include <stdint.h>

#define ERKM_MAGIC 0x4d4b5245u     /* "ERKM" */
#define ERKM_NP 24

typedef struct {
    uint32_t magic;
    uint16_t nzones, nlayers, np, flags;   /* flags 1: synthesized (the modelled mode: hammer noise attack) */
} erkm_hdr;

typedef struct {
    uint32_t ratio;        /* f / f0(key), Q20 */
    int16_t amp, depth;    /* Q15: amplitude (full scale 1), beat depth */
    uint32_t d1, d2;       /* decay a block before / after the knee, Q30 */
    uint16_t knee;         /* blocks */
    uint16_t beat;         /* Hz, Q8 */
} erkm_partial;

typedef struct {
    const erkm_hdr *hdr;
    const uint32_t *f0inc;
    const uint8_t *zone;
    const erkm_partial *p;
} erkm_bank;

typedef struct {
    int np;
    uint32_t blocks;
    uint32_t ph[ERKM_NP], inc[ERKM_NP];
    int32_t a[ERKM_NP];            /* the synthesis amplitude, Q15 << 16 */
    int32_t A[ERKM_NP];            /* the partial's amplitude, Q15 */
    uint32_t e[ERKM_NP], d1[ERKM_NP], d2[ERKM_NP];     /* decay state Q30, multipliers */
    uint16_t knee[ERKM_NP];
    int16_t depth[ERKM_NP];
    int32_t bc[ERKM_NP], bs[ERKM_NP], rc[ERKM_NP], rs[ERKM_NP];   /* beat rotator (Q30): state, step */
    uint32_t noise, nlfsr;         /* modelled attack: hammer noise envelope (Q30), LFSR */
    int32_t nlp, ngain, nfc;
} erkm_voice;

int erkm_open(erkm_bank *b, const void *img);
void erkm_init(void);
void erkm_start(erkm_voice *m, const erkm_bank *b, int key, int l0, int l1, int w, int vel, int soft);
/* adds one block (ERK_BLOCK samples) into buf, scaled by g (Q15); returns the summed partial level (Q15) */
int32_t erkm_block(erkm_voice *m, int32_t *buf, int32_t g, int run);
#endif
