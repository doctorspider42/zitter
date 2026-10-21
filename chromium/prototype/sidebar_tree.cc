// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/prototype/sidebar_tree.h"
#include <map>
#include <optional>
#include <set>
#include "base/command_line.h"
#include "build/build_config.h"
#if BUILDFLAG(IS_MAC)
#include "chromium/prototype/sidebar_scene.rs.h"
#endif
namespace zitter::prototype {
struct SidebarTree::Impl {
#if BUILDFLAG(IS_MAC)
  std::optional<rust::Box<sidebar::SidebarTree>> rust_tree;
#endif
  std::map<std::string, std::set<std::string, std::less<>>, std::less<>> collapsed;
};
SidebarTree::SidebarTree() : impl_(std::make_unique<Impl>()) {
#if BUILDFLAG(IS_MAC)
  if (base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII("zitter-sidebar") == "rust-metal")
    impl_->rust_tree.emplace(sidebar::new_sidebar_tree());
#endif
}
SidebarTree::~SidebarTree() = default;
bool SidebarTree::Collapsed(std::string_view workspace, std::string_view id) const {
#if BUILDFLAG(IS_MAC)
  if (impl_->rust_tree) return (*impl_->rust_tree)->collapsed(
      rust::Str(workspace.data(), workspace.size()), rust::Str(id.data(), id.size()));
#endif
  const auto found = impl_->collapsed.find(workspace);
  return found != impl_->collapsed.end() && found->second.contains(id);
}
void SidebarTree::SetCollapsed(std::string_view workspace, std::string_view id, bool value) {
#if BUILDFLAG(IS_MAC)
  if (impl_->rust_tree) {
    (*impl_->rust_tree)->set_collapsed(rust::Str(workspace.data(), workspace.size()),
        rust::Str(id.data(), id.size()), value);
    return;
  }
#endif
  if (value) impl_->collapsed[std::string(workspace)].insert(std::string(id));
  else if (auto found = impl_->collapsed.find(workspace); found != impl_->collapsed.end()) {
    if (auto item = found->second.find(id); item != found->second.end()) found->second.erase(item);
    if (found->second.empty()) impl_->collapsed.erase(found);
  }
}
bool SidebarTree::Toggle(std::string_view workspace, std::string_view id) {
#if BUILDFLAG(IS_MAC)
  if (impl_->rust_tree) return (*impl_->rust_tree)->toggle(
      rust::Str(workspace.data(), workspace.size()), rust::Str(id.data(), id.size()));
#endif
  const bool value = !Collapsed(workspace, id); SetCollapsed(workspace, id, value); return value;
}
void SidebarTree::ForgetWorkspace(std::string_view workspace) {
#if BUILDFLAG(IS_MAC)
  if (impl_->rust_tree) {
    (*impl_->rust_tree)->forget_workspace(rust::Str(workspace.data(), workspace.size()));
    return;
  }
#endif
  if (auto found = impl_->collapsed.find(workspace); found != impl_->collapsed.end()) impl_->collapsed.erase(found);
}
std::vector<SidebarTreeEntry> SidebarTree::Plan(std::string_view workspace,
                                              base::span<const SidebarTreeNode> nodes) const {
  std::vector<SidebarTreeEntry> out;
#if BUILDFLAG(IS_MAC)
  if (impl_->rust_tree) {
    std::vector<sidebar::TreeNode> input;
    input.reserve(nodes.size());
    for (const auto& node : nodes) input.push_back({rust::String(node.id), rust::String(node.parent), node.folder});
    const auto result = (*impl_->rust_tree)->tree_plan(rust::Str(workspace.data(), workspace.size()),
        rust::Slice<const sidebar::TreeNode>(input.data(), input.size()));
    out.reserve(result.size());
    for (const auto& item : result) out.push_back({item.index, item.parent, item.depth, item.collapsed, item.visible});
    return out;
  }
#endif
  // Views baseline: the original ordered recursive traversal, with stable IDs
  // and bounded malformed-input handling matching the Rust plan contract.
  std::map<std::string_view, std::vector<size_t>> children;
  std::set<std::string_view> ids;
  for (size_t i = 0; i < nodes.size(); ++i)
    if (!nodes[i].id.empty() && ids.insert(nodes[i].id).second) children[nodes[i].parent].push_back(i);
  auto append = [&](auto&& self, std::string_view parent_id, size_t parent, int depth, bool visible) -> void {
    if (depth > 64) return;
    const auto found = children.find(parent_id); if (found == children.end()) return;
    for (size_t index : found->second) {
      const auto& node = nodes[index]; const bool collapsed = node.folder && Collapsed(workspace, node.id);
      out.push_back({index, parent, depth, collapsed, visible});
      if (node.folder) self(self, node.id, index, depth + 1, visible && !collapsed);
    }
  };
  out.reserve(nodes.size()); append(append, "", kRoot, 0, true); return out;
}
SidebarTabPlan SidebarTree::Tabs(std::string_view workspace, base::span<const SidebarTreeNode> nodes,
    base::span<const SidebarTabNode> tabs, size_t first, size_t second, bool split) const {
  SidebarTabPlan out;
#if BUILDFLAG(IS_MAC)
  if (impl_->rust_tree) {
    std::vector<sidebar::TreeNode> input;
    std::vector<sidebar::TabNode> tab_input;
    input.reserve(nodes.size()); tab_input.reserve(tabs.size());
    for (const auto& node : nodes) input.push_back({rust::String(node.id), rust::String(node.parent), node.folder});
    for (const auto& tab : tabs) tab_input.push_back({tab.index, rust::String(tab.pin)});
    const auto result = (*impl_->rust_tree)->tab_plan(rust::Str(workspace.data(), workspace.size()),
        rust::Slice<const sidebar::TreeNode>(input.data(), input.size()),
        rust::Slice<const sidebar::TabNode>(tab_input.data(), tab_input.size()), first, second, split);
    out.slots.reserve(result.slots.size()); out.pins.reserve(result.pins.size());
    for (const auto& item : result.slots) out.slots.push_back({item.first, item.second, item.pin, item.pair});
    for (const auto& item : result.pins) out.pins.push_back({item.index, item.visible});
    return out;
  }
#endif
  // Default Views keeps the same grouping contract in its native baseline.
  const auto tree = Plan(workspace, nodes);
  std::map<std::string_view, size_t> reachable;
  for (const auto& entry : tree) reachable.emplace(nodes[entry.index].id, entry.index);
  std::map<size_t, size_t> positions;
  std::vector<size_t> order;
  std::set<std::string_view> referenced;
  for (size_t i = 0; i < tabs.size(); ++i) {
    if (!positions.emplace(tabs[i].index, i).second) continue;
    order.push_back(i);
    if (!tabs[i].pin.empty()) referenced.insert(tabs[i].pin);
  }
  for (const auto& entry : tree) out.pins.push_back({entry.index, !referenced.contains(nodes[entry.index].id)});
  const auto a = positions.find(first), b = positions.find(second);
  const bool paired = split && first != second && a != positions.end() && b != positions.end();
  bool emitted = false;
  for (size_t index : order) {
    if (paired && (index == a->second || index == b->second)) {
      if (!emitted) {
        size_t pin = kRoot;
        for (size_t member : {a->second, b->second}) {
          if (auto found = reachable.find(tabs[member].pin); found != reachable.end()) {pin = found->second; break;}
        }
        out.slots.push_back({a->second, b->second, pin, true}); emitted = true;
      }
    } else out.slots.push_back({index, kRoot, kRoot, false});
  }
  return out;
}

}
