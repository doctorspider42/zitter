#!/usr/bin/env python3
"""Exercise bookmark reconciliation in two disposable desktop workspaces."""

import argparse
import json
import os
import plistlib
import secrets
import subprocess
import tempfile
import uuid
from contextlib import contextmanager
from pathlib import Path

from verify_content_automation import ROOT, expect, request, wait


def read_json(path):
    return json.loads(path.read_bytes())


def write_json(path, value):
    path.write_text(json.dumps(value), encoding="utf-8")


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
def running(executable, root, panel="settings"):
    with (root / "test.log").open("wb") as log:
        process = subprocess.Popen([executable, "--zitter-content-workspace", "--zitter-automation",
                                    f"--zitter-open-panel={panel}", f"--zitter-content-workspace-root={root}"],
                                   stdout=log, stderr=log)
        try:
            channel = root / "automation.sock"
            if not wait(lambda: request(channel, {"cmd": "ping"}, 2).get("ok"), 30):
                raise AssertionError((root / "test.log").read_text(errors="replace")[-3000:])
            yield channel
        finally:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path, nargs="?", default=ROOT / "build/ZitterContentLab.app")
    app = parser.parse_args().app.resolve(strict=True)
    executable = app
    if app.is_dir():
        executable = app / "Contents/MacOS" / plistlib.loads(
            (app / "Contents/Info.plist").read_bytes())["CFBundleExecutable"]
    with (tempfile.TemporaryDirectory(prefix="zitter-content-probe-") as a,
          tempfile.TemporaryDirectory(prefix="zitter-content-probe-") as b,
          tempfile.TemporaryDirectory() as shared):
        first, second, folder = map(lambda p: Path(p).resolve(), (a, b, shared))
        extra = "space-" + str(uuid.uuid4())
        prepare(first, folder, "first", extra)
        prepare(second, folder, "second")
        def ids(root):
            return {item["id"] for item in read_json(root / "ContentWorkspaceSession.json")["work"]["pins"]}

        with running(executable, first):
            assert wait(lambda: list((folder / "Zitter Bookmarks").glob("*.json")), 30)
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
                return any("pin" not in read_json(path)["space/work"].get("first", {"pin": {}})
                           for path in (folder / "Zitter Bookmarks").glob("*.json"))
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
            click({"Wyłącz synchronizację", "Turn off sync"})
            assert wait(lambda: not read_json(first / "BookmarkSync.json").get("method"), 10)
            assert ids(first) == {"second"}
            assert len(list((folder / "Zitter Bookmarks").glob("*.json"))) == 2
    print("Folder sync passed first merge, custom workspace, nested pins, offline deletion and disable.")


if __name__ == "__main__":
    main()
