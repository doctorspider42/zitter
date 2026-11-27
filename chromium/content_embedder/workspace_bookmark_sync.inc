// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// Included in workspace_lab.cc, alongside the workspace persistence callbacks.
namespace {
constexpr std::string_view kDropboxCredentialPrefix = "zitter-bookmark-sync/dropbox/";

constexpr char kDropboxRedirect[] = "http://127.0.0.1:53682/dropbox/callback";
constexpr char kDropboxSyncIndex[] = "/sync-v2-index.json";
constexpr char kDropboxDownload[] = "https://content.dropboxapi.com/2/files/download";
constexpr char kDropboxUpload[] = "https://content.dropboxapi.com/2/files/upload";
constexpr char kDropboxCursor[] = "https://api.dropboxapi.com/2/files/list_folder/get_latest_cursor";
constexpr char kDropboxChanges[] = "https://api.dropboxapi.com/2/files/list_folder/continue";
constexpr char kDropboxLongPoll[] = "https://notify.dropboxapi.com/2/files/list_folder/longpoll";
constexpr int kDropboxWaitSeconds = 90;
constexpr auto kBookmarkBatchDelay = base::Milliseconds(1500);
constexpr auto kBookmarkSafetyCheck = base::Minutes(5);

std::string DropboxShardPath(const std::string& digest) {
  return "/sync-v2-" + digest + ".json";
}

std::string DropboxFileArgument(std::string path) {
  return *base::WriteJson(base::DictValue().Set("path", std::move(path)));
}

std::string DropboxWriteArgument(std::string path, const std::string& revision) {
  auto args = base::DictValue().Set("path", std::move(path))
      .Set("autorename", false).Set("strict_conflict", true);
  if (revision.empty()) args.Set("mode", "add");
  else args.Set("mode", base::DictValue().Set(".tag", "update").Set("update", revision));
  return *base::WriteJson(args);
}

bool DropboxError(int status, const std::string& body, std::string_view prefix) {
  if (status != 409) return false;
  const auto json = base::JSONReader::ReadDict(body, base::JSON_PARSE_RFC);
  const auto* error = json ? json->FindString("error_summary") : nullptr;
  return error && error->starts_with(prefix);
}

std::unique_ptr<network::SimpleURLLoader> MakeDropboxLoader(
    std::string endpoint, std::string_view access, std::string argument,
    std::string body, std::string_view mime, base::TimeDelta timeout) {
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GURL(endpoint);
  request->method = "POST";
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->redirect_mode = network::mojom::RedirectMode::kError;
  request->load_flags = net::LOAD_DISABLE_CACHE;
  if (!access.empty())
    request->headers.SetHeader("Authorization", base::StrCat({"Bearer ", access}));
  if (!argument.empty()) request->headers.SetHeader("Dropbox-API-Arg", argument);
  constexpr auto annotation = net::DefineNetworkTrafficAnnotation("zitter_dropbox_bookmarks", R"(
    semantics { sender: "Bookmark sync" description: "Synchronizes workspace data and waits for Dropbox changes."
      trigger: "The user connects Dropbox." data: "Authorization tokens, folder cursors and workspace data."
      destination: OTHER destination_other: "Dropbox" }
    policy { cookies_allowed: NO setting: "Disable bookmark sync in Settings."
      policy_exception_justification: "User-initiated desktop feature." })");
  auto loader = network::SimpleURLLoader::Create(std::move(request), annotation);
  loader->SetAllowHttpErrorResults(true);
  loader->SetTimeoutDuration(timeout);
  if (!mime.empty()) loader->AttachStringForUpload(std::move(body), mime);
  return loader;
}

base::TimeDelta DropboxRetryAfter(const net::HttpResponseHeaders& headers) {
  int seconds = 60;
  if (const auto delay = headers.GetNormalizedHeader("Retry-After"))
    base::StringToInt(*delay, &seconds);
  return base::Seconds(std::max(30, seconds));
}

std::string DropboxAppKey() {
  return base::Environment::Create()->GetVar("ZITTER_DROPBOX_APP_KEY")
      .value_or(BUILDFLAG(ZITTER_DROPBOX_APP_KEY));
}

std::vector<prototype::PinnedItem> SyncablePins(const std::vector<prototype::PinnedItem>& pins) {
  std::vector<prototype::PinnedItem> result;
  std::set<std::string> excluded;
  for (const auto& pin : pins) {
    if (pin.live_folder || excluded.contains(pin.parent_id)) {
      excluded.insert(pin.id);
    } else {
      result.push_back(pin);
    }
  }
  return result;
}

const base::DictValue* BookmarkScope(const base::DictValue& ledger, std::string_view key) {
  const auto* group = ledger.FindDict(key);
  const auto* record = group ? group->FindDict("") : nullptr;
  return record ? record->FindDict("scope") : nullptr;
}

bool HasBookmarkWorkspace(const base::DictValue& ledger,
                          const base::DictValue& collections,
                          std::string_view id) {
  const auto key = base::StrCat({"space/", id});
  const auto* pins = collections.FindList(key);
  const auto* tabs = collections.FindList(base::StrCat({"tabs2/", id}));
  return BookmarkScope(ledger, key) || (pins && !pins->empty()) ||
         (tabs && !tabs->empty());
}

// A login attempt belongs to the tab that started it, even when the user
// switches tabs or signs in through an identity provider.
class DropboxLogin final : public content::WebContentsUserData<DropboxLogin> {
 public:
  ~DropboxLogin() override;
  void Receive(const GURL& url);

 private:
  friend class content::WebContentsUserData<DropboxLogin>;
  DropboxLogin(content::WebContents* page, std::string state,
               base::OnceCallback<void(std::string)> done);
  WEB_CONTENTS_USER_DATA_KEY_DECL();
  std::string state_;
  base::OnceCallback<void(std::string)> done_;
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(DropboxLogin);
DropboxLogin::DropboxLogin(content::WebContents* page, std::string state,
                           base::OnceCallback<void(std::string)> done)
    : WebContentsUserData(*page), state_(std::move(state)), done_(std::move(done)) {}
DropboxLogin::~DropboxLogin() {
  if (done_) std::move(done_).Run({});
}

void DropboxLogin::Receive(const GURL& url) {
  std::string state, code, error;
  if (!done_ || !net::GetValueForKeyInQuery(url, "state", &state) || state != state_)
    return;
  if (!net::GetValueForKeyInQuery(url, "error", &error) &&
      (!net::GetValueForKeyInQuery(url, "code", &code) || code.empty() || code.size() > 1024))
    return;
  std::move(done_).Run(std::move(code));
}

class DropboxLoginThrottle final : public content::NavigationThrottle {
 public:
  using NavigationThrottle::NavigationThrottle;
  ThrottleCheckResult WillStartRequest() override;
  ThrottleCheckResult WillRedirectRequest() override;
  const char* GetNameForLogging() override;
};

const char* DropboxLoginThrottle::GetNameForLogging() {
  return "ZitterDropboxLogin";
}

content::NavigationThrottle::ThrottleCheckResult DropboxLoginThrottle::WillRedirectRequest() {
  return WillStartRequest();
}

content::NavigationThrottle::ThrottleCheckResult DropboxLoginThrottle::WillStartRequest() {
  const GURL& url = navigation_handle()->GetURL();
  GURL::Replacements parts;
  parts.ClearQuery();
  parts.ClearRef();
  if (url.ReplaceComponents(parts) != GURL(kDropboxRedirect)) return PROCEED;
  if (auto* login = DropboxLogin::FromWebContents(navigation_handle()->GetWebContents()))
    login->Receive(url);
  return CANCEL_AND_IGNORE;
}

}  // namespace

void AddDropboxLoginThrottle(content::NavigationThrottleRegistry& registry) {
  auto& navigation = registry.GetNavigationHandle();
  if (navigation.IsInPrimaryMainFrame() && DropboxLogin::FromWebContents(navigation.GetWebContents()))
    registry.AddThrottle(std::make_unique<DropboxLoginThrottle>(registry));
}

void WorkspaceLab::SetSyncStatus(std::u16string message) {
  sync_status_ = std::move(message);
  if (shell_) shell_->RefreshBookmarkSync();
}

bool WorkspaceLab::CanDeleteSyncedScope(std::string_view scope) const {
  if (!sync_prefs_ || sync_prefs_->GetString("method").empty() ||
      sync_prefs_->GetBoolean("import_pending")) return false;
  const auto& ledger = sync_prefs_->GetDict("ledger");
  return ledger.contains(scope) && !bookmark_sync::ScopeDeleted(ledger, scope);
}

void WorkspaceLab::ConfigureBookmarkSync(std::string method) {
  if (!sync_prefs_ || !CanEditWorkspace()) return;
  if (method == "now") {
    if (sync_prefs_->GetString("method").empty() || sync_busy_) return;
    sync_busy_ = true;
    SetSyncStatus(prototype::T(u"Zapisywanie sesji…"));
    SaveSession(base::BindOnce([](base::WeakPtr<WorkspaceLab> lab, bool saved) {
      if (!lab) return;
      lab->sync_busy_ = false;
      if (!saved) {
        lab->SetSyncStatus(prototype::T(
            u"Nie udało się zapisać sesji. Synchronizacja nie została uruchomiona."));
        return;
      }
      if (!lab->CanEditWorkspace() || base::Time::Now() < lab->sync_retry_after_) {
        lab->SetSyncStatus(prototype::T(u"Synchronizacja wstrzymana. Zitter spróbuje ponownie."));
        return;
      }
      lab->SetSyncStatus(prototype::T(u"Synchronizowanie…"));
      lab->RunBookmarkSync();
    }, sync_weak_.GetWeakPtr()));
    return;
  }
  dropbox_login_.InvalidateWeakPtrs();
  dropbox_login_timeout_.Stop();
  if (method == "off") {
    sync_weak_.InvalidateWeakPtrs();
    sync_timer_.Stop();
    sync_batch_timer_.Stop();
    sync_followup_pending_ = false;
    sync_last_success_ = {};
    dropbox_watch_timer_.Stop();
    dropbox_watch_loader_.reset();
    dropbox_watch_step_ = DropboxWatchStep::kCursor;
    dropbox_watch_cursor_.clear();
    dropbox_synced_revision_.clear();
    dropbox_notified_revision_.reset();
    dropbox_wait_after_ = {};
    dropbox_watch_healthy_ = false;
    dropbox_watch_missing_scope_ = false;
    sync_loader_.reset();
    folder_shard_cache_.clear();
    dropbox_exchange_.reset();
    dropbox_shard_cache_.clear();
    sync_busy_ = false;
    bookmark_extension_import_.clear();
    dropbox_access_.clear();
    for (const char* key : {"method", "folder", "credential", "device", "snapshot",
                            "ledger", "scope_snapshot", "import_pending"})
      sync_prefs_->ClearPref(key);
    sync_prefs_->CommitPendingWrite();
    if (!sync_deletions_.empty()) {
      sync_deletions_.clear();
      PersistWorkspaceChanges();
    }
    SetSyncStatus(prototype::T(u"Synchronizacja wyłączona. Zakładki pozostały na tym urządzeniu."));
    return;
  }
  if (!sync_prefs_->GetString("method").empty() || sync_busy_) return;
  if (method == "folder") {
    if (!sync_picker_) sync_picker_ = std::make_unique<SyncPicker>(this);
    auto* welcome = shell_->welcome_widget_for_testing();
    sync_picker_->Show((welcome ? welcome : widget_.get())->GetNativeWindow());
    return;
  }
  const auto key = DropboxAppKey();
  if (key.empty() || !os_crypt_) {
    SetSyncStatus(prototype::T(u"Połączenie z Dropbox jest niedostępne w tej wersji."));
    return;
  }
  std::string verifier, challenge;
  base::Base64UrlEncode(base::RandBytesAsVector(32), base::Base64UrlEncodePolicy::OMIT_PADDING, &verifier);
  base::Base64UrlEncode(crypto::hash::Sha256(verifier), base::Base64UrlEncodePolicy::OMIT_PADDING, &challenge);
  const GURL authorization_url("https://www.dropbox.com/oauth2/authorize?response_type=code"
      "&token_access_type=offline&code_challenge_method=S256&client_id=" +
      base::EscapeQueryParamValue(key, false) + "&code_challenge=" + challenge);
  const auto state = base::Uuid::GenerateRandomV4().AsLowercaseString();
  if (!model_.active() || !web_contents() ||
      tabs_[active_index_].size() >= prototype::kMaxWorkspaceSessionTabs) {
    SetSyncStatus(prototype::T(u"Nie można otworzyć karty Dropbox. Zamknij kartę i spróbuj ponownie."));
    return;
  }
  auto origin = web_contents()->GetWeakPtr();
  const bool welcome = shell_->SuspendWelcomeForLogin();
  shell_->CloseOverlays(true);
  AddTab(active_index_);
  SelectTab(tabs_[active_index_].size() - 1);
  auto* page = web_contents();
  auto pending = dropbox_login_.GetWeakPtr();
  DropboxLogin::CreateForWebContents(page, state, base::BindPostTaskToCurrentDefault(
      base::BindOnce([](base::WeakPtr<WorkspaceLab> lab, base::WeakPtr<content::WebContents> tab,
                        base::WeakPtr<content::WebContents> origin, bool welcome,
                        std::string verifier, std::string code) {
        if (!lab) return;
        lab->dropbox_login_timeout_.Stop();
        if (tab && lab->web_contents() == tab.get()) {
          lab->CloseTab(lab->selected_[lab->active_index_]);
          auto location = origin ? lab->TabForPage(origin.get()) : std::nullopt;
          if (location && location->first == lab->active_index_)
            lab->SelectTab(location->second);
        }
        if (welcome) lab->shell_->ResumeWelcomeAfterLogin();
        lab->ExchangeDropboxCode(std::move(verifier), std::move(code));
      }, pending, page->GetWeakPtr(), origin, welcome, std::move(verifier))));
  dropbox_login_timeout_.Start(FROM_HERE, base::Minutes(5), base::BindOnce(
      [](base::WeakPtr<WorkspaceLab> lab, bool welcome) {
        if (!lab) return;
        if (welcome) lab->shell_->ResumeWelcomeAfterLogin();
        lab->SetSyncStatus(prototype::T(u"Czas na połączenie minął. Wybierz ponownie Dropbox."));
        lab->dropbox_login_.InvalidateWeakPtrs();
      }, pending, welcome));
  SetSyncStatus(prototype::T(u"Zezwól na dostęp w Dropbox. Zitter połączy się automatycznie."));
  prototype::LoadWorkspaceTab(page, GURL(authorization_url.spec() + "&redirect_uri=" +
      base::EscapeQueryParamValue(kDropboxRedirect, false) + "&state=" + state), ui::PAGE_TRANSITION_LINK);
  widget_->Activate();
  page->Focus();
}

void WorkspaceLab::StartBookmarkSync(std::string method) {
  sync_weak_.InvalidateWeakPtrs();
  sync_prefs_->SetString("device", base::Uuid::GenerateRandomV4().AsLowercaseString());
  sync_prefs_->SetString("method", method);
  sync_prefs_->SetBoolean("import_pending", true);
  sync_prefs_->CommitPendingWrite();
  sync_busy_ = false;
  sync_retry_after_ = {};
  sync_timer_.Start(FROM_HERE, base::Seconds(30), this,
                    &WorkspaceLab::RunPeriodicBookmarkSync);
  SetSyncStatus(prototype::T(u"Łączenie…"));
  RunBookmarkSync();
}

base::DictValue WorkspaceLab::CaptureBookmarks() {
  // Older sessions named an open pin on its tab, which never synced.
  if (AdoptPinnedTabNames())
    PersistWorkspaceChanges();
  base::DictValue result;
  for (size_t i = 0; i < pins_.size(); ++i) {
    const auto& workspace = model_.workspaces()[i];
    result.Set("space/" + workspace.id, WriteWorkspacePins(SyncablePins(pins_[i])));
    auto* tabs = result.EnsureList("tabs2/" + workspace.id);
    std::map<std::string, size_t> occurrences;
    std::set<std::string> used;
    for (const auto& tab : tabs_[i]) {
      if (!tab.pin.empty() && !tab.sync_id.empty())
        used.insert(tab.sync_id);
    }
    for (size_t index = 0; index < tabs_[i].size(); ++index) {
      const auto url = BookmarkTabUrl(i, index);
      if (!url.is_valid()) continue;
      auto& tab = tabs_[i][index];
      const size_t occurrence = occurrences[url.spec()]++;
      if (!bookmark_sync::ValidTabId(tab.sync_id))
        tab.sync_id = bookmark_sync::LegacyTabId(workspace.id, url.spec(), occurrence);
      if (used.contains(tab.sync_id))
        tab.sync_id = base::Uuid::GenerateRandomV4().AsLowercaseString();
      used.insert(tab.sync_id);
      auto data = base::DictValue().Set("id", tab.sync_id).Set("url", url.spec());
      const auto title = base::UTF16ToUTF8(tab.custom_title);
      if (!title.empty() && title.size() <= 8192) data.Set("title", title);
      tabs->Append(std::move(data));
    }
  }
  for (const auto& profile : profiles_) {
    if (removed_profiles_.contains(profile.id)) continue;
    auto found = profile_favorites_.find(profile.id);
    result.Set("profile/" + profile.id, found == profile_favorites_.end()
        ? base::ListValue() : WriteWorkspacePins(SyncablePins(found->second)));
  }
  return result;
}

std::vector<Workspace> WorkspaceLab::OrderedBookmarkWorkspaces() const {
  auto spaces = model_.workspaces();
  if (!sync_prefs_) return spaces;
  const auto& order = sync_prefs_->GetList("workspace_order");
  const auto rank = [&](const Workspace& space) {
    return std::ranges::find_if(order, [&](const base::Value& id) {
      return id.GetIfString() && id.GetString() == space.id;
    }) - order.begin();
  };
  std::stable_sort(spaces.begin(), spaces.end(),
                   [&](const Workspace& a, const Workspace& b) { return rank(a) < rank(b); });
  return spaces;
}

base::DictValue WorkspaceLab::CaptureBookmarkScopes() const {
  base::DictValue result;
  int position = 0;
  for (const auto& space : OrderedBookmarkWorkspaces()) {
    result.Set("space/" + space.id, base::DictValue().Set("profile", space.profile_id)
        .Set("name", space.name).Set("position", position++)
        .Set("color", static_cast<int>(shell_->workspace_color(space.id) & 0xffffff))
        .Set("emoji", base::UTF16ToUTF8(shell_->workspace_emoji(space.id))));
  }
  for (size_t i = 0; i < profiles_.size(); ++i) {
    const auto& profile = profiles_[i];
    if (removed_profiles_.contains(profile.id)) continue;
    base::ListValue extensions;
    for (const auto& extension : GetConfiguredExtensions(*contexts_[i])) {
      if (extension->from_webstore() && extensions.size() < 128)
        extensions.Append(extension->id());
    }
    result.Set("profile/" + profile.id, base::DictValue().Set("profile", profile.id)
        .Set("name", profile.name).Set("extensions", std::move(extensions)));
  }
  return result;
}

GURL WorkspaceLab::BookmarkTabUrl(size_t workspace, size_t index) const {
  const auto& tab = tabs_[workspace][index];
  // Pins already sync in their own collections. The tab displaying a pin
  // belongs to the local session, including the page it has navigated to.
  if (!tab.pin.empty())
    return {};
  auto* page = tab.view->web_contents();
  if (page && (DropboxLogin::FromWebContents(page) ||
               (page->GetController().GetLastCommittedEntry() &&
                page->GetController().GetLastCommittedEntry()->GetHasPostData())))
    return {};
  const auto url = page && !page->GetLastCommittedURL().is_empty()
      ? page->GetLastCommittedURL() : tab.deferred_url;
  return bookmark_sync::CanSyncTabUrl(url) ? url : GURL();
}

bool WorkspaceLab::ApplyBookmarkTabs(const base::DictValue& collections) {
  bool changed = false;
  for (size_t workspace = 0; workspace < tabs_.size(); ++workspace) {
    const auto& space = model_.workspaces()[workspace];
    const auto* incoming = collections.FindList("tabs2/" + space.id);
    if (!incoming) continue;

    auto& tabs = tabs_[workspace];
    const size_t previous_count = tabs.size();
    std::vector<GURL> urls;
    base::ListValue existing;
    std::set<std::string_view> pinned_tab_ids;
    for (size_t i = 0; i < previous_count; ++i) {
      if (!tabs[i].pin.empty() && !tabs[i].sync_id.empty())
        pinned_tab_ids.insert(tabs[i].sync_id);
      urls.push_back(BookmarkTabUrl(workspace, i));
      if (!urls.back().is_valid()) continue;
      auto value = base::DictValue().Set("id", tabs[i].sync_id).Set("url", urls.back().spec());
      const auto title = base::UTF16ToUTF8(tabs[i].custom_title);
      if (!title.empty() && title.size() <= 8192) value.Set("title", title);
      existing.Append(std::move(value));
    }
    if (existing == *incoming) continue;

    std::vector<size_t> order;
    std::vector<bool> retained(previous_count, false);
    std::vector<const base::DictValue*> added;
    std::map<size_t, std::u16string> renamed;
    for (const auto& value : *incoming) {
      const auto& remote = value.GetDict();
      const GURL url(*remote.FindString("url"));
      const auto& id = *remote.FindString("id");
      // A legacy copy of a tab already open at a local pin is redundant.
      if (pinned_tab_ids.contains(id))
        continue;
      size_t match = 0;
      while (match < previous_count &&
             (retained[match] || urls[match] != url || tabs[match].sync_id != id)) ++match;
      if (match < previous_count) {
        retained[match] = true;
        order.push_back(match);
        const auto* title = remote.FindString("title");
        const auto name = title ? base::UTF8ToUTF16(*title) : std::u16string();
        if (tabs[match].custom_title != name) renamed.emplace(match, name);
      } else {
        order.push_back(previous_count + added.size());
        added.push_back(&remote);
      }
    }
    const auto is_empty_placeholder = [&] {
      if (previous_count != 1 || incoming->empty() || !tabs.front().pin.empty())
        return false;
      const auto url = WorkspaceTabUrl(workspace, 0);
      return url.is_empty() || url == GURL("about:blank");
    }();
    for (size_t i = 0; i < previous_count; ++i) {
      if (retained[i]) continue;
      if (!urls[i].is_valid() && !is_empty_placeholder) {
        retained[i] = true;
        order.push_back(i);
      }
    }
    if (order.size() > prototype::kMaxWorkspaceSessionTabs) continue;

    bool blocked = false;
    for (size_t i = 0; i < previous_count; ++i) {
      if (retained[i]) continue;
      auto* page = tabs[i].view->web_contents();
      if (page && (page->NeedToFireBeforeUnload() || page->GetController().GetPendingEntry())) {
        blocked = true;
        break;
      }
    }
    if (blocked || WorkspaceHasCapture(workspace)) continue;
    if (added.empty() && renamed.empty() && order.size() == previous_count &&
        std::ranges::is_sorted(order)) continue;

    const bool active = workspace == active_index_;
    const auto selected = selected_[workspace];
    const auto new_index = [&](size_t previous) -> std::optional<size_t> {
      const auto it = std::ranges::find(order, previous);
      return it == order.end() ? std::nullopt
                              : std::optional<size_t>(it - order.begin());
    };
    const auto retained_selection = new_index(selected);
    auto new_selected = retained_selection;
    if (!new_selected && !tabs[selected].sync_id.empty()) {
      const auto replacement = std::ranges::find_if(added, [&](const auto* tab) {
        return *tab->FindString("id") == tabs[selected].sync_id;
      });
      if (replacement != added.end())
        new_selected = new_index(previous_count + (replacement - added.begin()));
    }
    base::AutoReset<bool> updating(&updating_panes_, true);
    if (active && !retained_selection) {
      ExitPageFullscreen();
      CancelPermissionPrompt();
      devtools_.reset();
      Observe(nullptr);
      shell_->SetPanes({}, {});
    }
    auto split = splits_[workspace];
    for (auto& member : split.tabs) {
      const auto index = new_index(member);
      if (!index) {
        split = {};
        break;
      }
      member = *index;
    }
    for (const auto& [index, name] : renamed) tabs[index].custom_title = name;
    for (const auto* tab : added) {
      const GURL url(*tab->FindString("url"));
      AddTab(workspace, url);
      imported_favicon_urls_[space.profile_id].insert(url);
      tabs.back().sync_id = *tab->FindString("id");
      if (const auto* title = tab->FindString("title"))
        tabs.back().custom_title = base::UTF8ToUTF16(*title);
    }
    std::vector<Tab> result;
    result.reserve(order.size());
    for (const auto index : order) result.push_back(std::move(tabs[index]));
    for (size_t i = 0; i < previous_count; ++i) {
      if (retained[i]) continue;
      HideTabView(*tabs[i].view);
      if (tabs[i].view->parent())
        tabs[i].detached_view = shell_->content_host()->RemoveChildViewT(tabs[i].view.get());
    }
    tabs = std::move(result);
    if (tabs.empty()) AddTab(workspace);
    splits_[workspace] = std::move(split);
    selected_[workspace] = new_selected.value_or(0);
    if (active) ShowTab();
    changed = true;
  }
  PumpImportedFavicons();
  return changed;
}

bool WorkspaceLab::PrepareBookmarkScopes(const base::DictValue& ledger,
                                         const base::DictValue& collections,
                                         base::OnceClosure resume, bool restore) {
  // Create only containers needed by incoming bookmarks. Keep local profile
  // isolation, including on a second computer with no existing custom profiles.
  for (const auto [scope, items] : collections) {
    const auto* info = BookmarkScope(ledger, scope);
    if (!info) continue;
    if (items.GetList().empty() && !info->contains("position") &&
        !info->contains("extensions")) continue;
    const auto* profile = info->FindString("profile");
    const auto* name = info->FindString("name");
    if (!profile || !name || !IsContentWorkspaceName(*name)) continue;
    if (removed_profiles_.contains(*profile)) {
      if (restore) return false;
      continue;
    }
    if (!HasProfile(*profile)) {
      if (ProfileCount() >= kMaxContentProfiles) continue;
      if (!profile->starts_with("profile-") ||
          !base::Uuid::ParseLowercase(std::string_view(*profile).substr(8)).is_valid())
        return false;
      const auto* group = ledger.FindDict("profile/" + *profile);
      const auto* descriptor = group ? group->FindDict("") : nullptr;
      const auto* identity = descriptor ? descriptor->FindDict("scope") : nullptr;
      const auto* label = identity ? identity->FindString("name") : nullptr;
      if (!label || !IsContentWorkspaceName(*label)) return false;
      ContentProfileInfo incoming{*profile, *label, BUILDFLAG(IS_MAC)};
      auto session = CaptureSession();
      session.EnsureList("extra_profiles")->Append(WriteProfileMetadata(incoming));
      saving_catalogue_ = true;
      session_writer_->PostTaskAndReplyWithResult(FROM_HERE,
          base::BindOnce(&ProvisionProfile, session_path_, incoming, std::move(session)),
          base::BindOnce([](base::WeakPtr<WorkspaceLab> lab, ContentProfileInfo incoming,
                            base::OnceClosure resume, bool ok) {
            if (!lab) return;
            lab->saving_catalogue_ = false;
            if (ok) {
              auto context = lab->OpenNewProfile(incoming);
              lab->PublishProfile(std::move(incoming), std::move(context));
              lab->RefreshShell();
            } else lab->FinishBookmarkSync({}, std::nullopt);
            if (std::exchange(lab->close_after_catalogue_save_, false)) {
              lab->RequestClose();
              return;
            }
            if (!ok) return;
            if (resume) std::move(resume).Run();
            else lab->RunBookmarkSync();
          }, weak_factory_.GetWeakPtr(), incoming, std::move(resume)));
      return false;
    }
  }
  return true;
}

bool WorkspaceLab::ApplyBookmarks(const base::DictValue& ledger,
                                  const base::DictValue& collections, bool replace) {
  auto spaces = model_.workspaces();
  auto candidate = CaptureSession();
  auto tombstones = removed_workspaces_.Clone();
  for (const auto [scope, list] : collections) {
    if (!scope.starts_with("space/")) continue;
    const std::string id(scope.substr(6));
    const bool builtin = std::ranges::contains(kWorkspaceIds, id);
    if (WorkspaceIndex(id) || (!replace && (builtin || tombstones.contains(id)))) continue;
    const auto* info = BookmarkScope(ledger, scope);
    if (list.GetList().empty() && (!info || !info->contains("position"))) continue;
    if (!info || !info->FindString("profile") || !info->FindString("name")) return false;
    Workspace space{id, *info->FindString("profile"), *info->FindString("name")};
    if (!HasProfile(space.profile_id)) continue;
    if (builtin)
      SetCatalogueName(candidate, id, space.name, "workspace_names", "extra_workspaces");
    else
      AppendWorkspaceMetadata(candidate, space, {});
    tombstones.Remove(id);
    candidate.Set(id, base::DictValue().Set("profile_id", space.profile_id).Set("active", 0)
        .Set("tabs", base::ListValue().Append(prototype::WriteWorkspaceSessionTab(GURL(), u""))));
    spaces.push_back(std::move(space));
  }
  std::vector<size_t> discarded;
  std::optional<std::string> fallback;
  auto rules = link_rules_.Clone();
  {
    // Older backups can contain only profile favorites. Keep the catalogue
    // when the backup has no spaces to restore.
    const auto remote = [&](const Workspace& space) {
      return HasBookmarkWorkspace(ledger, collections, space.id);
    };
    const bool replace_catalogue = replace && std::ranges::any_of(spaces, remote);
    if (replace_catalogue ||
        std::ranges::any_of(spaces, [&](const Workspace& space) {
          return bookmark_sync::ScopeDeleted(ledger, "space/" + space.id);
        })) {
      for (size_t i = 0; i < model_.workspaces().size(); ++i) {
        const auto& space = spaces[i];
        const bool deleted = bookmark_sync::ScopeDeleted(ledger, "space/" + space.id);
        if ((!deleted && (!replace_catalogue || remote(space))) ||
            spaces.size() - discarded.size() <= 1 ||
            std::ranges::any_of(pins_[i], [](const auto& pin) {
              return pin.live_folder.has_value();
            })) {
          continue;
        }
        if (WorkspaceHasCapture(i) || (peek_workspace_id_ == space.id && PeekPage()) ||
            std::ranges::any_of(WorkspacePages(i), [](auto* page) {
              return page->NeedToFireBeforeUnload() || page->GetController().GetPendingEntry();
            })) {
          if (replace) return false;
          continue;
        }
        discarded.push_back(i);
        tombstones.Set(space.id, space.profile_id);
        candidate.Remove(space.id);
        if (const auto* existing_rules = candidate.FindDict("link_rules")) {
          for (const auto [host, target] : *existing_rules)
            if (target.GetString() == space.id) rules.Remove(host);
        }
        if (auto* names = candidate.FindDict("workspace_names")) {
          names->Remove(space.id);
          if (names->empty()) candidate.Remove("workspace_names");
        }
        if (auto* extras = candidate.FindList("extra_workspaces")) {
          extras->EraseIf([&](const base::Value& item) {
            return *item.GetDict().FindString("id") == space.id;
          });
          if (extras->empty()) candidate.Remove("extra_workspaces");
        }
      }
      if (std::ranges::contains(discarded, active_index_)) {
        const auto first = std::ranges::find_if(spaces, [&](const Workspace& space) {
          return candidate.contains(space.id);
        });
        fallback = first->id;
        candidate.Set("active_workspace", *fallback);
        candidate.FindDict(*fallback)->Remove("sleeping");
      }
      if (rules.empty()) candidate.Remove("link_rules");
      else candidate.Set("link_rules", rules.Clone());
    }
  }
  // The ledger can contain more live spaces than a single installation can
  // open. Leave the overflow in sync and apply everything that fits.
  while (spaces.size() - discarded.size() > kMaxContentWorkspaces) {
    const auto& space = spaces.back();
    candidate.Remove(space.id);
    if (const auto* old = removed_workspaces_.Find(space.id))
      tombstones.Set(space.id, old->Clone());
    if (auto* names = candidate.FindDict("workspace_names")) {
      names->Remove(space.id);
      if (names->empty()) candidate.Remove("workspace_names");
    }
    if (auto* extras = candidate.FindList("extra_workspaces")) {
      extras->EraseIf([&](const base::Value& item) {
        return *item.GetDict().FindString("id") == space.id;
      });
      if (extras->empty()) candidate.Remove("extra_workspaces");
    }
    spaces.pop_back();
  }
  if (tombstones.empty()) candidate.Remove("removed_workspaces");
  else candidate.Set("removed_workspaces", tombstones.Clone());
  auto pins = pins_;
  pins.resize(spaces.size());
  auto favorites = profile_favorites_;
  std::map<std::string, std::set<GURL>> icons;
  auto update = [&](const std::string& key, auto& dest, bool profile,
                    const std::string& profile_id) {
    const auto* list = collections.FindList(key);
    if (!list && !replace) return true;
    auto decoded = ReadWorkspacePins(base::DictValue().Set(
        "pins", list ? list->Clone() : base::ListValue()));
    if (!decoded) return false;
    for (const auto& pin : *decoded)
      if (pin.profile_pin != profile || pin.live_folder) return false;
    auto syncable = SyncablePins(dest);
    if (*decoded == syncable) return true;
    for (const auto& pin : *decoded) {
      if (!pin.folder && pin.url.SchemeIsHTTPOrHTTPS() &&
          !pin.url.has_username() && !pin.url.has_password() &&
          !std::ranges::contains(dest, pin.url, &prototype::PinnedItem::url))
        icons[profile_id].insert(pin.url);
    }
    for (size_t i = 0; i < dest.size(); ++i) {
      auto pin = dest[i];
      if (std::ranges::contains(syncable, pin.id, &prototype::PinnedItem::id)) continue;
      size_t at = std::min(i, decoded->size());
      if (!pin.parent_id.empty()) {
        const auto parent = std::ranges::find(*decoded, pin.parent_id, &prototype::PinnedItem::id);
        if (parent == decoded->end()) pin.parent_id.clear();
        else at = std::max(at, static_cast<size_t>(parent - decoded->begin() + 1));
      }
      decoded->insert(decoded->begin() + at, std::move(pin));
    }
    dest = std::move(*decoded);
    return !!ReadWorkspacePins(base::DictValue().Set("pins", WriteWorkspacePins(dest)));
  };
  for (size_t i = 0; i < pins.size(); ++i)
    if (!bookmark_sync::ScopeDeleted(ledger, "space/" + spaces[i].id) &&
        !update("space/" + spaces[i].id, pins[i], false, spaces[i].profile_id)) return false;
  for (const auto& profile : profiles_) {
    if (removed_profiles_.contains(profile.id) ||
        bookmark_sync::ScopeDeleted(ledger, "profile/" + profile.id)) continue;
    if (!update("profile/" + profile.id, favorites[profile.id], true, profile.id)) return false;
  }
  candidate.Remove("profile_favorites");
  for (const auto& [id, items] : favorites)
    if (!items.empty()) candidate.EnsureDict("profile_favorites")->Set(id, WriteWorkspacePins(items));
  std::map<std::string, std::string> moved;
  std::map<std::string, std::string> profile_names;
  for (const auto& profile : profiles_) {
    if (removed_profiles_.contains(profile.id) ||
        bookmark_sync::ScopeDeleted(ledger, "profile/" + profile.id)) continue;
    const auto* info = BookmarkScope(ledger, "profile/" + profile.id);
    const auto* name = info ? info->FindString("name") : nullptr;
    if (name && *name != profile.name) {
      SetCatalogueName(candidate, profile.id, *name, "profile_names", "extra_profiles");
      profile_names.emplace(profile.id, *name);
    }
  }
  for (size_t i = 0; i < spaces.size(); ++i) {
    auto* entry = candidate.FindDict(spaces[i].id);
    if (!entry) continue;
    entry->Set("pins", WriteWorkspacePins(pins[i]));
    const auto* info = BookmarkScope(ledger, "space/" + spaces[i].id);
    const auto* owner = info ? info->FindString("profile") : nullptr;
    if (i < tabs_.size() && owner && *owner != spaces[i].profile_id && HasProfile(*owner) &&
        !bookmark_sync::ScopeDeleted(ledger, "profile/" + *owner) &&
        !bookmark_sync::ScopeDeleted(ledger, "space/" + spaces[i].id) &&
        !WorkspaceHasCapture(i) && !(peek_workspace_id_ == spaces[i].id && PeekPage()) &&
        std::ranges::none_of(WorkspacePages(i), [](auto* page) {
          return page->NeedToFireBeforeUnload() || page->GetController().GetPendingEntry();
        })) {
      auto epoch = base::Uuid::GenerateRandomV4().AsLowercaseString();
      candidate.EnsureDict("timeline_epochs")->Set(spaces[i].id, epoch);
      moved.emplace(spaces[i].id, std::move(epoch));
      spaces[i].profile_id = *owner;
      entry->Set("profile_id", *owner);
      entry->Set("active", 0);
      entry->Set("tabs", base::ListValue().Append(
          prototype::WriteWorkspaceSessionTab(GURL(), u"")));
      entry->Remove("split");
      entry->Remove("sleeping");
    }
    for (auto& value : *entry->FindList("tabs")) {
      auto& tab = value.GetDict();
      const auto* id = tab.FindString("pin");
      if (id && !std::ranges::contains(pins[i], *id, &prototype::PinnedItem::id) &&
          !std::ranges::contains(favorites[spaces[i].profile_id], *id, &prototype::PinnedItem::id))
        tab.Remove("pin");
    }
  }
  if (!ValidateContentWorkspaceSession(candidate)) return false;
  for (size_t i = pins_.size(); i < spaces.size(); ++i) AppendWorkspace(spaces[i], nullptr);
  pins_ = std::move(pins);
  profile_favorites_ = std::move(favorites);
  for (auto& profile : profiles_) {
    const auto name = profile_names.find(profile.id);
    if (name != profile_names.end()) profile.name = name->second;
  }
  for (const auto& [id, epoch] : moved) {
    const size_t index = WorkspaceIndex(id).value();
    const auto& space = spaces[index];
    CHECK(model_.Update(id, space.name, space.profile_id));
    ReplaceWorkspaceProfile(index, epoch);
  }
  if (fallback) CHECK(controller_.Activate(*fallback));
  for (auto it = discarded.rbegin(); it != discarded.rend(); ++it)
    EraseWorkspaceAt(*it);
  removed_workspaces_ = std::move(tombstones);
  link_rules_ = std::move(rules);
  if (removed_workspace_ && !removed_workspaces_.contains(removed_workspace_->workspace.id))
    removed_workspace_.reset();
  std::vector<std::pair<int, std::string>> order;
  for (const auto& space : model_.workspaces()) {
    if (bookmark_sync::ScopeDeleted(ledger, "space/" + space.id)) continue;
    const auto* info = BookmarkScope(ledger, "space/" + space.id);
    if (!info) continue;
    const auto position = info->FindInt("position");
    if (!position) continue;
    order.emplace_back(*position, space.id);
    const auto& name = *info->FindString("name");
    if (name != space.name) model_.Update(space.id, name, space.profile_id);
    if (const auto color = info->FindInt("color");
        color && (shell_->workspace_color(space.id) & 0xffffff) != static_cast<unsigned>(*color))
      shell_->SetWorkspaceColor(space.id, static_cast<SkColor>(*color));
    if (const auto* emoji = info->FindString("emoji");
        emoji && shell_->workspace_emoji(space.id) != base::UTF8ToUTF16(*emoji))
      shell_->SetWorkspaceEmoji(space.id, base::UTF8ToUTF16(*emoji));
  }
  if (!order.empty()) {
    std::ranges::sort(order);
    base::ListValue ids;
    for (const auto& [position, id] : order) ids.Append(id);
    if (ids != sync_prefs_->GetList("workspace_order"))
      sync_prefs_->SetList("workspace_order", std::move(ids));
  }
  for (size_t i = 0; i < tabs_.size(); ++i) {
    const auto visible = WorkspacePins(i);
    for (auto& tab : tabs_[i])
      if (!tab.pin.empty() && !std::ranges::contains(visible, tab.pin, &prototype::PinnedItem::id))
        tab.pin.clear();
  }
  for (auto& [profile, urls] : icons) imported_favicon_urls_[profile].merge(urls);
  return true;
}

void WorkspaceLab::RunPeriodicBookmarkSync() {
  if (dropbox_watch_healthy_ && !sync_last_success_.is_null() &&
      base::TimeTicks::Now() - sync_last_success_ < kBookmarkSafetyCheck) {
    return;
  }
  RunBookmarkSync();
}

void WorkspaceLab::ScheduleBookmarkSync() {
  if (!sync_prefs_ || !ready_for_external_urls_ ||
      sync_prefs_->GetString("method").empty() ||
      sync_prefs_->GetBoolean("import_pending")) return;
  sync_batch_timer_.Start(FROM_HERE, kBookmarkBatchDelay, this,
                          &WorkspaceLab::RunDebouncedBookmarkSync);
}

void WorkspaceLab::RunDebouncedBookmarkSync() {
  if (sync_prefs_->GetString("method").empty() ||
      sync_prefs_->GetBoolean("import_pending")) return;
  const auto retry_delay = sync_retry_after_ - base::Time::Now();
  if (sync_busy_ || !CanEditWorkspace() || !shell_->appearance_ready() ||
      retry_delay.is_positive()) {
    sync_batch_timer_.Start(FROM_HERE, std::max(kBookmarkBatchDelay, retry_delay),
                            this, &WorkspaceLab::RunDebouncedBookmarkSync);
    return;
  }
  const bool local_changed = CaptureBookmarks() != sync_prefs_->GetDict("snapshot") ||
      CaptureBookmarkScopes() != sync_prefs_->GetDict("scope_snapshot") ||
      !sync_deletions_.empty();
  const bool remote_changed = dropbox_notified_revision_ &&
      *dropbox_notified_revision_ != dropbox_synced_revision_;
  dropbox_notified_revision_.reset();
  const bool followup = std::exchange(sync_followup_pending_, false);
  if (!local_changed) {
    if (remote_changed || followup) RunBookmarkSync();
    return;
  }
  sync_busy_ = true;
  SaveSession(base::BindOnce(
      [](base::WeakPtr<WorkspaceLab> lab, bool followup, bool saved) {
        if (!lab) return;
        lab->sync_busy_ = false;
        if (saved && lab->CanEditWorkspace()) {
          lab->RunBookmarkSync();
        } else if (saved) {
          lab->sync_followup_pending_ |= followup;
          lab->ScheduleBookmarkSync();
        } else {
          lab->FinishBookmarkSync({}, std::nullopt);
        }
      }, sync_weak_.GetWeakPtr(), remote_changed || followup));
}

void WorkspaceLab::RunBookmarkSync() {
  if (!sync_prefs_ || sync_busy_ || !CanEditWorkspace() || base::Time::Now() < sync_retry_after_) return;
  const auto method = sync_prefs_->GetString("method");
  if (method.empty()) return;
  if (!shell_->appearance_ready()) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, base::BindOnce(&WorkspaceLab::RunBookmarkSync, sync_weak_.GetWeakPtr()),
        base::Milliseconds(100));
    return;
  }
  const auto device = base::Uuid::ParseLowercase(sync_prefs_->GetString("device"));
  if (!device.is_valid() || (method != "folder" && method != "dropbox")) {
    SetSyncStatus(prototype::T(u"Wyłącz synchronizację i skonfiguruj ją ponownie."));
    return;
  }
  for (const auto [key, list] : sync_prefs_->GetDict("snapshot")) {
    if (!list.is_list() || !bookmark_sync::ValidSnapshot(key, list.GetList())) {
      SetSyncStatus(prototype::T(u"Wyłącz synchronizację i skonfiguruj ją ponownie."));
      return;
    }
  }
  sync_busy_ = true;
  auto snapshot = CaptureBookmarks();
  base::DictValue validated;
  if (!bookmark_sync::Merge(validated, sync_prefs_->GetDict("ledger"))) {
    FinishBookmarkSync(std::move(snapshot), std::nullopt);
    return;
  }
  bookmark_sync::UpgradeLegacyTabs(validated);
  const bool importing = sync_prefs_->GetBoolean("import_pending");
  base::DictValue ledger;
  if (!importing) {
    ledger = bookmark_sync::RecordEdits(sync_prefs_->GetDict("snapshot"), snapshot,
                                       validated, sync_prefs_->GetString("device"));
    sync_sent_scopes_ = CaptureBookmarkScopes();
    bookmark_sync::RecordScopeEdits(sync_prefs_->GetDict("scope_snapshot"), sync_sent_scopes_,
                                   ledger, sync_prefs_->GetString("device"));
    for (const auto [scope, owner] : sync_deletions_)
      if (owner.GetString() == device.AsLowercaseString())
        bookmark_sync::RecordScopeDeletion(ledger, scope, owner.GetString());
  }
  if (method == "folder") {
    session_writer_->PostTaskAndReplyWithResult(FROM_HERE, base::BindOnce(
        &bookmark_sync::ExchangeFolder, sync_prefs_->GetFilePath("folder"),
        sync_prefs_->GetString("device"), std::move(ledger), importing,
        std::move(folder_shard_cache_)), base::BindOnce(
        [](base::WeakPtr<WorkspaceLab> lab, base::DictValue snapshot,
           std::optional<bookmark_sync::FolderResult> result) {
          if (!lab) return;
          if (!result) {
            lab->FinishBookmarkSync(std::move(snapshot), std::nullopt);
            return;
          }
          lab->folder_shard_cache_ = std::move(result->cache);
          lab->FinishBookmarkSync(std::move(snapshot), std::move(result->ledger));
        }, sync_weak_.GetWeakPtr(), std::move(snapshot)));
    return;
  }
  if (dropbox_access_.empty() || dropbox_expires_ <= base::Time::Now()) {
    if (!os_crypt_) { FinishBookmarkSync({}, std::nullopt); return; }
    os_crypt_->GetInstance(base::BindOnce([](base::WeakPtr<WorkspaceLab> lab,
                                scoped_refptr<os_crypt_async::Encryptor> crypt) {
      if (!lab) return;
      std::string bytes, token;
      if (!crypt || !base::Base64Decode(lab->sync_prefs_->GetString("credential"), &bytes) ||
          !crypt->DecryptString(bytes, &token) || !token.starts_with(kDropboxCredentialPrefix)) {
        lab->FinishBookmarkSync({}, std::nullopt); return;
      }
      lab->DropboxToken("grant_type=refresh_token&refresh_token=" +
                        base::EscapeQueryParamValue(std::string_view(token).substr(kDropboxCredentialPrefix.size()), false), false);
    }, sync_weak_.GetWeakPtr()));
    return;
  }
  PumpDropboxWatch();
  DownloadDropboxBookmarks(std::move(snapshot), std::move(ledger));
}

void WorkspaceLab::FinishBookmarkSync(base::DictValue snapshot,
                                       std::optional<base::DictValue> ledger) {
  sync_busy_ = false;
  if (!CanEditWorkspace()) {
    sync_last_success_ = {};
    if (ledger) {
      sync_followup_pending_ = true;
      ScheduleBookmarkSync();
    }
    return;
  }
  if (ledger) {
    bookmark_sync::UpgradeLegacyTabs(*ledger);
    if (sync_prefs_->GetBoolean("import_pending")) {
      ReviewBookmarkImport(std::move(*ledger));
      return;
    }
    // Catch edits made while reading the folder or waiting for the network.
    auto current = CaptureBookmarks();
    auto current_scopes = CaptureBookmarkScopes();
    auto reconciled = bookmark_sync::RecordEdits(snapshot, current, *ledger,
                                                sync_prefs_->GetString("device"));
    bookmark_sync::RecordScopeEdits(sync_sent_scopes_, current_scopes,
                                   reconciled, sync_prefs_->GetString("device"));
    auto merged = bookmark_sync::Materialize(reconciled);
    if (merged && !PrepareBookmarkScopes(reconciled, *merged)) {
      if (!saving_catalogue_) SetSyncStatus(prototype::T(u"Synchronizacja wstrzymana. Zitter spróbuje ponownie."));
      return;
    }
    const bool deleted_space = std::ranges::any_of(model_.workspaces(), [&](const Workspace& space) {
      return bookmark_sync::ScopeDeleted(reconciled, "space/" + space.id);
    });
    const bool changed = merged && (deleted_space || std::ranges::any_of(*merged, [&](const auto& entry) {
      const auto [scope, items] = entry;
      const auto* local = current.FindList(scope);
      const auto* info = BookmarkScope(reconciled, scope);
      const auto* local_info = current_scopes.FindDict(scope);
      if (info && info->contains("position") && (!local_info || *info != *local_info))
        return true;
      if (scope.starts_with("profile/") && info && local_info &&
          *info->FindString("name") != *local_info->FindString("name"))
        return true;
      return local ? items != *local : !items.GetList().empty();
    }));
    const auto previous_order = sync_prefs_->GetList("workspace_order").Clone();
    if (merged && (!changed || ApplyBookmarks(reconciled, *merged, false))) {
      const bool tabs_applied = ApplyBookmarkTabs(*merged);
      std::vector<std::string> acknowledged;
      for (const auto [scope, device] : sync_deletions_)
        if (bookmark_sync::ScopeDeleted(*ledger, scope)) acknowledged.emplace_back(scope);
      for (const auto& scope : acknowledged)
        sync_deletions_.Remove(scope);
      auto after = changed ? CaptureBookmarks() : current.Clone();
      auto scopes = changed || tabs_applied ? CaptureBookmarkScopes() : current_scopes.Clone();
      const bool applied = tabs_applied || after != current || scopes != current_scopes ||
                           previous_order != sync_prefs_->GetList("workspace_order");
      const bool followup = reconciled != *ledger;
      auto commit = base::BindOnce([](base::WeakPtr<WorkspaceLab> lab, base::DictValue after,
                                      base::DictValue scopes, base::DictValue reconciled,
                                      bool followup, bool ok) {
        if (!lab) return;
        lab->sync_busy_ = false;
        if (!ok) { lab->FinishBookmarkSync({}, std::nullopt); return; }
        lab->sync_last_success_ = base::TimeTicks::Now();
        auto& prefs = *lab->sync_prefs_;
        if (prefs.GetDict("snapshot") != after || prefs.GetDict("scope_snapshot") != scopes ||
            prefs.GetDict("ledger") != reconciled) {
          prefs.SetDict("snapshot", std::move(after));
          prefs.SetDict("scope_snapshot", std::move(scopes));
          prefs.SetDict("ledger", std::move(reconciled));
          prefs.CommitPendingWrite();
        }
        const auto destination = prefs.GetString("method") == "folder"
            ? prefs.GetFilePath("folder").LossyDisplayName() : u"Dropbox";
        lab->SetSyncStatus(destination + u" · " + prototype::T(u"Zsynchronizowano") +
                           u" " + base::TimeFormatTimeOfDay(base::Time::Now()));
        if (followup) {
          lab->sync_followup_pending_ = true;
          lab->ScheduleBookmarkSync();
        }
      }, sync_weak_.GetWeakPtr(), after.Clone(), std::move(scopes), std::move(reconciled), followup);
      // Commit the baseline only after its matching workspace state is durable.
      if (applied || !acknowledged.empty() || sync_prefs_->GetDict("snapshot") != after) {
        sync_busy_ = true;
        if (applied) RefreshShell();
        SaveSession(std::move(commit));
      } else std::move(commit).Run(true);
      return;
    }
  }
  sync_last_success_ = {};
  SetSyncStatus(prototype::T(u"Synchronizacja wstrzymana. Zitter spróbuje ponownie."));
}

std::vector<std::pair<std::string, std::string>>
WorkspaceLab::BookmarkExtensionCandidates(const base::DictValue& ledger) const {
  std::vector<std::pair<std::string, std::string>> candidates;
  for (const auto [key, records] : ledger) {
    if (!key.starts_with("profile/")) continue;
    const auto* scope = BookmarkScope(ledger, key);
    const auto* ids = scope ? scope->FindList("extensions") : nullptr;
    if (!ids || ids->empty()) continue;
    const std::string profile(key.substr(8));
    if (removed_profiles_.contains(profile)) continue;
    const auto owner = std::ranges::find(profiles_, profile, &ContentProfileInfo::id);
    const auto installed = owner == profiles_.end()
        ? std::vector<scoped_refptr<const extensions::Extension>>()
        : GetConfiguredExtensions(*contexts_[owner - profiles_.begin()]);
    for (const auto& id : *ids) {
      if (!std::ranges::contains(installed, id.GetString(), &extensions::Extension::id))
        candidates.emplace_back(profile, id.GetString());
    }
  }
  return candidates;
}

void WorkspaceLab::ReviewBookmarkImport(base::DictValue ledger) {
  if (ledger.empty()) {
    CommitBookmarkImport(std::move(ledger), false, false);
    return;
  }
  const auto incoming = bookmark_sync::Materialize(ledger);
  if (!incoming) {
    FinishBookmarkSync({}, std::nullopt);
    return;
  }
  prototype::WorkspaceShell::BookmarkImportPreview preview;
  std::set<std::string> profiles;
  std::map<std::string, std::pair<int, std::string>> spaces;
  for (const auto [key, items] : *incoming) {
    if (key.starts_with("profile/")) {
      profiles.emplace(key.substr(8));
      preview.bookmarks += items.GetList().size();
    } else if (bookmark_sync::IsTabScope(key)) {
      preview.tabs += items.GetList().size();
      if (!items.GetList().empty())
        spaces.try_emplace(std::string(key.substr(6)), kMaxContentWorkspaces, "");
    } else if (key.starts_with("space/")) {
      preview.bookmarks += items.GetList().size();
      const auto* scope = BookmarkScope(ledger, key);
      const std::string id(key.substr(6));
      if (scope) {
        profiles.insert(*scope->FindString("profile"));
        spaces[id] = {scope->FindInt("position").value_or(kMaxContentWorkspaces),
                      *scope->FindString("name")};
      } else if (!items.GetList().empty()) {
        spaces.try_emplace(id, kMaxContentWorkspaces, "");
      }
    }
  }
  std::vector<std::tuple<int, std::string, std::string>> ordered;
  for (const auto& [id, item] : spaces)
    ordered.emplace_back(item.first, id, item.second);
  std::ranges::sort(ordered);
  for (const auto& [position, id, label] : ordered) {
    if (preview.workspace_names.size() >= 6) break;
    std::string name = label;
    if (name.empty()) {
      if (const auto* local_space = model_.Find(id)) name = local_space->name;
    }
    if (name.empty()) continue;
    const auto* tabs = incoming->FindList("tabs2/" + id);
    preview.workspace_names.push_back(prototype::TF(u"{} · Karty: {}",
        {base::UTF8ToUTF16(name), base::NumberToString16(tabs ? tabs->size() : 0)}));
  }
  preview.profiles = profiles.size();
  preview.workspaces = spaces.size();
  preview.extensions = BookmarkExtensionCandidates(ledger).size();
  const auto local = CaptureBookmarks();
  const bool has_local = std::ranges::any_of(local, [](const auto& entry) {
    return !entry.second.GetList().empty();
  }) || (!spaces.empty() && std::ranges::any_of(model_.workspaces(), [&](const Workspace& space) {
    return !spaces.contains(space.id);
  }));
  sync_busy_ = true;
  shell_->ReviewBookmarkImport(std::move(preview), has_local, base::BindOnce(
      [](base::WeakPtr<WorkspaceLab> lab, base::DictValue ledger,
         std::optional<bool> replace, bool extensions) {
        if (!lab) return;
        if (!replace) {
          lab->ConfigureBookmarkSync("off");
          return;
        }
        if (!*replace) {
          lab->CommitBookmarkImport(std::move(ledger), false, extensions);
          return;
        }
        auto* welcome = lab->shell_->welcome_widget_for_testing();
        auto* settings = lab->shell_->general_widget();
        auto* owner = welcome && welcome->IsVisible() ? welcome
            : settings && settings->IsVisible() ? settings : lab->widget_.get();
        lab->shell_->ConfirmDanger(owner, prototype::T(u"Zastąpić lokalne dane synchronizacji?"),
            prototype::T(u"Pobrana kopia zastąpi przestrzenie, zakładki i otwarte karty."),
            prototype::T(u"Lokalne przestrzenie i zakładki mogą zostać usunięte, a otwarte karty zamknięte."),
            prototype::T(u"Zastąp lokalne dane"), base::BindPostTaskToCurrentDefault(base::BindOnce(
                [](base::WeakPtr<WorkspaceLab> lab, base::DictValue ledger, bool extensions,
                   bool accepted) {
                  if (!lab) return;
                  if (accepted)
                    lab->CommitBookmarkImport(std::move(ledger), true, extensions);
                  else {
                    lab->sync_busy_ = false;
                    lab->ReviewBookmarkImport(std::move(ledger));
                  }
                }, lab, std::move(ledger), extensions)));
      }, sync_weak_.GetWeakPtr(), std::move(ledger)));
}

void WorkspaceLab::CommitBookmarkImport(base::DictValue ledger, bool replace, bool extensions) {
  sync_busy_ = false;
  if (!CanEditWorkspace()) return;
  auto incoming = bookmark_sync::Materialize(ledger);
  if (!incoming) {
    FinishBookmarkSync({}, std::nullopt);
    return;
  }
  auto resume = base::BindOnce(&WorkspaceLab::CommitBookmarkImport,
                               sync_weak_.GetWeakPtr(), ledger.Clone(), replace, extensions);
  if (!PrepareBookmarkScopes(ledger, *incoming, std::move(resume), replace)) {
    if (!saving_catalogue_) FinishBookmarkSync({}, std::nullopt);
    return;
  }
  auto extension_candidates = extensions ? BookmarkExtensionCandidates(ledger)
                                       : decltype(bookmark_extension_import_)();
  if (!replace) {
    // Remote items keep their saved positions and win on a shared ID.
    auto combined = incoming->Clone();
    auto local = CaptureBookmarks();
    for (auto [key, pins] : local) {
      const auto* records = ledger.FindDict(key);
      auto* destination = combined.EnsureList(key);
      for (auto& pin : pins.GetList()) {
        if (!records || !records->contains(*pin.GetDict().FindString("id")))
          destination->Append(std::move(pin));
      }
    }
    ledger = bookmark_sync::RecordEdits(*incoming, combined, ledger,
                                       sync_prefs_->GetString("device"));
    incoming = bookmark_sync::Materialize(ledger);
  }
  if (!incoming || !ApplyBookmarks(ledger, *incoming, replace)) {
    FinishBookmarkSync({}, std::nullopt);
    return;
  }
  if (replace) {
    for (const auto& space : model_.workspaces())
      incoming->EnsureList("tabs2/" + space.id);
  }
  ApplyBookmarkTabs(*incoming);
  RefreshShell();
  sync_busy_ = true;
  SaveSession(base::BindOnce(
      [](base::WeakPtr<WorkspaceLab> lab, base::DictValue ledger,
         base::DictValue snapshot, base::DictValue scopes,
         std::vector<std::pair<std::string, std::string>> extensions, bool saved) {
        if (!lab) return;
        lab->sync_busy_ = false;
        if (!saved) {
          lab->FinishBookmarkSync({}, std::nullopt);
          return;
        }
        auto& prefs = *lab->sync_prefs_;
        prefs.SetDict("ledger", std::move(ledger));
        prefs.SetDict("snapshot", std::move(snapshot));
        prefs.SetDict("scope_snapshot", std::move(scopes));
        prefs.ClearPref("import_pending");
        prefs.CommitPendingWrite();
        lab->bookmark_extension_import_ = std::move(extensions);
        std::ranges::reverse(lab->bookmark_extension_import_);
        lab->InstallNextBookmarkExtension();
        lab->RunBookmarkSync();
      }, sync_weak_.GetWeakPtr(), std::move(ledger), CaptureBookmarks(),
      CaptureBookmarkScopes(), std::move(extension_candidates)));
}

void WorkspaceLab::InstallNextBookmarkExtension() {
  if (!CanEditWorkspace() || extension_install_ || web_store_update_ ||
      shell_->HasDangerDialog()) {
    bookmark_extension_import_.clear();
    return;
  }
  while (!bookmark_extension_import_.empty()) {
    auto [profile, id] = std::move(bookmark_extension_import_.back());
    bookmark_extension_import_.pop_back();
    if (removed_profiles_.contains(profile)) continue;
    const auto owner = std::ranges::find(profiles_, profile, &ContentProfileInfo::id);
    if (owner == profiles_.end()) continue;
    const size_t index = owner - profiles_.begin();
    auto& context = *contexts_[index];
    auto installed = GetConfiguredExtensions(context);
    if (std::ranges::contains(installed, id, &extensions::Extension::id)) continue;
    auto next = mojo::WrapCallbackWithDefaultInvokeIfNotRun(
        base::BindPostTaskToCurrentDefault(base::BindOnce(
            [](base::WeakPtr<WorkspaceLab> lab, bool) {
              if (lab) lab->InstallNextBookmarkExtension();
            }, weak_factory_.GetWeakPtr())), false);
    extension_install_ = WorkspaceExtensionInstall::FromWebStore(
        context.GetDefaultStoragePartition()->GetURLLoaderFactoryForBrowserProcess().get(),
        id, "0.0.0.0", context.GetPath(), std::move(installed),
        base::BindOnce(&WorkspaceLab::OnExtensionReview, weak_factory_.GetWeakPtr(),
                       index, content::WeakDocumentPtr(), std::string(), std::move(next), true));
    return;
  }
}

void WorkspaceLab::ExchangeDropboxCode(std::string verifier, std::string code) {
  if (sync_busy_ || !sync_prefs_->GetString("method").empty()) return;
  dropbox_login_timeout_.Stop();
  if (code.empty()) {
    SetSyncStatus(prototype::T(u"Anulowano łączenie z Dropbox."));
    return;
  }
  sync_busy_ = true;
  SetSyncStatus(prototype::T(u"Łączenie…"));
  DropboxToken("grant_type=authorization_code&code_verifier=" + verifier +
               "&code=" + base::EscapeQueryParamValue(code, false) +
               "&redirect_uri=" + base::EscapeQueryParamValue(kDropboxRedirect, false), true);
}

void WorkspaceLab::DropboxToken(std::string form, bool connecting) {
  form += "&client_id=" + base::EscapeQueryParamValue(DropboxAppKey(), false);
  DropboxRequest("https://api.dropboxapi.com/oauth2/token", {}, std::move(form),
      "application/x-www-form-urlencoded", base::BindOnce(
      [](base::WeakPtr<WorkspaceLab> lab, bool connecting, int status, std::string, std::string body) {
        if (!lab) return;
        auto data = base::JSONReader::ReadDict(body, base::JSON_PARSE_RFC);
        const auto* access = data ? data->FindString("access_token") : nullptr;
        const auto* refresh = data ? data->FindString("refresh_token") : nullptr;
        if (status != 200 || !access || (connecting && !refresh)) {
          lab->sync_busy_ = false;
          lab->sync_last_success_ = {};
          lab->SetSyncStatus(prototype::T(u"Nie udało się połączyć z Dropbox. Sprawdź połączenie lub połącz konto ponownie."));
          return;
        }
        lab->dropbox_access_ = *access;
        lab->dropbox_expires_ = base::Time::Now() +
            base::Seconds(data->FindInt("expires_in").value_or(3600) - 60);
        if (!connecting) {
          lab->sync_busy_ = false;
          lab->RunBookmarkSync();
          return;
        }
        lab->os_crypt_->GetInstance(base::BindOnce([](base::WeakPtr<WorkspaceLab> lab,
            std::string refresh, scoped_refptr<os_crypt_async::Encryptor> crypt) {
          if (!lab) return;
          std::string encrypted;
          if (!crypt || !crypt->IsEncryptionAvailable() || !crypt->EncryptString(base::StrCat({kDropboxCredentialPrefix, refresh}), &encrypted)) {
            lab->FinishBookmarkSync({}, std::nullopt); return;
          }
          lab->sync_prefs_->SetString("credential", base::Base64Encode(encrypted));
          lab->StartBookmarkSync("dropbox");
        }, lab, *refresh));
      }, sync_weak_.GetWeakPtr(), connecting));
}

void WorkspaceLab::DropboxRequest(std::string endpoint, std::string argument, std::string body,
                                  std::string mime, DropboxReply reply) {
  const std::string_view access = argument.empty() ? std::string_view() : dropbox_access_;
  sync_loader_ = MakeDropboxLoader(std::move(endpoint), access, std::move(argument),
                                   std::move(body), mime, base::Seconds(30));
  sync_loader_->DownloadToString(WorkspaceProfile(active_index_)->GetDefaultStoragePartition()
      ->GetURLLoaderFactoryForBrowserProcess().get(), base::BindOnce(
      [](base::WeakPtr<WorkspaceLab> lab, DropboxReply reply, std::optional<std::string> data) {
        if (!lab) return;
        auto loader = std::move(lab->sync_loader_);
        auto* info = loader->ResponseInfo();
        auto headers = info ? info->headers : nullptr;
        int status = headers && data ? headers->response_code() : 0;
        if (status == 401) lab->dropbox_access_.clear();
        if (status == 429)
          lab->sync_retry_after_ = std::max(
              lab->sync_retry_after_, base::Time::Now() + DropboxRetryAfter(*headers));
        std::move(reply).Run(status, headers ? headers->GetNormalizedHeader("Dropbox-API-Result")
            .value_or("") : "", std::move(data).value_or(""));
      }, sync_weak_.GetWeakPtr(), std::move(reply)), network::SimpleURLLoader::kMaxBoundedStringDownloadSize);
}

void WorkspaceLab::PumpDropboxWatch() {
  if (sync_prefs_->GetString("method") != "dropbox" ||
      dropbox_watch_missing_scope_ || dropbox_watch_loader_ ||
      dropbox_watch_timer_.IsRunning() || sync_prefs_->GetBoolean("import_pending")) return;
  const auto step = dropbox_watch_step_;
  const bool long_poll = step == DropboxWatchStep::kWait;
  if (!long_poll && (dropbox_access_.empty() || dropbox_expires_ <= base::Time::Now())) {
    dropbox_watch_healthy_ = false;
    return;
  }
  auto delay = sync_retry_after_ - base::Time::Now();
  if (long_poll)
    delay = std::max(delay, dropbox_wait_after_ - base::TimeTicks::Now());
  if (delay.is_positive()) {
    dropbox_watch_timer_.Start(FROM_HERE, delay, this, &WorkspaceLab::PumpDropboxWatch);
    return;
  }

  const char* endpoint = kDropboxCursor;
  base::DictValue args;
  if (step == DropboxWatchStep::kCursor) {
    args.Set("path", "");
    args.Set("include_deleted", true);
    args.Set("limit", 200);
  } else {
    args.Set("cursor", dropbox_watch_cursor_);
    endpoint = long_poll ? kDropboxLongPoll : kDropboxChanges;
    if (long_poll) args.Set("timeout", kDropboxWaitSeconds);
  }
  auto body = base::WriteJson(args);
  if (!body) return;
  // Dropbox can add up to 90 seconds of jitter to the requested wait time.
  dropbox_watch_loader_ = MakeDropboxLoader(
      endpoint, long_poll ? std::string_view() : dropbox_access_, {},
      std::move(*body), "application/json",
      base::Seconds(long_poll ? kDropboxWaitSeconds + 100 : 30));
  dropbox_watch_loader_->DownloadToString(
      WorkspaceProfile(active_index_)->GetDefaultStoragePartition()
          ->GetURLLoaderFactoryForBrowserProcess().get(),
      base::BindOnce(&WorkspaceLab::OnDropboxWatchResponse, sync_weak_.GetWeakPtr(), step),
      network::SimpleURLLoader::kMaxBoundedStringDownloadSize);
}

void WorkspaceLab::OnDropboxWatchResponse(DropboxWatchStep step,
                                         std::optional<std::string> body) {
  auto loader = std::move(dropbox_watch_loader_);
  const auto* info = loader->ResponseInfo();
  const auto* headers = info ? info->headers.get() : nullptr;
  const int status = headers ? headers->response_code() : 0;
  const auto response = body ? base::JSONReader::ReadDict(*body, base::JSON_PARSE_RFC)
                             : std::nullopt;
  const auto* error = response ? response->FindDict("error") : nullptr;
  const auto* tag = error ? error->FindString(".tag") : nullptr;

  if (status != 200 || !response) {
    dropbox_watch_healthy_ = false;
    if ((status == 401 || status == 403) && tag && *tag == "missing_scope") {
      dropbox_watch_missing_scope_ = true;
      return;
    }
    if (status == 409 && tag && *tag == "reset") {
      dropbox_watch_cursor_.clear();
      dropbox_watch_step_ = DropboxWatchStep::kCursor;
    }
    const auto delay = status == 429 ? DropboxRetryAfter(*headers) : base::Seconds(30);
    if (status == 429)
      sync_retry_after_ = std::max(sync_retry_after_, base::Time::Now() + delay);
    dropbox_watch_timer_.Start(FROM_HERE, delay, this, &WorkspaceLab::PumpDropboxWatch);
    return;
  }

  if (step == DropboxWatchStep::kWait) {
    if (const auto changes = response->FindBool("changes")) {
      dropbox_watch_healthy_ = true;
      dropbox_wait_after_ = base::TimeTicks::Now() +
          base::Seconds(std::max(1, response->FindInt("backoff").value_or(0)));
      dropbox_watch_step_ = *changes ? DropboxWatchStep::kChanges : DropboxWatchStep::kWait;
      PumpDropboxWatch();
      return;
    }
  } else if (const auto* cursor = response->FindString("cursor"); cursor && !cursor->empty()) {
    const auto* entries = response->FindList("entries");
    const auto more = response->FindBool("has_more");
    if (step == DropboxWatchStep::kCursor || (entries && more)) {
      dropbox_watch_healthy_ = true;
      dropbox_watch_cursor_ = *cursor;
      dropbox_watch_step_ = more.value_or(false) ? DropboxWatchStep::kChanges
                                                : DropboxWatchStep::kWait;
      if (step == DropboxWatchStep::kCursor) {
        // Catch updates between the last index read and creation of the cursor.
        sync_followup_pending_ = true;
      } else {
        for (const auto& value : *entries) {
          const auto* entry = value.GetIfDict();
          const auto* path = entry ? entry->FindString("path_lower") : nullptr;
          if (!path || *path != kDropboxSyncIndex) continue;
          const auto* revision = entry->FindString("rev");
          dropbox_notified_revision_ = revision ? *revision : std::string();
        }
      }
      if (!more.value_or(false) &&
          (sync_followup_pending_ ||
           (dropbox_notified_revision_ && *dropbox_notified_revision_ != dropbox_synced_revision_)) &&
          !sync_batch_timer_.IsRunning()) {
        sync_batch_timer_.Start(FROM_HERE, base::TimeDelta(), this,
                                &WorkspaceLab::RunDebouncedBookmarkSync);
      }
      PumpDropboxWatch();
      return;
    }
  }
  dropbox_watch_healthy_ = false;
  dropbox_watch_timer_.Start(FROM_HERE, base::Seconds(30), this,
                            &WorkspaceLab::PumpDropboxWatch);
}

void WorkspaceLab::DownloadDropboxBookmarks(base::DictValue snapshot, base::DictValue ledger) {
  dropbox_exchange_ = std::make_unique<DropboxSyncExchange>();
  dropbox_exchange_->snapshot = std::move(snapshot);
  dropbox_exchange_->local = std::move(ledger);
  dropbox_exchange_->read_only = sync_prefs_->GetBoolean("import_pending");
  if (dropbox_shard_cache_.empty() && !sync_prefs_->GetDict("ledger").empty()) {
    base::ThreadPool::PostTaskAndReplyWithResult(FROM_HERE, {base::TaskPriority::USER_VISIBLE},
        base::BindOnce(&bookmark_sync::CacheShards, sync_prefs_->GetDict("ledger").Clone()),
        base::BindOnce([](base::WeakPtr<WorkspaceLab> lab, base::DictValue cache) {
          if (!lab) return;
          lab->dropbox_shard_cache_ = std::move(cache);
          lab->FetchDropboxIndex();
        }, sync_weak_.GetWeakPtr()));
    return;
  }
  FetchDropboxIndex();
}

void WorkspaceLab::FetchDropboxIndex() {
  auto& exchange = *dropbox_exchange_;
  exchange.remote.clear();
  exchange.index.clear();
  exchange.pending.clear();
  exchange.outgoing.reset();
  exchange.revision.clear();
  DropboxRequest(kDropboxDownload, DropboxFileArgument(kDropboxSyncIndex), {}, {},
      base::BindOnce([](base::WeakPtr<WorkspaceLab> lab, int status, std::string metadata,
                        std::string body) {
        if (!lab) return;
        if (DropboxError(status, body, "path/not_found")) {
          lab->DropboxRequest(kDropboxDownload, DropboxFileArgument("/bookmarks-v1.json"), {}, {},
              base::BindOnce([](base::WeakPtr<WorkspaceLab> lab, int status, std::string,
                                 std::string body) {
                if (!lab) return;
                if (status == 200) {
                  auto legacy = base::JSONReader::ReadDict(body, base::JSON_PARSE_RFC);
                  if (!legacy || !bookmark_sync::Merge(lab->dropbox_exchange_->remote, *legacy)) {
                    lab->CompleteDropboxExchange(false);
                    return;
                  }
                } else if (!DropboxError(status, body, "path/not_found")) {
                  lab->CompleteDropboxExchange(false);
                  return;
                }
                lab->MergeDropboxShards();
              }, lab));
          return;
        }
        const auto info = base::JSONReader::ReadDict(metadata, base::JSON_PARSE_RFC);
        const auto* revision = info ? info->FindString("rev") : nullptr;
        auto index = status == 200 ? bookmark_sync::ReadIndex(body) : std::nullopt;
        if (!index || !revision || revision->empty()) {
          lab->CompleteDropboxExchange(false);
          return;
        }
        auto& flow = *lab->dropbox_exchange_;
        flow.revision = *revision;
        flow.index = std::move(*index);
        for (const auto [key, value] : flow.index) {
          const auto& digest = value.GetString();
          auto* cached = flow.cache.FindDict(digest);
          if (!cached) {
            auto entry = lab->dropbox_shard_cache_.Extract(digest);
            if (entry) cached = &flow.cache.Set(digest, std::move(*entry))->GetDict();
          }
          if (cached) {
            // Confirm the digest belongs to the index entry as well as to a
            // previously verified download. An index may itself be corrupt.
            if (!bookmark_sync::MatchesShardKey(key, *cached) ||
                !bookmark_sync::Merge(flow.remote, *cached)) {
              lab->CompleteDropboxExchange(false);
              return;
            }
          } else {
            flow.pending.emplace_back(key, digest);
          }
        }
        lab->FetchNextDropboxShard();
      }, sync_weak_.GetWeakPtr()));
}

void WorkspaceLab::FetchNextDropboxShard() {
  auto& flow = *dropbox_exchange_;
  if (flow.pending.empty()) {
    MergeDropboxShards();
    return;
  }
  auto [key, digest] = std::move(flow.pending.back());
  flow.pending.pop_back();
  auto argument = DropboxFileArgument(DropboxShardPath(digest));
  DropboxRequest(kDropboxDownload, std::move(argument), {}, {},
      base::BindOnce([](base::WeakPtr<WorkspaceLab> lab, std::string key, std::string digest,
                        int status, std::string, std::string body) {
        if (!lab) return;
        auto shard = status == 200 ? bookmark_sync::ReadShard(key, digest, body) : std::nullopt;
        auto& exchange = *lab->dropbox_exchange_;
        if (!shard || !bookmark_sync::Merge(exchange.remote, *shard)) {
          lab->CompleteDropboxExchange(false);
          return;
        }
        exchange.cache.Set(digest, std::move(*shard));
        lab->FetchNextDropboxShard();
      }, sync_weak_.GetWeakPtr(), std::move(key), std::move(digest)));
}

void WorkspaceLab::MergeDropboxShards() {
  auto& flow = *dropbox_exchange_;
  base::ThreadPool::PostTaskAndReplyWithResult(FROM_HERE, {base::TaskPriority::USER_VISIBLE},
      base::BindOnce([](base::DictValue local, base::DictValue remote,
                        bool read_only) -> std::optional<DropboxSyncExchange::Plan> {
        if (!bookmark_sync::Merge(local, remote)) return std::nullopt;
        bookmark_sync::UpgradeLegacyTabs(local);
        if (!bookmark_sync::Materialize(local)) return std::nullopt;
        auto shards = read_only ? std::nullopt : bookmark_sync::WriteShards(local);
        if (!read_only && !shards) return std::nullopt;
        return DropboxSyncExchange::Plan{std::move(local), std::move(shards)};
      }, flow.local.Clone(), std::move(flow.remote), flow.read_only),
      base::BindOnce([](base::WeakPtr<WorkspaceLab> lab,
                        std::optional<DropboxSyncExchange::Plan> plan) {
        if (!lab) return;
        if (!plan) {
          lab->CompleteDropboxExchange(false);
          return;
        }
        auto& exchange = *lab->dropbox_exchange_;
        exchange.merged = std::move(plan->merged);
        exchange.outgoing = std::move(plan->shards);
        if (!exchange.outgoing ||
            (!exchange.revision.empty() && exchange.outgoing->index == exchange.index)) {
          lab->CompleteDropboxExchange(true);
          return;
        }
        for (const auto [key, value] : exchange.outgoing->index) {
          if (!exchange.cache.contains(value.GetString()))
            exchange.pending.emplace_back(key, value.GetString());
        }
        lab->UploadNextDropboxShard();
      }, sync_weak_.GetWeakPtr()));
}

void WorkspaceLab::UploadNextDropboxShard() {
  auto& flow = *dropbox_exchange_;
  if (flow.pending.empty()) {
    auto index = base::WriteJson(base::DictValue().Set("format", 2)
                                .Set("shards", flow.outgoing->index.Clone()));
    if (!index) {
      CompleteDropboxExchange(false);
      return;
    }
    DropboxRequest(kDropboxUpload, DropboxWriteArgument(kDropboxSyncIndex, flow.revision),
        std::move(*index), "application/octet-stream",
        base::BindOnce([](base::WeakPtr<WorkspaceLab> lab, int status, std::string, std::string body) {
          if (!lab) return;
          if (DropboxError(status, body, "path/conflict") && lab->dropbox_exchange_->retries++ < 2) {
            lab->FetchDropboxIndex();
            return;
          }
          if (status == 200) {
            const auto info = base::JSONReader::ReadDict(body, base::JSON_PARSE_RFC);
            const auto* revision = info ? info->FindString("rev") : nullptr;
            lab->dropbox_exchange_->revision = revision ? *revision : std::string();
          }
          lab->CompleteDropboxExchange(status == 200);
        }, sync_weak_.GetWeakPtr()));
    return;
  }
  auto [key, digest] = std::move(flow.pending.back());
  flow.pending.pop_back();
  const auto argument = DropboxWriteArgument(DropboxShardPath(digest), "");
  const auto body = flow.outgoing->blobs.at(key);
  DropboxRequest(kDropboxUpload, argument, body, "application/octet-stream",
      base::BindOnce([](base::WeakPtr<WorkspaceLab> lab, std::string key, std::string digest,
                        int status, std::string, std::string response) {
        if (!lab) return;
        if (status == 200) {
          auto shard = bookmark_sync::ReadShard(
              key, digest, lab->dropbox_exchange_->outgoing->blobs.at(key));
          if (!shard) {
            lab->CompleteDropboxExchange(false);
            return;
          }
          lab->dropbox_exchange_->cache.Set(digest, std::move(*shard));
          lab->UploadNextDropboxShard();
          return;
        }
        if (!DropboxError(status, response, "path/conflict")) {
          lab->CompleteDropboxExchange(false);
          return;
        }
        // A concurrent writer may have uploaded the same immutable shard.
        // Verify it before publishing an index that references it.
        auto argument = DropboxFileArgument(DropboxShardPath(digest));
        lab->DropboxRequest(kDropboxDownload, std::move(argument), {}, {},
            base::BindOnce([](base::WeakPtr<WorkspaceLab> lab, std::string key, std::string digest,
                              int status, std::string, std::string body) {
              if (!lab) return;
              auto shard = status == 200 ? bookmark_sync::ReadShard(key, digest, body) : std::nullopt;
              if (!shard) {
                lab->CompleteDropboxExchange(false);
                return;
              }
              lab->dropbox_exchange_->cache.Set(digest, std::move(*shard));
              lab->UploadNextDropboxShard();
            }, lab, std::move(key), std::move(digest)));
      }, sync_weak_.GetWeakPtr(), std::move(key), std::move(digest)));
}

void WorkspaceLab::CompleteDropboxExchange(bool success) {
  auto flow = std::move(dropbox_exchange_);
  if (success) {
    dropbox_synced_revision_ = flow->revision;
    const auto& index = flow->outgoing ? flow->outgoing->index : flow->index;
    dropbox_shard_cache_.clear();
    for (const auto [key, value] : index) {
      auto cached = flow->cache.Extract(value.GetString());
      if (cached) dropbox_shard_cache_.Set(value.GetString(), std::move(*cached));
    }
  }
  FinishBookmarkSync(std::move(flow->snapshot),
      success ? std::optional(std::move(flow->merged)) : std::nullopt);
}
