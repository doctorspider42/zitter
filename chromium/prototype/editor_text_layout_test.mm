// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// Window-free SDK-only geometry/raster regression harness. No NSApplication.
#include "editor_text_layout_mac.inc"
#include <cstdio>
#include <cstdlib>
using zitter::prototype::EditorTextLayout;
namespace {
void Check(bool ok, const char *message) {
  if (!ok) {
    fprintf(stderr, "FAIL: %s\n", message);
    std::abort();
  }
}
bool Near(float a, float b) { return std::abs(a - b) < 0.05f; }
void Line(std::u16string_view text) {
  EditorTextLayout l(text, 14);
  Check(l.valid(), "line creation");
  Check(l.length() == text.size(), "UTF16 length");
  Check(std::isfinite(l.width()) && l.width() >= 0, "finite width");
  Check(l.ascent() > 0 && l.descent() >= 0, "metrics");
  Check(!l.graphemes().empty() && l.graphemes().front() == 0 &&
            l.graphemes().back() == text.size(),
        "boundary endpoints");
  for (auto c : l.carets()) {
    Check(std::isfinite(c.x), "finite caret");
    Check(
        std::binary_search(l.graphemes().begin(), l.graphemes().end(), c.index),
        "caret is grapheme stop");
    auto hit = l.HitTest(c.x);
    Check(Near(hit.x, c.x), "caret hit roundtrip incl bidi affinity");
    Check(Near(l.Caret(c.index, c.secondary).x, c.x),
          "caret affinity geometry");
  }
  size_t previous = 0;
  for (auto w : l.words()) {
    Check(w.start >= previous && w.start < w.end && w.end <= text.size(),
          "word ordering");
    Check(std::binary_search(l.graphemes().begin(), l.graphemes().end(),
                             w.start) &&
              std::binary_search(l.graphemes().begin(), l.graphemes().end(),
                                 w.end),
          "word grapheme endpoints");
    previous = w.end;
  }
  auto full = l.Spans(0, text.size());
  float sum = 0;
  for (auto span : full) {
    Check(span.width > 0 && std::isfinite(span.x), "finite selection");
    sum += span.width;
  }
  if (!text.empty())
    Check(Near(sum, l.width()), "full selection covers line");
  for (size_t a : l.graphemes())
    for (size_t b : l.graphemes()) {
      auto spans = l.Spans(a, b), reverse = l.Spans(b, a);
      Check(spans.size() == reverse.size(), "reversed selection");
      float right = -1e9;
      for (size_t i = 0; i < spans.size(); ++i) {
        Check(Near(spans[i].x, reverse[i].x) &&
                  Near(spans[i].width, reverse[i].width),
              "symmetric selection");
        Check(spans[i].x >= right && spans[i].width > 0,
              "ordered selection spans");
        right = spans[i].x + spans[i].width;
        Check(spans[i].x >= -0.05 && right <= l.width() + 0.05,
              "selection bounded by line");
      }
    }
}
void Raster() {
  constexpr size_t width = 256, height = 64;
  std::vector<uint8_t> pixels(width * height * 4);
  CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
  CGContextRef context = CGBitmapContextCreate(
      pixels.data(), width, height, 8, width * 4, space,
      static_cast<uint32_t>(kCGImageAlphaPremultipliedLast) |
          kCGBitmapByteOrder32Big);
  Check(context, "bitmap context");
  CGColorSpaceRelease(space);
  EditorTextLayout text(u"office 日本", 18);
  text.Draw(context, 8, 24, 0xffff0000);
  size_t ink = 0;
  for (size_t i = 0; i < pixels.size(); i += 4)
    if (pixels[i + 3]) {
      ++ink;
      Check(pixels[i] > 0 && pixels[i + 1] == 0 && pixels[i + 2] == 0,
            "foreground comes from context");
    }
  Check(ink > 100, "raster has text");
  std::fill(pixels.begin(), pixels.end(), 0);
  CGContextSaveGState(context);
  CGContextClipToRect(context, CGRectMake(0, 0, 12, height));
  text.Draw(context, -20, 24, 0xff00ff00);
  CGContextRestoreGState(context);
  ink = 0;
  for (size_t y = 0; y < height; ++y)
    for (size_t x = 0; x < width; ++x) {
      auto i = (y * width + x) * 4;
      if (pixels[i + 3]) {
        ++ink;
        Check(x < 12 && pixels[i] == 0 && pixels[i + 1] > 0 &&
                  pixels[i + 2] == 0,
              "scroll clipping and color");
      }
    }
  Check(ink > 0, "scroll retains ink");
  CGContextRelease(context);
}
} // namespace
int main() {
  @autoreleasepool {
    for (auto text : {u"", u"hello world", u"office affine ffi", u"a\u0301b",
                      u"👩‍👩‍👧‍👦🇵🇱🦀",
                      u"日本語 中文 ไทย", u"مرحبا بالعالم", u"אבג 123 דהו",
                      u"abc אבג xyz", u"אבג abc דהו", u"a  b   "})
      Line(text);
    EditorTextLayout cluster(u"a\u0301🦀", 14);
    Check(cluster.graphemes() == std::vector<size_t>({0, 2, 4}),
          "composed boundaries");
    EditorTextLayout rtl(u"אבג", 14);
    Check(rtl.Caret(0).x > rtl.Caret(3).x, "RTL direction");
    EditorTextLayout ltr(u"abc", 14);
    Check(ltr.Caret(0).x < ltr.Caret(3).x, "LTR direction");
    EditorTextLayout fallback(u"abc", std::numeric_limits<float>::quiet_NaN());
    Check(fallback.valid() && fallback.width() > 0, "invalid font fallback");
    std::u16string long_text(10000, u'x');
    EditorTextLayout long_line(long_text, 14);
    Check(long_line.length() == 10000 && long_line.width() > 4096,
          "no tile-limit truncation");
    Raster();
    printf("PASS: CoreText geometry, Unicode/bidi, selection, long line, "
           "clipping/raster; no windows\n");
  }
}
