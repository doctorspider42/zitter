#!/usr/bin/env python3
"""Check ordinary macOS startup import, dismissal, and restart in an isolated root."""

import argparse
import json
from pathlib import Path
import plistlib
import subprocess
from tempfile import TemporaryDirectory

from content_file_keychain_fixture import compile_file_keychain_fixture, file_keychain_environment
from package_app import BUNDLE_ID

MARKER = "Zitter Content startup import probe passed"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path)
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
        for phase in ("keep", "skip", "reopen"):
            result = subprocess.run(
                [executable], env=dict(env, ZITTER_CONTENT_STARTUP_IMPORT_PROBE=phase),
                capture_output=True, text=True, timeout=30,
            )
            if result.returncode or MARKER not in result.stdout.splitlines():
                raise SystemExit(f"Startup import {phase} failed.\n{result.stdout}\n{result.stderr}")
            session = json.loads((root / "ContentKeychainWorkspaceSession.json").read_text())
            pending = True if phase == "keep" else None
            if (session.get("startup_import_pending") is not pending
                    or any(session.get(role, {}).get("profile_id") != role
                           for role in ("work", "personal"))):
                raise SystemExit(f"Unexpected saved Workspace after startup import {phase}")
        if not (root / "fixture.keychain-db").is_file():
            raise SystemExit("The startup trial did not use its isolated Keychain")
    print("Content startup import survived a restart and stayed dismissed after skipping")


if __name__ == "__main__":
    main()
