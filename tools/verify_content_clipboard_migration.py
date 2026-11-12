#!/usr/bin/env python3
"""Verify legacy Clipboard rules in Content and in a fresh rollback profile."""

import argparse
from contextlib import ExitStack
import json
from pathlib import Path
import secrets
import shutil
from tempfile import TemporaryDirectory

from profile_migration_trial import (
    clipboard_rules_from_preferences, copy_clipboard_permissions, inventory, prepare, rollback,
)
from verify_content_extension_migration import run_legacy_profile_probe, seed_legacy_root
from verify_content_profiles import KEY_FILE
from verify_content_site_permissions import clipboard_rules, run_site_permission_probe, start_server


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("legacy_app", type=Path, help="Installed legacy macOS Zitter.app")
    parser.add_argument("content_app", type=Path, help="Local Zitter Content Lab.app")
    args = parser.parse_args()
    legacy = (args.legacy_app / "Contents/MacOS/Chromium").resolve()
    content = (args.content_app / "Contents/MacOS/Zitter Content Lab").resolve()
    node = shutil.which("node")
    if not legacy.is_file() or not content.is_file() or not node:
        parser.error("Expected both local macOS apps and Node.js")

    with ExitStack() as fixtures:
        def scratch(prefix):
            return Path(fixtures.enter_context(TemporaryDirectory(prefix=prefix))).resolve()

        source = scratch("zitter-content-legacy-")
        block_source = scratch("zitter-content-legacy-")
        restored = scratch("zitter-content-legacy-")
        restored.rmdir()
        content_root = scratch("zitter-content-probe-")
        trial = scratch("zitter-content-clipboard-migration-") / "trial"
        origin = start_server(fixtures)
        other = start_server(fixtures)
        url = origin + "/site-permissions"
        seed_legacy_root(source, url)
        seed_legacy_root(block_source, url)
        run_legacy_profile_probe(node, legacy, source, "-", "-", url, "seed-clipboard-allow")
        run_legacy_profile_probe(node, legacy, block_source, "-", "-", url, "seed-clipboard-block")

        def read_legacy_rules(path):
            return clipboard_rules_from_preferences(json.loads(path.read_text(encoding="utf-8")))

        work = source / "ZitterWork" / "Preferences"
        block = block_source / "ZitterWork" / "Preferences"
        if read_legacy_rules(work) != {origin: 1} or read_legacy_rules(block) != {origin: 2}:
            raise SystemExit("Legacy settings did not persist the expected rules")
        # Generate both rule variants with the legacy Settings handler. Give
        # the Block file to the fixture's Personal profile before snapshotting.
        shutil.copyfile(block, source / "ZitterPersonal" / "Preferences")
        original = inventory(source)
        if prepare(source, trial) != 2:
            raise SystemExit("The snapshot lost a workspace profile")

        key = content_root / KEY_FILE
        key.write_bytes(secrets.token_bytes(32))
        key.chmod(0o600)
        content_work = content_root / "workspace-work"
        content_personal = content_root / "workspace-personal"
        for profile, destination in (("work-profile", content_work),
                                     ("personal-profile", content_personal)):
            if copy_clipboard_permissions(trial, profile, destination) != 1:
                raise SystemExit(f"Did not convert one Clipboard rule for {profile}")
        run_site_permission_probe(content, content_root, "reopen", origin)
        run_site_permission_probe(content, content_root, "other", other)
        if clipboard_rules(content_work) != {origin: 1} or clipboard_rules(content_personal) != {origin: 2}:
            raise SystemExit("Opening the converted profiles changed their rules")
        run_site_permission_probe(content, content_root, "reset", origin)
        if clipboard_rules(content_work) or clipboard_rules(content_personal) != {origin: 2}:
            raise SystemExit("Content reset changed the wrong migrated profile")

        rollback(trial, restored)
        if inventory(restored) != original:
            raise SystemExit("Rollback changed the legacy snapshot")
        run_legacy_profile_probe(node, legacy, restored, "-", "-", url, "read-clipboard-allow")
        if (inventory(source) != original or inventory(trial / "candidate") != original
                or inventory(trial / "baseline") != original):
            raise SystemExit("Clipboard migration changed an input snapshot")
        print("Migrated separate legacy Clipboard rules and verified the legacy rollback")


if __name__ == "__main__":
    main()
