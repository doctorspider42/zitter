#include "chromium/app/performance_probe.h"

#include <cstdio>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "chromium/prototype/workspace_window.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"

namespace zitter {
namespace {
bool PageOk(content::WebContents* page, const GURL& url) {
  if (!page || page->GetLastCommittedURL() != url || page->GetPrimaryMainFrame()->IsErrorDocument()) return false;
  auto* entry = page->GetController().GetLastCommittedEntry();
  return entry && entry->GetHttpStatusCode() == 200;
}
}
AppPerformanceProbe::AppPerformanceProbe(prototype::WorkspaceWindow* window, base::OnceClosure done)
    : window_(window), done_(std::move(done)) {}
AppPerformanceProbe::~AppPerformanceProbe() = default;

void AppPerformanceProbe::Start() {
  const auto* command = base::CommandLine::ForCurrentProcess();
  const auto expected_directory = command->GetSwitchValuePath("user-data-dir");
  if (expected_directory.empty() || !expected_directory.IsAbsolute() ||
      g_browser_process->profile_manager()->user_data_dir() != expected_directory) {
    Finish(false); return;
  }
  const auto mode = command->GetSwitchValueASCII("zitter-app-benchmark");
  url_ = GURL(command->GetSwitchValueASCII("zitter-performance-url"));
  if (!url_.SchemeIsHTTPOrHTTPS() || !url_.is_valid() ||
      (command->HasSwitch("zitter-perf-iterations") &&
       !base::StringToInt(command->GetSwitchValueASCII("zitter-perf-iterations"), &iterations_)) ||
      (command->HasSwitch("zitter-perf-tabs") &&
       !base::StringToInt(command->GetSwitchValueASCII("zitter-perf-tabs"), &tabs_)) ||
      iterations_ < 1 || iterations_ > 200 || tabs_ < 2 || tabs_ > 200) { Finish(false); return; }
  window_->shell()->CloseOverlays(true);
  if (mode == "startup") {
    phase_ = Phase::kStartup; start_ = base::TimeTicks::Now();
    auto* page = window_->contents(window_->active_id());
    if (!page || page->GetVisibleURL() != url_) { Finish(false); return; }
    Observe(page); ArmDeadline();
    if (page->CompletedFirstVisuallyNonEmptyPaint()) DidFirstVisuallyNonEmptyPaint();
    return;
  }
  if (mode != "navigation" || !window_->Select("work") || window_->tab_count("work") != 1) {
    Finish(false); return;
  }
  NavigateNext();
}

void AppPerformanceProbe::ArmDeadline() {
  deadline_.Start(FROM_HERE, base::Seconds(30),
                  base::BindOnce(&AppPerformanceProbe::Finish, weak_factory_.GetWeakPtr(), false));
}

void AppPerformanceProbe::NavigateNext() {
  Observe(nullptr); painted_ = base::TimeTicks(); advance_pending_ = false;
  start_ = base::TimeTicks::Now();
  const bool new_tab = phase_ == Phase::kNavigation || preloaded_ != 0;
  if (!window_->NavigateFromBar(url_, new_tab)) { Finish(false); return; }
  dispatched_ = base::TimeTicks::Now();
  auto* page = window_->contents("work");
  if (!page) { Finish(false); return; }
  Observe(page); ArmDeadline();
  if (page->CompletedFirstVisuallyNonEmptyPaint() && !page->IsLoading()) DidFirstVisuallyNonEmptyPaint();
}

void AppPerformanceProbe::DidFirstVisuallyNonEmptyPaint() {
  if (phase_ == Phase::kDone) return;
  if (!PageOk(web_contents(), url_)) { Finish(false); return; }
  painted_ = base::TimeTicks::Now();
  if (phase_ == Phase::kStartup) {
    LOG(INFO) << "ZITTER_APP_FIRST_PAINT";
    Finish(true); return;
  }
  MaybeAdvance();
}
void AppPerformanceProbe::DidStopLoading() { MaybeAdvance(); }
void AppPerformanceProbe::WebContentsDestroyed() { if (phase_ != Phase::kDone) Finish(false); }

void AppPerformanceProbe::MaybeAdvance() {
  if (phase_ == Phase::kDone || phase_ == Phase::kStartup || advance_pending_ ||
      !web_contents() || web_contents()->IsLoading() || painted_.is_null()) return;
  if (!PageOk(web_contents(), url_)) { Finish(false); return; }
  advance_pending_ = true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
      base::BindOnce(&AppPerformanceProbe::Advance, weak_factory_.GetWeakPtr()));
}

void AppPerformanceProbe::Sample(const char* metric, double milliseconds) {
  if (trial_ < 0) return;
  LOG(INFO) << "ZITTER_PERF {\"metric\":\"" << metric << "\",\"trial\":" << trial_
            << ",\"ms\":" << milliseconds << "}";
}

void AppPerformanceProbe::Advance() {
  advance_pending_ = false; deadline_.Stop(); Observe(nullptr);
  if (phase_ == Phase::kPreload) {
    if (++preloaded_ == tabs_) phase_ = Phase::kNavigation;
    NavigateNext(); return;
  }
  Sample("app_new_tab_dispatch_ms", (dispatched_ - start_).InMillisecondsF());
  Sample("app_new_tab_first_visual_paint_ms", (painted_ - start_).InMillisecondsF());
  window_->CloseTab(tabs_);
  if (++trial_ < iterations_) { NavigateNext(); return; }
  phase_ = Phase::kSplit; trial_ = -2; PrepareSplit();
}

void AppPerformanceProbe::PrepareSplit() {
  ArmDeadline(); window_->SelectTab(0);
  auto* page = window_->contents("work");
  if (!page) { Finish(false); return; }
  page->GetPrimaryMainFrame()->InsertVisualStateCallback(
      base::BindOnce(&AppPerformanceProbe::SplitReady, weak_factory_.GetWeakPtr()));
}

void AppPerformanceProbe::SplitReady(bool ok) {
  if (!ok || phase_ != Phase::kSplit) { Finish(false); return; }
  start_ = base::TimeTicks::Now();
  if (!window_->SplitTab(1)) { Finish(false); return; }
  dispatched_ = base::TimeTicks::Now(); frames_pending_ = 2;
  for (bool right : {false, true}) {
    auto* page = window_->pane_contents(right);
    if (!page) { Finish(false); return; }
    page->GetPrimaryMainFrame()->InsertVisualStateCallback(
        base::BindOnce(&AppPerformanceProbe::SplitFrame, weak_factory_.GetWeakPtr()));
  }
}

void AppPerformanceProbe::SplitFrame(bool ok) {
  if (phase_ != Phase::kSplit) return;
  if (!ok) { Finish(false); return; }
  if (--frames_pending_) return;
  Sample("app_split_dispatch_ms", (dispatched_ - start_).InMillisecondsF());
  Sample("app_split_visual_state_ms", (base::TimeTicks::Now() - start_).InMillisecondsF());
  window_->Unsplit(0);
  if (++trial_ == iterations_) { Finish(true); return; }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
      base::BindOnce(&AppPerformanceProbe::PrepareSplit, weak_factory_.GetWeakPtr()));
}

void AppPerformanceProbe::Finish(bool ok) {
  if (phase_ == Phase::kDone) return;
  if (!ok) {
    auto* page = web_contents();
    fprintf(stderr, "ZITTER_APP_FAILURE phase=%d preloaded=%d trial=%d loading=%d painted=%d url=%s\n",
            static_cast<int>(phase_), preloaded_, trial_, page && page->IsLoading(),
            !painted_.is_null(), page ? page->GetLastCommittedURL().spec().c_str() : "none");
    if (page) {
      auto* view = page->GetRenderWidgetHostView();
      fprintf(stderr, "ZITTER_APP_VIEW visibility=%d frame_live=%d view=%d showing=%d bounds=%s widget_visible=%d widget_active=%d\n",
              static_cast<int>(page->GetVisibility()), page->GetPrimaryMainFrame()->IsRenderFrameLive(),
              !!view, view && view->IsShowing(), view ? view->GetViewBounds().ToString().c_str() : "none",
              window_->widget()->IsVisible(), window_->widget()->IsActive());
    }
    fflush(stderr);
  }
  phase_ = Phase::kDone; deadline_.Stop(); Observe(nullptr);
  LOG(INFO) << "ZITTER_APP_STATUS {\"ok\":" << (ok ? "true" : "false") << "}";
  if (done_) base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, std::move(done_));
}
}  // namespace zitter
