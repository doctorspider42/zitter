// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CONTENT_WORKSPACE_NATIVE_MESSAGING_MAC_H_
#define ZITTER_CONTENT_WORKSPACE_NATIVE_MESSAGING_MAC_H_

#include <memory>
#include <string>

namespace content {
class BrowserContext;
}
namespace extensions {
class NativeMessageHost;
}

namespace zitter::content_embedder {
std::unique_ptr<extensions::NativeMessageHost> CreateWorkspaceNativeMessageHost(
    content::BrowserContext& context, const std::string& extension_id,
    const std::string& host_name, bool allow_user_level);
}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_WORKSPACE_NATIVE_MESSAGING_MAC_H_
