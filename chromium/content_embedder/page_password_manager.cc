// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "page_password_manager.h"

#include <memory>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "chromium/prototype/i18n.h"
#include "chromium/prototype/page_dialogs.h"
#include "chromium/prototype/sidebar_layout.h"
#include "chromium/prototype/sidebar_metal.h"
#include "chromium/prototype/sidebar_scene.rs.h"
#if BUILDFLAG(IS_MAC)
#include "chromium/prototype/workspace_keyboard_mac.h"
#endif
#include "components/metrics/profile_metrics_service.h"
#include "components/password_manager/content/browser/content_password_manager_driver_factory.h"
#include "components/password_manager/core/browser/credentials_filter.h"
#include "components/password_manager/core/browser/features/password_manager_features_util.h"
#include "components/password_manager/core/browser/password_feature_manager.h"
#include "components/password_manager/core/browser/password_form_manager_for_ui.h"
#include "components/password_manager/core/browser/password_manager.h"
#include "components/password_manager/core/browser/password_manager_client.h"
#include "components/password_manager/core/browser/password_cross_domain_confirmation_popup_controller.h"
#include "components/password_manager/core/browser/undo_password_change_controller.h"
#include "components/password_manager/core/common/password_manager_pref_names.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/pref_service.h"
#include "components/safe_browsing/buildflags.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/ssl_status.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "net/cert/cert_status_flags.h"
#include "services/metrics/public/cpp/ukm_source_id.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "ui/base/mojom/ui_base_types.mojom.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/layout/layout_provider.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"
#include "url/gurl.h"
#include "url/origin.h"

#include "profile_context.h"

namespace zitter::content_embedder {
namespace {

namespace pm = password_manager;

class LocalCredentialPolicy final : public pm::CredentialsFilter,
                                    public pm::PasswordFeatureManager {
 public:
  bool ShouldSave(const pm::PasswordForm& form) const override {
    const auto origin = url::Origin::Create(form.url);
    return form.scheme == pm::PasswordForm::Scheme::kHtml &&
           !form.IsFederatedCredential() && !origin.opaque() &&
           form.signon_realm == origin.GetURL().spec();
  }
  bool ShouldSaveGaiaPasswordHash(const pm::PasswordForm&) const override {
    return false;
  }
  bool ShouldSaveEnterprisePasswordHash(const pm::PasswordForm&) const override {
    return false;
  }
  bool IsSyncAccountEmail(const std::string&) const override { return false; }
  bool IsGenerationEnabled() const override { return false; }
  bool IsBiometricAuthenticationBeforeFillingEnabled() const override {
    return false;
  }
  bool IsAccountStorageActive() const override { return false; }
  pm::features_util::PasswordAccountStorageUsageLevel
  ComputePasswordAccountStorageUsageLevel() const override {
    return pm::features_util::PasswordAccountStorageUsageLevel::
        kNotUsingAccountStorage;
  }
};

class PagePasswordManager final
    : public pm::PasswordManagerClient,
      public content::WebContentsObserver,
      public content::WebContentsUserData<PagePasswordManager> {
 public:
  ~PagePasswordManager() override { DismissPrompt(); }

  void ConfigurePrompt(std::u16string profile_label,
                       base::RepeatingCallback<gfx::Rect()> anchor) {
    profile_label_ = std::move(profile_label);
    anchor_ = std::move(anchor);
  }

  views::Widget* prompt() const { return prompt_.get(); }

  bool PromptMetalContains(std::string_view text) const {
    return scene_ && scene_->contains_text(text);
  }

  bool IsFillingEnabled(const url::Origin& origin,
                        base::optional_ref<const GURL> url) const override {
    return !contents_->GetPrimaryMainFrame()->IsErrorDocument() &&
           !origin.opaque() && origin == GetLastCommittedOrigin() &&
           (!url || url::Origin::Create(*url) == origin) &&
           IsCommittedMainFrameSecure() && !WasLastNavigationHTTPError() &&
           GetProfilePasswordStore();
  }

  bool IsCommittedMainFrameSecure() const override {
    const GURL& url = GetLastCommittedURL();
    if (url.SchemeIs("https")) {
      auto* entry = Entry();
      if (!entry)
        return false;
      const auto& ssl = entry->GetSSL();
      constexpr int kInsecureContent =
          content::SSLStatus::RAN_INSECURE_CONTENT |
          content::SSLStatus::RAN_CONTENT_WITH_CERT_ERRORS;
      return ssl.certificate && !net::IsCertStatusError(ssl.cert_status) &&
             !(ssl.content_status & kInsecureContent);
    }
    return url.SchemeIs("http") && url.host() == "127.0.0.1";
  }

  bool WasLastNavigationHTTPError() const override {
    const auto* entry = Entry();
    return entry && entry->GetHttpStatusCode() >= 400;
  }

  bool IsSavingAndFillingEnabled(
      const url::Origin& origin,
      base::optional_ref<const GURL> url) const override {
    return !profile_label_.empty() && IsFillingEnabled(origin, url) &&
           profile_->GetPasswordSavingSetting().value_or(false);
  }
  bool IsAutoSignInEnabled() const override { return false; }

  PrefService* GetPrefs() const override { return profile_->GetPasswordPrefs(); }
  PrefService* GetLocalStatePrefs() const override { return nullptr; }
  metrics::ProfileMetricsService* GetProfileMetricsService() override {
    return &metrics_;
  }
  const syncer::SyncService* GetSyncService() const override { return nullptr; }
  affiliations::AffiliationService* GetAffiliationService() override {
    return nullptr;
  }
  pm::PasswordStoreInterface* GetProfilePasswordStore() const override {
    return profile_->GetPasswordStore();
  }
  pm::PasswordStoreInterface* GetAccountPasswordStore() const override {
    return nullptr;
  }
  pm::PasswordReuseManager* GetPasswordReuseManager() const override {
    return nullptr;
  }
  pm::PasswordChangeServiceInterface* GetPasswordChangeService() const override {
    return nullptr;
  }
  const pm::PasswordManagerInterface* GetPasswordManager() const override {
    return &manager_;
  }
  const pm::PasswordFeatureManager* GetPasswordFeatureManager() const override {
    return &policy_;
  }
  const pm::CredentialsFilter* GetStoreResultFilter() const override {
    return &policy_;
  }
  pm::UndoPasswordChangeController* GetUndoPasswordChangeController() override {
    return &undo_;
  }

  const GURL& GetLastCommittedURL() const override {
    return GetWebContents().GetLastCommittedURL();
  }
  url::Origin GetLastCommittedOrigin() const override {
    return GetWebContents().GetPrimaryMainFrame()->GetLastCommittedOrigin();
  }
  ukm::SourceId GetUkmSourceId() override { return ukm::kInvalidSourceId; }
  pm::PasswordManagerMetricsRecorder* GetMetricsRecorder() override {
    return nullptr;
  }
  signin::IdentityManager* GetIdentityManager() override { return nullptr; }
  const signin::IdentityManager* GetIdentityManager() const override {
    return nullptr;
  }
  scoped_refptr<network::SharedURLLoaderFactory> GetURLLoaderFactory() override {
    return nullptr;
  }

  bool IsIsolationForPasswordSitesEnabled() const override { return true; }
  bool IsNewTabPage() const override { return false; }

  bool PromptUserToSaveOrUpdatePassword(
      std::unique_ptr<pm::PasswordFormManagerForUI> form,
      bool update) override {
    const auto origin = url::Origin::Create(form->GetURL());
    if (!CanActOnPrompt(origin) || form->IsBlocklisted() ||
        form->GetPendingCredentials().signon_realm != origin.GetURL().spec() ||
        form->GetPendingCredentials().password_value.empty()) {
      return false;
    }
    auto* parent = views::Widget::GetWidgetForNativeWindow(
        contents_->GetTopLevelNativeWindow());
    if (!parent)
      return false;

    DismissPrompt();
    pending_form_ = std::move(form);
    pending_update_ = update;
    const auto& credential = pending_form_->GetPendingCredentials();
    auto username = base::CollapseWhitespace(
        credential.username_value.substr(0, 128), false);
    base::i18n::SanitizeUserSuppliedString(&username);
    const auto copy = sidebar::password_prompt_copy(
        update, origin.Serialize(), base::UTF16ToUTF8(username),
        base::UTF16ToUTF8(profile_label_));
    auto body = std::make_unique<views::View>();
    scene_ = prototype::CreatePanelScene(body.get());
    prototype::SetSidebarPanelLayout(
        body.get(), true, gfx::Insets(),
        views::LayoutProvider::Get()->GetDistanceMetric(
            views::DISTANCE_UNRELATED_CONTROL_VERTICAL));
    for (const auto& line : copy.lines) {
      body->AddChildView(scene_->MakeText(
          base::UTF8ToUTF16(std::string(line)), true, false, 0));
    }
    body->AddChildView(scene_->MakeText(
        prototype::TF(u"Profil: {}", {profile_label_}), false, false, 0));
    auto actions = scene_->MakeActions(0);
    auto* never = scene_->AddAction(
        actions.get(), base::BindRepeating(&PagePasswordManager::BlocklistSite,
                                           prompt_weak_.GetWeakPtr()),
        prototype::T(u"Nigdy tutaj"));
    scene_->SetActionKind(never, 3);
    delegate_ = prototype::CreateSceneModalDialog(std::move(body), false, false);
    delegate_->SetExtraView(std::move(actions));
    if (auto* bubble = delegate_->AsBubbleDialogDelegate())
      bubble->set_fixed_width(300);
    delegate_->SetOwnershipOfNewWidget(
        views::Widget::InitParams::CLIENT_OWNS_WIDGET);
    delegate_->SetTitle(base::UTF8ToUTF16(std::string(copy.title)));
    delegate_->SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk) |
                          static_cast<int>(ui::mojom::DialogButton::kCancel));
    delegate_->SetDefaultButton(static_cast<int>(ui::mojom::DialogButton::kCancel));
    delegate_->SetButtonLabel(ui::mojom::DialogButton::kOk,
                              base::UTF8ToUTF16(std::string(copy.accept)));
    delegate_->SetButtonLabel(ui::mojom::DialogButton::kCancel,
                              base::UTF8ToUTF16(std::string(copy.cancel)));
    delegate_->SetAcceptCallback(base::BindOnce(&PagePasswordManager::SavePrompt,
                                                prompt_weak_.GetWeakPtr()));
    delegate_->SetCancelCallback(base::BindOnce(
        &PagePasswordManager::DeclinePrompt, prompt_weak_.GetWeakPtr()));
    prompt_.reset(views::DialogDelegate::CreateDialogWidget(
        delegate_.get(), parent->GetNativeWindow(), parent->GetNativeView()));
    prompt_->SetColorModeOverride(parent->color_mode_override());
    prompt_->MakeCloseSynchronous(base::BindOnce(
        [](base::WeakPtr<PagePasswordManager> self, views::Widget::ClosedReason) {
          if (self)
            self->DismissPrompt();
        },
        prompt_weak_.GetWeakPtr()));
    if (auto* bubble = delegate_->AsBubbleDialogDelegate()) {
      const auto anchor = anchor_.Run();
      const auto bounds = parent->GetClientAreaBoundsInScreen();
      bubble->SetAnchorRect(anchor.IsEmpty()
                                ? gfx::Rect(bounds.x() + 166, bounds.bottom() - 180,
                                            0, 0)
                                : gfx::Rect(anchor.CenterPoint().x(),
                                            anchor.bottom() + 6, 0, 0));
    }
#if BUILDFLAG(IS_MAC)
    prototype::InstallWorkspaceKeyboardRouting(prompt_.get(), true);
#endif
    prompt_->ShowInactive();
    scene_->Schedule();
    return true;
  }
  void PromptUserToMovePasswordToAccount(
      std::unique_ptr<pm::PasswordFormManagerForUI>) override {}
  void ShowManualFallbackForSaving(
      std::unique_ptr<pm::PasswordFormManagerForUI>, bool, bool) override {}
  void HideManualFallbackForSaving() override {}
  void FocusedInputChanged(pm::PasswordManagerDriver*, autofill::FieldRendererId,
                           autofill::mojom::FocusedFieldType) override {}
  bool PromptUserToChooseCredentials(
      std::vector<std::unique_ptr<pm::PasswordForm>>, const url::Origin&,
      CredentialsCallback) override {
    return false;
  }
  void NotifyUserAutoSignin(std::vector<std::unique_ptr<pm::PasswordForm>>,
                            const url::Origin&) override {}
  void NotifyUserCouldBeAutoSignedIn(std::unique_ptr<pm::PasswordForm>) override {}
  void NotifySuccessfulLoginWithExistingPassword(
      std::unique_ptr<pm::PasswordFormManagerForUI>) override {}
  void NotifyKeychainError() override {}
  bool IsPasswordChangeOngoing() override { return false; }
  void NotifyStorePasswordCalled() override {}
  void AutomaticPasswordSave(std::unique_ptr<pm::PasswordFormManagerForUI>,
                             bool) override {}
  void OpenPasswordDetailsBubble(const pm::PasswordForm&) override {}
  void MaybeShowSavePasswordPrimingPromo(const url::Origin&) override {}
  std::unique_ptr<pm::PasswordCrossDomainConfirmationPopupController>
  ShowCrossDomainConfirmationPopup(const gfx::RectF&, base::i18n::TextDirection,
                                   const GURL&, const std::u16string&, bool,
                                   base::OnceClosure) override {
    return nullptr;
  }

#if BUILDFLAG(SAFE_BROWSING_AVAILABLE)
  safe_browsing::PasswordProtectionService* GetPasswordProtectionService()
      const override {
    return nullptr;
  }
#if defined(ON_FOCUS_PING_ENABLED)
  void CheckSafeBrowsingReputation(const GURL&, const GURL&) override {}
#endif
#endif

 private:
  friend class content::WebContentsUserData<PagePasswordManager>;

  PagePasswordManager(content::WebContents* contents, ProfileContext& profile)
      : content::WebContentsObserver(contents),
        content::WebContentsUserData<PagePasswordManager>(*contents),
        contents_(*contents),
        profile_(profile),
        manager_(this) {
    saving_preference_.Init(GetPrefs());
    saving_preference_.Add(
        pm::prefs::kCredentialsEnableService,
        base::BindRepeating(&PagePasswordManager::OnSavingPreferenceChanged,
                            base::Unretained(this)));
  }

  void OnSavingPreferenceChanged() {
    if (profile_->GetPasswordSavingSetting().value_or(false))
      return;
    DismissPrompt();
    manager_.DropFormManagers();
  }

  bool CanActOnPrompt(const url::Origin& origin) const {
    return web_contents() &&
           contents_->GetVisibility() == content::Visibility::VISIBLE &&
           !contents_->GetController().GetPendingEntry() &&
           IsSavingAndFillingEnabled(origin, {});
  }

  void SavePrompt() {
    if (pending_form_ &&
        CanActOnPrompt(url::Origin::Create(pending_form_->GetURL()))) {
      auto form = std::move(pending_form_);
      form->Save();
    }
  }

  void DeclinePrompt() {
    if (!pending_form_)
      return;
    auto form = std::move(pending_form_);
    if (pending_update_)
      form->OnNopeUpdateClicked();
    else
      form->OnNoInteraction(false);
  }

  void BlocklistSite() {
    if (pending_form_ &&
        CanActOnPrompt(url::Origin::Create(pending_form_->GetURL()))) {
      auto form = std::move(pending_form_);
      form->Blocklist();
    }
    DismissPrompt();
  }

  void DismissPrompt() {
    prompt_weak_.InvalidateWeakPtrs();
    if (auto form = std::move(pending_form_))
      form->OnNoInteraction(pending_update_);
    scene_.reset();
    prompt_.reset();
    delegate_.reset();
  }

  void DidStartNavigation(content::NavigationHandle* navigation) override {
    if (navigation->IsInPrimaryMainFrame() && !navigation->IsSameDocument())
      DismissPrompt();
  }

  void OnVisibilityChanged(content::Visibility visibility) override {
    if (visibility != content::Visibility::VISIBLE)
      DismissPrompt();
  }

  void WebContentsDestroyed() override { DismissPrompt(); }

  content::NavigationEntry* Entry() const {
    return contents_->GetController().GetLastCommittedEntry();
  }

  const raw_ref<content::WebContents> contents_;
  const raw_ref<ProfileContext> profile_;
  LocalCredentialPolicy policy_;
  metrics::ProfileMetricsService metrics_;
  pm::UndoPasswordChangeController undo_;
  pm::PasswordManager manager_;
  std::u16string profile_label_;
  base::RepeatingCallback<gfx::Rect()> anchor_;
  std::unique_ptr<pm::PasswordFormManagerForUI> pending_form_;
  bool pending_update_ = false;
  std::unique_ptr<views::DialogDelegate> delegate_;
  std::unique_ptr<views::Widget> prompt_;
  std::unique_ptr<prototype::PanelScene> scene_;
  PrefChangeRegistrar saving_preference_;
  base::WeakPtrFactory<PagePasswordManager> prompt_weak_{this};
  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(PagePasswordManager);

}  // namespace

void AttachPagePasswordManager(content::WebContents& contents,
                               ProfileContext& profile) {
  if (profile.IsOffTheRecord())
    return;
  PagePasswordManager::CreateForWebContents(&contents, profile);
  pm::ContentPasswordManagerDriverFactory::CreateForWebContents(
      &contents, PagePasswordManager::FromWebContents(&contents));
}

void ConfigurePagePasswordPrompt(content::WebContents& contents,
                                 std::u16string profile_label,
                                 base::RepeatingCallback<gfx::Rect()> anchor) {
  if (auto* client = PagePasswordManager::FromWebContents(&contents))
    client->ConfigurePrompt(std::move(profile_label), std::move(anchor));
}

views::Widget* GetPagePasswordPromptForTesting(content::WebContents& contents) {
  auto* client = PagePasswordManager::FromWebContents(&contents);
  return client ? client->prompt() : nullptr;
}

bool PagePasswordPromptMetalContainsForTesting(content::WebContents& contents,
                                               std::string_view text) {
  auto* client = PagePasswordManager::FromWebContents(&contents);
  return client && client->PromptMetalContains(text);
}

}  // namespace zitter::content_embedder
