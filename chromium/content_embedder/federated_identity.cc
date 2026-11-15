// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "federated_identity.h"

#include <array>

#include <algorithm>
#include <iterator>
#include <utility>

#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chromium/prototype/i18n.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/common/referrer.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/webid/identity_request_account.h"
#include "content/public/browser/webid/identity_request_dialog_controller.h"
#include "content/public/common/content_features.h"
#include "net/base/schemeful_site.h"
#include "third_party/blink/public/mojom/webid/federated_request.mojom.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "url/origin.h"

#include "account_chooser.h"
#include "profile_context.h"
#include "profile_permission_controller.h"

namespace zitter::content_embedder {
namespace {

// Settings > Site permissions > Third-party sign-in.
constexpr char kSiteCategory[] = "webid-api";

// Keys in ProfilePermissionController::kFederatedIdentity.
// "sharing": {"<requester> <embedder> <idp>": {"<account id>": "<last used>"}}
constexpr char kSharing[] = "sharing";
// "signin": {"<idp>": bool}
constexpr char kSignin[] = "signin";
// "embargo": {"<rp>": {"count": int, "until": "<time>"}}
constexpr char kEmbargo[] = "embargo";
constexpr char kEmbargoCount[] = "count";
constexpr char kEmbargoUntil[] = "until";
// "reauthn_embargo": {"<rp>": "<time>"}
constexpr char kReauthnEmbargo[] = "reauthn_embargo";
// "requires_mediation": {"<site>": true}
constexpr char kRequiresMediation[] = "requires_mediation";

// Chrome's FedCM embargo after the user closes a passive prompt: 2 hours,
// then a day, a week and four weeks.
constexpr auto kDismissEmbargo = std::to_array<base::TimeDelta>(
    {base::Hours(2), base::Days(1), base::Days(7), base::Days(28)});
// Chrome's embargo between two automatic re-authentications on a site.
constexpr base::TimeDelta kReauthnEmbargoDuration = base::Minutes(10);
// Keeps the stored state small: oldest grants beyond this are dropped.
constexpr size_t kMaxSharingEntries = 512;

std::string TimeText(base::Time time) {
  return base::NumberToString(time.ToDeltaSinceWindowsEpoch().InMicroseconds());
}

std::optional<base::Time> ParseTime(const std::string* text) {
  int64_t micros = 0;
  if (!text || !base::StringToInt64(*text, &micros))
    return std::nullopt;
  return base::Time::FromDeltaSinceWindowsEpoch(base::Microseconds(micros));
}

std::string SharingKey(const url::Origin& requester,
                       const url::Origin& embedder,
                       const url::Origin& identity_provider) {
  return base::StrCat(
      {requester.Serialize(), " ", embedder.Serialize(), " ", identity_provider.Serialize()});
}

std::string SiteKey(const url::Origin& origin) {
  return net::SchemefulSite(origin).Serialize();
}

void DropOldestSharingEntry(base::DictValue& sharing) {
  std::string oldest_key;
  std::optional<base::Time> oldest;
  for (const auto [key, accounts] : sharing) {
    const auto* entries = accounts.GetIfDict();
    if (!entries)
      continue;
    for (const auto [account, used] : *entries) {
      const auto time = ParseTime(used.GetIfString()).value_or(base::Time());
      if (!oldest || time < *oldest) {
        oldest = time;
        oldest_key = key;
      }
    }
  }
  if (!oldest_key.empty())
    sharing.Remove(oldest_key);
}

}  // namespace

LabFederatedIdentity::LabFederatedIdentity(ProfileContext& profile)
    : permissions_(profile.site_permissions()) {}

LabFederatedIdentity::~LabFederatedIdentity() = default;

LabFederatedIdentity::PermissionStatus LabFederatedIdentity::GetApiPermissionStatus(
    const url::Origin& relying_party_embedder) {
  if (!base::FeatureList::IsEnabled(::features::kFedCm))
    return PermissionStatus::BLOCKED_VARIATIONS;
  if (permissions().SiteSetting(kSiteCategory, relying_party_embedder) == 2)
    return PermissionStatus::BLOCKED_SETTINGS;
  const auto* embargo = permissions().FederatedIdentityState().FindDict(kEmbargo);
  const auto* entry = embargo ? embargo->FindDict(relying_party_embedder.Serialize()) : nullptr;
  const auto until = entry ? ParseTime(entry->FindString(kEmbargoUntil)) : std::nullopt;
  if (until && base::Time::Now() < *until)
    return PermissionStatus::BLOCKED_EMBARGO;
  return PermissionStatus::GRANTED;
}

void LabFederatedIdentity::RecordDismissAndEmbargo(const url::Origin& relying_party_embedder) {
  if (relying_party_embedder.opaque())
    return;
  permissions().UpdateFederatedIdentityState([&](base::DictValue& state) {
    auto* entry = state.EnsureDict(kEmbargo)->EnsureDict(relying_party_embedder.Serialize());
    const int count = std::min(entry->FindInt(kEmbargoCount).value_or(0) + 1,
                               static_cast<int>(kDismissEmbargo.size()));
    entry->Set(kEmbargoCount, count);
    entry->Set(kEmbargoUntil, TimeText(base::Time::Now() + kDismissEmbargo[static_cast<size_t>(count - 1)]));
  });
}

void LabFederatedIdentity::RemoveEmbargoAndResetCounts(
    const url::Origin& relying_party_embedder) {
  permissions().UpdateFederatedIdentityState([&](base::DictValue& state) {
    if (auto* embargo = state.FindDict(kEmbargo))
      embargo->Remove(relying_party_embedder.Serialize());
  });
}

bool LabFederatedIdentity::HasThirdPartyCookiesAccess(
    content::RenderFrameHost&,
    const GURL&,
    const url::Origin&) const {
  // No per-site cookie exceptions exist in Zitter's Content profile.
  return false;
}

bool LabFederatedIdentity::AreThirdPartyCookiesEnabledInSettings() const {
  // The browsing flag is "block third-party cookies".
  return !permissions().BrowsingFlag("third_party_cookies");
}

bool LabFederatedIdentity::IsAutoReauthnSettingEnabled() {
  // Chrome's automatic re-authentication setting is allowed by default and
  // Zitter does not offer a separate switch for it.
  return true;
}

bool LabFederatedIdentity::IsAutoReauthnEmbargoed(const url::Origin& relying_party_embedder) {
  const auto* embargo = permissions().FederatedIdentityState().FindDict(kReauthnEmbargo);
  const auto last = embargo
                        ? ParseTime(embargo->FindString(relying_party_embedder.Serialize()))
                        : std::nullopt;
  return last && base::Time::Now() < *last + kReauthnEmbargoDuration;
}

bool LabFederatedIdentity::IsAutoReauthnDisabledByEmbedder(content::WebContents*) {
  return false;
}

void LabFederatedIdentity::RecordEmbargoForAutoReauthn(
    const url::Origin& relying_party_embedder) {
  if (relying_party_embedder.opaque())
    return;
  permissions().UpdateFederatedIdentityState([&](base::DictValue& state) {
    state.EnsureDict(kReauthnEmbargo)
        ->Set(relying_party_embedder.Serialize(), TimeText(base::Time::Now()));
  });
}

void LabFederatedIdentity::RemoveEmbargoForAutoReauthn(
    const url::Origin& relying_party_embedder) {
  permissions().UpdateFederatedIdentityState([&](base::DictValue& state) {
    if (auto* embargo = state.FindDict(kReauthnEmbargo))
      embargo->Remove(relying_party_embedder.Serialize());
  });
}

void LabFederatedIdentity::SetRequiresUserMediation(const url::Origin& rp_origin,
                                                    bool requires_user_mediation) {
  if (rp_origin.opaque())
    return;
  permissions().UpdateFederatedIdentityState([&](base::DictValue& state) {
    if (requires_user_mediation) {
      state.EnsureDict(kRequiresMediation)->Set(SiteKey(rp_origin), true);
    } else if (auto* sites = state.FindDict(kRequiresMediation)) {
      sites->Remove(SiteKey(rp_origin));
    }
  });
}

bool LabFederatedIdentity::RequiresUserMediation(const url::Origin& rp_origin) {
  const auto* sites = permissions().FederatedIdentityState().FindDict(kRequiresMediation);
  return !rp_origin.opaque() && sites && sites->FindBool(SiteKey(rp_origin)).value_or(false);
}

void LabFederatedIdentity::AddIdpSigninStatusObserver(IdpSigninStatusObserver* observer) {
  if (!signin_observers_.HasObserver(observer))
    signin_observers_.AddObserver(observer);
}

void LabFederatedIdentity::RemoveIdpSigninStatusObserver(IdpSigninStatusObserver* observer) {
  signin_observers_.RemoveObserver(observer);
}

bool LabFederatedIdentity::HasSharingPermission(const url::Origin& relying_party_requester,
                                                const url::Origin& relying_party_embedder,
                                                const url::Origin& identity_provider) {
  const auto* sharing = permissions().FederatedIdentityState().FindDict(kSharing);
  const auto* accounts =
      sharing ? sharing->FindDict(SharingKey(relying_party_requester, relying_party_embedder,
                                             identity_provider))
              : nullptr;
  return accounts && !accounts->empty();
}

std::optional<base::Time> LabFederatedIdentity::GetLastUsedTimestamp(
    const url::Origin& relying_party_requester,
    const url::Origin& relying_party_embedder,
    const url::Origin& identity_provider,
    const std::string& account_id) {
  const auto* sharing = permissions().FederatedIdentityState().FindDict(kSharing);
  const auto* accounts =
      sharing ? sharing->FindDict(SharingKey(relying_party_requester, relying_party_embedder,
                                             identity_provider))
              : nullptr;
  const auto* used = accounts ? accounts->Find(account_id) : nullptr;
  if (!used)
    return std::nullopt;
  // A grant without a readable time is known but undated.
  return ParseTime(used->GetIfString()).value_or(base::Time());
}

bool LabFederatedIdentity::HasSharingPermission(const url::Origin& relying_party_requester) {
  const auto* sharing = permissions().FederatedIdentityState().FindDict(kSharing);
  if (!sharing)
    return false;
  const auto prefix = relying_party_requester.Serialize() + " ";
  for (const auto [key, accounts] : *sharing) {
    if (key.starts_with(prefix) && accounts.is_dict() && !accounts.GetDict().empty())
      return true;
  }
  return false;
}

void LabFederatedIdentity::GrantSharingPermission(const url::Origin& relying_party_requester,
                                                  const url::Origin& relying_party_embedder,
                                                  const url::Origin& identity_provider,
                                                  const std::string& account_id) {
  if (relying_party_requester.opaque() || relying_party_embedder.opaque() ||
      identity_provider.opaque() || account_id.empty()) {
    return;
  }
  const auto key = SharingKey(relying_party_requester, relying_party_embedder,
                              identity_provider);
  permissions().UpdateFederatedIdentityState([&](base::DictValue& state) {
    auto* sharing = state.EnsureDict(kSharing);
    if (!sharing->FindDict(key) && sharing->size() >= kMaxSharingEntries)
      DropOldestSharingEntry(*sharing);
    sharing->EnsureDict(key)->Set(account_id, TimeText(base::Time::Now()));
  });
}

void LabFederatedIdentity::RevokeSharingPermission(const url::Origin& relying_party_requester,
                                                   const url::Origin& relying_party_embedder,
                                                   const url::Origin& identity_provider,
                                                   const std::string& account_id) {
  const auto key = SharingKey(relying_party_requester, relying_party_embedder,
                              identity_provider);
  permissions().UpdateFederatedIdentityState([&](base::DictValue& state) {
    auto* sharing = state.FindDict(kSharing);
    if (!sharing)
      return;
    // As Chrome: an unknown account revokes every grant of the triple, so a
    // single sign-in can't be revoked over and over.
    auto* accounts = sharing->FindDict(key);
    if (!accounts || !accounts->Remove(account_id) || accounts->empty())
      sharing->Remove(key);
  });
}

void LabFederatedIdentity::RefreshExistingSharingPermission(
    const url::Origin& relying_party_requester,
    const url::Origin& relying_party_embedder,
    const url::Origin& identity_provider,
    const std::string& account_id) {
  const auto key = SharingKey(relying_party_requester, relying_party_embedder,
                              identity_provider);
  permissions().UpdateFederatedIdentityState([&](base::DictValue& state) {
    auto* sharing = state.FindDict(kSharing);
    auto* accounts = sharing ? sharing->FindDict(key) : nullptr;
    if (accounts && accounts->Find(account_id))
      accounts->Set(account_id, TimeText(base::Time::Now()));
  });
}

std::optional<bool> LabFederatedIdentity::GetIdpSigninStatus(const url::Origin& idp_origin) {
  const auto* signin = permissions().FederatedIdentityState().FindDict(kSignin);
  return signin ? signin->FindBool(idp_origin.Serialize()) : std::nullopt;
}

base::ListValue LabFederatedIdentity::GetAccounts(const url::Origin&) {
  // Accounts pushed with the login status are the experimental lightweight
  // FedCM mode, which Zitter does not keep.
  return base::ListValue();
}

void LabFederatedIdentity::SetIdpSigninStatus(
    const url::Origin& idp_origin,
    bool idp_signin_status,
    base::optional_ref<const blink::common::webid::LoginStatusOptions>) {
  if (idp_origin.opaque())
    return;
  permissions().UpdateFederatedIdentityState([&](base::DictValue& state) {
    state.EnsureDict(kSignin)->Set(idp_origin.Serialize(), idp_signin_status);
  });
  for (auto& observer : signin_observers_)
    observer.OnIdpSigninStatusReceived(idp_origin, idp_signin_status);
}

std::vector<GURL> LabFederatedIdentity::GetRegisteredIdPs() {
  return registered_idps_;
}

void LabFederatedIdentity::RegisterIdP(const GURL& url) {
  if (!std::ranges::contains(registered_idps_, url))
    registered_idps_.push_back(url);
}

void LabFederatedIdentity::UnregisterIdP(const GURL& url) {
  std::erase(registered_idps_, url);
}

void LabFederatedIdentity::OnSetRequiresUserMediation(const url::Origin&,
                                                      base::OnceClosure callback) {
  std::move(callback).Run();
}

void ClearFederatedSharingGrants(base::DictValue& state, base::Time begin, base::Time end) {
  auto* sharing = state.FindDict(kSharing);
  if (!sharing)
    return;
  std::vector<std::string> empty_keys;
  for (auto [key, value] : *sharing) {
    auto* accounts = value.GetIfDict();
    if (!accounts) {
      empty_keys.push_back(key);
      continue;
    }
    std::vector<std::string> removed;
    for (const auto [account, used] : *accounts) {
      const auto time = ParseTime(used.GetIfString()).value_or(base::Time());
      if (time >= begin && time < end)
        removed.push_back(account);
    }
    for (const auto& account : removed)
      accounts->Remove(account);
    if (accounts->empty())
      empty_keys.push_back(key);
  }
  for (const auto& key : empty_keys)
    sharing->Remove(key);
}

namespace {

using content::IdentityRequestAccount;
using content::IdentityRequestDialogController;
using DismissReason = IdentityRequestDialogController::DismissReason;

std::u16string AccountName(const IdentityRequestAccount& account) {
  return base::UTF8ToUTF16(!account.display_name.empty() ? account.display_name
                           : !account.name.empty()       ? account.name
                                                         : account.display_identifier);
}

std::u16string AccountDetail(const IdentityRequestAccount& account) {
  return base::UTF8ToUTF16(!account.display_identifier.empty() ? account.display_identifier
                                                               : account.email);
}

std::u16string IdpName(const IdentityRequestAccount& account) {
  return account.identity_provider
             ? base::UTF8ToUTF16(account.identity_provider->idp_for_display)
             : std::u16string();
}

// Closes nothing by itself: reports when the IdP's sign-in page goes away.
class PopupWatcher final : public content::WebContentsObserver {
 public:
  PopupWatcher(content::WebContents* popup, base::OnceClosure gone)
      : content::WebContentsObserver(popup), gone_(std::move(gone)) {}

  void WebContentsDestroyed() override {
    Observe(nullptr);
    if (gone_)
      std::move(gone_).Run();
  }

 private:
  base::OnceClosure gone_;
};

class LabIdentityDialog final : public IdentityRequestDialogController {
 public:
  explicit LabIdentityDialog(content::WebContents* contents)
      : rp_contents_(contents ? contents->GetWeakPtr() : nullptr) {}
  ~LabIdentityDialog() override = default;

  bool ShowAccountsDialog(
      content::RelyingPartyData rp_data,
      const std::vector<scoped_refptr<content::IdentityProviderData>>& idp_list,
      const std::vector<scoped_refptr<IdentityRequestAccount>>& accounts,
      const std::vector<scoped_refptr<IdentityRequestAccount>>&,
      blink::mojom::RpMode rp_mode,
      AccountSelectionCallback on_selected,
      LoginToIdPCallback,
      DismissCallback dismiss_callback,
      AccountsDisplayedCallback accounts_displayed_callback) override {
    if (accounts.empty()) {
      std::move(dismiss_callback).Run(DismissReason::kOther);
      return false;
    }
    rp_name_ = rp_data.rp_for_display;
    std::u16string idp_name;
    if (idp_list.size() == 1)
      idp_name = base::UTF8ToUTF16(idp_list.front()->idp_for_display);
    AccountChooser::Page page;
    page.title = Title(idp_name);
    bool sign_up = false;
    for (const auto& account : accounts) {
      AccountChooser::Account row;
      row.name = AccountName(*account);
      row.detail = AccountDetail(*account);
      if (idp_list.size() > 1 && account->identity_provider)
        row.detail += u" · " + IdpName(*account);
      if (!account->decoded_picture.IsEmpty())
        row.picture = account->decoded_picture.AsImageSkia();
      page.accounts.push_back(std::move(row));
      if (account->browser_trusted_login_state ==
              IdentityRequestAccount::LoginState::kSignUp &&
          account->identity_provider &&
          !account->identity_provider->disclosure_fields.empty()) {
        sign_up = true;
      }
    }
    if (sign_up) {
      page.note = prototype::TF(
          u"Aby kontynuować, {} udostępni tej witrynie Twoje imię i nazwisko, adres "
          u"e-mail i zdjęcie profilowe.",
          {idp_name.empty() ? prototype::T(u"dostawca tożsamości") : idp_name});
    }
    page.accept = prototype::T(u"Kontynuuj");
    page.cancel = prototype::T(u"Anuluj");
    accounts_ = accounts;
    on_selected_ = std::move(on_selected);
    dismiss_ = std::move(dismiss_callback);
    if (!Present(std::move(page), rp_mode,
                 base::BindOnce(&LabIdentityDialog::OnAccountAnswer,
                                weak_factory_.GetWeakPtr()))) {
      Dismiss(DismissReason::kOther);
      return false;
    }
    std::move(accounts_displayed_callback).Run();
    return true;
  }

  bool ShowFailureDialog(const content::RelyingPartyData& rp_data,
                         const std::string& idp_for_display,
                         blink::mojom::RpContext,
                         blink::mojom::RpMode rp_mode,
                         const content::IdentityProviderMetadata& idp_metadata,
                         const std::vector<scoped_refptr<IdentityRequestAccount>>&,
                         DismissCallback dismiss_callback,
                         LoginToIdPCallback login_callback) override {
    rp_name_ = rp_data.rp_for_display;
    const auto idp_name = base::UTF8ToUTF16(idp_for_display);
    AccountChooser::Page page;
    page.title = Title(idp_name);
    page.message = prototype::TF(
        u"Nie jesteś zalogowany w {}. Zaloguj się, aby kontynuować.", {idp_name});
    if (idp_metadata.idp_login_url.is_valid())
      page.accept = prototype::T(u"Kontynuuj");
    page.cancel = prototype::T(u"Anuluj");
    dismiss_ = std::move(dismiss_callback);
    login_ = base::BindOnce(login_callback, idp_metadata.config_url,
                            idp_metadata.idp_login_url);
    if (!Present(std::move(page), rp_mode,
                 base::BindOnce(&LabIdentityDialog::OnFailureAnswer,
                                weak_factory_.GetWeakPtr()))) {
      Dismiss(DismissReason::kOther);
      return false;
    }
    return true;
  }

  bool ShowErrorDialog(const content::RelyingPartyData& rp_data,
                       const std::string& idp_for_display,
                       blink::mojom::RpContext,
                       blink::mojom::RpMode rp_mode,
                       const content::IdentityProviderMetadata&,
                       const std::optional<content::IdentityCredentialTokenError>& error,
                       DismissCallback dismiss_callback,
                       MoreDetailsCallback more_details_callback) override {
    rp_name_ = rp_data.rp_for_display;
    const auto idp_name = base::UTF8ToUTF16(idp_for_display);
    AccountChooser::Page page;
    page.title = Title(idp_name);
    page.message = prototype::TF(u"Nie udało się zalogować w {} przez {}.",
                                 {rp_name_, idp_name});
    if (more_details_callback && error && error->url.is_valid())
      page.accept = prototype::T(u"Więcej informacji");
    page.cancel = prototype::T(u"Zamknij");
    dismiss_ = std::move(dismiss_callback);
    more_details_ = std::move(more_details_callback);
    if (!Present(std::move(page), rp_mode,
                 base::BindOnce(&LabIdentityDialog::OnErrorAnswer,
                                weak_factory_.GetWeakPtr()))) {
      Dismiss(DismissReason::kOther);
      return false;
    }
    return true;
  }

  bool ShowLoadingDialog(const content::RelyingPartyData& rp_data,
                         const std::string& idp_for_display,
                         blink::mojom::RpContext,
                         blink::mojom::RpMode rp_mode,
                         DismissCallback dismiss_callback) override {
    rp_name_ = rp_data.rp_for_display;
    AccountChooser::Page page;
    page.title = Title(base::UTF8ToUTF16(idp_for_display));
    page.message = prototype::T(u"Wczytywanie kont…");
    page.cancel = prototype::T(u"Anuluj");
    dismiss_ = std::move(dismiss_callback);
    if (!Present(std::move(page), rp_mode,
                 base::BindOnce(&LabIdentityDialog::OnCancelAnswer,
                                weak_factory_.GetWeakPtr()))) {
      Dismiss(DismissReason::kOther);
      return false;
    }
    return true;
  }

  bool ShowVerifyingDialog(const content::RelyingPartyData& rp_data,
                           const scoped_refptr<content::IdentityProviderData>& idp_data,
                           const scoped_refptr<IdentityRequestAccount>& account,
                           IdentityRequestAccount::SignInMode,
                           blink::mojom::RpMode rp_mode,
                           AccountsDisplayedCallback accounts_displayed_callback) override {
    if (!account)
      return false;
    rp_name_ = rp_data.rp_for_display;
    AccountChooser::Page page;
    page.title =
        Title(idp_data ? base::UTF8ToUTF16(idp_data->idp_for_display) : std::u16string());
    page.message = prototype::TF(u"Logowanie jako {}…", {AccountName(*account)});
    page.cancel = prototype::T(u"Anuluj");
    // Content gives no way to stop an automatic re-authentication; Cancel
    // only hides the notice.
    if (!Present(std::move(page), rp_mode,
                 base::BindOnce(&LabIdentityDialog::OnCancelAnswer,
                                weak_factory_.GetWeakPtr()))) {
      return false;
    }
    std::move(accounts_displayed_callback).Run();
    return true;
  }

  // The IdP's sign-in or continuation page opens in a new tab next to the
  // relying party.
  content::WebContents* ShowModalDialog(const GURL& url,
                                        blink::mojom::RpMode,
                                        DismissCallback dismiss_callback,
                                        ShownModalAsyncCallback,
                                        NativeAppResultCallback) override {
    // A modal account chooser would block the window the user signs in in.
    chooser_.reset();
    popup_.reset();
    if (!rp_contents_ || !url.is_valid()) {
      std::move(dismiss_callback).Run(DismissReason::kOther);
      return nullptr;
    }
    content::OpenURLParams params(url, content::Referrer(),
                                  WindowOpenDisposition::NEW_FOREGROUND_TAB,
                                  ui::PAGE_TRANSITION_AUTO_TOPLEVEL,
                                  /*is_renderer_initiated=*/false);
    // The user asked for the page in the FedCM dialog.
    params.user_gesture = true;
    auto* popup = rp_contents_->OpenURL(params, {});
    if (!popup) {
      std::move(dismiss_callback).Run(DismissReason::kOther);
      return nullptr;
    }
    popup_dismiss_ = std::move(dismiss_callback);
    popup_ = std::make_unique<PopupWatcher>(
        popup, base::BindOnce(&LabIdentityDialog::OnPopupGone, weak_factory_.GetWeakPtr()));
    return popup;
  }

  void CloseModalDialog() override {
    popup_dismiss_.Reset();
    if (!popup_)
      return;
    auto* popup = popup_->web_contents();
    popup_.reset();
    if (popup)
      popup->ClosePage();
  }

 private:
  std::u16string Title(const std::u16string& idp_name) const {
    return idp_name.empty() ? prototype::TF(u"Zaloguj się w {}", {rp_name_})
                            : prototype::TF(u"Zaloguj się w {} przez {}", {rp_name_, idp_name});
  }

  bool Present(AccountChooser::Page page, blink::mojom::RpMode rp_mode,
               AccountChooser::Done done) {
    if (chooser_) {
      chooser_->Update(std::move(page), std::move(done));
      return true;
    }
    if (!rp_contents_)
      return false;
    // A page that asks on load (passive mode) must not block the window.
    chooser_ = AccountChooser::Show(*rp_contents_, std::move(page),
                                    rp_mode == blink::mojom::RpMode::kActive,
                                    std::move(done));
    return chooser_ != nullptr;
  }

  // Runs the pending dismiss callback last: content may delete `this`.
  void Dismiss(DismissReason reason) {
    chooser_.reset();
    login_.Reset();
    more_details_.Reset();
    if (auto dismiss = std::move(dismiss_))
      std::move(dismiss).Run(reason);
  }

  static DismissReason ReasonFor(AccountChooser::Answer answer) {
    return answer == AccountChooser::Answer::kCancelled ? DismissReason::kCloseButton
                                                        : DismissReason::kOther;
  }

  void OnAccountAnswer(AccountChooser::Answer answer, std::optional<size_t> index) {
    if (answer != AccountChooser::Answer::kAccepted || !index || *index >= accounts_.size() ||
        !on_selected_) {
      Dismiss(ReasonFor(answer));
      return;
    }
    const auto account = accounts_[*index];
    const GURL config_url = account->identity_provider
                                ? account->identity_provider->idp_metadata.config_url
                                : GURL();
    // Content fetches the token next; the dialog says so until it is done.
    AccountChooser::Page page;
    page.title = Title(IdpName(*account));
    page.message = prototype::TF(u"Logowanie jako {}…", {AccountName(*account)});
    page.cancel = prototype::T(u"Anuluj");
    if (chooser_) {
      chooser_->Update(std::move(page), base::BindOnce(&LabIdentityDialog::OnCancelAnswer,
                                                       weak_factory_.GetWeakPtr()));
    }
    std::move(on_selected_)
        .Run(config_url, account->id,
             account->browser_trusted_login_state ==
                 IdentityRequestAccount::LoginState::kSignIn);
  }

  void OnFailureAnswer(AccountChooser::Answer answer, std::optional<size_t>) {
    if (answer != AccountChooser::Answer::kAccepted || !login_) {
      Dismiss(ReasonFor(answer));
      return;
    }
    // Content opens the IdP's sign-in page through ShowModalDialog(), whose
    // own callback reports if the user closes it.
    chooser_.reset();
    dismiss_.Reset();
    std::move(login_).Run();
  }

  void OnErrorAnswer(AccountChooser::Answer answer, std::optional<size_t>) {
    if (answer == AccountChooser::Answer::kAccepted && more_details_) {
      auto weak = weak_factory_.GetWeakPtr();
      chooser_.reset();
      std::move(more_details_).Run();
      if (weak)
        Dismiss(DismissReason::kMoreDetailsButton);
      return;
    }
    Dismiss(answer == AccountChooser::Answer::kCancelled ? DismissReason::kGotItButton
                                                         : DismissReason::kOther);
  }

  void OnCancelAnswer(AccountChooser::Answer answer, std::optional<size_t>) {
    Dismiss(ReasonFor(answer));
  }

  void OnPopupGone() {
    popup_.reset();
    if (auto dismiss = std::move(popup_dismiss_))
      std::move(dismiss).Run(DismissReason::kOther);
  }

  base::WeakPtr<content::WebContents> rp_contents_;
  std::u16string rp_name_;
  std::unique_ptr<AccountChooser> chooser_;
  std::vector<scoped_refptr<IdentityRequestAccount>> accounts_;
  AccountSelectionCallback on_selected_;
  DismissCallback dismiss_;
  base::OnceClosure login_;
  MoreDetailsCallback more_details_;
  std::unique_ptr<PopupWatcher> popup_;
  DismissCallback popup_dismiss_;
  base::WeakPtrFactory<LabIdentityDialog> weak_factory_{this};
};

}  // namespace

std::unique_ptr<content::IdentityRequestDialogController> CreateLabIdentityDialog(
    content::WebContents* contents) {
  return std::make_unique<LabIdentityDialog>(contents);
}

}  // namespace zitter::content_embedder
