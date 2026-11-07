from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import package_dmg as dmg


class DmgTests(unittest.TestCase):
    def test_pending_apple_submission_cannot_produce_a_final_dmg(self):
        with tempfile.TemporaryDirectory() as directory:
            with mock.patch.object(dmg, 'wait_for_acceptance', return_value=False), mock.patch.object(dmg, 'run') as run:
                self.assertFalse(dmg.finish(Path(directory), {}, 'test', 0, 1))
            run.assert_not_called()

    def test_only_a_verified_stapled_image_becomes_the_release_artifact(self):
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory)
            image = destination / 'submission.dmg'
            image.write_bytes(b'submitted image')
            state = {'version': '0.1.0', 'architecture': 'arm64', 'app_cdhash': 'abc'}
            events = []
            def run(command, capture=False):
                events.append('stapled')
                Path(command[-1]).write_bytes(b'stapled image')
            def verify(output, cdhash):
                self.assertEqual('abc', cdhash)
                self.assertEqual(b'stapled image', output.read_bytes())
                events.append('verified')
            with mock.patch.object(dmg, 'wait_for_acceptance', return_value=True), mock.patch.object(
                    dmg, 'run', side_effect=run), mock.patch.object(dmg, 'verify_image', side_effect=verify):
                self.assertTrue(dmg.finish(destination, state, 'test', 0, 1))
            self.assertEqual(['stapled', 'verified'], events)
            self.assertEqual(b'submitted image', image.read_bytes())
            self.assertEqual('Complete', state['status'])
            self.assertEqual(dmg.sha256(destination / state['artifact']), state['artifact_sha256'])

    def test_failed_distribution_check_does_not_mark_the_image_complete(self):
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory)
            (destination / 'submission.dmg').write_bytes(b'submitted image')
            state = {'version': '0.1.0', 'architecture': 'arm64', 'app_cdhash': 'abc', 'status': 'Accepted'}
            with mock.patch.object(dmg, 'wait_for_acceptance', return_value=True), mock.patch.object(
                    dmg, 'run'), mock.patch.object(dmg, 'verify_image', side_effect=subprocess.CalledProcessError(1, 'spctl')):
                with self.assertRaises(subprocess.CalledProcessError):
                    dmg.finish(destination, state, 'test', 0, 1)
            self.assertEqual('Accepted', state['status'])
            self.assertFalse(list(destination.glob('SHA256SUMS*')))

    def test_finder_layout_places_icons_on_the_background_artwork(self):
        script = dmg.finder_layout_script('Zitter 1')
        self.assertIn('tell disk "Zitter 1"', script)
        self.assertIn('set bounds of container window to {200, 120, 920, 580}', script)
        self.assertIn('file ".background:background.tiff"', script)
        self.assertIn('set position of item "Zitter.app" of container window to {200, 236}', script)
        self.assertIn('set position of item "Applications" of container window to {520, 236}', script)

    def test_background_is_rendered_for_the_version_and_architecture(self):
        with tempfile.TemporaryDirectory() as directory:
            commands = []
            with mock.patch.object(dmg, 'run', side_effect=lambda command, capture=False: commands.append(command)):
                dmg.render_background(Path(directory) / '.background', '0.4.1', 'arm64', Path(directory) / 'work')
            self.assertEqual(['0.4.1', 'Apple Silicon'], commands[0][3:5])
            self.assertEqual('tiffutil', commands[1][0])
            self.assertTrue(commands[1][-1].endswith('.background/background.tiff'))


if __name__ == '__main__':
    unittest.main()
