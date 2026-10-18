# Erik37 sample sources

The question: which freely licensed piano multisample can be shipped, cut down to about 300-450 KB, inside a free
firmware. That needs a licence that allows redistribution of (modified) samples: CC0, CC-BY, public domain or an
explicit "use without restrictions". Checked October 2026. Downloads go into `samples-src/<name>/`, which is never
committed.

## Recommendation

**Salamander Grand Piano V3 (Yamaha C5), CC-BY 3.0.** It is the best recording of the three tested. It has
16 velocity layers, so the bank can pick the layer velocities it wants, and it ships its own chromatic key-release
noises. The noise floor is low (-55 to -74 dB below the note peak), and the files are cleanly trimmed (about 10 ms
pre-roll). The only obligation is attribution, which fits in the firmware's about/credits text and the README:

> Piano samples: "Salamander Grand Piano V3" by Alexander Holm, CC BY 3.0
> (https://creativecommons.org/licenses/by/3.0/), resampled, trimmed, looped and ADPCM-compressed for Erik37.

**Runner-up: VCSL Grand Piano, Steinway B, CC0.** It carries no obligations at all and is a good instrument, but
it has only 3 layers. They are normalized (the dynamics have to be rebuilt), sampled in whole tones only, and stop
at G#7. Its release files are mostly noise. Use it if attribution must be avoided entirely.

**Not recommended: University of Iowa MIS piano.** The licence is fine, but the pp layer is unusable (see below)
and some top-octave files contain clicks.

## Tested candidates

| | Salamander Grand V3 | VCSL Grand Piano, Steinway B | Iowa MIS Piano |
|---|---|---|---|
| Piano | Yamaha C5 | Steinway Model B | Steinway Model B |
| Licence | CC BY 3.0 | CC0 1.0 | "without restrictions" (no formal licence) |
| Layers | 16 | 3 (vl2-vl4) | 3 (pp, mf, ff) |
| Sampling | minor thirds, A0-C8 (30 notes) | whole tones, A#0-G#7 (42 notes) | chromatic, A0/Bb0-C8 |
| Format | 48 kHz/24-bit (44.1/16 version used) | 44.1 kHz/24-bit stereo | 44.1 kHz/16-bit stereo AIFF (DAT) |
| Mics | 2x AKG C414, AB, ~12 cm over the strings | close pair | 2x Neumann KM84, 8" over bass / treble strings |
| Noise floor (re. peak) | -55 (v1) to -74 dB (v16) | -50 (soft) to -82 dB (loud) | ff/mf ~-60 dB; **pp -8 to -25 dB** |
| Release samples | 88 key-release noises, string-resonance releases, 4 pedal noises | per-note "NoSusRel", but -40 to -63 dBFS: mostly noise | none |
| Download | 394 MB (44.1/16 tar.xz) | 544 MB (NoSus + Rel used) | 1.4 GB (260 files) |

Measured in the Erik37 bank builder (18 zones, 450 KB):

| | Salamander | VCSL | Iowa |
|---|---|---|---|
| ADPCM SNR (mean) | 51.5 dB | 48.7 dB | 49.4 dB |
| Loop correlation (median / worst) | 0.87 / 0.57 | 0.78 / 0.33 | 0.86 / 0.50 |
| Largest pitch shift of a key from its zone root | 3 semitones | 4 semitones (top) | 2 semitones |

### Salamander Grand Piano V3

- Source: https://archive.org/details/SalamanderGrandPianoV3, mirrored at
  https://freepats.zenvoid.org/Piano/acoustic-grand-piano.html (the 44.1/16 download used here), SFZ port at
  https://github.com/sfzinstruments/SalamanderGrandPiano.
- The README shipped with the samples says: "Licence: CC-by http://creativecommons.org/licenses/by/3.0/ Author:
  Alexander Holm". archive.org lists "Attribution 3.0". The sfzinstruments port says: "This work is licensed under
  a Creative Commons Attribution 3.0 Unported License."
- One repackager (an abcjs soundfont repository) claims it is "now public domain". There is no primary statement
  of that, so treat it as CC-BY.
- Quality: bright, close, very consistent. "Re-exported all notes with all lowcut filters removed" (V2 changelog).
  The AB pair is nearly decorrelated (side ~ mid). Erik37 folds to mono after aligning the channels' delay, then
  re-spreads by key.

### VCSL (Versilian Community Sample Library), Grand Piano, Steinway B

- Source: https://github.com/sgossner/VCSL (`Chordophones/Zithers/Grand Piano, Steinway B/`).
- README: "This collection is under a Creative Commons 0 license. Essentially it's Public Domain- you can do
  whatever you want with these sounds (even make commercial software), no royalties, no credit, no special
  terms." The repository's LICENSE file is CC0 1.0 Universal.
- The folder's NORMALIZED.txt: "The sustain and non-sustain articulations for this instrument have been
  normalized. Use a velocity curve to re-generate dynamic range."
- Quality: a warm, close Steinway with a good bass. It has pedal-down ("Sus") and pedal-up ("NoSus") sustains;
  NoSus is used here. The soft layer is noisier after normalization. The files start exactly at the hammer, so the
  builder adds a 1 ms fade-in.
- The same library also has a CC0 Kawai grand ("Grand Piano, Kawai", irregular note map, marked legacy), not tested.

### University of Iowa Electronic Music Studios, MIS piano

- Source: https://theremin.music.uiowa.edu/MISpiano.html (Steinway & Sons model B, recorded 2001 by Evan Mazunik).
- Terms, on https://theremin.music.uiowa.edu/MIS.html: "Since 1997, these recordings have been freely available on
  this website and may be downloaded and used for any projects, without restrictions." There is no formal licence
  and no attribution requirement; the page asks to be told how the samples are used.
- Quality problems found:
  - The pp files peak at only -40 to -49 dBFS on 16-bit DAT, with hiss and room noise 8-25 dB below the note.
    The Erik37 Iowa bank therefore uses mf and ff only.
  - Long, varying silence or rumble before the notes, which needed a more robust onset detector.
  - Clicks in some top-octave files, present in the source too.
  - The page has a broken link (mf.Gb7 points to oops.html).

## Other candidates (checked, not tested)

| Candidate | Licence found | Verdict |
|---|---|---|
| Sofia MZ libraries: c.1930 Hamburg **Steinway D**, 1969 **C. Bechstein D282**, Bösendorfer Imperial, Fazioli F308, Blüthner (20 layers, 48/24) | Secondhand forum quotes: "Creative Commons Attribution" for sample-library use, CC0 for music made with them; Musical Artifacts lists the Bechstein as CC BY 3.0. The author's site (luciphercode.wixsite.com/sofia-mz) returns 404; no licence file seen. | The only free Steinway D / Bechstein found. Worth a test **if** a download with a licence file turns up; not usable on hearsay. |
| Ivy Audio "Piano in 162" (Steinway B) | Press (2014): "free of charge for personal or commercial use"; no redistribution grant found; torrent-only now | Out: no right to redistribute the samples |
| Maestro Concert Grand, Mats Helgesson (Yamaha CF-3) | mcgreadme.txt: "You may not modify and spread this soundfont without the author's written permission." | Out |
| Headroom Piano, Bengt Nilsson (Yamaha C3; 5 layers, close + Decca tree) | sfzinstruments repository LICENSE: CC BY 4.0; mapped "with the author's permission as long keeping the original instrument name" | Possible CC-BY alternative; not tested |
| Splendid Grand Piano (Akai Steinway, 4 layers) | Repackager: "released as public domain in early 2000 by Akai"; no primary source | Out: provenance not verifiable |
| YDP Grand Piano, FreePats (Yamaha Disklavier Pro, Zenph samples for OLPC) | "Creative Commons Attribution 3.0" | Possible; SF2 only, a lesser recording |
| Upright Piano KW, FreePats (Kawai upright, Zoom H1) | "Creative Commons CC0 1.0 public domain dedication" | Not a grand |
| Osiris Piano, Karoryfer/Versilian (Yamaha C2, soft pedal, half-stick) | CC0 (LICENSE in the sfzinstruments repo) | Soft-only character; not a general grand |
| Scarypiano, Karoryfer | CC0 | Processed Iowa samples (effect instrument) |
| Commercial libraries (Ivory, Garritan, Keyscape, Pianoteq, ...) | proprietary | Out |

No free, verifiable Steinway D or Bechstein multisample with a redistribution licence was found. The public-domain
or CC0 options are VCSL (Steinway B, Kawai) and Iowa (Steinway B, effectively unrestricted).
