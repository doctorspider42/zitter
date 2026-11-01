#ifndef ZITTER_CHROMIUM_PROTOTYPE_FINGERPRINT_PROTECTION_H_
#define ZITTER_CHROMIUM_PROTOTYPE_FINGERPRINT_PROTECTION_H_

#include <cstdint>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "components/prefs/pref_change_registrar.h"
#include "content/public/browser/web_contents_user_data.h"

class GURL;
class Profile;

namespace zitter::prototype {

// Per-profile switch, registered by tools/fingerprint_protection_patch.py.
inline constexpr char kFingerprintProtectionPref[] = "zitter.fingerprint_protection";

// The page's renderer key for per-site canvas, WebGL and audio noise, kept in
// step with its profile's switch. Off sends no key and leaves pages untouched.
class FingerprintProtection final : public content::WebContentsUserData<FingerprintProtection> {
 public:
  ~FingerprintProtection() override;
  static void Attach(content::WebContents* contents);
  // Random per profile object and process run: an off-the-record profile and
  // every relaunch get their own. Never stored.
  static const std::vector<uint8_t>& SessionKey(Profile* profile);
  // A new session key for `profile`; its open pages switch at once, as if
  // the browser had been relaunched.
  static void NewSessionKey(Profile* profile);
  // Runs `changed` after every NewSessionKey() for `profile`.
  static base::CallbackListSubscription ObserveSessionKey(Profile* profile, base::RepeatingClosure changed);
  // What a page on `url` sees, as 8 hex digits ("A3F9 21C0"): the start of the
  // seed Blink derives for its top-level site. Empty when protection is off or
  // the page is not on the web.
  static std::u16string SiteCode(Profile* profile, const GURL& url);
 private:
  friend class content::WebContentsUserData<FingerprintProtection>;
  explicit FingerprintProtection(content::WebContents* contents);
  void Apply();
  PrefChangeRegistrar registrar_;
  base::CallbackListSubscription key_changed_;
  WEB_CONTENTS_USER_DATA_KEY_DECL();
};
}
#endif
