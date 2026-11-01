// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_PROTOTYPE_TIME_MACHINE_VIEW_H_
#define ZITTER_CHROMIUM_PROTOTYPE_TIME_MACHINE_VIEW_H_
#include <cstdint>
#include <string>
#include <vector>
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include <array>
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/compositor/compositor_animation_observer.h"
#include "ui/views/view.h"
namespace ui { class Compositor; }
namespace zitter::sidebar { struct DesignPalette; struct Timeline; }
namespace zitter::prototype {

// Time Machine (design: Main.dc.html on the Zitter Time Machine canvas): the active
// workspace's recorded tabs. A day line and a rolling clock name the moment, a
// cover-flow deck shows every tab open in the hour before it (the hovered card
// turns to the front) and the axis below scrubs through the recorded week. Motion runs on
// springs stepped every frame, so a new target bends the movement instead of
// restarting it.
class TimeMachineView final : public views::View, public ui::CompositorAnimationObserver {
  METADATA_HEADER(TimeMachineView, views::View)

 public:
  struct Host {
    Host();
    Host(Host&&);
    Host& operator=(Host&&);
    ~Host();
    base::RepeatingCallback<const sidebar::Timeline*()> timeline;
    base::RepeatingCallback<base::Time()> now;
    base::RepeatingCallback<sidebar::DesignPalette()> palette;
    // Whether motion is wanted (the system does not ask for reduced motion).
    base::RepeatingCallback<bool()> animations;
    base::RepeatingCallback<bool(int64_t, int64_t)> restore;
    base::RepeatingClosure enable, close;
  };
  // The deck shows every tab open within this span before the playhead.
  static constexpr int64_t kQuantumMs = 60 * 60 * 1000;

  explicit TimeMachineView(Host host);
  TimeMachineView(const TimeMachineView&) = delete;
  TimeMachineView& operator=(const TimeMachineView&) = delete;
  ~TimeMachineView() override;

  // Shows `workspace_id` at its latest moment.
  void Open(std::string workspace_id, std::u16string workspace_name, bool enabled);
  // Moves the playhead; `snap` pulls it to a change within a few minutes.
  void SetMoment(int64_t t_ms, bool snap);
  void Step(bool forward);
  void Restore();
  // The card under the pointer or keyboard; -1 returns to the active tab.
  void FocusCard(int index);
  // The card drawn at `point` (view coordinates), nearest first; -1 if none.
  int CardAt(const gfx::Point& point) const;
  void SetDragging(bool dragging);
  // Scrolling the axis moves its window by `dx` pixels (positive: later);
  // zooming scales it by `factor` around `x` (negative `x`: the playhead).
  void Pan(float dx);
  void Zoom(float factor, float x);

  const sidebar::Timeline* timeline() const;
  const std::string& workspace_id() const { return workspace_id_; }
  int64_t moment() const { return t_ms_; }
  int64_t now_ms() const;
  int64_t window_start() const { return window_start_; }
  int64_t window_end() const { return window_end_; }
  sidebar::DesignPalette palette() const;
  bool animations() const;
  std::u16string value_text() const { return value_text_; }

  size_t card_count_for_testing() const;
  int front_card_for_testing() const { return front_; }
  bool animating_for_testing() const { return ticking_ != nullptr; }
  std::u16string restore_label_for_testing() const;
  views::View* axis_for_testing() const;
  views::View* deck_for_testing() const;

  // views::View:
  void Layout(PassKey) override;
  void OnPaint(gfx::Canvas* canvas) override;
  bool OnKeyPressed(const ui::KeyEvent& event) override;
  void OnThemeChanged() override;
  void RemovedFromWidget() override;

  // ui::CompositorAnimationObserver:
  void OnAnimationStep(base::TimeTicks timestamp) override;
  void OnCompositingShuttingDown(ui::Compositor* compositor) override;

 private:
  // A card's motion: `slot` slides to its place in the deck, `appear` swings it
  // in (0 → 1) or, leaving, out; `delay` staggers a new set's entrance.
  struct CardMotion {
    raw_ptr<views::View> view;
    std::string url;
    int index = 0;
    float slot = 0, slot_velocity = 0, appear = 0, appear_velocity = 0, delay = 0;
    bool leaving = false;
  };
  void Update(bool animate);
  int64_t first_ms() const;
  int FrontIndex() const;
  // Starts stepping the springs each frame, or settles them at once without motion.
  void Animate();
  void Settle();
  void StopTicking();
  // Advances every spring by `dt` seconds; true once all are at rest.
  bool Advance(float dt);
  void PlaceCards();

  Host host_;
  std::string workspace_id_;
  std::u16string workspace_name_;
  bool enabled_ = true;
  bool dragging_ = false;
  int64_t t_ms_ = 0;
  int64_t window_start_ = 0, window_end_ = 0;
  // Card focused by hover or keys (URL), empty for the active tab.
  std::string focus_url_;
  int front_ = 0, active_ = 0;
  std::vector<std::string> card_urls_;
  std::vector<CardMotion> cards_;
  std::vector<views::View*> paint_order_;
  float deck_position_ = 0, deck_velocity_ = 0;
  std::array<float, 4> reel_position_{}, reel_velocity_{};
  std::array<int, 4> reel_target_{};
  raw_ptr<ui::Compositor> ticking_ = nullptr;
  base::TimeTicks last_tick_;
  std::u16string day_line_, range_, value_text_, size_;
  int digit_size_ = 200;
  gfx::Rect clock_, colon_, deck_bounds_, caption_;
  std::vector<raw_ptr<views::View>> reels_;
  raw_ptr<views::View> deck_ = nullptr;
  raw_ptr<views::View> axis_ = nullptr;
  raw_ptr<views::View> restore_ = nullptr;
  raw_ptr<views::View> enable_ = nullptr;
  raw_ptr<views::View> close_ = nullptr;
};

}  // namespace zitter::prototype
#endif  // ZITTER_CHROMIUM_PROTOTYPE_TIME_MACHINE_VIEW_H_
