![Erik37](design/marketing/banner-1280x400.png)

# Erik37

A grand piano for the M-Vave SMK-37: a sampled concert grand with velocity layers, a key-by-key decay, dampers and
their noise, the three pedals, sympathetic string resonance and four rooms. It is its own firmware, built on
[fm1-base](https://github.com/charlesvestal/fm1-base) and [felucca-bsp](https://github.com/hugelton/felucca-bsp).

**Status: 1.0, experimental.** Played on an SMK-37 Pro. Video: https://youtu.be/5yUCB1-OJNE

Install it with the installer at <https://genkigenki.github.io/smk37/> (Chrome or Edge), the keyboard on USB.
There is one package per model: SMK-37 Pro (and the Starrykey 37 Play), SMK-37 Elite and MKE-P37. M-Vave's updater
takes the stock firmware back at any time.

![the play screen](app/site/shot-erik37.png)

## Playing

| Control | What it does |
| --- | --- |
| Keys | the piano, with velocity through the touch curve (TONE & TOUCH); C3..C6, OCTAVE- / OCTAVE+ move them by octaves |
| PEDAL jack | sustain; a continuous pedal gives half pedal. Plug it in before switching on: its rest position is learnt then |
| Pads 1-4 | tone: CONCERT, STUDIO, VINTAGE, JAZZ |
| Pads 5-8 | room: DRY, CHAMBER, HALL, CHURCH |
| Pads 9-11 | the pedals as under a grand: UNA CORDA, SOSTENUTO, SUSTAIN. Held: down while held; tapped: latched until the next tap |
| Pad 12 | string resonance on / off |
| Pads 13-15 | brilliance: mellow, natural, brilliant |
| Pad 16, SEQ PLAY | the metronome; pad 16 held, K1 sets its tempo (shown in the header) |
| SEQ REC | the sustain pedal (as pad 11) |
| Mod wheel | the sustain pedal, with half pedal |
| K1..K4 | the page's four knobs (the strip at the bottom of the screen); the faders do the same once moved |
| K5 MASTER | the volume |
| K6 SELECT, < / > | the page: play, TONE & TOUCH, ROOM, ABOUT |
| K7 ALGORITHM | the tone |
| K8 PRESETS | the room |
| PATCH | back to the play screen; held, the audio load and the voices in the header |
| BT | save the settings now (they also save themselves once the piano is quiet) |
| FX | every note let go |
| USB-MIDI | the keys and pedals go out (notes, CC 64 / 66 / 67); notes and those pedals come in and play the piano |
| OCTAVE- + OCTAVE+ held 5 s | update mode |

The pages: the play screen (a view into the piano: the strings light up as they sound, the dampers lift), TONE &
TOUCH (touch curve, brilliance, resonance, how far una corda goes), ROOM (room, size, amount, stereo width), ABOUT.
The pad lights: off a dim warm grey, the current choice and a latched pedal ivory, a held pedal bright gold.

## The sound

- The Salamander Grand Piano V3 (a Yamaha C5): 18 notes across the keyboard in 3 velocity layers, crossfaded in the
  attack, about 380 KB of block-ADPCM samples (4.25 bits a sample, 22.05 to 32 kHz), looped after the attack.
- Each key's decay is measured from the recording (a fast first decay, then the aftersound), faster in the treble.
  The top keys have no dampers, as on a grand.
- The brightness follows the velocity and darkens as a note rings. Releasing a key brings the damper down, with its
  noise.
- Sustain with half pedal, sostenuto, una corda (softer and darker, its depth set on TONE & TOUCH).
- Sympathetic resonance: undamped strings ring along while the sustain pedal is down.
- Tones: CONCERT (as recorded), STUDIO (closer, brighter), VINTAGE (darker, the unisons a little apart), JAZZ
  (round, warm). Rooms: an 8-line FDN reverb, as chamber, hall or church, with a size knob.
- Polyphony: 20 notes, plus room for the ones fading out. The quietest released notes give way first.
- The phase 1 study (the sample sources, the demos, the engine's prototype) is in `SOURCES.md`, `LISTEN.md` and
  `tools/`.

## Building

On Linux or WSL with JieLi's toolchain (`base/tools/get_toolchain.sh`), the AC79 SDK (for the package's SDK files)
and Python 3 with Pillow and NumPy:

```sh
git clone --recursive https://github.com/genkigenki/erik37
./build.sh                          # the three packages into build/erik37/ (and SHA256SUMS), the host simulator
app/tests/run_tests.sh              # the engine, the CPU budget, the flash, the loader, the packages, the screens
app/build/smk37/host/erik37-smk37_host app/tests/screens.txt /tmp/shots   # the simulator: a script, screenshots
```

`base/` is [fm1-base](https://github.com/charlesvestal/fm1-base) (Charles Vestal, MPL-2.0) with [felucca-bsp](https://github.com/hugelton/felucca-bsp) in `base/bsp` (Leo Kuroshita, MPL-2.0), included here with their SMK-37 board support and two additions for the SMK-37: the keybed's velocity and the PEDAL jack
(`plat_keybed_vel`, `plat_pedal_raw`).

## Licence

MPL-2.0. The piano samples are the Salamander Grand Piano V3 by Alexander Holm, CC BY 3.0. See `LICENSING.md`.
