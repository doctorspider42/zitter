// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "chromium/prototype/print_options.h"

#include <algorithm>
#include <vector>

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"

namespace zitter::prototype {

std::optional<std::string> PrintPageRanges(const PrintOptions& options, int page_count) {
  switch (options.pages) {
    case PrintOptions::Pages::kAll: return std::string();
    case PrintOptions::Pages::kCurrent: return base::NumberToString(std::clamp(options.current, 0, std::max(0, page_count - 1)) + 1);
    case PrintOptions::Pages::kRange: break;
  }
  // „1–2, 5”: dashes of any kind, spaces ignored, every page within the document.
  std::u16string text = options.range;
  base::ReplaceChars(text, u"–—", u"-", &text);
  base::RemoveChars(text, u" ", &text);
  std::vector<std::string> parts;
  for (const auto& part : base::SplitString(base::UTF16ToUTF8(text), ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    const auto ends = base::SplitString(part, "-", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
    int first = 0, last = 0;
    if (ends.empty() || ends.size() > 2 || !base::StringToInt(ends.front(), &first) ||
        !base::StringToInt(ends.back(), &last) || first < 1 || last < first || (page_count > 0 && last > page_count))
      return std::nullopt;
    parts.push_back(first == last ? base::NumberToString(first) : base::NumberToString(first) + "-" + base::NumberToString(last));
  }
  if (parts.empty()) return std::nullopt;
  return base::JoinString(parts, ",");
}

}  // namespace zitter::prototype
