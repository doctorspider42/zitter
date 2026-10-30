// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// Window-free geometry/raster checks for the gfx::RenderText EditorTextLayout.
// The same contract as the CoreText harness, editor_text_layout_test.mm.
#include "chromium/prototype/editor_text_layout.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/launcher/unit_test_launcher.h"
#include "base/test/test_suite.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/font_util.h"
#include "ui/gfx/scoped_canvas.h"

namespace zitter::prototype {
namespace {

bool Near(float a, float b) { return std::abs(a - b) < 0.05f; }
std::string DumpSpans(const EditorTextLayout& l) {
  std::string out;
  for (size_t i = 0; i + 1 < l.graphemes().size(); ++i)
    for (auto s : l.Spans(l.graphemes()[i], l.graphemes()[i + 1]))
      out += ' ' + std::to_string(s.x) + '+' + std::to_string(s.width);
  return out;
}

void CheckLine(std::u16string_view text, int direction, float font_size) {
  SCOPED_TRACE(testing::Message() << "direction=" << direction << " size=" << font_size
                                  << " text=" << base::UTF16ToUTF8(text));
  EditorTextLayout l(text, font_size, false, direction);
  ASSERT_TRUE(l.valid());
  EXPECT_EQ(l.length(), text.size());
  EXPECT_TRUE(std::isfinite(l.width()) && l.width() >= 0);
  EXPECT_GT(l.ascent(), 0);
  EXPECT_GE(l.descent(), 0);
  ASSERT_FALSE(l.graphemes().empty());
  EXPECT_EQ(l.graphemes().front(), 0u);
  EXPECT_EQ(l.graphemes().back(), text.size());
  for (auto c : l.carets()) {
    EXPECT_TRUE(std::isfinite(c.x));
    EXPECT_TRUE(std::binary_search(l.graphemes().begin(), l.graphemes().end(), c.index))
        << "caret is grapheme stop";
    EXPECT_TRUE(Near(l.HitTest(c.x).x, c.x)) << "caret hit roundtrip incl bidi affinity";
    EXPECT_TRUE(Near(l.Caret(c.index, c.secondary).x, c.x)) << "caret affinity geometry";
  }
  for (int sample = 0; sample <= 100; ++sample) {
    const float x = -5 + (l.width() + 10) * sample / 100.f;
    const auto expected = std::min_element(l.carets().begin(), l.carets().end(), [&](const auto& a, const auto& b) {
      return std::abs(a.x - x) < std::abs(b.x - x);
    });
    const auto hit = l.HitTest(x);
    EXPECT_TRUE(hit.index == expected->index && hit.secondary == expected->secondary)
        << "stable nearest-caret affinity at " << x;
  }
  size_t previous = 0;
  for (auto w : l.words()) {
    EXPECT_TRUE(w.start >= previous && w.start < w.end && w.end <= text.size()) << "word ordering";
    EXPECT_TRUE(std::binary_search(l.graphemes().begin(), l.graphemes().end(), w.start) &&
                std::binary_search(l.graphemes().begin(), l.graphemes().end(), w.end))
        << "word grapheme endpoints";
    previous = w.end;
  }
  float sum = 0;
  for (auto span : l.Spans(0, text.size())) {
    EXPECT_TRUE(span.width > 0 && std::isfinite(span.x)) << "finite selection";
    sum += span.width;
  }
  if (!text.empty())
    EXPECT_TRUE(Near(sum, l.width())) << "full selection covers line: " << sum << " vs " << l.width() << DumpSpans(l);
  for (size_t a : l.graphemes()) {
    for (size_t b : l.graphemes()) {
      const auto spans = l.Spans(a, b), reverse = l.Spans(b, a);
      ASSERT_EQ(spans.size(), reverse.size()) << "reversed selection";
      float right = -1e9;
      for (size_t i = 0; i < spans.size(); ++i) {
        EXPECT_TRUE(Near(spans[i].x, reverse[i].x) && Near(spans[i].width, reverse[i].width)) << "symmetric selection";
        EXPECT_TRUE(spans[i].x >= right && spans[i].width > 0) << "ordered selection spans";
        right = spans[i].x + spans[i].width;
        EXPECT_TRUE(spans[i].x >= -0.05 && right <= l.width() + 0.05) << "selection bounded by line";
      }
    }
  }
}

TEST(EditorTextLayoutTest, LineGeometry) {
  for (auto text : {u"", u"hello world", u"office affine ffi", u"áb", u"👩‍👩‍👧‍👦🇵🇱🦀", u"日本語 中文 ไทย",
                    u"مرحبا بالعالم", u"אבג 123 דהו", u"abc אבג xyz", u"אבג abc דהו", u"a  b   ", u"אבג   ",
                    u"   אבג", u"123 אבג   ", u"אבג abc   "}) {
    for (float font_size : {13.f, 14.f, 16.f, 17.f})
      for (int direction = 0; direction < 3; ++direction)
        CheckLine(text, direction, font_size);
  }
}

TEST(EditorTextLayoutTest, ClustersAndDirection) {
  EditorTextLayout cluster(u"á🦀", 14);
  EXPECT_EQ(cluster.graphemes(), std::vector<size_t>({0, 2, 4}));
  EditorTextLayout rtl(u"אבג", 14);
  EXPECT_GT(rtl.Caret(0).x, rtl.Caret(3).x);
  EditorTextLayout ltr(u"abc", 14);
  EXPECT_LT(ltr.Caret(0).x, ltr.Caret(3).x);
  EditorTextLayout natural(u"abc אבג", 14), forced(u"abc אבג", 14, false, 2);
  EXPECT_TRUE(Near(natural.width(), forced.width())) << "forced direction keeps line extent";
  EXPECT_GT(std::abs(natural.Caret(0).x - forced.Caret(0).x), 10) << "forced RTL changes visual geometry";
}

TEST(EditorTextLayoutTest, FallbackAndLongLine) {
  EditorTextLayout fallback(u"abc", std::numeric_limits<float>::quiet_NaN());
  EXPECT_TRUE(fallback.valid() && fallback.width() > 0);
  const std::u16string long_text(10000, u'x');
  EditorTextLayout long_line(long_text, 14);
  EXPECT_EQ(long_line.length(), 10000u);
  EXPECT_GT(long_line.width(), 4096) << "no tile-limit truncation";
}

constexpr int kWidth = 256, kHeight = 64;

// Calls |visit(x, y, color)| for every pixel with ink.
template <typename Visit>
size_t Ink(gfx::Canvas& canvas, Visit visit) {
  const SkBitmap bitmap = canvas.GetBitmap();
  size_t ink = 0;
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      const SkColor color = bitmap.getColor(x, y);
      if (SkColorGetA(color)) {
        ++ink;
        visit(x, y, color);
      }
    }
  }
  return ink;
}

TEST(EditorTextLayoutTest, RasterColorAndClip) {
  EditorTextLayout text(u"office 日本", 18);
  {
    gfx::Canvas canvas(gfx::Size(kWidth, kHeight), 1.0f, false);
    text.Draw(&canvas, 8, 24, 0xffff0000);
    const size_t ink = Ink(canvas, [](int x, int y, SkColor c) {
      EXPECT_TRUE(SkColorGetR(c) > 0 && SkColorGetG(c) == 0 && SkColorGetB(c) == 0) << "foreground comes from argb: " << x << "," << y << " " << std::hex << c;
    });
    EXPECT_GT(ink, 100u) << "raster has text";
  }
  {
    gfx::Canvas canvas(gfx::Size(kWidth, kHeight), 1.0f, false);
    {
      gfx::ScopedCanvas save(&canvas);
      canvas.ClipRect(gfx::Rect(0, 0, 12, kHeight));
      text.Draw(&canvas, -20, 24, 0xff00ff00);
    }
    const size_t ink = Ink(canvas, [](int x, int, SkColor c) {
      EXPECT_TRUE(x < 12 && SkColorGetR(c) == 0 && SkColorGetG(c) > 0 && SkColorGetB(c) == 0)
          << "scroll clipping and color: x=" << x << " " << std::hex << c;
    });
    EXPECT_GT(ink, 0u) << "scroll retains ink";
  }
}

TEST(EditorTextLayoutTest, RasterSharesRtlOrigin) {
  gfx::Canvas canvas(gfx::Size(kWidth, kHeight), 1.0f, false);
  EditorTextLayout rtl_spaces(u"a b   ", 18, false, 2);
  rtl_spaces.Draw(&canvas, 8, 24, 0xffff0000);
  int left = kWidth;
  Ink(canvas, [&](int x, int, SkColor) { left = std::min(left, x); });
  const auto span = rtl_spaces.Spans(0, 1);
  ASSERT_FALSE(span.empty()) << "forced RTL first glyph span";
  const float expected_left = 8 + span.front().x;
  EXPECT_TRUE(left >= std::floor(expected_left) - 2 && left <= std::ceil(expected_left) + 4)
      << "RTL origin shared by raster and geometry: ink at " << left << ", span at " << expected_left;
}

// The baseline argument is measured from the top: ink sits above it, descenders
// aside.
TEST(EditorTextLayoutTest, RasterBaseline) {
  gfx::Canvas canvas(gfx::Size(kWidth, kHeight), 1.0f, false);
  EditorTextLayout text(u"HHH", 18);
  text.Draw(&canvas, 8, 40, 0xff000000);
  int top = kHeight, bottom = -1;
  EXPECT_GT(Ink(canvas, [&](int, int y, SkColor) {
              top = std::min(top, y);
              bottom = std::max(bottom, y);
            }),
            0u);
  EXPECT_LE(bottom, 40);
  EXPECT_GE(bottom, 38);
  EXPECT_GE(top, 40 - std::ceil(text.ascent()));
}

}  // namespace
}  // namespace zitter::prototype

int main(int argc, char** argv) {
  base::TestSuite suite(argc, argv);
  gfx::InitializeFonts();
  return base::LaunchUnitTests(argc, argv, base::BindOnce(&base::TestSuite::Run, base::Unretained(&suite)));
}
