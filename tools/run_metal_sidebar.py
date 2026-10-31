#!/usr/bin/env python3
"""Run the real workspace with the Rust UI in an owned, isolated profile."""
import argparse
import functools
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
import plistlib
from pathlib import Path
import subprocess
import sys
import threading
from urllib.parse import urlencode

ROOT = Path(__file__).resolve().parents[1]
MARKER = ".zitter-metal-sidebar"
# Windows runs the development build's zitter.exe; macOS the packaged app.
WINDOWS = sys.platform == "win32"


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
        (profile / name).write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


class QuietHandler(SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, default=ROOT / (".chromium/src/out/ZitterWin/zitter.exe" if WINDOWS
                                                            else "build/ZitterRustMetal.app"))
    parser.add_argument("--profile", type=Path, default=ROOT / "build/rust-metal-profile")
    parser.add_argument("--port", type=int, default=8767)
    parser.add_argument("--raster", choices=["compositor", "metal"], default="compositor",
                        help="Rust UI painting: the Chromium compositor or in-process Metal")
    parser.add_argument("--open-panel", choices=["space", "sidebar", "palette", "address", "settings", "profiles", "downloads", "extensions", "extension-popup", "menu", "app-menu", "dialog", "toast", "add", "space-end", "space-new", "remove", "drop"], help="open this panel once the window is up (UI checks)")
    parser.add_argument("--load-extension", action="append", type=Path, default=[],
                        help="load an unpacked extension (UI checks of the extensions bar)")
    args = parser.parse_args()
    if WINDOWS:
        binary = args.app.resolve()
        if binary.suffix.lower() != ".exe" or not binary.is_file():
            parser.error("Build zitter.exe first; see docs/windows-port.md")
    else:
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
    extra = [f"--zitter-rust-raster={args.raster}"] if args.raster != "compositor" else []
    if args.open_panel:
        extra.append(f"--zitter-open-panel={args.open_panel}")
    if args.load_extension:
        extra.append(f"--load-extension={','.join(str(path.resolve()) for path in args.load_extension)}")
    print(f"Rust UI raster: {args.raster}; isolated profile: {args.profile.resolve()}", flush=True)
    try:
        launcher = [str(binary)] if WINDOWS else ["/usr/bin/open", "-W", "-n", str(args.app.resolve()), "--args"]
        return subprocess.call([*launcher, f"--user-data-dir={args.profile.resolve()}",
                                "--no-first-run",
                                "--disable-component-update", "--disable-background-networking", "--enable-logging=stderr", *extra])
    except KeyboardInterrupt:
        return 130
    finally:
        server.shutdown()


if __name__ == "__main__":
    raise SystemExit(main())
