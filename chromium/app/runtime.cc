#include "chromium/app/runtime.h"

#include "base/check.h"
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chromium/prototype/workspace_window.h"
#include "components/keep_alive_registry/keep_alive_types.h"
#include "components/keep_alive_registry/scoped_keep_alive.h"
#include "content/public/browser/storage_partition.h"
#include "ui/base/base_window.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace zitter {
Runtime::Runtime() = default;
Runtime::~Runtime() = default;

void Runtime::PreBrowserStart() {
  keep_alive_ = std::make_unique<ScopedKeepAlive>(
      KeepAliveOrigin::BROWSER, KeepAliveRestartOption::DISABLED);
}

void Runtime::PreMainMessageLoopRun() {
#if BUILDFLAG(IS_MAC)
  InstallAppShell();
#endif
  browsers_.Observe(GlobalBrowserCollection::GetInstance());
  auto* manager = g_browser_process->profile_manager();
  manager->CreateProfileAsync(manager->user_data_dir().AppendASCII("ZitterWork"),
      base::BindOnce(&Runtime::WorkReady, weak_factory_.GetWeakPtr()));
}

void Runtime::WorkReady(Profile* profile) {
  if (!profile) { LOG(ERROR) << "Cannot load ZitterWork"; RequestClose(); return; }
  work_ = ScopedProfileKeepAlive::TryAcquire(profile, ProfileKeepAliveOrigin::kBrowserWindow);
  CHECK(work_);
  auto* manager = g_browser_process->profile_manager();
  manager->CreateProfileAsync(manager->user_data_dir().AppendASCII("ZitterPersonal"),
      base::BindOnce(&Runtime::PersonalReady, weak_factory_.GetWeakPtr()));
}

void Runtime::PersonalReady(Profile* profile) {
  if (!profile) { LOG(ERROR) << "Cannot load ZitterPersonal"; RequestClose(); return; }
  personal_ = ScopedProfileKeepAlive::TryAcquire(profile, ProfileKeepAliveOrigin::kBrowserWindow);
  CHECK(personal_);
  window_ = std::make_unique<prototype::WorkspaceWindow>(work_->profile(), personal_->profile());
  window_->SetCloseCallback(base::BindOnce(&Runtime::RequestClose, weak_factory_.GetWeakPtr()));
  for (const auto& arg : base::CommandLine::ForCurrentProcess()->GetArgs()) {
    GURL url(arg);
    if (url.SchemeIsHTTPOrHTTPS()) { window_->Navigate(url); break; }
  }
  window_->widget()->Activate();
  window_->FocusAddressBar();
#if BUILDFLAG(IS_MAC)
  SetAppShellDelegate(this);
#endif
  LOG(INFO) << "Zitter ready; persistent data: " << g_browser_process->profile_manager()->user_data_dir();
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
  if (auto* window = browser->GetWindow()) window->Close();
  if (window_ && !closing_) window_->widget()->Activate();
}

#if BUILDFLAG(IS_MAC)
void Runtime::RaiseWindow() {
  if (window_ && !closing_) window_->widget()->Activate();
}

void Runtime::OpenUrl(const GURL& url) {
  if (!window_ || closing_) return;
  window_->NavigateFromBar(url, /*new_tab=*/true);
  window_->widget()->Activate();
}

void Runtime::RunCommand(AppCommand command) {
  if (!window_ || closing_) return;
  auto* shell = window_->shell();
  window_->widget()->Activate();
  switch (command) {
    case AppCommand::kNewTab: shell->OpenAddress(true); break;
    case AppCommand::kOpenAddress: window_->FocusAddressBar(); break;
    case AppCommand::kCloseTab: window_->CloseActiveTab(); break;
    case AppCommand::kReload: window_->RunPageCommand(prototype::PageCommand::kReload); break;
    case AppCommand::kToggleSidebar: shell->ToggleSidebar(); break;
    case AppCommand::kSettings: shell->OpenWorkspaceSettings(); break;
    case AppCommand::kWorkspaceWork: window_->Select("work"); break;
    case AppCommand::kWorkspacePersonal: window_->Select("personal"); break;
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
  ReleaseProfiles();
  keep_alive_.reset();
  chrome::ExitIgnoreUnloadHandlers();
}

void Runtime::ReleaseProfiles() {
#if BUILDFLAG(IS_MAC)
  SetAppShellDelegate(nullptr);
#endif
  window_.reset();
  // Ask storage backends to flush before their normal BrowserContext shutdown.
  for (auto* lease : {work_.get(), personal_.get()}) {
    if (lease) lease->profile()->GetDefaultStoragePartition()->Flush();
  }
  personal_.reset();
  work_.reset();
}

void Runtime::PostMainMessageLoopRun() {
  weak_factory_.InvalidateWeakPtrs();
  browsers_.Reset();
  stock_windows_.clear();
  ReleaseProfiles();
  keep_alive_.reset();
}
}  // namespace zitter
