#!/usr/bin/env python3
"""Exercise a signed release app without changing its resources or user profiles."""
import argparse
import http.server
import json
from pathlib import Path
import threading

from benchmark_app import ROOT, launch, write_fixture


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", required=True, type=Path)
    parser.add_argument("--results", required=True, type=Path, help="New directory for isolated profile and log")
    args = parser.parse_args()
    args.results.mkdir(parents=True, exist_ok=False)
    executable = args.app.resolve() / "Contents/MacOS/Chromium"
    class Handler(http.server.SimpleHTTPRequestHandler):
        def __init__(self, *values, **options):
            super().__init__(*values, directory=str(ROOT / "testdata"), **options)
        def log_message(self, *_):
            pass
    with http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler) as server:
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            url = f"http://127.0.0.1:{server.server_port}/performance_probe.html"
            profile = args.results / "profile"
            write_fixture(profile, url, 2, False)
            _, times = launch(executable, profile, url, "navigation", 1, 2, args.results / "app.log")
        finally:
            server.shutdown()
            thread.join()
    (args.results / "result.json").write_text(json.dumps({"status": "passed", "app": str(args.app.resolve()),
        "checks": ["page navigation", "new tab", "split view", "clean exit"], "startup": times}, indent=2) + "\n")
    print("Release smoke passed: navigation, new tab, split view and clean exit")


if __name__ == "__main__":
    main()
