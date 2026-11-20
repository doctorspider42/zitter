// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "bookmark_sync.h"
#include <algorithm>
#include <map>
#include <set>
#include <tuple>
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/uuid.h"
#include "workspace_session_store.h"
#include "chromium/prototype/workspace_session.h"

namespace zitter::content_embedder::bookmark_sync {
namespace {
constexpr size_t kMaxBytes = 16 * 1024 * 1024;
base::DictValue Index(const base::ListValue& pins) {
  base::DictValue result;
  int position = 0;
  for (const auto& pin : pins) {
    result.Set(*pin.GetDict().FindString("id"),
               base::DictValue().Set("pin", pin.Clone()).Set("position", position++));
  }
  return result;
}
auto Version(const base::DictValue& entry) {
  return std::tuple(*entry.FindInt("clock"), *entry.FindString("device"));
}
}

base::DictValue RecordEdits(const base::DictValue& previous,
                            const base::DictValue& current,
                            const base::DictValue& ledger,
                            const std::string& device) {
  auto result = ledger.Clone();
  int clock = 0;
  for (const auto [scope, records] : ledger)
    for (const auto [id, entry] : records.GetDict())
      clock = std::max(clock, *entry.GetDict().FindInt("clock"));
  for (const auto [scope, pins] : current) {
    auto prior = previous.FindList(scope) ? Index(*previous.FindList(scope)) : base::DictValue();
    auto now = Index(pins.GetList());
    auto put = [&](std::string_view id, base::DictValue value) {
      value.Set("clock", clock + 1);
      value.Set("device", device);
      result.EnsureDict(scope)->Set(id, std::move(value));
    };
    for (const auto [id, entry] : now) {
      const auto* old = prior.Find(id);
      if (!old || *old != entry) put(id, entry.GetDict().Clone());
    }
    for (const auto [id, entry] : prior)
      if (!now.contains(id)) put(id, {});
  }
  return result;
}

bool Merge(base::DictValue& target, const base::DictValue& source) {
  if (source.size() > 256) return false;
  for (const auto [scope, records] : source) {
    if (scope.size() > 1024 || !records.is_dict() || records.GetDict().size() > 20000)
      return false;
    auto* dest = target.EnsureDict(scope);
    for (const auto [id, value] : records.GetDict()) {
      if (!value.is_dict() || id.size() > 256) return false;
      const auto& entry = value.GetDict();
      const auto clock = entry.FindInt("clock");
      const auto* device = entry.FindString("device");
      if (!clock || *clock < 1 || *clock >= 2000000000 || !device ||
          !base::Uuid::ParseLowercase(*device).is_valid()) return false;
      if (id.empty()) {
        const auto* scope_info = entry.FindDict("scope");
        if (!scope_info || entry.contains("pin") || !scope_info->FindString("profile") ||
            !scope_info->FindString("name") || !IsContentWorkspaceName(*scope_info->FindString("name")))
          return false;
      }
      if (entry.contains("pin")) {
        const auto* pin = entry.FindDict("pin");
        if (!pin || pin->contains("github") || !pin->FindString("id") ||
            *pin->FindString("id") != id || !pin->FindString("parent_id") ||
            !entry.FindInt("position")) return false;
      }
      const auto* existing = dest->FindDict(id);
      if (!existing || Version(entry) > Version(*existing))
        dest->Set(id, entry.Clone());
    }
  }
  return true;
}

std::optional<base::DictValue> Materialize(const base::DictValue& ledger) {
  base::DictValue result;
  for (const auto [scope, records] : ledger) {
    std::vector<std::pair<int, const base::DictValue*>> ordered;
    for (const auto [id, entry] : records.GetDict()) {
      if (const auto* pin = entry.GetDict().FindDict("pin"))
        ordered.emplace_back(*entry.GetDict().FindInt("position"), pin);
    }
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    if (ordered.size() > prototype::kMaxWorkspaceSessionTabs) return std::nullopt;
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

std::optional<base::DictValue> ExchangeFolder(base::FilePath folder,
                                             std::string device,
                                             base::DictValue local) {
  if (!base::DirectoryExists(folder)) return std::nullopt;
  auto dir = folder.AppendASCII("Zitter Bookmarks");
  if (base::IsLink(dir) || !base::CreateDirectory(dir)) return std::nullopt;
  auto own = dir.AppendASCII(device + ".json");
  base::FileEnumerator files(dir, false, base::FileEnumerator::FILES, FILE_PATH_LITERAL("*.json"));
  std::string previous;
  for (auto path = files.Next(); !path.empty(); path = files.Next()) {
    if (!base::Uuid::ParseLowercase(path.BaseName().RemoveExtension().AsUTF8Unsafe()).is_valid()) continue;
    std::string data;
    if (base::IsLink(path) || !base::ReadFileToStringWithMaxSize(path, &data, kMaxBytes)) return std::nullopt;
    auto records = base::JSONReader::ReadDict(data, base::JSON_PARSE_RFC);
    if (!records || !Merge(local, *records)) return std::nullopt;
    if (path == own) previous = std::move(data);
  }
  auto json = base::WriteJson(local);
  if (!json || json->size() > kMaxBytes || !Materialize(local) ||
      (*json != previous && !base::ImportantFileWriter::WriteFileAtomically(own, *json)))
    return std::nullopt;
  return local;
}
}
