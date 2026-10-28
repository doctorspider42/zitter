#!/usr/bin/env python3
"""Package the regular Chromium-backed Zitter executable as a local macOS app.

This component development bundle links to this checkout's build dependencies;
it is signed with a pinned identity but is not a notarized, portable installer.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SIGNING_CONFIG = ROOT / ".zitter-signing.json"
BUNDLE_ID = "dev.zitter.browser"
LSREGISTER = Path("/System/Library/Frameworks/CoreServices.framework/Frameworks/"
                  "LaunchServices.framework/Support/lsregister")


def bundle_marker(app):
    current = app / "Contents/Resources/zitter-development-bundle"
    return current if current.is_file() else app / "Contents/zitter-development-bundle"


def clone_file(source, target):
    """Private APFS clone: signing must never mutate hard-linked build outputs."""
    target = Path(target)
    if target.exists() or target.is_symlink():
        target.unlink()
    if sys.platform == "darwin":
        result = subprocess.run(["/bin/cp", "-c", "-p", str(source), str(target)],
                                capture_output=True)
        if result.returncode == 0:
            return str(target)
        if target.exists():
            target.unlink()
    shutil.copy2(source, target)
    return str(target)


def signing_identity(explicit=None):
    identity = explicit or os.environ.get("ZITTER_CODESIGN_IDENTITY")
    if not identity and SIGNING_CONFIG.exists():
        identity = json.loads(SIGNING_CONFIG.read_text()).get("identity")
    if not identity or identity == "-":
        raise ValueError("Configure a permanent certificate with --sign-identity IDENTITY; "
                         "use --unsigned only for disposable test bundles")
    return identity


def sign_bundle(app, identity):
    """Sign nested code from the inside out, then verify the complete seal."""
    magic = {b"\xfe\xed\xfa\xce", b"\xce\xfa\xed\xfe", b"\xfe\xed\xfa\xcf",
             b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca",
             b"\xca\xfe\xba\xbf", b"\xbf\xba\xfe\xca"}
    files = []
    bundles = []
    for path in app.rglob("*"):
        if path.is_symlink():
            continue
        if path.is_dir() and path.suffix in (".app", ".framework", ".xpc"):
            bundles.append(path)
        elif path.is_file():
            with path.open("rb") as stream:
                if stream.read(4) in magic:
                    files.append(path)
    # codesign treats a bundle's executable as the whole bundle. Seal it only
    # after its children, through the enclosing bundle path below.
    executables = set()
    for bundle in bundles + [app]:
        plist = bundle / ("Resources/Info.plist" if bundle.suffix == ".framework" else "Contents/Info.plist")
        with plist.open("rb") as stream:
            name = plistlib.load(stream)["CFBundleExecutable"]
        executable = bundle / name if bundle.suffix == ".framework" else bundle / "Contents/MacOS" / name
        executables.add(executable.resolve())
    unique_files = {}
    for path in files:
        if path.resolve() not in executables:
            stat = path.stat()
            unique_files.setdefault((stat.st_dev, stat.st_ino), path)
    files = list(unique_files.values())
    # Do not use --deep for signing: each nested code object needs its own seal.
    def sign(path):
        command = ["/usr/bin/codesign", "--force", "--sign", identity, "--timestamp"]
        if path == app:
            command += ["--identifier", BUNDLE_ID]
        subprocess.run(command + [str(path)], check=True)
    # Independent dylibs can be signed concurrently; parent seals wait for all.
    with ThreadPoolExecutor(max_workers=4) as pool:
        list(pool.map(sign, files))
    for path in sorted(bundles, key=lambda p: len(p.parts), reverse=True) + [app]:
        sign(path)
    subprocess.run(["/usr/bin/codesign", "--verify", "--deep", "--strict", "--verbose=2", str(app)], check=True)


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


def package(out, destination, binary="zitter", register=True, identity=None, third_party_licenses=None):
    out = out.resolve()
    destination = destination.absolute()
    if not re.fullmatch(r"zitter(?:_[a-z0-9]+)*", binary):
        raise ValueError("Executable must be a Zitter target basename within the build directory")
    executable = out / binary
    if not executable.is_file():
        raise ValueError(f"Build //zitter:zitter first: {executable}")
    contents = destination / "Contents"
    if destination.exists():
        marker = bundle_marker(destination)
        if not marker.is_file():
            raise ValueError(f"Refusing to overwrite an unowned bundle: {destination}")
    macos = contents / "MacOS"
    macos.mkdir(parents=True, exist_ok=True)
    # Upstream helper/path conventions expect this inner executable name. The
    # application's visible name, bundle identity and data directory are Zitter.
    target = macos / "Chromium"
    if identity and (target.exists() or target.is_symlink()):
        target.unlink()
    if target.exists() and not os.path.samefile(target, executable):
        target.unlink()
    if not target.exists():
        if identity:
            clone_file(executable, target)
        else:
            os.link(executable, target)
    frameworks = contents / "Frameworks"
    frameworks.mkdir(exist_ok=True)
    framework = frameworks / "Chromium Framework.framework"
    if framework.is_symlink():
        framework.unlink()
    elif framework.exists():
        shutil.rmtree(framework)  # Generated subtree of our marker-owned bundle.
    private_copies = {}
    def link_file(source, target):
        if identity:
            source = Path(source).resolve()
            target = Path(target)
            existing = private_copies.get(source)
            if existing and existing.exists():
                if target.exists() or target.is_symlink():
                    target.unlink()
                os.link(existing, target)
                return str(target)
            clone_file(source, target)
            private_copies[source] = target
            return str(target)
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
    for name in ('LICENSE', 'NOTICE'):
        if (ROOT / name).is_file():
            clone_file(ROOT / name, resources / name)
    if third_party_licenses:
        clone_file(Path(third_party_licenses), resources / 'THIRD_PARTY_LICENSES.txt')
    # The design system's typefaces (SIL OFL 1.1) with their licenses; macOS
    # activates them for this app through ATSApplicationFontsPath.
    fonts = resources / "Fonts"
    shutil.rmtree(fonts, ignore_errors=True)
    font_sources = sorted((ROOT / "assets/fonts").glob("*")) if (ROOT / "assets/fonts").is_dir() else []
    if font_sources:
        fonts.mkdir()
        for font in font_sources:
            if font.suffix in (".ttf", ".otf", ".txt"):
                clone_file(font, fonts / font.name)
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
        "CFBundleIdentifier": BUNDLE_ID,
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
    if font_sources:
        info["ATSApplicationFontsPath"] = "Fonts"
    with (contents / "Info.plist").open("wb") as stream:
        plistlib.dump(info, stream)
    (resources / "zitter-development-bundle").write_text(str(out) + "\n")
    (contents / "zitter-development-bundle").unlink(missing_ok=True)
    os.utime(destination)
    if identity:
        sign_bundle(destination, identity)
    if register and LSREGISTER.exists() and destination.is_relative_to(ROOT):
        subprocess.run([str(LSREGISTER), "-f", str(destination)], check=False)
    print(destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / ".chromium/src/out/Zitter")
    parser.add_argument("--destination", type=Path, default=ROOT / "build/Zitter.app")
    parser.add_argument("--binary", default="zitter", help="Regular application target basename")
    parser.add_argument("--third-party-licenses", type=Path, help="Target-audited license collection to include before signing")
    parser.add_argument("--no-register", action="store_true", help="Skip LaunchServices registration for benchmark bundles")
    signing = parser.add_mutually_exclusive_group()
    signing.add_argument("--sign-identity", help="Permanent certificate name or SHA-1; saved locally after success")
    signing.add_argument("--unsigned", action="store_true", help="Explicit opt-out for disposable test bundles")
    args = parser.parse_args()
    if sys.platform != "darwin":
        parser.exit(1, "This packager is for macOS; run the native executable on Linux.\n")
    try:
        identity = None if args.unsigned else signing_identity(args.sign_identity)
        destination = args.destination.absolute()
        if destination.exists() and not bundle_marker(destination).is_file():
            raise ValueError(f"Refusing to overwrite an unowned bundle: {destination}")
        destination.parent.mkdir(parents=True, exist_ok=True)
        # Do not replace the installed bundle until signing and verification pass.
        with tempfile.TemporaryDirectory(prefix=".zitter-package-", dir=destination.parent) as stage:
            staged_app = Path(stage) / destination.name
            package(args.out, staged_app, args.binary, register=False, identity=identity, third_party_licenses=args.third_party_licenses)
            previous = Path(stage) / "previous.app"
            if destination.exists():
                destination.rename(previous)
            try:
                staged_app.rename(destination)
            except OSError:
                if previous.exists():
                    previous.rename(destination)
                raise
        if identity:
            SIGNING_CONFIG.write_text(json.dumps({"identity": identity}, indent=2) + "\n")
        if not args.no_register and LSREGISTER.exists() and destination.is_relative_to(ROOT):
            subprocess.run([str(LSREGISTER), "-f", str(destination)], check=False)
        print(f"Packaged: {destination}")
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Packaging failed: {error}\n")


if __name__ == "__main__":
    main()
