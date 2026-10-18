#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Erik37: Copyright (C) 2026 M. Geisler (genkigenki/makkuro)
"""The firmware's generated headers (fm1.toml gen):

  gen_assets.py GEN_DIR

  erk_bank_data.h   the sound bank (assets/erik37.bank, built by tools/build_bank.py from Salamander) as a C array
  erk_fonts.h       the anti-aliased fonts (Cinzel, Libre Caslon Text: SIL OFL 1.1), 4-bit alpha, as the mockups
                    rasterise them (design/erik37_gfx.py)
  erk_art.h         the static backgrounds (play, settings, boot), drawn by the design's own code, as a 256-colour
                    palette and LZ4 blocks of indices; the firmware dithers them to RGB565 as the mockups do
Needs Pillow and NumPy."""
import os
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
APP = HERE.parent
DESIGN = APP.parent / "design"
BASE_TOOLS = APP.parent / "base" / "tools"
sys.path.insert(0, str(DESIGN))
sys.path.insert(0, str(BASE_TOOLS))

import numpy as np  # noqa: E402
from PIL import Image, ImageDraw  # noqa: E402

import lz4blk  # noqa: E402
import erik37_gfx as G  # noqa: E402
import screens as SCR  # noqa: E402
from erik37_gfx import P  # noqa: E402

# ---------------------------------------------------------------- fonts
# name, face, px, characters ('\x7f' = middle dot, '\x80' = en dash)
ASCII = "".join(chr(c) for c in range(32, 127)) + "\x7f\x80"
UPPER = " !&'()+-./0123456789:<>?ABCDEFGHIJKLMNOPQRSTUVWXYZ\x7f\x80"
FONTS = [
    ("F_HEAD12", "head", 12, UPPER),
    ("F_HEAD11", "head", 11, UPPER),
    ("F_HEAD9", "head", 9, UPPER),
    ("F_TITLE34", "title", 34, " 37EIKR"),
    ("F_TEXT13", "text", 13, ASCII),
    ("F_TEXT12", "text", 12, ASCII),
    ("F_TEXT11", "text", 11, ASCII),
    ("F_ITAL11", "italic", 11, ASCII),
    ("F_ITAL10", "italic", 10, ASCII),
    ("F_ITAL12", "italic", 12, ASCII),
    ("F_DYN17", "italic_b", 17, " fmp"),
]
UNI = {"\x7f": "·", "\x80": "–"}


def gen_font(name, face, px, chars):
    f = G.font(face, px)
    pad = px
    glyphs = []
    data = bytearray()
    for ch in chars:
        u = UNI.get(ch, ch)
        im = Image.new("L", (px * 3 + 2 * pad, px * 3 + 2 * pad), 0)
        d = ImageDraw.Draw(im)
        d.text((pad, pad), u, font=f, fill=255)
        a = np.round(np.asarray(im, np.float32) / 255.0 * 15).astype(np.uint8)
        ys, xs = np.nonzero(a)
        adv = d.textlength(u, font=f)
        if len(xs):
            x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
        else:
            x0 = x1 = y0 = y1 = pad
        g = a[y0:y1, x0:x1]
        w, h = x1 - x0, y1 - y0
        off = len(data)
        flat = g.flatten().tolist()
        if len(flat) % 2:
            flat.append(0)
        for i in range(0, len(flat), 2):
            data.append(flat[i] << 4 | flat[i + 1])
        glyphs.append((ord(ch), int(x0 - pad), int(y0 - pad), int(w), int(h), int(round(adv * 16)), off))
    lines = [f"static const erk_glyph {name}_G[] = {{"]
    lines += [f"    {{{c}, {xo}, {yo}, {w}, {h}, {adv}, {off}}}," for c, xo, yo, w, h, adv, off in glyphs]
    lines.append("};")
    lines.append(f"static const uint8_t {name}_D[] = {{{', '.join(str(b) for b in data)}}};")
    lines.append(f"static const erk_font {name} = {{{px}, {len(glyphs)}, {name}_G, {name}_D}};")
    return "\n".join(lines), len(data) + 10 * len(glyphs)


# ---------------------------------------------------------------- backgrounds
def play_static():
    """the play screen without anything that moves: no dampers, no lit strings, no keys, no pedals, no text"""
    cv = G.Canvas()
    G.lacquer(cv)
    G.hairline(cv, 10, 230, SCR.RULE_Y, ornament=True)
    interior_static(cv)
    py = 163
    for k in range(3):                                   # the pedal rods; the pedals are drawn by the firmware
        x = 120 + (k - 1) * 22
        cv.vline(x, py - 7, py, P["gold_deep"], 0.9)
    cv.hline(120 - 30, 120 + 31, py - 7, P["gold_dim"], 0.9, fade=8)
    strip_static(cv)
    return cv


def interior_static(cv):
    """screens.interior() without the dampers, the played strings and the keys"""
    rim_x, bridge_y, string_x = SCR.rim_x, SCR.bridge_y, SCR.string_x

    def rimfn(d, k):
        pts = [(7 * k, 27 * k)] + [(rim_x(y) * k, y * k) for y in np.linspace(27, 130, 40)] + [(7 * k, 130 * k)]
        d.polygon(pts, fill=255)
    rim = cv.mask(rimfn)
    ys, xs = np.mgrid[0:240, 0:240].astype(np.float32)
    grain = 0.5 + 0.5 * np.sin(xs * 0.9 + np.sin(ys * 0.05 + xs * 0.02) * 3.0)
    wood = np.asarray(P["wood"], np.float32) * (0.32 + 0.10 * grain[..., None]) * (0.55 + 0.45 * (ys[..., None] / 130))
    cv.a = cv.a * (1 - rim[..., None]) + wood * rim[..., None]

    def platefn(d, k):
        pts = [(9 * k, 29 * k)] + [((rim_x(y) - 2) * k, y * k) for y in np.linspace(29, 122, 40)] + [(9 * k, 122 * k)]
        d.polygon(pts, fill=255)
        inner = [(17 * k, 72 * k)] + [((rim_x(y) - 10) * k, y * k) for y in np.linspace(72, 97, 12)] + [(17 * k, 97 * k)]
        d.polygon(inner, fill=0)
        for xa, xb in ((84, 76), (156, 150)):
            d.polygon([(xa * k, 97 * k), ((xa + 6) * k, 97 * k), ((xb + 6) * k, 72 * k), (xb * k, 72 * k)], fill=255)
        for hx, hy, r in ((38, 50, 8), (80, 47, 9), (124, 50, 8), (162, 60, 6)):
            d.ellipse(((hx - r) * k, (hy - r) * k, (hx + r) * k, (hy + r) * k), fill=0)
    plate = cv.mask(platefn) * rim
    shade = np.clip(1.0 - 0.0045 * (xs * 0.6 + ys * 1.4 - 40) + 0.35 * np.exp(-((xs - 50) ** 2 + (ys - 45) ** 2) / 2500), 0, 1.3)
    gold = (np.asarray(P["gold_deep"], np.float32) * 0.75 + (np.asarray(P["gold_dim"], np.float32) * 1.05
                                                              - np.asarray(P["gold_deep"], np.float32) * 0.75) * shade[..., None])
    cv.a = cv.a * (1 - plate[..., None]) + gold * 0.62 * plate[..., None]
    lit = np.clip(plate - np.roll(np.roll(plate, 1, 0), 1, 1), 0, 1)
    shd = np.clip(plate - np.roll(np.roll(plate, -1, 0), -1, 1), 0, 1)
    cv.add(lit, P["gold_hi"], 0.30)
    cv.a *= (1 - 0.45 * shd)[..., None]
    for y in range(27, 131):
        cv.px(int(rim_x(y)), y, P["gold"], 0.55 * (0.4 + 0.6 * (y - 27) / 103))
    cv.hline(7, int(rim_x(27)), 27, P["gold"], 0.35, fade=40)
    for i in range(37):
        x = string_x(i)
        by = bridge_y(x)
        while rim_x(by) < x + 4 and by < SCR.PIN_Y - 8:
            by += 1
        cv.rect(x - 3, by, x + 3, by + 2, P["wood"], 0.9)
    for i in range(37):
        x = string_x(i)
        y0 = string_top(i)
        bass = i < 12
        col = P["copper"] if bass else P["steel"]
        k = 0.42 if bass else 0.30
        for sx in ((x - 1, x) if bass else (x - 1, x + 1)):
            cv.vline(sx, y0, SCR.PIN_Y, col, k * (0.75 if sx != x - 1 else 1.0))
    for i in range(37):
        x = string_x(i)
        cv.px(x - 1, SCR.PIN_Y + (i & 1) * 2, P["steel"], 0.85)
        cv.px(x + 1, SCR.PIN_Y + 1 - (i & 1), P["steel"], 0.55)
    cv.rect(7, 129, 233, 132, (4, 4, 6), 1.0)
    cv.hline(8, 232, 131, P["gold"], 0.6, fade=30)


def string_top(i):
    x = SCR.string_x(i)
    y0 = int(round(SCR.bridge_y(x))) - 3
    while SCR.rim_x(y0) < x + 4 and y0 < SCR.PIN_Y - 8:
        y0 += 1
    return y0


def strip_static(cv):
    G.hairline(cv, 10, 230, SCR.STRIP_Y, ornament=False, alpha=0.75)
    for i in range(1, 4):
        cv.vline(i * SCR.COL_W, SCR.STRIP_Y + 8, 236, P["gold_deep"], 0.9, fade=10)


def settings_static():
    cv = G.Canvas()
    G.lacquer(cv)
    G.hairline(cv, 10, 230, SCR.RULE_Y, ornament=False)
    strip_static(cv)
    return cv


def boot_static():
    cv = G.Canvas()
    G.lacquer(cv)
    cv.glow(124, 78, 64, (34, 31, 28), 0.85)
    return cv


def pack_bg(name, cv):
    a = np.clip(cv.a, 0, 255)
    img = Image.fromarray(a.round().astype(np.uint8), "RGB")
    q = img.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = q.getpalette()[:768]
    pal += [0] * (768 - len(pal))
    idx = np.asarray(q, np.uint8).tobytes()
    blocks, offs = bytearray(), []
    for i in range(0, len(idx), 4096):
        c = lz4blk.compress(idx[i:i + 4096])
        assert lz4blk.decompress(c) == idx[i:i + 4096]
        offs.append(len(blocks))
        blocks += c
    offs.append(len(blocks))
    s = [f"static const uint8_t {name}_PAL[768] = {{{', '.join(map(str, pal))}}};",
         f"static const uint32_t {name}_OFF[{len(offs)}] = {{{', '.join(map(str, offs))}}};",
         f"static const uint8_t {name}_LZ[{len(blocks)}] = {{{', '.join(map(str, blocks))}}};",
         f"static const erk_art {name} = {{{name}_PAL, {name}_OFF, {name}_LZ, {len(offs) - 1}}};"]
    return "\n".join(s), len(blocks) + 768


def main():
    gen = Path(sys.argv[1])
    gen.mkdir(parents=True, exist_ok=True)
    bank = (APP / "assets" / "erik37.bank").read_bytes()
    with open(gen / "erk_bank_data.h", "w") as f:
        f.write("/* generated by tools/gen_assets.py from assets/erik37.bank: the sound bank (Salamander Grand Piano V3\n"
                " * by Alexander Holm, CC BY 3.0, resampled, trimmed, looped and ADPCM-compressed) */\n")
        f.write(f"#define ERK_BANK_BYTES {len(bank)}u\n")
        f.write(f"static const uint8_t ERK_BANK[{len(bank)}] __attribute__((aligned(4))) = {{\n")
        for i in range(0, len(bank), 32):
            f.write("    " + ",".join(str(b) for b in bank[i:i + 32]) + ",\n")
        f.write("};\n")
    total = 0
    with open(gen / "erk_fonts.h", "w") as f:
        f.write("/* generated by tools/gen_assets.py: Cinzel and Libre Caslon Text (SIL OFL 1.1), 4-bit alpha */\n")
        for spec in FONTS:
            src, n = gen_font(*spec)
            total += n
            f.write(src + "\n")
    print(f"gen_assets: fonts {total} B")
    with open(gen / "erk_art.h", "w") as f:
        f.write("/* generated by tools/gen_assets.py: the static backgrounds (design/), palette + LZ4 */\n")
        tops = ", ".join(str(string_top(i)) for i in range(37))
        f.write(f"static const uint8_t STRING_TOP[37] = {{{tops}}};\n")
        art = 0
        for name, fn in (("ART_PLAY", play_static), ("ART_SET", settings_static), ("ART_BOOT", boot_static)):
            src, n = pack_bg(name, fn())
            art += n
            f.write(src + "\n")
    print(f"gen_assets: bank {len(bank)} B, art {art} B")


if __name__ == "__main__":
    main()
