// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/app/updater_mac.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "base/version.h"
#include "build/build_config.h"
#include "chromium/app/mac_release_metadata.h"
#include "chromium/prototype/i18n.h"
#include "chromium/prototype/workspace_shell.h"
#include "net/base/load_flags.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "net/url_request/redirect_info.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "url/gurl.h"

namespace zitter {

MacUpdater::MacUpdater(
    prototype::WorkspaceShell& shell, std::string version,
    MacReleaseBackend backend, base::FilePath cache,
    Factory factory, base::RepeatingCallback<void(const GURL&)> open_release,
    base::RepeatingCallback<void(const base::FilePath&)> reveal_installer)
    : shell_(shell),
      version_(std::move(version)),
      backend_(backend),
      cache_(std::move(cache)),
      factory_(std::move(factory)),
      open_release_(std::move(open_release)),
      reveal_installer_(std::move(reveal_installer)) {}

MacUpdater::~MacUpdater() = default;

void MacUpdater::SetInstallCallback(
    base::RepeatingCallback<void(const MacInstaller&)> install) {
  install_ = std::move(install);
}

void MacUpdater::Attach() {
  shell_->SetUpdateCheckCallback(
      base::BindRepeating(&MacUpdater::CheckForUpdates, weak_factory_.GetWeakPtr()),
      base::BindRepeating(&MacUpdater::Configure, weak_factory_.GetWeakPtr()));
}

void MacUpdater::CheckForUpdates(CheckDone done) {
  StartMacCheck(std::move(done), false);
}

void MacUpdater::CheckInBackground() {
  if (shell_->appearance_ready())
    StartMacCheck({}, true);
}

void MacUpdater::Configure(bool check, bool download) {
  const bool start_download = download && !auto_download_updates_;
  auto_download_updates_ = download;
  if ((!check || !download) && update_download_ && !installer_requested_) {
    update_download_.reset();
    preparing_mac_installer_ = false;
    shell_->SetUpdateDownloadPending(false);
  }
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
                              &MacUpdater::CheckInBackground);
  update_timer_.Start(FROM_HERE, base::Hours(6), this, &MacUpdater::CheckInBackground);
}

void MacUpdater::StartMacCheck(CheckDone done, bool background) {
  auto factory = factory_.Run();
  if (!factory) {
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
  update_loader_->DownloadToString(
      factory.get(), base::BindOnce(&MacUpdater::MacUpdateChecked, weak_factory_.GetWeakPtr()),
      2 * 1024 * 1024);
}

void MacUpdater::MacUpdateChecked(std::optional<std::string> body) {
  update_loader_.reset();
  const bool background = std::exchange(background_update_check_, false) && update_timer_.IsRunning();
#if defined(ARCH_CPU_ARM64)
  constexpr std::string_view architecture = "arm64";
#else
  constexpr std::string_view architecture = "x86_64";
#endif
  auto [has_stable_release, latest, release_url, installer] =
      SelectMacRelease(body.value_or(""), base::Version(version_), backend_,
                       architecture, cache_);
  std::u16string message = prototype::T(u"Nie udało się sprawdzić aktualizacji.");
  if (latest.IsValid()) {
    if (release_url.is_valid()) {
      message = prototype::TF(u"Dostępna jest wersja {}.",
                              {base::UTF8ToUTF16(latest.GetString())});
    } else {
      message = prototype::T(u"Masz najnowszą wersję Zitter.");
    }
  } else if (has_stable_release) {
    message = prototype::T(u"Najnowsze wydanie nie ma instalatora dla tego Maca.");
  }
  const bool manual = !update_checks_.empty();
  const bool has_in_app_installer = install_ && installer.has_value();
  if ((background || manual) && has_in_app_installer &&
      downloaded_mac_version_ != installer->version && !preparing_mac_installer_) {
    auto download = base::BindRepeating(
        &MacUpdater::PrepareMacInstaller, weak_factory_.GetWeakPtr(), *installer, true);
    shell_->ShowUpdateOffer(installer->version, message,
                           prototype::T(u"Pobierz aktualizację"), std::move(download));
  } else if (background && release_url.is_valid() &&
             downloaded_mac_version_ != latest.GetString() && !preparing_mac_installer_) {
    ShowReleaseOffer(latest.GetString(), message, release_url);
  }
  if (auto_download_updates_ && (background || manual) &&
      installer && !preparing_mac_installer_ &&
      downloaded_mac_version_ != installer->version) {
    PrepareMacInstaller(std::move(*installer));
  }
  for (auto& done : std::exchange(update_checks_, {}))
    std::move(done).Run(message, has_in_app_installer ? GURL() : release_url);
}

void MacUpdater::PrepareMacInstaller(MacInstaller installer, bool requested) {
  if (preparing_mac_installer_)
    return;
  installer_requested_ = requested;
  preparing_mac_installer_ = true;
  shell_->SetUpdateDownloadPending(true);
  auto verify = base::BindOnce(&VerifyMacInstaller, installer.path, installer.size, installer.digest);
  base::ThreadPool::PostTaskAndReplyWithResult(FROM_HERE,
      {base::MayBlock(), base::TaskPriority::BEST_EFFORT}, std::move(verify),
      base::BindOnce(&MacUpdater::DownloadMacInstaller, weak_factory_.GetWeakPtr(), std::move(installer)));
}

void MacUpdater::DownloadMacInstaller(MacInstaller installer, bool already_cached) {
  auto factory = factory_.Run();
  if (already_cached || (!auto_download_updates_ && !installer_requested_) || !factory) {
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
      trigger: "An update is found while automatic downloads are enabled, or the user requests it."
      data: "The public release asset URL, without cookies or authentication."
      destination: WEBSITE
    }
    policy {
      cookies_allowed: NO
      setting: "Automatic downloads are opt-in in Zitter Preferences."
      policy_exception_justification: "The user initiated or enabled downloads."
    })");
  update_download_ = network::SimpleURLLoader::Create(std::move(request), annotation);
  update_download_->SetTimeoutDuration(base::Minutes(15));
  update_download_->SetOnRedirectCallback(base::BindRepeating(
      [](base::WeakPtr<MacUpdater> runtime, MacInstaller installer, const GURL&,
         const net::RedirectInfo& redirect, const network::mojom::URLResponseHead&,
         std::vector<std::string>*) {
        if (IsAllowedMacInstallerRedirect(redirect.new_url))
          return;
        if (runtime) {
          runtime->update_download_.reset();
          runtime->MacInstallerReady(std::move(installer), false);
        }
      }, weak_factory_.GetWeakPtr(), installer));
  const auto size = installer.size;
  update_download_->DownloadToTempFile(factory.get(),
      base::BindOnce(&MacUpdater::MacInstallerDownloaded, weak_factory_.GetWeakPtr(), std::move(installer)),
      size);
}

void MacUpdater::MacInstallerDownloaded(MacInstaller installer, base::FilePath temporary) {
  update_download_.reset();
  auto verify = base::BindOnce(&CacheMacInstaller, std::move(temporary), installer);
  base::ThreadPool::PostTaskAndReplyWithResult(FROM_HERE,
      {base::MayBlock(), base::TaskPriority::BEST_EFFORT, base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
      std::move(verify),
      base::BindOnce(&MacUpdater::MacInstallerReady, weak_factory_.GetWeakPtr(), std::move(installer)));
}

void MacUpdater::MacInstallerReady(MacInstaller installer, bool verified) {
  preparing_mac_installer_ = false;
  const bool requested = std::exchange(installer_requested_, false);
  if (!verified || (!auto_download_updates_ && !requested)) {
    if (requested)
      ShowReleaseOffer(installer.version,
                       prototype::T(u"Nie udało się zweryfikować pobranego instalatora."),
                       GURL("https://github.com/doctorspider42/zitter/releases/tag/v" + installer.version));
    else
      shell_->SetUpdateDownloadPending(false);
    return;
  }
  downloaded_mac_version_ = installer.version;
  auto reveal = base::BindRepeating(&MacUpdater::RevealMacInstaller,
                                   weak_factory_.GetWeakPtr(), std::move(installer));
  if (install_) {
    shell_->ShowUpdateOffer(
        downloaded_mac_version_,
        prototype::TF(u"Pobrano instalator wersji {}.", {base::UTF8ToUTF16(downloaded_mac_version_)}),
        prototype::T(u"Zainstaluj i uruchom ponownie"), std::move(reveal));
  } else {
    shell_->ShowUpdateReady(downloaded_mac_version_, std::move(reveal));
  }
}

void MacUpdater::RevealMacInstaller(const MacInstaller& installer) {
  if (rechecking_mac_installer_ || downloaded_mac_version_ != installer.version)
    return;
  rechecking_mac_installer_ = true;
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&VerifyMacInstaller, installer.path, installer.size, installer.digest),
      base::BindOnce(&MacUpdater::MacInstallerRechecked,
                     weak_factory_.GetWeakPtr(), installer));
}

void MacUpdater::MacInstallerRechecked(MacInstaller installer, bool verified) {
  rechecking_mac_installer_ = false;
  if (downloaded_mac_version_ != installer.version)
    return;
  if (verified) {
    if (install_)
      install_.Run(installer);
    else
      reveal_installer_.Run(installer.path);
    return;
  }
  downloaded_mac_version_.clear();
  ShowReleaseOffer(
      installer.version,
      prototype::T(u"Nie udało się zweryfikować pobranego instalatora."),
      GURL("https://github.com/doctorspider42/zitter/releases/tag/v" + installer.version));
}

void MacUpdater::ShowReleaseOffer(const std::string& version,
                                  std::u16string message,
                                  const GURL& release_url) {
  shell_->ShowUpdateOffer(
      version, std::move(message), prototype::T(u"Otwórz stronę wydania"),
      base::BindRepeating(
          [](base::WeakPtr<MacUpdater> updater, const GURL& url) {
            if (!updater)
              return;
            updater->shell_->CloseOverlays();
            updater->open_release_.Run(url);
          },
          weak_factory_.GetWeakPtr(), release_url));
}

}  // namespace zitter
