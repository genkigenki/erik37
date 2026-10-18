/* SPDX-License-Identifier: MPL-2.0
 * Erik37: Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* Host renderer: plays an event list through the engine into a WAV.
 *   render BANK.bin EVENTS.txt OUT.wav [-r off|chamber|hall] [-m MIX] [-s RES] [-g MASTER] [-n REL] [-t TAIL_S]
 *        [-M hybrid|model MODAL.bin]
 *   render BANK.bin --cost VOICES WARM SECS VEL [opts]   the cost run (tools/cpu_budget.py, tools/icount)
 * Events: "TIME on KEY VEL", "TIME off KEY", "TIME sus VAL", "TIME soft VAL" (seconds, MIDI numbers). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "../app/dsp/erk.h"

typedef struct {
    double t;
    char ty[8];
    int a, b;
} ev;

static void *slurp(const char *p, long *n)
{
    FILE *f = fopen(p, "rb");
    void *d;
    if (!f) {
        perror(p);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    *n = ftell(f);
    fseek(f, 0, SEEK_SET);
    d = malloc(*n + 16);
    if (fread(d, 1, *n, f) != (size_t)*n)
        exit(1);
    fclose(f);
    return d;
}

static void wr32(FILE *f, unsigned v) { fputc(v & 255, f); fputc(v >> 8 & 255, f); fputc(v >> 16 & 255, f); fputc(v >> 24, f); }
static void wr16(FILE *f, unsigned v) { fputc(v & 255, f); fputc(v >> 8 & 255, f); }

static void wav(const char *p, const int32_t *x, long frames)
{
    FILE *f = fopen(p, "wb");
    if (!f) {
        perror(p);
        exit(1);
    }
    fwrite("RIFF", 1, 4, f);
    wr32(f, 36 + frames * 6);
    fwrite("WAVEfmt ", 1, 8, f);
    wr32(f, 16);
    wr16(f, 1);
    wr16(f, 2);
    wr32(f, ERK_FS);
    wr32(f, ERK_FS * 6);
    wr16(f, 6);
    wr16(f, 24);
    fwrite("data", 1, 4, f);
    wr32(f, frames * 6);
    for (long i = 0; i < frames * 2; i++) {
        fputc(x[i] & 255, f);
        fputc(x[i] >> 8 & 255, f);
        fputc(x[i] >> 16 & 255, f);
    }
    fclose(f);
}

/* VOICES held notes (pedal down) spread over the keyboard at velocity VEL; WARM seconds unmeasured, then SECS
 * between raise(SIGUSR1) and raise(SIGUSR2) */
static int cost(int voices, double warm, double secs, int vel)
{
    long w = (long)(warm * ERK_FS) / ERK_BLOCK * ERK_BLOCK, frames = (long)(secs * ERK_FS) / ERK_BLOCK * ERK_BLOCK, i;
    int32_t buf[ERK_BLOCK * 2];
    int k;
    erk_sustain(127);
    for (k = 0; k < voices; k++)
        erk_note_on(28 + k * 72 / (voices > 1 ? voices - 1 : 1), vel);
    for (i = 0; i < w; i += ERK_BLOCK)
        erk_render(buf, ERK_BLOCK);
    memset(&erk_st, 0, sizeof erk_st);
#ifdef __linux__
    raise(SIGUSR1);
#endif
    for (i = 0; i < frames; i += ERK_BLOCK)
        erk_render(buf, ERK_BLOCK);
#ifdef __linux__
    raise(SIGUSR2);
#endif
    printf("frames %ld active %d voice_blocks %llu stream_blocks %llu partial_blocks %llu\n", frames, erk_active(),
           (unsigned long long)erk_st.voice_blocks, (unsigned long long)erk_st.stream_blocks,
           (unsigned long long)erk_st.partial_blocks);
    return 0;
}

int main(int argc, char **argv)
{
    long n, ne = 0, frames, f, i, clip = 0;
    void *img;
    erk_bank b;
    erk_cfg c = {ERK_REV_HALL, 7500, 900, 16000, 9000, ERK_SAMPLED, 0};
    double tail = 4.0, tend = 0;
    ev *e;
    int32_t *out;
    int peak = 0, maxact = 0;
    char line[256];
    FILE *fe;
    if (argc == 2 && !strcmp(argv[1], "--ram")) {
        int v, m, r, w;
        erk_ram(&v, &m, &r, &w);
        printf("ram: voices %d (of which modal state %d), resonance %d, reverb %d, total %d (sampled only %d)\n",
               v, m, r, w, v + r + w, v - m + r + w);
        return 0;
    }
    if (argc < 3)
        return 2;
    img = slurp(argv[1], &n);
    if (erk_bank_open(&b, img)) {
        fprintf(stderr, "%s: not a bank\n", argv[1]);
        return 1;
    }
    for (i = 3; i < argc - 1; i++) {
        if (!strcmp(argv[2], "--cost") && i < 7)
            continue;
        if (!strcmp(argv[i], "-r"))
            c.reverb = !strcmp(argv[i + 1], "off") ? ERK_REV_OFF : !strcmp(argv[i + 1], "chamber") ? ERK_REV_CHAMBER : ERK_REV_HALL;
        else if (!strcmp(argv[i], "-m"))
            c.rev_mix = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "-s"))
            c.res_level = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "-g"))
            c.master = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "-n"))
            c.rel_level = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "-t"))
            tail = atof(argv[i + 1]);
        else if (!strcmp(argv[i], "-M") && i + 2 < argc) {    /* -M hybrid|model MODAL.bin */
            static erkm_bank mb;
            long mn;
            if (erkm_open(&mb, slurp(argv[i + 2], &mn))) {
                fprintf(stderr, "%s: not a modal bank\n", argv[i + 2]);
                return 1;
            }
            c.mode = !strcmp(argv[i + 1], "model") ? ERK_MODELLED : ERK_HYBRID;
            c.modal = &mb;
        }
    }
    erk_init(&b, &c);
    if (!strcmp(argv[2], "--cost"))
        return cost(atoi(argv[3]), atof(argv[4]), atof(argv[5]), atoi(argv[6]));
    fe = fopen(argv[2], "r");
    if (!fe) {
        perror(argv[2]);
        return 1;
    }
    e = malloc(sizeof(ev) * 100000);
    while (fgets(line, sizeof line, fe) && ne < 100000) {
        ev *q = &e[ne];
        q->a = q->b = 0;
        if (line[0] == '#' || sscanf(line, "%lf %7s %d %d", &q->t, q->ty, &q->a, &q->b) < 3)
            continue;
        if (q->t > tend)
            tend = q->t;
        ne++;
    }
    fclose(fe);
    frames = (long)((tend + tail) * ERK_FS) / ERK_BLOCK * ERK_BLOCK;
    out = malloc(frames * 8);
    for (f = 0, i = 0; f < frames; f += ERK_BLOCK) {
        int a;
        while (i < ne && e[i].t * ERK_FS < f + ERK_BLOCK) {
            ev *q = &e[i++];
            if (!strcmp(q->ty, "on"))
                erk_note_on(q->a, q->b);
            else if (!strcmp(q->ty, "off"))
                erk_note_off(q->a);
            else if (!strcmp(q->ty, "sus"))
                erk_sustain(q->a);
            else if (!strcmp(q->ty, "soft"))
                erk_soft(q->a);
        }
        erk_render(out + 2 * f, ERK_BLOCK);
        a = erk_active();
        if (a > maxact)
            maxact = a;
    }
    for (i = 0; i < frames * 2; i++) {
        int v = abs(out[i]);
        if (v > peak)
            peak = v;
        if (v >= 8388607)
            clip++;
    }
    wav(argv[3], out, frames);
    printf("%s: %.1f s, peak %d, clipped %ld, max voices %d\n", argv[3], frames / (double)ERK_FS, peak, clip, maxact);
    return 0;
}
