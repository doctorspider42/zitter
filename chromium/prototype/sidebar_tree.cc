// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/prototype/sidebar_tree.h"
#include "chromium/prototype/sidebar_scene.rs.h"
namespace zitter::prototype {
struct SidebarTree::Impl {
  rust::Box<sidebar::SidebarTree> rust_tree = sidebar::new_sidebar_tree();
};
SidebarTree::SidebarTree() : impl_(std::make_unique<Impl>()) {}
SidebarTree::~SidebarTree() = default;
bool SidebarTree::Collapsed(std::string_view workspace, std::string_view id) const {
  return impl_->rust_tree->collapsed(
      rust::Str(workspace.data(), workspace.size()), rust::Str(id.data(), id.size()));
}
void SidebarTree::SetCollapsed(std::string_view workspace, std::string_view id, bool value) {
  impl_->rust_tree->set_collapsed(rust::Str(workspace.data(), workspace.size()),
      rust::Str(id.data(), id.size()), value);
}
bool SidebarTree::Toggle(std::string_view workspace, std::string_view id) {
  return impl_->rust_tree->toggle(
      rust::Str(workspace.data(), workspace.size()), rust::Str(id.data(), id.size()));
}
void SidebarTree::SetDefaultCollapsed(bool value) { impl_->rust_tree->set_default_collapsed(value); }
bool SidebarTree::Revealed(std::string_view workspace, std::string_view id) const {
  return impl_->rust_tree->revealed(
      rust::Str(workspace.data(), workspace.size()), rust::Str(id.data(), id.size()));
}
void SidebarTree::SetRevealed(std::string_view workspace, std::string_view id, bool value) {
  impl_->rust_tree->set_revealed(rust::Str(workspace.data(), workspace.size()),
      rust::Str(id.data(), id.size()), value);
}
void SidebarTree::ForgetWorkspace(std::string_view workspace) {
  impl_->rust_tree->forget_workspace(rust::Str(workspace.data(), workspace.size()));
}
std::vector<SidebarTreeEntry> SidebarTree::Plan(std::string_view workspace,
                                              base::span<const SidebarTreeNode> nodes) const {
  std::vector<SidebarTreeEntry> out;
  std::vector<sidebar::TreeNode> input;
  input.reserve(nodes.size());
  for (const auto& node : nodes) input.push_back({rust::String(node.id), rust::String(node.parent), node.folder});
  const auto result = impl_->rust_tree->tree_plan(rust::Str(workspace.data(), workspace.size()),
      rust::Slice<const sidebar::TreeNode>(input.data(), input.size()));
  out.reserve(result.size());
  for (const auto& item : result) out.push_back({item.index, item.parent, item.depth, item.collapsed, item.visible});
  return out;
}
SidebarTabPlan SidebarTree::Tabs(std::string_view workspace, base::span<const SidebarTreeNode> nodes,
    base::span<const SidebarTabNode> tabs, base::span<const size_t> members, bool split) const {
  SidebarTabPlan out;
  std::vector<sidebar::TreeNode> input;
  std::vector<sidebar::TabNode> tab_input;
  input.reserve(nodes.size()); tab_input.reserve(tabs.size());
  for (const auto& node : nodes) input.push_back({rust::String(node.id), rust::String(node.parent), node.folder});
  for (const auto& tab : tabs) tab_input.push_back({tab.index, rust::String(tab.pin)});
  const auto result = impl_->rust_tree->tab_plan(rust::Str(workspace.data(), workspace.size()),
      rust::Slice<const sidebar::TreeNode>(input.data(), input.size()),
      rust::Slice<const sidebar::TabNode>(tab_input.data(), tab_input.size()),
      rust::Slice<const size_t>(members.data(), members.size()), split);
  out.slots.reserve(result.slots.size()); out.pins.reserve(result.pins.size());
  for (const auto& item : result.slots)
    out.slots.push_back({item.first, std::vector<size_t>(item.members.begin(), item.members.end()), item.pin, item.pair});
  for (const auto& item : result.pins) out.pins.push_back({item.index, item.visible});
  return out;
}

}
