#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Erik37: Copyright (C) 2026 M. Geisler (genkigenki/makkuro)
"""The FM-1 package (build/erik37.fwsc): its identity, the app within the FM-1's slot, Felucca's update loader.
  fm1_pkg_test.py PACKAGE.fwsc APP.bin IDENTITY"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "base" / "tools"))
import fm1_install as I  # noqa: E402
import fm1pkg_make as M  # noqa: E402

raw = Path(sys.argv[1]).read_bytes()
img = Path(sys.argv[2]).stat().st_size
bad = 0


def ok(c, what):
    global bad
    print(f"  {'ok  ' if c else 'FAIL'} {what}")
    bad += not c


product = I.product_of(raw)
ok(product == sys.argv[3], f"identity {product} (want {sys.argv[3]})")
slot = M.BOARDS["fm1"][1]
ok(img <= slot, f"the app ({img} B) fits the FM-1's slot ({slot} B), {slot - img} B spare")
ok(I.LOADER_MARK in raw, "the update loader is Felucca's (its marker)")
sys.exit(1 if bad else 0)
