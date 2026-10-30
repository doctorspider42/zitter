// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// Platform text services outside macOS: gfx::RenderText (HarfBuzz + Skia) in
// place of CoreText. Same contracts as editor_text_layout_mac.inc and the
// CoreText half of sidebar_metal.mm: UTF-16 offsets, logical points, one shaped
// line shared by hit testing, caret/selection geometry and drawing.
#include <algorithm>
#include <climits>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "base/i18n/break_iterator.h"
#include "base/strings/utf_string_conversions.h"
#include "cc/paint/paint_flags.h"
#include "chromium/prototype/editor_text_layout.h"
#include "chromium/prototype/sidebar_metal.h"
#include "chromium/prototype/sidebar_scene.rs.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/range/range.h"
#include "ui/gfx/range/range_f.h"
#include "ui/gfx/render_text.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/gfx/text_elider.h"

namespace zitter::prototype {
namespace editor_text_detail {
// The platform UI font (Segoe UI on Windows) at a logical pixel size and CSS
// weight, matching the macOS system-font adapter.
gfx::FontList Font(float size, int weight) {
  const gfx::FontList base;
  const int pixels = std::isfinite(size) && size > 0 ? std::max(1, int(std::lround(size))) : 13;
  const int css = std::clamp((weight > 0 ? weight : 400) / 100 * 100, 100, 900);
  return base.DeriveWithSizeDelta(pixels - base.GetFontSize()).DeriveWithWeight(static_cast<gfx::Font::Weight>(css));
}
float Finite(float x) { return std::isfinite(x) ? x : 0; }
// Edges of one grapheme: leading edge at its start offset, trailing edge at
// its end offset (reversed for RTL glyphs).
struct Cluster {
  size_t start, end;
  float leading, trailing;
};
}  // namespace editor_text_detail

struct EditorTextLayout::Impl {
  std::u16string text;
  std::unique_ptr<gfx::RenderText> line;
  float width = 0, ascent = 0, descent = 0, origin_bias = 0;
  std::vector<size_t> graphemes;
  std::vector<EditorTextRange> words;
  std::vector<EditorCaretStop> carets;
  std::vector<editor_text_detail::Cluster> clusters;
};

EditorTextLayout::EditorTextLayout(std::u16string_view text, float font_size,
                                   bool medium, int writing_direction)
    : impl_(std::make_unique<Impl>()) {
  auto& g = *impl_;
  g.text = text;
  const gfx::FontList font = editor_text_detail::Font(font_size, medium ? 500 : 400);
  g.line = gfx::RenderText::CreateRenderText();
  g.line->SetFontList(font);
  g.line->SetCursorEnabled(false);
  // Editors paint on transparent layers, where LCD text fringes; grayscale
  // antialiasing like CoreText.
  g.line->set_subpixel_rendering_suppressed(true);
  g.line->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  g.line->SetDirectionalityMode(writing_direction == 1   ? gfx::DIRECTIONALITY_FORCE_LTR
                                : writing_direction == 2 ? gfx::DIRECTIONALITY_FORCE_RTL
                                                         : gfx::DIRECTIONALITY_FROM_TEXT);
  g.line->SetText(g.text);
  g.width = std::max(0.f, editor_text_detail::Finite(g.line->GetStringSizeF().width()));
  // Wide enough that RenderText never scrolls the line inside its display rect.
  g.line->SetDisplayRect(gfx::Rect(0, 0, int(std::ceil(g.width)) + 2, font.GetHeight()));
  g.ascent = font.GetBaseline();
  g.descent = font.GetHeight() - font.GetBaseline();

  g.graphemes.push_back(0);
  base::i18n::BreakIterator characters(g.text, base::i18n::BreakIterator::BREAK_CHARACTER);
  if (characters.Init()) {
    while (characters.Advance())
      if (characters.pos() > g.graphemes.back() && characters.pos() <= g.text.size())
        g.graphemes.push_back(characters.pos());
  }
  if (g.graphemes.back() != g.text.size())
    g.graphemes.push_back(g.text.size());

  for (size_t i = 0; i + 1 < g.graphemes.size(); ++i) {
    const gfx::RangeF span = g.line->GetCursorSpan(gfx::Range(g.graphemes[i], g.graphemes[i + 1]));
    g.clusters.push_back({g.graphemes[i], g.graphemes[i + 1], editor_text_detail::Finite(span.start()),
                          editor_text_detail::Finite(span.end())});
  }
  // Normalize the logical advance interval to a nonnegative origin, as the
  // CoreText adapter does for forced RTL trailing spaces.
  float left = 0, right = 0;
  for (const auto& c : g.clusters) {
    left = std::min({left, c.leading, c.trailing});
    right = std::max({right, c.leading, c.trailing});
  }
  g.origin_bias = -left;
  g.width = std::max(g.width, right - left);
  for (auto& c : g.clusters) {
    c.leading += g.origin_bias;
    c.trailing += g.origin_bias;
  }
  // Graphemes tile the line, as CoreText's do. RenderText can leave part of a
  // ZWJ emoji's advance outside every cursor span; give each gap to the
  // grapheme on its right, the glyph it belongs to.
  std::vector<editor_text_detail::Cluster*> visual;
  for (auto& c : g.clusters)
    visual.push_back(&c);
  std::stable_sort(visual.begin(), visual.end(), [](const auto* a, const auto* b) {
    return std::min(a->leading, a->trailing) < std::min(b->leading, b->trailing);
  });
  float edge = 0;
  for (auto* c : visual) {
    float& left_edge = c->leading <= c->trailing ? c->leading : c->trailing;
    if (left_edge > edge)
      left_edge = edge;
    edge = std::max({edge, c->leading, c->trailing});
  }
  // A boundary's primary caret is the leading edge of the grapheme after it;
  // its secondary caret is the trailing edge of the grapheme before it, when the
  // two differ at a direction change.
  for (size_t i = 0; i < g.graphemes.size(); ++i) {
    const auto* after = i < g.clusters.size() ? &g.clusters[i] : nullptr;
    const auto* before = i > 0 ? &g.clusters[i - 1] : nullptr;
    const float primary = after ? after->leading : before ? before->trailing : 0;
    g.carets.push_back({g.graphemes[i], primary, false});
    if (after && before && std::abs(before->trailing - primary) > 0.01)
      g.carets.push_back({g.graphemes[i], before->trailing, true});
  }
  std::stable_sort(g.carets.begin(), g.carets.end(), [](const auto& a, const auto& b) { return a.x < b.x; });

  base::i18n::BreakIterator words(g.text, base::i18n::BreakIterator::BREAK_WORD);
  if (words.Init()) {
    while (words.Advance()) {
      if (!words.IsWord())
        continue;
      size_t start = words.prev(), end = words.pos();
      // Expand to the same grapheme boundaries used for caret movement.
      auto lo = std::upper_bound(g.graphemes.begin(), g.graphemes.end(), start);
      if (lo != g.graphemes.begin())
        start = *--lo;
      auto hi = std::lower_bound(g.graphemes.begin(), g.graphemes.end(), end);
      if (hi == g.graphemes.end())
        continue;
      end = *hi;
      if (!g.words.empty() && start < g.words.back().end)
        g.words.back().end = std::max(end, g.words.back().end);
      else
        g.words.push_back({start, end});
    }
  }
}
EditorTextLayout::~EditorTextLayout() = default;
bool EditorTextLayout::valid() const { return impl_->line != nullptr; }
size_t EditorTextLayout::length() const { return impl_->text.size(); }
float EditorTextLayout::width() const { return impl_->width; }
float EditorTextLayout::ascent() const { return impl_->ascent; }
float EditorTextLayout::descent() const { return impl_->descent; }
const std::vector<size_t>& EditorTextLayout::graphemes() const { return impl_->graphemes; }
const std::vector<EditorTextRange>& EditorTextLayout::words() const { return impl_->words; }
const std::vector<EditorCaretStop>& EditorTextLayout::carets() const { return impl_->carets; }
EditorCaretStop EditorTextLayout::HitTest(float x) const {
  if (impl_->carets.empty())
    return {};
  if (!std::isfinite(x))
    x = 0;
  const auto& stops = impl_->carets;
  const auto upper = std::lower_bound(stops.begin(), stops.end(), x, [](const auto& stop, float at) { return stop.x < at; });
  if (upper == stops.begin())
    return *upper;
  const auto lower = std::prev(upper);
  if (upper == stops.end() || x - lower->x <= upper->x - x) {
    // Keep the existing stable affinity choice when several carets share x.
    return *std::lower_bound(stops.begin(), stops.end(), lower->x, [](const auto& stop, float at) { return stop.x < at; });
  }
  return *upper;
}
EditorCaretStop EditorTextLayout::Caret(size_t index, bool secondary) const {
  index = std::min(index, impl_->text.size());
  // Offsets inside a grapheme resolve to the boundary before it.
  const auto& stops = impl_->graphemes;
  const auto at = std::upper_bound(stops.begin(), stops.end(), index);
  const size_t boundary = at == stops.begin() ? 0 : *std::prev(at);
  const EditorCaretStop* primary = nullptr;
  for (const auto& caret : impl_->carets) {
    if (caret.index != boundary)
      continue;
    if (caret.secondary == secondary)
      return {index, caret.x, secondary};
    if (!caret.secondary)
      primary = &caret;
  }
  return {index, primary ? primary->x : 0, secondary};
}
std::vector<EditorTextSpan> EditorTextLayout::Spans(size_t start, size_t end) const {
  std::vector<EditorTextSpan> out;
  if (start > end)
    std::swap(start, end);
  start = std::min(start, length());
  end = std::min(end, length());
  if (start == end)
    return out;
  for (const auto& c : impl_->clusters) {
    if (c.end <= start || c.start >= end)
      continue;
    const float x = std::min(c.leading, c.trailing), w = std::abs(c.trailing - c.leading);
    if (w > 0.001)
      out.push_back({x, w});
  }
  std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.x < b.x; });
  std::vector<EditorTextSpan> merged;
  for (const auto& span : out) {
    if (!merged.empty() && span.x <= merged.back().x + merged.back().width + 0.01f)
      merged.back().width = std::max(merged.back().x + merged.back().width, span.x + span.width) - merged.back().x;
    else
      merged.push_back(span);
  }
  return merged;
}
void EditorTextLayout::Draw(gfx::Canvas* canvas, float x, float baseline, uint32_t argb) const {
  if (!canvas || !impl_->line || !std::isfinite(x) || !std::isfinite(baseline))
    return;
  gfx::ScopedCanvas save(canvas);
  // RenderText draws its fixed baseline from the top of the display rect.
  canvas->Translate(gfx::Vector2d(std::lround(x + impl_->origin_bias), std::lround(baseline - impl_->line->GetBaseline())));
  impl_->line->SetColor(argb);
  impl_->line->Draw(canvas);
}

void PaintEditorSceneFallback(gfx::Canvas* canvas, base::span<const sidebar::Primitive> scene) {
  for (const auto& op : scene) {
    gfx::ScopedCanvas save(canvas);
    canvas->ClipRect(gfx::RectF(op.clip.x, op.clip.y, op.clip.width, op.clip.height));
    if (op.editor) {
      if (op.width <= 0 || op.height <= 0)
        continue;
      const std::u16string text(op.utf16.begin(), op.utf16.end());
      canvas->ClipRect(gfx::RectF(int(op.x), int(op.y), std::min(4096.f, op.width), std::min(512.f, op.height)));
      EditorTextLayout line(text, op.font_size, op.bold, op.text_direction);
      line.Draw(canvas, int(op.x) + op.text_x, int(op.y) + op.baseline, op.color);
    } else {
      cc::PaintFlags flags;
      flags.setColor(op.color);
      flags.setAntiAlias(true);
      if (op.stroke > 0) {
        flags.setStyle(cc::PaintFlags::kStroke_Style);
        flags.setStrokeWidth(op.stroke);
      }
      canvas->DrawRoundRect(gfx::RectF(op.x, op.y, op.width, op.height), op.radius, flags);
    }
  }
}
std::vector<std::string> ShapeSidebarParagraph(std::string_view text, float font_size, int width, bool bold) {
  return ShapeSidebarParagraphWeight(text, font_size, width, bold ? 500 : 400);
}
std::vector<std::string> ShapeSidebarParagraphWeight(std::string_view text, float font_size, int width, int weight) {
  std::vector<std::string> lines;
  if (text.empty() || width <= 0 || !std::isfinite(font_size) || font_size <= 0)
    return lines;
  std::vector<std::u16string> wrapped;
  gfx::ElideRectangleText(base::UTF8ToUTF16(text), editor_text_detail::Font(font_size, weight), width, INT_MAX,
                          gfx::WRAP_LONG_WORDS, &wrapped);
  for (const auto& line : wrapped)
    lines.push_back(base::UTF16ToUTF8(line));
  return lines;
}
}  // namespace zitter::prototype
