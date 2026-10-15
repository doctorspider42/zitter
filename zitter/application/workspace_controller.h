#ifndef ZITTER_APPLICATION_WORKSPACE_CONTROLLER_H_
#define ZITTER_APPLICATION_WORKSPACE_CONTROLLER_H_

#include <functional>
#include <string_view>

#include "zitter/core/workspace_model.h"
#include "zitter/ports/workspace_runtime.h"

namespace zitter {
class WorkspaceController {
 public:
  WorkspaceController(WorkspaceModel& model, WorkspaceRuntime& runtime);
  ~WorkspaceController();
  bool Activate(std::string_view id);

 private:
  std::reference_wrapper<WorkspaceModel> model_;
  std::reference_wrapper<WorkspaceRuntime> runtime_;
};
}  // namespace zitter
#endif
