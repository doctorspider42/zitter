#!/usr/bin/env python3
"""Verify macOS Shared Dictionary negotiation across Work and Personal restarts."""

import argparse
from base64 import b64encode
from hashlib import sha256
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory, TemporaryFile
from threading import Event, Thread
from urllib.parse import urlsplit

from content_file_keychain_fixture import compile_file_keychain_fixture, file_keychain_environment
from verify_content_bundle import seal_local_copy
from verify_content_fresh_startup import APP_NAME, REQUEST_QUIT, verify_startup


DICTIONARY = b"Zitter shared dictionary fixture with repeated profile data. " * 64
PAYLOAD = b"Zitter shared dictionary fixture with repeated profile data. " * 12
HASH = sha256(DICTIONARY).digest()
AVAILABLE = ":" + b64encode(HASH).decode("ascii") + ":"
DCZ_PREFIX = bytes.fromhex("5e2a4d1820000000")


class Page(BaseHTTPRequestHandler):
    def send(self, body, content_type, **headers):
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        for key, value in headers.items():
            self.send_header(key.replace("_", "-"), value)
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = urlsplit(self.path).path
        if path == "/dictionary":
            self.server.dictionary_reads += 1
            self.send(DICTIONARY, "application/octet-stream",
                      Use_As_Dictionary='match="/payload/*"', Cache_Control="max-age=3600")
        elif path.startswith("/payload/"):
            available = self.headers.get("Available-Dictionary")
            encodings = {item.strip() for item in self.headers.get("Accept-Encoding", "").split(",")}
            encoded = available == AVAILABLE and "dcz" in encodings
            self.server.requests.append((path, available, encoded))
            if encoded:
                self.send(self.server.compressed, "text/plain", Content_Encoding="dcz",
                          Cache_Control="no-store")
            else:
                self.send(PAYLOAD, "text/plain", Cache_Control="no-store")
        elif path.startswith("/result/"):
            self.server.result = path
            self.send(b"", "text/plain")
            self.server.completed.set()
        elif path in ("/seed", "/reopen", "/personal"):
            phase = path.removeprefix("/")
            self.send(f"""<!doctype html><script>
(async () => {{
  if ("{phase}" === "seed") await (await fetch("/dictionary")).arrayBuffer();
  for (let attempt = 0; attempt < 20; ++attempt) {{
    const response = await fetch("/payload/{phase}?attempt=" + attempt);
    const body = await response.text();
    const encoded = response.headers.get("Content-Encoding") === "dcz";
    if (encoded || "{phase}" === "personal") {{
      const expected = {json.dumps(PAYLOAD.decode("ascii"))};
      await fetch("/result/" + (body === expected ? "passed" : "wrong-body"));
      return;
    }}
    await new Promise(resolve => setTimeout(resolve, 50));
  }}
  await fetch("/result/no-dictionary");
}})().catch(() => fetch("/result/fetch-failed"));
</script>""".encode(), "text/html")
        else:
            self.send_error(404)


def run_phase(executable, root, env, server, phase, role):
    session_path = root / "ContentKeychainWorkspaceSession.json"
    session = json.loads(session_path.read_text(encoding="utf-8"))
    session["active_workspace"] = role
    session[role].update(active=0, tabs=[{
        "url": f"http://127.0.0.1:{server.server_port}/{phase}", "title": "Shared Dictionary",
    }])
    session_path.write_text(json.dumps(session), encoding="utf-8")
    server.requests = []
    server.dictionary_reads = 0
    server.result = None
    server.completed.clear()
    with TemporaryFile() as log:
        process = subprocess.Popen([executable], env=env, stdout=log, stderr=log)
        try:
            if not server.completed.wait(20) or server.result != "/result/passed":
                log.seek(0)
                raise ValueError(f"Shared Dictionary {phase} failed: {server.requests!r}; "
                                 f"{getattr(server, 'result', None)}\n{log.read().decode(errors='replace')}")
            if server.dictionary_reads != int(phase == "seed"):
                raise ValueError(f"Unexpected dictionary downloads in {phase}: {server.dictionary_reads}")
            relevant = [request[1:] for request in server.requests if request[0] == f"/payload/{phase}"]
            if phase == "personal":
                if relevant != [(None, False)]:
                    raise ValueError(f"Personal offered the Work dictionary: {relevant!r}")
            elif not relevant or relevant[-1] != (AVAILABLE, True):
                raise ValueError(f"Work did not decode a dictionary-compressed response: {relevant!r}")
            subprocess.run(
                ["/usr/bin/osascript", "-l", "JavaScript", "-e", REQUEST_QUIT, str(process.pid)],
                check=True, timeout=15,
            )
            if process.wait(timeout=15) != 0:
                raise ValueError(f"Shared Dictionary {phase} did not quit cleanly")
            print(f"Shared Dictionary {phase} passed in {role}")
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=5)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path, help="Prepared macOS Content app with ordinary startup enabled")
    args = parser.parse_args()
    zstd = shutil.which("zstd")
    if not zstd:
        parser.error("The dictionary fixture needs the local zstd executable")
    with (TemporaryDirectory(prefix="zitter-shared-dictionary-") as work,
          TemporaryDirectory(prefix="zitter-content-probe-") as profile,
          ThreadingHTTPServer(("127.0.0.1", 0), Page) as server):
        work, root = Path(work).resolve(), Path(profile).resolve()
        app = work / APP_NAME
        subprocess.run(["/usr/bin/ditto", args.app, app], check=True, timeout=90)
        seal_local_copy(app)
        executable = app / "Contents/MacOS/Zitter Content Lab"
        library = compile_file_keychain_fixture(work)
        env = dict(file_keychain_environment(library), ZITTER_CONTENT_STARTUP_TRIAL_ROOT=str(root))
        dictionary = work / "dictionary"
        dictionary.write_bytes(DICTIONARY)
        frame = subprocess.run([zstd, "-q", "-c", "-D", dictionary], input=PAYLOAD,
                               capture_output=True, check=True, timeout=15).stdout
        server.compressed = DCZ_PREFIX + HASH + frame
        server.completed = Event()
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            verify_startup(executable, root, env)
            for phase, role in (("seed", "work"), ("reopen", "work"), ("personal", "personal")):
                run_phase(executable, root, env, server, phase, role)
        finally:
            server.shutdown()
            thread.join()


if __name__ == "__main__":
    main()
