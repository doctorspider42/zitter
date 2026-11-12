#include "chromium/prototype/link_navigation.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_throttle.h"
#include "content/public/browser/navigation_throttle_registry.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/page_transition_types.h"

namespace zitter::prototype {
namespace {
class LinkThrottle final : public content::NavigationThrottle {
 public:
  LinkThrottle(content::NavigationThrottleRegistry& registry, LinkNavigation::Handler handler)
      : NavigationThrottle(registry), handler_(std::move(handler)) {}
  ThrottleCheckResult WillStartRequest() override {
    auto& navigation = *navigation_handle();
    // Only explicit top-level GET links. Forms, redirects, frames, reloads and
    // history traversal retain their original context and request semantics.
    if (navigation.IsInPrimaryMainFrame() && navigation.IsRendererInitiated() &&
        navigation.HasUserGesture() && !navigation.IsPost() && !navigation.IsFormSubmission() && !navigation.IsSameDocument() && !navigation.IsHistory() &&
        ui::PageTransitionCoreTypeIs(navigation.GetPageTransition(), ui::PAGE_TRANSITION_LINK) &&
        handler_.Run(navigation)) return CANCEL_AND_IGNORE;
    return PROCEED;
  }
  const char* GetNameForLogging() override { return "ZitterLinkRouting"; }
 private:
  LinkNavigation::Handler handler_;
};
}
WEB_CONTENTS_USER_DATA_KEY_IMPL(LinkNavigation);
LinkNavigation::LinkNavigation(content::WebContents* contents, Handler handler)
    : content::WebContentsUserData<LinkNavigation>(*contents), handler_(std::move(handler)) {}
LinkNavigation::~LinkNavigation() = default;
void LinkNavigation::Attach(content::WebContents* contents, Handler handler) {
  CreateForWebContents(contents, std::move(handler));
}
void LinkNavigation::CreateThrottle(content::NavigationThrottleRegistry& registry) {
  if (auto* routing = FromWebContents(registry.GetNavigationHandle().GetWebContents()))
    registry.AddThrottle(std::make_unique<LinkThrottle>(registry, routing->handler_));
}
}
