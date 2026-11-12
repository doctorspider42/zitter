// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "chromium/prototype/adblock_browser_support.h"

#include "chromium/prototype/adblock.rs.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

#include "base/functional/callback.h"
#include "base/json/json_writer.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/isolated_world_ids.h"
#include "content_blocking_script.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "third_party/blink/public/common/renderer_preferences/renderer_preferences.h"

extern "C" void zitter_adblock_set_domain_resolver(
    void (*)(const uint8_t*, size_t, size_t*, size_t*));

namespace zitter::prototype {
namespace {

void ResolveDomain(const uint8_t* host,
                   size_t length,
                   size_t* start,
                   size_t* end) {
  const std::string_view name(reinterpret_cast<const char*>(host), length);
  const auto domain = net::registry_controlled_domains::GetDomainAndRegistry(
      name, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  *start = domain.empty() || domain.size() > length ? 0 : length - domain.size();
  *end = length;
}

}  // namespace

void ConfigureAdblockDomainResolver() {
  zitter_adblock_set_domain_resolver(&ResolveDomain);
}

std::string AdblockSiteScope(const GURL& page) {
  auto domain = net::registry_controlled_domains::GetDomainAndRegistry(
      page, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  return domain.empty() ? page.GetHost() : domain;
}

std::string AdblockRuleError(const std::string& rule) {
  const auto check = adblock::check_rule(rule);
  return check.kind == 0 ? std::string(check.error.empty() ? "invalid rule" : check.error) : std::string();
}
std::string AdblockRuleSite(const std::string& rule) {
  auto first_host = [](std::string_view list, char separator) {
    for (auto part : base::SplitStringPiece(list, std::string(1, separator), base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY))
      if (!part.starts_with("~")) return std::string(part);
    return std::string();
  };
  if (const auto at = rule.find('#'); at != std::string::npos && at > 0 && !rule.starts_with("||") && !rule.starts_with("@@"))
    return first_host(std::string_view(rule).substr(0, at), ',');
  if (const auto options = rule.rfind('$'); options != std::string::npos)
    for (auto option : base::SplitStringPiece(std::string_view(rule).substr(options + 1), ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY))
      if (option.starts_with("domain=")) return first_host(option.substr(7), '|');
  std::string_view body = rule;
  if (body.starts_with("@@")) body.remove_prefix(2);
  if (!body.starts_with("||")) return std::string();
  body.remove_prefix(2);
  return std::string(body.substr(0, body.find_first_of("^/*:|?$")));
}
std::vector<std::pair<size_t, std::string>> AdblockTextErrors(const std::string& text) {
  std::vector<std::pair<size_t, std::string>> errors;
  size_t line_number = 0;
  for (auto line : base::SplitStringPiece(text, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL)) {
    ++line_number;
    const std::string rule(base::TrimWhitespaceASCII(line, base::TRIM_ALL));
    if (rule.empty() || rule.starts_with("!")) continue;
    if (rule.size() > 1000) { errors.emplace_back(line_number, "the rule is longer than 1000 characters"); continue; }
    if (auto problem = AdblockRuleError(rule); !problem.empty()) errors.emplace_back(line_number, std::move(problem));
  }
  return errors;
}

void SetAdblockPageScripts(content::WebContents& contents,
                           std::vector<std::string> scripts) {
  auto& current = contents.GetMutableRendererPrefs()->zitter_page_scripts;
  if (current == scripts)
    return;
  current = std::move(scripts);
  contents.SyncRendererPrefs();
}

void RunAdblockCosmeticScript(content::RenderFrameHost* frame,
                              base::DictValue args,
                              base::OnceCallback<void(base::Value)> done) {
  if (!frame || !frame->IsRenderFrameLive() ||
      !frame->GetLastCommittedURL().SchemeIsHTTPOrHTTPS()) {
    std::move(done).Run(base::Value());
    return;
  }
  const auto json = base::WriteJson(args);
  if (!json) {
    std::move(done).Run(base::Value());
    return;
  }
  std::string script = kContentBlockingScript;
  base::ReplaceFirstSubstringAfterOffset(&script, 0, "ZITTER_ARGS", *json);
  frame->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(script), std::move(done),
      content::ISOLATED_WORLD_ID_CONTENT_END);
}

}  // namespace zitter::prototype
