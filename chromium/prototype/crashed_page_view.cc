// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "chromium/prototype/crashed_page_view.h"

#include <algorithm>
#include <utility>

#include "chromium/prototype/i18n.h"
#include "chromium/prototype/sidebar_scene.rs.h"
#include "chromium/prototype/toolbar_menu.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/text_utils.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/button.h"

namespace zitter::prototype {

namespace {

// The network error page's card (lab_clients.cc PrepareErrorPage).
constexpr int kCardWidth = 480;
constexpr int kPadding = 32;
constexpr int kRadius = 20;
constexpr int kSymbol = 44;
constexpr int kButtonHeight = 32;
constexpr char kReloadIcon[] = "M13 8a5 5 0 1 1-1.5-3.6M13 2.5v3h-3";

gfx::FontList TitleFont() { return DesignFont(2, 26, 600); }
gfx::FontList HintFont() { return DesignFont(2, 15, 400); }
gfx::FontList CodeFont() { return DesignFont(3, 12, 400); }
gfx::FontList ButtonFont() { return DesignFont(2, 13, 600); }

int TextHeight(const std::u16string& text, const gfx::FontList& font, int width) {
  int w = width, h = 0;
  gfx::Canvas::SizeStringInt(text, font, &w, &h, 0, gfx::Canvas::MULTI_LINE);
  return h;
}

}  // namespace

// The view's one action: a primary pill (design: Button, zt-btn-primary).
class CrashedPageView::ReloadButton final : public views::Button {
  METADATA_HEADER(ReloadButton, views::Button)

 public:
  ReloadButton(PressedCallback callback, CrashedPageView* owner)
      : views::Button(std::move(callback)), owner_(owner) {
    text_ = T(u"Odśwież kartę");
    GetViewAccessibility().SetName(text_);
    SetPreferredSize(gfx::Size(
        gfx::GetStringWidth(text_, ButtonFont()) + 16 + 8 + 2 * 16, kButtonHeight));
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const auto palette = owner_->palette_.Run();
    const gfx::RectF box(GetLocalBounds());
    FillRound(canvas, box, box.height() / 2, palette.accent);
    if (HasFocus() || GetState() == STATE_HOVERED || GetState() == STATE_PRESSED)
      FillRound(canvas, box, box.height() / 2, SkColorSetA(palette.on_accent, 0x1F));
    StrokeIcon(canvas, kReloadIcon, gfx::RectF(16, (height() - 16) / 2.f, 16, 16),
               palette.on_accent);
    canvas->DrawStringRectWithFlags(text_, ButtonFont(), palette.on_accent,
                                    gfx::Rect(40, 0, width() - 40, height()),
                                    gfx::Canvas::NO_SUBPIXEL_RENDERING);
  }

  void OnFocus() override { views::Button::OnFocus(); SchedulePaint(); }
  void OnBlur() override { views::Button::OnBlur(); SchedulePaint(); }

 private:
  raw_ptr<CrashedPageView> owner_;
  std::u16string text_;
};

BEGIN_METADATA(CrashedPageView, ReloadButton)
END_METADATA

CrashedPageView::CrashedPageView(
    base::RepeatingCallback<sidebar::DesignPalette()> palette,
    base::RepeatingClosure reload)
    : palette_(std::move(palette)) {
  reload_ = AddChildView(std::make_unique<ReloadButton>(
      base::BindRepeating([](base::RepeatingClosure reload, const ui::Event&) {
        reload.Run();
      }, std::move(reload)),
      this));
  GetViewAccessibility().SetRole(ax::mojom::Role::kAlert);
  SetReason(PageCrashReason::kCrashed);
}

CrashedPageView::~CrashedPageView() = default;

views::View* CrashedPageView::reload_button_for_testing() const {
  return reload_;
}

void CrashedPageView::SetReason(PageCrashReason reason) {
  reason_ = reason;
  GetViewAccessibility().SetName(Title() + u". " + Hint());
  InvalidateLayout();
  SchedulePaint();
}

std::u16string CrashedPageView::Title() const {
  switch (reason_) {
    case PageCrashReason::kOutOfMemory:
      return T(u"Stronie zabrakło pamięci");
    case PageCrashReason::kKilled:
      return T(u"Strona została zatrzymana");
    case PageCrashReason::kCrashed:
      return T(u"Ta strona uległa awarii");
  }
}

std::u16string CrashedPageView::Hint() const {
  return reason_ == PageCrashReason::kOutOfMemory
      ? T(u"Zamknij nieużywane karty i odśwież tę kartę.")
      : T(u"Pozostałe karty działają dalej. Odśwież kartę, żeby wczytać stronę ponownie.");
}

std::u16string CrashedPageView::Code() const {
  switch (reason_) {
    case PageCrashReason::kOutOfMemory:
      return u"RENDERER_OUT_OF_MEMORY";
    case PageCrashReason::kKilled:
      return u"RENDERER_KILLED";
    case PageCrashReason::kCrashed:
      return u"RENDERER_CRASHED";
  }
}

gfx::Rect CrashedPageView::CardBounds() const {
  const int width = std::min(kCardWidth, std::max(0, this->width() - 2 * 24));
  const int inner = std::max(0, width - 2 * kPadding);
  const int height = kPadding + kSymbol + 20 + TitleFont().GetHeight() + 8 +
                     TextHeight(Hint(), HintFont(), inner) + 24 + kButtonHeight + 24 +
                     1 + 14 + CodeFont().GetHeight() + kPadding;
  return gfx::Rect((this->width() - width) / 2,
                   std::max(0, (this->height() - height) / 2), width, height);
}

void CrashedPageView::Layout(PassKey) {
  const gfx::Rect card = CardBounds();
  const int inner = std::max(0, card.width() - 2 * kPadding);
  const int y = card.y() + kPadding + kSymbol + 20 + TitleFont().GetHeight() + 8 +
                TextHeight(Hint(), HintFont(), inner) + 24;
  const gfx::Size button = reload_->GetPreferredSize();
  reload_->SetBounds(card.x() + kPadding, y, std::min(button.width(), inner),
                     button.height());
}

void CrashedPageView::OnThemeChanged() {
  views::View::OnThemeChanged();
  SchedulePaint();
}

void CrashedPageView::OnPaint(gfx::Canvas* canvas) {
  const auto palette = palette_.Run();
  canvas->DrawColor(palette.bg_base);
  const gfx::Rect card = CardBounds();
  FillRound(canvas, gfx::RectF(card), kRadius, palette.line);
  gfx::RectF inside(card);
  inside.Inset(1);
  FillRound(canvas, inside, kRadius - 1, palette.bg_raised);

  const int x = card.x() + kPadding;
  const int inner = std::max(0, card.width() - 2 * kPadding);
  int y = card.y() + kPadding;
  FillRound(canvas, gfx::RectF(x, y, kSymbol, kSymbol), 13, palette.accent_soft);
  StrokeIcon(canvas, kWarnIcon, gfx::RectF(x + 12, y + 12, 20, 20), palette.accent_ink);
  y += kSymbol + 20;

  const auto title = TitleFont();
  canvas->DrawStringRectWithFlags(Title(), title, palette.ink,
                                  gfx::Rect(x, y, inner, title.GetHeight()),
                                  gfx::Canvas::NO_SUBPIXEL_RENDERING);
  y += title.GetHeight() + 8;

  const auto hint = HintFont();
  const int hint_height = TextHeight(Hint(), hint, inner);
  canvas->DrawStringRectWithFlags(Hint(), hint, palette.ink_muted,
                                  gfx::Rect(x, y, inner, hint_height),
                                  gfx::Canvas::MULTI_LINE | gfx::Canvas::NO_SUBPIXEL_RENDERING);
  y += hint_height + 24 + kButtonHeight + 24;

  canvas->FillRect(gfx::Rect(x, y, inner, 1), palette.line);
  y += 1 + 14;
  const auto code = CodeFont();
  canvas->DrawStringRectWithFlags(Code(), code, palette.ink_faint,
                                  gfx::Rect(x, y, inner, code.GetHeight()),
                                  gfx::Canvas::NO_SUBPIXEL_RENDERING);
}

BEGIN_METADATA(CrashedPageView)
END_METADATA

}  // namespace zitter::prototype
