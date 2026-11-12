#!/usr/bin/env python3
"""Verify the workspace password prompt against a disposable loopback login."""

import argparse
from contextlib import ExitStack, closing
from http.server import ThreadingHTTPServer
import json
import os
from pathlib import Path
import secrets
import shutil
import sqlite3
import subprocess
from tempfile import TemporaryDirectory
from threading import Thread
from urllib.parse import parse_qs

from profile_migration_trial import LOGIN_FILES, inventory, prepare, rollback
from verify_content_extension_migration import run_legacy_profile_probe, seed_legacy_root
from verify_content_password_forms import FormFixture, require
from verify_content_profiles import ENCRYPTED_PREFIX, KEY_FILE
from verify_content_workspace_session import (
    PROFILES, SESSION_FILE, WINDOWS_LOCAL_STATE, WorkspaceFixture, launch,
    new_wrapped_windows_key, seed_session,
    require_rejection,
)


USERNAME = "zitter-profile-probe"
PREF = "credentials_enable_service"
PREFS_FILE = "Content Password Preferences"
PROMPT_CASES = ("save", "update", "cancel", "navigate", "hide", "check")
SECRETS = {
    "save": "zitter-work-original-secret",
    "update": "zitter-work-probe-secret",
    "cancel": "zitter-work-cancelled-secret",
    "navigate": "zitter-work-navigated-secret",
    "hide": "zitter-work-hidden-secret",
    "disable": "zitter-work-disabled-secret",
    "off": "zitter-work-suppressed-secret",
    "personal": "zitter-personal-cancelled-secret",
    "unavailable": "zitter-work-unavailable-secret",
}
MARKER = "Zitter Content workspace password prompt passed"


class PasswordFixture(FormFixture):
    def credentials(self, case):
        if case == "work" and getattr(self.server, "expect_original_password", False):
            return USERNAME, SECRETS["save"]
        return super().credentials(case)

    def do_GET(self):
        if self.path == "/legacy/work":
            self.send_page(b"<!doctype html><title>Legacy Work fixture</title><p>Work</p>")
            return
        if self.path == "/password-save/replacement":
            self.send_page(b"<!doctype html><title>Zitter password replacement page</title>")
            return
        action = self.path.removeprefix("/password-save/")
        if action not in SECRETS:
            super().do_GET()
            return
        self.send_page(f"""<!doctype html><meta charset=utf-8>
<form method="post" action="/password-save/done">
  <input name="password" type="password" autocomplete="current-password"
         value="{SECRETS[action][:-1]}"
         style="position:absolute;left:10px;top:8px;height:32px">
  <p style="margin-top:60px">
    <input name="username" autocomplete="username" value="{USERNAME}">
    <input type="hidden" name="case" value="{action}">
  </p>
</form>
<script>
  const field = document.querySelector('[type=password]');
  field.addEventListener('click', () => {{
    field.setSelectionRange(field.value.length, field.value.length);
  }});
  field.addEventListener('input', event => {{
    if (event.isTrusted && field.value.endsWith('t'))
      setTimeout(() => field.form.requestSubmit(), 120);
  }});
  document.title = 'Zitter password form ready';
</script>""".encode())

    def do_POST(self):
        if self.path != "/password-save/done":
            self.send_error(404)
            return
        size = int(self.headers.get("Content-Length", 0))
        if not 0 < size <= 4096:
            self.send_error(400)
            return
        fields = parse_qs(self.rfile.read(size).decode())
        action, = fields.get("case", [""])
        if (action not in SECRETS or fields.get("username") != [USERNAME]
                or fields.get("password") != [SECRETS[action]]):
            self.send_error(400)
            return
        self.send_page(b"""<!doctype html><title>Zitter password form submitted</title>
<p>Signed in.</p>""")


def read_ciphertext(root, prefix):
    database = root / "workspace-work" / "Login Data"
    with closing(sqlite3.connect(f"{database.as_uri()}?mode=ro", uri=True)) as db:
        rows = db.execute(
            "SELECT username_value, password_value FROM logins WHERE blacklisted_by_user=0"
        ).fetchall()
    if len(rows) != 1 or rows[0][0] != USERNAME:
        raise SystemExit("The workspace must have exactly one saved credential")
    ciphertext = rows[0][1]
    if (not isinstance(ciphertext, bytes) or not ciphertext.startswith(prefix)
            or any(secret.encode() in ciphertext for secret in SECRETS.values())):
        raise SystemExit("The workspace stored unexpected password bytes")
    return ciphertext


def verify_windows_login_key(executable, root, origin):
    state = root / WINDOWS_LOCAL_STATE
    original = state.read_bytes()
    protected = [root / SESSION_FILE]
    protected.extend(root / profile / name for profile in PROFILES for name in LOGIN_FILES)
    before = {path: path.read_bytes() if path.exists() else None for path in protected}
    invalid = json.loads(original)
    invalid["os_crypt"]["encrypted_key"] = new_wrapped_windows_key()
    wrong_key = json.dumps(invalid).encode("utf-8")
    state.write_bytes(wrong_key)
    try:
        require_rejection(launch(executable, root, origin, ["--enable-logging=stderr"]),
                          "could not verify its login stores")
        if state.read_bytes() != wrong_key:
            raise SystemExit("A refused Windows launch replaced the DPAPI key")
        if any((path.read_bytes() if path.exists() else None) != data
               for path, data in before.items()):
            raise SystemExit("A refused Windows launch changed a login store or session")
    finally:
        state.write_bytes(original)


def legacy_password_preferences(app, origin):
    executable = (app / "Contents/MacOS/Chromium").resolve()
    node = shutil.which("node")
    if not executable.is_file() or not node:
        raise SystemExit("Expected the old Zitter app and Node.js")

    with ExitStack() as resources:
        source = Path(resources.enter_context(
            TemporaryDirectory(prefix="zitter-content-legacy-"))).resolve()
        parent = Path(resources.enter_context(
            TemporaryDirectory(prefix="zitter-content-password-trial-"))).resolve()
        restored = Path(resources.enter_context(
            TemporaryDirectory(prefix="zitter-content-legacy-"))).resolve()
        restored.rmdir()
        trial = parent / "trial"
        fixture = origin + "/legacy/work"
        seed_legacy_root(source, fixture)
        run_legacy_profile_probe(
            node, executable, source, "-", "-", fixture, "disable-password-saving"
        )
        source_snapshot = inventory(source)
        if prepare(source, trial) != 2:
            raise SystemExit("The legacy snapshot lost a workspace profile")
        prefs = Path("ZitterWork", "Preferences")
        payload = (trial / "candidate" / prefs).read_bytes()
        if json.loads(payload).get(PREF) is not False:
            raise SystemExit("The legacy preference was not written to disk")
        rollback(trial, restored)
        run_legacy_profile_probe(
            node, executable, restored, "-", "-", fixture, "read-password-saving-disabled"
        )
        if inventory(source) != source_snapshot:
            raise SystemExit("The legacy source profile changed")
        return payload


def verify_saving_preference(root, run, saved_ciphertext, legacy_snapshot, prefix):
    work = root / "workspace-work"
    content_prefs = work / PREFS_FILE
    legacy_prefs = work / "Preferences"
    legacy_prefs.write_bytes(legacy_snapshot)

    run("off")
    run("enable")
    if json.loads(content_prefs.read_text(encoding="utf-8")).get(PREF) is not True:
        raise SystemExit("Re-enabling was not saved to Work")
    run("disable")
    if json.loads(content_prefs.read_text(encoding="utf-8")).get(PREF) is not False:
        raise SystemExit("Disabling was not saved to Work")
    run("off")
    run("personal")
    run("check")

    damaged = b'{"credentials_enable_service":'
    content_prefs.write_bytes(damaged)
    run("unavailable")
    run("check")
    if content_prefs.read_bytes() != damaged:
        raise SystemExit("The damaged preference file was replaced")
    if legacy_prefs.read_bytes() != legacy_snapshot:
        raise SystemExit("Content changed the legacy profile preferences")
    if read_ciphertext(root, prefix) != saved_ciphertext:
        raise SystemExit("Changing preferences altered the saved password")


def open_interactive_reauth(executable, root, theme, renderer):
    print("Open Passwords in Work and use Reveal password on the synthetic login.", flush=True)
    print("Cancel once and check that it stays masked. Reopen and authenticate to check reveal.",
          flush=True)
    print("Close the workspace when finished.", flush=True)
    subprocess.run([
        str(executable), "--zitter-content-workspace",
        f"--zitter-content-workspace-root={root}", "--no-proxy-server",
        f"--zitter-content-workspace-theme={theme}",
        f"--zitter-rust-raster={renderer}",
    ], check=True)


def verify_clear_passwords(executable, root, theme, renderer):
    work, personal = (root / profile / "Login Data" for profile in PROFILES)
    old_user = USERNAME + "-old"
    with closing(sqlite3.connect(work)) as db:
        with db:
            db.row_factory = sqlite3.Row
            original = dict(db.execute("SELECT * FROM logins").fetchone())
            older = {**original, "id": original["id"] + 1, "username_value": old_user,
                     "date_created": original["date_created"] - 7_200_000_000}
            columns = ", ".join('"' + name.replace('"', '""') + '"' for name in older)
            db.execute(f"INSERT INTO logins ({columns}) VALUES "
                       f"({', '.join('?' for _ in older)})", tuple(older.values()))
        with closing(sqlite3.connect(personal)) as destination:
            db.backup(destination)

    def saved(database):
        with closing(sqlite3.connect(f"{database.as_uri()}?mode=ro", uri=True)) as db:
            return dict(db.execute("SELECT username_value, password_value FROM logins"))

    original = saved(work)
    expected_phases = (
        ("password-cancel", original, "Zitter Content cancelled password clearing"),
        ("password-recent", {old_user: original[old_user]}, "Zitter Content cleared Work saved passwords"),
        ("password-all", {}, "Zitter Content cleared Work saved passwords"),
        ("password-all", {}, "Zitter Content cleared Work saved passwords"),
    )
    with ThreadingHTTPServer(("127.0.0.1", 0), WorkspaceFixture) as server:
        origin = f"http://127.0.0.1:{server.server_port}"
        (root / SESSION_FILE).write_text(json.dumps(seed_session(origin)), encoding="utf-8")
        server.requests = []
        server.reopening = False
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            for phase, remaining, marker in expected_phases:
                # The Workspace fixture always starts in Personal, then switches to Work.
                session_path = root / SESSION_FILE
                session = json.loads(session_path.read_text(encoding="utf-8"))
                session["active_workspace"] = "personal"
                session_path.write_text(json.dumps(session), encoding="utf-8")
                result = launch(executable, root, origin, (
                    f"--zitter-content-clear-data-smoke-test={phase}",
                    f"--zitter-content-workspace-theme={theme}", f"--zitter-rust-raster={renderer}",
                ))
                require(result, marker, phase)
                if saved(work) != remaining or saved(personal) != original:
                    raise SystemExit("Password clearing changed the wrong profile or time range")
                server.reopening = True
        finally:
            server.shutdown()
            thread.join()
    print(f"Content cleared saved passwords by time range in {theme} with {renderer}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path, help="Zitter Content Lab.app or zitter_content.exe")
    parser.add_argument("--theme", choices=("light", "dark"), default="light")
    parser.add_argument("--renderer", choices=("compositor", "metal"), default="compositor")
    parser.add_argument("--clear-passwords", action="store_true",
                        help="verify Advanced saved-password clearing and cancellation")
    parser.add_argument("--password-manager", action="store_true",
                        help="also exercise the profile-scoped password manager")
    parser.add_argument("--password-preference", action="store_true",
                        help="also test profile preference migration and persistence")
    parser.add_argument("--interactive-reauth", action="store_true",
                        help="leave the seeded workspace open for a manual OS reauthentication check")
    parser.add_argument("--legacy-app", type=Path,
                        help="set the imported preference in a disposable old Zitter profile")
    parser.add_argument("--previous-content-app", type=Path,
                        help="save the first password with an earlier Content build")
    args = parser.parse_args()
    windows = os.name == "nt"
    if args.legacy_app and (windows or not args.password_preference):
        parser.error("--legacy-app requires macOS and --password-preference")
    if args.previous_content_app and (windows or args.clear_passwords):
        parser.error("--previous-content-app requires the macOS password prompt suite")
    if windows and args.renderer == "metal":
        parser.error("Metal requires macOS")
    executable = args.app.resolve() if windows else (
        args.app / "Contents/MacOS/Zitter Content Lab"
    ).resolve()
    prefix = b"v10" if windows else ENCRYPTED_PREFIX
    if not executable.is_file():
        parser.error(f"Missing app executable {executable}")
    previous = None
    if args.previous_content_app:
        previous = (args.previous_content_app / "Contents/MacOS/Zitter Content Lab").resolve()
        if not previous.is_file() or previous == executable:
            parser.error("Select a different, earlier Content app for the password seed")

    with (
        ThreadingHTTPServer(("127.0.0.1", 0), PasswordFixture) as server,
        TemporaryDirectory(prefix="zitter-content-probe-") as directory,
    ):
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            root = Path(directory).resolve()
            if not windows:
                key = root / KEY_FILE
                key.write_bytes(secrets.token_bytes(32))
                key.chmod(0o600)
            origin = f"http://127.0.0.1:{server.server_port}"
            def run(action, app=executable):
                result = launch(app, root, origin, (
                    f"--zitter-content-workspace-password-probe={action}",
                    f"--zitter-content-workspace-theme={args.theme}",
                    f"--zitter-rust-raster={args.renderer}",
                ))
                require(result, MARKER, action)
                print(f"Verified workspace password {action}", flush=True)

            if args.clear_passwords:
                run("save")
                read_ciphertext(root, prefix)
                verify_clear_passwords(executable, root, args.theme, args.renderer)
                return

            original = None
            for action in PROMPT_CASES:
                run(action, previous if action == "save" and previous else executable)
                ciphertext = read_ciphertext(root, prefix)
                if action == "save" and previous:
                    server.expect_original_password = True
                    try:
                        run("check")
                    finally:
                        server.expect_original_password = False
                    if read_ciphertext(root, prefix) != ciphertext:
                        raise SystemExit("The replacement changed the earlier app's encrypted password")
                    print("Verified the replacement autofills the earlier Content password", flush=True)
                if action == "update" and ciphertext == original:
                    raise SystemExit("Updating the login left the old password in place")
                if action not in ("save", "update") and ciphertext != original:
                    raise SystemExit(f"{action} altered the saved password")
                original = ciphertext
                if windows and action == "save":
                    verify_windows_login_key(executable, root, origin)
                    print("Verified Windows rejects a wrong key for a password-only profile",
                          flush=True)
            if args.password_manager:
                run("manager")
                if read_ciphertext(root, prefix) != original:
                    raise SystemExit("The manager changed the original Work credential")
            if args.password_preference:
                legacy = (
                    legacy_password_preferences(args.legacy_app, origin)
                    if args.legacy_app
                    else json.dumps({PREF: False, "other_preference": "retain"}).encode()
                )
                verify_saving_preference(root, run, original, legacy, prefix)
            if args.interactive_reauth:
                open_interactive_reauth(executable, root, args.theme, args.renderer)
        finally:
            server.shutdown()
            thread.join()


if __name__ == "__main__":
    main()
