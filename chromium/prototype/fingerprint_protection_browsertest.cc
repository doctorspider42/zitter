#include "chromium/prototype/fingerprint_protection.h"
#include "chromium/prototype/extensions_bar.h"
#include "chromium/prototype/i18n.h"
#include "chromium/prototype/workspace_shell.h"
#include "chromium/prototype/workspace_window.h"

#include <memory>
#include <string>

#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/test/run_until.h"
#include "base/test/scoped_feature_list.h"
#include "build/build_config.h"
#include "sandbox/policy/features.h"
#include "base/test/test_future.h"
#include "base/threading/thread_restrictions.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_destroyer.h"
#include "components/prefs/pref_service.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/common/renderer_preferences/renderer_preferences.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/base_window.h"

namespace zitter::prototype {

class ZitterFingerprintProtectionBrowserTest : public InProcessBrowserTest {
 public:
  ZitterFingerprintProtectionBrowserTest() {
    // The probe reads canvas and WebGL pixels: browser tests draw nothing
    // unless asked (on Windows every read was blank, so noise had nothing to
    // change).
    EnablePixelOutput();
#if BUILDFLAG(IS_WIN)
    // As in ZitterProfilesBrowserTest: the network service's AppContainer can't
    // read an out\ directory no installer granted it, and tests don't relaunch
    // it unsandboxed.
    network_sandbox_.InitAndDisableFeature(sandbox::policy::features::kNetworkServiceSandbox);
#endif
  }
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    auto* manager = g_browser_process->profile_manager();
    base::test::TestFuture<Profile*> work_ready, personal_ready;
    manager->CreateProfileAsync(manager->user_data_dir().AppendASCII("ZitterWork"), work_ready.GetCallback());
    work_ = work_ready.Get(); ASSERT_TRUE(work_);
    work_lease_ = std::make_unique<ScopedProfileKeepAlive>(work_, ProfileKeepAliveOrigin::kBrowserWindow);
    manager->CreateProfileAsync(manager->user_data_dir().AppendASCII("ZitterPersonal"), personal_ready.GetCallback());
    personal_ = personal_ready.Get(); ASSERT_TRUE(personal_);
    personal_lease_ = std::make_unique<ScopedProfileKeepAlive>(personal_, ProfileKeepAliveOrigin::kBrowserWindow);
    browser()->GetWindow()->Hide();
    {
      base::ScopedAllowBlockingForTesting files;
      const auto config=manager->user_data_dir().AppendASCII("ZitterWorkspaces.json");
      if(!base::PathExists(config))ASSERT_TRUE(base::WriteFile(config,R"({
        "profiles":[{"id":"work-profile","name":"Profil firmowy","directory":"ZitterWork"},{"id":"personal-profile","name":"Profil osobisty","directory":"ZitterPersonal"}],
        "workspaces":[{"id":"work","name":"Praca","profile_id":"work-profile"},{"id":"personal","name":"Prywatne","profile_id":"personal-profile"}],
        "active":"work","onboarding_complete":true
      })"));
    }
    SetUiLanguageForTesting("pl");
    window_ = std::make_unique<WorkspaceWindow>(work_, personal_);
    ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_ready(); }));
    embedded_test_server()->ServeFilesFromSourceDirectory("zitter/testdata");
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(window_->Select("work"));
  }
  void TearDownOnMainThread() override {
    window_.reset();
    personal_ = nullptr; work_ = nullptr;
    personal_lease_.reset(); work_lease_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
  }
  content::WebContents* page() { return window_->contents(window_->active_id()); }
  // Loads the probe on `host` in the active space and reads it like a tracker.
  base::DictValue Probe(const char* host) {
    EXPECT_TRUE(content::NavigateToURL(page(), embedded_test_server()->GetURL(host, "/fingerprint_probe.html")));
    auto result = content::EvalJs(page(), "probe()");
    EXPECT_TRUE(result.is_dict()) << result;
    if (!result.is_dict()) return base::DictValue();
    return result.ExtractDict().Clone();
  }
  static void ExpectAllDiffer(const base::DictValue& a, const base::DictValue& b) {
    for (const char* key : {"canvas", "pixels", "audio"}) EXPECT_NE(*a.Find(key), *b.Find(key)) << key;
    // Test bots without a GPU may not offer WebGL at all.
    if (a.FindString("webgl") && b.FindString("webgl")) EXPECT_NE(*a.FindString("webgl"), *b.FindString("webgl"));
    else LOG(WARNING) << "WebGL unavailable; readPixels noise not checked";
  }
 protected:
  raw_ptr<Profile> work_ = nullptr, personal_ = nullptr;
  std::unique_ptr<ScopedProfileKeepAlive> work_lease_, personal_lease_;
  std::unique_ptr<WorkspaceWindow> window_;
#if BUILDFLAG(IS_WIN)
  base::test::ScopedFeatureList network_sandbox_;
#endif
};

IN_PROC_BROWSER_TEST_F(ZitterFingerprintProtectionBrowserTest, PrivateControlWorksWhenRegularProtectionIsOff) {
  ASSERT_TRUE(window_->WritePreference("fingerprint_protection", 0));
  const auto plain = Probe("a.test");
  auto* profile = work_->GetOffTheRecordProfile(Profile::OTRProfileID::CreateUnique("ZitterPrivateTest"), true);
  const auto id = profile->GetOTRProfileID();
  auto one = std::make_unique<WorkspaceWindow>(work_->GetPath().DirName(), false, profile);
  auto two = std::make_unique<WorkspaceWindow>(work_->GetPath().DirName(), false, profile);
  const auto url = embedded_test_server()->GetURL("a.test", "/fingerprint_probe.html");
  ASSERT_TRUE(content::NavigateToURL(one->contents("private"), url));
  ASSERT_TRUE(content::NavigateToURL(two->contents("private"), url));
  auto* bar = one->extensions_bar();
  ASSERT_EQ(profile, bar->profile());
  ASSERT_TRUE(bar->fingerprint_button_for_testing());
  EXPECT_TRUE(one->ReadPreferences().at("fingerprint_protection").editable);
  EXPECT_NE(window_->shell()->active_workspace_color(), one->shell()->active_workspace_color());
  bar->ToggleFingerprintPanel();
  ASSERT_TRUE(bar->fingerprint_panel_for_testing());
  // The same popover offers turning protection on when the regular profile is off.
  bar->RunFingerprintCommandForTesting(1);
  EXPECT_TRUE(profile->GetPrefs()->GetBoolean(kFingerprintProtectionPref));
  EXPECT_FALSE(work_->GetPrefs()->GetBoolean(kFingerprintProtectionPref));
  EXPECT_TRUE(page()->GetMutableRendererPrefs()->zitter_fingerprint_key.empty());
  EXPECT_EQ(FingerprintProtection::SessionKey(profile), one->contents("private")->GetMutableRendererPrefs()->zitter_fingerprint_key);
  EXPECT_EQ(FingerprintProtection::SessionKey(profile), two->contents("private")->GetMutableRendererPrefs()->zitter_fingerprint_key);
  auto noisy = content::EvalJs(one->contents("private"), "probe()");
  ASSERT_TRUE(noisy.is_dict());
  ExpectAllDiffer(plain, noisy.ExtractDict());
  ASSERT_TRUE(two->WritePreference("fingerprint_protection", 0));
  EXPECT_TRUE(one->contents("private")->GetMutableRendererPrefs()->zitter_fingerprint_key.empty());
  EXPECT_TRUE(bar->fingerprint_button_for_testing());
  one.reset(); two.reset();
  ProfileDestroyer::DestroyOTRProfileWhenAppropriate(profile);
  ASSERT_TRUE(base::test::RunUntil([&] { return !work_->HasOffTheRecordProfile(id); }));
  profile = work_->GetOffTheRecordProfile(Profile::OTRProfileID::CreateUnique("ZitterPrivateTest"), true);
  EXPECT_FALSE(profile->GetPrefs()->GetBoolean(kFingerprintProtectionPref));
  ProfileDestroyer::DestroyOTRProfileWhenAppropriate(profile);
}

IN_PROC_BROWSER_TEST_F(ZitterFingerprintProtectionBrowserTest, OffByDefaultLeavesReadsUntouched) {
  EXPECT_EQ(0, window_->ReadPreferences().at("fingerprint_protection").value);
  EXPECT_TRUE(page()->GetMutableRendererPrefs()->zitter_fingerprint_key.empty());
  const auto first = Probe("a.test");
  // Without noise, the same drawing reads the same on every site.
  EXPECT_EQ(first, Probe("b.test"));
  EXPECT_EQ(first, Probe("a.test"));
}

IN_PROC_BROWSER_TEST_F(ZitterFingerprintProtectionBrowserTest, EachSiteAndProfileSeesItsOwnStableFingerprint) {
  const auto plain = Probe("a.test");
  ASSERT_TRUE(window_->WritePreference("fingerprint_protection", 1));
  EXPECT_EQ(1, window_->ReadPreferences().at("fingerprint_protection").value);
  EXPECT_EQ(FingerprintProtection::SessionKey(work_), page()->GetMutableRendererPrefs()->zitter_fingerprint_key);
  const auto site_a = Probe("a.test");
  ExpectAllDiffer(plain, site_a);
  // Stable for one site, so pages that check readback consistency keep working.
  EXPECT_EQ(site_a, Probe("a.test"));
  const auto site_b = Probe("b.test");
  ExpectAllDiffer(site_a, site_b);
  ASSERT_TRUE(window_->Select("personal"));
  EXPECT_TRUE(page()->GetMutableRendererPrefs()->zitter_fingerprint_key.empty()) << "the switch is per profile";
  EXPECT_EQ(plain, Probe("a.test"));
  ASSERT_TRUE(window_->WritePreference("fingerprint_protection", 1));
  ExpectAllDiffer(site_a, Probe("a.test"));
  // Turning it off again restores the untouched reads.
  ASSERT_TRUE(window_->Select("work"));
  ASSERT_TRUE(window_->WritePreference("fingerprint_protection", 0));
  EXPECT_TRUE(page()->GetMutableRendererPrefs()->zitter_fingerprint_key.empty());
  EXPECT_EQ(plain, Probe("a.test"));
}

IN_PROC_BROWSER_TEST_F(ZitterFingerprintProtectionBrowserTest, NewFingerprintChangesOnlyTheActiveProfile) {
  ASSERT_TRUE(window_->WritePreference("fingerprint_protection", 1));
  const auto before = Probe("a.test");
  const auto personal_key = FingerprintProtection::SessionKey(personal_);
  FingerprintProtection::NewSessionKey(work_);
  // The open page switches at once, like after a relaunch.
  EXPECT_EQ(FingerprintProtection::SessionKey(work_), page()->GetMutableRendererPrefs()->zitter_fingerprint_key);
  EXPECT_EQ(personal_key, FingerprintProtection::SessionKey(personal_));
  const auto after = Probe("a.test");
  ExpectAllDiffer(before, after);
  EXPECT_EQ(after, Probe("a.test"));
}

IN_PROC_BROWSER_TEST_F(ZitterFingerprintProtectionBrowserTest, HeaderButtonShowsWhatTheSiteSeesAndChangesIt) {
  ASSERT_TRUE(content::NavigateToURL(page(), embedded_test_server()->GetURL("a.test", "/fingerprint_probe.html")));
  auto* bar = window_->extensions_bar();
  ASSERT_TRUE(base::test::RunUntil([&] { return bar->profile() == work_.get(); }));
  EXPECT_FALSE(bar->fingerprint_button_for_testing()) << "only while protection is on";
  ASSERT_TRUE(window_->WritePreference("fingerprint_protection", 1));
  ASSERT_TRUE(base::test::RunUntil([&] { return bar->fingerprint_button_for_testing() != nullptr; }));
  bar->ToggleFingerprintPanel();
  ASSERT_TRUE(bar->fingerprint_panel_for_testing());
  // The code is the start of the seed Blink derives for the site on screen.
  const auto url = page()->GetLastCommittedURL();
  const auto code = bar->fingerprint_code_for_testing();
  EXPECT_EQ(FingerprintProtection::SiteCode(work_, url), code);
  EXPECT_EQ(9u, code.size());
  EXPECT_NE(FingerprintProtection::SiteCode(work_, GURL("https://a.test/")),
            FingerprintProtection::SiteCode(work_, GURL("https://b.test/")));
  EXPECT_TRUE(FingerprintProtection::SiteCode(work_, GURL("chrome://history")).empty());
  // A new fingerprint shows at once, and the page already uses it.
  bar->RunFingerprintCommandForTesting(0);
  EXPECT_NE(code, bar->fingerprint_code_for_testing());
  EXPECT_EQ(FingerprintProtection::SiteCode(work_, url), bar->fingerprint_code_for_testing());
  EXPECT_EQ(FingerprintProtection::SessionKey(work_), page()->GetMutableRendererPrefs()->zitter_fingerprint_key);
  // Privacy settings close the panel.
  bar->RunFingerprintCommandForTesting(2);
  ASSERT_TRUE(base::test::RunUntil([&] { return !bar->fingerprint_panel_for_testing(); }));
  EXPECT_TRUE(window_->shell()->general_widget()->IsVisible());
  // Turning protection off removes the button and its panel.
  bar->ToggleFingerprintPanel();
  ASSERT_TRUE(bar->fingerprint_panel_for_testing());
  bar->RunFingerprintCommandForTesting(1);
  EXPECT_EQ(0, window_->ReadPreferences().at("fingerprint_protection").value);
  ASSERT_TRUE(base::test::RunUntil([&] { return !bar->fingerprint_button_for_testing() && !bar->fingerprint_panel_for_testing(); }));
  EXPECT_TRUE(page()->GetMutableRendererPrefs()->zitter_fingerprint_key.empty());
}

IN_PROC_BROWSER_TEST_F(ZitterFingerprintProtectionBrowserTest, SessionKeysAreRandomPerProfileAndOffTheRecord) {
  const auto& work = FingerprintProtection::SessionKey(work_);
  EXPECT_EQ(32u, work.size());
  EXPECT_EQ(work, FingerprintProtection::SessionKey(work_));
  EXPECT_NE(work, FingerprintProtection::SessionKey(personal_));
  EXPECT_NE(work, FingerprintProtection::SessionKey(
      work_->GetPrimaryOTRProfile(/*create_if_needed=*/true)));
}

}  // namespace zitter::prototype
