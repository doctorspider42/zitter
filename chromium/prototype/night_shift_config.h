// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_PROTOTYPE_NIGHT_SHIFT_CONFIG_H_
#define ZITTER_CHROMIUM_PROTOTYPE_NIGHT_SHIFT_CONFIG_H_

#include <map>
#include <set>
#include <string>

#include "base/values.h"

namespace zitter::prototype {

// Night Shift for the whole app: which profiles run it and the look every
// site without its own tuning gets. Until the person saves it once
// (`managed`), each profile keeps the switch it had before.
struct NightShiftConfig {
  bool managed = false;
  bool enabled = false;
  bool all_profiles = true;
  std::set<std::string> profiles;  // Used when !all_profiles.
  std::map<std::string, bool> profile_overrides;
  int brightness = 100, contrast = 100;  // 75..125
  int sepia = 0;                         // 0..50
  int grayscale = 0;                     // 0..100
  bool force = false;  // Also over a site's own dark theme.

  bool EnabledFor(const std::string& profile_id) const;
  base::DictValue ToValue() const;
  // Missing or invalid fields fall back to the defaults above.
  static NightShiftConfig FromValue(const base::DictValue* value);
};

}  // namespace zitter::prototype

#endif
