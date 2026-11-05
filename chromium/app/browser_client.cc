#include "chromium/app/browser_client.h"

#include "base/check.h"
#include "chrome/browser/chrome_browser_main.h"
#include "chrome/common/profiler/main_thread_stack_sampling_profiler.h"
#include "chromium/app/runtime.h"
#include "chromium/prototype/content_blocking.h"
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

void BrowserClient::WillCreateURLLoaderFactory(
    content::BrowserContext* browser_context, content::RenderFrameHost* frame, int render_process_id,
    URLLoaderFactoryType type, const url::Origin& request_initiator, const net::IsolationInfo& isolation_info,
    std::optional<int64_t> navigation_id, ukm::SourceIdObj ukm_source_id, network::URLLoaderFactoryBuilder& factory_builder,
    mojo::PendingRemote<network::mojom::TrustedURLLoaderHeaderClient>* header_client, bool* bypass_redirect_checks,
    bool* disable_secure_dns, network::mojom::URLLoaderFactoryOverridePtr* factory_override,
    scoped_refptr<base::SequencedTaskRunner> navigation_response_task_runner, bool is_for_network_service) {
  // First in the chain: a blocked request never reaches extensions' webRequest.
  prototype::ContentBlocking::WillCreateURLLoaderFactory(browser_context, frame, type, isolation_info, factory_builder);
  ChromeContentBrowserClient::WillCreateURLLoaderFactory(
      browser_context, frame, render_process_id, type, request_initiator, isolation_info, navigation_id, ukm_source_id,
      factory_builder, header_client, bypass_redirect_checks, disable_secure_dns, factory_override,
      std::move(navigation_response_task_runner), is_for_network_service);
}

std::unique_ptr<content::BrowserMainParts> BrowserClient::CreateBrowserMainParts(
    bool is_integration_test) {
  auto parts = ChromeContentBrowserClient::CreateBrowserMainParts(is_integration_test);
  static_cast<ChromeBrowserMainParts*>(parts.get())->AddParts(std::make_unique<Runtime>());
  return parts;
}
}  // namespace zitter
