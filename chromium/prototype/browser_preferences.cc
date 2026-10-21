#include "chromium/prototype/browser_preferences.h"

#include <algorithm>
#include <array>
#include <set>
#include <tuple>

#include "base/i18n/tag_converters.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"
#include "components/content_settings/core/browser/content_settings_info.h"
#include "components/content_settings/core/browser/content_settings_registry.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/language/core/browser/pref_names.h"
#include "components/language/core/browser/language_prefs.h"
#include "ui/base/l10n/l10n_util.h"
#include "components/prefs/pref_service.h"
#include "components/spellcheck/browser/pref_names.h"
#include "net/dns/public/dns_over_https_config.h"
#include "url/origin.h"

namespace zitter::prototype {
namespace {
struct PreferenceSpec { const char* key; const char* pref; bool app_wide = false; };
constexpr PreferenceSpec kBooleans[] = {
    {"spellcheck", spellcheck::prefs::kSpellCheckEnable},
    {"online_spellcheck", spellcheck::prefs::kSpellCheckUseSpellingService},
    {"referrers", prefs::kEnableReferrers},
    {"caret_browsing", prefs::kCaretBrowsingEnabled},
    {"hardware_acceleration", prefs::kHardwareAccelerationModeEnabled, true},
};
constexpr PreferenceSpec kTexts[] = {
    {"accept_languages", language::prefs::kSelectedLanguages},
    {"font_standard", prefs::kWebKitStandardFontFamily},
    {"font_serif", prefs::kWebKitSerifFontFamily},
    {"font_sans", prefs::kWebKitSansSerifFontFamily},
    {"font_fixed", prefs::kWebKitFixedFontFamily},
    {"dns_templates", prefs::kDnsOverHttpsTemplates, true},
};
constexpr std::array<const char*, 3> kDnsModes = {"off", "automatic", "secure"};
constexpr std::array kSiteTypes = {
    ContentSettingsType::JAVASCRIPT, ContentSettingsType::IMAGES,
    ContentSettingsType::POPUPS, ContentSettingsType::SOUND,
    ContentSettingsType::NOTIFICATIONS, ContentSettingsType::GEOLOCATION,
    ContentSettingsType::MEDIASTREAM_CAMERA, ContentSettingsType::MEDIASTREAM_MIC,
    ContentSettingsType::CLIPBOARD_READ_WRITE};
std::optional<ContentSettingsType> SiteType(const std::string& key) {
  auto it = std::find(kSitePreferenceKeys.begin(), kSitePreferenceKeys.end(), key);
  if (it == kSitePreferenceKeys.end()) return std::nullopt;
  return kSiteTypes[it - kSitePreferenceKeys.begin()];
}
bool Editable(PrefService* service, const char* pref) {
  const auto* entry = service ? service->FindPreference(pref) : nullptr;
  return entry && entry->IsUserModifiable();
}
bool UserRule(const ContentSettingPatternSource& rule) {
  return !rule.incognito && rule.source == content_settings::ProviderType::kPrefProvider;
}
bool ValidDoh(const std::string& text) {
  auto config = net::DnsOverHttpsConfig::FromString(text);
  return config && !config->servers().empty();
}
}  // namespace

void AppendChromiumPreferences(Profile* profile, PrefService* local, BrowserPreferences& result) {
  auto* service = profile->GetPrefs();
  for (const auto& spec : kBooleans) {
    auto* prefs = spec.app_wide ? local : service;
    if (prefs && prefs->FindPreference(spec.pref))
      result[spec.key] = {prefs->GetBoolean(spec.pref), Editable(prefs, spec.pref)};
  }
  for (const auto& spec : kTexts) {
    auto* prefs = spec.app_wide ? local : service;
    if (prefs && prefs->FindPreference(spec.pref)) {
      const bool editable = Editable(prefs, spec.pref) &&
          (std::string_view(spec.key) != "accept_languages" || Editable(prefs, language::prefs::kAcceptLanguages));
      result[spec.key] = {0, editable, prefs->GetString(spec.pref)};
    }
  }
  if (local && local->FindPreference(prefs::kDnsOverHttpsMode)) {
    auto mode = local->GetString(prefs::kDnsOverHttpsMode);
    auto it = std::find(kDnsModes.begin(), kDnsModes.end(), mode);
    result["dns_mode"] = {it == kDnsModes.end() ? -1 : static_cast<int>(it - kDnsModes.begin()), Editable(local, prefs::kDnsOverHttpsMode)};
  }
}
std::optional<bool> WriteChromiumPreference(Profile* profile, PrefService* local, const std::string& key, int value) {
  for (const auto& spec : kBooleans) if (key == spec.key) {
    auto* service = spec.app_wide ? local : profile->GetPrefs();
    if (!Editable(service, spec.pref) || (value != 0 && value != 1)) return false;
    service->SetBoolean(spec.pref, value != 0);
    return true;
  }
  if (key == "dns_mode") {
    if (!Editable(local, prefs::kDnsOverHttpsMode) || value < 0 || value >= static_cast<int>(kDnsModes.size())) return false;
    // Strict mode must never be saved without a usable resolver.
    if (value == 2 && !ValidDoh(local->GetString(prefs::kDnsOverHttpsTemplates))) return false;
    local->SetString(prefs::kDnsOverHttpsMode, kDnsModes[value]);
    return true;
  }
  return std::nullopt;
}
bool WriteChromiumTextPreference(Profile* profile, PrefService* local, const std::string& key, std::string value) {
  value = std::string(base::TrimWhitespaceASCII(value, base::TRIM_ALL));
  if (value.size() > 4096 || !base::IsStringUTF8(value) || value.find_first_of("\r\n\0", 0, 3) != std::string::npos) return false;
  for (const auto& spec : kTexts) if (key == spec.key) {
    auto* service = spec.app_wide ? local : profile->GetPrefs();
    if (!Editable(service, spec.pref)) return false;
    if (key == "accept_languages") {
      auto tags = base::SplitString(value, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
      if (tags.empty() || tags.size() > 20) return false;
      std::vector<std::string> normalized;
      std::set<std::string> seen;
      for (const auto& tag : tags) {
        auto parsed = base::i18n::GetLanguageTagFromString(tag);
        if (!parsed) return false;
        std::string canonical(parsed->tag_string());
        if (!l10n_util::IsPossibleAcceptLanguage(canonical)) return false;
        if (seen.insert(canonical).second) normalized.push_back(canonical);
      }
      if (!Editable(service, language::prefs::kAcceptLanguages)) return false;
      language::LanguagePrefs language_prefs(service);
      language_prefs.SetUserSelectedLanguagesList(normalized);
      return true;
    } else if (key == "dns_templates") {
      if (!value.empty() && !ValidDoh(value)) return false;
      if (value.empty() && local->GetString(prefs::kDnsOverHttpsMode) == "secure") return false;
    } else if (value.empty() || value.size() > 128) {
      return false;
    }
    service->SetString(spec.pref, value);
    return true;
  }
  return false;
}
std::vector<SitePreferenceRule> ReadChromiumSitePreferences(Profile* profile) {
  std::vector<SitePreferenceRule> result;
  auto* settings = HostContentSettingsMapFactory::GetForProfile(profile);
  for (size_t i = 0; i < kSiteTypes.size(); ++i) {
    for (const auto& rule : settings->GetSettingsForOneType(kSiteTypes[i])) {
      if (rule.incognito || rule.IsExpired() ||
          (rule.primary_pattern == ContentSettingsPattern::Wildcard() && rule.secondary_pattern == ContentSettingsPattern::Wildcard())) continue;
      result.push_back({kSitePreferenceKeys[i], rule.primary_pattern.ToString(), rule.secondary_pattern.ToString(),
          static_cast<int>(rule.GetContentSetting()), UserRule(rule)});
    }
  }
  std::ranges::sort(result, [](const auto& a, const auto& b) {
    return std::tie(a.primary, a.key, a.secondary) < std::tie(b.primary, b.key, b.secondary);
  });
  return result;
}
bool WriteChromiumSitePreference(Profile* profile, std::string origin, const std::string& key, int value) {
  auto type = SiteType(key);
  if (!type) return false;
  origin = std::string(base::TrimWhitespaceASCII(origin, base::TRIM_ALL));
  const GURL url(origin);
  if (!url.SchemeIsHTTPOrHTTPS() || url.has_username() || url.has_password() || url.has_query() || url.has_ref() ||
      (url.path() != "/" && !url.path().empty())) return false;
  if (value != CONTENT_SETTING_DEFAULT && value != CONTENT_SETTING_ALLOW && value != CONTENT_SETTING_BLOCK && value != CONTENT_SETTING_ASK) return false;
  const auto setting = static_cast<ContentSetting>(value);
  auto* info = content_settings::ContentSettingsRegistry::GetInstance()->Get(*type);
  if (setting != CONTENT_SETTING_DEFAULT && (!info || !info->IsSettingValid(setting))) return false;
  auto* settings = HostContentSettingsMapFactory::GetForProfile(profile);
  content_settings::SettingInfo source;
  settings->GetWebsiteSetting(url, url, *type, &source);
  if (source.source != content_settings::SettingSource::kUser) return false;
  settings->SetContentSettingDefaultScope(url, url, *type, setting);
  return true;
}
bool RemoveChromiumSitePreference(Profile* profile, const SitePreferenceRule& rule) {
  auto type = SiteType(rule.key);
  if (!type) return false;
  auto* settings = HostContentSettingsMapFactory::GetForProfile(profile);
  for (const auto& existing : settings->GetSettingsForOneType(*type)) {
    if (!UserRule(existing) || existing.primary_pattern.ToString() != rule.primary || existing.secondary_pattern.ToString() != rule.secondary ||
        (existing.primary_pattern == ContentSettingsPattern::Wildcard() && existing.secondary_pattern == ContentSettingsPattern::Wildcard())) continue;
    settings->SetContentSettingCustomScope(existing.primary_pattern, existing.secondary_pattern, *type, CONTENT_SETTING_DEFAULT);
    return true;
  }
  return false;
}
}  // namespace zitter::prototype
