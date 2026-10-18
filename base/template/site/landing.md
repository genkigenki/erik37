# TEMPLATE

TEMPLATE is the smallest complete firmware for the M-VAVE FM-1, made to be copied: every control on the screen by
its name, a simple voice on the keys with velocity and pressure, settings that save, MIDI over USB, Bluetooth and
TRS, and a screen reader. Copy it to
start your own firmware on fm1-base.

![TEMPLATE while a key is held: C4 in the header with V 90 P 64, FX lit among the buttons, K3 at 3, the key filled](manual/img/screen-playing.png "TEMPLATE on the FM-1's screen")

Version {{VERSION}}. Installing is at your own risk; the installer can put M-VAVE's own firmware back.

- [Try it in the browser](emu/): the same code the FM-1 runs, with sound. No FM-1 needed.
- [Install](install/): from Chrome or Edge, with the FM-1 connected by USB.
- [Manual](manual/): the screen, the controls, saving, MIDI.
- [Download {{PKG}}](firmware/{{PKG}}): for the command-line installer, `python3 tools/fm1_install.py {{PKG}}`.
- [Source](https://github.com/charlesvestal/fm1-base): MPL-2.0. Built on fm1-base and felucca-bsp, from Felucca by
  Leo Kuroshita, Hügelton Instruments.

M-VAVE and FM-1 are trademarks of their owners. TEMPLATE is not affiliated with M-VAVE.
