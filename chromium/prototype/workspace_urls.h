// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CHROMIUM_PROTOTYPE_WORKSPACE_URLS_H_
#define ZITTER_CHROMIUM_PROTOTYPE_WORKSPACE_URLS_H_

#include "base/files/file_path.h"
#include "net/base/filename_util.h"
#include "url/gurl.h"

namespace zitter::prototype {

inline constexpr char kExtensionsTabUrl[] = "chrome://extensions/";
inline constexpr char kRssTabUrl[] = "zitter://rss/";
inline bool IsRssTabUrl(const GURL& url) { return url == GURL(kRssTabUrl); }
// Local documents are opened by explicit browser or OS actions only.
inline bool IsLocalHtmlUrl(const GURL& url) {
  base::FilePath path;
  return url.is_valid() && url.SchemeIsFile() && url.host().empty() &&
         net::FileURLToFilePath(url, &path) && path.IsAbsolute() &&
         !path.IsNetwork() &&
         (path.MatchesExtension(FILE_PATH_LITERAL(".html")) ||
          path.MatchesExtension(FILE_PATH_LITERAL(".htm")) ||
          path.MatchesExtension(FILE_PATH_LITERAL(".xhtml")) ||
          path.MatchesExtension(FILE_PATH_LITERAL(".xht")) ||
          path.MatchesExtension(FILE_PATH_LITERAL(".shtml")));
}

inline GURL ExternalOpenArgument(const base::FilePath& argument) {
  return argument.IsAbsolute() ? net::FilePathToFileURL(argument)
                               : GURL(argument.AsUTF8Unsafe());
}

inline bool IsExternalOpenUrl(const GURL& url) {
  return url.is_valid() && !url.has_username() && !url.has_password() &&
         (url.SchemeIsHTTPOrHTTPS() || IsLocalHtmlUrl(url));
}

inline bool IsWorkspaceTabUrl(const GURL& url) {
  return url.SchemeIsHTTPOrHTTPS() || IsRssTabUrl(url) ||
         url == GURL(kExtensionsTabUrl);
}

}  // namespace zitter::prototype

#endif  // ZITTER_CHROMIUM_PROTOTYPE_WORKSPACE_URLS_H_
