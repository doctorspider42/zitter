// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// Portable raster path for the Rust UI: adapters paint their Rust scene recipes
// through the Chromium compositor. The macOS Metal overlay is an opt-in
// alternative (--zitter-rust-raster=metal).
#include "chromium/prototype/sidebar_metal.h"
#include "chromium/prototype/i18n.h"
#include "chromium/prototype/sidebar_scene.rs.h"
#include <memory>
#include <vector>
#include <algorithm>
#include <cmath>
#include "base/command_line.h"
#include "base/i18n/rtl.h"
#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
#include "base/timer/timer.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animator.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/gfx/animation/animation.h"
#include "base/task/single_thread_task_runner.h"
#include "build/build_config.h"
#include "cc/paint/paint_flags.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/scrollbar/base_scroll_bar_thumb.h"
#include "ui/views/controls/scrollbar/scroll_bar.h"

namespace zitter::prototype {
namespace {
class SidebarScrollBar;
class SidebarThumb final : public views::BaseScrollBarThumb {
 public:
  SidebarThumb(views::ScrollBar* bar, base::RepeatingCallback<bool()> scene_visible)
      : BaseScrollBarThumb(bar), scene_visible_(std::move(scene_visible)) {}
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override { return IsHorizontal() ? gfx::Size(20,15) : gfx::Size(15,20); }
  unsigned color() const {
    const auto state = GetState();
    return GetColorProvider()->GetColor(state == views::Button::STATE_PRESSED
        ? ui::kColorWebNativeControlScrollbarThumbPressed : state == views::Button::STATE_HOVERED
        ? ui::kColorWebNativeControlScrollbarThumbHovered : ui::kColorWebNativeControlScrollbarThumb);
  }
  void OnPaint(gfx::Canvas* canvas) override {
    if (scene_visible_ && scene_visible_.Run()) return;
    sidebar::Item item{}; item.kind=6; item.tile=IsHorizontal(); item.width=width(); item.height=height(); item.fill=color();
    item.clip={0,0,float(width()),float(height())};
    const auto ops=sidebar::layout(rust::Slice<const sidebar::Item>(&item,1));
    for (const auto& op : ops) {
      cc::PaintFlags flags; flags.setAntiAlias(true); flags.setColor(op.color);
      canvas->DrawRoundRect(gfx::RectF(op.x,op.y,op.width,op.height),op.radius,flags);
    }
  }
  bool OnMousePressed(const ui::MouseEvent& event) override;
  bool OnMouseDragged(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override { drag_->end(); BaseScrollBarThumb::OnMouseReleased(event); }
  void OnMouseCaptureLost() override { drag_->end(); BaseScrollBarThumb::OnMouseCaptureLost(); }
 private:
  rust::Box<sidebar::ScrollThumb> drag_=sidebar::new_scroll_thumb();
  base::RepeatingCallback<bool()> scene_visible_;
};
class SidebarScrollBar final : public views::ScrollBar {
 public:
  SidebarScrollBar(base::RepeatingCallback<bool()> scene_visible, bool horizontal)
      : ScrollBar(horizontal ? Orientation::kHorizontal : Orientation::kVertical) {
    SetThumb(new SidebarThumb(this,std::move(scene_visible)));
    GetViewAccessibility().SetName(horizontal ? T(u"Przewijanie przestrzeni") : T(u"Przewijanie kart"));
    GetViewAccessibility().AddAction(ax::mojom::Action::kIncrement);
    GetViewAccessibility().AddAction(ax::mojom::Action::kDecrement);
  }
  int GetThickness() const override { return 15; }
  gfx::Rect GetTrackBounds() const override { return GetLocalBounds(); }
  void Update(int viewport, int content, int offset) override {
    viewport_=viewport; content_=content; offset_=offset;
    ScrollBar::Update(viewport,content,offset); SyncThumb();
  }
  void Layout(PassKey) override { SyncThumb(); }
  bool HandleAccessibleAction(const ui::AXActionData& action) override {
    if (action.action==ax::mojom::Action::kIncrement) return ScrollByAmount(ScrollAmount::kNextLine);
    if (action.action==ax::mojom::Action::kDecrement) return ScrollByAmount(ScrollAmount::kPrevLine);
    return ScrollBar::HandleAccessibleAction(action);
  }
  void Begin(sidebar::ScrollThumb& drag, float pointer) {
    drag.begin(Length(),viewport_,content_,offset_,pointer);
  }
  void Drag(const sidebar::ScrollThumb& drag, float pointer, float cross, bool snap) {
    ScrollToPosition(drag.drag(Length(),viewport_,content_,offset_,pointer,cross,GetThickness(),snap));
  }
  SidebarScrollBarSnapshot Snapshot() const {
    return {GetBoundsInScreen(),GetThumb()->GetBoundsInScreen(),
        static_cast<SidebarThumb*>(GetThumb())->color(),IsDrawn(),GetOrientation()==Orientation::kHorizontal};
  }
 protected:
  void ScrollToPosition(int position) override {
    offset_=sidebar::scroll_metrics(Length(),viewport_,content_,position).offset;
    ScrollBar::ScrollToPosition(offset_);
  }
 private:
  void SyncThumb() {
    const auto g=sidebar::scroll_metrics(Length(),viewport_,content_,offset_);
    offset_=g.offset;
    if (GetOrientation()==Orientation::kHorizontal) GetThumb()->SetBounds(g.position,0,g.length,height());
    else GetThumb()->SetBounds(0,g.position,width(),g.length);
    auto& ax=GetViewAccessibility(); ax.SetMinValueForRange(0); ax.SetMaxValueForRange(g.max_offset); ax.SetValueForRange(g.offset);
  }
  int Length() const { return GetOrientation()==Orientation::kHorizontal ? width() : height(); }
  int viewport_=0,content_=0,offset_=0;
};
bool SidebarThumb::OnMousePressed(const ui::MouseEvent& event) {
  if (!event.IsOnlyLeftMouseButton() || !GetEnabledInViewsSubtree()) return false;
  static_cast<SidebarScrollBar*>(scroll_bar())->Begin(*drag_,IsHorizontal() ? x()+GetMirroredXInView(event.x()) : y()+event.y());  // Logical track position (RTL-mirrored).
  SetState(views::Button::STATE_PRESSED); return true;
}
bool SidebarThumb::OnMouseDragged(const ui::MouseEvent& event) {
  if (!GetEnabledInViewsSubtree()) { drag_->end(); return false; }
  static_cast<SidebarScrollBar*>(scroll_bar())->Drag(*drag_,IsHorizontal() ? x()+GetMirroredXInView(event.x()) : y()+event.y(),IsHorizontal() ? event.y() : event.x(),GetSnapBackOnDragOutside());
  return true;
}
}  // namespace
std::unique_ptr<views::ScrollBar> CreateSidebarScrollBar(base::RepeatingCallback<bool()> scene_visible, bool horizontal) {
  return std::make_unique<SidebarScrollBar>(std::move(scene_visible),horizontal);
}
SidebarScrollBarSnapshot GetSidebarScrollBarSnapshot(views::ScrollBar* bar) {
  return static_cast<SidebarScrollBar*>(bar)->Snapshot();
}
namespace {
// Design: Overlay scrollbar. The bar is a 14 px hit strip over the content's
// edge; only the pill inside it is painted.
constexpr int kPanelBarHit = 14;
constexpr int kPanelThumbRest = 4;
constexpr int kPanelThumbHover = 8;
constexpr int kPanelEdgeRest = 2;
constexpr int kPanelEdgeHover = 3;
constexpr int kPanelEnds = 4;
constexpr int kPanelThumbMinimum = 32;
constexpr auto kPanelFadeIn = base::Milliseconds(120);
constexpr auto kPanelFadeOut = base::Milliseconds(220);
constexpr auto kPanelHideDelay = base::Milliseconds(800);
base::RepeatingCallback<PanelScrollBarColors(const views::View*)>& PanelColorSource() {
  static base::NoDestructor<base::RepeatingCallback<PanelScrollBarColors(const views::View*)>> source;
  return *source;
}
PanelScrollBarColors PanelColors(const views::View* view) {
  auto& source = PanelColorSource();
  return source ? source.Run(view) : PanelScrollBarColors();
}
SkColor PanelAlpha(unsigned color, float alpha) {
  return SkColorSetA(color, static_cast<U8CPU>(std::lround(255 * std::clamp(alpha, 0.f, 1.f))));
}
class PanelOverlayScrollBar;
class PanelOverlayThumb final : public views::BaseScrollBarThumb {
 public:
  explicit PanelOverlayThumb(PanelOverlayScrollBar* bar);
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override {
    return IsHorizontal() ? gfx::Size(kPanelThumbMinimum, kPanelBarHit) : gfx::Size(kPanelBarHit, kPanelThumbMinimum);
  }
  void OnPaint(gfx::Canvas* canvas) override;
  void OnBoundsChanged(const gfx::Rect& previous) override;
  void OnStateChanged() override;
  bool IsActive() const { return GetState() != views::Button::STATE_NORMAL; }
 private:
  raw_ptr<PanelOverlayScrollBar> bar_;
};
class PanelOverlayScrollBar final : public views::ScrollBar {
 public:
  explicit PanelOverlayScrollBar(bool horizontal)
      : ScrollBar(horizontal ? Orientation::kHorizontal : Orientation::kVertical) {
    SetNotifyEnterExitOnChild(true);
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    layer()->SetOpacity(0.f);
    SetThumb(new PanelOverlayThumb(this));
  }
  int GetThickness() const override { return kPanelBarHit; }
  bool OverlapsContent() const override { return true; }
  gfx::Rect GetTrackBounds() const override {
    auto track = GetLocalBounds();
    track.Inset(IsHorizontal() ? gfx::Insets::VH(0, kPanelEnds) : gfx::Insets::VH(kPanelEnds, 0));
    return track;
  }
  // A faded-out bar never takes clicks meant for the content beneath it.
  bool GetCanProcessEventsWithinSubtree() const override {
    return shown_ && ScrollBar::GetCanProcessEventsWithinSubtree();
  }
  void OnMouseEntered(const ui::MouseEvent&) override { Show(); SchedulePaint(); GetThumb()->SchedulePaint(); }
  void OnMouseExited(const ui::MouseEvent&) override { StartHideCountdown(); SchedulePaint(); GetThumb()->SchedulePaint(); }
  void OnPaint(gfx::Canvas* canvas) override {
    if (!Wide()) return;
    // Hover: the track gets a 6 % ink tint, as wide as the grown pill.
    const auto colors = PanelColors(this);
    cc::PaintFlags flags; flags.setAntiAlias(true); flags.setColor(PanelAlpha(colors.ink, 0.06f));
    canvas->DrawRoundRect(gfx::RectF(Pill(GetTrackBounds())), kPanelThumbHover / 2.f, flags);
  }
  bool Wide() const {
    return IsMouseHovered() || static_cast<PanelOverlayThumb*>(GetThumb())->IsActive();
  }
  // The pill inside `area` (the thumb or the track), flush with the far edge.
  gfx::Rect Pill(gfx::Rect area) const {
    const bool wide = Wide();
    const int thickness = wide ? kPanelThumbHover : kPanelThumbRest;
    const int edge = wide ? kPanelEdgeHover : kPanelEdgeRest;
    if (IsHorizontal()) return gfx::Rect(area.x(), area.bottom() - edge - thickness, area.width(), thickness);
    const int x = base::i18n::IsRTL() ? area.x() + edge : area.right() - edge - thickness;
    return gfx::Rect(x, area.y(), thickness, area.height());
  }
  void Show() {
    hide_timer_.Stop();
    if (shown_) return;
    shown_ = true;
    Fade(1.f, kPanelFadeIn);
  }
  void StartHideCountdown() {
    if (Wide()) return;
    hide_timer_.Start(FROM_HERE, kPanelHideDelay, base::BindOnce(&PanelOverlayScrollBar::Hide, base::Unretained(this)));
  }
 private:
  bool IsHorizontal() const { return GetOrientation() == Orientation::kHorizontal; }
  void Hide() {
    if (!shown_) return;
    shown_ = false;
    Fade(0.f, kPanelFadeOut);
  }
  void Fade(float opacity, base::TimeDelta duration) {
    ui::ScopedLayerAnimationSettings settings(layer()->GetAnimator());
    settings.SetTransitionDuration(gfx::Animation::PrefersReducedMotion() ? base::TimeDelta() : duration);
    settings.SetPreemptionStrategy(ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
    layer()->SetOpacity(opacity);
  }
  bool shown_ = false;
  base::OneShotTimer hide_timer_;
};
PanelOverlayThumb::PanelOverlayThumb(PanelOverlayScrollBar* bar) : BaseScrollBarThumb(bar), bar_(bar) {}
void PanelOverlayThumb::OnPaint(gfx::Canvas* canvas) {
  const auto colors = PanelColors(this);
  const bool pressed = GetState() == views::Button::STATE_PRESSED;
  const bool wide = bar_->Wide();
  // Night: ink at 32 % / 50 %; Paper needs a little more to hold 3:1.
  const float alpha = wide ? (colors.dark ? 0.5f : 0.55f) : (colors.dark ? 0.32f : 0.36f);
  cc::PaintFlags flags; flags.setAntiAlias(true);
  // Dragging takes the space accent, so the pill reads as held.
  flags.setColor(pressed ? SkColor(colors.accent) : PanelAlpha(colors.ink, alpha));
  const auto pill = bar_->Pill(GetLocalBounds());
  canvas->DrawRoundRect(gfx::RectF(pill), std::min(pill.width(), pill.height()) / 2.f, flags);
}
void PanelOverlayThumb::OnBoundsChanged(const gfx::Rect& previous) {
  if (previous.size() == size() && previous.origin() == origin()) return;
  bar_->Show();
  bar_->StartHideCountdown();
}
void PanelOverlayThumb::OnStateChanged() {
  if (GetState() == views::Button::STATE_NORMAL) bar_->StartHideCountdown();
  else bar_->Show();
  bar_->SchedulePaint();
  SchedulePaint();
}
}  // namespace
void SetPanelScrollBarColors(base::RepeatingCallback<PanelScrollBarColors(const views::View*)> colors) {
  PanelColorSource() = std::move(colors);
}
std::unique_ptr<views::ScrollBar> CreatePanelOverlayScrollBar(bool horizontal) {
  return std::make_unique<PanelOverlayScrollBar>(horizontal);
}
// Keeps Rust viewport projection but never presents; adapters see an invisible
// surface and paint their Rust scene recipe through the Chromium compositor.
class CompositorSurface final : public SidebarMetal {
 public:
  SidebarViewportProjection Project(gfx::Size content, gfx::Size size, gfx::PointF offset,
      base::span<const SidebarViewportNode> nodes) override {
    std::vector<sidebar::ViewportNode> input;
    input.reserve(nodes.size());
    for (const auto& node : nodes) input.push_back({node.parent,
        {float(node.bounds.x()), float(node.bounds.y()), float(node.bounds.width()), float(node.bounds.height())}, node.visible});
    const auto result = viewport_->project(content.width(), content.height(), size.width(), size.height(),
        offset.x(), offset.y(), rust::Slice<const sidebar::ViewportNode>(input.data(), input.size()));
    SidebarViewportProjection out;
    out.offset = gfx::PointF(result.x, result.y); out.nodes.reserve(result.nodes.size());
    for (const auto& node : result.nodes) out.nodes.push_back({node.index,
        gfx::RectF(node.bounds.x, node.bounds.y, node.bounds.width, node.bounds.height),
        gfx::RectF(node.clip.x, node.clip.y, node.clip.width, node.clip.height)});
    return out;
  }
  void Update(gfx::NativeWindow, gfx::Rect, base::span<const sidebar::Item>, unsigned) override {}
  void UpdateScene(gfx::NativeWindow, gfx::Rect, base::span<const sidebar::Primitive>, unsigned) override {}
  void UpdateUnderlay(gfx::NativeWindow, gfx::Rect, base::span<const sidebar::Primitive>) override {}
  bool underlay_order_correct_for_testing() const override { return true; }
  void UpdatePreview(gfx::NativeWindow, gfx::Rect, double, base::span<const sidebar::Item>, unsigned, bool, bool) override {}
  void SetNextPresentationCallback(base::OnceClosure) override {}
  void AnimatePreview(double, base::TimeDelta, base::OnceClosure done) override { if (done) base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, std::move(done)); }
  void Hide() override {}
  bool visible() const override { return false; }
  size_t frame_count() const override { return 0; }
  bool contains_text_for_testing(std::string_view) const override { return false; }
  gfx::Rect screen_bounds_for_testing() const override { return {}; }
  bool contains_rect_for_testing(gfx::RectF, gfx::RectF) const override { return false; }
 private:
  rust::Box<sidebar::Viewport> viewport_ = sidebar::new_viewport();
};
#if !BUILDFLAG(IS_MAC)
SidebarMetalStats GetSidebarMetalStatsForTesting() { return {}; }
#endif
std::unique_ptr<SidebarMetal> CreateSidebarMetal() {
  // Adapters paint their Rust recipes in the Chromium compositor by default;
  // --zitter-rust-raster=metal opts into the in-process Metal overlay.
  // Never null: an unavailable Metal device falls back to the compositor.
#if BUILDFLAG(IS_MAC)
  if (base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII("zitter-rust-raster") == "metal")
    if (auto surface = CreateMetalSidebarSurface()) return surface;
#endif
  return std::make_unique<CompositorSurface>();
}
}  // namespace zitter::prototype
