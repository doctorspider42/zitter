#ifndef ZITTER_CHROMIUM_PROTOTYPE_PAGE_SERVICES_H_
#define ZITTER_CHROMIUM_PROTOTYPE_PAGE_SERVICES_H_

#include <memory>
#include <optional>
#include "base/scoped_observation.h"
#include "components/content_settings/core/browser/content_settings_observer.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"

#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "url/gurl.h"
#include "components/password_manager/core/browser/password_store/password_store_consumer.h"

namespace password_manager { class PasswordFormManagerForUI; }
namespace views { class DialogDelegateView; class Widget; }

namespace zitter::prototype {

// Chromium's per-page clients, with native Zitter password consent UI and a
// history adapter for pages outside Chrome's BrowserWindow/TabStripModel.
class PageServices final : public content::WebContentsObserver,
                           public content::WebContentsUserData<PageServices>,
                           public password_manager::PasswordStoreConsumer,
                           public content_settings::Observer {
 public:
  static void Attach(content::WebContents* contents);
  ~PageServices() override;
  views::Widget* password_prompt() const;
  void ShowPasswords();

 private:
  friend class content::WebContentsUserData<PageServices>;
  explicit PageServices(content::WebContents* contents);
  bool OfferPassword(std::unique_ptr<password_manager::PasswordFormManagerForUI> form, bool update);
  void SetManualPassword(std::unique_ptr<password_manager::PasswordFormManagerForUI> form, bool update);
  void DismissPassword();
  void DidFinishNavigation(content::NavigationHandle* navigation) override;
  void DidStartNavigation(content::NavigationHandle* navigation) override;
  void OnVisibilityChanged(content::Visibility visibility) override;
  void WebContentsDestroyed() override;
  void EndVisit();
  void ApplySoundSetting();
  void OnContentSettingChanged(const ContentSettingsPattern& primary_pattern,
      const ContentSettingsPattern& secondary_pattern, ContentSettingsTypeSet types) override;
  base::ScopedObservation<HostContentSettingsMap, content_settings::Observer> sound_observation_{this};
  void OnGetPasswordStoreResultsOrErrorFrom(password_manager::PasswordStoreInterface* store,
      base::expected<std::vector<password_manager::StoredCredential>, password_manager::PasswordStoreBackendError> results) override;
  bool manage_requested_ = false;
  struct PendingPassword;
  std::shared_ptr<PendingPassword> pending_password_;

  std::optional<int> last_entry_id_;
  GURL last_url_;
  std::unique_ptr<password_manager::PasswordFormManagerForUI> manual_password_;
  bool manual_update_ = false;
  base::WeakPtr<views::Widget> prompt_;
  base::WeakPtrFactory<PageServices> weak_factory_{this};
  WEB_CONTENTS_USER_DATA_KEY_DECL();
};
}  // namespace zitter::prototype
#endif
