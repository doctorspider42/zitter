#!/usr/bin/env python3
"""Exercise actual HTTP Basic/Digest challenges through the Workspace dialog."""

import argparse
from base64 import b64encode
from hashlib import md5
from http.server import ThreadingHTTPServer
import json
from pathlib import Path
import secrets
from tempfile import TemporaryDirectory
from threading import Thread
from urllib.request import parse_http_list, parse_keqv_list

from verify_content_profiles import KEY_FILE
from verify_content_workspace_session import SESSION_FILE, WorkspaceFixture, launch, seed_session

USERNAME = "zitter-fixture"
PASSWORD = "fixture-password"
REALM = "Zitter HTTP fixture"
NONCE = "fixture-nonce-12345"
MARKER = "Zitter Content HTTP authentication probe passed"


def digest(text):
    return md5(text.encode("ascii")).hexdigest()


class AuthFixture(WorkspaceFixture):
    def log_message(self, _format, *args):
        pass

    def authenticated(self):
        header = self.headers.get("Authorization", "")
        if self.server.scheme == "Basic":
            expected = b64encode(f"{USERNAME}:{PASSWORD}".encode()).decode("ascii")
            return header == "Basic " + expected
        if not header.startswith("Digest "):
            return False
        try:
            fields = parse_keqv_list(parse_http_list(header.removeprefix("Digest ")))
            a1 = digest(f"{USERNAME}:{REALM}:{PASSWORD}")
            a2 = digest(f"GET:{self.path}")
            expected = digest(f"{a1}:{NONCE}:{fields['nc']}:{fields['cnonce']}:auth:{a2}")
            return (fields.get("username") == USERNAME and fields.get("realm") == REALM
                    and fields.get("nonce") == NONCE and fields.get("qop") == "auth"
                    and fields.get("uri") == self.path and fields.get("response") == expected)
        except (KeyError, ValueError, UnicodeError):
            return False

    def do_GET(self):
        if self.path == "/http-auth/replacement":
            self.send_page(b"<!doctype html><title>HTTP replacement</title>")
        elif self.path.startswith("/http-auth/"):
            accepted = self.authenticated()
            self.server.auth_results.append(accepted if "Authorization" in self.headers else None)
            title = "HTTP authenticated" if accepted else "HTTP cancelled"
            body = f"<!doctype html><title>{title}</title>".encode()
            self.send_response(200 if accepted else 401)
            if not accepted:
                challenge = f'{self.server.scheme} realm="{REALM}"'
                if self.server.scheme == "Digest":
                    challenge += f', nonce="{NONCE}", algorithm=MD5, qop="auth"'
                self.send_header("WWW-Authenticate", challenge)
            self.send_header("Content-Type", "text/html")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)
        else:
            super().do_GET()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path)
    parser.add_argument("--theme", choices=("light", "dark"), default="light")
    parser.add_argument("--renderer", choices=("compositor", "metal"), default="compositor")
    args = parser.parse_args()
    executable = (args.app / "Contents/MacOS/Zitter Content Lab").resolve()
    if not executable.is_file():
        parser.error("Missing Content macOS executable")
    with ThreadingHTTPServer(("127.0.0.1", 0), AuthFixture) as server:
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()
        origin = f"http://127.0.0.1:{server.server_port}"
        try:
            for scheme in ("Basic", "Digest"):
                for action in ("allow", "cancel", "navigate", "hide"):
                    with TemporaryDirectory(prefix="zitter-content-probe-") as directory:
                        root = Path(directory).resolve()
                        key = root / KEY_FILE
                        key.write_bytes(secrets.token_bytes(32))
                        key.chmod(0o600)
                        (root / SESSION_FILE).write_text(json.dumps(seed_session(origin)))
                        server.scheme, server.auth_results = scheme, []
                        result = launch(executable, root, origin, [
                            f"--zitter-content-http-auth-probe={action}",
                            f"--zitter-content-workspace-theme={args.theme}",
                            f"--zitter-rust-raster={args.renderer}",
                        ])
                        if result.returncode or MARKER not in result.stdout.splitlines():
                            raise SystemExit(f"{scheme} {action} failed:\n{result.stdout}\n{result.stderr}")
                        expected = [None, True, None] if action == "allow" else [None]
                        if server.auth_results != expected:
                            raise SystemExit(f"{scheme} {action}: unexpected authentication attempts "
                                             f"{server.auth_results!r}")
                        print(f"Verified HTTP {scheme} {action} ({args.theme}, {args.renderer})", flush=True)
        finally:
            server.shutdown()
            thread.join(timeout=5)


if __name__ == "__main__":
    main()
