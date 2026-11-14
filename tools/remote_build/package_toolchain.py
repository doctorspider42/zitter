#!/usr/bin/env python3
"""Packages the installed VS 2026 Build Tools and Windows SDK as a hermetic
Chromium toolchain (<hash>.zip), which remote compiles need.

  python tools/remote_build/package_toolchain.py [--sdk 10.0.28000.0]

Uses depot_tools' win_toolchain/package_from_installed.py with two fixes for
this machine, applied to a temporary copy: vswhere must list Build Tools
(`-products *`) and print UTF-8 (`-utf8`) on a Polish Windows.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
DEPOT_TOOLS = os.path.join(REPO, ".chromium", "depot_tools")
DEFAULT_OUT = os.path.join(REPO, ".chromium", "win_toolchain_pkg")


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--sdk", default="10.0.28000.0", help="Windows SDK version")
    p.add_argument("--out", default=DEFAULT_OUT, help="folder for <hash>.zip")
    args = p.parse_args()

    src_dir = os.path.join(DEPOT_TOOLS, "win_toolchain")
    if not os.path.isdir(src_dir):
        sys.exit(f"depot_tools not found at {DEPOT_TOOLS}")
    os.makedirs(args.out, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        for name in ("package_from_installed.py", "get_toolchain_if_necessary.py"):
            shutil.copy(os.path.join(src_dir, name), tmp)
        script = os.path.join(tmp, "package_from_installed.py")
        with open(script, encoding="utf-8") as f:
            text = f.read()
        old = 'vswhere.exe -prerelease"'
        if old not in text:
            sys.exit("package_from_installed.py changed; update package_toolchain.py")
        with open(script, "w", encoding="utf-8") as f:
            f.write(text.replace(old, 'vswhere.exe -prerelease -products * -utf8"'))
        env = dict(os.environ, PYTHONUTF8="1",
                   PATH=DEPOT_TOOLS + os.pathsep + os.environ.get("PATH", ""))
        subprocess.run([sys.executable, script, "2026", "-w", args.sdk, "--noarm",
                        "--allow_multiple_vs_installs"], cwd=args.out, env=env, check=True)
    print("Toolchain package is in", args.out)


if __name__ == "__main__":
    main()
