// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/prototype/time_machine_view.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include "base/byte_size.h"
#include "base/i18n/case_conversion.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "cc/paint/paint_flags.h"
#include "cc/paint/paint_shader.h"
#include "chromium/prototype/browser_tools.h"
#include "chromium/prototype/i18n.h"
#include "chromium/prototype/sidebar_scene.rs.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/text/bytes_formatting.h"
#include "ui/compositor/compositor.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/events/event.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/outsets.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/gfx/shadow_value.h"
#include "ui/gfx/skia_paint_util.h"
#include "ui/gfx/text_elider.h"
#include "ui/gfx/text_utils.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace zitter::prototype {
namespace {

// The design system's families (1 display, 2 sans, 3 mono; design.rs family_id),
// falling back to the system font where the bundled fonts are not active.
gfx::FontList Font(uint32_t family, float size, int weight, bool italic = false) {
  const int weight_value = std::clamp(weight > 0 ? weight : 400, 100, 900) / 100 * 100;
  return gfx::FontList({std::string(sidebar::design_font_family(family))}, italic ? gfx::Font::ITALIC : gfx::Font::NORMAL,
      std::max(1, static_cast<int>(std::lround(size))), static_cast<gfx::Font::Weight>(weight_value));
}
std::u16string Text(const rust::String& value) { return base::UTF8ToUTF16(std::string(value)); }
SkColor WithAlpha(SkColor color, U8CPU alpha) { return SkColorSetA(color, alpha); }
// shadow-float's blur in Noc and Papier (tokens.json).
SkColor FloatShade(const sidebar::DesignPalette& p) {
  return color_utils::GetRelativeLuminance(p.ink) > 0.5f ? SkColorSetARGB(0xa6, 0, 0, 0) : SkColorSetARGB(0x29, 0x16, 0x14, 0x1c);
}
void FillRound(gfx::Canvas* canvas, const gfx::RectF& rect, float radius, SkColor color) {
  cc::PaintFlags flags; flags.setAntiAlias(true); flags.setColor(color);
  canvas->DrawRoundRect(rect, radius, flags);
}
void StrokeRound(gfx::Canvas* canvas, const gfx::RectF& rect, float radius, SkColor color, float width = 1) {
  cc::PaintFlags flags; flags.setAntiAlias(true); flags.setStyle(cc::PaintFlags::kStroke_Style); flags.setStrokeWidth(width); flags.setColor(color);
  gfx::RectF inset = rect; inset.Inset(width / 2);
  canvas->DrawRoundRect(inset, std::max(0.f, radius - width / 2), flags);
}
// A Rust scene (timeline.rs) on a canvas: rounded rects, design-system text and
// the diamond (`timeline::DIAMOND`) of a snapshot mark.
void PaintPrimitives(gfx::Canvas* canvas, const rust::Vec<sidebar::Primitive>& ops) {
  for (const auto& op : ops) {
    gfx::ScopedCanvas clipped(canvas);
    canvas->ClipRect(gfx::RectF(op.clip.x, op.clip.y, op.clip.width, op.clip.height));
    const gfx::RectF rect(op.x, op.y, op.width, op.height);
    if (!op.text.empty()) {
      const auto font = Font(op.family ? op.family : 2, op.font_size, op.font_weight, op.italic);
      const auto text = gfx::ElideText(Text(op.text), font, op.width, gfx::ELIDE_TAIL);
      canvas->DrawStringRectWithFlags(text, font, op.color, gfx::ToEnclosingRect(rect),
          op.center ? gfx::Canvas::TEXT_ALIGN_CENTER : op.right_align ? gfx::Canvas::TEXT_ALIGN_RIGHT : gfx::Canvas::TEXT_ALIGN_LEFT);
      continue;
    }
    cc::PaintFlags flags; flags.setAntiAlias(true); flags.setColor(op.color);
    if (op.stroke > 0) { flags.setStyle(cc::PaintFlags::kStroke_Style); flags.setStrokeWidth(op.stroke); }
    if (op.shape == 20) {
      SkPathBuilder path;
      path.moveTo(rect.CenterPoint().x(), rect.y()); path.lineTo(rect.right(), rect.CenterPoint().y());
      path.lineTo(rect.CenterPoint().x(), rect.bottom()); path.lineTo(rect.x(), rect.CenterPoint().y()); path.close();
      canvas->DrawPath(path.detach(), flags);
      continue;
    }
    canvas->DrawRoundRect(rect, op.radius, flags);
  }
}
void PaintFocus(gfx::Canvas* canvas, const gfx::RectF& rect, float radius, SkColor color) {
  gfx::RectF ring = rect; ring.Outset(4);
  StrokeRound(canvas, ring, radius + 4, color, 2);
}
std::u16string Monogram(const GURL& url, const std::u16string& title) {
  std::string host(url.host());
  if (base::StartsWith(host, "www.")) host = host.substr(4);
  const std::u16string source = host.empty() ? title : base::UTF8ToUTF16(host);
  return source.empty() ? u"·" : base::i18n::ToUpper(source.substr(0, 1));
}
std::u16string DisplayHost(const GURL& url) {
  std::string host(url.host());
  if (base::StartsWith(host, "www.")) host = host.substr(4);
  return base::UTF8ToUTF16(host);
}

// A focusable button painted from the design system: the primary pill with its
// key hint, or the 28 px close icon.
class TimeMachineAction final : public views::View {
  METADATA_HEADER(TimeMachineAction, views::View)
 public:
  enum class Kind { kPrimary, kClose };
  TimeMachineAction(TimeMachineView* root, Kind kind, base::RepeatingClosure pressed)
      : root_(root), kind_(kind), pressed_(std::move(pressed)) {
    SetFocusBehavior(FocusBehavior::ALWAYS);
    GetViewAccessibility().SetRole(ax::mojom::Role::kButton);
    if (kind_ == Kind::kClose) SetLabel(T(u"Zamknij Time Machine"));
  }
  void SetLabel(std::u16string label) {
    label_ = std::move(label); GetViewAccessibility().SetName(label_); PreferredSizeChanged(); SchedulePaint();
  }
  const std::u16string& label() const { return label_; }
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override {
    if (kind_ == Kind::kClose) return gfx::Size(28, 28);
    return gfx::Size(gfx::GetStringWidth(label_, Font(2, 13, 600)) + 18 + 10 + 22 + 18, 36);
  }
  void OnPaint(gfx::Canvas* canvas) override {
    const auto p = root_->palette();
    const gfx::RectF bounds(GetLocalBounds());
    if (kind_ == Kind::kClose) {
      if (hovered_ || HasFocus()) FillRound(canvas, bounds, 8, p.bg_hover);
      cc::PaintFlags flags; flags.setAntiAlias(true); flags.setStyle(cc::PaintFlags::kStroke_Style); flags.setStrokeWidth(1.6f);
      flags.setStrokeCap(cc::PaintFlags::kRound_Cap); flags.setColor(hovered_ ? p.ink : p.ink_muted);
      canvas->DrawLine(gfx::PointF(10, 10), gfx::PointF(18, 18), flags); canvas->DrawLine(gfx::PointF(18, 10), gfx::PointF(10, 18), flags);
      if (HasFocus()) PaintFocus(canvas, bounds, 8, p.focus);
      return;
    }
    FillRound(canvas, bounds, height() / 2.f, pressed_down_ ? color_utils::AlphaBlend(p.ink, p.accent, 0.1f) : p.accent);
    const auto font = Font(2, 13, 600);
    const int text_width = gfx::GetStringWidth(label_, font);
    canvas->DrawStringRect(label_, font, p.on_accent, gfx::Rect(18, 0, text_width, height()));
    // The key hint is outlined in the button's own ink (design: zt-btn-primary .zt-kbd).
    const gfx::RectF key(18 + text_width + 10, 9, 22, 18);
    StrokeRound(canvas, key, 4, WithAlpha(p.on_accent, 0xb3));
    canvas->DrawStringRectWithFlags(u"⏎", Font(3, 11, 500), WithAlpha(p.on_accent, 0xb3), gfx::ToEnclosingRect(key), gfx::Canvas::TEXT_ALIGN_CENTER);
    if (HasFocus()) PaintFocus(canvas, bounds, height() / 2.f, p.focus);
  }
  bool OnMousePressed(const ui::MouseEvent& event) override {
    if (!event.IsOnlyLeftMouseButton()) return false;
    pressed_down_ = true; SchedulePaint(); return true;
  }
  void OnMouseReleased(const ui::MouseEvent& event) override {
    pressed_down_ = false; SchedulePaint();
    if (HitTestPoint(event.location())) pressed_.Run();
  }
  void OnMouseCaptureLost() override { pressed_down_ = false; SchedulePaint(); }
  void OnMouseEntered(const ui::MouseEvent&) override { hovered_ = true; SchedulePaint(); }
  void OnMouseExited(const ui::MouseEvent&) override { hovered_ = false; SchedulePaint(); }
  bool OnKeyPressed(const ui::KeyEvent& event) override {
    if (event.key_code() != ui::VKEY_RETURN && event.key_code() != ui::VKEY_SPACE) return false;
    pressed_.Run(); return true;
  }
  bool HandleAccessibleAction(const ui::AXActionData& action) override {
    if (action.action != ax::mojom::Action::kDoDefault) return views::View::HandleAccessibleAction(action);
    pressed_.Run(); return true;
  }
  void OnFocus() override { SchedulePaint(); }
  void OnBlur() override { SchedulePaint(); }

 private:
  raw_ptr<TimeMachineView> root_;
  Kind kind_;
  base::RepeatingClosure pressed_;
  std::u16string label_;
  bool hovered_ = false, pressed_down_ = false;
};
BEGIN_METADATA(TimeMachineAction)
END_METADATA

// The column of digits a reel slides over.
class TimeMachineReelStrip final : public views::View {
  METADATA_HEADER(TimeMachineReelStrip, views::View)
 public:
  TimeMachineReelStrip(TimeMachineView* root, int count) : root_(root), count_(count) {}
  void OnPaint(gfx::Canvas* canvas) override {
    const auto p = root_->palette();
    const int cell = height() / count_;
    const auto font = Font(1, cell, 400, true);
    for (int i = 0; i < count_; ++i)
      canvas->DrawStringRectWithFlags(base::NumberToString16(i), font, p.ink, gfx::Rect(0, i * cell, width(), cell), gfx::Canvas::TEXT_ALIGN_CENTER);
  }
 private:
  raw_ptr<TimeMachineView> root_;
  int count_;
};
BEGIN_METADATA(TimeMachineReelStrip)
END_METADATA

// One clock digit on a reel: a clipped window over a column of digits whose
// layer slides to the current one (design: .tm-reel, 420 ms).
class TimeMachineReel final : public views::View {
  METADATA_HEADER(TimeMachineReel, views::View)
 public:
  TimeMachineReel(TimeMachineView* root, int count) : root_(root), count_(count) {
    SetPaintToLayer(); layer()->SetFillsBoundsOpaquely(false); layer()->SetMasksToBounds(true);
    strip_ = AddChildView(std::make_unique<TimeMachineReelStrip>(root, count));
    strip_->SetPaintToLayer(); strip_->layer()->SetFillsBoundsOpaquely(false);
    SetCanProcessEventsWithinSubtree(false);
    GetViewAccessibility().SetIsIgnored(true);
  }
  // `position` is the digit at the window, fractional while the reel turns.
  void SetPosition(float position) {
    position_ = std::clamp(position, -0.4f, count_ - 0.6f);
    gfx::Transform transform; transform.Translate(0, -position_ * height());
    strip_->layer()->SetTransform(transform);
  }
  void Layout(PassKey) override {
    strip_->SetBounds(0, 0, width(), height() * count_);
    SetPosition(position_);
  }

 private:
  raw_ptr<TimeMachineView> root_;
  raw_ptr<views::View> strip_ = nullptr;
  int count_;
  float position_ = 0;
};
BEGIN_METADATA(TimeMachineReel)
END_METADATA

// One tab of the deck: a raised card with its monogram, title and host, the
// shadow-float around it and a fading reflection under it. Its layer carries
// the cover-flow transform.
class TimeMachineCard final : public views::View {
  METADATA_HEADER(TimeMachineCard, views::View)
 public:
  static constexpr int kWidth = 232, kHeight = 144, kMargin = 48, kReflection = 64;
  TimeMachineCard(TimeMachineView* root, const GURL& url, std::u16string title) : root_(root), url_(url), title_(std::move(title)) {
    SetPaintToLayer(); layer()->SetFillsBoundsOpaquely(false);
    SetCanProcessEventsWithinSubtree(false);
    // Distance darkens the card instead of fading it, so cards behind never show through.
    shade_ = AddChildView(std::make_unique<views::View>());
    shade_->SetPaintToLayer(); shade_->layer()->SetFillsBoundsOpaquely(false);
    shade_->layer()->SetOpacity(0);
    GetViewAccessibility().SetRole(ax::mojom::Role::kListBoxOption);
    GetViewAccessibility().SetName(title_.empty() ? DisplayHost(url_) : title_);
  }
  static gfx::Size Size() { return gfx::Size(kWidth + 2 * kMargin, kHeight + 2 * kMargin + kReflection); }
  void OnPaint(gfx::Canvas* canvas) override {
    const auto p = root_->palette();
    const gfx::RectF card(kMargin, kMargin, kWidth, kHeight);
    {
      cc::PaintFlags flags; flags.setAntiAlias(true); flags.setColor(p.bg_raised);
      flags.setLooper(gfx::CreateShadowDrawLooper({gfx::ShadowValue(gfx::Vector2d(0, 18), 48, FloatShade(p))}));
      canvas->DrawRoundRect(card, 12, flags);
    }
    PaintFace(canvas, card, p);
    // The reflection (design: -webkit-box-reflect below 6px, 14% fading out).
    const gfx::Rect mirror(kMargin, kMargin + kHeight + 6, kWidth, kReflection - 8);
    canvas->SaveLayerAlpha(255, mirror);
    {
      gfx::ScopedCanvas flip(canvas);
      canvas->Translate(gfx::Vector2d(0, 2 * (kMargin + kHeight) + 6));
      canvas->Scale(1, -1);
      PaintFace(canvas, card, p);
    }
    cc::PaintFlags mask;
    const SkPoint points[2] = {SkPoint::Make(0, mirror.y()), SkPoint::Make(0, mirror.bottom())};
    const SkColor4f colors[2] = {SkColor4f::FromColor(SkColorSetARGB(0x24, 255, 255, 255)), SkColor4f::FromColor(SK_ColorTRANSPARENT)};
    mask.setShader(cc::PaintShader::MakeLinearGradient(points, colors, nullptr, 2, SkTileMode::kClamp));
    mask.setBlendMode(SkBlendMode::kDstIn);
    canvas->DrawRect(gfx::RectF(mirror), mask);
    canvas->Restore();
  }
  void SetFront(bool front) { GetViewAccessibility().SetIsSelected(front); }
  ui::Layer* shade() { return shade_->layer(); }
  void Layout(PassKey) override {
    shade_->SetBounds(kMargin, kMargin, kWidth, kHeight + 6 + kReflection - 8);
    shade_->SetBackground(views::CreateRoundedRectBackground(root_->palette().bg_void, 12));
  }
  const GURL& url() const { return url_; }

 private:
  void PaintFace(gfx::Canvas* canvas, const gfx::RectF& card, const sidebar::DesignPalette& p) {
    FillRound(canvas, card, 12, p.bg_raised);
    StrokeRound(canvas, card, 12, p.line);
    const gfx::RectF tile(card.x() + 18, card.y() + 18, 28, 28);
    FillRound(canvas, tile, 6, p.bg_hover);
    canvas->DrawStringRectWithFlags(Monogram(url_, title_), Font(2, 13, 700), p.ink, gfx::ToEnclosingRect(tile), gfx::Canvas::TEXT_ALIGN_CENTER);
    std::vector<std::u16string> lines;
    const auto title_font = Font(2, 14, 600);
    gfx::ElideRectangleText(title_.empty() ? DisplayHost(url_) : title_, title_font, kWidth - 36, 38, gfx::ELIDE_LONG_WORDS, &lines);
    for (size_t i = 0; i < lines.size() && i < 2; ++i)
      canvas->DrawStringRect(lines[i], title_font, p.ink, gfx::Rect(card.x() + 18, card.y() + 58 + 19 * i, kWidth - 36, 19));
    const auto host_font = Font(3, 11, 400);
    canvas->DrawStringRect(gfx::ElideText(DisplayHost(url_), host_font, kWidth - 36, gfx::ELIDE_TAIL), host_font, p.ink_muted,
        gfx::Rect(card.x() + 18, card.bottom() - 32, kWidth - 36, 16));
  }
  raw_ptr<TimeMachineView> root_;
  raw_ptr<views::View> shade_ = nullptr;
  GURL url_;
  std::u16string title_;
};
BEGIN_METADATA(TimeMachineCard)
END_METADATA

// The deck: the card drawn under the pointer comes to the front. A card turning
// to the front uncovers others under a still pointer, so after a change the
// pointer has to travel a little before it picks the next one.
class TimeMachineDeck final : public views::View {
  METADATA_HEADER(TimeMachineDeck, views::View)
 public:
  explicit TimeMachineDeck(TimeMachineView* root) : root_(root) {
    SetFocusBehavior(FocusBehavior::ALWAYS);
    GetViewAccessibility().SetRole(ax::mojom::Role::kListBox);
  }
  void OnMouseMoved(const ui::MouseEvent& event) override { Hover(event.location()); }
  void OnMouseEntered(const ui::MouseEvent& event) override { Hover(event.location()); }
  void OnMouseExited(const ui::MouseEvent&) override { anchor_.reset(); root_->FocusCard(-1); }
  bool OnKeyPressed(const ui::KeyEvent& event) override {
    const int count = static_cast<int>(root_->card_count_for_testing());
    if (count == 0) return false;
    if (event.key_code() == ui::VKEY_LEFT || event.key_code() == ui::VKEY_RIGHT) {
      root_->FocusCard(std::clamp(root_->front_card_for_testing() + (event.key_code() == ui::VKEY_LEFT ? -1 : 1), 0, count - 1));
      return true;
    }
    return false;
  }
  void OnFocus() override { SchedulePaint(); }
  void OnBlur() override { SchedulePaint(); }
  void OnPaint(gfx::Canvas* canvas) override {
    if (!HasFocus()) return;
    const auto p = root_->palette();
    PaintFocus(canvas, gfx::RectF((width() - TimeMachineCard::kWidth) / 2.f, 12, TimeMachineCard::kWidth, TimeMachineCard::kHeight), 12, p.focus);
  }

 private:
  void Hover(const gfx::Point& location) {
    const gfx::Point at(location.x() + x(), location.y() + y());
    const int hit = root_->CardAt(at);
    if (hit < 0) return;
    if (hit != root_->front_card_for_testing()) {
      if (anchor_ && (at - *anchor_).Length() < kTravel) return;
      root_->FocusCard(hit);
    }
    anchor_ = at;
  }
  static constexpr float kTravel = 18;
  raw_ptr<TimeMachineView> root_;
  std::optional<gfx::Point> anchor_;
};
BEGIN_METADATA(TimeMachineDeck)
END_METADATA

// The axis (design: the old timeline): drag or click to move the playhead,
// wheel and arrows step through time, Shift+arrows jump between changes.
class TimeMachineAxis final : public views::View {
  METADATA_HEADER(TimeMachineAxis, views::View)
 public:
  explicit TimeMachineAxis(TimeMachineView* root) : root_(root) {
    SetFocusBehavior(FocusBehavior::ALWAYS);
    GetViewAccessibility().SetRole(ax::mojom::Role::kSlider);
    GetViewAccessibility().SetName(T(u"Chwila na osi czasu"));
  }
  sidebar::TimelineSceneInput Input() const {
    return {static_cast<float>(width()), static_cast<float>(height()), root_->moment(), TimeMachineView::kQuantumMs,
            root_->now_ms(), TimelineUtcOffsetMinutes(base::Time::FromMillisecondsSinceUnixEpoch(root_->now_ms())),
            root_->window_start(), root_->window_end()};
  }
  void OnPaint(gfx::Canvas* canvas) override {
    const auto* timeline = root_->timeline();
    if (!timeline) return;
    const auto input = Input();
    PaintPrimitives(canvas, timeline->timeline_scene(root_->workspace_id(), input, root_->palette()));
    // The ring shows for keyboard use only (design: :focus-visible), inside the panel.
    if (HasFocus() && keyboard_) StrokeRound(canvas, gfx::RectF(GetLocalBounds()), 12, root_->palette().focus, 2);
  }
  bool OnMousePressed(const ui::MouseEvent& event) override {
    if (!event.IsOnlyLeftMouseButton()) return false;
    keyboard_ = false; RequestFocus(); root_->SetDragging(true);
    root_->SetMoment(sidebar::timeline_time_at(Input(), event.x()), false); return true;
  }
  bool OnMouseDragged(const ui::MouseEvent& event) override {
    root_->SetMoment(sidebar::timeline_time_at(Input(), event.x()), false); return true;
  }
  void OnMouseReleased(const ui::MouseEvent&) override { root_->SetDragging(false); root_->SetMoment(root_->moment(), true); }
  void OnMouseCaptureLost() override { root_->SetDragging(false); }
  // Scrolling moves the visible span (either axis of a trackpad or wheel);
  // Cmd/Ctrl+scroll and pinching zoom around the pointer.
  bool OnMouseWheel(const ui::MouseWheelEvent& event) override {
    Scroll(event.x_offset(), event.y_offset(), event.x(), event.IsCommandDown() || event.IsControlDown()); return true;
  }
  void OnScrollEvent(ui::ScrollEvent* event) override {
    if (event->type() != ui::EventType::kScroll) return;
    Scroll(event->x_offset(), event->y_offset(), event->x(), event->IsCommandDown() || event->IsControlDown());
    event->SetHandled();
  }
  void OnGestureEvent(ui::GestureEvent* event) override {
    if (event->type() != ui::EventType::kGesturePinchUpdate) return;
    root_->Zoom(1.f / std::max(0.2f, event->details().scale()), event->x());
    event->SetHandled();
  }
  bool OnKeyPressed(const ui::KeyEvent& event) override {
    const int64_t five = 5 * 60 * 1000;
    if (!keyboard_) { keyboard_ = true; SchedulePaint(); }
    switch (event.key_code()) {
      case ui::VKEY_LEFT: case ui::VKEY_RIGHT: {
        const bool forward = event.key_code() == ui::VKEY_RIGHT;
        if (event.IsShiftDown()) root_->Step(forward);
        else root_->SetMoment(root_->moment() + (forward ? five : -five), false);
        return true;
      }
      case ui::VKEY_HOME: if (const auto* t = root_->timeline()) { const auto b = t->timeline_bounds(root_->workspace_id()); if (b.valid) root_->SetMoment(b.first_ms, false); } return true;
      case ui::VKEY_END: root_->SetMoment(root_->now_ms(), false); return true;
      case ui::VKEY_RETURN: root_->Restore(); return true;
      case ui::VKEY_OEM_PLUS: case ui::VKEY_ADD: root_->Zoom(0.5f, -1); return true;
      case ui::VKEY_OEM_MINUS: case ui::VKEY_SUBTRACT: root_->Zoom(2.f, -1); return true;
      default: return false;
    }
  }
  bool HandleAccessibleAction(const ui::AXActionData& action) override {
    if (action.action == ax::mojom::Action::kIncrement || action.action == ax::mojom::Action::kDecrement) {
      root_->Step(action.action == ax::mojom::Action::kIncrement); return true;
    }
    return views::View::HandleAccessibleAction(action);
  }
  void OnFocus() override { SchedulePaint(); }
  void OnBlur() override { keyboard_ = false; SchedulePaint(); }

 private:
  void Scroll(float dx, float dy, float x, bool zoom) {
    // Wheels scroll vertically; a downward scroll goes back in time.
    const float delta = std::abs(dx) >= std::abs(dy) ? -dx : dy;
    if (zoom) root_->Zoom(std::exp(delta / 480.f), x);
    else root_->Pan(-delta);
  }
  raw_ptr<TimeMachineView> root_;
  bool keyboard_ = false;
};
BEGIN_METADATA(TimeMachineAxis)
END_METADATA

int64_t Milliseconds(base::Time time) { return time.InMillisecondsSinceUnixEpoch(); }

}  // namespace

TimeMachineView::Host::Host() = default;
TimeMachineView::Host::Host(Host&&) = default;
TimeMachineView::Host& TimeMachineView::Host::operator=(Host&&) = default;
TimeMachineView::Host::~Host() = default;

TimeMachineView::TimeMachineView(Host host) : host_(std::move(host)) {
  SetFocusBehavior(FocusBehavior::NEVER);
  GetViewAccessibility().SetRole(ax::mojom::Role::kDialog);
  GetViewAccessibility().SetName(u"Time Machine");
  for (int count : {3, 10, 6, 10}) reels_.push_back(AddChildView(std::make_unique<TimeMachineReel>(this, count)));
  deck_ = AddChildView(std::make_unique<TimeMachineDeck>(this));
  restore_ = AddChildView(std::make_unique<TimeMachineAction>(this, TimeMachineAction::Kind::kPrimary, base::BindRepeating(&TimeMachineView::Restore, base::Unretained(this))));
  enable_ = AddChildView(std::make_unique<TimeMachineAction>(this, TimeMachineAction::Kind::kPrimary, base::BindRepeating([](TimeMachineView* view) {
    if (view->host_.enable) view->host_.enable.Run();
    view->enabled_ = true; view->SetMoment(view->now_ms(), false);
  }, base::Unretained(this))));
  static_cast<TimeMachineAction*>(enable_.get())->SetLabel(T(u"Włącz Time Machine"));
  close_ = AddChildView(std::make_unique<TimeMachineAction>(this, TimeMachineAction::Kind::kClose, base::BindRepeating([](TimeMachineView* view) {
    if (view->host_.close) view->host_.close.Run();
  }, base::Unretained(this))));
  axis_ = AddChildView(std::make_unique<TimeMachineAxis>(this));
}
TimeMachineView::~TimeMachineView() { StopTicking(); }

const sidebar::Timeline* TimeMachineView::timeline() const { return host_.timeline ? host_.timeline.Run() : nullptr; }
int64_t TimeMachineView::now_ms() const { return Milliseconds(host_.now ? host_.now.Run() : base::Time::Now()); }
sidebar::DesignPalette TimeMachineView::palette() const { return host_.palette.Run(); }
bool TimeMachineView::animations() const { return host_.animations && host_.animations.Run(); }
size_t TimeMachineView::card_count_for_testing() const { return card_urls_.size(); }
std::u16string TimeMachineView::restore_label_for_testing() const { return static_cast<TimeMachineAction*>(restore_.get())->label(); }
views::View* TimeMachineView::axis_for_testing() const { return axis_; }
views::View* TimeMachineView::deck_for_testing() const { return deck_; }

void TimeMachineView::Open(std::string workspace_id, std::u16string workspace_name, bool enabled) {
  // Opening rebuilds the deck so its cards swing in.
  for (auto& card : cards_) RemoveChildViewT(card.view.get());
  cards_.clear(); card_urls_.clear(); paint_order_.clear();
  workspace_id_ = std::move(workspace_id); workspace_name_ = std::move(workspace_name); enabled_ = enabled;
  focus_url_.clear(); dragging_ = false;
  t_ms_ = now_ms();
  const auto span = sidebar::timeline_window(t_ms_, t_ms_);
  window_start_ = span.start_ms; window_end_ = span.end_ms;
  InvalidateLayout(); Update(true);
  axis_->RequestFocus();
}

void TimeMachineView::SetMoment(int64_t t_ms, bool snap) {
  const auto* timeline = this->timeline();
  const int64_t now = now_ms();
  if (snap && timeline) t_ms = timeline->timeline_snap(workspace_id_, t_ms);
  t_ms = std::min(t_ms, now);
  if (const auto bounds = timeline ? timeline->timeline_bounds(workspace_id_) : sidebar::TimelineBounds{}; bounds.valid)
    t_ms = std::max(t_ms, std::min(bounds.first_ms, now) - 60 * 60 * 1000);
  const bool moved = t_ms != t_ms_;
  t_ms_ = t_ms;
  // While dragging the axis holds still; afterwards it follows the playhead.
  if (!dragging_) {
    const auto span = sidebar::timeline_follow(window_start_, window_end_, t_ms_, now, first_ms());
    window_start_ = span.start_ms; window_end_ = span.end_ms;
  }
  if (moved) focus_url_.clear();
  Update(true);
}

int64_t TimeMachineView::first_ms() const {
  const auto* timeline = this->timeline();
  const auto bounds = timeline ? timeline->timeline_bounds(workspace_id_) : sidebar::TimelineBounds{};
  return bounds.valid ? bounds.first_ms : now_ms();
}

void TimeMachineView::Pan(float dx) {
  if (dragging_) return;
  const auto span = sidebar::timeline_pan(static_cast<TimeMachineAxis*>(axis_.get())->Input(), dx, first_ms());
  window_start_ = span.start_ms; window_end_ = span.end_ms;
  axis_->SchedulePaint();
}

void TimeMachineView::Zoom(float factor, float x) {
  if (dragging_) return;
  const auto input = static_cast<TimeMachineAxis*>(axis_.get())->Input();
  // Keys zoom around the playhead.
  if (x < 0) x = 20 + (axis_->width() - 40) * static_cast<float>(t_ms_ - window_start_) / std::max<int64_t>(1, window_end_ - window_start_);
  const auto span = sidebar::timeline_zoom(input, factor, x, first_ms());
  window_start_ = span.start_ms; window_end_ = span.end_ms;
  axis_->SchedulePaint();
}

void TimeMachineView::Step(bool forward) {
  if (const auto* timeline = this->timeline()) {
    const int64_t next = timeline->timeline_step(workspace_id_, t_ms_, forward);
    SetMoment(next == t_ms_ && forward ? now_ms() : next, false);
  }
}

void TimeMachineView::SetDragging(bool dragging) {
  dragging_ = dragging;
  if (!dragging) SetMoment(t_ms_, false);
}

void TimeMachineView::Restore() {
  if (!enabled_ || card_urls_.empty() || !host_.restore) return;
  if (host_.restore.Run(t_ms_, kQuantumMs) && host_.close) host_.close.Run();
}

void TimeMachineView::FocusCard(int index) {
  const std::string url = index >= 0 && static_cast<size_t>(index) < card_urls_.size() ? card_urls_[index] : std::string();
  if (url == focus_url_) return;
  focus_url_ = url;
  front_ = FrontIndex();
  Animate();
}

void TimeMachineView::Update(bool animate) {
  const auto* timeline = this->timeline();
  const int64_t now = now_ms();
  const int offset = TimelineUtcOffsetMinutes(base::Time::FromMillisecondsSinceUnixEpoch(t_ms_));
  sidebar::TimelineMoment moment;
  if (timeline && enabled_) moment = timeline->timeline_query(workspace_id_, t_ms_, kQuantumMs);
  const auto clock = sidebar::timeline_clock(t_ms_, now, offset, moment.tabs.size(), kQuantumMs);
  day_line_ = Text(clock.day_line); range_ = Text(clock.range); value_text_ = Text(clock.value_text);
  size_ = timeline ? ui::FormatBytes(base::ByteSize(timeline->timeline_stats().bytes)) : std::u16string();
  for (size_t i = 0; i < reel_target_.size() && i < clock.digits.size(); ++i) reel_target_[i] = static_cast<int>(clock.digits[i]);
  GetViewAccessibility().SetDescription(day_line_ + u" " + value_text_);
  // Tabs that stay keep their card and slide to their new place; new tabs swing
  // in one after another; tabs gone from the hour turn away.
  std::vector<std::string> urls;
  for (const auto& tab : moment.tabs) urls.emplace_back(std::string(tab.url));
  if (urls != card_urls_) {
    std::vector<CardMotion> next;
    int entering = 0;
    for (size_t i = 0; i < moment.tabs.size(); ++i) {
      auto it = std::find_if(cards_.begin(), cards_.end(), [&](const CardMotion& card) { return !card.leaving && card.url == urls[i]; });
      if (it != cards_.end()) {
        next.push_back(*it); cards_.erase(it);
      } else {
        auto* view = AddChildViewAt(std::make_unique<TimeMachineCard>(this, GURL(urls[i]), Text(moment.tabs[i].title)), GetIndexOf(deck_).value());
        CardMotion card;
        card.view = view; card.url = urls[i]; card.slot = static_cast<float>(i); card.delay = 0.045f * entering++;
        next.push_back(card);
      }
      next.back().index = static_cast<int>(i);
    }
    for (auto& old : cards_) { old.leaving = true; old.delay = 0; next.push_back(old); }
    cards_ = std::move(next);
    card_urls_ = std::move(urls);
  }
  active_ = std::max(0, static_cast<int>(moment.active));
  auto* restore = static_cast<TimeMachineAction*>(restore_.get());
  restore->SetLabel(Text(clock.restore));
  restore->SetVisible(enabled_ && !card_urls_.empty());
  enable_->SetVisible(!enabled_);
  deck_->GetViewAccessibility().SetName(range_);
  axis_->GetViewAccessibility().SetValue(value_text_);
  axis_->SchedulePaint();
  SchedulePaint();
  front_ = FrontIndex();
  if (animate) Animate(); else Settle();
}

int TimeMachineView::CardAt(const gfx::Point& point) const {
  const gfx::PointF target(point);
  // Nearest first: paint_order_ runs far to near.
  for (auto it = paint_order_.rbegin(); it != paint_order_.rend(); ++it) {
    const auto motion = std::find_if(cards_.begin(), cards_.end(), [&](const CardMotion& card) { return card.view == *it; });
    if (motion == cards_.end() || motion->leaving || motion->appear < 0.5f) continue;
    const auto* card = *it;
    const auto& transform = card->layer()->transform();
    const float l = TimeMachineCard::kMargin, t = TimeMachineCard::kMargin;
    const float r = l + TimeMachineCard::kWidth, b = t + TimeMachineCard::kHeight;
    std::array<gfx::PointF, 4> quad = {gfx::PointF(l, t), gfx::PointF(r, t), gfx::PointF(r, b), gfx::PointF(l, b)};
    for (auto& corner : quad) corner = transform.MapPoint(corner) + card->bounds().OffsetFromOrigin();
    // Inside a convex quad: on the same side of all four edges.
    int sign = 0; bool inside = true;
    for (size_t i = 0; i < quad.size() && inside; ++i) {
      const auto& a = quad[i]; const auto& c = quad[(i + 1) % quad.size()];
      const float cross = (c.x() - a.x()) * (target.y() - a.y()) - (c.y() - a.y()) * (target.x() - a.x());
      const int side = cross > 0 ? 1 : cross < 0 ? -1 : 0;
      if (side != 0 && sign != 0 && side != sign) inside = false;
      if (side != 0) sign = side;
    }
    if (inside) return motion->index;
  }
  return -1;
}

int TimeMachineView::FrontIndex() const {
  if (const auto it = std::find(card_urls_.begin(), card_urls_.end(), focus_url_); !focus_url_.empty() && it != card_urls_.end())
    return static_cast<int>(it - card_urls_.begin());
  return std::min(active_, std::max(0, static_cast<int>(card_urls_.size()) - 1));
}

void TimeMachineView::Animate() {
  auto* compositor = GetWidget() ? GetWidget()->GetCompositor() : nullptr;
  if (!animations() || !compositor) { Settle(); return; }
  if (ticking_ != compositor) {
    StopTicking();
    compositor->AddAnimationObserver(this);
    ticking_ = compositor; last_tick_ = base::TimeTicks();
  }
}

void TimeMachineView::Settle() {
  StopTicking();
  deck_position_ = front_; deck_velocity_ = 0;
  for (size_t i = 0; i < reel_target_.size(); ++i) { reel_position_[i] = reel_target_[i]; reel_velocity_[i] = 0; }
  std::erase_if(cards_, [this](CardMotion& card) {
    if (card.leaving) { RemoveChildViewT(card.view.get()); return true; }
    card.slot = card.index; card.slot_velocity = 0; card.appear = 1; card.appear_velocity = 0; card.delay = 0;
    return false;
  });
  paint_order_.clear();
  PlaceCards();
}

void TimeMachineView::StopTicking() {
  if (ticking_) ticking_->RemoveAnimationObserver(this);
  ticking_ = nullptr;
}

void TimeMachineView::OnAnimationStep(base::TimeTicks timestamp) {
  const float dt = last_tick_.is_null() ? 1.f / 60 : static_cast<float>((timestamp - last_tick_).InSecondsF());
  last_tick_ = timestamp;
  const bool settled = Advance(dt);
  PlaceCards();
  if (settled) StopTicking();
}

void TimeMachineView::OnCompositingShuttingDown(ui::Compositor*) { StopTicking(); }

void TimeMachineView::RemovedFromWidget() { StopTicking(); }

bool TimeMachineView::Advance(float dt) {
  // Stiff, slightly underdamped springs: quick to answer, a small overshoot, no wobble.
  bool settled = true;
  auto spring = [&](float& value, float& velocity, float target, float stiffness, float damping) {
    const auto next = sidebar::timeline_spring(value, velocity, target, dt, stiffness, damping);
    value = next.value; velocity = next.velocity; settled &= next.settled;
  };
  spring(deck_position_, deck_velocity_, front_, 150, 19);
  for (size_t i = 0; i < reel_target_.size(); ++i) spring(reel_position_[i], reel_velocity_[i], reel_target_[i], 190, 22);
  for (auto& card : cards_) {
    if (!card.leaving) spring(card.slot, card.slot_velocity, card.index, 170, 21);
    if (card.delay > 0) { card.delay -= dt; settled = false; continue; }
    spring(card.appear, card.appear_velocity, card.leaving ? 0.f : 1.f, card.leaving ? 220 : 120, card.leaving ? 30 : 15);
  }
  std::erase_if(cards_, [this](CardMotion& card) {
    if (!card.leaving || card.appear > 0.02f) return false;
    std::erase(paint_order_, card.view.get());
    RemoveChildViewT(card.view.get()); return true;
  });
  return settled;
}

void TimeMachineView::PlaceCards() {
  for (size_t i = 0; i < reels_.size(); ++i) static_cast<TimeMachineReel*>(reels_[i].get())->SetPosition(reel_position_[i]);
  if (cards_.empty() || deck_bounds_.IsEmpty()) return;
  const auto size = TimeMachineCard::Size();
  const gfx::Point origin(deck_bounds_.CenterPoint().x() - size.width() / 2, deck_bounds_.y() + 12 - TimeMachineCard::kMargin);
  // CSS: perspective 1100px from 50% 30% of the deck; each card turns about its centre.
  const gfx::PointF eye(deck_bounds_.CenterPoint().x(), deck_bounds_.y() + deck_bounds_.height() * 0.3f);
  const gfx::PointF centre(size.width() / 2.f, TimeMachineCard::kMargin + TimeMachineCard::kHeight / 2.f);
  std::vector<std::pair<float, views::View*>> order;
  for (auto& motion : cards_) {
    auto* card = static_cast<TimeMachineCard*>(motion.view.get());
    if (card->bounds() != gfx::Rect(origin, size)) card->SetBoundsRect(gfx::Rect(origin, size));
    card->SetFront(!motion.leaving && motion.index == front_);
    const float offset = motion.slot - deck_position_;
    const auto place = sidebar::timeline_card(offset);
    // Arriving and leaving cards turn edge-on, away from the front, and sink back.
    const float hidden = 1.f - std::clamp(motion.appear, 0.f, 1.f);
    const float side = offset < 0 ? 1.f : -1.f;
    gfx::Transform transform;
    transform.Translate(eye.x() - origin.x(), eye.y() - origin.y());
    transform.ApplyPerspectiveDepth(1100);
    transform.Translate(origin.x() - eye.x(), origin.y() - eye.y());
    transform.Translate(centre.x(), centre.y());
    transform.Translate3d(place.x, 0, place.z - 160.f * hidden);
    transform.RotateAboutYAxis(place.angle + side * 88.f * hidden);
    transform.Translate(-centre.x(), -centre.y());
    card->layer()->SetTransform(transform);
    // Far cards fade out so the deck stays readable with dozens of tabs.
    const float far = std::clamp(9.f - std::abs(offset), 0.f, 1.f);
    card->layer()->SetOpacity(std::clamp(motion.appear, 0.f, 1.f) * far);
    card->shade()->SetOpacity(1.f - place.opacity);
    order.emplace_back(std::abs(offset), card);
  }
  // Nearer cards paint later (on top); reorder only when the order changes.
  std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
  std::vector<views::View*> paint;
  for (const auto& [ignored, view] : order) paint.push_back(view);
  if (paint != paint_order_) {
    // Cards sit first among the children (under the controls), far to near.
    for (size_t i = 0; i < paint.size(); ++i) ReorderChildView(paint[i], i);
    paint_order_ = std::move(paint);
  }
}

void TimeMachineView::Layout(PassKey) {
  const int w = width(), h = height();
  close_->SetBoundsRect(gfx::Rect(w - 24 - 28, 18, 28, 28));
  const int axis_h = 128;
  axis_->SetBounds(32, h - 28 - axis_h, std::max(0, w - 64), axis_h);
  // The clock grows with the window, up to the design's 200 px.
  const int region_top = 64, region_bottom = axis_->y() - 24;
  digit_size_ = std::clamp(region_bottom - region_top - 400, 80, 200);
  const int block = 24 + digit_size_ + 36 + 230 + 34 + 62;
  const int top = region_top + std::max(0, (region_bottom - region_top - block) / 2);
  const int reel_w = std::lround(digit_size_ * 0.58f), colon_w = std::lround(digit_size_ * 0.26f);
  const int clock_w = 4 * reel_w + colon_w;
  clock_ = gfx::Rect((w - clock_w) / 2, top + 24, clock_w, digit_size_);
  int x = clock_.x();
  for (size_t i = 0; i < reels_.size(); ++i) {
    if (i == 2) { colon_ = gfx::Rect(x, clock_.y(), colon_w, digit_size_); x += colon_w; }
    reels_[i]->SetBounds(x, clock_.y(), reel_w, digit_size_); x += reel_w;
  }
  const gfx::Rect deck_bounds(0, clock_.bottom() + 36, w, 230);
  const bool deck_moved = deck_bounds != deck_bounds_;
  deck_bounds_ = deck_bounds;
  deck_->SetBoundsRect(gfx::Rect(0, deck_bounds_.y(), w, TimeMachineCard::kHeight + 24));
  const auto button = restore_->GetPreferredSize();
  restore_->SetBounds((w - button.width()) / 2, deck_bounds_.bottom() + 34, button.width(), button.height());
  const auto enable = enable_->GetPreferredSize();
  enable_->SetBounds((w - enable.width()) / 2, deck_bounds_.y() + 120, enable.width(), enable.height());
  caption_ = gfx::Rect(0, restore_->bounds().bottom() + 10, w, 16);
  // Only a new deck position re-places the cards; label changes keep them turning.
  if (deck_moved) PlaceCards();
}

void TimeMachineView::OnPaint(gfx::Canvas* canvas) {
  const auto p = palette();
  canvas->DrawColor(p.bg_void);
  // Header: "Time Machine · space", the disk size, the Esc hint.
  const auto strong = Font(2, 13, 600), body = Font(2, 13, 400);
  int x = 32;
  const std::u16string name = u"Time Machine";
  canvas->DrawStringRect(name, strong, p.ink, gfx::Rect(x, 18, 200, 28)); x += gfx::GetStringWidth(name, strong) + 10;
  FillRound(canvas, gfx::RectF(x, 29, 6, 6), 3, p.accent); x += 16;
  canvas->DrawStringRect(workspace_name_, body, p.ink_muted, gfx::Rect(x, 18, 300, 28));
  const gfx::RectF esc(close_->x() - 8 - 30, 23, 30, 18);
  FillRound(canvas, esc, 4, p.bg_hover);
  canvas->DrawStringRectWithFlags(u"Esc", Font(3, 11, 500), p.ink_muted, gfx::ToEnclosingRect(esc), gfx::Canvas::TEXT_ALIGN_CENTER);
  if (!size_.empty() && enabled_) {
    const auto label = TF(u"{} na dysku", {size_});
    const auto small = Font(2, 12, 400);
    const int width = gfx::GetStringWidth(label, small);
    canvas->DrawStringRect(label, small, p.ink_muted, gfx::Rect(esc.x() - 16 - width, 18, width, 28));
  }
  // Day line (eyebrow, 0.14 em tracking in the design) and the clock's colon.
  canvas->DrawStringRectWithFlags(day_line_, Font(2, 12, 600), p.ink_muted, gfx::Rect(0, clock_.y() - 24, width(), 16), gfx::Canvas::TEXT_ALIGN_CENTER);
  {
    gfx::Rect colon = colon_; colon.Outset(gfx::Outsets::VH(0, colon_.width()));
    canvas->DrawStringRectWithFlags(u":", Font(1, digit_size_, 400, true), p.ink_muted, colon, gfx::Canvas::TEXT_ALIGN_CENTER | gfx::Canvas::NO_ELLIPSIS);
  }
  if (!enabled_) {
    canvas->DrawStringRectWithFlags(T(u"Time Machine jest wyłączony"), Font(1, 40, 400, true), p.ink, gfx::Rect(0, deck_bounds_.y() + 12, width(), 48), gfx::Canvas::TEXT_ALIGN_CENTER);
    canvas->DrawStringRectWithFlags(T(u"Włącz zapis historii kart, a Time Machine zacznie pamiętać, co było otwarte w tej przestrzeni."), body, p.ink_muted,
        gfx::Rect(0, deck_bounds_.y() + 70, width(), 20), gfx::Canvas::TEXT_ALIGN_CENTER);
    return;
  }
  if (card_urls_.empty()) {
    const auto* timeline = this->timeline();
    const bool any = timeline && timeline->timeline_bounds(workspace_id_).valid;
    canvas->DrawStringRectWithFlags(any ? T(u"Przestrzeń była wtedy pusta") : T(u"Nic jeszcze nie zapisano"), Font(2, 15, 400), p.ink_muted,
        gfx::Rect(0, deck_bounds_.y() + 64, width(), 22), gfx::Canvas::TEXT_ALIGN_CENTER);
  }
  canvas->DrawStringRectWithFlags(range_, Font(2, 12, 400), p.ink_muted, caption_, gfx::Canvas::TEXT_ALIGN_CENTER);
}

bool TimeMachineView::OnKeyPressed(const ui::KeyEvent& event) {
  if (event.key_code() == ui::VKEY_RETURN) { Restore(); return true; }
  return false;
}

void TimeMachineView::OnThemeChanged() {
  views::View::OnThemeChanged();
  SchedulePaint();
  for (auto& reel : reels_) for (views::View* child : reel->children()) child->SchedulePaint();
  for (auto& card : cards_) { card.view->InvalidateLayout(); card.view->SchedulePaint(); }
}

BEGIN_METADATA(TimeMachineView)
END_METADATA

}  // namespace zitter::prototype
