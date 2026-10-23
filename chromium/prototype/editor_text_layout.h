// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_PROTOTYPE_EDITOR_TEXT_LAYOUT_H_
#define ZITTER_CHROMIUM_PROTOTYPE_EDITOR_TEXT_LAYOUT_H_
#include <CoreGraphics/CoreGraphics.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>
namespace zitter::prototype {
struct EditorTextRange {
  size_t start = 0, end = 0;
};
struct EditorCaretStop {
  size_t index = 0;
  float x = 0;
  bool secondary = false;
};
struct EditorTextSpan {
  float x = 0, width = 0;
};
// One immutable shaped line is shared by hit testing, caret/selection geometry
// and drawing. All string offsets are UTF-16; all coordinates are logical
// points.
class EditorTextLayout {
public:
  // writing_direction: 0 natural, 1 LTR, 2 RTL; shared by input and raster.
  EditorTextLayout(std::u16string_view text, float font_size,
                   bool medium = false, int writing_direction = 0);
  ~EditorTextLayout();
  EditorTextLayout(const EditorTextLayout &) = delete;
  EditorTextLayout &operator=(const EditorTextLayout &) = delete;
  bool valid() const;
  size_t length() const;
  float width() const;
  float ascent() const;
  float descent() const;
  const std::vector<size_t> &graphemes() const;
  const std::vector<EditorTextRange> &words() const;
  const std::vector<EditorCaretStop> &carets() const;
  EditorCaretStop HitTest(float x) const;
  EditorCaretStop Caret(size_t index, bool secondary = false) const;
  std::vector<EditorTextSpan> Spans(size_t start, size_t end) const;
  // CGContext uses its normal baseline coordinates. The caller supplies the
  // horizontal scroll transform and clip; no truncation/re-shaping is
  // performed.
  void Draw(CGContextRef context, float x, float baseline, uint32_t argb) const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace zitter::prototype
#endif
