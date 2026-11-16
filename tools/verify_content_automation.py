#!/usr/bin/env python3
"""Verify the agent UI automation channel (--zitter-automation) on macOS and Windows.

Starts the packaged app in a disposable probe workspace root and drives it only
through <root>/automation.sock: it lists windows and views, clicks the
address row by name to open the command palette, closes it with Escape, takes a
screenshot and an animation contact sheet, and checks that the system cursor
never went to the clicked point.
"""

import argparse
import ctypes
import os
from pathlib import Path
import plistlib
import secrets
import struct
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / ".claude/skills/verify-ui/scripts"))
from ui_ipc import request
PALETTE_TITLES = {"Zitter — Paleta poleceń", "Zitter — Command Center"}
SIDEBAR_TITLES = {"Zitter — Karty", "Zitter — Tabs"}


class CGPoint(ctypes.Structure):
    _fields_ = [("x", ctypes.c_double), ("y", ctypes.c_double)]


def cursor():
    if os.name == "nt":
        from ctypes import wintypes

        ctypes.windll.user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
        point = wintypes.POINT()
        if not ctypes.windll.user32.GetCursorPos(ctypes.byref(point)):
            raise ctypes.WinError()
        return point.x, point.y
    graphics = ctypes.cdll.LoadLibrary(
        "/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics")
    graphics.CGEventCreate.restype = ctypes.c_void_p
    graphics.CGEventCreate.argtypes = [ctypes.c_void_p]
    graphics.CGEventGetLocation.restype = CGPoint
    graphics.CGEventGetLocation.argtypes = [ctypes.c_void_p]
    event = graphics.CGEventCreate(None)
    point = graphics.CGEventGetLocation(event)
    ctypes.cdll.LoadLibrary(
        "/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation").CFRelease(
        ctypes.c_void_p(event))
    return round(point.x), round(point.y)


def png_size(path):
    data = Path(path).read_bytes()[:24]
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise AssertionError(f"{path} is not a PNG")
    return struct.unpack(">II", data[16:24])


def expect(answer, what):
    if not answer.get("ok"):
        raise AssertionError(f"{what}: {answer.get('error')}")
    return answer


def wait(predicate, timeout):
    end = time.time() + timeout
    while time.time() < end:
        try:
            if (value := predicate()):
                return value
        except OSError:
            pass
        time.sleep(0.25)
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path, nargs="?", default=ROOT / "build/ZitterContentLab.app")
    parser.add_argument("--theme", choices=("light", "dark"), default="light")
    args = parser.parse_args()
    app = args.app.resolve(strict=True)
    if os.name == "nt":
        executable = app
    else:
        with (app / "Contents/Info.plist").open("rb") as stream:
            executable = app / "Contents/MacOS" / plistlib.load(stream)["CFBundleExecutable"]

    with tempfile.TemporaryDirectory(prefix="zitter-content-probe-") as temporary:
        root = Path(temporary).resolve()
        os.chmod(root, 0o700)
        key = root / "probe-cookie-key"
        if os.name != "nt":
            key.write_bytes(secrets.token_bytes(32))
            key.chmod(0o600)
        sock_path = root / "automation.sock"
        log = open(root / "app.log", "w")
        process = subprocess.Popen(
            [str(executable), "--zitter-content-workspace",
             f"--zitter-content-workspace-root={root}", "--zitter-automation",
             f"--zitter-content-workspace-theme={args.theme}"],
            stdout=log, stderr=log, start_new_session=True)
        try:
            before = cursor()
            if not wait(lambda: request(sock_path, {"cmd": "ping"}, 3).get("ok"), 40):
                raise AssertionError("the automation socket did not answer")

            def window(titles):
                listed = expect(request(sock_path, {"cmd": "windows"}), "windows")["windows"]
                return next((w for w in listed if w["visible"] and w["title"] in titles), None)

            main_window = wait(lambda: (w := window({"Zitter"})) and w["width"] > 600 and w, 30)
            if not main_window:
                raise AssertionError("no main window")
            sidebar = wait(lambda: window(SIDEBAR_TITLES), 15)
            if not sidebar:
                raise AssertionError("no sidebar window")
            time.sleep(1.5)  # first paint

            shot = root / "main.png"
            answer = expect(request(sock_path, {"cmd": "shot", "window": main_window["id"],
                                             "path": str(shot), "scale": 1}), "shot")
            width, height = png_size(shot)
            client = main_window["client"]
            if abs(width - client["width"]) > 1 or abs(height - client["height"]) > 1:
                raise AssertionError(f"shot is {width}x{height}, window {client}")

            views = expect(request(sock_path, {"cmd": "tree", "window": sidebar["id"]}),
                           "tree")["views"]
            if not views:
                raise AssertionError("the sidebar has no named views")

            # The address row opens the command palette; Escape closes it.
            names = [view["name"] for view in views]
            address = next((name for name in ("Adres strony", "Page address") if name in names), None)
            if not address:
                raise AssertionError(f"no address row among {names}")
            clicked = expect(request(sock_path, {"cmd": "click", "window": sidebar["id"],
                                              "name": address}), "click")
            click_point = (round(sidebar["client"]["x"] + clicked["x"]),
                           round(sidebar["client"]["y"] + clicked["y"]))
            panel = wait(lambda: window(PALETTE_TITLES), 10)
            if not panel:
                listed = request(sock_path, {"cmd": "windows"})["windows"]
                raise AssertionError(f"the click did not open the command palette: {clicked}, {listed}")
            expect(request(sock_path, {"cmd": "type", "text": "zitter"}), "type")

            sheet = root / "frames.png"
            answer = expect(request(sock_path, {"cmd": "frames", "window": panel["id"],
                                             "path": str(sheet), "count": 4, "interval_ms": 40,
                                             "scale": 0.25, "columns": 2}), "frames")
            if len(answer["times_ms"]) != 4 or png_size(sheet)[0] < 40:
                raise AssertionError(f"unexpected contact sheet: {answer}")

            expect(request(sock_path, {"cmd": "key", "key": "escape", "window": panel["id"]}), "key")
            if not wait(lambda: not window(PALETTE_TITLES), 10):
                raise AssertionError("Escape did not close the command palette")

            # The person may move the mouse meanwhile; a cursor driven by the
            # click would sit on the clicked point.
            after = cursor()
            if after == click_point and before != after:
                raise AssertionError(f"the system cursor moved to the clicked point {after}")
        except AssertionError as error:
            print(f"Automation check failed: {error}", file=sys.stderr)
            text = (root / "app.log").read_text(errors="replace")
            # The cause first (a failed CHECK or an error), then the end of the log.
            causes = [line for line in text.splitlines()
                      if any(mark in line for mark in ("FATAL", "Check failed", ":ERROR:"))]
            print("\n".join(causes[-10:]), file=sys.stderr)
            print(text[-1500:], file=sys.stderr)
            raise SystemExit(1)
        finally:
            if process.poll() is None:
                if os.name == "nt":
                    # Chromium children can still hold the redirected log and
                    # profile files after the Windows browser is terminated.
                    subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                                   capture_output=True, timeout=10)
                else:
                    process.terminate()
            try:
                process.wait(20)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(5)
            log.close()
    print("Zitter Content UI automation passed")


if __name__ == "__main__":
    main()
