// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/prototype/extensions_bar.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <utility>

#include "base/functional/callback_helpers.h"
#include "base/i18n/case_conversion.h"
#include "base/memory/scoped_refptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/timer/timer.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/extensions/extension_action_runner.h"
#include "chrome/browser/extensions/extension_tab_util.h"
#include "chrome/browser/extensions/extension_uninstall_dialog.h"
#include "chrome/browser/extensions/extension_view_host.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/views/extensions/extension_view_views.h"
#include "chromium/prototype/content_blocking.h"
#include "chromium/prototype/fingerprint_protection.h"
#include "chromium/prototype/i18n.h"
#include "chromium/prototype/sidebar_scene.rs.h"
#include "components/input/native_web_keyboard_event.h"
#include "components/prefs/pref_service.h"
#include "components/sessions/content/session_tab_helper.h"
#include "content/public/browser/eye_dropper.h"
#include "content/public/browser/keyboard_event_processing_result.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/reload_type.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "extensions/browser/extension_action.h"
#include "extensions/browser/extension_action_icon_factory.h"
#include "extensions/browser/extension_action_manager.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/permissions/site_permissions_helper.h"
#include "extensions/browser/permissions_manager.h"
#include "extensions/browser/uninstall_reason.h"
#include "extensions/common/extension.h"
#include "extensions/common/manifest_handlers/options_page_info.h"
#include "extensions/common/mojom/view_type.mojom.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider_key.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/gfx/image/image_skia_rep.h"
#include "ui/gfx/text_utils.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/bubble/bubble_frame_view.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/widget/widget_observer.h"
#include "ui/views/window/dialog_client_view.h"

namespace zitter::prototype {
namespace {

using extensions::Extension;
using extensions::ExtensionAction;
using SiteInteraction = extensions::SitePermissionsHelper::SiteInteraction;

// Design: Extensions. IconButton 28 px, 2 px apart; a 16 px `line` stroke
// between the pinned actions and the puzzle; a 296 px menu with 44 px rows.
constexpr int kButton = 28;
constexpr int kIcon = 16;
constexpr int kMenuWidth = 296;
constexpr int kRowHeight = 44;
constexpr int kFooterHeight = 30;
constexpr int kRowAction = 24;
constexpr gfx::Size kPopupMin(25, 25);
constexpr gfx::Size kPopupMax(800, 600);

// Design-system line icons on the 16 px grid, stroke 1.6.
constexpr char kPuzzleIcon[] = "M3 5.5h3V4a1.5 1.5 0 0 1 3 0v1.5h3v3h-1.5a1.5 1.5 0 0 0 0 3H12v2H3v-2h1.5a1.5 1.5 0 0 0 0-3H3z";
constexpr char kPinIcon[] = "M6 2.5h4l-.5 4 2 2h-7l2-2zM8 8.5V14";
constexpr char kWarnIcon[] = "M8 2.5 14 13H2zM8 6.5v3M8 11.2v.1";
constexpr char kGearIcon[] = "M8 2v1.5M8 12.5V14M2 8h1.5M12.5 8H14M3.8 3.8l1 1M11.2 11.2l1 1M3.8 12.2l1-1M11.2 4.8l1-1";
constexpr char kStoreIcon[] = "M3 6h10l-1 7.5H4zM5.5 6V5a2.5 2.5 0 0 1 5 0v1";
// Fingerprint protection: ridges, a turn arrow, a crossed-out shield.
constexpr char kFingerprintIcon[] = "M4.2 4.2a5.5 5.5 0 0 1 7.6 0M3 11.5V9.5a5 5 0 0 1 10 0v1M5.5 13.5V9.5a2.5 2.5 0 0 1 5 0V12M8 9v5";
constexpr char kTurnIcon[] = "M13 8a5 5 0 1 1-1.5-3.6M13 2.5v3h-3";
constexpr char kShieldOffIcon[] = "M8 2.5 13 4.5V8a5 6 0 0 1-5 5.5A5 6 0 0 1 3 8V4.5zM2.5 2.5l11 11";
// Ad blocking: a shield with a bar across, the assistant's spark.
constexpr char kShieldIcon[] = "M8 2.5 13 4.5V8a5 6 0 0 1-5 5.5A5 6 0 0 1 3 8V4.5zM5.5 7.5h5";
constexpr char kSparkIcon[] = "M8 2.5l1.3 3.7 3.7 1.3-3.7 1.3L8 12.5l-1.3-3.7L3 7.5l3.7-1.3z";

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

void StrokeIcon(gfx::Canvas* canvas, std::string_view d, gfx::RectF box, SkColor color) {
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

void FillCircle(gfx::Canvas* canvas, gfx::PointF center, float radius, SkColor color, bool stroke = false) {
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

bool IsDark(const views::View* view) {
  return view && view->GetWidget() &&
         view->GetWidget()->GetColorMode() == ui::ColorProviderKey::ColorMode::kDark;
}

// A design system family (1 display, 2 sans, 3 mono; design.rs family_id).
gfx::FontList DesignFont(uint32_t family, int size, int weight) {
  return gfx::FontList({std::string(sidebar::design_font_family(family))}, gfx::Font::NORMAL, size,
                       static_cast<gfx::Font::Weight>(weight));
}

// The icon an action shows for a tab, in full color or in the grey of an
// action the extension disabled on this page (design: Extensions).
gfx::ImageSkia ActionIcon(extensions::ExtensionActionIconFactory& factory, int tab_id) {
  return factory.GetIcon(tab_id).AsImageSkia();
}
gfx::ImageSkia Grey(const gfx::ImageSkia& image) {
  return gfx::ImageSkiaOperations::CreateTransparentImage(
      gfx::ImageSkiaOperations::CreateHSLShiftedImage(image, color_utils::HSL{-1, 0, -1}), 0.55);
}
uint32_t PixelHash(const gfx::ImageSkia& image) {
  if (image.isNull()) return 0;
  const auto& bitmap = image.GetRepresentation(1.0f).GetBitmap();
  uint32_t hash = 2166136261u;
  for (int y = 0; y < bitmap.height(); ++y)
    for (int x = 0; x < bitmap.width(); ++x) {
      hash ^= bitmap.getColor(x, y);
      hash *= 16777619u;
    }
  return hash;
}

std::u16string SiteAccessText(SiteInteraction interaction, const GURL& url) {
  switch (interaction) {
    case SiteInteraction::kGranted: return T(u"Ma dostęp do tej witryny");
    case SiteInteraction::kActiveTab: return T(u"Po kliknięciu");
    case SiteInteraction::kWithheld:
      return TF(u"Chce dostępu do {}", {base::UTF8ToUTF16(url.host())});
    case SiteInteraction::kNone: return T(u"Brak dostępu do tej witryny");
  }
  return {};
}

// A bubble's design palette: Noc or Papier from the bar's window, tinted by
// the active space.
sidebar::DesignPalette PaletteOf(bool dark, SkColor workspace) { return sidebar::design_palette(dark, workspace); }

// A small icon button inside a menu row: pin, more.
class RowAction final : public views::Button {
  METADATA_HEADER(RowAction, views::Button)
 public:
  RowAction(PressedCallback callback, std::u16string name, const char* icon, const sidebar::DesignPalette& palette)
      : views::Button(std::move(callback)), icon_(icon), palette_(palette) {
    GetViewAccessibility().SetName(name);
    SetTooltipText(name);
    SetPreferredSize(gfx::Size(kRowAction, kRowAction));
  }
  void SetOn(bool on) {
    on_ = on;
    GetViewAccessibility().SetRole(ax::mojom::Role::kToggleButton);
    GetViewAccessibility().SetCheckedState(on ? ax::mojom::CheckedState::kTrue : ax::mojom::CheckedState::kFalse);
    SchedulePaint();
  }
  bool on() const { return on_; }
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const gfx::RectF box(GetLocalBounds());
    SkColor ink = palette_.ink_muted;
    if (on_) { FillRound(canvas, box, 8, palette_.accent_soft); ink = palette_.accent_ink; }
    else if (GetState() == STATE_HOVERED || GetState() == STATE_PRESSED) { FillRound(canvas, box, 8, palette_.bg_hover); ink = palette_.ink; }
    const gfx::RectF glyph(box.CenterPoint().x() - 8, box.CenterPoint().y() - 8, 16, 16);
    if (icon_) StrokeIcon(canvas, icon_, glyph, ink);
    else for (float x : {4.f, 8.f, 12.f}) FillCircle(canvas, {glyph.x() + x, glyph.y() + 8}, 1.2f, ink);  // More: ⋯
  }
 private:
  const char* icon_;
  sidebar::DesignPalette palette_;
  bool on_ = false;
};
BEGIN_METADATA(RowAction)
END_METADATA

// "Zezwól": a quiet text button.
class QuietButton final : public views::Button {
  METADATA_HEADER(QuietButton, views::Button)
 public:
  QuietButton(PressedCallback callback, std::u16string text, const sidebar::DesignPalette& palette)
      : views::Button(std::move(callback)), text_(std::move(text)), palette_(palette) {
    GetViewAccessibility().SetName(text_);
    SetPreferredSize(gfx::Size(gfx::GetStringWidth(text_, DesignFont(2, 13, 600)) + 16, kRowAction));
  }
  void PaintButtonContents(gfx::Canvas* canvas) override {
    if (GetState() == STATE_HOVERED || GetState() == STATE_PRESSED) FillRound(canvas, gfx::RectF(GetLocalBounds()), 8, palette_.bg_hover);
    canvas->DrawStringRectWithFlags(text_, DesignFont(2, 13, 600), palette_.ink, GetLocalBounds(), gfx::Canvas::TEXT_ALIGN_CENTER);
  }
 private:
  std::u16string text_;
  sidebar::DesignPalette palette_;
};
BEGIN_METADATA(QuietButton)
END_METADATA

}  // namespace

// The right-click menu of an action (ContextMenu): pin, options, manage, and
// the uninstall at the end in `danger`.
class ExtensionsContextMenu final : public ui::SimpleMenuModel::Delegate {
 public:
  enum Command { kPin = 1, kOptions, kManage, kUninstall };
  ExtensionsContextMenu(ExtensionsBar* bar, std::string id, bool pinned, bool options)
      : bar_(bar), id_(std::move(id)), model_(this) {
    model_.AddItem(kPin, pinned ? T(u"Odepnij") : T(u"Przypnij"));
    if (options) model_.AddItem(kOptions, T(u"Opcje"));
    model_.AddItem(kManage, T(u"Zarządzaj rozszerzeniami"));
    model_.AddSeparator(ui::NORMAL_SEPARATOR);
    model_.AddItem(kUninstall, T(u"Usuń z Zittera…"));
    pinned_ = pinned;
  }
  void Run(views::Widget* parent, const gfx::Rect& anchor, ui::mojom::MenuSourceType source_type) {
    runner_ = std::make_unique<views::MenuRunner>(&model_, views::MenuRunner::CONTEXT_MENU);
    runner_->RunMenuAt(parent, nullptr, anchor, views::MenuAnchorPosition::kTopRight, source_type);
  }
  void ExecuteCommand(int command, int) override {
    // The bar may rebuild (and drop this menu) while a command runs.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce(
        [](base::WeakPtr<ExtensionsBar> bar, std::string id, int command, bool pinned) {
          if (!bar) return;
          switch (command) {
            case kPin: bar->SetPinned(id, !pinned); break;
            case kOptions: bar->OpenOptions(id); break;
            case kManage: bar->OpenPage(GURL("chrome://extensions/?id=" + id)); break;
            case kUninstall: bar->Uninstall(id); break;
          }
        }, bar_->weak_factory_.GetWeakPtr(), id_, command, pinned_));
  }
 private:
  raw_ptr<ExtensionsBar> bar_;
  std::string id_;
  bool pinned_ = false;
  ui::SimpleMenuModel model_;
  std::unique_ptr<views::MenuRunner> runner_;
};

// Chromium's own uninstall confirmation, modal to the window.
class ExtensionsUninstall final : public extensions::ExtensionUninstallDialog::Delegate {
 public:
  ExtensionsUninstall(Profile* profile, gfx::NativeWindow parent, scoped_refptr<const Extension> extension)
      : dialog_(extensions::ExtensionUninstallDialog::Create(profile, parent, this)) {
    dialog_->ConfirmUninstall(extension, extensions::UNINSTALL_REASON_USER_INITIATED,
                              extensions::UNINSTALL_SOURCE_TOOLBAR_CONTEXT_MENU);
  }
 private:
  std::unique_ptr<extensions::ExtensionUninstallDialog> dialog_;
};

// One action in the header (or a 20 px icon in a menu row): the extension's
// icon for the shown tab, greyed while the extension disables its action on
// this page, with its badge (design: Extensions).
class ExtensionsBar::ActionButton final : public views::Button,
                                          public views::ContextMenuController,
                                          public extensions::ExtensionActionIconFactory::Observer {
  METADATA_HEADER(ActionButton, views::Button)
 public:
  ActionButton(ExtensionsBar* bar, const Extension& extension, ExtensionAction& action, int icon_size, bool chrome)
      : views::Button(base::BindRepeating(&ActionButton::Pressed, base::Unretained(this))),
        bar_(bar), id_(extension.id()), name_(base::UTF8ToUTF16(extension.name())),
        factory_(&extension, &action, this), action_(&action), icon_size_(icon_size), chrome_(chrome) {
    set_context_menu_controller(this);
    if (chrome_) SetPreferredSize(gfx::Size(kButton, kButton));
    else SetPreferredSize(gfx::Size(20, 20));
    // In a menu row the row itself is the button; the icon only draws.
    if (!chrome_) SetCanProcessEventsWithinSubtree(false);
    Refresh();
  }
  const std::string& id() const { return id_; }
  const std::u16string& name() const { return name_; }
  bool enabled_on_page() const { return enabled_; }
  const std::string& badge() const { return badge_; }
  uint32_t icon_hash() const { return PixelHash(enabled_ ? icon_ : Grey(icon_)); }
  void SetPopupOpen(bool open) { if (popup_open_ != open) { popup_open_ = open; SchedulePaint(); } }
  bool popup_open() const { return popup_open_; }

  // Reads the action's state for the shown tab: icon, title, badge, enabled.
  void Refresh() {
    const int tab = bar_->tab_id();
    icon_ = ActionIcon(factory_, tab);
    badge_ = action_->GetDisplayBadgeText(tab);
    if (badge_.size() > 4) badge_.resize(4);
    bool enabled = action_->GetIsVisible(tab);
    if (const auto* extension = bar_->ExtensionFor(id_); extension && bar_->page() &&
        extensions::SitePermissionsHelper(bar_->profile()).GetSiteInteraction(*extension, bar_->page()) ==
            SiteInteraction::kWithheld)
      enabled = true;  // Clicking grants the access it asks for.
    enabled_ = enabled;
    auto title = base::UTF8ToUTF16(action_->GetTitle(tab));
    if (title.empty()) title = name_;
    SetTooltipText(title);
    GetViewAccessibility().SetName(badge_.empty() ? title : title + u": " + base::UTF8ToUTF16(badge_));
    SchedulePaint();
  }

  void OnIconUpdated() override { Refresh(); }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const bool hovered = GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
    if (chrome_) {
      const auto p = bar_->palette();
      const gfx::RectF box(GetLocalBounds());
      if (popup_open_) FillRound(canvas, box, 8, p.accent_soft);
      else if (hovered) FillRound(canvas, box, 8, p.bg_hover);
    }
    // Full color on hover even when disabled here, so the user sees what it is.
    const auto image = enabled_ || (chrome_ && hovered) ? icon_ : Grey(icon_);
    const gfx::Rect target(GetLocalBounds().CenterPoint().x() - icon_size_ / 2,
                           GetLocalBounds().CenterPoint().y() - icon_size_ / 2, icon_size_, icon_size_);
    if (!image.isNull())
      canvas->DrawImageInt(image, 0, 0, image.width(), image.height(), target.x(), target.y(),
                           target.width(), target.height(), true);
    if (chrome_ && !badge_.empty()) PaintBadge(canvas);
  }

  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(views::View* source, const gfx::Point&,
                                  ui::mojom::MenuSourceType source_type) override {
    bar_->ShowActionMenu(id_, source, source_type);
  }

 private:
  void Pressed() { bar_->RunAction(id_); }
  // A 12 px pill in the lower right corner: `mono` 9 px 600, `ink` with
  // `bg-raised` text and a 1.5 px `bg-void` ring. Always neutral.
  void PaintBadge(gfx::Canvas* canvas) {
    const auto p = bar_->palette();
    const auto text = base::UTF8ToUTF16(badge_);
    const auto font = DesignFont(3, 9, 600);
    const float width = std::max(14, gfx::GetStringWidth(text, font) + 6);
    const gfx::RectF pill(this->width() - width, height() - 1 - 12, width, 12);
    auto ring = pill;
    ring.Outset(1.5f);
    FillRound(canvas, ring, 7.5f, p.bg_void);
    FillRound(canvas, pill, 6, p.ink);
    canvas->DrawStringRectWithFlags(text, font, p.bg_raised, gfx::ToEnclosingRect(pill), gfx::Canvas::TEXT_ALIGN_CENTER);
  }

  raw_ptr<ExtensionsBar> bar_;
  std::string id_;
  std::u16string name_;
  extensions::ExtensionActionIconFactory factory_;
  raw_ptr<ExtensionAction> action_;
  int icon_size_;
  bool chrome_;
  gfx::ImageSkia icon_;
  std::string badge_;
  bool enabled_ = true;
  bool popup_open_ = false;
};
BEGIN_METADATA(ExtensionsBar, ActionButton)
END_METADATA

namespace {
// The puzzle at the end of the bar; it opens the extensions menu.
class PuzzleButton final : public views::Button {
  METADATA_HEADER(PuzzleButton, views::Button)
 public:
  PuzzleButton(PressedCallback callback, base::RepeatingCallback<sidebar::DesignPalette()> palette)
      : views::Button(std::move(callback)), palette_(std::move(palette)) {
    SetPreferredSize(gfx::Size(kButton, kButton));
    GetViewAccessibility().SetName(T(u"Rozszerzenia"));
    SetTooltipText(T(u"Rozszerzenia"));
    GetViewAccessibility().SetHasPopup(ax::mojom::HasPopup::kMenu);
  }
  void SetOpen(bool open) { if (open_ != open) { open_ = open; SchedulePaint(); } }
  void SetShake(int dx) { if (shake_ != dx) { shake_ = dx; SchedulePaint(); } }
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const auto p = palette_.Run();
    const gfx::RectF box(GetLocalBounds());
    SkColor ink = p.ink_muted;
    if (open_) { FillRound(canvas, box, 8, p.accent_soft); ink = p.accent_ink; }
    else if (GetState() == STATE_HOVERED || GetState() == STATE_PRESSED) { FillRound(canvas, box, 8, p.bg_hover); ink = p.ink; }
    StrokeIcon(canvas, kPuzzleIcon, gfx::RectF(box.CenterPoint().x() - 8 + shake_, box.CenterPoint().y() - 8, 16, 16), ink);
  }
 private:
  base::RepeatingCallback<sidebar::DesignPalette()> palette_;
  bool open_ = false;
  int shake_ = 0;
};
BEGIN_METADATA(PuzzleButton)
END_METADATA

// The 1 px × 16 px `line` stroke between pinned actions and the puzzle.
class Divider final : public views::View {
  METADATA_HEADER(Divider, views::View)
 public:
  explicit Divider(base::RepeatingCallback<sidebar::DesignPalette()> palette) : palette_(std::move(palette)) {
    SetPreferredSize(gfx::Size(9, kButton));
  }
  void OnPaint(gfx::Canvas* canvas) override {
    canvas->FillRect(gfx::Rect(width() / 2, height() / 2 - 8, 1, 16), palette_.Run().line);
  }
 private:
  base::RepeatingCallback<sidebar::DesignPalette()> palette_;
};
BEGIN_METADATA(Divider)
END_METADATA

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

// Ad blocking in the bar, first: the fingerprint button's IconButton with the
// page's blocked count in a small pill, crossed out where the site is allowed.
class ShieldButton final : public views::Button {
  METADATA_HEADER(ShieldButton, views::Button)
 public:
  ShieldButton(PressedCallback callback, base::RepeatingCallback<sidebar::DesignPalette()> palette)
      : views::Button(std::move(callback)), palette_(std::move(palette)) {
    SetPreferredSize(gfx::Size(kButton, kButton));
    GetViewAccessibility().SetHasPopup(ax::mojom::HasPopup::kDialog);
    SetCount(0);
  }
  void SetOpen(bool open) { if (open_ != open) { open_ = open; SchedulePaint(); } }
  // -1: off on this site.
  void SetCount(int count) {
    count_ = count;
    const auto name = count < 0 ? T(u"Blokowanie reklam wyłączone na tej stronie")
                                : TF(u"Blokowanie reklam: {} zablokowanych na tej stronie", {base::NumberToString16(count)});
    GetViewAccessibility().SetName(name);
    SetTooltipText(name);
    SchedulePaint();
  }
  int count() const { return count_; }
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const auto p = palette_.Run();
    const gfx::RectF box(GetLocalBounds());
    SkColor ink = p.ink_muted;
    if (open_) { FillRound(canvas, box, 8, p.accent_soft); ink = p.accent_ink; }
    else if (GetState() == STATE_HOVERED || GetState() == STATE_PRESSED) { FillRound(canvas, box, 8, p.bg_hover); ink = p.ink; }
    StrokeIcon(canvas, count_ < 0 ? kShieldOffIcon : kShieldIcon, gfx::RectF(box.CenterPoint().x() - 8, box.CenterPoint().y() - 8, 16, 16), ink);
    if (count_ <= 0) return;
    // The extension badge (design: Extensions): neutral, so it never competes
    // with the space accent.
    const auto text = count_ > 99 ? std::u16string(u"99+") : base::NumberToString16(count_);
    const auto font = DesignFont(3, 9, 600);
    const float width = std::max(14, gfx::GetStringWidth(text, font) + 6);
    const gfx::RectF pill(this->width() - width, height() - 1 - 12, width, 12);
    auto ring = pill;
    ring.Outset(1.5f);
    FillRound(canvas, ring, 7.5f, p.bg_void);
    FillRound(canvas, pill, 6, p.ink);
    canvas->DrawStringRectWithFlags(text, font, p.bg_raised, gfx::ToEnclosingRect(pill), gfx::Canvas::TEXT_ALIGN_CENTER);
  }
 private:
  base::RepeatingCallback<sidebar::DesignPalette()> palette_;
  bool open_ = false;
  int count_ = 0;
};
BEGIN_METADATA(ShieldButton)
END_METADATA

// The shield panel's first block, built like the fingerprint card: a tile,
// what was blocked here in `url`, and the site.
class ShieldCard final : public views::View {
  METADATA_HEADER(ShieldCard, views::View)
 public:
  explicit ShieldCard(const sidebar::DesignPalette& palette) : palette_(palette) {
    SetPreferredSize(gfx::Size(kMenuWidth - 8, 88));
    GetViewAccessibility().SetRole(ax::mojom::Role::kStatus);
  }
  void Set(int count, std::u16string site) {
    count_ = count; site_ = std::move(site);
    GetViewAccessibility().SetName(count_ < 0 ? T(u"Blokowanie wyłączone na tej stronie")
                                              : TF(u"Zablokowano na tej stronie: {}", {base::NumberToString16(count_)}));
    // As tall as the detail needs: one line or two, no empty band above the rows.
    int w = kMenuWidth - 8 - 16, h = 0;
    gfx::Canvas::SizeStringInt(Detail(), DesignFont(2, 12, 400), &w, &h, 0, gfx::Canvas::MULTI_LINE);
    SetPreferredSize(gfx::Size(kMenuWidth - 8, 48 + h + 8));
    SchedulePaint();
  }
  void OnPaint(gfx::Canvas* canvas) override {
    const gfx::RectF tile(8, 8, 32, 32);
    FillRound(canvas, tile, 6, count_ < 0 ? palette_.bg_hover : palette_.accent_soft);
    StrokeIcon(canvas, count_ < 0 ? kShieldOffIcon : kShieldIcon, gfx::RectF(16, 16, 16, 16), count_ < 0 ? palette_.ink_muted : palette_.accent_ink);
    const auto label = DesignFont(2, 12, 400);
    const int x = 52, right = width() - 8;
    canvas->DrawStringRectWithFlags(count_ < 0 ? T(u"Blokowanie wyłączone na tej stronie") : T(u"Zablokowano na tej stronie"),
                                    label, palette_.ink_muted, gfx::Rect(x, 6, right - x, 16), 0);
    canvas->DrawStringRectWithFlags(count_ < 0 ? u"—" : base::NumberToString16(count_), DesignFont(2, 13, 600), palette_.ink,
                                    gfx::Rect(x, 23, right - x, 18), gfx::Canvas::NO_SUBPIXEL_RENDERING);
    canvas->DrawStringRectWithFlags(Detail(), label, palette_.ink_muted, gfx::Rect(8, 48, width() - 16, height() - 48 - 8),
                                    gfx::Canvas::MULTI_LINE);
  }
 private:
  std::u16string Detail() const {
    return site_.empty() ? T(u"Reklamy i elementy śledzące z list EasyList i EasyPrivacy.")
                         : TF(u"{} · listy EasyList i EasyPrivacy oraz Twoje reguły.", {site_});
  }
  sidebar::DesignPalette palette_;
  int count_ = 0;
  std::u16string site_;
};
BEGIN_METADATA(ShieldCard)
END_METADATA

// A menu row: an extension (icon, name, site access, pin and more) or a
// footer command (icon, label).
class MenuRow final : public views::Button {
  METADATA_HEADER(MenuRow, views::Button)
 public:
  MenuRow(PressedCallback callback, const sidebar::DesignPalette& palette, int height)
      : views::Button(std::move(callback)), palette_(palette) {
    SetPreferredSize(gfx::Size(kMenuWidth - 8, height));
    SetNotifyEnterExitOnChild(true);
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(0, 8), 8));
    layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  }
  // Extension rows: the icon view, then text drawn by the row, then actions.
  void SetText(std::u16string title, std::u16string detail, bool warn, int lead) {
    title_ = std::move(title); detail_ = std::move(detail); warn_ = warn; lead_ = lead;
    GetViewAccessibility().SetName(detail_.empty() ? title_ : title_ + u", " + detail_);
  }
  void SetGlyph(const char* glyph) { glyph_ = glyph; }
  // The gear: spokes from the path and a ring in the middle.
  void SetRing(bool ring) { ring_ = ring; }
  void SetSelected(bool selected) { selected_ = selected; SchedulePaint(); }
  // Pin and more appear on hover; a pinned pin stays.
  void AddTrailing(views::View* view, bool always) { trailing_.push_back({view, always}); UpdateTrailing(); }
  views::View* text_slot() { return slot_; }
  void Init() {
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
  void OnMouseEntered(const ui::MouseEvent& event) override { views::Button::OnMouseEntered(event); UpdateTrailing(); }
  void OnMouseExited(const ui::MouseEvent& event) override { views::Button::OnMouseExited(event); UpdateTrailing(); }
  void PaintButtonContents(gfx::Canvas* canvas) override {
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
  void OnFocus() override { views::Button::OnFocus(); SchedulePaint(); }
  void OnBlur() override { views::Button::OnBlur(); SchedulePaint(); }
 private:
  void UpdateTrailing() {
    const bool hovered = IsMouseHovered();
    for (auto& [view, always] : trailing_) view->SetVisible(always || hovered);
    // Keep the text column the same width whether or not the actions show.
    InvalidateLayout();
  }
  sidebar::DesignPalette palette_;
  std::u16string title_, detail_;
  bool warn_ = false;
  int lead_ = 0;
  const char* glyph_ = nullptr;
  bool ring_ = false;
  bool selected_ = false;
  raw_ptr<views::View> icon_slot_ = nullptr;
  raw_ptr<views::View> slot_ = nullptr;
  std::vector<std::pair<raw_ptr<views::View>, bool>> trailing_;
};
BEGIN_METADATA(MenuRow)
END_METADATA

// The eyebrow above the rows: "ROZSZERZENIA" (or the panel's own label).
class MenuHeader final : public views::View {
  METADATA_HEADER(MenuHeader, views::View)
 public:
  explicit MenuHeader(const sidebar::DesignPalette& palette, std::u16string eyebrow = {})
      : eyebrow_(eyebrow.empty() ? T(u"Rozszerzenia") : std::move(eyebrow)), palette_(palette) {
    SetPreferredSize(gfx::Size(kMenuWidth - 8, 26));
  }
  void OnPaint(gfx::Canvas* canvas) override {
    const gfx::Rect box(8, 8, width() - 16, 14);
    canvas->DrawStringRectWithFlags(base::i18n::ToUpper(eyebrow_), DesignFont(2, 10, 600), palette_.ink_muted, box, 0);
  }
 private:
  std::u16string eyebrow_;
  sidebar::DesignPalette palette_;
};
BEGIN_METADATA(MenuHeader)
END_METADATA

class MenuSeparator final : public views::View {
  METADATA_HEADER(MenuSeparator, views::View)
 public:
  explicit MenuSeparator(SkColor color) : color_(color) { SetPreferredSize(gfx::Size(kMenuWidth - 8, 9)); }
  void OnPaint(gfx::Canvas* canvas) override { canvas->FillRect(gfx::Rect(8, 4, width() - 16, 1), color_); }
 private:
  SkColor color_;
};
BEGIN_METADATA(MenuSeparator)
END_METADATA

// Popup links open in a Zitter tab of the popup's profile: the popup has no
// browser window of its own.
class PopupHostDelegate final : public extensions::ExtensionViewHost::Delegate {
 public:
  explicit PopupHostDelegate(base::RepeatingCallback<void(const GURL&)> open) : open_(std::move(open)) {}
  content::WebContents* OpenURL(const content::OpenURLParams& params,
                                base::OnceCallback<void(content::NavigationHandle&)>) override {
    // Never reenter the shell from inside the renderer's navigation request.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce(open_, params.url));
    return nullptr;
  }
  content::KeyboardEventProcessingResult PreHandleKeyboardEvent(content::WebContents*,
      const input::NativeWebKeyboardEvent&) override {
    return content::KeyboardEventProcessingResult::NOT_HANDLED;
  }
  std::unique_ptr<content::EyeDropper> OpenEyeDropper(content::RenderFrameHost*, content::EyeDropperListener*) override {
    return nullptr;
  }
  // The popup belongs to its profile's Zitter window, as a Chrome popup
  // belongs to its browser window.
  extensions::WindowController* GetExtensionWindowController() override {
    auto* tabs = extensions::ExtensionTabUtil::GetEmbedderTabs();
    return tabs && profile_ ? tabs->GetWindow(profile_) : nullptr;
  }
  void set_profile(Profile* profile) { profile_ = profile; }
 private:
  base::RepeatingCallback<void(const GURL&)> open_;
  raw_ptr<Profile> profile_ = nullptr;
};

// Calls back when a widget becomes active (the popup closes when the window
// is used again) or goes away.
class ActivationWatcher final : public views::WidgetObserver {
 public:
  ActivationWatcher(views::Widget* widget, base::RepeatingClosure activated) : activated_(std::move(activated)) {
    observation_.Observe(widget);
  }
  void OnWidgetActivationChanged(views::Widget*, bool active) override { if (active) activated_.Run(); }
  void OnWidgetDestroying(views::Widget*) override { observation_.Reset(); }
 private:
  base::RepeatingClosure activated_;
  base::ScopedObservation<views::Widget, views::WidgetObserver> observation_{this};
};
}  // namespace

// The extensions menu (design: Extensions): every extension of the profile
// with its site access, pin and more; then manage and the Web Store. The bar
// owns this delegate and its widget.
class ExtensionsBar::Menu final : public views::BubbleDialogDelegate {
 public:
  Menu(ExtensionsBar* bar, views::View* anchor)
      : views::BubbleDialogDelegate(anchor, views::BubbleBorder::TOP_RIGHT), bar_(bar),
        palette_(bar->palette()) {
    SetOwnershipOfNewWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
    SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
    set_margins(gfx::Insets(4));
    set_corner_radius(12);
    set_fixed_width(kMenuWidth);
    SetBackgroundColor(palette_.bg_raised);
    SetShowCloseButton(false);
    SetAccessibleWindowRole(ax::mojom::Role::kMenu);
    SetTitle(T(u"Rozszerzenia"));
    SetShowTitle(false);
    auto body = std::make_unique<views::View>();
    body->SetLayoutManager(std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical));
    body_ = SetContentsView(std::move(body));
    Rebuild();
  }
  void Rebuild() {
    body_->RemoveAllChildViews();
    rows_.clear();
    auto* model = ToolbarActionsModel::Get(bar_->profile());
    const GURL url = bar_->page() ? bar_->page()->GetLastCommittedURL() : GURL();
    body_->AddChildView(std::make_unique<MenuHeader>(palette_));
    std::vector<const Extension*> extensions;
    for (const auto& id : model->action_ids())
      if (const auto* extension = bar_->ExtensionFor(id)) extensions.push_back(extension);
    std::ranges::sort(extensions, [](const Extension* a, const Extension* b) {
      return base::ToLowerASCII(a->name()) < base::ToLowerASCII(b->name());
    });
    if (extensions.empty()) {
      auto* row = body_->AddChildView(std::make_unique<MenuRow>(views::Button::PressedCallback(), palette_, kRowHeight));
      row->Init();
      row->SetText(T(u"Ten profil nie ma rozszerzeń"), T(u"Dodaj je z Chrome Web Store"), false, 0);
      row->SetEnabled(false);
    }
    auto weak = bar_->weak_factory_.GetWeakPtr();
    for (const auto* extension : extensions) {
      const std::string id = extension->id();
      auto* action = extensions::ExtensionActionManager::Get(bar_->profile())->GetExtensionAction(*extension);
      if (!action) continue;
      auto* row = body_->AddChildView(std::make_unique<MenuRow>(base::BindRepeating(
          [](base::WeakPtr<ExtensionsBar> bar, std::string id) { if (bar) bar->RunAction(id); }, weak, id),
          palette_, kRowHeight));
      row->AddChildView(std::make_unique<ActionButton>(bar_, *extension, *action, 20, false));
      row->Init();
      const auto interaction = bar_->page()
          ? extensions::SitePermissionsHelper(bar_->profile()).GetSiteInteraction(*extension, bar_->page())
          : SiteInteraction::kNone;
      row->SetText(base::UTF8ToUTF16(extension->name()), SiteAccessText(interaction, url),
                   interaction == SiteInteraction::kWithheld, 20);
      if (interaction == SiteInteraction::kWithheld) {
        row->AddTrailing(row->AddChildView(std::make_unique<QuietButton>(base::BindRepeating(
            [](base::WeakPtr<ExtensionsBar> bar, std::string id) { if (bar) bar->AllowSite(id); }, weak, id),
            T(u"Zezwól"), palette_)), true);
      } else {
        const bool pinned = model->IsActionPinned(id);
        auto* pin = row->AddChildView(std::make_unique<RowAction>(base::BindRepeating(
            [](base::WeakPtr<ExtensionsBar> bar, std::string id, bool pinned) { if (bar) bar->SetPinned(id, !pinned); },
            weak, id, pinned), pinned ? T(u"Odepnij") : T(u"Przypnij"), kPinIcon, palette_));
        pin->SetOn(pinned);
        row->AddTrailing(pin, pinned);
        auto* more = row->AddChildView(std::make_unique<RowAction>(views::Button::PressedCallback(),
            TF(u"Więcej: {}", {base::UTF8ToUTF16(extension->name())}), nullptr, palette_));
        more->SetCallback(base::BindRepeating(
            [](base::WeakPtr<ExtensionsBar> bar, std::string id, views::View* more) {
              if (bar) bar->ShowActionMenu(id, more, ui::mojom::MenuSourceType::kMouse);
            }, weak, id, more));
        row->AddTrailing(more, false);
      }
      rows_.push_back({id, row});
    }
    body_->AddChildView(std::make_unique<MenuSeparator>(palette_.line));
    auto add_footer = [&](std::u16string text, const char* glyph, GURL url) {
      auto* row = body_->AddChildView(std::make_unique<MenuRow>(base::BindRepeating(
          [](base::WeakPtr<ExtensionsBar> bar, GURL url) { if (bar) { bar->CloseMenus(); bar->OpenPage(url); } },
          weak, url), palette_, kFooterHeight));
      row->SetGlyph(glyph);
      row->Init();
      row->SetText(std::move(text), {}, false, 16);
      return row;
    };
    add_footer(T(u"Zarządzaj rozszerzeniami"), kGearIcon, GURL("chrome://extensions"))->SetRing(true);
    add_footer(u"Chrome Web Store", kStoreIcon, GURL("https://chromewebstore.google.com/category/extensions"));
    if (GetWidget()) SizeToContents();
  }
  std::vector<std::string> ids() const {
    std::vector<std::string> result;
    for (const auto& row : rows_) result.push_back(row.id);
    return result;
  }
 private:
  struct Row { std::string id; raw_ptr<MenuRow> view; };
  raw_ptr<ExtensionsBar> bar_;
  sidebar::DesignPalette palette_;
  raw_ptr<views::View> body_ = nullptr;
  std::vector<Row> rows_;
};

// An extension's popup under its icon: the extension page in a `bg-raised`
// card, `radius-md`, `shadow-float`. It shows immediately while loading and
// closes on window.close(), on Escape, and when the window is used again. The
// bar owns this delegate and its widget.
class ExtensionsBar::Popup final : public views::BubbleDialogDelegate,
                                   public ExtensionViewViews::Container,
                                   public content::WebContentsObserver {
 public:
  Popup(ExtensionsBar* bar, views::View* anchor, std::unique_ptr<extensions::ExtensionViewHost> host)
      : views::BubbleDialogDelegate(anchor, views::BubbleBorder::TOP_RIGHT, views::BubbleBorder::STANDARD_SHADOW, true),
        bar_(bar), host_(std::move(host)) {
    SetOwnershipOfNewWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
    SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
    set_margins(gfx::Insets());
    set_corner_radius(12);
    SetBackgroundColor(bar->palette().bg_raised);
    SetShowCloseButton(false);
    // Closing on deactivation would also close it for the extension's own
    // dialogs; using the window again closes it instead (as in Chrome).
    set_close_on_deactivate(false);
    auto view = std::make_unique<ExtensionViewViews>(bar->profile(), host_.get());
    view_ = view.get();
    view_->SetContainer(this);
    view_->Init();
    SetContentsView(std::move(view));
    SetInitiallyFocusedView(view_);
    // window.close() from the popup page.
    host_->SetCloseHandler(base::BindOnce(&Popup::HostClosed, base::Unretained(this)));
    anchor_watcher_ = std::make_unique<ActivationWatcher>(anchor->GetWidget(),
        base::BindRepeating(&Popup::AnchorActivated, base::Unretained(this)));
    Observe(host_->host_contents());
  }
  ~Popup() override {
    Observe(nullptr);
    anchor_watcher_.reset();
  }
  content::WebContents* contents() const { return host_ ? host_->host_contents() : nullptr; }
  // Called once the bubble widget exists.
  void Attached() {
    // Attaching a native holder hides Chromium popup WebViews until onload.
    // Reveal after attachment so the renderer can paint and auto-size while
    // remote resources are pending.
    view_->SetVisible(true);
    // Escape belongs to the extension page (ExtensionViewHost closes on it).
    if (auto* client = GetDialogClientView()) client->RemoveAccelerator(ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE));
    if (auto* frame = GetBubbleFrameView()) view_->holder()->SetNativeViewCornerRadii(frame->GetRoundedCorners());
    // Render the popup immediately. Waiting for window.onload also waits for
    // remote resources and makes password-manager actions appear unresponsive.
    Reveal();
  }
  gfx::Size GetMinBounds() override { return kPopupMin; }
  gfx::Size GetMaxBounds() override { return kPopupMax; }
  void DocumentOnLoadCompletedInPrimaryMainFrame() override {
    Observe(nullptr);
    if (GetWidget() && !GetWidget()->IsClosed()) SizeToContents();
  }
  void Close() { if (GetWidget() && !GetWidget()->IsClosed()) GetWidget()->Close(); }

 private:
  void Reveal() {
    GetWidget()->Show();
    if (host_) host_->host_contents()->Focus();
  }
  // Using the window again closes the popup, as in Chrome.
  void AnchorActivated() { if (GetWidget() && GetWidget()->IsVisible()) Close(); }
  void HostClosed(extensions::ExtensionHost*) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce(
        [](base::WeakPtr<ExtensionsBar> bar) { if (bar) bar->ClosePopup(); }, bar_->weak_factory_.GetWeakPtr()));
  }
  raw_ptr<ExtensionsBar> bar_;
  std::unique_ptr<extensions::ExtensionViewHost> host_;
  raw_ptr<ExtensionViewViews> view_ = nullptr;
  std::unique_ptr<ActivationWatcher> anchor_watcher_;
};

// Fingerprint protection under its button (design: Extensions menu): what this
// site sees, then a new fingerprint, turning protection off and Preferences.
// The bar owns this delegate and its widget.
class ExtensionsBar::FingerprintPanel final : public views::BubbleDialogDelegate {
 public:
  FingerprintPanel(ExtensionsBar* bar, views::View* anchor)
      : views::BubbleDialogDelegate(anchor, views::BubbleBorder::TOP_RIGHT), bar_(bar), palette_(bar->palette()) {
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
    body->SetLayoutManager(std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical));
    body->AddChildView(std::make_unique<MenuHeader>(palette_, T(u"Odcisk przeglądarki")));
    card_ = body->AddChildView(std::make_unique<FingerprintCard>(palette_));
    body->AddChildView(std::make_unique<MenuSeparator>(palette_.line));
    auto weak = bar_->weak_factory_.GetWeakPtr();
    auto add_row = [&](std::u16string text, const char* glyph, void (ExtensionsBar::*command)()) {
      auto* row = body->AddChildView(std::make_unique<MenuRow>(base::BindRepeating(
          [](base::WeakPtr<ExtensionsBar> bar, void (ExtensionsBar::*command)()) { if (bar) (bar.get()->*command)(); },
          weak, command), palette_, kFooterHeight));
      row->SetGlyph(glyph);
      row->Init();
      row->SetText(std::move(text), {}, false, 16);
      return row;
    };
    add_row(T(u"Zmień odcisk"), kTurnIcon, &ExtensionsBar::NewFingerprint);
    add_row(T(u"Wyłącz ochronę"), kShieldOffIcon, &ExtensionsBar::TurnOffFingerprintProtection);
    add_row(T(u"Ustawienia prywatności"), kGearIcon, &ExtensionsBar::OpenPrivacySettings)->SetRing(true);
    SetContentsView(std::move(body));
    Refresh();
  }
  void Refresh() {
    const GURL url = bar_->page() ? bar_->page()->GetLastCommittedURL() : GURL();
    card_->SetCode(bar_->profile() ? FingerprintProtection::SiteCode(bar_->profile(), url) : std::u16string());
  }
  // A new fingerprint lands: one tremor of the code and of the bar's button.
  void Tremor() {
    if (!gfx::Animation::ShouldRenderRichAnimation()) return;
    step_ = 0;
    timer_.Start(FROM_HERE, base::Milliseconds(45), base::BindRepeating([](FingerprintPanel* panel) {
      static constexpr std::array<int, 4> kOffsets = {1, -1, 1, 0};
      if (panel->step_ >= kOffsets.size()) { panel->timer_.Stop(); return; }
      const int dx = kOffsets[panel->step_++];
      panel->card_->SetShake(dx);
      if (auto* button = views::AsViewClass<FingerprintButton>(panel->bar_->fingerprint_button_.get())) button->SetShake(dx);
    }, base::Unretained(this)));
  }
  const std::u16string& code() const { return card_->code(); }
 private:
  raw_ptr<ExtensionsBar> bar_;
  sidebar::DesignPalette palette_;
  raw_ptr<FingerprintCard> card_ = nullptr;
  base::RepeatingTimer timer_;
  size_t step_ = 0;
};

// Ad blocking under the shield: what was blocked here, the site switch, the
// assistant for what the lists miss, and Preferences. The bar owns it.
class ExtensionsBar::ShieldPanel final : public views::BubbleDialogDelegate {
 public:
  ShieldPanel(ExtensionsBar* bar, views::View* anchor)
      : views::BubbleDialogDelegate(anchor, views::BubbleBorder::TOP_RIGHT), bar_(bar), palette_(bar->palette()) {
    SetOwnershipOfNewWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
    SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
    set_margins(gfx::Insets(4));
    set_corner_radius(12);
    set_fixed_width(kMenuWidth);
    SetBackgroundColor(palette_.bg_raised);
    SetShowCloseButton(false);
    SetAccessibleWindowRole(ax::mojom::Role::kDialog);
    SetTitle(T(u"Blokowanie reklam"));
    SetShowTitle(false);
    auto body = std::make_unique<views::View>();
    body->SetLayoutManager(std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical));
    body->AddChildView(std::make_unique<MenuHeader>(palette_, T(u"Blokowanie reklam")));
    card_ = body->AddChildView(std::make_unique<ShieldCard>(palette_));
    body->AddChildView(std::make_unique<MenuSeparator>(palette_.line));
    auto weak = bar_->weak_factory_.GetWeakPtr();
    auto add_row = [&](const char* glyph, void (ExtensionsBar::*command)()) {
      auto* row = body->AddChildView(std::make_unique<MenuRow>(base::BindRepeating(
          [](base::WeakPtr<ExtensionsBar> bar, void (ExtensionsBar::*command)()) { if (bar) (bar.get()->*command)(); },
          weak, command), palette_, kFooterHeight));
      row->SetGlyph(glyph);
      row->Init();
      return row;
    };
    site_row_ = add_row(kShieldOffIcon, &ExtensionsBar::ToggleSiteBlocking);
    if (bar_->callbacks_.assistant_available && bar_->callbacks_.assistant_available.Run() && bar_->callbacks_.block_with_assistant)
      add_row(kSparkIcon, &ExtensionsBar::BlockWithAssistant)->SetText(T(u"Zablokuj więcej z asystentem"), {}, false, 16);
    auto* settings = add_row(kGearIcon, &ExtensionsBar::OpenAdBlockingSettings);
    settings->SetText(T(u"Ustawienia blokowania"), {}, false, 16);
    settings->SetRing(true);
    SetContentsView(std::move(body));
    Refresh();
  }
  void Refresh() {
    const int count = bar_->ShieldCount();
    const GURL url = bar_->page() ? bar_->page()->GetLastCommittedURL() : GURL();
    card_->Set(count, base::UTF8ToUTF16(ContentBlocking::SiteKey(url)));
    site_row_->SetGlyph(count < 0 ? kShieldIcon : kShieldOffIcon);
    site_row_->SetText(count < 0 ? T(u"Włącz blokowanie na tej stronie") : T(u"Wyłącz blokowanie na tej stronie"), {}, false, 16);
    site_row_->SetEnabled(url.SchemeIsHTTPOrHTTPS());
    site_row_->SchedulePaint();
    // Another site's detail may take another number of lines.
    if (GetWidget() && !GetWidget()->IsClosed()) SizeToContents();
  }
 private:
  raw_ptr<ExtensionsBar> bar_;
  sidebar::DesignPalette palette_;
  raw_ptr<ShieldCard> card_ = nullptr;
  raw_ptr<MenuRow> site_row_ = nullptr;
};

ExtensionsBar::ExtensionsBar(Callbacks callbacks) : callbacks_(std::move(callbacks)) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 2));
  layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  GetViewAccessibility().SetRole(ax::mojom::Role::kToolbar);
  GetViewAccessibility().SetName(T(u"Rozszerzenia"));
  SetVisible(false);
}

ExtensionsBar::~ExtensionsBar() {
  // Bubbles report closing through weak pointers; this bar is going away.
  weak_factory_.InvalidateWeakPtrs();
  shield_widget_.reset();
  shield_panel_.reset();
  fingerprint_widget_.reset();
  fingerprint_panel_.reset();
  context_menu_.reset();
  popup_widget_.reset();
  popup_.reset();
  menu_widget_.reset();
  menu_.reset();
}

sidebar::DesignPalette ExtensionsBar::palette() const { return PaletteOf(IsDark(this), workspace_color()); }
SkColor ExtensionsBar::workspace_color() const {
  return callbacks_.workspace_color ? callbacks_.workspace_color.Run() : 0xFF928FD4;
}

void ExtensionsBar::SetContext(Profile* profile, content::WebContents* page) {
  if (profile && profile->IsOffTheRecord()) profile = nullptr;
  const bool profile_changed = profile != profile_;
  const bool page_changed = page != page_.get();
  if (page_changed) ClosePopup();
  page_ = page ? page->GetWeakPtr() : nullptr;
  if (page_changed) ObservePageCount();
  if (profile_changed) {
    CloseMenus();
    model_observation_.Reset();
    profile_ = profile;
    ObserveFingerprintProtection();
    if (auto* model = profile_ ? ToolbarActionsModel::Get(profile_) : nullptr) model_observation_.Observe(model);
    Rebuild();
    return;
  }
  RefreshButtons();
  RefreshShield();
  if (fingerprint_panel_) fingerprint_panel_->Refresh();
}

const Extension* ExtensionsBar::ExtensionFor(const std::string& id) const {
  if (!profile_) return nullptr;
  return extensions::ExtensionRegistry::Get(profile_)->enabled_extensions().GetByID(id);
}

int ExtensionsBar::tab_id() const {
  return page_ ? sessions::SessionTabHelper::IdForTab(page_.get()).id() : -1;
}

void ExtensionsBar::Rebuild() {
  RemoveAllChildViews();
  buttons_.clear();
  divider_ = nullptr;
  puzzle_ = nullptr;
  fingerprint_button_ = nullptr;
  shield_button_ = nullptr;
  auto* model = profile_ ? ToolbarActionsModel::Get(profile_) : nullptr;
  SetVisible(model != nullptr);
  const bool protection = model && profile_->GetPrefs()->GetBoolean(kFingerprintProtectionPref);
  if (!protection && fingerprint_widget_ && !fingerprint_widget_->IsClosed()) fingerprint_widget_->Close();
  const bool blocking = model && profile_->GetPrefs()->GetBoolean(kAdBlockingPref);
  if (!blocking && shield_widget_ && !shield_widget_->IsClosed()) shield_widget_->Close();
  if (!model) { PreferredSizeChanged(); return; }
  auto palette = base::BindRepeating(&ExtensionsBar::palette, base::Unretained(this));
  if (blocking) {
    shield_button_ = AddChildView(std::make_unique<ShieldButton>(
        base::BindRepeating(&ExtensionsBar::ToggleShieldPanel, base::Unretained(this)), palette));
    if (shield_widget_) static_cast<ShieldButton*>(shield_button_.get())->SetOpen(true);
    static_cast<ShieldButton*>(shield_button_.get())->SetCount(ShieldCount());
  }
  if (protection) {
    fingerprint_button_ = AddChildView(std::make_unique<FingerprintButton>(
        base::BindRepeating(&ExtensionsBar::ToggleFingerprintPanel, base::Unretained(this)), palette));
    if (fingerprint_widget_) static_cast<FingerprintButton*>(fingerprint_button_.get())->SetOpen(true);
  }
  size_t shown = 0;
  for (const auto& id : model->pinned_action_ids()) {
    if (shown == kMaxVisible) break;
    const auto* extension = ExtensionFor(id);
    auto* action = extension ? extensions::ExtensionActionManager::Get(profile_)->GetExtensionAction(*extension) : nullptr;
    if (!action) continue;
    buttons_.push_back(AddChildView(std::make_unique<ActionButton>(this, *extension, *action, kIcon, true)));
    if (id == popup_id_) buttons_.back()->SetPopupOpen(true);
    ++shown;
  }
  if (!buttons_.empty() || fingerprint_button_ || shield_button_) divider_ = AddChildView(std::make_unique<Divider>(palette));
  puzzle_ = AddChildView(std::make_unique<PuzzleButton>(
      base::BindRepeating(&ExtensionsBar::ToggleMenu, base::Unretained(this)), palette));
  if (menu_widget_) static_cast<PuzzleButton*>(puzzle_.get())->SetOpen(true);
  ScheduleMenuRebuild();
  PreferredSizeChanged();
  SchedulePaint();
}

// Menu rows run the commands that change the model (pin, allow), so the menu
// never rebuilds inside a row's own click.
void ExtensionsBar::ScheduleMenuRebuild() {
  if (!menu_ || menu_rebuild_pending_) return;
  menu_rebuild_pending_ = true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce(
      [](base::WeakPtr<ExtensionsBar> bar) {
        if (!bar) return;
        bar->menu_rebuild_pending_ = false;
        if (bar->menu_ && bar->profile_) bar->menu_->Rebuild();
      }, weak_factory_.GetWeakPtr()));
}

void ExtensionsBar::RefreshButtons() {
  for (auto& button : buttons_) button->Refresh();
}

void ExtensionsBar::ToggleMenu() {
  if (menu_widget_) { CloseMenus(); return; }
  if (!profile_ || !puzzle_ || !GetWidget()) return;
  menu_ = std::make_unique<Menu>(this, puzzle_);
  // Never delete a widget on its own closing stack.
  menu_widget_ = views::BubbleDialogDelegate::CreateBubble(menu_.get(), base::BindOnce(
      [](base::WeakPtr<ExtensionsBar> bar, views::Widget::ClosedReason) {
        base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
            base::BindOnce(&ExtensionsBar::DestroyMenu, bar));
      }, weak_factory_.GetWeakPtr()));
  static_cast<PuzzleButton*>(puzzle_.get())->SetOpen(true);
  menu_widget_->Show();
}

void ExtensionsBar::DestroyMenu() {
  menu_widget_.reset();
  menu_.reset();
  if (puzzle_) static_cast<PuzzleButton*>(puzzle_.get())->SetOpen(false);
}

void ExtensionsBar::CloseMenus() {
  if (menu_widget_ && !menu_widget_->IsClosed()) menu_widget_->Close();
}

void ExtensionsBar::RunAction(const std::string& id) {
  const auto* extension = ExtensionFor(id);
  if (!extension || !page_) return;
  auto* runner = extensions::ExtensionActionRunner::GetForWebContents(page_.get());
  if (!runner) return;
  // The open popup of this extension toggles closed, as in Chrome.
  if (popup_widget_ && popup_id_ == id) { ClosePopup(); return; }
  CloseMenus();
  const auto show = runner->RunAction(extension, /*grant_tab_permissions=*/true);
  if (show != ExtensionAction::ShowAction::kShowPopup) return;
  views::View* anchor = puzzle_;
  for (auto& button : buttons_) if (button->id() == id) anchor = button;
  ShowPopup(*extension, anchor);
}

void ExtensionsBar::ShowPopup(const Extension& extension, views::View* anchor) {
  auto* action = extensions::ExtensionActionManager::Get(profile_)->GetExtensionAction(extension);
  if (!action || !anchor || !anchor->GetWidget()) return;
  const GURL url = action->GetPopupUrl(tab_id());
  if (!url.is_valid()) return;
  DestroyPopup();
  auto delegate = std::make_unique<PopupHostDelegate>(base::BindRepeating(
      [](base::WeakPtr<ExtensionsBar> bar, const GURL& url) {
        if (!bar) return;
        bar->ClosePopup();
        bar->OpenPage(url);
      }, weak_factory_.GetWeakPtr()));
  delegate->set_profile(profile_);
  auto host = std::make_unique<extensions::ExtensionViewHost>(&extension, profile_, url,
      extensions::mojom::ViewType::kExtensionPopup, std::move(delegate));
  popup_ = std::make_unique<Popup>(this, anchor, std::move(host));
  popup_widget_ = views::BubbleDialogDelegate::CreateBubble(popup_.get(), base::BindOnce(
      [](base::WeakPtr<ExtensionsBar> bar, views::Widget::ClosedReason) {
        base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
            base::BindOnce(&ExtensionsBar::DestroyPopup, bar));
      }, weak_factory_.GetWeakPtr()));
  popup_id_ = extension.id();
  for (auto& button : buttons_) button->SetPopupOpen(button->id() == popup_id_);
  popup_->Attached();
}

void ExtensionsBar::ClosePopup() {
  if (popup_widget_ && !popup_widget_->IsClosed()) popup_widget_->Close();
}

void ExtensionsBar::DestroyPopup() {
  // The widget (and the extension view in it) goes before the page's host.
  popup_widget_.reset();
  popup_.reset();
  popup_id_.clear();
  for (auto& button : buttons_) button->SetPopupOpen(false);
}

void ExtensionsBar::ShowActionMenu(const std::string& id, views::View* source, ui::mojom::MenuSourceType source_type) {
  if (!profile_ || !ExtensionFor(id)) return;
  const auto* extension = ExtensionFor(id);
  context_menu_ = std::make_unique<ExtensionsContextMenu>(this, id, ToolbarActionsModel::Get(profile_)->IsActionPinned(id),
                                                          extensions::OptionsPageInfo::HasOptionsPage(extension));
  // Context-menu event points are already in screen coordinates. Anchor to
  // the icon itself so mouse and keyboard invocation both open below it.
  context_menu_->Run(source->GetWidget(), source->GetBoundsInScreen(), source_type);
}

void ExtensionsBar::SetPinned(const std::string& id, bool pinned) {
  if (auto* model = profile_ ? ToolbarActionsModel::Get(profile_) : nullptr; model && model->HasAction(id))
    model->SetActionVisibility(id, pinned);
}

void ExtensionsBar::AllowSite(const std::string& id) {
  const auto* extension = ExtensionFor(id);
  if (!extension || !page_) return;
  extensions::SitePermissionsHelper(profile_).UpdateSiteAccess(
      *extension, page_.get(), extensions::PermissionsManager::UserSiteAccess::kOnSite,
      page_->GetPrimaryMainFrame()->GetLastCommittedOrigin());
  ScheduleMenuRebuild();
  RefreshButtons();
}

void ExtensionsBar::OpenOptions(const std::string& id) {
  if (const auto* extension = ExtensionFor(id); extension && extensions::OptionsPageInfo::HasOptionsPage(extension))
    OpenPage(extensions::OptionsPageInfo::GetOptionsPage(extension));
}

void ExtensionsBar::Uninstall(const std::string& id) {
  const auto* extension = ExtensionFor(id);
  if (!extension || !GetWidget()) return;
  uninstall_ = std::make_unique<ExtensionsUninstall>(profile_, GetWidget()->GetNativeWindow(),
                                                     base::WrapRefCounted(extension));
}

void ExtensionsBar::OpenPage(const GURL& url) {
  if (profile_ && callbacks_.open_page && url.is_valid()) callbacks_.open_page.Run(profile_, url);
}

// A new extension lands: one tremor of the puzzle (design: Extensions), not a toast.
void ExtensionsBar::Tremor() {
  ++tremor_count_;
  if (!puzzle_ || !gfx::Animation::ShouldRenderRichAnimation()) return;
  // Four ±1 px steps over `dur-tremor` (180 ms).
  tremor_step_ = 0;
  tremor_timer_.Start(FROM_HERE, base::Milliseconds(45), base::BindRepeating([](ExtensionsBar* bar) {
    static constexpr std::array<int, 4> kOffsets = {1, -1, 1, 0};
    if (!bar->puzzle_ || bar->tremor_step_ >= kOffsets.size()) { bar->tremor_timer_.Stop(); return; }
    static_cast<PuzzleButton*>(bar->puzzle_.get())->SetShake(kOffsets[bar->tremor_step_++]);
  }, base::Unretained(this)));
}

void ExtensionsBar::OnToolbarActionAdded(const ToolbarActionsModel::ActionId&) {
  Rebuild();
  if (profile_ && ToolbarActionsModel::Get(profile_)->actions_initialized()) Tremor();
}
void ExtensionsBar::OnToolbarActionRemoved(const ToolbarActionsModel::ActionId& id) {
  if (id == popup_id_) DestroyPopup();
  Rebuild();
}
// setIcon, setBadgeText, enable/disable and friends: repaint that action.
void ExtensionsBar::OnToolbarActionUpdated(const ToolbarActionsModel::ActionId& id) {
  for (auto& button : buttons_) if (button->id() == id) button->Refresh();
  ScheduleMenuRebuild();
}
void ExtensionsBar::OnToolbarModelInitialized() { Rebuild(); }
void ExtensionsBar::OnToolbarPinnedActionsChanged() { Rebuild(); }
void ExtensionsBar::OnToolbarActionsModelShutdown() {
  model_observation_.Reset();
  profile_ = nullptr;
  ObserveFingerprintProtection();
  page_ = nullptr;
  Rebuild();
}

void ExtensionsBar::ObserveFingerprintProtection() {
  fingerprint_pref_.Reset();
  fingerprint_key_ = {};
  blocking_changed_ = {};
  if (!profile_) return;
  fingerprint_pref_.Init(profile_->GetPrefs());
  fingerprint_pref_.Add(kFingerprintProtectionPref, base::BindRepeating(&ExtensionsBar::Rebuild, base::Unretained(this)));
  // Ad blocking shares the registrar: its switch adds or removes the shield.
  fingerprint_pref_.Add(kAdBlockingPref, base::BindRepeating(&ExtensionsBar::Rebuild, base::Unretained(this)));
  blocking_changed_ = ContentBlocking::Get().Observe(base::BindRepeating(&ExtensionsBar::RefreshShield, base::Unretained(this)));
  fingerprint_key_ = FingerprintProtection::ObserveSessionKey(profile_, base::BindRepeating(
      [](ExtensionsBar* bar) { if (bar->fingerprint_panel_) bar->fingerprint_panel_->Refresh(); }, base::Unretained(this)));
}

void ExtensionsBar::ToggleFingerprintPanel() {
  if (fingerprint_widget_) { if (!fingerprint_widget_->IsClosed()) fingerprint_widget_->Close(); return; }
  if (!profile_ || !fingerprint_button_ || !GetWidget()) return;
  CloseMenus();
  ClosePopup();
  fingerprint_panel_ = std::make_unique<FingerprintPanel>(this, fingerprint_button_);
  // Never delete a widget on its own closing stack.
  fingerprint_widget_ = views::BubbleDialogDelegate::CreateBubble(fingerprint_panel_.get(), base::BindOnce(
      [](base::WeakPtr<ExtensionsBar> bar, views::Widget::ClosedReason) {
        base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
            base::BindOnce(&ExtensionsBar::DestroyFingerprintPanel, bar));
      }, weak_factory_.GetWeakPtr()));
  static_cast<FingerprintButton*>(fingerprint_button_.get())->SetOpen(true);
  fingerprint_widget_->Show();
}

void ExtensionsBar::DestroyFingerprintPanel() {
  fingerprint_widget_.reset();
  fingerprint_panel_.reset();
  if (fingerprint_button_) static_cast<FingerprintButton*>(fingerprint_button_.get())->SetOpen(false);
}

void ExtensionsBar::NewFingerprint() {
  if (!profile_) return;
  FingerprintProtection::NewSessionKey(profile_);
  if (fingerprint_panel_) fingerprint_panel_->Tremor();
}

void ExtensionsBar::TurnOffFingerprintProtection() {
  // The pref observer removes the button and closes this panel.
  if (profile_) profile_->GetPrefs()->SetBoolean(kFingerprintProtectionPref, false);
}

void ExtensionsBar::OpenPrivacySettings() {
  if (fingerprint_widget_ && !fingerprint_widget_->IsClosed()) fingerprint_widget_->Close();
  if (callbacks_.open_privacy_settings) callbacks_.open_privacy_settings.Run();
}

void ExtensionsBar::ObservePageCount() {
  page_count_ = {};
  counted_page_ = reinterpret_cast<uintptr_t>(page_.get());
  if (!page_) return;
  ContentBlockingTabHelper::CreateForWebContents(page_.get());
  page_count_ = ContentBlockingTabHelper::FromWebContents(page_.get())->ObserveCount(
      base::BindRepeating(&ExtensionsBar::RefreshShield, base::Unretained(this)));
  RefreshShield();
}
int ExtensionsBar::ShieldCount() const {
  if (!profile_ || !page_) return 0;
  const GURL url = page_->GetLastCommittedURL();
  if (url.SchemeIsHTTPOrHTTPS() && ContentBlocking::SiteAllowed(profile_, url)) return -1;
  auto* helper = ContentBlockingTabHelper::FromWebContents(page_.get());
  return helper ? helper->blocked() : 0;
}
void ExtensionsBar::RefreshShield() {
  if (reinterpret_cast<uintptr_t>(page_.get()) != counted_page_) { ObservePageCount(); return; }
  if (auto* button = views::AsViewClass<ShieldButton>(shield_button_.get())) button->SetCount(ShieldCount());
  if (shield_panel_) shield_panel_->Refresh();
}
void ExtensionsBar::ToggleShieldPanel() {
  if (shield_widget_) { if (!shield_widget_->IsClosed()) shield_widget_->Close(); return; }
  if (!profile_ || !shield_button_ || !GetWidget()) return;
  CloseMenus();
  ClosePopup();
  if (fingerprint_widget_ && !fingerprint_widget_->IsClosed()) fingerprint_widget_->Close();
  shield_panel_ = std::make_unique<ShieldPanel>(this, shield_button_);
  // Never delete a widget on its own closing stack.
  shield_widget_ = views::BubbleDialogDelegate::CreateBubble(shield_panel_.get(), base::BindOnce(
      [](base::WeakPtr<ExtensionsBar> bar, views::Widget::ClosedReason) {
        base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
            base::BindOnce(&ExtensionsBar::DestroyShieldPanel, bar));
      }, weak_factory_.GetWeakPtr()));
  static_cast<ShieldButton*>(shield_button_.get())->SetOpen(true);
  shield_widget_->Show();
}
void ExtensionsBar::DestroyShieldPanel() {
  shield_widget_.reset();
  shield_panel_.reset();
  if (shield_button_) static_cast<ShieldButton*>(shield_button_.get())->SetOpen(false);
}
void ExtensionsBar::ToggleSiteBlocking() {
  if (!profile_ || !page_) return;
  const GURL url = page_->GetLastCommittedURL();
  if (!url.SchemeIsHTTPOrHTTPS()) return;
  ContentBlocking::SetSiteAllowed(profile_, ContentBlocking::SiteKey(url), !ContentBlocking::SiteAllowed(profile_, url));
  // What already loaded or was hidden changes only with a fresh load.
  page_->GetController().Reload(content::ReloadType::NORMAL, true);
  RefreshShield();
}
void ExtensionsBar::BlockWithAssistant() {
  if (shield_widget_ && !shield_widget_->IsClosed()) shield_widget_->Close();
  if (callbacks_.block_with_assistant) callbacks_.block_with_assistant.Run();
}
void ExtensionsBar::OpenAdBlockingSettings() {
  if (shield_widget_ && !shield_widget_->IsClosed()) shield_widget_->Close();
  if (callbacks_.open_ad_blocking_settings) callbacks_.open_ad_blocking_settings.Run();
}
int ExtensionsBar::shield_count_for_testing() const {
  return shield_button_ ? static_cast<const ShieldButton*>(shield_button_.get())->count() : 0;
}
void ExtensionsBar::RunShieldCommandForTesting(int command) {
  if (command == 0) ToggleSiteBlocking();
  if (command == 1) BlockWithAssistant();
  if (command == 2) OpenAdBlockingSettings();
}

std::u16string ExtensionsBar::fingerprint_code_for_testing() const {
  return fingerprint_panel_ ? fingerprint_panel_->code() : std::u16string();
}

void ExtensionsBar::RunFingerprintCommandForTesting(int command) {
  if (command == 0) NewFingerprint();
  if (command == 1) TurnOffFingerprintProtection();
  if (command == 2) OpenPrivacySettings();
}

void ExtensionsBar::OnThemeChanged() {
  views::View::OnThemeChanged();
  SchedulePaint();
  for (const auto& child : children()) child->SchedulePaint();
}

std::vector<ExtensionsBar::ActionSnapshot> ExtensionsBar::visible_actions_for_testing() const {
  std::vector<ActionSnapshot> result;
  for (const auto& button : buttons_)
    result.push_back({button->id(), button->name(), button->badge(), button->enabled_on_page(),
                      button->popup_open(), button->icon_hash()});
  return result;
}
std::vector<std::string> ExtensionsBar::menu_ids_for_testing() const {
  return menu_ ? menu_->ids() : std::vector<std::string>();
}
views::View* ExtensionsBar::action_button_for_testing(std::string_view id) const {
  for (const auto& button : buttons_) if (button->id() == id) return button;
  return nullptr;
}
views::Widget* ExtensionsBar::menu_for_testing() const { return menu_widget_.get(); }
views::Widget* ExtensionsBar::popup_for_testing() const { return popup_widget_.get(); }
content::WebContents* ExtensionsBar::popup_contents_for_testing() const {
  return popup_ ? popup_->contents() : nullptr;
}

BEGIN_METADATA(ExtensionsBar)
END_METADATA

}  // namespace zitter::prototype
