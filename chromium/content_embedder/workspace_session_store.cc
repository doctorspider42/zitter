// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "workspace_session_store.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>

#include "base/base64.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_util.h"
#include "base/uuid.h"
#include "chromium/prototype/workspace_session.h"
#include "chromium/prototype/workspace_shell.h"
#include "chromium/prototype/workspace_urls.h"

namespace zitter::content_embedder {
namespace {

constexpr size_t kMaxSessionBytes = 16 * 1024 * 1024;

std::optional<std::array<std::string, kWorkspaceIds.size()>> ReadBuiltinNames(
    const base::DictValue& session, std::string_view key) {
  std::array<std::string, kWorkspaceIds.size()> names;
  std::ranges::copy(kBuiltinProfileNames, names.begin());
  if (!session.contains(key))
    return names;
  const auto* overrides = session.FindDict(key);
  if (!overrides || overrides->empty())
    return std::nullopt;
  for (const auto [id, name] : *overrides) {
    const auto position = std::ranges::find(kWorkspaceIds, id);
    if (position == kWorkspaceIds.end() || !name.is_string() ||
        !IsContentWorkspaceName(name.GetString())) {
      return std::nullopt;
    }
    names[position - kWorkspaceIds.begin()] = name.GetString();
  }
  return names;
}

bool IsValidSession(const base::DictValue& session) {
  const auto workspaces = ReadContentWorkspaces(session);
  if (!workspaces || session.size() != workspaces->size() + 1 +
                                         session.contains("extra_workspaces") +
                                         session.contains("extra_profiles") +
                                         session.contains("workspace_names") +
                                         session.contains("profile_names") +
                                         session.contains("removed_workspaces") +
                                         session.contains("timeline_epochs") +
                                         session.contains("profile_favorites") +
                                         session.contains("link_rules") +
                                         session.contains("clean_address_copies") +
                                         session.contains("shift_click_peek") +
                                         session.contains("assistant") +
                                         session.contains("night_shift") +
                                         session.contains("startup_import_pending") +
                                         session.contains("window_placement")) {
    return false;
  }
  if (session.contains("startup_import_pending") &&
      !session.FindBool("startup_import_pending").value_or(false)) {
    return false;
  }
  if (session.contains("window_placement")) {
    const auto* placement = session.FindDict("window_placement");
    if (!placement || placement->size() != 6 ||
        !placement->FindBool("fullscreen") || !placement->FindBool("maximized") ||
        placement->FindInt("width").value_or(0) <= 0 ||
        placement->FindInt("height").value_or(0) <= 0) {
      return false;
    }
    for (const auto* key : {"x", "y", "width", "height"}) {
      const auto value = placement->FindInt(key);
      if (!value || *value < -100000 || *value > 100000)
        return false;
    }
  }
  if (session.contains("night_shift") && !session.FindDict("night_shift"))
    return false;
  if (session.contains("assistant") && !session.FindDict("assistant"))
    return false;
  if (session.contains("shift_click_peek") && !session.FindBool("shift_click_peek"))
    return false;
  if (session.contains("clean_address_copies") &&
      !session.FindBool("clean_address_copies")) {
    return false;
  }
  if (session.contains("timeline_epochs")) {
    const auto* epochs = session.FindDict("timeline_epochs");
    const auto* removed = session.FindDict("removed_workspaces");
    if (!epochs || epochs->empty())
      return false;
    for (const auto [id, epoch] : *epochs) {
      if (!epoch.is_string() ||
          !base::Uuid::ParseLowercase(epoch.GetString()).is_valid() ||
          (!std::ranges::contains(*workspaces, id, &Workspace::id) &&
           (!removed || !removed->contains(id)))) {
        return false;
      }
    }
  }
  if (session.contains("link_rules")) {
    const auto* rules = session.FindDict("link_rules");
    if (!rules || rules->empty())
      return false;
    for (const auto [host, target] : *rules) {
      const GURL url("https://" + std::string(host) + "/");
      if (!url.is_valid() || host.empty() || url.host() != host ||
          url.has_username() || url.has_password() || !target.is_string() ||
          !std::ranges::contains(*workspaces, target.GetString(), &Workspace::id)) {
        return false;
      }
    }
  }
  const auto favorites = ReadContentProfileFavorites(session, *workspaces);
  if (!favorites)
    return false;
  const auto* active = session.FindString("active_workspace");
  if (!active || !std::ranges::contains(*workspaces, *active, &Workspace::id))
    return false;
  for (const auto& workspace : *workspaces) {
    const auto& id = workspace.id;
    const auto& profile_id = workspace.profile_id;
    const auto* entry = session.FindDict(id);
    if (!entry ||
        entry->size() != 3u + entry->contains("split") + entry->contains("pins") +
                             entry->contains("sleeping")) {
      return false;
    }
    if (entry->contains("sleeping")) {
      const auto sleeping = entry->FindBool("sleeping");
      if (!sleeping || (*sleeping && id == *active))
        return false;
    }
    auto tabs = prototype::ReadWorkspaceSessionTabs(*entry, profile_id);
    const auto selected = entry->FindInt("active");
    auto pins = ReadWorkspacePins(*entry);
    if (!tabs || !pins || !selected || *selected < 0 ||
        static_cast<size_t>(*selected) >= tabs->size() ||
        !ReadWorkspaceSplit(*entry, tabs->size()) ||
        !std::ranges::all_of(*tabs, [](const auto& tab) {
          return CanRestoreWorkspaceURL(tab.url) && tab.title;
        })) {
      return false;
    }
    const auto profile_favorites = favorites->find(profile_id);
    if (profile_favorites != favorites->end() &&
        std::ranges::any_of(*pins, [&](const auto& pin) {
          return !pin.profile_pin && std::ranges::contains(
              profile_favorites->second, pin.id, &prototype::PinnedItem::id);
        })) {
      return false;
    }
    std::set<std::string_view> opened;
    const auto& raw_tabs = *entry->FindList("tabs");
    for (size_t i = 0; i < tabs->size(); ++i) {
      const auto& tab = (*tabs)[i];
      if (raw_tabs[i].GetDict().size() != (2u + tab.pin.has_value() + tab.custom_title.has_value()))
        return false;
      if (!tab.pin)
        continue;
      const auto pin = std::ranges::find(*pins, *tab.pin, &prototype::PinnedItem::id);
      const bool shared = profile_favorites != favorites->end() &&
          std::ranges::contains(profile_favorites->second, *tab.pin,
                                &prototype::PinnedItem::id);
      if ((!shared && (pin == pins->end() || pin->folder)) ||
          !opened.insert(*tab.pin).second) {
        return false;
      }
    }
  }
  return true;
}

}  // namespace

bool IsContentWorkspaceName(std::string_view name) {
  return !name.empty() && name.size() <= 256 && base::IsStringUTF8(name) &&
         name.find_first_of("\r\n\t") == std::string_view::npos &&
         name.find('\0') == std::string_view::npos;
}

std::optional<std::vector<Workspace>> ReadContentWorkspaces(
    const base::DictValue& session) {
  const auto profiles = ReadContentProfiles(session);
  const auto names = ReadBuiltinNames(session, "workspace_names");
  if (!profiles || !names)
    return std::nullopt;
  const auto* removed = session.FindDict("removed_workspaces");
  if (session.contains("removed_workspaces") && (!removed || removed->empty()))
    return std::nullopt;
  if (removed) {
    for (const auto [id, profile] : *removed) {
      const bool builtin = std::ranges::contains(kWorkspaceIds, id);
      if ((!builtin && (!id.starts_with("space-") ||
                        !base::Uuid::ParseLowercase(id.substr(6)).is_valid())) ||
          session.contains(id) || !profile.is_string() ||
          !std::ranges::contains(*profiles, profile.GetString(),
                                 &ContentProfileInfo::id)) {
        return std::nullopt;
      }
    }
  }
  const auto* epochs = session.FindDict("timeline_epochs");
  std::vector<Workspace> workspaces;
  for (size_t i = 0; i < kWorkspaceIds.size(); ++i) {
    if (removed && removed->contains(kWorkspaceIds[i])) {
      if (const auto* overrides = session.FindDict("workspace_names");
          overrides && overrides->contains(kWorkspaceIds[i])) {
        return std::nullopt;
      }
      continue;
    }
    const auto id = kWorkspaceIds[i];
    const auto* entry = session.FindDict(id);
    const auto* profile = entry ? entry->FindString("profile_id") : nullptr;
    if (entry &&
        (!profile || !std::ranges::contains(*profiles, *profile, &ContentProfileInfo::id) ||
         (*profile != id && (!epochs || !epochs->contains(id))))) {
      return std::nullopt;
    }
    workspaces.push_back({std::string(id), profile ? *profile : std::string(id),
                          (*names)[i]});
  }
  if (!session.contains("extra_workspaces"))
    return workspaces;
  const auto* extras = session.FindList("extra_workspaces");
  if (!extras || extras->empty() || extras->size() > kMaxContentWorkspaces - workspaces.size())
    return std::nullopt;
  std::set<std::string_view> ids;
  std::set<std::string_view> sources;
  for (const auto& value : *extras) {
    const auto* metadata = value.GetIfDict();
    const auto* id = metadata ? metadata->FindString("id") : nullptr;
    const auto* name = metadata ? metadata->FindString("name") : nullptr;
    const auto* source = metadata ? metadata->FindString("import_source") : nullptr;
    const auto* entry = id ? session.FindDict(*id) : nullptr;
    const auto* profile = entry ? entry->FindString("profile_id") : nullptr;
    if (!id || !id->starts_with("space-") ||
        !base::Uuid::ParseLowercase(std::string_view(*id).substr(6)).is_valid() ||
        (removed && removed->contains(*id)) || !ids.insert(*id).second || !name || !IsContentWorkspaceName(*name) ||
        metadata->size() != 2u + (source != nullptr) ||
        (source && (!IsContentWorkspaceName(*source) ||
                    !sources.insert(*source).second)) || !profile ||
        !std::ranges::contains(*profiles, *profile, &ContentProfileInfo::id)) {
      return std::nullopt;
    }
    workspaces.push_back({*id, *profile, *name});
  }
  return workspaces;
}

std::optional<std::vector<ContentProfileInfo>> ReadContentProfiles(
    const base::DictValue& session) {
  const auto names = ReadBuiltinNames(session, "profile_names");
  if (!names)
    return std::nullopt;
  std::vector<ContentProfileInfo> profiles;
  for (size_t i = 0; i < kWorkspaceIds.size(); ++i) {
    profiles.push_back({std::string(kWorkspaceIds[i]),
                        (*names)[i]});
  }
  if (!session.contains("extra_profiles"))
    return profiles;
  const auto* extras = session.FindList("extra_profiles");
  if (!extras || extras->empty() ||
      extras->size() > kMaxContentProfiles - profiles.size()) {
    return std::nullopt;
  }
  std::set<std::string_view> ids;
  std::set<std::string_view> sources;
  for (const auto& value : *extras) {
    const auto* entry = value.GetIfDict();
    const auto* id = entry ? entry->FindString("id") : nullptr;
    const auto* name = entry ? entry->FindString("name") : nullptr;
    const auto* source = entry ? entry->FindString("import_source") : nullptr;
    const auto separate_network = entry ? entry->FindBool("separate_network")
                                        : std::nullopt;
    if (!id || !id->starts_with("profile-") ||
        !base::Uuid::ParseLowercase(std::string_view(*id).substr(8)).is_valid() ||
        !ids.insert(*id).second || !name || !IsContentWorkspaceName(*name) ||
        entry->size() != 2u + separate_network.has_value() + (source != nullptr) ||
        (source && (!IsContentWorkspaceName(*source) ||
                    !sources.insert(*source).second))) {
      return std::nullopt;
    }
    profiles.push_back({*id, *name, separate_network.value_or(false),
                        source ? *source : std::string()});
  }
  return profiles;
}

std::optional<WorkspaceSplit> ReadWorkspaceSplit(const base::DictValue& entry,
                                                 size_t tab_count) {
  WorkspaceSplit result;
  if (!entry.contains("split"))
    return result;
  const auto* split = entry.FindDict("split");
  if (!split || split->size() != 3)
    return std::nullopt;
  const auto* tabs = split->FindList("tabs");
  const auto* weights = split->FindList("weights");
  const auto vertical = split->FindBool("vertical");
  if (!tabs || !weights || !vertical || tabs->size() < 2 ||
      tabs->size() > prototype::kMaxSplitPanes || weights->size() != tabs->size()) {
    return std::nullopt;
  }
  for (const auto& tab : *tabs) {
    if (!tab.is_int() || tab.GetInt() < 0 ||
        static_cast<size_t>(tab.GetInt()) >= tab_count ||
        std::ranges::contains(result.tabs, static_cast<size_t>(tab.GetInt()))) {
      return std::nullopt;
    }
    result.tabs.push_back(tab.GetInt());
  }
  double total = 0;
  for (const auto& weight : *weights) {
    if ((!weight.is_double() && !weight.is_int()) ||
        !std::isfinite(weight.GetDouble()) || weight.GetDouble() <= 0) {
      return std::nullopt;
    }
    result.weights.push_back(weight.GetDouble());
    total += weight.GetDouble();
  }
  if (!std::isfinite(total))
    return std::nullopt;
  result.vertical = *vertical;
  return result;
}

namespace {

std::optional<std::vector<prototype::PinnedItem>> ReadPins(
    const base::ListValue& list) {
  std::vector<prototype::PinnedItem> pins;
  if (list.size() > prototype::kMaxWorkspaceSessionTabs)
    return std::nullopt;
  std::set<std::string_view> seen;
  std::set<std::string_view> folders;
  for (const auto& value : list) {
    const auto* pin = value.GetIfDict();
    if (!pin || pin->size() != 6u + pin->contains("github") + pin->contains("custom_title"))
      return std::nullopt;
    const auto* id = pin->FindString("id");
    const auto* title = pin->FindString("title");
    const auto* url = pin->FindString("url");
    const auto* parent = pin->FindString("parent_id");
    const auto folder = pin->FindBool("folder");
    const auto profile_pin = pin->FindBool("profile_pin");
    if (!id || id->empty() || id->size() > 256 ||
        !std::ranges::all_of(*id, [](char c) { return c >= '!' && c <= '~'; }) ||
        !seen.insert(*id).second || !title || title->size() > 32768 || !url ||
        url->size() > 32768 || !parent ||
        (!parent->empty() && !folders.contains(*parent)) ||
        !folder || !profile_pin || (*profile_pin && (*folder || !parent->empty()))) {
      return std::nullopt;
    }
    const GURL target(*url);
    if (*folder ? !url->empty() : url->empty() || !CanRestoreWorkspaceURL(target))
      return std::nullopt;
    if (*folder)
      folders.insert(*id);
    prototype::PinnedItem item{*id, *parent, *title, target, *folder, *profile_pin};
    if (pin->contains("custom_title")) {
      const auto* custom = pin->FindString("custom_title");
      if (!custom || custom->size() > 1024 || *folder)
        return std::nullopt;
      item.custom_title = *custom;
    }
    if (pin->contains("github")) {
      const auto* github = pin->FindDict("github");
      if (!*folder || !github || github->size() != 3)
        return std::nullopt;
      const auto* login = github->FindString("login");
      const auto* repository = github->FindString("repository");
      const auto* token = github->FindString("encrypted_token");
      std::string ciphertext;
      if (!login || login->size() > 39 || !repository ||
          repository->size() > 140 || !token || token->size() > 4096 ||
          (!token->empty() && !base::Base64Decode(*token, &ciphertext))) {
        return std::nullopt;
      }
      item.live_folder = prototype::LiveFolderConfig{*login, *repository, *token};
    }
    pins.push_back(std::move(item));
  }
  return pins;
}

}  // namespace

std::optional<std::vector<prototype::PinnedItem>> ReadWorkspacePins(
    const base::DictValue& entry) {
  if (!entry.contains("pins"))
    return std::vector<prototype::PinnedItem>();
  const auto* list = entry.FindList("pins");
  return list ? ReadPins(*list) : std::nullopt;
}

std::optional<ProfileFavorites> ReadContentProfileFavorites(
    const base::DictValue& session, std::span<const Workspace> workspaces) {
  ProfileFavorites favorites;
  const auto* shared = session.FindDict("profile_favorites");
  if (session.contains("profile_favorites")) {
    const auto profiles = ReadContentProfiles(session);
    if (!profiles || !shared || shared->empty())
      return std::nullopt;
    for (const auto [id, value] : *shared) {
      const auto* list = value.GetIfList();
      auto pins = list ? ReadPins(*list) : std::nullopt;
      if (!pins || pins->empty() ||
          !std::ranges::contains(*profiles, id, &ContentProfileInfo::id) ||
          !std::ranges::all_of(*pins, &prototype::PinnedItem::profile_pin)) {
        return std::nullopt;
      }
      favorites.emplace(id, std::move(*pins));
    }
  }

  // Older sessions stored profile favorites among each workspace's pins.
  for (const auto& workspace : workspaces) {
    const auto* entry = session.FindDict(workspace.id);
    if (!entry)
      continue;
    auto pins = ReadWorkspacePins(*entry);
    if (!pins)
      return std::nullopt;
    for (auto& pin : *pins) {
      if (!pin.profile_pin)
        continue;
      if (shared && shared->contains(workspace.profile_id))
        return std::nullopt;
      auto& profile = favorites[workspace.profile_id];
      const auto existing = std::ranges::find(profile, pin.id, &prototype::PinnedItem::id);
      if (existing != profile.end()) {
        if (*existing != pin)
          return std::nullopt;
      } else {
        if (profile.size() >= prototype::kMaxWorkspaceSessionTabs)
          return std::nullopt;
        profile.push_back(std::move(pin));
      }
    }
  }
  return favorites;
}

base::ListValue WriteWorkspacePins(const std::vector<prototype::PinnedItem>& pins) {
  base::ListValue result;
  for (const auto& pin : pins) {
    auto item = base::DictValue()
                    .Set("id", pin.id)
                    .Set("parent_id", pin.parent_id)
                    .Set("title", pin.title)
                    .Set("url", pin.url.spec())
                    .Set("folder", pin.folder)
                    .Set("profile_pin", pin.profile_pin);
    if (!pin.custom_title.empty())
      item.Set("custom_title", pin.custom_title);
    if (pin.live_folder) {
      item.Set("github", base::DictValue()
                             .Set("login", pin.live_folder->account)
                             .Set("repository", pin.live_folder->filter)
                             .Set("encrypted_token", pin.live_folder->encrypted_token));
    }
    result.Append(std::move(item));
  }
  return result;
}

bool CanRestoreWorkspaceURL(const GURL& url) {
  return url.is_empty() || prototype::IsRssTabUrl(url) ||
         url == GURL(prototype::kExtensionsTabUrl) ||
         (url.is_valid() && url.SchemeIsHTTPOrHTTPS() &&
          !url.has_username() && !url.has_password());
}

bool ValidateContentWorkspaceSession(const base::DictValue& session) {
  return IsValidSession(session);
}

bool WriteContentWorkspaceSession(const base::FilePath& path,
                                  const base::DictValue& session) {
  if (!IsValidSession(session))
    return false;
  const auto json = base::WriteJson(session);
  return json && json->size() <= kMaxSessionBytes && !base::IsLink(path) &&
         base::ImportantFileWriter::WriteFileAtomically(path, *json);
}

bool IsPreparedContentWorkspace(const base::FilePath& root,
                                std::string_view layout) {
  base::FilePath normalized;
  if (!root.IsAbsolute() || root.ReferencesParent() || base::IsLink(root) ||
      !base::NormalizeFilePath(root, &normalized) ||
      normalized != root.NormalizePathSeparators() ||
      !base::DirectoryExists(normalized)) {
    return false;
  }
  for (auto directory = normalized;; directory = directory.DirName()) {
    for (const char* name : {"ZitterWorkspaces.json", "Local State"}) {
      const auto path = directory.AppendASCII(name);
      if (base::PathExists(path) || base::IsLink(path))
        return false;
    }
    if (directory.DirName() == directory)
      break;
  }
  const auto marker = root.AppendASCII("ContentWorkspaceLayout");
  const std::string expected =
      "zitter-content-workspace-v1 " + std::string(layout) + "\n";
  base::File::Info info;
  std::string value;
  return !base::IsLink(marker) && base::GetFileInfo(marker, &info) &&
         !info.is_directory &&
         info.size == static_cast<int64_t>(expected.size()) &&
         base::ReadFileToStringWithMaxSize(marker, &value, expected.size()) &&
         value == expected;
}

std::optional<base::DictValue> ReadContentWorkspaceSession(
    const base::FilePath& path) {
  if (base::IsLink(path))
    return std::nullopt;
  if (!base::PathExists(path))
    return base::DictValue();
  std::string json;
  if (!base::ReadFileToStringWithMaxSize(path, &json, kMaxSessionBytes))
    return std::nullopt;
  auto session = base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  if (!session || !IsValidSession(*session))
    return std::nullopt;
  return session;
}

}  // namespace zitter::content_embedder
