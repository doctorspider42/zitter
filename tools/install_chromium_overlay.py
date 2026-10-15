#!/usr/bin/env python3
"""Install the Zitter desktop and test targets into an existing, pinned Chromium checkout.

No downloads, git staging, or broad replacement of Chromium source files.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DEPENDENCY = '      "//zitter:profile_workspace_browser_tests",\n'
MARKER = ".zitter-overlay.json"


def patch_build(text):
    target = '  test("browser_tests") {'
    if text.count(target) != 1:
        raise ValueError("Expected exactly one desktop browser_tests target")
    start = text.index(target)
    anchor = "    deps = [\n"
    position = text.index(anchor, start) + len(anchor)
    if text.count(DEPENDENCY) == 1 and text[position:].startswith(DEPENDENCY):
        return text
    if "//zitter:profile_workspace_browser_tests" in text:
        raise ValueError("Existing Zitter dependency is in an unexpected location")
    return text[:position] + DEPENDENCY + text[position:]


def overlay_files():
    files = {"BUILD.gn": ROOT / "chromium/BUILD.gn"}
    for directory in ("zitter", "chromium/prototype", "chromium/app", "testdata"):
        for path in sorted((ROOT / directory).rglob("*")):
            if path.is_file():
                files[str(path.relative_to(ROOT))] = path
    return files


def patch_keychain(text):
    # Keep OSCrypt's random key and native Keychain protection, with our own
    # identity. Reusing Chromium's item can block behind another app's ACL.
    for old, new in (("Chromium Safe Storage", "Zitter Safe Storage"),
                     ('"Chromium";', '"Zitter";')):
        if text.count(new) == 1 and old not in text:
            continue
        if text.count(old) != 1 or new in text:
            raise ValueError("Unexpected upstream Keychain identity; review the patch")
        text = text.replace(old, new)
    return text


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check_existing(destination):
    if not destination.exists():
        return
    marker = destination / MARKER
    if not marker.is_file():
        raise ValueError(f"Refusing to replace an unowned directory: {destination}")
    manifest = json.loads(marker.read_text())
    if manifest.get("owner") != "zitter-prototype":
        raise ValueError("Invalid overlay ownership marker")
    hashes = manifest["files"]
    actual = {str(p.relative_to(destination)) for p in destination.rglob("*")
              if p.is_file() and p.name != MARKER}
    if actual != set(hashes):
        raise ValueError("Overlay has added or deleted files; preserve local work first")
    for name, expected in hashes.items():
        if digest(destination / name) != expected:
            raise ValueError(f"Overlay has local modifications: {name}")


def install(checkout, dry_run=False):
    checkout = checkout.resolve()
    lock = json.loads((ROOT / "chromium/revision.json").read_text())
    revision = subprocess.check_output(
        ["git", "-C", str(checkout), "rev-parse", "HEAD"], text=True).strip()
    if revision != lock["revision"]:
        raise ValueError(f"Chromium revision mismatch: {revision}; expected {lock['revision']}")
    build = checkout / "chrome/test/BUILD.gn"
    original = build.read_text()
    patched = patch_build(original)
    keychain = checkout / "components/os_crypt/common/keychain_password_mac.mm"
    keychain_original = keychain.read_text()
    keychain_patched = patch_keychain(keychain_original)
    destination = checkout / "zitter"
    check_existing(destination)
    files = overlay_files()
    print(f"Chromium revision: {revision}")
    print(f"Overlay: {destination} ({len(files)} files)")
    print(f"Build hook: {build}")
    if dry_run:
        print("Dry run: no files changed")
        return
    # Prepare all files before touching the checkout's existing overlay.
    with tempfile.TemporaryDirectory(prefix="zitter-overlay-", dir=checkout) as staging:
        staged = Path(staging) / "zitter"
        staged.mkdir()
        hashes = {}
        for name, source in files.items():
            target = staged / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
            hashes[name] = digest(target)
        (staged / MARKER).write_text(json.dumps(
            {"owner": "zitter-prototype", "revision": revision, "files": hashes}, indent=2) + "\n")
        backup = Path(staging) / "previous"
        if destination.exists():
            destination.rename(backup)
        try:
            staged.rename(destination)
            # Preserve other edits in this file; add only our single dependency.
            if patched != original:
                build.write_text(patched)
            if keychain_patched != keychain_original:
                keychain.write_text(keychain_patched)
        except Exception:
            if destination.exists():
                shutil.rmtree(destination)
            if backup.exists():
                backup.rename(destination)
            build.write_text(original)
            keychain.write_text(keychain_original)
            raise
    print("Installed. Run gn gen/check and build zitter or zitter_prototype; see README.md.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("checkout", type=Path, help="Chromium src directory")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    try:
        install(args.checkout, args.dry_run)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Overlay installation failed: {error}\n")


if __name__ == "__main__":
    main()
