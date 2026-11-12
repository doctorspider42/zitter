#!/usr/bin/env python3
"""Check Content Lab scriptlets, request filtering, and cosmetic rules."""

import argparse
from collections import Counter
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import subprocess
import tempfile
import threading


PAGE = b"""<!doctype html>
<meta charset="utf-8">
<title>Scriptlet timing probe</title>
<script>
  // Capture the value in the very first script, before the body exists.
  const mainAtStart = window.zitterScriptletProbe === true;
  addEventListener('DOMContentLoaded', () => {
    const frames = [document.createElement('iframe'), document.createElement('iframe')];
    frames[1].setAttribute('sandbox', 'allow-scripts');
    const seen = new Set();
    const failures = mainAtStart ? [] : ['main-world-timing'];
    let networkDone = false;
    let cosmeticsDone = false;
    function finish() {
      if (seen.size === frames.length && networkDone && cosmeticsDone) {
        document.title = failures.length
          ? 'Zitter scriptlet probe failed ' + failures.join(' ')
          : 'Zitter scriptlet probe passed';
      }
    }
    addEventListener('message', event => {
      const index = frames.findIndex(frame => frame.contentWindow === event.source);
      if (index < 0 || seen.has(index))
        return;
      seen.add(index);
      const expectedOrigin = index === 0 ? location.origin : 'null';
      if (event.origin !== expectedOrigin || event.data !== (index === 0))
        failures.push(index === 0 ? 'inherited-frame' : 'opaque-frame');
      finish();
    });
    for (const frame of frames) {
      frame.srcdoc = '<script>parent.postMessage(window.zitterScriptletProbe === true, "*")</scr' + 'ipt>';
      document.body.append(frame);
    }

    setTimeout(() => {
      const late = document.createElement('div');
      late.className = 'late-ad';
      late.textContent = 'Late test element';
      document.body.append(late);
    }, 600);

    const deadline = performance.now() + 7000;
    const hidden = selector => {
      const element = document.querySelector(selector);
      return element && getComputedStyle(element).display === 'none';
    };
    function checkCosmetics() {
      const done = ['.site-ad', '.generic-ad', '.procedural-ad', '.late-ad'].every(hidden) &&
        !document.querySelector('.action-ad');
      if (!done && performance.now() < deadline) {
        setTimeout(checkCosmetics, 50);
        return;
      }
      if (!done || hidden('.exempt-ad') || hidden('.procedural-control') ||
          window.__zitterBlocking !== undefined)
        failures.push('cosmetics');
      cosmeticsDone = true;
      finish();
    }
    checkCosmetics();

    const blocked = fetch('/blocked').then(() => false, () => true);
    const allowed = fetch('/allowed')
      .then(response => response.ok && response.text())
      .then(body => body === 'allowed', () => false);
    const redirected = new Promise(resolve => {
      const bait = document.createElement('script');
      bait.onload = () => resolve(window.zitterBaitReachedServer !== true);
      bait.onerror = () => resolve(false);
      bait.src = '/bait.js';
      document.head.append(bait);
    });
    Promise.all([blocked, allowed, redirected]).then(([didBlock, didAllow, didRedirect]) => {
      if (!didBlock) failures.push('blocked-request');
      if (!didAllow) failures.push('exception');
      if (!didRedirect) failures.push('redirect');
      networkDone = true;
      finish();
    });
  });
</script>
<body>
<div class="site-ad">Site-specific test element</div>
<div class="generic-ad">Generic test element</div>
<div class="exempt-ad">Excepted test element</div>
<div class="procedural-ad">Probe sponsored message</div>
<div class="action-ad">Removed test element</div>
<div class="procedural-ad procedural-control">Regular content</div>
</body>
"""


PRIVATE_PAGE = b"""<!doctype html><meta charset="utf-8">
<script>
const scriptlet = window.zitterPrivateAdblock === true;
addEventListener('DOMContentLoaded', async () => {
  const blocked = await fetch('/private-blocked').then(() => false, () => true);
  setTimeout(() => {
    const hidden = getComputedStyle(document.querySelector('.private-ad')).display === 'none';
    document.title = scriptlet && blocked && hidden ? 'Private adblock filtered'
        : !scriptlet && !blocked && !hidden ? 'Private adblock bypassed'
        : 'Private adblock mixed: ' + [scriptlet, blocked, hidden].join(',');
  }, 150);
});
</script><div class="private-ad">Sponsored</div>
"""

PRIVATE_LISTS = (
    "/adblock-lists/easylist.txt", "/adblock-lists/easyprivacy.txt",
    "/adblock-lists/antiadblock.txt",
)
PRIVATE_MARKER = "Zitter Content private workspace ad blocking passed"


class ScriptletServer(ThreadingHTTPServer):
    def __init__(self):
        super().__init__(("127.0.0.1", 0), ScriptletFixture)
        self.hits = []


class ScriptletFixture(BaseHTTPRequestHandler):
    def do_GET(self):
        self.server.hits.append(self.path)
        if self.path in PRIVATE_LISTS:
            if any(self.headers.get(name) for name in ("Cookie", "Authorization", "Referer")):
                self.send_error(400)
                return
            rules = ["[Adblock Plus 2.0]"]
            rules.extend(f"||private-list-{index}.invalid^" for index in range(1000))
            if self.path == PRIVATE_LISTS[0]:
                origin = f"http://127.0.0.1:{self.server.server_port}"
                rules.extend((
                    f"|{origin}/private-blocked|$xmlhttprequest",
                    "127.0.0.1##.private-ad",
                    "127.0.0.1##+js(set-constant, zitterPrivateAdblock, true)",
                ))
            response = ("text/plain", ("\n".join(rules) + "\n").encode())
        else:
            response = {
                "/private-adblock": ("text/html; charset=utf-8", PRIVATE_PAGE),
                "/private-blocked": ("text/plain", b"permitted by site exception"),
                "/scriptlets": ("text/html; charset=utf-8", PAGE),
                "/allowed": ("text/plain", b"allowed"),
                "/blocked": ("text/plain", b"blocked request reached the server"),
                "/bait.js": ("text/javascript", b"window.zitterBaitReachedServer = true;"),
            }.get(self.path)
        if not response:
            self.send_error(404)
            return
        content_type, body = response
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def verify_private_workspace(executable, server, root):
    origin = f"http://127.0.0.1:{server.server_port}/"
    environment = dict(os.environ, TMPDIR=str(root), TMP=str(root), TEMP=str(root))
    expected = Counter(PRIVATE_LISTS + ("/private-adblock",) * 3 +
                       ("/private-blocked",))
    appearances = (("light", "compositor"), ("dark", "compositor")) if os.name == "nt" else (
        ("light", "compositor"), ("dark", "metal"), ("dark", "compositor"), ("light", "metal"),
    )
    for theme, renderer in appearances:
        server.hits.clear()
        result = subprocess.run(
            [
                str(executable),
                "--zitter-content-workspace",
                f"--zitter-content-private-adblock-probe={origin}",
                f"--zitter-content-workspace-theme={theme}",
                f"--zitter-rust-raster={renderer}",
            ],
            env=environment,
            capture_output=True,
            text=True,
            timeout=65,
        )
        if result.returncode or PRIVATE_MARKER not in result.stdout.splitlines():
            raise SystemExit(
                f"Private Workspace ad blocking failed in {theme}/{renderer}.\n"
                f"{result.stdout}\n{result.stderr}"
            )
        hits = Counter(path for path in server.hits if path != "/favicon.ico")
        if hits != expected:
            raise SystemExit(f"Unexpected private Workspace requests: {server.hits}")
        if any(root.iterdir()):
            raise SystemExit("The private Workspace left files in its temporary root")
    print("Private Workspace ad blocking passed across independent sessions")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path, help="Zitter Content Lab.app or zitter_content.exe")
    parser.add_argument("--private-workspace", action="store_true",
                        help="Check the temporary Workspace filter policy (required on Windows)")
    options = parser.parse_args()
    windows = os.name == "nt"
    if windows and not options.private_workspace:
        parser.error("Windows requires --private-workspace")
    executable = options.app if windows else options.app / "Contents/MacOS/Zitter Content Lab"
    executable = executable.resolve()
    if not executable.is_file():
        parser.error(f"App executable is missing: {executable}")

    with (
        ScriptletServer() as server,
        tempfile.TemporaryDirectory(prefix="zitter-content-probe-") as temp,
    ):
        root = Path(temp).resolve()
        url = f"http://127.0.0.1:{server.server_port}/scriptlets"
        server_thread = threading.Thread(target=server.serve_forever, daemon=True)
        server_thread.start()
        try:
            if windows:
                verify_private_workspace(executable, server, root)
                return
            result = subprocess.run(
                [
                    str(executable),
                    f"--zitter-content-profile-probe={url}",
                    f"--zitter-content-probe-root={root}",
                    "--zitter-content-probe-profile=private",
                    "--zitter-content-probe-scriptlets",
                ],
                capture_output=True,
                text=True,
                timeout=45,
            )
            if result.returncode or "Zitter scriptlet probe passed" not in result.stdout.splitlines():
                raise SystemExit(
                    f"Scriptlet probe failed ({result.returncode}).\n"
                    f"{result.stdout}\n{result.stderr}"
                )
            if (not {"/scriptlets", "/allowed"}.issubset(server.hits)
                    or {"/blocked", "/bait.js"}.intersection(server.hits)):
                raise SystemExit(f"Unexpected server requests: {server.hits}")
            if any(root.iterdir()):
                raise SystemExit("The private scriptlet probe left a profile directory")
            print("Content scriptlet timing, network filtering, and cosmetics passed")
            if options.private_workspace:
                verify_private_workspace(executable, server, root)
        except subprocess.TimeoutExpired as error:
            raise SystemExit(f"Scriptlet probe timed out. Output: {error.stderr!r}") from error
        finally:
            server.shutdown()
            server_thread.join()


if __name__ == "__main__":
    main()
