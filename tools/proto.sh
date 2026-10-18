#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Erik37: Copyright (C) 2026 M. Geisler (genkigenki/makkuro)
# The phase 1 prototype: the host tools, the candidate sound banks, the demos (demos/, LISTEN.md). Needs the sources
# in samples-src/ (SOURCES.md), a C compiler, Python 3 with numpy and scipy. The CPU budget of the prototype runs on
# Linux / WSL: tools/cpu_budget.py.
set -e
cd "$(dirname "$0")/.."
EXE=""
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) EXE=.exe ;; esac
mkdir -p build/banks
cc -O2 -Wall -o build/erkcodec$EXE tools/erkcodec.c
cc -O2 -Wall -DERK_MODAL=1 -o build/render$EXE host/render.c app/dsp/erk.c app/dsp/erk_modal.c
P="python3 -I"
command -v python3 >/dev/null 2>&1 || P="python -I"
$P tools/build_bank.py salamander build/banks/salamander_450.bin --zones 18 --layers 3 --budget 450 --report build/banks/salamander_450.json &
$P tools/build_bank.py salamander build/banks/salamander_300.bin --zones 16 --layers 2 --budget 300 --report build/banks/salamander_300.json &
$P tools/build_bank.py vcsl build/banks/vcsl_450.bin --zones 18 --layers 3 --budget 450 --report build/banks/vcsl_450.json &
$P tools/build_bank.py iowa build/banks/iowa_450.bin --zones 18 --layers 2 --budget 450 --report build/banks/iowa_450.json &
wait
$P tools/modal_fit.py measure build/banks/salamander_450.json build/banks/modal_salamander.bin
$P tools/modal_fit.py synth build/banks/modal_synth.bin
$P tools/demos.py
# the firmware's bank (app/assets/erik37.bank: Salamander, 18 zones x 3 layers, about 400 KB)
$P tools/build_bank.py salamander app/assets/erik37.bank --zones 18 --layers 3 --budget 400 --report build/banks/erik37_bank.json
