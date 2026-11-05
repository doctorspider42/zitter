#include "zitter/core/command_history.h"

#include <algorithm>
#include <cmath>

namespace zitter {
namespace {
constexpr double kWeekMs = 7.0 * 24 * 60 * 60 * 1000;
}

CommandHistory::CommandHistory() = default;
CommandHistory::~CommandHistory() = default;
CommandHistory::CommandHistory(const CommandHistory&) = default;
CommandHistory& CommandHistory::operator=(const CommandHistory&) = default;
CommandHistory::CommandHistory(CommandHistory&&) = default;
CommandHistory& CommandHistory::operator=(CommandHistory&&) = default;

bool CommandHistory::KeyLess::operator()(
    const std::pair<std::string_view, std::string_view>& left,
    const std::pair<std::string_view, std::string_view>& right) const {
  return left < right;
}

double CommandHistory::Score(std::string_view id, std::string_view target,
                             double now_ms) const {
  if (!std::isfinite(now_ms)) return 0;
  const auto found = entries_.find(std::pair{id, target});
  if (found == entries_.end()) return 0;
  // Weekly half-life; a clock rollback must not amplify a saved weight.
  const double weeks = std::max(0.0, now_ms - found->second.last_used_ms) / kWeekMs;
  const double score = found->second.weight * std::exp2(-weeks);
  return score >= 0.1 ? score : 0;
}

void CommandHistory::Record(std::string id, std::string target, double now_ms) {
  if (!std::isfinite(now_ms)) return;
  const double weight = std::min(64.0, Score(id, target, now_ms) + 1);
  entries_[{std::move(id), std::move(target)}] = {weight, now_ms};
  if (entries_.size() > kCapacity) {
    const auto oldest = std::min_element(entries_.begin(), entries_.end(),
        [](const auto& a, const auto& b) {
          return a.second.last_used_ms < b.second.last_used_ms;
        });
    entries_.erase(oldest);
  }
}

bool CommandHistory::Restore(std::string id, std::string target, CommandUsage usage) {
  if (id.empty() || id.size() > 128 || target.size() > 256 ||
      !std::isfinite(usage.weight) || usage.weight <= 0 || usage.weight > 64 ||
      !std::isfinite(usage.last_used_ms) || usage.last_used_ms <= 0) return false;
  const auto key = std::pair{std::move(id), std::move(target)};
  if (entries_.size() >= kCapacity && !entries_.contains(key)) return false;
  entries_[key] = usage;
  return true;
}
}  // namespace zitter
