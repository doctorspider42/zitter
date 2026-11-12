#!/usr/bin/env python3
"""Verify an offline chrome.storage.local migration and legacy reopen on macOS."""

import argparse
from base64 import b64encode
from contextlib import ExitStack
from hashlib import sha256
from http.server import ThreadingHTTPServer
import json
from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory
from threading import Thread

from profile_migration_trial import (
    EXTENSION_STATE, EXTENSION_STORAGE, LEGACY_EXTENSION_RECORD, LEGACY_EXTENSION_RECORDS,
    import_workspace_profiles,
    inventory, prepare, rollback,
)
from verify_content_extension_manifest import write_fixture
from verify_content_workspace_session import (
    EXTENSION_INSTALL_MARKER, EXTENSION_KEY_SWITCH, EXTENSION_REGISTRY_SWITCH,
    EXTENSION_PROFILE_SWITCH, EXTENSION_STATE_SWITCH,
    EXTENSION_SANDBOX_MARKER, EXTENSION_STORAGE_MARKER, MARKER,
    WorkspaceFixture, launch, make_signing_key, seed_session,
    sign_fixture, require_rejection,
)

MOCK_KEYCHAIN = ("--zitter-content-workspace-keychain", "--use-mock-keychain")
LEGACY_ALARM_TIME = 2134567890000


class MigrationFixture(WorkspaceFixture):
    def do_GET(self):
        if self.path in ("/legacy/work", "/legacy/personal"):
            self.send_page(b"<!doctype html><title>Legacy extension fixture</title>")
        elif self.path == "/favicon.ico":
            self.send_response(204)
            self.end_headers()
        else:
            super().do_GET()


def seed_legacy_root(root, work_url, *, profile="work"):
    profiles = [
        {"id": "work-profile", "name": "Work", "directory": "ZitterWork"},
        {"id": "personal-profile", "name": "Personal", "directory": "ZitterPersonal"},
    ]
    workspaces = [
        {"id": profile["name"].lower(), "name": profile["name"], "profile_id": profile["id"]}
        for profile in profiles
    ]
    for entry in profiles:
        (root / entry["directory"]).mkdir()
    catalogue = {
        "profiles": profiles, "workspaces": workspaces,
        "active": profile, "onboarding_complete": True,
    }
    (root / "ZitterWorkspaces.json").write_text(json.dumps(catalogue), encoding="utf-8")
    session = {
        profile: {
            "profile_id": f"{profile}-profile", "active": 0, "left": 0, "right": -1,
            "ratio": 0.5, "tabs": [{"url": work_url, "title": f"Legacy {profile.title()} fixture"}],
        }
    }
    (root / "ZitterSession.json").write_text(json.dumps(session), encoding="utf-8")


def run_legacy_profile_probe(node, executable, root, package, extension_id, work_url, action,
                             *, env=None, profile_directory="ZitterWork", companion=None,
                             counter=41, alarm_time=None, without_local_storage=False):
    script = Path(__file__).with_name("probe_legacy_profile.mjs")
    subprocess.run(
        [node, str(script), str(executable), str(root), str(package),
         extension_id, work_url, action, profile_directory, str(companion or "-"), str(counter),
         str(alarm_time) if alarm_time is not None else "",
         *(["without-local-storage"] if without_local_storage else [])],
        check=True, timeout=60, env=env,
    )


def prepare_workspace_trial(source, trial, origin):
    config = source / "ZitterWorkspaces.json"
    session = source / "ZitterSession.json"
    original = {path.name: path.read_bytes() for path in (config, session)}
    catalogue = json.loads(original[config.name])
    restored = seed_session(origin)
    catalogue["active"] = restored.pop("active_workspace")
    for name, entry in restored.items():
        entry["profile_id"] = f"{name}-profile"
    config.write_text(json.dumps(catalogue), encoding="utf-8")
    session.write_text(json.dumps(restored), encoding="utf-8")
    if prepare(source, trial) != 2:
        raise SystemExit("The legacy snapshot lost a workspace profile")
    return original


def cache_fixture_manifest(profile, extension_id, manifest):
    # Chromium omits this cache for command-line unpacked fixtures. Exercise
    # the packed-extension preference shape in the offline migration input.
    records = []
    for name in ("Secure Preferences", "Preferences"):
        path = profile / name
        if not path.is_file():
            continue
        data = json.loads(path.read_text(encoding="utf-8"))
        settings = data.get("extensions", {}).get("settings", {})
        if extension_id in settings:
            records.append((path, data, settings[extension_id]))
    if len(records) != 1:
        raise SystemExit("Expected one Chromium preferences record for the fixture")
    path, data, entry = records[0]
    entry["manifest"] = manifest
    path.write_text(json.dumps(data), encoding="utf-8")


def verify_multiple_extensions(node, openssl, signer, legacy, content, profile):
    with ExitStack() as resources:
        def scratch(prefix):
            return Path(resources.enter_context(TemporaryDirectory(prefix=prefix))).resolve()

        source = scratch("zitter-content-legacy-")
        restored = scratch("zitter-content-legacy-")
        restored.rmdir()
        root = scratch("zitter-content-probe-")
        root.rmdir()
        trials = scratch("zitter-content-migration-")
        baseline = trials / "baseline-trial"
        disabled_trial = trials / "disabled-trial"
        server = resources.enter_context(ThreadingHTTPServer(("127.0.0.1", 0), MigrationFixture))
        origin = f"http://127.0.0.1:{server.server_port}"
        fixture_url = f"{origin}/legacy/{profile}"
        profile_directory = "Zitter" + profile.title()
        packages = []
        archives = []
        digests = []
        ids = []
        incompatible_archives = []
        sender_ids = ["a" * 32, "b" * 32]
        for index in range(2):
            package = scratch("zitter-content-extension-")
            signed = scratch("zitter-content-extension-")
            key = signed / "signing-key.der"
            public = make_signing_key(openssl, key)
            encoded = b64encode(public).decode("ascii")
            digest = sha256(public).hexdigest()
            ids.append(digest[:32].translate(str.maketrans("0123456789abcdef", "abcdefghijklmnop")))
            digests.append(digest)
            legacy_manifest = write_fixture(package, encoded)
            (package / "worker.js").write_text("// Legacy storage fixture\n", encoding="utf-8")
            manifest = write_fixture(signed, encoded, expect_legacy_storage=True)
            if index == 0:
                policy = {"ids": sender_ids}
                legacy_manifest["externally_connectable"] = policy
                manifest["externally_connectable"] = policy
                (package / "manifest.json").write_text(json.dumps(legacy_manifest), encoding="utf-8")
                saved_manifest = legacy_manifest
                (signed / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
            archives.append(sign_fixture(signer, signed, key, "fixture.crx"))
            if index == 0:
                for name, ids_to_sign in (
                    ("wildcard", ["*"]),
                    ("different-sender", ["c" * 32]),
                    ("no-policy", None),
                ):
                    candidate = dict(manifest)
                    if ids_to_sign is None:
                        del candidate["externally_connectable"]
                    else:
                        candidate["externally_connectable"] = {"ids": ids_to_sign}
                    (signed / "manifest.json").write_text(json.dumps(candidate), encoding="utf-8")
                    incompatible_archives.append(sign_fixture(
                        signer, signed, key, f"{name}.crx"))
            key.unlink()
            packages.append(package)

        def run_legacy(directory, index, action):
            run_legacy_profile_probe(
                node, legacy, directory, packages[index], ids[index], fixture_url, action,
                profile_directory=profile_directory, companion=packages[1 - index],
                counter=(41, 73)[index],
            )

        def launch_content(index, install=False, archive=None):
            source_archive = archive or archives[index]
            return launch(content, root, origin, (
                *MOCK_KEYCHAIN, f"{EXTENSION_PROFILE_SWITCH}={profile}",
                f"{EXTENSION_KEY_SWITCH}={digests[index]}",
                f"{EXTENSION_REGISTRY_SWITCH}={source_archive}" if install else EXTENSION_REGISTRY_SWITCH,
            ))

        seed_legacy_root(source, fixture_url, profile=profile)
        server.requests = []
        server.reopening = False
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            for index in (0, 1):
                run_legacy(source, index, "seed")
            legacy_session = prepare_workspace_trial(source, baseline, origin)
            snapshot = inventory(source)
            rollback(baseline, restored)
            if inventory(restored) != snapshot:
                raise SystemExit("The two-extension rollback did not reproduce the snapshot")
            for name, data in legacy_session.items():
                (restored / name).write_bytes(data)
            for index in (0, 1):
                run_legacy(restored, index, "read")
            run_legacy(restored, 1, "seed-disabled")
            cache_fixture_manifest(restored / profile_directory, ids[0], saved_manifest)
            prepare_workspace_trial(restored, disabled_trial, origin)
            disabled_snapshot = inventory(restored)
            source_stores = [inventory(restored / profile_directory / EXTENSION_STORAGE / value)
                             for value in ids]
            import_workspace_profiles(disabled_trial, root, "mock-keychain",
                                      selected_extensions={profile: ids})
            target = root / f"mock-keychain-{profile}"
            other = root / f"mock-keychain-{'personal' if profile == 'work' else 'work'}"
            records = [target / LEGACY_EXTENSION_RECORDS / f"{value}.json" for value in ids]
            for index, value in enumerate(ids):
                receipt = json.loads(records[index].read_text(encoding="utf-8"))
                if (receipt["extension_id"] != value
                        or receipt["version"] != (2 if index == 0 else 1)
                        or receipt.get("external_extension_ids") != (sender_ids if index == 0 else None)
                        or bool(receipt["disable_reasons"]) != (index == 1)
                        or inventory(target / EXTENSION_STORAGE / value) != source_stores[index]):
                    raise SystemExit("A migrated extension lost its own grants, state, or data")
            receipt_dir = records[0].parent
            hidden_receipts = target / "withheld-receipts"
            receipt_dir.rename(hidden_receipts)
            requests = list(server.requests)
            require_rejection(launch_content(0, install=True),
                              "A pre-existing extension store requires its legacy permissions record")
            hidden_receipts.rename(receipt_dir)
            if server.requests != requests or (target / "Extensions").exists():
                raise SystemExit("An imported store was activated without its grants")
            for archive in incompatible_archives:
                require_rejection(launch_content(0, install=True, archive=archive),
                                  "Content extension permissions differ from the legacy record")
            if (server.requests != requests or (target / "Extensions").exists()
                    or inventory(target / EXTENSION_STORAGE / ids[0]) != source_stores[0]):
                raise SystemExit("A rejected sender policy published files or changed data")
            for run in (0, 1):
                for index in (0, 1):
                    result = launch_content(index, install=run == 0)
                    lines = result.stdout.splitlines()
                    expected = (EXTENSION_STORAGE_MARKER + str(42 + run) if index == 0
                                else "Zitter Content kept the imported extension disabled")
                    if (result.returncode or ":FATAL:" in result.stderr or
                            not {MARKER, expected, "Zitter Content verified the legacy extension grants"}.issubset(lines)
                            or (index == 1 and EXTENSION_STORAGE_MARKER in result.stdout)):
                        raise SystemExit(f"Content could not restore {ids[index]}.\n{result.stdout}\n{result.stderr}")
                    server.reopening = True
                if run == 0:
                    for archive in archives:
                        archive.unlink()
            if (inventory(target / EXTENSION_STORAGE / ids[1]) != source_stores[1]
                    or (other / EXTENSION_STORAGE).exists()):
                raise SystemExit("The disabled store changed or data escaped the owning profile")

            original = records[1].read_bytes()
            records[1].write_bytes(records[0].read_bytes())
            requests = list(server.requests)
            require_rejection(launch_content(1), "Content legacy extension record is missing or changed")
            records[1].unlink()
            require_rejection(launch_content(1), "Could not read Content legacy extension record")
            records[1].write_bytes(original)
            single_receipt = target / LEGACY_EXTENSION_RECORD
            single_receipt.write_bytes(records[0].read_bytes())
            require_rejection(launch_content(0), "Invalid Content legacy extension records directory")
            single_receipt.unlink()
            if (server.requests != requests or
                    inventory(target / EXTENSION_STORAGE / ids[1]) != source_stores[1]):
                raise SystemExit("A rejected extension receipt loaded workspace pages or changed data")
            if inventory(restored) != disabled_snapshot or inventory(source) != snapshot:
                raise SystemExit("Extension migration modified a legacy snapshot")
            print(f"Two legacy extensions preserved distinct data, grants, and disable state in {profile}")
            print("The legacy sender list survived installation and restart, and changed policies were rejected")
        finally:
            server.shutdown()
            thread.join()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("legacy_app", type=Path, help="Local legacy Zitter.app")
    parser.add_argument("content_app", type=Path, help="Local Zitter Content Lab.app")
    parser.add_argument("--crx-signer", type=Path, help="Path to the local CRX signing helper")
    parser.add_argument("--extension-profile", choices=("work", "personal"), default="work")
    parser.add_argument("--alarm-importer", type=Path,
                        help="Also migrate a legacy alarm using this local helper")
    parser.add_argument("--without-local-storage", action="store_true",
                        help="Seed the alarm without creating chrome.storage.local data")
    parser.add_argument("--multiple", action="store_true",
                        help="migrate two signed extensions from the same legacy profile")
    args = parser.parse_args()
    legacy = (args.legacy_app / "Contents/MacOS/Chromium").resolve()
    content = (args.content_app / "Contents/MacOS/Zitter Content Lab").resolve()
    signer = (args.crx_signer or args.content_app.resolve().parent / "crx3_build_action").resolve()
    node = shutil.which("node")
    openssl = shutil.which("openssl")
    if not all(path.is_file() for path in (legacy, content, signer)) or not node or not openssl:
        parser.error("Expected both local macOS apps, crx3_build_action, Node.js, and OpenSSL")
    if args.without_local_storage and not args.alarm_importer:
        parser.error("The no-local-storage fixture requires the alarm importer")
    if args.alarm_importer and (args.multiple or not args.alarm_importer.is_file()):
        parser.error("Alarm migration requires the local helper and the single-extension fixture")
    if args.multiple:
        verify_multiple_extensions(node, openssl, signer, legacy, content, args.extension_profile)
        return

    with ExitStack() as resources:
        def scratch(prefix):
            return Path(resources.enter_context(TemporaryDirectory(prefix=prefix))).resolve()

        legacy_root = scratch("zitter-content-legacy-")
        legacy_package = scratch("zitter-content-extension-")
        content_package = scratch("zitter-content-extension-")
        content_root = scratch("zitter-content-probe-")
        content_root.rmdir()
        disabled_root = scratch("zitter-content-probe-")
        disabled_root.rmdir()
        rollback_root = scratch("zitter-content-legacy-")
        rollback_root.rmdir()
        trial = scratch("zitter-content-migration-") / "trial"
        disabled_trial = trial.with_name("disabled-trial")
        server = resources.enter_context(
            ThreadingHTTPServer(("127.0.0.1", 0), MigrationFixture)
        )
        origin = f"http://127.0.0.1:{server.server_port}"
        profile = args.extension_profile
        other_profile = "personal" if profile == "work" else "work"
        profile_directory = "Zitter" + profile.title()
        fixture_url = f"{origin}/legacy/{profile}"
        server.requests = []
        server.reopening = False

        private_key = content_package / "signing-key.der"
        public_key = make_signing_key(openssl, private_key)
        encoded_key = b64encode(public_key).decode("ascii")
        digest = sha256(public_key).hexdigest()
        extension_id = digest[:32].translate(str.maketrans("0123456789abcdef", "abcdefghijklmnop"))
        legacy_manifest = write_fixture(legacy_package, encoded_key)
        # The legacy CDP probe controls when its data changes.
        (legacy_package / "worker.js").write_text("// Legacy storage fixture\n", encoding="utf-8")
        manifest = write_fixture(
            content_package, encoded_key, expect_legacy_storage=not args.without_local_storage,
        )
        if args.alarm_importer:
            for package, package_manifest in ((legacy_package, legacy_manifest),
                                              (content_package, manifest)):
                package_manifest["permissions"].append("alarms")
                (package / "manifest.json").write_text(json.dumps(package_manifest), encoding="utf-8")
            worker = content_package / "worker.js"
            script = worker.read_text(encoding="utf-8")
            marker = "    if (!Number.isSafeInteger(counter)"
            if script.count(marker) != 1:
                raise SystemExit("Could not attach the migrated alarm assertion")
            worker.write_text(script.replace(marker, f"""
    const alarms = await chrome.alarms.getAll();
    if (alarms.length !== 1 || alarms[0].name !== 'legacy-persistent' ||
        alarms[0].scheduledTime !== {LEGACY_ALARM_TIME} || !alarms[0].persistAcrossSessions)
      throw Error('Migrated legacy alarm missing or changed');
"""+marker), encoding="utf-8")
        archive = sign_fixture(signer, content_package, private_key, "fixture.crx")
        (content_package / "manifest.json").write_text(
            json.dumps(dict(manifest, permissions=["storage", "unlimitedStorage"])), encoding="utf-8"
        )
        escalated = sign_fixture(signer, content_package, private_key, "escalated.crx")
        private_key.unlink()
        trust = f"{EXTENSION_KEY_SWITCH}={digest}"
        content_flags = (*MOCK_KEYCHAIN, trust, f"{EXTENSION_PROFILE_SWITCH}={profile}")

        def run_legacy(root, action):
            run_legacy_profile_probe(
                node, legacy, root, legacy_package, extension_id, fixture_url, action,
                profile_directory=profile_directory,
                # The rollback reopen re-registers a command-line unpacked
                # extension. Chromium resets registered state on installation.
                alarm_time=LEGACY_ALARM_TIME if args.alarm_importer and action != "read" else None,
                without_local_storage=args.without_local_storage,
            )

        seed_legacy_root(legacy_root, fixture_url, profile=profile)
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            run_legacy(legacy_root, "seed")
            relative_store = Path(profile_directory, EXTENSION_STORAGE, extension_id)
            source_store = legacy_root / relative_store
            def local_inventory(store):
                return inventory(store) if store.exists() else None

            source_inventory = local_inventory(source_store)
            if args.without_local_storage:
                if source_inventory is not None:
                    raise SystemExit("The legacy alarm fixture unexpectedly created a local store")
            elif not (source_store / "CURRENT").is_file():
                raise SystemExit("The legacy browser did not create the selected profile's extension database")
            legacy_session = prepare_workspace_trial(legacy_root, trial, origin)
            snapshot = inventory(legacy_root)

            content_profile = content_root / f"mock-keychain-{profile}"
            if args.alarm_importer:
                try:
                    import_workspace_profiles(
                        trial, content_root, "mock-keychain",
                        selected_extensions={profile: extension_id},
                    )
                except ValueError as error:
                    if "alarm importer is required" not in str(error):
                        raise
                else:
                    raise SystemExit("Alarm migration silently dropped the legacy alarm")
                if content_root.exists():
                    raise SystemExit("A rejected alarm import published the destination")
                current = trial / "candidate" / profile_directory / EXTENSION_STATE / "CURRENT"
                original = current.read_bytes()
                try:
                    current.write_bytes(b"MANIFEST-999999\n")
                    try:
                        import_workspace_profiles(
                            trial, content_root, "mock-keychain",
                            selected_extensions={profile: extension_id},
                            alarm_importer=args.alarm_importer,
                        )
                    except ValueError as error:
                        if "changed legacy extension state" not in str(error):
                            raise
                    else:
                        raise SystemExit("Alarm migration accepted a changed snapshot")
                finally:
                    current.write_bytes(original)
                if content_root.exists():
                    raise SystemExit("An altered alarm snapshot published the destination")
            import_workspace_profiles(
                trial, content_root, "mock-keychain", selected_extensions={profile: extension_id},
                alarm_importer=args.alarm_importer,
            )
            if args.alarm_importer and (
                not (content_profile / EXTENSION_STATE / "CURRENT").is_file() or
                (content_root / f"mock-keychain-{other_profile}" / EXTENSION_STATE).exists()
            ):
                raise SystemExit("The alarm database did not stay in the selected profile")
            content_store = content_profile / EXTENSION_STORAGE / extension_id
            if local_inventory(content_store) != source_inventory:
                raise SystemExit("The migrated extension database changed before opening it")
            if args.without_local_storage:
                receipt = content_profile / LEGACY_EXTENSION_RECORDS / f"{extension_id}.json"
                backup = receipt.with_suffix(".saved")
                receipt.rename(backup)
                try:
                    require_rejection(
                        launch(content, content_root, origin, (
                            *content_flags, f"{EXTENSION_REGISTRY_SWITCH}={archive}",
                        )),
                        "Could not read Content legacy extension record",
                    )
                finally:
                    backup.rename(receipt)
                if (content_profile / "Extensions").exists():
                    raise SystemExit("The missing receipt allowed the archive to install")
            require_rejection(
                launch(content, content_root, origin,
                       (*content_flags, f"{EXTENSION_REGISTRY_SWITCH}={escalated}")),
                "Content extension permissions differ from the legacy record",
            )
            if (content_profile / "Extensions").exists() or local_inventory(content_store) != source_inventory:
                raise SystemExit("A permission mismatch modified the migration destination")
            result = launch(content, content_root, origin, [
                *content_flags, f"{EXTENSION_REGISTRY_SWITCH}={archive}",
            ])
            expected_counter = "1" if args.without_local_storage else "42"
            required = {MARKER, EXTENSION_INSTALL_MARKER, EXTENSION_SANDBOX_MARKER,
                        EXTENSION_STORAGE_MARKER + expected_counter,
                        "Zitter Content verified the legacy extension grants"}
            if result.returncode or ":FATAL:" in result.stderr or not required.issubset(result.stdout.splitlines()):
                raise SystemExit(f"Content could not read the migrated storage.\n{result.stdout}\n{result.stderr}")
            if local_inventory(content_store) == source_inventory or (
                content_root / f"mock-keychain-{other_profile}" / EXTENSION_STORAGE
            ).exists():
                raise SystemExit("The migrated data was not updated exclusively in the selected profile")

            rollback(trial, rollback_root)
            if inventory(rollback_root) != snapshot:
                raise SystemExit("Rollback did not reproduce the original snapshot")
            for name, data in legacy_session.items():
                (rollback_root / name).write_bytes(data)
            run_legacy(rollback_root, "read")
            run_legacy(rollback_root, "seed-disabled")
            prepare_workspace_trial(rollback_root, disabled_trial, origin)
            disabled_profile = disabled_root / f"mock-keychain-{profile}"
            import_workspace_profiles(
                disabled_trial, disabled_root, "mock-keychain",
                selected_extensions={profile: extension_id}, alarm_importer=args.alarm_importer,
            )
            receipt = (disabled_profile / LEGACY_EXTENSION_RECORDS / f"{extension_id}.json"
                       if args.without_local_storage else disabled_profile / LEGACY_EXTENSION_RECORD)
            if 1 not in json.loads(receipt.read_text(encoding="utf-8"))["disable_reasons"]:
                raise SystemExit("The legacy user-disabled reason was not exported")
            disabled_store = disabled_profile / EXTENSION_STORAGE / extension_id
            disabled_inventory = local_inventory(disabled_store)
            server.reopening = False
            for flag in (f"{EXTENSION_REGISTRY_SWITCH}={archive}", EXTENSION_REGISTRY_SWITCH):
                result = launch(content, disabled_root, origin, (*content_flags, flag))
                if (
                    result.returncode or ":FATAL:" in result.stderr
                    or MARKER not in result.stdout.splitlines()
                    or "Zitter Content kept the imported extension disabled" not in result.stdout.splitlines()
                    or EXTENSION_STORAGE_MARKER in result.stdout
                    or local_inventory(disabled_store) != disabled_inventory
                ):
                    raise SystemExit(f"Content did not preserve the disabled extension.\n{result.stdout}\n{result.stderr}")
                server.reopening = True

            user_choice = disabled_profile / "Extensions" / extension_id / "user-enabled"
            user_choice.write_text("enabled", encoding="ascii")
            result = launch(content, disabled_root, origin,
                            (*content_flags, EXTENSION_REGISTRY_SWITCH))
            if (result.returncode or EXTENSION_STORAGE_MARKER in result.stdout
                    or "Zitter Content kept the imported extension disabled" not in result.stdout):
                raise SystemExit(f"The local choice overrode a legacy disable.\n{result.stdout}\n{result.stderr}")
            requests_before = list(server.requests)
            require_rejection(
                launch(content, disabled_root, origin, (
                    *content_flags, EXTENSION_REGISTRY_SWITCH, f"{EXTENSION_STATE_SWITCH}=enable",
                )),
                "Unexpected Content extension enablement for the requested action",
            )
            if (server.requests != requests_before or user_choice.read_bytes() != b"enabled"
                    or local_inventory(disabled_store) != disabled_inventory):
                raise SystemExit("The rejected enable changed the imported profile")
            user_choice.unlink()

            backup = receipt.with_suffix(".saved")
            receipt.rename(backup)
            require_rejection(
                launch(content, disabled_root, origin, (*content_flags, EXTENSION_REGISTRY_SWITCH)),
                ("Could not read Content legacy extension record" if args.without_local_storage
                 else "Content legacy extension record is missing or changed"),
            )
            backup.rename(receipt)
            if inventory(legacy_root) != snapshot or inventory(trial / "candidate") != snapshot:
                raise SystemExit("The original or candidate profile was modified")
            if args.alarm_importer:
                print("Coordinated import preserved the legacy extension alarm and rollback")
            print("Coordinated import preserved legacy extension grants, disabled state, storage, and rollback")
        finally:
            server.shutdown()
            thread.join()


if __name__ == "__main__":
    main()
