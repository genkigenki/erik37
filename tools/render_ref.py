#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Erik37: Copyright (C) 2026 M. Geisler (genkigenki/makkuro)
"""Reference renders straight from a source's own recordings (float, stereo, full length, every velocity layer,
no compression, no loops, no effects): what the engine is measured against.

  render_ref.py SOURCE EVENTS.txt OUT.wav      the phrase: nearest sampled note (re-pitched if needed), nearest
                                               layer, the same velocity curve and loudness match as the bank,
                                               released notes fade with a 0.12 s damper
  render_ref.py SOURCE --notes OUT.wav KEY:VEL...   the raw files of those notes, untouched, one after another"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np  # noqa: E402
from scipy import signal  # noqa: E402

import eriklib as E  # noqa: E402
from build_bank import kweight_rms, TARGET_RMS, NODAMP_KEY  # noqa: E402

FS = 44100
_cache = {}


def sample(src, key, vel):
    k = min(src.notes, key=lambda q: abs(q - key))
    v, p = min(src.notes[k], key=lambda q: abs(q[0] - vel))
    if (p, key) in _cache:
        return _cache[p, key]
    x, fs = E.read_audio(p)
    if x.shape[1] == 1:
        x = np.repeat(x, 2, 1)
    m = E.mono(x, fs)
    x = x[E.onset(m, fs):]
    if key != k:                                  # re-pitch by resampling (a ratio of 2^(n/12))
        r = 2 ** ((key - k) / 12)
        n = int(len(x) / r)
        t = np.arange(n) * r
        x = np.stack([np.interp(t, np.arange(len(x)), x[:, c]) for c in range(2)], 1)
    g = TARGET_RMS / kweight_rms(x[:int(0.25 * FS)].mean(1), FS)
    _cache[p, key] = x * g
    return _cache[p, key]


def phrase(src, events, out):
    ev = []
    for ln in open(events):
        if ln.startswith("#") or not ln.strip():
            continue
        f = ln.split()
        ev.append((float(f[0]), f[1], int(f[2]), int(f[3]) if len(f) > 3 else 0))
    end = max(e[0] for e in ev) + 4
    y = np.zeros((int(end * FS) + FS, 2))
    sus = False
    held = {}                                     # key -> list of [start, vel, end or None]
    notes = []
    for t, ty, a, b in ev:
        if ty == "on":
            for n in notes:                       # re-strike: the old note stops
                if n[0] == a and n[3] is None:
                    n[3] = t
            notes.append([a, t, b, None, False])
        elif ty == "off":
            for n in notes:
                if n[0] == a and n[3] is None and not n[4]:
                    if sus:
                        n[4] = True               # held by the pedal
                    else:
                        n[3] = t
        elif ty == "sus":
            on = a >= 64
            if sus and not on:
                for n in notes:
                    if n[4] and n[3] is None:
                        n[3] = t
            sus = on
    for key, t, vel, tend, _ in notes:
        x = sample(src, key, vel) * (vel / 127) ** 1.8
        i0 = int(t * FS)
        n = min(len(x), len(y) - i0)
        x = x[:n].copy()
        if tend is not None and key < NODAMP_KEY:
            j = int((tend - t) * FS)
            if j < n:
                x[j:] *= np.exp(-np.arange(n - j) / (0.12 * FS))[:, None]
        y[i0:i0 + n] += x
    E.write_wav(out, y * 0.25)
    print(f"{out}: {len(notes)} notes")


def raw_notes(src, out, specs):
    parts = []
    for s in specs:
        key, vel = map(int, s.split(":"))
        k = min(src.notes, key=lambda q: abs(q - key))
        v, p = min(src.notes[k], key=lambda q: abs(q[0] - vel))
        x, fs = E.read_audio(p)
        if x.shape[1] == 1:
            x = np.repeat(x, 2, 1)
        x = x[E.onset(E.mono(x, fs), fs):][:int(3.5 * fs)]
        x[-int(0.1 * fs):] *= np.linspace(1, 0, int(0.1 * fs))[:, None]
        parts += [x, np.zeros((int(0.5 * fs), 2))]
    E.write_wav(out, np.concatenate(parts))


if __name__ == "__main__":
    src = E.source(sys.argv[1])
    if sys.argv[2] == "--notes":
        raw_notes(src, sys.argv[3], sys.argv[4:])
    else:
        phrase(src, sys.argv[2], sys.argv[3])
