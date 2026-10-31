#!/usr/bin/env python3
"""Launch the packaged Zitter app, find its windows, click, and screenshot them.

  ui.py launch [--open-panel space] [--load-extension DIR] quit the running copy, start build/ZitterRustMetal.app
                                    (and open the space panel by itself, without the mouse)
  ui.py windows                     the app's windows (id, onscreen, bounds in points)
  ui.py click X Y                   hover, then click at screen point X, Y
  ui.py rightclick X Y              the same with the right button (context menus)
  ui.py open-space-panel            click the sidebar's settings button (prefer launch --open-panel)
  ui.py shot OUT.png [--window W]   capture one app window: main (default), sidebar, panel, or an id

Screenshots capture the app's own window (screencapture -l), or for a child panel
just its area of the screen. Coordinates are macOS points; a Retina capture is 2x.
"""
import argparse, json, os, subprocess, sys, time
from pathlib import Path

if sys.platform == "win32":
    # The same commands for the Windows development build.
    sys.path.insert(0, str(Path(__file__).parent))
    import ui_win
    ui_win.main()
    sys.exit()

ROOT = Path(subprocess.run(["git", "rev-parse", "--show-toplevel"], capture_output=True, text=True,
                           cwd=Path(__file__).parent).stdout.strip())
APP = ROOT / "build/ZitterRustMetal.app"
HERE = Path(__file__).parent
BIN = Path(os.environ.get("TMPDIR", "/tmp")) / "zitter-uidrive"


def drive(*args):
    src = HERE / "uidrive.swift"
    if not BIN.exists() or BIN.stat().st_mtime < src.stat().st_mtime:
        subprocess.run(["swiftc", "-O", str(src), "-o", str(BIN)], check=True)
    return subprocess.run([str(BIN), *map(str, args)], check=True, capture_output=True, text=True).stdout


def windows():
    return [json.loads(line) for line in drive("windows", "Zitter").splitlines() if line]


def main_window():
    shown = [w for w in windows() if w["onscreen"]]
    return max(shown, key=lambda w: w["width"] * w["height"]) if shown else None


def activate():
    # An inactive app drops synthetic clicks; bring it to the front first.
    pid = subprocess.run(["pgrep", "-f", f"{APP}/Contents/MacOS/Chromium --user-data-dir"],
                         capture_output=True, text=True).stdout.split()
    if pid:
        drive("activate", pid[0])


def wait(predicate, timeout=15.0):
    end = time.time() + timeout
    while time.time() < end:
        if (value := predicate()):
            return value
        time.sleep(0.3)
    return None


def launch(a):
    if not APP.exists():
        sys.exit(f"No packaged app at {APP}; build and package first.")
    # A second instance cannot share the isolated profile; quit the old one first.
    subprocess.run(["pkill", "-TERM", "-f", f"{APP}/Contents/MacOS/Chromium --user-data-dir"])
    subprocess.run(["pkill", "-f", "tools/run_metal_sidebar.py"])
    wait(lambda: subprocess.run(["pgrep", "-f", f"{APP}/Contents/MacOS/"], capture_output=True).returncode != 0, 10)
    log = open(BIN.with_suffix(".log"), "w")
    panel = ["--open-panel", a.open_panel] if getattr(a, "open_panel", None) else []
    panel += [arg for path in getattr(a, "load_extension", None) or [] for arg in ("--load-extension", path)]
    subprocess.Popen([sys.executable, str(ROOT / "tools/run_metal_sidebar.py"), "--app", str(APP), *panel],
                     cwd=ROOT, stdout=log, stderr=log, start_new_session=True)
    w = wait(lambda: (m := main_window()) and m["width"] > 600 and m)
    if not w:
        sys.exit(f"The app did not show a window; see {log.name}")
    time.sleep(2)  # first paint and the sidebar's Metal surface
    # Only the space panel is a window find() knows; the others open in place.
    space = getattr(a, "open_panel", None) == "space"
    if space and not wait(lambda: find("panel"), 10):
        sys.exit("The app started, but the panel did not open.")
    print(json.dumps(find("panel") if space else w))


def open_space_panel(_):
    # The sidebar is its own window; its settings button sits 19 pt from the right
    # edge and 22 pt from the bottom. Clicks can land before the window is key, so retry.
    def panel():
        return find("panel")
    for _ in range(3):
        if (p := panel()):
            break
        activate()
        sidebar = find("sidebar")
        if not sidebar:
            sys.exit("No sidebar window; is the sidebar pinned and the app running?")
        drive("click", sidebar["x"] + sidebar["width"] - 19, sidebar["y"] + sidebar["height"] - 22)
        p = wait(panel, 3)
    if not p:
        sys.exit("The space panel did not open.")
    time.sleep(0.5)
    print(json.dumps(p))


def find(name):
    shown = [w for w in windows() if w["onscreen"]]
    # Window titles name the app's windows; sizes change with the main window.
    # Polish or English UI.
    titles = {"main": {"Zitter"}, "sidebar": {"Zitter — Karty", "Zitter — Tabs"},
              "panel": {"Zitter — Ustawienia przestrzeni", "Zitter — Space settings"}}
    return next((w for w in shown if w["title"] in titles.get(name, ()) or str(w["id"]) == name), None)


def shot(a):
    w = find(a.window)
    if not w:
        sys.exit(f"No {a.window} window on screen; list them with `ui.py windows`.")
    # Some panels are child windows that -l cannot capture alone; then take their
    # area of the screen (the app is frontmost after launch).
    if subprocess.run(["screencapture", "-x", "-o", "-l", str(w["id"]), a.out], capture_output=True).returncode:
        area = ",".join(str(round(w[k])) for k in ("x", "y", "width", "height"))
        subprocess.run(["screencapture", "-x", "-R", area, a.out], check=True)
    print(a.out)


parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
sub = parser.add_subparsers(dest="command", required=True)
l = sub.add_parser("launch"); l.add_argument("--open-panel", choices=["space", "sidebar", "palette", "address", "settings", "profiles", "downloads", "extensions", "extension-popup", "menu", "dialog", "toast", "add", "space-end", "space-new", "remove", "drop"],
    help="open this panel once the window is up (no mouse needed)")
l.add_argument("--load-extension", action="append", help="load an unpacked extension into the profiles (repeatable)")
l.set_defaults(run=launch)
sub.add_parser("windows").set_defaults(run=lambda _: print("\n".join(map(json.dumps, windows()))))
c = sub.add_parser("click"); c.add_argument("x", type=float); c.add_argument("y", type=float)
c.set_defaults(run=lambda a: (activate(), drive("click", a.x, a.y)))
r = sub.add_parser("rightclick"); r.add_argument("x", type=float); r.add_argument("y", type=float)
r.set_defaults(run=lambda a: (activate(), drive("rightclick", a.x, a.y)))
sub.add_parser("open-space-panel").set_defaults(run=open_space_panel)
s = sub.add_parser("shot"); s.add_argument("out"); s.add_argument("--window", default="main",
    help="main, sidebar, panel (the space panel) or a window id"); s.set_defaults(run=shot)
args = parser.parse_args()
args.run(args)
