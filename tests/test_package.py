import importlib.util
from pathlib import Path
import plistlib
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    'package_app', Path(__file__).resolve().parents[1] / 'tools/package_app.py')
packager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packager)


class PackageTests(unittest.TestCase):
    def test_switching_outputs_removes_stale_component_libraries(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ('component', 'static'):
                out = root / name
                version = out / 'Chromium Framework.framework/Versions/1.0'
                version.mkdir(parents=True)
                (version / 'Chromium Framework').write_bytes(name.encode())
                (out / 'zitter').write_bytes(name.encode())
                (out / 'libcurrent.dylib').write_bytes(name.encode())
            old = root / 'component/libstale.dylib'
            old.write_bytes(b'old build')
            app = root / 'Zitter.app'
            packager.package(root / 'component', app, register=False)
            unrelated = app / 'Contents/MacOS/notes.txt'
            unrelated.write_text('keep')
            packager.package(root / 'static', app, register=False)
            for relative in ('MacOS', 'Frameworks',
                             'Frameworks/Chromium Framework.framework/Versions/1.0'):
                folder = app / 'Contents' / relative
                self.assertFalse((folder / old.name).exists())
                self.assertEqual(b'static', (folder / 'libcurrent.dylib').read_bytes())
            self.assertEqual(b'old build', old.read_bytes())
            self.assertEqual('keep', unrelated.read_text())

    def test_helpers_and_component_dependencies_stay_inside_outer_bundle(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            out = root / 'out'
            version = out / 'Chromium Framework.framework/Versions/1.0'
            helper = version / 'Helpers/Chromium Helper.app/Contents/MacOS/Chromium Helper'
            helper.parent.mkdir(parents=True)
            helper.write_bytes(b'helper')
            (version / 'Chromium Framework').write_bytes(b'framework')
            (version.parent / 'Current').symlink_to('1.0')
            (out / 'zitter').write_bytes(b'browser')
            (out / 'libsandbox_mac_seatbelt.dylib').write_bytes(b'sandbox')
            app = root / 'Zitter.app'
            packager.package(out, app)
            packager.package(out, app)
            contents = app / 'Contents'
            target_helper = contents / 'Frameworks' / helper.relative_to(out)
            self.assertTrue(target_helper.is_file())
            self.assertTrue(target_helper.resolve().is_relative_to(app.resolve()))
            for relative in ('MacOS', 'Frameworks',
                             'Frameworks/Chromium Framework.framework/Versions/1.0'):
                self.assertTrue((contents / relative / 'libsandbox_mac_seatbelt.dylib').is_file())
            with (contents / 'Info.plist').open('rb') as stream:
                info = plistlib.load(stream)
            self.assertEqual(info['CFBundleIdentifier'], 'dev.zitter.browser')
            self.assertTrue(info['NSCameraUsageDescription'])
            self.assertTrue(info['NSMicrophoneUsageDescription'])
            self.assertEqual(info['CFBundleURLTypes'][0]['CFBundleURLSchemes'], ['http', 'https'])
            self.assertTrue((contents / 'MacOS' / info['CFBundleExecutable']).is_file())

    def test_notification_helper_uses_zitter_icon_without_touching_build_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            out = root / 'out'
            version = out / 'Chromium Framework.framework/Versions/1.0'
            alerts = version / 'Helpers/Chromium Helper (Alerts).app/Contents'
            (alerts / 'Resources').mkdir(parents=True)
            (alerts / 'Resources/app.icns').write_bytes(b'chromium icon')
            (alerts / 'Resources/Assets.car').write_bytes(b'chromium assets')
            with (alerts / 'Info.plist').open('wb') as stream:
                plistlib.dump({'CFBundleName': 'Chromium Helper (Alerts)',
                               'CFBundleIconName': 'AppIcon'}, stream)
            (version.parent / 'Current').symlink_to('1.0')
            (out / 'zitter').write_bytes(b'browser')
            app = root / 'Zitter.app'
            packager.package(out, app)
            packager.package(out, app)
            target = (app / 'Contents/Frameworks' / alerts.relative_to(out))
            icon = packager.ROOT / 'assets/icon/Zitter.icns'
            self.assertEqual((target / 'Resources/app.icns').read_bytes(), icon.read_bytes())
            self.assertFalse((target / 'Resources/Assets.car').exists())
            with (target / 'Info.plist').open('rb') as stream:
                info = plistlib.load(stream)
            self.assertEqual(info['CFBundleName'], 'Zitter')
            self.assertNotIn('CFBundleIconName', info)
            self.assertEqual((alerts / 'Resources/app.icns').read_bytes(), b'chromium icon')
            self.assertTrue((alerts / 'Resources/Assets.car').exists())
            with (alerts / 'Info.plist').open('rb') as stream:
                self.assertEqual(plistlib.load(stream)['CFBundleName'], 'Chromium Helper (Alerts)')

    def test_unowned_bundle_is_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'zitter').write_bytes(b'browser')
            app = root / 'Other.app'
            app.mkdir()
            with self.assertRaisesRegex(ValueError, 'unowned bundle'):
                packager.package(root, app)
            self.assertEqual(list(app.iterdir()), [])
