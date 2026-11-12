// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CONTENT_WORKSPACE_SESSION_STORE_H_
#define ZITTER_CONTENT_WORKSPACE_SESSION_STORE_H_

#include <array>
#include <cstddef>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "base/values.h"
#include "chromium/prototype/browser_data.h"
#include "zitter/core/workspace_model.h"

namespace base { class FilePath; }
class GURL;

namespace zitter::content_embedder {

inline constexpr std::array<std::string_view, 2> kWorkspaceIds = {
    "work", "personal"};
inline constexpr std::array<std::string_view, 2> kBuiltinProfileNames = {
    "Work", "Personal"};
inline constexpr size_t kMaxContentWorkspaces = 64;
inline constexpr size_t kMaxContentProfiles = 32;

struct ContentProfileInfo {
  std::string id;
  std::string name;
  bool separate_network = false;
  std::string import_source = {};
};
inline constexpr char kWorkspaceNetworkRoot[] = "NetworkProfiles";

bool IsPreparedContentWorkspace(const base::FilePath& root, std::string_view layout);
bool IsContentWorkspaceName(std::string_view name);
std::optional<std::vector<ContentProfileInfo>> ReadContentProfiles(
    const base::DictValue& session);
std::optional<std::vector<Workspace>> ReadContentWorkspaces(
    const base::DictValue& session);

struct WorkspaceSplit {
  std::vector<size_t> tabs;
  std::vector<double> weights;
  bool vertical = false;
};

std::optional<WorkspaceSplit> ReadWorkspaceSplit(const base::DictValue& entry,
                                                 size_t tab_count);

std::optional<std::vector<prototype::PinnedItem>> ReadWorkspacePins(
    const base::DictValue& entry);
using ProfileFavorites = std::map<std::string, std::vector<prototype::PinnedItem>>;
std::optional<ProfileFavorites> ReadContentProfileFavorites(
    const base::DictValue& session, std::span<const Workspace> workspaces);
base::ListValue WriteWorkspacePins(const std::vector<prototype::PinnedItem>& pins);

bool CanRestoreWorkspaceURL(const GURL& url);
bool ValidateContentWorkspaceSession(const base::DictValue& session);
std::optional<base::DictValue> ReadContentWorkspaceSession(
    const base::FilePath& path);
bool WriteContentWorkspaceSession(const base::FilePath& path,
                                  const base::DictValue& session);

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_WORKSPACE_SESSION_STORE_H_
