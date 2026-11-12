#include "chromium/prototype/fingerprint_protection.h"

#include <utility>

#include "base/functional/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "chromium/prototype/fingerprint_session.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/common/renderer_preferences/renderer_preferences.h"

namespace zitter::prototype {

WEB_CONTENTS_USER_DATA_KEY_IMPL(FingerprintProtection);

FingerprintProtection::~FingerprintProtection() = default;
void FingerprintProtection::Attach(content::WebContents* contents) {
  CreateForWebContents(contents);
}
const std::vector<uint8_t>& FingerprintProtection::SessionKey(Profile* profile) {
  return FingerprintSession::For(*profile).key();
}
void FingerprintProtection::NewSessionKey(Profile* profile) {
  FingerprintSession::For(*profile).Rotate();
}
base::CallbackListSubscription FingerprintProtection::ObserveSessionKey(Profile* profile,
                                                                        base::RepeatingClosure changed) {
  return FingerprintSession::For(*profile).ObserveRotation(std::move(changed));
}
std::u16string FingerprintProtection::SiteCode(Profile* profile, const GURL& url) {
  if (!profile->GetPrefs()->GetBoolean(kFingerprintProtectionPref))
    return {};
  return FingerprintSession::For(*profile).SiteCode(url);
}
FingerprintProtection::FingerprintProtection(content::WebContents* contents)
    : content::WebContentsUserData<FingerprintProtection>(*contents) {
  auto* profile = Profile::FromBrowserContext(contents->GetBrowserContext());
  registrar_.Init(profile->GetPrefs());
  // Pages already open follow the switch; their next read gets the change.
  registrar_.Add(kFingerprintProtectionPref,
                 base::BindRepeating(&FingerprintProtection::Apply, base::Unretained(this)));
  key_changed_ = ObserveSessionKey(profile,
      base::BindRepeating(&FingerprintProtection::Apply, base::Unretained(this)));
  Apply();
}
void FingerprintProtection::Apply() {
  auto& contents = GetWebContents();
  auto* profile = Profile::FromBrowserContext(contents.GetBrowserContext());
  auto& key = contents.GetMutableRendererPrefs()->zitter_fingerprint_key;
  if (profile->GetPrefs()->GetBoolean(kFingerprintProtectionPref)) {
    const auto& session_key = FingerprintSession::For(*profile).key();
    if (key == session_key)
      return;
    key = session_key;
  } else {
    if (key.empty())
      return;
    key.clear();
  }
  contents.SyncRendererPrefs();
}
}  // namespace zitter::prototype
