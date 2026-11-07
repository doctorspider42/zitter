#include "chromium/app/runtime.h"
#include <algorithm>
#include "chromium/app/performance_probe.h"
#include "chromium/app/startup_timing.h"

#include "base/check.h"
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
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
#include "chrome/browser/profiles/profile_destroyer.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/tab_list/tab_list_interface.h"
#include "components/tabs/public/tab_interface.h"
#include "chromium/prototype/workspace_window.h"
#if BUILDFLAG(IS_WIN)
#include "chromium/prototype/jump_list_win.h"
#include "chromium/app/updater_win.h"
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

void Runtime::PreProfileInit() {
  MarkStartup("pre_profile_init");
}

void Runtime::PreBrowserStart() {
  MarkStartup("pre_browser_start");
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
  MarkStartup("pre_main_loop");
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
        runtime->RequestQuitAll();
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
  window_->SetNewPrivateWindowCallback(base::BindRepeating(&Runtime::OpenPrivateWindow, weak_factory_.GetWeakPtr()));
  window_->SetReadyCallback(base::BindOnce(&Runtime::WindowReady, weak_factory_.GetWeakPtr()));
}

void Runtime::WindowReady() {
  MarkStartup("workspace_restored");
#if BUILDFLAG(IS_WIN)
  updater_ = std::make_unique<WindowsUpdater>(g_browser_process->profile_manager()->user_data_dir(),
      base::BindRepeating([](base::WeakPtr<Runtime> runtime, std::string version) {
        if (!runtime || !runtime->window_ || runtime->closing_) return;
        runtime->window_->shell()->ShowUpdateReady(std::move(version), base::BindRepeating(
            [](base::WeakPtr<Runtime> runtime) {
              if (!runtime || runtime->closing_ || !runtime->updater_) return;
              runtime->updater_->RequestInstall();
              runtime->RequestClose();
            }, runtime->weak_factory_.GetWeakPtr()));
      }, weak_factory_.GetWeakPtr()));
  updater_->Start();
#endif
  for (const auto& arg : base::CommandLine::ForCurrentProcess()->GetArgs()) {
#if BUILDFLAG(IS_WIN)
    GURL url(base::WideToUTF8(arg));
#else
    GURL url(arg);
#endif
    if (url.SchemeIsHTTPOrHTTPS()) window_->OpenExternalUrl(url);
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
  MarkStartup("workspace_ready");
  LOG(INFO) << "Zitter ready; persistent data: " << g_browser_process->profile_manager()->user_data_dir();
  if (base::CommandLine::ForCurrentProcess()->HasSwitch("zitter-app-benchmark")) {
    performance_probe_ = std::make_unique<AppPerformanceProbe>(window_.get(),
        base::BindOnce(&Runtime::RequestClose, weak_factory_.GetWeakPtr()));
    performance_probe_->Start();
  }
}

prototype::WorkspaceWindow* Runtime::ActiveWindow() const {
  for (const auto& window : private_windows_) {
    if (window->widget()->IsActive()) return window.get();
    for (const auto& owned : views::Widget::GetAllOwnedWidgets(window->widget()->GetNativeView()))
      if (owned->IsActive()) return window.get();
  }
  return window_.get();
}

void Runtime::OpenPrivateWindow() {
  if (closing_ || !window_ || !window_->configuration_ready()) return;
  if (!private_profile_) {
    auto* parent = window_->active_profile();
    if (!parent) return;
    parent = parent->GetOriginalProfile();
    private_parent_lease_ = ScopedProfileKeepAlive::TryAcquire(parent, ProfileKeepAliveOrigin::kBrowserWindow);
    if (!private_parent_lease_) return;
    // A unique OTR context disables extensions and prevents a new session from
    // reusing a context whose renderer shutdown is still pending.
    private_profile_ = parent->GetOffTheRecordProfile(Profile::OTRProfileID::CreateUnique("ZitterPrivate"), true);
  }
  auto window = std::make_unique<prototype::WorkspaceWindow>(
      g_browser_process->profile_manager()->user_data_dir(), false, private_profile_);
  auto* pointer = window.get();
  window->SetNewPrivateWindowCallback(base::BindRepeating(&Runtime::OpenPrivateWindow, weak_factory_.GetWeakPtr()));
  window->SetCloseCallback(base::BindOnce([](base::WeakPtr<Runtime> runtime, prototype::WorkspaceWindow* window) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
        base::BindOnce(&Runtime::ClosePrivateWindow, runtime, window));
  }, weak_factory_.GetWeakPtr(), pointer));
  private_windows_.push_back(std::move(window));
  pointer->widget()->Activate();
  pointer->FocusAddressBar();
}

void Runtime::ClosePrivateWindow(prototype::WorkspaceWindow* window) {
  std::erase_if(private_windows_, [window](const auto& entry) { return entry.get() == window; });
  if (!private_windows_.empty()) { if (quit_requested_) RequestQuitAll(); return; }
  if (private_profile_) {
    auto* profile = private_profile_.get();
    private_profile_ = nullptr;
    ProfileDestroyer::DestroyOTRProfileWhenAppropriate(profile);
    private_parent_lease_.reset();
  }
  if (quit_requested_) { RequestQuitAll(); return; }
  if (window_ && !window_->widget()->IsVisible()) RequestClose();
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
  if (browser->GetProfile()->IsOffTheRecord()) {
    OpenPrivateWindow();
    if (!private_windows_.empty()) {
      if (auto* tabs = TabListInterface::From(browser))
        for (int i = 0; i < tabs->GetTabCount(); ++i) {
          const auto url = tabs->GetTab(i)->GetContents()->GetVisibleURL();
          if (url.SchemeIsHTTPOrHTTPS()) private_windows_.back()->NavigateFromBar(url, true);
        }
    }
    if (auto* window = browser->GetWindow()) window->Close();
    return;
  }
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
  if (window_ && !closing_) { window_->widget()->Show(); window_->widget()->Activate(); }
}

#if BUILDFLAG(IS_MAC)
void Runtime::RequestQuit() { RequestQuitAll(); }

void Runtime::RaiseWindow() {
  if (ui_lab_) { ui_lab_->Raise(); return; }
  if (window_ && !closing_) { window_->widget()->Show(); window_->widget()->Activate(); }
}

void Runtime::OpenUrl(const GURL& url) {
  if (ui_lab_) { ui_lab_->Navigate(url); ui_lab_->Raise(); return; }
  if (!window_ || closing_) return;
  window_->OpenExternalUrl(url);
  window_->widget()->Activate();
}

void Runtime::RunCommand(AppCommand command) {
  if (!window_ || closing_) return;
  auto* target = ActiveWindow();
  auto* shell = target->shell();
  target->widget()->Activate();
  switch (command) {
    case AppCommand::kNewPrivateWindow: OpenPrivateWindow(); break;
    case AppCommand::kNewTab: target->ClosePeek(); shell->OpenAddress(true); break;
    case AppCommand::kOpenAddress: target->FocusAddressBar(); break;
    case AppCommand::kCommandPalette: target->OpenCommandPalette(); break;
    case AppCommand::kDownloads: target->ShowDownloads(); break;
    case AppCommand::kImport: target->RunShortcut("import"); break;
    case AppCommand::kCloseTab: target->CloseActiveTab(); break;
    case AppCommand::kDeveloper: target->RunShortcut("developer"); break;
    case AppCommand::kDevTools: target->RunShortcut("devtools_panel"); break;
    case AppCommand::kDevConsole: target->RunShortcut("devtools_console"); break;
    case AppCommand::kDevInspect: target->RunShortcut("devtools_inspect"); break;
    case AppCommand::kHardReload: target->RunShortcut("hard_reload"); break;
    case AppCommand::kReload: target->RunPageCommand(prototype::PageCommand::kReload); break;
    case AppCommand::kToggleSidebar: shell->ToggleSidebar(); break;
    case AppCommand::kRss: target->RunShortcut("rss"); break;
    case AppCommand::kHistory: target->RunShortcut("history"); break;
    case AppCommand::kTimeline: target->RunShortcut("timeline"); break;
    case AppCommand::kFind: target->RunShortcut("find"); break;
    case AppCommand::kPrint: target->RunShortcut("print"); break;
    case AppCommand::kSettings: shell->OpenGeneralSettings(); break;
    case AppCommand::kWorkspaceWork:
      if (!target->workspaces().empty()) target->Select(target->workspaces()[0].id);
      break;
    case AppCommand::kWorkspacePersonal:
      if (target->workspaces().size() > 1) target->Select(target->workspaces()[1].id);
      break;
  }
}
#endif

void Runtime::RequestQuitAll() {
  if (closing_) return;
  quit_requested_ = true;
  // Each window gets its existing beforeunload check before being released.
  if (!private_windows_.empty()) { private_windows_.back()->RequestClose(); return; }
#if BUILDFLAG(IS_MAC)
  if (ui_lab_) { RequestClose(); return; }
#endif
  if (window_) window_->RequestClose();
}

void Runtime::RequestClose() {
  if (closing_) return;
  if (!quit_requested_ && window_ && window_->widget()->IsVisible() &&
      std::any_of(private_windows_.begin(), private_windows_.end(), [](const auto& w) { return w->widget()->IsVisible(); })) {
    window_->widget()->Hide();
    window_->SetCloseCallback(base::BindOnce(&Runtime::RequestClose, weak_factory_.GetWeakPtr()));
    return;
  }
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
  private_windows_.clear();
  if (private_profile_) {
    auto* profile = private_profile_.get();
    private_profile_ = nullptr;
    ProfileDestroyer::DestroyOTRProfileWhenAppropriate(profile);
  }
  private_parent_lease_.reset();
  window_.reset();

}

void Runtime::PostMainMessageLoopRun() {
#if BUILDFLAG(IS_WIN)
  if (updater_) updater_->InstallOnExit();
  updater_.reset();
#endif
  weak_factory_.InvalidateWeakPtrs();
  browsers_.Reset();
  stock_windows_.clear();
  ReleaseProfiles();
  keep_alive_.reset();
}
}  // namespace zitter
