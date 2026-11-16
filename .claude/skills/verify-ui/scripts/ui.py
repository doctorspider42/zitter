#!/usr/bin/env python3
"""Launch the packaged Zitter Content app, drive it and screenshot it.

  ui.py launch [--open-panel space] [--theme dark] [--raster metal] [--scale 2]
                                    quit the running copy, start build/ZitterContentLab.app in the
                                    workspace root $TMPDIR/zitter-content-probe-ui (and open the
                                    space panel by itself, without the mouse)
  ui.py launch --fresh ...          the same with an empty workspace root
  ui.py windows                     the app's windows (id, title, visible, bounds in points)
  ui.py tree [--window W] [--all]   the window's named views: name, role, bounds in window points
  ui.py click X Y | --name N        click; X Y are screen points, or window points with --window
  ui.py rightclick ...              the same with the right button (context menus)
  ui.py move / drag / scroll        hover, drag (--to X Y | --to-name N), wheel (--dy 120)
  ui.py key return [--mod cmd]      press a key; ui.py type "text" types into the focused field
  ui.py shot OUT.png [--window W]   capture one window: main (default), sidebar, panel, a title or id
  ui.py frames OUT.png --count 8    capture an animation into one contact sheet (--then-click ...)

The app runs with --zitter-automation: commands go through a socket in the workspace
root, straight to the app's widgets and pages. The system cursor never moves, the app
may stay in the background, and screenshots come from the window's compositor, so other
windows covering it don't matter. --cursor (click) and --screen (shot) fall back to the
real mouse and screencapture. Window coordinates are points; a scale-2 shot is 2x.
"""
import argparse, json, os, plistlib, secrets, shutil, socket, subprocess, sys, tempfile, time
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
SOCKET = PROFILE / "automation.sock"
WINDOW_TITLES = {"main": {"Zitter"}, "sidebar": {"Zitter — Karty", "Zitter — Tabs"},
                 "panel": {"Zitter — Ustawienia przestrzeni", "Zitter — Space settings"}}


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


def automation(command, timeout=130):
    """Send one command to the running app; returns its JSON answer."""
    if not SOCKET.exists():
        sys.exit("No automation socket; relaunch with `ui.py launch` (it passes --zitter-automation).")
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as conn:
        conn.settimeout(timeout)
        try:
            conn.connect(str(SOCKET))
        except OSError as error:
            sys.exit(f"The app does not answer on {SOCKET}: {error}")
        conn.sendall((json.dumps(command) + "\n").encode())
        data = b""
        while not data.endswith(b"\n"):
            chunk = conn.recv(65536)
            if not chunk:
                break
            data += chunk
    answer = json.loads(data or b'{"ok": false, "error": "no answer"}')
    if not answer.get("ok"):
        sys.exit(f"{command['cmd']}: {answer.get('error')}")
    return answer


def automation_ready():
    if not SOCKET.exists():
        return False
    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as conn:
            conn.settimeout(3)
            conn.connect(str(SOCKET))
            conn.sendall(b'{"cmd": "ping"}\n')
            return b'"ok":true' in conn.recv(4096)
    except OSError:
        return False


def window_ref(name):
    """main/sidebar/panel, a window id, or (part of) a title → the app's window id."""
    if name is None:
        return None
    listed = automation({"cmd": "windows"})["windows"]
    if name.isdigit():
        return int(name)
    if name == "menu":
        # Context menus are untitled windows; the frontmost one.
        for w in listed_front_to_back(listed):
            if w["visible"] and not w["title"] and w["width"] > 1:
                return w["id"]
        sys.exit("No open menu.")
    titles = WINDOW_TITLES.get(name, {name})
    for w in listed:
        if w["title"] in titles and w["visible"]:
            return w["id"]
    for w in listed:
        if w["visible"] and name.lower() in w["title"].lower():
            return w["id"]
    sys.exit(f"No visible {name} window; list them with `ui.py windows`.")


def listed_front_to_back(listed):
    # `windows` lists all windows; the visible ones come front to back first
    # only from the app's ordered list, so prefer the newest (highest id).
    return sorted(listed, key=lambda w: -w["id"])


def target(a, command):
    command = dict(command)
    if getattr(a, "window", None):
        command["window"] = window_ref(a.window)
    if getattr(a, "name", None):
        command["name"] = a.name
        if a.index is not None:
            command["index"] = a.index
    elif getattr(a, "x", None) is not None:
        command.update(x=a.x, y=a.y)
    else:
        sys.exit("Give X Y or --name.")
    if getattr(a, "mod", None):
        command["modifiers"] = a.mod
    return command


def show(answer):
    answer.pop("ok", None)
    print(json.dumps(answer, ensure_ascii=False))


def settle(a):
    time.sleep(getattr(a, "settle", 0) or 0)


def windows():
    if not getattr(windows, "cursor", False) and automation_ready():
        listed = automation({"cmd": "windows"})["windows"]
        return [dict(w, onscreen=w["visible"]) for w in listed]
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
               f"--zitter-content-workspace-root={PROFILE}",
               "--zitter-automation=front" if a.front else "--zitter-automation"]
    SOCKET.unlink(missing_ok=True)
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
    if not wait(automation_ready, 30):
        sys.exit(f"The app did not open its automation socket; see {log.name}")
    w = wait(lambda: (m := main_window()) and m["width"] > 600 and m)
    if not w:
        sys.exit(f"The app did not show a window; see {log.name}")
    time.sleep(2)  # first paint and the sidebar's Metal surface
    # Only the space panel is a window find() knows; the others open in place.
    space = getattr(a, "open_panel", None) == "space"
    if space and not wait(lambda: find("panel"), 10):
        sys.exit("The app started, but the panel did not open.")
    print(json.dumps(find("panel") if space else w))


def click(a, button="left"):
    if a.cursor:
        activate()
        drive("rightclick" if button == "right" else "click", a.x, a.y)
        return
    command = target(a, {"cmd": "click", "button": button, "count": 2 if a.double else 1,
                         "hover_ms": a.hover})
    show(automation(command))
    settle(a)


def drag(a):
    command = target(a, {"cmd": "drag", "steps": a.steps, "step_ms": a.step_ms})
    if a.to:
        command.update(to_x=a.to[0], to_y=a.to[1])
    elif a.to_name:
        command["to_name"] = a.to_name
    else:
        sys.exit("Give --to X Y or --to-name N.")
    show(automation(command))
    settle(a)


def then_command(a):
    if a.then_click:
        return {"cmd": "click", "x": a.then_click[0], "y": a.then_click[1],
                **({"window": window_ref(a.window)} if a.window else {})}
    if a.then_name:
        return {"cmd": "click", "name": a.then_name}
    if a.then_key:
        return {"cmd": "key", "key": a.then_key, **({"modifiers": a.mod} if a.mod else {})}
    return None


def frames(a):
    command = {"cmd": "frames", "path": output(a.out), "window": window_ref(a.window), "scale": a.scale,
               "count": a.count, "interval_ms": a.interval, "delay_ms": a.delay,
               "sheet": not a.separate}
    if a.columns:
        command["columns"] = a.columns
    if a.crop:
        command["crop"] = crop(a.crop)
    if a.alone:
        command["alone"] = True
    if (first := then_command(a)):
        command["start_with"] = first
    show(automation(command))


def find(name):
    shown = [w for w in windows() if w["onscreen"]]
    # Window titles name the app's windows; sizes change with the main window.
    # Polish or English UI.
    return next((w for w in shown if w["title"] in WINDOW_TITLES.get(name, ()) or str(w["id"]) == name), None)


def crop(text):
    if not text:
        return None
    x, y, width, height = map(float, text.split(","))
    return {"x": x, "y": y, "width": width, "height": height}


def output(path):
    path = Path(path).resolve()
    path.parent.mkdir(parents=True, exist_ok=True)
    return str(path)


def shot(a):
    if not a.screen:
        command = {"cmd": "shot", "path": output(a.out), "window": window_ref(a.window), "scale": a.scale}
        if a.crop:
            command["crop"] = crop(a.crop)
        if a.alone:
            command["alone"] = True
        show(automation(command))
        return
    windows.cursor = True
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
l.add_argument("--front", action="store_true",
    help="let the app come to the front (by default it stays behind your own app)")
l.set_defaults(run=launch)
sub.add_parser("windows").set_defaults(run=lambda _: print("\n".join(map(json.dumps, windows()))))


def pointer_args(p, name=True):
    p.add_argument("x", type=float, nargs="?"); p.add_argument("y", type=float, nargs="?")
    p.add_argument("--window", help="X Y are points in this window (main, sidebar, panel, menu, a title or id)")
    if name:
        p.add_argument("--name", help="the view with this accessible name (see `ui.py tree`)")
        p.add_argument("--index", type=int, help="which of several views with that name")
    p.add_argument("--mod", action="append", choices=["cmd", "shift", "alt", "ctrl"])
    p.add_argument("--settle", type=float, default=0.3, help="seconds to wait afterwards (default 0.3)")


for verb, button in (("click", "left"), ("rightclick", "right")):
    c = sub.add_parser(verb); pointer_args(c)
    c.add_argument("--double", action="store_true")
    c.add_argument("--hover", type=int, default=0, help="hover this many ms before pressing (tooltips)")
    c.add_argument("--cursor", action="store_true", help="old way: move the real mouse (screen points)")
    c.set_defaults(run=lambda a, button=button: click(a, button))
m = sub.add_parser("move", help="hover"); pointer_args(m)
m.set_defaults(run=lambda a: (show(automation(target(a, {"cmd": "move"}))), settle(a)))
d = sub.add_parser("drag"); pointer_args(d)
d.add_argument("--to", type=float, nargs=2, metavar=("X", "Y")); d.add_argument("--to-name")
d.add_argument("--steps", type=int, default=12); d.add_argument("--step-ms", type=int, default=16)
d.set_defaults(run=drag)
sc = sub.add_parser("scroll"); pointer_args(sc)
sc.add_argument("--dx", type=float, default=0); sc.add_argument("--dy", type=float, default=0,
    help="positive scrolls towards the end of the content")
sc.set_defaults(run=lambda a: (show(automation(target(a, {"cmd": "scroll", "dx": a.dx, "dy": a.dy}))), settle(a)))
k = sub.add_parser("key"); k.add_argument("key", help="return, escape, tab, space, backspace, up, a, 1, f5, ...")
k.add_argument("--mod", action="append", choices=["cmd", "shift", "alt", "ctrl"]); k.add_argument("--window")
k.add_argument("--settle", type=float, default=0.3)
k.set_defaults(run=lambda a: (show(automation({"cmd": "key", "key": a.key, "modifiers": a.mod or [],
    **({"window": window_ref(a.window)} if a.window else {})})), settle(a)))
t = sub.add_parser("type"); t.add_argument("text"); t.add_argument("--window"); t.add_argument("--settle", type=float, default=0.3)
t.set_defaults(run=lambda a: (show(automation({"cmd": "type", "text": a.text,
    **({"window": window_ref(a.window)} if a.window else {})})), settle(a)))
tr = sub.add_parser("tree"); tr.add_argument("--window", default="main"); tr.add_argument("--all", action="store_true",
    help="unnamed views too"); tr.add_argument("--json", action="store_true")


def tree(a):
    answer = automation({"cmd": "tree", "window": window_ref(a.window), "all": a.all})
    if a.json:
        show(answer)
        return
    print(f"# {answer['window']} (x y width height in window points)")
    for v in answer["views"]:
        flags = "".join(f" [{f}]" for f in ("focused", "disabled", "web") if v.get(f))
        print(f"{v['x']:5} {v['y']:5} {v['width']:5} {v['height']:5}  {v['role']:<14} {v['name']!r}{flags}")


tr.set_defaults(run=tree)
s = sub.add_parser("shot"); s.add_argument("out"); s.add_argument("--window", default="main",
    help="main, sidebar, panel (the space panel), menu (the open context menu), a title or a window id")
s.add_argument("--scale", type=float, default=1, help="pixels per point (1 = window points; 2 = Retina)")
s.add_argument("--crop", help="x,y,width,height in window points")
s.add_argument("--alone", action="store_true", help="without the window's child windows (sidebar, panels)")
s.add_argument("--screen", action="store_true", help="old way: screencapture (needs the window on screen)")
s.set_defaults(run=shot)
f = sub.add_parser("frames", help="an animation's frames on one contact sheet")
f.add_argument("out"); f.add_argument("--window", default="main")
f.add_argument("--count", type=int, default=6); f.add_argument("--interval", type=int, default=50, help="ms between frames")
f.add_argument("--delay", type=int, default=0, help="ms before the first frame")
f.add_argument("--scale", type=float, default=0.5, help="pixels per point (default 0.5 saves context)")
f.add_argument("--columns", type=int); f.add_argument("--crop", help="x,y,width,height in window points")
f.add_argument("--alone", action="store_true", help="without child windows")
f.add_argument("--separate", action="store_true", help="one file per frame instead of a sheet")
f.add_argument("--then-click", type=float, nargs=2, metavar=("X", "Y"), help="click first (window points with --window)")
f.add_argument("--then-name", help="click this named view first"); f.add_argument("--then-key")
f.add_argument("--mod", action="append", choices=["cmd", "shift", "alt", "ctrl"])
f.set_defaults(run=frames)
args = parser.parse_args()
args.run(args)
