"""Erik37 pad lights: what each of the SMK's 16 RGB pads does, and its three light levels."""
import numpy as np

from erik37_gfx import P, Canvas, font, hairline, lacquer, mix, text_w

# the three levels. LED = the value written to the pad (the LEDs are much bluer and greener than they read
# on screen, so the warm tones carry little blue); SEEN = how they look, for the mockups
LEVELS = {
    "off": dict(led=(72, 54, 34), seen=(104, 90, 72), name="off: a dim warm grey (always clearly visible)"),
    "on": dict(led=(255, 190, 110), seen=(240, 222, 178), name="on / selected: warm ivory"),
    "active": dict(led=(255, 140, 16), seen=(255, 190, 70), name="active / held: bright gold"),
}

# pads 1..8 the top row, 9..16 the bottom row (they read like text, as in our other firmwares)
PADS = [
    ("CONCERT", "tone"), ("STUDIO", "tone"), ("VINTAGE", "tone"), ("JAZZ", "tone"),
    ("DRY", "room"), ("CHAMBER", "room"), ("HALL", "room"), ("CHURCH", "room"),
    ("UNA CORDA", "pedal"), ("SOSTENUTO", "pedal"), ("SUSTAIN", "pedal"), ("RESONANCE", "toggle"),
    ("MELLOW", "brill"), ("NATURAL", "brill"), ("BRILLIANT", "brill"), ("METRONOME", "toggle"),
]
GROUPS = [
    ("tone", "pads 1-4", "the tone: one of four, the current on"),
    ("room", "pads 5-8", "the room: dry or one of three, the current on"),
    ("pedal", "pads 9-11", "the pedals in their order under a grand: gold while held, a tap latches (ivory); "
                           "the PEDAL jack is sustain"),
    ("brill", "pads 13-15", "brilliance in three steps (the knob sets it finer), the current on"),
    ("toggle", "pads 12, 16", "toggles: string resonance, a quiet metronome (proposal); on = ivory"),
]


def example_state():
    """the state drawn in the mockup: CONCERT, HALL, NATURAL, resonance on, sustain held"""
    st = ["off"] * 16
    for i in (0, 6, 11, 13):
        st[i] = "on"
    st[10] = "active"
    return st


def pad(cv, x, y, s, level, label, seen=True):
    col = LEVELS[level]["seen"]

    def fn(d, k):
        d.rounded_rectangle((x * k, y * k, (x + s) * k, (y + s) * k), radius=7 * k, fill=255)
    m = cv.mask(fn)
    glow = {"off": 0.12, "on": 0.35, "active": 0.8}[level]
    cv.glow_cov(m, 9, col, glow)
    # the pad's face: lit from below through the rubber, brightest in the middle
    ys, xs = np.mgrid[0:cv.h, 0:cv.w].astype(np.float32)
    r = np.hypot(xs - (x + s / 2), ys - (y + s / 2)) / (s * 0.75)
    face = np.clip(1.0 - 0.45 * r, 0.3, 1)[..., None] * np.asarray(col, np.float32)
    cv.a = cv.a * (1 - m[..., None]) + face * m[..., None]
    f = font("head", 11)
    cv.text(x + s / 2, y + s + 9, label, f, P["ivory"] if level != "off" else P["ivory_dim"], anchor="c", track=0.5)


def pad_sheet():
    W, H = 960, 600
    cv = Canvas(W, H)
    lacquer(cv, band=(0.5, -30, 60))
    cv.text(48, 34, "ERIK37", font("title", 30), P["ivory"], track=4)
    cv.text(48 + text_w("ERIK37", font("title", 30), 4) + 18, 46, "pad lights", font("italic", 20), P["ivory_dim"])
    hairline(cv, 40, 920, 86, ornament=True)
    st = example_state()
    s, gap = 74, 34
    x0 = (W - (8 * s + 7 * gap)) / 2
    for i, (label, grp) in enumerate(PADS):
        row, col = divmod(i, 8)
        x = x0 + col * (s + gap)
        y = 128 + row * (s + 64)
        pad(cv, round(x), y, s, st[i], label)
        cv.text(x + 6, y + 4, str(i + 1), font("text", 11), (40, 34, 26) if st[i] != "off" else (60, 52, 40))
    # group brackets under the rows
    def bracket(a, b, y, txt):
        xa = x0 + a * (s + gap) + 8
        xb = x0 + b * (s + gap) + s - 8
        cv.hline(xa, xb, y, P["gold_dim"], 0.9, fade=20)
        cv.text((xa + xb) / 2, y + 5, txt, font("italic", 13), P["gold"], anchor="c")
    bracket(0, 3, 230, "tone")
    bracket(4, 7, 230, "room")
    bracket(0, 2, 368, "pedals")
    bracket(3, 3, 368, "toggle")
    bracket(4, 6, 368, "brilliance")
    bracket(7, 7, 368, "toggle")
    # the levels, with the values written to the LEDs
    y = 420
    for k, (lv, d) in enumerate(LEVELS.items()):
        x = 60 + k * 300
        def sw(dr, kk, x=x):
            dr.rounded_rectangle((x * kk, y * kk, (x + 34) * kk, (y + 34) * kk), radius=6 * kk, fill=255)
        m = cv.mask(sw)
        cv.glow_cov(m, 8, d["seen"], {"off": 0.1, "on": 0.3, "active": 0.7}[lv])
        cv.fill(m, d["seen"])
        cv.text(x + 48, y + 1, lv.upper(), font("head", 13), P["ivory"], track=1)
        r, g, b = d["led"]
        cv.text(x + 48, y + 20, f"LED {r}, {g}, {b}", font("text", 12), P["ivory_dim"])
    yy = 482
    for grp, where, txt in GROUPS:
        cv.text(60, yy, where, font("head", 11), P["gold"], track=0.6)
        cv.text(160, yy - 1, txt, font("italic", 13), P["ivory_dim"])
        yy += 21
    return cv
