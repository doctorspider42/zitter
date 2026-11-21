#!/usr/bin/env python3
"""Exercise bookmark reconciliation in two disposable desktop workspaces."""

import argparse
import base64
import hashlib
import json
import os
import plistlib
import secrets
import subprocess
import tempfile
import threading
import time
import uuid
from contextlib import contextmanager
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from verify_content_automation import ROOT, expect, request, temporary_workspace, wait
from windows_test_desktop import parse_probe_args


def read_json(path):
    return json.loads(path.read_bytes())


def write_json(path, value):
    path.write_text(json.dumps(value), encoding="utf-8")


def shard_directory(folder):
    return folder / "Zitter Bookmarks" / "sync-v2"


def device_index(root, folder):
    device = read_json(root / "BookmarkSync.json")["device"]
    return shard_directory(folder) / f"index-{device}.json"


def read_partition(index_path, key):
    digest = read_json(index_path)["shards"][key]
    path = index_path.parent / (digest + ".json")
    contents = path.read_bytes()
    assert hashlib.sha256(contents).hexdigest() == digest
    return json.loads(contents)


def write_partition_fixture(folder, device, key, partition):
    directory = shard_directory(folder)
    directory.mkdir(parents=True, exist_ok=True)
    contents = json.dumps(partition, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    digest = hashlib.sha256(contents).hexdigest()
    (directory / (digest + ".json")).write_bytes(contents)
    write_json(directory / f"index-{device}.json", {"format": 2, "shards": {key: digest}})


def pin(name, **overrides):
    return dict({"id": name, "parent_id": "", "title": name,
                 "url": "https://example.org/" + name,
                 "profile_pin": False, "folder": False}, **overrides)


def prepare(root, folder, local, extra=None):
    if os.name != "nt":
        key = root / "probe-cookie-key"
        key.write_bytes(secrets.token_bytes(32))
        key.chmod(0o600)
    session = {role: {"profile_id": role, "active": 0, "tabs": [{"title": "", "url": ""}]}
               for role in ("work", "personal")}
    session.update(active_workspace="work")
    session["work"]["pins"] = [pin(local)]
    if extra:
        session["extra_workspaces"] = [{"id": extra, "name": "Reading"}]
        session[extra] = {"profile_id": "personal", "active": 0,
                          "tabs": [{"title": "", "url": ""}],
                          "pins": [pin("books", folder=True, url=""), pin("chapter", parent_id="books")]}
    write_json(root / "ContentWorkspaceSession.json", session)
    write_json(root / "BookmarkSync.json", {"method": "folder", "device": str(uuid.uuid4()),
                                             "folder": str(folder)})


@contextmanager
def running(executable, root, panel="settings", theme="light"):
    log_path = root / "test.log"
    with log_path.open("wb") as log:
        process = subprocess.Popen([executable, "--zitter-content-workspace", "--zitter-automation",
                                    f"--zitter-content-workspace-theme={theme}",
                                    f"--zitter-open-panel={panel}", f"--zitter-content-workspace-root={root}"],
                                   stdout=log, stderr=log)
        try:
            channel = root / "automation.sock"
            if not wait(lambda: request(channel, {"cmd": "ping"}, 2).get("ok"), 30):
                raise AssertionError(log_path.read_text(errors="replace")[-3000:])
            yield channel
        finally:
            if process.poll() is None:
                if os.name == "nt":
                    # Chromium child processes can retain the log and profile files.
                    subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                                   capture_output=True, timeout=10)
                else:
                    process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


def visible_window(channel, titles):
    windows = expect(request(channel, {"cmd": "windows"}), "windows")["windows"]
    return next((window["id"] for window in reversed(windows)
                 if window["visible"] and window["title"] in titles), None)


def click_control(channel, names, titles):
    def find():
        window = visible_window(channel, titles)
        if window:
            tree = request(channel, {"cmd": "tree", "window": window})
            for view in tree.get("views", []):
                if view["name"] in names:
                    return window, view["name"]
        return None

    target = wait(find, 15)
    assert target, f"Missing control {names}"
    window, name = target
    # Scene dialogs reject clicks immediately after their first paint.
    time.sleep(0.65)
    expect(request(channel, {"cmd": "click", "window": window, "name": name, "index": 0}), name)


def verify_initial_import(executable, root, folder, fixture, seen_icon, replace):
    prepare(root, folder, "local")
    session_path = root / "ContentWorkspaceSession.json"
    session = read_json(session_path)
    local_tab = fixture + "/local-tab"
    session["work"]["tabs"] = [{"title": "", "url": local_tab}]
    write_json(session_path, session)
    prefs_path = root / "BookmarkSync.json"
    prefs = read_json(prefs_path)
    prefs["import_pending"] = True
    write_json(prefs_path, prefs)

    remote_device = str(uuid.uuid4())
    reading = "space-" + str(uuid.uuid4())
    empty = "space-" + str(uuid.uuid4())
    profile = "profile-" + str(uuid.uuid4())

    def record(**data):
        return {"clock": 1, "device": remote_device, **data}

    def space(name, position, owner, color, emoji, **pins):
        return {"": record(scope={"name": name, "profile": owner, "position": position,
                                  "color": color, "emoji": emoji}), **pins}

    remote = {
        "space/personal": space("Personal", 0, "personal", 0x334455, ""),
        f"space/{empty}": space("Empty", 1, "personal", 0x112244, ""),
        f"space/{reading}": space("Reading", 2, profile, 0x225588, "★"),
        "space/work": space("Research", 3, "work", 0x112233, "🔬",
                            second=record(pin=pin("second", url=fixture + "/second"), position=0),
                            first=record(pin=pin("first", url=fixture + "/first"), position=1)),
        "tabs/work": {"": record(scope={"name": "Research", "profile": "work",
                                         "tabs": [fixture + "/tab-two", fixture + "/tab-one"]})},
        f"profile/{profile}": {"": record(scope={"name": "Research profile", "profile": profile,
                                                "extensions": ["a" * 32]})},
    }
    shared = folder / "Zitter Bookmarks"
    shared.mkdir()
    original = shared / (remote_device + ".json")
    write_json(original, remote)

    seen_icon.clear()
    with running(executable, root, theme="dark" if replace else "light") as channel:
        chooser = {"Workspace sync preview", "Podgląd synchronizacji przestrzeni"}
        warning = {"Replace local sync data?", "Zastąpić lokalne dane synchronizacji?"}
        choose_replace = {"Replace local bookmarks and open tabs",
                          "Zastąp lokalne zakładki i otwarte karty"}
        window = wait(lambda: visible_window(channel, chooser), 15)
        assert window, "Missing initial sync preview"
        tree = expect(request(channel, {"cmd": "tree", "window": window}), "preview")["views"]
        copy = "\n".join(view["name"] for view in tree)
        assert ("Open tabs: 2" in copy or "Otwarte karty: 2" in copy) and "Research" in copy, copy
        assert ("Spaces: 4" in copy or "Przestrzenie: 4" in copy), copy
        click_control(channel, choose_replace, chooser)
        assert len(list(shared.iterdir())) == 1, "Local changes uploaded before first import"
        click_control(channel, {"Continue", "Kontynuuj"}, chooser)
        if replace:
            # Approval applies to the copy already loaded, even if the provider
            # changes while the dialog is open or profiles are being created.
            write_json(original, {})
            click_control(channel, {"Replace local data", "Zastąp lokalne dane"}, warning)
        else:
            click_control(channel, {"Cancel", "Anuluj"}, warning)
            click_control(channel, {"Continue", "Kontynuuj"}, chooser)
        assert wait(lambda: not read_json(prefs_path).get("import_pending"), 30)
        assert seen_icon.wait(15), "Sync did not request a favicon for the incoming bookmarks"

        def appearance_saved():
            path = root / "WorkspaceSettings.json"
            return path.exists() and read_json(path).get("workspace_emoji", {}).get(reading) == "★"

        assert wait(appearance_saved, 15)
        session = read_json(session_path)
        settings = read_json(root / "WorkspaceSettings.json")
        assert settings[reading].lower() == "#225588", settings
        assert settings["work"].lower() == "#112233", settings
        assert session["workspace_names"]["work"] == "Research"
        assert {item["id"] for item in session["extra_workspaces"]} == {reading, empty}
        assert [item["id"] for item in session["extra_profiles"]] == [profile]
        assert read_json(prefs_path)["workspace_order"] == ["personal", empty, reading, "work"]
        assert [item["id"] for item in session["work"]["pins"]] == (
            ["second", "first"] if replace else ["second", "first", "local"])
        tabs = [tab["url"] for tab in session["work"]["tabs"]]
        assert (local_tab in tabs) == (not replace), tabs
        assert [url for url in tabs if url.startswith(fixture + "/tab-")] == [
            fixture + "/tab-two", fixture + "/tab-one"], tabs
        assert read_json(original) == ({} if replace else remote)
        assert wait(device_index(root, folder).exists, 15)
    if replace:
        synced = {tab["url"]: tab["sync_id"] for tab in session["work"]["tabs"]
                  if tab["url"].startswith(fixture + "/tab-")}
        write_partition_fixture(folder, remote_device, "space/work", {
            "tabs2/work": {synced[fixture + "/tab-one"]: record(clock=50)}})
        with running(executable, root):
            def remote_close_applied():
                tabs = read_json(session_path)["work"]["tabs"]
                return [tab["url"] for tab in tabs if tab["url"].startswith(fixture + "/tab-")] == [
                    fixture + "/tab-two"]
            assert wait(remote_close_applied, 30), "A remote tab closure did not reach the workspace"


def verify_concurrent_tabs(executable, folder, first, second, receiver, fixture):
    prefix = fixture + "/concurrent-"
    session_name = "ContentWorkspaceSession.json"
    initial = [
        {"title": "", "url": prefix + suffix, "sync_id": str(uuid.uuid4())}
        for suffix in ("remove", "keep")
    ]

    def set_tabs(root, tabs):
        path = root / session_name
        session = read_json(path)
        session["work"]["tabs"] = tabs
        session["work"]["active"] = 0
        write_json(path, session)

    def tab_ids(root):
        tabs = read_json(root / session_name)["work"]["tabs"]
        return {tab["sync_id"] for tab in tabs if tab["url"].startswith(prefix)}

    def tab_by_id(root, identifier):
        tabs = read_json(root / session_name)["work"]["tabs"]
        return next((tab for tab in tabs if tab.get("sync_id") == identifier), {})

    for root in (first, second):
        prepare(root, folder, "local")
        set_tabs(root, initial)
        with running(executable, root):
            assert wait(device_index(root, folder).exists, 30)
            assert wait(lambda: read_json(root / "BookmarkSync.json").get("snapshot"), 15)

    ref_a, ref_b = (device_index(root, folder) for root in (first, second))
    untouched = read_json(ref_b)["shards"]["space/personal"]
    if read_json(ref_a)["shards"]["space/personal"] != untouched:
        with running(executable, first):
            assert wait(lambda: read_json(ref_a)["shards"]["space/personal"] == untouched, 20)
    untouched_file = ref_b.parent / (untouched + ".json")
    untouched_mtime = untouched_file.stat().st_mtime_ns

    # Both devices edit their existing copy offline. Closing one tab on the
    # second device must survive the index shifts from the first device's edit.
    additions = [
        {"title": "", "url": prefix + label, "sync_id": str(uuid.uuid4())}
        for label in ("from-a", "from-b")
    ]
    navigated = {**initial[1], "url": prefix + "navigated", "custom_title": "Shared title"}
    set_tabs(first, [*initial, additions[0]])
    set_tabs(second, [navigated, additions[1]])
    expected = {tab["sync_id"] for tab in [initial[1], *additions]}
    for root, index, addition in ((first, ref_a, additions[0]), (second, ref_b, additions[1])):
        with running(executable, root):
            assert wait(lambda: addition["sync_id"] in read_partition(index, "space/work")[
                "tabs2/work"], 30)
            if root == second:
                assert wait(lambda: tab_ids(second) == expected, 30), tab_ids(second)
        assert read_json(index)["shards"]["space/personal"] == untouched
        assert untouched_file.stat().st_mtime_ns == untouched_mtime

    with running(executable, first):
        assert wait(lambda: tab_ids(first) == expected, 45), "First device did not reconcile its tab IDs"
        restored = tab_by_id(first, navigated["sync_id"])
        assert restored["url"] == navigated["url"]
        assert restored["custom_title"] == navigated["custom_title"]
    assert tab_ids(second) == expected

    # Publish an index whose workspace shard has not arrived yet. No import
    # should be offered and nothing should be written on the receiving device.
    prepare(receiver, folder, "local")
    set_tabs(receiver, [{"title": "", "url": ""}])
    receiver_session = receiver / session_name
    session = read_json(receiver_session)
    session["work"].pop("pins")
    write_json(receiver_session, session)
    prefs = receiver / "BookmarkSync.json"
    write_json(prefs, {**read_json(prefs), "import_pending": True})
    receiver_index = device_index(receiver, folder)
    absent = uuid.uuid4().hex * 2
    incomplete = shard_directory(folder) / f"index-{uuid.uuid4()}.json"
    write_json(incomplete, {"format": 2, "shards": {"space/work": absent}})
    chooser = {"Workspace sync preview", "Podgląd synchronizacji przestrzeni"}
    settings = {"Zitter — Ustawienia", "Zitter — Settings", "Zitter — Preferences"}
    corrupt = incomplete.parent / (absent + ".json")
    for missing in (True, False):
        if not missing:
            corrupt.write_text("{}", encoding="utf-8")
        with running(executable, receiver) as channel:
            click_control(channel, {"Hasła i profile", "Passwords and profiles"}, settings)
            window = visible_window(channel, settings)
            def paused():
                tree = request(channel, {"cmd": "tree", "window": window}).get("views", [])
                return any("Sync paused." in view["name"] or
                           "Synchronizacja wstrzymana." in view["name"] for view in tree)
            assert wait(paused, 10)
            assert not visible_window(channel, chooser)
            assert not receiver_index.exists() and read_json(prefs)["import_pending"]
            assert not tab_ids(receiver)
    incomplete.unlink()
    corrupt.unlink()
    with running(executable, receiver) as channel:
        assert wait(lambda: visible_window(channel, chooser), 15)
        assert not receiver_index.exists(), "A clean device published data before approval"
        click_control(channel, {"Continue", "Kontynuuj"}, chooser)
        assert wait(lambda: tab_ids(receiver) == expected, 30), tab_ids(receiver)
    print("Stable tab IDs merged offline edits. Unchanged shards were reused. "
          "Missing and corrupt files failed safely.")


def verify_first_imports(executable):
    seen_icon = threading.Event()

    class Fixture(BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path == "/synced-favicon.png":
                body = base64.b64decode(
                    "iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAYAAAAf8/9hAAAAHUlEQVR4nGOUKbv2n4ECwESJ5l"
                    "EDRg0YNWAwGQAAAJACh9zWzYwAAAAASUVORK5CYII=")
                mime = "image/png"
                seen_icon.set()
            elif self.path in ("/first", "/second"):
                body = b'<!doctype html><link rel="icon" href="/synced-favicon.png">'
                mime = "text/html"
            else:
                body = b'<!doctype html><title>Local tab</title>'
                mime = "text/html"
            self.send_response(200)
            self.send_header("Content-Type", mime)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *args):
            pass

    with ThreadingHTTPServer(("127.0.0.1", 0), Fixture) as server:
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            fixture = f"http://127.0.0.1:{server.server_port}"
            for replace in (False, True):
                with (temporary_workspace() as root,
                      tempfile.TemporaryDirectory() as shared):
                    verify_initial_import(executable, Path(root).resolve(), Path(shared).resolve(),
                                          fixture, seen_icon, replace)
            with (tempfile.TemporaryDirectory() as shared,
                  temporary_workspace() as first,
                  temporary_workspace() as second,
                  temporary_workspace() as receiver):
                verify_concurrent_tabs(executable, Path(shared).resolve(), Path(first).resolve(),
                                       Path(second).resolve(), Path(receiver).resolve(), fixture)
        finally:
            server.shutdown()
            thread.join()
    print("First import and ongoing sync passed bookmark and tab order, replacement, remote tab closure, "
          "workspace appearance and favicon refresh.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path, nargs="?", default=ROOT / "build/ZitterContentLab.app")
    app = parse_probe_args(parser, __file__).app.resolve(strict=True)
    executable = app
    if app.is_dir():
        executable = app / "Contents/MacOS" / plistlib.loads(
            (app / "Contents/Info.plist").read_bytes())["CFBundleExecutable"]
    with (temporary_workspace() as a,
          temporary_workspace() as b,
          tempfile.TemporaryDirectory() as shared):
        first, second, folder = map(lambda p: Path(p).resolve(), (a, b, shared))
        extra = "space-" + str(uuid.uuid4())
        prepare(first, folder, "first", extra)
        prepare(second, folder, "second")
        def ids(root):
            return {item["id"] for item in read_json(root / "ContentWorkspaceSession.json")["work"]["pins"]}

        with running(executable, first):
            assert wait(device_index(first, folder).exists, 30)
        with running(executable, second):
            assert wait(lambda: ids(second) == {"first", "second"}, 45)
            pins = read_json(second / "ContentWorkspaceSession.json")[extra]["pins"]
            assert [p["id"] for p in pins] == ["books", "chapter"]
            assert pins[1]["parent_id"] == "books"

        # Remove the local bookmark while offline, then reconnect both devices.
        path = second / "ContentWorkspaceSession.json"
        session = read_json(path)
        session["work"]["pins"] = [p for p in session["work"]["pins"] if p["id"] != "first"]
        write_json(path, session)
        with running(executable, second):
            def deletion_published():
                return "pin" not in read_partition(device_index(second, folder), "space/work")[
                    "space/work"].get("first", {"pin": {}})
            assert wait(deletion_published, 45)
        with running(executable, first) as channel:
            assert wait(lambda: ids(first) == {"second"}, 45)
            def settings():
                windows = expect(request(channel, {"cmd": "windows"}), "settings")["windows"]
                return next((w["id"] for w in windows if w["visible"] and w["title"] in
                            {"Zitter — Ustawienia", "Zitter — Settings", "Zitter — Preferences"}), None)
            window = wait(settings, 15)
            assert window, "Settings did not open"
            def click(names):
                def find():
                    tree = expect(request(channel, {"cmd": "tree", "window": window}), "tree")
                    return next((view["name"] for view in tree["views"] if view["name"] in names), None)
                name = wait(find, 10)
                assert name, f"Missing control {names}"
                expect(request(channel, {"cmd": "click", "name": name, "window": window}), name)
            click({"Hasła i profile", "Passwords and profiles"})
            device = str(uuid.uuid4())
            write_partition_fixture(folder, device, "space/work", {
                "space/work": {"manual": {"clock": 100, "device": device,
                                          "pin": pin("manual"), "position": 0}}})
            assert "manual" not in ids(first)
            click({"Synchronizuj teraz", "Sync now"})
            assert wait(lambda: "manual" in ids(first), 8), "Sync now did not import a remote bookmark"
            click({"Wyłącz synchronizację", "Turn off sync"})
            assert wait(lambda: not read_json(first / "BookmarkSync.json").get("method"), 10)
            assert ids(first) == {"second", "manual"}
            assert len(list(shard_directory(folder).glob("index-*.json"))) == 3
    print("Folder sync passed first merge, custom workspace, nested pins, offline deletion, manual sync and disable.")
    verify_first_imports(executable)


if __name__ == "__main__":
    main()
