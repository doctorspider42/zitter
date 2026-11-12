// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CONTENT_EXTENSION_REGISTRY_PROBE_H_
#define ZITTER_CONTENT_EXTENSION_REGISTRY_PROBE_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ref.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/common/child_process_id.h"
#include "extension_manifest.h"
#include "third_party/blink/public/common/tokens/tokens.h"
#include "url/gurl.h"

class PrefStore;

namespace base {
class FilePath;
class Value;
}
namespace blink {
class StorageKey;
}
namespace content {
struct ContextMenuParams;
class BrowserContext;
class NavigationThrottleRegistry;
class RenderProcessHost;
class RenderFrameHost;
class SiteInstance;
class ServiceWorkerContext;
enum class ServiceWorkerCapability;
class WebContents;
}
namespace extensions {
class WebRequestAPI;
class Extension;
}
namespace url {
class Origin;
}

namespace zitter::prototype {
struct NightShiftConfig;
struct PageMenuItem;
}

namespace zitter::content_embedder {

class ProfileContext;
class WorkspaceLab;

struct ExtensionProbeFixture {
  scoped_refptr<const extensions::Extension> extension;
  bool keep_disabled;
  bool legacy_disabled;
};

inline constexpr char kNightShiftId[] = "kdpnmnbnedpcomicojommdikkdepclio";
const extensions::Extension* GetBundledNightShiftExtension(
    content::BrowserContext& context);
GURL BundledNightShiftSettingsPage(content::BrowserContext& context);
// Configure from the app's bundled resources before opening any profiles.
bool ConfigureBundledNightShift(const base::FilePath& directory);
void ApplyBundledNightShift(content::BrowserContext& context,
                            bool enabled,
                            const prototype::NightShiftConfig& config);
void PrepareExtensionRegistryProbe(
    std::multimap<base::FilePath, ExtensionProbeFixture> fixtures);
std::vector<scoped_refptr<const extensions::Extension>> GetConfiguredExtensions(
    content::BrowserContext& context);
bool RegisterInstalledExtension(
    content::BrowserContext& context,
    scoped_refptr<const extensions::Extension> extension);
extensions::WebRequestAPI* WorkspaceExtensionRequestProxy(
    content::BrowserContext& context);
bool WorkspaceExtensionHasRulesForTesting(content::BrowserContext& context,
                                           const std::string& id);
bool WorkspaceExtensionHasOffscreenForTesting(content::BrowserContext& context,
                                               const std::string& id);
void AttachExtensionWebContents(content::WebContents& contents);
void SetNightShiftSettingsDestination(content::WebContents& contents, const GURL& url);
void SetWorkspaceSidePanelDestination(content::WebContents& contents, const GURL& url);
bool ToggleWorkspaceSidePanelOnAction(content::WebContents& contents, const std::string& id);
void DispatchWorkspaceSidePanelEvent(content::BrowserContext& profile,
                                     const std::string& id, int tab_id,
                                     const GURL& url, bool opened);

class WorkspaceExtensionTab;
struct WorkspaceExtensionTabDeleter {
  void operator()(WorkspaceExtensionTab* tab) const;
};
using WorkspaceExtensionTabOwner =
    std::unique_ptr<WorkspaceExtensionTab, WorkspaceExtensionTabDeleter>;
WorkspaceExtensionTabOwner CreateWorkspaceExtensionTab(
    content::BrowserContext& context, WorkspaceLab& workspace);
void SetWorkspaceExtensionTabPage(WorkspaceExtensionTab* tab,
                                  content::WebContents* page);
void SetWorkspaceExtensionTabPageUrl(WorkspaceExtensionTab* tab,
                                     const GURL& url);
void MarkWorkspaceExtensionTabWindowClosing(WorkspaceExtensionTab* tab);
std::vector<prototype::PageMenuItem> WorkspaceExtensionPageMenu(
    content::RenderFrameHost& frame, const content::ContextMenuParams& params);

void DispatchWorkspaceExtensionCommand(content::BrowserContext& profile,
                                       const extensions::Extension& extension,
                                       const std::string& command,
                                       content::WebContents* active_page);

int WorkspaceExtensionTabIndex(const WorkspaceExtensionTab* tab);
int WorkspaceExtensionTabId(const WorkspaceExtensionTab* tab);
void NotifyWorkspaceExtensionTabMoved(WorkspaceExtensionTab* tab, int from_index);
void SetWorkspaceExtensionTabMuted(WorkspaceExtensionTab& tab, bool muted,
                                   std::string_view extension_id);
bool WorkspaceExtensionTabMuted(const WorkspaceExtensionTab& tab);
void UpdateWorkspaceExtensionTab(WorkspaceExtensionTab* tab, int index,
                                 bool active, bool highlighted, bool pinned,
                                 const GURL& deferred_url,
                                 std::u16string_view deferred_title);
int WorkspaceExtensionTabIdForTesting(content::WebContents& contents);
void GrantWorkspaceExtensionActiveTab(content::WebContents& page,
                                      const std::string& id);
bool VerifyExtensionRulePreferencesForTesting(content::BrowserContext& context,
                                               const std::string& id,
                                               bool restore);
scoped_refptr<PrefStore> GetWorkspaceExtensionPreferenceStore(
    content::BrowserContext& context);
void DispatchExtensionPasswordSavingChange(content::BrowserContext& context);
void RegisterExtensionContext(content::BrowserContext& context);
void UnregisterExtensionContext(content::BrowserContext& context);
void AddExtensionProbeNavigationThrottle(
    content::NavigationThrottleRegistry& registry);
bool CanLoadExtensionProbeResources(content::BrowserContext& context,
                                    const std::string& id = {});
void ConfigureWorkspaceExtensionIdleProbeForTesting(content::BrowserContext& context);

enum class ExtensionClipboardAccess { kRead, kWrite };
bool HasWorkspaceExtensionClipboardPermission(content::RenderFrameHost* frame,
                                              ExtensionClipboardAccess access);
bool IsWorkspaceOffscreenFrame(content::RenderFrameHost* frame);
bool HasWorkspaceOffscreenClipboardPermission(content::RenderFrameHost* frame,
                                              ExtensionClipboardAccess access);

bool CanRegisterExtensionWorkerFactory(content::BrowserContext& context);
bool IsExtensionProbeOrigin(content::BrowserContext& context, const GURL& site);
bool CanUseExtensionProbeWorker(content::BrowserContext& context,
                               const GURL& scope,
                               const blink::StorageKey& storage_key,
                               const GURL& script_url);
void RegisterExtensionProbeProcess(content::SiteInstance& site);
bool CanLoadExtensionProbeSubresources(content::RenderFrameHost& frame,
                                       const url::Origin& initiator);
bool CanLoadSandboxedExtensionSubresource(content::RenderFrameHost& frame,
                                          const GURL& resource);
bool CanLoadWebAccessibleExtensionResource(content::BrowserContext& context,
                                           const GURL& url,
                                           const url::Origin& initiator);
bool CanLoadExtensionWorkerResources(content::RenderProcessHost& process,
                                     const url::Origin& initiator);

class ManagedExtension {
 public:
  ManagedExtension(ProfileContext& owner,
                   std::string extension_id,
                   base::RepeatingClosure changed);
  ~ManagedExtension();

  bool Start(base::OnceCallback<void(bool)> installed = {});
  bool OwnsProfile(const ProfileContext& profile) const;
  ProfileContext& profile() const { return *owner_; }
  const std::string& id() const { return extension_id_; }
  const std::string& name() const;
  GURL options_page() const;
  GURL uninstall_url() const;
  GURL action_popup() const;
  bool enabled() const;
  bool selection() const;
  bool editable() const;
  bool uninstalling() const { return !uninstall_.is_null(); }
  bool failed() const { return failed_; }
  bool SetEnabled(bool enabled);
  std::optional<bool> UserScriptsAllowed() const;
  bool SetUserScriptsAllowed(bool allowed);
  void Uninstall(base::OnceCallback<void(bool)> done);
  std::optional<base::UnguessableToken> RunningWorkerForTesting();
  void StopWorkerForTesting(base::OnceClosure stopped);

 private:
  enum class Activation { kStartup, kInstall, kEnable };
  void Enable(Activation activation);
  void PrepareWorker(Activation activation);
  void OnWorkerPrepared(Activation activation, bool success);
  void WaitForWorkerRemoval(base::TimeTicks deadline);
  void OnWorkerRemovalChecked(base::TimeTicks deadline,
                              content::ServiceWorkerCapability capability);
  void CompleteTransition(bool success);
  void ClearUninstalledData();
  void WaitForUninstalledRenderers(base::TimeTicks deadline);
  void RemoveUninstalledPackage();
  void CompleteUninstall(bool success);

  const raw_ref<ProfileContext> owner_;
  const std::string extension_id_;
  const base::RepeatingClosure changed_;
  scoped_refptr<const extensions::Extension> extension_;
  bool ready_ = false;
  bool failed_ = false;
  bool legacy_disabled_ = false;
  std::optional<bool> pending_choice_;
  base::OnceCallback<void(bool)> installed_;
  base::OnceCallback<void(bool)> uninstall_;
  base::WeakPtrFactory<ManagedExtension> weak_factory_{this};
};

class ExtensionNavigationProbe final : private content::WebContentsObserver {
 public:
  ExtensionNavigationProbe(ProfileContext& owner, ProfileContext& other,
                           const base::FilePath& private_path,
                           content::WebContents& web_page,
                           base::OnceClosure peer_barrier,
                           base::RepeatingCallback<bool(ProfileContext&, bool)>
                               change_settings,
                           base::OnceCallback<void(bool)> done);
  ~ExtensionNavigationProbe() override;

  void Start();
  void ContinueAfterWorkerWakeUp();
  void ContinueAfterPeerDisabled();
  bool StartupWorkerIsRunning();
  bool OwnsProfile(const ProfileContext& profile) const;
  std::optional<bool> UserEnabled() const;
  bool CanSetUserEnabled() const { return user_choice_ready_; }
  bool SetUserEnabled(bool enabled);

 private:
  enum class Step {
    kOwner, kOther, kPrivate, kRemoved, kInitiallyDisabled, kPeerDisabled
  };
  enum class ScriptCheck {
    kNone, kOwner, kExtraAllowed, kExtraExcluded, kUnmatched, kOther, kPrivate, kRemoved,
    kInitiallyDisabled
  };
  void Navigate(content::BrowserContext& context);
  void DidFinishNavigation(content::NavigationHandle* navigation) override;
  void DocumentOnLoadCompletedInPrimaryMainFrame() override;
  void StartForeignScriptCheck();
  void CheckForeignScript(base::TimeTicks deadline);
  void OnForeignScriptResult(base::TimeTicks deadline, base::Value result);
  content::ServiceWorkerContext& WorkerContext();
  void CheckWorkerRegistration();
  void QueueWorkerWakeUp(base::TimeTicks deadline);
  void CheckWorkerWakeUp(base::TimeTicks deadline);
  void OnWorkerWakeUp(base::TimeTicks deadline, base::Value result);
  void WaitForContentScripts(base::TimeTicks deadline);
  void CheckContentScript(ScriptCheck check, content::BrowserContext& context);
  void ReadContentScriptResult(base::TimeTicks deadline);
  void OnContentScriptResult(base::TimeTicks deadline, base::Value result);
  void EnableExtension();
  void PrepareWorkerNavigation(content::ServiceWorkerCapability existing);
  void ActivateAndNavigate();
  void DisableExtension();
  void CheckWorkerRemoval(base::TimeTicks deadline);
  void CheckWorkerReply(base::TimeTicks deadline);
  void OnWorkerReply(base::TimeTicks deadline, base::Value result);
  void OnWorkerRemovalChecked(base::TimeTicks deadline,
                              content::ServiceWorkerCapability capability);
  void Advance();
  void Finish(bool success);

  const raw_ref<ProfileContext> owner_;
  const raw_ref<ProfileContext> other_;
  std::unique_ptr<ProfileContext> private_;
  const base::WeakPtr<content::WebContents> web_page_;
  const content::ChildProcessId web_renderer_;
  const bool paired_;
  const ExtensionFixtureStateAction state_action_;
  base::OnceClosure peer_barrier_;
  base::RepeatingCallback<bool(ProfileContext&, bool)> change_settings_;
  bool user_choice_ready_ = false;
  std::optional<bool> committed_user_choice_;
  scoped_refptr<const extensions::Extension> extension_;
  base::OnceCallback<void(bool)> done_;
  std::unique_ptr<content::WebContents> page_;
  std::unique_ptr<content::WebContents> owner_script_page_;
  GURL url_;
  GURL script_check_url_;
  content::ChildProcessId owner_script_renderer_;
  std::optional<blink::ServiceWorkerToken> startup_worker_;
  ScriptCheck script_check_ = ScriptCheck::kNone;
  Step step_ = Step::kOwner;
  base::WeakPtrFactory<ExtensionNavigationProbe> weak_factory_{this};
};

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_EXTENSION_REGISTRY_PROBE_H_
