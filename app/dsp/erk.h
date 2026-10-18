/* SPDX-License-Identifier: MPL-2.0
 * Erik37: Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* The piano engine: fixed point, 44.1 kHz stereo, blocks of ERK_BLOCK samples. Every call runs in the audio
 * context (the app feeds it from app_render); the erk_set_* setters may come from the main loop (word writes). */
#ifndef ERK_H
#define ERK_H
#include <stdint.h>
#include "erk_bank.h"
#ifndef ERK_MODAL
#define ERK_MODAL 0            /* the hybrid and modelled modes: the host prototype only */
#endif
#if ERK_MODAL
#include "erk_modal.h"
#endif

#define ERK_FS 44100
#define ERK_BLOCK 32
#ifndef ERK_VOICES
#define ERK_VOICES 20          /* notes that may sound at once (tools/target_budget.py: the CPU) */
#endif
#define ERK_DYING 4            /* extra slots for stolen / re-struck notes fading out */
#ifndef ERK_XF_MAX
#define ERK_XF_MAX 6           /* notes crossfading two velocity layers at once (the attack's first 0.3 s) */
#endif
#define ERK_RELV 4             /* release-noise voices */
#define ERK_NV (ERK_VOICES + ERK_DYING + ERK_RELV)

enum { ERK_REV_OFF, ERK_REV_CHAMBER, ERK_REV_HALL, ERK_REV_CHURCH };
enum { ERK_SAMPLED, ERK_HYBRID, ERK_MODELLED };   /* sample streams only / sampled attack + modal sustain / modal */
enum { ERK_CONCERT, ERK_STUDIO, ERK_VINTAGE, ERK_JAZZ, ERK_NTONES };

typedef struct {
    int reverb;                /* ERK_REV_* */
    int rev_mix;               /* Q15 */
    int res_level;             /* Q15, sympathetic resonance (pedal down) */
    int master;                /* Q15 */
    int rel_level;             /* Q15, release noise */
    int mode;                  /* ERK_SAMPLED, ERK_HYBRID, ERK_MODELLED */
#if ERK_MODAL
    const erkm_bank *modal;    /* the partial tables (hybrid, modelled) */
#else
    const void *modal;
#endif
} erk_cfg;

void erk_init(const erk_bank *b, const erk_cfg *cfg);
void erk_note_on(int key, int vel);           /* MIDI key, velocity 1..127 */
void erk_note_off(int key);
void erk_sustain(int val);                    /* 0..127: half pedal in between */
void erk_sostenuto(int on);
void erk_soft(int val);                       /* una corda, 0..127 */
void erk_all_off(void);                       /* every note released (dampers) */
void erk_panic(void);                         /* silence now */
void erk_click(int accent);                   /* the metronome's tick */
void erk_shed(void);                          /* overload: drop the quietest released voices */
void erk_render(int32_t *lr, int frames);     /* interleaved stereo, 24 bit (+-2^23), frames a multiple of ERK_BLOCK */
void erk_render_gain(int32_t *lr, int frames, int32_t master_q12);   /* the same at the platform's MASTER gain */
int erk_active(void);

/* sound settings (any time; the reverb's change clears its tail) */
void erk_set_room(int type, int size_q15, int amount_q15);   /* size: the decay, 0 short .. 1 long */
void erk_set_width(int width_q15);
void erk_set_tone(int tone, int brilliance_q15, int una_q15);   /* brilliance 0..1 (0.5 neutral); una corda depth */
void erk_set_resonance(int level_q15);
void erk_set_rel_level(int level_q15);
void erk_set_master(int q15);
float erk_room_seconds(int type, int size_q15);   /* the decay the screen shows */

/* what the screen shows (written by the render once a block) */
extern volatile uint16_t erk_key_lvl[ERK_KEYS];    /* each key's sound, Q15 */
extern volatile uint32_t erk_sost_keys[3];         /* keys held up by the sostenuto (bit k - ERK_KEY0) */
extern volatile int32_t erk_sus_q15, erk_soft_q15;

/* counters for the cost model (host only) */
typedef struct {
    uint64_t voice_blocks, stream_blocks, partial_blocks, blocks;
} erk_stats;
extern erk_stats erk_st;
void erk_ram(int *voices, int *modal_part, int *res, int *rev);
#endif
