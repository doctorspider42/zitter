// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "profile_permission_controller.h"

#include <utility>
#include <vector>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/permission_request_description.h"
#include "content/public/browser/permission_status_subscription.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/weak_document_ptr.h"
#include "content/public/browser/web_contents.h"
#include "extension_registry_probe.h"
#include "services/network/public/cpp/is_potentially_trustworthy.h"
#include "services/network/public/mojom/permissions_policy/permissions_policy_feature.mojom.h"
#include "third_party/blink/public/common/permissions/permission_utils.h"
#include "third_party/blink/public/mojom/permissions/permission.mojom.h"
#include "url/gurl.h"

namespace zitter::content_embedder {

using content::PermissionResult;
using content::PermissionStatus;

namespace {

bool IsNotification(const blink::mojom::PermissionDescriptorPtr& descriptor) {
  return descriptor && descriptor->name == blink::mojom::PermissionName::NOTIFICATIONS;
}

PermissionResult NotificationResult(int setting) {
  if (setting == 1)
    return PermissionResult(PermissionStatus::GRANTED);
  return PermissionResult(setting == 3 ? PermissionStatus::ASK : PermissionStatus::DENIED);
}

bool IsClipboardWriteDescriptor(
    const blink::mojom::PermissionDescriptorPtr& descriptor) {
  return descriptor &&
         descriptor->name == blink::mojom::PermissionName::CLIPBOARD_WRITE &&
         descriptor->extension && descriptor->extension->is_clipboard() &&
         descriptor->extension->get_clipboard()->will_be_sanitized;
}

bool IsClipboardReadDescriptor(
    const blink::mojom::PermissionDescriptorPtr& descriptor) {
  return descriptor &&
         descriptor->name == blink::mojom::PermissionName::CLIPBOARD_READ;
}

bool IsTrustworthyMainDocument(content::RenderFrameHost* frame,
                               content::BrowserContext* context) {
  return frame && frame->GetBrowserContext() == context && frame->IsActive() &&
         frame->IsInPrimaryMainFrame() &&
         ProfilePermissionController::IsClipboardOriginEligible(
             frame->GetLastCommittedOrigin());
}

bool HasClipboardFocus(content::RenderFrameHost* frame) {
  auto* contents = content::WebContents::FromRenderFrameHost(frame);
  return frame->IsFocused() && contents &&
         contents->GetVisibility() == content::Visibility::VISIBLE;
}

bool CanUseExtensionClipboard(
    const blink::mojom::PermissionDescriptorPtr& descriptor,
    content::RenderFrameHost* frame,
    content::BrowserContext* context) {
  if (!frame || frame->GetBrowserContext() != context || !HasClipboardFocus(frame))
    return false;
  if (IsClipboardReadDescriptor(descriptor)) {
    return HasWorkspaceExtensionClipboardPermission(frame, ExtensionClipboardAccess::kRead);
  }
  return IsClipboardWriteDescriptor(descriptor) &&
         HasWorkspaceExtensionClipboardPermission(frame, ExtensionClipboardAccess::kWrite);
}

bool CanReadClipboard(content::RenderFrameHost* frame,
                      content::BrowserContext* context) {
  return IsTrustworthyMainDocument(frame, context) && HasClipboardFocus(frame) &&
         frame->IsFeatureEnabled(
             network::mojom::PermissionsPolicyFeature::kClipboardRead);
}

bool WriteSiteSetting(PrefService* prefs, const char* pref, const char* times_pref,
                        base::DictValue& private_rules, const std::string& key,
                        std::optional<int> setting, size_t max_rules) {
  auto update = [&](base::DictValue& rules) {
    if (!setting) {
      rules.Remove(key);
      return true;
    }
    if (!rules.contains(key) && rules.size() >= max_rules)
      return false;
    rules.Set(key, *setting);
    return true;
  };
  if (!prefs)
    return update(private_rules);
  {
    ScopedDictPrefUpdate rules(prefs, pref);
    if (!update(*rules))
      return false;
    ScopedDictPrefUpdate times(prefs, times_pref);
    if (setting)
      times->Set(key, base::NumberToString(base::Time::Now().ToInternalValue()));
    else
      times->Remove(key);
  }
  prefs->CommitPendingWrite();
  return true;
}

}  // namespace

ProfilePermissionController::ProfilePermissionController(
    content::BrowserContext& context,
    std::optional<url::Origin> storage_probe_origin)
    : context_(context), storage_probe_origin_(std::move(storage_probe_origin)) {
  if (storage_probe_origin_) {
    CHECK(!context_->IsOffTheRecord());
    CHECK(!storage_probe_origin_->opaque());
    CHECK_EQ(storage_probe_origin_->scheme(), "http");
    CHECK_EQ(storage_probe_origin_->host(), "127.0.0.1");
  }
}

ProfilePermissionController::~ProfilePermissionController() = default;

bool ProfilePermissionController::IsClipboardOriginEligible(
    const url::Origin& origin) {
  return !origin.opaque() && origin.GetURL().SchemeIsHTTPOrHTTPS() &&
         network::IsUrlPotentiallyTrustworthy(origin.GetURL());
}

void ProfilePermissionController::SetSitePermissionPreferences(
    std::unique_ptr<PrefService> prefs) {
  CHECK(prefs && !site_permission_prefs_ && !context_->IsOffTheRecord());
  site_permission_prefs_ = std::move(prefs);
}

bool ProfilePermissionController::CanEditSitePermissions() const {
  return site_permission_prefs_ || context_->IsOffTheRecord();
}

bool ProfilePermissionController::CanClearSitePermissions() const {
  if (!site_permission_prefs_ || site_permission_prefs_->ReadOnly())
    return false;
  const auto status = site_permission_prefs_->GetInitializationStatus();
  return status == PrefService::INITIALIZATION_STATUS_SUCCESS ||
         status == PrefService::INITIALIZATION_STATUS_CREATED_NEW_PREF_STORE;
}

void ProfilePermissionController::ClearSitePermissions(base::Time begin,
                                                       base::Time end) {
  CHECK(CanClearSitePermissions());
  auto clear = [&](const char* rules_pref, const char* times_pref) {
    std::vector<std::string> removed;
    ScopedDictPrefUpdate rules(site_permission_prefs_.get(), rules_pref);
    ScopedDictPrefUpdate times(site_permission_prefs_.get(), times_pref);
    for (const auto [key, rule] : *rules) {
      int64_t value = 0;
      if (const auto* timestamp = times->FindString(key);
          timestamp && !base::StringToInt64(*timestamp, &value)) {
        value = 0;
      }
      const auto modified = base::Time::FromInternalValue(value);
      if (modified >= begin && modified < end)
        removed.push_back(key);
    }
    for (const auto& key : removed) {
      rules->Remove(key);
      times->Remove(key);
    }
    return removed;
  };
  const auto clipboard = clear(kClipboardPrefs, kClipboardRuleTimes);
  const auto notifications = clear(kNotificationPrefs, kNotificationRuleTimes);
  clear(kPopupPrefs, kPopupRuleTimes);
  clear(kMediaRules, kMediaRuleTimes);
  clear(kExternalProtocolPrefs, kExternalProtocolRuleTimes);
  site_permission_prefs_->CommitPendingWrite();
  for (const auto& key : clipboard)
    NotifyClipboardPermissionChanged(url::Origin::Create(GURL(key)));
  for (const auto& key : notifications)
    NotifyNotificationPermissionChanged(url::Origin::Create(GURL(key)));
}

bool ProfilePermissionController::PromptByDefault(std::string_view key) const {
  return !site_permission_prefs_ ||
         site_permission_prefs_->GetDict(kPromptDefaults).FindBool(key).value_or(true);
}

bool ProfilePermissionController::SetPromptByDefault(std::string_view key,
                                                    bool prompt) {
  if (!CanClearSitePermissions() ||
      (key != "clipboard" && key != "camera" && key != "microphone" &&
       key != "notifications"))
    return false;
  {
    ScopedDictPrefUpdate update(site_permission_prefs_.get(), kPromptDefaults);
    update->Set(key, prompt);
  }
  site_permission_prefs_->CommitPendingWrite();
  if (key == "clipboard")
    NotifyClipboardPermissionChanged(std::nullopt);
  if (key == "notifications")
    NotifyNotificationPermissionChanged(std::nullopt);
  return true;
}

bool ProfilePermissionController::CanUseNotifications(const url::Origin& origin) const {
  return CanClearSitePermissions() && IsPopupOriginEligible(origin) &&
         IsClipboardOriginEligible(origin) && context_->GetPlatformNotificationService();
}

int ProfilePermissionController::NotificationSetting(const url::Origin& origin) const {
  if (!CanUseNotifications(origin))
    return 2;
  const auto value = site_permission_prefs_->GetDict(kNotificationPrefs)
                         .FindInt(origin.Serialize());
  if (value == 1 || value == 2 || value == 3)
    return *value;
  return PromptByDefault("notifications") ? 3 : 2;
}

bool ProfilePermissionController::SetNotificationSetting(const url::Origin& origin,
                                                          int setting) {
  if (!CanUseNotifications(origin) || setting < 0 || setting > 3)
    return false;
  base::DictValue unused_private_rules;
  if (!WriteSiteSetting(site_permission_prefs_.get(), kNotificationPrefs,
                        kNotificationRuleTimes, unused_private_rules,
                        origin.Serialize(), setting ? std::optional(setting) : std::nullopt,
                        256)) {
    return false;
  }
  NotifyNotificationPermissionChanged(origin);
  return true;
}

int64_t ProfilePermissionController::NextNotificationId() {
  const auto next = site_permission_prefs_->GetInt64(kNotificationNextId) + 1;
  site_permission_prefs_->SetInt64(kNotificationNextId, next);
  return next;
}

void ProfilePermissionController::SetNotificationPrompt(ClipboardReadPrompt prompt) {
  notification_prompt_ = std::move(prompt);
}

int ProfilePermissionController::MediaSetting(const url::Origin& origin,
                                               std::string_view key) const {
  if (site_permission_prefs_) {
    const auto rule = site_permission_prefs_->GetDict(kMediaRules)
        .FindInt(origin.Serialize() + " " + std::string(key));
    if (rule == 2 || rule == 3)
      return *rule;
  }
  return PromptByDefault(key) ? 3 : 2;
}

bool ProfilePermissionController::SetMediaSetting(
    const url::Origin& origin, std::string_view key, int setting) {
  if (!CanClearSitePermissions() || !IsPopupOriginEligible(origin) ||
      (key != "camera" && key != "microphone") ||
      (setting != 0 && setting != 2 && setting != 3))
    return false;
  base::DictValue unused_private_rules;
  return WriteSiteSetting(site_permission_prefs_.get(), kMediaRules, kMediaRuleTimes,
                          unused_private_rules, origin.Serialize() + " " + std::string(key),
                          setting ? std::optional(setting) : std::nullopt, 256);
}

ProfilePermissionController::ClipboardSetting
ProfilePermissionController::GetClipboardSetting(const url::Origin& origin) const {
  const auto& rules = site_permission_prefs_ ? site_permission_prefs_->GetDict(kClipboardPrefs)
                                       : private_clipboard_rules_;
  const auto value = rules.FindInt(origin.Serialize());
  if (value == static_cast<int>(ClipboardSetting::kAllow))
    return ClipboardSetting::kAllow;
  if (value == static_cast<int>(ClipboardSetting::kBlock))
    return ClipboardSetting::kBlock;
  if (value == static_cast<int>(ClipboardSetting::kAsk) || PromptByDefault("clipboard"))
    return ClipboardSetting::kAsk;
  return ClipboardSetting::kBlock;
}

bool ProfilePermissionController::SetClipboardSetting(
    const url::Origin& origin, std::optional<ClipboardSetting> setting) {
  if (!CanEditSitePermissions() || !IsClipboardOriginEligible(origin) ||
      (setting && *setting != ClipboardSetting::kAllow &&
       *setting != ClipboardSetting::kBlock && *setting != ClipboardSetting::kAsk))
    return false;
  const auto stored = setting ? std::optional(static_cast<int>(*setting)) : std::nullopt;
  if (!WriteSiteSetting(site_permission_prefs_.get(), kClipboardPrefs, kClipboardRuleTimes,
                          private_clipboard_rules_, origin.Serialize(), stored, kMaxClipboardRules)) {
    return false;
  }
  NotifyClipboardPermissionChanged(origin);
  return true;
}

bool ProfilePermissionController::IsPopupOriginEligible(const url::Origin& origin) {
  return !origin.opaque() && origin.GetURL().SchemeIsHTTPOrHTTPS() &&
         origin.Serialize().size() <= 512;
}

bool ProfilePermissionController::GetPopupDefault() const {
  return site_permission_prefs_ ? site_permission_prefs_->GetBoolean(kPopupDefaultPref)
                                : private_popups_allowed_;
}

bool ProfilePermissionController::SetPopupDefault(bool allowed) {
  if (!CanEditSitePermissions())
    return false;
  if (site_permission_prefs_) {
    site_permission_prefs_->SetBoolean(kPopupDefaultPref, allowed);
    site_permission_prefs_->CommitPendingWrite();
  } else {
    private_popups_allowed_ = allowed;
  }
  return true;
}

ProfilePermissionController::PopupSetting
ProfilePermissionController::GetPopupSetting(const url::Origin& origin) const {
  if (!IsPopupOriginEligible(origin))
    return PopupSetting::kBlock;
  const auto& rules = site_permission_prefs_
                          ? site_permission_prefs_->GetDict(kPopupPrefs)
                          : private_popup_rules_;
  if (const auto setting = rules.FindInt(origin.Serialize()))
    return static_cast<PopupSetting>(*setting);
  return GetPopupDefault() ? PopupSetting::kAllow : PopupSetting::kBlock;
}

bool ProfilePermissionController::CanRememberExternalProtocol(
    const url::Origin& origin) const {
  return !context_->IsOffTheRecord() && CanClearSitePermissions() &&
         origin.GetURL().SchemeIsHTTPOrHTTPS() &&
         network::IsOriginPotentiallyTrustworthy(origin);
}

bool ProfilePermissionController::IsExternalProtocolAllowed(
    const url::Origin& origin, std::string_view scheme) const {
  return CanRememberExternalProtocol(origin) &&
         site_permission_prefs_->GetDict(kExternalProtocolPrefs)
             .FindInt(origin.Serialize() + " " + std::string(scheme)) == 1;
}

std::vector<std::string> ProfilePermissionController::AllowedExternalProtocols(
    const url::Origin& origin) const {
  std::vector<std::string> schemes;
  if (!CanRememberExternalProtocol(origin))
    return schemes;
  const auto prefix = origin.Serialize() + " ";
  for (const auto [key, value] : site_permission_prefs_->GetDict(kExternalProtocolPrefs)) {
    if (key.starts_with(prefix) && value.is_int() && value.GetInt() == 1) {
      const auto scheme = key.substr(prefix.size());
      if (scheme.size() <= 64 && GURL(scheme + ":").scheme() == scheme)
        schemes.push_back(scheme);
    }
  }
  return schemes;
}

bool ProfilePermissionController::ForgetExternalProtocol(
    const url::Origin& origin, std::string_view scheme) {
  if (!IsExternalProtocolAllowed(origin, scheme))
    return false;
  base::DictValue unused_private_rules;
  return WriteSiteSetting(site_permission_prefs_.get(), kExternalProtocolPrefs,
                          kExternalProtocolRuleTimes, unused_private_rules,
                          origin.Serialize() + " " + std::string(scheme),
                          std::nullopt, 256);
}

void ProfilePermissionController::AllowExternalProtocol(
    const url::Origin& origin, std::string_view scheme) {
  if (!CanRememberExternalProtocol(origin))
    return;
  base::DictValue unused_private_rules;
  WriteSiteSetting(site_permission_prefs_.get(), kExternalProtocolPrefs,
                   kExternalProtocolRuleTimes, unused_private_rules,
                   origin.Serialize() + " " + std::string(scheme), 1, 256);
}

bool ProfilePermissionController::SetPopupSetting(
    const url::Origin& origin, std::optional<PopupSetting> setting) {
  if (!CanEditSitePermissions() || !IsPopupOriginEligible(origin) ||
      (setting && *setting != PopupSetting::kAllow && *setting != PopupSetting::kBlock)) {
    return false;
  }
  return WriteSiteSetting(
      site_permission_prefs_.get(), kPopupPrefs, kPopupRuleTimes,
      private_popup_rules_, origin.Serialize(),
      setting ? std::make_optional(static_cast<int>(*setting)) : std::nullopt,
      kMaxPopupRules);
}

void ProfilePermissionController::InheritPrivatePopupSettings(
    const ProfilePermissionController& source) {
  CHECK(context_->IsOffTheRecord());
  private_popups_allowed_ = source.GetPopupDefault();
  private_popup_rules_ = source.site_permission_prefs_
      ? source.site_permission_prefs_->GetDict(kPopupPrefs).Clone()
      : source.private_popup_rules_.Clone();
}

bool ProfilePermissionController::HasClipboardReadGrant(
    content::RenderFrameHost* frame) {
  return CanReadClipboard(frame, &*context_) &&
         GetClipboardSetting(frame->GetLastCommittedOrigin()) ==
             ClipboardSetting::kAllow;
}

void ProfilePermissionController::SetClipboardReadPrompt(ClipboardReadPrompt prompt) {
  clipboard_read_prompt_ = std::move(prompt);
}

void ProfilePermissionController::OnPermissionStatusChangeSubscriptionAdded(
    content::PermissionController::SubscriptionId id) {
  auto* subscription = subscriptions() ? subscriptions()->Lookup(id) : nullptr;
  if (subscription && IsNotification(subscription->permission_descriptor)) {
    auto* frame = content::RenderFrameHost::FromID(subscription->render_process_id,
                                                  subscription->render_frame_id);
    subscription->permission_result = frame
        ? GetPermissionResultForCurrentDocument(subscription->permission_descriptor, frame,
                                                subscription->should_include_device_status)
        : GetPermissionResultForOriginWithoutContext(
              subscription->permission_descriptor,
              url::Origin::Create(subscription->requesting_origin),
              url::Origin::Create(subscription->embedding_origin));
    return;
  }
  if (!subscription || (!IsClipboardReadDescriptor(subscription->permission_descriptor) &&
                        !IsClipboardWriteDescriptor(subscription->permission_descriptor))) {
    return;
  }
  auto* frame = content::RenderFrameHost::FromID(subscription->render_process_id,
                                                subscription->render_frame_id);
  subscription->permission_result = GetPermissionResultForCurrentDocument(
      subscription->permission_descriptor, frame,
      subscription->should_include_device_status);
}

void ProfilePermissionController::NotifyClipboardPermissionChanged(
    std::optional<url::Origin> origin) {
  if (!subscriptions())
    return;
  std::vector<base::OnceClosure> callbacks;
  for (content::PermissionController::SubscriptionsMap::iterator iter(subscriptions());
       !iter.IsAtEnd(); iter.Advance()) {
    auto* subscription = iter.GetCurrentValue();
    if (!subscription || !subscription->permission_result ||
        (!IsClipboardReadDescriptor(subscription->permission_descriptor) &&
         !IsClipboardWriteDescriptor(subscription->permission_descriptor))) {
      continue;
    }
    auto* frame = content::RenderFrameHost::FromID(subscription->render_process_id,
                                                  subscription->render_frame_id);
    if (!frame || (origin && frame->GetLastCommittedOrigin() != *origin))
      continue;
    auto result = GetPermissionResultForCurrentDocument(
        subscription->permission_descriptor, frame,
        subscription->should_include_device_status);
    if (subscription->permission_result->status == result.status)
      continue;
    subscription->permission_result = result;
    callbacks.push_back(base::BindOnce(subscription->callback, result, false));
  }
  for (auto& callback : callbacks)
    std::move(callback).Run();
}

void ProfilePermissionController::NotifyNotificationPermissionChanged(
    std::optional<url::Origin> origin) {
  if (!subscriptions())
    return;
  std::vector<base::OnceClosure> callbacks;
  for (content::PermissionController::SubscriptionsMap::iterator iter(subscriptions());
       !iter.IsAtEnd(); iter.Advance()) {
    auto* subscription = iter.GetCurrentValue();
    if (!subscription || !subscription->permission_result ||
        !IsNotification(subscription->permission_descriptor)) {
      continue;
    }
    const auto requester = url::Origin::Create(subscription->requesting_origin);
    if (origin && requester != *origin)
      continue;
    auto* frame = content::RenderFrameHost::FromID(subscription->render_process_id,
                                                  subscription->render_frame_id);
    auto result = frame
        ? GetPermissionResultForCurrentDocument(subscription->permission_descriptor, frame,
                                                subscription->should_include_device_status)
        : GetPermissionResultForOriginWithoutContext(
              subscription->permission_descriptor, requester,
              url::Origin::Create(subscription->embedding_origin));
    if (subscription->permission_result->status == result.status)
      continue;
    subscription->permission_result = result;
    callbacks.push_back(base::BindOnce(subscription->callback, result, false));
  }
  for (auto& callback : callbacks)
    std::move(callback).Run();
}

void ProfilePermissionController::RequestPermissionsFromCurrentDocument(
    content::RenderFrameHost* frame,
    const content::PermissionRequestDescription& request,
    base::OnceCallback<void(const std::vector<PermissionResult>&)> callback) {
  std::vector<PermissionResult> results;
  results.reserve(request.permissions.size());
  const bool is_document_request =
      frame && !request.embedded_permission_request_descriptor &&
      url::Origin::Create(request.requesting_origin) ==
          frame->GetLastCommittedOrigin();
  if (is_document_request && request.permissions.size() == 1 &&
      IsNotification(request.permissions.front()) && request.user_gesture &&
      notification_prompt_ && IsTrustworthyMainDocument(frame, &*context_) &&
      HasClipboardFocus(frame) && NotificationSetting(frame->GetLastCommittedOrigin()) == 3) {
    notification_prompt_.Run(
        *frame, base::BindOnce(
            [](base::WeakPtr<ProfilePermissionController> self,
               content::WeakDocumentPtr document,
               base::OnceCallback<void(const std::vector<PermissionResult>&)> reply,
               bool accepted) {
              auto* current = document.AsRenderFrameHostIfValid();
              auto* contents = current ? content::WebContents::FromRenderFrameHost(current) : nullptr;
              if (!self || !IsTrustworthyMainDocument(current, &*self->context_) ||
                  !contents || contents->GetVisibility() != content::Visibility::VISIBLE) {
                std::move(reply).Run({PermissionResult(PermissionStatus::DENIED)});
                return;
              }
              const auto origin = current->GetLastCommittedOrigin();
              if (accepted && self->NotificationSetting(origin) == 3)
                self->SetNotificationSetting(origin, 1);
              std::move(reply).Run({NotificationResult(self->NotificationSetting(origin))});
            },
            weak_factory_.GetWeakPtr(), frame->GetWeakDocumentPtr(), std::move(callback)));
    return;
  }
  if (is_document_request && request.permissions.size() == 1 &&
      IsClipboardReadDescriptor(request.permissions.front()) &&
      request.user_gesture &&
      clipboard_read_prompt_ && CanEditSitePermissions() &&
      CanReadClipboard(frame, &*context_) &&
      GetClipboardSetting(frame->GetLastCommittedOrigin()) == ClipboardSetting::kAsk &&
      frame->ConsumeTransientUserActivation()) {
    clipboard_read_prompt_.Run(
        *frame, base::BindOnce(
            [](base::WeakPtr<ProfilePermissionController> self,
               const content::WeakDocumentPtr& document,
               base::OnceCallback<void(const std::vector<PermissionResult>&)> reply,
               bool accepted) {
              auto* current = document.AsRenderFrameHostIfValid();
              bool allowed = false;
              auto* contents = current
                  ? content::WebContents::FromRenderFrameHost(current)
                  : nullptr;
              if (accepted && self && contents &&
                  contents->GetVisibility() == content::Visibility::VISIBLE &&
                  IsTrustworthyMainDocument(current, &*self->context_) &&
                  current->IsFeatureEnabled(
                      network::mojom::PermissionsPolicyFeature::kClipboardRead)) {
                const auto& origin = current->GetLastCommittedOrigin();
                const auto setting = self->GetClipboardSetting(origin);
                allowed = setting == ClipboardSetting::kAllow ||
                    (setting == ClipboardSetting::kAsk &&
                     self->SetClipboardSetting(origin, ClipboardSetting::kAllow));
              }
              std::move(reply).Run({PermissionResult(
                  allowed ? PermissionStatus::GRANTED : PermissionStatus::DENIED)});
            },
            weak_factory_.GetWeakPtr(), frame->GetWeakDocumentPtr(),
            std::move(callback)));
    return;
  }
  for (const auto& descriptor : request.permissions) {
    if (!is_document_request) {
      results.emplace_back(PermissionStatus::DENIED);
    } else if (CanUseExtensionClipboard(descriptor, frame, &*context_)) {
      results.emplace_back(PermissionStatus::GRANTED);
    } else if (IsClipboardReadDescriptor(descriptor)) {
      results.emplace_back(HasClipboardReadGrant(frame) ? PermissionStatus::GRANTED
                                                        : PermissionStatus::DENIED);
    } else if (IsClipboardWriteDescriptor(descriptor)) {
      const bool allowed = request.user_gesture &&
          descriptor->extension->get_clipboard()->has_user_gesture &&
          IsTrustworthyMainDocument(frame, &*context_) && HasClipboardFocus(frame) &&
          frame->IsFeatureEnabled(network::mojom::PermissionsPolicyFeature::kClipboardWrite) &&
          frame->HasTransientUserActivation() &&
          GetClipboardSetting(frame->GetLastCommittedOrigin()) != ClipboardSetting::kBlock;
      results.emplace_back(allowed ? PermissionStatus::GRANTED
                                   : PermissionStatus::DENIED);
    } else {
      results.push_back(GetPermissionResultForCurrentDocument(
          descriptor, frame, /*should_include_device_status=*/false));
    }
  }
  std::move(callback).Run(results);
}

PermissionStatus ProfilePermissionController::GetPermissionStatus(
    const blink::mojom::PermissionDescriptorPtr& descriptor,
    const GURL& requester,
    const GURL& embedder) {
  return GetPermissionResultForOriginWithoutContext(
      descriptor, url::Origin::Create(requester), url::Origin::Create(embedder)).status;
}

PermissionResult
ProfilePermissionController::GetPermissionResultForOriginWithoutContext(
    const blink::mojom::PermissionDescriptorPtr& descriptor,
    const url::Origin& requester,
    const url::Origin& embedder) {
  if (IsNotification(descriptor) && requester == embedder)
    return NotificationResult(NotificationSetting(requester));
  return PermissionResult(PermissionStatus::DENIED);
}

PermissionResult
ProfilePermissionController::GetPermissionResultForCurrentDocument(
    const blink::mojom::PermissionDescriptorPtr& descriptor,
    content::RenderFrameHost* frame,
    bool) {
  if (CanUseExtensionClipboard(descriptor, frame, &*context_))
    return PermissionResult(PermissionStatus::GRANTED);
  if (IsTrustworthyMainDocument(frame, &*context_)) {
    if (IsNotification(descriptor))
      return NotificationResult(NotificationSetting(frame->GetLastCommittedOrigin()));
    const auto setting = GetClipboardSetting(frame->GetLastCommittedOrigin());
    if (IsClipboardReadDescriptor(descriptor)) {
      if (!frame->IsFeatureEnabled(
              network::mojom::PermissionsPolicyFeature::kClipboardRead)) {
        return PermissionResult(PermissionStatus::DENIED);
      }
      if (setting == ClipboardSetting::kAllow)
        return PermissionResult(PermissionStatus::GRANTED);
      return PermissionResult(setting == ClipboardSetting::kBlock
                                  ? PermissionStatus::DENIED : PermissionStatus::ASK);
    }
    if (IsClipboardWriteDescriptor(descriptor) &&
        descriptor->extension->get_clipboard()->has_user_gesture) {
      if (!frame->IsFeatureEnabled(
              network::mojom::PermissionsPolicyFeature::kClipboardWrite)) {
        return PermissionResult(PermissionStatus::DENIED);
      }
      return PermissionResult(setting == ClipboardSetting::kBlock
                                  ? PermissionStatus::DENIED : PermissionStatus::ASK);
    }
  }
  if (descriptor &&
      descriptor->name == blink::mojom::PermissionName::PERSISTENT_STORAGE &&
      storage_probe_origin_ && frame &&
      frame->GetBrowserContext() == &*context_ && frame->IsActive() &&
      frame->IsInPrimaryMainFrame() &&
      frame->GetLastCommittedOrigin() == *storage_probe_origin_) {
    return PermissionResult(PermissionStatus::GRANTED);
  }
  return PermissionResult(PermissionStatus::DENIED);
}

PermissionResult ProfilePermissionController::GetPermissionResultForWorker(
    const blink::mojom::PermissionDescriptorPtr& descriptor,
    content::RenderProcessHost*,
    const GURL& origin) {
  if (IsNotification(descriptor))
    return NotificationResult(NotificationSetting(url::Origin::Create(origin)));
  return PermissionResult(PermissionStatus::DENIED);
}

PermissionResult
ProfilePermissionController::GetPermissionResultForEmbeddedRequester(
    const blink::mojom::PermissionDescriptorPtr&,
    content::RenderFrameHost*,
    const url::Origin&) {
  return PermissionResult(PermissionStatus::DENIED);
}

void ProfilePermissionController::ResetPermission(
    blink::PermissionType type,
    const GURL& requesting_origin,
    const GURL& embedding_origin) {
  if (type == blink::PermissionType::NOTIFICATIONS &&
      url::Origin::Create(requesting_origin) == url::Origin::Create(embedding_origin)) {
    SetNotificationSetting(url::Origin::Create(requesting_origin), 0);
    return;
  }
  if ((type == blink::PermissionType::CLIPBOARD_READ_WRITE ||
       type == blink::PermissionType::CLIPBOARD_SANITIZED_WRITE) &&
      url::Origin::Create(requesting_origin) == url::Origin::Create(embedding_origin)) {
    SetClipboardSetting(url::Origin::Create(requesting_origin), std::nullopt);
    return;
  }
  if (type == blink::PermissionType::PERSISTENT_STORAGE &&
      storage_probe_origin_ &&
      url::Origin::Create(requesting_origin) == *storage_probe_origin_ &&
      url::Origin::Create(embedding_origin) == *storage_probe_origin_) {
    storage_probe_origin_.reset();
  }
}

}  // namespace zitter::content_embedder
