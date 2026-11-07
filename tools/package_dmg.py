#!/usr/bin/env python3
"""Create, sign and notarize a drag-to-Applications DMG from a stapled Zitter app.

The app's existing signature and ticket are preserved. Finder lays out the
window over the rendered background, so packaging needs a logged-in macOS
session and lets Terminal control Finder. The DMG receives its own Developer ID
signature, Apple submission and ticket. No GitHub upload is done.
"""
import argparse
import json
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile

from notarize_app import cdhash, run, save, sha256, verify_nested_signatures, wait_for_acceptance
from package_app import BUNDLE_ID, signing_identity


REPOSITORY = Path(__file__).resolve().parents[1]
VOLUME_NAME = "Zitter"
# The Finder window shows the 720x432 background below a 28 pt title bar; the
# icons sit at the centres the artwork leaves for them.
WINDOW_ORIGIN = (200, 120)
WINDOW_SIZE = (720, 432 + 28)
ICON_POSITIONS = {"Zitter.app": (200, 236), "Applications": (520, 236)}
ARCHITECTURE_LABELS = {"arm64": "Apple Silicon", "x86_64": "Intel", "universal": "Apple Silicon · Intel"}


def render_background(folder, version, architecture, work):
    """Render the drag-to-Applications artwork as a 1x/2x TIFF for Finder."""
    run(["swift", str(REPOSITORY / "tools/render_dmg_background.swift"), str(REPOSITORY / "assets/fonts"),
         version, ARCHITECTURE_LABELS[architecture], str(work)])
    folder.mkdir()
    run(["tiffutil", "-cathidpicheck", str(work / "background.png"), str(work / "background@2x.png"),
         "-out", str(folder / "background.tiff")])


def finder_layout_script(disk):
    left, top = WINDOW_ORIGIN
    width, height = WINDOW_SIZE
    positions = "\n".join(
        f'        set position of item "{name}" of container window to {{{x}, {y}}}'
        for name, (x, y) in ICON_POSITIONS.items())
    return f'''tell application "Finder"
    tell disk "{disk}"
        open
        set current view of container window to icon view
        set toolbar visible of container window to false
        set statusbar visible of container window to false
        set bounds of container window to {{{left}, {top}, {left + width}, {top + height}}}
        set viewOptions to the icon view options of container window
        set arrangement of viewOptions to not arranged
        set icon size of viewOptions to 128
        set text size of viewOptions to 13
        set background picture of viewOptions to file ".background:background.tiff"
{positions}
        close
        open
        update without registering applications
        delay 2
        close
    end tell
end tell
'''


def build_image(payload, layout, image):
    """Create the compressed image after Finder saves the window layout in .DS_Store."""
    megabytes = int(run(["du", "-sm", str(payload)], capture=True).stdout.split()[0])
    run(["hdiutil", "create", "-volname", VOLUME_NAME, "-srcfolder", str(payload),
         "-fs", "HFS+", "-format", "UDRW", "-size", f"{megabytes + megabytes // 5 + 64}m", str(layout)])
    attached = run(["hdiutil", "attach", str(layout), "-readwrite", "-noverify", "-noautoopen",
                    "-plist"], capture=True).stdout
    mounts = [entity["mount-point"] for entity in plistlib.loads(attached.encode())["system-entities"]
              if "mount-point" in entity]
    if len(mounts) != 1:
        raise ValueError("Expected one mounted layout volume")
    mount = Path(mounts[0])
    try:
        run(["SetFile", "-a", "C", str(mount)])
        run(["SetFile", "-a", "V", str(mount / ".VolumeIcon.icns")])
        run(["osascript", "-e", finder_layout_script(mount.name)])
        if not (mount / ".DS_Store").exists():
            raise ValueError("Finder did not save the DMG window layout")
        shutil.rmtree(mount / ".fseventsd", ignore_errors=True)
        run(["sync"])
    finally:
        run(["hdiutil", "detach", str(mount)])
    run(["hdiutil", "convert", str(layout), "-format", "UDZO", "-imagekey", "zlib-level=9",
         "-o", str(image)])
    layout.unlink()


def verify_app(app):
    run(["codesign", "--verify", "--deep", "--strict", str(app)])
    run(["xcrun", "stapler", "validate", str(app)])
    run(["spctl", "--assess", "--type", "execute", "--verbose=2", str(app)])


def prepare(source, destination, identity):
    info = plistlib.loads((source / "Contents/Info.plist").read_bytes())
    if info.get("CFBundleIdentifier") != BUNDLE_ID:
        raise ValueError("Expected a Zitter application")
    version = info["CFBundleShortVersionString"]
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise ValueError("Expected a three-component numeric application version")
    verify_app(source)
    verify_nested_signatures(source)
    architecture = run(["lipo", "-archs", str(source / "Contents/MacOS/Chromium")], capture=True).stdout.split()
    architecture = "universal" if set(architecture) == {"arm64", "x86_64"} else "-".join(architecture)
    if architecture not in ("arm64", "x86_64", "universal"):
        raise ValueError("Unsupported application architecture")
    if destination.exists() and any(destination.iterdir()):
        raise ValueError("Use an empty destination to prepare a disk image")
    destination.mkdir(parents=True, exist_ok=True)
    payload = destination / "payload"
    payload.mkdir()
    app = payload / "Zitter.app"
    run(["ditto", str(source), str(app)])
    if cdhash(source) != cdhash(app):
        raise ValueError("Copying the app changed its code signature")
    verify_app(app)
    (payload / "Applications").symlink_to("/Applications", target_is_directory=True)
    render_background(payload / ".background", version, architecture, destination / "background")
    shutil.copy2(REPOSITORY / "assets/icon/Zitter.icns", payload / ".VolumeIcon.icns")
    image = destination / "submission.dmg"
    build_image(payload, destination / "layout.dmg", image)
    run(["codesign", "--force", "--sign", identity, "--timestamp", "--identifier",
         BUNDLE_ID + ".dmg", str(image)])
    run(["codesign", "--verify", "--strict", str(image)])
    metadata_file = source / "Contents/Resources/zitter-build.json"
    metadata = json.loads(metadata_file.read_text()) if metadata_file.exists() else {}
    state = {"source": str(source), "version": version, "architecture": architecture,
             "source_commit": metadata.get("source_commit"), "app_cdhash": cdhash(app),
             "upload_sha256": sha256(image), "status": "Prepared"}
    save(destination / "dmg-state.json", state)
    return state


def verify_image(image, expected_cdhash):
    run(["hdiutil", "verify", str(image)])
    run(["codesign", "--verify", "--strict", str(image)])
    run(["xcrun", "stapler", "validate", str(image)])
    run(["spctl", "--assess", "--type", "open", "--context", "context:primary-signature",
         "--verbose=2", str(image)])
    with tempfile.TemporaryDirectory(prefix="zitter-dmg-check-") as directory:
        mount = Path(directory) / "mounted"
        mount.mkdir()
        run(["hdiutil", "attach", str(image), "-readonly", "-nobrowse", "-mountpoint", str(mount)])
        try:
            app = mount / "Zitter.app"
            if not (mount / "Applications").is_symlink() or (mount / "Applications").readlink() != Path("/Applications"):
                raise ValueError("DMG is missing its Applications shortcut")
            if not (mount / ".background/background.tiff").is_file() or not (mount / ".DS_Store").is_file():
                raise ValueError("DMG is missing its window background or layout")
            if cdhash(app) != expected_cdhash:
                raise ValueError("The DMG contains a different app signature")
            verify_app(app)
            # Model a browser download without bypassing quarantine protections.
            copy = Path(directory) / "downloaded/Zitter.app"
            copy.parent.mkdir()
            run(["ditto", str(app), str(copy)])
            run(["xattr", "-w", "com.apple.quarantine", "0083;00000000;ZitterDistributionCheck;", str(copy)])
            run(["syspolicy_check", "distribution", str(copy)])
        finally:
            run(["hdiutil", "detach", str(mount)])


def finish(destination, state, profile, timeout, poll_interval):
    state_path = destination / "dmg-state.json"
    image = destination / "submission.dmg"
    if not wait_for_acceptance(destination, state, profile, image, state_path, timeout, poll_interval):
        return False
    output = destination / f"Zitter-v{state['version']}-macos-{state['architecture']}.dmg"
    # Preserve the submitted image byte-for-byte so interrupted stapling is resumable.
    shutil.copy2(image, output)
    run(["xcrun", "stapler", "staple", str(output)])
    verify_image(output, state["app_cdhash"])
    checksum = sha256(output)
    (destination / f"SHA256SUMS-macos-{state['architecture']}-dmg.txt").write_text(f"{checksum}  {output.name}\n")
    state.update({"status": "Complete", "artifact": output.name, "artifact_sha256": checksum,
                  "dmg_ticket": "passed", "mounted_app_ticket": "passed", "gatekeeper": "passed",
                  "quarantined_app_distribution": "passed"})
    save(state_path, state)
    save(destination / f"Zitter-v{state['version']}-macos-{state['architecture']}-dmg-build.json",
         {k: v for k, v in state.items() if k != "source"})
    print(f"Notarized release DMG: {output}", flush=True)
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", required=True, type=Path, help="Already signed, notarized and stapled app")
    parser.add_argument("--destination", required=True, type=Path)
    parser.add_argument("--keychain-profile", default="zitter-notary")
    parser.add_argument("--sign-identity")
    parser.add_argument("--prepare-only", action="store_true")
    parser.add_argument("--submission-id", help="Recover the matching interrupted DMG upload")
    parser.add_argument("--timeout", type=int, default=1200)
    parser.add_argument("--poll-interval", type=int, default=20)
    args = parser.parse_args()
    if sys.platform != "darwin":
        parser.error("This tool requires macOS and Xcode command-line tools")
    if args.timeout < 0 or not 1 <= args.poll_interval <= 60:
        parser.error("Timeout must be nonnegative; polling interval must be 1..60 seconds")
    source, destination = args.app.resolve(), args.destination.resolve()
    if source == destination or source in destination.parents or destination in source.parents:
        parser.error("Source and destination must not contain one another")
    try:
        state_path = destination / "dmg-state.json"
        if state_path.exists():
            state = json.loads(state_path.read_text())
            if state["source"] != str(source):
                raise ValueError("Destination belongs to another source app")
        else:
            state = prepare(source, destination, signing_identity(args.sign_identity))
        if args.submission_id:
            if state.get("submission_id") and state["submission_id"] != args.submission_id:
                raise ValueError("Destination tracks another Apple submission")
            if state["status"] == "Complete":
                raise ValueError("DMG notarization is already complete")
            state["submission_id"] = args.submission_id
            save(state_path, state)
        if args.prepare_only:
            print(f"Signed DMG prepared: {destination / 'submission.dmg'}")
            return 0
        if state["status"] == "Complete":
            if sha256(destination / state["artifact"]) != state["artifact_sha256"]:
                raise ValueError("Final DMG changed")
            print(f"Already complete: {destination / state['artifact']}")
            return 0
        return 0 if finish(destination, state, args.keychain_profile, args.timeout, args.poll_interval) else 2
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(f"DMG packaging stopped: {error}", file=sys.stderr)
        if isinstance(error, subprocess.CalledProcessError) and error.stderr:
            print(error.stderr, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
