#!/usr/bin/env python3
"""Render assets/icon/zitter.svg into assets/icon/Zitter.icns (macOS only).

Rasterizes with the locally built Chromium in headless mode, so no image
libraries are needed, then assembles the iconset with Apple's iconutil.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets/icon/zitter.svg"
TARGET = ROOT / "assets/icon/Zitter.icns"
# iconutil names: icon_<size>x<size>[@2x].png
SIZES = [(16, 1), (16, 2), (32, 1), (32, 2), (128, 1), (128, 2),
         (256, 1), (256, 2), (512, 1), (512, 2)]


def render(chromium, svg, png, pixels, profile):
    page = png.with_suffix(".html")
    page.write_text(
        "<!doctype html><html><body style='margin:0;background:transparent'>"
        f"<img src='{svg.as_uri()}' width='{pixels}' height='{pixels}' "
        "style='display:block'></body></html>")
    subprocess.run([
        str(chromium), "--headless", "--disable-gpu", "--hide-scrollbars",
        "--no-first-run", f"--user-data-dir={profile}",
        "--default-background-color=00000000",
        f"--window-size={pixels},{pixels}", f"--screenshot={png}",
        page.as_uri()], check=True, capture_output=True)
    if not png.is_file():
        raise RuntimeError(f"Chromium did not write {png}")


def build(chromium, source=SOURCE, target=TARGET):
    if sys.platform != "darwin":
        raise RuntimeError("iconutil is macOS-only")
    with tempfile.TemporaryDirectory(prefix="zitter-icon-") as directory:
        work = Path(directory)
        iconset = work / "Zitter.iconset"
        iconset.mkdir()
        for size, scale in SIZES:
            suffix = "@2x" if scale == 2 else ""
            render(chromium, source, iconset / f"icon_{size}x{size}{suffix}.png",
                   size * scale, work / "profile")
        for html in iconset.glob("*.html"):
            html.unlink()
        subprocess.run(["iconutil", "-c", "icns", str(iconset), "-o", str(target)],
                       check=True)
        shutil.copy2(iconset / "icon_512x512@2x.png", target.with_name("zitter-1024.png"))
    print(target)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--chromium", type=Path,
                        default=ROOT / ".chromium/src/out/Zitter/Chromium.app/Contents/MacOS/Chromium")
    args = parser.parse_args()
    try:
        build(args.chromium)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Icon build failed: {error}\n")


if __name__ == "__main__":
    main()
