import importlib.util
from pathlib import Path
import plistlib
import tempfile
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location(
    'package_app', Path(__file__).resolve().parents[1] / 'tools/package_app.py')
packager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packager)


class PackageTests(unittest.TestCase):
    def test_every_hardlink_path_is_signed_independently(self):
        with tempfile.TemporaryDirectory() as directory:
            app = Path(directory) / 'Zitter.app'
            binary = app / 'Contents/MacOS/Chromium'
            binary.parent.mkdir(parents=True)
            binary.write_bytes(b'\xcf\xfa\xed\xfe' + b'app')
            (app / 'Contents/Info.plist').write_bytes(plistlib.dumps({'CFBundleExecutable': 'Chromium'}))
            library = binary.parent / 'libexample.dylib'
            library.write_bytes(b'\xcf\xfa\xed\xfe' + b'library')
            alias = app / 'Contents/Frameworks/libexample.dylib'
            alias.parent.mkdir()
            packager.os.link(library, alias)
            with mock.patch.object(packager.subprocess, 'run') as run:
                packager.sign_bundle(app, 'test')
            signed_paths = [c.args[0][-1] for c in run.call_args_list if '--sign' in c.args[0]]
            self.assertEqual({str(library), str(alias), str(app)}, set(signed_paths))

    def test_release_version_and_runtime_are_applied_before_signing(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            framework = root / 'out/Chromium Framework.framework/Versions/1.0'
            framework.mkdir(parents=True)
            (framework / 'Chromium Framework').write_bytes(b'framework')
            (root / 'out/zitter').write_bytes(b'app')
            app = root / 'Zitter.app'
            def verify_at_signing(destination, identity, hardened_runtime):
                info = plistlib.loads((destination / 'Contents/Info.plist').read_bytes())
                self.assertEqual('0.2.3', info['CFBundleVersion'])
                self.assertEqual('0.2.3', info['CFBundleShortVersionString'])
                self.assertTrue(hardened_runtime)
            with mock.patch.object(packager, 'sign_bundle', side_effect=verify_at_signing) as sign:
                packager.package(root / 'out', app, register=False, identity='test',
                                 app_version='0.2.3', hardened_runtime=True)
            sign.assert_called_once()
            with self.assertRaisesRegex(ValueError, 'requires a signing identity'):
                packager.package(root / 'out', root / 'Unsigned.app', hardened_runtime=True)

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

    def test_signed_package_does_not_modify_hardlinked_build_outputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            out = root / 'out'
            version = out / 'Chromium Framework.framework/Versions/1.0'
            version.mkdir(parents=True)
            (version / 'Chromium Framework').write_bytes(b'framework')
            (out / 'zitter').write_bytes(b'browser')
            (out / 'libcurrent.dylib').write_bytes(b'library')
            app = root / 'Zitter.app'
            # Migrate a legacy hard-linked package as well as a fresh one.
            packager.package(out, app, register=False)
            with mock.patch.object(packager, 'sign_bundle') as sign:
                packager.package(out, app, register=False, identity='permanent-certificate')
            sign.assert_called_once_with(app.absolute(), 'permanent-certificate')
            binary = app / 'Contents/MacOS/Chromium'
            self.assertFalse(binary.samefile(out / 'zitter'))
            self.assertFalse((app / 'Contents/MacOS/libcurrent.dylib').samefile(out / 'libcurrent.dylib'))
            self.assertFalse((app / 'Contents/Frameworks' / version.relative_to(out) /
                              'Chromium Framework').samefile(version / 'Chromium Framework'))
            binary.write_bytes(b'signed-browser')
            self.assertEqual(b'browser', (out / 'zitter').read_bytes())
            self.assertTrue(packager.bundle_marker(app).is_file())
            self.assertFalse((app / 'Contents/zitter-development-bundle').exists())

    def test_license_collection_is_bundled_before_signing(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            out = root / 'out'
            framework = out / 'Chromium Framework.framework/Versions/1.0'
            framework.mkdir(parents=True)
            (framework / 'Chromium Framework').write_bytes(b'framework')
            (out / 'zitter').write_bytes(b'app')
            notices = root / 'notices.txt'
            notices.write_text('reviewed third-party notices')
            app = root / 'Zitter.app'
            def verify_at_signing(*_):
                resources = app / 'Contents/Resources'
                self.assertEqual((resources / 'THIRD_PARTY_LICENSES.txt').read_text(), notices.read_text())
                self.assertTrue((resources / 'LICENSE').is_file())
                self.assertTrue((resources / 'NOTICE').is_file())
                # The design system's fonts travel with their OFL licenses.
                self.assertTrue((resources / 'Fonts/InstrumentSerif-Regular.ttf').is_file())
                self.assertTrue((resources / 'Fonts/SchibstedGrotesk-OFL.txt').is_file())
                with (app / 'Contents/Info.plist').open('rb') as stream:
                    self.assertEqual(packager.plistlib.load(stream)['ATSApplicationFontsPath'], 'Fonts')
            with mock.patch.object(packager, 'sign_bundle', side_effect=verify_at_signing):
                packager.package(out, app, register=False, identity='test', third_party_licenses=notices)

    def test_signing_identity_is_pinned_and_never_falls_back_to_adhoc(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / 'signing.json'
            with mock.patch.object(packager, 'SIGNING_CONFIG', config), mock.patch.dict(
                    packager.os.environ, {}, clear=True):
                with self.assertRaisesRegex(ValueError, 'permanent certificate'):
                    packager.signing_identity()
                config.write_text('{"identity": "saved-certificate"}')
                self.assertEqual('saved-certificate', packager.signing_identity())
                self.assertEqual('explicit-certificate', packager.signing_identity('explicit-certificate'))
                with self.assertRaises(ValueError):
                    packager.signing_identity('-')

    def test_nested_code_is_signed_before_its_parent_and_verified_strictly(self):
        with tempfile.TemporaryDirectory() as directory:
            app = Path(directory) / 'Zitter.app'
            helper = app / 'Contents/Frameworks/Helper.app'
            for bundle, name in ((app, 'Chromium'), (helper, 'Helper')):
                binary = bundle / 'Contents/MacOS' / name
                binary.parent.mkdir(parents=True)
                binary.write_bytes(b'\xcf\xfa\xed\xfe' + b'code')
                with (bundle / 'Contents/Info.plist').open('wb') as stream:
                    plistlib.dump({'CFBundleExecutable': name}, stream)
            library = app / 'Contents/Frameworks/libexample.dylib'
            library.write_bytes(b'\xcf\xfa\xed\xfe' + b'library')
            with mock.patch.object(packager.subprocess, 'run') as run:
                packager.sign_bundle(app, 'permanent-certificate')
            calls = [call.args[0] for call in run.call_args_list]
            self.assertEqual([str(library), str(helper), str(app)], [cmd[-1] for cmd in calls[:-1]])
            self.assertIn('--identifier', calls[-2])
            self.assertIn('dev.zitter.browser', calls[-2])
            self.assertIn('--strict', calls[-1])
            self.assertIn('--deep', calls[-1])
            for command in calls[:-1]:
                self.assertNotIn('--deep', command)

    def test_failed_signing_preserves_installed_bundle_and_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            out = root / 'out'
            version = out / 'Chromium Framework.framework/Versions/1.0'
            version.mkdir(parents=True)
            (version / 'Chromium Framework').write_bytes(b'framework')
            (out / 'zitter').write_bytes(b'new-browser')
            app = root / 'Zitter.app'
            marker = app / 'Contents/zitter-development-bundle'
            marker.parent.mkdir(parents=True)
            marker.write_text('old-build')
            installed = app / 'Contents/old-browser'
            installed.write_bytes(b'working-browser')
            config = root / 'signing.json'
            config.write_text('{"identity": "existing-certificate"}')
            argv = ['package_app.py', '--out', str(out), '--destination', str(app),
                    '--sign-identity', 'new-certificate', '--no-register']
            with mock.patch.object(packager.sys, 'argv', argv), mock.patch.object(
                    packager, 'SIGNING_CONFIG', config), mock.patch.object(
                    packager, 'sign_bundle', side_effect=ValueError('signing failed')):
                with self.assertRaises(SystemExit):
                    packager.main()
            self.assertEqual(b'working-browser', installed.read_bytes())
            self.assertEqual('old-build', marker.read_text())
            self.assertEqual('{"identity": "existing-certificate"}', config.read_text())
            self.assertEqual([], list(root.glob('.zitter-package-*')))

    def test_unowned_bundle_is_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'zitter').write_bytes(b'browser')
            app = root / 'Other.app'
            app.mkdir()
            with self.assertRaisesRegex(ValueError, 'unowned bundle'):
                packager.package(root, app)
            self.assertEqual(list(app.iterdir()), [])
