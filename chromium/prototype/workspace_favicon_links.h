// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_WORKSPACE_FAVICON_LINKS_H_
#define ZITTER_WORKSPACE_FAVICON_LINKS_H_

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "base/no_destructor.h"
#include "base/strings/escape.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "re2/re2.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace zitter::prototype {

inline bool IsWorkspaceFaviconUrl(const GURL& icon) {
  return icon.SchemeIsHTTPOrHTTPS() || icon.SchemeIs(url::kDataScheme);
}

// Inspect markup without running scripts or loading other page resources.
inline std::vector<GURL> WorkspaceIconCandidates(std::string_view html, const GURL& page_url) {
  static const base::NoDestructor<RE2> tag(
      R"re((?is)<!--.*?-->|<(?:script|style|title|textarea)\b[^>]*>.*?</(?:script|style|title|textarea)\s*>|<(link|base)\b((?:"[^"]*"|'[^']*'|[^'">])*)>)re");
  static const base::NoDestructor<RE2> attribute(
      R"re(([^\s=/"'<>]+)(?:\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s"'=<>]+)))?)re");
  constexpr size_t kMaxDeclaredIcons = 6;
  std::vector<std::string> paths;
  GURL base_url = page_url;
  bool has_base = false;
  std::string_view name, attributes;
  while (RE2::FindAndConsume(&html, *tag, &name, &attributes)) {
    if (name.empty()) continue;
    std::string_view key, quoted, single_quoted, unquoted, href, rel;
    while (RE2::FindAndConsume(&attributes, *attribute, &key, &quoted, &single_quoted, &unquoted)) {
      const auto value = !quoted.empty() ? quoted : !single_quoted.empty() ? single_quoted : unquoted;
      if (base::EqualsCaseInsensitiveASCII(key, "href") && href.empty()) href = value;
      if (base::EqualsCaseInsensitiveASCII(key, "rel") && rel.empty()) rel = value;
    }
    if (href.empty()) continue;
    std::string path = base::UTF16ToUTF8(base::UnescapeForHTML(base::UTF8ToUTF16(href)));
    if (base::EqualsCaseInsensitiveASCII(name, "base")) {
      if (!has_base) {
        const GURL candidate = page_url.Resolve(path);
        if (candidate.SchemeIsHTTPOrHTTPS()) base_url = candidate;
        has_base = true;
      }
    } else if (paths.size() < kMaxDeclaredIcons) {
      const auto lower_rel = base::ToLowerASCII(rel);
      const auto relations = base::SplitStringPiece(lower_rel, base::kWhitespaceASCII,
                                                     base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
      if (std::ranges::find(relations, "icon") != relations.end())
        paths.push_back(std::move(path));
    }
  }
  std::vector<GURL> icons;
  for (const auto& path : paths) {
    GURL url = base_url.Resolve(path);
    if (IsWorkspaceFaviconUrl(url) && std::ranges::find(icons, url) == icons.end())
      icons.push_back(std::move(url));
  }
  return icons;
}

}  // namespace zitter::prototype

#endif  // ZITTER_WORKSPACE_FAVICON_LINKS_H_
