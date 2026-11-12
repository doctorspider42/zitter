#!/usr/bin/env python3
"""Verify DPAPI-only migration from portable Chrome-backed Zitter on Windows."""

import argparse
from contextlib import ExitStack
from http import cookies
from http.server import ThreadingHTTPServer
import json
import os
from pathlib import Path
import shutil
from tempfile import TemporaryDirectory
from threading import Thread

from profile_migration_trial import (
    COOKIE_FILES, PREPARED_WORKSPACE_MARKER, WINDOWS_LOCAL_STATE,
    import_workspace_profiles, inventory, rollback,
)
from verify_content_extension_migration import (
    prepare_workspace_trial, run_legacy_profile_probe, seed_legacy_root,
)
from verify_content_login_migration import LoginFixture, encrypted_password
from verify_content_password_forms import require
from verify_content_password_prompt import verify_windows_login_key
from verify_content_storage_migration import cookie_ciphertext, set_legacy_active
from verify_content_workspace_session import ACTIVE_PATHS, MARKER, launch, require_rejection


PROFILES = {"work": "ZitterWork", "personal": "ZitterPersonal"}


class MigrationFixture(LoginFixture):
    def do_GET(self):
        if self.path not in ACTIVE_PATHS:
            super().do_GET()
            return
        role = self.path.split("/")[1]
        jar = cookies.SimpleCookie(self.headers.get("Cookie", ""))
        entry = jar.get("profile_probe")
        received = entry.value if entry else ""
        self.server.requests.append((self.path, received))
        self.send_page(f"""<!doctype html><script>
const expected = {json.dumps(role)};
if ({json.dumps(received)} === expected &&
    !document.cookie.split('; ').some(cookie => cookie.startsWith('profile_probe=')) &&
    localStorage.getItem('profile_probe') === expected) {{
  document.title = 'Zitter workspace probe ' + expected;
}}
</script>""".encode())


def refuse_profile_policy():
    import winreg

    # Chromium's Windows startup applies this policy even with --user-data-dir.
    for hive in (winreg.HKEY_LOCAL_MACHINE, winreg.HKEY_CURRENT_USER):
        for view in (winreg.KEY_WOW64_32KEY, winreg.KEY_WOW64_64KEY):
            for product in ("Chromium", "Zitter"):
                path = rf"SOFTWARE\Policies\{product}"
                try:
                    with winreg.OpenKey(hive, path, access=winreg.KEY_QUERY_VALUE | view) as key:
                        winreg.QueryValueEx(key, "UserDataDir")
                except FileNotFoundError:
                    continue
                raise SystemExit("UserDataDir policy may redirect the disposable legacy profile")


def probe_legacy(node, executable, root, url, action):
    for role, directory in PROFILES.items():
        set_legacy_active(root, role)
        for probe in (f"{action}-storage-{role}",
                      f"{action}-password" + ("-personal" if role == "personal" else "")):
            run_legacy_profile_probe(
                node, executable, root, "-", "-", url, probe, profile_directory=directory,
            )


def encrypted_stores(root, directories):
    return {
        role: (cookie_ciphertext(root / directory),
               encrypted_password(root / directory / "Login Data"))
        for role, directory in directories.items()
    }


def verify_dpapi_migration(legacy, content, node):
    with ExitStack() as resources:
        def scratch(prefix):
            return Path(resources.enter_context(TemporaryDirectory(prefix=prefix))).resolve()

        source = scratch("zitter-content-legacy-")
        trial = scratch("zitter-content-migration-") / "trial"
        content_root = scratch("zitter-content-migration-") / "prepared-workspace"
        prepared_flags = ("--zitter-content-open-prepared-workspace",)
        restored = scratch("zitter-content-legacy-")
        restored.rmdir()
        server = resources.enter_context(ThreadingHTTPServer(("127.0.0.1", 0), MigrationFixture))
        server.requests = []
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()
        resources.callback(thread.join)
        resources.callback(server.shutdown)
        origin = f"http://127.0.0.1:{server.server_port}"
        url = origin + "/legacy/login"

        seed_legacy_root(source, url)
        probe_legacy(node, legacy, source, url, "seed")
        original = encrypted_stores(source, PROFILES)
        if any(work == personal for work, personal in zip(original["work"], original["personal"])):
            raise SystemExit("Expected separate encrypted stores for Work and Personal")
        source_key = json.loads((source / "Local State").read_text(encoding="utf-8"))
        prepare_workspace_trial(source, trial, origin)
        snapshot = inventory(source)
        import_workspace_profiles(trial, content_root, "windows-dpapi")
        require_rejection(launch(content, content_root, origin),
                          "requires a temporary probe root")
        marker = content_root / PREPARED_WORKSPACE_MARKER
        original_marker = marker.read_bytes()
        try:
            marker.unlink()
            require_rejection(launch(content, content_root, origin, prepared_flags),
                              "Invalid prepared Content workspace")
        finally:
            marker.write_bytes(original_marker)
        if (content_root / "WorkspaceLock").exists():
            raise SystemExit("A refused launch locked the prepared workspace")
        key_path = content_root / WINDOWS_LOCAL_STATE
        original_key_file = key_path.read_bytes()
        if json.loads(original_key_file) != {
            "os_crypt": {"encrypted_key": source_key["os_crypt"]["encrypted_key"]},
        }:
            raise SystemExit("Content did not retain the original wrapped DPAPI key")
        content_directories = {role: f"workspace-{role}" for role in PROFILES}
        login_only = scratch("zitter-content-probe-")
        login_only.rmdir()
        import_workspace_profiles(trial, login_only, "windows-dpapi")
        # Exercise the imported Login Data guard independently of cookie decryption.
        for directory in content_directories.values():
            for name in COOKIE_FILES:
                (login_only / directory / "Network" / name).unlink(missing_ok=True)
        server.requests = []
        verify_windows_login_key(content, login_only, origin)
        if server.requests:
            raise SystemExit("Content loaded tabs after refusing the migrated login stores")

        for run in range(2):
            server.requests = []
            result = launch(content, content_root, origin, prepared_flags)
            require(result, MARKER, f"Migrated Workspace launch {run + 1}")
            if server.requests != [("/personal/active", "personal"), ("/work/active", "work")]:
                raise SystemExit("Content did not send the original cookies from both profiles")
            for action in ("check", "check-personal"):
                result = launch(content, content_root, origin, (
                    *prepared_flags,
                    f"--zitter-content-workspace-password-probe={action}",
                ))
                require(result, "Zitter Content workspace password prompt passed", action)
            if encrypted_stores(content_root, content_directories) != original:
                raise SystemExit("Content rewrote the migrated cookie or password ciphertext")
            if key_path.read_bytes() != original_key_file:
                raise SystemExit("Content rewrote the migrated DPAPI key")

        rollback(trial, restored)
        if inventory(restored) != snapshot:
            raise SystemExit("Rollback did not reproduce the legacy snapshot")
        probe_legacy(node, legacy, restored, url, "read")
        if any(inventory(path) != snapshot for path in (source, trial / "baseline", trial / "candidate")):
            raise SystemExit("Migration or rollback changed an input snapshot")
        print("Verified DPAPI migration, profile-isolated storage and autofill, and legacy rollback")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("legacy", type=Path, help="Portable stable Chrome-backed zitter.exe")
    parser.add_argument("content", type=Path, help="Relocated zitter_content.exe")
    parser.add_argument("--node", type=Path, help="Node.js executable with WebSocket support")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("The migration verifier requires native Windows")
    node = args.node or shutil.which("node")
    legacy, content = args.legacy.resolve(), args.content.resolve()
    if (not node or not Path(node).is_file() or legacy == content
            or not legacy.is_file() or not content.is_file()):
        parser.error("Expected two distinct executables and Node.js with WebSocket support")
    refuse_profile_policy()
    verify_dpapi_migration(legacy, content, Path(node).resolve())


if __name__ == "__main__":
    main()
