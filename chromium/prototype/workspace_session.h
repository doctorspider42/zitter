// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CHROMIUM_PROTOTYPE_WORKSPACE_SESSION_H_
#define ZITTER_CHROMIUM_PROTOTYPE_WORKSPACE_SESSION_H_

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/values.h"
#include "url/gurl.h"

namespace zitter::prototype {

inline constexpr size_t kMaxWorkspaceSessionTabs = 500;

struct WorkspaceSessionTab {
  GURL url;
  std::optional<std::u16string> title;
  std::optional<std::string> pin;
  std::optional<std::u16string> custom_title;
};

std::optional<std::vector<WorkspaceSessionTab>> ReadWorkspaceSessionTabs(
    const base::DictValue& workspace,
    std::string_view profile_id);

base::DictValue WriteWorkspaceSessionTab(const GURL& url,
                                         std::u16string_view title);

}  // namespace zitter::prototype

#endif  // ZITTER_CHROMIUM_PROTOTYPE_WORKSPACE_SESSION_H_
