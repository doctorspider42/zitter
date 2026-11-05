#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "zitter/core/browser_state.h"
#include "zitter/core/command_history.h"
#include "zitter/core/pin_tree.h"

namespace {
void Expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

void CommandHistoryContracts() {
  constexpr double now = 1800000000000;
  constexpr double week = 7.0 * 24 * 60 * 60 * 1000;
  zitter::CommandHistory history;
  for (int i = 0; i < 4; ++i) history.Record("settings", "", now - week);
  history.Record("history", "", now);
  history.Record("select_space", "work", now);
  Expect(history.Score("settings", "", now) == 2, "weekly decay retains frequency");
  Expect(history.Score("settings", "", now) > history.Score("history", "", now),
         "frequent older command beats a single recent use");
  Expect(history.Score("select_space", "personal", now) == 0,
         "workspace-specific usage stays isolated");
  Expect(history.Score("settings", "", now - 2 * week) == 4,
         "clock rollback never increases a saved weight");
  Expect(history.Score("settings", "", now + 10 * week) == 0,
         "stale usage expires");
  for (int i = 0; i < 100; ++i) history.Record("history", "", now);
  Expect(history.Score("history", "", now) == 64, "frequency is bounded");

  zitter::CommandHistory restored;
  for (const auto& [key, usage] : history.entries())
    Expect(restored.Restore(key.first, key.second, usage), "restore valid persisted usage");
  Expect(restored.Score("settings", "", now) == history.Score("settings", "", now),
         "persistence does not reapply decay");
  Expect(restored.Score("select_space", "work", now) == 1,
         "restored target is preserved");
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double infinity = std::numeric_limits<double>::infinity();
  Expect(!restored.Restore("", "", {1, now}), "reject empty persisted command");
  Expect(!restored.Restore("x", "", {-1, now}), "reject negative weight");
  Expect(!restored.Restore("x", "", {65, now}), "reject excessive weight");
  Expect(!restored.Restore("x", "", {nan, now}), "reject NaN weight");
  Expect(!restored.Restore("x", "", {1, infinity}), "reject infinite timestamp");
  Expect(!restored.Restore("x", "", {1, 0}), "reject absent timestamp");
  Expect(!restored.Restore(std::string(129, 'x'), "", {1, now}), "bound command ID");
  Expect(!restored.Restore("x", std::string(257, 'x'), {1, now}), "bound target ID");
  const auto size = restored.entries().size();
  restored.Record("invalid", "", nan);
  Expect(restored.entries().size() == size && restored.Score("settings", "", nan) == 0,
         "invalid clocks cannot poison ordering");

  zitter::CommandHistory bounded;
  for (size_t i = 0; i <= zitter::CommandHistory::kCapacity; ++i)
    bounded.Record(std::to_string(i), "", now + i);
  Expect(bounded.entries().size() == zitter::CommandHistory::kCapacity, "bound learned history");
  Expect(bounded.Score("0", "", now) == 0 && bounded.Score("256", "", now + 256) == 1,
         "evict oldest use instead of newest");
  Expect(!bounded.Restore("extra", "", {1, now}), "bound restored history");
  Expect(bounded.Restore("256", "", {2, now}), "allow replacing entry at capacity");
}

void PinTreeContracts() {
  // Children may precede parents in persisted vector order.
  const std::vector<zitter::PinNode> nodes{
      {"leaf", "child", false}, {"other", "", false},
      {"child", "root", true}, {"root", "", true}, {"sibling", "root", false}};
  const zitter::PinTree tree(nodes);
  Expect(tree.IsValid(), "admit a complete out-of-order tree");
  Expect(tree.CanPlace("new", "child"), "insert under a reachable folder");
  Expect(tree.CanPlace("root", ""), "keep root placement");
  Expect(!tree.CanPlace("root", "child"), "reject moving folder into descendant");
  Expect(!tree.CanPlace("root", "root"), "reject self-parenting");
  Expect(!tree.CanPlace("new", "other"), "reject a leaf as parent");
  Expect(!tree.CanPlace("new", "missing"), "reject missing parent");
  Expect(tree.Subtree("root") == std::vector<size_t>({0, 2, 3, 4}),
         "removal includes descendants in stable input order");
  Expect(tree.Subtree("child") == std::vector<size_t>({0, 2}), "nested subtree excludes siblings");
  Expect(tree.Subtree("missing").empty(), "missing folder has no removal plan");
  const std::vector<zitter::PinNode> cycle{{"a", "b", true}, {"b", "a", true}};
  const zitter::PinTree cyclic(cycle);
  Expect(!cyclic.IsValid(), "reject cyclic snapshots before publishing them");
  Expect(!cyclic.CanPlace("new", "a"), "terminate on a corrupt ancestor cycle");
  Expect(!cyclic.CanPlace("a", ""), "reject cyclic moved subtree");
  Expect(cyclic.Subtree("a").size() == 2, "cyclic removal visits each item once");
  const std::vector<zitter::PinNode> duplicates{{"a", "", true}, {"a", "", false}};
  Expect(!zitter::PinTree(duplicates).CanPlace("new", ""), "reject ambiguous IDs");
  Expect(zitter::PinTree(duplicates).Subtree("a").empty(), "reject ambiguous removal plans");
  Expect(!zitter::PinTree(duplicates).IsValid(), "reject duplicate snapshot IDs");
  const std::vector<zitter::PinNode> orphan{{"leaf", "missing", false}};
  Expect(!zitter::PinTree(orphan).IsValid(), "reject orphaned snapshot nodes");
  const std::vector<zitter::PinNode> leaf_parent{{"leaf", "", false}, {"child", "leaf", false}};
  Expect(!zitter::PinTree(leaf_parent).IsValid(), "reject snapshot children of a leaf");
  Expect(zitter::PinTree(std::span<const zitter::PinNode>()).IsValid(), "allow an empty catalogue");

  std::vector<std::string> ids;
  ids.reserve(66);
  for (int i = 0; i < 66; ++i) ids.push_back(std::to_string(i));
  std::vector<zitter::PinNode> deep;
  for (size_t i = 0; i < 64; ++i)
    deep.push_back({ids[i], i ? std::string_view(ids[i - 1]) : std::string_view(), true});
  // A folder at depth 64 is visible, but it cannot acquire a child at 65.
  deep.push_back({"branch", "", true});
  deep.push_back({"tip", "branch", false});
  const zitter::PinTree depth(deep);
  Expect(depth.IsValid(), "admit the existing deep tree");
  Expect(depth.CanPlace("new", ids[63]), "permit the renderer's maximum depth");
  Expect(!depth.CanPlace("branch", ids[63]), "include moved descendants in depth check");
  Expect(depth.CanPlace("branch", ids[62]), "allow subtree fitting depth limit");
  auto invalid_deep = deep;
  invalid_deep.back().parent = ids[63];
  invalid_deep.back().folder = true;
  invalid_deep.push_back({"too-deep", "tip", false});
  Expect(!zitter::PinTree(invalid_deep).IsValid(), "reject snapshots beyond the renderer depth bound");
}
}  // namespace

int main() {
  CommandHistoryContracts();
  PinTreeContracts();
  std::cout << "Product logic contracts passed\n";
}
