#!/usr/bin/env python3
# Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
"""Compile/run the CoreText editor geometry harness without opening windows."""
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    if sys.platform != 'darwin':
        raise SystemExit('This harness requires the macOS SDK (CoreText/AppKit).')
    root = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix='zitter-editor-layout-') as directory:
        binary = Path(directory) / 'editor_text_layout_tests'
        subprocess.run([
            'xcrun', 'clang++', '-std=c++20', '-fobjc-arc', '-Wall', '-Wextra',
            '-Werror', str(root / 'chromium/prototype/editor_text_layout_test.mm'),
            '-framework', 'AppKit', '-framework', 'CoreText', '-framework',
            'CoreGraphics', '-o', str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
