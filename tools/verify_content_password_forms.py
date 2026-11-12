#!/usr/bin/env python3
"""Verify form autofill through the Content renderer with disposable macOS profiles."""

import argparse
from contextlib import ExitStack, contextmanager
import hashlib
from http.server import ThreadingHTTPServer
import json
from pathlib import Path
import secrets
from tempfile import TemporaryDirectory
from threading import Thread

from verify_content_profiles import KEY_FILE, ProbePage, run_case


class FormFixture(ProbePage):
    def credentials(self, case):
        return ("zitter-profile-probe", f"zitter-{case}-probe-secret")

    def do_GET(self):
        if not self.path.startswith("/password-form/"):
            super().do_GET()
            return

        case = self.path.removeprefix("/password-form/")
        if case == "frame":
            self.send_page(b"""<!doctype html><meta charset=utf-8>
<style>body {margin: 0} iframe {display: block; width: 480px; height: 240px; border: 0}</style>
<script>
  addEventListener('message', event => {
    if (event.origin === location.origin && event.source === fixture.contentWindow &&
        ['passed', 'failed'].includes(event.data)) {
      document.title = 'Zitter password form probe ' + event.data;
    }
  });
</script>
<iframe id=fixture src="/password-form/child"
        onload="document.title = 'Zitter password form probe ready'"></iframe>""")
            return

        if case not in ("work", "personal", "private", "foreign", "child"):
            self.send_error(404)
            return
        expected_username = None
        digest = None
        if case in ("work", "personal"):
            expected_username, secret = self.credentials(case)
            digest = hashlib.sha256(secret.encode()).hexdigest()
        page = f"""<!doctype html><meta charset=utf-8>
<button type="button" style="position:absolute;left:10px;top:8px;height:32px">Check</button>
<form method="post" action="/submit" style="margin-top:65px">
  <input id="username" name="username" autocomplete="username">
  <input id="password" name="password" type="password" autocomplete="current-password">
  <button type="submit">Sign in</button>
</form>
<script>
  const expected = {json.dumps(digest)};
  const expectedUsername = {json.dumps(expected_username)};
  const username = document.querySelector('#username');
  const password = document.querySelector('#password');
  const finish = success => {{
    const result = success ? 'passed' : 'failed';
    if (parent !== window)
      parent.postMessage(result, location.origin);
    else
      document.title = 'Zitter password form probe ' + result;
  }};
  document.querySelector('button').addEventListener('click', async event => {{
    if (!event.isTrusted) return;
    const end = performance.now() + (expected ? 8000 : 1500);
    do {{
      if (!expected && (username.value || password.value ||
          username.matches(':-webkit-autofill') || password.matches(':-webkit-autofill'))) {{
        finish(false);
        return;
      }}
      if (expected && username.value === expectedUsername && password.value) {{
        const bytes = await crypto.subtle.digest(
            'SHA-256', new TextEncoder().encode(password.value));
        const hash = Array.from(new Uint8Array(bytes), b =>
          b.toString(16).padStart(2, '0')).join('');
        finish(hash === expected);
        return;
      }}
      await new Promise(resolve => setTimeout(resolve, 50));
    }} while (performance.now() < end);
    finish(!expected);
  }}, {{once: true}});
  if (parent === window)
    document.title = 'Zitter password form probe ready';
</script>"""
        self.send_page(page.encode())


@contextmanager
def serve():
    with ThreadingHTTPServer(("127.0.0.1", 0), FormFixture) as server:
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            yield server.server_port
        finally:
            server.shutdown()
            thread.join()


def require(result, marker, case):
    if result.returncode or marker not in result.stdout:
        raise SystemExit(f"{case} failed ({result.returncode}).\n{result.stdout}\n{result.stderr}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path, help="Zitter Content Lab.app")
    args = parser.parse_args()
    executable = (args.app / "Contents/MacOS/Zitter Content Lab").resolve()
    if not executable.is_file():
        parser.error(f"Missing app executable {executable}")

    with ExitStack() as resources:
        root = Path(resources.enter_context(
            TemporaryDirectory(prefix="zitter-content-probe-"))).resolve()
        origin = resources.enter_context(serve())
        foreign_origin = resources.enter_context(serve())
        key = root / KEY_FILE
        key.write_bytes(secrets.token_bytes(32))
        key.chmod(0o600)
        for index, profile in enumerate(("work", "personal")):
            result = run_case(executable, origin, root, index, profile, password_probe="write")
            require(result, "Zitter Content password store probe passed", f"Seed {profile}")
        for port, profile, case in (
            (origin, "work", "work"),
            (origin, "personal", "personal"),
            (origin, "private", "private"),
            (foreign_origin, "work", "foreign"),
            (origin, "work", "frame"),
        ):
            result = run_case(executable, port, root, 0, profile, password_form=case)
            require(result, "Zitter Content password form probe passed", case)
            print(f"Verified password form for {case}", flush=True)


if __name__ == "__main__":
    main()
