// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "chromium/prototype/workspace_session.h"

#include <utility>

#include "base/strings/utf_string_conversions.h"
#include "chromium/prototype/workspace_urls.h"

namespace zitter::prototype {

std::optional<std::vector<WorkspaceSessionTab>> ReadWorkspaceSessionTabs(
    const base::DictValue& workspace,
    std::string_view profile_id) {
  const auto* profile = workspace.FindString("profile_id");
  const auto* tabs = workspace.FindList("tabs");
  if (!profile || *profile != profile_id || !tabs || tabs->empty() ||
      tabs->size() > kMaxWorkspaceSessionTabs) {
    return std::nullopt;
  }

  std::vector<WorkspaceSessionTab> restored;
  restored.reserve(tabs->size());
  for (const auto& value : *tabs) {
    const auto* tab = value.GetIfDict();
    const auto* spec = tab ? tab->FindString("url") : nullptr;
    if (!spec || spec->size() > 32768)
      return std::nullopt;
    GURL url(*spec);
    if (!spec->empty() && !IsWorkspaceTabUrl(url))
      return std::nullopt;
    WorkspaceSessionTab entry{.url = std::move(url)};
    if (const auto* title = tab->FindString("title"); title && title->size() <= 32768)
      entry.title = base::UTF8ToUTF16(*title);
    if (const auto* pin = tab->FindString("pin"))
      entry.pin = *pin;
    restored.push_back(std::move(entry));
  }
  return restored;
}

base::DictValue WriteWorkspaceSessionTab(const GURL& url,
                                         std::u16string_view title) {
  return base::DictValue()
      .Set("url", IsWorkspaceTabUrl(url) ? url.spec() : "")
      .Set("title", base::UTF16ToUTF8(title));
}

}  // namespace zitter::prototype
