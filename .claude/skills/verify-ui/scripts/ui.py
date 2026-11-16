#!/usr/bin/env python3
"""Launch the packaged Zitter Content app, find its windows, click, and screenshot them.

  ui.py launch [--open-panel space] [--theme dark] [--raster metal] [--scale 2]
                                    quit the running copy, start build/ZitterContentLab.app in the
                                    workspace root $TMPDIR/zitter-content-probe-ui (and open the
                                    space panel by itself, without the mouse)
  ui.py launch --fresh ...          the same with an empty workspace root
  ui.py windows                     the app's windows (id, onscreen, bounds in points)
  ui.py click X Y                   hover, then click at screen point X, Y
  ui.py rightclick X Y              the same with the right button (context menus)
  ui.py open-space-panel            click the sidebar's settings button (prefer launch --open-panel)
  ui.py shot OUT.png [--window W]   capture one app window: main (default), sidebar, panel, or an id

Screenshots capture the app's own window (screencapture -l), or for a child panel
just its area of the screen. Coordinates are macOS points; a Retina capture is 2x.
"""
import argparse, json, os, plistlib, secrets, shutil, subprocess, sys, tempfile, time
from pathlib import Path

if sys.platform == "win32":
    # The same commands for the Windows development build.
    sys.path.insert(0, str(Path(__file__).parent))
    import ui_win
    ui_win.main()
    sys.exit()

ROOT = Path(subprocess.run(["git", "rev-parse", "--show-toplevel"], capture_output=True, text=True,
                           cwd=Path(__file__).parent).stdout.strip())
APP = Path(os.environ.get("ZITTER_UI_APP", ROOT / "build/ZitterContentLab.app"))
# A persistent workspace root: the app keeps its spaces, tabs and settings here
# between launches, apart from the user's own Zitter data. The app accepts a
# workspace root only as a private directory directly in $TMPDIR named
# zitter-content-probe-*.
PROFILE = Path(tempfile.gettempdir()).resolve() / "zitter-content-probe-ui"
HERE = Path(__file__).parent
BIN = Path(os.environ.get("TMPDIR", "/tmp")) / "zitter-uidrive"


def drive(*args):
    src = HERE / "uidrive.swift"
    if not BIN.exists() or BIN.stat().st_mtime < src.stat().st_mtime:
        subprocess.run(["swiftc", "-O", str(src), "-o", str(BIN)], check=True)
    return subprocess.run([str(BIN), *map(str, args)], check=True, capture_output=True, text=True).stdout


def app_info():
    with (APP / "Contents/Info.plist").open("rb") as stream:
        return plistlib.load(stream)


def executable():
    return APP / "Contents/MacOS" / app_info()["CFBundleExecutable"]


# The browser process of this workspace root; its helpers don't carry the switch.
# The user's own Zitter has the same bundle name, so windows are matched by process.
ROOT_SWITCH = f"zitter-content-workspace-root={PROFILE}"


def running():
    return subprocess.run(["pgrep", "-f", ROOT_SWITCH], capture_output=True, text=True).stdout.split()


def windows():
    pids = running()
    return [json.loads(line) for pid in pids for line in drive("windows", pid).splitlines() if line]


def main_window():
    shown = [w for w in windows() if w["onscreen"]]
    return max(shown, key=lambda w: w["width"] * w["height"]) if shown else None


def activate():
    # An inactive app drops synthetic clicks; bring it to the front first.
    pid = running()
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
    if not (APP / "Contents/Info.plist").is_file():
        sys.exit(f"No packaged app at {APP}; build and package first (build_app.sh).")
    # A second instance cannot share the workspace root; quit the old one first.
    subprocess.run(["pkill", "-TERM", "-f", ROOT_SWITCH])
    if not wait(lambda: not running(), 10):
        sys.exit("The running copy did not quit.")
    if a.fresh and PROFILE.exists():
        shutil.rmtree(PROFILE)
    PROFILE.mkdir(parents=True, exist_ok=True)
    PROFILE.chmod(0o700)
    key = PROFILE / "probe-cookie-key"
    if not key.is_file():
        # The workspace encrypts cookies with this private key instead of the
        # login keychain, so a test root never touches the user's keychain item.
        key.write_bytes(secrets.token_bytes(32))
        key.chmod(0o600)
    command = [str(executable()), "--zitter-content-workspace",
               f"--zitter-content-workspace-root={PROFILE}"]
    if a.open_panel:
        command.append(f"--zitter-open-panel={a.open_panel}")
    if a.theme:
        command.append(f"--zitter-content-workspace-theme={a.theme}")
    if a.raster:
        command.append(f"--zitter-rust-raster={a.raster}")
    if a.scale:
        command.append(f"--force-device-scale-factor={a.scale:g}")
    log = open(BIN.with_suffix(".log"), "w")
    subprocess.Popen(command, cwd=ROOT, stdout=log, stderr=log, start_new_session=True)
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
l = sub.add_parser("launch"); l.add_argument("--open-panel", choices=["assistant", "assistant-activity", "space", "sidebar", "palette", "address", "settings", "assistant-settings", "privacy", "profiles", "downloads", "history", "rss", "time-machine", "extensions", "extension-popup", "fingerprint", "shield", "night-shift", "ad-blocking", "menu", "app-menu", "dialog", "toast", "add", "space-end", "space-new", "remove", "drop", "passwords", "site", "site-data", "permission"],
    help="open this panel once the window is up (no mouse needed)")
l.add_argument("--theme", choices=["dark", "light"], help="Noc (dark) or Papier (light); default: the saved setting")
l.add_argument("--raster", choices=["compositor", "metal"], help="Rust UI painting (default: compositor)")
l.add_argument("--scale", type=float, help="force a device scale factor (HiDPI checks)")
l.add_argument("--fresh", action="store_true", help="start from an empty workspace root")
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
