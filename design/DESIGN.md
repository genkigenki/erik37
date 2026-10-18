# Erik37: design

A grand piano for the M-Vave SMK-37's 240 x 240 RGB565 screen. The style is black piano lacquer, fine brass
lines, ivory type, Roman capitals for names and headings, and labels set in italic like the directions in a
score ("una corda", "brilliance"). Colour is held back: everything is black, gold and ivory, plus one deep
wine for the damper felt.

Regenerate everything with `python make_design.py` (add `--anim` for the animations). It needs Pillow and
NumPy. Every screen is a function of its state (`screens.play_screen(...)`, `pages.tone_touch_screen(...)`,
`pages.room_screen(...)`, `boot.boot_frame(...)`), so the mockups can be read as a spec for the firmware's
drawing code.

## Files

| Path | What |
|---|---|
| `boot/boot-01..09.png` (+ `@4x`) | boot storyboard: rim of gold in the dark, piano, lid opening, gold plate shows, open, glint along the rim, name, final |
| `boot/storyboard.png`, `boot/boot.webp` | the frames with captions; the animation (3.6 s + hold, 66 ms a frame) |
| `screens/play*.png` (+ `@4x`) | play screen: chord with sustain, idle, half pedal + una corda (VINTAGE / CHURCH), sostenuto |
| `screens/tone-touch.png`, `room*.png` (+ `@4x`) | settings page I (TONE & TOUCH), page II (ROOM: hall, chamber, church) |
| `pads/pads.png` | pad lights: map, the three levels, LED values |
| `marketing/banner-1280x400.png` | README banner: rounded card, transparent outside, like `work/banners` |
| `marketing/thumbnail-1280x720.png`, `card-1200x630.png` | YouTube thumbnail, social / OG card |
| `site/shot-erik37.png`, `.webp` | site card screen (240 x 240), animated: boot, then a phrase on the play screen |
| `sheet.png` | contact sheet |
| `fonttest.png` (+ `@4x`) | legibility test of the candidate faces at 10-15 px, rasterised as the firmware would draw them |
| `erik37_gfx.py` | palette, fonts, canvas, primitives (one-to-one with firmware drawing) |
| `screens.py`, `pages.py`, `boot.py`, `pads.py`, `marketing.py`, `make_design.py` | the drawing code |

## Pixel rules (how the mockups are rendered, and what the firmware does the same way)

* **Output:** RGB565 with a **4 x 4 ordered (Bayer) dither**: add `BAYER4[y&3][x&3] / 16` of one 565 step
  before truncating. Without it the dark lacquer gradients (levels 5-16) band badly.
* **Shapes** use 4 x 4 supersampled coverage, giving 16 levels: the same 4-bit alpha as the AA fonts.
* **Text:** TTF glyphs pre-rasterised with 4-bit alpha (speak37 `tools/gen_aa_font.py`, with more faces).
* **Light** (string glow, glints, pad halos) is *added* to the frame buffer, like vector37 `cv_add()` / `glow()`.
  Glow falloff is `1 / (1 + (d/r)^2)^2`, which a small table can do.
* **Lines:** 1 px. Gold rules fade linearly at the ends: header rule over 70 % of each half, plain rules over 30 %.
  The header rule has a 5 px lozenge (r 2.5) in a 14 px gap at the centre.

## Palette

| Name | RGB888 | RGB565 | Use |
|---|---|---|---|
| lacq0 | 5, 5, 7 | 0x0020 | lacquer, deepest (bottom of the gradient) |
| lacq1 | 13, 13, 16 | 0x0862 | lacquer, top of the gradient |
| lacq_hi | 44, 44, 50 | 0x2966 | reflection band peak (mixed in at most ~45 %) |
| gold | 201, 164, 92 | 0xCD2B | brass lines, values, the current choice |
| gold_hi | 246, 218, 156 | 0xF6D3 | played strings, glint, the knob being turned |
| gold_dim | 112, 90, 52 | 0x72C6 | inactive brass, K tags, frames |
| gold_deep | 64, 50, 28 | 0x4183 | the plate in shadow, value tracks, separators |
| ivory | 238, 228, 206 | 0xEF39 | text, values |
| ivory_dim | 150, 142, 126 | 0x946F | labels, captions, ivory keys (x 0.82) |
| grey | 78, 72, 64 | 0x4A48 | choices not taken |
| felt | 58, 30, 32 | 0x38E4 | damper felt (felt_hi 96, 58, 56 = 0x61C7 on its top edge) |
| wood | 54, 38, 26 | 0x3123 | soundboard (x 0.32-0.42, faint grain), bridge |
| copper | 160, 104, 64 | 0xA348 | bass strings (alpha 0.42) |
| steel | 150, 150, 156 | 0x94B3 | treble strings (alpha 0.30) |

**Lacquer background:** a vertical gradient from lacq1 to lacq0, plus one soft diagonal reflection band
(a Gaussian across `0.62 x + y`, width 34 px, 55 %) and a thin second streak (22 %), both fading downwards.

## Fonts (all SIL OFL 1.1, files and licences in `fonts/`)

| Role | Face | Size / weight | Notes |
|---|---|---|---|
| Name (boot) | Cinzel | 34 px, wght 600, tracking +4 | Roman capitals with lining figures, so "37" sits level |
| Headings, tabs | Cinzel | 12 px (header, +1.5), 9 px (tabs, +0.6) | caps only; Cinzel has no lower case |
| Values | Libre Caslon Text | 13 px (12 px if wider than 50 px) | made for text, so it holds up best at small sizes |
| Labels, captions | Libre Caslon Text Italic | 11 px (labels), 10-11 px (captions) | lower case, like score directions |
| Dynamic mark | Libre Caslon Text Italic | 17 px, wght 700 | the last velocity as pp p mp mf f ff |
| K tags | Cinzel | 9 px, wght 600 | |

Rejected after `fonttest.png`: Cormorant Garamond (too thin and its figures too small below 13 px) and
EB Garamond / Playfair Display (their hairlines break up at 10-12 px). Cormorant is kept only for the test.

## Layout (by meaning, knobs at the bottom)

**Play screen**
* y 6-23: header. The tone on the left, the room on the right (Cinzel 12, ivory), gold rule at y 23 with the lozenge.
* y 27-131: the view into the piano from above, keyboard at the bottom.
  * The bentside rim narrows to the right: `rim_x(y) = 233 - 92 t^2.1`, with `t = (122 - y) / 96`.
  * The gold plate has four rose holes, two struts and a bevel (+30 % gold_hi on the top-left edges, -45 % on the bottom-right).
  * 37 strings at x = 12 + 6i. The 12 bass strings are copper, 2 px; the rest are steel pairs at x-1 / x+1.
  * The bridge follows `34 + 50 t^1.55`.
  * Tuning pins at y 125-127, staggered.
* Dampers (y 100-112, 5 px wide, 1 px gap) sit on notes 0-30. The top 6 notes have none, as on a real grand.
  * A damper lifts 4 px while its note sounds, under the sustain pedal (half pedal lifts 2 px), or when held by sostenuto.
  * A lifted damper leaves a 1 px shadow.
* A sounding string draws in gold to gold_hi, alpha 0.35 + 0.75 x level. It has a blurred glow (r 2.2) and a
  brighter spot where the hammer strikes (y 116). Level is the note's envelope, so a chord fades string by string.
* y 129-148: fallboard edge with a gold line at y 131, then the 37 keys C3-C6.
  * 22 white keys, 9 px wide on a 10 px pitch. Black keys are 6 x 9.
  * Pressed keys show gold, mixed by level.
* y 156-170: on the left the last dynamic (f, mf ...); in the middle the lyre with three brass pedals
  (una corda, sostenuto, sustain), each 12 x 5 r 2.5; on the right the keyboard range (C3 - C6, follows OCT).
  * Pressed pedal: gold_hi, 2 px lower, glow. Half pedal is drawn in between.
* y 184-240: knob strip, gold rule at y 184 (alpha 0.75).
  * Four 60 px columns, separated by gold_deep verticals that fade.
  * Each column, top to bottom: label (italic 11, ivory_dim), value (Caslon 13, ivory), then the K tag
    (Cinzel 9) with a value bar (gold over a gold_deep track, lozenge r 2.5 marker) or, for a choice,
    n lozenges with the current one bigger and gold.
  * The knob being turned has a gold_hi label and bar.
  * Main page: K1 tone, K2 room, K3 reverb, K4 the metronome's tempo. The tags read K1-K4 as the brief says. If
    the page knobs end up on the SMK's lower row, the tags read K5-K8; only the strings change.

**TONE & TOUCH (page I of II):**
* Header with two page lozenges at the right; under it the tone tabs.
* The velocity curve in a frame marked only at its corners, with quarter dotted grid lines.
  * The other curves (Soft x^0.55, Normal x^0.95, Hard x^1.7, Fixed) are drawn faintly.
  * The current curve is 1.4 px gold with a glow.
  * The last note played is an ivory dot on the curve with a dotted drop line.
  * pp / ff mark the axis ends.
* Caption: "Normal touch · last note 88".
* Knobs: touch, brilliance, resonance, una corda (how far the soft pedal goes).

**ROOM (page II):**
* Room tabs.
* The room's section in a 1 px gold line: chamber is a low coved room, hall a wide vault, church a pointed arch.
* A small brass grand stands on the stage line, with gold rings rising from it:
  * the number of rings follows the size (3 + 6 x size);
  * their brightness follows the amount.
* Stereo width: two ivory lozenges below the stage with a dotted span between them.
* Knobs: room, size (shown as decay in s), amount, width.

## Boot

* The piano is a small 3D model: a plan outline (Catmull-Rom bentside) extruded to the case, a keyboard,
  three tapered legs, a lyre with brass pedals, and a lid hinged on the spine.
* Camera: orthographic, yaw -20°, pitch 21°, fitted into the box (18, 14)-(222, 142).
* Faces are flat-shaded lacquer: diffuse 0.55 + 0.7 n·l, plus a Blinn specular (gloss 7). They are painted far to near.
* The model's mirror image, at 34 % and fading over 40 px, is the reflection on the stage floor.
* The firmware can play the boot from a precomputed table of projected polygons per frame (about 100 polygons
  of 4-60 points), filled with the same 16-level coverage.
* Over the model:
  * the rim's gold line (alpha rising from the keyboard end);
  * a fainter line on the case bottom;
  * the lid's edge line once the lid is open;
  * the glint, which brightens the rim line over a Gaussian of 7 % of its length, with a glow (r 3 and 9) and
    a four-ray star (rays 12 / 6 px).
* The lid eases open (cosine) from 0° to 42° and the prop stick appears past 14°. A dim gold plate with
  strings shows inside.
* Title: Cinzel 34 "ERIK37" at y 152, rule at y 196, "by genkigenki" in italic 12 at y 206, faded in.

## Pad lights (`pads/pads.png`)

Pads 1-8 are the top row and 9-16 the bottom, read like text.

Since 1.1 (the pedals moved to SCALE, CHORD and SEQ REC; `pads/pads.png` still shows 1.0's map):

| Pads | Function |
|---|---|
| 1-4 | the voices: CONCERT, STUDIO, VINTAGE, JAZZ (a tone in its room) |
| 5-16 | the user presets USER 1-12: a short press recalls, held a second saves the sound there |

| Pads | LED value | Meaning |
|---|---|---|
| voice, dim | 120, 72, 10 | amber: a voice to choose |
| voice, bright | 255, 140, 16 | bright gold: the voice chosen, a pad while pressed |
| preset, empty | 0, 0, 0 | dark |
| preset, dim | 72, 54, 34 | warm grey (1.0's "off", clearly visible): filled |
| preset, bright | 255, 190, 110 | warm ivory: the preset chosen, a pad while pressed |

The LED values carry little blue because the pads' LEDs turn warm tones cold. Check them on the hardware.
Toggles fire once per press (~180 ms debounce), and each toggle uses one pad.

## Marketing

* Same lacquer and gold.
* "ERIK37" in Cinzel 600 with a soft gold glow, the gold rule with its lozenge, a Caslon italic line, and a
  Cinzel caption line in gold.
* The real screens are scaled nearest-neighbour; non-integer scales go NN up, then box down.
* Screens sit in a slim lacquer bezel with a gold edge, a shadow and a faint reflection.
* The banner and card have the SMK's 37 keys as a fine gold outline.
* The banner is a rounded card (r 26), transparent outside, so it reads on GitHub's light and dark themes.
* No piano brand names or logos and no third-party art. Everything is drawn by the scripts.
