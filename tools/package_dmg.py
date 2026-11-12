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

from notarize_app import (
    CONTENT_APP_NAME, CONTENT_BUNDLE_ID, cdhash, read_content_release_metadata,
    run, save, sha256,
    verify_nested_signatures, wait_for_acceptance,
)
from package_app import BUNDLE_ID, signing_identity


REPOSITORY = Path(__file__).resolve().parents[1]
VOLUME_NAME = "Zitter"
# The Finder window shows the 720x432 background below a 28 pt title bar; the
# icons sit at the centres the artwork leaves for them.
WINDOW_ORIGIN = (200, 120)
WINDOW_SIZE = (720, 432 + 28)
ARCHITECTURE_LABELS = {"arm64": "Apple Silicon", "x86_64": "Intel", "universal": "Apple Silicon · Intel"}


def render_background(folder, version, architecture, work):
    """Render the drag-to-Applications artwork as a 1x/2x TIFF for Finder."""
    run(["swift", str(REPOSITORY / "tools/render_dmg_background.swift"), str(REPOSITORY / "assets/fonts"),
         version, ARCHITECTURE_LABELS[architecture], str(work)])
    folder.mkdir()
    run(["tiffutil", "-cathidpicheck", str(work / "background.png"), str(work / "background@2x.png"),
         "-out", str(folder / "background.tiff")])


def finder_layout_script(disk, app_name="Zitter.app"):
    left, top = WINDOW_ORIGIN
    width, height = WINDOW_SIZE
    icon_positions = {app_name: (200, 236), "Applications": (520, 236)}
    positions = "\n".join(
        f'        set position of item "{name}" of container window to {{{x}, {y}}}'
        for name, (x, y) in icon_positions.items())
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


def build_image(payload, layout, image, *, app_name="Zitter.app", volume_name=VOLUME_NAME):
    """Create the compressed image after Finder saves the window layout in .DS_Store."""
    megabytes = int(run(["du", "-sm", str(payload)], capture=True).stdout.split()[0])
    run(["hdiutil", "create", "-volname", volume_name, "-srcfolder", str(payload),
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
        run(["osascript", "-e", finder_layout_script(mount.name, app_name)])
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


def prepare(source, destination, identity, *, content_candidate=False,
            production_identity_trial=False, content_release=False):
    info = plistlib.loads((source / "Contents/Info.plist").read_bytes())
    bundle_id = BUNDLE_ID if production_identity_trial or content_release or not content_candidate else CONTENT_BUNDLE_ID
    app_name = CONTENT_APP_NAME if content_candidate and not content_release else "Zitter.app"
    executable_name = "Zitter Content Lab" if content_candidate else "Chromium"
    if (info.get("CFBundleIdentifier") != bundle_id
            or info.get("CFBundleExecutable") != executable_name
            or (content_release and (info.get("CFBundleDisplayName") != "Zitter"
                                     or info.get("ZitterUsePreparedWorkspace") is not True))):
        raise ValueError(f"Expected the {bundle_id} application")
    version = info["CFBundleShortVersionString"]
    if production_identity_trial and re.fullmatch(r"\d+\.\d+\.\d+\.0", version):
        # GN emits a fourth component. Keep the signed plist untouched.
        version = version.rsplit(".", 1)[0]
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise ValueError("Expected a three-component numeric application version")
    verify_app(source)
    verify_nested_signatures(source)
    metadata_file = source / "Contents/Resources/zitter-build.json"
    metadata = (read_content_release_metadata(source) if content_release
                else json.loads(metadata_file.read_text()) if metadata_file.exists() else {})
    architecture = run(["lipo", "-archs", str(source / "Contents/MacOS" / executable_name)], capture=True).stdout.split()
    architecture = "universal" if set(architecture) == {"arm64", "x86_64"} else "-".join(architecture)
    if architecture not in ("arm64", "x86_64", "universal"):
        raise ValueError("Unsupported application architecture")
    if destination.exists() and any(destination.iterdir()):
        raise ValueError("Use an empty destination to prepare a disk image")
    destination.mkdir(parents=True, exist_ok=True)
    payload = destination / "payload"
    payload.mkdir()
    app = payload / app_name
    # Clone on APFS so staging a large signed bundle does not duplicate its
    # data before hdiutil creates the writable layout image.
    run(["/bin/cp", "-cRp", str(source), str(app)])
    if cdhash(source) != cdhash(app):
        raise ValueError("Copying the app changed its code signature")
    verify_app(app)
    (payload / "Applications").symlink_to("/Applications", target_is_directory=True)
    render_background(payload / ".background", version, architecture, destination / "background")
    shutil.copy2(REPOSITORY / "assets/icon/Zitter.icns", payload / ".VolumeIcon.icns")
    image = destination / "submission.dmg"
    volume_name = VOLUME_NAME if app_name == "Zitter.app" else "Zitter Content Lab"
    build_image(payload, destination / "layout.dmg", image,
                app_name=app_name, volume_name=volume_name)
    image_id = (CONTENT_BUNDLE_ID + ".identity-trial.dmg"
                if production_identity_trial else bundle_id + ".dmg")
    run(["codesign", "--force", "--sign", identity, "--timestamp", "--identifier",
         image_id, str(image)])
    run(["codesign", "--verify", "--strict", str(image)])
    state = {"source": str(source), "version": version, "architecture": architecture,
             "source_commit": metadata.get("source_commit"), "app_cdhash": cdhash(app),
             "upload_sha256": sha256(image), "status": "Prepared"}
    if content_candidate:
        state["content_candidate"] = True
    if production_identity_trial:
        state["production_identity_trial"] = True
    if content_release:
        state["content_release"] = True
        for field in ("backend", "source_dirty", "chromium_revision", "gn_args_sha256",
                      "gn_dependency_count", "gn_dependencies_sha256", "chrome_metadata_inputs"):
            state[field] = metadata[field]
    save(destination / "dmg-state.json", state)
    return state


def verify_image(image, expected_cdhash, *, app_name="Zitter.app"):
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
            app = mount / app_name
            if not (mount / "Applications").is_symlink() or (mount / "Applications").readlink() != Path("/Applications"):
                raise ValueError("DMG is missing its Applications shortcut")
            if not (mount / ".background/background.tiff").is_file() or not (mount / ".DS_Store").is_file():
                raise ValueError("DMG is missing its window background or layout")
            if cdhash(app) != expected_cdhash:
                raise ValueError("The DMG contains a different app signature")
            verify_app(app)
            # Model a browser download without bypassing quarantine protections.
            copy = Path(directory) / "downloaded" / app_name
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
    lab = state.get("content_candidate", False) and not state.get("content_release", False)
    if state.get("production_identity_trial", False):
        prefix = "Zitter-Content-Identity-Trial"
    else:
        prefix = "Zitter-Content-Lab" if lab else "Zitter"
    platform = "macos-content" if state.get("content_release", False) else "macos"
    stem = f"{prefix}-v{state['version']}-{platform}-{state['architecture']}"
    output = destination / f"{stem}.dmg"
    # Preserve the submitted image byte-for-byte so interrupted stapling is resumable.
    shutil.copy2(image, output)
    run(["xcrun", "stapler", "staple", str(output)])
    verify_image(output, state["app_cdhash"], app_name=CONTENT_APP_NAME if lab else "Zitter.app")
    checksum = sha256(output)
    (destination / f"SHA256SUMS-{platform}-{state['architecture']}-dmg.txt").write_text(
        f"{checksum}  {output.name}\n")
    state.update({"status": "Complete", "artifact": output.name, "artifact_sha256": checksum,
                  "dmg_ticket": "passed", "mounted_app_ticket": "passed", "gatekeeper": "passed",
                  "quarantined_app_distribution": "passed"})
    save(state_path, state)
    save(destination / f"{stem}-dmg-build.json",
         {k: v for k, v in state.items() if k != "source"})
    print(f"Notarized release DMG: {output}", flush=True)
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", required=True, type=Path, help="Already signed, notarized and stapled app")
    parser.add_argument("--destination", required=True, type=Path)
    parser.add_argument("--keychain-profile", default="zitter-notary")
    parser.add_argument("--sign-identity")
    parser.add_argument("--content-candidate", action="store_true",
                        help="Package a stapled Content Lab app under its separate identity")
    identity_mode = parser.add_mutually_exclusive_group()
    identity_mode.add_argument("--content-release", action="store_true",
                               help="Package the Content release as Zitter.app")
    identity_mode.add_argument("--production-identity-trial", action="store_true",
                        help="Package a Content rehearsal signed under the production bundle identifier")
    parser.add_argument("--prepare-only", action="store_true")
    parser.add_argument("--submission-id", help="Recover the matching interrupted DMG upload")
    parser.add_argument("--timeout", type=int, default=1200)
    parser.add_argument("--poll-interval", type=int, default=20)
    args = parser.parse_args()
    if args.production_identity_trial and not args.content_candidate:
        parser.error("--production-identity-trial requires --content-candidate")
    content_candidate = args.content_candidate or args.content_release
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
            if (state.get("content_candidate", False) != content_candidate
                    or state.get("production_identity_trial", False) != args.production_identity_trial
                    or state.get("content_release", False) != args.content_release):
                raise ValueError("Destination belongs to another packaging mode")
        else:
            state = prepare(source, destination, signing_identity(args.sign_identity),
                            content_candidate=content_candidate,
                            production_identity_trial=args.production_identity_trial,
                            content_release=args.content_release)
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
