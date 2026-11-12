#!/usr/bin/env python3
"""Check argument-free Content startup and exclusive ownership of a fresh root."""

import argparse
import errno
import fcntl
import json
import os
from pathlib import Path
import plistlib
import subprocess
import sys
from tempfile import TemporaryDirectory, TemporaryFile
import time

from content_file_keychain_fixture import compile_file_keychain_fixture, file_keychain_environment
from notarize_app import verify_nested_signatures
from package_app import BUNDLE_ID
from verify_content_bundle import copy_dmg_app, loader_environment, seal_local_copy, verify_signature


APP_NAME = "Zitter Content Lab.app"
LAYOUT = "zitter-content-workspace-v1 fresh-keychain\n"
KEYCHAIN_ITEM = ("-s", "Zitter Content Lab Safe Storage", "-a", "Zitter Content Lab")
REQUEST_QUIT = """
function run(args) {
    ObjC.import("AppKit");
    const application = $.NSRunningApplication.runningApplicationWithProcessIdentifier(Number(args[0]));
    for (let attempt = 0; attempt < 100 && !application.finishedLaunching; ++attempt) {
        $.NSThread.sleepForTimeInterval(0.05);
    }
    if (!application.finishedLaunching || !application.terminate) {
        throw new Error("AppKit could not request quit for the candidate process");
    }
}
"""


def startup_holds_lock(path):
    try:
        with path.open("r+b") as lock:
            try:
                fcntl.lockf(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except OSError as error:
                if error.errno in (errno.EACCES, errno.EAGAIN):
                    return True
                raise
    except FileNotFoundError:
        return False
    return False


def system_keychain_item_exists():
    result = subprocess.run(
        ["/usr/bin/security", "find-generic-password", *KEYCHAIN_ITEM],
        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=15,
    )
    if result.returncode == 0:
        return True
    if result.returncode == (-25300 & 0xff):  # errSecItemNotFound
        return False
    raise ValueError(f"Keychain lookup failed: {result.stderr.decode(errors='replace')}")


def unused_system_profile_root():
    uid = os.getuid()
    if uid == 0 or os.geteuid() != uid or Path("/dev/console").stat().st_uid != uid:
        raise SystemExit("Run the system Keychain test from the disposable account's macOS desktop session")
    root = Path.home() / "Library/Application Support/Zitter Content"
    if root.exists() or root.is_symlink() or system_keychain_item_exists():
        raise SystemExit("Use a disposable macOS account with no Content profile or Safe Storage item")
    return root


def verify_startup(executable, root, env, *, label="Fresh startup"):
    marker = root / "ContentWorkspaceLayout"
    lock = root / "WorkspaceLock"
    roles = ("work", "personal")
    directories = tuple(root / f"keychain-{role}/Network" for role in roles)
    session_path = root / "ContentKeychainWorkspaceSession.json"
    expected_active = None
    for attempt in (1, 2):
        with TemporaryFile() as log:
            process = subprocess.Popen([executable], env=env, stdout=log, stderr=log)
            try:
                deadline = time.monotonic() + 15
                while process.poll() is None and time.monotonic() < deadline:
                    if all(path.is_dir() for path in directories) and startup_holds_lock(lock):
                        break
                    time.sleep(0.05)
                else:
                    log.seek(0)
                    raise ValueError(f"{label} {attempt} did not acquire the profile lock:\n"
                                     f"{log.read().decode(errors='replace')}")
                if not marker.is_file() or marker.read_text(encoding="ascii") != LAYOUT:
                    log.seek(0)
                    raise ValueError(f"{label} {attempt} did not open both profiles:\n"
                                     f"{log.read().decode(errors='replace')}")
                competing = subprocess.run([executable], env=env, capture_output=True, timeout=15)
                if (process.poll() is not None or competing.returncode != 1
                        or b"Could not lock" not in competing.stderr):
                    raise ValueError(f"{label} {attempt} did not enforce exclusive ownership:\n"
                                     f"{competing.stderr.decode(errors='replace')}")
                subprocess.run(
                    ["/usr/bin/osascript", "-l", "JavaScript", "-e", REQUEST_QUIT, str(process.pid)],
                    check=True, timeout=15,
                )
                if process.wait(timeout=15) != 0:
                    log.seek(0)
                    raise ValueError(f"{label} {attempt} failed to quit cleanly:\n"
                                     f"{log.read().decode(errors='replace')}")
                session = json.loads(session_path.read_text(encoding="utf-8"))
                active = session.get("active_workspace")
                if (active not in roles or any(
                        session.get(role, {}).get("profile_id") != role for role in roles)):
                    raise ValueError(f"{label} did not save Work and Personal")
                if expected_active is not None and active != expected_active:
                    raise ValueError(f"{label} did not restore the saved active workspace")
                print(f"{label} {attempt} held the exclusive lock and saved Work and Personal")
                if attempt == 1:
                    expected_active = next(role for role in roles if role != active)
                    session["active_workspace"] = expected_active
                    session_path.write_text(json.dumps(session), encoding="utf-8")
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)


def verify_network_dictionaries(root):
    for role in ("work", "personal"):
        profile = root / f"keychain-{role}"
        store = profile / "Network/Shared Dictionary"
        if (not (store / "db").is_file() or not (store / "cache").is_dir()
                or (profile / "Shared Dictionary").exists()):
            raise ValueError(f"The {role} Shared Dictionary escaped its network directory or failed to open")
    print("Both Shared Dictionary stores initialized within their profile network directories")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("candidate", type=Path, help="Production-identity trial app or signed DMG")
    parser.add_argument("--content-release", action="store_true",
                        help="Read Zitter.app from a production Content DMG")
    parser.add_argument("--network-dictionaries", action="store_true",
                        help="Check the persistent Shared Dictionary stores inside the network sandbox roots")
    parser.add_argument("--system-keychain", action="store_true",
                        help="Use the default profile root and account Keychain in a disposable macOS account")
    parser.add_argument("--scratch", type=Path,
                        help="Existing local directory for staging the candidate bundle")
    args = parser.parse_args()
    if sys.platform != "darwin":
        parser.error("This verifier requires macOS")
    candidate = args.candidate.resolve(strict=True)
    system_root = unused_system_profile_root() if args.system_keychain else None
    with (TemporaryDirectory(prefix="zitter-fresh-candidate-", dir=args.scratch) as staging,
          TemporaryDirectory(prefix="zitter-content-probe-") as fixture):
        staging, fixture = Path(staging).resolve(), Path(fixture).resolve()
        app = staging / ("Zitter.app" if args.content_release else APP_NAME)
        if candidate.is_file() and candidate.suffix == ".dmg":
            if args.system_keychain:
                subprocess.run(["/usr/bin/xcrun", "stapler", "validate", candidate],
                               check=True, timeout=60)
            copy_dmg_app(candidate, staging / "mounted", app, app_name=app.name)
        else:
            subprocess.run(["/usr/bin/ditto", candidate, app], check=True, timeout=90)
        with (app / "Contents/Info.plist").open("rb") as file:
            info = plistlib.load(file)
        if info.get("CFBundleIdentifier") != BUNDLE_ID or info.get("ZitterUsePreparedWorkspace") is not True:
            raise SystemExit("Expected a production-identity candidate with signed ordinary startup enabled")
        executable = app / "Contents/MacOS/Zitter Content Lab"
        if args.system_keychain:
            root = system_root
            verify_nested_signatures(app)
            subprocess.run(["/usr/bin/xcrun", "stapler", "validate", app], check=True, timeout=60)
            env = loader_environment()
            env.pop("ZITTER_CONTENT_STARTUP_TRIAL_ROOT", None)
        else:
            root = fixture
            if args.content_release:
                # The fixture needs injection in the browser only. Retain the
                # release signatures on the framework and subprocess helpers.
                verify_signature(app)
                subprocess.run(["/usr/bin/codesign", "--force", "--sign", "-", app],
                               check=True, timeout=90)
            else:
                seal_local_copy(app)
            library = compile_file_keychain_fixture(staging)
            env = dict(file_keychain_environment(library), ZITTER_CONTENT_STARTUP_TRIAL_ROOT=str(root))
            rejected = subprocess.run([executable], env=dict(loader_environment(),
                                      ZITTER_CONTENT_STARTUP_TRIAL_ROOT=str(root)),
                                      capture_output=True, timeout=15)
            if (rejected.returncode != 1 or b"Invalid isolated Content startup trial" not in rejected.stderr
                    or any(root.iterdir())):
                raise SystemExit("The argument-free trial accepted a profile override without its Keychain fixture")
        verify_startup(executable, root, env)
        if args.network_dictionaries:
            verify_network_dictionaries(root)
        verify_signature(app)
        if args.system_keychain:
            if not system_keychain_item_exists():
                raise ValueError("The signed app did not create its system Keychain item")
            print(f"System Keychain run retained its test profiles at {root}")
        elif not (root / "fixture.keychain-db").is_file():
            raise SystemExit("The fresh startup did not create its private Keychain file")


if __name__ == "__main__":
    main()
