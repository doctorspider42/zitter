#!/usr/bin/env python3
"""Verify ad blocking updates on documents restored from back/forward cache."""

import argparse
from collections import Counter
from http.server import ThreadingHTTPServer
import json
import os
from pathlib import Path
import secrets
from tempfile import TemporaryDirectory
from threading import Thread

from verify_content_workspace_session import (
    KEY_FILE, MARKER, SESSION_FILE, WorkspaceFixture, launch, seed_session,
)


PROBE = "--zitter-content-adblock-cache-smoke-test"
CACHE_MARKER = "Zitter Content reapplied ad blocking on cached documents"
PAGE = "/adblock-cache"
CHILD = "/adblock-cache-child"
AWAY = "/adblock-cache-away"
CONTENTS = """
<div class="cache-original">Original filter target</div>
<div class="cache-replacement">Replacement filter target</div>
<div class="cache-excepted">Cosmetic exception target</div>
<script>
let restores = 0;
addEventListener('pageshow', event => { if (event.persisted) ++restores; });
function report() {
  const hidden = name => getComputedStyle(document.querySelector('.cache-' + name)).display === 'none';
  const original = hidden('original'), replacement = hidden('replacement'), excepted = hidden('excepted');
  const state = original && !replacement && excepted ? 'old'
    : !original && replacement && !excepted ? 'new'
    : !original && !replacement && !excepted ? 'none' : 'transition';
  return 'Adblock cache ' + state + ':' + restores;
}
</script>
"""


class CacheFixture(WorkspaceFixture):
    def do_GET(self):
        if self.path not in (PAGE, CHILD, AWAY):
            super().do_GET()
            return
        self.server.cache_requests.append(self.path)
        port = self.server.server_port
        parent = f"http://127.0.0.1:{port}"
        child = f"http://localhost:{port}"
        if self.path == PAGE:
            markup = f"""{CONTENTS}<iframe src="{child}{CHILD}"></iframe>
<script>
const frame = document.querySelector('iframe');
let childState;
addEventListener('message', event => {{
  if (event.source === frame.contentWindow && event.origin === {json.dumps(child)})
    childState = event.data;
}});
setInterval(() => {{
  const own = report();
  document.title = childState === own ? own : 'Waiting for both cache documents';
}}, 50);
</script>"""
        elif self.path == CHILD:
            markup = f"""{CONTENTS}<script>
let previous;
setInterval(() => {{
  const state = report();
  if (state === previous) return;
  previous = state;
  parent.postMessage(state, {json.dumps(parent)});
}}, 50);
</script>"""
        else:
            markup = "<title>Adblock cache away</title>"
        self.send_page(("<!doctype html><meta charset=utf-8>" + markup).encode(),
                       cache_control="public, max-age=600")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path, help="Zitter Content Lab.app or zitter_content.exe")
    args = parser.parse_args()
    executable = args.app if os.name == "nt" else args.app / "Contents/MacOS/Zitter Content Lab"
    executable = executable.resolve()
    if not executable.is_file():
        parser.error(f"App executable is missing: {executable}")

    with ThreadingHTTPServer(("127.0.0.1", 0), CacheFixture) as server:
        origin = f"http://127.0.0.1:{server.server_port}"
        server.reopening = False
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            for theme in ("light", "dark"):
                for renderer in ("compositor", "metal"):
                    server.requests = []
                    server.cache_requests = []
                    with TemporaryDirectory(prefix="zitter-content-probe-") as directory:
                        root = Path(directory).resolve()
                        if os.name == "nt":
                            from verify_content_workspace_session import WINDOWS_LOCAL_STATE, new_wrapped_windows_key

                            (root / WINDOWS_LOCAL_STATE).write_text(json.dumps({
                                "os_crypt": {"encrypted_key": new_wrapped_windows_key()},
                            }), encoding="utf-8")
                        else:
                            key = root / KEY_FILE
                            key.write_bytes(secrets.token_bytes(32))
                            key.chmod(0o600)
                        (root / SESSION_FILE).write_text(json.dumps(seed_session(origin)), encoding="utf-8")
                        result = launch(executable, root, origin, (
                            PROBE, f"--zitter-rust-raster={renderer}",
                            f"--zitter-content-workspace-theme={theme}",
                        ))
                        if result.returncode or MARKER not in result.stdout or CACHE_MARKER not in result.stdout:
                            raise SystemExit(f"Cache probe failed in {theme}/{renderer}.\n"
                                             f"{result.stdout}\n{result.stderr}")
                        for path in root.rglob("Content Ad Blocking Preferences"):
                            prefs = json.loads(path.read_text(encoding="utf-8")).get("zitter", {})
                            if prefs.get("ad_blocking_rules") or prefs.get("ad_blocking_allowed_sites"):
                                raise SystemExit(f"Cache probe left test filters in {path.parent.name}")
                    requests = Counter(server.cache_requests)
                    if requests[PAGE] != 1 or requests[CHILD] != 1:
                        raise SystemExit(f"Cache probe fetched a document again: {server.cache_requests}")
                    print(f"Ad blocking cache restoration passed in {theme}/{renderer}")
        finally:
            server.shutdown()
            thread.join()


if __name__ == "__main__":
    main()
