// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CHROMIUM_PROTOTYPE_ADBLOCK_NETWORK_H_
#define ZITTER_CHROMIUM_PROTOTYPE_ADBLOCK_NETWORK_H_

#include <string>

#include "base/functional/callback.h"
#include "content/public/browser/content_browser_client.h"
#include "url/gurl.h"

namespace content { class RenderFrameHost; }
namespace net { class IsolationInfo; }
namespace network { class URLLoaderFactoryBuilder; }

namespace zitter::prototype {

// Returns an empty URL when the factory has no filterable top-level web page.
GURL AdblockTopLevelPage(content::RenderFrameHost* frame,
                         content::ContentBrowserClient::URLLoaderFactoryType type,
                         const net::IsolationInfo& isolation);

using AdblockRequestFilter =
    base::RepeatingCallback<bool(const GURL& url,
                                 const GURL& page,
                                 const std::string& type,
                                 std::string* redirect)>;

// The filter may be called after the originating profile is gone. Bind weak
// state and allow the request when that state has expired.
void AppendAdblockFactory(network::URLLoaderFactoryBuilder& builder,
                          GURL page,
                          AdblockRequestFilter filter);

}  // namespace zitter::prototype

#endif  // ZITTER_CHROMIUM_PROTOTYPE_ADBLOCK_NETWORK_H_
