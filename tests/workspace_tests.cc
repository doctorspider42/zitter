#include <cstdlib>
#include <iostream>
#include <string>

#include "zitter/application/workspace_controller.h"

namespace {
void Expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

class Runtime final : public zitter::WorkspaceRuntime {
 public:
  bool Activate(std::string_view workspace, std::string_view profile) override {
    ++calls;
    if (fail) {
      return false;
    }
    workspace_id = workspace;
    profile_id = profile;
    return true;
  }
  bool fail = false;
  int calls = 0;
  std::string workspace_id;
  std::string profile_id;
};
}  // namespace

int main() {
  zitter::WorkspaceModel model;
  Expect(!model.Add({"", "p", "Invalid"}), "reject empty identifier");
  Expect(model.Add({"work", "work-profile", "Work"}), "add work");
  Expect(model.Add({"personal", "personal-profile", "Personal"}), "add personal");
  Expect(!model.Add({"work", "other", "Duplicate"}), "reject duplicate workspace");
  // Sharing a profile is a supported product choice, not a validation error.
  Expect(model.Add({"project", "work-profile", "Project"}), "allow shared profile");
  Runtime runtime;
  zitter::WorkspaceController controller(model, runtime);
  Expect(!controller.Activate("missing"), "reject unknown workspace");
  Expect(runtime.calls == 0 && !model.active(), "unknown selection has no effects");
  Expect(controller.Activate("work"), "activate work");
  Expect(runtime.profile_id == "work-profile", "route correct profile");
  Expect(controller.Activate("work") && runtime.calls == 1, "selection is idempotent");
  runtime.fail = true;
  Expect(!controller.Activate("personal"), "propagate activation failure");
  Expect(model.active()->id == "work" && runtime.workspace_id == "work",
         "preserve selection on failure");
  runtime.fail = false;
  for (int i = 0; i < 100; ++i) {
    Expect(controller.Activate("personal"), "switch personal");
    Expect(runtime.profile_id == "personal-profile", "personal profile stays bound");
    Expect(controller.Activate("work"), "switch work");
    Expect(runtime.profile_id == "work-profile", "work profile stays bound");
  }
  std::cout << "Workspace contracts passed\n";
}
