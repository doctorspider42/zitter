// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "web_app_window.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/memory/raw_ref.h"
#include "base/i18n/rtl.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "build/build_config.h"
#include "chromium/prototype/i18n.h"
#include "chromium/prototype/link_navigation.h"
#include "chromium/prototype/page_dialogs.h"
#include "chromium/prototype/sidebar_scene.rs.h"
#include "chromium/prototype/toolbar_menu.h"
#include "content/public/browser/invalidate_type.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "lab_web_view.h"
#include "profile_context.h"
#include "third_party/blink/public/mojom/frame/fullscreen.mojom.h"
#include "third_party/blink/public/mojom/window_features/window_features.mojom.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/window_open_disposition.h"
#include "ui/color/color_provider_key.h"
#include "ui/display/screen.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"

#if BUILDFLAG(IS_WIN)
#include "ui/base/win/shell.h"
#include "ui/views/win/hwnd_util.h"
#endif

namespace zitter::content_embedder {

namespace {

constexpr char kBackIcon[] = "M10 3.5 5.5 8l4.5 4.5";
constexpr char kReloadIcon[] = "M12.5 8a4.5 4.5 0 1 1-1.3-3.2M12.5 3v2.3h-2.3";
constexpr char kOpenInBrowserIcon[] = "M9 3h4v4M13 3 7.5 8.5M11.5 9.5V13H3V4.5h3.5";
constexpr int kToolbarHeight = 36;

#if BUILDFLAG(IS_MAC)
constexpr int kShortcutModifier = ui::EF_COMMAND_DOWN;
#else
constexpr int kShortcutModifier = ui::EF_CONTROL_DOWN;
#endif

// The toolbar's colours: the app's theme colour when its manifest has one,
// Zitter's design palette otherwise.
struct ToolbarColors {
  SkColor background;
  SkColor ink;
  SkColor muted;
  SkColor hover;
};

ToolbarColors ColorsFor(const views::Widget* widget, std::optional<SkColor> theme) {
  const bool dark =
      widget && widget->GetColorMode() == ui::ColorProviderKey::ColorMode::kDark;
  const auto palette = sidebar::design_palette(dark, 0);
  if (!theme) {
    return {palette.bg_base, palette.ink, palette.ink_muted, palette.bg_hover};
  }
  const SkColor background = SkColorSetA(*theme, SK_AlphaOPAQUE);
  const SkColor ink = color_utils::GetColorWithMaxContrast(background);
  return {background, ink, SkColorSetA(ink, 0xB0), SkColorSetA(ink, 0x24)};
}

class ToolbarButton final : public views::Button {
  METADATA_HEADER(ToolbarButton, views::Button)

 public:
  ToolbarButton(const char* path, std::u16string title, PressedCallback callback,
                base::RepeatingCallback<ToolbarColors()> colors)
      : views::Button(std::move(callback)), path_(path), colors_(std::move(colors)) {
    SetPreferredSize(gfx::Size(28, 28));
    SetTooltipText(title);
    GetViewAccessibility().SetName(title);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const auto colors = colors_.Run();
    const bool hover =
        GetState() == STATE_HOVERED || GetState() == STATE_PRESSED || HasFocus();
    if (hover && GetEnabled())
      prototype::FillRound(canvas, gfx::RectF(GetLocalBounds()), 7, colors.hover);
    const gfx::RectF glyph(width() / 2 - 8, height() / 2 - 8, 16, 16);
    prototype::StrokeIcon(canvas, path_, glyph,
                          GetEnabled() ? (hover ? colors.ink : colors.muted)
                                       : SkColorSetA(colors.muted, 0x50));
  }

 private:
  const char* path_;
  base::RepeatingCallback<ToolbarColors()> colors_;
};

BEGIN_METADATA(ToolbarButton)
END_METADATA

class Toolbar final : public views::View {
  METADATA_HEADER(Toolbar, views::View)

 public:
  explicit Toolbar(base::RepeatingCallback<ToolbarColors()> colors)
      : colors_(std::move(colors)) {
    SetPreferredSize(gfx::Size(0, kToolbarHeight));
  }

  void OnPaint(gfx::Canvas* canvas) override {
    const auto colors = colors_.Run();
    canvas->DrawColor(colors.background);
    canvas->FillRect(gfx::Rect(0, height() - 1, width(), 1), colors.hover);
  }

  void OnThemeChanged() override {
    views::View::OnThemeChanged();
    if (title_)
      title_->SetEnabledColor(colors_.Run().ink);
    SchedulePaint();
  }

  void set_title(views::Label* title) { title_ = title; }

 private:
  base::RepeatingCallback<ToolbarColors()> colors_;
  raw_ptr<views::Label> title_ = nullptr;
};

BEGIN_METADATA(Toolbar)
END_METADATA

// Window shortcuts: reload, back, forward and close.
class RootView final : public views::View {
  METADATA_HEADER(RootView, views::View)

 public:
  explicit RootView(WebAppWindow& window) : window_(window) {
    for (const auto& accelerator :
         {ui::Accelerator(ui::VKEY_F5, ui::EF_NONE),
          ui::Accelerator(ui::VKEY_R, kShortcutModifier),
          ui::Accelerator(ui::VKEY_LEFT, ui::EF_ALT_DOWN),
          ui::Accelerator(ui::VKEY_W, kShortcutModifier)}) {
      AddAccelerator(accelerator);
    }
  }

  bool AcceleratorPressed(const ui::Accelerator& accelerator) override {
    switch (accelerator.key_code()) {
      case ui::VKEY_F5:
      case ui::VKEY_R:
        window_->Reload();
        return true;
      case ui::VKEY_LEFT:
        window_->GoBack();
        return true;
      case ui::VKEY_W:
        window_->Close();
        return true;
      default:
        return false;
    }
  }

 private:
  const raw_ref<WebAppWindow> window_;
};

BEGIN_METADATA(RootView)
END_METADATA

}  // namespace

// The page of an app window and its WebContentsDelegate.
class AppWebView final : public LabWebView {
  METADATA_HEADER(AppWebView, LabWebView)

 public:
  AppWebView(ProfileContext* profile, WebAppWindow& window)
      : LabWebView(profile), window_(window) {}

  content::WebContents* OpenURLFromTab(
      content::WebContents* source,
      const content::OpenURLParams& params,
      base::OnceCallback<void(content::NavigationHandle&)> callback) override {
    if (params.disposition == WindowOpenDisposition::CURRENT_TAB &&
        (window_->popup() || window_->app().InScope(params.url))) {
      content::NavigationController::LoadURLParams load(params);
      auto navigation = source->GetController().LoadURLWithParams(load);
      if (navigation && callback)
        std::move(callback).Run(*navigation);
      return source;
    }
    window_->OpenOutside(params.url);
    return nullptr;
  }

  content::WebContents* AddNewContents(
      content::WebContents* source,
      std::unique_ptr<content::WebContents> page,
      const GURL& target_url,
      WindowOpenDisposition disposition,
      const blink::mojom::WindowFeatures&,
      bool user_gesture,
      bool* was_blocked) override {
    if (disposition == WindowOpenDisposition::NEW_POPUP) {
      auto* popup = page.get();
      window_->AdoptPopup(std::move(page));
      return popup;
    }
    if (target_url.SchemeIsHTTPOrHTTPS() && user_gesture) {
      window_->OpenOutside(target_url);
    } else if (was_blocked) {
      *was_blocked = true;
    }
    return nullptr;
  }

  void CloseContents(content::WebContents*) override { window_->Close(); }

  void NavigationStateChanged(content::WebContents*,
                              content::InvalidateTypes) override {
    window_->PageChanged();
  }

  content::JavaScriptDialogManager* GetJavaScriptDialogManager(
      content::WebContents* source) override {
    return prototype::PageDialogs::FromWebContents(source);
  }

  void ShowRepostFormWarningDialog(content::WebContents* page) override {
    if (auto* dialogs = prototype::PageDialogs::FromWebContents(page))
      dialogs->ConfirmFormResubmission();
    else
      page->GetController().CancelPendingReload();
  }

  blink::mojom::DisplayMode GetDisplayMode(const content::WebContents*) override {
    return window_->fullscreen() ? blink::mojom::DisplayMode::kFullscreen
                                 : blink::mojom::DisplayMode::kStandalone;
  }

  void EnterFullscreenModeForTab(content::RenderFrameHost*,
                                 const blink::mojom::FullscreenOptions&) override {
    window_->SetFullscreen(true);
  }

  void ExitFullscreenModeForTab(content::WebContents*) override {
    window_->SetFullscreen(false);
  }

  bool IsFullscreenForTabOrPending(const content::WebContents*) override {
    return window_->fullscreen();
  }

  bool IsBackForwardCacheSupported(content::WebContents&) override { return true; }

 private:
  const raw_ref<WebAppWindow> window_;
};

BEGIN_METADATA(AppWebView)
END_METADATA

WebAppWindow::Callbacks::Callbacks() = default;
WebAppWindow::Callbacks::Callbacks(Callbacks&&) = default;
WebAppWindow::Callbacks& WebAppWindow::Callbacks::operator=(Callbacks&&) = default;
WebAppWindow::Callbacks::~Callbacks() = default;

WebAppWindow::WebAppWindow(const WebApp& app,
                           std::string profile_id,
                           ProfileContext& profile,
                           base::FilePath icon_file,
                           Callbacks callbacks,
                           const GURL& url,
                           std::unique_ptr<content::WebContents> popup)
    : app_(app),
      profile_id_(std::move(profile_id)),
      popup_(!!popup),
      callbacks_(std::move(callbacks)) {
  widget_ = std::make_unique<views::Widget>();
  const auto colors = base::BindRepeating(
      [](base::WeakPtr<WebAppWindow> window) {
        return ColorsFor(window ? window->widget_.get() : nullptr,
                         window ? window->app_.theme_color : std::nullopt);
      },
      weak_factory_.GetWeakPtr());
  const auto weak = weak_factory_.GetWeakPtr();

  auto root = std::make_unique<RootView>(*this);
  auto* layout = root->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical));
  layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kStretch);

  auto* toolbar = root->AddChildView(std::make_unique<Toolbar>(colors));
  auto* row = toolbar->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(4, 6), 2));
  row->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  back_ = toolbar->AddChildView(std::make_unique<ToolbarButton>(
      kBackIcon, prototype::T(u"Wstecz"),
      base::BindRepeating(&WebAppWindow::GoBack, weak), colors));
  toolbar->AddChildView(std::make_unique<ToolbarButton>(
      kReloadIcon, prototype::T(u"Odśwież"),
      base::BindRepeating(&WebAppWindow::Reload, weak), colors));
  title_ = toolbar->AddChildView(std::make_unique<views::Label>(app_.name));
  title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title_->SetElideBehavior(gfx::ELIDE_TAIL);
  title_->SetAutoColorReadabilityEnabled(false);
  title_->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, 6)));
  row->SetFlexForView(title_, 1);
  toolbar->set_title(title_);
  toolbar->AddChildView(std::make_unique<ToolbarButton>(
      kOpenInBrowserIcon, prototype::T(u"Otwórz w Zitter"),
      base::BindRepeating(&WebAppWindow::OpenInBrowser, weak), colors));

  web_view_ = root->AddChildView(std::make_unique<AppWebView>(&profile, *this));
  layout->SetFlexForView(web_view_, 1);
  content::WebContents* page = nullptr;
  if (popup) {
    page = popup.get();
    web_view_->SetOwnedWebContents(std::move(popup));
  } else {
    page = web_view_->GetWebContents(url);
    prototype::LinkNavigation::Attach(
        page, base::BindRepeating(
                  [](base::WeakPtr<WebAppWindow> window, content::NavigationHandle& navigation) {
                    if (!window || window->app_.InScope(navigation.GetURL()) ||
                        !navigation.GetURL().SchemeIsHTTPOrHTTPS()) {
                      return false;
                    }
                    window->OpenOutside(navigation.GetURL());
                    return true;
                  },
                  weak));
  }
  if (!prototype::PageDialogs::FromWebContents(page)) {
    prototype::PageDialogs::Attach(
        page, base::BindRepeating(
                  [](std::u16string name, content::BrowserContext*) { return name; },
                  app_.name));
  }

  delegate_ = std::make_unique<views::WidgetDelegate>();
  delegate_->SetTitle(app_.name);
  delegate_->SetHasWindowSizeControls(true);
  if (!app_.icon.drawsNothing()) {
    const auto icon = ui::ImageModel::FromImageSkia(
        gfx::ImageSkia::CreateFrom1xBitmap(app_.icon));
    delegate_->SetIcon(icon);
    delegate_->SetAppIcon(icon);
  }
  delegate_->SetInitiallyFocusedView(web_view_);
  delegate_->SetContentsView(std::move(root));

  views::Widget::InitParams params(views::Widget::InitParams::CLIENT_OWNS_WIDGET,
                                   views::Widget::InitParams::TYPE_WINDOW);
  params.delegate = delegate_.get();
  const auto work_area = display::Screen::Get()->GetPrimaryDisplay().work_area();
  gfx::Rect bounds(0, 0, popup_ ? 520 : 1024, popup_ ? 640 : 720);
  bounds.AdjustToFit(work_area);
  bounds.set_origin(work_area.CenterPoint() -
                    gfx::Vector2d(bounds.width() / 2, bounds.height() / 2));
  params.bounds = bounds;
  widget_->Init(std::move(params));
  widget_->AddObserver(this);
#if BUILDFLAG(IS_WIN)
  // Its own taskbar group, and pinning it launches the app.
  if (HWND hwnd = views::HWNDForWidget(widget_.get())) {
    ui::win::SetAppDetailsForWindow(
        WebAppUserModelId(profile_id_, app_.id), icon_file, 0,
        WebAppLaunchCommand(profile_id_, app_.id).GetCommandLineString(),
        base::UTF16ToWide(app_.name), hwnd);
  }
#endif
  if (!popup_)
    web_view_->LoadInitialURL(url);
  PageChanged();
  widget_->Show();
  web_view_->RequestFocus();
}

WebAppWindow::~WebAppWindow() {
  weak_factory_.InvalidateWeakPtrs();
  if (widget_) {
    widget_->RemoveObserver(this);
    widget_->CloseNow();
    widget_.reset();
  }
  delegate_.reset();
}

void WebAppWindow::Activate() {
  if (!widget_ || closed_)
    return;
  if (widget_->IsMinimized())
    widget_->Restore();
  widget_->Show();
  widget_->Activate();
}

void WebAppWindow::Close() {
  if (widget_ && !closed_)
    widget_->Close();
}

void WebAppWindow::GoBack() {
  auto* page = web_view_ ? web_view_->web_contents() : nullptr;
  if (page && page->GetController().CanGoBack())
    page->GetController().GoBack();
}

void WebAppWindow::Reload() {
  if (auto* page = web_view_ ? web_view_->web_contents() : nullptr)
    page->GetController().Reload(content::ReloadType::NORMAL, true);
}

void WebAppWindow::OpenInBrowser() {
  auto* page = web_view_ ? web_view_->web_contents() : nullptr;
  if (!page)
    return;
  const GURL url = page->GetLastCommittedURL();
  if (!url.SchemeIsHTTPOrHTTPS())
    return;
  callbacks_.open_in_browser.Run(url);
  Close();
}

void WebAppWindow::OpenOutside(const GURL& url) {
  // Navigation throttles and delegate calls ask for this; the Workspace opens
  // the tab once they return.
  if (url.SchemeIsHTTPOrHTTPS()) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(callbacks_.open_in_browser, url));
  }
}

void WebAppWindow::AdoptPopup(std::unique_ptr<content::WebContents> page) {
  callbacks_.open_popup.Run(std::move(page));
}

void WebAppWindow::PageChanged() {
  auto* page = web_view_ ? web_view_->web_contents() : nullptr;
  if (!page)
    return;
  if (back_)
    back_->SetEnabled(page->GetController().CanGoBack());
  if (title_) {
    std::u16string title = page->GetTitle();
    base::i18n::SanitizeUserSuppliedString(&title);
    if (title.empty() || title == base::UTF8ToUTF16(page->GetVisibleURL().spec()))
      title = app_.name;
    title_->SetText(title.substr(0, 512));
    title_->SetEnabledColor(ColorsFor(widget_.get(), app_.theme_color).ink);
    title_->SetTooltipText(base::UTF8ToUTF16(page->GetVisibleURL().spec()));
  }
  if (popup_ && widget_) {
    std::u16string title = page->GetTitle();
    base::i18n::SanitizeUserSuppliedString(&title);
    delegate_->SetTitle(title.empty() ? app_.name : title.substr(0, 512));
  }
}

void WebAppWindow::SetFullscreen(bool fullscreen) {
  if (!widget_ || fullscreen_ == fullscreen)
    return;
  fullscreen_ = fullscreen;
  widget_->SetFullscreen(fullscreen);
}

void WebAppWindow::OnWidgetClosing(views::Widget*) {
  if (closed_)
    return;
  closed_ = true;
  callbacks_.closed.Run(this);
}

}  // namespace zitter::content_embedder
