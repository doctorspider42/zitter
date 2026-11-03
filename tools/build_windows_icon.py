#!/usr/bin/env python3
"""Build the Windows-only Original XL ICO with Chromium and Pillow.

Usage: python tools/build_windows_icon.py --chromium PATH_TO_CHROMIUM_OR_EDGE
The macOS SVG, PNG and ICNS are never written by this script.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile
import time


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets/icon/zitter-windows.svg"
TARGET = ROOT / "chromium/app/zitter.ico"
SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256)


def build(chromium):
    from PIL import Image

    with tempfile.TemporaryDirectory(prefix="zitter-windows-icon-",
                                     ignore_cleanup_errors=True) as directory:
        work = Path(directory)
        png = work / "zitter-windows.png"
        page = work / "icon.html"
        page.write_text(
            '<!doctype html><body style="margin:0;background:transparent">'
            f'<img src="{SOURCE.as_uri()}" width="1024" height="1024" '
            'style="display:block"></body>', encoding="utf-8")
        subprocess.run([
            str(chromium), "--headless", "--disable-gpu", "--hide-scrollbars",
            "--no-first-run", f"--user-data-dir={work / 'profile'}",
            "--default-background-color=00000000", "--window-size=1024,1024",
            f"--screenshot={png}", page.as_uri(),
        ], check=True, capture_output=True, timeout=60)
        # Windows browser launchers can return before their renderer finishes.
        deadline = time.monotonic() + 30
        while True:
            try:
                with Image.open(png) as image:
                    image.load()
                break
            except OSError:
                if time.monotonic() >= deadline:
                    raise RuntimeError(f"Browser did not write a complete PNG: {png}")
                time.sleep(0.1)
        with Image.open(png) as image:
            if image.size != (1024, 1024):
                raise RuntimeError(f"Unexpected icon render size: {image.size}")
            image.convert("RGBA").save(
                TARGET, format="ICO", sizes=[(size, size) for size in SIZES],
                bitmap_format="png")
    print(TARGET)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--chromium", type=Path, required=True)
    args = parser.parse_args()
    build(args.chromium)


if __name__ == "__main__":
    main()
