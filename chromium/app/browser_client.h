#ifndef ZITTER_CHROMIUM_APP_BROWSER_CLIENT_H_
#define ZITTER_CHROMIUM_APP_BROWSER_CLIENT_H_

#include "chrome/app/chrome_main_delegate.h"
#include "chrome/browser/chrome_content_browser_client.h"

namespace zitter {
class MainDelegate final : public ChromeMainDelegate {
 public:
  using ChromeMainDelegate::ChromeMainDelegate;
 private:
  content::ContentBrowserClient* CreateContentBrowserClient() override;
};

class BrowserClient final : public ChromeContentBrowserClient {
 public:
  void CreateThrottlesForNavigation(content::NavigationThrottleRegistry& registry) override;
  // Built-in ad blocking: every frame and worker loads through its check.
  void WillCreateURLLoaderFactory(
      content::BrowserContext* browser_context,
      content::RenderFrameHost* frame,
      int render_process_id,
      URLLoaderFactoryType type,
      const url::Origin& request_initiator,
      const net::IsolationInfo& isolation_info,
      std::optional<int64_t> navigation_id,
      ukm::SourceIdObj ukm_source_id,
      network::URLLoaderFactoryBuilder& factory_builder,
      mojo::PendingRemote<network::mojom::TrustedURLLoaderHeaderClient>*
          header_client,
      bool* bypass_redirect_checks,
      bool* disable_secure_dns,
      network::mojom::URLLoaderFactoryOverridePtr* factory_override,
      scoped_refptr<base::SequencedTaskRunner> navigation_response_task_runner,
      bool is_for_network_service) override;
  std::unique_ptr<content::BrowserMainParts> CreateBrowserMainParts(
      bool is_integration_test) override;
};
}  // namespace zitter
#endif
