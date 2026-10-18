#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Erik37: Copyright (C) 2026 M. Geisler (genkigenki/makkuro)
"""The CPU budget on the AC791N (pi32v2), estimated two ways (Linux / WSL; JIELI_TOOLCHAIN or ~/.jieli/toolchain):

  cpu_budget.py BANK.bin MODAL_MEASURED.bin MODAL_SYNTH.bin [--report OUT.json]

1. Dynamic: the host engine (cc -O2) under tools/icount, instructions per 44.1 kHz sample, for: the bus alone, then
   N held voices in each mode (sampled single layer, sampled two-layer attack, hybrid, modelled); per voice = the
   difference / N.
2. Static: the engine built with the JieLi clang (-O2) for pi32v2; the instructions in the hot loops' bodies
   (stream_run, voice_out, erkm_block's partial loop, res_block, rev_block), as the repo's other budgets count.
   The pi32v2 / x86 ratio on the branch-free loops converts (1) to target instructions.
Cycles: 1.3 cycles an instruction (pi32v2: mostly single-cycle; loads, multiplies, taken branches cost more), against
320 MHz with audio on one core, budget 70 % (the rest: MIDI, UI, USB, the codec ISR)."""
import json
import os
import re
import subprocess
import sys

R = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
TC = os.environ.get("JIELI_TOOLCHAIN", os.path.expanduser("~/.jieli/toolchain"))
B = os.path.join(R, "build", "cost")
FS = 44100
CPI = 1.3
MHZ = 320
SHARE = 0.70


def sh(cmd, **kw):
    return subprocess.run(cmd, check=True, capture_output=True, text=True, **kw).stdout


def build():
    os.makedirs(B, exist_ok=True)
    src = [os.path.join(R, "host", "render.c"), os.path.join(R, "app", "dsp", "erk.c"), os.path.join(R, "app", "dsp", "erk_modal.c")]
    sh(["cc", "-O2", "-fno-tree-vectorize", "-DERK_MODAL=1", "-o", os.path.join(B, "render"), *src])   # scalar, like the target
    sh(["cc", "-O2", "-o", os.path.join(B, "icount"), os.path.join(R, "tools", "icount.c")])
    objs = []
    for f in ("erk", "erk_modal"):
        o = os.path.join(B, f + ".o")
        sh([os.path.join(TC, "pi32v2", "bin", "clang"), "-target", "pi32v2", "-O2", "-ffunction-sections",
            "-fno-builtin", "-DERK_MODAL=1", "-I" + os.path.join(R, "tools", "target"), "-c", os.path.join(R, "app", "dsp", f + ".c"),
            "-o", o])
        objs.append(o)
    dis = sh([os.path.join(TC, "common", "bin", "objdump"), "-d", *objs])
    open(os.path.join(B, "erk_pi32.dis"), "w").write(dis)
    return dis


def icount(bank, voices, warm, secs, vel, extra=()):
    out = sh([os.path.join(B, "icount"), os.path.join(B, "render"), bank, "--cost", str(voices), str(warm), str(secs),
              str(vel), *extra])
    n = int(re.search(r"icount (\d+)", out).group(1))
    st = dict(re.findall(r"(\w+) (\d+)", out.split("\n", 1)[1] if "\n" in out else out))
    frames = int(re.search(r"frames (\d+)", out).group(1))
    return n / frames, {k: int(v) for k, v in st.items()}, frames


LINE = re.compile(r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*\t(.*)$")


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
    """backward branches: [(start, end, body instructions, calls inside)], innermost first"""
    out = []
    for a, txt in ins:
        m = re.search(r"goto -?\d+ <[^:]*: ([0-9a-f]+) >", txt)
        if m:
            t = int(m.group(1), 16)
            if t <= a:
                body = [x for x in ins if t <= x[0] <= a]
                out.append((t, a, len(body), sum("call" in x[1] for x in body)))
    return sorted(set(out), key=lambda x: x[2])


def main():
    bank, mmeas, msyn = sys.argv[1:4]
    rep_path = sys.argv[sys.argv.index("--report") + 1] if "--report" in sys.argv else None
    dis = build()
    f = functions(dis)
    stat = {}
    for fn in ("stream_run", "voice_out", "erkm_block", "res_block", "rev_block"):
        ls = loops(f.get(fn, []))
        stat[fn] = {"instructions": len(f.get(fn, [])), "loops": [(n, c) for _, _, n, c in ls]}
    N = 24
    W, S = 0.4, 0.1
    bus, _, _ = icount(bank, 0, W, S, 100)
    runs = {
        "sampled (one layer, looping)": (100, (), W),
        "sampled (two layers, attack)": (51, (), 0.0),
        "hybrid (partials)": (100, ("-M", "hybrid", mmeas), W),
        "modelled (partials + hammer)": (100, ("-M", "model", msyn), W),
    }
    res = {"bus_x86_per_sample": round(bus, 1)}
    for name, (vel, extra, warm) in runs.items():
        n, st, fr = icount(bank, N, warm, S if warm else 0.15, vel, extra)
        act = st["voice_blocks"] / (fr / 32)
        per = (n - bus) / act
        res[name] = {"x86_per_voice_sample": round(per, 1), "voices_active": round(act, 1),
                     "streams_per_voice": round(st["stream_blocks"] / max(1, st["voice_blocks"]), 2),
                     "partials_per_voice": round(st["partial_blocks"] / max(1, st["voice_blocks"]), 1)}
    # the conversion: pi32v2 / x86 on the straight-line loops, x86 measured the same way (per iteration)
    vo_pi = min(n for n, c in stat["voice_out"]["loops"] if c == 0) if stat["voice_out"]["loops"] else 0
    # x86 voice_out iteration: from a pure-bus-free run is not separable; use the objdump of the host build
    xdis = sh(["objdump", "-d", "--no-show-raw-insn", os.path.join(B, "render")])
    xf = {}
    name = None
    for ln in xdis.splitlines():
        m = re.match(r"^[0-9a-f]+ <(\w+)>:", ln)
        if m:
            name = m.group(1)
            xf[name] = []
            continue
        mm = re.match(r"^\s*([0-9a-f]+):\s+(\S.*)$", ln)
        if name and mm:
            xf[name].append((int(mm.group(1), 16), mm.group(2)))
    def xloops(ins):
        out = []
        for a, txt in ins:
            m = re.match(r"j\w+\s+([0-9a-f]+)", txt)
            if m and int(m.group(1), 16) <= a:
                t = int(m.group(1), 16)
                out.append(sum(1 for x in ins if t <= x[0] <= a))
        return sorted(out)
    ratio = {}
    for fn in ("voice_out", "res_block", "rev_block"):
        xl = xloops(xf.get(fn, []))
        pl = [n for n, c in stat[fn]["loops"] if c == 0]
        if xl and pl:
            ratio[fn] = round(pl[0] / xl[0], 2)
    k = sum(ratio.values()) / len(ratio) if ratio else 1.0
    cyc_budget = MHZ * 1e6 * SHARE / FS
    res["pi32_over_x86"] = ratio
    res["ratio_used"] = round(k, 2)
    res["static_pi32"] = stat
    bus_pi = bus * k
    print(f"cpu budget: AC791N at {MHZ} MHz, {SHARE * 100:.0f} % for audio = {cyc_budget:.0f} cycles a sample, "
          f"{CPI} cycles an instruction")
    print(f"  pi32v2 / x86 instructions on the straight-line loops: {ratio} -> x{k:.2f}")
    print(f"  bus (resonance 12 combs + FDN reverb, half rate; mix, master): {bus:.0f} x86 -> ~{bus_pi:.0f} pi32v2 "
          f"instructions a sample")
    for name in runs:
        r = res[name]
        pi = r["x86_per_voice_sample"] * k
        poly = (cyc_budget - bus_pi * CPI) / (pi * CPI)
        r["pi32_per_voice_sample"] = round(pi, 1)
        r["polyphony"] = int(poly)
        r["mhz_24_voices"] = round((bus_pi + 24 * pi) * CPI * FS / 1e6, 1)
        print(f"  {name:32s} {r['x86_per_voice_sample']:6.1f} x86 -> ~{pi:5.0f} pi32v2 instr/voice/sample "
              f"(streams {r['streams_per_voice']}, partials {r['partials_per_voice']}): polyphony {int(poly)}, "
              f"24 voices = {r['mhz_24_voices']} MHz")
    for fn, s in stat.items():
        print(f"  static pi32v2 {fn:12s} {s['instructions']:4d} instructions, loop bodies {s['loops']}")
    if rep_path:
        json.dump(res, open(rep_path, "w"), indent=1)


if __name__ == "__main__":
    main()
