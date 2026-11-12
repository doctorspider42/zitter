#!/usr/bin/env python3
# Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
"""Chromium side of Zitter's built-in ad blocking (docs/ad-blocking.md).

Installs the vendored adblock-rust crates (third_party/rust) as
//third_party/rust/zitter_vendor, where Chromium's crates are visible to them,
registers the per-profile prefs, and adds the renderer hook that runs the
scriptlets in the page's own world at document start: a
RendererPreferences field the browser fills per tab, read by
chrome/renderer/zitter_page_scripts.cc (kept in chromium/renderer/).
Unchanged files keep their mtimes.
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
RULE_MISSES = 'zitter.ad_blocking_rule_misses'
LISTS = 'zitter.ad_blocking_lists'
CHROME_PREF_EDITS = [
    ('chrome/browser/prefs/browser_prefs.cc',
     '  AccessibilityUIMessageHandler::RegisterProfilePrefs(registry);\n',
     '  // Zitter: built-in ad blocking, its per-site switch and own rules.\n'
     f'  registry->RegisterBooleanPref("{ENABLED}", true);\n'
     f'  registry->RegisterListPref("{ALLOWED_SITES}");\n'
     f'  registry->RegisterListPref("{RULES}");\n'),
    # Its own edit, so checkouts patched before it gain only this line.
    ('chrome/browser/prefs/browser_prefs.cc',
     f'  registry->RegisterListPref("{RULES}");\n',
     f'  registry->RegisterDictionaryPref("{RULE_MISSES}");\n'),
    ('chrome/browser/prefs/browser_prefs.cc',
     f'  registry->RegisterDictionaryPref("{RULE_MISSES}");\n',
     f'  registry->RegisterDictionaryPref("{LISTS}");\n'),
]
CONTENT_EDITS = [
    # Scriptlets: the browser's per-tab list of "host\nscript" entries...
    ('third_party/blink/public/common/renderer_preferences/renderer_preferences.h',
     '  std::string accept_languages;\n',
     '  // Zitter: ad blocking scripts for the page\'s own world, "host\\nscript".\n'
     '  std::vector<std::string> zitter_page_scripts;\n'),
    ('third_party/blink/public/mojom/renderer_preferences.mojom',
     '  string accept_languages;\n',
     '\n  // Zitter: ad blocking scripts for the page\'s own world, "host\\nscript".\n'
     '  array<string> zitter_page_scripts;\n'),
    ('third_party/blink/public/common/renderer_preferences/renderer_preferences_mojom_traits.h',
     '  static const std::string& accept_languages(\n'
     '      const ::blink::RendererPreferences& data) {\n'
     '    return data.accept_languages;\n  }\n',
     '\n  static const std::vector<std::string>& zitter_page_scripts(\n'
     '      const ::blink::RendererPreferences& data) {\n'
     '    return data.zitter_page_scripts;\n  }\n'),
    ('third_party/blink/common/renderer_preferences/renderer_preferences_mojom_traits.cc',
     '  if (!data.ReadAcceptLanguages(&out->accept_languages))\n    return false;\n',
     '  if (!data.ReadZitterPageScripts(&out->zitter_page_scripts))\n    return false;\n'),
]
CHROME_RENDERER_EDITS = [
    # ...run by the renderer at document start, before any page script.
    ('chrome/renderer/chrome_content_renderer_client.cc',
     '#include "chrome/renderer/chrome_content_renderer_client.h"\n',
     '// Zitter: ad blocking scriptlets in the page\'s own world.\n'
     '#include "chrome/renderer/zitter_page_scripts.h"\n'),
    ('chrome/renderer/chrome_content_renderer_client.cc',
     'void ChromeContentRendererClient::RunScriptsAtDocumentStart(\n'
     '    content::RenderFrame* render_frame) {\n',
     '  // Zitter: ad blocking scriptlets (docs/ad-blocking.md).\n'
     '  zitter::RunPageScripts(render_frame);\n'),
    ('chrome/renderer/BUILD.gn',
     '    "chrome_content_renderer_client.h",\n',
     '    "zitter_page_scripts.cc",\n    "zitter_page_scripts.h",\n'),
]
EDITS = [*CHROME_PREF_EDITS, *CONTENT_EDITS, *CHROME_RENDERER_EDITS]
# Zitter's own renderer sources: chromium/renderer/<name> -> chrome/renderer/<name>.
OWNED = ['zitter_page_scripts.h', 'zitter_page_scripts.cc']
OWNED_MARKER = b'Copyright 2026 Zitter contributors'

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


def prepare(checkout, *, include_chrome=True, previous_plan=None):
    """Plan every write (path -> (old bytes or None, new bytes or None)) without touching the checkout."""
    root = Path(checkout).resolve()
    plan = dict(previous_plan or {})
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
    for name in OWNED if include_chrome else ():
        target = _inside(root, root / 'chrome/renderer' / name)
        old = target.read_bytes() if target.exists() else None
        if old is not None and OWNED_MARKER not in old:
            raise ValueError(f'Refusing to replace an unowned file: {target}')
        plan[target] = (old, (ROOT / 'chromium/renderer' / name).read_bytes())
    edits = EDITS if include_chrome else CONTENT_EDITS
    for path, anchor, addition in edits:
        target = _inside(root, root / path)
        # Several edits of one file build on each other.
        old, current = plan[target] if target in plan else (target.read_bytes(),) * 2
        plan[target] = (old, _insert(current.decode('utf-8'), anchor, addition, path).encode('utf-8'))
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
