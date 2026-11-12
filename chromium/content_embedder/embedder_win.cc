// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include <windows.h>

#include <array>
#include <cstdio>
#include <memory>
#include <optional>
#include <utility>

#include "base/base_paths.h"
#include "base/barrier_closure.h"
#include "base/check.h"
#include "base/command_line.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/path_service.h"
#include "base/process/process.h"
#include "base/process/process_handle.h"
#include "base/run_loop.h"
#include "base/values.h"
#include "base/win/dark_mode_support.h"
#include "base/win/win_util.h"
#include "components/prefs/pref_service.h"
#include "components/services/quarantine/quarantine_impl.h"
#include "content/public/app/content_main.h"
#include "content/public/app/content_main_delegate.h"
#include "content/public/app/sandbox_helper_win.h"
#include "content/public/browser/browser_main_parts.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/utility/content_utility_client.h"
#include "mojo/public/cpp/bindings/service_factory.h"
#include "sandbox/win/src/sandbox_types.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/display/screen.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/views_delegate.h"
#include "ui/views/widget/desktop_aura/desktop_native_widget_aura.h"
#include "ui/views/widget/desktop_aura/desktop_screen.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/views/widget/widget_observer.h"
#include "ui/wm/core/wm_state.h"
#include "url/gurl.h"

#include "cookie_encryption_win.h"
#include "download_probe.h"
#include "extension_archive_service.h"
#include "extension_manifest.h"
#include "extensions/browser/extension_file_task_runner.h"
#include "lab_clients.h"
#include "lab_web_view.h"
#include "profile_context.h"
#include "profile_permission_controller.h"
#include "workspace_lab.h"
#include "workspace_session_store.h"
#include "workspace_timeline.h"

namespace zitter::content_embedder {
namespace {

constexpr char kSmokeTest[] = "zitter-content-smoke-test";
constexpr char kWorkspaceLocalState[] = "ContentWorkspaceLocalState.json";
constexpr char kWorkspaceSession[] = "ContentWorkspaceSession.json";
constexpr std::array<const char*, 2> kProfileDirectories = {
    "workspace-work", "workspace-personal"};
constexpr char kNetworkData[] = "Network";
constexpr char kNetworkBootstrap[] = "NetworkBootstrap";

std::optional<base::FilePath> NormalizeProbeRoot(const base::FilePath& path) {
  base::FilePath temp;
  base::FilePath canonical_temp;
  base::FilePath root;
  if (!path.IsAbsolute() || path.ReferencesParent() || base::IsLink(path) ||
      !base::GetTempDir(&temp) ||
      !base::NormalizeFilePath(temp, &canonical_temp) ||
      !base::NormalizeFilePath(path, &root) || !base::DirectoryExists(root) ||
      !root.BaseName().value().starts_with(L"zitter-content-probe-") ||
      !base::FilePath::CompareEqualIgnoreCase(root.DirName().value(),
                                             canonical_temp.value())) {
    return std::nullopt;
  }
  return root;
}

using OpenEncryptionCallback =
    base::OnceCallback<os_crypt_async::OSCryptAsync*(
        const base::FilePath&, bool, std::vector<base::FilePath>)>;

class WindowsLabMainParts final : public content::BrowserMainParts,
                                  private content::WebContentsObserver,
                                  private views::WidgetObserver {
 public:
  explicit WindowsLabMainParts(
      OpenEncryptionCallback open_encryption)
      : open_encryption_(std::move(open_encryption)) {}

  int PreCreateThreads() override {
    const auto& command_line = *base::CommandLine::ForCurrentProcess();
    const bool interactive = command_line.HasSwitch(kWorkspaceLab);
    workspace_smoke_ = command_line.HasSwitch(kWorkspaceSmokeTest);
    workspace_lab_ = interactive || workspace_smoke_;
    const bool durable = command_line.HasSwitch(kWorkspaceRoot);
    if ((durable && !interactive) || command_line.HasSwitch(kWorkspaceKeychain) ||
        command_line.HasSwitch(kWorkspaceLegacyKeychain) ||
        (command_line.HasSwitch(kWorkspaceProbe) && !durable) ||
        (workspace_lab_ &&
         (command_line.HasSwitch(kSmokeTest) ||
          command_line.GetSwitchValueASCII("zitter-rust-raster") == "metal")) ||
        (interactive && workspace_smoke_)) {
      return 1;
    }
    if (command_line.HasSwitch(kWorkspaceProbe)) {
      workspace_probe_url_ =
          GURL(command_line.GetSwitchValueASCII(kWorkspaceProbe));
      if (!workspace_probe_url_.is_valid() ||
          !workspace_probe_url_.SchemeIs("http") ||
          workspace_probe_url_.host() != "127.0.0.1" ||
          !workspace_probe_url_.has_port() || workspace_probe_url_.has_username() ||
          workspace_probe_url_.has_password() ||
          workspace_probe_url_.path() != "/" || workspace_probe_url_.has_query() ||
          workspace_probe_url_.has_ref()) {
        return 1;
      }
    }
    if (command_line.HasSwitch(kDownloadProbe)) {
      const auto root = NormalizeProbeRoot(
          command_line.GetSwitchValuePath(kProbeRoot));
      if (workspace_lab_ || command_line.HasSwitch(kSmokeTest) || !root)
        return 1;
      download_probe_ = DownloadProbe::Create(*root);
      return download_probe_ && directory_.CreateUniqueTempDirUnderPath(*root)
                 ? 0
                 : 1;
    }
    if (durable)
      return directory_.CreateUniqueTempDir() &&
                     OpenWorkspaceRoot(command_line.GetSwitchValuePath(kWorkspaceRoot))
                 ? 0
                 : 1;
    if (!directory_.CreateUniqueTempDir())
      return 1;
    if (!workspace_lab_)
      return 0;
    const auto& root = directory_.GetPath();
    return base::CreateDirectory(root.AppendASCII(kProfileDirectories[0])) &&
                   base::CreateDirectory(root.AppendASCII(kProfileDirectories[1]))
               ? 0
               : 1;
  }

  int PreMainMessageLoopRun() override {
    wm_state_ = std::make_unique<wm::WMState>();
    if (!display::Screen::HasScreen())
      screen_ = views::CreateDesktopScreen();
    views_delegate_ = std::make_unique<LabViewsDelegate>();
    if (workspace_lab_) {
      if (!ConfigureWorkspaceExtensionProbe(workspace_extension_profiles_)) {
        return 1;
      }
      if (!IsWorkspaceExtensionReviewOnly())
        CreateWorkspace();
      return 0;
    }
    context_ = std::make_unique<ProfileContext>(
        directory_.GetPath(), ProfileContext::Mode::kPrivate);
    if (download_probe_)
      download_probe_->Prepare(*context_);

    delegate_ = std::make_unique<views::WidgetDelegate>();
    delegate_->SetTitle(u"Zitter Content Lab");
    delegate_->SetHasWindowSizeControls(true);
    auto* web_view = delegate_->SetContentsView(
        std::make_unique<LabWebView>(context_.get()));
    widget_ = std::make_unique<views::Widget>();
    views::Widget::InitParams params(views::Widget::InitParams::CLIENT_OWNS_WIDGET,
                                    views::Widget::InitParams::TYPE_WINDOW);
    params.delegate = delegate_.get();
    params.bounds = gfx::Rect(80, 80, 1000, 700);
    params.native_widget = new views::DesktopNativeWidgetAura(widget_.get());
    widget_->Init(std::move(params));
    widget_->AddObserver(this);
    widget_->Show();

    if (download_probe_) {
      Observe(web_view->GetWebContents(GURL("about:blank")));
      return 0;
    }
    const GURL url = LabStartPage();
    if (base::CommandLine::ForCurrentProcess()->HasSwitch(kSmokeTest))
      Observe(web_view->GetWebContents(url));
    web_view->LoadInitialURL(url);
    return 0;
  }

  void WillRunMainMessageLoop(std::unique_ptr<base::RunLoop>& loop) override {
    quit_ = loop->QuitClosure();
    if (IsWorkspaceExtensionReviewOnly()) {
      quit_.Run();
      return;
    }
    if (workspace_) {
      workspace_->Start(base::BindOnce(
          &WindowsLabMainParts::FinishWorkspace, weak_factory_.GetWeakPtr()));
    } else if (download_probe_) {
      download_probe_->Start(*web_contents(), quit_);
    }
  }

  void PostMainMessageLoopRun() override {
    download_probe_.reset();
    Observe(nullptr);
    workspace_.reset();
    additional_profiles_.clear();
    if (widget_) {
      widget_->RemoveObserver(this);
      widget_->CloseNow();
      widget_.reset();
      delegate_.reset();
    }
    personal_context_.reset();
    context_.reset();
    views_delegate_.reset();
    screen_.reset();
    wm_state_.reset();
  }

  void PostDestroyThreads() override {
    profile_lock_.Close();
    if (directory_.IsValid())
      (void)directory_.Delete();
  }

 private:
  bool OpenWorkspaceRoot(const base::FilePath& path) {
    const bool prepared = base::CommandLine::ForCurrentProcess()->HasSwitch(
        kOpenPreparedWorkspace);
    auto root = prepared ? std::optional(path) : NormalizeProbeRoot(path);
    if (!root || (prepared && !IsPreparedContentWorkspace(*root, "windows-dpapi"))) {
      std::fputs(prepared ? "Invalid prepared Content workspace\n"
                         : "The Content workspace requires a temporary probe root\n", stderr);
      return false;
    }
    const auto lock_path = root->AppendASCII("WorkspaceLock");
    if (!base::IsLink(lock_path)) {
      profile_lock_.Initialize(lock_path,
                               base::File::FLAG_OPEN_ALWAYS |
                                   base::File::FLAG_WRITE);
    }
    if (!profile_lock_.IsValid() ||
        profile_lock_.Lock(base::File::LockMode::kExclusive) !=
            base::File::FILE_OK) {
      std::fputs("Could not lock the Content workspace root\n", stderr);
      return false;
    }

    auto session = ReadContentWorkspaceSession(root->AppendASCII(kWorkspaceSession));
    if (!session || base::IsLink(root->AppendASCII("WorkspaceSettings.json"))) {
      std::fputs("Invalid Content workspace session or settings path\n", stderr);
      return false;
    }
    workspace_timeline_ = ReadWorkspaceTimeline(
        WorkspaceTimelinePath(root->AppendASCII(kWorkspaceSession)), *session);
    if (!workspace_timeline_) {
      LOG(ERROR) << "Invalid Content workspace timeline";
      return false;
    }

    bool can_create_key = true;
    std::vector<base::FilePath> network_paths;
    for (size_t index = 0; index < kProfileDirectories.size(); ++index) {
      const auto profile = root->AppendASCII(kProfileDirectories[index]);
      const auto network = profile.AppendASCII(kNetworkData);
      network_paths.push_back(network);
      const auto bootstrap = profile.AppendASCII(kNetworkBootstrap);
      const DWORD attributes = ::GetFileAttributesW(profile.value().c_str());
      if (attributes == INVALID_FILE_ATTRIBUTES) {
        if (::GetLastError() == ERROR_FILE_NOT_FOUND)
          continue;
        return false;
      }
      if (!(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
          (attributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
          base::IsLink(network) || base::IsLink(bootstrap) ||
          base::IsLink(network.AppendASCII("Cookies")) ||
          !ProfileContext::IsPasswordStorePathSafe(profile) ||
          !ProfileContext::IsHistoryStorePathSafe(profile) ||
          (base::PathExists(bootstrap) && !base::IsDirectoryEmpty(bootstrap))) {
        std::fputs("Invalid Content workspace profile directory\n", stderr);
        return false;
      }
      password_saving_defaults_[index] =
          ProfileContext::ReadPasswordSavingDefault(profile);
      can_create_key = false;
    }
    const auto profiles = ReadContentProfiles(*session).value();
    for (size_t index = kWorkspaceIds.size(); index < profiles.size(); ++index) {
      const auto path = root->AppendASCII(profiles[index].id);
      const auto network = path.AppendASCII(kNetworkData);
      const auto bootstrap = path.AppendASCII(kNetworkBootstrap);
      const auto safe_directory = [](const base::FilePath& directory) {
        const DWORD attributes = ::GetFileAttributesW(directory.value().c_str());
        return attributes != INVALID_FILE_ATTRIBUTES &&
               (attributes & FILE_ATTRIBUTE_DIRECTORY) &&
               !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
      };
      if (profiles[index].separate_network ||
          !safe_directory(path) || !safe_directory(network) ||
          !safe_directory(bootstrap) || !base::IsDirectoryEmpty(bootstrap)) {
        LOG(ERROR) << "Invalid additional Content profile directory or preferences";
        return false;
      }
      auto startup = WorkspaceProfileStartup::Read(path);
      if (!startup) {
        LOG(ERROR) << "Invalid additional Content profile directory or preferences";
        return false;
      }
      network_paths.push_back(network);
      additional_profile_startup_.push_back(std::move(startup));
      can_create_key = false;
    }
    if (IsWorkspaceExtensionRuntime()) {
      for (const auto& network : network_paths)
        workspace_extension_profiles_.push_back(network.DirName());
    }
    os_crypt_ = std::move(open_encryption_).Run(*root, can_create_key,
                                              std::move(network_paths));
    if (!os_crypt_) {
      std::fputs("The Content workspace could not open its DPAPI key\n", stderr);
      return false;
    }
    for (size_t index = 0; index < kProfileDirectories.size(); ++index) {
      const auto profile = root->AppendASCII(kProfileDirectories[index]);
      if (!base::CreateDirectory(profile.AppendASCII(kNetworkData)) ||
          !base::CreateDirectory(profile.AppendASCII(kNetworkBootstrap))) {
        return false;
      }
      site_permission_prefs_[index] = ProfileContext::OpenSitePermissionPreferences(profile);
      file_chooser_prefs_[index] =
          ProfileContext::OpenFileChooserPreferences(profile);
      if (!site_permission_prefs_[index]) {
        std::fputs("Could not open Content site permissions\n", stderr);
        return false;
      }
      page_zoom_prefs_[index] = ProfileContext::OpenPageZoomPreferences(profile);
      if (!page_zoom_prefs_[index]) {
        LOG(ERROR) << "Could not open Content page zoom preferences";
        return false;
      }
      fingerprint_prefs_[index] =
          ProfileContext::OpenFingerprintPreferences(profile);
      adblock_prefs_[index] = ProfileContext::OpenAdblockPreferences(profile);
      if (!adblock_prefs_[index]) {
        LOG(ERROR) << "Could not open Content ad blocking preferences";
        return false;
      }
      if (!fingerprint_prefs_[index]) {
        std::fputs("Could not open Content fingerprint preferences\n", stderr);
        return false;
      }
    }
    workspace_root_ = std::move(*root);
    workspace_session_ = std::move(*session);
    return true;
  }

  void CreateWorkspace() {
    const bool durable = !workspace_root_.empty();
    const auto& root = durable ? workspace_root_ : directory_.GetPath();
    const auto mode = durable ? ProfileContext::Mode::kPersistent
                              : ProfileContext::Mode::kPrivate;
    context_ = std::make_unique<ProfileContext>(
        root.AppendASCII(kProfileDirectories[0]), mode, std::nullopt, os_crypt_,
        password_saving_defaults_[0]);
    personal_context_ = std::make_unique<ProfileContext>(
        root.AppendASCII(kProfileDirectories[1]), mode, std::nullopt, os_crypt_,
        password_saving_defaults_[1]);
    if (durable) {
      context_->SetFileChooserPreferences(std::move(file_chooser_prefs_[0]));
      personal_context_->SetFileChooserPreferences(
          std::move(file_chooser_prefs_[1]));
      context_->SetPageZoomPreferences(std::move(page_zoom_prefs_[0]));
      personal_context_->SetPageZoomPreferences(std::move(page_zoom_prefs_[1]));
      context_->SetFingerprintPreferences(std::move(fingerprint_prefs_[0]));
      personal_context_->SetFingerprintPreferences(
          std::move(fingerprint_prefs_[1]));
      context_->SetAdblockPreferences(std::move(adblock_prefs_[0]));
      personal_context_->SetAdblockPreferences(std::move(adblock_prefs_[1]));
      context_->site_permissions().SetSitePermissionPreferences(
          std::move(site_permission_prefs_[0]));
      personal_context_->site_permissions().SetSitePermissionPreferences(
          std::move(site_permission_prefs_[1]));
    }
    const auto& command_line = *base::CommandLine::ForCurrentProcess();
    const bool dark =
        command_line.GetSwitchValueASCII("zitter-content-workspace-theme") ==
        "dark";
    for (auto& startup : additional_profile_startup_)
      additional_profiles_.push_back(startup->Open(*os_crypt_));
    additional_profile_startup_.clear();
    workspace_ = std::make_unique<WorkspaceLab>(
        root.AppendASCII("WorkspaceSettings.json"), directory_.GetPath(),
        durable ? root.AppendASCII(kWorkspaceSession) : base::FilePath(),
        std::move(workspace_session_), std::move(workspace_timeline_),
        workspace_probe_url_, *context_,
        personal_context_.get(), dark, /*metal=*/false, workspace_smoke_,
        nullptr, additional_profiles_, os_crypt_);
  }

  void FinishWorkspace(bool success) {
    if (workspace_smoke_) {
      if (success)
        std::puts("Zitter Content workspace smoke passed");
      else
        std::fputs("Zitter Content workspace smoke failed\n", stderr);
    }
    if (!success || workspace_root_.empty()) {
      quit_.Run();
      return;
    }
    const auto profiles = workspace_->profiles_for_shutdown();
    const bool flush_extensions = IsWorkspaceExtensionRuntime();
    auto flushed = base::BarrierClosure(
        profiles.size() + flush_extensions,
        base::BindOnce(&WindowsLabMainParts::WorkspaceFlushed,
                          weak_factory_.GetWeakPtr()));
    for (const auto& profile : profiles) {
      auto* partition = profile->GetDefaultStoragePartition();
      partition->Flush();
      partition->GetCookieManagerForBrowserProcess()->FlushCookieStore(flushed);
    }
    if (flush_extensions) {
      extensions::GetExtensionFileTaskRunner()->PostTaskAndReply(
          FROM_HERE, base::DoNothing(), flushed);
    }
  }

  void WorkspaceFlushed() {
    if (workspace_probe_url_.is_valid() ||
        GetExtensionFixtureStateAction() == ExtensionFixtureStateAction::kManageSmoke)
      std::puts("Zitter Content workspace session probe passed");
    quit_.Run();
  }

  void OnWidgetClosing(views::Widget*) override {
    if (quit_)
      quit_.Run();
  }

  void DocumentOnLoadCompletedInPrimaryMainFrame() override {
    if (!web_contents()->GetLastCommittedURL().SchemeIs("data"))
      return;

    const base::Process& renderer =
        web_contents()->GetPrimaryMainFrame()->GetProcess()->GetProcess();
    if (web_contents()->GetTitle() == u"Zitter Content Lab" &&
        renderer.IsValid() && renderer.Pid() != base::GetCurrentProcId()) {
      std::puts("Zitter Content Lab smoke passed");
    }
    if (quit_)
      quit_.Run();
  }

  base::ScopedTempDir directory_;
  OpenEncryptionCallback open_encryption_;
  raw_ptr<os_crypt_async::OSCryptAsync> os_crypt_ = nullptr;
  base::File profile_lock_;
  std::array<std::optional<bool>, 2> password_saving_defaults_ = {true, true};
  std::array<std::unique_ptr<PrefService>, 2> fingerprint_prefs_;
  std::array<std::unique_ptr<PrefService>, 2> page_zoom_prefs_;
  std::array<std::unique_ptr<ProfileContext::AdblockPreferences>, 2>
      adblock_prefs_;
  std::array<std::unique_ptr<PrefService>, 2> site_permission_prefs_;
  std::array<std::unique_ptr<PrefService>, 2> file_chooser_prefs_;
  base::FilePath workspace_root_;
  base::DictValue workspace_session_;
  std::unique_ptr<prototype::TimelineStore> workspace_timeline_;
  GURL workspace_probe_url_;
  std::unique_ptr<wm::WMState> wm_state_;
  std::unique_ptr<display::Screen> screen_;
  std::unique_ptr<LabViewsDelegate> views_delegate_;
  std::unique_ptr<ProfileContext> context_;
  std::unique_ptr<ProfileContext> personal_context_;
  std::vector<std::unique_ptr<WorkspaceProfileStartup>> additional_profile_startup_;
  std::vector<base::FilePath> workspace_extension_profiles_;
  std::vector<std::unique_ptr<ProfileContext>> additional_profiles_;
  std::unique_ptr<DownloadProbe> download_probe_;
  std::unique_ptr<WorkspaceLab> workspace_;
  std::unique_ptr<views::WidgetDelegate> delegate_;
  std::unique_ptr<views::Widget> widget_;
  base::RepeatingClosure quit_;
  bool workspace_lab_ = false;
  bool workspace_smoke_ = false;
  base::WeakPtrFactory<WindowsLabMainParts> weak_factory_{this};
};

class WindowsLabBrowserClient final : public LabBrowserClientBase {
 public:
  std::unique_ptr<content::BrowserMainParts> CreateBrowserMainParts(
      bool) override {
    return std::make_unique<WindowsLabMainParts>(
        base::BindOnce(&WindowsLabBrowserClient::OpenEncryption,
                       base::Unretained(this)));
  }

  std::string GetApplicationLocale() override { return "en-US"; }
  bool ShouldEnableStrictSiteIsolation() override { return true; }

  void ConfigureNetworkContextParams(
      content::BrowserContext* context,
      bool in_memory,
      const base::FilePath& relative_partition_path,
      network::mojom::NetworkContextParams* params,
      cert_verifier::mojom::CertVerifierCreationParams* cert_params) override {
    LabBrowserClientBase::ConfigureNetworkContextParams(
        context, in_memory, relative_partition_path, params, cert_params);
    if (in_memory || context->IsOffTheRecord() ||
        !relative_partition_path.empty()) {
      return;
    }
    const auto& path = context->GetPath();
    CHECK(encryption_ && path.DirName() == workspace_root_);
    encryption_->Configure(*params);
    params->file_paths = network::mojom::NetworkContextFilePaths::New();
    params->file_paths->data_directory = path.AppendASCII(kNetworkData);
    params->file_paths->cookie_database_name = base::FilePath(L"Cookies");
    // Chromium uses this empty sibling to grant the network sandbox access
    // to Network without giving it access to the profile or Local State.
    params->file_paths->unsandboxed_data_path =
        path.AppendASCII(kNetworkBootstrap);
    params->file_paths->trigger_migration = true;
  }

 private:
  os_crypt_async::OSCryptAsync* OpenEncryption(const base::FilePath& root,
      bool can_create_key, std::vector<base::FilePath> network_paths) {
    encryption_ = CookieEncryptionWin::Open(
        root.AppendASCII(kWorkspaceLocalState), can_create_key, network_paths);
    if (!encryption_)
      return nullptr;
    workspace_root_ = root;
    return encryption_->os_crypt();
  }

  base::FilePath workspace_root_;
  std::unique_ptr<CookieEncryptionWin> encryption_;
};

class WindowsLabUtilityClient final : public LabUtilityClient {
 public:
  void RegisterMainThreadServices(mojo::ServiceFactory& services) override;
};

void WindowsLabUtilityClient::RegisterMainThreadServices(
    mojo::ServiceFactory& services) {
  LabUtilityClient::RegisterMainThreadServices(services);
  services.Add(
      +[](mojo::PendingReceiver<quarantine::mojom::Quarantine> receiver) {
        return std::make_unique<quarantine::QuarantineImpl>(std::move(receiver));
      });
}

class WindowsLabMainDelegate final : public content::ContentMainDelegate {
 public:
  std::optional<int> BasicStartupComplete() override {
    return CheckLabStartupFlags();
  }

  void PreSandboxStartup() override {
    ui::ResourceBundle::InitSharedInstanceWithPakPath(
        base::PathService::CheckedGet(base::DIR_ASSETS)
            .AppendASCII("zitter_content.pak"));
  }

  std::optional<int> PreBrowserMain() override {
    return ConfigureExtensionProbes();
  }

  content::ContentClient* CreateContentClient() override { return &client_; }
  content::ContentBrowserClient* CreateContentBrowserClient() override {
    return &browser_client_;
  }
  content::ContentRendererClient* CreateContentRendererClient() override {
    return &renderer_client_;
  }
  content::ContentUtilityClient* CreateContentUtilityClient() override {
    return &utility_client_;
  }

 private:
  LabContentClient client_;
  WindowsLabBrowserClient browser_client_;
  LabRendererClient renderer_client_;
  WindowsLabUtilityClient utility_client_;
};

}  // namespace
}  // namespace zitter::content_embedder

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, wchar_t*, int) {
  base::win::PinUser32();
  base::win::AllowDarkModeForApp(true);
  base::CommandLine::Init(0, nullptr);

  sandbox::SandboxInterfaceInfo sandbox_info = {};
  content::InitializeSandboxInfo(&sandbox_info);
  zitter::content_embedder::WindowsLabMainDelegate delegate;
  content::ContentMainParams params(&delegate);
  params.instance = instance;
  params.sandbox_info = &sandbox_info;
  return content::ContentMain(std::move(params));
}
