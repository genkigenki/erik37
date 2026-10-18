#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Erik37: Copyright (C) 2026 M. Geisler (genkigenki/makkuro)
"""Render the listening demos into demos/ (44.1 kHz, 16-bit, loudness-matched per group, BS.1770).

  demos.py            needs build/banks/*.bin (tools/build_bank.py, tools/modal_fit.py) and build/render(.exe)"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np  # noqa: E402

import eriklib as E  # noqa: E402

R = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
BLD = os.path.join(R, "build")
BK = os.path.join(BLD, "banks")
OUT = os.path.join(R, "demos")
RENDER = os.path.join(BLD, "render.exe" if os.name == "nt" else "render")
TMP = os.path.join(BLD, "demo_tmp")
TARGET = -20.0           # LUFS
PEAK = -1.0              # dBFS ceiling: a group's target drops if any member would exceed it
SRCS = ["salamander", "vcsl", "iowa"]
NOTES = ["21:110", "36:100", "60:40", "60:110", "84:80", "108:90"]


def render(bank, events, out, *opts):
    subprocess.run([RENDER, bank, events, out, *opts], check=True, capture_output=True)
    return E.read_audio(out)[0]


def ref(src, events, out):
    subprocess.run([sys.executable, "-I", os.path.join(R, "tools", "render_ref.py"), src, events, out], check=True,
                   capture_output=True)
    return E.read_audio(out)[0]


def notes_ab(src, bank):
    """each note: the source's own file (untouched, 3.5 s), then the engine (dry), 3.5 s each"""
    s = E.source(src)
    parts = []
    gap = np.zeros((int(0.6 * 44100), 2))
    for spec in NOTES:
        key, vel = map(int, spec.split(":"))
        k = min(s.notes, key=lambda q: abs(q - key))
        v, p = min(s.notes[k], key=lambda q: abs(q[0] - vel))
        x, fs = E.read_audio(p)
        if x.shape[1] == 1:
            x = np.repeat(x, 2, 1)
        x = x[E.onset(E.mono(x, fs), fs):][:int(3.5 * fs)]
        x[-int(0.15 * fs):] *= np.linspace(1, 0, int(0.15 * fs))[:, None]
        ev = os.path.join(TMP, "note.txt")
        open(ev, "w").write(f"0 on {k} {vel}\n3.0 off {k}\n")
        y = render(bank, ev, os.path.join(TMP, "note.wav"), "-r", "off", "-t", "0.5")[:int(3.5 * fs)]
        y[-int(0.15 * fs):] *= np.linspace(1, 0, int(0.15 * fs))[:, None]
        # same loudness for the pair, so the A/B is about sound, not level
        gx, gy = 10 ** ((TARGET - E.lufs(x)) / 20), 10 ** ((TARGET - E.lufs(y)) / 20)
        parts += [x * gx, gap, y * gy, gap * 2]
    return np.concatenate(parts)


def features(bank):
    """the engine's extras, Salamander: (1) a pedalled chord without / with sympathetic resonance; (2) a soft line
    normal / una corda; (3) the same chord, hall / chamber / dry"""
    ev = os.path.join(TMP, "f.txt")
    chord = "0 sus 127\n0.05 on 48 70\n0.05 on 55 66\n0.05 on 64 64\n0.05 on 72 70\n0.6 off 48\n0.6 off 55\n" \
            "0.6 off 64\n0.6 off 72\n0.9 on 84 50\n1.5 off 84\n4.5 sus 0\n"
    open(ev, "w").write(chord)
    a = render(bank, ev, os.path.join(TMP, "f1.wav"), "-s", "0", "-t", "1.5")
    b = render(bank, ev, os.path.join(TMP, "f2.wav"), "-t", "1.5")
    line = "".join(f"{i * 0.45:.2f} on {k} 45\n{i * 0.45 + 0.42:.2f} off {k}\n"
                   for i, k in enumerate([64, 67, 72, 71, 67, 64, 60]))
    open(ev, "w").write(line)
    c = render(bank, ev, os.path.join(TMP, "f3.wav"), "-t", "2")
    open(ev, "w").write("0 soft 127\n" + line)
    d = render(bank, ev, os.path.join(TMP, "f4.wav"), "-t", "2")
    open(ev, "w").write(chord)
    e = render(bank, ev, os.path.join(TMP, "f5.wav"), "-r", "chamber", "-t", "1.5")
    f = render(bank, ev, os.path.join(TMP, "f6.wav"), "-r", "off", "-t", "1.5")
    gap = np.zeros((44100, 2))
    return np.concatenate([a, gap, b, gap, c, gap, d, gap, b, gap, e, gap, f])


def match(group):
    """one gain each to TARGET LUFS; the whole group lower if a peak would pass PEAK"""
    t = TARGET
    for name, x in group.items():
        over = E.peak_db(x) + (TARGET - E.lufs(x)) - PEAK
        if over > 0:
            t = min(t, TARGET - over)
    for name, x in group.items():
        y = x * 10 ** ((t - E.lufs(x)) / 20)
        E.write_wav(os.path.join(OUT, name), y)
        print(f"  {name:34s} {E.lufs(y):6.1f} LUFS  peak {E.peak_db(y):5.1f} dBFS")


def main():
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(TMP, exist_ok=True)
    ph = os.path.join(BLD, "phrase.txt")
    subprocess.run([sys.executable, "-I", os.path.join(R, "tools", "phrase.py"), ph], check=True)
    g = {}
    for i, s in enumerate(SRCS):
        g[f"0{i + 1}_phrase_{s}_450kb.wav"] = render(os.path.join(BK, f"{s}_450.bin"), ph, os.path.join(TMP, "p.wav"))
    g["04_phrase_salamander_300kb.wav"] = render(os.path.join(BK, "salamander_300.bin"), ph, os.path.join(TMP, "p.wav"))
    g["05_phrase_hybrid_salamander.wav"] = render(os.path.join(BK, "salamander_450.bin"), ph, os.path.join(TMP, "p.wav"),
                                                  "-M", "hybrid", os.path.join(BK, "modal_salamander.bin"))
    g["06_phrase_modelled.wav"] = render(os.path.join(BK, "salamander_450.bin"), ph, os.path.join(TMP, "p.wav"),
                                         "-M", "model", os.path.join(BK, "modal_synth.bin"))
    print("phrase, engine (hall):")
    match(g)
    g = {}
    for i, s in enumerate(SRCS):
        g[f"1{i + 1}_dry_{s}_engine.wav"] = render(os.path.join(BK, f"{s}_450.bin"), ph, os.path.join(TMP, "p.wav"),
                                                   "-r", "off")
        g[f"1{i + 1}_dry_{s}_source.wav"] = ref(s, ph, os.path.join(TMP, "r.wav"))
    print("phrase, dry: the engine vs the source's own full recordings:")
    match(g)
    print("single notes, source file then engine:")
    for i, s in enumerate(SRCS):
        y = notes_ab(s, os.path.join(BK, f"{s}_450.bin"))
        name = f"2{i + 1}_notes_{s}_source_then_engine.wav"
        E.write_wav(os.path.join(OUT, name), y * min(1.0, 10 ** ((PEAK - E.peak_db(y)) / 20)))
        print(f"  {name}")
    print("features:")
    match({"30_features_salamander.wav": features(os.path.join(BK, "salamander_450.bin"))})


if __name__ == "__main__":
    main()
