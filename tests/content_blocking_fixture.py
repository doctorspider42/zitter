# Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
"""Minimal Chromium layout holding each anchor the ad blocking hooks need."""
from pathlib import Path
import sys
from unittest import mock

with mock.patch.object(sys, 'path', [str(Path(__file__).resolve().parents[1] / 'tools'), *sys.path]):
    import content_blocking_patch as patch


def seed(checkout):
    # Other fixtures may have written the file already: add the anchor once.
    for index, (path, anchor, _) in enumerate(patch.EDITS):
        # An anchor an earlier edit writes is not upstream's.
        if any(path == earlier and anchor in added for earlier, _, added in patch.EDITS[:index]):
            continue
        target = checkout / path
        target.parent.mkdir(parents=True, exist_ok=True)
        text = target.read_text() if target.exists() else '// upstream\n'
        if anchor not in text:
            target.write_text(text + anchor + '// upstream end\n', newline='\n')
    for path, old, _ in patch.REPLACEMENTS:
        target = checkout / path
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists():
            target.write_text('# upstream\n' + old, newline='\n')
