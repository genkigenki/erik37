#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Erik37: Copyright (C) 2026 M. Geisler (genkigenki/makkuro)
"""The screens against their golden images (tests/golden/): every pixel the same.
  compare_shots.py SHOT_DIR [--update] [--board fm1]     (--update: the shots become the goldens)"""
import shutil
import sys
from pathlib import Path

from PIL import Image, ImageChops

GOLD = Path(__file__).resolve().parent / "golden"
shots = Path(sys.argv[1])
update = "--update" in sys.argv
GOLD = GOLD / sys.argv[sys.argv.index("--board") + 1] if "--board" in sys.argv else GOLD   # golden/fm1: the FM-1's
names = sorted(p.name for p in shots.glob("*.png") if p.name[:4] in ("boot", "play", "tone", "room", "setu", "abou"))
bad = 0
GOLD.mkdir(exist_ok=True)
for n in names:
    g = GOLD / n
    if update or not g.exists():
        shutil.copy(shots / n, g)
        print(f"  new  {n}")
        continue
    a, b = Image.open(shots / n).convert("RGB"), Image.open(g).convert("RGB")
    diff = ImageChops.difference(a, b).getbbox() if a.size == b.size else (0, 0) + a.size
    print(f"  {'ok  ' if not diff else 'FAIL'} {n}" + (f" differs in {diff}" if diff else ""))
    bad += bool(diff)
missing = [p.name for p in GOLD.glob("*.png") if p.name not in names]
for n in missing:
    print(f"  FAIL {n}: no shot")
sys.exit(1 if bad or missing else 0)
