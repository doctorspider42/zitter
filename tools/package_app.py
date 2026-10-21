#!/usr/bin/env python3
"""Package the regular Chromium-backed Zitter executable as a local macOS app.

This component development bundle links to this checkout's build dependencies;
it is double-clickable but is not a portable distribution or signed installer.
"""
import argparse
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
LSREGISTER = Path("/System/Library/Frameworks/CoreServices.framework/Frameworks/"
                  "LaunchServices.framework/Support/lsregister")


def brand_notification_helpers(framework, icon):
    """macOS shows web notifications under the Alerts helpers' own icon and name."""
    seen = set()
    for helper in framework.glob("Versions/*/Helpers/*(Alerts).app"):
        real = helper.resolve()
        if real in seen:
            continue
        seen.add(real)
        resources = real / "Contents/Resources"
        # These files are hard links into the build output: unlink, never edit.
        for name in ("app.icns", "Assets.car"):
            if (resources / name).exists():
                (resources / name).unlink()
        shutil.copyfile(icon, resources / "app.icns")
        plist_path = real / "Contents/Info.plist"
        with plist_path.open("rb") as stream:
            info = plistlib.load(stream)
        info.pop("CFBundleIconName", None)
        info["CFBundleIconFile"] = "app.icns"
        info["CFBundleName"] = info["CFBundleDisplayName"] = "Zitter"
        plist_path.unlink()
        with plist_path.open("wb") as stream:
            plistlib.dump(info, stream)


def package(out, destination, binary="zitter", register=True):
    out = out.resolve()
    destination = destination.absolute()
    if not re.fullmatch(r"zitter(?:_[a-z0-9]+)*", binary):
        raise ValueError("Executable must be a Zitter target basename within the build directory")
    executable = out / binary
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
    libraries = list(out.glob("*.dylib"))
    names = {library.name for library in libraries}
    # Switching an owned bundle from a component build must not retain old
    # component libraries. Unlink only generated dylibs, preserving build-output
    # hardlinks and unrelated files in the bundle.
    for directory in (macos, frameworks):
        for stale in directory.glob("*.dylib"):
            if stale.name not in names and (stale.is_file() or stale.is_symlink()):
                stale.unlink()
    for library in libraries:
        for directory in (macos, frameworks):
            target = directory / library.name
            if target.is_symlink():
                target.unlink()
            link_file(library, target)
    for version in (framework / "Versions").iterdir():
        if version.is_symlink() or not version.is_dir():
            continue
        for library in libraries:
            target = version / library.name
            if target.is_symlink():
                target.unlink()
            link_file(library, target)
    resources = contents / "Resources"
    resources.mkdir(exist_ok=True)
    # Zitter's own icon (tools/build_icon.py); Chromium's only as a fallback.
    icon = ROOT / "assets/icon/Zitter.icns"
    if not icon.exists():
        icon = out / "Chromium.app/Contents/Resources/app.icns"
    if icon.exists():
        # A real copy, not a link: the Dock caches icons per bundle and only
        # notices a change through the bundle's own files and dates.
        bundled = resources / "app.icns"
        if bundled.is_symlink() or bundled.exists():
            bundled.unlink()
        shutil.copyfile(icon, bundled)
    if icon.is_relative_to(ROOT):
        brand_notification_helpers(framework, icon)
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
        "NSCameraUsageDescription": "Zitter udostępnia kamerę stronom internetowym wyłącznie po Twojej zgodzie.",
        "NSMicrophoneUsageDescription": "Zitter udostępnia mikrofon stronom internetowym wyłącznie po Twojej zgodzie.",
        "NSPrincipalClass": "BrowserCrApplication",
        "CFBundleURLTypes": [{
            "CFBundleURLName": "dev.zitter.browser.web",
            "CFBundleTypeRole": "Viewer",
            "CFBundleURLSchemes": ["http", "https"],
        }],
    }
    if icon.exists():
        info["CFBundleIconFile"] = "app.icns"
    with (contents / "Info.plist").open("wb") as stream:
        plistlib.dump(info, stream)
    (contents / "zitter-development-bundle").write_text(str(out) + "\n")
    os.utime(destination)
    if register and LSREGISTER.exists() and destination.is_relative_to(ROOT):
        subprocess.run([str(LSREGISTER), "-f", str(destination)], check=False)
    print(destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / ".chromium/src/out/Zitter")
    parser.add_argument("--destination", type=Path, default=ROOT / "build/Zitter.app")
    parser.add_argument("--binary", default="zitter", help="Regular application target basename")
    parser.add_argument("--no-register", action="store_true", help="Skip LaunchServices registration for benchmark bundles")
    args = parser.parse_args()
    if sys.platform != "darwin":
        parser.exit(1, "This packager is for macOS; run the native executable on Linux.\n")
    try:
        package(args.out, args.destination, args.binary, register=not args.no_register)
    except (OSError, ValueError) as error:
        parser.exit(1, f"Packaging failed: {error}\n")


if __name__ == "__main__":
    main()
