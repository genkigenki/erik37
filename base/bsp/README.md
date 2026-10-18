# felucca-bsp

A base for custom firmware on M-VAVE synthesizers built around JieLi chips.
It holds the chip layer (clock, interrupts, flash, USB, UART, timers, boot entry) and one
directory of board code per device. It has no sound code and no user interface.

Named after its first user, Felucca; it is not tied to that application.

Unofficial. Not affiliated with or endorsed by M-VAVE or JieLi. Product and company names
belong to their owners.

> **Experimental parts.** Some features here are **beta**: the second core (`chip/fm1_cpu1.*`) and expanded flash
> (`chip/fm1_flash_ext.h`). Others are only a **concept** that does not run: BLE, USB host, a higher clock. None has run
> on a device yet. What is established and what is not: [docs/EXPERIMENTAL.md](docs/EXPERIMENTAL.md). The safe test
> firmware (`examples/hwtest/hwtest.bin`) contains none of them. Working Bluetooth MIDI and WiFi, on JieLi's own
> radio libraries, are in [fm1-base](https://github.com/charlesvestal/fm1-base), which builds on this repository.

> **Licences: settled.** The files here are MPL-2.0. Leo Kuroshita confirmed it for the Felucca code he owns
> ([#4](https://github.com/hugelton/felucca-bsp/issues/4)), and keremimo for his part of `chip/usb.c`
> ([#3](https://github.com/hugelton/felucca-bsp/pull/3)). Details in [LICENSING.md](LICENSING.md) and
> [NOTICE](NOTICE).

> **Licence notes before you start**
> - A fork of Felucca cannot move to MPL-2.0 by using this repository. To use another licence, start a new project and import this one: [Forks of Felucca](LICENSING.md#forks-of-felucca).

> **Status: beta.** Split out of [Felucca](https://github.com/hugelton/Felucca). Firmware is built on it through
> [fm1-base](https://github.com/charlesvestal/fm1-base), and two firmwares are its proof points so far:
> [CTL-1](https://github.com/charlesvestal/fm1-ctl) (Bluetooth MIDI) and [Radio](https://github.com/charlesvestal/fm1-radio)
> (WiFi). Expect changes to file names and layout.

## Devices

| Device | Status |
| --- | --- |
| M-VAVE FM-1 | Supported. Used by Felucca. |
| M-VAVE SMK-37 (Pro, Elite, MKE-P37) | `boards/smk37/` (`-DBOARD_SMK37=1`): keys with velocity, buttons, encoders, pads, faders, wheels, button and pad LEDs, the screen's wiring, the flash map. The board facts were checked on an SMK-37 Pro. |

"Planned" means the device is a target, not that it works. A device gets its own directory
under `boards/` once someone has it running. Pin maps and other board facts are added only
when they have been checked on a real unit.

## Layout

| Path | What |
| --- | --- |
| `chip/` | Registers, drivers and boot entry shared by the chip family; `libc.c` (the few routines a freestanding build needs); `ota.c` (the update session, which takes its hooks from the application); `fm1_perf.h` (cycle and stall counters) and `fm1_debug.h` (register access for development builds) |
| `chip/usb.c` | The USB device: MIDI, serial and audio classes with the EP0 and poll code |
| `loader/` | The update loader (builds as a RAM image; it includes `chip/usb.c`, `chip/ota.c` and `chip/libc.c`) |
| `boards/fm1/` | FM-1 wiring and drivers: LCD pins, key scan, ADC channels, audio codec lines |
| `boards/smk37/` | M-VAVE SMK-37 wiring and drivers under the same header names: the key, button and encoder matrix, pads, faders and wheels, LEDs, LCD pins and the pad LEDs, and `fm1_flash_map.h`, the flash windows the driver, `ota.c` and the loader keep to on it |
| `ble/` | **Concept, does not run yet.** A minimal BLE-MIDI sender over standard HCI; tested on a PC against a fake controller only ([ble/README.md](ble/README.md)) |
| `examples/hwtest-ble/` | **Concept, does not run.** Glue between `ble/` and JieLi's radio controller, and a survey of what linking it needs; no firmware ([README](examples/hwtest-ble/README.md)) |
| `docs/EXPERIMENTAL.md` | Second core, expanded flash, USB host, BLE, clock: what is established and what is not |
| `docs/CPU.md` | What the CPU has been seen to do: 64-bit multiplies among 16-bit loads leave a register a bit off; keep them out of line |
| `tools/` | Package builder (`fm1pkg.py`) and USB-MIDI installer (`fm1_install.py`) |
| `examples/hwtest/` | Test firmware for a real FM-1: screen, knob, buttons, LEDs, USB serial. `hwtest.bin` is the safe image; `hwtest-exp.bin` adds the experimental parts |
| `examples/felucca/` | Reference only: `ota_hw.c`, the hooks Felucca gives `ota.c`. Not built here |

Headers include each other by file name. Build with `-Ichip -Iboards/fm1` (the SMK-37: `-DBOARD_SMK37=1 -Ichip -Iboards/smk37`). The loader builds from this tree with
the JieLi toolchain and gives the same image as it does inside Felucca.

`tools/` has the package builder, the USB-MIDI installer and `fm1_rescue` (stock firmware back on an FM-1 that cannot
start, through the chip's update mode; macOS) (the loader marker `FELUCCA-LOADER-1` is part of the loader's contract). The loader, `ota.c` and `usb.c`
still use `FELUCCA_*` macro names; they will be renamed. `usb.c` needs `uac_fir.h` (generated by Felucca's tools) only
when its 48 kHz audio option is on.

## Licence

[MPL-2.0](LICENSE), applied per file: if you change a file from this repository and
distribute it, you publish your change to that file. Your own code on top of it may use any
licence. This repository can be combined with GPL code (no "Incompatible With Secondary
Licences" notice). See the notes at the top. Credits, including the JieLi SDK (Apache-2.0), are in [NOTICE](NOTICE).

## Firmware that uses this base

| Firmware | Device | Licence | About |
| --- | --- | --- | --- |
| [Felucca](https://github.com/hugelton/Felucca) | M-VAVE FM-1 | GPL-3.0-only | Multi-engine synthesizer firmware with a sequencer and a web editor. |
| [fm1-base](https://github.com/charlesvestal/fm1-base) | M-VAVE FM-1 | MPL-2.0 | A base and template for FM-1 firmware: platform, host simulator, browser emulator, Bluetooth MIDI, WiFi, USB audio. |
| [CTL-1](https://github.com/charlesvestal/fm1-ctl) | M-VAVE FM-1 | MPL-2.0 | A MIDI controller with velocity and aftertouch, scales and chords, an arpeggiator, a Euclidean drum sequencer and GM sounds; USB and Bluetooth MIDI. On fm1-base. |
| [Radio](https://github.com/charlesvestal/fm1-radio) | M-VAVE FM-1 | MPL-2.0 | An internet radio over WiFi: Radio Garden's stations, MP3 and HE-AAC, punch-in effects. On fm1-base. |

Using the base? Open an issue with the "List your firmware" form. We add the link and a short
description. Rules are in [LISTING.md](LISTING.md).

## Contributing

Issues and pull requests are welcome. By contributing you agree that your change is licensed
under MPL-2.0. Keep changes small and say which device you tested on.
