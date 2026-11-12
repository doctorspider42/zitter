#!/usr/bin/env python3
"""Build the Windows-only stable or Dev ICO with Chromium and Pillow.

Usage: python tools/build_windows_icon.py --chromium PATH_TO_CHROMIUM_OR_EDGE [--dev]
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
DEV_COLORS = {
    "#2C2652": "#3F531B",
    "#14121E": "#1B270F",
    "#08070C": "#0A1007",
    "#8B83FF": "#D7FF3F",
    "#F6F3FF": "#F8FFDF",
    "#D0CCDF": "#DCE9C5",
    "#B6B1C9": "#CBD7AB",
    "#92909F": "#A2AE85",
    "#AAA1FF": "#E7FF86",
    "#8175EC": "#A9CD32",
}


def build(chromium, dev=False):
    from PIL import Image

    with tempfile.TemporaryDirectory(prefix="zitter-windows-icon-",
                                     ignore_cleanup_errors=True) as directory:
        work = Path(directory)
        source = SOURCE
        target = TARGET
        if dev:
            svg = SOURCE.read_text(encoding="utf-8")
            for stable, preview in DEV_COLORS.items():
                svg = svg.replace(stable, preview)
            source = work / "zitter-dev.svg"
            source.write_text(svg, encoding="utf-8")
            target = TARGET.with_name("zitter-dev.ico")
        png = work / "zitter-windows.png"
        page = work / "icon.html"
        page.write_text(
            '<!doctype html><body style="margin:0;background:transparent">'
            f'<img src="{source.as_uri()}" width="1024" height="1024" '
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
                target, format="ICO", sizes=[(size, size) for size in SIZES],
                bitmap_format="png")
    print(target)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--chromium", type=Path, required=True)
    parser.add_argument("--dev", action="store_true", help="Generate the Zitter Dev icon")
    args = parser.parse_args()
    build(args.chromium, args.dev)


if __name__ == "__main__":
    main()
