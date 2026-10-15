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
#include "chromium/prototype/workspace_window.h"
#include "components/keep_alive_registry/keep_alive_types.h"
#include "components/keep_alive_registry/scoped_keep_alive.h"
#include "content/public/browser/storage_partition.h"
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
  LOG(INFO) << "Zitter ready; persistent data: " << g_browser_process->profile_manager()->user_data_dir();
}

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
  ReleaseProfiles();
  keep_alive_.reset();
}
}  // namespace zitter
