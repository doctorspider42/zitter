// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_PROTOTYPE_NIGHT_SHIFT_H_
#define ZITTER_CHROMIUM_PROTOTYPE_NIGHT_SHIFT_H_

#include <optional>
#include <set>
#include <string>

#include "base/functional/callback.h"
#include "base/values.h"

class GURL;
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
void PushNightShift(Profile* profile, bool enabled, const NightShiftConfig& config, base::OnceClosure done);

// The settings page reports a change by replacing its URL fragment with
// "#apply=<json>". Only the extension's own settings page is listened to.
void ObserveNightShiftSettings(content::WebContents* contents,
                               base::RepeatingCallback<void(base::DictValue)> apply);

// One site's Night Shift, as Zitter's panel shows it. The extension keeps it
// in its local storage under "site:<host>"; the fields follow
// site_policy.js siteSettings(), which reads the same record on the page.
struct NightShiftSite {
  bool enabled = false;  // Night Shift runs in this profile.
  std::string key;       // Empty on pages without site settings.
  std::string mode = "auto";  // auto, on (also over a site's own dark theme), off
  bool force = false;
  int brightness = 100, contrast = 100;
  bool tuned = false;     // Own brightness or contrast.
  bool repaired = false;  // A saved assistant repair, which Undo restores from.
  base::DictValue stored;
  // A repair the assistant staged for this very document, waiting for review.
  std::optional<base::DictValue> draft;
  std::string draft_label;

  bool on() const { return enabled && !key.empty() && mode != "off"; }
};

// "site:<host[:port]>" for http(s) pages, as site_policy.js siteKey().
std::string NightShiftSiteKey(const GURL& url);
// Reads the page's site (and a draft staged for its document). Calls back
// with `enabled` false when the extension is missing.
void ReadNightShiftSite(content::WebContents* page, base::OnceCallback<void(NightShiftSite)> done);
// Merges `change` into the site's record; a none value drops the key so the
// site follows the app-wide default again.
void WriteNightShiftSite(content::WebContents* page, const std::string& key, base::DictValue change,
                         base::OnceClosure done);
// Shows `proposal` on the page (none: back to what is saved). Calls back with
// whether the page's Night Shift can show it.
void PreviewNightShiftRepair(content::WebContents* page, const base::Value& proposal,
                             base::OnceCallback<void(bool)> done);
// Saves the reviewed draft for the site (keeping the previous repair for
// Undo) or drops it; either way the draft ends.
void FinishNightShiftReview(content::WebContents* page, const NightShiftSite& site, bool save,
                            base::OnceClosure done);
// Back to the repair saved before the last one.
void UndoNightShiftRepair(content::WebContents* page, const NightShiftSite& site, base::OnceClosure done);

}  // namespace zitter::prototype

#endif
