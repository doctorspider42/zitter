#ifndef ZITTER_CHROMIUM_PROTOTYPE_WORKSPACE_WINDOW_H_
#define ZITTER_CHROMIUM_PROTOTYPE_WORKSPACE_WINDOW_H_

#include <array>
#include <memory>
#include <optional>
#include "base/auto_reset.h"
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "ui/gfx/geometry/rect.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/browsing_data_remover.h"
#include "base/timer/timer.h"
#include "chromium/prototype/night_shift.h"
#include "base/time/clock.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "components/find_in_page/find_result_observer.h"
#include "components/find_in_page/find_tab_helper.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/shell_dialogs/select_file_dialog.h"
#include "ui/views/widget/widget_observer.h"
#include "chromium/prototype/assistant_host.h"
#include "chromium/prototype/assistant_panel.h"
#include "chromium/prototype/before_unload_batch.h"
#include "chromium/prototype/workspace_shell.h"
#include "zitter/application/workspace_controller.h"

class Profile;
class ScopedProfileKeepAlive;
class GURL;
namespace content {
class WebContents;
class NavigationHandle;
struct OpenURLParams;
struct ContextMenuParams;
enum class KeyboardEventProcessingResult;
}
namespace input { struct NativeWebKeyboardEvent; }
namespace gfx { class Image; }
namespace network { class SimpleURLLoader; }
namespace views {
class Label;
class Textfield;
class WebView;
class Widget;
class WidgetDelegate;
class ViewTracker;
}  // namespace views

namespace zitter::sidebar { struct Timeline; }
namespace zitter::prototype {
struct TimelineStore;

class DownloadPanel;
class HistoryPanel;
class RssPanel;
class PasswordsPanel;
class AdBlockingRulesPanel;
class PagePrinter;
class ExtensionsBar;
struct BrowsingImportData;

// Intentionally a substrate probe, not a replacement BrowserWindowInterface.
// The window owns profile keep-alives; WebViews own their WebContents.
class WorkspaceWindow final : public WorkspaceRuntime,
                              private AssistantHost,
                              public ui::AcceleratorTarget,
                              public ui::SelectFileDialog::Listener,
                              public content::BrowsingDataRemover::Observer,
                              public find_in_page::FindResultObserver,
                              public views::WidgetObserver {
 public:
  WorkspaceWindow(Profile* work, Profile* personal);
  explicit WorkspaceWindow(base::FilePath data_directory, bool offer_import = true, Profile* private_profile = nullptr);
  bool is_private() const { return private_profile_ != nullptr; }
  Profile* active_profile() const;
  bool clean_address_copies() const {
    return address_copy_preference_source_ ? address_copy_preference_source_.Run()
                                           : clean_address_copies_;
  }
  void SetAddressCopyPreferenceSource(base::RepeatingCallback<bool()> source) {
    address_copy_preference_source_ = std::move(source);
  }
  void SetNewPrivateWindowCallback(base::RepeatingClosure callback) { new_private_window_ = std::move(callback); }
  ~WorkspaceWindow() override;
  WorkspaceWindow(const WorkspaceWindow&) = delete;
  WorkspaceWindow& operator=(const WorkspaceWindow&) = delete;

  void PreviewArcImport(base::FilePath arc_directory);
  void PreviewBrowserImport(BrowserImportOptions options, base::FilePath directory = {});
  void InvalidateImportPreview();
  bool ApplyArcImport();
  void DismissImport();
#if BUILDFLAG(IS_WIN)
  // The first run's wizard instead of the bare import offer.
  void ShowWelcome();
  void WelcomeFinished(WorkspaceShell::WelcomeResult result);
  void StartWelcomeImport(BrowserImportOptions options, base::OnceCallback<void(bool, std::u16string)> done);
#endif
  bool import_ready() const { return pending_import_.has_value() && pending_import_->error.empty(); }
  const ArcImport* import_preview() const { return pending_import_ ? &*pending_import_ : nullptr; }
  const std::vector<PinnedItem>& pinned_items(std::string_view workspace_id) const;
  void OpenPinnedItem(std::string id);
  bool CreatePinnedFolder(std::string workspace, std::string parent, std::string name);
  bool ConfigureGitHubFolder(std::string workspace, std::string folder, std::string name,
                             GitHubLiveFolder config, std::string token, bool remove_token);
  void RefreshGitHubFolder(std::string workspace, std::string folder);
  bool RenamePinnedFolder(std::string workspace, std::string id, std::string name);
  bool RemovePinnedFolder(std::string workspace, std::string id, bool contents);
  // Undo of a folder removal: puts the workspace's earlier pins back.
  bool RestorePins(std::string workspace, std::vector<PinnedItem> items);
  bool CreateProfile(std::string name, std::string workspace_id = {});
  bool RenameProfile(std::string id, std::string name);
  bool AssignProfile(std::string workspace_id, std::string profile_id);
  bool RemoveProfile(std::string id);
  std::vector<ProfileInfo> profiles() const;
  bool profiles_busy() const { return profiles_busy_ || !StartupProfilesReady(); }
  bool AddWorkspace(std::string name, std::string profile_id);
  bool EditWorkspace(std::string name, std::string profile_id);
  bool RemoveWorkspace(std::string id = {});
  // Undo of the last removal: the workspace returns to its place with its pins,
  // link rules and tabs (reloaded from their URLs).
  bool RestoreRemovedWorkspace(std::string id);
  bool configuration_ready() const { return configuration_ready_; }
  using StartupProfileLoader = base::RepeatingCallback<void(base::FilePath, base::OnceCallback<void(Profile*)>)>;
  void SetStartupProfileLoaderForTesting(StartupProfileLoader loader);
  bool configuration_saved() const { return pending_config_writes_ == 0 && !session_timer_.IsRunning(); }
  std::vector<Workspace> workspaces() const { return model_.workspaces(); }
  bool Select(std::string_view workspace_id);
  void Navigate(const GURL& url);
  // Existing contents only; an unrealized or sleeping workspace has no page.
  content::WebContents* contents(std::string_view workspace_id) const;
  std::string active_id() const;
  views::Widget* widget() const { return widget_.get(); }
  void FocusAddressBar();
  void OpenCommandPalette();
  void SearchPalette(std::u16string query);
  void ActivatePaletteResult(PaletteResult result);
  std::vector<PaletteResult> CommandResults(std::u16string query) const;
  bool ExecuteCommand(const PaletteResult& result, bool record_usage = true);
  bool MoveActiveTab(std::string target_id);
  void ShowPasswords();
  void ShowDownloads();
  void ShowHistory();
  void ShowRss();
  RssPanel* rss_panel() const;
  // `hover`: opened from the window edge; it hides again once the pointer is gone.
  void ShowAssistant();
  void NewAssistantConversation();
  // Creates the panel hidden, so its scheduled tasks run.
  void EnsureAssistant();
  // The panel's state changed: the toolbar button follows it.
  void AssistantStateChanged() override;
  // The toolbar button: shows or hides the panel.
  void ToggleAssistantFromBar();
  // The page quick actions (0-4), shown as suggestions on a new conversation;
  // 6 opens the assistant settings.
  void RunAssistantPageAction(int command) override;
  bool AssistantPageActionsAvailable() override;
  content::WebContents* AssistantCurrentPage() override;
  // Opens the assistant and sends `prompt` (the shield's "block more").
  void AskAssistant(std::u16string prompt);
  void PrepareNightShiftRepair(content::WebContents* page);
  // Settings → Blokowanie reklam, for the active profile.
  base::DictValue ReadAdBlocking();
  std::string WriteAdBlocking(base::DictValue command);
  // Settings > Ad blocking > Twoje reguły: the editor dialog over Settings.
  void OpenAdBlockingRules();
  AdBlockingRulesPanel* ad_blocking_rules_panel() const { return ad_blocking_rules_panel_.get(); }
  Profile* AssistantProfile() const;
  content::BrowserContext* AssistantBrowserContext() const override;
  std::string AssistantProfileId() const override;
  base::DictValue AssistantContext(const AssistantPermissions& permissions) override;
  // `background` names a session's invisible page: @current and new pages
  // resolve to it, and mutations of the user's visible tabs are refused.
  void AssistantTool(std::string tool, base::DictValue args, AssistantPermissions permissions,
                     base::OnceCallback<void(base::Value)> done, bool user_confirmed = false,
                     std::string background = {}) override;
  const AssistantSettings& assistant_settings() const override;
  void SetAssistantSettings(AssistantSettings settings) override;
  // Settings rows for backends and models (Settings → Asystent AI).
  base::DictValue AssistantSettingsForShell();
  void WriteAssistantSettings(base::DictValue value);
  void WithAssistantApiKey(base::OnceCallback<void(std::string)> callback) override;
  void CheckAssistantConnection(std::string provider, bool discover = false);
  // Invisible pages of the assistant profile share its cookies and logins.
  // Starting from a visible tab moves that tab, with its page state, away from the user.
  bool AssistantStartBackground(const std::string& key, const std::string& from_tab, const GURL& url) override;
  bool AssistantHasBackground(const std::string& key) const override;
  // Puts the page back among the user's tabs and selects it.
  bool AssistantTakeOver(const std::string& key) override;
  void AssistantDiscardBackground(const std::string& key) override;
  // The element's bounds in screen coordinates, for a consent bubble beside it.
  views::WebView* AssistantTabView(std::string id);
  // Saved assistant conversations, beside the workspace configuration.
  void LoadAssistantSessions(base::OnceCallback<void(std::optional<base::DictValue>)> done) override;
  void SaveAssistantSessions(base::DictValue sessions) override;
  base::FilePath assistant_sessions_path() const { return data_directory_.AppendASCII("ZitterAssistantSessions.json"); }
  // The assistant's stable id for a tab (@tab:<id>), made on first use.
  std::string AssistantTargetFor(views::View* view);
  void AssistantElementBounds(std::string tab, std::string element, base::OnceCallback<void(std::optional<gfx::Rect>)> done) override;
  content::WebContents* assistant_background_for_testing(const std::string& key) const;
  AssistantPanel* assistant() const { return assistant_; }
  void ShowSitePanel();
  DownloadPanel* downloads() const { return downloads_.get(); }
  void EnsureDownloads();
  void SetDownloadProfile(Profile* profile);
  void DownloadCompleted(std::string guid, std::u16string name);
  BrowserPreferences ReadPreferences() const;
  std::u16string DownloadDirectory() const;
  void ChooseDownloadDirectory();
  bool WritePreference(std::string key, int value);
  bool WriteTextPreference(std::string key, std::string value);
  std::vector<SitePreferenceRule> ReadSitePreferences() const;
  bool WriteSitePreference(std::string origin, std::string key, int setting);
  bool RemoveSitePreference(SitePreferenceRule rule);
  void RequestClearBrowsingData(BrowsingDataSelection selection);
  bool ClearBrowsingData(std::string profile_id, BrowsingDataSelection selection);
  bool clearing_browsing_data() const { return data_remover_ != nullptr; }
  views::Widget* clear_data_dialog() const { return clear_data_dialog_.get(); }
  void OpenSystemSettings(bool certificates);
  bool OpenPeek(const GURL& url, std::string source_workspace = {});
  void ClosePeek();
  bool PromotePeek(bool split);
  content::WebContents* peek_contents() const;
  bool image_preview_visible_for_testing(content::WebContents* page) const;
  bool SetLinkRule(const GURL& url, std::string workspace_id);
  std::string WorkspaceForUrl(const GURL& url) const;
  const std::vector<LinkRule>& link_rules() const { return link_rules_; }
  bool OpenExternalUrl(const GURL& url);
  // A command from the taskbar's jump list (--zitter-command): "new_tab",
  // "palette" or "space:<id>". Brings the window forward first.
  bool RunExternalCommand(std::string command);
  // Opens an extension's page (options, chrome://extensions, a link from its
  // popup) as a new tab in a space that uses `profile`.
  bool OpenExtensionPage(Profile* profile, const GURL& url);
  // The same as a tab of the tabs API: shown when `active`, else opened behind.
  content::WebContents* OpenProfileTab(Profile* profile, const GURL& url, bool active);
  // Moves a tab to another space of its profile at `position` (tabs API).
  bool MoveTabToSpace(content::WebContents* page, const std::string& target, size_t position);
  bool MoveSidebarTab(size_t index, std::string target);
  bool DuplicateTab(size_t index);
  bool DuplicatePin(std::string id);
  bool ReloadWorkspaceFavicons(std::string workspace);
  bool workspace_favicons_pending_for_testing() const {
    return import_favicon_reads_ || !favicon_refreshing_.empty() || !import_favicon_queue_.empty();
  }
  ExtensionsBar* extensions_bar() const { return extensions_bar_; }
  bool OpenLink(const GURL& url, std::string source_workspace, bool new_tab, bool split = false);
  WorkspaceShell* shell() const override;
  bool NavigateFromBar(const GURL& url, bool new_tab) override;
  void SelectTab(size_t index);
  void CloseTab(size_t index);
  void CloseActiveTab();
  void ToggleSplit();
  // Adds a tab to the shown Split View group (starting a pair beside the shown
  // page); a full group replaces its focused pane.
  bool SplitTab(size_t index);
  // Separates the whole group, keeping the given tab on screen.
  void Unsplit(size_t index);
  // Takes one tab out of the group; the remaining pages stay together.
  void RemoveFromSplit(size_t index);
  // Closes every tab of the group containing `index` (a new tab remains if
  // the group held them all).
  void CloseSplitTabs(size_t index);
  // Moves a member one pane earlier or later (Arc's Move Left / Move Right).
  void MoveSplitPane(size_t index, bool later);
  // Reverses the pane order.
  void SwapSplit();
  void SetSplitVertical(bool vertical);
  bool split_vertical() const;
  void FocusPane(size_t pane);
  // Ratio of the two panes beside a divider; the other panes keep their size.
  void SetDividerRatio(size_t divider, double ratio, bool done = true);
  void SetSplitRatio(double ratio, bool done = true) { SetDividerRatio(0, ratio, done); }
  bool split() const;
  double split_ratio() const;
  size_t split_count() const;
  content::WebContents* pane_contents(size_t pane) const;
  // Sidebar reordering. `before` is a tab index (tab_count() appends) or a pin
  // id (empty appends to the parent folder). Crossing sections pins or unpins.
  bool MoveTab(size_t index, size_t before);
  bool PinTab(size_t index, std::string parent, std::string before);
  bool UnpinItem(std::string id, size_t before);
  bool UpdatePinnedUrl(std::string workspace, std::string id, GURL url);
  bool ResetPinnedUrl(std::string id);
  bool MovePin(std::string id, std::string parent, std::string before);
  void RunPageCommand(PageCommand command);
  void RunPageCommandOn(content::WebContents* page, PageCommand command, bool focused);
  GURL GetTabMenuUrl(std::optional<size_t> tab, std::string_view pin) const;
  // Time Machine (timeline.rs, ZitterTimeline.jsonl): every workspace's tabs over
  // time. A snapshot is a named mark on the active workspace's timeline.
  bool SaveSnapshot(std::string name);
  // Replaces the active workspace's tabs with every tab open in (t − quantum, t];
  // the state before is recorded first and offered as undo.
  bool RestoreTimeline(int64_t t_ms, int64_t quantum_ms);
  bool ReopenClosedTab();
  // `newest` counts from this run's most recently closed tab (History, Ostatnio zamknięte).
  bool ReopenClosedTabAt(size_t newest);
  HistoryPanel* history_panel() const { return history_panel_.get(); }
  PasswordsPanel* passwords_panel() const { return passwords_panel_.get(); }
  // Closes every tab of the active workspace but the selected one and pinned
  // pages; the toast's undo puts them back in their places.
  bool CloseOtherTabs();
  struct ClosedTabSlot { size_t index; GURL url; std::u16string title; };
  bool ReopenTabs(std::string workspace_id, std::vector<ClosedTabSlot> tabs);
  bool SleepWorkspace(std::string id);
  bool workspace_sleeping(std::string_view id) const;
  const sidebar::Timeline& timeline() const;
  base::Time Now() const { return clock_->Now(); }
  // Captures the active workspace now, so the view and Cmd+Shift+T see it.
  void RecordActiveTimeline() { RecordTimeline(false, active_id()); }
  void SetTimelineSettings(bool enabled, int days);
  bool ClearTimeline();
  bool timeline_enabled() const { return timeline_enabled_; }
  void SetClockForTesting(base::Clock* clock) { clock_ = clock; }
  struct TimelineMomentForTesting { std::vector<std::string> urls; size_t open = 0; };
  TimelineMomentForTesting timeline_moment_for_testing(std::string_view workspace_id, int64_t t_ms, int64_t quantum_ms) const;
  std::vector<std::string> timeline_marks_for_testing() const;
  size_t timeline_frames_for_testing() const;
  bool timeline_recorded_for_testing(std::string_view workspace_id) const;
  bool RunShortcut(std::string id);
  content::WebContents* find_page_for_testing() const { return find_page_; }
  size_t tab_count(std::string_view id) const;
  void SetCloseCallback(base::OnceClosure callback);
  void RequestClose();
  bool lifecycle_pending() const { return lifecycle_pending_; }
  void SetReadyCallback(base::OnceClosure callback);

 private:
  void OnBrowsingDataRemoverDone(uint64_t failed_data_types) override;
  raw_ptr<content::BrowsingDataRemover> data_remover_ = nullptr;
  std::unique_ptr<ScopedProfileKeepAlive> data_removal_lease_;
  std::u16string data_removal_profile_name_;
  base::WeakPtr<views::Widget> clear_data_dialog_;
  size_t clear_data_dialog_generation_ = 0;
  void FileSelected(const ui::SelectedFileInfo& file, int index) override;
  void FileSelectionCanceled() override;
  scoped_refptr<ui::SelectFileDialog> download_directory_dialog_;
  std::string download_directory_profile_;
  bool Activate(std::string_view workspace_id,
                std::string_view profile_id) override;
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;
  bool CanHandleAccelerators() const override;
  views::WebView* CreatePage(Profile* profile, views::View* parent = nullptr,
      std::unique_ptr<content::WebContents> owned = nullptr, bool create_contents = true);
  content::WebContents* EnsurePage(views::WebView* view);
  void PreparePage(content::WebContents* page, std::string source_hint = {});
  content::WebContents* AdoptPage(content::WebContents* source, std::unique_ptr<content::WebContents> page, bool background);
  bool RouteNavigation(content::NavigationHandle& navigation, const std::string& source_hint);
  content::WebContents* OpenPageLink(content::WebContents* source, const content::OpenURLParams& params);
  void ShowPageMenu(content::WebContents* source, content::RenderFrameHost* frame, const content::ContextMenuParams& params);
  std::string WorkspaceForPage(content::WebContents* page) const;
  void UpdateShell();
  void SetPageFullscreen(content::WebContents* page, bool fullscreen);
  base::WeakPtr<content::WebContents> fullscreen_page_;
  bool was_window_fullscreen_ = false;
  void ScheduleShellUpdate();
  bool shell_update_pending_ = false;
  void DrainExternalCommands();
  std::vector<std::string> pending_external_commands_;
  base::OneShotTimer external_command_timer_;
  void SaveConfiguration(base::OnceCallback<void(bool)> done = {});
  void SaveSelection();
  bool UpdateWorkspace(std::string id, std::string name, std::string profile_id);
  void RestoreConfiguration(std::string json);
  Profile* ProfileFor(std::string_view id) const;
  void LoadStartupProfiles();
  void LoadStartupProfile(size_t index);
  void StartupProfileLoaded(std::string id, std::string directory, Profile* profile);
  void ContinueStartupProfileLoading();
  bool StartupProfilesReady() const;
  bool DeferForProfile(std::string_view workspace, base::OnceClosure action);
  void FinishConfiguration();
  void ProfileCreated(ProfileInfo info, std::string workspace_id, Profile* profile);
  void ImportNextProfile(size_t index);
  void CommitArcImport();
  void CompleteArcImport(bool ok);
  void SaveFolderEdit(std::string workspace, std::vector<PinnedItem> previous, std::set<std::string> removed = {});
  void FinishArcImport(const WorkspaceShell::ImportStatusReport& status);
  bool CanRemoveProfile(std::string_view id) const;
  void ScheduleProfileDeletion(const std::string& directory);

  std::vector<content::WebContents*> WorkspacePages(std::string_view id, bool peek = true, bool any_peek = false) const;
  bool GuardPages(std::vector<content::WebContents*> pages, base::OnceClosure action);
  bool LifecycleScopeIsCurrent() const;
  void FinishLifecycle(bool proceed);
  void ArmCloseHandler();
  void CommitClose();
  BeforeUnloadBatch before_unload_batch_;
  std::vector<base::WeakPtr<content::WebContents>> lifecycle_topology_;
  base::WeakPtr<content::WebContents> lifecycle_source_page_;
  bool lifecycle_pending_ = false;
  bool lifecycle_approved_ = false;
  std::string lifecycle_workspace_;
  base::OnceClosure lifecycle_action_;
  base::OnceClosure close_callback_;
  void ShowPage(views::WebView* view);
  void RefreshPanes();
  void CloseWorkspaceTab(std::string_view workspace_id, size_t index);
  void PageFocused(views::WebView* view);
  void ScheduleSessionSave();
  void SchedulePreconnect(std::u16string_view query);
  void SaveSession();
  void ShowRestoredWindow(std::optional<base::DictValue> placement);
  void RememberWindowPlacement();
  void SaveWindowPlacement();
  void OnWidgetBoundsChanged(views::Widget*, const gfx::Rect&) override;
  void OnWidgetShowStateChanged(views::Widget*) override;
  void RestoreSession();
  struct Host;
  void AssociateReaderPin(Host& host, views::WebView* view);
  static GURL TabUrl(const Host& host, views::WebView* view);
  static std::u16string TabTitle(const Host& host, views::WebView* view);
  // The split group persists; it is on screen only while one of its members
  // is the selected page.
  static bool SplitShown(const Host& host);
  static bool InSplit(const Host& host, const views::WebView* view);
  static std::vector<views::WebView*> VisiblePanes(const Host& host);
  // Drops a page from the group (dissolving it below two members) and returns
  // the pane that takes its place, or nullptr.
  static views::WebView* LeaveSplit(Host& host, views::WebView* view);
  void ApplyPanes(const Host& host);
  bool InsertPin(std::string workspace, PinnedItem item, std::string before);
  void SaveGitHubFolder(std::string workspace, std::string folder, std::string name,
                        GitHubLiveFolder config);
  void ScheduleGitHubRefresh();
  void StartGitHubRefresh();
  void FetchGitHubResults(std::string token);
  void FinishGitHubRefresh(bool ok);
  base::DictValue CaptureHost(const Host& host) const;
  bool RestoreHost(Host& host, const base::DictValue& entry);
  void ReleaseHost(Host& host);
  void RecordTimeline(bool force, std::string_view workspace_id = {});
  // Puts back a layout captured with CaptureHost (Time Machine's undo).
  bool RestoreLayout(std::string workspace_id, base::DictValue entry);
  void LoadTimeline(std::string data);
  void AppendTimeline(std::string lines);
  void RewriteTimeline();
  void RecordClosedTab(const Host& host, size_t index);
  content::KeyboardEventProcessingResult PreHandlePageKeyEvent(
      content::WebContents* source,
      const input::NativeWebKeyboardEvent& event);

  struct Host {
    Host();
    ~Host();
    Host(Host&&) noexcept;
    Host& operator=(Host&&) noexcept;
    std::string workspace_id;
    std::string profile_id;
    raw_ptr<Profile> profile = nullptr;
    raw_ptr<views::WebView> view = nullptr;
    std::vector<raw_ptr<views::WebView>> tabs;
    size_t active_tab = 0;
    // Split View group in pane order: empty or 2..kMaxSplitPanes pages.
    std::vector<raw_ptr<views::WebView>> split;
    std::vector<double> weights;  // One size per pane, summing to one.
    bool vertical = false;
    std::map<views::WebView*, GURL> deferred_urls;
    std::map<views::WebView*, std::u16string> deferred_titles;
    std::set<views::WebView*> popup_tabs;
    std::map<std::string, raw_ptr<views::WebView>> pin_tabs;
    std::optional<base::DictValue> sleeping;
    // Tabs most recently shown first, for Ctrl+Tab. Entries may outlive their
    // tab and are only compared, never dereferenced; RecentTabs() prunes them.
    std::vector<views::WebView*> recent;
  };
  WorkspaceModel model_;
  raw_ptr<AssistantPanel> assistant_ = nullptr;
  base::CallbackListSubscription ad_blocking_changed_;
  std::map<std::string, std::unique_ptr<views::ViewTracker>> assistant_targets_;
  AssistantSettings assistant_settings_;
  base::WeakPtr<content::WebContents> night_shift_target_;
  GURL night_shift_url_;
  std::string night_shift_document_;
  std::string night_shift_request_;
  base::ListValue night_shift_candidates_;
  // Night Shift for the whole app (design: Night Shift · ustawienia).
  NightShiftConfig night_shift_config_;
  // Profile id -> config generation already written into its extension.
  std::map<std::string, int> night_shift_pushed_;
  int night_shift_generation_ = 0;
  void PushNightShiftConfig(Profile* profile);
  // Profiles whose bar hides the Night Shift moon (unpinned in the menu).
  std::set<std::string> night_shift_hidden_;
  std::string ProfileIdOf(Profile* profile) const;
  void ApplyNightShiftSettings(base::DictValue value);
  std::string NightShiftSettingsState();
  struct AssistantBackgroundPage {
    AssistantBackgroundPage();
    AssistantBackgroundPage(AssistantBackgroundPage&&);
    AssistantBackgroundPage& operator=(AssistantBackgroundPage&&);
    ~AssistantBackgroundPage();
    raw_ptr<views::WebView> view = nullptr;  // Hidden child of the content host.
    std::string profile_id, workspace_id, alias;  // `alias`: the tab id it was moved from.
    base::ScopedClosureRunner rendering;
  };
  std::map<std::string, AssistantBackgroundPage> assistant_background_;
  base::ListValue assistant_backend_status_;
  bool assistant_status_pending_ = false;
  std::set<std::string> assistant_checks_pending_;
  uint64_t assistant_connection_generation_=0;
  WorkspaceController controller_;
  std::vector<Host> hosts_;
  std::vector<LinkRule> link_rules_;
  struct RemovedWorkspace {
    Workspace workspace;
    std::vector<PinnedItem> pins;
    std::vector<LinkRule> rules;
    base::DictValue host;
    bool active = false;
  };
  std::optional<RemovedWorkspace> removed_workspace_;
  raw_ptr<views::WebView> peek_view_ = nullptr;
  // App-wide interaction preferences, persisted with the workspace catalogue.
  bool image_link_previews_ = false;
  bool shift_click_peek_ = true;
  bool clean_address_copies_ = true;
  base::RepeatingCallback<bool()> address_copy_preference_source_;
  std::string peek_workspace_id_;
  struct ProfileEntry {
    ProfileInfo info;
    std::unique_ptr<ScopedProfileKeepAlive> lease;
  };
  std::vector<ProfileEntry> profiles_;
  std::vector<std::string> startup_profile_order_;
  size_t next_startup_profile_ = 0;
  size_t pending_startup_profiles_ = 0;
  bool startup_profile_failed_ = false;
  bool startup_background_allowed_ = false;
  StartupProfileLoader startup_profile_loader_for_testing_;
  std::set<std::string> startup_profiles_requested_;
  base::OnceClosure pending_profile_action_;
  std::string pending_profile_id_;
  std::vector<std::string> deleted_directories_;
  // Owned by Runtime; all private windows release their pages before it dies.
  raw_ptr<Profile> private_profile_ = nullptr;
  base::RepeatingClosure new_private_window_;
  base::FilePath data_directory_;
  std::string restored_active_;
  bool configuration_invalid_ = false;
  bool profiles_busy_ = false;
  bool offer_import_ = true;
  bool onboarding_complete_ = false;
#if BUILDFLAG(IS_WIN)
  // The spaces the jump list last showed (id, name), so it is rewritten only
  // when they change.
  std::vector<std::pair<std::string, std::string>> jump_list_spaces_;
#endif
  std::string starter_workspace_id_;
  bool starter_has_imported_passwords_ = false;
  bool import_replaces_starter_ = false;
  size_t import_preview_generation_ = 0;
  std::map<std::string, std::vector<PinnedItem>> pins_;
  using GitHubFolderKey = std::pair<std::string, std::string>;
  struct GitHubRefresh {
    GitHubFolderKey key;
    std::string profile_id;
    GitHubLiveFolder config;
    std::vector<std::string> queries;
    std::vector<PinnedItem> results;
    bool manual = false;
  };
  base::OneShotTimer github_timer_;
  std::map<GitHubFolderKey, base::TimeTicks> github_next_refresh_;
  std::optional<GitHubFolderKey> github_manual_refresh_;
  std::optional<GitHubRefresh> github_refresh_;
  std::unique_ptr<network::SimpleURLLoader> github_loader_;
  mutable std::map<std::string, std::vector<PinnedItem>> combined_pins_;
  std::map<std::pair<std::string, std::string>, std::vector<uint32_t>> favicon_cache_;
  using FaviconKey = std::pair<std::string,std::string>;
  std::map<FaviconKey,uint64_t> favicon_generations_;
  std::map<FaviconKey, bool> favicon_refreshing_;  // Whether to discard an icon on failure.
  struct ImportedFaviconSource {
    std::unique_ptr<ScopedProfileKeepAlive> lease;
    std::unique_ptr<content::WebContents> page;
  };
  std::set<FaviconKey> import_favicon_queue_;
  size_t import_favicon_reads_ = 0;
  std::map<std::string, ImportedFaviconSource> import_favicon_sources_;
  void QueueImportedFavicons(const ArcImport& source);
  void PumpImportedFavicons();
  void StartWorkspaceFavicon(Profile* profile, std::string profile_id,
                             content::WebContents* source, GURL url,
                             GURL known_icon, bool force);
  void FetchWorkspaceFavicon(Profile* profile,
                             base::WeakPtr<content::WebContents> source,
                             FaviconKey key, uint64_t generation, GURL known_icon);
  void DiscoverWorkspaceFavicon(base::WeakPtr<content::WebContents> source,
                                 FaviconKey key, uint64_t generation, GURL known_icon,
                                 std::unique_ptr<network::SimpleURLLoader> loader,
                                 std::optional<std::string> markup);
  void DownloadWorkspaceFavicon(base::WeakPtr<content::WebContents> source,
                                 FaviconKey key, uint64_t generation,
                                 std::vector<GURL> icons, size_t index = 0,
                                 size_t renderer_attempt = 0);
  void FinishWorkspaceFavicon(FaviconKey key,uint64_t generation,GURL icon,gfx::Image image);
  std::string PinStorageKey(std::string_view workspace, std::string_view pin) const;
  std::optional<ArcImport> pending_import_;
  base::OnceCallback<void(bool, std::u16string)> welcome_import_done_;
  std::shared_ptr<BrowsingImportData> import_browsing_data_;
  bool import_loading_ = false;
  std::vector<std::string> import_new_profile_ids_;
  std::vector<std::string> import_new_workspace_ids_;
  std::vector<GitHubFolderKey> import_upgraded_github_folders_;
  base::FilePath configuration_path_;
  scoped_refptr<base::SequencedTaskRunner> config_runner_;
  gfx::Rect windowed_bounds_;
  bool window_placement_ready_ = false;
  bool window_maximized_ = false;
  bool window_fullscreen_ = false;
  base::OnceClosure ready_callback_;
  bool configuration_ready_ = false;
  int pending_config_writes_ = 0;
  // Shared across hosts: keydown can switch the destination of keyup.
  bool suppress_shortcut_char_ = false;
  bool closing_ = false;
  bool closing_split_tabs_ = false;  // One snapshot covers the whole group.
  bool restoring_session_ = false;
  std::string session_json_;
  // The Rust timeline (timeline_store.h keeps the cxx types out of this header).
  std::unique_ptr<TimelineStore> timeline_;
  raw_ptr<base::Clock> clock_;
  bool timeline_enabled_ = true;
  bool timeline_settings_known_ = false;
  int timeline_days_ = 30;
  // Recently closed tabs of this run (Cmd+Shift+T), kept even with the timeline off.
  struct ClosedTab { std::string workspace_id, profile_id; GURL url; std::u16string title; base::Time closed_at; };
  std::vector<ClosedTab> closed_tabs_;
  std::set<int> consumed_keys_;
  // Ctrl+Tab walks tabs in most-recently-used order while Ctrl is held; the
  // tab shown when Ctrl goes up becomes the most recent one.
  struct TabCycle {
    std::string workspace_id;
    std::vector<views::WebView*> order;
    size_t position = 0;
    base::TimeTicks last_step;
  };
  std::optional<TabCycle> tab_cycle_;
  base::RepeatingTimer tab_cycle_timer_;
  void NoteRecentTab();
  std::vector<views::WebView*> RecentTabs(const Host& host) const;
  void CycleRecentTab(bool backwards);
  void CheckTabCycle();
  void FinishTabCycle();
  base::OneShotTimer session_timer_;
  base::OneShotTimer preconnect_timer_;
  std::string last_preconnect_profile_;
  GURL last_preconnect_origin_;
  base::TimeTicks last_preconnect_time_;
  base::CancelableTaskTracker history_tasks_;
  base::CancelableTaskTracker favicon_tasks_;
  std::set<std::pair<std::string,std::string>> favicon_requests_;
  size_t search_generation_ = 0;
  std::unique_ptr<WorkspaceShell> shell_;
  // Widget must die before its non-owned delegate and before profiles.
  std::unique_ptr<views::WidgetDelegate> delegate_;
  std::unique_ptr<views::Widget> widget_;
  std::unique_ptr<DownloadPanel> downloads_;
  std::unique_ptr<HistoryPanel> history_panel_;
  std::unique_ptr<PasswordsPanel> passwords_panel_;
  std::unique_ptr<AdBlockingRulesPanel> ad_blocking_rules_panel_;
  raw_ptr<ExtensionsBar> extensions_bar_ = nullptr;  // Owned by the window root.
  std::unique_ptr<base::AutoReset<bool>> extension_install_ui_;
  // Extensions see the window's tabs through ExtensionTabUtil::EmbedderTabs.
  class ExtensionTabs;
  std::unique_ptr<ExtensionTabs> extension_tabs_;
  base::WeakPtr<content::WebContents> shown_page_;
  // Find in page and printing (design: Find & Print).
  bool StartFind();
  void Find(std::u16string text, bool forward, bool match_case);
  void StopFind();
  gfx::Rect FindBounds() const;
  void OnFindResultAvailable(content::WebContents* page) override;
  void OnFindTabHelperDestroyed(find_in_page::FindTabHelper* helper) override;
  bool StartPrint(content::WebContents* page = nullptr);
  raw_ptr<content::WebContents> find_page_ = nullptr;
  base::ScopedObservation<find_in_page::FindTabHelper, find_in_page::FindResultObserver> find_observation_{this};
  std::unique_ptr<PagePrinter> printer_;
  base::WeakPtrFactory<WorkspaceWindow> weak_factory_{this};
};
}  // namespace zitter::prototype
#endif
