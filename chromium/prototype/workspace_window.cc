#include "zitter/core/mouse_gesture.h"
#include "content/public/browser/render_widget_host_view.h"
#include "ui/color/color_provider_key.h"
#include "ui/views/background.h"
#include "chromium/prototype/browser_import_data.h"
#include "chromium/prototype/workspace_window.h"
#include "zitter/core/pin_tree.h"
#include "chromium/prototype/assistant_panel.h"
#include "ui/views/view_tracker.h"
#include "chromium/prototype/timeline_store.h"
#include "chromium/app/startup_timing.h"
#include "base/time/default_clock.h"
#include "chromium/prototype/i18n.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "chromium/prototype/download_panel.h"
#include "chromium/prototype/history_panel.h"
#include "chromium/prototype/rss_reader.h"
#include "content/public/browser/weak_document_ptr.h"
#include "content/public/common/isolated_world_ids.h"
#include "content/public/browser/host_zoom_map.h"
#include "content/public/common/url_constants.h"
#include "chromium/prototype/site_data.h"
#include "chromium/prototype/passwords_panel.h"
#include "content/public/browser/download_request_utils.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "chromium/prototype/ad_blocking_rules_panel.h"
#include "chromium/prototype/content_blocking.h"
#include "chromium/prototype/fingerprint_protection.h"
#include "chromium/prototype/page_services.h"
#include "chrome/browser/printing/print_view_manager.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/browser/printing/print_view_manager_common.h"
#include "components/find_in_page/find_types.h"
#include "chromium/prototype/page_dialogs.h"
#include "chromium/prototype/page_media_access.h"
#include "chromium/prototype/page_file_access.h"
#include "content/public/browser/file_select_listener.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/fullscreen_types.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_widget_host.h"
#include "third_party/blink/public/mojom/manifest/display_mode.mojom.h"
#include "chromium/prototype/browser_preferences.h"
#include "chromium/prototype/clean_url.h"
#include "chromium/prototype/link_navigation.h"
#include "chromium/prototype/image_link_preview.h"
#include "base/task/single_thread_task_runner.h"
#include "base/unguessable_token.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/context_menu_params.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "chromium/prototype/arc_importer.h"

#include "chrome/browser/favicon/favicon_service_factory.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/favicon/core/favicon_service.h"
#include "components/favicon_base/favicon_types.h"
#include "content/public/browser/favicon_status.h"
#include "third_party/blink/public/mojom/favicon/favicon_url.mojom.h"
#include "base/containers/flat_set.h"
#include "base/strings/escape.h"
#include "base/strings/string_split.h"
#include "net/base/load_flags.h"
#include "re2/re2.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "url/url_constants.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/gfx/image/image_skia_rep.h"
#include "skia/ext/image_operations.h"
#include <utility>
#include <algorithm>
#include <set>
#include <cmath>
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/ui/zoom/chrome_zoom_level_prefs.h"
#include "chrome/common/pref_names.h"
#include "chrome/browser/preloading/preloading_prefs.h"
#include "chrome/browser/devtools/devtools_window.h"
#include "chrome/browser/devtools/devtools_toggle_action.h"
#include "content/public/browser/weak_document_ptr.h"
#include "content/public/common/isolated_world_ids.h"
#include "ui/shell_dialogs/selected_file_info.h"
#include "ui/shell_dialogs/select_file_policy.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/pref_names.h"
#include "components/password_manager/core/common/password_manager_pref_names.h"
#include "components/prefs/pref_service.h"
#include "third_party/blink/public/common/page/page_zoom.h"

#include "base/auto_reset.h"
#include "base/base64.h"
#include "base/command_line.h"
#include "chromium/app/windows_channel.h"
#include "chromium/prototype/extensions_bar.h"
#include "chromium/prototype/night_shift.h"
#include "chromium/prototype/extension_tab_events.h"
#include "chrome/browser/ui/extensions/extension_install_ui.h"
#include "chrome/browser/ui/toolbar/toolbar_actions_model.h"
#include "chrome/browser/extensions/extension_tab_util.h"
#include "chrome/browser/extensions/window_controller.h"
#include "chrome/browser/extensions/window_controller_list.h"
#include "ui/base/base_window.h"
#include "components/sessions/content/session_tab_helper.h"
#include "extensions/common/extension.h"
#include "base/check.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_util.h"
#include "chrome/browser/browser_process.h"
#include "components/os_crypt/async/browser/os_crypt_async.h"
#include "components/os_crypt/async/common/encryptor.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/delete_profile_helper.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "content/public/browser/storage_partition.h"
#include "base/task/thread_pool.h"
#include "base/uuid.h"
#include "base/no_destructor.h"
#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/i18n/case_conversion.h"
#include "chrome/browser/history/history_service_factory.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_types.h"
#include "build/build_config.h"
#include "chromium/prototype/sidebar_scene.rs.h"
#include "chrome/browser/profiles/profile.h"
#include "components/input/native_web_keyboard_event.h"
#include "content/public/browser/invalidate_type.h"
#include "content/public/browser/keyboard_event_processing_result.h"
#include "content/public/browser/reload_type.h"
#include "content/public/common/isolated_world_ids.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/navigation_entry.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/page_transition_types.h"
#include "ui/content_accelerators/accelerator_util.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/native/native_view_host.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_MAC)
#include "chromium/prototype/workspace_frame_mac.h"
#include "chromium/prototype/workspace_keyboard_mac.h"
#elif BUILDFLAG(IS_WIN)
#include <windows.h>
#include <appmodel.h>
#include <shellapi.h>

#include "chrome/app/chrome_dll_resource.h"
#include "chromium/prototype/workspace_frame_win.h"
#include "chromium/prototype/jump_list_win.h"
#include "chromium/prototype/password_csv_import.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util_win.h"
#include "base/win/registry.h"
#include "base/win/scoped_gdi_object.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/image/image_family.h"
#include "ui/gfx/win/icon_util.h"
#endif

namespace zitter::prototype {
namespace {
constexpr char kWindowPlacementFile[] = "ZitterWindow.json";
constexpr gfx::Rect kDefaultWindowBounds(80, 80, 1280, 820);

base::SequencedTaskRunner& WindowPlacementRunner() {
  static const base::NoDestructor<scoped_refptr<base::SequencedTaskRunner>> runner(
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN}));
  return **runner;
}

// Palette result provenance; kind is sidebar::PaletteResultKind. Rust owns the
// wording.
std::u16string ResultDetail(int kind, const std::u16string& label) {
  return base::UTF8ToUTF16(std::string(sidebar::palette_result_detail(sidebar::PaletteResultKind{static_cast<uint8_t>(kind)}, base::UTF16ToUTF8(label))));
}
}  // namespace

namespace {

favicon::FaviconService* ImplicitFaviconService(Profile* profile) {
  // Chromium rejects implicit access for OTR profiles. Private pages can
  // fetch icons in memory without reading or writing the regular profile DB.
  return profile->IsOffTheRecord() ? nullptr :
      FaviconServiceFactory::GetForProfile(profile, ServiceAccessType::IMPLICIT_ACCESS);
}


// Only this allowlist crosses the native settings/profile boundary.
struct BooleanPreference {
  const char* key;
  const char* pref;
};
constexpr BooleanPreference kBooleanPreferences[] = {
    {"do_not_track", prefs::kEnableDoNotTrack},
    {"https_only", prefs::kHttpsOnlyModeEnabled},
    {"download_prompt", prefs::kPromptForDownload},
    {"link_auditing", prefs::kEnableHyperlinkAuditing},
    {"save_passwords", password_manager::prefs::kCredentialsEnableService},
    {"auto_signin", password_manager::prefs::kCredentialsEnableAutosignin},
    {"fingerprint_protection", kFingerprintProtectionPref},
    {"ad_blocking", kAdBlockingPref},
};
// One switch for the two lists that hide blocking from pages (Settings).
constexpr const char* kStealthLists[] = {"zitter", "antiadblock"};
struct ContentPreference {
  const char* key;
  ContentSettingsType type;
  ContentSetting enabled;
};
constexpr ContentPreference kContentPreferences[] = {
    {"javascript", ContentSettingsType::JAVASCRIPT, CONTENT_SETTING_ALLOW},
    {"images", ContentSettingsType::IMAGES, CONTENT_SETTING_ALLOW},
    {"popups", ContentSettingsType::POPUPS, CONTENT_SETTING_ALLOW},
    {"sound", ContentSettingsType::SOUND, CONTENT_SETTING_ALLOW},
    {"notifications", ContentSettingsType::NOTIFICATIONS, CONTENT_SETTING_ASK},
    {"location", ContentSettingsType::GEOLOCATION, CONTENT_SETTING_ASK},
    {"camera", ContentSettingsType::MEDIASTREAM_CAMERA, CONTENT_SETTING_ASK},
    {"microphone", ContentSettingsType::MEDIASTREAM_MIC, CONTENT_SETTING_ASK},
    {"clipboard", ContentSettingsType::CLIPBOARD_READ_WRITE, CONTENT_SETTING_ASK},
};
constexpr std::array kZoomPercentages = {75, 90, 100, 110, 125, 150, 175, 200};
constexpr std::array kFontSizes = {12, 14, 16, 18, 20, 24};
constexpr std::array kMinimumFontSizes = {0, 10, 12, 14, 16, 18};
// Page zoom steps, as in Chrome.
constexpr std::array kPageZoomPercentages = {25, 33, 50, 67, 75, 80, 90, 100, 110, 125, 150, 175, 200, 250, 300, 400, 500};

// Ctrl+scroll and Ctrl+= / Ctrl+- / Ctrl+0: one step in or out (`step` +1 or
// -1) or back to the default (0). The level is kept per site, as in Chrome.
void ZoomPage(content::WebContents* page, int step) {
  if (!page) return;
  auto* map = content::HostZoomMap::GetForWebContents(page);
  if (step == 0) { content::HostZoomMap::SetZoomLevel(page, map->GetDefaultZoomLevel()); page->SetPageScale(1.f); return; }
  const int current = static_cast<int>(std::lround(blink::ZoomLevelToZoomFactor(content::HostZoomMap::GetZoomLevel(page)) * 100));
  int next = current;
  if (step > 0) { for (int p : kPageZoomPercentages) if (p > current) { next = p; break; } }
  else { for (auto it = kPageZoomPercentages.rbegin(); it != kPageZoomPercentages.rend(); ++it) if (*it < current) { next = *it; break; } }
  if (next != current) content::HostZoomMap::SetZoomLevel(page, blink::ZoomFactorToZoomLevel(next / 100.0));
}
// The zoom step of a page key, if it is one: Ctrl (Cmd) with =, - or 0.
std::optional<int> PageZoomStep(const ui::Accelerator& accelerator) {
#if BUILDFLAG(IS_MAC)
  constexpr int kPrimary = ui::EF_COMMAND_DOWN;
#else
  constexpr int kPrimary = ui::EF_CONTROL_DOWN;
#endif
  if (accelerator.modifiers() != kPrimary) return std::nullopt;
  switch (accelerator.key_code()) {
    case ui::VKEY_OEM_PLUS: case ui::VKEY_ADD: return 1;
    case ui::VKEY_OEM_MINUS: case ui::VKEY_SUBTRACT: return -1;
    case ui::VKEY_0: case ui::VKEY_NUMPAD0: return 0;
    default: return std::nullopt;
  }
}

// WebView owns the delegate of its WebContents. Extend that delegate rather
// than replacing it and losing WebView's sizing/focus/lifecycle behavior.
// Native reader tabs use an inert opaque-origin document solely as the existing
// tab/navigation identity. No renderer bindings, profile data or feed text cross
// this boundary. Only browser-created loads can set the virtual internal URL.
void LoadWorkspaceTab(content::WebContents* page, const GURL& url,
                      ui::PageTransition transition) {
  content::NavigationController::LoadURLParams params(url);
  params.transition_type = transition;
  if (IsRssTabUrl(url)) {
    params.url = GURL("data:text/html,<meta http-equiv=Content-Security-Policy content=\"default-src 'none'\"><title>RSS</title>");
    params.load_type = content::NavigationController::LOAD_TYPE_DATA;
    params.virtual_url_for_special_cases = url;
  }
  page->GetController().LoadURLWithParams(params);
}
class WorkspaceWebView final : public views::WebView, public views::FocusChangeListener {
  METADATA_HEADER(WorkspaceWebView, views::WebView)

 public:
  using KeyHandler = base::RepeatingCallback<
      content::KeyboardEventProcessingResult(
          content::WebContents*, const input::NativeWebKeyboardEvent&)>;

  using OpenHandler = base::RepeatingCallback<content::WebContents*(content::WebContents*, const content::OpenURLParams&)>;
  using MenuHandler = base::RepeatingCallback<void(content::WebContents*, content::RenderFrameHost*, const content::ContextMenuParams&)>;
  using AdoptHandler = base::RepeatingCallback<content::WebContents*(content::WebContents*, std::unique_ptr<content::WebContents>, bool)>;
  using CreatedHandler = base::RepeatingCallback<void(content::WebContents*, content::WebContents*)>;
  using PeekHandler = base::RepeatingCallback<bool(content::WebContents*, const GURL&)>;
  WorkspaceWebView(Profile* profile, KeyHandler handler, OpenHandler open, MenuHandler menu,
                   AdoptHandler adopt, CreatedHandler created, base::RepeatingClosure changed,
                    base::RepeatingCallback<void(views::WebView*)> interacted,
                    base::RepeatingCallback<void(content::WebContents*, bool)> fullscreen,
                    base::RepeatingCallback<bool()> image_previews, PeekHandler peek,
                    base::RepeatingCallback<bool()> gestures_enabled,
                    base::RepeatingCallback<void(content::WebContents*, std::string)> gesture)
      : views::WebView(profile), handler_(std::move(handler)), open_(std::move(open)), menu_(std::move(menu)),
        adopt_(std::move(adopt)), created_(std::move(created)), changed_(std::move(changed)), interacted_(std::move(interacted)), fullscreen_(std::move(fullscreen)),
        image_previews_(std::move(image_previews)), peek_(std::move(peek)), gestures_enabled_(std::move(gestures_enabled)), gesture_(std::move(gesture)) {}
  ~WorkspaceWebView() override {
    if (devtools_view_) devtools_view_->SetWebContents(nullptr);
    if (web_contents()) web_contents()->RemoveUserData(DevToolsWindow::kEmbedderDockingHostKey);
    UnmountReader(false); if (GetFocusManager() && GetFocusManager()->HasFocusChangeListener(this)) GetFocusManager()->RemoveFocusChangeListener(this); if (is_fullscreen_) fullscreen_.Run(web_contents(), false); }
  void SetWebContents(content::WebContents* page) override {
    if (page != web_contents()) {
      if (devtools_view_) devtools_view_->SetWebContents(nullptr);
      if (web_contents()) web_contents()->RemoveUserData(DevToolsWindow::kEmbedderDockingHostKey);
      blank_background_.reset();
      awaiting_first_paint_ = page && (page->GetController().IsInitialNavigation() || page->IsLoading());
    }
    views::WebView::SetWebContents(page);
    UpdateBlankBackground();
    if (page && !DevToolsWindow::GetEmbedderDockingHost(page)) {
      page->SetUserData(DevToolsWindow::kEmbedderDockingHostKey,
          std::make_unique<DevToolsWindow::EmbedderDockingHost>(base::BindRepeating(
              &WorkspaceWebView::UpdateDockedDevTools, base::Unretained(this))));
    }
  }
  void UpdateDockedDevTools() {
    auto* frontend = DevToolsWindow::GetInTabWebContents(web_contents(), &devtools_strategy_);
    if (frontend && !devtools_view_) {
      devtools_view_ = AddChildViewAt(std::make_unique<views::WebView>(
          web_contents()->GetBrowserContext()), 0);
      devtools_view_->set_allow_accelerators(true);
    }
    if (devtools_view_) {
      devtools_view_->SetWebContents(frontend);
      devtools_view_->SetVisible(frontend != nullptr);
    }
    // Expose both native web trees while the frontend is embedded.
    set_lock_child_ax_tree_id_override(frontend || rss_);
    if (frontend) {
      GetViewAccessibility().RemoveChildTreeID();
      GetViewAccessibility().SetIsIgnored(false);
      GetViewAccessibility().SetRole(ax::mojom::Role::kGenericContainer);
    } else if (!rss_) {
      GetViewAccessibility().SetRole(ax::mojom::Role::kWebView);
      views::WebView::AXTreeIDForMainFrameHasChanged();
    }
    LayoutDockedDevTools();
  }
  void LayoutDockedDevTools() {
    gfx::Rect page_bounds = GetLocalBounds();
    if (devtools_view_ && devtools_view_->web_contents()) {
      gfx::Rect tools_bounds;
      ApplyDevToolsContentsResizingStrategy(devtools_strategy_, GetLocalBounds(),
                                            &tools_bounds, &page_bounds);
      devtools_view_->SetBoundsRect(tools_bounds);
    }
    if (!rss_) holder()->SetBoundsRect(page_bounds);
  }
  std::array<uint64_t,2> sidebar_identity() const {return {sidebar_identity_.GetHighForSerialization(),sidebar_identity_.GetLowForSerialization()};}
  void ResetImagePreview() { if (image_preview_) image_preview_->Reset(); }
  bool image_preview_visible() const { return image_preview_ && image_preview_->visible_for_testing(); }
  RssPanel* rss() const { return rss_.get(); }
  void SetReaderOpen(base::RepeatingCallback<void(GURL)> open) { rss_open_ = std::move(open); }
  void MountReader() {
    if (rss_ || !GetWidget()) return;
    rss_ = std::make_unique<RssPanel>();
    rss_->Show(Profile::FromBrowserContext(GetWebContents()->GetBrowserContext()), this, rss_open_);
    rss_->view()->AddPreTargetHandler(&reader_input_);
    holder()->SetVisible(false);
    set_lock_child_ax_tree_id_override(true);
    GetViewAccessibility().RemoveChildTreeID();
    GetViewAccessibility().SetIsIgnored(false);
    GetViewAccessibility().SetRole(ax::mojom::Role::kGenericContainer);
  }
  void SetWorkspaceBackground(SkColor color) {
    workspace_color_ = color;
    UpdateBlankBackground();
  }

 private:
  class ReaderInput final : public ui::EventHandler {
   public:
    explicit ReaderInput(WorkspaceWebView* owner) : owner_(owner) {}
    void OnEvent(ui::Event* event) override {
      if (owner_->rss_ && (event->type() == ui::EventType::kMousePressed || event->type() == ui::EventType::kTouchPressed)) {
        owner_->interacted_.Run(owner_);
        owner_->rss_->RequestFocus();
      }
    }
   private:
    raw_ptr<WorkspaceWebView> owner_;
  } reader_input_{this};
  void UnmountReader(bool restore_accessibility = true) {
    if (!rss_) return;
    if (rss_ && rss_->view()) rss_->view()->RemovePreTargetHandler(&reader_input_);
    rss_.reset();
    set_lock_child_ax_tree_id_override(false);
    if (restore_accessibility) {
      GetViewAccessibility().SetRole(ax::mojom::Role::kWebView);
      views::WebView::AXTreeIDForMainFrameHasChanged();
    }
  }
  raw_ptr<views::WebView> devtools_view_ = nullptr;
  DevToolsContentsResizingStrategy devtools_strategy_;
  std::unique_ptr<RssPanel> rss_;
  base::RepeatingCallback<void(GURL)> rss_open_;
  void AddedToWidget() override {
    views::WebView::AddedToWidget();
    GetFocusManager()->AddFocusChangeListener(this);
  }
  void OnWillChangeFocus(views::View*, views::View*) override {}
  void OnDidChangeFocus(views::View*, views::View* now) override {
    if (now && Contains(now)) interacted_.Run(this);
  }
  void OnBoundsChanged(const gfx::Rect& previous) override {
    views::WebView::OnBoundsChanged(previous);
    LayoutDockedDevTools();
    if (rss_ && rss_->view()) {
      holder()->SetVisible(false);
      // WebView also calls this for every descendant hierarchy change. Setting
      // unchanged bounds would immediately lay out the partially rebuilt RSS
      // tree for each added row/text node instead of batching invalidations.
      if (rss_->view()->bounds() != GetLocalBounds())
        rss_->view()->SetBoundsRect(GetLocalBounds());
    }
  }
  bool SkipDefaultKeyEventProcessing(const ui::KeyEvent& event) override {
    return rss_ ? views::View::SkipDefaultKeyEventProcessing(event)
                : views::WebView::SkipDefaultKeyEventProcessing(event);
  }
  gfx::NativeViewAccessible GetNativeViewAccessible() override {
    return (rss_ || (devtools_view_ && devtools_view_->web_contents())) ? views::View::GetNativeViewAccessible()
                : views::WebView::GetNativeViewAccessible();
  }
  void OnFocus() override {
    if (rss_ && rss_->view()) rss_->RequestFocus();
    else views::WebView::OnFocus();
  }
  void DidFinishNavigation(content::NavigationHandle* navigation) override {
    if (!navigation->HasCommitted() || !navigation->IsInPrimaryMainFrame()) return;
    if (IsRssTabUrl(web_contents()->GetVisibleURL())) MountReader();
    else if (rss_) { UnmountReader(); holder()->SetVisible(true); }
    changed_.Run();
  }
  void RenderViewReady() override {
    views::WebView::RenderViewReady();
    UpdateBlankBackground();
  }
  void DidFirstVisuallyNonEmptyPaint() override {
    awaiting_first_paint_ = false;
    UpdateBlankBackground();
  }
  SkColor workspace_color_ = SK_ColorTRANSPARENT;
  std::optional<SkColor> blank_background_;
  bool awaiting_first_paint_ = false;
  void UpdateBlankBackground() {
    auto* page = web_contents();
    if (!page || !GetWidget()) return;
    const auto& url = page->GetLastCommittedURL();
    const bool blank = url.is_empty() || url.IsAboutBlank();
    const auto color = sidebar::design_palette(
        GetWidget()->GetColorMode() == ui::ColorProviderKey::ColorMode::kDark,
        workspace_color_).bg_base;
    // Tint the initial blank document and the first load until the renderer
    // paints real content. Loaded pages keep their own document background.
    const std::optional<SkColor> background =
        blank || awaiting_first_paint_ ? std::optional<SkColor>(color) : std::nullopt;
    if (background != blank_background_) {
      page->SetPageBaseBackgroundColor(background);
      blank_background_ = background;
    }
    // This is the native fallback before a compositor frame. Content colors
    // supplied by the renderer take precedence over it.
    if (auto* native = page->GetRenderWidgetHostView()) native->SetBackgroundColor(color);
    if (!this->background() || this->background()->color() != color)
      SetBackground(views::CreateSolidBackground(color));
  }
  void OnBlur() override {
    mouse_gesture_.Cancel();
    views::WebView::OnBlur();
  }
  void OnThemeChanged() override {
    views::WebView::OnThemeChanged();
    UpdateBlankBackground();
  }
  const base::UnguessableToken sidebar_identity_=base::UnguessableToken::Create();
  void UpdateTargetURL(content::WebContents* source, const GURL& url) override {
    hovered_link_ = url;
    if (!image_preview_ || image_preview_->web_contents() != source)
      image_preview_ = std::make_unique<ImageLinkPreview>(source, this, image_previews_);
    image_preview_->Hover(url);
  }
  void VisibilityChanged(views::View* starting_from, bool visible) override {
    if (!visible) { ResetImagePreview(); mouse_gesture_.Cancel(); }
    views::WebView::VisibilityChanged(starting_from, visible);
  }
  void RemovedFromWidget() override {
    // A Peek WebView moves to the main window when promoted.
    image_preview_.reset();
    if (GetFocusManager() && GetFocusManager()->HasFocusChangeListener(this)) GetFocusManager()->RemoveFocusChangeListener(this);
    views::WebView::RemovedFromWidget();
  }
  bool CanEnterFullscreenModeForTab(content::RenderFrameHost*) override {
    return IsDrawn() && GetWidget() && GetWidget()->IsVisible();
  }
  void EnterFullscreenModeForTab(content::RenderFrameHost* frame,
      const blink::mojom::FullscreenOptions&) override {
    if (!CanEnterFullscreenModeForTab(frame)) return;
    is_fullscreen_ = true;
    fullscreen_.Run(GetWebContents(), true);
  }
  void ExitFullscreenModeForTab(content::WebContents* page) override {
    if (!is_fullscreen_) return;
    is_fullscreen_ = false;
    fullscreen_.Run(page, false);
  }
  bool IsFullscreenForTabOrPending(const content::WebContents*) override { return is_fullscreen_; }
  // Ctrl+scroll over the page.
  void ContentsZoomChange(bool zoom_in) override { ZoomPage(GetWebContents(), zoom_in ? 1 : -1); }
  blink::mojom::DisplayMode GetDisplayMode(const content::WebContents*) override {
    return is_fullscreen_ ? blink::mojom::DisplayMode::kFullscreen : blink::mojom::DisplayMode::kBrowser;
  }
  content::FullscreenState GetFullscreenState(const content::WebContents*) const override {
    return {.target_mode = is_fullscreen_ ? content::FullscreenMode::kContent : content::FullscreenMode::kWindowed};
  }
  void LoadingStateChanged(content::WebContents*, bool) override { changed_.Run(); }
  content::KeyboardEventProcessingResult PreHandleKeyboardEvent(
      content::WebContents* source,
      const input::NativeWebKeyboardEvent& event) override {
    ResetImagePreview();
    if (mouse_gesture_.active() && event.windows_key_code == ui::VKEY_ESCAPE) {
      mouse_gesture_.Cancel(); return content::KeyboardEventProcessingResult::HANDLED;
    }
    return handler_.Run(source, event);
  }

  bool PreHandleMouseEvent(content::WebContents* source, const blink::WebMouseEvent& event) override {
    using Type = blink::WebInputEvent::Type;
    const auto type = event.GetType();
    const auto point = event.PositionInWidget();
    const double now = base::TimeTicks::Now().since_origin().InSecondsF();
    constexpr int modifiers = blink::WebInputEvent::kAltKey | blink::WebInputEvent::kShiftKey |
        blink::WebInputEvent::kControlKey | blink::WebInputEvent::kMetaKey;
    if (type == Type::kMouseDown && event.button == blink::WebMouseEvent::Button::kRight &&
        (event.GetModifiers() & modifiers) == blink::WebInputEvent::kAltKey &&
        gestures_enabled_.Run() && !is_fullscreen_ && source->GetLastCommittedURL().SchemeIsHTTPOrHTTPS()) {
      interacted_.Run(this);
      mouse_gesture_.Begin(point.x(), point.y(), now);
      suppress_gesture_menu_until_ = base::TimeTicks::Now() + base::Seconds(3);
      return true;
    }
    if (type == Type::kMouseDown && event.button == blink::WebMouseEvent::Button::kRight) {
      // A fresh ordinary click also ends a drag whose release happened outside
      // this renderer. Never swallow that click's matching release or menu.
      mouse_gesture_.Cancel(); mouse_gesture_.End(point.x(), point.y(), now);
      suppress_gesture_menu_until_ = base::TimeTicks(); return false;
    }
    if (!mouse_gesture_.active()) return false;
    if (!gestures_enabled_.Run() || (event.GetModifiers() & modifiers) != blink::WebInputEvent::kAltKey)
      mouse_gesture_.Cancel();
    if (type == Type::kMouseUp && event.button == blink::WebMouseEvent::Button::kRight) {
      auto direction = mouse_gesture_.End(point.x(), point.y(), now);
      suppress_gesture_menu_until_ = base::TimeTicks::Now() + base::Milliseconds(300);
      if (!direction.empty()) gesture_.Run(source, std::move(direction));
      return true;
    }
    if (type == Type::kMouseMove) {
      if (!(event.GetModifiers() & blink::WebInputEvent::kRightButtonDown)) {
        mouse_gesture_.End(point.x(), point.y(), now); return false;
      }
      mouse_gesture_.Move(point.x(), point.y(), now); return true;
    }
    // A second button cancels; its own click still reaches the page.
    mouse_gesture_.Cancel(); return false;
  }
  content::WebContents* OpenURLFromTab(content::WebContents* source, const content::OpenURLParams& params,
      base::OnceCallback<void(content::NavigationHandle&)> callback) override {
    return open_.Run(source, params);
  }
  void RunFileChooser(content::RenderFrameHost* frame,
      scoped_refptr<content::FileSelectListener> listener,
      const blink::mojom::FileChooserParams& params) override {
    if (content::WebContents::FromRenderFrameHost(frame) != GetWebContents()) {
      listener->FileSelectionCanceled(); return;
    }
    RunPageFileChooser(frame, std::move(listener), params);
  }
  void EnumerateDirectory(content::WebContents* source,
      scoped_refptr<content::FileSelectListener> listener, const base::FilePath& path) override {
    if (source != GetWebContents()) { listener->FileSelectionCanceled(); return; }
    EnumeratePageDirectory(source, std::move(listener), path);
  }
  void RequestMediaAccessPermission(content::WebContents* source, const content::MediaStreamRequest& request,
      content::MediaResponseCallback callback) override {
    if (source != GetWebContents()) {
      std::move(callback).Run(blink::mojom::StreamDevicesSet(), blink::mojom::MediaStreamRequestResult::PERMISSION_DENIED, nullptr); return;
    }
    PageMediaAccess::Request(source, request, std::move(callback));
  }
  bool CheckMediaAccessPermission(content::RenderFrameHost* frame, const url::Origin& origin,
      blink::mojom::MediaStreamType type) override {
    return content::WebContents::FromRenderFrameHost(frame) == GetWebContents() && PageMediaAccess::Check(frame, origin, type);
  }
  void BeforeUnloadFired(bool) override {}
  content::JavaScriptDialogManager* GetJavaScriptDialogManager(content::WebContents* source) override {
    return PageDialogs::Attach(source);
  }
  void BeforeUnloadFired(content::WebContents* source, bool proceed, bool* fire_unload) override {
    auto* dialogs = PageDialogs::FromWebContents(source);
    *fire_unload = proceed && !(dialogs && dialogs->checking_before_unload());
  }
  bool HandleContextMenu(content::RenderFrameHost& frame, const content::ContextMenuParams& params) override {
    if (base::TimeTicks::Now() < suppress_gesture_menu_until_) {
      // A normal secondary click immediately restores the regular menu.
      return true;
    }
    // Editable fields retain Chromium's editing/spelling menu, which also
    // includes its policy-aware, frame-targeted Inspect Element action.
    if (params.is_editable || (!params.link_url.SchemeIsHTTPOrHTTPS() && !GetWebContents()->GetLastCommittedURL().SchemeIsHTTPOrHTTPS())) return false;
    menu_.Run(GetWebContents(), &frame, params);
    return true;
  }
  content::WebContents* AddNewContents(content::WebContents* source, std::unique_ptr<content::WebContents> page,
      const GURL& target_url, WindowOpenDisposition disposition, const blink::mojom::WindowFeatures&, bool user_gesture, bool* blocked) override {
    // target=_blank links can arrive as newly created contents. Require the
    // actual Shift-left-click on the hovered link so forms and script popups
    // keep their existing request semantics.
    if (user_gesture && disposition == WindowOpenDisposition::NEW_WINDOW &&
        target_url == hovered_link_ && !shift_link_click_.is_null() &&
        base::TimeTicks::Now() - shift_link_click_ < base::Seconds(1) && peek_.Run(source, target_url)) {
      shift_link_click_ = base::TimeTicks();
      if (blocked) *blocked = true;
      return nullptr;
    }
    if (!user_gesture) {
      auto* settings = HostContentSettingsMapFactory::GetForProfile(
          Profile::FromBrowserContext(source->GetBrowserContext()));
      const auto& url = source->GetLastCommittedURL();
      if (settings->GetContentSetting(url, url, ContentSettingsType::POPUPS) != CONTENT_SETTING_ALLOW) {
        if (blocked) *blocked = true;
        return nullptr;
      }
    }
    if (blocked) *blocked = false;
    return adopt_.Run(source, std::move(page), disposition == WindowOpenDisposition::NEW_BACKGROUND_TAB);
  }
  void WebContentsCreated(content::WebContents* source, const content::GlobalRenderFrameHostId&,
      const std::string&, const GURL&, content::WebContents* page) override { created_.Run(source, page); }
  KeyHandler handler_;
  OpenHandler open_;
  MenuHandler menu_;
  AdoptHandler adopt_;
  CreatedHandler created_;
  void TitleWasSet(content::NavigationEntry*) override { changed_.Run(); }
  // URL and back/forward availability feed the sidebar's address pill.
  void NavigationStateChanged(content::WebContents* source,
                              content::InvalidateTypes flags) override {
    mouse_gesture_.Cancel();
    views::WebView::NavigationStateChanged(source, flags);
    UpdateBlankBackground();
    changed_.Run();
  }
  void DidGetUserInteraction(const blink::WebInputEvent& event) override {
    // Renderer/native attachment can report focus during session restoration.
    // Only actual user interaction changes which pane owns browser commands.
    ResetImagePreview();
    shift_link_click_ = base::TimeTicks();
    if (event.GetType() == blink::WebInputEvent::Type::kMouseDown &&
        (event.GetModifiers() & (blink::WebInputEvent::kShiftKey | blink::WebInputEvent::kControlKey |
                                blink::WebInputEvent::kMetaKey | blink::WebInputEvent::kAltKey)) == blink::WebInputEvent::kShiftKey &&
        static_cast<const blink::WebMouseEvent&>(event).button == blink::WebMouseEvent::Button::kLeft)
      shift_link_click_ = base::TimeTicks::Now();
    interacted_.Run(this);
  }
  base::RepeatingClosure changed_;
  base::RepeatingCallback<void(views::WebView*)> interacted_;
  base::RepeatingCallback<void(content::WebContents*, bool)> fullscreen_;
  bool is_fullscreen_ = false;
  base::RepeatingCallback<bool()> image_previews_;
  PeekHandler peek_;
  base::RepeatingCallback<bool()> gestures_enabled_;
  base::RepeatingCallback<void(content::WebContents*, std::string)> gesture_;
  MouseGesture mouse_gesture_;
  base::TimeTicks suppress_gesture_menu_until_;
  std::unique_ptr<ImageLinkPreview> image_preview_;
  GURL hovered_link_;
  base::TimeTicks shift_link_click_;
};

BEGIN_METADATA(WorkspaceWebView)
END_METADATA
}  // namespace

// ui::BaseWindow over the Zitter widget for chrome.windows. Close is refused:
// the window holds every profile's spaces, and only the user closes it.
class ZitterBaseWindow final : public ui::BaseWindow {
 public:
  explicit ZitterBaseWindow(views::Widget& widget) : widget_(widget) {}
  bool IsActive() const override { return widget_->IsActive(); }
  bool IsMaximized() const override { return widget_->IsMaximized(); }
  bool IsMinimized() const override { return widget_->IsMinimized(); }
  bool IsFullscreen() const override { return widget_->IsFullscreen(); }
  gfx::NativeWindow GetNativeWindow() const override { return widget_->GetNativeWindow(); }
  gfx::Rect GetRestoredBounds() const override { return widget_->GetRestoredBounds(); }
  ui::mojom::WindowShowState GetRestoredState() const override {
    return widget_->IsMaximized() ? ui::mojom::WindowShowState::kMaximized : ui::mojom::WindowShowState::kNormal;
  }
  gfx::Rect GetBounds() const override { return widget_->GetWindowBoundsInScreen(); }
  void Show() override { widget_->IsVisible() ? widget_->Activate() : widget_->Show(); }
  void Hide() override {}
  bool IsVisible() const override { return widget_->IsVisible(); }
  void ShowInactive() override { if (!widget_->IsVisible()) widget_->ShowInactive(); }
  void Close() override {}
  void Activate() override { widget_->Activate(); }
  void Deactivate() override { widget_->Deactivate(); }
  void Maximize() override { widget_->Maximize(); }
  void Minimize() override { widget_->Minimize(); }
  void Restore() override { widget_->Restore(); }
  void SetBounds(const gfx::Rect& bounds) override { widget_->SetBounds(bounds); }
  void FlashFrame(bool flash) override { widget_->FlashFrame(flash); }
  ui::ZOrderLevel GetZOrderLevel() const override { return widget_->GetZOrderLevel(); }
  void SetZOrderLevel(ui::ZOrderLevel level) override { widget_->SetZOrderLevel(level); }
 private:
  const raw_ref<views::Widget> widget_;
};

// chrome.windows sees each profile's Zitter tabs as one window of type
// "normal", backed by the Zitter widget rather than a Browser (as Document
// PiP windows are).
class ZitterExtensionWindow final : public extensions::WindowController {
 public:
  using Tabs = extensions::ExtensionTabUtil::EmbedderTabs;
  static std::unique_ptr<ZitterExtensionWindow> Create(views::Widget& widget, Profile* profile, Tabs* tabs) {
    auto base_window = std::make_unique<ZitterBaseWindow>(widget);
    return base::WrapUnique(new ZitterExtensionWindow(std::move(base_window), profile, tabs));
  }
  ~ZitterExtensionWindow() override { extensions::WindowControllerList::GetInstance()->RemoveExtensionWindow(this); }
  SessionID session_id() const { return session_id_; }
  int GetWindowId() const override { return session_id_.id(); }
  std::string GetWindowTypeText() const override { return "normal"; }
  void SetFullscreenMode(bool, const GURL&) const override {}
  content::WebContents* GetActiveTab() const override { return tabs_->GetShownTab(profile()); }
  int GetTabCount() const override { return static_cast<int>(tabs_->GetTabs(profile()).size()); }
  content::WebContents* GetWebContentsAt(int i) const override {
    const auto tabs = tabs_->GetTabs(profile());
    return i >= 0 && static_cast<size_t>(i) < tabs.size() ? tabs[i] : nullptr;
  }
  bool IsVisibleToTabsAPIForExtension(const extensions::Extension* extension, bool) const override {
    return !extension || !extension->is_platform_app();
  }
  base::DictValue CreateWindowValueForExtension(const extensions::Extension* extension,
                                                PopulateTabBehavior populate_tab_behavior,
                                                extensions::mojom::ContextType context) const override {
    const gfx::Rect bounds = base_window_->IsMinimized() ? base_window_->GetRestoredBounds() : base_window_->GetBounds();
    base::DictValue dict;
    dict.Set("id", GetWindowId());
    dict.Set("focused", base_window_->IsActive());
    dict.Set("top", bounds.y());
    dict.Set("left", bounds.x());
    dict.Set("width", bounds.width());
    dict.Set("height", bounds.height());
    dict.Set("type", GetWindowTypeText());
    dict.Set("state", base_window_->IsMinimized() ? "minimized" : base_window_->IsMaximized() ? "maximized" : "normal");
    dict.Set("incognito", profile()->IsOffTheRecord());
    dict.Set("alwaysOnTop", false);
    if (populate_tab_behavior == kPopulateTabs) dict.Set(extensions::ExtensionTabUtil::kTabsKey, CreateTabList(extension, context));
    return dict;
  }
  base::ListValue CreateTabList(const extensions::Extension* extension, extensions::mojom::ContextType context) const override {
    base::ListValue list;
    const auto tabs = tabs_->GetTabs(profile());
    for (size_t i = 0; i < tabs.size(); ++i) {
      list.Append(extensions::ExtensionTabUtil::CreateTabObject(
          tabs[i], extensions::ExtensionTabUtil::GetScrubTabBehavior(extension, context, tabs[i]), extension, nullptr,
          static_cast<int>(i)).ToValue());
    }
    return list;
  }
  bool OpenOptionsPage(const extensions::Extension*, const GURL& url, bool) override {
    return tabs_->OpenTab(profile(), url, true) != nullptr;
  }

 private:
  ZitterExtensionWindow(std::unique_ptr<ZitterBaseWindow> base_window, Profile* profile, Tabs* tabs)
      : extensions::WindowController(base_window.get(), profile), base_window_(std::move(base_window)), tabs_(tabs) {
    extensions::WindowControllerList::GetInstance()->AddExtensionWindow(this);
  }
  const std::unique_ptr<ZitterBaseWindow> base_window_;
  raw_ptr<Tabs> tabs_;
  const SessionID session_id_ = SessionID::NewUnique();
};

// The tabs API sees each profile's Zitter tabs as one window: every space of
// that profile, in sidebar order, with the page on screen as the active tab.
class WorkspaceWindow::ExtensionTabs final : public extensions::ExtensionTabUtil::EmbedderTabs {
 public:
  explicit ExtensionTabs(WorkspaceWindow* window) : window_(window) {
    if (!window_->is_private()) extensions::ExtensionTabUtil::SetEmbedderTabs(this);
  }
  ~ExtensionTabs() override {
    if (extensions::ExtensionTabUtil::GetEmbedderTabs() == this) extensions::ExtensionTabUtil::SetEmbedderTabs(nullptr);
  }
  // One window per profile in use; each page knows its window id.
  void Sync() {
    if (window_->is_private()) return;
    std::set<Profile*> profiles;
    for (const auto& host : window_->hosts_) if (host.profile) profiles.insert(host.profile);
    std::erase_if(windows_, [&](const auto& entry) { return !profiles.contains(entry.first); });
    for (auto* profile : profiles) {
      auto& window = windows_[profile];
      if (!window) window = ZitterExtensionWindow::Create(*window_->widget_, profile, this);
      for (auto* page : GetTabs(profile))
        if (auto* helper = sessions::SessionTabHelper::FromWebContents(page);
            helper && helper->window_id() != window->session_id())
          helper->SetWindowID(window->session_id());
    }
  }
  std::vector<content::WebContents*> GetTabs(Profile* profile) override {
    std::vector<content::WebContents*> tabs;
    for (const auto& host : window_->hosts_) {
      if (host.profile != profile) continue;
      for (const auto& tab : host.tabs) if (auto* contents = tab->web_contents()) tabs.push_back(contents);
    }
    return tabs;
  }
  content::WebContents* GetShownTab(Profile* profile) override {
    content::WebContents* first = nullptr;
    for (const auto& host : window_->hosts_) {
      if (host.profile != profile || !host.view || !host.view->web_contents()) continue;
      if (host.workspace_id == window_->active_id()) return host.view->web_contents();
      if (!first) first = host.view->web_contents();
    }
    return first;
  }
  extensions::WindowController* GetWindow(Profile* profile) override {
    const auto found = windows_.find(profile);
    return found == windows_.end() ? nullptr : found->second.get();
  }
  content::WebContents* OpenTab(Profile* profile, const GURL& url, bool active) override {
    return window_->OpenProfileTab(profile, url, active);
  }
  bool ActivateTab(content::WebContents* contents) override {
    const auto [workspace, index] = Find(contents);
    if (workspace.empty() || !window_->Select(workspace)) return false;
    window_->SelectTab(index);
    return true;
  }
  bool CloseTab(content::WebContents* contents) override {
    const auto [workspace, index] = Find(contents);
    if (workspace.empty() || window_->tab_count(workspace) < 2) return false;
    window_->CloseWorkspaceTab(workspace, index);
    return true;
  }
  // `index` counts across the profile's spaces in sidebar order (-1: the
  // end). Within the space on screen the sidebar reorders; into another space
  // of the profile the tab moves there.
  bool MoveTab(content::WebContents* contents, int index) override {
    const auto [workspace, from] = Find(contents);
    if (workspace.empty()) return false;
    auto* profile = Profile::FromBrowserContext(contents->GetBrowserContext());
    std::string target;
    size_t position = 0, offset = 0;
    for (const auto& host : window_->hosts_) {
      if (host.profile != profile) continue;
      target = host.workspace_id;
      position = host.tabs.size();
      if (index >= 0 && static_cast<size_t>(index) < offset + host.tabs.size()) {
        position = static_cast<size_t>(index) - offset;
        break;
      }
      offset += host.tabs.size();
    }
    if (target.empty()) return false;
    if (target != workspace) return window_->MoveTabToSpace(contents, target, position);
    if (workspace != window_->active_id()) return false;
    const size_t to = std::min(position, window_->tab_count(workspace) - 1);
    return window_->MoveTab(from, to > from ? to + 1 : to);
  }
  // Split View: the pair shows side by side in its space.
  int SplitTabs(content::WebContents* first, content::WebContents* second) override {
    const auto [workspace, a] = Find(first);
    const auto [other, b] = Find(second);
    if (workspace.empty() || workspace != other || a == b || GetSplitId(first) >= 0 || GetSplitId(second) >= 0 ||
        !window_->Select(workspace))
      return -1;
    window_->SelectTab(a);
    if (!window_->SplitTab(b)) return -1;
    return GetSplitId(first);
  }
  bool Unsplit(Profile* profile, int split_view_id) override {
    for (const auto& host : window_->hosts_) {
      if (host.profile != profile || host.split.empty() || SplitId(host.workspace_id) != split_view_id) continue;
      const auto kept = std::find(host.tabs.begin(), host.tabs.end(), host.split.front()) - host.tabs.begin();
      const std::string workspace = host.workspace_id;
      if (!window_->Select(workspace)) return false;
      window_->Unsplit(kept);
      return true;
    }
    return false;
  }
  int GetSplitId(content::WebContents* contents) override {
    for (const auto& host : window_->hosts_)
      for (const auto& member : host.split)
        if (member->web_contents() == contents) return SplitId(host.workspace_id);
    return -1;
  }

 private:
  std::pair<std::string, size_t> Find(content::WebContents* contents) const {
    for (const auto& host : window_->hosts_)
      for (size_t i = 0; i < host.tabs.size(); ++i)
        if (host.tabs[i]->web_contents() == contents) return {host.workspace_id, i};
    return {};
  }
  // One split per space: its id is the space's, numbered apart from Chrome's.
  int SplitId(const std::string& workspace) {
    auto [entry, added] = split_ids_.try_emplace(workspace, 0);
    if (added) entry->second = 1'000'000 + static_cast<int>(split_ids_.size());
    return entry->second;
  }
  raw_ptr<WorkspaceWindow> window_;
  std::map<Profile*, std::unique_ptr<ZitterExtensionWindow>> windows_;
  std::map<std::string, int> split_ids_;
};

WorkspaceWindow::WorkspaceWindow(Profile* work, Profile* personal)
    : WorkspaceWindow(work->GetPath().DirName(), false) {
  CHECK(personal);
  CHECK_NE(work, personal);
  CHECK_EQ(work->GetPath().DirName(), personal->GetPath().DirName());
}

WorkspaceWindow::WorkspaceWindow(base::FilePath data_directory, bool offer_import, Profile* private_profile)
    : controller_(model_, *this), private_profile_(private_profile), data_directory_(std::move(data_directory)), offer_import_(offer_import && !private_profile) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!is_private()) MarkStartup("workspace_begin");
  // The UI language is fixed for the run, before any text is built.
  ApplyUiLanguage(g_browser_process->GetApplicationLocale());
  configuration_path_ = data_directory_.AppendASCII("ZitterWorkspaces.json");
  clock_ = base::DefaultClock::GetInstance();
  timeline_ = std::make_unique<TimelineStore>();
  config_runner_ = base::ThreadPool::CreateSequencedTaskRunner(
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE, base::TaskShutdownBehavior::BLOCK_SHUTDOWN});
  CHECK(!private_profile_ || private_profile_->IsOffTheRecord());
  WorkspaceShell::Callbacks callbacks;
  callbacks.private_mode = is_private();
  callbacks.preview_import = base::BindRepeating([](WorkspaceWindow* window, BrowserImportOptions options) {
    window->PreviewBrowserImport(options);
  }, base::Unretained(this));
  callbacks.invalidate_import = base::BindRepeating(&WorkspaceWindow::InvalidateImportPreview, base::Unretained(this));
  callbacks.apply_import = base::BindRepeating(&WorkspaceWindow::ApplyArcImport, base::Unretained(this));
  callbacks.dismiss_import = base::BindRepeating(&WorkspaceWindow::DismissImport, base::Unretained(this));
  callbacks.open_pin = base::BindRepeating(&WorkspaceWindow::OpenPinnedItem, base::Unretained(this));
  callbacks.create_profile = base::BindRepeating(&WorkspaceWindow::CreateProfile, base::Unretained(this));
  callbacks.rename_profile = base::BindRepeating(&WorkspaceWindow::RenameProfile, base::Unretained(this));
  callbacks.assign_profile = base::BindRepeating(&WorkspaceWindow::AssignProfile, base::Unretained(this));
  callbacks.remove_profile = base::BindRepeating(&WorkspaceWindow::RemoveProfile, base::Unretained(this));
  callbacks.add_workspace = base::BindRepeating(&WorkspaceWindow::AddWorkspace, base::Unretained(this));
  callbacks.edit_workspace = base::BindRepeating(&WorkspaceWindow::EditWorkspace, base::Unretained(this));
  callbacks.remove_workspace = base::BindRepeating(&WorkspaceWindow::RemoveWorkspace, base::Unretained(this));
  callbacks.select_workspace = base::BindRepeating(
      [](WorkspaceWindow* window, std::string id) { window->Select(id); }, base::Unretained(this));
  callbacks.navigate = base::BindRepeating(&WorkspaceWindow::NavigateFromBar, base::Unretained(this));
  callbacks.select_tab = base::BindRepeating(&WorkspaceWindow::SelectTab, base::Unretained(this));
  callbacks.close_tab = base::BindRepeating(&WorkspaceWindow::CloseTab, base::Unretained(this));
  callbacks.search = base::BindRepeating(&WorkspaceWindow::SearchPalette, base::Unretained(this));
  callbacks.commands = base::BindRepeating(&WorkspaceWindow::CommandResults, base::Unretained(this));
  callbacks.activate_result = base::BindRepeating(&WorkspaceWindow::ActivatePaletteResult, base::Unretained(this));
  callbacks.download_directory = base::BindRepeating(&WorkspaceWindow::DownloadDirectory, base::Unretained(this));
  callbacks.choose_download_directory = base::BindRepeating(&WorkspaceWindow::ChooseDownloadDirectory, base::Unretained(this));
  callbacks.write_text_preference = base::BindRepeating(&WorkspaceWindow::WriteTextPreference, base::Unretained(this));
  callbacks.read_site_preferences = base::BindRepeating(&WorkspaceWindow::ReadSitePreferences, base::Unretained(this));
  callbacks.write_site_preference = base::BindRepeating(&WorkspaceWindow::WriteSitePreference, base::Unretained(this));
  callbacks.remove_site_preference = base::BindRepeating(&WorkspaceWindow::RemoveSitePreference, base::Unretained(this));
  callbacks.rename_folder = base::BindRepeating(&WorkspaceWindow::RenamePinnedFolder, base::Unretained(this));
  callbacks.delete_folder = base::BindRepeating(&WorkspaceWindow::RemovePinnedFolder, base::Unretained(this));
  callbacks.restore_pins = base::BindRepeating(&WorkspaceWindow::RestorePins, base::Unretained(this));
  callbacks.restore_workspace = base::BindRepeating(&WorkspaceWindow::RestoreRemovedWorkspace, base::Unretained(this));
  callbacks.create_folder = base::BindRepeating(&WorkspaceWindow::CreatePinnedFolder, base::Unretained(this));
  if (!is_private()) {
    callbacks.configure_github_folder = base::BindRepeating(&WorkspaceWindow::ConfigureGitHubFolder, base::Unretained(this));
    callbacks.refresh_github_folder = base::BindRepeating(&WorkspaceWindow::RefreshGitHubFolder, base::Unretained(this));
  }
  callbacks.clear_browsing_data = base::BindRepeating(&WorkspaceWindow::RequestClearBrowsingData, base::Unretained(this));
  callbacks.clear_browsing_data_now = base::BindRepeating([](WorkspaceWindow* window, BrowsingDataSelection selection) {
    if (window->model_.active()) window->ClearBrowsingData(window->model_.active()->profile_id, selection);
  }, base::Unretained(this));
  callbacks.site_panel = base::BindRepeating(&WorkspaceWindow::ShowSitePanel, base::Unretained(this));
  callbacks.system_settings = base::BindRepeating(&WorkspaceWindow::OpenSystemSettings, base::Unretained(this));
  callbacks.read_preferences = base::BindRepeating(&WorkspaceWindow::ReadPreferences, base::Unretained(this));
  callbacks.write_preference = base::BindRepeating(&WorkspaceWindow::WritePreference, base::Unretained(this));
  callbacks.read_assistant = base::BindRepeating(&WorkspaceWindow::AssistantSettingsForShell, base::Unretained(this));
  callbacks.write_assistant = base::BindRepeating(&WorkspaceWindow::WriteAssistantSettings, base::Unretained(this));
  callbacks.read_ad_blocking = base::BindRepeating(&WorkspaceWindow::ReadAdBlocking, base::Unretained(this));
  callbacks.write_ad_blocking = base::BindRepeating(&WorkspaceWindow::WriteAdBlocking, base::Unretained(this));
  callbacks.open_ad_blocking_rules = base::BindRepeating(&WorkspaceWindow::OpenAdBlockingRules, base::Unretained(this));
  callbacks.operation_pending = base::BindRepeating(&WorkspaceWindow::lifecycle_pending, base::Unretained(this));
  callbacks.timeline = base::BindRepeating([](WorkspaceWindow* window) { return &window->timeline(); }, base::Unretained(this));
  callbacks.timeline_now = base::BindRepeating(&WorkspaceWindow::Now, base::Unretained(this));
  callbacks.record_timeline = base::BindRepeating(&WorkspaceWindow::RecordActiveTimeline, base::Unretained(this));
  callbacks.restore_timeline = base::BindRepeating(&WorkspaceWindow::RestoreTimeline, base::Unretained(this));
  callbacks.timeline_settings = base::BindRepeating(&WorkspaceWindow::SetTimelineSettings, base::Unretained(this));
  callbacks.clear_timeline = base::BindRepeating(&WorkspaceWindow::ClearTimeline, base::Unretained(this));
  callbacks.run_shortcut = base::BindRepeating(&WorkspaceWindow::RunShortcut, base::Unretained(this));
  callbacks.downloads = base::BindRepeating(&WorkspaceWindow::ShowDownloads, base::Unretained(this));
  callbacks.passwords = base::BindRepeating(&WorkspaceWindow::ShowPasswords, base::Unretained(this));
  callbacks.close_peek = base::BindRepeating(&WorkspaceWindow::ClosePeek, base::Unretained(this));
  callbacks.promote_peek = base::BindRepeating(&WorkspaceWindow::PromotePeek, base::Unretained(this));
  callbacks.peek = base::BindRepeating(&WorkspaceWindow::OpenPeek, base::Unretained(this));
  callbacks.set_link_rule = base::BindRepeating(&WorkspaceWindow::SetLinkRule, base::Unretained(this));
  callbacks.open_link = base::BindRepeating(&WorkspaceWindow::OpenLink, base::Unretained(this));
  callbacks.toggle_split = base::BindRepeating(&WorkspaceWindow::ToggleSplit, base::Unretained(this));
  callbacks.focus_pane = base::BindRepeating(&WorkspaceWindow::FocusPane, base::Unretained(this));
  callbacks.split_ratio = base::BindRepeating(&WorkspaceWindow::SetDividerRatio, base::Unretained(this));
  callbacks.split_tab = base::BindRepeating(&WorkspaceWindow::SplitTab, base::Unretained(this));
  callbacks.unsplit = base::BindRepeating(&WorkspaceWindow::Unsplit, base::Unretained(this));
  callbacks.leave_split = base::BindRepeating(&WorkspaceWindow::RemoveFromSplit, base::Unretained(this));
  callbacks.close_split_tabs = base::BindRepeating(&WorkspaceWindow::CloseSplitTabs, base::Unretained(this));
  callbacks.move_split_pane = base::BindRepeating(&WorkspaceWindow::MoveSplitPane, base::Unretained(this));
  callbacks.duplicate_pin = base::BindRepeating(&WorkspaceWindow::DuplicatePin, base::Unretained(this));
  callbacks.duplicate_tab = base::BindRepeating(&WorkspaceWindow::DuplicateTab, base::Unretained(this));
  callbacks.move_tab_to_workspace = base::BindRepeating(&WorkspaceWindow::MoveSidebarTab, base::Unretained(this));
  callbacks.move_tab = base::BindRepeating(&WorkspaceWindow::MoveTab, base::Unretained(this));
  callbacks.pin_tab = base::BindRepeating(&WorkspaceWindow::PinTab, base::Unretained(this));
  callbacks.unpin = base::BindRepeating(&WorkspaceWindow::UnpinItem, base::Unretained(this));
  callbacks.update_pinned_url = base::BindRepeating(&WorkspaceWindow::UpdatePinnedUrl, base::Unretained(this));
  callbacks.reset_pinned_url = base::BindRepeating(&WorkspaceWindow::ResetPinnedUrl, base::Unretained(this));
  callbacks.move_pin = base::BindRepeating(&WorkspaceWindow::MovePin, base::Unretained(this));
  callbacks.swap_split = base::BindRepeating(&WorkspaceWindow::SwapSplit, base::Unretained(this));
  callbacks.split_vertical = base::BindRepeating(&WorkspaceWindow::SetSplitVertical, base::Unretained(this));
  callbacks.split_pin = base::BindRepeating([](WorkspaceWindow* window, std::string id) {
    content::WebContents* previous = nullptr;
    for (const auto& host : window->hosts_) if (host.workspace_id == window->active_id() && host.view) previous = host.view->web_contents();
    window->OpenPinnedItem(std::move(id));
    for (auto& host : window->hosts_) {
      if (host.workspace_id != window->active_id()) continue;
      const size_t index = host.active_tab;
      // Opening the pin may have shown it alone; re-anchor on the page that was
      // focused so a shown group gains the pin as one more pane.
      for (const auto& tab : host.tabs) if (previous && tab->web_contents() == previous) host.view = tab;
      window->SplitTab(index);
    }
  }, base::Unretained(this));
  callbacks.page_command = base::BindRepeating(&WorkspaceWindow::RunPageCommand, base::Unretained(this));
  callbacks.clean_address_copies = base::BindRepeating(&WorkspaceWindow::clean_address_copies, base::Unretained(this));
  callbacks.tab_menu_url = base::BindRepeating(&WorkspaceWindow::GetTabMenuUrl, base::Unretained(this));
  shell_ = std::make_unique<WorkspaceShell>(
      data_directory_.AppendASCII("ZitterAppearance.json"), std::move(callbacks));
  auto root = shell_->Build();
  if (!is_private()) MarkStartup("shell_built");
  // Chrome's post-install bubble opens (and needs) a stock browser window;
  // Zitter confirms a new extension with the puzzle's tremor instead.
  extension_install_ui_ = std::make_unique<base::AutoReset<bool>>(
      ExtensionInstallUI::disable_ui_for_tests(true));
  extension_tabs_ = std::make_unique<ExtensionTabs>(this);
  // Design: Extensions. The bar follows the profile of the page on screen.
  ExtensionsBar::Callbacks extension_callbacks;
  extension_callbacks.private_mode = is_private();
  extension_callbacks.open_page = base::BindRepeating([](WorkspaceWindow* window, Profile* profile, const GURL& url) {
    window->OpenExtensionPage(profile, url);
  }, base::Unretained(this));
  extension_callbacks.workspace_color = base::BindRepeating([](WorkspaceWindow* window) {
    return window->shell_->active_workspace_color();
  }, base::Unretained(this));
  extension_callbacks.open_privacy_settings = base::BindRepeating([](WorkspaceWindow* window) {
    window->shell_->OpenPrivacySettings();
  }, base::Unretained(this));
  extension_callbacks.toggle_assistant = base::BindRepeating([](WorkspaceWindow* window) {
    window->NewAssistantConversation();
  }, base::Unretained(this));
  extension_callbacks.open_ad_blocking_settings = base::BindRepeating([](WorkspaceWindow* window) {
    window->shell_->OpenAdBlockingSettings();
  }, base::Unretained(this));
  extension_callbacks.toggle_assistant_panel = base::BindRepeating(&WorkspaceWindow::ToggleAssistantFromBar, base::Unretained(this));
  extension_callbacks.assistant_action = base::BindRepeating(&WorkspaceWindow::RunAssistantPageAction, base::Unretained(this));
  extension_callbacks.assistant_available = base::BindRepeating([](WorkspaceWindow* window) {
    return window->assistant_settings_.enabled;
  }, base::Unretained(this));
  extension_callbacks.night_shift_shown = base::BindRepeating([](WorkspaceWindow* window, Profile* profile) {
    return !window->night_shift_hidden_.contains(window->ProfileIdOf(profile));
  }, base::Unretained(this));
  extension_callbacks.set_night_shift_shown = base::BindRepeating([](WorkspaceWindow* window, Profile* profile, bool shown) {
    const auto id = window->ProfileIdOf(profile);
    if (id.empty() || !window->configuration_ready_) return;
    if (shown) window->night_shift_hidden_.erase(id);
    else window->night_shift_hidden_.insert(id);
    window->SaveConfiguration();
  }, base::Unretained(this));
  extension_callbacks.night_shift_state =
      base::BindRepeating(&WorkspaceWindow::NightShiftSettingsState, base::Unretained(this));
  extension_callbacks.fix_night_shift =
      base::BindRepeating(&WorkspaceWindow::PrepareNightShiftRepair, weak_factory_.GetWeakPtr());
  extension_callbacks.block_with_assistant = base::BindRepeating([](WorkspaceWindow* window) {
    // Rules that stopped working come first: the assistant replaces them.
    auto* bar = window->extensions_bar_.get();
    const auto stale = bar && bar->page() ? ContentBlocking::StaleRules(bar->profile(), bar->page()->GetLastCommittedURL())
                                          : std::vector<std::string>();
    if (stale.empty()) { window->AskAssistant(T(u"Zablokuj reklamy na tej stronie, których nie blokują listy filtrów.")); return; }
    window->AskAssistant(TF(u"Moje reguły blokowania na tej stronie od kilku wizyt nic nie ukrywają, reklamy pewnie się zmieniły. Znajdź te reklamy ponownie i zapisz nowe reguły, zastępując nimi stare (replaces): {}",
                            {base::UTF8ToUTF16(base::JoinString(stale, "\n"))}));
  }, base::Unretained(this));
  extension_callbacks.diagnose_blocking_with_assistant = base::BindRepeating([](WorkspaceWindow* window) {
    if (!window->assistant_settings_.enabled) return;
    window->ShowAssistant();
    if (window->assistant_) {
      window->assistant_->PreparePageAction(
          T(u"Sprawdź, czy ta strona wykrywa blokowanie reklam. Użyj adblock_diagnose na bieżącej karcie. Jeśli strona wykrywa blokowanie, przetestuj reguły tylko dla tej witryny z adblock_add_rules (check: true). Zostaw blokowanie reklam i śledzenia włączone. Poproś o zapis tylko reguł, które działają."),
          /*needs_actions=*/true, /*needs_web=*/false);
    }
  }, base::Unretained(this));
  ad_blocking_changed_ = ContentBlocking::Get().Observe(base::BindRepeating([](WorkspaceWindow* window) {
    if (window->shell_) window->shell_->RefreshAdBlockingPreferences();
    if (window->ad_blocking_rules_panel_) window->ad_blocking_rules_panel_->Refresh();
  }, base::Unretained(this)));
  extensions_bar_ = static_cast<ExtensionsBar*>(shell_->SetHeaderTrailing(
      std::make_unique<ExtensionsBar>(std::move(extension_callbacks))));
  delegate_ = std::make_unique<views::WidgetDelegate>();
  const bool dev = base::CommandLine::ForCurrentProcess()->HasSwitch(windows::kDevChannelSwitch);
  delegate_->SetTitle(dev ? (is_private() ? T(u"Zitter Dev — tryb prywatny") : u"Zitter Dev")
                          : (is_private() ? T(u"Zitter — tryb prywatny") : u"Zitter"));
  delegate_->SetHasWindowSizeControls(true);
  delegate_->SetContentsView(std::move(root));
  delegate_->SetClientViewFactory(WorkspaceShell::ClientViewFactory());
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_WINDOW);
  params.delegate = delegate_.get();
  params.bounds = kDefaultWindowBounds;
  widget_ = std::make_unique<views::Widget>();
#if BUILDFLAG(IS_MAC)
  params.native_widget = CreateWorkspaceNativeWidget(widget_.get());
#elif BUILDFLAG(IS_WIN)
  ConfigureWorkspaceNativeWidget(widget_.get(), params);
  // The titlebar-less window still has an icon: taskbar, Alt+Tab, system menu.
  // Zitter's icon in chrome.dll's resource slot, linked into zitter.exe.
  if (auto icons = IconUtil::CreateImageFamilyFromIconResource(::GetModuleHandle(nullptr), IDR_MAINFRAME)) {
    if (const auto* small = icons->GetBest(32, 32)) delegate_->SetIcon(ui::ImageModel::FromImage(*small));
    if (const auto* large = icons->GetBest(256, 256)) delegate_->SetAppIcon(ui::ImageModel::FromImage(*large));
  }
#endif
  widget_->Init(std::move(params));
  if (!is_private()) MarkStartup("widget_initialized");
#if BUILDFLAG(IS_WIN)
  if (!is_private()) SetWorkspaceCommandHandler(widget_.get(), base::BindRepeating(
      [](base::WeakPtr<WorkspaceWindow> window, std::string command) { if (window) window->RunExternalCommand(std::move(command)); },
      weak_factory_.GetWeakPtr()), data_directory_);
#endif
  shell_->Attach(widget_.get(), this);
  if (!is_private()) MarkStartup("shell_attached");
  if (is_private()) {
    widget_->Show();
    profiles_.push_back({{"private-profile", base::UTF16ToUTF8(T(u"Tryb prywatny")), "", ""}, nullptr});
    model_.Add({"private", "private-profile", base::UTF16ToUTF8(T(u"Tryb prywatny"))});
    restored_active_ = "private";
    onboarding_complete_ = true;
    assistant_settings_.enabled = false;
    shell_->SetAssistantAllowed(false);
    FinishConfiguration();
    return;
  }
  widget_->AddObserver(this);
  WindowPlacementRunner().PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce([](base::FilePath path) -> std::optional<base::DictValue> {
        std::string json;
        if (!base::ReadFileToStringWithMaxSize(path, &json, 4096)) return std::nullopt;
        return base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
      }, data_directory_.AppendASCII(kWindowPlacementFile)),
      base::BindOnce(&WorkspaceWindow::ShowRestoredWindow, weak_factory_.GetWeakPtr()));
}

WorkspaceWindow::Host::Host() = default;
WorkspaceWindow::Host::~Host() = default;
WorkspaceWindow::Host::Host(Host&&) noexcept = default;
WorkspaceWindow::Host& WorkspaceWindow::Host::operator=(Host&&) noexcept = default;

WorkspaceWindow::~WorkspaceWindow() {
  if (!is_private()) {
    SaveWindowPlacement();
    widget_->RemoveObserver(this);
  }
  if (assistant_) assistant_->Stop();
  assistant_ = nullptr;
  assistant_targets_.clear();
  external_command_timer_.Stop();
  downloads_.reset();
  if (clear_data_dialog_) clear_data_dialog_->Close();
  if (data_remover_) data_remover_->RemoveObserver(this);
  data_remover_ = nullptr;
  data_removal_lease_.reset();
  if (download_directory_dialog_) download_directory_dialog_->ListenerDestroyed();
  SaveSession();
  closing_ = true;
  history_tasks_.TryCancelAll();
  weak_factory_.InvalidateWeakPtrs();
  ClosePeek();
  extension_tabs_.reset();
  extensions_bar_->SetContext(nullptr, nullptr);
  extensions_bar_ = nullptr;
  shell_->Detach();
  widget_->GetFocusManager()->UnregisterAccelerators(this);
  for (auto& host : hosts_) {
    host.view = nullptr;
    host.split.clear(); host.deferred_urls.clear(); host.deferred_titles.clear(); host.popup_tabs.clear();
    host.tabs.clear();
    host.pin_tabs.clear();
    host.profile = nullptr;
  }
  widget_.reset();
  delegate_.reset();
  shell_.reset();
  for (const auto& entry : profiles_) {
    if (entry.lease) entry.lease->profile()->GetDefaultStoragePartition()->Flush();
  }
  profiles_.clear();
}

void WorkspaceWindow::ShowRestoredWindow(std::optional<base::DictValue> placement) {
  auto bounds = kDefaultWindowBounds;
  if (placement) {
    const auto x = placement->FindInt("x");
    const auto y = placement->FindInt("y");
    const auto width = placement->FindInt("width");
    const auto height = placement->FindInt("height");
    if (x && y && width && height && *width > 0 && *height > 0)
      bounds = gfx::Rect(*x, *y, *width, *height);
  }
  const auto* screen = display::Screen::Get();
  auto work_area = screen->GetDisplayMatching(bounds).work_area();
  if (work_area.IsEmpty()) work_area = screen->GetPrimaryDisplay().work_area();
  if (!work_area.IsEmpty()) bounds.AdjustToFit(work_area);

  const bool maximized = placement && placement->FindBool("maximized").value_or(false);
  const bool fullscreen = placement && placement->FindBool("fullscreen").value_or(false);
#if BUILDFLAG(IS_WIN)
  const float initial_scale =
      screen->GetDisplayNearestWindow(widget_->GetNativeWindow()).device_scale_factor();
#endif
  widget_->SetBounds(bounds);
#if BUILDFLAG(IS_WIN)
  // Crossing to a different DPI sends WM_DPICHANGED, which may resize the
  // window again. Apply the saved DIP bounds once the new scale is active.
  if (screen->GetDisplayNearestWindow(widget_->GetNativeWindow()).device_scale_factor() !=
      initial_scale) {
    widget_->SetBounds(bounds);
  }
#endif
  windowed_bounds_ = bounds;
  widget_->Show();
  if (maximized) widget_->Maximize();
  if (fullscreen) widget_->SetFullscreen(true);
  window_maximized_ = maximized;
  window_placement_ready_ = true;
  RememberWindowPlacement();
  MarkStartup("widget_shown");

  config_runner_->PostTaskAndReplyWithResult(FROM_HERE,
      base::BindOnce([](base::FilePath path) {
        std::string json;
        base::ReadFileToString(path, &json);
        std::string session;
        base::ReadFileToString(path.DirName().AppendASCII("ZitterSession.json"), &session);
        std::string timeline;
        base::ReadFileToStringWithMaxSize(path.DirName().AppendASCII("ZitterTimeline.jsonl"), &timeline, 64 * 1024 * 1024);
        return std::array<std::string, 3>{std::move(json), std::move(session), std::move(timeline)};
      }, configuration_path_),
      base::BindOnce([](base::WeakPtr<WorkspaceWindow> window, std::array<std::string, 3> data) {
        if (!window) return;
        window->session_json_ = std::move(data[1]);
        MarkStartup("workspace_files_read");
        window->LoadTimeline(std::move(data[2]));
        MarkStartup("timeline_loaded");
        window->RestoreConfiguration(std::move(data[0]));
      }, weak_factory_.GetWeakPtr()));
}

void WorkspaceWindow::RememberWindowPlacement() {
  if (!window_placement_ready_ || widget_->IsMinimized()) return;
  window_fullscreen_ = widget_->IsFullscreen();
  if (window_fullscreen_) return;

  window_maximized_ = widget_->IsMaximized();
  // Windows exposes the normal rectangle even while maximized. On Mac the
  // restored rectangle of a zoomed window is its current frame.
#if BUILDFLAG(IS_MAC)
  if (window_maximized_) return;
#endif
  const auto bounds = widget_->GetRestoredBounds();
  if (!bounds.IsEmpty()) windowed_bounds_ = bounds;
}

void WorkspaceWindow::OnWidgetBoundsChanged(views::Widget*, const gfx::Rect&) {
  RememberWindowPlacement();
}

void WorkspaceWindow::OnWidgetShowStateChanged(views::Widget*) {
  RememberWindowPlacement();
}

void WorkspaceWindow::SaveWindowPlacement() {
  if (!window_placement_ready_) return;
  RememberWindowPlacement();
  base::DictValue placement;
  placement.Set("x", windowed_bounds_.x());
  placement.Set("y", windowed_bounds_.y());
  placement.Set("width", windowed_bounds_.width());
  placement.Set("height", windowed_bounds_.height());
  placement.Set("maximized", window_maximized_);
  placement.Set("fullscreen", window_fullscreen_);
  WindowPlacementRunner().PostTask(
      FROM_HERE, base::BindOnce([](base::FilePath path, base::DictValue placement) {
        std::string json;
        if (base::JSONWriter::Write(placement, &json))
          base::ImportantFileWriter::WriteFileAtomically(path, json);
      }, data_directory_.AppendASCII(kWindowPlacementFile), std::move(placement)));
}

views::WebView* WorkspaceWindow::CreatePage(Profile* profile, views::View* parent,
    std::unique_ptr<content::WebContents> owned, bool create_contents) {
  EnsureNightShift(profile);
  PushNightShiftConfig(profile);
  auto* view = (parent ? parent : shell_->content_host())->AddChildView(std::make_unique<WorkspaceWebView>(
      profile, base::BindRepeating(&WorkspaceWindow::PreHandlePageKeyEvent, base::Unretained(this)),
      base::BindRepeating(&WorkspaceWindow::OpenPageLink, base::Unretained(this)),
      base::BindRepeating(&WorkspaceWindow::ShowPageMenu, base::Unretained(this)),
      base::BindRepeating(&WorkspaceWindow::AdoptPage, base::Unretained(this)),
      base::BindRepeating([](base::WeakPtr<WorkspaceWindow> window, content::WebContents* source, content::WebContents* page) {
        if (window) window->PreparePage(page, window->WorkspaceForPage(source));
      }, weak_factory_.GetWeakPtr()),
      base::BindRepeating(&WorkspaceWindow::ScheduleShellUpdate, base::Unretained(this)),
      base::BindRepeating(&WorkspaceWindow::PageFocused, base::Unretained(this)),
      base::BindRepeating(&WorkspaceWindow::SetPageFullscreen, weak_factory_.GetWeakPtr()),
      base::BindRepeating([](base::WeakPtr<WorkspaceWindow> window) {
        return window && window->image_link_previews_ && !window->closing_ && !window->profiles_busy_;
      }, weak_factory_.GetWeakPtr()),
      base::BindRepeating([](base::WeakPtr<WorkspaceWindow> window, content::WebContents* source, const GURL& url) {
        if (!window || !window->shift_click_peek_ || !url.SchemeIsHTTPOrHTTPS()) return false;
        const auto workspace = window->WorkspaceForPage(source);
        if (workspace.empty()) return false;
        base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
            base::BindOnce([](base::WeakPtr<WorkspaceWindow> window, GURL url, std::string workspace) {
              if (window) window->OpenPeek(url, workspace);
            }, window, url, workspace));
        return true;
      }, weak_factory_.GetWeakPtr()),
      base::BindRepeating([](base::WeakPtr<WorkspaceWindow> window) {
        return window && window->shell_ && window->shell_->browser_tools().mouse_gestures_enabled &&
            !window->closing_ && !window->profiles_busy_ && window->widget()->IsActive();
      }, weak_factory_.GetWeakPtr()),
      base::BindRepeating([](base::WeakPtr<WorkspaceWindow> window, content::WebContents* source, std::string direction) {
        if (!window) return;
        const auto command = window->shell_->browser_tools().MouseGestureCommand(direction);
        if (command.empty()) return;
        // Run after input dispatch: closing a tab may destroy this WebView.
        base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
            base::BindOnce([](base::WeakPtr<WorkspaceWindow> window, content::WebContents* source, std::string command) {
              if (!window || window->closing_ || window->profiles_busy_ || (window->contents(window->active_id()) != source && window->peek_contents() != source)) return;
              window->RunShortcut(std::move(command));
            }, window, source, command));
      }, weak_factory_.GetWeakPtr())));
  view->SetReaderOpen(base::BindRepeating([](base::WeakPtr<WorkspaceWindow> window, Profile* profile, GURL url) {
    if (window && url.SchemeIsHTTPOrHTTPS()) window->OpenProfileTab(profile, url, true);
  }, weak_factory_.GetWeakPtr(), profile));
  view->set_allow_accelerators(true);
  view->SetVisible(false);
  if (owned) view->SetOwnedWebContents(std::move(owned));
  if (create_contents || view->web_contents()) {
    PreparePage(view->GetWebContents());
    view->SetWorkspaceBackground(shell_->active_workspace_color());
    view->web_contents()->WasHidden();
  }
  if (view->web_contents() && IsRssTabUrl(view->web_contents()->GetVisibleURL())) view->MountReader();
  return view;
}

content::WebContents* WorkspaceWindow::EnsurePage(views::WebView* view) {
  if (!view->web_contents()) {
    auto* page = view->GetWebContents();
    PreparePage(page);
    static_cast<WorkspaceWebView*>(view)->SetWorkspaceBackground(shell_->active_workspace_color());
    page->WasHidden();
  }
  return view->web_contents();
}

GURL WorkspaceWindow::TabUrl(const Host& host, views::WebView* view) {
  if (auto pending = host.deferred_urls.find(view); pending != host.deferred_urls.end()) return pending->second;
  return view->web_contents() ? view->web_contents()->GetVisibleURL() : GURL();
}

std::u16string WorkspaceWindow::TabTitle(const Host& host, views::WebView* view) {
  if (IsRssTabUrl(TabUrl(host, view))) return T(u"Czytnik RSS");
  if (auto stored = host.deferred_titles.find(view); stored != host.deferred_titles.end()) return stored->second;
  return view->web_contents() ? view->web_contents()->GetTitle() : std::u16string();
}

void WorkspaceWindow::PreparePage(content::WebContents* page, std::string source_hint) {
  PageServices::Attach(page);
  favicon::ContentFaviconDriver::CreateForWebContents(page,
      ImplicitFaviconService(Profile::FromBrowserContext(page->GetBrowserContext())));
  PageServices::FromWebContents(page)->SetManagePasswords(base::BindRepeating([](base::WeakPtr<WorkspaceWindow> window) {
    if (window) window->ShowPasswords();
  }, weak_factory_.GetWeakPtr()));
  // Design: Monit o zapisanie hasła — under the address pill, folding into its key.
  PageServices::FromWebContents(page)->SetPasswordAnchor(base::BindRepeating([](base::WeakPtr<WorkspaceWindow> window) {
    return window ? window->shell_->password_anchor_in_screen() : gfx::Rect();
  }, weak_factory_.GetWeakPtr()));
  PageServices::FromWebContents(page)->SetPasswordPendingCallback(base::BindRepeating([](base::WeakPtr<WorkspaceWindow> window, content::WebContents* page, bool pending, bool folded) {
    if (!window) return;
    // Gone or saved: nothing to reopen, and the page may be closing.
    if (!pending) { window->shell_->SetPasswordOffer(false, false, u"", {}); return; }
    window->shell_->SetPasswordOffer(pending, folded, base::UTF8ToUTF16(page->GetLastCommittedURL().host()), base::BindRepeating([](base::WeakPtr<WorkspaceWindow> window, content::WebContents* page) {
      if (window && PageServices::FromWebContents(page)) PageServices::FromWebContents(page)->ReopenPassword();
    }, window, page));
  }, weak_factory_.GetWeakPtr(), page));
  AttachPagePrinting(page, base::BindRepeating([](base::WeakPtr<WorkspaceWindow> window, content::WebContents* page) {
    // Only the page on screen opens the sheet.
    if (window && page == (window->peek_contents() ? window->peek_contents() : window->contents(window->active_id())))
      window->StartPrint(page);
  }, weak_factory_.GetWeakPtr()));
  FingerprintProtection::Attach(page);
  ContentBlockingTabHelper::Attach(page);
  PageDialogs::Attach(page);
  PageMediaAccess::Attach(page);
  // Design: Prośba o dostęp — under the address pill, folding into its icon.
  PageMediaAccess::FromWebContents(page)->SetPromptAnchor(base::BindRepeating([](base::WeakPtr<WorkspaceWindow> window) {
    return window ? window->shell_->password_anchor_in_screen() : gfx::Rect();
  }, weak_factory_.GetWeakPtr()));
  PageMediaAccess::FromWebContents(page)->SetPromptCallback(base::BindRepeating([](base::WeakPtr<WorkspaceWindow> window, content::WebContents* page, const PermissionOffer& offer) {
    if (!window) return;
    if (!offer.pending) { window->shell_->SetPermissionOffer(false, false, 0, u"", {}); return; }
    window->shell_->SetPermissionOffer(true, offer.folded, offer.kind, offer.site, base::BindRepeating([](base::WeakPtr<WorkspaceWindow> window, content::WebContents* page) {
      if (window && PageMediaAccess::FromWebContents(page)) PageMediaAccess::FromWebContents(page)->ReopenPrompt();
    }, window, page));
  }, weak_factory_.GetWeakPtr(), page));
  ObserveNightShiftSettings(page, base::BindRepeating(&WorkspaceWindow::ApplyNightShiftSettings, weak_factory_.GetWeakPtr()));
  PageMediaAccess::FromWebContents(page)->SetCaptureCallback(
      base::BindRepeating(&WorkspaceWindow::ScheduleShellUpdate, weak_factory_.GetWeakPtr()));
  LinkNavigation::Attach(page, base::BindRepeating(
      [](base::WeakPtr<WorkspaceWindow> window, std::string source_hint, content::NavigationHandle& navigation) {
        return window && window->RouteNavigation(navigation, source_hint);
      }, weak_factory_.GetWeakPtr(), std::move(source_hint)));
}
content::WebContents* WorkspaceWindow::AdoptPage(content::WebContents* source,
    std::unique_ptr<content::WebContents> page, bool background) {
  const auto workspace = WorkspaceForPage(source);
  for (auto& host : hosts_) if (host.workspace_id == workspace) {
    if (page->GetBrowserContext() != host.profile) return nullptr;
    auto* view = CreatePage(host.profile, nullptr, std::move(page));
    host.tabs.push_back(view);
    host.popup_tabs.insert(view);
    // A popup from Peek remains in its owning workspace, never in a different
    // profile. Deferring activation avoids destroying an opener on its stack.
    if (!background) base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
        base::BindOnce([](base::WeakPtr<WorkspaceWindow> window, std::string workspace, size_t index) {
          if (window && window->Select(workspace)) window->SelectTab(index);
        }, weak_factory_.GetWeakPtr(), workspace, host.tabs.size() - 1));
    UpdateShell();
    return view->GetWebContents();
  }
  return nullptr;
}

bool WorkspaceWindow::Select(std::string_view id) {
  preconnect_timer_.Stop();
  if (!configuration_ready_ || profiles_busy_) return false;
  if (DeferForProfile(id, base::BindOnce(
      [](base::WeakPtr<WorkspaceWindow> w, std::string id) { if (w) w->Select(id); },
      weak_factory_.GetWeakPtr(), std::string(id)))) return false;
  if (peek_view_ && id != active_id() && GuardPages({peek_contents()},
      base::BindOnce([](base::WeakPtr<WorkspaceWindow> w, std::string id) { if (w) w->Select(id); }, weak_factory_.GetWeakPtr(), std::string(id)))) return false;
  const auto previous = active_id();
  const auto previous_assistant_profile = AssistantProfileId();
  const bool sidebar_was_visible = shell_->sidebar_visible();
  const bool changed = previous != id;
  const bool selected = controller_.Activate(id);
  if (selected) {
    if (assistant_ && previous_assistant_profile != AssistantProfileId()) assistant_->StopAll();
    if (changed) {
      ClosePeek();
      github_timer_.Stop();
      github_manual_refresh_.reset();
    }
    shell_->CloseOverlays(true);
    UpdateShell();
    widget_->Activate();
    for (auto& host : hosts_) if (host.workspace_id == active_id()) host.view->RequestFocus();
    if (sidebar_was_visible && !shell_->sidebar_visible()) shell_->ShowSidebar();
    if (changed) { shell_->AnimateWorkspaceSwitch(previous); SaveSelection(); }
  }
  return selected;
}

void WorkspaceWindow::ScheduleShellUpdate() {
  if (closing_ || shell_update_pending_) return;
  shell_update_pending_ = true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
      base::BindOnce([](base::WeakPtr<WorkspaceWindow> window) {
        if (window && window->shell_update_pending_) window->UpdateShell();
      }, weak_factory_.GetWeakPtr()));
}

void WorkspaceWindow::SetPageFullscreen(content::WebContents* page, bool fullscreen) {
  if (closing_ || !widget_ || !shell_) return;
  if (fullscreen) {
    if (fullscreen_page_ && fullscreen_page_.get() != page) fullscreen_page_->ExitFullscreen(true);
    views::WebView* view = nullptr;
    for (auto& host : hosts_) for (const auto& tab : host.tabs)
      if (tab->web_contents() == page) view = tab;
    if (peek_view_ && peek_contents() == page) {
      view = peek_view_;
      auto owned = view->parent()->RemoveChildViewT(view);
      shell_->content_host()->AddChildView(std::move(owned));
      shell_->HidePeek();
    }
    if (!view) return;
    PageFocused(view);
    fullscreen_page_ = page->GetWeakPtr();
    was_window_fullscreen_ = widget_->IsFullscreen();
    shell_->SetContentFullscreen(view);
    view->holder()->SetNativeViewCornerRadii(gfx::RoundedCornersF());
    widget_->SetFullscreen(true);
  } else {
    if (fullscreen_page_.get() != page) return;
    fullscreen_page_.reset();
    widget_->SetFullscreen(was_window_fullscreen_);
    shell_->SetContentFullscreen(nullptr);
    if (peek_view_ && peek_contents() == page && peek_view_->parent() == shell_->content_host()) {
      auto owned = shell_->content_host()->RemoveChildViewT(peek_view_.get());
      shell_->peek_host()->AddChildView(std::move(owned));
      shell_->ShowPeek(base::UTF8ToUTF16(page->GetVisibleURL().host()));
    }
    for (const auto& host : hosts_) if (host.workspace_id == active_id()) {
      ApplyPanes(host);
      for (auto* pane : VisiblePanes(host))
        if (pane->holder()->native_view()) pane->holder()->SetNativeViewCornerRadii(gfx::RoundedCornersF(10));
    }
  }
  widget_->LayoutRootViewIfNecessary();
  if (page && page->GetPrimaryMainFrame())
    page->GetPrimaryMainFrame()->GetRenderViewHost()->GetWidget()->SynchronizeVisualProperties();
}

namespace {
std::vector<uint32_t> FaviconPixels(const gfx::Image& image) {
  if (image.IsEmpty()) return {};
  const auto scaled = gfx::ImageSkiaOperations::CreateResizedImage(image.AsImageSkia(), skia::ImageOperations::RESIZE_BEST, gfx::Size(32, 32));
  const auto& bitmap = scaled.GetRepresentation(1.0f).GetBitmap();
  std::vector<uint32_t> pixels(32 * 32);
  if (!bitmap.readPixels(SkImageInfo::MakeN32Premul(32, 32), pixels.data(), 32 * sizeof(uint32_t), 0, 0)) return {};
  return pixels;
}
}  // namespace

void WorkspaceWindow::UpdateShell() {
  shell_update_pending_ = false;
  if (closing_ || !widget_) return;
  NoteRecentTab();
  {
    auto* page = peek_contents() ? peek_contents() : contents(active_id());
    auto* access = page ? PageMediaAccess::FromWebContents(page) : nullptr;
    const auto capture = access ? access->capture_state() : CaptureState();
    shell_->SetCaptureIndicator(capture.active, capture.camera, capture.microphone, capture.screen, capture.site,
        base::BindRepeating([](base::WeakPtr<content::WebContents> page) {
          if (auto* access = page ? PageMediaAccess::FromWebContents(page.get()) : nullptr) access->ToggleCaptureControls();
        }, page ? page->GetWeakPtr() : base::WeakPtr<content::WebContents>()));
  }
  if (fullscreen_page_ && fullscreen_page_.get() != contents(active_id()) && fullscreen_page_.get() != peek_contents())
    fullscreen_page_->ExitFullscreen(true);
  // The find bar belongs to one page: another tab or space ends it.
  if (find_page_ && find_page_ != (peek_contents() ? peek_contents() : contents(active_id()))) shell_->ResetFind();
  ScheduleSessionSave();
  if (auto* page = peek_contents() ? peek_contents() : contents(active_id())) shell_->SetDeveloperPage(page->GetVisibleURL(), DevToolsWindow::AllowDevToolsFor(Profile::FromBrowserContext(page->GetBrowserContext()), page), !peek_contents());
  if (peek_view_) {
    const auto* workspace = model_.Find(peek_workspace_id_);
    const auto& url = peek_contents()->GetVisibleURL();
    if (workspace) shell_->SetPeekTitle(base::UTF8ToUTF16(workspace->name + " · " + std::string(url.host())));
  }
  for (const auto& host : hosts_) {
    if (host.workspace_id != active_id()) continue;
    ShellState state;
    for (const auto& candidate : hosts_) if (candidate.profile_id == host.profile_id) for (const auto& view : candidate.tabs) {
      auto* page = view->web_contents();
      auto* entry = page ? page->GetController().GetVisibleEntry() : nullptr;
      if (!entry || !entry->GetFavicon().valid || favicon_refreshing_.contains({host.profile_id,page->GetVisibleURL().spec()})) continue;
      auto pixels = FaviconPixels(entry->GetFavicon().image);
      if (!pixels.empty()) favicon_cache_[{host.profile_id, page->GetVisibleURL().spec()}] = std::move(pixels);
    }
    for (const auto& [key, pixels] : favicon_cache_) if (key.first == host.profile_id) state.favicons[key.second] = pixels;
    std::map<views::WebView*, std::string> pin_ids;
    for (const auto& [id, view] : host.pin_tabs) pin_ids.emplace(view.get(), id);
    for (const auto& view : host.tabs) {
      const GURL display_url = TabUrl(host, view);
      auto title = TabTitle(host, view);
      if (title.empty()) title = base::UTF8ToUTF16(display_url.host());
      std::string pin_id;
      if (auto pin = pin_ids.find(view.get()); pin != pin_ids.end()) {
        pin_id = pin->second;
        if (view == host.view) state.active_pin = pin_id;
      }
      const size_t index = &view - host.tabs.data();
      if (pin_id.empty() || InSplit(host, view))
      {
        int capture = 0;
        if (auto* access = view->web_contents() ? PageMediaAccess::FromWebContents(view->web_contents()) : nullptr) {
          const auto live = access->capture_state();
          capture = !live.active ? 0 : live.camera ? 2 : live.microphone ? 1 : 3;
        }
        state.tabs.push_back({title.empty() ? T(u"Nowa karta") : title, display_url, index, pin_id, static_cast<WorkspaceWebView*>(view.get())->sidebar_identity(),
                              view->web_contents() && view->web_contents()->IsCurrentlyAudible(), capture});
      }
    }
    auto& controller = host.view->GetWebContents()->GetController();
    state.workspace_id = host.workspace_id;
    state.workspace_name = base::UTF8ToUTF16(model_.Find(host.workspace_id)->name);
    for (const auto& entry : profiles_)
      if (entry.info.id == host.profile_id) state.profile_name = base::UTF8ToUTF16(entry.info.name);
    state.profiles = profiles();
    state.link_rules = link_rules_;
    for (const auto& other : hosts_) if (other.sleeping) state.sleeping_workspaces.insert(other.workspace_id);
    state.profiles_busy = profiles_busy();
    state.pins = pinned_items(host.workspace_id);
    if (auto* service = ImplicitFaviconService(host.profile)) {
      for (const auto& pin : state.pins) {
        if (pin.folder || !pin.url.SchemeIsHTTPOrHTTPS()) continue;
        const auto key = std::make_pair(host.profile_id, pin.url.spec());
        if (favicon_refreshing_.contains(key) || favicon_cache_.contains(key) || !favicon_requests_.insert(key).second) continue;
        service->GetFaviconImageForPageURL(pin.url, base::BindOnce(
            [](base::WeakPtr<WorkspaceWindow> window, std::pair<std::string,std::string> key, uint64_t generation, const favicon_base::FaviconImageResult& result) {
              if (!window || window->favicon_generations_[key]!=generation || window->favicon_refreshing_.contains(key) || result.image.IsEmpty()) return;
              auto pixels = FaviconPixels(result.image);
              if (!pixels.empty()) { window->favicon_cache_[key] = std::move(pixels); window->UpdateShell(); }
            }, weak_factory_.GetWeakPtr(), key, favicon_generations_[key]), &favicon_tasks_);
      }
    }
    state.profile_id = host.profile_id;
    state.workspaces = model_.workspaces();
#if BUILDFLAG(IS_WIN)
    std::vector<std::pair<std::string, std::string>> listed;
    for (const auto& space : state.workspaces) listed.emplace_back(space.id, space.name);
    if (!is_private() && listed != jump_list_spaces_) {
      jump_list_spaces_ = listed;
      std::vector<JumpListSpace> spaces;
      for (const auto& [id, name] : listed) spaces.push_back({id, base::UTF8ToUTF16(name)});
      UpdateJumpList(std::move(spaces));
    }
#endif
    state.configuration_ready = configuration_ready_ && !profiles_busy_ && !configuration_invalid_;
    state.onboarding_complete = !offer_import_ || onboarding_complete_;
    state.url = host.view->GetWebContents()->GetVisibleURL();
    state.active_tab = host.active_tab;
    for (const auto& member : host.split)
      state.split_tabs.push_back(std::find(host.tabs.begin(), host.tabs.end(), member) - host.tabs.begin());
    state.split = !host.split.empty();
    state.split_visible = SplitShown(host);
    state.split_vertical = host.vertical;
    if (state.split_visible)
      state.active_pane = std::find(host.split.begin(), host.split.end(), host.view) - host.split.begin();
    state.can_go_back = controller.CanGoBack();
    state.can_go_forward = controller.CanGoForward();
    state.loading = host.view->GetWebContents()->ShouldShowLoadingUI();
    // The panel watches the active profile even while closed, so a finished
    // download shows its toast (design: Downloads).
    EnsureDownloads();
    downloads_->SetProfile(host.profile);
    extensions_bar_->SetContext(host.profile, host.view->GetWebContents());
    extension_tabs_->Sync();
    // The page on screen changed: chrome.tabs.onActivated for its profile.
    if (auto* shown = host.view->GetWebContents(); shown != shown_page_.get()) {
      shown_page_ = shown->GetWeakPtr();
      ExtensionTabEvents::DispatchActivated(shown);
    }
    shell_->Update(std::move(state));
    for (const auto& view : host.tabs)
      static_cast<WorkspaceWebView*>(view.get())->SetWorkspaceBackground(shell_->active_workspace_color());
  }
  ScheduleGitHubRefresh();
}

namespace {
// The page is a rounded card inside the tinted frame. The native view attaches
// only after the page is shown, so every refresh rounds each visible pane again.
void RoundPage(views::WebView* view) {
  if (!view || !view->holder()->native_view()) return;
  const auto corners = gfx::RoundedCornersF(view->web_contents() && view->web_contents()->IsFullscreen() ? 0 : 10);
  if (view->holder()->GetNativeViewCornerRadii() != corners) view->holder()->SetNativeViewCornerRadii(corners);
}
}  // namespace
void WorkspaceWindow::ShowPage(views::WebView* view) {
  auto* page = EnsurePage(view);
  RoundPage(view);
  if (view->GetVisible() && page->GetVisibility() == content::Visibility::VISIBLE) return;
  view->SetVisible(true);
  page->UpdateWebContentsVisibility(content::Visibility::VISIBLE);
  RoundPage(view);
}

bool WorkspaceWindow::Activate(std::string_view id,
                               std::string_view profile_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  Host* selected = nullptr;
  for (auto& host : hosts_) {
    if (host.workspace_id == id && host.profile_id == profile_id) {
      selected = &host;
    }
  }
  if (!selected || !selected->profile) {
    return false;
  }
  if (selected->sleeping) {
    auto stored = selected->sleeping->Clone();
    if (!RestoreHost(*selected, stored)) return false;
  }
  // The controller commits active_id after this callback returns.
  ApplyPanes(*selected);
  for (auto& host : hosts_) {
    const auto panes = VisiblePanes(host);
    for (auto& tab : host.tabs) {
      if (&host == selected && std::find(panes.begin(), panes.end(), tab) != panes.end()) {
        ShowPage(tab);
        host.deferred_titles.erase(tab);
        if (auto pending = host.deferred_urls.find(tab); pending != host.deferred_urls.end()) {
          LoadWorkspaceTab(tab->GetWebContents(), pending->second, ui::PAGE_TRANSITION_RELOAD);
          host.deferred_urls.erase(pending);
        }
      } else {
        if (tab->GetVisible()) {
          tab->SetVisible(false);
          tab->GetWebContents()->WasHidden();
        }
      }
    }
  }
  selected->view->RequestFocus();
  return true;
}

content::WebContents* WorkspaceWindow::contents(std::string_view id) const {
  for (const auto& host : hosts_) {
    if (host.workspace_id == id) {
      return host.view ? host.view->web_contents() : nullptr;
    }
  }
  return nullptr;
}

std::string WorkspaceWindow::active_id() const {
  return model_.active() ? model_.active()->id : std::string();
}

void WorkspaceWindow::FocusAddressBar() {
  if (peek_view_ && GuardPages({peek_contents()}, base::BindOnce(&WorkspaceWindow::FocusAddressBar, weak_factory_.GetWeakPtr()))) return;
  ClosePeek();
  UpdateShell();
  shell_->OpenAddress(false);
}

void WorkspaceWindow::OpenCommandPalette() {
  if (peek_view_ && GuardPages({peek_contents()}, base::BindOnce(&WorkspaceWindow::OpenCommandPalette, weak_factory_.GetWeakPtr()))) return;
  ClosePeek();
  UpdateShell(); shell_->OpenCommandPalette();
}
BrowserPreferences WorkspaceWindow::ReadPreferences() const {
  BrowserPreferences result;
  const auto* workspace = model_.active();
  if (!workspace || !configuration_ready_ || profiles_busy_) return result;
  auto* profile = ProfileFor(workspace->profile_id);
  if (!profile) return result;
  result["image_link_previews"] = {image_link_previews_, !configuration_invalid_};
  result["shift_click_peek"] = {shift_click_peek_, !configuration_invalid_};
  result["clean_address_copies"] = {clean_address_copies(), !configuration_invalid_};
  {
    // Assistant rows depend on the master switch and on the click permission.
    const auto& a = assistant_settings_;
    const bool on = !configuration_invalid_ && a.enabled;
    result["assistant_enabled"] = {a.enabled, !configuration_invalid_};
    result["assistant_tabs"] = {a.defaults.tabs, on};
    result["assistant_pages"] = {a.defaults.pages, on};
    result["assistant_actions"] = {a.defaults.actions, on};
    result["assistant_history"] = {a.defaults.history, on};
    result["assistant_web"] = {a.defaults.web, on};
    result["assistant_confirm_clicks"] = {a.defaults.confirm_clicks, on && a.defaults.actions};
    result["assistant_background"] = {a.background, on && a.defaults.actions};
    result["assistant_confirm_background"] = {a.confirm_background, on && a.defaults.actions && a.background};
    constexpr auto kConcurrency = std::to_array({2, 4, 8, 12});
    const auto found = std::ranges::find(kConcurrency, a.concurrency);
    result["assistant_concurrency"] = {found == kConcurrency.end() ? 3 : static_cast<int>(found - kConcurrency.begin()), on};
  }
  auto* service = profile->GetPrefs();
  auto read = [&](const char* key, const char* pref, int value) {
    const auto* entry = service->FindPreference(pref);
    if (entry) result[key] = {value, entry->IsUserModifiable()};
  };
  AppendChromiumPreferences(profile, g_browser_process->local_state(), result);
  for (const auto& entry : kBooleanPreferences)
    read(entry.key, entry.pref, service->GetBoolean(entry.pref));
  result["ad_blocking_stealth"] = {std::ranges::all_of(kStealthLists, [&](const char* id) { return ContentBlocking::ListEnabled(profile, id); }),
                                   service->GetBoolean(kAdBlockingPref)};
  read("download_directory", prefs::kDownloadDefaultDirectory, 0);
  read("preconnect", prefs::kNetworkPredictionOptions,
       prefetch::GetPreloadPagesState(*service) != prefetch::PreloadPagesState::kNoPreloading);
  read("third_party_cookies", prefs::kCookieControlsMode,
       service->GetInteger(prefs::kCookieControlsMode) == 1);
  auto* settings = HostContentSettingsMapFactory::GetForProfile(profile);
  for (const auto& entry : kContentPreferences) {
    content_settings::ProviderType provider;
    auto value = settings->GetDefaultContentSetting(entry.type, &provider);
    result[entry.key] = {value == entry.enabled,
        content_settings::GetSettingSourceFromProviderType(provider) == content_settings::SettingSource::kUser};
  }
  auto index = [](auto& values, int value) {
    auto it = std::find(std::begin(values), std::end(values), value);
    return it == std::end(values) ? -1 : static_cast<int>(it - std::begin(values));
  };
  read("font_size", prefs::kWebKitDefaultFontSize,
       index(kFontSizes, service->GetInteger(prefs::kWebKitDefaultFontSize)));
  read("fixed_font_size", prefs::kWebKitDefaultFixedFontSize,
       index(kFontSizes, service->GetInteger(prefs::kWebKitDefaultFixedFontSize)));
  read("minimum_font_size", prefs::kWebKitMinimumFontSize,
       index(kMinimumFontSizes, service->GetInteger(prefs::kWebKitMinimumFontSize)));
  if (auto* zoom = profile->GetZoomLevelPrefs())
    read("zoom", prefs::kPartitionDefaultZoomLevel, index(kZoomPercentages, static_cast<int>(std::lround(zoom->GetDefaultZoomFactor() * 100))));
  if (is_private()) for (auto& [key, value] : result)
    value.editable = key == "fingerprint_protection" && value.editable;
  return result;
}
std::u16string WorkspaceWindow::DownloadDirectory() const {
  if (!model_.active()) return {};
  auto* profile = ProfileFor(model_.active()->profile_id);
  return profile ? profile->GetPrefs()->GetFilePath(prefs::kDownloadDefaultDirectory).LossyDisplayName() : std::u16string();
}
void WorkspaceWindow::ChooseDownloadDirectory() {
  auto settings = ReadPreferences();
  auto it = settings.find("download_directory");
  if (it == settings.end() || !it->second.editable || !download_directory_profile_.empty()) return;
  download_directory_profile_ = model_.active()->profile_id;
  auto* profile = ProfileFor(download_directory_profile_);
  if (!download_directory_dialog_) download_directory_dialog_ = ui::SelectFileDialog::Create(this, nullptr);
  download_directory_dialog_->SelectFile(ui::SelectFileDialog::SELECT_FOLDER,
      T(u"Wybierz folder pobierania"), profile->GetPrefs()->GetFilePath(prefs::kDownloadDefaultDirectory),
      nullptr, 0, base::FilePath::StringType(), shell_->general_widget()->GetNativeWindow());
}
void WorkspaceWindow::FileSelected(const ui::SelectedFileInfo& file, int) {
  // A workspace may have changed while the native sheet was open.
  auto id = std::exchange(download_directory_profile_, {});
  auto* profile = ProfileFor(id);
  if (profile && file.file_path.IsAbsolute() &&
      profile->GetPrefs()->FindPreference(prefs::kDownloadDefaultDirectory)->IsUserModifiable()) {
    profile->GetPrefs()->SetFilePath(prefs::kDownloadDefaultDirectory, file.file_path);
    UpdateShell();
  }
}
void WorkspaceWindow::FileSelectionCanceled() { download_directory_profile_.clear(); }
bool WorkspaceWindow::WritePreference(std::string key, int value) {
  if (is_private()) {
    auto* profile = active_profile();
    if (key != "fingerprint_protection" || !profile || (value != 0 && value != 1) ||
        !profile->GetPrefs()->FindPreference(kFingerprintProtectionPref)->IsUserModifiable()) return false;
    profile->GetPrefs()->SetBoolean(kFingerprintProtectionPref, value != 0);
    return true;
  }
  auto current = ReadPreferences();
  auto it = current.find(key);
  if (it == current.end() || !it->second.editable) return false;
  if (key.starts_with("assistant_")) {
    auto settings = assistant_settings_;
    if (key == "assistant_concurrency") {
      constexpr auto kConcurrency = std::to_array({2, 4, 8, 12});
      if (value < 0 || value >= static_cast<int>(kConcurrency.size())) return false;
      settings.concurrency = kConcurrency[static_cast<size_t>(value)];
    } else {
      if (value != 0 && value != 1) return false;
      const bool on = value != 0;
      if (key == "assistant_enabled") settings.enabled = on;
      else if (key == "assistant_tabs") settings.defaults.tabs = on;
      else if (key == "assistant_pages") settings.defaults.pages = on;
      else if (key == "assistant_actions") settings.defaults.actions = on;
      else if (key == "assistant_history") settings.defaults.history = on;
      else if (key == "assistant_web") settings.defaults.web = on;
      else if (key == "assistant_confirm_clicks") settings.defaults.confirm_clicks = on;
      else if (key == "assistant_background") settings.background = on;
      else if (key == "assistant_confirm_background") settings.confirm_background = on;
      else return false;
    }
    SetAssistantSettings(std::move(settings));
    return true;
  }
  if (key == "clean_address_copies") {
    if (value != 0 && value != 1) return false;
    clean_address_copies_ = value != 0;
    SaveConfiguration();
    return true;
  }
  if (key == "image_link_previews" || key == "shift_click_peek") {
    if (value != 0 && value != 1) return false;
    if (key == "image_link_previews") {
      image_link_previews_ = value != 0;
      for (const auto& host : hosts_) for (const auto& view : host.tabs)
        static_cast<WorkspaceWebView*>(view.get())->ResetImagePreview();
      if (peek_view_) static_cast<WorkspaceWebView*>(peek_view_.get())->ResetImagePreview();
    } else shift_click_peek_ = value != 0;
    SaveConfiguration();
    return true;
  }
  auto* profile = ProfileFor(model_.active()->profile_id);
  auto* service = profile->GetPrefs();
  if (auto result = WriteChromiumPreference(profile, g_browser_process->local_state(), key, value)) return *result;
  if (key == "preconnect") {
    if (value != 0 && value != 1) return false;
    // Preserve an imported extended setting when it is already enabled.
    if (value && prefetch::GetPreloadPagesState(*service) != prefetch::PreloadPagesState::kNoPreloading) return true;
    prefetch::SetPreloadPagesState(service, value ? prefetch::PreloadPagesState::kStandardPreloading
                                              : prefetch::PreloadPagesState::kNoPreloading);
    if (!value) preconnect_timer_.Stop();
    return true;
  }
  for (const auto& entry : kBooleanPreferences) if (key == entry.key) {
    if (value != 0 && value != 1) return false;
    service->SetBoolean(entry.pref, value != 0);
    return true;
  }
  if (key == "ad_blocking_stealth") {
    if (value != 0 && value != 1) return false;
    for (const char* id : kStealthLists) ContentBlocking::SetListEnabled(profile, id, value != 0);
    return true;
  }
  if (key == "third_party_cookies") {
    if (value != 0 && value != 1) return false;
    // Chromium's normal mode (2) versus blocking third-party cookies (1).
    service->SetInteger(prefs::kCookieControlsMode, value ? 1 : 2);
    return true;
  }
  for (const auto& entry : kContentPreferences) if (key == entry.key) {
    if (value != 0 && value != 1) return false;
    HostContentSettingsMapFactory::GetForProfile(profile)->SetDefaultContentSetting(
        entry.type, value ? entry.enabled : CONTENT_SETTING_BLOCK);
    return true;
  }
  if (key == "zoom" && value >= 0 && value < static_cast<int>(std::size(kZoomPercentages))) {
    profile->GetZoomLevelPrefs()->SetDefaultZoomLevelPref(blink::ZoomFactorToZoomLevel(kZoomPercentages[value] / 100.0));
    return true;
  }
  if (key == "fixed_font_size" && value >= 0 && value < static_cast<int>(std::size(kFontSizes))) {
    service->SetInteger(prefs::kWebKitDefaultFixedFontSize, kFontSizes[value]); return true;
  }
  if (key == "font_size" && value >= 0 && value < static_cast<int>(std::size(kFontSizes))) {
    service->SetInteger(prefs::kWebKitDefaultFontSize, kFontSizes[value]); return true;
  }
  if (key == "minimum_font_size" && value >= 0 && value < static_cast<int>(std::size(kMinimumFontSizes))) {
    service->SetInteger(prefs::kWebKitMinimumFontSize, kMinimumFontSizes[value]); return true;
  }
  return false;
}

// The panel feeds the footer ring and the start cue (design: Downloads).
void WorkspaceWindow::EnsureDownloads() {
  if (downloads_) return;
  downloads_ = std::make_unique<DownloadPanel>();
  downloads_->SetCompletedCallback(base::BindRepeating(&WorkspaceWindow::DownloadCompleted, base::Unretained(this)));
  downloads_->SetStartedCallback(base::BindRepeating([](WorkspaceWindow* window, std::u16string name) {
    window->shell_->DownloadStarted(std::move(name));
  }, base::Unretained(this)));
  downloads_->SetProgressCallback(base::BindRepeating([](WorkspaceWindow* window, int active, double fraction, std::u16string newest) {
    window->shell_->SetDownloadProgress(active, fraction, std::move(newest));
  }, base::Unretained(this)));
  // Design: Okno Pobrane — removing from the list is undone from its toast;
  // deleting from the disk asks first; a file gone from the disk downloads again.
  downloads_->SetRemovedCallback(base::BindRepeating([](WorkspaceWindow* window, int count, base::OnceClosure undo) {
    const int tens = count % 100, ones = count % 10;
    const auto files = count == 1 ? T(u"plik") : (ones >= 2 && ones <= 4 && (tens < 12 || tens > 14)) ? T(u"pliki") : T(u"plików");
    window->shell_->ShowToast(T(u"Usunięto z listy: "), base::NumberToString16(count) + u" " + files, std::move(undo));
  }, base::Unretained(this)));
  downloads_->SetConfirmCallback(base::BindRepeating([](WorkspaceWindow* window, views::Widget* parent, std::u16string title, std::u16string detail,
                                                        std::u16string warning, std::u16string action, base::OnceCallback<void(bool)> answer) {
    window->shell_->ConfirmDanger(parent, std::move(title), std::move(detail), std::move(warning), std::move(action), std::move(answer));
  }, base::Unretained(this)));
  downloads_->SetRedownloadCallback(base::BindRepeating([](WorkspaceWindow* window, GURL url) {
    auto* page = window->contents(window->active_id());
    if (!page || !url.SchemeIsHTTPOrHTTPS()) return;
    static constexpr net::NetworkTrafficAnnotationTag kRedownload = net::DefineNetworkTrafficAnnotation("zitter_download_again", R"(
      semantics {
        sender: "Zitter Downloads"
        description: "Downloads a file again from its original address."
        trigger: "The user chooses Download again for a file in the Downloads window."
        data: "The file's original URL and the profile's cookies."
        destination: WEBSITE
      }
      policy { cookies_allowed: YES setting: "None." policy_exception_justification: "User-initiated download." })");
    page->GetBrowserContext()->GetDownloadManager()->DownloadUrl(
        content::DownloadRequestUtils::CreateDownloadForWebContentsMainFrame(page, url, kRedownload));
  }, base::Unretained(this)));
  downloads_->SetChangeFolderCallback(base::BindRepeating(&WorkspaceWindow::ChooseDownloadDirectory, base::Unretained(this)));
}
void WorkspaceWindow::DownloadCompleted(std::string guid, std::u16string name) {
  shell_->DownloadFinished();
  shell_->ShowToast(T(u"Pobrano "), name, base::BindOnce([](base::WeakPtr<WorkspaceWindow> window, std::string guid) {
    if (window && window->downloads_) window->downloads_->Act(guid, DownloadPanel::Action::kReveal);
  }, weak_factory_.GetWeakPtr(), std::move(guid)), T(u"Pokaż"));
}
void WorkspaceWindow::ShowDownloads() {
  auto* page = peek_contents() ? peek_contents() : contents(active_id());
  if (!page) return;
  // The ring or pill it was opened from, before overlays (the sidebar) close.
  const auto anchor = shell_->downloads_anchor_in_screen();
  shell_->CloseOverlays(true);
  EnsureDownloads();
  downloads_->Show(Profile::FromBrowserContext(page->GetBrowserContext()), widget_.get(), anchor);
}
// Design: Historia — Zitter's own window over the active profile's history,
// with the tabs closed in this run beside it.
// Design: Zitter dostępy stron — the page on screen, its permissions and data.
void WorkspaceWindow::ShowSitePanel() {
  auto* page = peek_contents() ? peek_contents() : contents(active_id());
  if (!page || !page->GetLastCommittedURL().SchemeIsHTTPOrHTTPS()) return;
  auto* profile = Profile::FromBrowserContext(page->GetBrowserContext());
  const GURL url = page->GetLastCommittedURL();
  auto weak_page = page->GetWeakPtr();
  auto alive = [](base::WeakPtr<content::WebContents> page, const GURL& url) {
    return page && page->GetLastCommittedURL().host() == url.host();
  };
  WorkspaceShell::SiteHandlers handlers;
  handlers.read = base::BindRepeating([](base::WeakPtr<content::WebContents> page, GURL url) {
    return page ? ReadSitePermissions(Profile::FromBrowserContext(page->GetBrowserContext()), url) : SiteSummary();
  }, weak_page, url);
  handlers.set_permission = base::BindRepeating([](decltype(alive) alive, base::WeakPtr<content::WebContents> page, GURL url, std::string key, int setting) {
    return alive(page, url) && WriteSitePermission(Profile::FromBrowserContext(page->GetBrowserContext()), url, key, setting);
  }, alive, weak_page, url);
  handlers.reset_permissions = base::BindRepeating([](decltype(alive) alive, base::WeakPtr<content::WebContents> page, GURL url) {
    if (alive(page, url)) ResetSitePermissions(Profile::FromBrowserContext(page->GetBrowserContext()), url);
  }, alive, weak_page, url);
  handlers.measure = base::BindRepeating([](base::WeakPtr<content::WebContents> page, GURL url, base::RepeatingCallback<void(const SiteDataFacts&)> done) {
    if (page) MeasureSiteData(Profile::FromBrowserContext(page->GetBrowserContext()), url, std::move(done));
  }, weak_page, url);
  // Cleared, then reloaded so the page starts without what was removed.
  handlers.clear = base::BindRepeating([](decltype(alive) alive, base::WeakPtr<content::WebContents> page, GURL url, SiteClearChoice choice) {
    if (!alive(page, url)) return;
    ClearSiteData(Profile::FromBrowserContext(page->GetBrowserContext()), url, choice, base::BindOnce([](base::WeakPtr<content::WebContents> page) {
      if (page) page->GetController().Reload(content::ReloadType::NORMAL, true);
    }, page));
  }, alive, weak_page, url);
  handlers.delete_cookie = base::BindRepeating([](decltype(alive) alive, base::WeakPtr<content::WebContents> page, GURL url, SiteCookie cookie) {
    if (alive(page, url)) DeleteSiteCookie(Profile::FromBrowserContext(page->GetBrowserContext()), std::move(cookie), base::DoNothing());
  }, alive, weak_page, url);
  shell_->ShowSitePanel(ReadSitePermissions(profile, url), std::move(handlers));
}

void WorkspaceWindow::AssociateReaderPin(Host& host, views::WebView* view) {
  for (const auto& pin : pinned_items(host.workspace_id)) {
    if (!pin.folder && IsRssTabUrl(pin.url) && !host.pin_tabs.contains(pin.id)) {
      host.pin_tabs[pin.id] = view;
      return;
    }
  }
}
RssPanel* WorkspaceWindow::rss_panel() const {
  for (const auto& host : hosts_) if (host.workspace_id == active_id() && host.view)
    return static_cast<WorkspaceWebView*>(host.view.get())->rss();
  return nullptr;
}
void WorkspaceWindow::ShowRss() {
  if (!configuration_ready_ || profiles_busy_ || !model_.active()) return;
  auto* source = contents(active_id());
  shell_->CloseOverlays(true);
  for (auto& host : hosts_) {
    if (host.workspace_id != active_id() || !host.profile) continue;
    size_t index = 0;
    for (; index < host.tabs.size(); ++index) if (IsRssTabUrl(TabUrl(host, host.tabs[index]))) break;
    if (index == host.tabs.size()) {
      auto* view = CreatePage(host.profile);
      host.tabs.push_back(view);
      host.deferred_urls[view] = GURL(kRssTabUrl);
      AssociateReaderPin(host, view);
    }
    auto* view = static_cast<WorkspaceWebView*>(host.tabs[index].get());
    view->MountReader();
    auto* panel = view->rss();
    if (source && source != view->web_contents() && source->GetLastCommittedURL().SchemeIsHTTPOrHTTPS() && source->GetPrimaryMainFrame()) {
      panel->BeginDiscovery();
      source->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
        u"Array.from(document.querySelectorAll('link[rel~=alternate i]')).filter(x=>['application/rss+xml','application/atom+xml'].includes(x.type.toLowerCase().split(';')[0].trim())).slice(0,10).map(x=>x.href)",
        base::BindOnce([](base::WeakPtr<RssPanel> panel, size_t generation, content::WeakDocumentPtr document, base::Value result) {
          if (panel && panel->discovery_generation() == generation && document.AsRenderFrameHostIfValid()) panel->DiscoverFromPage(std::move(result));
        }, panel->GetWeakPtr(), panel->discovery_generation(), source->GetPrimaryMainFrame()->GetWeakDocumentPtr()), content::ISOLATED_WORLD_ID_CONTENT_END);
    }
    SelectTab(index);
    return;
  }
}

void WorkspaceWindow::ShowHistory() {
  if (is_private()) return;
  if (!model_.active()) return;
  auto* profile = ProfileFor(model_.active()->profile_id);
  if (!profile) return;
  shell_->CloseOverlays(true);
  if (!history_panel_) {
    history_panel_ = std::make_unique<HistoryPanel>();
    history_panel_->SetOpenCallback(base::BindRepeating([](base::WeakPtr<WorkspaceWindow> window, GURL url, HistoryPanel::Open where) {
      if (!window || !window->model_.active()) return;
      if (where == HistoryPanel::Open::kNewTab) { window->OpenProfileTab(window->ProfileFor(window->model_.active()->profile_id), url, true); return; }
      if (window->OpenPeek(url) && where == HistoryPanel::Open::kSplit) window->PromotePeek(true);
    }, weak_factory_.GetWeakPtr()));
    history_panel_->SetClosedSource(base::BindRepeating([](base::WeakPtr<WorkspaceWindow> window) {
      std::vector<HistoryPanel::Closed> closed;
      if (!window) return closed;
      for (auto tab = window->closed_tabs_.rbegin(); tab != window->closed_tabs_.rend(); ++tab) {
        std::u16string space;
        for (const auto& workspace : window->model_.workspaces()) if (workspace.id == tab->workspace_id) space = base::UTF8ToUTF16(workspace.name);
        closed.push_back({tab->title, tab->url, space, tab->closed_at});
      }
      return closed;
    }, weak_factory_.GetWeakPtr()));
    history_panel_->SetReopenCallback(base::BindRepeating([](base::WeakPtr<WorkspaceWindow> window, size_t newest) {
      if (window) window->ReopenClosedTabAt(newest);
    }, weak_factory_.GetWeakPtr()));
    history_panel_->SetConfirmCallback(base::BindRepeating([](WorkspaceWindow* window, views::Widget* parent, std::u16string title, std::u16string detail,
                                                              std::u16string warning, std::u16string action, base::OnceCallback<void(bool)> answer) {
      window->shell_->ConfirmDanger(parent, std::move(title), std::move(detail), std::move(warning), std::move(action), std::move(answer));
    }, base::Unretained(this)));
    history_panel_->SetClearDataCallback(base::BindRepeating([](base::WeakPtr<WorkspaceWindow> window) {
      if (!window || !window->model_.active() || !window->history_panel_) return;
      auto* profile = window->ProfileFor(window->model_.active()->profile_id);
      window->shell_->ShowClearDataDialog(window->history_panel_->widget(), profile ? BrowsingProfileName(profile) : std::u16string());
    }, weak_factory_.GetWeakPtr()));
    history_panel_->SetTimeMachineCallback(base::BindRepeating([](base::WeakPtr<WorkspaceWindow> window) {
      if (window) window->RunShortcut("timeline");
    }, weak_factory_.GetWeakPtr()));
  }
  history_panel_->Show(profile, widget_.get());
}
// Design: Menedżer haseł — Zitter's own window over the active profile's
// password store.
void WorkspaceWindow::ShowPasswords() {
  if (is_private()) return;
  if (!model_.active()) return;
  auto* profile = ProfileFor(model_.active()->profile_id);
  if (!profile) return;
  shell_->CloseOverlays(true);
  ClosePeek();
  if (!passwords_panel_) {
    passwords_panel_ = std::make_unique<PasswordsPanel>();
    passwords_panel_->SetOpenCallback(base::BindRepeating([](base::WeakPtr<WorkspaceWindow> window, GURL url) {
      if (window && window->model_.active() && url.SchemeIsHTTPOrHTTPS()) window->OpenProfileTab(window->ProfileFor(window->model_.active()->profile_id), url, true);
    }, weak_factory_.GetWeakPtr()));
    passwords_panel_->SetConfirmCallback(base::BindRepeating([](WorkspaceWindow* window, views::Widget* parent, std::u16string title, std::u16string detail,
                                                                std::u16string warning, std::u16string action, base::OnceCallback<void(bool)> answer) {
      window->shell_->ConfirmDanger(parent, std::move(title), std::move(detail), std::move(warning), std::move(action), std::move(answer));
    }, base::Unretained(this)));
    passwords_panel_->SetImportCallback(base::BindRepeating([](base::WeakPtr<WorkspaceWindow> window) { if (window) window->shell_->OpenImport(); }, weak_factory_.GetWeakPtr()));
  }
  passwords_panel_->Show(profile, widget_.get());
}
std::string WorkspaceWindow::WorkspaceForUrl(const GURL& url) const {
  if (!url.SchemeIsHTTPOrHTTPS()) return {};
  for (const auto& rule : link_rules_)
    if (rule.host == url.host() && model_.Find(rule.workspace_id)) return rule.workspace_id;
  return {};
}
bool WorkspaceWindow::SetLinkRule(const GURL& url, std::string workspace_id) {
  if (!configuration_ready_ || configuration_invalid_ || profiles_busy_ ||
      !url.SchemeIsHTTPOrHTTPS() || url.host().empty() ||
      (!workspace_id.empty() && !model_.Find(workspace_id))) return false;
  std::erase_if(link_rules_, [&](const auto& rule) { return rule.host == url.host(); });
  if (!workspace_id.empty()) link_rules_.push_back({std::string(url.host()), std::move(workspace_id)});
  UpdateShell(); SaveConfiguration();
  return true;
}
std::string WorkspaceWindow::WorkspaceForPage(content::WebContents* page) const {
  if (!page) return {};
  if (page && page == peek_contents()) return peek_workspace_id_;
  for (const auto& host : hosts_)
    for (const auto& tab : host.tabs) if (tab->web_contents() == page) return host.workspace_id;
  return {};
}
bool WorkspaceWindow::OpenExtensionPage(Profile* profile, const GURL& url) {
  return OpenProfileTab(profile, url, true) != nullptr;
}
content::WebContents* WorkspaceWindow::OpenProfileTab(Profile* profile, const GURL& url, bool active) {
  if (closing_ || !configuration_ready_ || profiles_busy_ || !profile || !url.is_valid()) return nullptr;
  // The page belongs to the profile whose extension asked for it: the space on
  // screen when it uses that profile, otherwise the first space that does.
  std::string target;
  for (const auto& host : hosts_)
    if (host.profile == profile && (target.empty() || host.workspace_id == active_id())) target = host.workspace_id;
  if (target.empty() || (active && !Select(target))) return nullptr;
  for (auto& host : hosts_) {
    if (host.workspace_id != target) continue;
    auto* view = CreatePage(host.profile);
    host.tabs.push_back(view);
    auto* page = view->GetWebContents();
    LoadWorkspaceTab(page, url, ui::PAGE_TRANSITION_AUTO_TOPLEVEL);
    if (active) SelectTab(host.tabs.size() - 1);
    else UpdateShell();
    extension_tabs_->Sync();  // The tabs API answers with the new tab's window.
    return page;
  }
  return nullptr;
}
bool WorkspaceWindow::OpenExternalUrl(const GURL& url) { return url.SchemeIsHTTPOrHTTPS() && OpenLink(url, active_id(), true); }
bool WorkspaceWindow::RunExternalCommand(std::string command) {
  if (!widget_) return false;
  const GURL requested_url(command.starts_with("url:") ? command.substr(4) : "");
  const bool url_command = requested_url.is_valid() && requested_url.SchemeIsHTTPOrHTTPS();
  if (!url_command && !command.starts_with("space:") && command != "new_tab" && command != "palette") return false;
  if (!configuration_ready_ || profiles_busy_) {
    pending_external_commands_.push_back(std::move(command));
    if (!external_command_timer_.IsRunning())
      external_command_timer_.Start(FROM_HERE, base::Milliseconds(100), this, &WorkspaceWindow::DrainExternalCommands);
    return true;
  }
  if (closing_) return false;
  widget_->Activate();
  if (url_command) return OpenExternalUrl(requested_url);
  if (command.starts_with("space:")) return Select(command.substr(6));
  if (command == "new_tab" || command == "palette") return RunShortcut(std::move(command));
  return false;
}
void WorkspaceWindow::DrainExternalCommands() {
  auto commands = std::exchange(pending_external_commands_, {});
  for (auto& command : commands) RunExternalCommand(std::move(command));
}
bool WorkspaceWindow::OpenLink(const GURL& url, std::string source_workspace, bool new_tab, bool split) {
  preconnect_timer_.Stop();
  if (peek_view_ && GuardPages({peek_contents()}, base::BindOnce([](base::WeakPtr<WorkspaceWindow> w, GURL url, std::string source, bool fresh, bool split) {
    if (w) w->OpenLink(url, source, fresh, split);
  }, weak_factory_.GetWeakPtr(), url, source_workspace, new_tab, split))) return true;
  if (!configuration_ready_ || profiles_busy_ || !IsWorkspaceTabUrl(url)) return false;
  auto target = WorkspaceForUrl(url);
  if (target.empty()) target = model_.Find(source_workspace) ? source_workspace : active_id();
  if (DeferForProfile(target, base::BindOnce(
      [](base::WeakPtr<WorkspaceWindow> w, GURL url, std::string source, bool fresh, bool split) {
        if (w) w->OpenLink(url, source, fresh, split);
      }, weak_factory_.GetWeakPtr(), url, source_workspace, new_tab, split))) return true;
  const bool switching = target != active_id();
  if (!Select(target)) return false;
  content::WebContents* previous = nullptr;
  for (const auto& host : hosts_) if (host.workspace_id == active_id() && host.view) previous = host.view->web_contents();
  if (!NavigateFromBar(url, new_tab || split || switching)) return false;
  if (split) {
    for (auto& host : hosts_) if (host.workspace_id == active_id()) {
      const size_t index = host.active_tab;
      for (const auto& tab : host.tabs) if (previous && tab->web_contents() == previous) host.view = tab;
      return SplitTab(index);
    }
  }
  return true;
}
content::WebContents* WorkspaceWindow::peek_contents() const {
  return peek_view_ ? peek_view_->GetWebContents() : nullptr;
}
bool WorkspaceWindow::image_preview_visible_for_testing(content::WebContents* page) const {
  if (peek_view_ && peek_view_->web_contents() == page)
    return static_cast<WorkspaceWebView*>(peek_view_.get())->image_preview_visible();
  for (const auto& host : hosts_) for (const auto& view : host.tabs)
    if (view->web_contents() == page) return static_cast<WorkspaceWebView*>(view.get())->image_preview_visible();
  return false;
}
bool WorkspaceWindow::OpenPeek(const GURL& url, std::string source_workspace) {
  preconnect_timer_.Stop();
  if (peek_view_ && GuardPages({peek_contents()}, base::BindOnce([](base::WeakPtr<WorkspaceWindow> w, GURL url, std::string source) {
    if (w) w->OpenPeek(url, source);
  }, weak_factory_.GetWeakPtr(), url, source_workspace))) return true;
  if (!configuration_ready_ || profiles_busy_ || !url.SchemeIsHTTPOrHTTPS()) return false;
  auto target = WorkspaceForUrl(url);
  if (target.empty()) target = source_workspace.empty() ? active_id() : source_workspace;
  const auto* workspace = model_.Find(target);
  if (!workspace) return false;
  if (DeferForProfile(target, base::BindOnce(
      [](base::WeakPtr<WorkspaceWindow> w, GURL url, std::string source) { if (w) w->OpenPeek(url, source); },
      weak_factory_.GetWeakPtr(), url, source_workspace))) return true;
  ClosePeek();
  peek_workspace_id_ = target;
  peek_view_ = CreatePage(ProfileFor(workspace->profile_id), shell_->peek_host());
  shell_->ShowPeek(base::UTF8ToUTF16(workspace->name + " · " + std::string(url.host())));
  ShowPage(peek_view_);
  LoadWorkspaceTab(peek_view_->GetWebContents(), url, ui::PAGE_TRANSITION_LINK);
  peek_view_->RequestFocus();
  UpdateShell();
  return true;
}
void WorkspaceWindow::ClosePeek() {
  if (!peek_view_) return;
  if (GuardPages({peek_contents()}, base::BindOnce(&WorkspaceWindow::ClosePeek, weak_factory_.GetWeakPtr()))) return;
  auto* view = peek_view_.get();
  peek_view_ = nullptr; peek_workspace_id_.clear();
  shell_->HidePeek();
  view->SetVisible(false); view->GetWebContents()->WasHidden();
  view->parent()->RemoveChildViewT(view);
  if (!closing_ && widget_ && widget_->IsVisible()) {
    widget_->Activate();
    if (auto* page = contents(active_id())) page->Focus();
  }
  UpdateShell();
}
bool WorkspaceWindow::PromotePeek(bool split) {
  if (!peek_view_ || !configuration_ready_ || profiles_busy_) return false;
  const auto target = peek_workspace_id_;
  auto page = peek_view_->parent()->RemoveChildViewT(peek_view_.get());
  peek_view_ = nullptr; peek_workspace_id_.clear(); shell_->HidePeek();
  if (!Select(target)) return false;
  for (auto& host : hosts_) if (host.workspace_id == target) {
    auto* view = shell_->content_host()->AddChildView(std::move(page));
    host.tabs.push_back(view);
    if (split) return SplitTab(host.tabs.size() - 1);
    SelectTab(host.tabs.size() - 1);
    return true;
  }
  return false;
}
bool WorkspaceWindow::RouteNavigation(content::NavigationHandle& navigation, const std::string& source_hint) {
  if (closing_ || !configuration_ready_ || profiles_busy_) return false;
  auto source = WorkspaceForPage(navigation.GetWebContents());
  if (source.empty() && model_.Find(source_hint)) source = source_hint;
  const auto target = WorkspaceForUrl(navigation.GetURL());
  if (source.empty() || target.empty() || target == source) return false;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
      base::BindOnce([](base::WeakPtr<WorkspaceWindow> window, GURL url, std::string source,
          base::WeakPtr<content::WebContents> original) {
        if (!window) return;
        window->OpenLink(url, source, true);
        // target=_blank creates a temporary source-profile host before its
        // first request. Remove that empty host after a routed request cancels.
        if (!original || original->GetLastCommittedURL().SchemeIsHTTPOrHTTPS()) return;
        for (const auto& host : window->hosts_) if (host.workspace_id == source)
          for (size_t i = 0; i < host.tabs.size(); ++i)
            if (host.tabs[i]->web_contents() == original.get() && host.popup_tabs.contains(host.tabs[i])) {
              window->CloseWorkspaceTab(source, i); return;
            }
      }, weak_factory_.GetWeakPtr(), navigation.GetURL(), source, navigation.GetWebContents()->GetWeakPtr()));
  return true;
}
void WorkspaceWindow::PushNightShiftConfig(Profile* profile) {
  if (!profile || is_private() || !night_shift_config_.managed) return;
  for (const auto& entry : profiles_) {
    if (!entry.lease || entry.lease->profile() != profile) continue;
    auto& pushed = night_shift_pushed_[entry.info.id];
    if (pushed == night_shift_generation_) return;
    pushed = night_shift_generation_;
    PushNightShift(profile, night_shift_config_.EnabledFor(entry.info.id), night_shift_config_,
                   base::BindOnce([](base::WeakPtr<WorkspaceWindow> window) {
                     if (window && window->extensions_bar_) window->extensions_bar_->RefreshNightShift();
                   }, weak_factory_.GetWeakPtr()));
    return;
  }
}

std::string WorkspaceWindow::ProfileIdOf(Profile* profile) const {
  for (const auto& entry : profiles_)
    if (entry.lease && entry.lease->profile() == profile) return entry.info.id;
  return {};
}

void WorkspaceWindow::ApplyNightShiftSettings(base::DictValue value) {
  if (is_private() || !configuration_ready_) return;
  auto config = NightShiftConfig::FromValue(&value);
  // Only profiles that exist; a removed one must not come back as a name.
  std::erase_if(config.profiles, [this](const std::string& id) {
    return std::ranges::none_of(profiles_, [&id](const auto& entry) { return entry.info.id == id; });
  });
  night_shift_config_ = std::move(config);
  ++night_shift_generation_;
  SaveConfiguration();
  for (const auto& entry : profiles_)
    if (entry.lease) PushNightShiftConfig(entry.lease->profile());
}

std::string WorkspaceWindow::NightShiftSettingsState() {
  base::ListValue profiles;
  const auto current = model_.active() ? model_.active()->profile_id : std::string();
  for (const auto& entry : profiles_) {
    const auto spaces = std::ranges::count_if(model_.workspaces(), [&entry](const auto& space) {
      return space.profile_id == entry.info.id;
    });
    profiles.Append(base::DictValue()
        .Set("id", entry.info.id)
        .Set("name", entry.info.name)
        .Set("spaces", static_cast<int>(spaces))
        .Set("current", entry.info.id == current));
  }
  auto state = base::DictValue().Set("profiles", std::move(profiles));
  if (night_shift_config_.managed) state.Set("config", night_shift_config_.ToValue());
  std::string json;
  base::JSONWriter::Write(state, &json);
  return json;
}

content::WebContents* WorkspaceWindow::OpenPageLink(content::WebContents* source,
    const content::OpenURLParams& params) {
  // The editable menu's spelling "Language settings" opens Chrome's settings
  // page, which Zitter does not load: open its own spelling preferences.
  if (params.url.SchemeIs(content::kChromeUIScheme) && params.url.host() == "settings") {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
        base::BindOnce([](base::WeakPtr<WorkspaceWindow> window) {
          if (window && window->shell_) window->shell_->OpenGeneralSettings(T(u"Sprawdzaj pisownię"));
        }, weak_factory_.GetWeakPtr()));
    return nullptr;
  }
  if (!params.url.SchemeIsHTTPOrHTTPS() || params.post_data) return nullptr;
  const auto workspace = WorkspaceForPage(source);
  if (workspace.empty()) return nullptr;
  if (shift_click_peek_ && params.user_gesture && params.disposition == WindowOpenDisposition::NEW_WINDOW &&
      ui::PageTransitionCoreTypeIs(params.transition, ui::PAGE_TRANSITION_LINK)) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
        base::BindOnce([](base::WeakPtr<WorkspaceWindow> window, GURL url, std::string workspace) {
          if (window) window->OpenPeek(url, workspace);
        }, weak_factory_.GetWeakPtr(), params.url, workspace));
    return nullptr;
  }
  const bool new_tab = params.disposition != WindowOpenDisposition::CURRENT_TAB;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
      base::BindOnce([](base::WeakPtr<WorkspaceWindow> window, GURL url, std::string workspace, bool new_tab, bool background) {
        if (!window) return;
        const auto target = window->WorkspaceForUrl(url);
        // Cmd+click: a new tab that loads behind the current one, announced by a toast.
        if (background && (target.empty() || target == workspace)) {
          for (auto& host : window->hosts_) if (host.workspace_id == workspace) {
            auto* view = window->CreatePage(host.profile);
            host.tabs.push_back(view);
            LoadWorkspaceTab(view->GetWebContents(), url, ui::PAGE_TRANSITION_LINK);
            window->UpdateShell();
            std::string site(url.host());
            if (base::StartsWith(site, "www.")) site = site.substr(4);
            window->shell_->ShowToast(T(u"Otwarto w tle: "), base::UTF8ToUTF16(site), base::BindOnce([](base::WeakPtr<WorkspaceWindow> w, std::string id, base::WeakPtr<content::WebContents> page) {
              if (!w || !page || !w->Select(id)) return;
              for (const auto& host : w->hosts_) if (host.workspace_id == id)
                for (size_t i = 0; i < host.tabs.size(); ++i) if (host.tabs[i]->web_contents() == page.get()) { w->SelectTab(i); return; }
            }, window->weak_factory_.GetWeakPtr(), workspace, view->GetWebContents()->GetWeakPtr()), T(u"Pokaż"));
            return;
          }
        }
        window->OpenLink(url, workspace, new_tab);
      }, weak_factory_.GetWeakPtr(), params.url, workspace, new_tab,
          params.disposition == WindowOpenDisposition::NEW_BACKGROUND_TAB));
  return nullptr;
}
void WorkspaceWindow::ShowPageMenu(content::WebContents* source, content::RenderFrameHost* frame, const content::ContextMenuParams& params) {
  const auto url = params.link_url.SchemeIsHTTPOrHTTPS() ? params.link_url : GURL();
  auto& controller = source->GetController();
  PageMenuNavigation navigation{
      .url = source->GetLastCommittedURL(),
      .can_go_back = controller.CanGoBack(),
      .can_go_forward = controller.CanGoForward(),
      .can_reload = true,
      .run = base::BindRepeating(
          [](base::WeakPtr<WorkspaceWindow> window, base::WeakPtr<content::WebContents> page,
             PageCommand command) {
            if (!window || !page) return;
            const auto* focused = window->peek_contents() ? window->peek_contents()
                                                          : window->contents(window->active_id());
            window->RunPageCommandOn(page.get(), command, focused == page.get());
          }, weak_factory_.GetWeakPtr(), source->GetWeakPtr())};
  shell_->ShowLinkMenu(url, WorkspaceForPage(source), source->GetContainerBounds().origin() + gfx::Vector2d(params.x, params.y),
      DevToolsWindow::AllowDevToolsFor(Profile::FromBrowserContext(source->GetBrowserContext()), source) ? base::BindRepeating([](content::WeakDocumentPtr document, int x, int y) {
        auto* target = document.AsRenderFrameHostIfValid();
        if (!target) return;
        auto* page = content::WebContents::FromRenderFrameHost(target);
        if (DevToolsWindow::AllowDevToolsFor(Profile::FromBrowserContext(page->GetBrowserContext()), page)) DevToolsWindow::InspectElement(target, x, y);
      }, frame->GetWeakDocumentPtr(), params.x, params.y) : base::RepeatingClosure(), std::move(navigation));
}

void WorkspaceWindow::ActivatePaletteResult(PaletteResult result) {
  if (!result.command_id.empty()) { ExecuteCommand(result); return; }
  if (peek_view_ && GuardPages({peek_contents()}, base::BindOnce(&WorkspaceWindow::ActivatePaletteResult, weak_factory_.GetWeakPtr(), result))) return;
  if (DeferForProfile(result.workspace_id, base::BindOnce(
      &WorkspaceWindow::ActivatePaletteResult, weak_factory_.GetWeakPtr(), result))) return;
  if (!Select(result.workspace_id)) return;
  if (result.tab) {
    for (const auto& host : hosts_) {
      if (host.workspace_id != active_id()) continue;
      auto matches = [&](size_t i) {
        const auto url = TabUrl(host, host.tabs[i]);
        return url == result.url;
      };
      if (*result.tab < host.tabs.size() && matches(*result.tab)) { SelectTab(*result.tab); return; }
      for (size_t i = 0; i < host.tabs.size(); ++i) if (matches(i)) { SelectTab(i); return; }
    }
    if (IsWorkspaceTabUrl(result.url)) NavigateFromBar(result.url, true);
  } else if (!result.pin_id.empty()) OpenPinnedItem(result.pin_id);
}
void WorkspaceWindow::SearchPalette(std::u16string query) {
  history_tasks_.TryCancelAll();
  const auto generation = ++search_generation_;
  SchedulePreconnect(query);
  const auto* workspace = model_.Find(active_id());
  if (!workspace) return;
  const auto profile_id = workspace->profile_id;
  if (query.starts_with(u">")) { shell_->SetSearchResults(query, profile_id, {}); return; }
  const auto needle = base::i18n::ToLower(query);
  auto matches = [&](const std::u16string& title, const GURL& url) {
    return needle.empty() || base::i18n::ToLower(title).find(needle) != std::u16string::npos ||
        base::i18n::ToLower(base::UTF8ToUTF16(url.spec())).find(needle) != std::u16string::npos;
  };
  std::vector<PaletteResult> results;
  std::set<std::string> open_urls;
  for (const auto& host : hosts_) {
    const auto label = base::UTF8ToUTF16(model_.Find(host.workspace_id)->name);
    for (size_t i = 0; i < host.tabs.size() && results.size() < 8; ++i) {
      const auto& view = host.tabs[i];
      const auto url = TabUrl(host, view);
      if (!IsWorkspaceTabUrl(url)) continue;
      auto title = TabTitle(host, view);
      if (title.empty()) title = base::UTF8ToUTF16(url.host());
      if (!matches(title + u" " + label, url)) continue;
      if (needle.empty() && host.workspace_id == active_id() && i == host.active_tab) { open_urls.insert(url.spec()); continue; }
      results.push_back({title, ResultDetail(0, label), url, i, host.workspace_id});
      open_urls.insert(url.spec());
    }
  }
  for (const auto& candidate : model_.workspaces()) {
    const auto label = base::UTF8ToUTF16(candidate.name);
    std::map<std::string, const PinnedItem*> by_id;
    for (const auto& pin : pinned_items(candidate.id)) by_id[pin.id] = &pin;
    for (const auto& pin : pinned_items(candidate.id)) {
      if (results.size() >= 12) break;
      if (pin.folder || open_urls.contains(pin.url.spec())) continue;
      std::u16string path;
      std::string parent = pin.parent_id;
      for (int depth = 0; !parent.empty() && depth < 64; ++depth) {
        auto found = by_id.find(parent);
        if (found == by_id.end()) break;
        path = u" / " + base::UTF8ToUTF16(found->second->title) + path;
        parent = found->second->parent_id;
      }
      if (!matches(base::UTF8ToUTF16(pin.title) + u" " + label + path, pin.url)) continue;
      results.push_back({base::UTF8ToUTF16(pin.title), ResultDetail(1, label + path), pin.url, std::nullopt, candidate.id, pin.id});
      open_urls.insert(pin.url.spec());
    }
    if (candidate.id != active_id() && results.size() < 16 && matches(label, GURL()))
      results.push_back({label, ResultDetail(2, label), GURL(), std::nullopt, candidate.id, "", true});
  }
  auto* service = is_private() ? nullptr : HistoryServiceFactory::GetForProfile(ProfileFor(profile_id), ServiceAccessType::EXPLICIT_ACCESS);
  shell_->SetSearchResults(query, profile_id, results, /*complete=*/!service);
  if (!service) return;
  history::QueryOptions options;
  options.max_count = 12;
  options.policy_for_404_visits = history::VisitQuery404sPolicy::kExclude404s;
  service->QueryHistory(query, options, base::BindOnce(
      [](base::WeakPtr<WorkspaceWindow> window, size_t generation, std::u16string query,
          std::string profile_id, std::vector<PaletteResult> results, std::set<std::string> seen,
          history::QueryResults history) {
        if (!window || generation != window->search_generation_ || !window->model_.active() ||
            window->model_.active()->profile_id != profile_id) return;
        for (const auto& row : history) {
          if (!row.url().SchemeIsHTTPOrHTTPS() || !seen.insert(row.url().spec()).second) continue;
          results.push_back({row.title().empty() ? base::UTF8ToUTF16(row.url().host()) : row.title(),
              ResultDetail(3, base::UTF8ToUTF16(row.url().host())), row.url(), std::nullopt});
        }
        window->shell_->SetSearchResults(std::move(query), std::move(profile_id), std::move(results));
      }, weak_factory_.GetWeakPtr(), generation, query, profile_id, std::move(results), std::move(open_urls)), &history_tasks_);
}

size_t WorkspaceWindow::tab_count(std::string_view id) const {
  for (const auto& host : hosts_) if (host.workspace_id == id) return host.tabs.size();
  return 0;
}

void WorkspaceWindow::SelectTab(size_t index) {
  pending_profile_action_.Reset();
  if (peek_view_ && GuardPages({peek_contents()}, base::BindOnce(&WorkspaceWindow::SelectTab, weak_factory_.GetWeakPtr(), index))) return;
  ClosePeek();
  for (auto& host : hosts_) {
    if (host.workspace_id != active_id() || index >= host.tabs.size()) continue;
    auto* selected = host.tabs[index].get();
    // The group stays together: a tab outside it is shown alone, and selecting
    // any member brings the whole split back.
    host.active_tab = index;
    host.view = selected;
    RefreshPanes();
    shell_->CloseOverlays(true);
    UpdateShell();
    widget_->Activate();
    host.view->RequestFocus();
    return;
  }
}

void WorkspaceWindow::NoteRecentTab() {
  if (tab_cycle_) return;  // Tabs passed on the way are not "used".
  for (auto& host : hosts_) {
    if (host.workspace_id != active_id() || !host.view) continue;
    if (!host.recent.empty() && host.recent.front() == host.view) return;
    std::erase(host.recent, host.view.get());
    host.recent.insert(host.recent.begin(), host.view.get());
    // Closed tabs leave stale entries behind; keep the list bounded.
    if (host.recent.size() > host.tabs.size() + 32) host.recent = RecentTabs(host);
    return;
  }
}

std::vector<views::WebView*> WorkspaceWindow::RecentTabs(const Host& host) const {
  std::vector<views::WebView*> order;
  const auto open = [&host](views::WebView* view) {
    return std::ranges::any_of(host.tabs, [view](const auto& tab) { return tab.get() == view; });
  };
  // The shown tab always leads, even before it was ever noted.
  if (host.view) order.push_back(host.view.get());
  for (auto* view : host.recent)
    if (open(view) && std::ranges::find(order, view) == order.end()) order.push_back(view);
  // Tabs never shown in this run follow in sidebar order.
  for (const auto& tab : host.tabs)
    if (std::ranges::find(order, tab.get()) == order.end()) order.push_back(tab.get());
  return order;
}

void WorkspaceWindow::CycleRecentTab(bool backwards) {
  Host* host = nullptr;
  for (auto& candidate : hosts_) if (candidate.workspace_id == active_id()) host = &candidate;
  if (!host || host->tabs.size() < 2) return;
  if (!tab_cycle_ || tab_cycle_->workspace_id != active_id()) {
    NoteRecentTab();
    tab_cycle_ = TabCycle{std::string(active_id()), RecentTabs(*host), 0, {}};
  }
  auto& cycle = *tab_cycle_;
  const size_t count = cycle.order.size();
  cycle.position = backwards ? (cycle.position + count - 1) % count : (cycle.position + 1) % count;
  cycle.last_step = base::TimeTicks::Now();
  const auto found = std::ranges::find_if(host->tabs, [&cycle](const auto& tab) {
    return tab.get() == cycle.order[cycle.position];
  });
  if (found == host->tabs.end()) { FinishTabCycle(); return; }
  SelectTab(found - host->tabs.begin());
  if (!tab_cycle_timer_.IsRunning())
    tab_cycle_timer_.Start(FROM_HERE, base::Milliseconds(50), this, &WorkspaceWindow::CheckTabCycle);
}

void WorkspaceWindow::CheckTabCycle() {
  if (!tab_cycle_) { tab_cycle_timer_.Stop(); return; }
#if BUILDFLAG(IS_WIN)
  // Ctrl usually goes up over the page, but it can land anywhere: ask Windows.
  const bool held = ::GetKeyState(VK_CONTROL) & 0x8000;
#else
  // The page reports Ctrl's release; this only ends a cycle whose keyup was lost.
  const bool held = base::TimeTicks::Now() - tab_cycle_->last_step < base::Milliseconds(1500);
#endif
  if (!held || tab_cycle_->workspace_id != active_id()) FinishTabCycle();
}

void WorkspaceWindow::FinishTabCycle() {
  tab_cycle_timer_.Stop();
  if (!tab_cycle_) return;
  tab_cycle_.reset();
  NoteRecentTab();
}

void WorkspaceWindow::CloseTab(size_t index) { CloseWorkspaceTab(active_id(), index); }
void WorkspaceWindow::CloseWorkspaceTab(std::string_view workspace_id, size_t index) {
  if (is_private() && hosts_.size() == 1 && hosts_[0].tabs.size() == 1 && index == 0) { RequestClose(); return; }
  for (auto& host : hosts_) {
    if (host.workspace_id != workspace_id || index >= host.tabs.size() || host.tabs.size() < 2) {
      continue;
    }
    auto* page = host.tabs[index]->web_contents();
    if (page && GuardPages({page}, base::BindOnce([](base::WeakPtr<WorkspaceWindow> w, std::string id, base::WeakPtr<content::WebContents> page) {
      if (!w || !page) return;
      for (const auto& host : w->hosts_) if (host.workspace_id == id)
        for (size_t i = 0; i < host.tabs.size(); ++i) if (host.tabs[i]->web_contents() == page.get()) { w->CloseWorkspaceTab(id, i); return; }
    }, weak_factory_.GetWeakPtr(), std::string(workspace_id), page->GetWeakPtr()))) return;
    if (configuration_ready_ && !restoring_session_ && !closing_) {
      if (!closing_split_tabs_) RecordTimeline(true, host.workspace_id);
      RecordClosedTab(host, index);
    }
    views::WebView* closing = host.tabs[index];
    std::erase_if(host.pin_tabs, [closing](const auto& entry) { return entry.second == closing; });
    const bool was_active = closing == host.view;
    if (host.workspace_id == active_id()) shell_->SetPanes({}, {});
    if (auto* survivor = LeaveSplit(host, closing)) {
      // Closing a member of a hidden group must not pull its partners on screen.
      if (was_active) host.view = survivor;
    }
    host.deferred_urls.erase(closing);
    host.deferred_titles.erase(closing);
    host.popup_tabs.erase(closing);
    host.tabs.erase(host.tabs.begin() + index);
    if (host.active_tab > index || host.active_tab >= host.tabs.size()) --host.active_tab;
    if (was_active) {
      if (host.view == closing || !host.view) host.view = host.tabs[host.active_tab];
    }
    host.active_tab = std::find(host.tabs.begin(), host.tabs.end(), host.view) - host.tabs.begin();
    shell_->content_host()->RemoveChildViewT(closing);
    RefreshPanes();
    UpdateShell();
    if (was_active && host.workspace_id == active_id()) host.view->RequestFocus();
    return;
  }
}

// A tab moves to another space of the same profile (the tabs API's window
// spans them all). Its page, history and storage stay; it leaves any split and
// its pin, as a closed tab would.
namespace {

bool IsWorkspaceFaviconUrl(const GURL& icon) {
  return icon.SchemeIsHTTPOrHTTPS() || icon.SchemeIs(url::kDataScheme);
}

// Inspect markup without running scripts or loading other page resources.
std::vector<GURL> WorkspaceIconCandidates(std::string_view html, const GURL& page_url) {
  static const base::NoDestructor<RE2> tag(
      R"re((?is)<!--.*?-->|<(?:script|style|title|textarea)\b[^>]*>.*?</(?:script|style|title|textarea)\s*>|<(link|base)\b((?:"[^"]*"|'[^']*'|[^'">])*)>)re");
  static const base::NoDestructor<RE2> attribute(
      R"re(([^\s=/"'<>]+)(?:\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s"'=<>]+)))?)re");
  constexpr size_t kMaxDeclaredIcons = 6;
  std::vector<std::string> paths;
  GURL base_url = page_url;
  bool has_base = false;
  std::string_view name, attributes;
  while (RE2::FindAndConsume(&html, *tag, &name, &attributes)) {
    if (name.empty()) continue;
    std::string_view key, quoted, single_quoted, unquoted, href, rel;
    while (RE2::FindAndConsume(&attributes, *attribute, &key, &quoted, &single_quoted, &unquoted)) {
      const auto value = !quoted.empty() ? quoted : !single_quoted.empty() ? single_quoted : unquoted;
      if (base::EqualsCaseInsensitiveASCII(key, "href") && href.empty()) href = value;
      if (base::EqualsCaseInsensitiveASCII(key, "rel") && rel.empty()) rel = value;
    }
    if (href.empty()) continue;
    std::string path = base::UTF16ToUTF8(base::UnescapeForHTML(base::UTF8ToUTF16(href)));
    if (base::EqualsCaseInsensitiveASCII(name, "base")) {
      if (!has_base) {
        const GURL candidate = page_url.Resolve(path);
        if (candidate.SchemeIsHTTPOrHTTPS()) base_url = candidate;
        has_base = true;
      }
    } else if (paths.size() < kMaxDeclaredIcons) {
      const auto lower_rel = base::ToLowerASCII(rel);
      const auto relations = base::SplitStringPiece(lower_rel, base::kWhitespaceASCII,
                                                     base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
      if (std::ranges::find(relations, "icon") != relations.end())
        paths.push_back(std::move(path));
    }
  }
  std::vector<GURL> icons;
  for (const auto& path : paths) {
    GURL url = base_url.Resolve(path);
    if (IsWorkspaceFaviconUrl(url) && std::ranges::find(icons, url) == icons.end())
      icons.push_back(std::move(url));
  }
  return icons;
}

}  // namespace

bool WorkspaceWindow::ReloadWorkspaceFavicons(std::string workspace) {
  if (!configuration_ready_ || configuration_invalid_ || profiles_busy_ || closing_ || !model_.Find(workspace)) return false;
  if (workspace != active_id() && (!Select(workspace) || active_id() != workspace)) return false;
  for (const auto& host : hosts_) {
    if (host.workspace_id != workspace) continue;
    auto* source = contents(workspace);
    if (!source) return false;
    // Only an unrealized empty page needs initialization; loaded pages keep
    // their document, forms and navigation history throughout the refresh.
    if (!source->GetPrimaryMainFrame()->IsRenderFrameLive() &&
        (source->GetVisibleURL().is_empty() || source->GetVisibleURL() == GURL("about:blank")))
      source->GetController().LoadURL(GURL("about:blank"), content::Referrer(), ui::PAGE_TRANSITION_AUTO_TOPLEVEL, {});
    std::map<GURL,GURL> pages;
    for (const auto& view : host.tabs) {
      const auto url = TabUrl(host, view);
      if (!url.SchemeIsHTTPOrHTTPS()) continue;
      auto& icon = pages[url];
      if (auto* page = view->web_contents()) {
        if (page->GetVisibleURL() != url || icon.is_valid()) continue;
        if (auto* entry = page->GetController().GetVisibleEntry()) icon = entry->GetFavicon().url;
        if (!icon.is_valid()) for (const auto& candidate : page->GetFaviconURLs())
          if (candidate->icon_url.is_valid()) { icon = candidate->icon_url; break; }
      }
    }
    for (const auto& pin : pinned_items(workspace)) if (!pin.folder && pin.url.SchemeIsHTTPOrHTTPS()) pages.try_emplace(pin.url);
    auto* service = ImplicitFaviconService(host.profile);
    if (service) service->ClearUnableToDownloadFavicons();
    for (const auto& [url, known_icon] : pages)
      StartWorkspaceFavicon(host.profile, host.profile_id, source, url, known_icon, true);
    UpdateShell();
    return true;
  }
  return false;
}

void WorkspaceWindow::QueueImportedFavicons(const ArcImport& source) {
  if (!source.options.bookmarks) return;
  ImportedFaviconTargets targets;
  auto queue = [&](const std::string& storage, const std::string& profile_id) {
    const auto items = pins_.find(storage);
    if (items == pins_.end()) return;
    for (const auto& item : items->second) {
      if (!item.folder && item.url.SchemeIsHTTPOrHTTPS() &&
          !item.url.has_username() && !item.url.has_password()) {
        targets[profile_id].insert(item.url);
      }
    }
  };
  for (const auto& space : source.spaces) {
    if (const auto* workspace = model_.Find(space.id);
        workspace && workspace->profile_id == space.profile_id)
      queue(space.id, workspace->profile_id);
  }
  for (const auto& profile : source.profiles) queue("@profile:" + profile.id, profile.id);
  if (targets.empty()) return;
  auto request = std::make_shared<ImportedFaviconTargets>(std::move(targets));
  ++import_favicon_reads_;
  config_runner_->PostTaskAndReplyWithResult(FROM_HERE, base::BindOnce(
      [](std::vector<ImportSourceProfile> sources, std::shared_ptr<const ImportedFaviconTargets> targets,
         ImportBrowser browser) {
        return ReadImportedFavicons(sources, *targets, browser);
      }, source.sources, request, source.options.browser), base::BindOnce(
      [](base::WeakPtr<WorkspaceWindow> window, std::shared_ptr<ImportedFaviconTargets> targets,
         std::vector<ImportedFavicon> icons) {
        if (!window) return;
        --window->import_favicon_reads_;
        if (window->closing_) return;
        for (const auto& icon : icons) {
          const FaviconKey key{icon.profile_id, icon.page_url.spec()};
          auto* profile = window->ProfileFor(icon.profile_id);
          if (!profile || window->favicon_refreshing_.contains(key) || window->favicon_cache_.contains(key)) continue;
          auto image = gfx::Image::CreateFrom1xBitmap(icon.bitmap);
          auto pixels = FaviconPixels(image);
          if (pixels.empty()) continue;
          window->favicon_cache_.emplace(key, std::move(pixels));
          if (auto* service = ImplicitFaviconService(profile))
            service->SetFavicons({icon.page_url}, icon.icon_url, favicon_base::IconType::kFavicon, image);
        }
        for (const auto& [profile, pages] : *targets)
          for (const auto& page : pages)
            window->import_favicon_queue_.emplace(profile, page.spec());
        window->UpdateShell();
        window->PumpImportedFavicons();
      }, weak_factory_.GetWeakPtr(), request));
}

void WorkspaceWindow::PumpImportedFavicons() {
  if (closing_) return;
  // Bound network and decoder work when a large import commits all at once.
  constexpr size_t kConcurrentFavicons = 4;
  while (!import_favicon_queue_.empty() && favicon_refreshing_.size() < kConcurrentFavicons) {
    auto key = import_favicon_queue_.extract(import_favicon_queue_.begin()).value();
    if (favicon_refreshing_.contains(key) || favicon_cache_.contains(key)) continue;
    auto* profile = ProfileFor(key.first);
    if (!profile) continue;
    auto& source = import_favicon_sources_[key.first];
    if (!source.page) {
      source.lease = ScopedProfileKeepAlive::TryAcquire(profile, ProfileKeepAliveOrigin::kBrowserWindow);
      if (!source.lease) continue;
      content::WebContents::CreateParams params(profile);
      params.initially_hidden = true;
      source.page = content::WebContents::Create(params);
      source.page->GetController().LoadURL(GURL("about:blank"), content::Referrer(),
                                           ui::PAGE_TRANSITION_AUTO_TOPLEVEL, {});
    }
    StartWorkspaceFavicon(profile, key.first, source.page.get(), GURL(key.second), {}, false);
  }
  std::erase_if(import_favicon_sources_, [&](const auto& entry) {
    const FaviconKey first{entry.first, {}};
    const auto waiting = import_favicon_queue_.lower_bound(first);
    const auto loading = favicon_refreshing_.lower_bound(first);
    return (waiting == import_favicon_queue_.end() || waiting->first != entry.first) &&
           (loading == favicon_refreshing_.end() || loading->first.first != entry.first);
  });
}

void WorkspaceWindow::StartWorkspaceFavicon(Profile* profile, std::string profile_id,
    content::WebContents* source, GURL url, GURL known_icon, bool force) {
  const FaviconKey key{std::move(profile_id), url.spec()};
  const uint64_t generation = ++favicon_generations_[key];
  favicon_refreshing_.insert_or_assign(key, force);
  favicon_cache_.erase(key);
  favicon_requests_.insert(key);
  auto download = base::BindOnce(
      [](base::WeakPtr<WorkspaceWindow> window, base::WeakPtr<content::WebContents> source,
         FaviconKey key, uint64_t generation, GURL known_icon, bool force,
         const favicon_base::FaviconImageResult& result) {
        if (!window || window->closing_ || window->favicon_generations_[key] != generation) return;
        if (!force && !result.image.IsEmpty()) {
          window->FinishWorkspaceFavicon(std::move(key), generation, result.icon_url, result.image);
          return;
        }
        auto* profile = window->ProfileFor(key.first);
        if (!known_icon.is_valid()) known_icon = result.icon_url;
        if (!force) {
          // Automatic imports fetch only icon resources. Manual reload can
          // also discover linked icons by requesting the page's markup.
          std::vector<GURL> icons;
          if (IsWorkspaceFaviconUrl(known_icon)) icons.push_back(known_icon);
          const GURL fallback = GURL(key.second).Resolve("/favicon.ico");
          if (IsWorkspaceFaviconUrl(fallback) && fallback != known_icon) icons.push_back(fallback);
          window->DownloadWorkspaceFavicon(source, std::move(key), generation, std::move(icons));
          return;
        }
        window->FetchWorkspaceFavicon(profile, source, std::move(key), generation, std::move(known_icon));
      }, weak_factory_.GetWeakPtr(), source->GetWeakPtr(), key, generation, std::move(known_icon), force);
  if (auto* service = ImplicitFaviconService(profile)) {
    if (force) service->SetFaviconOutOfDateForPage(url);
    service->GetFaviconImageForPageURL(url, std::move(download), &favicon_tasks_);
  } else {
    std::move(download).Run(favicon_base::FaviconImageResult());
  }
}

void WorkspaceWindow::FetchWorkspaceFavicon(
    Profile* profile, base::WeakPtr<content::WebContents> source,
    FaviconKey key, uint64_t generation, GURL known_icon) {
  if (!profile) {
    FinishWorkspaceFavicon(std::move(key), generation, {}, {});
    return;
  }
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GURL(key.second);
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->load_flags = net::LOAD_BYPASS_CACHE | net::LOAD_DO_NOT_SAVE_COOKIES;
  constexpr auto annotation = net::DefineNetworkTrafficAnnotation(
      "zitter_workspace_favicon_discovery", R"(
    semantics {
      sender: "Zitter workspace icon refresh"
      description: "Fetches markup to discover favicon URLs for tabs and pinned pages."
      trigger: "The user explicitly refreshes favicons."
      data: "The page URL, without cookies or authentication."
      destination: WEBSITE
    }
    policy {
      cookies_allowed: NO
      setting: "This request only follows a user-initiated refresh."
      policy_exception_justification: "User initiated icon refresh."
    })");
  auto loader = network::SimpleURLLoader::Create(std::move(request), annotation);
  loader->SetTimeoutDuration(base::Seconds(10));
  loader->SetAllowPartialResults(true);
  auto factory = profile->GetDefaultStoragePartition()->GetURLLoaderFactoryForBrowserProcess();
  auto* loading = loader.get();
  loading->DownloadToString(factory.get(),
      base::BindOnce(&WorkspaceWindow::DiscoverWorkspaceFavicon,
                     weak_factory_.GetWeakPtr(), source, std::move(key), generation,
                     std::move(known_icon), std::move(loader)), 512 * 1024);
}

void WorkspaceWindow::DiscoverWorkspaceFavicon(
    base::WeakPtr<content::WebContents> source, FaviconKey key, uint64_t generation,
    GURL known_icon, std::unique_ptr<network::SimpleURLLoader> loader,
    std::optional<std::string> markup) {
  if (closing_ || favicon_generations_[key] != generation) return;
  const GURL& final_url = loader->GetFinalURL();
  const GURL page_url = final_url.SchemeIsHTTPOrHTTPS() ? final_url : GURL(key.second);
  auto icons = markup ? WorkspaceIconCandidates(*markup, page_url) : std::vector<GURL>{};
  for (const GURL& candidate : {known_icon, page_url.Resolve("/favicon.ico")}) {
    if (IsWorkspaceFaviconUrl(candidate) && std::ranges::find(icons, candidate) == icons.end())
      icons.push_back(candidate);
  }
  DownloadWorkspaceFavicon(source, std::move(key), generation, std::move(icons));
}

void WorkspaceWindow::DownloadWorkspaceFavicon(
    base::WeakPtr<content::WebContents> source, FaviconKey key, uint64_t generation,
    std::vector<GURL> icons, size_t index, size_t renderer_attempt) {
  if (closing_ || favicon_generations_[key] != generation || !favicon_refreshing_.contains(key)) return;
  if (!source || index == icons.size()) {
    FinishWorkspaceFavicon(std::move(key), generation, {}, {});
    return;
  }
  if (!source->GetPrimaryMainFrame()->IsRenderFrameLive()) {
    if (renderer_attempt < 40) {
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&WorkspaceWindow::DownloadWorkspaceFavicon, weak_factory_.GetWeakPtr(),
                         source, std::move(key), generation, std::move(icons),
                         index, renderer_attempt + 1), base::Milliseconds(50));
    } else {
      FinishWorkspaceFavicon(std::move(key), generation, {}, {});
    }
    return;
  }
  const GURL icon = icons[index];
  source->DownloadImage(icon, true, gfx::Size(32, 32), 64, true, base::BindOnce(
      [](base::WeakPtr<WorkspaceWindow> window, base::WeakPtr<content::WebContents> source,
         FaviconKey key, uint64_t generation, std::vector<GURL> icons, size_t index,
         int, int status, const GURL& icon, const std::vector<SkBitmap>& bitmaps,
         const std::vector<gfx::Size>&) {
        if (!window) return;
        if (status < 400 && !bitmaps.empty() && !bitmaps.front().drawsNothing()) {
          window->FinishWorkspaceFavicon(std::move(key), generation, icon,
                                         gfx::Image::CreateFrom1xBitmap(bitmaps.front()));
        } else {
          window->DownloadWorkspaceFavicon(source, std::move(key), generation,
                                           std::move(icons), index + 1);
        }
      }, weak_factory_.GetWeakPtr(), source, std::move(key), generation, std::move(icons), index));
}

void WorkspaceWindow::FinishWorkspaceFavicon(FaviconKey key,uint64_t generation,GURL icon,gfx::Image image) {
  if (closing_ || favicon_generations_[key] != generation) return;
  const auto refresh = favicon_refreshing_.extract(key);
  if (refresh.empty()) return;
  if (!image.IsEmpty() || refresh.mapped()) {
    auto pixels = FaviconPixels(image);
    if (!pixels.empty()) favicon_cache_[key] = std::move(pixels);
    else favicon_cache_.erase(key);
    for (const auto& host : hosts_) if (host.profile_id == key.first) for (const auto& view : host.tabs) {
      auto* page = view->web_contents();
      auto* entry = page ? page->GetController().GetVisibleEntry() : nullptr;
      if (!entry || page->GetVisibleURL().spec() != key.second) continue;
      auto& favicon = entry->GetFavicon();
      favicon.valid = !image.IsEmpty(); favicon.url = icon; favicon.image = image;
    }
    if (auto* profile = ProfileFor(key.first)) if (auto* service = ImplicitFaviconService(profile)) {
      const base::flat_set<GURL> pages{GURL(key.second)};
      if (!image.IsEmpty()) service->SetFavicons(pages,icon,favicon_base::IconType::kFavicon,image);
      else service->DeleteFaviconMappings(pages,favicon_base::IconType::kFavicon);
    }
  } else {
    // A background miss must not erase an icon discovered by an open tab.
    favicon_requests_.erase(key);
  }
  if (model_.active() && model_.active()->profile_id == key.first) UpdateShell();
  if (!import_favicon_queue_.empty() || !import_favicon_sources_.empty()) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&WorkspaceWindow::PumpImportedFavicons, weak_factory_.GetWeakPtr()));
  }
}

bool WorkspaceWindow::DuplicatePin(std::string id) {
  if (!configuration_ready_ || configuration_invalid_ || profiles_busy_ || closing_) return false;
  for (auto& host : hosts_) if (host.workspace_id == active_id()) {
    if (auto found=host.pin_tabs.find(id);found!=host.pin_tabs.end())
      return DuplicateTab(std::find(host.tabs.begin(),host.tabs.end(),found->second)-host.tabs.begin());
    const auto& pins=pinned_items(active_id());
    const auto pin=std::ranges::find(pins,id,&PinnedItem::id);
    if(pin==pins.end()||pin->folder)return false;
    const GURL url=pin->url;
    auto* view=CreatePage(host.profile);host.tabs.push_back(view);
    SelectTab(host.tabs.size()-1);LoadWorkspaceTab(view->GetWebContents(),url,ui::PAGE_TRANSITION_AUTO_BOOKMARK);
    return true;
  }
  return false;
}

bool WorkspaceWindow::DuplicateTab(size_t index) {
  if (!configuration_ready_ || configuration_invalid_ || profiles_busy_ || closing_) return false;
  for (auto& host : hosts_) {
    if (host.workspace_id != active_id() || index >= host.tabs.size()) continue;
    auto* source = EnsurePage(host.tabs[index]);
    auto* duplicate = CreatePage(host.profile);
    if (auto deferred = host.deferred_urls.find(host.tabs[index]); deferred != host.deferred_urls.end()) {
      host.deferred_urls[duplicate] = deferred->second;
      host.deferred_titles[duplicate] = TabTitle(host, host.tabs[index]);
    } else duplicate->GetWebContents()->GetController().CopyStateFrom(&source->GetController(), true);
    host.tabs.insert(host.tabs.begin() + index + 1, duplicate);
    SelectTab(index + 1);
    duplicate->GetWebContents()->GetController().LoadIfNecessary();
    UpdateShell();
    return true;
  }
  return false;
}

bool WorkspaceWindow::MoveSidebarTab(size_t index, std::string target) {
  if (!configuration_ready_ || configuration_invalid_ || profiles_busy_ || closing_) return false;
  const auto* space = model_.Find(target);
  if (!space || target == active_id()) return false;
  for (auto& host : hosts_) {
    if (host.workspace_id != active_id() || index >= host.tabs.size()) continue;
    if (host.profile_id != space->profile_id) {
      auto* source=host.tabs[index].get();
      const GURL url=TabUrl(host,source);const auto title=TabTitle(host,source);
      if(!IsWorkspaceTabUrl(url))return false;
      if(GuardPages({source->web_contents()},base::BindOnce([](base::WeakPtr<WorkspaceWindow> window,std::string workspace,size_t index,std::string target){
        if(window&&window->active_id()==workspace)window->MoveSidebarTab(index,std::move(target));
      },weak_factory_.GetWeakPtr(),active_id(),index,target)))return true;
      for(auto& destination:hosts_)if(destination.workspace_id==target){
        if(destination.sleeping){auto stored=destination.sleeping->Clone();if(!RestoreHost(destination,stored))return false;}
        auto* moved=CreatePage(destination.profile,nullptr,nullptr,false);
        destination.deferred_urls[moved]=url;destination.deferred_titles[moved]=title;destination.tabs.push_back(moved);
        if(host.tabs.size()==1)host.tabs.push_back(CreatePage(host.profile));
        CloseTab(index);UpdateShell();return true;
      }
      return false;
    }
    // Keep an empty page behind when moving the final tab out of a space.
    if (host.tabs.size() == 1) host.tabs.push_back(CreatePage(host.profile));
    return MoveTabToSpace(EnsurePage(host.tabs[index]), target, SIZE_MAX);
  }
  return false;
}

bool WorkspaceWindow::MoveTabToSpace(content::WebContents* page, const std::string& target, size_t position) {
  if (!configuration_ready_ || profiles_busy_ || closing_ || !page) return false;
  Host* from = nullptr;
  Host* to = nullptr;
  size_t index = 0;
  for (auto& host : hosts_) {
    if (host.workspace_id == target) to = &host;
    for (size_t i = 0; i < host.tabs.size(); ++i)
      if (host.tabs[i]->web_contents() == page) { from = &host; index = i; }
  }
  if (!from || !to || from == to || from->profile != to->profile || from->tabs.size() < 2) return false;
  if (to->sleeping) { auto stored = to->sleeping->Clone(); if (!RestoreHost(*to, stored)) return false; }
  if (!to->view) return false;
  views::WebView* moving = from->tabs[index];
  std::erase_if(from->pin_tabs, [moving](const auto& entry) { return entry.second == moving; });
  const bool was_active = moving == from->view;
  if (from->workspace_id == active_id() || to->workspace_id == active_id()) shell_->SetPanes({}, {});
  if (auto* survivor = LeaveSplit(*from, moving); survivor && was_active) from->view = survivor;
  if (const auto url = from->deferred_urls.find(moving); url != from->deferred_urls.end()) {
    to->deferred_urls[moving] = url->second;
    from->deferred_urls.erase(url);
  }
  if (const auto title = from->deferred_titles.find(moving); title != from->deferred_titles.end()) {
    to->deferred_titles[moving] = title->second;
    from->deferred_titles.erase(title);
  }
  from->popup_tabs.erase(moving);
  from->tabs.erase(from->tabs.begin() + index);
  if (from->active_tab > index || from->active_tab >= from->tabs.size()) --from->active_tab;
  if (was_active && (from->view == moving || !from->view)) from->view = from->tabs[from->active_tab];
  from->active_tab = std::find(from->tabs.begin(), from->tabs.end(), from->view) - from->tabs.begin();
  to->tabs.insert(to->tabs.begin() + std::min(position, to->tabs.size()), moving);
  to->active_tab = std::find(to->tabs.begin(), to->tabs.end(), to->view) - to->tabs.begin();
  moving->SetVisible(false);
  if (auto* contents = moving->web_contents()) contents->WasHidden();
  RefreshPanes();
  UpdateShell();
  return true;
}

namespace {
std::vector<double> EqualWeights(size_t count) { return std::vector<double>(count, count ? 1.0 / count : 0.0); }
}  // namespace
bool WorkspaceWindow::split() const {
  for (const auto& host : hosts_) if (host.workspace_id == active_id()) return SplitShown(host);
  return false;
}
size_t WorkspaceWindow::split_count() const {
  for (const auto& host : hosts_) if (host.workspace_id == active_id()) return SplitShown(host) ? host.split.size() : 1;
  return 0;
}
bool WorkspaceWindow::InSplit(const Host& host, const views::WebView* view) {
  return view && std::find(host.split.begin(), host.split.end(), view) != host.split.end();
}
bool WorkspaceWindow::SplitShown(const Host& host) {
  return host.split.size() >= 2 && InSplit(host, host.view);
}
std::vector<views::WebView*> WorkspaceWindow::VisiblePanes(const Host& host) {
  if (SplitShown(host)) return {host.split.begin(), host.split.end()};
  return {host.view.get()};
}
views::WebView* WorkspaceWindow::LeaveSplit(Host& host, views::WebView* view) {
  const auto found = std::find(host.split.begin(), host.split.end(), view);
  if (found == host.split.end()) return nullptr;
  const size_t position = found - host.split.begin();
  host.split.erase(found);
  if (position < host.weights.size()) host.weights.erase(host.weights.begin() + position);
  if (host.split.empty()) return nullptr;
  views::WebView* next = host.split[std::min(position, host.split.size() - 1)];
  if (host.split.size() < 2) { host.split.clear(); host.weights.clear(); host.vertical = false; }
  else { auto normalized = sidebar::split_normalized(rust::Slice<const double>(host.weights.data(), host.weights.size()), host.split.size()); host.weights.assign(normalized.begin(), normalized.end()); }
  return next;
}
void WorkspaceWindow::ApplyPanes(const Host& host) {
  const bool shown = SplitShown(host);
  const auto panes = VisiblePanes(host);
  shell_->SetPanes({panes.begin(), panes.end()}, shown ? host.weights : std::vector<double>(), shown && host.vertical);
}
double WorkspaceWindow::split_ratio() const {
  for (const auto& host : hosts_) if (host.workspace_id == active_id())
    return sidebar::split_pair_ratio(rust::Slice<const double>(host.weights.data(), host.weights.size()), 0);
  return 0.5;
}
content::WebContents* WorkspaceWindow::pane_contents(size_t pane) const {
  for (const auto& host : hosts_) {
    if (host.workspace_id != active_id()) continue;
    const auto panes = VisiblePanes(host);
    return pane < panes.size() && panes[pane] ? panes[pane]->web_contents() : nullptr;
  }
  return nullptr;
}
void WorkspaceWindow::RefreshPanes() {
  for (auto& host : hosts_) {
    if (host.workspace_id != active_id()) continue;
    ApplyPanes(host);
    const auto panes = VisiblePanes(host);
    for (auto& tab : host.tabs) {
      if (std::find(panes.begin(), panes.end(), tab) != panes.end()) {
        ShowPage(tab);
        host.deferred_titles.erase(tab);
        if (auto pending = host.deferred_urls.find(tab); pending != host.deferred_urls.end()) {
          GURL url = pending->second;
          host.deferred_urls.erase(pending);
          LoadWorkspaceTab(tab->GetWebContents(), url, ui::PAGE_TRANSITION_RELOAD);
        }
      } else if (tab->GetVisible()) { tab->SetVisible(false); tab->GetWebContents()->WasHidden(); }
    }
    // Native views of newly shown panes attach after this pass.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce([](base::WeakPtr<WorkspaceWindow> window) {
      if (!window) return;
      for (auto& host : window->hosts_) if (host.workspace_id == window->active_id()) for (auto* pane : window->VisiblePanes(host)) RoundPage(pane);
    }, weak_factory_.GetWeakPtr()));
  }
}
void WorkspaceWindow::PageFocused(views::WebView* view) {
  if (closing_) return;
  for (auto& host : hosts_) {
    if (host.workspace_id != active_id() || !SplitShown(host) || !InSplit(host, view) || view == host.view) continue;
    host.view = view;
    host.active_tab = std::find(host.tabs.begin(), host.tabs.end(), view) - host.tabs.begin();
    UpdateShell();
  }
}
void WorkspaceWindow::FocusPane(size_t pane) {
  for (auto& host : hosts_) {
    if (host.workspace_id != active_id()) continue;
    const auto panes = VisiblePanes(host);
    auto* view = pane < panes.size() ? panes[pane] : nullptr;
    if (view) { PageFocused(view); view->RequestFocus(); }
  }
}
void WorkspaceWindow::SetDividerRatio(size_t divider, double ratio, bool done) {
  if (!std::isfinite(ratio)) return;
  for (auto& host : hosts_) {
    if (host.workspace_id != active_id() || divider + 1 >= host.split.size()) continue;
    auto weights = sidebar::split_apply_ratio(rust::Slice<const double>(host.weights.data(), host.weights.size()), divider, ratio);
    host.weights.assign(weights.begin(), weights.end());
    ApplyPanes(host);
    if (done) ScheduleSessionSave();
  }
}
bool WorkspaceWindow::SplitTab(size_t index) {
  if (!configuration_ready_ || profiles_busy_) return false;
  for (auto& host : hosts_) {
    if (host.workspace_id != active_id() || index >= host.tabs.size()) continue;
    RecordTimeline(true, active_id());
    // A visible group gains a pane; otherwise the shown page anchors a new pair.
    if (!SplitShown(host)) { host.split = {host.view}; host.weights.clear(); host.vertical = false; }
    if (InSplit(host, host.tabs[index])) {
      // Dropping a shown page beside itself creates an independent copy;
      // a menu action on the current tab must not silently do nothing.
      auto* source = host.tabs[index].get();
      const auto pending = host.deferred_urls.find(source);
      const GURL url = pending == host.deferred_urls.end()
          ? source->GetWebContents()->GetVisibleURL() : pending->second;
      host.tabs.push_back(CreatePage(host.profile));
      index = host.tabs.size() - 1;
      if (IsWorkspaceTabUrl(url)) host.deferred_urls[host.tabs[index]] = url;
    }
    if (host.split.size() >= kMaxSplitPanes) {
      // A full group replaces its focused pane.
      *std::find(host.split.begin(), host.split.end(), host.view) = host.tabs[index];
    } else {
      host.split.push_back(host.tabs[index]);
      host.weights = EqualWeights(host.split.size());
    }
    host.view = host.tabs[index];
    host.active_tab = index;
    RefreshPanes(); UpdateShell();
    shell_->CloseOverlays();
    host.view->RequestFocus();
    return true;
  }
  return false;
}
void WorkspaceWindow::Unsplit(size_t index) {
  if (!configuration_ready_ || profiles_busy_) return;
  for (auto& host : hosts_) {
    if (host.workspace_id != active_id() || host.split.empty() || index >= host.tabs.size()) continue;
    auto* kept = host.tabs[index].get();
    if (!InSplit(host, kept)) return;
    RecordTimeline(true, active_id());
    host.split.clear(); host.weights.clear(); host.vertical = false;
    host.view = kept; host.active_tab = index;
    RefreshPanes(); UpdateShell();
    host.view->RequestFocus();
    return;
  }
}
void WorkspaceWindow::RemoveFromSplit(size_t index) {
  if (!configuration_ready_ || profiles_busy_) return;
  for (auto& host : hosts_) {
    if (host.workspace_id != active_id() || index >= host.tabs.size() || !InSplit(host, host.tabs[index])) continue;
    RecordTimeline(true, active_id());
    const bool shown = SplitShown(host);
    auto* leaving = host.tabs[index].get();
    auto* next = LeaveSplit(host, leaving);
    // The rest of a shown group stays on screen, focused beside the removed pane.
    if (shown && host.view == leaving && next) host.view = next;
    host.active_tab = std::find(host.tabs.begin(), host.tabs.end(), host.view) - host.tabs.begin();
    RefreshPanes(); UpdateShell();
    host.view->RequestFocus();
    return;
  }
}
bool WorkspaceWindow::CloseOtherTabs() {
  if (!configuration_ready_ || profiles_busy_) return false;
  for (auto& host : hosts_) {
    if (host.workspace_id != active_id() || host.sleeping || host.tabs.size() < 2) continue;
    views::WebView* keep = host.tabs[host.active_tab];
    // Tabs open from a pin stay, as in Arc; the pin keeps its page.
    auto closes = [&](views::WebView* view) {
      return view != keep && std::none_of(host.pin_tabs.begin(), host.pin_tabs.end(), [&](const auto& pin) { return pin.second == view; });
    };
    std::vector<views::WebView*> closing;
    std::vector<ClosedTabSlot> undo;
    for (size_t i = 0; i < host.tabs.size(); ++i) if (closes(host.tabs[i])) {
      closing.push_back(host.tabs[i]);
      undo.push_back({i, TabUrl(host, host.tabs[i]), TabTitle(host, host.tabs[i])});
    }
    if (closing.empty()) return false;
    // Every page with a beforeunload handler is asked once, before any closes.
    std::vector<content::WebContents*> pages;
    for (auto* view : closing) if (auto* page = view->web_contents()) pages.push_back(page);
    if (GuardPages(pages, base::BindOnce([](base::WeakPtr<WorkspaceWindow> w) { if (w) w->CloseOtherTabs(); }, weak_factory_.GetWeakPtr()))) return true;
    // One timeline frame holds them all, so Time Machine or Cmd+Shift+T bring them back.
    RecordTimeline(true, active_id());
    base::AutoReset<bool> quiet(&closing_split_tabs_, true);
    for (auto* view : closing) {
      const auto found = std::find(host.tabs.begin(), host.tabs.end(), view);
      if (found != host.tabs.end()) CloseWorkspaceTab(host.workspace_id, found - host.tabs.begin());
    }
    // Read the count before moving the entries into Undo; argument evaluation
    // order must not decide whether the toast sees an empty vector.
    const auto count_copy = base::UTF8ToUTF16(std::string(sidebar::tab_count_copy(undo.size())));
    shell_->ShowToast(T(u"Zamknięto "), count_copy,
        base::BindOnce([](base::WeakPtr<WorkspaceWindow> w, std::string id, std::vector<ClosedTabSlot> tabs) {
          if (w) w->ReopenTabs(id, std::move(tabs));
        }, weak_factory_.GetWeakPtr(), host.workspace_id, std::move(undo)));
    return true;
  }
  return false;
}
bool WorkspaceWindow::ReopenTabs(std::string workspace_id, std::vector<ClosedTabSlot> tabs) {
  if (!configuration_ready_ || profiles_busy_) return false;
  for (auto& host : hosts_) {
    if (host.workspace_id != workspace_id || host.sleeping) continue;
    views::WebView* selected = host.view;
    // Back in their places, unloaded until shown; the tab that stayed is untouched.
    for (const auto& tab : tabs) {
      auto* view = CreatePage(host.profile);
      host.tabs.insert(host.tabs.begin() + std::min(tab.index, host.tabs.size()), view);
      if (IsWorkspaceTabUrl(tab.url)) host.deferred_urls[view] = tab.url;
      if (IsRssTabUrl(tab.url)) AssociateReaderPin(host, view);
      if (!tab.title.empty()) host.deferred_titles[view] = tab.title;
      std::erase_if(closed_tabs_, [&](const ClosedTab& closed) { return closed.workspace_id == workspace_id && closed.url == tab.url; });
    }
    const auto found = std::find(host.tabs.begin(), host.tabs.end(), selected);
    if (found != host.tabs.end()) host.active_tab = found - host.tabs.begin();
    RefreshPanes(); UpdateShell(); SaveSession();
    return true;
  }
  return false;
}
void WorkspaceWindow::CloseSplitTabs(size_t index) {
  if (!configuration_ready_ || profiles_busy_) return;
  for (auto& host : hosts_) {
    if (host.workspace_id != active_id() || index >= host.tabs.size() || !InSplit(host, host.tabs[index])) continue;
    // Every page with a beforeunload handler is asked once, before any closes.
    std::vector<content::WebContents*> pages;
    for (const auto& member : host.split) if (auto* page = member->web_contents()) pages.push_back(page);
    if (GuardPages(pages, base::BindOnce(&WorkspaceWindow::CloseSplitTabs, weak_factory_.GetWeakPtr(), index))) return;
    RecordTimeline(true, active_id());
    const std::vector<views::WebView*> members(host.split.begin(), host.split.end());
    const bool fresh = members.size() == host.tabs.size();
    if (fresh) host.tabs.push_back(CreatePage(host.profile));
    base::AutoReset<bool> quiet(&closing_split_tabs_, true);
    for (auto* member : members) {
      const auto found = std::find(host.tabs.begin(), host.tabs.end(), member);
      if (found != host.tabs.end()) CloseWorkspaceTab(host.workspace_id, found - host.tabs.begin());
    }
    if (fresh) { SelectTab(host.tabs.size() - 1); shell_->OpenAddress(false); }
    return;
  }
}
void WorkspaceWindow::MoveSplitPane(size_t index, bool later) {
  if (!configuration_ready_ || profiles_busy_) return;
  for (auto& host : hosts_) {
    if (host.workspace_id != active_id() || index >= host.tabs.size()) continue;
    const auto found = std::find(host.split.begin(), host.split.end(), host.tabs[index]);
    if (found == host.split.end()) return;
    const size_t position = found - host.split.begin();
    if (later ? position + 1 >= host.split.size() : position == 0) return;
    const size_t other = later ? position + 1 : position - 1;
    std::swap(host.split[position], host.split[other]);
    if (other < host.weights.size() && position < host.weights.size()) std::swap(host.weights[position], host.weights[other]);
    RefreshPanes(); UpdateShell();
    return;
  }
}
void WorkspaceWindow::SwapSplit() {
  if (!configuration_ready_ || profiles_busy_) return;
  for (auto& host : hosts_) {
    if (host.workspace_id != active_id() || host.split.empty()) continue;
    std::reverse(host.split.begin(), host.split.end());
    std::reverse(host.weights.begin(), host.weights.end());
    RefreshPanes(); UpdateShell();
    return;
  }
}
void WorkspaceWindow::SetSplitVertical(bool vertical) {
  if (!configuration_ready_ || profiles_busy_) return;
  for (auto& host : hosts_) {
    if (host.workspace_id != active_id() || host.split.empty() || host.vertical == vertical) continue;
    host.vertical = vertical;
    RefreshPanes(); UpdateShell();
    return;
  }
}
bool WorkspaceWindow::split_vertical() const {
  for (const auto& host : hosts_) if (host.workspace_id == active_id()) return !host.split.empty() && host.vertical;
  return false;
}
void WorkspaceWindow::ToggleSplit() {
  if (!configuration_ready_ || profiles_busy_) return;
  for (auto& host : hosts_) {
    if (host.workspace_id != active_id()) continue;
    if (SplitShown(host)) {
      RecordTimeline(true, active_id());
      host.split.clear(); host.weights.clear(); host.vertical = false;
      RefreshPanes(); UpdateShell(); host.view->RequestFocus();
      return;
    }
    host.split.clear(); host.weights.clear();
    for (size_t i = 0; i < host.tabs.size(); ++i) if (host.tabs[i] != host.view) { SplitTab(i); return; }
    host.tabs.push_back(CreatePage(host.profile));
    SplitTab(host.tabs.size() - 1);
    shell_->OpenAddress(false);
    return;
  }
}

bool WorkspaceWindow::MoveTab(size_t index, size_t before) {
  if (!configuration_ready_ || profiles_busy_) return false;
  for (auto& host : hosts_) {
    if (host.workspace_id != active_id() || index >= host.tabs.size() || before > host.tabs.size()) continue;
    if (before == index || before == index + 1) return true;
    auto* moving = host.tabs[index].get();
    host.tabs.erase(host.tabs.begin() + index);
    host.tabs.insert(host.tabs.begin() + (before > index ? before - 1 : before), moving);
    host.active_tab = std::find(host.tabs.begin(), host.tabs.end(), host.view) - host.tabs.begin();
    UpdateShell();
    return true;
  }
  return false;
}

void WorkspaceWindow::CloseActiveTab() {
  if (peek_view_) { ClosePeek(); return; }
  for (const auto& host : hosts_) {
    if (host.workspace_id == active_id()) {
      CloseTab(host.active_tab);
      return;
    }
  }
}

void WorkspaceWindow::RunPageCommand(PageCommand command) {
  auto* page = peek_contents() ? peek_contents() : contents(active_id());
  if (!page) return;
  RunPageCommandOn(page, command, true);
}

void WorkspaceWindow::RunPageCommandOn(content::WebContents* page,
                                       PageCommand command, bool focused) {
  if (command == PageCommand::kReload && IsRssTabUrl(page->GetVisibleURL())) {
    RssReader::For(Profile::FromBrowserContext(page->GetBrowserContext()))->Refresh();
    return;
  }
  auto& controller = page->GetController();
  switch (command) {
    case PageCommand::kBack:
      if (controller.CanGoBack()) controller.GoBack();
      break;
    case PageCommand::kForward:
      if (controller.CanGoForward()) controller.GoForward();
      break;
    case PageCommand::kReload:
      if (focused) shell_->AnimateReload();
      controller.Reload(content::ReloadType::NORMAL, true);
      break;
  }
  if (focused) page->Focus();
}

GURL WorkspaceWindow::GetTabMenuUrl(std::optional<size_t> tab, std::string_view pin) const {
  const auto host = std::ranges::find(hosts_, active_id(), &Host::workspace_id);
  if (host == hosts_.end()) return {};
  if (tab) return *tab < host->tabs.size() ? TabUrl(*host, host->tabs[*tab]) : GURL();
  if (const auto found = host->pin_tabs.find(std::string(pin)); found != host->pin_tabs.end())
    return TabUrl(*host, found->second);
  const auto& pins = pinned_items(active_id());
  const auto item = std::ranges::find(pins, pin, &PinnedItem::id);
  return item != pins.end() && !item->folder ? item->url : GURL();
}

bool WorkspaceWindow::NavigateFromBar(const GURL& url, bool new_tab) {
  preconnect_timer_.Stop();
  if (peek_view_ && GuardPages({peek_contents()}, base::BindOnce([](base::WeakPtr<WorkspaceWindow> w, GURL url, bool fresh) {
    if (w) w->NavigateFromBar(url, fresh);
  }, weak_factory_.GetWeakPtr(), url, new_tab))) return true;
  if (!url.is_valid() || !IsWorkspaceTabUrl(url)) return false;
  if (const auto target = WorkspaceForUrl(url); !target.empty() && target != active_id())
    return OpenLink(url, active_id(), true);
  pending_profile_action_.Reset();
  ClosePeek();
  if (new_tab) {
    for (auto& host : hosts_) {
      if (host.workspace_id != active_id()) continue;
      host.tabs.push_back(CreatePage(host.profile));
      SelectTab(host.tabs.size() - 1);
      break;
    }
  }
  auto* page = contents(active_id());
  if (!page) return false;
  LoadWorkspaceTab(page, url, ui::PAGE_TRANSITION_TYPED);
  return true;
}

void WorkspaceWindow::Navigate(const GURL& url) { NavigateFromBar(url, false); }

bool WorkspaceWindow::AcceleratorPressed(const ui::Accelerator& accelerator) {
  if (accelerator.key_state() != ui::Accelerator::KeyState::PRESSED) return false;
  if (shell_->CaptureShortcut(accelerator)) return true;
  if (accelerator.modifiers() == 0 && accelerator.key_code() == ui::VKEY_ESCAPE) {
    if (assistant_ && assistant_->HandleEscape()) return true;
    if (fullscreen_page_) { fullscreen_page_->ExitFullscreen(true); return true; }
    if (!shell_->CloseOverlays()) {
      if (!peek_view_) return false;
      ClosePeek();
    }
    widget_->Activate();
    if (auto* page = contents(active_id())) page->Focus();
    return true;
  }
  const auto id = shell_->ShortcutFor(accelerator);
  if (id.empty()) return false;
  consumed_keys_.insert(accelerator.key_code()); suppress_shortcut_char_ = true;
  RunShortcut(id);
  return true;
}

bool WorkspaceWindow::RunShortcut(std::string id) {
  if (id == "duplicate_tab") { for (const auto& host : hosts_) if (host.workspace_id == active_id()) return DuplicateTab(host.active_tab); return false; }
  if (id == "private_window") { if (new_private_window_) new_private_window_.Run(); return true; }
  if (id == "assistant") { ShowAssistant(); return true; }
  if (id.empty()) return false;
  if (id == "exit_fullscreen") {
    if (fullscreen_page_) fullscreen_page_->ExitFullscreen(true);
    return true;
  }
  if (id == "toggle_window_fullscreen") {
    if (fullscreen_page_) fullscreen_page_->ExitFullscreen(true);
    else widget_->SetFullscreen(!widget_->IsFullscreen());
    return true;
  }
  if (id == "copy_url") {
    // The address pill trembles once: the copy's only confirmation (design: AddressPill).
    auto* page = peek_contents() ? peek_contents() : contents(active_id());
    if (!page || !IsWorkspaceTabUrl(page->GetVisibleURL())) return false;
    const GURL& url = page->GetVisibleURL();
    const auto clean = clean_address_copies() ? CleanUrlForCopy(url) : std::nullopt;
    ui::ScopedClipboardWriter(ui::ClipboardBuffer::kCopyPaste)
        .WriteText(base::UTF8ToUTF16(clean ? *clean : url.spec()));
    shell_->TremorAddress();
    return true;
  }
  if (id == "developer") { shell_->ShowDeveloperMenu(); return true; }
  if (id == "view_source") {
    // The page's source opens beside it, in a new tab of the same space.
    auto* page = peek_contents() ? peek_contents() : contents(active_id());
    if (!page || !page->GetVisibleURL().SchemeIsHTTPOrHTTPS()) return false;
    return OpenLink(GURL("view-source:" + page->GetVisibleURL().spec()), active_id(), true, false);
  }
  if (id.starts_with("devtools") || id == "hard_reload" || id == "project_split") {
    auto* page = peek_contents() ? peek_contents() : contents(active_id());
    if (!page) return false;
    if (id == "hard_reload" && IsRssTabUrl(page->GetVisibleURL())) { RunPageCommand(PageCommand::kReload); return true; }
    if (id == "hard_reload") { page->GetController().Reload(content::ReloadType::BYPASSING_CACHE, true); return true; }
    if (id == "project_split") {
      if (peek_contents() || !IsLocalProject(page->GetVisibleURL())) return false;
      for (const auto& host : hosts_) if (host.workspace_id == active_id()) return SplitTab(host.active_tab);
      return false;
    }
    if (!DevToolsWindow::AllowDevToolsFor(Profile::FromBrowserContext(page->GetBrowserContext()), page)) return false;
    if (id == "devtools") {
      if (auto* existing = DevToolsWindow::GetInstanceForInspectedWebContents(page)) {
        existing->GetDevToolsWebContents()->ClosePage();
        return true;
      }
    }
    // The page's docking host embeds Chromium's frontend; its Dock side menu
    // can still transfer ownership to a separate TYPE_DEVTOOLS window.
    const auto action = id == "devtools" ? DevToolsToggleAction::Toggle() :
        id == "devtools_console" ? DevToolsToggleAction::ShowConsolePanel() :
        id == "devtools_inspect" ? DevToolsToggleAction::Inspect() : DevToolsToggleAction::Show();
    DevToolsWindow::OpenDevToolsWindow(page, action, DevToolsOpenedByAction::kMainMenuOrMainShortcut);
    return true;
  }
  if (id == "downloads") { ShowDownloads(); return true; }
  if (id == "find") return StartFind();
  if (id == "print") return StartPrint();
  if (id == "print_system") {
    auto* page = peek_contents() ? peek_contents() : contents(active_id());
    if (!page || !printing::PrintViewManager::FromWebContents(page)) return false;
    shell_->HidePrint();
    printing::StartBasicPrint(page);
    return true;
  }
  // The |z⟩ menu's commands without a shortcut.
  if (id == "passwords") { ShowPasswords(); return true; }
  if (id == "import") { shell_->OpenImport(); return true; }
  if (id == "quit") { RequestClose(); return true; }
  // Development (--zitter-open-panel=site / permission): the page's origin with
  // two sample rules, then its panel; or a notification request from it.
  if (id == "site_demo" || id == "permission_demo") {
    auto* page = contents(active_id());
    if (!page) return false;
    // A real site, so the panel has cookies and history to show.
    const GURL origin("https://example.com/");
    if (page->GetLastCommittedURL() != origin || page->GetController().GetLastCommittedEntry()->GetPageType() == content::PAGE_TYPE_ERROR) { Navigate(origin); return true; }
    auto* profile = Profile::FromBrowserContext(page->GetBrowserContext());
    if (id == "permission_demo") {
      ResetSitePermissions(profile, origin);
      page->GetPrimaryMainFrame()->ExecuteJavaScriptWithUserGestureForTests(u"Notification.requestPermission()", base::NullCallback(), content::ISOLATED_WORLD_ID_GLOBAL);
      return true;
    }
    WriteSitePermission(profile, origin, "notifications", 1);
    WriteSitePermission(profile, origin, "geolocation", 2);
    WriteSitePermission(profile, origin, "local-network-access", 1);
    WriteSitePermission(profile, origin, "sound", 1);
    // A cookie for the data page.
    page->GetPrimaryMainFrame()->ExecuteJavaScriptForTests(u"document.cookie='session=demo; max-age=3600'; document.cookie='prefs=dark; max-age=8640000'", base::NullCallback(), content::ISOLATED_WORLD_ID_GLOBAL);
    ShowSitePanel();
    return true;
  }
  // Development (--zitter-open-panel=extension-popup): the first pinned action.
  if (id == "extension_popup") {
    const auto actions = extensions_bar_->visible_actions_for_testing();
    if (actions.empty()) return false;
    extensions_bar_->RunAction(actions.front().id);
    return true;
  }
  if (id == "extensions") { shell_->CloseOverlays(true); extensions_bar_->ToggleMenu(); return extensions_bar_->profile() != nullptr; }
  if (id == "fingerprint") { shell_->CloseOverlays(true); extensions_bar_->ToggleFingerprintPanel(); return extensions_bar_->fingerprint_panel_for_testing() != nullptr; }
  if (id == "shield") { shell_->CloseOverlays(true); extensions_bar_->ToggleShieldPanel(); return extensions_bar_->shield_panel_for_testing() != nullptr; }
  if (id == "night_shift") { shell_->CloseOverlays(true); extensions_bar_->ToggleNightShiftPanel(); return extensions_bar_->night_shift_panel_for_testing() != nullptr; }
  if (id == "history") { ShowHistory(); return true; }
  if (id == "rss") { ShowRss(); return true; }
  if (id == "settings") { shell_->OpenGeneralSettings(); return true; }
  if (id == "address" || id == "new_tab" || id == "palette") {
    if (peek_view_ && GuardPages({peek_contents()}, base::BindOnce([](base::WeakPtr<WorkspaceWindow> w, std::string id) { if (w) w->RunShortcut(id); }, weak_factory_.GetWeakPtr(), id))) return true;
    ClosePeek(); UpdateShell();
    if (id == "address") shell_->OpenAddress(false);
    else shell_->OpenCommandPalette();
    return true;
  }
  if (id == "close_tab") { CloseActiveTab(); return true; }
  if (id == "close_other_tabs") return CloseOtherTabs();
  if (id == "reopen") return ReopenClosedTab();
  if (id == "sidebar") { shell_->ToggleSidebar(); return true; }
  if (id == "reload" || id == "back" || id == "forward") {
    RunPageCommand(id == "reload" ? PageCommand::kReload : id == "back" ? PageCommand::kBack : PageCommand::kForward); return true;
  }
  if (id == "split") { ToggleSplit(); return true; }
  if (id == "close_split") { if (split()) ToggleSplit(); return true; }
  if (id == "left_pane" || id == "right_pane" || id == "third_pane" || id == "fourth_pane") {
    FocusPane(id == "left_pane" ? 0 : id == "right_pane" ? 1 : id == "third_pane" ? 2 : 3); return true;
  }
  if (id == "timeline") { shell_->OpenTimeMachine(); return true; }
  if (id == "snapshot") return SaveSnapshot(base::UTF16ToUTF8(T(u"Snapshot")));
  if (id == "sleep") return SleepWorkspace(active_id());
  if (id == "next_tab" || id == "previous_tab") {
    for (const auto& host : hosts_) if (host.workspace_id == active_id() && !host.tabs.empty()) {
      CycleRecentTab(id == "previous_tab"); return true;
    }
  }
  if (id.starts_with("tab") && id.size() == 4 && id.back() >= '1' && id.back() <= '9') {
    const size_t index = id.back() - '1';
    if (index < tab_count(active_id())) { SelectTab(index); return true; }
  }
  const auto& spaces = model_.workspaces();
  if (spaces.empty()) return false;
  if (id == "next_space" || id == "previous_space") {
    auto current = std::find_if(spaces.begin(), spaces.end(), [&](const auto& space) { return space.id == active_id(); });
    const size_t index = current - spaces.begin();
    return Select(spaces[(index + (id == "next_space" ? 1 : spaces.size() - 1)) % spaces.size()].id);
  }
  if (id.starts_with("space") && id.size() == 6 && id.back() >= '1' && id.back() <= '9') {
    const size_t index = id.back() - '1';
    return index < spaces.size() && Select(spaces[index].id);
  }
  return false;
}

content::KeyboardEventProcessingResult WorkspaceWindow::PreHandlePageKeyEvent(
    content::WebContents* source, const input::NativeWebKeyboardEvent& event) {
  using Result = content::KeyboardEventProcessingResult;
  using Type = blink::WebInputEvent::Type;
  const auto accelerator = ui::GetAcceleratorFromNativeWebKeyboardEvent(event);
  const int key = accelerator.key_code();
  if (event.GetType() == Type::kKeyUp && key == ui::VKEY_CONTROL && tab_cycle_) FinishTabCycle();
  if (event.GetType() == Type::kKeyUp && consumed_keys_.erase(key)) {
    suppress_shortcut_char_ = false; return Result::HANDLED;
  }
  if (event.GetType() == Type::kChar && suppress_shortcut_char_) return Result::HANDLED;
  if (event.GetType() != Type::kRawKeyDown && event.GetType() != Type::kKeyDown) return Result::NOT_HANDLED;
  // The previous keyup may have gone to a palette textfield. A new physical
  // press always owns its own release, including an unmodified normal key.
  consumed_keys_.erase(key);
  suppress_shortcut_char_ = false;
  if (event.skip_if_unhandled || (source != contents(active_id()) && source != peek_contents())) return Result::NOT_HANDLED;
  const bool escape_peek = key == ui::VKEY_ESCAPE && accelerator.modifiers() == 0 && source == peek_contents() && !fullscreen_page_;
  if (key == ui::VKEY_ESCAPE && accelerator.modifiers() == 0 && assistant_ && assistant_->HandleEscape()) {
    consumed_keys_.insert(key); suppress_shortcut_char_ = true; return Result::HANDLED;
  }
  const bool escape_fullscreen = key == ui::VKEY_ESCAPE && accelerator.modifiers() == 0 && fullscreen_page_;
  auto action = shell_->ShortcutFor(accelerator);
  if (escape_fullscreen) action = "exit_fullscreen";
  if (action.empty() && !escape_peek) {
    const auto zoom = PageZoomStep(accelerator);
    if (!zoom) return Result::NOT_HANDLED;
    consumed_keys_.insert(key); suppress_shortcut_char_ = true;
    ZoomPage(source, *zoom);
    return Result::HANDLED;
  }
  consumed_keys_.insert(key); suppress_shortcut_char_ = true;
  // All actions are deferred: close/restore/sleep may destroy the source page.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce(
      [](base::WeakPtr<WorkspaceWindow> window, std::string action, bool escape_peek) {
        if (!window) return;
        if (escape_peek) window->ClosePeek();
        else window->RunShortcut(std::move(action));
        // Key state was recorded before posting. Do not arm it again after a
        // fast keyup has already been consumed on the originating page.
      }, weak_factory_.GetWeakPtr(), action, escape_peek));
  return Result::HANDLED;
}

bool WorkspaceWindow::CanHandleAccelerators() const {
  return widget_ && widget_->IsVisible();
}

Profile* WorkspaceWindow::active_profile() const {
  return model_.active() ? ProfileFor(model_.active()->profile_id) : nullptr;
}

Profile* WorkspaceWindow::ProfileFor(std::string_view id) const {
  if (is_private()) return id == "private-profile" ? private_profile_.get() : nullptr;
  for (const auto& entry : profiles_) {
    if (entry.info.id == id && entry.lease) return entry.lease->profile();
  }
  return nullptr;
}

bool WorkspaceWindow::AddWorkspace(std::string name, std::string profile_id) {
  if (is_private()) return false;
  auto* profile = ProfileFor(profile_id);
  if (!configuration_ready_ || configuration_invalid_ || profiles_busy() || name.empty() || !profile) return false;
  const auto id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  if (!model_.Add({id, profile_id, std::move(name)})) return false;
  hosts_.emplace_back();
  auto& host = hosts_.back();
  host.workspace_id = id;
  host.profile_id = profile_id;
  host.profile = profile;
  host.view = CreatePage(profile);
  host.tabs.push_back(host.view);
  Select(id);
  SaveConfiguration();
  return true;
}

bool WorkspaceWindow::EditWorkspace(std::string name, std::string profile_id) {
  return UpdateWorkspace(active_id(), std::move(name), std::move(profile_id));
}

bool WorkspaceWindow::AssignProfile(std::string workspace_id, std::string profile_id) {
  const auto* workspace = model_.Find(workspace_id);
  if (!workspace) return false;
  return UpdateWorkspace(std::move(workspace_id), workspace->name, std::move(profile_id));
}

bool WorkspaceWindow::UpdateWorkspace(std::string id, std::string name, std::string profile_id) {
  auto* profile = ProfileFor(profile_id);
  if (!configuration_ready_ || configuration_invalid_ || profiles_busy() || name.empty() || !profile) return false;
  for (auto& host : hosts_) {
    if (host.workspace_id != id) continue;
    if (host.profile_id != profile_id) {
      if (GuardPages(WorkspacePages(id), base::BindOnce([](base::WeakPtr<WorkspaceWindow> w, std::string id, std::string name, std::string profile) {
        if (w) w->UpdateWorkspace(id, name, profile);
      }, weak_factory_.GetWeakPtr(), id, name, profile_id))) return true;
      if (peek_workspace_id_ == id) ClosePeek();
      // A live WebContents must never change its BrowserContext.
      closing_ = true;
      if (id == active_id()) shell_->SetPanes({}, {});
      host.split.clear(); host.weights.clear(); host.deferred_urls.clear(); host.deferred_titles.clear(); host.popup_tabs.clear();
      host.pin_tabs.clear();
      host.sleeping.reset();
      auto old_tabs = std::move(host.tabs);
      host.view = nullptr;
      host.profile = profile;
      host.profile_id = profile_id;
      host.active_tab = 0;
      model_.Update(id, name, profile_id);
      for (auto& tab : old_tabs) shell_->content_host()->RemoveChildViewT(tab.get());
      host.view = CreatePage(profile);
      host.tabs.push_back(host.view);
      if (id == active_id()) RefreshPanes();
      closing_ = false;
    } else {
      model_.Update(id, name, profile_id);
    }
    UpdateShell();
    SaveConfiguration();
    return true;
  }
  return false;
}

bool WorkspaceWindow::RemoveWorkspace(std::string id) {
  if (!configuration_ready_ || configuration_invalid_ || profiles_busy() || hosts_.size() <= 1) return false;
  if (id.empty()) id = active_id();
  if (std::none_of(hosts_.begin(), hosts_.end(), [&](const auto& host) { return host.workspace_id == id; })) return false;
  if (GuardPages(WorkspacePages(id, true, true), base::BindOnce([](base::WeakPtr<WorkspaceWindow> w, std::string target) {
        if (w) w->RemoveWorkspace(std::move(target));
      }, weak_factory_.GetWeakPtr(), id))) return true;
  const auto next = model_.workspaces()[0].id == id
      ? model_.workspaces()[1].id : model_.workspaces()[0].id;
  const bool was_active = active_id() == id;
  if (was_active && !Select(next)) return false;
  for (auto it = hosts_.begin(); it != hosts_.end(); ++it) {
    if (it->workspace_id != id) continue;
    RemovedWorkspace removed{*model_.Find(id), pins_[id], {}, CaptureHost(*it), was_active};
    std::ranges::copy_if(link_rules_, std::back_inserter(removed.rules), [&](const auto& rule) { return rule.workspace_id == id; });
    removed_workspace_ = std::move(removed);
    auto tabs = std::move(it->tabs);
    it->view = nullptr;
    hosts_.erase(it);
    model_.Remove(id);
    pins_.erase(id);
    std::erase_if(link_rules_, [&](const auto& rule) { return rule.workspace_id == id; });
    for (auto& tab : tabs) shell_->content_host()->RemoveChildViewT(tab.get());
    break;
  }
  UpdateShell();
  SaveConfiguration();
  return true;
}

bool WorkspaceWindow::RestoreRemovedWorkspace(std::string id) {
  if (!removed_workspace_ || removed_workspace_->workspace.id != id || !configuration_ready_ ||
      configuration_invalid_ || profiles_busy()) return false;
  auto removed = std::move(*removed_workspace_); removed_workspace_.reset();
  auto* profile = ProfileFor(removed.workspace.profile_id);
  if (!profile || !model_.Add(removed.workspace)) return false;  // Returns at the end of the list.
  pins_[id] = std::move(removed.pins);
  std::ranges::move(removed.rules, std::back_inserter(link_rules_));
  hosts_.emplace_back();
  auto& host = hosts_.back();
  host.workspace_id = id; host.profile_id = removed.workspace.profile_id; host.profile = profile;
  if (!RestoreHost(host, removed.host)) { host.view = CreatePage(profile); host.tabs.push_back(host.view); }
  if (removed.active) Select(id); else UpdateShell();
  SaveConfiguration();
  return true;
}

void WorkspaceWindow::SaveConfiguration(base::OnceCallback<void(bool)> done) {
  if (is_private()) { if (done) std::move(done).Run(true); return; }
  if (!configuration_ready_ || configuration_invalid_) { if (done) std::move(done).Run(false); return; }
  base::DictValue dict;
  dict.Set("image_link_previews", image_link_previews_);
  dict.Set("shift_click_peek", shift_click_peek_);
  dict.Set("clean_address_copies", clean_address_copies_);
  dict.Set("assistant", assistant_settings_.ToValue());
  if (night_shift_config_.managed) dict.Set("night_shift", night_shift_config_.ToValue());
  if (!night_shift_hidden_.empty()) {
    base::ListValue hidden;
    for (const auto& id : night_shift_hidden_) hidden.Append(id);
    dict.Set("night_shift_hidden", std::move(hidden));
  }
  const auto* replaced_starter = import_replaces_starter_ ? model_.Find(starter_workspace_id_) : nullptr;
  base::ListValue profile_list;
  for (const auto& entry : profiles_) {
    if (replaced_starter && entry.info.id == replaced_starter->profile_id) continue;
    base::DictValue item;
    item.Set("id", entry.info.id);
    item.Set("name", entry.info.name);
    item.Set("directory", entry.info.directory);
    item.Set("source_id", entry.info.source_id);
    base::ListValue favorites;
    if (auto found = pins_.find("@profile:" + entry.info.id); found != pins_.end()) for (const auto& pin : found->second) {
      base::DictValue value; value.Set("id", pin.id); value.Set("title", pin.title); value.Set("url", pin.url.spec());
      favorites.Append(std::move(value));
    }
    item.Set("pins", std::move(favorites));
    profile_list.Append(std::move(item));
  }
  dict.Set("profiles", std::move(profile_list));
  base::ListValue deleted;
  for (const auto& directory : deleted_directories_) deleted.Append(directory);
  dict.Set("deleted_profiles", std::move(deleted));
  base::ListValue list;
  for (const auto& workspace : model_.workspaces()) {
    if (replaced_starter && workspace.id == replaced_starter->id) continue;
    base::DictValue item;
    item.Set("id", workspace.id);
    item.Set("name", workspace.name);
    item.Set("profile_id", workspace.profile_id);
    base::ListValue pinned;
    for (const auto& pin : pinned_items(workspace.id)) {
      if (pin.profile_pin) continue;
      base::DictValue value;
      value.Set("id", pin.id);
      value.Set("parent_id", pin.parent_id);
      value.Set("title", pin.title);
      value.Set("url", pin.url.spec());
      value.Set("folder", pin.folder);
      if (pin.github) {
        value.Set("github", base::DictValue().Set("login", pin.github->login)
            .Set("repository", pin.github->repository).Set("encrypted_token", pin.github->encrypted_token));
      }
      pinned.Append(std::move(value));
    }
    item.Set("pins", std::move(pinned));
    list.Append(std::move(item));
  }
  dict.Set("workspaces", std::move(list));
  base::ListValue rules;
  for (const auto& rule : link_rules_) {
    base::DictValue value;
    value.Set("host", rule.host); value.Set("workspace_id", rule.workspace_id);
    rules.Append(std::move(value));
  }
  dict.Set("link_rules", std::move(rules));
  dict.Set("active", replaced_starter ? import_new_workspace_ids_.front() : active_id());
  dict.Set("onboarding_complete", onboarding_complete_ || import_replaces_starter_);
  if (!starter_workspace_id_.empty() && !onboarding_complete_ && !import_replaces_starter_)
    dict.Set("starter_workspace", starter_workspace_id_);
  std::string json;
  if (!base::JSONWriter::Write(dict, &json)) { if (done) std::move(done).Run(false); return; }
  ++pending_config_writes_;
  config_runner_->PostTaskAndReplyWithResult(FROM_HERE,
      base::BindOnce([](base::FilePath path, std::string json) {
        return base::ImportantFileWriter::WriteFileAtomically(path, json);
      }, configuration_path_, std::move(json)),
      base::BindOnce([](base::WeakPtr<WorkspaceWindow> window, base::OnceCallback<void(bool)> done, bool ok) {
        if (!window) return;
        --window->pending_config_writes_;
        window->shell_->SetConfigurationError(!ok);
        if (done) std::move(done).Run(ok);
      }, weak_factory_.GetWeakPtr(), std::move(done)));
}

namespace {
bool ValidDirectory(std::string_view name) {
  return name == "ZitterWork" || name == "ZitterPersonal" ||
      (name.starts_with("ZitterProfile-") &&
       base::Uuid::ParseLowercase(name.substr(14)).is_valid());
}
std::string TrimName(std::string name) {
  base::TrimWhitespaceASCII(name, base::TRIM_ALL, &name);
  return name;
}
}  // namespace

std::vector<ProfileInfo> WorkspaceWindow::profiles() const {
  std::vector<ProfileInfo> result;
  for (const auto& entry : profiles_) result.push_back(entry.info);
  return result;
}

bool WorkspaceWindow::CreateProfile(std::string name, std::string workspace_id) {
  if (is_private()) return false;
  name = TrimName(std::move(name));
  if (!configuration_ready_ || configuration_invalid_ || profiles_busy() || name.empty()) return false;
  for (const auto& entry : profiles_) {
    if (base::EqualsCaseInsensitiveASCII(entry.info.name, name)) {
      shell_->SetProfileStatus(WorkspaceShell::ProfileStatusEvent::kDuplicateName);
      return false;
    }
  }
  if (!workspace_id.empty() && !model_.Find(workspace_id)) return false;
  ProfileInfo info;
  info.id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  info.directory = "ZitterProfile-" + info.id;
  info.name = std::move(name);
  profiles_busy_ = true;
  shell_->SetProfileStatus(WorkspaceShell::ProfileStatusEvent::kCreating);
  UpdateShell();
  auto path = data_directory_.AppendASCII(info.directory);
  g_browser_process->profile_manager()->CreateProfileAsync(path,
      base::BindOnce(&WorkspaceWindow::ProfileCreated, weak_factory_.GetWeakPtr(), std::move(info), std::move(workspace_id)));
  return true;
}

void WorkspaceWindow::ProfileCreated(ProfileInfo info, std::string workspace_id, Profile* profile) {
  auto lease = profile ? ScopedProfileKeepAlive::TryAcquire(profile, ProfileKeepAliveOrigin::kBrowserWindow) : nullptr;
  if (!lease) {
    profiles_busy_ = false;
    UpdateShell();
    shell_->SetProfileStatus(WorkspaceShell::ProfileStatusEvent::kCreateFailed);
    return;
  }
  const auto id = info.id;
  if (auto* attributes = g_browser_process->profile_manager()->GetProfileAttributesStorage().GetProfileAttributesWithPath(profile->GetPath()))
    attributes->SetLocalProfileName(base::UTF8ToUTF16(info.name), false);
  profiles_.push_back({std::move(info), std::move(lease)});
  SaveConfiguration(base::BindOnce([](base::WeakPtr<WorkspaceWindow> window, std::string id, std::string workspace_id, bool ok) {
    if (!window) return;
    if (!ok) {
      auto it = std::find_if(window->profiles_.begin(), window->profiles_.end(), [&](const auto& entry) { return entry.info.id == id; });
      if (it != window->profiles_.end()) {
        window->ScheduleProfileDeletion(it->info.directory);
        window->profiles_.erase(it);
      }
    }
    window->profiles_busy_ = false;
    if (ok && !workspace_id.empty()) window->AssignProfile(workspace_id, id);
    window->UpdateShell();
    window->shell_->SetProfileStatus(ok ? WorkspaceShell::ProfileStatusEvent::kCreated : WorkspaceShell::ProfileStatusEvent::kCreateNotSaved);
  }, weak_factory_.GetWeakPtr(), id, std::move(workspace_id)));
}

bool WorkspaceWindow::RenameProfile(std::string id, std::string name) {
  if (is_private()) return false;
  name = TrimName(std::move(name));
  if (!configuration_ready_ || configuration_invalid_ || profiles_busy() || name.empty()) return false;
  ProfileEntry* selected = nullptr;
  for (auto& entry : profiles_) {
    if (entry.info.id == id) selected = &entry;
    else if (base::EqualsCaseInsensitiveASCII(entry.info.name, name)) {
      shell_->SetProfileStatus(WorkspaceShell::ProfileStatusEvent::kDuplicateName);
      return false;
    }
  }
  if (!selected) return false;
  const auto old_name = selected->info.name;
  selected->info.name = std::move(name);
  profiles_busy_ = true;
  UpdateShell();
  SaveConfiguration(base::BindOnce([](base::WeakPtr<WorkspaceWindow> window, std::string id,
                                     std::string old_name, bool ok) {
    if (!window) return;
    for (auto& entry : window->profiles_) {
      if (entry.info.id != id) continue;
      if (!ok) entry.info.name = old_name;
      if (auto* attributes = g_browser_process->profile_manager()->GetProfileAttributesStorage().GetProfileAttributesWithPath(entry.lease->profile()->GetPath()))
        attributes->SetLocalProfileName(base::UTF8ToUTF16(entry.info.name), false);
    }
    window->profiles_busy_ = false;
    window->UpdateShell();
    window->shell_->SetProfileStatus(ok ? WorkspaceShell::ProfileStatusEvent::kRenamed : WorkspaceShell::ProfileStatusEvent::kRenameFailed);
  }, weak_factory_.GetWeakPtr(), id, old_name));
  return true;
}

void WorkspaceWindow::SaveSelection() {
  if (is_private()) return;
  if (configuration_invalid_) return;
  ++pending_config_writes_;
  // Changing a dot only persists its ID; serializing all imported pins stays off
  // the UI sequence and runs in order with structural configuration writes.
  config_runner_->PostTaskAndReplyWithResult(FROM_HERE, base::BindOnce(
      [](base::FilePath path, std::string id) {
        std::string json;
        if (!base::ReadFileToString(path, &json)) return true;  // Initial catalogue follows.
        auto dict = base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
        if (!dict) return false;
        dict->Set("active", std::move(id));
        return base::JSONWriter::Write(*dict, &json) && base::ImportantFileWriter::WriteFileAtomically(path, json);
      }, configuration_path_, active_id()), base::BindOnce(
      [](base::WeakPtr<WorkspaceWindow> window, bool ok) {
        if (!window) return;
        --window->pending_config_writes_;
        if (!ok) window->shell_->SetConfigurationError(true);
      }, weak_factory_.GetWeakPtr()));
}

bool WorkspaceWindow::CanRemoveProfile(std::string_view id) const {
  if (profiles_.size() <= 1 || !ProfileFor(id)) return false;
  for (const auto& workspace : model_.workspaces())
    if (workspace.profile_id == id) return false;
  return true;
}

bool WorkspaceWindow::RemoveProfile(std::string id) {
  if (is_private()) return false;
  if (!configuration_ready_ || configuration_invalid_ || profiles_busy() || !CanRemoveProfile(id)) return false;
  auto it = std::find_if(profiles_.begin(), profiles_.end(), [&](const auto& entry) { return entry.info.id == id; });
  CHECK(it != profiles_.end());
  const auto index = static_cast<size_t>(it - profiles_.begin());
  auto removed = std::make_unique<ProfileEntry>(std::move(*it));
  const auto directory = removed->info.directory;
  profiles_.erase(it);
  deleted_directories_.push_back(directory);
  profiles_busy_ = true;
  UpdateShell();
  shell_->SetProfileStatus(WorkspaceShell::ProfileStatusEvent::kDeleting);
  // Commit the catalogue and a deletion marker before touching any user data.
  SaveConfiguration(base::BindOnce([](base::WeakPtr<WorkspaceWindow> window, size_t index,
                                     std::unique_ptr<ProfileEntry> removed, bool ok) {
    if (!window) return;
    const auto directory = removed->info.directory;
    if (ok) {
      window->ScheduleProfileDeletion(directory);
      removed.reset();
    } else {
      window->profiles_.insert(window->profiles_.begin() + index, std::move(*removed));
      std::erase(window->deleted_directories_, directory);
    }
    window->profiles_busy_ = false;
    window->UpdateShell();
    window->shell_->SetProfileStatus(ok ? WorkspaceShell::ProfileStatusEvent::kDeleted : WorkspaceShell::ProfileStatusEvent::kDeleteNotSaved);
  }, weak_factory_.GetWeakPtr(), index, std::move(removed)));
  return true;
}

void WorkspaceWindow::ScheduleProfileDeletion(const std::string& directory) {
  auto* manager = g_browser_process->profile_manager();
  auto path = data_directory_.AppendASCII(directory);
  // If Chromium already completed cleanup on startup, do not recreate it.
  if (!manager->GetProfileAttributesStorage().GetProfileAttributesWithPath(path)) return;
  manager->GetDeleteProfileHelper().MaybeScheduleProfileForDeletion(
      path, base::BindOnce([](Profile*) {}), ProfileMetrics::DELETE_PROFILE_SETTINGS);
}

void WorkspaceWindow::RestoreConfiguration(std::string json) {
  auto dict = base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  WorkspaceModel restored;
  std::vector<ProfileEntry> entries;
  std::vector<std::string> deleted;
  std::map<std::string, std::vector<PinnedItem>> restored_pins;
  bool valid = dict.has_value();
  // Migrate the workspace-only format without moving the original profiles.
  if (!dict || !dict->contains("profiles")) {
    entries.push_back({{"work-profile", base::UTF16ToUTF8(T(u"Profil firmowy")), "ZitterWork", ""}, nullptr});
    entries.push_back({{"personal-profile", base::UTF16ToUTF8(T(u"Profil osobisty")), "ZitterPersonal", ""}, nullptr});
  } else if (const auto* list = dict->FindList("profiles")) {
    for (const auto& item : *list) {
      const auto* entry = item.GetIfDict();
      const auto* id = entry ? entry->FindString("id") : nullptr;
      const auto* name = entry ? entry->FindString("name") : nullptr;
      const auto* directory = entry ? entry->FindString("directory") : nullptr;
      if (!id || id->empty() || !name || TrimName(*name).empty() || !directory || !ValidDirectory(*directory)) { valid = false; break; }
      for (const auto& old : entries)
        if (old.info.id == *id || old.info.directory == *directory) valid = false;
      const auto* source = entry->FindString("source_id");
      entries.push_back({{*id, *name, *directory, source ? *source : ""}, nullptr});
      if (const auto* favorites = entry->FindList("pins")) {
        std::set<std::string> seen;
        for (const auto& value : *favorites) {
          const auto* pin = value.GetIfDict();
          const auto* key = pin ? pin->FindString("id") : nullptr;
          const auto* title = pin ? pin->FindString("title") : nullptr;
          const auto* url = pin ? pin->FindString("url") : nullptr;
          if (!key || key->empty() || !seen.insert(*key).second || !title || !url || !IsWorkspaceTabUrl(GURL(*url))) { valid = false; break; }
          restored_pins["@profile:" + *id].push_back({*key, "", *title, GURL(*url), false, true});
        }
      }
    }
    if (entries.empty()) valid = false;
  } else { valid = false; }
  if (dict) {
    if (const auto* list = dict->FindList("deleted_profiles")) {
      for (const auto& item : *list) {
        if (!item.is_string() || !ValidDirectory(item.GetString())) { valid = false; break; }
        for (const auto& entry : entries)
          if (entry.info.directory == item.GetString()) valid = false;
        deleted.push_back(item.GetString());
      }
    }
    if (const auto* list = dict->FindList("workspaces")) {
      if (list->empty()) valid = false;
      for (const auto& item : *list) {
        const auto* entry = item.GetIfDict();
        const auto* id = entry ? entry->FindString("id") : nullptr;
        const auto* name = entry ? entry->FindString("name") : nullptr;
        const auto* profile = entry ? entry->FindString("profile_id") : nullptr;
        bool found = false;
        for (const auto& candidate : entries) if (profile && candidate.info.id == *profile) found = true;
        if (!id || !name || !profile || !found || !restored.Add({*id, *profile, *name})) { valid = false; break; }
        if (const auto* pinned = entry->FindList("pins")) {
          std::set<std::string> seen;
          std::set<std::string> folders;
          for (const auto& value : *pinned) {
            const auto* pin = value.GetIfDict();
            const auto* pin_id = pin ? pin->FindString("id") : nullptr;
            const auto* parent = pin ? pin->FindString("parent_id") : nullptr;
            const auto* title = pin ? pin->FindString("title") : nullptr;
            const auto* url = pin ? pin->FindString("url") : nullptr;
            const bool folder = pin && pin->FindBool("folder").value_or(false);
            if (!pin_id || pin_id->empty() || !seen.insert(*pin_id).second || !parent ||
                (!parent->empty() && !folders.contains(*parent)) || !title || !url ||
                (!folder && !IsWorkspaceTabUrl(GURL(*url)))) { valid = false; break; }
            restored_pins[*id].push_back({*pin_id, *parent, *title, GURL(*url), folder});
            if (const auto* github = pin->FindDict("github"); folder && github) {
              GitHubLiveFolder config;
              if (const auto* login = github->FindString("login")) config.login = *login;
              if (const auto* repo = github->FindString("repository")) config.repository = *repo;
              if (const auto* token = github->FindString("encrypted_token")) config.encrypted_token = *token;
              restored_pins[*id].back().github = std::move(config);
            }
            if (folder) folders.insert(*pin_id);
          }
        }
      }
    } else { valid = false; }
  }
  if (valid) {
    const auto* active = dict->FindString("active");
    restored_active_ = active && restored.Find(*active) ? *active : restored.workspaces()[0].id;
    model_ = std::move(restored);
    if (const auto* rules = dict->FindList("link_rules")) {
      for (const auto& value : *rules) {
        const auto* rule = value.GetIfDict();
        const auto* host = rule ? rule->FindString("host") : nullptr;
        const auto* target = rule ? rule->FindString("workspace_id") : nullptr;
        if (!host || !target || !model_.Find(*target)) continue;
        const GURL url("https://" + *host + "/");
        if (!url.SchemeIsHTTPOrHTTPS() || url.host() != *host || url.has_username() || url.has_password()) continue;
        if (std::none_of(link_rules_.begin(), link_rules_.end(), [&](const auto& old) { return old.host == *host; }))
          link_rules_.push_back({*host, *target});
      }
    }
    pins_ = std::move(restored_pins);
    image_link_previews_ = dict->FindBool("image_link_previews").value_or(false);
    shift_click_peek_ = dict->FindBool("shift_click_peek").value_or(true);
    clean_address_copies_ = dict->FindBool("clean_address_copies").value_or(true);
    night_shift_config_ = NightShiftConfig::FromValue(dict->FindDict("night_shift"));
    night_shift_hidden_.clear();
    if (const auto* hidden = dict->FindList("night_shift_hidden"))
      for (const auto& id : *hidden) if (id.is_string()) night_shift_hidden_.insert(id.GetString());
    ++night_shift_generation_;
    assistant_settings_ = AssistantSettings::FromValue(dict->FindDict("assistant"));
    shell_->SetAssistantAllowed(assistant_settings_.enabled);
    onboarding_complete_ = dict->FindBool("onboarding_complete").value_or(true);
    if (const auto* starter = dict->FindString("starter_workspace"))
      if (!onboarding_complete_ && model_.Find(*starter)) starter_workspace_id_ = *starter;
    profiles_ = std::move(entries);
    deleted_directories_ = std::move(deleted);
  } else {
    configuration_invalid_ = !json.empty();
    if (json.empty()) {
#if BUILDFLAG(IS_WIN)
      if (offer_import_) {
        assistant_settings_.enabled = false;
        assistant_settings_.backends.fill(false);
        shell_->SetAssistantAllowed(false);
      }
#endif
      profiles_.push_back({{"personal-profile", base::UTF16ToUTF8(T(u"Profil osobisty")), "ZitterPersonal", ""}, nullptr});
      model_.Add({"personal", "personal-profile", base::UTF16ToUTF8(T(u"Moja przestrzeń"))});
      starter_workspace_id_ = "personal";
      restored_active_ = "personal";
    } else {
      // Invalid existing metadata retains the established recovery behavior.
      profiles_.push_back({{"work-profile", base::UTF16ToUTF8(T(u"Profil firmowy")), "ZitterWork", ""}, nullptr});
      profiles_.push_back({{"personal-profile", base::UTF16ToUTF8(T(u"Profil osobisty")), "ZitterPersonal", ""}, nullptr});
      model_.Add({"work", "work-profile", base::UTF16ToUTF8(T(u"Praca"))});
      model_.Add({"personal", "personal-profile", base::UTF16ToUTF8(T(u"Prywatne"))});
      restored_active_ = "work";
    }
  }
  LoadStartupProfiles();
}

bool WorkspaceWindow::StartupProfilesReady() const {
  if (is_private()) return true;
  return !profiles_.empty() && std::all_of(profiles_.begin(), profiles_.end(),
      [](const auto& entry) { return entry.lease != nullptr; });
}

bool WorkspaceWindow::DeferForProfile(std::string_view workspace, base::OnceClosure action) {
  const auto* space = model_.Find(workspace);
  if (!space) return false;
  // A newer explicit intent supersedes a pending selection/navigation.
  pending_profile_action_.Reset();
  if (ProfileFor(space->profile_id)) return false;
  const auto profile_id = space->profile_id;
  pending_profile_id_ = profile_id;
  pending_profile_action_ = base::BindOnce(
      [](base::WeakPtr<WorkspaceWindow> window, std::string workspace,
         std::string profile_id, base::OnceClosure action) {
        if (!window || window->closing_) return;
        const auto* space = window->model_.Find(workspace);
        if (space && space->profile_id == profile_id && window->ProfileFor(profile_id))
          std::move(action).Run();
      }, weak_factory_.GetWeakPtr(), std::string(workspace), profile_id, std::move(action));
  for (size_t i = 0; i < profiles_.size(); ++i)
    if (profiles_[i].info.id == profile_id && !startup_profiles_requested_.contains(profile_id)) {
      LoadStartupProfile(i);
      break;
    }
  return true;
}

void WorkspaceWindow::LoadStartupProfiles() {
  CHECK(!profiles_.empty());
  size_t primary = 0;
  if (const auto* active = model_.Find(restored_active_)) {
    for (size_t i = 0; i < profiles_.size(); ++i)
      if (profiles_[i].info.id == active->profile_id) primary = i;
  }
  startup_profile_order_ = {profiles_[primary].info.id};
  for (size_t i = 0; i < profiles_.size(); ++i)
    if (i != primary) startup_profile_order_.push_back(profiles_[i].info.id);
  next_startup_profile_ = 1;
  LoadStartupProfile(primary);
  // Already initialized profiles need no disk/service startup and retain the
  // synchronous switching behavior of existing windows.
  for (size_t i = 0; i < profiles_.size(); ++i)
    if (i != primary && g_browser_process->profile_manager()->GetProfileByPath(
        data_directory_.AppendASCII(profiles_[i].info.directory))) LoadStartupProfile(i);
}

void WorkspaceWindow::ContinueStartupProfileLoading() {
  if (closing_ || startup_profile_failed_) return;
  if (!configuration_ready_) {
    const auto* active = model_.Find(restored_active_);
    if (!active || !ProfileFor(active->profile_id)) return;
    FinishConfiguration();
    // Give initial active-window work a head start over background services.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(FROM_HERE,
        base::BindOnce([](base::WeakPtr<WorkspaceWindow> window) {
          if (!window) return;
          window->startup_background_allowed_ = true;
          window->ContinueStartupProfileLoading();
        }, weak_factory_.GetWeakPtr()), base::Milliseconds(100));
    return;
  }
  if (!startup_background_allowed_) { UpdateShell(); return; }
  while (pending_startup_profiles_ < 2 && next_startup_profile_ < startup_profile_order_.size()) {
    const auto id = startup_profile_order_[next_startup_profile_++];
    if (startup_profiles_requested_.contains(id)) continue;
    const auto entry = std::find_if(profiles_.begin(), profiles_.end(),
        [&](const auto& candidate) { return candidate.info.id == id; });
    if (entry != profiles_.end()) LoadStartupProfile(entry - profiles_.begin());
  }
  UpdateShell();
}

void WorkspaceWindow::SetStartupProfileLoaderForTesting(StartupProfileLoader loader) {
  CHECK(!configuration_ready_ && startup_profiles_requested_.empty());
  startup_profile_loader_for_testing_ = std::move(loader);
}

void WorkspaceWindow::LoadStartupProfile(size_t index) {
  const auto id = profiles_[index].info.id;
  const auto directory = profiles_[index].info.directory;
  if (!startup_profiles_requested_.insert(id).second) return;
  ++pending_startup_profiles_;
  const auto path = data_directory_.AppendASCII(directory);
  auto complete = base::BindOnce(&WorkspaceWindow::StartupProfileLoaded,
                                weak_factory_.GetWeakPtr(), id, directory);
  if (startup_profile_loader_for_testing_) {
    startup_profile_loader_for_testing_.Run(path, std::move(complete));
    return;
  }
  auto* manager = g_browser_process->profile_manager();
  if (manager->GetProfileByPath(path)) {
    manager->CreateProfileAsync(path, std::move(complete));
    return;
  }
  // A non-directory profile path can crash Chromium's keyed database services
  // before its failure callback. Check the filesystem on the config runner.
  config_runner_->PostTaskAndReplyWithResult(FROM_HERE,
      base::BindOnce([](base::FilePath path) {
        return !base::PathExists(path) || base::DirectoryExists(path);
      }, path),
      base::BindOnce([](base::WeakPtr<WorkspaceWindow> window, base::FilePath path,
                       base::OnceCallback<void(Profile*)> complete, bool valid) {
        if (!window || window->closing_) return;
        if (!valid) { std::move(complete).Run(nullptr); return; }
        g_browser_process->profile_manager()->CreateProfileAsync(path, std::move(complete));
      }, weak_factory_.GetWeakPtr(), path, std::move(complete)));
}

void WorkspaceWindow::StartupProfileLoaded(std::string id, std::string directory, Profile* profile) {
  if (closing_) return;
  CHECK_GT(pending_startup_profiles_, 0u);
  --pending_startup_profiles_;
  auto entry = std::find_if(profiles_.begin(), profiles_.end(),
      [&](const auto& candidate) { return candidate.info.id == id && candidate.info.directory == directory; });
  if (entry == profiles_.end()) return;
  auto lease = profile ? ScopedProfileKeepAlive::TryAcquire(profile, ProfileKeepAliveOrigin::kBrowserWindow) : nullptr;
  if (!lease || profile->GetPath() != data_directory_.AppendASCII(directory)) {
    startup_profile_failed_ = true;
    pending_profile_action_.Reset();
    shell_->SetProfileStatus(WorkspaceShell::ProfileStatusEvent::kLoadFailed);
    shell_->OpenProfiles();
    return;
  }
  if (auto* attributes = g_browser_process->profile_manager()->GetProfileAttributesStorage().GetProfileAttributesWithPath(profile->GetPath()))
    attributes->SetLocalProfileName(base::UTF8ToUTF16(entry->info.name), false);
  entry->lease = std::move(lease);
  for (auto& host : hosts_) if (host.profile_id == id) {
    CHECK(!host.profile);
    host.profile = profile;
    for (const auto& view : host.tabs) {
      CHECK(!view->web_contents());
      view->SetBrowserContext(profile);
    }
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
      base::BindOnce([](base::WeakPtr<WorkspaceWindow> window) {
        if (!window || window->closing_) return;
        // The pending action may target a different profile still loading.
        // Its wrapper rechecks the immutable profile assignment.
        if (window->pending_profile_action_ && window->ProfileFor(window->pending_profile_id_))
          std::move(window->pending_profile_action_).Run();
        if (window) window->ContinueStartupProfileLoading();
      }, weak_factory_.GetWeakPtr()));
}

void WorkspaceWindow::FinishConfiguration() {
  closing_ = true;
  for (const auto& workspace : model_.workspaces()) {
    hosts_.emplace_back();
    auto& host = hosts_.back();
    host.workspace_id = workspace.id;
    host.profile_id = workspace.profile_id;
    host.profile = ProfileFor(workspace.profile_id);
    host.view = CreatePage(host.profile, nullptr, nullptr, false);
    host.tabs.push_back(host.view);
  }
  RestoreSession();
  closing_ = false;
  configuration_ready_ = true;
  Select(restored_active_);
  AssistantStateChanged();
  // Saved scheduled tasks need the panel, even when nobody opens it.
  LoadAssistantSessions(base::BindOnce([](base::WeakPtr<WorkspaceWindow> window, std::optional<base::DictValue> saved) {
    const auto* tasks = saved ? saved->FindList("tasks") : nullptr;
    if (window && tasks && !tasks->empty()) window->EnsureAssistant();
  }, weak_factory_.GetWeakPtr()));
  shell_->WarmWorkspaceSidebars(pins_);
  SaveConfiguration();
  for (const auto& directory : deleted_directories_) ScheduleProfileDeletion(directory);
  if (configuration_invalid_) shell_->SetConfigurationError(true);
  if (ready_callback_) std::move(ready_callback_).Run();
  if (offer_import_ && !onboarding_complete_ && !configuration_invalid_) {
#if BUILDFLAG(IS_WIN)
    ShowWelcome();
#else
    shell_->OpenImport();
#endif
  }
}

#if BUILDFLAG(IS_WIN)
namespace {
SkBitmap ReadBrowserIcon(const base::FilePath& path, int index) {
  // Only read resources from local application files.
  const auto& value = path.value();
  if (!path.IsAbsolute() || value.size() < 3 || value[1] != L':') return {};
  HICON large = nullptr;
  const auto count = ::ExtractIconExW(value.c_str(), index, &large, nullptr, 1);
  base::win::ScopedGDIObject<HICON> icon(large);
  if (count != 1 || !icon.is_valid()) return {};
  auto bitmap = IconUtil::CreateSkBitmapFromHICON(icon.get());
  bitmap.setImmutable();
  return bitmap;
}

SkBitmap ReadArcBrowserIcon() {
  constexpr wchar_t kArcFamily[] = L"TheBrowserCompany.Arc_ttt1ap7aakyb4";
  UINT32 count = 0;
  UINT32 length = 0;
  if (::GetPackagesByPackageFamily(kArcFamily, &count, nullptr, &length, nullptr) !=
      ERROR_INSUFFICIENT_BUFFER) return {};
  std::vector<PWSTR> names(count);
  std::vector<wchar_t> buffer(length);
  if (::GetPackagesByPackageFamily(kArcFamily, &count, names.data(), &length, buffer.data()) !=
      ERROR_SUCCESS) return {};
  for (UINT32 i = 0; i < count; ++i) {
    UINT32 path_length = 0;
    if (::GetPackagePathByFullName(names[i], &path_length, nullptr) !=
        ERROR_INSUFFICIENT_BUFFER) continue;
    std::vector<wchar_t> path(path_length);
    if (::GetPackagePathByFullName(names[i], &path_length, path.data()) != ERROR_SUCCESS)
      continue;
    const base::FilePath package(path.data());
    auto icon = ReadBrowserIcon(package.AppendASCII("assets/DesktopShortcut.ico"), 0);
    if (icon.drawsNothing()) icon = ReadBrowserIcon(package.AppendASCII("Arc.exe"), 0);
    if (!icon.drawsNothing()) return icon;
  }
  return {};
}

SkBitmap FindWelcomeBrowserIcon(ImportBrowser browser) {
  std::u16string_view executable;
  switch (browser) {
    case ImportBrowser::kArc: return ReadArcBrowserIcon();
    case ImportBrowser::kChrome: executable = u"chrome.exe"; break;
    case ImportBrowser::kEdge: executable = u"msedge.exe"; break;
    case ImportBrowser::kBrave: executable = u"brave.exe"; break;
    case ImportBrowser::kFirefox: executable = u"firefox.exe"; break;
    case ImportBrowser::kOpera: executable = u"opera.exe"; break;
    case ImportBrowser::kVivaldi: executable = u"vivaldi.exe"; break;
  }
  for (HKEY hive : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
    for (base::win::RegistryValueIterator registration(hive, L"Software\\RegisteredApplications");
         registration.Valid(); ++registration) {
      if (registration.Type() != REG_SZ) continue;
      const std::wstring_view capabilities(registration.Value());
      if (!capabilities.starts_with(L"Software\\Clients\\StartMenuInternet\\")) continue;
      base::win::RegKey key(hive, registration.Value(), KEY_QUERY_VALUE);
      std::wstring location;
      if (key.ReadValue(L"ApplicationIcon", &location) != ERROR_SUCCESS) continue;
      const size_t comma = location.rfind(L',');
      int index = 0;
      if (comma != std::wstring::npos) {
        if (!base::StringToInt(base::AsStringPiece16(location).substr(comma + 1), &index)) continue;
        location.resize(comma);
      }
      if (location.size() >= 2 && location.front() == L'"' && location.back() == L'"')
        location = location.substr(1, location.size() - 2);
      const base::FilePath path(location);
      if (!base::EqualsCaseInsensitiveASCII(base::AsStringPiece16(path.BaseName().value()),
                                            executable)) continue;
      auto icon = ReadBrowserIcon(path, index);
      if (!icon.drawsNothing()) return icon;
    }
  }
  return {};
}
}  // namespace

void WorkspaceWindow::ShowWelcome() {
  // Which browsers this computer has: their data folders, read off the UI thread.
  config_runner_->PostTaskAndReplyWithResult(FROM_HERE, base::BindOnce([] {
        std::vector<WorkspaceShell::WelcomeSetup::Source> found;
        for (const auto browser : {ImportBrowser::kChrome, ImportBrowser::kEdge, ImportBrowser::kBrave,
                                   ImportBrowser::kArc, ImportBrowser::kFirefox,
                                   ImportBrowser::kOpera, ImportBrowser::kVivaldi})
          if (base::DirectoryExists(DefaultBrowserDirectory(browser)))
            found.push_back({browser, FindWelcomeBrowserIcon(browser)});
        return found;
      }),
      base::BindOnce([](base::WeakPtr<WorkspaceWindow> window,
                       std::vector<WorkspaceShell::WelcomeSetup::Source> found) {
        if (!window || window->onboarding_complete_) return;
        const auto* starter = window->model_.Find(window->starter_workspace_id_);
        WorkspaceShell::WelcomeSetup setup;
        setup.sources = std::move(found);
        setup.space_name = starter && !starter->name.empty() ? base::UTF8ToUTF16(starter->name) : T(u"Moja przestrzeń");
        setup.space_color = window->shell_->workspace_color(window->starter_workspace_id_);
        setup.assistant_enabled = window->assistant_settings_.enabled;
        setup.set_assistant_enabled = base::BindRepeating([](base::WeakPtr<WorkspaceWindow> window, bool enabled) {
          if (!window || window->configuration_invalid_) return;
          auto settings = window->assistant_settings_;
          settings.enabled = enabled;
          window->SetAssistantSettings(std::move(settings));
          window->AssistantStateChanged();
        }, window);
        setup.import_browser = base::BindRepeating(&WorkspaceWindow::StartWelcomeImport, window);
        setup.import_passwords = base::BindRepeating([](base::WeakPtr<WorkspaceWindow> window,
            gfx::NativeWindow owner, base::OnceCallback<void(std::u16string)> report) {
          auto* page = window ? window->contents(window->active_id()) : nullptr;
          if (!page) { std::move(report).Run(T(u"Nie udało się zaimportować pliku CSV.")); return; }
          ImportPasswordsFromCsv(page, owner, base::BindOnce([](base::WeakPtr<WorkspaceWindow> window,
              std::string workspace, base::OnceCallback<void(std::u16string)> report, PasswordCsvOutcome outcome) {
            if (window && outcome.imported && workspace == window->starter_workspace_id_)
              window->starter_has_imported_passwords_ = true;
            if (outcome.cancelled) { std::move(report).Run({}); return; }
            std::move(report).Run(outcome.imported ? TF(u"Zaimportowano haseł: {}.", {base::NumberToString16(outcome.count)})
                                                   : T(u"Nie udało się zaimportować pliku CSV."));
          }, window, window->active_id(), std::move(report)));
        }, window->weak_factory_.GetWeakPtr());
        window->shell_->ShowWelcome(std::move(setup), base::BindOnce(&WorkspaceWindow::WelcomeFinished, window));
      }, weak_factory_.GetWeakPtr()));
}

void WorkspaceWindow::WelcomeFinished(WorkspaceShell::WelcomeResult result) {
  // An import owns the first-run placeholder until it has committed or failed.
  if (welcome_import_done_ || onboarding_complete_) return;
  const std::string starter = starter_workspace_id_;
  if (const auto* space = model_.Find(starter)) {
    const std::string name = base::UTF16ToUTF8(result.space_name);
    if (!name.empty() && name != space->name) UpdateWorkspace(starter, name, space->profile_id);
    if (result.space_color) shell_->SetWorkspaceColor(starter, *result.space_color);
  }
  DismissImport();
}

void WorkspaceWindow::StartWelcomeImport(BrowserImportOptions options, base::OnceCallback<void(bool, std::u16string)> done) {
  if (!configuration_ready_ || configuration_invalid_ || profiles_busy_ || welcome_import_done_) {
    std::move(done).Run(false, T(u"Nie można teraz rozpocząć importu. Spróbuj ponownie."));
    return;
  }
  welcome_import_done_ = std::move(done);
  PreviewBrowserImport(options);
}
#endif

const std::vector<PinnedItem>& WorkspaceWindow::pinned_items(std::string_view workspace_id) const {
  std::vector<PinnedItem> combined;
  if (const auto* space = model_.Find(workspace_id)) {
    if (auto found = pins_.find("@profile:" + space->profile_id); found != pins_.end()) combined = found->second;
  }
  if (auto found = pins_.find(std::string(workspace_id)); found != pins_.end())
    combined.insert(combined.end(), found->second.begin(), found->second.end());
  auto& cached = combined_pins_[std::string(workspace_id)];
  if (cached != combined) cached = std::move(combined);
  return cached;
}

std::string WorkspaceWindow::PinStorageKey(std::string_view workspace, std::string_view pin) const {
  if (const auto* space = model_.Find(workspace)) {
    const std::string key = "@profile:" + space->profile_id;
    if (auto found = pins_.find(key); found != pins_.end())
      for (const auto& item : found->second) if (item.id == pin) return key;
  }
  return std::string(workspace);
}

void WorkspaceWindow::OpenPinnedItem(std::string id) {
  const auto& pins = pinned_items(active_id());
  const auto pin = std::find_if(pins.begin(), pins.end(), [&](const auto& item) { return item.id == id && !item.folder; });
  if (pin == pins.end()) return;
  if (peek_view_ && GuardPages({peek_contents()}, base::BindOnce(&WorkspaceWindow::OpenPinnedItem, weak_factory_.GetWeakPtr(), id))) return;
  const GURL url = pin->url;
  if (const auto target = WorkspaceForUrl(url); !target.empty() && target != active_id()) {
    OpenLink(url, active_id(), true); return;
  }
  for (auto& host : hosts_) {
    if (host.workspace_id != active_id()) continue;
    if (auto found = host.pin_tabs.find(id); found != host.pin_tabs.end()) {
      auto tab = std::find(host.tabs.begin(), host.tabs.end(), found->second);
      if (tab != host.tabs.end()) SelectTab(tab - host.tabs.begin());
      return;
    }
    host.tabs.push_back(CreatePage(host.profile));
    host.pin_tabs[id] = host.tabs.back();
    SelectTab(host.tabs.size() - 1);
    NavigateFromBar(url, false);
    return;
  }
}

namespace {
std::vector<PinNode> PinNodes(const std::vector<PinnedItem>& items) {
  std::vector<PinNode> nodes;
  nodes.reserve(items.size());
  for (const auto& item : items) nodes.push_back({item.id, item.parent_id, item.folder});
  return nodes;
}

bool IsGitHubChild(const std::vector<PinnedItem>& items, std::string_view parent) {
  return !parent.empty() && std::ranges::any_of(items, [&](const auto& item) {
    return item.id == parent && item.github.has_value();
  });
}

bool ValidGitHubScope(const GitHubLiveFolder& config) {
  return (!config.login.empty() || !config.repository.empty()) &&
      (config.login.empty() || RE2::FullMatch(config.login, "[a-zA-Z0-9-]{1,39}")) &&
      (config.repository.empty() ||
       RE2::FullMatch(config.repository, "[a-zA-Z0-9-]{1,39}/[a-zA-Z0-9_.-]{1,100}"));
}

bool ValidGitHubToken(std::string_view token) {
  return RE2::FullMatch(token, "[a-zA-Z0-9_]{1,512}");
}

std::string GitHubPullKey(const GURL& url) {
  if (!url.SchemeIs("https") || url.host() != "github.com" ||
      url.has_username() || url.has_password() || url.has_port() ||
      !RE2::FullMatch(url.path(), "/[a-zA-Z0-9-]+/[a-zA-Z0-9_.-]+/pull/[1-9][0-9]*"))
    return {};
  return base::ToLowerASCII(url.path());
}
}  // namespace

bool WorkspaceWindow::InsertPin(std::string workspace, PinnedItem item, std::string before) {
  if (!configuration_ready_ || configuration_invalid_ || profiles_busy_ || !model_.Find(workspace)) return false;
  if (item.parent_id == "@profile") { item.parent_id.clear(); item.profile_pin = true; }
  if (item.profile_pin) workspace = "@profile:" + model_.Find(workspace)->profile_id;
  auto& items = pins_[workspace];
  if (IsGitHubChild(items, item.parent_id)) return false;
  const auto nodes = PinNodes(items);
  if (!PinTree(nodes).CanPlace(item.id, item.parent_id)) return false;
  auto previous = items;
  std::erase_if(items, [&](const auto& other) { return other.id == item.id; });
  // Siblings keep vector order; an empty `before` appends after the last one.
  auto at = std::find_if(items.begin(), items.end(), [&](const auto& other) { return other.id == before && other.parent_id == item.parent_id; });
  items.insert(at, std::move(item));
  SaveFolderEdit(std::move(workspace), std::move(previous));
  return true;
}
bool WorkspaceWindow::PinTab(size_t index, std::string parent, std::string before) {
  for (auto& host : hosts_) {
    if (host.workspace_id != active_id() || index >= host.tabs.size()) continue;
    auto* view = host.tabs[index].get();
    for (const auto& [id, pinned] : host.pin_tabs) if (pinned == view) return MovePin(id, std::move(parent), std::move(before));
    const GURL url = TabUrl(host, view);
    if (!IsWorkspaceTabUrl(url)) return false;
    auto title = base::UTF16ToUTF8(TabTitle(host, view));
    if (title.empty()) title = url.host();
    PinnedItem item{"pin-" + base::Uuid::GenerateRandomV4().AsLowercaseString(), std::move(parent), std::move(title), url, false};
    const auto id = item.id;
    host.pin_tabs[id] = view;
    if (InsertPin(host.workspace_id, std::move(item), std::move(before))) return true;
    host.pin_tabs.erase(id);
    return false;
  }
  return false;
}
bool WorkspaceWindow::UpdatePinnedUrl(std::string workspace, std::string id, GURL url) {
  if (!configuration_ready_ || configuration_invalid_ || profiles_busy_ || !model_.Find(workspace) ||
      !url.is_valid() || !IsWorkspaceTabUrl(url)) return false;
  workspace = PinStorageKey(workspace, id);
  auto& items = pins_[workspace];
  auto pin = std::find_if(items.begin(), items.end(), [&](const auto& item) { return item.id == id && !item.folder; });
  if (pin == items.end() || IsGitHubChild(items, pin->parent_id)) return false;
  if (pin->url == url) return true;
  auto previous = items;
  pin->url = std::move(url);
  SaveFolderEdit(std::move(workspace), std::move(previous));
  return true;
}
bool WorkspaceWindow::ResetPinnedUrl(std::string id) {
  if (!configuration_ready_ || configuration_invalid_ || profiles_busy_) return false;
  const auto& items = pinned_items(active_id());
  auto pin = std::find_if(items.begin(), items.end(), [&](const auto& item) { return item.id == id && !item.folder; });
  if (pin == items.end() || !IsWorkspaceTabUrl(pin->url)) return false;
  if (peek_view_ && GuardPages({peek_contents()}, base::BindOnce(
      [](base::WeakPtr<WorkspaceWindow> window, std::string workspace, std::string id) {
        if (window && window->active_id() == workspace) window->ResetPinnedUrl(std::move(id));
      }, weak_factory_.GetWeakPtr(), active_id(), id))) return true;
  ClosePeek();
  const GURL url = pin->url;
  for (auto& host : hosts_) if (host.workspace_id == active_id()) {
    views::WebView* view;
    if (auto found = host.pin_tabs.find(id); found != host.pin_tabs.end()) view = found->second;
    else { view = CreatePage(host.profile); host.tabs.push_back(view); host.pin_tabs[id] = view; }
    if (host.deferred_urls.contains(view)) host.deferred_urls[view] = url;
    SelectTab(std::find(host.tabs.begin(), host.tabs.end(), view) - host.tabs.begin());
    auto* page = EnsurePage(view);
    if (page->GetVisibleURL() != url)
      LoadWorkspaceTab(page, url, ui::PAGE_TRANSITION_AUTO_BOOKMARK);
    return true;
  }
  return false;
}
bool WorkspaceWindow::UnpinItem(std::string id, size_t before) {
  if (!configuration_ready_ || configuration_invalid_ || profiles_busy_) return false;
  for (auto& host : hosts_) {
    if (host.workspace_id != active_id() || before > host.tabs.size()) continue;
    const auto storage = PinStorageKey(host.workspace_id, id);
    auto& items = pins_[storage];
    const auto pin = std::find_if(items.begin(), items.end(), [&](const auto& item) { return item.id == id && !item.folder; });
    if (pin == items.end() || IsGitHubChild(items, pin->parent_id)) return false;
    size_t index;
    if (auto found = host.pin_tabs.find(id); found != host.pin_tabs.end()) {
      // An opened pin keeps its page and simply becomes an ordinary tab.
      index = std::find(host.tabs.begin(), host.tabs.end(), found->second) - host.tabs.begin();
      host.pin_tabs.erase(found);
    } else {
      // A closed pin becomes a tab that loads when it is first shown.
      auto* view = CreatePage(host.profile, nullptr, nullptr, false);
      host.deferred_urls[view] = pin->url;
      host.deferred_titles[view] = base::UTF8ToUTF16(pin->title);
      host.tabs.push_back(view);
      index = host.tabs.size() - 1;
    }
    MoveTab(index, before);
    auto previous = items;
    items.erase(pin);
    SaveFolderEdit(storage, std::move(previous), {id});
    return true;
  }
  return false;
}
bool WorkspaceWindow::MovePin(std::string id, std::string parent, std::string before) {
  const auto& items = pinned_items(active_id());
  const auto pin = std::find_if(items.begin(), items.end(), [&](const auto& item) { return item.id == id; });
  if (pin == items.end() || id == before || IsGitHubChild(items, pin->parent_id)) return false;
  PinnedItem moved = *pin;
  if (parent == "@profile" && !moved.profile_pin) {
    const auto* space = model_.Find(active_id());
    if (!space || moved.folder || !configuration_ready_ || profiles_busy_) return false;
    const auto workspace = active_id();
    const auto storage = "@profile:" + space->profile_id;
    auto previous = pins_[workspace];
    auto previous_profile = pins_[storage];
    std::erase_if(pins_[workspace], [&](const auto& item) { return item.id == id; });
    moved.parent_id.clear(); moved.profile_pin = true;
    auto& favorites = pins_[storage];
    favorites.insert(std::ranges::find(favorites, before, &PinnedItem::id),
                     std::move(moved));
    profiles_busy_ = true; UpdateShell();
    SaveConfiguration(base::BindOnce([](base::WeakPtr<WorkspaceWindow> window, std::string workspace, std::string storage,
        std::vector<PinnedItem> previous, std::vector<PinnedItem> previous_profile, bool ok) {
      if (!window) return;
      if (!ok) { window->pins_[workspace] = std::move(previous); window->pins_[storage] = std::move(previous_profile); window->shell_->ShowFolderSaveError(); }
      window->profiles_busy_ = false; window->UpdateShell();
    }, weak_factory_.GetWeakPtr(), workspace, storage, std::move(previous), std::move(previous_profile)));
    return true;
  }
  moved.parent_id = moved.profile_pin ? "" : std::move(parent);
  return InsertPin(active_id(), std::move(moved), std::move(before));
}

bool WorkspaceWindow::CreatePinnedFolder(std::string workspace, std::string parent, std::string name) {
  name = std::string(base::TrimWhitespaceASCII(name, base::TRIM_ALL));
  if (!configuration_ready_ || configuration_invalid_ || profiles_busy_ || !model_.Find(workspace) ||
      name.empty() || name.size() > 256 || !base::IsStringUTF8(name)) return false;
  auto& items = pins_[workspace];
  if (IsGitHubChild(items, parent)) return false;
  const auto id = "folder-" + base::Uuid::GenerateRandomV4().AsLowercaseString();
  const auto nodes = PinNodes(items);
  if (!PinTree(nodes).CanPlace(id, parent)) return false;
  items.push_back({id, parent, name, GURL(), true});
  profiles_busy_ = true;
  UpdateShell();
  SaveConfiguration(base::BindOnce([](base::WeakPtr<WorkspaceWindow> window, std::string workspace, std::string id, bool ok) {
    if (!window) return;
    if (!ok) std::erase_if(window->pins_[workspace], [&](const auto& item) { return item.id == id; });
    window->profiles_busy_ = false;
    window->UpdateShell();
    if (!ok) window->shell_->ShowFolderSaveError();
  }, weak_factory_.GetWeakPtr(), workspace, id));
  return true;
}

bool WorkspaceWindow::ConfigureGitHubFolder(std::string workspace, std::string folder,
                                            std::string name, GitHubLiveFolder config,
                                            std::string token, bool remove_token) {
  if (is_private() || !configuration_ready_ || configuration_invalid_ || profiles_busy() ||
      workspace != active_id() || name.empty() || name.size() > 256 || !base::IsStringUTF8(name) ||
      !ValidGitHubScope(config) || (!token.empty() && !ValidGitHubToken(token)))
    return false;

  if (!folder.empty()) {
    const auto& items = pinned_items(workspace);
    const auto item = std::ranges::find(items, folder, &PinnedItem::id);
    if (item == items.end() || !item->github) return false;
    if (!remove_token) config.encrypted_token = item->github->encrypted_token;
  }
  if (token.empty()) {
    SaveGitHubFolder(std::move(workspace), std::move(folder), std::move(name), std::move(config));
    return true;
  }

  profiles_busy_ = true;
  UpdateShell();
  g_browser_process->os_crypt_async()->GetInstance(base::BindOnce(
      [](base::WeakPtr<WorkspaceWindow> window, std::string workspace, std::string folder,
         std::string name, GitHubLiveFolder config, std::string token,
         scoped_refptr<os_crypt_async::Encryptor> encryptor) {
        if (!window) return;
        window->profiles_busy_ = false;
        std::string ciphertext;
        if (!encryptor->IsEncryptionAvailable() || !encryptor->EncryptString(token, &ciphertext)) {
          window->UpdateShell();
          window->shell_->ShowToast(T(u"Nie udało się bezpiecznie zapisać tokena GitHub"), {}, {});
          return;
        }
        config.encrypted_token = base::Base64Encode(ciphertext);
        window->SaveGitHubFolder(std::move(workspace), std::move(folder), std::move(name), std::move(config));
      }, weak_factory_.GetWeakPtr(), std::move(workspace), std::move(folder),
      std::move(name), std::move(config), std::move(token)));
  return true;
}

void WorkspaceWindow::SaveGitHubFolder(std::string workspace, std::string folder,
                                       std::string name, GitHubLiveFolder config) {
  auto& items = pins_[workspace];
  auto previous = items;
  if (folder.empty()) {
    folder = "github-folder-" + base::Uuid::GenerateRandomV4().AsLowercaseString();
    items.push_back({folder, "", std::move(name), GURL(), true, false, std::move(config)});
  } else {
    auto item = std::ranges::find(items, folder, &PinnedItem::id);
    CHECK(item != items.end() && item->github);
    item->title = std::move(name);
    item->github = std::move(config);
  }
  RefreshGitHubFolder(workspace, folder);
  SaveFolderEdit(std::move(workspace), std::move(previous));
}

void WorkspaceWindow::RefreshGitHubFolder(std::string workspace, std::string folder) {
  const auto& items = pinned_items(workspace);
  const auto item = std::ranges::find(items, folder, &PinnedItem::id);
  if (is_private() || workspace != active_id() || item == items.end() ||
      !item->github || !ValidGitHubScope(*item->github))
    return;
  github_manual_refresh_.emplace(std::move(workspace), std::move(folder));
  github_timer_.Stop();
  ScheduleGitHubRefresh();
}

void WorkspaceWindow::ScheduleGitHubRefresh() {
  if (closing_ || is_private() || !configuration_ready_ || configuration_invalid_ ||
      profiles_busy() || github_refresh_ || github_timer_.IsRunning()) return;
  const auto workspace = pins_.find(active_id());
  if (workspace == pins_.end()) return;
  const auto now = base::TimeTicks::Now();
  auto next = base::TimeTicks::Max();
  for (const auto& pin : workspace->second) {
    if (!pin.github || !ValidGitHubScope(*pin.github)) continue;
    const GitHubFolderKey key{workspace->first, pin.id};
    const auto previous = github_next_refresh_.find(key);
    const auto due = github_manual_refresh_ == key || previous == github_next_refresh_.end()
        ? now : previous->second;
    next = std::min(next, due);
  }
  if (!next.is_max()) {
    github_timer_.Start(FROM_HERE, std::max(base::Seconds(1), next - now),
                        this, &WorkspaceWindow::StartGitHubRefresh);
  }
}

void WorkspaceWindow::StartGitHubRefresh() {
  if (closing_ || profiles_busy() || github_refresh_) return;
  const auto workspace = pins_.find(active_id());
  const auto* space = model_.Find(active_id());
  if (workspace == pins_.end() || !space || !ProfileFor(space->profile_id)) return;

  const auto now = base::TimeTicks::Now();
  const PinnedItem* target = nullptr;
  bool manual = false;
  for (const auto& pin : workspace->second) {
    if (!pin.github || !ValidGitHubScope(*pin.github)) continue;
    const GitHubFolderKey key{workspace->first, pin.id};
    if (github_manual_refresh_ == key) {
      target = &pin;
      manual = true;
      break;
    }
    const auto due = github_next_refresh_.find(key);
    if (!target && (due == github_next_refresh_.end() || due->second <= now)) target = &pin;
  }
  github_manual_refresh_.reset();
  if (!target) {
    ScheduleGitHubRefresh();
    return;
  }

  auto& job = github_refresh_.emplace();
  job.key = {workspace->first, target->id};
  job.profile_id = space->profile_id;
  job.config = *target->github;
  job.manual = manual;
  github_next_refresh_[job.key] = now + base::Minutes(5);
  const auto& [login, repository, encrypted_token] = job.config;
  const std::string scope = repository.empty() ? "" : " repo:" + repository;
  if (login.empty()) {
    job.queries.push_back(scope);
  } else {
    job.queries.push_back(scope + " review-requested:" + login);
    job.queries.push_back(scope + " author:" + login);
  }
  if (encrypted_token.empty()) {
    FetchGitHubResults("");
    return;
  }
  g_browser_process->os_crypt_async()->GetInstance(base::BindOnce(
      [](base::WeakPtr<WorkspaceWindow> window, std::string encrypted,
         scoped_refptr<os_crypt_async::Encryptor> encryptor) {
        if (!window) return;
        std::string ciphertext, token;
        if (!base::Base64Decode(encrypted, &ciphertext) ||
            !encryptor->DecryptString(ciphertext, &token) || !ValidGitHubToken(token)) {
          window->FinishGitHubRefresh(false);
          return;
        }
        window->FetchGitHubResults(std::move(token));
      }, weak_factory_.GetWeakPtr(), encrypted_token));
}

void WorkspaceWindow::FetchGitHubResults(std::string token) {
  auto& job = *github_refresh_;
  auto* profile = ProfileFor(job.profile_id);
  const auto* space = model_.Find(job.key.first);
  if (!profile || !space || space->profile_id != job.profile_id) {
    FinishGitHubRefresh(false);
    return;
  }
  const auto& pins = pinned_items(job.key.first);
  const auto folder = std::ranges::find(pins, job.key.second, &PinnedItem::id);
  if (folder == pins.end() || folder->github != job.config) {
    FinishGitHubRefresh(false);
    return;
  }
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GURL("https://api.github.com/search/issues?sort=updated&per_page=100&q=" +
      base::EscapeQueryParamValue("is:open is:pr" + job.queries.back(), true));
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->redirect_mode = network::mojom::RedirectMode::kError;
  request->load_flags = net::LOAD_DISABLE_CACHE;
  request->headers.SetHeader("Accept", "application/vnd.github+json");
  if (!token.empty()) request->headers.SetHeader("Authorization", "Bearer " + token);
  job.queries.pop_back();

  constexpr auto annotation = net::DefineNetworkTrafficAnnotation("zitter_github_live_folder", R"(
    semantics {
      sender: "Zitter GitHub Live Folders"
      description: "Fetches open pull requests for a GitHub folder configured by the user."
      trigger: "A configured GitHub folder is in the active workspace or the user requests a refresh."
      data: "GitHub login or repository filters and an optional user-provided GitHub API token."
      destination: OTHER
      destination_other: "api.github.com"
    }
    policy {
      cookies_allowed: NO
      setting: "The user can remove the Live Folder to stop requests."
      policy_exception_justification: "This feature is enabled only by an explicit user configuration."
    })");
  github_loader_ = network::SimpleURLLoader::Create(std::move(request), annotation);
  github_loader_->SetTimeoutDuration(base::Seconds(20));
  github_loader_->DownloadToString(
      profile->GetDefaultStoragePartition()->GetURLLoaderFactoryForBrowserProcess().get(),
      base::BindOnce([](base::WeakPtr<WorkspaceWindow> window, std::string token,
                       std::optional<std::string> body) {
        if (!window) return;
        window->github_loader_.reset();
        auto result = body ? base::JSONReader::ReadDict(*body, base::JSON_PARSE_RFC) : std::nullopt;
        const auto* items = result ? result->FindList("items") : nullptr;
        if (!items || result->FindBool("incomplete_results").value_or(true)) {
          window->FinishGitHubRefresh(false);
          return;
        }
        auto& job = *window->github_refresh_;
        for (const auto& item : *items) {
          const auto* dict = item.GetIfDict();
          const auto* link = dict ? dict->FindString("html_url") : nullptr;
          const auto* title = dict ? dict->FindString("title") : nullptr;
          if (!link || !title) continue;
          const GURL url(*link);
          const auto key = GitHubPullKey(url);
          if (key.empty() || std::ranges::any_of(job.results, [&](const auto& pin) {
                return GitHubPullKey(pin.url) == key;
              })) continue;
          job.results.push_back({"github-pr-" + job.key.second + key, job.key.second, *title, url});
        }
        if (job.queries.empty()) window->FinishGitHubRefresh(true);
        else window->FetchGitHubResults(std::move(token));
      }, weak_factory_.GetWeakPtr(), std::move(token)), 4 * 1024 * 1024);
}

void WorkspaceWindow::FinishGitHubRefresh(bool ok) {
  auto job = std::move(*github_refresh_);
  github_refresh_.reset();
  const auto& [workspace, folder] = job.key;
  const auto* space = model_.Find(workspace);
  const auto storage = pins_.find(workspace);
  if (space && space->profile_id == job.profile_id && storage != pins_.end() && !profiles_busy()) {
    auto& items = storage->second;
    const auto target = std::ranges::find(items, folder, &PinnedItem::id);
    if (target != items.end() && target->github == job.config) {
      if (ok) {
        auto previous = items;
        std::map<std::string, std::string> existing_ids;
        const auto is_result = [&](const PinnedItem& pin) {
          return pin.parent_id == folder && !pin.folder && !GitHubPullKey(pin.url).empty();
        };
        for (const auto& pin : items) {
          if (is_result(pin))
            existing_ids.emplace(GitHubPullKey(pin.url), pin.id);
        }
        for (auto& pin : job.results) {
          const auto existing = existing_ids.find(GitHubPullKey(pin.url));
          if (existing != existing_ids.end()) pin.id = existing->second;
        }
        std::erase_if(items, is_result);
        const auto insertion = std::ranges::find(items, folder, &PinnedItem::id) + 1;
        items.insert(insertion, std::make_move_iterator(job.results.begin()),
                     std::make_move_iterator(job.results.end()));
        if (items != previous) SaveFolderEdit(workspace, std::move(previous));
      } else if (job.manual) {
        shell_->ShowToast(T(u"Nie udało się odświeżyć folderu GitHub"), {}, {});
      }
    }
  }
  ScheduleGitHubRefresh();
}

bool WorkspaceWindow::RenamePinnedFolder(std::string workspace, std::string id, std::string name) {
  name = std::string(base::TrimWhitespaceASCII(name, base::TRIM_ALL));
  if (!configuration_ready_ || configuration_invalid_ || profiles_busy_ || !model_.Find(workspace) ||
      name.empty() || name.size() > 256 || !base::IsStringUTF8(name)) return false;
  auto& items = pins_[workspace];
  auto folder = std::find_if(items.begin(), items.end(), [&](const auto& item) { return item.id == id && item.folder; });
  if (folder == items.end()) return false;
  auto previous = items;
  folder->title = std::move(name);
  SaveFolderEdit(std::move(workspace), std::move(previous));
  return true;
}
bool WorkspaceWindow::RemovePinnedFolder(std::string workspace, std::string id, bool contents) {
  if (!configuration_ready_ || configuration_invalid_ || profiles_busy_ || !model_.Find(workspace)) return false;
  auto& items = pins_[workspace];
  auto folder = std::find_if(items.begin(), items.end(), [&](const auto& item) { return item.id == id && item.folder; });
  if (folder == items.end()) return false;
  auto previous = items;
  std::set<std::string> removed{id};
  if (contents || folder->github) {
    const auto nodes = PinNodes(items);
    const auto subtree = PinTree(nodes).Subtree(id);
    if (subtree.empty()) return false;  // Ambiguous IDs must not cause a partial deletion.
    for (const auto index : subtree) removed.insert(items[index].id);
  } else {
    const auto parent = folder->parent_id;
    for (auto& item : items) if (item.parent_id == id) item.parent_id = parent;
  }
  std::erase_if(items, [&](const auto& item) { return removed.contains(item.id); });
  SaveFolderEdit(std::move(workspace), std::move(previous), std::move(removed));
  return true;
}
bool WorkspaceWindow::RestorePins(std::string workspace, std::vector<PinnedItem> items) {
  if (!configuration_ready_ || configuration_invalid_ || profiles_busy_ || !model_.Find(workspace)) return false;
  std::erase_if(items, [](const auto& item) { return item.profile_pin; });
  const auto nodes = PinNodes(items);
  if (!PinTree(nodes).IsValid()) return false;
  auto previous = std::exchange(pins_[workspace], std::move(items));
  SaveFolderEdit(std::move(workspace), std::move(previous), {});
  return true;
}
void WorkspaceWindow::SaveFolderEdit(std::string workspace, std::vector<PinnedItem> previous, std::set<std::string> removed) {
  profiles_busy_ = true;
  UpdateShell();
  SaveConfiguration(base::BindOnce([](base::WeakPtr<WorkspaceWindow> window, std::string workspace,
      std::vector<PinnedItem> previous, std::set<std::string> removed, bool ok) {
    if (!window) return;
    if (!ok) window->pins_[workspace] = std::move(previous);
    for (auto& host : window->hosts_) if (host.workspace_id == workspace || "@profile:" + host.profile_id == workspace) {
      // A rolled-back pin must not leave its page hidden behind a missing row.
      const auto& items = window->pinned_items(host.workspace_id);
      std::erase_if(host.pin_tabs, [&](const auto& item) {
        return removed.contains(item.first) || std::none_of(items.begin(), items.end(), [&](const auto& pin) { return pin.id == item.first; });
      });
    }
    // Previously opened pins stay alive as ordinary tabs after unpinning.
    window->profiles_busy_ = false;
    window->UpdateShell();
    if (!ok) window->shell_->ShowFolderSaveError();
  }, weak_factory_.GetWeakPtr(), workspace, std::move(previous), std::move(removed)));
}

void WorkspaceWindow::PreviewArcImport(base::FilePath directory) {
  if (is_private()) return;
  PreviewBrowserImport(BrowserImportOptions(), std::move(directory));
}
void WorkspaceWindow::InvalidateImportPreview() {
  if (profiles_busy_) return;
  ++import_preview_generation_;
  pending_import_.reset();
  import_loading_ = false;
}
void WorkspaceWindow::PreviewBrowserImport(BrowserImportOptions options, base::FilePath directory) {
  if (is_private()) return;
  if (!configuration_ready_ || configuration_invalid_ || profiles_busy_) return;
  if (directory.empty()) directory = DefaultBrowserDirectory(options.browser);
  shell_->BeginImportPreview(options);
  import_loading_ = true;
  pending_import_.reset();
  const auto generation = ++import_preview_generation_;
  WorkspaceShell::ImportStatusReport reading;reading.stage=WorkspaceShell::ImportStatusReport::Stage::kReading;reading.browser=ImportBrowserName(options.browser);
  shell_->SetImportStatus(reading, false, true);
  config_runner_->PostTaskAndReplyWithResult(FROM_HERE, base::BindOnce(ReadBrowserImport, options, directory),
      base::BindOnce([](base::WeakPtr<WorkspaceWindow> window, size_t generation, ArcImport result) {
        if (!window) return;
        if (generation != window->import_preview_generation_) return;
        window->import_loading_ = false;
        WorkspaceShell::ImportStatusReport report;report.error=result.error;report.profiles=result.profiles.size();report.spaces=result.spaces.size();
        report.folders=result.folder_count;report.tabs=result.tab_count;report.skipped=result.skipped_count;
        window->pending_import_ = std::move(result);
        window->shell_->SetImportStatus(report, window->pending_import_->error.empty());
        if (window->welcome_import_done_) {
          if (!window->pending_import_->error.empty()) {
            std::move(window->welcome_import_done_).Run(false, base::UTF8ToUTF16(window->pending_import_->error));
          } else if (!window->ApplyArcImport()) {
            std::move(window->welcome_import_done_).Run(false, T(u"Nie można teraz rozpocząć importu. Spróbuj ponownie."));
          }
        }
      }, weak_factory_.GetWeakPtr(), generation));
}

bool WorkspaceWindow::ApplyArcImport() {
  if (!configuration_ready_ || configuration_invalid_ || profiles_busy() || import_loading_ || !import_ready()) return false;
  profiles_busy_ = true;
  import_new_profile_ids_.clear();
  import_new_workspace_ids_.clear();
  import_upgraded_github_folders_.clear();
  UpdateShell();
  WorkspaceShell::ImportStatusReport importing;importing.stage=WorkspaceShell::ImportStatusReport::Stage::kImporting;
  shell_->SetImportStatus(importing, false, /*busy=*/true, /*applying=*/true);
  if (!pending_import_->options.history && !pending_import_->options.cookies) {
    ImportNextProfile(0);
  } else {
    config_runner_->PostTaskAndReplyWithResult(FROM_HERE,
        base::BindOnce([](ArcImport source) { return ReadBrowsingImportData(source); }, *pending_import_),
        base::BindOnce([](base::WeakPtr<WorkspaceWindow> window, std::shared_ptr<BrowsingImportData> data) {
          if (!window) return;
          if (!data->error.empty()) {
            window->profiles_busy_ = false;
            window->UpdateShell();
            WorkspaceShell::ImportStatusReport failed;failed.error=data->error;
            window->shell_->SetImportStatus(failed, true);
            if (window->welcome_import_done_)
              std::move(window->welcome_import_done_).Run(false, base::UTF8ToUTF16(data->error));
            return;
          }
          window->import_browsing_data_ = std::move(data);
          window->ImportNextProfile(0);
        }, weak_factory_.GetWeakPtr()));
  }
  return true;
}

void WorkspaceWindow::ImportNextProfile(size_t index) {
  if (index == pending_import_->profiles.size()) { CommitArcImport(); return; }
  const auto& info = pending_import_->profiles[index];
  if (ProfileFor(info.id)) { ImportNextProfile(index + 1); return; }
  g_browser_process->profile_manager()->CreateProfileAsync(data_directory_.AppendASCII(info.directory),
      base::BindOnce([](base::WeakPtr<WorkspaceWindow> window, size_t index, Profile* profile) {
        if (!window) return;
        auto lease = profile ? ScopedProfileKeepAlive::TryAcquire(profile, ProfileKeepAliveOrigin::kBrowserWindow) : nullptr;
        if (!lease) { window->CompleteArcImport(false); return; }
        const auto info = window->pending_import_->profiles[index];
        if (auto* attributes = g_browser_process->profile_manager()->GetProfileAttributesStorage().GetProfileAttributesWithPath(profile->GetPath()))
          attributes->SetLocalProfileName(base::UTF8ToUTF16(info.name), false);
        window->profiles_.push_back({info, std::move(lease)});
        window->import_new_profile_ids_.push_back(info.id);
        window->ImportNextProfile(index + 1);
      }, weak_factory_.GetWeakPtr(), index));
}

void WorkspaceWindow::CommitArcImport() {
  // Only the untouched first-run placeholder can be replaced. Existing work or
  // workspaces are never discarded by an import opened later in Settings.
  import_replaces_starter_ = false;
  if (!onboarding_complete_ && model_.workspaces().size() == 1 && !starter_workspace_id_.empty()) {
    const auto* starter = model_.Find(starter_workspace_id_);
    auto host = std::find_if(hosts_.begin(), hosts_.end(), [&](const auto& h) { return h.workspace_id == starter_workspace_id_; });
    import_replaces_starter_ = starter && !starter_has_imported_passwords_ &&
        (starter->name == "Moja przestrzeń" || starter->name == base::UTF16ToUTF8(T(u"Moja przestrzeń"))) &&
        starter->profile_id == "personal-profile" && pinned_items(starter->id).empty() &&
        host != hosts_.end() && host->tabs.size() == 1 && host->split.empty() &&
        !IsWorkspaceTabUrl(TabUrl(*host, host->view));
  }
  closing_ = true;
  // Seed favorites only when creating a profile, just as with imported Spaces.
  for (const auto& id : import_new_profile_ids_) {
    const auto key = "@profile:" + id;
    const auto favorites = pending_import_->profile_favorites.find(id);
    if (favorites != pending_import_->profile_favorites.end())
      pins_[key] = favorites->second;
    else
      pins_.erase(key);
  }
  for (const auto& space : pending_import_->spaces) {
    if (model_.Find(space.id)) {
      // Earlier versions imported these as ordinary folders. Upgrade only
      // matching folders, preserving their current location, title and links.
      auto& existing = pins_[space.id];
      for (const auto& pin : space.pins) {
        if (!pin.github) continue;
        auto folder = std::ranges::find(existing, pin.id, &PinnedItem::id);
        if (folder == existing.end() || !folder->folder || folder->github) continue;
        folder->github = pin.github;
        import_upgraded_github_folders_.emplace_back(space.id, folder->id);
      }
      continue;
    }
    CHECK(model_.Add({space.id, space.profile_id, space.name}));
    import_new_workspace_ids_.push_back(space.id);
    pins_[space.id] = space.pins;
    hosts_.emplace_back();
    auto& host = hosts_.back();
    host.workspace_id = space.id;
    host.profile_id = space.profile_id;
    host.profile = ProfileFor(space.profile_id);
    host.view = CreatePage(host.profile);
    host.tabs.push_back(host.view);
  }
  if (import_new_workspace_ids_.empty()) import_replaces_starter_ = false;
  closing_ = false;
  SaveConfiguration(base::BindOnce(&WorkspaceWindow::CompleteArcImport, weak_factory_.GetWeakPtr()));
}

void WorkspaceWindow::CompleteArcImport(bool ok) {
  const auto added_profiles = import_new_profile_ids_.size();
  const auto added_spaces = import_new_workspace_ids_.size();
  if (!ok) {
    closing_ = true;
    for (const auto& [workspace, folder] : import_upgraded_github_folders_) {
      auto& items = pins_[workspace];
      auto item = std::ranges::find(items, folder, &PinnedItem::id);
      if (item != items.end()) item->github.reset();
    }
    for (const auto& id : import_new_workspace_ids_) {
      for (auto it = hosts_.begin(); it != hosts_.end(); ++it) {
        if (it->workspace_id != id) continue;
        auto tabs = std::move(it->tabs);
        it->view = nullptr;
        hosts_.erase(it);
        for (auto& tab : tabs) shell_->content_host()->RemoveChildViewT(tab.get());
        break;
      }
      model_.Remove(id);
      pins_.erase(id);
    }
    for (const auto& id : import_new_profile_ids_) {
      pins_.erase("@profile:" + id);
      auto it = std::find_if(profiles_.begin(), profiles_.end(), [&](const auto& entry) { return entry.info.id == id; });
      if (it != profiles_.end()) { ScheduleProfileDeletion(it->info.directory); profiles_.erase(it); }
    }
    closing_ = false;
  } else {
    if (import_replaces_starter_) {
      const auto* starter = model_.Find(starter_workspace_id_);
      const std::string profile_id = starter->profile_id;
      CHECK(controller_.Activate(import_new_workspace_ids_.front()));
      for (auto it = hosts_.begin(); it != hosts_.end(); ++it) {
        if (it->workspace_id != starter_workspace_id_) continue;
        auto tabs = std::move(it->tabs);
        it->view = nullptr; it->split.clear();
        hosts_.erase(it);
        for (auto& tab : tabs) shell_->content_host()->RemoveChildViewT(tab.get());
        break;
      }
      CHECK(model_.Remove(starter_workspace_id_));
      pins_.erase(starter_workspace_id_);
      std::erase_if(profiles_, [&](const auto& p) { return p.info.id == profile_id; });
    }
    starter_workspace_id_.clear();
    // Apply appearance only after a successful catalogue write, and only to new
    // spaces so a repeated import preserves the user's local customizations.
    for (const auto& space : pending_import_->spaces) {
      if (!space.emoji.empty() && std::ranges::find(import_new_workspace_ids_, space.id) != import_new_workspace_ids_.end())
        shell_->SetWorkspaceEmoji(space.id, space.emoji);
    }
    onboarding_complete_ = true;
  }
  import_replaces_starter_ = false;
  import_upgraded_github_folders_.clear();
  if (ok) SaveConfiguration();
  WorkspaceShell::ImportStatusReport status;status.stage=WorkspaceShell::ImportStatusReport::Stage::kFinished;status.ok=ok;status.added_profiles=added_profiles;status.added_spaces=added_spaces;
  if (ok && import_browsing_data_) {
    std::vector<Profile*> destinations;
    for (const auto& data : import_browsing_data_->profiles) destinations.push_back(ProfileFor(data.profile_id));
    WorkspaceShell::ImportStatusReport saving;saving.stage=WorkspaceShell::ImportStatusReport::Stage::kSavingBrowsingData;
    shell_->SetImportStatus(saving, false, true, true);
    WriteBrowsingImportData(std::move(import_browsing_data_), std::move(destinations),
        base::BindOnce([](base::WeakPtr<WorkspaceWindow> window, WorkspaceShell::ImportStatusReport status, BrowsingImportResult result) {
          if (!window) return;
          status.browsing_data = true;status.history = result.history;status.cookies = result.cookies;
          status.data_skipped = result.skipped;status.data_failed = result.failed;
          window->FinishArcImport(status);
        }, weak_factory_.GetWeakPtr(), status));
    return;
  }
  FinishArcImport(status);
}

void WorkspaceWindow::FinishArcImport(const WorkspaceShell::ImportStatusReport& status) {
  import_browsing_data_.reset();
  if (status.ok) QueueImportedFavicons(*pending_import_);
  pending_import_.reset();
  import_new_profile_ids_.clear();
  import_new_workspace_ids_.clear();
  profiles_busy_ = false;
  UpdateShell();
  shell_->WarmWorkspaceSidebars(pins_);
  shell_->SetImportStatus(status, false);
  if (welcome_import_done_) {
    std::u16string message = status.ok
        ? TF(u"Import zakończony. Dodano {} profili i {} przestrzeni. Możesz wybrać je na liście przestrzeni.",
             {base::NumberToString16(status.added_profiles), base::NumberToString16(status.added_spaces)})
        : T(u"Import się nie powiódł. Dotychczasowe przestrzenie pozostały bez zmian. Spróbuj ponownie.");
    if (status.data_failed) message += u" " + T(u"Część danych nie została zapisana. Ponów import.");
    std::move(welcome_import_done_).Run(status.ok, std::move(message));
  }
}

void WorkspaceWindow::DismissImport() {
  if (profiles_busy_) return;
  InvalidateImportPreview();
  starter_workspace_id_.clear();
  const bool finishing_onboarding = !onboarding_complete_;
  onboarding_complete_ = true;
  SaveConfiguration();
  if (finishing_onboarding) UpdateShell();
}

void WorkspaceWindow::ScheduleSessionSave() {
  if (is_private()) return;
  if (!configuration_ready_ || configuration_invalid_ || restoring_session_ || closing_ || profiles_busy_) return;
  session_timer_.Start(FROM_HERE, base::Milliseconds(350),
      base::BindOnce(&WorkspaceWindow::SaveSession, weak_factory_.GetWeakPtr()));
}
void WorkspaceWindow::SaveSession() {
  if (is_private()) return;
  session_timer_.Stop();
  if (!configuration_ready_ || configuration_invalid_ || profiles_busy_) return;
  base::DictValue session;
  for (const auto& host : hosts_) {
    auto entry = CaptureHost(host);
    session.Set(host.workspace_id, std::move(entry));
  }
  RecordTimeline(false);
  ++pending_config_writes_;
  config_runner_->PostTaskAndReplyWithResult(FROM_HERE, base::BindOnce([](base::FilePath path, base::DictValue session) {
    std::string json;
    return base::JSONWriter::Write(session, &json) &&
        base::ImportantFileWriter::WriteFileAtomically(path, json);
  }, data_directory_.AppendASCII("ZitterSession.json"), std::move(session)), base::BindOnce([](base::WeakPtr<WorkspaceWindow> window, bool ok) {
    if (!window) return;
    --window->pending_config_writes_;
    if (!ok) window->shell_->SetConfigurationError(true);
  }, weak_factory_.GetWeakPtr()));
}
void WorkspaceWindow::RestoreSession() {
  auto session = base::JSONReader::ReadDict(session_json_, base::JSON_PARSE_RFC);
  session_json_.clear();
  if (!session) return;
  restoring_session_ = true;
  for (auto& host : hosts_) {
    const auto* entry = session->FindDict(host.workspace_id);
    if (!entry || !entry->FindString("profile_id") || *entry->FindString("profile_id") != host.profile_id) continue;
    if (!RestoreHost(host, *entry)) continue;
    if (entry->FindBool("sleeping").value_or(false) && host.workspace_id != restored_active_) {
      auto stored = entry->Clone(); ReleaseHost(host); host.sleeping = std::move(stored);
    }
  }
  restoring_session_ = false;
}

void WorkspaceWindow::SetReadyCallback(base::OnceClosure callback) {
  if (configuration_ready_) std::move(callback).Run();
  else ready_callback_ = std::move(callback);
}

void WorkspaceWindow::SetCloseCallback(base::OnceClosure callback) {
  close_callback_ = std::move(callback);
  ArmCloseHandler();
}

bool WorkspaceWindow::StartFind() {
  auto* page = peek_contents() ? peek_contents() : contents(active_id());
  auto* helper = page ? find_in_page::FindTabHelper::FromWebContents(page) : nullptr;
  if (!helper) return false;
  if (find_page_ != page) {
    StopFind();
    find_page_ = page;
    find_observation_.Observe(helper);
  }
  helper->set_find_ui_active(true);
  shell_->ShowFind(base::BindRepeating(&WorkspaceWindow::FindBounds, base::Unretained(this)),
                   base::BindRepeating(&WorkspaceWindow::Find, weak_factory_.GetWeakPtr()),
                   base::BindRepeating(&WorkspaceWindow::StopFind, weak_factory_.GetWeakPtr()));
  return true;
}
void WorkspaceWindow::Find(std::u16string text, bool forward, bool match_case) {
  auto* helper = find_page_ ? find_in_page::FindTabHelper::FromWebContents(find_page_) : nullptr;
  if (!helper) return;
  if (text.empty()) { helper->StopFinding(find_in_page::SelectionAction::kClear); return; }
  helper->StartFinding(std::move(text), forward, match_case, /*find_match=*/true);
}
void WorkspaceWindow::StopFind() {
  if (!find_page_) return;
  if (auto* helper = find_in_page::FindTabHelper::FromWebContents(find_page_)) {
    // Like Chrome's find bar: the last match stays selected.
    helper->StopFinding(find_in_page::SelectionAction::kKeep);
    helper->set_find_ui_active(false);
  }
  find_observation_.Reset();
  find_page_ = nullptr;
}
gfx::Rect WorkspaceWindow::FindBounds() const {
  return find_page_ ? find_page_->GetContainerBounds() : widget_->GetClientAreaBoundsInScreen();
}
void WorkspaceWindow::OnFindResultAvailable(content::WebContents* page) {
  if (page != find_page_) return;
  const auto& result = find_in_page::FindTabHelper::FromWebContents(page)->find_result();
  shell_->SetFindResult(result.active_match_ordinal(), result.number_of_matches());
}
void WorkspaceWindow::OnFindTabHelperDestroyed(find_in_page::FindTabHelper*) {
  find_observation_.Reset();
  find_page_ = nullptr;
  if (shell_) shell_->ResetFind();
}

bool WorkspaceWindow::StartPrint(content::WebContents* page) {
  if (!page) page = peek_contents() ? peek_contents() : contents(active_id());
  if (!page || !printing::PrintViewManager::FromWebContents(page)) return false;
  // A sheet already open reports its close first, for its own printer.
  shell_->HidePrint();
  printer_ = std::make_unique<PagePrinter>(page);
  const auto weak = weak_factory_.GetWeakPtr();
  WorkspaceShell::PrintHandlers handlers;
  handlers.preview = base::BindRepeating([](base::WeakPtr<WorkspaceWindow> window, PrintOptions options) {
    if (!window || !window->printer_) return;
    window->printer_->Render(options, base::BindOnce([](base::WeakPtr<WorkspaceWindow> window, PagePrinter::Preview preview) {
      if (window) window->shell_->SetPrintPreview(std::move(preview.pages), preview.failed);
    }, window));
  }, weak);
  handlers.print = base::BindRepeating([](base::WeakPtr<WorkspaceWindow> window, PrintOptions options) {
    if (!window || !window->printer_) return;
    window->printer_->Print(options, base::BindOnce([](base::WeakPtr<WorkspaceWindow> window, std::u16string error) {
      if (window) window->shell_->PrintFinished(std::move(error));
    }, window));
  }, weak);
  handlers.system_dialog = base::BindRepeating([](base::WeakPtr<WorkspaceWindow> window) {
    if (window) window->RunShortcut("print_system");
  }, weak);
  // The sheet may close from inside the printer's own callbacks.
  handlers.closed = base::BindRepeating([](base::WeakPtr<WorkspaceWindow> window) {
    if (window && window->printer_)
      base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE, std::move(window->printer_));
  }, weak);
  shell_->ShowPrint(std::move(handlers));
  PagePrinter::ListPrinters(base::BindOnce([](base::WeakPtr<WorkspaceWindow> window, std::vector<PrinterChoice> printers) {
    if (window) window->shell_->SetPrinters(std::move(printers));
  }, weak));
  return true;
}
}  // namespace zitter::prototype
