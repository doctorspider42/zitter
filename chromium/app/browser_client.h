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
  std::unique_ptr<content::BrowserMainParts> CreateBrowserMainParts(
      bool is_integration_test) override;
};
}  // namespace zitter
#endif
