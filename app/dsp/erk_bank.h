/* SPDX-License-Identifier: MPL-2.0
 * Erik37: Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* The sound bank: one flash image written by tools/build_bank.py (little endian, 4-byte aligned).
 *   erk_bank_hdr | erk_key[88] | erk_vel[128] | erk_smp[nzones * nlayers] | erk_smp[nrel] | sample data
 * Samples are block ADPCM (erk_codec.h); offsets are bytes into the data area. */
#ifndef ERK_BANK_H
#define ERK_BANK_H
#include <stdint.h>

#define ERK_MAGIC 0x314b5245u      /* "ERK1" */
#define ERK_KEYS 88
#define ERK_KEY0 21                /* A0 */
#define ERK_MAXL 3
#define ERK_MAXREL 4

typedef struct {
    uint32_t off;         /* byte offset of the first block */
    uint32_t len;         /* samples */
    uint32_t loop;        /* loop start (a multiple of 32), samples */
    uint32_t loop_len;    /* 0: one-shot */
    int16_t h1, h2;       /* decoder history at the loop start */
    uint16_t rate;        /* Hz */
    uint16_t gain;        /* playback gain, Q12 (undoes the peak normalization) */
} erk_smp;

typedef struct {
    uint32_t m1, m2;      /* envelope multipliers a block (32 samples), Q30: before / after the knee */
    uint32_t mr1, mr2;    /* damper release: the first ERK_REL1 blocks / after */
    uint16_t knee;        /* blocks */
    uint16_t inc;         /* source samples an output sample, 0.16 (< 1: upsampling only) */
    uint16_t pan_l, pan_r;/* Q15, constant power */
    uint16_t a_floor;     /* the brightness filter's lowest coefficient (Q14), from the pitch */
    uint16_t dark;        /* filter darkening a block, Q16 (fraction of the way to a_floor kept) */
    uint8_t zone, flags;  /* flags: ERK_NODAMP (no damper: the top keys ring on) */
    uint8_t rel, pad;     /* release sample */
} erk_key;
#define ERK_NODAMP 1

typedef struct {
    uint16_t amp;         /* Q15 velocity -> amplitude */
    uint16_t a;           /* Q14 brightness filter coefficient */
    uint16_t w;           /* Q15 weight of layer l1 (0: l0 only) */
    uint8_t l0, l1;
} erk_vel;

typedef struct {
    uint32_t magic;
    uint16_t nzones, nlayers, nrel, pad;
    uint32_t data_size;
    uint16_t rel_gain;    /* Q15 */
    uint16_t pad2;
} erk_bank_hdr;

typedef struct {
    const erk_bank_hdr *hdr;
    const erk_key *key;
    const erk_vel *vel;
    const erk_smp *smp, *rel;
    const uint8_t *data;
} erk_bank;

static inline int erk_bank_open(erk_bank *b, const void *img)
{
    const uint8_t *p = (const uint8_t *)img;
    b->hdr = (const erk_bank_hdr *)p;
    if (b->hdr->magic != ERK_MAGIC)
        return -1;
    p += sizeof(erk_bank_hdr);
    b->key = (const erk_key *)p;
    p += sizeof(erk_key) * ERK_KEYS;
    b->vel = (const erk_vel *)p;
    p += sizeof(erk_vel) * 128;
    b->smp = (const erk_smp *)p;
    p += sizeof(erk_smp) * b->hdr->nzones * b->hdr->nlayers;
    b->rel = (const erk_smp *)p;
    p += sizeof(erk_smp) * b->hdr->nrel;
    b->data = p;
    return 0;
}
#endif
