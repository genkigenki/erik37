# Licensing

**Erik37 is MPL-2.0** (full text in `LICENSE`), file by file: every source file carries an
`SPDX-License-Identifier: MPL-2.0` header. That covers the firmware (`app/`), the tools (`tools/`, `host/`) and the
design scripts and their images (`design/`).

## What Erik37 is built from

| What | Licence | Where |
| --- | --- | --- |
| fm1-base (the platform) and felucca-bsp (chip, board, update loader, packaging) | MPL-2.0 | `base/` and `base/bsp/` (git submodules); their own `LICENSING.md` lists what they are built from |
| **The piano: "Salamander Grand Piano V3" by Alexander Holm** | **CC BY 3.0** | `app/assets/erik37.bank`, inside every package |
| Cinzel (The Cinzel Project Authors), Libre Caslon Text (The Libre Caslon Text Project Authors) | SIL OFL 1.1 | `design/fonts/` (the TTF files and their licences); the firmware carries glyphs rasterised from them |
| fm1-base's own fonts (Barlow Semi Condensed, for its platform screens) | SIL OFL 1.1 | `base/assets/fonts/` |
| JieLi AC79 SDK files a package needs (`uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin`) | Apache-2.0 | read from your SDK checkout at build time; never in this repository |

## The piano samples

The sound bank `app/assets/erik37.bank` is derived from the **Salamander Grand Piano V3** by **Alexander Holm**,
licensed under the [Creative Commons Attribution 3.0 Unported licence](https://creativecommons.org/licenses/by/3.0/)
(source: <https://archive.org/details/SalamanderGrandPianoV3>). It was changed for Erik37: 18 of the notes in 3 of
the velocity layers chosen, folded to mono, trimmed at the hammer, resampled (22.05 to 32 kHz), looped with a
crossfade after the attack, the decay taken out (the firmware puts it back per key) and ADPCM-compressed; the
release noises shortened. The bank is a derivative of the recording and stays under CC BY 3.0.

The firmware names the source and its licence on its ABOUT page. When you pass the firmware or the bank on, keep
this attribution:

> Piano samples: "Salamander Grand Piano V3" by Alexander Holm, CC BY 3.0
> (https://creativecommons.org/licenses/by/3.0/), resampled, trimmed, looped and compressed for Erik37.

The other candidate sources Erik37 was tried with, and their terms, are in `SOURCES.md`. Their samples are not in
this repository.

## The fonts

Cinzel and Libre Caslon Text are under the SIL Open Font License 1.1 (`design/fonts/OFL-cinzel.txt`,
`design/fonts/OFL-librecaslontext.txt`). The firmware embeds 4-bit bitmaps rendered from them
(`app/tools/gen_assets.py`), which the OFL allows. The font software is not sold, and the Reserved Font Names are not
used for anything derived.
