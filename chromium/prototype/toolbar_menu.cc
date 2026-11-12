// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "chromium/prototype/toolbar_menu.h"

#include <algorithm>
#include <cctype>

#include "base/i18n/case_conversion.h"
#include "base/strings/string_number_conversions.h"
#include "cc/paint/paint_flags.h"
#include "chromium/prototype/assistant_visuals.h"
#include "chromium/prototype/i18n.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/text_utils.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/layout/box_layout.h"

namespace zitter::prototype {
namespace {

constexpr int kIcon = 16;

// The subset of SVG path data the design icons use: M L H V A Z, absolute or
// relative, with implicit repeats.
SkPath SvgPath(std::string_view d, const SkMatrix& transform) {
  SkPathBuilder path;
  SkPoint current{0, 0}, start{0, 0};
  char command = 0;
  size_t i = 0;
  auto skip = [&] { while (i < d.size() && (d[i] == ' ' || d[i] == ',')) ++i; };
  auto number = [&]() -> float {
    skip();
    size_t j = i;
    if (j < d.size() && (d[j] == '-' || d[j] == '+')) ++j;
    bool dot = false;
    while (j < d.size() && (std::isdigit(static_cast<unsigned char>(d[j])) || (d[j] == '.' && !dot))) {
      dot |= d[j] == '.';
      ++j;
    }
    double value = 0;
    base::StringToDouble(d.substr(i, j - i), &value);
    i = j;
    return static_cast<float>(value);
  };
  while (true) {
    skip();
    if (i >= d.size()) break;
    if (std::isalpha(static_cast<unsigned char>(d[i]))) {
      command = d[i++];
      if (command == 'z' || command == 'Z') { path.close(); current = start; continue; }
    }
    const bool relative = std::islower(static_cast<unsigned char>(command));
    switch (std::tolower(static_cast<unsigned char>(command))) {
      case 'm': {
        SkPoint p{number(), number()};
        if (relative) p += current;
        path.moveTo(p);
        current = start = p;
        command = relative ? 'l' : 'L';  // Further pairs are lines.
        break;
      }
      case 'l': {
        SkPoint p{number(), number()};
        if (relative) p += current;
        path.lineTo(p);
        current = p;
        break;
      }
      case 'h': {
        const float x = number();
        current.fX = relative ? current.fX + x : x;
        path.lineTo(current);
        break;
      }
      case 'v': {
        const float y = number();
        current.fY = relative ? current.fY + y : y;
        path.lineTo(current);
        break;
      }
      case 'a': {
        const float rx = number(), ry = number(), rotation = number();
        const bool large = number() != 0, sweep = number() != 0;
        SkPoint p{number(), number()};
        if (relative) p += current;
        // SVG sweep-flag 1 is clockwise.
        path.rArcTo({rx, ry}, rotation, large ? SkPathBuilder::kLarge_ArcSize : SkPathBuilder::kSmall_ArcSize,
                    sweep ? SkPathDirection::kCW : SkPathDirection::kCCW, p - current);
        current = p;
        break;
      }
      default:
        return path.detach(&transform);
    }
  }
  return path.detach(&transform);
}

}  // namespace

void StrokeIcon(gfx::Canvas* canvas, std::string_view d, gfx::RectF box, SkColor color) {
  if (d == kAssistantSparkIcon) { PaintAssistantSpark(canvas, box, color); return; }
  const float scale = box.width() / kIcon;
  SkMatrix transform = SkMatrix::Translate(box.x(), box.y());
  transform.preScale(scale, scale);
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kStroke_Style);
  flags.setStrokeWidth(1.6f * scale);
  flags.setStrokeCap(cc::PaintFlags::kRound_Cap);
  flags.setStrokeJoin(cc::PaintFlags::kRound_Join);
  flags.setColor(color);
  canvas->DrawPath(SvgPath(d, transform), flags);
}

void FillCircle(gfx::Canvas* canvas, gfx::PointF center, float radius, SkColor color, bool stroke) {
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(color);
  if (stroke) { flags.setStyle(cc::PaintFlags::kStroke_Style); flags.setStrokeWidth(1.6f); }
  canvas->DrawCircle(center, radius, flags);
}

void FillRound(gfx::Canvas* canvas, gfx::RectF rect, float radius, SkColor color) {
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(color);
  canvas->DrawRoundRect(rect, radius, flags);
}

// A design system family (1 display, 2 sans, 3 mono; design.rs family_id).
gfx::FontList DesignFont(uint32_t family, int size, int weight) {
  return gfx::FontList({std::string(sidebar::design_font_family(family))}, gfx::Font::NORMAL, size,
                       static_cast<gfx::Font::Weight>(weight));
}

MenuRow::MenuRow(PressedCallback callback, const sidebar::DesignPalette& palette, int height) : views::Button(std::move(callback)), palette_(palette)  {
    SetPreferredSize(gfx::Size(kMenuWidth - 8, height));
    SetNotifyEnterExitOnChild(true);
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(0, 8), 8));
    layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  }

void MenuRow::SetText(std::u16string title, std::u16string detail, bool warn, int lead) {
    title_ = std::move(title); detail_ = std::move(detail); warn_ = warn; lead_ = lead;
    GetViewAccessibility().SetName(detail_.empty() ? title_ : title_ + u", " + detail_);
  }

void MenuRow::SetGlyph(const char* glyph) { glyph_ = glyph; }

void MenuRow::SetRing(bool ring) { ring_ = ring; }

void MenuRow::SetSelected(bool selected) { selected_ = selected; SchedulePaint(); }

void MenuRow::AddTrailing(views::View* view, bool always) { trailing_.push_back({view, always}); UpdateTrailing(); }

views::View* MenuRow::text_slot() { return slot_; }

void MenuRow::Init() {
    if (glyph_) {
      auto* icon = AddChildView(std::make_unique<views::View>());
      icon->SetPreferredSize(gfx::Size(16, 16));
      icon->SetCanProcessEventsWithinSubtree(false);
      icon_slot_ = icon;
    }
    slot_ = AddChildView(std::make_unique<views::View>());
    // The text column spans the row's height; the row draws the text in it.
    slot_->SetPreferredSize(gfx::Size(0, GetPreferredSize({}).height()));
    slot_->SetCanProcessEventsWithinSubtree(false);
    static_cast<views::BoxLayout*>(GetLayoutManager())->SetFlexForView(slot_, 1);
  }

void MenuRow::OnMouseEntered(const ui::MouseEvent& event) { views::Button::OnMouseEntered(event); UpdateTrailing(); }

void MenuRow::OnMouseExited(const ui::MouseEvent& event) { views::Button::OnMouseExited(event); UpdateTrailing(); }

void MenuRow::PaintButtonContents(gfx::Canvas* canvas) {
    const bool hovered = IsMouseHovered() || GetState() == STATE_PRESSED || selected_ || HasFocus();
    if (hovered) FillRound(canvas, gfx::RectF(GetLocalBounds()), 8, palette_.bg_hover);
    if (icon_slot_) {
      const gfx::RectF icon(icon_slot_->bounds());
      StrokeIcon(canvas, glyph_, icon, palette_.ink_muted);
      if (ring_) FillCircle(canvas, icon.CenterPoint(), 2, palette_.ink_muted, true);
    }
    if (!slot_) return;
    const auto box = slot_->bounds();
    const auto body = DesignFont(2, 13, 400);
    if (detail_.empty()) {
      canvas->DrawStringRectWithFlags(title_, body, palette_.ink, box, gfx::Canvas::NO_SUBPIXEL_RENDERING);
      return;
    }
    const auto label = DesignFont(2, 12, 400);
    const int top = box.y() + (box.height() - 34) / 2;
    canvas->DrawStringRectWithFlags(title_, body, palette_.ink, gfx::Rect(box.x(), top, box.width(), 18), 0);
    int x = box.x();
    const SkColor tone = warn_ ? palette_.warn : palette_.ink_muted;
    if (warn_) { StrokeIcon(canvas, kWarnIcon, gfx::RectF(x, top + 20, 12, 12), tone); x += 16; }
    canvas->DrawStringRectWithFlags(detail_, label, tone, gfx::Rect(x, top + 18, box.right() - x, 16), 0);
  }

void MenuRow::OnFocus() { views::Button::OnFocus(); SchedulePaint(); }

void MenuRow::OnBlur() { views::Button::OnBlur(); SchedulePaint(); }

void MenuRow::UpdateTrailing() {
    const bool hovered = IsMouseHovered();
    for (auto& [view, always] : trailing_) view->SetVisible(always || hovered);
    // Keep the text column the same width whether or not the actions show.
    InvalidateLayout();
  }

MenuRow::~MenuRow() = default;

MenuHeader::MenuHeader(const sidebar::DesignPalette& palette, std::u16string eyebrow) : eyebrow_(eyebrow.empty() ? T(u"Rozszerzenia") : std::move(eyebrow)), palette_(palette)  {
    SetPreferredSize(gfx::Size(kMenuWidth - 8, 26));
  }

void MenuHeader::OnPaint(gfx::Canvas* canvas) {
    const gfx::Rect box(8, 8, width() - 16, 14);
    canvas->DrawStringRectWithFlags(base::i18n::ToUpper(eyebrow_), DesignFont(2, 10, 600), palette_.ink_muted, box, 0);
  }

MenuSeparator::MenuSeparator(SkColor color) : color_(color)  { SetPreferredSize(gfx::Size(kMenuWidth - 8, 9)); }

void MenuSeparator::OnPaint(gfx::Canvas* canvas) { canvas->FillRect(gfx::Rect(8, 4, width() - 16, 1), color_); }

BEGIN_METADATA(MenuRow)
END_METADATA
BEGIN_METADATA(MenuHeader)
END_METADATA
BEGIN_METADATA(MenuSeparator)
END_METADATA

ShieldButton::ShieldButton(
    PressedCallback callback,
    base::RepeatingCallback<sidebar::DesignPalette()> palette)
    : views::Button(std::move(callback)), palette_(std::move(palette)) {
  SetPreferredSize(gfx::Size(28, 28));
  GetViewAccessibility().SetHasPopup(ax::mojom::HasPopup::kDialog);
  SetCount(0);
}

ShieldButton::~ShieldButton() = default;

void ShieldButton::SetOpen(bool open) {
  if (open_ != open) {
    open_ = open;
    SchedulePaint();
  }
}

void ShieldButton::SetCount(int count) {
  count_ = count;
  const auto name = count < 0
      ? T(u"Blokowanie reklam wyłączone na tej stronie")
      : TF(u"Blokowanie reklam: {} zablokowanych na tej stronie",
           {base::NumberToString16(count)});
  GetViewAccessibility().SetName(name);
  SetTooltipText(name);
  SchedulePaint();
}

void ShieldButton::PaintButtonContents(gfx::Canvas* canvas) {
  const auto palette = palette_.Run();
  const gfx::RectF box(GetLocalBounds());
  SkColor ink = palette.ink_muted;
  if (open_) {
    FillRound(canvas, box, 8, palette.accent_soft);
    ink = palette.accent_ink;
  } else if (GetState() == STATE_HOVERED || GetState() == STATE_PRESSED) {
    FillRound(canvas, box, 8, palette.bg_hover);
    ink = palette.ink;
  }
  const auto center = box.CenterPoint();
  StrokeIcon(canvas, count_ < 0 ? kShieldOffIcon : kShieldIcon,
             gfx::RectF(center.x() - 8, center.y() - 8, 16, 16), ink);
  if (count_ <= 0)
    return;

  const auto text = count_ > 99 ? std::u16string(u"99+")
                               : base::NumberToString16(count_);
  const auto font = DesignFont(3, 9, 600);
  const float width = std::max(14, gfx::GetStringWidth(text, font) + 6);
  const gfx::RectF pill(this->width() - width, height() - 13, width, 12);
  auto ring = pill;
  ring.Outset(1.5f);
  FillRound(canvas, ring, 7.5f, palette.bg_void);
  FillRound(canvas, pill, 6, palette.ink);
  canvas->DrawStringRectWithFlags(text, font, palette.bg_raised,
                                  gfx::ToEnclosingRect(pill),
                                  gfx::Canvas::TEXT_ALIGN_CENTER);
}

ShieldCard::ShieldCard(const sidebar::DesignPalette& palette)
    : palette_(palette) {
  SetPreferredSize(gfx::Size(kMenuWidth - 8, 88));
  GetViewAccessibility().SetRole(ax::mojom::Role::kStatus);
}

void ShieldCard::Set(int count, std::u16string site) {
  count_ = count;
  site_ = std::move(site);
  GetViewAccessibility().SetName(
      count_ < 0 ? T(u"Blokowanie wyłączone na tej stronie")
                 : TF(u"Zablokowano na tej stronie: {}",
                      {base::NumberToString16(count_)}));
  int width = kMenuWidth - 8 - 16;
  int height = 0;
  gfx::Canvas::SizeStringInt(Detail(), DesignFont(2, 12, 400), &width, &height,
                            0, gfx::Canvas::MULTI_LINE);
  SetPreferredSize(gfx::Size(kMenuWidth - 8, 48 + height + 8));
  SchedulePaint();
}

void ShieldCard::OnPaint(gfx::Canvas* canvas) {
  const gfx::RectF tile(8, 8, 32, 32);
  FillRound(canvas, tile, 6, count_ < 0 ? palette_.bg_hover : palette_.accent_soft);
  StrokeIcon(canvas, count_ < 0 ? kShieldOffIcon : kShieldIcon,
             gfx::RectF(16, 16, 16, 16),
             count_ < 0 ? palette_.ink_muted : palette_.accent_ink);
  const auto label = DesignFont(2, 12, 400);
  constexpr int x = 52;
  const int right = width() - 8;
  canvas->DrawStringRectWithFlags(
      count_ < 0 ? T(u"Blokowanie wyłączone na tej stronie")
                 : T(u"Zablokowano na tej stronie"),
      label, palette_.ink_muted, gfx::Rect(x, 6, right - x, 16), 0);
  canvas->DrawStringRectWithFlags(
      count_ < 0 ? u"—" : base::NumberToString16(count_), DesignFont(2, 13, 600),
      palette_.ink, gfx::Rect(x, 23, right - x, 18),
      gfx::Canvas::NO_SUBPIXEL_RENDERING);
  canvas->DrawStringRectWithFlags(
      Detail(), label, palette_.ink_muted,
      gfx::Rect(8, 48, width() - 16, height() - 56), gfx::Canvas::MULTI_LINE);
}

std::u16string ShieldCard::Detail() const {
  return site_.empty()
      ? T(u"Reklamy i elementy śledzące z list EasyList i EasyPrivacy.")
      : TF(u"{} · listy EasyList i EasyPrivacy oraz Twoje reguły.", {site_});
}

BEGIN_METADATA(ShieldButton)
END_METADATA
BEGIN_METADATA(ShieldCard)
END_METADATA

}  // namespace zitter::prototype
