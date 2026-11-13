#ifndef ZITTER_CHROMIUM_PROTOTYPE_BROWSER_DATA_H_
#define ZITTER_CHROMIUM_PROTOTYPE_BROWSER_DATA_H_

#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <optional>
#include <vector>

#include "zitter/core/browser_state.h"
#include "zitter/core/workspace_model.h"
#include "url/gurl.h"
#include "base/files/file_path.h"
#include "base/feature_list.h"
#include "base/time/time.h"

class Profile;

namespace zitter::prototype {
BASE_DECLARE_FEATURE(kZitterLiveFolders);
std::u16string BrowsingProfileName(Profile* profile);
using ::zitter::BrowserPreference;
using ::zitter::BrowserPreferences;

inline constexpr std::array<const char*, 9> kSitePreferenceKeys = {
    "javascript", "images", "popups", "sound", "notifications", "location", "camera", "microphone", "clipboard"};
// Polish source text (the English is in i18n_en_native.rs): show through T().
inline constexpr std::array<const char16_t*, 9> kSitePreferenceNames = {
    u"JavaScript", u"Obrazy", u"Wyskakujące okna", u"Dźwięk", u"Powiadomienia", u"Lokalizacja", u"Kamera", u"Mikrofon", u"Schowek"};

// The site settings categories exposed by Chromium, including feature- and
// platform-dependent entries. Each backend supplies the policies it implements.
struct SiteCategory {
  const char* key;
  const char16_t* title;
};

inline constexpr SiteCategory kSiteContentCategories[] = {
    {"javascript", u"JavaScript"},
    {"images", u"Obrazy"},
    {"popups", u"Wyskakujące okna"},
    {"sound", u"Dźwięk"},
    {"automatic-downloads", u"Pobieranie wielu plików"},
    {"auto-picture-in-picture", u"Automatyczny obraz w obrazie"},
    {"mixed-script", u"Niezabezpieczona zawartość"},
    {"subresource-filter", u"Natrętne reklamy"},
    {"javascript-optimizer", u"Optymalizator JavaScript"},
    {"javascript-jit", u"Kompilacja JavaScript JIT"},
    {"protected-media-identifier", u"Chronione treści"},
    {"automatic-fullscreen", u"Pełny ekran bez pytania"},
};

inline constexpr SiteCategory kSiteDeviceCategories[] = {
    {"media-stream-camera", u"Kamera"},
    {"media-stream-mic", u"Mikrofon"},
    {"geolocation", u"Lokalizacja"},
    {"notifications", u"Powiadomienia"},
    {"clipboard", u"Schowek"},
    {"file-system-write-guard", u"Edycja plików"},
    {"bluetooth-guard", u"Urządzenia Bluetooth"},
    {"bluetooth-scanning", u"Wyszukiwanie Bluetooth"},
    {"usb-guard", u"Urządzenia USB"},
    {"hid-guard", u"Urządzenia HID"},
    {"serial-guard", u"Porty szeregowe"},
    {"midi-sysex", u"Urządzenia MIDI"},
    {"smart-card-guard", u"Karty inteligentne"},
    {"sensors", u"Czujniki ruchu"},
    {"vr", u"Wirtualna rzeczywistość"},
    {"ar", u"Rozszerzona rzeczywistość"},
    {"hand-tracking", u"Śledzenie dłoni"},
    {"speaker-selection", u"Wybór głośnika"},
    {"captured-surface-control", u"Sterowanie udostępnianym ekranem"},
    {"web-printing", u"Drukowanie"},
};

inline constexpr SiteCategory kSiteOtherCategories[] = {
    {"background-sync", u"Synchronizacja w tle"},
    {"idle-detection", u"Wykrywanie bezczynności"},
    {"local-fonts", u"Czcionki na komputerze"},
    {"window-placement", u"Zarządzanie oknami"},
    {"keyboard-lock", u"Przejmowanie klawiatury"},
    {"storage-access", u"Dostęp do danych w ramkach"},
    {"top-level-storage-access", u"Dostęp do danych innych witryn"},
    {"payment-handler", u"Obsługa płatności"},
    {"webid-api", u"Logowanie przez zewnętrzne usługi"},
    {"web-app-installation", u"Instalowanie aplikacji internetowych"},
    {"local-network-access", u"Połączenia z siecią lokalną"},
    {"local-network", u"Sieć lokalna"},
    {"loopback-network", u"Usługi na tym komputerze"},
    {"protocol-handler", u"Otwieranie linków"},
};

inline constexpr auto kSiteCategoryGroups = std::to_array<std::span<const SiteCategory>>(
    {kSiteDeviceCategories, kSiteContentCategories, kSiteOtherCategories});

inline constexpr std::string_view kSiteDefaultPrefix = "site_default.";

inline constexpr std::string_view SiteDefaultKey(std::string_view key) {
  if (key == "media-stream-camera") return "camera";
  if (key == "media-stream-mic") return "microphone";
  if (key == "geolocation") return "location";
  return key;
}

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

struct LiveFolderConfig {
  std::string account;
  std::string filter;
  std::string encrypted_token;
  LiveFolderProvider provider = LiveFolderProvider::kGitHub;
  std::string site = {};
  bool operator==(const LiveFolderConfig&) const = default;
};

inline constexpr std::array<std::string_view, kLiveFolderProviders.size()>
    kLiveFolderProviderKeys{"github", "gitlab", "jira"};

struct PinnedItem {
  std::string id;
  std::string parent_id;
  std::string title;
  GURL url;
  bool folder = false;
  bool profile_pin = false;
  std::optional<LiveFolderConfig> live_folder = std::nullopt;
  std::string custom_title = {};
  const std::string& display_title() const {
    return custom_title.empty() ? title : custom_title;
  }
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
