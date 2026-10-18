#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Erik37: Copyright (C) 2026 M. Geisler (genkigenki/makkuro)
"""Modal (partial) tables for the engine's hybrid and modelled modes (src/erk_modal.h).

  modal_fit.py measure BANK.json OUT.bin   measured from the bank's own source notes (the hybrid: the sampled attack
                                           hands over to these partials at ~0.25 s)
  modal_fit.py synth OUT.bin               from a physical recipe (the modelled mode): hammer position and velocity
                                           spectrum, string inharmonicity, frequency-dependent loss, two-string
                                           coupling (prompt and aftersound), a few soundboard modes for the knock

Per zone and layer, up to NP partials: frequency ratio to the key's nominal f0 (inharmonicity included), amplitude
(full scale = 1, on the bank's loudness scale), a two-slope decay (dB/s, knee), a beat (Hz, depth)."""
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np  # noqa: E402
from scipy import signal  # noqa: E402

import eriklib as E  # noqa: E402
from build_bank import kweight_rms, TARGET_RMS, f0, fit_env  # noqa: E402

FS = 44100
BLK = 32
NP = 24
FMAX = 10000.0
T_H = 0.25            # the handover time (src/erk.c HY_MID)


def mult(dbs):
    return int(round(2 ** 30 * 10 ** (dbs * BLK / FS / 20)))


def pack_partial(ratio, amp, d1, d2, knee, beat, depth):
    return struct.pack("<IhhIIHH", int(round(ratio * 2 ** 20)), int(round(np.clip(amp, 0, 0.99) * 32767)),
                       int(round(np.clip(depth, 0, 0.9) * 32767)), mult(d1), mult(d2),
                       min(65535, int(round(knee * FS / BLK))), int(round(beat * 256)))


def measure_note(x, key):
    """partials of a trimmed mono note: [(ratio, amp at T_H, d1, d2, knee_after_T_H, beat, depth)]"""
    f = f0(key)
    seg = x[int(0.05 * FS):int(1.05 * FS)]
    if len(seg) < FS // 2:
        seg = np.pad(seg, (0, FS // 2))
    nfft = 1 << int(np.ceil(np.log2(len(seg) * 2)))
    X = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), nfft))
    fr = np.fft.rfftfreq(nfft, 1 / FS)
    B = 10 ** float(np.interp(key, [21, 48, 72, 108], [-3.7, -4.0, -3.4, -2.2]))
    ff = f
    found = []
    for it in range(3):
        found = []
        for n in range(1, NP + 1):
            fp = n * ff * np.sqrt(1 + B * n * n)
            if fp > FMAX:
                break
            w = max(3.0, 0.012 * fp)
            sel = np.nonzero((fr > fp - w) & (fr < fp + w))[0]
            if not len(sel):
                continue
            k = sel[np.argmax(X[sel])]
            if 0 < k < len(X) - 1:                    # parabolic peak
                a, b, c = np.log(X[k - 1:k + 2] + 1e-12)
                k = k + 0.5 * (a - c) / (a - 2 * b + c + 1e-12)
            found.append((n, float(k * FS / nfft)))
        if len(found) >= 3:
            n = np.array([q[0] for q in found], float)
            y = (np.array([q[1] for q in found]) / n) ** 2
            c = np.polyfit(n * n, y, 1)
            if c[1] > 0 and c[0] > 0:
                ff, B = float(np.sqrt(c[1])), float(c[0] / c[1])
    out = []
    t = np.arange(len(x)) / FS
    for n, fp in found:
        z = x * np.exp(-2j * np.pi * fp * t)
        cut = float(np.clip(f / 4, 3, 20))
        b, a = signal.butter(2, cut / (FS / 2))
        env = 2 * np.abs(signal.filtfilt(b, a, z))
        fr10 = env[::441]
        A = float(fr10[int(T_H * 100)]) if len(fr10) > int(T_H * 100) else 0.0
        if A < 1e-5:
            continue
        tail = fr10[int(T_H * 100):]
        # decay: the two-slope fit on this partial's envelope from T_H on (knee relative to T_H)
        d = fit_env(tail ** 2, 0)
        d["s2"] = min(d["s2"], -0.5)
        # beat: the strongest slow modulation of the envelope against the fit
        db = 20 * np.log10(tail[:600] + 1e-9)
        tt = np.arange(len(db)) * 0.01
        if len(db) > 100:
            res = db - np.polyval(np.polyfit(tt, db, 2), tt)
            R = np.abs(np.fft.rfft(res * np.hanning(len(res)), 4096))
            fq = np.fft.rfftfreq(4096, 0.01)
            sel = (fq > 0.15) & (fq < 4)
            k = np.argmax(R[sel])
            beat = float(fq[sel][k])
            depth = float(np.clip((10 ** (np.std(res) * 1.4 / 20) - 1), 0, 0.35))
        else:
            beat, depth = 0.0, 0.0
        out.append((n, (fp / f, A, d["s1"], d["s2"], d["tk"], beat, depth)))
    # calibrate: the partials' power at T_H no more than the signal's own (leakage, noise in the estimates)
    a, b = int((T_H - 0.05) * FS), int((T_H + 0.05) * FS)
    sig = float(np.mean(x[a:b] ** 2)) if b < len(x) else 0.0
    part = sum(q[1] ** 2 / 2 for _, q in out)
    if part > 0.85 * sig > 0:
        k = np.sqrt(0.85 * sig / part)
        out = [(n, (q[0], q[1] * k) + q[2:]) for n, q in out]
    return out


def measure(bank_json, out_path):
    r = json.load(open(bank_json))
    src = E.source(r["source"])
    roots, vc, L = r["roots"], r["layer_vel"], r["layers"]
    files = {(s["zone"], s["layer"]): s["file"] for s in r["samples"]}
    paths = {os.path.basename(p): p for k in src.notes for v, p in src.notes[k]}
    blob = []
    stats = []
    for z, root in enumerate(roots):
        for l in range(L):
            x, fs = E.read_audio(paths[files[z, l]])
            m = E.mono(x, fs)
            m = m[E.onset(m, fs):]
            m = m * TARGET_RMS / kweight_rms(m[:int(0.25 * FS)], FS)
            ps = measure_note(m, root)
            stats.append(len(ps))
            slot = [pack_partial(1, 0, -100, -100, 1, 0, 0)] * NP      # by harmonic number: layers line up
            for n, q in ps:
                if n <= NP:
                    slot[n - 1] = pack_partial(*q)
            ent = slot
            blob.append(b"".join(ent))
    write(out_path, roots, L, blob, 0)
    print(f"{out_path}: measured {len(roots)} zones x {L} layers, partials {min(stats)}..{max(stats)} "
          f"(mean {np.mean(stats):.1f}), {os.path.getsize(out_path) / 1024:.1f} KB")


def synth(out_path):
    """the modelled piano: 18 zones, 3 layers (velocity 30 / 72 / 112)"""
    N = 18
    roots = [int(round(k)) for k in np.linspace(23, 106, N)]
    vels = [30, 72, 112]
    blob = []
    for root in roots:
        fz = f0(root)
        B = 10 ** float(np.interp(root, [21, 48, 72, 108], [-3.7, -4.0, -3.4, -2.2]))
        beta = 1 / 7.7                                    # the striking point, 1/7.7 of the speaking length
        for v in vels:
            ps = []
            fc = 900 + 5200 * (v / 127) ** 1.6            # felt: a harder blow excites higher partials
            for n in range(1, 64):
                fn = n * fz * np.sqrt(1 + B * n * n)
                if fn > FMAX or len(ps) >= NP - 3:
                    break
                a = abs(np.sin(n * np.pi * beta)) / n ** 0.7 * np.exp(-fn / fc)
                b1, b3 = 0.35 + 0.6 * (root - 21) / 87, 2.2e-7     # loss: 1/tau = b1 + b3 f^2
                dbs = -8.686 * (b1 + b3 * fn * fn)
                rng = np.random.default_rng(root * 97 + n)
                ps.append([fn / fz, a, 3.0 * dbs, dbs, float(np.clip(0.9 * (220 / fz) ** 0.25, 0.25, 2.5)),
                           float(rng.uniform(0.2, 1.4)), float(rng.uniform(0.1, 0.35))])
            a = np.array([p[1] for p in ps])
            a *= np.sqrt(2) * TARGET_RMS / np.sqrt(np.sum(a ** 2)) * 1.6     # equal loudness (sine rms = a / sqrt 2)
            for p, q in zip(ps, a):
                p[1] = float(q)
            for fb, ab, db in ((92.0, 0.10, -90), (180.0, 0.07, -110), (310.0, 0.05, -140)):   # soundboard knock
                ps.append([fb / fz, ab * TARGET_RMS * 4 * (v / 127) ** 0.5, db, db, 1.0, 0.0, 0.0])
            ent = [pack_partial(*p) for p in ps] + [pack_partial(1, 0, -100, -100, 1, 0, 0)] * (NP - len(ps))
            blob.append(b"".join(ent))
    write(out_path, roots, 3, blob, 1)
    print(f"{out_path}: synthesized {N} zones x 3 layers, {os.path.getsize(out_path) / 1024:.1f} KB")


def write(path, roots, L, blob, flags):
    bounds = [(roots[i] + roots[i + 1]) / 2 for i in range(len(roots) - 1)]
    zone = [int(np.searchsorted(bounds, k, side="left")) for k in range(21, 109)]
    finc = [int(round(f0(k) / FS * 2 ** 32)) for k in range(21, 109)]
    hdr = struct.pack("<IHHHH", 0x4d4b5245, len(roots), L, NP, flags)
    with open(path, "wb") as f:
        f.write(hdr + struct.pack("<88I", *finc) + bytes(zone) + b"".join(blob))


if __name__ == "__main__":
    if sys.argv[1] == "measure":
        measure(sys.argv[2], sys.argv[3])
    else:
        synth(sys.argv[2])
