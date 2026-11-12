// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CHROMIUM_PROTOTYPE_WORKSPACE_URLS_H_
#define ZITTER_CHROMIUM_PROTOTYPE_WORKSPACE_URLS_H_

#include "url/gurl.h"

namespace zitter::prototype {

inline constexpr char kExtensionsTabUrl[] = "chrome://extensions/";
inline constexpr char kRssTabUrl[] = "zitter://rss/";
inline bool IsRssTabUrl(const GURL& url) { return url == GURL(kRssTabUrl); }
inline bool IsWorkspaceTabUrl(const GURL& url) {
  return url.SchemeIsHTTPOrHTTPS() || IsRssTabUrl(url) ||
         url == GURL(kExtensionsTabUrl);
}

}  // namespace zitter::prototype

#endif  // ZITTER_CHROMIUM_PROTOTYPE_WORKSPACE_URLS_H_
