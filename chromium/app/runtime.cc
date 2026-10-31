#include "chromium/app/runtime.h"
#include "chromium/app/performance_probe.h"

#include "base/check.h"
#include "base/command_line.h"
#include "base/functional/bind.h"
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
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/tab_list/tab_list_interface.h"
#include "components/tabs/public/tab_interface.h"
#include "chromium/prototype/workspace_window.h"
#if BUILDFLAG(IS_WIN)
#include "chromium/prototype/jump_list_win.h"
#endif
#include "components/keep_alive_registry/keep_alive_types.h"
#include "components/keep_alive_registry/scoped_keep_alive.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/base_window.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"
#if BUILDFLAG(IS_MAC)
#include "chromium/app/ui_lab.h"
#endif

namespace zitter {
Runtime::Runtime() = default;
Runtime::~Runtime() = default;

void Runtime::PreBrowserStart() {
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
        runtime->RequestClose();
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
  window_->SetReadyCallback(base::BindOnce(&Runtime::WindowReady, weak_factory_.GetWeakPtr()));
}

void Runtime::WindowReady() {
  for (const auto& arg : base::CommandLine::ForCurrentProcess()->GetArgs()) {
#if BUILDFLAG(IS_WIN)
    GURL url(base::WideToUTF8(arg));
#else
    GURL url(arg);
#endif
    if (url.SchemeIsHTTPOrHTTPS()) { window_->OpenExternalUrl(url); break; }
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
  LOG(INFO) << "Zitter ready; persistent data: " << g_browser_process->profile_manager()->user_data_dir();
  if (base::CommandLine::ForCurrentProcess()->HasSwitch("zitter-app-benchmark")) {
    performance_probe_ = std::make_unique<AppPerformanceProbe>(window_.get(),
        base::BindOnce(&Runtime::RequestClose, weak_factory_.GetWeakPtr()));
    performance_probe_->Start();
  }
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
  if (window_ && !closing_) window_->widget()->Activate();
}

#if BUILDFLAG(IS_MAC)
void Runtime::RequestQuit() {
  if (ui_lab_) { RequestClose(); return; }
  if (window_ && !closing_) window_->RequestClose();
}

void Runtime::RaiseWindow() {
  if (ui_lab_) { ui_lab_->Raise(); return; }
  if (window_ && !closing_) window_->widget()->Activate();
}

void Runtime::OpenUrl(const GURL& url) {
  if (ui_lab_) { ui_lab_->Navigate(url); ui_lab_->Raise(); return; }
  if (!window_ || closing_) return;
  window_->OpenExternalUrl(url);
  window_->widget()->Activate();
}

void Runtime::RunCommand(AppCommand command) {
  if (!window_ || closing_) return;
  auto* shell = window_->shell();
  window_->widget()->Activate();
  switch (command) {
    case AppCommand::kNewTab: window_->ClosePeek(); shell->OpenAddress(true); break;
    case AppCommand::kOpenAddress: window_->FocusAddressBar(); break;
    case AppCommand::kCommandPalette: window_->OpenCommandPalette(); break;
    case AppCommand::kDownloads: window_->ShowDownloads(); break;
    case AppCommand::kPasswords: window_->ShowPasswords(); break;
    case AppCommand::kCloseTab: window_->CloseActiveTab(); break;
    case AppCommand::kReload: window_->RunPageCommand(prototype::PageCommand::kReload); break;
    case AppCommand::kToggleSidebar: shell->ToggleSidebar(); break;
    case AppCommand::kHistory: window_->RunShortcut("history"); break;
    case AppCommand::kTimeline: window_->RunShortcut("timeline"); break;
    case AppCommand::kFind: window_->RunShortcut("find"); break;
    case AppCommand::kPrint: window_->RunShortcut("print"); break;
    case AppCommand::kSettings: shell->OpenGeneralSettings(); break;
    case AppCommand::kWorkspaceWork:
      if (!window_->workspaces().empty()) window_->Select(window_->workspaces()[0].id);
      break;
    case AppCommand::kWorkspacePersonal:
      if (window_->workspaces().size() > 1) window_->Select(window_->workspaces()[1].id);
      break;
  }
}
#endif

void Runtime::RequestClose() {
  if (closing_) return;
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
#if BUILDFLAG(IS_MAC)
  SetAppShellDelegate(nullptr);
  ui_lab_.reset();
#endif
  performance_probe_.reset();
  window_.reset();

}

void Runtime::PostMainMessageLoopRun() {
  weak_factory_.InvalidateWeakPtrs();
  browsers_.Reset();
  stock_windows_.clear();
  ReleaseProfiles();
  keep_alive_.reset();
}
}  // namespace zitter
