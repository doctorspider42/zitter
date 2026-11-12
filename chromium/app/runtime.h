#ifndef ZITTER_CHROMIUM_APP_RUNTIME_H_
#define ZITTER_CHROMIUM_APP_RUNTIME_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "base/memory/raw_ptr.h"

#include "base/containers/flat_set.h"
#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "build/build_config.h"
#include "chrome/browser/chrome_browser_main_extra_parts.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "url/gurl.h"
#if BUILDFLAG(IS_MAC)
#include "chromium/app/app_shell_mac.h"
#endif

class Profile;
class ScopedKeepAlive;
class ScopedProfileKeepAlive;
class BrowserCollection;
namespace base { class FilePath; }
namespace zitter::prototype { class WorkspaceWindow; }

namespace zitter {
class AppPerformanceProbe;
class UiLab;
class WindowsUpdater;
class MacUpdater;
// Lives inside Chromium's regular browser startup/shutdown, never a test runner.
// On macOS the app shell keeps Chrome's app-level entry points (Dock, menu,
// links from other apps) away from stock Browser windows. Should an unknown
// upstream path still create one, Runtime closes it and logs the path.
class Runtime final : public ChromeBrowserMainExtraParts,
#if BUILDFLAG(IS_MAC)
                      public AppShellDelegate,
#endif
                      public BrowserCollectionObserver {
 public:
  Runtime();
  ~Runtime() override;
  void PreProfileInit() override;
  void PreBrowserStart() override;
  void PreMainMessageLoopRun() override;
  void PostMainMessageLoopRun() override;
  void OnBrowserCreated(BrowserWindowInterface* browser) override;
  void OnBrowserClosed(BrowserWindowInterface* browser) override;
#if BUILDFLAG(IS_MAC)
  void RaiseWindow() override;
  void RequestQuit() override;
  void OpenUrl(const GURL& url) override;
  void RunCommand(AppCommand command) override;
#endif
 private:
  void ReplaceStockWindow(BrowserWindowInterface* browser);
  void WindowReady();
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
  using UpdateCheckDone =
      base::OnceCallback<void(std::u16string, GURL)>;
  void CheckForUpdates(UpdateCheckDone done);
#endif
#if BUILDFLAG(IS_WIN)
  void ConfigureUpdates(bool check, bool download);
  void CheckInBackground();
  base::OneShotTimer update_startup_timer_;
  base::RepeatingTimer update_timer_;
  bool auto_download_updates_ = false;
  void CheckWindowsUpdate(bool download, UpdateCheckDone done);
#endif
  void OpenPrivateWindow();
  void ClosePrivateWindow(prototype::WorkspaceWindow* window);
  prototype::WorkspaceWindow* ActiveWindow() const;
  void RequestClose();
  void RequestQuitAll();
  void Close();
#if !BUILDFLAG(IS_MAC)
  // Answer to the in-progress downloads prompt; quitting resumes Close().
  void DownloadsAnswered(bool quit);
#endif
  void ReleaseProfiles();
  std::unique_ptr<ScopedKeepAlive> keep_alive_;
  std::unique_ptr<prototype::WorkspaceWindow> window_;
  std::vector<std::unique_ptr<prototype::WorkspaceWindow>> private_windows_;
  raw_ptr<Profile> private_profile_ = nullptr;
  std::unique_ptr<ScopedProfileKeepAlive> private_parent_lease_;
  std::unique_ptr<AppPerformanceProbe> performance_probe_;
#if BUILDFLAG(IS_WIN)
  std::unique_ptr<WindowsUpdater> updater_;
#endif
#if BUILDFLAG(IS_MAC)
  std::unique_ptr<UiLab> ui_lab_;
  std::unique_ptr<MacUpdater> mac_updater_;
#endif
  base::ScopedObservation<BrowserCollection, BrowserCollectionObserver> browsers_{this};
  // Stock windows awaiting closure; dropped if they close first.
  base::flat_set<BrowserWindowInterface*> stock_windows_;
  bool closing_ = false;
  bool quit_requested_ = false;
#if !BUILDFLAG(IS_MAC)
  bool downloads_confirmed_ = false;
  base::CallbackListSubscription closing_all_browsers_;
#endif
  base::WeakPtrFactory<Runtime> weak_factory_{this};
};
}  // namespace zitter
#endif
