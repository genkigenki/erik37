# fm1-base

A base for M-VAVE FM-1 firmware, on [felucca-bsp](https://github.com/hugelton/felucca-bsp): the platform layer
above the hardware (screen, drawing and fonts, panel, LEDs, audio, MIDI over USB and TRS, storage, the screen reader),
a host simulator and a browser emulator that run the same app, and the tools to build, package and install it.
MPL-2.0: an app built on it may use any licence. See [DESIGN.md](DESIGN.md), [LICENSING.md](LICENSING.md) and
[CREDITS.md](CREDITS.md).

**Status: beta.** Two firmwares are built on it so far, and they are its proof points:
[CTL-1](https://github.com/charlesvestal/fm1-ctl) (with Bluetooth MIDI) and
[Radio](https://github.com/charlesvestal/fm1-radio) (with WiFi). Expect the platform interface (`include/plat.h`)
and `fm1.toml`'s settings to change before 1.0.

The template (`template/`) runs on an FM-1: screen, keys and their LEDs, sound, MASTER, encoders, settings kept
across a power cycle, USB-MIDI both ways, the screen reader, and an update started from the template itself. The same
app runs on the computer in the host simulator and in the browser: try the template's at
<https://charlesvestal.github.io/fm1-base/emu/>.

    git clone --recursive https://github.com/charlesvestal/fm1-base

## Optional parts

A firmware has only what its fm1.toml asks for (and its app.h, for velocity and pressure); every build prints the
parts it has.

| Part | How to include it | What the app calls |
| --- | --- | --- |
| Bluetooth MIDI with a computer or tablet | `ble = true` (`tools/build.py --no-ble` leaves it out of one build) | `TO_BLE`, `PORT_BLE`; `plat_ble_state`, `plat_ble_connected`; `plat_ble_enable` |
| Bluetooth MIDI devices (the FM-1 connects to a keyboard or controller) | `ble_central = true`, with `ble` | `plat_ble_scan`, `plat_ble_found`, `plat_ble_connect`, `plat_ble_disconnect`, `plat_ble_device` |
| USB audio: the FM-1 recorded over the cable | on unless `usb_audio_in = false` | nothing: the master output |
| USB audio: a computer plays through the FM-1 | on unless `usb_audio_out = false` (needs the input too) | nothing: mixed after the app, at MASTER |
| Velocity and pressure from the keys | `#define APP_TOUCH` in app.h | `touch_on`, `touch_poll`, `touch_keys`, `touch_vel`, `touch_pressure` (lib/touch.h) |
| WiFi: a station on the networks around, with lwIP's sockets | `wifi = true` (not with `ble`, yet), and `net = [...]` for the app's network code | `plat_wifi_on`, `plat_wifi_state`, `plat_wifi_scan`, `plat_wifi_found`, `plat_wifi_join`, `plat_wifi_saved`, `plat_wifi_forget`, `plat_net_task` |
| A page for Bluetooth MIDI devices: the scan while it's up, the list, SAVE to connect or let go, and what happened in words | `#define APP_BTDEV` in app.h, with `ble_central` | `btdev_page`, `btdev_event`, `btdev_word`, `btdev_found`, `btdev_choose`, `btdev_save`, `btdev_title` (lib/btdev.h) |

Every firmware also counts each port's MIDI messages, in and out: `plat_midi_count(PORT_*, out)` for counters,
`plat_midi_ago` for activity lights.

## Bluetooth and WiFi

A firmware has Bluetooth or WiFi, not both: each fills most of what the FM-1 has (the chip has no external RAM),
and together they don't fit. Each needs JieLi's AC79 SDK (`$AC79_SDK`) for its libraries; a build without either
links nothing of JieLi's.

### Turning Bluetooth on

1. In fm1.toml, `ble = true`. Add `ble_central = true` for the FM-1 to connect to MIDI devices too (a keyboard,
   a controller), beside a computer.
2. In app.h, give it a store object: `OBJ_BLE` in the app's object enum, and its offset in `APP_STORE_MAP` (the
   radio's address and pairing are kept there).
3. In the app: MIDI goes out with `TO_BLE` and comes in on `PORT_BLE`, as USB's does. For a page of devices, add
   `#define APP_BTDEV` to app.h and use lib/btdev.h (the template's REC does).

Bluetooth starts at every power-on, as on M-VAVE's firmware, unless the last Bluetooth run crashed. A firmware
that lets the player turn it off keeps that as a setting, calls `plat_ble_enable(0)` / `plat_ble_enable(1)` when it
changes, and tells the platform whether to start it at power-on with `APP_BLE_AT_BOOT` in app.h (an expression,
e.g. `(set.bluetooth != 0)`). Off ends every link and stops advertising and scanning: the radio is quiet. Its manual
should say where the setting is (the template's: for details, see template/docs/MANUAL.md).
`tools/build.py --no-ble` builds the same firmware without Bluetooth (NAME-usb.fwsc).

### Turning WiFi on

1. In fm1.toml, `wifi = true` (and not `ble`). Leave USB audio out (`usb_audio_in = false`, `usb_audio_out =
   false`): WiFi needs its RAM.
2. In app.h, two store objects: `OBJ_BLE` (the radio's address and calibration) and `OBJ_WIFI` (the networks
   kept), with their offsets in `APP_STORE_MAP`; and a smaller canvas, `#define APP_CANVAS_PX (240u * 48u)` (draw
   the screen in strips 48 rows high or less).
3. The app's network code goes in its own files, listed in fm1.toml's `net = [...]`: they're built as the SDK
   builds code that talks lwIP, and include `lwip/sockets.h`. That code runs in a task of its own
   (`plat_net_task`): a socket call waits, and the main loop must not.
4. In the app: `plat_wifi_on(1)` turns it on; it joins the networks it joined before (six, the most recent first).
   `plat_wifi_scan` and `plat_wifi_found` list the networks around, `plat_wifi_join` joins one (kept once joined),
   `plat_wifi_state` and `plat_wifi_ip` say how far it got. Passwords come over USB:
   `tools/fm1_remote.py wifi join SSID PASSWORD` (or typed on the keys: Radio has a page for it).

For a working example, see `examples/wifi`; for a whole firmware, fm1-radio.

### What each costs

The FM-1 has a 581 KB app slot in the flash, 96 KB of RAM for `.data` and `.bss` (128 KB in a WiFi build), and a
336 KB pool of RAM for big buffers (304 KB in a WiFi build). Measured on the template (October 2026), in KB:

| | Flash | RAM | Pool |
| --- | --- | --- | --- |
| The template without a radio (`--no-ble`) | 77 | 27 | 68 |
| Bluetooth MIDI with a computer (`ble`) | + 124 | + 45 | + 24 |
| Bluetooth MIDI devices too (`ble_central`) | + 8 | + 1 | 0 |
| WiFi (`wifi`, from fm1-radio's measurements) | + about 300 | + about 100 | + about 276 |

- Bluetooth: JieLi's BT controller and stack (the flash), its 24 KB heap and their tables (the RAM), three task
  stacks of 8 KB (the pool). The template with Bluetooth and devices is 210 KB of flash, 74 of 96 KB of RAM: room
  for a big app (CTL-1, with a General MIDI synth, is 491 KB).
- WiFi: JieLi's driver (146 KB), the WPA supplicant (77 KB), lwIP (51 KB), the radio layer, crypto and the shim
  (25 KB) in the flash; the driver's buffers, lwIP, the shim's task table and 24 KB of the heap (about 100 KB) in
  the RAM; the rest of the heap (248 KB) and lwIP's packet buffers (28 KB) in the pool. The heap, 272 KB, holds the
  WiFi library's own buffers (about 60 KB) and its tasks' stacks (4 to 8 KB each, by task, as a task starts: about
  6), and what is left is the app's (`malloc`; fm1-radio's HE-AAC player and HTTPS stream peak at 256 KB of it).
  That leaves an app about 170 KB of flash and a few KB of RAM.
  WiFi calibrates its radio at every start (a kept calibration was Bluetooth's, which the access point could not
  hear). `examples/wifi` is 420 KB of
  flash, 119 of 128 KB of RAM and 299 of 304 KB of pool; fm1-radio (MP3 and HE-AAC decoders, HTTPS) is
  576 KB, 127 KB and 300 KB.
- In a WiFi build, fm1-base's large font (the splash's name, the panel's screens) can go too:
  `#define APP_BIG_FONT FONT_M` in app.h saves 20 KB of flash.

## The M-Vave SMK-37

Every fm1-base firmware also builds for the M-Vave SMK-37 (the same AC791N; Pro, Elite and MKE-P37):
`BOARD=smk37 tools/build.py APP_DIR` (`MODEL=elite` / `mke`; or `--board`, `--model`) builds with felucca-bsp's
`boards/smk37`, the SMK-37's flash map and package layout, into `APP_DIR/build/smk37/NAME-smk37.fwsc`. The FM-1 build
is unchanged. The platform maps the SMK-37's controls by name (SCALE = SEL, CHORD = ENV, NOTE REPEAT = LFO, PARA =
EDIT, GLOBE = GLO, PATCH = HOME, BT = SAVE, SEQ PLAY = PLAY, SEQ REC = REC; K1..K4 = KNOB 1..4, K5 = MASTER, K6..K8 =
SELECT, ALGORITHM, PRESETS), the keybed's F3..G5 as the 27 keys and the pads as the white keys; `plat.h` has the rest
(`plat_smk_*`, `plat_pad_*`, `plat_fader`, `plat_wheels`, `plat_keybed`) behind `BOARD_SMK37`. The store's objects
go to the SMK-37's own window (the app's `APP_STORE_MAP` is the FM-1's), the data area (`plat_data`) is
0xC2000..0xE9FFF, and nothing ever writes its calibration, Bluetooth pairing or key areas. The identity is the model's
name and the firmware's digits (`smk_id_prefix` in fm1.toml when a release's would be longer than 20 characters).

## Development over USB

`tools/fm1_remote.py` drives an FM-1 running any fm1-base firmware as a player would: buttons, keys, encoders,
MASTER, and reads back what the screen says (the screen reader's channel), the last crash and the radio's log; with
WiFi, it turns it on and off, joins networks and reads its state (`wifi ...`).
For example, `tools/fm1_remote.py listen tap REC wait 3000 screen log`.

The site's installer page (`tools/make_site.py`, from `web/installer.html`) reads the same crash record and log in
Chrome or Edge: "Report a problem" shows them, ready to copy, so a player can send them without Python. The record
survives a restart, not switching off.
