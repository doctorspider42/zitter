import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("gen_design_tokens", ROOT / "tools/gen_design_tokens.py")
gen = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gen)


class DesignTokensTests(unittest.TestCase):
    def test_generated_rust_matches_tokens_json(self):
        expected = gen.render(json.loads(gen.SOURCE.read_text(encoding="utf-8")))
        self.assertEqual(gen.OUTPUT.read_text(encoding="utf-8"), expected,
                         "design_tokens.rs is stale; run tools/gen_design_tokens.py")

    def test_every_fixed_token_reaches_rust_and_derived_ones_do_not(self):
        data = json.loads(gen.SOURCE.read_text(encoding="utf-8"))
        rust = gen.OUTPUT.read_text(encoding="utf-8")
        for token in data["color"]["tokens"]:
            name = gen.const_name(token["name"])
            if gen.DERIVED.match(token["name"]):
                self.assertNotIn(f" {name}: Themed", rust)
            else:
                self.assertIn(f" {name}: Themed", rust)
        for group in data["type"]["groups"]:
            for style in group["styles"]:
                self.assertIn(f" {gen.const_name(style['name'])}: TypeStyle", rust)

    def test_aliases_resolve_per_theme_and_alpha_is_kept(self):
        tokens = [
            {"name": "base", "value": {"dark": "#000000", "light": "#ffffff"}},
            {"name": "alias", "value": "{base}"},
            {"name": "veil", "value": {"dark": "#0c0b11b8", "light": "#16141c52"}},
        ]
        resolved = gen.resolve_colors(tokens)
        self.assertEqual(resolved["alias"], {"dark": "#000000", "light": "#ffffff"})
        self.assertEqual(gen.argb(resolved["veil"]["dark"]), "0xb80c0b11")
        with self.assertRaises(ValueError):
            gen.argb("red")


if __name__ == "__main__":
    unittest.main()
