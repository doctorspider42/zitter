# Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

with mock.patch.object(sys, 'path', [str(Path(__file__).resolve().parents[1] / 'tools'), *sys.path]):
    import content_blocking_patch as patch
    import fingerprint_protection_patch
    import vendor_adblock
import content_blocking_fixture


def seed(root):
    content_blocking_fixture.seed(root)


def snapshot(root):
    return {p: (p.read_bytes(), p.stat().st_mtime_ns) for p in root.rglob('*') if p.is_file()}


class ContentBlockingPatchTests(unittest.TestCase):
    def test_installs_vendored_crates_and_prefs_idempotently(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            seed(root)
            before = {p: p.read_bytes() for p in root.rglob('*') if p.is_file()}
            plan = patch.prepare(root)
            self.assertEqual(before, {p: p.read_bytes() for p in root.rglob('*') if p.is_file()})
            patch.apply(plan)
            prefs = (root / patch.EDITS[0][0]).read_text()
            for pref in (patch.ENABLED, patch.ALLOWED_SITES, patch.RULES, patch.RULE_MISSES, patch.LISTS):
                self.assertEqual(1, prefs.count(f'"{pref}"'))
            vendor = root / patch.VENDOR
            self.assertTrue((vendor / patch.MARKER).exists())
            self.assertEqual((patch.SOURCE / 'BUILD.gn').read_bytes(), (vendor / 'BUILD.gn').read_bytes())
            self.assertTrue((vendor / 'adblock/src/lib.rs').exists())
            self.assertIn('allow_unsafe = true', (root / patch.REPLACEMENTS[0][0]).read_text())
            state = snapshot(root)
            patch.apply(patch.prepare(root))
            self.assertEqual(state, snapshot(root))

    def test_content_preserves_scriptlets_when_removing_legacy_fingerprint_transport(self):
        from fingerprint_protection_fixture import seed as seed_fingerprint

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            seed(root)
            seed_fingerprint(root)
            for path, anchor, addition in fingerprint_protection_patch.RENDERER_PREFS_EDITS:
                target = root / path
                original = target.read_text()
                if anchor not in original:
                    original += anchor
                target.write_text(original.replace(anchor, anchor + addition, 1), newline="\n")
            for path in root.glob("chrome/**/*"):
                if path.is_file():
                    path.unlink()

            def prepare():
                fingerprint = fingerprint_protection_patch.prepare(
                    root, register_chrome_pref=False
                )
                return patch.prepare(root, include_chrome=False, previous_plan=fingerprint)

            plan = prepare()
            self.assertFalse(any(path.is_relative_to(root / "chrome") for path in plan))
            patch.apply(plan)
            header = root / patch.CONTENT_EDITS[0][0]
            self.assertIn("zitter_page_scripts", header.read_text())
            for path, _, _ in fingerprint_protection_patch.RENDERER_PREFS_EDITS:
                self.assertNotIn("zitter_fingerprint_key", (root / path).read_text())
            self.assertTrue((root / patch.VENDOR / patch.MARKER).is_file())
            self.assertTrue(all(old == new for old, new in prepare().values()))

    def test_removes_files_no_longer_vendored(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            seed(root)
            patch.apply(patch.prepare(root))
            stale = root / patch.VENDOR / 'oldcrate/src/lib.rs'
            stale.parent.mkdir(parents=True)
            stale.write_text('// gone upstream\n')
            patch.apply(patch.prepare(root))
            self.assertFalse(stale.exists())

    def test_refuses_an_unowned_vendor_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            seed(root)
            (root / patch.VENDOR).mkdir(parents=True)
            (root / patch.VENDOR / 'BUILD.gn').write_text('# someone else\n')
            with self.assertRaisesRegex(ValueError, 'unowned'):
                patch.prepare(root)

    def test_rejects_unexpected_upstream_layout_before_any_write(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = root / patch.EDITS[0][0]
            target.parent.mkdir(parents=True)
            target.write_text('unexpected upstream layout\n', newline='\n')
            with self.assertRaisesRegex(ValueError, 'browser_prefs'):
                patch.prepare(root)
            self.assertFalse((root / patch.VENDOR).exists())

    def test_vendored_crates_match_their_pins_and_build_file(self):
        for name, version, *_ in vendor_adblock.CRATES:
            manifest = vendor_adblock.VENDOR / name / 'Cargo.toml'
            self.assertIn(f'version = "{version}"', manifest.read_text(encoding='utf-8'), name)
        self.assertEqual(vendor_adblock.build_gn().encode('utf-8'), (vendor_adblock.VENDOR / 'BUILD.gn').read_bytes())


if __name__ == '__main__':
    unittest.main()
