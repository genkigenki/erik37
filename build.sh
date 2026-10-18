#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Erik37: Copyright (C) 2026 M. Geisler (genkigenki/makkuro)
# Build Erik37 for the M-Vave SMK-37 (Pro, Elite, MKE-P37) and the FM-1 with fm1-base (base/, a git submodule), the
# host simulators, and the packages into build/erik37/ with their SHA256SUMS. Linux or WSL.
#   ./build.sh
#   JIELI_TOOLCHAIN  JieLi Linux toolchain (default: ~/.jieli/toolchain)
#   AC79_SDK         JieLi AC79 SDK checkout (default: ~/fw-AC79_AIoT_SDK)
#   PYTHON           a Python 3 with Pillow (and libraqm) and NumPy
set -e
cd "$(dirname "$0")"
export JIELI_TOOLCHAIN="${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}"
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
PY="${PYTHON:-python3}"
[ -f base/tools/build.py ] || { echo "build.sh: no fm1-base in base/ (git submodule update --init --recursive)"; exit 1; }
for m in pro elite mke; do
    BOARD=smk37 MODEL=$m "$PY" base/tools/build.py app "$@"
done
BOARD=fm1 "$PY" base/tools/build.py app "$@"
BOARD=smk37 "$PY" base/tools/build.py app --host
BOARD=fm1 "$PY" base/tools/build.py app --host
mkdir -p build/erik37
rm -f build/erik37/*.fwsc
cp app/build/smk37/*.fwsc app/build/smk37-elite/*.fwsc app/build/smk37-mke/*.fwsc build/erik37/
for f in app/build/erik37*.fwsc; do cp "$f" "build/erik37/$(basename "$f" | sed 's/^erik37/erik37-fm1/')"; done
(cd build/erik37 && sha256sum *.fwsc > SHA256SUMS && cat SHA256SUMS)
