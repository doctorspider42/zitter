#!/usr/bin/env python3
r"""Windows counterpart of ui.py: launch the Content build, find its windows,
click, and screenshot them.

  ui.py launch [--open-panel space] [--scale 1.25] [--theme light] [--fresh]
                                     quit the running copy, start out/ZitterWinContent/zitter.exe
                                     in the workspace root %TEMP%\zitter-content-probe-ui
  ui.py windows                      the app's top-level windows (id, title, bounds in pixels)
  ui.py click X Y | rightclick X Y   bring the app forward, then click at screen pixel X, Y
  ui.py shot OUT.png [--window W]    capture one window: main (default), sidebar, panel, or an id

The sidebar, palette and panels are translucent windows that PrintWindow leaves
blank, so shot brings the app forward and copies that window's area of the
screen, never more. Coordinates are physical pixels (this process is DPI aware).
"""
import argparse, ctypes, json, os, runpy, subprocess, sys, tempfile, time
from ctypes import wintypes
from pathlib import Path

ROOT = Path(subprocess.run(["git", "rev-parse", "--show-toplevel"], capture_output=True, text=True,
                           cwd=Path(__file__).parent).stdout.strip())
# ZITTER_UI_OUT picks another build directory than out/ZitterWinContent.
EXE = ROOT / ".chromium/src" / os.environ.get("ZITTER_UI_OUT", "out/ZitterWinContent") / "zitter.exe"
# The Windows Content app opens a workspace root only directly under %TEMP%
# with this name prefix. It persists between launches, apart from the user's data.
PROFILE = Path(tempfile.gettempdir()).resolve() / "zitter-content-probe-ui"
LOG = ROOT / "build/win-ui.log"

user32, dwmapi, kernel32 = ctypes.windll.user32, ctypes.windll.dwmapi, ctypes.windll.kernel32
user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))  # per-monitor v2: real pixels


def processes():
    """zitter.exe processes running in this profile (the browser and its children)."""
    query = f"Name='zitter.exe'"
    out = subprocess.run(["powershell", "-NoProfile", "-Command",
                          f"Get-CimInstance Win32_Process -Filter \"{query}\" | "
                          "ForEach-Object { \"$($_.ProcessId)`t$($_.CommandLine)\" }"],
                         capture_output=True, text=True,
                         creationflags=subprocess.CREATE_NO_WINDOW).stdout
    pids = []
    for line in out.splitlines():
        pid, _, command = line.partition("\t")
        if str(PROFILE) in command and pid.isdigit():
            pids.append(int(pid))
    return pids


def windows():
    pids, found = set(processes()), []
    def visit(hwnd, _):
        pid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if pid.value in pids and user32.IsWindowVisible(hwnd):
            rect = wintypes.RECT()
            # The visible frame, without Windows 10's invisible resize borders.
            if dwmapi.DwmGetWindowAttribute(hwnd, 9, ctypes.byref(rect), ctypes.sizeof(rect)):
                user32.GetWindowRect(hwnd, ctypes.byref(rect))
            title = ctypes.create_unicode_buffer(256)
            user32.GetWindowTextW(hwnd, title, 256)
            found.append({"id": hwnd, "title": title.value, "onscreen": True, "x": rect.left, "y": rect.top,
                          "width": rect.right - rect.left, "height": rect.bottom - rect.top})
        return True
    user32.EnumWindows(ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)(visit), 0)
    return found


def main_window():
    shown = [w for w in windows() if w["title"] == "Zitter"]
    return max(shown, key=lambda w: w["width"] * w["height"]) if shown else None


def find(name):
    titles = {"main": {"Zitter"}, "sidebar": {"Zitter — Karty", "Zitter — Tabs"},
              "panel": {"Zitter — Ustawienia przestrzeni", "Zitter — Space settings"}}
    if name == "main":
        return main_window()
    return next((w for w in windows() if w["title"] in titles.get(name, ()) or str(w["id"]) == name), None)


def activate():
    w = main_window()
    if not w:
        sys.exit("No Zitter window on the working desktop. Use `ui.py launch --front` for physical input.")
    # Windows lets a process move another window forward only after input; an
    # Alt press counts. SW_RESTORE first, in case it is minimized.
    user32.keybd_event(0x12, 0, 0, 0)
    user32.keybd_event(0x12, 0, 2, 0)
    user32.ShowWindow(w["id"], 9)
    user32.SetForegroundWindow(w["id"])
    time.sleep(0.6)


def launch(_):
    runpy.run_path(str(Path(__file__).with_name("ui.py")), run_name="__main__")


def click(a, button):
    activate()
    user32.SetCursorPos(int(a.x), int(a.y))
    time.sleep(0.2)
    down, up = (0x0002, 0x0004) if button == "left" else (0x0008, 0x0010)
    user32.mouse_event(down, 0, 0, 0, 0)
    user32.mouse_event(up, 0, 0, 0, 0)


def shot(a):
    from PIL import ImageGrab
    w = find(a.window)
    if not w:
        sys.exit(f"No {a.window} window on screen; list them with `ui.py windows`.")
    if not a.as_is:
        activate()
    box = (w["x"], w["y"], w["x"] + w["width"], w["y"] + w["height"])
    Path(a.out).parent.mkdir(parents=True, exist_ok=True)
    ImageGrab.grab(bbox=box, all_screens=True).save(a.out)
    print(a.out)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    l = sub.add_parser("launch")
    l.add_argument("--open-panel", choices=["assistant", "assistant-activity", "space", "sidebar", "palette", "address", "settings", "assistant-settings", "privacy", "profiles", "downloads",
                                            "history", "time-machine", "extensions", "extension-popup", "fingerprint", "shield", "night-shift", "ad-blocking",
                                            "menu", "app-menu", "dialog", "toast",
                                            "add", "space-end", "space-new", "remove", "drop"],
                   help="open this panel once the window is up (no mouse needed)")
    l.add_argument("--scale", type=float, help="force a device scale factor, as 1.25 or 1.5 (HiDPI)")
    l.add_argument("--theme", choices=["dark", "light"], help="Noc (dark) or Papier (light)")
    l.add_argument("--fresh", action="store_true", help="start from an empty workspace root")
    l.add_argument("--front", action="store_true", help="use the working desktop")
    l.set_defaults(run=launch)
    sub.add_parser("windows").set_defaults(run=lambda _: print("\n".join(map(json.dumps, windows()))))
    for name, button in (("click", "left"), ("rightclick", "right")):
        c = sub.add_parser(name)
        c.add_argument("x", type=float)
        c.add_argument("y", type=float)
        c.set_defaults(run=lambda a, button=button: click(a, button))
    s = sub.add_parser("shot")
    s.add_argument("out")
    s.add_argument("--window", default="main", help="main, sidebar, panel (the space panel) or a window id")
    s.add_argument("--as-is", action="store_true",
                   help="don't bring the app forward (keeps open menus, which an Alt press closes)")
    s.set_defaults(run=shot)
    args = parser.parse_args()
    args.run(args)


if __name__ == "__main__":
    main()
