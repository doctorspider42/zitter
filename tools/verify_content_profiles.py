#!/usr/bin/env python3
"""Exercise Content Lab cookies and DOM storage on a disposable loopback origin."""

import argparse
from contextlib import closing
from http import cookies
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import secrets
import sqlite3
import subprocess
import tempfile
import threading


# Each invocation starts a new browser process. Both durable profiles and both
# private sessions visit the same origin, with different expected values.
CASES = (
    ("work", "", "work"),
    ("personal", "", "personal"),
    ("private", "", "private"),
    ("work", "work", ""),
    ("personal", "personal", ""),
    ("private", "", ""),
)
KEY_FILE = "probe-cookie-key"
ENCRYPTED_PREFIX = b"zitter-probe-v1"


class ProbePage(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        pass

    def do_GET(self):
        if self.path == "/after-console":
            self.send_page(b"<!doctype html><title>Replacement document</title>")
            return
        if not self.path.startswith("/case/"):
            self.send_error(404)
            return
        try:
            index = int(self.path.removeprefix("/case/"))
            if not 0 <= index < len(CASES):
                raise ValueError("Unknown case")
        except ValueError:
            self.send_error(404)
            return

        profile, expected, seed = CASES[index]
        request_cookies = cookies.SimpleCookie(self.headers.get("Cookie", ""))
        cookie = request_cookies.get("profile_probe")
        network_value = cookie.value if cookie is not None else ""
        page = f"""<!doctype html>
        <meta charset="utf-8">
        <body>
        <script>
          const expected = {json.dumps(expected)};
          const seed = {json.dumps(seed)};
          const canPersist = {json.dumps(profile == "work")};

          function framePermission() {{
            const frame = document.createElement('iframe');
            const result = new Promise(resolve => {{
              window.addEventListener('message', function receive(event) {{
                if (event.source !== frame.contentWindow)
                  return;
                window.removeEventListener('message', receive);
                frame.remove();
                resolve(event.data);
              }});
            }});
            frame.srcdoc = '<script>navigator.storage.persisted().then(' +
              'value => parent.postMessage(value, "*"), ' +
              '() => parent.postMessage(null, "*"));</scr' + 'ipt>';
            document.body.append(frame);
            return result;
          }}

          (async () => {{
            const cookie = document.cookie.split('; ')
              .find(entry => entry.startsWith('profile_probe='))?.split('=')[1] ?? '';
            const storage = localStorage.getItem('profile_probe') ?? '';
            if (cookie !== expected || storage !== expected ||
                {json.dumps(network_value)} !== expected ||
                await navigator.storage.persisted() !== canPersist ||
                await navigator.storage.persist() !== canPersist ||
                (canPersist && await framePermission() !== false)) {{
              throw new Error('Unexpected profile state');
            }}
            if (seed) {{
              document.cookie = 'profile_probe=' + seed +
                '; Max-Age=600; Path=/; SameSite=Strict';
              localStorage.setItem('profile_probe', seed);
            }}
            console.info('zitter-content-probe-console');
            document.title = 'Zitter profile probe passed';
          }})().catch(() => document.title = 'Zitter profile probe failed');
        </script>""".encode()
        self.send_page(page)

    def send_page(self, page):
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(page)))
        self.end_headers()
        self.wfile.write(page)


def verify_ciphertext(root, profile, *, encrypted_prefix=ENCRYPTED_PREFIX):
    database = root / profile / "Network/Cookies"
    if not database.is_file():
        raise SystemExit(f"{profile} cookie database was not created")
    with closing(sqlite3.connect(f"{database.as_uri()}?mode=ro", uri=True)) as connection:
        rows = connection.execute(
            "SELECT value, encrypted_value FROM cookies WHERE name = ?",
            ("profile_probe",),
        ).fetchall()
    if (len(rows) != 1 or rows[0][0] != "" or not isinstance(rows[0][1], bytes)
            or not rows[0][1].startswith(encrypted_prefix)):
        raise SystemExit(f"{profile} cookie was not stored with OSCrypt encryption")


def verify_history(root, profile, expected):
    database = root / profile / "History"
    if not database.is_file():
        raise SystemExit(f"{profile} history database was not created")
    with closing(sqlite3.connect(f"{database.as_uri()}?mode=ro", uri=True)) as history:
        actual = {
            url: (title, visits)
            for url, title, visits in history.execute(
                "SELECT urls.url, urls.title, COUNT(visits.id) FROM urls "
                "JOIN visits ON visits.url = urls.id GROUP BY urls.id"
            )
        }
    if actual != expected:
        raise SystemExit(f"{profile} history mismatch: {actual!r}, expected {expected!r}")


def verify_password_ciphertext(root, profile):
    database = root / profile / "Login Data"
    with closing(sqlite3.connect(f"{database.as_uri()}?mode=ro", uri=True)) as db:
        rows = db.execute("SELECT username_value, password_value FROM logins").fetchall()
    if len(rows) != 1 or rows[0][0] != "zitter-profile-probe":
        raise SystemExit(f"Unexpected password entries for {profile}")
    ciphertext = rows[0][1]
    if (not isinstance(ciphertext, bytes) or not ciphertext.startswith(ENCRYPTED_PREFIX)
            or f"zitter-{profile}-probe-secret".encode() in ciphertext):
        raise SystemExit(f"Unexpected password ciphertext for {profile}")


def run_case(executable, port, root, index, profile, console_probe=False, password_probe=None,
             password_form=None):
    path = f"/password-form/{password_form}" if password_form else f"/case/{index}"
    args = [
        str(executable),
        f"--zitter-content-profile-probe=http://127.0.0.1:{port}{path}",
        f"--zitter-content-probe-root={root}",
        f"--zitter-content-probe-profile={profile}",
    ]
    if console_probe:
        args.append("--zitter-content-probe-console")
    if password_probe:
        args.append(f"--zitter-content-probe-password-store={password_probe}")
    if password_form:
        args.append("--zitter-content-probe-password-form")
    try:
        return subprocess.run(args, capture_output=True, text=True, timeout=45)
    except subprocess.TimeoutExpired as error:
        raise SystemExit(
            f"Profile case {index} timed out. Output: {error.stderr!r}"
        ) from error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path, help="Zitter Content Lab.app")
    parser.add_argument("--password-store", action="store_true",
                        help="also test the profile-scoped password stores")
    options = parser.parse_args()
    executable = (options.app / "Contents/MacOS/Zitter Content Lab").resolve()
    if not executable.is_file():
        parser.error(f"App executable is missing: {executable}")

    with (
        ThreadingHTTPServer(("127.0.0.1", 0), ProbePage) as server,
        tempfile.TemporaryDirectory(prefix="zitter-content-probe-") as root,
    ):
        server_thread = threading.Thread(target=server.serve_forever, daemon=True)
        server_thread.start()
        try:
            root = Path(root).resolve()
            rejected = run_case(executable, server.server_port, root, 0, "work")
            if (rejected.returncode != 1 or any(root.iterdir())
                    or "needs a private 32-byte key fixture" not in rejected.stderr):
                raise SystemExit(
                    "A persistent probe without a key fixture must fail before "
                    f"creating a profile.\n{rejected.stdout}\n{rejected.stderr}"
                )
            key = root / KEY_FILE
            key.write_bytes(secrets.token_bytes(32))
            key.chmod(0o600)
            if options.password_store:
                with tempfile.TemporaryDirectory() as outside:
                    canary = Path(outside) / "outside-database"
                    canary.write_bytes(b"untouched")
                    work = root / "work"
                    work.mkdir()
                    for name in ("Login Data", "Login Data-journal"):
                        link = work / name
                        link.symlink_to(canary)
                        rejected = run_case(
                            executable, server.server_port, root, 0, "work",
                            password_probe="write",
                        )
                        if rejected.returncode != 1 or canary.read_bytes() != b"untouched":
                            raise SystemExit(f"Profile must reject a symlink at {name}")
                        link.unlink()
                    work.rmdir()
            for index, (profile, expected, seed) in enumerate(CASES):
                print(f"Checking {profile} (expected {expected or 'empty'})", flush=True)
                console_probe = index == 0
                result = run_case(
                    executable, server.server_port, root, index, profile, console_probe,
                    ("write" if seed else "read") if options.password_store else None,
                )
                if (result.returncode or "Zitter profile probe passed" not in result.stdout
                        or (options.password_store and
                            "Zitter Content password store probe passed" not in result.stdout)
                        or (console_probe and "Zitter Content console probe passed"
                            not in result.stdout)):
                    raise SystemExit(
                        f"Profile case {index} failed ({result.returncode}).\n"
                        f"{result.stdout}\n{result.stderr}"
                    )

            origin = f"http://127.0.0.1:{server.server_port}"
            for profile in ("work", "personal"):
                verify_ciphertext(root, profile)
                if options.password_store:
                    verify_password_ciphertext(root, profile)
                expected = {
                    f"{origin}/case/{index}": ("Zitter profile probe passed", 1)
                    for index, (owner, _, _) in enumerate(CASES)
                    if owner == profile
                }
                if profile == "work":
                    expected[f"{origin}/after-console"] = ("Replacement document", 1)
                verify_history(root, profile, expected)
            if {path.name for path in root.iterdir()} != {"work", "personal", KEY_FILE}:
                raise SystemExit("Unexpected persistent directories after private sessions")
            print("Persistent and private profile storage passed")
        finally:
            server.shutdown()
            server_thread.join()


if __name__ == "__main__":
    main()
