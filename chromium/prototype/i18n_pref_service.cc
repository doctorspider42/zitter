// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "chromium/prototype/i18n.h"

#include "components/language/core/browser/pref_names.h"
#include "components/prefs/pref_service.h"

namespace zitter::prototype {

std::string StoredUiLanguage(PrefService* preferences) {
  // Use the embedder's application locale preference so Chromium's menus
  // and the Zitter UI agree on the next launch.
  if (preferences) {
    const auto choice = preferences->GetString(language::prefs::kApplicationLocale);
    if (choice == "pl") return "pl";
    if (choice == "en" || choice == "en-US") return "en";
  }
  return "system";
}

void StoreUiLanguage(std::string_view choice, PrefService* preferences) {
  if (preferences) {
    // Chromium has an en-US pack, not a bare "en" one.
    preferences->SetString(language::prefs::kApplicationLocale,
                           choice == "pl" ? "pl" : choice == "en" ? "en-US" : "");
  }
}

}  // namespace zitter::prototype
