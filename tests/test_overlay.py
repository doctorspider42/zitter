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
    "windows_api": None,
    "function_details": None,
}


def extensions_logo_source(slot):
    indent = "  " if slot == "product-logo" else "    "
    inner = indent + "  "
    return ('<cr-toolbar>\n<if expr="is_android">\n'
            f'{indent}<picture slot="{slot}">\n'
            f'{inner}<source media="(prefers-color-scheme: dark)"\n'
            f'{inner}    srcset="//resources/images/chrome_logo_dark.svg">\n'
            f'{inner}<img srcset="images/product_logo.png" role="presentation">\n'
            f'{indent}</picture>\n</if>\n</cr-toolbar>\n')


def seed_extensions_logo(checkout):
    for slot, path in overlay.EXTENSIONS_LOGO_FILES.items():
        (checkout / path).parent.mkdir(parents=True, exist_ok=True)
        (checkout / path).write_text(extensions_logo_source(slot), newline="\n")
    # The Windows icon resource rides along with the other branding patches.
    icon = checkout / overlay.WINDOW_ICON_FILE
    icon.parent.mkdir(parents=True, exist_ok=True)
    registry = checkout / overlay.REGISTRY_EXTENSIONS_FILE
    registry.parent.mkdir(parents=True, exist_ok=True)
    registry.write_text(overlay.REGISTRY_EXTENSIONS_OLD, newline="\n")
    direct_write = checkout / overlay.DIRECT_WRITE_FILE
    direct_write.parent.mkdir(parents=True, exist_ok=True)
    direct_write.write_text(overlay.DIRECT_WRITE_ANCHOR, newline="\n")
    search_highlight = checkout / overlay.SEARCH_HIGHLIGHT_FILE
    search_highlight.parent.mkdir(parents=True, exist_ok=True)
    search_highlight.write_text(overlay.SEARCH_HIGHLIGHT_OLD, newline="\n")
    icon.write_text('#else\nIDR_MAINFRAME       ICON                        "theme\\chromium\\win\\chromium.ico"\n#endif\n', newline="\n")


def seed_extension_tabs(checkout):
    texts = dict(EXTENSION_TABS_SOURCES,
                 source=EXTENSION_TABS_SOURCES["source"] + overlay.TAB_OBJECT_ACTIVE_OLD +
                 overlay.TAB_OBJECT_HIGHLIGHTED_OLD + overlay.TAB_OBJECT_SPLIT_OLD,
                 tabs_api=(overlay.TABS_INCLUDE_ANCHOR + overlay.TABS_GET_OLD + overlay.TABS_QUERY_ANCHOR +
                           overlay.TABS_CREATE_OLD + overlay.TABS_UPDATE_OLD + overlay.TABS_REMOVE_OLD +
                           overlay.TABS_MOVE_OLD + overlay.TABS_CREATE_SPLIT_OLD + overlay.TABS_UNSPLIT_OLD),
                 windows_api=(overlay.WINDOWS_GET_ALL_OLD + overlay.WINDOWS_LAST_FOCUSED_OLD +
                              overlay.WINDOWS_CREATE_ANCHOR + overlay.WINDOWS_UPDATE_OLD +
                              overlay.WINDOWS_REMOVE_OLD),
                 function_details=(overlay.FUNCTION_DETAILS_INCLUDE_ANCHOR + overlay.CURRENT_WINDOW_ANCHOR))
    for part, path in overlay.EXTENSION_TABS_FILES.items():
        (checkout / path).parent.mkdir(parents=True, exist_ok=True)
        (checkout / path).write_text(texts[part], newline="\n")
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
        self.assertIn("embedder->OpenTab(", tabs_api)
        self.assertIn("embedder->CloseTab(contents)", tabs_api)
        self.assertIn("embedder->SplitTabs(first, second)", tabs_api)
        self.assertIn("embedder->Unsplit(", tabs_api)
        windows_api = overlay.patch_extension_tabs(texts["windows_api"], "windows_api")
        self.assertIn("kTabStripNotEditableError", windows_api)
        self.assertIn("embedder->OpenTab(window_profile", windows_api)
        self.assertIn("window->Minimize();", windows_api)
        details = overlay.patch_extension_tabs(texts["function_details"], "function_details")
        self.assertLess(details.index("GetWindow(profile)"), details.index("NOTE(rafaelw)"))

    def test_extensions_page_logo_is_zitter_mark_on_every_platform(self):
        import base64
        mark = base64.b64encode((overlay.ROOT / "assets/icon/zitter.svg").read_bytes()).decode()
        for slot in overlay.EXTENSIONS_LOGO_FILES:
            original = extensions_logo_source(slot)
            patched = overlay.patch_extensions_logo(original, slot)
            self.assertNotIn('is_android', patched)
            self.assertNotIn('chrome_logo_dark', patched)
            self.assertIn(f'<picture slot="{slot}">', patched)
            self.assertIn(mark, patched)
            self.assertEqual(patched, overlay.patch_extensions_logo(patched, slot))
            with self.assertRaises(ValueError):
                overlay.patch_extensions_logo("unknown upstream layout", slot)

    def test_chrome_registry_extensions_are_skipped_behind_zitters_switch(self):
        patched = overlay.patch_registry_extensions("a\n" + overlay.REGISTRY_EXTENSIONS_OLD + "#else\n")
        self.assertIn(f'HasSwitch(\n            "{overlay.REGISTRY_EXTENSIONS_SWITCH}")', patched)
        self.assertLess(patched.index("HasSwitch"), patched.index("new ExternalRegistryLoader"))
        self.assertEqual(patched, overlay.patch_registry_extensions(patched))
        self.assertIn(overlay.REGISTRY_EXTENSIONS_SWITCH,
                      (overlay.ROOT / "chromium/app/main.cc").read_text(encoding="utf-8"))
        with self.assertRaises(ValueError):
            overlay.patch_registry_extensions("unknown upstream layout")
    def test_sideloaded_fonts_bring_every_variable_instance(self):
        patched = overlay.patch_direct_write("before\n" + overlay.DIRECT_WRITE_ANCHOR + "after\n")
        self.assertIn("font_set_builder1->AddFontFile(font_file.Get())", patched)
        self.assertLess(patched.index("CreateFontFileReference"), patched.index("AddFontFile"))
        self.assertEqual(patched, overlay.patch_direct_write(patched))
        self.assertTrue(sorted((overlay.ROOT / "assets/fonts").glob("*.ttf")))
        with self.assertRaises(ValueError):
            overlay.patch_direct_write("unknown upstream layout")
    def test_search_highlight_takes_the_design_colors_once(self):
        patched = overlay.patch_search_highlight("before\n" + overlay.SEARCH_HIGHLIGHT_OLD + "after\n")
        self.assertIn("0xD7, 0xFF, 0x3F", patched)
        self.assertNotIn("Orange", patched)
        self.assertEqual(patched, overlay.patch_search_highlight(patched))
        with self.assertRaises(ValueError):
            overlay.patch_search_highlight("unknown upstream layout")
    def test_windows_icon_is_zitter_in_chromes_resource_slot(self):
        original = ('#else\n'
                    'IDR_MAINFRAME       ICON                        "theme\\chromium\\win\\chromium.ico"\n'
                    '#endif\n')
        patched = overlay.patch_window_icon("before\n" + original + "after\n")
        self.assertNotIn("chromium.ico", patched)
        self.assertIn('"../../zitter/chromium/app/zitter.ico"', patched)
        self.assertTrue((overlay.ROOT / "chromium/app/zitter.ico").is_file())
        self.assertEqual(patched, overlay.patch_window_icon(patched))
        with self.assertRaises(ValueError):
            overlay.patch_window_icon("unknown upstream layout")
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
            seed_extensions_logo(checkout)
            build = checkout / "chrome/test/BUILD.gn"
            build.parent.mkdir(parents=True)
            original = ('# Existing local build configuration\n'
                        '  test("browser_tests") {\n    deps = [\n    ]\n  }\n')
            build.write_text(original, newline="\n")
            keychain = checkout / "components/os_crypt/common/keychain_password_mac.mm"
            keychain.parent.mkdir(parents=True)
            keychain.write_text('"Chromium Safe Storage"\n"Chromium";\n', newline="\n")
            autofill = checkout / "chrome/browser/ui/autofill/chrome_autofill_client.cc"
            autofill.parent.mkdir(parents=True)
            autofill.write_text('bool ChromeAutofillClient::IsAutofillProfileEnabled() const {\n  return true;\n}\n', newline="\n")
            media_header = checkout / "chrome/browser/media/webrtc/media_capture_devices_dispatcher.h"
            media_header.parent.mkdir(parents=True)
            media_header.write_text("  void ProcessMediaAccessRequest(content::WebContents* web_contents,\n", newline="\n")
            media_header.with_suffix(".cc").write_text("void MediaCaptureDevicesDispatcher::ProcessMediaAccessRequest(\n", newline="\n")
            component = checkout / "components/component_updater/component_updater_service.cc"
            component.parent.mkdir(parents=True)
            component.write_text('#include "base/check.h"\n'
                                 'bool CrxUpdateService::RegisterComponent(\n'
                                 '    const ComponentRegistration& component) {\n'
                                 '  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);\n}\n', newline="\n")
            with mock.patch.object(overlay.subprocess, "check_output", return_value=revision):
                overlay.install(checkout, dry_run=True)
                self.assertFalse((checkout / "zitter").exists())
                self.assertEqual(build.read_text(), original)
                overlay.install(checkout)
                overlay.install(checkout)
                self.assertEqual(build.read_text().replace(overlay.DEPENDENCY, ""), original)
                source = checkout / "zitter/zitter/core/workspace_model.cc"
                source.write_text(source.read_text() + "\n// Local changes\n", newline="\n")
                with self.assertRaisesRegex(ValueError, "local modifications"):
                    overlay.install(checkout)
                self.assertIn("Local changes", source.read_text())


if __name__ == "__main__":
    unittest.main()
