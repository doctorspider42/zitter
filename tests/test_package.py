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
            self.assertTrue((contents / 'MacOS' / info['CFBundleExecutable']).is_file())

    def test_unowned_bundle_is_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'zitter').write_bytes(b'browser')
            app = root / 'Other.app'
            app.mkdir()
            with self.assertRaisesRegex(ValueError, 'unowned bundle'):
                packager.package(root, app)
            self.assertEqual(list(app.iterdir()), [])
