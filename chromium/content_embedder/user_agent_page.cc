// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "user_agent_page.h"

#include <map>
#include <utility>

#include "chromium/app/zitter_version.h"
#include "components/embedder_support/user_agent_utils.h"
#include "base/functional/bind.h"
#include "base/task/single_thread_task_runner.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_throttle.h"
#include "content/public/browser/navigation_throttle_registry.h"
#include "content/public/common/referrer.h"
#include "third_party/blink/public/mojom/loader/referrer.mojom.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "profile_context.h"
#include "profile_permission_controller.h"
#include "url/gurl.h"

namespace zitter::content_embedder {
namespace {

std::string ChromeMajor() {
  const int major = prototype::ChromeMajorVersion(embedder_support::GetUserAgent());
  return major > 0 ? std::to_string(major) : std::string("0");
}

blink::UserAgentMetadata MetadataWithBrand(const std::string& brand,
                                           const std::string& major,
                                           const std::string& full) {
  auto metadata = embedder_support::GetUserAgentMetadata();
  metadata.brand_version_list = embedder_support::GetUserAgentBrandMajorVersionList(
      blink::UserAgentBrandVersion{brand, major});
  metadata.brand_full_version_list =
      embedder_support::GetUserAgentBrandFullVersionList(
          blink::UserAgentBrandVersion{brand, full});
  return metadata;
}

// The override the page applies to its main-frame navigations.
class UserAgentPage final : public content::WebContentsUserData<UserAgentPage>,
                            public content::WebContentsObserver {
 public:
  ~UserAgentPage() override = default;

  const std::optional<std::string>& committed_preset() const { return committed_; }
  // Redirect restarts since the last committed page; bounded against loops.
  bool TakeRedirectRestart() { return ++redirect_restarts_ <= 3; }

 private:
  friend WebContentsUserData;

  explicit UserAgentPage(content::WebContents* page)
      : WebContentsUserData(*page), WebContentsObserver(page) {}

  void DidStartNavigation(content::NavigationHandle* navigation) override {
    if (!navigation->IsInPrimaryMainFrame() || navigation->IsSameDocument() ||
        embedder_support::GetUserAgentFromCommandLine()) {
      return;
    }
    const GURL& url = navigation->GetURL();
    if (!url.SchemeIsHTTPOrHTTPS())
      return;
    const auto settings =
        static_cast<ProfileContext*>(web_contents()->GetBrowserContext())
            ->site_permissions()
            .UserAgent();
    const auto resolved = prototype::ResolveUserAgent(settings, url.host());
    pending_[navigation->GetNavigationId()] = resolved.preset;
    if (resolved.preset == prototype::kUserAgentChrome && settings.client_hints) {
      navigation->SetIsOverridingUserAgent(false);
      return;
    }
    // Safe here: NavigationRequest skips the reload a mid-load change would
    // otherwise trigger.
    web_contents()->SetUserAgentOverride(
        UserAgentOverrideFor(settings, resolved.preset), false);
    navigation->SetIsOverridingUserAgent(true);
  }

  void DidFinishNavigation(content::NavigationHandle* navigation) override {
    const auto it = pending_.find(navigation->GetNavigationId());
    if (it == pending_.end()) {
      if (navigation->IsInPrimaryMainFrame() && !navigation->IsSameDocument() &&
          navigation->HasCommitted()) {
        committed_.reset();
      }
      return;
    }
    if (navigation->HasCommitted() && !navigation->IsErrorPage()) {
      committed_ = it->second;
      redirect_restarts_ = 0;
    }
    pending_.erase(it);
  }

  std::map<int64_t, std::string> pending_;
  std::optional<std::string> committed_;
  int redirect_restarts_ = 0;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(UserAgentPage);

class UserAgentRedirectThrottle final : public content::NavigationThrottle {
 public:
  explicit UserAgentRedirectThrottle(content::NavigationThrottleRegistry& registry)
      : NavigationThrottle(registry) {}

  ThrottleCheckResult WillRedirectRequest() override {
    auto& navigation = *navigation_handle();
    const GURL& url = navigation.GetURL();
    const auto& chain = navigation.GetRedirectChain();
    if (!navigation.IsInPrimaryMainFrame() || navigation.IsPost() || chain.empty() ||
        !url.SchemeIsHTTPOrHTTPS() || embedder_support::GetUserAgentFromCommandLine()) {
      return PROCEED;
    }
    auto* contents = navigation.GetWebContents();
    const auto settings = static_cast<ProfileContext*>(contents->GetBrowserContext())
                              ->site_permissions()
                              .UserAgent();
    const auto target = prototype::ResolveUserAgent(settings, url.host());
    const auto start = prototype::ResolveUserAgent(settings, chain.front().host());
    auto* page = UserAgentPage::FromWebContents(contents);
    if (target.preset == start.preset || !page || !page->TakeRedirectRestart())
      return PROCEED;
    // The new navigation starts at `url`, where DidStartNavigation picks its preset.
    content::NavigationController::LoadURLParams params(url);
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

  const char* GetNameForLogging() override { return "ZitterUserAgentRedirect"; }
};

}  // namespace

void AddUserAgentRedirectThrottle(content::NavigationThrottleRegistry& registry) {
  if (registry.GetNavigationHandle().IsInPrimaryMainFrame())
    registry.AddThrottle(std::make_unique<UserAgentRedirectThrottle>(registry));
}

std::string DefaultUserAgent() {
  if (auto override = embedder_support::GetUserAgentFromCommandLine())
    return *override;
  return embedder_support::GetUserAgent();
}

blink::UserAgentMetadata DefaultUserAgentMetadata() {
  if (embedder_support::GetUserAgentFromCommandLine())
    return embedder_support::GetUserAgentMetadata();
  const auto chrome = embedder_support::GetUserAgentMetadata();
  return MetadataWithBrand("Google Chrome", ChromeMajor(), chrome.full_version);
}

blink::UserAgentMetadata ZitterUserAgentMetadata() {
  const std::string version = ZITTER_VERSION_STRING;
  return MetadataWithBrand("Zitter", version.substr(0, version.find('.')), version);
}

blink::UserAgentOverride UserAgentOverrideFor(
    const prototype::UserAgentSettings& settings,
    std::string_view preset) {
  blink::UserAgentOverride result;
  result.ua_string_override = prototype::UserAgentString(
      settings, preset, embedder_support::GetUserAgent(), ZITTER_VERSION_STRING);
  if (!settings.client_hints || preset == prototype::kUserAgentZitter)
    result.ua_metadata_override = ZitterUserAgentMetadata();
  else if (preset == prototype::kUserAgentChrome)
    result.ua_metadata_override = DefaultUserAgentMetadata();
  // Firefox, Safari and own agents send no client hints, like the browsers
  // they name.
  return result;
}

UserAgentChoice DescribeUserAgent(const prototype::UserAgentSettings& settings,
                                  const GURL& page) {
  UserAgentChoice choice;
  choice.resolved = prototype::ResolveUserAgent(settings, page.host());
  choice.ua = prototype::UserAgentString(settings, choice.resolved.preset,
                                         embedder_support::GetUserAgent(),
                                         ZITTER_VERSION_STRING);
  return choice;
}

std::optional<std::string> CommittedUserAgentPreset(content::WebContents& page) {
  const auto* helper = UserAgentPage::FromWebContents(&page);
  return helper ? helper->committed_preset() : std::nullopt;
}

void AttachUserAgentPage(content::WebContents& page) {
  UserAgentPage::CreateForWebContents(&page);
}

}  // namespace zitter::content_embedder
