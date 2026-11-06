import importlib.util
from pathlib import Path
import unittest
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("package_win", ROOT / "tools/package_win.py")
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class WindowsPackageTest(unittest.TestCase):
    def test_keeps_what_runs_and_drops_symbols_sources_and_test_data(self):
        files = package.select_files([
            "./zitter.exe", "./zitter_assistant.exe", "./zitter.exe.pdb", "./base.dll", "chrome_elf.dll", "resources.pak",
            "locales/pl.pak", "locales/pl.pak.info", "Fonts/InstrumentSerif-Regular.ttf", "icudtl.dat",
            "v8_context_snapshot.bin", "gen/third_party/devtools-frontend/src/front_end/Tests.js",
            "pyproto/google/__init__.py", "angledata/foo.bin", "../../third_party/x.txt", "zitter.lib",
            "VkLayer_khronos_validation.dll", "msdia140.dll", "dbghelp.dll", "locales/en-XA.pak",
        ])
        self.assertEqual(files, sorted(["zitter.exe", "zitter_assistant.exe", "base.dll", "chrome_elf.dll", "resources.pak",
                                        "locales/pl.pak", "Fonts/InstrumentSerif-Regular.ttf", "icudtl.dat",
                                        "v8_context_snapshot.bin"]))

    def test_refuses_a_build_without_zitter(self):
        with self.assertRaises(ValueError):
            package.select_files(["./chrome.exe", "./base.dll"])

    def test_refuses_a_build_without_native_assistant(self):
        with self.assertRaisesRegex(ValueError, 'zitter_assistant.exe'):
            package.select_files(['zitter.exe', 'base.dll'])

    def test_version_comes_from_the_exe_resource(self):
        self.assertRegex(package.version(), r"^\d+\.\d+\.\d+$")

    def test_archive_contains_project_and_font_license_notices(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            (folder / "zitter.exe").write_bytes(b"MZ fixture")
            archive = folder / "portable.zip"
            package.write_zip(archive, folder, ["zitter.exe"], prefix="Zitter/",
                              extras=package.package_notices())
            with zipfile.ZipFile(archive) as result:
                for name in ("LICENSE", "NOTICE", "Fonts/InstrumentSerif-OFL.txt",
                             "Fonts/JetBrainsMono-OFL.txt", "Fonts/SchibstedGrotesk-OFL.txt"):
                    self.assertGreater(len(result.read("Zitter/" + name)), 100)


if __name__ == "__main__":
    unittest.main()
