#!/usr/bin/env python3
"""Verify macOS legacy Workspace data migration and rollback."""

import argparse
from contextlib import ExitStack, closing, contextmanager
from difflib import unified_diff
import ctypes
import base64
import hashlib
from http import cookies
from http.server import ThreadingHTTPServer
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import signal
import sqlite3
import subprocess
from tempfile import TemporaryDirectory, TemporaryFile
from threading import Thread
import time
from urllib.parse import parse_qs, urlsplit
from uuid import uuid4

from content_file_keychain_fixture import (
    compile_file_keychain_fixture, file_keychain_environment, seal_legacy_copy,
    shared_keychain_environments,
)
from package_app import BUNDLE_ID
from profile_migration_trial import (
    ASSISTANT_SESSIONS_FILE, COOKIE_FILES, HISTORY_FILES, LOCAL_STORAGE,
    copy_verified, import_workspace_profiles,
    inventory, prepare, prepare_macos_upgrade, rollback,
)
from verify_content_bundle import copy_dmg_app, loader_environment, verify_signature
from verify_content_extension_migration import run_legacy_profile_probe, seed_legacy_root
from verify_content_workspace_session import (
    ACTIVE_PATHS, MARKER, WorkspaceFixture, launch, seed_session, workspace_command,
)


PROFILES = {"work": "ZitterWork", "personal": "ZitterPersonal"}
KEYCHAIN = ("--zitter-content-workspace-keychain",)
MOCK_KEYCHAIN = (*KEYCHAIN, "--use-mock-keychain")
SESSION_FILES = {
    "keychain": "ContentKeychainWorkspaceSession.json",
    "mock-keychain": "ContentMockKeychainWorkspaceSession.json",
}
FILE_KEYCHAIN = "fixture.keychain-db"
KEYCHAIN_ITEM_CREATED = "Content file Keychain created its isolated password item"
COOKIE_GUARD_FAILURE = "The Content workspace could not verify its cookie stores"
TIMELINE_CLEAR_PROBE = "--zitter-content-timeline-clear-smoke-test"
TIMELINE_CLEAR_MARKER = "Zitter Content cleared Time Machine with the requested scope"
TIMELINE_PANEL_PROBE = "--zitter-content-timeline-panel-smoke-test"
TIMELINE_PANEL_MARKER = "Zitter Content restored and undid a layout from Time Machine"
TIMELINE_PROBE = "--zitter-content-timeline-smoke-test"
TIMELINE_MARKER = "Zitter Content reopened a Work tab from the workspace timeline"
TIMELINE_LEGACY_WORK = "/work/timeline-legacy"
TIMELINE_LEGACY_PERSONAL = "/personal/timeline-legacy"
TIMELINE_RECORDED_WORK = "/work/timeline-recorded"
FILTER_LISTS_PROBE = "--zitter-content-filter-lists-smoke-test"
FILTER_LISTS_MARKER = "Zitter Content applied, updated, and retained shared filter lists"
FILTER_LISTS_PAGE = "/filter-lists-page"
FILTER_TARGETS = ("/lists-stable", "/lists-new", "/lists-optional")
ADBLOCK_PROBE = "--zitter-content-profile-adblock-smoke-test"
ADBLOCK_MARKER = "Zitter Content applied separate migrated ad blocking settings"
ADBLOCK_PAGES = ("/work/adblock-profile", "/personal/adblock-profile")
ADBLOCK_TARGET = "/adblock-profile-target"
ADBLOCK_DIAGNOSTIC_QUERY = "?diagnostic-secret=fixture"
ADBLOCK_TRIAL_PAGE = "/work/adblock-trial"
ADBLOCK_COSMETIC_TRIAL_PAGE = "/work/adblock-cosmetic-trial"
ADBLOCK_COSMETIC_TRIAL_RULE = "127.0.0.1##.zitter-trial-sponsor"
ADBLOCK_TRIAL_TARGET = "/adblock-trial-target"
ADBLOCK_EDITOR_TARGET = "/adblock-editor-target"
ADBLOCK_HISTORY_404 = "/adblock-history-404"
ASSISTANT_CREDENTIAL_PROBE = "--zitter-content-assistant-credential-probe"
ASSISTANT_CREDENTIAL_MARKER = "Zitter Content Assistant credential probe passed"
ASSISTANT_PANEL_MARKER = "Zitter Content Assistant panel probe passed"
ASSISTANT_CREDENTIAL_FIXTURE = "zitter-assistant-credential-fixture"
ASSISTANT_RESEARCH = "space-25478a1f-59d2-4d17-a621-44de56e086cf"
ASSISTANT_SLEEPING = "space-d33877a6-6d03-4fd7-9a03-e30576a52277"
ASSISTANT_RESEARCH_PATH = "/assistant-research"
ASSISTANT_PIN_PATH = "/assistant-pin"
ASSISTANT_REDIRECT_PATH = "/assistant-redirect"
ASSISTANT_NO_CONTENT_PATH = "/assistant-no-content"
ASSISTANT_HARD_RELOAD_PATH = "/assistant-hard-reload"
ASSISTANT_AD_BLOCKED = "/assistant-ad-blocked"
ASSISTANT_AD_PUBLIC = "/assistant-ad-public"
ASSISTANT_RULE_TRIAL = "/assistant-rule-trial"
ASSISTANT_RULE_CONTROL = "/assistant-rule-control"
ASSISTANT_RULE_TARGET = "/assistant-rule-target"
ASSISTANT_DIAGNOSE_PAGE = "/assistant-diagnose"
ASSISTANT_DIAGNOSE_CHILD = "/assistant-diagnose-child"
ASSISTANT_DIAGNOSE_REPORT = "/assistant-diagnose-report"
ASSISTANT_OLD_SAVED_RULE = "||127.0.0.1/assistant-unused-old^$xmlhttprequest"
ASSISTANT_OLD_COSMETIC = "127.0.0.1##.replace-test-ad:has-text(Prior ad slot)"
ASSISTANT_EXEMPT_COSMETIC = "127.0.0.1##.exempt-test-ad"
ASSISTANT_SAVED_EXCEPTION = "127.0.0.1#@#.exempt-test-ad"
ASSISTANT_PREVIEW_REPORT = "/assistant-preview-report"
ASSISTANT_LOCAL_PIN = f"@pin:{ASSISTANT_RESEARCH}/research-pin"
ASSISTANT_SHARED_PIN = "@pin:shared/work-favorite"
ASSISTANT_SLEEPING_PATH = "/assistant-sleeping"
ASSISTANT_SPACES = {"work", ASSISTANT_RESEARCH, ASSISTANT_SLEEPING}
ASSISTANT_SETTINGS_FIXTURE = {
    "enabled": True, "backends": [True, True, False, True],
    "catalog_version": 2, "codex_path": "", "claude_path": "",
    "ollama_configured": True,
    "default_model": "openai/gpt-5.4", "background": False,
    "confirm_background": True, "concurrency": 3,
    "models": [{"provider": "openai", "id": "gpt-5.4", "visible": False},
               {"provider": "codex", "id": "workspace-fixture", "visible": True},
               {"provider": "ollama", "id": "panel-fixture", "visible": True}],
    "permissions": {"tabs": True, "pages": True, "actions": True,
                    "history": True, "web": True, "confirm_clicks": True},
}
ASSISTANT_SETTINGS_REOPENED = {
    **ASSISTANT_SETTINGS_FIXTURE,
    "catalog_version": 3, "default_model": "openai/gpt-6.1-sol",
    "models": [{"provider": "openai", "id": "gpt-6.1-sol", "visible": False},
               *ASSISTANT_SETTINGS_FIXTURE["models"][1:],
               {"provider": "openai", "id": "gpt-6.1-terra", "visible": True},
               {"provider": "openai", "id": "gpt-6-astra", "visible": True}],
}
ASSISTANT_HISTORY_FIXTURE = {
    "version": 1,
    "sessions": [
        {
            "key": f"legacy-{name}", "profile": f"{name}-profile",
            "title": f"Imported {name.title()} conversation", "updated": 1750000000000,
            "provider": "ollama", "model": "panel-fixture", "permissions": {"tabs": True},
            "messages": [{"kind": 1, "text": "An old question"},
                         {"kind": 0, "text": f"Imported {name.title()} reply"}],
        }
        for name in ("work", "personal")
    ],
    "tasks": [
        {"id": "legacy-work-task", "profile": "work-profile", "name": "Old task",
         "instruction": "List tabs", "provider": "ollama", "model": "panel-fixture",
         "active": True, "next_run": 1, "permissions": {"tabs": True}},
    ],
}
HISTORY_PROBE = "--zitter-content-history-panel-smoke-test=legacy"
HISTORY_MARKER = "Zitter Content history panel profile isolation passed"
SLEEP_PROBE = "--zitter-content-workspace-sleep-probe"
SLEEP_MARKER = "Zitter Content woke and released a migrated workspace"
SLEEP_DIALOG_MARKER = "Zitter Content honored both beforeunload responses during workspace sleep"
PINS_PROBE = "--zitter-content-workspace-pins-probe"
PINS_MARKER = "Zitter Content restored and reopened migrated pins"
PIN_CRASH_MARKER = b"Zitter Content committed pin changes before forced exit"
SPLITS = {"work": {"tabs": [0, 1], "weights": [0.25, 0.75], "vertical": False},
          "personal": {"tabs": [1, 0], "weights": [0.625, 0.375], "vertical": True}}


def filter_list_fixture(origin, targets=(), includes=()):
    rules = ["[Adblock Plus 2.0]", "! Title: Content test fixture"]
    rules.extend(f"||test-ad-{index}.invalid^" for index in range(1000))
    rules.extend(f"|{origin}{target}|$xmlhttprequest" for target in targets)
    rules.extend(f"!#include {name}" for name in includes)
    return ("\n".join(rules) + "\n").encode("utf-8")


def assistant_saved_network_rule(index):
    return f"||127.0.0.1/assistant-unused-save-{index}^$xmlhttprequest"


def assistant_saved_cosmetic_rule(index):
    return f"127.0.0.1##.save-test-ad-{index}"


def assistant_page_markup(tab, index):
    color = "#ffe1e1" if tab == "active" else "#c6dcff"
    return f"""<style>body {{ background: {color} }}</style><p>Public Assistant {tab}</p>
<span id="fixture-debug">Original debug text</span>
<div id="fixture-ad" class="ad-slot" style="width:320px;height:90px"><span>Sponsored</span></div>
<div class="ad-private-wrapper" style="width:320px;height:90px">
  <span data-private>assistant-hidden-ad-marker</span>
</div>
<div class="save-test-ad-{index}" style="width:320px;height:90px;outline:1px solid">Save rule fixture</div>
<div class="replace-test-ad save-test-ad-{index - 1}" style="width:320px;height:90px">Prior ad slot</div>
<div class="exempt-test-ad" style="width:320px;height:90px">Exception ad slot</div>
<button onclick="fetch('/assistant-click?trusted=' + event.isTrusted)">Visible action</button>
<form onsubmit="event.preventDefault(); reportKey(event)">
<input aria-label="Fixture input" id="fixture-input" onkeydown="reportKey(event)"
       oninput="document.querySelector('#fixture-other').type='password'">
<button type="submit" hidden>Submit</button>
</form>
<input aria-label="Fixture other" id="fixture-other" value="original">
<div role="textbox" aria-label="Fixture editor" id="fixture-editor" contenteditable="true">Original editor</div>
<select aria-label="Fixture selection" onchange="reportEdit(this)">
<option value="one">One</option><option value="two">Two</option>
</select>
<script>
console.log('fixture-console-{tab}');
if (location.pathname === '/work/active') {{
  const ad = document.querySelector('.save-test-ad-{index}');
  new MutationObserver(records => {{
    for (const record of records) {{
      if (!record.attributeName.startsWith('data-zitter-assistant-preview-')) continue;
      const shown = record.target.hasAttribute(record.attributeName);
      const query = new URLSearchParams({{
        target: record.target === ad,
        shown,
        inline: ad.style.outlineStyle,
        painted: getComputedStyle(ad).outlineStyle,
      }});
      fetch('{ASSISTANT_PREVIEW_REPORT}?' + query, {{keepalive: true}}).catch(() => {{}});
    }}
  }}).observe(document.body, {{subtree: true, attributes: true}});
  fetch('{ASSISTANT_AD_BLOCKED}?private=blocked-marker').catch(() => {{}});
  fetch('http://localhost:' + location.port + '{ASSISTANT_AD_PUBLIC}?private=resource-marker#fragment',
        {{mode: 'no-cors'}}).catch(() => {{}});
}}
function reportKey(event) {{
  const kind = event.type === 'submit' ? 'submit' : event.keyCode === 35 ? 'end'
    : event.keyCode === 87 ? 'reserved' : '';
  if (kind) fetch('/assistant-key?' + kind + '=' + event.isTrusted);
}}
function reportEdit(select) {{
  const query = new URLSearchParams({{
    input: document.querySelector('#fixture-input').value,
    editor: document.querySelector('#fixture-editor').textContent,
    protected: document.querySelector('#fixture-other').value === 'original',
    choice: select.value
  }});
  fetch('/assistant-edit?' + query);
  document.querySelector('#fixture-other').focus();
}}
</script>
<div data-private>private-assistant-marker</div>
<input type="password" aria-label="private-assistant-marker" value="private-assistant-marker">"""


class StorageFixture(WorkspaceFixture):
    def do_POST(self):
        if self.path != "/api/chat" or self.headers.get("Authorization"):
            self.send_error(400)
            return
        request = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        last = json.loads(request["messages"][-1]["content"])
        expected = {self.server.origin + "/work/active": "work",
                    self.server.origin + "/work/deferred": "work",
                    self.server.origin + ASSISTANT_RESEARCH_PATH: ASSISTANT_RESEARCH}
        if self.server.assistant_pass:
            expected[self.server.origin + ASSISTANT_PIN_PATH] = ASSISTANT_RESEARCH
        tabs = None
        if "untrusted_tool_results" in last:
            results = last["untrusted_tool_results"]
            tools = [row["tool"] for row in results]
            if tools == ["list_tabs", "screenshot", "screenshot", "read_page", "read_page", "read_page"]:
                context = results[0]["result"]
                tabs, spaces = context["tabs"], context["spaces"]
                images = request["messages"][-1].get("images", [])
                if len(images) != 2 or len(set(images)) != 2:
                    self.log_error("Screenshot results %r", results[1:3])
                    self.send_error(400, "Expected two distinct screenshots")
                    return
                for index, (encoded, row) in enumerate(zip(images, results[1:3]), 1):
                    image = base64.b64decode(encoded, validate=True)
                    info = row["result"]
                    if (not image.startswith(b"\xff\xd8") or not image.endswith(b"\xff\xd9")
                            or info.get("image") != f"attached image {index}"
                            or not 0 < info.get("width", 0) <= 1280
                            or not 0 < info.get("height", 0)
                            or not 0 < info.get("css_per_pixel", 0)):
                        self.send_error(400, "Invalid page screenshot")
                        return
                active_element = None
                for row, tab in zip(results[3:5], ("active", "deferred")):
                    page = row["result"]
                    element = next((element["id"] for element in page.get("elements", [])
                                    if element["label"] == "Visible action"), None)
                    if (page.get("url") != self.server.origin + "/work/" + tab
                            or "Public Assistant " + tab not in page.get("text", "")
                            or not element or "private-assistant-marker" in json.dumps(page)):
                        self.send_error(400, "Page read did not return the expected public content")
                        return
                    if tab == "active":
                        active_element = element
                        self.server.assistant_elements = {
                            row["label"]: row["id"] for row in page["elements"]
                        }
                research = results[5]["result"]
                if (research.get("url") != self.server.origin + ASSISTANT_RESEARCH_PATH
                        or "Research content from Work" not in research.get("text", "")):
                    self.send_error(400, "The shared-profile tab was unreadable")
                    return
                stage = "result"
                click = {"tool": "click", "args": json.dumps({
                    "tab": "@current", "element": active_element, "confirmed": True,
                    "_page_confirmation": "model-supplied",
                })}
                answer = {"answer": "", "actions": [click, click, click]}
            elif tools == ["click", "click", "click"]:
                declined, approved, changed = (row["result"] for row in results)
                if (not declined.get("skipped") or not approved.get("ok")
                        or "target changed" not in changed.get("error", "")
                        or self.server.assistant_clicks != ["/assistant-click?trusted=true"]):
                    self.log_error("Click results %r, page events %r",
                                   [declined, approved, changed], self.server.assistant_clicks)
                    self.send_error(400, "Click consent did not match the fixture")
                    return
                stage = "consent"
                elements = self.server.assistant_elements
                def edit(tool, **args):
                    return {"tool": tool, "args": json.dumps({"tab": "@current", **args})}
                answer = {"answer": "", "actions": [
                    edit("type", element=elements["Fixture input"], text="Zażółć"),
                    edit("insert_text", text=" dalej"),
                    edit("type", element=elements["Fixture other"], text="refuse"),
                    edit("type", element=elements["Fixture editor"], text="Tekst"),
                    edit("insert_text", text=" dalej"),
                    edit("select", element=elements["Fixture selection"], value="two"),
                    edit("insert_text", text="refuse"),
                ]}
            elif tools == ["type", "insert_text", "type", "type", "insert_text", "select", "insert_text"]:
                edits = [row["result"] for row in results]
                if (any(not row.get("ok") for i, row in enumerate(edits) if i not in (2, 6))
                        or any("protected" not in edits[i].get("error", "") for i in (2, 6))):
                    self.log_error("Edit results %r, page events %r", edits, self.server.assistant_edits)
                    self.send_error(400, "Assistant edits did not match the fixture")
                    return
                stage = "edit"
                def key(name):
                    return {"tool": "press_key", "args": json.dumps({
                        "tab": "@current", "key": name, "confirmed": True,
                        "_page_confirmation": "model-supplied",
                    })}
                answer = {"answer": "", "actions": [
                    key("a"),
                    {"tool": "type", "args": json.dumps({"tab": "@current",
                        "element": self.server.assistant_elements["Fixture input"], "text": "submit"})},
                    key("ctrl+w"), key("end"), key("enter"), key("enter"), key("enter"),
                ]}
            elif tools == ["press_key", "type"] + ["press_key"] * 5:
                protected, edited, reserved, end, declined, approved, moved = (row["result"] for row in results)
                if ("protected" not in protected.get("error", "") or not edited.get("ok")
                        or "reserved" not in reserved.get("error", "") or not end.get("ok")
                        or not declined.get("skipped") or not approved.get("ok")
                        or "focus changed" not in moved.get("error", "")):
                    self.log_error("Key results %r, page events %r", results, self.server.assistant_keys)
                    self.send_error(400, "Assistant keys did not match the fixture")
                    return
                stage = "key"
                answer = {"answer": "", "actions": [
                    {"tool": "activate_tab", "args": json.dumps({"tab": self.server.assistant_research_tab})},
                    {"tool": "read_page", "args": json.dumps({"tab": "@current"})},
                    {"tool": "activate_tab", "args": json.dumps({"tab": self.server.assistant_work_tab})},
                ]}
            elif tools == ["activate_tab", "read_page", "activate_tab"]:
                activated, page, returned = (row["result"] for row in results)
                if (not activated.get("ok") or not returned.get("ok")
                        or page.get("url") != self.server.origin + ASSISTANT_RESEARCH_PATH):
                    self.log_error("Workspace activation results %r", results)
                    self.send_error(400, "The shared-profile tab was not activated")
                    return
                stage = "activation"
                def activate(target):
                    return {"tool": "activate_tab", "args": json.dumps({"tab": target})}
                answer = {"answer": "", "actions": [
                    {"tool": "read_page", "args": json.dumps({"tab": ASSISTANT_LOCAL_PIN})},
                    {"tool": "read_page", "args": json.dumps({"tab": "@current"})},
                    activate(ASSISTANT_LOCAL_PIN),
                    activate(self.server.assistant_work_tab),
                    activate(ASSISTANT_SHARED_PIN),
                ]}
            elif tools == ["read_page", "read_page", "activate_tab", "activate_tab", "activate_tab"]:
                opened, page, reused, work, shared = (row["result"] for row in results)
                if (not all(row.get("ok") for row in (opened, reused, work, shared))
                        or not opened.get("tab") or reused.get("tab") != opened["tab"]
                        or shared.get("tab") != self.server.assistant_work_tab
                        or page.get("url") != self.server.origin + ASSISTANT_PIN_PATH
                        or "Pinned page from Work" not in page.get("text", "")):
                    self.log_error("Pin results %r", results)
                    self.send_error(400, "Assistant pins did not match the fixture")
                    return
                stage = "pins"
                def tab_action(tool, tab):
                    return {"tool": tool, "args": json.dumps({"tab": tab})}
                research_tab = self.server.assistant_research_tab
                answer = {"answer": "", "actions": [
                    tab_action("pin_tab", research_tab),
                    {"tool": "list_tabs", "args": "{}"},
                    tab_action("pin_tab", research_tab),
                    {"tool": "list_tabs", "args": "{}"},
                    tab_action("unpin_tab", research_tab),
                    {"tool": "list_tabs", "args": "{}"},
                    tab_action("activate_tab", self.server.assistant_work_tab),
                ]}
            elif tools == ["pin_tab", "list_tabs", "pin_tab", "list_tabs",
                           "unpin_tab", "list_tabs", "activate_tab"]:
                pinned, first, repeated, second, unpinned, final, returned = (
                    row["result"] for row in results
                )
                existing = {ASSISTANT_LOCAL_PIN, ASSISTANT_SHARED_PIN}
                first_pins = {row["mention"] for row in first["pins"]}
                second_pins = {row["mention"] for row in second["pins"]}
                final_pins = {row["mention"] for row in final["pins"]}
                added = [row for row in first["pins"] if row["mention"] not in existing]
                if (not all(row.get("ok") for row in (pinned, repeated, unpinned, returned))
                        or len(added) != 1
                        or added[0]["url"] != self.server.origin + ASSISTANT_RESEARCH_PATH
                        or not added[0]["mention"].startswith(f"@pin:{ASSISTANT_RESEARCH}/")
                        or first_pins != second_pins or len(second["pins"]) != len(first["pins"])
                        or final_pins != existing or len(final["pins"]) != len(existing)
                        or [row["id"] for row in final["spaces"] if row["current"]] != [ASSISTANT_RESEARCH]
                        or not any(row["mention"] == self.server.assistant_research_tab for row in final["tabs"])):
                    self.send_error(400, "Assistant pin changes did not match the fixture")
                    return
                stage = "pin_edits"
                answer = {"answer": "", "actions": [
                    {"tool": "open_tab", "args": json.dumps({"url": "file:///tmp/zitter"})},
                    {"tool": "open_tab", "args": json.dumps({
                        "url": self.server.origin + ASSISTANT_PIN_PATH,
                    })},
                ]}
            elif tools == ["open_tab", "open_tab"]:
                refused, opened = (row["result"] for row in results)
                if ("HTTP(S)" not in refused.get("error", "") or not opened.get("ok")
                        or opened.get("url") != self.server.origin + ASSISTANT_PIN_PATH
                        or opened.get("title") != "Pinned research"
                        or not opened.get("tab", "").startswith("@tab:")
                        or opened["tab"] == self.server.assistant_work_tab):
                    self.send_error(400, "Assistant open_tab did not match the fixture")
                    return
                self.server.assistant_opened_tab = opened["tab"]
                stage = "open_tab"
                answer = {"answer": "", "actions": [
                    {"tool": "navigate", "args": json.dumps({
                        "tab": opened["tab"],
                        "url": self.server.origin + ASSISTANT_REDIRECT_PATH,
                    })},
                    {"tool": "navigate", "args": json.dumps({
                        "tab": opened["tab"],
                        "url": self.server.origin + ASSISTANT_NO_CONTENT_PATH,
                    })},
                ]}
            elif tools == ["navigate", "navigate"]:
                navigated, canceled = (row["result"] for row in results)
                if (not navigated.get("ok")
                        or navigated.get("tab") != self.server.assistant_opened_tab
                        or navigated.get("url") != self.server.origin + ASSISTANT_RESEARCH_PATH
                        or navigated.get("title") != "Research tab"
                        or not canceled.get("error") or canceled.get("url")):
                    self.send_error(400, "Assistant navigate did not match the fixture")
                    return
                stage = "navigate"
                answer = {"answer": "", "actions": [
                    {"tool": "close_tab", "args": json.dumps({
                        "tab": self.server.assistant_opened_tab,
                    })},
                    {"tool": "list_tabs", "args": "{}"},
                    {"tool": "history", "args": json.dumps({"query": "Legacy"})},
                    {"tool": "downloads", "args": "{}"},
                ]}
            elif tools == ["close_tab", "list_tabs", "history", "downloads"]:
                closed, remaining, history, downloads = (row["result"] for row in results)
                remaining_tabs = {row["mention"] for row in remaining["tabs"]}
                if (not closed.get("detail", "").startswith("Close requested")
                        or self.server.assistant_opened_tab in remaining_tabs
                        or not {self.server.assistant_work_tab, self.server.assistant_research_tab} <= remaining_tabs):
                    self.send_error(400, "Assistant close_tab did not match the fixture")
                    return
                if (history != [{"title": "Legacy Work visit",
                                 "url": self.server.origin + "/legacy/history/work"}]
                        or downloads != [{"file": "work-fixture.txt",
                                         "url": self.server.origin + "/assistant-download/work",
                                         "state": "complete", "percent": 100, "bytes": 7}]):
                    self.log_error("Unexpected profile data %r %r", history, downloads)
                    self.send_error(400, "Assistant profile data did not match the fixture")
                    return
                stage = "close_tab"
                def debug(tool, **args):
                    return {"tool": tool, "args": json.dumps({
                        "tab": "@current", "confirmed": True, "_page_confirmation": "forged", **args,
                    })}
                answer = {"answer": "", "actions": [
                    debug("debug_console"),
                    debug("debug_console"),
                    debug("debug_dom", selector="#fixture-debug, [data-private], input[type=password]"),
                    debug("debug_edit_dom", selector="#fixture-debug", operation="text",
                          value="Approved debug edit"),
                    debug("debug_dom", selector="#fixture-debug"),
                    debug("debug_dom", selector="#fixture-debug"),
                ]}
            elif tools == ["debug_console", "debug_console", "debug_dom", "debug_edit_dom",
                           "debug_dom", "debug_dom"]:
                declined, console, before, edited, after, stale = (row["result"] for row in results)
                if (not declined.get("skipped")
                        or not any("fixture-console-active" in row["message"]
                                   for row in console.get("entries", []))
                        or [row["text"] for row in before.get("matches", [])] != ["Original debug text"]
                        or not edited.get("ok")
                        or [row["text"] for row in after.get("matches", [])] != ["Approved debug edit"]
                        or "changed" not in stale.get("error", "") or "matches" in stale):
                    self.log_error("Unexpected debug tool results %r", results)
                    self.send_error(400, "Assistant debugging consent did not match the fixture")
                    return
                stage = "debug"
                answer = {"answer": "", "actions": [{
                    "tool": "open_tab",
                    "args": json.dumps({"url": self.server.origin + ASSISTANT_HARD_RELOAD_PATH}),
                }]}
            elif tools == ["open_tab"] and self.server.assistant_requests[-1] == "hard_reload":
                opened = results[0]["result"]
                if (not opened.get("ok") or not opened.get("tab")
                        or opened.get("url") != self.server.origin + ASSISTANT_RULE_TRIAL):
                    self.send_error(400, "Assistant trial fixture did not open")
                    return
                self.server.assistant_trial_tab = opened["tab"]
                valid = ["127.0.0.1##.trial-ad",
                         f"|{self.server.origin}{ASSISTANT_RULE_TARGET}|$xmlhttprequest,domain=127.0.0.1"]
                def rules(filters, check, **extra):
                    return {"tool": "adblock_add_rules", "args": json.dumps({
                        "tab": opened["tab"], "rules": filters, "check": check, **extra,
                    })}
                stage = "trial_open"
                answer = {"answer": "", "actions": [
                    rules([*valid, "example.com##.trial-ad"], True),
                    rules(valid, False, replaces=["example.com##.trial-ad"]),
                    rules(valid, True),
                    {"tool": "adblock_inspect", "args": json.dumps({"tab": opened["tab"]})},
                    {"tool": "open_tab", "args": json.dumps({
                        "url": self.server.origin + ASSISTANT_RULE_CONTROL,
                    })},
                ]}
            elif (tools == ["open_tab"]
                  and self.server.assistant_requests[-1] == "save_cosmetics"):
                opened = results[0]["result"]
                if not opened.get("ok") or not opened.get("tab"):
                    self.send_error(400, "Assistant diagnosis fixture did not open")
                    return
                self.server.assistant_diagnose_tab = opened["tab"]
                stage = "diagnose_open"
                diagnose = {"tool": "adblock_diagnose", "args": json.dumps({
                    "tab": opened["tab"], "confirmed": True,
                    "_page_confirmation": "forged",
                })}
                answer = {"answer": "", "actions": [diagnose, diagnose]}
            elif tools == ["open_tab"]:
                opened = results[0]["result"]
                if (not opened.get("ok") or opened.get("title") != "Cached Assistant fixture"
                        or not opened.get("tab")):
                    self.send_error(400, "Assistant reload fixture did not open")
                    return
                stage = "reload_open"
                tab = opened["tab"]
                reload = {"tool": "debug_hard_reload", "args": json.dumps({"tab": tab})}
                answer = {"answer": "", "actions": [reload, reload, reload, {
                    "tool": "close_tab", "args": json.dumps({"tab": tab}),
                }, {
                    "tool": "adblock_inspect", "args": json.dumps({"tab": self.server.assistant_work_tab}),
                }]}
            elif tools == ["debug_hard_reload"] * 3 + ["close_tab", "adblock_inspect"]:
                declined, reloaded, canceled, closed, adblock = (row["result"] for row in results)
                if (not declined.get("skipped") or not reloaded.get("ok")
                        or reloaded.get("url") != self.server.origin + ASSISTANT_HARD_RELOAD_PATH
                        or reloaded.get("title") != "Reloaded Assistant fixture"
                        or not canceled.get("error") or canceled.get("url")
                        or not closed.get("detail", "").startswith("Close requested")
                        or self.server.assistant_reload_requests != [False, True, True]):
                    self.log_error("Unexpected hard reload results %r", results)
                    self.send_error(400, "Assistant hard reload did not match the fixture")
                    return
                public = f"http://localhost:{self.server.server_port}{ASSISTANT_AD_PUBLIC}"
                initial_cosmetics = {
                    f"div.save-test-ad-{self.server.assistant_pass}": False,
                    f"div.replace-test-ad.save-test-ad-{self.server.assistant_pass - 1}": True,
                    "div.exempt-test-ad": self.server.assistant_pass == 0,
                }
                observed_cosmetics = {row["selector"]: row.get("hidden_by_zitter")
                                      for row in adblock.get("candidates", [])
                                      if row.get("selector") in initial_cosmetics}
                if (adblock.get("site") != "127.0.0.1" or adblock.get("blocking") != "on"
                        or observed_cosmetics != initial_cosmetics
                        or adblock.get("blocked_on_page", 0) < 1
                        or self.server.origin + ASSISTANT_AD_BLOCKED not in adblock.get("blocked_requests", [])
                        or not any(row.get("selector") == "div#fixture-ad"
                                   and row.get("rule_hint", "").startswith("127.0.0.1##")
                                   for row in adblock.get("candidates", []))
                        or public not in {row["url"] for row in adblock.get("requests", [])}
                        or any(marker in json.dumps(adblock)
                               for marker in ("assistant-hidden-ad-marker", "blocked-marker", "resource-marker"))
                        or self.server.assistant_blocked_hits):
                    self.log_error("Unexpected ad blocking inspection %r", adblock)
                    self.send_error(400, "Assistant ad blocking inspection did not match the fixture")
                    return
                stage = "hard_reload"
                answer = {"answer": "", "actions": [{
                    "tool": "open_tab", "args": json.dumps({
                        "url": self.server.origin + ASSISTANT_RULE_TRIAL,
                    }),
                }]}
            elif tools == ["adblock_add_rules"] * 3 + ["adblock_inspect", "open_tab"]:
                rejected, save, trial, inspected, control = (row["result"] for row in results)
                if (not rejected.get("error") or not save.get("error")
                        or not trial.get("checked") or not control.get("ok")
                        or self.server.origin + ASSISTANT_RULE_TARGET not in inspected.get("blocked_requests", [])
                        or not any(row.get("selector") == "div.trial-ad" and row.get("hidden_by_zitter")
                                   for row in inspected.get("candidates", []))):
                    self.log_error("Unexpected ad blocking trial results %r", results)
                    self.send_error(400, "Assistant trial did not block both fixture targets")
                    return
                stage = "trial"
                answer = {"answer": "", "actions": [
                    {"tool": "adblock_inspect", "args": json.dumps({"tab": control["tab"]})},
                    {"tool": "close_tab", "args": json.dumps({"tab": control["tab"]})},
                    {"tool": "close_tab", "args": json.dumps({"tab": self.server.assistant_trial_tab})},
                ]}
            elif tools == ["adblock_inspect", "close_tab", "close_tab"]:
                control, closed_control, closed_trial = (row["result"] for row in results)
                if (control.get("blocked_on_page") != 0
                        or not any(row.get("selector") == "div.trial-ad" and not row.get("hidden_by_zitter")
                                   for row in control.get("candidates", []))
                        or not all(row.get("detail", "").startswith("Close requested")
                                   for row in (closed_control, closed_trial))
                        or self.server.assistant_trial_targets != [ASSISTANT_RULE_TRIAL, ASSISTANT_RULE_CONTROL]
                        or self.server.assistant_trial_pages != [
                            ASSISTANT_RULE_TRIAL, ASSISTANT_RULE_TRIAL, ASSISTANT_RULE_CONTROL,
                        ]):
                    self.log_error("Unexpected control tab result %r", results)
                    self.send_error(400, "Assistant trial reached another tab or reloaded unexpectedly")
                    return
                stage = "trial_control"
                previous = self.server.assistant_pass - 1
                replaces = ([ASSISTANT_OLD_SAVED_RULE, ASSISTANT_OLD_COSMETIC] if previous < 0 else
                            [assistant_saved_network_rule(previous), assistant_saved_cosmetic_rule(previous),
                             ASSISTANT_SAVED_EXCEPTION])
                save = {"tool": "adblock_add_rules", "args": json.dumps({
                    "tab": self.server.assistant_work_tab,
                    "rules": [assistant_saved_network_rule(self.server.assistant_pass),
                              assistant_saved_cosmetic_rule(self.server.assistant_pass),
                              ASSISTANT_SAVED_EXCEPTION],
                    "replaces": replaces,
                    "confirmed": True,
                    "_page_confirmation": "forged",
                })}
                answer = {"answer": "", "actions": [
                    {"tool": "adblock_add_rules", "args": json.dumps({
                        "tab": self.server.assistant_work_tab,
                        "rules": ["127.0.0.1##.ad-private-wrapper"],
                    })},
                    save, save, save,
                ]}
            elif tools == ["adblock_add_rules"] * 4:
                protected, declined, stale, saved = (row["result"] for row in results)
                if ("protected" not in protected.get("error", "")
                        or not declined.get("skipped")
                        or "changed" not in stale.get("error", "")
                        or not saved.get("ok") or saved.get("saved") != 3
                        or saved.get("replaced") != (3 if self.server.assistant_pass else 2)):
                    self.log_error("Unexpected rule save results %r", results)
                    self.send_error(400, "Assistant rule saving did not honor consent")
                    return
                stage = "save_rules"
                answer = {"answer": "", "actions": [{
                    "tool": "adblock_inspect", "args": json.dumps({
                        "tab": self.server.assistant_work_tab,
                    }),
                }]}
            elif tools == ["adblock_inspect"] and self.server.assistant_requests[-1] == "save_rules":
                report = results[0]["result"]
                expected = {
                    f"div.save-test-ad-{self.server.assistant_pass}": True,
                    f"div.replace-test-ad.save-test-ad-{self.server.assistant_pass - 1}": False,
                    "div.exempt-test-ad": False,
                }
                observed = {row["selector"]: row.get("hidden_by_zitter")
                            for row in report.get("candidates", [])
                            if row.get("selector") in expected}
                if observed != expected:
                    self.log_error("Unexpected live cosmetic filtering %r", report)
                    self.send_error(400, "Saved cosmetic rules did not reconcile the loaded page")
                    return
                stage = "save_cosmetics"
                answer = {"answer": "", "actions": [{
                    "tool": "open_tab", "args": json.dumps({
                        "url": self.server.origin + ASSISTANT_DIAGNOSE_PAGE,
                    }),
                }]}
            elif tools == ["adblock_diagnose"] * 2:
                denied, report = (row["result"] for row in results)
                if (not denied.get("skipped") or report.get("site") != "127.0.0.1"
                        or not any(row.get("read") == "offsetHeight"
                                   and row.get("element") == "div#ad-bait"
                                   for row in report.get("bait_reads", []))
                        or not any(row.get("name") == "adblockDetected"
                                   and row.get("access") == "write"
                                   for row in report.get("globals", []))):
                    self.log_error("Unexpected diagnosis results %r", results)
                    self.send_error(400, "Assistant diagnosis missed the first inline script")
                    return
                stage = "diagnosed"
                target = json.dumps({"tab": self.server.assistant_diagnose_tab})
                answer = {"answer": "", "actions": [
                    {"tool": "debug_hard_reload", "args": target},
                    {"tool": "close_tab", "args": target},
                ]}
            elif tools == ["debug_hard_reload", "close_tab"]:
                reloaded, closed = (row["result"] for row in results)
                if (not reloaded.get("ok")
                        or not closed.get("detail", "").startswith("Close requested")):
                    self.send_error(400, "Assistant diagnosis control reload failed")
                    return
                stage = "diagnose_control"
                answer = {"answer": "Content Assistant round trip passed", "actions": []}
            else:
                self.send_error(400, "Unexpected Assistant tools")
                return
        else:
            context = last["untrusted_browser_context"]
            tabs, spaces = context["tabs"], context["spaces"]
            stage = "context"
            deferred = next(tab for tab in tabs if tab["url"] == self.server.origin + "/work/deferred")
            self.server.assistant_work_tab = next(tab["mention"] for tab in tabs if tab["current"])
            research = next(tab for tab in tabs if tab["url"] == self.server.origin + ASSISTANT_RESEARCH_PATH)
            self.server.assistant_research_tab = research["mention"]
            answer = {"answer": "", "actions": [
                {"tool": "list_tabs", "args": "{}"},
                {"tool": "screenshot", "args": json.dumps({"tab": "@current"})},
                {"tool": "screenshot", "args": json.dumps({"tab": research["mention"]})},
                {"tool": "read_page", "args": json.dumps({"tab": "@current"})},
                {"tool": "read_page", "args": json.dumps({"tab": deferred["mention"]})},
                {"tool": "read_page", "args": json.dumps({"tab": research["mention"]})},
            ]}
        if (request["model"] != "panel-fixture" or
                (tabs is not None and ({tab["url"]: tab["space"] for tab in tabs} != expected
                                       or len(tabs) != len(expected)
                                       or sorted(pin["mention"] for pin in context["pins"])
                                           != sorted((ASSISTANT_LOCAL_PIN, ASSISTANT_SHARED_PIN))
                                       or {space["id"] for space in spaces} != ASSISTANT_SPACES
                                       or [space["id"] for space in spaces if space["current"]] != ["work"]))):
            self.send_error(400)
            return
        self.server.assistant_requests.append(stage)
        # Keep the focused composer disabled through a normal caret blink.
        time.sleep(0.65)
        content = json.dumps(answer)
        chunks = [content]
        if stage == "diagnose_control":
            split = content.index("round trip")
            chunks = [content[:split], content[split:]]
        frames = [
            json.dumps({"message": {"content": chunk}, "done": index == len(chunks) - 1}).encode() + b"\n"
            for index, chunk in enumerate(chunks)
        ]
        self.send_response(200)
        self.send_header("Content-Type", "application/x-ndjson")
        self.send_header("Content-Length", str(sum(map(len, frames))))
        self.end_headers()
        for index, frame in enumerate(frames):
            if index:
                time.sleep(0.75)
            self.wfile.write(frame)
            self.wfile.flush()

    def do_GET(self):
        assistant_path = urlsplit(self.path).path
        if self.server.assistant_fixture and assistant_path == ASSISTANT_PREVIEW_REPORT:
            self.server.assistant_preview_observations.append(urlsplit(self.path).query)
            self.send_response(204)
            self.end_headers()
            return
        if self.server.assistant_fixture and assistant_path == ASSISTANT_DIAGNOSE_REPORT:
            self.server.assistant_diagnose_observations.append(urlsplit(self.path).query)
            self.send_response(204)
            self.end_headers()
            return
        if self.server.assistant_fixture and assistant_path in (ASSISTANT_DIAGNOSE_PAGE,
                                                                 ASSISTANT_DIAGNOSE_CHILD):
            main = assistant_path == ASSISTANT_DIAGNOSE_PAGE
            kind = "main" if main else "child"
            child = f'<iframe src="{ASSISTANT_DIAGNOSE_CHILD}"></iframe>' if main else ""
            self.send_page(f"""<!doctype html><title>Assistant diagnosis</title>
<div id="ad-bait"></div>
<script>
const armed = typeof Object.getOwnPropertyDescriptor(window, 'adblockDetected')?.set === 'function';
window.adblockDetected = true;
void document.getElementById('ad-bait').offsetHeight;
const report = new Image();
report.src = '{ASSISTANT_DIAGNOSE_REPORT}?kind={kind}&armed=' + armed;
document.body.append(report);
</script>
{child}""".encode())
            return
        if self.server.assistant_fixture and assistant_path in (ASSISTANT_RULE_TRIAL, ASSISTANT_RULE_CONTROL):
            self.server.assistant_trial_pages.append(assistant_path)
            self.send_page(f"""<!doctype html><title>Assistant rule trial</title>
<div class="trial-ad" style="width:320px;height:90px" aria-label="Ad slot">Trial fixture</div>
<script>fetch({json.dumps(ASSISTANT_RULE_TARGET)}).catch(() => {{}});</script>""".encode())
            return
        if self.server.assistant_fixture and assistant_path == ASSISTANT_RULE_TARGET:
            self.server.assistant_trial_targets.append(urlsplit(self.headers.get("Referer", "")).path)
            self.send_response(204)
            self.end_headers()
            return
        if self.server.assistant_fixture and assistant_path in (ASSISTANT_AD_BLOCKED, ASSISTANT_AD_PUBLIC):
            if assistant_path == ASSISTANT_AD_BLOCKED:
                self.server.assistant_blocked_hits += 1
            self.send_response(204)
            self.end_headers()
            return
        if self.server.assistant_fixture and assistant_path == ASSISTANT_HARD_RELOAD_PATH:
            bypass = "no-cache" in self.headers.get("Cache-Control", "")
            self.server.assistant_reload_requests.append(bypass)
            if self.server.assistant_reload_requests.count(True) == 2:
                self.send_response(204)
                self.end_headers()
                return
            title = b"Reloaded Assistant fixture" if bypass else b"Cached Assistant fixture"
            self.send_page(b"<!doctype html><title>" + title + b"</title>",
                           cache_control="public, max-age=600")
            return
        if self.server.assistant_fixture and assistant_path == ASSISTANT_REDIRECT_PATH:
            self.send_response(302)
            self.send_header("Location", ASSISTANT_RESEARCH_PATH + "?private=fixture#fragment")
            self.end_headers()
            return
        if self.server.assistant_fixture and assistant_path == ASSISTANT_NO_CONTENT_PATH:
            self.send_response(204)
            self.end_headers()
            return
        if self.server.assistant_fixture and assistant_path in (ASSISTANT_RESEARCH_PATH, ASSISTANT_PIN_PATH):
            cookie = cookies.SimpleCookie(self.headers.get("Cookie", "")).get("profile_probe")
            if not cookie or cookie.value != "work":
                self.send_error(403)
                return
            if assistant_path == ASSISTANT_PIN_PATH:
                self.send_page(b"<!doctype html><title>Pinned research</title>"
                               b"<body>Pinned page from Work</body>")
            else:
                self.send_page(b"<!doctype html><title>Research tab</title>"
                               b"<body style='background:#c6dcff'>Research content from Work</body>")
            return
        if self.server.assistant_fixture and self.path.startswith("/assistant-click?"):
            self.server.assistant_clicks.append(self.path)
            self.send_response(204)
            self.end_headers()
            return
        if self.server.assistant_fixture and self.path.startswith("/assistant-edit?"):
            self.server.assistant_edits.append(parse_qs(urlsplit(self.path).query))
            self.send_response(204)
            self.end_headers()
            return
        if self.server.assistant_fixture and self.path.startswith("/assistant-key?"):
            self.server.assistant_keys.append(urlsplit(self.path).query)
            self.send_response(204)
            self.end_headers()
            return
        if self.path.startswith("/migration-result?"):
            self.server.observations.append(parse_qs(urlsplit(self.path).query))
            self.send_response(204)
            self.end_headers()
            return
        if self.path in ("/work/pin", "/work/pin-edited"):
            self.server.requests.append((self.path, ""))
            title = (b"Zitter pinned Work fixture" if self.path == "/work/pin"
                     else b"Zitter edited Work pin fixture")
            self.send_page(b"<!doctype html><title>" + title + b"</title>")
            return
        if self.path.startswith("/adblock-lists/"):
            name = self.path.rsplit("/", 1)[-1]
            self.server.list_requests.append((name, self.headers.get("Cookie", ""),
                                              self.headers.get("Authorization", ""),
                                              self.headers.get("Referer", "")))
            if self.server.bad_list_update and name == "optional-part.txt":
                self.send_error(404)
            elif self.server.bad_list_update and name == "easylist.txt":
                self.send_page(b"not a filter list")
            elif self.server.bad_list_update and name == "easyprivacy.txt":
                self.send_response(302)
                self.send_header("Location", "/adblock-lists/unexpected.txt")
                self.end_headers()
            elif name == "easylist.txt":
                self.send_page(filter_list_fixture(self.server.origin, FILTER_TARGETS[:2]))
            elif name == "ublock.txt":
                self.send_page(filter_list_fixture(self.server.origin,
                                                   includes=("optional-part.txt",)))
            elif name == "optional-part.txt":
                self.send_page(f"|{self.server.origin}{FILTER_TARGETS[2]}|$xmlhttprequest\n".encode())
            elif name == "easyprivacy.txt":
                body = filter_list_fixture(self.server.origin)
                # A complete list can exceed Chromium's bounded string limit.
                self.send_page(body + (b"!" + b" " * 1022 + b"\n") * 6144)
            else:
                self.send_error(404)
            return
        if self.path in (FILTER_LISTS_PAGE, *FILTER_TARGETS):
            parsed = cookies.SimpleCookie(self.headers.get("Cookie", ""))
            entry = parsed.get("profile_probe")
            self.server.requests.append((self.path, entry.value if entry else ""))
            if self.path != FILTER_LISTS_PAGE:
                self.send_page(b"allowed")
                return
            self.send_page(f"""<!doctype html><script>
Promise.all({json.dumps(FILTER_TARGETS)}.map(
  url => fetch(url).then(() => '0', () => '1')))
  .then(bits => document.title = 'List probe ' + bits.join(''));
</script>""".encode())
            return
        if self.path == ADBLOCK_HISTORY_404:
            parsed = cookies.SimpleCookie(self.headers.get("Cookie", ""))
            entry = parsed.get("profile_probe")
            self.server.requests.append((self.path, entry.value if entry else ""))
            body = b"<!doctype html><title>Profile 404 fixture</title>"
            self.send_response(404)
            self.send_header("Content-Type", "text/html")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        adblock_path = urlsplit(self.path).path
        if adblock_path in (ADBLOCK_TRIAL_PAGE, ADBLOCK_TRIAL_TARGET, ADBLOCK_COSMETIC_TRIAL_PAGE):
            parsed = cookies.SimpleCookie(self.headers.get("Cookie", ""))
            entry = parsed.get("profile_probe")
            self.server.requests.append((adblock_path, entry.value if entry else ""))
            if adblock_path == ADBLOCK_TRIAL_TARGET:
                self.send_page(b"allowed")
            elif adblock_path == ADBLOCK_COSMETIC_TRIAL_PAGE:
                self.send_page(b"""<!doctype html><div class="zitter-trial-sponsor">Sponsored</div>
<script>
const sponsor = document.querySelector('.zitter-trial-sponsor');
setTimeout(() => {
  document.title = getComputedStyle(sponsor).display === 'none'
    ? 'Adblock trial blocked' : 'Adblock trial allowed';
}, 250);
</script>""")
            else:
                self.send_page(f"""<!doctype html><script>
fetch({json.dumps(ADBLOCK_TRIAL_TARGET)}, {{cache: 'no-store'}}).then(
  () => document.title = 'Adblock trial allowed',
  () => document.title = 'Adblock trial blocked');
</script>""".encode())
            return
        if adblock_path in (*ADBLOCK_PAGES, ADBLOCK_TARGET, ADBLOCK_EDITOR_TARGET):
            parsed = cookies.SimpleCookie(self.headers.get("Cookie", ""))
            entry = parsed.get("profile_probe")
            self.server.requests.append((adblock_path, entry.value if entry else ""))
            if adblock_path not in ADBLOCK_PAGES:
                self.send_page(b"allowed")
                return
            self.send_page(f"""<!doctype html><script>
const scriptlet = window.zitterProfileAdblock === true;
document.addEventListener('DOMContentLoaded', async () => {{
  const blocked = await Promise.all(
    {json.dumps([ADBLOCK_TARGET + ADBLOCK_DIAGNOSTIC_QUERY + "#private", ADBLOCK_EDITOR_TARGET])}.map(
      url => fetch(url).then(() => false, () => true)));
  setTimeout(() => {{
    const hidden = getComputedStyle(document.querySelector('.profile-ad')).display === 'none';
    document.title = scriptlet && hidden && blocked.every(Boolean)
      ? 'Profile adblock filtered'
      : !scriptlet && !hidden && blocked.every(value => !value)
      ? 'Profile adblock bypassed'
      : 'Profile adblock mixed: ' + JSON.stringify([scriptlet, hidden, blocked]);
  }}, 150);
}});
</script><div class="profile-ad">Sponsored</div>""".encode())
            return
        if self.path in (TIMELINE_LEGACY_WORK, TIMELINE_LEGACY_PERSONAL,
                          TIMELINE_RECORDED_WORK):
            parsed = cookies.SimpleCookie(self.headers.get("Cookie", ""))
            entry = parsed.get("profile_probe")
            self.server.requests.append((self.path, entry.value if entry else ""))
            self.send_page(b"<!doctype html><title>Workspace timeline fixture</title>")
            return
        if self.path == "/legacy/work":
            self.send_page(b"<!doctype html><title>Legacy storage fixture</title>")
            return
        if self.path in ("/legacy/history/work", "/legacy/history/personal"):
            name = self.path.rsplit("/", 1)[-1].title()
            self.send_page(f"<!doctype html><title>Legacy {name} visit</title>".encode())
            return
        if self.path == "/favicon.ico":
            self.send_response(204)
            self.end_headers()
            return
        if self.server.assistant_fixture and self.path == "/work/deferred":
            self.send_page(("<!doctype html><title>Deferred Work</title>"
                            + assistant_page_markup("deferred", self.server.assistant_pass)).encode())
            return
        if self.server.split_fixture and self.path in ("/personal/deferred", "/work/deferred"):
            profile = self.path.split("/")[1]
            self.server.requests.append((self.path, ""))
            self.send_page(f"<!doctype html><title>Deferred {profile.title()}</title>".encode())
            return
        if self.path not in ACTIVE_PATHS:
            self.server.requests.append((self.path, ""))
            self.send_error(404)
            return

        profile = self.path.split("/")[1]
        parsed = cookies.SimpleCookie(self.headers.get("Cookie", ""))
        entry = parsed.get("profile_probe")
        actual = entry.value if entry else ""
        self.server.requests.append((self.path, actual))
        beforeunload = ""
        if self.server.sleep_before_unload and profile == "work":
            beforeunload = "addEventListener('beforeunload', event => event.preventDefault());"
        self.send_page(f"""<!doctype html><meta charset=utf-8><script>
{beforeunload}
const expected = {json.dumps(profile)};
const received = {json.dumps(actual)};
const hidden = !document.cookie.split('; ').some(value => value.startsWith('profile_probe='));
navigator.permissions.query({{name: 'clipboard-read'}}).then(async permission => {{
  const clipboard = expected === 'work' ? 'granted' : 'denied';
  const storage = localStorage.getItem('profile_probe');
  await fetch('/migration-result?' + new URLSearchParams({{
    profile: expected, cookie: received, hidden, storage, clipboard: permission.state,
  }}));
  document.title = received === expected && hidden && permission.state === clipboard &&
      storage === expected
      ? 'Zitter workspace probe ' + expected : 'Zitter workspace storage or Clipboard mismatch';
}}, () => document.title = 'Zitter workspace Clipboard query failed');
</script>{assistant_page_markup("active", self.server.assistant_pass) if self.server.assistant_fixture else ""}""".encode())


def cookie_database(profile):
    paths = [profile / name for name in ("Cookies", "Network/Cookies")]
    present = [path for path in paths if path.is_file()]
    if len(present) != 1:
        raise SystemExit(f"Expected one cookie database for {profile.name}")
    return present[0]


def cookie_ciphertext(profile):
    database = cookie_database(profile)
    with closing(sqlite3.connect(f"{database.as_uri()}?mode=ro", uri=True)) as db:
        rows = db.execute(
            "SELECT value, encrypted_value, is_httponly, is_persistent FROM cookies "
            "WHERE name = ?", ("profile_probe",),
        ).fetchall()
    if (len(rows) != 1 or rows[0][0] != "" or not isinstance(rows[0][1], bytes)
            or not rows[0][1].startswith(b"v10")
            or rows[0][2:] != (1, 1)):
        raise SystemExit("Expected one v10 encrypted, persistent HttpOnly cookie")
    return rows[0][1]


def seed_personal_stores(source, destination):
    directory = cookie_database(source).parent
    target = destination / directory.relative_to(source)
    target.mkdir(exist_ok=True)
    for name in COOKIE_FILES:
        if (directory / name).is_file():
            shutil.copy2(directory / name, target / name)
    storage = source / LOCAL_STORAGE
    copy_verified(storage, destination / LOCAL_STORAGE, inventory(storage))
    for name in HISTORY_FILES:
        if (source / name).is_file():
            shutil.copy2(source / name, destination / name)


def legacy_visits(profile, url):
    database = profile / "History"
    with closing(sqlite3.connect(database.as_uri() + "?mode=ro", uri=True)) as db:
        return db.execute(
            "SELECT visits.visit_time, visits.transition, urls.title FROM visits "
            "JOIN urls ON visits.url = urls.id WHERE urls.url = ? ORDER BY visits.visit_time",
            (url,),
        ).fetchall()


def expected_session(origin, split_fixture, pin_fixture=False, sleep_fixture=False):
    session = seed_session(origin)
    if sleep_fixture:
        session["work"]["sleeping"] = True
    if split_fixture:
        for name, split in SPLITS.items():
            session[name]["split"] = split
    if pin_fixture:
        session["work"]["pins"] = [
            {"id": "folder-work", "title": "Work folder", "url": "",
             "parent_id": "", "folder": True, "profile_pin": False},
            {"id": "open-work", "title": "Selected Work pin", "url": origin + "/work/active",
             "parent_id": "folder-work", "folder": False, "profile_pin": False},
            {"id": "closed-work", "title": "Closed Work pin", "url": origin + "/work/pin",
             "parent_id": "folder-work", "folder": False, "profile_pin": False},
        ]
        session["personal"]["pins"] = [
            {"id": "favorite-personal", "title": "Personal favorite",
             "url": origin + "/personal/active", "parent_id": "",
             "folder": False, "profile_pin": True},
        ]
        session["work"]["tabs"][1]["pin"] = "open-work"
        session["personal"]["tabs"][0]["pin"] = "favorite-personal"
    return session


def add_assistant_workspaces(session, origin, *, pin_open=False):
    def pin(identifier, title, url, *, shared=False, folder=False):
        return {"id": identifier, "parent_id": "", "title": title, "url": url,
                "folder": folder, "profile_pin": shared}

    session["profile_favorites"] = {
        "work": [pin("work-favorite", "Work favorite", origin + "/work/active", shared=True)],
    }
    session["work"]["tabs"][1]["pin"] = "work-favorite"
    session["personal"]["pins"] = [
        pin("personal-assistant", "Personal pin", origin + "/personal/active"),
    ]
    session["extra_workspaces"] = [
        {"id": ASSISTANT_RESEARCH, "name": "Research"},
        {"id": ASSISTANT_SLEEPING, "name": "Archive"},
    ]
    session[ASSISTANT_RESEARCH] = {
        "profile_id": "work", "active": 0,
        "tabs": [{"url": origin + ASSISTANT_RESEARCH_PATH, "title": "Research tab"}],
        "pins": [
            pin("research-folder", "Research folder", "", folder=True),
            pin("research-pin", "Pinned research", origin + ASSISTANT_PIN_PATH),
        ],
    }
    session[ASSISTANT_SLEEPING] = {
        "profile_id": "work", "active": 0, "sleeping": True,
        "tabs": [{"url": origin + ASSISTANT_SLEEPING_PATH, "title": "Archive tab"}],
        "pins": [pin("archive-pin", "Archive pin", origin + ASSISTANT_SLEEPING_PATH)],
    }
    if pin_open:
        session[ASSISTANT_RESEARCH]["active"] = 1
        session[ASSISTANT_RESEARCH]["tabs"].append({
            "url": origin + ASSISTANT_PIN_PATH, "title": "Pinned research", "pin": "research-pin",
        })


def prepare_workspace(trial, root, origin, split_fixture, layout, pin_fixture=False,
                      sleep_fixture=False, assistant_ciphertext=None):
    import_workspace_profiles(trial, root, layout)
    verify_imported_workspace(root, origin, split_fixture, layout, pin_fixture,
                              sleep_fixture, assistant_ciphertext)


def verify_imported_workspace(root, origin, split_fixture, layout, pin_fixture,
                              sleep_fixture, assistant_ciphertext):
    expected = expected_session(origin, split_fixture, pin_fixture, sleep_fixture)
    if assistant_ciphertext:
        expected["assistant"] = dict(ASSISTANT_SETTINGS_FIXTURE,
                                     encrypted_api_key=assistant_ciphertext)
    path = root / SESSION_FILES[layout]
    if json.loads(path.read_text(encoding="utf-8")) != expected:
        raise SystemExit("Imported session changed the active workspace or tab layout")
    if assistant_ciphertext:
        add_assistant_workspaces(expected, origin)
        path.write_text(json.dumps(expected), encoding="utf-8")


def seed_assistant_downloads(root, layout, origin):
    # Chromium History timestamps are microseconds since the Windows epoch.
    now = int((time.time() + 11644473600) * 1_000_000)
    for profile in PROFILES:
        filename = root / f"{profile}-fixture.txt"
        filename.write_bytes(b"fixture")
        with closing(sqlite3.connect(root / f"{layout}-{profile}" / "History")) as db, db:
            db.execute("""
                INSERT INTO downloads (
                    id, guid, current_path, target_path, start_time, received_bytes,
                    total_bytes, state, danger_type, interrupt_reason, hash,
                    end_time, opened, last_access_time, transient, referrer,
                    site_url, embedder_download_data, tab_url, tab_referrer_url,
                    http_method, by_ext_id, by_ext_name, by_web_app_id,
                    etag, last_modified, mime_type, original_mime_type)
                VALUES (1, ?, ?, ?, ?, 7, 7, 1, 0, 0, X'', ?, 0, ?, 0, '',
                        '', '', '', '', 'GET', '', '', '', '', '', 'text/plain', 'text/plain')
            """, (str(uuid4()), str(filename), str(filename), now, now, now))
            db.execute("INSERT INTO downloads_url_chains VALUES (1, 0, ?)",
                       (f"{origin}/assistant-download/{profile}?private=fixture#fragment",))


def verify_reopened_session(root, origin, split_fixture, layout, pin_fixture=False,
                            edited_pin=False, sleep_fixture=False,
                            assistant_ciphertext=None):
    expected = expected_session(origin, split_fixture, pin_fixture, sleep_fixture)
    if assistant_ciphertext:
        expected["assistant"] = dict(ASSISTANT_SETTINGS_REOPENED,
                                     encrypted_api_key=assistant_ciphertext,
                                     shortcut_access={})
        add_assistant_workspaces(expected, origin, pin_open=True)
    if edited_pin:
        expected["work"]["pins"][0]["title"] = "Work folder (edited)"
        expected["work"]["pins"][2]["url"] = origin + "/work/pin-edited"
    for name in PROFILES:
        entry = expected[name]
        entry["tabs"][entry["active"]]["title"] = f"Zitter workspace probe {name}"
    if pin_fixture:
        expected["profile_favorites"] = {"personal": expected["personal"].pop("pins")}
    actual = json.loads((root / SESSION_FILES[layout]).read_text(encoding="utf-8"))
    if actual != expected:
        diff = "".join(unified_diff(
            json.dumps(expected, indent=2, sort_keys=True).splitlines(keepends=True),
            json.dumps(actual, indent=2, sort_keys=True).splitlines(keepends=True),
            fromfile="expected session", tofile="saved session",
        ))
        raise SystemExit(f"Content changed the restored session:\n{diff}")


def set_legacy_active(root, workspace):
    path = root / "ZitterWorkspaces.json"
    config = json.loads(path.read_text(encoding="utf-8"))
    config["active"] = workspace
    path.write_text(json.dumps(config), encoding="utf-8")


def seed_legacy_session(root, origin, split_fixture, pin_fixture, sleep_fixture):
    expected = expected_session(origin, False, pin_fixture, sleep_fixture)
    path = root / "ZitterWorkspaces.json"
    config = json.loads(path.read_text(encoding="utf-8"))
    config["active"] = expected.pop("active_workspace")
    # Exercise an older configuration unless the Assistant fixture is seeded.
    config.pop("assistant", None)
    if pin_fixture:
        for entry in config["workspaces"]:
            entry["pins"] = [
                {key: value for key, value in pin.items() if key != "profile_pin"}
                for pin in expected[entry["id"]].get("pins", []) if not pin["profile_pin"]
            ]
        for entry in config["profiles"]:
            workspace = entry["id"].removesuffix("-profile")
            entry["pins"] = [
                {key: pin[key] for key in ("id", "title", "url")}
                for pin in expected[workspace].get("pins", []) if pin["profile_pin"]
            ]
    path.write_text(json.dumps(config), encoding="utf-8")
    for name, entry in expected.items():
        entry.pop("pins", None)
        entry.update(profile_id=f"{name}-profile", split=[], weights=[])
    if split_fixture:
        expected["work"].pop("split")
        expected["work"].pop("weights")
        expected["work"].update(left=0, right=1, ratio=0.25)
        expected["personal"].update(split=[1, 0], weights=[0.625, 0.375], vertical=True)
    (root / "ZitterSession.json").write_text(json.dumps(expected), encoding="utf-8")


def assistant_credential_fixture(openssl):
    key = hashlib.pbkdf2_hmac("sha1", b"mock_password", b"saltysalt", 1003, 16)
    result = subprocess.run(
        [openssl, "enc", "-aes-128-cbc", "-K", key.hex(), "-iv", (b" " * 16).hex()],
        input=ASSISTANT_CREDENTIAL_FIXTURE.encode("ascii"), capture_output=True, check=True,
    )
    return base64.b64encode(b"v10" + result.stdout).decode("ascii")


def replace_cookie_key(openssl, profile, key):
    with closing(sqlite3.connect(cookie_database(profile))) as db, db:
        host, = db.execute(
            "SELECT host_key FROM cookies WHERE name = ?", ("profile_probe",),
        ).fetchone()
        plaintext = hashlib.sha256(host.encode()).digest() + b"personal"
        encrypted = subprocess.run(
            [openssl, "enc", "-aes-128-cbc", "-K", key.hex(), "-iv", (b" " * 16).hex()],
            input=plaintext, capture_output=True, check=True, timeout=10,
        ).stdout
        db.execute("UPDATE cookies SET encrypted_value = ? WHERE name = ?",
                   (b"v10" + encrypted, "profile_probe"))


def interrupt_after_pin_save(executable, root, origin, flags, env):
    command = workspace_command(executable, root, origin, flags)
    with TemporaryFile() as output, TemporaryFile() as errors:
        process = subprocess.Popen(command, stdout=output, stderr=errors, env=env,
                                   start_new_session=True)
        deadline = time.monotonic() + 60
        acknowledged = False
        try:
            while process.poll() is None and time.monotonic() < deadline:
                if PIN_CRASH_MARKER in os.pread(output.fileno(), 65536, 0):
                    acknowledged = True
                    break
                time.sleep(0.025)
        finally:
            if process.poll() is None:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            process.wait(timeout=10)
        stdout = os.pread(output.fileno(), 65536, 0).decode("utf-8", "replace")
        stderr = os.pread(errors.fileno(), 65536, 0).decode("utf-8", "replace")
        if (not acknowledged or process.returncode != -signal.SIGKILL
                or PINS_MARKER not in stdout or MARKER in stdout):
            raise SystemExit(f"Could not interrupt an acknowledged pin save:\n{stdout}\n{stderr}")
        print("Killed Content after the pin snapshot committed and before normal shutdown")
        return stdout


def verify_update_publisher(app, team):
    requirement = (
        'anchor apple generic and certificate leaf[field.1.2.840.113635.100.6.1.13] exists'
        f' and certificate leaf[subject.OU] = "{team}" and identifier "{BUNDLE_ID}"'
    )
    result = subprocess.run(
        ["/usr/bin/codesign", "--verify", "--deep", "--strict", f"-R={requirement}", app],
        capture_output=True, text=True, timeout=60,
    )
    if result.returncode:
        raise SystemExit(f"Expected the installed Zitter Developer ID publisher on {app}:\n"
                         f"{result.stderr}")


def copy_update_trial_bundles(legacy_app, content_app, directory):
    for app in (legacy_app, content_app):
        with (app / "Contents/Info.plist").open("rb") as stream:
            if plistlib.load(stream).get("CFBundleIdentifier") != BUNDLE_ID:
                raise SystemExit("The replacement trial requires the production identifier on both bundles")
    details = subprocess.run(
        ["/usr/bin/codesign", "--display", "--verbose=4", legacy_app],
        capture_output=True, text=True, check=True, timeout=60,
    )
    team = re.search(r"^TeamIdentifier=([A-Z0-9]{10})$", details.stderr, re.MULTILINE)
    if not team:
        raise SystemExit("The installed Zitter app has no Developer ID team")
    for app in (legacy_app, content_app):
        verify_update_publisher(app, team[1])
    installed = directory / "Zitter.app"
    incoming = directory / "Incoming.app"
    for source, copy in ((legacy_app, installed), (content_app, incoming)):
        subprocess.run(["/usr/bin/ditto", source, copy], check=True, timeout=90)
        verify_update_publisher(copy, team[1])
    return installed, incoming


def swap_trial_applications(installed, staged):
    # Both bundles live in the same private installation directory.
    rename_swap = 0x00000002  # Darwin RENAME_SWAP.
    rename = ctypes.CDLL(None, use_errno=True).renamex_np
    rename.argtypes = (ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint)
    rename.restype = ctypes.c_int
    if rename(os.fsencode(installed), os.fsencode(staged), rename_swap) != 0:
        error = ctypes.get_errno()
        raise OSError(error, os.strerror(error), installed)


@contextmanager
def temporary_trial_replacement(installed, incoming):
    swap_trial_applications(installed, incoming)
    try:
        verify_signature(installed)
        yield
    finally:
        swap_trial_applications(installed, incoming)
        verify_signature(installed)


def verify_replacement_launchservices(installed):
    with TemporaryDirectory(prefix="zitter-content-launchservices-") as directory:
        output = Path(directory) / "stdout"
        errors = Path(directory) / "stderr"
        output.touch()
        errors.touch()
        subprocess.run([
            "/usr/bin/open", "-n", "-g", "-W", "-a", installed,
            "--stdout", output, "--stderr", errors, "--args",
            "--zitter-content-workspace-smoke-test",
        ], check=True, timeout=60)
        stdout = output.read_text(encoding="utf-8", errors="replace").splitlines()
        if ("Zitter Content production identity isolated Workspace trial" not in stdout
                or "Zitter Content workspace smoke passed" not in stdout):
            stderr = errors.read_text(encoding="utf-8", errors="replace")
            raise SystemExit(f"LaunchServices did not start the replacement bundle.\n{stdout!r}\n{stderr}")
        print("LaunchServices opened the signed replacement at its temporary installation path")


def verify_ordinary_prepared_startup(content, root, server, env):
    server.requests.clear()
    server.observations.clear()
    expected = {
        "profile": ["personal"], "cookie": ["personal"], "hidden": ["true"],
        "storage": ["personal"], "clipboard": ["denied"],
    }
    trial_env = dict(env, ZITTER_CONTENT_STARTUP_TRIAL_ROOT=str(root))
    with TemporaryFile() as output:
        process = subprocess.Popen([content], env=trial_env, stdout=output, stderr=output)
        try:
            deadline = time.monotonic() + 15
            while process.poll() is None and time.monotonic() < deadline:
                if ("/personal/active", "personal") in server.requests and expected in server.observations:
                    print("Argument-free Content startup opened the prepared Personal profile")
                    return
                time.sleep(0.05)
            output.seek(0)
            raise SystemExit(f"Ordinary startup did not open the prepared profile:\n"
                             f"{output.read().decode(errors='replace')}")
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("legacy_app", type=Path, help="Installed legacy macOS Zitter.app")
    parser.add_argument("content_app", type=Path, help="Local Zitter Content Lab.app or a DMG for --replace-app")
    parser.add_argument("--sleep-workspace", action="store_true",
                        help="Import a sleeping workspace, then wake and suspend it after loading")
    parser.add_argument("--sleep-before-unload", action="store_true",
                        help="Exercise declining and accepting a page's sleep confirmation")
    timeline = parser.add_mutually_exclusive_group()
    timeline.add_argument("--timeline-clear", action="store_true",
                          help="Clear by profile and period, then exercise global clear and undo")
    timeline.add_argument("--timeline-panel", action="store_true",
                          help="Restore and undo a layout, then save a snapshot from the panel flow")
    timeline.add_argument("--timeline", action="store_true",
                        help="Reopen a migrated tab, then reopen a new closure after restart")
    parser.add_argument("--filter-lists", action="store_true",
                        help="Verify a migrated filter cache, refresh and failed refresh")
    parser.add_argument("--assistant-settings", action="store_true",
                        help="Verify offline Assistant settings import and restart")
    parser.add_argument("--adblock", action="store_true",
                        help="Verify separate migrated ad blocking settings and edits across restart")
    parser.add_argument("--initial-appearance",
                        choices=("light-compositor", "dark-compositor", "light-metal", "dark-metal"),
                        help="Choose the appearance for the interactive ad blocking trial")
    parser.add_argument("--pins", action="store_true", help="Restore and reopen migrated pins")
    parser.add_argument("--edit-pins", action="store_true",
                        help="Exercise pin mutations and persist an updated target")
    parser.add_argument("--crash-pins", action="store_true",
                        help="Kill Content after committing pin edits, then verify recovery")
    parser.add_argument("--split", action="store_true", help="Restore horizontal and vertical splits")
    parser.add_argument("--appearance", choices=("light", "dark"),
                        help="Check imported settings in the basic Workspace fixture")
    parser.add_argument("--ordinary-startup", action="store_true",
                        help="Also start the production-identity candidate without command-line arguments")
    parser.add_argument("--single-personal-startup", action="store_true",
                        help="Repeat ordinary startup with a Personal-only offline catalogue")
    parser.add_argument("--prepared-root", action="store_true",
                        help="Open the imported profile through the explicit prepared Workspace path")
    parser.add_argument("--replace-app", action="store_true",
                        help="Rehearse replacement and rollback in a disposable installation directory")
    keychain = parser.add_mutually_exclusive_group()
    keychain.add_argument("--file-keychain", action="store_true",
                          help="Open copied stores with real macOS Keychain APIs in a private file")
    keychain.add_argument("--shared-keychain", action="store_true",
                          help="Use one private file Keychain in both the legacy and Content apps")
    args = parser.parse_args()
    if args.appearance and any((args.timeline, args.timeline_clear, args.timeline_panel,
                               args.adblock, args.filter_lists, args.pins, args.edit_pins,
                               args.crash_pins, args.sleep_workspace, args.sleep_before_unload,
                               args.assistant_settings)):
        parser.error("--appearance uses the basic Workspace fixture")
    if args.assistant_settings and (
            not (args.shared_keychain or args.file_keychain)
            or any((args.filter_lists, args.adblock, args.timeline, args.timeline_panel,
                    args.timeline_clear, args.split, args.pins, args.edit_pins,
                    args.crash_pins, args.sleep_workspace, args.sleep_before_unload))):
        parser.error("--assistant-settings requires a file Keychain and the basic workspace fixture")
    if args.ordinary_startup and not args.shared_keychain:
        parser.error("--ordinary-startup requires a shared private Keychain")
    if args.single_personal_startup and not args.ordinary_startup:
        parser.error("--single-personal-startup requires --ordinary-startup")
    if args.initial_appearance and not args.adblock:
        parser.error("--initial-appearance requires --adblock")
    args.edit_pins = args.edit_pins or args.crash_pins
    args.pins = args.pins or args.edit_pins or args.timeline_panel
    args.split = args.split or args.timeline_panel
    args.sleep_workspace = args.sleep_workspace or args.sleep_before_unload
    if args.sleep_workspace and args.edit_pins:
        parser.error("--sleep-workspace cannot be combined with pin edits")
    if args.timeline_panel and (args.sleep_workspace or args.edit_pins):
        parser.error("--timeline-panel cannot be combined with sleep or pin edits")
    if (args.timeline or args.timeline_clear) and (args.split or args.pins or args.sleep_workspace):
        parser.error("--timeline and --timeline-clear use the basic workspace layout fixture")
    if (args.adblock or args.filter_lists) and (
            (args.adblock and args.filter_lists) or
            args.timeline or args.timeline_panel or args.timeline_clear
                         or args.split or args.pins or args.sleep_workspace):
        parser.error("--adblock and --filter-lists each use the basic workspace layout fixture")
    real_keychain = args.file_keychain or args.shared_keychain
    if args.replace_app and real_keychain:
        parser.error("--replace-app uses the mock Keychain because the file fixture grants access by executable path")
    layout = "keychain" if real_keychain else "mock-keychain"
    flags = KEYCHAIN if real_keychain else MOCK_KEYCHAIN
    legacy = (args.legacy_app / "Contents/MacOS/Chromium").resolve()
    content_app = args.content_app.resolve()
    disk_image = content_app.is_file() and content_app.suffix.lower() == ".dmg"
    if disk_image and not args.replace_app:
        parser.error("A DMG input requires --replace-app")
    node = shutil.which("node")
    openssl = shutil.which("openssl")
    if not legacy.is_file() or not node or not openssl:
        parser.error("Expected the legacy macOS app, Node.js, and OpenSSL")

    assistant_ciphertext = assistant_credential_fixture(openssl) if args.assistant_settings else None
    with ExitStack() as fixtures:
        def scratch(prefix):
            return Path(fixtures.enter_context(TemporaryDirectory(prefix=prefix))).resolve()

        if disk_image:
            extraction = scratch("zitter-content-image-trial-")
            image = content_app
            content_app = extraction / "Zitter Content Lab.app"
            copy_dmg_app(image, extraction / "Mounted Image", content_app)
        content = content_app / "Contents/MacOS/Zitter Content Lab"
        if not content.is_file():
            parser.error(f"Content executable not found: {content}")
        if args.replace_app:
            installed_app, incoming_app = copy_update_trial_bundles(
                args.legacy_app, content_app, scratch("zitter-content-installation-"))
            legacy = installed_app / "Contents/MacOS/Chromium"

        source = scratch("zitter-content-legacy-")
        personal_source = scratch("zitter-content-legacy-")
        restored = scratch("zitter-content-legacy-")
        restored.rmdir()
        content_root = scratch("zitter-content-probe-")
        trial = scratch("zitter-content-storage-migration-") / "trial"
        library = (compile_file_keychain_fixture(scratch("zitter-content-keychain-library-"))
                   if real_keychain else None)
        legacy_env = (dict(os.environ, BREAKPAD_DUMP_LOCATION=str(scratch("zitter-content-crashpad-")))
                      if args.replace_app else None)
        shared_root = None
        if args.shared_keychain:
            shared_root = scratch("zitter-content-shared-keychain-")
            legacy = seal_legacy_copy(args.legacy_app, scratch("zitter-content-legacy-app-"))
            content_env, legacy_env = shared_keychain_environments(library, shared_root, content)
            if assistant_ciphertext:
                legacy_env["ZITTER_CONTENT_TEST_SEED_LEGACY_KEY"] = "1"
        server = fixtures.enter_context(ThreadingHTTPServer(("127.0.0.1", 0), StorageFixture))
        server.requests = []
        server.assistant_requests = []
        server.assistant_clicks = []
        server.assistant_edits = []
        server.assistant_keys = []
        server.assistant_fixture = bool(assistant_ciphertext)
        server.assistant_blocked_hits = 0
        server.observations = []
        server.split_fixture = args.split
        server.sleep_before_unload = args.sleep_before_unload
        origin = f"http://127.0.0.1:{server.server_port}"
        server.origin = origin
        server.list_requests = []
        server.bad_list_update = False
        url = origin + "/legacy/work"
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()
        fixtures.callback(thread.join)
        fixtures.callback(server.shutdown)

        rejected = subprocess.run(
            [str(content), "--zitter-content-workspace",
             f"--zitter-content-workspace-root={content_root}", *MOCK_KEYCHAIN],
            capture_output=True, timeout=20,
        )
        if rejected.returncode != 1 or any(content_root.iterdir()):
            raise SystemExit("Mock Keychain must require a loopback probe before touching the profile")
        if library:
            rejected = launch(content, content_root, origin, KEYCHAIN, env=loader_environment())
            if rejected.returncode != 1 or any(content_root.iterdir()):
                raise SystemExit("Real Keychain loopback probe started without the isolated fixture")
        content_root.rmdir()

        for profile, root in (("work", source), ("personal", personal_source)):
            visit_url = f"{origin}/legacy/history/{profile}"
            seed_legacy_root(root, visit_url)
            run_legacy_profile_probe(node, legacy, root, "-", "-", visit_url, f"seed-storage-{profile}", env=legacy_env)
            decision = "allow" if profile == "work" else "block"
            run_legacy_profile_probe(node, legacy, root, "-", "-", visit_url, f"seed-clipboard-{decision}", env=legacy_env)
        seed_personal_stores(personal_source / "ZitterWork", source / "ZitterPersonal")
        shutil.copy2(personal_source / "ZitterWork/Preferences",
                     source / "ZitterPersonal/Preferences")
        seed_legacy_session(source, origin, args.split, args.pins, args.sleep_workspace)
        if args.filter_lists:
            cache = source / "Zitter Filter Lists"
            cache.mkdir(exist_ok=True)
            (cache / "easylist.txt").write_bytes(filter_list_fixture(origin, FILTER_TARGETS[:1]))
            for name, directory in PROFILES.items():
                path = source / directory / "Preferences"
                prefs = json.loads(path.read_text(encoding="utf-8"))
                prefs.setdefault("zitter", {}).update({
                    "ad_blocking": True,
                    "ad_blocking_lists": {"antiadblock": False, "ublock": name == "work"},
                })
                path.write_text(json.dumps(prefs), encoding="utf-8")
        if args.adblock:
            for name, directory in PROFILES.items():
                path = source / directory / "Preferences"
                prefs = json.loads(path.read_text(encoding="utf-8"))
                prefs.setdefault("zitter", {}).update({
                    "ad_blocking": True,
                    "ad_blocking_allowed_sites": ["127.0.0.1"] if name == "personal" else [],
                    "ad_blocking_lists": {"zitter": False, "ublock": True},
                    "ad_blocking_rules": [
                        f"|{origin}{ADBLOCK_TARGET}^$xmlhttprequest",
                        "127.0.0.1##.profile-ad",
                        "127.0.0.1##+js(set-constant, zitterProfileAdblock, true)",
                    ],
                })
                path.write_text(json.dumps(prefs), encoding="utf-8")
        if assistant_ciphertext:
            path = source / PROFILES["work"] / "Preferences"
            prefs = json.loads(path.read_text(encoding="utf-8"))
            prefs.setdefault("zitter", {}).update({
                "ad_blocking": True,
                "ad_blocking_rules": [f"|{origin}{ASSISTANT_AD_BLOCKED}^$xmlhttprequest",
                                      ASSISTANT_OLD_SAVED_RULE, ASSISTANT_OLD_COSMETIC,
                                      ASSISTANT_EXEMPT_COSMETIC],
            })
            path.write_text(json.dumps(prefs), encoding="utf-8")
        if args.timeline or args.timeline_panel or args.timeline_clear:
            config = json.loads((source / "ZitterWorkspaces.json").read_text(encoding="utf-8"))
            profiles = {space["id"]: space["profile_id"] for space in config["workspaces"]}
            (source / "ZitterAppearance.json").write_text(
                json.dumps({"time_machine": True, "time_machine_days": 90}),
                encoding="utf-8",
            )
            recent = int(time.time() * 1000) - 60_000
            now = recent - 45 * 24 * 3600 * 1000
            frames = []
            for name in PROFILES:
                base = {"w": name, "p": profiles[name], "a": 0}
                current = [origin + f"/{name}/active", f"Zitter workspace probe {name}"]
                old = [origin + f"/{name}/timeline-legacy", "Workspace timeline fixture"]
                earlier = [old] if args.timeline_panel else [current, old]
                frames.extend([{**base, "t": now - 120_000, "tabs": earlier},
                               {**base, "t": now - 60_000, "tabs": [current]}])
                if args.timeline_clear:
                    frames.extend([
                        {"w": name, "t": now - 60_000, "m": name.title() + " older"},
                        {**base, "t": recent, "tabs": [current]},
                        {"w": name, "t": recent, "m": name.title() + " recent"},
                    ])
            (source / "ZitterTimeline.jsonl").write_text(
                "".join(json.dumps(frame) + "\n" for frame in frames), encoding="utf-8",
            )
        if args.appearance:
            (source / "ZitterAppearance.json").write_text(json.dumps({
                "theme": args.appearance, "sidebar_pinned": False, "search_engine": "bing",
                "work": "#345678", "workspace_emoji": {"work": "★"},
            }), encoding="utf-8")
        if assistant_ciphertext:
            path = source / "ZitterWorkspaces.json"
            config = json.loads(path.read_text(encoding="utf-8"))
            config["assistant"] = dict(ASSISTANT_SETTINGS_FIXTURE,
                                       encrypted_api_key=assistant_ciphertext)
            path.write_text(json.dumps(config), encoding="utf-8")
            (source / ASSISTANT_SESSIONS_FILE).write_text(
                json.dumps(ASSISTANT_HISTORY_FIXTURE), encoding="utf-8",
            )
        ciphertext = {name: cookie_ciphertext(source / directory)
                      for name, directory in PROFILES.items()}
        history = {name: legacy_visits(source / directory, f"{origin}/legacy/history/{name}")
                   for name, directory in PROFILES.items()}
        if any(not visits or {row[2] for row in visits} != {f"Legacy {name.title()} visit"}
               for name, visits in history.items()):
            raise SystemExit(f"The legacy browser did not persist both history fixtures: {history!r}")
        if ciphertext["work"] == ciphertext["personal"]:
            raise SystemExit("Legacy profiles unexpectedly have the same cookie ciphertext")
        original = inventory(source)
        rehearses_upgrade = args.replace_app and args.prepared_root
        if rehearses_upgrade:
            profile_count = prepare_macos_upgrade(source, trial, content_root, mock_keychain=True)
            verify_imported_workspace(content_root, origin, args.split, layout, args.pins,
                                      args.sleep_workspace, assistant_ciphertext)
        else:
            profile_count = prepare(source, trial)
        if profile_count != len(PROFILES):
            raise SystemExit("The snapshot lost a legacy profile")

        wrong_keys = (None,) if library else ((b"unrelated-fixture-key", 1003), (b"", 1))
        for wrong_key in wrong_keys:
            wrong_key_root = scratch("zitter-content-probe-")
            wrong_key_root.rmdir()
            prepare_workspace(trial, wrong_key_root, origin, args.split, layout, args.pins,
                              args.sleep_workspace, assistant_ciphertext)
            if wrong_key:
                secret, iterations = wrong_key
                key = hashlib.pbkdf2_hmac("sha1", secret, b"saltysalt", iterations, 16)
                replace_cookie_key(openssl, wrong_key_root / f"{layout}-personal", key)
            before = {name: inventory(wrong_key_root / f"{layout}-{name}") for name in PROFILES}
            server.requests.clear()
            env = file_keychain_environment(library) if library else None
            rejected = launch(content, wrong_key_root, origin, flags, env=env)
            if (rejected.returncode != 1 or COOKIE_GUARD_FAILURE not in rejected.stderr
                    or MARKER in rejected.stdout or server.requests):
                raise SystemExit(f"Content failed to reject an unrelated cookie key\n{rejected.stderr}")
            if library and KEYCHAIN_ITEM_CREATED not in rejected.stdout:
                raise SystemExit("The file Keychain failed to create the unrelated password item")
            for name, expected in before.items():
                if inventory(wrong_key_root / f"{layout}-{name}") != expected:
                    raise SystemExit(f"The refused launch changed the {name} profile")

        if not rehearses_upgrade:
            prepare_workspace(trial, content_root, origin, args.split, layout, args.pins,
                              args.sleep_workspace, assistant_ciphertext)
        if args.replace_app:
            replacement = fixtures.enter_context(ExitStack())
            replacement.enter_context(temporary_trial_replacement(installed_app, incoming_app))
            verify_replacement_launchservices(installed_app)
            content = installed_app / "Contents/MacOS/Zitter Content Lab"
        if not shared_root:
            content_env = file_keychain_environment(library, seed_legacy_key=True) if library else None
        if assistant_ciphertext:
            seed_assistant_downloads(content_root, layout, origin)
            content_env["ZITTER_OLLAMA_URL"] = origin
            content_env["ZITTER_CODEX_PATH"] = str(content_root / "disabled-codex")
            content_env["ZITTER_CLAUDE_PATH"] = str(content_root / "disabled-claude")
            content_env.pop("OPENAI_API_KEY", None)
            content_env.pop("ZITTER_OPENAI_API_KEY", None)
        open_flags = (*flags, "--zitter-content-workspace-legacy-keychain") if shared_root else flags
        if args.prepared_root:
            open_flags += ("--zitter-content-open-prepared-workspace",)
            marker = content_root / "ContentWorkspaceLayout"
            original_marker = marker.read_bytes()
            try:
                marker.write_bytes(b"zitter-content-workspace-v1 windows-dpapi\n")
                server.requests.clear()
                rejected = launch(content, content_root, origin, open_flags, env=content_env)
                if (rejected.returncode != 1 or "Invalid prepared Content workspace" not in rejected.stderr
                        or server.requests or (content_root / "WorkspaceLock").exists()):
                    raise SystemExit(f"Prepared Workspace did not reject the wrong layout before locking:\n{rejected.stderr}")
            finally:
                marker.write_bytes(original_marker)
            session = content_root / SESSION_FILES[layout]
            displaced = session.with_suffix(".offline")
            session.rename(displaced)
            try:
                rejected = launch(content, content_root, origin, open_flags, env=content_env)
                if (rejected.returncode != 1 or "Invalid prepared Content workspace" not in rejected.stderr
                        or server.requests or (content_root / "WorkspaceLock").exists()):
                    raise SystemExit(f"Prepared Workspace did not reject the missing session before locking:\n{rejected.stderr}")
            finally:
                displaced.rename(session)
            print("Content rejected the wrong prepared Workspace layout and missing session before locking")
        appearances = [("light", "compositor"), ("dark", "metal")]
        if (args.pins or args.sleep_workspace or args.timeline_clear
                or args.adblock or args.assistant_settings):
            appearances.extend([("dark", "compositor"), ("light", "metal")])
        if args.initial_appearance:
            first = tuple(args.initial_appearance.split("-"))
            appearances.remove(first)
            appearances.insert(0, first)
        for index, (theme, renderer) in enumerate(appearances):
            server.requests.clear()
            server.observations.clear()
            server.assistant_requests.clear()
            server.assistant_pass = index
            server.assistant_clicks.clear()
            server.assistant_edits.clear()
            server.assistant_keys.clear()
            server.assistant_reload_requests = []
            server.assistant_trial_pages = []
            server.assistant_trial_targets = []
            server.assistant_diagnose_observations = []
            server.assistant_preview_observations = []
            server.assistant_elements = {}
            appearance = (f"--zitter-rust-raster={renderer}",
                          f"--zitter-content-workspace-theme={theme}")
            if args.appearance:
                appearance = (f"--zitter-rust-raster={renderer}",
                              f"--zitter-content-imported-appearance-probe={args.appearance}")
            if assistant_ciphertext:
                probe, marker = ASSISTANT_CREDENTIAL_PROBE, ASSISTANT_PANEL_MARKER
            elif args.filter_lists:
                mode = "restart" if index else "import"
                probe, marker = f"{FILTER_LISTS_PROBE}={mode}", FILTER_LISTS_MARKER
                server.list_requests.clear()
                server.bad_list_update = index != 0
            elif args.adblock:
                mode = "restart" if index else "import"
                probe, marker = f"{ADBLOCK_PROBE}={mode}", ADBLOCK_MARKER
            elif args.timeline_clear:
                mode = ("profile", "undo", "global", "restart")[index]
                probe, marker = f"{TIMELINE_CLEAR_PROBE}={mode}", TIMELINE_CLEAR_MARKER
            elif args.timeline_panel:
                probe, marker = TIMELINE_PANEL_PROBE, TIMELINE_PANEL_MARKER
            elif args.timeline:
                mode = "import" if index == 0 else "restart"
                probe, marker = f"{TIMELINE_PROBE}={mode}", TIMELINE_MARKER
            elif args.sleep_workspace:
                probe, marker = SLEEP_PROBE, SLEEP_MARKER
                if args.sleep_before_unload:
                    probe += "=before-unload"
                    marker = SLEEP_DIALOG_MARKER
            else:
                probe, marker = (PINS_PROBE, PINS_MARKER) if args.pins else (HISTORY_PROBE, HISTORY_MARKER)
            crash = args.crash_pins and index == 0
            if args.edit_pins and index == 0:
                probe += "=crash" if crash else "=edit"
            probe_flags = (*open_flags, probe, *appearance)
            if crash:
                stdout = interrupt_after_pin_save(
                    content, content_root, origin, probe_flags, content_env,
                )
            else:
                result = launch(content, content_root, origin, probe_flags, env=content_env)
                if assistant_ciphertext and any(
                        ASSISTANT_CREDENTIAL_FIXTURE in stream
                        for stream in (result.stdout, result.stderr)):
                    raise SystemExit("The Assistant probe logged its credential")
                native_timeline_recovery = "Zitter Content used the native menu for timeline recovery"
                if (result.returncode or MARKER not in result.stdout
                        or marker not in result.stdout
                        or (args.appearance and "Zitter Content applied the imported Workspace appearance"
                            not in result.stdout.splitlines())
                        or (args.timeline and index == 1
                            and native_timeline_recovery not in result.stdout.splitlines())):
                    assistant_stages = (
                        f"Assistant stages: {server.assistant_requests!r}\n"
                        if assistant_ciphertext else ""
                    )
                    raise SystemExit(f"Content did not reopen the migrated stores in {theme} {renderer}: "
                                     f"{server.observations!r}\n{assistant_stages}"
                                     f"{result.stdout}\n{result.stderr}")
                stdout = result.stdout
                if assistant_ciphertext and sorted(server.assistant_preview_observations) != (
                        ["target=true&shown=false&inline=solid&painted=solid"] * 3 +
                        ["target=true&shown=true&inline=solid&painted=dashed"] * 3):
                    raise SystemExit("Unexpected ad blocking preview behavior "
                                     f"{server.assistant_preview_observations!r}\n{result.stderr}")
                if assistant_ciphertext and sorted(server.assistant_diagnose_observations) != (
                        ["kind=child&armed=false"] * 3 +
                        ["kind=main&armed=false"] * 2 + ["kind=main&armed=true"]):
                    raise SystemExit("Unexpected diagnostic probe scope "
                                     f"{server.assistant_diagnose_observations!r}")
                if assistant_ciphertext and (
                        ASSISTANT_CREDENTIAL_MARKER not in stdout
                        or server.assistant_requests != ["context", "result", "consent", "edit",
                                                         "key", "activation", "pins", "pin_edits",
                                                         "open_tab", "navigate", "close_tab", "debug",
                                                         "reload_open", "hard_reload", "trial_open",
                                                         "trial", "trial_control", "save_rules", "save_cosmetics",
                                                         "diagnose_open", "diagnosed", "diagnose_control"]
                        or server.assistant_keys != ["end=true", "submit=true"]
                        or server.assistant_edits != [{"input": ["Zażółć dalej"], "editor": ["Tekst dalej"],
                                                      "protected": ["true"], "choice": ["two"]}]):
                    raise SystemExit("The Assistant did not complete a scoped provider round trip")
            if assistant_ciphertext:
                prefs = json.loads((content_root / f"{layout}-work" /
                                    "Content Ad Blocking Preferences").read_text(encoding="utf-8"))
                if prefs["zitter"]["ad_blocking_rules"] != [
                    f"|{origin}{ASSISTANT_AD_BLOCKED}^$xmlhttprequest",
                    ASSISTANT_EXEMPT_COSMETIC,
                    assistant_saved_network_rule(index), assistant_saved_cosmetic_rule(index),
                    ASSISTANT_SAVED_EXCEPTION,
                ]:
                    raise SystemExit("The Assistant did not persist exactly the approved rule replacements")
                personal_prefs = content_root / f"{layout}-personal" / "Content Ad Blocking Preferences"
                if personal_prefs.exists() and json.loads(personal_prefs.read_text(encoding="utf-8")).get(
                        "zitter", {}).get("ad_blocking_rules"):
                    raise SystemExit("The Assistant changed Personal ad blocking rules")
                path = content_root / ASSISTANT_SESSIONS_FILE
                history_data = json.loads(path.read_text(encoding="utf-8"))
                conversations = {session["key"]: session for session in history_data["sessions"]}
                for name in PROFILES:
                    imported = conversations.pop(f"legacy-{name}", {})
                    if (imported.get("profile") != name
                            or imported["messages"][1]["text"] != f"Imported {name.title()} reply"):
                        raise SystemExit("An imported Assistant conversation lost its profile")
                if len(conversations) != index + 1 or any(
                        session["profile"] != "work"
                        or session["model"] != "panel-fixture"
                        or not any("Content Assistant round trip passed" in message["text"]
                                   for message in session["messages"])
                        for session in conversations.values()):
                    raise SystemExit("New Assistant conversations did not reopen under Work")
                tasks = history_data["tasks"]
                if (len(tasks) != 1 or tasks[0]["id"] != "legacy-work-task"
                        or tasks[0]["profile"] != "work" or tasks[0]["active"]
                        or tasks[0]["next_run"] != 0):
                    raise SystemExit("An imported Assistant task resumed without confirmation")
            if shared_root and "rejected a write attempt" in stdout:
                raise SystemExit("Content attempted to write the legacy Keychain entry")
            if library and (KEYCHAIN_ITEM_CREATED in stdout) != (index == 0 and not shared_root):
                raise SystemExit("The file Keychain did not reuse the original password item")
            expected_requests = [("/personal/active", "personal"), ("/work/active", "work")]
            if args.split:
                expected_requests = [("/personal/deferred", ""), ("/personal/active", "personal"),
                                     ("/work/deferred", ""), ("/work/active", "work")]
                observed = sorted(server.requests[:2]) + sorted(server.requests[2:])
                expected_requests = sorted(expected_requests[:2]) + sorted(expected_requests[2:])
            else:
                observed = server.requests
            if args.filter_lists:
                expected_requests.extend([(FILTER_LISTS_PAGE, "work")] * 2)
                if index == 0:
                    expected_requests.extend([(FILTER_TARGETS[1], "work"),
                                              (FILTER_TARGETS[2], "work")])
                expected_requests.extend([(FILTER_LISTS_PAGE, "personal"),
                                          (FILTER_TARGETS[2], "personal")])
                observed, expected_requests = sorted(observed), sorted(expected_requests)
                expected_lists = [(name, "", "", "") for name in
                                  ("easylist.txt", "easyprivacy.txt", "ublock.txt", "optional-part.txt")]
                if sorted(server.list_requests) != sorted(expected_lists):
                    raise SystemExit(f"Unexpected filter downloads: {server.list_requests}")
                cache = content_root / "Zitter Filter Lists"
                stored = {name: (cache / name).read_bytes()
                          for name in ("easylist.txt", "easyprivacy.txt", "ublock.txt")}
                if index == 0:
                    if (origin + FILTER_TARGETS[1]).encode() not in stored["easylist.txt"] or (
                            origin + FILTER_TARGETS[2]).encode() not in stored["ublock.txt"]:
                        raise SystemExit("The cache did not publish both complete lists")
                    complete_cache = stored
                elif stored != complete_cache:
                    raise SystemExit("A failed refresh overwrote a valid list")
            if args.adblock:
                if index == 0:
                    expected_requests.extend([(ADBLOCK_TRIAL_PAGE, "work")] * 5)
                    expected_requests.extend([(ADBLOCK_COSMETIC_TRIAL_PAGE, "work")] * 5)
                    expected_requests.extend([(ADBLOCK_TRIAL_TARGET, "work")] * 3)
                expected_requests.extend([(ADBLOCK_PAGES[0], "work")] * 3)
                expected_requests.extend([(ADBLOCK_TARGET, "work"),
                                          (ADBLOCK_EDITOR_TARGET, "work")])
                expected_requests.extend([(ADBLOCK_PAGES[1], "personal"),
                                          (ADBLOCK_TARGET, "personal"),
                                          (ADBLOCK_EDITOR_TARGET, "personal"),
                                          (ADBLOCK_HISTORY_404, "personal")])
                observed = sorted(observed)
                expected_requests = sorted(expected_requests)
                for name in PROFILES:
                    prefs_file = content_root / f"{layout}-{name}" / "Content Ad Blocking Preferences"
                    prefs = json.loads(prefs_file.read_text(encoding="utf-8"))["zitter"]
                    if (prefs["ad_blocking"] != (name == "personal")
                            or prefs["ad_blocking_lists"] != {"zitter": False, "ublock": True}
                            or ADBLOCK_COSMETIC_TRIAL_RULE in prefs["ad_blocking_rules"]
                            or (f"|{origin}{ADBLOCK_TRIAL_TARGET}|$xmlhttprequest,domain=127.0.0.1"
                                in prefs["ad_blocking_rules"])
                            or prefs.get("ad_blocking_allowed_sites", []) != (
                                ["127.0.0.1"] if name == "personal" else [])
                            or (f"|{origin}{ADBLOCK_EDITOR_TARGET}|$xmlhttprequest"
                                in prefs["ad_blocking_rules"]) != (name == "work")):
                        raise SystemExit(f"Ad blocking settings changed unexpectedly for {name}")
                db_path = content_root / f"{layout}-personal" / "History"
                with closing(sqlite3.connect(db_path.as_uri() + "?mode=ro", uri=True)) as db:
                    hidden = db.execute("SELECT hidden FROM urls WHERE url = ?",
                                        (origin + ADBLOCK_HISTORY_404,)).fetchone()
                    if hidden != (1,):
                        raise SystemExit(f"The HTTP 404 visit was not hidden: {hidden}")
            if args.timeline:
                if index == 0:
                    expected_requests.append((TIMELINE_LEGACY_WORK, "work"))
                expected_requests.append((TIMELINE_RECORDED_WORK, "work"))
            if args.timeline_clear:
                timeline_path = content_root / Path(SESSION_FILES[layout]).with_suffix(".timeline.jsonl")
                records = [json.loads(line) for line in timeline_path.read_text(encoding="utf-8").splitlines()]
                marks = sorted(record["m"] for record in records if "m" in record)
                expected_marks = ["Personal older", "Personal recent", "Work older"] if index < 2 else []
                if marks != expected_marks:
                    raise SystemExit(f"Unexpected marks after Time Machine {mode}: {marks}")
                seeded_frames = {(record["w"], record["t"]) for record in records
                                 if "tabs" in record and record["t"] <= recent}
                expected_frames = {
                    (name, time_ms) for name in PROFILES
                    for time_ms in (now - 120_000, now - 60_000)
                }
                expected_frames.add(("personal", recent))
                if seeded_frames != (expected_frames if index < 2 else set()):
                    raise SystemExit(f"Unexpected retained frames after Time Machine {mode}: {seeded_frames}")
            if args.timeline_panel:
                expected_requests.extend([
                    (TIMELINE_LEGACY_WORK, "work"),
                    ("/work/deferred", ""),
                    ("/work/active", "work"),
                ])
                observed = sorted(observed)
                expected_requests = sorted(expected_requests)
                timeline_path = content_root / Path(SESSION_FILES[layout]).with_suffix(".timeline.jsonl")
                marks = [json.loads(line) for line in timeline_path.read_text(encoding="utf-8").splitlines()]
                snapshots = [mark["w"] for mark in marks if mark.get("m") == "Command Center"]
                if snapshots != ["work"] * (index + 1):
                    raise SystemExit("The Work snapshot was not committed to the timeline")
            if args.pins and not (args.sleep_workspace or args.timeline_panel) and (not args.edit_pins or index == 0):
                expected_requests.append(("/work/pin", ""))
            if args.edit_pins:
                expected_requests.append(("/work/pin-edited", ""))
            if observed != expected_requests:
                raise SystemExit(f"Unexpected HTTP cookie observations: {server.requests!r}")
            verify_reopened_session(content_root, origin, args.split, layout, args.pins,
                                    args.edit_pins, args.sleep_workspace,
                                    assistant_ciphertext)
            for name, encrypted in ciphertext.items():
                profile = content_root / f"{layout}-{name}"
                if cookie_ciphertext(profile) != encrypted:
                    raise SystemExit(f"Content changed the {name} cookie ciphertext")
                if legacy_visits(profile, f"{origin}/legacy/history/{name}") != history[name]:
                    raise SystemExit(f"Content changed the {name} legacy history visits")
        unrelated = "mock-keychain" if library else "keychain"
        if any((content_root / path).exists()
               for path in (f"{unrelated}-work", "workspace-work")):
            raise SystemExit("Keychain migration reused an unrelated profile directory")
        if library and not ((shared_root or content_root) / FILE_KEYCHAIN).is_file():
            raise SystemExit("The real Keychain fixture did not persist its private file")

        if args.ordinary_startup:
            startup_root = scratch("zitter-content-probe-")
            startup_root.rmdir()
            startup_trial = scratch("zitter-content-startup-upgrade-") / "trial"
            prepare_macos_upgrade(source, startup_trial, startup_root)
            if inventory(startup_trial / "baseline") != original:
                raise SystemExit("The startup upgrade changed its rollback baseline")
            verify_ordinary_prepared_startup(content, startup_root, server, content_env)
            if args.single_personal_startup:
                single_source = scratch("zitter-content-legacy-")
                copy_verified(source, single_source, original)
                catalogue_path = single_source / "ZitterWorkspaces.json"
                catalogue = json.loads(catalogue_path.read_text(encoding="utf-8"))
                catalogue["profiles"] = [profile for profile in catalogue["profiles"]
                                         if profile["directory"] == "ZitterPersonal"]
                catalogue["workspaces"] = [workspace for workspace in catalogue["workspaces"]
                                           if workspace["id"] == "personal"]
                catalogue_path.write_text(json.dumps(catalogue), encoding="utf-8")
                session_path = single_source / "ZitterSession.json"
                session = json.loads(session_path.read_text(encoding="utf-8"))
                session_path.write_text(json.dumps({"personal": session["personal"]}), encoding="utf-8")
                shutil.rmtree(single_source / "ZitterWork")
                single_root = scratch("zitter-content-probe-")
                single_root.rmdir()
                single_trial = scratch("zitter-content-startup-upgrade-") / "trial"
                if prepare_macos_upgrade(single_source, single_trial, single_root) != 1:
                    raise SystemExit("Personal-only upgrade copied an unexpected profile")
                verify_ordinary_prepared_startup(content, single_root, server, content_env)
                if any(path.startswith("/work/") for path, _ in server.requests):
                    raise SystemExit("Personal-only startup loaded a Work tab")
                print("The prepared Personal-only catalogue started without restoring Work")

        if args.replace_app:
            replacement.close()

        rollback(trial, restored)
        if inventory(restored) != original:
            raise SystemExit("Rollback changed the legacy snapshot")
        # Open the verified rollback copy in Work for the legacy application checks.
        set_legacy_active(restored, "work")
        run_legacy_profile_probe(node, legacy, restored, "-", "-", url, "read-storage-work", env=legacy_env)
        run_legacy_profile_probe(node, legacy, restored, "-", "-", url, "read-clipboard-allow", env=legacy_env)
        if (inventory(source) != original or inventory(trial / "candidate") != original
                or inventory(trial / "baseline") != original):
            raise SystemExit("Storage migration changed an input snapshot")
        print("Migrated macOS Workspace storage, history, and Clipboard rules and verified rollback")
        if args.replace_app:
            print("Both signed bundles ran from the same disposable installation path across atomic replacement and rollback")


if __name__ == "__main__":
    main()
