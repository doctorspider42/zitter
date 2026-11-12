// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CONTENT_NAVIGATION_REPUTATION_H_
#define ZITTER_CONTENT_NAVIGATION_REPUTATION_H_

#include <memory>
#include <string>

#include "base/functional/callback_forward.h"

class GURL;

namespace base {
class FilePath;
}

namespace content {
class NavigationThrottleRegistry;
}

namespace zitter::content_embedder {

// Browser-wide local Safe Browsing lists. Full hash caches for V5 belong
// to the BrowserContext that requested the lookup.
class NavigationReputation {
 public:
  NavigationReputation(const base::FilePath& root, const std::string& api_key);
  NavigationReputation(const base::FilePath& root,
                       const GURL& loopback_provider,
                       base::OnceClosure ready);
  ~NavigationReputation();

  void AddThrottle(content::NavigationThrottleRegistry& registry);

 private:
  class Service;
  std::unique_ptr<Service> service_;
};

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_NAVIGATION_REPUTATION_H_
