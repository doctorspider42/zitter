#!/usr/bin/env python3
# Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
"""Package zitter.exe and what it loads at run time for Windows.

  python tools/package_win.py [--out .chromium/src/out/ZitterWin] [--setup PATH]

Writes build/Zitter-<version>-win-x64-portable.zip and, given the installer's
zitter_setup.exe, build/Zitter-<version>-win-x64-setup.exe: the installer with
the same files appended as a ZIP it reads from its own end. The file list is
GN's runtime_deps for //zitter:zitter without symbols, generated sources and
test data. The version is zitter.exe's (chromium/app/zitter_version.h).
"""
import argparse
import ctypes
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / ".chromium/src"
DEPOT_TOOLS = ROOT / ".chromium/depot_tools"
# Not needed at run time: symbols, link products, generated sources (DevTools'
# front end ships inside resources.pak), Python protos and ANGLE's test data.
EXCLUDED = re.compile(r"(^gen/|^\.\./|^pyproto/|^angledata/|\.(pdb|lib|exp|ilk)$)")
# Debugging aids that Chrome's own installer (chrome.release) leaves out too:
# the Vulkan validation layer, the mock driver, symbol readers and the
# pseudolocales a non-official build generates.
DEBUG_ONLY = {
    "VkLayer_khronos_validation.dll", "VkLayer_khronos_validation.json", "VkICD_mock_icd.dll",
    "VkICD_mock_icd.json", "msdia140.dll", "dbgcore.dll", "dbghelp.dll", "locales/en-XA.pak",
    "locales/ar-XB.pak",
}


def version():
    text = (ROOT / "chromium/app/zitter_version.h").read_text(encoding="utf-8")
    full = re.search(r'#define ZITTER_VERSION_STRING "([\d.]+)"', text).group(1)
    # 0.1.0.0 → 0.1.0, as the macOS bundle names it.
    return ".".join(full.split(".")[:3])


def select_files(runtime_deps):
    """Paths relative to the build directory that the package carries."""
    files = []
    for entry in runtime_deps:
        path = entry.strip().removeprefix("./")
        if not path or EXCLUDED.search(path) or path in DEBUG_ONLY:
            continue
        if path.startswith("resources/") and not path.startswith("resources/night_shift/"):
            continue
        if path.startswith("locales/") and not path.endswith(".pak"):
            continue
        files.append(path)
    if "zitter.exe" not in files:
        raise ValueError("runtime_deps has no zitter.exe; is this a Zitter build directory?")
    if "zitter_assistant.exe" not in files:
        raise ValueError("runtime_deps has no native zitter_assistant.exe helper")
    return sorted(set(files))


def runtime_deps(out):
    env = dict(os.environ, DEPOT_TOOLS_WIN_TOOLCHAIN="0", PATH=f"{DEPOT_TOOLS};{os.environ['PATH']}")
    # GN needs the toolchain from docs/windows-port.md, vs2026_install included.
    result = subprocess.run([sys.executable, str(DEPOT_TOOLS / "gn.py"), "desc", str(out), "//zitter:zitter",
                             "runtime_deps"], cwd=SRC, env=env, capture_output=True, text=True)
    if result.returncode:
        raise SystemExit(f"gn desc failed:\n{result.stdout}{result.stderr}")
    return result.stdout.splitlines()


def product_name(path):
    """Read the product from the built PE rather than from the current GN args."""
    path = str(path.resolve())
    version_api = ctypes.windll.version
    size = version_api.GetFileVersionInfoSizeW(path, None)
    if not size:
        raise ValueError(f"No version resource in {path}")
    resource = ctypes.create_string_buffer(size)
    name = ctypes.c_void_p()
    length = ctypes.c_uint()
    if not version_api.GetFileVersionInfoW(path, 0, size, resource) or not version_api.VerQueryValueW(
        resource, r"\StringFileInfo\040904b0\ProductName", ctypes.byref(name), ctypes.byref(length)
    ):
        raise ValueError(f"No product name in {path}")
    return ctypes.wstring_at(name.value)


def write_zip(destination, out, files, prefix="", extras=None):
    with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED, compresslevel=6, allowZip64=True) as archive:
        for name in files:
            source = out / name
            if not source.is_file():
                raise FileNotFoundError(f"{source} is listed but not built")
            archive.write(source, prefix + name)
        for name, source in (extras or {}).items():
            archive.write(source, prefix + name)


def package_notices(third_party_licenses=None):
    notices = {name: ROOT / name for name in ("LICENSE", "NOTICE")}
    notices.update({f"Fonts/{path.name}": path
                    for path in (ROOT / "assets/fonts").glob("*-OFL.txt")})
    if third_party_licenses:
        notices["THIRD_PARTY_LICENSES.txt"] = third_party_licenses
    return notices


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, default=SRC / "out/ZitterWin")
    parser.add_argument("--setup", type=Path, help="zitter_setup.exe; adds the installer to the output")
    parser.add_argument("--destination", type=Path, default=ROOT / "build")
    parser.add_argument("--third-party-licenses", type=Path,
                        help="THIRD_PARTY_LICENSES.txt from tools/audit_licenses.py, shipped beside zitter.exe")
    args = parser.parse_args()
    out = args.out.resolve()
    product = product_name(out / "zitter.exe")
    if product not in ("Zitter", "Zitter Dev"):
        parser.error(f"Unexpected product name in zitter.exe: {product}")
    dev = product == "Zitter Dev"
    if dev and not args.setup:
        parser.error("Zitter Dev requires a standalone zitter_setup.exe")
    if args.setup and product_name(args.setup) != product:
        parser.error("zitter.exe and zitter_setup.exe have different product names")
    files = select_files(runtime_deps(out))
    extras = package_notices(args.third_party_licenses)
    args.destination.mkdir(parents=True, exist_ok=True)
    stem = f"{product.replace(' ', '-')}-{version()}-win-x64"
    if not dev:
        portable = args.destination / f"{stem}-portable.zip"
        write_zip(portable, out, files, prefix="Zitter/", extras=extras)
        print(f"{portable} ({len(files) + len(extras)} files, {portable.stat().st_size // 2**20} MB)")
    if args.setup:
        # The installer finds the ZIP from its end: minizip skips the bytes before it.
        setup = args.destination / f"{stem}-setup.exe"
        payload = args.destination / f"{stem}-payload.zip"
        write_zip(payload, out, files, extras=extras)
        with open(setup, "wb") as target:
            for part in (args.setup, payload):
                with open(part, "rb") as source:
                    shutil.copyfileobj(source, target)
        payload.unlink()
        print(f"{setup} ({setup.stat().st_size // 2**20} MB, unsigned)")


if __name__ == "__main__":
    main()
