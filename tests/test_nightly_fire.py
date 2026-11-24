import datetime
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("fire", ROOT / "tools/nightly/fire.py")
fire = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fire)


class NightlyFireTest(unittest.TestCase):
    def test_version_comes_from_the_short_string(self):
        header = (ROOT / "chromium/app/zitter_version.h").read_text(encoding="utf-8")
        version = fire.parse_version(header)
        self.assertRegex(version, r"^\d+\.\d+\.\d+$")
        self.assertEqual(fire.parse_version('#define ZITTER_VERSION_SHORT L"1.2.3"'), "1.2.3")
        with self.assertRaises(ValueError):
            fire.parse_version("#define ZITTER_VERSION 1,2,3,0")

    def test_run_id_sorts_by_time_and_names_the_commit(self):
        when = datetime.datetime(2026, 11, 24, 22, 5)
        self.assertEqual(fire.run_id_for("f6c5f113e47f95979c4c", when), "20261124-2205-f6c5f113")

    def test_windows_names_match_published_releases(self):
        names = fire.windows_release_names("0.23.0")
        self.assertEqual(names["setup"], "Zitter-v0.23.0-windows-x64-setup.exe")
        self.assertEqual(names["zip"], "Zitter-v0.23.0-windows-x64.zip")
        self.assertEqual(names["sums"], "SHA256SUMS-windows-x64.txt")
        self.assertEqual(names["build"], "Zitter-v0.23.0-windows-x64-build.json")

    def test_summary_lists_steps_and_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            old_runs = fire.RUNS
            fire.RUNS = Path(tmp)
            try:
                args = SimpleNamespace(skip_mac=False, skip_windows=False, no_notarize=False,
                                       no_verify=False, foreground=False, smoke=False)
                facts = {"commit": "a" * 40, "version": "9.9.9", "chromiumRevision": "b" * 40}
                run = fire.Run("20260101-0000-aaaaaaaa", facts, args)
                run.status["windows"].update(state="succeeded", steps=[
                    {"name": "build", "state": "succeeded", "started": "s", "finished": "f"}])
                run.status["mac"].update(state="failed", error="notarization rejected")
                run.status.update(state="partial", finished="f")
                (run.dir / "release").mkdir()
                (run.dir / "release" / "Zitter-v9.9.9-windows-x64.zip").write_bytes(b"x")
                run.write_summary()
                summary = (run.dir / "summary.md").read_text(encoding="utf-8")
                self.assertIn("**partial**", summary)
                self.assertIn("- build: succeeded", summary)
                self.assertIn("Error: notarization rejected", summary)
                self.assertIn("Zitter-v9.9.9-windows-x64.zip", summary)
                saved = json.loads((run.dir / "status.json").read_text(encoding="utf-8"))
                self.assertEqual(saved["version"], "9.9.9")
            finally:
                fire.RUNS = old_runs


if __name__ == "__main__":
    unittest.main()
