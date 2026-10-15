#!/usr/bin/env python3
"""Package the regular Chromium-backed Zitter executable as a local macOS app.

This component development bundle links to this checkout's build dependencies;
it is double-clickable but is not a portable distribution or signed installer.
"""
import argparse
import os
from pathlib import Path
import plistlib
import shutil
import sys


ROOT = Path(__file__).resolve().parents[1]


def symlink(source, target):
    if target.is_symlink():
        if target.resolve() == source.resolve():
            return
        target.unlink()
    elif target.exists():
        raise ValueError(f"Refusing to replace an unrelated file: {target}")
    target.symlink_to(source)


def package(out, destination):
    out = out.resolve()
    destination = destination.absolute()
    executable = out / "zitter"
    if not executable.is_file():
        raise ValueError(f"Build //zitter:zitter first: {executable}")
    contents = destination / "Contents"
    if destination.exists():
        marker = contents / "zitter-development-bundle"
        if not marker.is_file():
            raise ValueError(f"Refusing to overwrite an unowned bundle: {destination}")
    macos = contents / "MacOS"
    macos.mkdir(parents=True, exist_ok=True)
    # Upstream helper/path conventions expect this inner executable name. The
    # application's visible name, bundle identity and data directory are Zitter.
    target = macos / "Chromium"
    if target.exists() and not os.path.samefile(target, executable):
        target.unlink()
    if not target.exists():
        os.link(executable, target)
    frameworks = contents / "Frameworks"
    frameworks.mkdir(exist_ok=True)
    framework = frameworks / "Chromium Framework.framework"
    if framework.is_symlink():
        framework.unlink()
    elif framework.exists():
        shutil.rmtree(framework)  # Generated subtree of our marker-owned bundle.
    def link_file(source, target):
        target = Path(target)
        if target.exists():
            if os.path.samefile(source, target):
                return str(target)
            target.unlink()
        os.link(source, target)
        return str(target)
    # Helpers must physically live under an outer .app/Contents/Frameworks:
    # resolving a symlink to an unbundled framework breaks NSBundle discovery.
    shutil.copytree(out / "Chromium Framework.framework", framework,
                    symlinks=True, dirs_exist_ok=True, copy_function=link_file)
    for library in out.glob("*.dylib"):
        for directory in (macos, frameworks):
            target = directory / library.name
            if target.is_symlink():
                target.unlink()
            link_file(library, target)
    for version in (framework / "Versions").iterdir():
        if version.is_symlink() or not version.is_dir():
            continue
        for library in out.glob("*.dylib"):
            target = version / library.name
            if target.is_symlink():
                target.unlink()
            link_file(library, target)
    resources = contents / "Resources"
    resources.mkdir(exist_ok=True)
    icon = out / "Chromium.app/Contents/Resources/app.icns"
    if icon.exists():
        symlink(icon, resources / "app.icns")
    info = {
        "CFBundleExecutable": "Chromium",
        "CFBundleIdentifier": "dev.zitter.browser",
        "CFBundleName": "Zitter",
        "CFBundleDisplayName": "Zitter",
        "CFBundlePackageType": "APPL",
        "CFBundleVersion": "0.1.0",
        "CFBundleShortVersionString": "0.1.0",
        "CrProductDirName": "Zitter",
        "LSMinimumSystemVersion": "13.0",
        "NSHighResolutionCapable": True,
        "NSPrincipalClass": "BrowserCrApplication",
    }
    if icon.exists():
        info["CFBundleIconFile"] = "app.icns"
    with (contents / "Info.plist").open("wb") as stream:
        plistlib.dump(info, stream)
    (contents / "zitter-development-bundle").write_text(str(out) + "\n")
    print(destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / ".chromium/src/out/Zitter")
    parser.add_argument("--destination", type=Path, default=ROOT / "build/Zitter.app")
    args = parser.parse_args()
    if sys.platform != "darwin":
        parser.exit(1, "This packager is for macOS; run the native executable on Linux.\n")
    try:
        package(args.out, args.destination)
    except (OSError, ValueError) as error:
        parser.exit(1, f"Packaging failed: {error}\n")


if __name__ == "__main__":
    main()
