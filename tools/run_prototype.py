#!/usr/bin/env python3
"""Run the Chromium-backed Zitter probe; --verify runs storage, keyboard and native-shell tests."""
import argparse
import os
from pathlib import Path
import plistlib
import subprocess
import sys


def mac_app(executable):
    """Give LaunchServices an identifiable app without duplicating build output."""
    out = executable.parent
    app = out / "Zitter Prototype.app"
    contents = app / "Contents"
    macos = contents / "MacOS"
    macos.mkdir(parents=True, exist_ok=True)
    for source in out.iterdir():
        if source == app:
            continue
        target = macos / source.name
        if source == executable:
            if target.exists() and not os.path.samefile(source, target):
                target.unlink()
            if not target.exists():
                os.link(source, target)
        elif not target.exists() and not target.is_symlink():
            target.symlink_to(source)
    frameworks = contents / "Frameworks"
    frameworks.mkdir(exist_ok=True)
    framework = frameworks / "Chromium Framework.framework"
    if not framework.exists() and not framework.is_symlink():
        framework.symlink_to(out / framework.name)
    resources = contents / "Resources"
    resources.mkdir(exist_ok=True)
    icon = Path(__file__).resolve().parents[1] / "assets/icon/Zitter.icns"
    if icon.exists() and not (resources / "app.icns").is_symlink():
        (resources / "app.icns").symlink_to(icon)
    with (contents / "Info.plist").open("wb") as stream:
        plistlib.dump({"CFBundleExecutable": executable.name,
                       "CFBundleIconFile": "app.icns",
                       "CFBundleIdentifier": "dev.zitter.prototype",
                       "CFBundleName": "Zitter Prototype",
                       "CFBundleDisplayName": "Zitter Prototype",
                       "CFBundlePackageType": "APPL", "CFBundleVersion": "1",
                       "NSHighResolutionCapable": True}, stream)
    return macos / executable.name


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify", action="store_true",
                        help="Run automated storage, keyboard and shell tests instead of the manual window")
    parser.add_argument("--out", type=Path, default=root / ".chromium/src/out/Zitter")
    args = parser.parse_args()
    executable = args.out.resolve() / ("zitter_prototype.exe" if sys.platform == "win32"
                                       else "zitter_prototype")
    if not executable.is_file():
        parser.exit(1, f"Prototype has not been built: {executable}\nSee README.md.\n")
    test_filter = ("ZitterProfilesBrowserTest.*-*.ManualWindow" if args.verify
                   else "ZitterProfilesBrowserTest.ManualWindow")
    command = [str(executable), f"--gtest_filter={test_filter}",
               "--test-launcher-jobs=1", "--enable-pixel-output-in-tests"]
    if not args.verify:
        # Keep the browser test in this process for a long-lived manual window.
        # Renderer/GPU processes remain separate; this is not --single-process.
        command += ["--zitter-interactive", "--single-process-tests",
                    "--test-launcher-timeout=3600000"]
        if sys.platform == "darwin":
            # Bundling must not redirect children to nonexistent branded helpers.
            command += [f"--browser-subprocess-path={executable}"]
            command[0] = str(mac_app(executable))
    return subprocess.call(command, cwd=root / ".chromium/src")


if __name__ == "__main__":
    sys.exit(main())
