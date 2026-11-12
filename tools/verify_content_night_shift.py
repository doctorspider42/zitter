#!/usr/bin/env python3
"""Verify bundled Night Shift across ordinary macOS starts with disposable profiles."""

import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import plistlib
import subprocess
from tempfile import TemporaryDirectory
from threading import Event, Thread

from content_file_keychain_fixture import compile_file_keychain_fixture, file_keychain_environment
from package_app import BUNDLE_ID
from verify_content_fresh_startup import verify_startup
from verify_content_upgrade import run_profile


class Fixture(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path.startswith("/result/"):
            self.server.result = self.path
            self.server.completed.set()
            body = b""
        elif self.path in ("/trial/work", "/trial/personal"):
            role = self.path.rsplit("/", 1)[-1]
            expected = json.dumps(role == "work")
            body = f"""<!doctype html><title>Night Shift configuration</title>
<style>body {{ color: black; background: white; }}</style>
<body><h1>Night Shift fixture</h1></body>
<script>
const deadline = performance.now() + 5500;
const timer = setInterval(() => {{
  const styled = !!document.querySelector("style.darkreader");
  if (performance.now() < deadline && !styled) return;
  clearInterval(timer);
  fetch("/result/{role}/" + (styled === {expected} ? "passed" : "failed"));
}}, 100);
</script>""".encode()
        else:
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Type", "text/html")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path)
    args = parser.parse_args()
    source = args.app.resolve(strict=True)
    with (TemporaryDirectory(prefix="zitter-night-shift-") as trial,
          TemporaryDirectory(prefix="zitter-content-probe-") as profile,
          ThreadingHTTPServer(("127.0.0.1", 0), Fixture) as server):
        app = Path(trial) / "Zitter.app"
        root = Path(profile).resolve()
        subprocess.run(["/bin/cp", "-cRp", source, app], check=True, timeout=90)
        info_path = app / "Contents/Info.plist"
        info = plistlib.loads(info_path.read_bytes())
        info.update(CFBundleIdentifier=BUNDLE_ID, ZitterUsePreparedWorkspace=True)
        info_path.write_bytes(plistlib.dumps(info))
        subprocess.run(["/usr/bin/codesign", "--force", "--sign", "-", app], check=True, timeout=90)
        library = compile_file_keychain_fixture(Path(trial))
        env = dict(file_keychain_environment(library), ZITTER_CONTENT_STARTUP_TRIAL_ROOT=str(root))
        verify_startup(app / "Contents/MacOS/Zitter Content Lab", root, env)
        server.completed = Event()
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()
        session_path = root / "ContentKeychainWorkspaceSession.json"
        try:
            for index, role in enumerate(("work", "personal", "work")):
                session = json.loads(session_path.read_text())
                session["active_workspace"] = role
                if index == 0:
                    session["night_shift"] = {"enabled": True, "all_profiles": False,
                                              "profiles": ["work"], "brightness": 90}
                session[role].update(active=0, tabs=[{
                    "url": f"http://127.0.0.1:{server.server_port}/trial/{role}",
                    "title": "Night Shift configuration",
                }])
                session_path.write_text(json.dumps(session))
                run_profile(app, env, server, role, seed=False)
                saved = json.loads(session_path.read_text())["night_shift"]
                if saved["profiles"] != ["work"] or saved["brightness"] != 90:
                    raise ValueError("Night Shift configuration changed across restart")
                print(f"{role}: expected Night Shift state, configuration retained", flush=True)
        finally:
            server.shutdown()
            thread.join()


if __name__ == "__main__":
    main()
