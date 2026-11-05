// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/prototype/content_blocking.h"

#include <algorithm>
#include <set>
#include <utility>

#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/no_destructor.h"
#include "base/memory/raw_ptr.h"
#include "base/path_service.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/supports_user_data.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/chrome_paths.h"
#include "chromium/prototype/adblock.rs.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "content/public/browser/global_routing_id.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/isolated_world_ids.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"
#include "mojo/public/cpp/system/data_pipe.h"
#include "net/base/data_url.h"
#include "third_party/blink/public/common/renderer_preferences/renderer_preferences.h"
#include "url/origin.h"
#include "content_blocking_script.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/base/isolation_info.h"
#include "net/base/net_errors.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "net/http/http_response_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/cpp/url_loader_completion_status.h"
#include "services/network/public/cpp/url_loader_factory_builder.h"
#include "services/network/public/mojom/url_loader.mojom.h"
#include "services/network/public/mojom/url_loader_factory.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"

extern "C" void zitter_adblock_set_domain_resolver(void (*)(const uint8_t*, size_t, size_t*, size_t*));

namespace zitter::prototype {
namespace {
// Zitter's isolated world, shared with the assistant (workspace_assistant.cc).
constexpr int kZitterWorld = content::ISOLATED_WORLD_ID_CONTENT_END;
constexpr char kStateKey[] = "zitter-content-blocking";
constexpr base::TimeDelta kListAge = base::Days(4);
constexpr size_t kMaxList = 8 * 1024 * 1024;
struct ListSource { const char* id; const char* name; const char* url; };
// GPLv3 / CC BY-SA 3.0 data, downloaded, never compiled in (docs/ad-blocking.md).
constexpr ListSource kLists[] = {
    {"easylist", "EasyList", "https://easylist.to/easylist/easylist.txt"},
    {"easyprivacy", "EasyPrivacy", "https://easylist.to/easylist/easyprivacy.txt"},
};
constexpr net::NetworkTrafficAnnotationTag kListTraffic = net::DefineNetworkTrafficAnnotation("zitter_filter_lists", R"(
    semantics {
      sender: "Zitter ad blocking"
      description: "Downloads the EasyList and EasyPrivacy filter lists."
      trigger: "Startup and every day while running, when a list is missing or older than four days, and Settings > Ad blocking > Update now."
      data: "None. No cookies or credentials."
      destination: OTHER
      destination_other: "easylist.to"
    }
    policy {
      cookies_allowed: NO
      setting: "Settings > Ad blocking > Block ads and trackers."
      policy_exception_justification: "Not implemented."
    })");

void ResolveDomain(const uint8_t* host, size_t length, size_t* start, size_t* end) {
  const std::string_view name(reinterpret_cast<const char*>(host), length);
  const auto domain = net::registry_controlled_domains::GetDomainAndRegistry(
      name, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  *start = domain.empty() || domain.size() > length ? 0 : length - domain.size();
  *end = length;
}
std::string Normalize(std::string host) {
  host = base::ToLowerASCII(base::TrimWhitespaceASCII(host, base::TRIM_ALL));
  while (host.ends_with('.')) host.pop_back();
  if (host.starts_with("www.")) host.erase(0, 4);
  return host;
}
// adblock-rust's request types, from what the page asked the resource to be.
std::string RequestType(const network::ResourceRequest& request) {
  using D = network::mojom::RequestDestination;
  switch (request.destination) {
    case D::kScript: case D::kWorker: case D::kSharedWorker: case D::kServiceWorker:
    case D::kAudioWorklet: case D::kPaintWorklet: return "script";
    case D::kImage: return "image";
    case D::kStyle: return "stylesheet";
    case D::kFrame: case D::kIframe: case D::kFencedframe: return "sub_frame";
    case D::kFont: return "font";
    case D::kAudio: case D::kVideo: case D::kTrack: return "media";
    case D::kObject: case D::kEmbed: return "object";
    case D::kEmpty: return request.keepalive ? "ping" : "xmlhttprequest";
    default: return "other";
  }
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
    ReadSwitches();
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
ContentBlocking::ContentBlocking() {
  zitter_adblock_set_domain_resolver(&ResolveDomain);
  for (const auto& list : kLists) lists_.push_back({list.id, list.name, list.url});
  texts_ = std::make_shared<const std::vector<std::string>>();
}
ContentBlocking::~ContentBlocking() = default;

base::CallbackListSubscription ContentBlocking::Observe(base::RepeatingClosure changed) {
  return observers_.Add(std::move(changed));
}
void ContentBlocking::Notify() { observers_.Notify(); }

void ContentBlocking::Start() {
  if (started_) return;
  started_ = true;
  base::FilePath user_data;
  if (!base::PathService::Get(chrome::DIR_USER_DATA, &user_data)) return;
  directory_ = user_data.AppendASCII("Zitter Filter Lists");
  std::vector<base::FilePath> paths;
  for (const auto& list : kLists) paths.push_back(directory_.AppendASCII(std::string(list.id) + ".txt"));
  base::ThreadPool::PostTaskAndReplyWithResult(FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce([](std::vector<base::FilePath> paths) {
        std::pair<std::vector<std::optional<std::string>>, std::vector<base::Time>> result;
        for (const auto& path : paths) {
          std::string text;
          base::File::Info info;
          // Rust takes UTF-8 only; a damaged file counts as missing.
          const bool ok = base::ReadFileToStringWithMaxSize(path, &text, kMaxList) && base::GetFileInfo(path, &info) &&
                          base::IsStringUTF8(text);
          result.first.push_back(ok ? std::optional<std::string>(std::move(text)) : std::nullopt);
          result.second.push_back(ok ? info.last_modified : base::Time());
        }
        return result;
      }, std::move(paths)),
      base::BindOnce([](base::WeakPtr<ContentBlocking> self, std::pair<std::vector<std::optional<std::string>>, std::vector<base::Time>> read) {
        if (self) self->ListsLoaded(std::move(read.first), std::move(read.second));
      }, weak_factory_.GetWeakPtr()));
}

void ContentBlocking::ListsLoaded(std::vector<std::optional<std::string>> texts, std::vector<base::Time> times) {
  if (!started_ || directory_.empty()) return;
  // One slot per list, in kLists order; a missing list stays empty.
  auto next = std::make_shared<std::vector<std::string>>(lists_.size());
  bool stale = false;
  for (size_t i = 0; i < lists_.size(); ++i) {
    if (texts[i]) {
      lists_[i].rules = adblock::count_rules(*texts[i]);
      lists_[i].updated = times[i];
      (*next)[i] = std::move(*texts[i]);
    }
    stale |= !texts[i] || base::Time::Now() - times[i] > kListAge;
  }
  texts_ = std::move(next);
  RebuildAll();
  // Never compete with the first pages: the first download waits 30 s.
  ScheduleUpdate(stale ? base::Seconds(30) : base::Hours(24));
}

void ContentBlocking::ScheduleUpdate(base::TimeDelta delay) {
  update_timer_.Start(FROM_HERE, delay, base::BindOnce(&ContentBlocking::DownloadStale, base::Unretained(this), false));
}
void ContentBlocking::UpdateLists() {
  Start();
  DownloadStale(true);
}
void ContentBlocking::DownloadStale(bool force) {
  if (directory_.empty() || !g_browser_process || !g_browser_process->shared_url_loader_factory()) return;
  if (updating_) return;
  for (size_t i = 0; i < lists_.size(); ++i) {
    if (!force && !lists_[i].updated.is_null() && base::Time::Now() - lists_[i].updated < kListAge) continue;
    auto request = std::make_unique<network::ResourceRequest>();
    request->url = GURL(lists_[i].url);
    request->credentials_mode = network::mojom::CredentialsMode::kOmit;
    auto loader = network::SimpleURLLoader::Create(std::move(request), kListTraffic);
    loader->SetRetryOptions(1, network::SimpleURLLoader::RETRY_ON_NETWORK_CHANGE);
    auto* raw = loader.get();
    ++updating_;
    raw->DownloadToString(g_browser_process->shared_url_loader_factory().get(),
        base::BindOnce(&ContentBlocking::Downloaded, weak_factory_.GetWeakPtr(), i, std::move(loader)), kMaxList);
  }
  if (updating_) { last_error_.clear(); Notify(); }
  ScheduleUpdate(base::Hours(24));
}
void ContentBlocking::Downloaded(size_t index, std::unique_ptr<network::SimpleURLLoader> loader, std::optional<std::string> body) {
  --updating_;
  const int status = loader->ResponseInfo() && loader->ResponseInfo()->headers ? loader->ResponseInfo()->headers->response_code() : 0;
  // Only a whole list replaces the one on disk.
  if (!body || status != 200 || !body->starts_with("[Adblock") || !base::IsStringUTF8(*body) || adblock::count_rules(*body) < 1000) {
    last_error_ = lists_[index].name;
    Notify();
    return;
  }
  base::ThreadPool::PostTask(FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT, base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce([](base::FilePath directory, std::string name, std::string text) {
        base::CreateDirectory(directory);
        base::ImportantFileWriter::WriteFileAtomically(directory.AppendASCII(name + ".txt"), text);
      }, directory_, lists_[index].id, *body));
  lists_[index].rules = adblock::count_rules(*body);
  lists_[index].updated = base::Time::Now();
  auto next = std::make_shared<std::vector<std::string>>(*texts_);
  next->resize(std::max(next->size(), index + 1));
  (*next)[index] = std::move(*body);
  texts_ = std::move(next);
  RebuildAll();
}
void ContentBlocking::SetListsForTesting(std::vector<std::string> texts) {
  started_ = true;
  update_timer_.Stop();
  for (size_t i = 0; i < lists_.size(); ++i) {
    lists_[i].rules = i < texts.size() ? adblock::count_rules(texts[i]) : 0;
    lists_[i].updated = base::Time::Now();
  }
  texts_ = std::make_shared<const std::vector<std::string>>(std::move(texts));
  RebuildAll();
}

ContentBlocking::ProfileState* ContentBlocking::StateFor(Profile* profile, bool build) {
  if (!profile) return nullptr;
  profile = profile->GetOriginalProfile();
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
  base::ThreadPool::PostTaskAndReplyWithResult(FROM_HERE, {base::TaskPriority::USER_VISIBLE},
      base::BindOnce([](std::shared_ptr<const std::vector<std::string>> texts, std::vector<std::string> rules) {
        rust::Vec<rust::String> lists;
        for (const auto& text : *texts) lists.push_back(rust::String(text));
        lists.push_back(rust::String(base::JoinString(rules, "\n")));
        return adblock::new_engine(lists);
      }, texts_, std::move(rules)),
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
                       g_browser_process->profile_manager()->GetLoadedProfiles() : std::vector<Profile*>())
    if (StateFor(profile, false)) Rebuild(profile);
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
  ScopedListPrefUpdate update(profile->GetOriginalProfile()->GetPrefs(), kAdBlockingAllowedSitesPref);
  update->EraseIf([&](const base::Value& value) { return value.is_string() && Normalize(value.GetString()) == key; });
  if (allowed) update->Append(key);
}
std::vector<std::string> ContentBlocking::AllowedSites(Profile* profile) {
  return profile ? StringList(profile->GetOriginalProfile()->GetPrefs()->GetList(kAdBlockingAllowedSitesPref)) : std::vector<std::string>();
}

std::vector<std::string> ContentBlocking::Rules(Profile* profile) {
  return profile ? StringList(profile->GetOriginalProfile()->GetPrefs()->GetList(kAdBlockingRulesPref)) : std::vector<std::string>();
}
std::string ContentBlocking::RuleError(const std::string& rule) {
  const auto check = adblock::check_rule(rule);
  return check.kind == 0 ? std::string(check.error.empty() ? "invalid rule" : check.error) : std::string();
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
    ScopedDictPrefUpdate misses(profile->GetOriginalProfile()->GetPrefs(), kAdBlockingRuleMissesPref);
    for (const auto& rule : added) misses->Remove(rule);
  }
  ScopedListPrefUpdate update(profile->GetOriginalProfile()->GetPrefs(), kAdBlockingRulesPref);
  if (!comment.empty()) update->Append("! " + comment);
  for (auto& rule : added) update->Append(std::move(rule));
  return true;
}
void ContentBlocking::RemoveRule(Profile* profile, const std::string& rule) {
  if (!profile) return;
  ScopedDictPrefUpdate(profile->GetOriginalProfile()->GetPrefs(), kAdBlockingRuleMissesPref)->Remove(rule);
  ScopedListPrefUpdate update(profile->GetOriginalProfile()->GetPrefs(), kAdBlockingRulesPref);
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
  ScopedDictPrefUpdate misses(profile->GetOriginalProfile()->GetPrefs(), kAdBlockingRuleMissesPref);
  for (const auto& [rule, count] : hits) {
    if (count > 0) misses->Remove(rule);
    else misses->Set(rule, std::min(misses->FindInt(rule).value_or(0) + 1, 99));
  }
}
std::vector<std::string> ContentBlocking::StaleRules(Profile* profile, const GURL& page) {
  std::vector<std::string> stale;
  if (!profile) return stale;
  const auto& misses = profile->GetOriginalProfile()->GetPrefs()->GetDict(kAdBlockingRuleMissesPref);
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

namespace {
// The loader of a request Zitter answered itself: nothing left to do.
class ServedLoader final : public network::mojom::URLLoader {
 public:
  void FollowRedirect(network::HttpRequestHeadersUpdateParams, const std::optional<GURL>&) override {}
  void SetPriority(net::RequestPriority, int32_t) override {}
};
// `$redirect=`: a 200 response with the body of a resource Zitter ships
// (adblock_resources.js), so bait requests look like they loaded.
bool Serve(const std::string& data_url, const network::ResourceRequest& request,
           mojo::PendingReceiver<network::mojom::URLLoader> loader, mojo::PendingRemote<network::mojom::URLLoaderClient> client) {
  std::string mime, charset, body;
  if (!net::DataURL::Parse(GURL(data_url), &mime, &charset, &body)) return false;
  mojo::ScopedDataPipeProducerHandle producer;
  mojo::ScopedDataPipeConsumerHandle consumer;
  if (mojo::CreateDataPipe(std::max<size_t>(body.size(), 1), producer, consumer) != MOJO_RESULT_OK) return false;
  if (!body.empty() && producer->WriteAllData(base::as_byte_span(body)) != MOJO_RESULT_OK) return false;
  producer.reset();
  auto head = network::mojom::URLResponseHead::New();
  head->mime_type = mime;
  head->charset = charset;
  head->content_length = static_cast<int64_t>(body.size());
  head->headers = net::HttpResponseHeaders::TryToCreate("HTTP/1.1 200 OK\r\n\r\n");
  head->headers->SetHeader("Content-Type", mime);
  head->headers->SetHeader("Access-Control-Allow-Origin", "*");
  const bool same_origin = request.request_initiator && request.request_initiator->IsSameOriginWith(request.url);
  head->response_type = same_origin ? network::mojom::FetchResponseType::kBasic
                        : request.mode == network::mojom::RequestMode::kNoCors ? network::mojom::FetchResponseType::kOpaque
                                                                              : network::mojom::FetchResponseType::kCors;
  mojo::MakeSelfOwnedReceiver(std::make_unique<ServedLoader>(), std::move(loader));
  mojo::Remote<network::mojom::URLLoaderClient> remote(std::move(client));
  remote->OnReceiveResponse(std::move(head), std::move(consumer), std::nullopt);
  network::URLLoaderCompletionStatus status(net::OK);
  status.decoded_body_length = base::ByteSize(body.size());
  remote->OnComplete(status);
  return true;
}
// A frame's or worker's URLLoaderFactory in front of the real one: a blocked
// request fails with ERR_BLOCKED_BY_CLIENT before it leaves the browser, or
// gets a neutral body when a `$redirect=` rule names one.
class BlockingFactory final : public network::mojom::URLLoaderFactory {
 public:
  BlockingFactory(Profile* profile, base::WeakPtr<ContentBlocking::ProfileState> state, GURL page,
                  content::GlobalRenderFrameHostId frame,
                  mojo::PendingReceiver<network::mojom::URLLoaderFactory> receiver,
                  mojo::PendingRemote<network::mojom::URLLoaderFactory> target)
      : profile_(profile), state_(std::move(state)), page_(std::move(page)), frame_(frame), target_(std::move(target)) {
    receivers_.Add(this, std::move(receiver));
    receivers_.set_disconnect_handler(base::BindRepeating(&BlockingFactory::Disconnected, base::Unretained(this)));
    target_.set_disconnect_handler(base::BindOnce(&BlockingFactory::Destroy, base::Unretained(this)));
  }
  void CreateLoaderAndStart(mojo::PendingReceiver<network::mojom::URLLoader> loader, int32_t request_id, uint32_t options,
                            const network::ResourceRequest& request, mojo::PendingRemote<network::mojom::URLLoaderClient> client,
                            const net::MutableNetworkTrafficAnnotationTag& traffic_annotation) override {
    // Never the page itself: a blocked top-level document would show only an
    // error. The state lives on the original profile, which `profile_` is, and
    // goes with it.
    ContentBlockingTabHelper* helper = nullptr;
    if (auto* frame = content::RenderFrameHost::FromID(frame_))
      if (auto* contents = content::WebContents::FromRenderFrameHost(frame)) helper = ContentBlockingTabHelper::FromWebContents(contents);
    std::string redirect;
    const bool blocked = state_ && request.destination != network::mojom::RequestDestination::kDocument &&
        ((helper && helper->has_trial() && helper->TrialBlocks(request.url, page_, RequestType(request), &redirect)) ||
         ContentBlocking::Get().Check(profile_, request.url, page_, RequestType(request), &redirect) == ContentBlocking::Verdict::kBlock);
    if (blocked) {
      if (redirect.empty() || !Serve(redirect, request, std::move(loader), std::move(client)))
        if (client) mojo::Remote<network::mojom::URLLoaderClient>(std::move(client))->OnComplete(
            network::URLLoaderCompletionStatus(net::ERR_BLOCKED_BY_CLIENT));
      if (helper) helper->RecordBlocked(request.url);
      return;
    }
    target_->CreateLoaderAndStart(std::move(loader), request_id, options, request, std::move(client), traffic_annotation);
  }
  void Clone(mojo::PendingReceiver<network::mojom::URLLoaderFactory> receiver) override {
    receivers_.Add(this, std::move(receiver));
  }

 private:
  void Disconnected() { if (receivers_.empty()) Destroy(); }
  void Destroy() { delete this; }
  raw_ptr<Profile, DanglingUntriaged> profile_;
  base::WeakPtr<ContentBlocking::ProfileState> state_;
  GURL page_;
  content::GlobalRenderFrameHostId frame_;
  mojo::ReceiverSet<network::mojom::URLLoaderFactory> receivers_;
  mojo::Remote<network::mojom::URLLoaderFactory> target_;
};
}  // namespace

void ContentBlocking::WillCreateURLLoaderFactory(content::BrowserContext* context, content::RenderFrameHost* frame,
                                                 content::ContentBrowserClient::URLLoaderFactoryType type,
                                                 const net::IsolationInfo& isolation, network::URLLoaderFactoryBuilder& builder) {
  using Type = content::ContentBrowserClient::URLLoaderFactoryType;
  if (type != Type::kNavigation && type != Type::kDocumentSubResource && type != Type::kWorkerMainResource &&
      type != Type::kWorkerSubResource && type != Type::kServiceWorkerScript && type != Type::kServiceWorkerSubResource) return;
  auto* profile = Profile::FromBrowserContext(context);
  if (!profile || !(profile->IsRegularProfile() || profile->IsIncognitoProfile())) return;
  // The top-level page: what $domain=, third-party and the site switch see.
  GURL page;
  if (isolation.top_frame_origin() && !isolation.top_frame_origin()->opaque()) page = isolation.top_frame_origin()->GetURL();
  else if (frame) page = frame->GetOutermostMainFrame()->GetLastCommittedOrigin().GetURL();
  if (!page.SchemeIsHTTPOrHTTPS()) return;
  // Main-frame navigations only load the page itself, which is never blocked.
  if (type == Type::kNavigation && (!frame || !frame->GetParentOrOuterDocument())) return;
  auto* state = ContentBlocking::Get().StateFor(profile, true);
  if (!state) return;
  auto [receiver, target] = builder.Append();
  new BlockingFactory(profile->GetOriginalProfile(), state->weak_factory.GetWeakPtr(), std::move(page),
                      frame ? frame->GetGlobalId() : content::GlobalRenderFrameHostId(), std::move(receiver), std::move(target));
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
  for (const auto& [host, script] : ContentBlocking::Get().PageScripts(profile)) add(host, script);
  if (trial_) for (const auto& script : trial_->engine->page_scripts()) add(std::string(script.host), std::string(script.script));
  if (!probe_.empty()) next.push_back("*\n" + probe_);
  auto& scripts = GetWebContents().GetMutableRendererPrefs()->zitter_page_scripts;
  if (scripts == next) return;
  scripts = std::move(next);
  GetWebContents().SyncRendererPrefs();
}
bool ContentBlockingTabHelper::SetTrial(const std::vector<std::string>& rules, std::string* error) {
  for (const auto& rule : rules)
    if (auto problem = ContentBlocking::RuleError(rule); !problem.empty()) { if (error) *error = rule + ": " + problem; return false; }
  rust::Vec<rust::String> lists;
  lists.push_back(rust::String(base::JoinString(rules, "\n")));
  trial_ = std::make_unique<Trial>(Trial{adblock::new_engine(lists), ContentBlocking::RegistrableDomain(GetWebContents().GetLastCommittedURL())});
  ApplyPageScripts();
  return true;
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
  if (!frame || !frame->IsRenderFrameLive() || !frame->GetLastCommittedURL().SchemeIsHTTPOrHTTPS()) {
    std::move(done).Run(base::Value());
    return;
  }
  std::string script = kContentBlockingScript;
  base::ReplaceFirstSubstringAfterOffset(&script, 0, "ZITTER_ARGS", base::WriteJson(args).value_or("{}"));
  frame->ExecuteJavaScriptInIsolatedWorld(base::UTF8ToUTF16(script), std::move(done), kZitterWorld);
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
      [](base::WeakPtr<ContentBlockingTabHelper> self, content::GlobalRenderFrameHostId id, std::vector<std::string> exceptions, base::Value result) {
        auto* frame = content::RenderFrameHost::FromID(id);
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
      }, weak_factory_.GetWeakPtr(), frame->GetGlobalId(), cosmetic.exceptions));
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
