#ifndef ZITTER_CHROMIUM_APP_RUNTIME_H_
#define ZITTER_CHROMIUM_APP_RUNTIME_H_

#include <memory>

#include "base/auto_reset.h"
#include "base/containers/flat_set.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "build/build_config.h"
#include "chrome/browser/chrome_browser_main_extra_parts.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#if BUILDFLAG(IS_MAC)
#include "chromium/app/app_shell_mac.h"
#endif

class Profile;
class ScopedKeepAlive;
class ScopedProfileKeepAlive;
class BrowserCollection;
namespace zitter::prototype { class WorkspaceWindow; }

namespace zitter {
class AppPerformanceProbe;
class UiLab;
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
  void RequestClose();
  void Close();
  void ReleaseProfiles();
  std::unique_ptr<ScopedKeepAlive> keep_alive_;
  std::unique_ptr<prototype::WorkspaceWindow> window_;
  std::unique_ptr<AppPerformanceProbe> performance_probe_;
  std::unique_ptr<base::AutoReset<bool>> extension_install_ui_;
#if BUILDFLAG(IS_MAC)
  std::unique_ptr<UiLab> ui_lab_;
#endif
  base::ScopedObservation<BrowserCollection, BrowserCollectionObserver> browsers_{this};
  // Stock windows awaiting closure; dropped if they close first.
  base::flat_set<BrowserWindowInterface*> stock_windows_;
  bool closing_ = false;
  base::WeakPtrFactory<Runtime> weak_factory_{this};
};
}  // namespace zitter
#endif
