#ifndef ZITTER_CHROMIUM_PROTOTYPE_BROWSER_DATA_H_
#define ZITTER_CHROMIUM_PROTOTYPE_BROWSER_DATA_H_

#include <array>
#include <map>
#include <string>
#include <optional>
#include <vector>

#include "url/gurl.h"
#include "base/files/file_path.h"
#include "base/time/time.h"

class Profile;

namespace zitter::prototype {
std::u16string BrowsingProfileName(Profile* profile);
struct BrowserPreference {
  int value = 0;
  bool editable = false;
  std::string text = {};
};
using BrowserPreferences = std::map<std::string, BrowserPreference>;

inline constexpr std::array<const char*, 9> kSitePreferenceKeys = {
    "javascript", "images", "popups", "sound", "notifications", "location", "camera", "microphone", "clipboard"};
// Polish source text (the English is in i18n_en_native.rs): show through T().
inline constexpr std::array<const char16_t*, 9> kSitePreferenceNames = {
    u"JavaScript", u"Obrazy", u"Wyskakujące okna", u"Dźwięk", u"Powiadomienia", u"Lokalizacja", u"Kamera", u"Mikrofon", u"Schowek"};

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
  // The Clear browsing data dialog's advanced kinds (design: Wyczyść dane).
  bool downloads = false;
  bool passwords = false;
  bool form_data = false;
  bool site_settings = false;
  // Time Machine's timeline; unset follows `history` (the settings row).
  std::optional<bool> time_machine;
};

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
  bool operator==(const PaletteResult&) const = default;
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

struct PinnedItem {
  std::string id;
  std::string parent_id;
  std::string title;
  GURL url;
  bool folder = false;
  bool operator==(const PinnedItem&) const = default;
};

struct ImportedSpace {
  std::string id;
  std::string name;
  std::string profile_id;
  std::vector<PinnedItem> pins;
  std::u16string emoji;
};

enum class ImportBrowser { kArc, kChrome, kEdge, kBrave };
struct BrowserImportOptions {
  ImportBrowser browser = ImportBrowser::kArc;
  bool separate_profiles = true;
  bool bookmarks = true;
  bool history = false;
  bool cookies = false;
};

struct ImportSourceProfile {
  base::FilePath directory;
  std::string destination_id;
};
struct ArcImport {
  BrowserImportOptions options;
  std::vector<ImportSourceProfile> sources;
  std::vector<ProfileInfo> profiles;
  std::vector<ImportedSpace> spaces;
  size_t tab_count = 0;
  size_t folder_count = 0;
  size_t skipped_count = 0;
  std::string error;
};
}  // namespace zitter::prototype
#endif
