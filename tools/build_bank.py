#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Erik37: Copyright (C) 2026 M. Geisler (genkigenki/makkuro)
"""Build a sound bank (src/erk_bank.h) from a sample source.

  build_bank.py SOURCE OUT.bin --zones N --layers L --budget KB [--report OUT.json]

Per zone and layer: onset trim, mono fold-down (channels time-aligned), a two-slope decay fit (dB/s before and
after a knee, from the mf layer: the engine re-applies it, per key), flattening by that model, resampling to the
zone's rate, a loop after the attack (block-aligned start, correlation-picked end, crossfaded), and block ADPCM.
Durations scale to fill the budget. Release noises: the source's own when it has usable ones, else synthesized."""
import argparse
import json
import os
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np  # noqa: E402
from scipy import signal  # noqa: E402

import eriklib as E  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
CODEC = os.path.join(HERE, "..", "build", "erkcodec.exe" if os.name == "nt" else "erkcodec")
FS = 44100
BLK = 32
REL1_BLOCKS = 83                      # src/erk.c ERK_REL1: the damper's first stage, ~60 ms
LAYER_VEL = {2: [55, 110], 3: [30, 72, 112]}
TARGET_RMS = 0.12                     # K-weighted RMS of a zone's first 250 ms at unity gain
NODAMP_KEY = 90                       # F#6 and up: no dampers


def rate_of(root):
    return 22050 if root < 45 else 28000 if root < 69 else 32000


def t_attack(key):
    return float(np.interp(key, [21, 60, 108], [0.30, 0.20, 0.08]))


def t_loop(key):
    return float(np.interp(key, [21, 60, 108], [0.40, 0.25, 0.10]))


def f0(key):
    return 440.0 * 2 ** ((key - 69) / 12)


def kweight_rms(x, fs):
    (b1, a1), (b2, a2) = E._kweight(fs)
    y = signal.lfilter(b2, a2, signal.lfilter(b1, a1, x))
    return float(np.sqrt(np.mean(y ** 2)) + 1e-12)


def fit_decay(m, fs):
    """two-slope (dB/s) model with a knee, from the 10 ms RMS envelope; t from the onset"""
    fr = int(0.01 * fs)
    n = len(m) // fr
    p = np.mean(m[:n * fr].reshape(n, fr) ** 2, axis=1)
    return fit_env(p, int(np.argmax(p)) + 12)        # from ~120 ms: the loop region on (the attack is in the data)


def fit_env(p, i0):
    """the fit on a power envelope in 10 ms frames, from frame i0; the noise floor (hiss, room) taken out first"""
    n = len(p)
    pf = np.median(p[-max(5, n // 10):])
    db = 10 * np.log10(np.maximum(p - pf, 1e-3 * pf) / p.max() + 1e-12)
    t = np.arange(n) * 0.01
    floor = 10 * np.log10(pf / p.max() + 1e-12)
    ok = np.nonzero(db > floor + 6)[0]
    i1 = min(int(ok[-1]) if len(ok) else n - 1, int(25 / 0.01))
    if i1 - i0 < 30:
        i1 = min(n - 1, i0 + 30)
    tt, yy = t[i0:i1], db[i0:i1]
    sw = 1 / np.sqrt(0.3 + tt)                       # the first seconds weigh more (they are what is heard)
    best = None
    for tk in np.arange(tt[0] + 0.1, max(tt[0] + 0.15, min(8.0, tt[-1] - 0.3)), 0.05):
        A = np.stack([np.ones_like(tt), np.minimum(tt, tk), np.maximum(tt - tk, 0)], 1)
        c, *_ = np.linalg.lstsq(A * sw[:, None], yy * sw, rcond=None)
        if c[1] > 0 or c[2] > 0 or c[2] < c[1] - 1e-9:
            continue
        err = float(np.sum(((A @ c - yy) * sw) ** 2))
        if best is None or err < best[0]:
            best = (err, c, tk)
    if best is None:
        A = np.stack([np.ones_like(tt), tt], 1)
        c, *_ = np.linalg.lstsq(A, yy, rcond=None)
        s = min(float(c[1]), -0.5)
        return {"s1": s, "s2": s, "tk": 1.0}
    _, c, tk = best
    return {"s1": float(min(c[1], -0.3)), "s2": float(min(c[2], -0.3)), "tk": float(tk)}


def model_db(t, d):
    return d["s1"] * np.minimum(t, d["tk"]) + d["s2"] * np.maximum(t - d["tk"], 0)


def find_loop(y, ls0, L0):
    """block-aligned start near ls0, end picked by the correlation of the crossfade regions; -> (ls, le, r, X)"""
    X = max(64, int(0.35 * L0))
    best = None
    lo, hi = int(ls0 - 0.15 * L0), int(ls0 + 0.15 * L0)
    for ls in range(max(X + 64, lo) // BLK * BLK, hi + 1, BLK):
        T = y[ls - X:ls]
        a, b = ls + int(0.85 * L0), ls + int(1.15 * L0)
        if b > len(y):
            break
        seg = y[a - X:b]
        c = signal.correlate(seg, T, mode="valid", method="fft")
        cs = np.concatenate([[0], np.cumsum(seg ** 2)])
        en = cs[X:] - cs[:-X]
        r = c / np.sqrt(en * np.sum(T ** 2) + 1e-20)
        k = int(np.argmax(r))
        if best is None or r[k] > best[2]:
            best = (ls, a + k, float(r[k]), X)
    return best


def crossfade(y, ls, le, X, r):
    y = y.copy()
    w = np.arange(X) / X
    if r > 0.7:
        g_out, g_in = 1 - w, w
    else:
        g_out, g_in = np.cos(w * np.pi / 2), np.sin(w * np.pi / 2)
    y[le - X:le] = y[le - X:le] * g_out + y[ls - X:ls] * g_in
    return y[:le]


def detrend_loop(y, ls, le, fs):
    """remove the level slope over the loop (the layer's own decay vs the zone model)"""
    L = le - ls
    q = L // 4
    rms = [np.sqrt(np.mean(y[ls + i * q:ls + (i + 1) * q] ** 2)) + 1e-12 for i in range(4)]
    db = 20 * np.log10(rms)
    tc = (np.arange(4) + 0.5) * q / fs
    k = np.polyfit(tc, db, 1)[0]                    # dB/s
    t = np.arange(len(y) - ls) / fs
    y = y.copy()
    y[ls:] *= 10 ** (-k * t / 20)
    return y, float(k)


def resample(x, fs_in, fs_out):
    from math import gcd
    g = gcd(int(fs_in), int(fs_out))
    return signal.resample_poly(x, int(fs_out) // g, int(fs_in) // g)


def encode(y16, tmp, nfilt=16):
    a, b, c = (os.path.join(tmp, n) for n in ("in.s16", "out.bin", "dec.s16"))
    y16.astype("<i2").tofile(a)
    subprocess.run([CODEC, "enc", a, b, c, str(nfilt)], check=True)
    return np.fromfile(b, np.uint8), np.fromfile(c, "<i2")


def snr_db(ref, dec):
    ref, dec = ref.astype(np.float64), dec.astype(np.float64)
    return float(10 * np.log10(np.sum(ref ** 2) / (np.sum((ref - dec) ** 2) + 1e-9)))


def ima_snr(y16, tmp):
    a, c = os.path.join(tmp, "in.s16"), os.path.join(tmp, "ima.s16")
    y16.astype("<i2").tofile(a)
    subprocess.run([CODEC, "ima", a, c], check=True)
    return snr_db(y16, np.fromfile(c, "<i2"))


def synth_release(key, fs=22050, n=None):
    """a felt damper landing: low-passed noise burst and a soft thump (for sources without release samples)"""
    rng = np.random.default_rng(key)
    n = n or int(0.35 * fs)
    t = np.arange(n) / fs
    fc = float(np.interp(key, [21, 60, 108], [700, 1400, 2600]))
    b, a = signal.butter(2, [120 / (fs / 2), fc / (fs / 2)], "band")
    nz = signal.lfilter(b, a, rng.standard_normal(n))
    env = (1 - np.exp(-t / 0.003)) * np.exp(-t / 0.035)
    th = np.sin(2 * np.pi * float(np.interp(key, [21, 108], [70, 200])) * t) * np.exp(-t / 0.05) * (1 - np.exp(-t / 0.004))
    y = 0.6 * nz / (np.max(np.abs(nz)) + 1e-9) * env + 0.4 * th
    return y / np.max(np.abs(y))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("source")
    ap.add_argument("out")
    ap.add_argument("--zones", type=int, default=18)
    ap.add_argument("--layers", type=int, default=3)
    ap.add_argument("--budget", type=float, default=450, help="KB (1024 bytes), the whole bank image")
    ap.add_argument("--report")
    ap.add_argument("--nfilt", type=int, default=16)
    a = ap.parse_args()

    src = E.source(a.source)
    avail = sorted(src.notes)
    N, L = a.zones, a.layers
    # zone roots: evenly spread over the keyboard, snapped to sampled notes
    want = np.linspace(21 + 87 / N / 2, 108 - 87 / N / 2, N)
    roots = []
    for w in want:
        c = [k for k in avail if k not in roots and (not roots or k > roots[-1])]
        roots.append(min(c, key=lambda k: abs(k - w)))
    bounds = [(roots[i] + roots[i + 1]) / 2 for i in range(N - 1)]
    zone_of = [int(np.searchsorted(bounds, k, side="left")) for k in range(21, 109)]
    vc = LAYER_VEL[L]

    # load everything once: trimmed mono at 44.1 kHz
    raw = {}
    fits = []
    for z, root in enumerate(roots):
        lay = src.notes[root]
        picks = []
        for v in vc:
            vel, p = min(lay, key=lambda q: abs(q[0] - v))
            picks.append((vel, p))
        for l, (vel, p) in enumerate(picks):
            x, fs = E.read_audio(p)
            assert fs == FS, p
            m = E.mono(x, fs)
            o = E.onset(m, fs)
            raw[z, l] = (m[o:], vel, os.path.basename(p))
        mf = min(range(L), key=lambda l: abs(picks[l][0] - 75))
        f = fit_decay(raw[z, mf][0], FS)
        # physical bounds: the aftersound dies at least this fast (dB/s), the prompt decay is the faster one
        f["s2"] = min(f["s2"], float(np.interp(root, [21, 108], [-0.8, -4.0])))
        f["s1"] = min(f["s1"], f["s2"])
        fits.append(f)
    # releases
    rel_keys = [36, 60, 84]
    rels = []
    use_own = a.source == "salamander" and src.rels
    for rk in rel_keys:
        if use_own:
            k = min(src.rels, key=lambda q: abs(q - rk))
            x, fs = E.read_audio(src.rels[k])
            m = E.mono(x, fs)
            m = m[E.onset(m, fs, rel=0.05):]
            m = resample(m, fs, 22050)[:int(0.40 * 22050)]
            m[-int(0.05 * 22050):] *= np.linspace(1, 0, int(0.05 * 22050))
            rels.append(m / np.max(np.abs(m)))
        else:
            rels.append(synth_release(rk))

    overhead = 20 + 32 * 88 + 8 * 128 + 24 * (N * L + len(rels))
    rel_bytes = sum((len(r) + 31) // 32 * 17 for r in rels)

    def size_at(scale):
        tot = 0
        for z, root in enumerate(roots):
            fsz = rate_of(root)
            for l in range(L):
                n = min(len(raw[z, l][0]) * fsz // FS - 64, int((t_attack(root) + t_loop(root)) * scale * fsz))
                tot += (n + 31) // 32 * 17
        return tot

    budget = int(a.budget * 1024) - overhead - rel_bytes
    lo, hi = 0.2, 20.0
    for _ in range(40):
        mid = (lo + hi) / 2
        lo, hi = (mid, hi) if size_at(mid) <= budget * 0.985 else (lo, mid)
    scale = lo

    tmp = tempfile.mkdtemp(prefix="erk")

    def zones_pass(scale, measure):
        smps, blobs, zinfo = [], [], []
        off = 0
        snr_blk, snr_brr, snr_ima = [], [], []
        for z, root in enumerate(roots):
            fsz = rate_of(root)
            for l in range(L):
                m, vel, fname = raw[z, l]
                t = np.arange(len(m)) / FS
                flat = m / 10 ** (model_db(t, fits[z]) / 20)
                y = resample(flat, FS, fsz)
                ta, tl = t_attack(root) * scale, t_loop(root) * scale
                ls0, L0 = int(ta * fsz), int(tl * fsz)
                if ls0 + 1.2 * L0 > len(y) - 64:          # short sample: shrink proportionally
                    k = (len(y) - 64) / (ls0 + 1.2 * L0)
                    ls0, L0 = int(ls0 * k), int(L0 * k)
                y, slope = detrend_loop(y, ls0, ls0 + L0, fsz)
                ls, le, r, X = find_loop(y, ls0, L0)
                y = crossfade(y, ls, le, X, r)
                nf = int(0.001 * fsz)                    # 1 ms fade-in: a file cut right at the hammer must not step
                y[:nf] *= 0.5 - 0.5 * np.cos(np.pi * np.arange(nf) / nf)
                pk = np.max(np.abs(y))
                y16 = np.round(y / pk * 32000).astype(np.int16)
                # the level as heard: the stored (flattened) data times the decay the engine re-applies
                n25 = min(int(0.25 * fsz), len(y))
                heard = y[:n25] * 10 ** (model_db(np.arange(n25) / fsz, fits[z]) / 20)
                krms = kweight_rms(heard / pk * 32000 / 32768, fsz)
                gain = min(65535, int(round(TARGET_RMS / krms * 4096)))
                enc, dec = encode(y16, tmp, a.nfilt)
                # the loop restart state: the decoder history just before the (block-aligned) loop start
                h1, h2 = int(dec[ls - 1]), int(dec[ls - 2])
                smps.append(struct.pack("<IIIIhhHH", off, len(y16), ls, le - ls, h1, h2, fsz, gain))
                blobs.append(enc.tobytes())
                off += len(enc)
                sb = snr_db(y16, dec)
                snr_blk.append(sb)
                if measure:
                    _, d4 = encode(y16, tmp, 4)
                    snr_brr.append(snr_db(y16, d4))
                    snr_ima.append(ima_snr(y16, tmp))
                zinfo.append({"zone": z, "root": root, "layer": l, "file": fname, "vel": vel, "rate": fsz,
                              "len_s": round(len(y16) / fsz, 3), "loop_start_s": round(ls / fsz, 3),
                              "loop_len_s": round((le - ls) / fsz, 3), "loop_r": round(r, 3), "bytes": len(enc),
                              "snr_db": round(sb, 1), "gain": round(gain / 4096, 2), "loop_slope_dbs": round(slope, 2)})
        return smps, blobs, zinfo, off, snr_blk, snr_brr, snr_ima

    # the size is not linear in the scale (short samples, loop search): correct it on the real size
    best = None
    for _ in range(6):
        got = zones_pass(scale, False)[3]
        print(f"  scale {scale:.3f}: {got / 1024:.1f} KB of {budget / 1024:.1f}", file=sys.stderr)
        if got <= budget and (best is None or got > best[1]):
            best = (scale, got)
        if budget * 0.99 <= got <= budget:
            break
        scale *= (budget / got) ** 0.9 * (0.995 if got > budget else 1)
    scale = best[0] if best else scale * 0.95
    smps, blobs, zinfo, off, snr_blk, snr_brr, snr_ima = zones_pass(scale, True)
    relsmps = []
    for i, rr in enumerate(rels):
        y16 = np.round(rr * 32000).astype(np.int16)
        enc, dec = encode(y16, tmp, a.nfilt)
        krms = kweight_rms(rr[:int(0.1 * 22050)] * 32000 / 32768, 22050)
        gain = min(65535, int(round(TARGET_RMS / krms * 4096)))
        relsmps.append(struct.pack("<IIIIhhHH", off, len(y16), 0, 0, 0, 0, 22050, gain))
        blobs.append(enc.tobytes())
        off += len(enc)

    # per key
    keys = []
    for k in range(21, 109):
        z = zone_of[k - 21]
        root = roots[z]
        fsz = rate_of(root)
        inc = int(round(65536 * fsz / FS * 2 ** ((k - root) / 12)))
        assert inc < 65536, (k, inc)
        # the zone's own model (its samples are flattened with it); interpolating slopes and knees apart
        # would pair a steep slope with a late knee. Shifted keys: a touch faster up, slower down.
        sh = 2 ** (-(k - root) / 12 * 0.5)
        s1, s2, tk = fits[z]["s1"] / sh, fits[z]["s2"] / sh, fits[z]["tk"] * sh

        def mult(dbs):
            return int(round(2 ** 30 * 10 ** (dbs * BLK / FS / 20)))
        r1 = float(np.interp(k, [21, 108], [-120, -250]))     # damper, dB/s: the first ~60 ms, then the tail
        r2 = float(np.interp(k, [21, 108], [-45, -90]))
        p = 0.5 + 0.30 * (k - 64.5) / 43.5
        pl, pr = np.cos(p * np.pi / 2), np.sin(p * np.pi / 2)
        afl = 1 - np.exp(-2 * np.pi * min(16000, max(3000, 6 * f0(k))) / FS)
        tau = float(np.interp(k, [21, 108], [4.0, 1.5]))
        dark = int(round(65535 * np.exp(-BLK / (FS * tau))))
        flags = 1 if k >= NODAMP_KEY else 0
        rel = 0 if k < 48 else 1 if k < 72 else 2
        keys.append(struct.pack("<IIIIHHHHHHBBBB", mult(s1), mult(s2), mult(r1), mult(r2),
                                int(round(tk * FS / BLK)), inc, int(round(pl * 32767)), int(round(pr * 32767)),
                                int(round(afl * 16384)), dark, z, flags, rel, 0))
    # per velocity
    vels = []
    for v in range(128):
        amp = (v / 127) ** 1.8
        fc = 18000 * 2 ** (-2.2 * (1 - v / 127) ** 1.5)
        av = 1 - np.exp(-2 * np.pi * fc / FS)
        l0, l1, w = 0, 0, 0.0
        if v >= vc[-1]:
            l0 = l1 = L - 1
        elif v > vc[0]:
            i = int(np.searchsorted(vc, v, side="right")) - 1
            f = (v - vc[i]) / (vc[i + 1] - vc[i])
            if f < 0.3:
                l0 = l1 = i
            elif f > 0.7:
                l0 = l1 = i + 1
            else:
                l0, l1, w = i, i + 1, (f - 0.3) / 0.4
        vels.append(struct.pack("<HHHBB", int(round(amp * 32767)), int(round(min(1.0, av) * 16384)),
                                int(round(w * 32767)), l0, l1))
    data = b"".join(blobs)
    hdr = struct.pack("<IHHHHIHH", 0x314b5245, N, L, len(rels), 0, len(data), int(0.30 * 32767), 0)
    img = hdr + b"".join(keys) + b"".join(vels) + b"".join(smps) + b"".join(relsmps) + data
    with open(a.out, "wb") as f:
        f.write(img)

    tot = len(img)
    rep = {"source": a.source, "title": src.title, "zones": N, "layers": L, "roots": roots, "layer_vel": vc,
           "scale": round(scale, 3), "bytes": tot, "kb": round(tot / 1024, 1), "budget_kb": a.budget,
           "tables_bytes": overhead, "release_bytes": rel_bytes, "release_source": "own" if use_own else "synth",
           "sample_bytes": len(data) - rel_bytes,
           "snr_block16_db": round(float(np.mean(snr_blk)), 2), "snr_block4_db": round(float(np.mean(snr_brr)), 2),
           "snr_ima_db": round(float(np.mean(snr_ima)), 2), "decay": [dict(root=r, **f) for r, f in zip(roots, fits)],
           "max_shift": int(max(abs(k - roots[zone_of[k - 21]]) for k in range(21, 109))), "samples": zinfo}
    if a.report:
        with open(a.report, "w") as f:
            json.dump(rep, f, indent=1)
    print(f"{a.source}: {N} zones x {L} layers, scale {scale:.2f}, {tot / 1024:.1f} KB "
          f"(samples {(len(data) - rel_bytes) / 1024:.1f}, releases {rel_bytes / 1024:.1f}, tables {overhead / 1024:.1f}); "
          f"SNR block16 {rep['snr_block16_db']} dB, block4 {rep['snr_block4_db']} dB, IMA {rep['snr_ima_db']} dB; "
          f"max shift {rep['max_shift']} st")


if __name__ == "__main__":
    main()
