#ifndef ZITTER_CHROMIUM_PROTOTYPE_PAGE_SERVICES_H_
#define ZITTER_CHROMIUM_PROTOTYPE_PAGE_SERVICES_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include "base/scoped_observation.h"
#include "components/content_settings/core/browser/content_settings_observer.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "url/gurl.h"

namespace password_manager { class PasswordFormManagerForUI; }
namespace views { class DialogDelegate; class Widget; class View; }

namespace zitter::prototype {
class PanelScene;

// Chromium's per-page clients, with native Zitter password consent UI and a
// history adapter for pages outside Chrome's BrowserWindow/TabStripModel.
class PageServices final : public content::WebContentsObserver,
                           public content::WebContentsUserData<PageServices>,
                           public content_settings::Observer {
 public:
  static void Attach(content::WebContents* contents);
  ~PageServices() override;
  views::Widget* password_prompt() const;
  // A pending manual save first; otherwise `manage` (Chrome's password manager).
  void ShowPasswords();
  void SetManagePasswords(base::RepeatingClosure manage) { manage_passwords_ = std::move(manage); }
  bool password_scene_visible_for_testing() const;
  bool password_scene_contains_for_testing(std::string_view text) const;

 private:
  friend class content::WebContentsUserData<PageServices>;
  explicit PageServices(content::WebContents* contents);
  bool OfferPassword(std::unique_ptr<password_manager::PasswordFormManagerForUI> form, bool update);
  void SetManualPassword(std::unique_ptr<password_manager::PasswordFormManagerForUI> form, bool update);
  void DismissPassword();
  void ShowPasswordWidget(std::unique_ptr<views::DialogDelegate> dialog,bool show=true);
  void DidFinishNavigation(content::NavigationHandle* navigation) override;
  void DidStartNavigation(content::NavigationHandle* navigation) override;
  void OnVisibilityChanged(content::Visibility visibility) override;
  void WebContentsDestroyed() override;
  void EndVisit();
  void ApplySoundSetting();
  void OnContentSettingChanged(const ContentSettingsPattern& primary_pattern,
      const ContentSettingsPattern& secondary_pattern, ContentSettingsTypeSet types) override;
  base::ScopedObservation<HostContentSettingsMap, content_settings::Observer> sound_observation_{this};
  base::RepeatingClosure manage_passwords_;
  struct PendingPassword;
  std::shared_ptr<PendingPassword> pending_password_;

  std::optional<int> last_entry_id_;
  GURL last_url_;
  std::unique_ptr<password_manager::PasswordFormManagerForUI> manual_password_;
  bool manual_update_ = false;
  // Scene subscriptions die before the widget, and the delegate after it.
  std::unique_ptr<views::DialogDelegate> password_delegate_;
  std::unique_ptr<views::Widget> password_widget_;
  std::unique_ptr<PanelScene> password_scene_;
  base::WeakPtr<views::Widget> prompt_;
  // Store requests must be invalidated independently of stable client callbacks.
  base::WeakPtrFactory<PageServices> password_ui_factory_{this};
  base::WeakPtrFactory<PageServices> weak_factory_{this};
  WEB_CONTENTS_USER_DATA_KEY_DECL();
};
}  // namespace zitter::prototype
#endif
