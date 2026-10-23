#!/usr/bin/env python3
# Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
"""Guarded Chromium hook for sharing native text-edit state with custom editors.

No Textfield/RenderText instance is created. The native UI-thread kill buffer
remains the sole store, including for Chromium controls outside the prototype.
"""
from pathlib import Path
import os
import tempfile

HEADER_PATH = 'ui/views/controls/textfield/text_edit_kill_buffer.h'
HEADER = '''// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_VIEWS_CONTROLS_TEXTFIELD_TEXT_EDIT_KILL_BUFFER_H_
#define UI_VIEWS_CONTROLS_TEXTFIELD_TEXT_EDIT_KILL_BUFFER_H_

#include <string_view>
#include "ui/views/views_export.h"

namespace views {
// Shared native editing facility, UI thread only. The borrowed text is valid
// until the next write. These operations never create a textfield/model.
VIEWS_EXPORT std::u16string_view GetTextEditKillBuffer();
VIEWS_EXPORT void SetTextEditKillBuffer(std::u16string_view text);
}  // namespace views

#endif  // UI_VIEWS_CONTROLS_TEXTFIELD_TEXT_EDIT_KILL_BUFFER_H_
'''
SOURCE_PATH = 'ui/views/controls/textfield/textfield_model.cc'
BUILD_PATH = 'ui/views/BUILD.gn'
INCLUDE = '#include "ui/views/controls/textfield/text_edit_kill_buffer.h"\n'
SOURCE_ANCHOR = '/////////////////////////////////////////////////////////////////\n// TextfieldModel: public\n'
DEFINITIONS = '''// Zitter: expose only the shared UI-thread editing buffer, without a model.
std::u16string_view GetTextEditKillBuffer() {
  return *GetKillBuffer();
}

void SetTextEditKillBuffer(std::u16string_view text) {
  SetKillBuffer(text);
}

'''


def _insert(text, anchor, addition, before=False):
    replacement = addition + anchor if before else anchor + addition
    if text.count(anchor) == 1 and text.count(replacement) == 1 and text.count(addition) == 1:
        return text
    if text.count(anchor) != 1 or addition in text:
        raise ValueError('Unexpected native text-edit service layout; review the pinned fork hook')
    return text.replace(anchor, replacement, 1)


def prepare(checkout):
    """Validate the entire patch before any write; preserve unrelated edits."""
    root = Path(checkout).resolve()
    paths = [root / name for name in (HEADER_PATH, SOURCE_PATH, BUILD_PATH)]
    for path in paths:
        if path.is_symlink() or not path.resolve().is_relative_to(root):
            raise ValueError('Native editor service path escapes the checkout')
    header, source, build = paths
    old_header = header.read_bytes() if header.exists() else None
    if old_header is not None and old_header != HEADER.encode():
        raise ValueError('Native editing service header contains local changes')
    old_source, old_build = source.read_bytes(), build.read_bytes()
    markers = ("GetTextEditKillBuffer(", "SetTextEditKillBuffer(", "Zitter: expose only")
    if any(marker in old_source.decode() for marker in markers) and DEFINITIONS not in old_source.decode():
        raise ValueError("Native editing service implementation contains local changes")
    if "text_edit_kill_buffer" in old_source.decode() and INCLUDE not in old_source.decode():
        raise ValueError("Native editing service include contains local changes")
    if "text_edit_kill_buffer" in old_build.decode() and '    "controls/textfield/text_edit_kill_buffer.h",\n' not in old_build.decode():
        raise ValueError("Native editing service build entry contains local changes")
    patched = _insert(old_source.decode(), '#include "ui/views/controls/textfield/textfield_model.h"\n', INCLUDE)
    patched = _insert(patched, SOURCE_ANCHOR, DEFINITIONS, before=True)
    patched_build = _insert(old_build.decode(), '    "controls/textfield/textfield_model.h",\n',
                            '    "controls/textfield/text_edit_kill_buffer.h",\n')
    return {header: (old_header, HEADER.encode()), source: (old_source, patched.encode()),
            build: (old_build, patched_build.encode())}


def _write(path, data):
    if data is None:
        path.unlink()
        return
    # A compiler/observer never sees a half-written source file.
    with tempfile.NamedTemporaryFile(dir=path.parent, prefix='.zitter-editor-', delete=False) as output:
        temporary = Path(output.name)
        try:
            output.write(data)
            output.flush()
            if path.exists():
                os.chmod(temporary, path.stat().st_mode)
            else:
                os.chmod(temporary, 0o644)
            os.replace(temporary, path)
        finally:
            temporary.unlink(missing_ok=True)


def apply(plan):
    """Apply idempotently, refusing edits made after prepare; rollback on error."""
    for path, (old, _) in plan.items():
        current = path.read_bytes() if path.exists() else None
        if current != old:
            raise ValueError('Native editor service changed during staging')
    written = []
    try:
        for path, (old, new) in plan.items():
            if old != new:
                written.append(path)
                _write(path, new)
    except Exception:
        for path in reversed(written):
            old, new = plan[path]
            current = path.read_bytes() if path.exists() else None
            if current == new:
                _write(path, old)
            elif current != old:
                raise ValueError('Native editor service changed during rollback; local edits preserved')
        raise
