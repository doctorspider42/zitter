// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "protocol_handlers.h"

#include <algorithm>
#include <set>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/supports_user_data.h"
#include "base/time/time.h"
#include "components/custom_handlers/protocol_handler.h"
#include "components/custom_handlers/protocol_handler_registry.h"
#include "components/custom_handlers/protocol_handler_throttle.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/render_frame_host.h"
#include "profile_context.h"
#include "profile_permission_controller.h"
#include "third_party/blink/public/common/loader/url_loader_throttle.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace zitter::content_embedder {

namespace {

using custom_handlers::ProtocolHandler;

ProfilePermissionController& Permissions(content::BrowserContext& context) {
  return static_cast<ProfileContext&>(context).site_permissions();
}

// Requests the person turned down this session: a page that asks again
// without a click is not asked about again.
class DeclinedHandlers final : public base::SupportsUserData::Data {
 public:
  static std::set<std::string>& For(content::BrowserContext& context) {
    static const char kKey[] = "zitter_declined_protocol_handlers";
    auto* data = static_cast<DeclinedHandlers*>(context.GetUserData(kKey));
    if (!data) {
      auto owned = std::make_unique<DeclinedHandlers>();
      data = owned.get();
      context.SetUserData(kKey, std::move(owned));
    }
    return data->keys_;
  }

 private:
  std::set<std::string> keys_;
};

void AnswerRegistration(base::WeakPtr<content::BrowserContext> context,
                        ProtocolHandler handler,
                        std::string request,
                        bool allowed) {
  if (!context)
    return;
  if (!allowed) {
    auto& declined = DeclinedHandlers::For(*context);
    if (declined.size() >= 256)
      declined.clear();
    declined.insert(std::move(request));
    return;
  }
  auto& permissions = Permissions(*context);
  auto* registry = permissions.protocol_handlers();
  if (registry && permissions.CanClearSitePermissions())
    registry->OnAcceptRegisterProtocolHandler(handler);
}

}  // namespace

void RegisterPageProtocolHandler(content::RenderFrameHost& frame,
                                 const std::string& protocol,
                                 const GURL& url,
                                 bool user_gesture) {
  auto& context = *frame.GetBrowserContext();
  auto& permissions = Permissions(context);
  auto* registry = permissions.protocol_handlers();
  const auto origin = frame.GetLastCommittedOrigin();
  // The prompt names the page's site, so only that site may ask; fenced
  // frames may not register at all.
  if (!registry || !permissions.CanClearSitePermissions() || !frame.IsActive() ||
      frame.IsNestedWithinFencedFrame() ||
      origin != frame.GetOutermostMainFrame()->GetLastCommittedOrigin()) {
    return;
  }
  const auto handler = ProtocolHandler::CreateProtocolHandler(protocol, url);
  // Ignored, already registered, or replacing this site's own handler.
  if (!handler.IsValid() || registry->SilentlyHandleRegisterHandlerRequest(handler))
    return;
  if (permissions.SiteSetting(kProtocolHandlerCategory, origin) != 3)
    return;
  auto request = origin.Serialize() + " " + protocol + " " + url.spec();
  if (!user_gesture && DeclinedHandlers::For(context).contains(request))
    return;
  permissions.RunSitePrompt(
      frame, std::string(kProtocolHandlerCategory) + ":" + protocol,
      base::BindOnce(&AnswerRegistration, context.GetWeakPtr(), handler,
                     std::move(request)));
}

void UnregisterPageProtocolHandler(content::RenderFrameHost& frame,
                                   const std::string& protocol,
                                   const GURL& url) {
  auto& permissions = Permissions(*frame.GetBrowserContext());
  auto* registry = permissions.protocol_handlers();
  if (!registry || !permissions.CanClearSitePermissions() || !frame.IsActive())
    return;
  const auto handler = ProtocolHandler::CreateProtocolHandler(protocol, url);
  if (registry->IsRegistered(handler))
    registry->RemoveHandler(handler);
}

bool IsProtocolHandled(content::BrowserContext& context, std::string_view scheme) {
  auto* registry = Permissions(context).protocol_handlers();
  return registry && registry->IsHandledProtocol(scheme);
}

GURL TranslateProtocolHandlerURL(content::BrowserContext& context, const GURL& url) {
  auto* registry = Permissions(context).protocol_handlers();
  if (!url.is_valid() || !registry || !registry->IsHandledProtocol(url.scheme()))
    return GURL();
  return registry->Translate(url);
}

std::unique_ptr<blink::URLLoaderThrottle> CreateProtocolHandlerThrottle(
    content::BrowserContext* context) {
  auto* registry = context ? Permissions(*context).protocol_handlers() : nullptr;
  if (!registry)
    return nullptr;
  return std::make_unique<custom_handlers::ProtocolHandlerThrottle>(*registry);
}

std::vector<std::string> ProtocolHandlerSchemes(ProfilePermissionController& permissions,
                                                const url::Origin& origin) {
  std::vector<std::string> schemes;
  auto* registry = permissions.protocol_handlers();
  if (!registry || origin.opaque())
    return schemes;
  for (const auto& handler :
       registry->GetUserDefinedHandlers(base::Time(), base::Time::Max())) {
    if (!handler.IsExtensionHandler() && origin.IsSameOriginWith(handler.url()) &&
        !std::ranges::contains(schemes, handler.protocol())) {
      schemes.push_back(handler.protocol());
    }
  }
  std::ranges::sort(schemes);
  return schemes;
}

bool RemoveProtocolHandlers(ProfilePermissionController& permissions,
                            const url::Origin& origin,
                            std::string_view scheme) {
  auto* registry = permissions.protocol_handlers();
  if (!registry || !permissions.CanClearSitePermissions() || origin.opaque())
    return false;
  bool removed = false;
  for (const auto& handler :
       registry->GetUserDefinedHandlers(base::Time(), base::Time::Max())) {
    if (!handler.IsExtensionHandler() && origin.IsSameOriginWith(handler.url()) &&
        (scheme.empty() || handler.protocol() == scheme)) {
      registry->RemoveHandler(handler);
      removed = true;
    }
  }
  return removed;
}

}  // namespace zitter::content_embedder
