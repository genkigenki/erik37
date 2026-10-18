/* SPDX-License-Identifier: MPL-2.0
 * Erik37: Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* The engine on the host, with the firmware's own bank (app/assets/erik37.bank):
 *   engine_test BANK [OUT_DIR]     (OUT_DIR: the renders as WAV, for listening)
 * The checks: sound comes out and nothing clips; a held note sustains, a released one is damped; the pedal holds,
 * half pedal lets go faster; the sostenuto holds only the notes it caught; una corda is softer and darker; the rooms
 * (dry has no tail, the church the longest); the voices are capped and the stealing is click-free; the tones and the
 * brilliance change the colour; every voice ends. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../dsp/erk.h"

static int fails;
static int32_t buf[2 * 44100 * 12];
static const char *outdir;

static void check(int ok, const char *what)
{
    printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
    fails += !ok;
}

static void render(int32_t *o, int frames) { erk_render(o, frames / ERK_BLOCK * ERK_BLOCK); }

static double rms_db(const int32_t *o, int a, int b)   /* frames [a, b), both channels, dBFS of the 24-bit scale */
{
    double s = 0;
    int i;
    for (i = 2 * a; i < 2 * b; i++)
        s += (double)o[i] * o[i];
    return 10 * log10(s / (2.0 * (b - a)) / (8388608.0 * 8388608.0) + 1e-20);
}

static int32_t peak(const int32_t *o, int n)
{
    int32_t p = 0;
    int i;
    for (i = 0; i < 2 * n; i++)
        p = abs(o[i]) > p ? abs(o[i]) : p;
    return p;
}

/* the brightness: the energy above 3 kHz against all (a first difference stands in for a high-pass) */
static double bright(const int32_t *o, int a, int b)
{
    double hi = 0, all = 0;
    int i;
    for (i = a + 1; i < b; i++) {
        double d = (double)o[2 * i] - o[2 * i - 2];
        hi += d * d;
        all += (double)o[2 * i] * o[2 * i];
    }
    return hi / (all + 1);
}

/* clicks: a step far above the local signal (the second difference against its 10 ms neighbourhood) */
static int clicks(const int32_t *o, int n)
{
    int i, c = 0;
    for (i = 441; i < n - 441; i += 1) {
        double d2 = fabs((double)o[2 * i + 2] - 2.0 * o[2 * i] + o[2 * i - 2]), loc = 0;
        int j;
        if (d2 < 2e5)
            continue;
        for (j = i - 220; j < i + 220; j += 20)
            loc += fabs((double)o[2 * j + 2] - 2.0 * o[2 * j] + o[2 * j - 2]);
        loc /= 22;
        if (d2 > 30 * (loc + 1e3))
            c++;
    }
    return c;
}

static void wav(const char *name, const int32_t *o, int frames)
{
    char p[512];
    FILE *f;
    int i;
    uint32_t n;
    if (!outdir)
        return;
    snprintf(p, sizeof p, "%s/%s.wav", outdir, name);
    f = fopen(p, "wb");
    if (!f)
        return;
    n = (uint32_t)frames * 4u;
    fwrite("RIFF", 1, 4, f);
    { uint32_t v = 36 + n; fwrite(&v, 4, 1, f); }
    fwrite("WAVEfmt ", 1, 8, f);
    { uint32_t v = 16; fwrite(&v, 4, 1, f); }
    { uint16_t v[2] = {1, 2}; fwrite(v, 2, 2, f); }
    { uint32_t v[2] = {44100, 44100 * 4}; fwrite(v, 4, 2, f); }
    { uint16_t v[2] = {4, 16}; fwrite(v, 2, 2, f); }
    fwrite("data", 1, 4, f);
    fwrite(&n, 4, 1, f);
    for (i = 0; i < 2 * frames; i++) {
        int16_t s = (int16_t)(o[i] >> 8);
        fwrite(&s, 2, 1, f);
    }
    fclose(f);
}

static erk_bank bank;
static const erk_cfg CFG = {ERK_REV_HALL, 7600, 1300, 16000, 9000, ERK_SAMPLED, 0};

static void fresh(int room)
{
    erk_init(&bank, &CFG);
    erk_set_tone(ERK_CONCERT, 16384, 16384);
    erk_set_room(room, 16384, 7600);
    erk_set_width(26000);
    erk_set_resonance(1300);
}

#define S(t) ((int)((t) * 44100) / ERK_BLOCK * ERK_BLOCK)

int main(int argc, char **argv)
{
    FILE *f;
    long n;
    static uint8_t img[1 << 20];
    double a, b, c;
    int i;
    if (argc < 2)
        return 2;
    outdir = argc > 2 ? argv[2] : 0;
    f = fopen(argv[1], "rb");
    if (!f) {
        perror(argv[1]);
        return 2;
    }
    n = (long)fread(img, 1, sizeof img, f);
    fclose(f);
    check(n > 100000 && erk_bank_open(&bank, img) == 0, "the bank opens");
    printf("  bank: %ld bytes, %d zones x %d layers, %d release noises\n", n, bank.hdr->nzones, bank.hdr->nlayers,
           bank.hdr->nrel);

    /* a held note sounds and decays; released, the damper takes it */
    fresh(ERK_REV_OFF);
    erk_note_on(60, 100);
    render(buf, S(2));
    a = rms_db(buf, S(0.05), S(0.3));
    b = rms_db(buf, S(1.6), S(2.0));
    check(a > -40 && a < -6, "a C4 forte sounds (-40 .. -6 dBFS)");
    check(b < a - 3 && b > a - 40, "held, it decays (3 .. 40 dB in 2 s)");
    erk_note_off(60);
    render(buf, S(1.5));
    c = rms_db(buf, S(1.0), S(1.5));
    check(c < b - 30, "released, the damper stops it (30 dB down in 1 s)");
    render(buf, S(1));
    check(erk_active() == 0, "every voice has ended");
    wav("held_released", buf, S(1));

    /* the sustain pedal holds; lifted, the notes stop */
    fresh(ERK_REV_OFF);
    erk_sustain(127);
    erk_note_on(48, 90);
    render(buf, S(0.3));
    erk_note_off(48);
    render(buf, S(1.5));
    a = rms_db(buf, S(1.2), S(1.5));
    check(a > -60, "the pedal holds a released note");
    erk_sustain(0);
    render(buf, S(1.5));
    b = rms_db(buf, S(1.2), S(1.5));
    check(b < a - 25, "the pedal lifted: the damper");

    /* half pedal: the held note lets go faster than with the pedal down */
    fresh(ERK_REV_OFF);
    erk_sustain(127);
    erk_note_on(55, 90);
    render(buf, S(0.2));
    erk_note_off(55);
    render(buf, S(2));
    a = rms_db(buf, S(1.6), S(2.0));
    fresh(ERK_REV_OFF);
    erk_sustain(60);
    erk_note_on(55, 90);
    render(buf, S(0.2));
    erk_note_off(55);
    render(buf, S(2));
    b = rms_db(buf, S(1.6), S(2.0));
    check(b < a - 1.5, "half pedal holds less than a full pedal");

    /* the sostenuto: the note held when it went down rings; a note played after it does not */
    fresh(ERK_REV_OFF);
    erk_note_on(50, 90);
    render(buf, S(0.1));
    erk_sostenuto(1);
    erk_note_off(50);
    erk_note_on(74, 90);
    render(buf, S(0.1));
    erk_note_off(74);
    render(buf, S(1.0));
    check(erk_key_lvl[50 - ERK_KEY0] > 300, "sostenuto: the caught note rings on");
    check(erk_key_lvl[74 - ERK_KEY0] < 30, "sostenuto: a later note is damped");
    erk_sostenuto(0);
    render(buf, S(1.0));
    check(erk_key_lvl[50 - ERK_KEY0] < 60, "sostenuto lifted: the caught note is damped too");

    /* una corda: softer and darker */
    fresh(ERK_REV_OFF);
    erk_note_on(64, 100);
    render(buf, S(0.5));
    a = rms_db(buf, S(0.02), S(0.4));
    c = bright(buf, S(0.02), S(0.4));
    fresh(ERK_REV_OFF);
    erk_soft(127);
    erk_note_on(64, 100);
    render(buf, S(0.5));
    b = rms_db(buf, S(0.02), S(0.4));
    check(b < a - 1.0, "una corda is softer");
    check(bright(buf, S(0.02), S(0.4)) < c * 0.9, "una corda is darker");

    /* the rooms: a staccato chord, then what is left after 0.6 s */
    {
        double tail[4];
        int r;
        for (r = 0; r < 4; r++) {
            fresh(r);
            render(buf, S(0.05));
            erk_note_on(60, 110);
            erk_note_on(64, 110);
            erk_note_on(67, 110);
            render(buf, S(0.1));
            erk_note_off(60);
            erk_note_off(64);
            erk_note_off(67);
            render(buf, S(2.5));
            tail[r] = rms_db(buf, S(0.7), S(1.2));
        }
        check(tail[0] < tail[1] - 6, "dry: no room tail");
        check(tail[1] < tail[2] && tail[2] < tail[3], "chamber < hall < church");
        wav("church_tail", buf, S(2.5));
    }

    /* the tones and the brilliance colour the sound */
    {
        double br[4];
        int t;
        for (t = 0; t < 4; t++) {
            fresh(ERK_REV_OFF);
            erk_set_tone(t, 16384, 16384);
            erk_note_on(72, 96);
            render(buf, S(0.4));
            br[t] = bright(buf, S(0.02), S(0.35));
        }
        check(br[ERK_STUDIO] > br[ERK_CONCERT] && br[ERK_VINTAGE] < br[ERK_CONCERT], "studio brighter, vintage darker");
        fresh(ERK_REV_OFF);
        erk_set_tone(ERK_CONCERT, 32767, 16384);
        erk_note_on(72, 96);
        render(buf, S(0.4));
        check(bright(buf, S(0.02), S(0.35)) > br[ERK_CONCERT], "more brilliance: brighter");
    }

    /* many notes: the voices are capped, the stealing is click-free, nothing clips */
    fresh(ERK_REV_HALL);
    erk_sustain(127);
    for (i = 0; i < 48; i++) {
        erk_note_on(30 + i * 3 % 70, 70 + (i * 7) % 57);
        render(buf + 2 * S(0.06) * (i % 100), S(0.06));
    }
    check(erk_active() <= ERK_NV, "the voices stay within the cap");
    {
        int k = 0;
        int32_t p;
        for (i = 0; i < 48; i++) {
            erk_note_on(28 + (i * 5) % 75, 40 + (i * 11) % 87);
            render(buf + 2 * S(0.06) * i, S(0.06));
        }
        p = peak(buf, S(0.06) * 48);
        k = clicks(buf, S(0.06) * 48);
        printf("  48 notes with the pedal: peak %.1f dBFS, %d clicks, %d voices\n", 20 * log10(p / 8388608.0 + 1e-9), k,
               erk_active());
        check(p < 8388607, "nothing clips (48 notes, pedal down, forte)");
        check(k == 0, "the stealing is click-free");
        wav("stress", buf, S(0.06) * 48);
    }
    erk_sustain(0);
    erk_all_off();
    render(buf, S(8));
    check(erk_active() == 0, "after the pedal and 8 s, every voice has ended (the undamped treble too)");

    printf("%s\n", fails ? "ENGINE TEST FAILED" : "engine test passed");
    return fails != 0;
}
