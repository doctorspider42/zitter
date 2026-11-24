// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "chromium/prototype/user_agent_policy.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace zitter::prototype {
namespace {

constexpr char kChrome[] =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like "
    "Gecko) Chrome/157.0.0.0 Safari/537.36";

TEST(UserAgentPolicyTest, AutoUsesChromeAndTheCatalogue) {
  UserAgentSettings settings;
  auto resolved = ResolveUserAgent(settings, "example.com");
  EXPECT_EQ(resolved.preset, kUserAgentChrome);
  EXPECT_EQ(resolved.source, UserAgentSource::kDefault);

  for (const char* host : {"youtube.com", "www.youtube.com", "m.youtube.com",
                           "music.youtube.com.", "youtu.be",
                           "www.youtube-nocookie.com", "accounts.google.com"}) {
    resolved = ResolveUserAgent(settings, host);
    EXPECT_EQ(resolved.preset, kUserAgentZitter) << host;
    EXPECT_EQ(resolved.source, UserAgentSource::kCatalog) << host;
  }
  EXPECT_EQ(ResolveUserAgent(settings, "notyoutube.com").preset, kUserAgentChrome);
  EXPECT_EQ(ResolveUserAgent(settings, "google.com").preset, kUserAgentChrome);
  EXPECT_EQ(ResolveUserAgent(settings, "mail.google.com").preset, kUserAgentChrome);
}

TEST(UserAgentPolicyTest, FixedModeIgnoresTheCatalogue) {
  UserAgentSettings settings;
  settings.mode = kUserAgentChrome;
  EXPECT_EQ(ResolveUserAgent(settings, "youtube.com").preset, kUserAgentChrome);
  settings.mode = kUserAgentZitter;
  EXPECT_EQ(ResolveUserAgent(settings, "example.com").preset, kUserAgentZitter);
}

TEST(UserAgentPolicyTest, SiteExceptionsWinAndTheMostSpecificOneApplies) {
  UserAgentSettings settings;
  settings.sites = {{"youtube.com", kUserAgentFirefox},
                    {"example.com", kUserAgentSafari},
                    {"news.example.com", kUserAgentChrome}};
  auto resolved = ResolveUserAgent(settings, "m.youtube.com");
  EXPECT_EQ(resolved.preset, kUserAgentFirefox);
  EXPECT_EQ(resolved.source, UserAgentSource::kSite);
  EXPECT_EQ(resolved.matched_host, "youtube.com");
  EXPECT_EQ(ResolveUserAgent(settings, "shop.example.com").preset, kUserAgentSafari);
  EXPECT_EQ(ResolveUserAgent(settings, "a.news.example.com").preset, kUserAgentChrome);
  EXPECT_EQ(FindUserAgentSiteException(settings, "www.example.com"), kUserAgentSafari);
  EXPECT_FALSE(FindUserAgentSiteException(settings, "shop.example.com"));
}

TEST(UserAgentPolicyTest, MissingCustomAgentsFallBack) {
  UserAgentSettings settings;
  settings.mode = "custom:TV";
  settings.sites = {{"tv.example", "custom:Gone"}};
  EXPECT_EQ(ResolveUserAgent(settings, "tv.example").preset, kUserAgentChrome);
  EXPECT_EQ(ResolveUserAgent(settings, "youtube.com").preset, kUserAgentZitter);

  settings.custom = {{"TV", "Mozilla/5.0 (SMART-TV)"}};
  EXPECT_EQ(ResolveUserAgent(settings, "example.org").preset, "custom:TV");
  EXPECT_EQ(UserAgentString(settings, "custom:TV", kChrome, "0.22.2"),
            "Mozilla/5.0 (SMART-TV)");
}

TEST(UserAgentPolicyTest, BuildsStrings) {
  UserAgentSettings settings;
  EXPECT_EQ(UserAgentString(settings, kUserAgentChrome, kChrome, "0.22.2"), kChrome);
  EXPECT_EQ(UserAgentString(settings, kUserAgentZitter, kChrome, "0.22.2"),
            std::string(kChrome) + " Zitter/0.22.2");
  const auto firefox = UserAgentString(settings, kUserAgentFirefox, kChrome, "0.22.2");
  EXPECT_NE(firefox.find("Firefox/158.0"), std::string::npos) << firefox;
  EXPECT_NE(firefox.find("rv:158.0"), std::string::npos) << firefox;
  EXPECT_NE(UserAgentString(settings, kUserAgentSafari, kChrome, "0.22.2").find("Safari/"),
            std::string::npos);
  EXPECT_EQ(ChromeMajorVersion(kChrome), 157);
  EXPECT_EQ(ChromeMajorVersion("Mozilla/5.0"), 0);
}

TEST(UserAgentPolicyTest, ValidatesInput) {
  EXPECT_EQ(NormalizeUserAgentHost("WWW.Example.COM."), "example.com");
  EXPECT_EQ(NormalizeUserAgentHost("example.com/path"), "");
  EXPECT_EQ(NormalizeUserAgentHost("a..b"), "");
  EXPECT_EQ(NormalizeUserAgentHost(""), "");
  EXPECT_TRUE(UserAgentHostMatches("a.b.com", "b.com"));
  EXPECT_FALSE(UserAgentHostMatches("ab.com", "b.com"));

  UserAgentSettings settings;
  EXPECT_TRUE(IsValidUserAgentMode(settings, kUserAgentAuto));
  EXPECT_TRUE(IsValidUserAgentMode(settings, kUserAgentSafari));
  EXPECT_FALSE(IsValidUserAgentMode(settings, "custom:TV"));
  EXPECT_FALSE(IsValidUserAgentMode(settings, "opera"));
  EXPECT_TRUE(IsValidCustomUserAgent({"TV", "Mozilla/5.0"}));
  EXPECT_FALSE(IsValidCustomUserAgent({" TV", "Mozilla/5.0"}));
  EXPECT_FALSE(IsValidCustomUserAgent({"TV", "bad\r\nheader"}));
  EXPECT_FALSE(IsValidCustomUserAgent({"", "Mozilla/5.0"}));
  EXPECT_EQ(UserAgentPresetName("custom:Kiosk TV"), u"Kiosk TV");
}

TEST(UserAgentPolicyTest, RoundTripsThroughADictionary) {
  UserAgentSettings settings;
  settings.mode = "custom:TV";
  settings.custom = {{"TV", "Mozilla/5.0 (SMART-TV)"}};
  settings.sites = {{"example.com", kUserAgentFirefox}, {"tv.example", "custom:TV"}};
  settings.client_hints = false;
  settings.pinned = true;
  ASSERT_TRUE(AreValidUserAgentSettings(settings));
  const auto copy = UserAgentSettingsFromDict(UserAgentSettingsToDict(settings));
  EXPECT_EQ(copy.mode, "custom:TV");
  ASSERT_EQ(copy.sites.size(), 2u);
  ASSERT_EQ(copy.custom.size(), 1u);
  EXPECT_EQ(copy.custom[0].ua, "Mozilla/5.0 (SMART-TV)");
  EXPECT_FALSE(copy.client_hints);
  EXPECT_TRUE(copy.pinned);

  auto broken = UserAgentSettingsToDict(settings);
  broken.Set("mode", "opera");
  broken.FindDict("sites")->Set("WWW.bad.com", kUserAgentChrome);
  broken.FindDict("sites")->Set("ok.com", "custom:Gone");
  const auto cleaned = UserAgentSettingsFromDict(broken);
  EXPECT_EQ(cleaned.mode, kUserAgentAuto);
  EXPECT_EQ(cleaned.sites.size(), 2u);

  settings.sites.push_back({"example.com", kUserAgentChrome});
  EXPECT_FALSE(AreValidUserAgentSettings(settings));
  EXPECT_TRUE(UserAgentSettingsFromDict(base::DictValue()).client_hints);
}

}  // namespace
}  // namespace zitter::prototype
