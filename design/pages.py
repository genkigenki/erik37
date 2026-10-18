"""Erik37 settings pages: TONE & TOUCH (page I) and ROOM (page II)."""
import math

import numpy as np

from erik37_gfx import P, Canvas, font, lacquer, text_w
from screens import ROOMS, TONES, header, knob_strip


def tabs(cv, names, cur, y):
    """a row of choices: the current ivory over a gold lozenge, the others warm grey"""
    f = font("head", 9)
    ws = [text_w(n, f, 0.6) for n in names]
    gap = (216 - sum(ws)) / (len(names) + 1)
    x = 12 + gap
    for i, (n, w) in enumerate(zip(names, ws)):
        cv.text(x, y, n, f, P["ivory"] if i == cur else P["grey"], track=0.6)
        if i == cur:
            cv.lozenge(round(x + w / 2), y + 14, 2, P["gold"])
        x += w + gap


def curve_line(cv, pts, col, width=1.0, alpha=1.0, glow=0.0):
    def fn(d, k):
        d.line([(x * k + k / 2, y * k + k / 2) for x, y in pts], fill=255, width=max(1, round(width * k)), joint="curve")
    m = cv.mask(fn)
    if glow:
        cv.glow_cov(m, 2.0, col, glow)
    cv.fill(m, col, alpha)


def dotted_h(cv, x0, x1, y, col, alpha, step=3):
    for x in range(int(x0), int(x1) + 1, step):
        cv.px(x, int(y), col, alpha)


def dotted_v(cv, x, y0, y1, col, alpha, step=3):
    for y in range(int(y0), int(y1) + 1, step):
        cv.px(int(x), y, col, alpha)


def corners(cv, x0, y0, x1, y1, col, alpha=0.8, n=7):
    """a frame drawn only at its corners (the printed concert programme look)"""
    for (x, y, dx, dy) in ((x0, y0, 1, 1), (x1, y0, -1, 1), (x0, y1, 1, -1), (x1, y1, -1, -1)):
        for i in range(n):
            a = alpha * (1 - i / n)
            cv.px(x + dx * i, y, col, a)
            if i:
                cv.px(x, y + dy * i, col, a)


CURVES = [("Soft", lambda v: v ** 0.55), ("Normal", lambda v: v ** 0.95), ("Hard", lambda v: v ** 1.7),
          ("Fixed", lambda v: 0.78)]


def tone_touch_screen(tone=0, curve=1, brill=0.62, reso=0.40, una=0.35, vel=88, active=0):
    cv = Canvas()
    lacquer(cv)
    header(cv, "TONE & TOUCH", pages=(2, 0), centre_orn=False)
    tabs(cv, TONES, tone, 32)
    # the velocity curve: the key's speed (pp .. ff) to the hammer's force
    gx0, gx1, gy0, gy1 = 32, 208, 60, 144
    corners(cv, gx0 - 8, gy0 - 8, gx1 + 8, gy1 + 8, P["gold_dim"], 0.9)
    for k in range(1, 4):
        dotted_h(cv, gx0, gx1, round(gy0 + (gy1 - gy0) * k / 4), P["gold_deep"], 0.9)
        dotted_v(cv, round(gx0 + (gx1 - gx0) * k / 4), gy0, gy1, P["gold_deep"], 0.9)
    cv.hline(gx0, gx1 + 1, gy1, P["gold_dim"], 0.9)
    cv.vline(gx0, gy0, gy1 + 1, P["gold_dim"], 0.9)

    def pts_of(fn, n=70):
        return [(gx0 + (gx1 - gx0) * t, gy1 - (gy1 - gy0) * fn(t)) for t in np.linspace(0, 1, n)]
    for j, (name, fn) in enumerate(CURVES):
        if j != curve:
            p = pts_of(fn)
            curve_line(cv, p[1:] if j == 3 else p, P["gold_dim"], 1.0, 0.4)
    fn = CURVES[curve][1]
    curve_line(cv, pts_of(fn, 90), P["gold"], 1.4, 1.0, glow=0.5)
    # the last note played: dotted guides and an ivory point on the curve
    t = vel / 127
    xv, yv = gx0 + (gx1 - gx0) * t, gy1 - (gy1 - gy0) * fn(t)
    dotted_v(cv, round(xv), round(yv) + 5, gy1 - 1, P["ivory_dim"], 0.7, 2)
    cv.glow(xv, yv, 3, P["gold"], 0.6)

    def dot(d, k):
        d.ellipse(((xv - 2.4) * k, (yv - 2.4) * k, (xv + 2.6) * k, (yv + 2.6) * k), fill=255)
    cv.fill(cv.mask(dot), P["ivory"])
    fi = font("italic", 10)
    cv.text(gx0, gy1 + 4, "pp", fi, P["ivory_dim"])
    cv.text(gx1 + 1, gy1 + 4, "ff", fi, P["ivory_dim"], anchor="r")
    cv.text(120, 166, f"{CURVES[curve][0]} touch  ·  last note {vel}", font("italic", 11), P["ivory_dim"], anchor="c")
    knob_strip(cv, [("TOUCH", CURVES[curve][0], (4, curve)), ("BRILLIANCE", f"{round(brill * 100)}", brill),
                    ("RESONANCE", f"{round(reso * 100)}", reso), ("UNA CORDA", f"{round(una * 100)}", una)], active)
    return cv


def piano_icon(cv, cx, by, s=1.0, lid=0.55):
    """a small grand from the side (keyboard left), lacquer with a gold top edge: the ROOM page's source"""
    def body(d, k):
        pts = [(cx - 12 * s, by - 7 * s), (cx + 5 * s, by - 7 * s), (cx + 12 * s, by - 5.5 * s), (cx + 12 * s, by - 3.5 * s),
               (cx - 12 * s, by - 3.5 * s)]
        d.polygon([(x * k, y * k) for x, y in pts], fill=255)
        for lx in (cx - 10.5 * s, cx + 9.5 * s):
            d.rectangle(((lx - 0.7 * s) * k, (by - 4 * s) * k, (lx + 0.7 * s) * k, by * k), fill=255)
        d.polygon([((cx - 8 * s) * k, (by - 7 * s) * k), ((cx + 8 * s) * k, (by - 7 * s - 11 * s * lid) * k),
                   ((cx + 9.5 * s) * k, (by - 7 * s - 10 * s * lid) * k), ((cx + 6 * s) * k, (by - 7 * s) * k)], fill=255)
    m = cv.mask(body)
    cv.fill(m, P["gold_dim"])
    edge = np.clip(m - np.roll(m, 1, 0), 0, 1)
    cv.add(edge, P["gold_hi"], 0.6)


def room_shape(kind, x0, x1, top, floor):
    """the room's section: CHAMBER a low room with a coved ceiling, HALL a wide vault, CHURCH a pointed arch"""
    cx = (x0 + x1) / 2
    if kind == 0:
        w = (x1 - x0) * 0.40
        return [(cx - w, floor), (cx - w, top + 40), (cx - w + 10, top + 30), (cx + w - 10, top + 30), (cx + w, top + 40),
                (cx + w, floor)]
    if kind == 1:
        pts = [(x0, floor), (x0, top + 30)]
        for t in np.linspace(0, 1, 48):
            a = math.pi * (1 - t)
            pts.append((cx + (x1 - x0) / 2 * math.cos(a), top + 30 - 24 * math.sin(a)))
        return pts + [(x1, floor)]
    w = (x1 - x0) * 0.24                          # gothic: two arcs of radius r meeting at the apex
    r = 2.0 * w
    base = top + 70
    a0 = math.acos((r - w) / r)
    pts = [(cx - w, floor), (cx - w, base)]
    for t in np.linspace(0, 1, 30):
        ang = math.pi - t * a0
        pts.append((cx - w + r + r * math.cos(ang), base - r * math.sin(ang)))
    for t in np.linspace(1, 0, 30):
        ang = t * a0
        pts.append((cx + w - r + r * math.cos(ang), base - r * math.sin(ang)))
    return pts + [(cx + w, base), (cx + w, floor)]


def room_screen(room=1, size=0.55, amount=0.38, width=0.80, active=1):
    cv = Canvas()
    lacquer(cv)
    header(cv, "ROOM", pages=(2, 1), centre_orn=False)
    tabs(cv, ROOMS, room, 32)
    x0, x1, top, floor = 30, 210, 56, 146
    shape = room_shape(room, x0, x1, top, floor)
    cx = 120

    # the sound: gold rings from the piano, as many as the room is large, as bright as the reverb is loud
    def clipfn(d, k):
        d.polygon([(x * k, y * k) for x, y in shape], fill=255)
    inside = cv.mask(clipfn)
    ys, xs = np.mgrid[0:240, 0:240].astype(np.float32)
    r = np.hypot(xs - cx, (ys - (floor - 6)) * 1.1)
    rings = np.zeros_like(r)
    n = 3 + round(size * 6)
    for j in range(n):
        rr = 18 + j * 11 * (0.75 + 0.5 * size)
        rings += np.exp(-((r - rr) / 0.7) ** 2) * (1 - j / (n + 1)) ** 1.3
    rings *= inside * (ys < floor - 1)
    cv.add(rings, P["gold"], 0.18 + 0.6 * amount)
    curve_line(cv, shape, P["gold"], 1.0, 0.95)
    cv.hline(14, 227, floor, P["gold_dim"], 0.9, fade=36)
    piano_icon(cv, cx, floor, 1.25, 0.55)
    # stereo width: two small lozenges below the stage, a dotted span between them
    half = 16 + 72 * width
    for sx in (cx - half, cx + half):
        cv.lozenge(round(sx), floor + 7, 2.5, P["ivory"])
    dotted_h(cv, round(cx - half) + 6, round(cx + half) - 6, floor + 7, P["ivory_dim"], 0.6, 3)
    secs = 0.6 + size * 3.3
    cv.text(120, 166, f"{secs:.1f} s decay  ·  width {round(width * 100)}", font("italic", 11), P["ivory_dim"], anchor="c")
    knob_strip(cv, [("ROOM", ROOMS[room].title(), (3, room)), ("SIZE", f"{secs:.1f} s", size),
                    ("AMOUNT", f"{round(amount * 100)}", amount), ("WIDTH", f"{round(width * 100)}", width)], active)
    return cv
