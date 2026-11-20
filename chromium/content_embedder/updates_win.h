// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// Automatic updates for an installed Windows Content build: WindowsUpdater
// (chromium/app/updater_win.cc) behind Settings and the sidebar update card.

#ifndef ZITTER_CONTENT_UPDATES_WIN_H_
#define ZITTER_CONTENT_UPDATES_WIN_H_

#include <memory>
#include <string>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "chromium/prototype/workspace_shell.h"

namespace zitter {
class WindowsUpdater;
}

namespace zitter::content_embedder {

class WindowsContentUpdates final {
 public:
  WindowsContentUpdates(prototype::WorkspaceShell& shell,
                        const base::FilePath& workspace_root,
                        base::RepeatingClosure request_close);
  ~WindowsContentUpdates();

  // False for portable and development copies, which never update.
  bool Attach();
  // Runs the downloaded installer after the browser exits, when the user
  // chose to install it.
  void InstallOnExit();

 private:
  using UpdateCheckDone = prototype::WorkspaceShell::UpdateCheckDone;
  void Configure(bool check, bool download);
  void CheckInBackground();
  void Check(bool download, UpdateCheckDone done, std::string requested_version = {});
  void StartDownload(std::string version);
  void ReportDownloadProgress(const std::string& version, double progress);

  const raw_ref<prototype::WorkspaceShell> shell_;
  std::unique_ptr<WindowsUpdater> updater_;
  base::RepeatingClosure request_close_;
  base::OneShotTimer startup_timer_;
  base::RepeatingTimer timer_;
  bool auto_download_ = false;
  base::WeakPtrFactory<WindowsContentUpdates> weak_factory_{this};
};

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_UPDATES_WIN_H_
