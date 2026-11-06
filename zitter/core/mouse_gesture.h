#ifndef ZITTER_CORE_MOUSE_GESTURE_H_
#define ZITTER_CORE_MOUSE_GESTURE_H_

#include <algorithm>
#include <cmath>
#include <string>

namespace zitter {
// Coordinates are device-independent pixels; time is monotonic seconds.
// A recognizer owns one explicit Alt + secondary-button drag, never a click.
class MouseGesture {
 public:
  void Begin(float x, float y, double now) {
    active_ = true; valid_ = true; x_ = x; y_ = y; started_ = now; path_.clear();
  }
  bool active() const { return active_; }
  void Cancel() { valid_ = false; }
  void Move(float x, float y, double now) {
    if (!active_ || !valid_) return;
    if (now - started_ > 2.5 || now < started_) { Cancel(); return; }
    const float dx = x - x_, dy = y - y_;
    const float ax = std::abs(dx), ay = std::abs(dy);
    // Ignore hand jitter; require a decisive cardinal movement at every turn.
    if (std::max(ax, ay) < (path_.empty() ? 48 : 32)) return;
    if (std::max(ax, ay) < std::min(ax, ay) * 1.5f) { Cancel(); return; }
    const char direction = ax > ay ? (dx < 0 ? 'L' : 'R') : (dy < 0 ? 'U' : 'D');
    if (path_.empty() || path_.back() != direction) path_ += direction;
    if (path_.size() > 3) Cancel();
    x_ = x; y_ = y;
  }
  std::string End(float x, float y, double now) {
    if (!active_) return {};
    Move(x, y, now); active_ = false;
    return valid_ ? path_ : std::string();
  }
 private:
  bool active_ = false, valid_ = false;
  float x_ = 0, y_ = 0;
  double started_ = 0;
  std::string path_;
};
}  // namespace zitter
#endif
