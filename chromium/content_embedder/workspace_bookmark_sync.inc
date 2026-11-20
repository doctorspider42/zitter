// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// Included in workspace_lab.cc, alongside the workspace persistence callbacks.
namespace {
constexpr std::string_view kDropboxCredentialPrefix = "zitter-bookmark-sync/dropbox/";

constexpr char kDropboxRedirect[] = "http://127.0.0.1:53682/dropbox/callback";

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

void WorkspaceLab::ConfigureBookmarkSync(std::string method) {
  if (!sync_prefs_ || !CanEditWorkspace()) return;
  dropbox_login_.InvalidateWeakPtrs();
  dropbox_login_timeout_.Stop();
  if (method == "off") {
    sync_weak_.InvalidateWeakPtrs();
    sync_timer_.Stop();
    sync_loader_.reset();
    sync_busy_ = false;
    dropbox_access_.clear();
    for (const char* key : {"method", "folder", "credential", "device", "snapshot", "ledger"})
      sync_prefs_->ClearPref(key);
    sync_prefs_->CommitPendingWrite();
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
      "&token_access_type=offline&scope=files.content.read%20files.content.write&code_challenge_method=S256&client_id=" +
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
  sync_prefs_->CommitPendingWrite();
  sync_busy_ = false;
  sync_retry_after_ = {};
  sync_timer_.Start(FROM_HERE, base::Seconds(30), this, &WorkspaceLab::RunBookmarkSync);
  SetSyncStatus(prototype::T(u"Łączenie…"));
  RunBookmarkSync();
}

base::DictValue WorkspaceLab::CaptureBookmarks() const {
  base::DictValue result;
  for (size_t i = 0; i < pins_.size(); ++i) {
    const auto& workspace = model_.workspaces()[i];
    result.Set("space/" + workspace.id, WriteWorkspacePins(SyncablePins(pins_[i])));
  }
  for (const auto& profile : profiles_) {
    auto found = profile_favorites_.find(profile.id);
    result.Set("profile/" + profile.id, found == profile_favorites_.end()
        ? base::ListValue() : WriteWorkspacePins(SyncablePins(found->second)));
  }
  return result;
}

bool WorkspaceLab::PrepareBookmarkScopes(const base::DictValue& ledger,
                                         const base::DictValue& collections) {
  // Create only containers needed by incoming bookmarks. Keep local profile
  // isolation, including on a second computer with no existing custom profiles.
  for (const auto [scope, items] : collections) {
    if (items.GetList().empty()) continue;
    const auto* metadata = ledger.FindDict(scope)->FindDict("");
    const auto* info = metadata ? metadata->FindDict("scope") : nullptr;
    if (!info) continue;
    const auto* profile = info->FindString("profile");
    const auto* name = info->FindString("name");
    if (!profile || !name || !IsContentWorkspaceName(*name)) continue;
    if (!std::ranges::contains(profiles_, *profile, &ContentProfileInfo::id)) {
      if (!profile->starts_with("profile-") ||
          !base::Uuid::ParseLowercase(std::string_view(*profile).substr(8)).is_valid() ||
          profiles_.size() >= kMaxContentProfiles) return false;
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
          base::BindOnce([](base::WeakPtr<WorkspaceLab> lab, ContentProfileInfo incoming, bool ok) {
            if (!lab) return;
            lab->saving_catalogue_ = false;
            if (ok) {
              auto context = lab->OpenNewProfile(incoming);
              lab->PublishProfile(std::move(incoming), std::move(context));
              lab->RefreshShell();
              lab->RunBookmarkSync();
            } else lab->FinishBookmarkSync({}, std::nullopt);
            if (std::exchange(lab->close_after_catalogue_save_, false)) lab->RequestClose();
          }, weak_factory_.GetWeakPtr(), incoming));
      return false;
    }
  }
  return true;
}

bool WorkspaceLab::ApplyBookmarks(const base::DictValue& ledger, const base::DictValue& collections) {
  auto spaces = model_.workspaces();
  auto candidate = CaptureSession();
  for (const auto [scope, list] : collections) {
    if (!scope.starts_with("space/space-") || list.GetList().empty()) continue;
    const std::string id(scope.substr(6));
    if (WorkspaceIndex(id) || removed_workspaces_.contains(id)) continue;
    auto* meta = ledger.FindDict(scope)->FindDict("");
    auto* info = meta ? meta->FindDict("scope") : nullptr;
    if (!info || !info->FindString("profile") || !info->FindString("name")) return false;
    Workspace space{id, *info->FindString("profile"), *info->FindString("name")};
    AppendWorkspaceMetadata(candidate, space, {});
    candidate.Set(id, base::DictValue().Set("profile_id", space.profile_id).Set("active", 0)
        .Set("tabs", base::ListValue().Append(prototype::WriteWorkspaceSessionTab(GURL(), u""))));
    spaces.push_back(std::move(space));
  }
  auto pins = pins_;
  pins.resize(spaces.size());
  auto favorites = profile_favorites_;
  auto update = [&](const std::string& key, auto& dest, bool profile) {
    const auto* list = collections.FindList(key);
    if (!list) return true;
    auto decoded = ReadWorkspacePins(base::DictValue().Set("pins", list->Clone()));
    if (!decoded) return false;
    for (const auto& pin : *decoded)
      if (pin.profile_pin != profile || pin.live_folder) return false;
    auto syncable = SyncablePins(dest);
    if (*decoded == syncable) return true;
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
    if (!update("space/" + spaces[i].id, pins[i], false)) return false;
  for (const auto& profile : profiles_)
    if (!update("profile/" + profile.id, favorites[profile.id], true)) return false;
  candidate.Remove("profile_favorites");
  for (const auto& [id, items] : favorites)
    if (!items.empty()) candidate.EnsureDict("profile_favorites")->Set(id, WriteWorkspacePins(items));
  for (size_t i = 0; i < spaces.size(); ++i) {
    auto* entry = candidate.FindDict(spaces[i].id);
    entry->Set("pins", WriteWorkspacePins(pins[i]));
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
  for (size_t i = 0; i < tabs_.size(); ++i) {
    const auto visible = WorkspacePins(i);
    for (auto& tab : tabs_[i])
      if (!tab.pin.empty() && !std::ranges::contains(visible, tab.pin, &prototype::PinnedItem::id))
        tab.pin.clear();
  }
  return true;
}

void WorkspaceLab::RunBookmarkSync() {
  if (!sync_prefs_ || sync_busy_ || !CanEditWorkspace() || base::Time::Now() < sync_retry_after_) return;
  const auto method = sync_prefs_->GetString("method");
  if (method.empty()) return;
  const auto device = base::Uuid::ParseLowercase(sync_prefs_->GetString("device"));
  if (!device.is_valid() || (method != "folder" && method != "dropbox")) {
    SetSyncStatus(prototype::T(u"Wyłącz synchronizację i skonfiguruj ją ponownie."));
    return;
  }
  for (const auto [key, list] : sync_prefs_->GetDict("snapshot")) {
    if (!list.is_list() || !ReadWorkspacePins(base::DictValue().Set("pins", list.Clone()))) {
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
  auto ledger = bookmark_sync::RecordEdits(sync_prefs_->GetDict("snapshot"), snapshot,
                                          validated, sync_prefs_->GetString("device"));
  // Keep the original container names for provisioning on a new device.
  // Existing spaces and profiles continue to own their names locally.
  const auto add_scope = [&](const std::string& scope, const std::string& name, std::string profile) {
    auto* records = ledger.FindDict(scope);
    if (records && !records->contains("") && !records->empty())
      records->Set("", base::DictValue().Set("clock", 1).Set("device", sync_prefs_->GetString("device"))
          .Set("scope", base::DictValue().Set("name", name).Set("profile", std::move(profile))));
  };
  for (const auto& space : model_.workspaces()) {
    add_scope("space/" + space.id, space.name, space.profile_id);
    if (auto* records = ledger.FindDict("space/" + space.id); records && !records->empty())
      ledger.EnsureDict("profile/" + space.profile_id);
  }
  for (const auto& profile : profiles_) {
    auto* records = ledger.FindDict("profile/" + profile.id);
    if (records && !records->contains("") && profile.id.starts_with("profile-"))
      records->Set("", base::DictValue().Set("clock", 1).Set("device", sync_prefs_->GetString("device"))
          .Set("scope", base::DictValue().Set("name", profile.name).Set("profile", profile.id)));
  }
  if (method == "folder") {
    session_writer_->PostTaskAndReplyWithResult(FROM_HERE, base::BindOnce(
        &bookmark_sync::ExchangeFolder, sync_prefs_->GetFilePath("folder"),
        sync_prefs_->GetString("device"), std::move(ledger)), base::BindOnce(
        &WorkspaceLab::FinishBookmarkSync, sync_weak_.GetWeakPtr(), std::move(snapshot)));
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
  DownloadDropboxBookmarks(std::move(snapshot), std::move(ledger));
}

void WorkspaceLab::FinishBookmarkSync(base::DictValue snapshot,
                                       std::optional<base::DictValue> ledger) {
  sync_busy_ = false;
  if (!CanEditWorkspace()) return;
  if (ledger) {
    // Catch edits made while reading the folder or waiting for the network.
    auto current = CaptureBookmarks();
    auto reconciled = bookmark_sync::RecordEdits(snapshot, current, *ledger,
                                                sync_prefs_->GetString("device"));
    auto merged = bookmark_sync::Materialize(reconciled);
    if (merged && !PrepareBookmarkScopes(reconciled, *merged)) {
      if (!saving_catalogue_) SetSyncStatus(prototype::T(u"Synchronizacja wstrzymana. Zitter spróbuje ponownie."));
      return;
    }
    const bool changed = merged && std::ranges::any_of(*merged, [&](const auto& entry) {
      const auto [scope, items] = entry;
      const auto* local = current.FindList(scope);
      return local ? items != *local : !items.GetList().empty();
    });
    if (merged && (!changed || ApplyBookmarks(reconciled, *merged))) {
      auto after = changed ? CaptureBookmarks() : current.Clone();
      auto commit = base::BindOnce([](base::WeakPtr<WorkspaceLab> lab, base::DictValue after,
                                      base::DictValue reconciled, bool ok) {
        if (!lab) return;
        lab->sync_busy_ = false;
        if (!ok) { lab->FinishBookmarkSync({}, std::nullopt); return; }
        auto& prefs = *lab->sync_prefs_;
        if (prefs.GetDict("snapshot") != after || prefs.GetDict("ledger") != reconciled) {
          prefs.SetDict("snapshot", std::move(after));
          prefs.SetDict("ledger", std::move(reconciled));
          prefs.CommitPendingWrite();
        }
        const auto destination = prefs.GetString("method") == "folder"
            ? prefs.GetFilePath("folder").LossyDisplayName() : u"Dropbox";
        lab->SetSyncStatus(destination + u" · " + prototype::T(u"Zsynchronizowano") +
                           u" " + base::TimeFormatTimeOfDay(base::Time::Now()));
      }, sync_weak_.GetWeakPtr(), after.Clone(), std::move(reconciled));
      // Commit the baseline only after its matching workspace state is durable.
      if (after != current || sync_prefs_->GetDict("snapshot") != after) {
        sync_busy_ = true;
        if (after != current) RefreshShell();
        SaveSession(std::move(commit));
      } else std::move(commit).Run(true);
      return;
    }
  }
  SetSyncStatus(prototype::T(u"Synchronizacja wstrzymana. Zitter spróbuje ponownie."));
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
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GURL(endpoint);
  request->method = "POST";
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->redirect_mode = network::mojom::RedirectMode::kError;
  request->load_flags = net::LOAD_DISABLE_CACHE;
  if (!argument.empty()) {
    request->headers.SetHeader("Authorization", "Bearer " + dropbox_access_);
    request->headers.SetHeader("Dropbox-API-Arg", argument);
  }
  constexpr auto annotation = net::DefineNetworkTrafficAnnotation("zitter_dropbox_bookmarks", R"(
    semantics { sender: "Bookmark sync" description: "Synchronizes bookmarks with Dropbox."
      trigger: "The user connects Dropbox." data: "Authorization tokens and bookmark data."
      destination: OTHER destination_other: "Dropbox" }
    policy { cookies_allowed: NO setting: "Disable bookmark sync in Settings."
      policy_exception_justification: "User-initiated desktop feature." })");
  sync_loader_ = network::SimpleURLLoader::Create(std::move(request), annotation);
  sync_loader_->SetAllowHttpErrorResults(true);
  sync_loader_->SetTimeoutDuration(base::Seconds(30));
  if (!mime.empty()) sync_loader_->AttachStringForUpload(std::move(body), mime);
  sync_loader_->DownloadToString(WorkspaceProfile(active_index_)->GetDefaultStoragePartition()
      ->GetURLLoaderFactoryForBrowserProcess().get(), base::BindOnce(
      [](base::WeakPtr<WorkspaceLab> lab, DropboxReply reply, std::optional<std::string> data) {
        if (!lab) return;
        auto loader = std::move(lab->sync_loader_);
        auto* info = loader->ResponseInfo();
        auto headers = info ? info->headers : nullptr;
        int status = headers && data ? headers->response_code() : 0;
        if (status == 401) lab->dropbox_access_.clear();
        if (status == 429) {
          int seconds = 60;
          if (auto delay = headers->GetNormalizedHeader("Retry-After"))
            base::StringToInt(*delay, &seconds);
          lab->sync_retry_after_ = base::Time::Now() + base::Seconds(std::max(30, seconds));
        }
        std::move(reply).Run(status, headers ? headers->GetNormalizedHeader("Dropbox-API-Result")
            .value_or("") : "", std::move(data).value_or(""));
      }, sync_weak_.GetWeakPtr(), std::move(reply)), network::SimpleURLLoader::kMaxBoundedStringDownloadSize);
}

void WorkspaceLab::DownloadDropboxBookmarks(base::DictValue snapshot, base::DictValue ledger) {
  DropboxRequest("https://content.dropboxapi.com/2/files/download", R"({"path":"/bookmarks-v1.json"})",
      {}, {}, base::BindOnce([](base::WeakPtr<WorkspaceLab> lab, base::DictValue snapshot,
          base::DictValue ledger, int status, std::string metadata, std::string body) {
    if (!lab) return;
    auto data = base::JSONReader::ReadDict(body, base::JSON_PARSE_RFC);
    base::DictValue upload;
    upload.Set("path", "/bookmarks-v1.json");
    upload.Set("autorename", false);
    upload.Set("strict_conflict", true);
    if (status == 200) {
      auto info = base::JSONReader::ReadDict(metadata, base::JSON_PARSE_RFC);
      auto* rev = info ? info->FindString("rev") : nullptr;
      if (!rev || !data || !bookmark_sync::Merge(ledger, *data)) {
        lab->FinishBookmarkSync({}, std::nullopt); return;
      }
      if (ledger == *data) { lab->FinishBookmarkSync(std::move(snapshot), std::move(ledger)); return; }
      upload.Set("mode", base::DictValue().Set(".tag", "update").Set("update", *rev));
    } else {
      auto* error = data ? data->FindString("error_summary") : nullptr;
      if (status != 409 || !error || !error->starts_with("path/not_found")) {
        lab->FinishBookmarkSync({}, std::nullopt); return;
      }
      upload.Set("mode", "add");
    }
    auto json = base::WriteJson(ledger);
    if (!json || json->size() > network::SimpleURLLoader::kMaxBoundedStringDownloadSize || !bookmark_sync::Materialize(ledger)) {
      lab->FinishBookmarkSync({}, std::nullopt); return;
    }
    lab->DropboxRequest("https://content.dropboxapi.com/2/files/upload", *base::WriteJson(upload),
        std::move(*json), "application/octet-stream", base::BindOnce(
        [](base::WeakPtr<WorkspaceLab> lab, base::DictValue snapshot, base::DictValue ledger,
           int status, std::string, std::string) {
          if (!lab) return;
          if (status == 200) lab->FinishBookmarkSync(std::move(snapshot), std::move(ledger));
          else lab->FinishBookmarkSync({}, std::nullopt);
        }, lab, std::move(snapshot), std::move(ledger)));
  }, sync_weak_.GetWeakPtr(), std::move(snapshot), std::move(ledger)));
}
