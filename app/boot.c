/* SPDX-License-Identifier: MPL-2.0
 * Erik37: Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* The boot (design/boot.py, DESIGN.md "Boot"): a grand piano in a black room, its lid easing open, a gold glint
 * along the rim, then the name. A small 3D model (the plan outline extruded, a lid hinged on the spine), an
 * orthographic camera, flat-shaded lacquer (diffuse + Blinn specular), painted far to near with the 16-level
 * coverage, its mirror image on the floor. Part of the app's unity build (app.c), after draw.c. */

/* ---------------------------------------------------------------- the model (the piano's length = 1) */
#define BD 0.60f                               /* width at the keyboard */
#define Y_CASE0 0.30f
#define Y_CASE1 0.43f
#define Y_LID 0.436f
#define NBENT 78
#define NPLAN (NBENT + 1)
static const float BENT[12][2] = {{0.0f, 0.0f}, {0.13f, 0.0f}, {0.30f, 0.03f}, {0.45f, 0.14f}, {0.60f, 0.28f},
                                  {0.74f, 0.38f}, {0.86f, 0.47f}, {0.95f, 0.60f}, {0.99f, 0.76f}, {0.96f, 0.90f},
                                  {0.88f, 0.985f}, {0.80f, 1.0f}};
static float plan[NPLAN][2];                   /* x, z: the bentside (Catmull-Rom), then the spine's corner */

typedef struct {
    float x, y, z;
} v3;

static float b_yc, b_ys, b_pc, b_ps, b_sc, b_cx, b_cy;   /* camera: yaw, pitch, scale, origin on the screen */
static v3 b_light, b_view;

static v3 rot3(v3 p)
{
    v3 r = {p.x * b_yc + p.z * b_ys, p.y, -p.x * b_ys + p.z * b_yc};
    return r;
}

static void proj(v3 p, int mirror, float *sx, float *sy, float *depth)
{
    v3 r = rot3(p);
    float y = mirror ? -r.y : r.y, Y = y * b_pc + r.z * b_ps;
    *sx = b_cx + b_sc * r.x;
    *sy = b_cy - b_sc * Y;
    *depth = r.z * b_pc - y * b_ps;
}

static v3 norm3(v3 a)
{
    float l = esqrt(a.x * a.x + a.y * a.y + a.z * a.z) + 1e-9f;
    v3 r = {a.x / l, a.y / l, a.z / l};
    return r;
}

static float dot3(v3 a, v3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

/* lacquer: a dim diffuse term and a broad, soft specular (Blinn) one */
static rgb_t shade(v3 n, rgb_t base, float spec_k, int gloss)
{
    v3 h = {b_light.x + b_view.x, b_light.y + b_view.y, b_light.z + b_view.z};
    float diff, sp, r, g, b, k = 1;
    int i;
    n = norm3(rot3(n));
    h = norm3(h);
    diff = dot3(n, b_light);
    diff = diff > 0 ? diff : 0;
    sp = dot3(n, h);
    sp = sp > 0 ? sp : 0;
    for (i = 0; i < gloss; i++)
        k *= sp;
    r = base.r * (0.55f + 0.7f * diff) + 96 * k * spec_k;
    g = base.g * (0.55f + 0.7f * diff) + 96 * k * spec_k;
    b = base.b * (0.55f + 0.7f * diff) + 106 * k * spec_k;
    return C3((uint8_t)(r > 255 ? 255 : r), (uint8_t)(g > 255 ? 255 : g), (uint8_t)(b > 255 ? 255 : b));
}

static int facing(v3 n) { return dot3(rot3(n), b_view) > 0; }

static v3 lid_point(float x, float y, float z, float c, float s)   /* rotated about the hinge on the spine */
{
    float dz = z - BD, dy = y - Y_LID;
    v3 r = {x, Y_LID + dy * c - dz * s, BD + dz * c + dy * s};
    return r;
}

/* ---------------------------------------------------------------- the painter's list */
#define MAXITEM 200
#define MAXPTS 1200                            /* one scene (the case or the lid) needs ~800 */
typedef struct {
    float depth;
    int p0, n;                                 /* points in b_pts */
    rgb_t col;
    float alpha;
    uint8_t outline;
} item_t;
static item_t items[MAXITEM] PLAT_POOL;
static float b_pts[2 * MAXPTS] PLAT_POOL;
static int nitems, npts;

static void poly3(const v3 *pts, int n, rgb_t col, int mirror, float depth, int use_depth, float alpha, int outline)
{
    item_t *it;
    int i;
    float dsum = 0;
    if (nitems >= MAXITEM || npts + n > MAXPTS)
        return;
    it = &items[nitems++];
    it->p0 = npts;
    it->n = n;
    for (i = 0; i < n; i++) {
        float d;
        proj(pts[i], mirror, &b_pts[2 * npts], &b_pts[2 * npts + 1], &d);
        dsum += d;
        npts++;
    }
    it->depth = use_depth ? depth : dsum / n;
    it->col = col;
    it->alpha = alpha;
    it->outline = (uint8_t)outline;
}

static void quad(v3 a, v3 b, v3 c, v3 d, rgb_t col, int mirror, float depth, int use_depth)
{
    v3 q[4] = {a, b, c, d};
    poly3(q, 4, col, mirror, depth, use_depth, 1.0f, 0);
}

static float depth_of(v3 p, int mirror)
{
    float x, y, d;
    proj(p, mirror, &x, &y, &d);
    return d;
}

static v3 V(float x, float y, float z)
{
    v3 r = {x, y, z};
    return r;
}

/* the case, legs, lyre and keyboard (boot.py build) */
static void build(int mirror)
{
    rgb_t L = LACQ1;
    static const float LEG[3][2] = {{0.07f, 0.06f}, {0.07f, BD - 0.06f}, {0.83f, BD * 0.83f}};
    int i, k;
    float lz = BD * 0.5f, kx0 = -0.12f, kx1 = 0.0f, ky = 0.37f, zs0 = 0.035f, zs1 = BD - 0.035f;
    for (i = 0; i < 3; i++) {
        float lx = LEG[i][0], z = LEG[i][1], w = 0.022f, dl = depth_of(V(lx, 0.15f, z), mirror) + 0.05f;
        v3 n = {-0.3f, 0, -1};
        quad(V(lx - w, Y_CASE0, z), V(lx + w, Y_CASE0, z), V(lx + w * 0.6f, 0, z), V(lx - w * 0.6f, 0, z),
             shade(n, C3(15, 15, 18), 0.8f, 7), mirror, dl, 1);
        quad(V(lx - w, Y_CASE0, z), V(lx - w * 0.55f, Y_CASE0, z), V(lx - w * 0.35f, 0, z), V(lx - w * 0.6f, 0, z),
             C3(38, 38, 44), mirror, dl - 0.001f, 1);
        quad(V(lx - w * 0.9f, 0, z), V(lx + w * 0.9f, 0, z), V(lx + w * 0.9f, 0.012f, z), V(lx - w * 0.9f, 0.012f, z),
             GOLD_DIM, mirror, dl - 0.002f, 1);
    }
    for (k = 0; k < 2; k++) {
        float dx = k ? 0.018f : -0.018f;
        quad(V(0.16f + dx - 0.005f, Y_CASE0, lz), V(0.16f + dx + 0.005f, Y_CASE0, lz), V(0.16f + dx + 0.005f, 0.05f, lz),
             V(0.16f + dx - 0.005f, 0.05f, lz), C3(30, 30, 35), mirror, depth_of(V(0.16f, 0.1f, lz), mirror) + 0.02f, 1);
    }
    quad(V(0.12f, 0.05f, lz - 0.05f), V(0.20f, 0.05f, lz - 0.05f), V(0.20f, 0.025f, lz - 0.05f), V(0.12f, 0.025f, lz - 0.05f),
         C3(30, 30, 34), mirror, depth_of(V(0.16f, 0.04f, lz), mirror) + 0.01f, 1);
    for (k = 0; k < 3; k++) {
        float px = 0.135f + k * 0.025f;
        quad(V(px - 0.007f, 0.03f, lz - 0.11f), V(px + 0.007f, 0.03f, lz - 0.11f), V(px + 0.007f, 0.024f, lz - 0.05f),
             V(px - 0.007f, 0.024f, lz - 0.05f), GOLD, mirror, depth_of(V(px, 0.03f, lz - 0.08f), mirror), 1);
    }
    for (i = 0; i < NPLAN; i++) {              /* the case's sides turned to us */
        float x0 = plan[i][0], z0 = plan[i][1], x1 = plan[(i + 1) % NPLAN][0], z1 = plan[(i + 1) % NPLAN][1];
        v3 n = {z1 - z0, 0, -(x1 - x0)};
        float ov = 0.002f;
        if (!facing(n))
            continue;
        quad(V(x0 - ov * (x1 - x0), Y_CASE0, z0), V(x1, Y_CASE0, z1), V(x1, Y_CASE1, z1), V(x0, Y_CASE1, z0),
             shade(n, L, 1.0f, 7), mirror, 0, 0);
    }
    {
        v3 n = {-1, 0, 0};
        quad(V(kx0, ky - 0.035f, zs0), V(kx0, ky - 0.035f, zs1), V(kx0, ky, zs1), V(kx0, ky, zs0), shade(n, L, 1, 7), mirror, 0, 0);
    }
    quad(V(kx0, ky, zs0), V(kx1, ky, zs0), V(kx1, ky, zs1), V(kx0, ky, zs1), mixc(IVORY_DIM, IVORY, 0.3f), mirror, 0, 0);
    for (k = 0; k < 26; k++) {                 /* the black keys */
        float z = zs0 + (zs1 - zs0) * k / 26.0f;
        int m = k % 7;
        if (m == 0 || m == 1 || m == 3 || m == 4 || m == 5) {
            float zz = z + (zs1 - zs0) / 26.0f * 0.7f;
            quad(V(kx0 + 0.05f, ky + 0.002f, zz), V(kx1, ky + 0.002f, zz), V(kx1, ky + 0.002f, zz + 0.012f),
                 V(kx0 + 0.05f, ky + 0.002f, zz + 0.012f), C3(14, 14, 16), mirror, -10, 1);
        }
    }
    for (k = 0; k < 2; k++) {                  /* the cheeks */
        float zc = k ? zs1 : zs0 - 0.035f, d = depth_of(V(kx0, ky, zc), mirror) - 1;
        v3 n1 = {0, 0, -1}, n2 = {-1, 0, 0};
        quad(V(kx0 - 0.005f, Y_CASE0 + 0.02f, zc), V(kx1, Y_CASE0 + 0.02f, zc), V(kx1, Y_CASE1, zc),
             V(kx0 - 0.005f, Y_CASE1 - 0.03f, zc), shade(n1, L, 1, 7), mirror, d, 1);
        quad(V(kx0 - 0.005f, Y_CASE0 + 0.02f, zc + 0.035f), V(kx0 - 0.005f, Y_CASE0 + 0.02f, zc),
             V(kx0 - 0.005f, Y_CASE1 - 0.03f, zc), V(kx0 - 0.005f, Y_CASE1 - 0.03f, zc + 0.035f), shade(n2, L, 1, 7), mirror, d, 1);
    }
    {
        v3 n = {-1, 0, 0};
        quad(V(kx0, Y_CASE0 + 0.02f, zs0), V(kx0, Y_CASE0 + 0.02f, zs1), V(kx0, ky - 0.035f, zs1), V(kx0, ky - 0.035f, zs0),
             shade(n, L, 1, 7), mirror, -5, 1);
    }
}

static v3 b_lidtop[NPLAN], b_lidbot[NPLAN];
static int b_under;

/* the inside (gold plate, strings) and the lid, its edge and the prop (boot.py top_layers) */
static void top_layers(float ang, int mirror)
{
    static v3 q[NPLAN];
    float c = fcos_(ang), s = fsin_(ang), th = 0.012f;
    int i, k;
    for (i = 0; i < NPLAN; i++)
        q[i] = V(plan[i][0], Y_CASE1 - 0.002f, plan[i][1]);
    poly3(q, NPLAN, C3(6, 6, 8), mirror, -20, 1, 1.0f, 0);
    if (ang > 0.02f) {
        for (i = 0; i < NPLAN; i++)
            q[i] = V(plan[i][0] * 0.97f + 0.012f, Y_CASE1 - 0.01f, plan[i][1] * 0.94f + BD * 0.03f);
        poly3(q, NPLAN, mixc(GOLD_DEEP, GOLD_DIM, 0.2f), mirror, -21, 1, 1.0f, 0);
        for (k = 0; k < 12; k++) {             /* strings: lines from the keyboard to the bridge */
            float z = BD * (0.08f + 0.84f * k / 11.0f), xe = 0.3f;
            v3 l2[2];
            if (z > BD * 0.05f) {
                float mx = 0;
                for (i = 0; i < NBENT; i++)
                    if (plan[i][1] * 0.94f + BD * 0.03f <= z + 0.02f && plan[i][0] > mx)
                        mx = plan[i][0];
                xe = mx * 0.92f;
            }
            l2[0] = V(0.03f, Y_CASE1 - 0.008f, z);
            l2[1] = V(xe, Y_CASE1 - 0.008f, z);
            poly3(l2, 2, STEEL, mirror, -22, 1, 0.28f, 1);
        }
    }
    for (i = 0; i < NPLAN; i++) {
        b_lidtop[i] = lid_point(plan[i][0], Y_LID + th, plan[i][1], c, s);
        b_lidbot[i] = lid_point(plan[i][0], Y_LID, plan[i][1], c, s);
    }
    for (i = 0; i < NPLAN - 1; i++)
        quad(b_lidbot[i], b_lidbot[i + 1], b_lidtop[i + 1], b_lidtop[i], C3(20, 20, 24), mirror, -30, 1);
    b_under = ang * 57.29578f > 21.0f + 0.5f;  /* past the camera's pitch we see the underside */
    {
        v3 n = b_under ? V(0, -c, s) : V(0, c, -s);
        poly3(b_under ? b_lidbot : b_lidtop, NPLAN, shade(n, b_under ? C3(34, 31, 28) : LACQ1, 1.0f, 6), mirror, -31, 1, 1.0f, 0);
    }
    if (ang > 0.25f) {                         /* the prop stick */
        v3 l2[2] = {{0.56f, Y_CASE1, BD * 0.30f}, lid_point(0.56f, Y_LID, BD * 0.36f, c, s)};
        poly3(l2, 2, GOLD_DIM, mirror, -32, 1, 1.0f, 1);
    }
}

/* paint the list far to near; the reflection fades down from the floor line */
static void paint(float presence, int reflection)
{
    int i, j;
    for (i = 1; i < nitems; i++) {             /* sort by depth, far first (insertion: the list is small) */
        item_t t = items[i];
        for (j = i - 1; j >= 0 && items[j].depth < t.depth; j--)
            items[j + 1] = items[j];
        items[j + 1] = t;
    }
    for (i = 0; i < nitems; i++) {
        const item_t *it = &items[i];
        const float *p = &b_pts[2 * it->p0];
        float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f, a = it->alpha * presence;
        int k, x, y;
        for (k = 0; k < it->n; k++) {
            minx = p[2 * k] < minx ? p[2 * k] : minx;
            maxx = p[2 * k] > maxx ? p[2 * k] : maxx;
            miny = p[2 * k + 1] < miny ? p[2 * k + 1] : miny;
            maxy = p[2 * k + 1] > maxy ? p[2 * k + 1] : maxy;
        }
        mask_clear((int)minx - 3, (int)miny - 3, (int)maxx + 4, (int)maxy + 4);
        if (it->outline)
            mask_line(p, it->n, 1.0f);
        else
            mask_poly(p, it->n);
        if (!reflection) {
            mask_fill(it->col, a);
            continue;
        }
        for (y = mbox.y0; y < mbox.y1; y++) {  /* the floor's mirror: 34 %, fading over 40 px */
            float f = 1 - (y - b_cy) / 40.0f, aa;
            if (y <= b_cy || f <= 0)
                continue;
            aa = a * 0.34f * f * f;
            for (x = mbox.x0; x < mbox.x1; x++) {
                int m = MASK[y * SW + x];
                if (m)
                    blend(x, y, it->col, (int)(m * aa * 256 / 255));
            }
        }
    }
}

static void render_piano(float ang, float presence)
{
    nitems = npts = 0;
    build(1);
    paint(presence, 1);
    nitems = npts = 0;
    top_layers(ang, 1);
    paint(presence, 1);
    nitems = npts = 0;
    build(0);
    paint(presence, 0);
    nitems = npts = 0;
    top_layers(ang, 0);
    paint(presence, 0);
}

/* the fine gold rim line; with a glint (0..1 along it) a bright travelling spot with a four-ray star */
static void gold_polyline(const float *pts, int n, float alpha, float glint)
{
    float seg[NPLAN], tot = 0, acc = 0;
    int i;
    for (i = 0; i + 1 < n; i++) {
        float dx = pts[2 * i + 2] - pts[2 * i], dy = pts[2 * i + 3] - pts[2 * i + 1];
        seg[i] = esqrt(dx * dx + dy * dy);
        tot += seg[i];
    }
    if (tot <= 0)
        return;
    for (i = 0; i + 1 < n; i++) {
        float mid = (acc + seg[i] / 2) / tot, a = alpha * (0.35f + 0.65f * (mid * 3 < 1 ? mid * 3 : 1));
        rgb_t col = GOLD;
        acc += seg[i];
        if (glint >= 0) {
            float u = (mid - glint) / 0.07f, g = fexp2(-1.442695f * u * u);
            a = a + g > 1 ? 1 : a + g;
            col = mixc(GOLD, GOLD_HI, g);
        }
        mask_clear((int)(pts[2 * i] < pts[2 * i + 2] ? pts[2 * i] : pts[2 * i + 2]) - 2,
                   (int)(pts[2 * i + 1] < pts[2 * i + 3] ? pts[2 * i + 1] : pts[2 * i + 3]) - 2,
                   (int)(pts[2 * i] > pts[2 * i + 2] ? pts[2 * i] : pts[2 * i + 2]) + 3,
                   (int)(pts[2 * i + 1] > pts[2 * i + 3] ? pts[2 * i + 1] : pts[2 * i + 3]) + 3);
        mask_line(&pts[2 * i], 2, 1.0f);
        mask_fill(col, a);
    }
    if (glint >= 0 && glint <= 1) {
        float gx, gy, t;
        int r;
        acc = 0;
        i = 0;
        while (i < n - 2 && acc + seg[i] < glint * tot)
            acc += seg[i++];
        t = (glint * tot - acc) / (seg[i] > 1e-6f ? seg[i] : 1e-6f);
        gx = pts[2 * i] + (pts[2 * i + 2] - pts[2 * i]) * t;
        gy = pts[2 * i + 1] + (pts[2 * i + 3] - pts[2 * i + 1]) * t;
        glow(gx, gy, 3.0f, GOLD_HI, 0.9f);
        glow(gx, gy, 9.0f, GOLD, 0.18f);
        for (r = 1; r < 13; r++) {
            float k = (1 - r / 13.0f) * (1 - r / 13.0f);
            int ix = (int)(gx + 0.5f), iy = (int)(gy + 0.5f);
            addc(ix + r, iy, GOLD_HI, AF(0.9f * k));
            addc(ix - r, iy, GOLD_HI, AF(0.9f * k));
            if (r < 7) {
                addc(ix, iy + r, GOLD_HI, AF(0.7f * k));
                addc(ix, iy - r, GOLD_HI, AF(0.7f * k));
            }
        }
    }
}

static void boot_init(void)
{
    /* the bentside: Catmull-Rom through BENT, 7 points a span, then the spine's corner */
    int i, k, n = 0;
    float P[14][2];
    P[0][0] = BENT[0][0];
    P[0][1] = BENT[0][1];
    for (i = 0; i < 12; i++) {
        P[i + 1][0] = BENT[i][0];
        P[i + 1][1] = BENT[i][1];
    }
    P[13][0] = BENT[11][0];
    P[13][1] = BENT[11][1];
    for (i = 1; i < 12; i++)
        for (k = 0; k < 7; k++) {
            float t = k / 7.0f, t2 = t * t, t3 = t2 * t;
            int j;
            for (j = 0; j < 2; j++) {
                float p0 = P[i - 1][j], p1 = P[i][j], p2 = P[i + 1][j], p3 = P[i + 2][j];
                plan[n][j] = 0.5f * (2 * p1 + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 +
                                     (-p0 + 3 * p1 - 3 * p2 + p3) * t3);
            }
            plan[n][1] *= BD;
            n++;
        }
    plan[n][0] = BENT[11][0];
    plan[n][1] = BENT[11][1] * BD;
    n++;
    plan[n][0] = 0;
    plan[n][1] = BD;
    /* the camera: yaw -20, pitch 21; the light from the upper left */
    b_yc = fcos_(-20 * PI_F / 180);
    b_ys = fsin_(-20 * PI_F / 180);
    b_pc = fcos_(21 * PI_F / 180);
    b_ps = fsin_(21 * PI_F / 180);
    b_light = norm3(V(-0.55f, 0.55f, -0.65f));
    b_view = V(0, b_ps, -b_pc);
    /* fit the open piano (lid at 42) into (18, 14) - (222, 142) */
    {
        float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f, c = fcos_(42 * PI_F / 180), s = fsin_(42 * PI_F / 180);
        float x, y, d, w, h;
        b_sc = 1;
        b_cx = b_cy = 0;
        for (i = 0; i < NPLAN * 3 + 2; i++) {
            v3 p;
            if (i < NPLAN)
                p = V(plan[i][0], 0, plan[i][1]);
            else if (i < 2 * NPLAN)
                p = V(plan[i - NPLAN][0], Y_CASE1, plan[i - NPLAN][1]);
            else if (i < 3 * NPLAN)
                p = lid_point(plan[i - 2 * NPLAN][0], Y_LID, plan[i - 2 * NPLAN][1], c, s);
            else
                p = V(-0.125f, 0.37f, i == 3 * NPLAN ? 0.0f : BD);
            proj(p, 0, &x, &y, &d);
            minx = x < minx ? x : minx;
            maxx = x > maxx ? x : maxx;
            miny = y < miny ? y : miny;
            maxy = y > maxy ? y : maxy;
        }
        w = maxx - minx;
        h = maxy - miny;
        b_sc = (222 - 18) / w < (142 - 14) / h ? (222 - 18) / w : (142 - 14) / h;
        b_cx = (18 + 222) / 2.0f - b_sc * (minx + maxx) / 2;
        b_cy = 142 - b_sc * maxy;
    }
}

/* one frame: lid in degrees, presence 0..1, glint 0..1 (< 0 none), the title's and the byline's fade */
static void boot_frame(float lid_deg, float presence, float glint, float title, float byline)
{
    float rim[2 * NBENT], low[2 * NBENT], lidl[2 * NBENT], d, ang = lid_deg * PI_F / 180;
    int i;
    restore(0, SH);
    render_piano(ang, presence);
    hline(16, 225, (int)b_cy, GOLD_DEEP, 0.7f * presence, 60);
    for (i = 0; i < NBENT; i++) {
        proj(V(plan[i][0], Y_CASE1, plan[i][1]), 0, &rim[2 * i], &rim[2 * i + 1], &d);
        proj(V(plan[i][0], Y_CASE0, plan[i][1]), 0, &low[2 * i], &low[2 * i + 1], &d);
        proj(b_under ? b_lidbot[i] : b_lidtop[i], 0, &lidl[2 * i], &lidl[2 * i + 1], &d);
    }
    gold_polyline(rim, NBENT, 0.85f * presence, glint);
    gold_polyline(low, NBENT, 0.28f * presence, -1);
    if (lid_deg > 4)
        gold_polyline(lidl, NBENT, 0.75f * presence * (lid_deg / 15 < 1 ? lid_deg / 15 : 1), -1);
    if (title > 0) {
        float t = title > 1 ? 1 : title;
        text_glow(121, 152, &F_TITLE34, "ERIK37", IVORY, 4, 'c', t, 0.15f * t);
        hairline(46, 194, 196, GOLD, 0.9f * t, 1);
    }
    if (byline > 0)
        text(120, 206, &F_ITAL12, "by genkigenki", IVORY_DIM, 0, 'c', byline > 1 ? 1 : byline);
}

/* the animation (design/make_design.py anim_boot): 54 frames of 66 ms */
#define BOOT_FRAMES 54
static void boot_step(int f)
{
    float t = (float)f / (BOOT_FRAMES - 1), pres, lid, gl, title, by, u;
    pres = 0.25f + t / 0.12f;
    pres = pres > 1 ? 1 : pres;
    u = (t - 0.12f) / 0.42f;
    u = u < 0 ? 0 : u > 1 ? 1 : u;
    lid = 42 * (0.5f - 0.5f * fcos_(PI_F * u));
    gl = (t >= 0.52f && t <= 0.84f) ? (t - 0.52f) / 0.32f : -1;
    title = (t - 0.62f) / 0.2f;
    title = title < 0 ? 0 : title > 1 ? 1 : title;
    by = (t - 0.8f) / 0.15f;
    by = by < 0 ? 0 : by > 1 ? 1 : by;
    boot_frame(lid, pres, gl, title, by);
}
