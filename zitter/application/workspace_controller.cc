#include "zitter/application/workspace_controller.h"

namespace zitter {
WorkspaceController::WorkspaceController(WorkspaceModel& model,
                                         WorkspaceRuntime& runtime)
    : model_(model), runtime_(runtime) {}

WorkspaceController::~WorkspaceController() = default;

bool WorkspaceController::Activate(std::string_view id) {
  auto& model = model_.get();
  const Workspace* workspace = model.Find(id);
  if (!workspace) {
    return false;
  }
  if (model.active() == workspace) {
    return true;
  }
  if (!runtime_.get().Activate(workspace->id, workspace->profile_id)) {
    return false;
  }
  return model.SetActive(id);
}
}  // namespace zitter
