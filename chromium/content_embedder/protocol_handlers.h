// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CONTENT_PROTOCOL_HANDLERS_H_
#define ZITTER_CONTENT_PROTOCOL_HANDLERS_H_

#include <memory>
#include <string>
#include <string_view>
#include <vector>

class GURL;

namespace blink { class URLLoaderThrottle; }
namespace content {
class BrowserContext;
class RenderFrameHost;
}  // namespace content
namespace url { class Origin; }

namespace zitter::content_embedder {

class ProfilePermissionController;

// navigator.registerProtocolHandler() for Content pages, on Chrome's
// components/custom_handlers: the site permission prompt asks, the profile's
// site permission store keeps the accepted handlers, and a navigation to a
// handled scheme (a mailto: link, a typed web+foo: address) loads the
// handler's page instead.

// Settings > Site permissions key; prompts use "protocol-handler:<scheme>".
inline constexpr std::string_view kProtocolHandlerCategory = "protocol-handler";

void RegisterPageProtocolHandler(content::RenderFrameHost& frame,
                                 const std::string& protocol,
                                 const GURL& url,
                                 bool user_gesture);
void UnregisterPageProtocolHandler(content::RenderFrameHost& frame,
                                   const std::string& protocol,
                                   const GURL& url);

// True when an accepted handler takes `scheme` in `context`.
bool IsProtocolHandled(content::BrowserContext& context, std::string_view scheme);
// The handler page for `url`, or an empty URL when no handler takes it.
GURL TranslateProtocolHandlerURL(content::BrowserContext& context, const GURL& url);
// Rewrites navigations to handled schemes; null without a handler store.
std::unique_ptr<blink::URLLoaderThrottle> CreateProtocolHandlerThrottle(
    content::BrowserContext* context);

// The site panel: schemes `origin` handles, and forgetting them (an empty
// `scheme` forgets them all).
std::vector<std::string> ProtocolHandlerSchemes(ProfilePermissionController& permissions,
                                                const url::Origin& origin);
bool RemoveProtocolHandlers(ProfilePermissionController& permissions,
                            const url::Origin& origin,
                            std::string_view scheme = {});

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_PROTOCOL_HANDLERS_H_
