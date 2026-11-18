// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "updates_win.h"

#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "chromium/app/updater_win.h"
#include "chromium/prototype/i18n.h"
#include "url/gurl.h"

namespace zitter::content_embedder {

WindowsContentUpdates::WindowsContentUpdates(prototype::WorkspaceShell& shell,
                                             const base::FilePath& workspace_root,
                                             base::RepeatingClosure request_close)
    : shell_(shell),
      request_close_(std::move(request_close)) {
  updater_ = std::make_unique<WindowsUpdater>(
      workspace_root, base::BindRepeating(&WindowsContentUpdates::ReportDownloadProgress,
                                         weak_factory_.GetWeakPtr()));
}

WindowsContentUpdates::~WindowsContentUpdates() = default;

bool WindowsContentUpdates::Attach() {
  if (!updater_->Start())
    return false;
  shell_->SetUpdateCheckCallback(
      base::BindRepeating(
          [](base::WeakPtr<WindowsContentUpdates> self, UpdateCheckDone done) {
            if (self)
              self->Check(self->auto_download_, std::move(done));
          },
          weak_factory_.GetWeakPtr()),
      base::BindRepeating(&WindowsContentUpdates::Configure,
                          weak_factory_.GetWeakPtr()));
  return true;
}

void WindowsContentUpdates::InstallOnExit() {
  updater_->InstallOnExit();
}

void WindowsContentUpdates::Configure(bool check, bool download) {
  const bool start_download = download && !auto_download_;
  auto_download_ = download;
  if (!check) {
    startup_timer_.Stop();
    timer_.Stop();
    return;
  }
  if (timer_.IsRunning()) {
    if (start_download && !startup_timer_.IsRunning())
      CheckInBackground();
    return;
  }
  startup_timer_.Start(FROM_HERE, base::Seconds(30), this,
                       &WindowsContentUpdates::CheckInBackground);
  timer_.Start(FROM_HERE, base::Hours(6), this,
               &WindowsContentUpdates::CheckInBackground);
}

void WindowsContentUpdates::CheckInBackground() {
  Check(auto_download_, {});
}

void WindowsContentUpdates::StartDownload(std::string version) {
  shell_->SetUpdateDownloadPending(true, version);
  Check(true, {}, std::move(version));
}

void WindowsContentUpdates::ReportDownloadProgress(const std::string& version,
                                                    double progress) {
  shell_->SetUpdateDownloadPending(true, version);
  shell_->SetUpdateDownloadProgress(progress);
}

void WindowsContentUpdates::Check(bool download, UpdateCheckDone done,
                                 std::string requested_version) {
  updater_->CheckNow(download, base::BindOnce(
      [](base::WeakPtr<WindowsContentUpdates> self, UpdateCheckDone done,
         std::string requested_version,
         const updates::CheckResult& result) {
        if (!self)
          return;
        auto& shell = *self->shell_;
        shell.SetUpdateDownloadPending(false);
        const bool requested = !requested_version.empty();
        std::u16string message;
        switch (result.status) {
          case updates::CheckStatus::kReady:
            if (!done && !requested && !self->timer_.IsRunning())
              break;
            shell.ShowUpdateReady(
                result.release->version,
                base::BindRepeating(
                    [](base::WeakPtr<WindowsContentUpdates> self) {
                      if (!self)
                        return;
                      self->updater_->RequestInstall();
                      self->request_close_.Run();
                    },
                    self->weak_factory_.GetWeakPtr()),
                false);
            break;
          case updates::CheckStatus::kAvailable:
            if (done || self->timer_.IsRunning()) {
              const auto& version = result.release->version;
              shell.ShowUpdateOffer(
                  version,
                  prototype::TF(u"Dostępna jest wersja {}.", {base::UTF8ToUTF16(version)}),
                  prototype::T(u"Pobierz aktualizację"),
                  base::BindRepeating(&WindowsContentUpdates::StartDownload,
                                      self, version));
            }
            break;
          case updates::CheckStatus::kNoUpdate:
            message = prototype::T(u"Masz najnowszą dostępną wersję Zitter dla Windows.");
            if (requested)
              shell.ShowUpdateOffer({}, {}, {}, {});
            break;
          case updates::CheckStatus::kFailed:
            message = prototype::T(u"Nie udało się sprawdzić lub pobrać aktualizacji.");
            if (requested || (result.release && (done || self->timer_.IsRunning()))) {
              const auto& version = result.release ? result.release->version : requested_version;
              shell.ShowUpdateOffer(
                  version, prototype::T(u"Nie udało się pobrać aktualizacji."),
                  prototype::T(u"Ponów pobieranie"),
                  base::BindRepeating(&WindowsContentUpdates::StartDownload, self, version));
            }
            break;
        }
        if (done)
          std::move(done).Run(std::move(message), GURL());
      },
      weak_factory_.GetWeakPtr(), std::move(done), std::move(requested_version)));
}

}  // namespace zitter::content_embedder
