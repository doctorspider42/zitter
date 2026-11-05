#!/usr/bin/env python3
# Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
"""Chromium side of Zitter's built-in ad blocking (docs/ad-blocking.md).

Installs the vendored adblock-rust crates (third_party/rust) as
//third_party/rust/zitter_vendor, where Chromium's crates are visible to them,
and registers the per-profile prefs. Unchanged files keep their mtimes.
"""
from pathlib import Path
import os
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'third_party/rust'
VENDOR = 'third_party/rust/zitter_vendor'
MARKER = '.zitter-owned'
ENABLED = 'zitter.ad_blocking'
ALLOWED_SITES = 'zitter.ad_blocking_allowed_sites'
RULES = 'zitter.ad_blocking_rules'
EDITS = [
    ('chrome/browser/prefs/browser_prefs.cc',
     '  AccessibilityUIMessageHandler::RegisterProfilePrefs(registry);\n',
     '  // Zitter: built-in ad blocking, its per-site switch and own rules.\n'
     f'  registry->RegisterBooleanPref("{ENABLED}", true);\n'
     f'  registry->RegisterListPref("{ALLOWED_SITES}");\n'
     f'  registry->RegisterListPref("{RULES}");\n'),
]
# (path, old, new): Chromium marks aho-corasick "safe" (unsafe code forbidden),
# yet its sources use `unsafe impl`, so nothing that needs it builds. regex
# (and so adblock-rust) needs it through regex-automata.
REPLACEMENTS = [
    ('third_party/rust/aho_corasick/v1/BUILD.gn', '  allow_unsafe = false\n',
     '  # Zitter: its sources need unsafe code (tools/content_blocking_patch.py).\n  allow_unsafe = true\n'),
]


def _insert(text, anchor, addition, path):
    if text.count(anchor + addition) == 1 and text.count(addition) == 1:
        return text
    if text.count(anchor) != 1 or addition in text:
        raise ValueError(f'Unexpected upstream layout in {path}; review the ad blocking hook')
    return text.replace(anchor, anchor + addition, 1)


def _replace(text, old, new, path):
    if text.count(new) == 1 and old not in text:
        return text
    if text.count(old) != 1:
        raise ValueError(f'Unexpected upstream layout in {path}; review the ad blocking hook')
    return text.replace(old, new)


def _inside(root, target):
    if target.is_symlink() or not target.resolve().is_relative_to(root):
        raise ValueError('Ad blocking path escapes the checkout')
    return target


def prepare(checkout):
    """Plan every write (path -> (old bytes or None, new bytes or None)) without touching the checkout."""
    root = Path(checkout).resolve()
    plan = {}
    vendor = _inside(root, root / VENDOR)
    if vendor.exists() and not (vendor / MARKER).exists():
        raise ValueError(f'Refusing to replace an unowned directory: {vendor}')
    wanted = {path.relative_to(SOURCE).as_posix(): path.read_bytes() for path in SOURCE.rglob('*') if path.is_file()}
    wanted[MARKER] = b'Installed by tools/content_blocking_patch.py from Zitter\'s third_party/rust.\n'
    for name, data in wanted.items():
        target = _inside(root, vendor / name)
        plan[target] = (target.read_bytes() if target.exists() else None, data)
    if vendor.exists():
        for path in vendor.rglob('*'):
            if path.is_file() and path.relative_to(vendor).as_posix() not in wanted:
                plan[path] = (path.read_bytes(), None)
    for path, anchor, addition in EDITS:
        target = _inside(root, root / path)
        old = target.read_bytes()
        plan[target] = (old, _insert(old.decode('utf-8'), anchor, addition, path).encode('utf-8'))
    for path, before, after in REPLACEMENTS:
        target = _inside(root, root / path)
        old = target.read_bytes()
        plan[target] = (old, _replace(old.decode('utf-8'), before, after, path).encode('utf-8'))
    return plan


def _write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent, prefix='.zitter-adblock-', delete=False) as output:
        temporary = Path(output.name)
        output.write(data)
    try:
        os.chmod(temporary, path.stat().st_mode if path.exists() else 0o644)
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def apply(plan):
    """Apply idempotently; roll back on error."""
    for path, (old, _) in plan.items():
        if (path.read_bytes() if path.exists() else None) != old:
            raise ValueError('Ad blocking sources changed during staging')
    done = []
    try:
        for path, (old, new) in plan.items():
            if old == new:
                continue
            done.append(path)
            if new is None:
                path.unlink()
            else:
                _write(path, new)
    except Exception:
        for path in reversed(done):
            old, _ = plan[path]
            if old is None:
                path.unlink(missing_ok=True)
            else:
                _write(path, old)
        raise


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit(f'usage: {sys.argv[0]} CHROMIUM_SRC')
    try:
        apply(prepare(sys.argv[1]))
    except (OSError, ValueError) as error:
        sys.exit(f'Ad blocking patch failed: {error}')
