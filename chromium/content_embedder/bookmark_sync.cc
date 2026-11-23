// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "bookmark_sync.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/uuid.h"
#include "workspace_session_store.h"
#include "chromium/prototype/workspace_session.h"
#include "components/crx_file/id_util.h"
#include "crypto/hash.h"

namespace zitter::content_embedder::bookmark_sync {
bool CanSyncTabUrl(const GURL& url) {
  return url.is_valid() && url.SchemeIsHTTPOrHTTPS() &&
         !url.has_username() && !url.has_password() &&
         url.spec().size() <= 8192;
}

namespace {
constexpr size_t kMaxLegacyBytes = 16 * 1024 * 1024;
constexpr size_t kMaxIndexBytes = 256 * 1024;
constexpr size_t kMaxScopes = 256;

bool IsDigest(std::string_view value) {
  return value.size() == 64 && std::ranges::all_of(value, [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  });
}

std::string Digest(std::string_view value) {
  return base::HexEncodeLower(crypto::hash::Sha256(value));
}

base::DictValue Index(const base::ListValue& items, std::string_view kind) {
  base::DictValue result;
  int position = 0;
  for (const auto& item : items) {
    result.Set(*item.GetDict().FindString("id"),
               base::DictValue().Set(kind, item.Clone()).Set("position", position++));
  }
  return result;
}
auto Version(const base::DictValue& entry) {
  return std::tuple(*entry.FindInt("clock"), *entry.FindString("device"));
}

bool ValidVersion(const base::DictValue& entry) {
  const auto clock = entry.FindInt("clock");
  const auto* device = entry.FindString("device");
  return clock && *clock >= 1 && *clock < 2000000000 && device &&
         base::Uuid::ParseLowercase(*device).is_valid();
}

int NextClock(const base::DictValue& ledger) {
  int clock = 0;
  for (const auto [scope, records] : ledger)
    for (const auto [id, value] : records.GetDict())
      clock = std::max(clock, *value.GetDict().FindInt("clock"));
  return clock + 1;
}

base::DictValue Stamp(int clock, const std::string& device) {
  return base::DictValue().Set("clock", clock).Set("device", device);
}

constexpr char kDeletedScope[] = "@deleted";
constexpr std::string_view kScopeFields[] = {
    "name", "profile", "position", "color", "emoji", "extensions"};

const base::DictValue& FieldVersion(const base::DictValue& record,
                                   std::string_view field) {
  const auto* versions = record.FindDict("versions");
  const auto* stamp = versions ? versions->FindDict(field) : nullptr;
  return stamp ? *stamp : record;
}

void SetField(base::DictValue& record, std::string_view field,
              const base::Value& value, const base::DictValue& stamp) {
  record.EnsureDict("scope")->Set(field, value.Clone());
  record.EnsureDict("versions")->Set(
      field, Stamp(*stamp.FindInt("clock"), *stamp.FindString("device")));
}

base::DictValue MergeScope(const base::DictValue& first,
                           const base::DictValue& second) {
  const auto& newer = Version(first) >= Version(second) ? first : second;
  auto result = newer.Clone();
  for (const auto field : kScopeFields) {
    const auto* left = first.FindDict("scope")->Find(field);
    const auto* right = second.FindDict("scope")->Find(field);
    if (!left && !right) continue;
    const auto& left_stamp = FieldVersion(first, field);
    const auto& right_stamp = FieldVersion(second, field);
    if (left && (!right || Version(left_stamp) >= Version(right_stamp)))
      SetField(result, field, *left, left_stamp);
    else
      SetField(result, field, *right, right_stamp);
  }
  return result;
}

std::string SpaceScopeForTabs(std::string_view key) {
  return base::StrCat({"space/", key.substr(IsTabScope(key) ? 6 : 5)});
}

bool SuppressedScope(const base::DictValue& ledger, std::string_view key) {
  const auto space_key = (IsTabScope(key) || key.starts_with("tabs/"))
      ? SpaceScopeForTabs(key) : std::string(key);
  if (ScopeDeleted(ledger, space_key)) return true;
  if (!space_key.starts_with("space/")) return false;
  const auto* space = ledger.FindDict(space_key);
  const auto* record = space ? space->FindDict("") : nullptr;
  const auto* scope = record ? record->FindDict("scope") : nullptr;
  const auto* profile = scope ? scope->FindString("profile") : nullptr;
  return profile && ScopeDeleted(ledger, "profile/" + *profile);
}

std::vector<std::string> SharedOrder(const base::ListValue& list,
                                     const base::DictValue& other) {
  std::vector<std::string> order;
  for (const auto& item : list) {
    const auto& id = *item.GetDict().FindString("id");
    if (other.contains(id)) order.push_back(id);
  }
  return order;
}

// Keep anchors when a tab is inserted or closed. Only an explicit reorder
// advances the positions of existing tabs, so shifting indices cannot revive
// a tab deleted concurrently on another device.
bool AnchorTabPositions(const base::ListValue& previous,
                        const base::ListValue& current,
                        const base::DictValue& prior, base::DictValue& next,
                        const base::DictValue* records) {
  if (SharedOrder(previous, next) != SharedOrder(current, prior))
    return false;
  std::vector<base::DictValue*> ordered;
  std::vector<std::optional<double>> anchors;
  double last = -std::numeric_limits<double>::infinity();
  for (const auto& value : current) {
    const auto& id = *value.GetDict().FindString("id");
    ordered.push_back(next.FindDict(id));
    const auto* record = records && prior.contains(id) ? records->FindDict(id) : nullptr;
    const auto position = record && record->contains("tab") ?
        record->FindDouble("position") : std::nullopt;
    if (position) {
      if (*position < last) return false;
      last = *position;
    }
    anchors.push_back(position);
  }
  for (size_t begin = 0; begin < anchors.size();) {
    if (anchors[begin]) {
      ordered[begin]->Set("position", *anchors[begin]);
      ++begin;
      continue;
    }
    size_t end = begin;
    while (end < anchors.size() && !anchors[end]) ++end;
    const auto count = static_cast<double>(end - begin + 1);
    const double left = begin > 0 ? *anchors[begin - 1] :
        end < anchors.size() ? *anchors[end] - count : -1;
    const double right = end < anchors.size() ? *anchors[end] : left + count;
    double previous_position = left;
    for (size_t i = begin; i < end; ++i) {
      const double position = left + (right - left) * (i - begin + 1) / count;
      if (position <= previous_position || position >= right) return false;
      ordered[i]->Set("position", position);
      previous_position = position;
    }
    begin = end;
  }
  return true;
}

bool ValidTab(std::string_view id, const base::DictValue& tab) {
  const auto* key = tab.FindString("id");
  const auto* url = tab.FindString("url");
  const auto* title = tab.FindString("title");
  return key && *key == id && ValidTabId(id) && url && CanSyncTabUrl(GURL(*url)) &&
      (!tab.contains("title") || (title && title->size() <= 8192)) &&
      tab.size() == (title ? 3u : 2u);
}
}

bool ValidTabId(std::string_view id) {
  return IsDigest(id) || base::Uuid::ParseLowercase(id).is_valid();
}

std::string LegacyTabId(std::string_view workspace, std::string_view url, size_t occurrence) {
  return Digest(base::StrCat({workspace, "\n", url, "\n", base::NumberToString(occurrence)}));
}

bool IsTabScope(std::string_view scope) {
  return scope.starts_with("tabs2/");
}

bool ValidSnapshot(std::string_view scope, const base::ListValue& items) {
  if (!IsTabScope(scope))
    return !!ReadWorkspacePins(base::DictValue().Set("pins", items.Clone()));
  if (items.size() > prototype::kMaxWorkspaceSessionTabs) return false;
  std::set<std::string> seen;
  for (const auto& item : items) {
    const auto* tab = item.GetIfDict();
    const auto* id = tab ? tab->FindString("id") : nullptr;
    if (!id || !ValidTab(*id, *tab) || !seen.insert(*id).second) return false;
  }
  return true;
}

void UpgradeLegacyTabs(base::DictValue& ledger) {
  std::vector<std::string> legacy;
  for (const auto [key, records] : ledger)
    if (key.starts_with("tabs/")) legacy.emplace_back(key);
  for (const auto& key : legacy) {
    auto records = ledger.Extract(key);
    const auto* meta = records->GetDict().FindDict("");
    if (!meta) continue;
    const auto* urls = meta->FindDict("scope")->FindList("tabs");
    const std::string workspace = key.substr(5);
    const auto current = "tabs2/" + workspace;
    if (!urls || ledger.contains(current)) continue;
    auto* dest = ledger.EnsureDict(current);
    std::map<std::string, size_t> occurrences;
    int position = 0;
    for (const auto& value : *urls) {
      const auto& url = value.GetString();
      const auto id = LegacyTabId(workspace, url, occurrences[url]++);
      dest->Set(id, base::DictValue().Set("clock", *meta->FindInt("clock"))
          .Set("device", *meta->FindString("device")).Set("position", position++)
          .Set("tab", base::DictValue().Set("id", id).Set("url", url)));
    }
  }
}

base::DictValue RecordEdits(const base::DictValue& previous,
                            const base::DictValue& current,
                            const base::DictValue& ledger,
                            const std::string& device) {
  auto result = ledger.Clone();
  const int clock = NextClock(ledger);
  for (const auto [scope, items] : current) {
    if (SuppressedScope(ledger, scope)) continue;
    const bool tabs = IsTabScope(scope);
    const std::string_view kind = tabs ? "tab" : "pin";
    const auto* previous_items = previous.FindList(scope);
    auto prior = previous_items ? Index(*previous_items, kind) : base::DictValue();
    auto now = Index(items.GetList(), kind);
    bool anchored = false;
    if (tabs && previous_items) {
      anchored = AnchorTabPositions(*previous_items, items.GetList(), prior, now, ledger.FindDict(scope));
      if (!anchored) now = Index(items.GetList(), kind);
    }
    // Closing a pin only shifts the remaining indices. It is not a reorder.
    const bool removed_pins = !tabs && previous_items && now.size() < prior.size() &&
        SharedOrder(*previous_items, now) == SharedOrder(items.GetList(), prior) &&
        std::ranges::all_of(now, [&](const auto& item) { return prior.contains(item.first); });
    auto put = [&](std::string_view id, base::DictValue value) {
      value.Set("clock", clock);
      value.Set("device", device);
      result.EnsureDict(scope)->Set(id, std::move(value));
    };
    for (const auto [id, entry] : now) {
      const auto* old = prior.Find(id);
      if (old && (anchored || removed_pins
                      ? *old->GetDict().Find(kind) == *entry.GetDict().Find(kind)
                      : *old == entry)) continue;
      auto changed = entry.GetDict().Clone();
      if (removed_pins && old) {
        const auto* saved = ledger.FindDict(scope);
        const auto* record = saved ? saved->FindDict(id) : nullptr;
        if (const auto position = record ? record->FindInt("position") : std::nullopt)
          changed.Set("position", *position);
      }
      put(id, std::move(changed));
    }
    for (const auto [id, entry] : prior)
      if (!now.contains(id)) put(id, {});
    if (tabs) result.EnsureDict(scope);
  }
  return result;
}

bool ScopeDeleted(const base::DictValue& ledger, std::string_view key) {
  const auto* records = ledger.FindDict(key);
  return records && records->contains(kDeletedScope);
}

void RecordScopeDeletion(base::DictValue& ledger, std::string_view key,
                         const std::string& device) {
  if (!ScopeDeleted(ledger, key))
    ledger.EnsureDict(key)->Set(kDeletedScope, Stamp(NextClock(ledger), device));
}

void RecordScopeEdits(const base::DictValue& previous,
                      const base::DictValue& current,
                      base::DictValue& ledger,
                      const std::string& device) {
  const auto stamp = Stamp(NextClock(ledger), device);
  for (const auto [key, value] : current) {
    const auto* owner = value.GetDict().FindString("profile");
    if (ScopeDeleted(ledger, key) ||
        (key.starts_with("space/") && owner && ScopeDeleted(ledger, "profile/" + *owner)))
      continue;
    auto* group = ledger.EnsureDict(key);
    const auto* record = group->FindDict("");
    const auto* before = previous.FindDict(key);
    if (!record) {
      group->Set("", stamp.Clone().Set("scope", value.Clone()));
      continue;
    }
    const auto& remote = *record->FindDict("scope");
    auto updated = record->Clone();
    for (const auto field : kScopeFields) {
      const auto* local = value.GetDict().Find(field);
      const auto* old = before ? before->Find(field) : nullptr;
      const auto* theirs = remote.Find(field);
      if (!local || (theirs && (*theirs == *local || !old || *old == *local)))
        continue;
      SetField(updated, field, *local, stamp);
    }
    if (updated != *record) {
      // Keep the original fallback stamp for untouched legacy fields.
      for (const auto field : kScopeFields) {
        const auto* original = remote.Find(field);
        if (original && !updated.FindDict("versions")->contains(field))
          SetField(updated, field, *original, *record);
      }
      updated.Set("clock", *stamp.FindInt("clock"));
      updated.Set("device", device);
      group->Set("", std::move(updated));
    }
  }
}

bool Merge(base::DictValue& target, const base::DictValue& source) {
  if (source.size() > kMaxScopes) return false;
  for (const auto [scope, records] : source) {
    if (scope.size() > 1024 || !records.is_dict() || records.GetDict().size() > 20000)
      return false;
    auto* dest = target.EnsureDict(scope);
    if (target.size() > kMaxScopes) return false;
    for (const auto [id, value] : records.GetDict()) {
      if (!value.is_dict() || id.size() > 256) return false;
      const auto& entry = value.GetDict();
      if (!ValidVersion(entry)) return false;
      if (id == kDeletedScope && (IsTabScope(scope) || entry.size() != 2))
        return false;
      if (IsTabScope(scope)) {
        if (!ValidTabId(id)) return false;
        const auto* tab = entry.FindDict("tab");
        const auto position = entry.FindDouble("position");
        if (entry.contains("tab")
            ? !tab || !ValidTab(id, *tab) || !position ||
                !std::isfinite(*position) || std::abs(*position) > 1e12 || entry.size() != 4
            : entry.size() != 2)
          return false;
      } else if (entry.contains("tab")) {
        return false;
      }
      if (id.empty()) {
        const auto* scope_info = entry.FindDict("scope");
        if (!scope_info || entry.contains("pin") || !scope_info->FindString("profile") ||
            !scope_info->FindString("name") || !IsContentWorkspaceName(*scope_info->FindString("name")))
          return false;
        if (entry.contains("versions")) {
          const auto* versions = entry.FindDict("versions");
          if (!versions || scope.starts_with("tabs/")) return false;
          for (const auto [field, stamp] : *versions) {
            if (!std::ranges::contains(kScopeFields, field) || !scope_info->contains(field) ||
                !stamp.is_dict() || stamp.GetDict().size() != 2 ||
                !ValidVersion(stamp.GetDict()) ||
                Version(stamp.GetDict()) > Version(entry)) {
              return false;
            }
          }
        }
        if (scope_info->contains("position")) {
          const int position = scope_info->FindInt("position").value_or(-1);
          const int color = scope_info->FindInt("color").value_or(-1);
          const auto* emoji = scope_info->FindString("emoji");
          if (!scope.starts_with("space/") || position < 0 ||
              position >= static_cast<int>(kMaxContentWorkspaces) || color < 0 ||
              color > 0xffffff || !emoji || emoji->size() > 128)
            return false;
        }
        if (scope_info->contains("extensions")) {
          const auto* extensions = scope_info->FindList("extensions");
          if (!scope.starts_with("profile/") ||
              scope.substr(8) != *scope_info->FindString("profile") || !extensions ||
              extensions->size() > 128)
            return false;
          std::set<std::string> seen;
          for (const auto& extension_id : *extensions) {
            const auto* key = extension_id.GetIfString();
            if (!key || !crx_file::id_util::IdIsValid(*key) || !seen.insert(*key).second)
              return false;
          }
        }
        if (scope_info->contains("tabs")) {
          const auto* tabs = scope_info->FindList("tabs");
          if (!scope.starts_with("tabs/") || !tabs ||
              tabs->size() > prototype::kMaxWorkspaceSessionTabs)
            return false;
          for (const auto& tab : *tabs) {
            if (!tab.is_string() || !CanSyncTabUrl(GURL(tab.GetString())))
              return false;
          }
        }
      }
      if (entry.contains("pin")) {
        const auto* pin = entry.FindDict("pin");
        if (!pin || pin->contains("github") || !pin->FindString("id") ||
            *pin->FindString("id") != id || !pin->FindString("parent_id") ||
            !entry.FindInt("position")) return false;
      }
      const auto* existing = dest->FindDict(id);
      if (existing && id.empty() &&
          (entry.contains("versions") || existing->contains("versions"))) {
        dest->Set(id, MergeScope(*existing, entry));
        continue;
      }
      auto item_version = [](const base::DictValue& record) {
        return std::tuple(*record.FindInt("clock"), record.size() == 2,
                          *record.FindString("device"));
      };
      if (!existing || item_version(entry) > item_version(*existing))
        dest->Set(id, entry.Clone());
    }
  }
  return true;
}

std::optional<base::DictValue> Materialize(const base::DictValue& ledger) {
  base::DictValue result;
  for (const auto [scope, records] : ledger) {
    if (SuppressedScope(ledger, scope)) continue;
    if (IsTabScope(scope)) {
      std::vector<std::tuple<double, std::string_view, const base::DictValue*>> ordered;
      for (const auto [id, entry] : records.GetDict()) {
        if (const auto* tab = entry.GetDict().FindDict("tab"))
          ordered.emplace_back(*entry.GetDict().FindDouble("position"), id, tab);
      }
      std::ranges::sort(ordered);
      if (ordered.size() > prototype::kMaxWorkspaceSessionTabs)
        ordered.resize(prototype::kMaxWorkspaceSessionTabs);
      auto* list = result.EnsureList(scope);
      for (const auto& [position, id, tab] : ordered) list->Append(tab->Clone());
      continue;
    }
    std::vector<std::pair<int, const base::DictValue*>> ordered;
    for (const auto [id, entry] : records.GetDict()) {
      if (const auto* pin = entry.GetDict().FindDict("pin"))
        ordered.emplace_back(*entry.GetDict().FindInt("position"), pin);
    }
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    if (ordered.size() > prototype::kMaxWorkspaceSessionTabs)
      ordered.resize(prototype::kMaxWorkspaceSessionTabs);
    std::map<std::string, const base::DictValue*> items;
    for (const auto& [position, pin] : ordered) items.emplace(*pin->FindString("id"), pin);
    base::ListValue list;
    std::set<std::string> emitted, visiting;
    const auto emit = [&](auto&& self, const base::DictValue& pin) -> void {
      const auto& id = *pin.FindString("id");
      if (emitted.contains(id)) return;
      visiting.insert(id);
      auto item = pin.Clone();
      const auto& parent = *pin.FindString("parent_id");
      const auto found = items.find(parent);
      if (found != items.end() && found->second->FindBool("folder").value_or(false) &&
          !visiting.contains(parent)) self(self, *found->second);
      else item.Set("parent_id", "");
      visiting.erase(id);
      emitted.insert(id);
      list.Append(std::move(item));
    };
    for (const auto& [position, pin] : ordered) emit(emit, *pin);
    if (!ReadWorkspacePins(base::DictValue().Set("pins", list.Clone()))) return std::nullopt;
    result.Set(scope, std::move(list));
  }
  return result;
}

namespace {
std::string ShardKey(std::string_view scope) {
  if (IsTabScope(scope)) return base::StrCat({"space/", scope.substr(6)});
  if (scope.starts_with("tabs/")) return base::StrCat({"space/", scope.substr(5)});
  return std::string(scope);
}

bool ValidShardKey(std::string_view key) {
  const bool space = key.starts_with("space/");
  if (!space && !key.starts_with("profile/")) return false;
  auto id = key.substr(space ? 6 : 8);
  if (id == "work" || id == "personal") return true;
  const std::string_view prefix = space ? "space-" : "profile-";
  return id.starts_with(prefix) && base::Uuid::ParseLowercase(id.substr(prefix.size())).is_valid();
}
}

std::optional<ShardSet> WriteShards(const base::DictValue& ledger) {
  std::map<std::string, base::DictValue> groups;
  for (const auto [scope, value] : ledger) {
    auto key = ShardKey(scope);
    if (!ValidShardKey(key)) return std::nullopt;
    if (ScopeDeleted(ledger, key)) {
      if (scope == key)
        groups[key].Set(scope, base::DictValue().Set(
            kDeletedScope, value.GetDict().Find(kDeletedScope)->Clone()));
      continue;
    }
    groups[key].Set(scope, value.Clone());
  }
  ShardSet result;
  for (const auto& [key, group] : groups) {
    auto json = base::WriteJson(group);
    if (!json || json->size() > kMaxShardBytes) return std::nullopt;
    result.index.Set(key, Digest(*json));
    result.blobs.emplace(key, std::move(*json));
  }
  return result;
}

std::optional<base::DictValue> ReadIndex(std::string_view json) {
  if (json.size() > kMaxIndexBytes) return std::nullopt;
  auto value = base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  if (!value || value->FindInt("format") != 2 || value->size() != 2)
    return std::nullopt;
  auto shards = value->Extract("shards");
  if (!shards || !shards->is_dict() || shards->GetDict().size() > kMaxScopes)
    return std::nullopt;
  for (const auto [key, digest] : shards->GetDict()) {
    if (!ValidShardKey(key) || !digest.is_string() || !IsDigest(digest.GetString()))
      return std::nullopt;
  }
  return std::move(*shards).TakeDict();
}

std::optional<base::DictValue> ReadShard(std::string_view key, std::string_view digest,
                                        std::string_view json) {
  if (json.size() > kMaxShardBytes || Digest(json) != digest) return std::nullopt;
  auto shard = base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  if (!shard || !MatchesShardKey(key, *shard)) return std::nullopt;
  base::DictValue validated;
  if (!Merge(validated, *shard)) return std::nullopt;
  return validated;
}

bool MatchesShardKey(std::string_view key, const base::DictValue& shard) {
  return !shard.empty() && std::ranges::all_of(shard, [&](const auto& entry) {
    return ShardKey(entry.first) == key;
  });
}

base::DictValue CacheShards(base::DictValue ledger) {
  UpgradeLegacyTabs(ledger);
  base::DictValue cache;
  auto shards = WriteShards(ledger);
  if (!shards) return cache;
  for (const auto& [key, json] : shards->blobs) {
    const auto& digest = *shards->index.FindString(key);
    if (auto shard = ReadShard(key, digest, json))
      cache.Set(digest, std::move(*shard));
  }
  return cache;
}

std::optional<FolderResult> ExchangeFolder(base::FilePath folder, std::string device,
                                          base::DictValue local, bool read_only,
                                          base::DictValue cache) {
  if (!base::DirectoryExists(folder)) return std::nullopt;
  const auto dir = folder.AppendASCII("Zitter Bookmarks");
  if (base::IsLink(dir) || !base::CreateDirectory(dir)) return std::nullopt;
  const auto shard_dir = dir.AppendASCII("sync-v2");
  if (base::IsLink(shard_dir)) return std::nullopt;
  if (cache.empty() && !local.empty())
    cache = CacheShards(local.Clone());
  base::DictValue next_cache;
  std::set<std::string> migrated;
  base::DictValue previous;
  base::FileEnumerator indexes(shard_dir, false, base::FileEnumerator::FILES,
                               FILE_PATH_LITERAL("index-*.json"));
  for (auto path = indexes.Next(); !path.empty(); path = indexes.Next()) {
    const auto name = path.BaseName().RemoveExtension().AsUTF8Unsafe();
    if (!name.starts_with("index-")) continue;
    const auto peer = name.substr(6);
    if (!base::Uuid::ParseLowercase(peer).is_valid()) continue;
    std::string json;
    if (base::IsLink(path) ||
        !base::ReadFileToStringWithMaxSize(path, &json, kMaxIndexBytes)) return std::nullopt;
    auto index = ReadIndex(json);
    if (!index) return std::nullopt;
    for (const auto [key, value] : *index) {
      const auto& digest = value.GetString();
      const auto* entry = next_cache.FindDict(digest);
      if (!entry) {
        const auto blob = shard_dir.AppendASCII(digest + ".json");
        if (base::IsLink(blob) || !base::PathExists(blob)) return std::nullopt;
        auto cached = cache.Extract(digest);
        if (!cached) {
          if (!base::ReadFileToStringWithMaxSize(blob, &json, kMaxShardBytes)) return std::nullopt;
          auto shard = ReadShard(key, digest, json);
          if (!shard) return std::nullopt;
          cached = base::Value(std::move(*shard));
        }
        entry = &next_cache.Set(digest, std::move(*cached))->GetDict();
      }
      if (!MatchesShardKey(key, *entry) || !Merge(local, *entry)) return std::nullopt;
    }
    migrated.insert(peer);
    if (peer == device) previous = std::move(*index);
  }
  base::FileEnumerator files(dir, false, base::FileEnumerator::FILES, FILE_PATH_LITERAL("*.json"));
  for (auto path = files.Next(); !path.empty(); path = files.Next()) {
    const auto peer = path.BaseName().RemoveExtension().AsUTF8Unsafe();
    if (!base::Uuid::ParseLowercase(peer).is_valid() || migrated.contains(peer)) continue;
    std::string data;
    if (base::IsLink(path) ||
        !base::ReadFileToStringWithMaxSize(path, &data, kMaxLegacyBytes)) return std::nullopt;
    auto records = base::JSONReader::ReadDict(data, base::JSON_PARSE_RFC);
    if (!records || !Merge(local, *records)) return std::nullopt;
  }
  UpgradeLegacyTabs(local);
  if (!Materialize(local)) return std::nullopt;
  if (!read_only) {
    auto plan = WriteShards(local);
    if (!plan || !base::CreateDirectory(shard_dir)) return std::nullopt;
    for (const auto& [key, json] : plan->blobs) {
      const auto& digest = *plan->index.FindString(key);
      if (next_cache.contains(digest)) continue;
      const auto blob = shard_dir.AppendASCII(digest + ".json");
      if (base::IsLink(blob) || !base::ImportantFileWriter::WriteFileAtomically(blob, json))
        return std::nullopt;
      auto verified = ReadShard(key, digest, json);
      if (!verified) return std::nullopt;
      next_cache.Set(digest, std::move(*verified));
    }
    if (plan->index != previous || !migrated.contains(device)) {
      auto json = base::WriteJson(base::DictValue().Set("format", 2)
                                                .Set("shards", std::move(plan->index)));
      const auto own = shard_dir.AppendASCII("index-" + device + ".json");
      if (!json || base::IsLink(own) ||
          !base::ImportantFileWriter::WriteFileAtomically(own, *json)) return std::nullopt;
    }
  }
  return FolderResult{std::move(local), std::move(next_cache)};
}
}
