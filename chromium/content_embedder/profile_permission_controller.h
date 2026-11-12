// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CONTENT_PROFILE_PERMISSION_CONTROLLER_H_
#define ZITTER_CONTENT_PROFILE_PERMISSION_CONTROLLER_H_

#include <memory>
#include <optional>
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
  bool SetClipboardSetting(const url::Origin& origin, ClipboardSetting setting);
  bool HasClipboardReadGrant(content::RenderFrameHost* frame);
  using ClipboardReadPrompt = base::RepeatingCallback<void(
      content::RenderFrameHost&, base::OnceCallback<void(bool)>)>;
  void SetClipboardReadPrompt(ClipboardReadPrompt prompt);

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
  void NotifyClipboardPermissionChanged(const url::Origin& origin);

  std::unique_ptr<PrefService> site_permission_prefs_;
  base::DictValue private_clipboard_rules_;
  base::DictValue private_popup_rules_;
  bool private_popups_allowed_ = false;
  const raw_ref<content::BrowserContext> context_;
  std::optional<url::Origin> storage_probe_origin_;
  ClipboardReadPrompt clipboard_read_prompt_;
  base::WeakPtrFactory<ProfilePermissionController> weak_factory_{this};
};

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_PROFILE_PERMISSION_CONTROLLER_H_
