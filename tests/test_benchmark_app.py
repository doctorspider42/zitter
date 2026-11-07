import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

TOOLS = Path(__file__).resolve().parents[1] / "tools"
spec = importlib.util.spec_from_file_location("benchmark_app", TOOLS / "benchmark_app.py")
benchmark = importlib.util.module_from_spec(spec)
with mock.patch.object(sys, "path", [str(TOOLS), *sys.path]):
    spec.loader.exec_module(benchmark)


class RegularAppBenchmarkTest(unittest.TestCase):
    def test_startup_restores_both_profiles_and_selects_last_profile(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary) / "isolated"
            benchmark.write_fixture(directory, "http://127.0.0.1/page", 25, True)
            catalogue = json.loads((directory / "ZitterWorkspaces.json").read_text())
            session = json.loads((directory / "ZitterSession.json").read_text())
            self.assertEqual("personal", catalogue["active"])
            self.assertEqual(["work", "personal"], [item["id"] for item in catalogue["workspaces"]])
            for workspace in session.values():
                self.assertEqual(25, len(workspace["tabs"]))
                self.assertEqual({"http://127.0.0.1/page"}, {tab["url"] for tab in workspace["tabs"]})
            benchmark.verify_restored_session(directory, 25)
            with self.assertRaises(FileExistsError):
                benchmark.write_fixture(directory, "unused", 2, False)

    def test_rejects_a_restored_workload_with_lost_tabs_or_wrong_profile(self):
        for mutation in ("tabs", "profile", "active"):
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary) / "isolated"
                benchmark.write_fixture(directory, "http://127.0.0.1/page", 25, True)
                session_path = directory / "ZitterSession.json"
                session = json.loads(session_path.read_text())
                if mutation == "tabs":
                    session["work"]["tabs"].pop()
                elif mutation == "profile":
                    session["personal"]["profile_id"] = "work-profile"
                else:
                    catalogue_path = directory / "ZitterWorkspaces.json"
                    catalogue = json.loads(catalogue_path.read_text())
                    catalogue["active"] = "work"
                    catalogue_path.write_text(json.dumps(catalogue))
                session_path.write_text(json.dumps(session))
                with self.assertRaisesRegex(ValueError, "workload or profile assignment"):
                    benchmark.verify_restored_session(directory, 25)

    def test_window_timing_separates_pre_main_show_and_window_server(self):
        lines = [f"ZITTER_STARTUP {stage} {10_000 + index * 1_000}"
                 for index, stage in enumerate(benchmark.STARTUP_STAGES)]
        timings = benchmark.window_timing("\n".join(lines), 5_000_000, 18_500_000)
        self.assertEqual(5, timings["startup_stage_ending_at_main_entry_ms"])
        self.assertEqual(1, timings["startup_stage_ending_at_shell_attached_ms"])
        self.assertEqual(0.5, timings["startup_show_to_window_onscreen_ms"])
        self.assertEqual(13.5, timings["app_process_window_onscreen_ms"])
        self.assertGreater(timings["startup_to_workspace_ready_ms"],
                           timings["app_process_window_onscreen_ms"])
        with self.assertRaisesRegex(ValueError, "Missing, duplicate, or out-of-order"):
            benchmark.window_timing("\n".join(lines[:-1]), 5_000_000, 18_500_000)
        with self.assertRaisesRegex(ValueError, "before Zitter requested Show"):
            benchmark.window_timing("\n".join(lines), 5_000_000, 17_500_000)

    def run_stub(self, lines):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            executable = directory / "stub"
            executable.write_text("#!" + sys.executable + "\n" +
                                  "\n".join("print(" + repr(line) + ", flush=True)" for line in lines))
            executable.chmod(0o700)
            return benchmark.launch(executable, directory, "http://127.0.0.1/page", "startup", 1, 2,
                                    directory / "run.log")

    @unittest.skipUnless(os.name == "posix", "the stub is a shebang script")
    def test_collects_process_markers_and_requires_success(self):
        output, times = self.run_stub(["Zitter ready; isolated", "ZITTER_APP_FIRST_PAINT",
                                       'ZITTER_APP_STATUS {"ok":true}'])
        self.assertEqual({"app_process_ready_ms", "app_process_first_visual_paint_ms"}, set(times))
        self.assertGreaterEqual(times["app_process_first_visual_paint_ms"], times["app_process_ready_ms"])
        self.assertIn("ZITTER_APP_STATUS", output)

    @unittest.skipUnless(os.name == "posix", "the stub is a shebang script")
    def test_rejects_missing_or_contradictory_completion(self):
        for lines in (["Zitter ready; isolated"],
                      ['ZITTER_APP_STATUS {"ok":true}', 'ZITTER_APP_STATUS {"ok":false}']):
            with self.subTest(lines=lines), self.assertRaisesRegex(ValueError, "failed or incomplete"):
                self.run_stub(lines)
