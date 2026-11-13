// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CONTENT_PROFILE_CONTEXT_H_
#define ZITTER_CONTENT_PROFILE_CONTEXT_H_

#include <cstdint>
#include <memory>
#include <vector>
#include <optional>
#include <string>
#include <string_view>

#include "base/callback_list.h"
#include "base/files/file_path.h"
#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "build/build_config.h"
#include "content/public/browser/browser_context.h"
#include "url/origin.h"

class GURL;
class PersistentPrefStore;
class PrefService;
class PrefChangeRegistrar;

namespace content { class WebContents; }
namespace history { class HistoryService; }
namespace favicon { class FaviconServiceImpl; }
namespace os_crypt_async { class OSCryptAsync; }
namespace password_manager {
class PasswordStore;
class PasswordStoreInterface;
}
namespace network { class URLLoaderFactoryBuilder; }
namespace content { class RenderFrameHost; }
namespace zitter::prototype { class RssReader; }

namespace zitter::content_embedder {

class ProfileDownloadDelegate;
class ProfilePermissionController;
class ProfileContext final : public content::BrowserContext {
 public:
  enum class Mode { kPersistent, kPrivate };

  ProfileContext(base::FilePath path,
                 Mode mode,
                 std::optional<url::Origin> storage_probe_origin = std::nullopt,
                 os_crypt_async::OSCryptAsync* os_crypt = nullptr,
                 std::optional<bool> password_saving_default = true,
                 base::FilePath separate_network_path = {});
  ~ProfileContext() override;

  static void AttachPage(content::WebContents& contents);
  static bool IsPasswordStorePathSafe(const base::FilePath& profile_path);
  static bool IsHistoryStorePathSafe(const base::FilePath& profile_path);
  static std::optional<bool> ReadPasswordSavingDefault(
      const base::FilePath& profile_path);
  static std::unique_ptr<PrefService> OpenFingerprintPreferences(
      const base::FilePath& profile_path);
  static std::unique_ptr<PrefService> OpenSitePermissionPreferences(
      const base::FilePath& profile_path);
  static std::unique_ptr<PrefService> OpenFileChooserPreferences(
      const base::FilePath& profile_path);
  static std::unique_ptr<PrefService> OpenPageZoomPreferences(
      const base::FilePath& profile_path);
  void SetPageZoomPreferences(std::unique_ptr<PrefService> prefs);
  double default_page_zoom_level() const { return default_page_zoom_level_; }
  bool CanSetDefaultPageZoomLevel() const;
  bool SetDefaultPageZoomLevel(double level);
  void InheritPrivatePageZoom(ProfileContext& parent);
  void SetFileChooserPreferences(std::unique_ptr<PrefService> prefs);
  const base::FilePath& file_chooser_directory() const {
    return file_chooser_directory_;
  }
  void SetFileChooserDirectory(const base::FilePath& directory);
  ProfilePermissionController& site_permissions();
#if BUILDFLAG(IS_WIN)
  using WebNotificationPresenter = base::RepeatingCallback<void(
      const std::string&, const GURL&, std::u16string, std::u16string,
      base::RepeatingClosure)>;
  void SetWebNotificationPresenter(
      WebNotificationPresenter show,
      base::RepeatingCallback<void(const std::string&)> close);
#endif
  void SetFingerprintPreferences(std::unique_ptr<PrefService> prefs);
  std::optional<bool> GetFingerprintProtection();
  bool SetFingerprintProtection(bool enabled);
  void InheritPrivateFingerprintProtection(ProfileContext& parent);
  base::CallbackListSubscription ObserveFingerprintProtection(
      base::RepeatingClosure changed);

  struct AdblockPreferences {
    explicit AdblockPreferences(scoped_refptr<PersistentPrefStore> store,
                                bool async = false);
    ~AdblockPreferences();

    const scoped_refptr<PersistentPrefStore> store;
    std::unique_ptr<PrefService> service;
  };
  static std::unique_ptr<AdblockPreferences> OpenAdblockPreferences(
      const base::FilePath& profile_path);
  void SetAdblockPreferences(std::unique_ptr<AdblockPreferences> prefs);
  void InitializePrivateAdblock(ProfileContext* parent = nullptr);
  std::optional<bool> GetAdblockEnabled() const;
  bool IsAdblockEnabledFor(const GURL& page) const;
  bool SetAdblockEnabled(bool enabled);
  bool GetAdblockListEnabled(std::string_view id) const;
  bool SetAdblockListEnabled(std::string_view id, bool enabled);
  void SetAdblockFilterTexts(std::shared_ptr<const std::vector<std::string>> texts,
                             base::OnceClosure ready);
  bool IsAdblockBuilding() const;
  base::CallbackListSubscription ObserveAdblockSettings(base::RepeatingClosure changed);
  std::vector<std::string> AdblockRules() const;
  bool SetAdblockRules(std::vector<std::string> rules);
  std::vector<std::string> AdblockAllowedSites() const;
  std::optional<bool> GetEditableAdblockSiteException(const GURL& page) const;
  bool SetAdblockSiteAllowed(std::string_view site, bool allowed);
  bool ConfigureAdblockProbe(const GURL& url);
  static void TryAdblockRule(content::WebContents& contents, std::string rule,
                             base::OnceCallback<void(bool)> done);
  static void TryAdblockRules(content::WebContents& contents,
                              std::vector<std::string> rules,
                              base::OnceCallback<void(std::optional<size_t>)> done);
  static bool HasAdblockTrial(content::WebContents& contents,
                             std::optional<size_t> id = std::nullopt);
  static void ClearAdblockTrial(content::WebContents& contents,
                               std::optional<size_t> id = std::nullopt);
  static int AdblockCount(content::WebContents& contents);
  static std::vector<std::string> RecentAdblockRequests(
      content::WebContents& contents);
  static base::CallbackListSubscription ObserveAdblockCount(
      content::WebContents& contents, base::RepeatingClosure changed);
  void MaybeFilterAdblockRequests(const GURL& page,
                                  content::RenderFrameHost* frame,
                                  int64_t page_source_id,
                                  network::URLLoaderFactoryBuilder& builder);

  void ConfigureDownloadProbe(GURL url, base::FilePath directory,
                              bool prompt_for_probe = false,
                              bool allow_http_fixture = true);
  void EnableUserDownloads(const base::FilePath& suggested_directory,
                           std::string safe_browsing_key = {});
  void SetDownloadProbeCertificate(std::vector<uint8_t> der);
  const std::vector<uint8_t>& download_probe_certificate() const {
    return download_probe_certificate_;
  }
  int BlockingDownloadCount() const;
  history::HistoryService* GetHistory();
  favicon::FaviconServiceImpl* GetFaviconService();
  void CloseImportedHistory(base::OnceClosure done);
  prototype::RssReader& GetRssReader();
  password_manager::PasswordStoreInterface* GetPasswordStore();
  PrefService* GetPasswordPrefs() const { return password_prefs_.get(); }
  std::optional<bool> GetPasswordSavingSetting() const;
  bool SetPasswordSavingSetting(bool enabled);

  base::FilePath GetPath() const override;
  base::FilePath GetNetworkPath() const;
  void InitializeNewWorkspacePreferences();
  bool IsOffTheRecord() override;
  std::unique_ptr<content::ZoomLevelDelegate>
      CreateZoomLevelDelegate(const base::FilePath&) override;
  content::DownloadManagerDelegate* GetDownloadManagerDelegate() override;
  content::BrowserPluginGuestManager* GetGuestManager() override;
  storage::SpecialStoragePolicy* GetSpecialStoragePolicy() override;
  content::PlatformNotificationService* GetPlatformNotificationService()
      override;
  content::PushMessagingService* GetPushMessagingService() override;
  content::StorageNotificationService* GetStorageNotificationService() override;
  content::SSLHostStateDelegate* GetSSLHostStateDelegate() override;
  content::PermissionControllerDelegate* GetPermissionControllerDelegate()
      override;
  content::ReduceAcceptLanguageControllerDelegate*
  GetReduceAcceptLanguageControllerDelegate() override;
  content::ClientHintsControllerDelegate* GetClientHintsControllerDelegate()
      override;
  content::BackgroundFetchDelegate* GetBackgroundFetchDelegate() override;
  content::BackgroundSyncController* GetBackgroundSyncController() override;
  content::BrowsingDataRemoverDelegate* GetBrowsingDataRemoverDelegate()
      override;

 private:
  class FingerprintPage;
  class HistoryPage;
  class AdblockPage;
  class AdblockState;

  void ApplyDefaultPageZoomLevel(double level);

  const base::FilePath path_;
  const base::FilePath separate_network_path_;
  const Mode mode_;
  const raw_ptr<os_crypt_async::OSCryptAsync> os_crypt_;
  void UpdateFingerprintProtection(bool enabled);

  bool fingerprint_protection_ = false;
  uint64_t fingerprint_transport_sequence_ = 0;
  bool fingerprint_overridden_ = false;
  base::CallbackListSubscription inherited_fingerprint_;
  std::unique_ptr<PrefService> fingerprint_prefs_;
  base::RepeatingClosureList fingerprint_changed_;
  raw_ptr<content::DownloadManager> download_manager_ = nullptr;
  std::unique_ptr<AdblockState> adblock_;
  std::unique_ptr<ProfileDownloadDelegate> downloads_;
  std::string download_safe_browsing_key_;
  std::vector<uint8_t> download_probe_certificate_;
  std::unique_ptr<ProfilePermissionController> permissions_;
  std::unique_ptr<content::PlatformNotificationService> notifications_;
  std::unique_ptr<PrefService> file_chooser_prefs_;
  std::unique_ptr<PrefService> page_zoom_prefs_;
  double default_page_zoom_level_ = 0.0;
  base::FilePath file_chooser_directory_;
  std::unique_ptr<history::HistoryService> history_;
  std::unique_ptr<favicon::FaviconServiceImpl> favicon_service_;
  std::unique_ptr<prototype::RssReader> rss_reader_;
  bool persistent_password_prefs_ = false;
  std::unique_ptr<PrefService> password_prefs_;
  std::unique_ptr<PrefChangeRegistrar> password_privacy_registrar_;
  scoped_refptr<password_manager::PasswordStore> password_store_;
};

// Validated on the startup sequence before making the network path available.
class WorkspaceProfileStartup {
 public:
  static std::unique_ptr<WorkspaceProfileStartup> Read(
      base::FilePath path, base::FilePath separate_network_path = {});
  ~WorkspaceProfileStartup();

  std::unique_ptr<ProfileContext> Open(os_crypt_async::OSCryptAsync& encryption);

 private:
  explicit WorkspaceProfileStartup(base::FilePath path);

  base::FilePath path_;
  base::FilePath separate_network_path_;
  std::optional<bool> password_saving_default_;
  std::unique_ptr<PrefService> site_permissions_;
  std::unique_ptr<PrefService> file_chooser_;
  std::unique_ptr<PrefService> page_zoom_;
  std::unique_ptr<PrefService> fingerprint_;
  std::unique_ptr<ProfileContext::AdblockPreferences> adblock_;
};

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_PROFILE_CONTEXT_H_
