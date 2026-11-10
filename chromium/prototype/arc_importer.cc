#include "chromium/prototype/arc_importer.h"

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <utility>

#include "base/base_paths.h"
#include "base/files/file_util.h"
#include "base/files/file_enumerator.h"
#include "base/json/json_reader.h"
#include "base/path_service.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/strings/utf_string_conversion_utils.h"
#include "base/uuid.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chromium/prototype/i18n.h"

namespace zitter::prototype {
namespace {
using Dictionary = std::map<std::string, const base::DictValue*>;
Dictionary SwiftDictionary(const base::Value* value) {
  Dictionary result;
  if (!value) return result;
  if (const auto* dict = value->GetIfDict()) {
    for (auto [key, item] : *dict)
      if (item.is_dict()) result.emplace(key, &item.GetDict());
  } else if (const auto* list = value->GetIfList()) {
    for (size_t i = 0; i + 1 < list->size(); i += 2)
      if ((*list)[i].is_string() && (*list)[i + 1].is_dict())
        result.emplace((*list)[i].GetString(), &(*list)[i + 1].GetDict());
  }
  return result;
}
std::string ProfileDirectory(const base::DictValue* profile) {
  if (!profile) return {};
  if (profile->contains("default")) return "Default";
  if (const auto* directory = profile->FindStringByDottedPath("custom._0.directoryBasename")) {
    // Only an identity: never use Arc's basename as a destination path.
    if (!directory->empty() && directory->find_first_of("/\\") == std::string::npos &&
        *directory != "." && *directory != "..") return *directory;
  }
  return {};
}
std::string Text(const base::DictValue& dict, std::string_view key, std::string fallback) {
  const auto* value = dict.FindString(key);
  return value && !value->empty() ? *value : std::move(fallback);
}
void VisitItem(const std::string& id, const std::string& parent, const Dictionary& items,
               std::vector<PinnedItem>& pins, ArcImport& result,
               std::set<std::string>& visited, size_t depth, bool favorite = false) {
  if (depth > 64 || visited.size() > 20000 || !visited.insert(id).second) {
    ++result.skipped_count;
    return;
  }
  auto it = items.find(id);
  if (it == items.end()) { ++result.skipped_count; return; }
  const auto& item = *it->second;
  const auto* data = item.FindDict("data");
  if (!data) { ++result.skipped_count; return; }
  std::string child_parent = parent;
  if (data->contains("list") || data->contains("splitView")) {
    if (!favorite) {
      pins.push_back({id, parent, Text(item, "title", data->contains("splitView") ? "Split View" : "Folder"), GURL(), true});
      if (data->FindDictByDottedPath("list.automaticLiveFolderData.dataSource.github"))
        pins.back().github.emplace();
      child_parent = id;
      ++result.folder_count;
    }
  } else if (const auto* tab = data->FindDict("tab")) {
    GURL url(Text(*tab, "savedURL", ""));
    if (url.is_valid() && url.SchemeIsHTTPOrHTTPS()) {
      pins.push_back({favorite ? "arc-favorite-" + id : id, parent,
                      Text(item, "title", Text(*tab, "savedTitle", std::string(url.host()))),
                      url, false, favorite});
      ++result.tab_count;
    } else { ++result.skipped_count; }
    return;
  } else if (!data->contains("itemContainer")) {
    ++result.skipped_count;
    return;
  }
  if (const auto* children = item.FindList("childrenIds")) {
    for (const auto& child : *children) {
      if (child.is_string()) VisitItem(child.GetString(), child_parent, items, pins, result, visited, depth + 1, favorite);
      else ++result.skipped_count;
    }
  }
}
}  // namespace

ArcImport ParseArcImport(std::string_view sidebar, std::string_view local_state) {
  ArcImport result;
  auto data = base::JSONReader::ReadDict(sidebar, base::JSON_PARSE_RFC);
  if (!data) { result.error = base::UTF16ToUTF8(T(u"Nie udało się odczytać pliku StorableSidebar.json.")); return result; }
  auto state = base::JSONReader::ReadDict(local_state, base::JSON_PARSE_RFC);
  const auto* cache = state ? state->FindDictByDottedPath("profile.info_cache") : nullptr;
  std::map<std::string, std::string> profile_ids;
  auto add_profile = [&](const std::string& source) -> std::string {
    if (auto it = profile_ids.find(source); it != profile_ids.end()) return it->second;
    const auto id = "arc-profile-" + source;
    const auto* entry = cache ? cache->FindDict(source) : nullptr;
    result.profiles.push_back({id, entry ? Text(*entry, "name", source) : (source == "Default" ? base::UTF16ToUTF8(T(u"Osobisty (Arc)")) : source + " (Arc)"),
                              "ZitterProfile-" + base::Uuid::GenerateRandomV4().AsLowercaseString(), "arc:" + source});
    profile_ids[source] = id;
    return id;
  };
  // Include local profiles even when they have no Space.
  if (cache) {
    for (auto [directory, entry] : *cache) {
      if (entry.is_dict() && directory != "Guest Profile" && directory != "System Profile" &&
          directory.find_first_of("/\\") == std::string::npos && directory != "." && directory != "..")
        add_profile(directory);
    }
  }
  const auto* containers = data->FindListByDottedPath("sidebar.containers");
  if (!containers) { result.error = base::UTF16ToUTF8(T(u"Nieobsługiwany format paska bocznego Arc.")); return result; }
  std::set<std::string> seen_spaces;
  // Containers alternate a kind marker ({"global": {}}, {"littleBrowser": {}})
  // and its data. Little Arc windows hold transient spaces with no pinned tabs.
  bool little_browser = false;
  for (const auto& value : *containers) {
    const auto* container = value.GetIfDict();
    if (container && !container->contains("spaces")) {
      little_browser = container->contains("littleBrowser");
      continue;
    }
    if (!container || std::exchange(little_browser, false)) continue;
    const auto spaces = SwiftDictionary(container->Find("spaces"));
    const auto items = SwiftDictionary(container->Find("items"));
    std::vector<std::string> order;
    if (const auto* ordered = data->FindListByDottedPath("firebaseSyncState.syncData.orderedSpaceIDs.value"))
      for (const auto& id : *ordered) if (id.is_string() && spaces.contains(id.GetString())) order.push_back(id.GetString());
    // Preserve serialized order when the sync order is unavailable.
    if (const auto* list = container->FindList("spaces")) {
      for (size_t i = 0; i + 1 < list->size(); i += 2)
        if ((*list)[i].is_string()) order.push_back((*list)[i].GetString());
    } else {
      for (const auto& [id, space] : spaces) order.push_back(id);
    }
    std::map<std::string, std::string> favorites;
    if (const auto* list = container->FindList("topAppsContainerIDs")) {
      for (size_t i = 0; i + 1 < list->size(); i += 2) {
        const auto directory = ProfileDirectory((*list)[i].GetIfDict());
        if (!directory.empty() && (*list)[i + 1].is_string()) favorites[directory] = (*list)[i + 1].GetString();
      }
    }
    for (const auto& [directory, root] : favorites) {
      if (!items.contains(root)) continue;
      const auto profile_id = add_profile(directory);
      auto [entry, inserted] = result.profile_favorites.try_emplace(profile_id);
      if (!inserted) continue;
      std::set<std::string> visited;
      VisitItem(root, "", items, entry->second, result, visited, 0, true);
    }
    for (const auto& id : order) {
      if (!seen_spaces.insert(id).second || !spaces.contains(id)) continue;
      const auto& space = *spaces.at(id);
      const auto directory = ProfileDirectory(space.FindDict("profile"));
      if (directory.empty()) { ++result.skipped_count; continue; }
      ImportedSpace imported;
      imported.id = "arc-space-" + id;
      imported.name = Text(space, "title", base::UTF16ToUTF8(T(u"Osobiste (Arc)")));
      imported.profile_id = add_profile(directory);
      // Current Arc stores the full emoji sequence; older exports use a scalar.
      if (const auto* icon = space.FindDictByDottedPath("customInfo.iconType")) {
        if (const auto* emoji = icon->FindString("emoji_v2"); emoji && !emoji->empty()) {
          if (base::IsStringUTF8(*emoji)) imported.emoji = base::UTF8ToUTF16(*emoji);
        } else if (auto codepoint = icon->FindInt("emoji"); codepoint && *codepoint >= 0x20 &&
                   *codepoint <= 0x10FFFF && !(*codepoint >= 0xD800 && *codepoint <= 0xDFFF)) {
          base::WriteUnicodeCharacter(*codepoint, &imported.emoji);
        }
      }
      std::set<std::string> visited;
      bool pinned_found = false;
      if (const auto* list = space.FindList("newContainerIDs")) {
        for (size_t i = 0; i + 1 < list->size(); i += 2) {
          const auto* kind = (*list)[i].GetIfDict();
          if (kind && kind->contains("pinned") && (*list)[i + 1].is_string()) {
            if (!items.contains((*list)[i + 1].GetString())) {
              result.error = base::UTF16ToUTF8(T(u"Dane Arc są niekompletne. Zamknij Arc i ponów import."));
              return result;
            }
            VisitItem((*list)[i + 1].GetString(), "", items, imported.pins, result, visited, 0);
            pinned_found = true;
          }
        }
      }
      // Earlier Arc snapshots encode the root kinds as strings.
      if (!pinned_found) {
        if (const auto* list = space.FindList("containerIDs")) {
          for (size_t i = 0; i + 1 < list->size(); i += 2) {
            if ((*list)[i].is_string() && (*list)[i].GetString() == "pinned" && (*list)[i + 1].is_string()) {
              if (!items.contains((*list)[i + 1].GetString())) {
              result.error = base::UTF16ToUTF8(T(u"Dane Arc są niekompletne. Zamknij Arc i ponów import."));
              return result;
            }
            VisitItem((*list)[i + 1].GetString(), "", items, imported.pins, result, visited, 0);
              pinned_found = true;
            }
          }
        }
      }
      if (!pinned_found) { result.error = base::UTF16ToUTF8(T(u"Nie znaleziono przypiętych kart jednej z przestrzeni Arc. Import został zatrzymany.")); return result; }
      result.spaces.push_back(std::move(imported));
    }
  }
  if (result.spaces.empty()) result.error = base::UTF16ToUTF8(T(u"Nie znaleziono przestrzeni Arc do importu."));
  return result;
}

ArcImport ReadArcImport(const base::FilePath& arc_directory) {
  if (arc_directory.empty()) {
    ArcImport result;
    result.error = base::UTF16ToUTF8(T(u"Nie znaleziono Arc na tym komputerze."));
    return result;
  }
  constexpr size_t kMaxSize = 32 * 1024 * 1024;
  std::string sidebar, state;
  if (!base::ReadFileToStringWithMaxSize(arc_directory.AppendASCII("StorableSidebar.json"), &sidebar, kMaxSize)) {
    ArcImport result;
    result.error = base::UTF16ToUTF8(T(u"Nie znaleziono danych Arc. Uruchom Arc na tym komputerze i spróbuj ponownie."));
    return result;
  }
  base::ReadFileToStringWithMaxSize(arc_directory.AppendASCII("User Data").AppendASCII("Local State"), &state, kMaxSize);
  return ParseArcImport(sidebar, state);
}

base::FilePath DefaultArcDirectory() {
  base::FilePath path;
#if BUILDFLAG(IS_MAC)
  if (base::PathService::Get(base::DIR_APP_DATA, &path)) return path.AppendASCII("Arc");
#elif BUILDFLAG(IS_WIN)
  // Arc for Windows is an MSIX package; its data sits in the package's
  // redirected LocalAppData, laid out as on macOS.
  if (base::PathService::Get(base::DIR_LOCAL_APP_DATA, &path)) {
    base::FileEnumerator packages(path.AppendASCII("Packages"), false, base::FileEnumerator::DIRECTORIES,
                                  FILE_PATH_LITERAL("TheBrowserCompany.Arc_*"));
    for (auto package = packages.Next(); !package.empty(); package = packages.Next()) {
      const auto arc = package.AppendASCII("LocalCache").AppendASCII("Local").AppendASCII("Arc");
      if (base::PathExists(arc.AppendASCII("StorableSidebar.json"))) return arc;
    }
  }
  path.clear();
#endif
  return path;
}

std::u16string ImportBrowserName(ImportBrowser browser) {
  switch (browser) {
    case ImportBrowser::kArc: return u"Arc";
    case ImportBrowser::kChrome: return u"Google Chrome";
    case ImportBrowser::kEdge: return u"Microsoft Edge";
    case ImportBrowser::kBrave: return u"Brave";
  }
}

base::FilePath DefaultBrowserDirectory(ImportBrowser browser) {
  if (browser == ImportBrowser::kArc) return DefaultArcDirectory();
  base::FilePath path;
#if BUILDFLAG(IS_MAC)
  if (!base::PathService::Get(base::DIR_APP_DATA, &path)) return {};
  switch (browser) {
    case ImportBrowser::kChrome: return path.AppendASCII("Google/Chrome");
    case ImportBrowser::kEdge: return path.AppendASCII("Microsoft Edge");
    case ImportBrowser::kBrave: return path.AppendASCII("BraveSoftware/Brave-Browser");
    case ImportBrowser::kArc: break;
  }
#elif BUILDFLAG(IS_WIN)
  if (!base::PathService::Get(base::DIR_LOCAL_APP_DATA, &path)) return {};
  switch (browser) {
    case ImportBrowser::kChrome: return path.AppendASCII("Google").AppendASCII("Chrome").AppendASCII("User Data");
    case ImportBrowser::kEdge: return path.AppendASCII("Microsoft").AppendASCII("Edge").AppendASCII("User Data");
    case ImportBrowser::kBrave:
      return path.AppendASCII("BraveSoftware").AppendASCII("Brave-Browser").AppendASCII("User Data");
    case ImportBrowser::kArc: break;
  }
#endif
  return {};
}

ArcImport ApplyImportOptions(ArcImport result, BrowserImportOptions options) {
  result.options = options;
  if (!result.error.empty()) return result;
  if (!options.bookmarks) {
    for (auto& space : result.spaces) space.pins.clear();
    result.profile_favorites.clear();
    result.tab_count = result.folder_count = 0;
  }
  if (!options.separate_profiles && !result.profiles.empty()) {
    // A separate destination per browser prevents cross-browser identity clashes.
    auto profile = result.profiles.front();
    profile.id = options.browser == ImportBrowser::kArc ? "arc-profile-merged" :
        options.browser == ImportBrowser::kChrome ? "chrome-profile-merged" :
        options.browser == ImportBrowser::kEdge ? "edge-profile-merged" : "brave-profile-merged";
    profile.name = base::UTF16ToUTF8(TF(u"Import z {}", {ImportBrowserName(options.browser)}));
    profile.source_id = "import:" + profile.id;
    if (!result.profile_favorites.empty()) {
      std::vector<PinnedItem> favorites;
      std::set<std::string> seen;
      for (const auto& source : result.profiles) {
        const auto entry = result.profile_favorites.find(source.id);
        if (entry == result.profile_favorites.end()) continue;
        for (auto& favorite : entry->second) {
          if (seen.insert(favorite.id).second) favorites.push_back(std::move(favorite));
          else --result.tab_count;
        }
      }
      result.profile_favorites.clear();
      result.profile_favorites.emplace(profile.id, std::move(favorites));
    }
    result.profiles = {profile};
    for (auto& space : result.spaces) space.profile_id = profile.id;
    for (auto& source : result.sources) source.destination_id = profile.id;
  }
  return result;
}

namespace {
void VisitBookmark(const base::DictValue& node, const std::string& parent,
                   ImportedSpace& space, ArcImport& result, size_t depth,
                   size_t& visited) {
  if (depth > 64 || ++visited > 20000) { ++result.skipped_count; return; }
  const auto* type = node.FindString("type");
  if (!type) { ++result.skipped_count; return; }
  // Generate unique destination IDs even for damaged/duplicate source IDs.
  const auto id = space.id + "-bookmark-" + std::to_string(visited);
  const auto name = Text(node, "name", base::UTF16ToUTF8(T(u"Bez nazwy")));
  if (*type == "url") {
    const GURL url(Text(node, "url", ""));
    if (!url.SchemeIsHTTPOrHTTPS() || !url.is_valid()) { ++result.skipped_count; return; }
    space.pins.push_back({id, parent, name, url, false});
    ++result.tab_count;
  } else if (*type == "folder") {
    space.pins.push_back({id, parent, name, GURL(), true});
    ++result.folder_count;
    if (const auto* children = node.FindList("children")) {
      for (const auto& child : *children) {
        if (child.is_dict()) VisitBookmark(child.GetDict(), id, space, result, depth + 1, visited);
        else ++result.skipped_count;
      }
    }
  } else ++result.skipped_count;
}
}  // namespace

ArcImport ReadBrowserImport(BrowserImportOptions options, const base::FilePath& directory) {
  if (options.browser == ImportBrowser::kArc) {
    auto result = ReadArcImport(directory);
    for (const auto& profile : result.profiles)
      result.sources.push_back({directory.AppendASCII("User Data").AppendASCII(profile.source_id.substr(4)), profile.id});
    return ApplyImportOptions(std::move(result), options);
  }
  ArcImport result;
  if (directory.empty() || !base::DirectoryExists(directory)) {
    result.error = base::UTF16ToUTF8(TF(u"Nie znaleziono danych przeglądarki {}.", {ImportBrowserName(options.browser)}));
    return result;
  }
  const char* prefix = options.browser == ImportBrowser::kChrome ? "chrome" :
      options.browser == ImportBrowser::kEdge ? "edge" : "brave";
  constexpr size_t kMaxSize = 32 * 1024 * 1024;
  std::string state_json;
  base::ReadFileToStringWithMaxSize(directory.AppendASCII("Local State"), &state_json, kMaxSize);
  auto state = base::JSONReader::ReadDict(state_json, base::JSON_PARSE_RFC);
  const auto* cache = state ? state->FindDictByDottedPath("profile.info_cache") : nullptr;
  std::set<std::string> directories;
  auto valid_directory = [](const std::string& name) {
    return name == "Default" || (name.starts_with("Profile ") && name.size() > 8 &&
        std::all_of(name.begin() + 8, name.end(), [](char c) { return c >= '0' && c <= '9'; }));
  };
  if (cache) for (const auto [name, value] : *cache)
    if (value.is_dict() && valid_directory(name)) directories.insert(name);
  base::FileEnumerator folders(directory, false, base::FileEnumerator::DIRECTORIES);
  for (auto path = folders.Next(); !path.empty(); path = folders.Next()) {
    const auto name = path.BaseName().AsUTF8Unsafe();
    if (valid_directory(name)) directories.insert(name);
  }
  for (const auto& name : directories) {
    const auto profile_dir = directory.AppendASCII(name);
    if (!base::DirectoryExists(profile_dir)) continue;
    const auto* entry = cache ? cache->FindDict(name) : nullptr;
    const auto title = entry ? Text(*entry, "name", name) : name;
    ProfileInfo profile;
    profile.id = std::string(prefix) + "-profile-" + name;
    profile.name = title + " (" + base::UTF16ToUTF8(ImportBrowserName(options.browser)) + ")";
    profile.directory = "ZitterProfile-" + base::Uuid::GenerateRandomV4().AsLowercaseString();
    profile.source_id = std::string(prefix) + ":" + name;
    ImportedSpace space;
    space.id = std::string(prefix) + "-space-" + name;
    space.name = title;
    space.profile_id = profile.id;
    if (options.bookmarks && base::PathExists(profile_dir.AppendASCII("Bookmarks"))) {
      std::string bookmarks_json;
      if (!base::ReadFileToStringWithMaxSize(profile_dir.AppendASCII("Bookmarks"), &bookmarks_json, kMaxSize)) {
        result.error = base::UTF16ToUTF8(T(u"Nie udało się odczytać zakładek. Zamknij przeglądarkę i spróbuj ponownie."));
        return result;
      }
      auto data = base::JSONReader::ReadDict(bookmarks_json, base::JSON_PARSE_RFC);
      const auto* roots = data ? data->FindDict("roots") : nullptr;
      if (!roots) { result.error = base::UTF16ToUTF8(T(u"Nieprawidłowy plik zakładek. Import został zatrzymany.")); return result; }
      size_t visited = 0;
      for (const auto key : {"bookmark_bar", "other", "synced"})
        if (const auto* root = roots->FindDict(key)) VisitBookmark(*root, "", space, result, 0, visited);
    }
    result.sources.push_back({profile_dir, profile.id});
    result.profiles.push_back(std::move(profile));
    result.spaces.push_back(std::move(space));
  }
  if (result.spaces.empty()) result.error = base::UTF16ToUTF8(T(u"Nie znaleziono profili do importu."));
  return ApplyImportOptions(std::move(result), options);
}
}  // namespace zitter::prototype
