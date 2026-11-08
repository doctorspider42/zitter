// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/prototype/clean_url.h"

#include <algorithm>
#include <functional>
#include <string_view>
#include <utility>
#include <vector>

#include "base/containers/flat_set.h"
#include "base/json/json_reader.h"
#include "base/no_destructor.h"
#include "base/strings/escape.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "brave_clean_urls_json.h"
#include "extensions/common/url_pattern.h"
#include "extensions/common/url_pattern_set.h"
#include "url/gurl.h"

namespace zitter::prototype {
namespace {

struct Rule {
  extensions::URLPatternSet include;
  extensions::URLPatternSet exclude;
  base::flat_set<std::string, std::less<>> params;
};

std::vector<Rule> LoadRules() {
  const auto list = base::JSONReader::ReadList(kBraveCleanUrlsJson, base::JSON_PARSE_RFC);
  if (!list)
    return {};

  constexpr int kSchemes = URLPattern::SCHEME_HTTP | URLPattern::SCHEME_HTTPS;
  std::vector<Rule> rules;
  rules.reserve(list->size());
  for (const auto& entry : *list) {
    const auto* dict = entry.GetIfDict();
    if (!dict)
      return {};
    const auto* include = dict->FindList("include");
    const auto* exclude = dict->FindList("exclude");
    const auto* params = dict->FindList("params");
    Rule rule;
    if (!include || !exclude || !params ||
        !rule.include.Populate(*include, kSchemes, false, nullptr) ||
        !rule.exclude.Populate(*exclude, kSchemes, false, nullptr)) {
      return {};
    }
    std::vector<std::string> names;
    for (const auto& param : *params) {
      if (!param.is_string())
        return {};
      names.push_back(param.GetString());
    }
    rule.params = decltype(rule.params)(std::move(names));
    rules.push_back(std::move(rule));
  }
  return rules;
}

std::string_view ParameterName(std::string_view field) {
  return field.substr(0, field.find('='));
}

bool IsSignature(std::string_view name) {
  const std::string key = base::ToLowerASCII(base::UnescapeBinaryURLComponent(name));
  return key == "sig" || key == "signature" || key == "oauth_signature" ||
         key == "x-amz-signature" || key == "x-goog-signature";
}

}  // namespace

std::optional<std::string> CleanUrlForCopy(const GURL& url) {
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS() || url.query().empty() ||
      url.has_username() || url.has_password()) {
    return std::nullopt;
  }

  const auto fields = base::SplitStringPiece(
      url.query(), "&", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  for (const auto field : fields) {
    if (IsSignature(ParameterName(field)))
      return std::nullopt;
  }

  static const base::NoDestructor<std::vector<Rule>> rules(LoadRules());
  std::vector<const Rule*> matching;
  for (const auto& rule : *rules) {
    if (rule.include.MatchesURL(url) && !rule.exclude.MatchesURL(url))
      matching.push_back(&rule);
  }

  std::vector<std::string_view> kept;
  for (const auto field : fields) {
    const auto name = ParameterName(field);
    // These click IDs are covered by Brave's separate navigation filter.
    if (name == "fbclid" || name == "msclkid")
      continue;
    if (!std::ranges::any_of(matching, [name](const Rule* rule) {
          return rule->params.contains(name);
        })) {
      kept.push_back(field);
    }
  }
  if (kept.size() == fields.size())
    return std::nullopt;

  // Splice the query directly so retained fields keep their original encoding.
  const std::string_view spec = url.spec();
  const size_t query_start = url.parsed_for_possibly_invalid_spec().query.begin;
  std::string result(spec.substr(0, query_start - 1));
  if (!kept.empty()) {
    result += '?';
    result += base::JoinString(kept, "&");
  }
  result += spec.substr(query_start + url.query().size());
  return result;
}

}  // namespace zitter::prototype
