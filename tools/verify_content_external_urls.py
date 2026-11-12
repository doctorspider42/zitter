#!/usr/bin/env python3
"""Verify cold and warm LaunchServices URL delivery in a disposable Workspace."""

import argparse
from contextlib import ExitStack, contextmanager
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import plistlib
import os
import signal
import subprocess
from tempfile import TemporaryDirectory
from threading import Event, Thread
import time

from content_file_keychain_fixture import compile_file_keychain_fixture, file_keychain_environment
from notarize_app import verify_nested_signatures
from package_app import BUNDLE_ID
from package_dmg import verify_app
from verify_content_bundle import seal_local_copy
from verify_content_fresh_startup import (REQUEST_QUIT, startup_holds_lock,
                                          system_keychain_item_exists, unused_system_profile_root)
from verify_content_tls import make_tls_fixture


OPEN_URL = r"""
import AppKit

let config = NSWorkspace.OpenConfiguration()
config.createsNewApplicationInstance = CommandLine.arguments[3] == "cold"
config.environment = ProcessInfo.processInfo.environment.filter {
    $0.key.hasPrefix("DYLD_") || $0.key.hasPrefix("ZITTER_CONTENT_")
}
let appURL = URL(fileURLWithPath: CommandLine.arguments[1]).resolvingSymlinksInPath()
let conflicting = NSWorkspace.shared.urlsForApplications(withBundleIdentifier: "dev.zitter.content-launch-probe").contains {
    let installed = $0.resolvingSymlinksInPath()
    return installed.path != appURL.path && FileManager.default.fileExists(atPath: installed.path)
}
guard !conflicting else {
    fputs("Use a GUI session without another registered Content launch probe.\n", stderr)
    exit(1)
}
let page = URL(string: CommandLine.arguments[2])!
let registration = LSRegisterURL(appURL as CFURL, true)
let handlers = NSWorkspace.shared.urlsForApplications(toOpen: page)
guard registration == noErr, handlers.contains(where: {
    $0.resolvingSymlinksInPath().path == appURL.path
}) else {
    let known = NSWorkspace.shared.urlsForApplications(withBundleIdentifier: "dev.zitter.content-launch-probe")
    fputs("LaunchServices did not register the probe as a \(page.scheme ?? "URL") handler (status \(registration), \(known.count) registrations, \(handlers.count) handlers).\n", stderr)
    exit(1)
}
NSWorkspace.shared.open([page], withApplicationAt: appURL, configuration: config) { app, error in
    guard let app else {
        fputs("\(String(describing: error))\n", stderr)
        exit(1)
    }
    print(app.processIdentifier)
    exit(0)
}
RunLoop.main.run()
"""


SEND_EVENT = r"""
import AppKit
let target = NSAppleEventDescriptor(processIdentifier: Int32(CommandLine.arguments[1])!)
let event = NSAppleEventDescriptor(eventClass: 0x4755524c, eventID: 0x4755524c,
                                  targetDescriptor: target, returnID: -1, transactionID: 0)
event.setParam(NSAppleEventDescriptor(string: CommandLine.arguments[2]), forKeyword: 0x2d2d2d2d)
do {
    _ = try event.sendEvent(options: [.noReply, .neverInteract], timeout: 5)
} catch {
    fputs("\(error)\n", stderr)
    exit(1)
}
"""

OPEN_DEFAULT_URL = r"""
import AppKit

let app = URL(fileURLWithPath: CommandLine.arguments[1]).resolvingSymlinksInPath()
for scheme in ["http", "https"] {
    let probe = URL(string: "\(scheme)://127.0.0.1/")!
    guard NSWorkspace.shared.urlForApplication(toOpen: probe)?.resolvingSymlinksInPath().path == app.path else {
        fputs("The selected Zitter app is not the default handler for \(scheme).\n", stderr)
        exit(1)
    }
}
if CommandLine.arguments.count > 2 {
    let page = URL(string: CommandLine.arguments[2])!
    guard NSWorkspace.shared.open(page) else {
        fputs("LaunchServices could not open the URL with the default handler.\n", stderr)
        exit(1)
    }
}
"""


class Page(BaseHTTPRequestHandler):
    def do_GET(self):
        self.server.requested.set()
        self.send_response(200)
        self.end_headers()
        if self.path in self.server.commits:
            self.server.commits[self.path].set()
        elif self.path in ("/cold", "/warm"):
            self.wfile.write(b"<title>External link</title><script>fetch(location.pathname + '/committed')</script>")


def browser_pid(executable):
    listing = subprocess.check_output(["/bin/ps", "-axo", "pid=,command="], text=True)
    for line in listing.splitlines():
        fields = line.split(maxsplit=1)
        if len(fields) == 2 and (fields[1] == str(executable)
                                 or fields[1].startswith(f"{executable} http")):
            return int(fields[0])
    return None


@contextmanager
def serve_pages(tls=None):
    with ThreadingHTTPServer(("127.0.0.1", 0), Page) as server:
        server.requested = Event()
        server.commits = {f"/{phase}/committed": Event() for phase in ("cold", "warm")}
        handshake = Event()
        if tls:
            tls.set_servername_callback(lambda *_: handshake.set())
            server.socket = tls.wrap_socket(server.socket, server_side=True)
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            yield server, handshake
        finally:
            server.shutdown()
            thread.join()


def verify_urls(app, root, staging, library=None, *, arguments=False, apple_event=False,
                https=False, system_default=False):
    executable = app / "Contents/MacOS/Zitter Content Lab"
    env = (dict(file_keychain_environment(library), ZITTER_CONTENT_STARTUP_TRIAL_ROOT=str(root))
           if library else None)
    opener = staging / "open-url"
    source = opener.with_suffix(".swift")
    if not arguments:
        swift = OPEN_DEFAULT_URL if system_default else SEND_EVENT if apple_event else OPEN_URL
        source.write_text(swift, encoding="utf-8")
        subprocess.run(["/usr/bin/xcrun", "swiftc", source, "-o", opener], check=True, timeout=90)
    if system_default:
        selection = subprocess.run([opener, app], capture_output=True, text=True, timeout=10)
        if selection.returncode:
            raise SystemExit(selection.stderr.strip() or "Could not check the default browser")
        root = unused_system_profile_root()
        if browser_pid(executable) is not None:
            raise ValueError("Quit the default Zitter app before starting the cold URL check")
    with ExitStack() as stack:
        pages = []
        if https or system_default:
            for phase in (("warm",) if system_default else ("cold", "warm")):
                directory = staging / phase
                directory.mkdir()
                _, tls = make_tls_fixture(directory, "openssl")
                server, handshake = stack.enter_context(serve_pages(tls))
                pages.append((f"https://127.0.0.1:{server.server_port}/{phase}", handshake, server))
            warm_url, warm_loaded, _ = pages[-1]
        if https:
            cold_url, cold_loaded, _ = pages[0]
        else:
            server, _ = stack.enter_context(serve_pages())
            origin = f"http://127.0.0.1:{server.server_port}"
            cold_url, cold_loaded = origin + "/cold", server.commits["/cold/committed"]
            if not system_default:
                warm_url, warm_loaded = origin + "/warm", server.commits["/warm/committed"]
        pid = None
        process = None
        try:
            if system_default:
                subprocess.run([opener, app, cold_url], check=True, timeout=10)
                if not cold_loaded.wait(15) or (pid := browser_pid(executable)) is None:
                    raise ValueError("The default Zitter app did not load the cold HTTP URL")
                subprocess.run([opener, app, warm_url], check=True, timeout=10)
                if not warm_loaded.wait(15) or browser_pid(executable) != pid:
                    raise ValueError("The warm HTTPS URL did not reach the same default Zitter process")
            elif apple_event:
                process = subprocess.Popen([executable], env=env)
                pid = process.pid
                deadline = time.monotonic() + 15
                while process.poll() is None and not startup_holds_lock(root / "WorkspaceLock"):
                    if time.monotonic() >= deadline:
                        raise ValueError("Apple Event candidate did not open its profile root")
                    time.sleep(0.05)
                subprocess.run([opener, str(pid), cold_url], check=True, timeout=10)
                if not cold_loaded.wait(15):
                    raise ValueError("The direct startup Apple Event URL did not commit")
                subprocess.run([opener, str(pid), warm_url], check=True, timeout=10)
            elif arguments:
                for invalid in (origin.replace("://", "://user@") + "/cold", "file:///tmp/invalid"):
                    rejected = subprocess.run([executable, invalid], env=env, capture_output=True, timeout=15)
                    if rejected.returncode != 1 or any(root.iterdir()):
                        raise ValueError("The candidate accepted an unsafe URL argument")
                process = subprocess.Popen([executable, cold_url, warm_url], env=env)
                pid = process.pid
            else:
                pid = int(subprocess.check_output(
                    [opener, app, cold_url, "cold"], env=env, text=True, timeout=15,
                ))
                if not cold_loaded.wait(15) or browser_pid(executable) != pid:
                    raise ValueError("The cold LaunchServices URL did not reach the candidate")
                warm_pid = int(subprocess.check_output(
                    [opener, app, warm_url, "warm"], env=env, text=True, timeout=15,
                ))
                if warm_pid != pid:
                    raise ValueError("The warm URL was sent to another process")
            if not cold_loaded.wait(15) or not warm_loaded.wait(15):
                raise ValueError("A supplied URL did not reach its fixture")
            subprocess.run(
                ["/usr/bin/osascript", "-l", "JavaScript", "-e", REQUEST_QUIT, str(pid)],
                check=True, timeout=15,
            )
            if (arguments or apple_event) and process.wait(timeout=15) != 0:
                raise ValueError("The candidate did not quit cleanly")
            deadline = time.monotonic() + 15
            while browser_pid(executable) == pid and time.monotonic() < deadline:
                time.sleep(0.05)
            if browser_pid(executable) == pid:
                raise ValueError("The candidate did not quit")
            session = json.loads((root / "ContentKeychainWorkspaceSession.json").read_text())
            urls = [[tab["url"] for tab in session[role]["tabs"]] for role in ("work", "personal")]
            if not any(tabs[-2:] == [cold_url, warm_url] for tabs in urls):
                raise ValueError(f"The delivered links were not saved in separate tabs: {urls}")
            if pages:
                if any(server.requested.is_set() for _, _, server in pages):
                    raise ValueError("The candidate sent an HTTP request over an untrusted TLS connection")
                if system_default:
                    if not system_keychain_item_exists():
                        raise ValueError("The default Zitter app did not create its Safe Storage item")
                    print("The system default Zitter app saved the HTTP and HTTPS links in separate tabs")
                else:
                    print("Cold and warm LaunchServices HTTPS links reached the TLS fixtures and were saved")
                return
            delivery = "Command-line" if arguments else "Direct Apple Event" if apple_event else "Cold and warm LaunchServices"
            print(f"{delivery} URLs committed in separate tabs and were saved")
        finally:
            remaining = browser_pid(executable)
            if remaining is not None:
                try:
                    os.kill(remaining, signal.SIGTERM)
                except ProcessLookupError:
                    pass
            if process is not None and process.poll() is None:
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path, help="Prepared Zitter.app with Content release metadata")
    parser.add_argument("--scratch", type=Path, help="Existing directory for staging the app bundle")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--arguments", action="store_true",
                      help="Test HTTP links passed as arguments to the browser executable")
    mode.add_argument("--apple-event", action="store_true",
                      help="Send URL Apple Events directly to the candidate process")
    mode.add_argument("--https", action="store_true",
                      help="Check LaunchServices HTTPS delivery using untrusted local TLS fixtures")
    mode.add_argument("--system-default", action="store_true",
                      help="Launch the installed signed default browser in a fresh disposable macOS account")
    args = parser.parse_args()
    candidate = args.app.resolve(strict=True)
    if args.system_default:
        info = plistlib.loads((candidate / "Contents/Info.plist").read_bytes())
        if info.get("CFBundleIdentifier") != BUNDLE_ID or info.get("ZitterUsePreparedWorkspace") is not True:
            parser.error("The system default probe requires the installed production Content app")
        verify_app(candidate)
        verify_nested_signatures(candidate)
        with TemporaryDirectory(prefix="zitter-external-links-", dir=args.scratch) as staging:
            verify_urls(candidate, None, Path(staging), system_default=True)
        return
    with (TemporaryDirectory(prefix="zitter-external-links-", dir=args.scratch or candidate.parent) as staging,
          TemporaryDirectory(prefix="zitter-content-probe-") as profile):
        staging, profile = Path(staging).resolve(), Path(profile).resolve()
        app = staging / "Zitter.app"
        subprocess.run(["/bin/cp", "-cRp", args.app, app], check=True, timeout=90)
        for path in app.rglob("Info.plist"):
            if path.is_symlink():
                continue
            info = plistlib.loads(path.read_bytes())
            identifier = info.get("CFBundleIdentifier", "")
            if identifier == "dev.zitter.browser" or identifier.startswith("dev.zitter.browser."):
                info["CFBundleIdentifier"] = identifier.replace(
                    "dev.zitter.browser", "dev.zitter.content-launch-probe", 1,
                )
                path.write_bytes(plistlib.dumps(info))
        seal_local_copy(app)
        verify_urls(app, profile, staging, compile_file_keychain_fixture(staging),
                    arguments=args.arguments, apple_event=args.apple_event, https=args.https)


if __name__ == "__main__":
    main()
