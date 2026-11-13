#ifndef ZITTER_CHROMIUM_PROTOTYPE_SITE_DATA_H_
#define ZITTER_CHROMIUM_PROTOTYPE_SITE_DATA_H_

#include "base/functional/callback.h"
#include "chromium/prototype/browser_data.h"

class Profile;

namespace zitter::prototype {
// One site's permissions and stored data, as its panel shows and clears them
// (design: Zitter dostępy stron). Chromium's content settings, browsing data
// model, history and remover do the work.

// Every permission this site has its own rule for, keyed by Chromium's name.
SiteSummary ReadSitePermissions(Profile* profile, const GURL& url, bool full = false);
void AppendSiteDefaults(Profile* profile, BrowserPreferences& result);
bool WriteSiteDefault(Profile* profile, std::string_view key, int index);
// `key` is that name; `setting` 0 default, 1 allow, 2 block, 3 ask (back to the default when that asks).
bool WriteSitePermission(Profile* profile, const GURL& url, const std::string& key, int setting);
// Every rule for this site back to the defaults.
void ResetSitePermissions(Profile* profile, const GURL& url);
// Cookie and storage sizes, the cookies by name, visits; answers as each arrives.
void MeasureSiteData(Profile* profile, const GURL& url, base::RepeatingCallback<void(const SiteDataFacts&)> done);
// One cookie, by name, domain and path; then `done`.
void DeleteSiteCookie(Profile* profile, SiteCookie cookie, base::OnceClosure done);
// Clears the chosen kinds for the site's registrable domain, then `done`.
void ClearSiteData(Profile* profile, const GURL& url, SiteClearChoice choice, base::OnceClosure done);
}  // namespace zitter::prototype
#endif
