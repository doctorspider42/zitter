import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("boundaries", ROOT / "tools/check_boundaries.py")
boundaries = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(boundaries)


class BoundaryTests(unittest.TestCase):
    def check_header(self, header):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "zitter/core/nested/policy.h"
            path.parent.mkdir(parents=True)
            path.write_text(f'#include "{header}"\n')
            return boundaries.violations(root)

    def test_product_sources_respect_boundary(self):
        self.assertEqual([], boundaries.violations(ROOT))

    def test_checks_nested_sources(self):
        for header in ("base/time/time.h", "ui/views/view.h", "chrome/browser/profiles/profile.h",
                       "content/public/browser/web_contents.h", "AppKit/AppKit.h"):
            with self.subTest(header=header):
                self.assertEqual(1, len(self.check_header(header)))

    def test_rejects_relative_escape(self):
        self.assertEqual(1, len(self.check_header("zitter/core/../../chromium/prototype/browser_data.h")))

    def test_allows_product_contracts_and_standard_library(self):
        for header in ("span", "optional", "zitter/core/browser_state.h",
                       "zitter/application/workspace_controller.h"):
            with self.subTest(header=header):
                self.assertEqual([], self.check_header(header))


if __name__ == "__main__":
    unittest.main()
