#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Erik37: Copyright (C) 2026 M. Geisler (genkigenki/makkuro)
"""The audio's CPU budget on the AC791N, from the firmware's own code (app/build/smk37/erik37.dis): the hot loops'
instructions (a backward branch closes each), as the repo's other budgets count them, put together per sample.

  target_budget.py DIS [--voices N] [--xf N] [--limit PCT] [--report OUT.json]

  a voice     stream_run's loop (the interpolation) + stream_decode's loop x R (source samples an output sample, R
              0.70: 22.05 / 28 / 32 kHz zones up to 3 semitones up) + voice_out's loop + the block's upkeep / 32
  two layers  a second stream (the attack's first 0.3 s, at most --xf notes at once)
  the bus     the reverb (8-line FDN, every 2nd sample), the resonance (12 combs, every 4th), the tone EQ (both
              channels), the mix and the output, the block's upkeep / 32, the keys' poll
Cycles: 1.3 an instruction (pi32v2: mostly single-cycle; loads, multiplies and taken branches more), against 320 MHz
(the SDK's clock) and 240 MHz. The worst case: every voice sounding, --xf of them in their two-layer attack, the four
fading slots and the release noises busy. Fails above --limit % at 320 MHz (default 75)."""
import json
import re
import sys

LINE = re.compile(r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*\t(.*)$")
FS = 44100
CPI = 1.3
R = 0.70


def functions(dis):
    f, name = {}, None
    for ln in dis.splitlines():
        m = re.match(r"^([A-Za-z_]\w*):\s*$", ln)
        if m:
            name = m.group(1)
            f[name] = []
            continue
        mm = LINE.match(ln)
        if name and mm:
            f[name].append((int(mm.group(1), 16), mm.group(2)))
    return f


def loops(ins):
    """(start, end, size, calls) of each loop, innermost first"""
    out = set()
    for a, t in ins:
        m = re.search(r"goto -?\d+ <[^:]*: ([0-9a-f]+) >", t)
        if m and int(m.group(1), 16) <= a:
            s = int(m.group(1), 16)
            body = [x for x in ins if s <= x[0] <= a]
            out.add((s, a, len(body), sum("call" in x[1] for x in body)))
    return sorted(out, key=lambda l: l[2])


def main():
    a = sys.argv[1:]
    dis = open(a[0]).read()
    arg = lambda k, d: type(d)(a[a.index(k) + 1]) if k in a else d
    nv, nx, limit = arg("--voices", 20), arg("--xf", 6), arg("--limit", 75.0)
    f = functions(dis)
    one = lambda fn: loops(f[fn])[-1][2]                    # a function's (outermost) loop
    run, dec, out = one("stream_run"), one("stream_decode"), one("voice_out")
    rl = loops(f["res_block"])
    res = (rl[-1][2] - rl[0][2] + 12 * rl[0][2]) / 4.0      # outer once, the inner 12 times, every 4th sample
    rev = loops(f["rev_block"])[-1][2] / 2.0
    eq = 2.0 * one("eq_block")
    rg = loops(f["erk_render_gain"])
    outer = rg[-1]
    inner = [l for l in rg if l is not outer and outer[0] <= l[0] and l[1] <= outer[1]]
    # the voice loop: the largest inner loop with calls (voice_block inlined); the per-sample loops: no calls
    vloop = max((l for l in inner if l[3]), key=lambda l: l[2])
    per_sample = [l for l in inner if not l[3] and not (vloop[0] <= l[0] and l[1] <= vloop[1])]
    mix = sum(l[2] for l in per_sample if l[2] > 14)        # (the 88 keys' levels, memsets: per block, small)
    block = outer[2] - vloop[2] - sum(l[2] for l in per_sample) + sum(l[2] * (88 if l[2] <= 14 else 0) for l in per_sample)
    upkeep = vloop[2] / 32.0                                # each voice's block: envelope, handover, filter, gains
    poll = loops(f["app_render"])[-1][2] / 64.0              # the keys / MIDI / pedals, every 64 frames (no key new)
    V = run + dec * R + out + upkeep
    V2 = V + run + dec * R
    VN = run + dec * 0.5 + out + upkeep                     # a release noise (22.05 kHz)
    B = res + rev + eq + mix + block / 32.0 + poll
    rep = {"instr": {"stream_run": run, "stream_decode": dec, "voice_out": out, "voice_upkeep": round(upkeep, 1),
                     "voice": round(V, 1), "voice_two_layers": round(V2, 1), "resonance": res, "reverb": rev, "eq": eq,
                     "mix_out": mix, "block": round(block / 32.0, 1), "poll": round(poll, 1), "bus": round(B, 1)}}
    print(f"erik37 cpu budget (pi32v2, {a[0].split('/')[-1]}), instructions a 44.1 kHz sample:")
    print(f"  a voice {V:.0f} (interpolation {run}, decode {dec} x {R}, filter/pan {out}, upkeep {upkeep:.0f}); "
          f"two layers {V2:.0f}")
    print(f"  the bus {B:.0f}: reverb {rev:.0f}, resonance {res:.0f}, EQ {eq:.0f}, mix/out {mix}, block {block / 32:.0f}, "
          f"keys {poll:.0f}")
    ok = True
    rows = []
    for v in sorted({16, 20, 24, nv}):
        typ = B + v * V
        worst = B + v * V + min(v, nx) * (V2 - V) + 4 * V + 4 * VN
        r = {"voices": v}
        for mhz in (240, 320):
            cyc = mhz * 1e6 / FS
            r[f"typ_{mhz}"] = round(typ * CPI / cyc * 100, 1)
            r[f"worst_{mhz}"] = round(worst * CPI / cyc * 100, 1)
        rows.append(r)
        print(f"  {v:2d} voices: typical {r['typ_320']:5.1f} % at 320 MHz ({r['typ_240']:5.1f} % at 240), worst "
              f"{r['worst_320']:5.1f} % ({r['worst_240']:5.1f} %)" + ("   <- the build" if v == nv else ""))
        if v == nv and r["worst_320"] > limit:
            ok = False
    rep["table"] = rows
    if "--report" in a:
        json.dump(rep, open(arg("--report", ""), "w"), indent=1)
    print(f"  {'ok  ' if ok else 'FAIL'} {nv} voices, the worst case within {limit:.0f} % at 320 MHz")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
