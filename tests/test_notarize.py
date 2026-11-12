import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import notarize_app as notarizer
import package_app as packager


class NotarizationTests(unittest.TestCase):
    def setUp(self):
        patcher = mock.patch.object(notarizer, 'verify_nested_signatures')
        patcher.start()
        self.addCleanup(patcher.stop)

    def fixture(self, directory):
        destination = Path(directory)
        (destination / 'submission.zip').write_bytes(b'original signed archive')
        state = {'version': '0.1.0', 'architecture': 'arm64', 'cdhash': 'abc',
                 'upload_sha256': notarizer.sha256(destination / 'submission.zip'), 'status': 'Prepared'}
        return destination, state

    def test_hardened_entitlements_are_scoped_to_browser_and_jit_helpers(self):
        app = Path('/Zitter.app')
        browser = packager.runtime_signing_options(app, app)
        self.assertIn('runtime,restrict,kill,library', browser)
        for name in ('Chromium Helper (Renderer).app', 'Chromium Helper (GPU).app',
                     'Chromium Helper (Aperitif Renderer).app', 'Chromium Helper (Aperitif GPU).app'):
            options = packager.runtime_signing_options(app / 'Contents/Frameworks' / name, app)
            self.assertIn('runtime,restrict,kill', options)
            with Path(options[-1]).open('rb') as stream:
                self.assertEqual({'com.apple.security.cs.allow-jit': True}, packager.plistlib.load(stream))
        plain = packager.runtime_signing_options(app / 'Contents/Frameworks/Chromium Helper.app', app)
        self.assertNotIn('--entitlements', plain)
        for path in (packager.ROOT / 'chromium/signing').glob('*.plist'):
            with path.open('rb') as stream:
                self.assertNotIn('com.apple.security.get-task-allow', packager.plistlib.load(stream))

    def test_production_identity_trial_sets_startup_only_on_the_outer_bundle(self):
        with tempfile.TemporaryDirectory() as directory:
            app = Path(directory) / notarizer.CONTENT_APP_NAME
            outer = app / "Contents/Info.plist"
            helper = app / "Contents/Helpers/Helper.app/Contents/Info.plist"
            for path, suffix in ((outer, ""), (helper, ".helper")):
                path.parent.mkdir(parents=True)
                path.write_bytes(packager.plistlib.dumps({
                    "CFBundleIdentifier": notarizer.CONTENT_BUNDLE_ID + suffix,
                }))

            notarizer.configure_production_identity_trial(app)

            outer_info = packager.plistlib.loads(outer.read_bytes())
            helper_info = packager.plistlib.loads(helper.read_bytes())
            self.assertEqual(packager.BUNDLE_ID, outer_info["CFBundleIdentifier"])
            self.assertIs(outer_info["ZitterUsePreparedWorkspace"], True)
            self.assertEqual(packager.BUNDLE_ID + ".helper", helper_info["CFBundleIdentifier"])
            self.assertNotIn("ZitterUsePreparedWorkspace", helper_info)

    def test_content_release_sets_outer_product_name(self):
        with tempfile.TemporaryDirectory() as directory:
            app = Path(directory) / "Zitter.app"
            info = app / "Contents/Info.plist"
            info.parent.mkdir(parents=True)
            info.write_bytes(packager.plistlib.dumps({
                "CFBundleIdentifier": notarizer.CONTENT_BUNDLE_ID,
                "CFBundleExecutable": "Zitter Content Lab",
            }))
            notarizer.configure_production_identity_trial(app, content_release=True)
            values = packager.plistlib.loads(info.read_bytes())
            self.assertEqual(values["CFBundleIdentifier"], packager.BUNDLE_ID)
            self.assertEqual(values["CFBundleName"], "Zitter")
            self.assertEqual(values["CFBundleDisplayName"], "Zitter")
            self.assertEqual(values["CFBundleExecutable"], "Zitter Content Lab")
            self.assertEqual(values["CFBundleURLTypes"], [{
                "CFBundleTypeRole": "Viewer",
                "CFBundleURLName": "dev.zitter.browser.web",
                "CFBundleURLSchemes": ["http", "https"],
            }])
            self.assertTrue(values["ZitterUsePreparedWorkspace"])

    def test_rejected_submission_never_staples_or_creates_release_archive(self):
        with tempfile.TemporaryDirectory() as directory:
            destination, state = self.fixture(directory)
            def command(args, capture=False):
                if 'submit' in args:
                    return subprocess.CompletedProcess(args, 0, json.dumps({'id': 'submission-1'}))
                if 'info' in args:
                    return subprocess.CompletedProcess(args, 0, json.dumps({'status': 'Invalid'}))
                return subprocess.CompletedProcess(args, 0, '')
            with mock.patch.object(notarizer, 'cdhash', return_value='abc'), mock.patch.object(
                    notarizer, 'run', side_effect=command) as run, mock.patch.object(notarizer, 'archive') as archive:
                with self.assertRaisesRegex(ValueError, 'Apple rejected'):
                    notarizer.notarize(destination, state, 'test-profile', 0, 1)
            self.assertEqual('submission-1', json.loads((destination / 'notarization-state.json').read_text())['submission_id'])
            self.assertFalse(any('stapler' in c.args[0] for c in run.call_args_list))
            archive.assert_not_called()

    def test_pending_submission_resumes_without_uploading_again(self):
        with tempfile.TemporaryDirectory() as directory:
            destination, state = self.fixture(directory)
            state['submission_id'] = 'existing-id'
            with mock.patch.object(notarizer, 'cdhash', return_value='abc'), mock.patch.object(
                    notarizer, 'run', return_value=subprocess.CompletedProcess([], 0, '{"status":"In Progress"}')) as run:
                self.assertFalse(notarizer.notarize(destination, state, 'test-profile', 0, 1))
            self.assertFalse(any('submit' in c.args[0] for c in run.call_args_list))
            self.assertEqual('existing-id', state['submission_id'])

    def test_changed_archive_is_not_submitted(self):
        with tempfile.TemporaryDirectory() as directory:
            destination, state = self.fixture(directory)
            (destination / 'submission.zip').write_bytes(b'changed')
            with mock.patch.object(notarizer, 'cdhash', return_value='abc'), mock.patch.object(notarizer, 'run') as run:
                with self.assertRaisesRegex(ValueError, 'changed'):
                    notarizer.notarize(destination, state, 'test-profile', 0, 1)
            self.assertFalse(any('submit' in c.args[0] for c in run.call_args_list))

    def test_accepted_submission_verifies_ticket_and_gatekeeper_before_final_archive(self):
        with tempfile.TemporaryDirectory() as directory:
            destination, state = self.fixture(directory)
            state['submission_id'] = 'accepted-id'
            events = []
            def command(args, capture=False):
                events.append(args)
                if 'log' in args:
                    (destination / 'notarization-log.json').write_text(json.dumps({'sha256': state['upload_sha256']}))
                return subprocess.CompletedProcess(args, 0, '{"status":"Accepted"}' if 'info' in args else '')
            def archive(app, output):
                self.assertTrue(any('spctl' in c for c in events))
                self.assertTrue(any('validate' in c and 'stapler' in c for c in events))
                output.write_bytes(b'final stapled archive')
            with mock.patch.object(notarizer, 'cdhash', return_value='abc'), mock.patch.object(
                    notarizer, 'run', side_effect=command), mock.patch.object(notarizer, 'archive', side_effect=archive):
                self.assertTrue(notarizer.notarize(destination, state, 'test-profile', 0, 1))
            self.assertEqual('Complete', state['status'])
            self.assertEqual(notarizer.sha256(destination / state['artifact']), state['artifact_sha256'])

    def test_gatekeeper_failure_does_not_produce_a_successful_release(self):
        with tempfile.TemporaryDirectory() as directory:
            destination, state = self.fixture(directory)
            state['submission_id'] = 'accepted-id'
            def command(args, capture=False):
                if 'log' in args:
                    (destination / 'notarization-log.json').write_text(json.dumps({'sha256': state['upload_sha256']}))
                if 'spctl' in args:
                    raise subprocess.CalledProcessError(1, args)
                return subprocess.CompletedProcess(args, 0, '{"status":"Accepted"}' if 'info' in args else '')
            with mock.patch.object(notarizer, 'cdhash', return_value='abc'), mock.patch.object(
                    notarizer, 'run', side_effect=command), mock.patch.object(notarizer, 'archive') as archive:
                with self.assertRaises(subprocess.CalledProcessError):
                    notarizer.notarize(destination, state, 'test-profile', 0, 1)
            archive.assert_not_called()
            self.assertNotEqual('Complete', state['status'])

    def test_interrupted_upload_requires_reconciliation_before_resubmitting(self):
        with tempfile.TemporaryDirectory() as directory:
            destination, state = self.fixture(directory)
            state['status'] = 'Submitting'
            with mock.patch.object(notarizer, 'cdhash', return_value='abc'), mock.patch.object(notarizer, 'run') as run:
                with self.assertRaisesRegex(ValueError, 'Previous upload was interrupted'):
                    notarizer.notarize(destination, state, 'test-profile', 0, 1)
            self.assertFalse(any('submit' in c.args[0] for c in run.call_args_list))


class NestedSignatureTests(unittest.TestCase):
    def test_transient_network_read_is_retried_without_resubmission(self):
        command = ['xcrun', 'notarytool', 'info', 'existing-id']
        failure = subprocess.CalledProcessError(1, command, stderr='NSURLErrorDomain Code=-1003')
        success = subprocess.CompletedProcess(command, 0, '{}')
        with mock.patch.object(notarizer, 'run', side_effect=[failure, success]) as run, mock.patch.object(notarizer.time, 'sleep'):
            self.assertIs(success, notarizer.notary_read(command))
        self.assertEqual(2, run.call_count)
        self.assertTrue(all(call.args[0] == command for call in run.call_args_list))

    def test_authentication_error_is_not_retried(self):
        command = ['xcrun', 'notarytool', 'info', 'existing-id']
        failure = subprocess.CalledProcessError(1, command, stderr='HTTP status code 401')
        with mock.patch.object(notarizer, 'run', side_effect=failure) as run:
            with self.assertRaises(subprocess.CalledProcessError):
                notarizer.notary_read(command)
        self.assertEqual(1, run.call_count)

    def test_adhoc_nested_library_is_rejected_even_when_outer_seal_verifies(self):
        with tempfile.TemporaryDirectory() as directory:
            app = Path(directory) / 'Zitter.app'
            app.mkdir()
            (app / 'library.dylib').write_bytes(b'\xcf\xfa\xed\xfe' + b'code')
            details = 'Signature=adhoc\nCodeDirectory flags=0x10000(runtime)\n'
            with mock.patch.object(notarizer, 'run', return_value=subprocess.CompletedProcess([], 0, '', details)):
                with self.assertRaisesRegex(ValueError, 'Missing Developer ID signature'):
                    notarizer.verify_nested_signatures(app)


if __name__ == '__main__':
    unittest.main()
