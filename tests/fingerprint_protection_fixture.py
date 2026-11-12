# Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
"""Minimal Chromium layout holding each anchor the fingerprint protection hooks need."""
from pathlib import Path
import sys
from unittest import mock

with mock.patch.object(sys, 'path', [str(Path(__file__).resolve().parents[1] / 'tools'), *sys.path]):
    import fingerprint_protection_patch as patch


def seed(checkout):
    anchors = {}
    additions = {}
    for path, anchor, addition in (*patch.EDITS, patch.PUBLIC_STATE_EDIT):
        previous = additions.setdefault(path, [])
        if not any(anchor in text for text in previous):
            anchors.setdefault(path, []).append(anchor)
        previous.append(addition)
    for path, parts in anchors.items():
        target = checkout / path
        if target.exists():
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text('// upstream\n' + '// upstream\n'.join(parts) + '// upstream end\n', newline='\n')
