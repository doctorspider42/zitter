// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// Included by sidebar_metal.mm and the window-free native layout test harness.
#include "editor_text_layout.h"
#import <AppKit/AppKit.h>
#import <CoreText/CoreText.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
namespace zitter::prototype {
namespace editor_text_detail {
NSFont *Font(float size, bool medium) {
  return [NSFont
      systemFontOfSize:std::isfinite(size) && size > 0 ? size : 13
                weight:medium ? NSFontWeightMedium : NSFontWeightRegular];
}
struct Run {
  size_t start, end;
  float x, width;
  bool rtl;
};
float Finite(CGFloat x) { return std::isfinite(x) ? static_cast<float>(x) : 0; }
} // namespace editor_text_detail
struct EditorTextLayout::Impl {
  std::u16string text;
  CTLineRef line = nullptr;
  float width = 0, ascent = 0, descent = 0, origin_bias = 0;
  std::vector<size_t> graphemes;
  std::vector<EditorTextRange> words;
  std::vector<EditorCaretStop> carets;
  std::vector<editor_text_detail::Run> runs;
  ~Impl() {
    if (line)
      CFRelease(line);
  }
};
EditorTextLayout::EditorTextLayout(std::u16string_view text, float font_size,
                                   bool medium, int writing_direction)
    : impl_(std::make_unique<Impl>()) {
  impl_->text = text;
  auto &g = *impl_;
  if (text.size() > static_cast<size_t>(std::numeric_limits<CFIndex>::max()))
    return;
  NSString *string = [[NSString alloc]
      initWithCharacters:reinterpret_cast<const unichar *>(text.data())
                  length:text.size()];
  NSMutableParagraphStyle* paragraph=[[NSMutableParagraphStyle alloc] init];
  paragraph.baseWritingDirection=writing_direction==1?NSWritingDirectionLeftToRight:writing_direction==2?NSWritingDirectionRightToLeft:NSWritingDirectionNatural;
  NSDictionary *attributes = @{NSParagraphStyleAttributeName:paragraph,
    NSFontAttributeName : editor_text_detail::Font(font_size, medium),
    (__bridge NSString *)kCTForegroundColorFromContextAttributeName : @YES
  };
  NSAttributedString *attributed =
      [[NSAttributedString alloc] initWithString:string attributes:attributes];
  g.line = CTLineCreateWithAttributedString(
      (__bridge CFAttributedStringRef)attributed);
  if (!g.line)
    return;
  CGFloat ascent = 0, descent = 0;
  g.width = std::max(0.f, editor_text_detail::Finite(CTLineGetTypographicBounds(
                              g.line, &ascent, &descent, nullptr)));
  g.ascent = std::max(0.f, editor_text_detail::Finite(ascent));
  g.descent = std::max(0.f, editor_text_detail::Finite(descent));
  // Empty CoreText lines have no typographic metrics; use the exact drawing
  // font.
  if (g.ascent + g.descent == 0) {
    CTFontRef font =
        (__bridge CTFontRef)editor_text_detail::Font(font_size, medium);
    g.ascent = editor_text_detail::Finite(CTFontGetAscent(font));
    g.descent = editor_text_detail::Finite(CTFontGetDescent(font));
  }
  CFStringRef chars = (__bridge CFStringRef)string;
  g.graphemes.push_back(0);
  for (CFIndex at = 0; at < static_cast<CFIndex>(text.size());) {
    const auto range = CFStringGetRangeOfComposedCharactersAtIndex(chars, at);
    const auto next = range.location + range.length;
    if (next <= at || next > static_cast<CFIndex>(text.size()))
      break;
    g.graphemes.push_back(next);
    at = next;
  }
  if (g.graphemes.back() != text.size())
    g.graphemes.push_back(text.size());
  for (size_t index : g.graphemes) {
    CGFloat secondary = 0;
    const auto primary =
        CTLineGetOffsetForStringIndex(g.line, index, &secondary);
    g.carets.push_back({index, editor_text_detail::Finite(primary), false});
    if (std::abs(primary - secondary) > 0.01)
      g.carets.push_back({index, editor_text_detail::Finite(secondary), true});
  }
  std::stable_sort(g.carets.begin(), g.carets.end(),
                   [](const auto &a, const auto &b) { return a.x < b.x; });
  CFArrayRef runs = CTLineGetGlyphRuns(g.line);
  for (CFIndex i = 0; i < CFArrayGetCount(runs); ++i) {
    CTRunRef run = static_cast<CTRunRef>(
        const_cast<void *>(CFArrayGetValueAtIndex(runs, i)));
    const auto range = CTRunGetStringRange(run);
    if (range.location < 0 || range.length <= 0)
      continue;
    const auto count = CTRunGetGlyphCount(run);
    if (count <= 0)
      continue;
    std::vector<CGPoint> positions(count);
    std::vector<CGSize> advances(count);
    CTRunGetPositions(run, CFRangeMake(0, 0), positions.data());
    CTRunGetAdvances(run, CFRangeMake(0, 0), advances.data());
    float left = std::numeric_limits<float>::max(),
          right = std::numeric_limits<float>::lowest();
    for (CFIndex j = 0; j < count; ++j) {
      const float a = editor_text_detail::Finite(positions[j].x),
                  b = editor_text_detail::Finite(positions[j].x +
                                                 advances[j].width);
      left = std::min({left, a, b});
      right = std::max({right, a, b});
    }
    const bool rtl = CTRunGetStatus(run) & kCTRunStatusRightToLeft;
    g.runs.push_back({static_cast<size_t>(range.location),
                      static_cast<size_t>(range.location + range.length), left,
                      std::max(0.f, right - left), rtl});
  }
  // Forced RTL can place trailing spaces left of CoreText's line origin.
  // Normalize the entire logical advance interval, not each individual caret:
  // geometry and drawing then share a nonnegative, viewport-relative origin.
  float left=0,right=0;
  for(const auto& run:g.runs){left=std::min(left,run.x);right=std::max(right,run.x+run.width);}
  for(const auto& caret:g.carets){left=std::min(left,caret.x);right=std::max(right,caret.x);}
  g.origin_bias=-left;g.width=std::max(g.width,right-left);
  for(auto& run:g.runs)run.x+=g.origin_bias;
  for(auto& caret:g.carets)caret.x+=g.origin_bias;
  CFStringTokenizerRef tokenizer = CFStringTokenizerCreate(
      kCFAllocatorDefault, chars, CFRangeMake(0, text.size()),
      kCFStringTokenizerUnitWord, nullptr);
  if (tokenizer) {
    while (CFStringTokenizerAdvanceToNextToken(tokenizer) !=
           kCFStringTokenizerTokenNone) {
      auto range = CFStringTokenizerGetCurrentTokenRange(tokenizer);
      if (range.location < 0 || range.length <= 0)
        continue;
      size_t start = range.location, end = range.location + range.length;
      // Word tokens may bisect a fallback/composed glyph. Expand to the same
      // composed-character boundaries used for interactive caret movement.
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
    CFRelease(tokenizer);
  }
}
EditorTextLayout::~EditorTextLayout() = default;
bool EditorTextLayout::valid() const { return impl_->line != nullptr; }
size_t EditorTextLayout::length() const { return impl_->text.size(); }
float EditorTextLayout::width() const { return impl_->width; }
float EditorTextLayout::ascent() const { return impl_->ascent; }
float EditorTextLayout::descent() const { return impl_->descent; }
const std::vector<size_t> &EditorTextLayout::graphemes() const {
  return impl_->graphemes;
}
const std::vector<EditorTextRange> &EditorTextLayout::words() const {
  return impl_->words;
}
const std::vector<EditorCaretStop> &EditorTextLayout::carets() const {
  return impl_->carets;
}
EditorCaretStop EditorTextLayout::HitTest(float x) const {
  if (impl_->carets.empty())
    return {};
  if (!std::isfinite(x))
    x = 0;
  const auto& stops=impl_->carets;
  const auto upper=std::lower_bound(stops.begin(),stops.end(),x,[](const auto& stop,float at){return stop.x<at;});
  if(upper==stops.begin())return *upper;
  const auto lower=std::prev(upper);
  if(upper==stops.end()||x-lower->x<=upper->x-x){
    // Keep the existing stable affinity choice when several carets share x.
    return *std::lower_bound(stops.begin(),stops.end(),lower->x,[](const auto& stop,float at){return stop.x<at;});
  }
  return *upper;
}
EditorCaretStop EditorTextLayout::Caret(size_t index, bool secondary) const {
  index = std::min(index, impl_->text.size());
  if (!impl_->line)
    return {index, 0, false};
  CGFloat other = 0;
  CGFloat primary = CTLineGetOffsetForStringIndex(impl_->line, index, &other);
  return {index, editor_text_detail::Finite(secondary ? other : primary)+impl_->origin_bias,
          secondary};
}
std::vector<EditorTextSpan> EditorTextLayout::Spans(size_t start,
                                                    size_t end) const {
  std::vector<EditorTextSpan> out;
  if (start > end)
    std::swap(start, end);
  start = std::min(start, length());
  end = std::min(end, length());
  if (start == end || !impl_->line)
    return out;
  for (const auto &run : impl_->runs) {
    const auto a = std::max(start, run.start), b = std::min(end, run.end);
    if (a >= b)
      continue;
    const auto position = [&](size_t at) {
      if (at == run.start)
        return run.x + (run.rtl ? run.width : 0);
      if (at == run.end)
        return run.x + (run.rtl ? 0 : run.width);
      const auto p = Caret(at);
      return std::clamp(p.x, run.x, run.x + run.width);
    };
    const float x = position(a), y = position(b);
    if (std::abs(y - x) > 0.001)
      out.push_back({std::min(x, y), std::abs(y - x)});
  }
  std::sort(out.begin(), out.end(),
            [](const auto &a, const auto &b) { return a.x < b.x; });
  std::vector<EditorTextSpan> merged;
  for (const auto &span : out) {
    if (!merged.empty() &&
        span.x <= merged.back().x + merged.back().width + 0.01f)
      merged.back().width =
          std::max(merged.back().x + merged.back().width, span.x + span.width) -
          merged.back().x;
    else
      merged.push_back(span);
  }
  return merged;
}
void EditorTextLayout::Draw(CGContextRef context, float x, float baseline,
                            uint32_t argb) const {
  if (!context || !impl_->line || !std::isfinite(x) || !std::isfinite(baseline))
    return;
  CGContextSaveGState(context);
  CGContextSetRGBFillColor(context, ((argb >> 16) & 255) / 255.,
                           ((argb >> 8) & 255) / 255., (argb & 255) / 255.,
                           (argb >> 24) / 255.);
  CGContextSetTextPosition(context, x+impl_->origin_bias, baseline);
  CTLineDraw(impl_->line, context);
  CGContextRestoreGState(context);
}
} // namespace zitter::prototype
