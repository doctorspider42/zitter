#!/usr/bin/env python3
"""Rehearse replacing a macOS Content bundle over disposable profiles."""

import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import plistlib
import secrets
import sqlite3
import subprocess
from tempfile import TemporaryDirectory, TemporaryFile
from threading import Event, Thread

from content_file_keychain_fixture import compile_file_keychain_fixture, file_keychain_environment
from package_app import BUNDLE_ID
from notarize_app import verify_nested_signatures
from verify_content_bundle import copy_dmg_app, loader_environment, verify_signature
from verify_content_fresh_startup import (
    REQUEST_QUIT, system_keychain_item_exists, unused_system_profile_root,
    verify_network_dictionaries, verify_startup,
)

ROLES = ("work", "personal")


class ProfileFixture(BaseHTTPRequestHandler):
    def do_GET(self):
        self.server.requests.append(self.path)
        role = self.path.removeprefix("/profile/")
        if self.path.startswith("/profile/") and role in ROLES:
            value = json.dumps(self.server.values[role])
            initialize = """
                localStorage.setItem('upgrade', expected);
                document.cookie = 'upgrade=' + expected + '; Path=/; Max-Age=3600';
            """ if self.server.seed else ""
            body = f"""<!doctype html><title>{role} upgrade fixture</title><script>
                const expected = {value};
                {initialize}
                const cookie = document.cookie.split('; ').includes('upgrade=' + expected);
                const stored = localStorage.getItem('upgrade') === expected;
                fetch('/result/{role}/' + (cookie && stored ? 'passed' : 'failed'));
            </script>""".encode()
        elif self.path.startswith("/result/"):
            self.server.result = self.path
            self.server.completed.set()
            body = b""
        else:
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Type", "text/html")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, _format, *args):
        pass


def run_profile(app, env, server, role, *, seed):
    server.seed = seed
    server.result = None
    server.requests = []
    server.completed.clear()
    with TemporaryFile() as log:
        process = subprocess.Popen([app / "Contents/MacOS/Zitter Content Lab"],
                                   env=env, stdout=log, stderr=log)
        try:
            if not server.completed.wait(20) or server.result != f"/result/{role}/passed":
                log.seek(0)
                raise ValueError(f"{role} fixture failed: {server.result}, requests={server.requests!r}\n"
                                 f"{log.read().decode(errors='replace')}")
            subprocess.run(
                ["/usr/bin/osascript", "-l", "JavaScript", "-e", REQUEST_QUIT, str(process.pid)],
                check=True, timeout=15,
            )
            if process.wait(timeout=15) != 0:
                raise ValueError(f"{role} did not quit cleanly")
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=5)


def encrypted_cookies(root, values):
    ciphertext = {}
    for role in ROLES:
        path = root / f"keychain-{role}/Network/Cookies"
        with sqlite3.connect(path.as_uri() + "?mode=ro", uri=True) as db:
            rows = db.execute(
                "SELECT value, encrypted_value FROM cookies WHERE name = 'upgrade'"
            ).fetchall()
        if len(rows) != 1 or rows[0][0] or not rows[0][1] or values[role].encode() in rows[0][1]:
            raise ValueError(f"{role} did not persist an encrypted fixture cookie")
        ciphertext[role] = rows[0][1]
    if ciphertext["work"] == ciphertext["personal"]:
        raise ValueError("Profile cookie ciphertext unexpectedly matches")
    return ciphertext


def stage(source, destination, *, version=None, preserve_signature=False):
    if source.is_file() and source.suffix == ".dmg":
        if preserve_signature:
            subprocess.run(["/usr/bin/xcrun", "stapler", "validate", source],
                           check=True, timeout=60)
        mount = destination.with_suffix(".mount")
        copy_dmg_app(source, mount, destination, app_name="Zitter.app")
        mount.rmdir()
    else:
        subprocess.run(["/bin/cp", "-cRp", source, destination], check=True, timeout=90)
    path = destination / "Contents/Info.plist"
    info = plistlib.loads(path.read_bytes())
    if version:
        info.update(CFBundleIdentifier=BUNDLE_ID, ZitterUsePreparedWorkspace=True,
                    CFBundleVersion=version, CFBundleShortVersionString=version)
        path.write_bytes(plistlib.dumps(info))
    if info.get("CFBundleIdentifier") != BUNDLE_ID or info.get("ZitterUsePreparedWorkspace") is not True:
        raise ValueError("The app must support ordinary startup under the production identifier")
    if preserve_signature:
        verify_nested_signatures(destination)
        subprocess.run(["/usr/bin/xcrun", "stapler", "validate", destination],
                       check=True, timeout=60)
    else:
        # Signing only the outer bundle preserves the already sealed nested code.
        subprocess.run(["/usr/bin/codesign", "--force", "--sign", "-", destination],
                       check=True, timeout=90)
        verify_signature(destination)
    return info["CFBundleShortVersionString"]


def designated_requirement(app):
    result = subprocess.run(
        ["/usr/bin/codesign", "-d", "-r-", app], capture_output=True,
        text=True, check=True, timeout=15,
    )
    for line in (result.stdout + result.stderr).splitlines():
        if line.startswith("designated => "):
            return line
    raise ValueError(f"Missing designated requirement for {app}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("previous", type=Path, help="Earlier Content app or release DMG")
    parser.add_argument("replacement", type=Path, help="Newer Content app or release DMG")
    parser.add_argument("--version", help="Version for the ad hoc replacement fixture")
    parser.add_argument("--system-keychain", action="store_true",
                        help="Use two stapled apps in a disposable macOS account")
    parser.add_argument("--validate-only", action="store_true",
                        help="Validate both signed candidates without launching or opening the Keychain")
    parser.add_argument("--scratch", type=Path,
                        help="Existing directory for staging the app bundles")
    parser.add_argument("--network-dictionaries", action="store_true",
                        help="Check the profile network sandbox directories before and after replacement")
    args = parser.parse_args()
    preserve_signature = args.system_keychain or args.validate_only
    if preserve_signature == bool(args.version) or (args.system_keychain and args.validate_only):
        parser.error("Choose one of --version, --system-keychain, or --validate-only")
    system_root = unused_system_profile_root() if args.system_keychain else None
    with (TemporaryDirectory(prefix="zitter-content-upgrade-", dir=args.scratch) as work,
          TemporaryDirectory(prefix="zitter-content-probe-") as profile,
          ThreadingHTTPServer(("127.0.0.1", 0), ProfileFixture) as server):
        work, root = Path(work).resolve(), Path(profile).resolve()
        app = work / "Zitter.app"
        next_app = work / "Replacement.app"
        before = stage(args.previous.resolve(strict=True), app,
                       preserve_signature=preserve_signature)
        after = stage(args.replacement.resolve(strict=True), next_app, version=args.version,
                      preserve_signature=preserve_signature)
        versions = [tuple(map(int, version.split("."))) for version in (before, after)]
        if versions[1] <= versions[0]:
            parser.error("The replacement version must be newer")
        if preserve_signature:
            if designated_requirement(app) != designated_requirement(next_app):
                raise ValueError("The signed apps have different designated requirements")
        if args.validate_only:
            print(f"Validated signed Content replacement {before} -> {after}")
            return
        if args.system_keychain:
            root = system_root
            env = loader_environment()
            env.pop("ZITTER_CONTENT_STARTUP_TRIAL_ROOT", None)
        else:
            library = compile_file_keychain_fixture(work)
            env = dict(file_keychain_environment(library), ZITTER_CONTENT_STARTUP_TRIAL_ROOT=str(root))
            env["ZITTER_CONTENT_TEST_KEYCHAIN_UPGRADE_READER"] = str(next_app)
        verify_startup(app / "Contents/MacOS/Zitter Content Lab", root, env)
        if args.network_dictionaries:
            verify_network_dictionaries(root)
        if args.system_keychain and not system_keychain_item_exists():
            raise ValueError("The earlier signed app did not create its system Keychain item")
        session_path = root / "ContentKeychainWorkspaceSession.json"
        server.completed = Event()
        server.values = {role: secrets.token_hex(16) for role in ROLES}
        worker = Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            for role in ROLES:
                session = json.loads(session_path.read_text())
                session["active_workspace"] = role
                session[role].update(active=0, tabs=[{
                    "url": f"http://127.0.0.1:{server.server_port}/profile/{role}",
                    "title": f"{role} upgrade fixture",
                }])
                session_path.write_text(json.dumps(session))
                run_profile(app, env, server, role, seed=True)
            cookies = encrypted_cookies(root, server.values)
            original_session = json.loads(session_path.read_text())
            app.rename(work / "Previous.app")
            next_app.rename(app)
            # Let the newer build restore the previous application's session as saved.
            run_profile(app, env, server, "personal", seed=False)
            session = json.loads(session_path.read_text())
            for role in ROLES:
                if session[role]["tabs"] != original_session[role]["tabs"]:
                    raise ValueError(f"The replacement changed {role} tabs")
            session["active_workspace"] = "work"
            session_path.write_text(json.dumps(session))
            run_profile(app, env, server, "work", seed=False)
            verify_startup(app / "Contents/MacOS/Zitter Content Lab", root, env,
                           label="Replacement startup")
            if args.network_dictionaries:
                verify_network_dictionaries(root)
            if encrypted_cookies(root, server.values) != cookies:
                raise ValueError("The replacement rewrote the encrypted fixture cookies")
            if args.system_keychain:
                if not system_keychain_item_exists():
                    raise ValueError("The upgrade did not retain a system Keychain item")
                print(f"System Keychain run retained its test profiles at {root}")
            print(f"Content replacement {before} -> {after} restored both profiles and encrypted cookies")
        finally:
            server.shutdown()
            worker.join()


if __name__ == "__main__":
    main()
