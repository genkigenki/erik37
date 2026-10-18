"""Erik37 boot: a grand piano in a black room, its lid slowly opening, a gold glint running along the rim,
then the name.

The piano is a small 3D model (a plan outline extruded, a lid hinged on the spine) seen by an orthographic
camera; faces are flat-shaded lacquer (a dim diffuse term plus a broad specular one) and painted far to near.
The firmware can play the same thing from a table of projected polygons per frame (a few hundred points),
filled with the 16-level coverage it uses for everything else.
"""
import math

import numpy as np
from PIL import Image, ImageDraw

from erik37_gfx import P, S, Canvas, font, hairline, lacquer, mix, text_w

K = 4                        # supersampling of the model
W4 = S * K

# ---------------------------------------------------------------- the model (units: the piano's length = 1)
D = 0.60                     # width at the keyboard
Y_CASE0, Y_CASE1 = 0.30, 0.43
Y_LID = 0.436
BENT = [(0.0, 0.0), (0.13, 0.0), (0.30, 0.03), (0.45, 0.14), (0.60, 0.28), (0.74, 0.38), (0.86, 0.47), (0.95, 0.60),
        (0.99, 0.76), (0.96, 0.90), (0.88, 0.985), (0.80, 1.0)]


def catmull(pts, n=8):
    out = []
    p = [pts[0]] + pts + [pts[-1]]
    for i in range(1, len(p) - 2):
        p0, p1, p2, p3 = p[i - 1], p[i], p[i + 1], p[i + 2]
        for k in range(n):
            t = k / n
            out.append(tuple(0.5 * ((2 * p1[j]) + (-p0[j] + p2[j]) * t + (2 * p0[j] - 5 * p1[j] + 4 * p2[j] - p3[j]) * t * t +
                                    (-p0[j] + 3 * p1[j] - 3 * p2[j] + p3[j]) * t ** 3) for j in range(2)))
    out.append(pts[-1])
    return out


BENT_S = [(x, z * D) for x, z in catmull(BENT, 7)]
PLAN = BENT_S + [(0.0, D)]                   # bentside + tail, then the spine back to the keyboard corner

# ---------------------------------------------------------------- camera
YAW, PITCH = math.radians(-20), math.radians(21)
SC = 168
CX, CY = 26, 132                             # screen position of the model's origin (keyboard, floor): fitted below
LIGHT = np.array([-0.55, 0.55, -0.65])
LIGHT /= np.linalg.norm(LIGHT)
VIEW = np.array([0.0, math.sin(PITCH), -math.cos(PITCH)])   # towards the camera, after the yaw


def rot(p):
    x, y, z = p
    c, s = math.cos(YAW), math.sin(YAW)
    return (x * c + z * s, y, -x * s + z * c)


def proj(p, mirror=False):
    x, y, z = rot(p)
    if mirror:
        y = -y
    X = x
    Y = y * math.cos(PITCH) + z * math.sin(PITCH)
    depth = z * math.cos(PITCH) - y * math.sin(PITCH)
    return ((CX + SC * X) * K, (CY - SC * Y) * K), depth


def normal_world(n):
    return np.array(rot(n))


def shade(n, base, spec_k=1.0, gloss=7):
    """lacquer: a dim diffuse term and a broad, soft specular (Blinn) one"""
    n = normal_world(n)
    n = n / (np.linalg.norm(n) + 1e-9)
    v = VIEW
    h = (LIGHT + v) / np.linalg.norm(LIGHT + v)
    diff = max(0.0, float(n @ LIGHT))
    spec = max(0.0, float(n @ h)) ** gloss
    c = np.asarray(base, np.float32) * (0.55 + 0.7 * diff) + np.asarray((96, 96, 106), np.float32) * spec * spec_k
    return c


def facing(n):
    return float(normal_world(n) @ VIEW) > 0


class Scene:
    """painter's list of polygons, rendered into a 4x float buffer"""

    def __init__(self):
        self.items = []

    def poly(self, pts3, col, mirror=False, depth=None, alpha=1.0, outline=None):
        pts, ds = zip(*(proj(p, mirror) for p in pts3))
        self.items.append((depth if depth is not None else sum(ds) / len(ds), pts, col, alpha, outline))

    def render(self, buf, cov=None):
        for depth, pts, col, alpha, outline in sorted(self.items, key=lambda t: -t[0]):
            xs, ys = [p[0] for p in pts], [p[1] for p in pts]
            pad = (outline or 0) + 2
            x0, y0 = max(0, int(min(xs)) - pad), max(0, int(min(ys)) - pad)
            x1, y1 = min(W4, int(max(xs)) + pad + 1), min(W4, int(max(ys)) + pad + 1)
            if x1 <= x0 or y1 <= y0:
                continue
            m = Image.new("L", (x1 - x0, y1 - y0), 0)
            d = ImageDraw.Draw(m)
            loc = [(x - x0, y - y0) for x, y in pts]
            if outline:
                d.line(loc, fill=255, width=outline)
            else:
                d.polygon(loc, fill=255)
            a = np.asarray(m, np.float32)[..., None] / 255.0 * alpha
            sub = buf[y0:y1, x0:x1]
            sub[:] = sub * (1 - a) + np.asarray(col, np.float32) * a
            if cov is not None:
                cov[y0:y1, x0:x1] = np.maximum(cov[y0:y1, x0:x1], a[..., 0])


def lid_point(x, y, z, ang):
    """a point of the lid (plan x z at height y) rotated about the hinge on the spine (z = D, y = Y_LID)"""
    dz, dy = z - D, y - Y_LID
    c, s = math.cos(ang), math.sin(ang)
    return (x, Y_LID + dy * c - dz * s, D + dz * c + dy * s)


def build(ang, mirror=False):
    sc = Scene()
    L = P["lacq1"]
    # legs (three, tapered) and the lyre with its brass pedals
    for (lx, lz) in ((0.07, 0.06), (0.07, D - 0.06), (0.83, D * 0.83)):
        w = 0.022
        dl = proj((lx, 0.15, lz))[1] + 0.05
        sc.poly([(lx - w, Y_CASE0, lz), (lx + w, Y_CASE0, lz), (lx + w * 0.6, 0.0, lz), (lx - w * 0.6, 0.0, lz)],
                shade((-0.3, 0, -1), (15, 15, 18), 0.8), mirror, depth=dl)
        sc.poly([(lx - w, Y_CASE0, lz), (lx - w * 0.55, Y_CASE0, lz), (lx - w * 0.35, 0.0, lz), (lx - w * 0.6, 0.0, lz)],
                (38, 38, 44), mirror, depth=dl - 0.001)          # the lit edge of the leg
        sc.poly([(lx - w * 0.9, 0.0, lz), (lx + w * 0.9, 0.0, lz), (lx + w * 0.9, 0.012, lz), (lx - w * 0.9, 0.012, lz)],
                P["gold_dim"], mirror, depth=dl - 0.002)
    lz = D * 0.5
    for dx in (-0.018, 0.018):
        sc.poly([(0.16 + dx - 0.005, Y_CASE0, lz), (0.16 + dx + 0.005, Y_CASE0, lz), (0.16 + dx + 0.005, 0.05, lz),
                 (0.16 + dx - 0.005, 0.05, lz)], (30, 30, 35), mirror, depth=proj((0.16, 0.1, lz))[1] + 0.02)
    sc.poly([(0.12, 0.05, lz - 0.05), (0.20, 0.05, lz - 0.05), (0.20, 0.025, lz - 0.05), (0.12, 0.025, lz - 0.05)],
            (30, 30, 34), mirror, depth=proj((0.16, 0.04, lz))[1] + 0.01)
    for k in range(3):
        px = 0.135 + k * 0.025
        sc.poly([(px - 0.007, 0.03, lz - 0.11), (px + 0.007, 0.03, lz - 0.11), (px + 0.007, 0.024, lz - 0.05),
                 (px - 0.007, 0.024, lz - 0.05)], P["gold"], mirror, depth=proj((px, 0.03, lz - 0.08))[1])
    # the case: the sides, extruded from the plan outline (only the faces turned to us)
    pl = PLAN
    for i in range(len(pl)):
        (x0, z0), (x1, z1) = pl[i], pl[(i + 1) % len(pl)]
        n = (z1 - z0, 0.0, -(x1 - x0))               # outward (the plan runs clockwise seen from above)
        if not facing(n):
            continue
        ov = 0.002
        sc.poly([(x0 - ov * (x1 - x0), Y_CASE0, z0), (x1, Y_CASE0, z1), (x1, Y_CASE1, z1), (x0, Y_CASE1, z0)],
                shade(n, L), mirror)
    # the keyboard: key bed, ivory keys, black keys, the key slip and cheeks
    kx0, kx1 = -0.12, 0.0
    ky = 0.37
    zs0, zs1 = 0.035, D - 0.035
    sc.poly([(kx0, ky - 0.035, zs0), (kx0, ky - 0.035, zs1), (kx0, ky, zs1), (kx0, ky, zs0)], shade((-1, 0, 0), L), mirror)
    sc.poly([(kx0, ky, zs0), (kx1, ky, zs0), (kx1, ky, zs1), (kx0, ky, zs1)], mix(P["ivory_dim"], P["ivory"], 0.3), mirror)
    nk = 26
    for k in range(nk):
        z = zs0 + (zs1 - zs0) * k / nk
        if k % 7 in (0, 1, 3, 4, 5):
            zz = z + (zs1 - zs0) / nk * 0.7
            sc.poly([(kx0 + 0.05, ky + 0.002, zz), (kx1, ky + 0.002, zz), (kx1, ky + 0.002, zz + 0.012),
                     (kx0 + 0.05, ky + 0.002, zz + 0.012)], (14, 14, 16), mirror, depth=-10)
    for zc in (zs0 - 0.035, zs1):
        sc.poly([(kx0 - 0.005, Y_CASE0 + 0.02, zc), (kx1, Y_CASE0 + 0.02, zc), (kx1, Y_CASE1, zc), (kx0 - 0.005, Y_CASE1 - 0.03, zc)],
                shade((0, 0, -1), L), mirror, depth=proj((kx0, ky, zc))[1] - 1)
        sc.poly([(kx0 - 0.005, Y_CASE0 + 0.02, zc + 0.035), (kx0 - 0.005, Y_CASE0 + 0.02, zc), (kx0 - 0.005, Y_CASE1 - 0.03, zc),
                 (kx0 - 0.005, Y_CASE1 - 0.03, zc + 0.035)], shade((-1, 0, 0), L), mirror, depth=proj((kx0, ky, zc))[1] - 1)
    sc.poly([(kx0, Y_CASE0 + 0.02, zs0), (kx0, Y_CASE0 + 0.02, zs1), (kx0, ky - 0.035, zs1), (kx0, ky - 0.035, zs0)],
            shade((-1, 0, 0), L), mirror, depth=-5)
    return sc


def top_layers(ang, mirror=False):
    """what is drawn over the case: the inside (gold plate, strings) and the lid, its edge and the prop"""
    sc = Scene()
    inner = [(x * 0.97 + 0.012, z * 0.94 + D * 0.03) for x, z in PLAN]
    sc.poly([(x, Y_CASE1 - 0.002, z) for x, z in PLAN], (6, 6, 8), mirror, depth=-20)
    if ang > 0.02:
        sc.poly([(x, Y_CASE1 - 0.01, z) for x, z in inner], mix(P["gold_deep"], P["gold_dim"], 0.2), mirror, depth=-21)
        for k in range(12):                                # strings: lines from the keyboard to the bridge
            z = D * (0.08 + 0.84 * k / 11)
            xe = max(x for x, zz in BENT_S if zz * 0.94 + D * 0.03 <= z + 0.02) * 0.92 if z > D * 0.05 else 0.3
            sc.poly([(0.03, Y_CASE1 - 0.008, z), (xe, Y_CASE1 - 0.008, z)], P["steel"], mirror, depth=-22, outline=K,
                    alpha=0.28)
    # the lid: its edge (thickness), then its face; the face we see is the top when closed, the underside open
    th = 0.012
    pts_top = [lid_point(x, Y_LID + th, z, ang) for x, z in PLAN]
    pts_bot = [lid_point(x, Y_LID, z, ang) for x, z in PLAN]
    for i in range(len(PLAN) - 1):
        sc.poly([pts_bot[i], pts_bot[i + 1], pts_top[i + 1], pts_top[i]], (20, 20, 24), mirror, depth=-30)
    under = math.degrees(ang) > math.degrees(PITCH) + 0.5
    face = pts_bot if under else pts_top
    n_lid = (0.0, math.cos(ang), -math.sin(ang)) if not under else (0.0, -math.cos(ang), math.sin(ang))
    sc.poly(face, shade(n_lid, (34, 31, 28) if under else P["lacq1"], 1.0, 6), mirror, depth=-31)
    if ang > 0.25:                                         # the prop stick
        a = (0.56, Y_CASE1, D * 0.30)
        b = lid_point(0.56, Y_LID, D * 0.36, ang)
        sc.poly([a, b], P["gold_dim"], mirror, depth=-32, outline=K)
    return sc, pts_top, pts_bot, under


RIM = [(x, Y_CASE1, z) for x, z in BENT_S]


def fit(box=(18, 14, 222, 142), ang=math.radians(42)):
    """scale and place the model so the open piano (lid, legs, keyboard) fills the box"""
    global SC, CX, CY
    SC, CX, CY = 1.0, 0.0, 0.0
    pts = [(x, y, z) for x, z in PLAN for y in (0.0, Y_CASE1)] + [lid_point(x, Y_LID, z, ang) for x, z in PLAN]
    pts += [(-0.125, 0.37, 0.0), (-0.125, 0.37, D)]
    xy = [proj(p)[0] for p in pts]
    xs, ys = [p[0] / K for p in xy], [p[1] / K for p in xy]
    w, h = max(xs) - min(xs), max(ys) - min(ys)
    SC = min((box[2] - box[0]) / w, (box[3] - box[1]) / h)
    CX = (box[0] + box[2]) / 2 - SC * (min(xs) + max(xs)) / 2
    CY = box[3] - SC * max(ys)


fit()


def render_piano(ang, glint=None, fade=1.0):
    """returns (rgb float 240 x 240 x 3 at 4x averaged down, coverage) plus the screen points of the rim"""
    buf = np.zeros((W4, W4, 3), np.float32)
    cov = np.zeros((W4, W4), np.float32)
    # the reflection on the polished floor first (the model mirrored, dim, fading downwards)
    rb = np.zeros_like(buf)
    rc = np.zeros_like(cov)
    build(ang, True).render(rb, rc)
    top_layers(ang, True)[0].render(rb, rc)
    ys = np.arange(W4, dtype=np.float32)[:, None]
    fl = CY * K
    f = np.clip(1 - (ys - fl) / (40 * K), 0, 1) ** 2 * (ys > fl)
    build(ang).render(buf, cov)
    sc, pts_top, pts_bot, under = top_layers(ang)
    sc.render(buf, cov)

    def down(a):
        return a.reshape(S, K, S, K, -1).mean(axis=(1, 3))
    rgb = down(buf)
    c = down(cov[..., None])[..., 0]
    rr = down(rb * (0.34 * f)[..., None])
    rcv = down((rc * 0.34 * f)[..., None])[..., 0]
    rim = [tuple(v / K for v in proj(p)[0]) for p in RIM]
    lid = [tuple(v / K for v in proj(p)[0]) for p in (pts_bot if under else pts_top)][:len(BENT_S)]
    return rgb, c, rr, rcv, rim, lid


def gold_polyline(cv, pts, alpha, glint=None, width=1.0):
    """the fine gold rim line; with a glint (0..1 along the line) a bright travelling spot, a star on it"""
    seg = [math.dist(pts[i], pts[i + 1]) for i in range(len(pts) - 1)]
    tot = sum(seg)
    acc = 0
    for i in range(len(pts) - 1):
        mid = (acc + seg[i] / 2) / tot
        acc += seg[i]
        a = alpha * (0.35 + 0.65 * min(1, mid * 3))
        col = P["gold"]
        if glint is not None:
            g = math.exp(-((mid - glint) / 0.07) ** 2)
            a = min(1.0, a + g)
            col = mix(P["gold"], P["gold_hi"], g)

        def fn(d, k, i=i):
            (x0, y0), (x1, y1) = pts[i], pts[i + 1]
            d.line([(x0 * k, y0 * k), (x1 * k, y1 * k)], fill=255, width=max(1, round(width * k)))
        cv.fill(cv.mask(fn), col, a)
    if glint is not None and 0 <= glint <= 1:
        acc, i = 0, 0
        while i < len(seg) - 1 and acc + seg[i] < glint * tot:
            acc += seg[i]
            i += 1
        t = (glint * tot - acc) / max(1e-6, seg[i])
        gx = pts[i][0] + (pts[i + 1][0] - pts[i][0]) * t
        gy = pts[i][1] + (pts[i + 1][1] - pts[i][1]) * t
        cv.glow(gx, gy, 3.0, P["gold_hi"], 0.9)
        cv.glow(gx, gy, 9.0, P["gold"], 0.18)
        for r in range(1, 13):                           # a four-ray star, the horizontal rays longer
            k = (1 - r / 13) ** 2
            cv.addpx(round(gx) + r, round(gy), P["gold_hi"], 0.9 * k)
            cv.addpx(round(gx) - r, round(gy), P["gold_hi"], 0.9 * k)
            if r < 7:
                cv.addpx(round(gx), round(gy) + r, P["gold_hi"], 0.7 * k)
                cv.addpx(round(gx), round(gy) - r, P["gold_hi"], 0.7 * k)


def boot_frame(ang_deg, presence=1.0, glint=None, title=0.0, byline=0.0, room_light=1.0):
    cv = Canvas()
    lacquer(cv, strength=room_light)
    # a soft pool of light on the stage behind the piano
    cv.glow(124, 78, 64, (34, 31, 28), 0.85 * max(presence, 0.5))      # the light behind: the silhouette reads
    rgb, c, rr, rcv, rim, lid = render_piano(math.radians(ang_deg))
    cv.a = cv.a * (1 - rcv[..., None] * presence) + rr * presence
    cv.a = cv.a * (1 - c[..., None] * presence) + rgb * presence
    # the stage line, fine and fading
    cv.hline(16, 225, CY, P["gold_deep"], 0.7 * presence, fade=60)
    gold_polyline(cv, rim, 0.85 * presence, glint)
    gold_polyline(cv, [tuple(v / K for v in proj((x, Y_CASE0, z))[0]) for x, z in BENT_S], 0.28 * presence)
    if ang_deg > 4:
        gold_polyline(cv, lid, 0.75 * presence * min(1, ang_deg / 15))
    if title > 0:
        f = font("title", 34)
        cv.text(121, 152, "ERIK37", f, P["ivory"], track=4, anchor="c", alpha=min(1, title), glow=0.15 * title)
        hairline(cv, 46, 194, 196, alpha=0.9 * min(1, title))
    if byline > 0:
        cv.text(120, 206, "by genkigenki", font("italic", 12), P["ivory_dim"], anchor="c", alpha=min(1, byline))
    return cv


# the storyboard: (lid angle in degrees, presence, glint, title, byline)
STORY = [
    (0, 0.30, None, 0, 0),
    (0, 1.0, None, 0, 0),
    (10, 1.0, None, 0, 0),
    (22, 1.0, None, 0, 0),
    (34, 1.0, None, 0, 0),
    (42, 1.0, 0.12, 0, 0),
    (42, 1.0, 0.50, 0.45, 0),
    (42, 1.0, 0.88, 1.0, 0.35),
    (42, 1.0, None, 1.0, 1.0),
]
