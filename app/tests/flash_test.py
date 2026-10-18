#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Erik37: Copyright (C) 2026 M. Geisler (genkigenki/makkuro)
"""The flash: what the firmware may touch and what it fills (the SMK-37 build, base/bsp/boards/smk37/fm1_flash_map.h).
  flash_test.py BUILD_DIR      (app/build/smk37)
  - the image fits the SMK-37's app slot (0x97400 B), the update loader's app area ends at 0x9D000 (its listing
    carries that limit; the FM-1's 0x93000 would stop an install part-way)
  - the app never uses the big data area (plat_data: the stock USRFLASH 0xC2000..0xE9FFF): its store objects only,
    which fm1-base keeps in the SMK-37's own window 0x9D000..0xAEFFF
  - the protected ranges stay protected in the flash map the build used: 0x0-0x3FFF, 0xC0000-0xC1FFF,
    0xF4000-0xFDFFF, 0xFF000-"""
import re
import sys
from pathlib import Path

B = Path(sys.argv[1])
ROOT = Path(__file__).resolve().parents[2]
bad = 0


def ok(c, what):
    global bad
    print(f"  {'ok  ' if c else 'FAIL'} {what}")
    bad += not c


img = (B / "erik37.bin").stat().st_size
ok(img <= 0x97400, f"the image ({img} B) fits the SMK-37 app slot (0x97400 = {0x97400} B), {0x97400 - img} B spare")
ldr = (B / "loader" / "loader.dis").read_text()
ok(re.search(r"\b0x9[dD]000\b|\b643072\b", ldr) is not None, "the update loader's listing carries the app end 0x9D000")
ok(re.search(r"\b0x93000\b|\b602112\b", ldr) is None, "... and not the FM-1's 0x93000")
dis = (B / "erik37.dis").read_text()
calls = [m for m in ("plat_data_erase", "plat_data_write") if re.search(r"call .*<" + m + r"\b", dis)]
ok(not calls, "no write into the big data area (USRFLASH 0xC2000..0xEAFFF): " + (", ".join(calls) or "none"))
fm = (ROOT / "base" / "bsp" / "boards" / "smk37" / "fm1_flash_map.h").read_text()
want = {"FL_HEAD_HI": 0x4000, "FL_CAL_LO": 0xC0000, "FL_CAL_HI": 0xC2000, "FL_USRRES_LO": 0xF4000,
        "FL_USRRES_HI": 0xFE000, "FL_KEYMAC_LO": 0xFF000, "FL_APP_HI": 0x9D000, "FL_DATA_LO": 0x9D000,
        "FL_DATA_HI": 0xB0000}
for k, v in want.items():
    m = re.search(rf"#define {k}\s+0x([0-9A-Fa-f]+)u", fm)
    ok(m is not None and int(m.group(1), 16) == v, f"flash map {k} = {v:#x}")
ok("FL_PROTECTED" in fm and "FL_GUARD" in fm, "the RAM driver's second net (FL_GUARD / FL_PROTECTED) is in the map")
sys.exit(1 if bad else 0)
