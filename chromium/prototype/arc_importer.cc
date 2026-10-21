#include "chromium/prototype/arc_importer.h"

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <utility>

#include "base/base_paths.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/path_service.h"
#include "base/strings/string_util.h"
#include "base/uuid.h"
#include "base/values.h"
#include "build/build_config.h"

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
               ImportedSpace& space, ArcImport& result, std::set<std::string>& visited,
               size_t depth) {
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
    space.pins.push_back({id, parent, Text(item, "title", data->contains("splitView") ? "Split View" : "Folder"), GURL(), true});
    child_parent = id;
    ++result.folder_count;
  } else if (const auto* tab = data->FindDict("tab")) {
    GURL url(Text(*tab, "savedURL", ""));
    if (url.is_valid() && url.SchemeIsHTTPOrHTTPS()) {
      space.pins.push_back({id, parent, Text(item, "title", Text(*tab, "savedTitle", std::string(url.host()))), url, false});
      ++result.tab_count;
    } else { ++result.skipped_count; }
    return;
  } else if (!data->contains("itemContainer")) {
    ++result.skipped_count;
    return;
  }
  if (const auto* children = item.FindList("childrenIds")) {
    for (const auto& child : *children) {
      if (child.is_string()) VisitItem(child.GetString(), child_parent, items, space, result, visited, depth + 1);
      else ++result.skipped_count;
    }
  }
}
}  // namespace

ArcImport ParseArcImport(std::string_view sidebar, std::string_view local_state) {
  ArcImport result;
  auto data = base::JSONReader::ReadDict(sidebar, base::JSON_PARSE_RFC);
  if (!data) { result.error = "Nie udało się odczytać pliku StorableSidebar.json."; return result; }
  auto state = base::JSONReader::ReadDict(local_state, base::JSON_PARSE_RFC);
  const auto* cache = state ? state->FindDictByDottedPath("profile.info_cache") : nullptr;
  std::map<std::string, std::string> profile_ids;
  auto add_profile = [&](const std::string& source) -> std::string {
    if (auto it = profile_ids.find(source); it != profile_ids.end()) return it->second;
    const auto id = "arc-profile-" + source;
    const auto* entry = cache ? cache->FindDict(source) : nullptr;
    result.profiles.push_back({id, entry ? Text(*entry, "name", source) : (source == "Default" ? "Osobisty (Arc)" : source + " (Arc)"),
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
  if (!containers) { result.error = "Nieobsługiwany format paska bocznego Arc."; return result; }
  std::set<std::string> seen_spaces;
  for (const auto& value : *containers) {
    const auto* container = value.GetIfDict();
    if (!container || !container->contains("spaces")) continue;
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
    for (const auto& id : order) {
      if (!seen_spaces.insert(id).second || !spaces.contains(id)) continue;
      const auto& space = *spaces.at(id);
      const auto directory = ProfileDirectory(space.FindDict("profile"));
      if (directory.empty()) { ++result.skipped_count; continue; }
      ImportedSpace imported;
      imported.id = "arc-space-" + id;
      imported.name = Text(space, "title", "Osobiste (Arc)");
      imported.profile_id = add_profile(directory);
      std::set<std::string> visited;
      if (auto it = favorites.find(directory); it != favorites.end()) {
        if (auto root = items.find(it->second); root != items.end()) {
          const auto* children = root->second->FindList("childrenIds");
          if (children && !children->empty()) {
            const auto folder_id = "arc-favorites-" + id;
            imported.pins.push_back({folder_id, "", "Ulubione Arc", GURL(), true});
            ++result.folder_count;
            VisitItem(it->second, folder_id, items, imported, result, visited, 0);
          }
        }
      }
      bool pinned_found = false;
      if (const auto* list = space.FindList("newContainerIDs")) {
        for (size_t i = 0; i + 1 < list->size(); i += 2) {
          const auto* kind = (*list)[i].GetIfDict();
          if (kind && kind->contains("pinned") && (*list)[i + 1].is_string()) {
            if (!items.contains((*list)[i + 1].GetString())) {
              result.error = "Dane Arc są niekompletne. Zamknij Arc i ponów import.";
              return result;
            }
            VisitItem((*list)[i + 1].GetString(), "", items, imported, result, visited, 0);
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
              result.error = "Dane Arc są niekompletne. Zamknij Arc i ponów import.";
              return result;
            }
            VisitItem((*list)[i + 1].GetString(), "", items, imported, result, visited, 0);
              pinned_found = true;
            }
          }
        }
      }
      if (!pinned_found) { result.error = "Nie znaleziono przypiętych kart jednej z przestrzeni Arc. Import został zatrzymany."; return result; }
      result.spaces.push_back(std::move(imported));
    }
  }
  if (result.spaces.empty()) result.error = "Nie znaleziono przestrzeni Arc do importu.";
  return result;
}

ArcImport ReadArcImport(const base::FilePath& arc_directory) {
  if (arc_directory.empty()) {
    ArcImport result;
    result.error = "Import Arc jest obecnie dostępny na macOS.";
    return result;
  }
  constexpr size_t kMaxSize = 32 * 1024 * 1024;
  std::string sidebar, state;
  if (!base::ReadFileToStringWithMaxSize(arc_directory.AppendASCII("StorableSidebar.json"), &sidebar, kMaxSize)) {
    ArcImport result;
    result.error = "Nie znaleziono danych Arc. Uruchom Arc na tym komputerze i spróbuj ponownie.";
    return result;
  }
  base::ReadFileToStringWithMaxSize(arc_directory.AppendASCII("User Data").AppendASCII("Local State"), &state, kMaxSize);
  return ParseArcImport(sidebar, state);
}

base::FilePath DefaultArcDirectory() {
  base::FilePath path;
#if BUILDFLAG(IS_MAC)
  if (base::PathService::Get(base::DIR_APP_DATA, &path)) return path.AppendASCII("Arc");
#endif
  return path;
}
}  // namespace zitter::prototype
