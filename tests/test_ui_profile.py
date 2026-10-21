import importlib.util
from pathlib import Path
import sys
import tempfile
import xml.etree.ElementTree as ET
import unittest

TOOLS = Path(__file__).resolve().parents[1] / 'tools'
sys.path.insert(0, str(TOOLS))
import profile_ui
import audit_licenses
import analyze_ui_instruments


class ProfileTests(unittest.TestCase):
    def frame(self, **kwargs):
        return dict(trial=0, start_us=1000, received_us=2000, draw_start_us=3000,
                    swap_start_us=4000, swap_end_us=5000, presentation_us=18000,
                    callback_us=7000, interval_us=8333, **kwargs)

    def test_future_displaylink_timestamp_is_not_callback_failure(self):
        result = profile_ui.summarize_frames([self.frame()], 1)
        self.assertEqual(result['swap_end_to_presentation_ms']['median'], 13)
        self.assertEqual(result['presentation_to_callback_ms']['median'], -11)

    def test_missing_duplicate_and_nonmonotonic_stages_rejected(self):
        frame = self.frame()
        for frames in ([], [frame, frame], [{**frame, 'received_us': 0}],
                       [{**frame, 'received_us': frame['start_us'] - 1}]):
            with self.assertRaises(ValueError):
                profile_ui.summarize_frames(frames, 1)

    def test_warmups_are_excluded(self):
        frame = self.frame()
        result = profile_ui.summarize_frames([{**frame, 'trial': -1}, frame], 1)
        self.assertEqual(result['start_to_received_ms']['median'], 1)

    def test_scanner_errors_remain_visible(self):
        rows, errors = audit_licenses.parse_metadata("Error: missing metadata\n{'Name': 'test', 'License': 'MIT'}\n")
        self.assertEqual(errors, ['Error: missing metadata'])
        self.assertEqual(rows[0]['Name'], 'test')
        with self.assertRaises(ValueError):
            audit_licenses.parse_metadata('Error only')

    def test_ninja_escaped_bundle_names(self):
        self.assertEqual(audit_licenses.ninja_tokens('Chromium$ Framework.framework/Chromium$ Framework lib.a'),
                         ['Chromium Framework.framework/Chromium Framework', 'lib.a'])


class InstrumentsTests(unittest.TestCase):
    def test_gpu_union_excludes_nested_and_foreign_work(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            (directory / 'gpu-toc.xml').write_text('<trace-toc><start-date>2026-10-21T00:00:00+02:00</start-date><processes><process pid="1" path="/test/ZitterUiProfile.app/Contents/MacOS/Chromium"/></processes></trace-toc>')
            (directory / 'gpu-skia.log').write_text('/000010.000:INFO:x ZITTER_UI_SAMPLE {"trial":0,}\n/000011.000:INFO:x ZITTER_UI_SAMPLE {"trial":1,}')
            root = ET.Element('rows')
            for start, duration, pid, depth in [(10000000000, 2000000, '1', '0'),
                                                (10001000000, 3000000, '1', '0'),
                                                (10000000000, 50000000, '1', '1'),
                                                (10000000000, 50000000, '2', '0')]:
                row = ET.SubElement(root, 'row')
                process = ET.SubElement(row, 'process')
                ET.SubElement(process, 'pid').text = pid
                for tag, value in [('start-time', start), ('duration', duration), ('duration', 500000),
                                   ('metal-nesting-level', depth), ('gpu-state', 'Active'),
                                   ('gpu-channel-name', 'Fragment')]:
                    ET.SubElement(row, tag).text = str(value)
            ET.ElementTree(root).write(directory / 'gpu-intervals.xml')
            result = analyze_ui_instruments.summarize(directory, 'gpu', iterations=2)
            self.assertEqual(result['gpu_packets'], 2)
            self.assertEqual(result['gpu_busy_union_ms'], 4)
            self.assertEqual(result['gpu_busy_ms_per_update'], 2)
            # An otherwise valid export must not silently undercount a run
            # whose final measured updates happened after recording stopped.
            toc = ET.parse(directory / 'gpu-toc.xml')
            summary = ET.SubElement(toc.getroot(), 'summary')
            ET.SubElement(summary, 'duration').text = '10.5'
            toc.write(directory / 'gpu-toc.xml')
            with self.assertRaisesRegex(ValueError, 'complete measurement window'):
                analyze_ui_instruments.summarize(directory, 'gpu', iterations=2)
            summary.find('duration').text = '12'
            toc.write(directory / 'gpu-toc.xml')
            self.assertEqual(analyze_ui_instruments.summarize(directory, 'gpu', iterations=2)['recording_duration_seconds'], 12)


if __name__ == '__main__':
    unittest.main()
