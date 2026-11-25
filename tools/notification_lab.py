#!/usr/bin/env python3
"""Test bench for Zitter notifications on Windows.

  python tools/notification_lab.py serve     # the lab page on http://127.0.0.1:8765
  python tools/notification_lab.py doctor    # what Windows does with Zitter's toasts
  python tools/notification_lab.py toast     # a toast for Zitter's ID, sent without Zitter

`serve` hosts testdata/notification_probe.html and its service worker. Open the
printed URL in Zitter, allow notifications and use the buttons.

`doctor` reads the registry: the AppUserModelID registration that Windows needs
before it shows toasts from an unpackaged app, the account-wide toast switch
and Zitter's own switch in Settings → System → Notifications.

`toast` asks Windows, through PowerShell, to show a toast under Zitter's
AppUserModelID. If it appears while Zitter's own toasts don't, the fault is in
Zitter; if it doesn't appear either, Windows (settings, Focus Assist,
registration) is blocking it.

Use --dev for the Dev build's ID (Zitter.Dev.Browser).
"""

import argparse
import functools
import http.server
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TESTDATA = ROOT / "testdata"
STABLE_APP_ID = "Zitter.Browser"
DEV_APP_ID = "Zitter.Dev.Browser"
PRODUCT_NAMES = {STABLE_APP_ID: "Zitter", DEV_APP_ID: "Zitter Dev"}
PAGE = "notification_probe.html"

NOTIFICATION_SETTINGS = r"Software\Microsoft\Windows\CurrentVersion\Notifications\Settings"
PUSH_NOTIFICATIONS = r"Software\Microsoft\Windows\CurrentVersion\PushNotifications"
APP_USER_MODEL_IDS = r"Software\Classes\AppUserModelId"


class LabHandler(http.server.SimpleHTTPRequestHandler):
    # Windows can map .js to text/plain in the registry; service workers then
    # fail to register.
    extensions_map = {
        **http.server.SimpleHTTPRequestHandler.extensions_map,
        ".js": "text/javascript",
        ".html": "text/html; charset=utf-8",
    }

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, format, *args):
        sys.stderr.write("  %s\n" % (format % args))


def serve(port):
    handler = functools.partial(LabHandler, directory=str(TESTDATA))
    server = http.server.ThreadingHTTPServer(("127.0.0.1", port), handler)
    print(f"Notification lab: http://127.0.0.1:{port}/{PAGE}")
    print("Open it in Zitter. Ctrl+C stops the server.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


def read_registry(path, name):
    """A value under HKEY_CURRENT_USER, or None. With no name: True if the key exists."""
    import winreg
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, path) as key:
            return True if name is None else winreg.QueryValueEx(key, name)[0]
    except OSError:
        return None


def start_menu_shortcuts(product):
    name = product + ".lnk"
    folders = [
        Path(os.environ.get("APPDATA", "")) / "Microsoft/Windows/Start Menu/Programs",
        Path(os.environ.get("PROGRAMDATA", "")) / "Microsoft/Windows/Start Menu/Programs",
    ]
    return [folder / name for folder in folders if (folder / name).exists()]


def diagnose(app_id, read, shortcuts):
    """Findings as (level, text) pairs; level is "ok", "warn" or "fail".

    `read(path, name)` returns an HKCU registry value or None (with name None:
    whether the key exists); `shortcuts` lists
    the Start menu shortcuts named after the product.
    """
    findings = []
    if read(PUSH_NOTIFICATIONS, "ToastEnabled") == 0:
        findings.append(("fail", "Notifications are off for the whole account "
                                 "(Settings → System → Notifications)."))
    else:
        findings.append(("ok", "Notifications are on for the account."))

    registered = read(rf"{APP_USER_MODEL_IDS}\{app_id}", "DisplayName")
    if shortcuts:
        findings.append(("ok", f"Start menu shortcut: {shortcuts[0]}"))
    elif registered:
        icon = read(rf"{APP_USER_MODEL_IDS}\{app_id}", "IconUri")
        findings.append(("ok", f'Registered as "{registered}" without a shortcut'
                               + (f" (icon {icon})." if icon else ", no icon.")))
    else:
        findings.append(("fail", f"{app_id} has neither a Start menu shortcut nor a registry "
                                 "entry, so Windows drops its toasts. Start Zitter once with "
                                 "Windows notifications on, or install it."))

    app_settings = rf"{NOTIFICATION_SETTINGS}\{app_id}"
    if read(app_settings, "Enabled") == 0:
        findings.append(("fail", "Zitter's notifications are off in "
                                 "Settings → System → Notifications."))
    elif read(app_settings, "ShowInActionCenter") == 0:
        findings.append(("warn", "Zitter's toasts skip the notification center: one you miss "
                                 "is gone."))
    elif not read(app_settings, None):
        findings.append(("warn", "Windows has no settings for Zitter yet. It adds them after "
                                 "the first toast."))
    else:
        findings.append(("ok", "Zitter's notifications are on in Windows."))
    findings.append(("warn", "Focus Assist (Do not disturb) hides banners without a trace in "
                             "the registry; check the clock area in the taskbar."))
    return findings


def doctor(app_id):
    if sys.platform != "win32":
        sys.exit("doctor reads the Windows registry; run it on Windows.")
    marks = {"ok": "OK  ", "warn": "WARN", "fail": "FAIL"}
    print(f"Windows notifications for {app_id}:")
    findings = diagnose(app_id, read_registry, start_menu_shortcuts(PRODUCT_NAMES[app_id]))
    for level, text in findings:
        print(f"  [{marks[level]}] {text}")
    return 1 if any(level == "fail" for level, _ in findings) else 0


def powershell_toast_script(app_id, title, body):
    def quote(text):
        return "'" + text.replace("'", "''") + "'"

    return "\n".join([
        "$ErrorActionPreference = 'Stop'",
        "[Windows.UI.Notifications.ToastNotificationManager, Windows.UI.Notifications,"
        " ContentType = WindowsRuntime] > $null",
        "[Windows.Data.Xml.Dom.XmlDocument, Windows.Data.Xml.Dom, ContentType = WindowsRuntime]"
        " > $null",
        "$xml = New-Object Windows.Data.Xml.Dom.XmlDocument",
        "$xml.LoadXml('<toast><visual><binding template=\"ToastGeneric\">"
        "<text></text><text></text></binding></visual></toast>')",
        "$texts = $xml.GetElementsByTagName('text')",
        f"$texts.Item(0).AppendChild($xml.CreateTextNode({quote(title)})) > $null",
        f"$texts.Item(1).AppendChild($xml.CreateTextNode({quote(body)})) > $null",
        f"$notifier = [Windows.UI.Notifications.ToastNotificationManager]::CreateToastNotifier({quote(app_id)})",
        "Write-Output ('Notifier setting: ' + [string]$notifier.Setting)",
        "$notifier.Show([Windows.UI.Notifications.ToastNotification]::new($xml))",
        "Write-Output 'Windows accepted the toast.'",
    ])


def toast(app_id):
    if sys.platform != "win32":
        sys.exit("toast talks to Windows; run it on Windows.")
    script = powershell_toast_script(app_id, "Zitter notification lab",
                                     "Sent by tools/notification_lab.py, not by Zitter.")
    result = subprocess.run(["powershell", "-NoProfile", "-NonInteractive", "-Command", script],
                            capture_output=True, text=True)
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)
    if result.returncode == 0:
        print("If no toast appeared, Windows hides it: run `doctor` and check Focus Assist.")
    return result.returncode


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=["serve", "doctor", "toast"])
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--dev", action="store_true", help="use the Dev build's AppUserModelID")
    args = parser.parse_args(argv)
    app_id = DEV_APP_ID if args.dev else STABLE_APP_ID
    if args.command == "serve":
        serve(args.port)
        return 0
    if args.command == "doctor":
        return doctor(app_id)
    return toast(app_id)


if __name__ == "__main__":
    sys.exit(main())
