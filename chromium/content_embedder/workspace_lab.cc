// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "workspace_lab.h"
#include "http_auth_prompt.h"

#include "adblock_cache_probe.h"
#include "workspace_favicon_refresh.h"

#include "base/auto_reset.h"
#include "base/barrier_closure.h"
#include "base/functional/function_ref.h"
#include "base/memory/raw_ref.h"

#include "extension_manifest.h"
#include "extension_registry_probe.h"
#include "extensions_manager.h"
#include "extensions/browser/extension_api_frame_id_map.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/extension_user_script_loader.h"
#include "extensions/browser/user_script_manager.h"
#include "lab_web_view.h"
#include "page_password_manager.h"
#include "profile_context.h"
#include "profile_permission_controller.h"
#include "profile_download_delegate.h"
#include "workspace_devtools.h"
#include "workspace_media_access.h"
#include "night_shift_site.h"
#include "workspace_session_store.h"
#include "workspace_timeline.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <type_traits>
#include <utility>

#include "base/base64.h"
#include "base/check.h"
#include "base/containers/adapters.h"
#include "base/command_line.h"
#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/i18n/time_formatting.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/escape.h"
#include "base/logging.h"
#include "base/notreached.h"
#include "base/process/process.h"
#include "base/process/process_handle.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/bind_post_task.h"
#include "base/task/thread_pool.h"
#include "base/uuid.h"
#include "build/build_config.h"
#include "chromium/prototype/link_navigation.h"
#include "chromium/prototype/ad_blocking_rules_panel.h"
#include "chromium/prototype/adblock.rs.h"
#include "chromium/prototype/adblock_browser_support.h"
#include "chromium/prototype/adblock_filter_lists.h"
#include "chromium/prototype/arc_importer.h"
#include "chromium/prototype/assistant_panel.h"
#include "chromium/prototype/browser_import_data.h"
#include "chromium/prototype/browser_tools.h"
#include "chromium/prototype/clean_url.h"
#include "chromium/prototype/rss_reader.h"
#include "chromium/prototype/editor_control.h"
#include "chromium/prototype/download_panel.h"
#include "chromium/prototype/fingerprint_session.h"
#include "chromium/prototype/fingerprint_toolbar.h"
#include "chromium/prototype/history_panel.h"
#include "chromium/prototype/i18n.h"
#include "chromium/prototype/sidebar_scene.rs.h"
#include "chromium/prototype/sidebar_layout.h"
#include "chromium/prototype/page_dialogs.h"
#include "chromium/prototype/passwords_panel.h"
#include "chromium/prototype/time_machine_view.h"
#include "chromium/prototype/toolbar_menu.h"
#include "chromium/prototype/workspace_session.h"
#include "chromium/prototype/workspace_shell.h"
#include "chromium/prototype/workspace_urls.h"
#include "components/device_reauth/device_authenticator.h"
#include "components/find_in_page/find_tab_helper.h"
#include "components/find_in_page/find_types.h"
#include "components/history/core/browser/history_service.h"
#include "components/input/native_web_keyboard_event.h"
#include "components/os_crypt/async/browser/os_crypt_async.h"
#include "components/prefs/pref_service.h"
#include "components/crx_file/id_util.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/context_menu_params.h"
#include "content/public/browser/context_factory.h"
#include "content/public/browser/download_manager.h"
#include "components/password_manager/core/browser/password_store/password_store_consumer.h"
#include "components/password_manager/core/browser/password_store/password_store_interface.h"
#include "components/password_manager/core/browser/password_store/stored_credential.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/favicon/core/favicon_service_impl.h"
#include "components/favicon_base/favicon_types.h"
#include "content/public/browser/host_zoom_map.h"
#include "content/public/browser/keyboard_event_processing_result.h"
#include "content/public/browser/favicon_status.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/fullscreen_types.h"
#include "content/public/common/url_constants.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents_user_data.h"
#include "net/base/url_util.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/permission_request_description.h"
#include "content/public/browser/permission_result.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/weak_document_ptr.h"
#include "content/public/browser/web_authentication_delegate.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_client.h"
#include "content/public/common/isolated_world_ids.h"
#include "extensions/browser/view_type_utils.h"
#include "extensions/browser/extension_action.h"
#include "extensions/browser/extension_action_icon_factory.h"
#include "extensions/browser/api/storage/storage_area_namespace.h"
#include "extensions/browser/api/storage/storage_frontend.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_registry.h"
#include "chromium/prototype/assistant_visuals.h"
#include "ui/gfx/canvas.h"
#include "extensions/browser/extension_action_manager.h"
#include "extensions/browser/image_loader.h"
#include "extensions/common/api/commands/commands_handler.h"
#include "extensions/common/constants.h"
#include "extensions/common/extension.h"
#include "extensions/common/manifest_constants.h"
#include "extensions/common/manifest_handlers/description_info.h"
#include "extensions/common/permissions/permission_set.h"
#include "extensions/common/permissions/permissions_data.h"
#include "net/base/net_errors.h"
#include "net/cert/cert_status_flags.h"
#include "net/base/load_flags.h"
#include "net/ssl/ssl_info.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "third_party/blink/public/common/page/page_zoom.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "third_party/blink/public/mojom/frame/user_activation_notification_type.mojom.h"
#include "third_party/blink/public/mojom/permissions/permission.mojom.h"
#include "third_party/re2/src/re2/re2.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/accelerators/command.h"
#include "ui/base/ime/text_input_client.h"
#include "ui/base/models/simple_combobox_model.h"
#include "ui/color/color_provider_key.h"
#include "ui/display/screen.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/gfx/image/image_skia_rep.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/base/page_transition_types.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/events/keycodes/dom/dom_key.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/content_accelerators/accelerator_util.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/native_theme/native_theme.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_controller.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/combobox/combobox.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/controls/native/native_view_host.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/layout_provider.h"
#include "ui/views/view.h"
#include "ui/views/views_delegate.h"
#include "ui/views/view_class_properties.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"
#include "ui/views/widget/widget_delegate.h"
#include "url/gurl.h"
#include "url/origin.h"

#if BUILDFLAG(ENABLE_PRINTING)
#include "ui/shell_dialogs/select_file_dialog_factory.h"
#include "ui/shell_dialogs/selected_file_info.h"
#include "ui/shell_dialogs/select_file_policy.h"
#include "workspace_printing.h"
#if BUILDFLAG(ENABLE_PDF)
#include "workspace_pdf_preview.h"
#endif
#endif

#if BUILDFLAG(IS_MAC)
#include <dlfcn.h>

#include "chromium/prototype/workspace_frame_mac.h"
#include "password_authenticator_mac.h"
#elif BUILDFLAG(IS_WIN)
#include "chromium/prototype/workspace_frame_win.h"
#include "password_authenticator_win.h"
#endif
#include "url/url_constants.h"

#include "zitter/core/pin_tree.h"

namespace content {
CONTENT_EXPORT bool ZitterHistoryEntryHasPost(const NavigationEntry& entry);
CONTENT_EXPORT bool ZitterGoThroughHistory(NavigationController& controller,
                                          bool forward);
}  // namespace content

namespace zitter::content_embedder {
class WorkspacePasswordClear final : public password_manager::PasswordStoreConsumer {
 public:
  WorkspacePasswordClear(password_manager::PasswordStoreInterface& store,
                          base::Time begin, base::Time end,
                          base::OnceCallback<void(bool)> done)
      : store_(store), begin_(begin), end_(end), done_(std::move(done)) {}

  void Start() {
    store_->RemoveLoginsCreatedBetween(
        FROM_HERE, begin_, end_,
        base::BindOnce(&WorkspacePasswordClear::CheckRemaining,
                       weak_factory_.GetWeakPtr()));
  }

 private:
  void CheckRemaining(bool) {
    // The deletion callback reports whether any logins changed. A read also
    // distinguishes an empty range from a failed removal that left rows behind.
    store_->GetAllLogins(weak_factory_.GetWeakPtr());
  }

  void OnGetPasswordStoreResultsOrErrorFrom(
      password_manager::PasswordStoreInterface* store,
      base::expected<std::vector<password_manager::StoredCredential>,
                     password_manager::PasswordStoreBackendError> results) override {
    const bool cleared = store == &*store_ && results &&
        std::ranges::none_of(*results, [this](const auto& login) {
          return login.date_created >= begin_ && login.date_created <= end_;
        });
    auto done = std::move(done_);
    std::move(done).Run(cleared);
  }

  const raw_ref<password_manager::PasswordStoreInterface> store_;
  const base::Time begin_;
  const base::Time end_;
  base::OnceCallback<void(bool)> done_;
  base::WeakPtrFactory<WorkspacePasswordClear> weak_factory_{this};
};

namespace {

constexpr char kExtensionsPuzzle[] =
    "M3 5.5h3V4a1.5 1.5 0 0 1 3 0v1.5h3v3h-1.5a1.5 1.5 0 0 0 0 3H12v2H3v-2h1.5a1.5 1.5 0 0 0 0-3H3z";
constexpr char kNightShiftMoon[] =
    "M12.8 9.8A5.3 5.3 0 0 1 6.2 3.2a5.2 5.2 0 1 0 6.6 6.6z";
constexpr char kNightShiftSpark[] = "M12 2v4m-2-2h4";
using ToolbarPalette = base::RepeatingCallback<sidebar::DesignPalette()>;

class ContentToolbarButton final : public views::Button,
                                   public extensions::ExtensionActionIconFactory::Observer {
  METADATA_HEADER(ContentToolbarButton, views::Button)
 public:
  ContentToolbarButton(const char* path, std::u16string title,
                       PressedCallback callback, ToolbarPalette palette,
                       const char* active_glyph = nullptr)
      : views::Button(std::move(callback)),
        path_(path),
        active_glyph_(active_glyph),
        palette_(std::move(palette)) {
    SetPreferredSize(gfx::Size(30, 30));
    SetTooltipText(title);
    GetViewAccessibility().SetName(title);
  }
  void SetExtensionAction(const extensions::Extension& extension,
                          extensions::ExtensionAction& action, int tab_id) {
    tab_id_ = tab_id;
    badge_ = base::UTF8ToUTF16(action.GetDisplayBadgeText(tab_id)).substr(0, 2);
    icon_factory_ = std::make_unique<extensions::ExtensionActionIconFactory>(
        &extension, &action, this);
    OnIconUpdated();
  }
  void OnIconUpdated() override {
    if (icon_factory_)
      icon_ = icon_factory_->GetIcon(tab_id_);
    SchedulePaint();
  }
  void SetToggledAppearance(bool toggled) {
    if (toggled_ == toggled)
      return;
    toggled_ = toggled;
    SchedulePaint();
  }
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const auto palette = palette_.Run();
    const bool hover = GetState() == STATE_HOVERED || GetState() == STATE_PRESSED || HasFocus();
    const gfx::RectF box(GetLocalBounds());
    const bool selected_background = toggled_ && !active_glyph_;
    if (hover || selected_background)
      prototype::FillRound(canvas, box, 8,
                           selected_background ? palette.accent_soft : palette.bg_hover);
    const int x = width() / 2 - 8;
    const int y = height() / 2 - 8;
    if (icon_.IsEmpty()) {
      const gfx::RectF glyph(x, y, 16, 16);
      const SkColor ink = toggled_ ? palette.accent_ink : hover ? palette.ink : palette.ink_muted;
      prototype::StrokeIcon(canvas, path_, glyph, ink);
      if (toggled_ && active_glyph_)
        prototype::StrokeIcon(canvas, active_glyph_, glyph, ink);
    } else {
      const auto& image = icon_.AsImageSkia();
      canvas->DrawImageInt(image, 0, 0, image.width(), image.height(), x, y, 16, 16, true);
    }
    if (!badge_.empty()) {
      const gfx::Rect bounds(width() - 15, height() - 15, 14, 14);
      prototype::FillRound(canvas, gfx::RectF(bounds), 7, palette.ink);
      canvas->DrawStringRectWithFlags(badge_, prototype::DesignFont(2, 9, 700),
                                      palette.bg_base, bounds, gfx::Canvas::TEXT_ALIGN_CENTER);
    }
  }
 private:
  const char* path_;
  const char* active_glyph_;
  ToolbarPalette palette_;
  gfx::Image icon_;
  std::u16string badge_;
  bool toggled_ = false;
  int tab_id_ = 0;
  std::unique_ptr<extensions::ExtensionActionIconFactory> icon_factory_;
};
BEGIN_METADATA(ContentToolbarButton)
END_METADATA

class ContentToolbarDivider final : public views::View {
  METADATA_HEADER(ContentToolbarDivider, views::View)
 public:
  explicit ContentToolbarDivider(ToolbarPalette palette) : palette_(std::move(palette)) {
    SetPreferredSize(gfx::Size(9, 30));
  }
  void OnPaint(gfx::Canvas* canvas) override {
    canvas->FillRect(gfx::Rect(width() / 2, height() / 2 - 8, 1, 16), palette_.Run().line);
  }
 private:
  ToolbarPalette palette_;
};
BEGIN_METADATA(ContentToolbarDivider)
END_METADATA

constexpr char kAdblockTrialPage[] = "/work/adblock-trial";
constexpr char kAdblockCosmeticTrialPage[] = "/work/adblock-cosmetic-trial";
constexpr char kAdblockCosmeticTrialRule[] = "127.0.0.1##.zitter-trial-sponsor";
constexpr char kAdblockTrialTarget[] = "/adblock-trial-target";
constexpr int kAdblockShieldToggleId = 1;
constexpr int kAdblockShieldRequestsId = 2;
constexpr int kAdblockShieldEndTrialId = 3;

constexpr auto& kNames = kBuiltinProfileNames;
constexpr std::array<std::array<std::string_view, 2>, 2> kProbePaths = {{
    {"/work/deferred", "/work/active"},
    {"/personal/active", "/personal/deferred"},
}};
constexpr std::array<size_t, 2> kProbeSelection = {1, 0};
constexpr std::array<std::u16string_view, 2> kProbeTitles = {
    u"Zitter workspace probe work", u"Zitter workspace probe personal"};
constexpr char16_t kAddressProbeQuery[] = u"Zitter workspace probe";
constexpr char kGitHubFolderProbe[] = "zitter-content-github-folder-probe";
constexpr char kGitHubFixtureTokenFirst[] = "zitter_fixture_first";
constexpr char kGitHubFixtureTokenSecond[] = "zitter_fixture_second";
constexpr char kSitePermissionProbe[] = "zitter-content-workspace-site-permission-probe";
constexpr char kClipboardReadProbePhase[] = "prompt-read";
constexpr char kFingerprintProbe[] = "zitter-content-workspace-fingerprint-probe";
constexpr char kFingerprintCrossProcessProbe[] =
    "zitter-content-fingerprint-cross-process-frame";
constexpr char kPrivateWindowProbe[] = "zitter-content-private-window-probe";
constexpr char kPrivateAdblockProbe[] = "zitter-content-private-adblock-probe";
constexpr std::string_view kPrivateAdblockPage = "/private-adblock";
constexpr char kAdblockListsProbe[] = "zitter-content-filter-lists-smoke-test";
constexpr std::string_view kFilterListsPage = "/filter-lists-page";
constexpr char kAssistantCredentialProbe[] =
    "zitter-content-assistant-credential-probe";
constexpr char kAdblockProfileProbe[] = "zitter-content-profile-adblock-smoke-test";
constexpr char kAdblockCacheProbe[] = "zitter-content-adblock-cache-smoke-test";
constexpr std::array<std::string_view, 2> kAdblockProfilePages = {
    "/work/adblock-profile", "/personal/adblock-profile"};
constexpr std::string_view kAdblockEditorTarget = "/adblock-editor-target";
constexpr std::string_view kAdblockHistory404 = "/adblock-history-404";
#if BUILDFLAG(ENABLE_PRINTING)
constexpr char kPdfExportProbe[] = "zitter-content-pdf-export-probe";
constexpr std::string_view kPdfExportPage = "/pdf-export";
#endif
constexpr char kFindReloadProbe[] = "zitter-content-find-reload-probe";
class NightShiftSettingsObserver final
    : public content::WebContentsObserver,
      public content::WebContentsUserData<NightShiftSettingsObserver> {
 private:
  friend class content::WebContentsUserData<NightShiftSettingsObserver>;

  NightShiftSettingsObserver(content::WebContents* page,
                             GURL settings_url,
                             base::RepeatingCallback<void(base::DictValue)> apply,
                             base::RepeatingCallback<void(bool)> open_action,
                             base::RepeatingClosure site_changed,
                             base::RepeatingCallback<void(int)> resize)
      : content::WebContentsObserver(page),
        content::WebContentsUserData<NightShiftSettingsObserver>(*page),
        settings_url_(std::move(settings_url)),
        apply_(std::move(apply)), open_action_(std::move(open_action)),
        site_changed_(std::move(site_changed)), resize_(std::move(resize)) {}

  void DidFinishNavigation(content::NavigationHandle* navigation) override {
    constexpr std::string_view kApply = "apply=";
    constexpr std::string_view kPanelHeight = "panel-height=";
    if (!navigation->IsInPrimaryMainFrame() || !navigation->HasCommitted() ||
        !navigation->IsSameDocument() || navigation->IsErrorPage()) {
      return;
    }
    const GURL& url = navigation->GetURL();
    if (url.GetWithoutRef() != settings_url_ ||
        web_contents()->GetPrimaryMainFrame()->GetLastCommittedOrigin() !=
            url::Origin::Create(settings_url_)) {
      return;
    }
    if (open_action_ && (url.ref() == "settings" || url.ref() == "repair")) {
      open_action_.Run(url.ref() == "repair");
      return;
    }
    if (url.ref().starts_with("site-changed=")) {
      site_changed_.Run();
      return;
    }
    if (resize_ && url.ref().starts_with(kPanelHeight)) {
      int height;
      if (base::StringToInt(url.ref().substr(kPanelHeight.size()), &height) &&
          height > 0) {
        resize_.Run(height);
      }
      return;
    }
    if (!url.ref().starts_with(kApply) || url.ref().size() > 64 * 1024)
      return;
    auto settings = base::JSONReader::ReadDict(
        base::UnescapeBinaryURLComponent(url.ref().substr(kApply.size())),
        base::JSON_PARSE_RFC);
    if (settings)
      apply_.Run(std::move(*settings));
  }

  const GURL settings_url_;
  const base::RepeatingCallback<void(base::DictValue)> apply_;
  const base::RepeatingCallback<void(bool)> open_action_;
  const base::RepeatingClosure site_changed_;
  const base::RepeatingCallback<void(int)> resize_;
  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(NightShiftSettingsObserver);

constexpr char kCommandCenterProbe[] = "zitter-content-command-center-probe";
constexpr std::array<std::string_view, 18> kWorkspacePaletteShortcuts = {
    "new_tab", "settings", "shortcuts", "engines", "spaces", "profiles",
    "assistant", "assistant_settings", "sidebar", "downloads",
    "timeline", "reopen", "developer", "rss", "import", "history", "passwords",
    "reload_workspace_icons"};
constexpr std::array<std::string_view, 14> kPagePaletteShortcuts = {
    "back", "forward", "reload", "find", "close_tab", "split", "left_pane",
    "right_pane", "devtools_panel", "devtools_console", "devtools_inspect",
    "hard_reload", "copy_url", "close_other_tabs"};
constexpr char kRssProbe[] = "zitter-content-rss-probe";
constexpr char kBrowserImportProbe[] = "zitter-content-browser-import-probe";
constexpr char kExtraWorkspaceProbe[] = "zitter-content-extra-workspace-probe";
constexpr char kAdditionalProfileProbe[] = "zitter-content-additional-profile-probe";
constexpr char kRssFeedPath[] = "/workspace-feed.xml";
constexpr std::string_view kFindReloadPage = "/find-reload";
constexpr std::string_view kCopyAddressPage =
    "/find-reload?fbclid=tracker&value=kept#section";
constexpr char kNavigationProbe[] = "zitter-content-navigation-smoke-test";
constexpr std::array<std::string_view, 2> kNavigationProbePaths = {
    "/work/navigation-second", "/personal/navigation-second"};
constexpr std::u16string_view kNavigationProbeTitle = u"Workspace navigation fixture";
constexpr char kTimelineClearProbe[] = "zitter-content-timeline-clear-smoke-test";
constexpr char kTimelinePanelProbe[] = "zitter-content-timeline-panel-smoke-test";
constexpr char kTimelineProbe[] = "zitter-content-timeline-smoke-test";
constexpr std::string_view kTimelineLegacyWork = "/work/timeline-legacy";
constexpr std::string_view kTimelineLegacyPersonal = "/personal/timeline-legacy";
constexpr std::string_view kTimelineRecordedWork = "/work/timeline-recorded";
constexpr std::u16string_view kTimelineProbeTitle = u"Workspace timeline fixture";
constexpr char kClosedTabProbe[] = "zitter-content-closed-tab-smoke-test";
constexpr char kClosedTabProbePath[] = "/work/recently-closed";
constexpr char16_t kClosedTabProbeTitle[] = u"Recently closed Work fixture";
constexpr size_t kMaxClosedTabsPerWorkspace = 50;
constexpr char kShiftPeekProbe[] = "zitter-content-shift-peek-probe";
constexpr char kPeekProbe[] = "zitter-content-peek-probe";
constexpr char kPopupPolicyProbe[] = "zitter-content-popup-policy-probe";
constexpr char kPopupProbe[] = "zitter-content-popup-probe";
constexpr char kLinkRoutingProbe[] = "zitter-content-link-routing-probe";
constexpr char kSplitActionsProbe[] = "zitter-content-split-actions-probe";
constexpr char kWorkspaceSleepProbe[] = "zitter-content-workspace-sleep-probe";
constexpr char kWorkspacePinsProbe[] = "zitter-content-workspace-pins-probe";
constexpr char kWorkspaceDownloadProbe[] = "zitter-content-workspace-download-probe";
constexpr std::array<const char*, 2> kDownloadProbeFolders = {
    "work-downloads", "personal-downloads"};
constexpr std::array<const char*, 2> kDownloadProbePaths = {
    "/download/work", "/download/personal"};
constexpr std::u16string_view kFingerprintResult = u"Zitter fingerprint result ";
void HoverProbePage(content::WebContents& page, gfx::Point point) {
  const auto origin = page.GetContainerBounds().origin();
  blink::WebMouseEvent hover(blink::WebInputEvent::Type::kMouseMove, 0,
                             base::TimeTicks::Now());
  hover.SetPositionInWidget(point.x(), point.y());
  hover.SetPositionInScreen(origin.x() + point.x(), origin.y() + point.y());
  page.GetPrimaryMainFrame()->GetRenderWidgetHost()->ForwardMouseEvent(hover);
}

void ClickProbePage(content::WebContents& page, int modifiers = 0,
                    gfx::Point point = gfx::Point(30, 24)) {
  page.Focus();
  HoverProbePage(page, point);
  auto* widget = page.GetPrimaryMainFrame()->GetRenderWidgetHost();
  const auto origin = page.GetContainerBounds().origin();
  blink::WebMouseEvent click(blink::WebInputEvent::Type::kMouseDown,
                             modifiers | blink::WebInputEvent::kLeftButtonDown,
                             base::TimeTicks::Now());
  click.SetPositionInWidget(point.x(), point.y());
  click.SetPositionInScreen(origin.x() + point.x(), origin.y() + point.y());
  click.button = blink::WebMouseEvent::Button::kLeft;
  click.click_count = 1;
  widget->ForwardMouseEvent(click);
  click.SetType(blink::WebInputEvent::Type::kMouseUp);
  click.SetModifiers(modifiers);
  widget->ForwardMouseEvent(click);
}

std::vector<PinNode> PinNodes(const std::vector<prototype::PinnedItem>& pins) {
  std::vector<PinNode> nodes;
  nodes.reserve(pins.size());
  for (const auto& pin : pins)
    nodes.push_back({pin.id, pin.parent_id, pin.folder});
  return nodes;
}

bool IsValidFolderTitle(std::string_view title) {
  return !title.empty() && title.size() <= 256 && base::IsStringUTF8(title);
}

bool IsGitHubFolder(const std::vector<prototype::PinnedItem>& pins,
                    std::string_view id) {
  const auto item = std::ranges::find(pins, id, &prototype::PinnedItem::id);
  return item != pins.end() && item->live_folder.has_value();
}

bool ValidGitHubScope(const prototype::LiveFolderConfig& config) {
  return (!config.account.empty() || !config.filter.empty()) &&
         (config.account.empty() || RE2::FullMatch(config.account, "[a-zA-Z0-9-]{1,39}")) &&
         (config.filter.empty() ||
          RE2::FullMatch(config.filter, "[a-zA-Z0-9-]{1,39}/[a-zA-Z0-9_.-]{1,100}"));
}

bool ValidGitHubToken(std::string_view token) {
  return RE2::FullMatch(token, "[a-zA-Z0-9_]{1,512}");
}

std::string GitHubCredentialPrefix(std::string_view profile,
                                   std::string_view workspace,
                                   std::string_view folder) {
  return base::StrCat(
      {"zitter-github-folder-v1\n", profile, "\n", workspace, "\n", folder, "\n"});
}

std::string GitHubPullKey(const GURL& url) {
  if (!url.SchemeIs("https") || url.host() != "github.com" ||
      url.has_username() || url.has_password() || url.has_port() ||
      !RE2::FullMatch(url.path(), "/[a-zA-Z0-9-]+/[a-zA-Z0-9_.-]+/pull/[1-9][0-9]*")) {
    return {};
  }
  return base::ToLowerASCII(url.path());
}

base::DictValue WriteProfileMetadata(const ContentProfileInfo& profile) {
  auto metadata = base::DictValue().Set("id", profile.id).Set("name", profile.name);
  if (profile.separate_network)
    metadata.Set("separate_network", true);
  if (!profile.import_source.empty())
    metadata.Set("import_source", profile.import_source);
  return metadata;
}

void SetCatalogueName(base::DictValue& session, std::string_view id,
                      std::string_view name, std::string_view builtin_key,
                      std::string_view extra_key) {
  const auto builtin = std::ranges::find(kWorkspaceIds, id);
  if (builtin != kWorkspaceIds.end()) {
    if (name != kBuiltinProfileNames[builtin - kWorkspaceIds.begin()]) {
      session.EnsureDict(builtin_key)->Set(id, name);
    } else if (auto* overrides = session.FindDict(builtin_key)) {
      overrides->Remove(id);
      if (overrides->empty())
        session.Remove(builtin_key);
    }
    return;
  }
  auto* extras = session.FindList(extra_key);
  CHECK(extras);
  for (auto& value : *extras) {
    auto& entry = value.GetDict();
    if (*entry.FindString("id") == id) {
      entry.Set("name", name);
      return;
    }
  }
  NOTREACHED();
}

void AppendWorkspaceMetadata(base::DictValue& session, const Workspace& workspace,
                             std::string_view import_source) {
  auto metadata = base::DictValue().Set("id", workspace.id).Set("name", workspace.name);
  if (!import_source.empty())
    metadata.Set("import_source", import_source);
  session.EnsureList("extra_workspaces")->Append(std::move(metadata));
}

bool PrepareProfileDirectories(const base::FilePath& session_path,
                      const ContentProfileInfo& profile,
                      std::vector<base::ScopedTempDir>& directories) {
  const auto root = session_path.DirName();
  const auto directory = root.AppendASCII(profile.id);
  base::ScopedTempDir pending_profile;
  if (base::PathExists(directory) || base::IsLink(directory) ||
      !pending_profile.Set(directory)) {
    return false;
  }
  base::ScopedTempDir pending_network;
  if (profile.separate_network) {
    const auto network_root = root.AppendASCII(kWorkspaceNetworkRoot);
    const auto network = network_root.AppendASCII(profile.id);
    if (base::IsLink(network_root) || !base::DirectoryExists(network_root) ||
        base::PathExists(network) || base::IsLink(network) ||
        !pending_network.Set(network)) {
      return false;
    }
  } else if (!base::CreateDirectory(directory.AppendASCII("Network")) ||
             !base::CreateDirectory(directory.AppendASCII("NetworkBootstrap"))) {
    return false;
  }
  directories.push_back(std::move(pending_profile));
  if (pending_network.IsValid())
    directories.push_back(std::move(pending_network));
  return true;
}

bool ProvisionProfile(const base::FilePath& session_path,
                      const ContentProfileInfo& profile,
                      const base::DictValue& session) {
  std::vector<base::ScopedTempDir> directories;
  if (!PrepareProfileDirectories(session_path, profile, directories) ||
      !WriteContentWorkspaceSession(session_path, session)) {
    return false;
  }
  for (auto& directory : directories)
    (void)directory.Take();
  return true;
}

std::vector<uint32_t> FaviconPixels(const gfx::Image& image) {
  if (image.IsEmpty())
    return {};
  constexpr int kEdge = 32;
  const auto scaled = gfx::ImageSkiaOperations::CreateResizedImage(
      image.AsImageSkia(), skia::ImageOperations::RESIZE_BEST,
      gfx::Size(kEdge, kEdge));
  const auto& bitmap = scaled.GetRepresentation(1.0f).GetBitmap();
  std::vector<uint32_t> pixels(kEdge * kEdge);
  if (!bitmap.readPixels(SkImageInfo::MakeN32Premul(kEdge, kEdge),
                         pixels.data(), kEdge * sizeof(uint32_t), 0, 0)) {
    return {};
  }
  return pixels;
}

bool HasClearableData(const BrowsingDataSelection& selection) {
  return selection.period >= 0 && selection.period <= 4 &&
         (selection.history || selection.cookies || selection.cache ||
          selection.downloads || selection.passwords || selection.site_settings ||
          selection.time_machine.value_or(false)) &&
         !selection.form_data;
}
constexpr std::array<std::string_view, 2> kTitles = {
    "Content Work tab", "Content Personal tab"};

// The shell still exposes controls for services that the lab is bringing up.
template <typename Result, typename... Args>
void SetUnavailable(base::RepeatingCallback<Result(Args...)>& callback) {
  if (callback)
    return;
  callback = base::BindRepeating([](Args...) -> Result {
    if constexpr (!std::is_void_v<Result>)
      return Result{};
  });
}

void SetUnavailableActions(prototype::WorkspaceShell::Callbacks& callbacks) {
  auto set = [&](auto&... fields) { (SetUnavailable(fields), ...); };
  set(callbacks.add_workspace, callbacks.edit_workspace, callbacks.remove_workspace,
      callbacks.create_profile, callbacks.rename_profile, callbacks.remove_profile,
      callbacks.assign_profile, callbacks.preview_import, callbacks.invalidate_import,
      callbacks.apply_import, callbacks.dismiss_import, callbacks.open_pin,
      callbacks.duplicate_pin, callbacks.pin_tab, callbacks.unpin, callbacks.move_pin,
      callbacks.update_pinned_url, callbacks.reset_pinned_url,
      callbacks.create_folder, callbacks.rename_folder, callbacks.delete_folder,
      callbacks.restore_pins, callbacks.restore_workspace, callbacks.duplicate_tab,
      callbacks.move_tab,
      callbacks.move_tab_to_workspace, callbacks.activate_result,
      callbacks.commands, callbacks.system_settings,
      callbacks.promote_peek, callbacks.close_peek, callbacks.peek,
      callbacks.open_link, callbacks.set_link_rule, callbacks.split_tab,
      callbacks.split_pin, callbacks.leave_split, callbacks.unsplit,
      callbacks.close_split_tabs, callbacks.move_split_pane,
      callbacks.split_vertical, callbacks.write_assistant);
}

class ProbePasswordAuthenticator final
    : public device_reauth::DeviceAuthenticator {
 public:
  explicit ProbePasswordAuthenticator(
      base::RepeatingCallback<void(AuthenticateCallback)> on_request)
      : on_request_(std::move(on_request)) {}

  bool CanAuthenticateWithBiometrics() override { return false; }
  bool CanAuthenticateWithBiometricOrScreenLock() override { return true; }
  void AuthenticateWithMessage(const std::u16string&,
                               AuthenticateCallback callback) override {
    on_request_.Run(std::move(callback));
  }
  // The probe controls the reply to simulate an already queued OS result.
  void Cancel() override {}

 private:
  base::RepeatingCallback<void(AuthenticateCallback)> on_request_;
};

constexpr std::array kPageZoomSteps = {
    25, 33, 50, 67, 75, 80, 90, 100, 110, 125, 150, 175, 200, 250, 300, 400, 500};
#if BUILDFLAG(IS_MAC)
constexpr int kPageZoomModifier = ui::EF_COMMAND_DOWN;
constexpr int kPageZoomWebModifier = blink::WebInputEvent::kMetaKey;
#else
constexpr int kPageZoomModifier = ui::EF_CONTROL_DOWN;
constexpr int kPageZoomWebModifier = blink::WebInputEvent::kControlKey;
#endif

std::optional<int> PageZoomStep(const ui::Accelerator& accelerator) {
  if (accelerator.modifiers() != kPageZoomModifier)
    return std::nullopt;
  switch (accelerator.key_code()) {
    case ui::VKEY_OEM_PLUS:
    case ui::VKEY_ADD:
      return 1;
    case ui::VKEY_OEM_MINUS:
    case ui::VKEY_SUBTRACT:
      return -1;
    case ui::VKEY_0:
    case ui::VKEY_NUMPAD0:
      return 0;
    default:
      return std::nullopt;
  }
}

void ZoomWorkspacePage(content::WebContents& page, int step) {
  auto* map = content::HostZoomMap::GetForWebContents(&page);
  if (step == 0) {
    content::HostZoomMap::SetZoomLevel(&page, map->GetDefaultZoomLevel());
    page.SetPageScale(1.f);
    return;
  }
  const int current = std::lround(
      blink::ZoomLevelToZoomFactor(content::HostZoomMap::GetZoomLevel(&page)) * 100);
  int next = current;
  if (step > 0) {
    if (const auto larger = std::ranges::upper_bound(kPageZoomSteps, current);
        larger != kPageZoomSteps.end()) {
      next = *larger;
    }
  } else if (const auto smaller = std::ranges::lower_bound(kPageZoomSteps, current);
             smaller != kPageZoomSteps.begin()) {
    next = *std::prev(smaller);
  }
  if (next != current)
    content::HostZoomMap::SetZoomLevel(&page, blink::ZoomFactorToZoomLevel(next / 100.0));
}

class WorkspaceWebView final : public LabWebView {
 public:
  using ShowPageMenu = base::RepeatingCallback<bool(
      content::RenderFrameHost&, const content::ContextMenuParams&)>;
  using AdoptPage = base::RepeatingCallback<content::WebContents*(
      content::WebContents*, std::unique_ptr<content::WebContents>, const GURL&,
      WindowOpenDisposition, bool, bool)>;
  using OpenPageURL = base::RepeatingCallback<content::WebContents*(
      content::WebContents*, const content::OpenURLParams&,
      base::OnceCallback<void(content::NavigationHandle&)>)>;

  WorkspaceWebView(ProfileContext* context,
                   base::WeakPtr<WorkspaceMediaAccess> media_access,
                   base::RepeatingCallback<void(GURL)> open_article,
                   ShowPageMenu show_page_menu, AdoptPage adopt_page,
                   OpenPageURL open_page_url,
                   base::RepeatingCallback<void(content::WebContents*)> close_popup,
                   base::RepeatingCallback<bool(content::WebContents*)> can_fullscreen,
                   base::RepeatingCallback<void(content::WebContents*, bool)> fullscreen)
      : LabWebView(context),
        profile_(context),
        media_access_(std::move(media_access)),
        open_article_(std::move(open_article)),
        show_page_menu_(std::move(show_page_menu)),
        adopt_page_(std::move(adopt_page)),
        open_page_url_(std::move(open_page_url)),
        close_popup_(std::move(close_popup)),
        can_fullscreen_(std::move(can_fullscreen)),
        fullscreen_(std::move(fullscreen)) {}
  ~WorkspaceWebView() override = default;
  METADATA_HEADER(WorkspaceWebView, LabWebView)

 public:
  const GURL& hovered_link_for_testing() const { return hovered_link_; }

  void SetPageZoomShortcutFilter(
      base::RepeatingCallback<bool(const ui::Accelerator&)> filter) {
    page_zoom_shortcut_filter_ = std::move(filter);
  }

  void ContentsZoomChange(bool zoom_in) override {
    if (auto* page = web_contents(); page && GetVisible())
      ZoomWorkspacePage(*page, zoom_in ? 1 : -1);
  }

  content::KeyboardEventProcessingResult PreHandleKeyboardEvent(
      content::WebContents* source,
      const input::NativeWebKeyboardEvent& event) override {
    using Result = content::KeyboardEventProcessingResult;
    using Type = blink::WebInputEvent::Type;
    const auto accelerator = ui::GetAcceleratorFromNativeWebKeyboardEvent(event);
    const auto key = accelerator.key_code();
    if (event.GetType() == Type::kKeyUp && page_zoom_keys_.erase(key)) {
      suppress_page_zoom_char_ = false;
      return Result::HANDLED;
    }
    if (event.GetType() == Type::kChar && suppress_page_zoom_char_)
      return Result::HANDLED;
    if (event.GetType() == Type::kRawKeyDown || event.GetType() == Type::kKeyDown) {
      page_zoom_keys_.erase(key);
      suppress_page_zoom_char_ = false;
      const auto step = PageZoomStep(accelerator);
      if (step && !event.skip_if_unhandled && source && source == web_contents() &&
          GetVisible() && page_zoom_shortcut_filter_.Run(accelerator)) {
        page_zoom_keys_.insert(key);
        suppress_page_zoom_char_ = true;
        ZoomWorkspacePage(*source, *step);
        return Result::HANDLED;
      }
    }
    return LabWebView::PreHandleKeyboardEvent(source, event);
  }

  void SetNavigationChangedCallback(
      base::RepeatingCallback<void(content::WebContents*)> callback) {
    navigation_changed_ = std::move(callback);
  }

  void SetPresentationChangedCallback(base::RepeatingClosure callback) {
    presentation_changed_ = std::move(callback);
  }

  prototype::RssPanel* MountReader() {
    if (!rss_) {
      CloseExtensionSidePanel();
      rss_ = std::make_unique<prototype::RssPanel>();
      rss_->Show(profile_->GetRssReader(), this, open_article_);
      holder()->SetVisible(false);
      set_lock_child_ax_tree_id_override(true);
      GetViewAccessibility().RemoveChildTreeID();
      GetViewAccessibility().SetIsIgnored(false);
      GetViewAccessibility().SetRole(ax::mojom::Role::kGenericContainer);
    }
    return rss_.get();
  }

  void SetDevTools(content::WebContents* contents,
                   std::optional<gfx::Rect> page_bounds) {
    if (contents)
      CloseExtensionSidePanel();
    if (contents && !devtools_) {
      // This view hosts a borrowed frontend and never creates WebContents.
      devtools_ = AddChildViewAt(std::make_unique<views::WebView>(), 0);
      devtools_->set_allow_accelerators(true);
    }
    if (!devtools_)
      return;
    const bool changed = devtools_->web_contents() != contents;
    if (changed) {
      devtools_->SetWebContents(contents);
      devtools_->SetVisible(contents != nullptr);
      set_lock_child_ax_tree_id_override(contents != nullptr);
      if (contents) {
        GetViewAccessibility().RemoveChildTreeID();
        GetViewAccessibility().SetIsIgnored(false);
        GetViewAccessibility().SetRole(ax::mojom::Role::kGenericContainer);
      } else {
        GetViewAccessibility().SetRole(ax::mojom::Role::kWebView);
        AXTreeIDForMainFrameHasChanged();
      }
    }
    if (changed || page_bounds_ != page_bounds) {
      page_bounds_ = page_bounds;
      InvalidateLayout();
    }
  }

  bool SetExtensionSidePanel(const std::string& id, int tab_id, const GURL& url, bool toggle) {
    if (url.is_empty()) {
      if (side_panel_ && side_panel_id_ == id)
        CloseExtensionSidePanel();
      return true;
    }
    if (rss_ || (devtools_ && devtools_->web_contents()))
      return false;
    if (side_panel_ && side_panel_id_ == id && side_panel_url_ == url) {
      if (toggle)
        CloseExtensionSidePanel();
      return true;
    }
    CloseExtensionSidePanel();
    side_panel_id_ = id;
    side_panel_tab_id_ = tab_id;
    side_panel_url_ = url;
    auto view = std::make_unique<LabWebView>(profile_);
    auto* contents = view->GetWebContents(url);
    SetWorkspaceSidePanelDestination(*contents, url);
    side_panel_ = AddChildView(std::move(view));
    side_panel_close_ = AddChildView(std::make_unique<views::LabelButton>(
        base::BindRepeating(&WorkspaceWebView::CloseExtensionSidePanel,
                            base::Unretained(this)),
        prototype::T(u"Zamknij")));
    set_lock_child_ax_tree_id_override(true);
    GetViewAccessibility().RemoveChildTreeID();
    GetViewAccessibility().SetIsIgnored(false);
    GetViewAccessibility().SetRole(ax::mojom::Role::kGenericContainer);
    InvalidateLayout();
    prototype::LoadWorkspaceTab(contents, url, ui::PAGE_TRANSITION_FROM_API);
    DispatchWorkspaceSidePanelEvent(*profile_, id, tab_id, url, true);
    return true;
  }

  void CloseExtensionSidePanel() {
    if (!side_panel_)
      return;
    auto* panel = std::exchange(side_panel_, nullptr).get();
    auto* close = std::exchange(side_panel_close_, nullptr).get();
    RemoveChildViewT(panel);
    RemoveChildViewT(close);
    const auto id = std::exchange(side_panel_id_, {});
    const auto url = std::exchange(side_panel_url_, {});
    DispatchWorkspaceSidePanelEvent(*profile_, id, side_panel_tab_id_, url, false);
    set_lock_child_ax_tree_id_override(false);
    GetViewAccessibility().SetRole(ax::mojom::Role::kWebView);
    AXTreeIDForMainFrameHasChanged();
    InvalidateLayout();
  }

  content::WebContents* extension_side_panel_for_testing() const {
    return side_panel_ ? side_panel_->web_contents() : nullptr;
  }

  bool HasDockedDevTools() {
    if (!devtools_ || !devtools_->IsDrawn() || !page_bounds_)
      return false;
    gfx::Rect expected = GetLocalBounds();
    expected.Intersect(*page_bounds_);
    return !expected.IsEmpty() && expected != GetLocalBounds() &&
           holder()->bounds() == expected;
  }

 private:
  void OnAudioStateChanged(bool audible) override {
    if (presentation_changed_)
      presentation_changed_.Run();
  }

  void DidUpdateAudioMutingState(bool muted) override {
    if (presentation_changed_)
      presentation_changed_.Run();
  }

  void NavigationStateChanged(content::WebContents* source,
                              content::InvalidateTypes changed_flags) override {
    if ((changed_flags & content::INVALIDATE_TYPE_ICON) && presentation_changed_)
      presentation_changed_.Run();
  }

  void DidFinishNavigation(content::NavigationHandle* navigation) override {
    if (!navigation->IsInPrimaryMainFrame() || !navigation->HasCommitted())
      return;
    if (prototype::IsRssTabUrl(web_contents()->GetVisibleURL())) {
      if (GetWidget())
        MountReader();
    } else if (rss_) {
      rss_.reset();
      holder()->SetVisible(true);
      set_lock_child_ax_tree_id_override(false);
      GetViewAccessibility().SetRole(ax::mojom::Role::kWebView);
      AXTreeIDForMainFrameHasChanged();
    }
    if (navigation_changed_)
      navigation_changed_.Run(web_contents());
  }

  void OnFocus() override {
    if (rss_)
      rss_->RequestFocus();
    else
      LabWebView::OnFocus();
  }

  gfx::NativeViewAccessible GetNativeViewAccessible() override {
    return rss_ ? views::View::GetNativeViewAccessible()
                : LabWebView::GetNativeViewAccessible();
  }

  bool SkipDefaultKeyEventProcessing(const ui::KeyEvent& event) override {
    return rss_ ? views::View::SkipDefaultKeyEventProcessing(event)
                : LabWebView::SkipDefaultKeyEventProcessing(event);
  }

  void RequestMediaAccessPermission(content::WebContents* source,
                                    const content::MediaStreamRequest& request,
                                    content::MediaResponseCallback reply) override {
    if (media_access_)
      media_access_->Request(source, request, std::move(reply));
    else
      LabWebView::RequestMediaAccessPermission(source, request, std::move(reply));
  }

  bool CheckMediaAccessPermission(content::RenderFrameHost* frame,
                                  const url::Origin& origin,
                                  blink::mojom::MediaStreamType type) override {
    return media_access_ && media_access_->Check(frame, origin, type);
  }

  void Layout(PassKey) override {
    LayoutSuperclass<LabWebView>(this);
    gfx::Rect bounds = GetLocalBounds();
    if (rss_) {
      if (rss_->view() && rss_->view()->bounds() != bounds)
        rss_->view()->SetBoundsRect(bounds);
      return;
    }
    if (devtools_ && devtools_->web_contents()) {
      devtools_->SetBoundsRect(bounds);
      if (page_bounds_) {
        bounds.Intersect(*page_bounds_);
      } else {
        bounds.set_width(bounds.width() / 2);
      }
    }
    if (side_panel_) {
      const int width = std::min(380, bounds.width() / 2);
      const int header = std::min(32, bounds.height());
      const int left = bounds.right() - width;
      side_panel_close_->SetBounds(left, bounds.y(), width, header);
      side_panel_->SetBounds(left, bounds.y() + header, width, bounds.height() - header);
      bounds.set_width(bounds.width() - width);
    }
    holder()->SetBoundsRect(bounds);
  }

  void UpdateTargetURL(content::WebContents*, const GURL& url) override {
    hovered_link_ = url;
  }

  bool PreHandleMouseEvent(content::WebContents* source,
                           const blink::WebMouseEvent& event) override {
    constexpr int modifiers = blink::WebInputEvent::kShiftKey |
        blink::WebInputEvent::kControlKey | blink::WebInputEvent::kMetaKey |
        blink::WebInputEvent::kAltKey;
    if (event.GetType() == blink::WebInputEvent::Type::kMouseDown) {
      shift_link_click_ = {};
      shift_link_url_ = {};
      if (event.button == blink::WebMouseEvent::Button::kLeft &&
          (event.GetModifiers() & modifiers) == blink::WebInputEvent::kShiftKey &&
          hovered_link_.SchemeIsHTTPOrHTTPS()) {
        shift_link_click_ = base::TimeTicks::Now();
        shift_link_url_ = hovered_link_;
      }
    }
    return LabWebView::PreHandleMouseEvent(source, event);
  }

 public:
  bool ConsumeShiftLink(const GURL& url) {
    if (shift_link_url_ != url || shift_link_click_.is_null() ||
        base::TimeTicks::Now() - shift_link_click_ >= base::Seconds(1)) {
      return false;
    }
    shift_link_click_ = {};
    shift_link_url_ = {};
    return true;
  }

 private:
  bool ShouldResumeRequestsForCreatedWindow() override { return false; }
  bool CanEnterFullscreenModeForTab(content::RenderFrameHost* frame) override {
    return frame && frame->IsActive() && IsDrawn() &&
           can_fullscreen_.Run(content::WebContents::FromRenderFrameHost(frame));
  }

  void EnterFullscreenModeForTab(
      content::RenderFrameHost* frame,
      const blink::mojom::FullscreenOptions&) override {
    if (!CanEnterFullscreenModeForTab(frame))
      return;
    is_fullscreen_ = true;
    fullscreen_.Run(web_contents(), true);
  }

  void ExitFullscreenModeForTab(content::WebContents* page) override {
    if (std::exchange(is_fullscreen_, false))
      fullscreen_.Run(page, false);
  }

  bool IsFullscreenForTabOrPending(const content::WebContents*) override {
    return is_fullscreen_;
  }

  content::FullscreenState GetFullscreenState(
      const content::WebContents*) const override {
    return {.target_mode = is_fullscreen_ ? content::FullscreenMode::kContent
                                          : content::FullscreenMode::kWindowed};
  }

  blink::mojom::DisplayMode GetDisplayMode(const content::WebContents*) override {
    return is_fullscreen_ ? blink::mojom::DisplayMode::kFullscreen
                          : blink::mojom::DisplayMode::kBrowser;
  }

  bool IsBackForwardCacheSupported(content::WebContents&) override {
    return true;
  }

  content::WebContents* AddNewContents(
      content::WebContents* source, std::unique_ptr<content::WebContents> page,
      const GURL& target_url, WindowOpenDisposition disposition,
      const blink::mojom::WindowFeatures&, bool user_gesture,
      bool* was_blocked) override {
    const bool shift_link = user_gesture &&
        disposition == WindowOpenDisposition::NEW_WINDOW && ConsumeShiftLink(target_url);
    auto* adopted = adopt_page_.Run(source, std::move(page), target_url,
                                    disposition, user_gesture, shift_link);
    if (!adopted && was_blocked)
      *was_blocked = true;
    return adopted;
  }

  content::WebContents* OpenURLFromTab(
      content::WebContents* source, const content::OpenURLParams& params,
      base::OnceCallback<void(content::NavigationHandle&)> callback) override {
    return open_page_url_.Run(source, params, std::move(callback));
  }

  void CloseContents(content::WebContents* page) override {
    close_popup_.Run(page);
  }

  void ShowRepostFormWarningDialog(content::WebContents* page) override {
    if (auto* dialogs = prototype::PageDialogs::FromWebContents(page))
      dialogs->ConfirmFormResubmission();
    else
      page->GetController().CancelPendingReload();
  }

  bool HandleContextMenu(content::RenderFrameHost& frame,
                         const content::ContextMenuParams& params) override {
    return show_page_menu_.Run(frame, params);
  }
  content::JavaScriptDialogManager* GetJavaScriptDialogManager(
      content::WebContents* source) override;
  using content::WebContentsObserver::BeforeUnloadFired;
  void BeforeUnloadFired(content::WebContents* source,
                         bool proceed,
                         bool* fire_unload) override;
  raw_ptr<LabWebView> side_panel_ = nullptr;
  raw_ptr<views::LabelButton> side_panel_close_ = nullptr;
  std::string side_panel_id_;
  GURL side_panel_url_;
  int side_panel_tab_id_ = -1;
  raw_ptr<views::WebView> devtools_ = nullptr;
  std::optional<gfx::Rect> page_bounds_;
  raw_ptr<ProfileContext> profile_;
  base::WeakPtr<WorkspaceMediaAccess> media_access_;
  base::RepeatingCallback<void(GURL)> open_article_;
  ShowPageMenu show_page_menu_;
  GURL hovered_link_;
  GURL shift_link_url_;
  base::TimeTicks shift_link_click_;
  AdoptPage adopt_page_;
  OpenPageURL open_page_url_;
  base::RepeatingCallback<void(content::WebContents*)> close_popup_;
  base::RepeatingCallback<bool(content::WebContents*)> can_fullscreen_;
  base::RepeatingCallback<void(content::WebContents*, bool)> fullscreen_;
  bool is_fullscreen_ = false;
  std::unique_ptr<prototype::RssPanel> rss_;
  base::RepeatingCallback<void(content::WebContents*)> navigation_changed_;
  base::RepeatingClosure presentation_changed_;
  base::RepeatingCallback<bool(const ui::Accelerator&)> page_zoom_shortcut_filter_;
  std::set<ui::KeyboardCode> page_zoom_keys_;
  bool suppress_page_zoom_char_ = false;
};

content::JavaScriptDialogManager* WorkspaceWebView::GetJavaScriptDialogManager(
    content::WebContents* source) {
  return prototype::PageDialogs::FromWebContents(source);
}

void WorkspaceWebView::BeforeUnloadFired(content::WebContents* source,
                                        bool proceed,
                                        bool* fire_unload) {
  const auto* dialogs = prototype::PageDialogs::FromWebContents(source);
  *fire_unload = proceed && !(dialogs && dialogs->checking_before_unload());
}

BEGIN_METADATA(WorkspaceWebView)
END_METADATA

void HideTabView(views::WebView& view) {
  view.SetVisible(false);
  if (auto* page = view.web_contents())
    page->WasHidden();
}

bool IsNewPageDisposition(WindowOpenDisposition disposition) {
  return disposition == WindowOpenDisposition::NEW_FOREGROUND_TAB ||
         disposition == WindowOpenDisposition::NEW_BACKGROUND_TAB ||
         disposition == WindowOpenDisposition::NEW_WINDOW ||
         disposition == WindowOpenDisposition::NEW_POPUP;
}

bool CanOpenPageSource(content::WebContents* page) {
  if (!page)
    return false;
  auto& controller = page->GetController();
  const auto* entry = controller.GetLastCommittedEntry();
  return entry && entry->GetPageType() == content::PAGE_TYPE_NORMAL &&
         !entry->GetHasPostData() && controller.CanViewSource() &&
         entry->GetURL().SchemeIsHTTPOrHTTPS() &&
         CanRestoreWorkspaceURL(entry->GetURL()) &&
         entry->GetURL().spec().size() <= 32768;
}

GURL WorkspaceDocumentURL(content::WebContents& page) {
  const auto* entry = page.GetController().GetLastCommittedEntry();
  return entry && entry->IsViewSourceMode() ? entry->GetVirtualURL()
                                            : page.GetLastCommittedURL();
}

bool IsPageOpeningURL(const GURL& url) {
  return url.is_empty() || url == GURL("about:blank") ||
         (url.SchemeIsHTTPOrHTTPS() && CanRestoreWorkspaceURL(url));
}

}  // namespace

#if BUILDFLAG(IS_MAC)
ui::ContextFactory* LabViewsDelegate::GetContextFactory() {
  return content::GetContextFactory();
}
#endif

struct WorkspaceLab::PrivateSession {
  explicit PrivateSession(scoped_refptr<base::SequencedTaskRunner> file_runner)
      : directory(new base::ScopedTempDir,
                  base::OnTaskRunnerDeleter(std::move(file_runner))) {}
  ~PrivateSession() = default;

  std::unique_ptr<base::ScopedTempDir, base::OnTaskRunnerDeleter> directory;
  std::unique_ptr<ProfileContext> profile;
  base::CallbackListSubscription filter_ready;
  std::vector<std::unique_ptr<WorkspaceLab>> windows;
};

struct WorkspaceLab::GitHubRefresh {
  base::UnguessableToken id = base::UnguessableToken::Create();
  std::string workspace;
  std::string profile;
  std::string folder;
  prototype::LiveFolderConfig config;
  bool manual = false;
  std::vector<std::string> queries;
  std::map<std::string, size_t> result_indexes;
  std::vector<prototype::PinnedItem> results;
  std::unique_ptr<network::SimpleURLLoader> loader;
};

struct WorkspaceLab::PendingBrowserImport {
  struct Space {
    Workspace workspace;
    std::string source;
    std::u16string emoji;
  };
  std::vector<ContentProfileInfo> profiles;
  std::vector<std::unique_ptr<ProfileContext>> contexts;
  std::vector<Space> spaces;
  base::DictValue session;
  prototype::BrowsingImportResult result;
  bool browsing_data = false;
};

namespace {

constexpr std::u16string_view kUnfilteredDnrProbeTitle =
    u"allow,allow,allow,allow,allow,original,original/original,allow,allow,"
    u"original,original/original";

}  // namespace

struct WorkspaceLab::ExtensionDnrNavigationProbe {
  class Request final : public content::WebContentsObserver {
   public:
    Request(ProfileContext& profile, const GURL& start, const GURL& destination,
            net::Error expected_error)
        : page_(content::WebContents::Create(
              content::WebContents::CreateParams(&profile))),
          destination_(destination),
          expected_error_(expected_error) {
      Observe(page_.get());
      page_->GetController().LoadURLWithParams(
          content::NavigationController::LoadURLParams(start));
    }

    std::optional<bool> result() const { return result_; }

   private:
    void DidFinishNavigation(content::NavigationHandle* navigation) override {
      if (!navigation->IsInPrimaryMainFrame() || navigation->IsSameDocument())
        return;
      result_ = navigation->GetURL() == destination_ &&
                navigation->HasCommitted() &&
                navigation->IsErrorPage() == (expected_error_ != net::OK) &&
                navigation->GetNetErrorCode() == expected_error_;
      if (!*result_) {
        LOG(ERROR) << "DNR navigation did not produce the expected result. Expected "
                   << expected_error_ << ", observed " << navigation->GetNetErrorCode();
      }
    }

    std::unique_ptr<content::WebContents> page_;
    const GURL destination_;
    const net::Error expected_error_;
    std::optional<bool> result_;
  };

  std::unique_ptr<ProfileContext> private_profile;
  std::vector<std::unique_ptr<Request>> requests;
  std::unique_ptr<content::WebContents> private_subresources;
};

class WorkspaceLab::ExtensionShortcutTarget final : public ui::AcceleratorTarget {
 public:
  explicit ExtensionShortcutTarget(WorkspaceLab& workspace) : workspace_(workspace) {}

  bool AcceleratorPressed(const ui::Accelerator& accelerator) override {
    return workspace_->DispatchExtensionShortcut(accelerator);
  }

  bool CanHandleAccelerators() const override {
    return workspace_->CanHandleAccelerators();
  }

 private:
  const raw_ref<WorkspaceLab> workspace_;
};

WorkspaceLab::WorkspaceLab(const base::FilePath& settings_path,
                           const base::FilePath& temporary_root,
                           base::FilePath session_path,
                           base::DictValue session,
                           std::unique_ptr<prototype::TimelineStore> timeline,
                           GURL session_probe_url,
                           ProfileContext& work,
                           ProfileContext* personal,
                           bool dark,
                           bool metal,
                           bool smoke,
                           WorkspaceLab* private_owner,
                           base::span<const std::unique_ptr<ProfileContext>> extra_profiles,
                           os_crypt_async::OSCryptAsync* os_crypt,
                           base::FilePath user_download_directory,
                           std::string download_safe_browsing_key,
                           bool offer_startup_import,
                           std::string startup_import_probe)
    : devtools_path_(temporary_root.AppendASCII("devtools")),
      session_path_(std::move(session_path)),
      user_download_directory_(std::move(user_download_directory)),
      download_safe_browsing_key_(std::move(download_safe_browsing_key)),
      session_writer_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskShutdownBehavior::BLOCK_SHUTDOWN})),
      timeline_(std::move(timeline)),
      session_probe_url_(std::move(session_probe_url)),
      dark_(dark),
      metal_(metal),
      smoke_(smoke),
      private_owner_(private_owner),
      os_crypt_(os_crypt),
      contexts_{&work},
      startup_import_pending_(offer_startup_import),
      startup_import_probe_(std::move(startup_import_probe)) {
  if (personal)
    contexts_.push_back(personal);
  for (const auto& profile : extra_profiles)
    contexts_.push_back(profile.get());
  profiles_ = ReadContentProfiles(session).value();
  if (private_owner_)
    profiles_.resize(1);
  CHECK_EQ(profiles_.size(), contexts_.size());
  if (const auto* extras = session.FindList("extra_workspaces")) {
    for (const auto& entry : *extras) {
      if (const auto* source = entry.GetDict().FindString("import_source"))
        imported_workspace_sources_.emplace(*entry.GetDict().FindString("id"), *source);
    }
  }
  CHECK(private_owner_ ? !personal && work.IsOffTheRecord() : personal != nullptr);
  if (!views::LayoutProvider::Get())
    layout_provider_ = std::make_unique<views::LayoutProvider>();
  if (!views::ViewsDelegate::GetInstance())
    views_delegate_ = std::make_unique<LabViewsDelegate>();
  prototype::ApplyUiLanguage("en-US");
  prototype::WorkspaceShell::Callbacks callbacks;
  callbacks.private_mode = private_owner_ != nullptr;
  if (!private_owner_ && IsManagedExtensionSession()) {
    for (const auto& context : contexts_) {
      for (const auto& extension : GetConfiguredExtensions(*context)) {
        auto title = base::UTF8ToUTF16(extension->name());
        base::i18n::SanitizeUserSuppliedString(&title);
        const auto add_preference = [&](std::string key, std::u16string label) {
          if (!std::ranges::contains(callbacks.extension_preferences, key,
                                     &std::pair<std::string, std::u16string>::first)) {
            callbacks.extension_preferences.emplace_back(std::move(key), std::move(label));
          }
        };
        add_preference("extension_enabled." + extension->id(), title);
        if (extensions::UserScriptManager::IsUserScriptsAPIPermissionAvailable(*extension)) {
          add_preference("extension_user_scripts." + extension->id(),
                         prototype::TF(u"Skrypty użytkownika: {}", {title}));
        }
      }
    }
  } else {
    callbacks.show_extension_enablement =
        !private_owner_ && base::CommandLine::ForCurrentProcess()->HasSwitch(
                              kExtensionRegistryProbe);
  }
  callbacks.can_create_profile = false;
  callbacks.can_reassign_workspace = !private_owner_;
#if BUILDFLAG(ENABLE_PRINTING)
  callbacks.can_export_pdf = true;
#if BUILDFLAG(ENABLE_PDF)
  callbacks.can_preview_pdf = true;
#endif
#endif
  const auto weak = weak_factory_.GetWeakPtr();
  if (!private_owner_) {
    for (auto& profile : contexts_) {
      if (!smoke_ && session_path_.empty())
        profile->InitializePrivateAdblock();
      ConfigureProfileForWorkspace(*profile);
    }
  }
  if (!private_owner_ && !session_path_.empty() &&
      base::CommandLine::ForCurrentProcess()->HasSwitch(kRestoreWorkspaceExtensions)) {
    callbacks.install_extension =
        base::BindRepeating(&WorkspaceLab::OpenExtensionPackage, weak);
  }
  if (!private_owner_ && !session_path_.empty() && IsManagedExtensionSession()) {
    callbacks.uninstall_extension =
        base::BindRepeating(&WorkspaceLab::ConfirmUninstallExtension, weak);
    callbacks.open_extension_options =
        base::BindRepeating(&WorkspaceLab::OpenExtensionOptions, weak);
    callbacks.open_extension_popup =
        base::BindRepeating(&WorkspaceLab::OpenExtensionPopup, weak);
  }
  if (!private_owner_ && !session_path_.empty() && IsManagedExtensionSession()) {
    callbacks.open_extensions_manager = base::BindRepeating(
        [](base::WeakPtr<WorkspaceLab> lab) {
          if (lab)
            lab->OpenExtensionsManager();
        }, weak);
  }
  callbacks.select_workspace =
      base::BindRepeating(&WorkspaceLab::SelectWorkspace, weak);
  callbacks.add_workspace = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, std::string name, std::string profile) {
        return lab && lab->AddWorkspace(std::move(name), std::move(profile));
      }, weak);
  callbacks.edit_workspace = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, std::string name, std::string profile) {
        return lab && lab->model_.active() &&
               lab->UpdateWorkspace(lab->model_.active()->id, std::move(name),
                                    std::move(profile));
      }, weak);
  callbacks.assign_profile = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, std::string id, std::string profile) {
        const auto* workspace = lab ? lab->model_.Find(id) : nullptr;
        return workspace && lab->UpdateWorkspace(std::move(id), workspace->name,
                                                 std::move(profile));
      }, weak);
  callbacks.rename_profile = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, std::string id, std::string name) {
        return lab && lab->RenameProfile(std::move(id), std::move(name));
      }, weak);
  callbacks.create_profile = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, std::string name, std::string workspace) {
        return lab && lab->CreateProfile(std::move(name), std::move(workspace));
      }, weak);
  callbacks.preview_import =
      base::BindRepeating(&WorkspaceLab::PreviewBrowserImport, weak);
  callbacks.invalidate_import =
      base::BindRepeating(&WorkspaceLab::InvalidateBrowserImport, weak);
  callbacks.dismiss_import =
      base::BindRepeating(&WorkspaceLab::DismissBrowserImport, weak);
  callbacks.apply_import = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab) { return lab && lab->ApplyBrowserImport(); },
      weak);
  callbacks.commands = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, std::u16string query) {
        return lab ? lab->WorkspaceCommandResults(std::move(query))
                   : std::vector<prototype::PaletteResult>();
      }, weak);
  callbacks.activate_result =
      base::BindRepeating(&WorkspaceLab::ActivateWorkspaceCommand, weak);
  callbacks.navigate = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, const GURL& url, bool new_tab) {
        return lab && lab->Navigate(url, new_tab);
      }, weak);
  callbacks.search = base::BindRepeating(&WorkspaceLab::SearchAddress, weak);
  if (timeline_) {
    callbacks.timeline = base::BindRepeating(
        [](base::WeakPtr<WorkspaceLab> lab) -> const sidebar::Timeline* {
          return lab && lab->timeline_settings_ready_
                     ? &*lab->timeline_->current
                     : nullptr;
        }, weak);
    callbacks.timeline_now = base::BindRepeating([] { return base::Time::Now(); });
    callbacks.record_timeline =
        base::BindRepeating(&WorkspaceLab::RecordTimeline, weak, false);
    callbacks.restore_timeline = base::BindRepeating(
        [](base::WeakPtr<WorkspaceLab> lab, int64_t time_ms, int64_t quantum_ms) {
          return lab && lab->RestoreTimeline(time_ms, quantum_ms);
        }, weak);
    callbacks.timeline_settings =
        base::BindRepeating(&WorkspaceLab::SetTimelineSettings, weak);
    callbacks.clear_timeline = base::BindRepeating(
        [](base::WeakPtr<WorkspaceLab> lab) {
          return lab && lab->ClearTimeline();
        }, weak);
  }
  callbacks.passwords = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab) { if (lab) lab->OpenPasswords(); }, weak);
  callbacks.downloads = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab) { if (lab) lab->OpenDownloads(); }, weak);
  callbacks.read_preferences = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab) {
        return lab ? lab->ReadPreferences() : BrowserPreferences();
      }, weak);
  callbacks.write_preference = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, std::string key, int value) {
        return lab && lab->WritePreference(std::move(key), value);
      }, weak);
  if (CanConfigureAssistant()) {
    callbacks.read_assistant = base::BindRepeating(
        [](base::WeakPtr<WorkspaceLab> lab) {
          return lab ? lab->ReadAssistantSettings() : base::DictValue();
        }, weak);
    callbacks.write_assistant =
        base::BindRepeating(&WorkspaceLab::WriteAssistantSettings, weak);
  }
  callbacks.clean_address_copies = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab) {
        return !lab || lab->CleanAddressCopies();
      }, weak);
  callbacks.read_ad_blocking = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab) {
        return lab ? lab->ReadAdBlocking() : base::DictValue();
      }, weak);
  callbacks.write_ad_blocking = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, base::DictValue command) {
        return lab ? lab->WriteAdBlocking(std::move(command)) : std::string();
      }, weak);
  callbacks.open_ad_blocking_rules =
      base::BindRepeating(&WorkspaceLab::OpenAdBlockingRules, weak);
  callbacks.clear_browsing_data =
      base::BindRepeating(&WorkspaceLab::RequestClearData, weak);
  callbacks.run_shortcut = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, std::string id) {
        return lab && lab->RunShortcut(std::move(id));
      }, weak);
  callbacks.page_command = base::BindRepeating(&WorkspaceLab::RunPageCommand, weak);
  callbacks.select_tab = base::BindRepeating(&WorkspaceLab::SelectTab, weak);
  callbacks.open_pin = base::BindRepeating(&WorkspaceLab::OpenPin, weak);
  callbacks.pin_tab = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, size_t index, std::string parent,
         std::string before) {
        return lab && lab->PinTab(index, std::move(parent), std::move(before));
      }, weak);
  callbacks.unpin = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, std::string id, size_t before) {
        return lab && lab->Unpin(std::move(id), before);
      }, weak);
  callbacks.move_tab = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, size_t index, size_t before) {
        return lab && lab->MoveTab(index, before);
      }, weak);
  callbacks.move_pin = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, std::string id, std::string parent,
         std::string before) {
        return lab && lab->MovePin(std::move(id), std::move(parent), std::move(before));
      }, weak);
  callbacks.update_pinned_url = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, std::string workspace, std::string id, GURL url) {
        return lab && lab->UpdatePinnedUrl(std::move(workspace), std::move(id), std::move(url));
      }, weak);
  callbacks.reset_pinned_url = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, std::string id) {
        return lab && lab->ResetPinnedUrl(std::move(id));
      }, weak);
  callbacks.create_folder = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, std::string workspace, std::string parent,
         std::string title) {
        return lab && lab->CreatePinnedFolder(std::move(workspace), std::move(parent),
                                               std::move(title));
      }, weak);
  if (!private_owner_ && !smoke_ && !session_path_.empty()) {
    callbacks.github_live_folders_only = true;
    callbacks.configure_live_folder = base::BindRepeating(
        [](base::WeakPtr<WorkspaceLab> lab, std::string workspace, std::string folder,
           std::string title, prototype::LiveFolderConfig config,
           std::string token, bool remove_token, bool) {
          return lab && config.provider == LiveFolderProvider::kGitHub &&
              lab->ConfigureGitHubFolder(
              std::move(workspace), std::move(folder), std::move(title),
              std::move(config), std::move(token), remove_token);
        }, weak);
    callbacks.refresh_live_folder =
        base::BindRepeating(&WorkspaceLab::RefreshGitHubFolder, weak);
  }
  callbacks.rename_sidebar_tab = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, std::optional<size_t> index,
         std::string id, std::u16string title) {
        if (!lab || !lab->CanEditWorkspace() || title.size() > 256)
          return false;
        if (!id.empty()) {
          auto& pins = lab->PinStorage(lab->active_index_, id);
          const auto pin = std::ranges::find(pins, id, &prototype::PinnedItem::id);
          if (pin == pins.end() || pin->folder || IsGitHubFolder(pins, pin->parent_id))
            return false;
          pin->custom_title = base::UTF16ToUTF8(title);
        } else {
          auto& tabs = lab->tabs_[lab->active_index_];
          if (!index || *index >= tabs.size())
            return false;
          tabs[*index].custom_title = std::move(title);
        }
        lab->RefreshShell();
        lab->PersistWorkspaceChanges();
        return true;
      }, weak);
  callbacks.rename_folder = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, std::string workspace, std::string id,
         std::string title) {
        return lab && lab->RenamePinnedFolder(std::move(workspace), std::move(id),
                                               std::move(title));
      }, weak);
  callbacks.delete_folder = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, std::string workspace, std::string id,
         bool contents) {
        return lab && lab->RemovePinnedFolder(std::move(workspace), std::move(id), contents);
      }, weak);
  callbacks.close_tab = base::BindRepeating(&WorkspaceLab::CloseTab, weak);
  callbacks.tab_muted = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, size_t index) {
        return lab && lab->TabMuted(index);
      }, weak);
  callbacks.set_tab_muted = base::BindRepeating(&WorkspaceLab::SetTabMuted, weak);
  callbacks.duplicate_tab = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, size_t index) {
        return lab && lab->DuplicateTab(index);
      }, weak);
  callbacks.duplicate_pin = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, std::string id) {
        return lab && lab->DuplicatePin(std::move(id));
      }, weak);
  callbacks.move_tab_to_workspace = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, size_t index, std::string target) {
        return lab && lab->MoveSidebarTab(index, std::move(target));
      }, weak);
  callbacks.split_tab = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, size_t index) {
        return lab && lab->SplitTab(index);
      }, weak);
  callbacks.split_pin = base::BindRepeating(&WorkspaceLab::SplitPin, weak);
  callbacks.leave_split = base::BindRepeating(&WorkspaceLab::LeaveSplit, weak);
  callbacks.close_split_tabs = base::BindRepeating(&WorkspaceLab::CloseSplitTabs, weak);
  callbacks.move_split_pane = base::BindRepeating(&WorkspaceLab::MoveSplitPane, weak);
  callbacks.unsplit = base::BindRepeating(&WorkspaceLab::ClearSplit, weak);
  callbacks.focus_pane = base::BindRepeating(&WorkspaceLab::FocusPane, weak);
  callbacks.split_ratio = base::BindRepeating(&WorkspaceLab::ResizeSplit, weak);
  callbacks.split_vertical = base::BindRepeating(&WorkspaceLab::SetSplitVertical, weak);
  callbacks.tab_menu_url = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, std::optional<size_t> tab,
         std::string_view pin) {
        return lab ? lab->TabUrl(tab, pin) : GURL();
      }, weak);
  callbacks.operation_pending = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab) {
        return lab && (lab->before_unload_batch_.pending() ||
                       lab->download_close_prompt_pending_ ||
                       lab->saving_session_ || lab->saving_catalogue_ || lab->clearing_profile_ ||
                       lab->replacing_timeline_);
      },
      weak);
  callbacks.site_panel = base::BindRepeating(&WorkspaceLab::OpenSitePanel, weak);
  callbacks.peek = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, const GURL& url, std::string source) {
        return lab && lab->OpenPeek(url, std::move(source));
      }, weak);
  callbacks.promote_peek = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, bool split) {
        return lab && lab->PromotePeek(split);
      }, weak);
  callbacks.close_peek = base::BindRepeating(&WorkspaceLab::ClosePeek, weak);
  callbacks.open_link = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, const GURL& url, std::string source,
         bool new_tab, bool split) {
        return lab && lab->OpenLink(url, std::move(source), new_tab, split);
      }, weak);
  callbacks.set_link_rule = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, const GURL& url, std::string workspace) {
        return lab && lab->SetLinkRule(url, std::move(workspace));
      }, weak);
  callbacks.remove_workspace = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, std::string id) {
        return lab && lab->RemoveWorkspace(std::move(id));
      }, weak);
  callbacks.restore_workspace = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, std::string id) {
        return lab && lab->RestoreWorkspace(std::move(id));
      }, weak);
  SetUnavailableActions(callbacks);
  shell_ = std::make_unique<prototype::WorkspaceShell>(settings_path,
                                                       std::move(callbacks));
  delegate_ = std::make_unique<views::WidgetDelegate>();
  delegate_->SetTitle(private_owner_ ? prototype::T(u"Zitter — tryb prywatny")
                                    : u"Zitter Content Workspace");
  delegate_->SetHasWindowSizeControls(true);
  delegate_->SetContentsView(shell_->Build());
  prototype::FingerprintToolbar::Callbacks fingerprint;
  fingerprint.palette = base::BindRepeating([](WorkspaceLab* lab) {
    return sidebar::design_palette(
        lab->widget_->GetColorMode() == ui::ColorProviderKey::ColorMode::kDark,
        lab->shell_->active_workspace_color());
  }, base::Unretained(this));
  fingerprint.enabled = base::BindRepeating([](WorkspaceLab* lab) {
    return lab->WorkspaceProfile(lab->active_index_)->GetFingerprintProtection()
        .value_or(false);
  }, base::Unretained(this));
  fingerprint.site_code = base::BindRepeating([](WorkspaceLab* lab) {
    auto& profile = *lab->WorkspaceProfile(lab->active_index_);
    auto* page = lab->web_contents();
    return page && profile.GetFingerprintProtection().value_or(false)
               ? prototype::FingerprintSession::For(profile).SiteCode(
                     page->GetLastCommittedURL())
               : std::u16string();
  }, base::Unretained(this));
  fingerprint.before_open = base::BindRepeating([](WorkspaceLab* lab) {
    lab->CloseAdblockShield();
    lab->shell_->CloseOverlays(true);
  }, base::Unretained(this));
  fingerprint.rotate = base::BindRepeating([](WorkspaceLab* lab) {
    auto& profile = *lab->WorkspaceProfile(lab->active_index_);
    if (profile.GetFingerprintProtection().value_or(false))
      prototype::FingerprintSession::For(profile).Rotate();
  }, base::Unretained(this));
  fingerprint.toggle_protection = base::BindRepeating([](WorkspaceLab* lab) {
    auto& profile = *lab->WorkspaceProfile(lab->active_index_);
    const bool enable = !profile.GetFingerprintProtection().value_or(false);
    lab->WritePreference("fingerprint_protection", enable);
  }, base::Unretained(this));
  fingerprint.open_settings = base::BindRepeating([](WorkspaceLab* lab) {
    lab->shell_->OpenPrivacySettings();
  }, base::Unretained(this));
  auto trailing = std::make_unique<views::View>();
  trailing->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 2));
  assistant_toolbar_button_ = trailing->AddChildView(std::make_unique<ContentToolbarButton>(
      prototype::kAssistantSparkIcon, prototype::T(u"Asystent"),
      base::BindRepeating([](base::WeakPtr<WorkspaceLab> lab) {
        if (lab && !lab->ShowAssistant(false))
          lab->shell_->OpenAssistantSettings();
      }, weak), fingerprint.palette));
  assistant_toolbar_button_->SetVisible(false);
  adblock_shield_button_ = trailing->AddChildView(
      std::make_unique<prototype::ShieldButton>(
          base::BindRepeating(&WorkspaceLab::ToggleAdblockShield, weak),
          fingerprint.palette));
  adblock_shield_button_->SetVisible(false);
  auto palette = fingerprint.palette;
  fingerprint_toolbar_ = std::make_unique<prototype::FingerprintToolbar>(
      std::move(fingerprint));
  fingerprint_button_ = trailing->AddChildView(
      fingerprint_toolbar_->CreateButton(/*private_mode=*/false));
  fingerprint_button_->SetVisible(false);
  if (!private_owner_ && BundledNightShiftSettingsPage(work).is_valid()) {
    night_shift_button_ = trailing->AddChildView(std::make_unique<ContentToolbarButton>(
        kNightShiftMoon, u"Night Shift",
        base::BindRepeating([](base::WeakPtr<WorkspaceLab> lab) {
          if (lab)
            lab->OpenNightShiftSettings(nullptr, true);
        }, weak), palette, kNightShiftSpark));
  }
  if (IsManagedExtensionSession() && !private_owner_) {
    pinned_extension_toolbar_ = trailing->AddChildView(std::make_unique<views::View>());
    pinned_extension_toolbar_->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 2));
    pinned_extension_toolbar_->SetVisible(false);
    extension_toolbar_divider_ = trailing->AddChildView(
        std::make_unique<ContentToolbarDivider>(palette));
    extension_toolbar_divider_->SetVisible(false);
    extensions_toolbar_button_ = trailing->AddChildView(std::make_unique<ContentToolbarButton>(
        kExtensionsPuzzle, prototype::T(u"Rozszerzenia"),
        base::BindRepeating(&WorkspaceLab::ShowExtensionsToolbarMenu, weak), palette));
  }
  if (private_owner_) {
    private_fingerprint_subscription_ = work.ObserveFingerprintProtection(
        base::BindRepeating(&WorkspaceLab::UpdateFingerprintToolbar, weak));
  }
  shell_->SetHeaderTrailing(std::move(trailing));
  delegate_->SetClientViewFactory(
      prototype::WorkspaceShell::ClientViewFactory());

  views::Widget::InitParams params(views::Widget::InitParams::CLIENT_OWNS_WIDGET,
                                   views::Widget::InitParams::TYPE_WINDOW);
  params.bounds = gfx::Rect(80, 80, 1100, 740);
  params.delegate = delegate_.get();
  const auto permission_probe = base::CommandLine::ForCurrentProcess()
      ->GetSwitchValueASCII(kSitePermissionProbe);
  params.accept_events =
      !smoke_ && (!session_probe_url_.is_valid() ||
                  permission_probe.starts_with("prompt-") ||
                  permission_probe.starts_with("media-"));
  widget_ = std::make_unique<views::Widget>();
#if BUILDFLAG(IS_MAC)
  params.native_widget = prototype::CreateWorkspaceNativeWidget(widget_.get());
#elif BUILDFLAG(IS_WIN)
  prototype::ConfigureWorkspaceNativeWidget(widget_.get(), params, {});
#endif
  widget_->Init(std::move(params));
  fullscreen_observation_.Observe(widget_.get());
  media_access_ = std::make_unique<WorkspaceMediaAccess>(
      *widget_, base::BindRepeating([](base::WeakPtr<WorkspaceLab> lab) {
        return lab ? lab->web_contents() : nullptr;
      }, weak),
      base::BindRepeating([](base::WeakPtr<WorkspaceLab> lab) {
        std::vector<content::WebContents*> pages;
        if (lab) {
          for (const auto& tab : lab->tabs_[lab->active_index_])
            pages.push_back(tab.view ? tab.view->web_contents() : nullptr);
        }
        return pages;
      }, weak),
      base::BindRepeating([](base::WeakPtr<WorkspaceLab> lab, std::u16string text,
                             std::vector<std::u16string> choices,
                             size_t tab_count,
                             std::u16string audio_option,
                             base::OnceCallback<void(std::optional<size_t>, bool)> answer) {
        if (!lab) {
          std::move(answer).Run(std::nullopt, false);
          return;
        }
        const bool tab = !choices.empty();
        lab->ShowPermissionPrompt(
            tab ? prototype::T(u"Co udostępnić?")
                : prototype::T(u"Zezwolić na przechwytywanie?"),
            std::move(text),
            tab ? prototype::T(u"Udostępnij") : prototype::T(u"Zezwól raz"),
            tab ? PermissionPrompt::kTabCapture : PermissionPrompt::kDeviceCapture,
            std::move(choices), tab_count, std::move(audio_option), std::move(answer));
      }, weak));
  ArmCloseHandler();
  shell_->Attach(widget_.get(), this);
  if (private_owner_ || smoke_ ||
      base::CommandLine::ForCurrentProcess()->HasSwitch("zitter-content-workspace-theme")) {
    shell_->SetThemeMode(dark_ ? prototype::ThemeMode::kDark
                               : prototype::ThemeMode::kLight);
  }

  downloads_ = std::make_unique<prototype::DownloadPanel>();
  downloads_->SetStartedCallback(base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, std::u16string name) {
        if (lab)
          lab->shell_->DownloadStarted(std::move(name));
      }, weak));
  downloads_->SetProgressCallback(base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, int active, double fraction,
         std::u16string newest) {
        if (lab)
          lab->shell_->SetDownloadProgress(active, fraction, std::move(newest));
      }, weak));
  downloads_->SetCompletedCallback(
      base::BindRepeating(&WorkspaceLab::DownloadCompleted, weak));
  downloads_->SetRemovedCallback(base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, int count, base::OnceClosure undo) {
        if (lab) {
          lab->shell_->ShowToast(prototype::T(u"Usunięto z listy: "),
                                base::NumberToString16(count), std::move(undo));
        }
      }, weak));
  downloads_->SetConfirmCallback(base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, views::Widget* parent,
         std::u16string title, std::u16string detail, std::u16string warning,
         std::u16string action, base::OnceCallback<void(bool)> answer) {
        if (lab) {
          lab->shell_->ConfirmDanger(parent, std::move(title), std::move(detail),
                                     std::move(warning), std::move(action),
                                     std::move(answer));
        }
      }, weak));

  downloads_->SetHistoryOnlyPolicy(base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, const download::DownloadItem& item) {
        if (!lab)
          return true;
        auto* delegate = static_cast<ProfileDownloadDelegate*>(
            lab->WorkspaceProfile(lab->active_index_)->GetDownloadManagerDelegate());
        return delegate->IsImportedHistoryEntry(item);
      }, weak));
  downloads_->SetFileDeletionPolicy(
      base::BindRepeating(
          [](base::WeakPtr<WorkspaceLab> lab, const download::DownloadItem& item) {
            if (!lab)
              return false;
            const auto* delegate = static_cast<ProfileDownloadDelegate*>(
                lab->WorkspaceProfile(lab->active_index_)->GetDownloadManagerDelegate());
            return delegate->CanDeleteDownloadFile(item);
          },
          weak),
      base::BindRepeating(
          [](base::WeakPtr<WorkspaceLab> lab, const download::DownloadItem& item,
             base::OnceCallback<void(bool)> done) {
            if (!lab) {
              std::move(done).Run(false);
              return;
            }
            auto* delegate = static_cast<ProfileDownloadDelegate*>(
                lab->WorkspaceProfile(lab->active_index_)->GetDownloadManagerDelegate());
            ++lab->pending_download_deletions_;
            delegate->DeleteDownloadFile(
                item, base::BindOnce(
                    [](base::WeakPtr<WorkspaceLab> lab,
                       base::OnceCallback<void(bool)> done, bool deleted) {
                      std::move(done).Run(deleted);
                      if (!lab)
                        return;
                      --lab->pending_download_deletions_;
                      if (lab->download_delete_probe_ != DownloadDeleteProbe::kNone) {
                        lab->FinishDownloadDeleteProbe(deleted);
                      } else if (!deleted) {
                        lab->shell_->ShowToast(
                            prototype::T(u"Nie udało się usunąć pobranego pliku."),
                            {}, {});
                      }
                      if (lab->close_after_download_deletions_ &&
                          lab->pending_download_deletions_ == 0) {
                        lab->close_after_download_deletions_ = false;
                        lab->RequestClose();
                      }
                    },
                    lab, std::move(done)));
            if (lab->download_delete_probe_ == DownloadDeleteProbe::kConfirm)
              lab->SelectWorkspace(std::string(kWorkspaceIds[1]));
          },
          weak));

  if (const auto* removed = session.FindDict("removed_workspaces"))
    removed_workspaces_ = removed->Clone();
  if (const auto* epochs = session.FindDict("timeline_epochs"))
    timeline_epochs_ = epochs->Clone();
  if (const auto* rules = session.FindDict("link_rules"))
    link_rules_ = rules->Clone();
  shift_click_peek_ = private_owner_ ? private_owner_->shift_click_peek_
                                     : session.FindBool("shift_click_peek").value_or(true);
  clean_address_copies_ = session.FindBool("clean_address_copies").value_or(true);
  if (!private_owner_) {
    if (const auto* settings = session.FindDict("assistant"))
      assistant_settings_ = prototype::AssistantSettings::FromValue(settings);
    night_shift_config_ =
        prototype::NightShiftConfig::FromValue(session.FindDict("night_shift"));
    if (night_shift_config_.managed) {
      for (size_t i = 0; i < contexts_.size(); ++i) {
        ApplyBundledNightShift(*contexts_[i],
                               night_shift_config_.EnabledFor(profiles_[i].id),
                               night_shift_config_);
      }
    }
  }
  UpdateAssistantHostSettings();
  ProbeAssistantProviders();
  auto workspaces = ReadContentWorkspaces(session);
  CHECK(workspaces);
  auto favorites = ReadContentProfileFavorites(session, *workspaces);
  CHECK(favorites);
  profile_favorites_ = std::move(*favorites);
  if (private_owner_)
    workspaces->resize(1);
  for (auto& workspace : *workspaces) {
    const auto* entry = session.FindDict(workspace.id);
    AppendWorkspace(std::move(workspace), entry);
  }
  if (const auto* active = session.FindString("active_workspace"))
    restored_workspace_ = WorkspaceIndex(*active).value();
}

std::optional<size_t> WorkspaceLab::WorkspaceIndex(std::string_view id) const {
  const auto& workspaces = model_.workspaces();
  const auto match = std::ranges::find(workspaces, id, &Workspace::id);
  if (match == workspaces.end())
    return std::nullopt;
  return match - workspaces.begin();
}

size_t WorkspaceLab::WorkspaceProfileIndex(size_t workspace) const {
  // Building the shell can query the first profile before tabs are attached.
  if (model_.workspaces().empty()) {
    CHECK_EQ(workspace, 0u);
    return 0;
  }
  const auto& profile_id = model_.workspaces()[workspace].profile_id;
  const auto profile = std::ranges::find(profiles_, profile_id,
                                        &ContentProfileInfo::id);
  CHECK(profile != profiles_.end());
  return profile - profiles_.begin();
}

ProfileContext* WorkspaceLab::WorkspaceProfile(size_t workspace) const {
  return contexts_[WorkspaceProfileIndex(workspace)].get();
}

std::string_view WorkspaceLab::WorkspaceProfileName(size_t workspace) const {
  return profiles_[WorkspaceProfileIndex(workspace)].name;
}

void WorkspaceLab::AppendWorkspace(Workspace workspace, const base::DictValue* entry) {
  const size_t index = tabs_.size();
  const auto profile_id = workspace.profile_id;
  CHECK(model_.Add(std::move(workspace)));
  tabs_.emplace_back();
  pins_.emplace_back();
  closed_tabs_.emplace_back();
  splits_.emplace_back();
  selected_.push_back(0);
  sleeping_.push_back(false);
  if (!entry) {
    AddTab(index);
    return;
  }
  auto restored = prototype::ReadWorkspaceSessionTabs(*entry, profile_id);
  auto pins = ReadWorkspacePins(*entry);
  CHECK(restored && pins);
  std::erase_if(*pins, [](const auto& pin) { return pin.profile_pin; });
  pins_[index] = std::move(*pins);
  for (auto& tab : *restored) {
    AddTab(index, std::move(tab.url),
           std::move(tab.title).value_or(std::u16string()),
           std::move(tab.pin).value_or(std::string()));
    tabs_[index].back().custom_title = std::move(tab.custom_title).value_or(std::u16string());
  }
  selected_[index] = static_cast<size_t>(*entry->FindInt("active"));
  sleeping_[index] = entry->FindBool("sleeping").value_or(false);
  auto split = ReadWorkspaceSplit(*entry, restored->size());
  CHECK(split);
  splits_[index] = std::move(*split);
}

bool WorkspaceLab::AddWorkspace(std::string name, std::string profile_id,
                                base::OnceCallback<void(std::string)> committed) {
  base::TrimWhitespaceASCII(name, base::TRIM_ALL, &name);
  if (smoke_ || private_owner_ || !CanEditWorkspace() || !model_.active() ||
      tabs_.size() >= kMaxContentWorkspaces || !IsContentWorkspaceName(name) ||
      !std::ranges::contains(profiles_, profile_id, &ContentProfileInfo::id)) {
    if (committed)
      std::move(committed).Run({});
    return false;
  }
  Workspace workspace{
      "space-" + base::Uuid::GenerateRandomV4().AsLowercaseString(),
      std::move(profile_id), std::move(name)};
  if (session_path_.empty()) {
    const auto id = workspace.id;
    AppendWorkspace(std::move(workspace), nullptr);
    RefreshShell();
    if (committed)
      std::move(committed).Run(id);
    return true;
  }

  auto session = CaptureSession();
  auto appearance = shell_->WorkspaceDraftAppearance();
  session.EnsureList("extra_workspaces")->Append(
      base::DictValue().Set("id", workspace.id).Set("name", workspace.name));
  session.Set(workspace.id,
      base::DictValue().Set("profile_id", workspace.profile_id).Set("active", 0)
          .Set("tabs", base::ListValue().Append(
              prototype::WriteWorkspaceSessionTab(GURL(), u""))));
  saving_catalogue_ = true;
  RefreshShell();
  session_writer_->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce(&WriteContentWorkspaceSession, session_path_,
                                std::move(session)),
      base::BindOnce([](base::WeakPtr<WorkspaceLab> lab, Workspace workspace,
                        std::pair<SkColor, std::u16string> appearance,
                        base::OnceCallback<void(std::string)> committed, bool ok) {
        if (!lab)
          return;
        lab->saving_catalogue_ = false;
        const auto id = workspace.id;
        if (ok)
          lab->AppendWorkspace(std::move(workspace), nullptr);
        lab->RefreshShell();
        lab->shell_->SetConfigurationError(!ok);
        if (ok) {
          lab->shell_->SetWorkspaceColor(id, appearance.first);
          if (!appearance.second.empty())
            lab->shell_->SetWorkspaceEmoji(id, std::move(appearance.second));
        }
        if (committed)
          std::move(committed).Run(ok ? id : std::string());
        if (lab && std::exchange(lab->close_after_catalogue_save_, false))
          lab->RequestClose();
      }, weak_factory_.GetWeakPtr(), std::move(workspace), std::move(appearance),
      std::move(committed)));
  return true;
}

bool WorkspaceLab::CreateProfile(std::string name, std::string workspace_id) {
  using Event = prototype::WorkspaceShell::ProfileStatusEvent;
  base::TrimWhitespaceASCII(name, base::TRIM_ALL, &name);
  if (smoke_ || private_owner_ || !os_crypt_ || session_path_.empty() ||
      !workspace_id.empty() || !CanEditWorkspace() || !model_.active() ||
      profiles_.size() >= kMaxContentProfiles || !IsContentWorkspaceName(name)) {
    return false;
  }
  if (std::ranges::any_of(profiles_, [&](const auto& profile) {
        return base::EqualsCaseInsensitiveASCII(name, profile.name);
      })) {
    shell_->SetProfileStatus(Event::kDuplicateName);
    return false;
  }
  ContentProfileInfo profile{
      "profile-" + base::Uuid::GenerateRandomV4().AsLowercaseString(),
      std::move(name), /*separate_network=*/BUILDFLAG(IS_MAC)};
  auto session = CaptureSession();
  session.EnsureList("extra_profiles")->Append(WriteProfileMetadata(profile));
  saving_catalogue_ = true;
  RefreshShell();
  shell_->SetProfileStatus(Event::kCreating);
  session_writer_->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce(&ProvisionProfile, session_path_, profile,
                                std::move(session)),
      base::BindOnce([](base::WeakPtr<WorkspaceLab> lab,
                        ContentProfileInfo profile, bool ok) {
        if (!lab)
          return;
        lab->saving_catalogue_ = false;
        if (ok) {
          auto context = lab->OpenNewProfile(profile);
          lab->PublishProfile(std::move(profile), std::move(context));
        }
        lab->RefreshShell();
        lab->shell_->SetProfileStatus(ok ? Event::kCreated : Event::kCreateNotSaved);
        if (std::exchange(lab->close_after_catalogue_save_, false))
          lab->RequestClose();
      }, weak_factory_.GetWeakPtr(), profile));
  return true;
}

bool WorkspaceLab::RemoveWorkspace(std::string id) {
  const auto index = WorkspaceIndex(id);
  if (smoke_ || private_owner_ || !CanEditWorkspace() || !index ||
      model_.workspaces().size() <= 1) {
    return false;
  }
  if (WorkspaceHasCapture(*index)) {
    shell_->SetOperationStatus(prototype::WorkspaceShell::OperationStatusEvent::kStopCapture);
    return false;
  }
  const auto pages = WorkspacePages(*index);
  if (std::ranges::none_of(pages, [](auto* page) {
        return page->NeedToFireBeforeUnload();
      })) {
    CommitWorkspaceRemoval(std::move(id));
  } else {
    CheckBeforeUnload(
        pages, base::BindOnce(&WorkspaceLab::CommitWorkspaceRemoval,
                              weak_factory_.GetWeakPtr(), std::move(id)),
        base::BindOnce([](base::WeakPtr<WorkspaceLab> lab) {
          if (lab)
            lab->shell_->CancelPendingWorkspaceRemoval();
        }, weak_factory_.GetWeakPtr()));
  }
  return true;
}

void WorkspaceLab::CommitWorkspaceRemoval(std::string id) {
  const size_t index = WorkspaceIndex(id).value();
  if (WorkspaceHasCapture(index)) {
    shell_->CancelPendingWorkspaceRemoval();
    shell_->SetOperationStatus(prototype::WorkspaceShell::OperationStatusEvent::kStopCapture);
    return;
  }
  const auto& workspace = model_.workspaces()[index];
  const auto source = imported_workspace_sources_.find(id);
  RemovedWorkspace removed{workspace, CaptureWorkspaceSession(index),
                           source == imported_workspace_sources_.end()
                               ? std::string() : source->second,
                           index == active_index_, {}};
  auto session = CaptureSession();
  if (auto* rules = session.FindDict("link_rules")) {
    for (const auto [host, target] : link_rules_) {
      if (target.GetString() == id) {
        removed.link_rules.Set(host, target.Clone());
        rules->Remove(host);
      }
    }
    if (rules->empty())
      session.Remove("link_rules");
  }
  const auto fallback = removed.was_active
                            ? model_.workspaces()[index == 0 ? 1 : 0].id
                            : model_.active()->id;
  if (removed.was_active) {
    session.Set("active_workspace", fallback);
    session.FindDict(fallback)->Remove("sleeping");
  }
  session.Remove(id);
  session.EnsureDict("removed_workspaces")->Set(id, workspace.profile_id);
  if (auto* names = session.FindDict("workspace_names")) {
    names->Remove(id);
    if (names->empty())
      session.Remove("workspace_names");
  }
  if (auto* extras = session.FindList("extra_workspaces")) {
    extras->EraseIf([&](const base::Value& entry) {
      return *entry.GetDict().FindString("id") == id;
    });
    if (extras->empty())
      session.Remove("extra_workspaces");
  }
  SaveCatalogueChange(std::move(session), base::BindOnce(
      [](base::WeakPtr<WorkspaceLab> lab, size_t index, std::string fallback,
         RemovedWorkspace removed, bool ok) {
        if (!lab)
          return;
        if (!ok) {
          lab->shell_->CancelPendingWorkspaceRemoval();
          lab->RefreshShell();
          lab->shell_->SetConfigurationError(true);
          return;
        }
        const auto& id = removed.workspace.id;
        if (lab->peek_workspace_id_ == id)
          lab->DiscardPeek();
        if (removed.was_active) {
          CHECK(lab->controller_.Activate(fallback));
        } else {
          lab->address_history_tasks_.TryCancelAll();
          lab->shell_->CloseOverlays(true);
          lab->ad_blocking_rules_panel_.reset();
          lab->history_panel_.reset();
          lab->passwords_panel_.reset();
        }
        {
          base::AutoReset<bool> updating(&lab->updating_panes_, true);
          for (auto& tab : lab->tabs_[index]) {
            HideTabView(*tab.view);
            if (tab.view->parent())
              tab.detached_view = lab->shell_->content_host()->RemoveChildViewT(tab.view.get());
          }
          const auto erase = [index](auto& list) { list.erase(list.begin() + index); };
          erase(lab->tabs_);
          erase(lab->closed_tabs_);
          erase(lab->pins_);
          erase(lab->splits_);
          erase(lab->selected_);
          erase(lab->sleeping_);
          CHECK(lab->model_.Remove(id));
          lab->active_index_ = lab->WorkspaceIndex(fallback).value();
        }
        lab->imported_workspace_sources_.erase(id);
        lab->removed_workspaces_.Set(id, removed.workspace.profile_id);
        for (const auto [host, target] : removed.link_rules)
          lab->link_rules_.Remove(host);
        lab->removed_workspace_ = std::move(removed);
        lab->RefreshShell();
        lab->tabs_[lab->active_index_][lab->selected_[lab->active_index_]].view->RequestFocus();
      }, weak_factory_.GetWeakPtr(), index, fallback, std::move(removed)));
}

bool WorkspaceLab::RestoreWorkspace(std::string id) {
  if (!CanEditWorkspace() || !removed_workspace_ ||
      removed_workspace_->workspace.id != id ||
      model_.workspaces().size() >= kMaxContentWorkspaces) {
    return false;
  }
  const auto& removed = *removed_workspace_;
  if (!removed.import_source.empty() &&
      std::ranges::any_of(imported_workspace_sources_, [&](const auto& source) {
        return source.second == removed.import_source;
      })) {
    return false;
  }
  auto session = CaptureSession();
  auto* tombstones = session.FindDict("removed_workspaces");
  CHECK(tombstones && tombstones->Remove(id));
  if (tombstones->empty())
    session.Remove("removed_workspaces");
  session.Set(id, removed.session.Clone());
  auto rules = link_rules_.Clone();
  for (const auto [host, target] : removed.link_rules) {
    if (!rules.contains(host))
      rules.Set(host, target.Clone());
  }
  if (!rules.empty())
    session.Set("link_rules", rules.Clone());
  if (removed.was_active) {
    session.Set("active_workspace", id);
    session.FindDict(id)->Remove("sleeping");
  }
  if (std::ranges::contains(kWorkspaceIds, id)) {
    SetCatalogueName(session, id, removed.workspace.name,
                     "workspace_names", "extra_workspaces");
  } else {
    AppendWorkspaceMetadata(session, removed.workspace, removed.import_source);
  }
  SaveCatalogueChange(std::move(session), base::BindOnce(
      [](base::WeakPtr<WorkspaceLab> lab, std::string id, base::DictValue rules, bool ok) {
        if (!lab)
          return;
        if (ok) {
          auto removed = std::move(*lab->removed_workspace_);
          lab->removed_workspace_.reset();
          lab->removed_workspaces_.Remove(id);
          lab->link_rules_ = std::move(rules);
          if (!removed.import_source.empty())
            lab->imported_workspace_sources_.emplace(id, std::move(removed.import_source));
          lab->AppendWorkspace(std::move(removed.workspace), &removed.session);
          if (removed.was_active) {
            CHECK(lab->controller_.Activate(id));
            lab->tabs_[lab->active_index_][lab->selected_[lab->active_index_]].view->RequestFocus();
          }
        }
        lab->RefreshShell();
        lab->shell_->SetConfigurationError(!ok);
      }, weak_factory_.GetWeakPtr(), std::move(id), std::move(rules)));
  return true;
}

bool WorkspaceLab::UpdateWorkspace(std::string id, std::string name,
                                   std::string profile_id) {
  base::TrimWhitespaceASCII(name, base::TRIM_ALL, &name);
  const auto index = WorkspaceIndex(id);
  if (smoke_ || private_owner_ || !CanEditWorkspace() || !index ||
      !IsContentWorkspaceName(name) ||
      !std::ranges::contains(profiles_, profile_id, &ContentProfileInfo::id)) {
    return false;
  }
  const auto& workspace = model_.workspaces()[*index];
  if (profile_id == workspace.profile_id && name == workspace.name)
    return true;
  if (profile_id != workspace.profile_id) {
    if (WorkspaceHasCapture(*index)) {
      shell_->SetOperationStatus(prototype::WorkspaceShell::OperationStatusEvent::kStopCapture);
      return false;
    }
    const auto pages = WorkspacePages(*index);
    if (std::ranges::any_of(pages, [](auto* page) {
          return page->NeedToFireBeforeUnload();
        })) {
      CheckBeforeUnload(pages, base::BindOnce(&WorkspaceLab::SaveWorkspaceUpdate,
          weak_factory_.GetWeakPtr(), std::move(id), std::move(name),
          std::move(profile_id)));
      return true;
    }
  }
  SaveWorkspaceUpdate(std::move(id), std::move(name), std::move(profile_id));
  return true;
}

void WorkspaceLab::SaveWorkspaceUpdate(std::string id, std::string name,
                                       std::string profile_id) {
  const size_t index = WorkspaceIndex(id).value();
  const bool reassign = profile_id != model_.workspaces()[index].profile_id;
  if (reassign && WorkspaceHasCapture(index)) {
    shell_->SetOperationStatus(prototype::WorkspaceShell::OperationStatusEvent::kStopCapture);
    return;
  }

  auto session = CaptureSession();
  SetCatalogueName(session, id, name, "workspace_names", "extra_workspaces");
  std::string epoch;
  if (reassign) {
    epoch = base::Uuid::GenerateRandomV4().AsLowercaseString();
    session.EnsureDict("timeline_epochs")->Set(id, epoch);
    auto* entry = session.FindDict(id);
    entry->Set("profile_id", profile_id);
    entry->Set("active", 0);
    entry->Set("tabs", base::ListValue().Append(
        prototype::WriteWorkspaceSessionTab(GURL(), u"")));
    entry->Remove("split");
    entry->Remove("sleeping");
  }
  SaveCatalogueChange(std::move(session), base::BindOnce(
      [](base::WeakPtr<WorkspaceLab> lab, size_t index, std::string id,
         std::string name, std::string profile, std::string epoch, bool ok) {
        if (!lab)
          return;
        if (ok) {
          CHECK(lab->model_.Update(id, std::move(name), profile));
          if (!epoch.empty())
            lab->ReplaceWorkspaceProfile(index, epoch);
        }
        lab->RefreshShell();
        lab->shell_->ReportWorkspaceNameSaved(ok);
      }, weak_factory_.GetWeakPtr(), index, std::move(id), std::move(name),
      std::move(profile_id), std::move(epoch)));
}

void WorkspaceLab::ReplaceWorkspaceProfile(size_t index,
                                           const std::string& epoch) {
  base::AutoReset<bool> updating(&updating_panes_, true);
  const auto& id = model_.workspaces()[index].id;
  if (peek_workspace_id_ == id)
    DiscardPeek();
  const bool active = index == active_index_;
  // A global timeline Undo can own a copy of the previous profile's data.
  shell_->HideToast();
  if (active) {
    shell_->CloseOverlays(true);
    fingerprint_toolbar_->Close();
    CloseAdblockShield();
    CancelPermissionPrompt();
    media_access_->Stop();
    address_history_tasks_.TryCancelAll();
    shell_->ResetDownloadPresentation();
    ad_blocking_rules_panel_.reset();
    history_panel_.reset();
    passwords_panel_.reset();
    devtools_.reset();
    Observe(nullptr);
    shell_->SetPanes({}, {});
  }
  for (auto& tab : tabs_[index]) {
    HideTabView(*tab.view);
    if (tab.view->parent())
      tab.detached_view = shell_->content_host()->RemoveChildViewT(tab.view.get());
  }
  tabs_[index].clear();
  closed_tabs_[index].clear();
  splits_[index] = {};
  selected_[index] = 0;
  sleeping_[index] = false;
  timeline_epochs_.Set(id, epoch);
  if (timeline_) {
    timeline_->current->timeline_forget_workspace(
        id, 0, base::Time::Max().InMillisecondsSinceUnixEpoch());
    timeline_dirty_ = true;
    if (!session_path_.empty())
      PersistTimeline();
  }
  AddTab(index);
  if (active) {
    ShowTab();
    auto* profile = WorkspaceProfile(index);
    downloads_->SetSource(
        profile->GetDownloadManager(), base::UTF8ToUTF16(WorkspaceProfileName(index)),
        {}, {}, {}, prototype::DownloadPanel::FileActions::kOpenRevealAndDelete);
    tabs_[index].front().view->RequestFocus();
  }
}

bool WorkspaceLab::RenameProfile(std::string id, std::string name) {
  using Event = prototype::WorkspaceShell::ProfileStatusEvent;
  base::TrimWhitespaceASCII(name, base::TRIM_ALL, &name);
  if (smoke_ || private_owner_ || !CanEditWorkspace() || !model_.active() ||
      !IsContentWorkspaceName(name)) {
    return false;
  }
  const auto profile = std::ranges::find(profiles_, id, &ContentProfileInfo::id);
  if (profile == profiles_.end())
    return false;
  if (std::ranges::any_of(profiles_, [&](const auto& other) {
        return other.id != id && base::EqualsCaseInsensitiveASCII(name, other.name);
      })) {
    shell_->SetProfileStatus(Event::kDuplicateName);
    return false;
  }
  if (profile->name == name)
    return true;
  auto session = CaptureSession();
  SetCatalogueName(session, id, name, "profile_names", "extra_profiles");
  SaveCatalogueChange(std::move(session), base::BindOnce(
      [](base::WeakPtr<WorkspaceLab> lab, size_t index, std::string name, bool ok) {
        if (!lab)
          return;
        if (ok) {
          lab->profiles_[index].name = std::move(name);
          if (lab->WorkspaceProfileIndex(lab->active_index_) == index) {
            auto* profile = lab->contexts_[index].get();
            lab->downloads_->SetSource(
                profile->GetDownloadManager(),
                base::UTF8ToUTF16(lab->profiles_[index].name), {}, {}, {},
                prototype::DownloadPanel::FileActions::kOpenRevealAndDelete);
          }
        }
        lab->RefreshShell();
        lab->shell_->SetProfileStatus(ok ? Event::kRenamed : Event::kRenameFailed);
      }, weak_factory_.GetWeakPtr(), profile - profiles_.begin(), std::move(name)));
  return true;
}

void WorkspaceLab::SaveCatalogueChange(base::DictValue session,
                                       base::OnceCallback<void(bool)> done) {
  if (session_path_.empty()) {
    std::move(done).Run(true);
    return;
  }
  saving_catalogue_ = true;
  RefreshShell();
  session_writer_->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce(&WriteContentWorkspaceSession, session_path_,
                                std::move(session)),
      base::BindOnce([](base::WeakPtr<WorkspaceLab> lab,
                        base::OnceCallback<void(bool)> done, bool ok) {
        if (!lab)
          return;
        lab->saving_catalogue_ = false;
        std::move(done).Run(ok);
        if (std::exchange(lab->close_after_catalogue_save_, false))
          lab->RequestClose();
      }, weak_factory_.GetWeakPtr(), std::move(done)));
}

std::unique_ptr<ProfileContext> WorkspaceLab::OpenNewProfile(
    const ContentProfileInfo& profile) {
  const auto root = session_path_.DirName();
  const auto network = profile.separate_network
                           ? root.AppendASCII(kWorkspaceNetworkRoot).AppendASCII(profile.id)
                           : base::FilePath();
  auto context = std::make_unique<ProfileContext>(
      root.AppendASCII(profile.id), ProfileContext::Mode::kPersistent,
      std::nullopt, os_crypt_, /*password_saving_default=*/true, network);
  context->InitializeNewWorkspacePreferences();
  return context;
}

void WorkspaceLab::PublishProfile(ContentProfileInfo profile,
                                  std::unique_ptr<ProfileContext> context) {
  ConfigureProfileForWorkspace(*context);
  if (adblock_filter_lists_)
    context->SetAdblockFilterTexts(adblock_filter_lists_->texts(), {});
  if (night_shift_config_.managed) {
    ApplyBundledNightShift(*context, night_shift_config_.EnabledFor(profile.id),
                           night_shift_config_);
  }
  contexts_.push_back(context.get());
  runtime_profiles_.push_back(std::move(context));
  profiles_.push_back(std::move(profile));
}

void WorkspaceLab::ConfigureProfileForWorkspace(ProfileContext& profile) {
  if (!profile.IsOffTheRecord() && !user_download_directory_.empty())
    profile.EnableUserDownloads(user_download_directory_,
                                download_safe_browsing_key_);
  profile.site_permissions().SetClipboardReadPrompt(base::BindRepeating(
      &WorkspaceLab::ShowClipboardReadPrompt, weak_factory_.GetWeakPtr()));
}

bool WorkspaceLab::CanImportBrowser() const {
  return !smoke_ && !private_owner_ && os_crypt_ && !session_path_.empty();
}

void WorkspaceLab::InvalidateBrowserImport() {
  ++browser_import_generation_;
  browser_import_preview_.reset();
}

void WorkspaceLab::DismissBrowserImport() {
  InvalidateBrowserImport();
  if (std::exchange(startup_import_pending_, false)) {
    RefreshShell();
    PersistWorkspaceChanges();
  }
}

void WorkspaceLab::PreviewBrowserImport(prototype::BrowserImportOptions options) {
  using Report = prototype::WorkspaceShell::ImportStatusReport;
  if (!CanImportBrowser() || !CanEditWorkspace()) {
    shell_->SetImportStatus(prototype::T(u"Nie można teraz rozpocząć importu. Spróbuj ponownie."),
                            false);
    return;
  }
  InvalidateBrowserImport();
  const size_t generation = browser_import_generation_;
  shell_->BeginImportPreview(options);
  Report report;
  report.stage = Report::Stage::kReading;
  report.browser = prototype::ImportBrowserName(options.browser);
  shell_->SetImportStatus(report, false, true);
  const bool fixture = !startup_import_probe_.empty() ||
      (session_probe_url_.is_valid() &&
       base::CommandLine::ForCurrentProcess()->HasSwitch(kInteractiveImportProbe));
  const auto source = fixture ? session_path_.DirName().AppendASCII("BrowserImportFixture")
                              : prototype::DefaultBrowserDirectory(options.browser);
  session_writer_->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce(&prototype::ReadBrowserImport, options, source),
      base::BindOnce([](base::WeakPtr<WorkspaceLab> lab, size_t generation,
                        prototype::ArcImport preview) {
        if (!lab || generation != lab->browser_import_generation_ ||
            !lab->shell_->import_visible()) {
          return;
        }
        Report report;
        report.error = preview.error;
        report.profiles = preview.profiles.size();
        report.spaces = preview.spaces.size();
        report.folders = preview.folder_count;
        report.tabs = preview.tab_count;
        report.skipped = preview.skipped_count;
        if (report.error.empty())
          lab->browser_import_preview_ = std::move(preview);
        lab->shell_->SetImportStatus(report, report.error.empty());
      }, weak_factory_.GetWeakPtr(), generation));
}

bool WorkspaceLab::ApplyBrowserImport() {
  using Report = prototype::WorkspaceShell::ImportStatusReport;
  if (!CanImportBrowser() || !CanEditWorkspace() || !browser_import_preview_)
    return false;
  auto source = *browser_import_preview_;
  auto job = std::make_unique<PendingBrowserImport>();
  job->session = CaptureSession();
  job->session.Remove("startup_import_pending");
  std::erase_if(source.spaces, [&](const auto& space) {
    return std::ranges::any_of(imported_workspace_sources_, [&](const auto& item) {
      return item.second == space.id;
    });
  });
  std::map<std::string, std::string> destinations;
  std::set<std::string> new_sources;
  for (const auto& info : source.profiles) {
    if (!std::ranges::contains(source.spaces, info.id,
                               &prototype::ImportedSpace::profile_id)) {
      continue;
    }
    const auto existing = std::ranges::find(profiles_, info.id,
                                            &ContentProfileInfo::import_source);
    if (existing != profiles_.end()) {
      destinations.emplace(info.id, existing->id);
      continue;
    }
    ContentProfileInfo profile{
        "profile-" + base::Uuid::GenerateRandomV4().AsLowercaseString(),
        info.name, /*separate_network=*/BUILDFLAG(IS_MAC), info.id};
    destinations.emplace(info.id, profile.id);
    new_sources.insert(info.id);
    job->session.EnsureList("extra_profiles")->Append(WriteProfileMetadata(profile));
    job->profiles.push_back(std::move(profile));
  }
  for (auto& [source_id, favorites] : source.profile_favorites) {
    if (!new_sources.contains(source_id))
      continue;
    std::erase_if(favorites, [](const prototype::PinnedItem& pin) {
      return pin.folder || !pin.parent_id.empty() ||
             !CanRestoreWorkspaceURL(pin.url);
    });
    if (favorites.empty())
      continue;
    for (auto& pin : favorites) {
      pin.id = "pin-" + base::Uuid::GenerateRandomV4().AsLowercaseString();
      pin.profile_pin = true;
    }
    job->session.EnsureDict("profile_favorites")
        ->Set(destinations.at(source_id), WriteWorkspacePins(favorites));
  }
  for (auto& space : source.spaces) {
    const auto target = destinations.find(space.profile_id);
    if (target == destinations.end())
      return false;
    Workspace workspace{"space-" + base::Uuid::GenerateRandomV4().AsLowercaseString(),
                        target->second, space.name};
    job->session.EnsureList("extra_workspaces")->Append(
        base::DictValue().Set("id", workspace.id).Set("name", workspace.name)
                         .Set("import_source", space.id));
    auto entry = base::DictValue().Set("profile_id", workspace.profile_id)
        .Set("active", 0).Set("tabs", base::ListValue().Append(
            prototype::WriteWorkspaceSessionTab(GURL(), u"")));
    if (!space.pins.empty()) {
      std::map<std::string, std::string> folders;
      std::vector<prototype::PinnedItem> pins;
      for (auto& pin : space.pins) {
        if (!pin.folder && !CanRestoreWorkspaceURL(pin.url))
          continue;
        if (!pin.parent_id.empty()) {
          const auto parent = folders.find(pin.parent_id);
          if (parent == folders.end())
            continue;
          pin.parent_id = parent->second;
        }
        std::string id = "pin-" + base::Uuid::GenerateRandomV4().AsLowercaseString();
        if (pin.folder)
          folders.emplace(pin.id, id);
        pin.id = std::move(id);
        pins.push_back(std::move(pin));
      }
      entry.Set("pins", WriteWorkspacePins(pins));
    }
    job->session.Set(workspace.id, std::move(entry));
    job->spaces.push_back({std::move(workspace), space.id, space.emoji});
  }
  if (profiles_.size() + job->profiles.size() > kMaxContentProfiles ||
      tabs_.size() + job->spaces.size() > kMaxContentWorkspaces ||
      !ValidateContentWorkspaceSession(job->session)) {
    shell_->SetImportStatus(prototype::T(u"Import się nie powiódł. Dotychczasowe przestrzenie pozostały bez zmian. Spróbuj ponownie."),
                            false);
    return false;
  }
  std::erase_if(source.sources, [&](const auto& entry) {
    return !new_sources.contains(entry.destination_id);
  });
  job->browsing_data = !source.sources.empty() &&
                       (source.options.history || source.options.cookies);
  browser_import_ = std::move(job);
  saving_catalogue_ = true;
  RefreshShell();
  Report report;
  report.stage = Report::Stage::kImporting;
  shell_->SetImportStatus(report, false, true, true);
  if (browser_import_->spaces.empty()) {
    // SubmitImport finishes its own state transition before a completion.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&WorkspaceLab::FinishBrowserImport,
                                  weak_factory_.GetWeakPtr(), true, std::string()));
    return true;
  }
  const bool fixture = session_probe_url_.is_valid() &&
      base::CommandLine::ForCurrentProcess()->HasSwitch(kInteractiveImportProbe);
  const bool fixture_without_keychain = fixture &&
      !base::CommandLine::ForCurrentProcess()->HasSwitch(kInteractiveImportKeychainProbe);
  session_writer_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce([](const base::FilePath& path, prototype::ArcImport source,
                        std::vector<ContentProfileInfo> profiles, bool fixture) {
        prototype::ImportCookieKeyReader key_reader;
        if (fixture) {
          key_reader = base::BindOnce(
              [](prototype::ImportBrowser) -> std::optional<prototype::ImportedCookieKey> {
                return std::nullopt;
              });
        }
        auto data = prototype::ReadBrowsingImportData(source, std::move(key_reader));
        if (!data->error.empty())
          return data;
        std::vector<base::ScopedTempDir> directories;
        for (const auto& profile : profiles) {
          if (!PrepareProfileDirectories(path, profile, directories))
            return std::shared_ptr<prototype::BrowsingImportData>();
        }
        for (auto& directory : directories)
          (void)directory.Take();
        return data;
      }, session_path_, std::move(source), browser_import_->profiles,
         fixture_without_keychain),
      base::BindOnce(&WorkspaceLab::ImportDirectoriesReady, weak_factory_.GetWeakPtr()));
  return true;
}

void WorkspaceLab::ImportDirectoriesReady(
    std::shared_ptr<prototype::BrowsingImportData> data) {
  if (!data || !data->error.empty()) {
    FinishBrowserImport(false, data ? std::move(data->error) : std::string());
    return;
  }
  for (const auto& profile : browser_import_->profiles)
    browser_import_->contexts.push_back(OpenNewProfile(profile));
  if (!browser_import_->browsing_data) {
    PublishBrowserImport();
    return;
  }
  prototype::BrowsingImportDestinations destinations;
  for (size_t i = 0; i < browser_import_->profiles.size(); ++i) {
    const auto weak = weak_factory_.GetWeakPtr();
    destinations.emplace(browser_import_->profiles[i].import_source,
        prototype::BrowsingImportDestination{
            base::BindRepeating([](base::WeakPtr<WorkspaceLab> lab, size_t index) {
              return lab ? lab->browser_import_->contexts[index]->GetHistory() : nullptr;
            }, weak, i),
            base::BindRepeating([](base::WeakPtr<WorkspaceLab> lab, size_t index) {
              return lab ? lab->browser_import_->contexts[index]->GetDefaultStoragePartition()
                                 ->GetCookieManagerForBrowserProcess() : nullptr;
            }, weak, i)});
  }
  prototype::WorkspaceShell::ImportStatusReport report;
  report.stage = prototype::WorkspaceShell::ImportStatusReport::Stage::kSavingBrowsingData;
  shell_->SetImportStatus(report, false, true, true);
  prototype::WriteBrowsingImportDataToServices(
      std::move(data), std::move(destinations),
      base::BindOnce(&WorkspaceLab::ImportedBrowsingDataWritten, weak_factory_.GetWeakPtr()));
}

void WorkspaceLab::ImportedBrowsingDataWritten(prototype::BrowsingImportResult result) {
  browser_import_->result = result;
  if (result.failed) {
    FinishBrowserImport(false);
    return;
  }
  const auto flushed = base::BarrierClosure(browser_import_->contexts.size(),
      base::BindPostTaskToCurrentDefault(base::BindOnce(
          &WorkspaceLab::PublishBrowserImport, weak_factory_.GetWeakPtr())));
  // Closing the staging history services commits their pending transactions.
  for (const auto& context : browser_import_->contexts)
    context->CloseImportedHistory(flushed);
}

void WorkspaceLab::PublishBrowserImport() {
  auto& session = browser_import_->session;
  for (size_t i = 0; i < model_.workspaces().size(); ++i)
    session.Set(model_.workspaces()[i].id, CaptureWorkspaceSession(i));
  session.Set("active_workspace", model_.active()->id);
  session_writer_->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce(&WriteContentWorkspaceSession, session_path_,
                                session.Clone()),
      base::BindOnce([](base::WeakPtr<WorkspaceLab> lab, bool ok) {
        if (lab)
          lab->FinishBrowserImport(ok);
      }, weak_factory_.GetWeakPtr()));
}

void WorkspaceLab::FinishBrowserImport(bool ok, std::string error) {
  using Report = prototype::WorkspaceShell::ImportStatusReport;
  auto job = std::move(browser_import_);
  if (ok) {
    startup_import_pending_ = false;
    for (size_t i = 0; i < job->profiles.size(); ++i)
      PublishProfile(std::move(job->profiles[i]), std::move(job->contexts[i]));
    profile_favorites_ = ReadContentProfileFavorites(
        job->session, ReadContentWorkspaces(job->session).value()).value();
    for (auto& space : job->spaces) {
      const auto id = space.workspace.id;
      AppendWorkspace(std::move(space.workspace), job->session.FindDict(id));
      imported_workspace_sources_.emplace(id, std::move(space.source));
      if (!space.emoji.empty())
        shell_->SetWorkspaceEmoji(id, std::move(space.emoji));
    }
  }
  saving_catalogue_ = false;
  InvalidateBrowserImport();
  RefreshShell();
  Report report;
  report.stage = error.empty() ? Report::Stage::kFinished : Report::Stage::kPreview;
  report.error = std::move(error);
  report.ok = ok;
  report.added_profiles = ok ? job->profiles.size() : 0;
  report.added_spaces = ok ? job->spaces.size() : 0;
  report.browsing_data = ok && job->browsing_data;
  report.history = job->result.history;
  report.cookies = job->result.cookies;
  report.data_skipped = job->result.skipped;
  shell_->SetImportStatus(report, false);
  if (std::exchange(close_after_catalogue_save_, false))
    RequestClose();
}

WorkspaceLab::~WorkspaceLab() {
  if (extension_shortcut_target_ && widget_ && widget_->GetFocusManager())
    widget_->GetFocusManager()->UnregisterAccelerators(extension_shortcut_target_.get());
  fullscreen_observation_.Reset();
  if (!private_owner_ && IsWorkspaceExtensionRuntime()) {
    for (const auto& context : contexts_)
      DispatchWorkspaceExtensionWindowEvent(*context, false, true);
  }
  if (fullscreen_page_) {
    fullscreen_page_.reset();
    shell_->SetContentFullscreen(nullptr);
  }
  if (pending_extension_tab_removal_)
    CompleteExtensionTabRemoval(false);
  for (const auto& workspace : tabs_) {
    for (const auto& tab : workspace) {
      MarkWorkspaceExtensionTabWindowClosing(tab.extension_tab.get());
    }
  }
#if BUILDFLAG(ENABLE_PRINTING)
  pdf_export_.reset();
#if BUILDFLAG(ENABLE_PDF)
  pdf_preview_.reset();
#endif
  pdf_probe_picker_.reset();
#endif
  StopFind();
  Observe(nullptr);
  weak_factory_.InvalidateWeakPtrs();
  if (extension_picker_)
    extension_picker_->ListenerDestroyed();
  extension_install_.reset();
  web_store_update_timer_.Stop();
  web_store_update_.reset();
  private_session_.reset();
  if (!private_owner_) {
    for (const auto& context : contexts_)
      context->site_permissions().SetClipboardReadPrompt({});
  }
  FinishPermissionPrompt(false);
  extension_popup_view_ = nullptr;
  extension_popup_widget_.reset();
  extension_popup_delegate_.reset();
  managed_cache_views_ = {};
  managed_cache_probe_host_.reset();
  media_access_.reset();
  adblock_count_subscription_ = {};
  extensions_toolbar_menu_widget_.reset();
  extensions_toolbar_menu_.reset();
  night_shift_widget_.reset();
  night_shift_panel_.reset();
  night_shift_button_ = nullptr;
  DestroyAdblockShield();
  adblock_shield_button_ = nullptr;
  private_fingerprint_subscription_ = {};
  fingerprint_toolbar_.reset();
  fingerprint_button_ = nullptr;
  assistant_toolbar_button_ = nullptr;
  extensions_toolbar_button_ = nullptr;
  pinned_extension_toolbar_ = nullptr;
  extension_toolbar_divider_ = nullptr;
  webauthn_probe_.reset();
  devtools_.reset();
  ad_blocking_rules_panel_.reset();
  history_panel_.reset();
  downloads_.reset();
  passwords_panel_.reset();
  DiscardPeek();
  if (assistant_) {
    assistant_ = nullptr;
    shell_->SetAssistantView(nullptr);
  }
  shell_.reset();
  for (auto& tabs : tabs_)
    tabs.clear();
  widget_->CloseNow();
}

void WorkspaceLab::Start(base::OnceCallback<void(bool)> done,
                         base::OnceClosure quit_from_app_for_testing) {
  done_ = std::move(done);
  quit_from_app_for_testing_ = std::move(quit_from_app_for_testing);
  if (private_owner_) {
    StartWorkspace();
    return;
  }
  const auto& command = *base::CommandLine::ForCurrentProcess();
  if (command.HasSwitch(kPrivateAdblockProbe)) {
    private_adblock_probe_url_ = GURL(command.GetSwitchValueASCII(kPrivateAdblockProbe));
    if (smoke_ || !session_path_.empty() ||
        !private_adblock_probe_url_.SchemeIs("http") ||
        private_adblock_probe_url_.host() != "127.0.0.1" ||
        !private_adblock_probe_url_.has_port() ||
        private_adblock_probe_url_.has_username() ||
        private_adblock_probe_url_.has_password() ||
        private_adblock_probe_url_.path() != "/" ||
        private_adblock_probe_url_.has_query() ||
        private_adblock_probe_url_.has_ref()) {
      Finish(false);
      return;
    }
  }
  const bool test_lists = command.HasSwitch(kAdblockListsProbe) ||
                          private_adblock_probe_url_.is_valid();
  if (!smoke_ && (!session_probe_url_.is_valid() || test_lists)) {
    const auto& loopback = private_adblock_probe_url_.is_valid()
        ? private_adblock_probe_url_ : session_probe_url_;
    if (test_lists && (!loopback.SchemeIsHTTPOrHTTPS() ||
                       loopback.host() != "127.0.0.1" ||
                       !loopback.has_port())) {
      Finish(false);
      return;
    }
    adblock_filter_lists_ = std::make_unique<prototype::AdblockFilterLists>(
        base::BindRepeating(
            [](base::WeakPtr<WorkspaceLab> lab,
               const prototype::AdblockFilterList& list) {
              return lab && std::ranges::any_of(lab->contexts_, [&](const auto& profile) {
                return profile->GetAdblockEnabled().value_or(false) &&
                       profile->GetAdblockListEnabled(list.id);
              });
            }, weak_factory_.GetWeakPtr()),
        prototype::AdblockFilterLists::Factory(),
        base::BindRepeating(&WorkspaceLab::RebuildAdblockProfiles,
                            weak_factory_.GetWeakPtr()),
        base::BindRepeating([](base::WeakPtr<WorkspaceLab> lab) {
          if (lab)
            lab->shell_->RefreshAdBlockingPreferences();
        }, weak_factory_.GetWeakPtr()));
    if (test_lists)
      adblock_filter_lists_->SetLoopbackSourceForTesting(loopback);
    waiting_for_adblock_cache_ = true;
    const auto cache_root = session_path_.empty()
        ? devtools_path_.DirName() : session_path_.DirName();
    adblock_filter_lists_->Start(
        cache_root.AppendASCII("Zitter Filter Lists"), !test_lists);
    return;
  }
  StartWorkspace();
}

void WorkspaceLab::RebuildAdblockProfiles() {
  const bool resume = std::exchange(waiting_for_adblock_cache_, false);
  const auto ready = base::BarrierClosure(
      contexts_.size() + (private_session_ ? 1 : 0),
      base::BindOnce([](base::WeakPtr<WorkspaceLab> lab, bool resume) {
        if (!lab)
          return;
        lab->shell_->RefreshAdBlockingPreferences();
        if (resume)
          lab->StartWorkspace();
      }, weak_factory_.GetWeakPtr(), resume));
  for (const auto& profile : contexts_)
    profile->SetAdblockFilterTexts(adblock_filter_lists_->texts(), ready);
  if (private_session_)
    private_session_->profile->SetAdblockFilterTexts(
        adblock_filter_lists_->texts(), ready);
}

void WorkspaceLab::StartWorkspace() {
  const auto& switches = *base::CommandLine::ForCurrentProcess();
  bool active_probe = switches.HasSwitch(kFindReloadProbe) || switches.HasSwitch(kRssProbe) ||
                      switches.HasSwitch(kInteractiveImportProbe) ||
                      switches.GetSwitchValueASCII(kTimelineProbe) == "restart";
#if BUILDFLAG(ENABLE_PRINTING)
  active_probe |= switches.HasSwitch(kPdfExportProbe);
#endif
  if (smoke_ || (session_probe_url_.is_valid() && !active_probe) ||
      private_adblock_probe_url_.is_valid()) {
    widget_->ShowInactive();
  } else {
    widget_->Show();
  }
  if (private_owner_) {
    SelectWorkspace(std::string(kWorkspaceIds[0]));
    shell_->OpenAddress(false);
    return;
  }
  if (adblock_filter_lists_ && session_path_.empty())
    adblock_filter_lists_->Refresh(false);
  if (private_adblock_probe_url_.is_valid()) {
    SelectWorkspace(std::string(kWorkspaceIds[0]));
    CheckPrivateAdblockProbe(0, base::TimeTicks::Now() + base::Seconds(10));
    return;
  }
  for (const auto& profile : contexts_) {
    auto* prefs = profile->GetPasswordPrefs();
    if (prefs && prefs->GetInitializationStatus() ==
                     PrefService::INITIALIZATION_STATUS_WAITING) {
      prefs->AddPrefInitObserver(base::BindOnce(
          [](base::WeakPtr<WorkspaceLab> lab, bool) {
            if (lab)
              lab->RefreshShell();
          },
          weak_factory_.GetWeakPtr()));
    }
  }
  if (smoke_) {
    const auto& command_line = *base::CommandLine::ForCurrentProcess();
    if (command_line.HasSwitch(kWorkspaceWebAuthnProbe)) {
      webauthn_probe_url_ = GURL(
          command_line.GetSwitchValueASCII(kWorkspaceWebAuthnProbe));
      if (!webauthn_probe_url_.SchemeIs("http") ||
          webauthn_probe_url_.host() != "localhost" ||
          !webauthn_probe_url_.has_port() ||
          webauthn_probe_url_.path() != "/" ||
          webauthn_probe_url_.has_username() ||
          webauthn_probe_url_.has_password() ||
          webauthn_probe_url_.has_query() ||
          webauthn_probe_url_.has_ref() ||
          !Activate(kWorkspaceIds[0], kWorkspaceIds[0]) ||
          !web_contents()->GetBrowserContext()->IsOffTheRecord()) {
        Finish(false);
        return;
      }
      web_contents()->GetController().LoadURLWithParams(
          content::NavigationController::LoadURLParams(webauthn_probe_url_));
      return;
    }
    tls_probe_expect_trusted_ = command_line.HasSwitch(kWorkspaceTrustedTlsProbe);
    if (tls_probe_expect_trusted_ || command_line.HasSwitch(kWorkspaceTlsProbe)) {
      tls_probe_url_ = GURL(command_line.GetSwitchValueASCII(
          tls_probe_expect_trusted_ ? kWorkspaceTrustedTlsProbe : kWorkspaceTlsProbe));
      if (!tls_probe_url_.is_valid() || !tls_probe_url_.SchemeIs("https") ||
          (!tls_probe_expect_trusted_ &&
           (tls_probe_url_.host() != "127.0.0.1" || !tls_probe_url_.has_port() ||
            tls_probe_url_.path() != "/")) ||
          tls_probe_url_.has_username() || tls_probe_url_.has_password() ||
          tls_probe_url_.has_query() || tls_probe_url_.has_ref() ||
          !Activate(kWorkspaceIds[0], kWorkspaceIds[0])) {
        Finish(false);
        return;
      }
      web_contents()->GetController().LoadURLWithParams(
          content::NavigationController::LoadURLParams(tls_probe_url_));
      return;
    }
    SwitchTo(0);
    return;
  }
  const auto& command_line = *base::CommandLine::ForCurrentProcess();
  if (command_line.HasSwitch(kWorkspaceDownloadProbe)) {
    StartDownloadProbe();
    return;
  }
  if (command_line.HasSwitch(kSitePermissionProbe)) {
    site_permission_probe_phase_ = command_line.GetSwitchValueASCII(kSitePermissionProbe);
    if (!session_probe_url_.is_valid() || session_path_.empty() ||
        (site_permission_probe_phase_ != "change" &&
         site_permission_probe_phase_ != "reopen" &&
         site_permission_probe_phase_ != "reset" &&
         site_permission_probe_phase_ != "policy" &&
         site_permission_probe_phase_ != "embedded" &&
         site_permission_probe_phase_ != "prompt-cancel" &&
         site_permission_probe_phase_ != "prompt-allow" &&
         site_permission_probe_phase_ != "prompt-navigation" &&
         site_permission_probe_phase_ != "prompt-switch" &&
         site_permission_probe_phase_ != kClipboardReadProbePhase &&
         site_permission_probe_phase_ != "media-allow" &&
         site_permission_probe_phase_ != "media-deny" &&
         site_permission_probe_phase_ != "media-policy" &&
         site_permission_probe_phase_ != "media-switch" &&
         site_permission_probe_phase_ != "media-tab-deny" &&
         site_permission_probe_phase_ != "media-tab-policy" &&
         site_permission_probe_phase_ != "media-tab-exclude" &&
         site_permission_probe_phase_ != "media-tab-allow" &&
         site_permission_probe_phase_ != "media-tab-switch" &&
         site_permission_probe_phase_ != "media-tab-other-allow" &&
         site_permission_probe_phase_ != "media-tab-other-close" &&
         site_permission_probe_phase_ != "media-tab-other-navigation" &&
         site_permission_probe_phase_ != "media-tab-other-audio-allow" &&
         site_permission_probe_phase_ != "media-tab-other-audio-navigation" &&
         site_permission_probe_phase_ != "media-tab-desktop-window-allow" &&
         site_permission_probe_phase_ != "media-tab-desktop-screen-allow" &&
         site_permission_probe_phase_ != "media-tab-desktop-exclude-allow" &&
         site_permission_probe_phase_ != "media-tab-desktop-native-cancel" &&
         site_permission_probe_phase_ != "media-tab-desktop-native-window-allow" &&
         site_permission_probe_phase_ != "media-tab-desktop-native-screen-allow" &&
         site_permission_probe_phase_ != "other")) {
      Finish(false);
      return;
    }
    if (site_permission_probe_phase_.starts_with("media-")) {
      const bool tab = site_permission_probe_phase_.starts_with("media-tab-");
      if (command_line.HasSwitch("use-fake-device-for-media-stream") == tab ||
          command_line.HasSwitch("use-fake-ui-for-media-stream")) {
        Finish(false);
        return;
      }
    }
    if (site_permission_probe_phase_.starts_with("media-tab-")) {
      using DesktopProbe = WorkspaceMediaAccess::DesktopProbe;
      auto mode = DesktopProbe::kTabsOnly;
      if (site_permission_probe_phase_ == "media-tab-desktop-native-cancel")
        mode = DesktopProbe::kNativeCancel;
      else if (site_permission_probe_phase_.starts_with("media-tab-desktop-native-"))
        mode = DesktopProbe::kNone;
      else if (site_permission_probe_phase_.starts_with("media-tab-desktop-"))
        mode = DesktopProbe::kSynthetic;
      media_access_->SetDesktopProbe(mode);
    }
    if (site_permission_probe_phase_ == kClipboardReadProbePhase) {
#if BUILDFLAG(IS_MAC)
      auto* install = reinterpret_cast<bool (*)()>(
          dlsym(RTLD_DEFAULT, "ZitterInstallClipboardFixture"));
      if (!install || !install()) {
        LOG(ERROR) << "Clipboard read fixture is unavailable";
        Finish(false);
        return;
      }
#else
      Finish(false);
      return;
#endif
    }
    shell_->SetPinned(true);
    shell_->ShowSidebar();
    LoadSitePermissionProbe();
    return;
  }
  if (command_line.HasSwitch(kFingerprintProbe)) {
    const auto phase = command_line.GetSwitchValueASCII(kFingerprintProbe);
    if (!session_probe_url_.is_valid() ||
        (phase != "change" && phase != "reopen")) {
      Finish(false);
      return;
    }
    fingerprint_probe_reopen_ = phase == "reopen";
    fingerprint_probe_step_ = 0;
    LoadFingerprintProbeStep();
    return;
  }
  if (command_line.HasSwitch(kWorkspacePasswordProbe)) {
    StartPasswordProbe(command_line.GetSwitchValueASCII(kWorkspacePasswordProbe));
    return;
  }
  if (command_line.HasSwitch("zitter-content-http-auth-probe")) {
    const auto action = command_line.GetSwitchValueASCII("zitter-content-http-auth-probe");
    if (!session_probe_url_.SchemeIs("http") ||
        session_probe_url_.host() != "127.0.0.1" ||
        (action != "allow" && action != "cancel" && action != "navigate" && action != "hide")) {
      Finish(false);
      return;
    }
    SelectWorkspace(std::string(kWorkspaceIds[0]));
    Navigate(session_probe_url_.Resolve("/http-auth/" + action), false);
    CheckHttpAuthProbe(action, 0, base::TimeTicks::Now() + base::Seconds(15));
    return;
  }
  if (session_probe_url_.is_valid() && !CheckSessionProbeSetup()) {
    Finish(false);
    return;
  }
  const bool restore_extensions = command_line.HasSwitch(kRestoreWorkspaceExtensions);
  if (restore_extensions)
    session_probe_advancing_ = true;
  SelectWorkspace(model_.workspaces()[restored_workspace_].id);
  if (std::exchange(close_after_workspace_start_, false)) {
    RequestClose();
    return;
  }
  for (auto& url : std::exchange(startup_external_urls_, {}))
    OpenExternalUrl(std::move(url));
  if (restore_extensions) {
    StartManagedExtensions(0);
    if (!session_probe_url_.is_valid()) {
      web_store_update_timer_.Start(FROM_HERE, base::Hours(6), this,
                                    &WorkspaceLab::CheckWebStoreUpdates);
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&WorkspaceLab::CheckWebStoreUpdates, weak_factory_.GetWeakPtr()),
          base::Seconds(30));
    }
  }
  if (startup_import_pending_ && CanImportBrowser())
    shell_->OpenImport();
  if (!startup_import_probe_.empty()) {
    const bool expect_import = startup_import_probe_ != "reopen";
    if (shell_->import_visible() != expect_import) {
      LOG(ERROR) << "Unexpected Content startup import panel visibility";
      Finish(false);
      return;
    }
    if (startup_import_probe_ == "skip") {
      DismissBrowserImport();
      shell_->CloseOverlays();
    }
    std::puts("Zitter Content startup import probe passed");
    RequestClose();
  }
}

bool WorkspaceLab::Activate(std::string_view workspace_id,
                                 std::string_view profile_id) {
  base::AutoReset<bool> updating(&updating_panes_, true);
  const auto& workspaces = model_.workspaces();
  for (size_t index = 0; index < workspaces.size(); ++index) {
    if (workspaces[index].id != workspace_id || workspaces[index].profile_id != profile_id)
      continue;
    if (tabs_[index][selected_[index]].view->GetBrowserContext() !=
        WorkspaceProfile(index)) {
      return false;
    }
    if (active_index_ != index) {
      CloseExtensionPopup();
      CloseNightShiftSettings();
      ExitPageFullscreen();
      fingerprint_toolbar_->Close();
      address_history_tasks_.TryCancelAll();
      shell_->CloseOverlays(true);
      shell_->HideToast();
      shell_->ResetDownloadPresentation();
      ad_blocking_rules_panel_.reset();
      history_panel_.reset();
      passwords_panel_.reset();
      Observe(nullptr);
      devtools_.reset();
      shell_->SetPanes({}, {});
      for (auto& tab : tabs_[active_index_]) {
        if (tab.view->GetVisible())
          HideTabView(*tab.view);
#if BUILDFLAG(IS_MAC)
        // Remove the native surface from this window without destroying the
        // WebContents or the tab's navigation state.
        if (tab.view->parent()) {
          tab.detached_view =
              shell_->content_host()->RemoveChildViewT(tab.view.get());
        }
#endif
      }
    }
    active_index_ = index;
    sleeping_[index] = false;
    ShowTab();
    if (!smoke_) {
      downloads_->SetSource(
          WorkspaceProfile(index)->GetDownloadManager(), base::UTF8ToUTF16(WorkspaceProfileName(index)),
          {}, {}, {}, prototype::DownloadPanel::FileActions::kOpenRevealAndDelete);
    }
    return true;
  }
  return false;
}

bool WorkspaceLab::AcceleratorPressed(const ui::Accelerator& accelerator) {
  if (!CanHandleAccelerators() || accelerator.key_state() != ui::Accelerator::KeyState::PRESSED)
    return false;
  if (accelerator.key_code() == ui::VKEY_ESCAPE && accelerator.modifiers() == 0) {
    if (ExitPageFullscreen())
      return true;
    if (PeekPage()) {
      ClosePeek();
      return true;
    }
    if (startup_import_pending_ && shell_->import_visible() && !saving_catalogue_)
      DismissBrowserImport();
    return shell_->CloseOverlays();
  }
  return RunShortcut(shell_->ShortcutFor(accelerator));
}

bool WorkspaceLab::CanHandleAccelerators() const {
  return !smoke_ && !session_probe_url_.is_valid();
}

namespace {


ui::Accelerator ExtensionCommandKey(content::BrowserContext& profile,
                                    const extensions::Extension& extension,
                                    const ui::Command& command) {
  if (auto* prefs = extensions::ExtensionPrefs::Get(&profile)) {
    if (const auto* keys = prefs->ReadPrefAsDict(extension.id(), kManagerShortcutPref)) {
      if (const auto* override = keys->FindString(command.command_name()))
        return ui::Command::StringToAccelerator(*override);
    }
  }
  return command.accelerator();
}

void SetManagerToolbarPin(extensions::ExtensionPrefs& prefs,
                          const std::string& id, bool pinned) {
  auto ids = prefs.GetPinnedExtensions();
  std::erase(ids, id);
  if (pinned)
    ids.push_back(id);
  prefs.UpdateExtensionPref(id, kManagerPinPref, base::Value(pinned));
  prefs.SetPinnedExtensions(ids);
}

struct ExtensionCommandBinding {
  scoped_refptr<const extensions::Extension> extension;
  std::string command;
};

void ForEachExtensionCommand(const extensions::Extension& extension,
                             base::FunctionRef<void(const ui::Command&)> visit) {
  if (const auto* action = extensions::CommandsInfo::GetActionCommand(&extension))
    visit(*action);
  if (const auto* commands = extensions::CommandsInfo::GetNamedCommands(&extension)) {
    for (const auto& entry : *commands)
      visit(entry.second);
  }
}

std::map<ui::Accelerator, ExtensionCommandBinding> ExtensionCommandBindings(
    content::BrowserContext& profile) {
  std::map<ui::Accelerator, ExtensionCommandBinding> bindings;
  for (const auto& extension : GetConfiguredExtensions(profile)) {
    if (!CanLoadExtensionProbeResources(profile, extension->id()))
      continue;
    ForEachExtensionCommand(*extension, [&](const ui::Command& command) {
      const auto key = ExtensionCommandKey(profile, *extension, command);
      if (command.global() || key.key_code() == ui::VKEY_UNKNOWN) {
        return;
      }
      auto [it, inserted] = bindings.try_emplace(
          key, ExtensionCommandBinding{extension, command.command_name()});
      if (!inserted)
        it->second = {};
    });
  }
  return bindings;
}

}  // namespace

base::ListValue WorkspaceLab::GetExtensionCommands(
    content::BrowserContext& profile, const extensions::Extension& extension,
    bool manager_details) const {
  const auto bindings = ExtensionCommandBindings(profile);
  base::ListValue result;
  ForEachExtensionCommand(extension, [&](const ui::Command& command) {
    const auto key = ExtensionCommandKey(profile, extension, command);
    const auto binding = bindings.find(key);
    const bool active = shell_->ShortcutFor(key).empty() &&
        binding != bindings.end() && binding->second.extension == &extension &&
        binding->second.command == command.command_name();
    auto entry = base::DictValue()
                     .Set("name", command.command_name())
                     .Set("description", command.description())
                     .Set("shortcut", active ? key.GetShortcutText() : std::u16string());
    if (manager_details)
      entry.Set("global", command.global());
    result.Append(std::move(entry));
  });
  return result;
}

void WorkspaceLab::RefreshExtensionShortcuts() {
  if (private_owner_ || !IsWorkspaceExtensionRuntime() || !widget_ ||
      !widget_->GetFocusManager()) {
    return;
  }
  std::set<ui::Accelerator> keys;
  for (const auto& [key, binding] :
       ExtensionCommandBindings(*WorkspaceProfile(active_index_))) {
    if (binding.extension)
      keys.insert(key);
  }
  if (keys == extension_shortcuts_)
    return;
  if (!extension_shortcut_target_)
    extension_shortcut_target_ = std::make_unique<ExtensionShortcutTarget>(*this);
  auto* focus = widget_->GetFocusManager();
  focus->UnregisterAccelerators(extension_shortcut_target_.get());
  extension_shortcuts_ = std::move(keys);
  for (const auto& key : extension_shortcuts_) {
    focus->RegisterAccelerator(key, ui::AcceleratorManager::kNormalPriority,
                               extension_shortcut_target_.get());
  }
}

bool WorkspaceLab::DispatchExtensionShortcut(const ui::Accelerator& accelerator) {
  if (private_owner_ || accelerator.IsRepeat() ||
      accelerator.key_state() != ui::Accelerator::KeyState::PRESSED ||
      !shell_->ShortcutFor(accelerator).empty()) {
    return false;
  }
  auto& profile = *WorkspaceProfile(active_index_);
  const auto bindings = ExtensionCommandBindings(profile);
  const auto it = bindings.find(accelerator);
  if (it == bindings.end() || !it->second.extension)
    return false;
  const auto& binding = it->second;
  if (binding.command == extensions::manifest_values::kActionCommandEvent) {
    OpenExtensionPopup(binding.extension->id());
  } else {
    DispatchWorkspaceExtensionCommand(
        profile, *binding.extension, binding.command,
        accelerator.IsMediaKey() ? nullptr : web_contents());
  }
  return true;
}

bool WorkspaceLab::RunShortcut(std::string id) {
  if (smoke_)
    return false;
  if (id == "exit_fullscreen")
    return ExitPageFullscreen();
  if (id == "toggle_window_fullscreen") {
    if (!ExitPageFullscreen())
      widget_->SetFullscreen(!widget_->IsFullscreen());
    return true;
  }
  if (id == "assistant" || id == "assistant_hover")
    return ShowAssistant(id == "assistant_hover");
  if (id == "private_window") {
    return OpenPrivateWindow();
  }
  if (id == "night_shift_settings")
    return OpenNightShiftSettings();
  if (id == "reload_workspace_icons")
    return RefreshWorkspaceFavicons();
  if (id == "find")
    return StartFind();
  if (id == "copy_url") {
    auto* page = PeekPage();
    if (!page)
      page = web_contents();
    if (!CanEditWorkspace() || !page ||
        !prototype::IsWorkspaceTabUrl(page->GetVisibleURL())) {
      return false;
    }
    const auto& url = page->GetVisibleURL();
    const auto clean = CleanAddressCopies() ? prototype::CleanUrlForCopy(url)
                                            : std::nullopt;
    ui::ScopedClipboardWriter(ui::ClipboardBuffer::kCopyPaste)
        .WriteText(base::UTF8ToUTF16(clean ? *clean : url.spec()));
    shell_->TremorAddress();
    return true;
  }
#if BUILDFLAG(ENABLE_PRINTING)
  if (id == "save_pdf")
    return StartPdfExport({}, {}, false);
#if BUILDFLAG(IS_MAC)
  if (id == "print_system")
    return StartPdfExport({}, {}, false, true);
#endif
#if BUILDFLAG(ENABLE_PDF)
  if (id == "print")
    return ShowPdfPrintSheet();
#endif
#endif
  if (id == "view_source") {
    auto* page = PeekPage();
    if (!page)
      page = web_contents();
    if (!CanEditWorkspace() || !CanOpenPageSource(page) ||
        tabs_[active_index_].size() >= prototype::kMaxWorkspaceSessionTabs) {
      return false;
    }
    const auto& committed = page->GetController().GetLastCommittedEntry()->GetURL();
    const GURL source(base::StrCat({content::kViewSourceScheme, ":", committed.spec()}));
    AddTab(active_index_);
    SelectTab(tabs_[active_index_].size() - 1);
    // This browser-initiated load stays in the current profile and bypasses
    // cross-workspace link routing. Content rewrites it into a source document.
    prototype::LoadWorkspaceTab(web_contents(), source, ui::PAGE_TRANSITION_TYPED);
    return true;
  }
  if (id == "hard_reload") {
    auto* page = PageForNavigationCommand();
    if (!CanEditWorkspace() || !page ||
        !page->GetController().GetLastCommittedEntry()) {
      return false;
    }
    ReloadWorkspacePage(*page, /*bypass_cache=*/true);
    return true;
  }
  if (id == "palette") {
    shell_->OpenCommandPalette();
  } else if (id == "address" || id == "new_tab") {
    shell_->OpenAddress(id != "address");
  } else if (id == "next_space" || id == "previous_space" ||
             (id.starts_with("space") && id.size() == 6 &&
              id.back() >= '1' && id.back() <= '9')) {
    const auto& workspaces = model_.workspaces();
    if (private_owner_ || !CanEditWorkspace() || workspaces.empty())
      return false;
    size_t index;
    if (id == "next_space" || id == "previous_space") {
      if (workspaces.size() < 2)
        return false;
      const size_t offset = id == "next_space" ? 1 : workspaces.size() - 1;
      index = (active_index_ + offset) % workspaces.size();
    } else {
      index = id.back() - '1';
      if (index >= workspaces.size())
        return false;
    }
    SelectWorkspace(workspaces[index].id);
  } else if (id == "back" || id == "forward" || id == "reload") {
    RunPageCommand(id == "back" ? prototype::PageCommand::kBack
                   : id == "forward" ? prototype::PageCommand::kForward
                                     : prototype::PageCommand::kReload);
  } else if (id == "sleep") {
    return SleepWorkspace(model_.active()->id);
  } else if (id == "timeline") {
    if (!timeline_settings_ready_ || !CanEditWorkspace())
      return false;
    shell_->OpenTimeMachine();
  } else if (id == "snapshot") {
    return SaveTimelineSnapshot(base::UTF16ToUTF8(prototype::T(u"Snapshot")));
  } else if (id == "reopen") {
    return ReopenLastClosedTab();
  } else if (id == "duplicate_tab") {
    return DuplicateTab(selected_[active_index_]);
  } else if (id == "split" || id == "close_split") {
    if (!CanEditWorkspace() || PeekPage())
      return false;
    const size_t selected = selected_[active_index_];
    if (SplitShown()) {
      RecordTimeline(true);
      ClearSplit(selected);
      return true;
    }
    if (id == "close_split")
      return true;
    if (tabs_[active_index_].size() > 1)
      return SplitTab(selected == 0 ? 1 : 0);
    AddTab(active_index_);
    if (!SplitTab(1))
      return false;
    shell_->OpenAddress(false);
  } else if (id == "left_pane" || id == "right_pane" ||
             id == "third_pane" || id == "fourth_pane") {
    if (!CanEditWorkspace() || !SplitShown() || PeekPage())
      return false;
    const size_t pane = id == "left_pane" ? 0 : id == "right_pane" ? 1
                                        : id == "third_pane" ? 2 : 3;
    if (pane >= splits_[active_index_].tabs.size())
      return false;
    FocusPane(pane);
  } else if (id == "close_other_tabs") {
    return CloseOtherTabs();
  } else if (id == "close_tab") {
    if (PeekPage() && shell_->peek_widget()->IsVisible())
      ClosePeek();
    else
      CloseTab(selected_[active_index_]);
  } else if (id == "next_tab" || id == "previous_tab") {
    const size_t count = tabs_[active_index_].size();
    if (count < 2)
      return false;
    SelectTab(shell_->CycleSidebarTab(selected_[active_index_], count,
                                      id == "previous_tab"));
  } else if (id.starts_with("tab") && id.size() == 4 &&
             id.back() >= '1' && id.back() <= '9') {
    const size_t index = id.back() - '1';
    if (index >= tabs_[active_index_].size())
      return false;
    SelectTab(index);
  } else if (id == "sidebar") {
    shell_->ToggleSidebar();
  } else if (id == "passwords") {
    return OpenPasswords();
  } else if (id == "history") {
    return OpenHistory();
  } else if (id == "downloads") {
    return OpenDownloads();
  } else if (id == "rss") {
    return OpenRss();
  } else if (id == "import") {
    if (!CanImportBrowser() || !CanEditWorkspace())
      return false;
    shell_->OpenImport();
  } else if (id == "settings") {
    shell_->OpenGeneralSettings();
  } else if (id == "shortcuts" || id == "engines") {
    shell_->OpenTools(id == "shortcuts" ? 1 : 2);
  } else if (id == "spaces" || id == "profiles") {
    if (private_owner_ || !CanEditWorkspace())
      return false;
    if (id == "spaces")
      shell_->OpenWorkspaceSettings();
    else
      shell_->OpenProfiles();
  } else if (id == "assistant_settings") {
    if (!CanConfigureAssistant() || assistant_host_settings_.enabled)
      return false;
    shell_->OpenAssistantSettings();
  } else if (id == "about") {
    shell_->ShowAbout(private_owner_ ? private_owner_->application_version_
                                     : application_version_);
  } else if (id == "fingerprint") {
    fingerprint_toolbar_->Toggle();
    return fingerprint_toolbar_->widget() &&
           !fingerprint_toolbar_->widget()->IsClosed();
  } else if (id == "developer") {
    shell_->ShowDeveloperMenu();
  } else if (id == "project_split") {
    auto* page = web_contents();
    return page && prototype::IsLocalProject(page->GetVisibleURL()) &&
           SplitTab(selected_[active_index_]);
  } else if (id == "devtools" || id == "devtools_panel" ||
             id == "devtools_console" || id == "devtools_inspect") {
    auto* page = web_contents();
    if (!page || prototype::IsRssTabUrl(page->GetVisibleURL()))
      return false;
    if (devtools_ && devtools_->IsOpenFor(page)) {
      if (id == "devtools") {
        devtools_.reset();
        return true;
      }
    } else {
      OpenDevTools({});
    }
    auto action = WorkspaceDevTools::Action::kShow;
    if (id == "devtools_console")
      action = WorkspaceDevTools::Action::kConsole;
    else if (id == "devtools_inspect")
      action = WorkspaceDevTools::Action::kInspect;
    devtools_->Show(action);
  } else {
    return false;
  }
  return true;
}

bool WorkspaceLab::OpenRss() {
  if (!CanEditWorkspace() || !model_.active() || !web_contents())
    return false;
  auto* source = web_contents();
  auto& tabs = tabs_[active_index_];
  size_t index = 0;
  for (; index < tabs.size(); ++index) {
    if (prototype::IsRssTabUrl(TabUrl(index, {})))
      break;
  }
  shell_->CloseOverlays(true);
  if (index == tabs.size()) {
    if (tabs.size() >= prototype::kMaxWorkspaceSessionTabs)
      return false;
    const auto pins = WorkspacePins(active_index_);
    const auto pin = std::ranges::find_if(pins, [&tabs](const auto& entry) {
      return prototype::IsRssTabUrl(entry.url) &&
             !std::ranges::contains(tabs, entry.id, &Tab::pin);
    });
    AddTab(active_index_, GURL(prototype::kRssTabUrl), {},
           pin == pins.end() ? "" : pin->id);
  }
  SelectTab(index);
  auto* panel = static_cast<WorkspaceWebView*>(tabs[index].view.get())->MountReader();
  if (source != web_contents())
    panel->DiscoverFromPage(*source);
  panel->RequestFocus();
  PersistWorkspaceChanges();
  return true;
}

bool WorkspaceLab::OpenPasswords() {
  if (!model_.active() || saving_session_ || before_unload_batch_.pending())
    return false;
  auto* store = WorkspaceProfile(active_index_)->GetPasswordStore();
  if (!store)
    return false;
  shell_->CloseOverlays(true);
  if (!passwords_panel_) {
    passwords_panel_ = std::make_unique<prototype::PasswordsPanel>();
    passwords_panel_->SetOpenCallback(base::BindRepeating(
        [](base::WeakPtr<WorkspaceLab> lab, GURL url) {
          if (lab)
            lab->Navigate(url, /*new_tab=*/true);
        },
        weak_factory_.GetWeakPtr()));
    passwords_panel_->SetConfirmCallback(base::BindRepeating(
        [](base::WeakPtr<WorkspaceLab> lab, views::Widget* parent,
           std::u16string title, std::u16string detail, std::u16string warning,
           std::u16string action, base::OnceCallback<void(bool)> answer) {
          if (lab) {
            lab->shell_->ConfirmDanger(parent, std::move(title), std::move(detail),
                                       std::move(warning), std::move(action),
                                       std::move(answer));
          }
        },
        weak_factory_.GetWeakPtr()));
  }
  prototype::PasswordsPanel::AuthenticatorFactory authenticator;
#if BUILDFLAG(IS_MAC)
  authenticator = base::BindRepeating(
      [](views::Widget*) { return CreateMacPasswordAuthenticator(); });
#elif BUILDFLAG(IS_WIN)
  authenticator = base::BindRepeating(
      [](views::Widget* owner) {
        return CreateWindowsPasswordAuthenticator(*owner);
      });
#endif
  if (session_probe_url_.is_valid() && password_probe_action_ == "manager") {
    authenticator = base::BindRepeating(
        [](base::WeakPtr<WorkspaceLab> lab, views::Widget*)
            -> std::unique_ptr<device_reauth::DeviceAuthenticator> {
          if (!lab)
            return nullptr;
          return std::make_unique<ProbePasswordAuthenticator>(
              base::BindRepeating(
                  [](base::WeakPtr<WorkspaceLab> lab,
                     device_reauth::DeviceAuthenticator::AuthenticateCallback reply) {
                    if (lab)
                      lab->password_manager_auth_reply_ = std::move(reply);
                  },
                  lab));
        },
        weak_factory_.GetWeakPtr());
  }
  passwords_panel_->Show(base::WrapRefCounted(store),
                         base::UTF8ToUTF16(WorkspaceProfileName(active_index_)), widget_.get(),
                         std::move(authenticator),
                         /*activate=*/!session_probe_url_.is_valid());
  return true;
}

bool WorkspaceLab::OpenDownloads() {
  if (!model_.active() || saving_session_ || before_unload_batch_.pending())
    return false;
  const auto anchor = shell_->downloads_anchor_in_screen();
  shell_->CloseOverlays(true);
  downloads_->Show(widget_.get(), anchor);
  return downloads_->widget() && downloads_->widget()->IsVisible();
}

void WorkspaceLab::DownloadCompleted(std::string guid, std::u16string name) {
  shell_->DownloadFinished();
  shell_->ShowToast(prototype::T(u"Pobrano "), std::move(name), {});
  if (download_probe_active_) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&WorkspaceLab::AdvanceDownloadProbe,
                                  weak_factory_.GetWeakPtr(), std::move(guid)));
  }
}

bool WorkspaceLab::HandleNativeMenuCommand(gfx::NativeWindow key,
                                            gfx::NativeWindow main,
                                            std::string command,
                                            bool validate) {
  WorkspaceLab* target = nullptr;
  auto owns_window = [](WorkspaceLab& workspace, gfx::NativeWindow native) {
    auto* peek = workspace.shell_->peek_widget();
    return workspace.widget_->GetNativeWindow() == native ||
           (peek && peek->IsVisible() && peek->GetNativeWindow() == native);
  };
  for (auto native : {key, main}) {
    if (!native)
      continue;
    if (owns_window(*this, native)) {
      target = this;
      break;
    }
    if (private_session_) {
      auto child = std::ranges::find_if(private_session_->windows,
          [&](const auto& window) {
            return owns_window(*window, native);
          });
      if (child != private_session_->windows.end()) {
        target = child->get();
        break;
      }
    }
  }
  if (!target && (command == "private_window" || command == "about" ||
                  command == "new_tab" || command == "reopen_window")) {
    target = this;
  }
  if (!target && command == "night_shift_settings" && widget_->IsVisible() &&
      (!private_session_ || private_session_->windows.empty())) {
    target = this;
  }
  if (!target || !target->done_ || !target->model_.active() ||
      !target->CanEditWorkspace()) {
    return false;
  }
  if ((command == "find" ||
       command == "save_pdf" || command == "print" || command == "print_system") &&
      (!target->web_contents() ||
       !target->web_contents()->GetController().GetLastCommittedEntry())) {
    return false;
  }
  if (command == "view_source") {
    auto* page = target->PeekPage();
    if (!page)
      page = target->web_contents();
    if (!CanOpenPageSource(page) ||
        target->tabs_[target->active_index_].size() >= prototype::kMaxWorkspaceSessionTabs) {
      return false;
    }
  }
  if (command == "reload" || command == "hard_reload") {
    auto* page = target->PageForNavigationCommand();
    if (!page || !page->GetController().GetLastCommittedEntry())
      return false;
  }
  if (command == "back" || command == "forward") {
    auto* page = target->PageForNavigationCommand();
    if (!page || (command == "back" ? !page->GetController().CanGoBack()
                                   : !page->GetController().CanGoForward())) {
      return false;
    }
  }
  if (command == "history" &&
      target->WorkspaceProfile(target->active_index_)->IsOffTheRecord()) {
    return false;
  }
  if (command == "copy_url") {
    auto* page = target->PeekPage();
    if (!page)
      page = target->web_contents();
    if (!page || !prototype::IsWorkspaceTabUrl(page->GetVisibleURL()))
      return false;
  }
  if (validate && command == "reopen")
    return target->LastClosedTab().has_value() ||
           target->HistoricalClosedTab().has_value();
  if (command == "night_shift_settings" &&
      (target->private_owner_ || target->session_path_.empty() ||
       !BundledNightShiftSettingsPage(
           *target->WorkspaceProfile(target->active_index_)).is_valid())) {
    return false;
  }
  if (command == "import" && !target->CanImportBrowser())
    return false;
  if (command == "timeline" && !target->timeline_settings_ready_)
    return false;
  const auto tab_count = target->tabs_[target->active_index_].size();
  if (command == "duplicate_tab" &&
      (tab_count == 0 || tab_count >= prototype::kMaxWorkspaceSessionTabs)) {
    return false;
  }
  if ((command == "next_tab" || command == "previous_tab") && tab_count < 2)
    return false;
  if ((command == "space1" || command == "space2" ||
       command == "next_space" || command == "previous_space") &&
      (target->private_owner_ ||
       target->model_.workspaces().size() < (command == "space1" ? 1u : 2u))) {
    return false;
  }
  if (validate)
    return true;
  if (command == "about" || command == "new_tab" || command == "reopen_window") {
    if (target->widget_->IsMinimized())
      target->widget_->Restore();
    target->widget_->Show();
    target->widget_->Activate();
  }
  return command == "reopen_window" || target->RunShortcut(std::move(command));
}

void WorkspaceLab::SetApplicationVersion(std::string version) {
  application_version_ = std::move(version);
}

void WorkspaceLab::SetNativeMenuProbe(
    base::RepeatingCallback<bool(std::string)> activate) {
  native_menu_probe_ = std::move(activate);
}

bool WorkspaceLab::OpenPrivateWindow() {
  if (private_owner_)
    return private_owner_->OpenPrivateWindow();
  if (!done_ || !model_.active() || !CanEditWorkspace())
    return false;

  if (++pending_private_windows_ == 1 && !private_session_) {
    session_writer_->PostTaskAndReplyWithResult(
        FROM_HERE,
        base::BindOnce([](const base::FilePath& root,
                          scoped_refptr<base::SequencedTaskRunner> file_runner)
                           -> std::unique_ptr<PrivateSession> {
          auto session = std::make_unique<PrivateSession>(std::move(file_runner));
          if (!session->directory->CreateUniqueTempDirUnderPath(root))
            return nullptr;
          return session;
        }, devtools_path_.DirName(), session_writer_),
        base::BindOnce(&WorkspaceLab::InitializePrivateSession,
                       weak_factory_.GetWeakPtr(), WorkspaceProfileIndex(active_index_)));
  } else {
    OpenPendingPrivateWindows();
  }
  return true;
}

void WorkspaceLab::InitializePrivateSession(
    size_t source_index,
    std::unique_ptr<PrivateSession> session) {
  if (!done_)
    return;
  if (!session) {
    pending_private_windows_ = 0;
    LOG(ERROR) << "Could not create the private Workspace directory";
    return;
  }
  auto& source = *contexts_[source_index];
  session->profile = std::make_unique<ProfileContext>(
      session->directory->GetPath().AppendASCII("Profile"),
      ProfileContext::Mode::kPrivate, std::nullopt, nullptr, std::nullopt);
  if (!user_download_directory_.empty())
    session->profile->EnableUserDownloads(user_download_directory_);
  session->profile->InitializePrivateAdblock(&source);
  session->profile->InheritPrivateFingerprintProtection(source);
  session->profile->InheritPrivatePageZoom(source);
  session->profile->site_permissions().InheritPrivatePopupSettings(source.site_permissions());
  session->profile->site_permissions().SetClipboardReadPrompt(
      base::BindRepeating(
          [](base::WeakPtr<WorkspaceLab> owner, content::RenderFrameHost& frame,
             base::OnceCallback<void(bool)> answer) {
            if (owner && owner->private_session_) {
              for (const auto& window : owner->private_session_->windows) {
                auto* page = window->web_contents();
                if (window->done_ && page &&
                    page->GetPrimaryMainFrame() == &frame) {
                  window->ShowClipboardReadPrompt(frame, std::move(answer));
                  return;
                }
              }
            }
            std::move(answer).Run(false);
          }, weak_factory_.GetWeakPtr()));
  session->filter_ready =
      session->profile->ObserveAdblockSettings(base::BindRepeating(
          [](base::WeakPtr<WorkspaceLab> owner) {
            base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
                FROM_HERE,
                base::BindOnce(&WorkspaceLab::OpenPendingPrivateWindows, owner));
          }, weak_factory_.GetWeakPtr()));
  private_session_ = std::move(session);
  if (adblock_filter_lists_) {
    private_session_->profile->SetAdblockFilterTexts(
        adblock_filter_lists_->texts(), {});
  }
  OpenPendingPrivateWindows();
}

void WorkspaceLab::OpenPendingPrivateWindows() {
  if (!done_ || !private_session_ || !pending_private_windows_ ||
      private_session_->profile->IsAdblockBuilding()) {
    return;
  }
  auto& session = *private_session_;
  const auto& directory = session.directory->GetPath();
  const size_t count = std::exchange(pending_private_windows_, 0);
  for (size_t index = 0; index < count; ++index) {
    auto window = std::make_unique<WorkspaceLab>(
        directory.AppendASCII("Settings.json"), directory, base::FilePath(),
        base::DictValue(), nullptr, GURL(), *session.profile, nullptr,
        /*dark=*/true, metal_, /*smoke=*/false, this);
    auto* child = window.get();
    session.windows.push_back(std::move(window));
    child->Start(base::BindOnce(
        [](base::WeakPtr<WorkspaceLab> owner, WorkspaceLab* child, bool) {
          base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
              FROM_HERE,
              base::BindOnce(&WorkspaceLab::ClosePrivateWindow, owner, child));
        }, weak_factory_.GetWeakPtr(), child));
  }
}

void WorkspaceLab::ClosePrivateWindow(WorkspaceLab* window) {
  if (!private_session_)
    return;
  std::erase_if(private_session_->windows,
                [window](const auto& entry) { return entry.get() == window; });
  if (private_session_->windows.empty() && !pending_private_windows_)
    private_session_.reset();
}

prototype::AdblockFilterLists* WorkspaceLab::AdblockLists() const {
  return private_owner_ ? private_owner_->adblock_filter_lists_.get()
                        : adblock_filter_lists_.get();
}

void WorkspaceLab::UpdateAdblockShield() {
  auto* page = web_contents();
  if (adblock_shield_page_ && !adblock_shield_page_.Run())
    CloseAdblockShield();
  if (adblock_counted_page_.get() != page) {
    adblock_count_subscription_ = page
        ? ProfileContext::ObserveAdblockCount(
              *page, base::BindRepeating(&WorkspaceLab::RefreshAdblockCount,
                                        weak_factory_.GetWeakPtr()))
        : base::CallbackListSubscription();
    adblock_counted_page_ = page ? page->GetWeakPtr() : nullptr;
  }
  RefreshAdblockCount();
}

void WorkspaceLab::RefreshAdblockCount() {
  auto* page = adblock_counted_page_.get();
  if (!page || page != web_contents())
    return;
  const bool show_shield =
      !smoke_ && WorkspaceProfile(active_index_)->GetAdblockEnabled() == true;
  adblock_shield_button_->SetVisible(show_shield);
  if (!show_shield) {
    CloseAdblockShield();
    return;
  }
  const int count = ProfileContext::AdblockCount(*page);
  if (adblock_shield_button_->count() == count)
    return;
  adblock_shield_button_->SetCount(count);
  if (adblock_shield_card_ && !adblock_shield_widget_->IsClosed() &&
      adblock_shield_page_.Run() == page) {
    adblock_shield_card_->Set(
        count, base::UTF8ToUTF16(page->GetLastCommittedURL().host()));
    RefreshAdblockShieldRequests();
    adblock_shield_panel_->SizeToContents();
  }
}

void WorkspaceLab::RefreshAdblockShieldRequests() {
  auto* page = adblock_shield_page_.Run();
  if (!page || !adblock_shield_requests_ || !adblock_shield_requests_->GetVisible())
    return;
  adblock_shield_requests_->RemoveAllChildViews();
  const auto palette = sidebar::design_palette(
      widget_->GetColorMode() == ui::ColorProviderKey::ColorMode::kDark,
      shell_->active_workspace_color());
  const auto add_label = [&](std::u16string text) {
    auto label = std::make_unique<views::Label>(text);
    label->SetFontList(prototype::DesignFont(2, 12, 400));
    label->SetEnabledColor(palette.ink_muted);
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    label->SetElideBehavior(gfx::ELIDE_MIDDLE);
    adblock_shield_requests_->AddChildView(std::move(label));
  };
  const auto requests = ProfileContext::RecentAdblockRequests(*page);
  if (requests.empty()) {
    add_label(prototype::T(u"Brak zablokowanych żądań sieciowych"));
    return;
  }
  for (auto it = requests.rbegin(); it != requests.rend(); ++it)
    add_label(base::UTF8ToUTF16(*it));
}

void WorkspaceLab::ToggleAdblockShield() {
  CloseNightShiftSettings();
  if (adblock_shield_widget_) {
    CloseAdblockShield();
    return;
  }
  auto* page = web_contents();
  auto* frame = page ? page->GetPrimaryMainFrame() : nullptr;
  const auto& profile = *WorkspaceProfile(active_index_);
  if (!frame || !frame->IsActive() || !CanEditWorkspace() ||
      profile.GetAdblockEnabled() != true) {
    return;
  }
  const auto url = page->GetLastCommittedURL();
  const auto allowed = profile.GetEditableAdblockSiteException(url);
  const auto weak = weak_factory_.GetWeakPtr();
  adblock_shield_page_ = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab,
         const content::WeakDocumentPtr& document, const GURL& url)
          -> content::WebContents* {
        auto* frame = document.AsRenderFrameHostIfValid();
        auto* page = lab ? lab->web_contents() : nullptr;
        return frame && frame->IsActive() && page &&
                       page->GetPrimaryMainFrame() == frame &&
                       page->GetLastCommittedURL().host() == url.host()
                   ? page : nullptr;
      }, weak, frame->GetWeakDocumentPtr(), url);

  shell_->CloseOverlays(true);
  fingerprint_toolbar_->Close();
  const auto palette = sidebar::design_palette(
      widget_->GetColorMode() == ui::ColorProviderKey::ColorMode::kDark,
      shell_->active_workspace_color());
  adblock_shield_panel_ = std::make_unique<views::BubbleDialogDelegate>(
      adblock_shield_button_, views::BubbleBorder::TOP_RIGHT);
  auto& panel = *adblock_shield_panel_;
  panel.SetOwnershipOfNewWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  panel.SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  panel.set_margins(gfx::Insets(4));
  panel.set_corner_radius(12);
  panel.set_fixed_width(prototype::kMenuWidth);
  panel.SetBackgroundColor(palette.bg_raised);
  panel.SetShowCloseButton(false);
  panel.SetAccessibleWindowRole(ax::mojom::Role::kDialog);
  panel.SetTitle(prototype::T(u"Blokowanie reklam"));
  panel.SetShowTitle(false);

  auto body = std::make_unique<views::View>();
  body->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  body->AddChildView(std::make_unique<prototype::MenuHeader>(
      palette, prototype::T(u"Blokowanie reklam")));
  adblock_shield_card_ = body->AddChildView(
      std::make_unique<prototype::ShieldCard>(palette));
  adblock_shield_card_->Set(ProfileContext::AdblockCount(*page),
                          base::UTF8ToUTF16(url.host()));
  body->AddChildView(std::make_unique<prototype::MenuSeparator>(palette.line));
  auto add_row = [&](const char* glyph, std::u16string title,
                     base::RepeatingClosure callback) {
    auto* row = body->AddChildView(std::make_unique<prototype::MenuRow>(
        std::move(callback), palette, prototype::kFooterHeight));
    row->SetGlyph(glyph);
    row->Init();
    row->SetText(std::move(title), {}, false, 16);
    return row;
  };
  if (ProfileContext::HasAdblockTrial(*page)) {
    static constexpr char kEndTrialIcon[] = "M4 4l8 8M12 4l-8 8";
    auto* row = add_row(
        kEndTrialIcon, prototype::T(u"Zakończ próbę reguły"),
        base::BindRepeating([](base::WeakPtr<WorkspaceLab> lab) {
          if (!lab || !lab->adblock_shield_widget_ ||
              lab->adblock_shield_widget_->IsClosed()) {
            return;
          }
          auto* page = lab->adblock_shield_page_.Run();
          if (!page)
            return;
          ProfileContext::ClearAdblockTrial(*page);
          lab->CloseAdblockShield();
          lab->ReloadWorkspacePage(*page, /*bypass_cache=*/false);
        }, weak));
    row->SetID(kAdblockShieldEndTrialId);
  }
  if (allowed.has_value()) {
    auto* row = add_row(
        *allowed ? prototype::kShieldIcon : prototype::kShieldOffIcon,
        *allowed ? prototype::T(u"Włącz blokowanie na tej stronie")
                 : prototype::T(u"Wyłącz blokowanie na tej stronie"),
        base::BindRepeating([](base::WeakPtr<WorkspaceLab> lab, bool allowed) {
          if (!lab || !lab->CanEditWorkspace() || !lab->adblock_shield_widget_ ||
              lab->adblock_shield_widget_->IsClosed()) {
            return;
          }
          auto* page = lab->adblock_shield_page_.Run();
          if (!page)
            return;
          auto& profile = *lab->WorkspaceProfile(lab->active_index_);
          const auto& url = page->GetLastCommittedURL();
          if (profile.GetEditableAdblockSiteException(url) != allowed ||
              !profile.SetAdblockSiteAllowed(url.host(), !allowed)) {
            return;
          }
          lab->CloseAdblockShield();
          lab->RunPageCommand(prototype::PageCommand::kReload);
        }, weak, *allowed));
    row->SetID(kAdblockShieldToggleId);
  } else if (url.SchemeIsHTTPOrHTTPS()) {
    auto* row = add_row(prototype::kShieldOffIcon,
                        prototype::T(u"Sprawdź wyjątki w ustawieniach"), {});
    row->SetEnabled(false);
  }
  if (url.SchemeIsHTTPOrHTTPS()) {
    static constexpr char kListIcon[] = "M3 4h10M3 8h10M3 12h10";
    auto* requests = add_row(
        kListIcon, prototype::T(u"Ostatnie zablokowane żądania"),
        base::BindRepeating([](base::WeakPtr<WorkspaceLab> lab) {
          if (!lab || !lab->adblock_shield_widget_ ||
              lab->adblock_shield_widget_->IsClosed() ||
              !lab->adblock_shield_page_.Run()) {
            return;
          }
          auto* list = lab->adblock_shield_requests_.get();
          const bool expanded = !list->GetVisible();
          auto* row = lab->adblock_shield_widget_->GetContentsView()
                          ->GetViewByID(kAdblockShieldRequestsId);
          if (expanded)
            row->GetViewAccessibility().SetIsExpanded();
          else
            row->GetViewAccessibility().SetIsCollapsed();
          list->SetVisible(expanded);
          if (expanded)
            lab->RefreshAdblockShieldRequests();
          lab->adblock_shield_panel_->SizeToContents();
        }, weak));
    requests->SetID(kAdblockShieldRequestsId);
    requests->GetViewAccessibility().SetIsCollapsed();
    adblock_shield_requests_ = body->AddChildView(std::make_unique<views::View>());
    adblock_shield_requests_->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets::TLBR(0, 12, 8, 12), 3));
    adblock_shield_requests_->SetVisible(false);
  }
  body->AddChildView(std::make_unique<prototype::MenuSeparator>(palette.line));
  auto* settings = add_row(
      prototype::kGearIcon, prototype::T(u"Ustawienia blokowania"),
      base::BindRepeating([](base::WeakPtr<WorkspaceLab> lab) {
        if (!lab || !lab->adblock_shield_widget_ ||
            lab->adblock_shield_widget_->IsClosed() ||
            !lab->adblock_shield_page_.Run()) {
          return;
        }
        lab->CloseAdblockShield();
        lab->shell_->OpenAdBlockingSettings();
      }, weak));
  settings->SetRing(true);
  panel.SetContentsView(std::move(body));
  adblock_shield_widget_ = views::BubbleDialogDelegate::CreateBubble(
      &panel, base::BindOnce([](base::WeakPtr<WorkspaceLab> lab,
                                views::Widget::ClosedReason) {
        base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE, base::BindOnce(&WorkspaceLab::DestroyAdblockShield, lab));
      }, weak));
  adblock_shield_button_->SetOpen(true);
  adblock_shield_widget_->Show();
}

void WorkspaceLab::CloseAdblockShield() {
  if (adblock_shield_widget_ && !adblock_shield_widget_->IsClosed())
    adblock_shield_widget_->Close();
}

void WorkspaceLab::DestroyAdblockShield() {
  if (adblock_shield_button_)
    adblock_shield_button_->SetOpen(false);
  adblock_shield_card_ = nullptr;
  adblock_shield_requests_ = nullptr;
  adblock_shield_widget_.reset();
  adblock_shield_panel_.reset();
  adblock_shield_page_.Reset();
}

void WorkspaceLab::OpenSitePanel() {
  auto* page = web_contents();
  auto* frame = page ? page->GetPrimaryMainFrame() : nullptr;
  if (!frame || !frame->IsActive())
    return;
  const auto origin = frame->GetLastCommittedOrigin();
  using Permissions = ProfilePermissionController;
  if (!Permissions::IsPopupOriginEligible(origin))
    return;

  auto resolve = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, const content::WeakDocumentPtr& document)
          -> Permissions* {
        auto* current = document.AsRenderFrameHostIfValid();
        auto* page = lab ? lab->web_contents() : nullptr;
        if (!current || !current->IsActive() || !page ||
            page->GetPrimaryMainFrame() != current) {
          return nullptr;
        }
        return &static_cast<ProfileContext*>(current->GetBrowserContext())->site_permissions();
      },
      weak_factory_.GetWeakPtr(), frame->GetWeakDocumentPtr());
  auto* permissions = resolve.Run();
  if (!permissions || !permissions->CanEditSitePermissions())
    return;

  prototype::WorkspaceShell::SiteHandlers handlers;
  handlers.read = base::BindRepeating(
      [](const base::RepeatingCallback<Permissions*()>& resolve,
         const url::Origin& origin) {
        prototype::SiteSummary summary;
        summary.url = origin.GetURL();
        summary.host = base::UTF8ToUTF16(origin.Serialize());
        summary.secure = summary.url.SchemeIs(url::kHttpsScheme);
        if (auto* current = resolve.Run()) {
          if (Permissions::IsClipboardOriginEligible(origin)) {
            summary.permissions.push_back(
                {"clipboard", static_cast<int>(current->GetClipboardSetting(origin))});
          }
          summary.permissions.push_back({
              .key = "popups",
              .setting = static_cast<int>(current->GetPopupSetting(origin)),
              .can_ask = false,
          });
        }
        return summary;
      },
      resolve, origin);
  handlers.set_permission = base::BindRepeating(
      [](const base::RepeatingCallback<Permissions*()>& resolve,
         const url::Origin& origin, std::string key, int setting) {
        auto* current = resolve.Run();
        if (!current)
          return false;
        if (key == "clipboard") {
          return current->SetClipboardSetting(
              origin, static_cast<Permissions::ClipboardSetting>(setting));
        }
        return key == "popups" && current->SetPopupSetting(
            origin, static_cast<Permissions::PopupSetting>(setting));
      },
      resolve, origin);
  handlers.reset_permissions = base::BindRepeating(
      [](const base::RepeatingCallback<Permissions*()>& resolve,
         const url::Origin& origin) {
        if (auto* current = resolve.Run()) {
          current->SetClipboardSetting(origin, Permissions::ClipboardSetting::kAsk);
          current->SetPopupSetting(origin, std::nullopt);
        }
      },
      resolve, origin);
  auto summary = handlers.read.Run();
  shell_->ShowSitePanel(std::move(summary), std::move(handlers));
}

void WorkspaceLab::PromptExtensionPermissions(
    content::BrowserContext& profile,
    const extensions::Extension& extension,
    const extensions::PermissionSet& permissions,
    base::OnceCallback<void(bool)> answer) {
  if (private_owner_ || profile.IsOffTheRecord() || !widget_ ||
      !widget_->IsActive() || WorkspaceProfile(active_index_) != &profile ||
      !CanEditWorkspace()) {
    std::move(answer).Run(false);
    return;
  }

  auto name = base::UTF8ToUTF16(extension.name());
  std::ranges::replace_if(name, [](char16_t c) { return c < 0x20; }, u' ');
  base::i18n::SanitizeUserSuppliedString(&name);
  auto detail = prototype::TF(u"{} prosi o dodatkowy dostęp w tym profilu.", {name});
  for (const auto& api : permissions.GetAPIsAsStrings())
    detail += u"\n" + prototype::TF(u"Uprawnienie: {}", {base::UTF8ToUTF16(api)});
  for (const auto& host : permissions.explicit_hosts())
    detail += u"\n" + prototype::TF(u"Dostęp do hosta: {}", {base::UTF8ToUTF16(host.GetAsString())});

  // Keep the entire requested scope visible in the permission prompt.
  if (detail.size() > 2048) {
    std::move(answer).Run(false);
    return;
  }
  const size_t owner = WorkspaceProfileIndex(active_index_);
  ShowPermissionPrompt(
      prototype::T(u"Zezwolić rozszerzeniu na dodatkowy dostęp?"),
      std::move(detail), prototype::T(u"Zezwól"), PermissionPrompt::kExtension,
      {}, 0, {}, base::BindOnce(
          [](base::WeakPtr<WorkspaceLab> lab, size_t profile,
             base::OnceCallback<void(bool)> answer, std::optional<size_t> accepted,
             bool) {
            std::move(answer).Run(accepted && lab && lab->CanEditWorkspace() &&
                                  lab->WorkspaceProfileIndex(lab->active_index_) == profile);
          },
          weak_factory_.GetWeakPtr(), owner, std::move(answer)));
}

void WorkspaceLab::ShowClipboardReadPrompt(
    content::RenderFrameHost& frame,
    base::OnceCallback<void(bool)> answer) {
  auto* page = web_contents();
  if (!page || page->GetPrimaryMainFrame() != &frame) {
    std::move(answer).Run(false);
    return;
  }
  const std::u16string_view prompt_text = frame.GetBrowserContext()->IsOffTheRecord()
      ? u"{} chce odczytać schowek. Zgoda dla tej witryny wygaśnie po zakończeniu sesji prywatnej."
      : u"{} chce odczytać schowek. Zgoda zostanie zapisana dla tej witryny w bieżącym profilu.";
  ShowPermissionPrompt(prototype::T(u"Zezwolić na odczyt schowka?"),
                       prototype::TF(prompt_text,
                         {base::UTF8ToUTF16(frame.GetLastCommittedOrigin().Serialize())}),
                       prototype::T(u"Zezwól dla witryny"),
                       PermissionPrompt::kClipboard, {}, 0, {},
                       base::BindOnce([](base::OnceCallback<void(bool)> done,
                                         std::optional<size_t> accepted, bool) {
                         std::move(done).Run(accepted.has_value());
                       }, std::move(answer)));
}

void WorkspaceLab::ShowPermissionPrompt(std::u16string title,
                                         std::u16string text,
                                         std::u16string allow,
                                         PermissionPrompt kind,
                                         std::vector<std::u16string> choices,
                                         size_t tab_count,
                                         std::u16string audio_option,
                                         base::OnceCallback<void(std::optional<size_t>, bool)> answer) {
  if (!widget_->IsVisible() || permission_prompt_widget_ || saving_session_ ||
      before_unload_batch_.pending()) {
    std::move(answer).Run(std::nullopt, false);
    return;
  }
  permission_prompt_answer_ = std::move(answer);
  permission_prompt_cancelled_ = false;
  auto message = std::make_unique<views::Label>(std::move(text));
  message->SetMultiLine(true);
  message->SetAllowCharacterBreak(true);
  message->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  auto body = std::make_unique<views::View>();
  prototype::SetSidebarPanelLayout(body.get(), true, gfx::Insets(), 12);
  body->AddChildView(std::move(message));
  if (!choices.empty()) {
    std::vector<ui::SimpleComboboxModel::Item> items;
    items.reserve(choices.size());
    for (auto& choice : choices)
      items.emplace_back(std::move(choice));
    permission_prompt_choices_ = body->AddChildView(
        std::make_unique<views::Combobox>(
            std::make_unique<ui::SimpleComboboxModel>(std::move(items))));
    permission_prompt_choices_->GetViewAccessibility().SetName(
        prototype::T(u"Źródło udostępniania"));
  }
  if (!audio_option.empty()) {
    permission_prompt_audio_ = body->AddChildView(
        std::make_unique<views::Checkbox>(std::move(audio_option)));
    permission_prompt_choices_->SetCallback(base::BindRepeating(
        [](base::WeakPtr<WorkspaceLab> lab, size_t tab_count) {
          if (!lab || !lab->permission_prompt_choices_ ||
              !lab->permission_prompt_audio_)
            return;
          const bool tab =
              lab->permission_prompt_choices_->GetSelectedIndex().value_or(tab_count) <
              tab_count;
          lab->permission_prompt_audio_->SetEnabled(tab);
          if (!tab)
            lab->permission_prompt_audio_->SetChecked(false);
        }, weak_factory_.GetWeakPtr(), tab_count));
  }
  permission_prompt_delegate_ =
      prototype::CreateSceneModalDialog(std::move(body), false);
  permission_prompt_delegate_->SetTitle(std::move(title));
  permission_prompt_delegate_->SetButtons(
      static_cast<int>(ui::mojom::DialogButton::kOk) |
      static_cast<int>(ui::mojom::DialogButton::kCancel));
  permission_prompt_delegate_->SetButtonLabel(
      ui::mojom::DialogButton::kOk, std::move(allow));
  permission_prompt_delegate_->SetButtonLabel(
      ui::mojom::DialogButton::kCancel, prototype::T(u"Anuluj"));
  permission_prompt_delegate_->SetDefaultButton(
      static_cast<int>(ui::mojom::DialogButton::kCancel));
  permission_prompt_widget_.reset(views::DialogDelegate::CreateDialogWidget(
      permission_prompt_delegate_.get(), widget_->GetNativeWindow(), widget_->GetNativeView()));
  permission_prompt_widget_->MakeCloseSynchronous(base::BindOnce(
      [](base::WeakPtr<WorkspaceLab> lab, views::Widget::ClosedReason reason) {
        base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE, base::BindOnce(&WorkspaceLab::FinishPermissionPrompt, lab,
                reason == views::Widget::ClosedReason::kAcceptButtonClicked));
      },
      weak_factory_.GetWeakPtr()));
  permission_prompt_widget_->SetColorModeOverride(widget_->color_mode_override());
  permission_prompt_widget_->Show();
  const bool tab_probe = site_permission_probe_phase_.starts_with("media-tab-");
  if ((kind == PermissionPrompt::kClipboard &&
       site_permission_probe_phase_ == kClipboardReadProbePhase) ||
      (kind == PermissionPrompt::kTabCapture && tab_probe) ||
      (kind == PermissionPrompt::kDeviceCapture && !tab_probe &&
       site_permission_probe_phase_.starts_with("media-"))) {
    site_permission_prompt_shown_ = true;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce([](base::WeakPtr<WorkspaceLab> lab) {
          if (lab && lab->permission_prompt_widget_ &&
              !lab->permission_prompt_widget_->IsClosed()) {
            const auto& phase = lab->site_permission_probe_phase_;
            const bool other = phase.starts_with("media-tab-other-");
            if (phase.starts_with("media-tab-")) {
              auto* audio = lab->permission_prompt_audio_.get();
              const bool audio_requested = phase != "media-tab-other-close";
              if (static_cast<bool>(audio) != audio_requested ||
                  (audio && audio->GetChecked())) {
                LOG(ERROR) << "Unexpected audio option in the tab capture picker";
                lab->Finish(false);
                return;
              }
              if (phase.starts_with("media-tab-other-audio-"))
                audio->SetChecked(true);
            }
            if (phase.starts_with("media-tab-desktop-")) {
              auto* choices = lab->permission_prompt_choices_.get();
              const bool exclude = phase == "media-tab-desktop-exclude-allow";
              if (!choices || choices->GetModel()->GetItemCount() != (exclude ? 2u : 3u)) {
                LOG(ERROR) << "Unexpected system capture sources";
                lab->Finish(false);
                return;
              }
              choices->MenuSelectionAt(phase.ends_with("-screen-allow") ? 2 : 1);
              if (lab->permission_prompt_audio_ && lab->permission_prompt_audio_->GetEnabled()) {
                LOG(ERROR) << "Tab audio option remained enabled for a system source";
                lab->Finish(false);
                return;
              }
            }
            if (other) {
              const bool select_second = phase.ends_with("-allow");
              auto* choices = lab->permission_prompt_choices_.get();
              if (!choices ||
                  choices->GetModel()->GetItemCount() != (select_second ? 2u : 1u)) {
                LOG(ERROR) << "Unexpected sources in the tab capture picker";
                lab->Finish(false);
                return;
              }
              choices->SetSelectedIndex(select_second ? 1 : 0);
            }
            if (other || phase.ends_with("-allow") || phase.ends_with("-switch") ||
                phase == "media-tab-desktop-native-cancel" ||
                phase == kClipboardReadProbePhase) {
              lab->permission_prompt_delegate_->AcceptDialog();
            } else {
              lab->permission_prompt_delegate_->CancelDialog();
            }
          }
        }, weak_factory_.GetWeakPtr()));
  }
}

void WorkspaceLab::CancelPermissionPrompt() {
  if (!permission_prompt_widget_)
    return;
  permission_prompt_cancelled_ = true;
  permission_prompt_widget_->Close();
}

void WorkspaceLab::FinishPermissionPrompt(bool accepted) {
  std::optional<size_t> selection;
  if (accepted && !permission_prompt_cancelled_) {
    selection = permission_prompt_choices_ ? permission_prompt_choices_->GetSelectedIndex()
                                           : std::optional<size_t>(0);
  }
  const bool share_audio =
      selection && permission_prompt_audio_ && permission_prompt_audio_->GetEnabled() &&
      permission_prompt_audio_->GetChecked();
  permission_prompt_choices_ = nullptr;
  permission_prompt_audio_ = nullptr;
  permission_prompt_widget_.reset();
  permission_prompt_delegate_.reset();
  if (permission_prompt_answer_)
    std::move(permission_prompt_answer_).Run(selection, share_audio);
}

GURL WorkspaceLab::SitePermissionProbeUrl() const {
  if (site_permission_probe_phase_.starts_with("media-tab-"))
    return session_probe_url_.Resolve("/site-" + site_permission_probe_phase_);
  if (site_permission_probe_phase_.starts_with("media-")) {
    return session_probe_url_.Resolve(site_permission_probe_phase_ == "media-policy"
        ? "/site-media-policy" : "/site-media");
  }
  if (site_permission_probe_phase_ == kClipboardReadProbePhase)
    return session_probe_url_.Resolve("/site-permissions-read");
  if (site_permission_probe_phase_ == "policy")
    return session_probe_url_.Resolve("/site-permissions-policy");
  if (site_permission_probe_phase_ == "embedded")
    return session_probe_url_.Resolve("/site-permissions-embedded");
  return session_probe_url_.Resolve("/site-permissions");
}

void WorkspaceLab::LoadSitePermissionProbe() {
  const bool other = site_permission_probe_phase_.starts_with("media-tab-other-");
  if (other || site_permission_probe_phase_ == "media-tab-exclude") {
    SelectWorkspace(std::string(kWorkspaceIds[1]));
    if (active_index_ != 1 ||
        !Navigate(session_probe_url_.Resolve("/site-media-decoy"), false)) {
      Finish(false);
      return;
    }
  }
  SelectWorkspace(std::string(kWorkspaceIds[site_permission_probe_profile_]));
  if (active_index_ != site_permission_probe_profile_ ||
      (other && (tabs_[active_index_].size() != 1 ||
                 !Navigate(session_probe_url_.Resolve("/site-media-source"), true)))) {
    Finish(false);
    return;
  }
  if (other)
    SelectTab(0);
  if (!Navigate(SitePermissionProbeUrl(), false))
    Finish(false);
}

void WorkspaceLab::RunClipboardPromptProbe() {
  site_permission_probe_changed_ = true;
  RequestClipboardPromptProbe(base::TimeTicks::Now() + base::Seconds(5), false);
}

void WorkspaceLab::RequestClipboardPromptProbe(base::TimeTicks deadline,
                                              bool activation_sent) {
  auto* page = web_contents();
  if (!page || page->GetLastCommittedURL() != SitePermissionProbeUrl()) {
    Finish(false);
    return;
  }
  auto* frame = page->GetPrimaryMainFrame();
  const bool focused = frame->IsFocused();
  if (!focused || !frame->HasTransientUserActivation()) {
    if (base::TimeTicks::Now() >= deadline) {
      LOG(ERROR) << "Clipboard prompt fixture did not gain focus and activation";
      Finish(false);
      return;
    }
    if (!focused) {
      if (!widget_->IsActive())
        widget_->Activate();
      page->Focus();
    } else if (!activation_sent) {
      frame->NotifyUserActivation(blink::mojom::UserActivationNotificationType::kTest);
      activation_sent = true;
    }
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&WorkspaceLab::RequestClipboardPromptProbe,
                       weak_factory_.GetWeakPtr(), deadline, activation_sent),
        base::Milliseconds(25));
    return;
  }
  auto descriptor = blink::mojom::PermissionDescriptor::New();
  descriptor->name = blink::mojom::PermissionName::CLIPBOARD_READ;
  content::PermissionRequestDescription request(
      std::move(descriptor), /*user_gesture=*/true,
      frame->GetLastCommittedOrigin().GetURL());
  WorkspaceProfile(active_index_)->site_permissions().RequestPermissionsFromCurrentDocument(
      frame, request, base::BindOnce(
          [](base::WeakPtr<WorkspaceLab> lab,
             const std::vector<content::PermissionResult>& results) {
            if (!lab)
              return;
            const auto expected = lab->site_permission_probe_phase_ == "prompt-allow"
                ? content::PermissionStatus::GRANTED : content::PermissionStatus::DENIED;
            if (!lab->site_permission_prompt_shown_ || results.size() != 1 ||
                results.front().status != expected) {
              LOG(ERROR) << "Unexpected Clipboard prompt result";
              lab->Finish(false);
              return;
            }
            std::puts("Zitter Content site permissions probe passed");
            lab->RequestClose();
          },
          weak_factory_.GetWeakPtr()));
  if (!permission_prompt_widget_ || !permission_prompt_widget_->IsVisible() ||
      permission_prompt_widget_->color_mode_override() != widget_->color_mode_override()) {
    Finish(false);
    return;
  }
  site_permission_prompt_shown_ = true;
  if (site_permission_probe_phase_ == "prompt-cancel") {
    permission_prompt_delegate_->CancelDialog();
  } else {
    permission_prompt_delegate_->AcceptDialog();
    if (site_permission_probe_phase_ == "prompt-navigation") {
      Navigate(session_probe_url_.Resolve("/site-permissions?replacement"), false);
    } else if (site_permission_probe_phase_ == "prompt-switch") {
      SelectWorkspace(std::string(kWorkspaceIds[1]));
    }
  }
}

void WorkspaceLab::CheckSitePermissionProbe() {
  auto* page = web_contents();
  const auto expected_url = SitePermissionProbeUrl();
  if (!page || page->GetLastCommittedURL() != expected_url)
    return;
  if (site_permission_probe_phase_ == kClipboardReadProbePhase ||
      site_permission_probe_phase_.starts_with("media-")) {
    const bool media = site_permission_probe_phase_.starts_with("media-");
    if (media && page->GetTitle() == u"Zitter media failed") {
      LOG(ERROR) << "The media fixture could not start a stream";
      Finish(false);
      return;
    }
    if (media && page->GetTitle() == u"Zitter media started") {
      if (site_permission_media_started_)
        return;
      site_permission_media_started_ = true;
      if (site_permission_probe_phase_ == "media-tab-other-audio-allow" &&
          (RunShortcut("sleep") || active_index_ != 0 || sleeping_[0])) {
        LOG(ERROR) << "Workspace slept while sharing a tab";
        Finish(false);
        return;
      }
      auto* frame = page->GetPrimaryMainFrame();
      if (site_permission_probe_phase_.starts_with("media-tab-other-audio-") &&
          media_access_->Check(frame, frame->GetLastCommittedOrigin(),
                              blink::mojom::MediaStreamType::DEVICE_AUDIO_CAPTURE)) {
        LOG(ERROR) << "Tab audio authorized access to the microphone";
        Finish(false);
        return;
      }
      if (site_permission_probe_phase_ == "media-tab-other-close") {
        CloseTab(1);
        return;
      }
      if (site_permission_probe_phase_.starts_with("media-tab-other-") &&
          site_permission_probe_phase_.ends_with("-navigation")) {
        auto* source = tabs_[active_index_][1].view->web_contents();
        content::NavigationController::LoadURLParams params(
            session_probe_url_.Resolve("/site-media-decoy"));
        source->GetController().LoadURLWithParams(params);
        return;
      }
      if (site_permission_probe_phase_.ends_with("-switch")) {
        auto previous = page->GetWeakPtr();
        SelectWorkspace(std::string(kWorkspaceIds[1]));
        CheckStoppedMediaProbe(std::move(previous),
                               base::TimeTicks::Now() + base::Seconds(5));
        return;
      }
      if (!media_access_->StopFromIndicatorForTesting()) {
        LOG(ERROR) << "The active stream did not expose its Stop control";
        Finish(false);
      }
      return;
    }
    if (media && (page->GetTitle() == u"Zitter media ended" ||
                  page->GetTitle() == u"Zitter media denied")) {
      const bool expect_ended = site_permission_probe_phase_.ends_with("-allow") ||
          site_permission_probe_phase_.starts_with("media-tab-other-");
      const bool valid = expect_ended
          ? site_permission_prompt_shown_ && site_permission_media_started_ &&
              page->GetTitle() == u"Zitter media ended"
          : page->GetTitle() == u"Zitter media denied" &&
              site_permission_prompt_shown_ ==
                  (site_permission_probe_phase_.ends_with("-deny") ||
                   site_permission_probe_phase_ == "media-tab-desktop-native-cancel");
      if (!valid) {
        LOG(ERROR) << "Unexpected media permission or capture outcome";
        Finish(false);
        return;
      }
      std::puts("Zitter Content site permissions probe passed");
      RequestClose();
      return;
    }
    if (!site_permission_probe_changed_ &&
        page->GetTitle() == (media ? u"Zitter media ready" : u"Zitter clipboard read ready")) {
      site_permission_probe_changed_ = true;
      widget_->Activate();
      ClickSitePermissionProbe(base::TimeTicks::Now() + base::Seconds(6));
    } else if (page->GetTitle() == u"Zitter clipboard read failed") {
      LOG(ERROR) << "Clipboard read did not return the fixture";
      Finish(false);
    } else if (page->GetTitle() == u"Zitter clipboard read passed" &&
               site_permission_prompt_shown_) {
      std::puts("Zitter Content site permissions probe passed");
      std::puts("Zitter Content read from the isolated macOS pasteboard");
      RequestClose();
    }
    return;
  }
  using Setting = ProfilePermissionController::ClipboardSetting;
  const bool work = site_permission_probe_profile_ == 0;
  const bool reset = site_permission_probe_phase_ == "reset";
  const bool changing = site_permission_probe_phase_ == "change";
  const bool prompting = site_permission_probe_phase_.starts_with("prompt-");
  const bool default_setting =
      prompting || site_permission_probe_phase_ == "other" ||
      (changing && !site_permission_probe_changed_) ||
      (work && reset && site_permission_probe_changed_);
  const Setting expected = default_setting ? Setting::kAsk
                                          : work ? Setting::kAllow : Setting::kBlock;
  std::u16string_view title;
  switch (expected) {
    case Setting::kAsk: title = u"Zitter site clipboard prompt/prompt"; break;
    case Setting::kAllow: title = u"Zitter site clipboard granted/prompt"; break;
    case Setting::kBlock: title = u"Zitter site clipboard denied/denied"; break;
  }
  if (site_permission_probe_phase_ == "policy" ||
      site_permission_probe_phase_ == "embedded") {
    title = u"Zitter site clipboard denied/denied";
  }
  if (page->GetTitle() != title)
    return;
  if (prompting) {
    if (!site_permission_probe_changed_)
      RunClipboardPromptProbe();
    return;
  }

  const auto origin = url::Origin::Create(expected_url);
  if (WorkspaceProfile(active_index_)->site_permissions().GetClipboardSetting(origin) != expected) {
    LOG(ERROR) << "The renderer and profile disagreed on the Clipboard setting";
    Finish(false);
    return;
  }
  if (!site_permission_probe_changed_) {
    shell_->ToggleSitePanel();
    auto* panel = shell_->site_panel_for_testing();
    if (!panel || !panel->IsVisible() || shell_->site_rows_for_testing() != 2 ||
        panel->color_mode_override() != widget_->color_mode_override()) {
      LOG(ERROR) << "The Clipboard permission row did not appear in the site panel";
      Finish(false);
      return;
    }
    if (changing || (reset && work)) {
      site_permission_probe_changed_ = true;
      shell_->SetSitePermissionForTesting(
          0, reset ? 10 : static_cast<int>(work ? Setting::kAllow : Setting::kBlock));
      return;
    }
  }
  shell_->CloseSitePanel();
  if (work) {
    ++site_permission_probe_profile_;
    site_permission_probe_changed_ = false;
    LoadSitePermissionProbe();
    return;
  }
  std::puts("Zitter Content site permissions probe passed");
  RequestClose();
}

void WorkspaceLab::ClickSitePermissionProbe(base::TimeTicks deadline) {
  auto* page = web_contents();
  if (!page || page->GetLastCommittedURL() != SitePermissionProbeUrl() ||
      (page->GetTitle() != u"Zitter media ready" &&
       page->GetTitle() != u"Zitter clipboard read ready")) {
    return;
  }
  if (base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "The permission fixture did not receive its click"
               << ", focused=" << page->GetPrimaryMainFrame()->IsFocused()
               << ", bounds=" << page->GetContainerBounds().ToString();
    Finish(false);
    return;
  }
  page->Focus();
  const bool other = site_permission_probe_phase_.starts_with("media-tab-other-");
  const bool has_decoy = other || site_permission_probe_phase_ == "media-tab-exclude";
  const bool decoy_ready = !has_decoy ||
      tabs_[1][selected_[1]].view->web_contents()->GetLastCommittedURL() ==
          session_probe_url_.Resolve("/site-media-decoy");
  const bool source_ready = !other ||
      tabs_[active_index_][1].view->web_contents()->GetLastCommittedURL() ==
          session_probe_url_.Resolve("/site-media-source");
  if (page->GetPrimaryMainFrame()->IsFocused() && decoy_ready && source_ready) {
    ClickProbePage(*page);
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::ClickSitePermissionProbe,
                               weak_factory_.GetWeakPtr(), deadline),
      base::Milliseconds(250));
}

void WorkspaceLab::CheckStoppedMediaProbe(
    base::WeakPtr<content::WebContents> previous, base::TimeTicks deadline) {
  if (!previous || active_index_ != 1 || base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "Capture did not stop after switching profiles";
    Finish(false);
    return;
  }
  if (site_permission_prompt_shown_ && previous->GetTitle() == u"Zitter media ended") {
    std::puts("Zitter Content site permissions probe passed");
    RequestClose();
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckStoppedMediaProbe,
                               weak_factory_.GetWeakPtr(), std::move(previous),
                               deadline),
      base::Milliseconds(50));
}

void WorkspaceLab::StartDownloadProbe() {
  if (session_path_.empty() || !session_probe_url_.is_valid()) {
    Finish(false);
    return;
  }
  const auto phase = base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
      kWorkspaceDownloadProbe);
  if (phase == "resume") {
    download_resume_probe_ = DownloadResumeProbe::kRetry;
  } else if (phase == "resume-save") {
    download_resume_probe_ = DownloadResumeProbe::kSave;
  } else if (phase == "resume-inspect") {
    download_resume_probe_ = DownloadResumeProbe::kInspect;
  } else if (phase == "resume-restore") {
    download_resume_probe_ = DownloadResumeProbe::kRestore;
  } else if (phase == "resume-reject") {
    download_resume_probe_ = DownloadResumeProbe::kReject;
  } else if (phase == "resume-abandon") {
    download_resume_probe_ = DownloadResumeProbe::kAbandon;
  } else if (phase == "delete-cancel") {
    download_delete_probe_ = DownloadDeleteProbe::kCancel;
  } else if (phase == "delete-confirm") {
    download_delete_probe_ = DownloadDeleteProbe::kConfirm;
  } else if (phase == "delete-reject") {
    download_delete_probe_ = DownloadDeleteProbe::kReject;
  } else if (phase == "close-wait" || phase == "close-quit") {
    download_close_probe_ = phase == "close-wait" ? DownloadCloseProbe::kWait
                                                 : DownloadCloseProbe::kQuit;
  } else if (!phase.empty()) {
    if (phase != "restore" && phase != "after-removal" && phase != "open" &&
        phase != "reveal" && phase != "open-invalid" && phase != "reveal-invalid" &&
        phase != "open-removed" &&
        phase != "clear-recent" && phase != "clear-all" && phase != "import-history") {
      Finish(false);
      return;
    }
    download_probe_active_ = true;
    SelectWorkspace(std::string(kWorkspaceIds[0]));
    CheckDownloadHistoryProbe(0, phase != "after-removal",
                              base::TimeTicks::Now() + base::Seconds(10));
    return;
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce([](const base::FilePath& root) {
        return std::ranges::all_of(kDownloadProbeFolders, [&](const char* name) {
          const auto folder = root.AppendASCII(name);
          return !base::IsLink(folder) && base::DirectoryExists(folder);
        });
      }, session_path_.DirName()),
      base::BindOnce(&WorkspaceLab::StartDownloadProbeTransfers,
                     weak_factory_.GetWeakPtr()));
}

void WorkspaceLab::CheckDownloadHistoryProbe(size_t profile,
                                             bool expect_work_entry,
                                             base::TimeTicks deadline) {
  auto* manager = contexts_[profile]->GetDownloadManager();
  if (!manager->IsManagerInitialized()) {
    if (base::TimeTicks::Now() >= deadline) {
      LOG(ERROR) << "The download manager did not restore its history";
      Finish(false);
      return;
    }
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&WorkspaceLab::CheckDownloadHistoryProbe,
                       weak_factory_.GetWeakPtr(), profile, expect_work_entry, deadline),
        base::Milliseconds(50));
    return;
  }
  if (active_index_ != profile || !OpenDownloads()) {
    Finish(false);
    return;
  }
  using Panel = prototype::DownloadPanel;
  const auto guids = downloads_->visible_guids();
  const auto phase = base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
      kWorkspaceDownloadProbe);
  if (phase == "clear-recent" || phase == "clear-all") {
    const int expected = phase == "clear-recent" ? 2 : 1;
    if (downloads_->count_for_testing(Panel::StateFilter::kDone) != expected) {
      LOG(ERROR) << "Could not restore downloads for the clear-data probe";
      Finish(false);
      return;
    }
    ConfirmClearDataProbe();
    return;
  }
  if (phase == "import-history") {
    const int failures = profile == 0 ? 2 : 0;
    if (guids.size() != static_cast<size_t>(failures + 1) ||
        downloads_->count_for_testing(Panel::StateFilter::kDone) != 1 ||
        downloads_->count_for_testing(Panel::StateFilter::kFailed) != failures ||
        !shell_->toast_text_for_testing().empty()) {
      LOG(ERROR) << "Unexpected imported download history for " << kNames[profile];
      Finish(false);
      return;
    }
    auto* delegate = static_cast<ProfileDownloadDelegate*>(
        contexts_[profile]->GetDownloadManagerDelegate());
    int interrupted = 0;
    int cancelled = 0;
    for (const auto& guid : guids) {
      auto* item = manager->GetDownloadByGuid(guid);
      if (!item ||
          item->GetURL() != session_probe_url_.Resolve(kDownloadProbePaths[profile]) ||
          !delegate->IsImportedHistoryEntry(*item) || item->CanResume() ||
          (item->GetState() != download::DownloadItem::COMPLETE &&
           !item->GetFullPath().empty()) ||
          downloads_->Act(guid, Panel::Action::kOpen) ||
          downloads_->Act(guid, Panel::Action::kReveal) ||
          downloads_->Act(guid, Panel::Action::kPauseResume) ||
          downloads_->Act(guid, Panel::Action::kCancel) ||
          downloads_->Act(guid, Panel::Action::kRetry) ||
          downloads_->Act(guid, Panel::Action::kDelete)) {
        LOG(ERROR) << "Imported download acquired file or transfer actions";
        Finish(false);
        return;
      }
      interrupted += item->GetState() == download::DownloadItem::INTERRUPTED;
      cancelled += item->GetState() == download::DownloadItem::CANCELLED;
    }
    if (interrupted != failures / 2 || cancelled != failures / 2) {
      Finish(false);
      return;
    }
    if (profile == 0) {
      SelectWorkspace(std::string(kWorkspaceIds[1]));
      CheckDownloadHistoryProbe(1, true, deadline);
    } else {
      std::puts("Zitter Content imported terminal download history");
      RequestClose();
    }
    return;
  }
  const bool expect_entry = profile != 0 || expect_work_entry;
  if (guids.size() != static_cast<size_t>(expect_entry) ||
      downloads_->count_for_testing(Panel::StateFilter::kDone) !=
          static_cast<int>(expect_entry) ||
      !shell_->toast_text_for_testing().empty()) {
    LOG(ERROR) << "Unexpected restored download history for " << kNames[profile];
    Finish(false);
    return;
  }
  if (expect_entry) {
    auto* item = manager->GetDownloadByGuid(guids.front());
    const auto expected_path = session_path_.DirName()
                                   .AppendASCII(kDownloadProbeFolders[profile])
                                   .AppendASCII("download-probe.txt");
    if (!item || item->GetURL() != session_probe_url_.Resolve(kDownloadProbePaths[profile]) ||
        item->GetTargetFilePath() != expected_path ||
        downloads_->Act(guids.front(), Panel::Action::kDelete)) {
      LOG(ERROR) << "Restored download did not match its owning profile";
      Finish(false);
      return;
    }
    if (profile == 0 && (phase.starts_with("open") || phase.starts_with("reveal"))) {
      const auto action = phase.starts_with("open") ? DownloadFileAction::kOpen
                                                   : DownloadFileAction::kReveal;
      auto* delegate = static_cast<ProfileDownloadDelegate*>(
          contexts_[profile]->GetDownloadManagerDelegate());
      delegate->SetFileActionForTesting(base::BindRepeating(
          [](base::WeakPtr<WorkspaceLab> lab, DownloadFileAction expected_action,
             const base::FilePath& expected_path, DownloadFileAction action,
             const base::FilePath& path) {
            if (!lab)
              return;
            if (action != expected_action || path != expected_path) {
              LOG(ERROR) << "The download shell action used an unexpected path";
              lab->Finish(false);
              return;
            }
            std::puts("Zitter Content workspace download file action passed");
            lab->RequestClose();
          },
          weak_factory_.GetWeakPtr(), action,
          phase == "open-removed" || phase.ends_with("-invalid")
              ? base::FilePath() : expected_path));
      if (!downloads_->Act(guids.front(), action == DownloadFileAction::kOpen
                                             ? Panel::Action::kOpen
                                             : Panel::Action::kReveal)) {
        Finish(false);
        return;
      }
      if (phase == "open-removed")
        item->Remove();
      return;
    }
  }
  if (profile == 0) {
    if (expect_work_entry &&
        !downloads_->Act(guids.front(), Panel::Action::kRemove)) {
      Finish(false);
      return;
    }
    // Disconnecting the panel commits removal from Work without deleting its file.
    SelectWorkspace(std::string(kWorkspaceIds[1]));
    CheckDownloadHistoryProbe(1, expect_work_entry,
                              base::TimeTicks::Now() + base::Seconds(10));
    return;
  }
  std::puts("Zitter Content workspace download history passed");
  RequestClose();
}

void WorkspaceLab::StartDownloadProbeTransfers(bool folders_valid) {
  if (!folders_valid) {
    Finish(false);
    return;
  }
  download_probe_active_ = true;
  for (size_t index = 0; index < contexts_.size(); ++index) {
    contexts_[index]->ConfigureDownloadProbe(
        session_probe_url_.Resolve(kDownloadProbePaths[index]),
        session_path_.DirName().AppendASCII(kDownloadProbeFolders[index]));
  }
  SelectWorkspace(std::string(kWorkspaceIds[0]));
  const bool use_history =
      download_delete_probe_ != DownloadDeleteProbe::kNone ||
      download_resume_probe_ == DownloadResumeProbe::kInspect ||
      download_resume_probe_ == DownloadResumeProbe::kRestore ||
      download_resume_probe_ == DownloadResumeProbe::kReject ||
      download_resume_probe_ == DownloadResumeProbe::kAbandon;
  if (!OpenDownloads() ||
      (!use_history &&
       (!downloads_->visible_guids().empty() ||
        !Navigate(session_probe_url_.Resolve(kDownloadProbePaths[0]), false)))) {
    LOG(ERROR) << "Could not start the Work download";
    Finish(false);
    return;
  }
  if (download_close_probe_ != DownloadCloseProbe::kNone)
    CheckDownloadCloseProbe(base::TimeTicks::Now() + base::Seconds(15));
  if (download_resume_probe_ != DownloadResumeProbe::kNone)
    CheckDownloadResumeProbe(base::TimeTicks::Now() + base::Seconds(15));
  if (download_delete_probe_ != DownloadDeleteProbe::kNone)
    CheckDownloadDeleteProbe(base::TimeTicks::Now() + base::Seconds(15));
}

void WorkspaceLab::CheckDownloadDeleteProbe(base::TimeTicks deadline) {
  auto* manager = contexts_[0]->GetDownloadManager();
  if (!manager->IsManagerInitialized()) {
    if (base::TimeTicks::Now() >= deadline) {
      Finish(false);
      return;
    }
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, base::BindOnce(&WorkspaceLab::CheckDownloadDeleteProbe,
                                  weak_factory_.GetWeakPtr(), deadline),
        base::Milliseconds(50));
    return;
  }

  const auto guids = downloads_->visible_guids();
  if (guids.size() != 1) {
    Finish(false);
    return;
  }
  download_probe_work_guid_ = guids.front();
  if (download_delete_probe_ == DownloadDeleteProbe::kConfirm) {
    downloads_->SetChoosing(true);
    downloads_->Choose(guids.front(), true);
    downloads_->DeleteChosen();
  } else if (!downloads_->Act(guids.front(),
                              prototype::DownloadPanel::Action::kDelete)) {
    Finish(false);
    return;
  }
  auto* prompt = shell_->danger_dialog_for_testing();
  auto* dialog = prompt ? prompt->widget_delegate()->AsDialogDelegate() : nullptr;
  if (!dialog || !prompt->IsVisible() ||
      prompt->color_mode_override() != widget_->color_mode_override() ||
      dialog->GetDefaultDialogButton() != static_cast<int>(ui::mojom::DialogButton::kCancel)) {
    Finish(false);
    return;
  }
  if (download_delete_probe_ == DownloadDeleteProbe::kCancel) {
    dialog->CancelDialog();
    FinishDownloadDeleteProbe(false);
    return;
  }
  dialog->AcceptDialog();
}

void WorkspaceLab::FinishDownloadDeleteProbe(bool deleted) {
  const bool expect_deleted = download_delete_probe_ == DownloadDeleteProbe::kConfirm;
  auto* item = contexts_[0]->GetDownloadManager()->GetDownloadByGuid(
      download_probe_work_guid_);
  if (deleted != expect_deleted || (item != nullptr) == expect_deleted ||
      (expect_deleted && active_index_ != 1) || pending_download_deletions_) {
    LOG(ERROR) << "Unexpected download deletion result";
    Finish(false);
    return;
  }
  std::puts("Zitter Content workspace download deletion passed");
  RequestClose();
}

void WorkspaceLab::CheckDownloadResumeProbe(base::TimeTicks deadline) {
  auto* manager = contexts_[0]->GetDownloadManager();
  content::DownloadManager::DownloadVector items;
  manager->GetAllDownloads(&items);
  if (manager->IsManagerInitialized() &&
      download_resume_probe_ == DownloadResumeProbe::kReject) {
    if (!items.empty() || !downloads_->visible_guids().empty()) {
      LOG(ERROR) << "An unapproved interrupted download was restored";
      Finish(false);
      return;
    }
    std::puts("Zitter Content rejected an interrupted download record");
    RequestClose();
    return;
  }
  if (manager->IsManagerInitialized() && items.size() == 1 &&
      items.front()->GetState() == download::DownloadItem::INTERRUPTED &&
      items.front()->CanResume()) {
    const auto* item = items.front().get();
    download_probe_work_guid_ = item->GetGuid();
    if (item->GetReceivedBytes() <= 0 || item->GetHash().empty() ||
        downloads_->visible_guids() != std::vector<std::string>{item->GetGuid()}) {
      Finish(false);
      return;
    }
    if (download_resume_probe_ == DownloadResumeProbe::kSave ||
        download_resume_probe_ == DownloadResumeProbe::kInspect) {
      std::puts(download_resume_probe_ == DownloadResumeProbe::kSave
                    ? "Zitter Content checkpointed an interrupted download"
                    : "Zitter Content restored an interrupted download without requesting it");
      RequestClose();
      return;
    }
    if (!downloads_->Act(download_probe_work_guid_,
                         prototype::DownloadPanel::Action::kRetry)) {
      LOG(ERROR) << "The download panel could not resume its interrupted item";
      Finish(false);
      return;
    }
    if (download_resume_probe_ == DownloadResumeProbe::kAbandon) {
      download_close_probe_ = DownloadCloseProbe::kQuit;
      CheckDownloadCloseProbe(deadline);
    }
    return;
  }
  if (base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "The download never became available for manual resumption";
    Finish(false);
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&WorkspaceLab::CheckDownloadResumeProbe,
                     weak_factory_.GetWeakPtr(), deadline),
      base::Milliseconds(50));
}

void WorkspaceLab::CheckDownloadCloseProbe(base::TimeTicks deadline) {
  content::DownloadManager::DownloadVector items;
  contexts_[0]->GetDownloadManager()->GetAllDownloads(&items);
  if (items.size() != 1 || items.front()->GetReceivedBytes() <= 0 ||
      !items.front()->GetResponseHeaders() ||
      contexts_[0]->BlockingDownloadCount() != 1) {
    if (base::TimeTicks::Now() >= deadline) {
      LOG(ERROR) << "The close probe never received partial download data";
      Finish(false);
      return;
    }
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&WorkspaceLab::CheckDownloadCloseProbe,
                       weak_factory_.GetWeakPtr(), deadline),
        base::Milliseconds(50));
    return;
  }

  download_probe_work_guid_ = items.front()->GetGuid();
  const bool quitting = download_close_probe_ == DownloadCloseProbe::kQuit;
  if (quitting) {
    items.front()->Pause();
    if (!items.front()->IsPaused()) {
      Finish(false);
      return;
    }
  }
  SelectWorkspace(std::string(kWorkspaceIds[1]));
  if (quitting && quit_from_app_for_testing_) {
    std::move(quit_from_app_for_testing_).Run();
  } else {
    widget_->Close();
  }
  auto* prompt = shell_->quit_dialog_for_testing();
  RequestClose();
  if (active_index_ != 1 || contexts_[0]->BlockingDownloadCount() != 1 ||
      contexts_[1]->BlockingDownloadCount() != 0 || !prompt ||
      prompt != shell_->quit_dialog_for_testing() || !prompt->IsVisible() ||
      prompt->color_mode_override() != widget_->color_mode_override()) {
    LOG(ERROR) << "Closing did not show one prompt for the background download";
    Finish(false);
    return;
  }

  auto* dialog = prompt->widget_delegate()->AsDialogDelegate();
  if (!dialog || dialog->GetDefaultDialogButton() !=
                     static_cast<int>(ui::mojom::DialogButton::kCancel)) {
    Finish(false);
    return;
  }
  if (quitting) {
    std::puts("Zitter Content confirmed close during a download");
    dialog->AcceptDialog();
    return;
  }

  prompt->Close();
  if (download_close_prompt_pending_ || shell_->quit_dialog_for_testing()) {
    LOG(ERROR) << "Dismissing the download prompt left a close pending";
    Finish(false);
    return;
  }
  RequestClose();
  prompt = shell_->quit_dialog_for_testing();
  if (!prompt || !prompt->IsVisible()) {
    Finish(false);
    return;
  }
  prompt->widget_delegate()->AsDialogDelegate()->CancelDialog();
  if (download_close_prompt_pending_ || shell_->quit_dialog_for_testing() ||
      !widget_->IsVisible() || contexts_[0]->BlockingDownloadCount() != 1) {
    LOG(ERROR) << "Waiting did not preserve the active download";
    Finish(false);
    return;
  }
  SelectWorkspace(std::string(kWorkspaceIds[0]));
  if (active_index_ != 0 || !OpenDownloads()) {
    Finish(false);
    return;
  }
  std::puts("Zitter Content waited for an active download");
  std::fflush(stdout);
}

void WorkspaceLab::AdvanceDownloadProbe(std::string guid) {
  using Panel = prototype::DownloadPanel;
  if (!downloads_->widget() || !downloads_->widget()->IsVisible() ||
      downloads_->visible_guids() != std::vector<std::string>{guid} ||
      downloads_->count_for_testing(Panel::StateFilter::kDone) != 1) {
    LOG(ERROR) << "The active download panel did not show exactly one completed item";
    Finish(false);
    return;
  }
  if (download_resume_probe_ != DownloadResumeProbe::kNone) {
    if (guid != download_probe_work_guid_) {
      Finish(false);
      return;
    }
    std::puts("Zitter Content resumed the interrupted Workspace download");
    RequestClose();
    return;
  }
  if (download_close_probe_ != DownloadCloseProbe::kNone) {
    if (download_close_probe_ != DownloadCloseProbe::kWait ||
        guid != download_probe_work_guid_ ||
        contexts_[0]->BlockingDownloadCount() != 0) {
      Finish(false);
      return;
    }
    RequestClose();
    if (shell_->quit_dialog_for_testing()) {
      Finish(false);
      return;
    }
    std::puts("Zitter Content finished the download and closed without prompting");
    return;
  }
  if (download_probe_work_guid_.empty()) {
    if (shell_->toast_text_for_testing().find(u"download-probe.txt") ==
        std::u16string::npos) {
      LOG(ERROR) << "Work did not show the download completion notice";
      Finish(false);
      return;
    }
    download_probe_work_guid_ = std::move(guid);
    downloads_->SetQuery(u"download-probe.txt");
    SelectWorkspace(std::string(kWorkspaceIds[1]));
    if (active_index_ != 1 || downloads_->widget() ||
        !shell_->toast_text_for_testing().empty() ||
        shell_->download_cue_widget_for_testing()->IsVisible() ||
        !OpenDownloads() || !downloads_->search_for_testing()->GetText().empty() ||
        !downloads_->visible_guids().empty() ||
        downloads_->Act(download_probe_work_guid_, Panel::Action::kOpen) ||
        downloads_->Act(download_probe_work_guid_, Panel::Action::kReveal) ||
        downloads_->Act(download_probe_work_guid_, Panel::Action::kDelete) ||
        !Navigate(session_probe_url_.Resolve(kDownloadProbePaths[1]), false)) {
      LOG(ERROR) << "The Personal download panel was not isolated from Work";
      Finish(false);
    }
    return;
  }

  SelectWorkspace(std::string(kWorkspaceIds[0]));
  if (active_index_ != 0 || downloads_->widget() || !OpenDownloads() ||
      downloads_->visible_guids() !=
          std::vector<std::string>{download_probe_work_guid_} ||
      downloads_->Act(guid, Panel::Action::kRemove)) {
    LOG(ERROR) << "Work did not recover only its own download";
    Finish(false);
    return;
  }
  std::puts("Zitter Content workspace download panel isolation passed");
  RequestClose();
}

bool WorkspaceLab::OpenHistory() {
  if (saving_session_ || before_unload_batch_.pending())
    return false;
  auto* history = WorkspaceProfile(active_index_)->GetHistory();
  if (!history)
    return false;
  shell_->CloseOverlays(true);
  if (!history_panel_) {
    history_panel_ = std::make_unique<prototype::HistoryPanel>();
    history_panel_->DisableAuxiliaryOpenModes();
    if (timeline_) {
      history_panel_->SetTimeMachineCallback(base::BindRepeating(
          [](base::WeakPtr<WorkspaceLab> lab) {
            if (lab)
              lab->RunShortcut("timeline");
          }, weak_factory_.GetWeakPtr()));
    }
    history_panel_->SetClosedSource(base::BindRepeating(
        [](base::WeakPtr<WorkspaceLab> lab) {
          std::vector<prototype::HistoryPanel::Closed> result;
          if (!lab)
            return result;
          const auto& tabs = lab->closed_tabs_[lab->active_index_];
          const auto workspace = base::UTF8ToUTF16(lab->WorkspaceProfileName(lab->active_index_));
          result.reserve(tabs.size());
          for (const auto& tab : base::Reversed(tabs))
            result.push_back({tab.title, tab.url, workspace, tab.closed_at});
          return result;
        }, weak_factory_.GetWeakPtr()));
    history_panel_->SetReopenCallback(base::BindRepeating(
        [](base::WeakPtr<WorkspaceLab> lab, size_t newest) {
          if (lab)
            lab->ReopenClosedTab(newest);
        }, weak_factory_.GetWeakPtr()));
    history_panel_->SetOpenCallback(base::BindRepeating(
        [](base::WeakPtr<WorkspaceLab> lab, GURL url,
           prototype::HistoryPanel::Open mode) {
          if (!lab)
            return;
          const auto source = lab->model_.active()->id;
          if (mode == prototype::HistoryPanel::Open::kPeek)
            lab->OpenPeek(url, source);
          else
            lab->OpenLink(url, source, true, mode == prototype::HistoryPanel::Open::kSplit);
        },
        weak_factory_.GetWeakPtr()));
    history_panel_->SetConfirmCallback(base::BindRepeating(
        [](base::WeakPtr<WorkspaceLab> lab, views::Widget* parent,
           std::u16string title, std::u16string detail, std::u16string warning,
           std::u16string action, base::OnceCallback<void(bool)> answer) {
          if (lab) {
            lab->shell_->ConfirmDanger(parent, std::move(title), std::move(detail),
                                       std::move(warning), std::move(action),
                                       std::move(answer));
          }
        },
        weak_factory_.GetWeakPtr()));
    history_panel_->SetClearDataCallback(base::BindRepeating(
        [](base::WeakPtr<WorkspaceLab> lab) {
          if (lab && lab->history_panel_)
            lab->OpenClearDataDialog(lab->history_panel_->widget());
        },
        weak_factory_.GetWeakPtr()));
  }
  history_panel_->Show(history, base::UTF8ToUTF16(WorkspaceProfileName(active_index_)),
                       widget_.get(), /*activate=*/!session_probe_url_.is_valid());
  return true;
}

void WorkspaceLab::SearchAddress(std::u16string query) {
  address_history_tasks_.TryCancelAll();
  const auto profile_id = model_.active()->profile_id;
  auto* service = query.starts_with(u">") ||
                          (clearing_profile_ && WorkspaceProfile(*clearing_profile_) ==
                                                    WorkspaceProfile(active_index_))
                      ? nullptr
                      : WorkspaceProfile(active_index_)->GetHistory();
  if (!service) {
    shell_->SetSearchResults(std::move(query), profile_id, {});
    return;
  }

  history::QueryOptions options;
  options.max_count = 12;
  options.policy_for_404_visits = history::VisitQuery404sPolicy::kExclude404s;
  service->QueryHistory(
      query, options,
      base::BindOnce(
          [](base::WeakPtr<WorkspaceLab> lab, size_t profile,
             std::u16string query, history::QueryResults rows) {
            if (!lab || lab->active_index_ != profile)
              return;
            std::set<GURL> seen;
            std::vector<prototype::PaletteResult> results;
            for (const auto& row : rows) {
              const auto& url = row.url();
              if (!url.SchemeIsHTTPOrHTTPS() || url.has_username() ||
                  url.has_password() || !seen.insert(url).second) {
                continue;
              }
              auto host = base::UTF8ToUTF16(url.host());
              auto detail = sidebar::palette_result_detail(
                  sidebar::PaletteResultKind::History, std::string(url.host()));
              results.push_back(
                  {row.title().empty() ? std::move(host) : row.title(),
                   base::UTF8ToUTF16(std::string(detail)), url, std::nullopt});
            }
            lab->shell_->SetSearchResults(
                std::move(query), lab->model_.active()->profile_id,
                std::move(results));
          },
          weak_factory_.GetWeakPtr(), active_index_, query),
      &address_history_tasks_);
}

void WorkspaceLab::RequestClearData(BrowsingDataSelection selection) {
  using Event = prototype::WorkspaceShell::ClearDataEvent;
  if (clearing_profile_ || replacing_timeline_ || saving_session_ || saving_catalogue_ ||
      before_unload_batch_.pending()) {
    shell_->ReportClearData(Event::kBusy, {});
    return;
  }
  const size_t profile = active_index_;
  if (WorkspaceProfile(profile)->IsOffTheRecord()) {
    shell_->ReportClearData(Event::kFailed, {});
    return;
  }
  const auto copy = shell_->ClearDataConfirmCopy(
      base::UTF8ToUTF16(WorkspaceProfileName(profile)), selection.period, selection.history,
      selection.cookies, selection.cache,
      timeline_ && selection.time_machine.value_or(selection.history));
  if (!HasClearableData(selection) || !copy.valid ||
      (selection.time_machine.value_or(false) && !timeline_)) {
    shell_->ReportClearData(Event::kNothingSelected, {});
    return;
  }
  shell_->ConfirmDanger(
      shell_->general_widget(), copy.title, copy.description, {}, copy.confirm,
      base::BindOnce(
          [](base::WeakPtr<WorkspaceLab> lab, size_t profile,
             BrowsingDataSelection selection, bool accepted) {
            if (lab && accepted)
              lab->ClearData(profile, selection);
          },
          weak_factory_.GetWeakPtr(), profile, selection));
}

void WorkspaceLab::OpenClearDataDialog(views::Widget* parent) {
  const size_t profile = active_index_;
  if (model_.active() && !clearing_profile_ && !replacing_timeline_ &&
      !WorkspaceProfile(profile)->IsOffTheRecord()) {
    shell_->ShowClearDataDialog(
        parent, base::UTF8ToUTF16(WorkspaceProfileName(profile)),
        base::BindRepeating(
            [](base::WeakPtr<WorkspaceLab> lab, size_t workspace,
               const std::string& profile_id, BrowsingDataSelection selection) {
              if (lab && lab->model_.active() &&
                  lab->model_.active()->profile_id == profile_id) {
                lab->ClearData(workspace, selection);
              }
            },
            weak_factory_.GetWeakPtr(), profile, model_.active()->profile_id),
        /*allow_time_machine=*/timeline_ != nullptr,
        /*allow_site_settings=*/WorkspaceProfile(profile)->site_permissions()
            .CanClearSitePermissions(),
        /*allow_download_history=*/true,
        /*allow_passwords=*/WorkspaceProfile(profile)->GetPasswordStore() != nullptr);
  }
}

void WorkspaceLab::ClearData(size_t profile, BrowsingDataSelection selection) {
  using Event = prototype::WorkspaceShell::ClearDataEvent;
  using Remover = content::BrowsingDataRemover;
  if (profile != active_index_ || clearing_profile_ || replacing_timeline_ ||
      saving_session_ || saving_catalogue_ || before_unload_batch_.pending() ||
      WorkspaceProfile(profile)->IsOffTheRecord() || !HasClearableData(selection) ||
      (selection.time_machine.value_or(false) && !timeline_) ||
      (selection.site_settings &&
       !WorkspaceProfile(profile)->site_permissions().CanClearSitePermissions()) ||
      (selection.passwords && !WorkspaceProfile(profile)->GetPasswordStore())) {
    shell_->ReportClearData(Event::kFailed, {});
    if (clear_data_probe_pending_)
      Finish(false);
    return;
  }
  clearing_profile_ = profile;
  // An earlier undo must not restore browsing records cleared here.
  if (selection.history || selection.time_machine.value_or(false))
    shell_->HideToast();
  address_history_tasks_.TryCancelAll();
  if (shell_->address_visible())
    shell_->CloseOverlays(true);
  shell_->SetBrowsingDataBusy(true);
  shell_->ReportClearData(Event::kClearing, base::UTF8ToUTF16(WorkspaceProfileName(profile)));
  const std::array<base::TimeDelta, 5> periods = {
      base::Hours(1), base::Days(1), base::Days(7), {}, base::Days(28)};
  const auto period = periods[selection.period];
  const auto begin = period.is_zero() ? base::Time() : base::Time::Now() - period;
  if (selection.site_settings) {
    auto* context = WorkspaceProfile(profile);
    context->site_permissions().ClearSitePermissions(begin, base::Time::Max());
    content::HostZoomMap::GetDefaultForBrowserContext(context)
        ->ClearZoomLevels(begin, base::Time::Max());
    shell_->CloseSitePanel();
  }
  uint64_t mask = selection.cache ? Remover::DATA_TYPE_CACHE : 0;
  if (selection.downloads)
    mask |= Remover::DATA_TYPE_DOWNLOADS;
  if (selection.cookies) {
    mask |= Remover::DATA_TYPE_COOKIES | Remover::DATA_TYPE_DOM_STORAGE |
            Remover::DATA_TYPE_MEDIA_LICENSES | Remover::DATA_TYPE_PRIVACY_SANDBOX |
            Remover::DATA_TYPE_DEVICE_BOUND_SESSIONS |
            Remover::DATA_TYPE_DECLARATIVE_PERFORMANCE_OBSERVER;
  }
  if (timeline_ && selection.time_machine.value_or(selection.history)) {
    bool timeline_changed = false;
    for (size_t index = 0; index < tabs_.size(); ++index) {
      if (WorkspaceProfile(index) != WorkspaceProfile(profile))
        continue;
      std::erase_if(closed_tabs_[index], [begin](const ClosedTab& tab) {
        return tab.closed_at >= begin;
      });
      timeline_changed |= timeline_->current->timeline_forget_workspace(
          model_.workspaces()[index].id, begin.InMillisecondsSinceUnixEpoch(),
          base::Time::Max().InMillisecondsSinceUnixEpoch());
    }
    if (history_panel_)
      history_panel_->RefreshClosedTabs();
    if (timeline_changed || timeline_dirty_) {
      PersistTimeline(base::BindOnce(&WorkspaceLab::ContinueClearData,
                                     weak_factory_.GetWeakPtr(),
                                     selection.history, selection.passwords, begin, mask));
      return;
    }
  }
  ContinueClearData(selection.history, selection.passwords, begin, mask, true);
}

void WorkspaceLab::ContinueClearData(bool history, bool passwords, base::Time begin,
                                     uint64_t mask, bool timeline_saved) {
  clear_data_failed_ = !timeline_saved;
  if (passwords) {
    password_clear_ = std::make_unique<WorkspacePasswordClear>(
        *WorkspaceProfile(*clearing_profile_)->GetPasswordStore(), begin, base::Time::Max(),
        base::BindOnce(
            [](base::WeakPtr<WorkspaceLab> lab, bool history, base::Time begin,
               uint64_t mask, bool cleared) {
              if (!lab)
                return;
              lab->password_clear_.reset();
              lab->clear_data_failed_ |= !cleared;
              lab->ClearHistoryAndStorage(history, begin, mask);
            },
            weak_factory_.GetWeakPtr(), history, begin, mask));
    password_clear_->Start();
    return;
  }
  ClearHistoryAndStorage(history, begin, mask);
}

void WorkspaceLab::ClearHistoryAndStorage(bool history, base::Time begin,
                                           uint64_t mask) {
  if (history) {
    WorkspaceProfile(*clearing_profile_)->GetHistory()->ExpireHistoryBetween(
        {}, std::nullopt, begin, base::Time::Max(), /*user_initiated=*/true,
        base::BindOnce(&WorkspaceLab::ClearStorageData,
                       weak_factory_.GetWeakPtr(), begin, mask),
        &history_removal_tasks_);
  } else {
    ClearStorageData(begin, mask);
  }
}

void WorkspaceLab::ClearStorageData(base::Time begin, uint64_t mask) {
  using Remover = content::BrowsingDataRemover;
  if (!mask) {
    OnBrowsingDataRemoverDone(0);
    return;
  }
  auto* remover = WorkspaceProfile(*clearing_profile_)->GetBrowsingDataRemover();
  data_remover_.Observe(remover);
  remover->RemoveAndReply(begin, base::Time::Max(), mask,
                          Remover::ORIGIN_TYPE_UNPROTECTED_WEB |
                              Remover::ORIGIN_TYPE_PROTECTED_WEB,
                          this);
}

void WorkspaceLab::OnBrowsingDataRemoverDone(uint64_t failed_data_types) {
  using Event = prototype::WorkspaceShell::ClearDataEvent;
  data_remover_.Reset();
  const auto profile = *clearing_profile_;
  clearing_profile_.reset();
  favicon_tasks_.TryCancelAll();
  cached_favicons_.clear();
  RefreshShell();
  shell_->SetBrowsingDataBusy(false);
  const bool failed = std::exchange(clear_data_failed_, false) || failed_data_types;
  shell_->ReportClearData(failed ? Event::kFailed : Event::kCleared,
                          base::UTF8ToUTF16(WorkspaceProfileName(profile)));
  if (clear_data_probe_pending_) {
    clear_data_probe_pending_ = false;
    if (failed) {
      LOG(ERROR) << "Clear-data probe failed for mask " << failed_data_types;
      Finish(false);
      return;
    }
    const auto phase = base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
        kWorkspaceClearDataProbe);
    if (phase == "site-recent" || phase == "site-all") {
      using Permissions = ProfilePermissionController;
      auto matches = [](ProfileContext& context, std::string_view host,
                        bool retained) {
        const auto origin = url::Origin::Create(GURL("https://" + std::string(host)));
        auto& permissions = context.site_permissions();
        return permissions.GetPopupDefault() &&
               permissions.GetClipboardSetting(origin) ==
                   (retained ? Permissions::ClipboardSetting::kAllow
                             : Permissions::ClipboardSetting::kAsk) &&
               permissions.GetPopupSetting(origin) ==
                   (retained ? Permissions::PopupSetting::kBlock
                             : Permissions::PopupSetting::kAllow) &&
               content::HostZoomMap::GetDefaultForBrowserContext(&context)
                   ->HasZoomLevel("https", std::string(host)) == retained;
      };
      if (!matches(*contexts_[0], "recent.example", false) ||
          !matches(*contexts_[0], "old.example", phase == "site-recent") ||
          !matches(*contexts_[1], "recent.example", true) ||
          !matches(*contexts_[1], "old.example", true)) {
        LOG(ERROR) << "Site settings clear did not respect profile and period";
        Finish(false);
        return;
      }
      std::puts("Zitter Content cleared Work site settings");
    }
    const auto download_phase =
        base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(kWorkspaceDownloadProbe);
    if (download_phase == "clear-recent" || download_phase == "clear-all") {
      const int remaining = download_phase == "clear-recent" ? 1 : 0;
      if (!downloads_ ||
          downloads_->count_for_testing(prototype::DownloadPanel::StateFilter::kDone) != remaining) {
        LOG(ERROR) << "Cleared downloads are still visible";
        Finish(false);
        return;
      }
      std::puts("Zitter Content cleared Work download history");
    }
    if (phase == "password-recent" || phase == "password-all")
      std::puts("Zitter Content cleared Work saved passwords");
    std::puts("Zitter Content cleared Work browsing data");
    SelectWorkspace(std::string(kWorkspaceIds[1]));
    RequestClose();
  }
}

void WorkspaceLab::OpenDevTools(base::OnceClosure ready) {
  auto* view = static_cast<WorkspaceWebView*>(
      tabs_[active_index_][selected_[active_index_]].view.get());
  // WorkspaceLab closes the tools before removing or replacing the tab view.
  devtools_ = std::make_unique<WorkspaceDevTools>(
      devtools_path_, *view->web_contents(),
      base::BindRepeating(&WorkspaceWebView::SetDevTools,
                          base::Unretained(view)),
      std::move(ready));
}

bool WorkspaceLab::OpenLink(const GURL& url, std::string source,
                            bool new_tab, bool split) {
  if (smoke_ || !CanEditWorkspace() || !model_.active() ||
      !url.SchemeIsHTTPOrHTTPS() || !CanRestoreWorkspaceURL(url) ||
      url.spec().size() > 32768) {
    return false;
  }
  const auto* rule = link_rules_.FindString(url.host());
  const auto target = WorkspaceIndex(rule ? *rule : source).value_or(active_index_);
  const bool add = new_tab || split || target != active_index_;
  if (add && tabs_[target].size() >= prototype::kMaxWorkspaceSessionTabs)
    return false;

  if (DeferPeekClose(base::BindOnce(
          [](base::WeakPtr<WorkspaceLab> lab, GURL url, std::string source,
             bool new_tab, bool split) {
            if (lab)
              lab->OpenLink(url, std::move(source), new_tab, split);
          }, weak_factory_.GetWeakPtr(), url, source, new_tab, split))) {
    return true;
  }
  DiscardPeek();
  SelectWorkspace(model_.workspaces()[target].id);
  if (active_index_ != target)
    return false;
  if (!add)
    return Navigate(url, false);

  AddTab(target, url);
  const size_t opened = tabs_[target].size() - 1;
  if (split)
    return SplitTab(opened);
  SelectTab(opened);
  PersistWorkspaceChanges();
  return true;
}

content::WebContents* WorkspaceLab::PeekPage() const {
  return peek_tab_ ? peek_tab_->view->web_contents() : nullptr;
}

bool WorkspaceLab::OpenPeek(const GURL& url, std::string source) {
  if (smoke_ || !CanEditWorkspace() || !model_.active() ||
      !url.SchemeIsHTTPOrHTTPS() || !CanRestoreWorkspaceURL(url) ||
      url.spec().size() > 32768) {
    return false;
  }
  const auto* rule = link_rules_.FindString(url.host());
  const auto target = WorkspaceIndex(rule ? *rule : source).value_or(active_index_);
  if (DeferPeekClose(base::BindOnce(
          [](base::WeakPtr<WorkspaceLab> lab, GURL url, std::string source) {
            if (lab)
              lab->OpenPeek(url, std::move(source));
          }, weak_factory_.GetWeakPtr(), url, source))) {
    return true;
  }
  DiscardPeek();

  peek_workspace_id_ = model_.workspaces()[target].id;
  peek_tab_ = MakeTab(target, *shell_->peek_host());
  auto* view = static_cast<WorkspaceWebView*>(peek_tab_->view.get());
  auto* page = view->GetWebContents(url);
  AttachPageServices(*page, target);
  view->SetNavigationChangedCallback(base::BindRepeating(
      &WorkspaceLab::UpdatePeekTitle, weak_factory_.GetWeakPtr()));
  shell_->ShowPeek(base::UTF8ToUTF16(
      model_.workspaces()[target].name + " · " + std::string(url.host())));
  view->SetVisible(true);
  page->UpdateWebContentsVisibility(content::Visibility::VISIBLE);
  prototype::LoadWorkspaceTab(page, url, ui::PAGE_TRANSITION_LINK);
  view->RequestFocus();
  return true;
}

void WorkspaceLab::UpdatePeekTitle(content::WebContents* page) {
  if (page != PeekPage())
    return;
  const auto* workspace = model_.Find(peek_workspace_id_);
  if (workspace && page->GetLastCommittedURL().SchemeIsHTTPOrHTTPS()) {
    shell_->SetPeekTitle(base::UTF8ToUTF16(
        workspace->name + " · " + std::string(page->GetLastCommittedURL().host())));
  }
}

bool WorkspaceLab::DeferPeekClose(base::OnceClosure follow_up) {
  auto* page = PeekPage();
  if (!page || !page->NeedToFireBeforeUnload())
    return false;
  if (fullscreen_page_.get() == page)
    ExitPageFullscreen();
  CheckBeforeUnload({page}, base::BindOnce(
      [](base::WeakPtr<WorkspaceLab> lab, base::OnceClosure follow_up) {
        if (!lab)
          return;
        lab->DiscardPeek();
        if (follow_up)
          std::move(follow_up).Run();
        else {
          lab->widget_->Activate();
          lab->tabs_[lab->active_index_][lab->selected_[lab->active_index_]].view->RequestFocus();
        }
      }, weak_factory_.GetWeakPtr(), std::move(follow_up)));
  return true;
}

void WorkspaceLab::DiscardPeek() {
  if (!peek_tab_)
    return;
  if (fullscreen_page_ && fullscreen_page_.get() == PeekPage())
    ExitPageFullscreen();
  auto tab = std::move(*peek_tab_);
  peek_tab_.reset();
  peek_workspace_id_.clear();
  shell_->HidePeek();
  HideTabView(*tab.view);
  tab.detached_view = tab.view->parent()->RemoveChildViewT(tab.view.get());
}

void WorkspaceLab::ClosePeek() {
  if (!CanEditWorkspace() || !PeekPage() || DeferPeekClose({}))
    return;
  DiscardPeek();
  widget_->Activate();
  tabs_[active_index_][selected_[active_index_]].view->RequestFocus();
}

bool WorkspaceLab::PromotePeek(bool split) {
  const auto target = WorkspaceIndex(peek_workspace_id_);
  if (!CanEditWorkspace() || !PeekPage() || !target ||
      tabs_[*target].size() >= prototype::kMaxWorkspaceSessionTabs) {
    return false;
  }

  if (fullscreen_page_.get() == PeekPage())
    ExitPageFullscreen();
  auto workspace = std::exchange(peek_workspace_id_, {});
  auto tab = std::move(*peek_tab_);
  peek_tab_.reset();
  shell_->HidePeek();
  HideTabView(*tab.view);
  tab.detached_view = shell_->peek_host()->RemoveChildViewT(tab.view.get());
  widget_->Activate();
  SelectWorkspace(std::move(workspace));
  CHECK_EQ(active_index_, *target);
  auto& tabs = tabs_[*target];
  tab.extension_tab = CreateWorkspaceExtensionTab(*WorkspaceProfile(*target), *this);
  SetWorkspaceExtensionTabPage(tab.extension_tab.get(), tab.view->web_contents());
  tabs.push_back(std::move(tab));
  if (split)
    return SplitTab(tabs.size() - 1);
  SelectTab(tabs.size() - 1);
  PersistWorkspaceChanges();
  return true;
}

bool WorkspaceLab::SetLinkRule(const GURL& url, std::string workspace) {
  if (smoke_ || private_owner_ || !CanEditWorkspace() ||
      !url.SchemeIsHTTPOrHTTPS() || !CanRestoreWorkspaceURL(url) ||
      url.host().empty() || (!workspace.empty() && !WorkspaceIndex(workspace))) {
    return false;
  }
  auto rules = link_rules_.Clone();
  if (workspace.empty())
    rules.Remove(url.host());
  else
    rules.Set(url.host(), std::move(workspace));
  if (rules == link_rules_)
    return true;

  auto session = CaptureSession();
  if (rules.empty())
    session.Remove("link_rules");
  else
    session.Set("link_rules", rules.Clone());
  SaveCatalogueChange(std::move(session), base::BindOnce(
      [](base::WeakPtr<WorkspaceLab> lab, base::DictValue rules, bool ok) {
        if (!lab)
          return;
        if (ok)
          lab->link_rules_ = std::move(rules);
        lab->RefreshShell();
        lab->shell_->SetConfigurationError(!ok);
      }, weak_factory_.GetWeakPtr(), std::move(rules)));
  return true;
}

bool WorkspaceLab::RouteNavigation(content::NavigationHandle& navigation) {
  const auto& url = navigation.GetURL();
  if (smoke_ || !CanEditWorkspace() || !model_.active() ||
      !url.SchemeIsHTTPOrHTTPS() || !CanRestoreWorkspaceURL(url) ||
      url.spec().size() > 32768) {
    return false;
  }
  const auto* rule = link_rules_.FindString(url.host());
  const auto target = rule ? WorkspaceIndex(*rule) : std::nullopt;
  auto* page = navigation.GetWebContents();
  const auto position = TabForPage(page);
  const bool preview = page && page == PeekPage() && shell_->peek_widget()->IsVisible();
  const auto workspace = preview ? WorkspaceIndex(peek_workspace_id_)
                                 : position ? std::make_optional(position->first)
                                            : std::nullopt;
  if (!workspace)
    return false;
  std::optional<uint64_t> popup_id;
  bool peek = false;
  if (!preview) {
    auto& tab = tabs_[*workspace][position->second];
    const bool fresh_popup = tab.opener_id && page->GetController().IsInitialNavigation();
    if (!fresh_popup && (*workspace != active_index_ || !tab.view->GetVisible()))
      return false;
    if (fresh_popup) {
      peek = shift_click_peek_ && tab.shift_peek_url == url;
      tab.shift_peek_url = GURL();
      popup_id = tab.id;
    }
  }
  if (!peek && (!target || *target == *workspace ||
                tabs_[*target].size() >= prototype::kMaxWorkspaceSessionTabs)) {
    return false;
  }
  if (popup_id)
    tabs_[*workspace][position->second].routing = true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::FinishRoutedNavigation,
                                weak_factory_.GetWeakPtr(), url, model_.workspaces()[*workspace].id,
                                rule ? *rule : std::string(), page->GetWeakPtr(),
                                popup_id, peek));
  return true;
}

void WorkspaceLab::FinishRoutedNavigation(
    GURL url, std::string source, std::string destination,
    base::WeakPtr<content::WebContents> page, std::optional<uint64_t> popup_id,
    bool peek) {
  const auto workspace = WorkspaceIndex(source);
  if (!page || !workspace ||
      (!popup_id && page.get() != PeekPage() && *workspace != active_index_)) {
    return;
  }
  if (popup_id) {
    auto& tabs = tabs_[*workspace];
    const auto popup = std::ranges::find(tabs, *popup_id, &Tab::id);
    if (popup == tabs.end() || popup->view->web_contents() != page.get())
      return;
    const size_t index = popup - tabs.begin();
    const auto opener = popup->opener_id;
    const bool was_selected = selected_[*workspace] == index;
    auto detached = DetachTab(*workspace, index);
    if (was_selected && opener) {
      const auto original = std::ranges::find(tabs, *opener, &Tab::id);
      if (original != tabs.end()) {
        if (*workspace == active_index_)
          SelectTab(original - tabs.begin());
        else
          selected_[*workspace] = original - tabs.begin();
      }
    }
    RefreshShell();
    PersistWorkspaceChanges();
  }
  if (peek) {
    OpenPeek(url, std::move(source));
    return;
  }
  const auto* current = link_rules_.FindString(url.host());
  if (current && *current == destination)
    OpenLink(url, std::move(source), true, false);
}

std::optional<std::pair<size_t, size_t>> WorkspaceLab::TabForPage(
    content::WebContents* page) const {
  if (!page)
    return std::nullopt;
  for (size_t workspace = 0; workspace < tabs_.size(); ++workspace) {
    const auto& tabs = tabs_[workspace];
    const auto tab = std::ranges::find_if(tabs, [page](const Tab& tab) {
      return tab.view->web_contents() == page;
    });
    if (tab != tabs.end())
      return std::pair(workspace, static_cast<size_t>(tab - tabs.begin()));
  }
  return std::nullopt;
}

content::WebContents* WorkspaceLab::AdoptPage(
    content::WebContents* source, std::unique_ptr<content::WebContents> page,
    const GURL& target_url, WindowOpenDisposition disposition, bool user_gesture,
    bool shift_link) {
  const auto opener = TabForPage(source);
  const bool from_peek = source && source == PeekPage() && shell_->peek_widget()->IsVisible();
  const auto workspace = from_peek ? WorkspaceIndex(peek_workspace_id_)
                                   : opener ? std::make_optional(opener->first)
                                            : std::nullopt;
  if (smoke_ || !CanEditWorkspace() || !workspace || !page ||
      (!from_peek && *workspace != active_index_) ||
      (!user_gesture &&
       WorkspaceProfile(*workspace)->site_permissions().GetPopupSetting(
           source->GetPrimaryMainFrame()->GetLastCommittedOrigin()) !=
           ProfilePermissionController::PopupSetting::kAllow) ||
      !IsNewPageDisposition(disposition) || !IsPageOpeningURL(target_url) ||
      page->GetBrowserContext() != WorkspaceProfile(*workspace) ||
      tabs_[*workspace].size() >= prototype::kMaxWorkspaceSessionTabs) {
    return nullptr;
  }

  auto& tabs = tabs_[*workspace];
  const auto opener_id = from_peek ? peek_tab_->id : tabs[opener->second].id;
  AddTab(*workspace);
  auto& tab = tabs.back();
  tab.opener_id = opener_id;
  if (shift_click_peek_ && shift_link)
    tab.shift_peek_url = target_url;
  auto* adopted = page.get();
  tab.view->SetOwnedWebContents(std::move(page));
  SetWorkspaceExtensionTabPage(tab.extension_tab.get(), adopted);
  HideTabView(*tab.view);
  AttachPageServices(*adopted, *workspace);
  // Chromium installs delayed navigation parameters after AddNewContents.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
          &WorkspaceLab::FinishOpeningPage, weak_factory_.GetWeakPtr(),
          model_.workspaces()[*workspace].id, tab.id,
          disposition == WindowOpenDisposition::NEW_BACKGROUND_TAB,
          /*resume_requests=*/true, from_peek));
  return adopted;
}

content::WebContents* WorkspaceLab::OpenPageURL(
    content::WebContents* source, const content::OpenURLParams& params,
    base::OnceCallback<void(content::NavigationHandle&)> navigation_callback) {
  const auto opener = TabForPage(source);
  const bool from_peek = source && source == PeekPage() && shell_->peek_widget()->IsVisible();
  const auto workspace = from_peek ? WorkspaceIndex(peek_workspace_id_)
                                   : opener ? std::make_optional(opener->first)
                                            : std::nullopt;
  if (smoke_ || !CanEditWorkspace() || !workspace || params.url.is_empty() ||
      !IsPageOpeningURL(params.url)) {
    return nullptr;
  }
  content::NavigationController::LoadURLParams load(params);
  auto* page = source;
  if (params.disposition != WindowOpenDisposition::CURRENT_TAB) {
    auto& tabs = tabs_[*workspace];
    if ((!from_peek && *workspace != active_index_) || !params.user_gesture ||
        !IsNewPageDisposition(params.disposition)) {
      return nullptr;
    }
    auto* source_view = static_cast<WorkspaceWebView*>(
        from_peek ? peek_tab_->view.get() : tabs[opener->second].view.get());
    if (shift_click_peek_ && params.disposition == WindowOpenDisposition::NEW_WINDOW &&
        params.is_renderer_initiated && !params.post_data &&
        ui::PageTransitionCoreTypeIs(params.transition, ui::PAGE_TRANSITION_LINK) &&
        source_view->ConsumeShiftLink(params.url)) {
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(
              [](base::WeakPtr<WorkspaceLab> lab, base::WeakPtr<content::WebContents> source,
                 GURL url, std::string workspace) {
                if (lab && source)
                  lab->OpenPeek(url, std::move(workspace));
              }, weak_factory_.GetWeakPtr(), source->GetWeakPtr(), params.url,
              model_.workspaces()[*workspace].id));
      return nullptr;
    }
    if (tabs.size() >= prototype::kMaxWorkspaceSessionTabs)
      return nullptr;
    const auto opener_id = from_peek ? peek_tab_->id : tabs[opener->second].id;
    AddTab(*workspace);
    auto& tab = tabs.back();
    tab.opener_id = opener_id;
    page = tab.view->GetWebContents(params.url);
    SetWorkspaceExtensionTabPage(tab.extension_tab.get(), page);
    HideTabView(*tab.view);
    AttachPageServices(*page, *workspace);
    load.frame_tree_node_id = {};
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(
            &WorkspaceLab::FinishOpeningPage, weak_factory_.GetWeakPtr(),
            model_.workspaces()[*workspace].id, tab.id,
            params.disposition == WindowOpenDisposition::NEW_BACKGROUND_TAB,
            /*resume_requests=*/false, from_peek));
  }
  auto navigation = page->GetController().LoadURLWithParams(load);
  if (navigation && navigation_callback)
    std::move(navigation_callback).Run(*navigation);
  return page;
}

void WorkspaceLab::FinishOpeningPage(std::string workspace, uint64_t tab_id,
                                     bool background, bool resume_requests,
                                     bool from_peek) {
  const auto index = WorkspaceIndex(workspace);
  if (!index)
    return;
  auto& tabs = tabs_[*index];
  const auto tab = std::ranges::find(tabs, tab_id, &Tab::id);
  if (tab == tabs.end() || !tab->view->web_contents())
    return;
  if (resume_requests)
    tab->view->web_contents()->ResumeLoadingCreatedWebContents();
  if (tab->routing)
    return;
  if (from_peek && !background && *index != active_index_ && CanEditWorkspace()) {
    if (DeferPeekClose(base::BindOnce(&WorkspaceLab::FinishOpeningPage,
                                    weak_factory_.GetWeakPtr(), workspace, tab_id,
                                    false, false, true))) {
      PersistWorkspaceChanges();
      return;
    }
    SelectWorkspace(workspace);
  }
  if (*index == active_index_ && !background)
    SelectTab(tab - tabs.begin());
  else
    RefreshShell();
  PersistWorkspaceChanges();
}

void WorkspaceLab::ClosePopup(content::WebContents* page) {
  const auto position = TabForPage(page);
  if (!position || !tabs_[position->first][position->second].opener_id)
    return;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
          [](base::WeakPtr<WorkspaceLab> lab, base::WeakPtr<content::WebContents> page) {
            if (!lab || !page || !lab->CanEditWorkspace())
              return;
            const auto current = lab->TabForPage(page.get());
            if (!current)
              return;
            const auto [workspace, index] = *current;
            if (workspace == lab->active_index_) {
              lab->RemoveTab(index);
            } else {
              auto detached = lab->DetachTab(workspace, index);
              lab->PersistWorkspaceChanges();
            }
          }, weak_factory_.GetWeakPtr(), page->GetWeakPtr()));
}

bool WorkspaceLab::ShowPageMenu(content::RenderFrameHost& frame,
                                const content::ContextMenuParams& params) {
  auto* page = content::WebContents::FromRenderFrameHost(&frame);
  const bool preview = page && page == PeekPage();
  if (smoke_ || !CanEditWorkspace() || !model_.active() || !page ||
      !frame.IsActive() ||
      (!preview && !std::ranges::any_of(tabs_[active_index_], [page](const Tab& tab) {
        return tab.view->GetVisible() && tab.view->web_contents() == page;
      }))) {
    return false;
  }

  auto& controller = page->GetController();
  prototype::PageMenuNavigation navigation{
      .url = page->GetLastCommittedURL(),
      .can_go_back = controller.CanGoBack(),
      .can_go_forward = controller.CanGoForward(),
      .can_reload = true,
      .run = base::BindRepeating(
          [](base::WeakPtr<WorkspaceLab> lab, base::WeakPtr<content::WebContents> page,
             prototype::PageCommand command) {
            if (lab && page)
              lab->RunPageCommandOn(*page, command);
          }, weak_factory_.GetWeakPtr(), page->GetWeakPtr()),
      .copy_image = prototype::MakeCopyImageAction(frame, params),
      .image_url = prototype::PageMenuImageUrl(params)};
  if (!preview && !private_owner_) {
    navigation.extension_items = WorkspaceExtensionPageMenu(frame, params);
    auto& document = *page->GetPrimaryMainFrame();
    if (CanSetNightShiftSiteMode(document)) {
      prototype::PageMenuItem night_shift{.title = u"Night Shift"};
      const auto add_mode = [&](std::u16string title, NightShiftSiteMode mode) {
        night_shift.children.push_back({
            .title = std::move(title),
            .run = base::BindRepeating(
                [](base::WeakPtr<WorkspaceLab> lab,
                   const content::WeakDocumentPtr& document, NightShiftSiteMode mode) {
                  if (auto* frame = document.AsRenderFrameHostIfValid()) {
                    SetNightShiftSiteMode(*frame, mode,
                        base::BindOnce(&WorkspaceLab::RefreshNightShiftButton, lab, true));
                  }
                }, weak_factory_.GetWeakPtr(), document.GetWeakDocumentPtr(), mode)});
      };
      add_mode(prototype::T(u"Jak w profilu"), NightShiftSiteMode::kAuto);
      add_mode(prototype::T(u"Ciemny motyw witryny"), NightShiftSiteMode::kDark);
      add_mode(prototype::T(u"Wymuś ciemny"), NightShiftSiteMode::kForce);
      add_mode(prototype::T(u"Wyłącz dla witryny"), NightShiftSiteMode::kOff);
      night_shift.children.push_back({
          .title = prototype::T(u"Dostrój wygląd witryny…"),
          .run = base::BindRepeating(
              [](base::WeakPtr<WorkspaceLab> lab,
                 const content::WeakDocumentPtr& document) {
                auto* frame = document.AsRenderFrameHostIfValid();
                if (!lab || !frame || !CanSetNightShiftSiteMode(*frame) ||
                    frame->GetBrowserContext() !=
                        lab->WorkspaceProfile(lab->active_index_)) {
                  return;
                }
                lab->OpenNightShiftSettings(frame);
              }, weak_factory_.GetWeakPtr(), document.GetWeakDocumentPtr())});
      if (assistant_host_settings_.enabled) {
        night_shift.children.push_back({
            .title = prototype::T(u"Popraw kolory z Asystentem…"),
            .run = base::BindRepeating(
                [](base::WeakPtr<WorkspaceLab> lab,
                   const content::WeakDocumentPtr& document) {
                  auto* frame = document.AsRenderFrameHostIfValid();
                  if (lab && frame)
                    lab->PrepareNightShiftRepair(*frame);
                }, weak_factory_.GetWeakPtr(), document.GetWeakDocumentPtr())});
      }
      navigation.extension_items.push_back(std::move(night_shift));
    }
  }
  const auto& link = params.link_url;
  const auto& download_directory = private_owner_
      ? private_owner_->user_download_directory_ : user_download_directory_;
  auto* delegate = download_directory.empty() ? nullptr
      : static_cast<ProfileDownloadDelegate*>(frame.GetBrowserContext()->GetDownloadManagerDelegate());
  if (delegate && delegate->CanSaveLink(frame, link)) {
    navigation.save_link = base::BindRepeating(
        [](base::WeakPtr<WorkspaceLab> lab, const content::WeakDocumentPtr& document,
           const GURL& link) {
          auto* frame = document.AsRenderFrameHostIfValid();
          if (!lab || !lab->CanEditWorkspace() || !frame)
            return;
          auto* delegate = static_cast<ProfileDownloadDelegate*>(
              frame->GetBrowserContext()->GetDownloadManagerDelegate());
          if (delegate)
            delegate->SaveLink(*frame, link);
        }, weak_factory_.GetWeakPtr(), frame.GetWeakDocumentPtr(), link);
  }
  shell_->ShowLinkMenu(
      link.SchemeIsHTTPOrHTTPS() && CanRestoreWorkspaceURL(link) ? link : GURL(),
      preview ? peek_workspace_id_ : model_.active()->id,
      page->GetContainerBounds().origin() + gfx::Vector2d(params.x, params.y),
      {}, std::move(navigation));
  return true;
}

void WorkspaceLab::OpenExternalUrl(GURL url) {
  if (smoke_ || private_owner_ || !url.is_valid() || !url.SchemeIsHTTPOrHTTPS() ||
      url.has_username() || url.has_password()) {
    return;
  }
  if (!model_.active()) {
    if (startup_external_urls_.size() < 32)
      startup_external_urls_.push_back(std::move(url));
    return;
  }
  if (!OpenLink(url, model_.active()->id, /*new_tab=*/true, /*split=*/false))
    return;
  if (widget_->IsMinimized())
    widget_->Restore();
  widget_->Show();
  widget_->Activate();
}

bool WorkspaceLab::Navigate(const GURL& url, bool new_tab) {
  if (smoke_ || saving_session_ || before_unload_batch_.pending() ||
      !web_contents() ||
      !url.is_valid() ||
      (!url.SchemeIsHTTPOrHTTPS() && !prototype::IsRssTabUrl(url)) ||
      url.has_username() || url.has_password()) {
    return false;
  }
  if (new_tab) {
    if (tabs_[active_index_].size() >= prototype::kMaxWorkspaceSessionTabs)
      return false;
    AddTab(active_index_);
    SelectTab(tabs_[active_index_].size() - 1);
  }
  prototype::LoadWorkspaceTab(web_contents(), url, ui::PAGE_TRANSITION_TYPED);
  return true;
}

WorkspaceExtensionTab* WorkspaceLab::CreateExtensionTab(
    content::BrowserContext& profile, const GURL& url, bool active) {
  if (smoke_ || private_owner_ || !CanEditWorkspace())
    return nullptr;
  size_t workspace = active_index_;
  if (WorkspaceProfile(workspace) != &profile) {
    for (workspace = 0; workspace < tabs_.size(); ++workspace) {
      if (WorkspaceProfile(workspace) == &profile)
        break;
    }
  }
  if (workspace == tabs_.size() ||
      tabs_[workspace].size() >= prototype::kMaxWorkspaceSessionTabs) {
    return nullptr;
  }
  if (active && workspace != active_index_) {
    if (PeekPage())
      return nullptr;
    SelectWorkspace(model_.workspaces()[workspace].id);
    if (active_index_ != workspace)
      return nullptr;
  }
  AddTab(workspace);
  auto& tab = tabs_[workspace].back();
  tab.deferred_url = url;
  if (url.SchemeIs(extensions::kExtensionScheme))
    SetWorkspaceExtensionTabPageUrl(tab.extension_tab.get(), url);
  if (active)
    SelectTab(tabs_[workspace].size() - 1);
  else
    NavigateExtensionTab(workspace, tab, url);
  RefreshShell();
  PersistWorkspaceChanges();
  return tab.extension_tab.get();
}

std::optional<std::pair<size_t, size_t>> WorkspaceLab::ExtensionTabPosition(
    const WorkspaceExtensionTab& target) const {
  for (size_t workspace = 0; workspace < tabs_.size(); ++workspace) {
    const auto& tabs = tabs_[workspace];
    const auto tab = std::ranges::find_if(tabs, [&](const Tab& entry) {
      return entry.extension_tab.get() == &target;
    });
    if (tab != tabs.end())
      return std::pair(workspace, tab - tabs.begin());
  }
  return std::nullopt;
}

void WorkspaceLab::NavigateExtensionTab(size_t workspace, Tab& tab,
                                        const GURL& url) {
  if (!tab.view->web_contents()) {
    tab.deferred_url = url;
    tab.deferred_title.clear();
  }
  auto& page = EnsureHiddenTabPage(workspace, tab, url);
  if (!url.is_empty())
    prototype::LoadWorkspaceTab(&page, url, ui::PAGE_TRANSITION_FROM_API);
}

content::WebContents& WorkspaceLab::EnsureHiddenTabPage(
    size_t workspace, Tab& tab, const GURL& url) {
  if (auto* page = tab.view->web_contents())
    return *page;
  if (tab.discarded_page)
    tab.view->SetOwnedWebContents(std::move(tab.discarded_page));
  auto* page = tab.view->GetWebContents(
      url.is_empty() || prototype::IsRssTabUrl(url) ? GURL("about:blank") : url);
  SetWorkspaceExtensionTabPage(tab.extension_tab.get(), page);
  AttachPageServices(*page, workspace);
  HideTabView(*tab.view);
  sleeping_[workspace] = false;
  return *page;
}

bool WorkspaceLab::UpdateExtensionTab(WorkspaceExtensionTab& target,
                                      const GURL& url, bool active,
                                      std::optional<bool> muted,
                                      std::string_view extension_id) {
  const auto position = ExtensionTabPosition(target);
  if (smoke_ || private_owner_ || !CanEditWorkspace() || !position)
    return false;
  const auto [workspace, index] = *position;
  if (active && workspace != active_index_) {
    if (PeekPage())
      return false;
    SelectWorkspace(model_.workspaces()[workspace].id);
    if (active_index_ != workspace)
      return false;
  }
  if (muted)
    SetWorkspaceExtensionTabMuted(target, *muted, extension_id);
  if (!url.is_empty())
    NavigateExtensionTab(workspace, tabs_[workspace][index], url);
  if (active)
    SelectTab(index);
  RefreshShell();
  PersistWorkspaceChanges();
  return true;
}

bool WorkspaceLab::ReloadExtensionTab(WorkspaceExtensionTab& target,
                                      bool bypass_cache) {
  const auto position = ExtensionTabPosition(target);
  if (smoke_ || private_owner_ || !CanEditWorkspace() || !position)
    return false;
  const auto [workspace, index] = *position;
  auto& tab = tabs_[workspace][index];
  if (tab.discarded_page) {
    auto& page = EnsureHiddenTabPage(workspace, tab, tab.deferred_url);
    ReloadWorkspacePage(page, bypass_cache);
    RefreshShell();
    return true;
  }
  if (auto* page = tab.view->web_contents()) {
    auto& controller = page->GetController();
    const auto* entry = controller.IsInitialNavigation() && controller.GetPendingEntry()
                            ? controller.GetPendingEntry()
                            : controller.GetLastCommittedEntry();
    if (entry && entry->GetHasPostData()) {
      // A background extension request must not surface a tab or its form.
      const auto* dialogs = prototype::PageDialogs::FromWebContents(page);
      if (page->GetVisibility() != content::Visibility::VISIBLE ||
          !page->GetTopLevelNativeWindow() || !dialogs ||
          dialogs->checking_before_unload() || dialogs->dialog_open() ||
          dialogs->leave_dialog_open()) {
        return false;
      }
    }
    ReloadWorkspacePage(*page, bypass_cache);
    return true;
  }

  const GURL url = tab.deferred_url;
  auto& page = EnsureHiddenTabPage(workspace, tab, url);
  if (prototype::IsRssTabUrl(url)) {
    prototype::LoadWorkspaceTab(&page, url, ui::PAGE_TRANSITION_RELOAD);
    WorkspaceProfile(workspace)->GetRssReader().Refresh();
  } else if (!url.is_empty()) {
    content::NavigationController::LoadURLParams params(url);
    params.transition_type = ui::PAGE_TRANSITION_RELOAD;
    params.reload_type = bypass_cache ? content::ReloadType::BYPASSING_CACHE
                                      : content::ReloadType::NORMAL;
    page.GetController().LoadURLWithParams(params);
  }
  RefreshShell();
  PersistWorkspaceChanges();
  return true;
}

WorkspaceExtensionTab* WorkspaceLab::DiscardExtensionTab(
    content::BrowserContext& profile, WorkspaceExtensionTab* target) {
  if (smoke_ || private_owner_ || !CanEditWorkspace())
    return nullptr;

  Tab* candidate = nullptr;
  content::WebContents* oldest = nullptr;
  for (size_t workspace = 0; workspace < tabs_.size(); ++workspace) {
    if (WorkspaceProfile(workspace) != &profile)
      continue;
    for (size_t index = 0; index < tabs_[workspace].size(); ++index) {
      auto& tab = tabs_[workspace][index];
      if (!tab.extension_tab || (target && tab.extension_tab.get() != target) ||
          (workspace == active_index_ && selected_[workspace] == index)) {
        continue;
      }
      auto* page = tab.view->web_contents();
      if (!page || page->GetVisibility() != content::Visibility::HIDDEN ||
          page->IsLoading() || page->NeedToFireBeforeUnload() ||
          page->IsCurrentlyAudible() || page->IsBeingCaptured() ||
          media_access_->IsCapturing(page) || page->HasPictureInPictureVideo() ||
          page->HasPictureInPictureDocument()) {
        continue;
      }
      auto& controller = page->GetController();
      const auto* entry = controller.GetLastCommittedEntry();
      if (controller.IsInitialNavigation() || controller.GetPendingEntry() ||
          !entry || !CanRestoreWorkspaceURL(page->GetLastCommittedURL()) ||
          content::ZitterHistoryEntryHasPost(*entry)) {
        continue;
      }
      if (!oldest || page->GetLastActiveTimeTicks() < oldest->GetLastActiveTimeTicks()) {
        candidate = &tab;
        oldest = page;
      }
    }
  }
  if (!candidate)
    return nullptr;

  // Keep Chromium's controller and session storage snapshot off the view.
  // The replacement has no renderer until it is shown or explicitly loaded.
  content::WebContents::CreateParams params(&profile);
  params.initially_hidden = true;
  params.desired_renderer_state = content::WebContents::CreateParams::kNoRendererProcess;
  params.last_active_time = oldest->GetLastActiveTime();
  params.last_active_time_ticks = oldest->GetLastActiveTimeTicks();
  auto replacement = content::WebContents::Create(params);
  replacement->GetController().CopyStateFrom(&oldest->GetController(),
                                              /*needs_reload=*/true);
  replacement->SetWasDiscarded(true);
  oldest->AboutToBeDiscarded(replacement.get());
  candidate->deferred_url = oldest->GetLastCommittedURL();
  candidate->deferred_title =
      oldest->GetController().GetLastCommittedEntry()->GetTitleForDisplay();
  candidate->discarded_page = std::move(replacement);
  SetWorkspaceExtensionTabPage(candidate->extension_tab.get(), nullptr);
  candidate->view->SetWebContents(nullptr);
  RefreshShell();
  return candidate->extension_tab.get();
}

bool WorkspaceLab::NavigateExtensionTabHistory(WorkspaceExtensionTab& target,
                                               bool forward) {
  const auto position = ExtensionTabPosition(target);
  if (smoke_ || private_owner_ || !CanEditWorkspace() || !position)
    return false;
  const auto [workspace, index] = *position;
  auto* page = tabs_[workspace][index].view->web_contents();
  return page && content::ZitterGoThroughHistory(page->GetController(), forward);
}

bool WorkspaceLab::MoveExtensionTabs(
    content::BrowserContext& profile,
    base::span<WorkspaceExtensionTab* const> targets,
    int index) {
  if (smoke_ || private_owner_ || !CanEditWorkspace())
    return false;
  // Plan the whole batch using the same flattened order as tabs.query.
  // A null slot represents the blank tab left in an emptied workspace.
  std::vector<std::vector<WorkspaceExtensionTab*>> layout(tabs_.size());
  size_t count = 0;
  for (size_t workspace = 0; workspace < tabs_.size(); ++workspace) {
    if (WorkspaceProfile(workspace) != &profile)
      continue;
    for (const auto& tab : tabs_[workspace])
      layout[workspace].push_back(tab.extension_tab.get());
    count += layout[workspace].size();
  }
  struct Move {
    size_t source;
    size_t from;
    size_t destination;
    size_t to;
  };
  std::vector<Move> plan;
  size_t next = index < 0 ? count : static_cast<size_t>(index);
  auto* focused = tabs_[active_index_][selected_[active_index_]].extension_tab.get();
  for (auto* target : targets) {
    const auto original = ExtensionTabPosition(*target);
    if (!original || WorkspaceProfile(original->first) != &profile)
      return false;
    const size_t source = original->first;
    auto& from_tabs = layout[source];
    const size_t from = std::ranges::find(from_tabs, target) - from_tabs.begin();
    if (from == from_tabs.size())
      return false;
    const bool append = next >= count;
    const size_t requested = std::min(next, count - 1);
    size_t destination = 0;
    size_t base = 0;
    while (requested >= base + layout[destination].size()) {
      base += layout[destination].size();
      ++destination;
    }
    auto& to_tabs = layout[destination];
    const bool crossing = source != destination;
    if (crossing) {
      const auto& tab = tabs_[source][original->second];
      auto* page = tab.view->web_contents();
      if (!tab.pin.empty() || to_tabs.size() >= prototype::kMaxWorkspaceSessionTabs ||
          (target == focused && PeekPage()) ||
          (page && (page->IsBeingCaptured() || media_access_->IsCapturing(page)))) {
        return false;
      }
    }
    from_tabs.erase(from_tabs.begin() + from);
    if (crossing && from_tabs.empty()) {
      from_tabs.push_back(nullptr);
      ++count;
    } else if (source < destination) {
      --base;
    }
    const size_t to = append ? to_tabs.size()
                             : std::min(requested - base, to_tabs.size());
    to_tabs.insert(to_tabs.begin() + to, target);
    next = base + to + 1;
    if (crossing || from != to)
      plan.push_back({source, from, destination, to});
  }
  if (plan.empty())
    return true;

  RecordTimeline(true);
  const size_t original_workspace = active_index_;
  const bool refresh_panes = std::ranges::any_of(plan, [&](const Move& move) {
    return move.source == original_workspace && move.destination != move.source;
  });
  if (refresh_panes)
    shell_->SetPanes({}, {});
  for (const auto& [source, from, destination, to] : plan) {
    if (source == destination) {
      ReorderWorkspaceTab(source, from, to > from ? to + 1 : to);
      continue;
    }
    auto moved = DetachTab(source, from, /*show=*/false);
    auto& destination_tabs = tabs_[destination];
    if (selected_[destination] >= to)
      ++selected_[destination];
    for (size_t& pane : splits_[destination].tabs) {
      if (pane >= to)
        ++pane;
    }
    sleeping_[destination] = false;
    destination_tabs.insert(destination_tabs.begin() + to, std::move(moved));
  }
  if (focused) {
    const auto position = ExtensionTabPosition(*focused);
    if (position && position->first != active_index_) {
      const auto [workspace, tab] = *position;
      selected_[workspace] = tab;
      SelectWorkspace(model_.workspaces()[workspace].id);
    }
  }
  if (refresh_panes && active_index_ == original_workspace)
    ShowTab();
  RefreshShell();
  RecordTimeline(true);
  if (timeline_dirty_)
    PersistTimeline();
  PersistWorkspaceChanges();
  return true;
}

void WorkspaceLab::RemoveExtensionTabs(
    base::span<WorkspaceExtensionTab* const> targets,
    base::OnceCallback<void(bool)> done) {
  if (smoke_ || private_owner_ || !CanEditWorkspace()) {
    std::move(done).Run(false);
    return;
  }
  std::vector<std::pair<size_t, size_t>> positions;
  std::vector<content::WebContents*> pages;
  for (const auto* target : targets) {
    const auto position = ExtensionTabPosition(*target);
    if (!position) {
      std::move(done).Run(false);
      return;
    }
    const auto [workspace, index] = *position;
    if (auto* page = tabs_[workspace][index].view->web_contents())
      pages.push_back(page);
    positions.push_back(*position);
  }
  if (std::ranges::none_of(pages, &content::WebContents::NeedToFireBeforeUnload)) {
    CommitExtensionTabRemoval(std::move(positions));
    std::move(done).Run(true);
    return;
  }
  pending_extension_tab_removal_ = std::move(done);
  CheckBeforeUnload(
      pages,
      base::BindOnce(&WorkspaceLab::CommitExtensionTabRemoval,
                     weak_factory_.GetWeakPtr(), std::move(positions))
          .Then(base::BindOnce(&WorkspaceLab::CompleteExtensionTabRemoval,
                               weak_factory_.GetWeakPtr(), true)),
      base::BindOnce(&WorkspaceLab::CompleteExtensionTabRemoval,
                     weak_factory_.GetWeakPtr(), false));
}

void WorkspaceLab::CompleteExtensionTabRemoval(bool success) {
  std::move(pending_extension_tab_removal_).Run(success);
}

void WorkspaceLab::CommitExtensionTabRemoval(
    std::vector<std::pair<size_t, size_t>> tabs) {
  const bool active_removed =
      std::ranges::contains(tabs, std::pair(active_index_, selected_[active_index_]));
  RecordTimeline(true);
  std::ranges::sort(tabs, std::greater<>());
  for (const auto& [workspace, index] : tabs) {
    RecordClosedTab(workspace, index);
    DetachTab(workspace, index);
  }
  RefreshShell();
  if (active_removed)
    tabs_[active_index_][selected_[active_index_]].view->RequestFocus();
  RecordTimeline(true);
  if (timeline_dirty_)
    PersistTimeline();
  PersistWorkspaceChanges();
}

#if BUILDFLAG(ENABLE_PRINTING)
bool WorkspaceLab::StartPdfExport(const prototype::PrintOptions& options,
                                  const std::string& ranges,
                                  bool from_preview,
                                  bool system) {
  auto* page = web_contents();
  if (!CanEditWorkspace() || pdf_export_ || !page || page->IsLoading() ||
      !page->GetLastCommittedURL().SchemeIsHTTPOrHTTPS() ||
      !page->GetTopLevelNativeWindow()) {
    return false;
  }
  if (!from_preview)
    shell_->CloseOverlays(true);
  pdf_export_ = std::make_unique<WorkspacePdfExport>(
      *page, base::BindOnce([](base::WeakPtr<WorkspaceLab> lab,
                              bool from_preview, bool system,
                              WorkspacePdfExport::Result result) {
        if (!lab)
          return;
        lab->pdf_export_.reset();
        const bool saved = result == WorkspacePdfExport::Result::kSaved;
        if (lab->system_print_probe_ != SystemPrintProbe::kNone) {
          const auto mode = std::exchange(lab->system_print_probe_, SystemPrintProbe::kNone);
          if ((mode == SystemPrintProbe::kCancel &&
               result == WorkspacePdfExport::Result::kCancelled) ||
              (mode == SystemPrintProbe::kFile && saved)) {
            if (saved)
              std::puts("Zitter Content exported a cross-process page to PDF");
            const size_t fixture = lab->selected_[0];
            lab->SelectTab(kProbeSelection[0]);
            lab->CloseTab(fixture);
            lab->SelectWorkspace(std::string(kWorkspaceIds[1]));
            lab->RequestClose();
          } else {
            lab->Finish(false);
          }
        } else if (lab->pdf_probe_picker_) {
          lab->pdf_probe_picker_.reset();
          if (saved) {
            const size_t fixture = lab->selected_[0];
            lab->SelectTab(kProbeSelection[0]);
            lab->CloseTab(fixture);
            lab->SelectWorkspace(std::string(kWorkspaceIds[1]));
            std::puts("Zitter Content exported a cross-process page to PDF");
            lab->RequestClose();
          } else {
            LOG(ERROR) << "PDF export did not save the fixture";
            lab->Finish(false);
          }
        } else if (system) {
          if (result == WorkspacePdfExport::Result::kFailed) {
            const auto error = prototype::T(u"Nie udało się wydrukować dokumentu.");
            if (from_preview)
              lab->shell_->PrintFinished(error);
            else
              lab->shell_->ShowToast(error, {}, {});
          } else if (saved && from_preview) {
            lab->shell_->HidePrint();
          }
        } else if (from_preview) {
          lab->shell_->PrintFinished(result == WorkspacePdfExport::Result::kFailed
              ? prototype::T(u"Nie udało się zapisać pliku PDF.") : u"");
        } else if (saved) {
          lab->shell_->ShowToast(prototype::T(u"Zapisano plik PDF."), {}, {});
        } else if (result == WorkspacePdfExport::Result::kFailed) {
          lab->shell_->ShowToast(
              prototype::T(u"Nie udało się zapisać pliku PDF."), {}, {});
        }
      }, weak_factory_.GetWeakPtr(), from_preview, system));
#if BUILDFLAG(IS_MAC)
  if (system) {
    pdf_export_->StartSystem(
        system_print_probe_ == SystemPrintProbe::kCancel,
        system_print_probe_ == SystemPrintProbe::kFile
            ? session_path_.DirName().AppendASCII("printed-fixture.pdf")
            : base::FilePath());
    return true;
  }
#endif
  pdf_export_->Start(options, ranges);
  return true;
}

#if BUILDFLAG(ENABLE_PDF)
bool WorkspaceLab::ShowPdfPrintSheet() {
  auto* page = web_contents();
  if (!CanEditWorkspace() || pdf_export_ || !page || page->IsLoading() ||
      !page->GetLastCommittedURL().SchemeIsHTTPOrHTTPS() ||
      !widget_->IsVisible()) {
    return false;
  }
  shell_->HidePrint();
  const auto weak = weak_factory_.GetWeakPtr();
  pdf_preview_ = std::make_unique<WorkspacePdfPreview>(
      *page, base::BindRepeating([](base::WeakPtr<WorkspaceLab> lab,
                                   std::vector<gfx::ImageSkia> pages) {
        if (lab) {
          const bool failed = pages.empty();
          lab->shell_->SetPrintPreview(std::move(pages), failed);
        }
      }, weak));
  prototype::WorkspaceShell::PrintHandlers handlers;
  handlers.preview = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, prototype::PrintOptions options) {
        if (lab && lab->pdf_preview_)
          lab->pdf_preview_->Request(std::move(options));
      }, weak);
  handlers.print = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, prototype::PrintOptions options) {
        if (!lab || !lab->pdf_preview_)
          return;
        auto ranges = prototype::PrintPageRanges(
            options, lab->pdf_preview_->page_count());
        if (!ranges || !lab->StartPdfExport(options, *ranges, true)) {
          lab->shell_->PrintFinished(
              prototype::T(u"Nie udało się zapisać pliku PDF."));
        }
      }, weak);
#if BUILDFLAG(IS_MAC)
  handlers.system_dialog = base::BindRepeating([](base::WeakPtr<WorkspaceLab> lab) {
    if (lab)
      lab->StartPdfExport({}, {}, true, true);
  }, weak);
#endif
  handlers.closed = base::BindRepeating([](base::WeakPtr<WorkspaceLab> lab) {
    if (lab) {
      lab->pdf_preview_.reset();
      lab->pdf_export_.reset();
    }
  }, weak);
  shell_->ShowPrint(std::move(handlers));
  return shell_->print_visible();
}

void WorkspaceLab::RequestScriptedPrint(content::RenderFrameHost& frame) {
  auto* page = web_contents();
  const auto now = base::TimeTicks::Now();
  if (!page || page->GetPrimaryMainFrame() != &frame || !widget_->IsActive() ||
      shell_->print_visible() ||
      (!last_scripted_print_.is_null() &&
       now - last_scripted_print_ < base::Seconds(2))) {
    return;
  }
  if (ShowPdfPrintSheet())
    last_scripted_print_ = now;
}

void WorkspaceLab::CheckScriptedPrintProbe(
    base::WeakPtr<content::WebContents> fixture,
    bool reloading,
    base::TimeTicks deadline) {
  if (!fixture || shell_->print_visible() || base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "Scripted print probe could not isolate the hidden document";
    Finish(false);
    return;
  }
  if (!reloading && fixture->GetTitle() == u"Hidden print returned") {
    std::puts("Zitter Content ignored a hidden scripted print request");
    CloseTab(tabs_[0].size() - 1);
    if (fixture || !Navigate(session_probe_url_.Resolve(kPdfExportPage), true)) {
      Finish(false);
      return;
    }
    fixture = web_contents()->GetWeakPtr();
    reloading = true;
  }
  if (reloading && fixture->GetTitle() == u"PDF fixture ready" &&
      !fixture->IsLoading()) {
    fixture->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
        u"window.print(); document.title = 'Visible print returned';", {},
        content::ISOLATED_WORLD_ID_CONTENT_END + 1);
    CheckPdfPreviewProbe(false, base::TimeTicks::Now() + base::Seconds(20));
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckScriptedPrintProbe,
                                weak_factory_.GetWeakPtr(), fixture, reloading,
                                deadline),
      base::Milliseconds(25));
}

void WorkspaceLab::CheckPdfPreviewProbe(bool range_selected,
                                         base::TimeTicks deadline) {
  if (shell_->print_ready_for_testing() &&
      shell_->print_preview_pages_for_testing() == 2) {
    if (range_selected) {
      shell_->PrintNowForTesting();
      if (!pdf_export_) {
        LOG(ERROR) << "PDF sheet did not export the selected range";
        Finish(false);
      }
      return;
    }
    shell_->PrintChooseForTesting(0, 2, u"2", true);
    range_selected = true;
  }
  if (base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "PDF sheet did not render both preview pages";
    Finish(false);
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckPdfPreviewProbe,
                                weak_factory_.GetWeakPtr(), range_selected,
                                deadline),
      base::Milliseconds(25));
}
#endif

void WorkspaceLab::CheckPdfExportProbe(base::TimeTicks deadline) {
  auto* page = web_contents();
  if (page->GetTitle() == u"PDF fixture ready" && !page->IsLoading()) {
    auto* main_frame = page->GetPrimaryMainFrame();
    bool has_cross_process_frame = false;
    main_frame->ForEachRenderFrameHost(
        [&](content::RenderFrameHost* frame) {
          has_cross_process_frame |=
              frame->IsRenderFrameLive() &&
              frame->GetProcess() != main_frame->GetProcess() &&
              frame->GetLastCommittedURL().host() == "localhost";
        });
    if (!has_cross_process_frame) {
      LOG(ERROR) << "PDF fixture did not have a cross-process frame";
      Finish(false);
      return;
    }
#if BUILDFLAG(IS_MAC)
    const auto system_mode = base::CommandLine::ForCurrentProcess()
                                 ->GetSwitchValueASCII(kPdfExportProbe);
    if (system_mode == "system-cancel" || system_mode == "system-file") {
      system_print_probe_ = system_mode == "system-cancel"
          ? SystemPrintProbe::kCancel : SystemPrintProbe::kFile;
      if (!native_menu_probe_ || !native_menu_probe_.Run("print_system") ||
          !pdf_export_) {
        LOG(ERROR) << "Could not start the system print dialog";
        Finish(false);
      }
      return;
    }
#endif
    pdf_probe_picker_ = InstallPdfSaveProbePicker(
        session_path_.DirName().AppendASCII("printed-fixture.pdf"));
#if BUILDFLAG(ENABLE_PDF)
    const auto mode = base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
        kPdfExportProbe);
    if (mode == "scripted") {
      const auto fixture = page->GetWeakPtr();
      SelectTab(kProbeSelection[0]);
      if (!fixture || fixture->GetVisibility() != content::Visibility::HIDDEN) {
        Finish(false);
        return;
      }
      fixture->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
          u"window.print(); document.title = 'Hidden print returned';", {},
          content::ISOLATED_WORLD_ID_CONTENT_END + 1);
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE, base::BindOnce(&WorkspaceLab::CheckScriptedPrintProbe,
                                    weak_factory_.GetWeakPtr(), fixture, false,
                                    base::TimeTicks::Now() + base::Seconds(10)),
          base::Milliseconds(350));
      return;
    }
    if (mode == "preview") {
      if (!RunProbePaletteCommand(u"> print pdf", "print") ||
          !shell_->print_visible()) {
        LOG(ERROR) << "Could not open the PDF print sheet from Command Center";
        Finish(false);
        return;
      }
      CheckPdfPreviewProbe(false, base::TimeTicks::Now() + base::Seconds(20));
      return;
    }
#endif
    if (!(native_menu_probe_ ? native_menu_probe_.Run("save_pdf")
                             : RunShortcut("save_pdf")) || !pdf_export_) {
      LOG(ERROR) << "Could not start the PDF export";
      Finish(false);
    }
    return;
  }
  if (base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "PDF fixture did not load";
    Finish(false);
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckPdfExportProbe,
                                weak_factory_.GetWeakPtr(), deadline),
      base::Milliseconds(25));
}
#endif

bool WorkspaceLab::StartFind() {
  auto* page = web_contents();
  if (!CanEditWorkspace() || !page ||
      !page->GetController().GetLastCommittedEntry() || !widget_->IsVisible()) {
    return false;
  }
  if (find_page_.get() != page) {
    shell_->ResetFind();
    StopFind();
    find_page_ = page->GetWeakPtr();
  }
  if (!find_observation_.IsObserving()) {
    find_in_page::FindTabHelper::CreateForWebContents(page);
    find_observation_.Observe(
        find_in_page::FindTabHelper::FromWebContents(page));
  }
  find_observation_.GetSource()->set_find_ui_active(true);
  const auto weak = weak_factory_.GetWeakPtr();
  shell_->ShowFind(
      base::BindRepeating([](base::WeakPtr<WorkspaceLab> lab) {
        return lab ? lab->FindBounds() : gfx::Rect();
      }, weak),
      base::BindRepeating(&WorkspaceLab::Find, weak),
      base::BindRepeating(&WorkspaceLab::StopFind, weak));
  return true;
}

void WorkspaceLab::Find(std::u16string text, bool forward, bool match_case) {
  auto* helper = find_observation_.GetSource();
  if (!helper || !find_page_ || find_page_.get() != web_contents())
    return;
  if (text.empty()) {
    helper->StopFinding(find_in_page::SelectionAction::kClear);
    return;
  }
  helper->StartFinding(std::move(text), forward, match_case, /*find_match=*/true);
}

void WorkspaceLab::StopFind() {
  if (auto* helper = find_observation_.GetSource()) {
    find_observation_.Reset();
    helper->StopFinding(find_in_page::SelectionAction::kKeep);
  }
}

gfx::Rect WorkspaceLab::FindBounds() const {
  return find_page_ ? find_page_->GetContainerBounds()
                    : widget_->GetClientAreaBoundsInScreen();
}

void WorkspaceLab::OnFindResultAvailable(content::WebContents* contents) {
  if (contents != find_page_.get() || contents != web_contents())
    return;
  const auto& result = find_observation_.GetSource()->find_result();
  shell_->SetFindResult(result.active_match_ordinal(), result.number_of_matches());
}

void WorkspaceLab::OnFindTabHelperDestroyed(find_in_page::FindTabHelper*) {
  find_observation_.Reset();
  find_page_.reset();
  shell_->ResetFind();
}

content::WebContents* WorkspaceLab::PageForNavigationCommand() {
  return shell_->peek_widget()->IsVisible() ? PeekPage() : web_contents();
}

void WorkspaceLab::RunPageCommand(prototype::PageCommand command) {
  if (auto* page = PageForNavigationCommand())
    RunPageCommandOn(*page, command);
}

void WorkspaceLab::RunPageCommandOn(content::WebContents& page,
                                    prototype::PageCommand command) {
  if (smoke_ || !model_.active() || !CanEditWorkspace())
    return;
  auto& controller = page.GetController();
  switch (command) {
    case prototype::PageCommand::kBack:
      if (controller.CanGoBack())
        controller.GoBack();
      break;
    case prototype::PageCommand::kForward:
      if (controller.CanGoForward())
        controller.GoForward();
      break;
    case prototype::PageCommand::kReload:
      ReloadWorkspacePage(page, /*bypass_cache=*/false);
      break;
  }
}

void WorkspaceLab::ReloadWorkspacePage(content::WebContents& page,
                                       bool bypass_cache) {
  if (prototype::IsRssTabUrl(page.GetVisibleURL())) {
    static_cast<ProfileContext*>(page.GetBrowserContext())->GetRssReader().Refresh();
    return;
  }
  page.GetController().Reload(bypass_cache ? content::ReloadType::BYPASSING_CACHE
                                           : content::ReloadType::NORMAL,
                              /*check_for_repost=*/true);
}

base::DictValue WorkspaceLab::ReadAdBlocking() const {
  auto& profile = *WorkspaceProfile(active_index_);
  base::ListValue lists;
  base::Time updated;
  const auto& sources = AdblockLists() ? AdblockLists()->lists()
                                               : prototype::AdblockFilterListSources();
  for (const auto& list : sources) {
    if (!AdblockLists() && !list.builtin)
      continue;
    const auto count = !AdblockLists() && list.builtin
        ? adblock::count_rules(adblock::builtin_list()) : list.rules;
    if (!list.builtin && list.updated > updated)
      updated = list.updated;
    lists.Append(base::DictValue()
                     .Set("id", list.id)
                     .Set("name", list.builtin
                        ? base::UTF16ToUTF8(prototype::T(u"Zamienniki Zittera"))
                        : list.name)
                     .Set("rules", static_cast<int>(count))
                     .Set("optional", list.optional)
                     .Set("builtin", list.builtin)
                     .Set("enabled", profile.GetAdblockListEnabled(list.id)));
  }
  base::ListValue sites;
  for (const auto& site : profile.AdblockAllowedSites())
    sites.Append(site);
  std::map<std::string, int> counts;
  base::ListValue rules;
  for (const auto& rule : profile.AdblockRules()) {
    rules.Append(rule);
    if (!rule.starts_with('!'))
      ++counts[prototype::AdblockRuleSite(rule)];
  }
  base::ListValue rule_sites;
  for (const auto& [site, count] : counts)
    rule_sites.Append(base::DictValue().Set("site", site).Set("count", count));
  return base::DictValue()
      .Set("available", profile.GetAdblockEnabled().has_value())
      .Set("list_updates_available", AdblockLists() != nullptr)
      .Set("updating", AdblockLists() && AdblockLists()->updating())
      .Set("error", AdblockLists() ? AdblockLists()->last_error() : "")
      .Set("updated", updated.is_null() ? std::string()
                       : base::UTF16ToUTF8(base::TimeFormatShortDate(updated)))
      .Set("lists", std::move(lists))
      .Set("sites", std::move(sites))
      .Set("rules", std::move(rules))
      .Set("rule_sites", std::move(rule_sites));
}

std::string WorkspaceLab::WriteAdBlocking(base::DictValue command) {
  const auto* action = command.FindString("command");
  if (!action || !CanEditWorkspace())
    return {};
  auto& profile = *WorkspaceProfile(active_index_);
  if (*action == "update" && AdblockLists()) {
    AdblockLists()->Refresh(true);
  } else if (*action == "list") {
    const auto* list = command.FindString("list");
    const auto enabled = command.FindBool("enabled");
    if (list && enabled && profile.SetAdblockListEnabled(*list, *enabled) &&
        *enabled && AdblockLists()) {
      AdblockLists()->Refresh(false);
    }
  } else if (*action == "site_remove") {
    if (const auto* site = command.FindString("site"))
      profile.SetAdblockSiteAllowed(*site, false);
  }
  return {};
}

void WorkspaceLab::OpenAdBlockingRules() {
  const size_t workspace = active_index_;
  if (!WorkspaceProfile(workspace)->GetAdblockEnabled().has_value() ||
      !shell_->general_widget() || !CanEditWorkspace()) {
    return;
  }
  if (!ad_blocking_rules_panel_)
    ad_blocking_rules_panel_ = std::make_unique<prototype::AdBlockingRulesPanel>();
  prototype::AdBlockingRulesPanel::Callbacks callbacks;
  const auto weak = weak_factory_.GetWeakPtr();
  callbacks.read = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, size_t workspace) {
        return lab ? lab->WorkspaceProfile(workspace)->AdblockRules()
                   : std::vector<std::string>();
      }, weak, workspace);
  callbacks.add = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, size_t workspace, std::string rule) {
        if (lab && lab->active_index_ == workspace && lab->CanEditWorkspace()) {
          auto rules = lab->WorkspaceProfile(workspace)->AdblockRules();
          if (!std::ranges::contains(rules, rule))
            rules.push_back(std::move(rule));
          if (lab->WorkspaceProfile(workspace)->SetAdblockRules(std::move(rules))) {
            lab->RefreshAdBlockingRules();
            return std::string();
          }
        }
        return base::UTF16ToUTF8(prototype::T(u"Nie udało się zapisać ustawień."));
      }, weak, workspace);
  callbacks.remove = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, size_t workspace, std::string rule) {
        if (!lab || lab->active_index_ != workspace || !lab->CanEditWorkspace())
          return;
        auto rules = lab->WorkspaceProfile(workspace)->AdblockRules();
        std::erase(rules, rule);
        if (lab->WorkspaceProfile(workspace)->SetAdblockRules(std::move(rules)))
          lab->RefreshAdBlockingRules();
      }, weak, workspace);
  callbacks.save = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, size_t workspace, std::string text) {
        if (!lab || lab->active_index_ != workspace || !lab->CanEditWorkspace())
          return false;
        auto rules = base::SplitString(
            text, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
        if (!lab->WorkspaceProfile(workspace)->SetAdblockRules(std::move(rules)))
          return false;
        lab->RefreshAdBlockingRules();
        return true;
      }, weak, workspace);
  callbacks.try_rule = base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, size_t workspace, std::string rule,
         base::OnceCallback<bool(bool)> done) {
        auto* page = lab ? lab->web_contents() : nullptr;
        if (!page || lab->active_index_ != workspace || !lab->CanEditWorkspace()) {
          std::move(done).Run(false);
          return;
        }
        ProfileContext::TryAdblockRule(
            *page, std::move(rule),
            base::BindOnce([](base::WeakPtr<WorkspaceLab> lab,
                              base::WeakPtr<content::WebContents> page,
                              base::OnceCallback<bool(bool)> done, bool ready) {
              const bool active = ready && lab && page &&
                                  lab->web_contents() == page.get();
              const bool accepted = std::move(done).Run(active);
              if (active && accepted && lab && page) {
                lab->shell_->CloseOverlays(true);
                lab->ReloadWorkspacePage(*page, /*bypass_cache=*/false);
              } else if (ready && page) {
                ProfileContext::ClearAdblockTrial(*page);
              }
            }, lab, page->GetWeakPtr(), std::move(done)));
      }, weak, workspace);
  ad_blocking_rules_panel_->Show(model_.workspaces()[workspace].profile_id,
                                 shell_->general_widget(), std::move(callbacks));
}

void WorkspaceLab::RefreshAdBlockingRules() {
  shell_->RefreshAdBlockingPreferences();
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce([](base::WeakPtr<WorkspaceLab> lab) {
        if (lab && lab->ad_blocking_rules_panel_)
          lab->ad_blocking_rules_panel_->Refresh();
      }, weak_factory_.GetWeakPtr()));
}

BrowserPreferences WorkspaceLab::ReadPreferences() const {
  if (!model_.active())
    return {};
  auto& profile = *WorkspaceProfile(active_index_);
  BrowserPreferences settings;
  const auto* extension_prefs = IsWorkspaceExtensionRuntime() && !private_owner_
      ? extensions::ExtensionPrefs::Get(&profile) : nullptr;
  const auto pinned_extensions = extension_prefs
      ? extension_prefs->GetPinnedExtensions() : extensions::ExtensionIdList();
  for (const auto& manager : managed_extensions_) {
    if (manager->OwnsProfile(profile)) {
      settings["extension_enabled." + manager->id()] = {
          .value = manager->selection(), .editable = manager->editable()};
      if (const auto allowed = manager->UserScriptsAllowed()) {
        settings["extension_user_scripts." + manager->id()] = {
            .value = *allowed, .editable = manager->editable() && manager->enabled()};
      }
      if (manager->options_page().is_valid())
        settings["extension_options." + manager->id()] = {.editable = CanEditWorkspace()};
      if (manager->action_popup().is_valid()) {
        settings["extension_popup." + manager->id()] = {.editable = CanEditWorkspace()};
        settings["extension_pinned." + manager->id()] = {
            .value = std::ranges::contains(pinned_extensions, manager->id()),
            .editable = CanEditWorkspace() && extension_prefs};
      }
    }
  }
  if (const auto* probe = ExtensionProbeForProfile(profile)) {
    if (const auto enabled = probe->UserEnabled()) {
      settings["extension_enabled"] = {
          .value = *enabled, .editable = probe->CanSetUserEnabled()};
    }
  }
  if (CanConfigureAssistant()) {
    const bool enabled = assistant_settings_ && assistant_settings_->enabled;
    settings["assistant_enabled"] = {.value = enabled, .editable = CanEditWorkspace()};
    settings["assistant_tabs"] = {
        .value = assistant_settings_ && assistant_settings_->defaults.tabs,
        .editable = enabled && CanEditWorkspace()};
    settings["assistant_pages"] = {
        .value = assistant_settings_ && assistant_settings_->defaults.pages,
        .editable = enabled && CanEditWorkspace()};
    settings["assistant_actions"] = {
        .value = assistant_settings_ && assistant_settings_->defaults.actions,
        .editable = enabled && CanEditWorkspace()};
    settings["assistant_history"] = {
        .value = assistant_settings_ && assistant_settings_->defaults.history,
        .editable = enabled && CanEditWorkspace()};
    settings["assistant_confirm_clicks"] = {
        .value = !assistant_settings_ || assistant_settings_->defaults.confirm_clicks,
        .editable = enabled && CanEditWorkspace()};
    settings["assistant_web"] = {
        .value = assistant_settings_ && assistant_settings_->defaults.web,
        .editable = enabled && CanEditWorkspace()};
  }
  settings["shift_click_peek"] = {.value = shift_click_peek_, .editable = true};
  settings["clean_address_copies"] = {
      .value = CleanAddressCopies(), .editable = !private_owner_};
  const auto& zoom_values = prototype::kWorkspaceZoomPercentages;
  const int zoom_percent = std::lround(
      blink::ZoomLevelToZoomFactor(profile.default_page_zoom_level()) * 100);
  const auto zoom = std::ranges::find(zoom_values, zoom_percent);
  settings["zoom"] = {
      .value = zoom == zoom_values.end() ? -1
                                       : static_cast<int>(zoom - zoom_values.begin()),
      .editable = CanEditWorkspace() && profile.CanSetDefaultPageZoomLevel()};
  auto& permissions = profile.site_permissions();
  if (permissions.CanEditSitePermissions()) {
    settings["popups"] = {.value = permissions.GetPopupDefault(), .editable = true};
  }
  if (const auto saving = profile.GetPasswordSavingSetting())
    settings["save_passwords"] = {.value = *saving, .editable = true};
  if (const auto protection = profile.GetFingerprintProtection())
    settings["fingerprint_protection"] = {.value = *protection,
                                          .editable = true};
  if (const auto blocking = profile.GetAdblockEnabled()) {
    settings["ad_blocking"] = {.value = *blocking, .editable = true};
    if (AdblockLists()) {
      settings["ad_blocking_stealth"] = {
          .value = profile.GetAdblockListEnabled("zitter") &&
                   profile.GetAdblockListEnabled("antiadblock"),
          .editable = *blocking};
    }
  }
  return settings;
}

bool WorkspaceLab::WritePreference(std::string key, int value) {
  if (!model_.active())
    return false;
  auto& profile = *WorkspaceProfile(active_index_);
  if (key == "zoom") {
    const auto& zoom_values = prototype::kWorkspaceZoomPercentages;
    return CanEditWorkspace() && value >= 0 &&
           static_cast<size_t>(value) < zoom_values.size() &&
           profile.SetDefaultPageZoomLevel(
               blink::ZoomFactorToZoomLevel(zoom_values[value] / 100.0));
  }
  if (value != 0 && value != 1)
    return false;
  if (key.starts_with("assistant_"))
    return WriteAssistantPreference(key, value == 1);
  if (std::string_view preference = key; preference.starts_with("extension_enabled.")) {
    preference.remove_prefix(std::string_view("extension_enabled.").size());
    auto* manager = ManagedExtensionForProfile(profile, preference);
    return manager && CanEditWorkspace() && manager->SetEnabled(value == 1);
  }
  constexpr std::string_view kUserScriptsPrefix = "extension_user_scripts.";
  if (std::string_view preference = key; preference.starts_with(kUserScriptsPrefix)) {
    auto* manager = ManagedExtensionForProfile(profile, preference.substr(kUserScriptsPrefix.size()));
    return manager && CanEditWorkspace() && manager->SetUserScriptsAllowed(value == 1);
  }
  constexpr std::string_view kPinnedPrefix = "extension_pinned.";
  if (key.starts_with(kPinnedPrefix))
    return SetExtensionToolbarPinned(key.substr(kPinnedPrefix.size()), value == 1);
  if (key == "extension_enabled") {
    if (!CanEditWorkspace())
      return false;
    auto* probe = ExtensionProbeForProfile(profile);
    return probe && probe->SetUserEnabled(value == 1);
  }
  if (key == "shift_click_peek") {
    if (!CanEditWorkspace())
      return false;
    shift_click_peek_ = value == 1;
    PersistWorkspaceChanges();
    return true;
  }
  if (key == "clean_address_copies") {
    if (private_owner_ || !CanEditWorkspace())
      return false;
    clean_address_copies_ = value == 1;
    PersistWorkspaceChanges();
    return true;
  }
  if (key == "popups")
    return CanEditWorkspace() && profile.site_permissions().SetPopupDefault(value == 1);
  if (key == "fingerprint_protection") {
    if (!profile.SetFingerprintProtection(value == 1))
      return false;
    fingerprint_toolbar_->Close();
    UpdateFingerprintToolbar();
    return true;
  }
  if (key == "ad_blocking") {
    if (!CanEditWorkspace() || !profile.SetAdblockEnabled(value == 1))
      return false;
    if (value && AdblockLists())
      AdblockLists()->Refresh(false);
    UpdateAdblockShield();
    return true;
  }
  if (key == "ad_blocking_stealth" && AdblockLists() && CanEditWorkspace()) {
    const bool builtin = profile.SetAdblockListEnabled("zitter", value == 1);
    const bool warning = profile.SetAdblockListEnabled("antiadblock", value == 1);
    if (value)
      AdblockLists()->Refresh(false);
    return builtin && warning;
  }
  return key == "save_passwords" && profile.SetPasswordSavingSetting(value == 1);
}

void WorkspaceLab::UpdateFingerprintToolbar() {
  auto& profile = *WorkspaceProfile(active_index_);
  const auto enabled = profile.GetFingerprintProtection();
  const bool private_mode = profile.IsOffTheRecord();
  const bool visible = !smoke_ && (private_mode || enabled.value_or(false));
  if (fingerprint_button_->GetVisible() != visible) {
    fingerprint_button_->SetVisible(visible);
    fingerprint_button_->parent()->InvalidateLayout();
  }
  fingerprint_toolbar_->UpdateButton(private_mode);
  fingerprint_toolbar_->Refresh();
}

void WorkspaceLab::SelectWorkspace(std::string id) {
  const auto index = WorkspaceIndex(id);
  if (index && *index != active_index_ && PeekPage()) {
    if (!CanEditWorkspace() ||
        DeferPeekClose(base::BindOnce(&WorkspaceLab::SelectWorkspace,
                                     weak_factory_.GetWeakPtr(), id))) {
      return;
    }
    DiscardPeek();
  }
  const bool waking = index && sleeping_[*index];
  if (!saving_session_ && !saving_catalogue_ && !before_unload_batch_.pending() &&
      controller_.Activate(id)) {
    RefreshShell();
    tabs_[active_index_][selected_[active_index_]].view->RequestFocus();
    if (waking)
      PersistWorkspaceChanges();
  }
}

std::vector<prototype::PaletteResult> WorkspaceLab::WorkspaceCommandResults(
    std::u16string query) const {
  if (smoke_ || !model_.active() || !CanEditWorkspace())
    return {};

  sidebar::CommandContext context{};
  context.reopen = LastClosedTab().has_value() || HistoricalClosedTab().has_value();
  auto* page = web_contents();
  const auto* navigation =
      page ? page->GetController().GetLastCommittedEntry() : nullptr;
  context.page = page != nullptr;
  context.http_page =
      navigation && navigation->GetVirtualURL().SchemeIsHTTPOrHTTPS();
  context.can_back = page && page->GetController().CanGoBack();
  context.can_forward = page && page->GetController().CanGoForward();
  context.split = SplitShown();
  context.tab_count = tabs_[active_index_].size();
  context.assistant = assistant_host_settings_.enabled;
  auto& profile = *WorkspaceProfile(active_index_);
  const auto& active = model_.active()->id;
  bool other_can_sleep = false;
  for (size_t index = 0; index < tabs_.size(); ++index) {
    const auto& workspace = model_.workspaces()[index];
    const bool capturing = WorkspaceHasCapture(index);
    other_can_sleep |= index != active_index_ && !sleeping_[index] && !capturing;
    context.workspaces.push_back(
        {workspace.id, workspace.name,
         sleeping_[index], workspace.profile_id == model_.active()->profile_id,
         capturing});
  }
  const bool commands_only = query.starts_with(u">");
  const bool compact = !commands_only && base::TrimWhitespace(query, base::TRIM_ALL).empty();
  if (!commands_only)
    query.insert(query.begin(), u'>');

  std::vector<prototype::PaletteResult> results;
  for (const auto& entry :
       sidebar::command_catalogue(context, base::UTF16ToUTF8(query))) {
    if (entry.id == "select_space" && std::string_view(entry.target) == active)
      continue;
    if (entry.id == "sleep_others" && !other_can_sleep) {
      continue;
    }
    const std::string_view id(entry.id);
    if ((id == "reload_workspace_icons" && !commands_only) ||
        ((id == "spaces" || id == "profiles") && private_owner_) ||
        (id == "assistant_settings" && !CanConfigureAssistant()) ||
        (id == "timeline" && !timeline_settings_ready_) ||
        (id == "save_snapshot" && !timeline_enabled_) ||
        (id == "import" && !CanImportBrowser()) ||
        (id == "history" && profile.IsOffTheRecord()) ||
        (id == "close_other_tabs" && (PeekPage() || OtherUnpinnedTabs().empty())) ||
        (id == "passwords" && !profile.GetPasswordSavingSetting().has_value())) {
      continue;
    }
    bool page_tool = std::ranges::contains(kPagePaletteShortcuts, id);
    if (id == "move_tab" || id == "copy_tab") {
      const auto destination = WorkspaceIndex(std::string_view(entry.target));
      if (private_owner_ || PeekPage() || !destination ||
          *destination == active_index_ ||
          tabs_[*destination].size() >= prototype::kMaxWorkspaceSessionTabs ||
          (id == "move_tab" &&
           (page->IsBeingCaptured() || media_access_->IsCapturing(page))) ||
          (id == "copy_tab" &&
           (!navigation->GetURL().SchemeIsHTTPOrHTTPS() ||
            !CanRestoreWorkspaceURL(navigation->GetURL()) ||
            navigation->GetURL().spec().size() > 32768))) {
        continue;
      }
      page_tool = true;
    }
#if BUILDFLAG(ENABLE_PRINTING) && BUILDFLAG(ENABLE_PDF)
    page_tool |= id == "print" && context.http_page && !page->IsLoading();
#endif
    if (page_tool && !navigation && id != "close_other_tabs")
      continue;
    const bool workspace_action = id == "select_space" || id == "sleep_space" ||
                                  id == "sleep_others" || id == "save_snapshot";
    if (!page_tool && !workspace_action &&
        !std::ranges::contains(kWorkspacePaletteShortcuts, id)) {
      continue;
    }
    prototype::PaletteResult result;
    result.title = base::UTF8ToUTF16(std::string(entry.title));
    result.detail = base::UTF8ToUTF16(std::string(entry.detail));
    result.workspace_id = active;
    result.command_id = std::string(entry.id);
    result.target_workspace_id = std::string(entry.target);
    if (page_tool) {
      result.command_page_id = navigation ? navigation->GetUniqueID() : 0;
      result.command_tab_id = tabs_[active_index_][selected_[active_index_]].id;
    }
    results.push_back(std::move(result));
  }
  shell_->RankCommandResults(results);
  if (compact && results.size() > 6)
    results.resize(6);
  return results;
}

void WorkspaceLab::ActivateWorkspaceCommand(prototype::PaletteResult result) {
  if (!model_.active() || result.workspace_id != model_.active()->id ||
      !CanEditWorkspace()) {
    return;
  }
  const auto available = WorkspaceCommandResults(u">");
  if (!std::ranges::any_of(available, [&](const auto& current) {
        return current.command_id == result.command_id &&
               current.target_workspace_id == result.target_workspace_id &&
               current.command_page_id == result.command_page_id &&
               current.command_tab_id == result.command_tab_id;
      })) {
    return;
  }
  shell_->RecordCommandUse(result.command_id, result.target_workspace_id);
  if (result.command_id == "select_space") {
    SelectWorkspace(std::move(result.target_workspace_id));
  } else if (result.command_id == "sleep_space") {
    SleepWorkspace(result.target_workspace_id);
  } else if (result.command_id == "sleep_others") {
    SleepOtherWorkspaces();
  } else if (result.command_id == "save_snapshot") {
    if (SaveTimelineSnapshot("Command Center"))
      shell_->OpenTimeMachine();
  } else if (result.command_tab_id) {
    auto* page = web_contents();
    const auto* entry = page ? page->GetController().GetLastCommittedEntry() : nullptr;
    const size_t selected = selected_[active_index_];
    if ((entry ? entry->GetUniqueID() : 0) != result.command_page_id ||
        tabs_[active_index_][selected].id != result.command_tab_id) {
      return;
    }
    if (result.command_id != "move_tab" && result.command_id != "copy_tab") {
      RunShortcut(std::move(result.command_id));
      return;
    }
    const auto destination = WorkspaceIndex(result.target_workspace_id);
    if (private_owner_ || PeekPage() || !destination ||
        *destination == active_index_ ||
        tabs_[*destination].size() >= prototype::kMaxWorkspaceSessionTabs) {
      return;
    }
    const bool same_profile =
        WorkspaceProfile(active_index_) == WorkspaceProfile(*destination);
    if (result.command_id == "move_tab") {
      if (same_profile)
        MoveSidebarTab(selected, std::move(result.target_workspace_id));
      return;
    }
    const GURL url = entry->GetURL();
    if (same_profile || !url.SchemeIsHTTPOrHTTPS() ||
        !CanRestoreWorkspaceURL(url) || url.spec().size() > 32768) {
      return;
    }
    const auto title = page->GetTitle().substr(0, 8192);
    SelectWorkspace(std::move(result.target_workspace_id));
    if (active_index_ == *destination) {
      AddTab(*destination, url, title);
      SelectTab(tabs_[*destination].size() - 1);
      PersistWorkspaceChanges();
    }
  } else if (std::ranges::contains(kWorkspacePaletteShortcuts,
                                   result.command_id)) {
    RunShortcut(std::move(result.command_id));
  }
}

bool WorkspaceLab::ActivateWorkspaceCommandForProbe(std::u16string query,
                                                   std::string_view id) {
  auto results = WorkspaceCommandResults(std::move(query));
  auto found = std::ranges::find(results, id, &prototype::PaletteResult::command_id);
  if (found == results.end())
    return false;
  ActivateWorkspaceCommand(std::move(*found));
  return true;
}

void WorkspaceLab::CheckCommandCenterProbe(bool reopening,
                                           base::TimeTicks deadline) {
  if (!shell_->tools_ready()) {
    if (base::TimeTicks::Now() >= deadline) {
      LOG(ERROR) << "Command Center tools did not load";
      Finish(false);
      return;
    }
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&WorkspaceLab::CheckCommandCenterProbe,
                       weak_factory_.GetWeakPtr(), reopening, deadline),
        base::Milliseconds(25));
    return;
  }

  constexpr char kChord[] = "Alt+Shift+F9";
  const auto fail = [this] {
    LOG(ERROR) << "Command Center ranking or shortcut did not match the fixture";
    Finish(false);
  };
  if (!reopening) {
    const auto original = WorkspaceCommandResults({});
    auto find = WorkspaceCommandResults(u">find");
    auto stale = std::ranges::find(find, "find", &prototype::PaletteResult::command_id);
    if (original.size() != 6 || original.front().command_id == "downloads" ||
        stale == find.end()) {
      fail();
      return;
    }
    SelectTab(0);
    ActivateWorkspaceCommand(std::move(*stale));
    SelectTab(kProbeSelection[0]);
    if (!shell_->SetShortcut("downloads", kChord) ||
        !RunProbePaletteCommand(u">downloads", "downloads", {}) ||
        !downloads_->widget() || !downloads_->widget()->IsVisible()) {
      fail();
      return;
    }
  }
  const auto ranked = WorkspaceCommandResults({});
  const auto chord = prototype::ParseShortcut(kChord);
  if (!chord || ranked.size() != 6 ||
      ranked.front().command_id != "downloads" ||
      !ranked.front().detail.ends_with(
          u" · " + base::UTF8ToUTF16(prototype::DisplayShortcut(*chord))) ||
      shell_->browser_tools().CommandScore("find", "", base::Time::Now()) != 0) {
    fail();
    return;
  }
  SelectWorkspace(std::string(kWorkspaceIds[1]));
  std::puts("Zitter Content Command Center ranking passed");
  RequestClose();
}

bool WorkspaceLab::WorkspaceHasCapture(size_t workspace) const {
  return std::ranges::any_of(WorkspacePages(workspace), [this](auto* page) {
    return page->IsBeingCaptured() || media_access_->IsCapturing(page);
  });
}

bool WorkspaceLab::SleepWorkspace(std::string_view id) {
  const auto index = WorkspaceIndex(id);
  if (!CanEditWorkspace() || !index || private_owner_)
    return false;
  const size_t workspace = *index;
  if (sleeping_[workspace])
    return false;
  if (WorkspaceHasCapture(workspace)) {
    shell_->SetOperationStatus(prototype::WorkspaceShell::OperationStatusEvent::kStopCapture);
    return false;
  }
  const auto pages = WorkspacePages(workspace);
  if (std::ranges::none_of(pages, [](auto* page) {
        return page->NeedToFireBeforeUnload();
      })) {
    return CommitWorkspaceSleep(workspace);
  }

  CheckBeforeUnload(pages, base::BindOnce([](base::WeakPtr<WorkspaceLab> lab,
                                           size_t workspace) {
    if (lab)
      lab->CommitWorkspaceSleep(workspace);
  }, weak_factory_.GetWeakPtr(), workspace));
  return true;
}

void WorkspaceLab::SleepOtherWorkspaces() {
  if (!CanEditWorkspace() || private_owner_)
    return;
  std::vector<size_t> targets;
  std::vector<content::WebContents*> pages;
  for (size_t index = 0; index < tabs_.size(); ++index) {
    if (index == active_index_ || sleeping_[index] || WorkspaceHasCapture(index))
      continue;
    targets.push_back(index);
    auto workspace_pages = WorkspacePages(index);
    pages.insert(pages.end(), workspace_pages.begin(), workspace_pages.end());
  }
  auto sleep = base::BindOnce([](base::WeakPtr<WorkspaceLab> lab,
                                 std::vector<size_t> targets) {
    if (lab) {
      for (size_t target : targets)
        lab->CommitWorkspaceSleep(target);
    }
  }, weak_factory_.GetWeakPtr(), std::move(targets));
  if (std::ranges::any_of(pages, &content::WebContents::NeedToFireBeforeUnload))
    CheckBeforeUnload(pages, std::move(sleep));
  else
    std::move(sleep).Run();
}

bool WorkspaceLab::CommitWorkspaceSleep(size_t workspace) {
  if (!CanEditWorkspace() || sleeping_[workspace])
    return false;
  if (WorkspaceHasCapture(workspace)) {
    shell_->SetOperationStatus(prototype::WorkspaceShell::OperationStatusEvent::kStopCapture);
    return false;
  }
  const bool switching = workspace == active_index_;
  if (switching) {
    if (tabs_.size() < 2)
      return false;
    const size_t next = (workspace + 1) % tabs_.size();
    if (!controller_.Activate(model_.workspaces()[next].id))
      return false;
  }

  if (peek_workspace_id_ == model_.workspaces()[workspace].id)
    DiscardPeek();
  for (auto& tab : tabs_[workspace]) {
    auto* page = tab.view->web_contents();
    if (!page)
      continue;
    const auto url = WorkspaceDocumentURL(*page);
    if (!url.is_empty()) {
      tab.deferred_url = CanRestoreWorkspaceURL(url) ? url : GURL();
      const auto* entry = page->GetController().GetLastCommittedEntry();
      tab.deferred_title = entry && !tab.deferred_url.is_empty()
                               ? entry->GetTitleForDisplay()
                               : std::u16string();
    }
    tab.native_muted = page->IsAudioMuted();
    SetWorkspaceExtensionTabPage(tab.extension_tab.get(), nullptr);
    tab.view->SetWebContents(nullptr);
  }
  sleeping_[workspace] = true;
  RefreshShell();
  if (switching)
    tabs_[active_index_][selected_[active_index_]].view->RequestFocus();
  PersistWorkspaceChanges();
  return true;
}

WorkspaceLab::Tab WorkspaceLab::MakeTab(size_t workspace, views::View& host) {
  const uint64_t id = next_tab_id_++;
  auto* view = host.AddChildView(
      std::make_unique<WorkspaceWebView>(WorkspaceProfile(workspace),
          media_access_->GetWeakPtr(), base::BindRepeating(
              [](base::WeakPtr<WorkspaceLab> lab, uint64_t id, GURL url) {
                if (lab && std::ranges::contains(lab->tabs_[lab->active_index_], id,
                                                  &Tab::id)) {
                  lab->Navigate(url, /*new_tab=*/true);
                }
              }, weak_factory_.GetWeakPtr(), id),
          base::BindRepeating(
              [](base::WeakPtr<WorkspaceLab> lab, content::RenderFrameHost& frame,
                 const content::ContextMenuParams& params) {
                return lab && lab->ShowPageMenu(frame, params);
              }, weak_factory_.GetWeakPtr()),
          base::BindRepeating(
              [](base::WeakPtr<WorkspaceLab> lab, content::WebContents* source,
                 std::unique_ptr<content::WebContents> page, const GURL& target,
                 WindowOpenDisposition disposition, bool user_gesture,
                 bool shift_link) {
                return lab ? lab->AdoptPage(source, std::move(page), target,
                                            disposition, user_gesture, shift_link)
                           : nullptr;
              }, weak_factory_.GetWeakPtr()),
          base::BindRepeating(
              [](base::WeakPtr<WorkspaceLab> lab, content::WebContents* source,
                 const content::OpenURLParams& params,
                 base::OnceCallback<void(content::NavigationHandle&)> callback) {
                return lab ? lab->OpenPageURL(source, params, std::move(callback))
                           : nullptr;
              }, weak_factory_.GetWeakPtr()),
          base::BindRepeating(&WorkspaceLab::ClosePopup, weak_factory_.GetWeakPtr()),
          base::BindRepeating(
              [](base::WeakPtr<WorkspaceLab> lab, content::WebContents* page) {
                return lab && lab->CanFullscreenPage(page);
              }, weak_factory_.GetWeakPtr()),
          base::BindRepeating(&WorkspaceLab::SetPageFullscreen, weak_factory_.GetWeakPtr())));
  view->SetPageZoomShortcutFilter(base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, const ui::Accelerator& accelerator) {
        return lab && lab->CanEditWorkspace() &&
               lab->shell_->ShortcutFor(accelerator).empty();
      }, weak_factory_.GetWeakPtr()));
  view->SetPresentationChangedCallback(base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab) {
        if (!lab || std::exchange(lab->tab_audio_refresh_pending_, true))
          return;
        base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE, base::BindOnce([](base::WeakPtr<WorkspaceLab> lab) {
              if (lab) {
                lab->tab_audio_refresh_pending_ = false;
                lab->RefreshShell();
              }
            }, lab));
      }, weak_factory_.GetWeakPtr()));
  view->SetVisible(false);
  return {nullptr, view, id, {}, {}, {},
          view->AddWebContentsFocusedCallback(base::BindRepeating(
              &WorkspaceLab::FocusVisibleView, weak_factory_.GetWeakPtr()))};
}

void WorkspaceLab::AddTab(size_t workspace, GURL deferred_url,
                          std::u16string deferred_title, std::string pin) {
  auto tab = MakeTab(workspace, *shell_->content_host());
  tab.extension_tab = CreateWorkspaceExtensionTab(*WorkspaceProfile(workspace), *this);
  tab.deferred_url = std::move(deferred_url);
  tab.deferred_title = std::move(deferred_title);
  tab.pin = std::move(pin);
  tabs_[workspace].push_back(std::move(tab));
}

void WorkspaceLab::OpenPin(std::string id) {
  if (saving_session_ || before_unload_batch_.pending())
    return;
  const auto pins = WorkspacePins(active_index_);
  const auto pin = std::ranges::find(pins, id, &prototype::PinnedItem::id);
  if (pin == pins.end() || pin->folder)
    return;
  const auto& tabs = tabs_[active_index_];
  const auto tab = std::ranges::find(tabs, id, &Tab::pin);
  if (tab != tabs.end()) {
    SelectTab(tab - tabs.begin());
    return;
  }
  if (tabs.size() >= prototype::kMaxWorkspaceSessionTabs)
    return;
  AddTab(active_index_, pin->url, base::UTF8ToUTF16(pin->title), std::move(id));
  SelectTab(tabs.size() - 1);
  PersistWorkspaceChanges();
}

bool WorkspaceLab::CanEditWorkspace() const {
  return !saving_session_ && !saving_catalogue_ && !before_unload_batch_.pending() &&
         !download_close_prompt_pending_ && !clearing_profile_ &&
         !replacing_timeline_;
}

std::vector<prototype::PinnedItem> WorkspaceLab::WorkspacePins(
    size_t workspace) const {
  auto items = pins_[workspace];
  const auto profile = profile_favorites_.find(model_.workspaces()[workspace].profile_id);
  if (profile != profile_favorites_.end())
    items.insert(items.end(), profile->second.begin(), profile->second.end());
  return items;
}

std::vector<prototype::PinnedItem>& WorkspaceLab::PinStorage(
    size_t workspace, std::string_view id) {
  auto& local = pins_[workspace];
  if (std::ranges::contains(local, id, &prototype::PinnedItem::id))
    return local;
  return profile_favorites_[model_.workspaces()[workspace].profile_id];
}

bool WorkspaceLab::PlacePin(std::vector<prototype::PinnedItem>& pins,
                            prototype::PinnedItem pin, std::string parent,
                            std::string before) {
  if (pin.id == before || (pin.folder && parent == "@profile") ||
      IsGitHubFolder(pins, parent))
    return false;
  pin.profile_pin = parent == "@profile" || pin.profile_pin;
  if (pin.profile_pin && !parent.empty() && parent != "@profile")
    return false;
  if (parent == "@profile")
    parent.clear();
  if (!before.empty()) {
    const auto sibling = std::ranges::find(pins, before, &prototype::PinnedItem::id);
    if (sibling == pins.end() || sibling->parent_id != parent ||
        sibling->profile_pin != pin.profile_pin)
      return false;
  }

  std::vector<size_t> subtree;
  {
    const auto nodes = PinNodes(pins);
    const PinTree tree(nodes);
    if (!tree.CanPlace(pin.id, parent))
      return false;
    subtree = tree.Subtree(pin.id);
  }
  pin.parent_id = std::move(parent);
  std::vector<prototype::PinnedItem> moved;
  moved.reserve(std::max<size_t>(1, subtree.size()));
  moved.push_back(std::move(pin));
  // Stored pins place parents before children. Move the whole subtree so
  // descendants remain after the moved root without changing sibling order.
  for (size_t i = 1; i < subtree.size(); ++i)
    moved.push_back(std::move(pins[subtree[i]]));
  for (auto it = subtree.rbegin(); it != subtree.rend(); ++it)
    pins.erase(pins.begin() + *it);
  const auto at = std::ranges::find(pins, before, &prototype::PinnedItem::id);
  pins.insert(at, std::make_move_iterator(moved.begin()),
              std::make_move_iterator(moved.end()));
  return true;
}

bool WorkspaceLab::CreatePinnedFolder(std::string workspace, std::string parent,
                                      std::string title) {
  const auto position = WorkspaceIndex(workspace);
  title = std::string(base::TrimWhitespaceASCII(title, base::TRIM_ALL));
  if (!CanEditWorkspace() || !position || !IsValidFolderTitle(title))
    return false;
  auto& pins = pins_[*position];
  if (pins.size() >= prototype::kMaxWorkspaceSessionTabs ||
      !PlacePin(pins, {"folder-" + base::Uuid::GenerateRandomV4().AsLowercaseString(),
                      {}, std::move(title), GURL(), true}, std::move(parent), {})) {
    return false;
  }
  RefreshShell();
  PersistWorkspaceChanges();
  return true;
}

bool WorkspaceLab::ConfigureGitHubFolder(std::string workspace, std::string folder,
                                         std::string title,
                                         prototype::LiveFolderConfig config,
                                         std::string token, bool remove_token) {
  const auto* space = model_.active();
  title = std::string(base::TrimWhitespaceASCII(title, base::TRIM_ALL));
  if (!space || space->id != workspace || !CanEditWorkspace() || private_owner_ ||
      smoke_ || session_path_.empty() || !IsValidFolderTitle(title) ||
      !ValidGitHubScope(config) ||
      (!token.empty() && (!os_crypt_ || !ValidGitHubToken(token)))) {
    return false;
  }

  const auto& pins = pins_[active_index_];
  std::optional<prototype::PinnedItem> previous;
  config.encrypted_token.clear();
  if (!folder.empty()) {
    const auto item = std::ranges::find(pins, folder, &prototype::PinnedItem::id);
    if (item == pins.end() || !item->live_folder)
      return false;
    previous = *item;
    if (!remove_token)
      config.encrypted_token = item->live_folder->encrypted_token;
  } else {
    if (pins.size() >= prototype::kMaxWorkspaceSessionTabs)
      return false;
    folder = "github-folder-" + base::Uuid::GenerateRandomV4().AsLowercaseString();
  }

  prototype::PinnedItem updated = previous.value_or(
      prototype::PinnedItem{std::move(folder), {}, {}, GURL(), true});
  updated.title = std::move(title);
  updated.live_folder = std::move(config);
  if (token.empty())
    return SaveGitHubFolder(std::move(workspace), space->profile_id,
                            std::move(previous), std::move(updated));

  os_crypt_->GetInstance(base::BindOnce(
      [](base::WeakPtr<WorkspaceLab> lab, std::string workspace, std::string profile,
         std::optional<prototype::PinnedItem> previous, prototype::PinnedItem updated,
         std::string token, scoped_refptr<os_crypt_async::Encryptor> encryptor) {
        if (!lab)
          return;
        std::string ciphertext;
        const auto credential =
            base::StrCat({GitHubCredentialPrefix(profile, workspace, updated.id), token});
        if (!encryptor || !encryptor->IsEncryptionAvailable() ||
            !encryptor->EncryptString(credential, &ciphertext)) {
          lab->shell_->ShowToast(
              prototype::T(u"Nie udało się bezpiecznie zapisać tokena GitHub"), {}, {});
          return;
        }
        updated.live_folder->encrypted_token = base::Base64Encode(ciphertext);
        if (!lab->SaveGitHubFolder(std::move(workspace), std::move(profile),
                                   std::move(previous), std::move(updated))) {
          lab->shell_->ShowFolderSaveError();
        }
      }, weak_factory_.GetWeakPtr(), std::move(workspace), space->profile_id,
      std::move(previous), std::move(updated), std::move(token)));
  return true;
}

bool WorkspaceLab::SaveGitHubFolder(std::string workspace, std::string profile,
                                    std::optional<prototype::PinnedItem> previous,
                                    prototype::PinnedItem folder) {
  const auto* space = model_.active();
  if (!space || space->id != workspace || space->profile_id != profile ||
      !CanEditWorkspace()) {
    return false;
  }
  auto& pins = pins_[active_index_];
  const GitHubFolderKey key(workspace, folder.id);
  if (previous) {
    const auto item = std::ranges::find(pins, folder.id, &prototype::PinnedItem::id);
    if (item == pins.end() || *item != *previous)
      return false;
    *item = std::move(folder);
  } else {
    if (pins.size() >= prototype::kMaxWorkspaceSessionTabs)
      return false;
    pins.push_back(std::move(folder));
  }
  github_next_refresh_.erase(key);
  if (github_scheduled_folder_ == key)
    github_timer_.Stop();
  RefreshShell();
  PersistWorkspaceChanges();
  return true;
}

void WorkspaceLab::RefreshGitHubFolder(std::string workspace, std::string folder) {
  if (!model_.active() || model_.active()->id != workspace)
    return;
  StartGitHubRefresh(std::move(folder), /*manual=*/true);
}

void WorkspaceLab::ScheduleGitHubRefresh() {
  if (github_refresh_ && !GitHubRefreshIsCurrent())
    github_refresh_.reset();
  if (github_refresh_ || private_owner_ || smoke_ || session_path_.empty() ||
      !model_.active() || !CanEditWorkspace()) {
    github_timer_.Stop();
    return;
  }
  const auto now = base::TimeTicks::Now();
  auto next = base::TimeTicks::Max();
  const auto& workspace = model_.active()->id;
  GitHubFolderKey candidate;
  for (const auto& pin : pins_[active_index_]) {
    if (!pin.live_folder || !ValidGitHubScope(*pin.live_folder))
      continue;
    const GitHubFolderKey key(workspace, pin.id);
    const auto previous = github_next_refresh_.find(key);
    const auto due = previous == github_next_refresh_.end() ? now : previous->second;
    if (due < next) {
      next = due;
      candidate = key;
    }
  }
  if (github_timer_.IsRunning() && candidate == github_scheduled_folder_)
    return;
  github_timer_.Stop();
  if (!candidate.second.empty()) {
    github_scheduled_folder_ = candidate;
    github_timer_.Start(FROM_HERE, std::max(base::Seconds(1), next - now),
                       base::BindOnce(&WorkspaceLab::StartGitHubRefresh,
                                      weak_factory_.GetWeakPtr(), std::move(candidate.second),
                                      /*manual=*/false));
  }
}

void WorkspaceLab::StartGitHubRefresh(std::string folder, bool manual) {
  const auto* space = model_.active();
  if (!space || private_owner_ || smoke_ || session_path_.empty() || !CanEditWorkspace())
    return;
  const auto& pins = pins_[active_index_];
  const auto target = std::ranges::find(pins, folder, &prototype::PinnedItem::id);
  if (target == pins.end() || !target->live_folder || !ValidGitHubScope(*target->live_folder))
    return;
  github_timer_.Stop();
  github_refresh_ = std::make_unique<GitHubRefresh>();
  auto& job = *github_refresh_;
  job.workspace = space->id;
  job.profile = space->profile_id;
  job.folder = std::move(folder);
  job.config = *target->live_folder;
  job.manual = manual;
  github_next_refresh_[{job.workspace, job.folder}] = base::TimeTicks::Now() + base::Minutes(5);

  const auto& login = job.config.account;
  const auto& repository = job.config.filter;
  const auto& ciphertext = job.config.encrypted_token;
  const auto scope = repository.empty() ? "" : " repo:" + repository;
  if (login.empty()) {
    job.queries.push_back(scope);
  } else {
    job.queries.push_back(scope + " review-requested:" + login);
    job.queries.push_back(scope + " author:" + login);
  }

  if (ciphertext.empty()) {
    FetchGitHubResults({});
    return;
  }
  if (!os_crypt_) {
    FinishGitHubRefresh(false);
    return;
  }
  os_crypt_->GetInstance(base::BindOnce(
      [](base::WeakPtr<WorkspaceLab> lab, base::UnguessableToken id,
         scoped_refptr<os_crypt_async::Encryptor> encryptor) {
        if (!lab || !lab->github_refresh_ || lab->github_refresh_->id != id)
          return;
        const auto& job = *lab->github_refresh_;
        std::string ciphertext, credential;
        const auto prefix = GitHubCredentialPrefix(job.profile, job.workspace, job.folder);
        if (!lab->GitHubRefreshIsCurrent() || !encryptor ||
            !base::Base64Decode(job.config.encrypted_token, &ciphertext) ||
            !encryptor->DecryptString(ciphertext, &credential) ||
            !credential.starts_with(prefix) ||
            !ValidGitHubToken(std::string_view(credential).substr(prefix.size()))) {
          lab->FinishGitHubRefresh(false);
          return;
        }
        lab->FetchGitHubResults(credential.substr(prefix.size()));
      }, weak_factory_.GetWeakPtr(), job.id));
}

bool WorkspaceLab::GitHubRefreshIsCurrent() const {
  const auto* space = model_.active();
  if (!github_refresh_ || !space || space->id != github_refresh_->workspace ||
      space->profile_id != github_refresh_->profile || !CanEditWorkspace()) {
    return false;
  }
  const auto& pins = pins_[active_index_];
  const auto folder = std::ranges::find(pins, github_refresh_->folder,
                                       &prototype::PinnedItem::id);
  return folder != pins.end() && folder->live_folder == github_refresh_->config;
}

void WorkspaceLab::FetchGitHubResults(std::string token) {
  if (!GitHubRefreshIsCurrent()) {
    FinishGitHubRefresh(false);
    return;
  }
  auto& job = *github_refresh_;
  auto endpoint = GURL("https://api.github.com/");
  if (session_probe_url_.is_valid() &&
      base::CommandLine::ForCurrentProcess()->HasSwitch(kGitHubFolderProbe)) {
    if (session_probe_url_.host() != "127.0.0.1" ||
        (!token.empty() && token != kGitHubFixtureTokenFirst &&
         token != kGitHubFixtureTokenSecond)) {
      FinishGitHubRefresh(false);
      return;
    }
    endpoint = session_probe_url_;
  }
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = endpoint.Resolve("/search/issues?sort=updated&per_page=100&q=" +
      base::EscapeQueryParamValue("is:open is:pr" + job.queries.back(), true));
  job.queries.pop_back();
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->redirect_mode = network::mojom::RedirectMode::kError;
  request->load_flags = net::LOAD_DISABLE_CACHE;
  request->headers.SetHeader("Accept", "application/vnd.github+json");
  if (!token.empty())
    request->headers.SetHeader("Authorization", "Bearer " + token);

  constexpr auto annotation = net::DefineNetworkTrafficAnnotation(
      "zitter_content_github_live_folder", R"(
        semantics {
          sender: "Zitter GitHub Live Folders"
          description: "Fetches open pull requests for a configured GitHub folder."
          trigger: "The folder is in the active workspace, or the user requests a refresh."
          data: "GitHub login or repository filters and an optional user-provided API token."
          destination: OTHER
          destination_other: "api.github.com"
        }
        policy {
          cookies_allowed: NO
          setting: "The user can remove the folder to stop requests."
          policy_exception_justification: "Requires explicit user configuration."
        })");
  job.loader = network::SimpleURLLoader::Create(std::move(request), annotation);
  job.loader->SetTimeoutDuration(base::Seconds(20));
  job.loader->DownloadToString(
      WorkspaceProfile(active_index_)->GetDefaultStoragePartition()
          ->GetURLLoaderFactoryForBrowserProcess().get(),
      base::BindOnce(&WorkspaceLab::GitHubResultsReceived, weak_factory_.GetWeakPtr(),
                     std::move(token)),
      4 * 1024 * 1024);
}

void WorkspaceLab::GitHubResultsReceived(std::string token, std::optional<std::string> body) {
  github_refresh_->loader.reset();
  if (!GitHubRefreshIsCurrent() || !body) {
    FinishGitHubRefresh(false);
    return;
  }
  auto result = base::JSONReader::ReadDict(*body, base::JSON_PARSE_RFC);
  const auto* items = result ? result->FindList("items") : nullptr;
  if (!items || items->size() > 100 || result->FindBool("incomplete_results").value_or(true)) {
    FinishGitHubRefresh(false);
    return;
  }
  auto& job = *github_refresh_;
  for (const auto& item : *items) {
    const auto* dict = item.GetIfDict();
    const auto* link = dict ? dict->FindString("html_url") : nullptr;
    const auto* title = dict ? dict->FindString("title") : nullptr;
    if (!link || link->size() > 32768 || !title || title->size() > 32768)
      continue;
    GURL url(*link);
    auto key = GitHubPullKey(url);
    if (!key.empty() && job.result_indexes.try_emplace(key, job.results.size()).second) {
      job.results.push_back({"pin-" + base::Uuid::GenerateRandomV4().AsLowercaseString(),
                             job.folder, *title, std::move(url)});
    }
  }
  if (job.queries.empty())
    FinishGitHubRefresh(true);
  else
    FetchGitHubResults(std::move(token));
}

void WorkspaceLab::FinishGitHubRefresh(bool ok) {
  const bool current = GitHubRefreshIsCurrent();
  auto job = std::move(github_refresh_);
  if (!current) {
    ScheduleGitHubRefresh();
    return;
  }
  auto& pins = pins_[active_index_];
  std::set<std::string> removed;
  if (ok) {
    for (const auto& pin : pins) {
      if (pin.folder || pin.parent_id != job->folder)
        continue;
      const auto key = GitHubPullKey(pin.url);
      if (key.empty())
        continue;
      if (const auto found = job->result_indexes.find(key); found != job->result_indexes.end()) {
        auto& replacement = job->results[found->second];
        if (!removed.contains(replacement.id))
          replacement.id = pin.id;
      }
      removed.insert(pin.id);
    }
    ok = pins.size() - removed.size() + job->results.size() <=
         prototype::kMaxWorkspaceSessionTabs;
  }
  if (ok) {
    auto updated = pins;
    std::erase_if(updated, [&](const auto& pin) { return removed.contains(pin.id); });
    const auto folder = std::ranges::find(updated, job->folder, &prototype::PinnedItem::id);
    for (const auto& result : job->results)
      removed.erase(result.id);
    updated.insert(folder + 1, std::make_move_iterator(job->results.begin()),
                   std::make_move_iterator(job->results.end()));
    if (updated != pins) {
      for (auto& tab : tabs_[active_index_]) {
        if (removed.contains(tab.pin))
          tab.pin.clear();
      }
      pins = std::move(updated);
      RefreshShell();
      PersistWorkspaceChanges();
      return;
    }
  } else if (job->manual) {
    shell_->ShowToast(prototype::T(u"Nie udało się odświeżyć folderu GitHub"), {}, {});
  }
  ScheduleGitHubRefresh();
}

bool WorkspaceLab::RenamePinnedFolder(std::string workspace, std::string id,
                                      std::string title) {
  const auto position = WorkspaceIndex(workspace);
  title = std::string(base::TrimWhitespaceASCII(title, base::TRIM_ALL));
  if (!CanEditWorkspace() || !position || !IsValidFolderTitle(title))
    return false;
  auto& pins = pins_[*position];
  const auto folder = std::ranges::find(pins, id, &prototype::PinnedItem::id);
  if (folder == pins.end() || !folder->folder)
    return false;
  folder->title = std::move(title);
  RefreshShell();
  PersistWorkspaceChanges();
  return true;
}

bool WorkspaceLab::RemovePinnedFolder(std::string workspace, std::string id,
                                      bool contents) {
  const auto position = WorkspaceIndex(workspace);
  if (!CanEditWorkspace() || !position)
    return false;
  const size_t index = *position;
  auto& pins = pins_[index];
  const auto folder = std::ranges::find(pins, id, &prototype::PinnedItem::id);
  if (folder == pins.end() || !folder->folder)
    return false;
  if (contents || folder->live_folder) {
    std::vector<size_t> subtree;
    {
      const auto nodes = PinNodes(pins);
      subtree = PinTree(nodes).Subtree(id);
    }
    if (subtree.empty())
      return false;
    for (size_t member : subtree) {
      github_next_refresh_.erase({workspace, pins[member].id});
      for (auto& tab : tabs_[index]) {
        if (tab.pin == pins[member].id)
          tab.pin.clear();
      }
    }
    for (auto it = subtree.rbegin(); it != subtree.rend(); ++it)
      pins.erase(pins.begin() + *it);
  } else {
    for (auto& pin : pins) {
      if (pin.parent_id == id)
        pin.parent_id = folder->parent_id;
    }
    pins.erase(folder);
  }
  RefreshShell();
  PersistWorkspaceChanges();
  return true;
}

bool WorkspaceLab::PinTab(size_t index, std::string parent, std::string before) {
  auto& tabs = tabs_[active_index_];
  if (!CanEditWorkspace() || index >= tabs.size())
    return false;
  auto& tab = tabs[index];
  if (!tab.pin.empty())
    return MovePin(tab.pin, std::move(parent), std::move(before));
  const bool profile_favorite = parent == "@profile";
  const auto url = TabUrl(index, {});
  auto& pins = profile_favorite
                   ? profile_favorites_[model_.active()->profile_id]
                   : pins_[active_index_];
  if (pins.size() >= prototype::kMaxWorkspaceSessionTabs ||
      url.is_empty() || !CanRestoreWorkspaceURL(url) || url.spec().size() > 32768)
    return false;
  auto* page = tab.view->web_contents();
  const auto title = page && !page->GetTitle().empty() ? page->GetTitle()
                                                     : tab.deferred_title;
  std::string id = "pin-" + base::Uuid::GenerateRandomV4().AsLowercaseString();
  if (!PlacePin(pins, {id, {}, title.empty() ? std::string(url.host()) : base::UTF16ToUTF8(title.substr(0, 8192)),
                 url, false, false, std::nullopt, base::UTF16ToUTF8(tab.custom_title)}, std::move(parent), std::move(before))) {
    return false;
  }
  tab.pin = id;
  RefreshShell();
  if (profile_favorite)
    shell_->RevealProfileFavorite(id);
  PersistWorkspaceChanges();
  return true;
}

bool WorkspaceLab::Unpin(std::string id, size_t before) {
  auto& tabs = tabs_[active_index_];
  auto& pins = PinStorage(active_index_, id);
  const auto pin = std::ranges::find(pins, id, &prototype::PinnedItem::id);
  if (!CanEditWorkspace() || before > tabs.size() || pin == pins.end() ||
      pin->folder || IsGitHubFolder(pins, pin->parent_id))
    return false;
  const auto opened = std::ranges::find(tabs, id, &Tab::pin);
  size_t index = opened - tabs.begin();
  if (opened == tabs.end()) {
    if (tabs.size() >= prototype::kMaxWorkspaceSessionTabs)
      return false;
    AddTab(active_index_, pin->url, base::UTF8ToUTF16(pin->title));
  } else {
    opened->pin.clear();
  }
  if (pin->profile_pin) {
    const auto& profile_id = model_.active()->profile_id;
    for (size_t workspace = 0; workspace < tabs_.size(); ++workspace) {
      if (model_.workspaces()[workspace].profile_id != profile_id)
        continue;
      for (auto& tab : tabs_[workspace]) {
        if (tab.pin == id)
          tab.pin.clear();
      }
    }
    if (removed_workspace_ && removed_workspace_->workspace.profile_id == profile_id) {
      for (auto& value : *removed_workspace_->session.FindList("tabs")) {
        auto& tab = value.GetDict();
        if (const auto* pin_id = tab.FindString("pin"); pin_id && *pin_id == id)
          tab.Remove("pin");
      }
    }
  }
  pins.erase(pin);
  return MoveTab(index, before);
}

bool WorkspaceLab::MoveTab(size_t index, size_t before) {
  auto& tabs = tabs_[active_index_];
  if (!CanEditWorkspace() || index >= tabs.size() || before > tabs.size())
    return false;
  auto* moved = tabs[index].extension_tab.get();
  const int original_index = WorkspaceExtensionTabIndex(moved);
  ReorderWorkspaceTab(active_index_, index, before);
  RefreshShell();
  NotifyWorkspaceExtensionTabMoved(moved, original_index);
  PersistWorkspaceChanges();
  return true;
}

void WorkspaceLab::ReorderWorkspaceTab(size_t workspace, size_t index,
                                       size_t before) {
  auto& tabs = tabs_[workspace];
  const size_t destination = before > index ? before - 1 : before;
  if (destination != index) {
    if (destination < index) {
      std::rotate(tabs.begin() + destination, tabs.begin() + index,
                  tabs.begin() + index + 1);
    } else {
      std::rotate(tabs.begin() + index, tabs.begin() + index + 1,
                  tabs.begin() + destination + 1);
    }
    auto remap = [index, destination](size_t& position) {
      if (position == index)
        position = destination;
      else if (index < position && position <= destination)
        --position;
      else if (destination <= position && position < index)
        ++position;
    };
    remap(selected_[workspace]);
    for (size_t& pane : splits_[workspace].tabs)
      remap(pane);
  }
}

bool WorkspaceLab::MovePin(std::string id, std::string parent, std::string before) {
  if (!CanEditWorkspace())
    return false;
  auto& pins = PinStorage(active_index_, id);
  const auto pin = std::ranges::find(pins, id, &prototype::PinnedItem::id);
  if (pin == pins.end() || IsGitHubFolder(pins, pin->parent_id))
    return false;
  const bool profile_favorite = parent == "@profile";
  if (profile_favorite && !pin->profile_pin) {
    auto& favorites = profile_favorites_[model_.active()->profile_id];
    auto promoted = *pin;
    // Older interactive imports numbered pins independently in each space.
    if (id.starts_with("import-pin-"))
      promoted.id = "pin-" + base::Uuid::GenerateRandomV4().AsLowercaseString();
    if (pin->folder || favorites.size() >= prototype::kMaxWorkspaceSessionTabs ||
        !PlacePin(favorites, promoted, std::move(parent), std::move(before))) {
      return false;
    }
    for (auto& tab : tabs_[active_index_]) {
      if (tab.pin == id)
        tab.pin = promoted.id;
    }
    pins.erase(pin);
    id = std::move(promoted.id);
  } else if (!PlacePin(pins, *pin, std::move(parent), std::move(before))) {
    return false;
  }
  RefreshShell();
  if (profile_favorite)
    shell_->RevealProfileFavorite(id);
  PersistWorkspaceChanges();
  return true;
}

bool WorkspaceLab::UpdatePinnedUrl(std::string workspace, std::string id, GURL url) {
  const auto position = WorkspaceIndex(workspace);
  if (!CanEditWorkspace() || !position || url.is_empty() ||
      !CanRestoreWorkspaceURL(url) || url.spec().size() > 32768) {
    return false;
  }
  auto& pins = PinStorage(*position, id);
  const auto pin = std::ranges::find(pins, id, &prototype::PinnedItem::id);
  if (pin == pins.end() || pin->folder || IsGitHubFolder(pins, pin->parent_id))
    return false;
  pin->url = std::move(url);
  RefreshShell();
  PersistWorkspaceChanges();
  return true;
}

bool WorkspaceLab::ResetPinnedUrl(std::string id) {
  if (!CanEditWorkspace())
    return false;
  const auto url = TabUrl(std::nullopt, id);
  if (url.is_empty())
    return false;
  auto& tabs = tabs_[active_index_];
  const auto tab = std::ranges::find(tabs, id, &Tab::pin);
  if (tab != tabs.end() && !tab->view->web_contents())
    tab->deferred_url = url;
  OpenPin(id);
  if (tabs[selected_[active_index_]].pin != id)
    return false;
  return web_contents()->GetVisibleURL() == url || Navigate(url, false);
}

void WorkspaceLab::AttachPageServices(content::WebContents& page,
                                      size_t workspace) {
  if (!private_owner_ && IsManagedExtensionSession()) {
    AttachExtensionsManager(page, base::BindRepeating(
        [](base::WeakPtr<WorkspaceLab> lab, content::WebContents& page,
           const base::DictValue& request) {
          return lab ? lab->DispatchExtensionsManager(page, request)
                     : base::DictValue();
        }, weak_factory_.GetWeakPtr()));
  }
  prototype::LinkNavigation::Attach(&page, base::BindRepeating(
      [](base::WeakPtr<WorkspaceLab> lab, content::NavigationHandle& navigation) {
        return lab && lab->RouteNavigation(navigation);
      }, weak_factory_.GetWeakPtr()));
#if BUILDFLAG(ENABLE_PRINTING) && BUILDFLAG(ENABLE_PDF)
  AttachScriptedPrint(
      page, base::BindRepeating(&WorkspaceLab::RequestScriptedPrint,
                                weak_factory_.GetWeakPtr()));
#endif
  ConfigurePagePasswordPrompt(
      page, base::UTF8ToUTF16(WorkspaceProfileName(workspace)),
      base::BindRepeating(
          [](base::WeakPtr<WorkspaceLab> lab) {
            return lab ? lab->shell_->password_anchor_in_screen() : gfx::Rect();
          }, weak_factory_.GetWeakPtr()));
  prototype::PageDialogs::Attach(
      &page, base::BindRepeating(
                [](std::u16string label, content::BrowserContext*) { return label; },
                base::UTF8ToUTF16(WorkspaceProfileName(workspace))));
}

bool WorkspaceLab::CanFullscreenPage(content::WebContents* page) const {
  if (!page || !widget_->IsVisible() || !CanEditWorkspace())
    return false;
  if (fullscreen_page_)
    return page == fullscreen_page_.get() && widget_->IsActive();
  if (auto* peek = PeekPage()) {
    return page == peek && shell_->peek_widget()->IsVisible() &&
           shell_->peek_widget()->IsActive();
  }
  return page == web_contents() && widget_->IsActive();
}

bool WorkspaceLab::ExitPageFullscreen() {
  if (!fullscreen_page_)
    return false;
  fullscreen_page_->ExitFullscreen(/*will_cause_resize=*/true);
  return true;
}

void WorkspaceLab::SetPageFullscreen(content::WebContents* page, bool enter) {
  if (enter) {
    if (!CanFullscreenPage(page) || fullscreen_page_)
      return;
    auto& tabs = tabs_[active_index_];
    const bool from_peek = page == PeekPage();
    auto* view = from_peek ? peek_tab_->view.get()
                           : tabs[selected_[active_index_]].view.get();
    window_was_fullscreen_ = widget_->IsFullscreen();
    fullscreen_page_ = page->GetWeakPtr();
    for (auto& tab : tabs) {
      if (tab.view != view && tab.view->GetVisible())
        HideTabView(*tab.view);
    }
    if (from_peek) {
      auto owned = shell_->peek_host()->RemoveChildViewT(view);
      shell_->content_host()->AddChildView(std::move(owned));
      shell_->HidePeek();
      page->UpdateWebContentsVisibility(content::Visibility::VISIBLE);
    }
    shell_->SetContentFullscreen(view);
    view->holder()->SetNativeViewCornerRadii(gfx::RoundedCornersF());
    widget_->SetFullscreen(true);
    if (from_peek) {
      widget_->Activate();
      view->RequestFocus();
    }
  } else {
    if (fullscreen_page_.get() != page)
      return;
    fullscreen_page_.reset();
    shell_->SetContentFullscreen(nullptr);
    const bool from_peek = page == PeekPage();
    if (from_peek) {
      auto* view = peek_tab_->view.get();
      auto owned = shell_->content_host()->RemoveChildViewT(view);
      shell_->peek_host()->AddChildView(std::move(owned));
      view->holder()->SetNativeViewCornerRadii(gfx::RoundedCornersF(10));
    } else if (const auto position = TabForPage(page)) {
      auto* view = tabs_[position->first][position->second].view.get();
      view->holder()->SetNativeViewCornerRadii(gfx::RoundedCornersF(10));
    }
    widget_->SetFullscreen(window_was_fullscreen_);
    ShowTab();
    if (from_peek) {
      const auto* workspace = model_.Find(peek_workspace_id_);
      CHECK(workspace);
      shell_->ShowPeek(base::UTF8ToUTF16(
          workspace->name + " · " + std::string(page->GetLastCommittedURL().host())));
      peek_tab_->view->RequestFocus();
    }
  }
  widget_->LayoutRootViewIfNecessary();
  if (auto* frame = page->GetPrimaryMainFrame())
    frame->GetRenderWidgetHost()->SynchronizeVisualProperties();
}

bool WorkspaceLab::IsExtensionWindowFocused(content::BrowserContext& context) const {
  return widget_ && widget_->IsActive() && model_.active() &&
         WorkspaceProfile(active_index_) == &context;
}

void WorkspaceLab::OnWidgetActivationChanged(views::Widget* widget, bool active) {
  if (widget == widget_.get() && !private_owner_ && IsWorkspaceExtensionRuntime() &&
      model_.active()) {
    DispatchWorkspaceExtensionWindowEvent(*WorkspaceProfile(active_index_), active);
  }
}

void WorkspaceLab::OnWidgetShowStateChanged(views::Widget* widget) {
  if (widget == widget_.get() && fullscreen_page_ && !widget->IsFullscreen()) {
    window_was_fullscreen_ = false;
    ExitPageFullscreen();
  }
}

void WorkspaceLab::ShowTab() {
  if (fullscreen_page_) {
    if (fullscreen_page_.get() == PeekPage() ||
        tabs_[active_index_][selected_[active_index_]].view->web_contents() ==
            fullscreen_page_.get()) {
      return;
    }
    ExitPageFullscreen();
  }
  CancelPermissionPrompt();
  base::AutoReset<bool> updating(&updating_panes_, true);
  auto& tabs = tabs_[active_index_];
  const size_t selected = selected_[active_index_];
  const auto& split = splits_[active_index_];
  const bool show_split = SplitShown();
  const auto visible = show_split ? split.tabs : std::vector<size_t>{selected};
  for (size_t index = 0; index < tabs.size(); ++index) {
    if (tabs[index].view->GetVisible() && !std::ranges::contains(visible, index))
      HideTabView(*tabs[index].view);
  }
  std::vector<views::View*> panes;
  std::vector<size_t> pending_navigation;
  std::vector<content::WebContents*> restored_pages;
  for (size_t index : visible) {
    auto& tab = tabs[index];
    if (tab.detached_view)
      shell_->content_host()->AddChildView(std::move(tab.detached_view));
    auto* view = tab.view.get();
    const bool needs_contents = !view->web_contents();
    const bool restoring = !!tab.discarded_page;
    if (restoring)
      view->SetOwnedWebContents(std::move(tab.discarded_page));
    auto* page = view->GetWebContents(
        tab.deferred_url.is_empty() || prototype::IsRssTabUrl(tab.deferred_url)
            ? GURL("about:blank") : tab.deferred_url);
    if (needs_contents) {
      SetWorkspaceExtensionTabPage(tab.extension_tab.get(), page);
      if (!tab.extension_tab && tab.native_muted)
        page->SetAudioMuted(true);
      AttachPageServices(*page, active_index_);
      if (restoring)
        restored_pages.push_back(page);
      else if (!tab.deferred_url.is_empty())
        pending_navigation.push_back(index);
    }
    panes.push_back(view);
  }
  auto* page = tabs[selected].view->web_contents();
  if (web_contents() != page) {
    ClearAssistantAdblockPreview();
#if BUILDFLAG(ENABLE_PRINTING)
    shell_->HidePrint();
    pdf_export_.reset();
#endif
    shell_->ResetFind();
    find_page_.reset();
    media_access_->Stop();
  }
  if (devtools_ && !devtools_->IsOpenFor(page))
    devtools_.reset();
  shell_->SetPanes(std::move(panes), show_split ? split.weights : std::vector<double>(),
                   show_split && split.vertical);
  // Native child views become visible immediately. Set their new bounds
  // first, especially when switching between horizontal and vertical splits.
  shell_->content_host()->DeprecatedLayoutImmediately();
  for (size_t index : visible) {
    auto* view = tabs[index].view.get();
    view->SetVisible(true);
    view->web_contents()->UpdateWebContentsVisibility(content::Visibility::VISIBLE);
  }
  Observe(page);
  for (auto* restored : restored_pages)
    restored->GetController().LoadIfNecessary();
  for (size_t index : pending_navigation) {
    auto& tab = tabs[index];
    if (prototype::IsRssTabUrl(tab.deferred_url)) {
      prototype::LoadWorkspaceTab(tab.view->web_contents(), tab.deferred_url,
                                  ui::PAGE_TRANSITION_AUTO_TOPLEVEL);
    } else {
      tab.view->LoadInitialURL(tab.deferred_url);
    }
  }
}

bool WorkspaceLab::SplitShown() const {
  return std::ranges::contains(splits_[active_index_].tabs,
                               selected_[active_index_]);
}

bool WorkspaceLab::SplitTab(size_t index) {
  auto& split = splits_[active_index_];
  auto& tabs = tabs_[active_index_];
  const auto selected = selected_[active_index_];
  const bool shown = SplitShown();
  const bool duplicate = shown ? std::ranges::contains(split.tabs, index)
                               : index == selected;
  if (smoke_ || !CanEditWorkspace() || index >= tabs.size() ||
      (duplicate && tabs.size() >= prototype::kMaxWorkspaceSessionTabs)) {
    return false;
  }
  RecordTimeline(true);
  if (!shown) {
    split = {};
    split.tabs.push_back(selected);
  }
  if (duplicate) {
    AddTab(active_index_, TabUrl(index, {}));
    index = tabs.size() - 1;
  }
  if (split.tabs.size() == prototype::kMaxSplitPanes) {
    *std::ranges::find(split.tabs, selected) = index;
  } else {
    split.tabs.push_back(index);
    split.weights.assign(split.tabs.size(), 1.0 / split.tabs.size());
  }
  selected_[active_index_] = index;
  shell_->CloseOverlays();
  UpdateSplitPresentation();
  tabs[index].view->RequestFocus();
  return true;
}

void WorkspaceLab::SplitPin(std::string id) {
  const auto& tabs = tabs_[active_index_];
  if (smoke_ || !CanEditWorkspace())
    return;
  const auto opened = std::ranges::find(tabs, id, &Tab::pin);
  if (opened != tabs.end()) {
    SplitTab(opened - tabs.begin());
    return;
  }
  const auto pins = WorkspacePins(active_index_);
  const auto pin = std::ranges::find(pins, id, &prototype::PinnedItem::id);
  if (pin == pins.end() || pin->folder ||
      tabs.size() >= prototype::kMaxWorkspaceSessionTabs) {
    return;
  }
  AddTab(active_index_, pin->url, base::UTF8ToUTF16(pin->title), std::move(id));
  SplitTab(tabs.size() - 1);
}

bool WorkspaceLab::RemoveSplitMember(size_t workspace, size_t index) {
  auto& split = splits_[workspace];
  const auto member = std::ranges::find(split.tabs, index);
  if (member == split.tabs.end())
    return false;
  const size_t position = member - split.tabs.begin();
  split.tabs.erase(member);
  split.weights.erase(split.weights.begin() + position);
  if (selected_[workspace] == index)
    selected_[workspace] = split.tabs[std::min(position, split.tabs.size() - 1)];
  if (split.tabs.size() < 2) {
    split = {};
  } else {
    const auto weights = sidebar::split_normalized(
        rust::Slice<const double>(split.weights.data(), split.weights.size()),
        split.weights.size());
    split.weights.assign(weights.begin(), weights.end());
  }
  return true;
}

void WorkspaceLab::LeaveSplit(size_t index) {
  if (!CanEditWorkspace() || !RemoveSplitMember(active_index_, index))
    return;
  UpdateSplitPresentation();
  tabs_[active_index_][selected_[active_index_]].view->RequestFocus();
}

void WorkspaceLab::ClearSplit(size_t index) {
  auto& split = splits_[active_index_];
  if (!CanEditWorkspace() || !std::ranges::contains(split.tabs, index))
    return;
  split = {};
  selected_[active_index_] = index;
  UpdateSplitPresentation();
  tabs_[active_index_][index].view->RequestFocus();
}

void WorkspaceLab::CloseSplitTabs(size_t index) {
  const auto& split = splits_[active_index_];
  if (!CanEditWorkspace() || !std::ranges::contains(split.tabs, index))
    return;
  std::vector<content::WebContents*> pages;
  for (size_t member : split.tabs) {
    if (auto* page = tabs_[active_index_][member].view->web_contents())
      pages.push_back(page);
  }
  if (std::ranges::any_of(pages, [](auto* page) {
        return page->NeedToFireBeforeUnload();
      })) {
    CheckBeforeUnload(pages, base::BindOnce(&WorkspaceLab::CommitCloseSplitTabs,
                                           weak_factory_.GetWeakPtr()));
  } else {
    CommitCloseSplitTabs();
  }
}

void WorkspaceLab::CommitCloseSplitTabs() {
  auto& split = splits_[active_index_];
  if (!CanEditWorkspace() || split.tabs.empty())
    return;
  RecordTimeline(true);
  auto members = std::move(split.tabs);
  split = {};
  const auto& tabs = tabs_[active_index_];
  const bool all = members.size() == tabs.size();
  if (all)
    AddTab(active_index_);
  std::ranges::sort(members);
  const size_t selected = selected_[active_index_];
  const size_t preceding = std::ranges::lower_bound(members, selected) - members.begin();
  selected_[active_index_] = std::min(selected - preceding, tabs.size() - members.size() - 1);
  for (size_t member : members)
    RecordClosedTab(active_index_, member);
  auto detached = DetachTabs(active_index_, members);
  RefreshShell();
  tabs[selected_[active_index_]].view->RequestFocus();
  if (all)
    shell_->OpenAddress(false);
  RecordTimeline(true);
  if (timeline_dirty_)
    PersistTimeline();
  PersistWorkspaceChanges();
}

std::vector<size_t> WorkspaceLab::OtherUnpinnedTabs() const {
  const auto& tabs = tabs_[active_index_];
  std::vector<size_t> indices;
  for (size_t index = 0; index < tabs.size(); ++index) {
    if (index != selected_[active_index_] && tabs[index].pin.empty())
      indices.push_back(index);
  }
  return indices;
}

bool WorkspaceLab::CloseOtherTabs() {
  if (!CanEditWorkspace() || PeekPage())
    return false;
  auto indices = OtherUnpinnedTabs();
  if (indices.empty())
    return false;

  std::vector<content::WebContents*> pages;
  for (size_t index : indices) {
    if (auto* page = tabs_[active_index_][index].view->web_contents())
      pages.push_back(page);
  }
  if (std::ranges::any_of(pages, [](auto* page) {
        return page->NeedToFireBeforeUnload();
      })) {
    CheckBeforeUnload(pages, base::BindOnce(&WorkspaceLab::CommitCloseOtherTabs,
                                           weak_factory_.GetWeakPtr(),
                                           std::move(indices)));
  } else {
    CommitCloseOtherTabs(std::move(indices));
  }
  return true;
}

void WorkspaceLab::CommitCloseOtherTabs(std::vector<size_t> indices) {
  if (!CanEditWorkspace() || OtherUnpinnedTabs() != indices)
    return;
  RecordTimeline(true);
  std::vector<std::pair<size_t, ClosedTab>> undo;
  undo.reserve(indices.size());
  for (size_t index : indices)
    undo.emplace_back(index, RecordClosedTab(active_index_, index));
  // Keep the old views alive until the split and focus are updated together.
  std::vector<Tab> detached;
  detached.reserve(indices.size());
  for (size_t index : base::Reversed(indices))
    detached.push_back(DetachTab(active_index_, index, /*show=*/false));
  ShowTab();
  RefreshShell();
  tabs_[active_index_][selected_[active_index_]].view->RequestFocus();
  RecordTimeline(true);
  if (timeline_dirty_)
    PersistTimeline();
  PersistWorkspaceChanges();
  const auto count = base::UTF8ToUTF16(std::string(sidebar::tab_count_copy(undo.size())));
  shell_->ShowToast(
      prototype::T(u"Zamknięto "), count,
      base::BindOnce(&WorkspaceLab::UndoCloseOtherTabs, weak_factory_.GetWeakPtr(),
                     model_.active()->id, model_.active()->profile_id,
                     std::move(undo)));
}

void WorkspaceLab::UndoCloseOtherTabs(
    std::string workspace_id, std::string profile_id,
    std::vector<std::pair<size_t, ClosedTab>> closed) {
  const auto workspace = WorkspaceIndex(workspace_id);
  if (!workspace || !CanEditWorkspace() || sleeping_[*workspace] ||
      model_.workspaces()[*workspace].profile_id != profile_id) {
    return;
  }
  auto& tabs = tabs_[*workspace];
  if (closed.size() > prototype::kMaxWorkspaceSessionTabs - tabs.size())
    return;
  auto& recent = closed_tabs_[*workspace];
  for (auto& [index, tab] : closed) {
    AddTab(*workspace, std::move(tab.url), std::move(tab.title));
    ReorderWorkspaceTab(*workspace, tabs.size() - 1,
                        std::min(index, tabs.size() - 1));
    std::erase_if(recent, [&tab](const ClosedTab& entry) {
      return entry.tab_id == tab.tab_id;
    });
  }
  if (*workspace == active_index_) {
    ShowTab();
    if (history_panel_)
      history_panel_->RefreshClosedTabs();
  }
  RefreshShell();
  PersistWorkspaceChanges();
}

void WorkspaceLab::MoveSplitPane(size_t index, bool later) {
  auto& split = splits_[active_index_];
  if (!CanEditWorkspace())
    return;
  const auto member = std::ranges::find(split.tabs, index);
  if (member == split.tabs.end())
    return;
  const size_t position = member - split.tabs.begin();
  if (later ? position + 1 == split.tabs.size() : position == 0)
    return;
  const size_t other = later ? position + 1 : position - 1;
  std::swap(split.tabs[position], split.tabs[other]);
  std::swap(split.weights[position], split.weights[other]);
  UpdateSplitPresentation();
}

void WorkspaceLab::UpdateSplitPresentation() {
  ShowTab();
  RefreshShell();
  PersistWorkspaceChanges();
}

void WorkspaceLab::FocusPane(size_t pane) {
  const auto& split = splits_[active_index_];
  if (SplitShown() && pane < split.tabs.size())
    SelectTab(split.tabs[pane]);
}

void WorkspaceLab::FocusVisibleView(views::WebView* view) {
  if (updating_panes_ || !SplitShown() || !view->GetVisible())
    return;
  const auto& tabs = tabs_[active_index_];
  for (size_t index : splits_[active_index_].tabs) {
    if (tabs[index].view == view && selected_[active_index_] != index) {
      SelectTab(index);
      return;
    }
  }
}

void WorkspaceLab::ResizeSplit(size_t divider, double ratio, bool done) {
  auto& split = splits_[active_index_];
  if (!CanEditWorkspace() || !SplitShown() || divider >= split.weights.size() - 1)
    return;
  const auto weights = sidebar::split_apply_ratio(
      rust::Slice<const double>(split.weights.data(), split.weights.size()),
      divider, ratio);
  split.weights.assign(weights.begin(), weights.end());
  if (done)
    PersistWorkspaceChanges();
}

void WorkspaceLab::SetSplitVertical(bool vertical) {
  if (!CanEditWorkspace() || !SplitShown())
    return;
  splits_[active_index_].vertical = vertical;
  UpdateSplitPresentation();
}

void WorkspaceLab::SelectTab(size_t index) {
  auto& tabs = tabs_[active_index_];
  if (saving_session_ || before_unload_batch_.pending() || index >= tabs.size())
    return;
  if (selected_[active_index_] != index) {
    ExitPageFullscreen();
    selected_[active_index_] = index;
    ShowTab();
  }
  RefreshShell();
  tabs[index].view->RequestFocus();
}

bool WorkspaceLab::TabMuted(size_t index) const {
  const auto& tabs = tabs_[active_index_];
  if (index >= tabs.size())
    return false;
  const auto& tab = tabs[index];
  if (tab.extension_tab)
    return WorkspaceExtensionTabMuted(*tab.extension_tab);
  auto* page = tab.view->web_contents();
  return page ? page->IsAudioMuted() : tab.native_muted;
}

void WorkspaceLab::SetTabMuted(size_t index, bool muted) {
  auto& tabs = tabs_[active_index_];
  if (smoke_ || !CanEditWorkspace() || index >= tabs.size())
    return;
  auto& tab = tabs[index];
  if (tab.extension_tab) {
    SetWorkspaceExtensionTabMuted(*tab.extension_tab, muted, {});
  } else {
    tab.native_muted = muted;
    if (auto* page = tab.view->web_contents())
      page->SetAudioMuted(muted);
  }
}

bool WorkspaceLab::ActivateTabMuteMenuForTesting(size_t index, bool muted) {
  shell_->BuildTabMenu(index);
  auto* menu = shell_->workspace_menu();
  const auto label = prototype::T(muted ? u"Wycisz kartę" : u"Włącz dźwięk karty");
  for (size_t item = 0; item < menu->GetItemCount(); ++item) {
    if (menu->GetLabelAt(item) == label && menu->IsEnabledAt(item)) {
      menu->ActivatedAt(item);
      return true;
    }
  }
  return false;
}

bool WorkspaceLab::DuplicateTab(size_t index) {
  return DuplicateWorkspaceTab(active_index_, index).has_value();
}

WorkspaceExtensionTab* WorkspaceLab::DuplicateExtensionTab(
    WorkspaceExtensionTab& target) {
  const auto position = ExtensionTabPosition(target);
  if (private_owner_ || !position)
    return nullptr;
  const auto [workspace, index] = *position;
  const auto duplicate = DuplicateWorkspaceTab(workspace, index);
  return duplicate ? tabs_[workspace][*duplicate].extension_tab.get() : nullptr;
}

std::optional<size_t> WorkspaceLab::DuplicateWorkspaceTab(size_t workspace,
                                                          size_t index) {
  auto& tabs = tabs_[workspace];
  const bool switch_workspace = workspace != active_index_;
  if (smoke_ || !CanEditWorkspace() || index >= tabs.size() ||
      tabs.size() >= prototype::kMaxWorkspaceSessionTabs ||
      (switch_workspace && PeekPage())) {
    return std::nullopt;
  }
  const auto& source = tabs[index];
  auto* page = source.view->web_contents();
  if (!page)
    page = source.discarded_page.get();
  auto* history = page ? &page->GetController() : nullptr;
  const bool copy_history =
      history && (source.discarded_page || !history->IsInitialNavigation());
  if (history) {
    const auto* entry = copy_history ? history->GetLastCommittedEntry()
                                     : history->GetPendingEntry();
    // Loading a cloned POST entry can replay the form without a reload prompt.
    if (entry && content::ZitterHistoryEntryHasPost(*entry))
      return std::nullopt;
  }
  AddTab(workspace, copy_history ? GURL() : source.deferred_url,
         copy_history ? std::u16string() : source.deferred_title);
  const size_t duplicate = index + 1;
  ReorderWorkspaceTab(workspace, tabs.size() - 1, duplicate);
  if (copy_history) {
    auto& clone = EnsureHiddenTabPage(workspace, tabs[duplicate], GURL());
    clone.GetController().CopyStateFrom(history, /*needs_reload=*/true);
  }
  if (switch_workspace) {
    selected_[workspace] = duplicate;
    SelectWorkspace(model_.workspaces()[workspace].id);
  } else {
    SelectTab(duplicate);
  }
  if (copy_history)
    tabs[duplicate].view->web_contents()->GetController().LoadIfNecessary();
  RefreshShell();
  PersistWorkspaceChanges();
  return duplicate;
}

bool WorkspaceLab::DuplicatePin(std::string id) {
  const auto& tabs = tabs_[active_index_];
  if (smoke_ || !CanEditWorkspace() ||
      tabs.size() >= prototype::kMaxWorkspaceSessionTabs) {
    return false;
  }
  const auto opened = std::ranges::find(tabs, id, &Tab::pin);
  if (opened != tabs.end())
    return DuplicateTab(opened - tabs.begin());
  const auto pins = WorkspacePins(active_index_);
  const auto pin = std::ranges::find(pins, id, &prototype::PinnedItem::id);
  if (pin == pins.end() || pin->folder)
    return false;
  AddTab(active_index_, pin->url, base::UTF8ToUTF16(pin->title));
  SelectTab(tabs.size() - 1);
  PersistWorkspaceChanges();
  return true;
}

bool WorkspaceLab::MoveSidebarTab(size_t index, std::string target) {
  const auto destination = WorkspaceIndex(target);
  if (smoke_ || private_owner_ || !CanEditWorkspace() || !destination ||
      *destination == active_index_ || index >= tabs_[active_index_].size() ||
      tabs_[*destination].size() >= prototype::kMaxWorkspaceSessionTabs) {
    return false;
  }
  auto* page = tabs_[active_index_][index].view->web_contents();
  if (WorkspaceProfile(active_index_) != WorkspaceProfile(*destination) &&
      page && page->NeedToFireBeforeUnload()) {
    CheckBeforeUnload(
        {page}, base::BindOnce(
            [](base::WeakPtr<WorkspaceLab> lab, size_t source, size_t index,
               size_t destination) {
              if (lab)
                lab->CommitTabMove(source, index, destination);
            }, weak_factory_.GetWeakPtr(), active_index_, index, *destination));
    return true;
  }
  return CommitTabMove(active_index_, index, *destination);
}

bool WorkspaceLab::CommitTabMove(size_t source_workspace, size_t index,
                                 size_t target) {
  if (!CanEditWorkspace())
    return false;
  auto& source = tabs_[source_workspace][index];
  auto* page = source.view->web_contents();
  if (page && (page->IsBeingCaptured() || media_access_->IsCapturing(page))) {
    shell_->SetOperationStatus(prototype::WorkspaceShell::OperationStatusEvent::kStopCapture);
    return false;
  }
  const bool same_profile =
      WorkspaceProfile(source_workspace) == WorkspaceProfile(target);
  auto* extension_tab = same_profile ? source.extension_tab.get() : nullptr;
  const int extension_index = WorkspaceExtensionTabIndex(extension_tab);
  if (!same_profile) {
    const auto committed = page ? WorkspaceDocumentURL(*page) : GURL();
    const auto& url = committed.is_empty() ? source.deferred_url : committed;
    if (url.is_empty() || !CanRestoreWorkspaceURL(url) || url.spec().size() > 32768)
      return false;
    const auto& title = page && !page->GetTitle().empty() ? page->GetTitle()
                                                          : source.deferred_title;
    AddTab(target, url, title.substr(0, 8192));
  }

  RecordTimeline(true);
  auto moved = DetachTab(source_workspace, index);
  if (same_profile) {
    moved.pin.clear();
    tabs_[target].push_back(std::move(moved));
  }
  sleeping_[target] = false;
  RefreshShell();
  NotifyWorkspaceExtensionTabMoved(extension_tab, extension_index);
  if (source_workspace == active_index_)
    tabs_[active_index_][selected_[active_index_]].view->RequestFocus();
  RecordTimeline(true);
  if (timeline_dirty_)
    PersistTimeline();
  PersistWorkspaceChanges();
  return true;
}

void WorkspaceLab::CloseTab(size_t index) {
  auto& tabs = tabs_[active_index_];
  if (smoke_ || !CanEditWorkspace() || index >= tabs.size())
    return;
  if (private_owner_ && tabs.size() == 1) {
    RequestClose();
    return;
  }
  auto* page = tabs[index].view->web_contents();
  if (!page || !page->NeedToFireBeforeUnload()) {
    RemoveTab(index);
    return;
  }

  SelectTab(index);
  CheckBeforeUnload(
      {page}, base::BindOnce(&WorkspaceLab::RemoveTab,
                             weak_factory_.GetWeakPtr(), index));
}

WorkspaceLab::ClosedTab WorkspaceLab::RecordClosedTab(size_t workspace,
                                                       size_t index) {
  const auto& tab = tabs_[workspace][index];
  auto* page = tab.view->web_contents();
  const auto committed = page ? WorkspaceDocumentURL(*page) : GURL();
  const auto& url = committed.is_empty() ? tab.deferred_url : committed;
  if (url.is_empty() || !CanRestoreWorkspaceURL(url) ||
      url.spec().size() > 32768) {
    return ClosedTab{.tab_id = tab.id};
  }
  const auto& title = page && !page->GetTitle().empty() ? page->GetTitle()
                                                       : tab.deferred_title;
  ClosedTab entry{url, title.substr(0, 8192), base::Time::Now(), tab.id};
  if (WorkspaceProfile(workspace)->IsOffTheRecord())
    return entry;
  auto& closed = closed_tabs_[workspace];
  if (closed.size() == kMaxClosedTabsPerWorkspace)
    closed.erase(closed.begin());
  closed.push_back(entry);
  if (history_panel_ && workspace == active_index_)
    history_panel_->RefreshClosedTabs();
  return entry;
}

std::optional<size_t> WorkspaceLab::LastClosedTab() const {
  const auto& closed = closed_tabs_[active_index_];
  if (closed.empty() ||
      tabs_[active_index_].size() >= prototype::kMaxWorkspaceSessionTabs) {
    return std::nullopt;
  }
  for (size_t newest = 0; newest < closed.size(); ++newest) {
    const auto& url = closed[closed.size() - 1 - newest].url;
    const auto& tabs = tabs_[active_index_];
    bool already_open = false;
    for (size_t index = 0; index < tabs.size(); ++index) {
      if (TabUrl(index, {}) == url) {
        already_open = true;
        break;
      }
    }
    if (!already_open)
      return newest;
  }
  return std::nullopt;
}

bool WorkspaceLab::ReopenClosedTab(size_t newest) {
  auto& closed = closed_tabs_[active_index_];
  auto& tabs = tabs_[active_index_];
  if (smoke_ || !CanEditWorkspace() ||
      tabs.size() >= prototype::kMaxWorkspaceSessionTabs ||
      newest >= closed.size()) {
    return false;
  }
  const auto found = closed.end() - 1 - newest;
  auto tab = std::move(*found);
  closed.erase(found);
  OpenRecoveredTab(std::move(tab));
  return true;
}

std::optional<WorkspaceLab::ClosedTab> WorkspaceLab::HistoricalClosedTab() const {
  const auto& tabs = tabs_[active_index_];
  if (!timeline_ || !timeline_enabled_ ||
      WorkspaceProfile(active_index_)->IsOffTheRecord() ||
      tabs.size() >= prototype::kMaxWorkspaceSessionTabs) {
    return std::nullopt;
  }
  std::vector<rust::String> open;
  open.reserve(tabs.size());
  for (size_t index = 0; index < tabs.size(); ++index)
    open.emplace_back(TabUrl(index, {}).spec());
  const auto closed = timeline_->current->timeline_last_closed(
      model_.active()->id,
      rust::Slice<const rust::String>(open.data(), open.size()));
  if (!closed.valid)
    return std::nullopt;
  GURL url(std::string(closed.tab.url));
  if (url.is_empty() || !CanRestoreWorkspaceURL(url))
    return std::nullopt;
  return ClosedTab{std::move(url), base::UTF8ToUTF16(std::string(closed.tab.title)),
                   base::Time::FromMillisecondsSinceUnixEpoch(closed.t_ms)};
}

bool WorkspaceLab::ReopenLastClosedTab() {
  if (!CanEditWorkspace())
    return false;
  if (const auto newest = LastClosedTab()) {
    if (!ReopenClosedTab(*newest))
      return false;
    if (history_panel_)
      history_panel_->RefreshClosedTabs();
    return true;
  }
  auto tab = HistoricalClosedTab();
  if (!tab)
    return false;
  OpenRecoveredTab(std::move(*tab));
  return true;
}

void WorkspaceLab::OpenRecoveredTab(ClosedTab tab) {
  AddTab(active_index_, std::move(tab.url), std::move(tab.title));
  SelectTab(tabs_[active_index_].size() - 1);
  PersistWorkspaceChanges();
}

void WorkspaceLab::RecordTimeline(bool force) {
  if (!timeline_ || !timeline_enabled_ || smoke_ || replacing_timeline_ ||
      (clearing_profile_ && WorkspaceProfile(*clearing_profile_) ==
                                WorkspaceProfile(active_index_)) ||
      WorkspaceProfile(active_index_)->IsOffTheRecord()) {
    return;
  }
  const auto& workspace = model_.active()->id;
  sidebar::TimelineFrame frame{base::Time::Now().InMillisecondsSinceUnixEpoch(),
                               workspace, model_.active()->profile_id,
                               static_cast<int>(selected_[active_index_]), {}};
  for (const auto& tab : tabs_[active_index_]) {
    auto* page = tab.view->web_contents();
    const auto committed = page ? WorkspaceDocumentURL(*page) : GURL();
    const auto& url = committed.is_empty() ? tab.deferred_url : committed;
    const auto& title = page && !page->GetTitle().empty() ? page->GetTitle()
                                                         : tab.deferred_title;
    frame.tabs.push_back({CanRestoreWorkspaceURL(url) ? url.spec() : "",
                          url.SchemeIs(content::kViewSourceScheme)
                              ? std::string() : base::UTF16ToUTF8(title.substr(0, 8192))});
  }
  const auto changed = timeline_->current->timeline_record(frame, force);
  timeline_dirty_ |= changed != sidebar::TimelineRecord::Skip;
}

void WorkspaceLab::PersistTimeline(base::OnceCallback<void(bool)> done) {
  if (!timeline_ || session_path_.empty()) {
    if (done)
      std::move(done).Run(false);
    return;
  }
  timeline_->current->timeline_prune(
      (base::Time::Now() - base::Days(timeline_days_))
          .InMillisecondsSinceUnixEpoch(),
      kMaxTimelineBytes);
  timeline_dirty_ = false;
  session_writer_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&WriteWorkspaceTimeline, WorkspaceTimelinePath(session_path_),
                     timeline_->current->timeline_frames(),
                     timeline_->current->timeline_marks(), timeline_epochs_.Clone()),
      base::BindOnce([](base::WeakPtr<WorkspaceLab> lab,
                        base::OnceCallback<void(bool)> done, bool ok) {
        if (!ok) {
          LOG(ERROR) << "Could not save the Content workspace timeline";
          if (lab)
            lab->timeline_dirty_ = true;
        }
        if (done)
          std::move(done).Run(ok);
      }, weak_factory_.GetWeakPtr(), std::move(done)));
}

bool WorkspaceLab::ClearTimeline() {
  if (!timeline_ || !CanEditWorkspace() ||
      WorkspaceProfile(active_index_)->IsOffTheRecord()) {
    return false;
  }
  shell_->HideToast();
  ReplaceTimeline(std::make_unique<prototype::TimelineStore>(), true);
  return true;
}

void WorkspaceLab::ReplaceTimeline(
    std::unique_ptr<prototype::TimelineStore> replacement, bool offer_undo) {
  auto previous = std::exchange(timeline_, std::move(replacement));
  replacing_timeline_ = true;
  PersistTimeline(base::BindOnce(&WorkspaceLab::TimelineReplacementSaved,
                                 weak_factory_.GetWeakPtr(),
                                 std::move(previous), offer_undo));
}

void WorkspaceLab::TimelineReplacementSaved(
    std::unique_ptr<prototype::TimelineStore> previous,
    bool offer_undo,
    bool ok) {
  replacing_timeline_ = false;
  if (!ok) {
    timeline_ = std::move(previous);
    shell_->ReportClearData(prototype::WorkspaceShell::ClearDataEvent::kFailed, {});
    return;
  }
  if (!offer_undo)
    return;
  for (auto& closed : closed_tabs_)
    closed.clear();
  if (history_panel_)
    history_panel_->RefreshClosedTabs();
  shell_->ShowToast(
      prototype::T(u"Wyczyszczono "), prototype::T(u"historię kart"),
      base::BindOnce(
          [](base::WeakPtr<WorkspaceLab> lab,
             std::unique_ptr<prototype::TimelineStore> previous) {
            if (lab && lab->CanEditWorkspace())
              lab->ReplaceTimeline(std::move(previous), false);
          }, weak_factory_.GetWeakPtr(), std::move(previous)));
}

void WorkspaceLab::SetTimelineSettings(bool enabled, int days) {
  timeline_settings_ready_ = true;
  timeline_enabled_ = enabled;
  timeline_days_ = std::clamp(days, 1, kMaxTimelineRetentionDays);
  if (timeline_->current->timeline_prune(
          (base::Time::Now() - base::Days(timeline_days_))
              .InMillisecondsSinceUnixEpoch(),
          kMaxTimelineBytes)) {
    PersistTimeline();
  }
}

bool WorkspaceLab::SaveTimelineSnapshot(std::string name) {
  if (!timeline_enabled_ || !CanEditWorkspace() ||
      WorkspaceProfile(active_index_)->IsOffTheRecord()) {
    return false;
  }
  RecordTimeline(true);
  const auto& workspace = model_.active()->id;
  const auto bounds = timeline_->current->timeline_bounds(workspace);
  if (!bounds.valid)
    return false;
  const sidebar::TimelineMark mark{
      base::Time::Now().InMillisecondsSinceUnixEpoch(), workspace, name};
  if (!timeline_->current->timeline_mark(mark))
    return false;
  PersistTimeline();
  shell_->ShowToast(
      prototype::T(u"Zapisano snapshot "), base::UTF8ToUTF16(name),
      base::BindOnce([](base::WeakPtr<WorkspaceLab> lab) {
        if (lab)
          lab->RunShortcut("timeline");
      }, weak_factory_.GetWeakPtr()),
      prototype::T(u"Pokaż"));
  return true;
}

bool WorkspaceLab::RestoreTimeline(int64_t time_ms, int64_t quantum_ms) {
  if (!timeline_enabled_ || !CanEditWorkspace())
    return false;
  const auto& profile = model_.active()->profile_id;
  const auto moment =
      timeline_->current->timeline_query(model_.active()->id, time_ms, quantum_ms);
  if (moment.tabs.empty() ||
      moment.tabs.size() > prototype::kMaxWorkspaceSessionTabs ||
      std::string_view(moment.profile) != profile) {
    return false;
  }

  base::ListValue tabs;
  for (const auto& tab : moment.tabs) {
    const GURL url(std::string(tab.url));
    if (url.is_empty() || !CanRestoreWorkspaceURL(url))
      return false;
    tabs.Append(prototype::WriteWorkspaceSessionTab(
        url, base::UTF8ToUTF16(std::string(tab.title)).substr(0, 8192)));
  }
  const auto time = base::Time::FromMillisecondsSinceUnixEpoch(time_ms);
  const auto clock = sidebar::timeline_clock(
      time_ms, base::Time::Now().InMillisecondsSinceUnixEpoch(),
      prototype::TimelineUtcOffsetMinutes(time), moment.tabs.size(), quantum_ms);
  return ApplyTimelineLayout(
      active_index_,
      base::DictValue().Set("profile_id", profile)
                       .Set("active", std::max(0, moment.active))
                       .Set("tabs", std::move(tabs)),
      base::UTF8ToUTF16(std::string(clock.handle)));
}

bool WorkspaceLab::ApplyTimelineLayout(size_t workspace,
                                       base::DictValue layout,
                                       std::u16string restore_time) {
  if (workspace != active_index_ || !CanEditWorkspace())
    return false;
  if (WorkspaceHasCapture(workspace)) {
    shell_->SetOperationStatus(
        prototype::WorkspaceShell::OperationStatusEvent::kStopCapture);
    return false;
  }
  std::vector<content::WebContents*> pages;
  for (const auto& tab : tabs_[workspace]) {
    if (auto* page = tab.view->web_contents())
      pages.push_back(page);
  }
  if (std::ranges::any_of(pages, [](auto* page) {
        return page->NeedToFireBeforeUnload();
      })) {
    CheckBeforeUnload(
        pages, base::BindOnce(&WorkspaceLab::CommitTimelineLayout,
                             weak_factory_.GetWeakPtr(), workspace,
                             std::move(layout), std::move(restore_time)));
  } else {
    CommitTimelineLayout(workspace, std::move(layout), std::move(restore_time));
  }
  return true;
}

void WorkspaceLab::CommitTimelineLayout(size_t workspace,
                                        base::DictValue layout,
                                        std::u16string restore_time) {
  if (workspace != active_index_ || !CanEditWorkspace())
    return;
  if (WorkspaceHasCapture(workspace)) {
    shell_->SetOperationStatus(
        prototype::WorkspaceShell::OperationStatusEvent::kStopCapture);
    return;
  }
  auto restored = prototype::ReadWorkspaceSessionTabs(layout, model_.workspaces()[workspace].profile_id);
  if (!restored)
    return;
  auto split = ReadWorkspaceSplit(layout, restored->size());
  const auto active = layout.FindInt("active");
  if (!split || !active || *active < 0 ||
      static_cast<size_t>(*active) >= restored->size()) {
    return;
  }

  RecordTimeline(true);
  auto undo = restore_time.empty() ? base::DictValue()
                                   : CaptureWorkspaceSession(workspace);
  {
    base::AutoReset<bool> updating(&updating_panes_, true);
    CancelPermissionPrompt();
    media_access_->Stop();
    devtools_.reset();
    Observe(nullptr);
    shell_->SetPanes({}, {});
    for (auto& tab : tabs_[workspace]) {
      HideTabView(*tab.view);
      if (tab.view->parent())
        tab.detached_view = shell_->content_host()->RemoveChildViewT(tab.view.get());
    }
    tabs_[workspace].clear();
    const auto pins = WorkspacePins(workspace);
    for (auto& tab : *restored) {
      const auto pin = tab.pin ? std::ranges::find(
          pins, *tab.pin, &prototype::PinnedItem::id) : pins.end();
      AddTab(workspace, std::move(tab.url),
             std::move(tab.title).value_or(std::u16string()),
             pin != pins.end() && !pin->folder ? pin->id : "");
      tabs_[workspace].back().custom_title = std::move(tab.custom_title).value_or(std::u16string());
    }
    splits_[workspace] = std::move(*split);
    selected_[workspace] = *active;
    ShowTab();
    RefreshShell();
    tabs_[workspace][selected_[workspace]].view->RequestFocus();
  }
  RecordTimeline(true);
  if (timeline_dirty_)
    PersistTimeline();
  PersistWorkspaceChanges();

  if (!undo.empty()) {
    shell_->ShowToast(
        prototype::T(u"Przywrócono karty z: "), std::move(restore_time),
        base::BindOnce(
            [](base::WeakPtr<WorkspaceLab> lab, size_t workspace,
               base::DictValue layout) {
              if (lab)
                lab->ApplyTimelineLayout(workspace, std::move(layout), {});
            }, weak_factory_.GetWeakPtr(), workspace, std::move(undo)));
  }
}

void WorkspaceLab::RemoveTab(size_t index) {
  RecordTimeline(true);
  RecordClosedTab(active_index_, index);
  const bool was_active = selected_[active_index_] == index;
  auto detached = DetachTab(active_index_, index);
  RefreshShell();
  if (was_active)
    tabs_[active_index_][selected_[active_index_]].view->RequestFocus();
  RecordTimeline(true);
  if (timeline_dirty_)
    PersistTimeline();
  PersistWorkspaceChanges();
}

WorkspaceLab::Tab WorkspaceLab::DetachTab(size_t workspace, size_t index,
                                           bool show) {
  auto& tabs = tabs_[workspace];
  if (tabs.size() == 1)
    AddTab(workspace);
  if (!RemoveSplitMember(workspace, index) && selected_[workspace] == index)
    selected_[workspace] = index + 1 < tabs.size() ? index + 1 : index - 1;
  for (auto& tab : splits_[workspace].tabs) {
    if (tab > index)
      --tab;
  }
  if (selected_[workspace] > index)
    --selected_[workspace];
  auto detached = DetachTabs(workspace, base::span_from_ref(index), show);
  return std::move(detached.front());
}

std::vector<WorkspaceLab::Tab> WorkspaceLab::DetachTabs(
    size_t workspace, base::span<const size_t> indices, bool show) {
  if (workspace == active_index_)
    ExitPageFullscreen();
  auto& tabs = tabs_[workspace];
  std::vector<Tab> detached;
  detached.reserve(indices.size());
  // The indices are sorted so erasing a higher slot leaves the others valid.
  for (size_t index : base::Reversed(indices)) {
    HideTabView(*tabs[index].view);
    detached.push_back(std::move(tabs[index]));
    tabs.erase(tabs.begin() + index);
  }
  if (show && workspace == active_index_)
    ShowTab();
  for (auto& tab : detached) {
    if (!tab.detached_view)
      tab.detached_view = shell_->content_host()->RemoveChildViewT(tab.view.get());
  }
  return detached;
}

GURL WorkspaceLab::TabUrl(std::optional<size_t> tab, std::string_view pin) const {
  const auto& tabs = tabs_[active_index_];
  if (!pin.empty()) {
    const auto pins = WorkspacePins(active_index_);
    const auto item = std::ranges::find(pins, pin, &prototype::PinnedItem::id);
    return item == pins.end() || item->folder ? GURL() : item->url;
  }
  if (!tab || *tab >= tabs.size())
    return {};
  const auto& entry = tabs[*tab];
  auto* page = entry.view->web_contents();
  return page && !page->GetVisibleURL().is_empty() ? page->GetVisibleURL()
                                                  : entry.deferred_url;
}

void WorkspaceLab::SwitchTo(size_t index) {
  if (!controller_.Activate(kWorkspaceIds[index]) || active_index_ != index) {
    Finish(false);
    return;
  }

  const GURL url(base::StrCat(
      {"data:text/html,<!doctype html><meta charset=utf-8>"
       "<meta name=color-scheme content='light dark'><script>document.title='",
       kTitles[index], "'</script><h1 id=inspect-probe style='margin:0'>",
       kTitles[index], "</h1>"}));
  tabs_[index][0].view->LoadInitialURL(url);
  RefreshShell();
}

bool WorkspaceLab::RefreshWorkspaceFavicons() {
  auto* source = web_contents();
  if (!CanEditWorkspace() || !model_.active() || !source ||
      !source->GetPrimaryMainFrame()->IsRenderFrameLive()) {
    return false;
  }
  std::map<GURL, GURL> pages;
  for (size_t index = 0; index < tabs_[active_index_].size(); ++index) {
    const auto url = TabUrl(index, {});
    if (!url.SchemeIsHTTPOrHTTPS() || url.has_username() || url.has_password())
      continue;
    auto& known_icon = pages[url];
    auto* page = tabs_[active_index_][index].view->web_contents();
    if (page && page->GetLastCommittedURL() == url && !known_icon.is_valid()) {
      if (auto* entry = page->GetController().GetLastCommittedEntry())
        known_icon = entry->GetFavicon().url;
    }
  }
  for (const auto& pin : WorkspacePins(active_index_)) {
    if (!pin.folder && pin.url.SchemeIsHTTPOrHTTPS() &&
        !pin.url.has_username() && !pin.url.has_password()) {
      pages.try_emplace(pin.url);
    }
  }
  if (pages.empty())
    return false;
  favicon_refresh_scope_ = {model_.active()->id, model_.active()->profile_id};
  favicon_refresh_ = std::make_unique<WorkspaceFaviconRefresh>(
      *source, std::move(pages),
      base::BindRepeating(&WorkspaceLab::PublishWorkspaceFavicon,
                          weak_factory_.GetWeakPtr()));
  favicon_refresh_->Start();
  return true;
}

void WorkspaceLab::PublishWorkspaceFavicon(const GURL& url, const GURL& icon,
                                            const gfx::Image& image) {
  if (!model_.active() ||
      favicon_refresh_scope_ != std::pair(model_.active()->id, model_.active()->profile_id)) {
    return;
  }
  for (auto& tab : tabs_[active_index_]) {
    auto* page = tab.view->web_contents();
    if (!page || page->GetLastCommittedURL() != url)
      continue;
    if (auto* entry = page->GetController().GetLastCommittedEntry()) {
      auto& favicon = entry->GetFavicon();
      favicon.valid = true;
      favicon.url = icon;
      favicon.image = image;
    }
  }
  if (auto* service = WorkspaceProfile(active_index_)->GetFaviconService())
    service->SetFavicons({url}, icon, favicon_base::IconType::kFavicon, image);
  auto& cached = cached_favicons_[url];
  if (cached.task != base::CancelableTaskTracker::kBadTaskId)
    favicon_tasks_.TryCancel(cached.task);
  cached.task = base::CancelableTaskTracker::kBadTaskId;
  cached.pixels = FaviconPixels(image);
  RefreshShell();
}

void WorkspaceLab::ReadCachedFavicons(prototype::ShellState& state,
                                       const std::set<GURL>& urls) {
  if (favicon_refresh_ &&
      favicon_refresh_scope_ != std::pair(state.workspace_id, state.profile_id)) {
    favicon_refresh_.reset();
  }
  if (favicon_profile_ != state.profile_id) {
    favicon_tasks_.TryCancelAll();
    cached_favicons_.clear();
    favicon_profile_ = state.profile_id;
  }
  std::erase_if(cached_favicons_, [&](const auto& entry) {
    if (urls.contains(entry.first) && !state.favicons.contains(entry.first.spec()))
      return false;
    if (entry.second.task != base::CancelableTaskTracker::kBadTaskId)
      favicon_tasks_.TryCancel(entry.second.task);
    return true;
  });
  if (urls.empty())
    return;
  auto* service = WorkspaceProfile(active_index_)->GetFaviconService();
  for (const auto& url : urls) {
    if (state.favicons.contains(url.spec()))
      continue;
    auto [cached, created] = cached_favicons_.try_emplace(url);
    if (!created) {
      if (!cached->second.pixels.empty())
        state.favicons[url.spec()] = cached->second.pixels;
      continue;
    }
    if (!service)
      continue;
    cached->second.task = service->GetFaviconImageForPageURL(
        url, base::BindOnce(
            [](base::WeakPtr<WorkspaceLab> lab, const std::string& profile,
               const GURL& url, const favicon_base::FaviconImageResult& result) {
              if (!lab || lab->favicon_profile_ != profile)
                return;
              auto cached = lab->cached_favicons_.find(url);
              if (cached == lab->cached_favicons_.end())
                return;
              cached->second.task = base::CancelableTaskTracker::kBadTaskId;
              cached->second.pixels = FaviconPixels(result.image);
              if (!cached->second.pixels.empty())
                lab->RefreshShell();
            },
            weak_factory_.GetWeakPtr(), favicon_profile_, url),
        &favicon_tasks_);
  }
}

void WorkspaceLab::RefreshExtensionActionAppearance() {
  if (private_owner_ || !IsWorkspaceExtensionRuntime() || !model_.active())
    return;
  auto& profile = *WorkspaceProfile(active_index_);
  const auto& tab = tabs_[active_index_][selected_[active_index_]];
  const int tab_id = WorkspaceExtensionTabId(tab.extension_tab.get());
  const auto configured = GetConfiguredExtensions(profile);
  if (pinned_extension_toolbar_) {
    pinned_extension_toolbar_->RemoveAllChildViews();
    if (auto* prefs = extensions::ExtensionPrefs::Get(&profile)) {
      for (const auto& id : prefs->GetPinnedExtensions()) {
        const auto extension = std::ranges::find_if(configured, [&](const auto& candidate) {
          return candidate->id() == id;
        });
        if (extension == configured.end() || !CanLoadExtensionProbeResources(profile, id))
          continue;
        auto* action = extensions::ExtensionActionManager::Get(&profile)->GetExtensionAction(**extension);
        if (!action)
          continue;
        auto button = std::make_unique<ContentToolbarButton>(
            kExtensionsPuzzle, base::UTF8ToUTF16((*extension)->name()),
            views::Button::PressedCallback(),
            base::BindRepeating([](WorkspaceLab* lab) {
              return sidebar::design_palette(
                  lab->widget_->GetColorMode() == ui::ColorProviderKey::ColorMode::kDark,
                  lab->shell_->active_workspace_color());
            }, base::Unretained(this)));
        button->SetCallback(base::BindRepeating(
            [](base::WeakPtr<WorkspaceLab> lab, std::string id, views::View* button) {
              if (lab)
                lab->OpenExtensionPopupAt(id, button->GetBoundsInScreen());
            }, weak_factory_.GetWeakPtr(), id, base::Unretained(button.get())));
        button->SetExtensionAction(**extension, *action, tab_id);
        pinned_extension_toolbar_->AddChildView(std::move(button));
      }
    }
    const bool visible = !pinned_extension_toolbar_->children().empty();
    pinned_extension_toolbar_->SetVisible(visible);
    extension_toolbar_divider_->SetVisible(visible);
  }
  for (const auto& extension : configured) {
    auto* action = CanLoadExtensionProbeResources(profile, extension->id())
        ? extensions::ExtensionActionManager::Get(&profile)->GetExtensionAction(*extension)
        : nullptr;
    if (!action) {
      shell_->SetExtensionActionAppearance(extension->id(), {}, {}, SK_ColorTRANSPARENT);
      continue;
    }
    auto badge = base::UTF8ToUTF16(action->GetExplicitlySetBadgeText(tab_id));
    std::ranges::replace_if(badge, [](char16_t c) { return c < 0x20; }, u' ');
    base::i18n::SanitizeUserSuppliedString(&badge);
    shell_->SetExtensionActionAppearance(extension->id(), action->GetExplicitlySetIcon(tab_id),
                                         std::move(badge), action->GetBadgeBackgroundColor(tab_id));
  }
}

void WorkspaceLab::RefreshShell() {
  if (!web_contents() || !model_.active())
    return;
  ScheduleGitHubRefresh();
  RefreshNightShiftButton();
  if (assistant_ && assistant_profile_ != AssistantProfileId()) {
    if (assistant_->SwitchProfile()) {
      assistant_profile_ = AssistantProfileId();
    } else {
      assistant_->SetVisible(false);
      shell_->LayoutAssistant(false);
    }
  }
  if (!private_owner_ && IsWorkspaceExtensionRuntime()) {
    std::map<const ProfileContext*, int> next_index;
    for (size_t workspace = 0; workspace < tabs_.size(); ++workspace) {
      int& index = next_index[WorkspaceProfile(workspace)];
      for (size_t tab = 0; tab < tabs_[workspace].size(); ++tab, ++index) {
        const auto& entry = tabs_[workspace][tab];
        const bool active = workspace == active_index_ && tab == selected_[workspace];
        const bool highlighted = active ||
            (workspace == active_index_ && SplitShown() &&
             std::ranges::contains(splits_[workspace].tabs, tab));
        UpdateWorkspaceExtensionTab(entry.extension_tab.get(), index, active,
                                    highlighted, !entry.pin.empty(),
                                    entry.deferred_url, entry.deferred_title);
      }
    }
  }
  prototype::ShellState state;
  state.workspace_id = model_.active()->id;
  state.profile_id = model_.active()->profile_id;
  state.workspace_name = private_owner_ ? prototype::T(u"Tryb prywatny")
                                       : base::UTF8ToUTF16(model_.active()->name);
  state.profile_name = private_owner_ ? state.workspace_name
      : base::UTF8ToUTF16(WorkspaceProfileName(active_index_));
  if (!private_owner_) {
    state.workspaces = model_.workspaces();
    for (const auto [host, target] : link_rules_)
      state.link_rules.push_back({std::string(host), target.GetString()});
  }
  for (size_t workspace = 0; workspace < tabs_.size(); ++workspace) {
    if (sleeping_[workspace])
      state.sleeping_workspaces.insert(model_.workspaces()[workspace].id);
  }
  for (const auto& profile : profiles_) {
    state.profiles.push_back({profile.id,
                              private_owner_ ? base::UTF16ToUTF8(state.profile_name)
                                             : profile.name,
                              "", ""});
  }
  state.configuration_ready = true;
  state.onboarding_complete = !startup_import_pending_;
  state.profiles_busy = saving_catalogue_;
  state.url = web_contents()->GetVisibleURL();
  state.can_go_back = web_contents()->GetController().CanGoBack();
  state.can_go_forward = web_contents()->GetController().CanGoForward();
  state.loading = web_contents()->IsLoading();
  state.active_tab = selected_[active_index_];
  state.pins = WorkspacePins(active_index_);
  state.active_pin = tabs_[active_index_][state.active_tab].pin;
  const auto& split = splits_[active_index_];
  state.split = !split.tabs.empty();
  state.split_visible = SplitShown();
  state.split_vertical = split.vertical;
  state.split_tabs = split.tabs;
  if (state.split_visible) {
    state.active_pane = std::ranges::find(split.tabs, state.active_tab) - split.tabs.begin();
  }
  std::set<GURL> cached_icon_urls;
  for (const auto& pin : state.pins) {
    if (!pin.folder && pin.url.SchemeIsHTTPOrHTTPS())
      cached_icon_urls.insert(pin.url);
  }
  const auto& tabs = tabs_[active_index_];
  for (size_t index = 0; index < tabs.size(); ++index) {
    auto* page = tabs[index].view->web_contents();
    const GURL url = TabUrl(index, {});
    auto title = prototype::IsRssTabUrl(url) ? prototype::T(u"Czytnik RSS")
        : page && !page->GetTitle().empty() ? page->GetTitle()
                                          : tabs[index].deferred_title;
    if (smoke_)
      title = base::UTF8ToUTF16(kTitles[active_index_]);
    auto* driver = page ? favicon::ContentFaviconDriver::FromWebContents(page)
                        : nullptr;
    if (driver && driver->FaviconIsValid() && url.SchemeIsHTTPOrHTTPS() &&
        page->GetLastCommittedURL() == url) {
      auto pixels = FaviconPixels(driver->GetFavicon());
      if (!pixels.empty())
        state.favicons[url.spec()] = std::move(pixels);
    } else if (!page && url.SchemeIsHTTPOrHTTPS()) {
      cached_icon_urls.insert(url);
    }
    if (!tabs[index].custom_title.empty())
      title = tabs[index].custom_title;
    state.tabs.push_back({title.empty() ? u"New tab" : std::move(title),
                          url, index, tabs[index].pin, {tabs[index].id, 1},
                          page && page->IsCurrentlyAudible() && !page->IsAudioMuted(),
                          0, !tabs[index].custom_title.empty()});
  }
  ReadCachedFavicons(state, cached_icon_urls);
  const GURL developer_url = state.url;
  shell_->Update(std::move(state));
  shell_->SetDeveloperPage(developer_url);
  RefreshExtensionShortcuts();
  RefreshExtensionActionAppearance();
  UpdateAdblockShield();
  UpdateFingerprintToolbar();
}

void WorkspaceLab::DidStartNavigation(content::NavigationHandle* navigation) {
  if (navigation->IsInPrimaryMainFrame())
    ClearAssistantAdblockPreview();
  if (navigation->IsInPrimaryMainFrame() && !navigation->IsSameDocument()) {
    CloseNightShiftSettings();
#if BUILDFLAG(ENABLE_PRINTING)
    shell_->HidePrint();
    pdf_export_.reset();
#endif
    shell_->ResetFind();
    find_page_.reset();
    CloseAdblockShield();
    CancelPermissionPrompt();
    media_access_->Stop();
  }
}

void WorkspaceLab::DidFinishNavigation(content::NavigationHandle* navigation) {
  if (!navigation->IsInPrimaryMainFrame())
    return;
  if (webauthn_probe_url_.is_valid() &&
      navigation->GetRedirectChain().front() == webauthn_probe_url_) {
    if (!navigation->HasCommitted() || navigation->IsErrorPage() ||
        navigation->GetURL() != webauthn_probe_url_) {
      Finish(false);
      return;
    }
    auto* browser = content::GetContentClientForTesting()->browser();
    if (!browser->IsSecurityLevelAcceptableForWebAuthn(
            navigation->GetRenderFrameHost(),
            url::Origin::Create(webauthn_probe_url_))) {
      Finish(false);
      return;
    }
    webauthn_probe_ = std::make_unique<WorkspaceWebAuthnProbe>(
        *web_contents(),
        base::BindOnce([](base::WeakPtr<WorkspaceLab> lab, bool passed) {
          if (!lab)
            return;
          lab->webauthn_probe_.reset();
          if (passed)
            std::puts("Zitter Content virtual WebAuthn probe passed");
          lab->Finish(passed);
        }, weak_factory_.GetWeakPtr()));
    return;
  }
  if (tls_probe_url_.is_valid() &&
      navigation->GetRedirectChain().front() == tls_probe_url_) {
    auto* browser = content::GetContentClientForTesting()->browser();
    const bool webauthn_allowed =
        navigation->HasCommitted() &&
        browser->IsSecurityLevelAcceptableForWebAuthn(
            navigation->GetRenderFrameHost(),
            url::Origin::Create(navigation->GetURL()));
    if (tls_probe_expect_trusted_) {
      const auto& ssl = navigation->GetSSLInfo();
      const bool trusted = navigation->GetURL() == tls_probe_url_ &&
                           navigation->HasCommitted() &&
                           !navigation->IsErrorPage() &&
                           navigation->GetNetErrorCode() == net::OK &&
                           ssl && ssl->cert &&
                           !net::IsCertStatusError(ssl->cert_status) &&
                           webauthn_allowed;
      if (trusted)
        std::puts("Zitter Content committed a trusted HTTPS page");
      Finish(trusted);
      return;
    }
    const bool blocked = navigation->GetURL() == tls_probe_url_ &&
                         navigation->HasCommitted() &&
                         navigation->IsErrorPage() &&
                         navigation->GetNetErrorCode() ==
                             net::ERR_CERT_AUTHORITY_INVALID &&
                         !webauthn_allowed;
    if (!blocked)
      Finish(false);
    return;
  }
  if (smoke_)
    return;
  if (navigation->HasCommitted() && !navigation->IsErrorPage()) {
    auto& tab = tabs_[active_index_][selected_[active_index_]];
    tab.deferred_url = GURL();
    tab.deferred_title.clear();
    RecordTimeline(false);
  }
  RefreshShell();
}

void WorkspaceLab::DidStartLoading() {
  if (!smoke_)
    RefreshShell();
}

void WorkspaceLab::DidStopLoading() {
  if (!smoke_)
    RefreshShell();
}

void WorkspaceLab::TitleWasSet(content::NavigationEntry*) {
  if (!smoke_)
    RefreshShell();
  if (!site_permission_probe_phase_.empty()) {
    CheckSitePermissionProbe();
    return;
  }
  if (fingerprint_probe_step_) {
    CheckFingerprintProbePage();
    return;
  }
  if (!password_probe_action_.empty()) {
    AdvancePasswordProbe();
    return;
  }
  if (session_probe_url_.is_valid()) {
    CheckSessionProbePage();
  }
}

void WorkspaceLab::LoadFingerprintProbeStep() {
  enum class Action { kNone, kEnable, kRotate, kDisable };
  struct Step {
    size_t profile;
    bool expected;
    Action action = Action::kNone;
    bool second_site = false;
  };
  constexpr Step kChange[] = {
      {0, true}, {1, false}, {1, false, Action::kEnable},
      {1, true, Action::kNone, true}, {1, true, Action::kRotate},
      {0, true, Action::kDisable}};
  constexpr Step kReopen[] = {{0, false}, {1, true}};
  const size_t index = *fingerprint_probe_step_;
  const auto steps = fingerprint_probe_reopen_
                         ? base::span<const Step>(kReopen)
                         : base::span<const Step>(kChange);
  if (index == steps.size()) {
    RequestClose();
    return;
  }
  const auto& step = steps[index];
  const bool switching_profile = active_index_ != step.profile;
  SelectWorkspace(std::string(kWorkspaceIds[step.profile]));
  if (active_index_ != step.profile ||
      (switching_profile && fingerprint_toolbar_->widget() &&
       fingerprint_toolbar_->widget()->IsVisible())) {
    LOG(ERROR) << "Fingerprint toolbar did not follow the active profile at step "
               << index << " (active workspace " << active_index_ << ")";
    Finish(false);
    return;
  }
  shell_->OpenPrivacySettings();
  auto* control = shell_->preference_switch_for_testing("fingerprint_protection");
  if (control) {
    control->GetWidget()->LayoutRootViewIfNecessary();
    control->ScrollViewToVisible();
  }
  if (!control || !control->IsDrawn() || !control->GetEnabled() ||
      shell_->preference_switch_checked_for_testing("fingerprint_protection") !=
          step.expected) {
    LOG(ERROR) << "Fingerprint switch did not reflect the active profile at "
               << index;
    Finish(false);
    return;
  }
  ui::AXActionData action;
  action.action = ax::mojom::Action::kDoDefault;
  if (step.action == Action::kEnable) {
    if (!control->HandleAccessibleAction(action) ||
        shell_->preference_switch_checked_for_testing("fingerprint_protection") ==
            step.expected) {
      LOG(ERROR) << "Could not change fingerprint protection at " << index;
      Finish(false);
      return;
    }
  }
  shell_->CloseOverlays(true);

  if (step.action == Action::kRotate || step.action == Action::kDisable) {
    widget_->LayoutRootViewIfNecessary();
    if (!fingerprint_button_->IsDrawn() ||
        !fingerprint_button_->HandleAccessibleAction(action) ||
        !fingerprint_toolbar_->widget() ||
        !fingerprint_toolbar_->widget()->IsVisible()) {
      LOG(ERROR) << "Could not open the fingerprint toolbar popover";
      Finish(false);
      return;
    }
    const auto code = fingerprint_toolbar_->site_code_for_testing();
    auto& profile = *WorkspaceProfile(active_index_);
    const auto expected_code =
        prototype::FingerprintSession::For(profile).SiteCode(
            web_contents()->GetLastCommittedURL());
    const bool rotate = step.action == Action::kRotate;
    auto* command = rotate
                        ? fingerprint_toolbar_->rotate_button_for_testing()
                        : fingerprint_toolbar_->protection_button_for_testing();
    if (code.empty() || code != expected_code || !command ||
        !command->GetEnabled() || !command->HandleAccessibleAction(action) ||
        (rotate && fingerprint_toolbar_->site_code_for_testing() == code) ||
        (!rotate && (fingerprint_button_->GetVisible() ||
                     profile.GetFingerprintProtection() != false))) {
      LOG(ERROR) << "Fingerprint toolbar action did not update the active profile";
      Finish(false);
      return;
    }
    fingerprint_toolbar_->Close();
  }

  if ((step.action == Action::kRotate || step.action == Action::kDisable) &&
      base::CommandLine::ForCurrentProcess()->HasSwitch(
          kFingerprintCrossProcessProbe)) {
    fingerprint_probe_url_ = web_contents()->GetLastCommittedURL();
    web_contents()->GetPrimaryMainFrame()->ExecuteJavaScriptForTests(
        u"resampleFingerprint()", base::NullCallback(),
        content::ISOLATED_WORLD_ID_GLOBAL);
    return;
  }

  fingerprint_probe_url_ = session_probe_url_.Resolve(base::StrCat(
      {"/fingerprint?", fingerprint_probe_reopen_ ? "reopen=" : "change=",
       std::to_string(index)}));
  if (step.second_site) {
    GURL::Replacements replace;
    replace.SetHostStr("localhost");
    fingerprint_probe_url_ = fingerprint_probe_url_.ReplaceComponents(replace);
  }
  if (!Navigate(fingerprint_probe_url_, false))
    Finish(false);
}

void WorkspaceLab::CheckFingerprintProbePage() {
  if (fingerprint_popup_pending_ || fingerprint_probe_url_.is_empty() ||
      web_contents()->GetLastCommittedURL() != fingerprint_probe_url_) {
    return;
  }
  const auto& title = web_contents()->GetTitle();
  if (title.starts_with(u"Zitter fingerprint frame mismatch")) {
    LOG(ERROR) << base::UTF16ToUTF8(title);
    Finish(false);
    return;
  }
  const bool cross_process_probe =
      base::CommandLine::ForCurrentProcess()->HasSwitch(kFingerprintCrossProcessProbe);
  const bool live_sample = cross_process_probe && !fingerprint_probe_reopen_ &&
                           *fingerprint_probe_step_ >= 4;
  const std::u16string_view prefix =
      live_sample ? u"Zitter fingerprint live result " : kFingerprintResult;
  if (!title.starts_with(prefix) || title.size() > prefix.size() + 256) {
    return;
  }
  if (cross_process_probe) {
    auto* main = web_contents()->GetPrimaryMainFrame();
    bool cross_process = false;
    main->ForEachRenderFrameHost([&](content::RenderFrameHost* frame) {
      if (frame->GetLastCommittedURL().path() == "/fingerprint-frame" &&
          frame->GetProcess() != main->GetProcess()) {
        cross_process = true;
      }
    });
    if (!cross_process) {
      LOG(ERROR) << "The fingerprint fixture did not run in a separate renderer";
      Finish(false);
      return;
    }
  }
  auto result = base::UTF16ToUTF8(
      std::u16string_view(title).substr(prefix.size()));
  if (cross_process_probe && !fingerprint_probe_reopen_ &&
      *fingerprint_probe_step_ == 0) {
    fingerprint_popup_pending_ = true;
    auto* opener = web_contents();
    opener->GetPrimaryMainFrame()->ExecuteJavaScriptWithUserGestureForTests(
        u"sampleFingerprintPopup()", base::NullCallback(),
        content::ISOLATED_WORLD_ID_GLOBAL);
    CheckFingerprintPopup(opener->GetWeakPtr(), std::move(result),
                          base::TimeTicks::Now() + base::Seconds(8));
    return;
  }
  AdvanceFingerprintProbeSample(std::move(result));
}

void WorkspaceLab::CheckFingerprintPopup(
    base::WeakPtr<content::WebContents> opener, std::string sample,
    base::TimeTicks deadline) {
  const auto position = TabForPage(opener.get());
  if (!position || position->first != active_index_) {
    Finish(false);
    return;
  }
  const auto& title = opener->GetTitle();
  if (title.starts_with(u"Zitter fingerprint popup mismatch") ||
      base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "Initial fingerprint popup probe failed: "
               << base::UTF16ToUTF8(title);
    Finish(false);
    return;
  }
  const auto& tabs = tabs_[active_index_];
  const auto popup = std::ranges::find(tabs, std::optional(tabs[position->second].id),
                                       &Tab::opener_id);
  if (title == u"Zitter fingerprint popup passed" && popup != tabs.end() &&
      popup->view->web_contents() == web_contents()) {
    auto* page = popup->view->web_contents();
    auto* frame = page->GetPrimaryMainFrame();
    auto* opener_frame = opener->GetPrimaryMainFrame();
    if (page == opener.get() || page->GetBrowserContext() != opener->GetBrowserContext() ||
        frame->GetProcess() != opener_frame->GetProcess() ||
        (!frame->GetLastCommittedURL().is_empty() &&
         frame->GetLastCommittedURL() != GURL("about:blank"))) {
      LOG(ERROR) << "The fingerprint popup did not retain its initial document";
      Finish(false);
      return;
    }
    CloseTab(popup - tabs.begin());
    if (web_contents() != opener.get()) {
      LOG(ERROR) << "The fingerprint popup did not return focus to its opener";
      Finish(false);
      return;
    }
    fingerprint_popup_pending_ = false;
    std::puts("Zitter Content verified fingerprint protection in an initial popup");
    AdvanceFingerprintProbeSample(std::move(sample));
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckFingerprintPopup,
                                weak_factory_.GetWeakPtr(), std::move(opener),
                                std::move(sample), deadline),
      base::Milliseconds(25));
}

void WorkspaceLab::AdvanceFingerprintProbeSample(std::string sample) {
  std::printf("Zitter fingerprint step %zu %s\n", *fingerprint_probe_step_,
              sample.c_str());
  if (!fingerprint_probe_reopen_ && *fingerprint_probe_step_ == 0 &&
      !RunShortcut("fingerprint")) {
    LOG(ERROR) << "Could not open the Work fingerprint popover before switching";
    Finish(false);
    return;
  }
  fingerprint_probe_url_ = GURL();
  ++*fingerprint_probe_step_;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::LoadFingerprintProbeStep,
                                weak_factory_.GetWeakPtr()));
}

void WorkspaceLab::CheckHttpAuthProbe(std::string action, int phase,
                                      base::TimeTicks deadline) {
  auto* page = web_contents();
  if (!page || base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "HTTP authentication probe timed out in phase " << phase;
    Finish(false);
    return;
  }
  if ((phase == 0 || phase == 2) && HttpAuthWidgetForTesting(page)) {
    if (action == "navigate") {
      Navigate(session_probe_url_.Resolve("/http-auth/replacement"), false);
    } else if (action == "hide") {
      auto old_page = page->GetWeakPtr();
      SelectWorkspace(std::string(kWorkspaceIds[1]));
      if (!old_page || HttpAuthWidgetForTesting(old_page.get())) {
        Finish(false);
        return;
      }
      std::puts("Zitter Content HTTP authentication probe passed");
      Finish(true);
      return;
    } else if (!AnswerHttpAuthForTesting(page, action == "allow" && phase == 0)) {
      Finish(false);
      return;
    }
    ++phase;
  } else if ((phase == 1 || phase == 3) && !HttpAuthWidgetForTesting(page)) {
    const std::u16string expected = action == "navigate" ? u"HTTP replacement"
        : action == "allow" && phase == 1 ? u"HTTP authenticated" : u"HTTP cancelled";
    if (page->GetTitle() == expected) {
      if (action == "allow" && phase == 1) {
        SelectWorkspace(std::string(kWorkspaceIds[1]));
        Navigate(session_probe_url_.Resolve("/http-auth/allow"), false);
        phase = 2;
      } else {
        std::puts("Zitter Content HTTP authentication probe passed");
        Finish(true);
        return;
      }
    }
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckHttpAuthProbe,
          weak_factory_.GetWeakPtr(), action, phase, deadline), base::Milliseconds(50));
}

void WorkspaceLab::StartPasswordProbe(std::string action) {
  constexpr std::string_view kActions[] = {
      "save", "update", "cancel", "navigate", "hide", "check", "check-personal",
      "disable", "off", "personal", "enable", "unavailable", "manager"};
  if (!session_probe_url_.is_valid() || !std::ranges::contains(kActions, action)) {
    Finish(false);
    return;
  }
  password_probe_action_ = std::move(action);
  const size_t profile = (password_probe_action_ == "personal" ||
                          password_probe_action_ == "check-personal") ? 1 : 0;
  SelectWorkspace(std::string(kWorkspaceIds[profile]));
  if (password_probe_action_ == "manager") {
    if (!RunShortcut("passwords")) {
      FinishPasswordProbe(false);
      return;
    }
    CheckPasswordManager(PasswordManagerStep::kInitial,
                         base::TimeTicks::Now() + base::Seconds(6));
    return;
  }
  if (password_probe_action_ == "enable") {
    auto* prefs = contexts_[0]->GetPasswordPrefs();
    if (prefs->GetInitializationStatus() ==
        PrefService::INITIALIZATION_STATUS_WAITING) {
      prefs->AddPrefInitObserver(base::BindOnce(
          [](base::WeakPtr<WorkspaceLab> lab, bool) {
            if (lab)
              lab->EnablePasswordSavingProbe();
          },
          weak_factory_.GetWeakPtr()));
    } else {
      EnablePasswordSavingProbe();
    }
    return;
  }
  if (!web_contents() || active_index_ != profile ||
      !Navigate(PasswordProbePage(), false)) {
    LOG(ERROR) << "Could not open the workspace password fixture";
    FinishPasswordProbe(false);
  }
}

void WorkspaceLab::CheckPasswordManager(PasswordManagerStep step,
                                        base::TimeTicks deadline) {
  using Page = prototype::PasswordsPanel::Page;
  auto* panel = passwords_panel_.get();
  if (!panel || !panel->widget() || !panel->widget()->IsVisible()) {
    FinishPasswordProbe(false);
    return;
  }
  const std::vector<std::u16string> work_sites = {u"127.0.0.1"};
  const std::vector<std::u16string> added_sites = {u"127.0.0.1", u"panel.test"};
  const auto sites = panel->listed_for_testing();
  auto advance = [&](PasswordManagerStep next) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&WorkspaceLab::CheckPasswordManager,
                       weak_factory_.GetWeakPtr(), next,
                       base::TimeTicks::Now() + base::Seconds(6)));
  };
  if (panel->loaded_for_testing()) {
    switch (step) {
      case PasswordManagerStep::kInitial:
        if (sites == work_sites &&
            (!metal_ || panel->scene_contains_for_testing("127.0.0.1"))) {
          if (panel->widget()->color_mode_override() != widget_->color_mode_override()) {
            FinishPasswordProbe(false);
            return;
          }
          panel->OpenDetail(0);
          panel->RevealPassword(false);
          if (panel->revealed_for_testing() || !password_manager_auth_reply_) {
            FinishPasswordProbe(false);
            return;
          }
          panel->SetPage(Page::kList);
          panel->OpenDetail(0);
          std::move(password_manager_auth_reply_).Run(true);
          if (panel->revealed_for_testing()) {
            FinishPasswordProbe(false);
            return;
          }
          panel->RevealPassword(false);
          if (!password_manager_auth_reply_) {
            FinishPasswordProbe(false);
            return;
          }
          std::move(password_manager_auth_reply_).Run(false);
          if (panel->revealed_for_testing()) {
            FinishPasswordProbe(false);
            return;
          }
          panel->SetPage(Page::kList);
          password_manager_weak_count_ = panel->issues_for_testing(2);
          if (!panel->AddPassword(u"https://panel.test", u"panel-user", u"123456")) {
            FinishPasswordProbe(false);
            return;
          }
          advance(PasswordManagerStep::kAdded);
          return;
        }
        break;
      case PasswordManagerStep::kAdded:
        if (sites == added_sites &&
            panel->issues_for_testing(2) == password_manager_weak_count_ + 1) {
          panel->OpenDetail(1);
          if (!panel->ChangePassword(u"Kwiat-Tramwaj-17-Lato!")) {
            FinishPasswordProbe(false);
            return;
          }
          advance(PasswordManagerStep::kEdited);
          return;
        }
        break;
      case PasswordManagerStep::kEdited:
        if (sites == added_sites &&
            panel->issues_for_testing(2) == password_manager_weak_count_ &&
            panel->page() == Page::kDetail && !panel->revealed_for_testing()) {
          panel->DeleteDetail();
          advance(PasswordManagerStep::kConfirm);
          return;
        }
        break;
      case PasswordManagerStep::kConfirm:
        if (auto* dialog = shell_->danger_dialog_for_testing();
            dialog && dialog->IsVisible()) {
          dialog->widget_delegate()->AsDialogDelegate()->AcceptDialog();
          advance(PasswordManagerStep::kDeleted);
          return;
        }
        break;
      case PasswordManagerStep::kDeleted:
        if (sites == work_sites && panel->page() == Page::kList) {
          const auto old_panel = panel->widget()->GetWeakPtr();
          SelectWorkspace(std::string(kWorkspaceIds[1]));
          if (active_index_ != 1 || old_panel || passwords_panel_ ||
              !RunShortcut("passwords")) {
            FinishPasswordProbe(false);
            return;
          }
          advance(PasswordManagerStep::kPersonal);
          return;
        }
        break;
      case PasswordManagerStep::kPersonal:
        FinishPasswordProbe(sites.empty());
        return;
    }
  }
  if (base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "Password manager probe timed out at step " << static_cast<int>(step);
    FinishPasswordProbe(false);
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&WorkspaceLab::CheckPasswordManager,
                     weak_factory_.GetWeakPtr(), step, deadline),
      base::Milliseconds(75));
}

void WorkspaceLab::AdvancePasswordProbe() {
  auto* page = web_contents();
  const auto url = page->GetLastCommittedURL();
  const auto& title = page->GetTitle();
  const bool checking = password_probe_action_.starts_with("check");
  if (checking && password_probe_submitted_ && url == PasswordProbePage() &&
      (title == u"Zitter password form probe passed" ||
       title == u"Zitter password form probe failed")) {
    FinishPasswordProbe(title == u"Zitter password form probe passed");
    return;
  }
  const std::u16string_view ready = checking ? u"Zitter password form probe ready"
                                             : u"Zitter password form ready";
  if (!password_probe_submitted_ && title == ready && url == PasswordProbePage()) {
    password_probe_submitted_ = true;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&WorkspaceLab::SubmitPasswordProbe, weak_factory_.GetWeakPtr()),
        base::Milliseconds(400));
  } else if (password_probe_submitted_ && !password_probe_waiting_ &&
             title == u"Zitter password form submitted" &&
             url == session_probe_url_.Resolve("/password-save/done")) {
    password_probe_waiting_ = true;
    const bool blocked = password_probe_action_ == "off" ||
                         password_probe_action_ == "unavailable";
    CheckPasswordPrompt(base::TimeTicks::Now() +
                        (blocked ? base::Milliseconds(1500) : base::Seconds(6)));
  } else if (password_probe_waiting_ && password_probe_action_ == "navigate" &&
             title == u"Zitter password replacement page" &&
             url == session_probe_url_.Resolve("/password-save/replacement")) {
    FinishPasswordProbe(!GetPagePasswordPromptForTesting(*page));
  }
}

void WorkspaceLab::SubmitPasswordProbe() {
  auto* page = web_contents();
  if (!page || page->GetLastCommittedURL() != PasswordProbePage()) {
    LOG(ERROR) << "Workspace password fixture changed before submission";
    FinishPasswordProbe(false);
    return;
  }
  ClickProbePage(*page);
  if (!password_probe_action_.starts_with("check")) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&WorkspaceLab::TypePasswordProbeSuffix,
                       weak_factory_.GetWeakPtr()),
        base::Milliseconds(150));
  }
}

void WorkspaceLab::TypePasswordProbeSuffix() {
  auto* page = web_contents();
  if (!page || page->GetLastCommittedURL() != PasswordProbePage())
    return;
  auto* widget = page->GetPrimaryMainFrame()->GetRenderWidgetHost();
  for (auto type : {blink::WebInputEvent::Type::kRawKeyDown,
                    blink::WebInputEvent::Type::kChar,
                    blink::WebInputEvent::Type::kKeyUp}) {
    input::NativeWebKeyboardEvent event(type, 0, base::TimeTicks::Now());
    event.windows_key_code = ui::VKEY_T;
    event.dom_code = static_cast<int>(ui::DomCode::US_T);
    event.dom_key = ui::DomKey::FromCharacter(u't');
    event.text[0] = u't';
    event.unmodified_text[0] = u't';
    widget->ForwardKeyboardEvent(event);
  }
}

void WorkspaceLab::CheckPasswordPrompt(base::TimeTicks deadline) {
  auto* page = web_contents();
  auto* prompt = page ? GetPagePasswordPromptForTesting(*page) : nullptr;
  const bool unavailable = password_probe_action_ == "unavailable";
  if (password_probe_action_ == "off" || unavailable) {
    if (prompt || base::TimeTicks::Now() >= deadline) {
      FinishPasswordProbe(!prompt && CheckPasswordSavingToggle(
          unavailable ? std::nullopt : std::optional(false)));
      return;
    }
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&WorkspaceLab::CheckPasswordPrompt,
                       weak_factory_.GetWeakPtr(), deadline),
        base::Milliseconds(75));
    return;
  }
  const auto login_label = prototype::TF(u"Użytkownik: {}", {u"zitter-profile-probe"});
  const bool rendered_in_metal =
      page && PagePasswordPromptMetalContainsForTesting(
                  *page, base::UTF16ToUTF8(login_label));
  if (!prompt || !prompt->IsVisible() || (metal_ && !rendered_in_metal)) {
    if (base::TimeTicks::Now() >= deadline) {
      LOG(ERROR) << "Expected a rendered password prompt after form submission"
                 << ", visible=" << (prompt && prompt->IsVisible())
                 << ", metal_text=" << rendered_in_metal;
      FinishPasswordProbe(false);
      return;
    }
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&WorkspaceLab::CheckPasswordPrompt,
                       weak_factory_.GetWeakPtr(), deadline),
        base::Milliseconds(75));
    return;
  }
  const bool update = password_probe_action_ != "save" &&
                      password_probe_action_ != "personal";
  const auto copy = sidebar::password_prompt_copy(update, "", "", "");
  auto* delegate = prompt->widget_delegate()->AsDialogDelegate();
  if (!delegate || delegate->GetModalType() != ui::mojom::ModalType::kNone ||
      prompt->IsActive() || rendered_in_metal != metal_ ||
      delegate->GetWindowTitle() != base::UTF8ToUTF16(std::string(copy.title)) ||
      prompt->color_mode_override() != widget_->color_mode_override()) {
    LOG(ERROR) << "Unexpected password prompt title or theme: "
               << (delegate ? base::UTF16ToUTF8(delegate->GetWindowTitle()) : "(no dialog)");
    FinishPasswordProbe(false);
    return;
  }

  if (password_probe_action_ == "navigate") {
    if (!Navigate(session_probe_url_.Resolve("/password-save/replacement"), false))
      FinishPasswordProbe(false);
    return;
  }
  if (password_probe_action_ == "hide") {
    SelectWorkspace(std::string(kWorkspaceIds[1]));
    FinishPasswordProbe(active_index_ == 1 && !GetPagePasswordPromptForTesting(*page));
    return;
  }
  if (password_probe_action_ == "disable") {
    const auto original_prompt = prompt->GetWeakPtr();
    const bool ready = CheckPasswordSavingToggle(true);
    const bool changed = ready && original_prompt &&
                         TogglePasswordSavingForProbe(false);
    FinishPasswordProbe(changed && !original_prompt &&
                        !GetPagePasswordPromptForTesting(*page));
    return;
  }
  if (password_probe_action_ == "cancel" || password_probe_action_ == "personal")
    delegate->CancelDialog();
  else
    delegate->AcceptDialog();
  const bool closed = !GetPagePasswordPromptForTesting(*page);
  if (!closed)
    LOG(ERROR) << "Password prompt remained open after the decision";
  FinishPasswordProbe(closed);
}

bool WorkspaceLab::CheckPasswordSavingToggle(std::optional<bool> expected) {
  shell_->OpenGeneralSettings();
  auto* search = shell_->preferences_search();
  search->SelectRange(gfx::Range(0, search->GetText().size()));
  search->input_client()->InsertText(
      prototype::T(u"Proponuj zapisywanie haseł"),
      ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);
  auto* control = shell_->preference_switch_for_testing("save_passwords");
  if (control) {
    control->GetWidget()->LayoutRootViewIfNecessary();
    control->ScrollViewToVisible();
  }
  const bool matches = control && control->IsDrawn() &&
      control->GetEnabled() == expected.has_value() &&
      WorkspaceProfile(active_index_)->GetPasswordSavingSetting() == expected &&
      (!expected || shell_->preference_switch_checked_for_testing(
                        "save_passwords") == *expected);
  if (!matches)
    LOG(ERROR) << "Password saving toggle did not reflect the active profile"
               << ", drawn=" << (control && control->IsDrawn())
               << ", editable=" << (control && control->GetEnabled());
  return matches;
}

bool WorkspaceLab::TogglePasswordSavingForProbe(bool enabled) {
  auto* control = shell_->preference_switch_for_testing("save_passwords");
  ui::AXActionData action;
  action.action = ax::mojom::Action::kDoDefault;
  return control && control->HandleAccessibleAction(action) &&
         WorkspaceProfile(active_index_)->GetPasswordSavingSetting() == enabled &&
         shell_->preference_switch_checked_for_testing("save_passwords") == enabled;
}

void WorkspaceLab::EnablePasswordSavingProbe() {
  const bool control_ready = CheckPasswordSavingToggle(false);
  const bool enabled = control_ready && TogglePasswordSavingForProbe(true);
  if (!enabled)
    LOG(ERROR) << "Could not re-enable password saving through Settings"
               << ", control_ready=" << control_ready;
  FinishPasswordProbe(enabled);
}

GURL WorkspaceLab::PasswordProbePage() const {
  if (password_probe_action_ == "check")
    return session_probe_url_.Resolve("/password-form/work");
  if (password_probe_action_ == "check-personal")
    return session_probe_url_.Resolve("/password-form/personal");
  return session_probe_url_.Resolve("/password-save/" + password_probe_action_);
}

void WorkspaceLab::FinishPasswordProbe(bool success) {
  if (success)
    std::puts("Zitter Content workspace password prompt passed");
  Finish(success);
}

void WorkspaceLab::ArmCloseHandler() {
  widget_->MakeCloseSynchronous(base::BindOnce(
      [](base::WeakPtr<WorkspaceLab> lab, views::Widget::ClosedReason) {
        if (!lab)
          return;
        if (lab->widget_->IsClosed()) {
          lab->Finish(!lab->smoke_);
          return;
        }
        lab->ArmCloseHandler();
        lab->RequestClose();
      },
      weak_factory_.GetWeakPtr()));
}

std::vector<content::WebContents*> WorkspaceLab::WorkspacePages(size_t workspace) const {
  std::vector<content::WebContents*> pages;
  for (const auto& tab : tabs_[workspace]) {
    if (auto* page = tab.view->web_contents())
      pages.push_back(page);
  }
  if (peek_workspace_id_ == model_.workspaces()[workspace].id && PeekPage())
    pages.push_back(PeekPage());
  return pages;
}

std::vector<content::WebContents*> WorkspaceLab::OpenPages() const {
  std::vector<content::WebContents*> pages;
  for (size_t workspace = 0; workspace < tabs_.size(); ++workspace) {
    auto workspace_pages = WorkspacePages(workspace);
    pages.insert(pages.end(), workspace_pages.begin(), workspace_pages.end());
  }
  if (private_session_) {
    for (const auto& window : private_session_->windows) {
      auto private_pages = window->OpenPages();
      pages.insert(pages.end(), private_pages.begin(), private_pages.end());
    }
  }
  return pages;
}

void WorkspaceLab::RequestClose() {
  if (!done_)
    return;
  ExitPageFullscreen();
  if (!model_.active() && !smoke_ && !session_probe_url_.is_valid() &&
      !private_owner_) {
    // A native Quit can arrive while the startup filter cache is loading.
    close_after_workspace_start_ = true;
    return;
  }
  if (saving_catalogue_) {
    close_after_catalogue_save_ = true;
    return;
  }
  CancelPermissionPrompt();
  media_access_->Stop();
  if (pending_download_deletions_) {
    close_after_download_deletions_ = true;
    return;
  }
  if (smoke_) {
    Finish(false);
    return;
  }
  if (saving_session_ || before_unload_batch_.pending() || clearing_profile_ ||
      replacing_timeline_ || download_close_prompt_pending_) {
    return;
  }
  auto pages = OpenPages();
  if (std::ranges::none_of(pages, [](auto* page) {
        return page->NeedToFireBeforeUnload();
      })) {
    ConfirmDownloadShutdown();
    return;
  }

  // Ask about the visible tab first, then the rest of both workspaces.
  const auto current = std::ranges::find(pages, web_contents());
  if (current != pages.end())
    std::rotate(pages.begin(), current, current + 1);
  CheckBeforeUnload(
      pages, base::BindOnce(&WorkspaceLab::ConfirmDownloadShutdown,
                            weak_factory_.GetWeakPtr()));
}

void WorkspaceLab::ConfirmDownloadShutdown() {
  if (private_owner_ &&
      std::ranges::any_of(private_owner_->private_session_->windows,
                          [this](const auto& window) {
                            return window.get() != this && window->done_;
                          })) {
    CommitClose();
    return;
  }
  int count = 0;
  for (const auto& context : contexts_)
    count += context->BlockingDownloadCount();
  if (private_session_)
    count += private_session_->profile->BlockingDownloadCount();
  if (count == 0) {
    CommitClose();
    return;
  }

  download_close_prompt_pending_ = true;
  shell_->CloseOverlays(true);
  if (widget_->IsMinimized())
    widget_->Restore();
  if (!widget_->IsVisible())
    widget_->Show();
  shell_->ConfirmQuitWithDownloads(
      count, base::BindOnce(&WorkspaceLab::OnDownloadShutdownAnswered,
                            weak_factory_.GetWeakPtr()));
}

void WorkspaceLab::OnDownloadShutdownAnswered(bool close) {
  download_close_prompt_pending_ = false;
  if (close)
    CommitClose();
}

base::DictValue WorkspaceLab::CaptureSession() const {
  base::DictValue session;
  session.Set("active_workspace", model_.active()->id);
  if (startup_import_pending_)
    session.Set("startup_import_pending", true);
  if (!removed_workspaces_.empty())
    session.Set("removed_workspaces", removed_workspaces_.Clone());
  if (!timeline_epochs_.empty())
    session.Set("timeline_epochs", timeline_epochs_.Clone());
  if (!link_rules_.empty())
    session.Set("link_rules", link_rules_.Clone());
  if (!shift_click_peek_)
    session.Set("shift_click_peek", false);
  if (!clean_address_copies_)
    session.Set("clean_address_copies", false);
  if (assistant_settings_)
    session.Set("assistant", assistant_settings_->ToValue());
  if (night_shift_config_.managed)
    session.Set("night_shift", night_shift_config_.ToValue());
  for (const auto& [profile, favorites] : profile_favorites_) {
    if (!favorites.empty())
      session.EnsureDict("profile_favorites")->Set(profile, WriteWorkspacePins(favorites));
  }
  for (size_t i = 0; i < std::min(profiles_.size(), kWorkspaceIds.size()); ++i) {
    if (profiles_[i].name != kBuiltinProfileNames[i])
      session.EnsureDict("profile_names")->Set(profiles_[i].id, profiles_[i].name);
  }
  for (size_t i = kWorkspaceIds.size(); i < profiles_.size(); ++i) {
    session.EnsureList("extra_profiles")->Append(WriteProfileMetadata(profiles_[i]));
  }
  for (size_t index = 0; index < tabs_.size(); ++index) {
    const auto& workspace = model_.workspaces()[index];
    session.Set(workspace.id, CaptureWorkspaceSession(index));
    if (std::ranges::contains(kWorkspaceIds, workspace.id)) {
      SetCatalogueName(session, workspace.id, workspace.name,
                       "workspace_names", "extra_workspaces");
    } else {
      const auto source = imported_workspace_sources_.find(workspace.id);
      AppendWorkspaceMetadata(session, workspace,
                              source == imported_workspace_sources_.end()
                                  ? std::string_view() : source->second);
    }
  }
  return session;
}

base::DictValue WorkspaceLab::CaptureWorkspaceSession(size_t workspace) const {
  base::ListValue tabs;
  for (const auto& tab : tabs_[workspace]) {
    auto* page = tab.view->web_contents();
    const auto committed = page ? WorkspaceDocumentURL(*page) : GURL();
    const auto& url = committed.is_empty() ? tab.deferred_url : committed;
    const auto title = page && !page->GetTitle().empty() ? page->GetTitle()
                                                       : tab.deferred_title;
    auto entry = prototype::WriteWorkspaceSessionTab(
        CanRestoreWorkspaceURL(url) && url.spec().size() <= 32768 ? url : GURL(),
        url.SchemeIs(content::kViewSourceScheme) ? std::u16string()
                                                  : title.substr(0, 8192));
    if (!tab.pin.empty())
      entry.Set("pin", tab.pin);
    if (!tab.custom_title.empty())
      entry.Set("custom_title", base::UTF16ToUTF8(tab.custom_title));
    tabs.Append(std::move(entry));
  }
  auto entry = base::DictValue()
                   .Set("profile_id", model_.workspaces()[workspace].profile_id)
                   .Set("active", static_cast<int>(selected_[workspace]))
                   .Set("tabs", std::move(tabs));
  if (!pins_[workspace].empty())
    entry.Set("pins", WriteWorkspacePins(pins_[workspace]));
  if (sleeping_[workspace])
    entry.Set("sleeping", true);
  const auto& split = splits_[workspace];
  if (!split.tabs.empty()) {
    base::ListValue members;
    base::ListValue weights;
    for (size_t index : split.tabs)
      members.Append(static_cast<int>(index));
    for (double weight : split.weights)
      weights.Append(weight);
    entry.Set("split", base::DictValue()
                           .Set("tabs", std::move(members))
                           .Set("weights", std::move(weights))
                           .Set("vertical", split.vertical));
  }
  return entry;
}

void WorkspaceLab::CommitClose() {
  if (session_path_.empty()) {
    Finish(true);
    return;
  }

  RecordTimeline(true);
  if (timeline_dirty_)
    PersistTimeline();
  saving_session_ = true;
  SaveSession(base::BindOnce(&WorkspaceLab::SessionSaved,
                            weak_factory_.GetWeakPtr()));
}

void WorkspaceLab::SaveSession(base::OnceCallback<void(bool)> done) {
  session_writer_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&WriteContentWorkspaceSession, session_path_, CaptureSession()),
      std::move(done));
}

void WorkspaceLab::PersistWorkspaceChanges() {
  if (session_path_.empty() || saving_catalogue_)
    return;
  SaveSession(base::BindOnce(&WorkspaceLab::PinSessionSaved,
                            weak_factory_.GetWeakPtr(),
                            std::exchange(pin_crash_save_pending_, false)));
}

void WorkspaceLab::PinSessionSaved(bool crash_probe, bool ok) {
  if (crash_probe) {
    if (!ok) {
      Finish(false);
      return;
    }
    std::puts("Zitter Content committed pin changes before forced exit");
    std::fflush(stdout);
    return;
  }
  if (!ok && !saving_session_)
    shell_->ShowFolderSaveError();
}

void WorkspaceLab::SessionSaved(bool ok) {
  if (ok || session_probe_url_.is_valid()) {
    Finish(ok);
    return;
  }
  saving_session_ = false;
  shell_->SetConfigurationError(true);
}

void WorkspaceLab::CheckBeforeUnload(
    const std::vector<content::WebContents*>& pages,
    base::OnceClosure action, base::OnceClosure cancelled) {
  std::vector<base::WeakPtr<content::WebContents>> topology;
  for (auto* page : OpenPages())
    topology.push_back(page->GetWeakPtr());
  auto source = web_contents()->GetWeakPtr();
  shell_->CloseOverlays(true);
  shell_->SetOperationStatus(
      prototype::WorkspaceShell::OperationStatusEvent::kChecking);

  before_unload_batch_.Start(
      pages,
      base::BindRepeating([](content::WebContents* page) {
        return prototype::PageDialogs::FromWebContents(page);
      }),
      base::BindRepeating(
          [](base::WeakPtr<WorkspaceLab> lab,
             const base::WeakPtr<content::WebContents>& source,
             const std::vector<base::WeakPtr<content::WebContents>>& topology) {
            return lab && source && lab->web_contents() == source.get() &&
                   std::ranges::equal(lab->OpenPages(), topology,
                                      [](auto* page, const auto& saved) {
                                        return page == saved.get();
                                      });
          },
          weak_factory_.GetWeakPtr(), std::move(source), std::move(topology)),
      base::BindOnce(
          [](base::WeakPtr<WorkspaceLab> lab, base::OnceClosure action,
             base::OnceClosure cancelled, bool proceed) {
            if (!lab)
              return;
            lab->shell_->SetOperationStatus(
                proceed ? prototype::WorkspaceShell::OperationStatusEvent::kDone
                        : prototype::WorkspaceShell::OperationStatusEvent::kCancelled);
            if (proceed)
              std::move(action).Run();
            else if (cancelled)
              std::move(cancelled).Run();
          },
          weak_factory_.GetWeakPtr(), std::move(action), std::move(cancelled)));
}

bool WorkspaceLab::CheckSessionProbeSetup() const {
  const auto* command = base::CommandLine::ForCurrentProcess();
  const auto extra_mode = command->GetSwitchValueASCII(kExtraWorkspaceProbe);
  const bool extra_workspace =
      (!extra_mode.empty() && extra_mode != "create") ||
      (command->HasSwitch(kAdditionalProfileProbe) &&
       command->GetSwitchValueASCII(kAdditionalProfileProbe) != "create");
  const size_t imported_workspaces =
      command->GetSwitchValueASCII(kInteractiveImportProbe).ends_with("repeat") ? 2 : 0;
  const bool extension_observer = command->HasSwitch(kExtensionInstallUiProbe);
  const size_t assistant_workspaces = command->HasSwitch(kAssistantCredentialProbe) ? 2 : 0;
  if (session_path_.empty() || restored_workspace_ != 1 ||
      tabs_.size() != kProbePaths.size() + extra_workspace +
                          imported_workspaces + extension_observer + assistant_workspaces ||
      !std::equal(kProbeSelection.begin(), kProbeSelection.end(), selected_.begin())) {
    return false;
  }
  for (size_t workspace = 0; workspace < kProbePaths.size(); ++workspace) {
    const bool restored_reader = workspace == 0 &&
        base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(kRssProbe) == "restore";
    const size_t fixture_tabs = kProbePaths[workspace].size();
    if (tabs_[workspace].size() != fixture_tabs + restored_reader)
      return false;
    if (restored_reader && !prototype::IsRssTabUrl(tabs_[workspace].back().deferred_url))
      return false;
    for (size_t index = 0; index < fixture_tabs; ++index) {
      const auto& tab = tabs_[workspace][index];
      if (tab.view->web_contents() ||
          tab.deferred_url != session_probe_url_.Resolve(kProbePaths[workspace][index])) {
        return false;
      }
    }
  }
  return true;
}

void WorkspaceLab::CheckSessionProbePage() {
  if (pin_probe_opening_) {
    auto* page = web_contents();
    const auto target = TabUrl(std::nullopt, "closed-work");
    const auto edited_target = session_probe_url_.Resolve("/work/pin-edited");
    const std::u16string_view expected_title = target == edited_target
        ? u"Zitter edited Work pin fixture" : u"Zitter pinned Work fixture";
    if (page->GetTitle() != expected_title || page->GetLastCommittedURL() != target)
      return;
    const auto& tabs = tabs_[active_index_];
    if (active_index_ != 0 || tabs.size() != 3 || selected_[0] != 2 ||
        tabs[2].pin != "closed-work" ||
        page->GetBrowserContext() != contexts_[0].get()) {
      LOG(ERROR) << "Unexpected active tab while exercising a migrated pin";
      Finish(false);
      return;
    }
    const auto probe_mode = base::CommandLine::ForCurrentProcess()
                                ->GetSwitchValueASCII(kWorkspacePinsProbe);
    if (target != edited_target && (probe_mode == "edit" || probe_mode == "crash")) {
      if (!UpdatePinnedUrl("work", "closed-work", edited_target) ||
          !ResetPinnedUrl("closed-work") || web_contents() != page) {
        LOG(ERROR) << "Could not reset the open pin to its edited URL";
        Finish(false);
      }
      return;
    }
    pin_probe_opening_ = false;
    session_probe_advancing_ = true;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce([](base::WeakPtr<WorkspaceLab> lab,
                                     std::string probe_mode) {
          if (!lab)
            return;
          lab->SelectTab(kProbeSelection[0]);
          lab->CloseTab(2);
          lab->SelectWorkspace(std::string(kWorkspaceIds[1]));
          const bool crash = probe_mode == "crash";
          if (crash || probe_mode == "edit") {
            lab->pin_crash_save_pending_ = crash;
            if (!lab->RenamePinnedFolder("work", "folder-work", " Work folder (edited) ")) {
              lab->Finish(false);
              return;
            }
          }
          std::puts("Zitter Content restored and reopened migrated pins");
          if (!crash)
            lab->RequestClose();
        }, weak_factory_.GetWeakPtr(), probe_mode));
    return;
  }
  if (session_probe_advancing_ ||
      !web_contents()->GetTitle().starts_with(u"Zitter workspace ") ||
      !web_contents()->GetLastCommittedURL().SchemeIsHTTPOrHTTPS()) {
    return;
  }
  const size_t workspace = active_index_;
  const size_t selected = kProbeSelection[workspace];
  const auto expected_url =
      session_probe_url_.Resolve(kProbePaths[workspace][selected]);
  const auto& companion = tabs_[workspace][1 - selected];
  const bool split_visible = SplitShown();
#if BUILDFLAG(IS_MAC)
  if (std::ranges::any_of(tabs_[1 - workspace], [](const Tab& tab) {
        return tab.view->parent() != nullptr;
      })) {
    Finish(false);
    return;
  }
#endif
  if (web_contents()->GetLastCommittedURL() != expected_url ||
      web_contents()->GetBrowserContext() != contexts_[workspace].get() ||
      web_contents()->GetTitle() != kProbeTitles[workspace] ||
      static_cast<bool>(companion.view->web_contents()) != split_visible ||
      companion.view->GetVisible() != split_visible ||
      shell_->split_tabs_for_testing() != splits_[workspace].tabs ||
      (workspace == 1 && tabs_[0][kProbeSelection[0]].view->web_contents())) {
    Finish(false);
    return;
  }

  session_probe_advancing_ = true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&WorkspaceLab::AdvanceSessionProbe,
                     weak_factory_.GetWeakPtr()));
}

bool WorkspaceLab::SubmitProbeCommand(std::u16string_view query,
                                      std::string_view command,
                                      std::string_view target) {
  if (!RunShortcut("palette"))
    return false;
  shell_->address_input()->input_client()->InsertText(
      std::u16string(query),
      ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);
  const auto& results = shell_->suggestions();
  return results.size() == 1 && results.front().command_id == command &&
         results.front().target_workspace_id == target &&
         shell_->HandleAddressKey(
             ui::KeyEvent(ui::EventType::kKeyPressed, ui::VKEY_RETURN, 0));
}

bool WorkspaceLab::RunProbePaletteCommand(std::u16string_view query,
                                          std::string_view command,
                                          std::string_view target) {
  if (!SubmitProbeCommand(query, command, target))
    return false;
  // Run the activation posted by the shared palette after it closes.
  base::RunLoop dispatched(base::RunLoop::Type::kNestableTasksAllowed);
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, dispatched.QuitClosure());
  dispatched.Run();
  return true;
}

void WorkspaceLab::RunGitHubFolderProbe(std::string phase) {
  auto& pins = pins_[active_index_];
  const std::string workspace = model_.active()->id;
  prototype::LiveFolderConfig config{"", "example/project", ""};
  if (phase == "create") {
    if (!pins.empty() ||
        !ConfigureGitHubFolder(workspace, "", "Pull requests", config,
                               kGitHubFixtureTokenFirst, false)) {
      Finish(false);
      return;
    }
  } else {
    if (pins.empty() || !pins.front().live_folder) {
      Finish(false);
      return;
    }
    const auto id = pins.front().id;
    if (phase == "replace" || phase == "remove") {
      const bool remove = phase == "remove";
      if (!ConfigureGitHubFolder(workspace, id, "Pull requests", config,
                                 remove ? "" : kGitHubFixtureTokenSecond, remove)) {
        Finish(false);
        return;
      }
    } else if (phase == "restart" || phase == "wrong-profile") {
      RefreshGitHubFolder(workspace, id);
    } else {
      Finish(false);
      return;
    }
  }
  CheckGitHubFolderProbe(std::move(phase), base::TimeTicks::Now() + base::Seconds(15));
}

void WorkspaceLab::CheckGitHubFolderProbe(std::string phase, base::TimeTicks deadline) {
  const auto& pins = pins_[active_index_];
  const bool wrong_profile = phase == "wrong-profile";
  if (base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "GitHub folder fixture did not complete";
    Finish(false);
    return;
  }
  const bool complete = !github_refresh_ && !pins.empty() && pins.front().live_folder &&
      github_next_refresh_.contains({model_.active()->id, pins.front().id}) &&
      (wrong_profile ? pins.size() == 1
                     : pins.size() == 2 && pins[1].parent_id == pins[0].id &&
                           pins[1].title == phase);
  if (complete) {
    std::puts("Zitter Content GitHub folder probe passed");
    RequestClose();
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckGitHubFolderProbe,
                                weak_factory_.GetWeakPtr(), std::move(phase), deadline),
      base::Milliseconds(50));
}

void WorkspaceLab::AdvanceSessionProbe() {
  session_probe_advancing_ = false;
  const auto* command = base::CommandLine::ForCurrentProcess();
  if (command->HasSwitch(kGitHubFolderProbe)) {
    const auto phase = command->GetSwitchValueASCII(kGitHubFolderProbe);
    if (active_index_ == 0 || phase == "wrong-profile") {
      session_probe_advancing_ = true;
      RunGitHubFolderProbe(phase);
      return;
    }
  }
  const bool sleep_probe = command->HasSwitch(kWorkspaceSleepProbe);
  if (active_index_ == 1) {
    if ((command->HasSwitch(kTimelineProbe) ||
         command->HasSwitch(kTimelinePanelProbe) ||
         command->HasSwitch(kTimelineClearProbe)) && !timeline_settings_ready_) {
      session_probe_advancing_ = true;
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE, base::BindOnce(&WorkspaceLab::AdvanceSessionProbe,
                                    weak_factory_.GetWeakPtr()),
          base::Milliseconds(25));
      return;
    }
    if (command->HasSwitch(kTimelineProbe)) {
      const auto historical = HistoricalClosedTab();
      if (LastClosedTab() || !historical ||
          historical->url != session_probe_url_.Resolve(kTimelineLegacyPersonal)) {
        LOG(ERROR) << "Personal did not retain its own migrated timeline";
        Finish(false);
        return;
      }
    }
    if (sleep_probe && (!sleeping_[0] ||
        std::ranges::any_of(tabs_[0], [](const Tab& tab) {
          return tab.view->web_contents() != nullptr;
        }))) {
      Finish(false);
      return;
    }
    if (!sleep_probe) {
      SelectWorkspace(std::string(kWorkspaceIds[0]));
    } else if (!SubmitProbeCommand(u"> wake Work", "select_space", kWorkspaceIds[0])) {
      LOG(ERROR) << "Command Center could not wake Work";
      Finish(false);
    }
    return;
  }
  if (command->HasSwitch("zitter-content-favicon-probe")) {
    session_probe_advancing_ = true;
    CheckFaviconProbe(0, base::TimeTicks::Now() + base::Seconds(8));
    return;
  }
  if (command->HasSwitch(kPrivateWindowProbe)) {
    session_probe_advancing_ = true;
    const auto phase = command->GetSwitchValueASCII(kPrivateWindowProbe);
    if (phase == "zoom-change" || phase == "zoom-reopen") {
      const bool reopening = phase == "zoom-reopen";
      auto has_zoom = [](content::WebContents* page, double factor) {
        return page && blink::ZoomValuesEqual(
            content::HostZoomMap::GetZoomLevel(page),
            blink::ZoomFactorToZoomLevel(factor));
      };
      shell_->OpenGeneralSettings();
      auto* search = shell_->preferences_search();
      search->input_client()->InsertText(
          prototype::T(u"Domyślne powiększenie"),
          ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);
      shell_->general_widget()->LayoutRootViewIfNecessary();
      auto* picker = shell_->preference_choice_for_testing("zoom");
      auto* personal = tabs_[1][selected_[1]].view->web_contents();
      if (!picker || !picker->IsDrawn() ||
          shell_->preference_choice_selection_for_testing("zoom") !=
              (reopening ? 5u : 4u) ||
          !has_zoom(web_contents(), reopening ? 1.75 : 1.25) ||
          !has_zoom(personal, 0.9)) {
        LOG(ERROR) << "Page zoom did not match the profile and settings picker";
        Finish(false);
        return;
      }
      if (!reopening)
        shell_->ActivatePreferenceChoiceForTesting("zoom", 5);
      shell_->CloseOverlays(true);
      auto* page = web_contents();
      auto* delegate = page->GetDelegate();
      auto zoom_key = [&](ui::KeyboardCode key) {
        for (auto type : {blink::WebInputEvent::Type::kRawKeyDown,
                          blink::WebInputEvent::Type::kKeyUp}) {
          input::NativeWebKeyboardEvent event(type, kPageZoomWebModifier,
                                              base::TimeTicks::Now());
          event.windows_key_code = key;
          if (delegate->PreHandleKeyboardEvent(page, event) !=
              content::KeyboardEventProcessingResult::HANDLED) {
            return false;
          }
        }
        return true;
      };
      if (reopening) {
        if (!zoom_key(ui::VKEY_0) || !has_zoom(page, 1.5)) {
          LOG(ERROR) << "Page zoom reset did not restore Work's default";
          Finish(false);
          return;
        }
      } else {
        if (!zoom_key(ui::VKEY_OEM_PLUS) || !has_zoom(page, 1.75) ||
            !zoom_key(ui::VKEY_OEM_MINUS) || !has_zoom(page, 1.5)) {
          LOG(ERROR) << "Page zoom shortcuts did not change the active Work page";
          Finish(false);
          return;
        }
        delegate->ContentsZoomChange(true);
        if (!has_zoom(page, 1.75)) {
          LOG(ERROR) << "Page zoom gesture did not change the active Work page";
          Finish(false);
          return;
        }
      }
      auto* work_map = content::HostZoomMap::GetForWebContents(page);
      auto* personal_map = content::HostZoomMap::GetForWebContents(personal);
      if (!has_zoom(page, reopening ? 1.5 : 1.75) || !has_zoom(personal, 0.9) ||
          !blink::ZoomValuesEqual(work_map->GetZoomLevelForHostAndScheme(
              "http", "localhost"), blink::ZoomFactorToZoomLevel(1.75)) ||
          !blink::ZoomValuesEqual(personal_map->GetZoomLevelForHostAndScheme(
              "http", "localhost"), blink::ZoomFactorToZoomLevel(1.1)) ||
          work_map->HasZoomLevel("http", "reset.example")) {
        LOG(ERROR) << "The page zoom picker did not update only Work";
        Finish(false);
        return;
      }
    }
#if BUILDFLAG(IS_MAC)
    constexpr int kPrivateModifiers = ui::EF_COMMAND_DOWN | ui::EF_SHIFT_DOWN;
#else
    constexpr int kPrivateModifiers = ui::EF_CONTROL_DOWN | ui::EF_SHIFT_DOWN;
#endif
    if (!contexts_[0]->site_permissions().SetPopupSetting(
            url::Origin::Create(session_probe_url_),
            ProfilePermissionController::PopupSetting::kAllow) ||
        !contexts_[0]->SetFingerprintProtection(true) ||
        !RunShortcut(shell_->ShortcutFor(
            ui::Accelerator(ui::VKEY_N, kPrivateModifiers)))) {
      LOG(ERROR) << "The private-window shortcut did not open a window";
      Finish(false);
      return;
    }
    CheckPrivateWindowProbe(0, base::TimeTicks::Now() + base::Seconds(8));
    return;
  }
  if (sleep_probe) {
    session_probe_advancing_ = true;
    if (command->GetSwitchValueASCII(kWorkspaceSleepProbe) == "before-unload") {
      web_contents()->GetPrimaryMainFrame()->NotifyUserActivation(
          blink::mojom::UserActivationNotificationType::kTest);
      if (!RunShortcut("sleep") || !before_unload_batch_.pending()) {
        LOG(ERROR) << "Sleep did not wait for the page beforeunload handler";
        Finish(false);
        return;
      }
      CheckSleepProbeDialog(false, false, base::TimeTicks::Now() + base::Seconds(6));
      return;
    }
    std::vector<base::WeakPtr<content::WebContents>> work_pages;
    for (const auto& tab : tabs_[0]) {
      if (auto* page = tab.view->web_contents())
        work_pages.push_back(page->GetWeakPtr());
    }
    if (sleeping_[0] || work_pages.empty() ||
        !SubmitProbeCommand(u"> sleep Work", "sleep_space", kWorkspaceIds[0])) {
      LOG(ERROR) << "Command Center could not sleep Work";
      Finish(false);
      return;
    }
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](base::WeakPtr<WorkspaceLab> lab,
               std::vector<base::WeakPtr<content::WebContents>> pages) {
              if (!lab)
                return;
              if (lab->active_index_ != 1 || !lab->sleeping_[0] ||
                  lab->sleeping_[1] ||
                  std::ranges::any_of(pages, [](const auto& page) { return !!page; })) {
                LOG(ERROR) << "Sleeping Work did not release its pages";
                lab->Finish(false);
                return;
              }
              lab->FinishSleepProbe();
            },
            weak_factory_.GetWeakPtr(), std::move(work_pages)));
    return;
  }
  if (command->HasSwitch(kShiftPeekProbe)) {
    session_probe_advancing_ = true;
    const bool restore = command->GetSwitchValueASCII(kShiftPeekProbe) == "restore";
    if (shift_click_peek_ == restore ||
        !Navigate(session_probe_url_.Resolve("/peek/gesture"), true)) {
      Finish(false);
      return;
    }
    CheckShiftPeekProbe(ShiftPeekProbePhase::kSource, restore,
                         base::TimeTicks::Now() + base::Seconds(16));
    return;
  }
  if (command->HasSwitch(kPeekProbe)) {
    session_probe_advancing_ = true;
    const auto target = session_probe_url_.Resolve("/peek/start");
    shell_->BuildLinkMenu(target, "work");
    auto* menu = shell_->link_menu();
    for (size_t item = 0; item < menu->GetItemCount(); ++item) {
      if (menu->GetLabelAt(item) == prototype::T(u"Otwórz w Peek") &&
          menu->IsEnabledAt(item)) {
        menu->ActivatedAt(item);
        CheckPeekProbe(PeekProbePhase::kStart, {},
                       base::TimeTicks::Now() + base::Seconds(16));
        return;
      }
    }
    Finish(false);
    return;
  }
  if (command->HasSwitch(kPopupPolicyProbe)) {
    using Permissions = ProfilePermissionController;
    session_probe_advancing_ = true;
    const bool restore = command->GetSwitchValueASCII(kPopupPolicyProbe) == "restore";
    auto* opener = web_contents();
    const auto origin = opener->GetPrimaryMainFrame()->GetLastCommittedOrigin();
    auto& permissions = contexts_[0]->site_permissions();
    if (!restore) {
      shell_->ToggleSitePanel();
      if (shell_->site_rows_for_testing() != 2) {
        Finish(false);
        return;
      }
      shell_->SetSitePermissionForTesting(1, 1);
      shell_->CloseSitePanel();
    }
    if (permissions.GetPopupSetting(origin) != Permissions::PopupSetting::kAllow) {
      Finish(false);
      return;
    }
    opener->GetPrimaryMainFrame()->ExecuteJavaScriptForTests(
        u"window.open('/popup-policy', 'allowed-popup');",
        base::DoNothing(), content::ISOLATED_WORLD_ID_GLOBAL);
    CheckPopupPolicyProbe(PopupPolicyProbePhase::kAllowed, restore,
                           opener->GetWeakPtr(),
                           base::TimeTicks::Now() + base::Seconds(12));
    return;
  }
  if (command->HasSwitch(kPopupProbe)) {
    session_probe_advancing_ = true;
    auto* opener = web_contents();
    opener->GetPrimaryMainFrame()->ExecuteJavaScriptForTests(
        u"if (!window.open('/popup-blocked')) document.title = 'Popup blocked';",
        base::DoNothing(), content::ISOLATED_WORLD_ID_GLOBAL);
    CheckPopupProbe(PopupProbePhase::kBlocked, opener->GetWeakPtr(),
                     base::TimeTicks::Now() + base::Seconds(16));
    return;
  }
  if (command->HasSwitch(kLinkRoutingProbe)) {
    session_probe_advancing_ = true;
    const auto phase = command->GetSwitchValueASCII(kLinkRoutingProbe) == "reopen"
                           ? LinkRoutingProbePhase::kSplit
                           : LinkRoutingProbePhase::kSaveRule;
    CheckLinkRoutingProbe(phase, web_contents()->GetWeakPtr(),
                           base::TimeTicks::Now() + base::Seconds(12));
    return;
  }
  if (command->HasSwitch(kSplitActionsProbe)) {
    session_probe_advancing_ = true;
    if (!Navigate(session_probe_url_.Resolve("/split-actions/left"), false)) {
      Finish(false);
      return;
    }
    CheckSplitActionsProbe(SplitActionsProbePhase::kSource, 0, {},
                           base::TimeTicks::Now() + base::Seconds(15));
    return;
  }
  if (command->HasSwitch(kWorkspacePinsProbe)) {
    const auto probe_mode = command->GetSwitchValueASCII(kWorkspacePinsProbe);
    if ((probe_mode == "edit" || probe_mode == "crash") && !ExercisePinEdits()) {
      Finish(false);
      return;
    }
    const auto& work_tabs = tabs_[0];
    const auto& personal_tabs = tabs_[1];
    if (work_tabs[kProbeSelection[0]].pin != "open-work" ||
        personal_tabs[kProbeSelection[1]].pin != "favorite-personal") {
      Finish(false);
      return;
    }
    OpenPin("open-work");
    if (work_tabs.size() != 2 || selected_[0] != kProbeSelection[0]) {
      Finish(false);
      return;
    }
    pin_probe_opening_ = true;
    OpenPin("closed-work");
    if (work_tabs.size() != 3) {
      Finish(false);
      return;
    }
    return;
  }
  if (command->HasSwitch(kTimelineClearProbe)) {
    session_probe_advancing_ = true;
    const std::string mode = command->GetSwitchValueASCII(kTimelineClearProbe);
    if ((mode == "profile" && !AcceptTimelineClearDialogForProbe()) ||
        ((mode == "undo" || mode == "global") && !ClearTimeline())) {
      LOG(ERROR) << "Could not initiate Time Machine clear for " << mode;
      Finish(false);
      return;
    }
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&WorkspaceLab::CheckTimelineClearProbe,
                                  weak_factory_.GetWeakPtr(), mode, false,
                                  base::TimeTicks::Now() + base::Seconds(5)));
    return;
  }
  if (command->HasSwitch(kTimelinePanelProbe)) {
    session_probe_advancing_ = true;
    auto before = CaptureWorkspaceSession(0);
    if (!before.contains("split") || !before.contains("pins") ||
        !RunShortcut("timeline") || !shell_->time_machine_visible()) {
      LOG(ERROR) << "Could not open Work's Time Machine with a split and pins";
      Finish(false);
      return;
    }
    auto* panel = shell_->time_machine_for_testing();
    const auto bounds = timeline_->current->timeline_bounds("work");
    panel->SetMoment(bounds.first_ms, /*snap=*/false);
    if (!bounds.valid || panel->workspace_id() != "work" ||
        panel->card_count_for_testing() != 1) {
      LOG(ERROR) << "Time Machine did not show Work's historical card";
      Finish(false);
      return;
    }
    panel->Restore();
    CheckTimelinePanelProbe(0, std::move(before),
                            base::TimeTicks::Now() + base::Seconds(5));
    return;
  }
  if (command->HasSwitch(kTimelineProbe)) {
    session_probe_advancing_ = true;
    const bool imported = command->GetSwitchValueASCII(kTimelineProbe) == "import";
    const auto target = session_probe_url_.Resolve(
        imported ? kTimelineLegacyWork : kTimelineRecordedWork);
    const auto historical = HistoricalClosedTab();
    if (LastClosedTab() || !historical || historical->url != target) {
      LOG(ERROR) << "The expected closed Work tab was not available from the timeline";
      Finish(false);
      return;
    }
    bool reopened;
    if (imported) {
      reopened = SubmitProbeCommand(u"> reopen closed tab", "reopen");
    } else if (native_menu_probe_) {
      reopened = native_menu_probe_.Run("reopen");
      if (reopened)
        std::puts("Zitter Content used the native menu for timeline recovery");
    } else {
      reopened = RunShortcut("reopen");
    }
    if (!reopened) {
      LOG(ERROR) << "Could not reopen Work from the timeline";
      Finish(false);
      return;
    }
    CheckTimelineProbe(target, imported, base::TimeTicks::Now() + base::Seconds(5));
    return;
  }
  if (command->HasSwitch(kAdblockListsProbe)) {
    session_probe_advancing_ = true;
    const bool restart =
        command->GetSwitchValueASCII(kAdblockListsProbe) == "restart";
    if (!adblock_filter_lists_ ||
        !contexts_[0]->GetAdblockListEnabled("ublock") ||
        contexts_[1]->GetAdblockListEnabled("ublock") ||
        !Navigate(session_probe_url_.Resolve(kFilterListsPage), true)) {
      Finish(false);
      return;
    }
    CheckFilterListsProbe(restart, 0, base::TimeTicks::Now() + base::Seconds(12));
    return;
  }
  if (command->HasSwitch("zitter-content-assistant-settings-probe")) {
    session_probe_advancing_ = true;
    const bool reopening =
        command->GetSwitchValueASCII("zitter-content-assistant-settings-probe") == "reopen";
    if (reopening) {
      shell_->OpenAssistantSettings();
    } else if (!ActivateWorkspaceCommandForProbe(u">assistant", "assistant_settings") ||
               shell_->preference_category_for_testing() != 10 ||
               !shell_->general_widget()->IsVisible()) {
      LOG(ERROR) << "Command Center did not open Assistant settings";
      Finish(false);
      return;
    }
    CheckAssistantSettingsProbe(reopening ? 10 : 0, reopening,
                                base::TimeTicks::Now() + base::Seconds(25));
    return;
  }
  if (command->HasSwitch(kAssistantCredentialProbe)) {
    session_probe_advancing_ = true;
    if (!os_crypt_ || !assistant_settings_ ||
        assistant_settings_->encrypted_api_key.empty()) {
      LOG(ERROR) << "The migrated Assistant credential was not available";
      Finish(false);
      return;
    }
    os_crypt_->GetInstance(base::BindOnce(
        [](base::WeakPtr<WorkspaceLab> lab,
           scoped_refptr<os_crypt_async::Encryptor> encryptor) {
          if (!lab)
            return;
          const auto& encoded = lab->assistant_settings_->encrypted_api_key;
          if (prototype::DecryptAssistantApiKey(encoded, *encryptor) !=
                  "zitter-assistant-credential-fixture" ||
              !prototype::DecryptAssistantApiKey("not base64!", *encryptor).empty() ||
              !prototype::DecryptAssistantApiKey("djEwAA==", *encryptor).empty()) {
            LOG(ERROR) << "The Assistant credential probe failed";
            lab->Finish(false);
            return;
          }
          std::puts("Zitter Content Assistant credential probe passed");
          if (!lab->RunShortcut("assistant") || !lab->assistant_->IsDrawn()) {
            lab->Finish(false);
            return;
          }
          lab->CheckAssistantPanelProbe(false, 0, base::TimeTicks::Now() + base::Seconds(50));
        }, weak_factory_.GetWeakPtr()));
    return;
  }
  if (command->HasSwitch(kAdblockCacheProbe)) {
    session_probe_advancing_ = true;
    adblock_cache_probe_ = std::make_unique<AdblockCacheProbe>(
        *web_contents(), session_probe_url_,
        base::BindOnce([](base::WeakPtr<WorkspaceLab> lab, bool passed) {
          if (!lab)
            return;
          lab->adblock_cache_probe_.reset();
          if (!passed) {
            lab->Finish(false);
            return;
          }
          std::puts("Zitter Content reapplied ad blocking on cached documents");
          lab->RequestClose();
        }, weak_factory_.GetWeakPtr()));
    adblock_cache_probe_->Start();
    return;
  }
  if (command->HasSwitch(kAdblockProfileProbe)) {
    session_probe_advancing_ = true;
    const bool restart =
        command->GetSwitchValueASCII(kAdblockProfileProbe) == "restart";
    if (restart) {
      StartAdblockProfileProbe(/*restart=*/true);
    } else if (Navigate(session_probe_url_.Resolve(kAdblockTrialPage), true)) {
      CheckAdblockTrialProbe(false, 0, base::TimeTicks::Now() + base::Seconds(12));
    } else {
      Finish(false);
    }
    return;
  }
#if BUILDFLAG(ENABLE_PRINTING)
  if (command->HasSwitch(kPdfExportProbe)) {
    session_probe_advancing_ = true;
    if (!Navigate(session_probe_url_.Resolve(kPdfExportPage), true)) {
      Finish(false);
      return;
    }
    CheckPdfExportProbe(base::TimeTicks::Now() + base::Seconds(10));
    return;
  }
#endif
  if (command->HasSwitch(kCommandCenterProbe)) {
    session_probe_advancing_ = true;
    CheckCommandCenterProbe(
        command->GetSwitchValueASCII(kCommandCenterProbe) == "reopen",
        base::TimeTicks::Now() + base::Seconds(8));
    return;
  }
  if (command->HasSwitch(kFindReloadProbe)) {
    session_probe_advancing_ = true;
    const bool copy = command->GetSwitchValueASCII(kFindReloadProbe) == "copy-address";
    if (!Navigate(session_probe_url_.Resolve(copy ? kCopyAddressPage : kFindReloadPage),
                  true)) {
      Finish(false);
      return;
    }
    CheckFindReloadProbe(0, base::TimeTicks::Now() + base::Seconds(8));
    return;
  }
  if (command->HasSwitch(kAdditionalProfileProbe)) {
    session_probe_advancing_ = true;
    if (command->GetSwitchValueASCII(kAdditionalProfileProbe) == "create") {
      if (WorkspaceCommandResults({}).size() != 6 ||
          !WorkspaceCommandResults(u"refresh all favicons").empty() ||
          !ActivateWorkspaceCommandForProbe(u">keyboard shortcuts", "shortcuts") ||
          !shell_->tools_widget()->IsVisible() ||
          !shell_->shortcut_search_for_testing() ||
          !ActivateWorkspaceCommandForProbe(u">search engines", "engines") ||
          !shell_->tools_widget()->IsVisible() ||
          shell_->shortcut_search_for_testing()) {
        LOG(ERROR) << "Command Center did not open the requested tools";
        Finish(false);
        return;
      }
      shell_->CloseOverlays(true);
      if (!ActivateWorkspaceCommandForProbe(u">workspace spaces", "spaces") ||
          !shell_->settings_visible() ||
          !ActivateWorkspaceCommandForProbe(u">profile accounts", "profiles") ||
          !shell_->profiles_widget()->IsVisible()) {
        LOG(ERROR) << "Command Center did not open the requested management panel";
        Finish(false);
        return;
      }
      shell_->profile_name_input()->SetText(u"Research profile");
      if (!shell_->SubmitProfileCreationForTesting()) {
        Finish(false);
        return;
      }
      CheckCreatedProfileProbe(base::TimeTicks::Now() + base::Seconds(10));
      return;
    }
    const auto& workspace = model_.workspaces().back();
    const auto expected = session_probe_url_.Resolve("/research-profile");
    if (contexts_.size() != 3 || profiles_.back().name != "Research profile" ||
        workspace.profile_id != profiles_.back().id ||
        WorkspaceProfile(2) != contexts_.back().get() ||
        tabs_[2].size() != 1 || tabs_[2][0].deferred_url != expected ||
        tabs_[2][0].view->web_contents()) {
      LOG(ERROR) << "The additional profile was not restored with its workspace";
      Finish(false);
      return;
    }
    SelectWorkspace(workspace.id);
    CheckAdditionalProfileProbe(base::TimeTicks::Now() + base::Seconds(10));
    return;
  }
  if (command->HasSwitch(kExtraWorkspaceProbe)) {
    session_probe_advancing_ = true;
    const auto mode = command->GetSwitchValueASCII(kExtraWorkspaceProbe);
    const bool reopening = mode != "create";
    if (mode == "reassigned") {
      SelectWorkspace(model_.workspaces().back().id);
      CheckWorkspaceReassignmentProbe(/*phase=*/0,
                                      base::TimeTicks::Now() + base::Seconds(10));
      return;
    }
    if (!reopening) {
      shell_->OpenWorkspaceSettings();
      shell_->StartWorkspaceCreationForTesting();
      shell_->SetProfilePickerSelectionForTesting(/*managed=*/false, 0);
      shell_->workspace_input()->SetText(u"Research");
      shell_->SaveWorkspaceForTesting();
    }
    CheckExtraWorkspaceProbe(reopening, false, base::TimeTicks::Now() + base::Seconds(10));
    return;
  }
  if (command->HasSwitch(kBrowserImportProbe)) {
    session_probe_advancing_ = true;
    RunBrowserImportProbe(command->GetSwitchValueASCII(kBrowserImportProbe) == "repeat");
    return;
  }
  if (command->HasSwitch(kInteractiveImportProbe)) {
    session_probe_advancing_ = true;
    widget_->Show();
    widget_->Activate();
    StartInteractiveImportProbe(command->GetSwitchValueASCII(kInteractiveImportProbe),
                                base::TimeTicks::Now() + base::Seconds(4));
    return;
  }
  if (command->HasSwitch(kRssProbe)) {
    session_probe_advancing_ = true;
    const bool reopening = command->GetSwitchValueASCII(kRssProbe) == "restore";
    widget_->Activate();
    const bool opened = reopening ? SubmitProbeCommand(u"> rss atom", "rss", {})
        : native_menu_probe_ ? native_menu_probe_.Run("rss") : RunShortcut("rss");
    if (!opened) {
      LOG(ERROR) << "Could not open the RSS reader";
      Finish(false);
      return;
    }
    CheckRssProbe(reopening, reopening, base::TimeTicks::Now() + base::Seconds(10));
    return;
  }
  if (command->HasSwitch(kNavigationProbe)) {
    session_probe_advancing_ = true;
#if BUILDFLAG(IS_MAC)
    if (native_menu_probe_) {
      const std::u16string version = u" " + base::UTF8ToUTF16(application_version_) +
                                     u" · Chromium ";
      auto displays_version = [&](auto&& self, views::View* view) -> bool {
        if (view->GetViewAccessibility().GetCachedName().find(version) !=
            std::u16string::npos) {
          return true;
        }
        return std::ranges::any_of(view->children(),
                                   [&](views::View* child) { return self(self, child); });
      };
      if (application_version_.empty() || !native_menu_probe_.Run("about")) {
        Finish(false);
        return;
      }
      auto* about = shell_->about_widget_for_testing();
      if (!about || !displays_version(displays_version, about->GetRootView())) {
        LOG(ERROR) << "The native About dialog did not show the bundle version";
        Finish(false);
        return;
      }
      about->Close();
      std::puts("Zitter Content verified the bundle version in native About");
    }
#endif
    CheckNavigationProbe(
        0, {web_contents()->GetWeakPtr(),
            tabs_[1][selected_[1]].view->web_contents()->GetWeakPtr()},
        base::TimeTicks::Now() + base::Seconds(5));
    return;
  }
  if (command->HasSwitch(kClosedTabProbe)) {
    session_probe_advancing_ = true;
    if (!Navigate(session_probe_url_.Resolve(kClosedTabProbePath), true)) {
      Finish(false);
      return;
    }
    CheckClosedTabProbe(0, base::TimeTicks::Now() + base::Seconds(5));
    return;
  }
  if (command->HasSwitch(kExtensionRegistryProbe)) {
    session_probe_advancing_ = true;
    const auto owner = ExtensionFixtureProfileIndex();
    if (!owner) {
      Finish(false);
      return;
    }
    const bool paired = command->HasSwitch(kExtensionFixturePeer);
    if (IsManagedExtensionSession()) {
      StartManagedExtensions(*owner);
      return;
    }
    base::RepeatingClosure barrier;
    if (paired) {
      barrier = base::BarrierClosure(
          2, base::BindOnce(
                 [](base::WeakPtr<WorkspaceLab> lab) {
                   if (!lab || lab->extension_probes_pending_ != 2)
                     return;
                   if (!lab->extension_probes_[1]->StartupWorkerIsRunning()) {
                     lab->FinishExtensionProbe(false);
                     return;
                   }
                   lab->extension_probes_[0]->ContinueAfterWorkerWakeUp();
                 },
                 weak_factory_.GetWeakPtr()));
    }
    extension_probes_pending_ = paired ? 2 : 1;
    for (size_t i = 0; i < extension_probes_pending_; ++i) {
      const size_t index = i == 0 ? *owner : 1 - *owner;
      auto* page = tabs_[index][selected_[index]].view->web_contents();
      if (!page || page->GetBrowserContext() != contexts_[index]) {
        FinishExtensionProbe(false);
        return;
      }
      extension_probes_[i] = std::make_unique<ExtensionNavigationProbe>(
          *contexts_[index], *contexts_[1 - index],
          session_path_.DirName().AppendASCII(i == 0 ? "extension-private"
                                                     : "extension-peer-private"),
          *page, barrier,
          base::BindRepeating(
              [](base::WeakPtr<WorkspaceLab> lab, ProfileContext& profile,
                 bool enabled) {
                return lab &&
                       lab->ToggleExtensionFromSettingsForProbe(profile, enabled);
              },
              weak_factory_.GetWeakPtr()),
          base::BindOnce(&WorkspaceLab::FinishExtensionProbe,
                         weak_factory_.GetWeakPtr()));
    }
    for (const auto& probe : extension_probes_) {
      if (probe)
        probe->Start();
    }
    return;
  }
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          kWorkspaceHistoryPanelProbe)) {
    session_probe_advancing_ = true;
    const auto imported_theme =
        command->GetSwitchValueASCII("zitter-content-imported-appearance-probe");
    if (!imported_theme.empty()) {
      if (!shell_->appearance_ready()) {
        base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
            FROM_HERE, base::BindOnce(&WorkspaceLab::AdvanceSessionProbe,
                                      weak_factory_.GetWeakPtr()),
            base::Milliseconds(25));
        return;
      }
      const auto theme = imported_theme == "dark" ? prototype::ThemeMode::kDark
                                                   : prototype::ThemeMode::kLight;
      if (shell_->theme_mode() != theme || shell_->pinned() ||
          shell_->search_engine() != prototype::SearchEngine::kBing ||
          shell_->workspace_color("work") != 0xff345678 ||
          shell_->workspace_emoji("work") != u"★") {
        LOG(ERROR) << "The Workspace did not apply the imported appearance";
        Finish(false);
        return;
      }
      std::puts("Zitter Content applied the imported Workspace appearance");
    }
    if (!RunShortcut("history")) {
      Finish(false);
      return;
    }
    CheckHistoryPanelProbe(base::TimeTicks::Now() + base::Seconds(5));
    return;
  }
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          kWorkspaceAddressHistoryProbe)) {
    session_probe_advancing_ = true;
    OpenAddressHistoryProbe();
    return;
  }
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          kWorkspaceClearDataProbe)) {
    session_probe_advancing_ = true;
    OpenClearDataDialog(widget_.get());
    auto* dialog = shell_->clear_data_dialog_for_testing();
    if (!dialog || !dialog->IsVisible()) {
      Finish(false);
      return;
    }
    dialog->widget_delegate()->AsDialogDelegate()->CancelDialog();
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&WorkspaceLab::ConfirmClearDataProbe,
                                  weak_factory_.GetWeakPtr()));
    return;
  }
  SelectWorkspace(std::string(kWorkspaceIds[1]));
  if (active_index_ != 1 || web_contents()->GetTitle() != kProbeTitles[1]) {
    Finish(false);
    return;
  }
  RequestClose();
}

bool WorkspaceLab::AcceptTimelineClearDialogForProbe() {
  OpenClearDataDialog(widget_.get());
  auto* dialog = shell_->clear_data_dialog_for_testing();
  if (!dialog || !dialog->IsVisible())
    return false;
  shell_->SetClearDataAdvancedForTesting(true);
  for (size_t kind = 0; kind < 3; ++kind)
    shell_->SetClearDataChoiceForTesting(kind, false);
  shell_->SetClearDataChoiceForTesting(7, true);
  auto* delegate = dialog->widget_delegate()->AsDialogDelegate();
  if (!delegate->IsDialogButtonEnabled(ui::mojom::DialogButton::kOk))
    return false;
  delegate->AcceptDialog();
  return true;
}

void WorkspaceLab::CheckTimelineClearProbe(std::string mode, bool follow_up,
                                            base::TimeTicks deadline) {
  auto fail = [&] {
    LOG(ERROR) << "Time Machine clear did not finish for " << mode
               << ", follow-up=" << follow_up;
    Finish(false);
  };
  if (clearing_profile_ || replacing_timeline_) {
    if (base::TimeTicks::Now() >= deadline) {
      fail();
      return;
    }
  } else {
    const auto marks = timeline_->current->timeline_marks();
    const auto has_mark = [&](std::string_view workspace, std::string_view name) {
      return std::ranges::any_of(marks, [&](const auto& mark) {
        return std::string_view(mark.workspace) == workspace &&
               std::string_view(mark.name) == name;
      });
    };
    if ((mode == "undo" || mode == "global") && !follow_up) {
      if (!marks.empty() || !shell_->toast_widget()->IsVisible()) {
        fail();
        return;
      }
      if (mode == "undo") {
        shell_->UndoToast();
      } else if (!AcceptTimelineClearDialogForProbe()) {
        fail();
        return;
      }
      follow_up = true;
      deadline = base::TimeTicks::Now() + base::Seconds(5);
    } else {
      if (mode == "global")
        shell_->UndoToast();
      const bool expected = mode == "profile" || mode == "undo"
          ? marks.size() == 3 && has_mark("work", "Work older") &&
                has_mark("personal", "Personal older") &&
                has_mark("personal", "Personal recent")
          : marks.empty() && !replacing_timeline_ && !HistoricalClosedTab();
      if (!expected) {
        fail();
        return;
      }
      SelectWorkspace(std::string(kWorkspaceIds[1]));
      std::puts("Zitter Content cleared Time Machine with the requested scope");
      RequestClose();
      return;
    }
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckTimelineClearProbe,
                                weak_factory_.GetWeakPtr(), std::move(mode),
                                follow_up, deadline),
      base::Milliseconds(25));
}

void WorkspaceLab::CheckTimelinePanelProbe(size_t phase,
                                           base::DictValue before,
                                           base::TimeTicks deadline) {
  const auto old_url = session_probe_url_.Resolve(kTimelineLegacyWork);
  auto* page = web_contents();
  bool ready = active_index_ == 0 && page &&
               page->GetBrowserContext() == contexts_[0].get();
  if (phase == 0) {
    ready = ready && !shell_->time_machine_visible() &&
            page->GetLastCommittedURL() == old_url &&
            page->GetTitle() == kTimelineProbeTitle &&
            tabs_[0].size() == 1 && splits_[0].tabs.empty() &&
            shell_->toast_widget()->IsVisible();
  } else if (phase == 1) {
    ready = ready && std::ranges::all_of(tabs_[0], [](const Tab& tab) {
      auto* page = tab.view->web_contents();
      return page && !page->IsLoading() && !page->GetLastCommittedURL().is_empty();
    }) && CaptureWorkspaceSession(0) == before;
  } else {
    const auto marks = timeline_->current->timeline_marks();
    ready = ready && shell_->time_machine_visible() &&
            std::ranges::any_of(marks, [](const auto& mark) {
              return mark.workspace == "work" && mark.name == "Command Center";
            });
  }
  if (!ready && base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "Time Machine panel probe failed in phase " << phase;
    Finish(false);
    return;
  }
  if (ready) {
    if (phase == 0) {
      shell_->UndoToast();
    } else if (phase == 1) {
      if (!SubmitProbeCommand(u"> save snapshot", "save_snapshot")) {
        LOG(ERROR) << "Could not save a snapshot through Command Center";
        Finish(false);
        return;
      }
    } else {
      SelectWorkspace(std::string(kWorkspaceIds[1]));
      std::puts("Zitter Content restored and undid a layout from Time Machine");
      RequestClose();
      return;
    }
    ++phase;
    deadline = base::TimeTicks::Now() + base::Seconds(5);
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckTimelinePanelProbe,
                                weak_factory_.GetWeakPtr(), phase,
                                std::move(before), deadline),
      base::Milliseconds(25));
}

void WorkspaceLab::CheckTimelineProbe(GURL target,
                                      bool record_new_tab,
                                      base::TimeTicks deadline) {
  auto* page = web_contents();
  if (active_index_ != 0 || page->GetBrowserContext() != contexts_[0].get() ||
      page->IsLoading() || page->GetLastCommittedURL() != target ||
      page->GetTitle() != kTimelineProbeTitle) {
    if (base::TimeTicks::Now() >= deadline) {
      LOG(ERROR) << "Timeline did not reopen the expected Work tab: " << target;
      Finish(false);
      return;
    }
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, base::BindOnce(&WorkspaceLab::CheckTimelineProbe,
                                  weak_factory_.GetWeakPtr(), std::move(target),
                                  record_new_tab, deadline),
        base::Milliseconds(25));
    return;
  }
  if (record_new_tab) {
    const auto next = session_probe_url_.Resolve(kTimelineRecordedWork);
    if (!Navigate(next, /*new_tab=*/false)) {
      Finish(false);
      return;
    }
    CheckTimelineProbe(next, false, base::TimeTicks::Now() + base::Seconds(5));
    return;
  }
  CloseTab(selected_[0]);
  SelectWorkspace(std::string(kWorkspaceIds[1]));
  std::puts("Zitter Content reopened a Work tab from the workspace timeline");
  RequestClose();
}

void WorkspaceLab::CheckFilterListsProbe(bool restart, size_t phase,
                                         base::TimeTicks deadline) {
  auto* page = web_contents();
  const std::u16string_view expected = phase == 0 && !restart ? u"List probe 100"
                      : phase == 3 ? u"List probe 110" : u"List probe 111";
  const bool settled = phase == 1
      ? !adblock_filter_lists_->updating() &&
            std::ranges::none_of(contexts_, [](const auto& profile) {
              return profile->IsAdblockBuilding();
            })
      : page && !page->IsLoading() && page->GetTitle() == expected;
  if (!settled) {
    if (base::TimeTicks::Now() >= deadline) {
      LOG(ERROR) << "Filter-list phase " << phase
                 << " did not settle, title="
                 << (page ? base::UTF16ToUTF8(page->GetTitle()) : "")
                 << ", list error=" << adblock_filter_lists_->last_error();
      Finish(false);
      return;
    }
  } else {
    if (phase == 0) {
      WriteAdBlocking(base::DictValue().Set("command", "update"));
      if (!adblock_filter_lists_->updating()) {
        Finish(false);
        return;
      }
    } else if (phase == 1) {
      if (adblock_filter_lists_->last_error().empty() == restart) {
        LOG(ERROR) << "Unexpected result from the list refresh";
        Finish(false);
        return;
      }
      RunPageCommand(prototype::PageCommand::kReload);
    } else {
      const size_t fixture = selected_[active_index_];
      SelectTab(kProbeSelection[active_index_]);
      CloseTab(fixture);
      if (phase == 3) {
        std::puts("Zitter Content applied, updated, and retained shared filter lists");
        RequestClose();
        return;
      }
      SelectWorkspace(std::string(kWorkspaceIds[1]));
      if (active_index_ != 1 ||
          !Navigate(session_probe_url_.Resolve(kFilterListsPage), true)) {
        Finish(false);
        return;
      }
    }
    ++phase;
    deadline = base::TimeTicks::Now() + base::Seconds(12);
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckFilterListsProbe,
                                weak_factory_.GetWeakPtr(), restart, phase,
                                deadline),
      base::Milliseconds(25));
}

void WorkspaceLab::CheckPrivateWindowProbe(int phase,
                                            base::TimeTicks deadline) {
  constexpr std::array<std::string_view, 4> kPaths = {
      "/private-window/seed", "/private-window/read", "/private-window/remaining",
      "/private-window/fresh"};
  using PopupSetting = ProfilePermissionController::PopupSetting;
  const auto origin = url::Origin::Create(session_probe_url_);
  const bool zoom_probe = !base::CommandLine::ForCurrentProcess()
                               ->GetSwitchValueASCII(kPrivateWindowProbe).empty();
  auto* session = private_session_.get();
  auto* first = session && !session->windows.empty()
      ? session->windows.front().get() : nullptr;
  auto* second = session && session->windows.size() == 2
      ? session->windows.back().get() : nullptr;
  auto navigate = [this](WorkspaceLab& window, std::string_view path) {
    return window.Navigate(session_probe_url_.Resolve(path), false);
  };
  auto loaded = [this](WorkspaceLab* window, std::string_view path) {
    auto* page = window ? window->web_contents() : nullptr;
    return page && page->GetLastCommittedURL() == session_probe_url_.Resolve(path) &&
           page->GetTitle() == u"Private window verified";
  };
  auto fail = [this, phase] {
    LOG(ERROR) << "Private window probe failed in phase " << phase;
    Finish(false);
  };

  bool ready = false;
  switch (phase) {
    case 0:
      if (first && !second) {
        if (native_menu_probe_ ? !native_menu_probe_.Run("private_window")
                               : !first->RunShortcut("private_window")) {
          fail();
          return;
        }
        second = session->windows.size() == 2
            ? session->windows.back().get() : nullptr;
        if (!second) {
          fail();
          return;
        }
      }
      if ((ready = first && second)) {
        const auto commands = first->WorkspaceCommandResults(u">");
        if (std::ranges::any_of(commands, [](const auto& entry) {
              const auto& id = entry.command_id;
              return id == "profiles" || id == "spaces" ||
                     id == "assistant" || id == "assistant_settings";
            })) {
          fail();
          return;
        }
        if (zoom_probe &&
            (session->profile->CanSetDefaultPageZoomLevel() ||
             !blink::ZoomValuesEqual(
                 content::HostZoomMap::GetZoomLevel(first->web_contents()),
                 blink::ZoomFactorToZoomLevel(1.5)) ||
             !blink::ZoomValuesEqual(
                 content::HostZoomMap::GetForWebContents(first->web_contents())
                     ->GetZoomLevelForHostAndScheme("http", "localhost"),
                 blink::ZoomFactorToZoomLevel(1.75)))) {
          fail();
          return;
        }
        if (session->profile->site_permissions().GetPopupSetting(origin) != PopupSetting::kAllow ||
            !session->profile->site_permissions().SetPopupSetting(origin, PopupSetting::kBlock) ||
            contexts_[0]->site_permissions().GetPopupSetting(origin) != PopupSetting::kAllow ||
            session->profile->GetAdblockEnabled() != true ||
            session->profile->GetFingerprintProtection() != true ||
            first->web_contents()->GetBrowserContext() != session->profile.get() ||
            second->web_contents()->GetBrowserContext() != session->profile.get() ||
            !session->profile->IsOffTheRecord() ||
            !navigate(*first, kPaths[0])) {
          fail();
          return;
        }
      }
      break;
    case 1:
      if ((ready = loaded(first, kPaths[0]))) {
        if (!second || !contexts_[0]->SetAdblockEnabled(false) ||
            session->profile->GetAdblockEnabled() != false ||
            !session->profile->SetAdblockEnabled(true) ||
            contexts_[0]->GetAdblockEnabled() != false ||
            !contexts_[0]->SetFingerprintProtection(false) ||
            session->profile->GetFingerprintProtection() != false ||
            !session->profile->SetFingerprintProtection(true) ||
            contexts_[0]->GetFingerprintProtection() != false ||
            !navigate(*second, kPaths[1])) {
          fail();
          return;
        }
      }
      break;
    case 2:
      if ((ready = loaded(second, kPaths[1])))
        first->RequestClose();
      break;
    case 3:
      if ((ready = first && !second)) {
        if (!navigate(*first, kPaths[2])) {
          fail();
          return;
        }
      }
      break;
    case 4:
      if ((ready = loaded(first, kPaths[2]))) {
        if (native_menu_probe_ && !zoom_probe) {
          first->widget_->Activate();
          if (!native_menu_probe_.Run("about") ||
              !first->shell_->about_widget_for_testing() ||
              !first->shell_->about_widget_for_testing()->IsVisible() ||
              shell_->about_widget_for_testing()) {
            LOG(ERROR) << "Native About did not target the private window";
            fail();
            return;
          }
          first->shell_->about_widget_for_testing()->Close();
          first->widget_->Activate();
          if (!native_menu_probe_.Run("dock_new_tab") ||
              !first->shell_->address_visible() || shell_->address_visible()) {
            LOG(ERROR) << "Native Dock new-tab did not target the private window";
            fail();
            return;
          }
          first->shell_->CloseOverlays(true);
          if (native_menu_probe_.Run("history") ||
              !native_menu_probe_.Run("settings") ||
              !first->shell_->general_widget() ||
              !first->shell_->general_widget()->IsVisible() ||
              (shell_->general_widget() && shell_->general_widget()->IsVisible())) {
            LOG(ERROR) << "Native history or settings ignored the private window";
            fail();
            return;
          }
          first->shell_->CloseOverlays(true);
          if (!native_menu_probe_.Run("close_tab") || first->done_ || !done_) {
            fail();
            return;
          }
        } else {
          first->CloseTab(0);
        }
      }
      break;
    case 5:
      if ((ready = !session)) {
        if (native_menu_probe_ && !zoom_probe) {
          widget_->Hide();
          if (!native_menu_probe_.Run("dock_reopen") || !widget_->IsVisible()) {
            fail();
            return;
          }
        }
        contexts_[0]->SetAdblockEnabled(true);
        SelectWorkspace(std::string(kWorkspaceIds[1]));
        if (active_index_ != 1 || !RunShortcut("private_window")) {
          fail();
          return;
        }
      }
      break;
    case 6:
      if ((ready = first != nullptr)) {
        if (session->profile->site_permissions().GetPopupSetting(origin) != PopupSetting::kBlock ||
            session->profile->GetAdblockEnabled() != false ||
            !navigate(*first, kPaths[3])) {
          fail();
          return;
        }
      }
      break;
    case 7:
      if ((ready = loaded(first, kPaths[3]))) {
        if (!zoom_probe) {
          std::puts("Zitter Content verified the private window lifecycle");
          RequestClose();
          return;
        }
        GURL::Replacements host;
        host.SetHostStr("localhost");
        if (!blink::ZoomValuesEqual(
                content::HostZoomMap::GetZoomLevel(first->web_contents()),
                blink::ZoomFactorToZoomLevel(0.9)) ||
            !first->Navigate(session_probe_url_.Resolve(kPaths[3])
                                 .ReplaceComponents(host), false)) {
          fail();
          return;
        }
      }
      break;
    case 8: {
      auto* page = first ? first->web_contents() : nullptr;
      if (page && page->GetLastCommittedURL().host() == "localhost" &&
          page->GetTitle() == u"Private window verified") {
        if (!blink::ZoomValuesEqual(content::HostZoomMap::GetZoomLevel(page),
                                    blink::ZoomFactorToZoomLevel(1.1))) {
          fail();
          return;
        }
        std::puts("Zitter Content verified profile page zoom");
        std::puts("Zitter Content verified the private window lifecycle");
        RequestClose();
        return;
      }
      break;
    }
  }

  if (ready) {
    ++phase;
    deadline = base::TimeTicks::Now() + base::Seconds(8);
  } else if (base::TimeTicks::Now() >= deadline) {
    fail();
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckPrivateWindowProbe,
                                weak_factory_.GetWeakPtr(), phase, deadline),
      base::Milliseconds(25));
}

void WorkspaceLab::CheckPrivateAdblockProbe(int phase,
                                             base::TimeTicks deadline) {
  const auto target = private_adblock_probe_url_.Resolve(kPrivateAdblockPage);
  auto* page = web_contents();
  const bool ready = phase == 0
      ? !adblock_filter_lists_->updating() &&
            !contexts_[0]->IsAdblockBuilding() &&
            !contexts_[1]->IsAdblockBuilding()
      : page->GetLastCommittedURL() == target &&
            page->GetTitle() == (phase == 2 ? u"Private adblock bypassed"
                                           : u"Private adblock filtered") &&
            adblock_shield_button_->count() == (phase == 2 ? -1 : 2);
  if (ready) {
    if (phase == 0) {
      if (!adblock_filter_lists_->last_error().empty() ||
          !Navigate(target, /*new_tab=*/false)) {
        LOG(ERROR) << "Private filter lists failed to initialize";
        Finish(false);
        return;
      }
    } else if (phase == 1) {
      ui::AXActionData activate;
      activate.action = ax::mojom::Action::kDoDefault;
      widget_->LayoutRootViewIfNecessary();
      adblock_shield_button_->HandleAccessibleAction(activate);
      auto* row = adblock_shield_widget_ && adblock_shield_widget_->IsVisible()
          ? adblock_shield_widget_->GetContentsView()->GetViewByID(
                kAdblockShieldToggleId)
          : nullptr;
      if (!row || !row->HandleAccessibleAction(activate) ||
          contexts_[0]->GetEditableAdblockSiteException(target) != true ||
          contexts_[1]->GetEditableAdblockSiteException(target) != false) {
        LOG(ERROR) << "Could not set an isolated private site exception";
        Finish(false);
        return;
      }
    } else if (phase == 2) {
      SelectWorkspace(std::string(kWorkspaceIds[1]));
      if (active_index_ != 1 || !Navigate(target, /*new_tab=*/false)) {
        Finish(false);
        return;
      }
    } else {
      std::puts("Zitter Content private workspace ad blocking passed");
      RequestClose();
      return;
    }
    ++phase;
    deadline = base::TimeTicks::Now() + base::Seconds(10);
  } else if (base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "Private ad blocking probe stalled in phase " << phase
               << ": " << base::UTF16ToUTF8(page->GetTitle());
    Finish(false);
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckPrivateAdblockProbe,
                                weak_factory_.GetWeakPtr(), phase, deadline),
      base::Milliseconds(25));
}

bool WorkspaceLab::CheckAdblockShieldRequestsForProbe(bool filtered) {
  auto* row = adblock_shield_widget_->GetContentsView()->GetViewByID(
      kAdblockShieldRequestsId);
  ui::AXActionData activate;
  activate.action = ax::mojom::Action::kDoDefault;
  if (!row || !adblock_shield_requests_ || adblock_shield_requests_->GetVisible() ||
      !row->HandleAccessibleAction(activate) ||
      !adblock_shield_requests_->GetVisible()) {
    return false;
  }
  adblock_shield_widget_->LayoutRootViewIfNecessary();
  std::vector<std::u16string> labels;
  for (const auto& child : adblock_shield_requests_->children()) {
    const auto* label = static_cast<const views::Label*>(child.get());
    if (!label->IsDrawn() || label->bounds().IsEmpty() ||
        !adblock_shield_requests_->GetLocalBounds().Contains(label->bounds())) {
      return false;
    }
    labels.emplace_back(label->GetText());
  }

  if (!filtered) {
    return labels == std::vector<std::u16string>{
                         prototype::T(u"Brak zablokowanych żądań sieciowych")};
  }
  return labels.size() == 2 &&
         std::ranges::contains(labels, base::UTF8ToUTF16(
             session_probe_url_.Resolve("/adblock-profile-target").spec())) &&
         std::ranges::contains(labels, base::UTF8ToUTF16(
             session_probe_url_.Resolve(kAdblockEditorTarget).spec()));
}

void WorkspaceLab::CheckAdblockTrialProbe(bool cosmetic, int step,
                                         base::TimeTicks deadline) {
  auto* page = web_contents();
  const bool blocked = step == 1 || step == 3;
  const auto fixture_url = session_probe_url_.Resolve(
      cosmetic ? kAdblockCosmeticTrialPage : kAdblockTrialPage);
  const bool arrived = step == 4
      ? page->GetLastCommittedURL().IsAboutBlank()
      : page->GetLastCommittedURL() == fixture_url &&
            page->GetTitle() == (blocked ? u"Adblock trial blocked"
                                         : u"Adblock trial allowed");
  if (!arrived) {
    if (base::TimeTicks::Now() >= deadline) {
      LOG(ERROR) << (cosmetic ? "Cosmetic" : "Network")
                 << " ad blocking trial did not finish step " << step
                 << ": " << base::UTF16ToUTF8(page->GetTitle());
      Finish(false);
      return;
    }
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&WorkspaceLab::CheckAdblockTrialProbe,
                       weak_factory_.GetWeakPtr(), cosmetic, step, deadline),
        base::Milliseconds(25));
    return;
  }
  const auto rule = cosmetic ? std::string(kAdblockCosmeticTrialRule)
      : base::StrCat({"|", session_probe_url_.Resolve(kAdblockTrialTarget).spec(),
                      "|$xmlhttprequest,domain=127.0.0.1"});
  if (ProfileContext::HasAdblockTrial(*page) != blocked ||
      std::ranges::contains(contexts_[0]->AdblockRules(), rule)) {
    LOG(ERROR) << "The rule trial changed persisted rules or had the wrong scope";
    Finish(false);
    return;
  }

  ui::AXActionData activate;
  activate.action = ax::mojom::Action::kDoDefault;
  bool advanced = true;
  switch (step) {
    case 0:
    case 2: {
      shell_->OpenAdBlockingSettings();
      OpenAdBlockingRules();
      auto* editor = ad_blocking_rules_panel_.get();
      auto* action = editor ? editor->PrepareTrialForTesting(
          base::UTF8ToUTF16(rule)) : nullptr;
      if (action)
        action->GetWidget()->LayoutRootViewIfNecessary();
      advanced = action && action->IsDrawn() &&
                 action->GetBoundsInScreen().width() > 0 &&
                 action->HandleAccessibleAction(activate);
      break;
    }
    case 1: {
      ToggleAdblockShield();
      auto* end = adblock_shield_widget_
          ? adblock_shield_widget_->GetContentsView()->GetViewByID(
                kAdblockShieldEndTrialId) : nullptr;
      advanced = end && end->HandleAccessibleAction(activate);
      break;
    }
    case 3:
      page->GetController().LoadURLWithParams(
          content::NavigationController::LoadURLParams(GURL("about:blank")));
      break;
    case 4:
      advanced = page->GetController().CanGoBack();
      if (advanced)
        page->GetController().GoBack();
      break;
    case 5: {
      if (!cosmetic) {
        if (Navigate(session_probe_url_.Resolve(kAdblockCosmeticTrialPage),
                     /*new_tab=*/false)) {
          CheckAdblockTrialProbe(true, 0,
                                 base::TimeTicks::Now() + base::Seconds(12));
        } else {
          Finish(false);
        }
        return;
      }
      const size_t fixture = selected_[0];
      SelectTab(kProbeSelection[0]);
      CloseTab(fixture);
      LOG(INFO) << "Zitter Content ad blocking trial passed";
      StartAdblockProfileProbe(/*restart=*/false);
      return;
    }
  }
  if (!advanced) {
    LOG(ERROR) << "Could not activate the ad blocking trial action at step " << step;
    Finish(false);
    return;
  }
  CheckAdblockTrialProbe(cosmetic, step + 1,
                         base::TimeTicks::Now() + base::Seconds(12));
}

void WorkspaceLab::StartAdblockProfileProbe(bool restart) {
  const auto rule = base::StrCat(
      {"|", session_probe_url_.Resolve(kAdblockEditorTarget).spec(),
       "|$xmlhttprequest"});
  const auto rules = contexts_[0]->AdblockRules();
  if (contexts_[0]->GetAdblockEnabled() != !restart ||
      std::ranges::contains(rules, rule) != restart) {
    LOG(ERROR) << "Work's migrated ad blocking state was not restored";
    Finish(false);
    return;
  }
  if (!restart) {
    shell_->OpenAdBlockingSettings();
    OpenAdBlockingRules();
    if (!ad_blocking_rules_panel_ || !ad_blocking_rules_panel_->widget() ||
        !ad_blocking_rules_panel_->AddForTesting(rule).empty()) {
      LOG(ERROR) << "Could not add a rule in the Work profile editor";
      Finish(false);
      return;
    }
    ad_blocking_rules_panel_->Close();
    shell_->CloseOverlays(true);
  }
  if ((restart && !WritePreference("ad_blocking", 1)) ||
      !Navigate(session_probe_url_.Resolve(kAdblockProfilePages[0]), true)) {
    Finish(false);
    return;
  }
  CheckAdblockProfileProbe(0, 0, base::TimeTicks::Now() + base::Seconds(8));
}

void WorkspaceLab::CheckAdblockProfileProbe(size_t workspace, int shield_step,
                                            base::TimeTicks deadline) {
  auto* page = web_contents();
  const bool filtered = workspace == 0 && shield_step != 1;
  const std::u16string_view title =
      filtered ? u"Profile adblock filtered" : u"Profile adblock bypassed";
  const int expected_count = filtered ? 3 : -1;
  if (active_index_ != workspace ||
      page->GetBrowserContext() != contexts_[workspace].get() ||
      page->GetTitle() != title ||
      adblock_shield_button_->count() != expected_count) {
    if (base::TimeTicks::Now() >= deadline) {
      LOG(ERROR) << "Unexpected ad blocking result in " << kWorkspaceIds[workspace]
                 << ": " << base::UTF16ToUTF8(page->GetTitle())
                 << ", shield count " << adblock_shield_button_->count()
                 << ", expected " << expected_count;
      Finish(false);
      return;
    }
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&WorkspaceLab::CheckAdblockProfileProbe,
                       weak_factory_.GetWeakPtr(), workspace, shield_step, deadline),
        base::Milliseconds(25));
    return;
  }

  if (workspace == 0 && shield_step < 2) {
    ui::AXActionData activate;
    activate.action = ax::mojom::Action::kDoDefault;
    widget_->LayoutRootViewIfNecessary();
    if (!adblock_shield_button_->GetVisible() ||
        !adblock_shield_button_->HandleAccessibleAction(activate) ||
        !adblock_shield_widget_ || !adblock_shield_widget_->IsVisible()) {
      LOG(ERROR) << "Could not open the Work ad blocking shield";
      Finish(false);
      return;
    }
    auto* row = adblock_shield_widget_->GetContentsView()->GetViewByID(
        kAdblockShieldToggleId);
    const auto url = page->GetLastCommittedURL();
    if (!CheckAdblockShieldRequestsForProbe(filtered) ||
        !row || !row->HandleAccessibleAction(activate) ||
        contexts_[0]->GetEditableAdblockSiteException(url) != (shield_step == 0) ||
        contexts_[1]->GetEditableAdblockSiteException(url) != true) {
      LOG(ERROR) << "Could not change the Work exception through the shield";
      Finish(false);
      return;
    }
    CheckAdblockProfileProbe(workspace, shield_step + 1,
                             base::TimeTicks::Now() + base::Seconds(8));
    return;
  }

  if (workspace == 1) {
    ToggleAdblockShield();
    if (!adblock_shield_widget_ || !adblock_shield_widget_->IsVisible() ||
        !CheckAdblockShieldRequestsForProbe(false)) {
      LOG(ERROR) << "The Personal shield showed another document's requests";
      Finish(false);
      return;
    }
    CloseAdblockShield();
  }
  const size_t fixture = selected_[workspace];
  SelectTab(kProbeSelection[workspace]);
  CloseTab(fixture);
  if (workspace == 0) {
    shell_->OpenAdBlockingSettings();
    auto* toggle = shell_->preference_switch_for_testing("ad_blocking");
    if (toggle) {
      toggle->GetWidget()->LayoutRootViewIfNecessary();
      toggle->ScrollViewToVisible();
    }
    ui::AXActionData activate;
    activate.action = ax::mojom::Action::kDoDefault;
    if (!toggle || !toggle->HandleAccessibleAction(activate) ||
        contexts_[0]->GetAdblockEnabled() != false) {
      LOG(ERROR) << "Could not switch off ad blocking through Settings";
      Finish(false);
      return;
    }
    shell_->CloseOverlays(true);
    SelectWorkspace(std::string(kWorkspaceIds[1]));
    if (active_index_ != 1 || contexts_[1]->GetAdblockEnabled() != true ||
        !Navigate(session_probe_url_.Resolve(kAdblockProfilePages[1]), true)) {
      Finish(false);
      return;
    }
    CheckAdblockProfileProbe(1, 0, base::TimeTicks::Now() + base::Seconds(8));
    return;
  }
  if (!Navigate(session_probe_url_.Resolve(kAdblockHistory404), true)) {
    Finish(false);
    return;
  }
  FinishAdblockHistoryProbe(base::TimeTicks::Now() + base::Seconds(5));
}

void WorkspaceLab::FinishAdblockHistoryProbe(base::TimeTicks deadline) {
  if (web_contents()->GetTitle() != u"Profile 404 fixture") {
    if (base::TimeTicks::Now() >= deadline) {
      LOG(ERROR) << "The HTTP 404 fixture did not load";
      Finish(false);
      return;
    }
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, base::BindOnce(&WorkspaceLab::FinishAdblockHistoryProbe,
                                  weak_factory_.GetWeakPtr(), deadline),
        base::Milliseconds(25));
    return;
  }
  const size_t fixture = selected_[1];
  SelectTab(kProbeSelection[1]);
  CloseTab(fixture);
  std::puts("Zitter Content applied separate migrated ad blocking settings");
  RequestClose();
}

void WorkspaceLab::CheckCreatedProfileProbe(base::TimeTicks deadline) {
  if (!saving_catalogue_ && contexts_.size() == 3) {
    if (tabs_.size() == 3) {
      if (WorkspaceProfile(2) != contexts_.back().get()) {
        Finish(false);
        return;
      }
      SelectWorkspace(model_.workspaces().back().id);
      if (active_index_ != 2 ||
          !Navigate(session_probe_url_.Resolve("/research-profile"), false)) {
        Finish(false);
        return;
      }
      CheckAdditionalProfileProbe(deadline);
      return;
    }
    shell_->OpenWorkspaceSettings();
    shell_->StartWorkspaceCreationForTesting();
    shell_->SetProfilePickerSelectionForTesting(/*managed=*/false, 2);
    shell_->workspace_input()->SetText(u"Research");
    shell_->SaveWorkspaceForTesting();
    if (!saving_catalogue_) {
      LOG(ERROR) << "Could not create the workspace for the new profile";
      Finish(false);
      return;
    }
  }
  if (base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "Timed out creating the live Content profile";
    Finish(false);
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckCreatedProfileProbe,
                                weak_factory_.GetWeakPtr(), deadline),
      base::Milliseconds(25));
}

void WorkspaceLab::CheckAdditionalProfileProbe(base::TimeTicks deadline) {
  auto* page = web_contents();
  if (active_index_ == 2 && page && !page->IsLoading() &&
      page->GetTitle() == u"Zitter additional profile isolated") {
    if (page->GetBrowserContext() != contexts_.back().get() ||
        page->GetLastCommittedURL() != session_probe_url_.Resolve("/research-profile")) {
      Finish(false);
      return;
    }
    RecordTimeline(true);
    SelectWorkspace(std::string(kWorkspaceIds[1]));
    std::puts("Zitter Content verified an additional encrypted profile");
    RequestClose();
    return;
  }
  if (base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "Timed out waiting for isolated additional profile storage";
    Finish(false);
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckAdditionalProfileProbe,
                                weak_factory_.GetWeakPtr(), deadline),
      base::Milliseconds(25));
}

void WorkspaceLab::CheckExtraWorkspaceProbe(bool reopening, bool selected,
                                            base::TimeTicks deadline) {
  const GURL fixture = session_probe_url_.Resolve("/extra-workspace");
  if (tabs_.size() == 3 && !saving_catalogue_) {
    const auto& extra = model_.workspaces().back();
    if (!selected) {
      if (extra.name != (reopening ? "Research renamed" : "Research") ||
          model_.workspaces().front().name != (reopening ? "Work renamed" : "Work") ||
          profiles_.front().name != (reopening ? "Work profile renamed" : "Work") ||
          extra.profile_id != "work" ||
          WorkspaceProfile(2) != contexts_[0].get() ||
          tabs_[2].size() != 1 || tabs_[2][0].view->web_contents() ||
          (reopening && tabs_[2][0].deferred_url != fixture) ||
          !shell_->workspace_button(extra.id)) {
        LOG(ERROR) << "Unexpected extra workspace configuration";
        Finish(false);
        return;
      }
      SelectWorkspace(extra.id);
      if (active_index_ != 2 || (!reopening && !Navigate(fixture, false))) {
        Finish(false);
        return;
      }
      selected = true;
    } else if (web_contents()->GetTitle() == u"Zitter extra workspace shares Work") {
      if (web_contents()->GetBrowserContext() != contexts_[0].get() ||
          web_contents()->GetLastCommittedURL() != fixture ||
          tabs_[0][kProbeSelection[0]].view->web_contents() == web_contents()) {
        Finish(false);
        return;
      }
      const auto mode = base::CommandLine::ForCurrentProcess()
                            ->GetSwitchValueASCII(kExtraWorkspaceProbe);
      if (mode == "tab-commands") {
        if (native_menu_probe_)
          widget_->Activate();
        if (!Navigate(session_probe_url_.Resolve("/tab-commands"), false)) {
          Finish(false);
          return;
        }
        CheckTabCommandsProbe(TabCommandProbePhase::kSource, extra.id, {},
                              base::TimeTicks::Now() + base::Seconds(12));
      } else if (mode == "favorites") {
        ExerciseSharedFavoritesProbe(extra.id);
      } else if (mode == "remove") {
        CheckWorkspaceRemovalProbe(0, extra.id, base::TimeTicks::Now() + base::Seconds(10));
      } else if (mode == "reassign") {
        shell_->OpenWorkspaceSettings();
        shell_->SetProfilePickerSelectionForTesting(/*managed=*/false, 1);
        shell_->SaveWorkspaceForTesting();
        CheckWorkspaceReassignmentProbe(/*phase=*/0, deadline);
      } else if (reopening) {
        FinishExtraWorkspaceProbe();
      } else {
        shell_->OpenWorkspaceSettings();
        shell_->workspace_input()->SetText(u"Research renamed");
        shell_->SaveWorkspaceForTesting();
        CheckWorkspaceRenameProbe(0, web_contents()->GetWeakPtr(), deadline);
      }
      return;
    }
  }
  if (base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "Timed out creating or restoring the extra Workspace";
    Finish(false);
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckExtraWorkspaceProbe,
                                weak_factory_.GetWeakPtr(), reopening,
                                selected, deadline),
      base::Milliseconds(25));
}

void WorkspaceLab::CheckWorkspaceRenameProbe(
    int phase, base::WeakPtr<content::WebContents> page, base::TimeTicks deadline) {
  if (!page || tabs_[2][0].view->web_contents() != page.get()) {
    LOG(ERROR) << "Renaming replaced the active workspace contents";
    Finish(false);
    return;
  }
  if (!saving_catalogue_) {
    switch (phase) {
      case 0:
        if (model_.active()->name == "Research renamed") {
          shell_->OpenProfiles("work");
          shell_->StartProfileRenameForTesting("work");
          shell_->profile_rename_input()->SetText(u"Work profile renamed");
          shell_->SubmitProfileRenameForTesting();
          ++phase;
        }
        break;
      case 1:
        if (profiles_.front().name == "Work profile renamed") {
          SelectWorkspace("work");
          shell_->OpenWorkspaceSettings();
          shell_->workspace_input()->SetText(u"Work renamed");
          shell_->SaveWorkspaceForTesting();
          ++phase;
        }
        break;
      case 2:
        if (model_.active()->name == "Work renamed") {
          SelectWorkspace(model_.workspaces()[2].id);
          if (web_contents() != page.get() || model_.active()->profile_id != "work") {
            Finish(false);
            return;
          }
          FinishExtraWorkspaceProbe();
          return;
        }
        break;
    }
  }
  if (base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "Timed out renaming the workspace or profile in phase " << phase;
    Finish(false);
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckWorkspaceRenameProbe,
                                weak_factory_.GetWeakPtr(), phase,
                                std::move(page), deadline),
      base::Milliseconds(25));
}

void WorkspaceLab::CheckShiftPeekProbe(
    ShiftPeekProbePhase phase, bool restore, base::TimeTicks deadline) {
  using Phase = ShiftPeekProbePhase;
  const auto plain = session_probe_url_.Resolve("/peek/shift-plain");
  const auto blank = session_probe_url_.Resolve("/peek/shift-blank");
  auto fail = [this, phase] {
    LOG(ERROR) << "Shift Peek probe failed in phase " << static_cast<int>(phase);
    Finish(false);
  };
  auto hovered = [this](const GURL& url) {
    auto* view = static_cast<WorkspaceWebView*>(
        tabs_[active_index_][selected_[active_index_]].view.get());
    return view->hovered_link_for_testing() == url;
  };
  auto close_fixture = [this] {
    SelectWorkspace("work");
    SelectTab(kProbeSelection[0]);
    CloseTab(2);
    SelectWorkspace("personal");
  };
  if (phase == Phase::kSource && web_contents()->GetTitle() == u"Shift Peek source") {
    widget_->Activate();
    web_contents()->Focus();
    HoverProbePage(*web_contents(), gfx::Point(30, restore ? 64 : 24));
    phase = restore ? Phase::kHoverBlank : Phase::kHoverPlain;
  } else if (phase == Phase::kHoverPlain && hovered(plain)) {
    ClickProbePage(*web_contents(), blink::WebInputEvent::kShiftKey, gfx::Point(30, 24));
    phase = Phase::kPlain;
  } else if (phase == Phase::kPlain && PeekPage() &&
             PeekPage()->GetTitle() == u"Peek shift-plain") {
    if (active_index_ != 0 || tabs_[0].size() != 3 || tabs_[1].size() != 2 ||
        PeekPage()->GetBrowserContext() != contexts_[1].get()) {
      fail();
      return;
    }
    PeekPage()->GetPrimaryMainFrame()->ExecuteJavaScriptWithUserGestureForTests(
        u"window.open('/peek/child', 'peek-child');", base::DoNothing(),
        content::ISOLATED_WORLD_ID_GLOBAL);
    phase = Phase::kChild;
  } else if (phase == Phase::kChild && web_contents()->GetTitle() == u"Peek child") {
    if (active_index_ != 1 || PeekPage() || tabs_[1].size() != 3 ||
        web_contents()->GetBrowserContext() != contexts_[1].get()) {
      fail();
      return;
    }
    SelectTab(kProbeSelection[1]);
    CloseTab(2);
    SelectWorkspace("work");
    HoverProbePage(*web_contents(), gfx::Point(30, 64));
    phase = Phase::kHoverBlank;
  } else if (phase == Phase::kHoverBlank && hovered(blank)) {
    ClickProbePage(*web_contents(), blink::WebInputEvent::kShiftKey, gfx::Point(30, 64));
    phase = Phase::kBlank;
  } else if (phase == Phase::kBlank) {
    auto* page = restore ? web_contents() : PeekPage();
    if (page && page->GetTitle() == u"Peek shift-blank") {
      if (page->GetBrowserContext() != contexts_[1].get() || tabs_[0].size() != 3 ||
          active_index_ != (restore ? 1u : 0u) ||
          tabs_[1].size() != (restore ? 3u : 2u)) {
        fail();
        return;
      }
      if (restore) {
        if (PeekPage()) {
          fail();
          return;
        }
        SelectTab(kProbeSelection[1]);
        CloseTab(2);
      } else {
        ClosePeek();
        if (!WritePreference("shift_click_peek", 0)) {
          fail();
          return;
        }
      }
      close_fixture();
      std::puts("Zitter Content verified Shift Peek and its saved setting");
      RequestClose();
      return;
    }
  }
  if (base::TimeTicks::Now() >= deadline) {
    fail();
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckShiftPeekProbe,
                                weak_factory_.GetWeakPtr(), phase, restore, deadline),
      base::Milliseconds(25));
}

void WorkspaceLab::CheckPeekProbe(
    PeekProbePhase phase, base::WeakPtr<content::WebContents> preview,
    base::TimeTicks deadline) {
  using Phase = PeekProbePhase;
  auto fail = [this, phase] {
    LOG(ERROR) << "Peek probe failed in phase " << static_cast<int>(phase);
    Finish(false);
  };
  auto loaded = [this](std::string_view path, std::u16string_view title) {
    auto* page = PeekPage();
    return page && page->GetLastCommittedURL() == session_probe_url_.Resolve(path) &&
           page->GetTitle() == title;
  };
  auto toolbar = [this](size_t index) {
    auto* button = shell_->peek_action_for_testing(index);
    ui::AXActionData activate;
    activate.action = ax::mojom::Action::kDoDefault;
    return button && button->HandleAccessibleAction(activate);
  };

  if (phase == Phase::kStart && loaded("/peek/start", u"Peek start")) {
    if (active_index_ != 0 || tabs_[0].size() != 2 || tabs_[1].size() != 2 ||
        PeekPage()->GetBrowserContext() != contexts_[1].get() ||
        web_contents()->GetLastCommittedURL() != session_probe_url_.Resolve("/work/active") ||
        !shell_->peek_widget()->IsVisible()) {
      fail();
      return;
    }
    preview = PeekPage()->GetWeakPtr();
    const auto window = shell_->peek_widget()->GetNativeWindow();
    if (!HandleNativeMenuCommand(window, {}, "hard_reload", true) ||
        !(native_menu_probe_ ? native_menu_probe_.Run("hard_reload")
                            : HandleNativeMenuCommand(window, {}, "hard_reload", false))) {
      LOG(ERROR) << "Hard Reload was unavailable while Peek was open";
      fail();
      return;
    }
    phase = Phase::kReloaded;
  } else if (phase == Phase::kReloaded && loaded("/peek/start", u"Peek reloaded")) {
    if (!preview || PeekPage() != preview.get() ||
        web_contents()->GetLastCommittedURL() != session_probe_url_.Resolve("/work/active")) {
      fail();
      return;
    }
    auto params = content::OpenURLParams::CreateBrowserInitiated(
        session_probe_url_.Resolve("/peek/next"), WindowOpenDisposition::CURRENT_TAB,
        ui::PAGE_TRANSITION_LINK);
    if (PeekPage()->OpenURL(params, {}) != PeekPage()) {
      fail();
      return;
    }
    phase = Phase::kNext;
  } else if (phase == Phase::kNext && loaded("/peek/next", u"Peek next")) {
    PeekPage()->GetPrimaryMainFrame()->ExecuteJavaScriptWithUserGestureForTests(
        u"document.addEventListener('fullscreenchange', () => {"
        u"  if (!document.fullscreenElement) document.title = 'Peek next';"
        u"});"
        u"document.documentElement.requestFullscreen().then(() => "
        u"  document.title = 'Peek fullscreen', () => document.title = 'Peek rejected');",
        base::DoNothing(), content::ISOLATED_WORLD_ID_GLOBAL);
    phase = Phase::kFullscreen;
  } else if (phase == Phase::kFullscreen && loaded("/peek/next", u"Peek fullscreen")) {
    auto* view = peek_tab_->view.get();
    if (!preview || fullscreen_page_.get() != preview.get() ||
        !preview->IsFullscreen() || !widget_->IsFullscreen() ||
        shell_->peek_widget()->IsVisible() || view->parent() != shell_->content_host() ||
        view->bounds() != shell_->content_host()->GetLocalBounds() ||
        active_index_ != 0 || preview->GetBrowserContext() != contexts_[1].get() ||
        !RunShortcut("exit_fullscreen")) {
      fail();
      return;
    }
    phase = Phase::kRestored;
  } else if (phase == Phase::kRestored && loaded("/peek/next", u"Peek next")) {
    if (!preview || PeekPage() != preview.get() || fullscreen_page_ ||
        preview->IsFullscreen() || widget_->IsFullscreen() ||
        !shell_->peek_widget()->IsVisible() || peek_tab_->view->parent() != shell_->peek_host() ||
        !preview->GetController().CanGoBack() || active_index_ != 0 || !toolbar(0)) {
      fail();
      return;
    }
    std::puts("Zitter Content restored Peek after fullscreen without replacing its page");
    phase = Phase::kBack;
  } else if (phase == Phase::kBack && loaded("/peek/start", u"Peek reloaded")) {
    if (!toolbar(1)) {
      fail();
      return;
    }
    phase = Phase::kForward;
  } else if (phase == Phase::kForward && loaded("/peek/next", u"Peek next")) {
    if (!toolbar(2)) {
      fail();
      return;
    }
    phase = Phase::kPromoted;
  } else if (phase == Phase::kPromoted && !PeekPage()) {
    if (!preview || web_contents() != preview.get() || active_index_ != 1 ||
        !web_contents()->GetController().CanGoBack() || tabs_[1].size() != 3 ||
        shell_->peek_widget()->IsVisible()) {
      fail();
      return;
    }
    SelectTab(kProbeSelection[1]);
    CloseTab(2);
    SelectWorkspace("work");
    if (!OpenPeek(session_probe_url_.Resolve("/peek/dirty"), "work")) {
      fail();
      return;
    }
    phase = Phase::kDirty;
  } else if (phase == Phase::kDirty && loaded("/peek/dirty", u"Peek dirty")) {
    preview = PeekPage()->GetWeakPtr();
    PeekPage()->GetPrimaryMainFrame()->NotifyUserActivation(
        blink::mojom::UserActivationNotificationType::kTest);
    if (!toolbar(4)) {
      fail();
      return;
    }
    phase = Phase::kDecline;
  } else if (phase == Phase::kDecline || phase == Phase::kAccept) {
    auto* page = PeekPage();
    auto* dialogs = page ? prototype::PageDialogs::FromWebContents(page) : nullptr;
    if (dialogs && dialogs->leave_dialog_open()) {
      const bool accept = phase == Phase::kAccept;
      if (!dialogs->HandleJavaScriptDialog(page, accept, nullptr)) {
        fail();
        return;
      }
      phase = accept ? Phase::kClosed : Phase::kRetry;
    }
  } else if (phase == Phase::kRetry && !before_unload_batch_.pending()) {
    if (!preview || PeekPage() != preview.get() ||
        !shell_->peek_widget()->IsVisible() || !toolbar(4)) {
      fail();
      return;
    }
    phase = Phase::kAccept;
  } else if (phase == Phase::kClosed && !before_unload_batch_.pending()) {
    if (PeekPage() || preview || shell_->peek_widget()->IsVisible() ||
        active_index_ != 0 || tabs_[1].size() != 2 ||
        !OpenPeek(session_probe_url_.Resolve("/peek/start"), "work")) {
      fail();
      return;
    }
    phase = Phase::kSplitOpen;
  } else if (phase == Phase::kSplitOpen && loaded("/peek/start", u"Peek start")) {
    preview = PeekPage()->GetWeakPtr();
    if (!toolbar(3)) {
      fail();
      return;
    }
    phase = Phase::kSplitPromoted;
  } else if (phase == Phase::kSplitPromoted && !PeekPage()) {
    if (!preview || web_contents() != preview.get() || active_index_ != 1 ||
        splits_[1].tabs != std::vector<size_t>{0, 2} ||
        tabs_[0].size() != 2 || shell_->peek_widget()->IsVisible()) {
      fail();
      return;
    }
    SelectTab(kProbeSelection[1]);
    CloseTab(2);
    std::puts("Zitter Content verified routed Peek and live promotion");
    RequestClose();
    return;
  }
  if (base::TimeTicks::Now() >= deadline) {
    fail();
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckPeekProbe,
                                weak_factory_.GetWeakPtr(), phase,
                                std::move(preview), deadline),
      base::Milliseconds(25));
}

void WorkspaceLab::CheckPopupPolicyProbe(
    PopupPolicyProbePhase phase, bool restore,
    base::WeakPtr<content::WebContents> opener, base::TimeTicks deadline) {
  using Phase = PopupPolicyProbePhase;
  auto fail = [this, phase] {
    LOG(ERROR) << "Popup policy probe failed in phase " << static_cast<int>(phase);
    Finish(false);
  };
  auto try_blocked = [](content::WebContents& page) {
    page.GetPrimaryMainFrame()->ExecuteJavaScriptForTests(
        u"document.title = 'Checking popup policy';"
        u"if (!window.open('/popup-blocked')) document.title = 'Popup policy blocked';",
        base::DoNothing(), content::ISOLATED_WORLD_ID_GLOBAL);
  };
  if (!opener) {
    fail();
    return;
  }
  if (phase == Phase::kAllowed && web_contents()->GetTitle() == u"Popup policy Work") {
    if (active_index_ != 0 || tabs_[0].size() != 3 ||
        web_contents()->GetBrowserContext() != contexts_[0].get()) {
      fail();
      return;
    }
    CloseTab(2);
    if (web_contents() != opener.get()) {
      fail();
      return;
    }
    opener->GetPrimaryMainFrame()->ExecuteJavaScriptForTests(
        u"const iframe = document.createElement('iframe');"
        u"iframe.src = 'http://localhost:' + location.port + '/popup-policy-iframe';"
        u"addEventListener('message', e => {"
        u"  if (e.source === iframe.contentWindow && e.data === 'popup blocked')"
        u"    document.title = 'Popup policy frame blocked';"
        u"}); document.body.append(iframe);",
        base::DoNothing(), content::ISOLATED_WORLD_ID_GLOBAL);
    phase = Phase::kEmbedded;
  } else if (phase == Phase::kEmbedded &&
             opener->GetTitle() == u"Popup policy frame blocked") {
    if (tabs_[0].size() != 2) {
      fail();
      return;
    }
    if (restore) {
      shell_->ToggleSitePanel();
      shell_->SetSitePermissionForTesting(1, 10);
      shell_->CloseSitePanel();
      try_blocked(*opener);
      phase = Phase::kReset;
    } else {
      SelectWorkspace("personal");
      try_blocked(*web_contents());
      phase = Phase::kOtherProfile;
    }
  } else if (phase == Phase::kReset && opener->GetTitle() == u"Popup policy blocked") {
    if (!WritePreference("popups", 1)) {
      fail();
      return;
    }
    opener->GetPrimaryMainFrame()->ExecuteJavaScriptForTests(
        u"window.open('/popup-policy', 'default-allowed-popup');",
        base::DoNothing(), content::ISOLATED_WORLD_ID_GLOBAL);
    phase = Phase::kDefaultAllowed;
  } else if (phase == Phase::kDefaultAllowed &&
             web_contents()->GetTitle() == u"Popup policy Work") {
    if (active_index_ != 0 || tabs_[0].size() != 3) {
      fail();
      return;
    }
    CloseTab(2);
    shell_->ToggleSitePanel();
    shell_->SetSitePermissionForTesting(1, 2);
    shell_->CloseSitePanel();
    try_blocked(*opener);
    phase = Phase::kExplicitBlock;
  } else if (phase == Phase::kExplicitBlock &&
             opener->GetTitle() == u"Popup policy blocked") {
    if (tabs_[0].size() != 2 || !WritePreference("popups", 0)) {
      fail();
      return;
    }
    shell_->ToggleSitePanel();
    shell_->SetSitePermissionForTesting(1, 10);
    shell_->CloseSitePanel();
    SelectWorkspace("personal");
    try_blocked(*web_contents());
    phase = Phase::kOtherProfile;
  } else if (phase == Phase::kOtherProfile &&
             web_contents()->GetTitle() == u"Popup policy blocked") {
    if (active_index_ != 1 || tabs_[0].size() != 2 || tabs_[1].size() != 2) {
      fail();
      return;
    }
    std::puts("Zitter Content verified per-profile popup exceptions");
    RequestClose();
    return;
  }
  if (base::TimeTicks::Now() >= deadline) {
    fail();
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckPopupPolicyProbe,
                                weak_factory_.GetWeakPtr(), phase, restore,
                                std::move(opener), deadline),
      base::Milliseconds(25));
}

void WorkspaceLab::CheckPopupProbe(
    PopupProbePhase phase, base::WeakPtr<content::WebContents> opener,
    base::TimeTicks deadline) {
  using Phase = PopupProbePhase;
  const auto target = session_probe_url_.Resolve("/popup-routed");
  auto fail = [this, phase] {
    LOG(ERROR) << "Popup probe failed in phase " << static_cast<int>(phase);
    Finish(false);
  };
  auto script = [](content::WebContents& page, std::u16string_view code) {
    page.GetPrimaryMainFrame()->ExecuteJavaScriptWithUserGestureForTests(
        std::u16string(code), base::DoNothing(), content::ISOLATED_WORLD_ID_GLOBAL);
  };
  if (!opener) {
    fail();
    return;
  }
  if (phase == Phase::kBlocked && opener->GetTitle() == u"Popup blocked") {
    opener->GetPrimaryMainFrame()->ExecuteJavaScriptForTests(
        u"document.title = 'Zitter workspace probe work';", base::DoNothing(),
        content::ISOLATED_WORLD_ID_GLOBAL);
    content::OpenURLParams params = content::OpenURLParams::CreateBrowserInitiated(
        session_probe_url_.Resolve("/popup-background"),
        WindowOpenDisposition::NEW_BACKGROUND_TAB, ui::PAGE_TRANSITION_LINK);
    params.user_gesture = true;
    if (tabs_[0].size() != 2 || !opener->OpenURL(params, {})) {
      fail();
      return;
    }
    phase = Phase::kBackground;
  } else if (phase == Phase::kBackground && tabs_[0].size() == 3) {
    auto* page = tabs_[0].back().view->web_contents();
    if (page && page->GetTitle() == u"Popup background Work") {
      if (web_contents() != opener.get() || tabs_[0].back().view->GetVisible() ||
          page->GetBrowserContext() != contexts_[0].get()) {
        fail();
        return;
      }
      CloseTab(2);
      script(*opener, u"window.open('/popup-work', 'popup-fixture');");
      phase = Phase::kSameProfile;
    }
  } else if (phase == Phase::kSameProfile && tabs_[0].size() == 3 &&
             web_contents()->GetTitle() == u"Popup same-profile opener") {
    if (web_contents()->GetBrowserContext() != contexts_[0].get()) {
      fail();
      return;
    }
    script(*web_contents(), u"window.close();");
    phase = Phase::kClosed;
  } else if (phase == Phase::kClosed && tabs_[0].size() == 2) {
    if (web_contents() != opener.get() || !SetLinkRule(target, "personal")) {
      fail();
      return;
    }
    phase = Phase::kRule;
  } else if (phase == Phase::kRule && !saving_catalogue_) {
    const auto* rule = link_rules_.FindString(target.host());
    if (!rule || *rule != "personal") {
      fail();
      return;
    }
    script(*opener,
           u"const f = document.createElement('form'); f.action = '/popup-form';"
           u"f.method = 'post'; f.target = '_blank';"
           u"const i = document.createElement('input'); i.name = 'value';"
           u"i.value = 'unchanged'; f.append(i); document.body.append(f); f.submit();");
    phase = Phase::kPostForm;
  } else if (phase == Phase::kPostForm && tabs_[0].size() == 3 &&
             web_contents()->GetTitle() == u"Popup POST form Work") {
    if (active_index_ != 0 || web_contents()->GetBrowserContext() != contexts_[0].get()) {
      fail();
      return;
    }
    CloseTab(2);
    script(*opener,
           u"const g = document.createElement('form'); g.action = '/popup-get-form';"
           u"g.target = '_blank';"
           u"const j = document.createElement('input'); j.name = 'value';"
           u"j.value = 'unchanged'; g.append(j); document.body.append(g); g.submit();");
    phase = Phase::kGetForm;
  } else if (phase == Phase::kGetForm && tabs_[0].size() == 3 &&
             web_contents()->GetTitle() == u"Popup GET form Work") {
    if (active_index_ != 0 || web_contents()->GetBrowserContext() != contexts_[0].get()) {
      fail();
      return;
    }
    CloseTab(2);
    phase = Phase::kFormClosed;
  } else if (phase == Phase::kFormClosed && tabs_[0].size() == 2) {
    if (web_contents() != opener.get()) {
      fail();
      return;
    }
    script(*opener,
           u"const a = document.createElement('a'); a.href = '/popup-routed';"
           u"a.target = '_blank'; document.body.append(a); a.click();");
    phase = Phase::kRouted;
  } else if (phase == Phase::kRouted && active_index_ == 1 &&
             web_contents()->GetTitle() == u"Popup routed Personal") {
    if (tabs_[0].size() != 2 || tabs_[1].size() != 3 ||
        opener->GetLastCommittedURL() != session_probe_url_.Resolve("/work/active") ||
        web_contents()->GetBrowserContext() != contexts_[1].get()) {
      fail();
      return;
    }
    SelectTab(kProbeSelection[1]);
    CloseTab(2);
    std::puts("Zitter Content verified blocked, same-profile, form, and routed popups");
    RequestClose();
    return;
  }
  if (base::TimeTicks::Now() >= deadline) {
    fail();
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckPopupProbe,
                                weak_factory_.GetWeakPtr(), phase,
                                std::move(opener), deadline),
      base::Milliseconds(25));
}

void WorkspaceLab::CheckLinkRoutingProbe(
    LinkRoutingProbePhase phase, base::WeakPtr<content::WebContents> source,
    base::TimeTicks deadline) {
  using Phase = LinkRoutingProbePhase;
  const auto target = session_probe_url_.Resolve("/routed-link");
  const auto* rule = link_rules_.FindString(target.host());
  auto fail = [this, phase] {
    LOG(ERROR) << "Link routing probe failed in phase " << static_cast<int>(phase);
    Finish(false);
  };
  auto activate_link_item = [this, &target](std::u16string_view label,
                                             bool in_submenu) {
    shell_->BuildLinkMenu(target, model_.active()->id);
    ui::MenuModel* menu = shell_->link_menu();
    if (in_submenu) {
      for (size_t i = 0; i < menu->GetItemCount(); ++i) {
        if (menu->GetTypeAt(i) == ui::MenuModel::TYPE_SUBMENU) {
          menu = menu->GetSubmenuModelAt(i);
          break;
        }
      }
    }
    for (size_t i = 0; i < menu->GetItemCount(); ++i) {
      if (menu->GetLabelAt(i) == label && menu->IsEnabledAt(i)) {
        menu->ActivatedAt(i);
        return true;
      }
    }
    return false;
  };
  if (!source) {
    fail();
    return;
  }
  if (phase == Phase::kSaveRule) {
    if (!activate_link_item(u"Personal · Personal", true)) {
      fail();
      return;
    }
    phase = Phase::kTyped;
  } else if (phase == Phase::kTyped && rule && !saving_catalogue_) {
    if (*rule != "personal" || active_index_ != 0) {
      fail();
      return;
    }
    if (tabs_[0].size() == 2) {
      if (!Navigate(session_probe_url_.Resolve("/typed-link"), true)) {
        fail();
        return;
      }
    } else if (web_contents()->GetTitle() == u"Zitter typed link work") {
      CloseTab(2);
      if (web_contents() != source.get()) {
        fail();
        return;
      }
      source->GetPrimaryMainFrame()->ExecuteJavaScriptWithUserGestureForTests(
          u"const a = document.createElement('a'); a.href = '/routed-link';"
          u"document.body.append(a); a.click();",
          base::DoNothing(), content::ISOLATED_WORLD_ID_GLOBAL);
      phase = Phase::kRouted;
    }
  } else if (phase == Phase::kRouted && active_index_ == 1 &&
             web_contents()->GetTitle() == u"Zitter routed link personal") {
    if (source->GetLastCommittedURL() != session_probe_url_.Resolve("/work/active") ||
        tabs_[0].size() != 2 || tabs_[1].size() != 3) {
      fail();
      return;
    }
    SelectTab(kProbeSelection[1]);
    CloseTab(2);
    std::puts("Zitter Content routed a renderer link to Personal");
    RequestClose();
    return;
  } else if (phase == Phase::kSplit) {
    if (!rule || *rule != "personal" ||
        !activate_link_item(prototype::T(u"Otwórz obok w Split View"), false)) {
      fail();
      return;
    }
    phase = Phase::kRemoveRule;
  } else if (phase == Phase::kRemoveRule && active_index_ == 1 &&
             web_contents()->GetTitle() == u"Zitter routed link personal") {
    if (rule) {
      if (!saving_catalogue_ &&
          (!SplitShown() || splits_[1].tabs != std::vector<size_t>{0, 2} ||
           !activate_link_item(prototype::TF(u"Wyłącz automatyczny wybór przestrzeni dla {}",
                                         {base::UTF8ToUTF16(target.host())}), true))) {
        fail();
        return;
      }
    } else if (!saving_catalogue_) {
      SelectTab(kProbeSelection[1]);
      CloseTab(2);
      std::puts("Zitter Content reopened and removed a link rule through Split View");
      RequestClose();
      return;
    }
  }
  if (base::TimeTicks::Now() >= deadline) {
    fail();
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckLinkRoutingProbe,
                                weak_factory_.GetWeakPtr(), phase,
                                std::move(source), deadline),
      base::Milliseconds(25));
}

void WorkspaceLab::CheckSplitActionsProbe(
    SplitActionsProbePhase phase, size_t answers,
    base::WeakPtr<content::WebContents> survivor, base::TimeTicks deadline) {
  using Phase = SplitActionsProbePhase;
  const auto& panes = splits_[0].tabs;
  const auto& tabs = tabs_[0];
  auto activate_menu = [this](std::optional<size_t> tab, std::string pin,
                              std::u16string_view label) {
    shell_->BuildTabMenu(tab, std::move(pin));
    auto* menu = shell_->workspace_menu();
    for (size_t item = 0; item < menu->GetItemCount(); ++item) {
      if (menu->GetLabelAt(item) == prototype::T(label) && menu->IsEnabledAt(item)) {
        menu->ActivatedAt(item);
        return true;
      }
    }
    LOG(ERROR) << "Split probe menu command unavailable: " << base::UTF16ToUTF8(label);
    return false;
  };
  auto fail = [this, phase] {
    LOG(ERROR) << "Split actions probe failed in phase " << static_cast<int>(phase);
    Finish(false);
  };
  if (phase == Phase::kSource && web_contents()->GetTitle() == u"Split fixture left") {
    if (!RunProbePaletteCommand(u"> split compare", "split") || !SplitShown()) {
      fail();
      return;
    }
    phase = Phase::kPaletteOpened;
  } else if (phase == Phase::kPaletteOpened &&
             web_contents()->GetController().GetLastCommittedEntry()) {
    const size_t source = kProbeSelection[0];
    if (!RunProbePaletteCommand(u"> focus left pane", "left_pane") ||
        selected_[0] != source ||
        !RunProbePaletteCommand(u"> focus right pane", "right_pane") ||
        selected_[0] == source ||
        !RunProbePaletteCommand(u"> focus left pane", "left_pane") ||
        !RunProbePaletteCommand(u"> split compare", "split") || SplitShown() ||
        selected_[0] != source || tabs.size() != 2) {
      fail();
      return;
    }
    std::puts("Zitter Content Command Center split and pane focus passed");
    if (!activate_menu({}, "split-pin", u"Otwórz obok w Split View")) {
      fail();
      return;
    }
    phase = Phase::kPinOpened;
  } else if (phase == Phase::kPinOpened && panes == std::vector<size_t>{1, 2} &&
             web_contents()->GetTitle() == u"Split fixture right") {
    if (selected_[0] != 2 || tabs[2].pin != "split-pin") {
      fail();
      return;
    }
    ResizeSplit(0, 0.65, true);
    if (!activate_menu(2, {}, u"Przesuń w lewo")) {
      fail();
      return;
    }
    phase = Phase::kReordered;
  } else if (phase == Phase::kReordered && panes == std::vector<size_t>{2, 1}) {
    const auto& weights = splits_[0].weights;
    if (selected_[0] != 2 || std::abs(weights[1] - 0.65) > 0.001 ||
        !activate_menu(0, {}, u"Otwórz obok w Split View")) {
      fail();
      return;
    }
    phase = Phase::kThirdPane;
  } else if (phase == Phase::kThirdPane && panes == std::vector<size_t>{2, 1, 0}) {
    survivor = web_contents()->GetWeakPtr();
    if (selected_[0] != 0 ||
        !activate_menu(0, {}, u"Wyjmij kartę ze splitu")) {
      fail();
      return;
    }
    phase = Phase::kPaneRemoved;
  } else if (phase == Phase::kPaneRemoved && panes == std::vector<size_t>{2, 1}) {
    if (tabs.size() != 3 || !survivor || tabs[0].view->web_contents() != survivor.get() ||
        selected_[0] != 1 || !SplitShown()) {
      fail();
      return;
    }
    for (size_t pane : panes) {
      tabs[pane].view->web_contents()->GetPrimaryMainFrame()->NotifyUserActivation(
          blink::mojom::UserActivationNotificationType::kTest);
    }
    if (!activate_menu(1, {}, u"Zamknij wszystkie karty w splicie")) {
      fail();
      return;
    }
    phase = Phase::kDecline;
  } else if (phase == Phase::kDecline || phase == Phase::kAccept) {
    if (tabs.size() != 3 || panes != std::vector<size_t>{2, 1}) {
      fail();
      return;
    }
    auto* page = tabs[panes[answers]].view->web_contents();
    auto* dialogs = prototype::PageDialogs::FromWebContents(page);
    if (dialogs && dialogs->leave_dialog_open()) {
      const bool accept = answers == 0 || phase == Phase::kAccept;
      if (!dialogs->HandleJavaScriptDialog(page, accept, nullptr)) {
        fail();
        return;
      }
      if (++answers == panes.size()) {
        phase = phase == Phase::kDecline ? Phase::kRetry : Phase::kClosed;
        answers = 0;
      }
    }
  } else if (phase == Phase::kRetry && !before_unload_batch_.pending()) {
    if (tabs.size() != 3 || panes != std::vector<size_t>{2, 1} ||
        !closed_tabs_[0].empty() ||
        !activate_menu(1, {}, u"Zamknij wszystkie karty w splicie")) {
      fail();
      return;
    }
    phase = Phase::kAccept;
  } else if (phase == Phase::kClosed && !before_unload_batch_.pending()) {
    if (tabs.size() != 1 || !panes.empty() || web_contents() != survivor.get() ||
        closed_tabs_[0].size() != 2 || TabUrl(std::nullopt, "split-pin").is_empty()) {
      fail();
      return;
    }
    if (!activate_menu({}, "split-pin", u"Otwórz obok w Split View")) {
      fail();
      return;
    }
    phase = Phase::kReopened;
  } else if (phase == Phase::kReopened && panes == std::vector<size_t>{0, 1} &&
             web_contents()->GetTitle() == u"Split fixture right") {
    web_contents()->GetPrimaryMainFrame()->NotifyUserActivation(
        blink::mojom::UserActivationNotificationType::kTest);
    if (!activate_menu(1, {}, u"Zamknij wszystkie karty w splicie")) {
      fail();
      return;
    }
    phase = Phase::kAcceptAll;
  } else if (phase == Phase::kAcceptAll) {
    auto* page = web_contents();
    auto* dialogs = prototype::PageDialogs::FromWebContents(page);
    if (dialogs && dialogs->leave_dialog_open()) {
      if (!dialogs->HandleJavaScriptDialog(page, true, nullptr)) {
        fail();
        return;
      }
      phase = Phase::kBlank;
    }
  } else if (phase == Phase::kBlank && !before_unload_batch_.pending()) {
    if (tabs.size() != 1 || !panes.empty() || survivor ||
        !shell_->address_visible() ||
        (!web_contents()->GetLastCommittedURL().is_empty() &&
         web_contents()->GetLastCommittedURL() != GURL("about:blank"))) {
      fail();
      return;
    }
    OpenPin("split-pin");
    if (tabs.size() != 2 || tabs[1].pin != "split-pin" ||
        !Navigate(session_probe_url_.Resolve("/split-actions/left"), true) ||
        !Navigate(session_probe_url_.Resolve("/split-actions/right"), true)) {
      fail();
      return;
    }
    SelectTab(0);
    survivor = web_contents()->GetWeakPtr();
    phase = Phase::kOtherReady;
  } else if (phase == Phase::kOtherReady &&
             tabs[1].view->web_contents()->GetTitle() == u"Split fixture right" &&
             tabs[2].view->web_contents()->GetTitle() == u"Split fixture left" &&
             tabs[3].view->web_contents()->GetTitle() == u"Split fixture right") {
    if (!SplitTab(1) || !SplitTab(2) || !SplitTab(3) ||
        !RunShortcut("third_pane") || selected_[0] != 2 ||
        !RunShortcut("fourth_pane") || selected_[0] != 3 ||
        !RunShortcut("left_pane") || web_contents() != survivor.get() ||
        !RunShortcut("close_split") || SplitShown()) {
      fail();
      return;
    }
    std::puts("Zitter Content focused third and fourth split panes");
    for (size_t index : {2u, 3u}) {
      tabs[index].view->web_contents()->GetPrimaryMainFrame()->NotifyUserActivation(
          blink::mojom::UserActivationNotificationType::kTest);
    }
    answers = 0;
    if (!RunProbePaletteCommand(u"> close other tabs", "close_other_tabs")) {
      fail();
      return;
    }
    phase = Phase::kOtherDecline;
  } else if (phase == Phase::kOtherDecline || phase == Phase::kOtherAccept) {
    if (tabs.size() != 4 || web_contents() != survivor.get()) {
      fail();
      return;
    }
    auto* page = tabs[2 + answers].view->web_contents();
    auto* dialogs = prototype::PageDialogs::FromWebContents(page);
    if (dialogs && dialogs->leave_dialog_open()) {
      const bool accept = answers == 0 || phase == Phase::kOtherAccept;
      if (!dialogs->HandleJavaScriptDialog(page, accept, nullptr)) {
        fail();
        return;
      }
      if (++answers == 2) {
        phase = accept ? Phase::kOtherClosed : Phase::kOtherRetry;
        answers = 0;
      }
    }
  } else if (phase == Phase::kOtherRetry && !before_unload_batch_.pending()) {
    if (tabs.size() != 4 || closed_tabs_[0].size() != 4 ||
        !RunProbePaletteCommand(u"> close other tabs", "close_other_tabs")) {
      fail();
      return;
    }
    phase = Phase::kOtherAccept;
  } else if (phase == Phase::kOtherClosed && !before_unload_batch_.pending()) {
    if (tabs.size() != 2 || web_contents() != survivor.get() ||
        tabs[1].pin != "split-pin" ||
        tabs[1].view->web_contents()->GetTitle() != u"Split fixture right" ||
        closed_tabs_[0].size() != 6 ||
        !WorkspaceCommandResults(u"> close other tabs").empty()) {
      fail();
      return;
    }
    auto* pinned = tabs[1].view->web_contents();
    auto* undo = shell_->toast_undo_for_testing();
    ui::AXActionData click;
    click.action = ax::mojom::Action::kDoDefault;
    if (!undo || !undo->IsDrawn() || !undo->HandleAccessibleAction(click) ||
        tabs.size() != 4 || web_contents() != survivor.get() ||
        tabs[1].view->web_contents() != pinned ||
        tabs[2].view->web_contents() || tabs[3].view->web_contents() ||
        tabs[2].deferred_url != session_probe_url_.Resolve("/split-actions/left") ||
        tabs[3].deferred_url != session_probe_url_.Resolve("/split-actions/right") ||
        closed_tabs_[0].size() != 4) {
      fail();
      return;
    }
    if (!RunProbePaletteCommand(u"> close other tabs", "close_other_tabs") ||
        tabs.size() != 2 || web_contents() != survivor.get() ||
        tabs[1].view->web_contents() != pinned) {
      fail();
      return;
    }
    std::puts("Zitter Content restored a closed tab group through the toast");
    std::puts("Zitter Content closed other tabs while keeping active and pinned pages");
    const auto cycle = [this](const char* command) {
      return native_menu_probe_ ? native_menu_probe_.Run(command)
                                : RunShortcut(command);
    };
    if (!cycle("previous_space") || active_index_ != 1 ||
        !cycle("next_space") || active_index_ != 0 ||
        !RunShortcut("space2") || active_index_ != 1) {
      fail();
      return;
    }
    std::puts("Zitter Content cycled workspaces and selected a numbered workspace");
    std::puts("Zitter Content verified split actions and batch beforeunload");
    RequestClose();
    return;
  }
  if (base::TimeTicks::Now() >= deadline) {
    fail();
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckSplitActionsProbe,
                                weak_factory_.GetWeakPtr(), phase, answers,
                                std::move(survivor), deadline),
      base::Milliseconds(25));
}

void WorkspaceLab::CheckTabCommandsProbe(
    TabCommandProbePhase phase, std::string research,
    base::WeakPtr<content::WebContents> duplicate, base::TimeTicks deadline) {
  using Phase = TabCommandProbePhase;
  auto move_from_menu = [this](size_t index, std::u16string_view destination) {
    shell_->BuildTabMenu(index);
    auto* menu = shell_->workspace_menu();
    for (size_t item = 0; item < menu->GetItemCount(); ++item) {
      if (menu->GetTypeAt(item) != ui::MenuModel::TYPE_SUBMENU)
        continue;
      auto* submenu = menu->GetSubmenuModelAt(item);
      for (size_t choice = 0; choice < submenu->GetItemCount(); ++choice) {
        if (submenu->GetLabelAt(choice) == destination) {
          if (!submenu->IsEnabledAt(choice))
            return false;
          submenu->ActivatedAt(choice);
          return true;
        }
      }
    }
    return false;
  };
  if (phase == Phase::kSource && web_contents()->GetTitle() == u"Zitter tab commands work") {
    if (!ActivateTabMuteMenuForTesting(selected_[active_index_], true)) {
      Finish(false);
      return;
    }
    phase = Phase::kMuted;
  } else if (phase == Phase::kMuted && web_contents()->IsAudioMuted()) {
    if (!ActivateTabMuteMenuForTesting(selected_[active_index_], false)) {
      Finish(false);
      return;
    }
    phase = Phase::kUnmuted;
  } else if (phase == Phase::kUnmuted && !web_contents()->IsAudioMuted()) {
    auto* original = web_contents();
    if (native_menu_probe_ &&
        (native_menu_probe_.Run("next_tab") || native_menu_probe_.Run("previous_tab"))) {
      Finish(false);
      return;
    }
    const bool duplicated = native_menu_probe_ ? native_menu_probe_.Run("duplicate_tab")
                                               : RunShortcut("duplicate_tab");
    if (!duplicated || web_contents() == original ||
        tabs_[2].size() != 2 || tabs_[2][0].view->web_contents() != original ||
        !web_contents()->GetController().CanGoBack()) {
      Finish(false);
      return;
    }
    duplicate = web_contents()->GetWeakPtr();
    phase = Phase::kDuplicate;
  } else if (phase == Phase::kDuplicate &&
             web_contents()->GetTitle() == u"Zitter tab commands duplicate") {
    if (native_menu_probe_ &&
        (!native_menu_probe_.Run("previous_tab") || selected_[2] != 0 ||
         !native_menu_probe_.Run("next_tab") || web_contents() != duplicate.get())) {
      Finish(false);
      return;
    }
    if (!RunShortcut("tab1") || selected_[2] != 0 ||
        RunShortcut("tab9") || !RunShortcut("tab2") ||
        web_contents() != duplicate.get() || !RunShortcut("space3") ||
        active_index_ != 2 || RunShortcut("space9")) {
      Finish(false);
      return;
    }
    std::puts("Zitter Content selected numbered tabs and workspaces");
    auto results = WorkspaceCommandResults(u"> move tab Work renamed");
    if (results.size() != 1 || results.front().command_id != "move_tab") {
      Finish(false);
      return;
    }
    SelectTab(0);
    ActivateWorkspaceCommand(std::move(results.front()));
    if (tabs_[0].size() != 2 || tabs_[2].size() != 2) {
      Finish(false);
      return;
    }
    SelectTab(1);
    if (!RunProbePaletteCommand(u"> move tab Work renamed", "move_tab", "work")) {
      Finish(false);
      return;
    }
    phase = Phase::kSameProfileMove;
  } else if (phase == Phase::kSameProfileMove && tabs_[0].size() == 3 && tabs_[2].size() == 1) {
    if (tabs_[0].back().view->web_contents() != duplicate.get() ||
        selected_[0] != kProbeSelection[0]) {
      Finish(false);
      return;
    }
    SelectWorkspace("work");
    SelectTab(2);
    if (!duplicate || web_contents() != duplicate.get() ||
        !web_contents()->GetController().CanGoBack()) {
      Finish(false);
      return;
    }
    web_contents()->GetPrimaryMainFrame()->NotifyUserActivation(
        blink::mojom::UserActivationNotificationType::kTest);
    if (!move_from_menu(2, u"Personal")) {
      Finish(false);
      return;
    }
    phase = Phase::kDeclineMove;
  } else if (phase == Phase::kDeclineMove || phase == Phase::kAcceptMove) {
    auto* dialogs = duplicate ? prototype::PageDialogs::FromWebContents(duplicate.get())
                              : nullptr;
    if (dialogs && dialogs->leave_dialog_open()) {
      const bool accept = phase == Phase::kAcceptMove;
      if (!before_unload_batch_.pending() || tabs_[0].size() != 3 ||
          tabs_[1].size() != 2 ||
          !dialogs->HandleJavaScriptDialog(duplicate.get(), accept, nullptr)) {
        Finish(false);
        return;
      }
      phase = accept ? Phase::kCrossProfileMove : Phase::kRetryMove;
    }
  } else if (phase == Phase::kRetryMove && !before_unload_batch_.pending()) {
    if (!duplicate || web_contents() != duplicate.get() ||
        tabs_[0].size() != 3 || tabs_[1].size() != 2 ||
        !move_from_menu(2, u"Personal")) {
      Finish(false);
      return;
    }
    phase = Phase::kAcceptMove;
  } else if (phase == Phase::kCrossProfileMove &&
             tabs_[0].size() == 2 && tabs_[1].size() == 3) {
    if (duplicate || tabs_[1].back().view->web_contents() ||
        tabs_[1].back().deferred_url != session_probe_url_.Resolve("/tab-commands") ||
        selected_[1] != kProbeSelection[1]) {
      Finish(false);
      return;
    }
    SelectWorkspace("personal");
    SelectTab(2);
    phase = Phase::kDestination;
  } else if (phase == Phase::kDestination &&
             web_contents()->GetTitle() == u"Zitter tab commands personal") {
    if (web_contents()->GetBrowserContext() != contexts_[1].get() ||
        web_contents()->GetController().CanGoBack()) {
      Finish(false);
      return;
    }
    SelectWorkspace(research);
    auto* last_page = web_contents();
    if (!MoveSidebarTab(0, "work") || tabs_[2].size() != 1 ||
        web_contents() == last_page || tabs_[0].back().view->web_contents() != last_page ||
        !tabs_[2][0].deferred_url.is_empty()) {
      Finish(false);
      return;
    }
    SelectWorkspace("work");
    SelectTab(2);
    if (web_contents() != last_page ||
        !last_page->GetController().CanGoBack() ||
        !RunProbePaletteCommand(u"> copy tab Personal", "copy_tab", "personal") ||
        active_index_ != 1 || tabs_[0].size() != 3 ||
        tabs_[0].back().view->web_contents() != last_page ||
        tabs_[1].size() != 4 || selected_[1] != 3) {
      Finish(false);
      return;
    }
    phase = Phase::kCopy;
  } else if (phase == Phase::kCopy &&
             web_contents()->GetTitle() == u"Zitter tab commands personal") {
    if (web_contents()->GetBrowserContext() != contexts_[1].get() ||
        web_contents()->GetController().CanGoBack()) {
      Finish(false);
      return;
    }
    std::puts("Zitter Content verified tab duplication, moves and profile-isolated copy");
    RequestClose();
    return;
  }
  if (base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "Tab commands probe timed out in phase " << static_cast<int>(phase);
    Finish(false);
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckTabCommandsProbe,
                                weak_factory_.GetWeakPtr(), phase,
                                std::move(research), std::move(duplicate),
                                deadline),
      base::Milliseconds(25));
}

void WorkspaceLab::ExerciseSharedFavoritesProbe(std::string research) {
  shell_->BuildTabMenu(std::nullopt, "favorite-work");
  auto* menu = shell_->workspace_menu();
  for (size_t index = 0; index < menu->GetItemCount(); ++index) {
    if (menu->GetLabelAt(index) == prototype::T(u"Duplikuj kartę") &&
        menu->IsEnabledAt(index)) {
      menu->ActivatedAt(index);
      CheckClosedFavoriteDuplicate(std::move(research),
                                   base::TimeTicks::Now() + base::Seconds(8));
      return;
    }
  }
  LOG(ERROR) << "The closed favorite has no enabled duplicate command";
  Finish(false);
}

void WorkspaceLab::CheckClosedFavoriteDuplicate(std::string research,
                                                base::TimeTicks deadline) {
  const std::string favorite = "favorite-work";
  const auto old_url = session_probe_url_.Resolve("/work/active");
  const auto new_url = session_probe_url_.Resolve("/work/pin-edited");
  auto* duplicate = web_contents();
  if (tabs_[active_index_].size() != 2 || !duplicate ||
      duplicate->GetLastCommittedURL() != old_url) {
    if (base::TimeTicks::Now() >= deadline) {
      LOG(ERROR) << "The closed favorite duplicate did not load";
      Finish(false);
      return;
    }
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, base::BindOnce(&WorkspaceLab::CheckClosedFavoriteDuplicate,
                                  weak_factory_.GetWeakPtr(),
                                  std::move(research), deadline),
        base::Milliseconds(25));
    return;
  }
  if (TabUrl(std::nullopt, favorite) != old_url ||
      !TabUrl(std::nullopt, "local-work").is_empty() ||
      !tabs_[active_index_][selected_[active_index_]].pin.empty() ||
      duplicate->GetBrowserContext() != contexts_[0].get() ||
      duplicate->GetController().CanGoBack()) {
    Finish(false);
    return;
  }
  OpenPin(favorite);
  auto* research_page = web_contents();
  if (tabs_[active_index_].size() != 3 || research_page == duplicate ||
      tabs_[active_index_][selected_[active_index_]].pin != favorite ||
      research_page->GetBrowserContext() != contexts_[0].get() ||
      !UpdatePinnedUrl(research, favorite, new_url)) {
    Finish(false);
    return;
  }
  SelectWorkspace("work");
  auto* work_page = web_contents();
  if (TabUrl(std::nullopt, favorite) != new_url ||
      TabUrl(std::nullopt, "local-work").is_empty() ||
      tabs_[0][selected_[0]].pin != favorite || work_page == research_page) {
    Finish(false);
    return;
  }
  SelectWorkspace("personal");
  if (!TabUrl(std::nullopt, favorite).is_empty()) {
    Finish(false);
    return;
  }
  SelectWorkspace(research);
  if (!Unpin(favorite, tabs_[active_index_].size()) ||
      !TabUrl(std::nullopt, favorite).is_empty() ||
      web_contents() != research_page ||
      !tabs_[active_index_][selected_[active_index_]].pin.empty()) {
    Finish(false);
    return;
  }
  SelectWorkspace("work");
  if (web_contents() != work_page ||
      !tabs_[0][selected_[0]].pin.empty() ||
      !TabUrl(std::nullopt, favorite).is_empty() ||
      TabUrl(std::nullopt, "local-work").is_empty()) {
    Finish(false);
    return;
  }
  shell_->SetPinned(true);
  shell_->BuildTabMenu(selected_[active_index_], {});
  auto* menu = shell_->workspace_menu();
  for (size_t index = 0; index < menu->GetItemCount(); ++index) {
    if (menu->GetLabelAt(index) != prototype::T(u"Przypnij do profilu") ||
        !menu->IsEnabledAt(index)) {
      continue;
    }
    menu->ActivatedAt(index);
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce([](base::WeakPtr<WorkspaceLab> lab) {
          if (!lab)
            return;
          const auto id = lab->tabs_[lab->active_index_][lab->selected_[lab->active_index_]].pin;
          if (id.empty() || !std::ranges::contains(lab->profile_favorites_["work"], id,
                                                  &prototype::PinnedItem::id) ||
              !lab->shell_->pin_button(id) ||
              lab->shell_->pin_button(id)->GetVisibleBounds().IsEmpty() ||
              !lab->Unpin(id, lab->tabs_[lab->active_index_].size())) {
            LOG(ERROR) << "The tab menu did not show a profile favorite";
            lab->Finish(false);
            return;
          }
          if (!lab->shell_->DragTabToProfileForTesting(lab->selected_[lab->active_index_])) {
            LOG(ERROR) << "Could not drag a tab onto the profile drop slot";
            lab->Finish(false);
            return;
          }
          base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
              FROM_HERE, base::BindOnce([](base::WeakPtr<WorkspaceLab> lab) {
                if (!lab)
                  return;
                const auto pin = lab->tabs_[lab->active_index_][lab->selected_[lab->active_index_]].pin;
                if (pin.empty() || !std::ranges::contains(lab->profile_favorites_["work"], pin,
                                                         &prototype::PinnedItem::id) ||
                    !lab->shell_->pin_button(pin) ||
                    lab->shell_->pin_button(pin)->GetVisibleBounds().IsEmpty() ||
                    !lab->Unpin(pin, lab->tabs_[lab->active_index_].size())) {
                  LOG(ERROR) << "Dropping the tab did not show a profile favorite";
                  lab->Finish(false);
                  return;
                }
                lab->SelectWorkspace("personal");
                std::puts("Zitter Content verified shared profile favorites, tab menu pinning, and drag and drop");
                lab->RequestClose();
              }, lab));
        }, weak_factory_.GetWeakPtr()));
    return;
  }
  LOG(ERROR) << "The open tab has no enabled profile favorite command";
  Finish(false);
}

void WorkspaceLab::CheckWorkspaceReassignmentProbe(
    int phase, base::TimeTicks deadline) {
  const auto* workspace = model_.active();
  if (!saving_catalogue_ && workspace && workspace->profile_id == "personal") {
    auto* page = web_contents();
    if (active_index_ != 2 || !page || workspace->name != "Research renamed" ||
        page->GetBrowserContext() != contexts_[1].get() ||
        tabs_[active_index_].size() != 1 || SplitShown()) {
      Finish(false);
      return;
    }
    if (phase == 0) {
      const auto& url = page->GetLastCommittedURL();
      if ((!url.is_empty() && url != GURL("about:blank")) ||
          !Navigate(session_probe_url_.Resolve("/reassigned-workspace"), false)) {
        Finish(false);
        return;
      }
      phase = 1;
    } else if (phase == 1 &&
               page->GetTitle() == u"Zitter reassigned workspace uses Personal") {
      if (!SaveTimelineSnapshot("Current assignment")) {
        Finish(false);
        return;
      }
      // The restart probe must see the blank tab saved by reassignment.
      page->GetController().LoadURL(GURL("about:blank"), content::Referrer(),
                                    ui::PAGE_TRANSITION_AUTO_TOPLEVEL, {});
      phase = 2;
    } else if (phase == 2 && page->GetLastCommittedURL() == GURL("about:blank")) {
      SelectWorkspace("personal");
      std::puts("Zitter Content verified workspace reassignment");
      RequestClose();
      return;
    }
  }
  if (base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "Workspace reassignment probe timed out";
    Finish(false);
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckWorkspaceReassignmentProbe,
                                weak_factory_.GetWeakPtr(), phase, deadline),
      base::Milliseconds(25));
}

void WorkspaceLab::CheckWorkspaceRemovalProbe(
    int phase, std::string extra, base::TimeTicks deadline) {
  auto accept_removal = [this](const std::string& id) {
    shell_->RequestRemoveWorkspace(id);
    auto* dialog = shell_->workspace_remove_dialog();
    if (!dialog)
      return false;
    dialog->widget_delegate()->AsDialogDelegate()->AcceptDialog();
    return true;
  };
  auto undo_visible = [this] {
    return shell_->toast_widget() && shell_->toast_widget()->IsVisible();
  };
  if (!saving_catalogue_) {
    if (phase == 0 && accept_removal("work")) {
      ++phase;
    } else if (phase == 1 && !WorkspaceIndex("work") && undo_visible() &&
               model_.active()->id == extra && active_index_ == 1 &&
               web_contents()->GetTitle() == u"Zitter extra workspace shares Work") {
      shell_->UndoToast();
      ++phase;
    } else if (phase == 2 && WorkspaceIndex("work") &&
               model_.Find("work")->name == "Work renamed" &&
               model_.active()->id == extra && accept_removal(extra)) {
      ++phase;
    } else if (phase == 3 && !WorkspaceIndex(extra) && undo_visible() &&
               model_.active()->id == "personal") {
      shell_->UndoToast();
      ++phase;
    } else if (phase == 4 && WorkspaceIndex(extra) &&
               model_.active()->id == extra &&
               web_contents()->GetTitle() == u"Zitter extra workspace shares Work" &&
               web_contents()->GetBrowserContext() == contexts_[0].get() &&
               accept_removal(extra)) {
      ++phase;
    } else if (phase == 5 && !WorkspaceIndex(extra) &&
               model_.active()->id == "personal") {
      std::puts("Zitter Content verified workspace removal and undo");
      RequestClose();
      return;
    }
  }
  if (base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "Workspace removal probe timed out in phase " << phase;
    Finish(false);
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckWorkspaceRemovalProbe,
                                weak_factory_.GetWeakPtr(), phase,
                                std::move(extra), deadline),
      base::Milliseconds(25));
}

void WorkspaceLab::FinishExtraWorkspaceProbe() {
  RecordTimeline(true);
  SelectWorkspace(std::string(kWorkspaceIds[1]));
  std::puts("Zitter Content verified an independent Workspace on Work");
  RequestClose();
}

void WorkspaceLab::StartInteractiveImportProbe(std::string mode,
                                               base::TimeTicks deadline) {
  const bool arc = mode.starts_with("arc-");
  const bool native = native_menu_probe_ && !arc;
  if (!(native ? native_menu_probe_.Run("import") : RunShortcut("import"))) {
    if (native && base::TimeTicks::Now() < deadline) {
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE, base::BindOnce(&WorkspaceLab::StartInteractiveImportProbe,
                                    weak_factory_.GetWeakPtr(), std::move(mode), deadline),
          base::Milliseconds(100));
      return;
    }
    LOG(ERROR) << "Could not open the browser import menu";
    Finish(false);
    return;
  }
  const auto browser = arc ? prototype::ImportBrowser::kArc
                           : prototype::ImportBrowser::kChrome;
  shell_->OpenImport(browser);
  shell_->SetImportOptionCheckedForTesting(2, true);
#if BUILDFLAG(IS_MAC)
  shell_->SetImportOptionCheckedForTesting(3, true);
#endif
  shell_->OpenImport(browser);
  CheckInteractiveImportProbe(false, mode.ends_with("repeat"),
                              base::TimeTicks::Now() + base::Seconds(12));
}

void WorkspaceLab::CheckInteractiveImportProbe(bool apply, bool repeat,
                                                base::TimeTicks deadline) {
  if (!apply && browser_import_preview_ && !browser_import_) {
    const auto& preview = *browser_import_preview_;
    const bool arc = base::CommandLine::ForCurrentProcess()
                         ->GetSwitchValueASCII(kInteractiveImportProbe)
                         .starts_with("arc-");
    if (preview.spaces.size() != 2 || preview.tab_count != (arc ? 3u : 2u) ||
        (arc && preview.folder_count != 1) || !preview.options.history ||
        (BUILDFLAG(IS_MAC) && !preview.options.cookies) ||
        contexts_.size() != (repeat ? 4u : 2u) ||
        tabs_.size() != (repeat ? 4u : 2u)) {
      LOG(ERROR) << "Unexpected browser import preview";
      Finish(false);
      return;
    }
    std::puts("Zitter Content approved the browser import preview");
    shell_->SubmitImportForTesting();
    if (!browser_import_) {
      LOG(ERROR) << "Browser import could not be started from the form";
      Finish(false);
      return;
    }
    apply = true;
  } else if (apply && !browser_import_ && !saving_catalogue_) {
    const auto* command = base::CommandLine::ForCurrentProcess();
    const bool rejected = command->GetSwitchValueASCII(kInteractiveImportProbe)
                              .starts_with("reject");
    if (contexts_.size() != (rejected ? 2u : 4u) ||
        tabs_.size() != (rejected ? 2u : 4u) ||
        imported_workspace_sources_.size() != (rejected ? 0u : 2u)) {
      LOG(ERROR) << "Unexpected imported browser catalogue";
      Finish(false);
      return;
    }
    if (!rejected && command->HasSwitch(kInteractiveImportKeychainProbe))
      VerifyInteractiveImportedCookies();
    else
      FinishInteractiveImportProbe();
    return;
  }
  if (base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "Browser import did not finish";
    Finish(false);
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckInteractiveImportProbe,
                                weak_factory_.GetWeakPtr(), apply, repeat, deadline),
      base::Milliseconds(25));
}

void WorkspaceLab::VerifyInteractiveImportedCookies() {
  const auto verified = base::BarrierClosure(2, base::BindOnce(
      &WorkspaceLab::FinishInteractiveImportProbe, weak_factory_.GetWeakPtr()));
  for (size_t i = kWorkspaceIds.size(); i < profiles_.size(); ++i) {
    const std::string_view name = profiles_[i].import_source == "chrome-profile-Default"
                                      ? "default" : "secondary";
    contexts_[i]->GetDefaultStoragePartition()->GetCookieManagerForBrowserProcess()
        ->GetAllCookies(base::BindOnce(
            [](base::WeakPtr<WorkspaceLab> lab, std::string_view name,
               base::RepeatingClosure verified,
               const std::vector<net::CanonicalCookie>& cookies) {
              if (!lab)
                return;
              if (cookies.size() != 1 ||
                  cookies.front().Name() != base::StrCat({"imported_", name}) ||
                  cookies.front().Value() != base::StrCat({"original-", name})) {
                LOG(ERROR) << "Unexpected decrypted browser import fixture value";
                lab->Finish(false);
                return;
              }
              verified.Run();
            }, weak_factory_.GetWeakPtr(), name, verified));
  }
}

void WorkspaceLab::FinishInteractiveImportProbe() {
  SelectWorkspace(std::string(kWorkspaceIds[1]));
  std::puts("Zitter Content verified the interactive browser import");
  RequestClose();
}

void WorkspaceLab::RunBrowserImportProbe(bool repeating) {
  const auto source = session_path_.DirName().AppendASCII("BrowserImportFixture");
  session_writer_->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce([](const base::FilePath& path) {
        prototype::BrowserImportOptions options;
        options.browser = prototype::ImportBrowser::kChrome;
        options.history = true;
        options.cookies = true;
        auto preview = prototype::ReadBrowserImport(options, path);
        if (!preview.error.empty() || preview.profiles.size() != 2 ||
            preview.spaces.size() != 2 || preview.tab_count != 2) {
          return std::shared_ptr<prototype::BrowsingImportData>();
        }
        return prototype::ReadBrowsingImportData(preview, base::BindOnce(
            [](prototype::ImportBrowser) -> std::optional<prototype::ImportedCookieKey> {
              return std::nullopt;
            }));
      }, source),
      base::BindOnce([](base::WeakPtr<WorkspaceLab> lab, bool repeating,
                        std::shared_ptr<prototype::BrowsingImportData> data) {
        if (!lab)
          return;
        if (!data || !data->error.empty() || data->profiles.size() != 2 ||
            !std::ranges::all_of(data->profiles, [](const auto& profile) {
              return profile.history.size() == 1 && profile.cookies.size() == 1;
            })) {
          LOG(ERROR) << "Could not read the browser import fixture";
          lab->Finish(false);
          return;
        }
        prototype::BrowsingImportDestinations destinations;
        // The fixture uses an explicit reverse mapping to catch positional writes.
        const std::array<const char*, 2> ids = {
            "chrome-profile-Profile 1", "chrome-profile-Default"};
        for (size_t i = 0; i < ids.size(); ++i) {
          destinations.emplace(ids[i], prototype::BrowsingImportDestination{
              base::BindRepeating([](base::WeakPtr<WorkspaceLab> owner, size_t index) {
                return owner ? owner->contexts_[index]->GetHistory() : nullptr;
              }, lab, i),
              base::BindRepeating([](base::WeakPtr<WorkspaceLab> owner, size_t index) {
                return owner ? owner->contexts_[index]->GetDefaultStoragePartition()
                                   ->GetCookieManagerForBrowserProcess() : nullptr;
              }, lab, i)});
        }
        prototype::WriteBrowsingImportDataToServices(
            std::move(data), std::move(destinations),
            base::BindOnce([](base::WeakPtr<WorkspaceLab> owner, bool repeating,
                              prototype::BrowsingImportResult result) {
              if (!owner)
                return;
              const size_t expected = repeating ? 0 : 2;
              if (result.failed || result.history != expected ||
                  result.cookies != expected || result.skipped != (repeating ? 4u : 0u)) {
                LOG(ERROR) << "Unexpected browser import result: " << result.history
                           << " history, " << result.cookies << " cookies, "
                           << result.skipped << " skipped, failed=" << result.failed;
                owner->Finish(false);
                return;
              }
              owner->SelectWorkspace(std::string(kWorkspaceIds[1]));
              std::puts("Zitter Content verified imported history and cookies");
              owner->RequestClose();
            }, lab, repeating));
      }, weak_factory_.GetWeakPtr(), repeating));
}

void WorkspaceLab::CheckRssProbe(bool reopening,
                                 bool subscribed,
                                 base::TimeTicks deadline) {
  const GURL feed = session_probe_url_.Resolve(kRssFeedPath);
  if (prototype::IsRssTabUrl(web_contents()->GetVisibleURL()) &&
      !web_contents()->IsLoading()) {
    auto* view = static_cast<WorkspaceWebView*>(tabs_[0][selected_[0]].view.get());
    auto* panel = view->MountReader();
    auto* reader = panel->reader();
    auto& other_reader = contexts_[1]->GetRssReader();
    if (reader->ready() && panel->discovered_for_testing() == 1 && !subscribed) {
      reader->Add(feed);
      subscribed = true;
    }
    if (subscribed && reader->ready() && !reader->busy() && other_reader.ready()) {
      const auto& feeds = reader->feeds();
      if (!other_reader.feeds().empty() || tabs_[0].size() != 3 ||
          feeds.size() != 1 || feeds[0].url != feed || feeds[0].title != "Field notes" ||
          feeds[0].articles.size() != 1 || feeds[0].articles[0].id != "fixture" ||
          feeds[0].articles[0].text != "A field note" ||
          (reopening && (!feeds[0].articles[0].saved || !feeds[0].articles[0].read))) {
        LOG(ERROR) << "RSS data did not match the current profile and fixture";
        Finish(false);
        return;
      }
      panel->Select(feed, "fixture");
      reader->Mark(feed, "fixture", /*read=*/true, /*saved=*/true);
      SelectTab(kProbeSelection[0]);
      SelectWorkspace(std::string(kWorkspaceIds[1]));
      std::puts("Zitter Content verified native RSS and profile isolation");
      RequestClose();
      return;
    }
  }
  if (base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "Timed out opening, discovering or fetching the RSS fixture";
    Finish(false);
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckRssProbe,
                                weak_factory_.GetWeakPtr(), reopening,
                                subscribed, deadline),
      base::Milliseconds(25));
}

void WorkspaceLab::CheckFindReloadProbe(int phase,
                                        base::TimeTicks deadline) {
  auto* page = web_contents();
  auto command = [this](const char* id) {
    if (native_menu_probe_) {
      widget_->Activate();
      return native_menu_probe_.Run(id);
    }
    return RunShortcut(id);
  };
  auto fail = [this, phase] {
    LOG(ERROR) << "Find and hard reload probe failed in phase " << phase
               << ", title " << base::UTF16ToUTF8(web_contents()->GetTitle());
    Finish(false);
  };
  auto count_is = [this](std::u16string ordinal) {
    return shell_->find_count_for_testing() ==
           prototype::TF(u"{} z {}", {std::move(ordinal), u"2"});
  };
  bool ready = false;
  switch (phase) {
    case 0:
      if ((ready = page->GetTitle() == u"Find and reload initial")) {
#if BUILDFLAG(IS_MAC)
        if (base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
                kFindReloadProbe) == "copy-address") {
          auto* install = reinterpret_cast<bool (*)()>(
              dlsym(RTLD_DEFAULT, "ZitterInstallClipboardFixture"));
          auto* matches = reinterpret_cast<bool (*)(const char*)>(
              dlsym(RTLD_DEFAULT, "ZitterClipboardFixtureMatches"));
          const auto clean = session_probe_url_.Resolve(
              "/find-reload?value=kept#section");
          if (!install || !matches || !install()) {
            fail();
            return;
          }
          shell_->SetPinned(true);
          shell_->ShowSidebar();
          auto* button = shell_->copy_url_button_for_testing();
          ui::AXActionData press;
          press.action = ax::mojom::Action::kDoDefault;
          if (!button || !button->HandleAccessibleAction(press) ||
              !matches(clean.spec().c_str())) {
            fail();
            return;
          }
          ui::ScopedClipboardWriter(ui::ClipboardBuffer::kCopyPaste)
              .WriteText(u"address menu fixture");
          if (!command("copy_url") ||
              !matches(clean.spec().c_str()) ||
              !WritePreference("clean_address_copies", 0)) {
            fail();
            return;
          }
          auto results = WorkspaceCommandResults(u">copy url");
          auto copy = std::ranges::find(results, "copy_url",
                                        &prototype::PaletteResult::command_id);
          if (copy == results.end()) {
            fail();
            return;
          }
          ActivateWorkspaceCommand(std::move(*copy));
          const auto& original = page->GetVisibleURL().spec();
          if (!matches(original.c_str()) || !command("address")) {
            fail();
            return;
          }
          ui::ScopedClipboardWriter(ui::ClipboardBuffer::kCopyPaste)
              .WriteText(u"address editor fixture");
          auto* editor = shell_->address_input();
          editor->SetText(base::UTF8ToUTF16(original));
          editor->SelectRange(gfx::Range(0, editor->GetText().size()));
          if (!native_menu_probe_.Run("copy:") || !matches(original.c_str())) {
            fail();
            return;
          }
          shell_->CloseOverlays(true);
          std::puts("Zitter Content copied cleaned and original page addresses");
        }
#endif
        if (!RunProbePaletteCommand(u"> find ctrl+f", "find")) {
          LOG(ERROR) << "Find could not dispatch from Command Center";
          fail();
          return;
        }
        if (!shell_->find_visible()) {
          LOG(ERROR) << "Find bar did not become visible";
          fail();
          return;
        }
        shell_->find_field_for_testing()->InsertText(
            u"needle", ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);
      }
      break;
    case 1:
      if ((ready = count_is(u"1"))) {
        auto* field = static_cast<views::View*>(shell_->find_field_for_testing());
        if (!field->OnKeyPressed(
                ui::KeyEvent(ui::EventType::kKeyPressed, ui::VKEY_RETURN, 0))) {
          fail();
          return;
        }
      }
      break;
    case 2:
      if ((ready = count_is(u"2"))) {
        SelectWorkspace(std::string(kWorkspaceIds[1]));
        if (active_index_ != 1 || shell_->find_visible()) {
          fail();
          return;
        }
        SelectWorkspace(std::string(kWorkspaceIds[0]));
        if (active_index_ != 0 || !command("find") || !shell_->find_visible() ||
            !shell_->find_field_for_testing()->GetText().empty() ||
            !command("hard_reload")) {
          fail();
          return;
        }
      }
      break;
    case 3:
      ready = (page->GetTitle() == u"Find and reload bypassed" ||
               page->GetTitle() == u"Find and reload editor ready") &&
              !shell_->find_visible();
      if (ready && native_menu_probe_)
        page->Focus();
      break;
    case 4:
      ready = !native_menu_probe_ ||
              page->GetTitle() == u"Find and reload editor ready";
      if (ready && native_menu_probe_ && !native_menu_probe_.Run("selectAll:")) {
        LOG(ERROR) << "Select All did not dispatch to the page";
        fail();
        return;
      }
      break;
    case 5:
      ready = !native_menu_probe_ ||
              page->GetTitle() == u"Find and reload selected";
      if (ready && native_menu_probe_) {
        if (!command("address") || !shell_->address_visible()) {
          fail();
          return;
        }
        auto* field = shell_->address_input();
        auto* input = field->input_client();
        const std::u16string original(field->GetText());
        constexpr std::u16string_view kEdit = u"native menu editor";
        field->SelectRange(gfx::Range(original.size()));
        if (!native_menu_probe_.Run("selectAll:") ||
            field->GetSelectedRange() != gfx::Range(0, original.size())) {
          LOG(ERROR) << "Select All did not select the address editor";
          fail();
          return;
        }
        input->InsertText(
            std::u16string(kEdit),
            ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);
        if (field->GetText() != kEdit || !native_menu_probe_.Run("undo:") ||
            field->GetText() != original || !native_menu_probe_.Run("redo:") ||
            field->GetText() != kEdit) {
          LOG(ERROR) << "Undo or Redo did not update the address editor";
          fail();
          return;
        }
        shell_->CloseOverlays(true);
        std::puts("Zitter Content native Edit menu passed");
      }
      if (ready) {
        const size_t fixture = selected_[0];
        SelectTab(kProbeSelection[0]);
        CloseTab(fixture);
      }
      break;
    case 6:
      if ((ready = shell_->tools_ready())) {
        auto* control = shell_->developer_control_for_testing();
        if (!control || !control->IsDrawn() || !command("developer")) {
          fail();
          return;
        }
        auto* menu = shell_->developer_menu_for_testing();
        const auto compare = menu->GetIndexOfCommandId(19004);
        if (auto* controller = views::MenuController::GetActiveInstance())
          controller->Cancel(views::MenuController::ExitType::kAll);
        if (!compare || !menu->IsEnabledAt(*compare)) {
          fail();
          return;
        }
        menu->ActivatedAt(*compare);
      }
      break;
    case 7:
      if ((ready = SplitShown() && !page->IsLoading() &&
                   page->GetLastCommittedURL() == session_probe_url_.Resolve(kProbePaths[0][1]))) {
        const auto& split = splits_[0];
        auto* original = tabs_[0][kProbeSelection[0]].view->web_contents();
        if (split.tabs.size() != 2 || split.tabs.front() != kProbeSelection[0] ||
            original == page || original->GetBrowserContext() != page->GetBrowserContext()) {
          fail();
          return;
        }
        CloseTab(selected_[0]);
        AddTab(0);
        SelectTab(tabs_[0].size() - 1);
        if (shell_->developer_control_for_testing()->IsDrawn()) {
          fail();
          return;
        }
        CloseTab(selected_[0]);
        SelectTab(kProbeSelection[0]);
        auto results = WorkspaceCommandResults(u">developer localhost");
        auto settings = std::ranges::find(results, "developer",
                                          &prototype::PaletteResult::command_id);
        if (settings == results.end()) {
          fail();
          return;
        }
        ActivateWorkspaceCommand(std::move(*settings));
        auto* menu = shell_->developer_menu_for_testing();
        const auto off = menu->GetIndexOfCommandId(19020);
        if (auto* controller = views::MenuController::GetActiveInstance())
          controller->Cancel(views::MenuController::ExitType::kAll);
        if (!off) {
          fail();
          return;
        }
        menu->ActivatedAt(*off);
      }
      break;
    case 8:
      ready = shell_->browser_tools().developer_mode == "off" &&
              shell_->appearance_saved();
      if (ready && (shell_->developer_control_for_testing()->IsDrawn() ||
                    !command("view_source") ||
                    web_contents() == page)) {
        fail();
        return;
      }
      break;
    case 9: {
      const auto* entry = page->GetController().GetLastCommittedEntry();
      ready = entry && entry->IsViewSourceMode() && !page->IsLoading();
      if (ready) {
        const auto url = session_probe_url_.Resolve(kProbePaths[0][kProbeSelection[0]]);
        auto* original = tabs_[0][kProbeSelection[0]].view->web_contents();
        const auto captured = CaptureWorkspaceSession(0);
        const auto& source = captured.FindList("tabs")->back().GetDict();
        if (!original || original == page ||
            original->GetBrowserContext() != page->GetBrowserContext() ||
            entry->GetURL() != url ||
            page->GetVisibleURL() != GURL("view-source:" + url.spec()) ||
            !source.FindString("url")->empty() ||
            RecordClosedTab(0, selected_[0]).url.is_valid() ||
            RunShortcut("view_source")) {
          fail();
          return;
        }
        CloseTab(selected_[0]);
        std::puts("Zitter Content opened source in the active profile");
        if (!command("toggle_window_fullscreen") || !widget_->IsFullscreen()) {
          fail();
          return;
        }
        web_contents()->GetPrimaryMainFrame()->ExecuteJavaScriptWithUserGestureForTests(
            u"document.documentElement.requestFullscreen().then(() => "
            u"document.title = 'Zitter fullscreen ready', () => "
            u"document.title = 'Zitter fullscreen rejected');",
            base::DoNothing(), content::ISOLATED_WORLD_ID_GLOBAL);
      }
      break;
    }
    case 10:
      ready = page->GetTitle() == u"Zitter fullscreen ready";
      if (ready) {
        auto* view = tabs_[0][selected_[0]].view.get();
        if (fullscreen_page_.get() != page || !page->IsFullscreen() ||
            view->bounds() != shell_->content_host()->GetLocalBounds()) {
          fail();
          return;
        }
        SelectWorkspace(std::string(kWorkspaceIds[1]));
        if (active_index_ != 1 || fullscreen_page_ || page->IsFullscreen() ||
            !widget_->IsFullscreen() || !command("toggle_window_fullscreen") ||
            widget_->IsFullscreen()) {
          fail();
          return;
        }
        std::puts("Zitter Content restored window fullscreen after page fullscreen");
        std::puts("Zitter Content developer menu and project split passed");
        std::puts("Zitter Content find and cache bypass passed");
        RequestClose();
        return;
      }
      break;
  }
  if (ready) {
    ++phase;
    deadline = base::TimeTicks::Now() + base::Seconds(8);
  } else if (base::TimeTicks::Now() >= deadline) {
    fail();
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckFindReloadProbe,
                                weak_factory_.GetWeakPtr(), phase, deadline),
      base::Milliseconds(25));
}

void WorkspaceLab::CheckNavigationProbe(
    size_t step,
    std::array<base::WeakPtr<content::WebContents>, 2> pages,
    base::TimeTicks deadline) {
  enum class Action {
    kNavigate, kSwitch, kBackButton, kBackShortcut, kForwardPalette,
    kForwardButton, kBackMenu, kForwardMenu, kFinish,
  };
  struct Step {
    size_t workspace;
    bool second_page;
    bool can_go_back;
    bool can_go_forward;
    Action action;
  };
  constexpr auto steps = std::to_array<Step>({
      {0, false, false, false, Action::kNavigate},
      {0, true, true, false, Action::kSwitch},
      {1, false, false, false, Action::kNavigate},
      {1, true, true, false, Action::kSwitch},
      {0, true, true, false, Action::kBackButton},
      {0, false, false, true, Action::kSwitch},
      {1, true, true, false, Action::kBackShortcut},
      {1, false, false, true, Action::kSwitch},
      {0, false, false, true, Action::kForwardPalette},
      {0, true, true, false, Action::kSwitch},
      {1, false, false, true, Action::kForwardButton},
      {1, true, true, false, Action::kBackMenu},
      {1, false, false, true, Action::kForwardMenu},
      {1, true, true, false, Action::kFinish},
  });
  const auto [workspace, second_page, can_go_back, can_go_forward, action] =
      steps[step];
  const auto expected_url = session_probe_url_.Resolve(
      second_page ? kNavigationProbePaths[workspace]
                  : kProbePaths[workspace][kProbeSelection[workspace]]);
  const auto expected_title =
      second_page ? kNavigationProbeTitle : kProbeTitles[workspace];
  auto* page = web_contents();
  auto* back = shell_->navigation_button_for_testing(prototype::PageCommand::kBack);
  auto* forward =
      shell_->navigation_button_for_testing(prototype::PageCommand::kForward);
  if (!page || page != pages[workspace].get() || active_index_ != workspace ||
      page->GetBrowserContext() != contexts_[workspace].get()) {
    LOG(ERROR) << "Navigation changed the active profile or WebContents at step "
               << step;
    Finish(false);
    return;
  }
  auto& controller = page->GetController();
  if (page->IsLoading() || page->GetLastCommittedURL() != expected_url ||
      page->GetTitle() != expected_title ||
      controller.CanGoBack() != can_go_back ||
      controller.CanGoForward() != can_go_forward ||
      back->GetEnabled() != can_go_back || forward->GetEnabled() != can_go_forward) {
    if (base::TimeTicks::Now() >= deadline) {
      LOG(ERROR) << "Navigation state did not settle at step " << step
                 << ", URL=" << page->GetLastCommittedURL()
                 << ", title=" << base::UTF16ToUTF8(page->GetTitle())
                 << ", back=" << controller.CanGoBack()
                 << ", forward=" << controller.CanGoForward();
      Finish(false);
      return;
    }
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, base::BindOnce(&WorkspaceLab::CheckNavigationProbe,
                                  weak_factory_.GetWeakPtr(), step,
                                  std::move(pages), deadline),
        base::Milliseconds(25));
    return;
  }

  bool sent = true;
  switch (action) {
    case Action::kNavigate:
      sent = Navigate(session_probe_url_.Resolve(kNavigationProbePaths[workspace]),
                      /*new_tab=*/false);
      break;
    case Action::kSwitch:
      SelectWorkspace(std::string(kWorkspaceIds[1 - workspace]));
      break;
    case Action::kBackButton:
    case Action::kForwardButton: {
      ui::AXActionData activate;
      activate.action = ax::mojom::Action::kDoDefault;
      auto* button = action == Action::kBackButton ? back : forward;
      sent = button->HandleAccessibleAction(activate);
      break;
    }
    case Action::kBackShortcut:
      sent = RunShortcut("back");
      break;
    case Action::kForwardPalette:
      sent = SubmitProbeCommand(u"> forward", "forward");
      break;
    case Action::kBackMenu:
    case Action::kForwardMenu: {
      const bool going_back = action == Action::kBackMenu;
      const char* command = going_back ? "back" : "forward";
      const char* disabled = going_back ? "forward" : "back";
      sent = native_menu_probe_ ? !native_menu_probe_.Run(disabled) &&
                                      native_menu_probe_.Run(command)
                                : RunShortcut(command);
      break;
    }
    case Action::kFinish:
      if (native_menu_probe_)
        std::puts("Zitter Content validated native menu navigation");
      std::puts("Zitter Content preserved live back and forward across workspace switches");
      RequestClose();
      return;
  }
  if (!sent) {
    LOG(ERROR) << "Navigation action was not accepted at step " << step;
    Finish(false);
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckNavigationProbe,
                               weak_factory_.GetWeakPtr(), step + 1,
                               std::move(pages),
                               base::TimeTicks::Now() + base::Seconds(5)));
}

void WorkspaceLab::CheckClosedTabProbe(size_t phase, base::TimeTicks deadline) {
  const auto target = session_probe_url_.Resolve(kClosedTabProbePath);
  if (active_index_ != 0 || web_contents()->GetLastCommittedURL() != target ||
      web_contents()->GetTitle() != kClosedTabProbeTitle ||
      web_contents()->GetBrowserContext() != contexts_[0].get()) {
    if (base::TimeTicks::Now() >= deadline) {
      LOG(ERROR) << "Recently closed Work tab did not load in phase " << phase;
      Finish(false);
      return;
    }
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&WorkspaceLab::CheckClosedTabProbe,
                       weak_factory_.GetWeakPtr(), phase, deadline),
        base::Milliseconds(25));
    return;
  }

  CloseTab(selected_[0]);
  if (web_contents()->GetLastCommittedURL() == target || LastClosedTab() != 0) {
    LOG(ERROR) << "Closing the Work fixture did not record it in phase " << phase;
    Finish(false);
    return;
  }

  if (phase == 0) {
    SelectWorkspace(std::string(kWorkspaceIds[1]));
    if (active_index_ != 1 || RunShortcut("reopen") ||
        (native_menu_probe_ && native_menu_probe_.Run("reopen")) ||
        !OpenHistory()) {
      LOG(ERROR) << "Could not check recently closed tabs in Personal";
      Finish(false);
      return;
    }
    history_panel_->SetShowClosed(true);
    history_panel_->widget()->LayoutRootViewIfNecessary();
    if (history_panel_->has_visible_visit_title_for_testing(kClosedTabProbeTitle)) {
      LOG(ERROR) << "Personal history showed a closed Work tab";
      Finish(false);
      return;
    }
    SelectWorkspace(std::string(kWorkspaceIds[0]));
    if (active_index_ != 0 || !OpenHistory()) {
      LOG(ERROR) << "Could not open Work history";
      Finish(false);
      return;
    }
    history_panel_->SetShowClosed(true);
    history_panel_->widget()->LayoutRootViewIfNecessary();
    if (!history_panel_->has_visible_visit_title_for_testing(kClosedTabProbeTitle)) {
      LOG(ERROR) << "Work history did not show the recently closed tab";
      Finish(false);
      return;
    }
    history_panel_->ActivateForTesting(0);
  } else if (phase == 1) {
    if (!RunShortcut("reopen")) {
      LOG(ERROR) << "The reopen shortcut did not restore the Work tab";
      Finish(false);
      return;
    }
  } else if (phase == 2) {
    if (!SubmitProbeCommand(u"> reopen closed tab", "reopen")) {
      LOG(ERROR) << "Command Center did not offer to reopen the Work tab";
      Finish(false);
      return;
    }
  } else if (phase == 3 && native_menu_probe_) {
    if (!native_menu_probe_.Run("reopen")) {
      LOG(ERROR) << "The native menu did not reopen the Work tab";
      Finish(false);
      return;
    }
  } else {
    SelectWorkspace(std::string(kWorkspaceIds[1]));
    if (native_menu_probe_)
      std::puts("Zitter Content validated native menu closed tab recovery");
    std::puts("Zitter Content reopened closed tabs in their own workspace");
    RequestClose();
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckClosedTabProbe,
                               weak_factory_.GetWeakPtr(), phase + 1,
                               base::TimeTicks::Now() + base::Seconds(5)));
}

void WorkspaceLab::CheckSleepProbeDialog(bool confirm, bool answered,
                                           base::TimeTicks deadline) {
  if (base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "Timed out waiting for the workspace sleep confirmation";
    Finish(false);
    return;
  }
  if (!answered) {
    auto* page = web_contents();
    auto* dialogs = prototype::PageDialogs::FromWebContents(page);
    if (dialogs && dialogs->leave_dialog_open()) {
      answered = dialogs->HandleJavaScriptDialog(page, confirm, nullptr);
      if (!answered) {
        Finish(false);
        return;
      }
    }
  } else if (!before_unload_batch_.pending()) {
    if (confirm) {
      std::puts("Zitter Content honored both beforeunload responses during workspace sleep");
      FinishSleepProbe();
      return;
    }
    if (active_index_ != 0 || sleeping_[0] || !web_contents() ||
        !RunShortcut("sleep") || !before_unload_batch_.pending()) {
      LOG(ERROR) << "Declining the sleep confirmation did not preserve the workspace";
      Finish(false);
      return;
    }
    confirm = true;
    answered = false;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckSleepProbeDialog,
                               weak_factory_.GetWeakPtr(), confirm, answered, deadline),
      base::Milliseconds(25));
}

void WorkspaceLab::FinishSleepProbe() {
  if (active_index_ != 1 || !sleeping_[0] || sleeping_[1] ||
      std::ranges::any_of(tabs_[0], [](const Tab& tab) {
        return tab.view->web_contents() != nullptr;
      })) {
    LOG(ERROR) << "Workspace sleep did not release the Work pages";
    Finish(false);
    return;
  }
  std::puts("Zitter Content woke and released a migrated workspace");
  RequestClose();
}

bool WorkspaceLab::ExercisePinEdits() {
  auto* page = web_contents();
  const auto panes = splits_[0].tabs;
  if (!PinTab(0, "folder-work", "open-work"))
    return false;
  const auto id = tabs_[0][0].pin;
  const auto first_child = std::ranges::find(pins_[0], "folder-work",
                                            &prototype::PinnedItem::parent_id);
  if (first_child == pins_[0].end() || first_child->id != id ||
      !MovePin(id, "@profile", "") ||
      !std::ranges::contains(profile_favorites_["work"], id, &prototype::PinnedItem::id) ||
      !Unpin(id, 2) || selected_[0] != 0 || web_contents() != page ||
      !tabs_[0][1].pin.empty()) {
    return false;
  }
  auto moved_panes = panes;
  for (size_t& pane : moved_panes)
    pane = 1 - pane;
  return splits_[0].tabs == moved_panes && MoveTab(1, 0) &&
         splits_[0].tabs == panes && selected_[0] == 1 && web_contents() == page &&
         ExerciseFolderEdits();
}

bool WorkspaceLab::ExerciseFolderEdits() {
  const auto& pins = pins_[0];
  if (!CreatePinnedFolder("work", "folder-work", "Temporary parent"))
    return false;
  const std::string parent = pins.back().id;
  if (!CreatePinnedFolder("work", parent, "Temporary child"))
    return false;
  const std::string child = pins.back().id;
  if (MovePin(parent, child, "") || MovePin(parent, "@profile", "") ||
      !MovePin(parent, "", "folder-work") ||
      !MovePin("closed-work", child, "") ||
      !RemovePinnedFolder("work", parent, false)) {
    return false;
  }
  const auto promoted = std::ranges::find(pins, child, &prototype::PinnedItem::id);
  const auto closed = std::ranges::find(pins, "closed-work", &prototype::PinnedItem::id);
  if (promoted == pins.end() || !promoted->parent_id.empty() ||
      closed == pins.end() || closed->parent_id != child ||
      !MovePin("closed-work", "folder-work", "") ||
      !RemovePinnedFolder("work", child, true)) {
    return false;
  }

  if (!CreatePinnedFolder("work", "folder-work", "Temporary deletion"))
    return false;
  const std::string deletion = pins.back().id;
  auto* active_page = web_contents();
  auto* pane = tabs_[0][0].view->web_contents();
  const auto split = splits_[0].tabs;
  return PinTab(0, deletion, "") && RemovePinnedFolder("work", deletion, true) &&
         tabs_[0][0].pin.empty() && tabs_[0][0].view->web_contents() == pane &&
         selected_[0] == 1 && web_contents() == active_page && splits_[0].tabs == split;
}

ExtensionNavigationProbe* WorkspaceLab::ExtensionProbeForProfile(
    const ProfileContext& profile) const {
  for (const auto& probe : extension_probes_) {
    if (probe && probe->OwnsProfile(profile))
      return probe.get();
  }
  return nullptr;
}

ManagedExtension* WorkspaceLab::ManagedExtensionForProfile(
    const ProfileContext& profile, std::string_view id) const {
  for (const auto& manager : managed_extensions_) {
    if (manager->OwnsProfile(profile) && manager->id() == id)
      return manager.get();
  }
  return nullptr;
}

void WorkspaceLab::StartManagedExtensions(size_t owner) {
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          "zitter-content-extension-offscreen-clipboard-probe")) {
#if BUILDFLAG(IS_MAC)
    auto* install = reinterpret_cast<bool (*)()>(
        dlsym(RTLD_DEFAULT, "ZitterInstallClipboardFixture"));
    if (!install || !install()) {
      LOG(ERROR) << "Offscreen clipboard fixture is unavailable";
      Finish(false);
      return;
    }
#else
    Finish(false);
    return;
#endif
  }
  for (size_t offset = 0; offset < contexts_.size(); ++offset) {
    auto& context = *contexts_[(owner + offset) % contexts_.size()];
    if (auto* prefs = extensions::ExtensionPrefs::Get(&context)) {
      extensions::ExtensionIdList pinned;
      for (const auto& extension : GetConfiguredExtensions(context)) {
        bool value = false;
        if (prefs->ReadPrefAsBoolean(extension->id(), kManagerPinPref, &value) && value)
          pinned.push_back(extension->id());
      }
      prefs->SetPinnedExtensions(pinned);
    }
    for (const auto& extension : GetConfiguredExtensions(context)) {
      managed_extensions_.push_back(std::make_unique<ManagedExtension>(
          context, extension->id(),
          base::BindRepeating(&WorkspaceLab::RefreshShell,
                              weak_factory_.GetWeakPtr())));
      if (!managed_extensions_.back()->Start()) {
        Finish(false);
        return;
      }
    }
  }
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(kExtensionInstallUiProbe)) {
    const size_t profile = ExtensionFixtureProfileIndex().value();
    SelectWorkspace(model_.workspaces()[profile].id);
    shell_->OpenGeneralSettings();
    auto* search = shell_->preferences_search();
    search->input_client()->InsertText(
        prototype::T(u"Zainstaluj rozszerzenie"),
        ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);
    auto* button = shell_->install_extension_for_testing();
    ui::AXActionData action;
    action.action = ax::mojom::Action::kDoDefault;
    if (!button || !button->IsDrawn()) {
      LOG(ERROR) << "The extension install action is unavailable in Settings";
      Finish(false);
      return;
    }
    button->GetWidget()->LayoutRootViewIfNecessary();
    button->ScrollViewToVisible();
    if (!button->HandleAccessibleAction(action) || !extension_install_) {
      LOG(ERROR) << "The extension install action did not open a package";
      Finish(false);
    }
    return;
  }
  if (GetExtensionFixtureStateAction() == ExtensionFixtureStateAction::kManageSmoke) {
    if (managed_extensions_.size() != 2) {
      LOG(ERROR) << "The managed extension check requires two trusted installations";
      Finish(false);
      return;
    }
    if (!managed_extensions_[1]->selection() && managed_extensions_[0]->selection()) {
      std::swap(managed_extensions_[0], managed_extensions_[1]);
    }
    CheckManagedExtensionsForProbe(ManagedProbePhase::kStart,
                                   base::TimeTicks::Now() + base::Seconds(15));
  }
}

bool WorkspaceLab::PrepareManagedExtensionCacheForProbe(ProfileContext& owner,
                                                        ProfileContext& peer) {
  const auto page_url = session_probe_url_.Resolve("/extension-cache");
  if (!managed_cache_probe_host_) {
    managed_cache_probe_host_ = std::make_unique<views::View>();
    size_t view_index = 0;
    for (auto* profile : {&owner, &peer}) {
      for (size_t workspace = 0; workspace < model_.workspaces().size(); ++workspace) {
        if (WorkspaceProfile(workspace) == profile) {
          auto tab = MakeTab(workspace, *managed_cache_probe_host_);
          managed_cache_views_[view_index++] = tab.view;
          tab.view->LoadInitialURL(page_url);
          break;
        }
      }
    }
    CHECK_EQ(view_index, managed_cache_views_.size());
    return false;
  }

  if (!managed_cache_documents_[0].AsRenderFrameHostIfValid()) {
    if (!std::ranges::all_of(managed_cache_views_, [&](auto view) {
          auto* page = view->web_contents();
          return page->GetLastCommittedURL() == page_url && !page->IsLoading();
        })) {
      return false;
    }
    if (base::CommandLine::ForCurrentProcess()->HasSwitch("zitter-content-extension-dnr-probe")) {
      const auto& owner_title = managed_cache_views_[0]->web_contents()
                                    ->GetController().GetLastCommittedEntry()->GetTitle();
      const auto& peer_title = managed_cache_views_[1]->web_contents()
                                   ->GetController().GetLastCommittedEntry()->GetTitle();
      if (owner_title.empty() || peer_title.empty())
        return false;

      const bool recovered_allow_rule = base::CommandLine::ForCurrentProcess()->HasSwitch(
          "zitter-content-extension-dnr-restore-probe") &&
          owner.GetPath().BaseName().AsUTF8Unsafe() == "workspace-personal";
      std::u16string_view expected_owner = kUnfilteredDnrProbeTitle;
      if (managed_extensions_.front()->enabled()) {
        expected_owner = recovered_allow_rule
            ? u"block,allow,block,allow,block,redirected,request/response,block,allow,"
              u"original,original/original"
            : u"block,block,block,allow,block,redirected,request/response,block,allow,"
              u"original,original/original";
      }
      if (owner_title != expected_owner || peer_title != kUnfilteredDnrProbeTitle) {
        LOG(ERROR) << "Unexpected DNR results. Owning profile: " << owner_title
                   << ", peer: " << peer_title << ", expected owner: " << expected_owner;
        Finish(false);
        return false;
      }
      std::puts("Zitter Content verified profile-scoped static network rules");
    }
    const GURL away_url(base::StrCat(
        {"http://localhost:", session_probe_url_.port(), "/extension-cache-away"}));
    for (size_t index = 0; index < managed_cache_views_.size(); ++index) {
      auto* page = managed_cache_views_[index]->web_contents();
      managed_cache_documents_[index] = page->GetPrimaryMainFrame()->GetWeakDocumentPtr();
      page->GetController().LoadURLWithParams(
          content::NavigationController::LoadURLParams(away_url));
    }
    return false;
  }

  return std::ranges::all_of(managed_cache_documents_, [](const auto& document) {
    auto* frame = document.AsRenderFrameHostIfValid();
    return frame && frame->IsInLifecycleState(
                        content::RenderFrameHost::LifecycleState::kInBackForwardCache);
  });
}

bool WorkspaceLab::CheckExtensionRuleApiForProbe(ManagedExtension& owner) {
  if (extension_rule_api_probe_complete_)
    return true;
  if (!extension_rule_api_probe_page_) {
    const bool restore = base::CommandLine::ForCurrentProcess()->HasSwitch(
        "zitter-content-extension-dnr-restore-probe");
    const GURL url("chrome-extension://" + owner.id() + "/probe.html" +
                   (restore ? "#dnr-restore" : "#dnr-install"));
    extension_rule_api_probe_page_ = content::WebContents::Create(
        content::WebContents::CreateParams(&owner.profile()));
    extension_rule_api_probe_page_->GetController().LoadURLWithParams(
        content::NavigationController::LoadURLParams(url));
    return false;
  }
  const auto& title = extension_rule_api_probe_page_->GetTitle();
  if (title == u"Extension DNR API verified") {
    extension_rule_api_probe_complete_ = true;
    extension_rule_api_probe_page_.reset();
    std::puts("Zitter Content verified static, dynamic and session rule APIs");
    return true;
  }
  if (title.starts_with(u"DNR fixture error:")) {
    LOG(ERROR) << title;
    Finish(false);
  }
  return false;
}

bool WorkspaceLab::CheckExtensionDnrNavigationForProbe(ProfileContext& owner,
                                                       ProfileContext& peer) {
  if (extension_dnr_navigation_probe_complete_)
    return true;
  if (!extension_dnr_navigation_probe_) {
    auto probe = std::make_unique<ExtensionDnrNavigationProbe>();
    probe->private_profile = std::make_unique<ProfileContext>(
        owner.GetPath().DirName().AppendASCII("extension-dnr-private-probe"),
        ProfileContext::Mode::kPrivate);
    const auto target = session_probe_url_.Resolve("/extension-dnr-navigation");
    const auto redirect = session_probe_url_.Resolve("/extension-dnr-navigation-redirect");
    const auto upgrade = session_probe_url_.Resolve("/extension-dnr-upgrade");
    GURL::Replacements https;
    https.SetSchemeStr("https");
    const auto upgraded = upgrade.ReplaceComponents(https);
    for (auto* profile : {&owner, &peer, probe->private_profile.get()}) {
      const bool owns_rules = profile == &owner;
      for (const auto& start : {target, redirect}) {
        probe->requests.push_back(
            std::make_unique<ExtensionDnrNavigationProbe::Request>(
                *profile, start, target,
                owns_rules ? net::ERR_BLOCKED_BY_CLIENT : net::OK));
      }
      probe->requests.push_back(
          std::make_unique<ExtensionDnrNavigationProbe::Request>(
              *profile, upgrade, owns_rules ? upgraded : upgrade,
              owns_rules ? net::ERR_CERT_AUTHORITY_INVALID : net::OK));
    }
    probe->private_subresources = content::WebContents::Create(
        content::WebContents::CreateParams(probe->private_profile.get()));
    probe->private_subresources->GetController().LoadURLWithParams(
        content::NavigationController::LoadURLParams(
            session_probe_url_.Resolve("/extension-cache")));
    extension_dnr_navigation_probe_ = std::move(probe);
  }
  for (const auto& request : extension_dnr_navigation_probe_->requests) {
    const auto result = request->result();
    if (!result)
      return false;
    if (!*result) {
      Finish(false);
      return false;
    }
  }
  const auto* private_entry = extension_dnr_navigation_probe_->private_subresources
                                  ->GetController().GetLastCommittedEntry();
  if (!private_entry || private_entry->GetTitle().empty())
    return false;
  if (private_entry->GetTitle() != kUnfilteredDnrProbeTitle) {
    LOG(ERROR) << "Extension network rules affected private subresources";
    Finish(false);
    return false;
  }
  extension_dnr_navigation_probe_.reset();
  extension_dnr_navigation_probe_complete_ = true;
  std::puts("Zitter Content verified DNR navigations and private subresource isolation");
  return true;
}

bool WorkspaceLab::CheckExtensionRuleCacheForProbe(ManagedExtension& owner) {
  static constexpr std::array<std::string_view, 4> kMutations = {
      "dynamic", "session", "static", "enabled"};
  if (extension_rule_cache_probe_step_ == kMutations.size())
    return true;
  if (!extension_rule_cache_probe_page_) {
    const GURL url(base::StrCat({"chrome-extension://", owner.id(),
                                "/probe.html#dnr-cache-",
                                kMutations[extension_rule_cache_probe_step_]}));
    extension_rule_cache_probe_page_ = content::WebContents::Create(
        content::WebContents::CreateParams(&owner.profile()));
    extension_rule_cache_probe_page_->GetController().LoadURLWithParams(
        content::NavigationController::LoadURLParams(url));
    return false;
  }

  const auto& title = extension_rule_cache_probe_page_->GetTitle();
  if (title.starts_with(u"DNR fixture error:")) {
    LOG(ERROR) << title;
    Finish(false);
    return false;
  }
  if (title != u"Extension DNR cache mutation verified")
    return false;
  auto* peer = managed_cache_documents_[1].AsRenderFrameHostIfValid();
  if (!peer || !peer->IsInLifecycleState(
                   content::RenderFrameHost::LifecycleState::kInBackForwardCache)) {
    LOG(ERROR) << "A ruleset update evicted another profile's history entry";
    Finish(false);
    return false;
  }
  if (managed_cache_documents_[0].AsRenderFrameHostIfValid())
    return false;

  std::puts(base::StrCat({"Zitter Content isolated history eviction for ",
                          kMutations[extension_rule_cache_probe_step_], " rules"})
                .c_str());
  ++extension_rule_cache_probe_step_;
  extension_rule_cache_probe_page_.reset();
  managed_cache_views_ = {};
  managed_cache_documents_ = {};
  managed_cache_probe_host_.reset();
  return true;
}

bool WorkspaceLab::CheckManagedOffscreenForProbe(ManagedExtension& owner,
                                                  ManagedExtension& peer) {
  const std::array<ManagedExtension*, 2> extensions{&owner, &peer};
  for (size_t index = 0; index < extensions.size(); ++index) {
    auto& extension = *extensions[index];
    const bool has_document =
        WorkspaceExtensionHasOffscreenForTesting(extension.profile(), extension.id());
    if (!extension.enabled()) {
      if (has_document) {
        LOG(ERROR) << "A disabled extension still owns an offscreen document";
        Finish(false);
        return false;
      }
      offscreen_probe_complete_[index] = false;
      continue;
    }
    if (offscreen_probe_complete_[index]) {
      if (!has_document) {
        LOG(ERROR) << "The active profile lost its offscreen document";
        Finish(false);
        return false;
      }
      continue;
    }

    auto& page = offscreen_probe_pages_[index];
    if (!page) {
      auto* loader = extensions::ExtensionSystem::Get(&extension.profile())
                         ->user_script_manager()
                         ->GetUserScriptLoaderForExtension(extension.id());
      if (!loader->initial_load_complete())
        return false;
      const auto allowed = extension.UserScriptsAllowed();
      if (!allowed || (index == 1 && *allowed) ||
          (index == 0 && !*allowed &&
           !ToggleExtensionFromSettingsForProbe(extension.profile(), true,
                                                extension.id(), true))) {
        LOG(ERROR) << "Settings did not scope user script approval to its profile";
        Finish(false);
        return false;
      }
      if (index == 0)
        ConfigureWorkspaceExtensionIdleProbeForTesting(extension.profile());
      const char* mode = index == 1 ? "denied" : *allowed ? "restore" : "fresh";
      const std::string_view clipboard =
          base::CommandLine::ForCurrentProcess()->HasSwitch(
              "zitter-content-extension-offscreen-clipboard-probe")
              ? "clipboard-" : "";
      const std::string_view privacy =
          base::CommandLine::ForCurrentProcess()->HasSwitch(
              "zitter-content-extension-privacy-restore-probe")
              ? "privacy-restart-" : "";
      page = content::WebContents::Create(
          content::WebContents::CreateParams(&extension.profile()));
      extensions::SetViewType(page.get(), extensions::mojom::ViewType::kTabContents);
      page->GetController().LoadURLWithParams(content::NavigationController::LoadURLParams(
          GURL(base::StrCat({"chrome-extension://", extension.id(),
                             "/probe.html#offscreen-probe-", clipboard, privacy, mode}))));
      return false;
    }
    const auto& title = page->GetTitle();
    if (title.starts_with(u"Offscreen fixture error:")) {
      LOG(ERROR) << title;
      Finish(false);
      return false;
    }
    if (title != u"Extension offscreen verified" || !has_document)
      return false;
    offscreen_probe_complete_[index] = true;
    page.reset();
    std::puts("Zitter Content verified the offscreen document in its owning profile");
  }
#if BUILDFLAG(IS_MAC)
  if (owner.enabled() && base::CommandLine::ForCurrentProcess()->HasSwitch(
                             "zitter-content-extension-offscreen-clipboard-probe")) {
    auto* matches = reinterpret_cast<bool (*)(const char*)>(
        dlsym(RTLD_DEFAULT, "ZitterClipboardFixtureMatches"));
    if (!matches || !matches("zitter-offscreen-clipboard-fixture")) {
      LOG(ERROR) << "Offscreen clipboard changed outside the authorized document";
      Finish(false);
      return false;
    }
    std::puts("Zitter Content verified the isolated offscreen pasteboard");
  }
#endif
  return true;
}

bool WorkspaceLab::CheckUserScriptExecutionForProbe(ManagedExtension& owner,
                                                     ManagedExtension& peer) {
  if (user_script_probe_phase_ == UserScriptProbePhase::kDone || !owner.enabled())
    return true;

  const bool approved = user_script_probe_phase_ == UserScriptProbePhase::kApproved;
  if (!user_script_probe_pages_[0]) {
    if (!user_script_probe_private_) {
      user_script_probe_private_ = std::make_unique<ProfileContext>(
          owner.profile().GetPath().DirName().AppendASCII("extension-user-script-private"),
          ProfileContext::Mode::kPrivate);
    }
    const std::array profiles{&owner.profile(), &peer.profile(),
                              user_script_probe_private_.get()};
    const auto url = session_probe_url_.Resolve("/extension-user-script");
    for (size_t i = 0; i < profiles.size(); ++i) {
      auto& page = user_script_probe_pages_[i];
      page = content::WebContents::Create(content::WebContents::CreateParams(profiles[i]));
      page->GetController().LoadURLWithParams(content::NavigationController::LoadURLParams(url));
    }
    return false;
  }

  for (size_t i = 0; i < user_script_probe_pages_.size(); ++i) {
    const auto& page = user_script_probe_pages_[i];
    const auto& title = page->GetTitle();
    if (page->IsLoading() || title.empty() || title == u"User script fixture")
      return false;
    const std::u16string_view expected =
        approved && i == 0 ? u"User script executed" : u"User script absent";
    if (title != expected) {
      LOG(ERROR) << "Unexpected user script result in context " << i << ": " << title;
      Finish(false);
      return false;
    }
  }

  user_script_probe_pages_ = {};
  if (!ToggleExtensionFromSettingsForProbe(owner.profile(), !approved, owner.id(), true)) {
    Finish(false);
    return false;
  }
  if (approved) {
    user_script_probe_phase_ = UserScriptProbePhase::kRevoked;
    return false;
  }

  user_script_probe_private_.reset();
  user_script_probe_phase_ = UserScriptProbePhase::kDone;
  std::puts("Zitter Content verified user script execution and revocation");
  return true;
}

bool WorkspaceLab::CheckManagedHttpAuthForProbe(ManagedExtension& owner,
                                               ManagedExtension& peer) {
  if (extension_http_auth_verified_ || !owner.enabled())
    return true;

  if (!extension_http_auth_pages_[0]) {
    extension_http_auth_private_ = std::make_unique<ProfileContext>(
        owner.profile().GetPath().DirName().AppendASCII("extension-auth-private"),
        ProfileContext::Mode::kPrivate);
    const auto target = session_probe_url_.Resolve("/extension-http-auth");
    GURL::Replacements other_host;
    other_host.SetHostStr("localhost");
    const auto denied = target.ReplaceComponents(other_host);
    const std::array profiles{&owner.profile(), &owner.profile(), &peer.profile(),
                              extension_http_auth_private_.get()};
    for (size_t index = 0; index < profiles.size(); ++index) {
      auto& page = extension_http_auth_pages_[index];
      page = content::WebContents::Create(content::WebContents::CreateParams(profiles[index]));
      page->GetController().LoadURLWithParams(
          content::NavigationController::LoadURLParams(index == 1 ? denied : target));
    }
    return false;
  }

  for (size_t index = 0; index < extension_http_auth_pages_.size(); ++index) {
    const auto& page = extension_http_auth_pages_[index];
    const auto& title = page->GetTitle();
    if (page->IsLoading() || page->GetLastCommittedURL().is_empty())
      return false;
    if ((title == u"Extension auth accepted") != (index == 0)) {
      LOG(ERROR) << "Unexpected extension authentication in context " << index
                 << ": " << page->GetLastCommittedURL() << " (" << title << ")";
      Finish(false);
      return false;
    }
  }

  extension_http_auth_pages_ = {};
  extension_http_auth_private_.reset();
  extension_http_auth_verified_ = true;
  std::puts("Zitter Content scoped extension HTTP credentials to the authorized site and profile");
  return true;
}

bool WorkspaceLab::CheckManagedExtensionCacheEvictionForProbe() {
  if (managed_cache_documents_[0].AsRenderFrameHostIfValid())
    return false;
  auto* peer_frame = managed_cache_documents_[1].AsRenderFrameHostIfValid();
  if (!peer_frame || !peer_frame->IsInLifecycleState(
                         content::RenderFrameHost::LifecycleState::kInBackForwardCache)) {
    LOG(ERROR) << "An extension change evicted a different profile's history";
    Finish(false);
    return false;
  }
  std::puts("Zitter Content evicted the owning profile's extension history cache");
  managed_cache_views_ = {};
  managed_cache_documents_ = {};
  managed_cache_probe_host_.reset();
  managed_cache_eviction_pending_ = false;
  return true;
}

void WorkspaceLab::CheckManagedExtensionsForProbe(ManagedProbePhase phase,
                                                  base::TimeTicks deadline) {
  auto& owner = *managed_extensions_[0];
  auto& peer = *managed_extensions_[1];
  const auto peer_worker = peer.RunningWorkerForTesting();
  if (owner.failed() || peer.failed() || base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "Managed extensions did not finish transition "
               << static_cast<int>(phase);
    Finish(false);
    return;
  }
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          "zitter-content-extension-notification-delivery-probe")) {
    if (owner.editable() && peer.editable() && CheckManagedOffscreenForProbe(owner, peer)) {
      std::puts("Zitter Content verified profile-scoped notification delivery and actions");
      Finish(true);
      return;
    }
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, base::BindOnce(&WorkspaceLab::CheckManagedExtensionsForProbe,
                                  weak_factory_.GetWeakPtr(), phase, deadline),
        base::Milliseconds(50));
    return;
  }
  const bool verify_cache =
      &owner.profile() != &peer.profile() && session_probe_url_.SchemeIs("http") &&
      session_probe_url_.host() == "127.0.0.1" && session_probe_url_.has_port();
  const bool verify_dnr = base::CommandLine::ForCurrentProcess()->HasSwitch(
      "zitter-content-extension-dnr-probe") && owner.enabled();
  if (owner.editable() && peer.editable() && peer_worker &&
      (!base::CommandLine::ForCurrentProcess()->HasSwitch("zitter-content-extension-offscreen-probe") ||
       CheckManagedOffscreenForProbe(owner, peer)) &&
      (!base::CommandLine::ForCurrentProcess()->HasSwitch(
           "zitter-content-extension-webrequest-auth-probe") ||
       CheckManagedHttpAuthForProbe(owner, peer)) &&
      (!base::CommandLine::ForCurrentProcess()->HasSwitch("zitter-content-extension-offscreen-probe") ||
       CheckUserScriptExecutionForProbe(owner, peer)) &&
      (!verify_dnr ||
       (WorkspaceExtensionHasRulesForTesting(owner.profile(), owner.id()) &&
        CheckExtensionRuleApiForProbe(owner) &&
        CheckExtensionDnrNavigationForProbe(owner.profile(), peer.profile()))) &&
      (!managed_cache_eviction_pending_ || CheckManagedExtensionCacheEvictionForProbe()) &&
      (!extension_rule_cache_probe_page_ || CheckExtensionRuleCacheForProbe(owner)) &&
      (!verify_cache || phase == ManagedProbePhase::kDone ||
       (PrepareManagedExtensionCacheForProbe(owner.profile(), peer.profile()) &&
        (!verify_dnr || CheckExtensionRuleCacheForProbe(owner))))) {
    const auto worker = owner.RunningWorkerForTesting();
    if (phase == ManagedProbePhase::kStart) {
      managed_peer_worker_ = peer_worker;
      const auto rule_prefs = base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
          "zitter-content-extension-rule-prefs-probe");
      if (!rule_prefs.empty()) {
        const bool restore = rule_prefs == "restore";
        if (!VerifyExtensionRulePreferencesForTesting(owner.profile(), owner.id(), restore) ||
            !VerifyExtensionRulePreferencesForTesting(peer.profile(), peer.id(), restore)) {
          LOG(ERROR) << "Content extension ruleset preferences failed profile validation";
          Finish(false);
          return;
        }
        std::puts(restore ? "Zitter Content restored profile-specific ruleset preferences"
                          : "Zitter Content recorded profile-specific ruleset preferences");
      }
    }
    const bool expects_enabled =
        phase == ManagedProbePhase::kStart ? owner.enabled()
        : phase == ManagedProbePhase::kInitialEnable ||
              phase == ManagedProbePhase::kReenabled;
    if (owner.enabled() != expects_enabled ||
        worker.has_value() != expects_enabled ||
        (worker && worker == peer_worker) ||
        (phase == ManagedProbePhase::kReenabled && worker == managed_owner_worker_) ||
        peer_worker != managed_peer_worker_) {
      LOG(ERROR) << "A managed extension used the wrong worker at phase "
                 << static_cast<int>(phase);
      Finish(false);
      return;
    }
    if (phase == ManagedProbePhase::kDone) {
      std::puts("Zitter Content managed extension toggled without restarting its peer");
      SelectWorkspace(std::string(kWorkspaceIds[1]));
      RequestClose();
      return;
    }
    if (phase == ManagedProbePhase::kReenabled &&
        base::CommandLine::ForCurrentProcess()->HasSwitch(
            "zitter-content-extension-uninstall-probe")) {
      const GURL extension_page("chrome-extension://" + owner.id() + "/probe.html");
      if (!managed_uninstall_probe_page_) {
        managed_uninstall_probe_page_ = content::WebContents::Create(
            content::WebContents::CreateParams(&owner.profile()));
        managed_uninstall_probe_page_->GetController().LoadURLWithParams(
            content::NavigationController::LoadURLParams(extension_page));
      }
      auto* page = managed_uninstall_probe_page_.get();
      if (page->GetLastCommittedURL() != extension_page ||
          page->GetPrimaryMainFrame()->GetLastCommittedOrigin() !=
              url::Origin::Create(extension_page) ||
          !page->GetPrimaryMainFrame()->IsRenderFrameLive()) {
        if (base::TimeTicks::Now() >= deadline) {
          LOG(ERROR) << "Could not open the extension page before uninstall";
          Finish(false);
          return;
        }
        base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
            FROM_HERE, base::BindOnce(&WorkspaceLab::CheckManagedExtensionsForProbe,
                                      weak_factory_.GetWeakPtr(), phase, deadline),
            base::Milliseconds(50));
        return;
      }
      for (size_t index = 0; index < model_.workspaces().size(); ++index) {
        if (WorkspaceProfile(index) == &owner.profile()) {
          SelectWorkspace(model_.workspaces()[index].id);
          break;
        }
      }
      shell_->OpenGeneralSettings();
      auto* button = shell_->extension_remove_for_testing(
          "extension_enabled." + owner.id());
      ui::AXActionData activate;
      activate.action = ax::mojom::Action::kDoDefault;
      if (!button->GetEnabled() || !button->HandleAccessibleAction(activate)) {
        Finish(false);
        return;
      }
      auto* prompt = shell_->danger_dialog_for_testing();
      auto* dialog = prompt ? prompt->widget_delegate()->AsDialogDelegate() : nullptr;
      if (!dialog || dialog->GetDefaultDialogButton() !=
                         static_cast<int>(ui::mojom::DialogButton::kCancel)) {
        Finish(false);
        return;
      }
      dialog->AcceptDialog();
      return;
    }
    if ((phase == ManagedProbePhase::kStart ||
         phase == ManagedProbePhase::kInitialEnable) && expects_enabled) {
      managed_owner_worker_ = worker;
    }
    if (!ToggleExtensionFromSettingsForProbe(owner.profile(), !expects_enabled, owner.id())) {
      Finish(false);
      return;
    }
    managed_cache_eviction_pending_ = verify_cache;
    switch (phase) {
      case ManagedProbePhase::kStart:
        phase = expects_enabled ? ManagedProbePhase::kDisabled
                                : ManagedProbePhase::kInitialEnable;
        break;
      case ManagedProbePhase::kInitialEnable:
        phase = ManagedProbePhase::kDisabled;
        break;
      case ManagedProbePhase::kDisabled:
        phase = ManagedProbePhase::kReenabled;
        break;
      case ManagedProbePhase::kReenabled:
        phase = ManagedProbePhase::kDone;
        break;
      case ManagedProbePhase::kDone:
        NOTREACHED();
    }
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&WorkspaceLab::CheckManagedExtensionsForProbe,
                     weak_factory_.GetWeakPtr(), phase, deadline),
      base::Milliseconds(50));
}

namespace {

std::string WebStoreListingId(std::string_view listing) {
  listing = base::TrimWhitespaceASCII(listing, base::TRIM_ALL);
  if (crx_file::id_util::IdIsValid(listing))
    return std::string(listing);
  if (listing.size() > 2048)
    return {};
  const GURL url(listing);
  if (!url.SchemeIs("https") || url.host() != "chromewebstore.google.com" ||
      url.has_username() || url.has_password() || url.has_port()) {
    return {};
  }
  const auto segments = base::SplitStringPiece(url.path(), "/", base::KEEP_WHITESPACE,
                                              base::SPLIT_WANT_NONEMPTY);
  if ((segments.size() != 2 && segments.size() != 3) || segments[0] != "detail" ||
      !crx_file::id_util::IdIsValid(segments.back())) {
    return {};
  }
  return std::string(segments.back());
}

}  // namespace

bool WorkspaceLab::InstallFromWebStore(std::string_view listing) {
  const auto id = WebStoreListingId(listing);
  if (id.empty() || extension_install_ || web_store_update_ || !CanEditWorkspace() ||
      !ExtensionDialogOwner() ||
      (extension_picker_ && extension_picker_->IsRunning(ExtensionDialogOwner()->GetNativeWindow()))) {
    return false;
  }
  const auto index = WorkspaceProfileIndex(active_index_);
  auto& profile = *contexts_[index];
  if (auto* manager = ManagedExtensionForProfile(profile, id);
      manager && manager->uninstalling()) {
    return false;
  }
  auto installed = GetConfiguredExtensions(profile);
  const auto current = std::ranges::find(installed, id, &extensions::Extension::id);
  const auto version = current == installed.end() ? "0.0.0.0"
                                                 : (*current)->version().GetString();
  extension_install_profile_ = index;
  SetExtensionManagerStatus(prototype::T(u"Pobieram i weryfikuję podpis ze sklepu…"));
  extension_install_ = WorkspaceExtensionInstall::FromWebStore(
      profile.GetDefaultStoragePartition()->GetURLLoaderFactoryForBrowserProcess().get(),
      id, version, profile.GetPath(), std::move(installed),
      base::BindOnce(&WorkspaceLab::OnExtensionReview, weak_factory_.GetWeakPtr(), index));
  return true;
}

void WorkspaceLab::CheckWebStoreUpdates() {
  if (private_owner_ || smoke_ || session_probe_url_.is_valid() || web_store_update_ ||
      extension_install_ || !CanEditWorkspace()) {
    return;
  }
  web_store_updates_.clear();
  for (size_t i = 0; i < contexts_.size(); ++i) {
    for (const auto& extension : GetConfiguredExtensions(*contexts_[i])) {
      if (extension->from_webstore())
        web_store_updates_.emplace_back(profiles_[i].id, extension->id());
    }
  }
  CheckNextWebStoreUpdate();
}

void WorkspaceLab::CheckNextWebStoreUpdate() {
  while (!web_store_updates_.empty() && CanEditWorkspace() && !extension_install_) {
    auto [profile_id, id] = std::move(web_store_updates_.back());
    web_store_updates_.pop_back();
    const auto profile = std::ranges::find(profiles_, profile_id, &ContentProfileInfo::id);
    if (profile == profiles_.end())
      continue;
    const size_t index = profile - profiles_.begin();
    auto& context = *contexts_[index];
    auto installed = GetConfiguredExtensions(context);
    const auto current = std::ranges::find(installed, id, &extensions::Extension::id);
    const auto* manager = ManagedExtensionForProfile(context, id);
    if (current == installed.end() || !(*current)->from_webstore() || !manager ||
        !manager->editable()) {
      continue;
    }
    const auto version = (*current)->version().GetString();
    web_store_update_ = WorkspaceExtensionInstall::FromWebStore(
        context.GetDefaultStoragePartition()->GetURLLoaderFactoryForBrowserProcess().get(),
        std::move(id), version, context.GetPath(), std::move(installed),
        base::BindOnce(&WorkspaceLab::OnWebStoreUpdate, weak_factory_.GetWeakPtr(),
                       std::move(profile_id)));
    return;
  }
}

void WorkspaceLab::OnWebStoreUpdate(std::string profile_id,
                                    std::optional<base::DictValue> review) {
  auto update = std::move(web_store_update_);
  const auto profile = std::ranges::find(profiles_, profile_id, &ContentProfileInfo::id);
  if (profile != profiles_.end() && review && review->contains("previous_version") &&
      CanEditWorkspace()) {
    const size_t index = profile - profiles_.begin();
    const auto* id = review->FindString("id");
    const auto* manager = id ? ManagedExtensionForProfile(*contexts_[index], *id) : nullptr;
    if (manager && manager->editable() && review->FindBool("scope_expanded") == false) {
      InstallReviewedExtension(index, *update, true);
    } else if (manager && review->FindBool("scope_expanded") == true) {
      SetExtensionManagerStatus(prototype::T(
          u"Aktualizacja rozszerzenia wymaga sprawdzenia nowych uprawnień. Użyj ponownie linku ze sklepu."));
    }
  }
  CheckNextWebStoreUpdate();
}

bool WorkspaceLab::OpenExtensionsManager() {
  if (private_owner_ || !CanEditWorkspace() || !model_.active() ||
      !IsManagedExtensionSession()) {
    return false;
  }
  const GURL url(prototype::kExtensionsTabUrl);
  auto& tabs = tabs_[active_index_];
  const auto existing = std::ranges::find_if(tabs, [&](const Tab& tab) {
    auto* page = tab.view->web_contents();
    return page ? page->GetVisibleURL() == url : tab.deferred_url == url;
  });
  shell_->CloseOverlays(true);
  if (existing != tabs.end()) {
    SelectTab(existing - tabs.begin());
  } else if (!CreateExtensionTab(*WorkspaceProfile(active_index_), url, true)) {
    return false;
  }
  widget_->Activate();
  return true;
}

views::Widget* WorkspaceLab::ExtensionDialogOwner() const {
  auto* settings = shell_->general_widget();
  if (settings && settings->IsVisible())
    return settings;
  if (const auto* page = web_contents()) {
    const auto& url = page->GetLastCommittedURL();
    if (url == GURL(prototype::kExtensionsTabUrl) || !WebStoreListingId(url.spec()).empty())
      return widget_.get();
  }
  return nullptr;
}

void WorkspaceLab::SetExtensionManagerStatus(std::u16string message, bool error) {
  extension_manager_status_ = message;
  extension_manager_error_ = error;
  shell_->SetPreferencesStatus(std::move(message), error);
}

base::DictValue WorkspaceLab::DispatchExtensionsManager(
    content::WebContents& page, const base::DictValue& request) {
  base::DictValue result;
  if (private_owner_ || !IsManagedExtensionSession() || !model_.active() ||
      page.GetLastCommittedURL() != GURL(prototype::kExtensionsTabUrl) ||
      &page != web_contents()) {
    return result;
  }
  auto& profile = *WorkspaceProfile(active_index_);
  if (page.GetBrowserContext() != &profile)
    return result;
  const auto* op = request.FindString("op");
  if (!op)
    return result;

  auto* prefs = extensions::ExtensionPrefs::Get(&profile);
  auto* profile_prefs = user_prefs::UserPrefs::Get(&profile);
  const bool can_install = base::CommandLine::ForCurrentProcess()->HasSwitch(
      kRestoreWorkspaceExtensions);
  bool ok = *op == "read";
  if (!ok && CanEditWorkspace()) {
    const auto* id = request.FindString("id");
    auto* manager = id ? ManagedExtensionForProfile(profile, *id) : nullptr;
    const auto checked = request.FindBool("value");
    if (*op == "install" && can_install) {
      SetExtensionManagerStatus({});
      OpenExtensionPackage();
      ok = true;
    } else if (*op == "autoPin" && checked && profile_prefs) {
      profile_prefs->SetBoolean(kManagerAutoPinPref, *checked);
      ok = true;
    } else if (*op == "store") {
      OpenExternalUrl(GURL("https://chromewebstore.google.com/category/extensions"));
      ok = true;
    } else if (manager && manager->editable()) {
      SetExtensionManagerStatus({});
      if (*op == "enable" && checked) {
        ok = manager->SetEnabled(*checked);
      } else if (*op == "pin" && checked) {
        ok = SetExtensionToolbarPinned(*id, *checked);
      } else if (*op == "scripts" && checked) {
        ok = manager->SetUserScriptsAllowed(*checked);
      } else if (*op == "remove") {
        ConfirmUninstallExtension(*id);
        ok = true;
      } else if (*op == "options") {
        ok = OpenExtensionOptionsTab(profile, *id);
      } else if (*op == "shortcut" && prefs) {
        const auto* name = request.FindString("command");
        const auto* key = request.FindString("key");
        for (const auto& extension : GetConfiguredExtensions(profile)) {
          if (extension->id() != *id || !name || !key || key->size() > 64)
            continue;
          ForEachExtensionCommand(*extension, [&](const ui::Command& command) {
            if (command.global() || command.command_name() != *name)
              return;
            const auto shortcut = ui::Command::StringToAccelerator(*key);
            if (!key->empty()) {
              const auto bindings = ExtensionCommandBindings(profile);
              const auto existing = bindings.find(shortcut);
              if (shortcut.key_code() == ui::VKEY_UNKNOWN ||
                  !shell_->ShortcutFor(shortcut).empty() ||
                  (existing != bindings.end() &&
                   (existing->second.extension != extension ||
                    existing->second.command != *name))) {
                return;
              }
            }
            const auto* saved = prefs->ReadPrefAsDict(*id, kManagerShortcutPref);
            auto overrides = saved ? saved->Clone() : base::DictValue();
            overrides.Set(*name, *key);
            prefs->UpdateExtensionPref(*id, kManagerShortcutPref,
                                       base::Value(std::move(overrides)));
            RefreshExtensionShortcuts();
            ok = true;
          });
        }
      }
    }
  }

  base::ListValue items;
  const auto pinned = prefs ? prefs->GetPinnedExtensions()
                            : extensions::ExtensionIdList();
  for (const auto& extension : GetConfiguredExtensions(profile)) {
    const auto* manager = ManagedExtensionForProfile(profile, extension->id());
    if (!manager)
      continue;
    const auto& permissions = extension->permissions_data()->active_permissions();
    base::ListValue api_permissions;
    for (const auto& api : permissions.GetAPIsAsStrings())
      api_permissions.Append(api);
    base::ListValue hosts;
    for (const auto& host : permissions.effective_hosts().ToStringVector())
      hosts.Append(host);
    auto item = base::DictValue().Set("id", extension->id())
        .Set("name", extension->name())
        .Set("description", extensions::DescriptionInfo::GetDescription(*extension))
        .Set("version", extension->version().GetString())
        .Set("webStore", extension->from_webstore())
        .Set("enabled", manager->enabled())
        .Set("requestedEnabled", manager->selection())
        .Set("editable", manager->editable() && CanEditWorkspace())
        .Set("canPin", manager->action_popup().is_valid())
        .Set("pinned", std::ranges::contains(pinned, extension->id()))
        .Set("options", manager->enabled() && manager->options_page().is_valid())
        .Set("commands", GetExtensionCommands(profile, *extension, true))
        .Set("permissions", std::move(api_permissions))
        .Set("hosts", std::move(hosts));
    if (const auto scripts = manager->UserScriptsAllowed())
      item.Set("scripts", *scripts);
    if (auto* loader = extensions::ImageLoader::Get(&profile)) {
      const auto key = std::make_tuple(profiles_[WorkspaceProfileIndex(active_index_)].id,
                                       extension->id(), extension->version().GetString());
      auto [icon, inserted] = extension_manager_icons_.try_emplace(key);
      if (inserted) {
        loader->LoadImageAtEveryScaleFactorAsync(
            extension.get(), gfx::Size(24, 24),
            base::BindOnce([](base::WeakPtr<WorkspaceLab> lab,
                              ExtensionManagerIconKey key, const gfx::Image& image) {
              if (!lab || image.IsEmpty())
                return;
              auto bytes = image.As1xPNGBytes();
              if (bytes && bytes->size() <= 16 * 1024) {
                lab->extension_manager_icons_[key] =
                    "data:image/png;base64," + base::Base64Encode(*bytes);
              }
            }, weak_factory_.GetWeakPtr(), key));
      }
      if (!icon->second.empty())
        item.Set("icon", icon->second);
    }
    items.Append(std::move(item));
  }
  const bool dark = widget_->GetColorMode() == ui::ColorProviderKey::ColorMode::kDark;
  const auto palette = sidebar::design_palette(dark, shell_->active_workspace_color());
  const auto css_color = [](SkColor color) {
    return base::StringPrintf("#%06x", color & 0xffffffu);
  };
  auto colors = base::DictValue().Set("frame", css_color(palette.bg_void))
      .Set("sidebar", css_color(palette.bg_base))
      .Set("surface", css_color(palette.bg_raised))
      .Set("hover", css_color(palette.bg_hover))
      .Set("line", css_color(palette.line))
      .Set("edge", css_color(palette.line_strong))
      .Set("ink", css_color(palette.ink))
      .Set("muted", css_color(palette.ink_muted))
      .Set("accent", css_color(palette.accent))
      .Set("on-accent", css_color(palette.on_accent))
      .Set("accent-ink", css_color(palette.accent_ink))
      .Set("danger", css_color(palette.danger));
  return base::DictValue().Set("ok", ok)
      .Set("items", std::move(items))
      .Set("colors", std::move(colors))
      .Set("profile", WorkspaceProfileName(active_index_))
      .Set("canInstall", can_install)
      .Set("autoPin", profile_prefs && profile_prefs->GetBoolean(kManagerAutoPinPref))
      .Set("dark", dark)
      .Set("english", prototype::UiLanguageIsEnglish())
      .Set("status", extension_manager_status_)
      .Set("error", extension_manager_error_);
}

void WorkspaceLab::OpenExtensionPackage() {
  auto* settings = ExtensionDialogOwner();
  if (!settings || !settings->IsVisible() || !CanEditWorkspace() ||
      extension_install_ || web_store_update_ ||
      (extension_picker_ && extension_picker_->IsRunning(settings->GetNativeWindow()))) {
    return;
  }
  extension_install_profile_ = WorkspaceProfileIndex(active_index_);
  const auto probe = base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
      kExtensionInstallUiProbe);
  if (!probe.empty()) {
    FileSelected(ui::SelectedFileInfo(probe), 1);
    return;
  }
  if (!extension_picker_)
    extension_picker_ = ui::SelectFileDialog::Create(this, nullptr);
  ui::SelectFileDialog::FileTypeInfo types;
  types.extensions = {{FILE_PATH_LITERAL("crx")}};
  extension_picker_->SelectFile(
      ui::SelectFileDialog::SELECT_OPEN_FILE,
      prototype::T(u"Wybierz podpisane rozszerzenie"), {}, &types, 1,
      FILE_PATH_LITERAL("crx"), settings->GetNativeWindow());
}

void WorkspaceLab::ShowExtensionsToolbarMenu() {
  CloseNightShiftSettings();
  if (extensions_toolbar_menu_widget_ && !extensions_toolbar_menu_widget_->IsClosed()) {
    CloseExtensionsToolbarMenu();
    return;
  }
  auto& profile = *WorkspaceProfile(active_index_);
  const auto* prefs = extensions::ExtensionPrefs::Get(&profile);
  const auto pinned = prefs ? prefs->GetPinnedExtensions() : extensions::ExtensionIdList();
  const auto weak = weak_factory_.GetWeakPtr();
  const auto palette = base::BindRepeating([](WorkspaceLab* lab) {
    return sidebar::design_palette(
        lab->widget_->GetColorMode() == ui::ColorProviderKey::ColorMode::kDark,
        lab->shell_->active_workspace_color());
  }, base::Unretained(this));
  const auto colors = palette.Run();
  shell_->CloseOverlays(true);
  CloseAdblockShield();
  fingerprint_toolbar_->Close();

  extensions_toolbar_menu_ = std::make_unique<views::BubbleDialogDelegate>(
      extensions_toolbar_button_, views::BubbleBorder::TOP_RIGHT);
  auto& panel = *extensions_toolbar_menu_;
  panel.SetOwnershipOfNewWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  panel.SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  panel.set_margins(gfx::Insets(4));
  panel.set_corner_radius(12);
  panel.set_fixed_width(prototype::kMenuWidth);
  panel.SetBackgroundColor(colors.bg_raised);
  panel.SetTitle(prototype::T(u"Rozszerzenia"));
  panel.SetShowTitle(false);
  panel.SetShowCloseButton(false);

  auto body = std::make_unique<views::View>();
  body->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  body->AddChildView(std::make_unique<prototype::MenuHeader>(colors));
  auto add_row = [&](const char* glyph, std::u16string title, int height,
                     base::RepeatingClosure run) {
    auto* row = body->AddChildView(std::make_unique<prototype::MenuRow>(
        std::move(run), colors, height));
    row->SetGlyph(glyph);
    row->Init();
    row->SetText(std::move(title), {}, false, 16);
    return row;
  };
  bool empty = true;
  for (const auto& manager : managed_extensions_) {
    if (!manager->OwnsProfile(profile))
      continue;
    empty = false;
    auto name = base::UTF8ToUTF16(manager->name());
    std::ranges::replace_if(name, [](char16_t c) { return c < 0x20; }, u' ');
    base::i18n::SanitizeUserSuppliedString(&name);
    const auto id = manager->id();
    const bool has_popup = manager->action_popup().is_valid();
    const bool has_options = manager->options_page().is_valid();
    auto* row = add_row(nullptr, name, 44,
        base::BindRepeating([](base::WeakPtr<WorkspaceLab> lab, std::string id,
                               bool popup, bool options) {
          if (!lab || (!popup && !options))
            return;
          lab->CloseExtensionsToolbarMenu();
          if (popup)
            lab->OpenExtensionPopup(id);
          else
            lab->OpenExtensionOptions(id);
        }, weak, id, has_popup, has_options));
    auto icon = std::make_unique<ContentToolbarButton>(
        kExtensionsPuzzle, name, views::Button::PressedCallback(), palette);
    icon->SetPreferredSize(gfx::Size(20, 20));
    icon->SetCanProcessEventsWithinSubtree(false);
    icon->GetViewAccessibility().SetIsIgnored(true);
    const auto* extension = extensions::ExtensionRegistry::Get(&profile)
                                ->enabled_extensions().GetByID(id);
    if (extension) {
      if (auto* action = extensions::ExtensionActionManager::Get(&profile)
                             ->GetExtensionAction(*extension)) {
        icon->SetExtensionAction(*extension, *action,
            web_contents() ? WorkspaceExtensionTabIdForTesting(*web_contents()) : -1);
      }
    }
    row->AddChildViewAt(std::move(icon), 0);
    row->SetText(name, manager->enabled() ? std::u16string() : prototype::T(u"Wyłączone w tym profilu"),
                 false, 16);
    if (has_popup) {
      const bool is_pinned = std::ranges::contains(pinned, id);
      constexpr char kPin[] = "M6 2.5h4l-.5 4 2 2h-7l2-2zM8 8.5V14";
      auto pin = std::make_unique<ContentToolbarButton>(
          kPin, prototype::TF(is_pinned ? u"Odepnij rozszerzenie: {}" : u"Przypnij rozszerzenie: {}", {name}),
          base::BindRepeating([](base::WeakPtr<WorkspaceLab> lab, std::string id, bool pinned) {
            if (lab && lab->SetExtensionToolbarPinned(std::move(id), pinned))
              lab->CloseExtensionsToolbarMenu();
          }, weak, id, !is_pinned), palette);
      pin->SetToggledAppearance(is_pinned);
      row->AddTrailing(row->AddChildView(std::move(pin)), true);
    }
  }
  if (empty)
    add_row(nullptr, prototype::T(u"Brak zainstalowanych rozszerzeń"), 36, {})->SetEnabled(false);
  body->AddChildView(std::make_unique<prototype::MenuSeparator>(colors.line));
  add_row(prototype::kGearIcon, prototype::T(u"Zarządzaj rozszerzeniami"),
          prototype::kFooterHeight, base::BindRepeating([](base::WeakPtr<WorkspaceLab> lab) {
            if (!lab)
              return;
            lab->CloseExtensionsToolbarMenu();
            lab->OpenExtensionsManager();
          }, weak))->SetRing(true);
  constexpr char kStore[] = "M3 6h10l-1 7.5H4zM5.5 6V5a2.5 2.5 0 0 1 5 0v1";
  if (web_contents() && base::CommandLine::ForCurrentProcess()->HasSwitch(
                            kRestoreWorkspaceExtensions)) {
    const auto listing = web_contents()->GetLastCommittedURL();
    if (!WebStoreListingId(listing.spec()).empty()) {
      auto* install = add_row(kStore, prototype::T(u"Zainstaluj z tej strony"),
          prototype::kFooterHeight,
          base::BindRepeating([](base::WeakPtr<WorkspaceLab> lab, GURL listing) {
            if (!lab || !lab->web_contents() ||
                lab->web_contents()->GetLastCommittedURL() != listing)
              return;
            lab->CloseExtensionsToolbarMenu();
            lab->InstallFromWebStore(listing.spec());
          }, weak, listing));
      install->SetEnabled(!extension_install_ && !web_store_update_);
    }
  }
  add_row(kStore, u"Chrome Web Store", prototype::kFooterHeight,
          base::BindRepeating([](base::WeakPtr<WorkspaceLab> lab) {
            if (!lab)
              return;
            lab->CloseExtensionsToolbarMenu();
            lab->OpenExternalUrl(GURL("https://chromewebstore.google.com/category/extensions"));
          }, weak));
  panel.SetContentsView(std::move(body));
  extensions_toolbar_menu_widget_ = views::BubbleDialogDelegate::CreateBubble(&panel);
  extensions_toolbar_menu_widget_->Show();
}

void WorkspaceLab::CloseExtensionsToolbarMenu() {
  if (extensions_toolbar_menu_widget_ && !extensions_toolbar_menu_widget_->IsClosed())
    extensions_toolbar_menu_widget_->Close();
}

bool WorkspaceLab::SetExtensionToolbarPinned(std::string id, bool pinned) {
  if (!CanEditWorkspace())
    return false;
  auto& profile = *WorkspaceProfile(active_index_);
  const auto* manager = ManagedExtensionForProfile(profile, id);
  if (!manager || !manager->action_popup().is_valid())
    return false;
  auto* prefs = extensions::ExtensionPrefs::Get(&profile);
  if (!prefs)
    return false;
  SetManagerToolbarPin(*prefs, id, pinned);
  RefreshExtensionActionAppearance();
  return true;
}

void WorkspaceLab::OpenExtensionPopup(std::string id) {
  if (extensions_toolbar_button_)
    OpenExtensionPopupAt(std::move(id), extensions_toolbar_button_->GetBoundsInScreen());
}

void WorkspaceLab::OpenExtensionPopupAt(std::string id, gfx::Rect anchor) {
  if (extension_popup_widget_ && !extension_popup_widget_->IsClosed()) {
    CloseExtensionPopup();
    return;
  }
  if (!widget_ || !widget_->IsVisible() || !CanEditWorkspace() ||
      private_owner_) {
    return;
  }
  if (auto* page = web_contents(); page && ToggleWorkspaceSidePanelOnAction(*page, id))
    return;
  auto& profile = *WorkspaceProfile(active_index_);
  const auto* manager = ManagedExtensionForProfile(profile, id);
  const GURL url = manager ? manager->action_popup() : GURL();
  if (!url.is_valid())
    return;
  if (auto* page = web_contents())
    GrantWorkspaceExtensionActiveTab(*page, id);

  auto name = base::UTF8ToUTF16(manager->name());
  std::ranges::replace_if(name, [](char16_t c) { return c < 0x20; }, u' ');
  base::i18n::SanitizeUserSuppliedString(&name);
  ShowExtensionBubble(profile, url, std::move(name), anchor);
}

content::WebContents* WorkspaceLab::ShowExtensionBubble(
    ProfileContext& profile, const GURL& url, std::u16string title,
    gfx::Rect anchor) {
  if (extension_popup_widget_ && !extension_popup_widget_->IsClosed())
    return nullptr;
  // Keep the delegate alive while Views finishes notifying widget observers.
  extension_popup_view_ = nullptr;
  extension_popup_widget_.reset();
  extension_popup_delegate_.reset();
  auto view = std::make_unique<LabWebView>(&profile);
  view->SetPreferredSize(gfx::Size(380, 420));
  auto* contents = view->GetWebContents(url);
  extensions::SetViewType(contents, extensions::mojom::ViewType::kExtensionPopup);
  extension_popup_view_ = view.get();
  CloseNightShiftSettings();
  CloseAdblockShield();
  fingerprint_toolbar_->Close();
  shell_->CloseOverlays(true);
  extension_popup_delegate_ = std::make_unique<views::BubbleDialogDelegate>(
      extensions_toolbar_button_, views::BubbleBorder::TOP_RIGHT);
  auto& bubble = *extension_popup_delegate_;
  // Pinned buttons can be recreated while the extension updates its icon.
  // Use a screen rect and parent the bubble to the stable browser window.
  bubble.SetAnchorView(nullptr);
  bubble.SetAnchorRect(anchor);
  bubble.set_parent_window(widget_->GetNativeView());
  bubble.SetOwnershipOfNewWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  bubble.SetTitle(std::move(title));
  bubble.SetShowTitle(false);
  bubble.SetShowCloseButton(false);
  bubble.SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  bubble.set_margins(gfx::Insets());
  bubble.set_corner_radius(12);
  bubble.set_close_on_deactivate(true);
  bubble.SetContentsView(std::move(view));
  extension_popup_widget_ = views::BubbleDialogDelegate::CreateBubble(&bubble);
  extension_popup_widget_->SetColorModeOverride(widget_->color_mode_override());
  extension_popup_widget_->Show();
  extension_popup_view_->RequestFocus();
  prototype::LoadWorkspaceTab(contents, url, ui::PAGE_TRANSITION_AUTO_TOPLEVEL);
  return contents;
}

void WorkspaceLab::CloseExtensionPopup() {
  if (extension_popup_widget_ && !extension_popup_widget_->IsClosed())
    extension_popup_widget_->Close();
}

void WorkspaceLab::CloseNightShiftSettings() {
  if (night_shift_widget_ && !night_shift_widget_->IsClosed())
    night_shift_widget_->Close();
}

bool WorkspaceLab::OpenNightShiftSettings(content::RenderFrameHost* document,
                                          bool quick_panel) {
  if (night_shift_widget_ && !night_shift_widget_->IsClosed()) {
    CloseNightShiftSettings();
    if (quick_panel)
      return true;
  }
  if (!night_shift_button_ || private_owner_ || session_path_.empty() || !CanEditWorkspace())
    return false;
  auto& context = *WorkspaceProfile(active_index_);
  GURL url = BundledNightShiftSettingsPage(context);
  if (!url.is_valid())
    return false;

  base::ListValue profiles;
  const auto& current = model_.active()->profile_id;
  for (const auto& profile : profiles_) {
    const auto spaces = std::ranges::count(model_.workspaces(), profile.id,
                                           &Workspace::profile_id);
    profiles.Append(base::DictValue()
                        .Set("id", profile.id)
                        .Set("name", profile.name)
                        .Set("spaces", static_cast<int>(spaces))
                        .Set("current", profile.id == current));
  }
  auto state = base::DictValue().Set("profiles", std::move(profiles));
  if (night_shift_config_.managed)
    state.Set("config", night_shift_config_.ToValue());
  const auto encoded = base::WriteJson(state);
  if (!encoded)
    return false;
  const auto theme = shell_->theme_mode();
  const bool dark = theme == prototype::ThemeMode::kDark ||
      (theme == prototype::ThemeMode::kSystem &&
       ui::NativeTheme::GetInstanceForNativeUi()->preferred_color_scheme() ==
           ui::NativeTheme::PreferredColorScheme::kDark);
  url = net::AppendOrReplaceQueryParameter(url, "appearance", dark ? "dark" : "light");
  url = net::AppendOrReplaceQueryParameter(
      url, "language", prototype::UiLanguageIsEnglish() ? "en" : "pl");
  url = net::AppendOrReplaceQueryParameter(url, "state", *encoded);
  if (quick_panel) {
    auto* frame = web_contents()->GetPrimaryMainFrame();
    if (CanSetNightShiftSiteMode(*frame))
      document = frame;
    if (document && assistant_host_settings_.enabled)
      url = net::AppendOrReplaceQueryParameter(url, "repair", "1");
  }
  if (document) {
    const auto site_key = NightShiftSiteKey(document->GetLastCommittedURL());
    auto* source = content::WebContents::FromRenderFrameHost(document);
    const int tab = source ? WorkspaceExtensionTabIdForTesting(*source) : -1;
    if (site_key.empty() || tab <= 0 || document->GetBrowserContext() != &context)
      return false;
    url = net::AppendOrReplaceQueryParameter(url, "site", site_key);
    url = net::AppendOrReplaceQueryParameter(url, "tab", base::NumberToString(tab));
    url = net::AppendOrReplaceQueryParameter(
        url, "document", extensions::ExtensionApiFrameIdMap::GetDocumentId(document).ToString());
  }
  auto apply = base::BindRepeating(&WorkspaceLab::ApplyNightShiftSettings,
                                   weak_factory_.GetWeakPtr());
  auto site_changed = base::BindRepeating(&WorkspaceLab::RefreshNightShiftButton,
                                          weak_factory_.GetWeakPtr(), true);
  if (!quick_panel) {
    if (!CreateExtensionTab(context, url, /*active=*/true))
      return false;
    NightShiftSettingsObserver::CreateForWebContents(
        web_contents(), url, std::move(apply), base::RepeatingCallback<void(bool)>(),
        std::move(site_changed), base::RepeatingCallback<void(int)>());
    shell_->CloseOverlays(true);
    widget_->Activate();
    return true;
  }
  url = net::AppendOrReplaceQueryParameter(url, "presentation", "panel");
  const auto palette = sidebar::design_palette(
      widget_->GetColorMode() == ui::ColorProviderKey::ColorMode::kDark,
      shell_->active_workspace_color());
  shell_->CloseOverlays(true);
  CloseAdblockShield();
  CloseExtensionsToolbarMenu();
  fingerprint_toolbar_->Close();
  night_shift_widget_.reset();
  night_shift_panel_ = std::make_unique<views::BubbleDialogDelegate>(
      night_shift_button_, views::BubbleBorder::TOP_RIGHT);
  auto& panel = *night_shift_panel_;
  panel.SetOwnershipOfNewWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  panel.SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  panel.set_margins(gfx::Insets());
  panel.set_corner_radius(12);
  panel.SetBackgroundColor(palette.bg_raised);
  panel.SetTitle(u"Night Shift");
  panel.SetShowTitle(false);
  panel.SetShowCloseButton(false);
  panel.set_close_on_deactivate(true);

  auto view = std::make_unique<LabWebView>(&context);
  constexpr int kPanelWidth = 296;
  view->SetPreferredSize(gfx::Size(kPanelWidth, document ? 340 : 120));
  auto* page = view->GetWebContents(url);
  SetNightShiftSettingsDestination(*page, url);
  NightShiftSettingsObserver::CreateForWebContents(
      page, url, std::move(apply),
      base::BindRepeating([](base::WeakPtr<WorkspaceLab> lab,
                             const content::WeakDocumentPtr& document, bool repair) {
        if (!lab)
          return;
        lab->CloseNightShiftSettings();
        if (repair) {
          if (auto* frame = document.AsRenderFrameHostIfValid();
              frame && content::WebContents::FromRenderFrameHost(frame) == lab->web_contents()) {
            lab->PrepareNightShiftRepair(*frame);
          }
        } else {
          lab->OpenNightShiftSettings();
        }
      }, weak_factory_.GetWeakPtr(),
      document ? document->GetWeakDocumentPtr() : content::WeakDocumentPtr()),
      std::move(site_changed),
      base::BindRepeating([](base::WeakPtr<WorkspaceLab> lab, int height) {
        if (!lab || !lab->night_shift_widget_ ||
            lab->night_shift_widget_->IsClosed()) {
          return;
        }
        const auto work_area = display::Screen::Get()
                                   ->GetDisplayNearestWindow(lab->widget_->GetNativeWindow())
                                   .work_area();
        const auto anchor = lab->night_shift_button_->GetBoundsInScreen();
        // Leave room for the bubble frame on either side of the anchor.
        const int available = std::max(work_area.bottom() - anchor.bottom(),
                                       anchor.y() - work_area.y()) - 16;
        const gfx::Size size(kPanelWidth,
                             std::clamp(height, 1, std::max(1, available)));
        auto* contents = lab->night_shift_panel_->GetContentsView();
        if (contents->GetPreferredSize() != size) {
          contents->SetPreferredSize(size);
          lab->night_shift_panel_->SizeToContents();
        }
      }, weak_factory_.GetWeakPtr()));
  panel.SetContentsView(std::move(view));
  night_shift_widget_ = views::BubbleDialogDelegate::CreateBubble(&panel);
  night_shift_widget_->SetColorModeOverride(widget_->color_mode_override());
  night_shift_widget_->Show();
  prototype::LoadWorkspaceTab(page, url, ui::PAGE_TRANSITION_AUTO_TOPLEVEL);
  return true;
}

void WorkspaceLab::ApplyNightShiftSettings(base::DictValue value) {
  if (private_owner_ || session_path_.empty() || !CanEditWorkspace())
    return;
  auto config = prototype::NightShiftConfig::FromValue(&value);
  std::erase_if(config.profiles, [&](const std::string& id) {
    return !std::ranges::contains(profiles_, id, &ContentProfileInfo::id);
  });
  std::erase_if(config.profile_overrides, [&](const auto& entry) {
    return !std::ranges::contains(profiles_, entry.first, &ContentProfileInfo::id);
  });
  night_shift_config_ = std::move(config);
  PersistWorkspaceChanges();
  for (size_t i = 0; i < contexts_.size(); ++i) {
    ApplyBundledNightShift(*contexts_[i],
                           night_shift_config_.EnabledFor(profiles_[i].id),
                           night_shift_config_);
  }
  RefreshNightShiftButton(true);
}

void WorkspaceLab::RefreshNightShiftButton(bool force) {
  if (!night_shift_button_)
    return;
  auto* page = web_contents();
  const auto key = page ? NightShiftSiteKey(page->GetLastCommittedURL()) : std::string();
  if (!force && night_shift_indicator_page_.get() == page && night_shift_indicator_key_ == key)
    return;
  const auto read = ++night_shift_indicator_read_;
  if (!force || key.empty())
    views::AsViewClass<ContentToolbarButton>(night_shift_button_)->SetToggledAppearance(false);
  night_shift_indicator_page_ = page ? page->GetWeakPtr() : nullptr;
  night_shift_indicator_key_ = key;
  if (key.empty())
    return;
  auto* context = page->GetBrowserContext();
  const auto* extension = GetBundledNightShiftExtension(*context);
  if (!extension)
    return;
  extensions::StorageFrontend::Get(context)->GetValues(
      base::WrapRefCounted(extension), extensions::StorageAreaNamespace::kLocal,
      std::vector<std::string>{key, "enabled"},
      base::BindOnce([](base::WeakPtr<WorkspaceLab> lab, uint64_t read, std::string key,
                        extensions::StorageFrontend::GetResult result) {
        if (!lab || !lab->night_shift_button_ || lab->night_shift_indicator_read_ != read ||
            !result.data)
          return;
        const auto* record = result.data->FindDict(key);
        const auto* mode = record ? record->FindString("mode") : nullptr;
        const bool active = (mode && (*mode == "on" || *mode == "dark")) ||
                            ((!mode || *mode != "off") &&
                             result.data->FindBool("enabled").value_or(false));
        views::AsViewClass<ContentToolbarButton>(lab->night_shift_button_)
            ->SetToggledAppearance(active);
      }, weak_factory_.GetWeakPtr(), read, key));
}

void WorkspaceLab::OpenExtensionOptions(std::string id) {
  OpenExtensionOptionsTab(*WorkspaceProfile(active_index_), id);
}

bool WorkspaceLab::SetExtensionSidePanel(WorkspaceExtensionTab& target,
                                        const std::string& id, const GURL& url,
                                        bool toggle) {
  const auto position = ExtensionTabPosition(target);
  if (!position)
    return false;
  const auto [workspace, index] = *position;
  if (!url.is_empty() && (private_owner_ || !CanEditWorkspace() ||
                         workspace != active_index_ || !url.SchemeIs(extensions::kExtensionScheme) ||
                         url.GetHost() != id)) {
    return false;
  }
  auto& tab = tabs_[workspace][index];
  auto* page = tab.view->web_contents();
  if (!url.is_empty() && !page)
    return false;
  return static_cast<WorkspaceWebView*>(tab.view.get())->SetExtensionSidePanel(
      id, page ? WorkspaceExtensionTabIdForTesting(*page) : -1, url, toggle);
}

bool WorkspaceLab::OpenExtensionOptionsTab(content::BrowserContext& context,
                                           const std::string& id) {
  if (private_owner_ || context.IsOffTheRecord() || !CanEditWorkspace())
    return false;
  const auto* manager = ManagedExtensionForProfile(
      static_cast<ProfileContext&>(context), id);
  const GURL url = manager ? manager->options_page() : GURL();
  if (!url.is_valid())
    return false;
  CloseExtensionPopup();

  for (size_t workspace = 0; workspace < tabs_.size(); ++workspace) {
    if (WorkspaceProfile(workspace) != &context)
      continue;
    const auto& tabs = tabs_[workspace];
    const auto existing = std::ranges::find_if(tabs, [&](const Tab& tab) {
      auto* page = tab.view->web_contents();
      return page && page->GetVisibleURL() == url;
    });
    if (existing == tabs.end())
      continue;
    const auto index = existing - tabs.begin();
    if (workspace != active_index_) {
      if (PeekPage())
        return false;
      SelectWorkspace(model_.workspaces()[workspace].id);
      if (active_index_ != workspace)
        return false;
    }
    shell_->CloseOverlays(true);
    SelectTab(index);
    widget_->Activate();
    return true;
  }

  if (!CreateExtensionTab(context, url, /*active=*/true))
    return false;
  shell_->CloseOverlays(true);
  widget_->Activate();
  return true;
}

void WorkspaceLab::ConfirmUninstallExtension(std::string id) {
  auto* settings = ExtensionDialogOwner();
  if (!settings || !settings->IsVisible() || !CanEditWorkspace())
    return;
  const size_t profile = WorkspaceProfileIndex(active_index_);
  auto* extension = ManagedExtensionForProfile(*contexts_[profile], id);
  if (!extension || !extension->editable())
    return;
  auto name = base::UTF8ToUTF16(extension->name());
  base::i18n::SanitizeUserSuppliedString(&name);
  GURL uninstall_url = extension->uninstall_url();
  std::u16string detail =
      prototype::TF(u"Profil: {}", {base::UTF8ToUTF16(profiles_[profile].name)});
  if (uninstall_url.is_valid()) {
    auto origin =
        base::UTF8ToUTF16(url::Origin::Create(uninstall_url).Serialize());
    base::i18n::SanitizeUserSuppliedString(&origin);
    detail += u"\n" +
              prototype::TF(u"Po usunięciu otworzy się strona {}.", {origin});
  }
  shell_->ConfirmDanger(
      settings, prototype::TF(u"Usunąć {}?", {name}), std::move(detail),
      prototype::T(u"Dane rozszerzenia w tym profilu zostaną usunięte."),
      prototype::T(u"Usuń"),
      base::BindOnce(
          [](base::WeakPtr<WorkspaceLab> lab, size_t profile,
             std::string id, GURL uninstall_url, bool accepted) {
            if (!accepted || !lab || !lab->CanEditWorkspace() ||
                lab->WorkspaceProfileIndex(lab->active_index_) != profile)
              return;
            auto* extension = lab->ManagedExtensionForProfile(*lab->contexts_[profile], id);
            if (!extension)
              return;
            extension->Uninstall(base::BindOnce(
                [](base::WeakPtr<WorkspaceLab> lab, size_t profile,
                   std::string id, GURL uninstall_url, bool success) {
                  if (!lab)
                    return;
                  if (success) {
                    std::erase_if(lab->managed_extensions_, [&](const auto& extension) {
                      return extension->OwnsProfile(*lab->contexts_[profile]) &&
                             extension->id() == id;
                    });
                  }
                  lab->RefreshShell();
                  if (lab->WorkspaceProfileIndex(lab->active_index_) == profile)
                    lab->SetExtensionManagerStatus(
                        success ? prototype::T(u"Rozszerzenie zostało usunięte.")
                                : prototype::T(u"Nie udało się usunąć rozszerzenia."),
                        !success);
                  if (success && uninstall_url.is_valid()) {
                    lab->CreateExtensionTab(
                        *lab->contexts_[profile], uninstall_url,
                        /*active=*/lab->WorkspaceProfileIndex(lab->active_index_) == profile);
                  }
                  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
                          "zitter-content-extension-uninstall-probe")) {
                    if (!success || lab->managed_extensions_.size() != 1 ||
                        !lab->managed_uninstall_probe_page_ ||
                        lab->managed_uninstall_probe_page_->GetPrimaryMainFrame()->IsRenderFrameLive() ||
                        lab->managed_extensions_.front()->RunningWorkerForTesting() !=
                            lab->managed_peer_worker_) {
                      lab->Finish(false);
                      return;
                    }
                    std::puts("Zitter Content removed an extension without restarting its peer");
                    lab->RequestClose();
                  }
                }, lab, profile, std::move(id), std::move(uninstall_url)));
          }, weak_factory_.GetWeakPtr(), profile, std::move(id),
          std::move(uninstall_url)));
}

void WorkspaceLab::FileSelected(const ui::SelectedFileInfo& file, int index) {
  if (!CanEditWorkspace() ||
      WorkspaceProfileIndex(active_index_) != extension_install_profile_) {
    return;
  }
  SetExtensionManagerStatus(prototype::T(u"Sprawdzam podpis rozszerzenia…"));
  extension_install_ = WorkspaceExtensionInstall::Start(
      file.path(), contexts_[extension_install_profile_]->GetPath(),
      GetConfiguredExtensions(*contexts_[extension_install_profile_]),
      base::BindOnce(&WorkspaceLab::OnExtensionReview,
                     weak_factory_.GetWeakPtr(), extension_install_profile_));
}

void WorkspaceLab::FileSelectionCanceled() {}

void WorkspaceLab::OnExtensionReview(size_t profile,
                                    std::optional<base::DictValue> review) {
  auto install = std::move(extension_install_);
  auto* settings = ExtensionDialogOwner();
  if (!settings || !settings->IsVisible() || !CanEditWorkspace() ||
      WorkspaceProfileIndex(active_index_) != profile) {
    SetExtensionManagerStatus({});
    return;
  }
  if (!review) {
    SetExtensionManagerStatus(
        prototype::T(u"Nie udało się zweryfikować tego rozszerzenia."), true);
    if (base::CommandLine::ForCurrentProcess()->HasSwitch(kExtensionInstallUiProbe)) {
      LOG(ERROR) << "The Settings package review failed";
      Finish(false);
    }
    return;
  }
  if (review->FindBool("up_to_date") == true) {
    SetExtensionManagerStatus(prototype::T(u"Rozszerzenie jest aktualne."));
    return;
  }
  const auto text = [](const std::string& value) {
    auto result = base::UTF8ToUTF16(value);
    std::ranges::replace_if(result, [](char16_t c) { return c < 0x20; }, u' ');
    base::i18n::SanitizeUserSuppliedString(&result);
    return result;
  };
  std::u16string detail = prototype::TF(u"Profil: {}",
      {text(profiles_[profile].name)});
  const auto* previous_version = review->FindString("previous_version");
  detail += u"\n" + (previous_version
      ? prototype::TF(u"Wersja: {} → {}",
                     {text(*previous_version), text(*review->FindString("version"))})
      : prototype::TF(u"Wersja: {}", {text(*review->FindString("version"))}));
  if (review->FindBool("scope_expanded").value_or(false)) {
    detail += u"\n" + prototype::T(
        u"Ta aktualizacja zmienia zakres dostępu lub zabezpieczenia rozszerzenia.");
  }
  detail += u"\n" + prototype::TF(u"Klucz podpisu SHA-256: {}",
      {text(*review->FindString("key_sha256"))});
  const auto append = [&](std::u16string label, const base::ListValue& values) {
    for (const auto& value : values)
      detail += u"\n" + label + u" " + text(value.GetString());
  };
  append(prototype::T(u"Uprawnienie:"), *review->FindList("api_permissions"));
  if (review->FindBool("file_access_blocked").value_or(false))
    detail += u"\n" + prototype::T(u"Dostęp rozszerzenia do lokalnych plików jest wyłączony.");
  if (const auto* commands = review->FindDict("commands")) {
    for (const auto [name, value] : *commands) {
      detail += u"\n" + prototype::TF(u"Polecenie rozszerzenia: {}", {text(name)});
      if (const auto* keys = value.GetDict().FindDict("suggested_key")) {
        for (const auto [platform, key] : *keys) {
          if (key.is_string())
            detail += u" " + text(platform) + u" " + text(key.GetString());
        }
      }
    }
  }

  if (const auto* options = review->FindString("options_page"))
    detail += u"\n" + prototype::TF(u"Strona opcji: {}", {text(*options)});
  if (const auto* popup = review->FindString("action_popup"))
    detail += u"\n" + prototype::TF(u"Strona popupu: {}", {text(*popup)});
  if (const auto* previous_policy = review->FindString("previous_extension_pages_csp")) {
    detail += u"\n" + prototype::TF(
        u"Poprzednia polityka CSP rozszerzenia: {}", {text(*previous_policy)});
  }
  if (const auto* policy = review->FindString("extension_pages_csp")) {
    detail += u"\n" + prototype::TF(u"Polityka CSP rozszerzenia: {}", {text(*policy)});
  }
  if (const auto* sandbox = review->FindList("sandbox_pages")) {
    detail += u"\n" + text("Sandbox CSP") + u"  " + text(*review->FindString("sandbox_csp"));
    append(prototype::T(u"Zasób:"), *sandbox);
  }
  if (const auto* resources = review->FindList("web_accessible_resources")) {
    for (const auto& entry : *resources) {
      const auto& resource = entry.GetDict();
      detail += u"\n" + prototype::T(
          resource.FindBool("use_dynamic_url").value_or(false)
              ? u"Zasoby dostępne dla witryn pod adresem dynamicznym"
              : u"Zasoby dostępne dla witryn");
      append(prototype::T(u"Zasób:"), *resource.FindList("resources"));
      append(prototype::T(u"Dostęp dla:"), *resource.FindList("matches"));
    }
  }
  append(prototype::T(u"Odczyt danych kart dla:"),
         *review->FindList("host_permissions"));
  append(prototype::T(u"Odczyt odpowiedzi z:"),
         *review->FindList("network_hosts"));
  append(prototype::T(u"Nieaktywne uprawnienie opcjonalne:"),
         *review->FindList("optional_api_permissions"));
  append(prototype::T(u"Nieaktywny opcjonalny dostęp do hosta:"),
         *review->FindList("optional_host_permissions"));
  if (review->FindList("api_permissions")->contains("fontSettings")) {
    detail += u"\n" + prototype::T(u"Odczyt listy zainstalowanych czcionek.");
  }
  if (review->FindList("api_permissions")->contains("tabs")) {
    detail += u"\n" + prototype::T(u"Odczyt adresów i tytułów kart w tym profilu.");
  }
  const auto& external_ids = *review->FindList("external_extension_ids");
  if (external_ids.size() == 1 && external_ids[0].GetString() == "*") {
    detail += u"\n" + prototype::T(u"Wiadomości może wysyłać każde rozszerzenie w tym profilu.");
  } else {
    append(prototype::T(u"Nadawca wiadomości:"), external_ids);
  }
  for (const auto& value : *review->FindList("content_scripts")) {
    const auto& script = value.GetDict();
    detail += u"\n" + prototype::T(
        script.FindBool("all_frames").value_or(false)
            ? u"Skrypt na stronach i w pasujących ramkach"
            : u"Skrypt na stronach");
    if (script.FindBool("match_origin_as_fallback").value_or(false)) {
      detail += u"\n" + prototype::T(
          u"Skrypt może dopasować origin inicjatora dokumentu, niezależnie od ścieżki inicjatora.");
    } else if (script.FindBool("match_about_blank").value_or(false)) {
      detail += u"\n" + prototype::T(
          u"Skrypt może też działać w dokumentach about:blank i about:srcdoc powiązanych z pasującą stroną.");
    }
    if (*script.FindString("world") == "MAIN")
      detail += u"\n" + prototype::T(u"Skrypt działa w tym samym środowisku co kod strony.");
    const auto& timing = *script.FindString("run_at");
    detail += u"\n" + prototype::T(
        timing == "document_start" ? u"Uruchamia się na początku ładowania strony."
        : timing == "document_end" ? u"Uruchamia się po odczytaniu dokumentu."
                                   : u"Uruchamia się, gdy dokument jest gotowy.");
    append(prototype::T(u"Adres:"), *script.FindList("matches"));
    append(prototype::T(u"Wykluczony adres:"), *script.FindList("exclude_matches"));
    append(prototype::T(u"Wzorzec:"), *script.FindList("include_globs"));
    append(prototype::T(u"Wykluczony wzorzec:"), *script.FindList("exclude_globs"));
  }
  // The modal scrolls. Bound rendering work for exceptionally large manifests
  // without truncating the permissions the user is asked to approve.
  constexpr size_t kMaxExtensionReviewCharacters = 16 * 1024;
  if (detail.size() > kMaxExtensionReviewCharacters) {
    SetExtensionManagerStatus(
        prototype::T(u"Zakres dostępu jest za długi, aby wyświetlić go w całości."),
        true);
    return;
  }
  const bool is_update = previous_version != nullptr;
  if (is_update && base::CommandLine::ForCurrentProcess()->HasSwitch(kExtensionInstallUiProbe) &&
      review->FindBool("scope_expanded") == true) {
    std::puts("Zitter Content disclosed expanded extension access in Settings");
  }
  SetExtensionManagerStatus({});
  shell_->ConfirmDanger(
      settings, prototype::TF(is_update ? u"Zaktualizować {}?" : u"Zainstalować {}?",
                             {text(*review->FindString("name"))}),
      std::move(detail),
      prototype::T(u"Rozszerzenie działa w tle. Podpis identyfikuje klucz, ale Zitter nie potwierdza tożsamości wydawcy."),
      prototype::T(is_update ? u"Zaktualizuj" : u"Zainstaluj"),
      base::BindOnce(
          [](base::WeakPtr<WorkspaceLab> lab, size_t profile, bool is_update,
             std::string id, std::unique_ptr<WorkspaceExtensionInstall> install,
             bool accepted) {
            if (!lab || !lab->CanEditWorkspace() ||
                lab->WorkspaceProfileIndex(lab->active_index_) != profile) {
              return;
            }
            const bool probe = base::CommandLine::ForCurrentProcess()->HasSwitch(
                kExtensionInstallUiProbe);
            if (!accepted) {
              if (probe) {
                std::puts("Zitter Content cancelled the extension review");
                lab->Finish(true);
              }
              return;
            }
            auto* manager = lab->ManagedExtensionForProfile(*lab->contexts_[profile], id);
            if (manager && manager->uninstalling()) {
              lab->SetExtensionManagerStatus(
                  prototype::T(u"Trwa usuwanie tego rozszerzenia."), true);
              return;
            }
            lab->InstallReviewedExtension(profile, *install, is_update);
          },
          weak_factory_.GetWeakPtr(), profile, is_update, *review->FindString("id"),
          std::move(install)));
  const auto& command = *base::CommandLine::ForCurrentProcess();
  if (!command.HasSwitch(kExtensionInstallUiProbe))
    return;
  auto* prompt = shell_->danger_dialog_for_testing();
  auto* dialog = prompt ? prompt->widget_delegate()->AsDialogDelegate() : nullptr;
  if (!dialog || !prompt->IsVisible() ||
      prompt->color_mode_override() != widget_->color_mode_override() ||
      dialog->GetDefaultDialogButton() !=
          static_cast<int>(ui::mojom::DialogButton::kCancel)) {
    Finish(false);
    return;
  }
  if (command.HasSwitch(kExtensionInstallUiCancel))
    dialog->CancelDialog();
  else
    dialog->AcceptDialog();
}

void WorkspaceLab::InstallReviewedExtension(
    size_t profile, WorkspaceExtensionInstall& install, bool is_update) {
  auto extension = install.Publish();
  const bool probe = base::CommandLine::ForCurrentProcess()->HasSwitch(
      kExtensionInstallUiProbe);
  if (!extension) {
    SetExtensionManagerStatus(
        prototype::T(u"Nie udało się zainstalować rozszerzenia w tym profilu."),
        true);
    if (probe)
      Finish(false);
    return;
  }

  if (is_update) {
    SetExtensionManagerStatus(
        prototype::T(u"Aktualizacja rozszerzenia będzie aktywna po ponownym uruchomieniu Zittera."));
    if (probe) {
      std::puts("Zitter Content approved an extension update from Settings");
      Finish(true);
    }
    return;
  }

  auto& owner = *contexts_[profile];
  const auto id = extension->id();
  if (!RegisterInstalledExtension(owner, extension)) {
    OnInstalledExtension(profile, id, false);
    return;
  }
  auto manager = std::make_unique<ManagedExtension>(
      owner, id, base::BindRepeating(&WorkspaceLab::RefreshShell,
                                     weak_factory_.GetWeakPtr()));
  managed_extensions_.push_back(std::move(manager));
  auto title = base::UTF8ToUTF16(extension->name());
  base::i18n::SanitizeUserSuppliedString(&title);
  shell_->AddExtensionPreference("extension_enabled." + id, title);
  if (extensions::UserScriptManager::IsUserScriptsAPIPermissionAvailable(*extension)) {
    shell_->AddExtensionPreference("extension_user_scripts." + id,
                                   prototype::TF(u"Skrypty użytkownika: {}", {title}));
  }
  if (!managed_extensions_.back()->Start(base::BindOnce(
          &WorkspaceLab::OnInstalledExtension, weak_factory_.GetWeakPtr(),
          profile, id)) && !managed_extensions_.back()->failed()) {
    OnInstalledExtension(profile, id, false);
  }
}

void WorkspaceLab::OnInstalledExtension(size_t profile, std::string id,
                                         bool active) {
  auto& context = *contexts_[profile];
  auto* manager = ManagedExtensionForProfile(context, id);
  if (active && manager && manager->action_popup().is_valid() &&
      user_prefs::UserPrefs::Get(&context)->GetBoolean(kManagerAutoPinPref)) {
    SetManagerToolbarPin(*extensions::ExtensionPrefs::Get(&context), id, true);
    RefreshExtensionActionAppearance();
  }
  if (WorkspaceProfileIndex(active_index_) == profile) {
    SetExtensionManagerStatus(
        active
            ? prototype::T(u"Rozszerzenie jest zainstalowane i włączone.")
            : prototype::T(u"Rozszerzenie zapisano, ale nie udało się go uruchomić."),
        !active);
  }
  if (!base::CommandLine::ForCurrentProcess()->HasSwitch(kExtensionInstallUiProbe))
    return;
  auto& owner = *contexts_[profile];
  if (!active || !ManagedExtensionForProfile(owner, id) ||
      std::ranges::any_of(contexts_, [&](const auto& context) {
        return context != &owner && CanLoadExtensionProbeResources(*context, id);
      })) {
    LOG(ERROR) << "Settings did not activate the extension only in its owner profile";
    Finish(false);
    return;
  }
  CheckLiveExtensionInstall(id, false, base::TimeTicks::Now() + base::Seconds(12));
}

void WorkspaceLab::CheckLiveExtensionInstall(const std::string& id,
                                             bool navigated,
                                             base::TimeTicks deadline) {
  const auto url = session_probe_url_.Resolve("/extension-content/live-install");
  if (!navigated) {
    auto& owner = *WorkspaceProfile(active_index_);
    auto* manager = ManagedExtensionForProfile(owner, id);
    const std::string key = "extension_enabled." + id;
    auto* toggle = shell_->preference_switch_for_testing(key);
    if (!toggle || !toggle->GetEnabled() ||
        !shell_->preference_switch_checked_for_testing(key)) {
      LOG(ERROR) << "Settings did not expose an enabled switch for the installed extension";
      Finish(false);
      return;
    }
    const bool worker_running = manager && manager->RunningWorkerForTesting().has_value();
    if (worker_running && base::CommandLine::ForCurrentProcess()->HasSwitch(
                              "zitter-content-extension-publisher-worker-probe")) {
      std::puts("Zitter Content activated a publisher extension from Settings");
      Finish(true);
      return;
    }
    auto* loader = extensions::ExtensionSystem::Get(&owner)
                       ->user_script_manager()
                       ->GetUserScriptLoaderForExtension(id);
    if (worker_running && loader->initial_load_complete()) {
      if (base::CommandLine::ForCurrentProcess()->HasSwitch(
              "zitter-content-extension-publisher-overlay-probe")) {
        shell_->CloseOverlays(true);
        if (!Navigate(session_probe_url_.Resolve("/bitwarden-overlay"), true)) {
          Finish(false);
          return;
        }
        publisher_overlay_probe_page_ = web_contents()->GetWeakPtr();
        CheckPublisherOverlayForProbe(id, false, deadline);
        return;
      }
      navigated = Navigate(url, true);
      if (!navigated) {
        Finish(false);
        return;
      }
    }
  } else if (web_contents()->GetLastCommittedURL() == url &&
             web_contents()->GetTitle() == u"Live extension script verified isolated storage") {
    shell_->CloseOverlays(true);
    CheckExtensionPermissionDecision(id, 0, false,
                                     base::TimeTicks::Now() + base::Seconds(15));
    return;
  }
  if (base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "Live extension installation page did not finish: "
               << web_contents()->GetTitle();
    Finish(false);
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckLiveExtensionInstall,
                                weak_factory_.GetWeakPtr(), id, navigated,
                                deadline),
      base::Milliseconds(50));
}

void WorkspaceLab::CheckPublisherOverlayForProbe(const std::string& id, bool inserted,
                                                  base::TimeTicks deadline) {
  auto* page = publisher_overlay_probe_page_.get();
  if (!page || base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "Bitwarden sandbox overlay did not finish";
    if (page) {
      page->ForEachRenderFrameHost([](content::RenderFrameHost* frame) {
        LOG(ERROR) << "Overlay fixture frame " << frame->GetFrameName() << " at "
                   << frame->GetLastCommittedURL() << ", origin "
                   << frame->GetLastCommittedOrigin().Serialize() << ", error "
                   << frame->IsErrorDocument();
      });
    }
    Finish(false);
    return;
  }
  if (page->GetLastCommittedURL() == session_probe_url_.Resolve("/bitwarden-overlay")) {
    if (!inserted) {
      const auto configured = GetConfiguredExtensions(*page->GetBrowserContext());
      const auto extension = std::ranges::find_if(configured, [&](const auto& entry) {
        return entry->id() == id;
      });
      if (extension == configured.end()) {
        Finish(false);
        return;
      }
      const auto guid = base::UTF8ToUTF16((*extension)->guid());
      page->GetPrimaryMainFrame()->ExecuteJavaScriptForTests(
          base::StrCat({
              u"for (const [name, url] of [",
              u"['dynamic', 'chrome-extension://", guid, u"/overlay/menu-button.html'],",
              u"['canonical', 'chrome-extension://", base::UTF8ToUTF16(id),
              u"/overlay/menu-button.html'],",
              u"['bootstrap', 'chrome-extension://", guid, u"/overlay/menu.html'],",
              u"['canonical-bootstrap', 'chrome-extension://", base::UTF8ToUTF16(id),
              u"/overlay/menu.html']]) {",
              u"const frame = document.createElement('iframe'); frame.name = name;",
              u"frame.src = url; document.documentElement.append(frame); }"}),
          base::NullCallback(), content::ISOLATED_WORLD_ID_GLOBAL);
      inserted = true;
    } else {
      content::RenderFrameHost* overlay = nullptr;
      content::RenderFrameHost* bootstrap = nullptr;
      size_t blocked = 0;
      page->ForEachRenderFrameHost([&](content::RenderFrameHost* frame) {
        const auto& name = frame->GetFrameName();
        if (name == "dynamic" && !frame->IsErrorDocument() &&
            frame->GetLastCommittedURL().host() == id &&
            frame->GetLastCommittedURL().path() == "/overlay/menu-button.html") {
          overlay = frame;
        } else if (name == "bootstrap" && !frame->IsErrorDocument() &&
                   frame->GetLastCommittedURL().host() == id &&
                   frame->GetLastCommittedURL().path() == "/overlay/menu.html") {
          bootstrap = frame;
        } else if ((name == "canonical" || name == "canonical-bootstrap") &&
                   (frame->IsErrorDocument() ||
                    frame->GetLastCommittedURL() == GURL("about:blank#blocked"))) {
          ++blocked;
        }
      });
      auto* main = page->GetPrimaryMainFrame();
      if ((overlay && (!overlay->GetLastCommittedOrigin().opaque() ||
                       overlay->GetProcess() == main->GetProcess())) ||
          (bootstrap && (bootstrap->GetLastCommittedOrigin().opaque() ||
                         bootstrap->GetProcess() == main->GetProcess())) ||
          (overlay && bootstrap && overlay->GetProcess() == bootstrap->GetProcess())) {
        LOG(ERROR) << "Bitwarden overlay did not isolate its frame processes";
        Finish(false);
        return;
      }
      if (overlay && bootstrap && blocked == 2) {
        overlay->ExecuteJavaScriptForTests(
            u"(() => { let parentAccess = false; try { parentAccess = !!parent.document; } catch {}"
            u"return self.origin === 'null' && !parentAccess && !globalThis.chrome?.runtime &&"
            u"!!customElements.get('autofill-inline-menu-button'); })()",
            base::BindOnce(
                [](base::WeakPtr<WorkspaceLab> lab, std::string id,
                   base::TimeTicks deadline, base::Value result) {
                  if (!lab)
                    return;
                  if (result.GetIfBool().value_or(false)) {
                    std::puts("Zitter Content loaded the isolated Bitwarden web overlay");
                    lab->Finish(true);
                    return;
                  }
                  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
                      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckPublisherOverlayForProbe,
                                                lab, id, true, deadline),
                      base::Milliseconds(50));
                },
                weak_factory_.GetWeakPtr(), id, deadline),
            content::ISOLATED_WORLD_ID_GLOBAL);
        return;
      }
    }
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckPublisherOverlayForProbe,
                                weak_factory_.GetWeakPtr(), id, inserted, deadline),
      base::Milliseconds(50));
}

void WorkspaceLab::CheckExtensionPermissionDecision(
    const std::string& id, int step, bool dialog_handled,
    base::TimeTicks deadline) {
  auto* page = web_contents();
  const auto title = page->GetTitle();
  const bool ready_to_request =
      step == 0 ||
      (step == 1 && title == u"Optional permission cancelled" && dialog_handled) ||
      (step == 2 && title == u"Optional permission granted and removed" && dialog_handled);
  if (ready_to_request && !widget_->IsActive()) {
    // AppKit can finish closing Settings after an activation request.
    widget_->Activate();
    page->Focus();
  } else if (ready_to_request) {
    ++step;
    dialog_handled = false;
    page->GetPrimaryMainFrame()->ExecuteJavaScriptWithUserGestureForTests(
        step == 1 ? u"document.dispatchEvent(new Event('zitter-test-optional-cancel'));"
        : step == 2 ? u"document.dispatchEvent(new Event('zitter-test-optional-approve'));"
                    : u"document.dispatchEvent(new Event('zitter-test-optional-persist'));",
        base::NullCallback(), content::ISOLATED_WORLD_ID_GLOBAL);
  } else if (!dialog_handled && permission_prompt_widget_ &&
             !permission_prompt_widget_->IsClosed()) {
    dialog_handled = true;
    if (step == 1)
      permission_prompt_delegate_->CancelDialog();
    else
      permission_prompt_delegate_->AcceptDialog();
  } else if (step == 3 && dialog_handled &&
             title == u"Optional permission saved for restart") {
    std::puts("Zitter Content verified extension permission consent and revocation");
    auto* manager = ManagedExtensionForProfile(*WorkspaceProfile(active_index_), id);
    const auto worker = manager ? manager->RunningWorkerForTesting() : std::nullopt;
    if (!worker) {
      LOG(ERROR) << "The installation probe had no worker to stop";
      Finish(false);
      return;
    }
    manager->StopWorkerForTesting(base::BindOnce(
        &WorkspaceLab::WakeInstalledExtensionForProbe,
        weak_factory_.GetWeakPtr(), id, *worker));
    return;
  }
  if (base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "Extension permission decision did not finish at step "
               << step << " (dialog handled: " << dialog_handled << "): " << title;
    Finish(false);
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckExtensionPermissionDecision,
                                weak_factory_.GetWeakPtr(), id, step,
                                dialog_handled, deadline),
      base::Milliseconds(50));
}

void WorkspaceLab::WakeInstalledExtensionForProbe(
    const std::string& id,
    base::UnguessableToken prior_worker) {
  auto* manager = ManagedExtensionForProfile(*WorkspaceProfile(active_index_), id);
  if (!manager || manager->RunningWorkerForTesting()) {
    LOG(ERROR) << "The extension worker did not stop before the messaging probe";
    Finish(false);
    return;
  }
  web_contents()->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
      u"document.dispatchEvent(new Event('zitter-test-wake-worker'));",
      base::BindOnce(
          [](base::WeakPtr<WorkspaceLab> lab, std::string id,
             base::UnguessableToken prior_worker, base::Value) {
            if (lab) {
              lab->CheckInstalledExtensionWake(
                  id, prior_worker, base::TimeTicks::Now() + base::Seconds(12));
            }
          },
          weak_factory_.GetWeakPtr(), id, prior_worker),
      content::ISOLATED_WORLD_ID_CONTENT_END);
}

void WorkspaceLab::CheckInstalledExtensionWake(
    const std::string& id,
    base::UnguessableToken prior_worker,
    base::TimeTicks deadline) {
  auto* manager = ManagedExtensionForProfile(*WorkspaceProfile(active_index_), id);
  const auto worker = manager ? manager->RunningWorkerForTesting() : std::nullopt;
  if (worker && *worker != prior_worker &&
      web_contents()->GetTitle() == u"Message woke the installed worker") {
    std::puts("Zitter Content installed an extension from Settings");
    std::puts("Zitter Content started the installed extension without a restart");
    std::puts("Zitter Content woke a stopped extension worker with a runtime message");
    const size_t sender_tab = selected_[active_index_];
    if (!Navigate(session_probe_url_.Resolve("/extension-content/tab-message-target"), true)) {
      Finish(false);
      return;
    }
    CheckTabMessageTarget(id, sender_tab, base::TimeTicks::Now() + base::Seconds(10));
    return;
  }
  if (base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "The runtime message did not wake the extension worker: "
               << web_contents()->GetTitle();
    Finish(false);
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckInstalledExtensionWake,
                                weak_factory_.GetWeakPtr(), id, prior_worker,
                                deadline),
      base::Milliseconds(50));
}

void WorkspaceLab::CheckTabMessageTarget(const std::string& id,
                                         size_t sender_tab,
                                         base::TimeTicks deadline) {
  if (web_contents()->GetTitle() != u"Extension tab message target ready") {
    if (base::TimeTicks::Now() >= deadline) {
      LOG(ERROR) << "The extension tab message target did not load";
      Finish(false);
      return;
    }
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, base::BindOnce(&WorkspaceLab::CheckTabMessageTarget,
                                  weak_factory_.GetWeakPtr(), id, sender_tab, deadline),
        base::Milliseconds(50));
    return;
  }

  auto* target = web_contents();
  const size_t peer_index = 1 - active_index_;
  auto* peer = tabs_[peer_index][selected_[peer_index]].view->web_contents();
  if (!peer) {
    const std::string workspace = model_.active()->id;
    SelectWorkspace(model_.workspaces()[peer_index].id);
    peer = tabs_[peer_index][selected_[peer_index]].view->web_contents();
    SelectWorkspace(workspace);
  }
  const int tab = WorkspaceExtensionTabIdForTesting(*target);
  const auto document = extensions::ExtensionApiFrameIdMap::GetDocumentId(
      target->GetPrimaryMainFrame());
  const int peer_tab = peer ? WorkspaceExtensionTabIdForTesting(*peer) : -1;
  if (tab < 0 || peer_tab < 0 || !document) {
    LOG(ERROR) << "The extension tab messaging probe could not resolve its targets";
    Finish(false);
    return;
  }
  const auto details = base::WriteJson(base::DictValue()
      .Set("tab", tab)
      .Set("document", document.ToString())
      .Set("peer", peer_tab));
  CHECK(details);
  const size_t target_tab = selected_[active_index_];
  SelectTab(sender_tab);
  web_contents()->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(
          "document.dispatchEvent(new CustomEvent('zitter-test-tab-message', "
          "{detail: JSON.stringify(" + *details + ")}));"),
      base::BindOnce(
          [](base::WeakPtr<WorkspaceLab> lab, const std::string& id,
             size_t target_tab, int tab, base::Value) {
            if (lab)
              lab->CheckTabMessageReply(
                  id, target_tab, tab, base::TimeTicks::Now() + base::Seconds(15));
          },
          weak_factory_.GetWeakPtr(), id, target_tab, tab),
      content::ISOLATED_WORLD_ID_CONTENT_END);
}

void WorkspaceLab::CheckTabMessageReply(const std::string& id,
                                         size_t target_tab, int tab_id,
                                         base::TimeTicks deadline) {
  auto* page = web_contents();
  if (page->GetLastCommittedURL().path() == "/extension-content/api-tab-audio-state") {
    auto* frame = page->GetPrimaryMainFrame();
    if (page->GetTitle() == u"Audio fixture ready") {
      frame->ExecuteJavaScriptWithUserGestureForTests(
          u"window.startFixtureAudio();", base::DoNothing(),
          content::ISOLATED_WORLD_ID_GLOBAL);
    } else {
      const bool badge = shell_->row_has_audio_badge_for_testing(
          shell_->tab_button(selected_[active_index_]));
      if (page->GetTitle() == u"Audio fixture playing" &&
          page->IsCurrentlyAudible() && badge) {
        frame->ExecuteJavaScriptForTests(
            u"document.title = 'Audio fixture badge visible';",
            base::NullCallback(), content::ISOLATED_WORLD_ID_GLOBAL);
      } else if (page->GetTitle() == u"Audio fixture badge visible" &&
                 page->IsAudioMuted() && !badge) {
        frame->ExecuteJavaScriptForTests(
            u"window.stopFixtureAudio();", base::NullCallback(),
            content::ISOLATED_WORLD_ID_GLOBAL);
      }
    }
  }
  if (page->GetLastCommittedURL().path() == "/extension-content/api-tab-deferred") {
    if (page->GetTitle() == u"Extension tab bypassed cache" && page->IsAudioMuted()) {
      page->GetPrimaryMainFrame()->ExecuteJavaScriptForTests(
          u"document.title = 'Extension audio muted';", base::NullCallback(),
          content::ISOLATED_WORLD_ID_GLOBAL);
    } else if (page->GetTitle() == u"Extension native unmute requested") {
      if (page->IsAudioMuted()) {
        if (!ActivateTabMuteMenuForTesting(selected_[active_index_], false)) {
          LOG(ERROR) << "The native unmute action was unavailable";
          Finish(false);
          return;
        }
      } else {
        page->GetPrimaryMainFrame()->ExecuteJavaScriptForTests(
            u"document.title = 'Extension audio unmuted';", base::NullCallback(),
            content::ISOLATED_WORLD_ID_GLOBAL);
      }
    }
  }
  if (auto* dialogs = prototype::PageDialogs::FromWebContents(page);
      dialogs && dialogs->repost_dialog_open()) {
    auto* delegate = dialogs->dialog()->widget_delegate()->AsDialogDelegate();
    const auto cancel = static_cast<int>(ui::mojom::DialogButton::kCancel);
    const auto origin = page->GetPrimaryMainFrame()->GetLastCommittedOrigin().Serialize();
    if (extension_tab_repost_answers_ >= 2 ||
        delegate->GetDefaultDialogButton() != cancel ||
        delegate->GetWindowTitle() != prototype::TF(
            u"Wysłać formularz ponownie? · {}", {base::UTF8ToUTF16(origin)}) ||
        delegate->GetDialogButtonLabel(ui::mojom::DialogButton::kOk) !=
            prototype::T(u"Wyślij ponownie") ||
        dialogs->HandleJavaScriptDialog(page, true, nullptr)) {
      LOG(ERROR) << "The repost confirmation did not protect the native action";
      Finish(false);
      return;
    }
    if (extension_tab_repost_answers_++ == 0) {
      delegate->CancelDialog();
      page->GetPrimaryMainFrame()->ExecuteJavaScriptForTests(
          u"document.title = 'Extension repost cancelled';",
          base::NullCallback(), content::ISOLATED_WORLD_ID_GLOBAL);
    } else {
      delegate->AcceptDialog();
    }
    page = web_contents();
  }
  if (page->GetTitle() == u"Extension tab updated" &&
      page->GetLastCommittedURL().path() == "/extension-content/api-tab-update") {
    if (!extension_tab_close_answers_) {
      extension_tab_close_answers_ = 0;
      page->GetPrimaryMainFrame()->NotifyUserActivation(
          blink::mojom::UserActivationNotificationType::kTest);
      page->GetPrimaryMainFrame()->ExecuteJavaScriptForTests(
          u"document.title = 'Extension tab close ready';",
          base::NullCallback(), content::ISOLATED_WORLD_ID_GLOBAL);
    }
  } else if (page->GetTitle() == u"Extension tab close ready" &&
             extension_tab_close_answers_ && *extension_tab_close_answers_ < 2) {
    auto* dialogs = prototype::PageDialogs::FromWebContents(page);
    if (dialogs && dialogs->leave_dialog_open()) {
      const bool accept = (*extension_tab_close_answers_)++ != 0;
      if (!dialogs->HandleJavaScriptDialog(page, accept, nullptr)) {
        Finish(false);
        return;
      }
      page = web_contents();
    }
  }
  if (page->GetTitle() == u"Extension tab messaging verified") {
    if (extension_tab_close_answers_ != 2u ||
        extension_tab_repost_answers_ != 2u) {
      LOG(ERROR) << "The extension did not wait for both tab closure responses";
      Finish(false);
      return;
    }
    std::puts("Zitter Content routed extension tab messages and rejected foreign targets");
    auto* manager = ManagedExtensionForProfile(*WorkspaceProfile(active_index_), id);
    const auto worker = manager ? manager->RunningWorkerForTesting() : std::nullopt;
    if (!worker) {
      Finish(false);
      return;
    }
    const std::string detail = *base::WriteJson(
        base::DictValue().Set("tabId", tab_id).Set("windowId", 1));
    web_contents()->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
        base::UTF8ToUTF16(
            "document.dispatchEvent(new CustomEvent('zitter-test-tab-removed', "
            "{detail: JSON.stringify(" + detail + ")}));"),
        base::BindOnce(
            [](base::WeakPtr<WorkspaceLab> lab, const std::string& id,
               size_t target_tab, base::UnguessableToken prior_worker,
               base::Value) {
              if (!lab)
                return;
              auto* manager = lab->ManagedExtensionForProfile(
                  *lab->WorkspaceProfile(lab->active_index_), id);
              manager->StopWorkerForTesting(base::BindOnce(
                  [](base::WeakPtr<WorkspaceLab> lab, const std::string& id,
                     size_t target_tab, base::UnguessableToken prior_worker) {
                    if (!lab)
                      return;
                    auto* manager = lab->ManagedExtensionForProfile(
                        *lab->WorkspaceProfile(lab->active_index_), id);
                    if (!manager || manager->RunningWorkerForTesting()) {
                      lab->Finish(false);
                      return;
                    }
                    lab->CloseTab(target_tab);
                    lab->CheckRemovedTabEvent(
                        id, prior_worker, base::TimeTicks::Now() + base::Seconds(10));
                  },
                  lab, id, target_tab, prior_worker));
            },
            weak_factory_.GetWeakPtr(), id, target_tab, *worker),
        content::ISOLATED_WORLD_ID_CONTENT_END);
    return;
  }
  if (base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "The extension tab messaging probe failed: " << web_contents()->GetTitle();
    Finish(false);
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckTabMessageReply,
                                weak_factory_.GetWeakPtr(), id, target_tab,
                                tab_id, deadline),
      base::Milliseconds(50));
}

void WorkspaceLab::CheckRemovedTabEvent(
    const std::string& id, base::UnguessableToken prior_worker,
    base::TimeTicks deadline) {
  auto* manager = ManagedExtensionForProfile(*WorkspaceProfile(active_index_), id);
  const auto worker = manager ? manager->RunningWorkerForTesting() : std::nullopt;
  if (worker && *worker != prior_worker &&
      web_contents()->GetTitle() == u"Tab removal woke the installed worker") {
    std::puts("Zitter Content delivered tab lifecycle events to the installed worker");
    const size_t source = active_index_;
    const auto& workspaces = model_.workspaces();
    const auto sibling = std::ranges::find_if(workspaces, [&](const auto& workspace) {
      return workspace.id != workspaces[source].id &&
             workspace.profile_id == workspaces[source].profile_id;
    });
    const int tab_id = WorkspaceExtensionTabIdForTesting(*web_contents());
    if (sibling == workspaces.end() || tab_id < 0) {
      LOG(ERROR) << "The extension discard probe needs a same-profile workspace and tab";
      Finish(false);
      return;
    }
    SelectWorkspace(sibling->id);
    CheckExtensionTabDiscard(source, tab_id, false,
                             base::TimeTicks::Now() + base::Seconds(10));
    return;
  }
  if (base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "The tab removal event did not wake the extension worker";
    Finish(false);
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckRemovedTabEvent,
                                weak_factory_.GetWeakPtr(), id, prior_worker,
                                deadline),
      base::Milliseconds(50));
}

void WorkspaceLab::CheckExtensionTabDiscard(size_t source, int tab_id,
                                             bool sleeping,
                                             base::TimeTicks deadline) {
  const auto& title = web_contents()->GetTitle();
  if (!sleeping && title == u"Extension discard observer ready") {
    if (!CommitWorkspaceSleep(source)) {
      Finish(false);
      return;
    }
    web_contents()->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
        base::UTF8ToUTF16(
            "document.dispatchEvent(new CustomEvent('zitter-test-tab-discard', "
            "{detail: " + base::NumberToString(tab_id) + "}));"),
        base::NullCallback(), content::ISOLATED_WORLD_ID_CONTENT_END);
    sleeping = true;
  } else if (sleeping && title == u"Discarded extension tab verified") {
    SelectWorkspace(model_.workspaces()[source].id);
    if (active_index_ != source || sleeping_[source] ||
        WorkspaceExtensionTabIdForTesting(*web_contents()) != tab_id ||
        !web_contents()->IsAudioMuted()) {
      LOG(ERROR) << "The restored extension tab lost its ID or mute state";
      Finish(false);
      return;
    }
    std::puts("Zitter Content kept the extension tab ID across Workspace sleep");
    auto& profile = *WorkspaceProfile(active_index_);
    const auto manager = std::ranges::find_if(managed_extensions_, [&](const auto& item) {
      return item->OwnsProfile(profile) && item->options_page().is_valid();
    });
    if (manager == managed_extensions_.end()) {
      Finish(false);
      return;
    }
    const GURL url = (*manager)->options_page();
    const auto original_workspace = model_.workspaces()[active_index_].id;
    const auto show_options = [&] {
      auto title = base::UTF8ToUTF16((*manager)->name());
      base::i18n::SanitizeUserSuppliedString(&title);
      shell_->preferences_search()->SetText(title);
      shell_->OpenGeneralSettings();
      return shell_->extension_options_for_testing((*manager)->id());
    };
    for (const auto& workspace : model_.workspaces()) {
      if (workspace.profile_id != model_.workspaces()[source].profile_id) {
        SelectWorkspace(workspace.id);
        if (show_options()->parent()->GetVisible()) {
          LOG(ERROR) << "Extension options appeared in another profile";
          Finish(false);
          return;
        }
        break;
      }
    }
    SelectWorkspace(original_workspace);
    show_options();
    ClickExtensionOptionsForProbe((*manager)->id(), url,
                                  base::TimeTicks::Now() + base::Seconds(5));
    return;
  }
  if (base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "The extension discard probe did not finish: " << title;
    Finish(false);
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckExtensionTabDiscard,
                                weak_factory_.GetWeakPtr(), source, tab_id,
                                sleeping, deadline),
      base::Milliseconds(50));
}

void WorkspaceLab::ClickExtensionOptionsForProbe(
    std::string id, GURL url, base::TimeTicks deadline) {
  auto* options = shell_->extension_options_for_testing(id);
  auto* settings = options->GetWidget();
  if (!settings->IsVisible())
    shell_->OpenGeneralSettings();
  settings->LayoutRootViewIfNecessary();
  if (settings->IsVisible() && options->IsDrawn() && options->GetEnabled())
    options->ScrollViewToVisible();
  const auto bounds = options->GetVisibleBounds();
  if (!settings->IsVisible() || !CanEditWorkspace() || bounds.IsEmpty() ||
      !options->GetEnabled()) {
    if (base::TimeTicks::Now() >= deadline) {
      LOG(ERROR) << "Extension options Settings state: visible=" << settings->IsVisible()
                 << ", editable=" << CanEditWorkspace()
                 << ", button enabled=" << options->GetEnabled();
      for (auto* view = options; view; view = view->parent())
        LOG(ERROR) << view->GetClassName() << " " << view->bounds().ToString()
                   << " visible=" << view->GetVisible();
      Finish(false);
      return;
    }
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, base::BindOnce(&WorkspaceLab::ClickExtensionOptionsForProbe,
                                  weak_factory_.GetWeakPtr(), std::move(id),
                                  std::move(url), deadline),
        base::Milliseconds(50));
    return;
  }
  const size_t before = tabs_[active_index_].size();
  ui::AXActionData action;
  action.action = ax::mojom::Action::kDoDefault;
  if (!options->HandleAccessibleAction(action) ||
      tabs_[active_index_].size() != before + 1) {
    LOG(ERROR) << "The extension options action did not create a tab";
    Finish(false);
    return;
  }
  CheckExtensionOptionsPage(std::move(url), base::TimeTicks::Now() + base::Seconds(10));
}

void WorkspaceLab::CheckExtensionOptionsPage(GURL url, base::TimeTicks deadline) {
  auto* page = web_contents();
  auto* frame = page ? page->GetPrimaryMainFrame() : nullptr;
  if (frame && page->GetLastCommittedURL() == url &&
      frame->GetLastCommittedOrigin() == url::Origin::Create(url) &&
      frame->GetSiteInstance()->RequiresDedicatedProcess() &&
      page->GetTitle() == u"Zitter extension options verified storage") {
    std::puts("Zitter Content opened extension options only in their owning profile");
    frame->ExecuteJavaScriptForTests(
        u"chrome.runtime.sendMessage({probe: 'open-options'}).then("
        u"reply => document.title = reply, error => document.title = String(error));",
        base::NullCallback(), content::ISOLATED_WORLD_ID_GLOBAL);
    CheckRuntimeOptionsPage(std::move(url), tabs_[active_index_].size(),
                            base::TimeTicks::Now() + base::Seconds(10));
    return;
  }
  if (base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "The extension options page did not verify its storage: expected "
               << url << ", committed " << (page ? page->GetLastCommittedURL() : GURL())
               << ", title " << (page ? page->GetTitle() : std::u16string())
               << ", origin " << (frame ? frame->GetLastCommittedOrigin().Serialize() : "")
               << ", dedicated " << (frame && frame->GetSiteInstance()->RequiresDedicatedProcess());
    Finish(false);
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckExtensionOptionsPage,
                                weak_factory_.GetWeakPtr(), std::move(url), deadline),
      base::Milliseconds(50));
}

void WorkspaceLab::CheckRuntimeOptionsPage(
    GURL url, size_t tab_count, base::TimeTicks deadline) {
  auto* page = web_contents();
  if (page && page->GetLastCommittedURL() == url &&
      page->GetTitle() == u"Worker opened its extension options" &&
      tabs_[active_index_].size() == tab_count) {
    std::puts("Zitter Content reused the options tab for runtime.openOptionsPage");
    auto& profile = *WorkspaceProfile(active_index_);
    const auto manager = std::ranges::find_if(managed_extensions_, [&](const auto& item) {
      return item->OwnsProfile(profile) && item->action_popup().is_valid();
    });
    if (manager == managed_extensions_.end()) {
      Finish(false);
      return;
    }
    const auto popup_url = (*manager)->action_popup();
    shell_->OpenGeneralSettings();
    auto* popup = shell_->extension_popup_for_testing((*manager)->id());
    popup->GetWidget()->LayoutRootViewIfNecessary();
    popup->ScrollViewToVisible();
    if (!popup->IsDrawn() || !popup->GetEnabled() ||
        !shell_->ExtensionActionAppearanceForTesting((*manager)->id(), u"OK")) {
      LOG(ERROR) << "The extension action or its updated appearance is unavailable in Settings";
      Finish(false);
      return;
    }
    shell_->CloseOverlays(true);
    GURL::Replacements host;
    host.SetHostStr("active-tab.localhost");
    const auto target =
        session_probe_url_.ReplaceComponents(host).Resolve("/extension-active-tab");
    if (!Navigate(target, /*new_tab=*/true)) {
      Finish(false);
      return;
    }
    CheckActiveTabActionProbe(target, popup_url,
                             base::TimeTicks::Now() + base::Seconds(10));
    return;
  }
  if (base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "The worker did not reuse its options tab: "
               << (page ? page->GetTitle() : std::u16string());
    Finish(false);
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckRuntimeOptionsPage,
                                weak_factory_.GetWeakPtr(), std::move(url),
                                tab_count, deadline),
      base::Milliseconds(50));
}

void WorkspaceLab::CheckActiveTabActionProbe(
    GURL target, GURL popup, base::TimeTicks deadline) {
  auto* page = web_contents();
  if (page && page->GetLastCommittedURL() == target &&
      page->GetTitle() == u"Active tab fixture") {
    auto& profile = *WorkspaceProfile(active_index_);
    const auto extensions = GetConfiguredExtensions(profile);
    const auto extension = std::ranges::find(extensions, popup.host(),
                                              &extensions::Extension::id);
    const int tab_id = WorkspaceExtensionTabIdForTesting(*page);
    if (extension == extensions.end() ||
        (*extension)->permissions_data()->CanAccessPage(target, tab_id, nullptr)) {
      LOG(ERROR) << "The extension already had access before the user action";
      Finish(false);
      return;
    }
    const ui::Accelerator action_key(ui::VKEY_Z, ui::EF_ALT_DOWN | ui::EF_SHIFT_DOWN);
    if (shell_->general_widget()->IsVisible() ||
        !extension_shortcuts_.contains(action_key) ||
        !DispatchExtensionShortcut(action_key) || !extension_popup_view_) {
      LOG(ERROR) << "The extension action shortcut did not open its popup";
      Finish(false);
      return;
    }
    CheckExtensionPopup(std::move(popup), base::TimeTicks::Now() + base::Seconds(10));
    return;
  }
  if (base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "The activeTab fixture did not load";
    Finish(false);
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckActiveTabActionProbe,
                                weak_factory_.GetWeakPtr(), std::move(target),
                                std::move(popup), deadline),
      base::Milliseconds(50));
}

void WorkspaceLab::CheckExtensionPopup(GURL url, base::TimeTicks deadline) {
  auto* page = extension_popup_widget_ && !extension_popup_widget_->IsClosed() &&
                       extension_popup_view_
                   ? extension_popup_view_->web_contents()
                   : nullptr;
  auto* frame = page ? page->GetPrimaryMainFrame() : nullptr;
  const bool owning_popup = frame && page->GetLastCommittedURL() == url &&
      frame->GetLastCommittedOrigin() == url::Origin::Create(url) &&
      frame->GetSiteInstance()->RequiresDedicatedProcess() &&
      page->GetBrowserContext() == WorkspaceProfile(active_index_);
  auto* active_view = static_cast<WorkspaceWebView*>(
      tabs_[active_index_][selected_[active_index_]].view.get());
  const int tab_id = WorkspaceExtensionTabIdForTesting(*active_view->web_contents());
  if (owning_popup && page->GetTitle() == u"Zitter extension popup verified storage" &&
      !extension_side_panel_probe_open_requested_) {
    extension_side_panel_probe_open_requested_ = true;
    frame->ExecuteJavaScriptWithUserGestureForTests(
        base::StrCat({u"chrome.sidePanel.open({tabId:", base::NumberToString16(tab_id),
                      u"}).then(() => document.title = 'Extension side panel open requested',"
                      u" error => document.title = String(error));"}),
        base::NullCallback(), content::ISOLATED_WORLD_ID_GLOBAL);
  }
  auto* panel = active_view->extension_side_panel_for_testing();
  if (owning_popup && page->GetTitle() == u"Extension side panel open requested" && panel &&
      panel->GetTitle() == u"Zitter extension script executed" &&
      panel->GetLastCommittedURL() == url.GetWithoutRef() &&
      panel->GetBrowserContext() == WorkspaceProfile(active_index_) &&
      !extension_side_panel_probe_close_requested_) {
    extension_side_panel_probe_close_requested_ = true;
    frame->ExecuteJavaScriptForTests(
        base::StrCat({u"chrome.sidePanel.close({tabId:", base::NumberToString16(tab_id),
                      u"}).then(() => document.title = 'Extension side panel closed',"
                      u" error => document.title = String(error));"}),
        base::NullCallback(), content::ISOLATED_WORLD_ID_GLOBAL);
  }
  if (owning_popup && page->GetTitle() == u"Extension side panel closed" && !panel &&
      extension_side_panel_probe_close_requested_) {
    std::puts("Zitter Content opened and closed the extension side panel in its owning tab");
    CloseExtensionPopup();
    std::puts("Zitter Content opened the extension action popup in its owning profile");
    if (!Navigate(session_probe_url_.Resolve("/extension-content/public-resource"),
                  /*new_tab=*/false)) {
      Finish(false);
      return;
    }
    CheckWebAccessibleExtensionResources(base::TimeTicks::Now() + base::Seconds(10));
    return;
  }
  if (base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "The extension popup did not verify its storage: "
               << (page ? page->GetTitle() : std::u16string());
    Finish(false);
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckExtensionPopup,
                                weak_factory_.GetWeakPtr(), std::move(url), deadline),
      base::Milliseconds(50));
}

void WorkspaceLab::CheckWebAccessibleExtensionResources(base::TimeTicks deadline) {
  const auto& title = web_contents()->GetTitle();
  if (title == u"Public extension resource policy verified" &&
      !extension_menu_probe_dispatched_) {
    auto* page = web_contents();
    const int tab_id = WorkspaceExtensionTabIdForTesting(*page);
    if (std::ranges::any_of(GetConfiguredExtensions(*WorkspaceProfile(active_index_)),
                           [&](const auto& extension) {
          return extension->permissions_data()->HasTabPermissionsForSecurityOrigin(
              tab_id, GURL("http://active-tab.localhost/"));
        })) {
      LOG(ERROR) << "The activeTab grant survived a cross-origin navigation";
      Finish(false);
      return;
    }
    std::puts("Zitter Content revoked activeTab after cross-origin navigation");
    content::ContextMenuParams params;
    content::RenderFrameHost* menu_frame = nullptr;
    page->ForEachRenderFrameHost([&](content::RenderFrameHost* frame) {
      if (frame->IsActive() && frame->GetLastCommittedURL().host() == "menu-frame.localhost")
        menu_frame = frame;
    });
    if (!menu_frame || !ShowPageMenu(*menu_frame, params)) {
      Finish(false);
      return;
    }
    auto* menu = shell_->page_context_menu_for_testing();
    for (size_t index = 0; index < menu->GetItemCount(); ++index) {
      if (menu->GetLabelAt(index) == u"Zitter extension menu") {
        auto* child = menu->GetSubmenuModelAt(index);
        if (!child || child->GetItemCount() != 3 ||
            child->GetLabelAt(0) != u"Verify extension menu" ||
            child->GetTypeAt(1) != ui::MenuModel::TYPE_SEPARATOR ||
            child->GetLabelAt(2) != u"Unavailable action" || child->IsEnabledAt(2)) {
          Finish(false);
          return;
        }
        extension_menu_probe_dispatched_ = true;
        std::puts("Zitter Content verified web-accessible extension resources");
        child->ActivatedAt(0);
        shell_->CloseOverlays(true);
        break;
      }
    }
  }
  if (title == u"Extension context menu verified") {
    std::puts("Zitter Content verified extension context menu dispatch");
    GURL::Replacements host;
    host.SetHostStr("active-tab-command.localhost");
    const auto target =
        session_probe_url_.ReplaceComponents(host).Resolve("/extension-active-tab");
    if (!Navigate(target, /*new_tab=*/false)) {
      Finish(false);
      return;
    }
    CheckExtensionCommandGrant(target, base::TimeTicks::Now() + base::Seconds(10));
    return;
  }
  if (base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "The web-accessible extension resource probe failed: " << title;
    Finish(false);
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckWebAccessibleExtensionResources,
                                weak_factory_.GetWeakPtr(), deadline),
      base::Milliseconds(50));
}

void WorkspaceLab::CheckExtensionCommandGrant(GURL target, base::TimeTicks deadline) {
  auto* page = web_contents();
  if (page && page->GetLastCommittedURL() == target) {
    if (extension_command_probe_dispatched_ &&
        page->GetTitle() == u"Extension command grant verified") {
      std::puts("Zitter Content verified activeTab for a named extension command");
      Finish(true);
      return;
    }
    if (!extension_command_probe_dispatched_ && page->GetTitle() == u"Active tab fixture") {
      const int tab_id = WorkspaceExtensionTabIdForTesting(*page);
      const auto extensions = GetConfiguredExtensions(*WorkspaceProfile(active_index_));
      if (std::ranges::any_of(extensions, [&](const auto& extension) {
            return extension->permissions_data()->CanAccessPage(target, tab_id, nullptr);
          })) {
        LOG(ERROR) << "The command fixture already had access before the shortcut";
        Finish(false);
        return;
      }
      const ui::Accelerator key(ui::VKEY_Y, ui::EF_ALT_DOWN | ui::EF_SHIFT_DOWN);
      if (!extension_shortcuts_.contains(key) || !DispatchExtensionShortcut(key)) {
        LOG(ERROR) << "The extension command was not assigned to the owning profile";
        Finish(false);
        return;
      }
      extension_command_probe_dispatched_ = true;
    }
  }
  if (base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "The named extension command did not use its activeTab grant";
    Finish(false);
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckExtensionCommandGrant,
                                weak_factory_.GetWeakPtr(), std::move(target), deadline),
      base::Milliseconds(50));
}

bool WorkspaceLab::ToggleExtensionFromSettingsForProbe(ProfileContext& owner,
                                                        bool enabled,
                                                        std::string_view id,
                                                        bool user_scripts) {
  std::optional<size_t> owner_index;
  for (size_t index = 0; index < model_.workspaces().size(); ++index) {
    if (WorkspaceProfile(index) == &owner) {
      owner_index = index;
      break;
    }
  }
  if (!owner_index)
    return false;
  const auto original_workspace = model_.active()->id;
  const auto* manager = ManagedExtensionForProfile(owner, id);
  const auto key = manager
                       ? (user_scripts ? "extension_user_scripts." : "extension_enabled.") +
                             manager->id()
                       : "extension_enabled";
  const auto title = manager ? base::UTF8ToUTF16(manager->name())
                             : prototype::T(u"Rozszerzenie profilu");
  const auto show_toggle = [&](size_t index) -> views::View* {
    SelectWorkspace(model_.workspaces()[index].id);
    if (active_index_ != index)
      return nullptr;
    shell_->OpenGeneralSettings();
    auto* search = shell_->preferences_search();
    search->SelectRange(gfx::Range(0, search->GetText().size()));
    search->input_client()->InsertText(
        title,
        ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);
    auto* control = shell_->preference_switch_for_testing(key);
    if (!control || !control->IsDrawn())
      return nullptr;
    control->GetWidget()->LayoutRootViewIfNecessary();
    control->ScrollViewToVisible();
    return control;
  };
  bool isolated = true;
  for (size_t index = 0; index < model_.workspaces().size(); ++index) {
    auto& profile = *WorkspaceProfile(index);
    if (&profile == &owner)
      continue;
    const auto* control = show_toggle(index);
    const auto* peer = ManagedExtensionForProfile(profile, id);
    const bool peer_editable =
        peer && peer->editable() &&
        (!user_scripts || (peer->enabled() && peer->UserScriptsAllowed().has_value()));
    isolated &= control && control->GetEnabled() == peer_editable;
  }
  auto* control = show_toggle(*owner_index);
  ui::AXActionData action;
  action.action = ax::mojom::Action::kDoDefault;
  const bool changed =
      isolated && control && control->GetEnabled() &&
      control->GetWidget()->color_mode_override() == widget_->color_mode_override() &&
      shell_->preference_switch_checked_for_testing(key) != enabled &&
      control->HandleAccessibleAction(action) &&
      shell_->preference_switch_checked_for_testing(key) == enabled;
  shell_->CloseOverlays(true);
  SelectWorkspace(original_workspace);
  if (changed) {
    std::puts("Zitter Content changed the active profile extension through Settings");
    std::fflush(stdout);
  } else {
    LOG(ERROR) << "Extension Settings did not reflect the active profile";
  }
  return changed;
}

void WorkspaceLab::FinishExtensionProbe(bool success) {
  if (!extension_probes_pending_)
    return;
  if (success && --extension_probes_pending_) {
    extension_probes_[1]->ContinueAfterPeerDisabled();
    return;
  }
  extension_probes_pending_ = 0;
  for (auto& probe : extension_probes_)
    probe.reset();
  if (!success) {
    Finish(false);
    return;
  }
  SelectWorkspace(std::string(kWorkspaceIds[1]));
  RequestClose();
}

void WorkspaceLab::ConfirmClearDataProbe() {
  if (clearing_profile_ || shell_->clear_data_dialog_for_testing()) {
    Finish(false);
    return;
  }
  OpenClearDataDialog(widget_.get());
  auto* dialog = shell_->clear_data_dialog_for_testing();
  if (!dialog || !dialog->IsVisible() ||
      dialog->color_mode_override() != widget_->color_mode_override()) {
    Finish(false);
    return;
  }
  const auto phase = base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
      kWorkspaceClearDataProbe);
  const auto download_phase =
      base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(kWorkspaceDownloadProbe);
  const bool clear_sites = phase == "site-recent" || phase == "site-all";
  const bool clear_downloads = download_phase == "clear-recent" || download_phase == "clear-all";
  const bool clear_passwords = phase == "password-recent" || phase == "password-all" ||
                               phase == "password-cancel";
  if (clear_sites || clear_downloads || clear_passwords) {
    shell_->SetClearDataAdvancedForTesting(true);
    shell_->SetClearDataPeriodForTesting(
        phase == "site-recent" || phase == "password-recent" ||
                download_phase == "clear-recent" ? 0 : 4);
    for (size_t kind = 0; kind < 3; ++kind)
      shell_->SetClearDataChoiceForTesting(kind, false);
    shell_->SetClearDataChoiceForTesting(clear_sites ? 6 : clear_passwords ? 4 : 3, true);
    if (!dialog->widget_delegate()->AsDialogDelegate()->IsDialogButtonEnabled(
            ui::mojom::DialogButton::kOk)) {
      Finish(false);
      return;
    }
  }
  if (phase == "password-cancel") {
    dialog->widget_delegate()->AsDialogDelegate()->CancelDialog();
    std::puts("Zitter Content cancelled password clearing");
    RequestClose();
    return;
  }
  clear_data_probe_pending_ = true;
  dialog->widget_delegate()->AsDialogDelegate()->AcceptDialog();
}

void WorkspaceLab::OpenAddressHistoryProbe() {
  shell_->OpenCommandPalette();
  shell_->address_input()->input_client()->InsertText(
      kAddressProbeQuery,
      ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);
  if (shell_->address_input()->GetText() != kAddressProbeQuery ||
      !shell_->palette_loading_for_testing()) {
    LOG(ERROR) << "Address history probe did not start a search";
    Finish(false);
    return;
  }
  CheckAddressHistoryProbe(base::TimeTicks::Now() + base::Seconds(5));
}

void WorkspaceLab::CheckAddressHistoryProbe(base::TimeTicks deadline) {
  const size_t profile = active_index_;
  const auto& results = shell_->search_results();
  const auto expected = session_probe_url_.Resolve(
      kProbePaths[profile][kProbeSelection[profile]]);
  const bool correct_results =
      smoke_ ? results.empty() && !contexts_[profile]->GetHistory()
             : results.size() == 1 && results.front().url == expected &&
                   results.front().title == kProbeTitles[profile] &&
                   std::ranges::any_of(shell_->suggestions(), [&](const auto& row) {
                     return row == results.front();
                   });
  if (!shell_->address_visible() || shell_->palette_loading_for_testing() ||
      !correct_results) {
    if (base::TimeTicks::Now() >= deadline) {
      LOG(ERROR) << "Address history probe did not finish for profile "
                 << profile << ", results=" << results.size()
                 << ", visible=" << shell_->address_visible()
                 << ", loading=" << shell_->palette_loading_for_testing();
      Finish(false);
      return;
    }
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&WorkspaceLab::CheckAddressHistoryProbe,
                       weak_factory_.GetWeakPtr(), deadline),
        base::Milliseconds(50));
    return;
  }
  if (smoke_) {
    std::puts("Zitter Content private address history stayed empty");
    Finish(true);
    return;
  }
  if (profile == 0) {
    SearchAddress(kAddressProbeQuery);
    SelectWorkspace(std::string(kWorkspaceIds[1]));
    if (active_index_ != 1 || shell_->address_visible() ||
        !shell_->search_results().empty()) {
      Finish(false);
      return;
    }
    OpenAddressHistoryProbe();
    return;
  }
  std::puts("Zitter Content address history profile isolation passed");
  shell_->CloseOverlays(true);
  RequestClose();
}

void WorkspaceLab::CheckFaviconProbe(size_t profile, base::TimeTicks deadline,
                                     bool refresh_started) {
  constexpr std::array<uint32_t, 2> kColors = {0xff303030, 0xffc0c0c0};
  const auto phase = base::CommandLine::ForCurrentProcess()
      ->GetSwitchValueASCII("zitter-content-favicon-probe");
  const bool refreshed = profile == 0 && (phase == "refreshed" || refresh_started);
  const auto color = refreshed ? 0xff707070u : kColors[profile];
  const auto url = session_probe_url_.Resolve(kProbePaths[profile][kProbeSelection[profile]]);
  const size_t deferred = 1 - kProbeSelection[profile];
  const auto deferred_url = session_probe_url_.Resolve(kProbePaths[profile][deferred]);
  const auto pin_url = session_probe_url_.Resolve("/shared-icon-pin");
  if (active_index_ == profile &&
      shell_->favicon_pixel_for_testing(url.spec()) == color &&
      (phase.empty() ||
       (!tabs_[profile][deferred].view->web_contents() &&
        shell_->favicon_pixel_for_testing(deferred_url.spec()) == color &&
        shell_->favicon_pixel_for_testing(pin_url.spec()) == color))) {
    if (profile == 0 && phase == "refresh" && !refresh_started) {
      if (!SubmitProbeCommand(u"> refresh all favicons", "reload_workspace_icons")) {
        LOG(ERROR) << "Could not activate the favicon refresh command";
        Finish(false);
        return;
      }
      refresh_started = true;
    } else if (profile == 0) {
      SelectWorkspace(std::string(kWorkspaceIds[1]));
      CheckFaviconProbe(1, deadline);
      return;
    } else {
      std::puts("Zitter Content verified profile tab favicons");
      RequestClose();
      return;
    }
  }
  if (base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "Tab favicon missing or wrong for profile " << profile
               << ", refresh started " << refresh_started;
    Finish(false);
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckFaviconProbe,
                               weak_factory_.GetWeakPtr(), profile, deadline,
                               refresh_started),
      base::Milliseconds(25));
}

void WorkspaceLab::CheckHistoryPanelProbe(base::TimeTicks deadline) {
  const size_t profile = active_index_;
  const auto expected = session_probe_url_.Resolve(
      kProbePaths[profile][kProbeSelection[profile]]);
  if (!history_panel_ || !history_panel_->widget()) {
    Finish(false);
    return;
  }
  const auto visits = history_panel_->listed_for_testing();
  const bool querying = history_panel_->querying_for_testing();
  const bool legacy_history = base::CommandLine::ForCurrentProcess()
                                  ->GetSwitchValueASCII(kWorkspaceHistoryPanelProbe) == "legacy";
  const auto legacy_visit = session_probe_url_.Resolve(
      profile == 0 ? "/legacy/history/work" : "/legacy/history/personal");
  const auto other_legacy_visit = session_probe_url_.Resolve(
      profile == 0 ? "/legacy/history/personal" : "/legacy/history/work");
  const std::u16string_view legacy_title =
      profile == 0 ? u"Legacy Work visit" : u"Legacy Personal visit";
  const bool correct_visits = legacy_history
      ? std::ranges::contains(visits, expected) &&
            std::ranges::contains(visits, legacy_visit) &&
            !std::ranges::contains(visits, other_legacy_visit)
      : visits == std::vector<GURL>{expected};
  const bool visible = history_panel_->widget()->IsVisible();
  const bool correct_theme = history_panel_->widget()->color_mode_override() ==
                             widget_->color_mode_override();
  const bool contains_title =
      history_panel_->has_visible_visit_title_for_testing(kProbeTitles[profile]) &&
      (!legacy_history || history_panel_->has_visible_visit_title_for_testing(legacy_title));
  const bool contains_metal_title =
      !metal_ || history_panel_->scene_contains_text_for_testing(
                     base::UTF16ToUTF8(kProbeTitles[profile]));
  const bool contains_legacy_metal_title = !metal_ || !legacy_history ||
      history_panel_->scene_contains_text_for_testing(base::UTF16ToUTF8(legacy_title));
  if (querying || !correct_visits || !visible || !correct_theme ||
      !contains_title || !contains_metal_title || !contains_legacy_metal_title) {
    if (base::TimeTicks::Now() >= deadline) {
      LOG(ERROR) << "History panel probe did not display the active profile "
                 << profile << ": querying=" << querying
                 << ", correct_visits=" << correct_visits
                 << ", visit_count=" << visits.size() << ", visible=" << visible
                 << ", correct_theme=" << correct_theme
                 << ", contains_title=" << contains_title
                 << ", contains_metal_title=" << contains_metal_title;
      Finish(false);
      return;
    }
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&WorkspaceLab::CheckHistoryPanelProbe,
                       weak_factory_.GetWeakPtr(), deadline),
        base::Milliseconds(50));
    return;
  }
  if (profile == 0) {
    history_panel_->SetQuery(u"Retired Work query");
    if (!RunShortcut("history") || !history_panel_->querying_for_testing()) {
      Finish(false);
      return;
    }
    SelectWorkspace(std::string(kWorkspaceIds[1]));
    if (history_panel_ || active_index_ != 1 || !RunShortcut("history")) {
      Finish(false);
      return;
    }
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&WorkspaceLab::CheckHistoryPanelProbe,
                       weak_factory_.GetWeakPtr(),
                       base::TimeTicks::Now() + base::Seconds(5)),
        base::Milliseconds(200));
    return;
  }
  std::puts("Zitter Content history panel profile isolation passed");
  history_panel_.reset();
  RequestClose();
}

void WorkspaceLab::DocumentOnLoadCompletedInPrimaryMainFrame() {
  if (!tls_probe_expect_trusted_ && tls_probe_url_.is_valid() &&
      web_contents()->GetLastCommittedURL() == tls_probe_url_) {
    web_contents()->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
        uR"JS((() => {
          const page = document.querySelector('main#certificate-error');
          const heading = page?.querySelector('h1');
          const bounds = heading?.getBoundingClientRect();
          return document.title === 'Certificate could not be verified' &&
              heading?.textContent === document.title &&
              bounds?.width > 0 && bounds?.height > 0 &&
              bounds.left >= 0 && bounds.right <= innerWidth &&
              page.querySelector('code')?.textContent === 'ERR_CERT_AUTHORITY_INVALID' &&
              !document.querySelector('a, button, form, script');
        })())JS",
        base::BindOnce([](base::WeakPtr<WorkspaceLab> lab, base::Value result) {
          if (!lab)
            return;
          const bool passed = result.GetIfBool().value_or(false);
          if (passed)
            std::puts("Zitter Content rejected an untrusted TLS certificate");
          lab->Finish(passed);
        }, weak_factory_.GetWeakPtr()),
        content::ISOLATED_WORLD_ID_CONTENT_END);
    return;
  }
  if (fingerprint_probe_step_ || download_probe_active_)
    return;
  if (!site_permission_probe_phase_.empty()) {
    CheckSitePermissionProbe();
    return;
  }
  if (!password_probe_action_.empty()) {
    AdvancePasswordProbe();
    return;
  }
  if (session_probe_url_.is_valid()) {
    CheckSessionProbePage();
    return;
  }
  if (!smoke_ || !web_contents()->GetLastCommittedURL().SchemeIs("data"))
    return;

  const base::Process& renderer =
      web_contents()->GetPrimaryMainFrame()->GetProcess()->GetProcess();
  if (web_contents()->GetBrowserContext() != WorkspaceProfile(active_index_) ||
      web_contents()->GetTitle() != base::UTF8ToUTF16(kTitles[active_index_]) ||
      !renderer.IsValid() || renderer.Pid() == base::GetCurrentProcId() ||
      renderer.Pid() == work_renderer_) {
    Finish(false);
    return;
  }

#if BUILDFLAG(IS_MAC)
  std::printf("Zitter Content renderer helper: %s\n",
              base::GetProcessExecutablePath(renderer.Handle()).value().c_str());
#endif
  Observe(nullptr);
  auto* runner = base::SingleThreadTaskRunner::GetCurrentDefault().get();
  if (active_index_ == 0) {
    work_renderer_ = renderer.Pid();
    runner->PostTask(FROM_HERE,
                     base::BindOnce(&WorkspaceLab::SwitchTo,
                                    weak_factory_.GetWeakPtr(), 1));
    return;
  }

  runner->PostTask(FROM_HERE,
                   base::BindOnce(&WorkspaceLab::CheckPresentation,
                                  weak_factory_.GetWeakPtr(),
                                  base::TimeTicks::Now() + base::Seconds(5)));
}

void WorkspaceLab::CheckPresentation(base::TimeTicks deadline) {
  const bool metal_text =
      shell_->metal_sidebar_contains_text_for_testing(kTitles[active_index_]);
  if (metal_ && !metal_text && base::TimeTicks::Now() < deadline) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&WorkspaceLab::CheckPresentation,
                       weak_factory_.GetWeakPtr(), deadline),
        base::Milliseconds(50));
    return;
  }
  const auto* work_view = tabs_[0][0].view.get();
  const auto* personal_view = tabs_[1][0].view.get();
  auto* webauthn = content::GetContentClientForTesting()
                       ->browser()
                       ->GetWebAuthenticationDelegate();
  const bool native_passkey_preflight =
      base::CommandLine::ForCurrentProcess()->HasSwitch(kWorkspaceNativePasskeyPreflight);
  if (native_passkey_preflight &&
      !webauthn->SupportsResidentKeys(personal_view->web_contents()->GetPrimaryMainFrame())) {
    LOG(ERROR) << "Native passkey discovery is unavailable in the visible Personal tab";
    Finish(false);
    return;
  }
  if (metal_ != metal_text || !widget_->IsVisible() ||
      !personal_view->IsDrawn() || work_view->GetVisible() ||
      work_view->web_contents()->GetVisibility() != content::Visibility::HIDDEN ||
      webauthn->IsFocused(work_view->web_contents()) ||
      webauthn->SupportsResidentKeys(work_view->web_contents()->GetPrimaryMainFrame()) ||
      !webauthn->IsFocused(personal_view->web_contents()) ||
      shell_->theme_mode() != (dark_ ? prototype::ThemeMode::kDark
                                     : prototype::ThemeMode::kLight) ||
      !controller_.Activate(kWorkspaceIds[0])) {
    Finish(false);
    return;
  }
  const base::Process& restored =
      web_contents()->GetPrimaryMainFrame()->GetProcess()->GetProcess();
  const bool passed = work_view->IsDrawn() && !personal_view->GetVisible() &&
         personal_view->web_contents()->GetVisibility() ==
             content::Visibility::HIDDEN &&
         webauthn->IsFocused(work_view->web_contents()) &&
         !webauthn->IsFocused(personal_view->web_contents()) &&
         restored.IsValid() && restored.Pid() == work_renderer_ &&
         web_contents()->GetTitle() == base::UTF8ToUTF16(kTitles[0]);
  if (native_passkey_preflight) {
    if (!passed ||
        !webauthn->SupportsResidentKeys(work_view->web_contents()->GetPrimaryMainFrame()) ||
        webauthn->SupportsResidentKeys(personal_view->web_contents()->GetPrimaryMainFrame())) {
      LOG(ERROR) << "Native passkey discovery did not follow Workspace visibility";
      Finish(false);
      return;
    }
    std::puts("Zitter Content native passkey discovery preflight passed");
  }
  if (passed && base::CommandLine::ForCurrentProcess()->HasSwitch(
                    kWorkspaceAddressHistoryProbe)) {
    RefreshShell();
    OpenAddressHistoryProbe();
    return;
  }
  if (passed && base::CommandLine::ForCurrentProcess()->HasSwitch(
                    "zitter-content-devtools-smoke-test")) {
    OpenDevTools(base::BindOnce(
            [](base::WeakPtr<WorkspaceLab> lab) {
              if (lab) {
                lab->widget_->LayoutRootViewIfNecessary();
                auto* view = static_cast<WorkspaceWebView*>(
                    lab->tabs_[0][0].view.get());
                if (!view->HasDockedDevTools()) {
                  lab->Finish(false);
                  return;
                }
                std::puts("Zitter Content DevTools frontend ready");
                lab->Finish(true);
              }
            },
            weak_factory_.GetWeakPtr()));
    return;
  }
  Finish(passed);
}

void WorkspaceLab::Finish(bool success) {
  if (done_)
    std::move(done_).Run(success);
}

}  // namespace zitter::content_embedder
