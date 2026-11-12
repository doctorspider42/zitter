// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CONTENT_WORKSPACE_DEVTOOLS_H_
#define ZITTER_CONTENT_WORKSPACE_DEVTOOLS_H_

#include <memory>
#include <optional>

#include "base/functional/callback.h"
#include "ui/gfx/geometry/rect.h"

namespace base {
class FilePath;
}
namespace content {
class NavigationThrottleRegistry;
class WebContents;
}

namespace zitter::content_embedder {

class WorkspaceWebAuthnProbe {
 public:
  WorkspaceWebAuthnProbe(content::WebContents& target,
                         base::OnceCallback<void(bool)> done);
  ~WorkspaceWebAuthnProbe();

 private:
  class Client;
  std::unique_ptr<Client> client_;
};

class WorkspaceDevTools {
 public:
  enum class Action { kShow, kConsole, kInspect };
  using Dock = base::RepeatingCallback<void(content::WebContents*,
                                          std::optional<gfx::Rect>)>;

  WorkspaceDevTools(const base::FilePath& path,
                    content::WebContents& target,
                    Dock dock,
                    base::OnceClosure ready);
  ~WorkspaceDevTools();

  bool IsOpenFor(content::WebContents* target) const;
  void Show(Action action);

  static void AddNavigationThrottle(content::NavigationThrottleRegistry& registry);

 private:
  class Window;
  std::unique_ptr<Window> window_;
};

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_WORKSPACE_DEVTOOLS_H_
