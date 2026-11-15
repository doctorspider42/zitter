// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "browsing_preferences.h"

#include <memory>
#include <string_view>

#include "base/callback_list.h"
#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_throttle.h"
#include "content/public/browser/navigation_throttle_registry.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_user_data.h"
#include "content/public/common/referrer.h"
#include "net/base/url_util.h"
#include "net/http/http_util.h"
#include "profile_context.h"
#include "profile_permission_controller.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "third_party/blink/public/common/renderer_preferences/renderer_preferences.h"
#include "third_party/blink/public/common/web_preferences/web_preferences.h"
#include "url/gurl.h"

namespace zitter::content_embedder {
namespace {

void SetFont(blink::web_pref::ScriptFontFamilyMap& map, const std::string& family) {
  if (family.empty())
    map.erase(blink::web_pref::kCommonScript);
  else
    map[blink::web_pref::kCommonScript] = base::UTF8ToUTF16(family);
}

std::string DefaultAcceptLanguages() {
  const std::string locale = base::i18n::GetConfiguredLocale();
  if (base::StartsWith(locale, "pl", base::CompareCase::INSENSITIVE_ASCII))
    return "pl,en-US,en";
  return "en-US,en";
}

class HttpsOnlyThrottle final : public content::NavigationThrottle {
 public:
  explicit HttpsOnlyThrottle(content::NavigationThrottleRegistry& registry)
      : NavigationThrottle(registry) {}

  ThrottleCheckResult WillStartRequest() override { return Check(); }
  ThrottleCheckResult WillRedirectRequest() override { return Check(); }
  const char* GetNameForLogging() override { return "ZitterHttpsOnly"; }

 private:
  ThrottleCheckResult Check() {
    auto& navigation = *navigation_handle();
    const GURL& url = navigation.GetURL();
    if (!navigation.IsInPrimaryMainFrame() || !url.SchemeIs(url::kHttpScheme) ||
        net::IsLocalhost(url)) {
      return PROCEED;
    }
    auto* contents = navigation.GetWebContents();
    const auto& prefs =
        static_cast<ProfileContext*>(contents->GetBrowserContext())->site_permissions();
    if (!prefs.BrowsingFlag("https_only"))
      return PROCEED;
    GURL::Replacements secure;
    secure.SetSchemeStr(url::kHttpsScheme);
    if (url.port() == "80")
      secure.ClearPort();
    const GURL upgraded = url.ReplaceComponents(secure);
    // A site that redirects its HTTPS address back to HTTP keeps HTTP.
    for (const auto& previous : navigation.GetRedirectChain()) {
      if (previous == upgraded)
        return PROCEED;
    }
    content::NavigationController::LoadURLParams params(upgraded);
    params.transition_type = navigation.GetPageTransition();
    params.referrer = content::Referrer(navigation.GetReferrer().url,
                                        navigation.GetReferrer().policy);
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](base::WeakPtr<content::WebContents> contents,
               content::NavigationController::LoadURLParams params) {
              if (contents)
                contents->GetController().LoadURLWithParams(params);
            },
            contents->GetWeakPtr(), std::move(params)));
    return CANCEL_AND_IGNORE;
  }
};

class BrowsingPage final : public content::WebContentsUserData<BrowsingPage> {
 public:
  ~BrowsingPage() override = default;

 private:
  friend WebContentsUserData;

  explicit BrowsingPage(content::WebContents* page)
      : WebContentsUserData(*page),
        subscription_(Preferences().ObserveBrowsingPreferences(base::BindRepeating(
            &BrowsingPage::Apply, base::Unretained(this), /*initial=*/false))) {
    Apply(/*initial=*/true);
  }

  ProfilePermissionController& Preferences() {
    return static_cast<ProfileContext*>(GetWebContents().GetBrowserContext())
        ->site_permissions();
  }

  void Apply(bool initial) {
    auto& prefs = Preferences();
    ApplyBrowsingPagePreferences(prefs, GetWebContents());
    ApplyBrowsingNetworkPreferences(*GetWebContents().GetBrowserContext(), prefs);
    const bool muted = !prefs.BrowsingFlag("sound");
    // A new page starts muted only when sound is off; a change applies to all.
    if (!initial || muted)
      GetWebContents().SetAudioMuted(muted);
  }

  base::CallbackListSubscription subscription_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(BrowsingPage);

}  // namespace

void AttachBrowsingPreferences(content::WebContents& page) {
  BrowsingPage::CreateForWebContents(&page);
}

std::string BrowsingAcceptLanguages(const ProfilePermissionController& prefs) {
  std::string languages;
  base::RemoveChars(prefs.BrowsingText("accept_languages"), " ", &languages);
  return languages.empty() ? DefaultAcceptLanguages() : languages;
}

void ApplyBrowsingWebPreferences(const ProfilePermissionController& prefs,
                                 blink::web_pref::WebPreferences& web) {
  web.images_enabled = prefs.BrowsingFlag("images");
  web.javascript_enabled = prefs.BrowsingFlag("javascript");
  web.hyperlink_auditing_enabled = prefs.BrowsingFlag("link_auditing");
  web.default_font_size = prefs.BrowsingNumber("font_size");
  web.default_fixed_font_size = prefs.BrowsingNumber("fixed_font_size");
  web.minimum_font_size = prefs.BrowsingNumber("minimum_font_size");
  SetFont(web.standard_font_family_map, prefs.BrowsingText("font_standard"));
  SetFont(web.serif_font_family_map, prefs.BrowsingText("font_serif"));
  SetFont(web.sans_serif_font_family_map, prefs.BrowsingText("font_sans"));
  SetFont(web.fixed_font_family_map, prefs.BrowsingText("font_fixed"));
}

void ApplyBrowsingPagePreferences(const ProfilePermissionController& prefs,
                                  content::WebContents& page) {
  auto& renderer = *page.GetMutableRendererPrefs();
  renderer.enable_referrers = prefs.BrowsingFlag("referrers");
  renderer.enable_do_not_track = prefs.BrowsingFlag("do_not_track");
  renderer.caret_browsing_enabled = prefs.BrowsingFlag("caret_browsing");
  renderer.accept_languages = BrowsingAcceptLanguages(prefs);
  page.SyncRendererPrefs();
  page.NotifyPreferencesChanged();
}

void ApplyBrowsingNetworkPreferences(content::BrowserContext& context,
                                     const ProfilePermissionController& prefs) {
  auto* partition = context.GetDefaultStoragePartition();
  partition->GetCookieManagerForBrowserProcess()->BlockThirdPartyCookies(
      prefs.BrowsingFlag("third_party_cookies"));
  partition->GetNetworkContext()->SetAcceptLanguage(
      net::HttpUtil::GenerateAcceptLanguageHeader(BrowsingAcceptLanguages(prefs)));
}

void AddHttpsOnlyThrottle(content::NavigationThrottleRegistry& registry) {
  if (registry.GetNavigationHandle().IsInPrimaryMainFrame())
    registry.AddThrottle(std::make_unique<HttpsOnlyThrottle>(registry));
}

}  // namespace zitter::content_embedder
