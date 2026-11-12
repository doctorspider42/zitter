#!/usr/bin/env python3
"""Exercise Content clipboard permission with a disposable loopback document."""

import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import subprocess
import tempfile
import threading


SCRIPT = """
  const payload = 'zitter-clipboard-fixture';
  const denied = async (operation) => {
    try { await operation(); return false; }
    catch (error) { return error.name === 'NotAllowedError'; }
  };
  const finish = (passed) => {
    if (self === top)
      document.title = 'Zitter clipboard probe ' + (passed ? 'passed' : 'failed');
    else
      parent.postMessage({result: passed}, location.origin);
  };

  async function init() {
    const mode = location.pathname.split('/').at(-1);
    if (mode === 'child') {
      document.querySelector('button').onclick = async () => {
        const write = await navigator.permissions.query({name: 'clipboard-write'});
        finish(navigator.userActivation.isActive && document.hasFocus() &&
               write.state === 'denied' &&
               await denied(() => navigator.clipboard.writeText(payload)));
      };
      parent.postMessage({ready: true}, location.origin);
      return;
    }
    if (mode === 'frame') {
      addEventListener('message', ({origin, source, data}) => {
        if (origin !== location.origin || source !== document.querySelector('iframe').contentWindow)
          return;
        if (data.ready) document.title = 'Zitter clipboard probe ready';
        if ('result' in data) finish(data.result);
      });
      document.querySelector('iframe').src = '/clipboard/child';
      return;
    }

    document.title = 'Zitter clipboard probe needs focus';
    const deadline = performance.now() + 3000;
    while (!document.hasFocus() && performance.now() < deadline)
      await new Promise(resolve => setTimeout(resolve, 25));
    if (!document.hasFocus()) {
      await fetch('/clipboard/report?focus=false');
      finish(false);
      return;
    }

    const read = await navigator.permissions.query({name: 'clipboard-read'});
    const write = await navigator.permissions.query({name: 'clipboard-write'});
    const withoutGesture = await denied(() => navigator.clipboard.writeText(payload));
    if (navigator.userActivation.isActive || read.state !== 'prompt' ||
        write.state !== 'prompt' || !withoutGesture) {
      await fetch('/clipboard/report?' + new URLSearchParams({
        read: read.state, write: write.state, withoutGesture,
        active: navigator.userActivation.isActive,
      }));
      finish(false);
      return;
    }
    if (mode === 'no-gesture') {
      finish(true);
      return;
    }

    document.querySelector('button').onclick = async () => {
      if (!navigator.userActivation.isActive) {
        finish(false);
        return;
      }
      if (mode === 'custom') {
        const item = new ClipboardItem({'web text/plain': new Blob([payload], {type: 'text/plain'})});
        finish(await denied(() => navigator.clipboard.write([item])));
        return;
      }
      try { await navigator.clipboard.writeText(payload); finish(true); }
      catch (error) { finish(false); }
    };
    document.title = 'Zitter clipboard probe ready';
  }
  init().catch(() => finish(false));
"""
CASES = ("primary", "no-gesture", "frame", "custom")


class ClipboardFixture(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path.startswith("/clipboard/report?"):
            self.send_response(204)
            self.end_headers()
            return
        mode = self.path.removeprefix("/clipboard/")
        if not self.path.startswith("/clipboard/") or mode not in (*CASES, "child"):
            self.send_error(404)
            return
        element = "<iframe></iframe>" if mode == "frame" else "<button>Copy fixture</button>"
        body = f"""<!doctype html><meta charset=utf-8>
        <style>body {{ margin: 0 }} button {{ width: 200px; height: 55px }}
        iframe {{ display: block; border: 0; width: 300px; height: 100px }}</style>
        {element}<script>{SCRIPT}</script>""".encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path, help="Zitter Content Lab.app")
    options = parser.parse_args()
    executable = (options.app / "Contents/MacOS/Zitter Content Lab").resolve()
    if not executable.is_file():
        parser.error(f"Content Lab executable not found: {executable}")

    with ThreadingHTTPServer(("127.0.0.1", 0), ClipboardFixture) as server:
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            for mode in CASES:
                with tempfile.TemporaryDirectory(prefix="zitter-content-probe-") as temp:
                    root = Path(temp).resolve()
                    url = f"http://127.0.0.1:{server.server_port}/clipboard/{mode}"
                    args = [
                        executable, "--zitter-content-clipboard-probe", "--no-proxy-server",
                        f"--zitter-content-profile-probe={url}",
                        f"--zitter-content-probe-root={root}",
                        "--zitter-content-probe-profile=private",
                    ]
                    try:
                        result = subprocess.run(args, capture_output=True, text=True, timeout=30)
                    except subprocess.TimeoutExpired as error:
                        raise SystemExit(f"{mode} timed out: {error.stderr!r}") from error
                    if result.returncode or "Zitter Content clipboard probe passed" not in result.stdout:
                        raise SystemExit(f"{mode} failed.\n{result.stdout}\n{result.stderr}")
                    if any(root.iterdir()):
                        raise SystemExit(f"{mode} left a private profile directory")
                    print(f"Content clipboard permission passed: {mode}")
        finally:
            server.shutdown()
            thread.join()


if __name__ == "__main__":
    main()
