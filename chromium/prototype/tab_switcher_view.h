// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_PROTOTYPE_TAB_SWITCHER_VIEW_H_
#define ZITTER_CHROMIUM_PROTOTYPE_TAB_SWITCHER_VIEW_H_
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/compositor/compositor_animation_observer.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/view.h"
#include "url/gurl.h"
namespace ui { class Compositor; }
namespace zitter::sidebar { struct DesignPalette; }
namespace zitter::prototype {

class TabSwitcherCard;

// Ctrl+Tab (design: Main.dc.html on the Ctrl+Tab Switcher canvas): the active
// workspace's tabs, most recently used first, stacked into the depth like Time
// Machine. The front card is the one Ctrl's release opens; every Tab pushes it
// forward out of view and pulls the next one from the stack. The time line on
// the right shows how far back the selection is. A tap shorter than
// `kRevealDelay` switches without showing anything.
class TabSwitcherView final : public views::View, public ui::CompositorAnimationObserver {
  METADATA_HEADER(TabSwitcherView, views::View)

 public:
  struct Card {
    Card();
    Card(const Card&);
    Card& operator=(const Card&);
    ~Card();
    uint64_t id = 0;
    std::u16string title;
    GURL url;
    // 32×32 premultiplied pixels (ShellState::favicons), empty without one.
    std::vector<uint32_t> favicon;
    gfx::ImageSkia thumbnail;
    // When the tab was last left; null when unknown.
    base::Time used;
  };
  struct Host {
    Host();
    Host(Host&&);
    Host& operator=(Host&&);
    ~Host();
    base::RepeatingCallback<sidebar::DesignPalette()> palette;
    base::RepeatingCallback<bool()> animations;
    // Whether Ctrl (the switcher's modifier) is still held.
    base::RepeatingCallback<bool()> modifier_down;
    // The widget to show the overlay in, once the delay passes.
    base::RepeatingClosure reveal;
    // The chosen tab, or nullopt when the switcher was dismissed.
    base::RepeatingCallback<void(std::optional<uint64_t>)> done;
  };
  static constexpr base::TimeDelta kRevealDelay = base::Milliseconds(180);
  // At most this many cards stand in the stack behind the front one.
  static constexpr int kDepth = 4;

  explicit TabSwitcherView(Host host);
  TabSwitcherView(const TabSwitcherView&) = delete;
  TabSwitcherView& operator=(const TabSwitcherView&) = delete;
  ~TabSwitcherView() override;

  // Starts a switch over `cards` (index 0 is the current tab) at `selected`.
  void Open(std::vector<Card> cards, int selected);
  // Tab (older) or Shift+Tab (newer); wraps around.
  void Step(bool backwards);
  void Select(int index);
  void Commit();
  void Cancel();
  // Shows the overlay now (the reveal delay passed or the modifier is held).
  void Reveal();
  void SetThumbnail(uint64_t id, const gfx::ImageSkia& image);

  bool active() const { return active_; }
  bool revealed() const { return revealed_; }
  int selected() const { return selected_; }
  size_t card_count() const { return cards_.size(); }
  const Card& card(int index) const { return cards_[index]; }
  sidebar::DesignPalette palette() const;
  // The card's rectangle (unscaled, at depth 0) in view coordinates.
  const gfx::Rect& card_bounds() const { return card_bounds_; }
  bool animating_for_testing() const { return ticking_ != nullptr; }
  std::u16string when_for_testing(int index) const { return When(index); }

  // views::View:
  void Layout(PassKey) override;
  void OnPaint(gfx::Canvas* canvas) override;
  bool SkipDefaultKeyEventProcessing(const ui::KeyEvent& event) override;
  bool OnKeyPressed(const ui::KeyEvent& event) override;
  bool OnKeyReleased(const ui::KeyEvent& event) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;
  void OnMouseMoved(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  bool OnMouseWheel(const ui::MouseWheelEvent& event) override;
  void OnThemeChanged() override;
  void RemovedFromWidget() override;

  // ui::CompositorAnimationObserver:
  void OnAnimationStep(base::TimeTicks timestamp) override;
  void OnCompositingShuttingDown(ui::Compositor* compositor) override;

 private:
  void Finish(std::optional<uint64_t> id);
  void RebuildCards();
  void PlaceCards();
  void Animate();
  void StopTicking();
  void OnRevealTimer();
  // The card drawn at `point`, front-most first; -1 if none.
  int CardAt(const gfx::Point& point) const;
  // The time line's tick at `point`; -1 if none.
  int TickAt(const gfx::Point& point) const;
  gfx::Rect TickBounds(int index) const;
  std::u16string When(int index) const;
  float scale() const;

  Host host_;
  std::vector<Card> cards_;
  std::vector<raw_ptr<TabSwitcherCard>> views_;
  int selected_ = 0;
  int hovered_tick_ = -1;
  int pressed_card_ = -1;
  bool active_ = false;
  bool revealed_ = false;
  // The stack's position (fractional while it moves) and the spring's speed.
  float position_ = 0, velocity_ = 0;
  base::Time opened_at_;
  base::OneShotTimer reveal_timer_;
  raw_ptr<ui::Compositor> ticking_ = nullptr;
  base::TimeTicks last_tick_;
  gfx::Rect card_bounds_, caption_, timeline_;
};

}  // namespace zitter::prototype
#endif  // ZITTER_CHROMIUM_PROTOTYPE_TAB_SWITCHER_VIEW_H_
