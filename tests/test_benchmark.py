import importlib.util
import json
from pathlib import Path
import unittest


spec = importlib.util.spec_from_file_location(
    "benchmark_browser", Path(__file__).resolve().parents[1] / "tools/benchmark_browser.py")
benchmark = importlib.util.module_from_spec(spec)
spec.loader.exec_module(benchmark)


class BenchmarkResultsTest(unittest.TestCase):
    def output(self, count=20):
        return "\n".join("[INFO] ZITTER_PERF " + json.dumps({
            "metric": metric, "trial": trial, "ms": trial + 1,
        }) for metric in benchmark.METRICS for trial in range(count))

    def test_preserves_samples_and_uses_nearest_rank_p95(self):
        result = benchmark.summarize(self.output(), 20)
        for values in result.values():
            self.assertEqual(values["samples_ms"], list(range(1, 21)))
            self.assertEqual(values["median_ms"], 10.5)
            self.assertEqual(values["p95_ms"], 19)

    def test_rejects_missing_trials(self):
        with self.assertRaisesRegex(ValueError, "Incomplete"):
            benchmark.summarize(self.output(19), 20)

    def test_identical_launcher_echo_does_not_duplicate_samples(self):
        result = benchmark.summarize(self.output() + "\n" + self.output(), 20)
        self.assertEqual(len(result[benchmark.METRICS[0]]["samples_ms"]), 20)

    def test_rejects_conflicting_repeated_sample(self):
        line = 'ZITTER_PERF {"metric":"split_dispatch_ms","trial":0,"ms":999}'
        with self.assertRaisesRegex(ValueError, "Conflicting"):
            benchmark.summarize(self.output() + "\n" + line, 20)

    def test_rejects_nonfinite_samples(self):
        line = 'ZITTER_PERF {"metric":"split_dispatch_ms","trial":0,"ms":NaN}'
        with self.assertRaisesRegex(ValueError, "Invalid"):
            benchmark.summarize(line, 20)

    def test_restoration_metrics_and_resource_samples_keep_their_units(self):
        names = benchmark.SCENARIOS["restoration"][1]
        output = "\n".join("ZITTER_PERF " + json.dumps({
            "metric": metric, "trial": trial, "ms": 10 + trial,
        }) for metric in names for trial in range(2))
        for trial in range(2):
            output += "\nZITTER_RESOURCE " + json.dumps({
                "trial": trial, "realized_pages": 2, "saved_tabs": 50,
                "browser_resident_bytes": 100, "browser_physical_footprint_bytes": 80,
            })
        self.assertEqual(set(names), set(benchmark.summarize(output, 2, names)))
        self.assertEqual([2, 2], [s["realized_pages"] for s in benchmark.resources(output, 2)])
        with self.assertRaisesRegex(ValueError, "Incomplete"):
            benchmark.resources(output, 3)

    def test_rejects_impossible_resource_counts(self):
        with self.assertRaisesRegex(ValueError, "Invalid"):
            benchmark.resources('ZITTER_RESOURCE {"trial":0,"realized_pages":51,"saved_tabs":50}', 1)

    def test_profile_page_requires_a_paint_sample_not_just_ready(self):
        names = benchmark.SCENARIOS["profile-page"][1]
        output = "\n".join("ZITTER_PERF " + json.dumps({
            "metric": metric, "trial": 0, "ms": 100,
        }) for metric in benchmark.SCENARIOS["profiles"][1])
        with self.assertRaisesRegex(ValueError, "Incomplete.*profiles_first_visual_paint_ms"):
            benchmark.summarize(output, 1, names)
        output += '\nZITTER_PERF {"metric":"profiles_first_visual_paint_ms","trial":0,"ms":150}'
        self.assertEqual(150, benchmark.summarize(output, 1, names)["profiles_first_visual_paint_ms"]["median_ms"])

    def test_memory_tree_follows_out_of_order_descendants_and_excludes_other_apps(self):
        rows = "30 20 3 renderer --type=renderer\n99 1 999 personal-browser\n10 1 1 launcher\n20 10 2 browser\n40 20 4 gpu --type=gpu-process"
        self.assertEqual({"process_count": 4, "renderer_count": 1,
                          "total_rss_bytes": 10 * 1024, "renderer_rss_bytes": 3 * 1024},
                         benchmark.process_tree_sample(rows, 10))
        self.assertEqual(0, benchmark.process_tree_sample(rows, 123)["total_rss_bytes"])

    def test_memory_tree_rejects_negative_and_duplicate_rss_rows(self):
        for rows in ["10 1 -1 process", "10 1 5 process\n10 1 5 process", "unexpected row"]:
            with self.subTest(rows=rows), self.assertRaises(ValueError):
                benchmark.process_tree_sample(rows, 10)

    def test_renderer_diagnostics_require_complete_real_boolean_samples(self):
        self.assertEqual([], benchmark.renderer_samples("", 1))
        with self.assertRaisesRegex(ValueError, "Incomplete"):
            benchmark.renderer_samples("", 1, required=True)
        line = 'ZITTER_RENDERER {"trial":0,"spares_before":2,"ready_matching_spares_before":1,"used_spare":true}'
        self.assertTrue(benchmark.renderer_samples(line + "\n" + line, 1, required=True)[0]["used_spare"])
        with self.assertRaisesRegex(ValueError, "Invalid"):
            benchmark.renderer_samples(line.replace('true', '1'), 1)

    def test_connection_experiment_requires_no_speculative_get_and_socket_reuse(self):
        value = {"trial": 0, "connections_before_navigation": 1, "accepted_connections": 1,
                 "http_requests": 1, "http_requests_before_navigation": 0,
                 "simulated_rtt_ms": 120, "typing_pause_ms": 600}
        line = "ZITTER_CONNECTION " + json.dumps(value)
        self.assertEqual([value], benchmark.connection_samples(line, 1, True))
        for key, wrong in [("http_requests_before_navigation", 1), ("accepted_connections", 2),
                           ("connections_before_navigation", 0), ("typing_pause_ms", 1)]:
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, "Invalid"):
                benchmark.connection_samples("ZITTER_CONNECTION " + json.dumps({**value, key: wrong}), 1, True)
        with self.assertRaisesRegex(ValueError, "Incomplete"):
            benchmark.connection_samples(line, 2, True)
