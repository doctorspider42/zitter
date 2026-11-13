// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CONTENT_EXTENSION_NOTIFICATIONS_MAC_H_
#define ZITTER_CONTENT_EXTENSION_NOTIFICATIONS_MAC_H_

#include <memory>
#include <string_view>

namespace content {
class BrowserContext;
class PlatformNotificationService;
}
class ExtensionFunctionRegistry;

namespace zitter::content_embedder {
class ProfileContext;
std::unique_ptr<content::PlatformNotificationService> CreateMacWebNotificationService(
    ProfileContext& profile);
void RegisterExtensionNotificationFunctions(ExtensionFunctionRegistry& registry);
void RemoveExtensionNotifications(content::BrowserContext& context, std::string_view extension_id);
}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_EXTENSION_NOTIFICATIONS_MAC_H_
