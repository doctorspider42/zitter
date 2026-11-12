// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "chromium/prototype/i18n.h"

#include <CoreFoundation/CoreFoundation.h>

#include "base/apple/foundation_util.h"
#include "base/strings/sys_string_conversions.h"

namespace zitter::prototype {

namespace {
// The choice lives in the app's defaults; "AppleLanguages" follows it so the
// Chromium-owned UI (menus, system dialogs) switches with it on the next start.
constexpr char kLanguageKey[] = "ZitterLanguage";
}  // namespace

std::string StoredUiLanguage(PrefService*) {
  base::apple::ScopedCFTypeRef<CFPropertyListRef> value(CFPreferencesCopyAppValue(
      base::SysUTF8ToCFStringRef(kLanguageKey).get(), kCFPreferencesCurrentApplication));
  if (auto* string = base::apple::CFCast<CFStringRef>(value.get())) {
    const auto choice = base::SysCFStringRefToUTF8(string);
    if (choice == "pl" || choice == "en") return choice;
  }
  return "system";
}

void StoreUiLanguage(std::string_view choice, PrefService*) {
  const bool fixed = choice == "pl" || choice == "en";
  const auto key = base::SysUTF8ToCFStringRef(kLanguageKey);
  const auto apple = base::SysUTF8ToCFStringRef("AppleLanguages");
  if (fixed) {
    const auto code = base::SysUTF8ToCFStringRef(std::string(choice));
    CFStringRef codes[] = {code.get()};
    base::apple::ScopedCFTypeRef<CFArrayRef> list(CFArrayCreate(nullptr, reinterpret_cast<const void**>(codes), 1, &kCFTypeArrayCallBacks));
    CFPreferencesSetAppValue(key.get(), code.get(), kCFPreferencesCurrentApplication);
    CFPreferencesSetAppValue(apple.get(), list.get(), kCFPreferencesCurrentApplication);
  } else {
    CFPreferencesSetAppValue(key.get(), nullptr, kCFPreferencesCurrentApplication);
    CFPreferencesSetAppValue(apple.get(), nullptr, kCFPreferencesCurrentApplication);
  }
  CFPreferencesAppSynchronize(kCFPreferencesCurrentApplication);
}

}  // namespace zitter::prototype
