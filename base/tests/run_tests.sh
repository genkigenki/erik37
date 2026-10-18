#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Charles Vestal
# fm1-base's tests: the platform's pieces on the host (encoder, TRS jack, storage, fastmath), the installer against a
# simulated FM-1, the update path against the template's package (tools/build.py template first), and the template in
# the simulator (tests/basic.txt).
#   tests/run_tests.sh
set -u
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
PY="${PYTHON:-python3}"
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"   # the package's SDK files (tools/fm1pkg_make.py)
OUT=template/build/tests
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
run encoder sh -c "$CC -O2 -w -Ibsp/chip -Ibsp/boards/fm1 -o $OUT/encoder_test tests/encoder_test.c && $OUT/encoder_test"
run trs sh -c "$CC -O2 -w -Ibsp/chip -Ibsp/boards/fm1 -Idevice -o $OUT/trs_test tests/trs_test.c && $OUT/trs_test"
run storage sh -c "$CC -O2 -w -o $OUT/storage_test tests/storage_test.c && $OUT/storage_test"
run wifi-scan sh -c "$CC -O2 -w -Itests/wifi_stub -o $OUT/wifi_scan_test tests/wifi_scan_test.c && $OUT/wifi_scan_test"
run fastmath sh -c "$CC -O2 -w -o $OUT/fastmath_test tests/fastmath_test.c -lm && $OUT/fastmath_test"
run installer "$PY" tests/install_test.py
PKG=template/build/template.fwsc
if [ -f "$PKG" ]; then
    run ota-entry sh -c "$CC -w -o $OUT/ota_test tests/ota_test.c && $OUT/ota_test $PKG"
    run update-loader sh -c "head -c 60000 template/build/template.bin > $OUT/old_app.bin && \
        $PY tools/fm1pkg_make.py $OUT/old_app.bin template/build/loader/ota.bin $OUT/old.fwsc >/dev/null && \
        $CC -w -o $OUT/ldr_test tests/ldr_test.c && $OUT/ldr_test $OUT/old.fwsc $PKG"
else
    echo "  skip update path (no $PKG: tools/build.py template)"
fi
# the M-Vave SMK-37 (BOARD_SMK37): the flash map, the store owner and the stock mapping, the package layout, the
# update entry and the loader per model, on SMK-layout packages (the template's SMK build if there,
# tools/build.py template --board smk37; else made here from the FM-1 binaries: only the layout and the protocol
# are under test), and the template in the SMK simulator
SMK_INC="-Ibsp/chip -Ibsp/boards/smk37 -Iinclude"
run smk-flash sh -c "$CC -O2 -w -DBOARD_SMK37=1 $SMK_INC -o $OUT/smk_flash_test tests/smk_flash_test.c && $OUT/smk_flash_test"
run smk-owner sh -c "$CC -O2 -w -DBOARD_SMK37=1 $SMK_INC -o $OUT/smk_owner_test tests/smk_owner_test.c && $OUT/smk_owner_test"
if [ -f "$PKG" ]; then
    SMKPKG=template/build/smk37/template-smk37.fwsc
    if [ ! -f "$SMKPKG" ]; then
        SMKPKG=$OUT/smk_new.fwsc
        $PY tools/fm1pkg_make.py --board smk37 --product "SMK-37 Pro_100" template/build/template.bin template/build/loader/ota.bin "$SMKPKG" >/dev/null
    fi
    $PY tools/fm1pkg_make.py --board smk37 --product "SMK-37 Pro_100" "$OUT/old_app.bin" template/build/loader/ota.bin "$OUT/smk_old.fwsc" >/dev/null
    run smk-package "$PY" tests/smk_pkg_test.py "$SMKPKG"
    run smk-ota-entry sh -c "$CC -w -DBOARD_SMK37=1 -o $OUT/ota_test_smk tests/ota_test.c && $OUT/ota_test_smk $SMKPKG $PKG"
    run smk-ota-entry-elite sh -c "$CC -w -DBOARD_SMK37=1 -DSMK_MODEL='\"SMK-37 Elite\"' -o $OUT/ota_test_smk_e tests/ota_test.c && $OUT/ota_test_smk_e $SMKPKG"
    run smk-ota-entry-mke sh -c "$CC -w -DBOARD_SMK37=1 -DSMK_MODEL='\"MKE-P37\"' -o $OUT/ota_test_smk_m tests/ota_test.c && $OUT/ota_test_smk_m $SMKPKG"
    run smk-update-loader sh -c "$CC -w -DBOARD_SMK37=1 -o $OUT/ldr_test_smk tests/ldr_test.c && $OUT/ldr_test_smk $OUT/smk_old.fwsc $SMKPKG $PKG"
    for m in elite mke; do                 # the other models' packages, when built (tools/build.py template --board smk37 --model ...)
        f=template/build/smk37-$m/template-smk37-$m.fwsc
        [ -f "$f" ] && run smk-package-$m "$PY" tests/smk_pkg_test.py "$f"
    done
fi
run smk-host-build "$PY" tools/build.py template --host --board smk37
run smk-template template/build/smk37/host/template-smk37_host template/tests/basic.txt template/build/smk37/host
run host-build "$PY" tools/build.py template --host
run template template/build/host/template_host template/tests/basic.txt template/build/host
[ $FAIL -eq 0 ] && echo "all tests passed" || echo "TESTS FAILED"
exit $FAIL
