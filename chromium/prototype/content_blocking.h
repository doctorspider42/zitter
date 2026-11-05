// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_PROTOTYPE_CONTENT_BLOCKING_H_
#define ZITTER_CHROMIUM_PROTOTYPE_CONTENT_BLOCKING_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "url/gurl.h"

class Profile;
namespace content { class BrowserContext; class RenderFrameHost; }
namespace net { class IsolationInfo; }
namespace network { class SimpleURLLoader; class URLLoaderFactoryBuilder; }

namespace zitter::prototype {

// Per-profile prefs, registered by tools/content_blocking_patch.py.
inline constexpr char kAdBlockingPref[] = "zitter.ad_blocking";
inline constexpr char kAdBlockingAllowedSitesPref[] = "zitter.ad_blocking_allowed_sites";
inline constexpr char kAdBlockingRulesPref[] = "zitter.ad_blocking_rules";
// Own hiding rules that hid nothing on their site's recent loads: rule -> loads.
inline constexpr char kAdBlockingRuleMissesPref[] = "zitter.ad_blocking_rule_misses";

// Built-in ad blocking (docs/ad-blocking.md): the downloaded filter lists, one
// adblock-rust engine per profile (lists plus that profile's own rules), the
// per-site switch and the request check. UI thread only.
class ContentBlocking final {
 public:
  struct List {
    std::string id, name, url;
    size_t rules = 0;
    base::Time updated;
  };
  enum class Verdict { kAllow, kBlock, kOff };

  static ContentBlocking& Get();
  ContentBlocking(const ContentBlocking&) = delete;
  ContentBlocking& operator=(const ContentBlocking&) = delete;

  // Reads the lists from disk and schedules their refresh. Idempotent.
  void Start();
  const std::vector<List>& lists() const { return lists_; }
  bool updating() const { return updating_ > 0; }
  const std::string& last_error() const { return last_error_; }
  // Downloads every list now (Settings: Aktualizuj teraz).
  void UpdateLists();
  // Replaces the lists with these texts, for tests: no files, no downloads.
  void SetListsForTesting(std::vector<std::string> texts);
  // Lists, rules, engines or switches changed.
  base::CallbackListSubscription Observe(base::RepeatingClosure changed);

  // On for `profile` (its pref) and not switched off for the site of `page`.
  static bool EnabledFor(Profile* profile, const GURL& page);
  // The allow list's key for a page: its host without "www.".
  static std::string SiteKey(const GURL& page);
  // The registrable domain (eTLD+1) of a page's host, for assistant rule scope.
  static std::string RegistrableDomain(const GURL& page);
  static bool SiteAllowed(Profile* profile, const GURL& page);
  static void SetSiteAllowed(Profile* profile, const std::string& site, bool allowed);
  static std::vector<std::string> AllowedSites(Profile* profile);

  // The profile's own rules, one per entry.
  static std::vector<std::string> Rules(Profile* profile);
  // Checks one rule: empty when the engine parses it, else why not.
  static std::string RuleError(const std::string& rule);
  // Whether `rule` acts only on pages of `site` (an eTLD+1).
  static bool RuleInScope(const std::string& rule, const std::string& site);
  // Appends valid rules (skipping duplicates) after a comment line; false and
  // `error` when a rule is invalid. The engine rebuilds in the background.
  static bool AddRules(Profile* profile, const std::vector<std::string>& rules, const std::string& comment, std::string* error);
  static void RemoveRule(Profile* profile, const std::string& rule);
  // The selector of a hiding rule (`site##sel`, `site#?#sel`), else empty.
  static std::string HidingSelector(const std::string& rule);
  // The profile's own hiding rules for the site of `page`.
  static std::vector<std::string> SiteHidingRules(Profile* profile, const GURL& page);
  // Records one settled load of a page: how many elements each of its own
  // hiding rules acted on. A rule at zero on kStaleLoads loads in a row is stale.
  static void RecordRuleHits(Profile* profile, const std::vector<std::pair<std::string, int>>& hits);
  static std::vector<std::string> StaleRules(Profile* profile, const GURL& page);
  static constexpr int kStaleLoads = 3;

  // The request check: kBlock when the profile's engine blocks `url` loaded by
  // `page` as `type` (adblock-rust request types: script, image, sub_frame…).
  // With a `$redirect=` rule, `redirect` gets the data: URL to answer with.
  Verdict Check(Profile* profile, const GURL& url, const GURL& page, const std::string& type, std::string* redirect = nullptr);
  // Main-world scripts: host -> the scriptlets the profile's `+js()` rules
  // give it, for the hosts those rules name.
  std::vector<std::pair<std::string, std::string>> PageScripts(Profile* profile);
  // Cosmetic selectors for a frame on `url`: its site's, the generic ones that
  // need no class/ID lookup, and whether generic class/ID rules apply.
  // `procedural`: rules with an action, as adblock-rust's JSON for the page script.
  struct Cosmetic { std::vector<std::string> hide, procedural, exceptions; bool generichide = true; };
  Cosmetic CosmeticFor(Profile* profile, const GURL& url);
  std::vector<std::string> GenericSelectors(Profile* profile, const std::vector<std::string>& classes,
                                            const std::vector<std::string>& ids, const std::vector<std::string>& exceptions);
  // Whether the profile's engine is built and current.
  bool ReadyForTesting(Profile* profile);

  // Wraps a frame's or worker's URLLoaderFactory with the request check.
  // Called from the content browser client (app: zitter::BrowserClient).
  static void WillCreateURLLoaderFactory(content::BrowserContext* context, content::RenderFrameHost* frame,
                                         content::ContentBrowserClient::URLLoaderFactoryType type,
                                         const net::IsolationInfo& isolation, network::URLLoaderFactoryBuilder& builder);

  struct ProfileState;

 private:
  friend class base::NoDestructor<ContentBlocking>;
  ContentBlocking();
  ~ContentBlocking();
  ProfileState* StateFor(Profile* profile, bool build);
  void Rebuild(Profile* profile);
  void RebuildAll();
  void ListsLoaded(std::vector<std::optional<std::string>> texts, std::vector<base::Time> times);
  void ScheduleUpdate(base::TimeDelta delay);
  void DownloadStale(bool force);
  void Downloaded(size_t index, std::unique_ptr<network::SimpleURLLoader> loader, std::optional<std::string> body);
  void Notify();

  bool started_ = false;
  base::FilePath directory_;
  std::vector<List> lists_;
  // The list texts as the engines see them; shared with background builds.
  std::shared_ptr<const std::vector<std::string>> texts_;
  int updating_ = 0;
  std::string last_error_;
  base::OneShotTimer update_timer_;
  base::RepeatingClosureList observers_;
  base::WeakPtrFactory<ContentBlocking> weak_factory_{this};
};

// One tab: counts what was blocked on its page and hides what the cosmetic
// rules name in each of its frames.
class ContentBlockingTabHelper final : public content::WebContentsObserver,
                                      public content::WebContentsUserData<ContentBlockingTabHelper> {
 public:
  ~ContentBlockingTabHelper() override;
  static void Attach(content::WebContents* contents);
  // Requests blocked plus elements hidden on the current page.
  int blocked() const { return blocked_requests_ + hidden_elements_; }
  int blocked_requests() const { return blocked_requests_; }
  // Blocked request URLs (query cut), newest last, for the assistant.
  const std::vector<std::string>& blocked_urls() const { return blocked_urls_; }
  base::CallbackListSubscription ObserveCount(base::RepeatingClosure changed);
  void RecordBlocked(const GURL& url);
  // Applies the current rules to every frame again (rules or switch changed).
  void Reapply();
  // Rules on trial (adblock_add_rules check): their scriptlets and network
  // rules act in this tab only, on its next load, until cleared or saved.
  // False and `error` when they do not build.
  bool SetTrial(const std::vector<std::string>& rules, std::string* error);
  void ClearTrial();
  bool has_trial() const { return trial_ != nullptr; }
  // The trial's network verdict, as ContentBlocking::Check gives it.
  bool TrialBlocks(const GURL& url, const GURL& page, const std::string& type, std::string* redirect);
  // A main-world script for every frame of this tab, from its next load
  // (adblock_diagnose's probe); empty removes it.
  void SetProbe(std::string script);
  // Hides `selectors` at once on the main frame, before the engine has them.
  void HideNow(const std::vector<std::string>& selectors, base::OnceCallback<void(int)> done);
  // Outlines what `selectors` match on the main frame; empty clears it. `done`
  // gets {count, rules: [{selector, matches, error?}]}, empty without a page.
  void Preview(const std::vector<std::string>& selectors, base::OnceCallback<void(base::DictValue)> done);

 private:
  friend class content::WebContentsUserData<ContentBlockingTabHelper>;
  explicit ContentBlockingTabHelper(content::WebContents* contents);
  // content::WebContentsObserver:
  void ReadyToCommitNavigation(content::NavigationHandle* navigation) override;
  void DidFinishNavigation(content::NavigationHandle* navigation) override;
  void DOMContentLoaded(content::RenderFrameHost* frame) override;
  void DidFinishLoad(content::RenderFrameHost* frame, const GURL& url) override;
  void Style(content::RenderFrameHost* frame);
  // `reset` reports every class and ID again (the rules changed).
  void Scan(content::RenderFrameHost* frame, bool reset = false);
  void Run(content::RenderFrameHost* frame, base::DictValue args, base::OnceCallback<void(base::Value)> done);
  void SetHidden(int hidden);
  // The tab's renderer preferences: which main-world scripts run where.
  void ApplyPageScripts();
  // Counts what the site's own hiding rules act on once the page has settled.
  void CheckRuleHealth();
  Profile* profile();

  int blocked_requests_ = 0;
  int hidden_elements_ = 0;
  std::vector<std::string> blocked_urls_;
  base::RepeatingClosureList count_changed_;
  base::CallbackListSubscription rules_changed_;
  base::OneShotTimer rescan_;
  base::OneShotTimer health_;
  struct Trial;
  std::unique_ptr<Trial> trial_;
  std::string probe_;
  base::WeakPtrFactory<ContentBlockingTabHelper> weak_factory_{this};
  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace zitter::prototype
#endif  // ZITTER_CHROMIUM_PROTOTYPE_CONTENT_BLOCKING_H_
