// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_APP_UPDATER_WIN_H_
#define ZITTER_CHROMIUM_APP_UPDATER_WIN_H_

#include <optional>
#include <cstdint>
#include <string>
#include <string_view>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"

namespace zitter {
// Recognized before Chromium starts; waits for the browser and verifies the
// staged installer again in a short-lived process without a UI/message loop.
std::optional<int> RunWindowsUpdateHelper();
namespace updates {
struct Release {
  std::string version;
  std::string url;
  std::string sha256;
  int64_t size = 0;
};
// Only a newer, stable release's exact Windows installer, with a GitHub digest.
std::optional<Release> ParseRelease(std::string_view json,
                                    std::string_view current_version);
bool VerifyInstaller(const base::FilePath& path, const Release& release);
std::optional<Release> DownloadLatest(const base::FilePath& cache,
                                    std::string current_version);
}  // namespace updates

// Runs only for this executable's registered installation, never portable/dev
// builds. Downloads in the background; installs on exit after an explicit click.
class WindowsUpdater {
 public:
  using Ready = base::RepeatingCallback<void(std::string)>;
  WindowsUpdater(const base::FilePath& user_data, Ready ready);
  ~WindowsUpdater();
  void Start();
  void RequestInstall();
  void InstallOnExit();

 private:
  void Check();
  void Checked(std::optional<updates::Release> release);
  base::FilePath directory_;
  base::FilePath cache_;
  bool busy_ = false;
  bool install_requested_ = false;
  Ready ready_;
  std::optional<updates::Release> pending_;
  base::RepeatingTimer timer_;
  base::WeakPtrFactory<WindowsUpdater> weak_factory_{this};
};
}  // namespace zitter
#endif
