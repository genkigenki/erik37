/* SPDX-License-Identifier: MPL-2.0
 * Erik37: Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* Drawing, one to one with the mockups (design/erik37_gfx.py): an RGB888 work frame (the static background of the
 * page restored row by row from its palette image, then what moves drawn over it), 16-level coverage from 4 x 4
 * supersampled shapes, light added, 4-bit anti-aliased type, and the RGB565 output with a 4 x 4 ordered (Bayer)
 * dither, sent in bands of rows. Part of the app's unity build (app.c). */

typedef struct {
    uint16_t ch;
    int8_t xo, yo;
    uint8_t w, h;
    uint16_t adv;                   /* 1/16 px */
    uint32_t off;
} erk_glyph;
typedef struct {
    uint8_t px;
    uint16_t n;
    const erk_glyph *g;
    const uint8_t *d;
} erk_font;
typedef struct {
    const uint8_t *pal;
    const uint32_t *off;
    const uint8_t *lz;
    uint32_t nblk;
} erk_art;
#include "erk_fonts.h"
#include "erk_art.h"

typedef struct {
    uint8_t r, g, b;
} rgb_t;
#define C3(r, g, b) ((rgb_t){r, g, b})
/* the palette (design/DESIGN.md) */
#define LACQ0 C3(5, 5, 7)
#define LACQ1 C3(13, 13, 16)
#define GOLD C3(201, 164, 92)
#define GOLD_HI C3(246, 218, 156)
#define GOLD_DIM C3(112, 90, 52)
#define GOLD_DEEP C3(64, 50, 28)
#define IVORY C3(238, 228, 206)
#define IVORY_DIM C3(150, 142, 126)
#define GREY C3(78, 72, 64)
#define FELT C3(58, 30, 32)
#define FELT_HI C3(96, 58, 56)
#define WOOD C3(54, 38, 26)
#define COPPER C3(160, 104, 64)
#define STEEL C3(150, 150, 156)
#define BLACKISH C3(8, 8, 10)

#define SW 240
#define SH 240
static uint8_t W[SW * SH * 3] PLAT_POOL;     /* the frame being drawn, RGB888 */
static uint8_t BGI[SW * SH] PLAT_POOL;       /* the page's static background: palette indices */
static uint8_t MASK[SW * SH] PLAT_POOL;      /* coverage 0..255 */
static uint16_t BAND[SW * 16] PLAT_POOL;     /* RGB565 rows on their way to the panel */
static const uint8_t *bgp;                   /* the background's palette */
static uint8_t Q5[16][256], Q6[16][256];      /* the dither: floor(v m / 255 + bayer / 16) */
static int dirty0 = SH, dirty1 = 0;          /* the rows to send */
static const uint8_t BAYER4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

static void draw_init(void)
{
    uint32_t b, v;
    for (b = 0; b < 16u; b++)
        for (v = 0; v < 256u; v++) {
            Q5[b][v] = (uint8_t)((v * 31u * 16u + b * 255u) / 4080u > 31u ? 31u : (v * 31u * 16u + b * 255u) / 4080u);
            Q6[b][v] = (uint8_t)((v * 63u * 16u + b * 255u) / 4080u > 63u ? 63u : (v * 63u * 16u + b * 255u) / 4080u);
        }
}

/* ---------------------------------------------------------------- backgrounds */

/* LZ4 block decoding (tools/gen_assets.py: base/tools/lz4blk.py's blocks) */
static uint32_t lz4_dec(const uint8_t *s, uint8_t *d, uint32_t dmax)
{
    uint32_t o = 0;
    for (;;) {
        uint32_t tok = *s++, n = tok >> 4, m, off;
        if (n == 15u) {
            uint32_t b;
            do {
                b = *s++;
                n += b;
            } while (b == 255u);
        }
        while (n-- && o < dmax)
            d[o++] = *s++;
        if (o >= dmax)
            return o;
        off = (uint32_t)s[0] | (uint32_t)s[1] << 8;
        s += 2;
        m = (tok & 15u) + 4u;
        if ((tok & 15u) == 15u) {
            uint32_t b;
            do {
                b = *s++;
                m += b;
            } while (b == 255u);
        }
        if (!off || off > o)
            return o;
        while (m-- && o < dmax) {
            d[o] = d[o - off];
            o++;
        }
    }
}

static void art_load(const erk_art *a)
{
    uint32_t i;
    for (i = 0; i < a->nblk; i++)
        lz4_dec(a->lz + a->off[i], BGI + i * 4096u, (i + 1u) * 4096u <= SW * SH ? 4096u : SW * SH - i * 4096u);
    bgp = a->pal;
}

static void dirty(int y0, int y1)
{
    if (y0 < 0)
        y0 = 0;
    if (y1 > SH)
        y1 = SH;
    if (y0 < dirty0)
        dirty0 = y0;
    if (y1 > dirty1)
        dirty1 = y1;
}

/* rows [y0, y1) back to the background (and marked to send) */
static void restore(int y0, int y1)
{
    int i;
    dirty(y0, y1);
    if (y0 < 0)
        y0 = 0;
    if (y1 > SH)
        y1 = SH;
    for (i = y0 * SW; i < y1 * SW; i++) {
        const uint8_t *c = bgp + 3u * BGI[i];
        W[3 * i] = c[0];
        W[3 * i + 1] = c[1];
        W[3 * i + 2] = c[2];
    }
}

/* the marked rows, dithered to RGB565, to the panel */
static void flush(void)
{
    int y0 = dirty0, y1 = dirty1, y;
    dirty0 = SH;
    dirty1 = 0;
    for (y = y0; y < y1; y += 16) {
        int h = y1 - y < 16 ? y1 - y : 16, r, x;
        lcd_sync();                                   /* the last band may still be on its way */
        for (r = 0; r < h; r++) {
            const uint8_t *p = W + 3 * SW * (y + r), *b4 = BAYER4[(y + r) & 3];
            uint16_t *o = BAND + SW * r;
            for (x = 0; x < SW; x++, p += 3) {
                uint32_t b = b4[x & 3];
                uint32_t c = (uint32_t)Q5[b][p[0]] << 11 | (uint32_t)Q6[b][p[1]] << 5 | Q5[b][p[2]];
                o[x] = (uint16_t)(c >> 8 | (c & 0xFFu) << 8);   /* the panel takes it big-endian */
            }
        }
        lcd_blit(0, (uint32_t)y, SW, (uint32_t)h, BAND);
    }
}

/* ---------------------------------------------------------------- small maths for the screens (no libm) */

static float esqrt(float x)
{
    union {
        float f;
        uint32_t u;
    } v = {x};
    float g;
    if (x <= 0)
        return 0;
    v.u = 0x1FBD1DF5u + (v.u >> 1);           /* a first guess from the exponent, then Newton */
    g = v.f;
    g = 0.5f * (g + x / g);
    g = 0.5f * (g + x / g);
    g = 0.5f * (g + x / g);
    return g;
}

static float flog2(float x)                   /* x > 0: the exponent, and ln of the mantissa by atanh's series */
{
    union {
        float f;
        uint32_t u;
    } v = {x};
    float m, e = (float)((int)((v.u >> 23) & 255u) - 127), y, y2;
    v.u = (v.u & 0x007FFFFFu) | 0x3F800000u;
    m = v.f;                                   /* 1 .. 2 */
    y = (m - 1.0f) / (m + 1.0f);
    y2 = y * y;
    return e + 2.8853901f * y * (1.0f + y2 * (0.33333333f + y2 * (0.2f + y2 * (0.14285714f + y2 * 0.11111111f))));
}

static float fexp2(float x)
{
    int i = (int)(x < 0 ? x - 1.0f : x);
    float f = x - (float)i, r = 1.0f;
    float p = 1.0f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * 0.0096181f)));
    if (i > 0)
        while (i--)
            r *= 2.0f;
    else
        while (i++)
            r *= 0.5f;
    return p * r;
}

static float fpow(float x, float e) { return x <= 0 ? 0 : fexp2(e * flog2(x)); }

#define PI_F 3.14159265f

static float fsin_(float x)
{
    float s = 1, x2;
    while (x > PI_F)
        x -= 2 * PI_F;
    while (x < -PI_F)
        x += 2 * PI_F;
    if (x > PI_F / 2) {
        x = PI_F - x;
    } else if (x < -PI_F / 2) {
        x = -PI_F - x;
    }
    x2 = x * x;
    return s * x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72))));
}
static float fcos_(float x) { return fsin_(x + PI_F / 2); }

/* ---------------------------------------------------------------- pixels */

static inline void blend(int x, int y, rgb_t c, int a)          /* a 0..256 */
{
    uint8_t *p;
    if ((unsigned)x >= SW || (unsigned)y >= SH || a <= 0)
        return;
    if (a > 256)
        a = 256;
    p = W + 3 * (y * SW + x);
    p[0] = (uint8_t)(p[0] + (((int)c.r - p[0]) * a >> 8));
    p[1] = (uint8_t)(p[1] + (((int)c.g - p[1]) * a >> 8));
    p[2] = (uint8_t)(p[2] + (((int)c.b - p[2]) * a >> 8));
}

static inline void addc(int x, int y, rgb_t c, int k)           /* k 0..256 (and above) */
{
    uint8_t *p;
    int v;
    if ((unsigned)x >= SW || (unsigned)y >= SH || k <= 0)
        return;
    p = W + 3 * (y * SW + x);
    v = p[0] + (c.r * k >> 8);
    p[0] = (uint8_t)(v > 255 ? 255 : v);
    v = p[1] + (c.g * k >> 8);
    p[1] = (uint8_t)(v > 255 ? 255 : v);
    v = p[2] + (c.b * k >> 8);
    p[2] = (uint8_t)(v > 255 ? 255 : v);
}

static rgb_t mixc(rgb_t a, rgb_t b, float t)
{
    if (t < 0)
        t = 0;
    if (t > 1)
        t = 1;
    return C3((uint8_t)(a.r + (b.r - a.r) * t), (uint8_t)(a.g + (b.g - a.g) * t), (uint8_t)(a.b + (b.b - a.b) * t));
}

static rgb_t scalec(rgb_t a, float k)
{
    float r = a.r * k, g = a.g * k, b = a.b * k;
    return C3((uint8_t)(r > 255 ? 255 : r), (uint8_t)(g > 255 ? 255 : g), (uint8_t)(b > 255 ? 255 : b));
}

#define AF(f) ((int)((f) * 256.0f + 0.5f))

static void rect(int x0, int y0, int x1, int y1, rgb_t c, int a)   /* [x0, x1) x [y0, y1) */
{
    int x, y;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++)
            blend(x, y, c, a);
}

/* 1 px lines; fade: the ends fade over that many pixels (the gold hairlines) */
static void hline(float x0, float x1, int y, rgb_t c, float alpha, float fade)
{
    int x;
    for (x = (int)x0; x < (int)x1; x++) {
        float a = alpha;
        if (fade > 0) {
            float u = (x - x0 + 1) / fade, v = (x1 - x) / fade;
            a *= u < v ? (u < 1 ? u : 1) : (v < 1 ? v : 1);
        }
        blend(x, y, c, AF(a < 0 ? 0 : a));
    }
}

static void vline(int x, float y0, float y1, rgb_t c, float alpha, float fade)
{
    int y;
    for (y = (int)y0; y < (int)y1; y++) {
        float a = alpha;
        if (fade > 0) {
            float u = (y - y0 + 1) / fade, v = (y1 - y) / fade;
            a *= u < v ? (u < 1 ? u : 1) : (v < 1 ? v : 1);
        }
        blend(x, y, c, AF(a < 0 ? 0 : a));
    }
}

/* additive soft light, falloff 1 / (1 + (d / r)^2)^2 */
static void glow(float cx, float cy, float r, rgb_t c, float k)
{
    int R = (int)(r * 4), x, y;
    float ir2 = 1.0f / (r * r);
    for (y = (int)cy - R; y <= (int)cy + R; y++)
        for (x = (int)cx - R; x <= (int)cx + R; x++) {
            float d2 = ((x - cx) * (x - cx) + (y - cy) * (y - cy)) * ir2, f = 1.0f / (1.0f + d2);
            addc(x, y, c, AF(f * f * k));
        }
}

/* ---------------------------------------------------------------- coverage */

typedef struct {
    int x0, y0, x1, y1;
} box_t;

static box_t mbox;                                    /* what the mask holds */

static inline int iceil(float v)
{
    int i = (int)v;
    return (float)i < v ? i + 1 : i;
}

static void mask_clear(int x0, int y0, int x1, int y1)
{
    int y;
    x0 = x0 < 0 ? 0 : x0;
    y0 = y0 < 0 ? 0 : y0;
    x1 = x1 > SW ? SW : x1;
    y1 = y1 > SH ? SH : y1;
    mbox.x0 = x0;
    mbox.y0 = y0;
    mbox.x1 = x1 > x0 ? x1 : x0;
    mbox.y1 = y1 > y0 ? y1 : y0;
    for (y = y0; y < y1; y++)
        memset(MASK + y * SW + x0, 0, (uint32_t)(x1 > x0 ? x1 - x0 : 0));
}

/* a polygon (points in pixels, the PIL convention: pixel centres at .5) into the mask, 4 x 4 samples a pixel,
 * even-odd; the mask keeps the larger coverage */
static void mask_poly(const float *p, int n)
{
    float minx = p[0], maxx = p[0], miny = p[1], maxy = p[1];
    int i, x0, x1, y0, y1, y;
    static uint8_t cnt[SW];
    for (i = 1; i < n; i++) {
        minx = p[2 * i] < minx ? p[2 * i] : minx;
        maxx = p[2 * i] > maxx ? p[2 * i] : maxx;
        miny = p[2 * i + 1] < miny ? p[2 * i + 1] : miny;
        maxy = p[2 * i + 1] > maxy ? p[2 * i + 1] : maxy;
    }
    x0 = (int)minx - 1;
    x1 = (int)maxx + 2;
    y0 = (int)miny - 1;
    y1 = (int)maxy + 2;
    x0 = x0 < mbox.x0 ? mbox.x0 : x0;
    x1 = x1 > mbox.x1 ? mbox.x1 : x1;
    y0 = y0 < mbox.y0 ? mbox.y0 : y0;
    y1 = y1 > mbox.y1 ? mbox.y1 : y1;
    if (x1 <= x0 || y1 <= y0)
        return;
    for (y = y0; y < y1; y++) {
        int s, x;
        memset(cnt + x0, 0, (uint32_t)(x1 - x0));
        for (s = 0; s < 4; s++) {
            float sy = (float)y + s * 0.25f, xs[64];   /* the samples of PIL's 4x raster: x + c / 4, y + s / 4 */
            int nx = 0, j;
            for (i = 0; i < n; i++) {
                float ax = p[2 * i], ay = p[2 * i + 1], bx = p[2 * ((i + 1) % n)], by = p[2 * ((i + 1) % n) + 1];
                if ((ay <= sy && by > sy) || (by <= sy && ay > sy)) {
                    if (nx < 64)
                        xs[nx++] = ax + (sy - ay) * (bx - ax) / (by - ay);
                }
            }
            for (i = 1; i < nx; i++) {                /* sort the crossings */
                float v = xs[i];
                for (j = i - 1; j >= 0 && xs[j] > v; j--)
                    xs[j + 1] = xs[j];
                xs[j + 1] = v;
            }
            for (i = 0; i + 1 < nx; i += 2) {         /* the spans: count the sample columns inside */
                float a = xs[i], b = xs[i + 1];
                int c0 = iceil(a * 4.0f), c1 = iceil(b * 4.0f);   /* the sample columns in [a, b) */
                int c;
                if (c0 < x0 * 4)
                    c0 = x0 * 4;
                if (c1 > x1 * 4)
                    c1 = x1 * 4;
                for (c = c0; c < c1; c++)
                    cnt[c >> 2]++;
            }
        }
        for (x = x0; x < x1; x++) {
            uint32_t v = (uint32_t)(cnt[x] > 16 ? 16 : cnt[x]) * 255u / 16u;
            uint8_t *m = MASK + y * SW + x;
            if (v > *m)
                *m = (uint8_t)v;
        }
    }
}

static void mask_rect(float x0, float y0, float x1, float y1)
{
    float p[8] = {x0, y0, x1, y0, x1, y1, x0, y1};
    mask_poly(p, 4);
}

/* a thick polyline (each segment a quad) */
static void mask_line(const float *p, int n, float width)
{
    int i;
    for (i = 0; i + 1 < n; i++) {
        float dx = p[2 * i + 2] - p[2 * i], dy = p[2 * i + 3] - p[2 * i + 1], l = dx * dx + dy * dy, q[8], k;
        if (l < 1e-6f)
            continue;
        k = 0.5f * width / esqrt(l);
        q[0] = p[2 * i] - dy * k + 0.5f;
        q[1] = p[2 * i + 1] + dx * k + 0.5f;
        q[2] = p[2 * i + 2] - dy * k + 0.5f;
        q[3] = p[2 * i + 3] + dx * k + 0.5f;
        q[4] = p[2 * i + 2] + dy * k + 0.5f;
        q[5] = p[2 * i + 3] - dx * k + 0.5f;
        q[6] = p[2 * i] + dy * k + 0.5f;
        q[7] = p[2 * i + 1] - dx * k + 0.5f;
        mask_poly(q, 4);
    }
}

static void mask_fill(rgb_t c, float alpha)
{
    int x, y, a = AF(alpha);
    for (y = mbox.y0; y < mbox.y1; y++)
        for (x = mbox.x0; x < mbox.x1; x++) {
            int m = MASK[y * SW + x];
            if (m)
                blend(x, y, c, (m * a + 127) / 255);
        }
}

static void mask_addlight(rgb_t c, float k)
{
    int x, y, kk = AF(k);
    for (y = mbox.y0; y < mbox.y1; y++)
        for (x = mbox.x0; x < mbox.x1; x++) {
            int m = MASK[y * SW + x];
            if (m)
                addc(x, y, c, (m * kk + 127) / 255);
        }
}

/* the mask blurred (three box passes, about a Gaussian of the radius) and added as light; the mask is used up */
static void mask_glow(float radius, rgb_t c, float k)
{
    int r = (int)(radius * 0.9f + 0.5f), pass, x, y;
    static uint16_t line[SW];
    if (r < 1)
        r = 1;
    for (pass = 0; pass < 3; pass++) {
        for (y = mbox.y0; y < mbox.y1; y++) {         /* rows */
            int acc = 0, w = 2 * r + 1;
            uint8_t *m = MASK + y * SW;
            for (x = mbox.x0; x < mbox.x1; x++)
                line[x] = m[x];
            for (x = mbox.x0 - r; x < mbox.x1; x++) {
                int xa = x + r, xb = x - r - 1;
                if (xa < mbox.x1)
                    acc += line[xa];
                if (xb >= mbox.x0)
                    acc -= line[xb];
                if (x >= mbox.x0)
                    m[x] = (uint8_t)(acc / w);
            }
        }
        for (x = mbox.x0; x < mbox.x1; x++) {         /* columns */
            int acc = 0, w = 2 * r + 1;
            for (y = mbox.y0; y < mbox.y1; y++)
                line[y] = MASK[y * SW + x];
            for (y = mbox.y0 - r; y < mbox.y1; y++) {
                int ya = y + r, yb = y - r - 1;
                if (ya < mbox.y1)
                    acc += line[ya];
                if (yb >= mbox.y0)
                    acc -= line[yb];
                if (y >= mbox.y0)
                    MASK[y * SW + x] = (uint8_t)(acc / w);
            }
        }
    }
    mask_addlight(c, k);
}

static void lozenge(float cx, float cy, float r, rgb_t c, float alpha, int outline)
{
    float p[8] = {cx + 0.5f, cy - r + 0.5f, cx + r + 0.5f, cy + 0.5f, cx + 0.5f, cy + r + 0.5f, cx - r + 0.5f, cy + 0.5f};
    mask_clear((int)(cx - r) - 2, (int)(cy - r) - 2, (int)(cx + r) + 3, (int)(cy + r) + 3);
    if (outline) {
        float q[10] = {p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[0], p[1]};
        int i;
        for (i = 0; i < 10; i++)
            q[i] -= 0.5f;
        mask_line(q, 5, 1.0f);
    } else {
        mask_poly(p, 4);
    }
    mask_fill(c, alpha);
}

/* a rounded rectangle as a polygon (the corners in 4 steps) */
static int rrect_pts(float *p, float x0, float y0, float x1, float y1, float r)
{
    static const float C[5] = {1.0f, 0.9239f, 0.7071f, 0.3827f, 0.0f};
    int n = 0, i, q;
    float cx[4] = {x1 - r, x0 + r, x0 + r, x1 - r}, cy[4] = {y0 + r, y0 + r, y1 - r, y1 - r};
    for (q = 0; q < 4; q++)
        for (i = 0; i < 5; i++) {
            float c = C[i], s = C[4 - i], dx, dy;
            switch (q) {
            case 0: dx = c; dy = -s; break;
            case 1: dx = -s; dy = -c; break;
            case 2: dx = -c; dy = s; break;
            default: dx = s; dy = c; break;
            }
            p[2 * n] = cx[q] + dx * r;
            p[2 * n + 1] = cy[q] + dy * r;
            n++;
        }
    return n;
}

/* ---------------------------------------------------------------- type */

static const erk_glyph *glyph_of(const erk_font *f, uint32_t ch)
{
    int lo = 0, hi = f->n - 1;
    while (lo <= hi) {
        int m = (lo + hi) >> 1;
        if (f->g[m].ch == ch)
            return &f->g[m];
        if (f->g[m].ch < ch)
            lo = m + 1;
        else
            hi = m - 1;
    }
    return 0;
}

/* the string's characters: '\x7f' a middle dot, '\x80' an en dash */
static float twidth(const erk_font *f, const char *s, float track)
{
    float w = 0;
    int n = 0;
    for (; *s; s++, n++) {
        const erk_glyph *g = glyph_of(f, (uint8_t)*s);
        w += g ? g->adv / 16.0f : f->px * 0.3f;
    }
    return w + (n > 1 ? track * (n - 1) : 0);
}

/* anchor 'l', 'c' or 'r'; into the frame (alpha blended) or, mask = 1, into the mask (for its glow) */
static float text_at(float x, float y, const erk_font *f, const char *s, rgb_t c, float track, char anchor, float alpha,
                     int mask)
{
    float w = twidth(f, s, track), pen;
    int a = AF(alpha);
    if (anchor == 'c')
        x -= w / 2;
    else if (anchor == 'r')
        x -= w;
    pen = (float)(int)(x + 0.5f);
    for (; *s; s++) {
        const erk_glyph *g = glyph_of(f, (uint8_t)*s);
        int gx, gy, i, j;
        if (!g) {
            pen += f->px * 0.3f + track;
            continue;
        }
        gx = (int)(pen + 0.5f) + g->xo;
        gy = (int)y + g->yo;
        for (j = 0; j < g->h; j++)
            for (i = 0; i < g->w; i++) {
                uint32_t k = (uint32_t)j * g->w + (uint32_t)i;
                uint32_t v = (f->d[g->off + (k >> 1)] >> ((k & 1u) ? 0 : 4)) & 15u;
                if (!v)
                    continue;
                if (mask) {
                    int xx = gx + i, yy = gy + j;
                    if ((unsigned)xx < SW && (unsigned)yy < SH && MASK[yy * SW + xx] < v * 17u)
                        MASK[yy * SW + xx] = (uint8_t)(v * 17u);
                } else {
                    blend(gx + i, gy + j, c, (int)(v * (uint32_t)a / 15u));
                }
            }
        pen += g->adv / 16.0f + track;
    }
    return w;
}

static float text(float x, float y, const erk_font *f, const char *s, rgb_t c, float track, char anchor, float alpha)
{
    return text_at(x, y, f, s, c, track, anchor, alpha, 0);
}

/* type with a soft glow of its own colour under it */
static void text_glow(float x, float y, const erk_font *f, const char *s, rgb_t c, float track, char anchor,
                      float alpha, float glowk)
{
    float w = twidth(f, s, track), xl = anchor == 'c' ? x - w / 2 : anchor == 'r' ? x - w : x;
    mask_clear((int)xl - 8, (int)y - 8, (int)(xl + w) + 9, (int)y + f->px + 12);
    text_at(x, y, f, s, c, track, anchor, 1.0f, 1);
    mask_glow(2.0f, c, glowk);
    text(x, y, f, s, c, track, anchor, alpha);
}

/* the gold rule: fades at both ends; with the ornament a small lozenge in a gap at the centre */
static void hairline(float x0, float x1, int y, rgb_t c, float alpha, int ornament)
{
    float cx = (x0 + x1) / 2;
    if (ornament) {
        hline(x0, cx - 7, y, c, alpha, (cx - x0) * 0.7f);
        hline(cx + 7, x1, y, c, alpha, (x1 - cx) * 0.7f);
        lozenge(cx - 0.5f, (float)y, 2.5f, c, alpha, 0);
    } else {
        hline(x0, x1, y, c, alpha, (x1 - x0) * 0.3f);
    }
}

static void dotted_h(int x0, int x1, int y, rgb_t c, float alpha, int step)
{
    int x;
    for (x = x0; x <= x1; x += step)
        blend(x, y, c, AF(alpha));
}

static void dotted_v(int x, int y0, int y1, rgb_t c, float alpha, int step)
{
    int y;
    for (y = y0; y <= y1; y += step)
        blend(x, y, c, AF(alpha));
}

/* small number text without printf */
static void itoa_s(int v, char *b)
{
    char t[12];
    int n = 0;
    unsigned u = (unsigned)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}

static void cat_s(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}
