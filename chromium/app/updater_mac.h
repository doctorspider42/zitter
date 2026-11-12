// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_APP_UPDATER_MAC_H_
#define ZITTER_CHROMIUM_APP_UPDATER_MAC_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ref.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"

class GURL;
namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}
namespace zitter::prototype { class WorkspaceShell; }

namespace zitter {

struct MacInstaller;
enum class MacReleaseBackend;

class MacUpdater {
 public:
  using CheckDone = base::OnceCallback<void(std::u16string, GURL)>;
  using Factory = base::RepeatingCallback<scoped_refptr<network::SharedURLLoaderFactory>()>;

  MacUpdater(prototype::WorkspaceShell& shell, std::string version,
             MacReleaseBackend backend, base::FilePath cache, Factory factory,
             base::RepeatingCallback<void(const GURL&)> open_release,
             base::RepeatingCallback<void(const base::FilePath&)> reveal_installer);
  ~MacUpdater();

  void CheckForUpdates(CheckDone done);
  void Attach();

 private:
  void Configure(bool check, bool download);
  void CheckInBackground();
  void StartMacCheck(CheckDone done, bool background);
  void MacUpdateChecked(std::optional<std::string> body);
  void PrepareMacInstaller(MacInstaller installer);
  void DownloadMacInstaller(MacInstaller installer, bool already_cached);
  void MacInstallerDownloaded(MacInstaller installer, base::FilePath temporary);
  void MacInstallerReady(MacInstaller installer, bool verified);
  void RevealMacInstaller(const MacInstaller& installer);
  void MacInstallerRechecked(MacInstaller installer, bool verified);
  void ShowReleaseOffer(const std::string& version,
                        std::u16string message,
                        const GURL& release_url);

  const raw_ref<prototype::WorkspaceShell> shell_;
  const std::string version_;
  const MacReleaseBackend backend_;
  const base::FilePath cache_;
  const Factory factory_;
  const base::RepeatingCallback<void(const GURL&)> open_release_;
  const base::RepeatingCallback<void(const base::FilePath&)> reveal_installer_;
  base::OneShotTimer update_startup_timer_;
  base::RepeatingTimer update_timer_;
  std::unique_ptr<network::SimpleURLLoader> update_loader_;
  std::vector<CheckDone> update_checks_;
  std::unique_ptr<network::SimpleURLLoader> update_download_;
  bool auto_download_updates_ = false;
  bool background_update_check_ = false;
  bool preparing_mac_installer_ = false;
  bool rechecking_mac_installer_ = false;
  std::string downloaded_mac_version_;
  base::WeakPtrFactory<MacUpdater> weak_factory_{this};
};

}  // namespace zitter
#endif
