# Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import tempfile
import unittest
import fingerprint_protection_fixture
from fingerprint_protection_fixture import patch


class FingerprintProtectionPatchTests(unittest.TestCase):
    def test_plan_is_read_only_and_apply_is_idempotent(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fingerprint_protection_fixture.seed(root)
            prefs = root / 'chrome/browser/prefs/browser_prefs.cc'
            prefs.write_text(prefs.read_text() + '// unrelated local edit\n')
            before = {p: p.read_bytes() for p in root.rglob('*') if p.is_file()}
            plan = patch.prepare(root)
            self.assertEqual(before, {p: p.read_bytes() for p in root.rglob('*') if p.is_file()})
            patch.apply(plan)
            self.assertIn(f'RegisterBooleanPref("{patch.PREF}", false)', prefs.read_text())
            self.assertTrue(prefs.read_text().endswith('// unrelated local edit\n'))
            canvas = (root / patch.BLINK / 'core/html/canvas/html_canvas_element.cc').read_text()
            self.assertEqual(2, canvas.count('noise->ApplyToImage'))
            self.assertEqual(1, canvas.count('#include "third_party/blink/renderer/core/frame/zitter_fingerprint_protection.h"'))
            for name in patch.OWNED:
                self.assertEqual((patch.ROOT / 'chromium/blink' / name).read_bytes(), (root / patch.BLINK / name).read_bytes())
            state = {p: (p.read_bytes(), p.stat().st_mtime_ns) for p in root.rglob('*') if p.is_file()}
            patch.apply(patch.prepare(root))
            self.assertEqual(state, {p: (p.read_bytes(), p.stat().st_mtime_ns) for p in root.rglob('*') if p.is_file()})

    def test_rejects_unexpected_upstream_layout_before_any_write(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fingerprint_protection_fixture.seed(root)
            (root / patch.BLINK / 'modules/webaudio/offline_audio_context.cc').write_text('unexpected upstream layout\n')
            before = {p: p.read_bytes() for p in root.rglob('*') if p.is_file()}
            with self.assertRaisesRegex(ValueError, 'offline_audio_context'):
                patch.prepare(root)
            self.assertEqual(before, {p: p.read_bytes() for p in root.rglob('*') if p.is_file()})

    def test_refuses_to_replace_an_unowned_file(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fingerprint_protection_fixture.seed(root)
            target = root / patch.BLINK / patch.OWNED[0]
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text('// someone else\n')
            with self.assertRaisesRegex(ValueError, 'unowned'):
                patch.prepare(root)

    def test_rejects_edits_between_prepare_and_apply(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fingerprint_protection_fixture.seed(root)
            plan = patch.prepare(root)
            prefs = root / 'chrome/browser/prefs/browser_prefs.cc'
            prefs.write_text(prefs.read_text() + '// concurrent edit\n')
            with self.assertRaises(ValueError):
                patch.apply(plan)
            self.assertNotIn(patch.PREF, prefs.read_text())


if __name__ == '__main__':
    unittest.main()
