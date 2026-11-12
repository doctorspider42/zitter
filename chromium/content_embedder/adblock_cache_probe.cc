// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "adblock_cache_probe.h"

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/strings/strcat.h"
#include "base/task/single_thread_task_runner.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "profile_context.h"

namespace zitter::content_embedder {
namespace {

ProfileContext& Profile(content::WebContents& page) {
  return *static_cast<ProfileContext*>(page.GetBrowserContext());
}

constexpr char kOldRule[] = "127.0.0.1,localhost##.cache-original";
constexpr char kNewRule[] = "127.0.0.1,localhost##.cache-replacement";
constexpr char kExemptRule[] = "127.0.0.1,localhost##.cache-excepted";
constexpr char kException[] = "127.0.0.1,localhost#@#.cache-excepted";

}  // namespace

AdblockCacheProbe::AdblockCacheProbe(
    content::WebContents& page, GURL origin,
    base::OnceCallback<void(bool)> done)
    : content::WebContentsObserver(&page),
      page_url_(origin.Resolve("/adblock-cache")),
      away_url_(base::StrCat({"http://localhost:", origin.port(),
                             "/adblock-cache-away"})),
      original_rules_(Profile(page).AdblockRules()),
      done_(std::move(done)) {}

AdblockCacheProbe::~AdblockCacheProbe() = default;

void AdblockCacheProbe::Start() {
  auto& profile = Profile(*web_contents());
  auto rules = original_rules_;
  rules.insert(rules.end(), {kOldRule, kExemptRule});
  if (!profile.IsAdblockEnabledFor(page_url_) ||
      !profile.SetAdblockRules(std::move(rules))) {
    Complete(false);
    return;
  }
  deadline_ = base::TimeTicks::Now() + base::Seconds(30);
  web_contents()->GetController().LoadURLWithParams(
      content::NavigationController::LoadURLParams(page_url_));
  Poll();
}

void AdblockCacheProbe::DidFinishNavigation(content::NavigationHandle* navigation) {
  if (!navigation->IsInPrimaryMainFrame())
    return;
  if (!navigation->HasCommitted() || navigation->IsErrorPage()) {
    Complete(false);
    return;
  }
  if (navigation->GetURL() == page_url_ && (step_ == 3 || step_ == 5)) {
    restored_from_cache_ = navigation->IsServedFromBackForwardCache();
    if (!restored_from_cache_) {
      LOG(ERROR) << "The ad blocking cache probe reloaded instead of restoring";
      Complete(false);
    }
  }
}

void AdblockCacheProbe::WebContentsDestroyed() {
  Complete(false);
}

bool AdblockCacheProbe::CheckCachedDocuments() {
  auto* main = main_document_.AsRenderFrameHostIfValid();
  auto* child = child_document_.AsRenderFrameHostIfValid();
  if (!main || !child) {
    LOG(ERROR) << "An ad blocking cache document was evicted";
    Complete(false);
    return false;
  }
  using Lifecycle = content::RenderFrameHost::LifecycleState;
  return main->IsInLifecycleState(Lifecycle::kInBackForwardCache) &&
         child->IsInLifecycleState(Lifecycle::kInBackForwardCache);
}

bool AdblockCacheProbe::CheckRestoredDocuments() {
  auto* main = main_document_.AsRenderFrameHostIfValid();
  auto* child = child_document_.AsRenderFrameHostIfValid();
  return restored_from_cache_ && main &&
         main == web_contents()->GetPrimaryMainFrame() && child &&
         child->IsActive() && child->GetOutermostMainFrame() == main &&
         child->GetProcess() != main->GetProcess();
}

void AdblockCacheProbe::NavigateAway() {
  web_contents()->GetController().LoadURLWithParams(
      content::NavigationController::LoadURLParams(away_url_));
}

void AdblockCacheProbe::GoBack() {
  restored_from_cache_ = false;
  if (!web_contents()->GetController().CanGoBack()) {
    Complete(false);
    return;
  }
  web_contents()->GetController().GoBack();
}

void AdblockCacheProbe::Poll() {
  if (!done_ || !web_contents())
    return;
  if (base::TimeTicks::Now() >= deadline_) {
    LOG(ERROR) << "Ad blocking cache probe timed out at step " << step_
               << " with title " << web_contents()->GetTitle();
    Complete(false);
    return;
  }
  auto& page = *web_contents();
  auto& profile = Profile(page);
  const auto& url = page.GetLastCommittedURL();
  const auto title = page.GetTitle();
  const bool settled = !page.IsLoading() && !profile.IsAdblockBuilding();
  if (step_ == 0 && settled && url == page_url_ && title == u"Adblock cache old:0") {
    auto* main = page.GetPrimaryMainFrame();
    main_document_ = main->GetWeakDocumentPtr();
    main->ForEachRenderFrameHost([&](content::RenderFrameHost* frame) {
      if (frame->GetLastCommittedURL().host() == "localhost" &&
          frame->GetLastCommittedURL().path() == "/adblock-cache-child") {
        child_document_ = frame->GetWeakDocumentPtr();
      }
    });
    auto* child = child_document_.AsRenderFrameHostIfValid();
    if (!child || child->GetProcess() == main->GetProcess()) {
      Complete(false);
      return;
    }
    step_ = 1;
    NavigateAway();
  } else if (step_ == 1 && settled && url == away_url_ && CheckCachedDocuments()) {
    auto rules = original_rules_;
    rules.insert(rules.end(), {kNewRule, kExemptRule, kException});
    if (!profile.SetAdblockRules(std::move(rules))) {
      Complete(false);
      return;
    }
    step_ = 2;
  } else if (step_ == 2 && settled) {
    step_ = 3;
    GoBack();
  } else if (step_ == 3 && settled && url == page_url_ &&
             CheckRestoredDocuments() && title == u"Adblock cache new:1") {
    step_ = 4;
    NavigateAway();
  } else if (step_ == 4 && settled && url == away_url_ && CheckCachedDocuments()) {
    if (!profile.SetAdblockSiteAllowed(page_url_.host(), true)) {
      Complete(false);
      return;
    }
    step_ = 5;
    GoBack();
  } else if (step_ == 5 && settled && url == page_url_ &&
             CheckRestoredDocuments() && title == u"Adblock cache none:2") {
    if (!profile.SetAdblockRules(original_rules_) ||
        !profile.SetAdblockSiteAllowed(page_url_.host(), false)) {
      Complete(false);
      return;
    }
    step_ = 6;
  } else if (step_ == 6 && settled && title == u"Adblock cache none:2") {
    Complete(true);
    return;
  }
  if (done_) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, base::BindOnce(&AdblockCacheProbe::Poll, weak_factory_.GetWeakPtr()),
        base::Milliseconds(25));
  }
}

void AdblockCacheProbe::Complete(bool passed) {
  if (!done_)
    return;
  weak_factory_.InvalidateWeakPtrs();
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(std::move(done_), passed));
}

}  // namespace zitter::content_embedder
