// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/prototype/i18n.h"
#include <optional>
#include <vector>
#include "base/no_destructor.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "chromium/prototype/sidebar_scene.rs.h"
#if BUILDFLAG(IS_MAC)
#include "base/apple/foundation_util.h"
#include "base/strings/sys_string_conversions.h"
#include <CoreFoundation/CoreFoundation.h>
#endif
namespace zitter::prototype {
namespace {
std::optional<std::string>& TestingLanguage() {
  static base::NoDestructor<std::optional<std::string>> language;
  return *language;
}
// The user's first preferred language. Chromium's app locale is not enough: a
// bundle without a Polish locale pack reports en-US on a Polish system.
std::string SystemLanguage(std::string_view app_locale) {
#if BUILDFLAG(IS_MAC)
  base::apple::ScopedCFTypeRef<CFArrayRef> languages(CFLocaleCopyPreferredLanguages());
  if (languages && CFArrayGetCount(languages.get()) > 0) {
    if (auto* first = base::apple::CFCast<CFStringRef>(CFArrayGetValueAtIndex(languages.get(), 0)))
      return base::SysCFStringRefToUTF8(first);
  }
#endif
  return std::string(app_locale);
}
}  // namespace
std::u16string T(std::u16string_view polish) {
  if (!sidebar::ui_language_is_english()) return std::u16string(polish);
  const auto utf8 = base::UTF16ToUTF8(polish);
  const auto text = sidebar::translate(rust::Str(utf8.data(), utf8.size()));
  return base::UTF8ToUTF16(std::string_view(text.data(), text.size()));
}
std::u16string TF(std::u16string_view polish, std::initializer_list<std::u16string_view> args) {
  const auto pattern = T(polish);
  std::u16string out;
  const std::vector<std::u16string_view> values(args);
  size_t start = 0, next = 0;
  for (size_t at = pattern.find(u"{}"); at != std::u16string::npos; at = pattern.find(u"{}", start)) {
    out.append(pattern, start, at - start);
    if (next < values.size()) out.append(values[next++]);
    start = at + 2;
  }
  out.append(pattern, start, std::u16string::npos);
  return out;
}
void ApplyUiLanguage(std::string_view app_locale, PrefService* preferences) {
  if (const auto& testing = TestingLanguage()) {
    sidebar::set_ui_language(rust::Str(testing->data(), testing->size()));
    return;
  }
  const auto choice = StoredUiLanguage(preferences);
  const std::string locale = choice == "system" ? SystemLanguage(app_locale) : choice;
  sidebar::set_ui_language(rust::Str(locale.data(), locale.size()));
}
bool UiLanguageIsEnglish() { return sidebar::ui_language_is_english(); }
void SetUiLanguageForTesting(std::string_view locale) {
  TestingLanguage() = std::string(locale);
  sidebar::set_ui_language(rust::Str(locale.data(), locale.size()));
}
}  // namespace zitter::prototype
