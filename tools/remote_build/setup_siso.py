#!/usr/bin/env python3
"""Prepares a Chromium checkout for LAN remote builds (Siso + NativeLink).

Run after every fresh checkout or `gclient sync` that replaces these files:

  python tools/remote_build/setup_siso.py

It does four things inside .chromium/src (all paths are gitignored there):
  1. installs the hermetic VS/SDK toolchain from a local package
     (third_party/depot_tools/win_toolchain/vs_files/<hash>, build/win_toolchain.json),
  2. downloads the Linux clang of the same revision
     (third_party/llvm-build/Release+Asserts_linux),
  3. installs the clang remote wrapper and rewrapper_windows.cfg
     (buildtools/reclient_cfgs/chromium-browser-clang),
  4. installs the Siso backend config (build/config/siso/backend_config/backend.star).

Make the toolchain package once with package_toolchain.py.
"""

import argparse
import glob
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
DEFAULT_SRC = os.path.join(REPO, ".chromium", "src")
DEFAULT_PKG = os.path.join(REPO, ".chromium", "win_toolchain_pkg")
DEPOT_TOOLS = os.path.join(REPO, ".chromium", "depot_tools")


def run(cmd, cwd, env=None, check=True):
    print("+", " ".join(cmd), flush=True)
    return subprocess.run(cmd, cwd=cwd, env=env, check=check)


def base_env():
    env = dict(os.environ)
    env["PATH"] = DEPOT_TOOLS + os.pathsep + env.get("PATH", "")
    env["PYTHONUTF8"] = "1"
    return env


def install_toolchain(src, pkg_dir):
    zips = sorted(glob.glob(os.path.join(pkg_dir, "*.zip")))
    if len(zips) != 1:
        sys.exit(f"Expected one toolchain .zip in {pkg_dir}, found {len(zips)}. "
                 "Run package_toolchain.py first.")
    zip_hash = os.path.splitext(os.path.basename(zips[0]))[0]
    with open(os.path.join(src, "build", "vs_toolchain.py"), encoding="utf-8") as f:
        wanted = re.search(r"^TOOLCHAIN_HASH = '([0-9a-f]+)'", f.read(), re.M)
    if not wanted:
        sys.exit("TOOLCHAIN_HASH not found in build/vs_toolchain.py")
    env = base_env()
    env["DEPOT_TOOLS_WIN_TOOLCHAIN"] = "1"
    env["DEPOT_TOOLS_WIN_TOOLCHAIN_BASE_URL"] = pkg_dir
    env[f"GYP_MSVS_HASH_{wanted.group(1)}"] = zip_hash
    run([sys.executable, "build/vs_toolchain.py", "update", "--force"], src, env)
    if not os.path.exists(os.path.join(src, "build", "win_toolchain.json")):
        sys.exit("build/win_toolchain.json was not written")


def install_linux_clang(src):
    out = os.path.join("third_party", "llvm-build", "Release+Asserts_linux")
    # Unpacking ends with an error on a symlinked directory on Windows; the
    # files remote compiles need are extracted before it, so check those.
    run([sys.executable, "tools/clang/scripts/update.py", "--output-dir", out,
         "--host-os", "linux"], src, base_env(), check=False)
    clang = os.path.join(src, out, "bin", "clang")
    with open(clang, "rb") as f:
        if f.read(4) != b"\x7fELF":
            sys.exit(f"{clang} is not a Linux binary")
    with open(os.path.join(src, out, "bin", "clang-cl_remote_toolchain_inputs")) as f:
        for line in f.read().split():
            p = os.path.normpath(os.path.join(src, out, "bin", line))
            if not os.path.exists(p):
                sys.exit(f"Missing Linux clang file: {p}")


def install_configs(src):
    cfg_dir = os.path.join(src, "buildtools", "reclient_cfgs", "chromium-browser-clang")
    os.makedirs(cfg_dir, exist_ok=True)
    for name in ("clang_remote_wrapper", "rewrapper_windows.cfg"):
        shutil.copyfile(os.path.join(HERE, name), os.path.join(cfg_dir, name))
    shutil.copyfile(os.path.join(HERE, "backend.star"),
                    os.path.join(src, "build", "config", "siso", "backend_config",
                                 "backend.star"))


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--src", default=DEFAULT_SRC, help="Chromium src directory")
    p.add_argument("--toolchain-dir", default=DEFAULT_PKG,
                   help="folder with the packaged toolchain <hash>.zip")
    p.add_argument("--skip-toolchain", action="store_true")
    p.add_argument("--skip-clang", action="store_true")
    args = p.parse_args()
    src = os.path.abspath(args.src)
    if not os.path.isfile(os.path.join(src, "build", "vs_toolchain.py")):
        sys.exit(f"No Chromium checkout at {src}")
    if not args.skip_toolchain:
        install_toolchain(src, os.path.abspath(args.toolchain_dir))
    if not args.skip_clang:
        install_linux_clang(src)
    install_configs(src)
    print("Remote build setup is ready in", src)


if __name__ == "__main__":
    main()
