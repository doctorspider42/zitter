import base64
from contextlib import closing
import json
import os
from pathlib import Path
import shutil
import sqlite3
import stat
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

from tools.profile_migration_trial import (
    ASSISTANT_SESSIONS_FILE,
    MAX_ASSISTANT_CONVERSATIONS,
    CLIPBOARD_PREFS,
    IMPORTED_DOWNLOAD_MARKER,
    LEGACY_EXTENSION_RECORD,
    LEGACY_EXTENSION_RECORDS,
    PAGE_ZOOM_PREFS,
    PASSWORD_PREFS,
    PREPARED_WORKSPACE_MARKER,
    WINDOWS_LOCAL_STATE,
    clipboard_rules_from_preferences,
    copy_browser_storage,
    copy_clipboard_permissions,
    copy_extension_storage,
    import_workspace_profiles,
    inspect_windows_encryption,
    inventory,
    is_local_download_path,
    prepare,
    prepare_macos_upgrade,
    retain_download_history,
    rollback,
)


def clipboard_preferences(rules, **profile_fields):
    return {"profile": {"content_settings": {"exceptions": {"clipboard": rules}},
                        **profile_fields}}


class ProfileMigrationTrialTest(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.source = self.root / "offline"
        self.trial = self.root / "trial"
        self.recovered = self.root / "rollback"
        self.source.mkdir()
        self.config = {
            "profiles": [
                {"id": "work-profile", "name": "Work", "directory": "ZitterWork"},
                {"id": "personal-profile", "name": "Personal", "directory": "ZitterPersonal"},
            ],
            "workspaces": [
                {"id": "work", "name": "Work", "profile_id": "work-profile"},
                {"id": "personal", "name": "Personal", "profile_id": "personal-profile"},
            ],
        }
        self.write_config()
        (self.source / "Local State").write_text(
            '{"os_crypt":{"encrypted_key":"synthetic-dpapi-fixture"}}', encoding="utf-8"
        )
        for profile in self.config["profiles"]:
            root = self.source / profile["directory"]
            (root / "Network").mkdir(parents=True)
            (root / "Network/Cookies").write_bytes(profile["id"].encode())
            (root / "Network/Cookies-wal").write_bytes(b"pending cookie pages")
            (root / "Login Data").write_bytes(b"encrypted login fixture")

    def write_config(self):
        (self.source / "ZitterWorkspaces.json").write_text(
            json.dumps(self.config), encoding="utf-8"
        )

    def test_candidate_changes_do_not_affect_source_or_rollback(self):
        original = inventory(self.source)
        self.assertEqual(prepare(self.source, self.trial), 2)
        candidate = self.trial / "candidate"
        (candidate / "ZitterWork/Network/Cookies").write_bytes(b"candidate changed")
        (candidate / "ZitterPersonal/Login Data").unlink()
        (candidate / "ZitterWorkspaces.json").write_text("{}", encoding="utf-8")

        rollback(self.trial, self.recovered)
        self.assertEqual(inventory(self.source), original)
        self.assertEqual(inventory(self.trial / "baseline"), original)
        self.assertEqual(inventory(self.recovered), original)
        self.assertNotEqual(inventory(candidate), original)
        with self.assertRaisesRegex(ValueError, "already exists"):
            rollback(self.trial, self.recovered)

    def test_macos_upgrade_keeps_rollback_if_the_startup_session_is_missing(self):
        expected = inventory(self.source)
        target = self.root / "startup"
        with self.assertRaisesRegex(ValueError, "no Workspace session"):
            prepare_macos_upgrade(self.source, self.trial, target)
        self.assertFalse(target.exists())
        rollback(self.trial, self.recovered)
        self.assertEqual(inventory(self.recovered), expected)
        self.assertEqual(inventory(self.source), expected)

    def test_refuses_modified_baseline(self):
        prepare(self.source, self.trial)
        (self.trial / "baseline/Local State").write_text("{}", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "baseline has changed"):
            rollback(self.trial, self.recovered)
        self.assertFalse(self.recovered.exists())

    def test_refuses_active_or_nested_source(self):
        (self.source / "SingletonLock").touch()
        with self.assertRaisesRegex(ValueError, "browser lock"):
            prepare(self.source, self.trial)
        self.assertFalse(self.trial.exists())
        with self.assertRaisesRegex(ValueError, "separate directories"):
            prepare(self.source, self.source / "nested")

    def mark_reparse_point(self, target, *, after=0):
        lstat = Path.lstat
        canonical = target.resolve()
        reads = 0

        def marked(path):
            nonlocal reads
            info = lstat(path)
            if path in (target, canonical):
                reads += 1
                if reads > after:
                    return SimpleNamespace(st_mode=info.st_mode,
                                           st_file_attributes=stat.FILE_ATTRIBUTE_REPARSE_POINT)
            return info

        return mock.patch.object(Path, "lstat", marked)

    def test_prepare_refuses_reparse_roots_and_subdirectories(self):
        for path in (self.source, self.source / "ZitterWork/Network"):
            with self.subTest(path=path), self.mark_reparse_point(path):
                with self.assertRaisesRegex(ValueError, "reparse point"):
                    prepare(self.source, self.trial)
            self.assertFalse(self.trial.exists())

    def test_prepare_rolls_back_when_reparse_point_appears_during_copy(self):
        with self.mark_reparse_point(self.source / "ZitterWork/Network", after=1):
            with self.assertRaisesRegex(ValueError, "offline copy encountered a link or reparse point"):
                prepare(self.source, self.trial)
        self.assertFalse(self.trial.exists())

    def test_storage_copy_refuses_candidate_cookie_reparse_point(self):
        prepare(self.source, self.trial)
        candidate = self.trial / "candidate"
        with self.mark_reparse_point(candidate / "ZitterWork/Network"):
            with self.assertRaisesRegex(ValueError, "cookie directory contains a link"):
                copy_browser_storage(self.trial, "work-profile", self.recovered)
        self.assertFalse(self.recovered.exists())

    def test_refuses_workspace_that_points_to_unknown_profile(self):
        self.config["workspaces"][0]["profile_id"] = "missing"
        self.write_config()
        with self.assertRaisesRegex(ValueError, "unknown workspace profile"):
            prepare(self.source, self.trial)
        self.assertFalse(self.trial.exists())

    def test_copies_only_the_verified_extension_store_to_a_new_profile(self):
        extension_id = "a" * 32
        relative = Path("Local Extension Settings", extension_id)
        original = self.source / "ZitterWork" / relative
        original.mkdir(parents=True)
        (original / "CURRENT").write_bytes(b"MANIFEST-000001\n")
        (original / "MANIFEST-000001").write_bytes(b"synthetic manifest")
        (original / "000002.log").write_bytes(b"synthetic extension data")
        prepare(self.source, self.trial)
        target = self.root / "content-profile"

        copy_extension_storage(self.trial, "work-profile", extension_id, target)
        self.assertEqual(inventory(target / relative), inventory(original))
        self.assertFalse((target / "Network").exists())
        with self.assertRaisesRegex(ValueError, "already exists"):
            copy_extension_storage(self.trial, "work-profile", extension_id, target)

        candidate = self.trial / "candidate" / "ZitterWork" / relative
        (candidate / "000002.log").write_bytes(b"changed extension data")
        rejected = self.root / "rejected"
        with self.assertRaisesRegex(ValueError, "changed after preparation"):
            copy_extension_storage(self.trial, "work-profile", extension_id, rejected)
        self.assertFalse(rejected.exists())

    def test_exports_legacy_grants_and_rejects_modified_preferences(self):
        extension_id = "b" * 32
        profile = self.source / "ZitterWork"
        store = profile / "Local Extension Settings" / extension_id
        store.mkdir(parents=True)
        (store / "CURRENT").write_bytes(b"MANIFEST-000001\n")
        (store / "MANIFEST-000001").write_bytes(b"synthetic manifest")
        permissions = {
            "api": ["storage"], "manifest_permissions": [],
            "explicit_host": [], "scriptable_host": [],
        }
        settings = {
            "active_permissions": permissions, "granted_permissions": permissions,
            "withholding_permissions": False, "disable_reasons": [1],
            "path": "/legacy/extension",
        }
        preferences = {"extensions": {"settings": {extension_id: settings}}, "unrelated": True}
        (profile / "Secure Preferences").write_text(json.dumps(preferences), encoding="utf-8")
        prepare(self.source, self.trial)

        target = self.root / "content-profile"
        copy_extension_storage(
            self.trial, "work-profile", extension_id, target, include_install_state=True,
        )
        receipt = json.loads((target / LEGACY_EXTENSION_RECORD).read_text(encoding="utf-8"))
        self.assertEqual(receipt["disable_reasons"], [1])
        self.assertEqual(receipt["granted_permissions"], permissions)
        self.assertNotIn("path", receipt)
        self.assertFalse((target / "Secure Preferences").exists())

        preferences_file = self.trial / "candidate/ZitterWork/Secure Preferences"
        settings["disable_reasons"] = []
        preferences_file.write_text(json.dumps(preferences), encoding="utf-8")
        rejected = self.root / "rejected"
        with self.assertRaisesRegex(ValueError, "candidate extension preferences"):
            copy_extension_storage(
                self.trial, "work-profile", extension_id, rejected, include_install_state=True,
            )
        self.assertFalse(rejected.exists())

        settings["manifest"] = {"externally_connectable": {"ids": ["c" * 32, "a" * 32, "a" * 32]}}
        (profile / "Secure Preferences").write_text(json.dumps(preferences), encoding="utf-8")
        restricted_trial = self.root / "restricted-trial"
        prepare(self.source, restricted_trial)
        copy_extension_storage(
            restricted_trial, "work-profile", extension_id, rejected, include_install_state=True,
        )
        restricted_receipt = json.loads((rejected / LEGACY_EXTENSION_RECORD).read_text("utf-8"))
        self.assertEqual(restricted_receipt["version"], 2)
        self.assertEqual(restricted_receipt["external_extension_ids"], ["a" * 32, "c" * 32])
        self.assertEqual(receipt["version"], 1)
        self.assertNotIn("external_extension_ids", receipt)

        settings["manifest"]["externally_connectable"]["matches"] = ["https://example.test/*"]
        (profile / "Secure Preferences").write_text(json.dumps(preferences), encoding="utf-8")
        website_trial = self.root / "website-trial"
        prepare(self.source, website_trial)
        website_destination = self.root / "website-profile"
        with self.assertRaisesRegex(ValueError, "external messaging policy"):
            copy_extension_storage(
                website_trial, "work-profile", extension_id, website_destination,
                include_install_state=True,
            )
        self.assertFalse(website_destination.exists())

    def test_workspace_import_keeps_selected_extension_state_separate(self):
        extension_id = "c" * 32
        relative = Path("Local Extension Settings", extension_id)
        permissions = {
            "api": ["storage"], "manifest_permissions": [],
            "explicit_host": [], "scriptable_host": [],
        }
        reasons = {"work": [], "personal": [1]}
        for name, disabled in reasons.items():
            profile = self.source / f"Zitter{name.title()}"
            store = profile / relative
            store.mkdir(parents=True)
            (store / "CURRENT").write_bytes(b"MANIFEST-000001\n")
            (store / "MANIFEST-000001").write_bytes(name.encode())
            settings = {
                "active_permissions": permissions, "granted_permissions": permissions,
                "withholding_permissions": False, "disable_reasons": disabled,
            }
            (profile / "Secure Preferences").write_text(json.dumps({
                "extensions": {"settings": {extension_id: settings}},
            }), encoding="utf-8")

        prepare(self.source, self.trial)
        target = self.root / "workspace-import"
        selected = dict.fromkeys(reasons, extension_id)
        import_workspace_profiles(self.trial, target, "workspace",
                                  selected_extensions=selected)
        for name, disabled in reasons.items():
            imported = target / f"workspace-{name}"
            self.assertEqual((imported / relative / "MANIFEST-000001").read_bytes(), name.encode())
            receipt = json.loads((imported / LEGACY_EXTENSION_RECORD).read_text(encoding="utf-8"))
            self.assertEqual(receipt["disable_reasons"], disabled)
            self.assertFalse((imported / "Secure Preferences").exists())

        altered = self.trial / "candidate/ZitterPersonal" / relative / "MANIFEST-000001"
        altered.write_bytes(b"changed data")
        rejected = self.root / "rejected"
        with self.assertRaisesRegex(ValueError, "changed after preparation"):
            import_workspace_profiles(self.trial, rejected, "workspace",
                                      selected_extensions=selected)
        self.assertFalse(rejected.exists())

    def test_workspace_import_preserves_multiple_extensions_atomically(self):
        ids = ["b" * 32, "d" * 32]
        profile = self.source / "ZitterWork"
        settings = {}
        permissions = {
            "api": ["storage"], "manifest_permissions": [],
            "explicit_host": [], "scriptable_host": [],
        }
        for index, extension_id in enumerate(ids):
            store = profile / "Local Extension Settings" / extension_id
            store.mkdir(parents=True)
            (store / "CURRENT").write_bytes(b"MANIFEST-000001\n")
            (store / "MANIFEST-000001").write_bytes(extension_id.encode())
            settings[extension_id] = {
                "active_permissions": permissions, "granted_permissions": permissions,
                "withholding_permissions": False, "disable_reasons": [1] if index else [],
            }
        (profile / "Secure Preferences").write_text(json.dumps({
            "extensions": {"settings": settings},
        }), encoding="utf-8")
        prepare(self.source, self.trial)
        target = self.root / "multiple"
        import_workspace_profiles(self.trial, target, "workspace",
                                  selected_extensions={"work": ids})
        imported = target / "workspace-work"
        self.assertFalse((imported / LEGACY_EXTENSION_RECORD).exists())
        for extension_id in ids:
            receipt = json.loads((imported / LEGACY_EXTENSION_RECORDS /
                                  f"{extension_id}.json").read_text(encoding="utf-8"))
            self.assertEqual(receipt["extension_id"], extension_id)
            self.assertEqual(receipt["disable_reasons"], settings[extension_id]["disable_reasons"])
            self.assertEqual((imported / "Local Extension Settings" / extension_id /
                              "MANIFEST-000001").read_bytes(), extension_id.encode())
        rejected = self.root / "rejected-multiple"
        with self.assertRaisesRegex(ValueError, "distinct Chromium extension IDs"):
            import_workspace_profiles(self.trial, rejected, "workspace",
                                      selected_extensions={"work": [ids[0], ids[0]]})
        (self.trial / "candidate/ZitterWork/Local Extension Settings" / ids[1] /
         "MANIFEST-000001").write_bytes(b"changed data")
        with self.assertRaisesRegex(ValueError, "changed after preparation"):
            import_workspace_profiles(self.trial, rejected, "workspace",
                                      selected_extensions={"work": ids})
        self.assertFalse(rejected.exists())

    def test_converts_clipboard_rules_into_separate_profiles(self):
        origin = "https://example.test:443,*"
        work = clipboard_preferences({origin: {"setting": 1, "last_modified": "123"},
                                      "http://[::1]:8200,*": {"setting": 2},
                                      "https://ask.test:443,*": {"setting": 3}})
        personal = clipboard_preferences({origin: {"setting": 2}})
        for profile, preferences in (("ZitterWork", work), ("ZitterPersonal", personal)):
            (self.source / profile / "Preferences").write_text(
                json.dumps(preferences), encoding="utf-8"
            )
        original = inventory(self.source)
        prepare(self.source, self.trial)
        for profile, expected in (
            ("work-profile", {"https://example.test": 1, "http://[::1]:8200": 2}),
            ("personal-profile", {"https://example.test": 2}),
        ):
            target = self.root / profile
            self.assertEqual(copy_clipboard_permissions(self.trial, profile, target), len(expected))
            self.assertEqual([path.name for path in target.iterdir()], [CLIPBOARD_PREFS])
            settings = {"clipboard_permissions": expected}
            if profile == "work-profile":
                settings["clipboard_permission_times"] = {"https://example.test": "123"}
            self.assertEqual(json.loads((target / CLIPBOARD_PREFS).read_text(encoding="utf-8")),
                             {"zitter": settings})
            with self.assertRaisesRegex(ValueError, "already exists"):
                copy_clipboard_permissions(self.trial, profile, target)
        rollback(self.trial, self.recovered)
        self.assertEqual(inventory(self.recovered), original)
        self.assertEqual(inventory(self.source), original)

    def test_refuses_clipboard_rules_that_cannot_preserve_legacy_behavior(self):
        for scope in ("[*.]example.test:443,*", "https://example.test:*,*",
                      "https://example.test:443,https://other.test:443",
                      "https://example.test:443/path,*", "http://example.test:80,*",
                      "http://127.1:80,*"):
            with self.subTest(scope=scope), self.assertRaises(ValueError):
                clipboard_rules_from_preferences(clipboard_preferences({scope: {"setting": 1}}))

        scope = "https://example.test:443,*"
        for record in ({"setting": True}, {"setting": 1, "model": 1},
                       {"setting": 1, "expiration": "123"}, {"setting": 1, "last_visit": "123"},
                       {"setting": 1, "decided_by_related_website_sets": True}):
            with self.subTest(record=record), self.assertRaises(ValueError):
                clipboard_rules_from_preferences(clipboard_preferences({scope: record}))

        for field in ("default_content_setting_values", "managed_default_content_settings",
                      "managed_clipboard_blocked_for_urls"):
            with self.subTest(field=field), self.assertRaises(ValueError):
                clipboard_rules_from_preferences(clipboard_preferences({}, **{field: {"clipboard": 2}}))

    def test_refuses_modified_or_ambiguous_clipboard_preferences(self):
        source = self.source / "ZitterWork/Preferences"
        source.write_text('{"profile":{},"profile":{}}', encoding="utf-8")
        prepare(self.source, self.trial)
        target = self.root / "content-profile"
        with self.assertRaisesRegex(ValueError, "Duplicate key"):
            copy_clipboard_permissions(self.trial, "work-profile", target)
        self.assertFalse(target.exists())

        candidate = self.trial / "candidate/ZitterWork/Preferences"
        candidate.write_text('{"profile":{},"unliked":{}}', encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "changed after preparation"):
            copy_clipboard_permissions(self.trial, "work-profile", target)
        self.assertFalse(target.exists())

    def test_copies_browser_storage_from_both_legacy_layouts(self):
        work = self.source / "ZitterWork"
        for name in ("Cookies", "Cookies-wal"):
            (work / "Network" / name).rename(work / name)
        local_storage = work / "Local Storage" / "leveldb"
        local_storage.mkdir(parents=True)
        (local_storage / "CURRENT").write_bytes(b"MANIFEST-000001\n")
        (local_storage / "000002.log").write_bytes(b"local storage fixture")
        prepare(self.source, self.trial)

        for profile in ("work", "personal"):
            target = self.root / profile
            copy_browser_storage(self.trial, f"{profile}-profile", target)
            self.assertEqual((target / "Network/Cookies").read_bytes(),
                             f"{profile}-profile".encode())
            self.assertEqual((target / "Network/Cookies-wal").read_bytes(),
                             b"pending cookie pages")
            self.assertFalse((target / "Login Data").exists())
            self.assertFalse((target / "Cookies").exists())
        self.assertEqual(inventory(self.root / "work/Local Storage"),
                         inventory(work / "Local Storage"))
        self.assertFalse((self.root / "personal/Local Storage").exists())

    def test_refuses_ambiguous_and_modified_browser_storage(self):
        (self.source / "ZitterWork/Cookies").write_bytes(b"second cookie database")
        prepare(self.source, self.trial)
        target = self.root / "content"
        with self.assertRaisesRegex(ValueError, "two cookie databases"):
            copy_browser_storage(self.trial, "work-profile", target)
        self.assertFalse(target.exists())

        cookie = self.trial / "candidate/ZitterPersonal/Network/Cookies"
        cookie.write_bytes(b"changed cookie database")
        with self.assertRaisesRegex(ValueError, "changed after preparation"):
            copy_browser_storage(self.trial, "personal-profile", target)
        self.assertFalse(target.exists())

    def test_workspace_import_combines_stores_using_saved_associations(self):
        self.config["workspaces"][0]["profile_id"] = "personal-profile"
        self.config["workspaces"][1]["profile_id"] = "work-profile"
        self.write_config()
        origin = "https://example.test:443,*"
        for folder, setting in (("ZitterWork", 1), ("ZitterPersonal", 2)):
            (self.source / folder / "Preferences").write_text(
                json.dumps(clipboard_preferences({origin: {"setting": setting}})),
                encoding="utf-8",
            )
        original = inventory(self.source)
        prepare(self.source, self.trial)
        target = self.root / "workspace-import"

        import_workspace_profiles(self.trial, target, "mock-keychain")
        session_file = "ContentMockKeychainWorkspaceSession.json"
        self.assertEqual({path.name for path in target.iterdir()},
                         {"mock-keychain-work", "mock-keychain-personal", PREPARED_WORKSPACE_MARKER,
                          session_file})
        session = json.loads((target / session_file).read_text(encoding="utf-8"))
        self.assertEqual(session["profile_names"], {"work": "Personal", "personal": "Work"})
        self.assertEqual((target / PREPARED_WORKSPACE_MARKER).read_bytes(),
                         b"zitter-content-workspace-v1 mock-keychain\n")
        for workspace, owner, setting in (("work", "personal", 2), ("personal", "work", 1)):
            profile = target / f"mock-keychain-{workspace}"
            self.assertEqual((profile / "Network/Cookies").read_bytes(), f"{owner}-profile".encode())
            self.assertEqual(json.loads((profile / CLIPBOARD_PREFS).read_text(encoding="utf-8")),
                             {"zitter": {"clipboard_permissions": {"https://example.test": setting}}})
        self.assertEqual(inventory(self.trial / "candidate"), original)
        self.assertEqual(inventory(self.source), original)

    def test_workspace_import_remaps_assistant_history_and_pauses_tasks(self):
        self.config["workspaces"][0]["profile_id"] = "personal-profile"
        self.config["workspaces"][1]["profile_id"] = "work-profile"
        self.write_config()
        conversation = {
            "key": "legacy-work", "profile": "work-profile", "title": "A conversation",
            "provider": "ollama", "model": "local", "updated": 1750000000000,
            "auto_confirm": True, "confirm_background": False,
            "permissions": {"tabs": True, "confirm_clicks": False},
            "messages": [
                {"kind": 1, "text": "Ask", "chips": [
                    {"kind": "tab", "ref": "@tab:expired", "title": "Old tab", "color": 0xff112233},
                ]},
                {"kind": 0, "text": "Answer"},
                {"kind": 5, "text": "Waiting for confirmation", "answer": ""},
            ],
        }
        task = {
            "id": "task", "profile": "personal-profile", "name": "Daily",
            "instruction": "List tabs", "provider": "ollama", "model": "local",
            "active": True, "last_run": 1750000000000, "next_run": 1750001000000,
            "kind": 1, "days": 31, "minute": 420, "last_state": 1,
        }
        (self.source / ASSISTANT_SESSIONS_FILE).write_text(json.dumps({
            "version": 1,
            "sessions": [conversation, {**conversation, "key": "unmapped", "profile": "other"}],
            "tasks": [task],
        }), encoding="utf-8")
        original = inventory(self.source)
        prepare(self.source, self.trial)
        target = self.root / "workspace-import"
        import_workspace_profiles(self.trial, target, "workspace")

        data = json.loads((target / ASSISTANT_SESSIONS_FILE).read_text(encoding="utf-8"))
        self.assertEqual(len(data["sessions"]), 1)
        imported = data["sessions"][0]
        self.assertEqual(imported["profile"], "personal")
        self.assertEqual(imported["messages"][0]["chips"][0]["title"], "Old tab")
        self.assertEqual(imported["messages"][0]["chips"][0]["ref"], "")
        self.assertEqual(imported["messages"][1]["text"], "Answer")
        self.assertEqual(imported["messages"][2]["answer"], "—")
        self.assertNotIn("auto_confirm", imported)
        self.assertTrue(imported["confirm_background"])
        self.assertEqual(imported["permissions"], {"tabs": True, "confirm_clicks": True})
        self.assertEqual(data["tasks"], [{**task, "profile": "work", "active": False,
                                          "next_run": 0, "permissions": {"confirm_clicks": True}}])
        self.assertEqual(inventory(self.source), original)
        self.assertEqual(inventory(self.trial / "candidate"), original)

    def test_workspace_import_refuses_changed_assistant_history(self):
        file = self.source / ASSISTANT_SESSIONS_FILE
        file.write_text('{"version":1,"sessions":[]}', encoding="utf-8")
        prepare(self.source, self.trial)
        (self.trial / "candidate" / file.name).write_text(
            '{"version":1,"sessions":{}}', encoding="utf-8")
        target = self.root / "workspace-import"
        with self.assertRaisesRegex(ValueError, "Assistant sessions changed"):
            import_workspace_profiles(self.trial, target, "workspace")
        self.assertFalse(target.exists())

    def test_workspace_import_refuses_assistant_history_above_the_restore_limit(self):
        file = self.source / ASSISTANT_SESSIONS_FILE
        file.write_text(json.dumps({
            "version": 1,
            "sessions": [{"key": f"legacy-{index}", "profile": "work-profile",
                          "messages": [{"kind": 1, "text": "Question"}]}
                         for index in range(MAX_ASSISTANT_CONVERSATIONS + 1)],
        }), encoding="utf-8")
        original = inventory(self.source)
        prepare(self.source, self.trial)
        target = self.root / "workspace-import"
        with self.assertRaisesRegex(ValueError, "Content conversation limit"):
            import_workspace_profiles(self.trial, target, "workspace")
        self.assertFalse(target.exists())
        self.assertEqual(inventory(self.source), original)

    def test_workspace_import_preserves_assistant_settings_without_a_session(self):
        credential = base64.b64encode(b"v10" + bytes(range(32))).decode()
        self.config["assistant"] = {
            "enabled": False, "backends": [True, False, True, False],
            "catalog_version": 1, "default_model": "codex/gpt-5.4",
            "models": [{"provider": "codex", "id": "gpt-5.4", "visible": False,
                        "api_key": "nested secret"},
                       {"provider": "unknown", "id": "ignored"}],
            "encrypted_api_key": credential, "key": "plaintext secret",
            "permissions": {"pages": False, "confirm_clicks": True, "grant": True},
        }
        self.write_config()
        original = inventory(self.source)
        prepare(self.source, self.trial)
        target = self.root / "workspace-import"

        import_workspace_profiles(self.trial, target, "workspace")

        session = json.loads((target / "ContentWorkspaceSession.json").read_text())
        self.assertEqual(session["assistant"], {
            "enabled": False, "backends": [True, False, True, False],
            "catalog_version": 1, "default_model": "codex/gpt-5.4",
            "models": [{"provider": "codex", "id": "gpt-5.4", "visible": False}],
            "encrypted_api_key": credential,
            "permissions": {"pages": False, "confirm_clicks": True},
        })
        self.assertEqual(inventory(self.source), original)
        self.assertEqual(inventory(self.trial / "candidate"), original)

    def test_workspace_import_omits_unrecognized_assistant_credential(self):
        self.config["assistant"] = {
            "enabled": False,
            "encrypted_api_key": base64.b64encode(b"pretend plaintext key").decode(),
        }
        self.write_config()
        prepare(self.source, self.trial)
        target = self.root / "workspace-import"

        import_workspace_profiles(self.trial, target, "workspace")

        session = json.loads((target / "ContentWorkspaceSession.json").read_text())
        self.assertEqual(session["assistant"], {"enabled": False})

    def test_workspace_import_keeps_default_zoom_per_profile(self):
        for folder, level in (("ZitterWork", 1.25), ("ZitterPersonal", -0.5)):
            (self.source / folder / "Preferences").write_text(
                json.dumps({"partition": {"default_zoom_level": {"x": level, "x1234": 3}}}),
                encoding="utf-8",
            )
        prepare(self.source, self.trial)
        target = self.root / "workspace-import"
        import_workspace_profiles(self.trial, target, "workspace")
        for folder, level in (("workspace-work", 1.25), ("workspace-personal", -0.5)):
            self.assertEqual(json.loads((target / folder / PAGE_ZOOM_PREFS).read_bytes()),
                             {"partition": {"default_zoom_level": level}})

    def test_workspace_import_preserves_site_zoom_and_last_modified(self):
        preferences = {
            "partition": {
                "default_zoom_level": {"x": 1},
                "per_host_zoom_levels": {
                    "x": {
                        "news.example": {"zoom_level": 2, "last_modified": "13370000000000000"},
                        "legacy.example": -1,
                        "same-as-default.example": 1,
                        "invalid.example": True,
                        "": 2,
                    },
                    "x1234": {"other-partition.example": 2},
                },
            },
        }
        (self.source / "ZitterWork" / "Preferences").write_text(
            json.dumps(preferences), encoding="utf-8",
        )
        prepare(self.source, self.trial)
        target = self.root / "workspace-import"
        import_workspace_profiles(self.trial, target, "workspace")
        self.assertEqual(
            json.loads((target / "workspace-work" / PAGE_ZOOM_PREFS).read_bytes()),
            {"partition": {"default_zoom_level": 1.0, "per_host_zoom_levels": {
                "news.example": {"zoom_level": 2.0, "last_modified": "13370000000000000"},
                "legacy.example": {"zoom_level": -1.0},
            }}},
        )
        self.assertFalse((target / "workspace-personal" / PAGE_ZOOM_PREFS).exists())

    def test_workspace_import_refuses_invalid_default_zoom_before_publish(self):
        for index, invalid in enumerate((True, "125%", 100)):
            with self.subTest(invalid=invalid):
                (self.source / "ZitterPersonal" / "Preferences").write_text(
                    json.dumps({"partition": {"default_zoom_level": {"x": invalid}}}),
                    encoding="utf-8",
                )
                trial = self.root / f"zoom-trial-{index}"
                target = self.root / f"zoom-import-{index}"
                prepare(self.source, trial)
                with self.assertRaisesRegex(ValueError, "Invalid legacy default page zoom"):
                    import_workspace_profiles(trial, target, "workspace")
                self.assertFalse(target.exists())

    def test_workspace_import_cleans_up_after_second_profile_fails(self):
        prepare(self.source, self.trial)
        target = self.root / "workspace-import"
        target.mkdir()
        with self.assertRaisesRegex(ValueError, "already exists"):
            import_workspace_profiles(self.trial, target, "workspace")
        self.assertEqual(list(target.iterdir()), [])
        target.rmdir()

        personal_cookie = self.trial / "candidate/ZitterPersonal/Network/Cookies"
        personal_cookie.write_bytes(b"altered after preparation")
        with self.assertRaisesRegex(ValueError, "changed after preparation"):
            import_workspace_profiles(self.trial, target, "workspace")
        self.assertFalse(target.exists())
        self.assertEqual(list(self.root.glob(".zitter-import-*")), [])

    def test_workspace_import_preserves_shared_and_additional_profiles(self):
        profile = "b812efb2-315c-4125-bef2-26284bc5e58b"
        space = "4ba4cf09-472d-4ddd-ae25-7f099a2127f1"
        directory = f"ZitterProfile-{profile}"
        self.config["profiles"].append({"id": profile, "name": "Research", "directory": directory})
        self.config["workspaces"][1]["profile_id"] = "work-profile"
        self.config["workspaces"].append({"id": space, "name": "Lab", "profile_id": profile})
        self.config["profiles"][1]["pins"] = [{
            "id": "saved", "title": "Unassigned favorite", "url": "https://example.test/",
        }]
        self.config["active"] = space
        self.config["link_rules"] = [{"host": "example.test", "workspace_id": space}]
        self.write_config()
        network = self.source / directory / "Network"
        network.mkdir(parents=True)
        (network / "Cookies").write_bytes(b"third profile")
        tab = {"url": "https://example.test/", "title": "Research"}
        (self.source / "ZitterSession.json").write_text(json.dumps({
            space: {"profile_id": profile, "active": 0, "tabs": [tab]},
        }), encoding="utf-8")
        (self.source / "ZitterTimeline.jsonl").write_text(json.dumps({
            "t": 42, "w": space, "p": profile, "a": 0, "tabs": [[tab["url"], tab["title"]]],
        }) + "\n", encoding="utf-8")
        (self.source / "ZitterAppearance.json").write_text(json.dumps({
            "theme": "dark", "sidebar_width": 320, space: "#345678",
            "workspace_emoji": {space: "★", "missing": "x"},
        }), encoding="utf-8")
        browser_tools = {"developer_mode": "off", "mouse_gestures_enabled": False}
        (self.source / "ZitterTools.json").write_text(json.dumps(browser_tools), encoding="utf-8")
        target = self.root / "workspace-import"
        self.assertEqual(prepare_macos_upgrade(self.source, self.trial, target), 3)
        mapped_space, mapped_profile = f"space-{space}", f"profile-{profile}"
        session = json.loads((target / "ContentKeychainWorkspaceSession.json").read_text())
        self.assertEqual(session["active_workspace"], mapped_space)
        self.assertEqual(session["link_rules"], {"example.test": mapped_space})
        self.assertEqual(session["personal"]["profile_id"], "work")
        self.assertEqual(session["profile_favorites"]["personal"][0]["id"], "saved")
        self.assertEqual(session[mapped_space]["profile_id"], mapped_profile)
        self.assertEqual(session[mapped_space]["tabs"], [tab])
        self.assertEqual(session["extra_profiles"], [{"id": mapped_profile, "name": "Research"}])
        self.assertEqual(session["extra_workspaces"], [{"id": mapped_space, "name": "Lab"}])
        timeline = json.loads((target / "ContentKeychainWorkspaceSession.timeline.jsonl").read_text())
        self.assertEqual((timeline["w"], timeline["p"]), (mapped_space, mapped_profile))
        settings = json.loads((target / "KeychainWorkspaceSettings.json").read_text())
        self.assertEqual(settings, {
            "theme": "dark", "sidebar_width": 320, mapped_space: "#345678",
            "workspace_emoji": {mapped_space: "★"},
        })
        self.assertEqual(json.loads((target / "ZitterTools.json").read_text()), browser_tools)
        self.assertEqual((target / mapped_profile / "Network/Cookies").read_bytes(), b"third profile")
        self.assertEqual((target / "keychain-personal/Network/Cookies").read_bytes(), b"personal-profile")

    def test_macos_upgrade_keeps_a_single_personal_workspace(self):
        self.config["profiles"] = self.config["profiles"][1:]
        self.config["workspaces"] = self.config["workspaces"][1:]
        self.config["active"] = "personal"
        self.write_config()
        tab = {"url": "https://example.test/", "title": "Personal"}
        (self.source / "ZitterSession.json").write_text(json.dumps({
            "personal": {"profile_id": "personal-profile", "active": 0, "tabs": [tab]},
        }), encoding="utf-8")
        target = self.root / "workspace-import"
        self.assertEqual(prepare_macos_upgrade(self.source, self.trial, target), 1)
        session = json.loads((target / "ContentKeychainWorkspaceSession.json").read_text())
        self.assertEqual(session["active_workspace"], "personal")
        self.assertEqual(session["removed_workspaces"], {"work": "work"})
        self.assertNotIn("work", session)
        self.assertEqual(session["personal"]["tabs"], [tab])
        self.assertEqual(list((target / "keychain-work").iterdir()), [])
        self.assertEqual((target / "keychain-personal/Network/Cookies").read_bytes(),
                         b"personal-profile")
        rollback(self.trial, self.recovered)
        self.assertEqual(inventory(self.recovered), inventory(self.source))

    def test_workspace_import_restores_tab_order_and_active_workspace(self):
        self.config["active"] = "personal"
        self.write_config()
        tabs = [{"url": "https://example.test/%C5%81", "title": "Łódź"},
                {"url": "http://[::1]:8000/", "title": "Loopback"},
                {"url": "zitter://rss/", "title": "RSS"}]
        rss = b'{"feeds":[{"url":"https://example.test/feed","articles":[{"id":"a","saved":true}]}]}'
        (self.source / "ZitterWork" / "ZitterRSS.json").write_bytes(rss)
        saved = {"work": {"profile_id": "work-profile", "active": 1,
                          "split": [], "weights": [], "tabs": tabs}}
        (self.source / "ZitterSession.json").write_text(json.dumps(saved), encoding="utf-8")
        prepare(self.source, self.trial)

        target = self.root / "workspace-import"
        import_workspace_profiles(self.trial, target, "keychain")
        result = json.loads((target / "ContentKeychainWorkspaceSession.json").read_text(encoding="utf-8"))
        self.assertEqual((target / "keychain-work" / "ZitterRSS.json").read_bytes(), rss)
        self.assertFalse((target / "keychain-personal" / "ZitterRSS.json").exists())
        self.assertEqual(result, {"active_workspace": "personal",
                                  "work": {"profile_id": "work", "active": 1, "tabs": tabs},
                                  "personal": {"profile_id": "personal", "active": 0,
                                               "tabs": [{"url": "", "title": ""}]}})
        candidate_rss = self.trial / "candidate" / "ZitterWork" / "ZitterRSS.json"
        candidate_rss.write_bytes(rss.replace(b'"id":"a"', b'"id":"b"'))
        rejected = self.root / "rejected-rss"
        with self.assertRaisesRegex(ValueError, "RSS state changed after preparation"):
            import_workspace_profiles(self.trial, rejected, "keychain")
        self.assertFalse(rejected.exists())

    def test_workspace_import_converts_both_legacy_split_formats(self):
        tabs = [{"url": f"https://example.test/{index}", "title": str(index)}
                for index in range(3)]
        saved = {"work": {"profile_id": "work-profile", "active": 1, "tabs": tabs,
                          "left": 2, "right": 1, "ratio": 0.25},
                 "personal": {"profile_id": "personal-profile", "active": 0, "tabs": tabs,
                              "split": [2, 0, 1], "weights": [2, 1, 1], "vertical": True}}
        (self.source / "ZitterSession.json").write_text(json.dumps(saved), encoding="utf-8")
        prepare(self.source, self.trial)
        target = self.root / "workspace-import"
        import_workspace_profiles(self.trial, target, "workspace")
        result = json.loads((target / "ContentWorkspaceSession.json").read_text(encoding="utf-8"))
        self.assertEqual(result["work"]["split"],
                         {"tabs": [2, 1], "weights": [0.25, 0.75], "vertical": False})
        self.assertEqual(result["personal"]["split"],
                         {"tabs": [2, 0, 1], "weights": [2, 1, 1], "vertical": True})

    def create_history(self, profile, version):
        path = self.source / profile / "History"
        with closing(sqlite3.connect(path)) as db, db:
            db.executescript("""
                CREATE TABLE meta(key LONGVARCHAR PRIMARY KEY, value LONGVARCHAR);
                CREATE TABLE urls(id INTEGER PRIMARY KEY, url TEXT, title TEXT);
                CREATE TABLE visits(id INTEGER PRIMARY KEY, url INTEGER, visit_time INTEGER);
                CREATE TABLE downloads(id INTEGER PRIMARY KEY, target_path TEXT);
                CREATE TABLE downloads_url_chains(id INTEGER, url TEXT);
                CREATE TABLE downloads_slices(id INTEGER, offset INTEGER);
                INSERT INTO urls VALUES(1, 'https://example.test/', 'Example');
                INSERT INTO visits VALUES(1, 1, 123456);
                INSERT INTO downloads VALUES(1, '/disposable/legacy.bin');
                INSERT INTO downloads_url_chains VALUES(1, 'https://example.test/download');
                INSERT INTO downloads_slices VALUES(1, 100);
            """)
            db.execute("INSERT INTO meta VALUES ('version', ?)", (version,))
        return path

    def test_workspace_import_preserves_history_without_supported_download_columns(self):
        history = self.create_history("ZitterWork", 70)
        original = history.read_bytes()
        prepare(self.source, self.trial)
        target = self.root / "workspace-import"
        import_workspace_profiles(self.trial, target, "workspace")

        with closing(sqlite3.connect(target / "workspace-work/History")) as db:
            self.assertEqual(db.execute("SELECT * FROM urls").fetchall(),
                             [(1, "https://example.test/", "Example")])
            self.assertEqual(db.execute("SELECT * FROM visits").fetchall(), [(1, 1, 123456)])
            self.assertEqual(db.execute("SELECT * FROM downloads").fetchall(), [])
            self.assertEqual(db.execute("SELECT * FROM downloads_url_chains").fetchall(), [])
            self.assertEqual(db.execute("SELECT * FROM downloads_slices").fetchall(), [])
        self.assertFalse((target / "workspace-personal/History").exists())
        self.assertEqual(history.read_bytes(), original)
        self.assertEqual((self.trial / "candidate/ZitterWork/History").read_bytes(), original)

    def test_download_history_rejects_unsafe_records_and_transfer_state(self):
        with closing(sqlite3.connect(":memory:")) as db:
            db.executescript("""
                CREATE TABLE downloads(id INTEGER PRIMARY KEY, guid TEXT, state INTEGER,
                    transient INTEGER, danger_type INTEGER, interrupt_reason INTEGER,
                    current_path TEXT, target_path TEXT, received_bytes INTEGER, total_bytes INTEGER,
                    embedder_download_data TEXT, hash BLOB, http_method TEXT, etag TEXT,
                    last_modified TEXT);
                CREATE TABLE downloads_url_chains(id INTEGER, chain_index INTEGER, url TEXT);
                CREATE TABLE downloads_slices(download_id INTEGER);
            """)
            for row_id, state, danger, reason, target, url, offset in (
                (1, 1, 0, 0, "/Downloads/safe.txt", "https://example.test/file", 0),
                (2, 4, 0, 22, "/Downloads/partial.txt", "https://example.test/file", 0),
                (3, 1, 6, 0, "/Downloads/bypassed.txt", "https://example.test/file", 0),
                (4, 1, 0, 0, "/Downloads/../unsafe.txt", "https://example.test/file", 0),
                (5, 1, 0, 0, "/Downloads/bad-source.txt", "javascript:1", 0),
                (6, 1, 0, 0, "/Downloads/incomplete-chain.txt", "https://example.test/file", 1),
                (7, 2, 0, 40, "/Downloads/cancelled.txt", "https://example.test/file", 0),
                (8, 4, 0, 0, "/Downloads/invalid.txt", "https://example.test/file", 0),
                (9, 0, 0, 0, "/Downloads/active.txt", "https://example.test/file", 0),
            ):
                db.execute("INSERT INTO downloads VALUES (?, ?, ?, 0, ?, ?, ?, ?, 7, 7, "
                           "'old', X'FF', 'GET', 'etag', 'old')",
                           (row_id, f"a987fbc9-4bed-4078-8f07-00000000000{row_id}",
                            state, danger, reason, target, target))
                db.execute("INSERT INTO downloads_url_chains VALUES (?, ?, ?)", (row_id, offset, url))
                db.execute("INSERT INTO downloads_slices VALUES (?)", (row_id,))
            retain_download_history(db, windows=False)
            self.assertEqual(db.execute(
                "SELECT id, state, interrupt_reason, current_path, embedder_download_data, "
                "hash, http_method, etag, last_modified FROM downloads"
            ).fetchall(), [
                (1, 1, 0, "/Downloads/safe.txt", IMPORTED_DOWNLOAD_MARKER, b"", "", "", ""),
                (2, 4, 22, "", IMPORTED_DOWNLOAD_MARKER, b"", "", "", ""),
                (7, 2, 40, "", IMPORTED_DOWNLOAD_MARKER, b"", "", "", ""),
            ])
            self.assertEqual(db.execute("SELECT id FROM downloads_url_chains").fetchall(),
                             [(1,), (2,), (7,)])
            self.assertEqual(db.execute("SELECT * FROM downloads_slices").fetchall(), [])
        self.assertTrue(is_local_download_path(r"C:\Downloads\report.pdf", windows=True))
        self.assertFalse(is_local_download_path(r"\\server\share\report.pdf", windows=True))
        self.assertFalse(is_local_download_path(r"C:\Downloads\report.pdf:stream", windows=True))

    def test_workspace_import_preserves_bounded_favicon_bitmaps_per_profile(self):
        for directory, icon in (("ZitterWork", b"work"), ("ZitterPersonal", b"personal")):
            path = self.source / directory / "Favicons"
            with closing(sqlite3.connect(path)) as db, db:
                db.executescript("""
                    CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT);
                    INSERT INTO meta VALUES ('version', '9');
                    CREATE TABLE icon_mapping(id INTEGER PRIMARY KEY, page_url TEXT, icon_id INTEGER);
                    CREATE TABLE favicons(id INTEGER PRIMARY KEY, url TEXT);
                    CREATE TABLE favicon_bitmaps(id INTEGER PRIMARY KEY, icon_id INTEGER,
                                                 image_data BLOB, width INTEGER, height INTEGER);
                    INSERT INTO icon_mapping VALUES (1, 'https://example.test/', 1);
                    INSERT INTO favicons VALUES (1, 'https://example.test/favicon.ico');
                    INSERT INTO favicon_bitmaps VALUES (2, 1, X'AB', 4096, 32);
                """)
                db.execute("INSERT INTO favicon_bitmaps VALUES (1, 1, ?, 32, 32)", (icon,))
        original = inventory(self.source)
        prepare(self.source, self.trial)
        target = self.root / "workspace-import"
        import_workspace_profiles(self.trial, target, "workspace")
        for role in ("work", "personal"):
            with closing(sqlite3.connect(target / f"workspace-{role}" / "Favicons")) as db:
                self.assertEqual(db.execute("SELECT image_data FROM favicon_bitmaps").fetchall(),
                                 [(role.encode(),)])
                self.assertEqual(db.execute("SELECT page_url FROM icon_mapping").fetchall(),
                                 [("https://example.test/",)])
        self.assertEqual(inventory(self.source), original)

    def test_workspace_import_refuses_unknown_history_version_without_publishing(self):
        self.create_history("ZitterPersonal", 71)
        prepare(self.source, self.trial)
        target = self.root / "workspace-import"
        with self.assertRaisesRegex(ValueError, "Unsupported legacy History schema"):
            import_workspace_profiles(self.trial, target, "workspace")
        self.assertFalse(target.exists())

    def test_workspace_import_preserves_visits_in_history_wal(self):
        history = self.create_history("ZitterWork", 70)
        writer = self.root / "writer.sqlite"
        history.rename(writer)
        with closing(sqlite3.connect(writer)) as db:
            self.assertEqual(db.execute("PRAGMA journal_mode=WAL").fetchone(), ("wal",))
            with db:
                db.execute("INSERT INTO visits VALUES(2, 1, 789012)")
            for suffix in ("", "-wal", "-shm"):
                shutil.copy2(writer.with_name(writer.name + suffix),
                             history.with_name(history.name + suffix))

        prepare(self.source, self.trial)
        target = self.root / "workspace-import"
        import_workspace_profiles(self.trial, target, "workspace")

        imported = target / "workspace-work/History"
        self.assertFalse(imported.with_name("History-wal").exists())
        with closing(sqlite3.connect(imported)) as db:
            self.assertEqual(db.execute("SELECT visit_time FROM visits ORDER BY id").fetchall(),
                             [(123456,), (789012,)])

    def test_workspace_import_copies_login_stores_and_only_password_saving_preference(self):
        work = self.source / "ZitterWork"
        personal = self.source / "ZitterPersonal"
        (work / "Login Data").write_bytes(b"work login store")
        (personal / "Login Data").write_bytes(b"personal login store")
        (personal / "Login Data-wal").write_bytes(b"personal login WAL")
        (work / "Preferences").write_text(json.dumps({
            "credentials_enable_service": False, "unrelated_setting": "legacy-only",
        }), encoding="utf-8")
        prepare(self.source, self.trial)
        target = self.root / "workspace-import"
        import_workspace_profiles(self.trial, target, "workspace")

        work = target / "workspace-work"
        personal = target / "workspace-personal"
        self.assertEqual((work / "Login Data").read_bytes(), b"work login store")
        self.assertEqual((personal / "Login Data").read_bytes(), b"personal login store")
        self.assertEqual((personal / "Login Data-wal").read_bytes(), b"personal login WAL")
        self.assertEqual(json.loads((work / PASSWORD_PREFS).read_text(encoding="utf-8")),
                         {"credentials_enable_service": False})
        self.assertFalse((work / "Preferences").exists())
        self.assertFalse((personal / PASSWORD_PREFS).exists())

    def seed_windows_stores(self):
        for name in ("Work", "Personal"):
            profile = self.source / f"Zitter{name}"
            cookies = profile / "Network/Cookies"
            cookies.unlink()
            cookies.with_name("Cookies-wal").unlink()
            with closing(sqlite3.connect(cookies)) as db, db:
                db.execute("CREATE TABLE cookies(encrypted_value BLOB)")
                db.execute("INSERT INTO cookies VALUES (?)", (f"v10-cookie-{name}".encode(),))
            logins = profile / "Login Data"
            logins.unlink()
            with closing(sqlite3.connect(logins)) as db, db:
                db.execute("CREATE TABLE logins(password_value BLOB)")
                db.execute("INSERT INTO logins VALUES (?)", (f"v10-password-{name}".encode(),))

    def test_windows_import_stages_only_the_wrapped_dpapi_key(self):
        self.seed_windows_stores()
        wrapped = base64.b64encode(b"DPAPI\x00synthetic wrapped key").decode()
        (self.source / "Local State").write_text(json.dumps({
            "os_crypt": {"encrypted_key": wrapped, "audit_enabled": True},
            "unrelated": True,
        }), encoding="utf-8")
        source_snapshot = inventory(self.source)
        prepare(self.source, self.trial)
        target = self.root / "workspace-import"
        import_workspace_profiles(self.trial, target, "windows-dpapi")
        self.assertEqual((target / PREPARED_WORKSPACE_MARKER).read_bytes(),
                         b"zitter-content-workspace-v1 windows-dpapi\n")

        self.assertEqual(json.loads((target / WINDOWS_LOCAL_STATE).read_text(encoding="utf-8")),
                         {"os_crypt": {"encrypted_key": wrapped}})
        self.assertFalse((target / "Local State").exists())
        for name in ("Work", "Personal"):
            for store in ("Network/Cookies", "Login Data"):
                self.assertEqual((target / f"workspace-{name.lower()}" / store).read_bytes(),
                                 (self.source / f"Zitter{name}" / store).read_bytes())
        self.assertEqual(inventory(self.trial / "candidate"), source_snapshot)
        self.assertEqual(inventory(self.source), source_snapshot)

    def test_windows_inspection_reports_envelopes_and_reads_verified_wal(self):
        self.seed_windows_stores()
        wrapped = base64.b64encode(b"DPAPI\x00synthetic wrapped key").decode()
        (self.source / "Local State").write_text(json.dumps({
            "os_crypt": {"encrypted_key": wrapped, "app_bound_encrypted_key": "APPB"},
        }), encoding="utf-8")
        logins = self.source / "ZitterPersonal/Login Data"
        with closing(sqlite3.connect(logins)) as db:
            self.assertEqual(db.execute("PRAGMA journal_mode=WAL").fetchone(), ("wal",))
            db.execute("PRAGMA wal_autocheckpoint=0")
            with db:
                db.executemany("INSERT INTO logins VALUES (?)",
                               [(b"v20-secret",), (b"",), (b"other-secret",)])
            prepare(self.source, self.trial)
        candidate = self.trial / "candidate"
        before = inventory(candidate)
        report = inspect_windows_encryption(self.trial)
        self.assertEqual(report, {
            "dpapi_key": "valid-envelope",
            "app_bound_key_present": True,
            "profiles": [
                {"workspace_roles": ["work"], "cookies": {"v10": 1}, "logins": {"v10": 1}},
                {"workspace_roles": ["personal"], "cookies": {"v10": 1},
                 "logins": {"v10": 1, "v20": 1, "empty": 1, "other_nonempty": 1}},
            ],
        })
        self.assertEqual(inventory(candidate), before)
        wal = candidate / "ZitterPersonal/Login Data-wal"
        wal.write_bytes(wal.read_bytes() + b"changed")
        with self.assertRaisesRegex(ValueError, "Login Data database changed"):
            inspect_windows_encryption(self.trial)

    def test_windows_inspection_reports_uninitialized_encryption_settings(self):
        self.seed_windows_stores()
        (self.source / "Local State").write_text("{}", encoding="utf-8")
        prepare(self.source, self.trial)
        report = inspect_windows_encryption(self.trial)
        self.assertEqual(report["dpapi_key"], "missing")
        self.assertFalse(report["app_bound_key_present"])

    def test_windows_import_rejects_app_bound_state_or_ciphertext(self):
        self.seed_windows_stores()
        local_state = self.source / "Local State"
        wrapped = base64.b64encode(b"DPAPI\x00synthetic wrapped key").decode()
        local_state.write_text(json.dumps({
            "os_crypt": {"encrypted_key": wrapped, "app_bound_encrypted_key": "APPB"},
        }), encoding="utf-8")
        prepare(self.source, self.trial)
        target = self.root / "workspace-import"
        with self.assertRaisesRegex(ValueError, "App-Bound Encryption"):
            import_workspace_profiles(self.trial, target, "windows-dpapi")
        self.assertFalse(target.exists())

        local_state.write_text(json.dumps({"os_crypt": {"encrypted_key": wrapped}}),
                               encoding="utf-8")
        with closing(sqlite3.connect(self.source / "ZitterPersonal/Login Data")) as db, db:
            db.execute("UPDATE logins SET password_value = ?", (b"v20-app-bound",))
        second_trial = self.root / "app-bound-row-trial"
        prepare(self.source, second_trial)
        with self.assertRaisesRegex(ValueError, "App-Bound Encryption"):
            import_workspace_profiles(second_trial, target, "windows-dpapi")
        self.assertFalse(target.exists())

    def test_windows_import_rejects_an_invalid_or_changed_wrapped_key(self):
        prepare(self.source, self.trial)
        target = self.root / "workspace-import"
        with self.assertRaisesRegex(ValueError, "Malformed legacy Windows DPAPI key"):
            import_workspace_profiles(self.trial, target, "windows-dpapi")
        self.assertFalse(target.exists())
        (self.trial / "candidate/Local State").write_text('{"os_crypt":{}}', encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "Local State"):
            import_workspace_profiles(self.trial, target, "windows-dpapi")
        self.assertFalse(target.exists())

    def test_workspace_import_refuses_orphaned_login_sidecar_without_publishing(self):
        (self.source / "ZitterPersonal/Login Data").unlink()
        (self.source / "ZitterPersonal/Login Data-wal").write_bytes(b"orphaned WAL")
        prepare(self.source, self.trial)
        target = self.root / "workspace-import"
        with self.assertRaisesRegex(ValueError, "Login Data sidecars have no database"):
            import_workspace_profiles(self.trial, target, "workspace")
        self.assertFalse(target.exists())

    def test_workspace_import_preserves_closed_pins_without_a_saved_session(self):
        self.config["profiles"][1]["pins"] = [
            {"id": "favorite", "title": "Personal favorite", "url": "https://personal.test/"},
        ]
        self.config["workspaces"][0]["pins"] = [
            {"id": "folder", "title": "Work folder", "url": "", "parent_id": "", "folder": True},
            {"id": "pin", "title": "Work page", "url": "https://work.test/",
             "parent_id": "folder", "folder": False},
        ]
        self.write_config()
        prepare(self.source, self.trial)
        target = self.root / "workspace-import"
        import_workspace_profiles(self.trial, target, "workspace")
        session = json.loads((target / "ContentWorkspaceSession.json").read_text(encoding="utf-8"))
        self.assertEqual(session["work"]["tabs"], [{"url": "", "title": ""}])
        self.assertEqual(session["work"]["pins"], [
            {**pin, "profile_pin": False} for pin in self.config["workspaces"][0]["pins"]
        ])
        self.assertEqual(session["personal"]["pins"], [
            {**self.config["profiles"][1]["pins"][0], "parent_id": "",
             "folder": False, "profile_pin": True},
        ])

    def test_workspace_import_preserves_popup_defaults_and_exact_rules(self):
        for directory, default, rule in (("ZitterWork", 2, 1), ("ZitterPersonal", 1, 2)):
            (self.source / directory / "Preferences").write_text(json.dumps({
                "profile": {
                    "default_content_setting_values": {"popups": default},
                    "content_settings": {"exceptions": {"popups": {
                        "http://example.test:80,*": {"setting": rule, "last_modified": "123"},
                    }}},
                },
            }), encoding="utf-8")
        prepare(self.source, self.trial)
        target = self.root / "workspace-import"
        import_workspace_profiles(self.trial, target, "workspace")
        for profile, allowed, rule in (("work", False, 1), ("personal", True, 2)):
            prefs = json.loads((target / f"workspace-{profile}" / CLIPBOARD_PREFS).read_bytes())
            self.assertEqual(prefs["zitter"]["popup_permissions"], {"http://example.test": rule})
            self.assertEqual(prefs["zitter"]["popup_permission_times"],
                             {"http://example.test": "123"})
            self.assertEqual(prefs["zitter"].get("popups_allowed", False), allowed)

    def test_workspace_import_rejects_a_broad_popup_allow_rule(self):
        (self.source / "ZitterWork" / "Preferences").write_text(json.dumps({
            "profile": {"content_settings": {"exceptions": {"popups": {
                "[*.]example.test,*": {"setting": 1},
            }}}},
        }), encoding="utf-8")
        prepare(self.source, self.trial)
        target = self.root / "workspace-import"
        with self.assertRaisesRegex(ValueError, "Unsupported popup rule scope"):
            import_workspace_profiles(self.trial, target, "workspace")
        self.assertFalse(target.exists())

    def test_workspace_import_preserves_link_rules_without_a_saved_session(self):
        self.config["shift_click_peek"] = False
        self.config["link_rules"] = [
            {"host": "example.test", "workspace_id": "personal"},
            {"host": "example.test", "workspace_id": "work"},
            {"host": "[::1]", "workspace_id": "work"},
            {"host": "127.0.0.1", "workspace_id": "work"},
            {"host": "127.1", "workspace_id": "work"},
            {"host": "PORT.test", "workspace_id": "work"},
            {"host": "bad.test:443", "workspace_id": "work"},
            {"host": "missing.test", "workspace_id": "missing"},
        ]
        self.write_config()
        prepare(self.source, self.trial)
        target = self.root / "workspace-import"
        import_workspace_profiles(self.trial, target, "workspace")
        session = json.loads((target / "ContentWorkspaceSession.json").read_text(encoding="utf-8"))
        self.assertEqual(session["link_rules"], {
            "example.test": "personal", "[::1]": "work", "127.0.0.1": "work",
        })
        self.assertIs(session["shift_click_peek"], False)

    def test_workspace_import_preserves_address_copy_setting_without_a_saved_session(self):
        self.config["clean_address_copies"] = False
        self.write_config()
        prepare(self.source, self.trial)
        target = self.root / "workspace-import"
        import_workspace_profiles(self.trial, target, "workspace")
        session = json.loads((target / "ContentWorkspaceSession.json").read_text(encoding="utf-8"))
        self.assertIs(session["clean_address_copies"], False)

    def test_workspace_import_rejects_tab_linked_to_other_profile_pin(self):
        self.config["profiles"][1]["pins"] = [
            {"id": "personal-pin", "title": "Personal page", "url": "https://personal.test/"},
        ]
        self.write_config()
        (self.source / "ZitterSession.json").write_text(json.dumps({
            "work": {"profile_id": "work-profile", "active": 0,
                     "tabs": [{"url": "https://work.test/", "title": "Work",
                               "pin": "personal-pin"}]},
        }), encoding="utf-8")
        prepare(self.source, self.trial)
        target = self.root / "workspace-import"
        with self.assertRaisesRegex(ValueError, "Invalid legacy open pin for work"):
            import_workspace_profiles(self.trial, target, "workspace")
        self.assertFalse(target.exists())

    def test_workspace_filter_cache_preserves_verified_bytes_and_age(self):
        directory = self.source / "Zitter Filter Lists"
        directory.mkdir()
        source = directory / "easylist.txt"
        source.write_bytes(b"[Adblock Plus 2.0]\n||ads.test^\n")
        os.utime(source, (1000000000, 1000000000))
        (directory / "unknown.txt").write_bytes(b"[Adblock Plus 2.0]\n")
        prepare(self.source, self.trial)

        destination = self.root / "filter-cache-import"
        import_workspace_profiles(self.trial, destination, "workspace")
        target = destination / directory.name / source.name
        self.assertEqual(target.read_bytes(), source.read_bytes())
        self.assertEqual(target.stat().st_mtime, source.stat().st_mtime)
        self.assertFalse((target.parent / "unknown.txt").exists())

        candidate = self.trial / "candidate" / directory.name / source.name
        candidate.write_bytes(candidate.read_bytes().replace(b"ads.test", b"bad.test"))
        rejected = self.root / "invalid-filter-cache"
        with self.assertRaisesRegex(ValueError, "filter-list cache changed after preparation"):
            import_workspace_profiles(self.trial, rejected, "workspace")
        self.assertFalse(rejected.exists())

    def test_workspace_adblock_preferences_stay_with_each_profile(self):
        values = {
            "work": {"ad_blocking": False,
                     "ad_blocking_rules": ["||work.test^$script"],
                     "ad_blocking_lists": {"zitter": False, "ublock": True}},
            "personal": {"ad_blocking": True,
                         "ad_blocking_allowed_sites": ["personal.test"]},
        }
        for name, prefs in values.items():
            directory = self.source / ("ZitterWork" if name == "work" else "ZitterPersonal")
            (directory / "Preferences").write_text(
                json.dumps({"zitter": {**prefs, "unrelated": "omit"}}), encoding="utf-8",
            )
        prepare(self.source, self.trial)
        target = self.root / "workspace-import"
        import_workspace_profiles(self.trial, target, "workspace")
        for name, prefs in values.items():
            path = target / f"workspace-{name}" / "Content Ad Blocking Preferences"
            self.assertEqual(json.loads(path.read_text(encoding="utf-8")), {"zitter": prefs})

        path = self.source / "ZitterWork" / "Preferences"
        path.write_text(json.dumps({"zitter": {
            "ad_blocking_allowed_sites": ["user:secret@work.test"],
        }}), encoding="utf-8")
        invalid_trial = self.root / "invalid-adblock-trial"
        prepare(self.source, invalid_trial)
        rejected = self.root / "invalid-adblock"
        with self.assertRaisesRegex(ValueError, "Invalid legacy ad blocking exception"):
            import_workspace_profiles(invalid_trial, rejected, "workspace")
        self.assertFalse(rejected.exists())

    def test_workspace_import_keeps_only_the_inactive_workspace_sleeping(self):
        self.config["active"] = "personal"
        self.write_config()
        (self.source / "ZitterSession.json").write_text(json.dumps({
            name: {"profile_id": f"{name}-profile", "active": 0, "sleeping": True,
                   "tabs": [{"url": f"https://{name}.test/", "title": name}]}
            for name in ("work", "personal")
        }), encoding="utf-8")
        prepare(self.source, self.trial)
        target = self.root / "workspace-import"
        import_workspace_profiles(self.trial, target, "workspace")
        session = json.loads((target / "ContentWorkspaceSession.json").read_text(encoding="utf-8"))
        self.assertTrue(session["work"]["sleeping"])
        self.assertNotIn("sleeping", session["personal"])
        self.assertEqual(session["active_workspace"], "personal")

    def test_workspace_timeline_uses_current_profile_and_filters_unrestorable_tabs(self):
        source = self.source / "ZitterTimeline.jsonl"
        first = {"t": 100, "w": "work", "p": "work-profile", "a": 1,
                 "tabs": [["zitter://rss/", "RSS"], ["https://work.test/", "Work"],
                          ["https://user:secret@work.test/", "Credentials"]]}
        records = [first, {**first, "t": 200, "p": "personal-profile"},
                   {**first, "t": 300, "tabs": [], "a": -1, "r": True},
                   {"t": 150, "w": "work", "m": "Work snapshot"},
                   {"t": 250, "w": "work", "m": "Other profile snapshot"},
                   {**first, "w": "deleted", "t": 200}]
        source.write_text("".join(json.dumps(record) + "\n" for record in records),
                          encoding="utf-8")
        settings = {"time_machine": False, "time_machine_days": 90}
        (self.source / "ZitterAppearance.json").write_text(
            json.dumps({**settings, "other": "unrelated"}), encoding="utf-8",
        )
        prepare(self.source, self.trial)
        target = self.root / "workspace-import"
        import_workspace_profiles(self.trial, target, "workspace")
        self.assertEqual(json.loads((target / "WorkspaceSettings.json").read_text(
            encoding="utf-8")), settings)
        timeline = target / "ContentWorkspaceSession.timeline.jsonl"
        converted = [json.loads(line) for line in timeline.read_text(encoding="utf-8").splitlines()]
        self.assertEqual(converted, [
            {**first, "p": "work", "tabs": first["tabs"][:2]},
            {"t": 300, "w": "work", "p": "work", "a": -1, "tabs": []},
            {"t": 150, "w": "work", "m": "Work snapshot"},
        ])

        candidate = self.trial / "candidate" / source.name
        content = candidate.read_bytes()
        candidate.write_bytes(content.replace(b"Work snapshot", b"Fake snapshot"))
        rejected = self.root / "rejected-timeline"
        with self.assertRaisesRegex(ValueError, "workspace timeline changed after preparation"):
            import_workspace_profiles(self.trial, rejected, "workspace")
        self.assertFalse(rejected.exists())

    def test_workspace_import_refuses_unsupported_or_modified_session(self):
        saved = {"work": {"profile_id": "work-profile", "active": 0,
                          "tabs": [{"url": "https://example.test/", "title": "Example"}]}}
        unsupported = ({"split": [0, 1]}, {"right": 1}, {"sleeping": "true"},
                       {"tabs": [{"url": "https://user:secret@example.test/", "title": "Login"}]},
                       {"tabs": [{"url": "zitter://rss/?unexpected", "title": "Feeds"}]},
                       {"tabs": [{"url": "", "title": "a" * 8193}]},
                       {"tabs": [{"url": "", "title": "Pinned", "pin": "id"}]})
        for index, extra in enumerate(unsupported):
            with self.subTest(extra=extra):
                session = {"work": {**saved["work"], **extra}}
                (self.source / "ZitterSession.json").write_text(json.dumps(session), encoding="utf-8")
                trial = self.root / f"trial-{index}"
                prepare(self.source, trial)
                target = self.root / "workspace-import"
                with self.assertRaisesRegex(ValueError, "Unsupported legacy|Invalid legacy open pin"):
                    import_workspace_profiles(trial, target, "workspace")
                self.assertFalse(target.exists())

        session = self.source / "ZitterSession.json"
        session.write_text(json.dumps(saved), encoding="utf-8")
        prepare(self.source, self.trial)
        modified = self.trial / "candidate/ZitterSession.json"
        modified.write_text(session.read_text(encoding="utf-8").replace("Example", "Changed"),
                            encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "workspace session changed after preparation"):
            import_workspace_profiles(self.trial, target, "workspace")
        self.assertFalse(target.exists())

    def test_legacy_layout_uses_existing_directories(self):
        del self.config["profiles"]
        self.write_config()
        self.assertEqual(prepare(self.source, self.trial), 2)
        self.assertEqual(inventory(self.trial / "baseline"), inventory(self.source))


if __name__ == "__main__":
    unittest.main()
