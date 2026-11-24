// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CHROMIUM_PROTOTYPE_USER_AGENT_POLICY_H_
#define ZITTER_CHROMIUM_PROTOTYPE_USER_AGENT_POLICY_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/values.h"

namespace zitter::prototype {

// The profile stores these settings as one dictionary (see docs/user-agent.md):
// {"mode": "auto", "sites": {"host": "preset"}, "custom": [{"name", "ua"}],
//  "client_hints": true, "pinned": false}.

inline constexpr char kUserAgentAuto[] = "auto";
inline constexpr char kUserAgentChrome[] = "chrome";
inline constexpr char kUserAgentZitter[] = "zitter";
inline constexpr char kUserAgentFirefox[] = "firefox";
inline constexpr char kUserAgentSafari[] = "safari";
// Own user agents are "custom:<name>".
inline constexpr char kUserAgentCustomPrefix[] = "custom:";

inline constexpr size_t kMaxUserAgentSites = 500;
inline constexpr size_t kMaxCustomUserAgents = 20;
inline constexpr size_t kMaxCustomUserAgentName = 40;
inline constexpr size_t kMaxCustomUserAgentLength = 512;

struct CustomUserAgent {
  std::string name;
  std::string ua;
};

struct UserAgentSite {
  std::string host;
  std::string preset;
};

struct UserAgentSettings {
  UserAgentSettings();
  UserAgentSettings(const UserAgentSettings&);
  UserAgentSettings& operator=(const UserAgentSettings&);
  ~UserAgentSettings();

  std::string mode = kUserAgentAuto;
  std::vector<UserAgentSite> sites;
  std::vector<CustomUserAgent> custom;
  // Sec-CH-UA and navigator.userAgentData follow the chosen preset.
  bool client_hints = true;
  // The extensions bar shows the User-Agent button.
  bool pinned = false;
};

// Invalid entries are dropped; an invalid mode reads as auto.
UserAgentSettings UserAgentSettingsFromDict(const base::DictValue& dict);
base::DictValue UserAgentSettingsToDict(const UserAgentSettings& settings);
// Whether every entry is valid and within the limits.
bool AreValidUserAgentSettings(const UserAgentSettings& settings);

enum class UserAgentSource {
  // The user's exception for this site.
  kSite,
  // Zitter's built-in catalogue, in auto mode.
  kCatalog,
  // The profile-wide mode.
  kDefault,
};

struct ResolvedUserAgent {
  // A concrete preset: chrome, zitter, firefox, safari or custom:<name>.
  std::string preset;
  UserAgentSource source = UserAgentSource::kDefault;
  // The exception or catalogue entry that matched, if any.
  std::string matched_host;
};

// The built-in catalogue: sites that break unless Zitter introduces itself in
// a particular way. Shipped with the app; never fetched.
struct CatalogEntry {
  const char* host;
  const char* preset;
};
const std::vector<CatalogEntry>& UserAgentCatalog();

// Lowercases, drops a trailing dot and "www."; empty when not a plain host.
std::string NormalizeUserAgentHost(std::string_view host);
// True when `host` is `domain` or one of its subdomains.
bool UserAgentHostMatches(std::string_view host, std::string_view domain);

bool IsBuiltInUserAgentPreset(std::string_view preset);
// A preset the settings can resolve: built in, or an existing own agent.
bool IsKnownUserAgentPreset(const UserAgentSettings& settings,
                            std::string_view preset);
// A valid mode: auto or a known preset.
bool IsValidUserAgentMode(const UserAgentSettings& settings,
                          std::string_view mode);
bool IsValidCustomUserAgent(const CustomUserAgent& agent);

// Site exception, then catalogue (auto only), then the mode. A mode naming
// an own agent that no longer exists falls back to auto.
ResolvedUserAgent ResolveUserAgent(const UserAgentSettings& settings,
                                   std::string_view host);
// The catalogue entry for `host`, if any.
std::optional<CatalogEntry> FindUserAgentCatalogEntry(std::string_view host);
// The user's own exception for exactly this host (after normalization).
std::optional<std::string> FindUserAgentSiteException(
    const UserAgentSettings& settings,
    std::string_view host);

// The user agent string a concrete preset sends. `chrome_ua` is the reduced
// Chrome string of this build, `zitter_version` the app version.
std::string UserAgentString(const UserAgentSettings& settings,
                            std::string_view preset,
                            std::string_view chrome_ua,
                            std::string_view zitter_version);
// Chrome's major version from its user agent string ("Chrome/157.0.0.0").
int ChromeMajorVersion(std::string_view chrome_ua);
// The name shown for a preset: "Chrome", "Firefox", or the own agent's name.
std::u16string UserAgentPresetName(std::string_view preset);

}  // namespace zitter::prototype

#endif  // ZITTER_CHROMIUM_PROTOTYPE_USER_AGENT_POLICY_H_
