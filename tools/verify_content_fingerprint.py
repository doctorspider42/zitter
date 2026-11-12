#!/usr/bin/env python3
"""Run standalone fingerprint probes with disposable profiles."""

import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading
from urllib.parse import urlsplit


FIXTURE = Path(__file__).resolve().parents[1] / "testdata/fingerprint_probe.html"
PREFIX = "Zitter fingerprint step "
POPUP_MARKER = "Zitter Content verified fingerprint protection in an initial popup"
SURFACES = {"canvas", "pixels", "webgl", "audio"}


class FingerprintFixture(BaseHTTPRequestHandler):
    def do_GET(self):
        path = urlsplit(self.path).path
        if path not in ("/fingerprint", "/fingerprint-frame"):
            self.send_error(404)
            return
        body = FIXTURE.read_bytes()
        if self.server.cross_process_frame:
            if path == "/fingerprint":
                self.server.top_level_requests.append(self.path)
            port = self.server.server_port
            host = "localhost" if self.headers["Host"].startswith("127.0.0.1:") else "127.0.0.1"
            origin = json.dumps(f"http://{host}:{port}")
            if path == "/fingerprint-frame":
                body += f"""
<script>
const parentOrigin = {origin};
const sample = () => probe().then(result => parent.postMessage(result, parentOrigin));
addEventListener('message', event => {{
  if (event.source === parent && event.origin === parentOrigin && event.data === 'read')
    sample();
}});
sample();
</script>
""".encode()
            else:
                body += f"""
<script>
const childOrigin = {origin};
const frame = document.createElement('iframe');
let resolveChild;
addEventListener('message', event => {{
  if (event.source === frame.contentWindow && event.origin === childOrigin && resolveChild) {{
    const resolve = resolveChild;
    resolveChild = null;
    resolve(event.data);
  }}
}});
const childSample = new Promise(resolve => {{ resolveChild = resolve; }});
frame.src = childOrigin + '/fingerprint-frame';
document.documentElement.append(frame);

const blank = document.createElement('iframe');
document.documentElement.append(blank);
const initial = blank.contentWindow;
const script = initial.document.createElement('script');
script.textContent = document.scripts[0].textContent + ';window.initialFingerprint = probe();';
initial.document.documentElement.append(script);
const equal = (a, b) => Object.keys(a).every(key => a[key] === b[key]);
let previous;
function publish(top, child, empty, live = false) {{
  if (initial.location.href !== 'about:blank' || !equal(top, child) || !equal(top, empty)) {{
    const differences = Object.keys(top).filter(key => top[key] !== child[key] || top[key] !== empty[key]);
    document.title = 'Zitter fingerprint frame mismatch ' + differences.join(',');
    return;
  }}
  previous = top;
  document.title = (live ? 'Zitter fingerprint live result ' : 'Zitter fingerprint result ') +
      JSON.stringify(top);
}}
Promise.all([probe(), childSample, initial.initialFingerprint])
    .then(([top, child, empty]) => publish(top, child, empty));

async function resampleFingerprint() {{
  const deadline = performance.now() + 5000;
  document.title = 'Zitter fingerprint sampling live documents';
  do {{
    const reply = new Promise(resolve => {{ resolveChild = resolve; }});
    frame.contentWindow.postMessage('read', childOrigin);
    const [top, child, empty] = await Promise.all([probe(), reply, initial.probe()]);
    if (!equal(previous, top) && equal(top, child) && equal(top, empty)) {{
      publish(top, child, empty, true);
      return;
    }}
    await new Promise(resolve => setTimeout(resolve, 25));
  }} while (performance.now() < deadline);
  document.title = 'Zitter fingerprint frame mismatch live update';
}}

function sampleFingerprintPopup() {{
  const popup = window.open('about:blank', 'fingerprint-popup', 'popup');
  if (!popup || popup.location.href !== 'about:blank') {{
    document.title = 'Zitter fingerprint popup mismatch opening';
    return;
  }}
  const script = popup.document.createElement('script');
  script.textContent = document.scripts[0].textContent + ';window.initialFingerprint = probe();';
  popup.document.documentElement.append(script);
  popup.initialFingerprint.then(result => {{
    document.title = equal(previous, result)
        ? 'Zitter fingerprint popup passed'
        : 'Zitter fingerprint popup mismatch ' +
          Object.keys(previous).filter(key => previous[key] !== result[key]).join(',');
  }});
}}
</script>
""".encode()
        else:
            body += b"""
<script>
probe().then(result => {
  document.title = 'Zitter fingerprint result ' + JSON.stringify(result);
});
</script>
"""
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def read_results(stdout, expected_count):
    results = []
    for line in stdout.splitlines():
        if not line.startswith(PREFIX):
            continue
        step, _, payload = line[len(PREFIX):].partition(" ")
        if step != str(len(results)):
            raise ValueError(f"Unexpected step {step}")
        result = json.loads(payload)
        if not isinstance(result, dict) or result.keys() != SURFACES:
            raise ValueError(f"Incomplete sample for step {step}")
        if any(not isinstance(value, str) or not value for value in result.values()):
            raise ValueError(f"A readback surface is unavailable in step {step}")
        results.append(result)

    if len(results) != expected_count:
        raise ValueError(f"Expected {expected_count} samples, got {len(results)}")
    return results


def verify_results(stdout):
    results = read_results(stdout, 5)
    off, enabled, stable, rotated, disabled = results
    if off != disabled or enabled != stable:
        raise ValueError("Expected stable samples with unchanged protection")
    for surface in sorted(SURFACES):
        if len({off[surface], enabled[surface], rotated[surface]}) != 3:
            raise ValueError(f"Enabling or rotating protection did not change {surface}")


def verify_workspace(executable, root, url, theme, renderer, server):
    windows = os.name == "nt"
    if windows:
        from verify_content_workspace_session import WINDOWS_LOCAL_STATE, new_wrapped_windows_key

        (root / WINDOWS_LOCAL_STATE).write_text(
            json.dumps({"os_crypt": {"encrypted_key": new_wrapped_windows_key()}}),
            encoding="utf-8",
        )
    else:
        key = root / "probe-cookie-key"
        with os.fdopen(os.open(key, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), "wb") as output:
            output.write(os.urandom(32))
    work = root / "workspace-work"
    work.mkdir()
    legacy_path = work / "Preferences"
    legacy = b'{"zitter":{"fingerprint_protection":true}}'
    legacy_path.write_bytes(legacy)
    args = [
        str(executable),
        "--zitter-content-workspace",
        f"--zitter-content-workspace-root={root}",
        f"--zitter-content-workspace-probe={url}/",
        f"--zitter-content-workspace-theme={theme}",
        f"--zitter-rust-raster={renderer}",
        "--no-proxy-server",
    ]
    if server.cross_process_frame:
        args.extend(("--site-per-process", "--zitter-content-fingerprint-cross-process-frame"))

    def launch(phase):
        return subprocess.run(
            [*args, f"--zitter-content-workspace-fingerprint-probe={phase}"],
            capture_output=True, encoding="utf-8", errors="replace", timeout=90,
        )

    def sample(phase, count):
        result = launch(phase)
        try:
            if result.returncode:
                raise ValueError(f"Content Lab exited with {result.returncode}")
            if (server.cross_process_frame and phase == "change"
                    and POPUP_MARKER not in result.stdout.splitlines()):
                raise ValueError("The initial popup was not verified")
            return read_results(result.stdout, count)
        except ValueError as error:
            raise ValueError(f"{error}\n{result.stdout}\n{result.stderr}") from error

    work_on, plain, personal_on, other_site, rotated, work_off = sample("change", 6)
    if server.cross_process_frame:
        expected = [f"/fingerprint?change={step}" for step in range(4)]
        if server.top_level_requests != expected:
            raise ValueError(f"Unexpected navigation during live updates: {server.top_level_requests}")
    reopened_work, reopened_personal = sample("reopen", 2)
    if plain != work_off or plain != reopened_work:
        raise ValueError("Turning Work off did not restore unprotected reads")
    for surface in sorted(SURFACES):
        if server.cross_process_frame and rotated[surface] == other_site[surface]:
            raise ValueError(f"Live rotation did not change {surface}")
        if len({result[surface] for result in (
            work_on, plain, personal_on, other_site, rotated, reopened_personal
        )}) != 6:
            raise ValueError(f"Site, profile or session isolation failed for {surface}")

    filename = "Content Fingerprint Preferences"
    work_path = work / filename
    personal_path = root / "workspace-personal" / filename
    for path, expected in ((work_path, False), (personal_path, True)):
        if json.loads(path.read_text()) != {"zitter": {"fingerprint_protection": expected}}:
            raise ValueError(f"Unexpected fingerprint preferences in {path}")
    if legacy_path.read_bytes() != legacy:
        raise ValueError("The legacy preference was modified")

    malformed = b'{"zitter":{"fingerprint_protection":"wrong type"}}'
    work_path.write_bytes(malformed)
    result = launch("reopen")
    if (not result.returncode or
            "Could not open Content fingerprint preferences" not in result.stderr or
            work_path.read_bytes() != malformed):
        raise ValueError("Malformed fingerprint preferences were accepted or modified")
    if not windows:
        work_path.unlink()
        work_path.symlink_to(legacy_path)
        result = launch("reopen")
        if (not result.returncode or
                "Could not open Content fingerprint preferences" not in result.stderr or
                legacy_path.read_bytes() != legacy):
            raise ValueError("A symlinked fingerprint preference was accepted or modified")
    print(f"Content fingerprint workspace passed in {theme} with {renderer}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path, help="Zitter Content Lab.app or zitter_content.exe")
    parser.add_argument("--workspace", action="store_true")
    parser.add_argument("--cross-process-frame", action="store_true")
    parser.add_argument("--theme", choices=("light", "dark"), default="light")
    parser.add_argument("--renderer", choices=("compositor", "metal"), default="compositor")
    options = parser.parse_args()
    if options.cross_process_frame and not options.workspace:
        parser.error("--cross-process-frame requires --workspace")
    if os.name == "nt":
        if not options.workspace or options.renderer == "metal":
            parser.error("Windows requires --workspace with the compositor renderer")
        executable = options.app.resolve()
    else:
        executable = (options.app / "Contents/MacOS/Zitter Content Lab").resolve()
    if not executable.is_file():
        parser.error(f"App executable is missing: {executable}")

    with (
        ThreadingHTTPServer(("127.0.0.1", 0), FingerprintFixture) as server,
        tempfile.TemporaryDirectory(prefix="zitter-content-probe-") as temp,
    ):
        root = Path(temp).resolve()
        server.cross_process_frame = options.cross_process_frame
        server.top_level_requests = []
        url = f"http://127.0.0.1:{server.server_port}"
        server_thread = threading.Thread(target=server.serve_forever, daemon=True)
        server_thread.start()
        try:
            if options.workspace:
                verify_workspace(executable, root, url, options.theme, options.renderer, server)
                return
            result = subprocess.run(
                [
                    str(executable),
                    f"--zitter-content-profile-probe={url}/fingerprint",
                    f"--zitter-content-probe-root={root}",
                    "--zitter-content-probe-profile=private",
                    "--zitter-content-probe-fingerprint",
                ],
                capture_output=True,
                text=True,
                timeout=90,
            )
            if result.returncode:
                raise SystemExit(f"Content Lab exited with {result.returncode}\n{result.stderr}")
            try:
                verify_results(result.stdout)
            except ValueError as error:
                raise SystemExit(f"{error}\n{result.stdout}\n{result.stderr}") from error
            if any(root.iterdir()):
                raise SystemExit("The private fingerprint probe left a profile directory")
            print("Content fingerprint protection passed on canvas, WebGL and audio")
        except subprocess.TimeoutExpired as error:
            raise SystemExit(f"Fingerprint probe timed out. Output: {error.stderr!r}") from error
        except ValueError as error:
            raise SystemExit(str(error)) from error
        finally:
            server.shutdown()
            server_thread.join()


if __name__ == "__main__":
    main()
