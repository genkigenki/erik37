#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Erik37: Copyright (C) 2026 M. Geisler (genkigenki/makkuro)
"""The pad layer's waves (app/dsp/erk_pad.h): one cycle of a soft string-pad tone in 256 points (and a guard point
for the interpolation), harmonics n at 1 / n^1.6, in three widths so that no harmonic goes past ~11 kHz: 16 harmonics
(f0 below 700 Hz), 8 (below 1400 Hz) and 4.

  gen_pad.py app/dsp/erk_pad.h"""
import sys

import numpy as np

N = 256
PEAK = 24000
out = ["/* SPDX-License-Identifier: MPL-2.0", " * Erik37: Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */",
       "/* The pad layer's waves (tools/gen_pad.py): one cycle in 256 points and a guard point; 16, 8 and 4 harmonics",
       " * at 1 / n^1.6 */", "#pragma once", "#include <stdint.h>"]
for nh in (16, 8, 4):
    x = np.arange(N + 1) * 2 * np.pi / N
    y = sum(np.sin(n * x) / n ** 1.6 for n in range(1, nh + 1))
    y = np.round(y / np.max(np.abs(y[:N])) * PEAK).astype(int)
    rows = [", ".join(str(v) for v in y[i:i + 16]) for i in range(0, N + 1, 16)]
    out.append(f"static const int16_t erk_pad_w{nh}[{N + 1}] = {{\n    " + ",\n    ".join(rows) + "};")
open(sys.argv[1], "w", newline="\n").write("\n".join(out) + "\n")
