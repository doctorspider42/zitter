// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// FedCM (navigator.credentials.get({identity})) for a Content profile.
// Content runs the protocol; the profile keeps which accounts the user shared
// with which sites, identity provider sign-in status and the embargo after
// dismissed prompts, and honours Settings > Site permissions > Third-party
// sign-in ("webid-api"). The dialog lists the identity provider's accounts
// and shows loading, verifying and error states.

#ifndef ZITTER_CONTENT_FEDERATED_IDENTITY_H_
#define ZITTER_CONTENT_FEDERATED_IDENTITY_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/memory/raw_ref.h"
#include "base/observer_list.h"
#include "base/time/time.h"
#include "base/values.h"
#include "content/public/browser/webid/federated_identity_api_permission_context_delegate.h"
#include "content/public/browser/webid/federated_identity_auto_reauthn_permission_context_delegate.h"
#include "content/public/browser/webid/federated_identity_permission_context_delegate.h"
#include "url/gurl.h"

namespace content {
class IdentityRequestDialogController;
class WebContents;
}

namespace zitter::content_embedder {

class ProfileContext;
class ProfilePermissionController;

class LabFederatedIdentity final
    : public content::FederatedIdentityApiPermissionContextDelegate,
      public content::FederatedIdentityAutoReauthnPermissionContextDelegate,
      public content::FederatedIdentityPermissionContextDelegate {
 public:
  explicit LabFederatedIdentity(ProfileContext& profile);
  ~LabFederatedIdentity() override;

  // content::FederatedIdentityApiPermissionContextDelegate:
  PermissionStatus GetApiPermissionStatus(
      const url::Origin& relying_party_embedder) override;
  void RecordDismissAndEmbargo(const url::Origin& relying_party_embedder) override;
  void RemoveEmbargoAndResetCounts(const url::Origin& relying_party_embedder) override;
  bool HasThirdPartyCookiesAccess(content::RenderFrameHost& host,
                                  const GURL& provider_url,
                                  const url::Origin& relying_party_embedder) const override;
  bool AreThirdPartyCookiesEnabledInSettings() const override;

  // content::FederatedIdentityAutoReauthnPermissionContextDelegate:
  bool IsAutoReauthnSettingEnabled() override;
  bool IsAutoReauthnEmbargoed(const url::Origin& relying_party_embedder) override;
  bool IsAutoReauthnDisabledByEmbedder(content::WebContents* web_contents) override;
  void RecordEmbargoForAutoReauthn(const url::Origin& relying_party_embedder) override;
  void RemoveEmbargoForAutoReauthn(const url::Origin& relying_party_embedder) override;
  void SetRequiresUserMediation(const url::Origin& rp_origin,
                                bool requires_user_mediation) override;
  bool RequiresUserMediation(const url::Origin& rp_origin) override;

  // content::FederatedIdentityPermissionContextDelegate:
  void AddIdpSigninStatusObserver(IdpSigninStatusObserver* observer) override;
  void RemoveIdpSigninStatusObserver(IdpSigninStatusObserver* observer) override;
  bool HasSharingPermission(const url::Origin& relying_party_requester,
                            const url::Origin& relying_party_embedder,
                            const url::Origin& identity_provider) override;
  std::optional<base::Time> GetLastUsedTimestamp(
      const url::Origin& relying_party_requester,
      const url::Origin& relying_party_embedder,
      const url::Origin& identity_provider,
      const std::string& account_id) override;
  bool HasSharingPermission(const url::Origin& relying_party_requester) override;
  void GrantSharingPermission(const url::Origin& relying_party_requester,
                              const url::Origin& relying_party_embedder,
                              const url::Origin& identity_provider,
                              const std::string& account_id) override;
  void RevokeSharingPermission(const url::Origin& relying_party_requester,
                               const url::Origin& relying_party_embedder,
                               const url::Origin& identity_provider,
                               const std::string& account_id) override;
  void RefreshExistingSharingPermission(const url::Origin& relying_party_requester,
                                        const url::Origin& relying_party_embedder,
                                        const url::Origin& identity_provider,
                                        const std::string& account_id) override;
  std::optional<bool> GetIdpSigninStatus(const url::Origin& idp_origin) override;
  base::ListValue GetAccounts(const url::Origin& identity_provider) override;
  void SetIdpSigninStatus(
      const url::Origin& idp_origin,
      bool idp_signin_status,
      base::optional_ref<const blink::common::webid::LoginStatusOptions> options) override;
  std::vector<GURL> GetRegisteredIdPs() override;
  void RegisterIdP(const GURL& url) override;
  void UnregisterIdP(const GURL& url) override;
  void OnSetRequiresUserMediation(const url::Origin& relying_party,
                                  base::OnceClosure callback) override;

 private:
  ProfilePermissionController& permissions() const { return *permissions_; }

  const raw_ref<ProfilePermissionController> permissions_;
  base::ObserverList<IdpSigninStatusObserver> signin_observers_;
  // IdP registration is a session-only experiment in Content.
  std::vector<GURL> registered_idps_;
};

// Removes the sharing grants last used between `begin` and `end` from the
// state kept in ProfilePermissionController::kFederatedIdentity.
void ClearFederatedSharingGrants(base::DictValue& state, base::Time begin, base::Time end);

// The FedCM dialog for ContentBrowserClient::CreateIdentityRequestDialogController().
std::unique_ptr<content::IdentityRequestDialogController> CreateLabIdentityDialog(
    content::WebContents* contents);

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_FEDERATED_IDENTITY_H_
