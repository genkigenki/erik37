#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Charles Vestal
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Build an fm1-base firmware: the app, the update loader (felucca-bsp's) and an installable .fwsc package.

  tools/build.py APP_DIR [--release X.Y[.Z][-suffix]] [--no-ble] [--board fm1|smk37] [--model pro|elite|mke]
  tools/build.py APP_DIR --host        the host simulator instead: APP_DIR/build/host/NAME_host (cc, no toolchain)
  tools/build.py APP_DIR --web         the browser emulator instead: APP_DIR/build/emu/ (emcc); the page is the
                                       simulator alone (panel, screen, sound, screen reader), for a site to link or frame;
                                       an app may bring its own (APP_DIR/web/emu/index.html, worklet.js)

APP_DIR holds the app (app.h, app.c, its sources) and its fm1.toml:
  name = "TEMPLATE"          the build's file names (lower case)
  id_prefix = "FM-1_0"       package identity: dev builds are the prefix + "00", releases the prefix + XXYYZZ
  usb_manufacturer = "..."   the USB names (felucca-bsp FELUCCA_USB_MANUFACTURER / FELUCCA_USB_PRODUCT)
  usb_product = "..."
  o2 = ["dsp/engine.c"]      separate units compiled at -O2 (the app's DSP), relative to APP_DIR (globs too); each with
                             its own directory on the include path
  o2_cflags = ["-DX=1"]      more flags for those; o2_include = ["lib/prefix.h"]: a header included first in each
  include = ["dsp/lib"]      more include directories, relative to APP_DIR
  device_include = ["inc"]   include directories for the firmware only (headers the toolchain lacks)
  mmio_skip = ["dsp/lib/"]   paths whose numbers are not addresses (a library's tables): the register check skips
                             its address literals there (still checks casts, asm and register macros)
  rom_check_skip = ["pv_log2"]   functions whose constants only look like ROM addresses (0xFFC00000..): the check
                             that nothing calls or loads the chip ROM skips them
  mul64_check_skip = ["mul_f256"]   functions the note on inlined 64-bit multiplies beside loads leaves out
                             (felucca-bsp docs/CPU.md; the compiler runtime's __* are left out anyway)
  vendored = ["third_party/"]   other people's libraries, compiled for the FM-1 with their machine-specific code off
                             by flags: the register check skips them entirely (it reads the text: x86 asm behind
                             an #if would fail it)
  words = "words.tsv"        the screen reader's words (fm1-reader), relative to APP_DIR
  ble_central = true         and the FM-1 connects to Bluetooth MIDI devices too (FM1B_BLE_CENTRAL), beside a computer
  usb_audio_in = true        the FM-1 recordable over USB (FM1B_UAC; the default: true)
  usb_audio_out = true       a computer plays through the FM-1 over USB (FM1B_UAC_OUT; the default: true)
Optional parts: only what an fm1.toml asks for is built in (Bluetooth, USB audio; an app's lib/touch by APP_TOUCH in
app.h); the build prints what it has.
  ble = true                 Bluetooth MIDI (FM1B_BLE): ble/ on JieLi's BT libraries, from the AC79 SDK ($AC79_SDK);
                             --no-ble builds without it (NAME-usb.fwsc), and the build checks nothing of JieLi's is in
  wifi = true                WiFi (FM1B_WIFI): a station on JieLi's WiFi libraries and the SDK's lwIP (built from its
                             sources), on the same shim (ble/shim, ble/wifi) with fm1-radio's RAM split (device/
                             app_wifi.ld). Not with ble (yet)
  net = ["net/stream.c"]     with wifi: the app's network code (lwIP's sockets), compiled as the SDK builds code that
                             talks lwIP (its flags and headers), relative to APP_DIR (globs too: "tls/src/**/*.c"); it
                             calls the platform through plat.h, runs in a task of its own (plat_net_task)
  net_include = ["tls/inc"]  more include directories for those, relative to APP_DIR
  net_cflags = ["-DX=0"]     more flags for those
  gen = ["tools/places.py --out {gen}/places.h"]   the app's generators, run before the build (relative to APP_DIR;
                             {gen} is the generated headers' directory, on the include path)
Outputs in APP_DIR/build/: NAME.bin (app), loader/ota.bin (update loader), NAME.fwsc (package).

BOARD=smk37 (or --board smk37) builds the same firmware for the M-Vave SMK-37 (the same AC791N, SPL and update
loader; another panel, screen wiring and flash map): -DBOARD_SMK37=1 for the app and the loader, felucca-bsp's
boards/smk37 in place of boards/fm1, the SMK-37's app slot and package layout (fm1pkg_make.py), the identity
"SMK-37 Pro_" + the firmware's digits (fm1.toml smk_id_prefix, default id_prefix's: dev + "00", releases +
XXYYZZ), the USB product name with "SMK-37" for "FM-1", and the files in APP_DIR/build/smk37/: NAME-smk37.fwsc
(NAME-smk37-X.Y.fwsc for a release). MODEL=elite / mke (or --model) builds it under the SMK-37 Elite's or the
MKE-P37's name (identity, update loader, update record; -DSMK_MODEL): build/smk37-elite/NAME-smk37-elite.fwsc, ...
The default stays the FM-1, and its build is unchanged.
  smk_id_prefix = "3"        the digits of the SMK-37 identity (an identity is at most 20 characters: "SMK-37 Elite_"
                             leaves 7 for a release's digits + XXYYZZ)

The JieLi toolchain is Linux x86-64 only. JIELI_TOOLCHAIN points at it; on
macOS (or with JIELI_DOCKER=1) each tool runs in a linux/amd64 container.
"""
import argparse
import hashlib
import os
import platform
import re
import struct
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]        # fm1-base
BSP = SRC / "bsp"                                 # felucca-bsp (git submodule)
APP = OUT = GEN = LDR = ROOT = None               # set by main() from APP_DIR
CONF = {}
sys.path.insert(0, str(SRC / "tools"))
import fm1pkg_make  # noqa: E402
import lz4blk  # noqa: E402

APP_XIP = 0x02000120                # app.bin offset 0 in the XIP map; the SPL jumps here
APP_SLOT = fm1pkg_make.APP_SLOT
LOADER_LOAD = 0x01C0A800
LOADER_NAME = b"usb_hid_ota.bin"    # the file name the SPL looks for
DOCKER_IMAGE = os.environ.get("JIELI_DOCKER_IMAGE", "debian:bookworm-slim")
BOARD = "fm1"                       # fm1 (default) or smk37: BOARD env or --board
MODEL = "pro"                       # smk37: pro (default), elite or mke: MODEL env or --model
MODELS = {"pro": "SMK-37 Pro", "elite": "SMK-37 Elite", "mke": "MKE-P37"}   # one keyboard, three names
BOARD_DEFS = []                     # the board's flags for the app and the loader (none for the FM-1: unchanged)
CFLAGS = ["-Os", "-ffunction-sections", "-fno-builtin", "-Wall", "-Wno-unused-function"]
# the AC79's single-precision FPU (the SDK's own flags); no fused multiply-add, so the host
# build (-ffp-contract=off) computes the same samples
FPU = ["-mcpu=r3", "-mfprev1", "-ffp-contract=off"]
LINE = re.compile(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*\t(.*)$")

# SDK files of AC79NN_SDK_V1.2.1_2023-12-13 (the tested version)
SDK_SHA256 = {
    "uboot.boot": "4e3b4c220dc96641cb5a723f41e68ce41d5261ae9434bb33fbd7f2c59976ded4",
    "cfg_tool.bin": "276579954f076886a6a7694f65dc71c034a63a2c204b76749065c0ac7b010d1b",
    "cfg/eq_cfg_hw.bin": "41167491bffed4651750719c973d2758adeb9021a5670d02d6a53c85ed80ea7d",
}

PRODUCT = None                      # package identity (fm1.toml id_prefix; see main)
VERSION = None                      # APP_VERSION for release builds (default: "DEV", device/unity.c)
BLE = False                         # fm1.toml ble = true, unless --no-ble
WIFI = False                        # fm1.toml wifi = true
STEM = None                         # the build's file names: NAME, or NAME-usb (--no-ble)
BT_SYMS = ("btstack_init", "bt_ble_init", "bt_max_pwr_set", "le_controller_set_mac", "lmp_set_sniff_disable",
           "shim_os_init", "ble_midi_send_many")


def read_conf(path):
    """fm1.toml: key = "string" | number | true | false | ["string", ...] (a list may go on over several lines),
    # comments (the little TOML a firmware needs)"""
    conf = {}
    pending, start = "", 0
    for no, line in enumerate(path.read_text().splitlines(), 1):
        line = re.sub(r"\s+#.*$", "", line).strip()
        if not line or line.startswith("#"):
            continue
        if pending:                                  # a list's next line
            line, no = pending + " " + line, start
        if re.match(r"[A-Za-z_][A-Za-z0-9_]*\s*=\s*\[", line) and "]" not in line:
            pending, start = line, no
            continue
        pending = ""
        m = re.fullmatch(r"([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.+)", line)
        if not m:
            raise SystemExit(f"{path}:{no}: not key = value")
        v = m.group(2)
        if v.startswith("["):
            conf[m.group(1)] = re.findall(r'"([^"]*)"', v)
        elif v.startswith('"'):
            conf[m.group(1)] = v.strip('"')
        elif v in ("true", "false"):
            conf[m.group(1)] = v == "true"
        else:
            conf[m.group(1)] = int(v, 0)
    if pending:
        raise SystemExit(f"{path}:{start}: a list without its ]")
    return conf


def toolchain():
    tc = os.environ.get("JIELI_TOOLCHAIN")
    if not tc or not (Path(tc) / "pi32v2" / "bin" / "clang").exists():
        raise SystemExit("JIELI_TOOLCHAIN must point at the JieLi Linux toolchain "
                         "(the directory with pi32v2/ and common/; see tools/get_toolchain.sh)")
    return Path(tc).resolve()


def use_docker():
    native = platform.system() == "Linux" and platform.machine() in ("x86_64", "AMD64")
    return os.environ.get("JIELI_DOCKER", "0" if native else "1") == "1"


_BUILDER = None
_BUILDER_LOCK = __import__("threading").Lock()


def builder():
    """one long-lived toolchain container per source tree (starting a container per tool call cost
    most of the build); it stops by itself after an hour without a build"""
    global _BUILDER
    with _BUILDER_LOCK:            # the loader and the app build in parallel: start it once
        return _builder_start()


def _builder_start():
    global _BUILDER
    if _BUILDER:
        return _BUILDER
    name = "jieli-" + hashlib.sha1(str(ROOT).encode()).hexdigest()[:10]
    st = subprocess.run(["docker", "inspect", "-f", "{{.State.Running}}", name], capture_output=True, text=True)
    if st.stdout.strip() != "true":
        subprocess.run(["docker", "rm", "-f", name], capture_output=True)
        r = subprocess.run(["docker", "run", "-d", "--rm", "--name", name, "--platform", "linux/amd64",
                            "-v", f"{ROOT}:/work", "-v", f"{toolchain()}:/opt/jieli:ro", "-w", "/work",
                            DOCKER_IMAGE, "sh", "-c", "sleep 3600"], capture_output=True, text=True)
        if r.returncode:
            sys.stderr.write(r.stderr)
            raise SystemExit("build: cannot start the toolchain container")
    else:                           # an hour from now again
        subprocess.run(["docker", "exec", "-d", name, "sh", "-c", "sleep 3600 && kill 1"], capture_output=True)
    _BUILDER = name
    return name


def tc(tool, *args):
    """run a toolchain binary (pi32v2/bin/..., common/bin/...) with cwd ROOT; paths relative to ROOT"""
    rel = [str(Path(a).resolve().relative_to(ROOT)) if isinstance(a, Path) else a for a in args]
    if tool == "cc":                # the toolchain's cc wrapper needs python3; call clang directly
        tool, rel = "pi32v2/bin/clang", ["-target", "pi32v2", *rel]
    if use_docker():
        cmd = ["docker", "exec", "-w", "/work", builder(), f"/opt/jieli/{tool}", *rel]
    else:
        cmd = [str(toolchain() / tool), *rel]
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f"build: {tool} failed")
    if r.stderr.strip():
        sys.stderr.write(r.stderr)
    return r.stdout


def tc_all(*cmds):
    # at most 4 at once: a podman machine drops concurrent container connections (ssh EOF)
    with ThreadPoolExecutor(min(len(cmds), int(os.environ.get("FM1B_JOBS", "4")))) as ex:
        return list(ex.map(lambda c: tc(*c), cmds))


def generate():
    """generated headers: the fonts, the screen reader's words link (fm1-reader)"""
    GEN.mkdir(parents=True, exist_ok=True)
    tools = SRC / "tools"
    for c in [[tools / "gen_font.py", GEN / "felucca_font.h"],
              [tools / "gen_words.py", APP / CONF.get("words", "words.tsv"), APP / "web" / "words", GEN / "ann_words.h"]]:
        r = subprocess.run([sys.executable, *map(str, c)], capture_output=True, text=True)
        sys.stdout.write(r.stdout)
        if r.returncode:
            sys.stderr.write(r.stderr)
            raise SystemExit(f"build: {c[0].name} failed")
    for line in CONF.get("gen", []):          # the app's own generators: a script (relative to APP_DIR) and its arguments
        c = [a.replace("{gen}", str(GEN)) for a in line.split()]
        r = subprocess.run([sys.executable, str(APP / c[0]), *c[1:]], cwd=APP, capture_output=True, text=True)
        sys.stdout.write(r.stdout)
        if r.returncode:
            sys.stderr.write(r.stderr)
            raise SystemExit(f"build: {c[0]} failed")


# ---- update loader

def crc16(d, c=0):
    for b in d:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) if c & 0x8000 else c << 1
        c &= 0xFFFF
    return c


def ldr_head(dcrc, a, b, attr, name):
    body = struct.pack("<HIIBBH16s", dcrc, a, b, attr, 0, 0, name)
    return struct.pack("<H", crc16(body)) + body


def ldr_wrap(image):
    """ota.bin = outer JLFS head (32 B) + inner head (32 B, load address) +
    blocks [clen u32][dlen u32][LZ4 block], dlen 4096 except the last"""
    blocks = bytearray()
    for i in range(0, len(image), 4096):
        raw = image[i:i + 4096]
        c = lz4blk.compress(raw)
        if len(c) > 4096 or lz4blk.decompress(c) != raw:
            raise SystemExit(f"loader: block {i // 4096} does not compress below 4096 B")
        blocks += struct.pack("<II", len(c), len(raw)) + c
    inner = ldr_head(crc16(image), len(image), LOADER_LOAD, 0, LOADER_NAME)
    body = inner + bytes(blocks)
    return ldr_head(crc16(body), 0x20, len(body), 0x41, LOADER_NAME) + body


def build_loader():
    LDR.mkdir(parents=True, exist_ok=True)
    src = BSP / "loader"
    flags = [*CFLAGS, *BOARD_DEFS, *inc(BSP / "chip", board_dir())]
    tc_all(("cc", "-c", src / "crt0_ldr.S", "-o", LDR / "crt0_ldr.o"),
           ("cc", *flags, "-c", src / "loader.c", "-o", LDR / "loader.o"))
    elf = LDR / "loader.elf"
    tc("pi32v2/bin/ld", "--gc-sections", "-e", "_start", "-T", src / "loader.ld",
       LDR / "crt0_ldr.o", LDR / "loader.o", "-o", elf)
    _, dis, hdr, syms = tc_all(("common/bin/objcopy", "-O", "binary", "-j", ".text", elf, LDR / "loader.bin"),
                               ("common/bin/objdump", "-d", elf),
                               ("common/bin/objdump", "-h", elf),
                               ("common/bin/objdump", "-t", elf))
    (LDR / "loader.dis").write_text(dis)
    for ln in hdr.splitlines():     # the loader rewrites the flash: nothing may run from XIP
        p = ln.split()
        if len(p) > 4 and p[1].startswith(".") and int(p[3], 16) >= 0x02000000 and int(p[2], 16):
            raise SystemExit(f"loader: section {p[1]} at {p[3]} is outside RAM")
    image = (LDR / "loader.bin").read_bytes()
    if not image or len(image) > 0x14000:
        raise SystemExit("loader: image empty or too big")
    ota = ldr_wrap(image)
    (LDR / "ota.bin").write_bytes(ota)
    bss = [int(ln.split()[0], 16) for ln in syms.splitlines() if ln.rstrip().endswith("_bss_end")]
    print(f"loader: image {len(image)} B at {LOADER_LOAD:#x}" + (f", bss end {bss[0]:#x}" if bss else ""))
    print(f"loader: ota.bin {len(ota)} B")
    return ota


# ---- app

def inc(*dirs):
    return [f"-I{Path(d).resolve().relative_to(ROOT)}" for d in dirs]


def board_dir():
    """felucca-bsp's board layer: boards/fm1, or boards/smk37 for BOARD=smk37"""
    return BSP / "boards" / ("smk37" if BOARD == "smk37" else "fm1")


# ---- Bluetooth: JieLi's BT libraries (Apache-2.0, binary only; never in git) and a few SDK sources, on the shim
# (ble/shim) and the MIDI service (ble/midi)
BLE_LIBS = ["btctrler.a", "btstack.a", "crypto_toolbox_Osize.a", "lib_ccm_aes.a", "wl_rf_common.a"]
BLE_SDK_SRC = ["apps/common/config/bt_profile_config.c",
               "apps/common/config/log_config/lib_btctrler_config.c",
               "apps/common/config/log_config/lib_btstack_config.c"]
BLE_INC = ["include_lib", "include_lib/driver", "include_lib/driver/device", "include_lib/driver/cpu/wl82",
           "include_lib/system", "include_lib/system/generic", "include_lib/btctrler",
           "include_lib/btctrler/port/wl82", "include_lib/update", "include_lib/btstack/third_party/common",
           "include_lib/btstack/third_party/rcsp", "include_lib/utils", "include_lib/utils/syscfg",
           "include_lib/utils/event", "include_lib/media", "include_lib/media/cpu/wl82", "apps",
           "apps/demo/demo_ble/include", "apps/common", "apps/common/include", "apps/common/config/include",
           "include_lib/btstack", "include_lib/net", "apps/common/ble/include", "include_lib/newlib/include"]
# WiFi: the SDK's no-SDRAM builds (demo_audio) link the _sfc variants: smaller buffers, code from flash (fm1-radio's)
WIFI_LIBS = ["wl_wifi_sta_sfc.a", "wpasupplicant.a", "hsm.a"]
RADIO_LIBS = ["crypto_toolbox_Osize.a", "lib_ccm_aes.a", "wl_rf_common.a"]   # (either radio's)
# lwIP from the SDK's sources (its _sfc project's files and flags), with ble/lwip/lwipopts_sfc.h: a wider TCP window
LWIP_DIRS = ["src/api", "src/core", "src/core/ipv4", "src/netif", "port", "apps/sock_api", "apps/dhcp_srv", "apps/ntp"]
LWIP_SKIP = {"bridgeif.c", "bridgeif_fdb.c", "lowpan6.c", "lowpan6_ble.c", "lowpan6_common.c", "slipif.c", "zepif.c",
             "bt_ethernetif.c", "lte_ethernetif.c", "wired_ethernetif.c", "ext_wireless_ethernetif.c",
             "wifi_raw_ethernetif.c", "ethernetif.c"}
LWIP_CF = ["-D_POSIX_SOURCE", "-D_SYS_SELECT_H", "-DUSE_RTOS", "-DLIWP_USE_SMALL_MEMORY", "-Wno-unused-value"]
LWIP_INC = ["include_lib/net/device", "include_lib/net/device/eth", "include_lib/net/lwip_2_1_3/lwip/app",
            "include_lib/net/lwip_2_1_3/lwip/app/sock_api", "include_lib/net/lwip_2_1_3/lwip/app/dhcp_srv",
            "include_lib/net/lwip_2_1_3/lwip/port/arch", "include_lib/net/lwip_2_1_3/lwip/src/include/lwip",
            "include_lib/system/device", "include_lib/system/os/FreeRTOS/pi32_lto"]
WIFI_INC = ["include_lib/net/wifi", "include_lib/net/lwip_2_1_3/lwip/src/include",
            "include_lib/net/lwip_2_1_3/lwip/src/include/lwip", "include_lib/net/lwip_2_1_3/lwip/src/include/netif",
            "include_lib/net/lwip_2_1_3/lwip/port", "include_lib/net/lwip_2_1_3/lwip/port/arch", "include_lib/net/HSM"]
BLE_CF = ["-integrated-as", "-mcpu=r3", "-mfprev1", "-fno-common", "-Oz", "-flto", "-fallow-pointer-null",
          "-fprefer-gnu-section", "-femulated-tls", "-fms-extensions", "-w", "-DSUPPORT_MS_EXTENSIONS",
          "-DCONFIG_RELEASE_ENABLE", "-DCONFIG_CPU_WL82", "-DCONFIG_FREE_RTOS_ENABLE", "-DCONFIG_THREAD_ENABLE",
          "-D__GCC_PI32V2__", "-DCONFIG_NEW_ECC_ENABLE", "-DCONFIG_SFC_ENABLE", "-DCONFIG_NO_SDRAM_ENABLE",
          "-DCONFIG_BT_ENABLE=1", "-mllvm", "-pi32v2-large-program=true"]


def radio_units():
    """copy what the Bluetooth or WiFi build needs from the SDK into build/ble (the container sees only this tree);
    return the compile commands, the objects and the link's libraries"""
    import shutil
    sdk = Path(os.environ.get("AC79_SDK", Path.home() / "fw-AC79_AIoT_SDK"))
    dst = OUT / "ble" / "sdk"
    if not (dst / ".copied").exists():
        for d in ("include_lib", "apps/common/include", "apps/common/config/include", "apps/common/ble/include",
                  "apps/demo/demo_ble/include"):
            shutil.copytree(sdk / d, dst / d, dirs_exist_ok=True,
                            ignore=lambda dd, names: [n for n in names if (Path(dd) / n).is_file()
                                                      and not n.endswith((".h", ".hpp"))])
        for f in BLE_SDK_SRC:
            (dst / f).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy(sdk / f, dst / f)
        (dst / "lib").mkdir(exist_ok=True)
        for f in BLE_LIBS:
            shutil.copy(sdk / "cpu/wl82/liba" / f, dst / "lib" / f)
        shutil.copy(sdk / "include_lib/newlib/pi32v2-lib/libcompiler_rt.a", dst / "lib")
        (dst / ".copied").write_text("")
    if WIFI and not (dst / ".copied_wifi").exists():
        for d in LWIP_DIRS:                       # lwIP's sources
            for c in (sdk / "lib/net/lwip_2_1_3" / d).glob("*.c"):
                if c.name not in LWIP_SKIP:
                    (dst / "lwip" / d).mkdir(parents=True, exist_ok=True)
                    shutil.copy(c, dst / "lwip" / d / c.name)
        for f in WIFI_LIBS:
            shutil.copy(sdk / "cpu/wl82/liba" / f, dst / "lib" / f)
        (dst / ".copied_wifi").write_text("")
    flags = [*inc(*(dst / d for d in BLE_INC + WIFI_INC * WIFI)), *inc(SRC / "ble" / "shim")]
    if WIFI:
        flags += ["-DSHIM_WIFI=1"]
    o = OUT / "ble" / "o"
    o.mkdir(parents=True, exist_ok=True)
    units = [("cc", "-c", SRC / "ble" / "shim" / "ctx.S", "-o", o / "ctx.o")]
    objs = [o / "ctx.o"]
    # the SDK demo's app_config.h picks its BLE demo with #defines (a -D cannot override them): the peripheral alone
    # (TRANS_DATA_EN), or with ble_central one link as a peripheral and one as a central (TRANS_MULTI_BLE_EN: the
    # controller's link count, the multi-link stack). Written from the SDK's each build, as this one wants it
    cfg = "apps/demo/demo_ble/include/app_config.h"
    text = (sdk / cfg).read_text(errors="ignore")
    if BLE and CONF.get("ble_central"):
        flags.append("-DBLE_MIDI_CENTRAL=1")
        for old, new in (("#define TRANS_DATA_EN                             1",
                          "#define TRANS_DATA_EN                             0"),
                         ("#define TRANS_MULTI_BLE_EN                        0",
                          "#define TRANS_MULTI_BLE_EN                        1"),
                         ("#define TRANS_MULTI_BLE_SLAVE_NUMS                2",
                          "#define TRANS_MULTI_BLE_SLAVE_NUMS                1"),
                         ("#define TRANS_MULTI_BLE_MASTER_NUMS               2",
                          "#define TRANS_MULTI_BLE_MASTER_NUMS               1")):
            if old not in text:
                raise SystemExit(f"build: {cfg} in the SDK has changed: no {old!r}")
            text = text.replace(old, new)
    if (dst / cfg).read_text(errors="ignore") != text:
        (dst / cfg).write_text(text)
        for f in o.glob("*.o"):
            f.unlink()                            # (built for the other configuration)
    ble_only = [*(SRC / "ble" / "midi").glob("*.c"), *[dst / s for s in BLE_SDK_SRC]] if BLE else []
    for f in [*(SRC / "ble" / "shim").glob("*.c"), *ble_only]:
        units.append(("cc", *BLE_CF, *flags, "-c", f, "-o", o / (f.stem + ".o")))
        objs.append(o / (f.stem + ".o"))
    if WIFI:
        lwip_inc = [*inc(SRC / "ble" / "lwip"), *flags, *inc(*(dst / d for d in LWIP_INC))]
        for d in LWIP_DIRS:
            for f in sorted((dst / "lwip" / d).glob("*.c")):
                units.append(("cc", *BLE_CF, *LWIP_CF, *lwip_inc, "-c", f, "-o", o / ("lwip_" + f.stem + ".o")))
                objs.append(o / ("lwip_" + f.stem + ".o"))
        for f in (SRC / "ble" / "wifi").glob("*.c"):
            units.append(("cc", *BLE_CF, *LWIP_CF, *lwip_inc, "-c", f, "-o", o / ("wifi_" + f.stem + ".o")))
            objs.append(o / ("wifi_" + f.stem + ".o"))
        app_inc = inc(APP, SRC / "include", GEN, *(APP / d for d in CONF.get("include", []) + CONF.get("device_include", [])
                                                     + CONF.get("net_include", [])))
        for u in CONF.get("net", []):            # the app's network code: as the SDK builds code that talks lwIP
            for f in (sorted(APP.glob(u)) if any(ch in u for ch in "*?[") else [APP / u]):
                name = "net_" + "_".join(f.relative_to(APP).with_suffix("").parts) + ".o"
                units.append(("cc", *BLE_CF, *LWIP_CF, *CONF.get("net_cflags", []), *lwip_inc, *app_inc, "-c", f,
                              "-o", o / name))
                objs.append(o / name)
    linked = (BLE_LIBS if BLE else RADIO_LIBS) + WIFI_LIBS * WIFI
    libs = ["--start-group", *[dst / "lib" / f for f in linked], dst / "lib" / "libcompiler_rt.a", "--end-group"]
    return units, objs, libs



def build_app():
    flags = [*CFLAGS, *FPU, *BOARD_DEFS, *inc(BSP / "chip", board_dir(), SRC / "device", SRC / "include", SRC / "lib", APP, GEN,
                                 *(APP / d for d in CONF.get("include", []) + CONF.get("device_include", [])))]
    if BLE:
        flags += ["-DFM1B_BLE=1", *inc(SRC / "ble" / "shim")]
        if CONF.get("ble_central"):
            flags.append("-DFM1B_BLE_CENTRAL=1")
    if WIFI:
        flags += ["-DFM1B_WIFI=1", "-DSHIM_WIFI=1", *inc(SRC / "ble" / "shim")]
    for key, flag in (("usb_audio_in", "FM1B_UAC"), ("usb_audio_out", "FM1B_UAC_OUT")):
        if key in CONF and flag not in os.environ:
            flags.append(f"-D{flag}={int(bool(CONF[key]))}")
    for flag in ("FM1B_CDC", "FM1B_UAC", "FM1B_UAC_OUT", "FM1B_TRS"):
        v = os.environ.get(flag)    # unset: fm1.toml's, the app's (app.h) or the platform's default (device/unity.c)
        if v in ("0", "1"):
            flags.append(f"-D{flag}={v}")
    flags.append(f'-DFELUCCA_ID="{PRODUCT}"')
    for key, macro in (("usb_manufacturer", "FELUCCA_USB_MANUFACTURER"), ("usb_product", "FELUCCA_USB_PRODUCT")):
        if CONF.get(key):
            flags.append(f'-D{macro}="{usb_name(key, CONF[key])}"')
    if VERSION:
        flags.append(f'-DAPP_VERSION="{VERSION}"')
    o2 = [f if f != "-Os" else "-O2" for f in flags]
    units = [("cc", "-c", BSP / "chip" / "crt0.S", "-o", OUT / "crt0.o"),
             ("cc", "-c", BSP / "chip" / "fm1_vec.S", "-o", OUT / "fm1_vec.o"),
             ("cc", "-c", BSP / "chip" / "fm1_isr.S", "-o", OUT / "fm1_isr.o"),
             ("cc", *flags, "-c", SRC / "device" / "unity.c", "-o", OUT / "unity.o")]
    objs = [OUT / "crt0.o", OUT / "fm1_vec.o", OUT / "fm1_isr.o", OUT / "unity.o"]
    (OUT / "o2").mkdir(parents=True, exist_ok=True)
    o2_more = [*CONF.get("o2_cflags", []),
               *[a for h in CONF.get("o2_include", []) for a in ("-include", str((APP / h).resolve().relative_to(ROOT)))]]
    for u in CONF.get("o2", []):
        for f in (sorted(APP.glob(u)) if any(ch in u for ch in "*?[") else [APP / u]):
            o = OUT / "o2" / ("_".join(f.relative_to(APP).with_suffix("").parts) + ".o")
            units.append(("cc", *o2, *o2_more, *inc(f.parent), "-c", f, "-o", o))
            objs.append(o)
    b_units, b_objs, b_libs = radio_units() if BLE or WIFI else ([], [], [])
    tc_all(*units, *b_units)
    elf = OUT / (NAME + ".elf")
    # unreferenced functions (each in its own section: -ffunction-sections) are dropped: the DSP keeps
    # mono reference paths for the host tests that the firmware no longer calls. The BT libraries are LTO
    # bitcode: the linker needs the toolchain's LLVM plugin
    lto = ["--plugin", "/opt/jieli/pi32v2/bin/LLVMgold.so" if use_docker() else str(toolchain() / "pi32v2/bin/LLVMgold.so"),
           "--plugin-opt=mcpu=r3", "--plugin-opt=-pi32v2-large-program=true"] if BLE or WIFI else []
    ld = SRC / "device" / ("app_wifi.ld" if WIFI else "app.ld")
    if APP_SLOT != 0x8DFBC:             # another board's app slot (the SMK-37): the same script, its XIP length
        txt = ld.read_text()
        if txt.count("LENGTH = 0x8DFBC") != 1:
            raise SystemExit(f"{ld.name}: the XIP LENGTH line was not found")
        ld = OUT / ld.name
        ld.write_text(txt.replace("LENGTH = 0x8DFBC", f"LENGTH = {APP_SLOT:#X}".replace("0X", "0x")))
    tc("pi32v2/bin/ld", *lto, "--gc-sections", "-e", "_start", "-T", ld, *objs, *b_objs,
       *b_libs, "-o", elf)
    for sect in ("text.bin", "data.bin", "ramtext.bin"):
        (OUT / sect).unlink(missing_ok=True)
    *_, syms, dis, rt = tc_all(("common/bin/objcopy", "-O", "binary", "-j", ".text", elf, OUT / "text.bin"),
                               ("common/bin/objcopy", "-O", "binary", "-j", ".data", elf, OUT / "data.bin"),
                               ("common/bin/objcopy", "-O", "binary", "-j", ".ram_text", elf, OUT / "ramtext.bin"),
                               ("common/bin/objdump", "-t", elf),
                               ("common/bin/objdump", "-d", elf),
                               ("common/bin/objdump", "-d", "-j", ".ram_text", elf))
    (OUT / (NAME + ".dis")).write_text(dis)

    def symv(name):
        return int(re.search(r"^([0-9a-f]+) .*\s" + name + r"$", syms, re.M).group(1), 16)
    img = bytearray((OUT / "text.bin").read_bytes())
    # .ram_text and .data follow .text at their load addresses; crt0 copies them by words
    for sect, lname in (("ramtext.bin", "_rt_load"), ("data.bin", "_data_load")):
        load = symv(lname)
        if load % 4:
            raise SystemExit(f"{lname} {load:#x} is not word aligned")
        blob = (OUT / sect).read_bytes() if (OUT / sect).exists() else b""
        if blob:
            if load - APP_XIP < len(img):
                raise SystemExit(f"{lname} overlaps the image")
            img += b"\xff" * (load - APP_XIP - len(img))
            img += blob
    img += b"\xff" * (-len(img) % 4)
    (OUT / (NAME + ".bin")).write_bytes(img)
    return bytes(img), syms, dis, rt


def mul64_near_loads(dis, window=4):
    """functions with a load in flight beside a 32x32->64-bit multiply (rN_rM = rA * rB): one up to two instructions
    before it, or one after it before its product is used (using it waits for the multiply). On the FM-1 such code
    now and then leaves a register a bit off, most of all beside 16-bit loads (felucca-bsp docs/CPU.md); a
    multiply in a helper of its own (noinline) is clear of it"""
    # vsnprintf (ble/wifi): its 64-bit digits are a divide and a multiply a few times a log line, not a hot loop
    found, fn, body, skip = [], "", [], set(CONF.get("mul64_check_skip", [])) | {"vsnprintf"}

    def scan():
        for i, ins in enumerate(body):
            m = re.match(r"(r\d+)_(r\d+) [+-]?= r\d+ \* r\d+", ins)
            if not m:
                continue
            dst = {m.group(1), m.group(2)}
            before = [b for b in body[max(0, i - 2):i] if not re.search(r"\bcall\b", b)]
            if any(re.search(r"= [hbd]?\[", b) for b in before):
                return True
            for b in body[i + 1:i + 1 + window]:
                if any(re.search(r"\b" + r + r"\b", b) for r in dst):
                    break                       # the product used: the multiply has finished
                if re.search(r"= [hbd]?\[", b):
                    return True
        return False

    for ln in dis.splitlines() + ["end:"]:
        mm = LINE.match(ln)
        if not mm:
            m2 = re.match(r"^([A-Za-z_.$][\w.$]*):$", ln.strip())
            if m2 and not m2.group(1).startswith(".GJT"):   # (a jump table's piece: still the function before it)
                if fn and not fn.startswith("__") and fn.split(".")[0] not in skip and scan() and fn not in found:
                    found.append(fn)
                fn, body = m2.group(1), []
            continue
        body.append(mm.group(3))
    return found


def check(img, syms, dis, rt):
    errors, notes = [], []
    m = re.search(r"^([0-9a-f]+) .*\s_start$", syms, re.M)
    if not m or int(m.group(1), 16) != APP_XIP:
        errors.append(f"_start is not at {APP_XIP:#x}")
    if img[:4] != bytes.fromhex("04818000"):
        errors.append(f"image starts with {img[:4].hex()}, not the entry stub")
    rt_calls = [ln for ln in rt.splitlines() if re.search(r"\bcall\b", ln)]
    if rt_calls and (BLE or WIFI):  # JieLi's .volatile_ram_code calls out; it runs with the flash on
        notes.append(f"the radio: .ram_text contains calls (JieLi's .volatile_ram_code): {rt_calls[:3]}")
    elif rt_calls:                  # RAM code runs with the flash off: no calls into XIP
        errors.append(f".ram_text contains calls: {rt_calls[:3]}")
    else:
        notes.append(f".ram_text: {len([ln for ln in rt.splitlines() if LINE.match(ln)])} insns, no calls")
    skip_fns, fn = set(CONF.get("rom_check_skip", [])), ""
    for ln in dis.splitlines():     # nothing may call or load an address in the chip ROM
        mm = LINE.match(ln)
        if not mm:
            m2 = re.match(r"^([A-Za-z_.$][\w.$]*):$", ln.strip())
            if m2:
                fn = m2.group(1)
            continue
        if fn in skip_fns:          # (a vendored function whose constant only looks like a ROM address)
            continue
        for v in re.findall(r"= (-?\d+) <|call -?\d+ <[^:>]*: ([0-9a-f]+) >", mm.group(3)):
            val = (int(v[0]) & 0xFFFFFFFF) if v[0] else int(v[1], 16) & 0xFFFFFFFF
            if 0xFFC00000 <= val < 0xFFD00000:
                errors.append(f"reference to ROM address {val:#010x}")
    mul64 = mul64_near_loads(dis)       # the CPU fault of bsp docs/CPU.md: a note, the code may be fine
    if mul64:
        notes.append(f"64-bit multiplies inlined beside loads (felucca-bsp docs/CPU.md: may corrupt a "
                     f"register; move them out of line): {', '.join(mul64[:8])}" + (" ..." if len(mul64) > 8 else ""))
    else:
        notes.append("no 64-bit multiply inlined beside a load")
    bt = [n for n in BT_SYMS if re.search(r"\s" + n + r"$", syms, re.M)]
    if CONF.get("ble") and not BLE:
        if bt:
            errors.append(f"--no-ble build links Bluetooth code: {bt}")
        else:
            notes.append("--no-ble: no Bluetooth code linked")
    if len(img) > APP_SLOT:
        errors.append(f"image {len(img)} B exceeds the app slot")
    # the FPU is single precision; a soft-double routine in the image means a double crept in
    soft = sorted(set(re.findall(r"\s(__(?:add|sub|mul|div|neg|cmp|eq|ne|lt|le|gt|ge|un|extendsf|truncdf|fixdf|fixunsdf|floatsidf|floatunsidf|floatdidf)\w*df\w*)$", syms, re.M)))
    if soft and (BLE or WIFI):
        notes.append(f"soft-double routines linked (JieLi's radio setup): {soft[:6]}")
    elif soft:
        errors.append(f"soft-double routines linked: {soft[:6]} (a double in the code)")
    else:
        notes.append("no soft-double routines (single-precision FPU only)")

    def sym(name):
        mm = re.search(r"^([0-9a-f]+) .*\s" + name + r"$", syms, re.M)
        return int(mm.group(1), 16) if mm else 0
    bss = sym("_bss_end") - 0x01C08000
    pool = sym("_pool_end") - sym("_pool_start")
    ram, pool_max, spare = (128 * 1024, 0x4C000, 2048) if WIFI else (96 * 1024, 0x54000, 8192)   # (app_wifi.ld)
    notes.append(f"image {len(img)} B; RAM .data+.bss {bss} B of {ram}; pool {pool} B of {pool_max}")
    if bss > ram:
        errors.append("RAM region overflow")
    if pool_max - pool < spare:                   # keep some of the pool spare
        errors.append(f"pool headroom {pool_max - pool} B < {spare} B")
    return errors, notes


# Every hardware register access lives in felucca-bsp's chip/ and boards/. In device/, include/ and the app
# (comments stripped):
#  - no volatile pointer cast, except of a C object's address (`*(volatile T *)&x`: a read-once
#    of a RAM flag shared with an ISR, e.g. usb.c ota_wire_send);
#  - no literal in a register or reserved window (core SFRs 0x10000-0x13FFF, SFC 0x40000-0x43FFF,
#    GPIO/IOMAP 0x50000-0x51FFF, CPU 0x1EE0000-0x1EEFFFF, RAM top 0x01C7F000- (boot info,
#    mailbox, vectors), XIP 0x02000000-0x020FFFFF);
#  - no inline asm, except the empty compiler barrier RING_PUBLISH() (emits no instruction);
#  - no HAL register macro (a hal/ #define that is, or expands to, a volatile access).
# Linker symbols (_bss_start[], _rt_load[], ...) are plain C objects and pass.
MMIO_LIT = re.compile(r"\b0x0*(1[0-3][0-9a-f]{3}|4[0-3][0-9a-f]{3}|5[01][0-9a-f]{3}|1ee[0-9a-f]{4}|"
                      r"1c7f[0-9a-f]{3}|20[0-9a-f]{5})u?l?\b", re.I)
MMIO_CAST = re.compile(r"\(\s*(?:const\s+)?volatile\b[^()]*\*\s*\)(?!\s*&)")
MMIO_ASM = re.compile(r"\b(?:__asm__|asm)\b(?!\s+volatile\s*\(\s*\"\"\s*:::\s*\"memory\"\s*\))")


def mmio_check():
    def strip(s):
        s = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), s, flags=re.S)
        return re.sub(r"//[^\n]*", "", s)
    defs = {}
    for h in sorted([*(BSP / "chip").glob("*.h"), *(BSP / "boards" / "fm1").glob("*.h"), *(BSP / "boards" / "smk37").glob("*.h")]):
        for m in re.finditer(r"^#define\s+(\w+)(?:\([^)]*\))?\s+(.*)$", strip(h.read_text()), re.M):
            defs[m.group(1)] = m.group(2)
    regs = {n for n, b in defs.items() if re.search(r"\bvolatile\b", b)}
    while True:                                   # macros built on register macros (FM1_WR_LIMIT_H -> FM1_X2)
        more = {n for n, b in defs.items() if n not in regs and set(re.findall(r"\w+", b)) & regs}
        if not more:
            break
        regs |= more
    errors = []
    skip = CONF.get("mmio_skip", [])
    files = [*(SRC / "device").glob("*.[ch]"), *(SRC / "include").glob("*.h"), *(SRC / "lib").glob("*.[ch]"),
             *[f for f in APP.rglob("*.[ch]") if "build" not in f.relative_to(APP).parts
               and not any(str(f.relative_to(APP)).startswith(v) for v in CONF.get("vendored", []))]]
    for f in sorted(files):
        for no, ln in enumerate(strip(f.read_text()).splitlines(), 1):
            where = f"{f.relative_to(ROOT)}:{no}"
            for rx, what in ((MMIO_LIT, "register/window address"), (MMIO_CAST, "volatile pointer cast"),
                             (MMIO_ASM, "inline asm")):
                if rx is MMIO_LIT and any(str(f.relative_to(APP)).startswith(s) for s in skip if APP in f.parents):
                    continue
                m = rx.search(ln)
                if m:
                    errors.append(f"{where}: {what} outside the BSP ({m.group(0).strip()}); add a BSP helper")
            used = set(re.findall(r"\b[A-Z_][A-Z0-9_]*\b", ln)) & regs
            if used:
                errors.append(f"{where}: BSP register macro {sorted(used)[0]} outside the BSP; use a BSP helper")
    return errors


def o2_files():
    """the o2 units, their globs expanded"""
    return [f for u in CONF.get("o2", []) for f in (sorted(APP.glob(u)) if any(ch in u for ch in "*?[") else [APP / u])]


def o2_objects(cc, base_flags, out):
    """the o2 units compiled apart (each with its own flags: o2_cflags, o2_include, its directory) for the host or the
    browser; their objects"""
    extra = [*CONF.get("o2_cflags", []), *[a for h in CONF.get("o2_include", []) for a in ("-include", str(APP / h))]]
    out.mkdir(parents=True, exist_ok=True)
    jobs = [(f, out / ("_".join(f.relative_to(APP).with_suffix("").parts) + ".o")) for f in o2_files()]

    def one(job):
        f, o = job
        r = subprocess.run([cc, *base_flags, *extra, f"-I{f.parent}", "-c", str(f), "-o", str(o)],
                           capture_output=True, text=True)
        return r.stderr if r.returncode else ""
    with ThreadPoolExecutor(8) as ex:
        errs = [e for e in ex.map(one, jobs) if e]
    if errs:
        sys.stderr.write(errs[0])
        raise SystemExit("build: an o2 unit failed")
    return [str(o) for _, o in jobs]


def build_host():
    """the app against a simulated FM-1 (host/plat_host.c), with this machine's compiler; -ffp-contract=off like the
    device (no fused multiply-add), so the samples match"""
    out = OUT / "host"
    out.mkdir(parents=True, exist_ok=True)
    exe = out / (STEM + "_host")                  # NAME_host; NAME-usb_host with --no-ble
    inc_dirs = [f"-I{d}" for d in (SRC / "device", SRC / "include", SRC / "lib", APP, GEN, *(APP / d for d in CONF.get("include", [])))]
    o2_objs = o2_objects(os.environ.get("CC", "cc"), ["-O2", "-ffp-contract=off", "-std=c99", "-w", "-DPLAT_HOST", *BOARD_DEFS, *inc_dirs],
                         out / "o2")
    cmd = [os.environ.get("CC", "cc"), "-O2", "-ffp-contract=off", "-std=c99", "-Wall", "-Wextra",
           "-Wno-unused-function", "-Wno-unused-parameter", "-DPLAT_HOST", *BOARD_DEFS, *["-DFM1B_BLE=1"] * BLE, *["-DFM1B_BLE_CENTRAL=1"] * (BLE and bool(CONF.get("ble_central"))), *["-DFM1B_WIFI=1"] * WIFI,
           *(f"-I{d}" for d in (SRC / "device", SRC / "include", SRC / "lib", APP, GEN, *(APP / d for d in CONF.get("include", [])))),
           "-o", str(exe), str(SRC / "host" / "plat_host.c"), *o2_objs, "-lm"]
    r = subprocess.run(cmd, capture_output=True, text=True)
    sys.stderr.write(r.stderr)
    if r.returncode:
        raise SystemExit("build: the host simulator did not compile")
    print(f"host     {exe}")
    return 0


def build_web():
    """the host simulator in WebAssembly (web/emu/emu_web.c) and its page, in OUT/emu; the screen reader's voice
    (eSpeak NG, GPL: a separate program the page loads) from an fm1-reader checkout next to fm1-base, if there"""
    import shutil
    emu = OUT / "emu"
    emu.mkdir(parents=True, exist_ok=True)
    web_inc = [f"-I{d}" for d in (SRC / "host", SRC / "device", SRC / "include", SRC / "lib", APP, GEN, *(APP / d for d in CONF.get("include", [])))]
    o2_objs = o2_objects("emcc", ["-O2", "-ffp-contract=off", "-std=gnu99", "-w", "-DPLAT_HOST", "-DPLAT_WEB", *web_inc],
                         emu / "o2")
    cmd = ["emcc", "-O2", "-ffp-contract=off", "-std=gnu99", "-Wall", "-Wno-unused-function", "-Wno-unused-parameter",
           "-Wno-unused-variable", "-DPLAT_HOST", "-DPLAT_WEB", *["-DFM1B_BLE=1"] * BLE, *["-DFM1B_BLE_CENTRAL=1"] * (BLE and bool(CONF.get("ble_central"))), *["-DFM1B_WIFI=1"] * WIFI, *(f'-DAPP_VERSION="{VERSION}"',) * bool(VERSION),
           *(f"-I{d}" for d in (SRC / "host", SRC / "device", SRC / "include", SRC / "lib", APP, GEN, *(APP / d for d in CONF.get("include", [])))),
           "--no-entry", "-sSTANDALONE_WASM", "-sSTACK_SIZE=1048576", "-sINITIAL_MEMORY=33554432", "-sFILESYSTEM=0",
           "-o", str(emu / "app.wasm"), str(SRC / "web" / "emu" / "emu_web.c"), *o2_objs]
    r = subprocess.run(cmd, capture_output=True, text=True)
    sys.stderr.write(r.stderr)
    if r.returncode:
        raise SystemExit("build: the emulator did not compile (emcc: Emscripten)")
    # the page: the app's own (APP_DIR/web/emu/index.html, worklet.js), or fm1-base's
    web = {f: (APP / "web" / "emu" / f if (APP / "web" / "emu" / f).exists() else SRC / "web" / "emu" / f)
           for f in ("index.html", "worklet.js")}
    page = web["index.html"].read_text()
    page = page.replace("{{NAME}}", CONF.get("name", NAME)).replace("{{ID}}", NAME)
    (emu / "index.html").write_text(page)
    shutil.copy(web["worklet.js"], emu / "worklet.js")
    # next to fm1-base, or next to the app's repository (fm1-base a submodule in it)
    found = [p / "fm1-reader" / "web" for base in (SRC.parent, APP.parent.parent) for p in (base, base / "fm1-parent")
             if (p / "fm1-reader" / "web").is_dir()]
    reader = Path(os.environ.get("FM1_READER", found[0] if found else SRC.parent / "fm1-reader" / "web"))
    if (reader / "words.js").exists() and (reader / "espeak" / "espeak.wasm").exists():
        (emu / "reader" / "espeak").mkdir(parents=True, exist_ok=True)
        for f in ("words.js", "tts.js", "tts_worker.js"):
            shutil.copy(reader / f, emu / "reader" / f)
        for f in ("espeak.js", "espeak.wasm", "LICENSE", "README.md"):
            shutil.copy(reader / "espeak" / f, emu / "reader" / "espeak" / f)
        (emu / "reader" / "FROM.txt").write_text(f"From fm1-reader (https://github.com/charlesvestal/fm1-reader) web/, "
                                                 f"copied by fm1-base's tools/build.py --web.\n")
    else:
        print(f"emu: no fm1-reader at {reader}: the screen is read as plain text, without its voice (set FM1_READER)")
    print(f"emu      {emu}  ({(emu / 'app.wasm').stat().st_size} B wasm)")
    return 0


def usb_name(key, v):
    """the USB names on the SMK-37: the product's "FM-1" reads "SMK-37" (the installers find the keyboard by it)"""
    if BOARD != "smk37" or key != "usb_product":
        return v
    v = v.replace("FM-1", "SMK-37") if "FM-1" in v else v + " SMK-37"
    if len(v) > 31:
        raise SystemExit(f"usb_product {v!r} on the SMK-37: more than 31 characters")
    return v


def set_board(board, model):
    """BOARD / MODEL: the flags, the app slot, the package layout"""
    global BOARD, MODEL, APP_SLOT, BOARD_DEFS
    BOARD = (board or "fm1").lower().replace("-", "")
    if BOARD not in ("fm1", "smk37"):
        raise SystemExit(f"BOARD={BOARD}: fm1 or smk37")
    MODEL = (model or "pro").lower()
    if MODEL not in MODELS or (MODEL != "pro" and BOARD != "smk37"):
        raise SystemExit(f"MODEL={MODEL}: pro, elite or mke, with BOARD=smk37")
    fm1pkg_make.set_board(BOARD)
    APP_SLOT = fm1pkg_make.APP_SLOT
    BOARD_DEFS = []
    if BOARD == "smk37":
        BOARD_DEFS = ["-DBOARD_SMK37=1"] + ([f'-DSMK_MODEL="{MODELS[MODEL]}"'] if MODEL != "pro" else [])


def main():
    global PRODUCT, VERSION, APP, OUT, GEN, LDR, ROOT, CONF, NAME, BLE, WIFI, STEM
    ap = argparse.ArgumentParser()
    ap.add_argument("app", type=Path, help="the app's directory (with fm1.toml)")
    ap.add_argument("--release", metavar="X.Y[.Z]", help="release build: identity id_prefix + XXYYZZ, version string X.Y[.Z]")
    ap.add_argument("--no-ble", action="store_true", help="without Bluetooth (fm1.toml ble = true): NAME-usb.fwsc")
    ap.add_argument("--sdk", type=Path, help="JieLi AC79 SDK checkout (default: $AC79_SDK)")
    ap.add_argument("--host", action="store_true", help="build the host simulator instead of the firmware")
    ap.add_argument("--web", action="store_true", help="build the browser emulator instead of the firmware")
    ap.add_argument("--board", choices=["fm1", "smk37"], default=None,
                    help="target board: fm1 (default) or smk37, the M-Vave SMK-37 (default: $BOARD, else fm1)")
    ap.add_argument("--model", choices=sorted(MODELS), default=None,
                    help="smk37: the model name, pro (default), elite or mke (default: $MODEL, else pro)")
    a = ap.parse_args()
    board = a.board or os.environ.get("BOARD")
    set_board(board, a.model or (os.environ.get("MODEL") if board == "smk37" else None))
    APP = a.app.resolve()
    CONF = read_conf(APP / "fm1.toml")
    NAME = CONF.get("name", APP.name).lower()
    BLE = bool(CONF.get("ble")) and not a.no_ble
    WIFI = bool(CONF.get("wifi"))
    if BLE and WIFI:
        raise SystemExit("fm1.toml: ble and wifi together are not supported yet (one radio a build; --no-ble for WiFi)")
    STEM = NAME + ("-usb" if CONF.get("ble") and not BLE else "")
    OUT = APP / "build"
    if BOARD == "smk37":                          # the FM-1's files stay where they are (the tests use them)
        OUT = OUT / ("smk37" + ("" if MODEL == "pro" else "-" + MODEL))
        STEM = STEM + "-smk37" + ("" if MODEL == "pro" else "-" + MODEL)
    GEN, LDR = OUT / "gen", OUT / "loader"
    ROOT = Path(os.path.commonpath([SRC, APP]))
    prefix = CONF.get("id_prefix", "FM-1_0")
    if not re.fullmatch(r"FM-1_\d+", prefix):
        raise SystemExit(f"fm1.toml id_prefix {prefix!r}: FM-1_ and digits only (the update tools parse it)")
    if BOARD == "smk37":                          # the same digits after the SMK-37 model's name
        digits = str(CONF.get("smk_id_prefix", prefix[len("FM-1_"):]))
        if not re.fullmatch(r"\d+", digits):
            raise SystemExit(f"fm1.toml smk_id_prefix {digits!r}: digits only")
        prefix = MODELS[MODEL] + "_" + digits
    PRODUCT = prefix + "00"
    name = STEM + ".fwsc"
    if a.release:
        # identity: the prefix + major, minor, patch as two digits each (0.1 -> FM-1_0000100): FM-1 is the
        # model the update tools match on; the digits after the underscore are the firmware's.
        m = re.fullmatch(r"(\d{1,2})\.(\d{1,2})(?:\.(\d{1,2}))?(-[A-Za-z0-9]+)?", a.release)
        if not m:
            raise SystemExit(f"--release {a.release}: use X.Y[.Z][-suffix], up to two digits each")
        PRODUCT = prefix + "%02d%02d%02d" % (int(m[1]), int(m[2]), int(m[3] or 0))
        if len(PRODUCT) > 20:
            raise SystemExit(f"identity {PRODUCT!r}: more than 20 characters (fm1.toml smk_id_prefix: fewer digits)")
        VERSION = a.release.upper()
        name = f"{STEM}-{a.release}.fwsc"
    if a.host or a.web:
        generate()
        return build_host() if a.host else build_web()
    fm1pkg_make.SDK = a.sdk
    for rel, sha in SDK_SHA256.items():          # fail early without the SDK
        if hashlib.sha256(fm1pkg_make.sdk_file(rel)).hexdigest() != sha:
            print(f"warning: SDK {rel} differs from AC79NN_SDK_V1.2.1; the package will not match the reference")
    OUT.mkdir(parents=True, exist_ok=True)
    with ThreadPoolExecutor(2) as ex:
        gen, ldr = ex.submit(generate), ex.submit(build_loader)
        gen.result()
        ota = ldr.result()
    img, syms, dis, rt = build_app()
    errors, notes = check(img, syms, dis, rt)
    hal_err = mmio_check()
    errors += hal_err
    if not hal_err:
        notes.append("register access: felucca-bsp only (device/, include/ and the app clean)")
    for n in notes:
        print("  ok   ", n)
    for e in errors:
        print("  FAIL ", e)
    if errors:
        raise SystemExit("build: checks failed")
    def on(key, env, default):
        v = os.environ.get(env)
        return v == "1" if v in ("0", "1") else bool(CONF.get(key, default))
    parts = [("Bluetooth MIDI", BLE), ("Bluetooth MIDI devices", BLE and bool(CONF.get("ble_central"))), ("WiFi", WIFI),
             ("USB audio in", on("usb_audio_in", "FM1B_UAC", True)),
             ("USB audio out", on("usb_audio_out", "FM1B_UAC_OUT", True) and on("usb_audio_in", "FM1B_UAC", True)),
             ("velocity and pressure", any(re.search(r"^#define APP_TOUCH\b", f.read_text(errors="ignore"), re.M)
                                            for f in APP.glob("*.h")))]
    print("parts    " + ", ".join(n for n, x in parts if x) + "  (not built in: " +
          (", ".join(n for n, x in parts if not x) or "none") + ")")
    pkg = fm1pkg_make.ufw(fm1pkg_make.flash_image(img, fm1pkg_make.KEY), ota, PRODUCT)
    (OUT / name).write_bytes(pkg)
    print(f"app      {OUT / (NAME + '.bin')}  {len(img)} B")
    print(f"loader   {LDR / 'ota.bin'}  {len(ota)} B")
    print(f"package  {OUT / name}  {len(pkg)} B, identity {PRODUCT}" + ("" if BOARD == "fm1" else f", board {BOARD}"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
