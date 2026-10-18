"""Erik37 screens: the play screen (a view into the piano), TONE & TOUCH, ROOM, and the shared blocks
(header, knob strip). Each function draws one 240 x 240 frame for a given state."""
import math

import numpy as np

from erik37_gfx import P, Canvas, font, hairline, lacquer, mix, scale, text_w

TONES = ["CONCERT", "STUDIO", "VINTAGE", "JAZZ"]
ROOMS = ["CHAMBER", "HALL", "CHURCH"]
NOTE = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]

# ---------------------------------------------------------------- layout (pixels)
HEAD_Y = 6           # header text top
RULE_Y = 23          # gold rule under the header
STRIP_Y = 184        # gold rule above the knob strip
COL_W = 60


def header(cv, left, right=None, centre_orn=True, pages=None):
    """top line: left the tone (or the page title), right the room; a gold rule below"""
    f = font("head", 12)
    cv.text(12, HEAD_Y, left, f, P["ivory"], track=1.5)
    if right:
        cv.text(228, HEAD_Y, right, f, P["ivory"], track=1.5, anchor="r")
    if pages:                                          # page lozenges, right aligned
        n, cur = pages
        for i in range(n):
            x = 228 - (n - 1 - i) * 9 - 3
            cv.lozenge(x, HEAD_Y + 6, 2.5, P["gold"] if i == cur else P["gold_deep"], 1.0, outline=(i != cur))
    hairline(cv, 10, 230, RULE_Y, ornament=centre_orn)


def knob_strip(cv, knobs, active=None):
    """the knob block at the bottom: K tag, label (Cinzel caps), value (Caslon), and a value bar or the
    choice dots. knobs: [(label, value, frac or (n, i))], K1..K4 left to right."""
    hairline(cv, 10, 230, STRIP_Y, ornament=False, alpha=0.75)
    # labels are set like the directions in a score: lower-case Caslon italic ("una corda", "brilliance")
    fl, fv, fv2, ft = font("italic", 11), font("text", 13), font("text", 12), font("head", 9)
    for i, (label, value, amt) in enumerate(knobs):
        x0 = i * COL_W
        cx = x0 + COL_W / 2
        hot = (active == i)
        if i:
            cv.vline(x0, STRIP_Y + 8, 236, P["gold_deep"], 0.9, fade=10)
        cv.text(cx, STRIP_Y + 5, label.lower(), fl, P["gold_hi"] if hot else P["ivory_dim"], anchor="c")
        cv.text(cx, STRIP_Y + 20, value, fv if text_w(value, fv) <= 50 else fv2, P["ivory"], anchor="c")
        by = STRIP_Y + 43
        cv.text(x0 + 6, by - 5, f"K{i + 1}", ft, P["gold"] if hot else P["gold_dim"])
        bx0, bx1 = x0 + 22, x0 + COL_W - 7
        if isinstance(amt, tuple):                        # a choice: n small lozenges, the current gold
            n, cur = amt
            step = (bx1 - bx0) / max(1, n - 1)
            for k in range(n):
                xx = round(bx0 + k * step)
                if k == cur:
                    cv.lozenge(xx, by, 2.5, P["gold_hi"] if hot else P["gold"])
                else:
                    cv.lozenge(xx, by, 1.5, P["gold_dim"], 0.9)
        else:                                             # a value: a fine track, filled gold, a marker
            cv.hline(bx0, bx1, by, P["gold_deep"], 1.0)
            xm = bx0 + (bx1 - bx0) * amt
            cv.hline(bx0, round(xm), by, P["gold_hi"] if hot else P["gold"], 1.0)
            cv.lozenge(round(xm), by, 2.5, P["gold_hi"] if hot else P["gold"])


# ---------------------------------------------------------------- the view into the piano
KB_X0, KB_X1 = 10, 230      # the mini keyboard: 22 white keys of 10 px (C3..C6 shown, the SMK's 37 keys)
SX0, PITCH = 12, 6          # string of note i at x = SX0 + PITCH * i  (12 .. 228)
PIN_Y = 125                 # tuning pins
DAMP_Y0, DAMP_Y1 = 100, 112 # dampers (lowered); lifted 4 px
STRIKE_Y = 116              # where the hammers strike
NO_DAMPER = 31              # the top notes have no dampers (as on a real grand)
KEY_Y0, KEY_Y1 = 133, 148


def string_x(i):
    return SX0 + PITCH * i


def bridge_y(x):
    """the bridge follows the bentside: the bass strings long, the treble short"""
    t = max(0.0, (x - 70) / 160)
    return 34 + 50 * t ** 1.55


def rim_x(y):
    """the bentside (right inner rim) at height y: the case narrows towards the tail (up)"""
    t = max(0.0, min(1.0, (122 - y) / 96))
    return 233 - 92 * t ** 2.1


def interior(cv, notes, sustain=0.0, sostenuto=(), una_corda=False):
    """notes: {i: level 0..1} sounding; sustain 0..1 (half pedal shows half lifted)"""
    # inside the rim: the soundboard, a very dark warm wood with faint grain
    def rimfn(d, k):
        pts = [(7 * k, 27 * k)] + [(rim_x(y) * k, y * k) for y in np.linspace(27, 130, 40)] + [(7 * k, 130 * k)]
        d.polygon(pts, fill=255)
    rim = cv.mask(rimfn)
    ys, xs = np.mgrid[0:240, 0:240].astype(np.float32)
    grain = 0.5 + 0.5 * np.sin(xs * 0.9 + np.sin(ys * 0.05 + xs * 0.02) * 3.0)
    wood = np.asarray(P["wood"], np.float32) * (0.32 + 0.10 * grain[..., None]) * (0.55 + 0.45 * (ys[..., None] / 130))
    cv.a = cv.a * (1 - rim[..., None]) + wood * rim[..., None]

    # the plate: a gold frame along the rim, a pin plank at the bottom and two struts, darkening downwards
    def platefn(d, k):
        pts = [(9 * k, 29 * k)] + [((rim_x(y) - 2) * k, y * k) for y in np.linspace(29, 122, 40)] + [(9 * k, 122 * k)]
        d.polygon(pts, fill=255)
        inner = [(17 * k, 72 * k)] + [((rim_x(y) - 10) * k, y * k) for y in np.linspace(72, 97, 12)] + [(17 * k, 97 * k)]
        d.polygon(inner, fill=0)
        for xa, xb in ((84, 76), (156, 150)):        # struts (they run from the pin plank to the tail)
            d.polygon([(xa * k, 97 * k), ((xa + 6) * k, 97 * k), ((xb + 6) * k, 72 * k), (xb * k, 72 * k)], fill=255)
        # the rose holes of the plate
        for hx, hy, r in ((38, 50, 8), (80, 47, 9), (124, 50, 8), (162, 60, 6)):
            d.ellipse(((hx - r) * k, (hy - r) * k, (hx + r) * k, (hy + r) * k), fill=0)
    plate = cv.mask(platefn) * rim
    # light from the top left: the plate goes from dim brass (lit) to a deep bronze (far from the light)
    shade = np.clip(1.0 - 0.0045 * (xs * 0.6 + ys * 1.4 - 40) + 0.35 * np.exp(-((xs - 50) ** 2 + (ys - 45) ** 2) / 2500), 0, 1.3)
    gold = (np.asarray(P["gold_deep"], np.float32) * 0.75 + (np.asarray(P["gold_dim"], np.float32) * 1.05 - np.asarray(P["gold_deep"], np.float32) * 0.75) * shade[..., None])
    cv.a = cv.a * (1 - plate[..., None]) + gold * 0.62 * plate[..., None]
    # bevel: edges facing the light (up left) lit, the others in shadow; the rose holes get a gold ring
    lit = np.clip(plate - np.roll(np.roll(plate, 1, 0), 1, 1), 0, 1)
    shd = np.clip(plate - np.roll(np.roll(plate, -1, 0), -1, 1), 0, 1)
    cv.add(lit, P["gold_hi"], 0.30)
    cv.a *= (1 - 0.45 * shd)[..., None]
    # the rim itself: a gold line on the inner lacquer edge
    for y in range(27, 131):
        x = rim_x(y)
        cv.px(int(x), y, P["gold"], 0.55 * (0.4 + 0.6 * (y - 27) / 103))
    cv.hline(7, int(rim_x(27)), 27, P["gold"], 0.35, fade=40)

    # the bridge (dark wood) and its pins
    for i in range(37):
        x = string_x(i)
        by = bridge_y(x)
        while rim_x(by) < x + 4 and by < PIN_Y - 8:
            by += 1
        cv.rect(x - 3, by, x + 3, by + 2, P["wood"], 0.9)

    # the strings
    for i in range(37):
        x = string_x(i)
        y0 = int(round(bridge_y(x))) - 3
        while rim_x(y0) < x + 4 and y0 < PIN_Y - 8:      # inside the rim
            y0 += 1
        lvl = notes.get(i, 0.0)
        bass = i < 12
        col = P["copper"] if bass else P["steel"]
        k = 0.42 if bass else 0.30
        xs_ = (x - 1, x) if bass else (x - 1, x + 1)
        for sx in xs_:
            cv.vline(sx, y0, PIN_Y, col, k * (0.75 if sx != x - 1 else 1.0))
        if lvl > 0:
            c = mix(P["gold"], P["gold_hi"], min(1, lvl))
            for sx in xs_:
                cv.vline(sx, y0, PIN_Y, c, min(1, 0.35 + 0.75 * lvl))
            # the string's glow, strongest at the strike point
            def lf(d, kk, x=x, y0=y0):
                d.rectangle(((x - 1) * kk, y0 * kk, (x + 1) * kk, PIN_Y * kk), fill=255)
            cv.glow_cov(cv.mask(lf), 2.2, P["gold"], 0.9 * lvl)
            cv.glow(x + 0.5, STRIKE_Y, 2.5, P["gold_hi"], 0.55 * lvl)

    # tuning pins: a staggered double row of tiny steel dots
    for i in range(37):
        x = string_x(i)
        cv.px(x - 1, PIN_Y + (i & 1) * 2, P["steel"], 0.85)
        cv.px(x + 1, PIN_Y + 1 - (i & 1), P["steel"], 0.55)
    # the dampers: wine felt under a black head; lifted by the key (a sounding note), the sustain pedal
    # (all, half pedal half way), or held by the sostenuto
    for i in range(min(37, NO_DAMPER)):
        x = string_x(i)
        lift = max(sustain, 1.0 if notes.get(i, 0) > 0.05 else 0.0, 1.0 if i in sostenuto else 0.0)
        dy = -round(4 * lift)
        y0, y1 = DAMP_Y0 + dy, DAMP_Y1 + dy
        if lift:
            cv.rect(x - 2, DAMP_Y1 + dy, x + 3, DAMP_Y1 + dy + 1, (0, 0, 0), 0.55)     # its shadow
        cv.rect(x - 2, y0 + 4, x + 3, y1, P["felt"], 1.0)
        cv.rect(x - 2, y0 + 4, x + 3, y0 + 5, P["felt_hi"], 0.8)
        cv.rect(x - 2, y0, x + 3, y0 + 4, (8, 8, 10), 1.0)                            # the head
        cv.hline(x - 2, x + 3, y0, P["gold"], 0.22 + 0.25 * lift)

    # the fallboard edge and the keys
    cv.rect(7, 129, 233, 132, (4, 4, 6), 1.0)
    cv.hline(8, 232, 131, P["gold"], 0.6, fade=30)
    keyboard(cv, notes)


WHITE = [0, 2, 4, 5, 7, 9, 11]


def keyboard(cv, notes):
    """the 37 keys C3..C6 from above: 22 ivory keys of 10 px, black keys of 6 px; played keys gold"""
    n = 0
    wk = {}
    for i in range(37):
        if i % 12 in WHITE:
            wk[i] = n
            n += 1
    for i, k in wk.items():
        x0 = KB_X0 + 10 * k
        lvl = notes.get(i, 0.0)
        c = scale(P["ivory_dim"], 0.82)
        c = mix(c, P["gold_hi"], min(1, lvl * 1.2)) if lvl else c
        cv.rect(x0, KEY_Y0, x0 + 9, KEY_Y1, c, 1.0)
        cv.rect(x0, KEY_Y1 - 2, x0 + 9, KEY_Y1, scale(c, 0.78), 1.0)      # the front edge, a step darker
        if lvl:
            cv.glow(x0 + 4.5, KEY_Y0 + 5, 4, P["gold"], 0.25 * lvl)
    for i in range(37):
        if i % 12 not in WHITE:
            k = wk[i - 1] + 1
            x0 = KB_X0 + 10 * k - 3
            lvl = notes.get(i, 0.0)
            c = mix((10, 10, 12), P["gold"], min(1, lvl)) if lvl else (10, 10, 12)
            cv.rect(x0, KEY_Y0, x0 + 6, KEY_Y0 + 9, c, 1.0)
            cv.hline(x0 + 1, x0 + 5, KEY_Y0 + 8, (46, 46, 52) if not lvl else P["gold_hi"], 0.8)


def pedals(cv, cx, y, sustain, sostenuto, una_corda):
    """the lyre: three brass pedals (una corda, sostenuto, sustain), pressed = bright gold and lower"""
    lab = font("italic", 10)
    for k, (on, name) in enumerate(((una_corda, "una corda"), (sostenuto, "sost."), (sustain, "sustain"))):
        x = cx + (k - 1) * 22
        amt = float(on)
        dy = round(2 * amt)
        col = mix(P["gold_dim"], P["gold_hi"], amt)

        def pf(d, kk, x=x, dy=dy):
            d.rounded_rectangle(((x - 6) * kk, (y + dy) * kk, (x + 6) * kk, (y + dy + 5) * kk), radius=2.5 * kk, fill=255)
        m = cv.mask(pf)
        if amt:
            cv.glow_cov(m, 3, P["gold"], 0.6 * amt)
        cv.fill(m, col)
        cv.vline(x, y - 7, y + dy, P["gold_deep"], 0.9)      # the pedal rod
    # the pedal box: a fine brass bar the three rods hang from
    cv.hline(cx - 30, cx + 31, y - 7, P["gold_dim"], 0.9, fade=8)


def dynamic(vel):
    for lim, s in ((24, "pp"), (44, "p"), (64, "mp"), (84, "mf"), (108, "f")):
        if vel < lim:
            return s
    return "ff"


def play_screen(tone=0, room=1, notes=None, sustain=0.0, sostenuto=(), una_corda=False, vel=78, active=None,
                knobs=None, octave=0):
    cv = Canvas()
    lacquer(cv)
    header(cv, TONES[tone], ROOMS[room])
    interior(cv, notes or {}, sustain, sostenuto, una_corda)
    # the pedal row: the last dynamic on the left, the lyre in the middle, the keyboard's range on the right
    py = 163
    pedals(cv, 120, py, sustain, bool(sostenuto), una_corda)
    cv.text(26, py - 7, dynamic(vel), font("italic_b", 17), P["gold"], anchor="l")
    lo, hi = 3 + octave, 6 + octave
    cv.text(214, py - 2, f"C{lo} – C{hi}", font("text", 11), P["ivory_dim"], anchor="r")
    knob_strip(cv, knobs or [("TONE", TONES[tone].title(), (4, tone)), ("ROOM", ROOMS[room].title(), (3, room)),
                             ("REVERB", "38", 0.38), ("BRILLIANCE", "62", 0.62)], active)
    return cv
