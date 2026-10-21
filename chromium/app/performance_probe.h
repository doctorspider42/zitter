#ifndef ZITTER_CHROMIUM_APP_PERFORMANCE_PROBE_H_
#define ZITTER_CHROMIUM_APP_PERFORMANCE_PROBE_H_

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "content/public/browser/web_contents_observer.h"
#include "url/gurl.h"

namespace zitter::prototype { class WorkspaceWindow; }
namespace zitter {
// Opt-in measurement of the regular application, using public content APIs.
// Runtime destroys this before its window. No browser-test runner/dependencies.
class AppPerformanceProbe final : public content::WebContentsObserver {
 public:
  AppPerformanceProbe(prototype::WorkspaceWindow* window, base::OnceClosure done);
  ~AppPerformanceProbe() override;
  void Start();
 private:
  enum class Phase { kPreload, kNavigation, kSplit, kStartup, kDone };
  void DidFirstVisuallyNonEmptyPaint() override;
  void DidStopLoading() override;
  void WebContentsDestroyed() override;
  void MaybeAdvance();
  void Advance();
  void NavigateNext();
  void PrepareSplit();
  void SplitReady(bool ok);
  void SplitFrame(bool ok);
  void ArmDeadline();
  void Finish(bool ok);
  void Sample(const char* metric, double milliseconds);
  raw_ptr<prototype::WorkspaceWindow> window_;
  base::OnceClosure done_;
  GURL url_;
  Phase phase_ = Phase::kPreload;
  int iterations_ = 30, tabs_ = 25, preloaded_ = 0, trial_ = -2, frames_pending_ = 0;
  bool advance_pending_ = false;
  base::TimeTicks start_, dispatched_, painted_;
  base::OneShotTimer deadline_;
  base::WeakPtrFactory<AppPerformanceProbe> weak_factory_{this};
};
}  // namespace zitter
#endif
