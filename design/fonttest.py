"""Legibility test of the OFL serif candidates at the firmware's 240 x 240 sizes.

    python fonttest.py   ->  fonttest.png (true size) and fonttest@4x.png

Every face is rasterised like the firmware will draw it: anti-aliased, the alpha cut to 4 bits (16 levels,
as speak37's gen_aa_font.py stores glyphs), ivory on piano black, then the result quantised to RGB565.
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
F = HERE / "fonts"
BG, INK, GOLD = (8, 8, 10), (236, 226, 204), (204, 168, 96)

FACES = [
    ("Cormorant Garamond", "CormorantGaramond[wght].ttf", 600),
    ("Cinzel", "Cinzel[wght].ttf", 500),
    ("Libre Caslon Text", "LibreCaslonText[wght].ttf", 400),
    ("Libre Caslon Text Italic", "LibreCaslonText-Italic[wght].ttf", 400),
    ("Inter Tight (sans)", "InterTight[wght].ttf", 500),
]
SIZES = [10, 11, 12, 13, 15]


def font(file, size, wght=None):
    f = ImageFont.truetype(str(F / file), size)
    if wght is not None:
        try:
            f.set_variation_by_axes([wght])
        except OSError:
            pass
    return f


def q4(a):
    return a.point(lambda v: (v * 15 + 127) // 255 * 17)


def to565(img):
    def q(c, bits):
        m = (1 << bits) - 1
        return lambda v: ((v * m + 127) // 255) * 255 // m
    r, g, b = img.split()
    return Image.merge("RGB", (r.point(q(0, 5)), g.point(q(0, 6)), b.point(q(0, 5))))


def text(img, xy, s, f, col, track=0):
    m = Image.new("L", img.size, 0)
    d = ImageDraw.Draw(m)
    x, y = xy
    for ch in s:
        d.text((x, y), ch, font=f, fill=255)
        x += d.textlength(ch, font=f) + track
    img.paste(Image.new("RGB", img.size, col), (0, 0), q4(m))
    return x


def main():
    W = 480
    rows = len(FACES) * (len(SIZES) * 17 + 16)
    img = Image.new("RGB", (W, rows + 8), BG)
    lab = font("InterTight[wght].ttf", 9, 500)
    y = 4
    for name, file, w in FACES:
        text(img, (4, y), name.upper(), lab, GOLD, 1)
        y += 14
        for s in SIZES:
            f = font(file, s, w)
            text(img, (4, y), f"{s}", lab, (120, 112, 100))
            x = text(img, (22, y), "CONCERT  HALL  K1 VELOCITY", f, INK, 1)
            text(img, (x + 10, y), "Brilliance 64", f, INK)
            y += 17
        y += 2
    img = to565(img)
    img.save(HERE / "fonttest.png")
    img.resize((img.width * 4, img.height * 4), Image.NEAREST).save(HERE / "fonttest@4x.png")


if __name__ == "__main__":
    main()
