// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_CONTENT_EMBEDDER_EXTENSION_DOWNLOADS_H_
#define ZITTER_CHROMIUM_CONTENT_EMBEDDER_EXTENSION_DOWNLOADS_H_

#include <memory>

class ExtensionFunctionRegistry;

namespace content {
class BrowserContext;
}

namespace zitter::content_embedder {

// chrome.downloads over the profile's DownloadManager. Downloads still go
// through the profile's download delegate, so Safe Browsing, the download
// folder and "Ask where to save each file" apply as for any other download.
void RegisterExtensionDownloadFunctions(ExtensionFunctionRegistry& registry);

// Sends downloads.onCreated, onChanged and onErased for one regular profile.
class ExtensionDownloadEvents {
 public:
  virtual ~ExtensionDownloadEvents() = default;
};
std::unique_ptr<ExtensionDownloadEvents> CreateExtensionDownloadEvents(
    content::BrowserContext& context);

}  // namespace zitter::content_embedder

#endif
