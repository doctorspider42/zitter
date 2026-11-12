// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "chromium/prototype/fingerprint_toolbar.h"

#include <array>
#include <utility>

#include "base/memory/raw_ref.h"
#include "base/task/single_thread_task_runner.h"
#include "base/timer/timer.h"
#include "chromium/prototype/i18n.h"
#include "chromium/prototype/toolbar_menu.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"

namespace zitter::prototype {
namespace {

constexpr int kButton = 28;

// Fingerprint ridges and a turn arrow.
constexpr char kFingerprintIcon[] = "M4.2 4.2a5.5 5.5 0 0 1 7.6 0M3 11.5V9.5a5 5 0 0 1 10 0v1M5.5 13.5V9.5a2.5 2.5 0 0 1 5 0V12M8 9v5";
constexpr char kTurnIcon[] = "M13 8a5 5 0 1 1-1.5-3.6M13 2.5v3h-3";

// Fingerprint protection in the bar, before the pinned actions: an IconButton
// like theirs, shown while the profile's protection is on.
class FingerprintButton final : public views::Button {
  METADATA_HEADER(FingerprintButton, views::Button)
 public:
  FingerprintButton(PressedCallback callback, base::RepeatingCallback<sidebar::DesignPalette()> palette)
      : views::Button(std::move(callback)), palette_(std::move(palette)) {
    SetPreferredSize(gfx::Size(kButton, kButton));
    GetViewAccessibility().SetName(T(u"Ochrona przed fingerprintingiem"));
    SetTooltipText(T(u"Ochrona przed fingerprintingiem"));
    GetViewAccessibility().SetHasPopup(ax::mojom::HasPopup::kDialog);
  }
  void SetOpen(bool open) { if (open_ != open) { open_ = open; SchedulePaint(); } }
  void SetShake(int dx) { if (shake_ != dx) { shake_ = dx; SchedulePaint(); } }
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const auto p = palette_.Run();
    const gfx::RectF box(GetLocalBounds());
    SkColor ink = p.ink_muted;
    if (open_) { FillRound(canvas, box, 8, p.accent_soft); ink = p.accent_ink; }
    else if (GetState() == STATE_HOVERED || GetState() == STATE_PRESSED) { FillRound(canvas, box, 8, p.bg_hover); ink = p.ink; }
    StrokeIcon(canvas, kFingerprintIcon, gfx::RectF(box.CenterPoint().x() - 8 + shake_, box.CenterPoint().y() - 8, 16, 16), ink);
  }
 private:
  base::RepeatingCallback<sidebar::DesignPalette()> palette_;
  bool open_ = false;
  int shake_ = 0;
};
BEGIN_METADATA(FingerprintButton)
END_METADATA

// The panel's first block: what this site sees. An `accent-soft` tile with the
// fingerprint, the code in `url`, then what makes it change in `label`.
class FingerprintCard final : public views::View {
  METADATA_HEADER(FingerprintCard, views::View)
 public:
  explicit FingerprintCard(const sidebar::DesignPalette& palette) : palette_(palette) {
    SetPreferredSize(gfx::Size(kMenuWidth - 8, 88));
    GetViewAccessibility().SetRole(ax::mojom::Role::kStatus);
  }
  void SetCode(std::u16string code) {
    code_ = std::move(code);
    GetViewAccessibility().SetName(code_.empty() ? T(u"Ta strona nie dostaje odcisku")
                                                 : TF(u"Odcisk dla tej strony: {}", {code_}));
    SchedulePaint();
  }
  const std::u16string& code() const { return code_; }
  void SetShake(int dx) { if (shake_ != dx) { shake_ = dx; SchedulePaint(); } }
  void OnPaint(gfx::Canvas* canvas) override {
    const gfx::RectF tile(8, 8, 32, 32);
    FillRound(canvas, tile, 6, palette_.accent_soft);
    StrokeIcon(canvas, kFingerprintIcon, gfx::RectF(16, 16, 16, 16), palette_.accent_ink);
    const auto label = DesignFont(2, 12, 400);
    const int x = 52, right = width() - 8;
    canvas->DrawStringRectWithFlags(code_.empty() ? T(u"Ta strona nie dostaje odcisku") : T(u"Odcisk dla tej strony"),
                                    label, palette_.ink_muted, gfx::Rect(x, 6, right - x, 16), 0);
    canvas->DrawStringRectWithFlags(code_.empty() ? u"—" : code_, DesignFont(3, 13, 500), palette_.ink,
                                    gfx::Rect(x + shake_, 23, right - x, 18), gfx::Canvas::NO_SUBPIXEL_RENDERING);
    canvas->DrawStringRectWithFlags(T(u"Inny na każdej stronie i nowy po ponownym uruchomieniu Zittera."), label,
                                    palette_.ink_muted, gfx::Rect(8, 48, width() - 16, 32), gfx::Canvas::MULTI_LINE);
  }
 private:
  sidebar::DesignPalette palette_;
  std::u16string code_;
  int shake_ = 0;
};
BEGIN_METADATA(FingerprintCard)
END_METADATA

}  // namespace

class FingerprintToolbar::Panel final : public views::BubbleDialogDelegate {
 public:
  Panel(FingerprintToolbar& toolbar, views::View* anchor)
      : views::BubbleDialogDelegate(anchor, views::BubbleBorder::TOP_RIGHT),
        toolbar_(toolbar),
        palette_(toolbar.callbacks_.palette.Run()) {
    SetOwnershipOfNewWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
    SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
    set_margins(gfx::Insets(4));
    set_corner_radius(12);
    set_fixed_width(kMenuWidth);
    SetBackgroundColor(palette_.bg_raised);
    SetShowCloseButton(false);
    SetAccessibleWindowRole(ax::mojom::Role::kDialog);
    SetTitle(T(u"Ochrona przed fingerprintingiem"));
    SetShowTitle(false);
    auto body = std::make_unique<views::View>();
    body->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical));
    body->AddChildView(
        std::make_unique<MenuHeader>(palette_, T(u"Odcisk przeglądarki")));
    card_ = body->AddChildView(std::make_unique<FingerprintCard>(palette_));
    body->AddChildView(std::make_unique<MenuSeparator>(palette_.line));
    auto add_row = [&](std::u16string text, const char* glyph,
                       base::RepeatingClosure action) {
      auto* row = body->AddChildView(
          std::make_unique<MenuRow>(std::move(action), palette_, kFooterHeight));
      row->SetGlyph(glyph);
      row->Init();
      row->SetText(std::move(text), {}, false, 16);
      return row;
    };
    auto weak = toolbar.weak_factory_.GetWeakPtr();
    rotate_ = add_row(T(u"Zmień odcisk"), kTurnIcon,
                      base::BindRepeating(&FingerprintToolbar::Rotate, weak));
    protection_ = add_row(
        toolbar.callbacks_.enabled.Run() ? T(u"Wyłącz ochronę")
                                         : T(u"Włącz ochronę"),
        kShieldOffIcon,
        base::BindRepeating(&FingerprintToolbar::ToggleProtection, weak));
    add_row(T(u"Ustawienia prywatności"), kGearIcon,
            base::BindRepeating(&FingerprintToolbar::OpenSettings, weak))
        ->SetRing(true);
    SetContentsView(std::move(body));
    Refresh();
  }

  void Refresh() {
    card_->SetCode(toolbar_->callbacks_.site_code.Run());
    rotate_->SetEnabled(toolbar_->callbacks_.enabled.Run());
  }

  void Tremor() {
    if (!gfx::Animation::ShouldRenderRichAnimation())
      return;
    step_ = 0;
    timer_.Start(FROM_HERE, base::Milliseconds(45), base::BindRepeating(
        [](Panel* panel) {
          static constexpr std::array<int, 4> kOffsets = {1, -1, 1, 0};
          if (panel->step_ >= kOffsets.size()) {
            panel->timer_.Stop();
            return;
          }
          const int dx = kOffsets[panel->step_++];
          panel->card_->SetShake(dx);
          if (auto* button = views::AsViewClass<FingerprintButton>(
                  panel->toolbar_->button_.view())) {
            button->SetShake(dx);
          }
        },
        base::Unretained(this)));
  }

  const std::u16string& code() const { return card_->code(); }
  views::View* rotate_button() const { return rotate_; }
  views::View* protection_button() const { return protection_; }

  void DetachContents() {
    timer_.Stop();
    card_ = nullptr;
    rotate_ = nullptr;
    protection_ = nullptr;
  }

 private:
  const raw_ref<FingerprintToolbar> toolbar_;
  const sidebar::DesignPalette palette_;
  raw_ptr<FingerprintCard> card_ = nullptr;
  raw_ptr<MenuRow> rotate_ = nullptr;
  raw_ptr<MenuRow> protection_ = nullptr;
  base::RepeatingTimer timer_;
  size_t step_ = 0;
};

FingerprintToolbar::Callbacks::Callbacks() = default;
FingerprintToolbar::Callbacks::Callbacks(Callbacks&&) = default;
FingerprintToolbar::Callbacks::~Callbacks() = default;

FingerprintToolbar::FingerprintToolbar(Callbacks callbacks)
    : callbacks_(std::move(callbacks)) {}

FingerprintToolbar::~FingerprintToolbar() {
  weak_factory_.InvalidateWeakPtrs();
  DestroyPanel();
}

std::unique_ptr<views::View> FingerprintToolbar::CreateButton(bool private_mode) {
  auto button = std::make_unique<FingerprintButton>(
      base::BindRepeating(&FingerprintToolbar::Toggle,
                          weak_factory_.GetWeakPtr()),
      callbacks_.palette);
  button_.SetView(button.get());
  UpdateButton(private_mode);
  return button;
}

void FingerprintToolbar::UpdateButton(bool private_mode) {
  private_mode_ = private_mode;
  auto* button = views::AsViewClass<FingerprintButton>(button_.view());
  if (!button)
    return;
  const bool enabled = callbacks_.enabled.Run();
  button->SetOpen((widget_ && !widget_->IsClosed()) || (private_mode && enabled));
  button->SetTooltipText(
      !private_mode ? T(u"Ochrona przed fingerprintingiem")
      : enabled ? T(u"Ochrona przed fingerprintingiem w trybie prywatnym: włączona")
                : T(u"Ochrona przed fingerprintingiem w trybie prywatnym: wyłączona"));
}

void FingerprintToolbar::Toggle() {
  if (widget_) {
    Close();
    return;
  }
  auto* button = button_.view();
  if (!button || !button->IsDrawn() || !button->GetWidget())
    return;
  callbacks_.before_open.Run();
  panel_ = std::make_unique<Panel>(*this, button);
  // The client-owned widget must outlive its closing callback.
  widget_ = views::BubbleDialogDelegate::CreateBubble(
      panel_.get(),
      base::BindOnce([](base::WeakPtr<FingerprintToolbar> toolbar,
                        views::Widget::ClosedReason) {
        base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE, base::BindOnce(&FingerprintToolbar::DestroyPanel, toolbar));
      }, weak_factory_.GetWeakPtr()));
  UpdateButton(private_mode_);
  widget_->Show();
}

void FingerprintToolbar::Close() {
  if (widget_ && !widget_->IsClosed())
    widget_->Close();
}

void FingerprintToolbar::Refresh() {
  if (!widget_ || widget_->IsClosed())
    return;
  auto* button = button_.view();
  if (button && button->GetWidget() && panel_->GetAnchorView() != button)
    panel_->SetAnchorView(button);
  panel_->Refresh();
}

std::u16string FingerprintToolbar::site_code_for_testing() const {
  return panel_ ? panel_->code() : std::u16string();
}

views::View* FingerprintToolbar::rotate_button_for_testing() const {
  return panel_ ? panel_->rotate_button() : nullptr;
}

views::View* FingerprintToolbar::protection_button_for_testing() const {
  return panel_ ? panel_->protection_button() : nullptr;
}

void FingerprintToolbar::RunCommandForTesting(int command) {
  if (command == 0)
    Rotate();
  if (command == 1)
    ToggleProtection();
  if (command == 2)
    OpenSettings();
}

void FingerprintToolbar::Rotate() {
  if (!widget_ || widget_->IsClosed() || !callbacks_.enabled.Run())
    return;
  callbacks_.rotate.Run();
  Refresh();
  if (panel_)
    panel_->Tremor();
}

void FingerprintToolbar::ToggleProtection() {
  if (!widget_ || widget_->IsClosed())
    return;
  Close();
  callbacks_.toggle_protection.Run();
}

void FingerprintToolbar::OpenSettings() {
  if (!widget_ || widget_->IsClosed())
    return;
  Close();
  callbacks_.open_settings.Run();
}

void FingerprintToolbar::DestroyPanel() {
  if (panel_)
    panel_->DetachContents();
  widget_.reset();
  panel_.reset();
  if (auto* button = views::AsViewClass<FingerprintButton>(button_.view()))
    button->SetShake(0);
  UpdateButton(private_mode_);
}

}  // namespace zitter::prototype
