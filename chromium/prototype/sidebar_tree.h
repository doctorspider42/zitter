// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_PROTOTYPE_SIDEBAR_TREE_H_
#define ZITTER_CHROMIUM_PROTOTYPE_SIDEBAR_TREE_H_
#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "base/containers/span.h"
namespace zitter::prototype {
struct SidebarTreeNode { std::string id, parent; bool folder; };
struct SidebarTreeEntry {
  size_t index, parent;
  int depth;
  bool collapsed, visible;
};
// Only UI IDs cross the Rust ABI. Native adapters consume a pre-order plan and
// retain input/accessibility nodes, including descendants hidden by a folder.
class SidebarTree {
 public:
  static constexpr size_t kRoot = std::numeric_limits<size_t>::max();
  SidebarTree();
  ~SidebarTree();
  std::vector<SidebarTreeEntry> Plan(std::string_view workspace,
                                   base::span<const SidebarTreeNode> nodes) const;
  bool Collapsed(std::string_view workspace, std::string_view id) const;
  void SetCollapsed(std::string_view workspace, std::string_view id, bool value);
  bool Toggle(std::string_view workspace, std::string_view id);
  void ForgetWorkspace(std::string_view workspace);
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
#endif
