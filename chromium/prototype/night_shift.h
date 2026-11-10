// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_PROTOTYPE_NIGHT_SHIFT_H_
#define ZITTER_CHROMIUM_PROTOTYPE_NIGHT_SHIFT_H_

#include <set>
#include <string>

#include "base/functional/callback.h"
#include "base/values.h"

class Profile;
namespace content { class WebContents; }

namespace zitter::prototype {

inline constexpr char kNightShiftId[] = "kdpnmnbnedpcomicojommdikkdepclio";
void EnsureNightShift(Profile* profile);

// Night Shift for the whole app: which profiles run it and the look every
// site without its own tuning gets. Until the person saves it once
// (`managed`), each profile keeps the switch it had before.
struct NightShiftConfig {
  bool managed = false;
  bool enabled = false;
  bool all_profiles = true;
  std::set<std::string> profiles;  // Used when !all_profiles.
  int brightness = 100, contrast = 100;  // 75..125
  int sepia = 0;                         // 0..50
  int grayscale = 0;                     // 0..100
  bool force = false;  // Also over a site's own dark theme.

  bool EnabledFor(const std::string& profile_id) const;
  base::DictValue ToValue() const;
  // Missing or invalid fields fall back to the defaults above.
  static NightShiftConfig FromValue(const base::DictValue* value);
};

// Writes the profile's switch and the default look into the extension's own
// storage; the extension reacts to the change like to its own settings.
void PushNightShift(Profile* profile, bool enabled, const NightShiftConfig& config);

// The settings page reports a change by replacing its URL fragment with
// "#apply=<json>". Only the extension's own settings page is listened to.
void ObserveNightShiftSettings(content::WebContents* contents,
                               base::RepeatingCallback<void(base::DictValue)> apply);

}  // namespace zitter::prototype

#endif
