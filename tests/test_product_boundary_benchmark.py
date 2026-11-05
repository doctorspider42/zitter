import importlib.util
from pathlib import Path
import unittest

SPEC = importlib.util.spec_from_file_location("boundary_benchmark", Path(__file__).resolve().parents[1] / "tools/benchmark_product_boundary.py")
benchmark = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(benchmark)


class BenchmarkStatisticsTests(unittest.TestCase):
    def test_nearest_rank_p95_keeps_the_tail(self):
        self.assertEqual(20, benchmark.summary(list(range(1, 22)))["p95"])

    def test_pairing_uses_processes_not_iterations(self):
        result = benchmark.paired_summary([10] * 6, [8] * 6)
        self.assertAlmostEqual(-20, result["median_percent"])
        self.assertEqual(6, len(result["paired_percent"]))
        self.assertEqual(2 / 64, result["sign_flip_p"])

    def test_unchanged_processes_are_not_a_gain(self):
        result = benchmark.paired_summary([1, 10, 100], [1, 10, 100])
        self.assertEqual(0, result["median_percent"])
        self.assertEqual(1, result["sign_flip_p"])


if __name__ == "__main__":
    unittest.main()
