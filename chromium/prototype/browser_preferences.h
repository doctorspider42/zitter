#ifndef ZITTER_CHROMIUM_PROTOTYPE_BROWSER_PREFERENCES_H_
#define ZITTER_CHROMIUM_PROTOTYPE_BROWSER_PREFERENCES_H_

#include <optional>
#include <string>
#include "chromium/prototype/browser_data.h"

class Profile;
class PrefService;
namespace zitter::prototype {
void AppendChromiumPreferences(Profile* profile, PrefService* local_state, BrowserPreferences& result);
std::optional<bool> WriteChromiumPreference(Profile* profile, PrefService* local_state, const std::string& key, int value);
bool WriteChromiumTextPreference(Profile* profile, PrefService* local_state, const std::string& key, std::string value);
std::vector<SitePreferenceRule> ReadChromiumSitePreferences(Profile* profile);
bool WriteChromiumSitePreference(Profile* profile, std::string origin, const std::string& key, int setting);
bool RemoveChromiumSitePreference(Profile* profile, const SitePreferenceRule& rule);
}  // namespace zitter::prototype
#endif
