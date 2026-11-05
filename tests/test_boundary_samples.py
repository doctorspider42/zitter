import importlib.util
from pathlib import Path
import unittest

SPEC = importlib.util.spec_from_file_location("boundary_samples", Path(__file__).resolve().parents[1] / "tools/analyze_boundary_samples.py")
samples = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(samples)


class SampleEnvelopesTests(unittest.TestCase):
    def test_nested_calls_are_not_double_counted_and_workers_are_excluded(self):
        result = samples.analyze("""Call graph:
    100 Thread_1: CrBrowserMain
    + 100 start
    +   60 WorkspaceShell::RebuildTabs()
    +     20 WorkspaceShell::RebuildTabs()
    +   10 WorkspaceShell::RebuildTabs()
    100 Thread_2: Worker
    + 100 WorkspaceShell::RebuildTabs()
""")
        self.assertEqual(100, result["main_thread_samples"])
        self.assertEqual(70, result["groups"]["rebuild_tabs"]["samples"])

    def test_rejects_a_worker_only_sample(self):
        with self.assertRaises(ValueError):
            samples.analyze("    10 Thread_2: Worker\n")


if __name__ == "__main__":
    unittest.main()
