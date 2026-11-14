// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "profile_context.h"

#include "chromium/prototype/rss_reader.h"

#include "extension_registry_probe.h"

#include <algorithm>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "base/numerics/clamped_math.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chromium/prototype/adblock.rs.h"
#include "chromium/prototype/adblock_browser_support.h"
#include "chromium/prototype/adblock_filter_lists.h"
#include "chromium/prototype/adblock_network.h"
#include "chromium/prototype/fingerprint_session.h"
#include "components/download/public/common/download_item.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/favicon/core/favicon_client.h"
#include "components/favicon/core/favicon_service_impl.h"
#include "components/history/content/browser/history_context_helper.h"
#include "components/history/content/browser/history_database_helper.h"
#include "components/history/core/browser/history_backend_client.h"
#include "components/history/core/browser/history_client.h"
#include "components/history/core/browser/history_database_params.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/visit_delegate.h"
#include "components/password_manager/core/browser/affiliation/affiliated_match_helper.h"
#include "components/password_manager/core/browser/password_manager.h"
#include "components/password_manager/core/browser/password_store/login_database.h"
#include "components/password_manager/core/browser/password_store/password_store.h"
#include "components/password_manager/core/browser/password_store/password_store_built_in_backend.h"
#include "components/password_manager/core/common/password_manager_pref_names.h"
#include "components/prefs/in_memory_pref_store.h"
#include "components/prefs/json_pref_store.h"
#include "components/prefs/overlay_user_pref_store.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/pref_service_factory.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/prefs/writeable_pref_store.h"
#include "components/sync/model/wipe_model_upon_sync_disabled_behavior.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/version_info/channel.h"
#include "build/build_config.h"
#include "content/public/browser/platform_notification_service.h"
#if BUILDFLAG(IS_WIN)
#include "content/public/browser/notification_event_dispatcher.h"
#include "content/public/browser/platform_notification_context.h"
#include "third_party/blink/public/common/notifications/platform_notification_data.h"
#include "base/timer/timer.h"
#endif
#include "content/public/browser/back_forward_cache.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/host_zoom_map.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/page_user_data.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/weak_document_ptr.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "content/public/browser/zoom_level_delegate.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "net/http/http_response_headers.h"
#include "page_password_manager.h"
#include "download_url_reputation.h"
#include "profile_download_delegate.h"
#include "profile_permission_controller.h"
#include "third_party/blink/public/common/page/page_zoom.h"
#include "services/metrics/public/cpp/ukm_source_id.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_provider.h"
#include "third_party/blink/public/mojom/loader/referrer.mojom.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"
#include "url/origin.h"
#include "zitter_content/fingerprint_state.mojom.h"

#if BUILDFLAG(IS_MAC)
#include "extension_notifications_mac.h"
#endif

namespace zitter::content_embedder {
namespace {

namespace pm = password_manager;
constexpr char kContentPasswordPrefs[] = "Content Password Preferences";
constexpr char kContentAdblockPrefs[] = "Content Ad Blocking Preferences";
constexpr size_t kMaxAdblockPrefsBytes = 8 * 1024 * 1024;
constexpr size_t kMaxAdblockRules = 1000;
constexpr size_t kMaxAdblockSites = 512;
constexpr size_t kMaxAdblockRuleBytes = 1000;
constexpr char kContentFingerprintPrefs[] = "Content Fingerprint Preferences";
constexpr char kContentSitePermissionPrefs[] = "Content Site Permissions";
constexpr char kContentFileChooserPrefs[] = "Content File Chooser Preferences";
constexpr char kContentPageZoomPrefs[] = "Content Page Zoom Preferences";
constexpr char kDefaultPageZoomLevel[] = "partition.default_zoom_level";
constexpr char kPerHostPageZoomLevels[] = "partition.per_host_zoom_levels";
constexpr size_t kMaxPageZoomPrefsBytes = 4 * 1024 * 1024;

bool IsValidPageZoomLevel(double level) {
  return level >= blink::ZoomFactorToZoomLevel(blink::kMinimumBrowserZoomFactor) &&
         level <= blink::ZoomFactorToZoomLevel(blink::kMaximumBrowserZoomFactor);
}

std::optional<double> ReadHostZoomLevel(const base::Value& value) {
  const auto* entry = value.GetIfDict();
  const auto level = entry ? entry->FindDouble("zoom_level") : std::nullopt;
  return level && IsValidPageZoomLevel(*level) ? level : std::nullopt;
}

constexpr char kFileChooserDirectory[] = "selectfile.last_directory";

class PageZoomDelegate final : public content::ZoomLevelDelegate {
 public:
  PageZoomDelegate(double level, PrefService* prefs)
      : level_(level), prefs_(prefs) {}

  void InitHostZoomMap(content::HostZoomMap* map) override {
    map->SetDefaultZoomLevel(level_);
    if (!prefs_)
      return;
    for (const auto [host, value] : prefs_->GetDict(kPerHostPageZoomLevels)) {
      const auto level = ReadHostZoomLevel(value);
      if (!level || host.empty() || host.find('\0') != std::string::npos)
        continue;
      int64_t time = 0;
      if (const auto* timestamp = value.GetDict().FindString("last_modified");
          timestamp && !base::StringToInt64(*timestamp, &time)) {
        time = 0;
      }
      map->InitializeZoomLevelForHost(host, *level,
                                        base::Time::FromInternalValue(time));
    }
    subscription_ = map->AddZoomLevelChangedCallback(base::BindRepeating(
        &PageZoomDelegate::StoreHostLevel, base::Unretained(this),
        base::Unretained(map)));
  }

 private:
  void StoreHostLevel(content::HostZoomMap* map,
                      const content::HostZoomMap::ZoomLevelChange& change) {
    if (change.mode != content::HostZoomMap::ZOOM_CHANGED_FOR_HOST ||
        prefs_->ReadOnly() || change.host.empty() ||
        change.host.find('\0') != std::string::npos ||
        !IsValidPageZoomLevel(change.zoom_level)) {
      return;
    }
    {
      ScopedDictPrefUpdate update(prefs_, kPerHostPageZoomLevels);
      if (blink::ZoomValuesEqual(change.zoom_level, map->GetDefaultZoomLevel())) {
        update->Remove(change.host);
      } else {
        update->Set(change.host, base::DictValue()
            .Set("zoom_level", change.zoom_level)
            .Set("last_modified",
                 base::NumberToString(change.last_modified.ToInternalValue())));
      }
    }
    prefs_->CommitPendingWrite();
  }

  const double level_;
  const raw_ptr<PrefService> prefs_;
  base::CallbackListSubscription subscription_;
};

bool CanRememberFileChooserDirectory(const base::FilePath& path) {
  return path.IsAbsolute() && !path.IsNetwork() && !path.ReferencesParent() &&
         base::IsStringUTF8(path.AsUTF8Unsafe());
}

// A missing file is normal for a new profile. Treat other read failures as
// unavailable so a damaged preference cannot silently change behavior.
std::optional<base::DictValue> ReadProfilePrefs(
    const base::FilePath& path, size_t max_size) {
  if (base::IsLink(path) || base::DirectoryExists(path))
    return std::nullopt;
  base::File file(path, base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!file.IsValid()) {
    if (file.error_details() == base::File::FILE_ERROR_NOT_FOUND)
      return base::DictValue();
    return std::nullopt;
  }
  const int64_t length = file.GetLength();
  if (length < 0 || static_cast<uint64_t>(length) > max_size)
    return std::nullopt;
  std::string json(static_cast<size_t>(length), '\0');
  if (!file.ReadAndCheck(0, base::as_writable_byte_span(json)))
    return std::nullopt;
  return base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
}

std::optional<bool> ReadFingerprintSwitch(const base::DictValue& prefs) {
  if (const auto* group = prefs.Find("zitter"); group && !group->is_dict())
    return std::nullopt;
  const auto* value =
      prefs.FindByDottedPath(prototype::kFingerprintProtectionPref);
  if (!value)
    return false;
  return value->is_bool() ? std::optional(value->GetBool()) : std::nullopt;
}

bool CanRecordHistory(const GURL& url) {
  return url.is_valid() && url.SchemeIsHTTPOrHTTPS() && !url.has_username() &&
         !url.has_password();
}

bool IsDatabasePathSafe(const base::FilePath& profile_path,
                        std::string_view filename) {
  for (std::string_view suffix : {"", "-journal", "-wal", "-shm"}) {
    const auto path = profile_path.AppendASCII(base::StrCat({filename, suffix}));
    if (base::IsLink(path) || base::DirectoryExists(path))
      return false;
  }
  return true;
}

class LocalHistoryClient final : public history::HistoryClient {
 public:
  void OnHistoryServiceCreated(history::HistoryService*) override {}
  void Shutdown() override {}
  history::CanAddURLCallback GetThreadSafeCanAddURLCallback() const override;
  void NotifyProfileError(sql::InitStatus status, const std::string&) override;
  std::unique_ptr<history::HistoryBackendClient> CreateBackendClient() override;
  void UpdateBookmarkLastUsedTime(int64_t, base::Time) override {}
};

history::CanAddURLCallback LocalHistoryClient::GetThreadSafeCanAddURLCallback()
    const {
  return base::BindRepeating(&CanRecordHistory);
}

void LocalHistoryClient::NotifyProfileError(sql::InitStatus status,
                                            const std::string&) {
  LOG(ERROR) << "Could not open Content profile history: " << status;
}

std::unique_ptr<history::HistoryBackendClient>
LocalHistoryClient::CreateBackendClient() {
  return nullptr;
}

}  // namespace

class ProfileContext::HistoryPage final
    : private content::WebContentsObserver,
      public content::WebContentsUserData<HistoryPage> {
 public:
  ~HistoryPage() override = default;

 private:
  friend class content::WebContentsUserData<HistoryPage>;

  HistoryPage(content::WebContents* contents, ProfileContext& profile)
      : content::WebContentsObserver(contents),
        content::WebContentsUserData<HistoryPage>(*contents),
        profile_(profile),
        context_id_(history::ContextIDForWebContents(contents)) {}

  void DidFinishNavigation(content::NavigationHandle* navigation) override;
  void TitleWasSet(content::NavigationEntry* entry) override;
  void WebContentsDestroyed() override;
  void EndVisit();

  const raw_ref<ProfileContext> profile_;
  const history::ContextID context_id_;
  std::optional<int> entry_id_;
  GURL url_;
  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

void ProfileContext::HistoryPage::DidFinishNavigation(
    content::NavigationHandle* navigation) {
  if (!navigation->HasCommitted() || !navigation->IsInPrimaryMainFrame())
    return;
  const auto* entry = web_contents()->GetController().GetLastCommittedEntry();
  const auto& url = navigation->GetURL();
  const bool record = entry && !entry->IsViewSourceMode() &&
                      !navigation->IsErrorPage() &&
                      navigation->ShouldUpdateHistory() && CanRecordHistory(url);
  if (!navigation->IsSameDocument() || record)
    EndVisit();
  if (!record)
    return;
  auto& service = *profile_->GetHistory();
  const auto* headers = navigation->GetResponseHeaders();
  const int status = headers ? headers->response_code() : 0;
  const auto transition = navigation->GetPageTransition();
  const bool hidden = status >= 400 || !ui::PageTransitionIsMainFrame(transition);
  service.AddPage(history::HistoryAddPageArgs(
      url, entry->GetTimestamp(), context_id_, entry->GetUniqueID(),
      navigation->GetNavigationId(), navigation->GetReferrer().url,
      navigation->GetRedirectChain(), transition, hidden, history::SOURCE_BROWSED,
      status == 404 ? history::VisitResponseCodeCategory::k404
                    : history::VisitResponseCodeCategory::kNot404,
      navigation->DidReplaceEntry(), /*consider_for_ntp_most_visited=*/!hidden));
  entry_id_ = entry->GetUniqueID();
  url_ = url;
  if (!entry->GetTitle().empty())
    service.SetPageTitle(url_, entry->GetTitle());
}

void ProfileContext::HistoryPage::TitleWasSet(content::NavigationEntry* entry) {
  if (entry_id_ == entry->GetUniqueID() && !entry->GetTitle().empty())
    profile_->GetHistory()->SetPageTitle(url_, entry->GetTitle());
}

void ProfileContext::HistoryPage::WebContentsDestroyed() {
  EndVisit();
  if (profile_->history_)
    profile_->history_->ClearCachedDataForContextID(context_id_);
}

void ProfileContext::HistoryPage::EndVisit() {
  if (!entry_id_)
    return;
  profile_->GetHistory()->UpdateWithPageEndTime(context_id_, *entry_id_, url_,
                                               base::Time::Now());
  entry_id_.reset();
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(ProfileContext::HistoryPage);

class ProfileContext::FingerprintPage final
    : public content::WebContentsUserData<FingerprintPage>,
      public content::WebContentsObserver {
 public:
  ~FingerprintPage() override = default;

 private:
  friend class content::WebContentsUserData<FingerprintPage>;

  explicit FingerprintPage(content::WebContents* contents)
      : content::WebContentsUserData<FingerprintPage>(*contents),
        content::WebContentsObserver(contents),
        profile_(*static_cast<ProfileContext*>(contents->GetBrowserContext())) {
    auto apply = base::BindRepeating(&FingerprintPage::Apply,
                                     base::Unretained(this));
    enabled_changed_ = profile_->fingerprint_changed_.Add(apply);
    key_changed_ = prototype::FingerprintSession::For(*profile_)
                       .ObserveRotation(std::move(apply));
    Apply();
  }

  void Apply() {
    GetWebContents().ForEachRenderFrameHost(
        [this](content::RenderFrameHost* frame) { Send(*frame); });
  }

  void RenderFrameCreated(content::RenderFrameHost* frame) override {
    Send(*frame);
  }

  void ReadyToCommitNavigation(content::NavigationHandle* navigation) override {
    Send(*navigation->GetRenderFrameHost());
  }

  void Send(content::RenderFrameHost& frame) {
    if (!frame.IsRenderFrameLive())
      return;
    mojo::AssociatedRemote<mojom::FingerprintState> remote;
    frame.GetRemoteAssociatedInterfaces()->GetInterface(&remote);
    const auto& key = prototype::FingerprintSession::For(*profile_).key();
    remote->Set(++profile_->fingerprint_transport_sequence_,
                profile_->fingerprint_protection_ ? key : std::vector<uint8_t>());
  }

  const raw_ref<ProfileContext> profile_;
  base::CallbackListSubscription enabled_changed_;
  base::CallbackListSubscription key_changed_;
  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(ProfileContext::FingerprintPage);

namespace {

bool AdblockHostMatches(std::string_view host, std::string_view domain) {
  return host == domain ||
         (host.size() > domain.size() && host.ends_with(domain) &&
          host[host.size() - domain.size() - 1] == '.');
}

std::string NormalizeAdblockSite(std::string_view site) {
  if (site.empty() || site.size() > 253 ||
      site.find_first_of("/?#@\\") != std::string_view::npos ||
      std::ranges::any_of(site, [](unsigned char c) { return c <= 0x20; })) {
    return {};
  }
  const GURL url(base::StrCat({"https://", site, "/"}));
  if (!url.is_valid() || !url.has_host() || url.has_port() ||
      url.has_username() || url.has_password()) {
    return {};
  }
  std::string host(url.host());
  if (host.ends_with('.'))
    host.pop_back();
  if (host.starts_with("www."))
    host.erase(0, 4);
  return host;
}

bool CanStoreAdblockRules(const base::ListValue& rules) {
  return rules.size() <= kMaxAdblockRules &&
         std::ranges::all_of(rules, [](const base::Value& value) {
           const auto* rule = value.GetIfString();
           return rule && !rule->empty() && rule->size() <= kMaxAdblockRuleBytes &&
                  base::IsStringUTF8(*rule) &&
                  rule->find_first_of("\r\n") == std::string::npos;
         });
}

base::ListValue AdblockListValue(const std::vector<std::string>& values) {
  base::ListValue list;
  for (const auto& value : values)
    list.Append(value);
  return list;
}

std::vector<std::string> AdblockStrings(const base::ListValue& values) {
  std::vector<std::string> result;
  result.reserve(values.size());
  for (const auto& value : values)
    result.push_back(value.GetString());
  return result;
}

scoped_refptr<user_prefs::PrefRegistrySyncable> AdblockPrefRegistry() {
  auto registry = base::MakeRefCounted<user_prefs::PrefRegistrySyncable>();
  registry->RegisterBooleanPref(prototype::kAdBlockingPref, true);
  registry->RegisterListPref(prototype::kAdBlockingAllowedSitesPref);
  registry->RegisterListPref(prototype::kAdBlockingRulesPref);
  registry->RegisterDictionaryPref(prototype::kAdBlockingListsPref);
  return registry;
}

rust::Box<adblock::Engine> BuildAdblockEngine(
    prototype::AdblockFilterLists::Texts texts, std::vector<bool> enabled,
    std::vector<std::string> rules) {
  rust::Vec<rust::String> lists;
  const auto& sources = prototype::AdblockFilterListSources();
  for (size_t i = 0; i < sources.size(); ++i) {
    if (!enabled[i])
      continue;
    if (texts && !(*texts)[i].empty())
      lists.emplace_back((*texts)[i]);
    else if (!texts && sources[i].builtin)
      lists.emplace_back(std::string(adblock::builtin_list()));
  }
  if (!rules.empty())
    lists.emplace_back(base::JoinString(rules, "\n"));
  return adblock::new_engine(lists);
}

}  // namespace

ProfileContext::AdblockPreferences::AdblockPreferences(
    scoped_refptr<PersistentPrefStore> user_store, bool async)
    : store(std::move(user_store)) {
  PrefServiceFactory factory;
  factory.set_user_prefs(store);
  factory.set_async(async);
  service = factory.Create(AdblockPrefRegistry());
}

ProfileContext::AdblockPreferences::~AdblockPreferences() = default;

class ProfileContext::AdblockState {
 public:
  AdblockState(url::Origin origin, rust::Box<adblock::Engine> engine)
      : origin_(std::move(origin)), engine_(std::move(engine)) {}

  explicit AdblockState(
      std::unique_ptr<AdblockPreferences> preferences)
      : preferences_(std::move(preferences)),
        engine_(BuildAdblockEngine({}, EnabledLists(), ReadRules())) {
    UpdateAllowedSites();
    registrar_.Init(&prefs());
    const auto switches = base::BindRepeating(
        &AdblockState::PreferencesChanged, base::Unretained(this), false);
    const auto rules = base::BindRepeating(
        &AdblockState::PreferencesChanged, base::Unretained(this), true);
    registrar_.Add(prototype::kAdBlockingPref, switches);
    registrar_.Add(prototype::kAdBlockingAllowedSitesPref, switches);
    registrar_.Add(prototype::kAdBlockingRulesPref, rules);
    registrar_.Add(prototype::kAdBlockingListsPref, rules);
  }

  bool Matches(const GURL& url) const {
    return url.SchemeIsHTTPOrHTTPS() &&
           (!origin_ || origin_->IsSameOriginWith(url));
  }

  bool Enabled() const {
    return !preferences_ || prefs().GetBoolean(prototype::kAdBlockingPref);
  }

  bool EnabledFor(const GURL& page) const {
    if (!Enabled() || !Matches(page))
      return false;
    auto host = page.host();
    if (host.ends_with('.'))
      host.remove_suffix(1);
    return !std::ranges::any_of(allowed_sites_, [&](const std::string& site) {
      return AdblockHostMatches(host, site);
    });
  }

  bool ListEnabled(const prototype::AdblockFilterList& list) const {
    return !list.optional || prefs().GetDict(prototype::kAdBlockingListsPref)
                                 .FindBool(list.id).value_or(list.default_on);
  }

  bool building() const { return generation_ != built_; }

  void SetFilterTexts(prototype::AdblockFilterLists::Texts texts,
                      base::OnceClosure ready) {
    texts_ = std::move(texts);
    Rebuild(std::move(ready));
  }

  bool HasPreferences() const { return preferences_ != nullptr; }
  PrefService& prefs() const { return *preferences_->service; }
  PersistentPrefStore* user_store() const { return preferences_->store.get(); }
  const std::vector<std::string>& allowed_sites() const { return allowed_sites_; }

  void PreferencesChanged(bool rebuild) {
    UpdateAllowedSites();
    if (rebuild)
      Rebuild({});
    else
      changed_.Notify();
  }

  base::CallbackListSubscription Observe(base::RepeatingClosure callback) {
    return changed_.Add(std::move(callback));
  }

  std::vector<std::string> PageScripts() const {
    std::vector<std::string> scripts;
    if (!Enabled())
      return scripts;
    for (const auto& site : allowed_sites_)
      scripts.push_back("!" + site + "\n");
    for (const auto& entry : engine_->page_scripts()) {
      scripts.push_back(std::string(entry.host) + "\n" +
                        std::string(entry.script));
    }
    return scripts;
  }

  adblock::CosmeticResources CosmeticResources(
      const GURL& url, const adblock::Engine* trial) const {
    return (trial ? *trial : *engine_).cosmetic_resources(url.spec());
  }

  base::ListValue GenericSelectors(
      const base::DictValue& scan,
      const rust::Vec<rust::String>& exceptions,
      const adblock::Engine* trial) const {
    auto read = [](const base::ListValue* values) {
      rust::Vec<rust::String> strings;
      if (values) {
        for (const auto& value : *values) {
          if (const auto* text = value.GetIfString())
            strings.emplace_back(*text);
        }
      }
      return strings;
    };
    const auto selectors = (trial ? *trial : *engine_).hidden_class_id_selectors(
        read(scan.FindList("classes")), read(scan.FindList("ids")), exceptions);
    base::ListValue list;
    for (const auto& selector : selectors)
      list.Append(std::string(selector));
    return list;
  }

  base::WeakPtr<AdblockState> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

  using TrialLookup = base::RepeatingCallback<const adblock::Engine*(const GURL&)>;
  using TrialResult = base::OnceCallback<void(rust::Box<adblock::Engine>)>;

  void BuildTrial(std::vector<std::string> additions, TrialResult done) {
    auto rules = ReadRules();
    rules.insert(rules.end(), std::make_move_iterator(additions.begin()),
                 std::make_move_iterator(additions.end()));
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::TaskPriority::USER_VISIBLE},
        base::BindOnce(&BuildAdblockEngine, texts_, EnabledLists(), std::move(rules)),
        std::move(done));
  }

  void AttachFactory(const GURL& page,
                     base::RepeatingCallback<void(const GURL&)> record_block,
                     TrialLookup trial_lookup,
                     network::URLLoaderFactoryBuilder& builder) {
    prototype::AppendAdblockFactory(
        builder, page,
        base::BindRepeating(
            [](base::WeakPtr<AdblockState> state,
               const base::RepeatingCallback<void(const GURL&)>& record_block,
               const TrialLookup& trial_lookup, const GURL& url,
               const GURL& page, const std::string& type, std::string* redirect) {
              if (!state || !state->Matches(url) || !state->EnabledFor(page))
                return false;
              const auto* trial = trial_lookup ? trial_lookup.Run(page) : nullptr;
              const auto result = (trial ? *trial : *state->engine_)
                                      .check_request(url.spec(), page.spec(), type);
              if (result.verdict != 1)
                return false;
              *redirect = std::string(result.redirect);
              if (record_block)
                record_block.Run(url);
              return true;
            },
            weak_factory_.GetWeakPtr(), std::move(record_block),
            std::move(trial_lookup)));
  }

 private:
  std::vector<bool> EnabledLists() const {
    std::vector<bool> result;
    for (const auto& list : prototype::AdblockFilterListSources())
      result.push_back(ListEnabled(list));
    return result;
  }

  std::vector<std::string> ReadRules() const {
    return AdblockStrings(prefs().GetList(prototype::kAdBlockingRulesPref));
  }

  void Rebuild(base::OnceClosure ready) {
    const size_t generation = ++generation_;
    if (!texts_) {
      engine_ = BuildAdblockEngine({}, EnabledLists(), ReadRules());
      built_ = generation;
      changed_.Notify();
      if (ready)
        std::move(ready).Run();
      return;
    }
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::TaskPriority::USER_VISIBLE},
        base::BindOnce(&BuildAdblockEngine, texts_, EnabledLists(), ReadRules()),
        base::BindOnce([](base::WeakPtr<AdblockState> state, size_t generation,
                          base::OnceClosure ready, rust::Box<adblock::Engine> engine) {
          if (state && state->generation_ == generation) {
            state->engine_ = std::move(engine);
            state->built_ = generation;
            state->changed_.Notify();
          }
          if (ready)
            std::move(ready).Run();
        }, weak_factory_.GetWeakPtr(), generation, std::move(ready)));
  }

  void UpdateAllowedSites() {
    allowed_sites_.clear();
    for (const auto& site : prefs().GetList(prototype::kAdBlockingAllowedSitesPref))
      allowed_sites_.push_back(NormalizeAdblockSite(site.GetString()));
  }

  const std::optional<url::Origin> origin_;
  const std::unique_ptr<AdblockPreferences> preferences_;
  PrefChangeRegistrar registrar_;
  rust::Box<adblock::Engine> engine_;
  prototype::AdblockFilterLists::Texts texts_;
  size_t generation_ = 0;
  size_t built_ = 0;
  std::vector<std::string> allowed_sites_;
  base::RepeatingClosureList changed_;
  base::WeakPtrFactory<AdblockState> weak_factory_{this};
};

namespace {

class AdblockCounts : public content::PageUserData<AdblockCounts> {
 public:
  int total() const { return requests + hidden; }

  base::ClampedNumeric<int> requests = 0;
  int hidden = 0;
  std::vector<std::string> recent_requests;

 private:
  friend class content::PageUserData<AdblockCounts>;
  explicit AdblockCounts(content::Page& page) : PageUserData(page) {}
  PAGE_USER_DATA_KEY_DECL();
};

PAGE_USER_DATA_KEY_IMPL(AdblockCounts);

}  // namespace

class ProfileContext::AdblockPage final
    : private content::WebContentsObserver,
      public content::WebContentsUserData<AdblockPage> {
 public:
  ~AdblockPage() override = default;

  int Count() {
    auto* frame = GetWebContents().GetPrimaryMainFrame();
    const auto& url = frame->GetLastCommittedURL();
    if (state_ && state_->Enabled() && state_->Matches(url) &&
        !state_->EnabledFor(url)) {
      return -1;
    }
    const auto* counts = AdblockCounts::GetForPage(frame->GetPage());
    return counts ? counts->total() : 0;
  }

  base::CallbackListSubscription ObserveCount(base::RepeatingClosure changed) {
    return count_changed_.Add(std::move(changed));
  }

  std::vector<std::string> RecentRequests() {
    auto* frame = GetWebContents().GetPrimaryMainFrame();
    if (!state_ || !state_->EnabledFor(frame->GetLastCommittedURL()))
      return {};
    const auto* counts = AdblockCounts::GetForPage(frame->GetPage());
    return counts ? counts->recent_requests : std::vector<std::string>();
  }

  const adblock::Engine* TrialFor(const GURL& page) const {
    return trial_engine_ && trial_origin_.IsSameOriginWith(page)
               ? &**trial_engine_ : nullptr;
  }

  bool MatchesTrialId(std::optional<size_t> id) const {
    return !id || *id == trial_request_;
  }

  void TryRules(std::vector<std::string> rules,
                base::OnceCallback<void(std::optional<size_t>)> done) {
    auto* frame = GetWebContents().GetPrimaryMainFrame();
    const auto& page = frame->GetLastCommittedURL();
    const auto site = prototype::AdblockSiteScope(page);
    if (!state_ || !state_->HasPreferences() || state_->building() ||
        !state_->EnabledFor(page) || rules.empty() || rules.size() > 50 ||
        std::ranges::any_of(rules, [&](const auto& rule) {
          // A newly opened window can inherit renderer preferences.
          return rule.size() > 1000 || rule.contains("+js(") ||
                 !adblock::rule_in_scope(rule, site);
        })) {
      std::move(done).Run(std::nullopt);
      return;
    }
    state_->BuildTrial(
        std::move(rules),
        base::BindOnce([](base::WeakPtr<AdblockPage> self, size_t request,
                          const content::WeakDocumentPtr& document,
                          base::OnceCallback<void(std::optional<size_t>)> done,
                          rust::Box<adblock::Engine> engine) {
          auto* frame = document.AsRenderFrameHostIfValid();
          if (!self || request != self->trial_request_ || !self->state_ ||
              self->state_->building() || !frame || !frame->IsActive() ||
              self->GetWebContents().GetPrimaryMainFrame() != frame ||
              self->GetWebContents().GetVisibility() == content::Visibility::HIDDEN) {
            std::move(done).Run(std::nullopt);
            return;
          }
          self->trial_origin_ = frame->GetLastCommittedOrigin();
          self->trial_engine_.emplace(std::move(engine));
          ExcludeTrialDocumentFromCache(frame);
          std::move(done).Run(request);
        }, weak_factory_.GetWeakPtr(), ++trial_request_, frame->GetWeakDocumentPtr(),
        std::move(done)));
  }

  void ClearTrial() {
    ++trial_request_;
    trial_engine_.reset();
    trial_origin_ = url::Origin();
  }

  void RecordBlock(int64_t page_source_id, const GURL& url) {
    auto* frame = GetWebContents().GetPrimaryMainFrame();
    // The factory may have been created before commit, or outlive its page.
    if (frame->GetPageUkmSourceId() != page_source_id)
      return;
    if (auto* counts = AdblockCounts::GetOrCreateForPage(frame->GetPage())) {
      ++counts->requests;
      GURL::Replacements strip;
      strip.ClearUsername();
      strip.ClearPassword();
      strip.ClearQuery();
      strip.ClearRef();
      auto& recent = counts->recent_requests;
      if (recent.size() == 5)
        recent.erase(recent.begin());
      recent.push_back(url.ReplaceComponents(strip).spec().substr(0, 256));
      count_changed_.Notify();
    }
  }

 private:
  friend class content::WebContentsUserData<AdblockPage>;

  AdblockPage(content::WebContents* contents,
              base::WeakPtr<AdblockState> state)
      : content::WebContentsObserver(contents),
        content::WebContentsUserData<AdblockPage>(*contents),
        state_(std::move(state)) {
    ApplyScripts();
    settings_changed_ = state_->Observe(base::BindRepeating(
        &AdblockPage::OnSettingsChanged, base::Unretained(this)));
  }

  void OnSettingsChanged() {
    ++cosmetics_generation_;
    ClearTrial();
    ApplyScripts();
    GetWebContents().ForEachRenderFrameHost([this](content::RenderFrameHost* frame) {
      ReapplyCosmetics(frame);
    });
  }

  void ReapplyCosmetics(content::RenderFrameHost* frame) {
    if (!frame->IsActive() || !frame->IsRenderFrameLive() ||
        !frame->GetLastCommittedURL().SchemeIsHTTPOrHTTPS() ||
        frame->GetOutermostMainFrame() != GetWebContents().GetPrimaryMainFrame()) {
      return;
    }
    if (!state_ || state_->building())
      return;
    if (!AppliesTo(frame)) {
      prototype::RunAdblockCosmeticScript(
          frame, base::DictValue().Set("op", "reset_cosmetics"),
          base::BindOnce(&AdblockPage::RecordCosmeticResult,
                         weak_factory_.GetWeakPtr(), frame->GetWeakDocumentPtr(),
                         cosmetics_generation_));
      return;
    }
    prototype::RunAdblockCosmeticScript(
        frame, base::DictValue().Set("op", "scan").Set("read_only", true),
        base::BindOnce(&AdblockPage::ApplyUpdatedCosmetics,
                       weak_factory_.GetWeakPtr(), frame->GetWeakDocumentPtr(),
                       cosmetics_generation_));
  }

  void ApplyUpdatedCosmetics(content::WeakDocumentPtr document,
                             size_t generation, base::Value scan) {
    auto* frame = document.AsRenderFrameHostIfValid();
    if (!AppliesTo(frame) || state_->building() ||
        generation != cosmetics_generation_ || !scan.is_dict()) {
      return;
    }
    const auto* trial = TrialForFrame(frame);
    const auto rules = state_->CosmeticResources(frame->GetLastCommittedURL(), trial);
    auto selectors = rules.generichide ? base::ListValue() :
        state_->GenericSelectors(scan.GetDict(), rules.exceptions, trial);
    for (const auto& selector : rules.hide_selectors)
      selectors.Append(std::string(selector));
    base::ListValue actions;
    for (const auto& action : rules.procedural)
      actions.Append(std::string(action));
    prototype::RunAdblockCosmeticScript(
        frame, base::DictValue().Set("op", "style").Set("key", "site")
                   .Set("reset", true).Set("selectors", std::move(selectors))
                   .Set("actions", std::move(actions)),
        base::BindOnce(&AdblockPage::RecordCosmeticResult,
                       weak_factory_.GetWeakPtr(), document, generation));
  }

  void ApplyScripts() {
    if (state_)
      prototype::SetAdblockPageScripts(GetWebContents(), state_->PageScripts());
    count_changed_.Notify();
  }

  bool AppliesTo(content::RenderFrameHost* frame) const {
    return state_ && frame && frame->IsActive() && frame->IsRenderFrameLive() &&
           frame->GetOutermostMainFrame() ==
               GetWebContents().GetPrimaryMainFrame() &&
           state_->Matches(frame->GetLastCommittedURL()) &&
           state_->Matches(frame->GetLastCommittedOrigin().GetURL()) &&
           state_->EnabledFor(
               frame->GetOutermostMainFrame()->GetLastCommittedURL());
  }

  static void ExcludeTrialDocumentFromCache(content::RenderFrameHost* frame) {
    // Returning through session history must reload without the trial.
    content::BackForwardCache::DisableForRenderFrameHost(
        frame, {content::BackForwardCache::DisabledSource::kEmbedder,
                1, "Zitter ad blocking trial", {}, "ContentSettings"});
  }

  const adblock::Engine* TrialForFrame(content::RenderFrameHost* frame) const {
    return TrialFor(frame->GetOutermostMainFrame()->GetLastCommittedURL());
  }

  void DidFinishNavigation(content::NavigationHandle* navigation) override {
    if (!navigation->HasCommitted() || navigation->IsSameDocument())
      return;
    if (navigation->IsInPrimaryMainFrame() &&
        (navigation->IsErrorPage() ||
         !trial_origin_.IsSameOriginWith(navigation->GetURL()))) {
      ClearTrial();
    }
    if (navigation->IsErrorPage())
      return;
    auto* frame = navigation->GetRenderFrameHost();
    if (navigation->IsServedFromBackForwardCache()) {
      ++cosmetics_generation_;
      frame->ForEachRenderFrameHost([this](content::RenderFrameHost* restored) {
        ReapplyCosmetics(restored);
      });
      return;
    }
    if (!AppliesTo(frame))
      return;
    const auto* trial = TrialForFrame(frame);
    if (trial && frame->IsInPrimaryMainFrame())
      ExcludeTrialDocumentFromCache(frame);
    Style(frame);
  }

  void Style(content::RenderFrameHost* frame) {
    if (!AppliesTo(frame) || state_->building())
      return;
    const auto rules =
        state_->CosmeticResources(frame->GetLastCommittedURL(), TrialForFrame(frame));
    base::ListValue selectors;
    for (const auto& selector : rules.hide_selectors)
      selectors.Append(std::string(selector));
    base::ListValue actions;
    for (const auto& action : rules.procedural)
      actions.Append(std::string(action));
    if (selectors.empty() && actions.empty())
      return;
    prototype::RunAdblockCosmeticScript(
        frame,
        base::DictValue()
            .Set("op", "style")
            .Set("key", "site")
            .Set("selectors", std::move(selectors))
            .Set("actions", std::move(actions)),
        base::BindOnce(&AdblockPage::RecordCosmeticResult,
                       weak_factory_.GetWeakPtr(), frame->GetWeakDocumentPtr(),
                       cosmetics_generation_));
  }

  void DOMContentLoaded(content::RenderFrameHost* frame) override {
    Scan(frame);
  }

  void DidFinishLoad(content::RenderFrameHost* frame, const GURL&) override {
    if (!AppliesTo(frame) || state_->building())
      return;
    Scan(frame);
    if (!frame->IsInPrimaryMainFrame())
      return;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&AdblockPage::ScanDocument, weak_factory_.GetWeakPtr(),
                       frame->GetWeakDocumentPtr()),
        base::Seconds(3));
  }

  void ScanDocument(content::WeakDocumentPtr document) {
    Scan(document.AsRenderFrameHostIfValid());
  }

  void Scan(content::RenderFrameHost* frame) {
    if (!AppliesTo(frame) || state_->building())
      return;
    auto rules = state_->CosmeticResources(frame->GetLastCommittedURL(),
                                           TrialForFrame(frame));
    auto document = frame->GetWeakDocumentPtr();
    auto on_scan = rules.generichide
        ? base::BindOnce(&AdblockPage::RecordCosmeticResult,
                         weak_factory_.GetWeakPtr(), document, cosmetics_generation_)
        : base::BindOnce(&AdblockPage::ApplyGeneric, weak_factory_.GetWeakPtr(),
                         document, cosmetics_generation_, std::move(rules.exceptions));
    prototype::RunAdblockCosmeticScript(
        frame, base::DictValue().Set("op", "scan"), std::move(on_scan));
  }

  void ApplyGeneric(content::WeakDocumentPtr document,
                    size_t generation,
                    rust::Vec<rust::String> exceptions,
                    base::Value result) {
    auto* frame = document.AsRenderFrameHostIfValid();
    if (!AppliesTo(frame) || state_->building() ||
        generation != cosmetics_generation_ || !result.is_dict())
      return;
    auto selectors = state_->GenericSelectors(result.GetDict(), exceptions,
                                              TrialForFrame(frame));
    if (selectors.empty()) {
      RecordCosmeticResult(document, generation, std::move(result));
      return;
    }
    prototype::RunAdblockCosmeticScript(
        frame,
        base::DictValue()
            .Set("op", "style")
            .Set("key", "generic")
            .Set("append", true)
            .Set("selectors", std::move(selectors)),
        base::BindOnce(&AdblockPage::RecordCosmeticResult,
                       weak_factory_.GetWeakPtr(), document, generation));
  }

  void RecordCosmeticResult(content::WeakDocumentPtr document,
                            size_t generation,
                            base::Value result) {
    auto* frame = document.AsRenderFrameHostIfValid();
    if (!frame || !frame->IsActive() || !frame->IsInPrimaryMainFrame() ||
        frame != GetWebContents().GetPrimaryMainFrame() || !state_ ||
        state_->building() || generation != cosmetics_generation_ ||
        !result.is_dict()) {
      return;
    }
    const auto hidden = result.GetDict().FindInt("hidden");
    auto* counts = AdblockCounts::GetOrCreateForPage(frame->GetPage());
    if (counts && hidden && *hidden >= 0 && counts->hidden != *hidden) {
      counts->hidden = *hidden;
      count_changed_.Notify();
    }
  }

  const base::WeakPtr<AdblockState> state_;
  std::optional<rust::Box<adblock::Engine>> trial_engine_;
  url::Origin trial_origin_;
  size_t trial_request_ = 0;
  size_t cosmetics_generation_ = 0;
  base::RepeatingClosureList count_changed_;
  base::CallbackListSubscription settings_changed_;
  base::WeakPtrFactory<AdblockPage> weak_factory_{this};
  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(ProfileContext::AdblockPage);

// static
void ProfileContext::AttachPage(content::WebContents& contents) {
  FingerprintPage::CreateForWebContents(&contents);
  auto* profile = static_cast<ProfileContext*>(contents.GetBrowserContext());
  favicon::ContentFaviconDriver::CreateForWebContents(&contents, profile->GetFaviconService());
  if (!profile->IsOffTheRecord()) {
    HistoryPage::CreateForWebContents(&contents, *profile);
    if (profile->os_crypt_)
      AttachPagePasswordManager(contents, *profile);
  }
  if (profile->adblock_)
    AdblockPage::CreateForWebContents(&contents, profile->adblock_->GetWeakPtr());
}

std::unique_ptr<ProfileContext::AdblockPreferences>
ProfileContext::OpenAdblockPreferences(
    const base::FilePath& profile_path) {
  const auto path = profile_path.AppendASCII(kContentAdblockPrefs);
  const auto current = ReadProfilePrefs(path, kMaxAdblockPrefsBytes);
  if (!current)
    return nullptr;
  if (const auto* group = current->Find("zitter"); group && !group->is_dict())
    return nullptr;
  if (const auto* enabled = current->FindByDottedPath(prototype::kAdBlockingPref);
      enabled && !enabled->is_bool()) {
    return nullptr;
  }
  if (const auto* rules = current->FindByDottedPath(prototype::kAdBlockingRulesPref);
      rules && (!rules->is_list() || !CanStoreAdblockRules(rules->GetList()))) {
    return nullptr;
  }
  if (const auto* sites = current->FindByDottedPath(
          prototype::kAdBlockingAllowedSitesPref)) {
    if (!sites->is_list() || sites->GetList().size() > kMaxAdblockSites ||
        !std::ranges::all_of(sites->GetList(), [](const base::Value& site) {
          return site.is_string() && !NormalizeAdblockSite(site.GetString()).empty();
        })) {
      return nullptr;
    }
  }
  if (const auto* lists = current->FindByDottedPath(prototype::kAdBlockingListsPref)) {
    if (!lists->is_dict() || lists->GetDict().size() > 8)
      return nullptr;
    for (const auto [key, value] : lists->GetDict()) {
      if (key.size() > 64 || !value.is_bool())
        return nullptr;
    }
  }

  auto preferences = std::make_unique<AdblockPreferences>(
      base::MakeRefCounted<JsonPrefStore>(path));
  auto& prefs = *preferences->service;
  if (prefs.ReadOnly() ||
      (prefs.GetInitializationStatus() !=
           PrefService::INITIALIZATION_STATUS_SUCCESS &&
       prefs.GetInitializationStatus() !=
           PrefService::INITIALIZATION_STATUS_CREATED_NEW_PREF_STORE)) {
    return nullptr;
  }
  return preferences;
}

void ProfileContext::SetAdblockPreferences(
    std::unique_ptr<AdblockPreferences> prefs) {
  CHECK(prefs && !adblock_ && !IsOffTheRecord());
  prototype::ConfigureAdblockDomainResolver();
  adblock_ = std::make_unique<AdblockState>(std::move(prefs));
}

void ProfileContext::InitializePrivateAdblock(ProfileContext* parent) {
  CHECK(IsOffTheRecord() && !adblock_);
  prototype::ConfigureAdblockDomainResolver();
  scoped_refptr<PersistentPrefStore> store =
      base::MakeRefCounted<InMemoryPrefStore>();
  if (parent && parent->adblock_ && parent->adblock_->HasPreferences()) {
    store = base::MakeRefCounted<OverlayUserPrefStore>(
        store.get(), parent->adblock_->user_store());
  }
  adblock_ = std::make_unique<AdblockState>(
      std::make_unique<AdblockPreferences>(std::move(store)));
}

std::optional<bool> ProfileContext::GetAdblockEnabled() const {
  if (!adblock_ || !adblock_->HasPreferences())
    return std::nullopt;
  return adblock_->Enabled();
}

bool ProfileContext::IsAdblockEnabledFor(const GURL& page) const {
  return adblock_ && adblock_->EnabledFor(page);
}

bool ProfileContext::SetAdblockEnabled(bool enabled) {
  const auto current = GetAdblockEnabled();
  if (!current)
    return false;
  if (*current != enabled) {
    adblock_->prefs().SetBoolean(prototype::kAdBlockingPref, enabled);
    if (!IsOffTheRecord())
      adblock_->prefs().CommitPendingWrite();
  }
  return true;
}

bool ProfileContext::GetAdblockListEnabled(std::string_view id) const {
  const auto& sources = prototype::AdblockFilterListSources();
  const auto source = std::ranges::find(sources, id, &prototype::AdblockFilterList::id);
  return source != sources.end() && GetAdblockEnabled().has_value() &&
         adblock_->ListEnabled(*source);
}

bool ProfileContext::SetAdblockListEnabled(std::string_view id, bool enabled) {
  const auto& sources = prototype::AdblockFilterListSources();
  const auto source = std::ranges::find(sources, id, &prototype::AdblockFilterList::id);
  if (source == sources.end() || !source->optional || !GetAdblockEnabled().has_value())
    return false;
  auto& prefs = adblock_->prefs();
  auto lists = prefs.GetDict(prototype::kAdBlockingListsPref).Clone();
  lists.Set(id, enabled);
  prefs.SetDict(prototype::kAdBlockingListsPref, std::move(lists));
  if (!IsOffTheRecord())
    adblock_->prefs().CommitPendingWrite();
  return true;
}

void ProfileContext::SetAdblockFilterTexts(
    std::shared_ptr<const std::vector<std::string>> texts,
    base::OnceClosure ready) {
  CHECK(adblock_ && adblock_->HasPreferences());
  adblock_->SetFilterTexts(std::move(texts), std::move(ready));
}

bool ProfileContext::IsAdblockBuilding() const {
  return adblock_ && adblock_->building();
}

base::CallbackListSubscription ProfileContext::ObserveAdblockSettings(
    base::RepeatingClosure changed) {
  return adblock_ ? adblock_->Observe(std::move(changed))
                  : base::CallbackListSubscription();
}

std::vector<std::string> ProfileContext::AdblockRules() const {
  return GetAdblockEnabled().has_value()
      ? AdblockStrings(adblock_->prefs().GetList(prototype::kAdBlockingRulesPref))
      : std::vector<std::string>();
}

bool ProfileContext::SetAdblockRules(std::vector<std::string> rules) {
  if (!GetAdblockEnabled().has_value())
    return false;
  auto values = AdblockListValue(rules);
  if (!CanStoreAdblockRules(values) ||
      !std::ranges::all_of(rules, [](const std::string& rule) {
        return rule.starts_with('!') || adblock::check_rule(rule).kind != 0;
      })) {
    return false;
  }
  adblock_->prefs().SetList(prototype::kAdBlockingRulesPref, std::move(values));
  if (!IsOffTheRecord())
    adblock_->prefs().CommitPendingWrite();
  return true;
}

std::vector<std::string> ProfileContext::AdblockAllowedSites() const {
  return GetAdblockEnabled().has_value() ? adblock_->allowed_sites()
                                        : std::vector<std::string>();
}

std::optional<bool> ProfileContext::GetEditableAdblockSiteException(
    const GURL& page) const {
  if (GetAdblockEnabled() != true || !adblock_->Matches(page))
    return std::nullopt;
  const auto site = NormalizeAdblockSite(page.host());
  if (site.empty())
    return std::nullopt;
  auto host = page.host();
  if (host.ends_with('.'))
    host.remove_suffix(1);
  const auto& sites = adblock_->allowed_sites();
  if (std::ranges::any_of(sites, [&](const std::string& exception) {
        return exception != site && AdblockHostMatches(host, exception);
      })) {
    return std::nullopt;
  }
  return std::ranges::contains(sites, site);
}

bool ProfileContext::SetAdblockSiteAllowed(std::string_view site, bool allowed) {
  if (!GetAdblockEnabled().has_value())
    return false;
  const auto normalized = NormalizeAdblockSite(site);
  if (normalized.empty())
    return false;
  auto sites = AdblockAllowedSites();
  std::erase(sites, normalized);
  if (allowed)
    sites.push_back(normalized);
  if (sites.size() > kMaxAdblockSites)
    return false;
  adblock_->prefs().SetList(prototype::kAdBlockingAllowedSitesPref,
                           AdblockListValue(sites));
  if (!IsOffTheRecord())
    adblock_->prefs().CommitPendingWrite();
  return true;
}

bool ProfileContext::ConfigureAdblockProbe(const GURL& url) {
  CHECK(!adblock_);
  if (!IsOffTheRecord() || !url.SchemeIs("http") ||
      url.host() != "127.0.0.1" || !url.has_port()) {
    return false;
  }
  prototype::ConfigureAdblockDomainResolver();
  const GURL blocked = url.Resolve("/blocked");
  const GURL allowed = url.Resolve("/allowed");
  const GURL bait = url.Resolve("/bait.js");
  rust::Vec<rust::String> lists;
  // Anchor fixture rules to the complete URLs, including the ephemeral port.
  lists.emplace_back(base::StrCat(
      {"|", blocked.spec(), "|$xmlhttprequest\n|", bait.spec(),
       "|$script,redirect=noop.js\n|", allowed.spec(),
       "|$xmlhttprequest\n@@|", allowed.spec(), "|$xmlhttprequest"}));
  lists.emplace_back(
      "127.0.0.1##+js(set-constant, zitterScriptletProbe, true)\n"
      "127.0.0.1##.site-ad\n"
      "127.0.0.1##.procedural-ad:has-text(Probe sponsored)\n"
      "127.0.0.1##.action-ad:remove()\n"
      "##.generic-ad\n"
      "##.late-ad\n"
      "##.exempt-ad\n"
      "127.0.0.1#@#.exempt-ad");
  auto engine = adblock::new_engine(lists);
  const auto redirected = engine->check_request(bait.spec(), url.spec(), "script");
  if (engine->check_request(blocked.spec(), url.spec(), "xmlhttprequest").verdict != 1 ||
      engine->check_request(allowed.spec(), url.spec(), "xmlhttprequest").verdict != 2 ||
      redirected.verdict != 1 || redirected.redirect.empty()) {
    LOG(ERROR) << "Scriptlet probe fixture rules did not match the loopback URLs";
    return false;
  }
  auto state = std::make_unique<AdblockState>(url::Origin::Create(url),
                                              std::move(engine));
  if (state->PageScripts().size() != 1)
    return false;
  adblock_ = std::move(state);
  return true;
}

// static
void ProfileContext::TryAdblockRule(content::WebContents& contents,
                                   std::string rule,
                                   base::OnceCallback<void(bool)> done) {
  TryAdblockRules(contents, {std::move(rule)},
                  base::BindOnce([](base::OnceCallback<void(bool)> done,
                                    std::optional<size_t> id) {
                    std::move(done).Run(id.has_value());
                  }, std::move(done)));
}

// static
void ProfileContext::TryAdblockRules(
    content::WebContents& contents, std::vector<std::string> rules,
    base::OnceCallback<void(std::optional<size_t>)> done) {
  if (auto* page = AdblockPage::FromWebContents(&contents)) {
    page->TryRules(std::move(rules), std::move(done));
    return;
  }
  std::move(done).Run(std::nullopt);
}

// static
bool ProfileContext::HasAdblockTrial(content::WebContents& contents,
                                     std::optional<size_t> id) {
  auto* page = AdblockPage::FromWebContents(&contents);
  return page && page->MatchesTrialId(id) &&
         page->TrialFor(contents.GetLastCommittedURL());
}

// static
void ProfileContext::ClearAdblockTrial(content::WebContents& contents,
                                       std::optional<size_t> id) {
  if (auto* page = AdblockPage::FromWebContents(&contents);
      page && page->MatchesTrialId(id))
    page->ClearTrial();
}

// static
int ProfileContext::AdblockCount(content::WebContents& contents) {
  auto* page = AdblockPage::FromWebContents(&contents);
  return page ? page->Count() : 0;
}

// static
std::vector<std::string> ProfileContext::RecentAdblockRequests(
    content::WebContents& contents) {
  auto* page = AdblockPage::FromWebContents(&contents);
  return page ? page->RecentRequests() : std::vector<std::string>();
}

// static
base::CallbackListSubscription ProfileContext::ObserveAdblockCount(
    content::WebContents& contents,
    base::RepeatingClosure changed) {
  auto* page = AdblockPage::FromWebContents(&contents);
  return page ? page->ObserveCount(std::move(changed))
              : base::CallbackListSubscription();
}

void ProfileContext::MaybeFilterAdblockRequests(
    const GURL& page,
    content::RenderFrameHost* frame,
    int64_t page_source_id,
    network::URLLoaderFactoryBuilder& builder) {
  if (!adblock_ || !adblock_->Matches(page))
    return;
  base::RepeatingCallback<void(const GURL&)> record_block;
  AdblockState::TrialLookup trial_lookup;
  if (frame) {
    auto* contents = content::WebContents::FromRenderFrameHost(
        frame->GetOutermostMainFrame());
    if (contents) {
      trial_lookup = base::BindRepeating(
          [](base::WeakPtr<content::WebContents> contents, const GURL& page)
              -> const adblock::Engine* {
            auto* helper = contents ? AdblockPage::FromWebContents(contents.get()) : nullptr;
            return helper ? helper->TrialFor(page) : nullptr;
          }, contents->GetWeakPtr());
    }
    if (contents && page_source_id != ukm::kInvalidSourceId) {
      record_block = base::BindRepeating(
          [](base::WeakPtr<content::WebContents> contents, int64_t id,
             const GURL& url) {
            if (contents) {
              if (auto* page = AdblockPage::FromWebContents(contents.get()))
                page->RecordBlock(id, url);
            }
          }, contents->GetWeakPtr(), page_source_id);
    }
  }
  adblock_->AttachFactory(page, std::move(record_block), std::move(trial_lookup),
                          builder);
}

std::unique_ptr<PrefService> ProfileContext::OpenFingerprintPreferences(
    const base::FilePath& profile_path) {
  const auto path = profile_path.AppendASCII(kContentFingerprintPrefs);
  const auto current = ReadProfilePrefs(path, 64 * 1024);
  if (!current || !ReadFingerprintSwitch(*current).has_value())
    return nullptr;

  bool enabled_by_default = false;
  if (!current->FindByDottedPath(prototype::kFingerprintProtectionPref)) {
    const auto legacy = ReadProfilePrefs(
        profile_path.AppendASCII("Preferences"), 16 * 1024 * 1024);
    if (!legacy)
      return nullptr;
    const auto previous = ReadFingerprintSwitch(*legacy);
    if (!previous)
      return nullptr;
    enabled_by_default = *previous;
  }

  auto registry = base::MakeRefCounted<user_prefs::PrefRegistrySyncable>();
  registry->RegisterBooleanPref(prototype::kFingerprintProtectionPref,
                                enabled_by_default);
  PrefServiceFactory factory;
  factory.set_user_prefs(base::MakeRefCounted<JsonPrefStore>(path));
  auto prefs = factory.Create(registry);
  if (prefs->ReadOnly() ||
      (prefs->GetInitializationStatus() !=
           PrefService::INITIALIZATION_STATUS_SUCCESS &&
       prefs->GetInitializationStatus() !=
           PrefService::INITIALIZATION_STATUS_CREATED_NEW_PREF_STORE)) {
    return nullptr;
  }
  return prefs;
}

std::unique_ptr<PrefService> ProfileContext::OpenSitePermissionPreferences(
    const base::FilePath& profile_path) {
  using Permissions = ProfilePermissionController;
  const auto path = profile_path.AppendASCII(kContentSitePermissionPrefs);
  const auto current = ReadProfilePrefs(path, 384 * 1024);
  if (!current)
    return nullptr;
  if (const auto* group = current->Find("zitter"); group && !group->is_dict())
    return nullptr;
  if (const auto* value = current->FindByDottedPath(Permissions::kClipboardPrefs)) {
    const auto* rules = value->GetIfDict();
    if (!rules || rules->size() > Permissions::kMaxClipboardRules)
      return nullptr;
    for (auto [key, rule] : *rules) {
      const auto origin = url::Origin::Create(GURL(key));
      if (!Permissions::IsClipboardOriginEligible(origin) ||
          origin.Serialize() != key || !rule.is_int() ||
          (rule.GetInt() != static_cast<int>(Permissions::ClipboardSetting::kAllow) &&
           rule.GetInt() != static_cast<int>(Permissions::ClipboardSetting::kBlock) &&
           rule.GetInt() != static_cast<int>(Permissions::ClipboardSetting::kAsk))) {
        return nullptr;
      }
    }
  }
  if (const auto* allowed = current->FindByDottedPath(Permissions::kPopupDefaultPref);
      allowed && !allowed->is_bool()) {
    return nullptr;
  }
  if (const auto* value = current->FindByDottedPath(Permissions::kPopupPrefs)) {
    const auto* rules = value->GetIfDict();
    if (!rules || rules->size() > Permissions::kMaxPopupRules)
      return nullptr;
    for (const auto [key, rule] : *rules) {
      const auto origin = url::Origin::Create(GURL(key));
      if (!Permissions::IsPopupOriginEligible(origin) ||
          origin.Serialize() != key || !rule.is_int() ||
          (rule.GetInt() != static_cast<int>(Permissions::PopupSetting::kAllow) &&
           rule.GetInt() != static_cast<int>(Permissions::PopupSetting::kBlock))) {
        return nullptr;
      }
    }
  }
  auto registry = base::MakeRefCounted<user_prefs::PrefRegistrySyncable>();
  registry->RegisterDictionaryPref(Permissions::kClipboardPrefs);
  registry->RegisterDictionaryPref(Permissions::kNotificationPrefs);
  registry->RegisterDictionaryPref(Permissions::kNotificationRuleTimes);
  registry->RegisterInt64Pref(Permissions::kNotificationNextId, 0);
  registry->RegisterDictionaryPref(Permissions::kSpellingPrefs);
  registry->RegisterDictionaryPref(Permissions::kPromptDefaults);
  registry->RegisterDictionaryPref(Permissions::kMediaRules);
  registry->RegisterDictionaryPref(Permissions::kMediaRuleTimes);
  registry->RegisterDictionaryPref(Permissions::kPopupPrefs);
  registry->RegisterDictionaryPref(Permissions::kClipboardRuleTimes);
  registry->RegisterDictionaryPref(Permissions::kPopupRuleTimes);
  registry->RegisterDictionaryPref(Permissions::kExternalProtocolPrefs);
  registry->RegisterDictionaryPref(Permissions::kExternalProtocolRuleTimes);
  registry->RegisterBooleanPref(Permissions::kPopupDefaultPref, false);
  PrefServiceFactory factory;
  factory.set_user_prefs(base::MakeRefCounted<JsonPrefStore>(path));
  auto prefs = factory.Create(registry);
  if (prefs->ReadOnly() ||
      (prefs->GetInitializationStatus() != PrefService::INITIALIZATION_STATUS_SUCCESS &&
       prefs->GetInitializationStatus() !=
           PrefService::INITIALIZATION_STATUS_CREATED_NEW_PREF_STORE)) {
    return nullptr;
  }
  return prefs;
}

std::unique_ptr<PrefService> ProfileContext::OpenFileChooserPreferences(
    const base::FilePath& profile_path) {
  const auto path = profile_path.AppendASCII(kContentFileChooserPrefs);
  const auto current = ReadProfilePrefs(path, 64 * 1024);
  if (!current)
    return nullptr;
  if (const auto* group = current->Find("selectfile"); group && !group->is_dict())
    return nullptr;
  if (const auto* value = current->FindByDottedPath(kFileChooserDirectory)) {
    const auto* text = value->GetIfString();
    if (!text || text->find('\0') != std::string::npos ||
        (!text->empty() && !CanRememberFileChooserDirectory(
                               base::FilePath::FromUTF8Unsafe(*text)))) {
      return nullptr;
    }
  }
  auto registry = base::MakeRefCounted<user_prefs::PrefRegistrySyncable>();
  registry->RegisterFilePathPref(kFileChooserDirectory, base::FilePath());
  PrefServiceFactory factory;
  factory.set_user_prefs(base::MakeRefCounted<JsonPrefStore>(path));
  auto prefs = factory.Create(registry);
  const auto status = prefs->GetInitializationStatus();
  if (prefs->ReadOnly() ||
      (status != PrefService::INITIALIZATION_STATUS_SUCCESS &&
       status != PrefService::INITIALIZATION_STATUS_CREATED_NEW_PREF_STORE)) {
    return nullptr;
  }
  return prefs;
}

std::unique_ptr<PrefService> ProfileContext::OpenPageZoomPreferences(
    const base::FilePath& profile_path) {
  const auto path = profile_path.AppendASCII(kContentPageZoomPrefs);
  const auto current = ReadProfilePrefs(path, kMaxPageZoomPrefsBytes);
  if (!current)
    return nullptr;
  if (const auto* partition = current->Find("partition");
      partition && !partition->is_dict()) {
    return nullptr;
  }
  if (const auto* value = current->FindByDottedPath(kDefaultPageZoomLevel)) {
    if (!value->is_double() || !IsValidPageZoomLevel(value->GetDouble()))
      return nullptr;
  }
  if (const auto* hosts = current->FindByDottedPath(kPerHostPageZoomLevels)) {
    if (!hosts->is_dict())
      return nullptr;
    for (const auto [host, value] : hosts->GetDict()) {
      if (host.empty() || host.find('\0') != std::string::npos ||
          !ReadHostZoomLevel(value)) {
        return nullptr;
      }
    }
  }
  auto registry = base::MakeRefCounted<user_prefs::PrefRegistrySyncable>();
  registry->RegisterDoublePref(kDefaultPageZoomLevel, 0.0);
  registry->RegisterDictionaryPref(kPerHostPageZoomLevels);
  PrefServiceFactory factory;
  factory.set_user_prefs(base::MakeRefCounted<JsonPrefStore>(path));
  auto prefs = factory.Create(registry);
  const auto status = prefs->GetInitializationStatus();
  if (prefs->ReadOnly() ||
      (status != PrefService::INITIALIZATION_STATUS_SUCCESS &&
       status != PrefService::INITIALIZATION_STATUS_CREATED_NEW_PREF_STORE)) {
    return nullptr;
  }
  return prefs;
}

void ProfileContext::SetPageZoomPreferences(std::unique_ptr<PrefService> prefs) {
  CHECK(!IsOffTheRecord());
  page_zoom_prefs_ = std::move(prefs);
  default_page_zoom_level_ = page_zoom_prefs_->GetDouble(kDefaultPageZoomLevel);
}

bool ProfileContext::CanSetDefaultPageZoomLevel() const {
  if (!page_zoom_prefs_ || page_zoom_prefs_->ReadOnly())
    return false;
  const auto status = page_zoom_prefs_->GetInitializationStatus();
  return status == PrefService::INITIALIZATION_STATUS_SUCCESS ||
         status == PrefService::INITIALIZATION_STATUS_CREATED_NEW_PREF_STORE;
}

bool ProfileContext::SetDefaultPageZoomLevel(double level) {
  if (!CanSetDefaultPageZoomLevel() || !IsValidPageZoomLevel(level))
    return false;
  page_zoom_prefs_->SetDouble(kDefaultPageZoomLevel, level);
  ApplyDefaultPageZoomLevel(level);
  // HostZoomMap removes entries matching the new default without a host event.
  {
    ScopedDictPrefUpdate hosts(page_zoom_prefs_.get(), kPerHostPageZoomLevels);
    std::vector<std::string> redundant;
    for (const auto [host, value] : *hosts) {
      const auto site_level = ReadHostZoomLevel(value);
      if (!site_level || blink::ZoomValuesEqual(*site_level, level))
        redundant.push_back(host);
    }
    for (const auto& host : redundant)
      hosts->Remove(host);
  }
  page_zoom_prefs_->CommitPendingWrite();
  return true;
}

void ProfileContext::ApplyDefaultPageZoomLevel(double level) {
  if (level == default_page_zoom_level_)
    return;
  default_page_zoom_level_ = level;
  content::HostZoomMap::GetDefaultForBrowserContext(this)->SetDefaultZoomLevel(level);
}

void ProfileContext::InheritPrivatePageZoom(ProfileContext& parent) {
  CHECK(IsOffTheRecord());
  default_page_zoom_level_ = parent.default_page_zoom_level();
  content::HostZoomMap::GetDefaultForBrowserContext(this)->CopyFrom(
      content::HostZoomMap::GetDefaultForBrowserContext(&parent));
}

void ProfileContext::SetFileChooserPreferences(
    std::unique_ptr<PrefService> prefs) {
  if (IsOffTheRecord() || !prefs)
    return;
  file_chooser_directory_ = prefs->GetFilePath(kFileChooserDirectory);
  file_chooser_prefs_ = std::move(prefs);
}

void ProfileContext::SetFileChooserDirectory(const base::FilePath& directory) {
  if (!CanRememberFileChooserDirectory(directory) ||
      directory == file_chooser_directory_) {
    return;
  }
  file_chooser_directory_ = directory;
  if (file_chooser_prefs_) {
    file_chooser_prefs_->SetFilePath(kFileChooserDirectory, directory);
    file_chooser_prefs_->CommitPendingWrite();
  }
}

ProfilePermissionController& ProfileContext::site_permissions() {
  return *permissions_;
}

void ProfileContext::SetFingerprintPreferences(
    std::unique_ptr<PrefService> prefs) {
  CHECK(prefs && !fingerprint_prefs_);
  fingerprint_protection_ =
      prefs->GetBoolean(prototype::kFingerprintProtectionPref);
  fingerprint_prefs_ = std::move(prefs);
}

std::optional<bool> ProfileContext::GetFingerprintProtection() {
  if (!IsOffTheRecord() && !fingerprint_prefs_)
    return std::nullopt;
  return fingerprint_protection_;
}

bool ProfileContext::SetFingerprintProtection(bool enabled) {
  if (!GetFingerprintProtection().has_value())
    return false;
  fingerprint_overridden_ = IsOffTheRecord();
  if (fingerprint_protection_ == enabled)
    return true;
  if (fingerprint_prefs_) {
    fingerprint_prefs_->SetBoolean(prototype::kFingerprintProtectionPref,
                                  enabled);
    fingerprint_prefs_->CommitPendingWrite();
  }
  UpdateFingerprintProtection(enabled);
  return true;
}

void ProfileContext::InheritPrivateFingerprintProtection(ProfileContext& parent) {
  CHECK(IsOffTheRecord() && !inherited_fingerprint_);
  auto refresh = base::BindRepeating(
      [](ProfileContext* child, ProfileContext* parent) {
        if (!child->fingerprint_overridden_) {
          child->UpdateFingerprintProtection(
              parent->GetFingerprintProtection().value_or(false));
        }
      }, base::Unretained(this), base::Unretained(&parent));
  refresh.Run();
  inherited_fingerprint_ = parent.ObserveFingerprintProtection(std::move(refresh));
}

base::CallbackListSubscription ProfileContext::ObserveFingerprintProtection(
    base::RepeatingClosure changed) {
  return fingerprint_changed_.Add(std::move(changed));
}

void ProfileContext::UpdateFingerprintProtection(bool enabled) {
  if (fingerprint_protection_ == enabled)
    return;
  fingerprint_protection_ = enabled;
  fingerprint_changed_.Notify();
}

ProfileContext::ProfileContext(
    base::FilePath path,
    Mode mode,
    std::optional<url::Origin> storage_probe_origin,
    os_crypt_async::OSCryptAsync* os_crypt,
    std::optional<bool> password_saving_default,
    base::FilePath separate_network_path)
    : path_(std::move(path)),
      separate_network_path_(std::move(separate_network_path)),
      mode_(mode),
      os_crypt_(os_crypt),
      downloads_(
          std::make_unique<ProfileDownloadDelegate>(GURL(), base::FilePath())),
      permissions_(std::make_unique<ProfilePermissionController>(
          *this, std::move(storage_probe_origin))) {
  RegisterExtensionContext(*this);
  if (IsOffTheRecord() || !os_crypt_)
    return;

  auto registry = base::MakeRefCounted<user_prefs::PrefRegistrySyncable>();
  pm::PasswordManager::RegisterProfilePrefs(registry.get());
  auto policy = base::MakeRefCounted<InMemoryPrefStore>();
  policy->SetValue(pm::prefs::kPasswordLeakDetectionEnabled, base::Value(false),
                   WriteablePrefStore::DEFAULT_PREF_WRITE_FLAGS);
  PrefServiceFactory factory;
  factory.set_managed_prefs(std::move(policy));
  factory.set_extension_prefs(GetWorkspaceExtensionPreferenceStore(*this));
  persistent_password_prefs_ = password_saving_default.has_value();
  if (persistent_password_prefs_) {
    factory.set_user_prefs(base::MakeRefCounted<JsonPrefStore>(
        path_.AppendASCII(kContentPasswordPrefs)));
    factory.set_async(true);
  } else {
    factory.set_user_prefs(base::MakeRefCounted<InMemoryPrefStore>());
  }
  password_prefs_ = factory.Create(registry);
  password_prefs_->SetDefaultPrefValue(
      pm::prefs::kCredentialsEnableService,
      base::Value(password_saving_default.value_or(false)));
  password_privacy_registrar_ = std::make_unique<PrefChangeRegistrar>();
  password_privacy_registrar_->Init(password_prefs_.get());
  password_privacy_registrar_->Add(pm::prefs::kCredentialsEnableService, base::BindRepeating(
      [](ProfileContext* profile) { DispatchExtensionPasswordSavingChange(*profile); },
      base::Unretained(this)));
}

ProfileContext::~ProfileContext() {
  rss_reader_.reset();
  password_privacy_registrar_.reset();
  NotifyWillBeDestroyed();
  notifications_.reset();
  UnregisterExtensionContext(*this);
  if (password_store_) {
    password_store_->ShutdownOnUIThread();
    password_store_.reset();
  }
  if (download_manager_) {
    if (IsOffTheRecord()) {
      content::DownloadManager::DownloadVector items;
      download_manager_->GetAllDownloads(&items);
      // Chromium keeps interrupted partials available for recovery. A private
      // context has no recovery record and must discard its unfinished files.
      for (auto item : items) {
        if (item->GetState() == download::DownloadItem::INTERRUPTED)
          item->Cancel(/*user_cancel=*/false);
      }
    }
    download_manager_->Shutdown();
  }
  favicon_service_.reset();
  history_.reset();
  ShutdownStoragePartitions();
}

WorkspaceProfileStartup::WorkspaceProfileStartup(base::FilePath path)
    : path_(std::move(path)) {}

WorkspaceProfileStartup::~WorkspaceProfileStartup() = default;

std::unique_ptr<WorkspaceProfileStartup> WorkspaceProfileStartup::Read(
    base::FilePath path, base::FilePath separate_network_path) {
  const auto network = separate_network_path.empty()
                           ? path.AppendASCII("Network") : separate_network_path;
  if (base::IsLink(path) || !base::DirectoryExists(path) ||
      base::IsLink(network) || !base::DirectoryExists(network) ||
      !ProfileContext::IsPasswordStorePathSafe(path) ||
      !ProfileContext::IsHistoryStorePathSafe(path)) {
    return nullptr;
  }
  auto startup = base::WrapUnique(new WorkspaceProfileStartup(std::move(path)));
  startup->separate_network_path_ = std::move(separate_network_path);
  startup->password_saving_default_ =
      ProfileContext::ReadPasswordSavingDefault(startup->path_);
  startup->site_permissions_ = ProfileContext::OpenSitePermissionPreferences(startup->path_);
  startup->file_chooser_ = ProfileContext::OpenFileChooserPreferences(startup->path_);
  startup->page_zoom_ = ProfileContext::OpenPageZoomPreferences(startup->path_);
  startup->fingerprint_ = ProfileContext::OpenFingerprintPreferences(startup->path_);
  startup->adblock_ = ProfileContext::OpenAdblockPreferences(startup->path_);
  if (!startup->password_saving_default_ || !startup->site_permissions_ ||
      !startup->file_chooser_ || !startup->page_zoom_ || !startup->fingerprint_ ||
      !startup->adblock_) {
    return nullptr;
  }
  return startup;
}

std::unique_ptr<ProfileContext> WorkspaceProfileStartup::Open(
    os_crypt_async::OSCryptAsync& encryption) {
  auto profile = std::make_unique<ProfileContext>(
      std::move(path_), ProfileContext::Mode::kPersistent, std::nullopt,
      &encryption, password_saving_default_, std::move(separate_network_path_));
  profile->SetFileChooserPreferences(std::move(file_chooser_));
  profile->SetPageZoomPreferences(std::move(page_zoom_));
  profile->SetFingerprintPreferences(std::move(fingerprint_));
  profile->SetAdblockPreferences(std::move(adblock_));
  profile->site_permissions().SetSitePermissionPreferences(std::move(site_permissions_));
  return profile;
}

void ProfileContext::InitializeNewWorkspacePreferences() {
  CHECK(!IsOffTheRecord());
  auto preferences = [&](const char* filename,
                         scoped_refptr<user_prefs::PrefRegistrySyncable> registry) {
    PrefServiceFactory factory;
    factory.set_user_prefs(base::MakeRefCounted<JsonPrefStore>(
        path_.AppendASCII(filename)));
    factory.set_async(true);
    return factory.Create(std::move(registry));
  };
  using Permissions = ProfilePermissionController;
  auto site = base::MakeRefCounted<user_prefs::PrefRegistrySyncable>();
  site->RegisterDictionaryPref(Permissions::kClipboardPrefs);
  site->RegisterDictionaryPref(Permissions::kNotificationPrefs);
  site->RegisterDictionaryPref(Permissions::kNotificationRuleTimes);
  site->RegisterInt64Pref(Permissions::kNotificationNextId, 0);
  site->RegisterDictionaryPref(Permissions::kSpellingPrefs);
  site->RegisterDictionaryPref(Permissions::kPromptDefaults);
  site->RegisterDictionaryPref(Permissions::kMediaRules);
  site->RegisterDictionaryPref(Permissions::kMediaRuleTimes);
  site->RegisterDictionaryPref(Permissions::kPopupPrefs);
  site->RegisterDictionaryPref(Permissions::kClipboardRuleTimes);
  site->RegisterDictionaryPref(Permissions::kPopupRuleTimes);
  site->RegisterDictionaryPref(Permissions::kExternalProtocolPrefs);
  site->RegisterDictionaryPref(Permissions::kExternalProtocolRuleTimes);
  site->RegisterBooleanPref(Permissions::kPopupDefaultPref, false);
  site_permissions().SetSitePermissionPreferences(
      preferences(kContentSitePermissionPrefs, std::move(site)));
  auto chooser = base::MakeRefCounted<user_prefs::PrefRegistrySyncable>();
  chooser->RegisterFilePathPref(kFileChooserDirectory, base::FilePath());
  SetFileChooserPreferences(preferences(kContentFileChooserPrefs, std::move(chooser)));
  auto page_zoom = base::MakeRefCounted<user_prefs::PrefRegistrySyncable>();
  page_zoom->RegisterDoublePref(kDefaultPageZoomLevel, 0.0);
  page_zoom->RegisterDictionaryPref(kPerHostPageZoomLevels);
  SetPageZoomPreferences(preferences(kContentPageZoomPrefs, std::move(page_zoom)));
  auto fingerprint = base::MakeRefCounted<user_prefs::PrefRegistrySyncable>();
  fingerprint->RegisterBooleanPref(prototype::kFingerprintProtectionPref, false);
  SetFingerprintPreferences(preferences(kContentFingerprintPrefs, std::move(fingerprint)));
  SetAdblockPreferences(std::make_unique<AdblockPreferences>(
      base::MakeRefCounted<JsonPrefStore>(path_.AppendASCII(kContentAdblockPrefs)),
      /*async=*/true));
}

prototype::RssReader& ProfileContext::GetRssReader() {
  if (!rss_reader_)
    rss_reader_ = std::make_unique<prototype::RssReader>(*this);
  return *rss_reader_;
}

history::HistoryService* ProfileContext::GetHistory() {
  if (IsOffTheRecord())
    return nullptr;
  if (!history_) {
    history_ = std::make_unique<history::HistoryService>(
        std::make_unique<LocalHistoryClient>(), nullptr, nullptr, nullptr);
    history_->Init(history::HistoryDatabaseParamsForPath(
        path_, version_info::Channel::UNKNOWN));
  }
  return history_.get();
}

favicon::FaviconServiceImpl* ProfileContext::GetFaviconService() {
  if (!favicon_service_) {
    if (auto* history = GetHistory()) {
      favicon_service_ = std::make_unique<favicon::FaviconServiceImpl>(
          nullptr, history);
    }
  }
  return favicon_service_.get();
}

void ProfileContext::CloseImportedHistory(base::OnceClosure done) {
  CHECK(!download_manager_);
  if (!history_) {
    std::move(done).Run();
    return;
  }
  history_->SetOnBackendDestroyTask(std::move(done));
  favicon_service_.reset();
  history_.reset();
}

bool ProfileContext::IsPasswordStorePathSafe(
    const base::FilePath& profile_path) {
  return IsDatabasePathSafe(profile_path, "Login Data");
}

bool ProfileContext::IsHistoryStorePathSafe(const base::FilePath& profile_path) {
  return IsDatabasePathSafe(profile_path, "History") &&
         IsDatabasePathSafe(profile_path, "Favicons");
}

std::optional<bool> ProfileContext::ReadPasswordSavingDefault(
    const base::FilePath& profile_path) {
  const auto current =
      ReadProfilePrefs(profile_path.AppendASCII(kContentPasswordPrefs), 64 * 1024);
  if (!current)
    return std::nullopt;
  if (const auto* value = current->Find(pm::prefs::kCredentialsEnableService)) {
    // This profile already has an explicit Content setting.
    return value->is_bool() ? std::optional(true) : std::nullopt;
  }
  const auto legacy =
      ReadProfilePrefs(profile_path.AppendASCII("Preferences"), 16 * 1024 * 1024);
  if (!legacy)
    return std::nullopt;
  if (const auto* value = legacy->Find(pm::prefs::kCredentialsEnableService)) {
    if (!value->is_bool())
      return std::nullopt;
    return value->GetBool();
  }
  return true;
}

std::optional<bool> ProfileContext::GetPasswordSavingSetting() const {
  if (!persistent_password_prefs_ || password_prefs_->ReadOnly())
    return std::nullopt;
  switch (password_prefs_->GetInitializationStatus()) {
    case PrefService::INITIALIZATION_STATUS_SUCCESS:
    case PrefService::INITIALIZATION_STATUS_CREATED_NEW_PREF_STORE:
      return password_prefs_->GetBoolean(pm::prefs::kCredentialsEnableService);
    default:
      return std::nullopt;
  }
}

bool ProfileContext::SetPasswordSavingSetting(bool enabled) {
  if (!GetPasswordSavingSetting().has_value())
    return false;
  password_prefs_->SetBoolean(pm::prefs::kCredentialsEnableService, enabled);
  password_prefs_->CommitPendingWrite();
  return true;
}

password_manager::PasswordStoreInterface* ProfileContext::GetPasswordStore() {
  if (IsOffTheRecord() || !os_crypt_)
    return nullptr;
  if (password_store_)
    return password_store_.get();

  auto database = std::make_unique<pm::LoginDatabase>(
      path_.AppendASCII("Login Data"), pm::IsAccountStore(false),
      pm::LoginDatabase::DeletingUndecryptablePasswordsEnabled(false));
  auto backend = std::make_unique<pm::PasswordStoreBuiltInBackend>(
      std::move(database), syncer::WipeModelUponSyncDisabledBehavior::kNever,
      password_prefs_.get(), os_crypt_, nullptr);
  password_store_ = base::MakeRefCounted<pm::PasswordStore>(std::move(backend));
  password_store_->Init();
  return password_store_.get();
}

void ProfileContext::ConfigureDownloadProbe(GURL url,
                                            base::FilePath directory,
                                            bool prompt_for_probe,
                                            bool allow_http_fixture) {
  CHECK(!download_manager_);
  downloads_ = std::make_unique<ProfileDownloadDelegate>(
      std::move(url), std::move(directory), prompt_for_probe, allow_http_fixture);
}

void ProfileContext::EnableUserDownloads(
    const base::FilePath& suggested_directory,
    std::string safe_browsing_key) {
  CHECK(!download_manager_);
  downloads_->EnableUserDownloads(suggested_directory);
  if (!IsOffTheRecord())
    download_safe_browsing_key_ = std::move(safe_browsing_key);
}

int ProfileContext::BlockingDownloadCount() const {
  return download_manager_ ? download_manager_->BlockingShutdownCount() : 0;
}

void ProfileContext::SetDownloadProbeCertificate(std::vector<uint8_t> der) {
  CHECK(!download_manager_);
  download_probe_certificate_ = std::move(der);
}

base::FilePath ProfileContext::GetPath() const {
  return path_;
}

base::FilePath ProfileContext::GetNetworkPath() const {
  return separate_network_path_.empty() ? path_.AppendASCII("Network")
                                        : separate_network_path_;
}

bool ProfileContext::IsOffTheRecord() {
  return mode_ == Mode::kPrivate;
}

std::unique_ptr<content::ZoomLevelDelegate>
ProfileContext::CreateZoomLevelDelegate(const base::FilePath& partition_path) {
  if (partition_path != path_)
    return nullptr;
  return std::make_unique<PageZoomDelegate>(default_page_zoom_level_, page_zoom_prefs_.get());
}

content::DownloadManagerDelegate* ProfileContext::GetDownloadManagerDelegate() {
  // BrowserContext publishes the manager before asking for its delegate.
  auto* manager = GetDownloadManager();
  if (download_manager_ != manager) {
    download_manager_ = manager;
    if (!download_safe_browsing_key_.empty()) {
      downloads_->SetUrlReputation(std::make_unique<DownloadUrlReputation>(
          GetDefaultStoragePartition()->GetURLLoaderFactoryForBrowserProcess(),
          std::exchange(download_safe_browsing_key_, {})));
    }
    downloads_->AttachManager(*download_manager_, GetHistory());
  }
  return downloads_.get();
}

content::BrowserPluginGuestManager* ProfileContext::GetGuestManager() {
  return nullptr;
}

storage::SpecialStoragePolicy* ProfileContext::GetSpecialStoragePolicy() {
  return nullptr;
}

#if BUILDFLAG(IS_WIN)
namespace {

class WindowsWebNotifications final : public content::PlatformNotificationService {
 public:
  WindowsWebNotifications(ProfileContext& profile,
                          ProfileContext::WebNotificationPresenter show,
                          base::RepeatingCallback<void(const std::string&)> close)
      : profile_(profile), show_(std::move(show)), close_(std::move(close)) {}

  void DisplayNotification(const std::string& id, const GURL& origin,
                           const GURL&, const blink::PlatformNotificationData& data,
                           const blink::NotificationResources&) override {
    Show(id, origin, data, false);
  }
  void DisplayPersistentNotification(const std::string& id, const GURL&,
                                     const GURL& origin, const blink::PlatformNotificationData& data,
                                     const blink::NotificationResources&) override {
    Show(id, origin, data, true);
  }
  void CloseNotification(const std::string& id) override {
    entries_.erase(id);
    close_.Run(id);
  }
  void ClosePersistentNotification(const std::string& id) override { CloseNotification(id); }

  void GetDisplayedNotifications(DisplayedNotificationsCallback callback) override {
    GetDisplayedNotificationsForOrigin(GURL(), std::move(callback));
  }
  void GetDisplayedNotificationsForOrigin(const GURL& origin,
                                          DisplayedNotificationsCallback callback) override {
    std::set<std::string> ids;
    for (const auto& [id, source] : entries_) {
      if (origin.is_empty() || source == origin)
        ids.insert(id);
    }
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback), std::move(ids), true));
  }
  void ScheduleTrigger(base::Time timestamp) override {
    if (timestamp >= next_trigger_)
      return;
    next_trigger_ = timestamp;
    timer_.Start(FROM_HERE, std::max(base::TimeDelta(), timestamp - base::Time::Now()),
                 this, &WindowsWebNotifications::Trigger);
  }
  base::Time ReadNextTriggerTimestamp() override { return next_trigger_; }
  int64_t ReadNextPersistentNotificationId() override {
    return profile_->site_permissions().NextNotificationId();
  }
  void RecordNotificationUkmEvent(const content::NotificationDatabaseData&) override {}

 private:
  void Trigger() {
    next_trigger_ = base::Time::Max();
    profile_->ForEachLoadedStoragePartition([](content::StoragePartition* partition) {
      partition->GetPlatformNotificationContext()->TriggerNotifications();
    });
  }

  void Show(const std::string& id, const GURL& origin,
            const blink::PlatformNotificationData& data, bool persistent) {
    if (profile_->site_permissions().NotificationSetting(url::Origin::Create(origin)) != 1)
      return;
    entries_.insert_or_assign(id, origin);
    show_.Run(id, origin, data.title, data.body, base::BindRepeating(
        &WindowsWebNotifications::Click, weak_factory_.GetWeakPtr(), id, persistent));
    if (!persistent)
      content::NotificationEventDispatcher::GetInstance()->DispatchNonPersistentShowEvent(id);
  }
  void Click(const std::string& id, bool persistent) {
    const auto entry = entries_.find(id);
    if (entry == entries_.end() || profile_->ShutdownStarted())
      return;
    const auto origin = entry->second;
    CloseNotification(id);
    if (profile_->site_permissions().NotificationSetting(url::Origin::Create(origin)) != 1)
      return;
    auto* dispatcher = content::NotificationEventDispatcher::GetInstance();
    if (persistent) {
      dispatcher->DispatchNotificationClickEvent(
          &*profile_, id, origin, std::nullopt, std::nullopt, base::DoNothing());
    } else {
      dispatcher->DispatchNonPersistentClickEvent(id, base::DoNothing());
    }
  }

  const raw_ref<ProfileContext> profile_;
  const ProfileContext::WebNotificationPresenter show_;
  const base::RepeatingCallback<void(const std::string&)> close_;
  std::map<std::string, GURL> entries_;
  base::Time next_trigger_ = base::Time::Max();
  base::OneShotTimer timer_;
  base::WeakPtrFactory<WindowsWebNotifications> weak_factory_{this};
};
}  // namespace

void ProfileContext::SetWebNotificationPresenter(
    WebNotificationPresenter show,
    base::RepeatingCallback<void(const std::string&)> close) {
  CHECK(!notifications_ && !IsOffTheRecord());
  notifications_ = std::make_unique<WindowsWebNotifications>(
      *this, std::move(show), std::move(close));
}
#endif

content::PlatformNotificationService*
ProfileContext::GetPlatformNotificationService() {
#if BUILDFLAG(IS_MAC)
  if (!notifications_ && !ShutdownStarted() && !IsOffTheRecord())
    notifications_ = CreateMacWebNotificationService(*this);
#endif
  return notifications_.get();
}

content::PushMessagingService* ProfileContext::GetPushMessagingService() {
  return nullptr;
}

content::StorageNotificationService*
ProfileContext::GetStorageNotificationService() {
  return nullptr;
}

content::SSLHostStateDelegate* ProfileContext::GetSSLHostStateDelegate() {
  return nullptr;
}

content::PermissionControllerDelegate*
ProfileContext::GetPermissionControllerDelegate() {
  return permissions_.get();
}

content::ReduceAcceptLanguageControllerDelegate*
ProfileContext::GetReduceAcceptLanguageControllerDelegate() {
  return nullptr;
}

content::ClientHintsControllerDelegate*
ProfileContext::GetClientHintsControllerDelegate() {
  return nullptr;
}

content::BackgroundFetchDelegate* ProfileContext::GetBackgroundFetchDelegate() {
  return nullptr;
}

content::BackgroundSyncController*
ProfileContext::GetBackgroundSyncController() {
  return nullptr;
}

content::BrowsingDataRemoverDelegate*
ProfileContext::GetBrowsingDataRemoverDelegate() {
  return nullptr;
}

}  // namespace zitter::content_embedder
