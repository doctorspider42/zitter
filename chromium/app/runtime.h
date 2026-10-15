#ifndef ZITTER_CHROMIUM_APP_RUNTIME_H_
#define ZITTER_CHROMIUM_APP_RUNTIME_H_

#include <memory>

#include "base/memory/weak_ptr.h"
#include "chrome/browser/chrome_browser_main_extra_parts.h"

class Profile;
class ScopedKeepAlive;
class ScopedProfileKeepAlive;
namespace zitter::prototype { class WorkspaceWindow; }

namespace zitter {
// Lives inside Chromium's regular browser startup/shutdown, never a test runner.
class Runtime final : public ChromeBrowserMainExtraParts {
 public:
  Runtime();
  ~Runtime() override;
  void PreBrowserStart() override;
  void PreMainMessageLoopRun() override;
  void PostMainMessageLoopRun() override;
 private:
  void WorkReady(Profile* profile);
  void PersonalReady(Profile* profile);
  void RequestClose();
  void Close();
  void ReleaseProfiles();
  std::unique_ptr<ScopedKeepAlive> keep_alive_;
  std::unique_ptr<ScopedProfileKeepAlive> work_;
  std::unique_ptr<ScopedProfileKeepAlive> personal_;
  std::unique_ptr<prototype::WorkspaceWindow> window_;
  bool closing_ = false;
  base::WeakPtrFactory<Runtime> weak_factory_{this};
};
}  // namespace zitter
#endif
