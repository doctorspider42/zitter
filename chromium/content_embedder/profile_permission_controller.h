// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CONTENT_PROFILE_PERMISSION_CONTROLLER_H_
#define ZITTER_CONTENT_PROFILE_PERMISSION_CONTROLLER_H_

#include <map>
#include <memory>
#include <set>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/function_ref.h"
#include "base/time/time.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "chromium/prototype/user_agent_policy.h"
#include "content/public/browser/permission_controller_delegate.h"
#include "content/public/browser/weak_document_ptr.h"
#include "third_party/blink/public/common/permissions/permission_utils.h"
#include "url/origin.h"

class PrefService;

namespace content { class BrowserContext; }
namespace custom_handlers { class ProtocolHandlerRegistry; }

namespace zitter::content_embedder {

// The embedder owns permission decisions per browser context. Capabilities
// without a host flow stay denied.
class ProfilePermissionController final
    : public content::PermissionControllerDelegate {
 public:
  ProfilePermissionController(content::BrowserContext& context,
                              std::optional<url::Origin> storage_probe_origin);
  ~ProfilePermissionController() override;

  enum class ClipboardSetting { kAllow = 1, kBlock = 2, kAsk = 3 };
  static constexpr char kClipboardPrefs[] = "zitter.clipboard_permissions";
  static constexpr char kClipboardRuleTimes[] = "zitter.clipboard_permission_times";
  static constexpr size_t kMaxClipboardRules = 256;
  enum class PopupSetting { kAllow = 1, kBlock = 2 };
  static constexpr char kPopupPrefs[] = "zitter.popup_permissions";
  static constexpr char kPopupDefaultPref[] = "zitter.popups_allowed";
  static constexpr char kPopupRuleTimes[] = "zitter.popup_permission_times";
  static constexpr size_t kMaxPopupRules = 256;

  static constexpr char kExternalProtocolPrefs[] = "zitter.external_protocol_permissions";
  static constexpr char kExternalProtocolRuleTimes[] = "zitter.external_protocol_permission_times";
  bool CanRememberExternalProtocol(const url::Origin& origin) const;
  bool IsExternalProtocolAllowed(const url::Origin& origin,
                                 std::string_view scheme) const;
  std::vector<std::string> AllowedExternalProtocols(const url::Origin& origin) const;
  bool ForgetExternalProtocol(const url::Origin& origin, std::string_view scheme);
  void AllowExternalProtocol(const url::Origin& origin, std::string_view scheme);

  static constexpr char kPromptDefaults[] = "zitter.permission_prompt_defaults";
  static constexpr char kMediaRules[] = "zitter.media_permissions";
  static constexpr char kMediaRuleTimes[] = "zitter.media_permission_times";
  bool PromptByDefault(std::string_view key) const;
  bool SetPromptByDefault(std::string_view key, bool prompt);
  // Camera ("camera") and microphone ("microphone"): 1 allow, 2 block, 3 ask.
  int MediaSetting(const url::Origin& origin, std::string_view key) const;
  bool SetMediaSetting(const url::Origin& origin, std::string_view key, int setting);

  static bool IsPopupOriginEligible(const url::Origin& origin);
  bool GetPopupDefault() const;
  bool SetPopupDefault(bool allowed);
  PopupSetting GetPopupSetting(const url::Origin& origin) const;
  bool SetPopupSetting(const url::Origin& origin, std::optional<PopupSetting> setting);
  void InheritPrivatePopupSettings(const ProfilePermissionController& source);

  static bool IsClipboardOriginEligible(const url::Origin& origin);
  void SetSitePermissionPreferences(std::unique_ptr<PrefService> prefs);
  bool CanEditSitePermissions() const;
  bool CanClearSitePermissions() const;
  void ClearSitePermissions(base::Time begin, base::Time end);
  ClipboardSetting GetClipboardSetting(const url::Origin& origin) const;
  bool SetClipboardSetting(const url::Origin& origin, std::optional<ClipboardSetting> setting);
  bool HasClipboardReadGrant(content::RenderFrameHost* frame);
  // Prompt answers: (allowed, remember). "remember" is the prompt's
  // "Zawsze zezwalaj tej witrynie" checkbox; without it an allowed
  // permission lasts while the page that asked stays open.
  using ClipboardReadPrompt = base::RepeatingCallback<void(
      content::RenderFrameHost&, base::OnceCallback<void(bool, bool)>)>;
  void SetClipboardReadPrompt(ClipboardReadPrompt prompt);
  static constexpr char kNotificationPrefs[] = "zitter.notification_permissions";
  static constexpr char kNotificationRuleTimes[] = "zitter.notification_permission_times";
  static constexpr char kNotificationNextId[] = "zitter.notification_next_id";
  bool CanUseNotifications(const url::Origin& origin) const;
  int NotificationSetting(const url::Origin& origin) const;
  bool SetNotificationSetting(const url::Origin& origin, int setting);
  int64_t NextNotificationId();
  static constexpr char kSpellingPrefs[] = "zitter.spelling";
  bool SpellingEnabled() const;
  std::vector<std::u16string> SpellingDictionaries() const;
  std::u16string ActiveSpellingDictionary() const;
  bool SetSpellingEnabled(bool enabled);
  bool SetSpellingDictionary(const std::u16string& code, bool add);
  bool SelectSpellingDictionary(const std::u16string& code);
  void SetNotificationPrompt(ClipboardReadPrompt prompt);

  // Settings > Site permissions for the web APIs beyond camera, microphone,
  // notifications, clipboard and pop-ups. Settings: 1 allow, 2 block, 3 ask.
  static constexpr char kSiteDefaults[] = "zitter.site_defaults";
  static constexpr char kSiteRules[] = "zitter.site_rules";
  static constexpr char kSiteRuleTimes[] = "zitter.site_rule_times";
  struct SiteCategory {
    std::string_view key;
    blink::PermissionType type;
    int fallback;
    bool can_ask;
    bool can_allow = true;
  };
  static const SiteCategory* FindSiteCategory(std::string_view key);
  static const SiteCategory* FindSiteCategory(blink::PermissionType type);
  static std::vector<std::string_view> SiteCategoryKeys();
  int SiteDefault(std::string_view key) const;
  bool SetSiteDefault(std::string_view key, int setting);
  int SiteSetting(std::string_view key, const url::Origin& origin) const;
  bool SetSiteRule(std::string_view key, const url::Origin& origin,
                   std::optional<int> setting);
  // Devices a site may use (WebUSB, WebHID, Web Serial, Web Bluetooth) and
  // files it may edit, by API ("usb", "hid", "serial", "bluetooth") and a
  // device key. Devices without a stable key stay granted until exit.
  static constexpr char kDeviceGrants[] = "zitter.device_grants";
  bool HasDeviceGrant(std::string_view api, const url::Origin& origin,
                      const std::string& key) const;
  void GrantDevice(std::string_view api, const url::Origin& origin,
                   const std::string& key, const std::string& name, bool persistent);
  bool RevokeDevice(std::string_view api, const url::Origin& origin,
                    const std::string& key);
  std::vector<std::string> DeviceGrants(std::string_view api,
                                        const url::Origin& origin) const;
  // What an API keeps with a persistent grant besides its name (Web
  // Bluetooth's device ID and allowed services). Revoking the grant drops it.
  static constexpr char kDeviceGrantDetails[] = "zitter.device_grant_details";
  void SetDeviceGrantDetails(std::string_view api, const url::Origin& origin,
                             const std::string& key, base::DictValue details);
  const base::DictValue* DeviceGrantDetails(std::string_view api, const url::Origin& origin,
                                            const std::string& key) const;
  // `offer_remember` shows the "always allow" checkbox.
  using SitePrompt = base::RepeatingCallback<void(
      content::RenderFrameHost&, std::string_view key, bool offer_remember,
      base::OnceCallback<void(bool, bool)>)>;
  void SetSitePrompt(SitePrompt prompt);
  // Asks the user through the Zitter prompt; answers false without one.
  void RunSitePrompt(content::RenderFrameHost& frame, std::string_view key,
                     base::OnceCallback<void(bool)> answer);
  // The same with the "always allow" checkbox: answers (allowed, remember).
  void RunSitePrompt(content::RenderFrameHost& frame, std::string_view key,
                     base::OnceCallback<void(bool, bool)> answer);

  // Allows `key` ("camera", "microphone", "clipboard", "notifications" or a
  // site category) for `origin` while the frame's document lives. The
  // settings above read it as allowed; changing a setting drops it.
  void GrantForDocument(content::RenderFrameHost& frame, const url::Origin& origin,
                        std::string_view key);

  // navigator.registerProtocolHandler(): the handlers people accepted, kept
  // in the site permission store. Null without that store (private sessions).
  custom_handlers::ProtocolHandlerRegistry* protocol_handlers() {
    return protocol_handlers_.get();
  }
  // FedCM state kept with the site permissions: sharing grants per relying
  // party, identity provider and account, identity provider sign-in status
  // and embargo. Private profiles and unwritable preferences keep it for the
  // session only.
  static constexpr char kFederatedIdentity[] = "zitter.federated_identity";
  const base::DictValue& FederatedIdentityState() const;
  void UpdateFederatedIdentityState(base::FunctionRef<void(base::DictValue&)> update);

  // Profile browsing preferences from Settings (privacy, content and
  // readability, languages, downloads). Unknown keys and invalid values read
  // as the defaults; the Set functions reject them.
  static constexpr char kBrowsingPrefs[] = "zitter.browsing";
  static bool IsBrowsingFlag(std::string_view key);
  static bool IsBrowsingNumber(std::string_view key);
  static bool IsBrowsingText(std::string_view key);
  bool BrowsingFlag(std::string_view key) const;
  int BrowsingNumber(std::string_view key) const;
  std::string BrowsingText(std::string_view key) const;
  bool SetBrowsingFlag(std::string_view key, bool value);
  bool SetBrowsingNumber(std::string_view key, int value);
  bool SetBrowsingText(std::string_view key, std::string value);
  base::CallbackListSubscription ObserveBrowsingPreferences(base::RepeatingClosure callback);

  // User-Agent settings (prototype/user_agent_policy.h). Changes notify the
  // browsing preference observers.
  static constexpr char kUserAgentPrefs[] = "zitter.user_agent";
  prototype::UserAgentSettings UserAgent() const;
  bool SetUserAgent(const prototype::UserAgentSettings& settings);

  void OnPermissionStatusChangeSubscriptionAdded(
      content::PermissionController::SubscriptionId id) override;
  void RequestPermissionsFromCurrentDocument(
      content::RenderFrameHost* frame,
      const content::PermissionRequestDescription& request,
      base::OnceCallback<void(const std::vector<content::PermissionResult>&)>
          callback) override;
  blink::mojom::PermissionStatus GetPermissionStatus(
      const blink::mojom::PermissionDescriptorPtr& descriptor,
      const GURL& requesting_origin,
      const GURL& embedding_origin) override;
  content::PermissionResult GetPermissionResultForOriginWithoutContext(
      const blink::mojom::PermissionDescriptorPtr& descriptor,
      const url::Origin& requesting_origin,
      const url::Origin& embedding_origin) override;
  content::PermissionResult GetPermissionResultForCurrentDocument(
      const blink::mojom::PermissionDescriptorPtr& descriptor,
      content::RenderFrameHost* frame,
      bool should_include_device_status) override;
  content::PermissionResult GetPermissionResultForWorker(
      const blink::mojom::PermissionDescriptorPtr& descriptor,
      content::RenderProcessHost* process,
      const GURL& worker_origin) override;
  void ResetPermission(blink::PermissionType type,
                       const GURL& requesting_origin,
                       const GURL& embedding_origin) override;

 private:
  void NotifyClipboardPermissionChanged(std::optional<url::Origin> origin);
  void NotifyNotificationPermissionChanged(std::optional<url::Origin> origin);

  std::unique_ptr<PrefService> site_permission_prefs_;
  // Uses `site_permission_prefs_`; declared after it to be destroyed first.
  std::unique_ptr<custom_handlers::ProtocolHandlerRegistry> protocol_handlers_;
  base::RepeatingClosureList browsing_observers_;
  SitePrompt site_prompt_;
  // "api|origin" -> device keys granted for this session only.
  std::map<std::string, std::set<std::string>> session_device_grants_;
  content::PermissionResult SiteResult(const SiteCategory& category,
                                       const url::Origin& origin) const;
  base::DictValue private_clipboard_rules_;
  base::DictValue session_federated_identity_;
  base::DictValue private_popup_rules_;
  bool private_popups_allowed_ = false;
  const raw_ref<content::BrowserContext> context_;
  std::optional<url::Origin> storage_probe_origin_;
  ClipboardReadPrompt clipboard_read_prompt_;
  ClipboardReadPrompt notification_prompt_;
  bool HasDocumentGrant(const url::Origin& origin, std::string_view key) const;
  void DropDocumentGrants(const url::Origin& origin, std::string_view key);
  struct DocumentGrant {
    content::WeakDocumentPtr document;
    url::Origin origin;
    std::string key;
  };
  mutable std::vector<DocumentGrant> document_grants_;
  base::WeakPtrFactory<ProfilePermissionController> weak_factory_{this};
};

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_PROFILE_PERMISSION_CONTROLLER_H_
