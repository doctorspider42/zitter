// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/prototype/content_blocking.h"
#include "chromium/prototype/extensions_bar.h"
#include "chromium/prototype/i18n.h"
#include "chromium/prototype/workspace_shell.h"
#include "chromium/prototype/workspace_window.h"

#include <algorithm>
#include <memory>
#include <string>

#include "base/files/file_util.h"
#include "base/json/json_writer.h"
#include "base/memory/raw_ptr.h"
#include "base/test/run_until.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/test_future.h"
#include "base/threading/thread_restrictions.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chrome_content_browser_client.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_client.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "sandbox/policy/features.h"
#include "third_party/blink/public/common/renderer_preferences/renderer_preferences.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/base_window.h"

namespace zitter::prototype {
namespace {
// Browser tests run Chromium's own client; the app's (zitter::BrowserClient)
// adds the same hook.
class BlockingClient final : public ChromeContentBrowserClient {
 public:
  void WillCreateURLLoaderFactory(
      content::BrowserContext* browser_context, content::RenderFrameHost* frame, int render_process_id,
      URLLoaderFactoryType type, const url::Origin& request_initiator, const net::IsolationInfo& isolation_info,
      std::optional<int64_t> navigation_id, ukm::SourceIdObj ukm_source_id, network::URLLoaderFactoryBuilder& factory_builder,
      mojo::PendingRemote<network::mojom::TrustedURLLoaderHeaderClient>* header_client, bool* bypass_redirect_checks,
      bool* disable_secure_dns, network::mojom::URLLoaderFactoryOverridePtr* factory_override,
      scoped_refptr<base::SequencedTaskRunner> navigation_response_task_runner, bool is_for_network_service) override {
    ContentBlocking::WillCreateURLLoaderFactory(browser_context, frame, type, isolation_info, factory_builder);
    ChromeContentBrowserClient::WillCreateURLLoaderFactory(
        browser_context, frame, render_process_id, type, request_initiator, isolation_info, navigation_id, ukm_source_id,
        factory_builder, header_client, bypass_redirect_checks, disable_secure_dns, factory_override,
        std::move(navigation_response_task_runner), is_for_network_service);
  }
};
// The test's lists: one ad host, one generic and one site-specific hiding rule.
constexpr char kList[] = "[Adblock Plus 2.0]\n! Test list\n||ads.test^\n##.ad-banner\nblock.test##.sponsored-box\n";
}  // namespace

class ZitterContentBlockingBrowserTest : public InProcessBrowserTest {
 public:
  ZitterContentBlockingBrowserTest() {
#if BUILDFLAG(IS_WIN)
    // As in ZitterProfilesBrowserTest: the network service's AppContainer can't
    // read an out\ directory no installer granted it.
    network_sandbox_.InitAndDisableFeature(sandbox::policy::features::kNetworkServiceSandbox);
#endif
  }
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    previous_client_ = content::SetBrowserClientForTesting(&client_);
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
      const auto config = manager->user_data_dir().AppendASCII("ZitterWorkspaces.json");
      if (!base::PathExists(config)) ASSERT_TRUE(base::WriteFile(config, R"({
        "profiles":[{"id":"work-profile","name":"Profil firmowy","directory":"ZitterWork"},{"id":"personal-profile","name":"Profil osobisty","directory":"ZitterPersonal"}],
        "workspaces":[{"id":"work","name":"Praca","profile_id":"work-profile"},{"id":"personal","name":"Prywatne","profile_id":"personal-profile"}],
        "active":"work","onboarding_complete":true
      })"));
    }
    SetUiLanguageForTesting("pl");
    ContentBlocking::Get().SetListsForTesting({kList});
    window_ = std::make_unique<WorkspaceWindow>(work_, personal_);
    ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_ready(); }));
    embedded_test_server()->ServeFilesFromSourceDirectory("zitter/testdata");
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(window_->Select("work"));
    ASSERT_TRUE(base::test::RunUntil([&] { return ContentBlocking::Get().ReadyForTesting(work_); }));
  }
  void TearDownOnMainThread() override {
    window_.reset();
    personal_ = nullptr; work_ = nullptr;
    personal_lease_.reset(); work_lease_.reset();
    content::SetBrowserClientForTesting(previous_client_);
    InProcessBrowserTest::TearDownOnMainThread();
  }
  content::WebContents* page() { return window_->contents(window_->active_id()); }
  void LoadProbe() {
    ASSERT_TRUE(content::NavigateToURL(page(), embedded_test_server()->GetURL("block.test", "/adblock_probe.html")));
    ASSERT_TRUE(base::test::RunUntil([&] {
      return content::EvalJs(page(), "Object.keys(loaded).length").ExtractInt() == 3;
    }));
  }
  std::string Display(const std::string& id) {
    return content::EvalJs(page(), "getComputedStyle(document.getElementById('" + id + "')).display").ExtractString();
  }
  base::Value Tool(std::string tool, base::DictValue args, bool confirmed = false) {
    base::test::TestFuture<base::Value> result;
    args.Set("tab", "@current");
    window_->AssistantTool(tool, std::move(args), AssistantPermissions{true, true, true, false, false}, result.GetCallback(), confirmed);
    return result.Take();
  }

 protected:
  BlockingClient client_;
  raw_ptr<content::ContentBrowserClient> previous_client_ = nullptr;
  raw_ptr<Profile> work_ = nullptr;
  raw_ptr<Profile> personal_ = nullptr;
  std::unique_ptr<ScopedProfileKeepAlive> work_lease_, personal_lease_;
  std::unique_ptr<WorkspaceWindow> window_;
#if BUILDFLAG(IS_WIN)
  base::test::ScopedFeatureList network_sandbox_;
#endif
};

IN_PROC_BROWSER_TEST_F(ZitterContentBlockingBrowserTest, BlocksFixtureAdsAndTheShieldTurnsItOffPerSite) {
  LoadProbe();
  // The ad host's script and image never load; the page's own script does.
  EXPECT_EQ(false, content::EvalJs(page(), "loaded.script"));
  EXPECT_EQ(false, content::EvalJs(page(), "loaded.image"));
  EXPECT_EQ(true, content::EvalJs(page(), "loaded.own"));
  // Generic and site rules hide; content and the unlisted ad stay.
  ASSERT_TRUE(base::test::RunUntil([&] { return Display("generic") == "none" && Display("specific") == "none"; }));
  EXPECT_NE("none", Display("story"));
  EXPECT_NE("none", Display("unlisted"));
  auto* helper = ContentBlockingTabHelper::FromWebContents(page());
  ASSERT_TRUE(helper);
  EXPECT_EQ(2, helper->blocked_requests());
  for (const auto& url : helper->blocked_urls()) EXPECT_NE(std::string::npos, url.find("ads.test")) << url;
  // The shield shows requests and hidden elements.
  auto* bar = window_->extensions_bar();
  ASSERT_TRUE(base::test::RunUntil([&] { return bar->shield_button_for_testing() && bar->shield_count_for_testing() == 4; }));
  bar->ToggleShieldPanel();
  EXPECT_TRUE(bar->shield_panel_for_testing());
  // Off for this site: the page reloads and everything loads.
  bar->RunShieldCommandForTesting(0);
  EXPECT_TRUE(ContentBlocking::SiteAllowed(work_, GURL("http://www.block.test/x")));
  EXPECT_EQ((std::vector<std::string>{"block.test"}), ContentBlocking::AllowedSites(work_));
  ASSERT_TRUE(content::WaitForLoadStop(page()));
  ASSERT_TRUE(base::test::RunUntil([&] { return content::EvalJs(page(), "Object.keys(loaded).length").ExtractInt() == 3; }));
  EXPECT_EQ(true, content::EvalJs(page(), "loaded.script"));
  EXPECT_NE("none", Display("generic"));
  EXPECT_EQ(-1, bar->shield_count_for_testing());
  EXPECT_FALSE(ContentBlocking::SiteAllowed(personal_, GURL("http://block.test/"))) << "the switch is per profile";
  // Settings turn it back on.
  EXPECT_EQ("", window_->WriteAdBlocking(base::DictValue().Set("command", "site_remove").Set("site", "block.test")));
  EXPECT_FALSE(ContentBlocking::SiteAllowed(work_, GURL("http://block.test/")));
  // The profile switch removes the shield and stops blocking.
  ASSERT_TRUE(window_->WritePreference("ad_blocking", 0));
  ASSERT_TRUE(base::test::RunUntil([&] { return !bar->shield_button_for_testing(); }));
  LoadProbe();
  EXPECT_EQ(true, content::EvalJs(page(), "loaded.script"));
}

IN_PROC_BROWSER_TEST_F(ZitterContentBlockingBrowserTest, AssistantAddedRuleHidesAnElementAfterConsent) {
  LoadProbe();
  // The inspection finds the ad no list knows, with a stable selector.
  const auto inspect = Tool("adblock_inspect", {});
  ASSERT_TRUE(inspect.is_dict()) << inspect;
  EXPECT_EQ("block.test", *inspect.GetDict().FindString("site"));
  EXPECT_EQ("on", *inspect.GetDict().FindString("blocking"));
  bool found = false;
  for (const auto& item : *inspect.GetDict().FindList("candidates")) {
    const auto& candidate = item.GetDict();
    if (*candidate.FindString("selector") == "div#unlisted") { found = true; EXPECT_EQ(1, *candidate.FindInt("matches")); }
    EXPECT_NE("h1#story", *candidate.FindString("selector"));
  }
  EXPECT_TRUE(found) << inspect;
  EXPECT_EQ(2u, inspect.GetDict().FindList("blocked_requests")->size());
  // Rules for other sites, or for the whole web, are refused whole.
  const auto refused = Tool("adblock_add_rules", base::DictValue().Set("rules", base::ListValue().Append("##.promo-rail").Append("block.test##div#unlisted")));
  ASSERT_TRUE(refused.GetDict().FindList("invalid"));
  EXPECT_EQ(1u, refused.GetDict().FindList("invalid")->size());
  // Rules sent as one string are read one per line, with the same checks.
  const auto lines = Tool("adblock_add_rules", base::DictValue().Set("rules", "##.promo-rail\nblock.test##div#unlisted"));
  ASSERT_TRUE(lines.GetDict().FindList("invalid")) << lines;
  EXPECT_EQ(1u, lines.GetDict().FindList("invalid")->size());
  EXPECT_TRUE(ContentBlocking::Rules(work_).empty());
  // Without consent: a preview and a question, nothing saved.
  const auto rules = base::ListValue().Append("block.test##div#unlisted");
  const auto asked = Tool("adblock_add_rules", base::DictValue().Set("rules", rules.Clone()));
  EXPECT_TRUE(asked.GetDict().FindBool("confirmation_required").value_or(false)) << asked;
  EXPECT_EQ(1, asked.GetDict().FindInt("preview").value_or(0));
  EXPECT_NE(nullptr, asked.GetDict().FindString("prompt"));
  EXPECT_EQ(true, content::EvalJs(page(), "unlisted.hasAttribute('data-zitter-preview')"));
  EXPECT_TRUE(ContentBlocking::Rules(work_).empty());
  // The click setting never skips this consent.
  base::test::TestFuture<base::Value> unconfirmed;
  window_->AssistantTool("adblock_add_rules", base::DictValue().Set("tab", "@current").Set("rules", rules.Clone()),
                         AssistantPermissions{true, true, true, false, false, /*confirm_clicks=*/false}, unconfirmed.GetCallback(), false);
  EXPECT_TRUE(unconfirmed.Get().GetDict().FindBool("confirmation_required").value_or(false));
  // With consent: saved for the profile and hidden at once.
  const auto saved = Tool("adblock_add_rules", base::DictValue().Set("rules", rules.Clone()), true);
  EXPECT_TRUE(saved.GetDict().FindBool("ok").value_or(false)) << saved;
  EXPECT_EQ(1, saved.GetDict().FindInt("saved").value_or(0));
  EXPECT_EQ("none", Display("unlisted"));
  EXPECT_EQ(false, content::EvalJs(page(), "unlisted.hasAttribute('data-zitter-preview')"));
  const auto stored = ContentBlocking::Rules(work_);
  ASSERT_EQ(2u, stored.size());
  EXPECT_TRUE(stored[0].starts_with("! Asystent: block.test, "));
  EXPECT_EQ("block.test##div#unlisted", stored[1]);
  // The engine has it: a fresh load hides it too.
  ASSERT_TRUE(base::test::RunUntil([&] { return ContentBlocking::Get().ReadyForTesting(work_); }));
  LoadProbe();
  ASSERT_TRUE(base::test::RunUntil([&] { return Display("unlisted") == "none"; }));
  EXPECT_NE("none", Display("story"));
  // Settings list it and remove it; the comment above it goes too.
  const auto settings = window_->ReadAdBlocking();
  EXPECT_EQ(2u, settings.FindList("rules")->size());
  EXPECT_EQ("", window_->WriteAdBlocking(base::DictValue().Set("command", "rule_remove").Set("rule", "block.test##div#unlisted")));
  EXPECT_TRUE(ContentBlocking::Rules(work_).empty());
  ASSERT_TRUE(base::test::RunUntil([&] { return ContentBlocking::Get().ReadyForTesting(work_); }));
  LoadProbe();
  EXPECT_NE("none", Display("unlisted"));
  // A rule typed in Settings is checked first.
  EXPECT_NE("", window_->WriteAdBlocking(base::DictValue().Set("command", "rule_add").Set("rule", "||ads.test^$nosuchoption")));
  EXPECT_EQ("", window_->WriteAdBlocking(base::DictValue().Set("command", "rule_add").Set("rule", "block.test##.promo-rail")));
  EXPECT_EQ((std::vector<std::string>{"block.test##.promo-rail"}), ContentBlocking::Rules(work_));
}

IN_PROC_BROWSER_TEST_F(ZitterContentBlockingBrowserTest, ProceduralRulesAnchorOnTheAdLabel) {
  LoadProbe();
  // The inspection finds the ad by its label and offers a rule anchored on it.
  const auto inspect = Tool("adblock_inspect", {});
  std::string hint;
  for (const auto& item : *inspect.GetDict().FindList("candidates"))
    if (const auto* label = item.GetDict().FindString("label"); label && *label == "REKLAMA" && hint.empty()) hint = *item.GetDict().FindString("rule_hint");
  EXPECT_EQ("block.test##span:has-text(/^\\s*REKLAMA\\s*$/i):upward(1)", hint) << inspect;
  // A banner anchored on its size: it stays hidden although hidden it has
  // none, and the slot it leaves collapses with it.
  const auto banner = Tool("adblock_add_rules", base::DictValue().Set("rules", base::ListValue()
      .Append("block.test##a[href]:has(img):not(:has-text(/\\p{L}/u)):min-size(300, 90)")), true);
  EXPECT_TRUE(banner.GetDict().FindBool("ok").value_or(false)) << banner;
  EXPECT_EQ("none", Display("banner"));
  EXPECT_EQ("none", Display("slot"));
  ASSERT_TRUE(content::ExecJs(page(), "document.body.append(document.createElement('p'))"));
  ASSERT_TRUE(content::ExecJs(page(), "new Promise(r => setTimeout(r, 400))"));
  EXPECT_EQ("none", Display("banner"));
  EXPECT_NE("none", Display("story"));
  // The page runs each rule before consent: what it cannot parse, or what
  // matches nothing, goes back to the model with nothing saved.
  const auto broken = Tool("adblock_add_rules", base::DictValue().Set("rules", base::ListValue()
      .Append("block.test##div.not-on-this-page").Append("block.test##span:has-text(/a{2,1}/)")));
  ASSERT_TRUE(broken.GetDict().FindList("invalid")) << broken;
  EXPECT_EQ(2u, broken.GetDict().FindList("invalid")->size()) << broken;
  EXPECT_EQ(false, content::EvalJs(page(), "document.querySelector('[data-zitter-preview]') !== null"));
  EXPECT_NE("", ContentBlocking::RuleError("block.test##div:has-text(open"));
  EXPECT_NE("", ContentBlocking::RuleError("block.test##+js(trusted-set-constant, x, 1)"));
  // With consent the label's box goes at once; the story stays.
  const auto saved = Tool("adblock_add_rules", base::DictValue().Set("rules", base::ListValue().Append(hint)), true);
  EXPECT_TRUE(saved.GetDict().FindBool("ok").value_or(false)) << saved;
  EXPECT_EQ("none", Display("labelled"));
  EXPECT_NE("none", Display("story"));
  // From the engine on a fresh load, with an action rule beside it.
  EXPECT_EQ("", window_->WriteAdBlocking(base::DictValue().Set("command", "rule_add").Set("rule", "block.test##p#text:style(color: rgb(1, 2, 3))")));
  ASSERT_TRUE(base::test::RunUntil([&] { return ContentBlocking::Get().ReadyForTesting(work_); }));
  LoadProbe();
  ASSERT_TRUE(base::test::RunUntil([&] { return Display("labelled") == "none"; }));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(page(), "getComputedStyle(document.getElementById('text')).color").ExtractString() == "rgb(1, 2, 3)";
  }));
  // A late ad with the same label is hidden as it appears.
  ASSERT_TRUE(content::ExecJs(page(), "const late = document.createElement('div'); late.id = 'late';"
                                      "late.innerHTML = '<span>REKLAMA</span><a href=\"#\">x</a>'; document.body.append(late);"));
  ASSERT_TRUE(base::test::RunUntil([&] { return Display("late") == "none"; }));
  // A rule that hid nothing on kStaleLoads loads in a row is stale; one hit clears it.
  const GURL url = embedded_test_server()->GetURL("block.test", "/adblock_probe.html");
  const std::string old_rule = "block.test##p#text:style(color: rgb(1, 2, 3))";
  EXPECT_TRUE(ContentBlocking::StaleRules(work_, url).empty());
  for (int load = 0; load < ContentBlocking::kStaleLoads; ++load) ContentBlocking::RecordRuleHits(work_, {{old_rule, 0}});
  EXPECT_EQ((std::vector<std::string>{old_rule}), ContentBlocking::StaleRules(work_, url));
  ContentBlocking::RecordRuleHits(work_, {{old_rule, 1}});
  EXPECT_TRUE(ContentBlocking::StaleRules(work_, url).empty());
  // Stale again, as before a repair.
  for (int load = 0; load < ContentBlocking::kStaleLoads; ++load) ContentBlocking::RecordRuleHits(work_, {{old_rule, 0}});
  // The assistant replaces it: the new rule is saved, the old one goes.
  const std::string new_rule = "block.test##p#text:style(color: rgb(4, 5, 6))";
  const auto refused = Tool("adblock_add_rules", base::DictValue().Set("rules", base::ListValue().Append(new_rule))
      .Set("replaces", base::ListValue().Append("block.test##.never-saved")), true);
  ASSERT_TRUE(refused.GetDict().FindList("invalid")) << refused;
  const auto replaced = Tool("adblock_add_rules", base::DictValue().Set("rules", base::ListValue().Append(new_rule))
      .Set("replaces", base::ListValue().Append(old_rule)), true);
  EXPECT_EQ(1, replaced.GetDict().FindInt("replaced").value_or(0)) << replaced;
  const auto rules = ContentBlocking::Rules(work_);
  EXPECT_FALSE(std::ranges::contains(rules, old_rule));
  EXPECT_TRUE(std::ranges::contains(rules, new_rule));
  EXPECT_TRUE(ContentBlocking::StaleRules(work_, url).empty());
}

IN_PROC_BROWSER_TEST_F(ZitterContentBlockingBrowserTest, ScriptletsRedirectsAndCanvasBeatAnAntiAdblockWall) {
  const GURL url = embedded_test_server()->GetURL("block.test", "/adblock_wall.html");
  ASSERT_TRUE(content::NavigateToURL(page(), url));
  // The bait script is blocked and the global is missing: the page walls itself off.
  ASSERT_TRUE(base::test::RunUntil([&] { return content::EvalJs(page(), "!!document.getElementById('wall') && window.painted === 255").ExtractBool(); }))
      << content::EvalJs(page(), "JSON.stringify({wall: !!document.getElementById('wall'), painted: window.painted, detected})").ExtractString();
  // The diagnosis reloads once with probes in the page's own world.
  const auto diagnosis = Tool("adblock_diagnose", {});
  ASSERT_TRUE(diagnosis.is_dict() && !diagnosis.GetDict().FindString("error")) << diagnosis;
  const std::string log = base::WriteJson(diagnosis).value_or("");
  EXPECT_NE(std::string::npos, log.find("ads.test")) << "the failed bait load: " << log;
  EXPECT_NE(std::string::npos, log.find("canRunAds")) << "the detector global: " << log;
  EXPECT_NE(std::string::npos, log.find("div.adsbox")) << "the bait element read: " << log;
  EXPECT_NE(std::string::npos, log.find("Please disable your ad blocker")) << "the wall: " << log;
  EXPECT_NE(std::string::npos, log.find("adblock_banner.png")) << "the canvas draw: " << log;
  EXPECT_TRUE(page()->GetMutableRendererPrefs()->zitter_page_scripts.empty()) << "the probe is gone after one load";
  // Unknown scriptlets and redirect bodies are refused.
  EXPECT_NE("", ContentBlocking::RuleError("block.test##+js(no-such-scriptlet)"));
  EXPECT_NE("", ContentBlocking::RuleError("||ads.test^$redirect=nothing.js,domain=block.test"));
  // A trial is the whole engine: an exception on trial lifts the list's block.
  const auto excepted = Tool("adblock_add_rules", base::DictValue().Set("rules", base::ListValue().Append("@@||ads.test^$script,domain=block.test")).Set("check", true));
  EXPECT_TRUE(excepted.GetDict().FindBool("checked").value_or(false)) << excepted;
  EXPECT_EQ(true, content::EvalJs(page(), "window.baitLoaded && !detected.includes('script')"))
      << content::EvalJs(page(), "JSON.stringify({baitLoaded, detected, url: location.href})").ExtractString() << " blocked: " << ContentBlockingTabHelper::FromWebContents(page())->blocked_requests();
    // check: the rules act in this tab only and it reloads with them; nothing is saved.
  const auto rules = base::ListValue()
      .Append("block.test##+js(set-constant, canRunAds, true)")
      .Append("||ads.test^$script,redirect=noop.js,domain=block.test")
      .Append("block.test##+js(zitter-skip-draw, adblock_banner)");
  const auto tried = Tool("adblock_add_rules", base::DictValue().Set("rules", rules.Clone()).Set("check", true));
  EXPECT_TRUE(tried.GetDict().FindBool("checked").value_or(false)) << tried;
  ASSERT_TRUE(base::test::RunUntil([&] { return content::EvalJs(page(), "window.painted !== undefined").ExtractBool(); }));
  EXPECT_EQ(false, content::EvalJs(page(), "!!document.getElementById('wall')"));
  EXPECT_EQ(true, content::EvalJs(page(), "window.baitLoaded"));
  EXPECT_EQ(0, content::EvalJs(page(), "window.painted"));
  EXPECT_EQ("function toString() { [native code] }", content::EvalJs(page(), "Function.prototype.toString.toString()"));
  EXPECT_TRUE(ContentBlocking::Rules(work_).empty());
  // Saved with consent, they come from the profile's engine on every load.
  const auto saved = Tool("adblock_add_rules", base::DictValue().Set("rules", rules.Clone()), true);
  EXPECT_TRUE(saved.GetDict().FindBool("ok").value_or(false)) << saved;
  ASSERT_TRUE(base::test::RunUntil([&] { return ContentBlocking::Get().ReadyForTesting(work_); }));
  ASSERT_TRUE(base::test::RunUntil([&] { return !page()->GetMutableRendererPrefs()->zitter_page_scripts.empty(); }));
  ASSERT_TRUE(content::NavigateToURL(page(), url));
  ASSERT_TRUE(base::test::RunUntil([&] { return content::EvalJs(page(), "window.painted !== undefined").ExtractBool(); }));
  EXPECT_EQ(false, content::EvalJs(page(), "!!document.getElementById('wall')"));
  EXPECT_EQ(true, content::EvalJs(page(), "window.baitLoaded && window.canRunAds === true"));
  EXPECT_EQ(0, content::EvalJs(page(), "window.painted"));
  // The page's own world never sees Zitter's helpers as globals.
  EXPECT_EQ(false, content::EvalJs(page(), "'zitterSafe' in window || 'setConstant' in window"));
  // A region of a canvas is covered by a mask in the page's color.
  const auto masked = Tool("adblock_add_rules", base::DictValue().Set("rules", base::ListValue().Append("block.test##canvas#board:mask(0, 0, 50%, 100%)")), true);
  EXPECT_TRUE(masked.GetDict().FindBool("ok").value_or(false)) << masked;
  EXPECT_EQ(150, content::EvalJs(page(), "Math.round(document.querySelector('[data-zitter=mask]').getBoundingClientRect().width)"));
  // The site switch turns the scriptlets off with everything else.
  ContentBlocking::SetSiteAllowed(work_, "block.test", true);
  ASSERT_TRUE(base::test::RunUntil([&] { return page()->GetMutableRendererPrefs()->zitter_page_scripts.empty(); }));
}

IN_PROC_BROWSER_TEST_F(ZitterContentBlockingBrowserTest, BuiltInStandInsAndOptionalLists) {
  // Zitter's own list and the Adblock Warning Removal List are on by default;
  // uBlock filters is off until switched on. Required lists have no switch.
  const auto read = [&](const std::string& id) {
    for (const auto& item : *window_->ReadAdBlocking().FindList("lists"))
      if (*item.GetDict().FindString("id") == id) return item.GetDict().Clone();
    return base::DictValue();
  };
  EXPECT_TRUE(read("zitter").FindBool("enabled").value_or(false));
  EXPECT_TRUE(read("zitter").FindBool("builtin").value_or(false));
  EXPECT_GT(read("zitter").FindInt("rules").value_or(0), 10);
  EXPECT_TRUE(read("antiadblock").FindBool("enabled").value_or(false));
  EXPECT_FALSE(read("ublock").FindBool("enabled").value_or(true));
  EXPECT_FALSE(read("easylist").FindBool("optional").value_or(true));
  // A detector library the test list blocks gets Zitter's stand-in instead of an error.
  const GURL url = embedded_test_server()->GetURL("block.test", "/adblock_detectors.html");
  ASSERT_TRUE(content::NavigateToURL(page(), url));
  ASSERT_TRUE(base::test::RunUntil([&] { return content::EvalJs(page(), "window.detector").ExtractString() != "pending"; }));
  EXPECT_EQ("stand-in", content::EvalJs(page(), "window.detector"));
  // Switched off, the same request just fails again.
  EXPECT_EQ("", window_->WriteAdBlocking(base::DictValue().Set("command", "list").Set("list", "zitter").Set("enabled", false)));
  EXPECT_FALSE(ContentBlocking::ListEnabled(work_, "zitter"));
  ASSERT_TRUE(base::test::RunUntil([&] { return ContentBlocking::Get().ReadyForTesting(work_); }));
  ASSERT_TRUE(content::NavigateToURL(page(), url));
  ASSERT_TRUE(base::test::RunUntil([&] { return content::EvalJs(page(), "window.detector").ExtractString() != "pending"; }));
  EXPECT_EQ("blocked", content::EvalJs(page(), "window.detector"));
  // The switches are per profile, and a required list cannot be turned off.
  EXPECT_TRUE(ContentBlocking::ListEnabled(personal_, "zitter"));
  EXPECT_EQ("", window_->WriteAdBlocking(base::DictValue().Set("command", "list").Set("list", "ublock").Set("enabled", true)));
  EXPECT_TRUE(ContentBlocking::ListEnabled(work_, "ublock"));
  window_->WriteAdBlocking(base::DictValue().Set("command", "list").Set("list", "easylist").Set("enabled", false));
  EXPECT_TRUE(ContentBlocking::ListEnabled(work_, "easylist"));
}

}  // namespace zitter::prototype
