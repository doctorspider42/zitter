#!/usr/bin/env python3
"""Serve the restart diagnostic locally; does not automate or control the browser."""
import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import threading

ROOT = Path(__file__).resolve().parents[1]
LOCK = threading.Lock()


class Handler(SimpleHTTPRequestHandler):
    def do_POST(self):
        if self.path != "/result":
            self.send_error(404)
            return
        size = int(self.headers.get("Content-Length", 0))
        if not 0 < size <= 4096:
            self.send_error(400)
            return
        try:
            result = json.loads(self.rfile.read(size))
        except (ValueError, UnicodeError):
            self.send_error(400)
            return
        with LOCK:
            with self.server.report.open("a") as stream:
                stream.write(json.dumps(result) + "\n")
        self.send_response(204)
        self.end_headers()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8771)
    parser.add_argument("--report", type=Path,
                        default=ROOT / ".chromium/desktop-persistence.jsonl")
    args = parser.parse_args()
    args.report.parent.mkdir(parents=True, exist_ok=True)
    server = ThreadingHTTPServer(("127.0.0.1", args.port),
        partial(Handler, directory=str(ROOT / "testdata")))
    server.report = args.report
    print(f"http://127.0.0.1:{server.server_port}/persistence_probe.html", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
