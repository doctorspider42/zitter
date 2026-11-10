#!/usr/bin/env python3
# Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
"""Exercise the unpacked extension in an isolated Chromium profile."""

import argparse
import http.server
import itertools
import json
import subprocess
import tempfile
import threading
import time
from pathlib import Path

import websocket

ROOT = Path(__file__).resolve().parents[1]
MANAGER = "chrome-extension://kdpnmnbnedpcomicojommdikkdepclio/settings.html"
FIXTURE = """<!doctype html><meta charset=utf-8>
<link rel=stylesheet href=/test.css><script defer src=/test.js></script>
<h1>Night Shift browser fixture</h1><p id=caption>Image caption</p>
<img src=/image.svg width=160 height=70 alt="Orange rectangle">
<div id=late>Late CSS</div>"""
RESPONSES = {
    "/test.css": ("body{background:#fff;color:#191919} #caption{color:#222} "
                  "#late{background:#eee;color:#111}", "text/css"),
    "/test.js": ("""window.makeLate = () => {
        const sheet = document.querySelector("link").sheet;
        sheet.insertRule("#late { background-color: #f1d29e; }", sheet.cssRules.length);
    };""", "application/javascript"),
    "/image.svg": ('<svg xmlns="http://www.w3.org/2000/svg" width="160" height="70">'
                   '<rect width="160" height="70" fill="#ff6600"/></svg>', "image/svg+xml"),
    "/native": ('<!doctype html><meta name="color-scheme" content="dark">'
                '<style>body{background:#101010;color:#ddd}</style><h1>Native dark</h1>', "text/html"),
    "/contrast-fixture": ('''<!doctype html><meta charset="utf-8">
        <style>body{background:#101010;color:#eee} #low{color:#343434}
        #gradient{background-image:linear-gradient(#000,#fff)}</style>
        <figure><figcaption id="low">Low contrast</figcaption></figure>
        <figure><figcaption id="high">Readable</figcaption></figure>
        <figure><figcaption id="gradient">Variable background</figcaption></figure>''', "text/html"),
}
BACKGROUND = "getComputedStyle(document.querySelector('#late')).backgroundColor"
DARK = "Boolean(document.querySelector('.darkreader--user-agent'))"


class FixtureHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body, kind = RESPONSES.get(self.path, (FIXTURE, "text/html"))
        if self.path == "/crossorigin":
            source = f"http://127.0.0.1:{self.server.stylesheet_port}/test.css"
            body = FIXTURE.replace("href=/test.css", f"href={source}")
        self.send_response(200)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Security-Policy", "script-src 'self'; style-src http: 'unsafe-inline'")
        self.end_headers()
        self.wfile.write(body.encode())

    def log_message(self, *_):
        pass


def until(check, description, timeout=15):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if result := check():
            return result
        time.sleep(.1)
    raise AssertionError(f"Timed out waiting for {description}")


class DevTools:
    def __init__(self, url):
        self.socket = websocket.create_connection(url, timeout=15, suppress_origin=True)
        self.sequence = itertools.count(1)

    def call(self, method, session=None, **params):
        call = next(self.sequence)
        request = dict(id=call, method=method, params=params)
        if session:
            request["sessionId"] = session
        self.socket.send(json.dumps(request))
        while True:
            response = json.loads(self.socket.recv())
            if response.get("id") == call:
                if "error" in response:
                    raise RuntimeError(response["error"])
                return response["result"]

    def tab(self, url):
        target = self.call("Target.createTarget", url=url)["targetId"]
        session = self.call("Target.attachToTarget", targetId=target, flatten=True)["sessionId"]
        return Tab(self, session)


class Tab:
    def __init__(self, browser, session):
        self.browser = browser
        self.session = session

    def call(self, method, **params):
        return self.browser.call(method, session=self.session, **params)

    def eval(self, expression):
        result = self.call("Runtime.evaluate", expression=expression, awaitPromise=True, returnByValue=True)
        if "exceptionDetails" in result:
            raise RuntimeError(result["exceptionDetails"])
        return result["result"].get("value")


def change_sheet(tab):
    until(lambda: tab.eval("Boolean(window.makeLate && document.querySelector('.darkreader--sync'))"),
          "the themed fixture")
    before = tab.eval(BACKGROUND)
    tab.eval("makeLate()")
    return before


def check_contrast_sample(browser, site):
    tab = browser.tab(site + "/contrast-fixture")
    until(lambda: tab.eval("document.readyState === 'complete' && !!document.getElementById('low')"),
          "the contrast fixture")
    script = (ROOT / "chromium/assistant/page.js").read_text(encoding="utf-8")
    sample = tab.eval(script.replace("ZITTER_ARGS", '{"tool":"night_shift_inspect"}', 1))
    captions = {candidate["path"][0]["id"]: candidate for candidate in sample["candidates"]
                if candidate["path"][0]["tag"] == "figcaption"}
    assert captions["low"]["contrastRatio"] < 4.5
    assert captions["high"]["contrastRatio"] > 4.5
    assert captions["gradient"]["contrastRatio"] is None
    assert sample["lowContrastTargets"] == [captions["low"]["target"]]
    assert "Low contrast" not in json.dumps(sample)
    print("The Night Shift sample identifies low-contrast text without returning its contents")


def check_extension(browser, port):
    site = f"http://127.0.0.1:{port}"
    check_contrast_sample(browser, site)
    page = browser.tab(site)
    native = browser.tab(site + "/native")
    other = browser.tab(f"http://localhost:{port}")
    manager = browser.tab(MANAGER)
    until(lambda: manager.eval("document.querySelector('input')?.disabled === false"), "extension settings")

    def message(**body):
        result = manager.eval("chrome.runtime.sendMessage(" + json.dumps(body) + ")")
        if not result or not result.get("ok"):
            raise AssertionError(f"Worker request {body}: {result}")
        return result["value"]

    def registered():
        return manager.eval("chrome.scripting.getRegisteredContentScripts().then(s => s.length)")

    tab_id = manager.eval(f"chrome.tabs.query({{}}).then(tabs => tabs.find(t => t.url === '{site}/').id)")
    assert message(type="tab-settings", tabId=tab_id)["enabled"] is False
    assert registered() == 0 and not page.eval(DARK)
    message(type="set-global", enabled=True)
    until(lambda: page.eval(DARK) and other.eval(DARK), "the theme on open tabs")
    until(lambda: native.eval("getComputedStyle(document.body).backgroundColor === 'rgb(16, 16, 16)'") and
          not native.eval(DARK), "native dark detection")
    assert registered() == 2
    cross = browser.tab(site + "/crossorigin")
    until(lambda: cross.eval("Boolean(document.querySelector('link')?.sheet)"), "the cross-origin stylesheet")
    assert cross.eval("""(() => {
        try { document.querySelector('link').sheet.cssRules; return false; }
        catch (error) { return error.name === 'SecurityError'; }
    })()""")
    until(lambda: cross.eval(BACKGROUND) == "rgb(34, 36, 38)", "the anonymous linked stylesheet fallback")
    print("Cross-origin stylesheet without CORS themed successfully")

    loaded = page.eval("performance.timeOrigin")
    page.call("Page.reload")
    until(lambda: page.eval("performance.timeOrigin") != loaded, "the new document")
    before = change_sheet(page)
    after = until(lambda: (color := page.eval(BACKGROUND)) not in (before, "rgb(241, 210, 158)") and color,
                  "a linked stylesheet's CSSOM update under script CSP")
    assert page.eval("getComputedStyle(document.querySelector('img')).filter") == "none"
    print(f"Night Shift CSSOM change: {before} -> {after}")

    def write(**settings):
        return message(type="set-site", tabId=tab_id, key=f"site:127.0.0.1:{port}", settings=settings)

    write(mode="off")
    until(lambda: not page.eval(DARK), "site off")
    assert other.eval(DARK)
    write(mode="on", brightness=112, contrast=108)
    until(lambda: page.eval(DARK), "site on")
    assert message(type="tab-settings", tabId=tab_id)["settings"]["brightness"] == 112
    caption = "getComputedStyle(document.querySelector('#caption')).color"

    document_id = manager.eval(f"chrome.scripting.executeScript({{target:{{tabId:{tab_id}}},"
                               "func:()=>true}).then(([frame])=>frame.documentId)")
    draft = {"documentId": document_id, "url": site + "/", "proposal": {
        "label": "Caption contrast",
        "rules": [{"path": [{"tag": "p", "id": "caption"}],
                   "property": "color", "color": "#eddbff"}],
    }}
    manager.eval("chrome.storage.session.set(" + json.dumps({f"night-shift-draft:{tab_id}": draft}) + ")")
    # Zitter's native panel compares a draft through the isolated world.
    def review(proposal):
        return manager.eval(f"chrome.scripting.executeScript({{target:{{tabId:{tab_id}}},"
                            "func:p=>globalThis.zitterNightShiftReview?.(p),"
                            f"args:[{json.dumps(proposal)}]}}).then(([frame])=>frame.result)")

    assert message(type="get-review", tabId=tab_id)["proposal"]["label"] == "Caption contrast"
    assert review(draft["proposal"]) is True
    until(lambda: page.eval(caption) == "rgb(237, 219, 255)", "the draft preview")
    review(None)
    until(lambda: page.eval(caption) != "rgb(237, 219, 255)", "comparison without the fix")
    review(draft["proposal"])
    until(lambda: page.eval(caption) == "rgb(237, 219, 255)", "comparison with the fix")
    message(type="save-review", tabId=tab_id, documentId=document_id, proposal=draft["proposal"])
    review(None)
    until(lambda: page.eval(caption) == "rgb(237, 219, 255)", "the saved repair after the preview ends")
    assert message(type="tab-settings", tabId=tab_id)["settings"]["repair"]
    message(type="undo-review", tabId=tab_id, key=f"site:127.0.0.1:{port}")
    until(lambda: page.eval(caption) != "rgb(237, 219, 255)", "undoing the saved repair")
    print("Repair comparison, close, save and undo passed")

    message(type="set-global", enabled=False)
    until(lambda: not page.eval(DARK) and not other.eval(DARK), "global off on open tabs")
    assert registered() == 0
    assert page.eval("CSSStyleSheet.prototype.insertRule.toString().includes('[native code]')")
    fresh = browser.tab(site + "/?fresh")
    until(lambda: fresh.eval("document.readyState === 'complete' && !!window.makeLate"), "a fresh tab")
    assert not fresh.eval(DARK)
    message(type="set-global", enabled=True)
    until(lambda: fresh.eval(DARK) and page.eval(DARK), "reactivation on open tabs")
    assert message(type="tab-settings", tabId=tab_id)["settings"]["brightness"] == 112
    print("Global lifecycle, native dark, site isolation, tuning and typed repairs passed")


def check_original(browser, port):
    site = f"http://127.0.0.1:{port}"
    page = browser.tab(site)
    before = change_sheet(page)
    time.sleep(1)
    after = page.eval(BACKGROUND)
    print(f"Dark Reader 4.9.135 CSSOM change: {before} -> {after}")
    assert before == after
    cross = browser.tab(site + "/crossorigin")
    until(lambda: cross.eval("Boolean(document.querySelector('link')?.sheet)") and cross.eval(DARK),
          "the original theme with cross-origin CSS")
    time.sleep(1)
    color = cross.eval(BACKGROUND)
    print(f"Dark Reader 4.9.135 cross-origin CSS: {color}")
    assert color == "rgb(238, 238, 238)"


def run(browser_path, extension, check, port):
    # Windows can briefly keep renderer cache files open after Browser.close.
    with tempfile.TemporaryDirectory(prefix="zitter-night-shift-", ignore_cleanup_errors=True) as path:
        profile = Path(path)
        process = subprocess.Popen([
            str(browser_path), "--headless=new", "--disable-gpu", "--disable-background-networking",
            "--no-first-run", "--no-default-browser-check", "--remote-debugging-port=0",
            f"--user-data-dir={profile}", f"--disable-extensions-except={extension}",
            f"--load-extension={extension}", "about:blank",
        ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            port_file = profile / "DevToolsActivePort"
            until(port_file.exists, "browser startup")
            debug_port, path = port_file.read_text().splitlines()
            browser = DevTools(f"ws://127.0.0.1:{debug_port}{path}")
            try:
                check(browser, port)
            finally:
                try:
                    browser.call("Browser.close")
                except websocket.WebSocketConnectionClosedException:
                    pass
                browser.socket.close()
        finally:
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.terminate()
                process.wait(timeout=15)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("browser", type=Path, help="Chromium executable supporting unpacked extensions")
    parser.add_argument("--original", type=Path, help="unpacked Dark Reader 4.9.135 MV3 release")
    args = parser.parse_args()
    with (http.server.ThreadingHTTPServer(("127.0.0.1", 0), FixtureHandler) as server,
          http.server.ThreadingHTTPServer(("127.0.0.1", 0), FixtureHandler) as styles):
        server.stylesheet_port = styles.server_port
        for fixture in (server, styles):
            threading.Thread(target=fixture.serve_forever, daemon=True).start()
        try:
            run(args.browser.resolve(), ROOT / "chromium/night_shift", check_extension, server.server_port)
            if args.original:
                run(args.browser.resolve(), args.original.resolve(), check_original, server.server_port)
        finally:
            server.shutdown()
            styles.shutdown()


if __name__ == "__main__":
    main()
