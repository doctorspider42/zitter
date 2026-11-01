#!/usr/bin/env python3
"""Sign a private app copy, notarize it, staple the ticket and archive for release.

Credentials stay in a named notarytool Keychain profile. Reuse the destination
to resume a pending submission; --prepare-only needs no Apple credentials.
"""
import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import re
import subprocess
import sys
import time

from package_app import BUNDLE_ID, sign_bundle, signing_identity


def run(command, capture=False):
    return subprocess.run(command, check=True, text=True,
                          stdout=subprocess.PIPE if capture else None,
                          stderr=subprocess.PIPE if capture else None)


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(8 * 1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def save(path, value):
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n")
    temporary.replace(path)


def cdhash(app):
    result = run(["codesign", "--display", "--verbose=4", str(app)], capture=True)
    match = re.search(r"^CDHash=(\w+)$", result.stderr, re.M)
    if not match or "runtime" not in result.stderr:
        raise ValueError("Expected a signed Hardened Runtime application")
    return match[1]


def archive(app, destination):
    run(["ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", str(app), str(destination)])


def verify_nested_signatures(app):
    """A valid outer seal can still contain ad-hoc-signed nested libraries."""
    magic = {b"\xfe\xed\xfa\xce", b"\xce\xfa\xed\xfe", b"\xfe\xed\xfa\xcf",
             b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca",
             b"\xca\xfe\xba\xbf", b"\xbf\xba\xfe\xca"}
    count = 0
    for path in app.rglob("*"):
        if path.is_symlink() or not path.is_file():
            continue
        with path.open("rb") as stream:
            if stream.read(4) not in magic:
                continue
        result = run(["codesign", "--display", "--verbose=4", str(path)], capture=True)
        details = result.stderr
        if "Authority=Developer ID Application:" not in details or "Timestamp=" not in details:
            raise ValueError(f"Missing Developer ID signature or secure timestamp: {path.relative_to(app)}")
        if "runtime" not in details:
            raise ValueError(f"Missing Hardened Runtime signature: {path.relative_to(app)}")
        count += 1
    if not count:
        raise ValueError("Application contains no Mach-O code")
    print(f"Verified Developer ID, timestamp and Hardened Runtime on {count} Mach-O paths", flush=True)


def prepare(source, destination, identity):
    """Never sign hard links into a user's source application or build tree."""
    info = plistlib.loads((source / "Contents/Info.plist").read_bytes())
    if info.get("CFBundleIdentifier") != BUNDLE_ID:
        raise ValueError("Expected a Zitter application bundle")
    version = info["CFBundleShortVersionString"]
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise ValueError("Expected a numeric three-component application version")
    resources = source / "Contents/Resources"
    for name in ("LICENSE", "NOTICE", "THIRD_PARTY_LICENSES.txt"):
        if not (resources / name).is_file():
            raise ValueError(f"Missing bundled release notice: {name}")
    architectures = run(["lipo", "-archs", str(source / "Contents/MacOS/Chromium")], capture=True).stdout.split()
    architecture = "universal" if set(architectures) == {"arm64", "x86_64"} else "-".join(architectures)
    if architecture not in ("arm64", "x86_64", "universal"):
        raise ValueError(f"Unsupported macOS architecture: {architecture}")
    if destination.exists() and any(destination.iterdir()):
        raise ValueError("Use an empty destination for preparation; existing state resumes automatically")
    destination.mkdir(parents=True, exist_ok=True)
    app = destination / "work/Zitter.app"
    app.parent.mkdir()
    run(["ditto", str(source), str(app)])
    resources = app / "Contents/Resources"
    for marker in (resources / "zitter-development-bundle", app / "Contents/zitter-development-bundle"):
        marker.unlink(missing_ok=True)
    metadata_path = resources / "zitter-build.json"
    metadata = json.loads(metadata_path.read_text()) if metadata_path.exists() else {}
    metadata.pop("notarized", None)  # Acceptance is recorded externally after signing.
    metadata.pop("executable_sha256", None)  # Re-signing changes executable bytes.
    metadata["source_executable_sha256"] = sha256(source / "Contents/MacOS/Chromium")
    metadata.update({"version": version, "hardened_runtime": True, "signing": identity})
    save(metadata_path, metadata)
    sign_bundle(app, identity, hardened_runtime=True)
    verify_nested_signatures(app)
    upload = destination / "submission.zip"
    archive(app, upload)
    state = {"source": str(source), "version": version, "architecture": architecture,
             "cdhash": cdhash(app), "upload_sha256": sha256(upload), "status": "Prepared",
             "executable_sha256": sha256(app / "Contents/MacOS/Chromium")}
    save(destination / "notarization-state.json", state)
    return state


def notarize(destination, state, profile, timeout, poll_interval):
    state_path = destination / "notarization-state.json"
    app = destination / "work/Zitter.app"
    upload = destination / "submission.zip"
    run(["codesign", "--verify", "--deep", "--strict", str(app)])
    verify_nested_signatures(app)
    if cdhash(app) != state["cdhash"] or sha256(upload) != state["upload_sha256"]:
        raise ValueError("Prepared app or submission archive changed; use a new destination")
    auth = ["--keychain-profile", profile, "--output-format", "json"]
    if not state.get("submission_id"):
        if state["status"] == "Submitting":
            raise ValueError("Previous upload was interrupted before saving its ID. Inspect notarytool history, "
                             "then rerun with --submission-id for the matching upload")
        # Credential validation precedes uploading any application bytes.
        run(["xcrun", "notarytool", "history", *auth], capture=True)
        state["status"] = "Submitting"
        save(state_path, state)
        result = run(["xcrun", "notarytool", "submit", str(upload), *auth], capture=True)
        response = json.loads(result.stdout)
        state.update({"submission_id": response["id"], "status": response.get("status", "Submitted")})
        save(state_path, state)
        print(f"Apple submission: {state['submission_id']}", flush=True)
    deadline = time.monotonic() + timeout
    while True:
        result = run(["xcrun", "notarytool", "info", state["submission_id"], *auth], capture=True)
        response = json.loads(result.stdout)
        state["status"] = response["status"]
        save(state_path, state)
        print(f"Notarization: {state['status']}", flush=True)
        if state["status"] == "Accepted":
            break
        if state["status"] in ("Invalid", "Rejected"):
            run(["xcrun", "notarytool", "log", state["submission_id"],
                 str(destination / "notarization-log.json"), "--keychain-profile", profile])
            raise ValueError("Apple rejected the submission; inspect notarization-log.json before retrying")
        if time.monotonic() >= deadline:
            print("Still pending at Apple. Run the same command to resume; no new upload is needed.")
            return False
        time.sleep(min(poll_interval, max(0, deadline - time.monotonic())))
    run(["xcrun", "notarytool", "log", state["submission_id"],
         str(destination / "notarization-log.json"), "--keychain-profile", profile])
    log = json.loads((destination / "notarization-log.json").read_text())
    if log.get("sha256", "").lower() != state["upload_sha256"]:
        raise ValueError("Apple's accepted submission does not match this archive's SHA-256")
    run(["xcrun", "stapler", "staple", str(app)])
    run(["xcrun", "stapler", "validate", str(app)])
    run(["codesign", "--verify", "--deep", "--strict", str(app)])
    run(["spctl", "--assess", "--type", "execute", "--verbose=2", str(app)])
    filename = f"Zitter-v{state['version']}-macos-{state['architecture']}-notarized.zip"
    output = destination / filename
    temporary = destination / "final.tmp.zip"
    archive(app, temporary)
    temporary.replace(output)
    checksum = sha256(output)
    (destination / f"SHA256SUMS-macos-{state['architecture']}-notarized.txt").write_text(f"{checksum}  {filename}\n")
    state.update({"status": "Complete", "artifact": filename, "artifact_sha256": checksum,
                  "stapler_validate": "passed", "gatekeeper": "passed"})
    save(state_path, state)
    print(f"Notarized release archive: {output}", flush=True)
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", required=True, type=Path)
    parser.add_argument("--destination", required=True, type=Path)
    parser.add_argument("--keychain-profile", default="zitter-notary")
    parser.add_argument("--sign-identity")
    parser.add_argument("--submission-id", help="Recover the matching Apple submission after an interrupted upload")
    parser.add_argument("--prepare-only", action="store_true", help="Sign and archive without contacting Apple")
    parser.add_argument("--timeout", type=int, default=1200, help="Polling time in seconds; rerun to resume after timeout")
    parser.add_argument("--poll-interval", type=int, default=20)
    args = parser.parse_args()
    if sys.platform != "darwin":
        parser.error("This tool requires macOS and Xcode command-line tools")
    if args.timeout < 0 or not 1 <= args.poll_interval <= 60:
        parser.error("Timeout must be nonnegative; polling interval must be 1..60 seconds")
    source, destination = args.app.resolve(), args.destination.resolve()
    if destination == source or source in destination.parents or destination in source.parents:
        parser.error("Source app and working destination must not contain each other")
    try:
        state_path = destination / "notarization-state.json"
        if state_path.exists():
            state = json.loads(state_path.read_text())
            if state["source"] != str(source):
                raise ValueError("Destination belongs to a different source application")
        else:
            state = prepare(source, destination, signing_identity(args.sign_identity))
        if args.submission_id:
            if state.get("submission_id") and state["submission_id"] != args.submission_id:
                raise ValueError("Destination already tracks another submission")
            if state["status"] == "Complete":
                raise ValueError("This notarization is already complete")
            state["submission_id"] = args.submission_id
            save(state_path, state)
        if args.prepare_only:
            print(f"Prepared Hardened Runtime app: {destination / 'work/Zitter.app'}")
            return 0
        if state["status"] == "Complete":
            output = destination / state["artifact"]
            if sha256(output) != state["artifact_sha256"]:
                raise ValueError("Completed release archive changed")
            print(f"Already complete: {output}")
            return 0
        return 0 if notarize(destination, state, args.keychain_profile, args.timeout, args.poll_interval) else 2
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(f"Notarization stopped: {error}", file=sys.stderr)
        if isinstance(error, subprocess.CalledProcessError) and error.stderr:
            print(error.stderr, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
