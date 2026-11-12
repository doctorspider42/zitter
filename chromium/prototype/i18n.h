// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// UI language for native code. Polish is the source language: wrap every UI
// string in T(u"…") (or TF(u"… {} …", {arg}) for templates); with English active
// the Rust catalogue (chromium/prototype/i18n_en_*.rs) supplies the text.
#ifndef ZITTER_CHROMIUM_PROTOTYPE_I18N_H_
#define ZITTER_CHROMIUM_PROTOTYPE_I18N_H_
#include <initializer_list>
#include <string>
#include <string_view>
class PrefService;

namespace zitter::prototype {
std::u16string T(std::u16string_view polish);
// `{}` placeholders in the translated template take `args` in order.
std::u16string TF(std::u16string_view polish, std::initializer_list<std::u16string_view> args);
// The stored choice: "system" (follow the app locale), "pl" or "en".
std::string StoredUiLanguage(PrefService* preferences);
void StoreUiLanguage(std::string_view choice, PrefService* preferences);
// Applies the stored choice (or the app locale) before any UI is built.
void ApplyUiLanguage(std::string_view app_locale,
                     PrefService* preferences = nullptr);
bool UiLanguageIsEnglish();
// Tests pin a language regardless of the machine's locale or stored choice.
void SetUiLanguageForTesting(std::string_view locale);
}  // namespace zitter::prototype
#endif
