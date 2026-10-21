#include "chromium/app/browser_client.h"

#include "base/check.h"
#include "chrome/browser/chrome_browser_main.h"
#include "chrome/common/profiler/main_thread_stack_sampling_profiler.h"
#include "chromium/app/runtime.h"
#include "chromium/prototype/link_navigation.h"

namespace zitter {
content::ContentBrowserClient* MainDelegate::CreateContentBrowserClient() {
  chrome_content_browser_client_ = std::make_unique<BrowserClient>();
  CHECK(sampling_profiler_);
  chrome_content_browser_client_->SetSamplingProfiler(std::move(sampling_profiler_));
  return chrome_content_browser_client_.get();
}

void BrowserClient::CreateThrottlesForNavigation(content::NavigationThrottleRegistry& registry) {
  prototype::LinkNavigation::CreateThrottle(registry);
  ChromeContentBrowserClient::CreateThrottlesForNavigation(registry);
}

std::unique_ptr<content::BrowserMainParts> BrowserClient::CreateBrowserMainParts(
    bool is_integration_test) {
  auto parts = ChromeContentBrowserClient::CreateBrowserMainParts(is_integration_test);
  static_cast<ChromeBrowserMainParts*>(parts.get())->AddParts(std::make_unique<Runtime>());
  return parts;
}
}  // namespace zitter
