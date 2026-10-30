#include "chromium/prototype/browser_import_data.h"
#include "chromium/prototype/workspace_window.h"
#include "chromium/prototype/i18n.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "chromium/prototype/download_panel.h"
#include "chromium/prototype/page_services.h"
#include "chromium/prototype/page_dialogs.h"
#include "chromium/prototype/page_media_access.h"
#include "chromium/prototype/page_file_access.h"
#include "content/public/browser/file_select_listener.h"
#include "content/public/browser/render_frame_host.h"
#include "chromium/prototype/browser_preferences.h"
#include "chromium/prototype/link_navigation.h"
#include "base/task/single_thread_task_runner.h"
#include "base/unguessable_token.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/context_menu_params.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "chromium/prototype/arc_importer.h"

#include <utility>
#include <algorithm>
#include <set>
#include <cmath>
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/ui/zoom/chrome_zoom_level_prefs.h"
#include "chrome/common/pref_names.h"
#include "chrome/browser/preloading/preloading_prefs.h"
#include "ui/shell_dialogs/selected_file_info.h"
#include "ui/shell_dialogs/select_file_policy.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/pref_names.h"
#include "components/password_manager/core/common/password_manager_pref_names.h"
#include "components/prefs/pref_service.h"
#include "third_party/blink/public/common/page/page_zoom.h"

#include "base/auto_reset.h"
#include "chromium/prototype/extensions_bar.h"
#include "base/check.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_util.h"
#include "chrome/browser/browser_process.h"
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
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/navigation_entry.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/page_transition_types.h"
#include "ui/content_accelerators/accelerator_util.h"
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
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_MAC)
#include "chromium/prototype/workspace_frame_mac.h"
#include "chromium/prototype/workspace_keyboard_mac.h"
#endif

namespace zitter::prototype {
namespace {
// Palette result provenance; kind is sidebar::PaletteResultKind. Rust owns the
// wording.
std::u16string ResultDetail(int kind, const std::u16string& label) {
  return base::UTF8ToUTF16(std::string(sidebar::palette_result_detail(sidebar::PaletteResultKind{static_cast<uint8_t>(kind)}, base::UTF16ToUTF8(label))));
}
}  // namespace

namespace {



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
};
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

// WebView owns the delegate of its WebContents. Extend that delegate rather
// than replacing it and losing WebView's sizing/focus/lifecycle behavior.
class WorkspaceWebView final : public views::WebView {
  METADATA_HEADER(WorkspaceWebView, views::WebView)

 public:
  using KeyHandler = base::RepeatingCallback<
      content::KeyboardEventProcessingResult(
          content::WebContents*, const input::NativeWebKeyboardEvent&)>;

  using OpenHandler = base::RepeatingCallback<content::WebContents*(content::WebContents*, const content::OpenURLParams&, bool)>;
  using MenuHandler = base::RepeatingCallback<void(content::WebContents*, const content::ContextMenuParams&)>;
  using AdoptHandler = base::RepeatingCallback<content::WebContents*(content::WebContents*, std::unique_ptr<content::WebContents>, bool)>;
  using CreatedHandler = base::RepeatingCallback<void(content::WebContents*, content::WebContents*)>;
  WorkspaceWebView(Profile* profile, KeyHandler handler, OpenHandler open, MenuHandler menu,
                   AdoptHandler adopt, CreatedHandler created, base::RepeatingClosure changed,
                   base::RepeatingCallback<void(views::WebView*)> interacted)
      : views::WebView(profile), handler_(std::move(handler)), open_(std::move(open)), menu_(std::move(menu)),
        adopt_(std::move(adopt)), created_(std::move(created)), changed_(std::move(changed)), interacted_(std::move(interacted)) {}
  ~WorkspaceWebView() override = default;
  std::array<uint64_t,2> sidebar_identity() const {return {sidebar_identity_.GetHighForSerialization(),sidebar_identity_.GetLowForSerialization()};}

 private:
  const base::UnguessableToken sidebar_identity_=base::UnguessableToken::Create();
  content::KeyboardEventProcessingResult PreHandleKeyboardEvent(
      content::WebContents* source,
      const input::NativeWebKeyboardEvent& event) override {
    return handler_.Run(source, event);
  }

  bool PreHandleMouseEvent(content::WebContents*, const blink::WebMouseEvent& event) override {
    if (event.GetType() == blink::WebInputEvent::Type::kMouseDown) {
#if BUILDFLAG(IS_MAC)
      constexpr int primary = blink::WebInputEvent::kMetaKey;
#else
      constexpr int primary = blink::WebInputEvent::kControlKey;
#endif
      peek_click_ = event.button == blink::WebMouseEvent::Button::kLeft &&
          (event.GetModifiers() & primary) && !(event.GetModifiers() & (blink::WebInputEvent::kShiftKey | blink::WebInputEvent::kAltKey));
      peek_click_time_ = base::TimeTicks::Now();
    }
    return false;
  }
  content::WebContents* OpenURLFromTab(content::WebContents* source, const content::OpenURLParams& params,
      base::OnceCallback<void(content::NavigationHandle&)> callback) override {
    const bool peek = std::exchange(peek_click_, false) &&
        base::TimeTicks::Now() - peek_click_time_ < base::Seconds(2) &&
        params.disposition == WindowOpenDisposition::NEW_BACKGROUND_TAB;
    return open_.Run(source, params, peek);
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
  bool HandleContextMenu(content::RenderFrameHost&, const content::ContextMenuParams& params) override {
    if (params.is_editable || (!params.link_url.SchemeIsHTTPOrHTTPS() && !GetWebContents()->GetLastCommittedURL().SchemeIsHTTPOrHTTPS())) return false;
    menu_.Run(GetWebContents(), params);
    return true;
  }
  content::WebContents* AddNewContents(content::WebContents* source, std::unique_ptr<content::WebContents> page,
      const GURL&, WindowOpenDisposition disposition, const blink::mojom::WindowFeatures&, bool user_gesture, bool* blocked) override {
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
  bool peek_click_ = false;
  base::TimeTicks peek_click_time_;
  void TitleWasSet(content::NavigationEntry*) override { changed_.Run(); }
  // URL and back/forward availability feed the sidebar's address pill.
  void NavigationStateChanged(content::WebContents* source,
                              content::InvalidateTypes flags) override {
    views::WebView::NavigationStateChanged(source, flags);
    changed_.Run();
  }
  void DidGetUserInteraction(const blink::WebInputEvent& event) override {
    // Renderer/native attachment can report focus during session restoration.
    // Only actual user interaction changes which pane owns browser commands.
    interacted_.Run(this);
  }
  base::RepeatingClosure changed_;
  base::RepeatingCallback<void(views::WebView*)> interacted_;
};

BEGIN_METADATA(WorkspaceWebView)
END_METADATA
}  // namespace

WorkspaceWindow::WorkspaceWindow(Profile* work, Profile* personal)
    : WorkspaceWindow(work->GetPath().DirName(), false) {
  CHECK(personal);
  CHECK_NE(work, personal);
  CHECK_EQ(work->GetPath().DirName(), personal->GetPath().DirName());
}

WorkspaceWindow::WorkspaceWindow(base::FilePath data_directory, bool offer_import)
    : controller_(model_, *this), data_directory_(std::move(data_directory)), offer_import_(offer_import) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  // The UI language is fixed for the run, before any text is built.
  ApplyUiLanguage(g_browser_process->GetApplicationLocale());
  configuration_path_ = data_directory_.AppendASCII("ZitterWorkspaces.json");
  config_runner_ = base::ThreadPool::CreateSequencedTaskRunner(
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE, base::TaskShutdownBehavior::BLOCK_SHUTDOWN});
  WorkspaceShell::Callbacks callbacks;
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
  callbacks.query_history = base::BindRepeating(&WorkspaceWindow::QueryBrowsingHistory, base::Unretained(this));
  callbacks.clear_browsing_data = base::BindRepeating(&WorkspaceWindow::RequestClearBrowsingData, base::Unretained(this));
  callbacks.system_settings = base::BindRepeating(&WorkspaceWindow::OpenSystemSettings, base::Unretained(this));
  callbacks.read_preferences = base::BindRepeating(&WorkspaceWindow::ReadPreferences, base::Unretained(this));
  callbacks.write_preference = base::BindRepeating(&WorkspaceWindow::WritePreference, base::Unretained(this));
  callbacks.operation_pending = base::BindRepeating(&WorkspaceWindow::lifecycle_pending, base::Unretained(this));
  callbacks.time_machine = base::BindRepeating(&WorkspaceWindow::TimeMachine, base::Unretained(this));
  callbacks.save_snapshot = base::BindRepeating(&WorkspaceWindow::SaveSnapshot, base::Unretained(this));
  callbacks.restore_snapshot = base::BindRepeating(&WorkspaceWindow::RestoreSnapshot, base::Unretained(this));
  callbacks.delete_snapshot = base::BindRepeating(&WorkspaceWindow::DeleteSnapshot, base::Unretained(this));
  callbacks.reopen_closed = base::BindRepeating(&WorkspaceWindow::ReopenClosedTab, base::Unretained(this));
  callbacks.sleep_workspace = base::BindRepeating(&WorkspaceWindow::SleepWorkspace, base::Unretained(this));
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
  callbacks.move_tab = base::BindRepeating(&WorkspaceWindow::MoveTab, base::Unretained(this));
  callbacks.pin_tab = base::BindRepeating(&WorkspaceWindow::PinTab, base::Unretained(this));
  callbacks.unpin = base::BindRepeating(&WorkspaceWindow::UnpinItem, base::Unretained(this));
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
  shell_ = std::make_unique<WorkspaceShell>(
      data_directory_.AppendASCII("ZitterAppearance.json"), std::move(callbacks));
  auto root = shell_->Build();
  // Design: Extensions. The bar follows the profile of the page on screen.
  ExtensionsBar::Callbacks extension_callbacks;
  extension_callbacks.open_page = base::BindRepeating([](WorkspaceWindow* window, Profile* profile, const GURL& url) {
    window->OpenExtensionPage(profile, url);
  }, base::Unretained(this));
  extension_callbacks.workspace_color = base::BindRepeating([](WorkspaceWindow* window) {
    return window->shell_->active_workspace_color();
  }, base::Unretained(this));
  extensions_bar_ = static_cast<ExtensionsBar*>(shell_->SetHeaderTrailing(
      std::make_unique<ExtensionsBar>(std::move(extension_callbacks))));
  delegate_ = std::make_unique<views::WidgetDelegate>();
  delegate_->SetTitle(u"Zitter");
  delegate_->SetHasWindowSizeControls(true);
  delegate_->SetContentsView(std::move(root));
  delegate_->SetClientViewFactory(WorkspaceShell::ClientViewFactory());
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_WINDOW);
  params.delegate = delegate_.get();
  params.bounds = gfx::Rect(80, 80, 1280, 820);
  widget_ = std::make_unique<views::Widget>();
#if BUILDFLAG(IS_MAC)
  params.native_widget = CreateWorkspaceNativeWidget(widget_.get());
#endif
  widget_->Init(std::move(params));
  shell_->Attach(widget_.get(), this);
  widget_->Show();
  config_runner_->PostTaskAndReplyWithResult(FROM_HERE,
      base::BindOnce([](base::FilePath path) {
        std::string json;
        base::ReadFileToString(path, &json);
        std::string session;
        base::ReadFileToString(path.DirName().AppendASCII("ZitterSession.json"), &session);
        std::string timeline;
        base::ReadFileToString(path.DirName().AppendASCII("ZitterTimeMachine.json"), &timeline);
        return std::array<std::string, 3>{std::move(json), std::move(session), std::move(timeline)};
      }, configuration_path_),
      base::BindOnce([](base::WeakPtr<WorkspaceWindow> window, std::array<std::string, 3> data) {
        if (!window) return;
        window->session_json_ = std::move(data[1]);
        if (data[2].size() <= 32 * 1024 * 1024) {
          auto timeline = base::JSONReader::ReadDict(data[2], base::JSON_PARSE_RFC);
          if (timeline) window->time_machine_ = std::move(*timeline);
        }
        window->RestoreConfiguration(std::move(data[0]));
      }, weak_factory_.GetWeakPtr()));
}

WorkspaceWindow::Host::Host() = default;
WorkspaceWindow::Host::~Host() = default;
WorkspaceWindow::Host::Host(Host&&) noexcept = default;
WorkspaceWindow::Host& WorkspaceWindow::Host::operator=(Host&&) noexcept = default;

WorkspaceWindow::~WorkspaceWindow() {
  downloads_.reset();
  if (clear_data_dialog_) clear_data_dialog_->Close();
  if (data_remover_) data_remover_->RemoveObserver(this);
  data_remover_ = nullptr;
  data_removal_lease_.reset();
  if (download_directory_dialog_) download_directory_dialog_->ListenerDestroyed();
  SaveSession();
  if (time_machine_timer_.IsRunning()) FlushTimeMachine();
  closing_ = true;
  history_tasks_.TryCancelAll();
  history_view_tasks_.TryCancelAll();
  weak_factory_.InvalidateWeakPtrs();
  ClosePeek();
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

views::WebView* WorkspaceWindow::CreatePage(Profile* profile, views::View* parent,
    std::unique_ptr<content::WebContents> owned, bool create_contents) {
  auto* view = (parent ? parent : shell_->content_host())->AddChildView(std::make_unique<WorkspaceWebView>(
      profile, base::BindRepeating(&WorkspaceWindow::PreHandlePageKeyEvent, base::Unretained(this)),
      base::BindRepeating(&WorkspaceWindow::OpenPageLink, base::Unretained(this)),
      base::BindRepeating(&WorkspaceWindow::ShowPageMenu, base::Unretained(this)),
      base::BindRepeating(&WorkspaceWindow::AdoptPage, base::Unretained(this)),
      base::BindRepeating([](base::WeakPtr<WorkspaceWindow> window, content::WebContents* source, content::WebContents* page) {
        if (window) window->PreparePage(page, window->WorkspaceForPage(source));
      }, weak_factory_.GetWeakPtr()),
      base::BindRepeating(&WorkspaceWindow::ScheduleShellUpdate, base::Unretained(this)),
      base::BindRepeating(&WorkspaceWindow::PageFocused, base::Unretained(this))));
  view->set_allow_accelerators(true);
  view->SetVisible(false);
  if (owned) view->SetOwnedWebContents(std::move(owned));
  if (create_contents || view->web_contents()) {
    PreparePage(view->GetWebContents());
    view->web_contents()->WasHidden();
  }
  return view;
}

content::WebContents* WorkspaceWindow::EnsurePage(views::WebView* view) {
  if (!view->web_contents()) {
    auto* page = view->GetWebContents();
    PreparePage(page);
    page->WasHidden();
  }
  return view->web_contents();
}

GURL WorkspaceWindow::TabUrl(const Host& host, views::WebView* view) {
  if (auto pending = host.deferred_urls.find(view); pending != host.deferred_urls.end()) return pending->second;
  return view->web_contents() ? view->web_contents()->GetVisibleURL() : GURL();
}

std::u16string WorkspaceWindow::TabTitle(const Host& host, views::WebView* view) {
  if (auto stored = host.deferred_titles.find(view); stored != host.deferred_titles.end()) return stored->second;
  return view->web_contents() ? view->web_contents()->GetTitle() : std::u16string();
}

void WorkspaceWindow::PreparePage(content::WebContents* page, std::string source_hint) {
  PageServices::Attach(page);
  PageDialogs::Attach(page);
  PageMediaAccess::Attach(page);
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
  const bool sidebar_was_visible = shell_->sidebar_visible();
  const bool changed = previous != id;
  const bool selected = controller_.Activate(id);
  if (selected) {
    if (changed) ClosePeek();
    shell_->CloseOverlays(true);
    UpdateShell();
    widget_->Activate();
    contents(active_id())->Focus();
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

void WorkspaceWindow::UpdateShell() {
  shell_update_pending_ = false;
  if (closing_ || !widget_) return;
  ScheduleSessionSave();
  if (peek_view_) {
    const auto* workspace = model_.Find(peek_workspace_id_);
    const auto& url = peek_contents()->GetVisibleURL();
    if (workspace) shell_->SetPeekTitle(base::UTF8ToUTF16(workspace->name + " · " + std::string(url.host())));
  }
  for (const auto& host : hosts_) {
    if (host.workspace_id != active_id()) continue;
    ShellState state;
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
        state.tabs.push_back({title.empty() ? T(u"Nowa karta") : title, display_url, index, pin_id, static_cast<WorkspaceWebView*>(view.get())->sidebar_identity(),
                              view->web_contents() && view->web_contents()->IsCurrentlyAudible()});
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
    state.profile_id = host.profile_id;
    state.workspaces = model_.workspaces();
    state.configuration_ready = configuration_ready_ && !profiles_busy_ && !configuration_invalid_;
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
    // The panel watches the active profile even while closed, so a finished
    // download shows its toast (design: Downloads).
    if (!downloads_) {
      downloads_ = std::make_unique<DownloadPanel>();
      downloads_->SetCompletedCallback(base::BindRepeating(&WorkspaceWindow::DownloadCompleted, base::Unretained(this)));
    }
    downloads_->SetProfile(host.profile);
    extensions_bar_->SetContext(host.profile, host.view->GetWebContents());
    shell_->Update(std::move(state));
  }
}

namespace {
// The page is a rounded card inside the tinted frame. The native view attaches
// only after the page is shown, so every refresh rounds each visible pane again.
void RoundPage(views::WebView* view) {
  if (view && view->holder()->native_view() && view->holder()->GetNativeViewCornerRadii() != gfx::RoundedCornersF(10))
    view->holder()->SetNativeViewCornerRadii(gfx::RoundedCornersF(10));
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
          tab->GetWebContents()->GetController().LoadURL(pending->second, content::Referrer(), ui::PAGE_TRANSITION_RELOAD, std::string());
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
  auto* service = profile->GetPrefs();
  auto read = [&](const char* key, const char* pref, int value) {
    const auto* entry = service->FindPreference(pref);
    if (entry) result[key] = {value, entry->IsUserModifiable()};
  };
  AppendChromiumPreferences(profile, g_browser_process->local_state(), result);
  for (const auto& entry : kBooleanPreferences)
    read(entry.key, entry.pref, service->GetBoolean(entry.pref));
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
  auto current = ReadPreferences();
  auto it = current.find(key);
  if (it == current.end() || !it->second.editable) return false;
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

void WorkspaceWindow::DownloadCompleted(std::string guid, std::u16string name) {
  shell_->ShowToast(T(u"Pobrano "), name, base::BindOnce([](base::WeakPtr<WorkspaceWindow> window, std::string guid) {
    if (window && window->downloads_) window->downloads_->Act(guid, DownloadPanel::Action::kReveal);
  }, weak_factory_.GetWeakPtr(), std::move(guid)), T(u"Pokaż"));
}
void WorkspaceWindow::ShowDownloads() {
  auto* page = peek_contents() ? peek_contents() : contents(active_id());
  if (!page) return;
  shell_->CloseOverlays(true);
  if (!downloads_) downloads_ = std::make_unique<DownloadPanel>();
  downloads_->Show(Profile::FromBrowserContext(page->GetBrowserContext()), widget_.get());
}
void WorkspaceWindow::ShowPasswords() {
  shell_->CloseOverlays(true);
  if (auto* page = peek_contents() ? peek_contents() : contents(active_id())) PageServices::FromWebContents(page)->ShowPasswords();
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
  if (closing_ || !configuration_ready_ || profiles_busy_ || !profile || !url.is_valid()) return false;
  // The page belongs to the profile whose extension asked for it: the space on
  // screen when it uses that profile, otherwise the first space that does.
  std::string target;
  for (const auto& host : hosts_)
    if (host.profile == profile && (target.empty() || host.workspace_id == active_id())) target = host.workspace_id;
  if (target.empty() || !Select(target)) return false;
  for (auto& host : hosts_) {
    if (host.workspace_id != target) continue;
    auto* view = CreatePage(host.profile);
    host.tabs.push_back(view);
    view->GetWebContents()->GetController().LoadURL(url, content::Referrer(), ui::PAGE_TRANSITION_AUTO_TOPLEVEL, std::string());
    SelectTab(host.tabs.size() - 1);
    return true;
  }
  return false;
}
bool WorkspaceWindow::OpenExternalUrl(const GURL& url) { return OpenLink(url, active_id(), true); }
bool WorkspaceWindow::OpenLink(const GURL& url, std::string source_workspace, bool new_tab, bool split) {
  preconnect_timer_.Stop();
  if (peek_view_ && GuardPages({peek_contents()}, base::BindOnce([](base::WeakPtr<WorkspaceWindow> w, GURL url, std::string source, bool fresh, bool split) {
    if (w) w->OpenLink(url, source, fresh, split);
  }, weak_factory_.GetWeakPtr(), url, source_workspace, new_tab, split))) return true;
  if (!configuration_ready_ || profiles_busy_ || !url.SchemeIsHTTPOrHTTPS()) return false;
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
  peek_view_->GetWebContents()->GetController().LoadURL(url, content::Referrer(), ui::PAGE_TRANSITION_LINK, std::string());
  peek_view_->RequestFocus();
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
content::WebContents* WorkspaceWindow::OpenPageLink(content::WebContents* source,
    const content::OpenURLParams& params, bool peek) {
  if (!params.url.SchemeIsHTTPOrHTTPS() || params.post_data) return nullptr;
  const auto workspace = WorkspaceForPage(source);
  if (workspace.empty()) return nullptr;
  const bool new_tab = params.disposition != WindowOpenDisposition::CURRENT_TAB;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
      base::BindOnce([](base::WeakPtr<WorkspaceWindow> window, GURL url, std::string workspace, bool peek, bool new_tab, bool background) {
        if (!window) return;
        if (peek) { window->OpenPeek(url, workspace); return; }
        const auto target = window->WorkspaceForUrl(url);
        if (background && (target.empty() || target == workspace)) {
          for (auto& host : window->hosts_) if (host.workspace_id == workspace) {
            auto* view = window->CreatePage(host.profile);
            host.tabs.push_back(view);
            view->GetWebContents()->GetController().LoadURL(url, content::Referrer(), ui::PAGE_TRANSITION_LINK, std::string());
            window->UpdateShell(); return;
          }
        }
        window->OpenLink(url, workspace, new_tab);
      }, weak_factory_.GetWeakPtr(), params.url, workspace, peek, new_tab,
          params.disposition == WindowOpenDisposition::NEW_BACKGROUND_TAB));
  return nullptr;
}
void WorkspaceWindow::ShowPageMenu(content::WebContents* source, const content::ContextMenuParams& params) {
  const auto url = params.link_url.SchemeIsHTTPOrHTTPS() ? params.link_url : source->GetLastCommittedURL();
  shell_->ShowLinkMenu(url, WorkspaceForPage(source), source->GetContainerBounds().origin() + gfx::Vector2d(params.x, params.y));
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
    if (result.url.SchemeIsHTTPOrHTTPS()) NavigateFromBar(result.url, true);
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
      if (!url.SchemeIsHTTPOrHTTPS()) continue;
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
  auto* service = HistoryServiceFactory::GetForProfile(ProfileFor(profile_id), ServiceAccessType::EXPLICIT_ACCESS);
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

void WorkspaceWindow::CloseTab(size_t index) { CloseWorkspaceTab(active_id(), index); }
void WorkspaceWindow::CloseWorkspaceTab(std::string_view workspace_id, size_t index) {
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
      if (host.workspace_id == active_id() && !closing_split_tabs_) SaveSnapshot(base::UTF16ToUTF8(T(u"Przed zamknięciem karty")), true);
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
          tab->GetWebContents()->GetController().LoadURL(url, content::Referrer(), ui::PAGE_TRANSITION_RELOAD, std::string());
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
    SaveSnapshot(base::UTF16ToUTF8(T(u"Przed zmianą podziału")), true);
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
      if (url.SchemeIsHTTPOrHTTPS()) host.deferred_urls[host.tabs[index]] = url;
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
    SaveSnapshot(base::UTF16ToUTF8(T(u"Przed zamknięciem podziału")), true);
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
    SaveSnapshot(base::UTF16ToUTF8(T(u"Przed zmianą podziału")), true);
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
void WorkspaceWindow::CloseSplitTabs(size_t index) {
  if (!configuration_ready_ || profiles_busy_) return;
  for (auto& host : hosts_) {
    if (host.workspace_id != active_id() || index >= host.tabs.size() || !InSplit(host, host.tabs[index])) continue;
    // Every page with a beforeunload handler is asked once, before any closes.
    std::vector<content::WebContents*> pages;
    for (const auto& member : host.split) if (auto* page = member->web_contents()) pages.push_back(page);
    if (GuardPages(pages, base::BindOnce(&WorkspaceWindow::CloseSplitTabs, weak_factory_.GetWeakPtr(), index))) return;
    SaveSnapshot(base::UTF16ToUTF8(T(u"Przed zamknięciem kart Split View")), true);
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
      SaveSnapshot(base::UTF16ToUTF8(T(u"Przed zamknięciem podziału")), true);
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
  auto& controller = page->GetController();
  switch (command) {
    case PageCommand::kBack:
      if (controller.CanGoBack()) controller.GoBack();
      break;
    case PageCommand::kForward:
      if (controller.CanGoForward()) controller.GoForward();
      break;
    case PageCommand::kReload:
      controller.Reload(content::ReloadType::NORMAL, true);
      break;
  }
  page->Focus();
}

bool WorkspaceWindow::NavigateFromBar(const GURL& url, bool new_tab) {
  preconnect_timer_.Stop();
  if (peek_view_ && GuardPages({peek_contents()}, base::BindOnce([](base::WeakPtr<WorkspaceWindow> w, GURL url, bool fresh) {
    if (w) w->NavigateFromBar(url, fresh);
  }, weak_factory_.GetWeakPtr(), url, new_tab))) return true;
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) return false;
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
  content::NavigationController::LoadURLParams params(url);
  params.transition_type = ui::PAGE_TRANSITION_TYPED;
  page->GetController().LoadURLWithParams(params);
  return true;
}

void WorkspaceWindow::Navigate(const GURL& url) { NavigateFromBar(url, false); }

bool WorkspaceWindow::AcceleratorPressed(const ui::Accelerator& accelerator) {
  if (accelerator.key_state() != ui::Accelerator::KeyState::PRESSED) return false;
  if (shell_->CaptureShortcut(accelerator)) return true;
  if (accelerator.modifiers() == 0 && accelerator.key_code() == ui::VKEY_ESCAPE) {
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
  if (id.empty()) return false;
  if (id == "copy_url") {
    // The address pill trembles once: the copy's only confirmation (design: AddressPill).
    auto* page = peek_contents() ? peek_contents() : contents(active_id());
    if (!page || !page->GetLastCommittedURL().SchemeIsHTTPOrHTTPS()) return false;
    ui::ScopedClipboardWriter(ui::ClipboardBuffer::kCopyPaste).WriteText(base::UTF8ToUTF16(page->GetLastCommittedURL().spec()));
    shell_->TremorAddress();
    return true;
  }
  if (id == "downloads") { ShowDownloads(); return true; }
  // Development (--zitter-open-panel=extension-popup): the first pinned action.
  if (id == "extension_popup") {
    const auto actions = extensions_bar_->visible_actions_for_testing();
    if (actions.empty()) return false;
    extensions_bar_->RunAction(actions.front().id);
    return true;
  }
  if (id == "extensions") { shell_->CloseOverlays(true); extensions_bar_->ToggleMenu(); return extensions_bar_->profile() != nullptr; }
  if (id == "history") { shell_->OpenTools(3); return true; }
  if (id == "settings") { shell_->OpenGeneralSettings(); return true; }
  if (id == "address" || id == "new_tab" || id == "palette") {
    if (peek_view_ && GuardPages({peek_contents()}, base::BindOnce([](base::WeakPtr<WorkspaceWindow> w, std::string id) { if (w) w->RunShortcut(id); }, weak_factory_.GetWeakPtr(), id))) return true;
    ClosePeek(); UpdateShell();
    if (id == "palette") shell_->OpenCommandPalette();
    else shell_->OpenAddress(id == "new_tab");
    return true;
  }
  if (id == "close_tab") { CloseActiveTab(); return true; }
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
  if (id == "timeline" || id == "snapshot") { shell_->OpenTools(0); return true; }
  if (id == "sleep") return SleepWorkspace(active_id());
  if (id == "next_tab" || id == "previous_tab") {
    for (const auto& host : hosts_) if (host.workspace_id == active_id() && !host.tabs.empty()) {
      SelectTab(shell_->CycleSidebarTab(host.active_tab, host.tabs.size(), id == "previous_tab")); return true;
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
  const bool escape_peek = key == ui::VKEY_ESCAPE && accelerator.modifiers() == 0 && source == peek_contents();
  const auto action = shell_->ShortcutFor(accelerator);
  if (action.empty() && !escape_peek) return Result::NOT_HANDLED;
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

Profile* WorkspaceWindow::ProfileFor(std::string_view id) const {
  for (const auto& entry : profiles_) {
    if (entry.info.id == id && entry.lease) return entry.lease->profile();
  }
  return nullptr;
}

bool WorkspaceWindow::AddWorkspace(std::string name, std::string profile_id) {
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
  if (!configuration_ready_ || configuration_invalid_) { if (done) std::move(done).Run(false); return; }
  base::DictValue dict;
  const auto* replaced_starter = import_replaces_starter_ ? model_.Find(starter_workspace_id_) : nullptr;
  base::ListValue profile_list;
  for (const auto& entry : profiles_) {
    if (replaced_starter && entry.info.id == replaced_starter->profile_id) continue;
    base::DictValue item;
    item.Set("id", entry.info.id);
    item.Set("name", entry.info.name);
    item.Set("directory", entry.info.directory);
    item.Set("source_id", entry.info.source_id);
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
      base::DictValue value;
      value.Set("id", pin.id);
      value.Set("parent_id", pin.parent_id);
      value.Set("title", pin.title);
      value.Set("url", pin.url.spec());
      value.Set("folder", pin.folder);
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
                (!folder && !GURL(*url).SchemeIsHTTPOrHTTPS())) { valid = false; break; }
            restored_pins[*id].push_back({*pin_id, *parent, *title, GURL(*url), folder});
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
    onboarding_complete_ = dict->FindBool("onboarding_complete").value_or(true);
    if (const auto* starter = dict->FindString("starter_workspace"))
      if (!onboarding_complete_ && model_.Find(*starter)) starter_workspace_id_ = *starter;
    profiles_ = std::move(entries);
    deleted_directories_ = std::move(deleted);
  } else {
    configuration_invalid_ = !json.empty();
    if (json.empty()) {
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
  shell_->WarmWorkspaceSidebars(pins_);
  SaveConfiguration();
  for (const auto& directory : deleted_directories_) ScheduleProfileDeletion(directory);
  if (configuration_invalid_) shell_->SetConfigurationError(true);
  if (ready_callback_) std::move(ready_callback_).Run();
  if (offer_import_ && !onboarding_complete_) shell_->OpenImport();
}

const std::vector<PinnedItem>& WorkspaceWindow::pinned_items(std::string_view workspace_id) const {
  static const base::NoDestructor<std::vector<PinnedItem>> empty;
  auto it = pins_.find(std::string(workspace_id));
  return it == pins_.end() ? *empty : it->second;
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

bool WorkspaceWindow::InsertPin(std::string workspace, PinnedItem item, std::string before) {
  if (!configuration_ready_ || configuration_invalid_ || profiles_busy_ || !model_.Find(workspace)) return false;
  auto& items = pins_[workspace];
  // The parent must be a reachable folder that is not the item itself or one of its descendants.
  for (std::string ancestor = item.parent_id; !ancestor.empty();) {
    if (ancestor == item.id) return false;
    auto it = std::find_if(items.begin(), items.end(), [&](const auto& other) { return other.id == ancestor && other.folder; });
    if (it == items.end()) return false;
    ancestor = it->parent_id;
  }
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
    if (!url.SchemeIsHTTPOrHTTPS()) return false;
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
bool WorkspaceWindow::UnpinItem(std::string id, size_t before) {
  if (!configuration_ready_ || configuration_invalid_ || profiles_busy_) return false;
  for (auto& host : hosts_) {
    if (host.workspace_id != active_id() || before > host.tabs.size()) continue;
    auto& items = pins_[host.workspace_id];
    const auto pin = std::find_if(items.begin(), items.end(), [&](const auto& item) { return item.id == id && !item.folder; });
    if (pin == items.end()) return false;
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
    SaveFolderEdit(host.workspace_id, std::move(previous), {id});
    return true;
  }
  return false;
}
bool WorkspaceWindow::MovePin(std::string id, std::string parent, std::string before) {
  const auto& items = pinned_items(active_id());
  const auto pin = std::find_if(items.begin(), items.end(), [&](const auto& item) { return item.id == id; });
  if (pin == items.end() || id == before) return false;
  PinnedItem moved = *pin;
  moved.parent_id = std::move(parent);
  return InsertPin(active_id(), std::move(moved), std::move(before));
}

bool WorkspaceWindow::CreatePinnedFolder(std::string workspace, std::string parent, std::string name) {
  name = std::string(base::TrimWhitespaceASCII(name, base::TRIM_ALL));
  if (!configuration_ready_ || configuration_invalid_ || profiles_busy_ || !model_.Find(workspace) ||
      name.empty() || name.size() > 256 || !base::IsStringUTF8(name)) return false;
  auto& items = pins_[workspace];
  if (!parent.empty() && std::none_of(items.begin(), items.end(), [&](const auto& item) {
        return item.id == parent && item.folder;
      })) return false;
  // Match the renderer's nesting bound; do not create an invisible descendant.
  std::string ancestor = parent;
  size_t depth = 0;
  while (!ancestor.empty()) {
    if (++depth > 64) return false;
    auto it = std::find_if(items.begin(), items.end(), [&](const auto& item) { return item.id == ancestor && item.folder; });
    if (it == items.end()) return false;
    ancestor = it->parent_id;
  }
  const auto id = "folder-" + base::Uuid::GenerateRandomV4().AsLowercaseString();
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
  if (contents) {
    bool changed;
    do {
      changed = false;
      for (const auto& item : items) if (removed.contains(item.parent_id)) changed |= removed.insert(item.id).second;
    } while (changed);
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
    for (auto& host : window->hosts_) if (host.workspace_id == workspace) {
      // A rolled-back pin must not leave its page hidden behind a missing row.
      const auto& items = window->pins_[workspace];
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
  PreviewBrowserImport(BrowserImportOptions(), std::move(directory));
}
void WorkspaceWindow::InvalidateImportPreview() {
  if (profiles_busy_) return;
  ++import_preview_generation_;
  pending_import_.reset();
  import_loading_ = false;
}
void WorkspaceWindow::PreviewBrowserImport(BrowserImportOptions options, base::FilePath directory) {
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
      }, weak_factory_.GetWeakPtr(), generation));
}

bool WorkspaceWindow::ApplyArcImport() {
  if (!configuration_ready_ || configuration_invalid_ || profiles_busy() || import_loading_ || !import_ready()) return false;
  profiles_busy_ = true;
  import_new_profile_ids_.clear();
  import_new_workspace_ids_.clear();
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
    import_replaces_starter_ = starter && (starter->name == "Moja przestrzeń" || starter->name == base::UTF16ToUTF8(T(u"Moja przestrzeń"))) &&
        starter->profile_id == "personal-profile" && pinned_items(starter->id).empty() &&
        host != hosts_.end() && host->tabs.size() == 1 && host->split.empty() &&
        !TabUrl(*host, host->view).SchemeIsHTTPOrHTTPS();
  }
  closing_ = true;
  for (const auto& space : pending_import_->spaces) {
    if (model_.Find(space.id)) continue;
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
  pending_import_.reset();
  import_new_profile_ids_.clear();
  import_new_workspace_ids_.clear();
  profiles_busy_ = false;
  UpdateShell();
  shell_->WarmWorkspaceSidebars(pins_);
  shell_->SetImportStatus(status, false);
}

void WorkspaceWindow::DismissImport() {
  if (profiles_busy_) return;
  InvalidateImportPreview();
  starter_workspace_id_.clear();
  onboarding_complete_ = true;
  SaveConfiguration();
}

void WorkspaceWindow::ScheduleSessionSave() {
  if (!configuration_ready_ || configuration_invalid_ || restoring_session_ || closing_ || profiles_busy_) return;
  session_timer_.Start(FROM_HERE, base::Milliseconds(350),
      base::BindOnce(&WorkspaceWindow::SaveSession, weak_factory_.GetWeakPtr()));
}
void WorkspaceWindow::SaveSession() {
  session_timer_.Stop();
  if (!configuration_ready_ || configuration_invalid_ || profiles_busy_) return;
  base::DictValue session;
  for (const auto& host : hosts_) {
    auto entry = CaptureHost(host);
    session.Set(host.workspace_id, std::move(entry));
  }
  if (base::Time::Now() - last_automatic_snapshot_ >= base::Minutes(5)) SaveSnapshot(base::UTF16ToUTF8(T(u"Punkt przywracania")), true);
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
}  // namespace zitter::prototype
