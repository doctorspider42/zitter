// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "chromium/prototype/user_agent_policy.h"

#include <algorithm>

#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"

namespace zitter::prototype {
namespace {

bool IsHostChar(char c) {
  return base::IsAsciiAlphaNumeric(c) || c == '-' || c == '.' || c == '_';
}

const CustomUserAgent* FindCustom(const UserAgentSettings& settings,
                                  std::string_view preset) {
  if (!preset.starts_with(kUserAgentCustomPrefix))
    return nullptr;
  const auto name = preset.substr(std::string_view(kUserAgentCustomPrefix).size());
  for (const auto& agent : settings.custom) {
    if (agent.name == name)
      return &agent;
  }
  return nullptr;
}

std::string_view CleanHost(std::string_view host) {
  if (host.ends_with('.'))
    host.remove_suffix(1);
  return host;
}

}  // namespace

UserAgentSettings::UserAgentSettings() = default;
UserAgentSettings::UserAgentSettings(const UserAgentSettings&) = default;
UserAgentSettings& UserAgentSettings::operator=(const UserAgentSettings&) = default;
UserAgentSettings::~UserAgentSettings() = default;

const std::vector<CatalogEntry>& UserAgentCatalog() {
  // Google sign-in rejects browsers that claim to be Chrome without being it,
  // and YouTube signs in through it.
  static const base::NoDestructor<std::vector<CatalogEntry>> catalog({
      {"youtube.com", kUserAgentZitter},
      {"youtu.be", kUserAgentZitter},
      {"youtube-nocookie.com", kUserAgentZitter},
      {"accounts.google.com", kUserAgentZitter},
  });
  return *catalog;
}

std::string NormalizeUserAgentHost(std::string_view host) {
  host = CleanHost(host);
  if (host.empty() || host.size() > 253 || !std::ranges::all_of(host, IsHostChar) ||
      host.starts_with('.') || host.find("..") != std::string_view::npos) {
    return {};
  }
  std::string result = base::ToLowerASCII(host);
  if (result.starts_with("www.") && result.size() > 4)
    result.erase(0, 4);
  return result;
}

bool UserAgentHostMatches(std::string_view host, std::string_view domain) {
  host = CleanHost(host);
  return !domain.empty() &&
         (host == domain ||
          (host.size() > domain.size() && host.ends_with(domain) &&
           host[host.size() - domain.size() - 1] == '.'));
}

bool IsBuiltInUserAgentPreset(std::string_view preset) {
  return preset == kUserAgentChrome || preset == kUserAgentZitter ||
         preset == kUserAgentFirefox || preset == kUserAgentSafari;
}

bool IsKnownUserAgentPreset(const UserAgentSettings& settings,
                            std::string_view preset) {
  return IsBuiltInUserAgentPreset(preset) || FindCustom(settings, preset);
}

bool IsValidUserAgentMode(const UserAgentSettings& settings,
                          std::string_view mode) {
  return mode == kUserAgentAuto || IsKnownUserAgentPreset(settings, mode);
}

bool IsValidCustomUserAgent(const CustomUserAgent& agent) {
  const auto name = base::TrimWhitespaceASCII(agent.name, base::TRIM_ALL);
  return !name.empty() && name.size() == agent.name.size() &&
         agent.name.size() <= kMaxCustomUserAgentName &&
         base::IsStringUTF8(agent.name) && !agent.ua.empty() &&
         agent.ua.size() <= kMaxCustomUserAgentLength &&
         std::ranges::all_of(agent.ua, [](unsigned char c) {
           return c >= 0x20 && c < 0x7f;
         });
}

std::optional<CatalogEntry> FindUserAgentCatalogEntry(std::string_view host) {
  const CatalogEntry* best = nullptr;
  for (const auto& entry : UserAgentCatalog()) {
    if (UserAgentHostMatches(host, entry.host) &&
        (!best || std::string_view(entry.host).size() > std::string_view(best->host).size())) {
      best = &entry;
    }
  }
  return best ? std::optional<CatalogEntry>(*best) : std::nullopt;
}

std::optional<std::string> FindUserAgentSiteException(
    const UserAgentSettings& settings,
    std::string_view host) {
  const auto normalized = NormalizeUserAgentHost(host);
  for (const auto& site : settings.sites) {
    if (site.host == normalized)
      return site.preset;
  }
  return std::nullopt;
}

ResolvedUserAgent ResolveUserAgent(const UserAgentSettings& settings,
                                   std::string_view host) {
  const auto lower = base::ToLowerASCII(CleanHost(host));
  // The most specific exception wins: news.example.com over example.com.
  const UserAgentSite* best = nullptr;
  for (const auto& site : settings.sites) {
    if (IsKnownUserAgentPreset(settings, site.preset) &&
        UserAgentHostMatches(lower, site.host) &&
        (!best || site.host.size() > best->host.size())) {
      best = &site;
    }
  }
  if (best)
    return {best->preset, UserAgentSource::kSite, best->host};

  const bool auto_mode = settings.mode == kUserAgentAuto ||
                         !IsKnownUserAgentPreset(settings, settings.mode);
  if (auto_mode) {
    if (auto entry = FindUserAgentCatalogEntry(lower))
      return {entry->preset, UserAgentSource::kCatalog, entry->host};
    return {kUserAgentChrome, UserAgentSource::kDefault, {}};
  }
  return {settings.mode, UserAgentSource::kDefault, {}};
}

int ChromeMajorVersion(std::string_view chrome_ua) {
  const auto at = chrome_ua.find("Chrome/");
  if (at == std::string_view::npos)
    return 0;
  auto rest = chrome_ua.substr(at + 7);
  rest = rest.substr(0, rest.find('.'));
  int major = 0;
  return base::StringToInt(rest, &major) ? major : 0;
}

std::string UserAgentString(const UserAgentSettings& settings,
                            std::string_view preset,
                            std::string_view chrome_ua,
                            std::string_view zitter_version) {
  if (preset == kUserAgentZitter)
    return base::StrCat({chrome_ua, " Zitter/", zitter_version});
  if (preset == kUserAgentFirefox) {
    // Firefox ships a release a fortnight after Chrome's of the same cycle and
    // is currently one major ahead.
    const auto version = base::NumberToString(
        std::max(ChromeMajorVersion(chrome_ua) + 1, 140));
#if BUILDFLAG(IS_MAC)
    return base::StrCat({"Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:",
                         version, ".0) Gecko/20100101 Firefox/", version, ".0"});
#elif BUILDFLAG(IS_WIN)
    return base::StrCat({"Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:",
                         version, ".0) Gecko/20100101 Firefox/", version, ".0"});
#else
    return base::StrCat({"Mozilla/5.0 (X11; Linux x86_64; rv:", version,
                         ".0) Gecko/20100101 Firefox/", version, ".0"});
#endif
  }
  if (preset == kUserAgentSafari) {
    return "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 "
           "(KHTML, like Gecko) Version/26.1 Safari/605.1.15";
  }
  if (const auto* agent = FindCustom(settings, preset))
    return agent->ua;
  return std::string(chrome_ua);
}

UserAgentSettings UserAgentSettingsFromDict(const base::DictValue& dict) {
  UserAgentSettings settings;
  if (const auto* list = dict.FindList("custom")) {
    for (const auto& value : *list) {
      const auto* entry = value.GetIfDict();
      const auto* name = entry ? entry->FindString("name") : nullptr;
      const auto* ua = entry ? entry->FindString("ua") : nullptr;
      if (!name || !ua || settings.custom.size() >= kMaxCustomUserAgents)
        continue;
      CustomUserAgent agent{*name, *ua};
      if (IsValidCustomUserAgent(agent) &&
          !std::ranges::any_of(settings.custom, [&](const auto& other) {
            return other.name == agent.name;
          })) {
        settings.custom.push_back(std::move(agent));
      }
    }
  }
  if (const auto* sites = dict.FindDict("sites")) {
    for (const auto [host, preset] : *sites) {
      const auto* id = preset.GetIfString();
      if (id && settings.sites.size() < kMaxUserAgentSites &&
          NormalizeUserAgentHost(host) == host &&
          IsKnownUserAgentPreset(settings, *id)) {
        settings.sites.push_back({host, *id});
      }
    }
  }
  if (const auto* mode = dict.FindString("mode");
      mode && IsValidUserAgentMode(settings, *mode)) {
    settings.mode = *mode;
  }
  settings.client_hints = dict.FindBool("client_hints").value_or(true);
  settings.pinned = dict.FindBool("pinned").value_or(false);
  return settings;
}

base::DictValue UserAgentSettingsToDict(const UserAgentSettings& settings) {
  base::DictValue sites;
  for (const auto& site : settings.sites)
    sites.Set(site.host, site.preset);
  base::ListValue custom;
  for (const auto& agent : settings.custom)
    custom.Append(base::DictValue().Set("name", agent.name).Set("ua", agent.ua));
  return base::DictValue()
      .Set("mode", settings.mode)
      .Set("sites", std::move(sites))
      .Set("custom", std::move(custom))
      .Set("client_hints", settings.client_hints)
      .Set("pinned", settings.pinned);
}

bool AreValidUserAgentSettings(const UserAgentSettings& settings) {
  if (settings.custom.size() > kMaxCustomUserAgents ||
      settings.sites.size() > kMaxUserAgentSites ||
      !IsValidUserAgentMode(settings, settings.mode)) {
    return false;
  }
  for (size_t i = 0; i < settings.custom.size(); ++i) {
    if (!IsValidCustomUserAgent(settings.custom[i]))
      return false;
    for (size_t j = 0; j < i; ++j) {
      if (settings.custom[j].name == settings.custom[i].name)
        return false;
    }
  }
  for (size_t i = 0; i < settings.sites.size(); ++i) {
    const auto& site = settings.sites[i];
    if (NormalizeUserAgentHost(site.host) != site.host ||
        !IsKnownUserAgentPreset(settings, site.preset)) {
      return false;
    }
    for (size_t j = 0; j < i; ++j) {
      if (settings.sites[j].host == site.host)
        return false;
    }
  }
  return true;
}

std::u16string UserAgentPresetName(std::string_view preset) {
  if (preset == kUserAgentAuto)
    return u"Auto";
  if (preset == kUserAgentChrome)
    return u"Chrome";
  if (preset == kUserAgentZitter)
    return u"Zitter";
  if (preset == kUserAgentFirefox)
    return u"Firefox";
  if (preset == kUserAgentSafari)
    return u"Safari";
  if (preset.starts_with(kUserAgentCustomPrefix)) {
    return base::UTF8ToUTF16(
        preset.substr(std::string_view(kUserAgentCustomPrefix).size()));
  }
  return base::UTF8ToUTF16(preset);
}

}  // namespace zitter::prototype
