// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "chromium/prototype/user_agent_panel.h"

#include <algorithm>
#include <utility>

#include "base/i18n/case_conversion.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "chromium/prototype/i18n.h"
#include "chromium/prototype/toolbar_menu.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/text_utils.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"

namespace zitter::prototype {
namespace {

constexpr int kButton = 28;
constexpr int kInner = kMenuWidth - 8;
// An identity card: a frame with a face and two lines.
constexpr char kIdentityIcon[] =
    "M3.8 3.2h8.4a2 2 0 0 1 2 2v5.6a2 2 0 0 1-2 2H3.8a2 2 0 0 1-2-2V5.2a2 2 0 0 1 2-2z"
    "M3.8 10.6l1-1.2h2l1 1.2M9.6 6.6h2.6M9.6 9h1.8";
constexpr char kChevronIcon[] = "M4 6l4 4 4-4";
constexpr char kChevronUpIcon[] = "M4 10l4-4 4 4";
constexpr char kCopyIcon[] =
    "M7 5.5h5a1.5 1.5 0 0 1 1.5 1.5v5a1.5 1.5 0 0 1-1.5 1.5H7A1.5 1.5 0 0 1 5.5 12V7A1.5 1.5 0 0 1 7 5.5z"
    "M10.5 3.5V3A1.5 1.5 0 0 0 9 1.5H4A1.5 1.5 0 0 0 2.5 3v5A1.5 1.5 0 0 0 4 9.5h.5";
constexpr char kTurnIcon[] = "M13 8a5 5 0 1 1-1.5-3.6M13 2.5v3h-3";

std::u16string SourceTag(const UserAgentPanelState& state) {
  if (state.selected != "auto")
    return T(u"Twój wyjątek");
  return state.catalog ? T(u"Katalog Zittera") : T(u"Auto");
}

const UserAgentPanelState::Option* FindOption(const UserAgentPanelState& state,
                                              const std::string& id) {
  const auto it = std::ranges::find(state.options, id, &UserAgentPanelState::Option::id);
  return it == state.options.end() ? nullptr : &*it;
}

std::u16string PresetName(const UserAgentPanelState& state, const std::string& preset) {
  for (const auto& option : state.options) {
    if (option.id == preset)
      return option.name;
  }
  return base::UTF8ToUTF16(preset);
}

// A text button in the panel: quiet (text only) or primary (accent pill).
class TextButton final : public views::Button {
  METADATA_HEADER(TextButton, views::Button)
 public:
  TextButton(PressedCallback callback, const sidebar::DesignPalette& palette,
             std::u16string text, bool primary, const char* glyph = nullptr)
      : views::Button(std::move(callback)),
        palette_(palette),
        text_(std::move(text)),
        primary_(primary),
        glyph_(glyph) {
    const auto font = DesignFont(2, 12, primary ? 600 : 500);
    const int width = gfx::GetStringWidth(text_, font) + (glyph_ ? 20 : 0) +
                      (primary_ ? 28 : 20);
    SetPreferredSize(gfx::Size(width, primary_ ? 30 : 28));
    GetViewAccessibility().SetName(text_);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const gfx::RectF box(GetLocalBounds());
    SkColor ink = palette_.ink;
    if (primary_) {
      FillRound(canvas, box, box.height() / 2, palette_.accent);
      ink = palette_.on_accent;
    } else if (GetState() == STATE_HOVERED || GetState() == STATE_PRESSED || HasFocus()) {
      FillRound(canvas, box, 8, palette_.bg_hover);
    }
    int x = primary_ ? 14 : 10;
    if (glyph_) {
      StrokeIcon(canvas, glyph_, gfx::RectF(x, (height() - 14) / 2.f, 14, 14), ink);
      x += 20;
    }
    canvas->DrawStringRectWithFlags(text_, DesignFont(2, 12, primary_ ? 600 : 500), ink,
                                    gfx::Rect(x, 0, width() - x, height()),
                                    gfx::Canvas::NO_SUBPIXEL_RENDERING);
  }

  void OnFocus() override { views::Button::OnFocus(); SchedulePaint(); }
  void OnBlur() override { views::Button::OnBlur(); SchedulePaint(); }

 private:
  const sidebar::DesignPalette palette_;
  const std::u16string text_;
  const bool primary_;
  const char* glyph_;
};
BEGIN_METADATA(TextButton)
END_METADATA

// A small icon-only button (copy).
class GlyphButton final : public views::Button {
  METADATA_HEADER(GlyphButton, views::Button)
 public:
  GlyphButton(PressedCallback callback, const sidebar::DesignPalette& palette,
              const char* glyph, std::u16string name)
      : views::Button(std::move(callback)), palette_(palette), glyph_(glyph) {
    SetPreferredSize(gfx::Size(24, 24));
    GetViewAccessibility().SetName(name);
    SetTooltipText(name);
  }
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const gfx::RectF box(GetLocalBounds());
    SkColor ink = palette_.ink_muted;
    if (GetState() == STATE_HOVERED || GetState() == STATE_PRESSED || HasFocus()) {
      FillRound(canvas, box, 6, palette_.bg_hover);
      ink = palette_.ink;
    }
    StrokeIcon(canvas, glyph_, gfx::RectF(5, 5, 14, 14), ink);
  }
  void OnFocus() override { views::Button::OnFocus(); SchedulePaint(); }
  void OnBlur() override { views::Button::OnBlur(); SchedulePaint(); }

 private:
  const sidebar::DesignPalette palette_;
  const char* glyph_;
};
BEGIN_METADATA(GlyphButton)
END_METADATA

// A muted line of text in the footer.
class NoteLabel final : public views::View {
  METADATA_HEADER(NoteLabel, views::View)
 public:
  NoteLabel(const sidebar::DesignPalette& palette, std::u16string text)
      : palette_(palette), text_(std::move(text)) {
    SetPreferredSize(gfx::Size(0, 30));
    GetViewAccessibility().SetRole(ax::mojom::Role::kStaticText);
    GetViewAccessibility().SetName(text_);
  }
  void OnPaint(gfx::Canvas* canvas) override {
    canvas->DrawStringRectWithFlags(text_, DesignFont(2, 12, 400), palette_.ink_muted,
                                    gfx::Rect(12, 0, width() - 12, height()), 0);
  }

 private:
  const sidebar::DesignPalette palette_;
  const std::u16string text_;
};
BEGIN_METADATA(NoteLabel)
END_METADATA

// The panel's title: an accent tile, "User-Agent" and the site.
class PanelHeader final : public views::View {
  METADATA_HEADER(PanelHeader, views::View)
 public:
  PanelHeader(const sidebar::DesignPalette& palette, std::u16string host)
      : palette_(palette), host_(std::move(host)) {
    SetPreferredSize(gfx::Size(kInner, 50));
  }
  void OnPaint(gfx::Canvas* canvas) override {
    const gfx::RectF tile(8, 10, 32, 32);
    FillRound(canvas, tile, 8, palette_.accent_soft);
    StrokeIcon(canvas, kIdentityIcon, gfx::RectF(16, 18, 16, 16), palette_.accent_ink);
    const int x = 50;
    canvas->DrawStringRectWithFlags(u"User-Agent", DesignFont(2, 13, 600), palette_.ink,
                                    gfx::Rect(x, 9, width() - x - 8, 18), 0);
    canvas->DrawStringRectWithFlags(
        host_.empty() ? T(u"Ta strona nie wysyła User-Agenta") : host_,
        DesignFont(2, 12, host_.empty() ? 400 : 600),
        host_.empty() ? palette_.ink_muted : palette_.ink,
        gfx::Rect(x, 27, width() - x - 8, 16), 0);
  }

 private:
  const sidebar::DesignPalette palette_;
  const std::u16string host_;
};
BEGIN_METADATA(PanelHeader)
END_METADATA

// What the site sees: a caption with the source, the preset's name, and the
// full string behind "Szczegóły".
class IdentityCard final : public views::View {
  METADATA_HEADER(IdentityCard, views::View)
 public:
  IdentityCard(const sidebar::DesignPalette& palette,
               const UserAgentPanelState& state,
               bool expanded,
               base::RepeatingClosure toggle)
      : palette_(palette), expanded_(expanded) {
    const auto* option = FindOption(state, state.selected);
    name_ = option ? PresetName(state, option->preset) : u"—";
    ua_ = option ? base::UTF8ToUTF16(option->ua) : std::u16string();
    source_ = SourceTag(state);
    changed_ = option && !state.applied.empty() && option->preset != state.applied;
    caption_ = changed_ ? T(u"Po odświeżeniu") : T(u"Przedstawiasz się jako");
    GetViewAccessibility().SetRole(ax::mojom::Role::kStatus);
    GetViewAccessibility().SetName(caption_ + u": " + name_ + u", " + source_);

    details_ = AddChildView(std::make_unique<TextButton>(
        std::move(toggle), palette_, T(u"Szczegóły"), false,
        expanded_ ? kChevronUpIcon : kChevronIcon));
    details_->SetID(4);
    if (expanded_)
      details_->GetViewAccessibility().SetIsExpanded();
    else
      details_->GetViewAccessibility().SetIsCollapsed();
    details_->SetVisible(!ua_.empty());
    copy_ = AddChildView(std::make_unique<GlyphButton>(
        base::BindRepeating(
            [](std::u16string text) {
              ui::ScopedClipboardWriter(ui::ClipboardBuffer::kCopyPaste).WriteText(text);
            },
            ua_),
        palette_, kCopyIcon, T(u"Kopiuj User-Agent")));
    copy_->SetVisible(expanded_ && !ua_.empty());
    SetPreferredSize(gfx::Size(kInner, Height()));
  }

  std::u16string summary() const { return name_ + u" · " + source_; }

  void Layout(PassKey) override {
    const auto size = details_->GetPreferredSize({});
    details_->SetBounds(width() - 16 - size.width(), 30, size.width(), size.height());
    copy_->SetBounds(width() - 16 - 24, UaTop() - 2, 24, 24);
  }

  void OnPaint(gfx::Canvas* canvas) override {
    FillRound(canvas, gfx::RectF(8, 0, width() - 16, height()), 8, palette_.bg_base);
    const int x = 20, right = width() - 20;
    canvas->DrawStringRectWithFlags(base::i18n::ToUpper(caption_), DesignFont(2, 10, 600), palette_.ink_muted,
                                    gfx::Rect(x, 10, right - x, 14), 0);
    // The source as a pill at the right of the caption.
    const auto tag_font = DesignFont(2, 11, 500);
    const int tag_width = gfx::GetStringWidth(source_, tag_font) + 12;
    const gfx::RectF tag(right - tag_width, 8, tag_width, 18);
    FillRound(canvas, tag, 9, palette_.accent_soft);
    canvas->DrawStringRectWithFlags(source_, tag_font, palette_.accent_ink,
                                    gfx::ToEnclosingRect(tag),
                                    gfx::Canvas::TEXT_ALIGN_CENTER);
    canvas->DrawStringRectWithFlags(name_, DesignFont(2, 15, 600), palette_.ink,
                                    gfx::Rect(x, 32, right - x - 90, 22),
                                    gfx::Canvas::NO_SUBPIXEL_RENDERING);
    int y = 58;
    if (changed_) {
      StrokeIcon(canvas, kTurnIcon, gfx::RectF(x, y + 1, 14, 14), palette_.warn);
      canvas->DrawStringRectWithFlags(T(u"Odśwież, by zastosować"), DesignFont(2, 12, 400),
                                      palette_.warn, gfx::Rect(x + 20, y, right - x - 20, 16), 0);
      y += 22;
    }
    if (expanded_ && !ua_.empty()) {
      canvas->DrawStringRectWithFlags(ua_, UaFont(), palette_.ink_muted,
                                      gfx::Rect(x, UaTop(), UaWidth(), UaHeight()),
                                      gfx::Canvas::MULTI_LINE | gfx::Canvas::CHARACTER_BREAKABLE);
    }
  }

 private:
  static gfx::FontList UaFont() { return DesignFont(3, 11, 400); }
  int UaWidth() const { return kInner - 40 - 30; }
  int UaTop() const { return changed_ ? 80 : 58; }
  int UaHeight() const {
    int w = UaWidth(), h = 0;
    gfx::Canvas::SizeStringInt(ua_, UaFont(), &w, &h, 0,
                               gfx::Canvas::MULTI_LINE | gfx::Canvas::CHARACTER_BREAKABLE);
    return h;
  }
  int Height() const {
    int h = UaTop();
    if (expanded_ && !ua_.empty())
      h += std::max(UaHeight(), 22) + 6;
    return h + 6;
  }

  const sidebar::DesignPalette palette_;
  const bool expanded_;
  bool changed_ = false;
  std::u16string caption_, name_, ua_, source_;
  raw_ptr<views::View> details_ = nullptr;
  raw_ptr<views::View> copy_ = nullptr;
};
BEGIN_METADATA(IdentityCard)
END_METADATA

// One choice for the site: a radio, the name, and what it sends at the right.
class OptionRow final : public views::Button {
  METADATA_HEADER(OptionRow, views::Button)
 public:
  OptionRow(PressedCallback callback, const sidebar::DesignPalette& palette,
            const UserAgentPanelState::Option& option, bool selected)
      : views::Button(std::move(callback)),
        palette_(palette),
        name_(option.name),
        detail_(option.detail),
        selected_(selected) {
    SetPreferredSize(gfx::Size(kInner, 34));
    GetViewAccessibility().SetRole(ax::mojom::Role::kRadioButton);
    GetViewAccessibility().SetName(name_ + u", " + detail_);
    GetViewAccessibility().SetCheckedState(selected_ ? ax::mojom::CheckedState::kTrue
                                                     : ax::mojom::CheckedState::kFalse);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const gfx::RectF box(GetLocalBounds());
    if (selected_)
      FillRound(canvas, box, 8, palette_.accent_soft);
    else if (GetState() == STATE_HOVERED || GetState() == STATE_PRESSED || HasFocus())
      FillRound(canvas, box, 8, palette_.bg_hover);
    const gfx::PointF dot(17, height() / 2.f);
    if (selected_) {
      FillCircle(canvas, dot, 7, palette_.accent);
      FillCircle(canvas, dot, 3, palette_.on_accent);
    } else {
      FillCircle(canvas, dot, 6.2f, palette_.line_strong, /*stroke=*/true);
    }
    const auto label = DesignFont(2, 12, 400);
    const int detail_width = gfx::GetStringWidth(detail_, label);
    const int right = width() - 10;
    canvas->DrawStringRectWithFlags(detail_, label, palette_.ink_muted,
                                    gfx::Rect(right - detail_width, 0, detail_width, height()), 0);
    canvas->DrawStringRectWithFlags(name_, DesignFont(2, 13, selected_ ? 600 : 400), palette_.ink,
                                    gfx::Rect(32, 0, right - detail_width - 40, height()),
                                    gfx::Canvas::NO_SUBPIXEL_RENDERING);
  }

  void OnFocus() override { views::Button::OnFocus(); SchedulePaint(); }
  void OnBlur() override { views::Button::OnBlur(); SchedulePaint(); }

 private:
  const sidebar::DesignPalette palette_;
  const std::u16string name_, detail_;
  const bool selected_;
};
BEGIN_METADATA(OptionRow)
END_METADATA

// The pinned button: the identity card glyph, with a dot while the site has
// an exception.
class UserAgentButton final : public views::Button {
  METADATA_HEADER(UserAgentButton, views::Button)
 public:
  UserAgentButton(PressedCallback callback,
                  base::RepeatingCallback<sidebar::DesignPalette()> palette)
      : views::Button(std::move(callback)), palette_(std::move(palette)) {
    SetPreferredSize(gfx::Size(kButton, kButton));
    GetViewAccessibility().SetName(u"User-Agent");
    SetTooltipText(u"User-Agent");
    GetViewAccessibility().SetHasPopup(ax::mojom::HasPopup::kDialog);
  }
  void Set(bool open, bool exception) {
    if (open_ == open && exception_ == exception)
      return;
    open_ = open;
    exception_ = exception;
    SetTooltipText(exception_ ? T(u"User-Agent: własny wyjątek dla tej witryny")
                              : u"User-Agent");
    SchedulePaint();
  }
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const auto p = palette_.Run();
    const gfx::RectF box(GetLocalBounds());
    SkColor ink = p.ink_muted;
    if (open_) {
      FillRound(canvas, box, 8, p.accent_soft);
      ink = p.accent_ink;
    } else if (GetState() == STATE_HOVERED || GetState() == STATE_PRESSED) {
      FillRound(canvas, box, 8, p.bg_hover);
      ink = p.ink;
    }
    StrokeIcon(canvas, kIdentityIcon,
               gfx::RectF(box.CenterPoint().x() - 8, box.CenterPoint().y() - 8, 16, 16), ink);
    if (exception_) {
      const gfx::PointF dot(box.right() - 6.5f, box.bottom() - 6.5f);
      FillCircle(canvas, dot, 5, p.bg_void);
      FillCircle(canvas, dot, 3.5f, p.accent);
    }
  }

 private:
  base::RepeatingCallback<sidebar::DesignPalette()> palette_;
  bool open_ = false;
  bool exception_ = false;
};
BEGIN_METADATA(UserAgentButton)
END_METADATA

}  // namespace

UserAgentPanelState::UserAgentPanelState() = default;
UserAgentPanelState::UserAgentPanelState(const UserAgentPanelState&) = default;
UserAgentPanelState& UserAgentPanelState::operator=(const UserAgentPanelState&) = default;
UserAgentPanelState::~UserAgentPanelState() = default;

class UserAgentToolbar::Panel final : public views::BubbleDialogDelegate {
 public:
  Panel(UserAgentToolbar& toolbar, views::View* anchor)
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
    SetTitle(u"User-Agent");
    SetShowTitle(false);
    auto body = std::make_unique<views::View>();
    body_ = body.get();
    body->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical));
    SetContentsView(std::move(body));
    Build();
  }

  void Build() {
    body_->RemoveAllChildViews();
    card_ = nullptr;
    const auto state = toolbar_->callbacks_.state.Run();
    auto weak = toolbar_->weak_factory_.GetWeakPtr();
    body_->AddChildView(std::make_unique<PanelHeader>(palette_, state.host));
    if (!state.host.empty()) {
      card_ = body_->AddChildView(std::make_unique<IdentityCard>(
          palette_, state, toolbar_->details_,
          base::BindRepeating(&UserAgentToolbar::ToggleDetails, weak)));
      body_->AddChildView(std::make_unique<MenuHeader>(palette_, T(u"Dla tej witryny")));
      int id = kOptionIdBase;
      for (const auto& option : state.options) {
        auto* row = body_->AddChildView(std::make_unique<OptionRow>(
            base::BindRepeating(&UserAgentToolbar::Choose, weak, option.id), palette_,
            option, option.id == state.selected));
        row->SetID(id++);
      }
    }
    body_->AddChildView(std::make_unique<MenuSeparator>(palette_.line));
    const auto* option = FindOption(state, state.selected);
    const bool changed = option && !state.applied.empty() && option->preset != state.applied;
    if (changed) {
      auto* bar = body_->AddChildView(std::make_unique<views::View>());
      auto* layout = bar->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets::TLBR(2, 0, 2, 4), 4));
      layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
      auto* note = bar->AddChildView(std::make_unique<NoteLabel>(
          palette_, TF(u"Teraz: {}", {PresetName(state, state.applied)})));
      layout->SetFlexForView(note, 1);
      bar->AddChildView(std::make_unique<TextButton>(
          base::BindRepeating(&UserAgentToolbar::Undo, weak), palette_, T(u"Cofnij"), false))
          ->SetID(3);
      bar->AddChildView(std::make_unique<TextButton>(
          base::BindRepeating(&UserAgentToolbar::Reload, weak), palette_, T(u"Odśwież"), true,
          kTurnIcon))
          ->SetID(1);
    }
    auto* settings = body_->AddChildView(std::make_unique<MenuRow>(
        base::BindRepeating(&UserAgentToolbar::OpenSettings, weak), palette_, 44));
    settings->SetGlyph(kGearIcon);
    settings->SetRing(true);
    settings->Init();
    settings->SetText(T(u"Ustawienia User-Agent"),
                      TF(u"Wszędzie indziej: {}", {state.mode}), false, 16);
    settings->SetID(2);
    if (GetWidget())
      SizeToContents();
  }

  std::u16string identity() const { return card_ ? card_->summary() : std::u16string(); }
  views::View* body() const { return body_; }
  void DetachContents() {
    body_ = nullptr;
    card_ = nullptr;
  }

 private:
  const raw_ref<UserAgentToolbar> toolbar_;
  const sidebar::DesignPalette palette_;
  raw_ptr<views::View> body_ = nullptr;
  raw_ptr<IdentityCard> card_ = nullptr;
};

UserAgentToolbar::Callbacks::Callbacks() = default;
UserAgentToolbar::Callbacks::Callbacks(Callbacks&&) = default;
UserAgentToolbar::Callbacks::~Callbacks() = default;

UserAgentToolbar::UserAgentToolbar(Callbacks callbacks)
    : callbacks_(std::move(callbacks)) {}

UserAgentToolbar::~UserAgentToolbar() {
  weak_factory_.InvalidateWeakPtrs();
  DestroyPanel();
}

std::unique_ptr<views::View> UserAgentToolbar::CreateButton() {
  auto button = std::make_unique<UserAgentButton>(
      base::BindRepeating(&UserAgentToolbar::Toggle, weak_factory_.GetWeakPtr()),
      callbacks_.palette);
  button_.SetView(button.get());
  UpdateButton(exception_);
  return button;
}

void UserAgentToolbar::UpdateButton(bool exception) {
  exception_ = exception;
  if (auto* button = views::AsViewClass<UserAgentButton>(button_.view()))
    button->Set(widget_ && !widget_->IsClosed() && panel_ &&
                    panel_->GetAnchorView() == button,
                exception);
}

void UserAgentToolbar::Toggle() {
  if (widget_) {
    Close();
    return;
  }
  Open(button_.view());
}

void UserAgentToolbar::Open(views::View* anchor) {
  if (widget_)
    Close();
  if (!anchor || !anchor->IsDrawn() || !anchor->GetWidget())
    return;
  callbacks_.before_open.Run();
  details_ = false;
  opened_selection_ = callbacks_.state.Run().selected;
  panel_ = std::make_unique<Panel>(*this, anchor);
  widget_ = views::BubbleDialogDelegate::CreateBubble(
      panel_.get(),
      base::BindOnce([](base::WeakPtr<UserAgentToolbar> toolbar,
                        views::Widget::ClosedReason) {
        base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE, base::BindOnce(&UserAgentToolbar::DestroyPanel, toolbar));
      }, weak_factory_.GetWeakPtr()));
  UpdateButton(exception_);
  widget_->Show();
}

void UserAgentToolbar::Close() {
  if (!widget_ || widget_->IsClosed())
    return;
  widget_->Hide();
  if (!widget_->IsClosed())
    widget_->Close();
}

void UserAgentToolbar::Refresh() {
  // A row's press handler may be running: rebuild after it returns.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<UserAgentToolbar> toolbar) {
                       if (toolbar && toolbar->widget_ &&
                           !toolbar->widget_->IsClosed() && toolbar->panel_) {
                         toolbar->panel_->Build();
                       }
                     },
                     weak_factory_.GetWeakPtr()));
}

views::View* UserAgentToolbar::view_for_testing(int id) const {
  return panel_ && panel_->body() ? panel_->body()->GetViewByID(id) : nullptr;
}

std::u16string UserAgentToolbar::identity_for_testing() const {
  return panel_ ? panel_->identity() : std::u16string();
}

void UserAgentToolbar::Choose(std::string id) {
  if (!widget_ || widget_->IsClosed())
    return;
  callbacks_.choose.Run(id);
  Refresh();
}

void UserAgentToolbar::Undo() {
  Choose(opened_selection_);
}

void UserAgentToolbar::Reload() {
  if (!widget_ || widget_->IsClosed())
    return;
  Close();
  callbacks_.reload.Run();
}

void UserAgentToolbar::OpenSettings() {
  if (!widget_ || widget_->IsClosed())
    return;
  Close();
  callbacks_.open_settings.Run();
}

void UserAgentToolbar::ToggleDetails() {
  details_ = !details_;
  Refresh();
}

void UserAgentToolbar::DestroyPanel() {
  if (panel_)
    panel_->DetachContents();
  widget_.reset();
  panel_.reset();
  UpdateButton(exception_);
}

}  // namespace zitter::prototype
