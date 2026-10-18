"""Erik37 design kit: palette, fonts and the pixel primitives the mockups are drawn with.

Everything here is meant to map one to one onto firmware drawing code:
  * a 240 x 240 float canvas (the firmware: an RGB565 frame buffer), finally quantised to RGB565 with a
    4 x 4 ordered dither (the firmware: the same Bayer table added before the shift, see to565())
  * shapes are filled with 4 x 4 supersampled coverage (the firmware: 16-level AA coverage, as its AA fonts)
  * text: TTF glyphs pre-rasterised with a 4-bit alpha (speak37 tools/gen_aa_font.py does exactly this)
  * light (glows, glints) is *added*, as vector37's cv_add()/glow()
"""
import math
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = Path(__file__).resolve().parent
FONTS = HERE / "fonts"
S = 240           # the screen
SS = 4            # supersampling of shapes

# ---------------------------------------------------------------- palette (RGB888; DESIGN.md lists RGB565)
P = dict(
    lacq0=(5, 5, 7),            # piano lacquer, deepest
    lacq1=(13, 13, 16),         # lacquer, lit
    lacq_hi=(44, 44, 50),       # lacquer reflection peak (used at low alpha)
    felt=(58, 30, 32),          # damper felt (a deep wine, the one quiet colour)
    felt_hi=(96, 58, 56),
    wood=(54, 38, 26),          # soundboard / bridge (very dark spruce)
    gold=(201, 164, 92),        # brass line, main accent
    gold_hi=(246, 218, 156),    # glint, played string
    gold_dim=(112, 90, 52),     # inactive brass
    gold_deep=(64, 50, 28),     # plate in shadow, tracks
    copper=(160, 104, 64),      # bass strings
    steel=(150, 150, 156),      # treble strings
    ivory=(238, 228, 206),      # text, ivory keys
    ivory_dim=(150, 142, 126),  # labels
    grey=(78, 72, 64),          # inactive text, warm grey
)


def rgb565(c):
    r, g, b = c
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def mix(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def scale(c, k):
    return tuple(v * k for v in c)


# ---------------------------------------------------------------- fonts
FACE = dict(
    title=("Cinzel[wght].ttf", 600),         # the name, big headings
    head=("Cinzel[wght].ttf", 600),          # headings and knob labels (caps only)
    text=("LibreCaslonText[wght].ttf", 400),  # values, captions (mixed case, lining figures)
    italic=("LibreCaslonText-Italic[wght].ttf", 400),
    italic_b=("LibreCaslonText-Italic[wght].ttf", 700),
)
_fcache = {}


def font(face, size):
    key = (face, size)
    if key not in _fcache:
        file, w = FACE[face]
        f = ImageFont.truetype(str(FONTS / file), size)
        try:
            f.set_variation_by_axes([w])
        except OSError:
            pass
        _fcache[key] = f
    return _fcache[key]


_dd = ImageDraw.Draw(Image.new("L", (1, 1)))


def text_w(s, f, track=0):
    return sum(_dd.textlength(ch, font=f) for ch in s) + track * (len(s) - 1)


# ---------------------------------------------------------------- the canvas
class Canvas:
    def __init__(self, w=S, h=S, bg=(0, 0, 0)):
        self.w, self.h = w, h
        self.a = np.zeros((h, w, 3), np.float32)
        self.a[:] = bg

    # ---- coverage helpers
    def mask(self, fn):
        """fn(draw, k) draws white on an L image k times the size; returns coverage 0..1 (16 levels)."""
        m = Image.new("L", (self.w * SS, self.h * SS), 0)
        fn(ImageDraw.Draw(m), SS)
        m = m.resize((self.w, self.h), Image.BOX)
        c = np.asarray(m, np.float32) / 255.0
        return np.round(c * 15) / 15

    def fill(self, cov, col, alpha=1.0):
        cov = cov[..., None] * alpha
        self.a = self.a * (1 - cov) + np.asarray(col, np.float32) * cov

    def add(self, cov, col, k=1.0):
        self.a += cov[..., None] * np.asarray(col, np.float32) * k

    def shape(self, fn, col, alpha=1.0):
        self.fill(self.mask(fn), col, alpha)

    def rect(self, x0, y0, x1, y1, col, alpha=1.0):
        """pixel rectangle [x0, x1) x [y0, y1)"""
        x0, y0, x1, y1 = (int(round(v)) for v in (x0, y0, x1, y1))
        sub = self.a[max(0, y0):max(0, y1), max(0, x0):max(0, x1)]
        sub[:] = sub * (1 - alpha) + np.asarray(col, np.float32) * alpha

    def px(self, x, y, col, alpha=1.0):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.a[y, x] = self.a[y, x] * (1 - alpha) + np.asarray(col, np.float32) * alpha

    def addpx(self, x, y, col, k):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.a[y, x] += np.asarray(col, np.float32) * k

    def glow(self, cx, cy, r, col, k):
        """additive soft light, falloff 1 / (1 + (d / r)^2)^2 (firmware: a small precomputed table)"""
        R = int(r * 4)
        ys, xs = np.mgrid[max(0, int(cy) - R):min(self.h, int(cy) + R + 1), max(0, int(cx) - R):min(self.w, int(cx) + R + 1)]
        if xs.size == 0:
            return
        d2 = ((xs - cx) ** 2 + (ys - cy) ** 2) / (r * r)
        f = 1.0 / (1 + d2) ** 2
        self.a[ys, xs] += f[..., None] * np.asarray(col, np.float32) * k

    def glow_cov(self, cov, radius, col, k):
        """blurred copy of a coverage mask, added as light (glow of lines and type)"""
        m = Image.fromarray((np.clip(cov, 0, 1) * 255).astype(np.uint8))
        b = np.asarray(m.filter(ImageFilter.GaussianBlur(radius)), np.float32) / 255.0
        self.add(b, col, k)

    # ---- lines
    def hline(self, x0, x1, y, col, alpha=1.0, fade=0):
        """1 px line; fade: the ends fade over that many pixels (the gold hairlines)"""
        for x in range(int(x0), int(x1)):
            a = alpha
            if fade:
                a *= min(1.0, (x - x0 + 1) / fade, (x1 - x) / fade)
            self.px(x, int(y), col, max(0, a))

    def vline(self, x, y0, y1, col, alpha=1.0, fade=0):
        for y in range(int(y0), int(y1)):
            a = alpha
            if fade:
                a *= min(1.0, (y - y0 + 1) / fade, (y1 - y) / fade)
            self.px(int(x), y, col, max(0, a))

    def lozenge(self, cx, cy, r, col, alpha=1.0, outline=False):
        def fn(d, k):
            pts = [(cx * k, (cy - r) * k), ((cx + r) * k, cy * k), (cx * k, (cy + r) * k), ((cx - r) * k, cy * k)]
            pts = [(x + k / 2, y + k / 2) for x, y in pts]
            if outline:
                d.polygon(pts, outline=255, width=k)
            else:
                d.polygon(pts, fill=255)
        self.shape(fn, col, alpha)

    # ---- text (4-bit alpha glyphs)
    def text_cov(self, x, y, s, f, track=0):
        m = Image.new("L", (self.w, self.h), 0)
        d = ImageDraw.Draw(m)
        for ch in s:
            d.text((x, y), ch, font=f, fill=255)
            x += d.textlength(ch, font=f) + track
        c = np.asarray(m, np.float32) / 255.0
        return np.round(c * 15) / 15

    def text(self, x, y, s, f, col, track=0, anchor="l", alpha=1.0, glow=0.0):
        w = text_w(s, f, track)
        if anchor == "c":
            x -= w / 2
        elif anchor == "r":
            x -= w
        x = round(x)
        cov = self.text_cov(x, y, s, f, track)
        if glow:
            self.glow_cov(cov, 2.0, col, glow)
        self.fill(cov, col, alpha)
        return w

    # ---- output
    def image(self, dither=True):
        return to565(self.a, dither)

    def image888(self):
        """for the marketing art (not a firmware screen): full colour, a fine noise against banding"""
        n = np.random.default_rng(1).uniform(-0.5, 0.5, self.a.shape[:2])[..., None]
        return Image.fromarray(np.clip(self.a + n, 0, 255).round().astype(np.uint8), "RGB")


BAYER4 = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]], np.float32) / 16.0


def to565(a, dither=True):
    """RGB565 quantisation with an ordered 4 x 4 dither (the firmware adds BAYER4[y & 3][x & 3] scaled to one
    565 step before truncating; it is what keeps the dark lacquer gradients from banding)"""
    h, w, _ = a.shape
    a = np.clip(a, 0, 255)
    t = np.tile(BAYER4, (h // 4 + 1, w // 4 + 1))[:h, :w] if dither else np.full((h, w), 0.5, np.float32)
    out = np.empty_like(a)
    for ch, bits in ((0, 5), (1, 6), (2, 5)):
        m = (1 << bits) - 1
        v = np.floor(a[..., ch] * m / 255.0 + t)
        out[..., ch] = np.clip(v, 0, m) * 255.0 / m
    return Image.fromarray(out.round().astype(np.uint8), "RGB")


def up(img, k=4):
    return img.resize((img.width * k, img.height * k), Image.NEAREST)


# ---------------------------------------------------------------- shared backgrounds
def lacquer(cv, seed=0, band=(0.62, -40, 34), strength=1.0):
    """black piano lacquer: a cool vertical gradient and one broad soft reflection band (a window far away),
    plus a fine second streak. band = (slope, offset, width) in screen pixels."""
    h, w = cv.h, cv.w
    ys, xs = np.mgrid[0:h, 0:w].astype(np.float32)
    t = ys / h
    base = np.asarray(P["lacq1"], np.float32) * (1 - t[..., None]) + np.asarray(P["lacq0"], np.float32) * t[..., None]
    sl, off, wd = band
    u = (xs * sl + ys - off * h / S - w * 0.5 * sl) / (wd * h / S)
    refl = np.exp(-u * u) * 0.55 + np.exp(-((u - 2.6) * 3.0) ** 2) * 0.22
    refl *= (1.0 - 0.6 * t)
    cv.a = base + refl[..., None] * (np.asarray(P["lacq_hi"], np.float32) - np.asarray(P["lacq0"], np.float32)) * 0.5 * strength


def hairline(cv, x0, x1, y, col=None, alpha=0.9, ornament=True):
    """the gold rule: fades at both ends; with the ornament a small lozenge in a gap at the centre"""
    col = col or P["gold"]
    cx = (x0 + x1) / 2
    if ornament:
        cv.hline(x0, cx - 7, y, col, alpha, fade=(cx - x0) * 0.7)
        cv.hline(cx + 7, x1, y, col, alpha, fade=(x1 - cx) * 0.7)
        cv.lozenge(cx - 0.5, y, 2.5, col, alpha)
    else:
        cv.hline(x0, x1, y, col, alpha, fade=(x1 - x0) * 0.3)
