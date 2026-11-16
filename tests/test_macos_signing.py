from pathlib import Path
import plistlib
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import macos_signing as signing


class SigningTests(unittest.TestCase):
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
            signing.os.link(library, alias)
            with mock.patch.object(signing.subprocess, 'run') as run:
                signing.sign_bundle(app, 'test')
            signed_paths = [c.args[0][-1] for c in run.call_args_list if '--sign' in c.args[0]]
            self.assertEqual({str(library), str(alias), str(app)}, set(signed_paths))

    def test_signing_identity_is_pinned_and_never_falls_back_to_adhoc(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / 'signing.json'
            with mock.patch.object(signing, 'SIGNING_CONFIG', config), mock.patch.dict(
                    signing.os.environ, {}, clear=True):
                with self.assertRaisesRegex(ValueError, 'permanent certificate'):
                    signing.signing_identity()
                config.write_text('{"identity": "saved-certificate"}')
                self.assertEqual('saved-certificate', signing.signing_identity())
                self.assertEqual('explicit-certificate', signing.signing_identity('explicit-certificate'))
                with self.assertRaises(ValueError):
                    signing.signing_identity('-')

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
            with mock.patch.object(signing.subprocess, 'run') as run:
                signing.sign_bundle(app, 'permanent-certificate')
            calls = [call.args[0] for call in run.call_args_list]
            self.assertEqual([str(library), str(helper), str(app)], [cmd[-1] for cmd in calls[:-1]])
            self.assertIn('--identifier', calls[-2])
            self.assertIn('dev.zitter.browser', calls[-2])
            self.assertIn('--strict', calls[-1])
            self.assertIn('--deep', calls[-1])
            for command in calls[:-1]:
                self.assertNotIn('--deep', command)


if __name__ == '__main__':
    unittest.main()
