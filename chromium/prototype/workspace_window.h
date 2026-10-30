#ifndef ZITTER_CHROMIUM_PROTOTYPE_WORKSPACE_WINDOW_H_
#define ZITTER_CHROMIUM_PROTOTYPE_WORKSPACE_WINDOW_H_

#include <array>
#include <memory>
#include "base/auto_reset.h"
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/callback.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/browsing_data_remover.h"
#include "base/timer/timer.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/shell_dialogs/select_file_dialog.h"
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
namespace views {
class Label;
class Textfield;
class WebView;
class Widget;
class WidgetDelegate;
}  // namespace views

namespace zitter::prototype {

class DownloadPanel;
class ExtensionsBar;
struct BrowsingImportData;

// Intentionally a substrate probe, not a replacement BrowserWindowInterface.
// The window owns profile keep-alives; WebViews own their WebContents.
class WorkspaceWindow final : public WorkspaceRuntime,
                              public ui::AcceleratorTarget,
                              public ui::SelectFileDialog::Listener,
                              public content::BrowsingDataRemover::Observer {
 public:
  WorkspaceWindow(Profile* work, Profile* personal);
  explicit WorkspaceWindow(base::FilePath data_directory, bool offer_import = true);
  ~WorkspaceWindow() override;
  WorkspaceWindow(const WorkspaceWindow&) = delete;
  WorkspaceWindow& operator=(const WorkspaceWindow&) = delete;

  void PreviewArcImport(base::FilePath arc_directory);
  void PreviewBrowserImport(BrowserImportOptions options, base::FilePath directory = {});
  void InvalidateImportPreview();
  bool ApplyArcImport();
  void DismissImport();
  bool import_ready() const { return pending_import_.has_value() && pending_import_->error.empty(); }
  const ArcImport* import_preview() const { return pending_import_ ? &*pending_import_ : nullptr; }
  const std::vector<PinnedItem>& pinned_items(std::string_view workspace_id) const;
  void OpenPinnedItem(std::string id);
  bool CreatePinnedFolder(std::string workspace, std::string parent, std::string name);
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
  bool configuration_saved() const { return pending_config_writes_ == 0 && !session_timer_.IsRunning() && !time_machine_timer_.IsRunning(); }
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
  bool ExecuteCommand(const PaletteResult& result);
  bool MoveActiveTab(std::string target_id);
  void ShowPasswords();
  void ShowDownloads();
  void ShowHistory();
  DownloadPanel* downloads() const { return downloads_.get(); }
  void DownloadCompleted(std::string guid, std::u16string name);
  BrowserPreferences ReadPreferences() const;
  std::u16string DownloadDirectory() const;
  void ChooseDownloadDirectory();
  bool WritePreference(std::string key, int value);
  bool WriteTextPreference(std::string key, std::string value);
  std::vector<SitePreferenceRule> ReadSitePreferences() const;
  bool WriteSitePreference(std::string origin, std::string key, int setting);
  bool RemoveSitePreference(SitePreferenceRule rule);
  void QueryBrowsingHistory(std::u16string query, size_t limit);
  void RequestClearBrowsingData(BrowsingDataSelection selection);
  bool ClearBrowsingData(std::string profile_id, BrowsingDataSelection selection);
  bool clearing_browsing_data() const { return data_remover_ != nullptr; }
  views::Widget* clear_data_dialog() const { return clear_data_dialog_.get(); }
  void OpenSystemSettings(bool certificates);
  bool OpenPeek(const GURL& url, std::string source_workspace = {});
  void ClosePeek();
  bool PromotePeek(bool split);
  content::WebContents* peek_contents() const;
  bool SetLinkRule(const GURL& url, std::string workspace_id);
  std::string WorkspaceForUrl(const GURL& url) const;
  const std::vector<LinkRule>& link_rules() const { return link_rules_; }
  bool OpenExternalUrl(const GURL& url);
  // Opens an extension's page (options, chrome://extensions, a link from its
  // popup) as a new tab in a space that uses `profile`.
  bool OpenExtensionPage(Profile* profile, const GURL& url);
  // The same as a tab of the tabs API: shown when `active`, else opened behind.
  content::WebContents* OpenProfileTab(Profile* profile, const GURL& url, bool active);
  // Moves a tab to another space of its profile at `position` (tabs API).
  bool MoveTabToSpace(content::WebContents* page, const std::string& target, size_t position);
  ExtensionsBar* extensions_bar() const { return extensions_bar_; }
  bool OpenLink(const GURL& url, std::string source_workspace, bool new_tab, bool split = false);
  WorkspaceShell* shell() const { return shell_.get(); }
  bool NavigateFromBar(const GURL& url, bool new_tab);
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
  bool MovePin(std::string id, std::string parent, std::string before);
  void RunPageCommand(PageCommand command);
  bool SaveSnapshot(std::string name, bool automatic = false);
  bool RestoreSnapshot(std::string id);
  bool DeleteSnapshot(std::string id);
  bool ReopenClosedTab(std::string id = {});
  bool SleepWorkspace(std::string id);
  bool workspace_sleeping(std::string_view id) const;
  TimeMachineState TimeMachine() const;
  bool RunShortcut(std::string id);
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
  content::WebContents* OpenPageLink(content::WebContents* source, const content::OpenURLParams& params, bool peek);
  void ShowPageMenu(content::WebContents* source, const content::ContextMenuParams& params);
  std::string WorkspaceForPage(content::WebContents* page) const;
  void UpdateShell();
  void ScheduleShellUpdate();
  bool shell_update_pending_ = false;
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
  void ContinueLifecycle(bool proceed);
  void FinishLifecycle(bool proceed);
  void ArmCloseHandler();
  void CommitClose();
  struct LifecyclePage {
    base::WeakPtr<content::WebContents> page;
    int entry_id = 0;
    bool checked = false;
    size_t navigation_generation = 0;
  };
  std::vector<LifecyclePage> lifecycle_pages_;
  std::vector<base::WeakPtr<content::WebContents>> lifecycle_topology_;
  base::WeakPtr<content::WebContents> lifecycle_source_page_;
  size_t lifecycle_index_ = 0;
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
  void RestoreSession();
  struct Host;
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
  base::DictValue CaptureHost(const Host& host) const;
  bool RestoreHost(Host& host, const base::DictValue& entry);
  void ReleaseHost(Host& host);
  void SaveTimeMachine();
  void FlushTimeMachine();
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
  };
  WorkspaceModel model_;
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
  base::FilePath data_directory_;
  std::string restored_active_;
  bool configuration_invalid_ = false;
  bool profiles_busy_ = false;
  bool offer_import_ = true;
  bool onboarding_complete_ = false;
  std::string starter_workspace_id_;
  bool import_replaces_starter_ = false;
  size_t import_preview_generation_ = 0;
  std::map<std::string, std::vector<PinnedItem>> pins_;
  std::optional<ArcImport> pending_import_;
  std::shared_ptr<BrowsingImportData> import_browsing_data_;
  bool import_loading_ = false;
  std::vector<std::string> import_new_profile_ids_;
  std::vector<std::string> import_new_workspace_ids_;
  base::FilePath configuration_path_;
  scoped_refptr<base::SequencedTaskRunner> config_runner_;
  base::OnceClosure ready_callback_;
  bool configuration_ready_ = false;
  int pending_config_writes_ = 0;
  // Shared across hosts: keydown can switch the destination of keyup.
  bool suppress_shortcut_char_ = false;
  bool closing_ = false;
  bool closing_split_tabs_ = false;  // One snapshot covers the whole group.
  bool restoring_session_ = false;
  std::string session_json_;
  base::DictValue time_machine_;
  base::Time last_automatic_snapshot_;
  std::set<int> consumed_keys_;
  base::OneShotTimer session_timer_;
  base::OneShotTimer time_machine_timer_;
  base::OneShotTimer preconnect_timer_;
  std::string last_preconnect_profile_;
  GURL last_preconnect_origin_;
  base::TimeTicks last_preconnect_time_;
  base::CancelableTaskTracker history_tasks_;
  base::CancelableTaskTracker history_view_tasks_;
  size_t history_view_generation_ = 0;
  size_t search_generation_ = 0;
  std::unique_ptr<WorkspaceShell> shell_;
  // Widget must die before its non-owned delegate and before profiles.
  std::unique_ptr<views::WidgetDelegate> delegate_;
  std::unique_ptr<views::Widget> widget_;
  std::unique_ptr<DownloadPanel> downloads_;
  raw_ptr<ExtensionsBar> extensions_bar_ = nullptr;  // Owned by the window root.
  std::unique_ptr<base::AutoReset<bool>> extension_install_ui_;
  // Extensions see the window's tabs through ExtensionTabUtil::EmbedderTabs.
  class ExtensionTabs;
  std::unique_ptr<ExtensionTabs> extension_tabs_;
  base::WeakPtr<content::WebContents> shown_page_;
  base::WeakPtrFactory<WorkspaceWindow> weak_factory_{this};
};
}  // namespace zitter::prototype
#endif
