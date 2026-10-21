#ifndef ZITTER_CHROMIUM_APP_APP_SHELL_MAC_H_
#define ZITTER_CHROMIUM_APP_APP_SHELL_MAC_H_

class GURL;

namespace zitter {

enum class AppCommand {
  kNewTab = 1,
  kOpenAddress,
  kCommandPalette,
  kPasswords,
  kDownloads,
  kCloseTab,
  kReload,
  kToggleSidebar,
  kSettings,
  kWorkspaceWork,
  kWorkspacePersonal,
};

class AppShellDelegate {
 public:
  virtual void RaiseWindow() = 0;
  virtual void RequestQuit() = 0;
  virtual void OpenUrl(const GURL& url) = 0;
  virtual void RunCommand(AppCommand command) = 0;

 protected:
  virtual ~AppShellDelegate() = default;
};

// Makes Zitter, not Chrome, the owner of macOS app-level entry points: Dock
// reopen and Dock menu, URLs from other apps, Handoff and the main menu. None
// of them can open a stock Chrome Browser window. Call once, before the main
// message loop runs; URLs that arrive before a delegate is set are queued.
void InstallAppShell();
// Reuse upstream native in-progress download consent before destroying Zitter.
bool ConfirmDownloadShutdown();
// Sets (or, with nullptr, clears) the receiver. Setting one flushes queued
// URLs and installs Zitter's main menu.
void SetAppShellDelegate(AppShellDelegate* delegate);

}  // namespace zitter

#endif
