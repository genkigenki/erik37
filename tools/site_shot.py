#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Erik37: Copyright (C) 2026 M. Geisler (genkigenki/makkuro)
"""The site's shot from the simulator's frames (app/tests/site.txt): shot-erik37.png (the play screen, a chord) and
shot-erik37.webp (the boot, then a phrase; 66 ms a frame).
  site_shot.py FRAME_DIR OUT_DIR"""
import sys
from pathlib import Path

from PIL import Image

src, out = Path(sys.argv[1]), Path(sys.argv[2])
out.mkdir(parents=True, exist_ok=True)
frames = [Image.open(p).convert("RGB") for p in sorted(src.glob("site-*.png"))]
hold = [frames[55]] * 10
frames[56 + 44].save(out / "shot-erik37.png")   # the pedal down, three notes ringing
frames[0].save(out / "shot-erik37.webp", save_all=True, append_images=frames[1:56] + hold + frames[56:], duration=66,
               loop=0, lossless=True)
print(f"{out}/shot-erik37.png, shot-erik37.webp ({len(frames) + len(hold)} frames)")
