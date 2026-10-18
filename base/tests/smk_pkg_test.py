#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 M. Geisler (genkigenki/makkuro)
"""An M-Vave SMK-37 package (tools/build.py --board smk37, or fm1pkg_make.py --board smk37): flash.bin is the stock
PRCT size (0x9D000), its app area carries the stock region table (VM, PRCT, BTIF, USRTRIM, USRFLASH, USR) exactly, so
a later stock install sees the same regions; the app fits its slot; the identity is the model's.
  smk_pkg_test.py PACKAGE.fwsc [IDENTITY]"""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import fm1_install as I  # noqa: E402
import fm1pkg_make as M  # noqa: E402

failed = 0


def ok(cond, what):
    global failed
    print(f"smk pkg: {what:<76} {'ok' if cond else 'FAIL'}")
    failed += not cond


def dec(b):
    out = bytearray(b)
    M.enc(out, 0, len(out))
    return bytes(out)


raw = Path(sys.argv[1]).read_bytes()
want = sys.argv[2] if len(sys.argv) > 2 else None
image = I.logical_image(raw)
product = I.product_of(raw)
ok(want is None or product == want, f"identity {product}" + (f" (want {want})" if want else ""))
ok(product.split("_")[0] in ("SMK-37 Pro", "SMK-37 Elite", "MKE-P37") and len(product) <= 20,
   "an SMK-37 model's identity, at most 20 characters")
hdr = dec(image[:0x40])
ents = [dec(image[0x40 + i * 0x50:0x90 + i * 0x50]) for i in range(struct.unpack_from("<H", hdr, 8)[0])]
files = {struct.unpack_from("<H", e, 0)[0]: struct.unpack_from("<II", e, 8) for e in ents}
off, size = files[0]
ok(size == 0x9D000, f"flash.bin {size:#x} B (the stock PRCT, 0x9D000)")
ok(M.crc16(image[off:off + size]) == struct.unpack_from("<H", [e for e in ents if e[0] == 0 and e[1] == 0][0], 4)[0],
   "flash.bin's CRC")
flash = bytearray(image[off:off + size])
area = bytearray(flash[0x4000:0x4000 + 0x400])
M.sfc(area, 0, len(area), 0, M.KEY)
got = []
for k in range(1, 12):
    e = bytes(area[k * 32:(k + 1) * 32])
    if M.crc16(e[2:]) != struct.unpack_from("<H", e)[0]:
        break
    dcrc, eoff, esize, flags, resvd, index = struct.unpack_from("<HIIBBH", e, 2)
    got.append((eoff, esize, flags, resvd, index, e[16:32]))
stock = [(0x9D000, 0x23000, 0x12, 0x81, 0, bytes.fromhex("564d00ffffffffffffffffff00800000")),
         (0, 0x9D000, 0x92, 0x82, 0, bytes.fromhex("5052435400ffffffffffffff434f4445")),
         (0xC0000, 0x1000, 0x92, 0x82, 0, bytes.fromhex("4254494600ffffffffffffff4155544f")),
         (0xC1000, 0x1000, 0x92, 0x81, 0, bytes.fromhex("5553525452494d00ffffffffffffffff")),
         (0xC2000, 0x29000, 0x92, 0x81, 0, bytes.fromhex("555352464c41534800ffffffffffffff")),
         (0xF4000, 0xA000, 0x92, 0x81, 1, b"USR\0" + b"\xff" * 12)]
ok([g[5][:7] for g in got[:2]] == [b"app.bin", b"cfg_too"] and got[0][1] == 0x97400,
   "app.bin (the stock slot, 0x97400) and cfg_tool.bin first")
ok(got[2:] == stock, "the region table == the stock one (VM, PRCT, BTIF, USRTRIM, USRFLASH, USR)")
ok(flash[:0x4000] != b"\xff" * 0x4000, "the head is there (the loader never writes it; checked against the device's)")
ok(I.LOADER_MARK in raw, "the update loader is Felucca's (its marker)")
print("SMK PACKAGE TEST FAILED" if failed else "smk package test passed")
sys.exit(1 if failed else 0)
