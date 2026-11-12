// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CHROMIUM_PROTOTYPE_PRINT_OPTIONS_H_
#define ZITTER_CHROMIUM_PROTOTYPE_PRINT_OPTIONS_H_

#include <optional>
#include <string>

namespace zitter::prototype {

// What the print sheet asks for (design: Find & Print, Print sheet).
struct PrintOptions {
  enum class Pages { kAll, kCurrent, kRange };
  enum class Margins { kDefault, kNone, kMinimal };
  std::string printer;  // Empty: Save as PDF.
  Pages pages = Pages::kAll;
  std::u16string range;  // "1–2, 5" for kRange.
  int current = 0;       // The previewed page, for kCurrent.
  bool landscape = false;
  int copies = 1;
  bool color = true;
  bool backgrounds = false;
  bool headers = true;
  Margins margins = Margins::kDefault;
};

struct PrinterChoice {
  std::string name;
  std::u16string label;
};

// Normalizes the page range for the print backend. An empty string means all pages.
std::optional<std::string> PrintPageRanges(const PrintOptions& options,
                                           int page_count);

}  // namespace zitter::prototype

#endif  // ZITTER_CHROMIUM_PROTOTYPE_PRINT_OPTIONS_H_
