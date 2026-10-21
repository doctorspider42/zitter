import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("metal_sidebar_launcher", Path(__file__).resolve().parents[1] / "tools/run_metal_sidebar.py")
launcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(launcher)


class MetalSidebarProfileTest(unittest.TestCase):
    def test_existing_unowned_profile_is_never_modified(self):
        with tempfile.TemporaryDirectory() as temporary:
            profile = Path(temporary)
            original = profile / "ZitterWorkspaces.json"
            original.write_text("user data")
            with self.assertRaises(ValueError):
                launcher.initialize_profile(profile, 8767)
            self.assertEqual(original.read_text(), "user data")
            self.assertFalse((profile / launcher.MARKER).exists())

    def test_owned_profile_preserves_session_on_relaunch(self):
        with tempfile.TemporaryDirectory() as temporary:
            profile = Path(temporary) / "isolated"
            launcher.initialize_profile(profile, 8767)
            session = profile / "ZitterSession.json"
            session.write_text('{"user":"changed session"}')
            launcher.initialize_profile(profile, 8767)
            self.assertEqual(json.loads(session.read_text()), {"user": "changed session"})

    def test_demo_is_real_session_with_split_folders_and_unicode(self):
        with tempfile.TemporaryDirectory() as temporary:
            profile = Path(temporary) / "isolated"
            launcher.initialize_profile(profile, 8767)
            config = json.loads((profile / "ZitterWorkspaces.json").read_text())
            session = json.loads((profile / "ZitterSession.json").read_text())
            self.assertTrue(config["onboarding_complete"])
            self.assertTrue(config["workspaces"][0]["pins"][0]["folder"])
            self.assertEqual(len(session["work"]["tabs"]), 24)
            self.assertEqual(session["work"]["right"], 1)
            self.assertIn("🦀", session["work"]["tabs"][1]["title"])
            self.assertTrue(all(tab["url"].startswith("http://127.0.0.1:8767/") for tab in session["work"]["tabs"]))


if __name__ == "__main__":
    unittest.main()
