#!/usr/bin/env python3
"""Verify macOS Login Data migration, autofill, and legacy rollback."""

import argparse
from contextlib import ExitStack, closing
from http.server import ThreadingHTTPServer
import json
from pathlib import Path
import shutil
import sqlite3
import subprocess
from tempfile import TemporaryDirectory
from threading import Thread

from content_file_keychain_fixture import (
    compile_file_keychain_fixture, file_keychain_environment, seal_legacy_copy,
    shared_keychain_environments,
)
from profile_migration_trial import (
    LOGIN_FILES, PASSWORD_PREFS, PASSWORD_SAVING_PREF, PREPARED_WORKSPACE_MARKER,
    copy_verified,
    import_workspace_profiles, inventory, prepare, rollback,
)
from verify_content_extension_migration import run_legacy_profile_probe, seed_legacy_root
from verify_content_password_forms import FormFixture, require
from verify_content_workspace_session import launch, require_rejection


class LoginFixture(FormFixture):
    def credentials(self, case):
        suffix = "-personal" if case == "personal" else ""
        return ("zitter-migration-user" + suffix, "zitter-migration-password" + suffix)

    def do_GET(self):
        if self.path == "/favicon.ico":
            self.send_response(204)
            self.end_headers()
            return
        if self.path != "/legacy/login":
            super().do_GET()
            return
        body = b"""<!doctype html><title>Login migration fixture</title>
<form action="/login" method="post">
  <input id="username" name="username" autocomplete="username"
         style="position:absolute;left:16px;top:18px">
  <input id="password" name="password" type="password" autocomplete="current-password"
         style="position:absolute;left:16px;top:48px">
  <button>Sign in</button>
</form>"""
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def encrypted_password(path):
    with closing(sqlite3.connect(f"{path.as_uri()}?mode=ro", uri=True)) as db:
        rows = db.execute("SELECT password_value FROM logins").fetchall()
    if len(rows) != 1 or not rows[0][0].startswith(b"v10"):
        raise SystemExit("Expected one v10 OSCrypt login in the legacy database")
    return rows[0][0]


def verify_workspace_autofill(content, trial, root, origin, ciphertext, env, shared, prepared):
    layout = "keychain" if env else "mock-keychain"
    import_workspace_profiles(trial, root, layout)
    work = root / f"{layout}-work"
    personal = root / f"{layout}-personal"
    keychain_flags = ("--zitter-content-workspace-legacy-keychain",) if shared else (
        () if env else ("--use-mock-keychain",)
    )
    flags = ("--zitter-content-workspace-keychain", *keychain_flags)
    if prepared:
        require_rejection(launch(content, root, origin, flags, env=env),
                          "requires a private temporary probe root")
        flags += ("--zitter-content-open-prepared-workspace",)
        marker = root / PREPARED_WORKSPACE_MARKER
        original = marker.read_bytes()
        try:
            marker.write_bytes(b"zitter-content-workspace-v1 windows-dpapi\n")
            require_rejection(launch(content, root, origin, flags, env=env),
                              "Invalid prepared Content workspace")
        finally:
            marker.write_bytes(original)
        legacy_state = root.parent / "Local State"
        try:
            legacy_state.write_text("{}", encoding="utf-8")
            require_rejection(launch(content, root, origin, flags, env=env),
                              "Invalid prepared Content workspace")
        finally:
            legacy_state.unlink(missing_ok=True)
        if (root / "WorkspaceLock").exists():
            raise SystemExit("A refused launch locked the prepared workspace")
        print("Content rejected implicit opening, wrong layout, and a legacy ancestor")
    if json.loads((work / PASSWORD_PREFS).read_text(encoding="utf-8")) != {
        PASSWORD_SAVING_PREF: False,
    } or (personal / PASSWORD_PREFS).exists():
        raise SystemExit("The coordinated import changed the profile password saving settings")
    for index in range(2):
        for action in ("check", "check-personal"):
            result = launch(content, root, origin, (
                *flags,
                f"--zitter-content-workspace-password-probe={action}",
            ), env=env)
            require(result, "Zitter Content workspace password prompt passed", action)
            if shared and "rejected a write attempt" in result.stdout:
                raise SystemExit("Content attempted to write the legacy Keychain entry")
        for profile, original in zip((work, personal), ciphertext):
            if encrypted_password(profile / "Login Data") != original:
                raise SystemExit(f"Content changed login ciphertext after launch {index + 1}")
    print("Content autofilled distinct imported Work and Personal credentials after restart")


def verify_refused_legacy_login_import(content, trial, scratch, origin, env):
    for failure in ("missing", "denied", "damaged"):
        root = scratch("zitter-content-probe-")
        root.rmdir()
        import_workspace_profiles(trial, root, "keychain")
        if failure == "damaged":
            database = root / "keychain-personal" / "Login Data"
            with closing(sqlite3.connect(database)) as db, db:
                db.execute("UPDATE logins SET password_value = ?", (b"v10damaged",))
            failure_env = env
        else:
            failure_env = dict(env, ZITTER_CONTENT_TEST_KEYCHAIN_LOOKUP_FAILURE=failure)
        before = {name: inventory(root / name)
                  for name in ("keychain-work", "keychain-personal")}
        session = root / "ContentKeychainWorkspaceSession.json"
        session_before = session.read_bytes() if session.is_file() else None
        result = launch(content, root, origin, (
            "--zitter-content-workspace-keychain", "--zitter-content-workspace-legacy-keychain",
        ), env=failure_env)
        if (result.returncode != 1
                or "could not verify legacy encrypted stores" not in result.stderr
                or "rejected a write attempt" in result.stdout):
            raise SystemExit(f"Legacy login migration failed to reject {failure} safely:\n"
                             f"{result.stdout}\n{result.stderr}")
        if any(inventory(root / name) != entries for name, entries in before.items()):
            raise SystemExit(f"Legacy login migration changed a profile after {failure} refusal")
        session_after = session.read_bytes() if session.is_file() else None
        if session_after != session_before:
            raise SystemExit(f"Legacy login migration changed the session after {failure} refusal")
    print("Content rejected missing and denied keys and damaged Login Data before changing profiles")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("legacy_app", type=Path, help="Local legacy Zitter.app")
    parser.add_argument("login_verifier", type=Path, help="Local verify_login_database binary")
    parser.add_argument("--content-app", type=Path,
                        help="also exercise the coordinated import in Zitter Content Lab.app")
    parser.add_argument("--prepared-root", action="store_true",
                        help="open the imported workspace outside the direct temp-probe layout")
    keychain = parser.add_mutually_exclusive_group()
    keychain.add_argument("--file-keychain", action="store_true",
                          help="Use a private macOS Keychain file in the Content Workspace")
    keychain.add_argument("--shared-keychain", action="store_true",
                          help="Use one private file Keychain in both the legacy and Content apps")
    options = parser.parse_args()
    if (options.file_keychain or options.shared_keychain) and not options.content_app:
        parser.error("File Keychain probes require --content-app")
    if options.prepared_root and not options.content_app:
        parser.error("--prepared-root requires --content-app")
    legacy = (options.legacy_app / "Contents/MacOS/Chromium").resolve()
    content = options.login_verifier.resolve()
    node = shutil.which("node")
    if not legacy.is_file() or not content.is_file() or not node:
        parser.error("Expected the legacy macOS app, the Content verifier, and Node.js")
    workspace = None
    if options.content_app:
        workspace = (options.content_app / "Contents/MacOS/Zitter Content Lab").resolve()
        if not workspace.is_file():
            parser.error(f"Expected the Content workspace executable at {workspace}")

    with ExitStack() as resources:
        def scratch(prefix):
            return Path(resources.enter_context(TemporaryDirectory(prefix=prefix))).resolve()

        source = scratch("zitter-content-legacy-")
        restored = scratch("zitter-content-legacy-")
        restored.rmdir()
        candidate = scratch("zitter-content-login-copy-")
        trial = scratch("zitter-content-login-migration-") / "trial"
        legacy_env = workspace_env = None
        if options.file_keychain or options.shared_keychain:
            library = compile_file_keychain_fixture(scratch("zitter-content-keychain-library-"))
            if options.shared_keychain:
                legacy = seal_legacy_copy(options.legacy_app, scratch("zitter-content-legacy-app-"))
                workspace_env, legacy_env = shared_keychain_environments(
                    library, scratch("zitter-content-shared-keychain-"), workspace, verifier=content,
                )
            else:
                workspace_env = file_keychain_environment(library, seed_legacy_key=True)
        server = resources.enter_context(ThreadingHTTPServer(("127.0.0.1", 0), LoginFixture))
        origin = f"http://127.0.0.1:{server.server_port}"
        work_url = origin + "/legacy/login"
        seed_legacy_root(source, work_url)
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            run_legacy_profile_probe(node, legacy, source, "-", "-", work_url, "seed-password", env=legacy_env)
            relative = Path("ZitterWork", "Login Data")
            original_ciphertext = encrypted_password(source / relative)
            if b"zitter-migration-password" in original_ciphertext:
                raise SystemExit("The old browser stored the synthetic password in plaintext")
            if workspace:
                personal = scratch("zitter-content-legacy-")
                seed_legacy_root(personal, work_url)
                run_legacy_profile_probe(
                    node, legacy, personal, "-", "-", work_url, "seed-password-personal",
                    env=legacy_env,
                )
                for name in LOGIN_FILES:
                    saved = personal / "ZitterWork" / name
                    if saved.is_file():
                        shutil.copy2(saved, source / "ZitterPersonal" / name)
                personal_ciphertext = encrypted_password(source / "ZitterPersonal/Login Data")
                if original_ciphertext == personal_ciphertext:
                    raise SystemExit("Expected distinct legacy profile credentials")
                run_legacy_profile_probe(
                    node, legacy, source, "-", "-", work_url, "disable-password-saving",
                    env=legacy_env,
                )
            source_snapshot = inventory(source)
            if prepare(source, trial) != 2:
                raise SystemExit("The legacy snapshot lost a workspace profile")
            candidate_work = trial / "candidate" / "ZitterWork"
            copy_verified(candidate_work, candidate, inventory(candidate_work))
            candidate.chmod(0o700)
            command = [str(content), f"--legacy-copy-root={candidate}", f"--legacy-origin={origin}/"]
            if options.shared_keychain:
                command.append("--file-keychain")
            subprocess.run(command, check=True, timeout=35,
                           env=workspace_env if options.shared_keychain else None)
            if encrypted_password(candidate / "Login Data") != original_ciphertext:
                raise SystemExit("Content altered the password ciphertext")
            if workspace:
                if options.prepared_root:
                    workspace_root = scratch("zitter-content-migration-") / "prepared-workspace"
                else:
                    workspace_root = scratch("zitter-content-probe-")
                    workspace_root.rmdir()
                verify_workspace_autofill(workspace, trial, workspace_root, origin,
                                          (original_ciphertext, personal_ciphertext), workspace_env,
                                          options.shared_keychain, options.prepared_root)
                if options.shared_keychain:
                    verify_refused_legacy_login_import(workspace, trial, scratch, origin,
                                                       workspace_env)

            rollback(trial, restored)
            if inventory(restored) != source_snapshot:
                raise SystemExit("Rollback did not reproduce the legacy snapshot")
            run_legacy_profile_probe(node, legacy, restored, "-", "-", work_url, "read-password", env=legacy_env)
            if inventory(source) != source_snapshot or inventory(trial / "candidate") != source_snapshot:
                raise SystemExit("The original source or offline candidate was modified")
            print("Content read legacy Login Data and the old password manager listed the restored credential")
        finally:
            server.shutdown()
            thread.join()


if __name__ == "__main__":
    main()
