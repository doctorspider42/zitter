// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/prototype/content_blocking.h"

#include <algorithm>
#include <set>
#include <utility>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/memory/raw_ptr.h"
#include "base/path_service.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/supports_user_data.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/chrome_paths.h"
#include "chromium/prototype/adblock.rs.h"
#include "chromium/prototype/adblock_browser_support.h"
#include "chromium/prototype/adblock_network.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "content/public/browser/global_routing_id.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/weak_document_ptr.h"
#include "content/public/browser/web_contents.h"
#include "url/origin.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/mojom/url_response_head.mojom.h"

namespace zitter::prototype {
namespace {
constexpr char kStateKey[] = "zitter-content-blocking";
std::string Normalize(std::string host) {
  host = base::ToLowerASCII(base::TrimWhitespaceASCII(host, base::TRIM_ALL));
  while (host.ends_with('.')) host.pop_back();
  if (host.starts_with("www.")) host.erase(0, 4);
  return host;
}
std::string CutQuery(const GURL& url) {
  GURL::Replacements strip; strip.ClearQuery(); strip.ClearRef(); strip.ClearUsername(); strip.ClearPassword();
  return url.ReplaceComponents(strip).spec();
}
rust::Vec<rust::String> RustStrings(const std::vector<std::string>& values) {
  rust::Vec<rust::String> out;
  for (const auto& value : values) out.push_back(rust::String(value));
  return out;
}
std::vector<std::string> Strings(const rust::Vec<rust::String>& values) {
  std::vector<std::string> out;
  for (const auto& value : values) out.emplace_back(value);
  return out;
}
std::vector<std::string> StringList(const base::ListValue& list) {
  std::vector<std::string> out;
  for (const auto& value : list) if (value.is_string()) out.push_back(value.GetString());
  return out;
}
}  // namespace

// One regular profile: its engine and a cached view of its switches.
struct ContentBlocking::ProfileState final : base::SupportsUserData::Data {
  explicit ProfileState(Profile* profile) : profile(profile) {
    registrar.Init(profile->GetPrefs());
    auto read = base::BindRepeating(&ProfileState::ReadSwitches, base::Unretained(this));
    registrar.Add(kAdBlockingPref, read);
    registrar.Add(kAdBlockingAllowedSitesPref, read);
    registrar.Add(kAdBlockingRulesPref, base::BindRepeating(
        [](ProfileState* state) { ContentBlocking::Get().Rebuild(state->profile); }, base::Unretained(this)));
    // A list switched on that was never downloaded is fetched now.
    registrar.Add(kAdBlockingListsPref, base::BindRepeating([](ProfileState* state) {
      ContentBlocking::Get().Rebuild(state->profile);
      ContentBlocking::Get().DownloadStale(false);
      ContentBlocking::Get().Notify();
    }, base::Unretained(this)));
    ReadSwitches();
    // Optional lists this profile has on may never have been downloaded: look
    // once the first pages had their turn.
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(FROM_HERE, base::BindOnce([] {
      ContentBlocking::Get().DownloadStale(false);
    }), base::Seconds(30));
  }
  void ReadSwitches() {
    enabled = profile->GetPrefs()->GetBoolean(kAdBlockingPref);
    allowed.clear();
    for (const auto& site : StringList(profile->GetPrefs()->GetList(kAdBlockingAllowedSitesPref))) allowed.insert(site);
    // Pages change their behavior: tabs re-apply, the bar repaints.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce(
        [](base::WeakPtr<ProfileState> state) { if (state) ContentBlocking::Get().Notify(); }, weak_factory.GetWeakPtr()));
  }
  bool Allowed(const GURL& page) const {
    std::string host = Normalize(std::string(page.host()));
    while (!host.empty()) {
      if (allowed.contains(host)) return true;
      const auto dot = host.find('.');
      if (dot == std::string::npos) break;
      host.erase(0, dot + 1);
    }
    return false;
  }
  raw_ptr<Profile> profile;
  PrefChangeRegistrar registrar;
  bool enabled = true;
  std::set<std::string> allowed;
  std::optional<rust::Box<adblock::Engine>> engine;
  uint64_t generation = 0, built = 0;
  // page_scripts() of `engine`, for the generation it was built from.
  std::vector<std::pair<std::string, std::string>> scripts;
  uint64_t scripts_built = 0;
  base::WeakPtrFactory<ProfileState> weak_factory{this};
};

ContentBlocking& ContentBlocking::Get() {
  static base::NoDestructor<ContentBlocking> instance;
  return *instance;
}
ContentBlocking::ContentBlocking()
    : list_store_(
          base::BindRepeating(&ContentBlocking::Wanted, base::Unretained(this)),
          base::BindRepeating([]() -> network::mojom::URLLoaderFactory* {
            return g_browser_process
                       ? g_browser_process->shared_url_loader_factory().get()
                       : nullptr;
          }),
          base::BindRepeating(&ContentBlocking::RebuildAll, base::Unretained(this)),
          base::BindRepeating(&ContentBlocking::Notify, base::Unretained(this))) {
  ConfigureAdblockDomainResolver();
}
ContentBlocking::~ContentBlocking() = default;

base::CallbackListSubscription ContentBlocking::Observe(base::RepeatingClosure changed) {
  return observers_.Add(std::move(changed));
}
void ContentBlocking::Notify() { observers_.Notify(); }

void ContentBlocking::Start() {
  base::FilePath user_data;
  if (base::PathService::Get(chrome::DIR_USER_DATA, &user_data))
    list_store_.Start(user_data.AppendASCII("Zitter Filter Lists"), true);
}

void ContentBlocking::UpdateLists() {
  Start();
  DownloadStale(true);
}

void ContentBlocking::DownloadStale(bool force) {
  list_store_.Refresh(force);
}

void ContentBlocking::SetListsForTesting(std::vector<std::string> texts) {
  list_store_.SetTextsForTesting(std::move(texts));
}

ContentBlocking::ProfileState* ContentBlocking::StateFor(Profile* profile, bool build) {
  if (!profile) return nullptr;
  auto* state = static_cast<ProfileState*>(profile->GetUserData(kStateKey));
  if (!state && build) {
    Start();
    auto created = std::make_unique<ProfileState>(profile);
    state = created.get();
    profile->SetUserData(kStateKey, std::move(created));
    Rebuild(profile);
  }
  return state;
}
void ContentBlocking::Rebuild(Profile* profile) {
  auto* state = StateFor(profile, false);
  if (!state) return;
  const uint64_t generation = ++state->generation;
  std::vector<std::string> rules = Rules(profile);
  std::vector<bool> enabled;
  for (const auto& list : list_store_.lists()) enabled.push_back(ListEnabled(profile, list.id));
  base::ThreadPool::PostTaskAndReplyWithResult(FROM_HERE, {base::TaskPriority::USER_VISIBLE},
      base::BindOnce([](std::shared_ptr<const std::vector<std::string>> texts, std::vector<bool> enabled, std::vector<std::string> rules) {
        rust::Vec<rust::String> lists;
        for (size_t i = 0; i < texts->size(); ++i) if (i >= enabled.size() || enabled[i]) lists.push_back(rust::String((*texts)[i]));
        lists.push_back(rust::String(base::JoinString(rules, "\n")));
        return adblock::new_engine(lists);
      }, list_store_.texts(), std::move(enabled), std::move(rules)),
      base::BindOnce([](base::WeakPtr<ProfileState> state, uint64_t generation, rust::Box<adblock::Engine> engine) {
        if (!state || state->generation != generation) return;
        state->engine.emplace(std::move(engine));
        state->built = generation;
        ContentBlocking::Get().Notify();
      }, state->weak_factory.GetWeakPtr(), generation));
}
void ContentBlocking::RebuildAll() {
  // Every profile with an engine; the others build on first use.
  for (auto* profile : g_browser_process && g_browser_process->profile_manager() ?
                       g_browser_process->profile_manager()->GetLoadedProfiles() : std::vector<Profile*>()) {
    if (StateFor(profile, false)) Rebuild(profile);
    for (auto* private_profile : profile->GetAllOffTheRecordProfiles())
      if (StateFor(private_profile, false)) Rebuild(private_profile);
  }
  Notify();
}
bool ContentBlocking::ReadyForTesting(Profile* profile) {
  auto* state = StateFor(profile, true);
  return state && state->engine && state->built == state->generation;
}

bool ContentBlocking::EnabledFor(Profile* profile, const GURL& page) {
  auto* state = Get().StateFor(profile, true);
  return state && state->enabled && page.SchemeIsHTTPOrHTTPS() && !state->Allowed(page);
}
std::string ContentBlocking::SiteKey(const GURL& page) {
  return page.SchemeIsHTTPOrHTTPS() ? Normalize(std::string(page.host())) : std::string();
}
std::string ContentBlocking::RegistrableDomain(const GURL& page) {
  auto domain = net::registry_controlled_domains::GetDomainAndRegistry(page, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  return domain.empty() ? SiteKey(page) : domain;
}
bool ContentBlocking::SiteAllowed(Profile* profile, const GURL& page) {
  auto* state = Get().StateFor(profile, true);
  return state && state->Allowed(page);
}
void ContentBlocking::SetSiteAllowed(Profile* profile, const std::string& site, bool allowed) {
  const auto key = Normalize(site);
  if (!profile || key.empty()) return;
  ScopedListPrefUpdate update(profile->GetPrefs(), kAdBlockingAllowedSitesPref);
  update->EraseIf([&](const base::Value& value) { return value.is_string() && Normalize(value.GetString()) == key; });
  if (allowed) update->Append(key);
}
bool ContentBlocking::ListEnabled(Profile* profile, const std::string& id) {
  const auto& sources = AdblockFilterListSources();
  const auto source = std::ranges::find(sources, id, &AdblockFilterList::id);
  if (source == sources.end()) return false;
  if (!source->optional) return true;
  if (!profile) return source->default_on;
  return profile->GetPrefs()->GetDict(kAdBlockingListsPref)
      .FindBool(id).value_or(source->default_on);
}
void ContentBlocking::SetListEnabled(Profile* profile, const std::string& id, bool enabled) {
  const auto& sources = AdblockFilterListSources();
  const auto source = std::ranges::find(sources, id, &AdblockFilterList::id);
  if (!profile || source == sources.end() || !source->optional) return;
  ScopedDictPrefUpdate(profile->GetPrefs(), kAdBlockingListsPref)->Set(id, enabled);
}
bool ContentBlocking::Wanted(const AdblockFilterList& list) {
  if (!list.optional) return true;
  for (auto* profile : g_browser_process && g_browser_process->profile_manager() ?
                       g_browser_process->profile_manager()->GetLoadedProfiles() : std::vector<Profile*>())
    if (!profile->IsOffTheRecord() && ListEnabled(profile, list.id)) return true;
  return false;
}

std::vector<std::string> ContentBlocking::AllowedSites(Profile* profile) {
  return profile ? StringList(profile->GetPrefs()->GetList(kAdBlockingAllowedSitesPref)) : std::vector<std::string>();
}

std::vector<std::string> ContentBlocking::Rules(Profile* profile) {
  return profile ? StringList(profile->GetPrefs()->GetList(kAdBlockingRulesPref)) : std::vector<std::string>();
}
std::string ContentBlocking::RuleError(const std::string& rule) {
  return AdblockRuleError(rule);
}
bool ContentBlocking::RuleInScope(const std::string& rule, const std::string& site) {
  return adblock::rule_in_scope(rule, site);
}
bool ContentBlocking::AddRules(Profile* profile, const std::vector<std::string>& rules, const std::string& comment, std::string* error) {
  if (!profile) return false;
  for (const auto& rule : rules) {
    if (auto problem = RuleError(rule); !problem.empty()) { if (error) *error = rule + ": " + problem; return false; }
  }
  const auto existing = Rules(profile);
  std::vector<std::string> added;
  for (const auto& raw : rules) {
    std::string rule(base::TrimWhitespaceASCII(raw, base::TRIM_ALL));
    if (rule.empty() || std::ranges::contains(existing, rule) || std::ranges::contains(added, rule)) continue;
    added.push_back(std::move(rule));
  }
  if (added.empty()) return true;
  {
    ScopedDictPrefUpdate misses(profile->GetPrefs(), kAdBlockingRuleMissesPref);
    for (const auto& rule : added) misses->Remove(rule);
  }
  ScopedListPrefUpdate update(profile->GetPrefs(), kAdBlockingRulesPref);
  if (!comment.empty()) update->Append("! " + comment);
  for (auto& rule : added) update->Append(std::move(rule));
  return true;
}
void ContentBlocking::RemoveRule(Profile* profile, const std::string& rule) {
  if (!profile) return;
  ScopedDictPrefUpdate(profile->GetPrefs(), kAdBlockingRuleMissesPref)->Remove(rule);
  ScopedListPrefUpdate update(profile->GetPrefs(), kAdBlockingRulesPref);
  auto& list = update.Get();
  for (size_t i = 0; i < list.size(); ++i) {
    if (!list[i].is_string() || list[i].GetString() != rule) continue;
    list.erase(list.begin() + i);
    // A comment line left with no rule under it goes too.
    if (i > 0 && list[i - 1].is_string() && list[i - 1].GetString().starts_with("!") &&
        (i == list.size() || (list[i].is_string() && list[i].GetString().starts_with("!"))))
      list.erase(list.begin() + i - 1);
    return;
  }
}

std::string ContentBlocking::HidingSelector(const std::string& rule) {
  for (const char* separator : {"##", "#?#"}) {
    const auto at = rule.find(separator);
    if (at == std::string::npos || rule.starts_with("!")) continue;
    // Scriptlets (`##+js(`) run in the page's own world, not as selectors.
    auto selector = rule.substr(at + std::char_traits<char>::length(separator));
    return selector.starts_with("+js(") ? std::string() : selector;
  }
  return std::string();
}
std::vector<std::string> ContentBlocking::SiteHidingRules(Profile* profile, const GURL& page) {
  std::vector<std::string> rules;
  const std::string site = RegistrableDomain(page);
  if (site.empty()) return rules;
  for (const auto& rule : Rules(profile)) if (!HidingSelector(rule).empty() && RuleInScope(rule, site)) rules.push_back(rule);
  return rules;
}
void ContentBlocking::RecordRuleHits(Profile* profile, const std::vector<std::pair<std::string, int>>& hits) {
  if (!profile || hits.empty()) return;
  ScopedDictPrefUpdate misses(profile->GetPrefs(), kAdBlockingRuleMissesPref);
  for (const auto& [rule, count] : hits) {
    if (count > 0) misses->Remove(rule);
    else misses->Set(rule, std::min(misses->FindInt(rule).value_or(0) + 1, 99));
  }
}
bool ContentBlocking::RuleStale(Profile* profile, const std::string& rule) {
  return profile && profile->GetPrefs()->GetDict(kAdBlockingRuleMissesPref).FindInt(rule).value_or(0) >= kStaleLoads;
}
std::string ContentBlocking::RuleSite(const std::string& rule) {
  return AdblockRuleSite(rule);
}
std::vector<std::pair<size_t, std::string>> ContentBlocking::TextErrors(const std::string& text) {
  return AdblockTextErrors(text);
}
bool ContentBlocking::SetRulesText(Profile* profile, const std::string& text) {
  if (!profile || !TextErrors(text).empty()) return false;
  base::ListValue rules;
  std::set<std::string> kept;
  for (auto line : base::SplitStringPiece(text, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    rules.Append(std::string(line));
    if (!line.starts_with("!")) kept.insert(std::string(line));
  }
  // Counters of rules that are gone go with them.
  {
    ScopedDictPrefUpdate misses(profile->GetPrefs(), kAdBlockingRuleMissesPref);
    std::vector<std::string> gone;
    for (const auto [rule, value] : *misses) if (!kept.contains(rule)) gone.push_back(rule);
    for (const auto& rule : gone) misses->Remove(rule);
  }
  profile->GetPrefs()->SetList(kAdBlockingRulesPref, std::move(rules));
  return true;
}
std::vector<std::string> ContentBlocking::StaleRules(Profile* profile, const GURL& page) {
  std::vector<std::string> stale;
  if (!profile) return stale;
  const auto& misses = profile->GetPrefs()->GetDict(kAdBlockingRuleMissesPref);
  for (const auto& rule : SiteHidingRules(profile, page))
    if (misses.FindInt(rule).value_or(0) >= kStaleLoads) stale.push_back(rule);
  return stale;
}

ContentBlocking::Verdict ContentBlocking::Check(Profile* profile, const GURL& url, const GURL& page, const std::string& type,
                                                std::string* redirect) {
  auto* state = StateFor(profile, true);
  if (!state || !state->enabled || state->Allowed(page)) return Verdict::kOff;
  if (!state->engine || !url.SchemeIsHTTPOrHTTPS()) return Verdict::kAllow;
  const auto check = (*state->engine)->check_request(url.spec(), page.spec(), type);
  if (redirect) *redirect = std::string(check.redirect);
  return check.verdict == 1 ? Verdict::kBlock : Verdict::kAllow;
}
std::vector<std::pair<std::string, std::string>> ContentBlocking::PageScripts(Profile* profile) {
  auto* state = StateFor(profile, true);
  if (!state || !state->engine) return {};
  if (state->scripts_built != state->built) {
    state->scripts.clear();
    for (const auto& script : (*state->engine)->page_scripts()) state->scripts.emplace_back(std::string(script.host), std::string(script.script));
    state->scripts_built = state->built;
  }
  return state->scripts;
}
ContentBlocking::Cosmetic ContentBlocking::CosmeticFor(Profile* profile, const GURL& url) {
  auto* state = StateFor(profile, true);
  if (!state || !state->engine || !url.SchemeIsHTTPOrHTTPS()) return {};
  const auto resources = (*state->engine)->cosmetic_resources(url.spec());
  return {Strings(resources.hide_selectors), Strings(resources.procedural), Strings(resources.exceptions), resources.generichide};
}
std::vector<std::string> ContentBlocking::GenericSelectors(Profile* profile, const std::vector<std::string>& classes,
                                                           const std::vector<std::string>& ids, const std::vector<std::string>& exceptions) {
  auto* state = StateFor(profile, true);
  if (!state || !state->engine) return {};
  return Strings((*state->engine)->hidden_class_id_selectors(RustStrings(classes), RustStrings(ids), RustStrings(exceptions)));
}

void ContentBlocking::WillCreateURLLoaderFactory(
    content::BrowserContext* context,
    content::RenderFrameHost* frame,
    content::ContentBrowserClient::URLLoaderFactoryType type,
    const net::IsolationInfo& isolation,
    network::URLLoaderFactoryBuilder& builder) {
  auto* profile = Profile::FromBrowserContext(context);
  if (!profile || !(profile->IsRegularProfile() || profile->IsOffTheRecord()))
    return;
  GURL page = AdblockTopLevelPage(frame, type, isolation);
  if (page.is_empty())
    return;
  auto* state = ContentBlocking::Get().StateFor(profile, true);
  if (!state)
    return;

  AppendAdblockFactory(
      builder, std::move(page),
      base::BindRepeating(
          [](base::WeakPtr<ProfileState> state,
             content::GlobalRenderFrameHostId frame_id,
             const GURL& url, const GURL& page, const std::string& type,
             std::string* redirect) {
            if (!state)
              return false;
            ContentBlockingTabHelper* helper = nullptr;
            if (auto* frame = content::RenderFrameHost::FromID(frame_id)) {
              if (auto* contents = content::WebContents::FromRenderFrameHost(frame))
                helper = ContentBlockingTabHelper::FromWebContents(contents);
            }
            const bool blocked =
                helper && helper->has_trial()
                    ? helper->TrialBlocks(url, page, type, redirect)
                    : ContentBlocking::Get().Check(state->profile, url, page,
                                                   type, redirect) ==
                          ContentBlocking::Verdict::kBlock;
            if (blocked && helper)
              helper->RecordBlocked(url);
            return blocked;
          },
          state->weak_factory.GetWeakPtr(),
          frame ? frame->GetGlobalId() : content::GlobalRenderFrameHostId()));
}

// Tabs ----------------------------------------------------------------------
WEB_CONTENTS_USER_DATA_KEY_IMPL(ContentBlockingTabHelper);

// A small engine of the rules on trial and the site they are for.
struct ContentBlockingTabHelper::Trial {
  rust::Box<adblock::Engine> engine;
  std::string site;
};

ContentBlockingTabHelper::ContentBlockingTabHelper(content::WebContents* contents)
    : content::WebContentsObserver(contents), content::WebContentsUserData<ContentBlockingTabHelper>(*contents) {
  rules_changed_ = ContentBlocking::Get().Observe(base::BindRepeating(&ContentBlockingTabHelper::Reapply, base::Unretained(this)));
  ApplyPageScripts();
}
ContentBlockingTabHelper::~ContentBlockingTabHelper() = default;
void ContentBlockingTabHelper::ApplyPageScripts() {
  auto* profile = this->profile();
  std::vector<std::string> next;
  auto add = [&](const std::string& host, const std::string& script) {
    if (ContentBlocking::EnabledFor(profile, GURL("https://" + host + "/"))) next.push_back(host + "\n" + script);
  };
  if (trial_) for (const auto& script : trial_->engine->page_scripts()) add(std::string(script.host), std::string(script.script));
  else for (const auto& [host, script] : ContentBlocking::Get().PageScripts(profile)) add(host, script);
  if (!probe_.empty()) next.push_back("*\n" + probe_);
  SetAdblockPageScripts(GetWebContents(), std::move(next));
}
void ContentBlockingTabHelper::SetTrial(std::vector<std::string> rules, base::OnceCallback<void(std::string)> done) {
  for (const auto& rule : rules)
    if (auto problem = ContentBlocking::RuleError(rule); !problem.empty()) { std::move(done).Run(rule + ": " + problem); return; }
  // The whole engine, so an exception on trial (@@) can lift a list's block.
  auto& blocking = ContentBlocking::Get();
  std::vector<std::string> own = ContentBlocking::Rules(profile());
  own.insert(own.end(), rules.begin(), rules.end());
  base::ThreadPool::PostTaskAndReplyWithResult(FROM_HERE, {base::TaskPriority::USER_BLOCKING},
      base::BindOnce([](std::shared_ptr<const std::vector<std::string>> texts, std::vector<std::string> rules) {
        rust::Vec<rust::String> lists;
        if (texts) for (const auto& text : *texts) lists.push_back(rust::String(text));
        lists.push_back(rust::String(base::JoinString(rules, "\n")));
        return adblock::new_engine(lists);
      }, blocking.list_store_.texts(), std::move(own)),
      base::BindOnce([](base::WeakPtr<ContentBlockingTabHelper> self, base::OnceCallback<void(std::string)> done, rust::Box<adblock::Engine> engine) {
        if (!self) { std::move(done).Run("The tab closed"); return; }
        self->trial_ = std::make_unique<Trial>(Trial{std::move(engine), ContentBlocking::RegistrableDomain(self->GetWebContents().GetLastCommittedURL())});
        self->ApplyPageScripts();
        std::move(done).Run(std::string());
      }, weak_factory_.GetWeakPtr(), std::move(done)));
}
void ContentBlockingTabHelper::ClearTrial() {
  if (!trial_) return;
  trial_.reset();
  ApplyPageScripts();
}
bool ContentBlockingTabHelper::TrialBlocks(const GURL& url, const GURL& page, const std::string& type, std::string* redirect) {
  if (!trial_ || !url.SchemeIsHTTPOrHTTPS() || !ContentBlocking::EnabledFor(profile(), page)) return false;
  const auto check = trial_->engine->check_request(url.spec(), page.spec(), type);
  if (check.verdict != 1) return false;
  if (redirect) *redirect = std::string(check.redirect);
  return true;
}
void ContentBlockingTabHelper::SetProbe(std::string script) {
  probe_ = std::move(script);
  ApplyPageScripts();
}
void ContentBlockingTabHelper::Attach(content::WebContents* contents) {
  CreateForWebContents(contents);
}
Profile* ContentBlockingTabHelper::profile() {
  return Profile::FromBrowserContext(GetWebContents().GetBrowserContext());
}
base::CallbackListSubscription ContentBlockingTabHelper::ObserveCount(base::RepeatingClosure changed) {
  return count_changed_.Add(std::move(changed));
}
void ContentBlockingTabHelper::RecordBlocked(const GURL& url) {
  ++blocked_requests_;
  if (blocked_urls_.size() < 300) blocked_urls_.push_back(CutQuery(url));
  count_changed_.Notify();
}
void ContentBlockingTabHelper::SetHidden(int hidden) {
  if (hidden == hidden_elements_) return;
  hidden_elements_ = hidden;
  count_changed_.Notify();
}
void ContentBlockingTabHelper::ReadyToCommitNavigation(content::NavigationHandle* navigation) {
  // The count belongs to the page: a new document starts at zero, before its
  // first subresource can be blocked.
  if (!navigation->IsInPrimaryMainFrame() || navigation->IsSameDocument()) return;
  // A trial is for its site: leaving it ends the trial.
  if (trial_ && ContentBlocking::RegistrableDomain(navigation->GetURL()) != trial_->site) ClearTrial();
  rescan_.Stop();
  health_.Stop();
  blocked_requests_ = 0;
  hidden_elements_ = 0;
  blocked_urls_.clear();
  count_changed_.Notify();
}
void ContentBlockingTabHelper::DidFinishNavigation(content::NavigationHandle* navigation) {
  if (!navigation->HasCommitted() || navigation->IsSameDocument() || navigation->IsErrorPage()) return;
  if (auto* frame = navigation->GetRenderFrameHost()) Style(frame);
}
void ContentBlockingTabHelper::DOMContentLoaded(content::RenderFrameHost* frame) { Scan(frame); }
void ContentBlockingTabHelper::DidFinishLoad(content::RenderFrameHost* frame, const GURL&) {
  Scan(frame);
  if (!frame->IsInPrimaryMainFrame()) return;
  // Late ads: one more look once the page has settled.
  rescan_.Start(FROM_HERE, base::Seconds(3), base::BindOnce([](base::WeakPtr<ContentBlockingTabHelper> self) {
    if (self) self->Scan(self->GetWebContents().GetPrimaryMainFrame());
  }, weak_factory_.GetWeakPtr()));
  // Late and lazy ads have had their chance by then.
  health_.Start(FROM_HERE, base::Seconds(12), base::BindOnce(&ContentBlockingTabHelper::CheckRuleHealth, weak_factory_.GetWeakPtr()));
}
void ContentBlockingTabHelper::CheckRuleHealth() {
  auto* frame = GetWebContents().GetPrimaryMainFrame();
  const GURL page = frame->GetLastCommittedURL();
  if (!ContentBlocking::EnabledFor(profile(), page)) return;
  auto rules = ContentBlocking::SiteHidingRules(profile(), page);
  if (rules.empty()) return;
  base::ListValue selectors;
  for (const auto& rule : rules) selectors.Append(ContentBlocking::HidingSelector(rule));
  Run(frame, base::DictValue().Set("op", "count").Set("selectors", std::move(selectors)), base::BindOnce(
      [](base::WeakPtr<ContentBlockingTabHelper> self, std::vector<std::string> rules, base::Value result) {
        const auto* counts = result.is_dict() ? result.GetDict().FindDict("counts") : nullptr;
        if (!self || !counts) return;
        std::vector<std::pair<std::string, int>> hits;
        for (auto& rule : rules) {
          const auto count = counts->FindInt(ContentBlocking::HidingSelector(rule));
          if (count) hits.emplace_back(std::move(rule), *count);
        }
        ContentBlocking::RecordRuleHits(self->profile(), hits);
      }, weak_factory_.GetWeakPtr(), std::move(rules)));
}
void ContentBlockingTabHelper::Run(content::RenderFrameHost* frame, base::DictValue args, base::OnceCallback<void(base::Value)> done) {
  RunAdblockCosmeticScript(frame, std::move(args), std::move(done));
}
void ContentBlockingTabHelper::Style(content::RenderFrameHost* frame) {
  const GURL page = frame->GetOutermostMainFrame()->GetLastCommittedURL();
  const bool on = ContentBlocking::EnabledFor(profile(), page);
  const auto cosmetic = on ? ContentBlocking::Get().CosmeticFor(profile(), frame->GetLastCommittedURL()) : ContentBlocking::Cosmetic();
  base::ListValue selectors, actions;
  for (const auto& selector : cosmetic.hide) selectors.Append(selector);
  for (const auto& action : cosmetic.procedural) actions.Append(action);
  Run(frame, base::DictValue().Set("op", "style").Set("key", "site").Set("selectors", std::move(selectors)).Set("actions", std::move(actions)),
      base::BindOnce([](base::WeakPtr<ContentBlockingTabHelper> self, bool main, base::Value result) {
        if (self && main && result.is_dict()) self->SetHidden(result.GetDict().FindInt("hidden").value_or(0));
      }, weak_factory_.GetWeakPtr(), frame->IsInPrimaryMainFrame()));
  // Rules from the assistant applied at once stay only until the engine has them.
  if (!on) Run(frame, base::DictValue().Set("op", "style").Set("key", "assistant").Set("selectors", base::ListValue()), base::DoNothing());
}
void ContentBlockingTabHelper::Scan(content::RenderFrameHost* frame, bool reset) {
  const GURL page = frame->GetOutermostMainFrame()->GetLastCommittedURL();
  if (!ContentBlocking::EnabledFor(profile(), page)) {
    Run(frame, base::DictValue().Set("op", "style").Set("key", "generic").Set("selectors", base::ListValue()), base::DoNothing());
    return;
  }
  const auto cosmetic = ContentBlocking::Get().CosmeticFor(profile(), frame->GetLastCommittedURL());
  if (cosmetic.generichide) return;
  Run(frame, base::DictValue().Set("op", "scan").Set("reset", reset), base::BindOnce(
      [](base::WeakPtr<ContentBlockingTabHelper> self, content::WeakDocumentPtr document, std::vector<std::string> exceptions, base::Value result) {
        auto* frame = document.AsRenderFrameHostIfValid();
        if (!self || !frame || !result.is_dict()) return;
        const auto* classes = result.GetDict().FindList("classes");
        const auto* ids = result.GetDict().FindList("ids");
        const auto selectors = ContentBlocking::Get().GenericSelectors(self->profile(), classes ? StringList(*classes) : std::vector<std::string>(),
                                                                       ids ? StringList(*ids) : std::vector<std::string>(), exceptions);
        if (selectors.empty()) {
          if (frame->IsInPrimaryMainFrame()) self->SetHidden(result.GetDict().FindInt("hidden").value_or(0));
          return;
        }
        base::ListValue list;
        for (const auto& selector : selectors) list.Append(selector);
        self->Run(frame, base::DictValue().Set("op", "style").Set("key", "generic").Set("append", true).Set("selectors", std::move(list)),
            base::BindOnce([](base::WeakPtr<ContentBlockingTabHelper> self, bool main, base::Value result) {
              if (self && main && result.is_dict()) self->SetHidden(result.GetDict().FindInt("hidden").value_or(0));
            }, self, frame->IsInPrimaryMainFrame()));
      }, weak_factory_.GetWeakPtr(), frame->GetWeakDocumentPtr(), cosmetic.exceptions));
}
void ContentBlockingTabHelper::Reapply() {
  ApplyPageScripts();
  GetWebContents().GetPrimaryMainFrame()->ForEachRenderFrameHost([this](content::RenderFrameHost* frame) {
    if (!frame->IsRenderFrameLive() || !frame->GetLastCommittedURL().SchemeIsHTTPOrHTTPS()) return;
    Style(frame);
    Scan(frame, /*reset=*/true);
  });
}
void ContentBlockingTabHelper::HideNow(const std::vector<std::string>& selectors, base::OnceCallback<void(int)> done) {
  base::ListValue list;
  for (const auto& selector : selectors) list.Append(selector);
  Run(GetWebContents().GetPrimaryMainFrame(), base::DictValue().Set("op", "style").Set("key", "assistant").Set("append", true).Set("selectors", std::move(list)),
      base::BindOnce([](base::WeakPtr<ContentBlockingTabHelper> self, base::OnceCallback<void(int)> done, base::Value result) {
        const int hidden = result.is_dict() ? result.GetDict().FindInt("hidden").value_or(0) : 0;
        if (self) self->SetHidden(hidden);
        std::move(done).Run(hidden);
      }, weak_factory_.GetWeakPtr(), std::move(done)));
}
void ContentBlockingTabHelper::Preview(const std::vector<std::string>& selectors, base::OnceCallback<void(base::DictValue)> done) {
  base::ListValue list;
  for (const auto& selector : selectors) list.Append(selector);
  Run(GetWebContents().GetPrimaryMainFrame(), base::DictValue().Set("op", "preview").Set("selectors", std::move(list)),
      base::BindOnce([](base::OnceCallback<void(base::DictValue)> done, base::Value result) {
        std::move(done).Run(result.is_dict() ? std::move(result).TakeDict() : base::DictValue());
      }, std::move(done)));
}

}  // namespace zitter::prototype
