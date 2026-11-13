#ifndef ZITTER_CORE_BROWSER_STATE_H_
#define ZITTER_CORE_BROWSER_STATE_H_

#include <map>
#include <optional>
#include <string>

namespace zitter {
// Product-facing snapshots and requests. Adapters enforce browser policies;
// these values neither grant permissions nor carry live browser objects.
struct BrowserPreference {
  int value = 0;
  bool editable = false;
  std::string text = {};
};

struct SitePreferenceRule {
  std::string key;
  std::string primary;
  std::string secondary;
  int setting = 0;
  bool editable = false;
};

struct BrowsingDataSelection {
  int period = 0;  // Hour, day, week, all time, four weeks.
  bool history = false;
  bool cookies = false;
  bool cache = true;
  // Advanced browsing-data categories.
  bool downloads = false;
  bool passwords = false;
  bool form_data = false;
  bool site_settings = false;
  // Time Machine's timeline; unset follows `history` (the settings row).
  std::optional<bool> time_machine;
};

struct SitePermissionState {
  std::string key;      // Backend key ("notifications", "local-network-access").
  int setting = 0;      // 1 allow, 2 block, 3 ask.
  bool can_ask = true;  // Sound, images and JavaScript only allow or block.
  bool today = false;   // Changed since midnight.
  bool editable = true; // False under policy or an extension.
  bool can_allow = true;
};

struct SiteClearChoice {
  bool cookies = true;
  bool cache = true;
  bool history = false;
  bool permissions = false;
};

struct LinkRule {
  std::string host;
  std::string workspace_id;
  bool operator==(const LinkRule&) const = default;
};

struct ProfileInfo {
  std::string id;
  std::string name;
  // Zitter-owned basename only; never an Arc profile directory.
  std::string directory;
  std::string source_id;
  bool operator==(const ProfileInfo&) const = default;
};

enum class ImportBrowser { kArc, kChrome, kEdge, kBrave, kFirefox, kOpera, kVivaldi };

struct BrowserImportOptions {
  ImportBrowser browser = ImportBrowser::kArc;
  bool separate_profiles = true;
  bool bookmarks = true;
  bool history = false;
  bool cookies = false;
};

using BrowserPreferences = std::map<std::string, BrowserPreference>;
}  // namespace zitter
#endif
