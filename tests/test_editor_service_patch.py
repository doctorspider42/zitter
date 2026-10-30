# Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock
import editor_service_fixture

with mock.patch.object(sys, 'path', [str(Path(__file__).resolve().parents[1] / 'tools'), *sys.path]):
    import editor_service_patch as service


class EditorServicePatchTests(unittest.TestCase):
    def test_plan_is_read_only_and_apply_is_idempotent(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            editor_service_fixture.seed(root)
            source = root / service.SOURCE_PATH
            source.write_text(source.read_text() + '// unrelated local edit\n', newline="\n")
            original = source.read_text()
            plan = service.prepare(root)
            self.assertFalse((root / service.HEADER_PATH).exists())
            self.assertEqual(original, source.read_text())
            service.apply(plan)
            self.assertIn('return *GetKillBuffer();', source.read_text())
            self.assertIn('SetKillBuffer(text);', source.read_text())
            self.assertIn('PreserveExistingModelBehavior();', source.read_text())
            self.assertTrue(source.read_text().endswith('// unrelated local edit\n'))
            before = {p: (p.read_bytes(), p.stat().st_mtime_ns, p.stat().st_mode) for p in plan}
            service.apply(service.prepare(root))
            self.assertEqual(before, {p: (p.read_bytes(), p.stat().st_mtime_ns, p.stat().st_mode) for p in plan})

    def test_rejects_changed_owned_header_or_implementation(self):
        for changed in ['header', 'source', 'include', 'build']:
            with self.subTest(changed=changed), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                editor_service_fixture.seed(root)
                service.apply(service.prepare(root))
                path = root / (service.HEADER_PATH if changed == 'header' else service.BUILD_PATH if changed == 'build' else service.SOURCE_PATH)
                if changed in ['include','build']:
                    path.write_text(path.read_text().replace('text_edit_kill_buffer.h','text_edit_kill_buffer_local.h'), newline="\n")
                else:
                    path.write_text(path.read_text().replace('GetTextEditKillBuffer()', 'GetTextEditKillBuffer() /* local change */'), newline="\n")
                before = {p: p.read_bytes() for p in root.rglob('*') if p.is_file()}
                with self.assertRaises(ValueError):
                    service.prepare(root)
                self.assertEqual(before, {p: p.read_bytes() for p in root.rglob('*') if p.is_file()})

    def test_rejects_unexpected_native_layout_before_any_write(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            editor_service_fixture.seed(root)
            (root / service.BUILD_PATH).write_text('unexpected upstream layout', newline="\n")
            with self.assertRaises(ValueError):
                service.prepare(root)
            self.assertFalse((root / service.HEADER_PATH).exists())
            self.assertNotIn('Zitter: expose', (root / service.SOURCE_PATH).read_text())

    def test_rejects_edits_between_prepare_and_apply(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            editor_service_fixture.seed(root)
            plan = service.prepare(root)
            source = root / service.SOURCE_PATH
            edited = source.read_bytes() + b'// intervening local work\n'
            source.write_bytes(edited)
            with self.assertRaises(ValueError):
                service.apply(plan)
            self.assertEqual(edited, source.read_bytes())
            self.assertFalse((root / service.HEADER_PATH).exists())

    def test_rolls_back_a_partial_write_failure(self):
        for after_write in [False, True]:
            with self.subTest(after_write=after_write), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                editor_service_fixture.seed(root)
                plan = service.prepare(root)
                target = (root / service.SOURCE_PATH).resolve()
                write = service._write
                def fail_once(path, data):
                    if path == target and data == plan[target][1]:
                        if after_write:
                            write(path, data)
                        raise OSError('simulated source write failure')
                    write(path, data)
                with mock.patch.object(service, '_write', side_effect=fail_once):
                    with self.assertRaises(OSError):
                        service.apply(plan)
                for path, (old, _) in plan.items():
                    self.assertEqual(old, path.read_bytes() if path.exists() else None)
                self.assertFalse(list(root.rglob('.zitter-editor-*')))

    def test_symlink_cannot_modify_a_file_outside_checkout(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'checkout'
            root.mkdir()
            editor_service_fixture.seed(root)
            outside = root.parent / 'private-header'
            outside.write_text('keep me', newline="\n")
            (root / service.HEADER_PATH).symlink_to(outside)
            with self.assertRaises(ValueError):
                service.prepare(root)
            self.assertEqual('keep me', outside.read_text())

    def test_partial_valid_hook_can_be_completed_without_rewriting_it(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            editor_service_fixture.seed(root)
            header = root / service.HEADER_PATH
            header.write_text(service.HEADER, newline="\n")
            before = header.stat().st_mtime_ns
            service.apply(service.prepare(root))
            self.assertEqual(before, header.stat().st_mtime_ns)
            self.assertIn('SetTextEditKillBuffer', (root / service.SOURCE_PATH).read_text())
