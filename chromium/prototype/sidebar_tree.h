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
struct SidebarTabNode { size_t index; std::string pin; };
// A Split View group lists every member position; first == members.front().
struct SidebarTabSlot { size_t first; std::vector<size_t> members; size_t pin; bool pair; };
struct SidebarPinVisibility { size_t index; bool visible; };
struct SidebarTabPlan {
  std::vector<SidebarTabSlot> slots;
  std::vector<SidebarPinVisibility> pins;
};
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
  SidebarTabPlan Tabs(std::string_view workspace, base::span<const SidebarTreeNode> nodes,
                      base::span<const SidebarTabNode> tabs, base::span<const size_t> members, bool split) const;
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
