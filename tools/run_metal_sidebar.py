#!/usr/bin/env python3
"""Run the real workspace with Rust/Metal cards in an owned, isolated profile."""
import argparse
import functools
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
import plistlib
from pathlib import Path
import subprocess
import threading
from urllib.parse import urlencode

ROOT = Path(__file__).resolve().parents[1]
MARKER = ".zitter-metal-sidebar"


def initialize_profile(profile, port):
    """Seed once; subsequent launches retain the prototype's real session."""
    profile = profile.resolve()
    if profile.exists():
        if not (profile / MARKER).is_file() or (profile / MARKER).read_text() != "isolated-metal-sidebar\n":
            raise ValueError(f"Refusing an unowned profile: {profile}")
        return
    profile.mkdir(parents=True)
    (profile / MARKER).write_text("isolated-metal-sidebar\n")
    def url(title):
        return f"http://127.0.0.1:{port}/sidebar_demo.html?{urlencode({'title': title})}"
    titles = ["Zitter · Rust + Metal", "Zażółć gęślą jaźń 🦀", "日本語 · Chromium", "مرحبا بالعالم",
              "Bardzo długi tytuł karty — sprawdzenie przycinania tekstu"]
    titles += [f"Karta {i} · przewijanie i klawiatura" for i in range(6, 25)]
    pins = [{"id": "docs", "parent_id": "", "title": "Projekt 🦀", "url": "", "folder": True},
            {"id": "readme", "parent_id": "docs", "title": "Dokumentacja", "url": url("Dokumentacja"), "folder": False}]
    configuration = {
        "profiles": [{"id": "work-profile", "name": "Prototyp", "directory": "ZitterWork"},
                     {"id": "personal-profile", "name": "Osobisty", "directory": "ZitterPersonal"}],
        "workspaces": [{"id": "work", "name": "Rust + Metal", "profile_id": "work-profile", "pins": pins},
                       {"id": "personal", "name": "Prywatne", "profile_id": "personal-profile", "pins": []}],
        "active": "work", "onboarding_complete": True}
    session = {"work": {"profile_id": "work-profile", "active": 0, "left": 0, "right": 1, "ratio": 0.5,
                         "tabs": [{"url": url(t), "title": t} for t in titles]},
               "personal": {"profile_id": "personal-profile", "active": 0, "left": 0, "right": -1,
                            "tabs": [{"url": url("Prywatne · osobny profil"), "title": "Prywatne"}]}}
    for name, data in [("ZitterWorkspaces.json", configuration), ("ZitterSession.json", session),
                       ("ZitterAppearance.json", {"sidebar_pinned": True, "sidebar_width": 300,
                                                  "workspace_animations": False})]:
        (profile / name).write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n")


class QuietHandler(SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, default=ROOT / "build/ZitterRustMetal.app")
    parser.add_argument("--profile", type=Path, default=ROOT / "build/rust-metal-profile")
    parser.add_argument("--backend", choices=["rust-metal", "views"], default="rust-metal")
    parser.add_argument("--port", type=int, default=8767)
    parser.add_argument("--raster", choices=["compositor", "metal"], default="compositor",
                        help="Rust backend painting: in-process Metal or the Chromium compositor")
    args = parser.parse_args()
    info = args.app.resolve() / "Contents/Info.plist"
    binary = args.app.resolve() / "Contents/MacOS" / plistlib.loads(info.read_bytes())["CFBundleExecutable"] if info.is_file() else info
    if not info.is_file() or not binary.is_file():
        parser.error("Package the prototype first; see docs/rust-metal-sidebar.md")
    try:
        initialize_profile(args.profile, args.port)
    except ValueError as error:
        parser.error(str(error))
    server = ThreadingHTTPServer(("127.0.0.1", args.port), functools.partial(QuietHandler, directory=str(ROOT / "testdata")))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    raster = [f"--zitter-rust-raster={args.raster}"] if args.backend == "rust-metal" and args.raster != "compositor" else []
    print(f"Sidebar: {args.backend} ({args.raster}); isolated profile: {args.profile.resolve()}", flush=True)
    try:
        return subprocess.call(["/usr/bin/open", "-W", "-n", str(args.app.resolve()), "--args",
                                f"--user-data-dir={args.profile.resolve()}",
                                f"--zitter-sidebar={args.backend}", "--no-first-run",
                                "--disable-component-update", "--disable-background-networking", "--enable-logging=stderr", *raster])
    except KeyboardInterrupt:
        return 130
    finally:
        server.shutdown()


if __name__ == "__main__":
    raise SystemExit(main())
