# Erik37 phase 1: the demos

Rendered by the engine's host prototype (`tools/proto.sh`, `tools/demos.py`) into `demos/`, 44.1 kHz, 16-bit,
loudness-matched within each group (BS.1770). The WAV files are not in git: `tools/proto.sh` makes them from the
sample sources in `SOURCES.md`.

The phrase is an original piece in 3/4, about 35 s long:

| Time | What happens |
| --- | --- |
| 0-9 s | pp chords with legato pedal over a low line |
| 9-16 s | a crescendo |
| 16-20 s | ff over a low A0 |
| 20-22 s | dry staccato chords (the dampers) |
| 22-25 s | a fast run to C8 and back |
| 27-35 s | a pp ending: a high chord and a low A0 ringing, then the pedal lifted |

| File | What |
| --- | --- |
| `01_phrase_salamander_450kb.wav` | the engine with Salamander (Yamaha C5), 450 KB, hall: **the one chosen for the firmware** (which uses 400 KB) |
| `02_phrase_vcsl_450kb.wav` | the same with VCSL's Steinway B (CC0) |
| `03_phrase_iowa_450kb.wav` | the same with the Iowa Steinway B (its pp layer is unusable: mf and ff only) |
| `04_phrase_salamander_300kb.wav` | Salamander at 300 KB (16 notes x 2 layers) |
| `05_phrase_hybrid_salamander.wav` | the hybrid: Salamander's attack, then partials measured from the same notes |
| `06_phrase_modelled.wav` | modelled: partials from a physical recipe and a synthesized hammer, no samples |
| `1x_dry_<source>_engine.wav` / `_source.wav` | the engine dry, against the source's own full recordings played straight |
| `2x_notes_<source>_source_then_engine.wav` | single notes, each first as the source's untouched file, then the engine |
| `30_features_salamander.wav` | a pedalled chord without and with resonance, a line normal and with una corda, the rooms |

What to listen for: the loops in long notes (the climax, the final A0), the layer changes in the crescendo, the
dampers and their noise in the staccato chords, the treble run, and the room's tail at the end.

The choice for the firmware is the sampled engine with Salamander. It has the best recording and the lowest cost:
the prototype's voices measured 105 (sampled), 258 (hybrid) and 322 (modelled) host instructions a voice and a sample
(-O2, counted by `tools/icount.c`). The firmware's sampled voice is about 110 pi32v2 instructions
(`app/tests/target_budget.py`). The hybrid's partial state would also need about 40 KB more RAM.
