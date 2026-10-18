#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Erik37: Copyright (C) 2026 M. Geisler (genkigenki/makkuro)
"""The demo phrase (an original piece, 3/4, ~45 s) as an event list for host/render and tools/render_ref.py.

  phrase.py OUT.txt

Soft opening with pedal, a crescendo to a ff chord over a low A0, dry staccato chords (dampers), a fast treble
run to C8 and back, a pp ending: a high chord and a low A0 left to ring, the pedal lifted at the end."""
import sys

BEAT = 0.75
ev = []


def note(bar, beat, key, vel, beats):
    t = (bar * 3 + beat) * BEAT
    ev.append((t, "on", key, vel))
    ev.append((t + beats * BEAT - 0.02, "off", key, 0))


def pedal(bar, beat, down):
    ev.append(((bar * 3 + beat) * BEAT, "sus", 127 if down else 0, 0))


def legato_pedal(bar):
    pedal(bar, 0, False)
    pedal(bar, 0.15, True)


# A: pp, bars 0-3
A = [(33, [52, 57, 59, 60], None),
     (29, [57, 60, 64], [(76, 0, 2, 50), (74, 2, 1, 44)]),
     (38, [53, 57, 60, 64], [(72, 0, 2, 48), (71, 2, 1, 42)]),
     (28, [56, 59, 62], [(68, 0, 3, 46)])]
for bar, (bass, chord, mel) in enumerate(A):
    legato_pedal(bar)
    note(bar, 0, bass, 46, 3)
    for k in chord:
        note(bar, 1, k, 34, 2)
    for k, b, d, v in mel or []:
        note(bar, b, k, v, d)
# B: crescendo, bars 4-6
B = [(33, 45, [69, 72, 76], [81, 84, 88], 66),
     (29, 41, [69, 72, 77], [81, 84, 89], 80),
     (31, 43, [71, 74, 79], [83, 86, 91], 96)]
for i, (b1, b2, ch, top, v) in enumerate(B):
    bar = 4 + i
    legato_pedal(bar)
    note(bar, 0, b1, v, 3)
    note(bar, 0, b2, v - 6, 3)
    for k in ch:
        note(bar, 1, k, v - 4, 2)
    for j, k in enumerate(top):             # broken chord in eighths
        note(bar, 1 + j * 0.5, k, v - 10 + 4 * j, 0.5)
# bar 7: the climax, A0 + A1 under a full A minor chord, ff, pedal held through bar 8
legato_pedal(7)
note(7, 0, 21, 122, 5.5)
note(7, 0, 33, 118, 5.5)
for k in (57, 60, 64, 69, 72, 76):
    note(7, 0, k, 112, 5)
# bar 9: dry staccato chords (pedal up: hear the dampers)
pedal(9, 0, False)
for j, v in enumerate((88, 80, 72)):
    for k in (64, 68, 71):
        note(9, j, k, v, 0.2)
    note(9, j, 40, v - 8, 0.2)
# bar 10: a fast run A5 -> C8 (A minor), then back down an arpeggio to E6
run = [81, 83, 84, 86, 88, 89, 91, 93, 95, 96, 98, 100, 101, 103, 105, 107, 108]
t0 = 10 * 3 * BEAT
pedal(10, 0, False)
for i, k in enumerate(run):
    t = t0 + i * 0.075
    v = int(96 - 30 * i / len(run))
    ev.append((t, "on", k, v))
    ev.append((t + 0.11, "off", k, 0))
down = [105, 100, 96, 93, 88]
for i, k in enumerate(down):
    t = t0 + len(run) * 0.075 + 0.1 + i * 0.13
    ev.append((t, "on", k, 58 - 4 * i))
    ev.append((t + 0.2, "off", k, 0))
# bars 12-14: pp ending, pedal down; a high chord, then the low A0 alone; pedal lifted at the very end
pedal(12, 0, True)
for k in (81, 84, 88, 93):
    note(12, 0, k, 34, 2.5)
note(12, 2, 57, 30, 3)
note(13, 1, 21, 40, 3)
note(13, 1, 33, 32, 3)
pedal(15, 1.5, False)

ev.sort(key=lambda e: (e[0], e[1] != "off"))
with open(sys.argv[1], "w") as f:
    f.write("# Erik37 demo phrase: time on/off key vel | sus val\n")
    for t, ty, a, b in ev:
        f.write(f"{t:.4f} {ty} {a} {b}\n" if ty in ("on", "off") else f"{t:.4f} {ty} {a}\n")
print(f"{sys.argv[1]}: {sum(1 for e in ev if e[1] == 'on')} notes, {ev[-1][0]:.1f} s")
