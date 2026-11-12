#include "zitter/core/workspace_model.h"

#include <utility>

namespace zitter {
WorkspaceModel::WorkspaceModel() = default;
WorkspaceModel::~WorkspaceModel() = default;

bool WorkspaceModel::Add(Workspace workspace) {
  if (workspace.id.empty() || workspace.profile_id.empty() ||
      workspace.name.empty() || Find(workspace.id)) {
    return false;
  }
  workspaces_.push_back(std::move(workspace));
  return true;
}

bool WorkspaceModel::Remove(std::string_view id) {
  if (workspaces_.size() <= 1 || !Find(id) || active_id_ == id) return false;
  for (auto it = workspaces_.begin(); it != workspaces_.end(); ++it) {
    if (it->id == id) { workspaces_.erase(it); break; }
  }
  return true;
}

bool WorkspaceModel::Update(std::string_view id, std::string name, std::string profile_id) {
  if (name.empty() || profile_id.empty()) return false;
  for (auto& workspace : workspaces_) {
    if (workspace.id != id) continue;
    workspace.name = std::move(name);
    workspace.profile_id = std::move(profile_id);
    return true;
  }
  return false;
}

bool WorkspaceModel::SetLiveFoldersEnabled(std::string_view id, LiveFolderProvider provider,
                                          bool enabled) {
  for (auto& workspace : workspaces_) {
    if (workspace.id != id) continue;
    workspace.live_folders_enabled[static_cast<std::size_t>(provider)] = enabled;
    return true;
  }
  return false;
}

const Workspace* WorkspaceModel::Find(std::string_view id) const {
  for (const auto& workspace : workspaces_) {
    if (workspace.id == id) {
      return &workspace;
    }
  }
  return nullptr;
}

const Workspace* WorkspaceModel::active() const {
  return Find(active_id_);
}

bool WorkspaceModel::SetActive(std::string_view id) {
  if (!Find(id)) {
    return false;
  }
  active_id_ = id;
  return true;
}
}  // namespace zitter
