#include "zitter/core/pin_tree.h"

#include <algorithm>
#include <utility>

namespace zitter {
PinTree::~PinTree() = default;

PinTree::PinTree(std::span<const PinNode> nodes)
    : nodes_(nodes), children_(nodes.size()) {
  by_id_.reserve(nodes.size());
  for (size_t i = 0; i < nodes.size(); ++i) {
    if (nodes[i].id.empty() || !by_id_.emplace(nodes[i].id, i).second)
      unique_ids_ = false;
  }
  for (size_t i = 0; i < nodes.size(); ++i) {
    if (const auto parent = by_id_.find(nodes[i].parent); parent != by_id_.end())
      children_[parent->second].push_back(i);
  }
}

bool PinTree::IsValid() const {
  if (!unique_ids_) return false;
  std::vector<std::pair<size_t, size_t>> pending;
  for (size_t i = 0; i < nodes_.size(); ++i)
    if (nodes_[i].parent.empty()) pending.emplace_back(i, 0);
  size_t reached = 0;
  while (!pending.empty()) {
    const auto [index, depth] = pending.back();
    pending.pop_back();
    if (depth > kMaxDepth || (!nodes_[index].folder && !children_[index].empty()))
      return false;
    ++reached;
    for (const auto child : children_[index]) pending.emplace_back(child, depth + 1);
  }
  // A cycle or an orphan cannot be reached from any root. Each unique ID has
  // at most one parent, so a rooted traversal cannot revisit a node.
  return reached == nodes_.size();
}

bool PinTree::CanPlace(std::string_view id, std::string_view parent) const {
  if (!unique_ids_ || id.empty()) return false;
  size_t depth = 0;
  while (!parent.empty()) {
    if (parent == id || ++depth > kMaxDepth) return false;
    const auto ancestor = by_id_.find(parent);
    if (ancestor == by_id_.end() || !nodes_[ancestor->second].folder) return false;
    parent = nodes_[ancestor->second].parent;
  }
  const auto root = by_id_.find(id);
  if (root == by_id_.end()) return true;  // A new leaf or empty folder.
  std::vector<std::pair<size_t, size_t>> pending{{root->second, depth}};
  std::vector<bool> visited(nodes_.size());
  while (!pending.empty()) {
    const auto [index, level] = pending.back();
    pending.pop_back();
    if (visited[index] || level > kMaxDepth) return false;
    visited[index] = true;
    for (const auto child : children_[index]) pending.emplace_back(child, level + 1);
  }
  return true;
}

std::vector<size_t> PinTree::Subtree(std::string_view id) const {
  const auto root = by_id_.find(id);
  if (!unique_ids_ || root == by_id_.end()) return {};
  std::vector<size_t> pending{root->second};
  std::vector<bool> visited(nodes_.size());
  while (!pending.empty()) {
    const size_t index = pending.back();
    pending.pop_back();
    if (visited[index]) continue;
    visited[index] = true;
    pending.insert(pending.end(), children_[index].begin(), children_[index].end());
  }
  std::vector<size_t> result;
  for (size_t i = 0; i < nodes_.size(); ++i) if (visited[i]) result.push_back(i);
  return result;
}
}  // namespace zitter
