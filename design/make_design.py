"""Erik37 design: renders every mockup and the marketing art.

    python make_design.py           # all stills, the storyboard, the pads, the marketing art, sheet.png
    python make_design.py --anim    # also the animations (boot.webp, site/shot-erik37.webp)

Needs Python 3 with Pillow and NumPy. Fonts (SIL OFL 1.1) are in fonts/. Writes:
  boot/boot-NN.png (+ @4x)  the boot storyboard, boot/storyboard.png
  screens/*.png (+ @4x)     play, tone & touch, room
  pads/pads.png             the pad-light scheme
  marketing/                banner (1280 x 400), thumbnail (1280 x 720), card (1200 x 630)
  site/shot-erik37.png      the site card's screen (240 x 240), shot-erik37.webp with --anim
  sheet.png                 a contact sheet of all of it
"""
import math
import sys
from pathlib import Path

from PIL import Image, ImageDraw

import boot
import marketing
import pads
import pages
import screens
from erik37_gfx import HERE, P, font, up

OUT = {k: HERE / k for k in ("boot", "screens", "pads", "marketing", "site")}

CAPTIONS = ["the dark: a rim of gold", "the piano", "the lid lifts", "the gold plate shows", "open", "a glint runs",
            "along the rim", "the name", "ERIK37 by genkigenki"]

CHORD = {0: 0.55, 7: 0.75, 16: 1.0, 19: 0.85, 24: 0.6, 28: 0.35}     # C3 G3 E4 G4 C5 E5, decaying


def save(img, folder, name, big=True):
    folder.mkdir(exist_ok=True)
    img.save(folder / f"{name}.png")
    if big:
        up(img, 4).save(folder / f"{name}@4x.png")


def render_stills():
    shots = {}
    for i, st in enumerate(boot.STORY):
        im = boot.boot_frame(*st).image()
        save(im, OUT["boot"], f"boot-{i + 1:02d}")
        shots[f"boot-{i + 1:02d}"] = im
    shots["play"] = screens.play_screen(0, 1, CHORD, sustain=1.0, vel=92).image()
    shots["play-idle"] = screens.play_screen(0, 1, {}, sustain=0.0, vel=64).image()
    shots["play-halfpedal"] = screens.play_screen(2, 2, {5: 0.9, 12: 0.7, 17: 0.5}, sustain=0.5, una_corda=True,
                                                  vel=40, octave=-1, knobs=None).image()
    shots["play-sostenuto"] = screens.play_screen(3, 0, {0: 0.8, 12: 0.4, 26: 1.0, 30: 0.8}, sostenuto=(0, 12), vel=110,
                                                  active=2).image()
    shots["tone-touch"] = pages.tone_touch_screen().image()
    shots["room"] = pages.room_screen().image()
    shots["room-chamber"] = pages.room_screen(0, 0.2, 0.30, 0.45, active=0).image()
    shots["room-church"] = pages.room_screen(2, 0.9, 0.55, 1.0, active=1).image()
    for k, im in shots.items():
        if not k.startswith("boot"):
            save(im, OUT["screens"], k)
    return shots


def storyboard(shots):
    n = len(boot.STORY)
    cols = 5
    rows = math.ceil(n / cols)
    s, g = 480, 28
    W = cols * s + (cols + 1) * g
    H = rows * (s + 52) + g + 70
    img = Image.new("RGB", (W, H), (6, 6, 8))
    d = ImageDraw.Draw(img)
    d.text((g, 22), "ERIK37  ·  boot", font=font("title", 26), fill=P["ivory"])
    for i in range(n):
        x = g + (i % cols) * (s + g)
        y = 70 + (i // cols) * (s + 52)
        img.paste(up(shots[f"boot-{i + 1:02d}"], 2), (x, y))
        d.text((x, y + s + 10), f"{i + 1}", font=font("head", 15), fill=P["gold"])
        d.text((x + 24, y + s + 8), CAPTIONS[i], font=font("italic", 17), fill=P["ivory_dim"])
    OUT["boot"].mkdir(exist_ok=True)
    img.save(OUT["boot"] / "storyboard.png")
    return img


def sheet(shots, pad_img, ban, thumb, card):
    """the contact sheet: storyboard row, the screens at 2x, the pads, the marketing art"""
    g = 30
    W = 2 * 1280 + 3 * g
    s = 480
    rows = []
    boot_keys = [f"boot-{i:02d}" for i in (1, 3, 5, 6, 9)]
    scr_keys = ["play", "play-idle", "play-halfpedal", "tone-touch", "room"]
    H = 90 + (s + 50) * 2 + g + 600 + g + 720 + g
    img = Image.new("RGB", (W, H), (6, 6, 8))
    d = ImageDraw.Draw(img)
    d.text((g, 26), "ERIK37", font=font("title", 36), fill=P["ivory"])
    d.text((g + 190, 38), "design  ·  240 x 240 screens at 2x (nearest neighbour), RGB565", font=font("italic", 20),
           fill=P["ivory_dim"])
    y = 90
    gap = (W - 5 * s) / 6
    for row, keys in enumerate((boot_keys, scr_keys)):
        for i, k in enumerate(keys):
            x = round(gap + i * (s + gap))
            img.paste(up(shots[k], 2), (x, y))
            d.text((x, y + s + 10), k, font=font("italic", 18), fill=P["ivory_dim"])
        y += s + 50
    y += g
    img.paste(pad_img, (g, y))
    img.paste(card.resize((round(card.width * 600 / card.height), 600), Image.LANCZOS), (g + 960 + g, y))
    y += 600 + g
    bg = Image.new("RGBA", ban.size, (6, 6, 8, 255))
    bg.alpha_composite(ban)
    img.paste(bg.convert("RGB"), (g, y))
    img.paste(thumb, (g + 1280 + g, y))
    img.save(HERE / "sheet.png")


def anim_boot():
    """the boot as an animation: ~3.6 s, the lid eased open, the glint, the name"""
    frames = []
    N = 54
    for f in range(N):
        t = f / (N - 1)
        pres = min(1.0, 0.25 + t / 0.12)
        lid = 42 * (0.5 - 0.5 * math.cos(math.pi * min(1, max(0, (t - 0.12) / 0.42))))
        gl = (t - 0.52) / 0.32 if 0.52 <= t <= 0.84 else None
        title = min(1, max(0, (t - 0.62) / 0.2))
        by = min(1, max(0, (t - 0.8) / 0.15))
        frames.append(boot.boot_frame(lid, pres, gl, title, by).image())
    return frames


def anim_play():
    """a phrase on the play screen: notes struck and decaying, the sustain pedal down for the second half"""
    seq = [(0, 0), (4, 7), (8, 16), (12, 19), (16, 24), (22, 28), (28, 12), (34, 19), (40, 23)]
    frames = []
    for f in range(56):
        notes = {}
        sus = 1.0 if 24 <= f < 50 else 0.0
        for t0, n in seq:
            if f >= t0:
                age = f - t0
                lvl = math.exp(-age / (14 if sus or age < 6 else 4))
                if lvl > 0.06:
                    notes[n] = max(notes.get(n, 0), lvl)
        vel = 70 + 6 * (len([1 for t0, _ in seq if t0 <= f]) % 5)
        frames.append(screens.play_screen(0, 1, notes, sustain=sus, vel=vel).image())
    return frames


def main():
    shots = render_stills()
    storyboard(shots)
    pad_img = pads.pad_sheet().image888()
    OUT["pads"].mkdir(exist_ok=True)
    pad_img.save(OUT["pads"] / "pads.png")
    OUT["marketing"].mkdir(exist_ok=True)
    ban = marketing.banner(shots["play"], shots["boot-09"])
    ban.save(OUT["marketing"] / "banner-1280x400.png")
    thumb = marketing.thumbnail(shots["play"], shots["boot-09"])
    thumb.save(OUT["marketing"] / "thumbnail-1280x720.png")
    card = marketing.social_card(shots["play"], shots["boot-09"])
    card.save(OUT["marketing"] / "card-1200x630.png")
    OUT["site"].mkdir(exist_ok=True)
    shots["play"].save(OUT["site"] / "shot-erik37.png")
    sheet(shots, pad_img, ban, thumb, card)
    if "--anim" in sys.argv:
        b = anim_boot()
        hold = [b[-1]] * 14
        b[0].save(OUT["boot"] / "boot.webp", save_all=True, append_images=b[1:] + hold, duration=66, loop=0, lossless=True)
        p = anim_play()
        seq = b + hold + p
        seq[0].save(OUT["site"] / "shot-erik37.webp", save_all=True, append_images=seq[1:], duration=66, loop=0,
                    lossless=True)
    print("done")


if __name__ == "__main__":
    main()
