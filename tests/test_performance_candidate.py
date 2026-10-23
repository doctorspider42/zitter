import importlib.util
import editor_service_fixture
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

TOOLS = Path(__file__).resolve().parents[1] / "tools"
spec = importlib.util.spec_from_file_location("stage_candidate", TOOLS / "stage_performance_candidate.py")
candidate = importlib.util.module_from_spec(spec)
with mock.patch.object(sys, "path", [str(TOOLS), *sys.path]):
    spec.loader.exec_module(candidate)


class PerformanceCandidateTest(unittest.TestCase):
    def test_application_measurement_preserves_frozen_product_and_owner_hashes(self):
        with tempfile.TemporaryDirectory() as directory:
            checkout = Path(directory)
            baseline = checkout / "zitter"
            contents = {
                "BUILD.gn": 'test("zitter_prototype") {}\nexecutable("zitter") {\nsources = [\n      "chromium/app/runtime.cc",\n]\n}\n',
                "chromium/prototype/workspace_window.cc": "frozen user product",
                "chromium/app/runtime.h": "namespace zitter {\n  std::unique_ptr<prototype::WorkspaceWindow> window_;\n}\n",
                "chromium/app/runtime.cc": '#include "chromium/app/runtime.h"\nvoid unrelated_user_behavior() {}\n'
                    '  LOG(INFO) << "Zitter ready; persistent data: " << g_browser_process->profile_manager()->user_data_dir();\n'
                    '  window_.reset();\n',
            }
            for name, value in contents.items():
                target = baseline / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text(value)
            (baseline / candidate.overlay.MARKER).write_text(json.dumps({
                "owner": "zitter-prototype", "files": {
                    name: hashlib.sha256(value.encode()).hexdigest() for name, value in contents.items()
                },
            }))
            self.stage(checkout, {}, variant="zitter_app_reference", reference_overlay=baseline, app_benchmark=True)
            staged = checkout / "zitter_app_reference"
            self.assertEqual("frozen user product", (staged / "chromium/prototype/workspace_window.cc").read_text())
            self.assertIn("unrelated_user_behavior", (staged / "chromium/app/runtime.cc").read_text())
            self.assertIn("zitter-app-benchmark", (staged / "chromium/app/runtime.cc").read_text())
            self.assertTrue((staged / "chromium/app/performance_probe.cc").is_file())
            for name in ("chromium/app/runtime.h", "chromium/app/runtime.cc"):
                data = (staged / name).read_bytes()
                self.assertEqual(data, candidate.instrument_app_probe(name, data))
            candidate.overlay.check_existing(baseline)

    def stage(self, checkout, files, **options):
        editor_service_fixture.seed(checkout)
        revision = json.loads((candidate.overlay.ROOT / "chromium/revision.json").read_text())["revision"]
        with mock.patch.object(candidate.overlay, "overlay_files", return_value=files), \
                mock.patch.object(candidate.subprocess, "check_output", return_value=revision):
            candidate.stage(checkout, **options)

    def test_reference_uses_frozen_product_with_current_tests_only(self):
        with tempfile.TemporaryDirectory() as directory:
            checkout = Path(directory)
            baseline = checkout / "zitter"
            baseline.mkdir()
            build = baseline / "BUILD.gn"
            build.write_text('test("zitter_prototype") {}\nexecutable("zitter") {}\n')
            product = baseline / "product.cc"
            product.write_text("frozen product")
            (baseline / candidate.overlay.MARKER).write_text(json.dumps({
                "owner": "zitter-prototype", "files": {
                    path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                    for path in (build, product)
                },
            }))
            updated = checkout / "updated.cc"
            updated.write_text('new benchmark uses "zitter/testdata"')
            self.stage(checkout, {"product.cc": updated, "new_browsertest.cc": updated},
                       variant="zitter_reference", reference_overlay=baseline, current_tests=True)
            staged = checkout / "zitter_reference"
            self.assertEqual("frozen product", (staged / "product.cc").read_text())
            self.assertIn('test("zitter_prototype_reference")', (staged / "BUILD.gn").read_text())
            self.assertIn("zitter_reference/testdata", (staged / "new_browsertest.cc").read_text())
            candidate.overlay.check_existing(baseline)

    def test_variant_name_cannot_escape_the_checkout(self):
        with self.assertRaisesRegex(ValueError, "Variant"):
            candidate.stage(Path("/unused"), variant="../not-a-variant")

    def test_can_freeze_an_owned_candidate_as_another_reference(self):
        with tempfile.TemporaryDirectory() as directory:
            checkout = Path(directory)
            build = checkout / "build.gn"
            build.write_text('test("zitter_prototype") {}\nexecutable("zitter") {}\n')
            product = checkout / "source.cc"
            product.write_text("measured lazy implementation")
            test = checkout / "test.cc"
            test.write_text('fixture uses "zitter/testdata"')
            files = {"BUILD.gn": build, "product.cc": product, "probe_browsertest.cc": test}
            self.stage(checkout, files)
            product.write_text("next unmeasured experiment")
            self.stage(checkout, files, variant="zitter_lazy_reference",
                       reference_overlay=checkout / "zitter_candidate")
            frozen = checkout / "zitter_lazy_reference"
            self.assertEqual("measured lazy implementation", (frozen / "product.cc").read_text())
            self.assertIn('test("zitter_prototype_lazy_reference")', (frozen / "BUILD.gn").read_text())
            self.assertIn('executable("zitter_lazy_reference")', (frozen / "BUILD.gn").read_text())
            self.assertIn("zitter_lazy_reference/testdata", (frozen / "probe_browsertest.cc").read_text())

    def test_candidate_keeps_baseline_and_reuses_unchanged_source_timestamps(self):
        with tempfile.TemporaryDirectory() as directory:
            checkout = Path(directory)
            baseline = checkout / "zitter"
            baseline.mkdir()
            (baseline / "sentinel").write_text("frozen baseline")
            source = checkout / "source.txt"
            source.write_text('test("zitter_prototype") {}\nexecutable("zitter") {}\n')
            files = {"BUILD.gn": source}
            self.stage(checkout, files)
            staged = checkout / "zitter_candidate/BUILD.gn"
            self.assertIn('test("zitter_prototype_candidate")', staged.read_text())
            self.assertIn('executable("zitter_candidate")', staged.read_text())
            timestamp = staged.stat().st_mtime_ns
            self.stage(checkout, files)
            self.assertEqual(timestamp, staged.stat().st_mtime_ns)
            self.assertEqual("frozen baseline", (baseline / "sentinel").read_text())

    def test_refuses_candidate_user_edits_or_untracked_files(self):
        with tempfile.TemporaryDirectory() as directory:
            checkout = Path(directory)
            source = checkout / "source.cc"
            source.write_text("original")
            files = {"source.cc": source}
            self.stage(checkout, files)
            staged = checkout / "zitter_candidate/source.cc"
            staged.write_text("user edit")
            with self.assertRaisesRegex(ValueError, "local edits"):
                self.stage(checkout, files)
            self.assertEqual("user edit", staged.read_text())
            staged.write_text("original")
            extra = staged.parent / "notes.txt"
            extra.write_text("keep my notes")
            with self.assertRaisesRegex(ValueError, "untracked"):
                self.stage(checkout, files)
            self.assertEqual("keep my notes", extra.read_text())
