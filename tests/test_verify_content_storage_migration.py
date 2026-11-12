from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory
import unittest
from unittest import mock

with mock.patch.object(sys, "path", [str(Path(__file__).resolve().parents[1] / "tools"), *sys.path]):
    import verify_content_storage_migration as migration


@unittest.skipUnless(sys.platform == "darwin", "Requires Darwin RENAME_SWAP")
class ReplacementTrialTest(unittest.TestCase):
    def test_failed_replacement_restores_original_bundle(self):
        for signature_failure in (False, True):
            with self.subTest(signature_failure=signature_failure), TemporaryDirectory() as directory:
                installed = Path(directory) / "Zitter.app"
                incoming = Path(directory) / "Incoming.app"
                installed.mkdir()
                incoming.mkdir()
                (installed / "legacy").touch()
                (incoming / "content").touch()
                check = [ValueError("bad signature"), None] if signature_failure else [None, None]
                expected = "bad signature" if signature_failure else "launch failed"
                with mock.patch.object(migration, "verify_signature", side_effect=check) as verify:
                    with self.assertRaisesRegex(ValueError, expected):
                        with migration.temporary_trial_replacement(installed, incoming):
                            self.assertTrue((installed / "content").is_file())
                            raise ValueError("launch failed")
                self.assertEqual(verify.call_args_list, [mock.call(installed), mock.call(installed)])
                self.assertTrue((installed / "legacy").is_file())
                self.assertTrue((incoming / "content").is_file())


    def test_rejects_adhoc_signature_with_production_identifier(self):
        with TemporaryDirectory() as directory:
            app = Path(directory) / "Zitter.app"
            executable = app / "Contents/MacOS/Zitter"
            executable.parent.mkdir(parents=True)
            shutil.copyfile("/usr/bin/true", executable)
            executable.chmod(0o755)
            (app / "Contents/Info.plist").write_bytes(plistlib.dumps({
                "CFBundleIdentifier": migration.BUNDLE_ID,
                "CFBundleExecutable": executable.name,
                "CFBundlePackageType": "APPL",
            }))
            subprocess.run(
                ["/usr/bin/codesign", "--force", "--sign", "-", app],
                capture_output=True, check=True,
            )
            migration.verify_signature(app)
            with self.assertRaisesRegex(SystemExit, "Expected the installed Zitter Developer ID publisher"):
                migration.verify_update_publisher(app, "ABC123DE45")
