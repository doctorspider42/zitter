// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/prototype/tab_switcher_view.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include "base/i18n/case_conversion.h"
#include "base/i18n/time_formatting.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "cc/paint/paint_flags.h"
#include "chromium/prototype/i18n.h"
#include "chromium/prototype/sidebar_scene.rs.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/compositor.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/events/event.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/gfx/shadow_value.h"
#include "ui/gfx/skia_paint_util.h"
#include "ui/gfx/text_elider.h"
#include "ui/gfx/text_utils.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/widget/widget.h"

namespace zitter::prototype {
namespace {

// The design's frame is 1440×900: a 760×475 front card, the stack rising 44 px
// per step behind it and shrinking 7.5 % per step, the time line on the right.
constexpr float kDesignCardWidth = 760, kDesignCardHeight = 475, kDesignStage = 1260, kDesignHeight = 900;
constexpr float kRise = 44, kShrink = 0.075f, kShadeStep = 0.2f, kShadeMax = 0.78f;
constexpr int kMargin = 72;           // Room around a card for its shadow.
constexpr int kTimelineWidth = 150, kTimelineInset = 40;
constexpr int kTickPitch = 56;

gfx::FontList Font(uint32_t family, float size, int weight, bool italic = false) {
  const int weight_value = std::clamp(weight > 0 ? weight : 400, 100, 900) / 100 * 100;
  return gfx::FontList({std::string(sidebar::design_font_family(family))}, italic ? gfx::Font::ITALIC : gfx::Font::NORMAL,
      std::max(1, static_cast<int>(std::lround(size))), static_cast<gfx::Font::Weight>(weight_value));
}
bool Dark(const sidebar::DesignPalette& p) { return color_utils::GetRelativeLuminance(p.ink) > 0.5f; }
// shadow-float's blur in Noc and Papier (tokens.json).
SkColor FloatShade(const sidebar::DesignPalette& p) {
  return Dark(p) ? SkColorSetARGB(0xa6, 0, 0, 0) : SkColorSetARGB(0x29, 0x16, 0x14, 0x1c);
}
void FillRound(gfx::Canvas* canvas, const gfx::RectF& rect, float radius, SkColor color) {
  cc::PaintFlags flags; flags.setAntiAlias(true); flags.setColor(color);
  canvas->DrawRoundRect(rect, radius, flags);
}
void StrokeRound(gfx::Canvas* canvas, const gfx::RectF& rect, float radius, SkColor color) {
  cc::PaintFlags flags; flags.setAntiAlias(true); flags.setStyle(cc::PaintFlags::kStroke_Style); flags.setStrokeWidth(1); flags.setColor(color);
  gfx::RectF inset = rect; inset.Inset(0.5f);
  canvas->DrawRoundRect(inset, radius - 0.5f, flags);
}
std::string BareHost(const GURL& url) {
  std::string host(url.host());
  if (base::StartsWith(host, "www.")) host = host.substr(4);
  return host;
}
std::u16string Monogram(const GURL& url, const std::u16string& title) {
  const std::u16string source = BareHost(url).empty() ? title : base::UTF8ToUTF16(BareHost(url));
  return source.empty() ? u"·" : base::i18n::ToUpper(source.substr(0, 1));
}
gfx::ImageSkia Favicon(const std::vector<uint32_t>& pixels) {
  if (pixels.size() != 1024) return {};
  SkBitmap bitmap; bitmap.allocN32Pixels(32, 32);
  std::copy(pixels.begin(), pixels.end(), bitmap.getAddr32(0, 0));
  bitmap.setImmutable();
  return gfx::ImageSkia::CreateFrom1xBitmap(bitmap);
}
// The favicon in a `size` tile, or the host's first letter where there is none.
void PaintTabIcon(gfx::Canvas* canvas, const gfx::Rect& tile, const TabSwitcherView::Card& card,
                  const sidebar::DesignPalette& p, float radius) {
  const auto icon = Favicon(card.favicon);
  if (!icon.isNull()) {
    const int size = std::max(16, tile.width() * 2 / 3);
    canvas->DrawImageInt(icon, 0, 0, 32, 32, tile.CenterPoint().x() - size / 2, tile.CenterPoint().y() - size / 2, size, size, true);
    return;
  }
  FillRound(canvas, gfx::RectF(tile), radius, p.bg_hover);
  canvas->DrawStringRectWithFlags(Monogram(card.url, card.title), Font(2, tile.height() * 0.5f, 600), p.ink, tile,
                                  gfx::Canvas::TEXT_ALIGN_CENTER);
}
bool IsControl(ui::KeyboardCode code) {
  return code == ui::VKEY_CONTROL || code == ui::VKEY_LCONTROL || code == ui::VKEY_RCONTROL;
}

}  // namespace

// One tab: the page as it was last seen (or its icon and host), the shadow-float
// under it and a shade in the window's void color that deepens with distance,
// so cards behind darken instead of showing through. Its layer carries the
// stack transform.
class TabSwitcherCard final : public views::View {
  METADATA_HEADER(TabSwitcherCard, views::View)
 public:
  TabSwitcherCard(TabSwitcherView* root, int index) : root_(root), index_(index) {
    SetPaintToLayer(); layer()->SetFillsBoundsOpaquely(false);
    SetCanProcessEventsWithinSubtree(false);
    SetFocusBehavior(FocusBehavior::ALWAYS);
    shade_ = AddChildView(std::make_unique<views::View>());
    shade_->SetPaintToLayer(); shade_->layer()->SetFillsBoundsOpaquely(false);
    shade_->layer()->SetOpacity(0);
    GetViewAccessibility().SetRole(ax::mojom::Role::kListBoxOption);
  }
  void SetCard(const TabSwitcherView::Card& card) {
    const std::u16string host = base::UTF8ToUTF16(BareHost(card.url));
    GetViewAccessibility().SetName(card.title.empty() ? host : card.title);
    if (!host.empty() && !card.title.empty()) GetViewAccessibility().SetDescription(host);
    SchedulePaint();
  }
  void SetShade(float opacity) { shade_->layer()->SetOpacity(opacity); }
  void Layout(PassKey) override {
    shade_->SetBounds(kMargin, kMargin, width() - 2 * kMargin, height() - 2 * kMargin);
    shade_->SetBackground(views::CreateRoundedRectBackground(root_->palette().bg_void, 12));
  }
  void OnPaint(gfx::Canvas* canvas) override {
    if (index_ >= static_cast<int>(root_->card_count())) return;
    const auto& card = root_->card(index_);
    const auto p = root_->palette();
    const gfx::RectF face(kMargin, kMargin, width() - 2 * kMargin, height() - 2 * kMargin);
    {
      cc::PaintFlags flags; flags.setAntiAlias(true); flags.setColor(p.bg_raised);
      flags.setLooper(gfx::CreateShadowDrawLooper({gfx::ShadowValue(gfx::Vector2d(0, 22), 64, FloatShade(p))}));
      canvas->DrawRoundRect(face, 12, flags);
    }
    {
      gfx::ScopedCanvas clip(canvas);
      canvas->ClipPath(SkPathBuilder().addRRect(SkRRect::MakeRectXY(gfx::RectFToSkRect(face), 12, 12)).detach(), true);
      const auto& image = card.thumbnail;
      if (!image.isNull() && image.width() > 0 && image.height() > 0) {
        // Cover the card from the top of the page: the top is what people recognise.
        const float aspect = face.width() / face.height();
        int sw = image.width(), sh = static_cast<int>(std::lround(image.width() / aspect));
        int sx = 0;
        if (sh > image.height()) { sh = image.height(); sw = static_cast<int>(std::lround(sh * aspect)); sx = (image.width() - sw) / 2; }
        canvas->DrawImageInt(image, sx, 0, sw, sh, face.x(), face.y(), face.width(), face.height(), true);
      } else {
        const int tile = std::clamp(static_cast<int>(face.height() * 0.16f), 40, 88);
        const gfx::Rect icon(face.CenterPoint().x() - tile / 2, face.CenterPoint().y() - tile / 2 - 14, tile, tile);
        PaintTabIcon(canvas, icon, card, p, tile * 0.22f);
        const auto font = Font(3, 13, 400);
        const std::u16string host = base::UTF8ToUTF16(BareHost(card.url));
        canvas->DrawStringRectWithFlags(gfx::ElideText(host.empty() ? card.title : host, font, face.width() - 48, gfx::ELIDE_TAIL),
            font, p.ink_muted, gfx::Rect(face.x() + 24, icon.bottom() + 16, face.width() - 48, 18), gfx::Canvas::TEXT_ALIGN_CENTER);
      }
    }
    StrokeRound(canvas, face, 12, p.line);
  }
  bool SkipDefaultKeyEventProcessing(const ui::KeyEvent& event) override { return event.key_code() == ui::VKEY_TAB; }
  bool OnKeyPressed(const ui::KeyEvent& event) override { return root_->OnKeyPressed(event); }
  bool OnKeyReleased(const ui::KeyEvent& event) override { return root_->OnKeyReleased(event); }
  int index() const { return index_; }

 private:
  raw_ptr<TabSwitcherView> root_;
  int index_;
  raw_ptr<views::View> shade_ = nullptr;
};
BEGIN_METADATA(TabSwitcherCard)
END_METADATA

TabSwitcherView::Card::Card() = default;
TabSwitcherView::Card::Card(const Card&) = default;
TabSwitcherView::Card& TabSwitcherView::Card::operator=(const Card&) = default;
TabSwitcherView::Card::~Card() = default;
TabSwitcherView::Host::Host() = default;
TabSwitcherView::Host::Host(Host&&) = default;
TabSwitcherView::Host& TabSwitcherView::Host::operator=(Host&&) = default;
TabSwitcherView::Host::~Host() = default;

TabSwitcherView::TabSwitcherView(Host host) : host_(std::move(host)) {
  SetPaintToLayer(); layer()->SetFillsBoundsOpaquely(false);
  SetFocusBehavior(FocusBehavior::ALWAYS);
  GetViewAccessibility().SetRole(ax::mojom::Role::kListBox);
  GetViewAccessibility().SetName(T(u"Ostatnio używane karty"));
}

TabSwitcherView::~TabSwitcherView() { StopTicking(); }

sidebar::DesignPalette TabSwitcherView::palette() const { return host_.palette.Run(); }

float TabSwitcherView::scale() const {
  const float stage = std::max(1, width() - kTimelineWidth - 2 * kTimelineInset + 60);
  return std::clamp(std::min(stage / kDesignStage, height() / kDesignHeight), 0.42f, 1.3f);
}

void TabSwitcherView::Open(std::vector<Card> cards, int selected) {
  cards_ = std::move(cards);
  selected_ = cards_.empty() ? 0 : std::clamp(selected, 0, static_cast<int>(cards_.size()) - 1);
  active_ = true; revealed_ = false; hovered_tick_ = -1; pressed_card_ = -1;
  opened_at_ = base::Time::Now();
  position_ = selected_; velocity_ = 0;
  StopTicking();
  layer()->GetAnimator()->StopAnimating();
  layer()->SetOpacity(0);
  RebuildCards();
  InvalidateLayout();
  SchedulePaint();
  reveal_timer_.Start(FROM_HERE, kRevealDelay, base::BindOnce(&TabSwitcherView::OnRevealTimer, base::Unretained(this)));
}

void TabSwitcherView::RebuildCards() {
  for (auto& view : views_) { auto* child = view.get(); view = nullptr; RemoveChildViewT(child); }
  views_.clear();
  views_.resize(cards_.size());
  // The current tab is drawn last, on top: older cards stand behind it.
  for (int i = static_cast<int>(cards_.size()) - 1; i >= 0; --i) {
    views_[i] = AddChildView(std::make_unique<TabSwitcherCard>(this, i));
    views_[i]->SetCard(cards_[i]);
    views_[i]->GetViewAccessibility().SetIsSelected(i == selected_);
  }
}

void TabSwitcherView::OnRevealTimer() {
  if (!active_) return;
  // The release may have happened before the switcher took the keyboard.
  if (host_.modifier_down && !host_.modifier_down.Run()) { Commit(); return; }
  Reveal();
}

void TabSwitcherView::Reveal() {
  if (!active_ || revealed_) return;
  revealed_ = true;
  reveal_timer_.Stop();
  const bool motion = host_.animations && host_.animations.Run();
  if (motion) {
    // The stack slides one step back in time as it appears.
    position_ = std::max(0, selected_ - 1);
    PlaceCards();
    ui::ScopedLayerAnimationSettings settings(layer()->GetAnimator());
    settings.SetTransitionDuration(base::Milliseconds(220));
    settings.SetTweenType(gfx::Tween::EASE_OUT);
    layer()->SetOpacity(1);
    Animate();
  } else {
    position_ = selected_;
    PlaceCards();
    layer()->SetOpacity(1);
  }
  if (selected_ < static_cast<int>(views_.size())) views_[selected_]->RequestFocus();
}

void TabSwitcherView::Step(bool backwards) {
  const int count = static_cast<int>(cards_.size());
  if (!active_ || count == 0) return;
  Select((selected_ + (backwards ? count - 1 : 1)) % count);
}

void TabSwitcherView::Select(int index) {
  if (!active_ || cards_.empty()) return;
  index = std::clamp(index, 0, static_cast<int>(cards_.size()) - 1);
  if (index == selected_) return;
  const int previous = selected_;
  selected_ = index;
  if (previous < static_cast<int>(views_.size())) views_[previous]->GetViewAccessibility().SetIsSelected(false);
  views_[selected_]->GetViewAccessibility().SetIsSelected(true);
  if (revealed_) {
    if (host_.animations && host_.animations.Run()) Animate(); else { position_ = selected_; PlaceCards(); }
    views_[selected_]->RequestFocus();
  } else {
    position_ = selected_;
    PlaceCards();
  }
  SchedulePaint();
}

void TabSwitcherView::Commit() {
  if (!active_) return;
  Finish(cards_.empty() ? std::nullopt : std::optional<uint64_t>(cards_[selected_].id));
}

void TabSwitcherView::Cancel() {
  if (active_) Finish(std::nullopt);
}

void TabSwitcherView::Finish(std::optional<uint64_t> id) {
  active_ = false; revealed_ = false;
  reveal_timer_.Stop();
  StopTicking();
  layer()->GetAnimator()->StopAnimating();
  layer()->SetOpacity(0);
  if (host_.done) host_.done.Run(id);
}

void TabSwitcherView::SetThumbnail(uint64_t id, const gfx::ImageSkia& image) {
  for (size_t i = 0; i < cards_.size(); ++i) {
    if (cards_[i].id != id) continue;
    cards_[i].thumbnail = image;
    if (i < views_.size()) views_[i]->SchedulePaint();
  }
}

void TabSwitcherView::Layout(PassKey) {
  const float s = scale();
  const int stage = std::max(1, width() - kTimelineWidth - 2 * kTimelineInset + 60);
  const int card_w = static_cast<int>(std::lround(kDesignCardWidth * s));
  const int card_h = static_cast<int>(std::lround(kDesignCardHeight * s));
  const int rise = static_cast<int>(std::lround(kDepth * kRise * s));
  const int caption_h = 70;
  const int block = rise + card_h + static_cast<int>(23 * s) + caption_h;
  const int top = std::max(0, (height() - block) / 2) + rise;
  card_bounds_ = gfx::Rect((stage - card_w) / 2, top, card_w, card_h);
  caption_ = gfx::Rect(0, card_bounds_.bottom() + static_cast<int>(23 * s), stage, caption_h);
  timeline_ = gfx::Rect(width() - kTimelineInset - kTimelineWidth, top - rise - 24, kTimelineWidth,
                        caption_.bottom() - (top - rise - 24));
  for (auto& view : views_) {
    gfx::Rect bounds = card_bounds_; bounds.Outset(kMargin);
    view->SetBoundsRect(bounds);
  }
  PlaceCards();
}

void TabSwitcherView::PlaceCards() {
  const float s = scale();
  const gfx::PointF pivot(kMargin + card_bounds_.width() / 2.f, kMargin);
  for (int i = 0; i < static_cast<int>(views_.size()); ++i) {
    auto* card = views_[i].get();
    const float d = i - position_;
    float dy, scale, opacity, shade = 0;
    if (d >= 0) {
      dy = -d * kRise * s;
      scale = 1 - kShrink * d;
      opacity = std::clamp(kDepth + 1 - d, 0.f, 1.f);
      shade = std::min(kShadeMax, kShadeStep * d);
    } else {
      // Cards already passed move forward, out of the picture.
      const float t = std::min(1.f, -d);
      dy = 90 * s * t; scale = 1 + 0.08f * t; opacity = 1 - t;
    }
    gfx::Transform transform;
    transform.Translate(pivot.x(), pivot.y() + dy);
    transform.Scale(scale, scale);
    transform.Translate(-pivot.x(), -pivot.y());
    card->layer()->SetTransform(transform);
    card->layer()->SetOpacity(opacity);
    card->SetShade(shade);
    card->SetVisible(opacity > 0.01f);
  }
}

void TabSwitcherView::Animate() {
  auto* compositor = GetWidget() ? GetWidget()->GetCompositor() : nullptr;
  if (!compositor) { position_ = selected_; velocity_ = 0; PlaceCards(); return; }
  if (ticking_ != compositor) {
    StopTicking();
    compositor->AddAnimationObserver(this);
    ticking_ = compositor; last_tick_ = base::TimeTicks();
  }
}

void TabSwitcherView::StopTicking() {
  if (ticking_) ticking_->RemoveAnimationObserver(this);
  ticking_ = nullptr;
}

void TabSwitcherView::OnAnimationStep(base::TimeTicks timestamp) {
  const float dt = last_tick_.is_null() ? 1.f / 60 : static_cast<float>((timestamp - last_tick_).InSecondsF());
  last_tick_ = timestamp;
  // dur-move: a stiff spring settles in about 220 ms without wobbling.
  const auto next = sidebar::timeline_spring(position_, velocity_, static_cast<float>(selected_), dt, 260, 32);
  position_ = next.value; velocity_ = next.velocity;
  if (next.settled) { position_ = selected_; velocity_ = 0; }
  PlaceCards();
  if (next.settled) StopTicking();
}

void TabSwitcherView::OnCompositingShuttingDown(ui::Compositor*) { StopTicking(); }

void TabSwitcherView::RemovedFromWidget() { StopTicking(); }

gfx::Rect TabSwitcherView::TickBounds(int index) const {
  const int count = static_cast<int>(cards_.size());
  if (count == 0) return {};
  const int span = timeline_.height() - 44;
  const int pitch = count > 1 ? std::min(kTickPitch, span / (count - 1)) : 0;
  const int bottom = timeline_.CenterPoint().y() + pitch * (count - 1) / 2;
  const int y = bottom - pitch * index;
  const int h = std::clamp(pitch, 12, 44);
  return gfx::Rect(timeline_.x(), y - h / 2, timeline_.width(), h);
}

int TabSwitcherView::TickAt(const gfx::Point& point) const {
  if (!timeline_.Contains(point)) return -1;
  for (int i = 0; i < static_cast<int>(cards_.size()); ++i)
    if (TickBounds(i).Contains(point)) return i;
  return -1;
}

int TabSwitcherView::CardAt(const gfx::Point& point) const {
  const float s = scale();
  const int front = static_cast<int>(std::lround(position_));
  for (int i = std::max(0, front); i < static_cast<int>(cards_.size()) && i <= front + kDepth; ++i) {
    const float d = i - position_;
    const float sc = 1 - kShrink * std::max(0.f, d);
    const float w = card_bounds_.width() * sc, h = card_bounds_.height() * sc;
    const gfx::RectF rect(card_bounds_.CenterPoint().x() - w / 2, card_bounds_.y() - d * kRise * s, w, h);
    if (rect.Contains(gfx::PointF(point))) return i;
  }
  return -1;
}

std::u16string TabSwitcherView::When(int index) const {
  if (index < 0 || index >= static_cast<int>(cards_.size())) return {};
  if (index == 0) return T(u"Teraz");
  const base::Time used = cards_[index].used;
  if (used.is_null()) return {};
  const base::TimeDelta delta = std::max(base::TimeDelta(), opened_at_ - used);
  if (delta < base::Hours(1))
    return TF(u"{} min", {base::NumberToString16(std::max<int64_t>(1, delta.InMinutes()))});
  base::Time::Exploded now, then;
  opened_at_.LocalExplode(&now); used.LocalExplode(&then);
  const bool today = now.year == then.year && now.month == then.month && now.day_of_month == then.day_of_month;
  if (today || delta < base::Hours(6))
    return TF(u"{} godz.", {base::NumberToString16(delta.InHours())});
  base::Time midnight;
  if (base::Time::FromLocalExploded({now.year, now.month, 0, now.day_of_month, 0, 0, 0, 0}, &midnight) &&
      used >= midnight - base::Days(1))
    return T(u"Wczoraj");
  if (delta < base::Days(7))
    return TF(u"{} dni", {base::NumberToString16(std::max<int64_t>(2, delta.InDays()))});
  return base::TimeFormatShortDate(used);
}

void TabSwitcherView::OnPaint(gfx::Canvas* canvas) {
  const auto p = palette();
  // The workspace's void color covers the window: a window cannot blur what
  // other windows draw, and on Windows a translucent fill lets page text through.
  canvas->DrawColor(p.bg_void);
  if (cards_.empty()) return;
  const float s = scale();

  // Caption: the front card's icon and title in display italic, its address below.
  const auto& front = cards_[selected_];
  const int tile = 24;
  const auto title_font = Font(1, std::clamp(34 * s, 22.f, 40.f), 400, true);
  const std::u16string host = base::UTF8ToUTF16(BareHost(front.url));
  std::u16string title = front.title.empty() ? host : front.title;
  title = gfx::ElideText(title, title_font, std::max(80, card_bounds_.width() - tile - 12), gfx::ELIDE_TAIL);
  const int title_w = gfx::GetStringWidth(title, title_font);
  const int line_h = title_font.GetHeight();
  const int row_w = tile + 12 + title_w;
  const int row_x = caption_.CenterPoint().x() - row_w / 2;
  PaintTabIcon(canvas, gfx::Rect(row_x, caption_.y() + (line_h - tile) / 2, tile, tile), front, p, 6);
  canvas->DrawStringRect(title, title_font, p.ink, gfx::Rect(row_x + tile + 12, caption_.y(), title_w + 2, line_h));
  if (!host.empty()) {
    const auto host_font = Font(3, 12, 500), path_font = Font(3, 12, 400);
    std::string path_part(front.url.path());
    if (path_part == "/") path_part.clear();
    if (front.url.has_query()) path_part += "?" + std::string(front.url.query());
    const int host_w = gfx::GetStringWidth(host, host_font);
    const auto path = gfx::ElideText(base::UTF8ToUTF16(path_part), path_font,
                                     std::max(0, card_bounds_.width() - host_w), gfx::ELIDE_TAIL);
    const int path_w = path.empty() ? 0 : gfx::GetStringWidth(path, path_font);
    const int x = caption_.CenterPoint().x() - (host_w + path_w) / 2;
    const int y = caption_.y() + line_h + 6;
    canvas->DrawStringRect(host, host_font, p.ink, gfx::Rect(x, y, host_w + 1, 16));
    if (!path.empty()) canvas->DrawStringRect(path, path_font, p.ink_muted, gfx::Rect(x + host_w, y, path_w + 1, 16));
  }

  // The time line: now at the bottom, older tabs above, the selection in the accent.
  const auto label_font = Font(2, 12, 500);
  for (int i = 0; i < static_cast<int>(cards_.size()); ++i) {
    const gfx::Rect tick = TickBounds(i);
    const bool on = i == selected_;
    const int bar_w = on ? 34 : i == hovered_tick_ ? 22 : i == 0 ? 18 : 12;
    const gfx::RectF bar(tick.right() - 4 - bar_w, tick.CenterPoint().y() - 1, bar_w, 2);
    FillRound(canvas, bar, 1, on ? p.accent : p.line_strong);
    if (!on && i != 0 && i != hovered_tick_) continue;
    const std::u16string when = When(i);
    if (when.empty()) continue;
    canvas->DrawStringRectWithFlags(when, label_font, on ? p.accent_ink : p.ink_muted,
        gfx::Rect(tick.x(), tick.CenterPoint().y() - 8, bar.x() - 12 - tick.x(), 16), gfx::Canvas::TEXT_ALIGN_RIGHT);
  }
}

bool TabSwitcherView::SkipDefaultKeyEventProcessing(const ui::KeyEvent& event) {
  return event.key_code() == ui::VKEY_TAB;
}

bool TabSwitcherView::OnKeyPressed(const ui::KeyEvent& event) {
  if (!active_) return false;
  switch (event.key_code()) {
    case ui::VKEY_TAB: Step(event.IsShiftDown()); return true;
    case ui::VKEY_UP: case ui::VKEY_RIGHT: Step(false); return true;
    case ui::VKEY_DOWN: case ui::VKEY_LEFT: Step(true); return true;
    case ui::VKEY_RETURN: case ui::VKEY_SPACE: Commit(); return true;
    case ui::VKEY_ESCAPE: Cancel(); return true;
    default: return IsControl(event.key_code());
  }
}

bool TabSwitcherView::OnKeyReleased(const ui::KeyEvent& event) {
  if (!active_ || !IsControl(event.key_code())) return false;
  Commit();
  return true;
}

bool TabSwitcherView::OnMousePressed(const ui::MouseEvent& event) {
  if (!active_) return false;
  pressed_card_ = TickAt(event.location()) >= 0 ? -2 : CardAt(event.location());
  return true;
}

void TabSwitcherView::OnMouseReleased(const ui::MouseEvent& event) {
  if (!active_) return;
  if (const int tick = TickAt(event.location()); tick >= 0) { Select(tick); return; }
  const int card = CardAt(event.location());
  if (card >= 0 && card == pressed_card_) {
    if (card == selected_) Commit(); else Select(card);
    return;
  }
  // A click beside the stack keeps the tab you started from.
  if (card < 0 && pressed_card_ == -1) Cancel();
}

void TabSwitcherView::OnMouseMoved(const ui::MouseEvent& event) {
  const int tick = TickAt(event.location());
  if (tick == hovered_tick_) return;
  hovered_tick_ = tick;
  SchedulePaint();
}

void TabSwitcherView::OnMouseExited(const ui::MouseEvent&) {
  if (hovered_tick_ < 0) return;
  hovered_tick_ = -1;
  SchedulePaint();
}

bool TabSwitcherView::OnMouseWheel(const ui::MouseWheelEvent& event) {
  if (!active_ || event.y_offset() == 0) return false;
  // Scrolling up reaches back in time, the way the stack rises.
  Select(selected_ + (event.y_offset() > 0 ? 1 : -1));
  return true;
}

void TabSwitcherView::OnThemeChanged() {
  views::View::OnThemeChanged();
  SchedulePaint();
  for (auto& card : views_) { card->InvalidateLayout(); card->SchedulePaint(); }
}

BEGIN_METADATA(TabSwitcherView)
END_METADATA

}  // namespace zitter::prototype
