#ifndef ZITTER_CORE_COMMAND_HISTORY_H_
#define ZITTER_CORE_COMMAND_HISTORY_H_

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace zitter {

// Milliseconds since Unix epoch at the adapter boundary; no platform clock.
struct CommandUsage {
  double weight = 0;
  double last_used_ms = 0;
};

class CommandHistory {
 public:
  CommandHistory();
  ~CommandHistory();
  CommandHistory(const CommandHistory&);
  CommandHistory& operator=(const CommandHistory&);
  CommandHistory(CommandHistory&&);
  CommandHistory& operator=(CommandHistory&&);

  struct KeyLess {
    using is_transparent = void;
    bool operator()(const std::pair<std::string_view, std::string_view>& left,
                    const std::pair<std::string_view, std::string_view>& right) const;
  };
  using Entries = std::map<std::pair<std::string, std::string>, CommandUsage, KeyLess>;
  static constexpr size_t kCapacity = 256;

  double Score(std::string_view id, std::string_view target, double now_ms) const;
  void Record(std::string id, std::string target, double now_ms);
  // Restore persisted entries without applying decay or changing their order.
  bool Restore(std::string id, std::string target, CommandUsage usage);
  const Entries& entries() const { return entries_; }
  bool empty() const { return entries_.empty(); }

 private:
  Entries entries_;
};

}  // namespace zitter
#endif
