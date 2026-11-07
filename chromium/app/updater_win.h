// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_APP_UPDATER_WIN_H_
#define ZITTER_CHROMIUM_APP_UPDATER_WIN_H_

#include <optional>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"

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
enum class CheckStatus { kReady, kAvailable, kNoUpdate, kFailed };
struct CheckResult {
  CheckStatus status;
  std::optional<Release> release = std::nullopt;
};
// Only a newer, stable release's exact Windows installer, with a GitHub digest.
std::optional<Release> ParseRelease(std::string_view json,
                                    std::string_view current_version);
bool VerifyInstaller(const base::FilePath& path, const Release& release);
CheckResult DownloadLatest(const base::FilePath& cache,
                           std::string current_version);
}  // namespace updates

// Runs only for this executable's registered installation, never portable/dev
// builds. Downloads in the background; installs on exit after an explicit click.
class WindowsUpdater {
 public:
  using CheckedCallback = base::OnceCallback<void(const updates::CheckResult&)>;
  explicit WindowsUpdater(const base::FilePath& user_data);
  ~WindowsUpdater();
  bool Start();
  void CheckNow(bool download, CheckedCallback done);
  void RequestInstall();
  void InstallOnExit();

 private:
  void Check(bool download);
  void Checked(updates::CheckResult result);
  base::FilePath directory_;
  base::FilePath cache_;
  bool busy_ = false;
  bool install_requested_ = false;
  bool download_requested_ = false;
  std::vector<CheckedCallback> checks_;
  std::optional<updates::Release> pending_;
  base::WeakPtrFactory<WindowsUpdater> weak_factory_{this};
};
}  // namespace zitter
#endif
