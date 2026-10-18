"""Erik37 marketing art: the GitHub README banner (1280 x 400, the family of work/banners/make_banners.py: a
rounded card, transparent outside, the real screens nearest-neighbour), the YouTube thumbnail (1280 x 720)
and the site / social card (1200 x 630). All built from the real 240 x 240 mockups."""
import math

import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter

from erik37_gfx import P, Canvas, font, lacquer

IV, IVD, GOLD, GOLDH, GOLDD = (P[k] for k in ("ivory", "ivory_dim", "gold", "gold_hi", "gold_dim"))


def c8(c, a=255):
    return tuple(int(v) for v in c) + (a,)


def scaled(im, size):
    """a 240 x 240 screen at any size: nearest-neighbour up to a multiple, then a box filter down"""
    if size % 240 == 0:
        return im.resize((size, size), Image.NEAREST)
    k = math.ceil(size / 240) * 2
    return im.resize((240 * k, 240 * k), Image.NEAREST).resize((size, size), Image.BOX)


def card_bg(w, h, band=(0.35, -20, 140)):
    cv = Canvas(w, h)
    lacquer(cv, band=band)
    img = cv.image888().convert("RGBA")
    # a warm pool of light behind the screens
    g = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    return img, g


def glow_layer(size, fn, radius, alpha=1.0):
    layer = Image.new("RGBA", size, (0, 0, 0, 0))
    fn(ImageDraw.Draw(layer))
    g = layer.filter(ImageFilter.GaussianBlur(radius))
    if alpha != 1.0:
        g.putalpha(g.getchannel("A").point(lambda v: int(min(255, v * alpha))))
    return layer, g


def spaced(d, x, y, s, f, fill, track):
    for ch in s:
        d.text((x, y), ch, font=f, fill=fill)
        x += d.textlength(ch, font=f) + track
    return x


def spaced_w(s, f, track):
    d = ImageDraw.Draw(Image.new("L", (1, 1)))
    return sum(d.textlength(ch, font=f) for ch in s) + track * (len(s) - 1)


def rule(d, x0, x1, y, col=GOLD, alpha=230, orn=True):
    """the gold rule with its lozenge (fading ends)"""
    cx = (x0 + x1) / 2
    n = int(x1 - x0)
    for i in range(n):
        x = x0 + i
        if orn and abs(x - cx) < 12:
            continue
        t = min(1, i / (n * 0.35), (n - i) / (n * 0.35))
        d.point((x, y), fill=c8(col, int(alpha * t)))
    if orn:
        d.polygon([(cx, y - 4), (cx + 4, y), (cx, y + 4), (cx - 4, y)], fill=c8(col, alpha))


def framed(img, shot, x, y, reflect=True):
    """a screen in a slim lacquer bezel with a gold edge, a shadow and its reflection on the 'stage'"""
    w = shot.width
    pad = max(8, w // 28)
    box = (x - pad, y - pad, x + w + pad, y + w + pad)
    sh = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ImageDraw.Draw(sh).rounded_rectangle((box[0] + 4, box[1] + 14, box[2] - 4, box[3] + 18), pad, fill=(0, 0, 0, 200))
    img.alpha_composite(sh.filter(ImageFilter.GaussianBlur(18)))
    gl = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ImageDraw.Draw(gl).rounded_rectangle(box, pad, outline=c8(GOLD, 120), width=3)
    img.alpha_composite(gl.filter(ImageFilter.GaussianBlur(10)))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle(box, pad, fill=(9, 9, 11, 255), outline=c8(GOLDD), width=1)
    d.rounded_rectangle((box[0] + 3, box[1] + 3, box[2] - 3, box[3] - 3), pad - 3, outline=(28, 28, 32, 255), width=1)
    img.paste(shot.convert("RGB"), (x, y))
    if reflect and box[3] + 10 < img.height:
        r = shot.convert("RGB").transpose(Image.FLIP_TOP_BOTTOM).crop((0, 0, w, min(w, img.height - box[3] - 6)))
        m = Image.linear_gradient("L").resize((r.width, r.height)).point(lambda v: int(max(0, 60 - v * 0.5)))
        m = ImageChops.invert(Image.linear_gradient("L").resize((r.width, r.height))).point(lambda v: int(v * 0.10))
        img.paste(r, (x, box[3] + 6), m)


def keys_outline(img, x0, y0, w, h, col=GOLDD, alpha=150):
    """the SMK's 37 keys (22 white, C to C), as fine gold outlines"""
    d = ImageDraw.Draw(img)
    n = 22
    kw = w / n
    for i in range(n):
        x = x0 + i * kw
        d.rectangle((round(x), y0, round(x + kw) - 2, y0 + h), outline=c8(col, alpha), width=1)
    for i in range(n - 1):
        if i % 7 in (0, 1, 3, 4, 5):
            cx = x0 + (i + 1) * kw
            d.rectangle((round(cx - kw * .3), y0, round(cx + kw * .3) - 1, y0 + h * .6), fill=(8, 8, 10, 255),
                        outline=c8(col, alpha), width=1)


def title(img, x, y, size, track, centre=False):
    f = font("title", size)
    w = spaced_w("ERIK37", f, track)
    if centre:
        x = x - w / 2
    lay, g = glow_layer(img.size, lambda d: spaced(d, x, y, "ERIK37", f, c8(GOLD, 255), track), size * 0.25, 0.35)
    img.alpha_composite(g)
    lay, _ = glow_layer(img.size, lambda d: spaced(d, x, y, "ERIK37", f, c8(IV), track), 1)
    img.alpha_composite(lay)
    return x, w


def round_card(img, r=26):
    m = Image.new("L", img.size, 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, img.width - 1, img.height - 1), r, fill=255)
    out = Image.new("RGBA", img.size, (0, 0, 0, 0))
    out.paste(img, (0, 0), m)
    d = ImageDraw.Draw(out)
    d.rounded_rectangle((0, 0, img.width - 1, img.height - 1), r, outline=c8(GOLDD, 160), width=1)
    return out


def banner(play, boot_last):
    W, H = 1280, 400
    img, _ = card_bg(W, H)
    pool = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(pool).ellipse((640, -60, 1300, 460), fill=(60, 50, 34, 60))
    img.alpha_composite(pool.filter(ImageFilter.GaussianBlur(110)))
    d = ImageDraw.Draw(img)
    x, w = title(img, 78, 92, 92, 10)
    rule(d, x - 6, x + w + 6, 222)
    fi = font("italic", 26)
    d.text((x + w / 2, 240), "a grand piano for the M-Vave SMK-37", font=fi, fill=c8(IVD), anchor="ma")
    fs = font("head", 15)
    t = "NEW FIRMWARE  ·  GENKIGENKI"
    spaced(d, x + w / 2 - spaced_w(t, fs, 3) / 2, 288, t, fs, c8(GOLD), 3)
    keys_outline(img, x + 10, 334, w - 20, 26)
    s = 276
    framed(img, scaled(boot_last, s), 636, 52)
    framed(img, scaled(play, s), 636 + s + 40, 52)
    return round_card(img)


def thumbnail(play, boot_last):
    W, H = 1280, 720
    img, _ = card_bg(W, H, band=(0.4, -10, 220))
    pool = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(pool).ellipse((640, 40, 1300, 700), fill=(70, 58, 38, 70))
    img.alpha_composite(pool.filter(ImageFilter.GaussianBlur(140)))
    d = ImageDraw.Draw(img)
    x, w = title(img, 70, 150, 128, 12)
    rule(d, x, x + w, 330)
    fi = font("italic", 46)
    d.text((x + w / 2, 356), "a grand piano", font=fi, fill=c8(IV), anchor="ma")
    d.text((x + w / 2, 414), "in 37 keys", font=fi, fill=c8(IVD), anchor="ma")
    fs = font("head", 20)
    t = "M-VAVE SMK-37  ·  FIRMWARE"
    spaced(d, x + w / 2 - spaced_w(t, fs, 4) / 2, 520, t, fs, c8(GOLD), 4)
    framed(img, scaled(play, 480), 720, 96)
    return img.convert("RGB")


def social_card(play, boot_last):
    W, H = 1200, 630
    img, _ = card_bg(W, H, band=(0.4, -10, 200))
    pool = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(pool).ellipse((600, 20, 1240, 620), fill=(70, 58, 38, 60))
    img.alpha_composite(pool.filter(ImageFilter.GaussianBlur(130)))
    d = ImageDraw.Draw(img)
    x, w = title(img, 76, 160, 104, 10)
    rule(d, x, x + w, 312)
    fi = font("italic", 34)
    d.text((x + w / 2, 334), "a grand piano for the SMK-37", font=fi, fill=c8(IVD), anchor="ma")
    fs = font("head", 17)
    t = "NEW FIRMWARE  ·  GENKIGENKI"
    spaced(d, x + w / 2 - spaced_w(t, fs, 3) / 2, 404, t, fs, c8(GOLD), 3)
    keys_outline(img, x + 14, 470, w - 28, 32)
    framed(img, scaled(play, 480), 668, 64, reflect=False)
    return img.convert("RGB")
