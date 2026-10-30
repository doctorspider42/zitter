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


# The upstream anchors patch_extension_tabs needs, as minimal stand-ins.
EXTENSION_TABS_SOURCES = {
    "header": "class ExtensionTabUtil {\n public:\n"
              "  // Any out parameter (`window`, `contents`, & `tab_index`) may be null.\n};\n",
    "source": ("// static\nbool ExtensionTabUtil::GetTabById(int tab_id,\n"
               "                                  content::BrowserContext* browser_context,\n"
               "                                  bool include_incognito,\n"
               "                                  WindowController** out_window,\n"
               "  // Prerendering tab is not visible and it cannot be in `TabStripModel`, if the\n"
               "// static\nbool ExtensionTabUtil::GetTabById(int tab_id,\n"),
    "tabs_api": None,
}


def seed_extension_tabs(checkout):
    texts = dict(EXTENSION_TABS_SOURCES, tabs_api=(overlay.TABS_INCLUDE_ANCHOR + overlay.TABS_GET_OLD +
                                                    overlay.TABS_QUERY_ANCHOR))
    for part, path in overlay.EXTENSION_TABS_FILES.items():
        (checkout / path).parent.mkdir(parents=True, exist_ok=True)
        (checkout / path).write_text(texts[part])
    return texts


class OverlayTests(unittest.TestCase):
    def test_component_exclusion_hook_is_narrow_and_idempotent(self):
        source = ('#include "base/check.h"\n'
                  'bool CrxUpdateService::RegisterComponent(\n'
                  '    const ComponentRegistration& component) {\n'
                  '  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);\n'
                  '  return original_registration(component);\n}\n')
        patched = overlay.patch_component_registration(source)
        self.assertEqual(patched, overlay.patch_component_registration(patched))
        self.assertIn('const std::string disabled_value', patched)
        self.assertIn('original_registration(component)', patched)
        with self.assertRaises(ValueError):
            overlay.patch_component_registration('unknown upstream layout')

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

    def test_extension_tabs_hook_is_narrow_idempotent_and_guarded(self):
        with tempfile.TemporaryDirectory() as directory:
            texts = seed_extension_tabs(Path(directory))
        for part, original in texts.items():
            patched = overlay.patch_extension_tabs(original, part)
            self.assertNotEqual(original, patched)
            self.assertEqual(patched, overlay.patch_extension_tabs(patched, part))
            with self.assertRaises(ValueError):
                overlay.patch_extension_tabs("unknown upstream layout", part)
        source = overlay.patch_extension_tabs(texts["source"], "source")
        # Only window-less lookups see embedder tabs; browser tabs come first.
        self.assertIn("if (g_embedder_tabs && !out_window)", source)
        self.assertLess(source.index("g_embedder_tabs && !out_window"), source.index("Prerendering tab"))
        tabs_api = overlay.patch_extension_tabs(texts["tabs_api"], "tabs_api")
        self.assertIn("embedder->GetShownTab(profile)", tabs_api)
        self.assertIn("ExtensionTabUtil::GetTabById(tab_id, browser_context(),", tabs_api)

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
            seed_extension_tabs(checkout)
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
            component = checkout / "components/component_updater/component_updater_service.cc"
            component.parent.mkdir(parents=True)
            component.write_text('#include "base/check.h"\n'
                                 'bool CrxUpdateService::RegisterComponent(\n'
                                 '    const ComponentRegistration& component) {\n'
                                 '  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);\n}\n')
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
