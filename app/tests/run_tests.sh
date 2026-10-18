#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Erik37: Copyright (C) 2026 M. Geisler (genkigenki/makkuro)
# Erik37's tests (Linux / WSL; the toolchain and the SDK as for ./build.sh): the engine on the host, the firmware's
# CPU budget, its flash, the update loader over the whole app area, the packages, the simulator (the controls, the
# sound) and the screens against their golden images.
#   app/tests/run_tests.sh        (after ./build.sh: the packages and the host simulator are built)
set -u
cd "$(dirname "$0")/../.."
CC="${CC:-cc}"
PY="${PYTHON:-python3}"
OUT=app/build/tests
B=app/build/smk37
FAIL=0
mkdir -p "$OUT"
run() {
    name="$1"; shift
    if "$@" > "$OUT/$name.log" 2>&1; then
        echo "  ok   $name"
    else
        echo "  FAIL $name (see $OUT/$name.log)"
        tail -15 "$OUT/$name.log" | sed 's/^/       /'
        FAIL=1
    fi
}
run engine sh -c "$CC -O2 -Wall -o $OUT/engine_test app/tests/engine_test.c app/dsp/erk.c -lm && $OUT/engine_test app/assets/erik37.bank $OUT"
run cpu-budget "$PY" app/tests/target_budget.py $B/erik37.dis --report $OUT/cpu_budget.json
run flash "$PY" app/tests/flash_test.py $B
# the update loader (felucca-bsp's, the SMK-37 build) over the whole app area: a package as large as the slot, the
# loader must write it up to 0x9CFFF (the FM-1's 0x93000 stopped a port's install part-way)
run loader-range sh -c "$PY -c \"import os; open('$OUT/full.bin','wb').write(open('$B/erik37.bin','rb').read() + os.urandom(0x97400 - os.path.getsize('$B/erik37.bin')))\" && \
    head -c 60000 $B/erik37.bin > $OUT/old.bin && \
    $PY base/tools/fm1pkg_make.py --board smk37 --product 'SMK-37 Pro_480100' $OUT/full.bin $B/loader/ota.bin $OUT/full.fwsc >/dev/null && \
    $PY base/tools/fm1pkg_make.py --board smk37 --product 'SMK-37 Pro_480100' $OUT/old.bin $B/loader/ota.bin $OUT/old.fwsc >/dev/null && \
    $CC -w -DBOARD_SMK37=1 -o $OUT/ldr_test base/tests/ldr_test.c && $OUT/ldr_test $OUT/old.fwsc $OUT/full.fwsc"
for m in pro elite mke; do
    case $m in pro) d=smk37; id="SMK-37 Pro_480100";; elite) d=smk37-elite; id="SMK-37 Elite_480100";; mke) d=smk37-mke; id="MKE-P37_480100";; esac
    f=$(ls app/build/$d/*.fwsc 2>/dev/null | head -1)
    if [ -n "$f" ]; then run package-$m "$PY" base/tests/smk_pkg_test.py "$f" "$id"; else echo "  skip package-$m (not built)"; fi
done
run smk-flash-map sh -c "$CC -O2 -w -DBOARD_SMK37=1 -Ibase/bsp/chip -Ibase/bsp/boards/smk37 -Ibase/include -o $OUT/smk_flash_test base/tests/smk_flash_test.c && $OUT/smk_flash_test"
mkdir -p "$OUT/play" "$OUT/screens"
run simulator $B/host/erik37-smk37_host app/tests/play.txt "$OUT/play"
run screens-run $B/host/erik37-smk37_host app/tests/screens.txt "$OUT/screens"
run screens "$PY" app/tests/compare_shots.py "$OUT/screens"
[ $FAIL -eq 0 ] && echo "all tests passed" || echo "TESTS FAILED"
exit $FAIL
