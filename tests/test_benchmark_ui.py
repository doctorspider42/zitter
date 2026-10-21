import importlib.util
import json
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("benchmark_ui", Path(__file__).resolve().parents[1] / "tools/benchmark_ui.py")
benchmark = importlib.util.module_from_spec(spec)
spec.loader.exec_module(benchmark)


class UiResultsTest(unittest.TestCase):
    def output(self, backend="views", samples=None):
        records = [("ZITTER_UI_READY", {"backend": backend, "scene_parity": True, "page_surface": "native-webcontents"}),
                   ("ZITTER_UI_RESOURCE", {"browser_cpu_ms": 2, "wall_ms": 20, "browser_footprint_bytes": 100}),
                   ("ZITTER_UI_IDLE", {"browser_cpu_ms": 1, "wall_ms": 2000}),
                   ("ZITTER_UI_STATUS", {"ok": True})]
        for sample in samples if samples is not None else [self.sample()]:
            records.append(("ZITTER_UI_SAMPLE", sample))
        return "\n".join(marker + " " + json.dumps(value) for marker, value in records)

    def sample(self, **values):
        return {"trial": 0, "scene_ms": .01, "submit_ms": .1, "update_to_present_ms": 8,
                "metal_gpu_ms": -1, "feedback_flags": 1, **values}

    def test_valid_reference(self):
        self.assertEqual(len(benchmark.parse_run(self.output(), "views", 1)["samples"]), 1)

    def test_rejects_incomplete_duplicate_or_mismatched_runs(self):
        for output in (self.output(samples=[]), self.output(samples=[self.sample(), self.sample()]),
                       self.output("rust-skia"), self.output() + '\nZITTER_UI_STATUS {"ok":false}'):
            with self.assertRaises(ValueError):
                benchmark.parse_run(output, "views", 1)

    def test_rejects_invalid_presentation_and_timing(self):
        for values in ({"update_to_present_ms": -1}, {"scene_ms": float("nan")},
                       {"feedback_flags": 16}, {"trial": 1}, {"trial": False}, {"metal_gpu_ms": 0}):
            with self.assertRaises(ValueError):
                benchmark.parse_run(self.output(samples=[self.sample(**values)]), "views", 1)

    def test_metal_requires_gpu_sample(self):
        with self.assertRaises(ValueError):
            benchmark.parse_run(self.output("rust-metal"), "rust-metal", 1)
        parsed = benchmark.parse_run(self.output("rust-metal", [self.sample(metal_gpu_ms=.2)]), "rust-metal", 1)
        self.assertEqual(parsed["samples"][0]["metal_gpu_ms"], .2)

    def test_requires_complete_idle_and_resource_measurements(self):
        for marker in ("ZITTER_UI_IDLE", "ZITTER_UI_RESOURCE"):
            output = "\n".join(line for line in self.output().splitlines() if not line.startswith(marker))
            with self.assertRaises(ValueError):
                benchmark.parse_run(output, "views", 1)

    def test_summary_keeps_process_repetitions_separate(self):
        runs = []
        for value in (1, 3):
            run = benchmark.parse_run(self.output(samples=[self.sample(update_to_present_ms=value)]), "views", 1)
            runs.append({"backend": "views", **run})
        metric = benchmark.aggregate(runs)["views"]["update_to_present_ms"]
        self.assertEqual(metric["run_medians"], [1, 3])
        self.assertEqual(metric["median"], 2)

    def test_validates_warmup_drop_accounting(self):
        output = self.output("rust-metal", [self.sample(metal_gpu_ms=.2)])
        self.assertEqual(benchmark.parse_run(output + '\nZITTER_UI_WARMUP_DROP 1', "rust-metal", 1)["warmup_dropped_frames"], 1)
        with self.assertRaises(ValueError):
            benchmark.parse_run(output + '\nZITTER_UI_WARMUP_DROP 2', "rust-metal", 1)
        with self.assertRaises(ValueError):
            benchmark.parse_run(self.output() + '\nZITTER_UI_WARMUP_DROP 1', "views", 1)


class CaptureResultsTest(unittest.TestCase):
    def sample(self):
        return {'trial': 0, 'marker_id': 11, 'start_us': 1000000}

    def capture(self, **changes):
        return json.dumps({'ready': True}) + '\n' + json.dumps({
            'marker_id': 11, 'scene_verified': True, 'display_ns': 1010000000,
            'callback_ns': 1020000000, **changes})

    def test_windowserver_timestamp_is_distinct_from_delivery(self):
        sample = self.sample()
        benchmark.correlate_capture([sample], self.capture())
        self.assertEqual(sample['windowserver_present_ms'], 10)
        self.assertEqual(sample['capture_delivery_ms'], 10)

    def test_windowserver_can_deliver_a_frame_before_its_display_deadline(self):
        sample = self.sample()
        benchmark.correlate_capture([sample], self.capture(callback_ns=1005000000))
        self.assertEqual(sample['capture_delivery_ms'], -5)

    def test_missing_stale_unverified_and_invalid_clock_frames_fail(self):
        for text in (json.dumps({'ready': True}), self.capture(marker_id=12),
                     self.capture(scene_verified=False), self.capture(display_ns=999000000),
                     self.capture(callback_ns=999000000), self.capture() + '\n' + json.dumps({'error': 'stopped'})):
            with self.assertRaises(ValueError):
                benchmark.correlate_capture([self.sample()], text)


if __name__ == "__main__":
    unittest.main()
