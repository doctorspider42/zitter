#ifndef ZITTER_CHROMIUM_PROTOTYPE_LINK_NAVIGATION_H_
#define ZITTER_CHROMIUM_PROTOTYPE_LINK_NAVIGATION_H_

#include "base/functional/callback.h"
#include "content/public/browser/web_contents_user_data.h"

namespace content { class NavigationHandle; class NavigationThrottleRegistry; }
namespace zitter::prototype {

// Per-page routing hook, shared by production and browser integration tests.
class LinkNavigation final : public content::WebContentsUserData<LinkNavigation> {
 public:
  ~LinkNavigation() override;
  using Handler = base::RepeatingCallback<bool(content::NavigationHandle&)>;
  static void Attach(content::WebContents* contents, Handler handler);
  static void CreateThrottle(content::NavigationThrottleRegistry& registry);
 private:
  friend class content::WebContentsUserData<LinkNavigation>;
  LinkNavigation(content::WebContents* contents, Handler handler);
  Handler handler_;
  WEB_CONTENTS_USER_DATA_KEY_DECL();
};
}
#endif
