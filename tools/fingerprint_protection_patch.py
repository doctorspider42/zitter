#!/usr/bin/env python3
# Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
"""Guarded Chromium hooks for Zitter's optional fingerprint protection.

The browser sends a per-profile session key in RendererPreferences; Blink adds
small, per-site noise where pages read back canvas, WebGL and audio output.
An empty key (the default) leaves every hook inert.
"""
from pathlib import Path
import os
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PREF = 'zitter.fingerprint_protection'
OWNED_MARKER = b'Copyright 2026 Zitter contributors'
BLINK = 'third_party/blink/renderer/'
# Zitter-owned Blink sources, kept in this repository: chromium/blink/<path>.
OWNED = ['platform/graphics/zitter_fingerprint_noise.h', 'platform/graphics/zitter_fingerprint_noise.cc',
         'core/frame/zitter_fingerprint_protection.h', 'core/frame/zitter_fingerprint_protection.cc']
INCLUDE = '// Zitter: optional fingerprint protection.\n#include "third_party/blink/renderer/core/frame/zitter_fingerprint_protection.h"\n'


def _own_include(header):
    return f'#include "third_party/blink/renderer/{header}"\n\n', INCLUDE + '\n'


# (path, anchor, addition): the addition goes right after the anchor.
EDITS = [
    (BLINK + 'platform/BUILD.gn', '    "graphics/unaccelerated_static_bitmap_image.h",\n',
     '    "graphics/zitter_fingerprint_noise.cc",\n    "graphics/zitter_fingerprint_noise.h",\n'),
    (BLINK + 'core/frame/build.gni', '  "local_dom_window.h",\n',
     '  "zitter_fingerprint_protection.cc",\n  "zitter_fingerprint_protection.h",\n'),
    ('third_party/blink/public/common/renderer_preferences/renderer_preferences.h',
     '  bool caret_browsing_enabled{false};\n',
     '  // Zitter: secret for per-site fingerprint noise; empty turns it off.\n'
     '  std::vector<uint8_t> zitter_fingerprint_key;\n'),
    ('third_party/blink/public/mojom/renderer_preferences.mojom',
     '  bool caret_browsing_enabled = false;\n',
     '\n  // Zitter: secret for per-site fingerprint noise; empty turns it off.\n'
     '  array<uint8> zitter_fingerprint_key;\n'),
    ('third_party/blink/public/common/renderer_preferences/renderer_preferences_mojom_traits.h',
     '  static const bool& caret_browsing_enabled(\n'
     '      const ::blink::RendererPreferences& data) {\n'
     '    return data.caret_browsing_enabled;\n  }\n',
     '\n  static const std::vector<uint8_t>& zitter_fingerprint_key(\n'
     '      const ::blink::RendererPreferences& data) {\n'
     '    return data.zitter_fingerprint_key;\n  }\n'),
    ('third_party/blink/common/renderer_preferences/renderer_preferences_mojom_traits.cc',
     '  out->caret_browsing_enabled = data.caret_browsing_enabled();\n',
     '  if (!data.ReadZitterFingerprintKey(&out->zitter_fingerprint_key)) {\n'
     '    return false;\n  }\n'),
    ('chrome/browser/prefs/browser_prefs.cc',
     '  registry->RegisterBooleanPref(prefs::kRendererAccessibilityEnabled, true);\n',
     f'  // Zitter: per-site noise against canvas, WebGL and audio fingerprinting.\n'
     f'  registry->RegisterBooleanPref("{PREF}", false);\n'),
    # Canvas: toDataURL, toBlob and OffscreenCanvas.convertToBlob encode a snapshot.
    (BLINK + 'core/html/canvas/html_canvas_element.cc', *_own_include('core/html/canvas/html_canvas_element.h')),
    (BLINK + 'core/html/canvas/html_canvas_element.cc',
     '  scoped_refptr<StaticBitmapImage> image_bitmap = Snapshot(source_buffer);\n',
     '  if (auto noise = ZitterFingerprintNoiseFor(GetExecutionContext())) {\n'
     '    image_bitmap = noise->ApplyToImage(std::move(image_bitmap));\n  }\n'),
    (BLINK + 'core/html/canvas/html_canvas_element.cc',
     '  scoped_refptr<StaticBitmapImage> image_bitmap = Snapshot(kBackBuffer);\n'
     '  if (image_bitmap) {\n    auto* options = ImageEncodeOptions::Create();\n',
     '    if (auto noise = ZitterFingerprintNoiseFor(GetExecutionContext())) {\n'
     '      image_bitmap = noise->ApplyToImage(std::move(image_bitmap));\n    }\n'),
    (BLINK + 'core/offscreencanvas/offscreen_canvas.cc', *_own_include('core/offscreencanvas/offscreen_canvas.h')),
    (BLINK + 'core/offscreencanvas/offscreen_canvas.cc',
     '  scoped_refptr<StaticBitmapImage> image_bitmap = context_->GetImage();\n',
     '  if (auto noise =\n'
     '          ZitterFingerprintNoiseFor(ExecutionContext::From(script_state))) {\n'
     '    image_bitmap = noise->ApplyToImage(std::move(image_bitmap));\n  }\n'),
    # Canvas: getImageData.
    (BLINK + 'modules/canvas/canvas2d/base_rendering_context_2d.cc',
     *_own_include('modules/canvas/canvas2d/base_rendering_context_2d.h')),
    (BLINK + 'modules/canvas/canvas2d/base_rendering_context_2d.cc',
     '      DCHECK(!bounds.intersect(SkIRect::MakeXYWH(sx, sy, sw, sh)));\n    }\n',
     '    if (auto noise = ZitterFingerprintNoiseFor(GetTopExecutionContext())) {\n'
     '      noise->ApplyToPixels(image_data_pixmap);\n    }\n'),
    # WebGL: readPixels into RGBA bytes.
    (BLINK + 'modules/webgl/webgl_rendering_context_base.cc',
     *_own_include('modules/webgl/webgl_rendering_context_base.h')),
    (BLINK + 'modules/webgl/webgl_rendering_context_base.cc',
     '    ContextGL()->ReadPixels(x, y, width, height, format, type, data);\n  }\n',
     '  if (format == GL_RGBA && type == GL_UNSIGNED_BYTE && width > 0 &&\n'
     '      height > 0 && !buffer) {\n'
     '    if (auto noise =\n'
     '            ZitterFingerprintNoiseFor(Host()->GetTopExecutionContext())) {\n'
     '      const size_t row_bytes = static_cast<size_t>(width) * 4;\n'
     '      const int rows = static_cast<int>(std::min<size_t>(\n'
     '          height, static_cast<size_t>(buffer_size.ValueOrDie()) / row_bytes));\n'
     '      noise->ApplyToPixels(SkPixmap(\n'
     '          SkImageInfo::Make(width, rows, kRGBA_8888_SkColorType,\n'
     '                            kUnpremul_SkAlphaType),\n'
     '          data, row_bytes));\n    }\n  }\n'),
    # Audio: the buffer OfflineAudioContext renders.
    (BLINK + 'modules/webaudio/offline_audio_context.cc',
     *_own_include('modules/webaudio/offline_audio_context.h')),
    (BLINK + 'modules/webaudio/offline_audio_context.cc',
     '  CHECK(rendered_buffer);\n',
     '  if (auto noise = ZitterFingerprintNoiseFor(GetExecutionContext())) {\n'
     '    for (unsigned i = 0; i < rendered_buffer->numberOfChannels(); ++i) {\n'
     '      noise->ApplyToAudio(rendered_buffer->getChannelData(i)->AsSpan());\n'
     '    }\n  }\n'),
]


def _insert(text, anchor, addition, path):
    if text.count(anchor + addition) == 1 and text.count(addition) == 1:
        return text
    if text.count(anchor) != 1 or addition in text:
        raise ValueError(f'Unexpected upstream layout in {path}; review the fingerprint protection hook')
    return text.replace(anchor, anchor + addition, 1)


def prepare(checkout):
    """Validate every edit before any write; preserve unrelated local edits."""
    root = Path(checkout).resolve()
    plan = {}
    for name in OWNED:
        target = root / BLINK / name
        if target.is_symlink() or not target.resolve().is_relative_to(root):
            raise ValueError('Fingerprint protection path escapes the checkout')
        old = target.read_bytes() if target.exists() else None
        if old is not None and OWNED_MARKER not in old:
            raise ValueError(f'Refusing to replace an unowned file: {target}')
        plan[target] = (old, (ROOT / 'chromium/blink' / name).read_bytes())
    texts = {}
    for path, anchor, addition in EDITS:
        target = root / path
        if target.is_symlink() or not target.resolve().is_relative_to(root):
            raise ValueError('Fingerprint protection path escapes the checkout')
        if target not in texts:
            texts[target] = target.read_text()
            plan[target] = (texts[target].encode(), None)
        texts[target] = _insert(texts[target], anchor, addition, path)
    for target, text in texts.items():
        plan[target] = (plan[target][0], text.encode())
    return plan


def _write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    # A compiler never sees a half-written source file.
    with tempfile.NamedTemporaryFile(dir=path.parent, prefix='.zitter-fingerprint-', delete=False) as output:
        temporary = Path(output.name)
        try:
            output.write(data)
            output.flush()
            os.chmod(temporary, path.stat().st_mode if path.exists() else 0o644)
            os.replace(temporary, path)
        finally:
            temporary.unlink(missing_ok=True)


def apply(plan):
    """Apply idempotently; unchanged files keep their mtimes. Roll back on error."""
    for path, (old, _) in plan.items():
        if (path.read_bytes() if path.exists() else None) != old:
            raise ValueError('Fingerprint protection sources changed during staging')
    written = []
    try:
        for path, (old, new) in plan.items():
            if old != new:
                written.append(path)
                _write(path, new)
    except Exception:
        for path in reversed(written):
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
        sys.exit(f'Fingerprint protection patch failed: {error}')
