#include "chromium/prototype/fingerprint_protection.h"

#include <memory>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/supports_user_data.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "crypto/hmac.h"
#include "crypto/random.h"
#include "net/base/schemeful_site.h"
#include "third_party/blink/public/common/renderer_preferences/renderer_preferences.h"
#include "url/gurl.h"

namespace zitter::prototype {
namespace {
const char kSessionKey[] = "zitter-fingerprint-session-key";
struct SessionKeyData final : base::SupportsUserData::Data {
  std::vector<uint8_t> bytes = std::vector<uint8_t>(32);
  base::RepeatingClosureList changed;
};
SessionKeyData& SessionKeyFor(Profile* profile) {
  auto* data = static_cast<SessionKeyData*>(profile->GetUserData(kSessionKey));
  if (!data) {
    auto created = std::make_unique<SessionKeyData>();
    crypto::RandBytes(created->bytes);
    data = created.get();
    profile->SetUserData(kSessionKey, std::move(created));
  }
  return *data;
}
}  // namespace

WEB_CONTENTS_USER_DATA_KEY_IMPL(FingerprintProtection);

FingerprintProtection::~FingerprintProtection() = default;
void FingerprintProtection::Attach(content::WebContents* contents) {
  CreateForWebContents(contents);
}
const std::vector<uint8_t>& FingerprintProtection::SessionKey(Profile* profile) {
  return SessionKeyFor(profile).bytes;
}
void FingerprintProtection::NewSessionKey(Profile* profile) {
  auto& data = SessionKeyFor(profile);
  crypto::RandBytes(data.bytes);
  data.changed.Notify();
}
base::CallbackListSubscription FingerprintProtection::ObserveSessionKey(Profile* profile,
                                                                        base::RepeatingClosure changed) {
  return SessionKeyFor(profile).changed.Add(std::move(changed));
}
std::u16string FingerprintProtection::SiteCode(Profile* profile, const GURL& url) {
  if (!profile->GetPrefs()->GetBoolean(kFingerprintProtectionPref) || !url.SchemeIsHTTPOrHTTPS()) return {};
  // The same input as Blink's ZitterFingerprintNoise::ForSite().
  const auto site = net::SchemefulSite(url).Serialize();
  const auto mac = crypto::hmac::SignSha256(SessionKey(profile), base::as_byte_span(site));
  const auto hex = base::HexEncode(base::span(mac).first(4u));
  return base::ASCIIToUTF16(hex.substr(0, 4) + " " + hex.substr(4));
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
  auto* profile = Profile::FromBrowserContext(GetWebContents().GetBrowserContext());
  const bool on = profile->GetPrefs()->GetBoolean(kFingerprintProtectionPref);
  auto& key = GetWebContents().GetMutableRendererPrefs()->zitter_fingerprint_key;
  std::vector<uint8_t> next = on ? SessionKey(profile) : std::vector<uint8_t>();
  if (key == next) return;
  key = std::move(next);
  GetWebContents().SyncRendererPrefs();
}
}  // namespace zitter::prototype
