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
import shutil
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


def read_content_release_metadata(app):
    path = app / "Contents/Resources/zitter-build.json"
    if not path.is_file() or path.is_symlink():
        raise ValueError("Content release requires its sealed build metadata")
    metadata = json.loads(path.read_text())
    if not isinstance(metadata, dict) or metadata.get("backend") != "content":
        raise ValueError("Expected Content build provenance")
    for field, length in (("source_commit", 40), ("chromium_revision", 40),
                          ("gn_args_sha256", 64), ("gn_dependencies_sha256", 64)):
        value = metadata.get(field)
        if not isinstance(value, str) or not re.fullmatch(f"[0-9a-f]{{{length}}}", value):
            raise ValueError(f"Invalid Content build metadata field: {field}")
    count = metadata.get("gn_dependency_count")
    inputs = metadata.get("chrome_metadata_inputs")
    if (type(metadata.get("source_dirty")) is not bool
            or type(count) is not int or count <= 0
            or not isinstance(inputs, list)
            or any(path not in ("chrome/VERSION", "chrome/app/theme/chromium/BRANDING")
                   for path in inputs)):
        raise ValueError("Content release requires a verified dependency graph")
    return metadata


def cdhash(app):
    result = run(["codesign", "--display", "--verbose=4", str(app)], capture=True)
    match = re.search(r"^CDHash=(\w+)$", result.stderr, re.M)
    if not match or "runtime" not in result.stderr:
        raise ValueError("Expected a signed Hardened Runtime application")
    return match[1]


def archive(app, destination):
    run(["ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", str(app), str(destination)])


def notary_read(command):
    """Retry transient Apple network reads, never submissions or auth errors."""
    for attempt in range(3):
        try:
            return run(command, capture=True)
        except subprocess.CalledProcessError as error:
            if attempt == 2 or not re.search(r"NSURLErrorDomain Code=-(?:1001|1003|1005|1009)\b", error.stderr or ""):
                raise
            print("Temporary Apple network error; retrying the same read", flush=True)
            time.sleep(3 * (attempt + 1))


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


CONTENT_BUNDLE_ID = "dev.zitter.content-lab"
CONTENT_APP_NAME = "Zitter Content Lab.app"


def configure_production_identity_trial(app, *, content_release=False):
    for path in app.rglob("Info.plist"):
        if path.is_symlink():
            continue
        info = plistlib.loads(path.read_bytes())
        identifier = info.get("CFBundleIdentifier", "")
        if identifier == CONTENT_BUNDLE_ID or identifier.startswith(CONTENT_BUNDLE_ID + "."):
            info["CFBundleIdentifier"] = BUNDLE_ID + identifier[len(CONTENT_BUNDLE_ID):]
            if path == app / "Contents/Info.plist":
                info["ZitterUsePreparedWorkspace"] = True
                if content_release:
                    info.update(CFBundleName="Zitter", CFBundleDisplayName="Zitter")
                    info["CFBundleURLTypes"] = [{
                        "CFBundleTypeRole": "Viewer",
                        "CFBundleURLName": "dev.zitter.browser.web",
                        "CFBundleURLSchemes": ["http", "https"],
                    }]
            path.write_bytes(plistlib.dumps(info))


def prepare(source, destination, identity, *, content_version=None,
            third_party_licenses=None, production_identity_trial=False, content_release=False):
    """Never sign hard links into a user's source application or build tree."""
    if content_release:
        read_content_release_metadata(source)
    info = plistlib.loads((source / "Contents/Info.plist").read_bytes())
    content = content_version is not None
    bundle_id = CONTENT_BUNDLE_ID if content else BUNDLE_ID
    if info.get("CFBundleIdentifier") != bundle_id:
        raise ValueError(f"Expected the {bundle_id} application bundle")
    version = content_version if content else info["CFBundleShortVersionString"]
    executable_name = "Zitter Content Lab" if content else "Chromium"
    executable = Path("Contents/MacOS") / executable_name
    if info.get("CFBundleExecutable") != executable_name:
        raise ValueError("Unexpected application executable")
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise ValueError("Expected a numeric three-component application version")
    if content:
        if not third_party_licenses or not third_party_licenses.is_file():
            raise ValueError("Content preparation requires the target's audited license collection")
    else:
        for name in ("LICENSE", "NOTICE", "THIRD_PARTY_LICENSES.txt"):
            if not (source / "Contents/Resources" / name).is_file():
                raise ValueError(f"Missing bundled release notice: {name}")
    architectures = run(["lipo", "-archs", str(source / executable)], capture=True).stdout.split()
    architecture = "universal" if set(architectures) == {"arm64", "x86_64"} else "-".join(architectures)
    if architecture not in ("arm64", "x86_64", "universal"):
        raise ValueError(f"Unsupported macOS architecture: {architecture}")
    if destination.exists() and any(destination.iterdir()):
        raise ValueError("Use an empty destination for preparation; existing state resumes automatically")
    destination.mkdir(parents=True, exist_ok=True)
    app = destination / "work" / (CONTENT_APP_NAME if content and not content_release else "Zitter.app")
    app.parent.mkdir()
    run(["ditto", str(source), str(app)])
    resources = app / "Contents/Resources"
    if content:
        resources.mkdir(exist_ok=True)
        info.update({"CFBundleShortVersionString": version, "CFBundleVersion": version})
        (app / "Contents/Info.plist").write_bytes(plistlib.dumps(info))
        for name in ("LICENSE", "NOTICE"):
            shutil.copyfile(Path(__file__).resolve().parents[1] / name, resources / name)
        shutil.copyfile(third_party_licenses, resources / "THIRD_PARTY_LICENSES.txt")
    for marker in (resources / "zitter-development-bundle", app / "Contents/zitter-development-bundle"):
        marker.unlink(missing_ok=True)
    metadata_path = resources / "zitter-build.json"
    metadata = json.loads(metadata_path.read_text()) if metadata_path.exists() else {}
    metadata.pop("notarized", None)  # Acceptance is recorded externally after signing.
    metadata.pop("executable_sha256", None)  # Re-signing changes executable bytes.
    metadata["source_executable_sha256"] = sha256(source / executable)
    metadata.update({"version": version, "hardened_runtime": True, "signing": identity})
    save(metadata_path, metadata)
    if production_identity_trial or content_release:
        configure_production_identity_trial(app, content_release=content_release)
        bundle_id = BUNDLE_ID
    sign_bundle(app, identity, hardened_runtime=True, bundle_id=bundle_id)
    verify_nested_signatures(app)
    upload = destination / "submission.zip"
    archive(app, upload)
    state = {"source": str(source), "version": version, "architecture": architecture,
             "cdhash": cdhash(app), "upload_sha256": sha256(upload), "status": "Prepared",
             "executable_sha256": sha256(app / executable)}
    if content:
        state["content_candidate"] = True
    if production_identity_trial:
        state["production_identity_trial"] = True
    if content_release:
        state["content_release"] = True
    save(destination / "notarization-state.json", state)
    return state


def wait_for_acceptance(destination, state, profile, upload, state_path, timeout, poll_interval):
    """Submit a ZIP or DMG once, resume its ID and verify Apple's archive hash."""
    if sha256(upload) != state["upload_sha256"]:
        raise ValueError("Prepared submission changed; use a new destination")
    auth = ["--keychain-profile", profile, "--output-format", "json"]
    if not state.get("submission_id"):
        if state["status"] == "Submitting":
            raise ValueError("Previous upload was interrupted before saving its ID. Inspect notarytool history, "
                             "then rerun with --submission-id for the matching upload")
        # Credential validation precedes uploading any application bytes.
        notary_read(["xcrun", "notarytool", "history", *auth])
        state["status"] = "Submitting"
        save(state_path, state)
        result = run(["xcrun", "notarytool", "submit", str(upload), *auth], capture=True)
        response = json.loads(result.stdout)
        state.update({"submission_id": response["id"], "status": response.get("status", "Submitted")})
        save(state_path, state)
        print(f"Apple submission: {state['submission_id']}", flush=True)
    deadline = time.monotonic() + timeout
    while True:
        result = notary_read(["xcrun", "notarytool", "info", state["submission_id"], *auth])
        response = json.loads(result.stdout)
        state["status"] = response["status"]
        save(state_path, state)
        print(f"Notarization: {state['status']}", flush=True)
        if state["status"] == "Accepted":
            break
        if state["status"] in ("Invalid", "Rejected"):
            notary_read(["xcrun", "notarytool", "log", state["submission_id"],
                 str(destination / "notarization-log.json"), "--keychain-profile", profile])
            raise ValueError("Apple rejected the submission; inspect notarization-log.json before retrying")
        if time.monotonic() >= deadline:
            print("Still pending at Apple. Run the same command to resume; no new upload is needed.")
            return False
        time.sleep(min(poll_interval, max(0, deadline - time.monotonic())))
    notary_read(["xcrun", "notarytool", "log", state["submission_id"],
         str(destination / "notarization-log.json"), "--keychain-profile", profile])
    log = json.loads((destination / "notarization-log.json").read_text())
    if log.get("sha256", "").lower() != state["upload_sha256"]:
        raise ValueError("Apple's accepted submission does not match this archive's SHA-256")
    return True


def notarize(destination, state, profile, timeout, poll_interval):
    state_path = destination / "notarization-state.json"
    content = state.get("content_candidate", False)
    app = destination / "work" / (CONTENT_APP_NAME if content and not state.get("content_release") else "Zitter.app")
    upload = destination / "submission.zip"
    run(["codesign", "--verify", "--deep", "--strict", str(app)])
    verify_nested_signatures(app)
    if cdhash(app) != state["cdhash"] or sha256(upload) != state["upload_sha256"]:
        raise ValueError("Prepared app or submission archive changed; use a new destination")
    if not wait_for_acceptance(destination, state, profile, upload, state_path, timeout, poll_interval):
        return False
    run(["xcrun", "stapler", "staple", str(app)])
    run(["xcrun", "stapler", "validate", str(app)])
    run(["codesign", "--verify", "--deep", "--strict", str(app)])
    run(["spctl", "--assess", "--type", "execute", "--verbose=2", str(app)])
    if state.get("production_identity_trial", False):
        prefix = "Zitter-Content-Identity-Trial"
    else:
        prefix = "Zitter-Content-Lab" if content and not state.get("content_release") else "Zitter"
    filename = f"{prefix}-v{state['version']}-macos-{state['architecture']}-notarized.zip"
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
    parser.add_argument("--content-version", help="Prepare an isolated Content Lab candidate at this version")
    parser.add_argument("--third-party-licenses", type=Path,
                        help="Audited license collection for the Content target")
    parser.add_argument("--submit-content-candidate", action="store_true",
                        help="Notarize an existing prepared Content Lab candidate")
    identity_mode = parser.add_mutually_exclusive_group()
    identity_mode.add_argument("--content-release", action="store_true",
                               help="Prepare or submit the production Zitter.app built on Content")
    identity_mode.add_argument("--production-identity-trial", action="store_true",
                        help="Use the production identifier and enable prepared-profile startup on the Content candidate")
    parser.add_argument("--submission-id", help="Recover the matching Apple submission after an interrupted upload")
    parser.add_argument("--prepare-only", action="store_true", help="Sign and archive without contacting Apple")
    parser.add_argument("--timeout", type=int, default=1200, help="Polling time in seconds; rerun to resume after timeout")
    parser.add_argument("--poll-interval", type=int, default=20)
    args = parser.parse_args()
    if args.content_version is not None and (not args.prepare_only or args.submission_id):
        parser.error("--content-version requires --prepare-only")
    if args.submit_content_candidate and (args.prepare_only or args.content_version is not None):
        parser.error("--submit-content-candidate resumes an existing prepared candidate")
    if (args.production_identity_trial or args.content_release) and not (
            args.content_version or args.submit_content_candidate):
        parser.error("A Content identity mode requires --content-version or --submit-content-candidate")
    if (args.third_party_licenses is None) != (args.content_version is None):
        parser.error("--content-version and --third-party-licenses must be supplied together")
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
            content_mode = args.content_version is not None or args.submit_content_candidate
            if (state.get("content_candidate", False) != content_mode
                    or state.get("production_identity_trial", False) != args.production_identity_trial
                    or state.get("content_release", False) != args.content_release):
                raise ValueError("Destination belongs to another preparation mode")
            if args.content_version is not None and state["version"] != args.content_version:
                raise ValueError("Destination belongs to another Content candidate version")
        else:
            if args.submit_content_candidate:
                raise ValueError("No prepared Content candidate at this destination")
            state = prepare(source, destination, signing_identity(args.sign_identity),
                            content_version=args.content_version,
                            third_party_licenses=args.third_party_licenses,
                            production_identity_trial=args.production_identity_trial,
                            content_release=args.content_release)
        if args.submission_id:
            if state.get("submission_id") and state["submission_id"] != args.submission_id:
                raise ValueError("Destination already tracks another submission")
            if state["status"] == "Complete":
                raise ValueError("This notarization is already complete")
            state["submission_id"] = args.submission_id
            save(state_path, state)
        if args.prepare_only:
            name = CONTENT_APP_NAME if args.content_version is not None and not args.content_release else "Zitter.app"
            print(f"Prepared Hardened Runtime app: {destination / 'work' / name}")
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
