# SPDX-License-Identifier: MPL-2.0
# Erik37: Copyright (C) 2026 M. Geisler (genkigenki/makkuro)
"""Shared helpers for the offline tools: WAV/AIFF I/O, the sample sources (which file is which note and layer),
onset trimming, mono fold-down, loudness (ITU-R BS.1770 K-weighted, gated)."""
import aifc
import os
import re
import struct
import warnings
import wave

import numpy as np
from scipy import signal

warnings.filterwarnings("ignore", category=DeprecationWarning)

NOTE = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def midi_of(name):
    """'C4' -> 60, 'A#0' / 'Bb0' -> 22"""
    m = re.fullmatch(r"([A-G])([#b]?)(-?\d+)", name)
    if not m:
        raise ValueError(name)
    n = NOTE[m.group(1)] + {"#": 1, "b": -1, "": 0}[m.group(2)]
    return 12 * (int(m.group(3)) + 1) + n


def read_audio(path):
    """-> (float64 array [n, ch] in -1..1, rate)"""
    if path.lower().endswith((".aif", ".aiff")):
        f = aifc.open(path)
        ch, w, fs, n = f.getnchannels(), f.getsampwidth(), f.getframerate(), f.getnframes()
        raw = f.readframes(n)
        f.close()
        big = True
    else:
        f = wave.open(path)
        ch, w, fs, n = f.getnchannels(), f.getsampwidth(), f.getframerate(), f.getnframes()
        raw = f.readframes(n)
        f.close()
        big = False
    b = np.frombuffer(raw, np.uint8)
    if w == 2:
        x = b.view(">i2" if big else "<i2").astype(np.float64) / 32768
    elif w == 3:
        t = b.reshape(-1, 3).astype(np.int32)
        v = (t[:, 0] << 16 | t[:, 1] << 8 | t[:, 2]) if big else (t[:, 2] << 16 | t[:, 1] << 8 | t[:, 0])
        v = np.where(v >= 1 << 23, v - (1 << 24), v)
        x = v.astype(np.float64) / (1 << 23)
    elif w == 4:
        x = b.view(">i4" if big else "<i4").astype(np.float64) / 2 ** 31
    else:
        raise ValueError(f"{path}: {w}-byte samples")
    return x.reshape(-1, ch), fs


def write_wav(path, x, fs=44100):
    """x: float [n] or [n, ch] in -1..1 -> 16-bit PCM (TPDF dither)"""
    x = np.atleast_2d(np.asarray(x, np.float64).T).T
    d = (np.random.default_rng(1).random(x.shape) - np.random.default_rng(2).random(x.shape)) / 32768
    y = np.clip(np.round((x + d) * 32767), -32768, 32767).astype("<i2")
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with wave.open(path, "wb") as f:
        f.setnchannels(x.shape[1])
        f.setsampwidth(2)
        f.setframerate(fs)
        f.writeframes(y.tobytes())


def mono(x, fs, maxlag_ms=2.0):
    """fold stereo to mono with the channels time-aligned first (spaced mics: no comb filter)"""
    if x.shape[1] == 1:
        return x[:, 0].copy()
    l, r = x[:, 0], x[:, 1]
    n = min(len(l), int(0.25 * fs))
    lag = int(maxlag_ms * 1e-3 * fs)
    c = signal.correlate(l[:n], r[:n], mode="full", method="fft")
    mid = n - 1
    k = int(np.argmax(c[mid - lag:mid + lag + 1])) - lag      # l[t] ~ r[t - k]
    r = np.roll(r, k)
    return 0.5 * (l + r)


def onset(x, fs, rel=0.1, pre_ms=3.0):
    """the hammer strike: the first point above rel * peak (1 ms smoothed |x|), walked back to where the rise
    starts (below 1/4 of that level, at most 15 ms), minus a short pre-roll. Robust to hiss and rumble before
    the note (the Iowa files)."""
    a = np.abs(x)
    w = max(1, int(fs * 1e-3))
    s = np.convolve(a, np.ones(w) / w, mode="same")
    th = rel * s.max()
    i = int(np.argmax(s > th))
    j, lim = i, max(0, i - int(0.015 * fs))
    while j > lim and s[j] > th / 4:
        j -= 1
    return max(0, j - int(pre_ms * 1e-3 * fs))


# ---------------------------------------------------------------- sources

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "samples-src")


class Source:
    """notes: {midi: [(velocity, path), ...] sorted by velocity}; rels: {midi: path} (release noises)"""
    name = ""
    title = ""
    gain_by_layer = True   # the layers keep their recorded level differences (False: normalized set)

    def __init__(self):
        self.notes, self.rels = {}, {}

    def layers(self, key):
        return self.notes[key]


class Salamander(Source):
    name, title = "salamander", "Salamander Grand Piano V3 (Yamaha C5)"

    def __init__(self):
        super().__init__()
        d = os.path.join(ROOT, "salamander", "SalamanderGrandPianoV3_44.1khz16bit")
        sfz = open(os.path.join(d, "SalamanderGrandPianoV3.sfz")).read()
        for m in re.finditer(r"sample=44\.1khz16bit\\([A-G]#?\d)v(\d+)\.wav lokey=\d+ hikey=\d+ lovel=(\d+)"
                             r"(?: hivel=(\d+))?", sfz):
            key = midi_of(m.group(1))
            lo, hi = int(m.group(3)), int(m.group(4) or 127)
            p = os.path.join(d, "44.1khz16bit", f"{m.group(1)}v{m.group(2)}.wav")
            self.notes.setdefault(key, []).append(((lo + hi) / 2, p))
        for k in self.notes:
            self.notes[k] = sorted(set(self.notes[k]))
        for m in re.finditer(r"sample=44\.1khz16bit\\rel(\d+)\.wav lokey=(\d+)", sfz):
            self.rels[int(m.group(2))] = os.path.join(d, "44.1khz16bit", f"rel{m.group(1)}.wav")


class VcslSteinwayB(Source):
    name, title = "vcsl", "VCSL Grand Piano, Steinway B (close mics)"
    gain_by_layer = False  # the set is normalized per layer ("use a velocity curve to re-generate dynamic range")
    VEL = {2: 40, 3: 80, 4: 115}

    def __init__(self):
        super().__init__()
        d = os.path.join(ROOT, "vcsl-steinwayb")
        for f in sorted(os.listdir(os.path.join(d, "NoSus"))):
            m = re.fullmatch(r"JHPiano_NoSus_Close_([A-G]#?\d)_vl(\d)_rr1\.wav", f)
            if m:
                self.notes.setdefault(midi_of(m.group(1)), []).append(
                    (self.VEL[int(m.group(2))], os.path.join(d, "NoSus", f)))
        for f in sorted(os.listdir(os.path.join(d, "Rel"))):
            m = re.fullmatch(r"JHPiano_NoSusRel_Close_([A-G]#?\d)_vl3_rr1\.wav", f)
            if m:
                self.rels[midi_of(m.group(1))] = os.path.join(d, "Rel", f)
        for k in self.notes:
            self.notes[k].sort()


class Iowa(Source):
    name, title = "iowa", "University of Iowa MIS Piano (Steinway B)"
    VEL = {"pp": 30, "mf": 75, "ff": 115}

    def __init__(self):
        super().__init__()
        d = os.path.join(ROOT, "iowa")
        for f in sorted(os.listdir(d)):
            m = re.fullmatch(r"Piano\.(pp|mf|ff)\.([A-G]b?\d)\.aiff", f)
            if m and os.path.getsize(os.path.join(d, f)) > 10000:
                self.notes.setdefault(midi_of(m.group(2)), []).append((self.VEL[m.group(1)], os.path.join(d, f)))
        for k in self.notes:
            self.notes[k].sort()


SOURCES = {c.name: c for c in (Salamander, VcslSteinwayB, Iowa)}


def source(name):
    return SOURCES[name]()


# ---------------------------------------------------------------- loudness (BS.1770-4)

def _kweight(fs):
    # the two biquads of BS.1770 from their analog prototypes (as in pyloudnorm), valid at any rate
    def shelf(G, Q, fc):
        A, w0 = 10 ** (G / 40), 2 * np.pi * fc / fs
        al = np.sin(w0) / (2 * Q)
        b = [A * ((A + 1) + (A - 1) * np.cos(w0) + 2 * np.sqrt(A) * al), -2 * A * ((A - 1) + (A + 1) * np.cos(w0)),
             A * ((A + 1) + (A - 1) * np.cos(w0) - 2 * np.sqrt(A) * al)]
        a = [(A + 1) - (A - 1) * np.cos(w0) + 2 * np.sqrt(A) * al, 2 * ((A - 1) - (A + 1) * np.cos(w0)),
             (A + 1) - (A - 1) * np.cos(w0) - 2 * np.sqrt(A) * al]
        return np.array(b) / a[0], np.array(a) / a[0]

    def hp(Q, fc):
        w0 = 2 * np.pi * fc / fs
        al = np.sin(w0) / (2 * Q)
        b = [(1 + np.cos(w0)) / 2, -(1 + np.cos(w0)), (1 + np.cos(w0)) / 2]
        a = [1 + al, -2 * np.cos(w0), 1 - al]
        return np.array(b) / a[0], np.array(a) / a[0]

    return shelf(4.0, 1 / np.sqrt(2), 1500.0), hp(0.5, 38.0)


def lufs(x, fs=44100):
    x = np.atleast_2d(np.asarray(x, np.float64).T).T
    (b1, a1), (b2, a2) = _kweight(fs)
    y = signal.lfilter(b2, a2, signal.lfilter(b1, a1, x, axis=0), axis=0)
    blk, hop = int(0.4 * fs), int(0.1 * fs)
    n = max(1, (len(y) - blk) // hop + 1)
    z = np.array([np.sum(np.mean(y[i * hop:i * hop + blk] ** 2, axis=0)) for i in range(n)])
    l = -0.691 + 10 * np.log10(z + 1e-20)
    g = z[l > -70]
    if not len(g):
        return -70.0
    rel = -0.691 + 10 * np.log10(np.mean(g)) - 10
    g = z[(l > -70) & (l > rel)]
    return float(-0.691 + 10 * np.log10(np.mean(g)))


def peak_db(x):
    return float(20 * np.log10(np.max(np.abs(x)) + 1e-12))
