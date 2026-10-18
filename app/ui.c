/* SPDX-License-Identifier: MPL-2.0
 * Erik37: Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* The screens (design/DESIGN.md, drawn as design/screens.py and pages.py draw them): the play screen (a view into
 * the piano: strings, dampers, keys, the pedals), TONE & TOUCH (page I) and ROOM (page II), with the header on top
 * and the knob strip at the bottom. Each draws from the state in app.c; what moves is redrawn by row bands. Part of
 * the app's unity build (app.c), after draw.c. */

static const char *const TONE_NAME[4] = {"CONCERT", "STUDIO", "VINTAGE", "JAZZ"};
static const char *const TONE_TITLE[4] = {"Concert", "Studio", "Vintage", "Jazz"};
static const char *const ROOM_NAME[4] = {"DRY", "CHAMBER", "HALL", "CHURCH"};
static const char *const ROOM_TITLE[4] = {"Dry", "Chamber", "Hall", "Church"};
/* the page knobs' tags: fm1-base maps the FM-1's KNOB 1..4 to the SMK-37's K1..K4 (K5 MASTER; K6..K8 SELECT,
 * ALGORITHM, PRESETS have no job on the SMK-37). Should a unit show them on K5..K8, this one number changes the tags */
#ifndef KTAG_FIRST
#define KTAG_FIRST 1
#endif
#define HEAD_Y 6
#define RULE_Y 23
#define STRIP_Y 184
#define COL_W 60
#define SX0 12
#define PITCH 6
#define PIN_Y 125
#define DAMP_Y0 100
#define DAMP_Y1 112
#define STRIKE_Y 116
#define NO_DAMPER 31
#define KB_X0 10
#define KEY_Y0 133
#define KEY_Y1 148
#define PEDAL_Y 163

/* ---------------------------------------------------------------- shared blocks */

static void header_text(const char *left, const char *right)
{
    text(12, HEAD_Y, &F_HEAD12, left, IVORY, 1.5f, 'l', 1.0f);
    if (right)
        text(228, HEAD_Y, &F_HEAD12, right, IVORY, 1.5f, 'r', 1.0f);
}

static void page_dots(int n, int cur)
{
    int i;
    for (i = 0; i < n; i++) {
        float x = 228 - (n - 1 - i) * 9 - 3;
        lozenge(x, HEAD_Y + 6, 2.5f, i == cur ? GOLD : GOLD_DEEP, 1.0f, i != cur);
    }
}

typedef struct {
    const char *label;
    char value[16];
    float amt;                                /* 0..1, or < 0: a choice */
    int n, cur;                               /* the choice */
} knob_t;

static void knob_strip(const knob_t *k, int active)
{
    int i;
    for (i = 0; i < 4; i++) {
        int x0 = i * COL_W, hot = active == i, by = STRIP_Y + 43;
        float cx = x0 + COL_W / 2.0f, bx0 = x0 + 22, bx1 = x0 + COL_W - 7;
        char tag[3] = {'K', (char)('0' + KTAG_FIRST + i), 0};
        const erk_font *fv = twidth(&F_TEXT13, k[i].value, 0) <= 50 ? &F_TEXT13 : &F_TEXT12;
        text(cx, STRIP_Y + 5, &F_ITAL11, k[i].label, hot ? GOLD_HI : IVORY_DIM, 0, 'c', 1.0f);
        text(cx, STRIP_Y + 20, fv, k[i].value, IVORY, 0, 'c', 1.0f);
        text(x0 + 6, by - 5, &F_HEAD9, tag, hot ? GOLD : GOLD_DIM, 0, 'l', 1.0f);
        if (k[i].amt < 0) {                   /* a choice: n small lozenges, the current gold */
            int n = k[i].n, j;
            float step = (bx1 - bx0) / (n > 1 ? n - 1 : 1);
            for (j = 0; j < n; j++) {
                float xx = (float)(int)(bx0 + j * step + 0.5f);
                if (j == k[i].cur)
                    lozenge(xx, (float)by, 2.5f, hot ? GOLD_HI : GOLD, 1.0f, 0);
                else
                    lozenge(xx, (float)by, 1.5f, GOLD_DIM, 0.9f, 0);
            }
        } else {                              /* a value: a fine track, filled gold, a marker */
            float xm = bx0 + (bx1 - bx0) * k[i].amt;
            hline(bx0, bx1, by, GOLD_DEEP, 1.0f, 0);
            hline(bx0, (float)(int)(xm + 0.5f), by, hot ? GOLD_HI : GOLD, 1.0f, 0);
            lozenge((float)(int)(xm + 0.5f), (float)by, 2.5f, hot ? GOLD_HI : GOLD, 1.0f, 0);
        }
    }
}

static void pct_text(char *b, int v)
{
    itoa_s(v, b);
}

/* ---------------------------------------------------------------- the play screen */

static const uint8_t WHITE_OF[12] = {1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1};

static void dampers(const float *lvl, const uint8_t *held, float sustain)
{
    int i;
    for (i = 0; i < NO_DAMPER; i++) {
        int x = SX0 + PITCH * i, note = ui_note(i), dy, y0, y1;
        float lift = sustain;
        if ((lvl[i] > 0.05f || held[i]) && lift < 1)
            lift = 1;
        if (note_sost(note))
            lift = 1;
        dy = -(int)(4 * lift + 0.5f);
        y0 = DAMP_Y0 + dy;
        y1 = DAMP_Y1 + dy;
        if (lift > 0)
            rect(x - 2, DAMP_Y1 + dy, x + 3, DAMP_Y1 + dy + 1, C3(0, 0, 0), AF(0.55f));
        rect(x - 2, y0 + 4, x + 3, y1, FELT, 256);
        rect(x - 2, y0 + 4, x + 3, y0 + 5, FELT_HI, AF(0.8f));
        rect(x - 2, y0, x + 3, y0 + 4, BLACKISH, 256);
        hline(x - 2, x + 3, y0, GOLD, 0.22f + 0.25f * lift, 0);
    }
}

static void lit_strings(const float *lvl)
{
    int i;
    for (i = 0; i < 37; i++) {
        float l = lvl[i];
        int x = SX0 + PITCH * i, y0 = STRING_TOP[i];
        if (l <= 0.02f)
            continue;
        {
            rgb_t c = mixc(GOLD, GOLD_HI, l > 1 ? 1 : l);
            float a = 0.35f + 0.75f * l;
            a = a > 1 ? 1 : a;
            if (i < 12) {
                vline(x - 1, (float)y0, PIN_Y, c, a, 0);
                vline(x, (float)y0, PIN_Y, c, a, 0);
            } else {
                vline(x - 1, (float)y0, PIN_Y, c, a, 0);
                vline(x + 1, (float)y0, PIN_Y, c, a, 0);
            }
            /* the string's glow, strongest where the hammer strikes */
            mask_clear(x - 9, y0 - 9, x + 10, PIN_Y + 10);
            mask_rect((float)(x - 1), (float)y0, (float)(x + 2), (float)(PIN_Y + 1));
            mask_glow(2.2f, GOLD, 0.9f * l);
            glow(x + 0.5f, STRIKE_Y, 2.5f, GOLD_HI, 0.55f * l);
        }
    }
}

static void keyboard(const float *lvl, const uint8_t *held)
{
    int i, n = 0, wk[37];
    for (i = 0; i < 37; i++)
        if (WHITE_OF[i % 12])
            wk[i] = n++;
    for (i = 0; i < 37; i++) {
        float l;
        int x0;
        rgb_t c;
        if (!WHITE_OF[i % 12])
            continue;
        x0 = KB_X0 + 10 * wk[i];
        l = held[i] ? (lvl[i] > 0.6f ? lvl[i] : 0.6f) : lvl[i] * 0.6f;
        c = scalec(IVORY_DIM, 0.82f);
        if (l > 0.01f)
            c = mixc(c, GOLD_HI, l * 1.2f);
        rect(x0, KEY_Y0, x0 + 9, KEY_Y1, c, 256);
        rect(x0, KEY_Y1 - 2, x0 + 9, KEY_Y1, scalec(c, 0.78f), 256);
        if (l > 0.01f)
            glow(x0 + 4.5f, KEY_Y0 + 5, 4, GOLD, 0.25f * l);
    }
    for (i = 0; i < 37; i++) {
        float l;
        int x0;
        rgb_t c;
        if (WHITE_OF[i % 12])
            continue;
        x0 = KB_X0 + 10 * (wk[i - 1] + 1) - 3;
        l = held[i] ? (lvl[i] > 0.6f ? lvl[i] : 0.6f) : lvl[i] * 0.6f;
        c = l > 0.01f ? mixc(C3(10, 10, 12), GOLD, l) : C3(10, 10, 12);
        rect(x0, KEY_Y0, x0 + 6, KEY_Y0 + 9, c, 256);
        hline(x0 + 1, x0 + 5, KEY_Y0 + 8, l > 0.01f ? GOLD_HI : C3(46, 46, 52), 0.8f, 0);
    }
}

/* the lyre: three brass pedals (una corda, sostenuto, sustain), pressed = bright gold and lower */
static void pedals(float sustain, float sost, float una)
{
    float amts[3] = {una, sost, sustain};
    int k;
    for (k = 0; k < 3; k++) {
        float x = 120 + (k - 1) * 22, amt = amts[k], p[40];
        int dy = (int)(2 * amt + 0.5f), n;
        rgb_t c = mixc(GOLD_DIM, GOLD_HI, amt);
        n = rrect_pts(p, x - 6 + 0.5f, PEDAL_Y + dy + 0.5f, x + 6 + 0.5f, PEDAL_Y + dy + 5 + 0.5f, 2.5f);
        if (amt > 0.01f) {
            mask_clear((int)x - 16, PEDAL_Y - 10, (int)x + 17, PEDAL_Y + 18);
            mask_poly(p, n);
            mask_glow(3.0f, GOLD, 0.6f * amt);
        }
        mask_clear((int)x - 8, PEDAL_Y - 2, (int)x + 9, PEDAL_Y + 9);
        mask_poly(p, n);
        mask_fill(c, 1.0f);
    }
}

static const char *dynamic_mark(int vel)
{
    static const struct {
        int lim;
        const char *s;
    } D[] = {{24, "pp"}, {44, "p"}, {64, "mp"}, {84, "mf"}, {108, "f"}};
    unsigned i;
    for (i = 0; i < sizeof D / sizeof D[0]; i++)
        if (vel < D[i].lim)
            return D[i].s;
    return "ff";
}

/* ---------------------------------------------------------------- TONE & TOUCH */

static void tabs(const char *const *names, int n, int cur, int y)
{
    float ws[4], sum = 0, gap, x;
    int i;
    for (i = 0; i < n; i++) {
        ws[i] = twidth(&F_HEAD9, names[i], 0.6f);
        sum += ws[i];
    }
    gap = (216 - sum) / (n + 1);
    x = 12 + gap;
    for (i = 0; i < n; i++) {
        text(x, (float)y, &F_HEAD9, names[i], i == cur ? IVORY : GREY, 0.6f, 'l', 1.0f);
        if (i == cur)
            lozenge((float)(int)(x + ws[i] / 2 + 0.5f), (float)(y + 14), 2.0f, GOLD, 1.0f, 0);
        x += ws[i] + gap;
    }
}

static void corners(int x0, int y0, int x1, int y1, rgb_t c, float alpha)
{
    static const int D[4][4] = {{0, 0, 1, 1}, {1, 0, -1, 1}, {0, 1, 1, -1}, {1, 1, -1, -1}};
    int q, i, n = 7;
    for (q = 0; q < 4; q++) {
        int x = D[q][0] ? x1 : x0, y = D[q][1] ? y1 : y0, dx = D[q][2], dy = D[q][3];
        for (i = 0; i < n; i++) {
            float a = alpha * (1 - (float)i / n);
            blend(x + dx * i, y, c, AF(a));
            if (i)
                blend(x, y + dy * i, c, AF(a));
        }
    }
}

#define GX0 32
#define GX1 208
#define GY0 60
#define GY1 144

static void curve_draw(int c, rgb_t col, float width, float alpha, float glowk, int npts)
{
    float p[2 * 96];
    int i, n = 0, from = c == 3 ? 1 : 0;
    for (i = from; i < npts; i++) {
        float t = (float)i / (npts - 1);
        p[2 * n] = GX0 + (GX1 - GX0) * t;
        p[2 * n + 1] = GY1 - (GY1 - GY0) * curve_of(c, t);
        n++;
    }
    mask_clear(GX0 - 12, GY0 - 12, GX1 + 13, GY1 + 13);
    mask_line(p, n, width);
    if (glowk > 0) {
        mask_glow(2.0f, col, glowk);
        mask_clear(GX0 - 12, GY0 - 12, GX1 + 13, GY1 + 13);
        mask_line(p, n, width);
    }
    mask_fill(col, alpha);
}

static void tone_page(int curve, int vel)
{
    int k;
    float t, xv, yv, p[48];
    char cap[48];
    tabs(TONE_NAME, 4, set.tone, 32);
    corners(GX0 - 8, GY0 - 8, GX1 + 8, GY1 + 8, GOLD_DIM, 0.9f);
    for (k = 1; k < 4; k++) {
        dotted_h(GX0, GX1, (int)(GY0 + (GY1 - GY0) * k / 4.0f + 0.5f), GOLD_DEEP, 0.9f, 3);
        dotted_v((int)(GX0 + (GX1 - GX0) * k / 4.0f + 0.5f), GY0, GY1, GOLD_DEEP, 0.9f, 3);
    }
    hline(GX0, GX1 + 1, GY1, GOLD_DIM, 0.9f, 0);
    vline(GX0, GY0, GY1 + 1, GOLD_DIM, 0.9f, 0);
    for (k = 0; k < 4; k++)
        if (k != curve)
            curve_draw(k, GOLD_DIM, 1.0f, 0.4f, 0, 70);
    curve_draw(curve, GOLD, 1.4f, 1.0f, 0.5f, 90);
    /* the last note played: a dotted drop line and an ivory point on the curve */
    t = vel / 127.0f;
    xv = GX0 + (GX1 - GX0) * t;
    yv = GY1 - (GY1 - GY0) * curve_of(curve, t);
    dotted_v((int)(xv + 0.5f), (int)(yv + 0.5f) + 5, GY1 - 1, IVORY_DIM, 0.7f, 2);
    glow(xv, yv, 3, GOLD, 0.6f);
    {
        int n = 0, i;
        for (i = 0; i < 24; i++) {             /* the dot: a circle of 5 px */
            float a = i * (6.2831853f / 24);
            p[2 * n] = xv + 0.1f + 2.5f * fcos_(a) + 0.5f;
            p[2 * n + 1] = yv + 0.1f + 2.5f * fsin_(a) + 0.5f;
            n++;
        }
        mask_clear((int)xv - 5, (int)yv - 5, (int)xv + 6, (int)yv + 6);
        mask_poly(p, n);
        mask_fill(IVORY, 1.0f);
    }
    text(GX0, GY1 + 4, &F_ITAL10, "pp", IVORY_DIM, 0, 'l', 1.0f);
    text(GX1 + 1, GY1 + 4, &F_ITAL10, "ff", IVORY_DIM, 0, 'r', 1.0f);
    cap[0] = 0;
    cat_s(cap, CURVE_NAME[curve]);
    cat_s(cap, " touch  \x7f  last note ");
    {
        char t[8];
        itoa_s(vel, t);
        cat_s(cap, t);
    }
    text(120, 166, &F_ITAL11, cap, IVORY_DIM, 0, 'c', 1.0f);
}

/* ---------------------------------------------------------------- ROOM */

static int room_shape(int kind, float *p, float x0, float x1, float top, float floor_)
{
    float cx = (x0 + x1) / 2;
    int n = 0, i;
    if (kind == 0) {                           /* chamber: a low room with a coved ceiling */
        float w = (x1 - x0) * 0.40f;
        float q[12] = {cx - w, floor_, cx - w, top + 40, cx - w + 10, top + 30, cx + w - 10, top + 30, cx + w, top + 40,
                       cx + w, floor_};
        for (i = 0; i < 12; i++)
            p[i] = q[i];
        return 6;
    }
    if (kind == 1) {                           /* hall: a wide vault */
        float c = -1, s = 0;
        p[n * 2] = x0;
        p[n * 2 + 1] = floor_;
        n++;
        p[n * 2] = x0;
        p[n * 2 + 1] = top + 30;
        n++;
        for (i = 0; i < 48; i++) {
            float a = 3.14159265f * (1 - i / 47.0f);
            c = fcos_(a);
            s = fsin_(a);
            p[n * 2] = cx + (x1 - x0) / 2 * c;
            p[n * 2 + 1] = top + 30 - 24 * s;
            n++;
        }
        p[n * 2] = x1;
        p[n * 2 + 1] = floor_;
        return n + 1;
    }
    {                                          /* church: two arcs meeting at the apex */
        float w = (x1 - x0) * 0.24f, r = 2.0f * w, base = top + 70, a0 = 1.0471976f;   /* acos((r - w) / r), r = 2 w */
        p[n * 2] = cx - w;
        p[n * 2 + 1] = floor_;
        n++;
        p[n * 2] = cx - w;
        p[n * 2 + 1] = base;
        n++;
        for (i = 0; i < 30; i++) {
            float ang = 3.14159265f - (i / 29.0f) * a0;
            p[n * 2] = cx - w + r + r * fcos_(ang);
            p[n * 2 + 1] = base - r * fsin_(ang);
            n++;
        }
        for (i = 29; i >= 0; i--) {
            float ang = (i / 29.0f) * a0;
            p[n * 2] = cx + w - r + r * fcos_(ang);
            p[n * 2 + 1] = base - r * fsin_(ang);
            n++;
        }
        p[n * 2] = cx + w;
        p[n * 2 + 1] = base;
        n++;
        p[n * 2] = cx + w;
        p[n * 2 + 1] = floor_;
        return n + 1;
    }
}

/* a small grand from the side (keyboard left), gold with a lit top edge */
static void piano_icon(float cx, float by, float s, float lid)
{
    float body[10] = {cx - 12 * s, by - 7 * s, cx + 5 * s, by - 7 * s, cx + 12 * s, by - 5.5f * s, cx + 12 * s, by - 3.5f * s,
                      cx - 12 * s, by - 3.5f * s};
    float lidp[8] = {cx - 8 * s, by - 7 * s, cx + 8 * s, by - 7 * s - 11 * s * lid, cx + 9.5f * s, by - 7 * s - 10 * s * lid,
                     cx + 6 * s, by - 7 * s};
    int x0 = (int)(cx - 16 * s), x1 = (int)(cx + 16 * s), y0 = (int)(by - 22 * s), y1 = (int)by + 2, x, y;
    float lx;
    mask_clear(x0, y0, x1, y1);
    mask_poly(body, 5);
    for (lx = -10.5f; lx < 10; lx += 20) {     /* the legs */
        float L = cx + lx * s;
        mask_rect(L - 0.7f * s, by - 4 * s, L + 0.7f * s, by);
    }
    mask_poly(lidp, 4);
    mask_fill(GOLD_DIM, 1.0f);
    for (y = y1 - 1; y > y0; y--)              /* the top edges lit: coverage minus the row above */
        for (x = x0; x < x1; x++) {
            int e = (int)MASK[y * SW + x] - (int)MASK[(y - 1) * SW + x];
            if (e > 0)
                addc(x, y, GOLD_HI, e * AF(0.6f) / 255);
        }
}

static void room_page(int room, float size, float amount, float width)
{
    float p[2 * 80], x0 = 30, x1 = 210, top = 56, floor_ = 146, cx = 120, half, secs;
    int n, x, y, k;
    char cap[48], t[12];
    tabs(ROOM_NAME, 4, room, 32);
    if (room == 0) {                           /* dry: a small booth, no rings */
        float q[8] = {cx - 46, floor_, cx - 46, top + 58, cx + 46, top + 58, cx + 46, floor_};
        for (k = 0; k < 8; k++)
            p[k] = q[k];
        n = 4;
    } else {
        n = room_shape(room - 1, p, x0, x1, top, floor_);
    }
    mask_clear(10, top - 8, 230, (int)floor_ + 2);
    mask_poly(p, n);
    if (room) {                                /* the sound: gold rings from the piano, as many as the room is large,
                                                * as bright as the reverb is loud */
        int nr = 3 + (int)(size * 6 + 0.5f), j;
        float kk = 0.18f + 0.6f * amount, rw[9], rr0[9];
        for (j = 0; j < nr && j < 9; j++) {    /* each ring's radius and weight, once */
            rr0[j] = 18 + j * 11 * (0.75f + 0.5f * size);
            rw[j] = fpow(1 - (float)j / (nr + 1), 1.3f);
        }
        nr = nr > 9 ? 9 : nr;
        for (y = top - 8; y < (int)floor_ - 1; y++)
            for (x = 10; x < 230; x++) {
                int m = MASK[y * SW + x];
                float r, sum = 0;
                if (!m)
                    continue;
                r = esqrt((x - cx) * (x - cx) + ((y - (floor_ - 6)) * 1.1f) * ((y - (floor_ - 6)) * 1.1f));
                for (j = 0; j < nr; j++) {
                    float u = (r - rr0[j]) / 0.7f;
                    if (u > -3 && u < 3)
                        sum += fexp2(-1.442695f * u * u) * rw[j];
                }
                if (sum > 0.002f)
                    addc(x, y, GOLD, AF(sum * kk * m / 255.0f));
            }
    }
    {                                          /* the room's outline */
        mask_clear(10, top - 8, 230, (int)floor_ + 2);
        mask_line(p, n, 1.0f);
        mask_fill(GOLD, 0.95f);
    }
    hline(14, 227, (int)floor_, GOLD_DIM, 0.9f, 36);
    piano_icon(cx, floor_, 1.25f, 0.55f);
    half = 16 + 72 * width;
    lozenge((float)(int)(cx - half + 0.5f), floor_ + 7, 2.5f, IVORY, 1.0f, 0);
    lozenge((float)(int)(cx + half + 0.5f), floor_ + 7, 2.5f, IVORY, 1.0f, 0);
    dotted_h((int)(cx - half + 0.5f) + 6, (int)(cx + half + 0.5f) - 6, (int)floor_ + 7, IVORY_DIM, 0.6f, 3);
    secs = erk_room_seconds(room, (int)(size * 32767));
    cap[0] = 0;
    if (room) {
        int ds = (int)(secs * 10 + 0.5f);
        itoa_s(ds / 10, t);
        cat_s(cap, t);
        cat_s(cap, ".");
        itoa_s(ds % 10, t);
        cat_s(cap, t);
        cat_s(cap, " s decay  \x7f  width ");
    } else {
        cat_s(cap, "no room  \x7f  width ");
    }
    itoa_s((int)(width * 100 + 0.5f), t);
    cat_s(cap, t);
    text(120, 166, &F_ITAL11, cap, IVORY_DIM, 0, 'c', 1.0f);
}

/* ---------------------------------------------------------------- ABOUT */

static void about_page(void)
{
    text_glow(120, 34, &F_HEAD12, "ERIK37", IVORY, 3, 'c', 1.0f, 0.12f);
    text(120, 51, &F_TEXT11, APP_VERSION, GOLD, 0, 'c', 1.0f);
    hairline(70, 170, 67, GOLD, 0.8f, 1);
#if BOARD_SMK37
    text(120, 73, &F_ITAL11, "a grand piano for the SMK-37", IVORY_DIM, 0, 'c', 1.0f);
#else
    text(120, 73, &F_ITAL11, "a grand piano for the FM-1", IVORY_DIM, 0, 'c', 1.0f);
#endif
    text(120, 86, &F_ITAL10, "by genkigenki", IVORY_DIM, 0, 'c', 1.0f);
    text(120, 104, &F_HEAD9, "THE PIANO", GOLD_DIM, 0.6f, 'c', 1.0f);
    text(120, 116, &F_TEXT11, "Salamander Grand Piano V3", IVORY, 0, 'c', 1.0f);
    text(120, 129, &F_ITAL11, "by Alexander Holm  \x7f  CC BY 3.0", IVORY_DIM, 0, 'c', 1.0f);
    text(120, 147, &F_HEAD9, "TYPE AND CODE", GOLD_DIM, 0.6f, 'c', 1.0f);
    text(120, 159, &F_ITAL10, "Cinzel, Libre Caslon Text (SIL OFL 1.1)", IVORY_DIM, 0, 'c', 1.0f);
    text(120, 170, &F_ITAL10, "fm1-base and felucca-bsp (MPL-2.0)", IVORY_DIM, 0, 'c', 1.0f);
}
