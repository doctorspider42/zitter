import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import install_archive_backend as backend


class ArchivePatchTest(unittest.TestCase):
    def test_build_patch_keeps_rar_capability_and_uses_gn_comments(self):
        source = 'if (safe_browsing_use_unrar) { deps += [ "//third_party/unrar" ] }'
        patched = backend.patch('chrome/utility/safe_browsing/BUILD.gn', source)
        self.assertTrue(patched.startswith('#'))
        self.assertIn('safe_browsing_use_unrar', patched)
        self.assertNotIn('"//third_party/unrar"', patched)
        self.assertEqual(patched, backend.patch('chrome/utility/safe_browsing/BUILD.gn', patched))

    def test_read_failure_finishes_failed_analysis_instead_of_waiting_for_nested_work(self):
        source = '#include "third_party/unrar/google/unrar_wrapper.h"\n  if (reader_.HasWriteError()) {\n    results()->analysis_result = ArchiveAnalysisResult::kDiskError;\n    results()->success = false;\n    return false;\n  }'
        patched = backend.patch('chrome/utility/safe_browsing/rar_analyzer.cc', source)
        self.assertIn('reader_.HasReadError()', patched)
        self.assertEqual(patched.count('return true;'), 2)
        self.assertEqual(patched, backend.patch('chrome/utility/safe_browsing/rar_analyzer.cc', patched))
        with self.assertRaises(ValueError):
            backend.patch('unreviewed.cc', 'unknown upstream layout')
