// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "workspace_timeline.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <utility>

#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_util.h"
#include "base/uuid.h"
#include "chromium/prototype/workspace_session.h"
#include "workspace_session_store.h"

namespace zitter::content_embedder {
namespace {

constexpr size_t kMaxTimelineFileBytes = 64 * 1024 * 1024;

bool LoadLine(sidebar::Timeline& timeline, std::string_view text,
              const std::vector<Workspace>& workspaces,
              const base::DictValue* removed, const base::DictValue* epochs) {
  const auto line = base::JSONReader::ReadDict(text, base::JSON_PARSE_RFC);
  if (!line)
    return false;
  const auto time = line->FindDouble("t");
  const auto* workspace = line->FindString("w");
  if (!time || *time < 0 || *time > 32503680000000.0 ||
      std::floor(*time) != *time || !workspace) {
    return false;
  }
  const auto owner = std::ranges::find(workspaces, *workspace, &Workspace::id);
  const bool deleted = owner == workspaces.end();
  const auto* profile_id = deleted ? (removed ? removed->FindString(*workspace)
                                              : nullptr)
                                   : &owner->profile_id;
  if (!profile_id)
    return false;
  const auto* epoch = line->FindString("e");
  const auto* expected_epoch = epochs ? epochs->FindString(*workspace) : nullptr;
  if (line->contains("e") &&
      (!epoch || !base::Uuid::ParseLowercase(*epoch).is_valid() || !expected_epoch)) {
    return false;
  }
  const bool stale = expected_epoch && (!epoch || *epoch != *expected_epoch);
  const size_t fields = line->size() - (epoch != nullptr);
  const auto milliseconds = static_cast<int64_t>(*time);
  if (const auto* name = line->FindString("m")) {
    return fields == 3 &&
           (deleted || stale || timeline.timeline_mark({milliseconds, *workspace, *name}));
  }
  const auto* profile = line->FindString("p");
  const auto active = line->FindInt("a");
  const auto* tabs = line->FindList("tabs");
  const auto replace = line->FindBool("r");
  if (!profile || (!stale && *profile != *profile_id) || !active || !tabs ||
      tabs->size() > prototype::kMaxWorkspaceSessionTabs ||
      *active < -1 || *active >= static_cast<int>(tabs->size()) ||
      fields != (replace ? 6u : 5u)) {
    return false;
  }
  sidebar::TimelineFrame frame{milliseconds, *workspace, *profile, *active, {}};
  for (const auto& value : *tabs) {
    const auto* pair = value.GetIfList();
    if (!pair || pair->size() != 2 || !(*pair)[0].is_string() ||
        !(*pair)[1].is_string()) {
      return false;
    }
    const auto& spec = (*pair)[0].GetString();
    const auto& title = (*pair)[1].GetString();
    const GURL url(spec);
    if (url.is_empty() || !CanRestoreWorkspaceURL(url) || spec.size() > 32768 ||
        title.size() > 32768) {
      return false;
    }
    frame.tabs.push_back({url.spec(), title});
  }
  return deleted || stale || timeline.timeline_load(frame, replace.value_or(false));
}

bool AppendLine(base::DictValue value, const base::DictValue& epochs,
                std::string& data) {
  if (const auto* epoch = epochs.FindString(*value.FindString("w")))
    value.Set("e", *epoch);
  const auto line = base::WriteJson(value);
  if (!line || line->size() >= kMaxTimelineFileBytes - data.size())
    return false;
  data.append(*line).push_back('\n');
  return true;
}

}  // namespace

base::FilePath WorkspaceTimelinePath(const base::FilePath& session_path) {
  return session_path.ReplaceExtension(FILE_PATH_LITERAL(".timeline.jsonl"));
}

std::unique_ptr<prototype::TimelineStore> ReadWorkspaceTimeline(
    const base::FilePath& path, const base::DictValue& session) {
  if (base::IsLink(path))
    return nullptr;
  auto store = std::make_unique<prototype::TimelineStore>();
  if (!base::PathExists(path))
    return store;
  const auto workspaces = ReadContentWorkspaces(session);
  if (!workspaces)
    return nullptr;
  std::string data;
  if (!base::ReadFileToStringWithMaxSize(path, &data, kMaxTimelineFileBytes))
    return nullptr;
  const auto* removed = session.FindDict("removed_workspaces");
  const auto* epochs = session.FindDict("timeline_epochs");
  std::string_view remaining(data);
  while (!remaining.empty()) {
    const size_t end = remaining.find('\n');
    const auto line = base::TrimWhitespaceASCII(remaining.substr(0, end),
                                                 base::TRIM_ALL);
    if (!line.empty() && !LoadLine(*store->current, line, *workspaces, removed, epochs))
      return nullptr;
    if (end == std::string_view::npos)
      break;
    remaining.remove_prefix(end + 1);
  }
  return store;
}

bool WriteWorkspaceTimeline(const base::FilePath& path,
                            rust::Vec<sidebar::TimelineFrame> frames,
                            rust::Vec<sidebar::TimelineMark> marks,
                            const base::DictValue& epochs) {
  std::string data;
  for (const auto& mark : marks) {
    if (!AppendLine(base::DictValue()
                        .Set("t", static_cast<double>(mark.t_ms))
                        .Set("w", std::string(mark.workspace))
                        .Set("m", std::string(mark.name)),
                    epochs, data)) {
      return false;
    }
  }
  for (const auto& frame : frames) {
    base::ListValue tabs;
    for (const auto& tab : frame.tabs) {
      tabs.Append(base::ListValue()
                      .Append(std::string(tab.url))
                      .Append(std::string(tab.title)));
    }
    if (!AppendLine(base::DictValue()
                        .Set("t", static_cast<double>(frame.t_ms))
                        .Set("w", std::string(frame.workspace))
                        .Set("p", std::string(frame.profile))
                        .Set("a", frame.active)
                        .Set("tabs", std::move(tabs)),
                    epochs, data)) {
      return false;
    }
  }
  return !base::IsLink(path) &&
         base::ImportantFileWriter::WriteFileAtomically(path, data);
}

}  // namespace zitter::content_embedder
