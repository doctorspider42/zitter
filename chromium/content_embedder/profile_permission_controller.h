// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CONTENT_PROFILE_PERMISSION_CONTROLLER_H_
#define ZITTER_CONTENT_PROFILE_PERMISSION_CONTROLLER_H_

#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "base/time/time.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "content/public/browser/permission_controller_delegate.h"
#include "url/origin.h"

class PrefService;

namespace content { class BrowserContext; }

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
  using ClipboardReadPrompt = base::RepeatingCallback<void(
      content::RenderFrameHost&, base::OnceCallback<void(bool)>)>;
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
  content::PermissionResult GetPermissionResultForEmbeddedRequester(
      const blink::mojom::PermissionDescriptorPtr& descriptor,
      content::RenderFrameHost* frame,
      const url::Origin& requesting_origin) override;
  void ResetPermission(blink::PermissionType type,
                       const GURL& requesting_origin,
                       const GURL& embedding_origin) override;

 private:
  void NotifyClipboardPermissionChanged(std::optional<url::Origin> origin);
  void NotifyNotificationPermissionChanged(std::optional<url::Origin> origin);

  std::unique_ptr<PrefService> site_permission_prefs_;
  base::DictValue private_clipboard_rules_;
  base::DictValue private_popup_rules_;
  bool private_popups_allowed_ = false;
  const raw_ref<content::BrowserContext> context_;
  std::optional<url::Origin> storage_probe_origin_;
  ClipboardReadPrompt clipboard_read_prompt_;
  ClipboardReadPrompt notification_prompt_;
  base::WeakPtrFactory<ProfilePermissionController> weak_factory_{this};
};

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_PROFILE_PERMISSION_CONTROLLER_H_
