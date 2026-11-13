#include "chromium/prototype/site_data.h"

#include <algorithm>
#include <array>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/barrier_closure.h"
#include "base/no_destructor.h"
#include "base/strings/utf_string_conversions.h"
#include "base/strings/string_util.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/time/time.h"
#include "chrome/browser/browsing_data/chrome_browsing_data_model_delegate.h"
#include "chrome/browser/browsing_data/chrome_browsing_data_remover_constants.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "components/browsing_data/content/browsing_data_model.h"
#include "components/content_settings/core/browser/content_settings_info.h"
#include "components/content_settings/core/browser/content_settings_registry.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/browser/website_settings_info.h"
#include "components/content_settings/core/browser/website_settings_registry.h"
#include "content/public/browser/storage_partition.h"
#include "net/cookies/canonical_cookie.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "components/history/core/browser/history_service.h"
#include "components/keyed_service/core/service_access_type.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browsing_data_filter_builder.h"
#include "content/public/browser/browsing_data_remover.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"

namespace zitter::prototype {
namespace {

// Settings Chromium keeps for itself, never asked of the person.
constexpr std::array<std::string_view, 12> kInternal = {
    "legacy-cookie-access", "legacy-cookie-scope", "mixed-script", "anti-abuse",
    "javascript-jit", "javascript-optimizer", "auto-dark-web-content", "request-desktop-site",
    "inline-cue-menu", "device-attributes", "durable-storage", "subresource-filter"};

// A permission by the name Chromium registers it under ("local-network-access").
const content_settings::ContentSettingsInfo* InfoFor(const std::string& key) {
  const auto* website = content_settings::WebsiteSettingsRegistry::GetInstance()->GetByName(key);
  return website ? content_settings::ContentSettingsRegistry::GetInstance()->Get(website->type()) : nullptr;
}

bool IsSiteCategory(std::string_view key) {
  return std::ranges::any_of(kSiteCategoryGroups, [key](auto group) {
    return std::ranges::any_of(group, [key](const auto& category) {
      return category.key == key;
    });
  });
}

std::vector<ContentSetting> DefaultPolicies(const content_settings::ContentSettingsInfo& info) {
  std::vector<ContentSetting> result;
  for (const auto setting : {CONTENT_SETTING_BLOCK, CONTENT_SETTING_ASK, CONTENT_SETTING_ALLOW}) {
    if (info.IsDefaultSettingValid(setting))
      result.push_back(setting);
  }
  return result;
}

// The site's registrable domain: cookies and storage are cleared for all of it.
std::string SiteDomain(const GURL& url) {
  auto domain = net::registry_controlled_domains::GetDomainAndRegistry(
      url, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  return domain.empty() ? std::string(url.host()) : domain;
}

bool InDomain(const std::string& host, const std::string& domain) {
  return host == domain || base::EndsWith(host, "." + domain);
}

// History answers later on this (UI) sequence; one tracker outlives every query.
base::CancelableTaskTracker& Tracker() {
  static base::NoDestructor<base::CancelableTaskTracker> tracker;
  return *tracker;
}

class SiteRemoval final : public content::BrowsingDataRemover::Observer {
 public:
  SiteRemoval(content::BrowsingDataRemover* remover, base::OnceClosure done)
      : remover_(remover), done_(std::move(done)) { remover_->AddObserver(this); }
  // The remover answers only observers it knows, and only the task's own one.
  ~SiteRemoval() override { remover_->RemoveObserver(this); }
  void OnBrowsingDataRemoverDone(uint64_t) override {
    auto done = std::move(done_);
    delete this;
    std::move(done).Run();
  }
 private:
  raw_ptr<content::BrowsingDataRemover> remover_;
  base::OnceClosure done_;
};

void DeleteSiteHistory(Profile* profile, const GURL& url, base::OnceClosure done) {
  auto* history = HistoryServiceFactory::GetForProfile(profile, ServiceAccessType::EXPLICIT_ACCESS);
  if (!history) { std::move(done).Run(); return; }
  history::QueryOptions options;
  options.max_count = 0;
  const auto domain = SiteDomain(url);
  history->QueryHistory(base::UTF8ToUTF16(domain), options, base::BindOnce(
      [](base::WeakPtr<history::HistoryService> history, std::string domain, base::OnceClosure done, history::QueryResults results) {
        std::vector<GURL> urls;
        for (const auto& result : results) if (InDomain(std::string(result.url().host()), domain)) urls.push_back(result.url());
        if (history && !urls.empty()) history->DeleteURLs(urls);
        std::move(done).Run();
      }, history->AsWeakPtr(), domain, std::move(done)), &Tracker());
}

}  // namespace

void AppendSiteDefaults(Profile* profile, BrowserPreferences& result) {
  auto* map = HostContentSettingsMapFactory::GetForProfile(profile);
  for (const auto group : kSiteCategoryGroups) {
    for (const auto& category : group) {
      const auto* info = InfoFor(category.key);
      if (!info)
        continue;
      const auto policies = DefaultPolicies(*info);
      if (policies.empty() || policies.front() != CONTENT_SETTING_BLOCK)
        continue;
      content_settings::ProviderType provider;
      const auto setting = map->GetDefaultContentSetting(
          info->website_settings_info()->type(), &provider);
      const auto index = std::ranges::find(policies, setting);
      std::string options;
      if (std::ranges::contains(policies, CONTENT_SETTING_ASK)) options += 'p';
      if (std::ranges::contains(policies, CONTENT_SETTING_ALLOW)) options += 'a';
      result[std::string(kSiteDefaultPrefix) + category.key] = {
          index == policies.end() ? -1 : static_cast<int>(index - policies.begin()),
          content_settings::GetSettingSourceFromProviderType(provider) ==
              content_settings::SettingSource::kUser, std::move(options)};
    }
  }
}

bool WriteSiteDefault(Profile* profile, std::string_view key, int index) {
  if (!IsSiteCategory(key))
    return false;
  const auto* info = InfoFor(std::string(key));
  if (!info)
    return false;
  const auto policies = DefaultPolicies(*info);
  if (index < 0 || static_cast<size_t>(index) >= policies.size())
    return false;
  HostContentSettingsMapFactory::GetForProfile(profile)->SetDefaultContentSetting(
      info->website_settings_info()->type(), policies[index]);
  return true;
}

SiteSummary ReadSitePermissions(Profile* profile, const GURL& url, bool full) {
  SiteSummary summary;
  summary.url = url;
  summary.host = base::UTF8ToUTF16(url.host());
  summary.secure = url.SchemeIsCryptographic();
  if (!url.SchemeIsHTTPOrHTTPS()) return summary;
  auto* settings = HostContentSettingsMapFactory::GetForProfile(profile);
  const auto midnight = base::Time::Now().LocalMidnight();
  // Every kind of permission, including ones Zitter has no name for yet: a
  // rule the site was given can always be changed back here.
  for (const auto* registry : *content_settings::ContentSettingsRegistry::GetInstance()) {
    const auto& name = registry->website_settings_info()->name();
    const bool category = full && IsSiteCategory(name);
    if ((!category && std::ranges::contains(kInternal, std::string_view(name))) ||
        !registry->IsSettingValid(CONTENT_SETTING_BLOCK))
      continue;
    const auto type = registry->website_settings_info()->type();
    content_settings::SettingInfo info;
    const auto setting = settings->GetContentSetting(url, url, type, &info);
    // Only what this site has its own rule for; defaults stay out of the way.
    if (!category &&
        ((info.source != content_settings::SettingSource::kUser &&
          info.source != content_settings::SettingSource::kPolicy) ||
         (info.primary_pattern == ContentSettingsPattern::Wildcard() &&
          info.secondary_pattern == ContentSettingsPattern::Wildcard())))
      continue;
    SitePermissionState state;
    state.key = name;
    state.setting = setting == CONTENT_SETTING_ALLOW ? 1 : setting == CONTENT_SETTING_BLOCK ? 2 : 3;
    state.can_ask = registry->IsSettingValid(CONTENT_SETTING_ASK);
    state.today = !info.metadata.last_modified().is_null() && info.metadata.last_modified() >= midnight;
    state.editable = info.source == content_settings::SettingSource::kUser ||
                     info.source == content_settings::SettingSource::kNone;
    state.can_allow = registry->IsSettingValid(CONTENT_SETTING_ALLOW);
    summary.permissions.push_back(std::move(state));
  }
  return summary;
}

bool WriteSitePermission(Profile* profile, const GURL& url, const std::string& key, int setting) {
  const auto* registry = InfoFor(key);
  if (!registry || !url.SchemeIsHTTPOrHTTPS() || setting < 0 || setting > 3) return false;
  const auto type = registry->website_settings_info()->type();
  auto* settings = HostContentSettingsMapFactory::GetForProfile(profile);
  content_settings::SettingInfo info;
  settings->GetContentSetting(url, url, type, &info);
  if (info.source != content_settings::SettingSource::kUser && info.source != content_settings::SettingSource::kNone) return false;
  ContentSetting value = static_cast<ContentSetting>(setting);
  // "Ask" is the default for permissions: drop the rule instead of pinning it.
  if (value == CONTENT_SETTING_ASK && settings->GetDefaultContentSetting(type) == CONTENT_SETTING_ASK) value = CONTENT_SETTING_DEFAULT;
  if (value != CONTENT_SETTING_DEFAULT && !registry->IsSettingValid(value)) return false;
  settings->SetContentSettingDefaultScope(url, url, type, value);
  return true;
}

void ResetSitePermissions(Profile* profile, const GURL& url) {
  if (!url.SchemeIsHTTPOrHTTPS()) return;
  for (const auto& state : ReadSitePermissions(profile, url).permissions) {
    if (!state.editable) continue;
    if (const auto* registry = InfoFor(state.key))
      HostContentSettingsMapFactory::GetForProfile(profile)->SetContentSettingDefaultScope(url, url, registry->website_settings_info()->type(), CONTENT_SETTING_DEFAULT);
  }
}

void MeasureSiteData(Profile* profile, const GURL& url, base::RepeatingCallback<void(const SiteDataFacts&)> done) {
  auto facts = std::make_shared<SiteDataFacts>();
  const auto domain = SiteDomain(url);
  facts->domain = base::UTF8ToUTF16(domain);
  BrowsingDataModel::BuildFromDisk(profile, ChromeBrowsingDataModelDelegate::CreateForProfile(profile), base::BindOnce(
      [](std::shared_ptr<SiteDataFacts> facts, std::string domain, base::RepeatingCallback<void(const SiteDataFacts&)> done,
         std::unique_ptr<BrowsingDataModel> model) {
        int64_t cookie_bytes = 0, storage_bytes = 0;
        for (const auto& entry : *model) {
          if (!InDomain(BrowsingDataModel::GetHost(*entry.data_owner), domain)) continue;
          const bool cookie = entry.data_details->storage_types.Has(BrowsingDataModel::StorageType::kCookie);
          (cookie ? cookie_bytes : storage_bytes) += entry.data_details->storage_size;
        }
        facts->cookie_bytes = cookie_bytes; facts->storage_bytes = storage_bytes;
        facts->bytes = cookie_bytes + storage_bytes;
        done.Run(*facts);
      }, facts, domain, done));
  // The cookies themselves, by name, for the details page.
  profile->GetDefaultStoragePartition()->GetCookieManagerForBrowserProcess()->GetAllCookies(base::BindOnce(
      [](std::shared_ptr<SiteDataFacts> facts, std::string domain, base::RepeatingCallback<void(const SiteDataFacts&)> done,
         const std::vector<net::CanonicalCookie>& cookies) {
        facts->cookie_list.clear();
        for (const auto& cookie : cookies) {
          if (!InDomain(cookie.DomainWithoutDot(), domain)) continue;
          facts->cookie_list.push_back({cookie.Name(), cookie.DomainWithoutDot(), cookie.Path(),
              cookie.IsPersistent() ? cookie.ExpiryDate() : base::Time()});
        }
        std::ranges::sort(facts->cookie_list, [](const auto& a, const auto& b) { return std::tie(a.domain, a.name) < std::tie(b.domain, b.name); });
        facts->cookies = static_cast<int>(facts->cookie_list.size());
        done.Run(*facts);
      }, facts, domain, done));
  auto* history = HistoryServiceFactory::GetForProfile(profile, ServiceAccessType::EXPLICIT_ACCESS);
  if (!history) return;
  history->GetVisibleVisitCountToHost(url, base::BindOnce(
      [](std::shared_ptr<SiteDataFacts> facts, base::RepeatingCallback<void(const SiteDataFacts&)> done,
         history::VisibleVisitCountToHostResult result) {
        facts->visits = result.success ? result.count : 0;
        if (result.success && result.count > 0) facts->first_visit = result.first_visit;
        done.Run(*facts);
      }, facts, done), &Tracker());
}

void DeleteSiteCookie(Profile* profile, SiteCookie cookie, base::OnceClosure done) {
  auto* manager = profile->GetDefaultStoragePartition()->GetCookieManagerForBrowserProcess();
  manager->GetAllCookies(base::BindOnce(
      [](base::WeakPtr<Profile> profile, SiteCookie wanted, base::OnceClosure done, const std::vector<net::CanonicalCookie>& cookies) {
        if (!profile) return;
        auto* manager = profile->GetDefaultStoragePartition()->GetCookieManagerForBrowserProcess();
        auto finish = base::BarrierClosure(1, std::move(done));
        for (const auto& cookie : cookies) {
          if (cookie.Name() != wanted.name || cookie.DomainWithoutDot() != wanted.domain || cookie.Path() != wanted.path) continue;
          manager->DeleteCanonicalCookie(cookie, base::BindOnce([](base::OnceClosure finish, bool) { std::move(finish).Run(); }, std::move(finish)));
          return;
        }
        std::move(finish).Run();
      }, profile->GetWeakPtr(), std::move(cookie), std::move(done)));
}

void ClearSiteData(Profile* profile, const GURL& url, SiteClearChoice choice, base::OnceClosure done) {
  if (!url.SchemeIsHTTPOrHTTPS()) { std::move(done).Run(); return; }
  if (choice.permissions) ResetSitePermissions(profile, url);
  auto after_history = base::BindOnce([](base::WeakPtr<Profile> profile, GURL url, SiteClearChoice choice, base::OnceClosure done) {
    uint64_t mask = 0;
    if (choice.cookies) mask |= chrome_browsing_data_remover::DATA_TYPE_SITE_DATA;
    if (choice.cache) mask |= content::BrowsingDataRemover::DATA_TYPE_CACHE;
    if (!profile || !mask) { std::move(done).Run(); return; }
    auto filter = content::BrowsingDataFilterBuilder::Create(content::BrowsingDataFilterBuilder::Mode::kDelete);
    filter->AddRegisterableDomain(SiteDomain(url));
    auto* remover = profile->GetBrowsingDataRemover();
    // Deletes itself once the remover reports back.
    auto* removal = new SiteRemoval(remover, std::move(done));
    remover->RemoveWithFilterAndReply(base::Time(), base::Time::Max(), mask,
        content::BrowsingDataRemover::ORIGIN_TYPE_UNPROTECTED_WEB, std::move(filter), removal);
  }, profile->GetWeakPtr(), url, choice, std::move(done));
  if (choice.history) DeleteSiteHistory(profile, url, std::move(after_history));
  else std::move(after_history).Run();
}

}  // namespace zitter::prototype
