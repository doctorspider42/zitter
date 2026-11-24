// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// Applies a profile's User-Agent settings (prototype/user_agent_policy.h) to
// every top-level navigation of a page.

#ifndef ZITTER_CONTENT_USER_AGENT_PAGE_H_
#define ZITTER_CONTENT_USER_AGENT_PAGE_H_

#include <optional>
#include <string>
#include <string_view>

#include "chromium/prototype/user_agent_policy.h"
#include "third_party/blink/public/common/user_agent/user_agent_metadata.h"

class GURL;

namespace content {
class NavigationThrottleRegistry;
class WebContents;
}

namespace zitter::content_embedder {

// The browser's default identity: the reduced Chrome user agent, with the
// Google Chrome brand in its client hints. Pages without an override and
// requests outside pages (service workers, extensions) use it.
std::string DefaultUserAgent();
blink::UserAgentMetadata DefaultUserAgentMetadata();

// Chrome's metadata plus the Zitter brand: what Zitter sends when client hints
// do not follow the preset.
blink::UserAgentMetadata ZitterUserAgentMetadata();

// The user agent string and client hints a concrete preset sends.
blink::UserAgentOverride UserAgentOverrideFor(
    const prototype::UserAgentSettings& settings,
    std::string_view preset);

// What `page` resolves to with `settings`.
struct UserAgentChoice {
  prototype::ResolvedUserAgent resolved;
  std::string ua;
};
UserAgentChoice DescribeUserAgent(const prototype::UserAgentSettings& settings,
                                  const GURL& page);

// What the page's committed document was loaded with.
std::optional<std::string> CommittedUserAgentPreset(content::WebContents& page);

void AttachUserAgentPage(content::WebContents& page);

// A top-level GET redirect to a host with another preset (a site sending you
// to accounts.google.com) starts again as a navigation to that host, so the
// request carries the preset the target needs.
void AddUserAgentRedirectThrottle(content::NavigationThrottleRegistry& registry);

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_USER_AGENT_PAGE_H_
