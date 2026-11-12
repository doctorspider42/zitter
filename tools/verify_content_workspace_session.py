#!/usr/bin/env python3
"""Verify Workspace session restore and two encrypted profile stores."""

import argparse
import base64
from contextlib import ExitStack, closing, contextmanager
import hashlib
from html import escape
from http import cookies
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import math
import os
from pathlib import Path
import secrets
import shutil
import socket
import sqlite3
import subprocess
import sys
import tempfile
import threading
import time
import struct
import zlib
from zipfile import ZIP_DEFLATED, ZIP_STORED, ZipFile

from content_file_keychain_fixture import (
    compile_file_keychain_fixture,
    file_keychain_environment,
)
from profile_migration_trial import (
    CLIPBOARD_PREFS, PAGE_ZOOM_PREFS, import_workspace_profiles, inventory, prepare, rollback,
    write_page_zoom_preference,
    write_site_permission_rules,
)
from verify_content_profiles import (
    ENCRYPTED_PREFIX,
    KEY_FILE,
    verify_ciphertext,
    verify_history,
)
from verify_content_extension_manifest import write_fixture
from verify_content_tls import make_tls_fixture
from verify_content_site_permissions import build_pasteboard_fixture


SESSION_FILE = "ContentWorkspaceSession.json"
WINDOWS_LOCAL_STATE = "ContentWorkspaceLocalState.json"
PROFILES = ("workspace-work", "workspace-personal")
EXTENSION_ALLOWED_PATH = "/extension-content/allowed"
EXTENSION_EXTRA_ALLOWED_PATH = "/extension-content/other-allowed"
EXTENSION_EXTRA_EXCLUDED_PATH = "/extension-content/other-excluded"
EXTENSION_UNMATCHED_PATH = "/extension-content/unmatched"
EXTENSION_SCRIPT_ABSENT = "Extension content script absent"
ACTIVE_PATHS = ("/personal/active", "/work/active")
MARKER = "Zitter Content workspace session probe passed"
HISTORY_MARKER = "Zitter Content history panel profile isolation passed"
CLOSED_TAB_MARKER = "Zitter Content reopened closed tabs in their own workspace"
CLOSED_TAB_PATH = "/work/recently-closed"
NAVIGATION_MARKER = "Zitter Content preserved live back and forward across workspace switches"
PDF_EXPORT_PATH = "/pdf-export"
PDF_FRAME_PATH = "/pdf-export-frame"
PDF_EXPORT_MARKER = "Zitter Content exported a cross-process page to PDF"
FIND_RELOAD_PATH = "/find-reload"
FIND_RELOAD_MARKER = "Zitter Content find and cache bypass passed"
COMMAND_CENTER_MARKER = "Zitter Content Command Center ranking passed"
NATIVE_EDIT_MARKER = "Zitter Content native Edit menu passed"
COPY_ADDRESS_MARKER = "Zitter Content copied cleaned and original page addresses"
DEVELOPER_MENU_MARKER = "Zitter Content developer menu and project split passed"
RSS_MARKER = "Zitter Content verified native RSS and profile isolation"
RSS_PATH = "/workspace-feed.xml"
BROWSER_IMPORT_MARKER = "Zitter Content verified imported history and cookies"
INTERACTIVE_IMPORT_MARKER = "Zitter Content verified the interactive browser import"
EXTRA_WORKSPACE_MARKER = "Zitter Content verified an independent Workspace on Work"
SHIFT_PEEK_MARKER = "Zitter Content verified Shift Peek and its saved setting"
PEEK_MARKER = "Zitter Content verified routed Peek and live promotion"
POPUP_POLICY_MARKER = "Zitter Content verified per-profile popup exceptions"
POPUP_MARKER = "Zitter Content verified blocked, same-profile, form, and routed popups"
LINK_ROUTE_MARKER = "Zitter Content routed a renderer link to Personal"
LINK_REOPEN_MARKER = "Zitter Content reopened and removed a link rule through Split View"
SPLIT_ACTIONS_MARKER = "Zitter Content verified split actions and batch beforeunload"
SPLIT_PALETTE_MARKER = "Zitter Content Command Center split and pane focus passed"
CLOSE_OTHER_TABS_MARKER = "Zitter Content closed other tabs while keeping active and pinned pages"
CLOSE_OTHER_TABS_UNDO_MARKER = "Zitter Content restored a closed tab group through the toast"
TAB_COMMANDS_MARKER = "Zitter Content verified tab duplication, moves and profile-isolated copy"
SHARED_FAVORITES_MARKER = "Zitter Content verified shared profile favorites"
WORKSPACE_REASSIGNMENT_MARKER = "Zitter Content verified workspace reassignment"
WORKSPACE_REMOVAL_MARKER = "Zitter Content verified workspace removal and undo"
ADDITIONAL_PROFILE_MARKER = "Zitter Content verified an additional encrypted profile"
NAVIGATION_PATHS = ("/work/navigation-second", "/personal/navigation-second")
PRIVATE_WINDOW_PATHS = tuple(f"/private-window/{phase}" for phase in
                             ("seed", "read", "remaining", "fresh"))
PRIVATE_WINDOW_MARKER = "Zitter Content verified the private window lifecycle"
ADDRESS_HISTORY_MARKER = "Zitter Content address history profile isolation passed"
CLEAR_DATA_MARKER = "Zitter Content cleared Work browsing data"
EXTENSION_CONTENT_SCRIPT_MARKER = "Zitter Content extension document-start script isolation passed"
EXTENSION_EXTRA_SCOPE_MARKER = "Zitter Content injected the second declared script scope"
EXTENSION_SANDBOX_MARKER = "Zitter Content extracted its extension in a sandboxed utility"
EXTENSION_INSTALL_MARKER = "Zitter Content installed its extension fixture"
EXTENSION_RESTORE_MARKER = "Zitter Content restored its extension fixture"
EXTENSION_INSTALL_UI_SWITCH = "--zitter-content-extension-install-ui-probe"
EXTENSION_INSTALL_UI_CANCEL = "--zitter-content-extension-install-ui-cancel"
EXTENSION_REVIEW_SWITCH = "--zitter-content-extension-install-review"
EXTENSION_REVIEW_MARKER = "Zitter Content extension review: "
EXTENSION_REGISTRY_SWITCH = "--zitter-content-extension-registry-probe"
EXTENSION_RESTORE_SWITCH = "--zitter-content-restore-extensions"
EXTENSION_UPDATE_SWITCH = "--zitter-content-extension-update-probe"
EXTENSION_KEY_SWITCH = "--zitter-content-extension-key-sha256"
EXTENSION_PROFILE_SWITCH = "--zitter-content-extension-profile"
EXTENSION_PEER_SWITCH = "--zitter-content-extension-peer-probe"
EXTENSION_SIBLING_SWITCH = "--zitter-content-extension-sibling-probe"
EXTENSION_SIBLING_KEY_SWITCH = "--zitter-content-extension-sibling-key-sha256"
EXTENSION_STATE_SWITCH = "--zitter-content-extension-state-probe"
EXTENSION_SETTINGS_MARKER = "Zitter Content changed the active profile extension through Settings"
EXTENSION_CACHE_EVICTION_MARKER = (
    "Zitter Content evicted the owning profile's extension history cache"
)
EXTENSION_MANAGED_MARKER = "Zitter Content managed extension toggled without restarting its peer"
EXTENSION_PAIRED_STORAGE_MARKER = "Zitter Content paired extension storage: startup:"
EXTENSION_PEER_SURVIVED_MARKER = "Zitter Content kept the peer extension running after disable"
EXTENSION_NESTED_SCRIPT = "assets/scripts/café.js"
EXTENSION_PACKAGE_FILES = (
    "manifest.json", "content.js", "worker.js", "probe.html", "probe.js",
    EXTENSION_NESTED_SCRIPT, "assets/empty.txt", "assets/data.bin", "assets/action.png",
)
EXTENSION_REGISTRY_MARKER = "Zitter Content extension registry profile isolation passed"
EXTENSION_NAVIGATION_MARKER = "Zitter Content extension navigation isolation passed"
EXTENSION_STORAGE_MARKER = "Zitter Content extension local storage counter: "
EXTENSION_QUEUE_MARKER = "Zitter Content extension startup event woke the service worker"
EXTENSION_WORKER_MARKER = "Zitter Content extension service worker executed and unregistered"
EXTENSION_SUBRESOURCE_MARKER = "Zitter Content extension subresource isolation passed"


def favicon_png(gray):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    header = struct.pack(">IIBBBBB", 32, 32, 8, 0, 0, 0, 0)
    data = zlib.compress((b"\0" + bytes([gray]) * 32) * 32)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", data) + chunk(b"IEND", b"")


class WorkspaceServer(ThreadingHTTPServer):
    def finish_request(self, request, client_address):
        tls = getattr(self, "dnr_tls", None)
        if tls is not None:
            try:
                request.settimeout(5)
                first_byte = request.recv(1, socket.MSG_PEEK)
                if first_byte == b"\x16":
                    self.dnr_tls_attempts.append(client_address)
                    with tls.wrap_socket(request, server_side=True) as encrypted:
                        super().finish_request(encrypted, client_address)
                    return
                request.settimeout(None)
            except OSError:
                return
        super().finish_request(request, client_address)


class WorkspaceFixture(BaseHTTPRequestHandler):
    def popup_page(self, body=None):
        jar = cookies.SimpleCookie(self.headers.get("Cookie", ""))
        cookie = jar.get("profile_probe")
        profile = cookie.value if cookie else ""
        referrer = self.headers.get("Referer")
        self.server.popup_requests.append((self.path, profile, referrer, body))
        expected, title = {
            "/popup-policy": ("work", "Popup policy Work"),
            "/popup-background": ("work", "Popup background Work"),
            "/popup-work": ("work", "Popup same-profile opener"),
            "/popup-form": ("work", "Popup POST form Work"),
            "/popup-get-form?value=unchanged": ("work", "Popup GET form Work"),
            "/popup-routed": ("personal", "Popup routed Personal"),
        }.get(self.path, ("", "Unexpected popup"))
        opener = "!!window.opener" if self.path in ("/popup-work", "/popup-policy") else "!window.opener"
        self.send_page(f"""<!doctype html><script>
if (document.cookie.includes('profile_probe={expected}') &&
    localStorage.getItem('profile_probe') === '{expected}' && {opener})
    document.title = {json.dumps(title)};
</script>""".encode())

    def do_POST(self):
        if self.path == "/api/chat" and getattr(self.server, "assistant_settings_probe", False):
            request = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
            prompt = json.loads(request["messages"][-1]["content"])
            if (request["model"] != "manual-fixture"
                    or not prompt["prompt"].startswith("Connection test.")
                    or prompt.get("untrusted_browser_context")
                    or self.headers.get("Authorization")):
                self.send_error(400)
                return
            self.server.assistant_requests.append("check")
            answer = json.dumps({"answer": "Ollama connected", "actions": []})
            body = json.dumps({"message": {"content": answer}, "done": True})
            self.send_page((body + "\n").encode())
            return
        if self.path == "/extension-content/api-tab-subframe-posted":
            self.server.extension_subframe_post_requests.append(
                self.rfile.read(int(self.headers["Content-Length"])))
            self.send_page(b"""<!doctype html><script>
parent.document.title = 'Extension subframe posted';
</script>""")
            return
        if self.path == "/extension-content/api-tab-posted":
            self.server.extension_post_requests.append(
                self.rfile.read(int(self.headers["Content-Length"])))
            title = ("Extension tab posted" if len(self.server.extension_post_requests) == 1
                     else "Extension tab reposted")
            self.send_page(f"<!doctype html><title>{title}</title>".encode())
            return
        if self.path == "/popup-form":
            body = self.rfile.read(int(self.headers["Content-Length"])).decode()
            self.popup_page(body)
            return
        self.send_error(404)

    def do_GET(self):
        if self.path == "/extension-http-auth":
            authorization = self.headers.get("Authorization")
            accepted = authorization == "Basic Zml4dHVyZTpwcm9iZQ=="
            host = self.headers["Host"].split(":", 1)[0]
            self.server.extension_auth_requests.append((host, authorization is not None, accepted))
            title = "Extension auth accepted" if accepted else "Extension auth refused"
            body = f"<!doctype html><title>{title}</title>".encode()
            self.send_response(200 if accepted else 401)
            if not accepted:
                self.send_header("WWW-Authenticate", 'Basic realm="Zitter extension fixture"')
            self.send_header("Content-Type", "text/html")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if self.path in {f"/extension-uninstall/{profile}" for profile in PROFILES}:
            self.server.requests.append((self.path, ""))
            self.send_page(b"<!doctype html><title>Extension removed</title>")
            return
        if self.path == "/api/tags" and getattr(self.server, "assistant_settings_probe", False):
            self.server.assistant_requests.append("discover")
            self.send_page(b'{"models":[{"name":"manual-fixture"},{"name":"discovered-fixture"}]}')
            return
        if self.path == "/extension-active-tab":
            self.send_page(b"<!doctype html><title>Active tab fixture</title>"
                           b"<iframe src='/extension-active-tab-frame'></iframe>")
            return
        if self.path == "/extension-active-tab-frame":
            self.send_page(b"<!doctype html><title>Active tab child fixture</title>")
            return
        if self.path == "/extension-dnr-navigation-redirect":
            self.send_response(302)
            self.send_header("Location", "/extension-dnr-navigation")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        if self.path == "/extension-dnr-navigation":
            self.server.dnr_navigation_requests.append(self.path)
            self.send_page(b"<!doctype html><title>DNR navigation allowed</title>")
            return
        if self.path == "/extension-dnr-upgrade":
            self.server.dnr_plain_upgrade_requests.append(self.path)
            self.send_page(b"<!doctype html><title>DNR plain HTTP allowed</title>")
            return
        if self.path in ("/extension-dnr-rewrite", "/extension-dnr-rewritten"):
            self.send_page(b"redirected" if self.path.endswith("-rewritten") else b"original",
                           cors=True)
            return
        if self.path == "/extension-dnr-headers":
            body = self.headers.get("X-Zitter-DNR-Probe", "original").encode("ascii")
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("X-Zitter-DNR-Probe", "original")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Access-Control-Expose-Headers", "X-Zitter-DNR-Probe")
            self.end_headers()
            self.wfile.write(body)
            return
        if self.path == "/extension-dnr-allow-frame":
            self.send_page(b"""<!doctype html><script>
fetch('/extension-dnr-allow-child').then(
  response => parent.postMessage(response.ok ? 'allow' : 'unexpected', location.origin),
  () => parent.postMessage('block', location.origin));
</script>""")
            return
        if self.path in ("/extension-dnr-test", "/extension-dnr-dynamic", "/extension-dnr-session",
                         "/extension-dnr-disabled", "/extension-dnr-extra",
                         "/extension-dnr-allow-child"):
            self.send_page(b"allowed")
            return
        if self.path == "/extension-user-script":
            self.send_page(b"""<!doctype html><title>User script fixture</title>
<script>
addEventListener('load', () => setTimeout(() => {
  const {userScript, dynamicScript} = document.documentElement.dataset;
  document.title = userScript !== 'ran' ? 'User script absent'
    : dynamicScript === 'ran' ? 'User script executed' : 'Dynamic script absent';
}, 100));
</script>""")
            return
        if self.path in ("/extension-cache", "/extension-cache-away"):
            if self.path == "/extension-cache" and getattr(self.server, "extension_dnr", False):
                self.send_page(b"""<!doctype html><body><script>
const request = path => fetch('/extension-dnr-' + path).then(
  response => response.ok ? 'allow' : 'unexpected', () => 'block');
const frame = document.createElement('iframe');
frame.src = '/extension-dnr-allow-frame';
const child = new Promise(resolve => addEventListener('message', event => {
  if (event.source === frame.contentWindow && event.origin === location.origin)
    resolve(event.data);
}));
document.body.appendChild(frame);
const ungranted = new URL(location.href);
ungranted.hostname = 'dnr-no-grant.localhost';
const read = url => fetch(url).then(response => response.text());
const headers = url => fetch(url).then(async response =>
  (await response.text()) + '/' + response.headers.get('X-Zitter-DNR-Probe'));
Promise.all([
  ...['test', 'dynamic', 'session', 'disabled', 'extra'].map(request),
  read('/extension-dnr-rewrite'),
  headers('/extension-dnr-headers'),
  request('allow-child'),
  child,
  read(new URL('/extension-dnr-rewrite', ungranted)),
  headers(new URL('/extension-dnr-headers', ungranted)),
]).then(results => document.title = results.join(','),
        error => document.title = String(error));
</script>""", cache_control="public, max-age=600")
                return
            self.send_page(b"<!doctype html><title>Extension cache fixture</title>",
                           cache_control="public, max-age=600")
            return
        if self.path == "/extension-host-grant":
            self.server.extension_host_requests.append(self.headers["Host"])
            self.send_page(b"private fixture response")
            return
        if getattr(self.server, "refresh_favicons", False):
            if self.path in ("/shared-icon-pin", "/work/deferred"):
                self.server.favicon_refresh_requests.append((self.path, self.headers.get("Cookie")))
                self.send_page(b'<!doctype html><base href="/work/"><link rel="icon" href="renewed.png">')
                return
            if self.path == "/work/renewed.png":
                self.server.favicon_refresh_requests.append((self.path, self.headers.get("Cookie")))
                data = favicon_png(0x70)
                self.send_response(200)
                self.send_header("Content-Type", "image/png")
                self.send_header("Set-Cookie", "icon_refresh_probe=1; Path=/")
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)
                return
        if self.path == "/shared-icon-pin":
            self.server.requests.append((self.path, ""))
            self.send_error(404)
            return
        if getattr(self.server, "favicon_probe", False) and self.path in (
                "/work/icon.png", "/personal/icon.png", "/work/renewed.png"):
            self.server.favicon_requests.append(self.path)
            if self.server.favicons_offline:
                self.send_error(404)
                return
            data = favicon_png(0x30 if self.path.startswith("/work/") else 0xc0)
            self.send_response(200)
            self.send_header("Content-Type", "image/png")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
        if self.path == "/peek/gesture":
            self.send_page(b"""<!doctype html><title>Shift Peek source</title>
<style>body { margin: 0 } a { display:block;height:40px;line-height:40px }</style>
<a href="/peek/shift-plain">Open the ordinary link</a>
<a href="/peek/shift-blank" target="_blank">Open the blank-target link</a>
""")
            return
        if self.path in ("/peek/start", "/peek/next", "/peek/dirty",
                         "/peek/shift-plain", "/peek/shift-blank", "/peek/child"):
            self.server.peek_requests.append((self.path, self.headers.get("Cookie")))
            title = "Peek " + self.path.rsplit("/", 1)[1]
            self.send_page(f"""<!doctype html><script>
if (location.pathname.endsWith('/dirty')) {{
    addEventListener('beforeunload', event => {{
        event.preventDefault();
        event.returnValue = '';
    }});
}}
if (location.pathname === '/peek/start' &&
    performance.getEntriesByType('navigation')[0]?.type === 'reload') {{
    sessionStorage.setItem('peek_reloaded', 'yes');
}}
if (document.cookie.includes('profile_probe=personal') &&
    localStorage.getItem('profile_probe') === 'personal')
    document.title = location.pathname === '/peek/start' && sessionStorage.getItem('peek_reloaded')
        ? 'Peek reloaded' : {json.dumps(title)};
</script>""".encode())
            return
        if self.path == "/popup-policy-iframe":
            self.send_page(b"""<!doctype html><script>
if (!window.open('/popup-blocked')) parent.postMessage('popup blocked', '*');
</script>""")
            return
        if (self.path in ("/popup-blocked", "/popup-policy", "/popup-background", "/popup-work", "/popup-routed")
                or self.path.startswith("/popup-get-form?")):
            self.popup_page()
            return
        if self.path == "/research-profile":
            jar = cookies.SimpleCookie(self.headers.get("Cookie", ""))
            cookie = jar.get("profile_probe")
            expected = "research" if self.server.research_reopening else ""
            self.server.research_requests.append(cookie.value if cookie else "")
            self.send_page(f"""<!doctype html><script>
const expected = {json.dumps(expected)};
const cookie = document.cookie.split('; ').find(value =>
    value.startsWith('profile_probe='))?.split('=')[1] ?? '';
const stored = localStorage.getItem('profile_probe') ?? '';
if (cookie === expected && stored === expected) {{
    document.cookie = 'profile_probe=research; Max-Age=600; Path=/; SameSite=Strict';
    localStorage.setItem('profile_probe', 'research');
    document.title = 'Zitter additional profile isolated';
}} else {{
    document.title = 'Wrong profile state';
}}
</script>""".encode())
            return
        if self.path in ("/typed-link", "/routed-link"):
            jar = cookies.SimpleCookie(self.headers.get("Cookie", ""))
            cookie = jar.get("profile_probe")
            profile = cookie.value if cookie else ""
            self.server.routing_requests.append((self.path, profile, self.headers.get("Referer")))
            expected = "work" if self.path == "/typed-link" else "personal"
            title = f"Zitter {self.path[1:].replace('-', ' ')} {expected}"
            self.send_page(f"""<!doctype html><script>
if (document.cookie.includes('profile_probe={expected}') &&
    localStorage.getItem('profile_probe') === '{expected}' &&
    !document.referrer)
    document.title = {json.dumps(title)};
</script>""".encode())
            return
        if self.path == "/work/deferred" and getattr(self.server, "split_actions", False):
            self.send_page(b"<!doctype html><title>Split fixture survivor</title>")
            return
        if self.path in ("/split-actions/left", "/split-actions/right"):
            side = self.path.rsplit("/", 1)[1]
            self.send_page(f"""<!doctype html><script>
addEventListener('beforeunload', event => {{
    event.preventDefault();
    event.returnValue = '';
}});
document.title = 'Split fixture {side}';
</script>""".encode())
            return
        if self.path == "/tab-commands":
            self.send_page(b"""<!doctype html><script>
const cookie = document.cookie.split('; ').find(c =>
    c.startsWith('profile_probe='))?.split('=')[1];
const stored = localStorage.getItem('profile_probe');
const oldSession = sessionStorage.getItem('tab_commands');
if (cookie === stored && !document.referrer &&
    (stored === 'work' || (stored === 'personal' && !oldSession))) {
    sessionStorage.setItem('tab_commands', stored);
    if (stored === 'work' && oldSession === 'work') {
        addEventListener('beforeunload', event => {
            event.preventDefault();
            event.returnValue = '';
        });
        document.title = 'Zitter tab commands duplicate';
    } else {
        document.title = 'Zitter tab commands ' + stored;
    }
}
</script>""")
            return
        if self.path == "/reassigned-workspace":
            self.send_page(b"""<!doctype html><script>
const personal = document.cookie.includes('profile_probe=personal') &&
    localStorage.getItem('profile_probe') === 'personal';
document.title = personal ? 'Zitter reassigned workspace uses Personal' : 'Wrong profile';
</script>""")
            return
        if self.path == "/extra-workspace":
            self.send_page(b"""<!doctype html><script>
const shared = document.cookie.includes('profile_probe=work') &&
    localStorage.getItem('profile_probe') === 'work';
document.title = shared ? 'Zitter extra workspace shares Work' : 'Wrong profile';
</script>""")
            return
        if self.path == RSS_PATH:
            self.server.rss_requests.append((self.headers.get("Cookie"),
                                             self.headers.get("Authorization")))
            self.send_page(b"""<?xml version="1.0"?>
<rss version="2.0"><channel><title>Field notes</title><item>
<title>A useful article</title><guid isPermaLink="false">fixture</guid>
<link>/article</link><description>A field note</description>
</item></channel></rss>""")
            return
        if self.path == CLOSED_TAB_PATH or self.path in NAVIGATION_PATHS:
            jar = cookies.SimpleCookie(self.headers.get("Cookie", ""))
            self.server.requests.append((self.path, jar["profile_probe"].value
                                         if "profile_probe" in jar else ""))
            title = ("Recently closed Work fixture" if self.path == CLOSED_TAB_PATH
                     else "Workspace navigation fixture")
            self.send_page(f"<!doctype html><title>{title}</title>".encode())
            return
        if self.path == PDF_EXPORT_PATH:
            frame = f"http://localhost:{self.server.server_port}{PDF_FRAME_PATH}"
            second_page = (
                '<section style="break-before: page"><h1>Selected print page</h1></section>'
                if self.server.print_preview else ""
            )
            self.send_page(f"""<!doctype html><title>PDF fixture ready</title>
<h1>Top-level print fixture</h1>
<iframe width="600" height="120" src="{frame}"></iframe>
{second_page}""".encode())
            return
        if self.path == PDF_FRAME_PATH:
            self.send_page(b"<!doctype html><h1>Cross-process print fixture</h1>")
            return
        if self.path.partition("?")[0] == FIND_RELOAD_PATH:
            bypass = "no-cache" in self.headers.get("Cache-Control", "")
            self.server.find_reload_requests.append(bypass)
            title = "bypassed" if bypass else "initial"
            editor = """<textarea>page editor fixture</textarea>
<script>
const field = document.querySelector('textarea');
addEventListener('focus', () => field.focus());
field.focus();
setInterval(() => {
  if (!document.hasFocus() || document.activeElement !== field) return;
  document.title = field.selectionStart === 0 &&
      field.selectionEnd === field.value.length
      ? 'Find and reload selected' : 'Find and reload editor ready';
}, 25);
</script>""" if bypass else ""
            body = f"""<!doctype html><title>Find and reload {title}</title>
<p>First needle.</p><p>Second NEEDLE.</p>{editor}""".encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Cache-Control", "public, max-age=600")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if self.path in PRIVATE_WINDOW_PATHS:
            jar = cookies.SimpleCookie(self.headers.get("Cookie", ""))
            actual = jar["private_probe"].value if "private_probe" in jar else ""
            self.server.requests.append((self.path, actual))
            expected = "shared" if self.path in PRIVATE_WINDOW_PATHS[1:3] else ""
            filtered = self.path != PRIVATE_WINDOW_PATHS[-1]
            self.send_page(f"""<!doctype html><script>
const expected = {json.dumps(expected)};
const cookie = document.cookie.split('; ')
  .find(entry => entry.startsWith('private_probe='))?.split('=')[1] ?? '';
const storage = localStorage.getItem('private_probe') ?? '';
if (cookie === expected && storage === expected &&
    {json.dumps(actual)} === expected &&
    !document.cookie.includes('profile_probe=') &&
    (window.zitterPrivateParent === true) === {json.dumps(filtered)}) {{
  document.cookie = 'private_probe=shared; Max-Age=600; Path=/; SameSite=Strict';
  localStorage.setItem('private_probe', 'shared');
  document.title = 'Private window verified';
}} else {{
  document.title = 'Private window mismatch';
}}
</script>""".encode())
            return
        if self.path == "/extension-content/tab-message-target":
            self.send_page(b"""<!doctype html>
<script>
history.replaceState(null, '', '#same-document');
document.title = document.documentElement.dataset.zitterContentScript === 'injected'
  ? 'Extension tab message target ready' : 'Extension tab message target missing';
</script>""")
            return
        if self.path == "/extension-content/tab-sleep-observer":
            self.send_page(b"""<!doctype html><script>
const root = document.documentElement;
document.title = root.dataset.zitterContentScript === 'injected'
  ? 'Extension discard observer ready' : 'Extension discard observer missing';
new MutationObserver(() => {
  const result = root.dataset.zitterTabDiscard;
  if (result) document.title = result === 'verified'
    ? 'Discarded extension tab verified' : result;
}).observe(root, {attributeFilter: ['data-zitter-tab-discard']});
</script>""")
            return
        if self.path == "/extension-content/scripting-target":
            self.send_page(b"<!doctype html><body>Scripting fixture</body>")
            return
        if self.path in ("/extension-content/api-tab-open", "/extension-content/api-tab-deferred"):
            cache_control = self.headers.get("Cache-Control", "")
            self.server.extension_reload_requests.append((self.path, cache_control))
            title = {"max-age=0": "Extension tab revalidated",
                     "no-cache": "Extension tab bypassed cache"}.get(
                         cache_control, "Extension tab created")
            self.send_page(f"<!doctype html><title>{title}</title>".encode(),
                           cache_control="public, max-age=600")
            return
        if self.path == "/extension-content/api-tab-post-form":
            self.send_page(b"""<!doctype html><form method="post" action="/extension-content/api-tab-posted">
<input name="fixture" value="reload">
</form><script>document.forms[0].submit()</script>""")
            return
        if self.path == "/extension-content/api-tab-audio-state":
            self.send_page(b"""<!doctype html><title>Audio fixture ready</title>
<script>
let context;
window.startFixtureAudio = async () => {
  document.title = 'Audio fixture starting';
  context = new AudioContext();
  const oscillator = context.createOscillator();
  oscillator.connect(context.destination);
  oscillator.start();
  await context.resume();
  document.title = 'Audio fixture playing';
};
window.stopFixtureAudio = async () => {
  await context.close();
  document.title = 'Audio fixture badge hidden';
};
</script>""")
            return
        if self.path == "/extension-content/api-tab-subframe-form":
            self.send_page(b"""<!doctype html>
<iframe name="post-target"></iframe>
<form method="post" target="post-target" action="/extension-content/api-tab-subframe-posted">
<input name="fixture" value="history">
</form><script>document.forms[0].submit()</script>""")
            return
        if self.path == "/extension-content/api-duplicate-deferred":
            self.server.extension_deferred_duplicates += 1
            self.send_page(b"<!doctype html><title>Deferred duplicate loaded</title>")
            return
        if self.path in ("/extension-content/api-duplicate-history-first",
                         "/extension-content/api-duplicate-history-second"):
            step = self.path.rsplit("-", 1)[1]
            self.send_page(f"""<!doctype html><title>Duplicate history {step}</title>
<script>if (document.wasDiscarded) document.title += ' restored';</script>""".encode())
            return
        if self.path == "/extension-content/api-tab-update":
            self.send_page(b"""<!doctype html><title>Extension tab updated</title>
<script>addEventListener('beforeunload', event => {
  event.preventDefault(); event.returnValue = true;
});</script>""")
            return
        if self.path == "/extension-content/user-agent":
            self.send_page(json.dumps(self.headers.get("User-Agent")).encode())
            return
        if self.path in ("/extension-content/public-resource",
                          "/extension-content/public-resource-denied"):
            target = f"chrome-extension://{self.server.web_resource_extension}"
            if self.path.endswith("-denied"):
                self.send_page(f"""<!doctype html><script>
fetch('{target}/assets/data.bin').then(
  () => parent.postMessage('foreign resource leaked', '*'),
  () => parent.postMessage('foreign resource blocked', '*'));
</script>""".encode())
                return
            self.send_page(f"""<!doctype html><title>Checking extension resources</title>
<script>
(async () => {{
  const headerAgent = await (await fetch('/extension-content/user-agent')).json();
  if (!headerAgent?.includes('Chrome/') || headerAgent !== navigator.userAgent ||
      !navigator.userAgentData?.brands.some(brand => brand.brand === 'Chromium')) {{
    throw Error('Chromium user agent did not match the request and renderer');
  }}
  const response = await fetch('{target}/assets/data.bin');
  const bytes = new Uint8Array(await response.arrayBuffer());
  if (!response.ok || bytes.length !== 256 || bytes[255] !== 255)
    throw Error('Public extension asset was unreadable');
  await fetch('{target}/probe.js').then(
    () => {{ throw Error('Undeclared extension resource was readable'); }}, () => {{}});
  const dynamic = document.documentElement.dataset.zitterDynamicResource;
  if (!dynamic || new URL(dynamic).hostname === new URL('{target}').hostname)
    throw Error('Extension did not generate a dynamic resource URL');
  const dynamicResponse = await fetch(dynamic);
  if (!dynamicResponse.ok || (await dynamicResponse.text()) !== '')
    throw Error('Dynamic extension asset was unreadable');
  for (const denied of ['{target}/assets/empty.txt', new URL('/probe.js', dynamic)]) {{
    await fetch(denied).then(
      () => {{ throw Error('Extension resource policy was bypassed'); }}, () => {{}});
  }}
  const foreign = document.createElement('iframe');
  foreign.src = 'http://menu-frame.localhost:{self.server.server_port}/extension-content/public-resource-denied';
  const result = new Promise(resolve => window.addEventListener('message', event => {{
    if (event.source === foreign.contentWindow) resolve(event.data);
  }}));
  document.body.appendChild(foreign);
  if (await result !== 'foreign resource blocked')
    throw Error('Foreign origin read an extension asset');
  document.title = 'Public extension resource policy verified';
}})().catch(error => document.title = String(error));
</script><body></body>""".encode())
            return
        if self.path == "/extension-content/live-install":
            self.send_page(b"""<!doctype html>
<script>
const root = document.documentElement;
if (root.dataset.zitterContentScript !== 'injected' ||
    globalThis.chrome?.storage !== undefined ||
    globalThis.chrome?.permissions !== undefined ||
    globalThis.chrome?.runtime?.sendMessage !== undefined) {
  document.title = 'Live extension script was missing or exposed its APIs';
} else {
  document.title = 'Waiting for isolated extension storage';
  const showWakeResult = () => {
    const result = root.dataset.zitterWorkerWake;
    if (result) document.title = result === 'verified'
      ? 'Message woke the installed worker'
      : result;
  };
  new MutationObserver(showWakeResult).observe(root, {
    attributeFilter: ['data-zitter-worker-wake']
  });
  new MutationObserver(() => {
    const result = root.dataset.zitterTabMessage;
    if (result) document.title = result === 'verified'
      ? 'Extension tab messaging verified' : result;
  }).observe(root, {attributeFilter: ['data-zitter-tab-message']});
  new MutationObserver(() => {
    if (root.dataset.zitterTabRemoved === 'verified')
      document.title = 'Tab removal woke the installed worker';
  }).observe(root, {attributeFilter: ['data-zitter-tab-removed']});
  const check = () => {
    const result = root.dataset.zitterFrameStorage;
    if (result) document.title = result === 'verified'
      ? 'Live extension script verified isolated storage'
      : result;
    return Boolean(result);
  };
  if (!check()) {
    new MutationObserver((records, observer) => {
      if (check()) observer.disconnect();
    }).observe(root, {attributeFilter: ['data-zitter-frame-storage']});
  }
}
</script>""")
            return
        if self.path == "/extension-content/fallback-parent":
            document = """<!doctype html><script>
const isolated = globalThis.chrome?.storage === undefined &&
                 globalThis.zitterContentScript === undefined;
parent.postMessage(isolated
  ? (document.documentElement.dataset.zitterContentScript || 'absent') : 'leaked', '*');
</script>"""
            script_literal = json.dumps(document).replace("<", r"\u003c")
            self.send_page(f"""<!doctype html><body><script>
const fixture = {script_literal};
const blob = new Blob([fixture], {{type: 'text/html'}});
function readFrame(url, origin) {{
  return new Promise(resolve => {{
    const frame = document.createElement('iframe');
    const onMessage = event => {{
      if (event.source !== frame.contentWindow || event.origin !== origin) return;
      window.removeEventListener('message', onMessage);
      resolve(event.data);
    }};
    window.addEventListener('message', onMessage);
    frame.src = url;
    document.body.append(frame);
  }});
}}
Promise.all([
  readFrame('data:text/html;base64,' + btoa(fixture), 'null'),
  readFrame(URL.createObjectURL(blob), location.origin),
]).then(results => {{
  parent.postMessage({{fallback: results.join(',')}}, 'http://127.0.0.1:' + location.port);
}});
</script>""".encode())
            return
        if self.path in ("/extension-content/other-frame", "/extension-content/other-frame-excluded"):
            self.send_page(b"""<!doctype html><script>
const isolated = globalThis.chrome?.storage === undefined &&
                 globalThis.zitterContentScript === undefined;
parent.postMessage({
  path: location.pathname,
  result: isolated ? (document.documentElement.dataset.zitterContentScript || 'absent')
                   : 'leaked',
}, 'http://127.0.0.1:' + location.port);
</script>""")
            return
        if self.path in (EXTENSION_ALLOWED_PATH, EXTENSION_UNMATCHED_PATH,
                         EXTENSION_EXTRA_ALLOWED_PATH, EXTENSION_EXTRA_EXCLUDED_PATH):
            self.server.requests.append((self.path, ""))
            frame_fixture = b""
            if self.path == EXTENSION_EXTRA_ALLOWED_PATH:
                fixture_url = f'http://localhost:{self.server.server_port}'
                frame_fixture = f"""<script>
const fixtureOrigin = '{fixture_url}';
window.addEventListener('message', event => {{
  for (const id of ['fallbackAllowed', 'fallbackDenied']) {{
    const frame = document.getElementById(id);
    if (event.source === frame?.contentWindow && event.origin === new URL(frame.src).origin)
      document.documentElement.dataset[id] = event.data.fallback;
  }}
  if (event.origin !== fixtureOrigin) return;
  for (const frame of document.querySelectorAll('iframe')) {{
    if (event.source === frame.contentWindow &&
        event.data?.path === new URL(frame.src).pathname) {{
      document.documentElement.dataset[frame.id] = event.data.result;
    }}
  }}
}});
</script>
<iframe id="matchedFrame" src="{fixture_url}/extension-content/other-frame"></iframe>
<iframe id="excludedFrame" src="{fixture_url}/extension-content/other-frame-excluded"></iframe>
<iframe id="fallbackAllowed" src="http://fallback.localhost:{self.server.server_port}/extension-content/fallback-parent"></iframe>
<iframe id="fallbackDenied" src="http://unapproved.example.com:{self.server.server_port}/extension-content/fallback-parent"></iframe>
""".encode()
            if self.path in (EXTENSION_EXTRA_ALLOWED_PATH, EXTENSION_EXTRA_EXCLUDED_PATH):
                srcdoc = """<!doctype html><script>
const isolated = globalThis.chrome?.storage === undefined &&
                 globalThis.zitterContentScript === undefined;
parent.document.documentElement.dataset.srcdocScript = isolated
  ? (document.documentElement.dataset.zitterContentScript || 'absent') : 'leaked';
</script>"""
                frame_fixture += f'<iframe srcdoc="{escape(srcdoc, quote=True)}"></iframe>'.encode()
                frame_fixture += b"""<iframe id="blankFrame" src="about:blank"></iframe>
<script>
window.addEventListener('load', () => {
  const blank = document.getElementById('blankFrame').contentDocument;
  const isolated = blank.URL === 'about:blank' &&
      blank.defaultView.chrome?.storage === undefined &&
      blank.defaultView.zitterContentScript === undefined;
  document.documentElement.dataset.blankScript = isolated
      ? (blank.documentElement.dataset.zitterContentScript || 'absent')
      : 'unexpected URL or leaked script';
});
</script>"""
            self.send_page(b"""<!doctype html>
<script>
  const marker = document.documentElement.dataset.zitterContentScript;
  if (globalThis.chrome?.storage !== undefined ||
      globalThis.zitterContentScript !== undefined) {
    document.title = 'Extension API or script escaped isolation';
  } else {
    document.title = marker === 'injected'
      ? 'Extension content script ran at document start'
      : 'Extension content script absent';
  }
</script>""" + frame_fixture)
            return
        if self.path not in ACTIVE_PATHS:
            if self.path.endswith("/deferred"):
                self.server.requests.append((self.path, ""))
            self.send_error(404)
            return

        profile = self.path.split("/")[1]
        if getattr(self.server, "extension_failure_fixture", False):
            self.send_page(f"<!doctype html><title>Zitter workspace probe {profile}</title>".encode())
            return
        request_cookies = cookies.SimpleCookie(self.headers.get("Cookie", ""))
        value = request_cookies.get("profile_probe")
        actual = value.value if value else ""
        if "icon_refresh_probe" in request_cookies:
            self.server.favicon_refresh_saved_cookie = True
        cleared = profile == "work" and getattr(self.server, "cleared_work", False)
        repeated_navigation = getattr(self.server, "navigation_probe", False) and any(
            path == self.path for path, _ in self.server.requests
        )
        if getattr(self.server, "extension_install_ui", False):
            # Settings can finish before both pages seed their initial cookies.
            expected = profile if actual else ""
        else:
            expected = profile if (self.server.reopening or repeated_navigation) and not cleared else ""
        self.server.requests.append((self.path, actual))
        rss_link = (f'<link rel="alternate" type="application/rss+xml" href="{RSS_PATH}">'
                    if getattr(self.server, "rss_probe", False) else "")
        favicon_link = (f'<link rel="icon" href="/{profile}/icon.png">'
                        if getattr(self.server, "favicon_probe", False) else "")
        if (profile == "work" and not actual
                and getattr(self.server, "refresh_favicons", False)):
            self.server.favicon_refresh_requests.append((self.path, self.headers.get("Cookie")))
            favicon_link = '<link rel="icon" href="/work/renewed.png">'
        if profile == "work" and getattr(self.server, "refreshed_icon_markup", False):
            favicon_link = '<link rel="icon" href="/work/renewed.png">'
        body = f"""<!doctype html>
<meta charset="utf-8">
{rss_link}{favicon_link}
<script>
  const profile = {json.dumps(profile)};
  const expected = {json.dumps(expected)};
  const cookie = document.cookie.split('; ')
    .find(entry => entry.startsWith('profile_probe='))?.split('=')[1] ?? '';
  const storage = localStorage.getItem('profile_probe') ?? '';
  if (cookie === expected && storage === expected && {json.dumps(actual)} === expected) {{
    document.cookie = 'profile_probe=' + profile + '; Max-Age=600; Path=/; SameSite=Strict';
    localStorage.setItem('profile_probe', profile);
    document.title = 'Zitter workspace probe ' + profile;
  }} else {{
    document.title = 'Zitter workspace storage mismatch';
  }}
</script>""".encode()
        self.send_page(body)

    def send_page(self, body, *, cache_control="no-store", cors=False):
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Cache-Control", cache_control)
        if cors:
            self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def seed_session(origin):
    return {
        "active_workspace": "personal",
        "work": {
            "profile_id": "work",
            "active": 1,
            "tabs": [
                {"url": origin + "/work/deferred", "title": "Deferred Work"},
                {"url": origin + "/work/active", "title": "Selected Work"},
            ],
        },
        "personal": {
            "profile_id": "personal",
            "active": 0,
            "tabs": [
                {"url": origin + "/personal/active", "title": "Selected Personal"},
                {"url": origin + "/personal/deferred", "title": "Deferred Personal"},
            ],
        },
    }


def workspace_command(executable, root, origin, extra_flags=()):
    return [
        str(executable),
        "--zitter-content-workspace",
        f"--zitter-content-workspace-root={root}",
        *([f"--zitter-content-workspace-probe={origin}/"] if origin else []),
        "--no-proxy-server",
        *(["--enable-logging=stderr"] if os.name == "nt" else []),
        *extra_flags,
    ]


def launch(executable, root, origin, extra_flags=(), *, env=None):
    try:
        return subprocess.run(
            workspace_command(executable, root, origin, extra_flags),
            capture_output=True,
            env=env,
            encoding="utf-8",
            errors="replace",
            timeout=60,
        )
    except subprocess.TimeoutExpired as error:
        raise SystemExit(
            f"Workspace probe timed out with {list(extra_flags)!r}.\n"
            f"Stdout: {error.stdout!r}\nStderr: {error.stderr!r}"
        ) from error


def verify_settings_update_event(executable, root, origin, flags, extension, store):
    pending = extension / "pending-update-2.0_0"
    if pending.read_text("ascii") != "1.0":
        raise SystemExit("Settings did not persist the previous extension version")
    for checkpoint in (b'"removed-after-restart"', b'"revocation-verified"'):
        with tempfile.TemporaryFile() as output:
            process = subprocess.Popen(
                workspace_command(executable, root, origin, [*flags, EXTENSION_RESTORE_SWITCH]),
                stdout=output, stderr=subprocess.STDOUT,
            )
            try:
                deadline = time.monotonic() + 30
                while process.poll() is None and time.monotonic() < deadline:
                    records = b"".join(entry.read_bytes() for entry in store.glob("*.log"))
                    if b"zitterOptionalRestartError" in records:
                        raise SystemExit(f"The restored worker reported a permission error: {records!r}")
                    if (not pending.exists() and b'zitterUpdateEvent' in records
                            and b'"1.0->2.0:granted"' in records and checkpoint in records):
                        break
                    time.sleep(0.05)
                else:
                    output.seek(0)
                    raise SystemExit(f"The updated worker did not reach {checkpoint!r}.\n"
                                     f"{output.read().decode('utf-8', errors='replace')}")
            finally:
                if process.poll() is None:
                    process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=10)


def require_rejection(result, message):
    if result.returncode != 1 or message not in result.stderr:
        raise SystemExit(
            f"Workspace launch was not rejected as expected.\n"
            f"{result.stdout}\n{result.stderr}"
        )


def verify_failed_worker_install(executable, origin, archive, extension_id, owner, flags,
                                 ui_session):
    with tempfile.TemporaryDirectory(prefix="zitter-content-probe-") as directory:
        root = Path(directory).resolve()
        session = root / SESSION_FILE
        session.write_text(json.dumps(ui_session), encoding="utf-8")
        if os.name != "nt":
            key = root / KEY_FILE
            key.write_bytes(secrets.token_bytes(32))
            key.chmod(0o600)
        result = launch(executable, root, origin, [
            *flags, "--zitter-content-restore-extensions",
            f"{EXTENSION_INSTALL_UI_SWITCH}={archive}",
            f"{EXTENSION_PROFILE_SWITCH}={owner}",
        ])
        expected_errors = (
            "Service worker registration failed",
            "Could not apply the extension choice in its profile",
            "Settings did not activate the extension",
        )
        if (result.returncode or MARKER in result.stdout.splitlines()
                or any(error not in result.stderr for error in expected_errors)):
            raise SystemExit(f"Broken worker did not finish the failed activation cleanly.\n"
                             f"{result.stdout}\n{result.stderr}")
        installed = root / f"workspace-{owner}" / "Extensions" / extension_id
        state = installed / "user-enabled"
        if (state.read_text(encoding="ascii") != "disabled"
                or (installed / "1.0_0/fixture.crx").read_bytes() != archive.read_bytes()):
            raise SystemExit("A failed worker did not leave its signed package safely disabled")
        session.write_text(json.dumps(seed_session(origin)), encoding="utf-8")
        pin = (installed / "trusted-key-sha256").read_text(encoding="ascii")
        result = launch(executable, root, origin, [
            *flags, EXTENSION_REGISTRY_SWITCH, f"{EXTENSION_KEY_SWITCH}={pin}",
            f"{EXTENSION_PROFILE_SWITCH}={owner}", f"{EXTENSION_STATE_SWITCH}=check-disabled",
        ])
        markers = {MARKER, "Zitter Content restored the extension disabled by the user"}
        if (result.returncode or not markers.issubset(result.stdout.splitlines())
                or "Service worker registration failed" in result.stderr
                or state.read_text(encoding="ascii") != "disabled"):
            raise SystemExit(f"Failed extension was retried on restart.\n{result.stdout}\n{result.stderr}")
        print("A failed module worker left its signed package disabled across restart")


def damage_probe_cookie(database):
    with closing(sqlite3.connect(database)) as cookies_db, cookies_db:
        cookies_db.execute("UPDATE cookies SET encrypted_value = CAST("
                           "substr(encrypted_value, 1, 3) || X'0001020304050607' AS BLOB)")


def encrypted_source_cookie(name, *, domain="import.test"):
    key = hashlib.pbkdf2_hmac("sha1", b"zitter-import-fixture", b"saltysalt", 1003, 16)
    plaintext = hashlib.sha256(domain.encode()).digest() + f"original-{name}".encode()
    encrypted = subprocess.run([
        "/usr/bin/openssl", "enc", "-aes-128-cbc",
        "-K", key.hex(), "-iv", (b" " * 16).hex(),
    ], input=plaintext, capture_output=True, check=True).stdout
    return b"v10" + encrypted


def seed_browser_import(root, *, encrypted=False):
    source = root / "BrowserImportFixture"
    source.mkdir()
    now = int((time.time() + 11644473600) * 1_000_000)
    for directory, name in (("Default", "default"), ("Profile 1", "secondary")):
        profile = source / directory
        profile.mkdir()
        url = f"http://import.test/{name}"
        (profile / "Bookmarks").write_text(json.dumps({"roots": {"bookmark_bar": {
            "type": "folder", "name": "Bookmarks", "children": [
                {"type": "url", "name": name, "url": url},
            ],
        }}}), encoding="utf-8")
        with closing(sqlite3.connect(profile / "History")) as history, history:
            history.execute("CREATE TABLE urls(url, title, visit_count, typed_count, "
                            "last_visit_time, hidden)")
            history.execute("INSERT INTO urls VALUES (?, ?, 1, 0, ?, 0)", (url, name, now))
        with closing(sqlite3.connect(profile / "Cookies")) as cookies_db, cookies_db:
            cookies_db.execute(
                "CREATE TABLE cookies(host_key, name, value, encrypted_value, path, "
                "creation_utc, expires_utc, last_access_utc, is_secure, is_httponly, "
                "samesite, priority, source_scheme, source_port)")
            if encrypted:
                cookies_db.execute("CREATE TABLE meta(key TEXT, value INTEGER)")
                cookies_db.execute("INSERT INTO meta VALUES ('version', 24)")
            cookies_db.execute(
                "INSERT INTO cookies VALUES ('import.test', ?, ?, ?, '/', "
                "?, ?, ?, 0, 0, -1, 1, 1, 80)",
                (f"imported_{name}", "" if encrypted else "original",
                 encrypted_source_cookie(name) if encrypted else b"",
                 now, now + 86400 * 1_000_000, now))
    return source


def read_browser_import_result(root, windows):
    results = []
    for profile, name in zip(PROFILES, ("secondary", "default")):
        with closing(sqlite3.connect((root / profile / "History").as_uri()
                                     + "?mode=ro", uri=True)) as history:
            rows = history.execute(
                "SELECT url, title FROM urls WHERE url LIKE 'http://import.test/%'"
            ).fetchall()
        if rows != [(f"http://import.test/{name}", name)]:
            raise SystemExit(f"Unexpected imported history in {profile}: {rows}")
        with closing(sqlite3.connect((root / profile / "Network/Cookies").as_uri()
                                     + "?mode=ro", uri=True)) as cookies_db:
            rows = cookies_db.execute(
                "SELECT name, value, encrypted_value FROM cookies "
                "WHERE host_key = 'import.test'").fetchall()
        prefix = b"v10" if windows else ENCRYPTED_PREFIX
        if (len(rows) != 1 or rows[0][:2] != (f"imported_{name}", "")
                or not rows[0][2].startswith(prefix)):
            raise SystemExit(f"Unexpected cookie mapping or encryption in {profile}")
        results.append(rows[0][2])
    return results


def verify_cookie_removed(root, profile):
    database = root / profile / "Network/Cookies"
    if not database.exists():
        return
    with closing(sqlite3.connect(f"{database.as_uri()}?mode=ro", uri=True)) as cookies_db:
        if cookies_db.execute("SELECT 1 FROM cookies WHERE name = 'profile_probe'").fetchone():
            raise SystemExit(f"The cookie was not removed from {profile}")


@contextmanager
def hold_root_lock(root):
    lock_path = root / "WorkspaceLock"
    lock_path.write_bytes(b"\0")
    with lock_path.open("r+b") as lock:
        if os.name == "nt":
            import msvcrt

            msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            import fcntl

            fcntl.lockf(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        yield


def new_wrapped_windows_key():
    # Use the current Windows user, just as the browser does.
    import ctypes
    from ctypes import wintypes

    class DataBlob(ctypes.Structure):
        _fields_ = [("size", wintypes.DWORD), ("data", ctypes.POINTER(ctypes.c_ubyte))]

    data = (ctypes.c_ubyte * 32).from_buffer_copy(secrets.token_bytes(32))
    source = DataBlob(len(data), data)
    wrapped = DataBlob()
    protect = ctypes.WinDLL("crypt32", use_last_error=True).CryptProtectData
    protect.argtypes = [ctypes.POINTER(DataBlob), wintypes.LPCWSTR,
                        ctypes.POINTER(DataBlob), ctypes.c_void_p, ctypes.c_void_p,
                        wintypes.DWORD, ctypes.POINTER(DataBlob)]
    protect.restype = wintypes.BOOL
    if not protect(ctypes.byref(source), None, None, None, None, 1, ctypes.byref(wrapped)):
        raise ctypes.WinError(ctypes.get_last_error())
    free = ctypes.WinDLL("kernel32").LocalFree
    free.argtypes = [ctypes.c_void_p]
    free.restype = ctypes.c_void_p
    try:
        return base64.b64encode(b"DPAPI" + ctypes.string_at(wrapped.data, wrapped.size)).decode("ascii")
    finally:
        free(wrapped.data)


def verify_windows_key_failure(executable, root, origin):
    state_file = root / WINDOWS_LOCAL_STATE
    original = state_file.read_bytes()
    session = root / SESSION_FILE
    session_before = session.read_bytes()
    cookie_paths = [root / profile / "Network" / name for profile in PROFILES
                    for name in ("Cookies", "Cookies-journal", "Cookies-wal", "Cookies-shm")]
    cookies_before = {path: path.read_bytes() if path.exists() else None for path in cookie_paths}

    state_file.unlink()
    require_rejection(launch(executable, root, origin), "could not open its DPAPI key")
    if state_file.exists():
        raise SystemExit("The missing Windows key was replaced")

    invalid = json.loads(original)
    invalid["os_crypt"]["encrypted_key"] = base64.b64encode(
        b"DPAPI" + bytes(32)
    ).decode("ascii")
    invalid_bytes = json.dumps(invalid).encode("utf-8")
    state_file.write_bytes(invalid_bytes)
    require_rejection(launch(executable, root, origin), "could not open its DPAPI key")
    if state_file.read_bytes() != invalid_bytes:
        raise SystemExit("The unreadable Windows key was replaced")

    invalid["os_crypt"]["encrypted_key"] = new_wrapped_windows_key()
    wrong_key_bytes = json.dumps(invalid).encode("utf-8")
    state_file.write_bytes(wrong_key_bytes)
    require_rejection(launch(executable, root, origin, ["--enable-logging=stderr"]),
                      "could not verify its cookie stores")
    if state_file.read_bytes() != wrong_key_bytes:
        raise SystemExit("The unrelated Windows key was replaced")
    if any((path.read_bytes() if path.exists() else None) != data
           for path, data in cookies_before.items()):
        raise SystemExit("A refused Windows launch changed a cookie database or sidecar")
    if session.read_bytes() != session_before:
        raise SystemExit("A failed Windows key lookup changed the saved session")
    state_file.write_bytes(original)


def make_signing_key(openssl, path):
    private_pem = subprocess.run(
        [openssl, "genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048"],
        capture_output=True, check=True,
    ).stdout
    subprocess.run(
        [openssl, "pkcs8", "-topk8", "-nocrypt", "-outform", "DER", "-out", str(path)],
        input=private_pem, check=True,
    )
    return subprocess.run(
        [openssl, "pkey", "-pubout", "-outform", "DER"],
        input=private_pem, capture_output=True, check=True,
    ).stdout


def sign_fixture(signer, package, key, output_name, extra_entry=None,
                 *, files=EXTENSION_PACKAGE_FILES):
    archive = package / "payload.zip"
    with ZipFile(archive, "w", compression=ZIP_DEFLATED) as payload:
        payload.writestr("assets/", b"")
        for name in files:
            payload.write(package / name, name)
        if extra_entry:
            payload.writestr(extra_entry, "unexpected")
    output = package / output_name
    subprocess.run([str(signer), str(output), str(archive), str(key)], check=True)
    archive.unlink()
    return output


def sign_archive_size_fixture(signer, package, key, name, *, files=(),
                              stored_sizes=(), deflated_sizes=(), empty_files=0):
    archive = package / "archive-size-fixture.zip"
    mib = 1024 * 1024
    with ZipFile(archive, "w", compression=ZIP_DEFLATED) as payload:
        for filename in files:
            payload.write(package / filename, filename)
        for index, size in enumerate(stored_sizes):
            payload.writestr(f"payload/stored-{index}.bin", bytes(size * mib),
                             compress_type=ZIP_STORED)
        for index, size in enumerate(deflated_sizes):
            payload.writestr(f"payload/deflated-{index}.bin", bytes(size * mib))
        for index in range(empty_files):
            payload.writestr(f"payload/entries/{index}", b"")
    output = package / name
    subprocess.run([str(signer), str(output), str(archive), str(key)], check=True)
    archive.unlink()
    return output


def make_external_receiver(signer, openssl, directory, sender_id, *, read_tabs=False):
    directory.mkdir()
    key = directory / "key.der"
    public_key = make_signing_key(openssl, key)
    digest = hashlib.sha256(public_key).hexdigest()
    extension_id = digest[:32].translate(str.maketrans("0123456789abcdef", "abcdefghijklmnop"))
    manifest = {
        "manifest_version": 3,
        "name": "External messaging receiver",
        "version": "1.0",
        "key": base64.b64encode(public_key).decode("ascii"),
        "background": {"service_worker": "worker.js"},
        "externally_connectable": {"ids": [sender_id]},
        "permissions": ["storage", "tabs"] if read_tabs else ["storage"],
    }
    (directory / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
    (directory / "worker.js").write_text("""
const tabUpdates = [];
if (chrome.runtime.getManifest().permissions.includes('tabs')) {
  chrome.tabs.onUpdated.addListener((tabId, change, tab) => tabUpdates.push({tabId, change, tab}));
}
chrome.runtime.onStartup.addListener(() => {
  chrome.storage.local.set({externalReceiverReady: true});
});
chrome.runtime.onMessageExternal.addListener((message, sender, reply) => {
  if (message?.probe === 'sender-tab') {
    reply(sender.tab);
    return;
  }
  if (message?.probe === 'query-tabs') {
    chrome.tabs.query(message.query).then(
      tabs => reply({tabs, updates: tabUpdates}), error => reply(String(error)));
    return true;
  }
  if (message?.probe !== 'external') return;
  chrome.storage.local.set({externalProbeSender: sender.id}).then(() => {
    reply({receiver: chrome.runtime.id, sender: sender.id});
  });
  return true;
});
""", encoding="utf-8")
    files = ("manifest.json", "worker.js")
    archive = sign_fixture(signer, directory, key, "fixture.crx", files=files)
    key.unlink()
    return extension_id, digest, archive, files


def seed_approved_receiver(profile, receiver):
    extension_id, digest, archive, files = receiver
    install = profile / "Extensions" / extension_id
    version = install / "1.0_0"
    version.mkdir(parents=True)
    for name in files:
        shutil.copyfile(archive.parent / name, version / name)
    shutil.copyfile(archive, version / "fixture.crx")
    (install / "active-version").write_text("1.0", encoding="ascii")
    (install / "trusted-key-sha256").write_text(digest.upper(), encoding="ascii")


def add_extension_message_fixture(package, *, content_path=None):
    with (package / "worker.js").open("a", encoding="utf-8") as worker:
        worker.write("""
chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
  if (message?.probe === 'version' && sender.id === chrome.runtime.id) {
    sendResponse(chrome.runtime.getManifest().version);
  }
});
chrome.runtime.onConnect.addListener(port => {
  if (port.name === 'zitter-probe' && port.sender?.id === chrome.runtime.id) {
    port.onMessage.addListener(message => {
      if (message?.probe === 'version') port.postMessage(chrome.runtime.getManifest().version);
    });
  }
});
""")
    if content_path:
        with (package / "content.js").open("a", encoding="utf-8") as script:
            script.write(f"""
if (location.pathname === {json.dumps(content_path)}) {{
  chrome.runtime.sendMessage({{probe: 'version'}}).then(
    result => document.documentElement.dataset.zitterExtensionMessage = result,
    error => document.documentElement.dataset.zitterExtensionMessage = String(error));
}}
""")


def add_extension_alarm_fixture(package, profile, initial_counter):
    worker = package / "worker.js"
    source = worker.read_text(encoding="utf-8")
    check = "    await chrome.storage.local.set({counter: counter + 1});"
    if source.count(check) != 1:
        raise SystemExit("Could not attach the extension alarm check")
    worker.write_text(
        source.replace(check, "    await checkAlarms(counter);\n" + check)
        + f"""
async function checkAlarms(counter) {{
  const profile = {json.dumps(profile)};
  const persistent = profile + '-persistent';
  const ephemeral = profile + '-ephemeral';
  if (counter === {initial_counter}) {{
    await chrome.alarms.create(persistent, {{delayInMinutes: 60, persistAcrossSessions: true}});
    await chrome.alarms.create(ephemeral, {{delayInMinutes: 60, persistAcrossSessions: false}});
    await chrome.alarms.create('to-clear', {{delayInMinutes: 60}});
    if (!await chrome.alarms.clear('to-clear') || await chrome.alarms.get('to-clear')) {{
      throw Error('Extension alarm did not clear');
    }}
    const alarms = await chrome.alarms.getAll();
    if (alarms.length !== 2 ||
        !alarms.some(a => a.name === persistent && a.persistAcrossSessions) ||
        !alarms.some(a => a.name === ephemeral && !a.persistAcrossSessions)) {{
      throw Error('Extension alarm creation crossed profiles or lost an alarm');
    }}
    const elapsed = profile + '-elapsed';
    let listener;
    let timer;
    const fired = new Promise((resolve, reject) => {{
      listener = alarm => alarm.name === elapsed
        ? resolve() : reject(Error('Another profile alarm fired'));
      chrome.alarms.onAlarm.addListener(listener);
      timer = setTimeout(() => reject(Error('Extension alarm did not fire')), 5000);
    }});
    try {{
      await Promise.all([
        chrome.alarms.create(elapsed, {{when: Date.now() - 1, persistAcrossSessions: false}}),
        fired,
      ]);
    }} finally {{
      clearTimeout(timer);
      chrome.alarms.onAlarm.removeListener(listener);
    }}
  }} else if (counter === {initial_counter + 1}) {{
    const alarms = await chrome.alarms.getAll();
    if (alarms.length !== 1 || alarms[0].name !== persistent ||
        !alarms[0].persistAcrossSessions || !await chrome.alarms.get(persistent) ||
        await chrome.alarms.get(ephemeral)) {{
      throw Error('Extension alarm restart crossed profiles or lost persistence');
    }}
    await chrome.alarms.clearAll();
    if ((await chrome.alarms.getAll()).length) {{
      throw Error('Extension alarm clearAll left an alarm');
    }}
  }} else if ((await chrome.alarms.getAll()).length) {{
    throw Error('A cleared extension alarm returned after restart');
  }}
}}
""",
        encoding="utf-8",
    )


def add_extension_uninstall_url_fixture(package, url, initial_counter):
    worker = package / "worker.js"
    source = worker.read_text(encoding="utf-8")
    check = "    await chrome.storage.local.set({counter: counter + 1});"
    if source.count(check) != 1:
        raise SystemExit("Could not attach the extension uninstall URL check")
    worker.write_text(
        source.replace(check, "    await checkUninstallUrl(counter);\n" + check)
        + f"""
async function checkUninstallUrl(counter) {{
  if (counter === {initial_counter}) {{
    await chrome.runtime.setUninstallURL({json.dumps(url)});
    for (const invalid of ['file:///tmp/extension', 'https://user:pass@localhost/',
                            'http://[invalid', 'https://localhost/' + 'x'.repeat(1023)]) {{
      let rejected = false;
      try {{
        await chrome.runtime.setUninstallURL(invalid);
      }} catch {{
        rejected = true;
      }}
      if (!rejected) throw Error('Invalid uninstall URL accepted');
    }}
  }} else if (counter === {initial_counter + 2}) {{
    await chrome.runtime.setUninstallURL('');
  }}
}}
""",
        encoding="utf-8",
    )


def add_extension_dnr_fixture(package, manifest):
    manifest["host_permissions"] = ["http://127.0.0.1/*"]
    manifest["declarative_net_request"] = {"rule_resources": [
        {"id": "fixture_rules", "enabled": True, "path": "rules.json"},
        {"id": "extra_rules", "enabled": False, "path": "extra-rules.json"},
    ]}

    def rule(rule_id, suffix):
        return {"id": rule_id, "action": {"type": "block"}, "condition": {
            "urlFilter": f"/extension-dnr-{suffix}", "resourceTypes": ["xmlhttprequest"],
        }}

    navigation_rule = rule(3, "navigation|")
    navigation_rule["condition"]["resourceTypes"] = ["main_frame"]
    redirect_rule = rule(5, "rewrite|")
    redirect_rule["action"] = {
        "type": "redirect", "redirect": {"transform": {"path": "/extension-dnr-rewritten"}},
    }
    header_rule = rule(6, "headers|")
    header_rule["action"] = {
        "type": "modifyHeaders",
        "requestHeaders": [{"header": "X-Zitter-DNR-Probe", "operation": "set", "value": "request"}],
        "responseHeaders": [{"header": "X-Zitter-DNR-Probe", "operation": "set", "value": "response"}],
    }
    allow_frame = rule(7, "allow-frame|")
    allow_frame["priority"] = 2
    allow_frame["action"] = {"type": "allowAllRequests"}
    allow_frame["condition"]["resourceTypes"] = ["sub_frame"]
    upgrade_rule = rule(9, "upgrade|")
    upgrade_rule["action"] = {"type": "upgradeScheme"}
    upgrade_rule["condition"]["resourceTypes"] = ["main_frame"]
    (package / "rules.json").write_text(json.dumps([
        rule(1, "test"), rule(2, "disabled"), navigation_rule, rule(4, "cache"),
        redirect_rule, header_rule, allow_frame, rule(8, "allow-child|"), upgrade_rule,
    ]), encoding="utf-8")
    (package / "extra-rules.json").write_text(json.dumps([rule(1, "extra")]),
                                             encoding="utf-8")
    with (package / "probe.js").open("a", encoding="utf-8") as script:
        script.write("""
if (location.hash.startsWith('#dnr-')) {
  chrome.runtime.sendMessage({dnrProbe: location.hash.slice(1)}).then(
    result => document.title = result,
    error => document.title = 'DNR fixture error: ' + String(error));
}
""")
    with (package / "worker.js").open("a", encoding="utf-8") as worker:
        worker.write(f"""
chrome.runtime.onMessage.addListener((message, sender, reply) => {{
  if (sender.id !== chrome.runtime.id ||
      typeof message?.dnrProbe !== 'string' || !message.dnrProbe.startsWith('dnr-'))
    return;
  (async () => {{
    const api = chrome.declarativeNetRequest;
    const mutation = message.dnrProbe;
    switch (mutation) {{
      case 'dnr-cache-dynamic':
        await api.updateDynamicRules({{addRules: [{json.dumps(rule(99, "cache-dynamic"))}]}});
        await api.updateDynamicRules({{removeRuleIds: [99]}});
        break;
      case 'dnr-cache-session':
        await api.updateSessionRules({{addRules: [{json.dumps(rule(99, "cache-session"))}]}});
        await api.updateSessionRules({{removeRuleIds: [99]}});
        break;
      case 'dnr-cache-static':
        await api.updateStaticRules({{rulesetId: 'fixture_rules', disableRuleIds: [4]}});
        await api.updateStaticRules({{rulesetId: 'fixture_rules', enableRuleIds: [4]}});
        break;
      case 'dnr-cache-enabled':
        await api.updateEnabledRulesets({{disableRulesetIds: ['extra_rules']}});
        await api.updateEnabledRulesets({{enableRulesetIds: ['extra_rules']}});
        break;
      case 'dnr-install':
      case 'dnr-restore':
        break;
      default:
        throw Error('Unknown DNR fixture phase');
    }}
    if (mutation.startsWith('dnr-cache-')) {{
      reply('Extension DNR cache mutation verified');
      return;
    }}
    const restored = message.dnrProbe === 'dnr-restore';
    const [dynamic, session, enabled, disabled] = await Promise.all([
      api.getDynamicRules(), api.getSessionRules(), api.getEnabledRulesets(),
      api.getDisabledRuleIds({{rulesetId: 'fixture_rules'}}),
    ]);
    const recoveredAction = chrome.runtime.getManifest().version === '2.0' ? 'allow' : 'block';
    if (session.length || dynamic.map(rule => rule.id).join() !== (restored ? '41' : '') ||
        (restored && dynamic[0]?.action.type !== recoveredAction) ||
        enabled.sort().join() !== (restored ? 'extra_rules,fixture_rules' : 'fixture_rules') ||
        disabled.join() !== (restored ? '2' : '')) {{
      throw Error('unexpected rule state on startup: ' +
          JSON.stringify([dynamic, session, enabled, disabled]));
    }}
    if (!restored) {{
      await api.updateDynamicRules({{addRules: [{json.dumps(rule(41, "dynamic"))}]}});
      await api.updateStaticRules({{rulesetId: 'fixture_rules', disableRuleIds: [2]}});
      await api.updateEnabledRulesets({{enableRulesetIds: ['extra_rules']}});
    }}
    await api.updateSessionRules({{addRules: [{json.dumps(rule(42, "session"))}]}});
    if ((await api.getSessionRules()).map(rule => rule.id).join() !== '42')
      throw Error('session rule did not install');
    reply('Extension DNR API verified');
  }})().catch(error => reply('DNR fixture error: ' + String(error)));
  return true;
}});
""")


def add_extension_offscreen_fixture(package, version):
    (package / "sandboxed.html").write_text(
        '<!doctype html><script src="sandboxed.js"></script>', encoding="utf-8",
    )
    (package / "sandboxed.js").write_text("""
let parentAccess = false;
try { parentAccess = Boolean(parent.document); } catch {}
parent.postMessage({
  sandboxFixture: true, parentAccess, runtimeAccess: Boolean(globalThis.chrome?.runtime),
}, '*');
""", encoding="utf-8")
    (package / "managed-storage.json").write_text(json.dumps({
        "type": "object", "properties": {"fixture": {"type": "string"}},
    }), encoding="utf-8")
    (package / "dynamic-script.js").write_text("""
if (location.pathname === '/extension-user-script')
  document.documentElement.dataset.dynamicScript = 'ran';
""", encoding="utf-8")
    (package / "offscreen.html").write_text(
        '<!doctype html><title>Offscreen fixture</title><script src="offscreen.js"></script>',
        encoding="utf-8",
    )
    (package / "offscreen.js").write_text(f"""
chrome.runtime.onMessage.addListener((message, sender, reply) => {{
  if (message?.offscreenEcho)
    reply([document.title, {json.dumps(version)}]);
  if (message?.clipboardOperation) {{
    const field = document.createElement('textarea');
    document.body.append(field);
    field.focus();
    if (message.clipboardOperation === 'write') {{
      field.value = message.text;
      field.select();
      document.execCommand('copy');
    }} else {{
      document.execCommand('paste');
    }}
    reply(field.value);
    field.remove();
  }}
}});
""", encoding="utf-8")
    with (package / "probe.js").open("a", encoding="utf-8") as script:
        script.write("""
if (location.hash.startsWith('#offscreen-probe-')) {
  const verifySandbox = new Promise((resolve, reject) => {
    const frame = document.createElement('iframe');
    const timeout = setTimeout(() => reject(Error('Sandboxed page did not execute')), 3000);
    function receive(event) {
      if (event.source !== frame.contentWindow || !event.data?.sandboxFixture) return;
      window.removeEventListener('message', receive);
      clearTimeout(timeout);
      frame.remove();
      if (event.origin === 'null' && !event.data.parentAccess && !event.data.runtimeAccess)
        resolve();
      else
        reject(Error('Sandboxed page acquired an extension origin or API access'));
    }
    window.addEventListener('message', receive);
    frame.src = 'sandboxed.html';
    if (document.body)
      document.body.append(frame);
    else
      document.addEventListener('DOMContentLoaded', () => document.body.append(frame), {once: true});
  });
  verifySandbox.then(() => chrome.runtime.sendMessage({
    offscreenProbe: location.hash.split('-').at(-1),
    clipboardProbe: location.hash.includes('-clipboard-'),
    privacyRestart: location.hash.includes('-privacy-restart-'),
  })).then(
    result => document.title = result,
    error => document.title = 'Offscreen fixture error: ' + String(error));
}
""")
    with (package / "worker.js").open("a", encoding="utf-8") as worker:
        worker.write("""
chrome.runtime.onMessage.addListener((message, sender, reply) => {
  if (!message?.offscreenProbe || sender.id !== chrome.runtime.id)
    return;
  (async () => {
    if (chrome.runtime.getManifest().incognito !== 'not_allowed')
      throw Error('Private extension mode was not disabled');
    const self = await chrome.management.getSelf();
    if (self.id !== chrome.runtime.id || self.version !== chrome.runtime.getManifest().version ||
        !self.enabled || self.type !== 'extension' || self.installType !== 'normal' ||
        typeof chrome.management.getAll === 'function')
      throw Error('Management metadata was incorrect or exposed other extensions');
    const nativePermission = chrome.runtime.getManifest().permissions.includes('nativeMessaging');
    if ((typeof chrome.runtime.sendNativeMessage === 'function') !== nativePermission ||
        (typeof chrome.runtime.connectNative === 'function') !== nativePermission)
      throw Error('Native messaging bindings did not respect the declared permission');
    if (nativePermission) {
      const echo = await chrome.runtime.sendNativeMessage('org.zitter.fixture.echo', {sequence: 1});
      if (echo.sequence !== 1)
        throw Error('One-shot native host response differed');
      const port = chrome.runtime.connectNative('org.zitter.fixture.echo');
      try {
        for (const sequence of [2, 3]) {
          const reply = await new Promise((resolve, reject) => {
            const timer = setTimeout(() => reject(Error('Native host response timed out')), 3000);
            function disconnected() {
              clearTimeout(timer);
              port.onMessage.removeListener(receive);
              reject(Error(chrome.runtime.lastError?.message || 'Native host disconnected'));
            }
            function receive(message) {
              clearTimeout(timer);
              port.onMessage.removeListener(receive);
              port.onDisconnect.removeListener(disconnected);
              resolve(message);
            }
            port.onMessage.addListener(receive);
            port.onDisconnect.addListener(disconnected);
            port.postMessage({sequence});
          });
          if (reply.sequence !== sequence)
            throw Error('Native messaging port returned the wrong sequence');
        }
      } finally {
        port.disconnect();
      }
      for (const [host, expected] of [
        ['org.zitter.fixture.denied', 'is forbidden'],
        ['org.zitter.fixture.missing', 'not found'],
        ['org..zitter', 'Invalid native messaging host name'],
      ]) {
        await chrome.runtime.sendNativeMessage(host, {}).then(
          () => { throw Error('Unexpected connection to ' + host); },
          error => { if (!String(error).includes(expected)) throw error; });
      }
    }
    const privacyPermission = chrome.runtime.getManifest().permissions.includes('privacy');
    if (Boolean(chrome.privacy) !== privacyPermission)
      throw Error('Privacy API did not respect the declared permission');
    if (privacyPermission) {
      const {passwordSavingEnabled, autofillAddressEnabled, autofillCreditCardEnabled} =
        chrome.privacy.services;
      for (const service of [autofillAddressEnabled, autofillCreditCardEnabled]) {
        const setting = await service.get({});
        if (setting.value !== false || setting.levelOfControl !== 'not_controllable')
          throw Error('An unavailable autofill service appeared controllable');
      }
      const {privacyFixture} = await chrome.storage.local.get('privacyFixture');
      const overridden = privacyFixture === 'set';
      const initial = await passwordSavingEnabled.get({});
      if (initial.value !== !overridden ||
          initial.levelOfControl !== (overridden ? 'controlled_by_this_extension'
                                              : 'controllable_by_this_extension'))
        throw Error('Password preference did not match ' + privacyFixture + ': ' + JSON.stringify(initial));
      if (privacyFixture === undefined || (overridden && message.privacyRestart)) {
        const clearing = overridden;
        let listener;
        let timeout;
        try {
          const event = new Promise((resolve, reject) => {
            timeout = setTimeout(() => reject(Error('Password preference change event missing')), 2500);
            listener = resolve;
            passwordSavingEnabled.onChange.addListener(listener);
          });
          const [change] = await Promise.all([event, clearing ? passwordSavingEnabled.clear({})
              : passwordSavingEnabled.set({value: false})]);
          const changed = await passwordSavingEnabled.get({});
          const level = clearing ? 'controllable_by_this_extension' : 'controlled_by_this_extension';
          if (changed.value !== clearing || change.value !== clearing ||
              changed.levelOfControl !== level || change.levelOfControl !== level)
            throw Error('Password preference override did not take effect');
          await chrome.storage.local.set({privacyFixture: clearing ? 'cleared' : 'set'});
        } finally {
          clearTimeout(timeout);
          passwordSavingEnabled.onChange.removeListener(listener);
        }
      }
    }
    const notificationPermission =
      chrome.runtime.getManifest().permissions.includes('notifications');
    if (Boolean(chrome.notifications) !== notificationPermission)
      throw Error('Notification API did not respect the declared permission');
    if (notificationPermission) {
      const level = await chrome.notifications.getPermissionLevel();
      if (!['granted', 'denied'].includes(level) ||
          Object.keys(await chrome.notifications.getAll()).length !== 0 ||
          await chrome.notifications.clear('absent') ||
          await chrome.notifications.update('absent', {title: 'Absent'}))
        throw Error('Unexpected state in the empty notification store');
      if (![chrome.notifications.onClicked, chrome.notifications.onButtonClicked,
            chrome.notifications.onClosed].every(event => typeof event.addListener === 'function'))
        throw Error('Notification events were not exposed');
      await chrome.notifications.create({type: 'basic', title: 'Incomplete'}).then(
        () => { throw Error('An incomplete notification was accepted'); },
        error => {
          if (!String(error).includes('require a title, message and icon URL')) throw error;
        });
      if (globalThis.zitterNotificationRole) {
        const api = chrome.notifications;
        const options = title => ({
          type: 'basic', title, message: 'Fixture', iconUrl: 'sandboxed.html', silent: true,
        });
        async function expectEvent(event, expected, trigger) {
          let listener;
          let timeout;
          try {
            const received = new Promise((resolve, reject) => {
              timeout = setTimeout(() => reject(Error('Notification event was not delivered')), 2500);
              listener = (...args) => resolve(args);
              event.addListener(listener);
            });
            const [actual] = await Promise.all([received, trigger()]);
            if (JSON.stringify(actual) !== JSON.stringify(expected))
              throw Error('Unexpected notification event arguments: ' + JSON.stringify(actual));
          } finally {
            clearTimeout(timeout);
            event.removeListener(listener);
          }
        }
        if (globalThis.zitterNotificationRole === 'owner') {
          if (await api.create('shared', options('Fixture owner')) !== 'shared' ||
              !await api.update('shared', {title: 'Fixture owner updated'}) ||
              Object.keys(await api.getAll()).join() !== 'shared')
            throw Error('The owning profile did not create and update its notification');
          await expectEvent(api.onClicked, ['click'],
            () => api.create('click', options('Fixture click')));
          await expectEvent(api.onClosed, ['dismiss', true],
            () => api.create('dismiss', options('Fixture dismiss')));
          await api.create('clear', options('Fixture clear'));
          await expectEvent(api.onClosed, ['clear', false], async () => {
            if (!await api.clear('clear')) throw Error('Clear missed the fixture notification');
          });
          if (Object.keys(await api.getAll()).join() !== 'shared')
            throw Error('Only the shared owner notification should remain');
        } else {
          await expectEvent(api.onButtonClicked, ['shared', 1], () => api.create('shared', {
            ...options('Fixture peer button'), buttons: [{title: 'First'}, {title: 'Second'}],
          }));
          if (Object.keys(await api.getAll()).length !== 0)
            throw Error('The peer acquired the owner notification after its button action');
        }
      }
    }
    if (chrome.runtime.getManifest().permissions.includes('webRequestAuthProvider') &&
        !chrome.webRequest?.onAuthRequired?.hasListeners())
      throw Error('HTTP authentication listener was not registered');
    if (message.offscreenProbe === 'denied') {
      if (chrome.idle)
        throw Error('Idle API exposed without a declared permission');
    } else {
      const idleState = await chrome.idle.queryState(60);
      if (idleState !== 'active')
        throw Error('Idle query did not return the fixture state');
      chrome.idle.setDetectionInterval(60);
      await new Promise((resolve, reject) => {
        const timeout = setTimeout(() => {
          chrome.idle.onStateChanged.removeListener(onIdleStateChanged);
          reject(Error('Native idle event did not reach the worker'));
        }, 4000);
        function onIdleStateChanged(state) {
          if (state !== 'idle') return;
          clearTimeout(timeout);
          chrome.idle.onStateChanged.removeListener(onIdleStateChanged);
          resolve();
        }
        chrome.idle.onStateChanged.addListener(onIdleStateChanged);
      });
    }
    const managed = await chrome.storage.managed.get({fixture: 'fallback'});
    if (managed.fixture !== 'fallback' || Object.keys(await chrome.storage.managed.get()).length)
      throw Error('Unconfigured managed storage did not return empty policy');
    await chrome.storage.managed.set({fixture: 'invalid'}).then(
      () => { throw Error('Managed storage accepted a write'); },
      error => { if (!String(error).includes('read-only')) throw error; });
    const dynamic = chrome.scripting;
    const registered = await dynamic.getRegisteredContentScripts({ids: ['fixture']});
    if (Boolean(registered.length) !== (message.offscreenProbe === 'restore'))
      throw Error('Dynamic content script persistence did not match the profile state');
    if (message.offscreenProbe !== 'denied') {
      if (registered.length) {
        if (registered[0].runAt !== 'document_end')
          throw Error('Restored dynamic content script lost its update');
        await dynamic.unregisterContentScripts({ids: ['fixture']});
      }
      await dynamic.registerContentScripts([{id: 'fixture',
        matches: ['http://127.0.0.1/*'], js: ['dynamic-script.js']}]);
      await dynamic.updateContentScripts([{id: 'fixture', runAt: 'document_end',
        persistAcrossSessions: true}]);
      const [updated] = await dynamic.getRegisteredContentScripts({ids: ['fixture']});
      if (updated?.runAt !== 'document_end' || !updated.persistAcrossSessions)
        throw Error('Dynamic content script update did not take effect');
    }
    const scripts = chrome.userScripts;
    if (message.offscreenProbe === 'denied') {
      if (scripts)
        throw Error('User scripts exposed in a profile without approval');
    } else {
      const [previous] = await scripts.getScripts({ids: ['fixture']});
      if (Boolean(previous) !== (message.offscreenProbe === 'restore'))
        throw Error('User script persistence did not match the profile state');
      if (previous) {
        if (previous.runAt !== 'document_end')
          throw Error('Restored user script lost its update');
        await scripts.unregister({ids: ['fixture']});
      }
      await scripts.register([{id: 'fixture', matches: ['http://127.0.0.1/*'],
        js: [{code: `if (location.pathname === '/extension-user-script')
          document.documentElement.dataset.userScript = 'ran'`}]}]);
      await scripts.update([{id: 'fixture', runAt: 'document_end'}]);
      const [updated] = await scripts.getScripts({ids: ['fixture']});
      if (updated?.runAt !== 'document_end')
        throw Error('User script update did not take effect');
      const csp = "script-src 'self'; object-src 'none'";
      await scripts.configureWorld({worldId: 'fixture', csp, messaging: false});
      const worlds = await scripts.getWorldConfigurations();
      if (!worlds.some(world => world.worldId === 'fixture' &&
                                world.csp === csp && world.messaging === false))
        throw Error('User script world configuration did not take effect');
      await scripts.resetWorldConfiguration('fixture');
      if ((await scripts.getWorldConfigurations()).some(world => world.worldId === 'fixture'))
        throw Error('User script world configuration survived reset');
    }
    const api = chrome.offscreen;
    const params = {url: 'offscreen.html', reasons: ['DOM_PARSER'], justification: 'Fixture'};
    const contexts = () => chrome.runtime.getContexts({contextTypes: ['OFFSCREEN_DOCUMENT']});
    const rejects = promise => promise.then(
      () => { throw Error('Invalid offscreen request succeeded'); }, () => {});
    if ((await contexts()).length)
      throw Error('Unexpected initial document in this profile');
    await rejects(api.createDocument({...params, url: 'https://example.invalid/'}));
    await api.createDocument(params);
    const [context] = await contexts();
    if (context?.documentUrl !== chrome.runtime.getURL(params.url) || context.incognito ||
        context.tabId !== -1 || context.windowId !== -1 || !(await api.hasDocument()))
      throw Error('Offscreen context metadata did not match the package');
    const echo = await chrome.runtime.sendMessage({offscreenEcho: true});
    if (echo?.join() !== 'Offscreen fixture,' + chrome.runtime.getManifest().version)
      throw Error('Offscreen document message crossed profiles');
    await rejects(api.createDocument(params));
    const readClipboard = () => chrome.runtime.sendMessage({clipboardOperation: 'read'});
    const writeClipboard = text => chrome.runtime.sendMessage({clipboardOperation: 'write', text});
    if (message.clipboardProbe) {
      if (await readClipboard())
        throw Error('A DOM parser document read the clipboard');
      await writeClipboard('unexpected-dom-parser-write');
    }
    await api.closeDocument();
    if ((await contexts()).length || await api.hasDocument())
      throw Error('The closed document remained visible');
    await api.createDocument(message.clipboardProbe
      ? {...params, reasons: ['CLIPBOARD']} : params);
    if (message.clipboardProbe) {
      const expected = 'zitter-offscreen-clipboard-fixture';
      const initial = await readClipboard();
      if (message.offscreenProbe === 'denied') {
        if (initial)
          throw Error('The peer read the clipboard without the permission');
        await writeClipboard('unexpected-peer-write');
      } else {
        if (!['zitter-private-pasteboard-fixture', expected].includes(initial))
          throw Error('The authorized document did not read the isolated pasteboard');
        await writeClipboard(expected);
        let actual;
        for (let attempt = 0; attempt < 20 && actual !== expected; attempt++) {
          actual = await readClipboard();
          if (actual !== expected) await new Promise(resolve => setTimeout(resolve, 25));
        }
        if (actual !== expected)
          throw Error('The authorized document did not write the isolated pasteboard');
      }
    }
    reply('Extension offscreen verified');
  })().catch(error => reply('Offscreen fixture error: ' + String(error)));
  return true;
});
""")
    return ("offscreen.html", "offscreen.js", "managed-storage.json", "dynamic-script.js",
            "sandboxed.html", "sandboxed.js")


def verify_extension_pair(executable, root, origin, server, signer, openssl, flags, profile,
                          *, managed=False, interactive=False, multi=False,
                          restore_installed=False, extra_profile=False, dnr=False,
                          offscreen=False, clipboard=False, webrequest_auth=False,
                          notifications=False, notification_delivery=False, privacy=False,
                          native_messaging=False):
    with ExitStack() as packages:
        work, personal = [
            Path(packages.enter_context(
                tempfile.TemporaryDirectory(prefix="zitter-content-extension-")
            )).resolve() for _ in PROFILES
        ]
        key = work / "signing-key.der"
        public_key = make_signing_key(openssl, key)
        encoded = base64.b64encode(public_key).decode("ascii")
        digest = hashlib.sha256(public_key).hexdigest()
        trusted_digests = [digest, digest]
        sibling_key = personal / "signing-key.der"
        sibling_public = make_signing_key(openssl, sibling_key) if multi else public_key
        trusted_digests[1] = hashlib.sha256(sibling_public).hexdigest()
        alphabet = str.maketrans("0123456789abcdef", "abcdefghijklmnop")
        extension_ids = [value[:32].translate(alphabet) for value in trusted_digests]
        extension_id = extension_ids[0]
        if native_messaging:
            hosts = root / "NativeMessagingHosts"
            hosts.mkdir(mode=0o700)
            executable_host = hosts / "fixture-echo"
            source = Path(__file__).resolve().parents[1] / "tests/fixtures/content_native_message_echo.c"
            subprocess.run(["xcrun", "--sdk", "macosx", "clang", source, "-o", executable_host],
                           check=True)
            foreign_id = "a" * 32 if extension_id != "a" * 32 else "b" * 32
            for host, allowed_id in (("echo", extension_id), ("denied", foreign_id)):
                name = f"org.zitter.fixture.{host}"
                (hosts / f"{name}.json").write_text(json.dumps({
                    "name": name, "type": "stdio", "path": str(executable_host),
                    "allowed_origins": [f"chrome-extension://{allowed_id}/"],
                }), encoding="utf-8")
        archives = []
        invalid_rules_archive = None
        for index, (files, version, counter) in enumerate(
            ((work, "1.0", 0), (personal, "2.0", 40))
        ):
            manifest_key = base64.b64encode(sibling_public).decode("ascii") if index else encoded
            manifest = write_fixture(files, manifest_key, initial_counter=counter)
            extra_files = ()
            if offscreen:
                manifest["permissions"] = ["offscreen", "scripting", "userScripts"]
                if PROFILES[index] == f"workspace-{profile}":
                    manifest["permissions"].append("idle")
                    if privacy:
                        manifest["permissions"].append("privacy")
                    if native_messaging:
                        manifest["permissions"].append("nativeMessaging")
                    if clipboard:
                        manifest["permissions"].extend(["clipboardRead", "clipboardWrite"])
                manifest["storage"] = {"managed_schema": "managed-storage.json"}
                manifest["incognito"] = "split"
                manifest["host_permissions"] = ["http://127.0.0.1/*"]
                manifest["sandbox"] = {"pages": ["sandboxed.html"]}
                manifest["content_security_policy"] = {
                    "sandbox": "sandbox allow-scripts; script-src 'self'",
                }
                extra_files = add_extension_offscreen_fixture(files, version)
                owns_probe = PROFILES[index] == f"workspace-{profile}"
                if notification_delivery or (notifications and owns_probe):
                    manifest["permissions"].append("notifications")
                if notification_delivery:
                    with (files / "worker.js").open("a", encoding="utf-8") as worker:
                        role = "owner" if owns_probe else "peer"
                        worker.write(f"\nglobalThis.zitterNotificationRole = {json.dumps(role)};\n")
                if webrequest_auth and PROFILES[index] == f"workspace-{profile}":
                    manifest["permissions"].extend(["webRequest", "webRequestAuthProvider"])
                    with (files / "worker.js").open("a", encoding="utf-8") as worker:
                        worker.write("""
chrome.webRequest.onAuthRequired.addListener((details, respond) => {
  respond(details.isProxy ? {} : {
    authCredentials: {username: 'fixture', password: 'probe'},
  });
}, {urls: ['http://127.0.0.1/*', 'http://localhost/*']}, ['asyncBlocking']);
""")
            if dnr and PROFILES[index] == f"workspace-{profile}":
                manifest["permissions"].append("declarativeNetRequest")
                add_extension_dnr_fixture(files, manifest)
                extra_files = ("rules.json", "extra-rules.json")
            if not managed:
                add_extension_message_fixture(files, content_path=EXTENSION_ALLOWED_PATH)
                manifest["permissions"].append("alarms")
                add_extension_alarm_fixture(files, PROFILES[index], counter)
                add_extension_uninstall_url_fixture(
                    files, f"{origin}/extension-uninstall/{PROFILES[index]}", counter
                )
            if multi and index:
                manifest["name"] = "Zitter sibling fixture"
                del manifest["content_scripts"]
            manifest["version"] = version
            (files / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
            archives.append(sign_fixture(
                signer, files, sibling_key if multi and index else key, "fixture.crx",
                files=("manifest.json", "worker.js") if multi and index
                      else (*EXTENSION_PACKAGE_FILES, *extra_files),
            ))
            if dnr and PROFILES[index] == f"workspace-{profile}":
                (files / "rules.json").write_text('{"invalid":true}', encoding="utf-8")
                invalid_rules_archive = sign_fixture(
                    signer, files, key, "invalid-rules.crx",
                    files=(*EXTENSION_PACKAGE_FILES, *extra_files),
                )
        key.unlink()
        if multi:
            sibling_key.unlink()
        owner = 0 if profile == "work" else 1
        profile_flag = f"{EXTENSION_PROFILE_SWITCH}={profile}"
        other_switch = EXTENSION_SIBLING_SWITCH if multi else EXTENSION_PEER_SWITCH
        switch_flags = [f"{EXTENSION_REGISTRY_SWITCH}={archives[owner]}",
                        f"{other_switch}={archives[1 - owner]}"]
        shared_flags = [*flags, f"{EXTENSION_KEY_SWITCH}={trusted_digests[owner]}", profile_flag]
        if multi:
            shared_flags.append(f"{EXTENSION_SIBLING_KEY_SWITCH}={trusted_digests[1 - owner]}")
        if managed:
            if offscreen:
                shared_flags.append("--zitter-content-extension-offscreen-probe")
                if clipboard:
                    shared_flags.append("--zitter-content-extension-offscreen-clipboard-probe")
                if webrequest_auth:
                    shared_flags.append("--zitter-content-extension-webrequest-auth-probe")
                if notification_delivery:
                    shared_flags.append("--zitter-content-extension-notification-delivery-probe")
            if dnr:
                require_rejection(launch(executable, root, origin, [
                    *shared_flags, f"{EXTENSION_REGISTRY_SWITCH}={invalid_rules_archive}",
                    f"{EXTENSION_REVIEW_SWITCH}=preview",
                ]), "Could not index verified extension rules")
                if (root / PROFILES[owner] / "Extensions").exists() or server.requests:
                    raise SystemExit("An invalid ruleset reached extension publication")
                server.extension_dnr = True
                server.dnr_navigation_requests = []
                server.dnr_plain_upgrade_requests = []
                server.dnr_tls_attempts = []
                _, server.dnr_tls = make_tls_fixture(work, openssl)
                shared_flags.append("--zitter-content-extension-dnr-probe")
            if interactive:
                server.reopening = False
                print("Open Settings and search for Zitter. Close Zitter to exit.",
                      flush=True)
                subprocess.run(workspace_command(executable, root, origin, [
                    *shared_flags, *switch_flags, f"{EXTENSION_STATE_SWITCH}=manage",
                ]), check=True)
            else:
                verify_managed_extensions(executable, root, origin, server, extension_ids,
                                          owner, shared_flags, switch_flags, archives, multi,
                                          restore_flags=flags if restore_installed else None,
                                          extra_profile=extra_profile, dnr=dnr,
                                          offscreen=offscreen, clipboard=clipboard,
                                          webrequest_auth=webrequest_auth,
                                          notification_delivery=notification_delivery, privacy=privacy)
            return
        launch_count = 3
        for run in range(1, launch_count + 1):
            server.reopening = run != 1
            result = launch(executable, root, origin, [
                *shared_flags, *switch_flags,
            ])
            lines = result.stdout.splitlines()
            required = {MARKER, EXTENSION_PEER_SURVIVED_MARKER,
                        EXTENSION_PAIRED_STORAGE_MARKER + f"1.0:{run}",
                        EXTENSION_PAIRED_STORAGE_MARKER + f"2.0:{40 + run}",
                        "Zitter Content extension frame received its worker version: 1.0",
                        "Zitter Content extension frame received its worker version: 2.0"}
            if (result.returncode or ":FATAL:" in result.stderr or not required.issubset(lines)
                    or lines.count(EXTENSION_CONTENT_SCRIPT_MARKER) != 2
                    or lines.count(EXTENSION_NAVIGATION_MARKER) != 2
                    or lines.count(EXTENSION_WORKER_MARKER) != 2):
                raise SystemExit(f"Paired extensions failed.\n{result.stdout}\n{result.stderr}")
            for profile_directory, version in zip(PROFILES, ("1.0", "2.0")):
                path = root / profile_directory
                installed = path / "Extensions" / extension_id
                store = path / "Local Extension Settings" / extension_id
                if ((installed / "active-version").read_text(encoding="ascii") != version
                        or not (store / "CURRENT").is_file()
                        or not (path / "Extension State" / "CURRENT").is_file()):
                    raise SystemExit(f"The paired fixture did not stay in {profile_directory}")
                expected_url = f"{origin}/extension-uninstall/{profile_directory}" if run < 3 else ""
                if (installed / "uninstall-url").read_text(encoding="ascii") != expected_url:
                    raise SystemExit(f"Incorrect uninstall URL in {profile_directory}")
            if any(path.startswith("/extension-uninstall/") for path, _ in server.requests):
                raise SystemExit("An uninstall URL was fetched during extension startup")
            for name in ("extension-private", "extension-peer-private"):
                if any((root / name / store).exists() for store in
                       ("Local Extension Settings", "Extension State")):
                    raise SystemExit("A private context acquired an extension store")
            for archive in archives:
                archive.unlink(missing_ok=True)
            switch_flags = [EXTENSION_REGISTRY_SWITCH, EXTENSION_PEER_SWITCH]
        verify_extension_choice(executable, root, origin, server, extension_id, profile, [
            *flags, f"{EXTENSION_KEY_SWITCH}={digest}", EXTENSION_REGISTRY_SWITCH,
        ], prior_launches=launch_count)
    print(f"The extension kept running in the other profile after disable in {profile}")


def extension_preferences(profile):
    path = profile / "Content Extension Preferences"
    if not path.exists():
        return {}
    return json.loads(path.read_text(encoding="utf-8"))["extensions"]["settings"]


def verify_managed_extensions(executable, root, origin, server, extension_ids, owner,
                              flags, switch_flags, archives, multi, *,
                              restore_flags=None, extra_profile=False, dnr=False,
                              offscreen=False, clipboard=False, webrequest_auth=False,
                              notification_delivery=False, privacy=False):
    env = build_pasteboard_fixture(root) if clipboard else None
    if notification_delivery:
        from verify_content_bundle import loader_environment
        source = Path(__file__).resolve().parents[1] / "tests/fixtures/content_notification_center_mac.mm"
        library = root / "notification_fixture.dylib"
        subprocess.run([
            "xcrun", "--sdk", "macosx", "clang++", "-fobjc-arc", "-dynamiclib",
            "-framework", "UserNotifications", "-framework", "Foundation", source, "-o", library,
        ], check=True)
        env = env or loader_environment()
        inserts = env.get("DYLD_INSERT_LIBRARIES")
        env["DYLD_INSERT_LIBRARIES"] = f"{inserts}:{library}" if inserts else str(library)

    def run(flags, url=origin):
        return launch(executable, root, url, flags, env=env)

    if notification_delivery:
        for reopening in (False, True):
            server.reopening = reopening
            inputs = (EXTENSION_REGISTRY_SWITCH, EXTENSION_PEER_SWITCH) if reopening else switch_flags
            result = run([*flags, *inputs, f"{EXTENSION_STATE_SWITCH}=manage-smoke"])
            marker = "Zitter Content verified profile-scoped notification delivery and actions"
            if result.returncode or marker not in result.stdout.splitlines():
                phase = "restart" if reopening else "initial launch"
                raise SystemExit(f"Notification delivery probe failed on {phase}.\n"
                                 f"{result.stdout}\n{result.stderr}")
        print("Notification create, update, clear and delegate events stayed in the owning profile")
        return

    profiles = [PROFILES[owner]] * 2 if multi else PROFILES
    paths = [root / profile / "Extensions" / extension_id / "user-enabled"
             for profile, extension_id in zip(profiles, extension_ids)]
    original_install = paths[owner].parent
    reinstall_archive = (archives[owner].read_bytes()
                         if not multi and restore_flags is None else None)
    if multi:
        corrupt = archives[1 - owner].with_name("corrupt.crx")
        data = archives[1 - owner].read_bytes()
        corrupt.write_bytes(data[:-1] + bytes([data[-1] ^ 1]))
        require_rejection(run([
            *flags, switch_flags[0], f"{EXTENSION_SIBLING_SWITCH}={corrupt}",
            f"{EXTENSION_STATE_SWITCH}=manage-smoke",
        ]), "Content extension CRX3 verification failed")
        if (server.requests or not (paths[owner].parent / "active-version").is_file()
                or paths[1 - owner].parent.exists()):
            raise SystemExit("A rejected sibling changed the first signed installation")
        switch_flags = [EXTENSION_REGISTRY_SWITCH, switch_flags[1]]
    rule_pref_switch = "--zitter-content-extension-rule-prefs-probe"
    rule_pref_paths = [root / profile / "Content Extension Rule Preferences" for profile in profiles]
    for reopening in (False, True):
        server.reopening = reopening
        if webrequest_auth:
            server.extension_auth_requests = []
        if dnr:
            server.dnr_navigation_requests = []
            server.dnr_plain_upgrade_requests = []
            server.dnr_tls_attempts = []
        if reopening and restore_flags is not None:
            restore = [*restore_flags, EXTENSION_RESTORE_SWITCH,
                       f"{EXTENSION_STATE_SWITCH}=manage-smoke"]
            if extra_profile:
                profile = move_extension_to_additional_profile(root, origin, original_install)
                profiles[owner] = profile.name
                paths[owner] = profile / "Extensions" / extension_ids[owner] / "user-enabled"
            else:
                trust = original_install / "trusted-key-sha256"
                original_trust = trust.read_bytes()
                trust.unlink()
                explicit = [flag for flag in flags
                            if not flag.startswith(EXTENSION_SIBLING_KEY_SWITCH + "=")]
                result = run([*explicit, EXTENSION_REGISTRY_SWITCH])
                if (result.returncode or MARKER not in result.stdout.splitlines()
                        or trust.read_bytes() != original_trust):
                    raise SystemExit(f"Explicit verification did not backfill trust.\n"
                                     f"{result.stdout}\n{result.stderr}")
            verify_trusted_restore_rejections(executable, root, server, restore, paths[owner].parent)
            result = run(restore, url=None)
        else:
            rule_pref_flags = []
            if privacy and reopening:
                rule_pref_flags.append("--zitter-content-extension-privacy-restore-probe")
            if dnr and reopening:
                rule_pref_flags.append("--zitter-content-extension-dnr-restore-probe")
            if reinstall_archive is not None and not dnr:
                phase = "restore" if reopening else "write"
                rule_pref_flags.append(f"{rule_pref_switch}={phase}")
            result = run([
                *flags, *switch_flags, *rule_pref_flags, f"{EXTENSION_STATE_SWITCH}=manage-smoke",
            ])
        lines = result.stdout.splitlines()
        if webrequest_auth:
            requests = server.extension_auth_requests
            authenticated = [request for request in requests if request[1]]
            marker = "Zitter Content scoped extension HTTP credentials to the authorized site and profile"
            if (marker not in lines or authenticated != [("127.0.0.1", True, True)]
                    or requests.count(("127.0.0.1", False, False)) < 3
                    or ("localhost", False, False) not in requests):
                raise SystemExit(f"Extension HTTP auth probe failed. Requests: {requests}\n"
                                 f"{result.stdout}\n{result.stderr}")
        if clipboard and "Zitter Content verified the isolated offscreen pasteboard" not in lines:
            raise SystemExit(f"Offscreen pasteboard probe failed.\n{result.stdout}\n{result.stderr}")
        if offscreen and (lines.count(
                "Zitter Content verified the offscreen document in its owning profile") < 2 or
                "Zitter Content verified user script execution and revocation" not in lines):
            raise SystemExit(f"Offscreen and user script probe failed.\n{result.stdout}\n{result.stderr}")
        if dnr and not {
                "Zitter Content verified profile-scoped static network rules",
                "Zitter Content verified static, dynamic and session rule APIs",
                "Zitter Content verified DNR navigations and private subresource isolation",
        }.issubset(lines):
            raise SystemExit(f"Static rules probe failed.\n{result.stdout}\n{result.stderr}")
        if dnr:
            for mutation in ("dynamic", "session", "static", "enabled"):
                if f"Zitter Content isolated history eviction for {mutation} rules" not in lines:
                    raise SystemExit(f"The history cache probe did not verify {mutation} rules")
            if len(server.dnr_navigation_requests) != 4:
                raise SystemExit(f"Unexpected DNR navigation requests: {server.dnr_navigation_requests}")
            if len(server.dnr_plain_upgrade_requests) != 2 or not server.dnr_tls_attempts:
                raise SystemExit("The DNR upgrade did not reach TLS only in the owning profile")
            private_profile = root / "extension-dnr-private-probe"
            if any((private_profile / name).exists() for name in (
                    "Extensions", "Content Extension Rule Preferences", "DNR Extension Rules")):
                raise SystemExit("The private context acquired extension ruleset state")
        if reinstall_archive is not None and not dnr:
            marker = "restored" if reopening else "recorded"
            if f"Zitter Content {marker} profile-specific ruleset preferences" not in lines:
                raise SystemExit(f"Ruleset preference probe failed.\n{result.stdout}\n{result.stderr}")
            if not reopening:
                for index, (path, extension_id, checksum) in enumerate(
                    zip(rule_pref_paths, extension_ids, (101, 202))
                ):
                    saved = json.loads(path.read_bytes())
                    settings = saved["extensions"]["settings"]
                    rule_settings = settings
                    if offscreen:
                        runtime = settings.get(extension_id, {})
                        if (bool(runtime.get("user_scripts_enabled")) != (index == owner)
                                or ("http://127.0.0.1/*" in
                                    runtime.get("persistent_script_url_patterns", [])) != (index == owner)):
                            raise SystemExit(f"Incorrect user script preferences for {path.parent.name}")
                        rule_settings = {
                            ext_id: {key: value for key, value in prefs.items()
                                     if key.startswith("dnr_")}
                            for ext_id, prefs in settings.items()
                        }
                    if rule_settings != {extension_id: {
                        "dnr_static_ruleset": {"1": {"checksum": checksum}},
                        "dnr_dynamic_ruleset": {"checksum": checksum + 1},
                        "dnr_enabled_ruleset_ids": [1],
                        "dnr_use_action_count_as_badge_text": True,
                    }}:
                        raise SystemExit(f"Incorrect ruleset preferences for {path.parent.name}")
                    settings[extension_id]["path"] = "unexpected-location"
                    settings["p" * 32] = {"dnr_dynamic_ruleset": {"checksum": 999}}
                    path.write_text(json.dumps(saved), encoding="utf-8")
        toggle_count = 4 if reopening else 3
        expected_evictions = 0 if multi or (reopening and restore_flags is not None) else toggle_count
        if (result.returncode or ":FATAL:" in result.stderr or MARKER not in lines
                or EXTENSION_MANAGED_MARKER not in lines
                or lines.count(EXTENSION_SETTINGS_MARKER) !=
                    toggle_count + (2 + int(not reopening) if offscreen else 0)
                or lines.count(EXTENSION_CACHE_EVICTION_MARKER) != expected_evictions):
            raise SystemExit(f"Managed extension lifecycle failed.\n{result.stdout}\n{result.stderr}")
        if paths[owner].read_bytes() != b"disabled" or paths[1 - owner].exists():
            raise SystemExit("Managed extension controls changed the wrong installation")
        if dnr:
            extension_id = extension_ids[owner]
            saved = json.loads(rule_pref_paths[owner].read_bytes())
            rule_prefs = saved["extensions"]["settings"][extension_id]
            rule_source = (root / profiles[owner] / "DNR Extension Rules" /
                           extension_id / "rules.json")
            index = rule_source.with_suffix(".fbs")
            if (paths[owner].with_name("dnr-recovery").read_bytes() != b"1"
                    or not rule_prefs.get("dnr_recovery_marker")
                    or "checksum" not in rule_prefs["dnr_dynamic_ruleset"]
                    or not index.is_file()):
                raise SystemExit("The extension did not establish recoverable dynamic rules")
            if not reopening:
                if owner == 0:
                    del rule_prefs["dnr_dynamic_ruleset"]
                    rule_pref_paths[owner].write_text(json.dumps(saved), encoding="utf-8")
                else:
                    rules = json.loads(rule_source.read_bytes())
                    rules[0]["action"] = {"type": "allow"}
                    rule_source.write_text(json.dumps(rules), encoding="utf-8")
            else:
                print("Recovered dynamic rules after interrupting the preference or index write")
        for profile, extension_id, version in zip(profiles, extension_ids, ("1.0", "2.0")):
            installed = root / profile / "Extensions" / extension_id
            store = root / profile / "Local Extension Settings" / extension_id
            if ((installed / "active-version").read_text(encoding="ascii") != version
                    or not (store / "CURRENT").is_file()):
                raise SystemExit(f"The managed extension did not create its own storage in {profile}")
            snapshot = extension_preferences(root / profile).get(extension_id, {})
            if set(snapshot) != {"active_permissions"} or not snapshot["active_permissions"].get("api"):
                raise SystemExit(f"The managed extension did not persist its grants in {profile}")
        if multi:
            other = root / PROFILES[1 - owner]
            if (other / "Extensions").exists() or (other / "Local Extension Settings").exists():
                raise SystemExit("The sibling installations escaped the owning profile")
        if reopening and extra_profile:
            previous_store = (original_install.parent.parent / "Local Extension Settings" /
                              extension_ids[owner])
            saved = json.loads((root / SESSION_FILE).read_text(encoding="utf-8"))
            metadata = {"id": profiles[owner], "name": "Research profile"}
            if os.name != "nt":
                metadata["separate_network"] = True
            if (original_install.exists() or previous_store.exists()
                    or saved["extra_profiles"] != [metadata]):
                raise SystemExit("Restore used the old profile or changed the profile catalogue")
        for archive in archives:
            archive.unlink(missing_ok=True)
        switch_flags = [
            EXTENSION_REGISTRY_SWITCH, EXTENSION_SIBLING_SWITCH if multi else EXTENSION_PEER_SWITCH,
        ]
    print(f"Managed extension controls completed two independent sessions in {profiles[owner]}")
    if offscreen:
        print("Offscreen and user scripts passed profile isolation and restart checks")
        print("Sandboxed extension pages ran packaged scripts without access to the parent or extension APIs")
    if webrequest_auth:
        print("Extension HTTP credentials passed host and profile isolation checks before and after restart")
    if privacy:
        print("Extension password saving override, change events and clearing passed across restart")
    if clipboard:
        print("The authorized offscreen document read and wrote the isolated pasteboard")
    if reinstall_archive is not None:
        session_path = root / SESSION_FILE
        before_uninstall = json.loads(session_path.read_bytes())
        if dnr:
            previous_rule_prefs = rule_pref_paths[owner].read_bytes()
            previous_rule_json = rule_source.read_bytes()
            legacy_prefs = json.loads(previous_rule_prefs)
            del legacy_prefs["extensions"]["settings"][extension_ids[owner]]["dnr_recovery_marker"]
            rule_pref_paths[owner].write_text(json.dumps(legacy_prefs), encoding="utf-8")
            paths[owner].with_name("dnr-recovery").unlink()
        uninstall_url = f"{origin}/extension-uninstall/{profiles[owner]}"
        (paths[owner].parent / "uninstall-url").write_text(uninstall_url, encoding="ascii")
        removed = run([
            *flags, *switch_flags, f"{EXTENSION_STATE_SWITCH}=manage-smoke",
            "--zitter-content-extension-uninstall-probe",
            *(["--zitter-content-extension-dnr-restore-probe"] if dnr else []),
        ])
        owner_store = root / profiles[owner] / "Local Extension Settings" / extension_ids[owner]
        peer_store = root / profiles[1 - owner] / "Local Extension Settings" / extension_ids[1 - owner]
        if (removed.returncode or "Zitter Content removed an extension without restarting its peer"
                not in removed.stdout.splitlines() or paths[owner].parent.exists()
                or owner_store.exists() or not paths[1 - owner].parent.is_dir()
                or not (peer_store / "CURRENT").is_file()):
            raise SystemExit(f"Settings uninstall changed the wrong profile.\n"
                             f"{removed.stdout}\n{removed.stderr}")
        if (extension_ids[owner] in extension_preferences(root / profiles[owner])
                or extension_ids[1 - owner] not in extension_preferences(root / profiles[1 - owner])):
            raise SystemExit("Settings uninstall changed the wrong permission preferences")
        print("Settings removed only the chosen profile's extension and local storage")
        if not dnr:
            if (json.loads(rule_pref_paths[owner].read_bytes())["extensions"]["settings"] or
                    extension_ids[1 - owner] not in
                    json.loads(rule_pref_paths[1 - owner].read_bytes())["extensions"]["settings"]):
                raise SystemExit("Uninstall removed the wrong profile's ruleset preferences")
            print("Native ruleset preferences survived restart and were removed only for the uninstalled extension")
        else:
            # Reproduce interrupted uninstall cleanup after the package directory
            # and its recovery marker were deleted.
            rule_pref_paths[owner].write_bytes(previous_rule_prefs)
            rule_source.parent.mkdir(parents=True, exist_ok=True)
            rule_source.write_bytes(previous_rule_json)
        archives[owner].write_bytes(reinstall_archive)
        session = json.loads(session_path.read_bytes())
        opened = []
        for workspace_id, workspace in session.items():
            if not isinstance(workspace, dict) or "tabs" not in workspace:
                continue
            tabs = workspace["tabs"]
            for index in range(len(tabs) - 1, -1, -1):
                if tabs[index].get("url") == uninstall_url:
                    opened.append(workspace["profile_id"])
                    tabs.pop(index)
                    workspace["active"] = before_uninstall[workspace_id]["active"]
        if opened != [("work", "personal")[owner]]:
            raise SystemExit(f"Settings opened the uninstall URL in the wrong profile: {opened}")
        if (f"/extension-uninstall/{profiles[owner]}", "") not in server.requests:
            raise SystemExit("The browser did not fetch the uninstall page after confirmation")
        print("Settings opened the uninstall page in the profile that owned the extension")
        session["active_workspace"] = "personal"
        session_path.write_text(json.dumps(session), encoding="utf-8")
        reinstalled = run([
            *flags, f"{EXTENSION_REGISTRY_SWITCH}={archives[owner]}", EXTENSION_PEER_SWITCH,
            f"{EXTENSION_STATE_SWITCH}=manage-smoke",
        ])
        owner_version = ("1.0", "2.0")[owner]
        package = paths[owner].parent / f"{owner_version}_0/fixture.crx"
        if (reinstalled.returncode or EXTENSION_MANAGED_MARKER not in reinstalled.stdout.splitlines()
                or not package.is_file() or package.read_bytes() != reinstall_archive
                or not (owner_store / "CURRENT").is_file()
                or not (peer_store / "CURRENT").is_file()):
            raise SystemExit(f"The signed package could not reinstall after removal.\n"
                             f"{reinstalled.stdout}\n{reinstalled.stderr}")
        print("The same signed package reinstalled after removal and recreated its profile store")
        if dnr:
            print("Fresh reinstall ignored the removed installation's leftover rules and preferences")


def move_extension_to_additional_profile(root, origin, installed):
    profile = root / "profile-18b282f1-2626-4cdd-8655-8ab8704a4d98"
    workspace_id = "space-d41f792a-d442-46ed-8447-f97c052199e6"
    profile.mkdir()
    metadata = {"id": profile.name, "name": "Research profile"}
    if os.name == "nt":
        (profile / "Network").mkdir()
        (profile / "NetworkBootstrap").mkdir()
    else:
        (root / "NetworkProfiles" / profile.name).mkdir(parents=True)
        metadata["separate_network"] = True
    for container in ("Extensions", "Local Extension Settings"):
        destination = profile / container
        destination.mkdir()
        source = installed.parent.parent / container / installed.name
        source.rename(destination / installed.name)
    orphan = (root / "profile-cf34ff9e-b5fc-4205-8259-bf27d61941ce" /
              "Extensions" / installed.name)
    orphan.mkdir(parents=True)
    (orphan / "trusted-key-sha256").write_text("invalid", encoding="ascii")
    session_file = root / SESSION_FILE
    session = json.loads(session_file.read_text(encoding="utf-8"))
    session["extra_profiles"] = [metadata]
    session["extra_workspaces"] = [{"id": workspace_id, "name": "Research renamed"}]
    session[workspace_id] = {
        "profile_id": profile.name, "active": 0, "tabs": [
            {"url": origin + EXTENSION_UNMATCHED_PATH, "title": "Research"},
        ],
    }
    session["active_workspace"] = workspace_id
    session_file.write_text(json.dumps(session), encoding="utf-8")
    return profile


def verify_trusted_restore_rejections(executable, root, server, flags, installed):
    trust = installed / "trusted-key-sha256"
    trusted_key = trust.read_bytes()
    version = (installed / "active-version").read_text(encoding="ascii")
    archive = installed / (version + "_0") / "fixture.crx"
    signed_package = archive.read_bytes()
    server.requests.clear()
    for path, original, invalid, error in (
        (trust, trusted_key, b"0" * 64, "Invalid Content extension trust record"),
        (archive, signed_package, signed_package + b"!", "Could not restore Content extension fixture"),
    ):
        path.write_bytes(invalid)
        try:
            require_rejection(launch(executable, root, None, flags), error)
            if server.requests or path.read_bytes() != invalid:
                raise SystemExit("A rejected trust restore loaded pages or rewrote the installation")
        finally:
            path.write_bytes(original)
    if os.name != "nt":
        outside = root / "external-extension-trust"
        trust.rename(outside)
        trust.symlink_to(outside)
        try:
            require_rejection(launch(executable, root, None, flags),
                              "Invalid Content extension trust record")
            if server.requests or outside.read_bytes() != trusted_key:
                raise SystemExit("A linked trust record changed the workspace")
        finally:
            trust.unlink()
            outside.rename(trust)
    unapproved = installed.parent / "pppppppppppppppppppppppppppppppp"
    shutil.copytree(installed, unapproved)
    (unapproved / trust.name).unlink()


def verify_extension_choice(executable, root, origin, server, extension_id, profile, flags,
                            *, prior_launches):
    other = "personal" if profile == "work" else "work"
    initial = {"work": prior_launches, "personal": 40 + prior_launches}
    owner_root = root / f"workspace-{profile}"
    state = owner_root / "Extensions" / extension_id / "user-enabled"
    other_state = root / f"workspace-{other}" / "Extensions" / extension_id / state.name
    store = owner_root / "Local Extension Settings" / extension_id

    def contents():
        return {path.name: path.read_bytes() for path in store.iterdir() if path.is_file()}

    def run(name, *markers, action=None, counter=None):
        arguments = [*flags, f"{EXTENSION_PROFILE_SWITCH}={name}"]
        if action:
            arguments.append(f"{EXTENSION_STATE_SWITCH}={action}")
        result = launch(executable, root, origin, arguments)
        lines = result.stdout.splitlines()
        expected = {MARKER, *markers}
        if counter is not None:
            expected.add(EXTENSION_STORAGE_MARKER + str(counter))
        elif EXTENSION_STORAGE_MARKER in result.stdout:
            raise SystemExit("A disabled extension ran its startup worker")
        if result.returncode or ":FATAL:" in result.stderr or not expected.issubset(lines):
            raise SystemExit(f"Extension choice probe failed.\n{result.stdout}\n{result.stderr}")

    run(profile, EXTENSION_SETTINGS_MARKER, "Zitter Content persisted extension disablement",
        action="disable", counter=initial[profile] + 1)
    if state.read_bytes() != b"disabled" or other_state.exists():
        raise SystemExit("Disabling the extension did not stay in its profile")
    saved = contents()
    server.requests.clear()
    state.write_bytes(b"invalid")
    require_rejection(launch(executable, root, origin, [
        *flags, f"{EXTENSION_PROFILE_SWITCH}={profile}",
    ]), "Invalid Content extension enablement record")
    if server.requests or state.read_bytes() != b"invalid" or contents() != saved:
        raise SystemExit("A malformed extension enablement record changed the profile")
    state.write_bytes(b"disabled")
    if os.name != "nt":
        outside = root / "external-enable-choice"
        state.rename(outside)
        state.symlink_to(outside)
        require_rejection(launch(executable, root, origin, [
            *flags, f"{EXTENSION_PROFILE_SWITCH}={profile}",
        ]), "Invalid Content extension enablement record")
        if server.requests or not state.is_symlink() or outside.read_bytes() != b"disabled":
            raise SystemExit("A linked extension enablement record changed the profile")
        state.unlink()
        outside.rename(state)
    run(profile, "Zitter Content restored the extension disabled by the user",
        action="check-disabled")
    run(other, counter=initial[other] + 1)
    if contents() != saved or state.read_bytes() != b"disabled" or other_state.exists():
        raise SystemExit("A disabled extension changed while reopening the profiles")
    run(profile, EXTENSION_SETTINGS_MARKER,
        "Zitter Content restored the extension disabled by the user",
        "Zitter Content persisted extension enablement",
        action="enable", counter=initial[profile] + 2)
    run(profile, counter=initial[profile] + 3)
    if state.read_bytes() != b"enabled" or other_state.exists():
        raise SystemExit("Enabling the extension did not stay in its profile")

    # Stop the browser after the atomic preference write and before unloading.
    arguments = [*flags, f"{EXTENSION_PROFILE_SWITCH}={profile}",
                 f"{EXTENSION_STATE_SWITCH}=pause-after-disable-commit"]
    with tempfile.TemporaryDirectory(prefix="zitter-extension-interruption-") as directory:
        log = Path(directory) / "browser.log"
        with log.open("wb") as output:
            process = subprocess.Popen(workspace_command(executable, root, origin, arguments),
                                       stdout=output, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 30
                while True:
                    text = log.read_text(encoding="utf-8", errors="replace")
                    if (process.poll() is None and EXTENSION_SETTINGS_MARKER in text
                            and "Zitter Content persisted extension disablement" in text):
                        break
                    if process.poll() is not None or time.monotonic() >= deadline:
                        raise SystemExit(f"Extension interruption probe failed.\n{text}")
                    time.sleep(0.05)
            finally:
                if process.poll() is None:
                    process.kill()
                process.wait(timeout=10)
        if EXTENSION_WORKER_MARKER in text:
            raise SystemExit("The extension worker was unregistered before interruption")
    if state.read_bytes() != b"disabled":
        raise SystemExit("The interrupted disable lost the user choice")
    run(profile, "Zitter Content restored the extension disabled by the user",
        action="check-disabled")
    run(profile, EXTENSION_SETTINGS_MARKER,
        "Zitter Content removed a stale extension worker registration",
        "Zitter Content persisted extension enablement",
        action="enable", counter=initial[profile] + 5)
    if state.read_bytes() != b"enabled" or other_state.exists():
        raise SystemExit("Recovering the interrupted disable changed the other profile")
    print(f"The {profile} extension kept its user choice and storage across restart")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path, help="Zitter Content Lab.app or zitter_content.exe")
    probe = parser.add_mutually_exclusive_group()
    probe.add_argument("--assistant-settings", action="store_true")
    probe.add_argument("--history-panel", action="store_true")
    probe.add_argument("--address-history", action="store_true")
    probe.add_argument("--closed-tabs", action="store_true")
    probe.add_argument("--navigation", action="store_true")
    probe.add_argument("--find-reload", action="store_true")
    probe.add_argument("--command-center", action="store_true")
    probe.add_argument("--copy-address", action="store_true")
    probe.add_argument("--rss", action="store_true")
    probe.add_argument("--browser-import", action="store_true")
    probe.add_argument("--interactive-import", action="store_true")
    probe.add_argument("--encrypted-import", action="store_true")
    probe.add_argument("--extra-workspace", action="store_true")
    probe.add_argument("--reassign-workspace", action="store_true")
    probe.add_argument("--shared-favorites", action="store_true")
    probe.add_argument("--tab-commands", action="store_true")
    probe.add_argument("--split-actions", action="store_true")
    probe.add_argument("--link-routing", action="store_true")
    probe.add_argument("--popup-commands", action="store_true")
    probe.add_argument("--popup-policy", action="store_true")
    probe.add_argument("--peek", action="store_true")
    probe.add_argument("--shift-peek", action="store_true")
    probe.add_argument("--additional-profile", action="store_true")
    probe.add_argument("--create-profile", action="store_true")
    probe.add_argument("--print-pdf", action="store_true")
    probe.add_argument("--print-preview", action="store_true")
    probe.add_argument("--scripted-print", action="store_true")
    probe.add_argument("--system-print-cancel", action="store_true")
    probe.add_argument("--system-print-file", action="store_true")
    probe.add_argument("--private-windows", action="store_true")
    probe.add_argument("--page-zoom", action="store_true")
    probe.add_argument("--clear-data", action="store_true")
    probe.add_argument("--clear-site-settings", action="store_true")
    probe.add_argument("--favicons", action="store_true")
    probe.add_argument("--extension-registry", action="store_true")
    probe.add_argument("--extension-pair", action="store_true")
    probe.add_argument("--extension-manage", action="store_true")
    parser.add_argument("--extension-offscreen", action="store_true",
                        help="Exercise signed offscreen documents with --extension-manage")
    parser.add_argument("--extension-offscreen-clipboard", action="store_true",
                        help="Exercise legacy offscreen clipboard on an isolated macOS pasteboard")
    parser.add_argument("--extension-webrequest-auth", action="store_true",
                        help="Verify profile and host isolation for signed extension HTTP credentials")
    parser.add_argument("--extension-notifications", action="store_true",
                        help="Query the macOS notification permission without showing a notification")
    parser.add_argument("--extension-notification-delivery", action="store_true",
                        help="Exercise extension notification delivery and actions with an isolated native fixture")
    parser.add_argument("--extension-privacy", action="store_true",
                        help="Exercise signed extension control of the profile password setting")
    parser.add_argument("--extension-native-messaging", action="store_true",
                        help="Exercise local native messaging with a disposable echo host")
    parser.add_argument("--extension-dnr", action="store_true",
                        help="Exercise signed static rules with --extension-manage")
    probe.add_argument("--extension-multi", action="store_true")
    probe.add_argument("--extension-restore-extra-profile", action="store_true")
    parser.add_argument("--extension-module-worker", action="store_true",
                        help="run the signed registry trial with a static worker module import")
    parser.add_argument("--interactive-extension-management", action="store_true",
                        help="keep the disposable signed extension workspace open")
    parser.add_argument("--restore-installed-extensions", action="store_true",
                        help="restart the managed trial using only its persisted trust records")
    parser.add_argument("--extension-profile", choices=("work", "personal"),
                        help="select the single profile or disable this profile first (default: work)")
    parser.add_argument("--crx-signer", type=Path,
                        help="Path to the local CRX signing helper for the extension probes")
    parser.add_argument("--openssl", type=Path,
                        help="OpenSSL executable for signing disposable extension fixtures")
    parser.add_argument("--theme", choices=("light", "dark"), default="light")
    parser.add_argument("--renderer", choices=("compositor", "metal"), default="compositor")
    options = parser.parse_args()
    windows = os.name == "nt"
    encrypted_prefix = b"v10" if windows else ENCRYPTED_PREFIX
    executable = options.app.resolve() if windows else (
        options.app / "Contents/MacOS/Zitter Content Lab"
    ).resolve()
    if not executable.is_file():
        parser.error(f"App executable is missing: {executable}")
    if windows and (options.system_print_cancel or options.system_print_file):
        parser.error("The system print probes require macOS")
    if options.copy_address and sys.platform != "darwin":
        parser.error("--copy-address requires the macOS pasteboard fixture")
    if windows and options.renderer == "metal":
        parser.error("Metal requires macOS")
    if options.encrypted_import and sys.platform != "darwin":
        parser.error("--encrypted-import requires macOS")
    managed_extensions = (options.extension_manage or options.extension_multi or
                          options.extension_restore_extra_profile)
    extension_probe = options.extension_registry or options.extension_pair or managed_extensions
    if options.extension_offscreen_clipboard and (
        not options.extension_offscreen or sys.platform != "darwin"
    ):
        parser.error("--extension-offscreen-clipboard requires the macOS offscreen probe")
    if options.extension_webrequest_auth and not options.extension_offscreen:
        parser.error("--extension-webrequest-auth requires --extension-offscreen")
    if options.extension_privacy and not options.extension_offscreen:
        parser.error("--extension-privacy requires --extension-offscreen")
    if options.extension_native_messaging and (not options.extension_offscreen or sys.platform != "darwin"):
        parser.error("--extension-native-messaging requires the macOS offscreen probe")
    if (options.extension_notifications or options.extension_notification_delivery) and (
        not options.extension_offscreen or sys.platform != "darwin"
    ):
        parser.error("Extension notification probes require the macOS offscreen probe")
    if options.extension_offscreen and (
        not options.extension_manage or options.extension_multi or options.extension_dnr
        or options.restore_installed_extensions or options.extension_restore_extra_profile
        or options.interactive_extension_management
    ):
        parser.error("--extension-offscreen requires the automated two-profile --extension-manage probe")
    if options.interactive_extension_management and (
        not managed_extensions or options.extension_restore_extra_profile
    ):
        parser.error("--interactive-extension-management requires --extension-manage or --extension-multi")
    if options.restore_installed_extensions and (
        not managed_extensions or options.interactive_extension_management
    ):
        parser.error("--restore-installed-extensions requires an automated extension management probe")
    if options.extension_profile and not extension_probe:
        parser.error("--extension-profile requires an extension probe")
    if options.extension_module_worker and not options.extension_registry:
        parser.error("--extension-module-worker requires --extension-registry")
    if (options.crx_signer or options.openssl) and not extension_probe:
        parser.error("--crx-signer and --openssl require an extension probe")
    if extension_probe:
        signer_name = "crx3_build_action.exe" if windows else "crx3_build_action"
        signer = (options.crx_signer or options.app.resolve().parent / signer_name).resolve()
        openssl = options.openssl or shutil.which("openssl")
        if not signer.is_file() or not openssl or not Path(openssl).is_file():
            parser.error("The extension probes require Chromium's crx3_build_action and OpenSSL")
        openssl = Path(openssl).resolve()
    extra_flags = [
        f"--zitter-content-workspace-theme={options.theme}",
        f"--zitter-rust-raster={options.renderer}",
    ]
    if options.extension_registry:
        extra_flags.append("--host-resolver-rules=MAP *.example.com 127.0.0.1")
    if options.history_panel:
        extra_flags.append("--zitter-content-history-panel-smoke-test")
    if options.address_history:
        extra_flags.append("--zitter-content-address-history-smoke-test")

    with (
        ExitStack() as fixtures,
        WorkspaceServer(("127.0.0.1", 0), WorkspaceFixture) as server,
        tempfile.TemporaryDirectory(prefix="zitter-content-probe-") as directory,
    ):
        root = Path(directory).resolve()
        server.extension_host_requests = []
        if options.extension_registry:
            extension_profile = options.extension_profile or "work"
            other_profile = "personal" if extension_profile == "work" else "work"
            extension_profile_flags = (
                [f"{EXTENSION_PROFILE_SWITCH}={extension_profile}"]
                if options.extension_profile else []
            )
            package = Path(fixtures.enter_context(
                tempfile.TemporaryDirectory(prefix="zitter-content-extension-")
            )).resolve()
            private_key = package / "signing-key.der"
            public_key = make_signing_key(openssl, private_key)
            manifest = write_fixture(package, base64.b64encode(public_key).decode("ascii"))
            manifest["host_permissions"] = ["http://localhost/*"]
            manifest["permissions"].extend([
                "activeTab", "clipboardRead", "clipboardWrite", "contextMenus", "fontSettings",
                "scripting", "sidePanel", "webNavigation",
            ])
            review_hosts = ([f"https://extension-review-{index:02}.example.invalid/*"
                             for index in range(36)] if options.extension_registry else [])
            manifest["host_permissions"].extend(review_hosts)
            manifest["host_permissions"].append("https://scope.example.com/*")
            manifest["optional_permissions"] = ["alarms"]
            manifest["optional_host_permissions"] = [
                "*://*/*", "https://*.scope.example.com/*",
            ]
            manifest["options_ui"] = {"page": "probe.html#options", "open_in_tab": True}
            manifest["action"] = {"default_popup": "probe.html#popup"}
            manifest["side_panel"] = {"default_path": "probe.html"}
            manifest["commands"] = {
                "_execute_action": {"suggested_key": {"default": "Alt+Shift+Z"}},
                "fixture-command": {
                    "description": "Fixture shortcut",
                    "suggested_key": {"default": "Alt+Shift+Y"},
                },
                "unassigned": {"description": "Unassigned fixture command"},
            }

            manifest["author"] = "Zitter fixture"
            manifest["minimum_chrome_version"] = "106.0.0.0"
            extension_csp = "script-src 'self'; object-src 'none'; style-src 'none';"
            manifest["content_security_policy"] = {"extension_pages": extension_csp}
            manifest["web_accessible_resources"] = [{
                "resources": ["assets/data.bin"],
                "matches": ["http://127.0.0.1/*"],
            }, {
                "resources": ["assets/empty.txt"],
                "matches": ["<all_urls>"],
                "use_dynamic_url": True,
            }]
            with (package / "probe.js").open("a", encoding="utf-8") as options_script:
                options_script.write("""
if (location.hash === '#options' || location.hash === '#popup') {
  const type = location.hash.slice(1);
  const verify = async () => {
    const {zitterFrameStorage} = await chrome.storage.local.get('zitterFrameStorage');
    if (zitterFrameStorage !== 'frame-value')
      throw Error('Extension page read the wrong profile');
    if (chrome.extension.getBackgroundPage() !== null)
      throw Error('The Manifest V3 extension exposed a background Window');
    const current = await chrome.tabs.getCurrent();
    if (type === 'popup' ? current !== undefined : !Number.isInteger(current?.id))
      throw Error('tabs.getCurrent did not distinguish a popup from a browser tab');
    if (type === 'popup') {
      const clipboard = await Promise.all(['clipboard-read', 'clipboard-write'].map(
        name => navigator.permissions.query({name, allowWithoutGesture: true})));
      if (clipboard.some(status => status.state !== 'granted'))
        throw Error('The focused extension popup did not receive its clipboard permissions');
      const [tab] = await chrome.tabs.query({active: true});
      if (!tab || new URL(tab.url).hostname !== 'active-tab.localhost')
        throw Error('The activeTab grant did not expose the selected tab');
      const sidePanel = chrome.sidePanel;
      const panelOptions = await sidePanel.getOptions({});
      if (panelOptions.path !== 'probe.html' || !panelOptions.enabled ||
          (await sidePanel.getLayout()).side !== 'right')
        throw Error('The extension side panel default did not match its manifest');
      await sidePanel.setOptions({tabId: tab.id, path: 'probe.html', enabled: true});
      const specificPanel = await sidePanel.getOptions({tabId: tab.id});
      if (specificPanel.tabId !== tab.id || !specificPanel.enabled)
        throw Error('The extension side panel options did not follow the tab');
      await sidePanel.open({tabId: tab.id}).then(
        () => { throw Error('Side panel opened without a user gesture'); },
        error => { if (!String(error).includes('user gesture')) throw error; });
      await sidePanel.setOptions({tabId: tab.id, path: 'https://example.invalid/'}).then(
        () => { throw Error('An external URL was accepted as a side panel'); },
        () => {});
      const navigation = chrome.webNavigation;
      let committed;
      for (let attempt = 0; attempt < 60 && !committed; attempt++) {
        ({zitterNavigationProbe: committed} = await chrome.storage.local.get('zitterNavigationProbe'));
        if (!committed) await new Promise(resolve => setTimeout(resolve, 25));
      }
      const frames = await navigation.getAllFrames({tabId: tab.id});
      const root = frames.find(frame => frame.frameId === 0);
      const child = frames.find(frame => frame.parentFrameId === 0 &&
        new URL(frame.url).pathname === '/extension-active-tab-frame');
      if (!root || !child || child.parentDocumentId !== root.documentId ||
          committed?.tabId !== tab.id || committed.documentId !== root.documentId ||
          committed.lifecycle !== 'active' || !committed.completed)
        throw Error('Web navigation events and frame IDs did not match the active tab');
      const byDocument = await navigation.getFrame({documentId: child.documentId});
      const byFrame = await navigation.getFrame({tabId: tab.id, frameId: child.frameId});
      if (byDocument?.url !== child.url || byFrame?.documentId !== child.documentId ||
          await navigation.getFrame({documentId: child.documentId, frameId: 0}) != null ||
          await navigation.getAllFrames({tabId: 2147483647}) != null)
        throw Error('Web navigation frame lookups ignored their tab or document scope');
      const failedNavigation = new Promise((resolve, reject) => {
        const timer = setTimeout(() => {
          navigation.onErrorOccurred.removeListener(onError);
          reject(Error('No navigation error was delivered for the blocked child frame'));
        }, 2500);
        function onError(details) {
          clearTimeout(timer);
          navigation.onErrorOccurred.removeListener(onError);
          resolve(details);
        }
        navigation.onErrorOccurred.addListener(onError, {
          url: [{pathEquals: '/extension-navigation-error'}],
        });
      });
      const [injection] = await chrome.scripting.executeScript({
        target: {tabId: tab.id},
        func: () => {
          const failedFrame = document.createElement('iframe');
          failedFrame.src = 'http://127.0.0.1:9/extension-navigation-error';
          document.body.append(failedFrame);
          return location.hostname;
        },
      });
      if (injection?.result !== 'active-tab.localhost')
        throw Error('The activeTab grant did not allow injection into the selected tab');
      const error = await failedNavigation;
      if (error.tabId !== tab.id || error.parentFrameId !== 0 || !error.error.startsWith('net::'))
        throw Error('The navigation error used the wrong frame or tab');
      const observeSameDocument = (event, suffix) => new Promise((resolve, reject) => {
        const documents = new Set();
        const timer = setTimeout(() => {
          event.removeListener(listener);
          reject(Error('No same-document navigation event for ' + suffix));
        }, 2500);
        function listener(details) {
          if (details.tabId !== tab.id || !details.url.endsWith(suffix)) return;
          documents.add(details.documentId);
          if (documents.has(root.documentId) && documents.has(child.documentId)) {
            clearTimeout(timer);
            event.removeListener(listener);
            resolve();
          }
        }
        event.addListener(listener, {url: [{hostEquals: 'active-tab.localhost'}]});
      });
      const changes = Promise.all([
        observeSameDocument(navigation.onHistoryStateUpdated, '?nav-probe=1'),
        observeSameDocument(navigation.onReferenceFragmentUpdated, '#nav-proof'),
      ]);
      await chrome.scripting.executeScript({target: {tabId: tab.id}, func: () => {
        for (const target of [window, document.querySelector('iframe').contentWindow]) {
          target.history.pushState({}, '', '?nav-probe=1');
          target.location.hash = 'nav-proof';
        }
      }});
      await changes;
      const commands = await chrome.commands.getAll();
      if (!commands.some(command => command.name === 'fixture-command' && command.shortcut) ||
          !commands.some(command => command.name === 'unassigned' && !command.shortcut))
        throw Error('Extension commands did not report their assignments');
      const violation = new Promise(resolve => document.addEventListener(
        'securitypolicyviolation', event => {
          if (event.effectiveDirective === 'style-src-elem') resolve(event);
        }));
      const inlineStyle = document.createElement('style');
      inlineStyle.textContent = ':root { --zitter-csp-probe: allowed; }';
      document.head.append(inlineStyle);
      await violation;
      if (getComputedStyle(document.documentElement).getPropertyValue('--zitter-csp-probe'))
        throw Error('The custom CSP allowed an inline style');
      const fonts = await new Promise((resolve, reject) => {
        chrome.fontSettings.getFontList(result => {
          if (chrome.runtime.lastError) reject(Error(chrome.runtime.lastError.message));
          else resolve(result);
        });
      });
      if (!Array.isArray(fonts) || fonts.length === 0 || fonts.some(font =>
        typeof font.fontId !== 'string' || !font.fontId ||
        typeof font.displayName !== 'string' || !font.displayName)) {
        throw Error('The extension did not receive a valid system font list');
      }
    }
    document.title = 'Zitter extension ' + type + ' verified storage';
  };
  verify().catch(error => document.title = String(error));
}
""")
            content_script = package / "content.js"
            content_script.write_text(
                "if (globalThis.chrome?.permissions !== undefined ||"
                " globalThis.chrome?.fontSettings !== undefined ||"
                " globalThis.chrome?.contextMenus !== undefined ||"
                " globalThis.chrome?.webNavigation !== undefined)"
                " throw Error('Content script could access a privileged extension API');\n"
                "if (location.pathname === '/extension-content/public-resource')"
                " document.documentElement.dataset.zitterDynamicResource ="
                " chrome.runtime.getURL('assets/empty.txt');\n"
                "if (location.pathname === '/extension-content/public-resource')"
                " chrome.storage.onChanged.addListener((changes, area) => {"
                " if (area === 'local' && changes.zitterMenuClicked?.newValue)"
                " document.title = 'Extension context menu verified'; });\n"

                + content_script.read_text(encoding="utf-8"),
                encoding="utf-8",
            )
            worker = package / "worker.js"
            with worker.open("a", encoding="utf-8") as script:
                script.write("""
let zitterCommittedNavigation;
const navigationFilter = {url: [{hostEquals: 'active-tab.localhost',
                                pathEquals: '/extension-active-tab'}]};
chrome.webNavigation.onCommitted.addListener(details => {
  if (details.frameId === 0)
    zitterCommittedNavigation = details;
}, navigationFilter);
chrome.webNavigation.onCompleted.addListener(details => {
  if (details.frameId === 0 && details.documentId === zitterCommittedNavigation?.documentId) {
    chrome.storage.local.set({zitterNavigationProbe: {tabId: details.tabId,
      documentId: details.documentId, lifecycle: details.documentLifecycle, completed: true}});
  }
}, navigationFilter);
""")
            worker_source = worker.read_text(encoding="utf-8")
            grant_check = "    const {counter, legacyMarker}"
            storage_check = "    await chrome.storage.local.set({counter: counter + 1});"
            if worker_source.count(grant_check) != 1 or worker_source.count(storage_check) != 1:
                raise SystemExit("Could not attach the extension permission and storage checks")
            worker.write_text(
                worker_source.replace(grant_check, "    await checkHostGrant();\n" + grant_check)
                .replace(storage_check, "    await checkProfileSyncStorage(counter);\n" + storage_check)
                + f"""
chrome.runtime.onInstalled.addListener(async () => {{
  await chrome.action.setIcon({{path: {{16: 'assets/action.png'}}}});
  await chrome.action.setBadgeBackgroundColor({{color: '#0b518d'}});
  await chrome.action.setBadgeText({{text: 'OK'}});
}});
chrome.runtime.onInstalled.addListener(() => {{
  chrome.contextMenus.removeAll(() => {{
    chrome.contextMenus.create({{id: 'fixture-root', title: 'Zitter extension menu'}}, () => {{
      chrome.contextMenus.create({{
        id: 'fixture-child', parentId: 'fixture-root', title: 'Verify extension menu'
      }});
      chrome.contextMenus.create({{
        id: 'fixture-separator', parentId: 'fixture-root', type: 'separator'
      }});
      chrome.contextMenus.create({{
        id: 'fixture-disabled', parentId: 'fixture-root', title: 'Unavailable action',
        type: 'normal', enabled: false
      }});
    }});
  }});
}});
chrome.contextMenus.onClicked.addListener(async (info, tab) => {{
  if (info.menuItemId !== 'fixture-child' || info.parentMenuItemId !== 'fixture-root' ||
      !Number.isInteger(tab?.id) || tab.incognito || !info.pageUrl ||
      tab.url !== info.pageUrl || info.frameUrl !== undefined ||
      new URL(tab.url).pathname !== '/extension-content/public-resource')
    throw Error('The extension menu received the wrong activeTab metadata');
  const [injection] = await chrome.scripting.executeScript({{
    target: {{tabId: tab.id}}, func: () => location.pathname,
  }});
  if (injection?.result !== '/extension-content/public-resource')
    throw Error('The extension menu could not inject into the top-level page');
  await chrome.storage.local.set({{zitterMenuClicked: true}});
}});
chrome.commands.onCommand.addListener(async (command, tab) => {{
  if (command !== 'fixture-command' || !Number.isInteger(tab?.id) || tab.incognito ||
      !tab.url || new URL(tab.url).hostname !== 'active-tab-command.localhost')
    throw Error('The named command received the wrong activeTab metadata');
  await chrome.scripting.executeScript({{
    target: {{tabId: tab.id}},
    func: () => {{ document.title = 'Extension command grant verified'; }},
  }});
}});
async function checkProfileSyncStorage(counter) {{
  const {{syncCounter}} = await chrome.storage.sync.get('syncCounter');
  if (syncCounter !== (counter === 0 ? undefined : counter))
    throw Error('Sync storage did not match the persisted profile counter');
  if (counter === 0) {{
    const oversized = 'x'.repeat(chrome.storage.sync.QUOTA_BYTES_PER_ITEM + 1);
    await chrome.storage.sync.set({{oversized}}).then(
      () => {{ throw Error('Sync storage accepted an item beyond its quota'); }},
      error => {{
        if (!error.message.includes('Resource::kQuotaBytesPerItem'))
          throw error;
      }});
    if ('oversized' in await chrome.storage.sync.get('oversized'))
      throw Error('Sync storage persisted an item beyond its quota');
  }}
  await chrome.storage.sync.set({{syncCounter: counter + 1}});
  if ('syncCounter' in await chrome.storage.local.get('syncCounter'))
    throw Error('Sync and local storage used the same namespace');
}}
chrome.runtime.onMessage.addListener((message, sender, reply) => {{
  if (message?.probe === 'open-options') {{
    chrome.runtime.openOptionsPage().then(
      () => reply('Worker opened its extension options'),
      error => reply(String(error)));
    return true;
  }}
  if (message?.probe !== 'optional-permission') return;
  const optional = {{permissions: ['alarms'], origins: ['http://*.unapproved.example.com/*']}};
  const broad = chrome.permissions.request({{origins: ['*://*/*']}}).then(
    () => false,
    error => error.message === 'The requested access is outside the supported scope.');
  const request = chrome.permissions.request(optional);
  const verify = async () => {{
    if (!(await broad)) throw Error('Unbounded host request was not rejected');
    const granted = await request;
    if (granted !== message.approve || await chrome.permissions.contains(optional) !== granted)
      throw Error('Unexpected permission decision or active scope');
    if (!granted) return 'Optional permission cancelled';
    const url = 'http://unapproved.example.com:{server.server_port}/extension-host-grant';
    const response = await fetch(url, {{cache: 'no-store'}});
    if (!response.ok || await response.text() !== 'private fixture response')
      throw Error('Approved host response was unreadable');
    const nested = await fetch(url.replace('unapproved.example.com', 'child.unapproved.example.com'),
                               {{cache: 'no-store'}});
    if (!nested.ok || await nested.text() !== 'private fixture response')
      throw Error('Approved subdomain response was unreadable');
    await chrome.alarms.getAll();
    if (message.persist) {{
      await chrome.storage.local.set({{zitterOptionalRestart: 'pending-grant'}});
      return 'Optional permission saved for restart';
    }}
    if (!(await chrome.permissions.remove(optional)) || await chrome.permissions.contains(optional))
      throw Error('Permission removal did not clear the active scope');
    try {{
      await fetch(url, {{cache: 'no-store'}});
    }} catch (error) {{
      if (error instanceof TypeError) return 'Optional permission granted and removed';
      throw error;
    }}
    throw Error('Removed host response remained readable');
  }};
  verify().then(reply, error => reply(String(error)));
  return true;
}});
chrome.runtime.onStartup.addListener(() => {{
  if (chrome.runtime.getManifest().version !== '2.0') return;
  const verify = async () => {{
    const {{zitterOptionalRestart: state}} = await chrome.storage.local.get('zitterOptionalRestart');
    if (state !== 'pending-grant' && state !== 'removed-after-restart') return;
    const optional = {{permissions: ['alarms'], origins: ['http://*.unapproved.example.com/*']}};
    const host = {{origins: optional.origins}};
    const api = {{permissions: optional.permissions}};
    const url = 'http://unapproved.example.com:{server.server_port}/extension-host-grant';
    if (state === 'pending-grant') {{
      if (!(await chrome.permissions.contains(optional)))
        throw Error('Approved scope did not survive restart');
      const response = await fetch(url, {{cache: 'no-store'}});
      if (!response.ok || await response.text() !== 'private fixture response')
        throw Error('Restored host response was unreadable');
      await chrome.alarms.getAll();
      if (!(await chrome.permissions.remove(optional)))
        throw Error('Could not remove restored permissions');
      await chrome.storage.local.set({{zitterOptionalRestart: 'removed-after-restart'}});
      return;
    }}
    if (await chrome.permissions.contains(host) || await chrome.permissions.contains(api))
      throw Error('A removed permission returned after restart');
    try {{
      await fetch(url, {{cache: 'no-store'}});
    }} catch (error) {{
      if (error instanceof TypeError) {{
        await chrome.storage.local.set({{zitterOptionalRestart: 'revocation-verified'}});
        return;
      }}
      throw error;
    }}
    throw Error('Removed host remained readable after restart');
  }};
  verify().catch(error => chrome.storage.local.set({{zitterOptionalRestartError: String(error)}}));
}});
async function checkHostGrant() {{
  const grantedOrigin = 'http://localhost/*';
  const scriptOrigin = 'http://127.0.0.1/extension-content/allowed';
  const deniedOrigin = 'https://unlisted-extension.example.invalid/*';
  const optionalOrigin = 'http://*.unapproved.example.com/*';
  const grants = await chrome.permissions.getAll();
  if (grants.permissions?.slice().sort().join() !== 'activeTab,clipboardRead,clipboardWrite,contextMenus,fontSettings,scripting,sidePanel,storage,webNavigation' ||
      !grants.origins?.includes(grantedOrigin) ||
      !grants.origins.includes(scriptOrigin) ||
      grants.origins.includes(deniedOrigin) ||
      grants.origins.includes(optionalOrigin) ||
      grants.origins.includes('*://*/*') ||
      await chrome.permissions.contains({{origins: ['*://*/*']}}) ||
      await chrome.permissions.contains({{permissions: ['alarms']}}) ||
      await chrome.permissions.contains({{origins: [optionalOrigin]}}) ||
      !(await chrome.permissions.contains({{
        permissions: ['storage'], origins: [grantedOrigin],
      }})) ||
      await chrome.permissions.contains({{permissions: ['unlimitedStorage']}}) ||
      await chrome.permissions.contains({{origins: [deniedOrigin]}})) {{
    throw Error('Extension permission queries did not reflect the active grants');
  }}
  try {{
    await chrome.permissions.remove({{origins: [grantedOrigin]}});
    throw Error('Removing a required permission unexpectedly succeeded');
  }} catch (error) {{
    if (!/required permissions/i.test(error.message)) throw error;
  }}
  try {{
    await chrome.permissions.request({{permissions: ['alarms'], origins: [optionalOrigin]}});
    throw Error('An optional permission request without a gesture unexpectedly succeeded');
  }} catch (error) {{
    if (error.message !== 'This function must be called during a user gesture') throw error;
  }}
  if (!(await chrome.permissions.remove({{permissions: ['alarms'], origins: [optionalOrigin]}}))) {{
    throw Error('Removing inactive optional permissions must be a no-op');
  }}
  const port = {server.server_port};
  const read = async (host) => {{
    const response = await fetch('http://' + host + ':' + port + '/extension-host-grant');
    if (!response.ok || await response.text() !== 'private fixture response')
      throw Error('Unexpected extension host fixture response');
  }};
  await read('localhost');
  await read('127.0.0.1');
  try {{
    await read('unapproved.example.com');
  }} catch (error) {{
    if (error instanceof TypeError) return;
    throw error;
  }}
  throw Error('Extension read an unapproved host');
}}
""",
                encoding="utf-8",
            )
            if options.extension_module_worker:
                manifest["background"]["type"] = "module"
                worker = package / "worker.js"
                worker.write_text(
                    "import './assets/scripts/caf%C3%A9.js';\n"
                    "if (!globalThis.zitterNestedArchiveReady) throw Error('Module did not run');\n"
                    + worker.read_text(encoding="utf-8"),
                    encoding="utf-8",
                )
            digest = hashlib.sha256(public_key).hexdigest()
            extension_id = digest[:32].translate(
                str.maketrans("0123456789abcdef", "abcdefghijklmnop")
            )
            allowed_receiver = make_external_receiver(
                signer, openssl, package / "allowed-receiver", extension_id, read_tabs=True)
            restricted_receiver = make_external_receiver(
                signer, openssl, package / "restricted-receiver", allowed_receiver[0])
            peer_receiver = make_external_receiver(
                signer, openssl, package / "peer-receiver", extension_id)
            manifest["externally_connectable"] = {
                "ids": [allowed_receiver[0], peer_receiver[0]],
            }
            trust_flag = f"{EXTENSION_KEY_SWITCH}={digest}"
            server.web_resource_extension = extension_id
            source_manifest = package / "manifest.json"
            extra_script = {
                "matches": ["http://127.0.0.1/*", "http://localhost/*"],
                "include_globs": [f"http://127.0.0.1:*{EXTENSION_EXTRA_ALLOWED_PATH}",
                                  "http://localhost:*/extension-content/other-frame"],
                "all_frames": True,
                "match_about_blank": True,
                "js": ["content.js"],
                "run_at": "document_start",
            }
            manifest["content_scripts"].extend((extra_script, {
                "matches": ["http://fallback.localhost/*"],
                "all_frames": True,
                "match_origin_as_fallback": True,
                "js": ["content.js"],
                "run_at": "document_start",
            }))
            manifest["homepage_url"] = "https://example.com/"
            manifest["content_scripts"].append({
                "world": "MAIN",
                "matches": ["http://localhost/extension-content/scripting-target"],
                "js": [EXTENSION_NESTED_SCRIPT],
                "run_at": "document_start",
            })
            manifest["content_scripts"][0]["matches"].extend([
                "http://127.0.0.1/extension-content/live-install",
                "http://127.0.0.1/extension-content/public-resource",
                "http://127.0.0.1/extension-content/tab-message-target",
                "http://127.0.0.1/extension-content/tab-sleep-observer",
                "http://127.0.0.1/extension-content/api-duplicate-history-*",
                "http://127.0.0.1/extension-content/api-tab-deferred",
            ])
            with (package / "worker.js").open("a", encoding="utf-8") as worker:
                worker.write(
                    "chrome.runtime.onInstalled.addListener(({reason, previousVersion}) => {"
                    "  chrome.storage.local.set({zitterInstallReason: reason});"
                    "  if (reason === 'update') chrome.permissions.contains({"
                    "    origins: ['https://extension-update-only.example.invalid/*']"
                    "  }).then(granted => chrome.storage.local.set({"
                    "    zitterUpdateEvent: previousVersion + '->' + chrome.runtime.getManifest().version"
                    "      + (granted ? ':granted' : ':denied')"
                    "  }));"
                    "});\n")
            add_extension_message_fixture(package)
            with (package / "worker.js").open("a", encoding="utf-8") as worker:
                worker.write("""
const createdTabs = [];
const activatedTabs = [];
const updatedTabs = [];
const removedTabs = [];
const movedTabs = [];
chrome.tabs.onCreated.addListener(tab => createdTabs.push(tab));
chrome.tabs.onActivated.addListener(info => activatedTabs.push(info));
chrome.tabs.onUpdated.addListener((tabId, change, tab) => updatedTabs.push({tabId, change, tab}));
chrome.tabs.onMoved.addListener((tabId, info) => movedTabs.push({tabId, ...info}));
chrome.tabs.onRemoved.addListener((tabId, info) => {
  removedTabs.push(tabId);
  chrome.storage.local.set({zitterRemovedTab: {tabId, ...info}});
});
chrome.runtime.onMessage.addListener((message, sender, reply) => {
  if (message?.probe !== 'discarded') return;
  (async () => {
    const tabs = await chrome.tabs.query({discarded: true, status: 'unloaded'});
    const tab = tabs.find(tab => tab.id === message.tab);
    const updates = updatedTabs.filter(update => update.tabId === message.tab);
    if (!tab || tab.active || !tab.discarded || tab.status !== 'unloaded' ||
        !tab.mutedInfo?.muted || tab.mutedInfo.extensionId !== chrome.runtime.id ||
        tab.url !== undefined || tab.title !== undefined || tab.pendingUrl !== undefined ||
        removedTabs.includes(message.tab) || createdTabs.some(tab => tab.id === message.tab) ||
        !updates.some(({change, tab}) => change.discarded && change.status === 'unloaded' &&
                                        tab.discarded && tab.status === 'unloaded')) {
      throw Error('Sleeping Workspace lost the tab or reported incorrect metadata');
    }
    const visible = await chrome.runtime.sendMessage(message.authorized, {
      probe: 'query-tabs', query: {
        discarded: true,
        url: 'http://127.0.0.1/extension-content/live-install',
        title: 'Tab removal woke the installed worker',
      },
    });
    if (visible?.tabs?.length !== 1 || visible.tabs[0].id !== message.tab ||
        !visible.tabs[0].url?.endsWith('/extension-content/live-install') ||
        visible.tabs[0].title !== 'Tab removal woke the installed worker') {
      throw Error('Sleeping tab did not retain its approved address and title');
    }
    try {
      await chrome.tabs.sendMessage(message.tab, {probe: 'target'});
    } catch (error) {
      if (error.message === 'Could not establish connection. Receiving end does not exist.')
        return 'verified';
      throw error;
    }
    throw Error('Sleeping tab unexpectedly had a message recipient');
  })().then(reply, error => reply(String(error)));
  return true;
});
chrome.runtime.onMessage.addListener((message, sender, reply) => {
  if (message?.probe !== 'tabs') return;
  (async () => {
    if (!Number.isInteger(sender.tab?.id) || sender.tab.id === message.tab ||
        sender.tab.incognito || sender.tab.url !== undefined ||
        sender.tab.title !== undefined || sender.tab.pendingUrl !== undefined ||
        !sender.documentId || chrome.tabs.highlight !== undefined) {
      throw Error('Unexpected sender metadata or API');
    }
    const tabs = await chrome.tabs.query({currentWindow: true});
    const target = tabs.find(tab => tab.id === message.tab);
    if (chrome.windows.WINDOW_ID_CURRENT !== -2 || chrome.windows.WINDOW_ID_NONE !== -1)
      throw Error('Unexpected window ID constants');
    const active = await chrome.tabs.query({active: true, windowId: chrome.windows.WINDOW_ID_CURRENT});
    if (!target || tabs.some(tab => tab.id === message.peer || tab.incognito ||
          tab.url !== undefined || tab.title !== undefined || tab.pendingUrl !== undefined) ||
        active.length !== 1 || active[0].id !== sender.tab.id ||
        active[0].windowId !== target.windowId || target.active || target.highlighted ||
        !active[0].highlighted || target.pinned ||
        (await chrome.tabs.query({windowId: target.windowId, index: target.index}))[0]?.id !== message.tab ||
        (await chrome.tabs.query({currentWindow: false})).length !== 0 ||
        (await chrome.tabs.query({url: 'http://127.0.0.1/*'})).length !== 0 ||
        (await chrome.tabs.query({title: 'Extension tab message target ready'})).length !== 0) {
      throw Error('Tab query returned foreign tabs, private metadata, or incorrect selection');
    }
    const creations = createdTabs.filter(tab => tab.id === message.tab);
    if (creations.length !== 1 || !creations[0].active || !creations[0].highlighted ||
        creations[0].index !== target.index || creations[0].windowId !== target.windowId ||
        createdTabs.some(tab => tab.id === message.peer || tab.url !== undefined ||
                               tab.title !== undefined || tab.pendingUrl !== undefined) ||
        !activatedTabs.some(info => info.tabId === message.tab && info.windowId === target.windowId) ||
        activatedTabs.some(info => info.tabId === message.peer)) {
      throw Error('Tab lifecycle delivered incorrect or foreign metadata');
    }
    const hasPrivateFields = data =>
      data.url !== undefined || data.title !== undefined || data.pendingUrl !== undefined;
    const targetUpdates = updatedTabs.filter(update => update.tabId === message.tab);
    if (!targetUpdates.some(update => update.change.status === 'loading') ||
        !targetUpdates.some(update => update.change.status === 'complete') ||
        updatedTabs.some(({tabId, change, tab}) =>
          tabId === message.peer || tabId !== tab.id ||
          hasPrivateFields(change) || hasPrivateFields(tab))) {
      throw Error('Tab updates had missing loading states or exposed foreign metadata');
    }
    const visible = await chrome.runtime.sendMessage(message.authorized, {
      probe: 'query-tabs', query: {
        url: ['http://127.0.0.1/extension-content/tab-message-target'],
        title: 'Extension tab message target*',
        status: 'complete',
      },
    });
    const visibleTab = visible?.tabs?.[0];
    if (visible?.tabs?.length !== 1 || visibleTab?.id !== message.tab ||
        !visibleTab.url?.endsWith('/extension-content/tab-message-target#same-document') ||
        visibleTab.title !== 'Extension tab message target ready') {
      throw Error('The tabs permission did not expose the matching address and title');
    }
    const visibleUpdates = visible.updates.filter(update => update.tabId === message.tab);
    const initialUrl = visibleTab.url.split('#')[0];
    if (!visibleUpdates.some(({change, tab}) => change.url === initialUrl && tab.url === initialUrl) ||
        !visibleUpdates.some(({change, tab}) =>
          change.url === visibleTab.url && tab.url === visibleTab.url && change.status === undefined) ||
        !visibleUpdates.some(({change, tab}) =>
          change.title === visibleTab.title && tab.title === visibleTab.title) ||
        visible.updates.some(update => update.tabId === message.peer)) {
      throw Error('Authorized tab updates omitted address or title or crossed profiles');
    }
    const query = {probe: 'target'};
    const options = {documentId: message.document};
    if (await chrome.tabs.sendMessage(message.tab, query, options) !== 'target-tab') {
      throw Error('Tab message did not reach the selected document');
    }
    for (const [tab, scope] of [[sender.tab.id, options], [message.peer, {}]]) {
      try {
        await chrome.tabs.sendMessage(tab, query, scope);
      } catch (error) {
        if (error.message === 'Could not establish connection. Receiving end does not exist.') continue;
        throw error;
      }
      throw Error('Tab message reached a mismatched document or foreign profile');
    }
    const port = chrome.tabs.connect(message.tab, {...options, name: 'zitter-tab-probe'});
    try {
      const answer = await new Promise((resolve, reject) => {
        port.onMessage.addListener(resolve);
        port.onDisconnect.addListener(() => reject(Error(chrome.runtime.lastError?.message)));
        port.postMessage(query);
      });
      if (answer !== 'target-tab') throw Error('Tab port reached the wrong document');
    } finally {
      port.disconnect();
    }
    const rejects = async (action, expected) => {
      try {
        await action();
      } catch (error) {
        if (error.message.includes(expected)) return;
        throw error;
      }
      throw Error('Tab mutation unexpectedly succeeded');
    };
    const origin = new URL(visibleTab.url).origin;
    await rejects(() => chrome.tabs.get(message.peer), 'No matching tab in this profile');
    await rejects(() => chrome.tabs.update(message.peer, {muted: true}),
                  'No matching tab in this profile');
    await rejects(() => chrome.tabs.create({url: 'chrome://settings'}),
                  'Only HTTP and HTTPS');
    await rejects(() => chrome.tabs.create({
      url: 'chrome-extension://' + message.authorized + '/probe.html',
    }), "calling extension's own");
    const helpUrl = chrome.runtime.getURL('/probe.html#self-help');
    for (const active of [false, true]) {
      const help = await chrome.tabs.create({url: helpUrl, active});
      try {
        let found = false;
        for (let attempt = 0; attempt < 60 && !found; ++attempt) {
          const reply = await chrome.tabs.query({
            title: 'Zitter extension script executed', status: 'complete',
          });
          found = reply.some(tab => tab.id === help.id && tab.url === helpUrl &&
                                    tab.active === active);
          if (!found) await new Promise(resolve => setTimeout(resolve, 50));
        }
        if (!found) throw Error('The extension help page did not load');
      } finally {
        await chrome.tabs.remove(help.id);
        if (active) await chrome.tabs.update(sender.tab.id, {active: true});
      }
    }
    await rejects(() => chrome.tabs.update(message.peer, {active: true}),
                  'No matching tab in this profile');
    const created = await chrome.tabs.create({
      windowId: sender.tab.windowId, active: false,
      url: origin + '/extension-content/api-tab-open',
    });
    if (!Number.isInteger(created.id) || created.active || created.discarded ||
        created.windowId !== sender.tab.windowId || hasPrivateFields(created)) {
      throw Error('Tab creation returned incorrect or unapproved metadata');
    }
    const fetched = await chrome.tabs.get(created.id);
    if (fetched.id !== created.id || fetched.active ||
        fetched.mutedInfo?.muted !== false || fetched.mutedInfo.reason !== undefined ||
        hasPrivateFields(fetched)) {
      throw Error('Tab lookup returned incorrect or unapproved metadata');
    }
    const waitForPage = async (path, title, id = created.id) => {
      for (let attempt = 0; attempt < 60; ++attempt) {
        const reply = await chrome.runtime.sendMessage(message.authorized, {
          probe: 'query-tabs', query: {title, status: 'complete'},
        });
        const tab = reply?.tabs?.find(tab => tab.id === id && tab.url === origin + path);
        if (tab) return tab;
        await new Promise(resolve => setTimeout(resolve, 50));
      }
      throw Error('The extension tab did not load ' + path);
    };
    await waitForPage('/extension-content/api-tab-open', 'Extension tab created');
    const script = tabId => ({
      target: {tabId},
      func: () => [location.hostname, document.body.textContent],
    });
    await rejects(() => chrome.scripting.executeScript(script(message.peer)), 'No tab with id');
    await rejects(() => chrome.scripting.executeScript(script(created.id)), 'Cannot access');
    const scriptURL = new URL('/extension-content/scripting-target', origin);
    scriptURL.hostname = 'localhost';
    const scriptTab = await chrome.tabs.create({url: scriptURL.href, active: false});
    let result;
    for (let attempt = 0; attempt < 60; ++attempt) {
      result = await chrome.scripting.executeScript(script(scriptTab.id)).catch(() => []);
      if (result[0]?.result?.[0] === 'localhost') break;
      await new Promise(resolve => setTimeout(resolve, 50));
    }
    if (result.length !== 1 || result[0].frameId !== 0 ||
        result[0].result[1] !== 'Scripting fixture')
      throw Error('Script did not run in the authorized document');
    const [pageWorld] = await chrome.scripting.executeScript({
      target: {tabId: scriptTab.id}, world: 'MAIN',
      func: () => globalThis.zitterNestedArchiveReady === true && !globalThis.chrome?.storage,
    });
    if (pageWorld?.result !== true) throw Error('Declared MAIN script did not run in the page world');
    const css = {target: {tabId: scriptTab.id}, css: 'body {color: rgb(17, 23, 31) !important;}'};
    const readColor = () => chrome.scripting.executeScript({
      target: css.target, func: () => getComputedStyle(document.body).color,
    }).then(results => results[0].result);
    await chrome.scripting.insertCSS(css);
    if (await readColor() !== 'rgb(17, 23, 31)') throw Error('CSS injection was not applied');
    await chrome.scripting.removeCSS(css);
    if (await readColor() === 'rgb(17, 23, 31)') throw Error('Injected CSS remained after removal');
    await chrome.tabs.remove(scriptTab.id);
    const blank = await chrome.tabs.create({active: false});
    const beforeMoves = (await chrome.tabs.query({})).map(tab => tab.id);
    await rejects(() => chrome.tabs.move([created.id, message.peer], {index: 0}),
                  'No matching tab in this profile');
    if ((await chrome.tabs.query({})).map(tab => tab.id).join() !== beforeMoves.join())
      throw Error('A rejected tab move modified the order');
    const first = await chrome.tabs.move(created.id, {index: 0});
    if (first.id !== created.id || first.index !== 0 || first.active ||
        hasPrivateFields(first) ||
        !movedTabs.some(event => event.tabId === created.id &&
                        event.fromIndex === beforeMoves.indexOf(created.id) &&
                        event.toIndex === 0 && event.windowId === first.windowId)) {
      throw Error('The tab did not move to the front or emit its move event');
    }
    const appended = await chrome.tabs.move([created.id, blank.id], {index: -1});
    const afterAppend = await chrome.tabs.query({});
    if (appended.map(tab => tab.id).join() !== [created.id, blank.id].join() ||
        afterAppend.slice(-2).map(tab => tab.id).join() !== [created.id, blank.id].join() ||
        afterAppend.find(tab => tab.active)?.id !== sender.tab.id ||
        movedTabs.some(event => ![created.id, blank.id].includes(event.tabId))) {
      throw Error('The cross-Workspace move lost the active tab or moved unrelated tabs');
    }
    await waitForPage('/extension-content/api-tab-open', 'Extension tab created');
    const restored = await chrome.tabs.move([created.id, blank.id], {index: 0});
    if (restored[0].id !== created.id || restored[0].index !== 0 ||
        restored[1].id !== blank.id || restored[1].index !== 1) {
      throw Error('The moved tabs did not return in request order');
    }
    await chrome.tabs.update(created.id, {active: true});
    const activeMove = await chrome.tabs.move(created.id, {index: -1});
    if (!activeMove.active || activeMove.id !== created.id ||
        (await chrome.tabs.query({active: true}))[0]?.id !== created.id ||
        !(await chrome.tabs.move(created.id, {index: 0})).active) {
      throw Error('Moving the selected tab across Workspaces changed the active tab');
    }
    await chrome.tabs.update(sender.tab.id, {active: true});
    await chrome.tabs.move([created.id, blank.id], {index: beforeMoves.indexOf(created.id)});
    await rejects(() => chrome.tabs.duplicate(message.peer), 'No matching tab in this profile');
    const historyFirst = '/extension-content/api-duplicate-history-first';
    const historySecond = '/extension-content/api-duplicate-history-second';
    const source = await chrome.tabs.create({active: false, url: origin + historyFirst});
    await waitForPage(historyFirst, 'Duplicate history first', source.id);
    await chrome.tabs.update(source.id, {url: origin + historySecond});
    const original = await waitForPage(historySecond, 'Duplicate history second', source.id);
    const sessionProbe = {probe: 'duplicate-session-storage'};
    await chrome.tabs.sendMessage(source.id, {...sessionProbe, value: 'source'});
    const clone = await chrome.tabs.duplicate(source.id);
    if (clone.id === source.id || clone.index !== original.index + 1 ||
        !clone.active || hasPrivateFields(clone) ||
        createdTabs.filter(tab => tab.id === clone.id).length !== 1) {
      throw Error('Tab duplication returned incorrect metadata or lifecycle');
    }
    await waitForPage(historySecond, 'Duplicate history second', clone.id);
    if (await chrome.tabs.sendMessage(clone.id, sessionProbe) !== 'source')
      throw Error('The duplicate did not inherit the session storage snapshot');
    await chrome.tabs.sendMessage(clone.id, {...sessionProbe, value: 'clone'});
    if (await chrome.tabs.sendMessage(source.id, sessionProbe) !== 'source')
      throw Error('Changing the duplicate also changed the source session storage');
    await rejects(() => chrome.tabs.goBack(message.peer), 'No matching tab in this profile');
    await rejects(() => chrome.tabs.goForward(message.peer), 'No matching tab in this profile');
    await chrome.tabs.goBack();
    await waitForPage(historyFirst, 'Duplicate history first', clone.id);
    await waitForPage(historySecond, 'Duplicate history second', source.id);
    await chrome.tabs.goForward(clone.id);
    await waitForPage(historySecond, 'Duplicate history second', clone.id);
    await rejects(() => chrome.tabs.goForward(clone.id), 'No usable forward entry');
    await rejects(() => chrome.tabs.discard(message.peer), 'No matching tab in this profile');
    await rejects(() => chrome.tabs.discard(clone.id), 'No eligible Workspace tab');
    const discarded = await chrome.tabs.discard(source.id);
    if (discarded.id !== source.id || !discarded.discarded || discarded.status !== 'unloaded' ||
        hasPrivateFields(discarded) ||
        !updatedTabs.some(({tabId, change}) =>
          tabId === source.id && change.discarded === true && change.status === 'unloaded')) {
      throw Error('Discard did not keep the original ID and report the unloaded state');
    }
    await rejects(() => chrome.tabs.discard(source.id), 'No eligible Workspace tab');
    await rejects(() => chrome.tabs.sendMessage(source.id, sessionProbe),
                  'Receiving end does not exist');
    await chrome.tabs.update(source.id, {active: true});
    const restoredSource = await waitForPage(historySecond, 'Duplicate history second restored', source.id);
    if (restoredSource.discarded ||
        !updatedTabs.some(({tabId, change}) => tabId === source.id && change.discarded === false) ||
        await chrome.tabs.sendMessage(source.id, sessionProbe) !== 'source') {
      throw Error('Restoring the discarded tab lost its session storage');
    }
    await chrome.tabs.goBack(source.id);
    await waitForPage(historyFirst, 'Duplicate history first', source.id);
    await chrome.tabs.update(clone.id, {active: true});
    await chrome.tabs.discard(source.id);
    await chrome.tabs.reload(source.id, {bypassCache: true});
    const background = await waitForPage(historyFirst, 'Duplicate history first restored', source.id);
    if (background.discarded || background.active ||
        (await chrome.tabs.query({active: true}))[0]?.id !== clone.id) {
      throw Error('Explicit reload of the discarded tab changed the selection');
    }
    await chrome.tabs.update(sender.tab.id, {active: true});
    await chrome.tabs.remove([source.id, clone.id]);

    const deferredClonePath = '/extension-content/api-duplicate-deferred';
    const stored = await chrome.runtime.sendMessage(message.authorized, {
      probe: 'query-tabs', query: {discarded: true, url: 'http://127.0.0.1' + deferredClonePath},
    });
    if (stored?.tabs?.length !== 1)
      throw Error('The deferred duplication fixture did not start unloaded');
    const storedId = stored.tabs[0].id;
    await rejects(() => chrome.tabs.goBack(storedId), 'No usable back entry');
    const deferredClone = await chrome.tabs.duplicate(storedId);
    if (deferredClone.id === storedId || !deferredClone.active ||
        deferredClone.index !== stored.tabs[0].index + 1 ||
        !(await chrome.tabs.query({discarded: true})).some(tab => tab.id === storedId)) {
      throw Error('Duplicating a deferred tab loaded the source or changed its ID');
    }
    await waitForPage(deferredClonePath, 'Deferred duplicate loaded', deferredClone.id);
    await chrome.tabs.update(sender.tab.id, {active: true});
    await chrome.tabs.remove(deferredClone.id);
    await rejects(() => chrome.tabs.reload(message.peer), 'No matching tab in this profile');
    await chrome.tabs.reload(created.id);
    await waitForPage('/extension-content/api-tab-open', 'Extension tab revalidated');
    await chrome.tabs.update(created.id, {active: true});
    await chrome.tabs.reload({bypassCache: true});
    await waitForPage('/extension-content/api-tab-open', 'Extension tab bypassed cache');
    await chrome.tabs.update(sender.tab.id, {active: true});
    const deferred = await chrome.runtime.sendMessage(message.authorized, {
      probe: 'query-tabs', query: {
        discarded: true, url: 'http://127.0.0.1/extension-content/api-tab-deferred',
      },
    });
    if (deferred?.tabs?.length !== 1)
      throw Error('The deferred reload fixture did not start unloaded');
    const deferredId = deferred.tabs[0].id;
    const muted = await chrome.tabs.update(deferredId, {muted: true});
    const repeatedMute = await chrome.tabs.update(deferredId, {muted: true});
    if (!muted.discarded || !repeatedMute.discarded || !muted.mutedInfo?.muted ||
        muted.mutedInfo.reason !== 'extension' || muted.mutedInfo.extensionId !== chrome.runtime.id ||
        !(await chrome.tabs.query({muted: true, discarded: true})).some(tab => tab.id === deferredId)) {
      throw Error('Muting a deferred tab loaded the tab or lost its mute metadata');
    }
    await chrome.tabs.reload(deferredId, {bypassCache: true});
    const loaded = await waitForPage('/extension-content/api-tab-deferred',
                                     'Extension tab bypassed cache', deferredId);
    if (loaded.discarded || loaded.active || (await chrome.tabs.query({active: true}))[0]?.id !== sender.tab.id)
      throw Error('Reloading a deferred tab changed the current selection');
    await chrome.tabs.update(deferredId, {active: true});
    await waitForPage('/extension-content/api-tab-deferred', 'Extension audio muted', deferredId);
    if (await chrome.tabs.sendMessage(deferredId, {probe: 'native-unmute'}) !== 'requested')
      throw Error('Could not start the native unmute trial');
    await waitForPage('/extension-content/api-tab-deferred', 'Extension audio unmuted', deferredId);
    const unmuted = await chrome.tabs.get(deferredId);
    const muteUpdates = updatedTabs.filter(update => update.tabId === deferredId && update.change.mutedInfo);
    if (unmuted.id !== deferredId || unmuted.mutedInfo?.muted !== false ||
        muteUpdates.map(update => update.change.mutedInfo.muted).join() !== 'true,false' ||
        muteUpdates[0]?.change.mutedInfo.extensionId !== chrome.runtime.id ||
        muteUpdates[1]?.change.mutedInfo.reason !== 'user' ||
        unmuted.mutedInfo.reason !== 'user' || unmuted.mutedInfo.extensionId !== undefined ||
        muteUpdates.some(({change, tab}) =>
          change.mutedInfo.muted !== tab.mutedInfo.muted || hasPrivateFields(tab))) {
      throw Error('Mute updates did not report the two effective state changes');
    }
    await chrome.tabs.update(sender.tab.id, {active: true});
    const audioPath = '/extension-content/api-tab-audio-state';
    const audio = await chrome.tabs.create({url: origin + audioPath});
    await waitForPage(audioPath, 'Audio fixture badge visible', audio.id);
    if (!(await chrome.tabs.get(audio.id)).audible ||
        !(await chrome.tabs.query({audible: true})).some(tab => tab.id === audio.id) ||
        !updatedTabs.some(update => update.tabId === audio.id && update.change.audible === true)) {
      throw Error('Web Audio did not produce an audible tab and event');
    }
    await chrome.tabs.update(sender.tab.id, {active: true});
    await rejects(() => chrome.tabs.discard(audio.id), 'No eligible Workspace tab');
    await chrome.tabs.update(audio.id, {active: true});
    await chrome.tabs.update(audio.id, {muted: true});
    await waitForPage(audioPath, 'Audio fixture badge hidden', audio.id);
    for (let attempt = 0; attempt < 60 && (await chrome.tabs.get(audio.id)).audible; ++attempt)
      await new Promise(resolve => setTimeout(resolve, 50));
    if ((await chrome.tabs.get(audio.id)).audible ||
        !updatedTabs.some(update => update.tabId === audio.id && update.change.audible === false)) {
      throw Error('Stopping Web Audio did not clear the audible tab and event');
    }
    await chrome.tabs.update(sender.tab.id, {active: true});
    await chrome.tabs.remove(audio.id);
    const observer = await chrome.runtime.sendMessage(message.authorized, {
      probe: 'query-tabs', query: {url: 'http://127.0.0.1/extension-content/tab-sleep-observer'},
    });
    if (observer?.tabs?.length !== 1)
      throw Error('The discard observer tab is missing');
    await chrome.tabs.update(observer.tabs[0].id, {active: true});
    await chrome.tabs.update(sender.tab.id, {active: true});
    const posted = await chrome.tabs.create({
      active: false, url: origin + '/extension-content/api-tab-post-form',
    });
    await waitForPage('/extension-content/api-tab-posted', 'Extension tab posted', posted.id);
    await rejects(() => chrome.tabs.discard(posted.id), 'No eligible Workspace tab');
    await rejects(() => chrome.tabs.duplicate(posted.id), 'Could not duplicate the Workspace tab');
    await rejects(() => chrome.tabs.reload(posted.id), 'Could not reload the Workspace tab');
    await chrome.tabs.update(posted.id, {active: true});
    await chrome.tabs.reload(posted.id);
    await waitForPage('/extension-content/api-tab-posted', 'Extension repost cancelled', posted.id);
    await chrome.tabs.reload({bypassCache: true});
    await waitForPage('/extension-content/api-tab-posted', 'Extension tab reposted', posted.id);
    const checkPostHistory = async id => {
      await chrome.tabs.update(id, {url: origin + historyFirst});
      await waitForPage(historyFirst, 'Duplicate history first', id);
      await chrome.tabs.update(id, {url: origin + historySecond});
      await waitForPage(historySecond, 'Duplicate history second', id);
      await chrome.tabs.goBack(id);
      await waitForPage(historyFirst, 'Duplicate history first', id);
      await rejects(() => chrome.tabs.goBack(id), 'No usable back entry');
      await waitForPage(historyFirst, 'Duplicate history first', id);
    };
    await checkPostHistory(posted.id);
    await chrome.tabs.update(sender.tab.id, {active: true});
    await chrome.tabs.remove(posted.id);
    const subframePath = '/extension-content/api-tab-subframe-form';
    const subframe = await chrome.tabs.create({active: false, url: origin + subframePath});
    await waitForPage(subframePath, 'Extension subframe posted', subframe.id);
    await rejects(() => chrome.tabs.discard(subframe.id), 'No eligible Workspace tab');
    await rejects(() => chrome.tabs.duplicate(subframe.id), 'Could not duplicate the Workspace tab');
    await checkPostHistory(subframe.id);
    await chrome.tabs.remove(subframe.id);
    await rejects(() => chrome.tabs.update(created.id, {url: 'javascript:alert(1)'}),
                  'Only HTTP and HTTPS');
    const changed = await chrome.tabs.update(created.id, {
      active: true, url: origin + '/extension-content/api-tab-update',
    });
    if (!changed.active || changed.id !== created.id || hasPrivateFields(changed) ||
        (await chrome.tabs.update({active: false})).id !== created.id) {
      throw Error('Tab update did not keep the same ID and selection');
    }
    await waitForPage('/extension-content/api-tab-update', 'Extension tab close ready');
    await chrome.tabs.update(sender.tab.id, {active: true});
    await rejects(() => chrome.tabs.discard(created.id), 'No eligible Workspace tab');
    await chrome.tabs.update(created.id, {active: true});
    await rejects(() => chrome.tabs.remove([created.id, message.peer]),
                  'No matching tab in this profile');
    if (!(await chrome.tabs.query({})).some(tab => tab.id === created.id))
      throw Error('A rejected batch already closed its same-profile tab');
    await rejects(() => chrome.tabs.remove([created.id, blank.id]),
                  'Workspace tab closure was cancelled');
    const remaining = await chrome.tabs.query({});
    if (!remaining.some(tab => tab.id === created.id) ||
        !remaining.some(tab => tab.id === blank.id)) {
      throw Error('Declining beforeunload already removed a tab');
    }
    await chrome.tabs.remove([created.id, blank.id, created.id]);
    await chrome.tabs.update(sender.tab.id, {active: true});
    if ((await chrome.tabs.query({})).some(tab => tab.id === created.id || tab.id === blank.id) ||
        !removedTabs.includes(created.id) || !removedTabs.includes(blank.id)) {
      throw Error('Tab removal did not close and report both requested tabs');
    }
    const automatic = await chrome.tabs.discard();
    if (!automatic.discarded || automatic.active || automatic.id === message.peer ||
        (await chrome.tabs.query({active: true}))[0]?.id !== sender.tab.id) {
      throw Error('Automatic discard left the calling profile or changed the selection');
    }
    await chrome.tabs.update(sender.tab.id, {muted: true});
    return 'verified';
  })().then(reply, error => reply(String(error)));
  return true;
});
""")
            with (package / "content.js").open("a", encoding="utf-8") as script:
                script.write("const zitterExternalReceivers = " + json.dumps([
                    allowed_receiver[0], restricted_receiver[0], peer_receiver[0],
                ]) + ";\n")
                script.write("""
if (location.pathname === '/extension-content/api-tab-deferred') {
  chrome.runtime.onMessage.addListener((message, sender, reply) => {
    if (message?.probe !== 'native-unmute') return;
    document.title = 'Extension native unmute requested';
    reply('requested');
  });
}
if (location.pathname.startsWith('/extension-content/api-duplicate-history-')) {
  chrome.runtime.onMessage.addListener((message, sender, reply) => {
    if (message?.probe === 'duplicate-session-storage') {
      if (message.value !== undefined) sessionStorage.setItem('duplicate-probe', message.value);
      reply(sessionStorage.getItem('duplicate-probe'));
    }
  });
}
if (location.pathname === '/extension-content/tab-sleep-observer') {
  document.addEventListener('zitter-test-tab-discard', event => {
    chrome.runtime.sendMessage({probe: 'discarded', tab: event.detail,
                                authorized: zitterExternalReceivers[0]}).then(
      reply => document.documentElement.dataset.zitterTabDiscard = reply,
      error => document.documentElement.dataset.zitterTabDiscard = String(error));
  }, {once: true});
}
if (location.pathname === '/extension-content/tab-message-target') {
  chrome.runtime.onMessage.addListener((message, sender, reply) => {
    if (message?.probe === 'target') reply('target-tab');
  });
  chrome.runtime.onConnect.addListener(port => {
    if (port.name === 'zitter-tab-probe') {
      port.onMessage.addListener(message => {
        if (message?.probe === 'target') port.postMessage('target-tab');
      });
    }
  });
}
if (location.pathname === '/extension-content/live-install') {
  for (const [action, approve, persist] of [
    ['cancel', false, false], ['approve', true, false], ['persist', true, true],
  ]) {
    document.addEventListener('zitter-test-optional-' + action, () => {
      chrome.runtime.sendMessage({probe: 'optional-permission', approve, persist}).then(
        reply => document.title = reply,
        error => document.title = String(error));
    }, {once: true});
  }
  document.addEventListener('zitter-test-tab-removed', event => {
    const {tabId, windowId} = JSON.parse(event.detail);
    chrome.storage.onChanged.addListener((changes, area) => {
      const info = changes.zitterRemovedTab?.newValue;
      if (area === 'local' && info?.tabId === tabId && info.windowId === windowId &&
          info.isWindowClosing === false) {
        document.documentElement.dataset.zitterTabRemoved = 'verified';
      }
    });
  }, {once: true});
  document.addEventListener('zitter-test-tab-message', event => {
    if (chrome.tabs?.sendMessage !== undefined || chrome.tabs?.connect !== undefined ||
        chrome.tabs?.query !== undefined || chrome.tabs?.onCreated !== undefined ||
        chrome.tabs?.onUpdated !== undefined || chrome.tabs?.create !== undefined ||
        chrome.tabs?.update !== undefined || chrome.tabs?.remove !== undefined ||
        chrome.tabs?.move !== undefined || chrome.tabs?.onMoved !== undefined ||
        chrome.tabs?.reload !== undefined || chrome.tabs?.duplicate !== undefined ||
        chrome.tabs?.goBack !== undefined || chrome.tabs?.goForward !== undefined ||
        chrome.tabs?.get !== undefined || chrome.tabs?.discard !== undefined) {
      document.documentElement.dataset.zitterTabMessage = 'Tab API leaked into a content script';
      return;
    }
    chrome.runtime.sendMessage({probe: 'tabs', authorized: zitterExternalReceivers[0],
                                ...JSON.parse(event.detail)}).then(
      reply => document.documentElement.dataset.zitterTabMessage = reply,
      error => document.documentElement.dataset.zitterTabMessage = String(error));
  }, {once: true});
  (async () => {
    const report = result => document.documentElement.dataset.zitterFrameStorage = result;
    try {
      const changed = new Promise(resolve => {
        const listener = (changes, area) => {
          if (area !== 'local' || changes.zitterFrameStorage?.newValue !== 'frame-value') return;
          chrome.storage.onChanged.removeListener(listener);
          resolve();
        };
        chrome.storage.onChanged.addListener(listener);
      });
      await chrome.storage.local.set({zitterFrameStorage: 'frame-value'});
      const {zitterFrameStorage} = await chrome.storage.local.get('zitterFrameStorage');
      if (zitterFrameStorage !== 'frame-value') {
        report('Wrong storage value');
        return;
      }
      await changed;
      const [allowed, restricted, peer] = zitterExternalReceivers;
      const senderTab = await chrome.runtime.sendMessage(allowed, {probe: 'sender-tab'});
      if (!Number.isInteger(senderTab?.id) || senderTab.url !== location.href)
        throw Error('Authorized recipient did not receive the sender tab URL');
      const external = {probe: 'external'};
      const response = await chrome.runtime.sendMessage(allowed, external);
      if (response?.receiver !== allowed || response?.sender !== chrome.runtime.id) {
        report('The allowed receiver did not return its own ID and the sender ID');
        return;
      }
      for (const target of [restricted, peer]) {
        try {
          await chrome.runtime.sendMessage(target, external);
          report('A restricted or cross-profile receiver was reachable');
          return;
        } catch (error) {
          if (error.message !== 'Could not establish connection. Receiving end does not exist.') {
            report('Unexpected rejection from the external receiver: ' + String(error));
            return;
          }
        }
      }
      const version = chrome.runtime.getManifest().version;
      if (await chrome.runtime.sendMessage({probe: 'version'}) !== version) {
        report('Message returned the wrong worker version');
        return;
      }
      const port = chrome.runtime.connect({name: 'zitter-probe'});
      const reply = await new Promise((resolve, reject) => {
        port.onMessage.addListener(resolve);
        port.onDisconnect.addListener(() => reject(chrome.runtime.lastError?.message));
        port.postMessage({probe: 'version'});
      });
      port.disconnect();
      document.addEventListener('zitter-test-wake-worker', () => {
        chrome.runtime.sendMessage({probe: 'version'}).then(
          result => document.documentElement.dataset.zitterWorkerWake =
            result === version ? 'verified' : 'Wake message returned the wrong version',
          error => document.documentElement.dataset.zitterWorkerWake = String(error));
      }, {once: true});
      report(reply === version ? 'verified' : 'Port returned the wrong worker version');
    } catch (error) {
      report(String(error));
    }
  })();
}
""")
            del manifest["incognito"]
            source_manifest.write_text(json.dumps(manifest), encoding="utf-8")
            manifest_bytes = source_manifest.read_bytes()
            source_archive = sign_archive_size_fixture(
                signer, package, private_key, "fixture.crx",
                files=EXTENSION_PACKAGE_FILES, stored_sizes=(14, 15),
                deflated_sizes=(16, 16, 16, 15), empty_files=1050,
            )
            archive_bytes = source_archive.read_bytes()
            messages_path = "_locales/en/messages.json"
            messages_file = package / messages_path
            messages_file.parent.mkdir(parents=True)
            messages_file.write_text(json.dumps({
                "extensionName": {"message": "Localized Zitter fixture"},
            }), encoding="utf-8")
            localized_manifest = dict(manifest, name="__MSG_extensionName__",
                                      default_locale="en")
            source_manifest.write_text(json.dumps(localized_manifest), encoding="utf-8")
            localized_archive = sign_fixture(
                signer, package, private_key, "localized.crx",
                files=(*EXTENSION_PACKAGE_FILES, messages_path),
            )
            source_manifest.write_bytes(manifest_bytes)
            worker_path = package / "worker.js"
            worker_bytes = worker_path.read_bytes()
            if options.extension_module_worker:
                worker_path.write_bytes(b"import './missing-module.js';\n" + worker_bytes)
                broken_worker_archive = sign_fixture(signer, package, private_key, "broken-worker.crx")
                worker_path.write_bytes(worker_bytes)
            corrupt_archive = package / "corrupt.crx"
            corrupt_archive.write_bytes(archive_bytes[:-1] + bytes([archive_bytes[-1] ^ 1]))
            unsafe_archive = sign_fixture(
                signer, package, private_key, "unsafe.crx", "../unexpected.js"
            )
            collision_archives = [
                sign_fixture(signer, package, private_key, name, entry)
                for name, entry in (
                    ("case-collision.crx", "assets/SCRIPTS/other.js"),
                    ("unicode-collision.crx", "assets/scripts/cafe\u0301.js"),
                    ("file-directory-collision.crx", "assets/scripts"),
                    ("reserved-device.crx", "assets/CON.txt"),
                )
            ]
            nested_script_bytes = (package / EXTENSION_NESTED_SCRIPT).read_bytes()
            worker_path.write_bytes(worker_bytes + b"\n// different signed bytes\n")
            replaced_archive = sign_fixture(signer, package, private_key, "replaced.crx")
            oversized_archives = [
                sign_archive_size_fixture(signer, package, private_key, name,
                                          files=("manifest.json",), **limits)
                for name, limits in (
                    ("archive-limit.crx", {"stored_sizes": (16, 16)}),
                    ("file-limit.crx", {"deflated_sizes": (17,)}),
                    ("expanded-limit.crx", {"deflated_sizes": (16,) * 6 + (1,)}),
                    ("entry-limit.crx", {"empty_files": 2048}),
                )
            ]
            keyless_manifest = {key: value for key, value in manifest.items() if key != "key"}
            source_manifest.write_text(json.dumps(keyless_manifest), encoding="utf-8")
            keyless_archive = sign_fixture(signer, package, private_key, "keyless.crx")
            broad_manifest = dict(
                manifest, host_permissions=["<all_urls>"], optional_host_permissions=[],
                content_scripts=[dict(extra_script, world="MAIN", matches=["<all_urls>"])],
            )
            source_manifest.write_text(json.dumps(broad_manifest), encoding="utf-8")
            broad_archive = sign_fixture(signer, package, private_key, "all-web-sites.crx")
            separate_schemes = ["http://*/*", "https://*/*"]
            separate_manifest = dict(
                broad_manifest, host_permissions=separate_schemes,
                content_scripts=[dict(extra_script, matches=[*separate_schemes, "file:///*"])],
            )
            source_manifest.write_text(json.dumps(separate_manifest), encoding="utf-8")
            separate_archive = sign_fixture(signer, package, private_key, "separate-web-schemes.crx")
            rejected_metadata = []
            for name, override, error in (
                ("future-engine", {"minimum_chrome_version": "9999.0.0.0"},
                 "invalid or unsupported minimum_chrome_version"),
                ("malformed-engine", {"minimum_chrome_version": "future"},
                 "invalid or unsupported minimum_chrome_version"),
                ("invalid-incognito", {"incognito": "invalid"}, "Extension manifest rejected"),
                ("invalid-managed-schema", {"storage": {"managed_schema": "../schema.json"}},
                 "storage.managed_schema must name a packaged schema file"),
                ("unsafe-extension-csp", {"content_security_policy": {
                    "extension_pages": "script-src 'self' 'unsafe-eval'; object-src 'none';",
                }}, "Extension manifest rejected"),
            ):
                source_manifest.write_text(json.dumps(dict(manifest, **override)), encoding="utf-8")
                rejected_metadata.append((sign_fixture(
                    signer, package, private_key, name + ".crx"), error))
            unsupported_archives = []
            for name, unsupported in (
                ("optional-api", dict(manifest, optional_permissions=["history"])),
                ("unknown-required-api", dict(
                    manifest, permissions=[*manifest["permissions"], "unknownZitterFixtureApi"],
                )),
                ("unknown-optional-api", dict(
                    manifest, optional_permissions=["unknownZitterFixtureApi"],
                )),
                ("optional-host", dict(manifest, optional_host_permissions=["https://*.com/*"])),
                ("ftp-hosts", dict(manifest, host_permissions=["ftp://example.com/*"])),
                ("all-sites-static-resource", dict(manifest, web_accessible_resources=[{
                    **manifest["web_accessible_resources"][0], "matches": ["<all_urls>"],
                }])),
                ("host-subdomains", dict(manifest, host_permissions=["https://*.google.com/*"])),
                ("host-store", dict(manifest, host_permissions=["https://chromewebstore.google.com/*"])),
                ("script-store-http", dict(manifest, content_scripts=[
                    dict(extra_script, matches=["http://chromewebstore.google.com/*"]),
                ])),
                ("main-world-ftp", dict(manifest, content_scripts=[
                    dict(extra_script, world="MAIN", matches=["ftp://example.com/*"]),
                ])),
                ("frame-fallback", dict(manifest, content_scripts=[dict(
                    extra_script, matches=["http://*.localhost/*"],
                    match_origin_as_fallback=True,
                )])),
                ("web-messaging", dict(manifest, externally_connectable={
                    "matches": ["http://127.0.0.1/*"], "ids": [allowed_receiver[0]],
                })),
            ):
                source_manifest.write_text(json.dumps(unsupported), encoding="utf-8")
                unsupported_archives.append(sign_fixture(signer, package, private_key, name + ".crx"))
            update_manifest = dict(
                manifest, version="2.0", externally_connectable={"ids": [allowed_receiver[0]]},
            )
            source_manifest.write_text(json.dumps(update_manifest), encoding="utf-8")
            update_manifest_bytes = source_manifest.read_bytes()
            update_archive = sign_fixture(signer, package, private_key, "update.crx")
            update_archive_bytes = update_archive.read_bytes()
            changed_policy = dict(update_manifest, content_security_policy={
                "extension_pages": extension_csp + " img-src 'self';",
            })
            source_manifest.write_text(json.dumps(changed_policy), encoding="utf-8")
            changed_policy_archive = sign_fixture(signer, package, private_key, "changed-policy.crx")
            changed_commands = dict(update_manifest, commands={
                **manifest["commands"],
                "fixture-command": {
                    **manifest["commands"]["fixture-command"],
                    "suggested_key": {"default": "Alt+Shift+X"},
                },
            })
            source_manifest.write_text(json.dumps(changed_commands), encoding="utf-8")
            changed_commands_archive = sign_fixture(
                signer, package, private_key, "changed-commands.crx")
            corrupt_update = package / "corrupt-update.crx"
            corrupt_update.write_bytes(
                update_archive_bytes[:-1] + bytes([update_archive_bytes[-1] ^ 1])
            )
            resource_escalation = dict(update_manifest, web_accessible_resources=[{
                "resources": ["assets/data.bin", "probe.js"],
                "matches": ["http://127.0.0.1/*"],
            }])
            source_manifest.write_text(json.dumps(resource_escalation), encoding="utf-8")
            resource_escalation_archive = sign_fixture(
                signer, package, private_key, "resource-escalation.crx")
            world_escalation = dict(update_manifest, content_scripts=[
                dict(manifest["content_scripts"][0], world="MAIN"),
                *manifest["content_scripts"][1:],
            ])
            source_manifest.write_text(json.dumps(world_escalation), encoding="utf-8")
            world_escalation_archive = sign_fixture(
                signer, package, private_key, "world-escalation.crx")
            escalated_manifest = dict(
                update_manifest, permissions=[*update_manifest["permissions"], "unlimitedStorage"])
            source_manifest.write_text(json.dumps(escalated_manifest), encoding="utf-8")
            escalated_archive = sign_fixture(signer, package, private_key, "escalated.crx")
            tabs_escalation = dict(
                update_manifest, permissions=[*update_manifest["permissions"], "tabs"])
            source_manifest.write_text(json.dumps(tabs_escalation), encoding="utf-8")
            tabs_escalation_archive = sign_fixture(signer, package, private_key, "tabs-escalation.crx")
            host_escalation = dict(
                update_manifest, host_permissions=["<all_urls>"], optional_host_permissions=[],
            )
            source_manifest.write_text(json.dumps(host_escalation), encoding="utf-8")
            host_escalation_archive = sign_fixture(signer, package, private_key, "host-escalation.crx")
            reviewed_update_manifest = dict(update_manifest, host_permissions=[
                *manifest["host_permissions"], "https://extension-update-only.example.invalid/*",
            ])
            source_manifest.write_text(json.dumps(reviewed_update_manifest), encoding="utf-8")
            reviewed_update_archive = sign_fixture(signer, package, private_key, "reviewed-update.crx")
            unrestricted_script = {key: value for key, value in extra_script.items()
                                   if key != "include_globs"}
            broader = dict(update_manifest, content_scripts=[
                manifest["content_scripts"][0], unrestricted_script,
            ])
            source_manifest.write_text(json.dumps(broader), encoding="utf-8")
            broader_archive = sign_fixture(signer, package, private_key, "broader-update.crx")
            frame_escalation = dict(
                manifest, version="2.0",
                content_scripts=[dict(manifest["content_scripts"][0], all_frames=True),
                                 *manifest["content_scripts"][1:]],
            )
            source_manifest.write_text(json.dumps(frame_escalation), encoding="utf-8")
            frame_escalation_archive = sign_fixture(signer, package, private_key, "frame-update.crx")
            about_escalation = dict(
                manifest, version="2.0",
                content_scripts=[dict(manifest["content_scripts"][0], match_about_blank=True),
                                 *manifest["content_scripts"][1:]],
            )
            source_manifest.write_text(json.dumps(about_escalation), encoding="utf-8")
            about_escalation_archive = sign_fixture(signer, package, private_key, "about-update.crx")
            origin_escalation = dict(
                manifest, version="2.0",
                content_scripts=[manifest["content_scripts"][0],
                                 dict(extra_script, match_origin_as_fallback=True),
                                 manifest["content_scripts"][2]],
            )
            source_manifest.write_text(json.dumps(origin_escalation), encoding="utf-8")
            origin_escalation_archive = sign_fixture(signer, package, private_key, "origin-update.crx")
            broader_messaging_archives = []
            for name, ids in (
                ("extra-sender", [allowed_receiver[0], restricted_receiver[0]]),
                ("any-sender", ["*"]),
                ("default-senders", None),
            ):
                broadened = dict(update_manifest)
                if ids is None:
                    del broadened["externally_connectable"]
                else:
                    broadened["externally_connectable"] = {"ids": ids}
                source_manifest.write_text(json.dumps(broadened), encoding="utf-8")
                broader_messaging_archives.append(sign_fixture(
                    signer, package, private_key, f"{name}.crx"))
            private_key.unlink()
            for name in EXTENSION_PACKAGE_FILES:
                (package / name).unlink()
            install_flag = f"{EXTENSION_REGISTRY_SWITCH}={source_archive}"
            install_root = root / f"workspace-{extension_profile}" / "Extensions"
            other_install_root = root / f"workspace-{other_profile}" / "Extensions"
            installed_id = install_root / extension_id
            active_version_file = installed_id / "active-version"
            previous_version_dir = installed_id / "1.0_0"
            candidate_version_dir = installed_id / "2.0_0"
        origin = f"http://127.0.0.1:{server.server_port}"
        if not windows:
            key = root / KEY_FILE
            key.write_bytes(secrets.token_bytes(32))
            key.chmod(0o600)
        session_file = root / SESSION_FILE

        with hold_root_lock(root):
            require_rejection(launch(executable, root, origin), "Could not lock")
        invalid = seed_session(origin)
        invalid["work"]["profile_id"] = "personal"
        invalid_bytes = json.dumps(invalid).encode()
        session_file.write_bytes(invalid_bytes)
        require_rejection(launch(executable, root, origin), "Invalid Content workspace session")
        if session_file.read_bytes() != invalid_bytes or any(root.glob("workspace-*")):
            raise SystemExit("An invalid session changed the fixture before startup")

        expected_session = seed_session(origin)
        if options.popup_policy or options.peek or options.shift_peek:
            expected_session["link_rules"] = {"127.0.0.1": "personal"}
        if options.split_actions:
            split_pin = {"id": "split-pin", "title": "Split pin",
                         "url": origin + "/split-actions/right", "parent_id": "",
                         "folder": False, "profile_pin": False}
            expected_session["work"]["pins"] = [split_pin]
        if options.shared_favorites:
            favorite = {"id": "favorite-work", "title": "Work favorite",
                        "url": origin + "/work/active", "parent_id": "",
                        "folder": False, "profile_pin": True}
            local_pin = {**favorite, "id": "local-work", "profile_pin": False}
            expected_session["work"]["pins"] = [favorite, local_pin]
            expected_session["work"]["tabs"][1]["pin"] = "favorite-work"
        session_file.write_text(json.dumps(expected_session), encoding="utf-8")
        expected_session["work"]["tabs"][1]["title"] = "Zitter workspace probe work"
        expected_session["personal"]["tabs"][0]["title"] = "Zitter workspace probe personal"
        server.requests = []
        server.navigation_probe = options.navigation
        server.split_actions = options.split_actions
        server_thread = threading.Thread(target=server.serve_forever, daemon=True)
        server_thread.start()
        try:
            if options.assistant_settings:
                server.assistant_settings_probe = True
                source = Path(__file__).resolve().parents[1] / "tests/fixtures/content_assistant_provider.c"
                cli_fixture = root / ("assistant-provider-fixture.exe" if windows else "assistant-provider-fixture")
                compiler = ["clang"] if windows else ["xcrun", "--sdk", "macosx", "clang"]
                subprocess.run([*compiler, source, "-o", cli_fixture], check=True, timeout=30)
                fixture_codex_home = root / "codex-credentials"
                fixture_codex_home.mkdir()
                env = dict(os.environ, ZITTER_OLLAMA_URL=origin, CODEX_HOME=str(fixture_codex_home))
                for name in ("OPENAI_API_KEY", "ZITTER_OPENAI_API_KEY",
                             "ZITTER_CODEX_PATH", "ZITTER_CLAUDE_PATH"):
                    env.pop(name, None)
                for phase, provider_requests in (("configure", ["discover", "check"]), ("reopen", [])):
                    server.reopening = phase == "reopen"
                    server.assistant_requests = []
                    result = launch(executable, root, origin, [
                        *extra_flags, f"--zitter-content-assistant-settings-probe={phase}",
                    ], env=env)
                    if (result.returncode or MARKER not in result.stdout
                            or "Zitter Content Assistant settings probe passed" not in result.stdout
                            or server.assistant_requests != provider_requests):
                        raise SystemExit(f"Assistant settings {phase} failed.\n{result.stdout}\n{result.stderr}")
                    snapshot = session_file.read_bytes()
                    secret = "zitter-content-openai-fixture"
                    if secret.encode() in snapshot or secret in result.stdout or secret in result.stderr:
                        raise SystemExit("Assistant exposed a plaintext credential")
                    session = json.loads(snapshot)
                    saved = session["assistant"]
                    if (not saved["enabled"] or not saved["permissions"]["tabs"]
                            or not saved["permissions"]["pages"] or not saved["permissions"]["actions"]
                            or not saved["permissions"]["history"]
                            or not saved["permissions"]["confirm_clicks"] or saved["permissions"]["web"]
                            or saved["default_model"] != "ollama/manual-fixture"
                            or {model["id"] for model in saved["models"] if model["provider"] == "ollama"}
                               != {"manual-fixture", "discovered-fixture"}):
                        raise SystemExit("The Ollama setup did not persist")
                    if (Path(saved["codex_path"]) != cli_fixture
                            or Path(saved["claude_path"]) != cli_fixture):
                        raise SystemExit("Assistant CLI executable paths did not persist")
                    key = saved["encrypted_api_key"]
                    if phase == "configure":
                        if not key or not base64.b64decode(key, validate=True):
                            raise SystemExit("Assistant did not encrypt the API key")
                    elif key:
                        raise SystemExit("Assistant did not remove the stored key")
                    if phase == "reopen":
                        workspace, = session["extra_workspaces"]
                        destination = session[workspace["id"]]
                        work = session["work"]
                        if (workspace["name"] != "Assistant space" or destination["profile_id"] != "work"
                                or len(destination["tabs"]) != 2
                                or destination["tabs"][1]["url"] != origin + "/work/active"
                                or len(work["tabs"]) != 1
                                or not any(pin["title"] == "Assistant folder" and pin["folder"]
                                           for pin in work["pins"])):
                            raise SystemExit("Assistant did not persist the Workspace, folder, and moved tab")
                print("Restored Assistant settings, removed its encrypted key, and saved Workspace actions")
                return
            if options.extension_pair or managed_extensions:
                verify_extension_pair(executable, root, origin, server, signer, openssl,
                                      extra_flags, options.extension_profile or "work",
                                      managed=managed_extensions,
                                      interactive=options.interactive_extension_management,
                                      multi=options.extension_multi or options.extension_restore_extra_profile,
                                      restore_installed=(options.restore_installed_extensions or
                                                         options.extension_restore_extra_profile),
                                      extra_profile=options.extension_restore_extra_profile,
                                      dnr=options.extension_dnr,
                                      offscreen=options.extension_offscreen,
                                      clipboard=options.extension_offscreen_clipboard,
                                      webrequest_auth=options.extension_webrequest_auth,
                                      notifications=options.extension_notifications,
                                      notification_delivery=options.extension_notification_delivery,
                                      privacy=options.extension_privacy,
                                      native_messaging=options.extension_native_messaging)
                return
            if options.shift_peek:
                server.peek_requests = []
                for phase in ("open", "restore"):
                    server.reopening = phase == "restore"
                    result = launch(executable, root, origin, [
                        *extra_flags, f"--zitter-content-shift-peek-probe={phase}",
                    ])
                    if (result.returncode or MARKER not in result.stdout.splitlines()
                            or SHIFT_PEEK_MARKER not in result.stdout.splitlines()):
                        raise SystemExit(f"Shift Peek {phase} failed.\n{result.stdout}\n{result.stderr}")
                    saved = json.loads(session_file.read_bytes())
                    if (saved.get("shift_click_peek") is not False
                            or any(saved[p]["tabs"] != expected_session[p]["tabs"]
                                   for p in ("work", "personal"))):
                        raise SystemExit("Shift Peek did not persist its toggle and preserve the session")
                if server.peek_requests != [
                    ("/peek/shift-plain", "profile_probe=personal"),
                    ("/peek/child", "profile_probe=personal"),
                    ("/peek/shift-blank", "profile_probe=personal"),
                    ("/peek/shift-blank", "profile_probe=personal"),
                ]:
                    raise SystemExit(f"Unexpected Shift Peek or child requests: {server.peek_requests}")
                print("Shift Peek and its child used Personal, and the saved toggle restored regular opening")
                return
            if options.peek:
                server.reopening = False
                server.peek_requests = []
                result = launch(executable, root, origin, [
                    *extra_flags, "--zitter-content-peek-probe",
                ])
                if (result.returncode or MARKER not in result.stdout.splitlines()
                        or PEEK_MARKER not in result.stdout.splitlines()
                        or "Zitter Content restored Peek after fullscreen without replacing its page"
                           not in result.stdout.splitlines()):
                    raise SystemExit(f"Peek failed.\n{result.stdout}\n{result.stderr}")
                saved = json.loads(session_file.read_bytes())
                for profile in ("work", "personal"):
                    if saved[profile]["tabs"] != expected_session[profile]["tabs"]:
                        raise SystemExit(f"Peek left an unexpected tab in {profile}")
                if ({path for path, _ in server.peek_requests}
                        != {"/peek/start", "/peek/next", "/peek/dirty"}
                        or any(cookie != "profile_probe=personal" for _, cookie in server.peek_requests)):
                    raise SystemExit(f"Peek crossed profile boundaries: {server.peek_requests}")
                print("Routed Peek kept Personal storage and promoted the same page into a tab and split")
                return
            if options.popup_policy:
                server.popup_requests = []
                for phase in ("grant", "restore"):
                    server.reopening = phase == "restore"
                    result = launch(executable, root, origin, [
                        *extra_flags, f"--zitter-content-popup-policy-probe={phase}",
                    ])
                    if (result.returncode or MARKER not in result.stdout.splitlines()
                            or POPUP_POLICY_MARKER not in result.stdout.splitlines()):
                        raise SystemExit(f"Popup policy {phase} failed.\n{result.stdout}\n{result.stderr}")
                    for profile in ("work", "personal"):
                        path = root / f"workspace-{profile}" / "Content Site Permissions"
                        prefs = json.loads(path.read_bytes()).get("zitter", {}) if path.exists() else {}
                        expected = {origin: 1} if profile == "work" and phase == "grant" else {}
                        if (prefs.get("popup_permissions", {}) != expected
                                or prefs.get("popups_allowed", False)):
                            raise SystemExit(f"Unexpected saved popup settings for {profile}")
                if [(path, profile) for path, profile, _, _ in server.popup_requests] != [
                    ("/popup-policy", "work"), ("/popup-policy", "work"),
                    ("/popup-policy", "work"),
                ]:
                    raise SystemExit(f"Unexpected popup policy requests: {server.popup_requests}")
                path = root / "workspace-work" / "Content Site Permissions"
                valid = path.read_bytes()
                path.write_text(json.dumps({"zitter": {
                    "popup_permissions": {origin + "/invalid": 1},
                }}), encoding="utf-8")
                require_rejection(launch(executable, root, origin),
                                  "Could not open Content site permissions")
                path.write_bytes(valid)
                print("Popup exception persisted in Work and never allowed Personal or a third-party frame")
                return
            if options.popup_commands:
                server.reopening = False
                server.popup_requests = []
                result = launch(executable, root, origin, [
                    *extra_flags, "--zitter-content-popup-probe",
                ])
                if (result.returncode or MARKER not in result.stdout.splitlines()
                        or POPUP_MARKER not in result.stdout.splitlines()):
                    raise SystemExit(f"Popup probe failed.\n{result.stdout}\n{result.stderr}")
                saved = json.loads(session_file.read_bytes())
                if (saved.get("link_rules") != {"127.0.0.1": "personal"}
                        or saved["work"]["tabs"] != expected_session["work"]["tabs"]
                        or saved["personal"]["tabs"] != expected_session["personal"]["tabs"]):
                    raise SystemExit(f"Popup changed the persisted opener layout: {saved}")
                requests = [(path, profile, body) for path, profile, _, body in server.popup_requests]
                if (requests != [
                        ("/popup-background", "work", None),
                        ("/popup-work", "work", None),
                        ("/popup-form", "work", "value=unchanged"),
                        ("/popup-get-form?value=unchanged", "work", None),
                        ("/popup-routed", "personal", None),
                    ] or server.popup_requests[-1][2] is not None):
                    raise SystemExit(f"Unexpected popup requests: {server.popup_requests}")
                print("Popups preserved their opener and form requests, and routed links sent only Personal data")
                return
            if options.link_routing:
                server.routing_requests = []
                host = "127.0.0.1"
                for phase, marker in (("save", LINK_ROUTE_MARKER), ("reopen", LINK_REOPEN_MARKER)):
                    server.reopening = phase == "reopen"
                    result = launch(executable, root, origin, [
                        *extra_flags, f"--zitter-content-link-routing-probe={phase}",
                    ])
                    if (result.returncode or MARKER not in result.stdout.splitlines()
                            or marker not in result.stdout.splitlines()):
                        raise SystemExit(f"Link routing {phase} failed.\n{result.stdout}\n{result.stderr}")
                    saved = json.loads(session_file.read_bytes())
                    for profile in ("work", "personal"):
                        if saved[profile]["tabs"] != expected_session[profile]["tabs"]:
                            raise SystemExit("Routing replaced a tab that should have remained")
                    if phase == "save":
                        if saved.get("link_rules") != {host: "personal"}:
                            raise SystemExit("The link rule was not saved")
                        valid = session_file.read_bytes()
                        for invalid_rule in ({host: "missing"}, {host + ":443": "personal"}):
                            saved["link_rules"] = invalid_rule
                            session_file.write_text(json.dumps(saved), encoding="utf-8")
                            require_rejection(launch(executable, root, origin),
                                              "Invalid Content workspace session")
                        session_file.write_bytes(valid)
                    elif "link_rules" in saved:
                        raise SystemExit("Removing the link rule was not durable")
                if server.routing_requests != [
                    ("/typed-link", "work", None),
                    ("/routed-link", "personal", None),
                    ("/routed-link", "personal", None),
                ]:
                    raise SystemExit(f"Unexpected requests across profiles: {server.routing_requests}")
                print("Link rules routed clicks and Split View without a source-profile request")
                return
            if options.split_actions:
                server.reopening = False
                result = launch(executable, root, origin, [
                    *extra_flags, "--zitter-content-split-actions-probe",
                ])
                if (result.returncode or MARKER not in result.stdout.splitlines()
                        or SPLIT_ACTIONS_MARKER not in result.stdout.splitlines()
                        or SPLIT_PALETTE_MARKER not in result.stdout.splitlines()
                        or CLOSE_OTHER_TABS_MARKER not in result.stdout.splitlines()
                        or CLOSE_OTHER_TABS_UNDO_MARKER not in result.stdout.splitlines()
                        or "Zitter Content focused third and fourth split panes"
                           not in result.stdout.splitlines()
                        or "Zitter Content cycled workspaces and selected a numbered workspace"
                           not in result.stdout.splitlines()):
                    raise SystemExit(f"Split actions failed.\n{result.stdout}\n{result.stderr}")
                saved = json.loads(session_file.read_bytes())
                if (saved["active_workspace"] != "personal"
                        or saved["work"].get("split")
                        or saved["work"]["pins"] != [split_pin]
                        or [tab["url"] for tab in saved["work"]["tabs"]]
                        != ["", origin + "/split-actions/right"]
                        or saved["personal"]["tabs"] != expected_session["personal"]["tabs"]):
                    raise SystemExit(f"Split closure saved unexpected workspace layouts: {saved}")
                print("Split pane edits preserved pages and batch close honored both beforeunload responses")
                return
            if options.interactive_import or options.encrypted_import:
                encrypted = options.encrypted_import
                source = seed_browser_import(root, encrypted=encrypted)
                phases = ["reject", "import", "repeat"]
                import_env = None
                if encrypted:
                    library = compile_file_keychain_fixture(root)
                    import_env = file_keychain_environment(library)
                    extra_flags.append("--zitter-content-interactive-import-keychain-probe")
                    phases[1:1] = ["reject-denied", "reject-host", "reject-version"]
                broken_history = source / "Default/History"
                source_cookie = source / "Profile 1/Cookies"
                valid_cookie = source_cookie.read_bytes()
                valid_history = broken_history.read_bytes()
                with closing(sqlite3.connect(broken_history)) as history, history:
                    history.execute("ALTER TABLE urls RENAME TO invalid_urls")
                first_catalogue = None
                for phase in phases:
                    server.reopening = phase != "reject"
                    env = import_env
                    if phase == "reject-denied":
                        env = dict(import_env, ZITTER_CONTENT_TEST_KEYCHAIN_LOOKUP_FAILURE="denied")
                    if phase in ("reject-host", "reject-version"):
                        damaged = encrypted_source_cookie("secondary", domain="unrelated.test")
                        if phase == "reject-version":
                            damaged = b"v20" + damaged[3:]
                        with closing(sqlite3.connect(source_cookie)) as db, db:
                            db.execute("UPDATE cookies SET encrypted_value = ?", (damaged,))
                    before = {path: path.read_bytes() for path in source.glob("*/*")}
                    result = launch(executable, root, origin, [
                        *extra_flags, f"--zitter-content-interactive-import-probe={phase}",
                    ], env=env)
                    markers = result.stdout.splitlines()
                    if (result.returncode or INTERACTIVE_IMPORT_MARKER not in markers
                            or MARKER not in markers):
                        raise SystemExit(f"Interactive import {phase} failed.\n{result.stdout}\n{result.stderr}")
                    if {path: path.read_bytes() for path in source.glob("*/*")} != before:
                        raise SystemExit("Interactive import changed its source files")
                    if encrypted:
                        lookup = "Content browser import looked up its fixture key"
                        approved = "Zitter Content approved the browser import preview"
                        expected = 0 if phase in ("reject", "repeat") else 1
                        if (markers.count(lookup) != expected or
                                (expected and markers.index(lookup) < markers.index(approved)) or
                                "Content browser import attempted to write its source key" in markers):
                            raise SystemExit(f"Unexpected source Keychain access during {phase}")
                    saved = json.loads(session_file.read_text(encoding="utf-8"))
                    if phase.startswith("reject"):
                        if (saved != expected_session or list(root.glob("profile-*"))
                                or (not windows and list((root / "NetworkProfiles").iterdir()))):
                            raise SystemExit("Rejected import published its profiles or directories")
                        broken_history.write_bytes(valid_history)
                        source_cookie.write_bytes(valid_cookie)
                        continue
                    catalogue = saved["extra_profiles"], saved["extra_workspaces"]
                    if first_catalogue is not None and catalogue != first_catalogue:
                        raise SystemExit("Import retry changed the profile or workspace IDs")
                    first_catalogue = catalogue
                    profiles = {profile["import_source"]: profile["id"]
                                for profile in catalogue[0]}
                    if len(profiles) != 2 or len(catalogue[1]) != 2:
                        raise SystemExit("Import did not publish both profiles and spaces")
                    for workspace in catalogue[1]:
                        suffix = workspace["import_source"].removeprefix("chrome-space-")
                        name = {"Default": "default", "Profile 1": "secondary"}[suffix]
                        profile_id = profiles[f"chrome-profile-{suffix}"]
                        entry = saved[workspace["id"]]
                        target = f"http://import.test/{name}"
                        if (entry["profile_id"] != profile_id or not any(
                                pin["url"] == target for pin in entry["pins"])):
                            raise SystemExit("Imported bookmark is associated with the wrong profile")
                        database = root / profile_id / "History"
                        with closing(sqlite3.connect(database.as_uri() + "?mode=ro", uri=True)) as db:
                            rows = db.execute("SELECT url,title FROM urls").fetchall()
                        if rows != [(target, name)]:
                            raise SystemExit("Imported history did not persist in its destination")
                        if not windows:
                            database = root / "NetworkProfiles" / profile_id / "Cookies"
                            with closing(sqlite3.connect(database.as_uri() + "?mode=ro", uri=True)) as db:
                                rows = db.execute("SELECT name,value,encrypted_value FROM cookies").fetchall()
                            if (len(rows) != 1 or rows[0][:2] != (f"imported_{name}", "")
                                    or not rows[0][2].startswith(ENCRYPTED_PREFIX)):
                                raise SystemExit("Imported cookie was not encrypted in its destination")
                if encrypted:
                    print("Encrypted import verified Keychain lookup, rejected corrupt data, and restored values")
                else:
                    print("Interactive import published isolated profiles and skipped a repeat")
                return
            if options.create_profile:
                server.research_requests = []
                profile_id = None
                for phase in ("create", "restore"):
                    server.reopening = phase == "restore"
                    server.research_reopening = server.reopening
                    result = launch(executable, root, origin, [
                        *extra_flags, f"--zitter-content-additional-profile-probe={phase}",
                    ])
                    markers = result.stdout.splitlines()
                    if (result.returncode or ADDITIONAL_PROFILE_MARKER not in markers
                            or MARKER not in markers):
                        raise SystemExit(f"Live profile {phase} failed.\n{result.stdout}\n{result.stderr}")
                    saved = json.loads(session_file.read_text(encoding="utf-8"))
                    profile, = saved["extra_profiles"]
                    workspace, = saved["extra_workspaces"]
                    if (profile["name"] != "Research profile"
                            or workspace["name"] != "Research"
                            or saved[workspace["id"]]["profile_id"] != profile["id"]
                            or (profile_id is not None and profile_id != profile["id"])):
                        raise SystemExit("Live profile or workspace identity changed")
                    profile_id = profile["id"]
                    if windows:
                        verify_ciphertext(root, profile_id, encrypted_prefix=b"v10")
                    else:
                        if (root / profile_id / "Network").exists():
                            raise SystemExit("Live macOS profile stored network data with its preferences")
                        database = root / "NetworkProfiles" / profile_id / "Cookies"
                        with closing(sqlite3.connect(database.as_uri() + "?mode=ro", uri=True)) as db:
                            rows = db.execute("SELECT value, encrypted_value FROM cookies "
                                              "WHERE name = 'profile_probe'").fetchall()
                        if len(rows) != 1 or rows[0][0] or not rows[0][1].startswith(ENCRYPTED_PREFIX):
                            raise SystemExit("Live macOS profile did not encrypt its cookie")
                if server.research_requests != ["", "research"]:
                    raise SystemExit(f"Live profile leaked a cookie: {server.research_requests}")
                if windows:
                    database = root / profile_id / "Network/Cookies"
                damage_probe_cookie(database)
                server.requests.clear()
                error = ("could not open its DPAPI key" if windows
                         else "could not verify its cookie stores")
                require_rejection(launch(executable, root, origin, extra_flags), error)
                if server.requests:
                    raise SystemExit("Invalid live profile loaded workspace tabs")
                print("Live profile and its workspace kept encrypted isolated storage across restart")
                return
            if options.additional_profile:
                server.reopening = False
                result = launch(executable, root, origin, extra_flags)
                if result.returncode or MARKER not in result.stdout.splitlines():
                    raise SystemExit(f"Profile bootstrap failed.\n{result.stdout}\n{result.stderr}")
                profile_id = "profile-18b282f1-2626-4cdd-8655-8ab8704a4d98"
                workspace_id = "space-d41f792a-d442-46ed-8447-f97c052199e6"
                profile = root / profile_id
                session = json.loads(session_file.read_text(encoding="utf-8"))
                session["extra_profiles"] = [{"id": profile_id, "name": "Research profile"}]
                session["extra_workspaces"] = [{"id": workspace_id, "name": "Research"}]
                session[workspace_id] = {"profile_id": profile_id, "active": 0, "tabs": [
                    {"url": origin + "/research-profile", "title": "Research"},
                ]}
                session_file.write_text(json.dumps(session), encoding="utf-8")
                require_rejection(launch(executable, root, origin, extra_flags),
                                  "Invalid additional Content profile directory or preferences")
                if not windows:
                    unregistered = root / "unregistered"
                    (unregistered / "Network").mkdir(parents=True)
                    profile.symlink_to(unregistered, target_is_directory=True)
                    require_rejection(launch(executable, root, origin, extra_flags),
                                      "Invalid additional Content profile directory or preferences")
                    profile.unlink()
                (profile / "Network").mkdir(parents=True)
                if windows:
                    (profile / "NetworkBootstrap").mkdir()
                server.reopening = True
                server.research_requests = []
                for reopening in (False, True):
                    server.research_reopening = reopening
                    result = launch(executable, root, origin, [
                        *extra_flags, "--zitter-content-additional-profile-probe",
                    ])
                    markers = result.stdout.splitlines()
                    if (result.returncode or ADDITIONAL_PROFILE_MARKER not in markers
                            or MARKER not in markers):
                        raise SystemExit(f"Additional profile failed.\n{result.stdout}\n{result.stderr}")
                    saved = json.loads(session_file.read_text(encoding="utf-8"))
                    if (saved["extra_profiles"] != session["extra_profiles"]
                            or saved[workspace_id]["profile_id"] != profile_id):
                        raise SystemExit("Additional profile assignment changed after restart")
                    verify_ciphertext(root, profile_id,
                                      encrypted_prefix=b"v10" if windows else ENCRYPTED_PREFIX)
                if server.research_requests != ["", "research"]:
                    raise SystemExit(f"Additional profile leaked a cookie: {server.research_requests}")
                valid_session = session_file.read_bytes()
                database = profile / "Network/Cookies"
                damage_probe_cookie(database)
                server.requests.clear()
                error = ("could not open its DPAPI key" if windows
                         else "could not verify its cookie stores")
                require_rejection(launch(executable, root, origin, extra_flags), error)
                if server.requests or session_file.read_bytes() != valid_session:
                    raise SystemExit("Rejected extra profile changed the session or loaded tabs")
                print("Additional profile preserved isolated encrypted storage across restart")
                return
            if (options.extra_workspace or options.reassign_workspace
                    or options.shared_favorites or options.tab_commands):
                identifier = None
                for phase in ("create", "restore"):
                    server.reopening = phase == "restore"
                    result = launch(executable, root, origin, [
                        *extra_flags, f"--zitter-content-extra-workspace-probe={phase}",
                    ])
                    markers = result.stdout.splitlines()
                    if (result.returncode or EXTRA_WORKSPACE_MARKER not in markers
                            or MARKER not in markers):
                        raise SystemExit(f"Extra Workspace {phase} failed.\n{result.stdout}\n{result.stderr}")
                    session = json.loads(session_file.read_text(encoding="utf-8"))
                    extras = session["extra_workspaces"]
                    if (len(extras) != 1 or extras[0]["name"] != "Research renamed"
                            or session["workspace_names"] != {"work": "Work renamed"}
                            or session["profile_names"] != {"work": "Work profile renamed"}):
                        raise SystemExit("Extra Workspace did not persist its metadata")
                    if identifier is not None and identifier != extras[0]["id"]:
                        raise SystemExit("Extra Workspace changed its ID on restart")
                    identifier = extras[0]["id"]
                    if (session[identifier]["profile_id"] != "work"
                            or session[identifier]["tabs"][0]["url"] != origin + "/extra-workspace"
                            or session["work"]["tabs"] != expected_session["work"]["tabs"]
                            or session["personal"]["tabs"] != expected_session["personal"]["tabs"]):
                        raise SystemExit("Extra Workspace replaced another workspace's tabs")
                    timeline = session_file.with_suffix(".timeline.jsonl")
                    entries = [json.loads(line) for line in timeline.read_text(
                        encoding="utf-8").splitlines()]
                    if not any(entry.get("w") == identifier and entry.get("p") == "work"
                               for entry in entries):
                        raise SystemExit("Time Machine did not preserve the extra Workspace binding")
                valid_session = session_file.read_bytes()
                if options.tab_commands:
                    result = launch(executable, root, origin, [
                        *extra_flags, "--zitter-content-extra-workspace-probe=tab-commands",
                    ])
                    if (result.returncode or MARKER not in result.stdout.splitlines()
                            or TAB_COMMANDS_MARKER not in result.stdout.splitlines()
                            or "Zitter Content selected numbered tabs and workspaces"
                               not in result.stdout.splitlines()):
                        raise SystemExit(f"Tab commands failed.\n{result.stdout}\n{result.stderr}")
                    saved = json.loads(session_file.read_bytes())
                    if (saved["active_workspace"] != "personal"
                            or saved["work"]["tabs"][:2] != expected_session["work"]["tabs"]
                            or saved["personal"]["tabs"][:2] != expected_session["personal"]["tabs"]
                            or saved["work"]["tabs"][2]["url"] != origin + "/tab-commands"
                            or [tab["url"] for tab in saved["personal"]["tabs"][2:]]
                            != [origin + "/tab-commands"] * 2
                            or [tab["url"] for tab in saved[identifier]["tabs"]] != [""]):
                        raise SystemExit("Tab commands saved unexpected workspace layouts")
                    print("Tab duplication, workspace moves and cross-profile copy preserved their boundaries")
                    return
                if options.shared_favorites:
                    if (session.get("profile_favorites") != {"work": [favorite]}
                            or session["work"].get("pins") != [local_pin]
                            or "pins" in session[identifier]):
                        raise SystemExit("The legacy favorite did not move to profile storage")
                    session["personal"]["tabs"][0]["pin"] = favorite["id"]
                    session_file.write_text(json.dumps(session), encoding="utf-8")
                    require_rejection(launch(executable, root, origin),
                                      "Invalid Content workspace session")
                    session_file.write_bytes(valid_session)
                    result = launch(executable, root, origin, [
                        *extra_flags, "--zitter-content-extra-workspace-probe=favorites",
                    ])
                    if (result.returncode or MARKER not in result.stdout.splitlines()
                            or SHARED_FAVORITES_MARKER not in result.stdout.splitlines()):
                        raise SystemExit(f"Shared favorites failed.\n{result.stdout}\n{result.stderr}")
                    saved = json.loads(session_file.read_bytes())
                    if ("profile_favorites" in saved or saved["work"]["pins"] != [local_pin]
                            or len(saved[identifier]["tabs"]) != 3
                            or saved[identifier]["tabs"][1]["url"] != favorite["url"]
                            or any("pin" in tab for key in ("work", identifier)
                                   for tab in saved[key]["tabs"])):
                        raise SystemExit("Unpinning did not preserve both spaces' tabs")
                    print("Closed shared favorite duplicated from its menu and unpinned from both spaces")
                    return
                session["profile_names"]["unknown"] = "Unknown profile"
                session_file.write_text(json.dumps(session), encoding="utf-8")
                require_rejection(launch(executable, root, origin),
                                  "Invalid Content workspace session")
                del session["profile_names"]["unknown"]
                session[identifier]["profile_id"] = "unregistered-profile"
                session_file.write_text(json.dumps(session), encoding="utf-8")
                require_rejection(launch(executable, root, origin),
                                  "Invalid Content workspace session")
                session_file.write_bytes(valid_session)
                valid_timeline = timeline.read_bytes()
                for entry in entries:
                    if entry.get("w") == identifier and "p" in entry:
                        entry["p"] = "personal"
                timeline.write_text("".join(json.dumps(entry) + "\n" for entry in entries),
                                    encoding="utf-8")
                require_rejection(launch(executable, root, origin),
                                  "Invalid Content workspace timeline")
                if session_file.read_bytes() != valid_session:
                    raise SystemExit("Invalid timeline changed the valid session")
                timeline.write_bytes(valid_timeline)
                if options.reassign_workspace:
                    old_mark = {
                        "t": int(time.time() * 1000),
                        "w": identifier,
                        "m": "Previous assignment",
                    }
                    old_timeline = valid_timeline + (json.dumps(old_mark) + "\n").encode()
                    timeline.write_bytes(old_timeline)
                    epoch = None
                    for phase in ("reassign", "reassigned"):
                        result = launch(executable, root, origin, [
                            *extra_flags, f"--zitter-content-extra-workspace-probe={phase}",
                        ])
                        if (result.returncode or MARKER not in result.stdout.splitlines()
                                or WORKSPACE_REASSIGNMENT_MARKER not in result.stdout.splitlines()):
                            raise SystemExit(
                                f"Workspace {phase} failed.\n{result.stdout}\n{result.stderr}"
                            )
                        catalogue = json.loads(session_file.read_bytes())
                        current_epoch = catalogue.get("timeline_epochs", {}).get(identifier)
                        if (not current_epoch or (epoch and current_epoch != epoch)
                                or catalogue[identifier]["profile_id"] != "personal"
                                or catalogue[identifier]["tabs"][0]["url"]
                                or catalogue["work"]["tabs"] != expected_session["work"]["tabs"]
                                or catalogue["personal"]["tabs"] != expected_session["personal"]["tabs"]):
                            raise SystemExit("Reassignment changed unrelated tabs or the new assignment")
                        epoch = current_epoch
                        records = [json.loads(line) for line in timeline.read_bytes().splitlines()]
                        assigned = [record for record in records if record["w"] == identifier]
                        if (not any(record.get("m") == "Current assignment" for record in assigned)
                                or not any(record.get("p") == "personal" for record in assigned)
                                or any(record.get("e") != epoch or record.get("p", "personal") != "personal"
                                       or record.get("m") == "Previous assignment" for record in assigned)):
                            raise SystemExit("Time Machine mixed two workspace assignments")
                        if not any(record.get("w") == "work" and record.get("p") == "work"
                                   for record in records):
                            raise SystemExit("Reassignment discarded another workspace's timeline")
                        if phase == "reassign":
                            timeline.write_bytes(old_timeline)
                    valid_session = session_file.read_bytes()
                    catalogue["timeline_epochs"][identifier] = "invalid-epoch"
                    session_file.write_text(json.dumps(catalogue), encoding="utf-8")
                    require_rejection(launch(executable, root, origin),
                                      "Invalid Content workspace session")
                    session_file.write_bytes(valid_session)
                    print("Reassignment opened a blank tab in the destination profile and isolated Time Machine")
                    return
                removal_fixture = json.loads(valid_session)
                removal_fixture["link_rules"] = {
                    "work-route.test": "work", "research-route.test": identifier,
                }
                session_file.write_text(json.dumps(removal_fixture), encoding="utf-8")
                removed = launch(executable, root, origin, [
                    *extra_flags, "--zitter-content-extra-workspace-probe=remove",
                ])
                if (removed.returncode
                        or WORKSPACE_REMOVAL_MARKER not in removed.stdout.splitlines()
                        or MARKER not in removed.stdout.splitlines()):
                    raise SystemExit(f"Workspace removal failed.\n{removed.stdout}\n{removed.stderr}")
                session = json.loads(session_file.read_text(encoding="utf-8"))
                if (session.get("removed_workspaces") != {identifier: "work"}
                        or identifier in session or "extra_workspaces" in session
                        or session.get("workspace_names") != {"work": "Work renamed"}
                        or session.get("link_rules") != {"work-route.test": "work"}
                        or session["work"]["tabs"] != expected_session["work"]["tabs"]
                        or session["personal"]["tabs"] != expected_session["personal"]["tabs"]):
                    raise SystemExit("Workspace removal or undo changed unrelated session data")
                # Keep the old journal to simulate an exit after the catalogue
                # commit and before the timeline is rewritten.
                timeline.write_bytes(valid_timeline)
                reopened = launch(executable, root, origin, extra_flags)
                if reopened.returncode or MARKER not in reopened.stdout.splitlines():
                    raise SystemExit(
                        f"Workspace removal did not survive restart.\n{reopened.stdout}\n{reopened.stderr}"
                    )
                persisted = json.loads(session_file.read_text(encoding="utf-8"))
                if persisted != session:
                    raise SystemExit("Reopening changed the catalogue after workspace removal")
                valid_session = session_file.read_bytes()
                session["removed_workspaces"][identifier] = "unregistered-profile"
                session_file.write_text(json.dumps(session), encoding="utf-8")
                require_rejection(launch(executable, root, origin),
                                  "Invalid Content workspace session")
                session_file.write_bytes(valid_session)
                print("Workspace names, removal, undo, and unrelated tabs survived restart")
                return
            if options.browser_import:
                source = seed_browser_import(root)
                results = None
                for phase in ("first", "repeat"):
                    server.reopening = phase == "repeat"
                    before = {path: path.read_bytes() for path in source.glob("*/*")}
                    result = launch(executable, root, origin, [
                        *extra_flags, f"--zitter-content-browser-import-probe={phase}",
                    ])
                    markers = result.stdout.splitlines()
                    if (result.returncode or BROWSER_IMPORT_MARKER not in markers
                            or MARKER not in markers):
                        raise SystemExit(f"Browser import {phase} failed.\n{result.stdout}\n{result.stderr}")
                    if {path: path.read_bytes() for path in source.glob("*/*")} != before:
                        raise SystemExit("Browser import changed its source files")
                    actual = read_browser_import_result(root, windows)
                    if results is not None and results != actual:
                        raise SystemExit("Browser import retry replaced existing cookies")
                    results = actual
                    if phase == "first":
                        for path in source.glob("*/History"):
                            with closing(sqlite3.connect(path)) as history, history:
                                history.execute("UPDATE urls SET title = 'replacement'")
                        for path in source.glob("*/Cookies"):
                            with closing(sqlite3.connect(path)) as cookies_db, cookies_db:
                                cookies_db.execute("UPDATE cookies SET value = 'replacement'")
                print("Browser import preserved source, profile mapping, encryption and existing rows")
                return
            if options.rss:
                server.rss_probe = True
                server.rss_requests = []
                for phase in ("subscribe", "restore"):
                    server.reopening = phase == "restore"
                    result = launch(executable, root, origin, [
                        *extra_flags, f"--zitter-content-rss-probe={phase}",
                    ])
                    markers = result.stdout.splitlines()
                    if result.returncode or RSS_MARKER not in markers or MARKER not in markers:
                        raise SystemExit(f"RSS {phase} failed.\n{result.stdout}\n{result.stderr}")
                    saved_session = json.loads(session_file.read_text(encoding="utf-8"))
                    reader_tabs = [tab for tab in saved_session["work"]["tabs"]
                                   if tab["url"] == "zitter://rss/"]
                    if len(reader_tabs) != 1 or len(saved_session["personal"]["tabs"]) != 2:
                        raise SystemExit("RSS session did not keep the reader in Work")
                    work_rss = root / PROFILES[0] / "ZitterRSS.json"
                    personal_rss = root / PROFILES[1] / "ZitterRSS.json"
                    articles = json.loads(work_rss.read_text(encoding="utf-8"))["feeds"][0]["articles"]
                    if personal_rss.exists() or not articles[0]["saved"] or not articles[0]["read"]:
                        raise SystemExit("RSS profile state was not saved in Work only")
                if server.rss_requests != [(None, None), (None, None)]:
                    raise SystemExit(f"Unexpected RSS requests: {server.rss_requests}")
                print("RSS discovery, credential-free fetch, profile isolation and restore passed")
                return
            if options.system_print_cancel:
                server.reopening = False
                server.print_preview = False
                result = launch(executable, root, origin, [
                    *extra_flags, "--site-per-process",
                    "--zitter-content-pdf-export-probe=system-cancel",
                ])
                markers = result.stdout.splitlines()
                if (result.returncode or MARKER not in markers or
                        "Zitter Content opened and cancelled the native print sheet" not in markers
                        or (root / "printed-fixture.pdf").exists()):
                    raise SystemExit(f"System print cancellation failed.\n{result.stdout}\n{result.stderr}")
                print("Native system print sheet opened and cancelled")
                return
            if (options.print_pdf or options.print_preview or options.scripted_print
                    or options.system_print_file):
                server.reopening = False
                server.print_preview = options.print_preview or options.scripted_print
                mode = ("scripted" if options.scripted_print else
                        "preview" if options.print_preview else
                        "system-file" if options.system_print_file else "direct")
                result = launch(executable, root, origin, [
                    *extra_flags, "--site-per-process",
                    f"--zitter-content-pdf-export-probe={mode}",
                ])
                markers = result.stdout.splitlines()
                if (result.returncode or PDF_EXPORT_MARKER not in markers
                        or MARKER not in markers
                        or (options.scripted_print and
                            "Zitter Content ignored a hidden scripted print request" not in markers)):
                    raise SystemExit(f"PDF export probe failed.\n{result.stdout}\n{result.stderr}")
                pdf = root / "printed-fixture.pdf"
                if windows:
                    parser_path = shutil.which("pdftotext")
                    if not parser_path:
                        raise SystemExit("The Windows PDF verifier requires pdftotext")
                    command = [parser_path, str(pdf), "-"]
                else:
                    script = root / "read_pdf.swift"
                    script.write_text(r"""import PDFKit
let url = URL(fileURLWithPath: CommandLine.arguments[1])
guard let document = PDFDocument(url: url) else { exit(1) }
print("Page count: \(document.pageCount)")
for index in 0..<document.pageCount {
    guard let page = document.page(at: index) else { exit(1) }
    let bounds = page.bounds(for: .mediaBox)
    print(bounds.width > bounds.height ? "Landscape" : "Portrait")
    print(page.string ?? "")
}
""", encoding="utf-8")
                    command = ["swift", str(script), str(pdf)]
                parsed = subprocess.run(command, capture_output=True, text=True, timeout=45)
                printed_text = " ".join(parsed.stdout.split())
                first_page = ("Top-level print fixture", "Cross-process print fixture")
                if server.print_preview:
                    valid = ("Selected print page" in printed_text
                             and not any(marker in printed_text for marker in first_page)
                             and (windows or "Page count: 1 Landscape" in printed_text))
                else:
                    valid = all(marker in printed_text for marker in first_page)
                    if options.system_print_file:
                        valid = valid and "Page count: 1 Landscape" in printed_text
                if parsed.returncode or not valid:
                    raise SystemExit(f"Exported PDF had unexpected content.\n{parsed.stdout}\n{parsed.stderr}")
                if options.scripted_print:
                    print("Scripted print ignored a hidden page and exported the visible selection")
                elif server.print_preview:
                    print("Preview showed two pages and exported only the selected page")
                elif options.system_print_file:
                    print("Native print settings produced a landscape PDF at the selected destination")
                else:
                    print("Exported PDF contains main and cross-process frame text")
                return
            if options.command_center:
                for phase in ("rank", "reopen"):
                    server.reopening = phase == "reopen"
                    result = launch(executable, root, origin, [
                        *extra_flags, f"--zitter-content-command-center-probe={phase}",
                    ])
                    markers = result.stdout.splitlines()
                    if result.returncode or COMMAND_CENTER_MARKER not in markers or MARKER not in markers:
                        raise SystemExit(f"Command Center {phase} failed.\n{result.stdout}\n{result.stderr}")
                    tools = json.loads((root / "ZitterTools.json").read_text(encoding="utf-8"))
                    shortcuts = {entry["id"]: entry["chord"] for entry in tools["shortcuts"]}
                    usage, = tools["command_usage"]
                    if (shortcuts.get("downloads") != "Alt+Shift+F9" or usage["id"] != "downloads"
                            or usage["target"] or usage["weight"] != 1):
                        raise SystemExit("Command Center did not save exactly one accepted command")
                print("Command Center retained ranking and the customized shortcut across restart")
                return
            if options.find_reload or options.copy_address:
                server.reopening = False
                server.find_reload_requests = []
                flag = "--zitter-content-find-reload-probe"
                env = None
                if options.copy_address:
                    flag += "=copy-address"
                    env = build_pasteboard_fixture(root)
                result = launch(executable, root, origin, [*extra_flags, flag], env=env)
                markers = result.stdout.splitlines()
                if (result.returncode or FIND_RELOAD_MARKER not in markers
                        or MARKER not in markers
                        or DEVELOPER_MENU_MARKER not in markers
                        or "Zitter Content opened source in the active profile" not in markers
                        or "Zitter Content restored window fullscreen after page fullscreen"
                           not in markers
                        or (not windows and NATIVE_EDIT_MARKER not in markers)
                        or (options.copy_address and COPY_ADDRESS_MARKER not in markers)):
                    raise SystemExit(f"Find and reload probe failed.\n{result.stdout}\n{result.stderr}")
                if server.find_reload_requests != [False, True]:
                    raise SystemExit(f"Unexpected cache bypass requests: {server.find_reload_requests}")
                tools = json.loads((root / "ZitterTools.json").read_text(encoding="utf-8"))
                if tools.get("developer_mode") != "off":
                    raise SystemExit("Developer mode was not saved")
                if options.copy_address:
                    session = json.loads((root / SESSION_FILE).read_text(encoding="utf-8"))
                    if session.get("clean_address_copies") is not False:
                        raise SystemExit("The address cleaning preference was not saved")
                    print("Sidebar, native menu, Command Center and address editor copied the expected addresses")
                print("Find in Page, Hard Reload, and developer menu passed")
                return
            if options.favicons:
                server.favicon_probe = True
                server.favicons_offline = server.reopening = False
                server.favicon_requests = []
                flags = [*extra_flags, "--zitter-content-favicon-probe"]

                def check_icons(directory, phase=""):
                    command = flags if not phase else [*flags[:-1], f"{flags[-1]}={phase}"]
                    result = launch(executable, directory, origin, command)
                    if (result.returncode or "Zitter Content verified profile tab favicons"
                            not in result.stdout.splitlines()):
                        raise SystemExit(f"Tab favicon probe failed.\n{result.stdout}\n{result.stderr}")

                check_icons(root)
                if set(server.favicon_requests) != {"/work/icon.png", "/personal/icon.png"}:
                    raise SystemExit("The two fixture icons were not fetched")
                source = root / "legacy-icons"
                source.mkdir()
                profiles = []
                for role in ("work", "personal"):
                    name = f"Zitter{role.title()}"
                    (source / name).mkdir()
                    database = root / f"workspace-{role}" / "Favicons"
                    with closing(sqlite3.connect(f"{database.as_uri()}?mode=ro", uri=True)) as db:
                        urls = db.execute("SELECT url FROM favicons WHERE url LIKE '%/icon.png'").fetchall()
                        if urls != [(f"{origin}/{role}/icon.png",)]:
                            raise SystemExit(f"Favicon database mixed the profile icons: {urls}")
                    legacy = source / name / "Favicons"
                    shutil.copy2(database, legacy)
                    with closing(sqlite3.connect(legacy)) as db, db:
                        icon_id = db.execute("SELECT id FROM favicons WHERE url = ?",
                                             (f"{origin}/{role}/icon.png",)).fetchone()[0]
                        db.executemany("INSERT INTO icon_mapping (page_url, icon_id) VALUES (?, ?)", (
                            (f"{origin}/shared-icon-pin", icon_id),
                            (f"{origin}/{role}/deferred", icon_id),
                        ))
                    profiles.append({"id": role, "name": role.title(), "directory": name})
                (source / "ZitterWorkspaces.json").write_text(json.dumps({
                    "profiles": profiles,
                    "workspaces": [{"id": role, "name": role.title(), "profile_id": role}
                                   for role in ("work", "personal")],
                }), encoding="utf-8")
                if windows:
                    shutil.copy2(root / WINDOWS_LOCAL_STATE, source / "Local State")
                else:
                    (source / "Local State").write_text("{}", encoding="utf-8")
                trial = root / "favicon-trial"
                original = inventory(source)
                prepare(source, trial)
                with tempfile.TemporaryDirectory(prefix="zitter-content-probe-") as scratch:
                    target = Path(scratch).resolve()
                    target.rmdir()
                    import_workspace_profiles(trial, target, "windows-dpapi" if windows else "workspace")
                    if not windows:
                        shutil.copy2(root / KEY_FILE, target / KEY_FILE)
                    session = seed_session(origin)
                    for role in ("work", "personal"):
                        session[role]["pins"] = [{
                            "id": f"{role}-icon-pin", "title": "Closed pin",
                            "url": f"{origin}/shared-icon-pin", "parent_id": "",
                            "folder": False, "profile_pin": False,
                        }]
                    (target / SESSION_FILE).write_text(json.dumps(session), encoding="utf-8")
                    server.favicons_offline = True
                    server.favicon_requests.clear()
                    check_icons(target, "cached")
                    server.reopening = True
                    check_icons(target, "cached")
                    if server.favicon_requests:
                        raise SystemExit("The imported profiles refetched fresh cached favicons")
                    if any(path.endswith("/deferred") or path == "/shared-icon-pin"
                           for path, _ in server.requests):
                        raise SystemExit("Reading favicons opened a deferred tab or a closed pin")
                    server.refresh_favicons = True
                    server.favicon_refresh_requests = []
                    server.favicon_refresh_saved_cookie = False
                    check_icons(target, "refresh")
                    server.refresh_favicons = False
                    expected_refresh = {
                        "/shared-icon-pin": 1, "/work/active": 1,
                        "/work/deferred": 1, "/work/renewed.png": 3,
                    }
                    observed = {}
                    for path, cookie in server.favicon_refresh_requests:
                        if cookie:
                            raise SystemExit("Favicon refresh sent profile cookies")
                        observed[path] = observed.get(path, 0) + 1
                    if observed != expected_refresh:
                        raise SystemExit(f"Unexpected favicon refresh requests: {observed}")
                    server.favicon_requests.clear()
                    server.refreshed_icon_markup = True
                    check_icons(target, "refreshed")
                    if server.favicon_requests:
                        raise SystemExit("Refreshed icons were not restored from disk")
                    if server.favicon_refresh_saved_cookie:
                        raise SystemExit("An icon response wrote a cookie into the profile")
                recovered = root / "favicon-rollback"
                rollback(trial, recovered)
                if any(inventory(path) != original for path in (source, trial / "candidate", recovered)):
                    raise SystemExit("Favicon import changed a source or rollback database")
                print("Content imported and reopened separate cached tab icons")
                return
            if options.clear_site_settings:
                if windows:
                    result = launch(executable, root, origin, extra_flags)
                    if result.returncode or MARKER not in result.stdout.splitlines():
                        raise SystemExit(f"Could not initialize the fixture DPAPI key.\n"
                                         f"{result.stdout}\n{result.stderr}")
                recent = str(time.time_ns() // 1_000 + 11_644_473_600_000_000)
                old = "13370000000000000"
                origins = ("https://recent.example", "https://old.example", "https://undated.example")
                for name in PROFILES:
                    profile = root / name
                    profile.mkdir(exist_ok=windows)
                    times = dict(zip(origins, (recent, old)))
                    write_site_permission_rules(
                        profile, dict.fromkeys(origins, 1), dict.fromkeys(origins, 2),
                        popups_allowed=True, clipboard_times=times, popup_times=times,
                    )
                    write_page_zoom_preference(profile, {"partition": {
                        "default_zoom_level": {"x": 1.0},
                        "per_host_zoom_levels": {"x": {
                            "recent.example": {"zoom_level": 2, "last_modified": recent},
                            "old.example": {"zoom_level": 3, "last_modified": old},
                            "undated.example": 2,
                        }},
                    }})
                for phase in ("site-recent", "site-all"):
                    server.reopening = windows or phase == "site-all"
                    result = launch(executable, root, origin, [
                        *extra_flags, f"--zitter-content-clear-data-smoke-test={phase}",
                    ])
                    if (result.returncode or "Zitter Content cleared Work site settings"
                            not in result.stdout.splitlines()):
                        raise SystemExit(f"Site settings clear failed.\n{result.stdout}\n{result.stderr}")
                    work, personal = (root / name for name in PROFILES)
                    for profile, expected in (
                        (work, origins[1:] if phase == "site-recent" else ()),
                        (personal, origins),
                    ):
                        settings = json.loads((profile / CLIPBOARD_PREFS).read_bytes())["zitter"]
                        zoom = json.loads((profile / PAGE_ZOOM_PREFS).read_bytes())["partition"]
                        if (any(set(settings.get(key, ())) != set(expected) for key in (
                                "clipboard_permissions", "popup_permissions"))
                                or any(set(settings.get(key, ())) != (set(expected) & set(times))
                                       for key in ("clipboard_permission_times", "popup_permission_times"))
                                or settings.get("popups_allowed") is not True
                                or zoom["default_zoom_level"] != 1.0
                                or set(zoom.get("per_host_zoom_levels", ())) != {
                                    url.split("://")[1] for url in expected}):
                            raise SystemExit("Site settings changed outside the selected profile or period")
                print("Content cleared recent and all-time Work site settings")
                return
            if options.private_windows or options.page_zoom:
                server.reopening = False
                if windows:
                    result = launch(executable, root, origin, extra_flags)
                    if result.returncode or MARKER not in result.stdout.splitlines():
                        raise SystemExit(f"Could not initialize the fixture DPAPI key.\n"
                                         f"{result.stdout}\n{result.stderr}")
                work, personal = (root / name for name in PROFILES)
                for profile, enabled in ((work, True), (personal, False)):
                    profile.mkdir(exist_ok=windows)
                    (profile / "Content Ad Blocking Preferences").write_text(
                        json.dumps({"zitter": {
                            "ad_blocking": enabled,
                            "ad_blocking_rules": [
                                "127.0.0.1##+js(set-constant, zitterPrivateParent, true)",
                            ],
                        }}),
                        encoding="utf-8",
                    )
                if options.page_zoom:
                    for profile, factor in ((work, 1.25), (personal, 0.9)):
                        write_page_zoom_preference(profile, {"partition": {
                            "default_zoom_level": {"x": math.log(factor, 1.2)},
                            "per_host_zoom_levels": {"x": ({
                                "localhost": {"zoom_level": math.log(1.75, 1.2),
                                              "last_modified": "13370000000000000"},
                                "reset.example": math.log(1.5, 1.2),
                            } if profile == work else {"localhost": math.log(1.1, 1.2)})},
                        }})
                phases = ("zoom-change", "zoom-reopen") if options.page_zoom else ("",)
                for phase in phases:
                    server.requests.clear()
                    server.reopening = windows or phase == "zoom-reopen"
                    flag = "--zitter-content-private-window-probe"
                    if phase:
                        flag += "=" + phase
                    with tempfile.TemporaryDirectory(prefix="zitter-private-session-") as temporary:
                        scratch = Path(temporary).resolve()
                        env = dict(os.environ, TMPDIR=str(scratch), TMP=str(scratch),
                                   TEMP=str(scratch))
                        result = launch(executable, root, origin, [*extra_flags, flag], env=env)
                        markers = result.stdout.splitlines()
                        if (result.returncode or PRIVATE_WINDOW_MARKER not in markers
                                or MARKER not in markers or (phase and
                                    "Zitter Content verified profile page zoom" not in markers)):
                            raise SystemExit(f"Private window probe failed.\n{result.stdout}\n{result.stderr}")
                        if any(scratch.iterdir()):
                            raise SystemExit("Private windows left files in the temporary root")
                    expected = list(zip(PRIVATE_WINDOW_PATHS, ("", "shared", "shared", "")))
                    if phase:
                        expected.append((PRIVATE_WINDOW_PATHS[-1], ""))
                    actual = [entry for entry in server.requests if entry[0] in PRIVATE_WINDOW_PATHS]
                    if actual != expected:
                        raise SystemExit(f"Unexpected private window requests: {actual}")
                if options.page_zoom:
                    for profile, factor in ((work, 1.5), (personal, 0.9)):
                        zoom = json.loads((profile / PAGE_ZOOM_PREFS).read_bytes())["partition"]
                        host = zoom["per_host_zoom_levels"]["localhost"]
                        if (not math.isclose(1.2 ** zoom["default_zoom_level"], factor)
                                or not math.isclose(1.2 ** host["zoom_level"],
                                                    1.75 if profile == work else 1.1)
                                or "reset.example" in zoom["per_host_zoom_levels"]
                                or (profile == work and
                                    host["last_modified"] != "13370000000000000")):
                            raise SystemExit("The saved profile or site zoom changed unexpectedly")
                    print("Content changed Work page zoom, reopened it, and inherited it privately")
                for profile in (work, personal):
                    db = profile / "Network/Cookies"
                    with closing(sqlite3.connect(f"{db.as_uri()}?mode=ro", uri=True)) as connection:
                        if connection.execute(
                                "SELECT 1 FROM cookies WHERE name = 'private_probe'").fetchone():
                            raise SystemExit("Private cookies reached a regular profile")
                print("Content private windows shared and discarded an isolated session")
                return
            if options.navigation:
                server.reopening = False
                result = launch(executable, root, origin, [
                    *extra_flags, "--zitter-content-navigation-smoke-test",
                ])
                lines = result.stdout.splitlines()
                if (result.returncode or NAVIGATION_MARKER not in lines
                        or (sys.platform == "darwin"
                            and "Zitter Content validated native menu navigation" not in lines)):
                    raise SystemExit(f"Live navigation probe failed.\n{result.stdout}\n{result.stderr}")
                initial = [(path, "") for path in ACTIVE_PATHS]
                later = server.requests[len(initial):]
                if (server.requests[:len(initial)] != initial
                        or not set(NAVIGATION_PATHS).issubset(path for path, _ in later)
                        or any(path not in (*ACTIVE_PATHS, *NAVIGATION_PATHS)
                               or cookie != path.split("/")[1] for path, cookie in later)):
                    raise SystemExit(f"Unexpected navigation requests: {server.requests}")
                for path in NAVIGATION_PATHS:
                    profile = path.split("/")[1]
                    workspace = expected_session[profile]
                    workspace["tabs"][workspace["active"]] = {
                        "url": origin + path, "title": "Workspace navigation fixture",
                    }
                if json.loads(session_file.read_text(encoding="utf-8")) != expected_session:
                    raise SystemExit("Navigation changed the saved workspace layout")
                print("Back and Forward survived live workspace switches")
                return
            if options.closed_tabs:
                server.reopening = False
                result = launch(executable, root, origin, [
                    *extra_flags, "--zitter-content-closed-tab-smoke-test",
                ])
                lines = result.stdout.splitlines()
                if (result.returncode or CLOSED_TAB_MARKER not in lines
                        or (sys.platform == "darwin"
                            and "Zitter Content validated native menu closed tab recovery" not in lines)):
                    raise SystemExit(f"Recently closed tab probe failed.\n{result.stdout}\n{result.stderr}")
                expected_requests = [(path, "") for path in ACTIVE_PATHS]
                closed_tab_loads = 5 if sys.platform == "darwin" else 4
                expected_requests.extend([(CLOSED_TAB_PATH, "work")] * closed_tab_loads)
                if server.requests != expected_requests:
                    raise SystemExit(f"Unexpected closed-tab requests: {server.requests}")
                if json.loads(session_file.read_text(encoding="utf-8")) != expected_session:
                    raise SystemExit("Closed-tab metadata escaped into the saved session")
                print("Recently closed tabs stayed within their workspace")
                return
            if options.extension_registry:
                server.extension_install_ui = True
                server.extension_reload_requests = []
                server.extension_post_requests = []
                server.extension_subframe_post_requests = []
                server.extension_deferred_duplicates = 0
                with tempfile.TemporaryDirectory(prefix="zitter-content-probe-") as directory:
                    ui_root = Path(directory).resolve()
                    ui_session = seed_session(origin)
                    observer_workspace = "space-d41f792a-d442-46ed-8447-f97c052199e6"
                    ui_session["extra_workspaces"] = [
                        {"id": observer_workspace, "name": "Extension discard observer"},
                    ]
                    ui_session[observer_workspace] = {
                        "profile_id": extension_profile, "active": 2,
                        "tabs": [
                            {"url": origin + "/extension-content/api-tab-deferred",
                             "title": "Deferred reload fixture"},
                            {"url": origin + "/extension-content/api-duplicate-deferred",
                             "title": "Deferred duplication fixture"},
                            {"url": origin + "/extension-content/tab-sleep-observer",
                             "title": "Extension discard observer"},
                        ],
                    }
                    if options.extension_module_worker:
                        server.extension_failure_fixture = True
                        try:
                            verify_failed_worker_install(
                                executable, origin, broken_worker_archive, extension_id,
                                extension_profile, extra_flags, ui_session,
                            )
                        finally:
                            server.extension_failure_fixture = False
                    (ui_root / SESSION_FILE).write_text(json.dumps(ui_session), encoding="utf-8")
                    if windows:
                        (ui_root / WINDOWS_LOCAL_STATE).write_text(json.dumps({
                            "os_crypt": {"encrypted_key": new_wrapped_windows_key()},
                        }), encoding="utf-8")
                    else:
                        ui_key = ui_root / KEY_FILE
                        ui_key.write_bytes(secrets.token_bytes(32))
                        ui_key.chmod(0o600)
                    ui_install = ui_root / f"workspace-{extension_profile}" / "Extensions"
                    ui_other = ui_root / f"workspace-{other_profile}" / "Extensions"
                    for receiver in (allowed_receiver, restricted_receiver):
                        seed_approved_receiver(ui_install.parent, receiver)
                    seed_approved_receiver(ui_other.parent, peer_receiver)
                    ui_flags = [
                        *extra_flags, "--zitter-content-restore-extensions",
                        "--disable-audio-output",
                        f"{EXTENSION_INSTALL_UI_SWITCH}={source_archive}",
                        f"{EXTENSION_PROFILE_SWITCH}={extension_profile}",
                    ]
                    for action, marker in (
                        ([EXTENSION_INSTALL_UI_CANCEL], "Zitter Content cancelled the extension review"),
                        ([], "Zitter Content installed an extension from Settings"),
                    ):
                        (ui_root / SESSION_FILE).write_text(json.dumps(ui_session), encoding="utf-8")
                        result = launch(executable, ui_root, origin, [*ui_flags, *action])
                        if result.returncode or marker not in result.stdout.splitlines():
                            raise SystemExit(f"Extension Settings trial failed.\n{result.stdout}\n{result.stderr}")
                        if not action and ("Zitter Content verified extension permission consent and revocation"
                                           not in result.stdout.splitlines()):
                            raise SystemExit("The signed extension did not exercise permission consent and revocation")
                        if not action and ("Zitter Content started the installed extension without a restart"
                                           not in result.stdout.splitlines()):
                            raise SystemExit("Settings did not activate the extension in the current session")
                        if not action and ("Zitter Content woke a stopped extension worker with a runtime message"
                                           not in result.stdout.splitlines()):
                            raise SystemExit("The runtime message did not wake the stopped worker")
                        if not action and ("Zitter Content routed extension tab messages and rejected foreign targets"
                                           not in result.stdout.splitlines()):
                            raise SystemExit(
                                f"The extension did not validate tab message routing.\n"
                                f"{result.stdout}\n{result.stderr}")
                        if not action and ("Zitter Content delivered tab lifecycle events to the installed worker"
                                           not in result.stdout.splitlines()):
                            raise SystemExit("The extension did not validate tab lifecycle delivery")
                        if not action and not {
                            "Zitter Content opened extension options only in their owning profile",
                            "Zitter Content reused the options tab for runtime.openOptionsPage",
                            "Zitter Content opened the extension action popup in its owning profile",
                            "Zitter Content opened and closed the extension side panel in its owning tab",
                            "Zitter Content revoked activeTab after cross-origin navigation",
                            "Zitter Content verified extension context menu dispatch",
                            "Zitter Content verified activeTab for a named extension command",
                            "Zitter Content verified web-accessible extension resources",
                        }.issubset(result.stdout.splitlines()):
                            raise SystemExit(f"Extension options trial failed.\n{result.stdout}\n{result.stderr}")
                        if not action and ("Zitter Content kept the extension tab ID across Workspace sleep"
                                           not in result.stdout.splitlines()):
                            raise SystemExit(
                                "The extension did not verify tab identity across Workspace sleep.\n"
                                f"{result.stdout}\n{result.stderr}")
                        if (sorted(entry.name for entry in ui_other.iterdir()) != [peer_receiver[0]]
                                or (ui_other.parent / "Local Extension Settings" / extension_id).exists()
                                or (action and ((ui_install / extension_id).exists() or
                                                (ui_install.parent / "Local Extension Settings" /
                                                 extension_id).exists()))):
                            raise SystemExit("Extension Settings wrote extension data into an unexpected profile")
                    if server.extension_reload_requests != [
                        ("/extension-content/api-tab-open", ""),
                        ("/extension-content/api-tab-open", "max-age=0"),
                        ("/extension-content/api-tab-open", "no-cache"),
                        ("/extension-content/api-tab-deferred", "no-cache"),
                    ] or server.extension_post_requests != [b"fixture=reload", b"fixture=reload"]:
                        raise SystemExit(
                            f"Unexpected tab reload requests: {server.extension_reload_requests}, "
                            f"POST bodies {server.extension_post_requests}")
                    if server.extension_deferred_duplicates != 1:
                        raise SystemExit("Duplicating a deferred tab loaded more than one page")
                    if server.extension_subframe_post_requests != [b"fixture=history"]:
                        raise SystemExit("A tab operation repeated a subframe POST")
                    for receiver, profile, expect_message in (
                        (allowed_receiver, ui_install.parent, True),
                        (restricted_receiver, ui_install.parent, False),
                        (peer_receiver, ui_other.parent, False),
                    ):
                        store = profile / "Local Extension Settings" / receiver[0]
                        records = [entry.read_bytes() for entry in store.glob("*.log")]
                        if not any(b"externalReceiverReady" in record for record in records):
                            raise SystemExit("An external messaging receiver never started")
                        if any(b"externalProbeSender" in record for record in records) != expect_message:
                            raise SystemExit("External messaging did not stay within its allowlist and profile")
                    ui_store = ui_install.parent / "Local Extension Settings" / extension_id
                    event_record = any(
                        b'zitterInstallReason' in data and b'"install"' in data
                        for data in (entry.read_bytes() for entry in ui_store.glob("*.log"))
                    )
                    if not event_record:
                        raise SystemExit("The newly installed worker did not handle runtime.onInstalled")
                    if not any(b'zitterFrameStorage' in entry.read_bytes()
                               for entry in ui_store.glob("*.log")):
                        raise SystemExit("The isolated frame did not write to the owner's extension store")
                    owner_snapshot = extension_preferences(ui_install.parent)
                    peer_snapshot = extension_preferences(ui_other.parent)
                    if (extension_id not in owner_snapshot or extension_id in peer_snapshot
                            or set(owner_snapshot[extension_id]) != {"active_permissions"}
                            or peer_receiver[0] not in peer_snapshot):
                        raise SystemExit("The Settings permission snapshots escaped their profiles")
                    ui_extension = ui_install / extension_id
                    grant_record = ui_extension / "optional-grants.json"
                    if json.loads(grant_record.read_text(encoding="utf-8")) != {
                        "public_key": manifest["key"],
                        "permissions": {
                            "permissions": ["alarms"],
                            "origins": ["http://*.unapproved.example.com/*"],
                        },
                    } or (ui_other / extension_id / grant_record.name).exists():
                        raise SystemExit("Approved scope did not stay with its signing key and owning profile")
                    if ((ui_extension / "1.0_0/fixture.crx").read_bytes() != archive_bytes
                            or (ui_extension / "trusted-key-sha256").read_text("ascii") != digest.upper()):
                        raise SystemExit("Settings did not save the reviewed archive and signing key")
                    (ui_root / SESSION_FILE).write_text(
                        json.dumps(seed_session(origin)), encoding="utf-8")
                    (ui_root / SESSION_FILE).with_suffix(".timeline.jsonl").unlink(missing_ok=True)
                    result = launch(executable, ui_root, origin, [
                        *extra_flags, EXTENSION_REGISTRY_SWITCH, trust_flag,
                        *extension_profile_flags,
                    ])
                    if (result.returncode or MARKER not in result.stdout.splitlines()
                            or EXTENSION_WORKER_MARKER not in result.stdout.splitlines()):
                        version_root = ui_extension / "1.0_0"
                        with ZipFile(source_archive) as package:
                            expected = {entry.filename for entry in package.infolist()
                                        if not entry.is_dir()}
                            actual = {path.relative_to(version_root).as_posix()
                                      for path in version_root.rglob("*") if path.is_file()}
                            actual.discard("fixture.crx")
                            changed = sorted(name for name in expected & actual
                                             if (version_root / name).read_bytes() != package.read(name))
                        raise SystemExit(
                            "Settings installation did not run on restart.\n"
                            f"Missing files: {sorted(expected - actual)}\n"
                            f"Unexpected files: {sorted(actual - expected)}\n"
                            f"Changed files: {changed}\n{result.stdout}\n{result.stderr}")
                    update_flags = [
                        flag for flag in ui_flags
                        if not flag.startswith(f"{EXTENSION_INSTALL_UI_SWITCH}=")
                    ] + [f"{EXTENSION_INSTALL_UI_SWITCH}={reviewed_update_archive}"]
                    for cancel, expected_version, marker in (
                        (True, "1.0", "Zitter Content cancelled the extension review"),
                        (False, "2.0", "Zitter Content approved an extension update from Settings"),
                    ):
                        (ui_root / SESSION_FILE).write_text(json.dumps(ui_session), encoding="utf-8")
                        result = launch(executable, ui_root, origin, [
                            *update_flags, *([EXTENSION_INSTALL_UI_CANCEL] if cancel else []),
                        ])
                        if (result.returncode or marker not in result.stdout.splitlines()
                                or "Zitter Content disclosed expanded extension access in Settings"
                                   not in result.stdout.splitlines()
                                or (ui_extension / "active-version").read_text("ascii") != expected_version
                                or (ui_extension / "2.0_0").exists() == cancel
                                or (ui_extension / "pending-update-2.0_0").exists() == cancel):
                            raise SystemExit(
                                f"Settings update review failed.\n{result.stdout}\n{result.stderr}")
                    if ((ui_extension / "2.0_0/fixture.crx").read_bytes() != reviewed_update_archive.read_bytes()
                            or (ui_other / extension_id).exists()
                            or (ui_other.parent / "Local Extension Settings" / extension_id).exists()):
                        raise SystemExit("Settings updated an unexpected package or profile")
                    (ui_root / SESSION_FILE).write_text(json.dumps(seed_session(origin)), encoding="utf-8")
                    verify_settings_update_event(executable, ui_root, origin, extra_flags,
                                                 ui_extension, ui_store)
                    if json.loads(grant_record.read_text(encoding="utf-8"))["permissions"] != {
                        "permissions": [], "origins": [],
                    }:
                        raise SystemExit("Revocation did not clear the saved optional scope")
                    (ui_root / SESSION_FILE).write_text(json.dumps(seed_session(origin)), encoding="utf-8")
                    (ui_root / SESSION_FILE).with_suffix(".timeline.jsonl").unlink(missing_ok=True)
                    result = launch(executable, ui_root, origin, [
                        *extra_flags, EXTENSION_REGISTRY_SWITCH, trust_flag,
                        *extension_profile_flags,
                    ])
                    if (result.returncode or EXTENSION_WORKER_MARKER not in result.stdout.splitlines()
                            or not any(b'zitterFrameStorage' in entry.read_bytes()
                                       for entry in ui_store.glob("*.log"))):
                        raise SystemExit(
                            f"The Settings update did not restore the worker and its data.\n"
                            f"{result.stdout}\n{result.stderr}")
                    print("Settings approved a same-key extension update and kept profile data")
                    print("Settings cancelled and approved the signed extension in the selected profile")
                    print("Extension permission dialog cancelled and granted access, then removal blocked the host again")
                    print("Approved optional permissions and their later revocation survived browser restarts")
                server.extension_install_ui = False
                server.requests = []
                require_rejection(launch(executable, root, origin, [
                    install_flag, trust_flag, f"{EXTENSION_PROFILE_SWITCH}=unknown",
                ]), "Content extension fixture profile must be work or personal")
                # A rejected package must never publish a partial installation.
                for path, pin, error in (
                    (corrupt_archive, trust_flag, "Content extension CRX3 verification failed"),
                    (source_archive, f"{EXTENSION_KEY_SWITCH}={'0' * 64}",
                     "Content extension CRX3 verification failed"),
                    (unsafe_archive, trust_flag, "Content extension CRX3 verification failed"),
                    *((path, trust_flag, "Content extension CRX3 verification failed")
                      for path in oversized_archives),
                    (keyless_archive, trust_flag, "Could not install Content extension fixture"),
                    *((path, trust_flag, error) for path, error in rejected_metadata),
                    *((path, trust_flag, "Extension manifest uses capabilities unsupported by the Content host")
                      for path in unsupported_archives),
                    *((path, trust_flag, "Content extension CRX3 verification failed")
                      for path in collision_archives),
                ):
                    require_rejection(
                        launch(executable, root, origin, [
                            f"{EXTENSION_REGISTRY_SWITCH}={path}", pin, *extension_profile_flags,
                        ]),
                        error,
                    )
                    if install_root.exists() or other_install_root.exists() or server.requests:
                        raise SystemExit("A rejected package published files or loaded a workspace tab")

                preview_flags = [trust_flag, *extension_profile_flags,
                                 f"{EXTENSION_REVIEW_SWITCH}=preview"]
                localized = launch(executable, root, origin, [
                    f"{EXTENSION_REGISTRY_SWITCH}={localized_archive}", *preview_flags,
                ])
                localized_previews = [
                    json.loads(line.removeprefix(EXTENSION_REVIEW_MARKER))
                    for line in localized.stdout.splitlines()
                    if line.startswith(EXTENSION_REVIEW_MARKER)
                ]
                if (localized.returncode or len(localized_previews) != 1 or
                        localized_previews[0].get("name") != "Localized Zitter fixture" or
                        install_root.exists() or server.requests):
                    raise SystemExit(
                        f"Localized extension review failed.\n{localized.stdout}\n{localized.stderr}"
                    )
                broad = launch(executable, root, origin, [
                    f"{EXTENSION_REGISTRY_SWITCH}={broad_archive}", *preview_flags,
                ])
                broad_previews = [
                    json.loads(line.removeprefix(EXTENSION_REVIEW_MARKER))
                    for line in broad.stdout.splitlines()
                    if line.startswith(EXTENSION_REVIEW_MARKER)
                ]
                if (broad.returncode or len(broad_previews) != 1 or
                        broad_previews[0].get("host_permissions") != ["*://*/*"] or
                        broad_previews[0].get("network_hosts") != ["*://*/*"] or
                        broad_previews[0]["content_scripts"][0]["matches"] != ["*://*/*"] or
                        install_root.exists() or server.requests):
                    raise SystemExit(
                        f"All-sites extension review failed.\n{broad.stdout}\n{broad.stderr}"
                    )
                separate = launch(executable, root, origin, [
                    f"{EXTENSION_REGISTRY_SWITCH}={separate_archive}", *preview_flags,
                ])
                separate_previews = [
                    json.loads(line.removeprefix(EXTENSION_REVIEW_MARKER))
                    for line in separate.stdout.splitlines()
                    if line.startswith(EXTENSION_REVIEW_MARKER)
                ]
                if (separate.returncode or len(separate_previews) != 1 or
                        separate_previews[0].get("host_permissions") != separate_schemes or
                        not separate_previews[0].get("file_access_blocked") or
                        "file:///*" in separate_previews[0].get("network_hosts", []) or
                        install_root.exists() or server.requests):
                    raise SystemExit(
                        f"Separate host schemes review failed.\n{separate.stdout}\n{separate.stderr}"
                    )
                result = launch(executable, root, origin, [install_flag, *preview_flags])
                previews = [line.removeprefix(EXTENSION_REVIEW_MARKER)
                            for line in result.stdout.splitlines()
                            if line.startswith(EXTENSION_REVIEW_MARKER)]
                if result.returncode or len(previews) != 1:
                    raise SystemExit(f"Extension review failed.\n{result.stdout}\n{result.stderr}")
                review = json.loads(previews[0])
                approval = review.pop("approval")
                expected_scripts = [{
                    "run_at": script.get("run_at", "document_idle"),
                    "world": script.get("world", "ISOLATED"),
                    "all_frames": script.get("all_frames", False),
                    "match_about_blank": script.get("match_about_blank", False),
                    "match_origin_as_fallback": script.get("match_origin_as_fallback", False),
                    "matches": sorted(script["matches"]),
                    "exclude_matches": sorted(script.get("exclude_matches", [])),
                    "include_globs": script.get("include_globs", []),
                    "exclude_globs": script.get("exclude_globs", []),
                } for script in manifest["content_scripts"]]
                if (review != {
                    "review_version": 7,
                    "name": manifest["name"],
                    "version": manifest["version"],
                    "id": extension_id,
                    "profile": str(install_root.parent),
                    "key_sha256": digest.upper(),
                    "archive_sha256": hashlib.sha256(archive_bytes).hexdigest().upper(),
                    "api_permissions": [
                        "activeTab", "clipboardRead", "clipboardWrite", "contextMenus",
                        "fontSettings", "scripting", "sidePanel", "storage", "webNavigation",
                    ],
                    "host_permissions": manifest["host_permissions"],
                    "side_panel_path": "probe.html",
                    "optional_api_permissions": manifest["optional_permissions"],
                    "optional_host_permissions": manifest["optional_host_permissions"],
                    "network_hosts": [
                        "http://127.0.0.1/*", "http://fallback.localhost/*", "http://localhost/*",
                        *review_hosts, "https://scope.example.com/*",
                    ],
                    "external_extension_ids": sorted(manifest["externally_connectable"]["ids"]),
                    "content_scripts": expected_scripts,
                    "background_service_worker": True,
                    "file_access_blocked": True,
                    "options_page": "/probe.html",
                    "action_popup": "/probe.html",
                    "extension_pages_csp": extension_csp,
                    "commands": manifest["commands"],
                    "web_accessible_resources": [{
                        "resources": ["/assets/data.bin"],
                        "matches": ["http://127.0.0.1/*"],
                        "use_dynamic_url": False,
                    }, {
                        "resources": ["/assets/empty.txt"],
                        "matches": ["<all_urls>"],
                        "use_dynamic_url": True,
                    }],
                } or len(approval) != 64):
                    raise SystemExit(f"Unexpected extension review: {review}")
                approved_flag = f"{EXTENSION_REVIEW_SWITCH}={approval}"
                for flags in (
                    [f"{EXTENSION_REGISTRY_SWITCH}={replaced_archive}", *extension_profile_flags],
                    [install_flag, f"{EXTENSION_PROFILE_SWITCH}={other_profile}"],
                ):
                    require_rejection(launch(executable, root, origin, [
                        *flags, trust_flag, approved_flag,
                    ]), "Content extension review does not match this package and profile")
                result = launch(executable, root, origin, [
                    f"{EXTENSION_REGISTRY_SWITCH}={corrupt_archive}", *preview_flags,
                ])
                require_rejection(result, "Content extension CRX3 verification failed")
                if (EXTENSION_REVIEW_MARKER in result.stdout or install_root.exists()
                        or other_install_root.exists() or server.requests):
                    raise SystemExit("Review or refused approval published an extension or loaded tabs")
                print("Reviewed the large signed archive and rejected packages over each size limit")
                print("Signed review disclosed the grants and rejected approval for changed bytes or profile")

            launch_count = 4 if options.extension_registry else 3 if windows else 2
            for run in range(launch_count):
                server.requests = []
                if windows and run == 2:
                    verify_windows_key_failure(executable, root, origin)
                    if server.requests:
                        raise SystemExit("A failed Windows key lookup loaded a workspace tab")
                server.reopening = run != 0
                server.cleared_work = options.clear_data and run == 1
                clear_now = options.clear_data and run == 0
                flags = extra_flags + (["--zitter-content-clear-data-smoke-test"] if clear_now else [])
                if options.extension_registry:
                    flags.extend([
                        trust_flag, install_flag if run == 0 else EXTENSION_REGISTRY_SWITCH,
                        *extension_profile_flags,
                    ])
                    if run == 0:
                        flags.append(approved_flag)
                    if run == 2:
                        flags.append(f"{EXTENSION_UPDATE_SWITCH}={update_archive}")
                result = launch(executable, root, origin, flags)
                if result.returncode or MARKER not in result.stdout.splitlines():
                    raise SystemExit(
                        f"Workspace probe failed ({result.returncode}).\n"
                        f"{result.stdout}\n{result.stderr}"
                    )
                if options.history_panel and HISTORY_MARKER not in result.stdout.splitlines():
                    raise SystemExit(f"History panel probe did not finish.\n{result.stdout}\n{result.stderr}")
                if options.address_history and ADDRESS_HISTORY_MARKER not in result.stdout.splitlines():
                    raise SystemExit(f"Address history probe did not finish.\n{result.stdout}\n{result.stderr}")
                if clear_now and CLEAR_DATA_MARKER not in result.stdout.splitlines():
                    raise SystemExit(f"Clear-data probe did not finish.\n{result.stdout}\n{result.stderr}")
                if options.extension_registry:
                    markers = {
                        EXTENSION_REGISTRY_MARKER, EXTENSION_NAVIGATION_MARKER,
                        EXTENSION_SUBRESOURCE_MARKER, EXTENSION_WORKER_MARKER,
                        EXTENSION_QUEUE_MARKER, EXTENSION_CONTENT_SCRIPT_MARKER,
                        EXTENSION_EXTRA_SCOPE_MARKER,
                        EXTENSION_STORAGE_MARKER + str(run + 1),
                        EXTENSION_SANDBOX_MARKER,
                    }
                    if run == 0:
                        markers.add(EXTENSION_INSTALL_MARKER)
                    elif run == 2:
                        markers.add("Zitter Content updated its extension fixture to 2.0")
                    else:
                        markers.add(EXTENSION_RESTORE_MARKER)
                    if ":FATAL:" in result.stderr or not markers.issubset(result.stdout.splitlines()):
                        raise SystemExit(
                            f"Extension registry probe failed.\n{result.stdout}\n{result.stderr}"
                        )
                    store_name = "Local Extension Settings"
                    store = install_root.parent / store_name
                    if list(store.glob("*/CURRENT")) != [store / extension_id / "CURRENT"] or any(
                        (profile / store_name).exists()
                        for profile in (other_install_root.parent, root / "extension-private")
                    ):
                        raise SystemExit("The extension store escaped its profile or changed ID")
                    sync_store_name = "Sync Extension Settings"
                    sync_store = install_root.parent / sync_store_name
                    if list(sync_store.glob("*/CURRENT")) != [
                        sync_store / extension_id / "CURRENT"
                    ] or any((profile / sync_store_name).exists() for profile in (
                        other_install_root.parent, root / "extension-private",
                    )):
                        raise SystemExit("The sync extension store escaped its profile or changed ID")
                    current_version = "1.0" if run < 2 else "2.0"
                    current_dir = installed_id / (current_version + "_0")
                    current_manifest_bytes = manifest_bytes if run < 2 else update_manifest_bytes
                    current_archive_bytes = archive_bytes if run < 2 else update_archive_bytes
                    if (
                        active_version_file.read_text(encoding="ascii") != current_version
                        or (installed_id / "trusted-key-sha256").read_text(encoding="ascii")
                        != digest.upper()
                        or (current_dir / "manifest.json").read_bytes() != current_manifest_bytes
                        or (current_dir / "fixture.crx").read_bytes() != current_archive_bytes
                        or (current_dir / EXTENSION_NESTED_SCRIPT).read_bytes() != nested_script_bytes
                        or (current_dir / "assets/empty.txt").read_bytes() != b""
                        or (current_dir / "assets/data.bin").read_bytes() != bytes(range(256))
                        or (previous_version_dir / "fixture.crx").read_bytes() != archive_bytes
                        or other_install_root.exists()
                    ):
                        raise SystemExit("The extension version or profile layout was unexpected")
                    if ((previous_version_dir / "payload/stored-0.bin").stat().st_size != 14 * 1024 * 1024
                            or (previous_version_dir / "payload/deflated-0.bin").stat().st_size != 16 * 1024 * 1024
                            or len(list((previous_version_dir / "payload/entries").iterdir())) != 1050):
                        raise SystemExit("The installed large extension lost archive entries")
                expected_requests = [
                    (path, path.split("/")[1] if server.reopening
                     and not (server.cleared_work and path.startswith("/work/")) else "")
                    for path in ACTIVE_PATHS
                ]
                if options.extension_registry:
                    expected_requests.extend([
                        (EXTENSION_ALLOWED_PATH, ""),
                        (EXTENSION_EXTRA_ALLOWED_PATH, ""),
                        (EXTENSION_EXTRA_EXCLUDED_PATH, ""),
                        (EXTENSION_UNMATCHED_PATH, ""),
                        (EXTENSION_ALLOWED_PATH, ""),
                        (EXTENSION_ALLOWED_PATH, ""),
                        (EXTENSION_ALLOWED_PATH, ""),
                    ])
                if server.requests != expected_requests:
                    raise SystemExit(f"Unexpected requests: {server.requests}")
                if json.loads(session_file.read_text(encoding="utf-8")) != expected_session:
                    raise SystemExit("Saved workspace session changed its tabs or selection")
                for profile in PROFILES:
                    if clear_now and profile == "workspace-work":
                        verify_cookie_removed(root, profile)
                        verify_history(root, profile, {})
                        continue
                    verify_ciphertext(root, profile, encrypted_prefix=encrypted_prefix)
                    name = profile.removeprefix("workspace-")
                    visit_count = run if options.clear_data and name == "work" else run + 1
                    expected_history = {
                        f"{origin}/{name}/active": (
                            f"Zitter workspace probe {name}", visit_count
                        ),
                    }
                    if options.extension_registry:
                        expected_history[origin + EXTENSION_ALLOWED_PATH] = (
                            EXTENSION_SCRIPT_ABSENT,
                            (2 if name == extension_profile else 1) * (run + 1),
                        )
                        if name == extension_profile:
                            for path in (EXTENSION_UNMATCHED_PATH, EXTENSION_EXTRA_ALLOWED_PATH,
                                         EXTENSION_EXTRA_EXCLUDED_PATH):
                                expected_history[origin + path] = (
                                    ("Extension content script ran at document start"
                                     if path == EXTENSION_EXTRA_ALLOWED_PATH
                                     else EXTENSION_SCRIPT_ABSENT),
                                    run + 1,
                                )
                    verify_history(root, profile, expected_history)
                if options.extension_registry and run == 0:
                    require_rejection(
                        launch(executable, root, origin, [
                            EXTENSION_REGISTRY_SWITCH, trust_flag,
                            f"{EXTENSION_PROFILE_SWITCH}={other_profile}",
                        ]),
                        "Could not restore Content extension fixture",
                    )
                    require_rejection(
                        launch(executable, root, origin, [
                            install_flag, trust_flag, *extension_profile_flags,
                        ]),
                        "Could not install Content extension fixture",
                    )
                    if (previous_version_dir / "manifest.json").read_bytes() != manifest_bytes:
                        raise SystemExit("A repeated install changed the installed package")
                    worker = previous_version_dir / "worker.js"
                    installed_archive = previous_version_dir / "fixture.crx"
                    worker_bytes = worker.read_bytes()
                    for path, original in (
                        (worker, worker_bytes), (installed_archive, archive_bytes),
                        (previous_version_dir / EXTENSION_NESTED_SCRIPT, nested_script_bytes),
                    ):
                        path.write_bytes(original + b"\n")
                        require_rejection(
                            launch(executable, root, origin, [
                                EXTENSION_REGISTRY_SWITCH, trust_flag, *extension_profile_flags,
                            ]),
                            "Could not restore Content extension fixture",
                        )
                        path.write_bytes(original)
                    if not windows:
                        assets = previous_version_dir / "assets"
                        outside_assets = root / "external-assets"
                        assets.rename(outside_assets)
                        assets.symlink_to(outside_assets, target_is_directory=True)
                        require_rejection(
                            launch(executable, root, origin, [
                                EXTENSION_REGISTRY_SWITCH, trust_flag, *extension_profile_flags,
                            ]),
                            "Could not restore Content extension fixture",
                        )
                        assets.unlink()
                        outside_assets.rename(assets)
                    if hasattr(os, "mkfifo"):
                        empty_asset = previous_version_dir / "assets/empty.txt"
                        empty_asset.unlink()
                        os.mkfifo(empty_asset)
                        require_rejection(
                            launch(executable, root, origin, [
                                EXTENSION_REGISTRY_SWITCH, trust_flag, *extension_profile_flags,
                            ]),
                            "Could not restore Content extension fixture",
                        )
                        empty_asset.unlink()
                        empty_asset.touch()
                    for invalid_update, expected_error in (
                        (corrupt_update, "Content extension CRX3 verification failed"),
                        (changed_policy_archive, "Content extension update must be newer and retain its permissions"),
                        (changed_commands_archive, "Content extension update must be newer and retain its permissions"),
                        (escalated_archive, "Content extension update must be newer and retain its permissions"),
                        (resource_escalation_archive, "Content extension update must be newer and retain its permissions"),
                        (world_escalation_archive, "Content extension update must be newer and retain its permissions"),
                        (tabs_escalation_archive, "Content extension update must be newer and retain its permissions"),
                        (host_escalation_archive, "Content extension update must be newer and retain its permissions"),
                        (origin_escalation_archive, "Content extension update must be newer and retain its permissions"),
                        (about_escalation_archive, "Content extension update must be newer and retain its permissions"),
                        (frame_escalation_archive, "Content extension update must be newer and retain its permissions"),
                        (broader_archive, "Content extension update must be newer and retain its permissions"),
                        *((archive, "Content extension update must be newer and retain its permissions")
                          for archive in broader_messaging_archives),
                    ):
                        require_rejection(
                            launch(executable, root, origin, [
                                EXTENSION_REGISTRY_SWITCH, trust_flag,
                                *extension_profile_flags,
                                f"{EXTENSION_UPDATE_SWITCH}={invalid_update}",
                            ]),
                            expected_error,
                        )
                        if active_version_file.read_text(encoding="ascii") != "1.0" or (
                            candidate_version_dir.exists()
                        ):
                            raise SystemExit("A rejected update changed the active installation")
                    # Simulate interruption after publishing the candidate directory
                    # but before switching the active version.
                    candidate_version_dir.mkdir()
                    with ZipFile(update_archive) as payload:
                        for name in EXTENSION_PACKAGE_FILES:
                            path = candidate_version_dir / name
                            path.parent.mkdir(parents=True, exist_ok=True)
                            path.write_bytes(payload.read(name))
                    (candidate_version_dir / "fixture.crx").write_bytes(update_archive_bytes)
                    if server.requests != expected_requests:
                        raise SystemExit("A rejected extension package loaded a workspace tab")
                if options.extension_registry and run == 2:
                    for stale_archive in (source_archive, update_archive):
                        require_rejection(
                            launch(executable, root, origin, [
                                EXTENSION_REGISTRY_SWITCH, trust_flag,
                                *extension_profile_flags,
                                f"{EXTENSION_UPDATE_SWITCH}={stale_archive}",
                            ]),
                            "Could not update Content extension fixture",
                        )
                    if (
                        active_version_file.read_text(encoding="ascii") != "2.0"
                        or (candidate_version_dir / "fixture.crx").read_bytes() != update_archive_bytes
                        or server.requests != expected_requests
                    ):
                        raise SystemExit("A stale update changed the active installation")
                    shutil.rmtree(package)
            if options.extension_registry:
                expected_hosts = {f"{host}:{server.server_port}"
                                  for host in ("localhost", "127.0.0.1", "unapproved.example.com")}
                if not windows:
                    expected_hosts.add(f"child.unapproved.example.com:{server.server_port}")
                if set(server.extension_host_requests) != expected_hosts:
                    raise SystemExit(f"Unexpected extension host requests: {server.extension_host_requests}")
                print("Extension worker reported its grants and read only approved host responses")
            print("Workspace tabs and two isolated encrypted profiles survived restart")
        finally:
            server.shutdown()
            server_thread.join()


if __name__ == "__main__":
    main()
