#ifndef ZITTER_CORE_PIN_TREE_H_
#define ZITTER_CORE_PIN_TREE_H_

#include <cstddef>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace zitter {

// Borrowed metadata only. URLs, titles, pages and persistence belong to adapters.
struct PinNode {
  std::string_view id;
  std::string_view parent;
  bool folder = false;
};

// A short-lived index. The input and its strings must outlive this object.
class PinTree {
 public:
  static constexpr size_t kMaxDepth = 64;
  explicit PinTree(std::span<const PinNode> nodes);
  ~PinTree();
  // Admission check for a complete snapshot, before any UI sees it.
  bool IsValid() const;
  // Checks reachability, cycles and the depth of the entire moved subtree.
  bool CanPlace(std::string_view id, std::string_view parent) const;
  // Includes the root; indices follow input order, independent of tree order.
  std::vector<size_t> Subtree(std::string_view id) const;

 private:
  std::span<const PinNode> nodes_;
  std::unordered_map<std::string_view, size_t> by_id_;
  std::vector<std::vector<size_t>> children_;
  bool unique_ids_ = true;
};

}  // namespace zitter
#endif
