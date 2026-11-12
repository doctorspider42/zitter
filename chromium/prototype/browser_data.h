#ifndef ZITTER_CHROMIUM_PROTOTYPE_BROWSER_DATA_H_
#define ZITTER_CHROMIUM_PROTOTYPE_BROWSER_DATA_H_

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <optional>
#include <vector>

#include "zitter/core/browser_state.h"
#include "url/gurl.h"
#include "base/files/file_path.h"
#include "base/time/time.h"

class Profile;

namespace zitter::prototype {
std::u16string BrowsingProfileName(Profile* profile);
using ::zitter::BrowserPreference;
using ::zitter::BrowserPreferences;

inline constexpr std::array<const char*, 9> kSitePreferenceKeys = {
    "javascript", "images", "popups", "sound", "notifications", "location", "camera", "microphone", "clipboard"};
// Polish source text (the English is in i18n_en_native.rs): show through T().
inline constexpr std::array<const char16_t*, 9> kSitePreferenceNames = {
    u"JavaScript", u"Obrazy", u"Wyskakujące okna", u"Dźwięk", u"Powiadomienia", u"Lokalizacja", u"Kamera", u"Mikrofon", u"Schowek"};

using ::zitter::SitePreferenceRule;
using ::zitter::BrowsingDataSelection;

// The site panel (design: Zitter dostępy stron, W1/W2).
using ::zitter::SitePermissionState;
struct SiteSummary {
  GURL url;
  std::u16string host;
  bool secure = false;
  std::vector<SitePermissionState> permissions;
};
struct SiteCookie {
  std::string name;
  std::string domain;
  std::string path;
  base::Time expires;  // Null: until the browser closes.
};
struct SiteDataFacts {
  std::u16string domain;      // The registrable domain the data belongs to.
  int64_t bytes = -1;         // Cookies and site storage; -1 while unknown.
  int64_t cookie_bytes = -1;
  int64_t storage_bytes = -1; // localStorage, IndexedDB, Cache Storage…
  int cookies = -1;
  std::vector<SiteCookie> cookie_list;
  int visits = -1;
  base::Time first_visit;
};
using ::zitter::SiteClearChoice;

struct PaletteResult {
  std::u16string title;
  std::u16string detail;
  GURL url;
  std::optional<size_t> tab;
  std::string workspace_id;
  std::string pin_id;
  bool workspace_only = false;
  std::string command_id;
  std::string target_workspace_id;
  int command_page_id = 0;
  uint64_t command_tab_id = 0;
  bool operator==(const PaletteResult&) const = default;
};

using ::zitter::LinkRule;

using ::zitter::ProfileInfo;

struct GitHubLiveFolder {
  std::string login;
  std::string repository;
  std::string encrypted_token;
  bool operator==(const GitHubLiveFolder&) const = default;
};

struct PinnedItem {
  std::string id;
  std::string parent_id;
  std::string title;
  GURL url;
  bool folder = false;
  bool profile_pin = false;
  std::optional<GitHubLiveFolder> github = std::nullopt;
  bool operator==(const PinnedItem&) const = default;
};

struct ImportedSpace {
  std::string id;
  std::string name;
  std::string profile_id;
  std::vector<PinnedItem> pins;
  std::u16string emoji;
};

using ::zitter::ImportBrowser;
using ::zitter::BrowserImportOptions;

struct ImportSourceProfile {
  base::FilePath directory;
  std::string destination_id;
};
struct ArcImport {
  BrowserImportOptions options;
  std::vector<ImportSourceProfile> sources;
  std::vector<ProfileInfo> profiles;
  std::vector<ImportedSpace> spaces;
  std::map<std::string, std::vector<PinnedItem>> profile_favorites;
  size_t tab_count = 0;
  size_t folder_count = 0;
  size_t skipped_count = 0;
  std::string error;
};
}  // namespace zitter::prototype
#endif
