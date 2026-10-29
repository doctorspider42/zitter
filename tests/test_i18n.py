import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("i18n_check", ROOT / "tools/i18n_check.py")
check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check)


class UiLanguageTests(unittest.TestCase):
    def test_every_wrapped_string_has_english(self):
        gaps = check.missing()
        self.assertFalse(gaps, "untranslated (tools/i18n_check.py --missing):\n" +
                         "\n".join(f'{sorted(files)} "{text}"' for text, files in sorted(gaps.items())[:40]))

    def test_catalogues_hold_each_string_once_with_matching_placeholders(self):
        for name in check.CATALOGUES:
            entries, duplicates = check.catalogue(name)
            self.assertEqual([], duplicates, name)
            for polish, english in entries.items():
                self.assertTrue(english, polish)
                self.assertEqual(polish.count("{}"), english.count("{}"), polish)


if __name__ == "__main__":
    unittest.main()
