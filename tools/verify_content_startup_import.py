#!/usr/bin/env python3
"""Check ordinary macOS startup import, dismissal, and restart in an isolated root."""

import argparse
import json
from pathlib import Path
import plistlib
import sqlite3
import subprocess
from tempfile import TemporaryDirectory

from content_file_keychain_fixture import compile_file_keychain_fixture, file_keychain_environment
from package_app import BUNDLE_ID
from verify_content_workspace_session import seed_browser_import

MARKER = "Zitter Content startup import probe passed"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path)
    parser.add_argument("--theme", choices=("light", "dark"), default="light")
    parser.add_argument("--renderer", choices=("compositor", "metal"), default="compositor")
    args = parser.parse_args()
    source = args.app.resolve(strict=True)
    with (TemporaryDirectory(prefix="zitter-content-startup-import-") as trial,
          TemporaryDirectory(prefix="zitter-content-probe-") as profile):
        app = Path(trial) / "Zitter.app"
        root = Path(profile).resolve()
        subprocess.run(["/bin/cp", "-cRp", source, app], check=True, timeout=90)
        info_path = app / "Contents/Info.plist"
        info = plistlib.loads(info_path.read_bytes())
        info.update(CFBundleIdentifier=BUNDLE_ID, ZitterUsePreparedWorkspace=True)
        info_path.write_bytes(plistlib.dumps(info))
        subprocess.run(["/usr/bin/codesign", "--force", "--sign", "-", app],
                       check=True, timeout=90)
        library = compile_file_keychain_fixture(Path(trial))
        env = dict(file_keychain_environment(library), ZITTER_CONTENT_STARTUP_TRIAL_ROOT=str(root))
        executable = app / "Contents/MacOS/Zitter Content Lab"
        env.update(ZITTER_CONTENT_STARTUP_IMPORT_THEME=args.theme,
                   ZITTER_CONTENT_STARTUP_IMPORT_RENDERER=args.renderer)
        session_path = root / "ContentKeychainWorkspaceSession.json"

        def run(phase):
            result = subprocess.run(
                [executable], env=dict(env, ZITTER_CONTENT_STARTUP_IMPORT_PROBE=phase),
                capture_output=True, text=True, timeout=45,
            )
            if result.returncode or MARKER not in result.stdout.splitlines():
                raise SystemExit(f"Startup import {phase} failed.\n{result.stdout}\n{result.stderr}")
            session = json.loads(session_path.read_text())
            pending = True if phase == "keep" else None
            if (session.get("startup_import_pending") is not pending
                    or any(session.get(role, {}).get("profile_id") != role
                           for role in ("work", "personal"))):
                raise SystemExit(f"Unexpected saved Workspace after startup import {phase}")
            return session

        for phase in ("keep", "skip", "reopen"):
            session = run(phase)

        # Reopen the onboarding state with Night Shift configured. Publishing
        # imported profiles writes its settings to their extension stores.
        session.update(startup_import_pending=True, night_shift={"enabled": True})
        session_path.write_text(json.dumps(session))
        source = seed_browser_import(root)
        original = {path: path.read_bytes() for path in source.glob("*/*")}
        for phase in ("apply", "reopen"):
            session = run(phase)
            if len(session.get("extra_profiles", [])) != 2 or len(session.get("extra_workspaces", [])) != 2:
                raise SystemExit(f"Imported profiles and spaces did not persist after {phase}")
        if original != {path: path.read_bytes() for path in source.glob("*/*")}:
            raise SystemExit("Startup import modified the source browser data")
        session["startup_import_pending"] = True
        session_path.write_text(json.dumps(session))
        (root / "PasswordsFixture.csv").write_text(
            "name,url,username,password\n"
            "valid,https://login.example.test,fixture,fixture-password\n"
            "duplicate,https://login.example.test,fixture,replacement-password\n"
            "invalid,file:///tmp/login,invalid,fixture-password\n"
        )
        run("csv")
        with sqlite3.connect(f"file:{root / 'keychain-work/Login Data'}?mode=ro", uri=True) as db:
            rows = db.execute("SELECT username_value, password_value FROM logins").fetchall()
        if (len(rows) != 1 or rows[0][0] != "fixture" or not rows[0][1]
                or b"fixture-password" in rows[0][1] or b"replacement-password" in rows[0][1]):
            raise SystemExit("Password CSV import did not preserve a single encrypted login")
        if not (root / "fixture.keychain-db").is_file():
            raise SystemExit("The startup trial did not use its isolated Keychain")
    print("Content startup import verified dismissal, persisted browser profiles, and sandboxed password CSV import")


if __name__ == "__main__":
    main()
