import importlib.util
import sys
import editor_service_fixture
from pathlib import Path
import tempfile
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location(
    "overlay", Path(__file__).resolve().parents[1] / "tools/install_chromium_overlay.py")
overlay = importlib.util.module_from_spec(spec)
with mock.patch.object(sys, "path", [str(Path(__file__).resolve().parents[1] / "tools"), *sys.path]):
    spec.loader.exec_module(overlay)


class OverlayTests(unittest.TestCase):
    def test_keychain_identity_is_separate_and_patch_is_guarded(self):
        source = ('const char kDefaultServiceName[] = "Chromium Safe Storage";\n'
                  'const char kDefaultAccountName[] = "Chromium";\n')
        patched = overlay.patch_keychain(source)
        self.assertIn('"Zitter Safe Storage"', patched)
        self.assertIn('"Zitter"', patched)
        self.assertEqual(patched, overlay.patch_keychain(patched))
        with self.assertRaises(ValueError):
            overlay.patch_keychain('unknown upstream layout')

    def test_embedded_autofill_guard_is_narrow_and_idempotent(self):
        original = 'bool ChromeAutofillClient::IsAutofillProfileEnabled() const {\n  return true;\n}\n'
        patched = overlay.patch_embedded_autofill(original)
        self.assertIn('MaybeGetFromContents(web_contents())', patched)
        self.assertTrue(patched.endswith('  return true;\n}\n'))
        self.assertEqual(patched, overlay.patch_embedded_autofill(patched))
        with self.assertRaises(ValueError):
            overlay.patch_embedded_autofill('unknown upstream layout')

    def test_media_registration_keeps_dispatcher_and_is_guarded(self):
        for header, original in ((True, "  void ProcessMediaAccessRequest(content::WebContents* web_contents,\n"),
                                 (False, "void MediaCaptureDevicesDispatcher::ProcessMediaAccessRequest(\n")):
            patched = overlay.patch_media_dispatcher(original, header=header)
            self.assertTrue(patched.endswith(original))
            self.assertIn("AddMediaAccessHandler", patched)
            self.assertEqual(patched, overlay.patch_media_dispatcher(patched, header=header))
            with self.assertRaises(ValueError):
                overlay.patch_media_dispatcher(patched + original, header=header)
            with self.assertRaises(ValueError):
                overlay.patch_media_dispatcher("unexpected layout", header=header)
            with self.assertRaises(ValueError):
                overlay.patch_media_dispatcher(original + original, header=header)

    def test_only_desktop_target_is_modified_and_reinstall_is_idempotent(self):
        source = ('test("android_browsertests") {\n    deps = [\n    ]\n}\n'
                  'if (!is_android) {\n  test("browser_tests") {\n'
                  '    deps = [\n      ":existing",\n    ]\n  }\n}\n')
        patched = overlay.patch_build(source)
        self.assertEqual(source, patched.replace(overlay.DEPENDENCY, ""))
        self.assertEqual(patched, overlay.patch_build(patched))
        self.assertEqual(patched.count(overlay.DEPENDENCY), 1)

    def test_rejects_unexpected_chromium_layout(self):
        with self.assertRaises(ValueError):
            overlay.patch_build('test("different_tests") {}')

    def test_rejects_unowned_destination(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(ValueError):
                overlay.check_existing(Path(directory))

    def test_install_preserves_existing_build_edits_and_rejects_modified_overlay(self):
        import json
        revision = json.loads((overlay.ROOT / "chromium/revision.json").read_text())["revision"]
        with tempfile.TemporaryDirectory() as directory:
            checkout = Path(directory)
            editor_service_fixture.seed(checkout)
            build = checkout / "chrome/test/BUILD.gn"
            build.parent.mkdir(parents=True)
            original = ('# Existing local build configuration\n'
                        '  test("browser_tests") {\n    deps = [\n    ]\n  }\n')
            build.write_text(original)
            keychain = checkout / "components/os_crypt/common/keychain_password_mac.mm"
            keychain.parent.mkdir(parents=True)
            keychain.write_text('"Chromium Safe Storage"\n"Chromium";\n')
            autofill = checkout / "chrome/browser/ui/autofill/chrome_autofill_client.cc"
            autofill.parent.mkdir(parents=True)
            autofill.write_text('bool ChromeAutofillClient::IsAutofillProfileEnabled() const {\n  return true;\n}\n')
            media_header = checkout / "chrome/browser/media/webrtc/media_capture_devices_dispatcher.h"
            media_header.parent.mkdir(parents=True)
            media_header.write_text("  void ProcessMediaAccessRequest(content::WebContents* web_contents,\n")
            media_header.with_suffix(".cc").write_text("void MediaCaptureDevicesDispatcher::ProcessMediaAccessRequest(\n")
            with mock.patch.object(overlay.subprocess, "check_output", return_value=revision):
                overlay.install(checkout, dry_run=True)
                self.assertFalse((checkout / "zitter").exists())
                self.assertEqual(build.read_text(), original)
                overlay.install(checkout)
                overlay.install(checkout)
                self.assertEqual(build.read_text().replace(overlay.DEPENDENCY, ""), original)
                source = checkout / "zitter/zitter/core/workspace_model.cc"
                source.write_text(source.read_text() + "\n// Local changes\n")
                with self.assertRaisesRegex(ValueError, "local modifications"):
                    overlay.install(checkout)
                self.assertIn("Local changes", source.read_text())


if __name__ == "__main__":
    unittest.main()
