// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CONTENT_WORKSPACE_LAB_H_
#define ZITTER_CONTENT_WORKSPACE_LAB_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/callback_list.h"
#include "base/containers/span.h"
#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/process/process_handle.h"
#include "base/scoped_observation.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/time/time.h"
#include "base/unguessable_token.h"
#include "base/values.h"
#include "base/task/sequenced_task_runner.h"
#include "build/build_config.h"
#include "chromium/prototype/assistant_host.h"
#include "chromium/prototype/before_unload_batch.h"
#include "components/find_in_page/find_result_observer.h"
#include "content/public/browser/weak_document_ptr.h"
#include "content/public/browser/browsing_data_remover.h"
#include "content/public/browser/web_contents_observer.h"
#include "extension_registry_probe.h"
#include "pdf/buildflags.h"
#include "printing/buildflags/buildflags.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/window_open_disposition.h"
#include "ui/gfx/native_ui_types.h"
#include "ui/views/views_delegate.h"
#include "ui/views/widget/widget_observer.h"
#include "url/gurl.h"
#include "zitter/application/workspace_controller.h"
#include "zitter/core/browser_state.h"
#include "zitter/core/workspace_model.h"
#include "zitter/ports/workspace_runtime.h"
#include "workspace_session_store.h"
#include "chromium/prototype/night_shift_config.h"

#include "ui/shell_dialogs/select_file_dialog.h"

namespace content {
class BrowserContext;
class NavigationHandle;
struct ContextMenuParams;
struct OpenURLParams;
}
namespace ui {
class ScopedSetSelectFileDialogFactory;
}
namespace views {
class BubbleDialogDelegate;
class DialogDelegate;
class LayoutProvider;
class Checkbox;
class Combobox;
class View;
class WebView;
class Widget;
class WidgetDelegate;
}
namespace gfx { class Image; }

namespace zitter::prototype {
struct ShellState;
struct BrowsingImportData;
struct BrowsingImportResult;
enum class PageCommand;
struct PrintOptions;
class FingerprintToolbar;
class ShieldButton;
class ShieldCard;
class AdBlockingRulesPanel;
class AssistantPanel;
class AdblockFilterLists;
class DownloadPanel;
class HistoryPanel;
class PasswordsPanel;
class WorkspaceShell;
struct PaletteResult;
struct TimelineStore;
}

namespace os_crypt_async { class OSCryptAsync; }

namespace extensions {
class Extension;
class PermissionSet;
}  // namespace extensions

namespace zitter::content_embedder {

inline constexpr char kInteractiveImportProbe[] =
    "zitter-content-interactive-import-probe";
inline constexpr char kInteractiveImportKeychainProbe[] =
    "zitter-content-interactive-import-keychain-probe";

inline constexpr char kWorkspaceLab[] = "zitter-content-workspace";
inline constexpr char kWorkspaceKeychain[] = "zitter-content-workspace-keychain";
inline constexpr char kWorkspaceLegacyKeychain[] =
    "zitter-content-workspace-legacy-keychain";
inline constexpr char kWorkspaceProbe[] = "zitter-content-workspace-probe";
inline constexpr char kWorkspaceRoot[] = "zitter-content-workspace-root";
inline constexpr char kOpenPreparedWorkspace[] =
    "zitter-content-open-prepared-workspace";
inline constexpr char kWorkspaceSmokeTest[] =
    "zitter-content-workspace-smoke-test";
inline constexpr char kWorkspaceTlsProbe[] = "zitter-content-workspace-tls-probe";
inline constexpr char kWorkspaceTrustedTlsProbe[] =
    "zitter-content-workspace-trusted-tls-probe";
inline constexpr char kWorkspaceNativePasskeyPreflight[] =
    "zitter-content-workspace-native-passkey-preflight";
inline constexpr char kWorkspaceWebAuthnProbe[] =
    "zitter-content-workspace-webauthn-probe";
inline constexpr char kWorkspaceHistoryPanelProbe[] =
    "zitter-content-history-panel-smoke-test";
inline constexpr char kWorkspaceAddressHistoryProbe[] =
    "zitter-content-address-history-smoke-test";
inline constexpr char kWorkspacePasswordProbe[] =
    "zitter-content-workspace-password-probe";
inline constexpr char kWorkspaceClearDataProbe[] =
    "zitter-content-clear-data-smoke-test";

class LabViewsDelegate final : public views::ViewsDelegate {
#if BUILDFLAG(IS_MAC)
 public:
  ui::ContextFactory* GetContextFactory() override;
#endif
};

class AdblockCacheProbe;
class WorkspaceDevTools;
class WorkspaceWebAuthnProbe;
class WorkspacePdfExport;
class WorkspacePdfPreview;
class ProfileContext;
class AssistantReloadNavigation;
class WorkspaceMediaAccess;
class ExtensionNavigationProbe;
class ManagedExtension;
class WorkspaceExtensionInstall;

class WorkspaceFaviconRefresh;
class WorkspacePasswordClear;

class WorkspaceLab final : private WorkspaceRuntime,
                           private prototype::AssistantHost,
                           private content::BrowsingDataRemover::Observer,
                           private content::WebContentsObserver,
                           private find_in_page::FindResultObserver,
                           private ui::AcceleratorTarget,
                           private ui::SelectFileDialog::Listener,
                           private views::WidgetObserver {
 public:
  WorkspaceLab(const base::FilePath& settings_path,
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
               WorkspaceLab* private_owner = nullptr,
               base::span<const std::unique_ptr<ProfileContext>> extra_profiles = {},
               os_crypt_async::OSCryptAsync* os_crypt = nullptr,
               base::FilePath user_download_directory = {},
               std::string download_safe_browsing_key = {});
  ~WorkspaceLab() override;

  void Start(base::OnceCallback<void(bool)> done,
             base::OnceClosure quit_from_app_for_testing = {});
  void RequestClose();
  void OpenExternalUrl(GURL url);
  void SetApplicationVersion(std::string version);
  prototype::WorkspaceShell* shell() const override;
  base::span<const raw_ptr<ProfileContext>> profiles_for_shutdown() const {
    return contexts_;
  }
  bool HandleNativeMenuCommand(gfx::NativeWindow key,
                               gfx::NativeWindow main,
                               std::string command,
                               bool validate);
  void SetNativeMenuProbe(base::RepeatingCallback<bool(std::string)> activate);
  WorkspaceExtensionTab* CreateExtensionTab(content::BrowserContext& profile,
                                            const GURL& url, bool active);
  bool UpdateExtensionTab(WorkspaceExtensionTab& target, const GURL& url,
                          bool active, std::optional<bool> muted,
                          std::string_view extension_id);
  bool ReloadExtensionTab(WorkspaceExtensionTab& target, bool bypass_cache);
  WorkspaceExtensionTab* DuplicateExtensionTab(WorkspaceExtensionTab& target);
  WorkspaceExtensionTab* DiscardExtensionTab(content::BrowserContext& profile,
                                             WorkspaceExtensionTab* target);
  bool NavigateExtensionTabHistory(WorkspaceExtensionTab& target, bool forward);
  void RefreshExtensionActionAppearance();
  base::ListValue GetExtensionCommands(content::BrowserContext& profile,
                                      const extensions::Extension& extension) const;
  bool OpenExtensionOptionsTab(content::BrowserContext& profile,
                               const std::string& id);
  bool SetExtensionSidePanel(WorkspaceExtensionTab& tab,
                             const std::string& id, const GURL& url,
                             bool toggle = false);

  void PromptExtensionPermissions(content::BrowserContext& profile,
                                  const extensions::Extension& extension,
                                  const extensions::PermissionSet& permissions,
                                  base::OnceCallback<void(bool)> answer);
  void RemoveExtensionTabs(base::span<WorkspaceExtensionTab* const> targets,
                           base::OnceCallback<void(bool)> done);
  bool MoveExtensionTabs(content::BrowserContext& profile,
                         base::span<WorkspaceExtensionTab* const> targets,
                         int index);

 private:
  bool Activate(std::string_view workspace_id,
                std::string_view profile_id) override;
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;
  bool CanHandleAccelerators() const override;
  class ExtensionShortcutTarget;
  void RefreshExtensionShortcuts();
  bool DispatchExtensionShortcut(const ui::Accelerator& accelerator);

  void DocumentOnLoadCompletedInPrimaryMainFrame() override;
  void DidStartNavigation(content::NavigationHandle* navigation) override;
  void DidFinishNavigation(content::NavigationHandle* navigation) override;
  void DidStartLoading() override;
  void DidStopLoading() override;
  void TitleWasSet(content::NavigationEntry*) override;
  void ArmCloseHandler();
  void ConfirmDownloadShutdown();
  void OnDownloadShutdownAnswered(bool close);
  void CommitClose();
  void SaveSession(base::OnceCallback<void(bool)> done);
  void PersistWorkspaceChanges();
  void PinSessionSaved(bool crash_probe, bool ok);
  void SessionSaved(bool ok);
  base::DictValue CaptureSession() const;
  base::DictValue CaptureWorkspaceSession(size_t workspace) const;
  std::vector<content::WebContents*> OpenPages() const;
  std::vector<content::WebContents*> WorkspacePages(size_t workspace) const;
  void CheckBeforeUnload(const std::vector<content::WebContents*>& pages,
                         base::OnceClosure action,
                         base::OnceClosure cancelled = {});
  void SelectWorkspace(std::string id);
  std::vector<prototype::PaletteResult> WorkspaceCommandResults(
      std::u16string query) const;
  void ActivateWorkspaceCommand(prototype::PaletteResult result);
  bool ActivateWorkspaceCommandForProbe(std::u16string query,
                                        std::string_view id);
  bool SleepWorkspace(std::string_view id);
  bool CommitWorkspaceSleep(size_t workspace);
  bool WorkspaceHasCapture(size_t workspace) const;
  struct Tab;
  void SelectTab(size_t index);
  bool DuplicateTab(size_t index);
  bool TabMuted(size_t index) const;
  void SetTabMuted(size_t index, bool muted);
  bool ActivateTabMuteMenuForTesting(size_t index, bool muted);
  std::optional<size_t> DuplicateWorkspaceTab(size_t workspace, size_t index);
  bool DuplicatePin(std::string id);
  bool MoveSidebarTab(size_t index, std::string target);
  bool CommitTabMove(size_t source_workspace, size_t index, size_t target);
  Tab DetachTab(size_t workspace, size_t index, bool show = true);
  std::vector<Tab> DetachTabs(size_t workspace, base::span<const size_t> indices,
                              bool show = true);
  void ReorderWorkspaceTab(size_t workspace, size_t index, size_t before);
  void CloseTab(size_t index);
  void RemoveTab(size_t index);
  void CommitExtensionTabRemoval(std::vector<std::pair<size_t, size_t>> tabs);
  void CompleteExtensionTabRemoval(bool success);
  std::optional<std::pair<size_t, size_t>> ExtensionTabPosition(
      const WorkspaceExtensionTab& target) const;
  void NavigateExtensionTab(size_t workspace, Tab& tab, const GURL& url);
  content::WebContents& EnsureHiddenTabPage(size_t workspace, Tab& tab,
                                               const GURL& url);
  void ReloadWorkspacePage(content::WebContents& page, bool bypass_cache);
  struct ClosedTab {
    GURL url;
    std::u16string title;
    base::Time closed_at;
    uint64_t tab_id = 0;
  };
  ClosedTab RecordClosedTab(size_t workspace, size_t index);
  void UndoCloseOtherTabs(std::string workspace_id, std::string profile_id,
                          std::vector<std::pair<size_t, ClosedTab>> closed);
  std::optional<ClosedTab> HistoricalClosedTab() const;
  bool ReopenLastClosedTab();
  void OpenRecoveredTab(ClosedTab tab);
  void RecordTimeline(bool force);
  void PersistTimeline(base::OnceCallback<void(bool)> done = {});
  bool ClearTimeline();
  void ReplaceTimeline(std::unique_ptr<prototype::TimelineStore> replacement,
                       bool offer_undo);
  void TimelineReplacementSaved(
      std::unique_ptr<prototype::TimelineStore> previous,
      bool offer_undo,
      bool ok);
  void SetTimelineSettings(bool enabled, int days);
  bool SaveTimelineSnapshot(std::string name);
  bool RestoreTimeline(int64_t time_ms, int64_t quantum_ms);
  bool ApplyTimelineLayout(size_t workspace,
                           base::DictValue layout,
                           std::u16string restore_time);
  void CommitTimelineLayout(size_t workspace,
                            base::DictValue layout,
                            std::u16string restore_time);
  std::optional<size_t> LastClosedTab() const;
  bool ReopenClosedTab(size_t newest);
  Tab MakeTab(size_t workspace, views::View& host);
  void AddTab(size_t workspace, GURL deferred_url = {},
              std::u16string deferred_title = {}, std::string pin = {});
  void OpenPin(std::string id);
  bool PinTab(size_t index, std::string parent, std::string before);
  bool Unpin(std::string id, size_t before);
  bool MoveTab(size_t index, size_t before);
  bool MovePin(std::string id, std::string parent, std::string before);
  bool UpdatePinnedUrl(std::string workspace, std::string id, GURL url);
  bool ResetPinnedUrl(std::string id);
  bool CanEditWorkspace() const;
  bool CreatePinnedFolder(std::string workspace, std::string parent, std::string title);
  bool RenamePinnedFolder(std::string workspace, std::string id, std::string title);
  bool RemovePinnedFolder(std::string workspace, std::string id, bool contents);
  std::vector<prototype::PinnedItem> WorkspacePins(size_t workspace) const;
  std::vector<prototype::PinnedItem>& PinStorage(size_t workspace,
                                                 std::string_view id);
  bool PlacePin(std::vector<prototype::PinnedItem>& pins, prototype::PinnedItem pin,
                std::string parent, std::string before);
  void ShowTab();
  bool CanFullscreenPage(content::WebContents* page) const;
  bool ExitPageFullscreen();
  void SetPageFullscreen(content::WebContents* page, bool enter);
  void OnWidgetShowStateChanged(views::Widget* widget) override;
  bool SplitShown() const;
  bool SplitTab(size_t index);
  void SplitPin(std::string id);
  bool RemoveSplitMember(size_t workspace, size_t index);
  void LeaveSplit(size_t index);
  void CloseSplitTabs(size_t index);
  void CommitCloseSplitTabs();
  std::vector<size_t> OtherUnpinnedTabs() const;
  bool CloseOtherTabs();
  void CommitCloseOtherTabs(std::vector<size_t> indices);
  void MoveSplitPane(size_t index, bool later);
  void UpdateSplitPresentation();
  void ClearSplit(size_t index);
  void FocusPane(size_t pane);
  void FocusVisibleView(views::WebView* view);
  void ResizeSplit(size_t divider, double ratio, bool done);
  void SetSplitVertical(bool vertical);
  GURL TabUrl(std::optional<size_t> tab, std::string_view pin) const;
  bool OpenLink(const GURL& url, std::string source, bool new_tab, bool split);
  bool OpenPeek(const GURL& url, std::string source);
  bool PromotePeek(bool split);
  void ClosePeek();
  bool DeferPeekClose(base::OnceClosure follow_up);
  void DiscardPeek();
  content::WebContents* PeekPage() const;
  void UpdatePeekTitle(content::WebContents* page);
  bool SetLinkRule(const GURL& url, std::string workspace);
  bool RouteNavigation(content::NavigationHandle& navigation);
  void FinishRoutedNavigation(GURL url, std::string source, std::string destination,
                              base::WeakPtr<content::WebContents> page,
                              std::optional<uint64_t> popup_id, bool peek);
  content::WebContents* AdoptPage(content::WebContents* source,
                                  std::unique_ptr<content::WebContents> page,
                                  const GURL& target_url,
                                  WindowOpenDisposition disposition,
                                  bool user_gesture, bool shift_link);
  content::WebContents* OpenPageURL(
      content::WebContents* source, const content::OpenURLParams& params,
      base::OnceCallback<void(content::NavigationHandle&)> navigation_callback);
  void FinishOpeningPage(std::string workspace, uint64_t tab_id, bool background,
                         bool resume_requests, bool from_peek);
  void ClosePopup(content::WebContents* page);
  std::optional<std::pair<size_t, size_t>> TabForPage(
      content::WebContents* page) const;
  void AttachPageServices(content::WebContents& page, size_t workspace);
  content::WebContents* PageForNavigationCommand();
  void RunPageCommandOn(content::WebContents& page, prototype::PageCommand command);
  bool ShowPageMenu(content::RenderFrameHost& frame,
                    const content::ContextMenuParams& params);
  bool Navigate(const GURL& url, bool new_tab);
  bool RunShortcut(std::string id);
  std::optional<size_t> WorkspaceIndex(std::string_view id) const;
  size_t WorkspaceProfileIndex(size_t workspace) const;
  ProfileContext* WorkspaceProfile(size_t workspace) const;
  bool OpenNightShiftSettings(content::RenderFrameHost* document = nullptr);
  void ApplyNightShiftSettings(base::DictValue value);
  std::string_view WorkspaceProfileName(size_t workspace) const;
  void AppendWorkspace(Workspace workspace, const base::DictValue* session);
  bool AddWorkspace(std::string name, std::string profile_id,
                    base::OnceCallback<void(std::string)> committed = {});
  bool RemoveWorkspace(std::string id);
  void CommitWorkspaceRemoval(std::string id);
  bool RestoreWorkspace(std::string id);
  bool UpdateWorkspace(std::string id, std::string name, std::string profile_id);
  void SaveWorkspaceUpdate(std::string id, std::string name,
                           std::string profile_id);
  void ReplaceWorkspaceProfile(size_t index, const std::string& epoch);
  bool RenameProfile(std::string id, std::string name);
  void SaveCatalogueChange(base::DictValue session,
                           base::OnceCallback<void(bool)> done);
  bool CreateProfile(std::string name, std::string workspace_id);
  std::unique_ptr<ProfileContext> OpenNewProfile(const ContentProfileInfo& profile);
  void PublishProfile(ContentProfileInfo profile, std::unique_ptr<ProfileContext> context);
  void ConfigureProfileForWorkspace(ProfileContext& profile);
  bool CanImportBrowser() const;
  void PreviewBrowserImport(prototype::BrowserImportOptions options);
  void InvalidateBrowserImport();
  bool ApplyBrowserImport();
  void ImportDirectoriesReady(std::shared_ptr<prototype::BrowsingImportData> data);
  void ImportedBrowsingDataWritten(prototype::BrowsingImportResult result);
  void PublishBrowserImport();
  void FinishBrowserImport(bool ok, std::string error = {});
  void SleepOtherWorkspaces();
  bool OpenRss();
  bool OpenHistory();
  bool OpenDownloads();
  void DownloadCompleted(std::string guid, std::u16string name);
  void OpenSitePanel();
  void ShowClipboardReadPrompt(content::RenderFrameHost& frame,
                               base::OnceCallback<void(bool)> answer);
  enum class PermissionPrompt { kClipboard, kDeviceCapture, kTabCapture, kExtension };
  void ShowPermissionPrompt(std::u16string title, std::u16string text,
                            std::u16string allow, PermissionPrompt kind,
                            std::vector<std::u16string> choices,
                            size_t tab_count,
                            std::u16string audio_option,
                            base::OnceCallback<void(std::optional<size_t>, bool)> answer);
  void CancelPermissionPrompt();
  void FinishPermissionPrompt(bool accepted);
  GURL SitePermissionProbeUrl() const;
  void LoadSitePermissionProbe();
  void CheckSitePermissionProbe();
  void ClickSitePermissionProbe(base::TimeTicks deadline);
  void CheckStoppedMediaProbe(base::WeakPtr<content::WebContents> previous,
                              base::TimeTicks deadline);
  void RunClipboardPromptProbe();
  void RequestClipboardPromptProbe(base::TimeTicks deadline, bool activation_sent);
  void StartDownloadProbe();
  void StartDownloadProbeTransfers(bool folders_valid);
  void CheckDownloadCloseProbe(base::TimeTicks deadline);
  void CheckDownloadResumeProbe(base::TimeTicks deadline);
  void CheckDownloadDeleteProbe(base::TimeTicks deadline);
  void FinishDownloadDeleteProbe(bool deleted);
  void AdvanceDownloadProbe(std::string guid);
  void CheckDownloadHistoryProbe(size_t profile, bool expect_work_entry,
                                 base::TimeTicks deadline);
  bool OpenPasswords();
  void SearchAddress(std::u16string query);
  void StartWorkspace();
  void RebuildAdblockProfiles();
  base::DictValue ReadAdBlocking() const;
  std::string WriteAdBlocking(base::DictValue command);
  void OpenAdBlockingRules();
  void RefreshAdBlockingRules();
  bool CanConfigureAssistant() const;
  base::DictValue ReadAssistantSettings() const;
  void WriteAssistantSettings(base::DictValue command);
  bool WriteAssistantPreference(std::string_view key, bool value);
  void StoreAssistantSettings(prototype::AssistantSettings settings);
  void UpdateAssistantHostSettings();
  void ProbeAssistantProviders();
  void SaveAssistantApiKey(std::string key);
  void CheckAssistantProvider(size_t index, bool discover);
  void AssistantProviderChecked(size_t index, bool discover,
                                uint64_t generation, base::DictValue result);
  bool ShowAssistant(bool hover);
  const prototype::AssistantSettings& assistant_settings() const override;
  content::BrowserContext* AssistantBrowserContext() const override;
  std::string AssistantProfileId() const override;
  base::DictValue AssistantContext(const prototype::AssistantPermissions&) override;
  void AssistantTool(std::string, base::DictValue, prototype::AssistantPermissions,
                     base::OnceCallback<void(base::Value)>, bool, std::string) override;
  void RunAssistantWorkspaceAction(
      std::string_view tool, const base::DictValue& args,
      const prototype::AssistantPermissions& permissions,
      base::OnceCallback<void(base::Value)> done);
  bool CanReadAssistantHistory(std::string_view profile,
                               uint64_t generation) const;
  void SearchAssistantHistory(std::string_view query,
                              base::OnceCallback<void(base::Value)> done);
  void ReadAssistantDownloads(std::string profile, uint64_t generation,
                              base::TimeTicks deadline,
                              base::OnceCallback<void(base::Value)> done);
  void NavigateAssistantTab(const GURL& url,
                            base::OnceCallback<void(base::Value)> done);
  void WaitForAssistantNavigation(
      base::WeakPtr<content::WebContents> page, std::string workspace,
      uint64_t generation, int entry_id, base::TimeTicks deadline,
      base::OnceCallback<void(base::Value)> done);
  bool SelectAssistantWorkspace(size_t workspace);
  base::Value OpenAssistantPin(std::string_view target);
  static std::string AssistantTabUrl(const Tab& tab);
  std::optional<std::pair<size_t, size_t>> FindAssistantTab(
      std::string_view target) const;
  bool CanInspectAssistantTab(content::WebContents* page,
                             std::string_view workspace,
                             uint64_t generation) const;
  bool CanActOnAssistantPage(content::WebContents* page) const;
  void PrepareNightShiftRepair(content::RenderFrameHost& document);
  void RunNightShiftAssistantTool(
      std::string_view tool, const base::DictValue& args, bool page_access,
      bool background, base::OnceCallback<void(base::Value)> done);
  void RunAssistantDebugTool(content::WebContents* page, std::string_view tool,
                             const base::DictValue& args, bool confirmed,
                             base::OnceCallback<void(base::Value)> done);
  void RunAssistantAdblockRules(content::WebContents* page,
                                const base::DictValue& args, bool confirmed,
                                base::OnceCallback<void(base::Value)> done);
  void RunAssistantAdblockDiagnosis(content::WebContents& page,
                                    base::OnceCallback<void(base::Value)> done);
  void ReadAssistantAdblockDiagnosis(
      base::WeakPtr<content::WebContents> page, content::WeakDocumentPtr document,
      GURL url, uint64_t generation, std::string nonce,
      base::OnceCallback<void(base::Value)> done);
  void SaveAssistantAdblockRules(content::WebContents* page,
                                 const base::DictValue& args,
                                 std::vector<std::string> rules,
                                 std::string site, bool confirmed,
                                 base::OnceCallback<void(base::Value)> done);
  void ClearAssistantAdblockPreview(bool invalidate = true);
  void WaitForAssistantReload(
      base::WeakPtr<content::WebContents> page, std::string workspace,
      uint64_t generation, std::unique_ptr<AssistantReloadNavigation> reload,
      base::TimeTicks deadline, base::OnceCallback<void(base::Value)> done);
  void ClickAssistantTab(content::WebContents* page, const base::DictValue& args,
                         prototype::AssistantPermissions permissions, bool user_confirmed,
                         base::OnceCallback<void(base::Value)> done);
  void PressAssistantPageKey(content::WebContents* page, const base::DictValue& args,
                             prototype::AssistantPermissions permissions, bool user_confirmed,
                             base::OnceCallback<void(base::Value)> done);
  std::optional<std::string> TakeAssistantPageApproval(content::WebContents* page,
      std::string_view tool, std::string_view target, std::string_view token);
  void EditAssistantPage(content::WebContents* page, std::string_view tool,
                         const base::DictValue& args,
                         base::OnceCallback<void(base::Value)> done);
  enum class AssistantInspection { kPage, kScreenshot, kAdblock };
  void InspectAssistantTab(base::WeakPtr<content::WebContents> page,
                           std::string workspace, AssistantInspection inspection,
                           uint64_t generation,
                        base::TimeTicks deadline,
                        base::OnceCallback<void(base::Value)> done);
  void WithAssistantApiKey(base::OnceCallback<void(std::string)>) override;
  bool AssistantStartBackground(const std::string&, const std::string&, const GURL&) override;
  bool AssistantHasBackground(const std::string&) const override;
  bool AssistantTakeOver(const std::string&) override;
  void AssistantDiscardBackground(const std::string&) override;
  void AssistantElementBounds(
      std::string, std::string,
      base::OnceCallback<void(std::optional<gfx::Rect>)>) override;
  void LoadAssistantSessions(
      base::OnceCallback<void(std::optional<base::DictValue>)>) override;
  void SaveAssistantSessions(base::DictValue) override;
  void AssistantStateChanged() override;
  bool NavigateFromBar(const GURL&, bool new_tab) override;
  void CheckAssistantPanelProbe(bool sent, int decisions, base::TimeTicks deadline);
  void CheckAssistantWorkspaceActions();
  void CheckAssistantSettingsProbe(int step, bool reopening, base::TimeTicks deadline);
  BrowserPreferences ReadPreferences() const;
  bool WritePreference(std::string key, int value);
  bool CleanAddressCopies() const {
    return private_owner_ ? private_owner_->clean_address_copies_
                          : clean_address_copies_;
  }
  void UpdateFingerprintToolbar();
  struct PrivateSession;
  struct ExtensionDnrNavigationProbe;
  bool OpenPrivateWindow();
  void InitializePrivateSession(size_t source,
                                std::unique_ptr<PrivateSession> session);
  void OpenPendingPrivateWindows();
  void ClosePrivateWindow(WorkspaceLab* window);
  prototype::AdblockFilterLists* AdblockLists() const;
  void UpdateAdblockShield();
  void RefreshAdblockCount();
  void RefreshAdblockShieldRequests();
  bool CheckAdblockShieldRequestsForProbe(bool filtered);
  void ToggleAdblockShield();
  void CloseAdblockShield();
  void DestroyAdblockShield();
  void RequestClearData(BrowsingDataSelection selection);
  void OpenClearDataDialog(views::Widget* parent);
  void ClearData(size_t profile, BrowsingDataSelection selection);
  void ContinueClearData(bool history, bool passwords, base::Time begin, uint64_t mask,
                         bool timeline_saved);
  void ClearHistoryAndStorage(bool history, base::Time begin, uint64_t mask);
  void ClearStorageData(base::Time begin, uint64_t mask);
  void OnBrowsingDataRemoverDone(uint64_t failed_data_types) override;
  void OpenDevTools(base::OnceClosure ready);
  void RunPageCommand(prototype::PageCommand command);
  bool StartFind();
#if BUILDFLAG(ENABLE_PRINTING)
  bool StartPdfExport(const prototype::PrintOptions& options,
                      const std::string& ranges,
                      bool from_preview,
                      bool system = false);
  void CheckPdfExportProbe(base::TimeTicks deadline);
#if BUILDFLAG(ENABLE_PDF)
  bool ShowPdfPrintSheet();
  void RequestScriptedPrint(content::RenderFrameHost& frame);
  void CheckScriptedPrintProbe(base::WeakPtr<content::WebContents> fixture,
                              bool reloading, base::TimeTicks deadline);
  void CheckPdfPreviewProbe(bool range_selected, base::TimeTicks deadline);
#endif
#endif
  void Find(std::u16string text, bool forward, bool match_case);
  void StopFind();
  gfx::Rect FindBounds() const;
  void OnFindResultAvailable(content::WebContents* contents) override;
  void OnFindTabHelperDestroyed(find_in_page::FindTabHelper* helper) override;
  void RefreshShell();
  void ReadCachedFavicons(prototype::ShellState& state, const std::set<GURL>& urls);
  bool RefreshWorkspaceFavicons();
  void PublishWorkspaceFavicon(const GURL& page, const GURL& icon,
                               const gfx::Image& image);
  void SwitchTo(size_t index);
  void CheckPresentation(base::TimeTicks deadline);
  bool CheckSessionProbeSetup() const;
  void CheckSessionProbePage();
  void AdvanceSessionProbe();
  void CheckCommandCenterProbe(bool reopening, base::TimeTicks deadline);
  void CheckClosedTabProbe(size_t phase, base::TimeTicks deadline);
  void CheckFilterListsProbe(bool restart, size_t phase, base::TimeTicks deadline);
  void FinishAdblockHistoryProbe(base::TimeTicks deadline);
  void CheckPrivateAdblockProbe(int phase, base::TimeTicks deadline);
  void CheckPrivateWindowProbe(int phase, base::TimeTicks deadline);
  void CheckFindReloadProbe(int phase, base::TimeTicks deadline);
  void CheckRssProbe(bool reopening, bool subscribed, base::TimeTicks deadline);
  void RunBrowserImportProbe(bool repeating);
  void CheckInteractiveImportProbe(bool apply, bool repeat, base::TimeTicks deadline);
  void VerifyInteractiveImportedCookies();
  void FinishInteractiveImportProbe();
  void CheckExtraWorkspaceProbe(bool reopening, bool selected,
                                base::TimeTicks deadline);
  void CheckWorkspaceRenameProbe(int phase, base::WeakPtr<content::WebContents> page,
                                 base::TimeTicks deadline);
  void FinishExtraWorkspaceProbe();
  enum class ShiftPeekProbePhase {
    kSource, kHoverPlain, kPlain, kChild, kHoverBlank, kBlank
  };
  void CheckShiftPeekProbe(ShiftPeekProbePhase phase, bool restore,
                           base::TimeTicks deadline);
  enum class PeekProbePhase {
    kStart, kReloaded, kNext, kFullscreen, kRestored, kBack, kForward,
    kPromoted, kDirty, kDecline, kRetry, kAccept, kClosed, kSplitOpen,
    kSplitPromoted
  };
  void CheckPeekProbe(PeekProbePhase phase,
                      base::WeakPtr<content::WebContents> preview,
                      base::TimeTicks deadline);
  enum class PopupPolicyProbePhase {
    kAllowed, kEmbedded, kReset, kDefaultAllowed, kExplicitBlock, kOtherProfile
  };
  void CheckPopupPolicyProbe(PopupPolicyProbePhase phase, bool restore,
                              base::WeakPtr<content::WebContents> opener,
                              base::TimeTicks deadline);
  enum class PopupProbePhase {
    kBlocked, kBackground, kSameProfile, kClosed, kRule, kPostForm,
    kGetForm, kFormClosed, kRouted
  };
  void CheckPopupProbe(PopupProbePhase phase,
                       base::WeakPtr<content::WebContents> opener,
                       base::TimeTicks deadline);
  enum class LinkRoutingProbePhase { kSaveRule, kTyped, kRouted, kSplit, kRemoveRule };
  void CheckLinkRoutingProbe(LinkRoutingProbePhase phase,
                             base::WeakPtr<content::WebContents> source,
                             base::TimeTicks deadline);
  enum class SplitActionsProbePhase {
    kSource, kPaletteOpened, kPinOpened, kReordered, kThirdPane, kPaneRemoved,
    kDecline, kRetry, kAccept, kClosed, kReopened, kAcceptAll, kBlank,
    kOtherReady, kOtherDecline, kOtherRetry, kOtherAccept, kOtherClosed
  };
  void CheckSplitActionsProbe(SplitActionsProbePhase phase, size_t answers,
                              base::WeakPtr<content::WebContents> survivor,
                              base::TimeTicks deadline);
  enum class TabCommandProbePhase {
    kSource, kMuted, kUnmuted, kDuplicate, kSameProfileMove, kDeclineMove, kRetryMove,
    kAcceptMove, kCrossProfileMove, kDestination, kCopy
  };
  void CheckTabCommandsProbe(TabCommandProbePhase phase, std::string research,
                             base::WeakPtr<content::WebContents> duplicate,
                             base::TimeTicks deadline);
  void ExerciseSharedFavoritesProbe(std::string research);
  void CheckClosedFavoriteDuplicate(std::string research, base::TimeTicks deadline);
  void CheckWorkspaceReassignmentProbe(int phase, base::TimeTicks deadline);
  void CheckWorkspaceRemovalProbe(int phase, std::string extra,
                                  base::TimeTicks deadline);
  void CheckAdditionalProfileProbe(base::TimeTicks deadline);
  void CheckCreatedProfileProbe(base::TimeTicks deadline);
  void StartAdblockProfileProbe(bool restart);
  void CheckAdblockTrialProbe(bool cosmetic, int step,
                              base::TimeTicks deadline);
  void CheckAdblockProfileProbe(size_t workspace, int shield_step,
                                base::TimeTicks deadline);
  bool AcceptTimelineClearDialogForProbe();
  void CheckTimelineClearProbe(std::string mode, bool follow_up,
                               base::TimeTicks deadline);
  void CheckTimelinePanelProbe(size_t phase,
                               base::DictValue before,
                               base::TimeTicks deadline);
  void CheckTimelineProbe(GURL target, bool record_new_tab,
                          base::TimeTicks deadline);
  void CheckNavigationProbe(
      size_t step,
      std::array<base::WeakPtr<content::WebContents>, 2> pages,
      base::TimeTicks deadline);
  bool SubmitProbeCommand(std::u16string_view query,
                          std::string_view command,
                          std::string_view target = {});
  bool RunProbePaletteCommand(std::u16string_view query,
                              std::string_view command,
                              std::string_view target = {});
  void CheckSleepProbeDialog(bool confirm, bool answered, base::TimeTicks deadline);
  void FinishSleepProbe();
  bool ExercisePinEdits();
  bool ExerciseFolderEdits();
  ExtensionNavigationProbe* ExtensionProbeForProfile(const ProfileContext& profile) const;
  ManagedExtension* ManagedExtensionForProfile(const ProfileContext& profile,
                                              std::string_view id) const;
  void StartManagedExtensions(size_t owner);
  enum class ManagedProbePhase { kStart, kInitialEnable, kDisabled, kReenabled, kDone };
  bool PrepareManagedExtensionCacheForProbe(ProfileContext& owner,
                                            ProfileContext& peer);
  bool CheckManagedExtensionCacheEvictionForProbe();
  bool CheckManagedOffscreenForProbe(ManagedExtension& owner, ManagedExtension& peer);
  bool CheckManagedHttpAuthForProbe(ManagedExtension& owner, ManagedExtension& peer);
  bool CheckUserScriptExecutionForProbe(ManagedExtension& owner, ManagedExtension& peer);
  bool CheckExtensionRuleApiForProbe(ManagedExtension& owner);
  bool CheckExtensionRuleCacheForProbe(ManagedExtension& owner);
  bool CheckExtensionDnrNavigationForProbe(ProfileContext& owner,
                                          ProfileContext& peer);
  void CheckManagedExtensionsForProbe(ManagedProbePhase phase,
                                      base::TimeTicks deadline);
  void OpenExtensionPackage();
  void OpenExtensionOptions(std::string id);
  void OpenExtensionPopup(std::string id);
  void CloseExtensionPopup();
  void CheckActiveTabActionProbe(GURL target, GURL popup, base::TimeTicks deadline);
  void CheckExtensionPopup(GURL url, base::TimeTicks deadline);
  void CheckExtensionCommandGrant(GURL target, base::TimeTicks deadline);
  void CheckWebAccessibleExtensionResources(base::TimeTicks deadline);
  void ClickExtensionOptionsForProbe(std::string id, GURL url,
                                    base::TimeTicks deadline);
  void CheckExtensionOptionsPage(GURL url, base::TimeTicks deadline);
  void CheckRuntimeOptionsPage(GURL url, size_t tab_count, base::TimeTicks deadline);
  void ConfirmUninstallExtension(std::string id);
  void FileSelected(const ui::SelectedFileInfo& file, int index) override;
  void FileSelectionCanceled() override;
  void OnExtensionReview(size_t profile, std::optional<base::DictValue> review);
  void InstallReviewedExtension(size_t profile, WorkspaceExtensionInstall& install,
                                bool is_update);
  void OnInstalledExtension(size_t profile, std::string id, bool active);
  void CheckLiveExtensionInstall(const std::string& id,
                                 bool navigated,
                                 base::TimeTicks deadline);
  void CheckPublisherOverlayForProbe(const std::string& id, bool inserted,
                                     base::TimeTicks deadline);
  void CheckExtensionPermissionDecision(const std::string& id,
                                        int step,
                                        bool dialog_handled,
                                        base::TimeTicks deadline);
  void WakeInstalledExtensionForProbe(const std::string& id,
                                      base::UnguessableToken prior_worker);
  void CheckInstalledExtensionWake(const std::string& id,
                                   base::UnguessableToken prior_worker,
                                   base::TimeTicks deadline);
  void CheckTabMessageTarget(const std::string& id, size_t sender_tab,
                             base::TimeTicks deadline);
  void CheckTabMessageReply(const std::string& id, size_t target_tab, int tab_id,
                            base::TimeTicks deadline);
  void CheckRemovedTabEvent(const std::string& id,
                            base::UnguessableToken prior_worker,
                            base::TimeTicks deadline);
  void CheckExtensionTabDiscard(size_t source, int tab_id, bool sleeping,
                                base::TimeTicks deadline);
  bool ToggleExtensionFromSettingsForProbe(ProfileContext& owner, bool enabled,
                                           std::string_view id = {},
                                           bool user_scripts = false);
  void FinishExtensionProbe(bool success);
  void CheckHistoryPanelProbe(base::TimeTicks deadline);
  void CheckFaviconProbe(size_t profile, base::TimeTicks deadline, bool refresh_started = false);
  void OpenAddressHistoryProbe();
  void CheckAddressHistoryProbe(base::TimeTicks deadline);
  void ConfirmClearDataProbe();
  enum class PasswordManagerStep { kInitial, kAdded, kEdited, kConfirm, kDeleted, kPersonal };
  void CheckPasswordManager(PasswordManagerStep step, base::TimeTicks deadline);
  void StartPasswordProbe(std::string action);
  void CheckHttpAuthProbe(std::string action, int phase,
                          base::TimeTicks deadline);
  void AdvancePasswordProbe();
  void SubmitPasswordProbe();
  void TypePasswordProbeSuffix();
  void CheckPasswordPrompt(base::TimeTicks deadline);
  GURL PasswordProbePage() const;
  void FinishPasswordProbe(bool success);
  void EnablePasswordSavingProbe();
  bool CheckPasswordSavingToggle(std::optional<bool> expected);
  bool TogglePasswordSavingForProbe(bool enabled);
  void LoadFingerprintProbeStep();
  void CheckFingerprintProbePage();
  void CheckFingerprintPopup(base::WeakPtr<content::WebContents> opener,
                              std::string sample, base::TimeTicks deadline);
  void AdvanceFingerprintProbeSample(std::string sample);
  void Finish(bool success);

  const base::FilePath devtools_path_;
  const base::FilePath session_path_;
  const base::FilePath user_download_directory_;
  const std::string download_safe_browsing_key_;
  const scoped_refptr<base::SequencedTaskRunner> session_writer_;
  std::unique_ptr<prototype::TimelineStore> timeline_;
  bool timeline_enabled_ = false;
  bool timeline_settings_ready_ = false;
  int timeline_days_ = 90;
  bool timeline_dirty_ = false;
  bool replacing_timeline_ = false;
  bool clear_data_failed_ = false;
  std::unique_ptr<WorkspacePasswordClear> password_clear_;
  const GURL session_probe_url_;
  GURL private_adblock_probe_url_;
  const bool dark_;
  const bool metal_;
  const bool smoke_;
  GURL webauthn_probe_url_;
  std::unique_ptr<WorkspaceWebAuthnProbe> webauthn_probe_;
  std::unique_ptr<AdblockCacheProbe> adblock_cache_probe_;
  GURL tls_probe_url_;
  bool tls_probe_expect_trusted_ = false;
  std::optional<size_t> fingerprint_probe_step_;
  bool fingerprint_probe_reopen_ = false;
  bool fingerprint_popup_pending_ = false;
  GURL fingerprint_probe_url_;
  std::string password_probe_action_;
  base::OnceCallback<void(bool)> password_manager_auth_reply_;
  int password_manager_weak_count_ = 0;
  bool password_probe_submitted_ = false;
  bool password_probe_waiting_ = false;
  const raw_ptr<WorkspaceLab> private_owner_;
  std::string application_version_;
  const raw_ptr<os_crypt_async::OSCryptAsync> os_crypt_;
  std::vector<std::unique_ptr<ProfileContext>> runtime_profiles_;
  std::vector<raw_ptr<ProfileContext>> contexts_;
  std::vector<ContentProfileInfo> profiles_;
  std::map<std::string, std::string> imported_workspace_sources_;
  base::DictValue removed_workspaces_;
  base::DictValue timeline_epochs_;
  base::DictValue link_rules_;
  struct RemovedWorkspace {
    Workspace workspace;
    base::DictValue session;
    std::string import_source;
    bool was_active;
    base::DictValue link_rules;
  };
  std::optional<RemovedWorkspace> removed_workspace_;
  struct PendingBrowserImport;
  std::unique_ptr<PendingBrowserImport> browser_import_;
  std::optional<prototype::ArcImport> browser_import_preview_;
  size_t browser_import_generation_ = 0;
  std::unique_ptr<PrivateSession> private_session_;
  size_t pending_private_windows_ = 0;
  base::RepeatingCallback<bool(std::string)> native_menu_probe_;
  WorkspaceModel model_;
  WorkspaceController controller_{model_, *this};
  std::unique_ptr<views::LayoutProvider> layout_provider_;
  std::unique_ptr<views::ViewsDelegate> views_delegate_;
  std::unique_ptr<views::WidgetDelegate> delegate_;
  std::unique_ptr<views::Widget> widget_;
  std::unique_ptr<prototype::WorkspaceShell> shell_;
  std::unique_ptr<ExtensionShortcutTarget> extension_shortcut_target_;
  std::set<ui::Accelerator> extension_shortcuts_;
  bool extension_command_probe_dispatched_ = false;
  bool extension_menu_probe_dispatched_ = false;

  std::unique_ptr<prototype::FingerprintToolbar> fingerprint_toolbar_;
  raw_ptr<views::View> fingerprint_button_ = nullptr;
  base::CallbackListSubscription private_fingerprint_subscription_;
  raw_ptr<prototype::ShieldButton> adblock_shield_button_ = nullptr;
  raw_ptr<prototype::ShieldCard> adblock_shield_card_ = nullptr;
  raw_ptr<views::View> adblock_shield_requests_ = nullptr;
  base::WeakPtr<content::WebContents> adblock_counted_page_;
  base::CallbackListSubscription adblock_count_subscription_;
  std::unique_ptr<views::BubbleDialogDelegate> adblock_shield_panel_;
  std::unique_ptr<views::Widget> adblock_shield_widget_;
  base::RepeatingCallback<content::WebContents*()> adblock_shield_page_;
  std::unique_ptr<WorkspaceDevTools> devtools_;
#if BUILDFLAG(ENABLE_PRINTING)
  std::unique_ptr<WorkspacePdfExport> pdf_export_;
  enum class SystemPrintProbe { kNone, kCancel, kFile };
  SystemPrintProbe system_print_probe_ = SystemPrintProbe::kNone;
#if BUILDFLAG(ENABLE_PDF)
  std::unique_ptr<WorkspacePdfPreview> pdf_preview_;
  base::TimeTicks last_scripted_print_;
#endif
  std::unique_ptr<ui::ScopedSetSelectFileDialogFactory> pdf_probe_picker_;
#endif
  base::WeakPtr<content::WebContents> find_page_;
  base::WeakPtr<content::WebContents> fullscreen_page_;
  bool window_was_fullscreen_ = false;
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      fullscreen_observation_{this};
  base::ScopedObservation<find_in_page::FindTabHelper,
                          find_in_page::FindResultObserver>
      find_observation_{this};
  std::array<std::unique_ptr<ExtensionNavigationProbe>, 2> extension_probes_;
  std::vector<std::unique_ptr<ManagedExtension>> managed_extensions_;
  scoped_refptr<ui::SelectFileDialog> extension_picker_;
  std::unique_ptr<WorkspaceExtensionInstall> extension_install_;
  size_t extension_install_profile_ = 0;
  std::optional<size_t> extension_tab_close_answers_;
  size_t extension_tab_repost_answers_ = 0;
  std::optional<base::UnguessableToken> managed_owner_worker_;
  std::optional<base::UnguessableToken> managed_peer_worker_;
  std::unique_ptr<content::WebContents> managed_uninstall_probe_page_;
  std::array<std::unique_ptr<content::WebContents>, 2> offscreen_probe_pages_;
  std::array<bool, 2> offscreen_probe_complete_{};
  std::unique_ptr<ProfileContext> extension_http_auth_private_;
  std::array<std::unique_ptr<content::WebContents>, 4> extension_http_auth_pages_;
  base::WeakPtr<content::WebContents> publisher_overlay_probe_page_;
  bool extension_http_auth_verified_ = false;
  std::unique_ptr<ProfileContext> user_script_probe_private_;
  std::array<std::unique_ptr<content::WebContents>, 3> user_script_probe_pages_;
  enum class UserScriptProbePhase { kApproved, kRevoked, kDone };
  UserScriptProbePhase user_script_probe_phase_ = UserScriptProbePhase::kApproved;
  std::unique_ptr<content::WebContents> extension_rule_api_probe_page_;
  bool extension_rule_api_probe_complete_ = false;
  std::unique_ptr<content::WebContents> extension_rule_cache_probe_page_;
  size_t extension_rule_cache_probe_step_ = 0;
  std::unique_ptr<ExtensionDnrNavigationProbe> extension_dnr_navigation_probe_;
  bool extension_dnr_navigation_probe_complete_ = false;
  std::unique_ptr<views::View> managed_cache_probe_host_;
  std::array<raw_ptr<views::WebView>, 2> managed_cache_views_{};
  std::array<content::WeakDocumentPtr, 2> managed_cache_documents_;
  bool managed_cache_eviction_pending_ = false;
  size_t extension_probes_pending_ = 0;
  std::unique_ptr<prototype::AdblockFilterLists> adblock_filter_lists_;
  bool waiting_for_adblock_cache_ = false;
  bool close_after_workspace_start_ = false;
  std::vector<GURL> startup_external_urls_;
  std::unique_ptr<prototype::AdBlockingRulesPanel> ad_blocking_rules_panel_;
  std::unique_ptr<prototype::HistoryPanel> history_panel_;
  std::unique_ptr<prototype::DownloadPanel> downloads_;
  std::unique_ptr<prototype::PasswordsPanel> passwords_panel_;
  std::unique_ptr<WorkspaceMediaAccess> media_access_;
  std::unique_ptr<views::DialogDelegate> extension_popup_delegate_;
  std::unique_ptr<views::Widget> extension_popup_widget_;
  raw_ptr<views::WebView> extension_popup_view_ = nullptr;
  bool extension_side_panel_probe_open_requested_ = false;
  bool extension_side_panel_probe_close_requested_ = false;
  std::unique_ptr<views::DialogDelegate> permission_prompt_delegate_;
  std::unique_ptr<views::Widget> permission_prompt_widget_;
  raw_ptr<views::Combobox> permission_prompt_choices_ = nullptr;
  raw_ptr<views::Checkbox> permission_prompt_audio_ = nullptr;
  base::OnceCallback<void(std::optional<size_t>, bool)> permission_prompt_answer_;
  bool permission_prompt_cancelled_ = false;
  struct CachedFavicon {
    base::CancelableTaskTracker::TaskId task = base::CancelableTaskTracker::kBadTaskId;
    std::vector<uint32_t> pixels;
  };
  std::string favicon_profile_;
  std::pair<std::string, std::string> favicon_refresh_scope_;
  std::unique_ptr<WorkspaceFaviconRefresh> favicon_refresh_;
  std::map<GURL, CachedFavicon> cached_favicons_;
  base::CancelableTaskTracker favicon_tasks_;
  base::CancelableTaskTracker address_history_tasks_;
  base::CancelableTaskTracker assistant_history_tasks_;
  base::CancelableTaskTracker history_removal_tasks_;
  std::optional<size_t> clearing_profile_;
  base::ScopedObservation<content::BrowsingDataRemover,
                          content::BrowsingDataRemover::Observer>
      data_remover_{this};
  bool clear_data_probe_pending_ = false;
  enum class DownloadCloseProbe { kNone, kWait, kQuit };
  DownloadCloseProbe download_close_probe_ = DownloadCloseProbe::kNone;
  base::OnceClosure quit_from_app_for_testing_;
  bool download_probe_active_ = false;
  std::string site_permission_probe_phase_;
  size_t site_permission_probe_profile_ = 0;
  bool site_permission_probe_changed_ = false;
  bool site_permission_prompt_shown_ = false;
  bool site_permission_media_started_ = false;
  enum class DownloadResumeProbe {
    kNone, kRetry, kSave, kInspect, kRestore, kReject, kAbandon
  };
  DownloadResumeProbe download_resume_probe_ = DownloadResumeProbe::kNone;
  enum class DownloadDeleteProbe { kNone, kCancel, kConfirm, kReject };
  DownloadDeleteProbe download_delete_probe_ = DownloadDeleteProbe::kNone;
  size_t pending_download_deletions_ = 0;
  bool close_after_download_deletions_ = false;
  std::string download_probe_work_guid_;
  struct Tab {
    // Release the view reference before destroying its detached owner.
    std::unique_ptr<views::WebView> detached_view;
    raw_ptr<views::WebView> view;
    uint64_t id;
    GURL deferred_url;
    std::u16string deferred_title;
    std::string pin;
    base::CallbackListSubscription focus_subscription;
    std::optional<uint64_t> opener_id = std::nullopt;
    bool routing = false;
    GURL shift_peek_url = {};
    WorkspaceExtensionTabOwner extension_tab = {};
    std::unique_ptr<content::WebContents> discarded_page = {};
    bool native_muted = false;
  };
  bool shift_click_peek_ = true;
  bool clean_address_copies_ = true;
  std::optional<prototype::AssistantSettings> assistant_settings_;
  prototype::NightShiftConfig night_shift_config_;
  prototype::AssistantSettings assistant_host_settings_;
  std::array<base::DictValue, 4> assistant_provider_status_;
  std::array<bool, 4> assistant_provider_checking_{};
  std::array<bool, 4> assistant_provider_available_{};
  struct AssistantPageApproval {
    content::WeakDocumentPtr document;
    GURL url;
    std::string tool;
    std::string target;
    std::string binding;
    std::string token;
    uint64_t settings_generation;
    base::ListValue adblock_selectors;
    bool adblock_preview_cleared = false;
  };
  std::optional<AssistantPageApproval> assistant_page_approval_;
  struct NightShiftRequest {
    content::WeakDocumentPtr document;
    GURL url;
    std::string id;
    base::ListValue candidates;
  };
  std::optional<NightShiftRequest> night_shift_request_;
  uint64_t assistant_provider_generation_ = 0;
  uint64_t assistant_key_write_generation_ = 0;
  raw_ptr<prototype::AssistantPanel> assistant_ = nullptr;
  std::string assistant_profile_;
  bool assistant_probe_saw_live_answer_ = false;
  std::optional<Tab> peek_tab_;
  std::string peek_workspace_id_;
  std::vector<std::vector<Tab>> tabs_;
  std::vector<std::vector<ClosedTab>> closed_tabs_;
  std::vector<std::vector<prototype::PinnedItem>> pins_;
  ProfileFavorites profile_favorites_;
  std::vector<WorkspaceSplit> splits_;
  std::vector<size_t> selected_;
  std::vector<bool> sleeping_;
  uint64_t next_tab_id_ = 1;
  size_t restored_workspace_ = 0;
  bool download_close_prompt_pending_ = false;
  bool saving_session_ = false;
  bool saving_catalogue_ = false;
  bool close_after_catalogue_save_ = false;
  bool session_probe_advancing_ = false;
  bool pin_probe_opening_ = false;
  bool pin_crash_save_pending_ = false;
  size_t active_index_ = 0;
  bool updating_panes_ = false;
  bool tab_audio_refresh_pending_ = false;
  prototype::BeforeUnloadBatch before_unload_batch_;
  base::OnceCallback<void(bool)> pending_extension_tab_removal_;
  base::ProcessId work_renderer_ = base::kNullProcessId;
  base::OnceCallback<void(bool)> done_;
  base::WeakPtrFactory<WorkspaceLab> weak_factory_{this};
};

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_WORKSPACE_LAB_H_
