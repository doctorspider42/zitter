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
