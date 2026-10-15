#ifndef ZITTER_PORTS_WORKSPACE_RUNTIME_H_
#define ZITTER_PORTS_WORKSPACE_RUNTIME_H_

#include <string_view>

namespace zitter {
class WorkspaceRuntime {
 public:
  virtual ~WorkspaceRuntime() = default;
  // UI-sequence operation on already initialized hosts. Failure must leave
  // the previously active host intact. Profile loading happens before binding.
  virtual bool Activate(std::string_view workspace_id,
                        std::string_view profile_id) = 0;
};
}  // namespace zitter
#endif
