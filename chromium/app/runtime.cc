#include "chromium/app/runtime.h"
#include <algorithm>
#include <array>
#include <utility>
#include "chromium/app/performance_probe.h"
#include "chromium/app/startup_timing.h"

#include "base/check.h"
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/browser_process.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/browser/download/download_core_service.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/lifetime/application_lifetime_desktop.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_destroyer.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/tab_list/tab_list_interface.h"
#include "components/tabs/public/tab_interface.h"
#include "chromium/prototype/workspace_window.h"
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
#include "chromium/prototype/i18n.h"
#endif
#if BUILDFLAG(IS_WIN)
#include "chromium/prototype/jump_list_win.h"
#include "chromium/app/updater_win.h"
#endif
#include "components/keep_alive_registry/keep_alive_types.h"
#include "components/keep_alive_registry/scoped_keep_alive.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/base_window.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"
#if BUILDFLAG(IS_MAC)
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/thread_pool.h"
#include "base/version.h"
#include "chrome/browser/platform_util.h"
#include "chromium/app/ui_lab.h"
#include "crypto/hash.h"
#include "net/base/load_flags.h"
#include "net/url_request/redirect_info.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#endif

namespace zitter {
Runtime::Runtime() = default;
Runtime::~Runtime() = default;

void Runtime::PreProfileInit() {
  MarkStartup("pre_profile_init");
}

void Runtime::PreBrowserStart() {
  MarkStartup("pre_browser_start");
  keep_alive_ = std::make_unique<ScopedKeepAlive>(
      KeepAliveOrigin::BROWSER, KeepAliveRestartOption::DISABLED);
#if BUILDFLAG(IS_WIN)
  // Chrome initializes services/profiles, but only our Views window is opened.
  // Set here, in the process that owns the profile, not in main(): a second
  // zitter.exe (a link from another app, the Start menu) forwards its command
  // line, and with this switch Chrome would drop its URLs. Without it, Chrome
  // opens them in a stock window, which ReplaceStockWindow moves into Zitter.
  base::CommandLine::ForCurrentProcess()->AppendSwitch(switches::kNoStartupWindow);
#endif
  // Chrome's Print Preview dialog needs a Chrome browser window. Ctrl+P opens
  // Zitter's print sheet; a page's window.print() gets the system dialog.
  base::CommandLine::ForCurrentProcess()->AppendSwitch(switches::kDisablePrintPreview);
}

void Runtime::PreMainMessageLoopRun() {
  MarkStartup("pre_main_loop");
#if BUILDFLAG(IS_MAC)
  InstallAppShell();
#endif
  browsers_.Observe(GlobalBrowserCollection::GetInstance());
#if !BUILDFLAG(IS_MAC)
  // Chromium quitting by itself (DevTools Browser.close, chrome://quit, the
  // session ending) closes all browsers, which Zitter's keep-alive outlives;
  // macOS routes these through the app shell's terminate hook instead.
  closing_all_browsers_ = chrome::AddClosingAllBrowsersCallback(base::BindRepeating(
      [](base::WeakPtr<Runtime> runtime, bool closing) {
        if (!runtime || !closing || runtime->closing_) return;
        runtime->downloads_confirmed_ = true;
        runtime->RequestQuitAll();
      }, weak_factory_.GetWeakPtr()));
#endif
#if BUILDFLAG(IS_MAC)
  if (base::CommandLine::ForCurrentProcess()->HasSwitch("zitter-ui-lab")) {
    SetAppShellDelegate(this);
    ui_lab_ = UiLab::Create(base::BindOnce(&Runtime::RequestClose, weak_factory_.GetWeakPtr()));
    return;
  }
#endif
  auto* manager = g_browser_process->profile_manager();
  window_ = std::make_unique<prototype::WorkspaceWindow>(manager->user_data_dir());
  window_->SetCloseCallback(base::BindOnce(&Runtime::RequestClose, weak_factory_.GetWeakPtr()));
  window_->SetNewPrivateWindowCallback(base::BindRepeating(&Runtime::OpenPrivateWindow, weak_factory_.GetWeakPtr()));
  window_->SetReadyCallback(base::BindOnce(&Runtime::WindowReady, weak_factory_.GetWeakPtr()));
}

void Runtime::WindowReady() {
  MarkStartup("workspace_restored");
#if BUILDFLAG(IS_WIN)
  updater_ = std::make_unique<WindowsUpdater>(
      g_browser_process->profile_manager()->user_data_dir());
  const bool updates_available = updater_->Start();
#elif BUILDFLAG(IS_MAC)
  const auto* command_line = base::CommandLine::ForCurrentProcess();
  const bool updates_available = !command_line->HasSwitch("disable-zitter-updates") &&
                                 !command_line->HasSwitch("zitter-app-benchmark");
#endif
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
  if (updates_available) {
    window_->shell()->SetUpdateCheckCallback(
        base::BindRepeating(&Runtime::CheckForUpdates, weak_factory_.GetWeakPtr()),
        base::BindRepeating(&Runtime::ConfigureUpdates, weak_factory_.GetWeakPtr()));
  }
#endif
  for (const auto& arg : base::CommandLine::ForCurrentProcess()->GetArgs()) {
#if BUILDFLAG(IS_WIN)
    GURL url(base::WideToUTF8(arg));
#else
    GURL url(arg);
#endif
    if (url.SchemeIsHTTPOrHTTPS()) window_->OpenExternalUrl(url);
  }
  window_->widget()->Activate();
#if BUILDFLAG(IS_WIN)
  // A jump list entry that started Zitter (no Zitter was running to take it).
  if (const auto command = base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(prototype::kZitterCommandSwitch);
      !command.empty() && window_->RunExternalCommand(command))
    return;
#endif
  // A restored workspace should reopen as its saved layout. Offer the palette
  // only for an empty page, rather than covering the restored panes on launch.
  auto* page = window_->contents(window_->active_id());
  if (!page || !page->GetVisibleURL().SchemeIsHTTPOrHTTPS()) window_->FocusAddressBar();
  else page->Focus();
#if BUILDFLAG(IS_MAC)
  SetAppShellDelegate(this);
  window_->shell()->SyncShortcuts();
#endif
  MarkStartup("workspace_ready");
  LOG(INFO) << "Zitter ready; persistent data: " << g_browser_process->profile_manager()->user_data_dir();
  if (base::CommandLine::ForCurrentProcess()->HasSwitch("zitter-app-benchmark")) {
    performance_probe_ = std::make_unique<AppPerformanceProbe>(window_.get(),
        base::BindOnce(&Runtime::RequestClose, weak_factory_.GetWeakPtr()));
    performance_probe_->Start();
  }
}

#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
void Runtime::ConfigureUpdates(bool check, bool download) {
  const bool start_download = download && !auto_download_updates_;
  auto_download_updates_ = download;
#if BUILDFLAG(IS_MAC)
  if ((!check || !download) && update_download_) {
    update_download_.reset();
    preparing_mac_installer_ = false;
  }
#endif
  if (!check) {
    update_startup_timer_.Stop();
    update_timer_.Stop();
    return;
  }
  if (update_timer_.IsRunning()) {
    if (start_download && !update_startup_timer_.IsRunning()) CheckInBackground();
    return;
  }
  update_startup_timer_.Start(FROM_HERE, base::Seconds(30), this,
                              &Runtime::CheckInBackground);
  update_timer_.Start(FROM_HERE, base::Hours(6), this, &Runtime::CheckInBackground);
}

void Runtime::CheckInBackground() {
  if (!window_ || closing_) return;
#if BUILDFLAG(IS_WIN)
  CheckWindowsUpdate(auto_download_updates_, {});
#elif BUILDFLAG(IS_MAC)
  StartMacCheck({}, true);
#endif
}

void Runtime::CheckForUpdates(UpdateCheckDone done) {
#if BUILDFLAG(IS_WIN)
  CheckWindowsUpdate(auto_download_updates_, std::move(done));
#elif BUILDFLAG(IS_MAC)
  StartMacCheck(std::move(done), false);
#endif
}
#endif

#if BUILDFLAG(IS_WIN)
void Runtime::CheckWindowsUpdate(bool download, UpdateCheckDone done) {
  if (!window_ || closing_ || !updater_) {
    if (done) std::move(done).Run(prototype::T(u"Nie udało się sprawdzić aktualizacji."), {});
    return;
  }
  updater_->CheckNow(download, base::BindOnce(
      [](base::WeakPtr<Runtime> runtime, UpdateCheckDone done,
         const updates::CheckResult& result) {
        if (!runtime || !runtime->window_ || runtime->closing_) return;
        auto* shell = runtime->window_->shell();
        std::u16string message;
        switch (result.status) {
          case updates::CheckStatus::kReady:
            if (!done && !runtime->update_timer_.IsRunning()) break;
            shell->ShowUpdateReady(result.release->version, base::BindRepeating(
                [](base::WeakPtr<Runtime> runtime) {
                  if (!runtime || runtime->closing_ || !runtime->updater_) return;
                  runtime->updater_->RequestInstall();
                  runtime->RequestClose();
                }, runtime->weak_factory_.GetWeakPtr()), false);
            break;
          case updates::CheckStatus::kAvailable:
            if (done || runtime->update_timer_.IsRunning()) {
              const auto& version = result.release->version;
              shell->ShowUpdateOffer(version, prototype::TF(u"Dostępna jest wersja {}.",
                  {base::UTF8ToUTF16(version)}), prototype::T(u"Pobierz aktualizację"),
                  base::BindRepeating([](base::WeakPtr<Runtime> runtime) {
                    if (!runtime || !runtime->window_ || runtime->closing_) return;
                    runtime->window_->shell()->DownloadUpdate(base::BindOnce(
                        &Runtime::CheckWindowsUpdate, runtime, true));
                  }, runtime->weak_factory_.GetWeakPtr()));
            }
            break;
          case updates::CheckStatus::kNoUpdate:
            message = prototype::T(u"Masz najnowszą dostępną wersję Zitter dla Windows.");
            break;
          case updates::CheckStatus::kFailed:
            message = prototype::T(u"Nie udało się sprawdzić lub pobrać aktualizacji.");
            break;
        }
        if (done) std::move(done).Run(std::move(message), {});
      }, weak_factory_.GetWeakPtr(), std::move(done)));
}
#elif BUILDFLAG(IS_MAC)
namespace {
constexpr int64_t kMaxMacInstaller = 600 * 1024 * 1024;

bool VerifyMacInstaller(const base::FilePath& path, int64_t size,
                        const std::array<uint8_t, crypto::hash::kSha256Size>& digest) {
  base::File file(path, base::File::FLAG_OPEN | base::File::FLAG_READ);
  std::array<uint8_t, crypto::hash::kSha256Size> actual;
  return file.IsValid() && file.GetLength() == size &&
         crypto::hash::HashFile(crypto::hash::kSha256, &file, actual) && actual == digest;
}
}  // namespace

struct Runtime::MacInstaller {
  std::string version;
  GURL url;
  int64_t size;
  std::array<uint8_t, crypto::hash::kSha256Size> digest;
  base::FilePath path;
};

void Runtime::StartMacCheck(UpdateCheckDone done, bool background) {
  if (!window_ || closing_ || !window_->active_profile()) {
    if (done) std::move(done).Run(prototype::T(u"Nie udało się sprawdzić aktualizacji."), {});
    return;
  }
  if (done) update_checks_.push_back(std::move(done));
  background_update_check_ |= background;
  if (update_loader_) return;

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GURL("https://api.github.com/repos/doctorspider42/zitter/releases?per_page=20");
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->redirect_mode = network::mojom::RedirectMode::kError;
  request->load_flags = net::LOAD_DISABLE_CACHE | net::LOAD_DO_NOT_SAVE_COOKIES;
  request->headers.SetHeader("Accept", "application/vnd.github+json");
  constexpr auto annotation = net::DefineNetworkTrafficAnnotation("zitter_manual_update", R"(
    semantics {
      sender: "Zitter update check"
      description: "Fetches recent stable Zitter releases to find an installer for this Mac."
      trigger: "The user requests a check, or at startup and periodically when automatic checks are enabled."
      data: "A request for public release metadata, without cookies or authentication."
      destination: WEBSITE
    }
    policy {
      cookies_allowed: NO
      setting: "The user can turn off automatic checks in Zitter Preferences."
      policy_exception_justification: "Public release metadata is requested without user data."
    })");
  update_loader_ = network::SimpleURLLoader::Create(std::move(request), annotation);
  update_loader_->SetTimeoutDuration(base::Seconds(15));
  auto factory = window_->active_profile()->GetDefaultStoragePartition()
                     ->GetURLLoaderFactoryForBrowserProcess();
  update_loader_->DownloadToString(
      factory.get(), base::BindOnce(&Runtime::MacUpdateChecked, weak_factory_.GetWeakPtr()),
      2 * 1024 * 1024);
}

void Runtime::MacUpdateChecked(std::optional<std::string> body) {
  update_loader_.reset();
  const bool background = std::exchange(background_update_check_, false) && update_timer_.IsRunning();
  const auto releases = body ? base::JSONReader::ReadList(*body, base::JSON_PARSE_RFC)
                             : std::nullopt;
  const base::Version installed(MacAppVersion());
  base::Version latest;
  GURL release_url;
  std::optional<MacInstaller> installer;
  std::u16string message = prototype::T(u"Nie udało się sprawdzić aktualizacji.");
  if (releases && installed.IsValid()) {
#if defined(ARCH_CPU_ARM64)
    constexpr char kMacInstaller[] = "arm64.dmg";
#else
    constexpr char kMacInstaller[] = "x86_64.dmg";
#endif
    const auto cache = g_browser_process->profile_manager()->user_data_dir().AppendASCII("Updates");
    // GitHub's Latest flag may point to a separate release for Windows.
    bool has_stable_release = false;
    for (const auto& entry : *releases) {
      const auto* release = entry.GetIfDict();
      const auto* tag = release ? release->FindString("tag_name") : nullptr;
      const auto* assets = release ? release->FindList("assets") : nullptr;
      if (!tag || !tag->starts_with('v') || !assets || release->FindBool("draft") != false ||
          release->FindBool("prerelease") != false) continue;
      const base::Version version(tag->substr(1));
      if (!version.IsValid() || version.components().size() != 3 || "v" + version.GetString() != *tag)
        continue;
      has_stable_release = true;
      if (latest.IsValid() && version.CompareTo(latest) <= 0) continue;

      const std::string prefix = "Zitter-" + *tag + "-macos-";
      bool has_installer = false;
      std::optional<MacInstaller> verified_asset;
      for (const auto& asset : *assets) {
        const auto* file = asset.GetIfDict();
        const auto* name = file ? file->FindString("name") : nullptr;
        if (!name || (*name != prefix + kMacInstaller && *name != prefix + "universal.dmg")) continue;
        has_installer = true;
        if (version.CompareTo(installed) <= 0) break;
        const auto* url = file->FindString("browser_download_url");
        const auto* digest = file->FindString("digest");
        const auto size = file->FindInt("size");
        MacInstaller candidate{version.GetString(),
            GURL("https://github.com/doctorspider42/zitter/releases/download/" + *tag + "/" + *name),
            size.value_or(0), {}, cache.AppendASCII(*name)};
        if (url && *url == candidate.url.spec() && size && *size > 0 &&
            *size <= kMaxMacInstaller && digest && digest->starts_with("sha256:") &&
            base::HexStringToSpan(digest->substr(7), candidate.digest)) {
          verified_asset = std::move(candidate);
          if (*name == prefix + kMacInstaller) break;
        }
      }
      if (!has_installer) continue;
      latest = version;
      installer = std::move(verified_asset);
      release_url = GURL("https://github.com/doctorspider42/zitter/releases/tag/" + *tag);
    }
    if (latest.IsValid()) {
      if (latest.CompareTo(installed) > 0) {
        message = prototype::TF(u"Dostępna jest wersja {}.",
                                {base::UTF8ToUTF16(latest.GetString())});
      } else {
        message = prototype::T(u"Masz najnowszą wersję Zitter.");
        release_url = GURL();
      }
    } else if (has_stable_release) {
      message = prototype::T(u"Najnowsze wydanie nie ma instalatora dla tego Maca.");
    }
  }
  if (!window_ || closing_) return;
  if (background && release_url.is_valid() && downloaded_mac_version_ != latest.GetString()) {
    window_->shell()->ShowUpdateOffer(latest.GetString(), message,
        prototype::T(u"Otwórz stronę wydania"),
        base::BindRepeating([](base::WeakPtr<Runtime> runtime, const GURL& url) {
          if (!runtime || !runtime->window_ || runtime->closing_) return;
          runtime->window_->shell()->CloseOverlays();
          runtime->OpenUrl(url);
        }, weak_factory_.GetWeakPtr(), release_url));
  }
  if (auto_download_updates_ && (background || !update_checks_.empty()) &&
      installer && !preparing_mac_installer_ &&
      downloaded_mac_version_ != installer->version) {
    PrepareMacInstaller(std::move(*installer));
  }
  for (auto& done : std::exchange(update_checks_, {}))
    std::move(done).Run(message, release_url);
}

void Runtime::PrepareMacInstaller(MacInstaller installer) {
  preparing_mac_installer_ = true;
  auto verify = base::BindOnce(&VerifyMacInstaller, installer.path, installer.size, installer.digest);
  base::ThreadPool::PostTaskAndReplyWithResult(FROM_HERE,
      {base::MayBlock(), base::TaskPriority::BEST_EFFORT}, std::move(verify),
      base::BindOnce(&Runtime::DownloadMacInstaller, weak_factory_.GetWeakPtr(), std::move(installer)));
}

void Runtime::DownloadMacInstaller(MacInstaller installer, bool already_cached) {
  if (already_cached || !auto_download_updates_ || !window_ || closing_ || !window_->active_profile()) {
    MacInstallerReady(std::move(installer), already_cached);
    return;
  }
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = installer.url;
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->load_flags = net::LOAD_DISABLE_CACHE | net::LOAD_DO_NOT_SAVE_COOKIES;
  constexpr auto annotation = net::DefineNetworkTrafficAnnotation("zitter_update_installer", R"(
    semantics {
      sender: "Zitter installer download"
      description: "Downloads an installer for a newer stable Zitter release."
      trigger: "An update is found while automatic downloads are enabled."
      data: "The public release asset URL, without cookies or authentication."
      destination: WEBSITE
    }
    policy {
      cookies_allowed: NO
      setting: "Automatic downloads are opt-in in Zitter Preferences."
      policy_exception_justification: "The user enabled automatic downloads."
    })");
  update_download_ = network::SimpleURLLoader::Create(std::move(request), annotation);
  update_download_->SetTimeoutDuration(base::Minutes(15));
  update_download_->SetOnRedirectCallback(base::BindRepeating(
      [](base::WeakPtr<Runtime> runtime, const GURL&, const net::RedirectInfo& redirect,
         const network::mojom::URLResponseHead&, std::vector<std::string>*) {
        const auto& url = redirect.new_url;
        if (url.SchemeIs("https") && !url.has_username() && !url.has_password() &&
            !url.has_port() && (url.host() == "github.com" ||
                                url.host() == "release-assets.githubusercontent.com" ||
                                url.host() == "objects.githubusercontent.com")) return;
        runtime->update_download_.reset();
        runtime->preparing_mac_installer_ = false;
      }, weak_factory_.GetWeakPtr()));
  auto factory = window_->active_profile()->GetDefaultStoragePartition()
                     ->GetURLLoaderFactoryForBrowserProcess();
  const auto size = installer.size;
  update_download_->DownloadToTempFile(factory.get(),
      base::BindOnce(&Runtime::MacInstallerDownloaded, weak_factory_.GetWeakPtr(), std::move(installer)),
      size);
}

void Runtime::MacInstallerDownloaded(MacInstaller installer, base::FilePath temporary) {
  update_download_.reset();
  auto verify = base::BindOnce([](base::FilePath file, const MacInstaller& installer) {
    if (file.empty()) return false;
    const bool saved = VerifyMacInstaller(file, installer.size, installer.digest) &&
                       base::CreateDirectory(installer.path.DirName()) &&
                       base::ReplaceFile(file, installer.path, nullptr);
    if (!saved) base::DeleteFile(file);
    return saved;
  }, std::move(temporary), installer);
  base::ThreadPool::PostTaskAndReplyWithResult(FROM_HERE,
      {base::MayBlock(), base::TaskPriority::BEST_EFFORT, base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
      std::move(verify),
      base::BindOnce(&Runtime::MacInstallerReady, weak_factory_.GetWeakPtr(), std::move(installer)));
}

void Runtime::MacInstallerReady(MacInstaller installer, bool verified) {
  preparing_mac_installer_ = false;
  if (!verified || !window_ || closing_) return;
  downloaded_mac_version_ = installer.version;
  window_->shell()->ShowUpdateReady(std::move(installer.version), base::BindRepeating(
      [](base::WeakPtr<Runtime> runtime, const base::FilePath& path) {
        if (runtime && runtime->window_ && !runtime->closing_ && runtime->window_->active_profile())
          platform_util::ShowItemInFolder(runtime->window_->active_profile(), path);
      }, weak_factory_.GetWeakPtr(), std::move(installer.path)));
}
#endif

prototype::WorkspaceWindow* Runtime::ActiveWindow() const {
  for (const auto& window : private_windows_) {
    if (window->widget()->IsActive()) return window.get();
    for (const auto& owned : views::Widget::GetAllOwnedWidgets(window->widget()->GetNativeView()))
      if (owned->IsActive()) return window.get();
  }
  return window_.get();
}

void Runtime::OpenPrivateWindow() {
  if (closing_ || !window_ || !window_->configuration_ready()) return;
  if (!private_profile_) {
    auto* parent = window_->active_profile();
    if (!parent) return;
    parent = parent->GetOriginalProfile();
    private_parent_lease_ = ScopedProfileKeepAlive::TryAcquire(parent, ProfileKeepAliveOrigin::kBrowserWindow);
    if (!private_parent_lease_) return;
    // A unique OTR context disables extensions and prevents a new session from
    // reusing a context whose renderer shutdown is still pending.
    private_profile_ = parent->GetOffTheRecordProfile(Profile::OTRProfileID::CreateUnique("ZitterPrivate"), true);
  }
  auto window = std::make_unique<prototype::WorkspaceWindow>(
      g_browser_process->profile_manager()->user_data_dir(), false, private_profile_);
  window->SetAddressCopyPreferenceSource(base::BindRepeating(
      [](base::WeakPtr<Runtime> runtime) {
        return !runtime || !runtime->window_ || runtime->window_->clean_address_copies();
      }, weak_factory_.GetWeakPtr()));
  auto* pointer = window.get();
#if BUILDFLAG(IS_MAC)
  const auto* command_line = base::CommandLine::ForCurrentProcess();
  if (!command_line->HasSwitch("disable-zitter-updates") &&
      !command_line->HasSwitch("zitter-app-benchmark")) {
    window->shell()->SetUpdateCheckCallback(base::BindRepeating(
        &Runtime::CheckForUpdates, weak_factory_.GetWeakPtr()));
  }
#endif
  window->SetNewPrivateWindowCallback(base::BindRepeating(&Runtime::OpenPrivateWindow, weak_factory_.GetWeakPtr()));
  window->SetCloseCallback(base::BindOnce([](base::WeakPtr<Runtime> runtime, prototype::WorkspaceWindow* window) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
        base::BindOnce(&Runtime::ClosePrivateWindow, runtime, window));
  }, weak_factory_.GetWeakPtr(), pointer));
  private_windows_.push_back(std::move(window));
  pointer->widget()->Activate();
  pointer->FocusAddressBar();
}

void Runtime::ClosePrivateWindow(prototype::WorkspaceWindow* window) {
  std::erase_if(private_windows_, [window](const auto& entry) { return entry.get() == window; });
  if (!private_windows_.empty()) { if (quit_requested_) RequestQuitAll(); return; }
  if (private_profile_) {
    auto* profile = private_profile_.get();
    private_profile_ = nullptr;
    ProfileDestroyer::DestroyOTRProfileWhenAppropriate(profile);
    private_parent_lease_.reset();
  }
  if (quit_requested_) { RequestQuitAll(); return; }
  if (window_ && !window_->widget()->IsVisible()) RequestClose();
}

void Runtime::OnBrowserCreated(BrowserWindowInterface* browser) {
  // DevTools, app and popup windows keep their upstream behavior.
  if (browser->GetType() != BrowserWindowInterface::TYPE_NORMAL) return;
  // A stock window means an app entry point bypassed Zitter's shell.
  LOG(ERROR) << "Closing a stock Chrome window; route this entry point to Zitter";
  stock_windows_.insert(browser);
  // Never close a Browser from inside its own creation notification.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
      base::BindOnce(&Runtime::ReplaceStockWindow, weak_factory_.GetWeakPtr(), browser));
}

void Runtime::OnBrowserClosed(BrowserWindowInterface* browser) {
  stock_windows_.erase(browser);
}

void Runtime::ReplaceStockWindow(BrowserWindowInterface* browser) {
  if (!stock_windows_.contains(browser)) return;  // Already gone.
  stock_windows_.erase(browser);
  if (browser->GetProfile()->IsOffTheRecord()) {
    OpenPrivateWindow();
    if (!private_windows_.empty()) {
      if (auto* tabs = TabListInterface::From(browser))
        for (int i = 0; i < tabs->GetTabCount(); ++i) {
          const auto url = tabs->GetTab(i)->GetContents()->GetVisibleURL();
          if (url.SchemeIsHTTPOrHTTPS()) private_windows_.back()->NavigateFromBar(url, true);
        }
    }
    if (auto* window = browser->GetWindow()) window->Close();
    return;
  }
  // Extensions open tabs (chrome.tabs.create, a welcome page after install)
  // through a stock window of their profile; those pages move into Zitter.
  if (auto* tabs = window_ && !closing_ ? TabListInterface::From(browser) : nullptr) {
    std::vector<GURL> urls;
    for (int i = 0; i < tabs->GetTabCount(); ++i) {
      auto* tab = tabs->GetTab(i);
      const GURL url = tab && tab->GetContents() ? tab->GetContents()->GetVisibleURL() : GURL();
      if (url.is_valid() && !url.IsAboutBlank() && url.host() != "newtab") urls.push_back(url);
    }
    for (const auto& url : urls) {
#if !BUILDFLAG(IS_MAC)
      // Links forwarded by a second zitter.exe open as external links do (the
      // active space, domain rules); macOS receives those through the app shell.
      if (url.SchemeIsHTTPOrHTTPS() || url.SchemeIsFile()) { window_->OpenExternalUrl(url); continue; }
#endif
      window_->OpenExtensionPage(browser->GetProfile(), url);
    }
  }
  if (auto* window = browser->GetWindow()) window->Close();
  if (window_ && !closing_) { window_->widget()->Show(); window_->widget()->Activate(); }
}

#if BUILDFLAG(IS_MAC)
void Runtime::RequestQuit() { RequestQuitAll(); }

void Runtime::RaiseWindow() {
  if (ui_lab_) { ui_lab_->Raise(); return; }
  if (window_ && !closing_) { window_->widget()->Show(); window_->widget()->Activate(); }
}

void Runtime::OpenUrl(const GURL& url) {
  if (ui_lab_) { ui_lab_->Navigate(url); ui_lab_->Raise(); return; }
  if (!window_ || closing_) return;
  window_->OpenExternalUrl(url);
  window_->widget()->Activate();
}

void Runtime::RunCommand(AppCommand command) {
  if (!window_ || closing_) return;
  auto* target = ActiveWindow();
  auto* shell = target->shell();
  if (command == AppCommand::kAbout && !target->widget()->IsVisible())
    target->widget()->Show();
  target->widget()->Activate();
  switch (command) {
    case AppCommand::kAbout: shell->ShowAbout(MacAppVersion()); break;
    case AppCommand::kNewPrivateWindow: OpenPrivateWindow(); break;
    case AppCommand::kNewTab: target->ClosePeek(); shell->OpenAddress(true); break;
    case AppCommand::kOpenAddress: target->FocusAddressBar(); break;
    case AppCommand::kCommandPalette: target->OpenCommandPalette(); break;
    case AppCommand::kDownloads: target->ShowDownloads(); break;
    case AppCommand::kImport: target->RunShortcut("import"); break;
    case AppCommand::kCloseTab: target->CloseActiveTab(); break;
    case AppCommand::kDeveloper: target->RunShortcut("developer"); break;
    case AppCommand::kDevTools: target->RunShortcut("devtools_panel"); break;
    case AppCommand::kDevConsole: target->RunShortcut("devtools_console"); break;
    case AppCommand::kDevInspect: target->RunShortcut("devtools_inspect"); break;
    case AppCommand::kHardReload: target->RunShortcut("hard_reload"); break;
    case AppCommand::kReload: target->RunPageCommand(prototype::PageCommand::kReload); break;
    case AppCommand::kToggleSidebar: shell->ToggleSidebar(); break;
    case AppCommand::kRss: target->RunShortcut("rss"); break;
    case AppCommand::kHistory: target->RunShortcut("history"); break;
    case AppCommand::kTimeline: target->RunShortcut("timeline"); break;
    case AppCommand::kFind: target->RunShortcut("find"); break;
    case AppCommand::kPrint: target->RunShortcut("print"); break;
    case AppCommand::kSettings: shell->OpenGeneralSettings(); break;
    case AppCommand::kWorkspaceWork:
      if (!target->workspaces().empty()) target->Select(target->workspaces()[0].id);
      break;
    case AppCommand::kWorkspacePersonal:
      if (target->workspaces().size() > 1) target->Select(target->workspaces()[1].id);
      break;
  }
}
#endif

void Runtime::RequestQuitAll() {
  if (closing_) return;
  quit_requested_ = true;
  // Each window gets its existing beforeunload check before being released.
  if (!private_windows_.empty()) { private_windows_.back()->RequestClose(); return; }
#if BUILDFLAG(IS_MAC)
  if (ui_lab_) { RequestClose(); return; }
#endif
  if (window_) window_->RequestClose();
}

void Runtime::RequestClose() {
  if (closing_) return;
  if (!quit_requested_ && window_ && window_->widget()->IsVisible() &&
      std::any_of(private_windows_.begin(), private_windows_.end(), [](const auto& w) { return w->widget()->IsVisible(); })) {
    window_->widget()->Hide();
    window_->SetCloseCallback(base::BindOnce(&Runtime::RequestClose, weak_factory_.GetWeakPtr()));
    return;
  }
  closing_ = true;
  // Never destroy a Widget or its close callback on its own event stack.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
      base::BindOnce(&Runtime::Close, weak_factory_.GetWeakPtr()));
}

void Runtime::Close() {
#if BUILDFLAG(IS_MAC)
  if (!ConfirmDownloadShutdown()) {
    closing_ = false;
    if (window_) window_->SetCloseCallback(base::BindOnce(&Runtime::RequestClose, weak_factory_.GetWeakPtr()));
    return;
  }
#else
  // Closing the window quits Zitter: ask before cancelling downloads, as
  // Chrome does for its last window.
  if (const int count = DownloadCoreService::BlockingShutdownCountAllProfiles();
      count && !downloads_confirmed_ && window_) {
    closing_ = false;
    window_->SetCloseCallback(base::BindOnce(&Runtime::RequestClose, weak_factory_.GetWeakPtr()));
    window_->shell()->ConfirmQuitWithDownloads(count, base::BindOnce(&Runtime::DownloadsAnswered, weak_factory_.GetWeakPtr()));
    return;
  }
#endif
  ReleaseProfiles();
  keep_alive_.reset();
  chrome::ExitIgnoreUnloadHandlers();
}

#if !BUILDFLAG(IS_MAC)
void Runtime::DownloadsAnswered(bool quit) {
  if (!quit) return;
  downloads_confirmed_ = true;
  RequestClose();
}
#endif

void Runtime::ReleaseProfiles() {
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
  update_startup_timer_.Stop();
  update_timer_.Stop();
#endif
#if BUILDFLAG(IS_MAC)
  update_loader_.reset();
  update_download_.reset();
  update_checks_.clear();
  SetAppShellDelegate(nullptr);
  ui_lab_.reset();
#endif
  performance_probe_.reset();
  private_windows_.clear();
  if (private_profile_) {
    auto* profile = private_profile_.get();
    private_profile_ = nullptr;
    ProfileDestroyer::DestroyOTRProfileWhenAppropriate(profile);
  }
  private_parent_lease_.reset();
  window_.reset();

}

void Runtime::PostMainMessageLoopRun() {
#if BUILDFLAG(IS_WIN)
  if (updater_) updater_->InstallOnExit();
  updater_.reset();
#endif
  weak_factory_.InvalidateWeakPtrs();
  browsers_.Reset();
  stock_windows_.clear();
  ReleaseProfiles();
  keep_alive_.reset();
}
}  // namespace zitter
