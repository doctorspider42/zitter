// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CHROMIUM_PROTOTYPE_ADBLOCK_BROWSER_SUPPORT_H_
#define ZITTER_CHROMIUM_PROTOTYPE_ADBLOCK_BROWSER_SUPPORT_H_

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/callback_forward.h"
#include "base/values.h"

class GURL;

namespace content {
class RenderFrameHost;
class WebContents;
}

namespace zitter::prototype {

// Per-profile prefs, registered by tools/content_blocking_patch.py.
inline constexpr char kAdBlockingPref[] = "zitter.ad_blocking";
inline constexpr char kAdBlockingAllowedSitesPref[] = "zitter.ad_blocking_allowed_sites";
inline constexpr char kAdBlockingRulesPref[] = "zitter.ad_blocking_rules";
// Own hiding rules that hid nothing on their site's recent loads: rule -> loads.
inline constexpr char kAdBlockingRuleMissesPref[] = "zitter.ad_blocking_rule_misses";
// Optional lists switched away from their default: list id -> on.
inline constexpr char kAdBlockingListsPref[] = "zitter.ad_blocking_lists";

void ConfigureAdblockDomainResolver();
std::string AdblockSiteScope(const GURL& page);

std::string AdblockRuleError(const std::string& rule);
std::string AdblockRuleSite(const std::string& rule);
std::vector<std::pair<size_t, std::string>> AdblockTextErrors(
    const std::string& text);

// Each entry contains a host, a newline, and a script to run at document start.
void SetAdblockPageScripts(content::WebContents& contents,
                           std::vector<std::string> scripts);

void RunAdblockCosmeticScript(content::RenderFrameHost* frame,
                              base::DictValue args,
                              base::OnceCallback<void(base::Value)> done);

}  // namespace zitter::prototype

#endif  // ZITTER_CHROMIUM_PROTOTYPE_ADBLOCK_BROWSER_SUPPORT_H_
