// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// Applies a profile's browsing preferences (Settings: privacy, content and
// readability, languages) to Content's page, renderer and network settings.

#ifndef ZITTER_CONTENT_BROWSING_PREFERENCES_H_
#define ZITTER_CONTENT_BROWSING_PREFERENCES_H_

#include <string>

namespace blink::web_pref {
struct WebPreferences;
}
namespace content {
class BrowserContext;
class NavigationThrottleRegistry;
class WebContents;
}  // namespace content

namespace zitter::content_embedder {

class ProfilePermissionController;

// The Accept-Language list a profile sends, as comma-separated codes.
std::string BrowsingAcceptLanguages(const ProfilePermissionController& prefs);

void ApplyBrowsingWebPreferences(const ProfilePermissionController& prefs,
                                 blink::web_pref::WebPreferences& web);

// Renderer preferences (referrers, Do Not Track, languages, caret browsing)
// and recomputed web preferences for one page.
void ApplyBrowsingPagePreferences(const ProfilePermissionController& prefs,
                                  content::WebContents& page);

// Third-party cookie blocking and Accept-Language in the profile's network
// context.
void ApplyBrowsingNetworkPreferences(content::BrowserContext& context,
                                     const ProfilePermissionController& prefs);

// Applies the preferences to a new page and again whenever they change.
void AttachBrowsingPreferences(content::WebContents& page);

// Upgrades top-level http:// navigations to https:// when the profile asks
// for HTTPS only.
void AddHttpsOnlyThrottle(content::NavigationThrottleRegistry& registry);

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_BROWSING_PREFERENCES_H_
