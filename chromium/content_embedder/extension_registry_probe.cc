// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "extension_registry_probe.h"

#include "extension_downloads.h"
#include "extensions_manager.h"

#include "base/base64.h"
#include "base/strings/string_util.h"
#include "base/supports_user_data.h"
#include "chromium/prototype/night_shift_config.h"
#include "extension_archive_service.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <ranges>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

#include "build/build_config.h"
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
#include "extension_notifications.h"
#endif
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
#include "extensions/browser/api/messaging/native_message_host.h"
#include "extensions/browser/api/messaging/native_message_port.h"
#include "workspace_native_messaging.h"
#include "workspace_native_message_port_dispatcher.h"
#endif
#if BUILDFLAG(IS_POSIX)
#include <sys/stat.h>
#endif

#include "base/atomic_sequence_num.h"
#include "base/i18n/rtl.h"
#include "base/check.h"
#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "base/no_destructor.h"
#include "base/notreached.h"
#include "base/one_shot_event.h"
#include "base/scoped_multi_source_observation.h"
#include "base/strings/pattern.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/threading/thread_restrictions.h"
#include "components/crx_file/id_util.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "components/version_info/version_info.h"
#include "components/prefs/in_memory_pref_store.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/pref_service_factory.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/user_prefs/user_prefs.h"
#include "components/value_store/value_store.h"
#include "components/value_store/value_store_factory_impl.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/child_process_security_policy.h"
#include "content/public/browser/context_menu_params.h"
#include "content/public/browser/font_list_async.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/back_forward_cache.h"
#include "content/public/browser/browsing_data_filter_builder.h"
#include "content/public/browser/browsing_data_remover.h"
#include "content/public/browser/navigation_details.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/document_user_data.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_throttle.h"
#include "content/public/browser/navigation_throttle_registry.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/render_process_host_observer.h"
#include "content/public/browser/security_principal.h"
#include "content/public/browser/service_worker_context.h"
#include "net/cookies/canonical_cookie.h"
#include "net/cookies/cookie_inclusion_status.h"
#include "net/cookies/cookie_options.h"
#include "net/cookies/cookie_partition_key_collection.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "content/public/browser/service_worker_running_info.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/common/color_parser.h"
#include "content/public/common/result_codes.h"
#include "base/barrier_closure.h"
#include "base/task/thread_pool.h"
#include "content/public/browser/storage_partition_config.h"
#include "extensions/browser/api/storage/storage_frontend.h"
#include "extensions/browser/uninstall_reason.h"
#include "content/public/browser/storage_partition_config.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "content/public/common/isolated_world_ids.h"
#include "extensions/browser/api/alarms/alarm_manager.h"
#include "extensions/browser/api/declarative_net_request/declarative_net_request_api.h"
#include "extensions/browser/api/declarative_net_request/file_backed_ruleset_source.h"
#include "extensions/browser/api/declarative_net_request/prefs_helper.h"
#include "extensions/browser/api/declarative_net_request/rules_monitor_service.h"
#include "extensions/browser/api/declarative_net_request/ruleset_manager.h"
#include "extensions/browser/api/declarative_net_request/web_contents_helper.h"
#include "extensions/browser/safe_browsing_delegate.h"
#include "extensions/browser/api/web_request/web_request_api.h"
#include "extensions/browser/api/declarative_net_request/utils.h"
#include "extensions/browser/api/alarms/alarms_api.h"
#include "extensions/browser/api/extensions_api_client.h"
#include "extensions/browser/api/messaging/message_service.h"
#include "extensions/browser/api/messaging/messaging_delegate.h"
#include "extensions/browser/api/permissions/permissions_api.h"
#include "extensions/browser/api/permissions/permissions_api_helpers.h"
#include "extensions/browser/api/runtime/runtime_api_delegate.h"
#include "extensions/browser/api/scripting/scripting_api.h"
#include "extensions/browser/api/user_scripts/user_scripts_api.h"
#include "extensions/browser/api/storage/backend_task_runner.h"
#include "extensions/browser/api/storage/settings_storage_quota_enforcer.h"
#include "extensions/browser/api/storage/storage_api.h"
#include "extensions/browser/api/storage/value_store_cache.h"
#include "extensions/browser/api/storage/value_store_util.h"
#include "extensions/browser/event_router.h"
#include "extensions/browser/extension_api_frame_id_map.h"
#include "extensions/browser/extension_action.h"
#include "extensions/browser/extension_action_manager.h"
#include "extensions/browser/extension_function_registry.h"
#include "content/public/browser/web_contents_delegate.h"
#include "extensions/browser/api/idle/idle_api.h"
#include "extensions/browser/api/idle/idle_manager.h"
#include "extensions/browser/api/idle/idle_manager_factory.h"
#include "extensions/browser/api/offscreen/offscreen_api.h"
#include "extensions/browser/api/offscreen/offscreen_document_manager.h"
#include "extensions/browser/api/runtime/runtime_api.h"
#include "extensions/browser/extension_host_delegate.h"
#include "extensions/browser/offscreen_document_host.h"
#include "extensions/browser/extension_file_task_runner.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_pref_store.h"
#include "extensions/browser/extension_pref_value_map_factory.h"
#include "components/password_manager/core/common/password_manager_pref_names.h"
#include "extensions/browser/extension_protocols.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_registry_observer.h"
#include "extensions/browser/extension_registry_factory.h"
#include "extensions/browser/extensions_browser_api_provider.h"
#include "extensions/browser/extensions_browser_client.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/extension_system_provider.h"
#include "extensions/browser/extension_util.h"
#include "extensions/browser/extension_user_script_loader.h"
#include "extensions/browser/extension_web_contents_observer.h"
#include "extensions/browser/image_loader.h"
#include "extensions/browser/kiosk/kiosk_delegate.h"
#include "extensions/browser/lazy_context_id.h"
#include "extensions/browser/permissions/active_tab_permission_granter.h"
#include "extensions/browser/permissions/permissions_updater.h"
#include "extensions/browser/permissions_manager.h"
#include "extensions/browser/pref_names.h"
#include "extensions/browser/process_map.h"
#include "extensions/browser/script_executor.h"
#include "extensions/browser/state_store.h"
#include "extensions/browser/quota_service.h"
#include "extensions/browser/renderer_startup_helper.h"
#include "extensions/browser/service_worker/service_worker_task_queue.h"
#include "extensions/browser/user_script_manager.h"
#include "extensions/browser/user_script_world_configuration_manager.h"
#include "extensions/browser/unloaded_extension_reason.h"
#include "extensions/browser/view_type_utils.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/mojom/permissions_policy/permissions_policy_feature.mojom.h"
#include "extensions/common/api/extension_action/action_info.h"
#include "extensions/common/api/extension_types.h"
#include "extensions/common/mojom/event_dispatcher.mojom.h"
#include "extensions/common/api/storage.h"
#include "extensions/common/constants.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_resource.h"
#include "extensions/common/extension_urls.h"
#include "extensions/common/file_util.h"
#include "extensions/common/manifest_handlers/incognito_info.h"
#include "extensions/common/manifest_handlers/background_info.h"
#include "extensions/common/manifest_handlers/content_scripts_handler.h"
#include "extensions/common/api/permissions.h"
#include "extensions/common/manifest_handlers/options_page_info.h"
#include "extensions/common/manifest_handlers/description_info.h"
#include "extensions/common/manifest_handlers/icons_handler.h"
#include "extensions/common/manifest_handlers/manifest_url_handlers.h"
#include "extensions/common/manifest_handlers/offline_enabled_info.h"
#include "extensions/common/manifest_handlers/sandboxed_page_info.h"
#include "extensions/common/manifest_handlers/version_name_info.h"
#include "extensions/common/api/management.h"
#include "extensions/common/manifest_handlers/web_accessible_resources_info.h"
#include "extensions/common/manifest_handlers/permissions_parser.h"
#include "extensions/common/mojom/frame.mojom.h"
#include "extensions/common/permissions/permissions_data.h"
#include "extensions/common/url_pattern_set.h"
#include "extensions/common/user_script.h"
#include "net/base/net_errors.h"
#include "ui/base/page_transition_types.h"
#include "net/http/http_response_headers.h"
#include "profile_context.h"
#include "third_party/blink/public/common/service_worker/service_worker_status_code.h"
#include "third_party/blink/public/common/storage_key/storage_key.h"
#include "third_party/blink/public/mojom/mediastream/media_stream.mojom.h"
#include "ui/gfx/codec/jpeg_codec.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/image/image.h"
#include "workspace_lab.h"
#include "chromium/prototype/workspace_shell.h"

namespace zitter::content_embedder {
namespace {

using content::BrowserContext;

// All regular Workspace stacks share the embedder's single main window.
constexpr int kWorkspaceExtensionWindowId = 1;
constexpr extensions::PrefMap kWorkspaceSidePanelPref{
    "zitter_side_panel", extensions::PrefType::kDictionary,
    extensions::PrefScope::kExtensionSpecific};
constexpr extensions::PrefMap kWorkspacePrivacyPref{
    "zitter_privacy", extensions::PrefType::kDictionary,
    extensions::PrefScope::kExtensionSpecific};
constexpr auto kPrivacyScope = extensions::api::types::ChromeSettingScope::kRegularOnly;
constexpr const char* kPasswordSavingPref = password_manager::prefs::kCredentialsEnableService;

base::DictValue ReadExtensionPasswordSaving(ProfileContext& profile, const std::string& id,
                                           bool value) {
  auto* map = ExtensionPrefValueMapFactory::GetForBrowserContext(&profile);
  const auto* pref = profile.GetPasswordPrefs()->FindPreference(kPasswordSavingPref);
  const char* level = "not_controllable";
  if (pref->IsExtensionModifiable()) {
    if (map->DoesExtensionControlPref(id, kPasswordSavingPref, nullptr))
      level = "controlled_by_this_extension";
    else
      level = map->CanExtensionControlPref(id, kPasswordSavingPref, false)
                  ? "controllable_by_this_extension" : "controlled_by_other_extensions";
  }
  return base::DictValue().Set("value", value).Set("levelOfControl", level);
}

void SetExtensionPasswordSaving(BrowserContext& context, const std::string& id,
                                std::optional<bool> value) {
  auto* map = ExtensionPrefValueMapFactory::GetForBrowserContext(&context);
  auto* prefs = extensions::ExtensionPrefs::Get(&context);
  if (value.has_value()) {
    map->SetExtensionPref(id, kPasswordSavingPref, kPrivacyScope, base::Value(*value));
    prefs->UpdateExtensionPref(id, kWorkspacePrivacyPref.name,
                               base::Value(base::DictValue().Set("passwordSavingEnabled", *value)));
  } else {
    map->RemoveExtensionPref(id, kPasswordSavingPref, kPrivacyScope);
    prefs->UpdateExtensionPref(id, kWorkspacePrivacyPref.name, std::nullopt);
  }
}

class LabPrivacyFunction : public ExtensionFunction {
 protected:
  ~LabPrivacyFunction() override = default;

  ResponseAction Run() override {
    EXTENSION_FUNCTION_VALIDATE(args().size() == 2 && args()[0].is_string() && args()[1].is_dict());
    auto* context = browser_context();
    if (!context || context->IsOffTheRecord() || !extension() ||
        !extension()->permissions_data()->HasAPIPermission(
            extensions::mojom::APIPermissionID::kPrivacy)) {
      return RespondNow(Error("Privacy settings are unavailable in this profile."));
    }
    const auto& setting = args()[0].GetString();
    const auto& details = args()[1].GetDict();
    const bool reading = std::string_view(name()) == "types.ChromeSetting.get";
    if (details.FindBool("incognito").value_or(false))
      return RespondNow(Error("Private profile settings are unavailable."));
    if (const auto* scope = details.FindString("scope");
        scope && *scope != "regular" && *scope != "regular_only") {
      return RespondNow(Error("Only regular profile settings are supported."));
    }
    if (setting == "autofillAddressEnabled" || setting == "autofillCreditCardEnabled") {
      if (!reading)
        return RespondNow(Error("This browser does not provide the requested autofill service."));
      return RespondNow(WithArguments(
          base::DictValue().Set("value", false).Set("levelOfControl", "not_controllable")));
    }
    if (setting != "passwordSavingEnabled")
      return RespondNow(Error("This privacy setting is unavailable."));
    auto& profile = static_cast<ProfileContext&>(*context);
    const auto value = profile.GetPasswordSavingSetting();
    if (!value)
      return RespondNow(Error("Password saving preferences are unavailable."));
    const bool controllable = profile.GetPasswordPrefs()->FindPreference(kPasswordSavingPref)
                                  ->IsExtensionModifiable();
    if (reading)
      return RespondNow(WithArguments(ReadExtensionPasswordSaving(profile, extension_id(), *value)));
    if (!controllable)
      return RespondNow(Error("Password saving is managed by browser policy."));
    if (std::string_view(name()) == "types.ChromeSetting.clear") {
      SetExtensionPasswordSaving(*context, extension_id(), std::nullopt);
      return RespondNow(NoArguments());
    }
    const auto next = details.FindBool("value");
    EXTENSION_FUNCTION_VALIDATE(next.has_value());
    SetExtensionPasswordSaving(*context, extension_id(), next);
    return RespondNow(NoArguments());
  }
};

#define ZITTER_PRIVACY_FUNCTION(Class, Method, Histogram) \
  class Class final : public LabPrivacyFunction { \
   public: \
    DECLARE_EXTENSION_FUNCTION("types.ChromeSetting." Method, Histogram) \
   private: \
    ~Class() override = default; \
  }
ZITTER_PRIVACY_FUNCTION(LabPrivacyGetFunction, "get", TYPES_CHROMESETTING_GET);
ZITTER_PRIVACY_FUNCTION(LabPrivacySetFunction, "set", TYPES_CHROMESETTING_SET);
ZITTER_PRIVACY_FUNCTION(LabPrivacyClearFunction, "clear", TYPES_CHROMESETTING_CLEAR);
#undef ZITTER_PRIVACY_FUNCTION

class ExtensionViewDestination : public base::SupportsUserData::Data {
 public:
  explicit ExtensionViewDestination(GURL url) : url(std::move(url)) {}
  const GURL url;
};

constexpr char kSidePanelDestination = 0;
constexpr char kNightShiftSettingsDestination = 0;


constexpr char kPopupTabContext = 0;
class PopupTabContext : public base::SupportsUserData::Data {
 public:
  explicit PopupTabContext(BrowserContext& context) : context(context) {}
  const raw_ref<BrowserContext> context;
};

BrowserContext* TabContextForExtensionFunction(ExtensionFunction& function) {
  auto* page = function.GetSenderWebContents();
  const auto* popup = page ? static_cast<PopupTabContext*>(
      page->GetUserData(&kPopupTabContext)) : nullptr;
  if (popup && function.extension() &&
      CanLoadExtensionProbeResources(*popup->context, function.extension()->id())) {
    return &*popup->context;
  }
  return function.browser_context();
}

void ClearTabActionState(BrowserContext& context, int tab_id);

bool CanReadExtensionTabUrl(const extensions::Extension& extension,
                            const GURL& url,
                            int tab_id = extension_misc::kUnknownTabId) {
  const auto* permissions = extension.permissions_data();
  return extension.origin().IsSameOriginWith(url) ||
         permissions->HasAPIPermission(extensions::mojom::APIPermissionID::kTab) ||
         permissions->HasHostPermission(url) ||
         (tab_id != extension_misc::kUnknownTabId &&
          permissions->HasTabPermissionsForSecurityOrigin(tab_id, url));
}

}  // namespace

bool IsWorkspaceNavigationFrame(content::RenderFrameHost& frame,
                                content::WebContents& page) {
  return frame.IsActive() && !frame.IsNestedWithinFencedFrame() &&
         frame.GetMainFrame() == page.GetPrimaryMainFrame() &&
         content::WebContents::FromRenderFrameHost(&frame) == &page;
}

bool CanExposeNavigationFrame(content::RenderFrameHost& frame,
                              content::WebContents& page) {
  return IsWorkspaceNavigationFrame(frame, page) &&
         frame.GetLastCommittedURL().SchemeIsHTTPOrHTTPS();
}

class NavigationDocumentState final
    : public content::DocumentUserData<NavigationDocumentState> {
 public:
  ~NavigationDocumentState() override = default;

  GURL url;
  bool load_failed = false;

 private:
  friend class content::DocumentUserData<NavigationDocumentState>;
  explicit NavigationDocumentState(content::RenderFrameHost* frame)
      : DocumentUserData(frame) {}
  DOCUMENT_USER_DATA_KEY_DECL();
};

DOCUMENT_USER_DATA_KEY_IMPL(NavigationDocumentState);

template <typename Frame>
base::DictValue NavigationFrameDetails(Frame* frame,
                                       content::RenderFrameHost* parent) {
  using Map = extensions::ExtensionApiFrameIdMap;
  using extensions::api::extension_types::ToString;
  base::DictValue details;
  details.Set("frameId", Map::GetFrameId(frame));
  details.Set("parentFrameId", Map::GetParentFrameId(frame));
  details.Set("frameType", ToString(Map::GetFrameType(frame)));
  details.Set("documentLifecycle", ToString(Map::GetDocumentLifecycle(frame)));
  if (parent)
    details.Set("parentDocumentId", Map::GetDocumentId(parent).ToString());
  return details;
}

base::DictValue NavigationDocumentDetails(content::RenderFrameHost* frame) {
  auto details = NavigationFrameDetails(frame, frame->GetParentOrOuterDocument());
  details.Set("documentId", extensions::ExtensionApiFrameIdMap::GetDocumentId(frame).ToString());
  return details;
}

class WorkspaceExtensionTab final : public content::WebContentsObserver {
 public:
  WorkspaceExtensionTab(BrowserContext& context, WorkspaceLab& workspace)
      : context_(&context), workspace_(workspace), id_(NextId()) {
    CHECK(Tabs().emplace(id_, this).second);
  }

  ~WorkspaceExtensionTab() override {
    ClearTabActionState(*context_, id_);
    SetPage(nullptr);
    CHECK_EQ(Tabs().erase(id_), 1u);
    if (published_) {
      Dispatch(extensions::events::TABS_ON_REMOVED, "tabs.onRemoved",
               base::ListValue().Append(id_).Append(
                   base::DictValue()
                       .Set("windowId", kWorkspaceExtensionWindowId)
                       .Set("isWindowClosing", window_closing_)));
    }
  }

  int id() const { return id_; }
  WorkspaceLab& workspace() const { return *workspace_; }
  int index() const { return index_; }
  bool active() const { return active_; }
  bool highlighted() const { return highlighted_; }
  bool pinned() const { return pinned_; }
  void SetExtensionPageUrl(const GURL& url) { extension_page_url_ = url; }
  bool AllowsExtensionPage(const GURL& url) const {
    return extension_page_url_ == url;
  }
  void SetWindowClosing() { window_closing_ = true; }

  const base::DictValue* SidePanelOptions(const std::string& id) const {
    const auto it = panel_options_.find(id);
    return it == panel_options_.end() ? nullptr : &it->second;
  }

  void SetSidePanelOptions(const std::string& id, base::DictValue options) {
    panel_options_.insert_or_assign(id, std::move(options));
  }

  void ClearSidePanelOptions(const std::string& id) {
    panel_options_.erase(id);
  }


  extensions::ScriptExecutor* GetScriptExecutor() {
    auto* page = web_contents();
    if (!page || page->IsBeingDestroyed())
      return nullptr;
    if (!script_executor_)
      script_executor_ = std::make_unique<extensions::ScriptExecutor>(page);
    return script_executor_.get();
  }

  void NotifyMoved(int from_index) {
    if (published_ && from_index >= 0 && from_index != index_) {
      Dispatch(extensions::events::TABS_ON_MOVED, "tabs.onMoved",
               base::ListValue().Append(id_).Append(
                   base::DictValue()
                       .Set("windowId", kWorkspaceExtensionWindowId)
                       .Set("fromIndex", from_index)
                       .Set("toIndex", index_)));
    }
  }

  void SetPage(content::WebContents* page) {
    if (page == web_contents())
      return;
    script_executor_.reset();
    if (auto* previous = web_contents()) {
      if (!previous->IsBeingDestroyed()) {
        if (auto* granter =
                extensions::ActiveTabPermissionGranter::FromWebContents(previous)) {
          for (const auto& extension :
               extensions::ExtensionRegistry::Get(context_)->enabled_extensions()) {
            granter->ClearActiveExtensionAndNotify(extension->id());
          }
        }
      }
      CHECK_EQ(LoadedPages().erase(previous), 1u);
    }
    if (page) {
      CHECK_EQ(page->GetBrowserContext(), context_);
      extensions::SetViewType(page, extensions::mojom::ViewType::kTabContents);
      CHECK(LoadedPages().emplace(page, this).second);
    }
    waiting_for_load_ = false;
    Observe(page);
    if (page) {
      if (muted_)
        page->SetAudioMuted(*muted_);
      else if (page->IsAudioMuted())
        muted_ = true;
    }
  }

  void Update(int index, bool active, bool highlighted, bool pinned,
              const GURL& deferred_url, std::u16string_view deferred_title) {
    const bool activated = active && !active_;
    const bool pin_changed = pinned != pinned_;
    const bool discard_changed = discarded() != last_discarded_;
    index_ = index;
    active_ = active;
    highlighted_ = highlighted;
    pinned_ = pinned;
    last_discarded_ = discarded();
    if (discarded()) {
      deferred_url_ = deferred_url;
      deferred_title_ = deferred_title;
    }
    if (!std::exchange(published_, true)) {
      DispatchSnapshot(extensions::events::TABS_ON_CREATED, "tabs.onCreated", {});
    } else {
      std::vector<std::string_view> changes;
      if (pin_changed)
        changes.push_back("pinned");
      if (discard_changed) {
        changes.push_back("discarded");
        changes.push_back("status");
      }
      DispatchUpdate(std::move(changes));
    }
    if (activated) {
      Dispatch(extensions::events::TABS_ON_ACTIVATED, "tabs.onActivated",
               base::ListValue().Append(
                   base::DictValue()
                       .Set("tabId", id_)
                       .Set("windowId", kWorkspaceExtensionWindowId)));
    }
  }

  bool discarded() const { return !web_contents(); }
  bool audible() const {
    return web_contents() && web_contents()->IsCurrentlyAudible();
  }
  bool muted() const { return muted_.value_or(false); }

  void SetMuted(bool mute, std::string_view extension_id) {
    if (muted() == mute)
      return;
    muted_ = mute;
    mute_source_.emplace(extension_id);
    if (web_contents())
      web_contents()->SetAudioMuted(mute);
    DispatchUpdate({"mutedInfo"});
  }
  std::string_view status() const {
    if (!web_contents())
      return "unloaded";
    return web_contents()->IsLoading() ? "loading" : "complete";
  }
  const GURL& url() const {
    return web_contents() ? web_contents()->GetLastCommittedURL() : deferred_url_;
  }
  const GURL* pending_url() const {
    const auto* entry =
        web_contents() ? web_contents()->GetController().GetPendingEntry() : nullptr;
    return entry ? &entry->GetURL() : nullptr;
  }

  base::DictValue Metadata() const {
    auto muted_info = base::DictValue().Set("muted", muted());
    if (mute_source_) {
      muted_info.Set("reason", mute_source_->empty() ? "user" : "extension");
      if (!mute_source_->empty())
        muted_info.Set("extensionId", *mute_source_);
    }
    return base::DictValue()
        .Set("id", id_)
        .Set("index", index_)
        .Set("windowId", kWorkspaceExtensionWindowId)
        .Set("incognito", context_->IsOffTheRecord())
        .Set("active", active_)
        .Set("highlighted", highlighted_)
        .Set("selected", active_)
        .Set("pinned", pinned_)
        .Set("audible", audible())
        .Set("mutedInfo", std::move(muted_info))
        .Set("discarded", discarded())
        .Set("status", status());
  }

  std::u16string_view CommittedTitle() const {
    if (!web_contents())
      return deferred_title_;
    const auto* entry = web_contents()->GetController().GetLastCommittedEntry();
    return entry ? std::u16string_view(entry->GetTitleForDisplay())
                 : std::u16string_view();
  }

  base::DictValue MetadataFor(const extensions::Extension& extension) const {
    auto data = Metadata();
    if (!url().is_empty() && CanReadExtensionTabUrl(extension, url(), id_)) {
      data.Set("url", url().spec());
      data.Set("title", base::UTF16ToUTF8(CommittedTitle()));
    }
    const auto* pending = pending_url();
    if (pending && CanReadExtensionTabUrl(extension, *pending, id_))
      data.Set("pendingUrl", pending->spec());
    return data;
  }

  static std::vector<WorkspaceExtensionTab*> InProfile(BrowserContext* context) {
    std::vector<WorkspaceExtensionTab*> result;
    if (!context)
      return result;
    for (const auto& [id, tab] : Tabs()) {
      if (tab->context_ == context && tab->published_)
        result.push_back(tab);
    }
    std::ranges::stable_sort(result, {}, &WorkspaceExtensionTab::index);
    return result;
  }

  static content::WebContents* Find(BrowserContext* context, int id) {
    const auto* tab = FindTab(context, id);
    auto* page = tab ? tab->web_contents() : nullptr;
    return page && !page->IsBeingDestroyed() ? page : nullptr;
  }

  static WorkspaceExtensionTab* FindTab(BrowserContext* context, int id) {
    const auto tab = Tabs().find(id);
    return tab != Tabs().end() && tab->second->context_ == context
               ? tab->second.get()
               : nullptr;
  }

  static WorkspaceExtensionTab* ActiveInProfile(BrowserContext* context) {
    const auto tabs = InProfile(context);
    const auto active = std::ranges::find(tabs, true, &WorkspaceExtensionTab::active);
    return active != tabs.end() ? *active : nullptr;
  }

  static WorkspaceExtensionTab* FromWebContents(content::WebContents* page) {
    const auto it = LoadedPages().find(page);
    return it == LoadedPages().end() ? nullptr : it->second.get();
  }

 private:
  bool ExposeNavigation(content::NavigationHandle& navigation) const {
    if (!published_ || navigation.IsInFencedFrameTree() || !navigation.GetURL().SchemeIsHTTPOrHTTPS()) {
      return false;
    }
    auto* parent = navigation.GetParentFrameOrOuterDocument();
    return navigation.IsInPrimaryMainFrame() ||
           (parent && parent->IsActive() &&
            content::WebContents::FromRenderFrameHost(parent) == web_contents());
  }

  void DispatchNavigation(extensions::events::HistogramValue type,
                          std::string_view name,
                          const GURL& url,
                          base::DictValue details) {
    details.Set("url", url.spec());
    details.Set("tabId", id_);
    details.Set("timeStamp", base::Time::Now().InMillisecondsFSinceUnixEpoch());
    Dispatch(type, name, base::ListValue().Append(std::move(details)), {}, url);
  }

  void DidStartNavigation(content::NavigationHandle* navigation) override {
    if (!ExposeNavigation(*navigation) || navigation->IsSameDocument())
      return;
    auto details = NavigationFrameDetails(navigation, navigation->GetParentFrameOrOuterDocument());
    details.Set("processId", -1);
    DispatchNavigation(extensions::events::WEB_NAVIGATION_ON_BEFORE_NAVIGATE,
                       "webNavigation.onBeforeNavigate", navigation->GetURL(),
                       std::move(details));
  }

  void DispatchCommittedNavigation(content::NavigationHandle& navigation) {
    using namespace extensions::events;
    using Map = extensions::ExtensionApiFrameIdMap;
    const auto& url = navigation.GetURL();
    if (!navigation.HasCommitted() || navigation.IsErrorPage()) {
      auto details = NavigationFrameDetails(&navigation, navigation.GetParentFrameOrOuterDocument());
      details.Set("documentId", Map::GetDocumentId(&navigation).ToString());
      details.Set("processId", -1);
      const int error = navigation.GetNetErrorCode();
      details.Set("error", net::ErrorToString(error == net::OK ? net::ERR_ABORTED : error));
      DispatchNavigation(WEB_NAVIGATION_ON_ERROR_OCCURRED,
                         "webNavigation.onErrorOccurred", url, std::move(details));
      return;
    }
    auto* frame = navigation.GetRenderFrameHost();
    auto* state = NavigationDocumentState::GetOrCreateForCurrentDocument(frame);
    const auto previous_url = std::exchange(state->url, url);
    auto details = NavigationDocumentDetails(frame);
    details.Set("processId", frame->GetProcess()->GetDeprecatedID());
    const auto transition = navigation.GetPageTransition();
    details.Set("transitionType", ui::PageTransitionCoreTypeIs(transition, ui::PAGE_TRANSITION_AUTO_TOPLEVEL)
                                      ? "start_page" : ui::PageTransitionGetCoreTransitionString(transition));
    base::ListValue qualifiers;
    if (transition & ui::PAGE_TRANSITION_CLIENT_REDIRECT)
      qualifiers.Append("client_redirect");
    if (navigation.WasServerRedirect() || (transition & ui::PAGE_TRANSITION_SERVER_REDIRECT))
      qualifiers.Append("server_redirect");
    if (transition & ui::PAGE_TRANSITION_FORWARD_BACK)
      qualifiers.Append("forward_back");
    if (transition & ui::PAGE_TRANSITION_FROM_ADDRESS_BAR)
      qualifiers.Append("from_address_bar");
    details.Set("transitionQualifiers", std::move(qualifiers));
    if (navigation.IsSameDocument()) {
      const auto& old_url = !previous_url.is_empty()
                                ? previous_url
                                : navigation.GetPreviousPrimaryMainFrameURL();
      const bool fragment = url != old_url && url.EqualsIgnoringRef(old_url);
      DispatchNavigation(fragment ? WEB_NAVIGATION_ON_REFERENCE_FRAGMENT_UPDATED
                                  : WEB_NAVIGATION_ON_HISTORY_STATE_UPDATED,
                         fragment ? "webNavigation.onReferenceFragmentUpdated"
                                  : "webNavigation.onHistoryStateUpdated",
                         url, std::move(details));
      return;
    }
    DispatchNavigation(WEB_NAVIGATION_ON_COMMITTED, "webNavigation.onCommitted",
                       url, std::move(details));
    if (navigation.IsServedFromBackForwardCache())
      DidFinishLoad(frame, url);
  }

  void DOMContentLoaded(content::RenderFrameHost* frame) override {
    DispatchLoadEvent(frame, extensions::events::WEB_NAVIGATION_ON_DOM_CONTENT_LOADED,
                      "webNavigation.onDOMContentLoaded");
  }

  void DidFinishLoad(content::RenderFrameHost* frame, const GURL& url) override {
    if (frame->GetLastCommittedURL().EqualsIgnoringRef(url)) {
      DispatchLoadEvent(frame, extensions::events::WEB_NAVIGATION_ON_COMPLETED,
                        "webNavigation.onCompleted");
    }
  }

  void DidFailLoad(content::RenderFrameHost* frame,
                   const GURL& url,
                   int error_code) override {
    if (!published_ || frame->IsErrorDocument() ||
        !CanExposeNavigationFrame(*frame, *web_contents()) ||
        !frame->GetLastCommittedURL().EqualsIgnoringRef(url)) {
      return;
    }
    auto* state = NavigationDocumentState::GetOrCreateForCurrentDocument(frame);
    if (std::exchange(state->load_failed, true))
      return;
    auto details = NavigationDocumentDetails(frame);
    details.Set("processId", frame->GetProcess()->GetDeprecatedID());
    details.Set("error", net::ErrorToString(error_code));
    DispatchNavigation(extensions::events::WEB_NAVIGATION_ON_ERROR_OCCURRED,
                       "webNavigation.onErrorOccurred", url, std::move(details));
  }

  void DispatchLoadEvent(content::RenderFrameHost* frame,
                         extensions::events::HistogramValue type,
                         std::string_view name) {
    if (!published_ || frame->IsErrorDocument() ||
        !CanExposeNavigationFrame(*frame, *web_contents())) {
      return;
    }
    const auto* state = NavigationDocumentState::GetForCurrentDocument(frame);
    if (state && state->load_failed)
      return;
    auto details = NavigationDocumentDetails(frame);
    details.Set("processId", frame->GetProcess()->GetDeprecatedID());
    DispatchNavigation(type, name, frame->GetLastCommittedURL(), std::move(details));
  }

  void DidFinishNavigation(content::NavigationHandle* navigation) override {
    if (ExposeNavigation(*navigation))
      DispatchCommittedNavigation(*navigation);
    if (navigation->HasCommitted() && navigation->IsInPrimaryMainFrame() &&
        !navigation->IsSameDocument()) {
      ClearTabActionState(*context_, id_);
      workspace_->RefreshExtensionActionAppearance();
    }
  }
  void WebContentsDestroyed() override { SetPage(nullptr); }

  void NavigationEntryCommitted(
      const content::LoadCommittedDetails& details) override {
    if (!details.is_main_frame)
      return;
    std::vector<std::string_view> changes;
    if (details.previous_main_frame_url != url())
      changes.push_back("url");
    if (!details.is_same_document) {
      waiting_for_load_ = web_contents()->IsLoading();
      changes.push_back("status");
    }
    DispatchUpdate(std::move(changes));
  }

  void DidStopLoading() override {
    if (std::exchange(waiting_for_load_, false))
      DispatchUpdate({"status"});
  }

  void TitleWasSet(content::NavigationEntry* entry) override {
    if (entry == web_contents()->GetController().GetLastCommittedEntry())
      DispatchUpdate({"title"});
  }

  void OnAudioStateChanged(bool audible) override {
    DispatchUpdate({"audible"});
  }

  void DidUpdateAudioMutingState(bool muted) override {
    if (this->muted() != muted) {
      muted_ = muted;
      mute_source_.reset();
      DispatchUpdate({"mutedInfo"});
    }
  }

  void DispatchUpdate(std::vector<std::string_view> changes) {
    if (published_ && !changes.empty() &&
        (!web_contents() || !web_contents()->IsBeingDestroyed())) {
      DispatchSnapshot(extensions::events::TABS_ON_UPDATED, "tabs.onUpdated",
                       std::move(changes));
    }
  }

  void DispatchSnapshot(extensions::events::HistogramValue type,
                        std::string_view name,
                        std::vector<std::string_view> changes) {
    // EventRouter applies the listener's permissions before queuing the event
    // for a lazy worker, while the WebContents is still available.
    Dispatch(
        type, name, base::ListValue(),
        base::BindRepeating(
            [](WorkspaceExtensionTab* tab,
               const std::vector<std::string_view>& changes, BrowserContext*,
               extensions::mojom::ContextType,
               const extensions::Extension* extension, const base::DictValue*,
               std::optional<base::ListValue>& args,
               extensions::mojom::EventFilteringInfoPtr&, bool*) {
              if (!extension)
                return false;
              auto metadata = tab->MetadataFor(*extension);
              base::ListValue payload;
              if (!changes.empty()) {
                base::DictValue change_info;
                for (const auto key : changes) {
                  if (const auto* value = metadata.Find(key))
                    change_info.Set(key, value->Clone());
                }
                if (change_info.empty())
                  return false;
                payload.Append(tab->id());
                payload.Append(std::move(change_info));
              }
              args.emplace(std::move(payload)).Append(std::move(metadata));
              return true;
            },
            base::Unretained(this), std::move(changes)));
  }

  void Dispatch(extensions::events::HistogramValue type, std::string_view name,
                base::ListValue args,
                extensions::Event::WillDispatchCallback scrub = {},
                const GURL& filter_url = {}) {
    if (context_->ShutdownStarted() ||
        (!user_prefs::UserPrefs::IsInitialized(context_) &&
         !CanLoadExtensionProbeResources(*context_))) {
      return;
    }
    auto* router = extensions::EventRouter::Get(context_);
    if (!router || !router->HasEventListener(std::string(name)))
      return;
    auto event = std::make_unique<extensions::Event>(
        type, name, std::move(args), context_,
        extensions::mojom::ContextType::kPrivilegedExtension);
    event->will_dispatch_callback = std::move(scrub);
    if (filter_url.is_valid()) {
      event->filter_info = extensions::mojom::EventFilteringInfo::New();
      event->filter_info->url = filter_url;
    }
    router->BroadcastEvent(std::move(event));
  }

  static int NextId() {
    static base::AtomicSequenceNumber ids;
    // Chrome's extension tab IDs are positive SessionIDs.
    return ids.GetNext() + 1;
  }

  static std::map<int, raw_ptr<WorkspaceExtensionTab>>& Tabs() {
    static base::NoDestructor<std::map<int, raw_ptr<WorkspaceExtensionTab>>> tabs;
    return *tabs;
  }

  static std::map<content::WebContents*, raw_ptr<WorkspaceExtensionTab>>&
  LoadedPages() {
    static base::NoDestructor<
        std::map<content::WebContents*, raw_ptr<WorkspaceExtensionTab>>> pages;
    return *pages;
  }

  std::map<std::string, base::DictValue> panel_options_;
  std::unique_ptr<extensions::ScriptExecutor> script_executor_;
  const raw_ptr<BrowserContext> context_;
  const raw_ref<WorkspaceLab> workspace_;
  const int id_;
  int index_ = 0;
  bool active_ = false;
  bool highlighted_ = false;
  GURL extension_page_url_;
  bool pinned_ = false;
  std::optional<bool> muted_;
  std::optional<std::string> mute_source_;
  bool last_discarded_ = true;
  GURL deferred_url_;
  std::u16string deferred_title_;
  bool published_ = false;
  bool waiting_for_load_ = false;
  bool window_closing_ = false;
};

static void ActivateFixture(BrowserContext& context,
                     const extensions::Extension& extension);

namespace {

class LabMessagingDelegate final : public extensions::MessagingDelegate {
 public:
  PolicyPermission IsNativeMessagingHostAllowed(
      BrowserContext* context, const std::string&) override {
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
    if (context && !context->IsOffTheRecord() && !context->ShutdownStarted())
      return PolicyPermission::ALLOW_ALL;
#endif
    return PolicyPermission::DISALLOW;
  }

#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
  std::unique_ptr<extensions::MessagePort> CreateReceiverForNativeApp(
      BrowserContext* context,
      base::WeakPtr<extensions::MessagePort::ChannelDelegate> channel,
      content::RenderFrameHost*, const extensions::ExtensionId& id,
      const extensions::PortId& receiver, const std::string& host,
      bool allow_user_level, const extensions::SigningCertificates&,
      std::string* error) override {
    if (!context || context->IsOffTheRecord() || context->ShutdownStarted()) {
      *error = extensions::NativeMessageHost::kForbiddenError;
      return nullptr;
    }
    const auto* extension = extensions::ExtensionRegistry::Get(context)
                                ->enabled_extensions().GetByID(id);
    if (!extension || !extension->permissions_data()->HasAPIPermission(
                          extensions::mojom::APIPermissionID::kNativeMessaging)) {
      *error = extensions::NativeMessageHost::kForbiddenError;
      return nullptr;
    }
    return std::make_unique<extensions::NativeMessagePort>(
        channel, receiver, CreateWorkspaceNativeMessageHost(*context, id, host, allow_user_level));
  }
#endif

  std::optional<base::DictValue> MaybeGetTabInfoForRecipient(
      content::WebContents* contents,
      const extensions::Extension& recipient) override {
    const auto* tab = contents ? WorkspaceExtensionTab::FromWebContents(contents)
                               : nullptr;
    if (!tab)
      return std::nullopt;
    return tab->MetadataFor(recipient);
  }

  content::WebContents* GetWebContentsByTabId(BrowserContext* context,
                                            int id) override {
    return WorkspaceExtensionTab::Find(context, id);
  }
};

// The Chrome-backed Zitter disabled account sync. Preserve the storage.sync
// namespace and its quota with the same profile-local persistence in Content.
class ProfileSyncValueStoreCache final : public extensions::ValueStoreCache {
 public:
  explicit ProfileSyncValueStoreCache(
      scoped_refptr<value_store::ValueStoreFactory> factory)
      : factory_(std::move(factory)) {}

  ~ProfileSyncValueStoreCache() override {
    DCHECK(extensions::IsOnBackendSequence());
  }

  void RunWithValueStoreForExtension(
      StorageCallback callback,
      scoped_refptr<const extensions::Extension> extension) override {
    DCHECK(extensions::IsOnBackendSequence());
    auto [entry, inserted] = stores_.try_emplace(extension->id());
    if (inserted) {
      const extensions::SettingsStorageQuotaEnforcer::Limits limits{
          static_cast<size_t>(extensions::api::storage::sync::QUOTA_BYTES),
          static_cast<size_t>(extensions::api::storage::sync::QUOTA_BYTES_PER_ITEM),
          static_cast<size_t>(extensions::api::storage::sync::MAX_ITEMS)};
      entry->second = std::make_unique<extensions::SettingsStorageQuotaEnforcer>(
          limits, extensions::value_store_util::CreateSettingsStore(
                      extensions::settings_namespace::SYNC,
                      extensions::value_store_util::ModelType::EXTENSION,
                      extension->id(), factory_));
    }
    std::move(callback).Run(entry->second.get());
  }

  void DeleteStorageSoon(const extensions::ExtensionId& id) override {
    DCHECK(extensions::IsOnBackendSequence());
    stores_.erase(id);
    extensions::value_store_util::DeleteValueStore(
        extensions::settings_namespace::SYNC,
        extensions::value_store_util::ModelType::EXTENSION, id, factory_);
  }

 private:
  const scoped_refptr<value_store::ValueStoreFactory> factory_;
  std::map<extensions::ExtensionId,
           std::unique_ptr<extensions::SettingsStorageQuotaEnforcer>> stores_;
};

// The standalone host has no enterprise extension policy source. Keep the
// managed namespace readable and empty, with Chromium's read-only semantics.
class UnconfiguredManagedValueStore final : public value_store::ValueStore {
 public:
  size_t GetBytesInUse(const std::string&) override { return 0; }
  size_t GetBytesInUse(const std::vector<std::string>&) override { return 0; }
  size_t GetBytesInUse() override { return 0; }

  ReadResult GetKeys() override { return Get(); }
  ReadResult Get(const std::string&) override { return Get(); }
  ReadResult Get(const std::vector<std::string>&) override { return Get(); }
  ReadResult Get() override { return ReadResult(base::DictValue(), Status()); }

  WriteResult Set(WriteOptions, const std::string&, const base::Value&) override {
    return RejectWrite();
  }
  WriteResult Set(WriteOptions, const base::DictValue&) override {
    return RejectWrite();
  }
  WriteResult Remove(const std::string&) override { return RejectWrite(); }
  WriteResult Remove(const std::vector<std::string>&) override { return RejectWrite(); }
  WriteResult Clear() override { return RejectWrite(); }

 private:
  static WriteResult RejectWrite() {
    return WriteResult(Status(READ_ONLY, "This storage area is read-only."));
  }
};

class UnconfiguredManagedValueStoreCache final : public extensions::ValueStoreCache {
 public:
  void RunWithValueStoreForExtension(
      StorageCallback callback,
      scoped_refptr<const extensions::Extension>) override {
    DCHECK(extensions::IsOnBackendSequence());
    std::move(callback).Run(&store_);
  }

  void DeleteStorageSoon(const extensions::ExtensionId&) override {
    DCHECK(extensions::IsOnBackendSequence());
  }

 private:
  UnconfiguredManagedValueStore store_;
};

class LabExtensionsAPIClient final : public extensions::ExtensionsAPIClient {
 public:
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
  std::unique_ptr<extensions::NativeMessagePortDispatcher> CreateNativeMessagePortDispatcher(
      std::unique_ptr<extensions::NativeMessageHost> host,
      base::WeakPtr<extensions::NativeMessagePort> port,
      scoped_refptr<base::SingleThreadTaskRunner> runner) override {
    return std::make_unique<extensions::WorkspaceNativeMessagePortDispatcher>(
        std::move(host), std::move(port), std::move(runner));
  }
#endif
  void AddAdditionalValueStoreCaches(
      BrowserContext* context,
      const scoped_refptr<value_store::ValueStoreFactory>& factory,
      extensions::SettingsChangedCallback observer,
      std::map<extensions::settings_namespace::Namespace,
               raw_ptr<extensions::ValueStoreCache, CtnExperimental>>* caches)
      override {
    (*caches)[extensions::settings_namespace::SYNC] =
        new ProfileSyncValueStoreCache(factory);
    (*caches)[extensions::settings_namespace::MANAGED] =
        new UnconfiguredManagedValueStoreCache();
  }

  extensions::MessagingDelegate* GetMessagingDelegate() override {
    return &messaging_delegate_;
  }

 private:
  LabMessagingDelegate messaging_delegate_;
};

class LabExtensionWebContentsObserver final
    : public extensions::ExtensionWebContentsObserver,
      private extensions::ExtensionRegistryObserver,
      public content::WebContentsUserData<LabExtensionWebContentsObserver> {
 public:
  ~LabExtensionWebContentsObserver() override = default;

  bool SetWorkspaceTabId(int tab_id) {
    if (workspace_tab_id_)
      return *workspace_tab_id_ == tab_id;
    workspace_tab_id_ = tab_id;
    web_contents()->ForEachRenderFrameHost(
        [this](content::RenderFrameHost* frame) { SendWorkspaceTabId(frame); });
    return true;
  }

 private:
  friend class content::WebContentsUserData<LabExtensionWebContentsObserver>;
  explicit LabExtensionWebContentsObserver(content::WebContents* contents)
      : ExtensionWebContentsObserver(contents),
        content::WebContentsUserData<LabExtensionWebContentsObserver>(*contents),
        rules_helper_(contents) {
    registry_observation_.Observe(
        extensions::ExtensionRegistry::Get(contents->GetBrowserContext()));
  }

  void OnExtensionLoaded(BrowserContext*, const extensions::Extension*) override {
    FlushBackForwardCache();
  }

  void OnExtensionUnloaded(BrowserContext*, const extensions::Extension*,
                           extensions::UnloadedExtensionReason) override {
    FlushBackForwardCache();
  }

  void OnShutdown(extensions::ExtensionRegistry*) override {
    registry_observation_.Reset();
  }

  void FlushBackForwardCache() {
    if (auto* page = web_contents(); page && !page->IsBeingDestroyed())
      page->GetController().GetBackForwardCache().Flush();
  }

  void SendWorkspaceTabId(content::RenderFrameHost* frame) {
    if (workspace_tab_id_) {
      if (auto* local_frame = GetLocalFrame(frame))
        local_frame->SetTabId(*workspace_tab_id_);
    }
  }

  void RenderFrameCreated(content::RenderFrameHost* frame) override {
    ExtensionWebContentsObserver::RenderFrameCreated(frame);
    SendWorkspaceTabId(frame);
  }

  void ReadyToCommitNavigation(content::NavigationHandle* navigation) override {
    // The registry probe enables package navigation before activating its
    // worker. Script tracking must start after the renderer startup helper,
    // which supplies the renderer endpoint used by the user script loader.
    if (extensions::ExtensionSystem::Get(browser_context())->is_ready())
      ExtensionWebContentsObserver::ReadyToCommitNavigation(navigation);
  }

  void DidFinishNavigation(content::NavigationHandle* navigation) override {
    if (extensions::ExtensionSystem::Get(browser_context())->is_ready())
      ExtensionWebContentsObserver::DidFinishNavigation(navigation);
  }

  base::ScopedObservation<extensions::ExtensionRegistry,
                          extensions::ExtensionRegistryObserver>
      registry_observation_{this};
  std::optional<int> workspace_tab_id_;
  extensions::declarative_net_request::WebContentsHelper rules_helper_;
  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(LabExtensionWebContentsObserver);

class LabExtensionHostDelegate final : public extensions::ExtensionHostDelegate {
 public:
  void OnExtensionHostCreated(content::WebContents*) override {}
  void CreateTab(std::unique_ptr<content::WebContents>, const GURL&,
                 const extensions::ExtensionId&, WindowOpenDisposition,
                 const blink::mojom::WindowFeatures&, bool) override {}
  void ProcessMediaAccessRequest(
      content::WebContents*, const content::MediaStreamRequest&,
      content::MediaResponseCallback callback, const extensions::Extension*) override {
    std::move(callback).Run(blink::mojom::StreamDevicesSet(),
                            blink::mojom::MediaStreamRequestResult::PERMISSION_DENIED,
                            nullptr);
  }
  bool CheckMediaAccessPermission(
      content::RenderFrameHost*, const url::Origin&,
      blink::mojom::MediaStreamType, const extensions::Extension*) override {
    return false;
  }
  content::PictureInPictureResult EnterPictureInPicture(content::WebContents*) override {
    return content::PictureInPictureResult::kNotSupported;
  }
  void ExitPictureInPicture() override {}
};

class LabKioskDelegate final : public extensions::KioskDelegate {
 public:
  bool IsAutoLaunchedKioskApp(const extensions::ExtensionId&) const override {
    return false;
  }
};

base::DictValue NavigationLookupDetails(content::RenderFrameHost* frame) {
  auto details = NavigationDocumentDetails(frame);
  details.Remove("frameId");
  details.Set("url", frame->GetLastCommittedURL().spec());
  const auto* state = NavigationDocumentState::GetForCurrentDocument(frame);
  details.Set("errorOccurred", frame->IsErrorDocument() || (state && state->load_failed));
  return details;
}

class LabWebNavigationGetFrameFunction final : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("webNavigation.getFrame", WEBNAVIGATION_GETFRAME)

 private:
  ~LabWebNavigationGetFrameFunction() override = default;

  ResponseAction Run() override {
    EXTENSION_FUNCTION_VALIDATE(args().size() == 1 && args()[0].is_dict());
    const auto& details = args()[0].GetDict();
    const auto tab_id = details.FindInt("tabId");
    const auto frame_id = details.FindInt("frameId");
    const auto* document = details.FindString("documentId");
    using Map = extensions::ExtensionApiFrameIdMap;
    content::RenderFrameHost* frame = nullptr;
    content::WebContents* page = nullptr;
    if (document) {
      const auto id = Map::DocumentIdFromString(*document);
      if (!id)
        return RespondNow(Error("Invalid documentId."));
      frame = Map::Get()->GetRenderFrameHostByDocumentId(id);
      page = frame ? content::WebContents::FromRenderFrameHost(frame) : nullptr;
      const auto* tab = page ? WorkspaceExtensionTab::FromWebContents(page) : nullptr;
      if (!tab || page->GetBrowserContext() != browser_context() ||
          (tab_id && *tab_id != tab->id()) ||
          (frame_id && *frame_id != Map::GetFrameId(frame))) {
        return RespondNow(WithArguments(base::Value()));
      }
    } else {
      if (!tab_id || !frame_id)
        return RespondNow(Error("Either documentId or both tabId and frameId must be specified."));
      page = WorkspaceExtensionTab::Find(browser_context(), *tab_id);
      frame = page ? Map::GetRenderFrameHostById(page, *frame_id) : nullptr;
    }
    if (!frame || !page || browser_context()->IsOffTheRecord() ||
        !CanExposeNavigationFrame(*frame, *page)) {
      return RespondNow(WithArguments(base::Value()));
    }
    return RespondNow(WithArguments(NavigationLookupDetails(frame)));
  }
};

class LabWebNavigationGetAllFramesFunction final : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("webNavigation.getAllFrames", WEBNAVIGATION_GETALLFRAMES)

 private:
  ~LabWebNavigationGetAllFramesFunction() override = default;

  ResponseAction Run() override {
    EXTENSION_FUNCTION_VALIDATE(args().size() == 1 && args()[0].is_dict());
    const auto tab_id = args()[0].GetDict().FindInt("tabId");
    EXTENSION_FUNCTION_VALIDATE(tab_id);
    auto* page = WorkspaceExtensionTab::Find(browser_context(), *tab_id);
    if (!page || browser_context()->IsOffTheRecord())
      return RespondNow(WithArguments(base::Value()));
    base::ListValue frames;
    page->ForEachRenderFrameHostWithAction([&](content::RenderFrameHost* frame) {
      using Action = content::RenderFrameHost::FrameIterationAction;
      if (!IsWorkspaceNavigationFrame(*frame, *page))
        return Action::kSkipChildren;
      if (!frame->GetLastCommittedURL().SchemeIsHTTPOrHTTPS())
        return Action::kContinue;
      auto details = NavigationLookupDetails(frame);
      details.Set("frameId", extensions::ExtensionApiFrameIdMap::GetFrameId(frame));
      details.Set("processId", frame->GetProcess()->GetDeprecatedID());
      frames.Append(std::move(details));
      return Action::kContinue;
    });
    return RespondNow(WithArguments(std::move(frames)));
  }
};

class LabTabsQueryFunction final : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("tabs.query", TABS_QUERY)

 private:
  ~LabTabsQueryFunction() override = default;

  ResponseAction Run() override {
    EXTENSION_FUNCTION_VALIDATE(extension() && args().size() == 1 && args()[0].is_dict());
    const auto& query = args()[0].GetDict();
    extensions::URLPatternSet urls;
    if (const auto* filter = query.Find("url")) {
      std::vector<std::string> patterns;
      if (const auto* pattern = filter->GetIfString()) {
        patterns.push_back(*pattern);
      } else if (const auto* entries = filter->GetIfList()) {
        for (const auto& entry : *entries) {
          EXTENSION_FUNCTION_VALIDATE(entry.is_string());
          patterns.push_back(entry.GetString());
        }
      } else {
        EXTENSION_FUNCTION_VALIDATE(false);
      }
      std::string error;
      if (!urls.Populate(patterns, URLPattern::SCHEME_ALL, true, &error))
        return RespondNow(Error(std::move(error)));
    }

    const auto matches = [&query](std::string_view key, bool value) {
      return query.FindBool(key).value_or(value) == value;
    };
    const int window_id = query.FindInt("windowId").value_or(-1);
    const auto* window_type = query.FindString("windowType");
    base::ListValue result;
    if (!matches("currentWindow", true) || !matches("lastFocusedWindow", true) ||
        (window_id >= 0 && window_id != kWorkspaceExtensionWindowId) ||
        (window_type && *window_type != "normal")) {
      return RespondNow(WithArguments(std::move(result)));
    }

    const auto* title = query.FindString("title");
    const auto* status = query.FindString("status");
    for (auto* tab : WorkspaceExtensionTab::InProfile(TabContextForExtensionFunction(*this))) {
      if (!matches("active", tab->active()) ||
          !matches("highlighted", tab->highlighted()) ||
          !matches("pinned", tab->pinned()) || !matches("audible", tab->audible()) ||
          !matches("muted", tab->muted()) || !matches("discarded", tab->discarded()) ||
          query.FindInt("index").value_or(tab->index()) != tab->index() ||
          (status && *status != tab->status())) {
        continue;
      }
      const auto& url = tab->url();
      const auto* pending = tab->pending_url();
      const bool can_read_current =
          CanReadExtensionTabUrl(*extension(), url, tab->id());
      const bool can_read_pending =
          pending && CanReadExtensionTabUrl(*extension(), *pending, tab->id());
      if ((title && !title->empty() &&
           (!can_read_current ||
            !base::MatchPattern(tab->CommittedTitle(), base::UTF8ToUTF16(*title)))) ||
          (!urls.is_empty() && !(can_read_current && urls.MatchesURL(url)) &&
           !(can_read_pending && urls.MatchesURL(*pending)))) {
        continue;
      }

      result.Append(tab->MetadataFor(*extension()));
    }
    return RespondNow(WithArguments(std::move(result)));
  }
};

bool ParseExtensionTabUrl(const base::DictValue& options, GURL& url) {
  const auto* input = options.FindString("url");
  if (!input)
    return true;
  url = GURL(*input);
  return url.is_valid() && url.SchemeIsHTTPOrHTTPS() && !url.has_username() &&
         !url.has_password() && url.spec().size() <= 32768;
}

// Accepts HTTP(S) URLs without credentials and the extension's own pages.
bool ResolveExtensionTabUrl(const extensions::Extension& extension, const std::string& input,
                            GURL& url) {
  if (ParseExtensionTabUrl(base::DictValue().Set("url", input), url))
    return true;
  url = url.has_scheme() ? url : extension.ResolveExtensionURL(input);
  if (url.SchemeIs(extensions::kExtensionScheme) && url.host() == extension.guid()) {
    GURL::Replacements canonical_host;
    canonical_host.SetHostStr(extension.id());
    url = url.ReplaceComponents(canonical_host);
  }
  return url.is_valid() && url.SchemeIs(extensions::kExtensionScheme) &&
         url::Origin::Create(url) == extension.origin() && !url.has_username() &&
         !url.has_password() && url.spec().size() <= 32768 &&
         !extensions::file_util::ExtensionURLToRelativeFilePath(url).empty();
}

class LabTabsCreateFunction final : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("tabs.create", TABS_CREATE)

 private:
  ~LabTabsCreateFunction() override = default;

  ResponseAction Run() override {
    EXTENSION_FUNCTION_VALIDATE(extension() && args().size() == 1 && args()[0].is_dict());
    const auto& options = args()[0].GetDict();
    const int window_id = options.FindInt("windowId").value_or(kWorkspaceExtensionWindowId);
    if (window_id != kWorkspaceExtensionWindowId &&
        window_id != extension_misc::kCurrentWindowId)
      return RespondNow(Error("No matching Workspace window."));
    GURL url;
    if (!ParseExtensionTabUrl(options, url)) {
      const std::string& input = *options.FindString("url");
      url = url.has_scheme() ? url : extension()->ResolveExtensionURL(input);
      if (url.SchemeIs(extensions::kExtensionScheme) &&
          url.host() == extension()->guid()) {
        GURL::Replacements canonical_host;
        canonical_host.SetHostStr(extension()->id());
        url = url.ReplaceComponents(canonical_host);
      }
      if (!url.is_valid() || !url.SchemeIs(extensions::kExtensionScheme) ||
          url::Origin::Create(url) != extension()->origin() ||
          url.has_username() || url.has_password() ||
          url.spec().size() > 32768 ||
          extensions::file_util::ExtensionURLToRelativeFilePath(url).empty()) {
        return RespondNow(Error(
            "Only HTTP and HTTPS URLs without credentials and the calling "
            "extension's own resource URLs are supported."));
      }
    }
    const auto tabs = WorkspaceExtensionTab::InProfile(browser_context());
    auto* created = tabs.empty() ? nullptr : tabs.front()->workspace().CreateExtensionTab(
        *browser_context(), url, options.FindBool("active").value_or(true));
    return created ? RespondNow(WithArguments(created->MetadataFor(*extension())))
                   : RespondNow(Error("Could not create a Workspace tab."));
  }
};

class LabTabsUpdateFunction final : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("tabs.update", TABS_UPDATE)

 private:
  ~LabTabsUpdateFunction() override = default;

  ResponseAction Run() override {
    EXTENSION_FUNCTION_VALIDATE(extension() && !args().empty() && args().size() <= 2 &&
                                args().back().is_dict());
    const auto& options = args().back().GetDict();
    GURL url;
    if (!ParseExtensionTabUrl(options, url))
      return RespondNow(Error("Only HTTP and HTTPS tab URLs without credentials are supported."));
    WorkspaceExtensionTab* target = nullptr;
    if (args().size() == 2 && !args()[0].is_none()) {
      EXTENSION_FUNCTION_VALIDATE(args()[0].is_int());
      target = WorkspaceExtensionTab::FindTab(TabContextForExtensionFunction(*this), args()[0].GetInt());
    } else {
      target = WorkspaceExtensionTab::ActiveInProfile(TabContextForExtensionFunction(*this));
    }
    if (!target)
      return RespondNow(Error("No matching tab in this profile."));
    if (!target->workspace().UpdateExtensionTab(
            *target, url, options.FindBool("active").value_or(false),
            options.FindBool("muted"), extension()->id())) {
      return RespondNow(Error("Could not update the Workspace tab."));
    }
    return RespondNow(WithArguments(target->MetadataFor(*extension())));
  }
};

class LabTabsRemoveFunction final : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("tabs.remove", TABS_REMOVE)

 private:
  ~LabTabsRemoveFunction() override = default;

  ResponseAction Run() override {
    EXTENSION_FUNCTION_VALIDATE(extension() && args().size() == 1);
    std::set<int> ids;
    const auto& requested = args()[0];
    if (requested.is_int()) {
      ids.insert(requested.GetInt());
    } else {
      EXTENSION_FUNCTION_VALIDATE(requested.is_list());
      for (const auto& id : requested.GetList()) {
        EXTENSION_FUNCTION_VALIDATE(id.is_int());
        ids.insert(id.GetInt());
      }
    }
    if (ids.empty())
      return RespondNow(NoArguments());
    std::vector<WorkspaceExtensionTab*> targets;
    for (int id : ids) {
      auto* target = WorkspaceExtensionTab::FindTab(browser_context(), id);
      if (!target)
        return RespondNow(Error("No matching tab in this profile."));
      targets.push_back(target);
    }
    targets.front()->workspace().RemoveExtensionTabs(
        targets, base::BindOnce(&LabTabsRemoveFunction::OnRemoved, this));
    return did_respond() ? AlreadyResponded() : RespondLater();
  }

  void OnRemoved(bool success) {
    Respond(success ? NoArguments() : Error("Workspace tab closure was cancelled."));
  }
};

class LabTabsMoveFunction final : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("tabs.move", TABS_MOVE)

 private:
  ~LabTabsMoveFunction() override = default;

  ResponseAction Run() override {
    EXTENSION_FUNCTION_VALIDATE(extension() && args().size() == 2 && args()[1].is_dict());
    const auto& options = args()[1].GetDict();
    const int window_id = options.FindInt("windowId").value_or(kWorkspaceExtensionWindowId);
    if (window_id != kWorkspaceExtensionWindowId &&
        window_id != extension_misc::kCurrentWindowId) {
      return RespondNow(Error("No matching Workspace window."));
    }
    const auto index = options.FindInt("index");
    EXTENSION_FUNCTION_VALIDATE(index && *index >= -1);
    std::vector<int> ids;
    const auto& requested = args()[0];
    if (requested.is_int()) {
      ids.push_back(requested.GetInt());
    } else {
      EXTENSION_FUNCTION_VALIDATE(requested.is_list());
      for (const auto& id : requested.GetList()) {
        EXTENSION_FUNCTION_VALIDATE(id.is_int());
        ids.push_back(id.GetInt());
      }
    }
    if (ids.empty())
      return RespondNow(Error("No tabs given."));
    std::set<int> seen;
    std::vector<WorkspaceExtensionTab*> targets;
    std::vector<int> original_indices;
    for (int id : ids) {
      auto* tab = WorkspaceExtensionTab::FindTab(browser_context(), id);
      if (!tab)
        return RespondNow(Error("No matching tab in this profile."));
      if (!seen.insert(id).second)
        return RespondNow(Error("A tab can only be moved once in a batch."));
      targets.push_back(tab);
      original_indices.push_back(tab->index());
    }
    if (!targets.front()->workspace().MoveExtensionTabs(
            *browser_context(), targets, *index)) {
      return RespondNow(Error("Could not move the Workspace tabs."));
    }
    base::ListValue result;
    for (size_t i = 0; i < targets.size(); ++i) {
      targets[i]->NotifyMoved(original_indices[i]);
      result.Append(targets[i]->MetadataFor(*extension()));
    }
    if (result.size() == 1)
      return RespondNow(WithArguments(std::move(result[0])));
    return RespondNow(WithArguments(std::move(result)));
  }
};

class LabTabsReloadFunction final : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("tabs.reload", TABS_RELOAD)

 private:
  ~LabTabsReloadFunction() override = default;

  ResponseAction Run() override {
    EXTENSION_FUNCTION_VALIDATE(extension() && args().size() <= 2);
    std::optional<int> tab_id;
    const base::DictValue* options = nullptr;
    if (!args().empty()) {
      if (args()[0].is_int()) {
        tab_id = args()[0].GetInt();
      } else if (args().size() == 1 && args()[0].is_dict()) {
        options = &args()[0].GetDict();
      } else {
        EXTENSION_FUNCTION_VALIDATE(args()[0].is_none());
      }
    }
    if (args().size() == 2 && !args()[1].is_none()) {
      EXTENSION_FUNCTION_VALIDATE(args()[1].is_dict());
      options = &args()[1].GetDict();
    }
    auto* target = tab_id ? WorkspaceExtensionTab::FindTab(TabContextForExtensionFunction(*this), *tab_id)
                          : WorkspaceExtensionTab::ActiveInProfile(TabContextForExtensionFunction(*this));
    if (!target)
      return RespondNow(Error("No matching tab in this profile."));
    const bool bypass_cache = options && options->FindBool("bypassCache").value_or(false);
    if (!target->workspace().ReloadExtensionTab(*target, bypass_cache))
      return RespondNow(Error("Could not reload the Workspace tab."));
    return RespondNow(NoArguments());
  }
};

class LabTabsDuplicateFunction final : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("tabs.duplicate", TABS_DUPLICATE)

 private:
  ~LabTabsDuplicateFunction() override = default;

  ResponseAction Run() override {
    EXTENSION_FUNCTION_VALIDATE(extension() && args().size() == 1 && args()[0].is_int());
    auto* target = WorkspaceExtensionTab::FindTab(browser_context(), args()[0].GetInt());
    if (!target)
      return RespondNow(Error("No matching tab in this profile."));
    auto* clone = target->workspace().DuplicateExtensionTab(*target);
    if (!clone)
      return RespondNow(Error("Could not duplicate the Workspace tab."));
    return RespondNow(WithArguments(clone->MetadataFor(*extension())));
  }
};

class LabTabsHistoryFunction : public ExtensionFunction {
 protected:
  explicit LabTabsHistoryFunction(bool forward) : forward_(forward) {}
  ~LabTabsHistoryFunction() override = default;

 private:
  ResponseAction Run() override {
    EXTENSION_FUNCTION_VALIDATE(extension() && args().size() <= 1);
    WorkspaceExtensionTab* target = nullptr;
    if (args().empty() || args()[0].is_none()) {
      target = WorkspaceExtensionTab::ActiveInProfile(browser_context());
    } else {
      EXTENSION_FUNCTION_VALIDATE(args()[0].is_int());
      target = WorkspaceExtensionTab::FindTab(browser_context(), args()[0].GetInt());
    }
    if (!target)
      return RespondNow(Error("No matching tab in this profile."));
    if (!target->workspace().NavigateExtensionTabHistory(*target, forward_)) {
      return RespondNow(Error(forward_ ? "No usable forward entry in the Workspace tab."
                                      : "No usable back entry in the Workspace tab."));
    }
    return RespondNow(NoArguments());
  }

  const bool forward_;
};

class LabTabsGoBackFunction final : public LabTabsHistoryFunction {
 public:
  LabTabsGoBackFunction() : LabTabsHistoryFunction(false) {}
  DECLARE_EXTENSION_FUNCTION("tabs.goBack", TABS_GOBACK)

 private:
  ~LabTabsGoBackFunction() override = default;
};

class LabTabsGoForwardFunction final : public LabTabsHistoryFunction {
 public:
  LabTabsGoForwardFunction() : LabTabsHistoryFunction(true) {}
  DECLARE_EXTENSION_FUNCTION("tabs.goForward", TABS_GOFORWARD)

 private:
  ~LabTabsGoForwardFunction() override = default;
};

class LabTabsGetFunction final : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("tabs.get", TABS_GET)

 private:
  ~LabTabsGetFunction() override = default;

  ResponseAction Run() override {
    EXTENSION_FUNCTION_VALIDATE(extension() && args().size() == 1 && args()[0].is_int());
    const auto* tab = WorkspaceExtensionTab::FindTab(TabContextForExtensionFunction(*this), args()[0].GetInt());
    return tab ? RespondNow(WithArguments(tab->MetadataFor(*extension())))
               : RespondNow(Error("No matching tab in this profile."));
  }
};

class LabTabsGetCurrentFunction final : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("tabs.getCurrent", TABS_GETCURRENT)

 private:
  ~LabTabsGetCurrentFunction() override = default;

  ResponseAction Run() override {
    EXTENSION_FUNCTION_VALIDATE(extension() && args().empty());
    const auto* tab = WorkspaceExtensionTab::FromWebContents(GetSenderWebContents());
    return tab ? RespondNow(WithArguments(tab->MetadataFor(*extension())))
               : RespondNow(NoArguments());
  }
};

class LabTabsDiscardFunction final : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("tabs.discard", TABS_DISCARD)

 private:
  ~LabTabsDiscardFunction() override = default;

  ResponseAction Run() override {
    EXTENSION_FUNCTION_VALIDATE(extension() && args().size() <= 1);
    WorkspaceExtensionTab* target = nullptr;
    WorkspaceLab* workspace = nullptr;
    if (args().empty() || args()[0].is_none()) {
      const auto tabs = WorkspaceExtensionTab::InProfile(browser_context());
      if (!tabs.empty())
        workspace = &tabs.front()->workspace();
    } else {
      EXTENSION_FUNCTION_VALIDATE(args()[0].is_int());
      target = WorkspaceExtensionTab::FindTab(browser_context(), args()[0].GetInt());
      if (!target)
        return RespondNow(Error("No matching tab in this profile."));
      workspace = &target->workspace();
    }
    auto* discarded = workspace ? workspace->DiscardExtensionTab(*browser_context(), target)
                                : nullptr;
    if (!discarded)
      return RespondNow(Error("No eligible Workspace tab to discard."));
    return RespondNow(WithArguments(discarded->MetadataFor(*extension())));
  }
};

base::FilePath OptionalGrantsPath(BrowserContext& context,
                                  const extensions::Extension& extension) {
  const auto directory = extension.path().DirName();
  if (!IsManagedExtensionSession() || context.IsOffTheRecord() ||
      extension.public_key().empty() ||
      directory != context.GetPath().AppendASCII(extensions::kInstallDirectoryName)
                       .AppendASCII(extension.id())) {
    return {};
  }
  return directory.AppendASCII(kExtensionOptionalGrants);
}

bool IsSupportedOptionalHostGrant(const URLPattern& pattern) {
  // A wildcard-scheme declaration allows requests for narrower patterns.
  // Optional all-host grants must name the HTTP or HTTPS scheme explicitly.
  return pattern.GetAsString() != "*://*/*" &&
         IsSupportedExtensionHostGrant(pattern);
}

bool SaveOptionalGrants(BrowserContext& context,
                        const extensions::Extension& extension,
                        const extensions::PermissionSet& active) {
  const auto path = OptionalGrantsPath(context, extension);
  if (path.empty())
    return true;
  const auto& required = extensions::PermissionsParser::GetRequiredPermissions(&extension);
  const auto& declared = extensions::PermissionsParser::GetOptionalPermissions(&extension);
  extensions::URLPatternSet hosts;
  for (const auto& pattern : active.explicit_hosts()) {
    if (IsSupportedOptionalHostGrant(pattern) &&
        declared.explicit_hosts().ContainsPattern(pattern) &&
        !required.explicit_hosts().ContainsPattern(pattern)) {
      hosts.AddPattern(pattern);
    }
  }
  extensions::APIPermissionSet apis;
  extensions::APIPermissionSet::Intersection(active.apis(), declared.apis(), &apis);
  extensions::APIPermissionSet optional_apis;
  extensions::APIPermissionSet::Difference(apis, required.apis(), &optional_apis);
  const extensions::PermissionSet optional(std::move(optional_apis), {}, std::move(hosts), {});
  auto packed = extensions::permissions_api_helpers::PackPermissionSet(optional);
  base::DictValue record;
  record.Set("public_key", extension.public_key());
  record.Set("permissions", packed->ToValue());
  const auto bytes = base::WriteJson(record);
  base::ScopedAllowBlockingForTesting allow_profile_io;
  return bytes && bytes->size() <= 64 * 1024 && !base::IsLink(path) &&
         base::ImportantFileWriter::WriteFileAtomically(path, *bytes);
}

void RestoreOptionalGrants(BrowserContext& context,
                           const extensions::Extension& extension) {
  const auto path = OptionalGrantsPath(context, extension);
  if (path.empty())
    return;
  base::ScopedAllowBlockingForTesting allow_profile_io;
  std::string bytes;
  if (base::IsLink(path) || !base::PathExists(path))
    return;
#if BUILDFLAG(IS_POSIX)
  base::stat_wrapper_t info;
  if (base::File::Lstat(path, &info) != 0 || !S_ISREG(info.st_mode))
    return;
#endif
  if (!base::ReadFileToStringWithMaxSize(path, &bytes, 64 * 1024))
    return;
  const auto record = base::JSONReader::ReadDict(bytes, base::JSON_PARSE_RFC);
  const auto* key = record ? record->FindString("public_key") : nullptr;
  const auto* permissions = record ? record->FindDict("permissions") : nullptr;
  if (!key || *key != extension.public_key() || !permissions)
    return;
  const auto packed = extensions::api::permissions::Permissions::FromValue(*permissions);
  if (!packed)
    return;
  using extensions::PermissionSet;
  const auto& declared =
      extensions::PermissionsParser::GetOptionalPermissions(&extension);
  std::string error;
  auto parsed = extensions::permissions_api_helpers::UnpackPermissionSet(
      *packed, extensions::PermissionsParser::GetRequiredPermissions(&extension),
      declared, /*allow_file_access=*/false, &error);
  if (!parsed)
    return;
  extensions::URLPatternSet bounded_hosts;
  for (const auto& pattern : parsed->optional_explicit_hosts) {
    if (IsSupportedOptionalHostGrant(pattern))
      bounded_hosts.AddPattern(pattern);
  }
  const bool trimmed_hosts = bounded_hosts != parsed->optional_explicit_hosts;
  const PermissionSet optional(std::move(parsed->optional_apis), {},
                               std::move(bounded_hosts), {});
  if (!declared.Contains(optional))
    return;
  const auto* data = extension.permissions_data();
  auto active = PermissionSet::CreateUnion(data->active_permissions(), optional);
  // Prune grants dropped by a newer manifest before it can run.
  if ((trimmed_hosts || !parsed->unlisted_apis.empty() || !parsed->unlisted_hosts.is_empty() ||
       !parsed->required_apis.empty() || !parsed->required_explicit_hosts.is_empty() ||
       !parsed->required_scriptable_hosts.is_empty() ||
       !parsed->restricted_file_scheme_patterns.is_empty()) &&
      !SaveOptionalGrants(context, extension, *active)) {
    return;
  }
  if (!optional.IsEmpty())
    data->SetPermissions(std::move(active), data->withheld_permissions().Clone());
}

class LabIsAllowedIncognitoAccessFunction final : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("extension.isAllowedIncognitoAccess",
                             EXTENSION_ISALLOWEDINCOGNITOACCESS)

 private:
  ~LabIsAllowedIncognitoAccessFunction() override = default;

  ResponseAction Run() override {
    return RespondNow(WithArguments(
        extensions::ExtensionsBrowserClient::Get()->IsExtensionIncognitoEnabled(
            extension(), browser_context())));
  }
};

class LabPermissionsRequestFunction final : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("permissions.request", PERMISSIONS_REQUEST)

 private:
  ~LabPermissionsRequestFunction() override = default;

  ResponseAction Run() override {
    if (!user_gesture())
      return RespondNow(Error("This function must be called during a user gesture"));

    using extensions::PermissionSet;
    using extensions::PermissionsParser;
    auto params = extensions::api::permissions::Request::Params::Create(args());
    EXTENSION_FUNCTION_VALIDATE(params && extension());
    const auto& declared = PermissionsParser::GetOptionalPermissions(extension());
    std::string error;
    auto requested = extensions::permissions_api_helpers::UnpackPermissionSet(
        params->permissions, PermissionsParser::GetRequiredPermissions(extension()),
        declared, /*allow_file_access=*/false, &error);
    if (!requested)
      return RespondNow(Error(std::move(error)));
    if (!requested->unlisted_apis.empty() || !requested->unlisted_hosts.is_empty() ||
        !requested->restricted_file_scheme_patterns.is_empty()) {
      return RespondNow(Error("Only permissions listed in the manifest can be requested."));
    }

    const auto& active = extension()->permissions_data()->active_permissions();
    const PermissionSet required(std::move(requested->required_apis), {},
                                 std::move(requested->required_explicit_hosts),
                                 std::move(requested->required_scriptable_hosts));
    const PermissionSet optional(std::move(requested->optional_apis), {},
                                 std::move(requested->optional_explicit_hosts), {});
    if (!declared.Contains(optional) || !active.Contains(required) ||
        !std::ranges::all_of(optional.explicit_hosts(), IsSupportedOptionalHostGrant)) {
      return RespondNow(Error("The requested access is outside the supported scope."));
    }
    auto pending = PermissionSet::CreateDifference(optional, active);
    if (pending->IsEmpty())
      return RespondNow(WithArguments(true));

    const auto tabs = WorkspaceExtensionTab::InProfile(browser_context());
    if (tabs.empty())
      return RespondNow(WithArguments(false));
    auto* permissions = pending.get();
    tabs.front()->workspace().PromptExtensionPermissions(
        *browser_context(), *extension(), *permissions, GetSenderWebContents(),
        base::BindOnce(&LabPermissionsRequestFunction::OnDecision, this,
                       std::move(pending)));
    return did_respond() ? AlreadyResponded() : RespondLater();
  }

  void OnDecision(std::unique_ptr<const extensions::PermissionSet> permissions,
                  bool allowed) {
    auto* context = browser_context();
    auto* registry = context && !context->ShutdownStarted()
                         ? extensions::ExtensionRegistry::Get(context)
                         : nullptr;
    if (!allowed || !registry ||
        registry->enabled_extensions().GetByID(extension()->id()) != extension()) {
      Respond(WithArguments(false));
      return;
    }
    const auto active = extensions::PermissionSet::CreateUnion(
        extension()->permissions_data()->active_permissions(), *permissions);
    if (!SaveOptionalGrants(*context, *extension(), *active)) {
      Respond(Error("Could not save optional permissions for this profile."));
      return;
    }
    extensions::PermissionsUpdater(context).GrantOptionalPermissions(
        *extension(), *permissions,
        base::BindOnce(&LabPermissionsRequestFunction::OnGranted, this));
  }

  void OnGranted() { Respond(WithArguments(true)); }
};

class LabPermissionsRemoveFunction final : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("permissions.remove", PERMISSIONS_REMOVE)

 private:
  ~LabPermissionsRemoveFunction() override = default;

  ResponseAction Run() override {
    if (browser_context()->IsOffTheRecord())
      return RespondNow(Error("Change optional permissions in the regular profile."));
    using extensions::PermissionSet;
    using extensions::PermissionsParser;
    auto params = extensions::api::permissions::Remove::Params::Create(args());
    EXTENSION_FUNCTION_VALIDATE(params && extension());
    const auto& declared = PermissionsParser::GetOptionalPermissions(extension());
    std::string error;
    auto parsed = extensions::permissions_api_helpers::UnpackPermissionSet(
        params->permissions, PermissionsParser::GetRequiredPermissions(extension()),
        declared, /*allow_file_access=*/false, &error);
    if (!parsed)
      return RespondNow(Error(std::move(error)));
    if (!parsed->unlisted_apis.empty() || !parsed->unlisted_hosts.is_empty() ||
        !parsed->restricted_file_scheme_patterns.is_empty()) {
      return RespondNow(Error("Only permissions listed in the manifest can be removed."));
    }
    if (!parsed->required_apis.empty() || !parsed->required_explicit_hosts.is_empty() ||
        !parsed->required_scriptable_hosts.is_empty()) {
      return RespondNow(Error("You cannot remove required permissions."));
    }
    const PermissionSet requested(std::move(parsed->optional_apis), {},
                                  std::move(parsed->optional_explicit_hosts), {});
    if (!declared.Contains(requested))
      return RespondNow(Error("The requested access is outside the supported scope."));
    const auto& active = extension()->permissions_data()->active_permissions();
    auto removed = PermissionSet::CreateIntersection(active, requested);
    if (removed->IsEmpty())
      return RespondNow(WithArguments(true));
    const auto remaining = PermissionSet::CreateDifference(active, *removed);
    if (!SaveOptionalGrants(*browser_context(), *extension(), *remaining))
      return RespondNow(Error("Could not save optional permissions for this profile."));
    extensions::PermissionsUpdater(browser_context()).RevokeOptionalPermissions(
        *extension(), *removed, extensions::PermissionsUpdater::RemoveType::kSoft,
        base::BindOnce(&LabPermissionsRemoveFunction::OnRemoved, this));
    return did_respond() ? AlreadyResponded() : RespondLater();
  }

  void OnRemoved() {
    if (auto* context = browser_context();
        context && !extension()->permissions_data()->HasAPIPermission(
                       extensions::mojom::APIPermissionID::kPrivacy) &&
        extensions::ExtensionPrefs::Get(context)->ReadPrefAsDictionary(
            extension_id(), kWorkspacePrivacyPref)) {
      SetExtensionPasswordSaving(*context, extension_id(), std::nullopt);
    }
    Respond(WithArguments(true));
  }
};

class LabRuntimeSetUninstallUrlFunction final : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("runtime.setUninstallURL", RUNTIME_SETUNINSTALLURL)

 private:
  ~LabRuntimeSetUninstallUrlFunction() override = default;

  ResponseAction Run() override {
    EXTENSION_FUNCTION_VALIDATE(args().size() == 1 && args()[0].is_string());
    auto* context = browser_context();
    if (!extension() || context->IsOffTheRecord() ||
        !CanLoadExtensionProbeResources(*context, extension()->id()) ||
        extensions::ExtensionRegistry::Get(context)->enabled_extensions().GetByID(
            extension()->id()) != extension()) {
      return RespondNow(Error("Extension is unavailable in this profile."));
    }
    if (!SetWorkspaceExtensionUninstallUrl(context->GetPath(), *extension(),
                                           args()[0].GetString())) {
      return RespondNow(Error("Could not save the uninstall URL."));
    }
    return RespondNow(NoArguments());
  }
};

base::DictValue ReadSidePanelOptions(content::BrowserContext* context,
                                    const extensions::Extension& extension,
                                    const WorkspaceExtensionTab* tab) {
  base::DictValue options;
  if (const auto* path = extension.manifest()->FindStringPath("side_panel.default_path"))
    options.Set("path", *path);
  if (const auto* prefs = extensions::ExtensionPrefs::Get(context)
                              ->ReadPrefAsDictionary(extension.id(), kWorkspaceSidePanelPref)) {
    if (const auto* path = prefs->FindString("path"))
      options.Set("path", *path);
    if (const auto enabled = prefs->FindBool("enabled"))
      options.Set("enabled", *enabled);
  }
  if (tab) {
    if (const auto* specific = tab->SidePanelOptions(extension.id()))
      options.Merge(specific->Clone());
    options.Set("tabId", tab->id());
  }
  if (!options.contains("enabled"))
    options.Set("enabled", options.contains("path"));
  return options;
}

class LabSidePanelFunction : public ExtensionFunction {
 protected:
  ~LabSidePanelFunction() override = default;

  ResponseAction Run() override {
    auto* context = browser_context();
    if (!extension() || context->IsOffTheRecord())
      return RespondNow(Error("The side panel is unavailable in this profile."));
    const std::string_view operation = name();
    auto* prefs = extensions::ExtensionPrefs::Get(context);
    const auto& id = extension_id();
    if (operation == "sidePanel.getLayout")
      return RespondNow(WithArguments(base::DictValue().Set("side", "right")));
    const auto* stored = prefs->ReadPrefAsDictionary(id, kWorkspaceSidePanelPref);
    if (operation == "sidePanel.getPanelBehavior") {
      return RespondNow(WithArguments(base::DictValue().Set(
          "openPanelOnActionClick",
          stored && stored->FindBool("openPanelOnActionClick").value_or(false))));
    }
    EXTENSION_FUNCTION_VALIDATE(args().size() == 1 && args()[0].is_dict());
    const auto& input = args()[0].GetDict();
    if (operation == "sidePanel.setPanelBehavior") {
      auto updated = stored ? stored->Clone() : base::DictValue();
      if (const auto enabled = input.FindBool("openPanelOnActionClick")) {
        updated.Set("openPanelOnActionClick", *enabled);
        prefs->UpdateExtensionPref(id, kWorkspaceSidePanelPref.name, base::Value(std::move(updated)));
      }
      return RespondNow(NoArguments());
    }

    const auto tab_id = input.FindInt("tabId");
    auto* tab = tab_id ? WorkspaceExtensionTab::FindTab(context, *tab_id) : nullptr;
    if (tab_id && !tab)
      return RespondNow(Error("No matching tab in this profile."));
    if (operation == "sidePanel.getOptions")
      return RespondNow(WithArguments(ReadSidePanelOptions(context, *extension(), tab)));
    if (operation == "sidePanel.setOptions") {
      const auto* previous = tab ? tab->SidePanelOptions(id) : stored;
      auto updated = previous ? previous->Clone() : base::DictValue();
      if (const auto* path = input.FindString("path")) {
        if (!ExtensionArchiveResourcePath(*path))
          return RespondNow(Error("The side panel path must be a packaged resource."));
        updated.Set("path", *path);
      }
      if (const auto enabled = input.FindBool("enabled"))
        updated.Set("enabled", *enabled);
      if (tab) {
        tab->SetSidePanelOptions(id, std::move(updated));
        if (!ReadSidePanelOptions(context, *extension(), tab).FindBool("enabled").value_or(false))
          tab->workspace().SetExtensionSidePanel(*tab, id, GURL());
      } else {
        prefs->UpdateExtensionPref(id, kWorkspaceSidePanelPref.name, base::Value(std::move(updated)));
      }
      return RespondNow(NoArguments());
    }

    const auto window = input.FindInt("windowId");
    if ((!tab && !window) ||
        (window && *window != kWorkspaceExtensionWindowId && *window != extension_misc::kCurrentWindowId)) {
      return RespondNow(Error("A tab or window in this profile is required."));
    }
    if (!tab)
      tab = WorkspaceExtensionTab::ActiveInProfile(context);
    if (!tab)
      return RespondNow(Error("No active tab in this profile."));
    if (operation == "sidePanel.close") {
      tab->workspace().SetExtensionSidePanel(*tab, id, GURL());
      return RespondNow(NoArguments());
    }
    if (!user_gesture())
      return RespondNow(Error("Opening a side panel requires a user gesture."));
    auto options = ReadSidePanelOptions(context, *extension(), tab_id ? tab : nullptr);
    const auto* path = options.FindString("path");
    if (!path || !options.FindBool("enabled").value_or(false) ||
        !ExtensionArchiveResourcePath(*path) ||
        !tab->workspace().SetExtensionSidePanel(*tab, id, extension()->GetResourceURL(*path))) {
      return RespondNow(Error("The side panel is unavailable for this tab."));
    }
    return RespondNow(NoArguments());
  }
};

#define ZITTER_SIDE_PANEL_FUNCTION(Class, Name, Histogram) \
  class Class final : public LabSidePanelFunction {        \
   public:                                                 \
    DECLARE_EXTENSION_FUNCTION("sidePanel." Name, Histogram) \
   private:                                                \
    ~Class() override = default;                           \
  }

ZITTER_SIDE_PANEL_FUNCTION(LabSidePanelGetOptionsFunction, "getOptions", SIDEPANEL_GETOPTIONS);
ZITTER_SIDE_PANEL_FUNCTION(LabSidePanelSetOptionsFunction, "setOptions", SIDEPANEL_SETOPTIONS);
ZITTER_SIDE_PANEL_FUNCTION(LabSidePanelOpenFunction, "open", SIDEPANEL_OPEN);
ZITTER_SIDE_PANEL_FUNCTION(LabSidePanelCloseFunction, "close", SIDEPANEL_CLOSE);
ZITTER_SIDE_PANEL_FUNCTION(LabSidePanelGetLayoutFunction, "getLayout", SIDEPANEL_GETLAYOUT);
ZITTER_SIDE_PANEL_FUNCTION(LabSidePanelGetBehaviorFunction, "getPanelBehavior", SIDEPANEL_GETPANELBEHAVIOR);
ZITTER_SIDE_PANEL_FUNCTION(LabSidePanelSetBehaviorFunction, "setPanelBehavior", SIDEPANEL_SETPANELBEHAVIOR);
#undef ZITTER_SIDE_PANEL_FUNCTION

base::DictValue WorkspaceExtensionWindow(
    BrowserContext& context,
    const std::vector<WorkspaceExtensionTab*>& tabs,
    const extensions::Extension& extension,
    bool populate) {
  auto result = base::DictValue()
                    .Set("id", kWorkspaceExtensionWindowId)
                    .Set("focused", tabs.front()->workspace().IsExtensionWindowFocused(context))
                    .Set("incognito", context.IsOffTheRecord())
                    .Set("alwaysOnTop", false)
                    .Set("type", "normal")
                    .Set("state", "normal");
  if (populate) {
    base::ListValue entries;
    for (auto* tab : tabs)
      entries.Append(tab->MetadataFor(extension));
    result.Set("tabs", std::move(entries));
  }
  return result;
}

class LabWindowsGetCurrentFunction final : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("windows.getCurrent", WINDOWS_GETCURRENT)

 private:
  ~LabWindowsGetCurrentFunction() override = default;
  ResponseAction Run() override {
    EXTENSION_FUNCTION_VALIDATE(extension() && (args().empty() ||
        (args().size() == 1 && (args()[0].is_dict() || args()[0].is_none()))));
    const auto tabs = WorkspaceExtensionTab::InProfile(browser_context());
    if (tabs.empty())
      return RespondNow(Error("No current window."));
    const bool populate = !args().empty() && args()[0].is_dict() &&
        args()[0].GetDict().FindBool("populate").value_or(false);
    return RespondNow(WithArguments(WorkspaceExtensionWindow(*browser_context(), tabs, *extension(), populate)));
  }
};

class LabWindowsGetAllFunction final : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("windows.getAll", WINDOWS_GETALL)

 private:
  ~LabWindowsGetAllFunction() override = default;
  ResponseAction Run() override {
    EXTENSION_FUNCTION_VALIDATE(extension() && (args().empty() ||
        (args().size() == 1 && (args()[0].is_dict() || args()[0].is_none()))));
    const auto tabs = WorkspaceExtensionTab::InProfile(browser_context());
    const auto* options = args().empty() ? nullptr : args()[0].GetIfDict();
    const auto* types = options ? options->FindList("windowTypes") : nullptr;
    base::ListValue result;
    if (!tabs.empty() && (!types || types->contains("normal"))) {
      result.Append(WorkspaceExtensionWindow(
          *browser_context(), tabs, *extension(), options && options->FindBool("populate").value_or(false)));
    }
    return RespondNow(WithArguments(std::move(result)));
  }
};

base::DictValue ExtensionCookieDetails(const net::CanonicalCookie& cookie) {
  const char* same_site = "unspecified";
  switch (cookie.SameSite()) {
    case net::CookieSameSite::NO_RESTRICTION: same_site = "no_restriction"; break;
    case net::CookieSameSite::LAX_MODE: same_site = "lax"; break;
    case net::CookieSameSite::STRICT_MODE: same_site = "strict"; break;
    case net::CookieSameSite::UNSPECIFIED: break;
  }
  auto result = base::DictValue()
                    .Set("name", cookie.Name())
                    .Set("value", cookie.Value())
                    .Set("domain", cookie.Domain())
                    .Set("hostOnly", cookie.IsHostCookie())
                    .Set("path", cookie.Path())
                    .Set("secure", cookie.SecureAttribute())
                    .Set("httpOnly", cookie.IsHttpOnly())
                    .Set("sameSite", same_site)
                    .Set("session", !cookie.IsPersistent())
                    .Set("storeId", "0");
  if (cookie.IsPersistent())
    result.Set("expirationDate", cookie.ExpiryDate().InSecondsFSinceUnixEpoch());
  return result;
}

class LabCookiesGetFunction final : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("cookies.get", COOKIES_GET)

 private:
  ~LabCookiesGetFunction() override = default;

  ResponseAction Run() override {
    if (args().size() != 1 || !args()[0].is_dict())
      return RespondNow(Error("Invalid cookie details."));
    const auto& details = args()[0].GetDict();
    const auto* address = details.FindString("url");
    const auto* name = details.FindString("name");
    const auto* store = details.FindString("storeId");
    if (!address || !name || (store && *store != "0"))
      return RespondNow(Error("Invalid URL, cookie name or store."));
    const GURL url(*address);
    auto* context = browser_context();
    if (!context || !extension() || context->IsOffTheRecord() ||
        !url.SchemeIsHTTPOrHTTPS() || url.has_username() || url.has_password() ||
        extension_urls::IsWebstoreDomain(url) ||
        !extension()->permissions_data()->HasAPIPermission(
            extensions::mojom::APIPermissionID::kCookie) ||
        !extension()->permissions_data()->HasHostPermission(url)) {
      return RespondNow(Error("The extension does not have access to this cookie store or URL."));
    }
    context->GetDefaultStoragePartition()->GetCookieManagerForBrowserProcess()
        ->GetCookieList(url, net::CookieOptions::MakeAllInclusive(),
                        net::CookiePartitionKeyCollection(),
                        base::BindOnce(&LabCookiesGetFunction::OnCookies, this,
                                       url, *name));
    return RespondLater();
  }

  void OnCookies(GURL url, std::string name, const net::CookieAccessResultList& cookies,
                 const net::CookieAccessResultList&) {
    auto* context = browser_context();
    if (!context || !extension() ||
        !CanLoadExtensionProbeResources(*context, extension()->id()) ||
        !extension()->permissions_data()->HasHostPermission(url)) {
      Respond(Error("The extension no longer has access to this URL."));
      return;
    }
    for (const auto& entry : cookies) {
      const auto& cookie = entry.cookie;
      if (cookie.Name() != name || cookie.IsPartitioned())
        continue;
      Respond(WithArguments(ExtensionCookieDetails(cookie)));
      return;
    }
    Respond(WithArguments(base::Value()));
  }
};

extensions::api::management::ExtensionInfo BuildManagementInfo(
    const extensions::Extension& extension, bool enabled) {
  const auto* caller = &extension;
  namespace management = extensions::api::management;
  management::ExtensionInfo info;
  info.id = caller->id();
  info.name = caller->name();
  info.short_name = caller->short_name();
  info.description = extensions::DescriptionInfo::GetDescription(*caller);
  info.version = caller->VersionString();
  if (const auto& version_name = extensions::VersionNameInfo::GetVersionName(*caller);
      !version_name.empty())
    info.version_name = version_name;
  info.may_disable = true;
  info.enabled = enabled;
  info.disabled_reason = management::ExtensionDisabledReason::kNone;
  info.is_app = false;
  info.type = management::ExtensionType::kExtension;
  info.offline_enabled = extensions::OfflineEnabledInfo::IsOfflineEnabled(caller);
  info.options_url = extensions::OptionsPageInfo::GetOptionsPage(caller).spec();
  if (const auto homepage = extensions::ManifestURL::GetHomepageURL(caller);
      homepage.is_valid())
    info.homepage_url = homepage.spec();
  // Settings installs verified CRX files. The unpacked manifest location
  // used internally for validation is not how the user installed the CRX.
  info.install_type = management::ExtensionInstallType::kNormal;
  info.launch_type = management::LaunchType::kNone;
  const auto& permissions = caller->permissions_data()->active_permissions();
  const auto apis = permissions.GetAPIsAsStrings();
  info.permissions.assign(apis.begin(), apis.end());
  info.host_permissions = permissions.explicit_hosts().ToStringVector();
  const auto& icons = extensions::IconsInfo::GetIcons(caller).map();
  if (!icons.empty()) {
    info.icons.emplace();
    for (const auto& [size, path] : icons) {
      management::IconInfo icon;
      icon.size = size;
      icon.url = caller->GetResourceURL(path).spec();
      info.icons->push_back(std::move(icon));
    }
  }
  return info;
}

class LabManagementGetSelfFunction final : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("management.getSelf", MANAGEMENT_GETSELF)

 private:
  ~LabManagementGetSelfFunction() override = default;

  ResponseAction Run() override {
    EXTENSION_FUNCTION_VALIDATE(args().empty());
    auto* context = browser_context();
    const auto* caller = extension();
    if (!context || !caller || context->IsOffTheRecord() || context->ShutdownStarted()) {
      return RespondNow(Error("Extension metadata is unavailable in this profile."));
    }
    const auto* registry = extensions::ExtensionRegistry::Get(context);
    if (!registry || registry->enabled_extensions().GetByID(caller->id()) != caller) {
      return RespondNow(Error("Extension metadata is unavailable in this profile."));
    }

    const auto info = BuildManagementInfo(*caller, true);
    if (base::CommandLine::ForCurrentProcess()->HasSwitch(
            "zitter-content-extension-publisher-overlay-probe")) {
      std::puts("Zitter Content served publisher extension metadata to its caller");
    }
    return RespondNow(ArgumentList(extensions::api::management::GetSelf::Results::Create(info)));
  }
};

class LabRuntimeOpenOptionsPageFunction final : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("runtime.openOptionsPage", RUNTIME_OPENOPTIONSPAGE)

 private:
  ~LabRuntimeOpenOptionsPageFunction() override = default;

  ResponseAction Run() override {
    EXTENSION_FUNCTION_VALIDATE(extension() && args().empty());
    const auto tabs = WorkspaceExtensionTab::InProfile(browser_context());
    if (tabs.empty() ||
        !tabs.front()->workspace().OpenExtensionOptionsTab(
            *browser_context(), extension()->id())) {
      return RespondNow(Error("Could not open the extension's options page."));
    }
    return RespondNow(NoArguments());
  }
};

class LabGetFontListFunction final : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("fontSettings.getFontList", FONTSETTINGS_GETFONTLIST)

 private:
  ~LabGetFontListFunction() override = default;

  ResponseAction Run() override {
    EXTENSION_FUNCTION_VALIDATE(args().empty());
    if (!extension() || browser_context()->IsOffTheRecord() ||
        !extension()->permissions_data()->HasAPIPermission(
            extensions::mojom::APIPermissionID::kFontSettings)) {
      return RespondNow(Error("Font list access is unavailable in this context."));
    }
    content::GetFontListAsync(
        base::BindOnce(&LabGetFontListFunction::OnFontList, this));
    return RespondLater();
  }

  void OnFontList(base::ListValue fonts) {
    base::ListValue result;
    for (const auto& entry : fonts) {
      const auto* values = entry.GetIfList();
      if (!values || values->size() < 2 ||
          !(*values)[0].is_string() || !(*values)[1].is_string()) {
        continue;
      }
      result.Append(base::DictValue()
                        .Set("fontId", (*values)[0].GetString())
                        .Set("displayName", (*values)[1].GetString()));
    }
    Respond(WithArguments(std::move(result)));
  }
};

class LabCommandsGetAllFunction final : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("commands.getAll", COMMANDS_GETALL)

 private:
  ~LabCommandsGetAllFunction() override = default;

  ResponseAction Run() override {
    EXTENSION_FUNCTION_VALIDATE(extension() && args().empty());
    const auto tabs = WorkspaceExtensionTab::InProfile(browser_context());
    if (tabs.empty())
      return RespondNow(WithArguments(base::ListValue()));
    return RespondNow(WithArguments(tabs.front()->workspace().GetExtensionCommands(
        *browser_context(), *extension())));
  }
};

struct ExtensionMenuItem {
  std::string id;
  std::string parent;
  std::u16string title;
  bool all_contexts;
  bool enabled;
  bool separator;
  uint64_t serial;
};

class ExtensionMenus final : public base::SupportsUserData::Data {
 public:
  static ExtensionMenus& Get(BrowserContext& profile) {
    static const int kKey = 0;
    auto* menus = static_cast<ExtensionMenus*>(profile.GetUserData(&kKey));
    if (!menus) {
      auto owned = std::make_unique<ExtensionMenus>();
      menus = owned.get();
      profile.SetUserData(&kKey, std::move(owned));
    }
    return *menus;
  }

  std::map<std::string, std::vector<ExtensionMenuItem>> items;
  uint64_t next_serial = 0;
};

bool CanUseExtensionMenus(BrowserContext& profile, const extensions::Extension& extension) {
  return !profile.IsOffTheRecord() &&
         CanLoadExtensionProbeResources(profile, extension.id()) &&
         extension.permissions_data()->HasAPIPermission(
             extensions::mojom::APIPermissionID::kContextMenus);
}

class LabContextMenusCreateFunction final : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("contextMenus.create", CONTEXTMENUS_CREATE)

 private:
  ~LabContextMenusCreateFunction() override = default;

  ResponseAction Run() override {
    EXTENSION_FUNCTION_VALIDATE(extension() && args().size() == 1 && args()[0].is_dict());
    if (!CanUseExtensionMenus(*browser_context(), *extension()))
      return RespondNow(Error("Context menu permission is unavailable."));
    const auto& data = args()[0].GetDict();
    const auto* id = data.FindString("id");
    const auto* title = data.FindString("title");
    const auto* parent = data.FindString("parentId");
    const auto* type = data.FindString("type");
    const bool separator = type && *type == "separator";
    if (type && *type != "normal" && !separator)
      return RespondNow(Error("Unsupported context menu item type."));
    if (!id || id->empty() || id->size() > 128 ||
        (!separator && (!title || title->empty() || title->size() > 512))) {
      return RespondNow(Error("A context menu item needs a valid ID and title."));
    }
    bool all_contexts = false;
    if (const auto* contexts = data.FindList("contexts")) {
      if (contexts->empty() || !std::ranges::all_of(*contexts, [](const auto& value) {
            return value.is_string() &&
                   (value.GetString() == "page" || value.GetString() == "all");
          })) {
        return RespondNow(Error("Unsupported menu context."));
      }
      all_contexts = contexts->contains("all");
    }
    auto& menus = ExtensionMenus::Get(*browser_context());
    auto& items = menus.items[extension()->id()];
    if (items.size() >= 64 || std::ranges::contains(items, *id, &ExtensionMenuItem::id))
      return RespondNow(Error("Duplicate item or too many context menu items."));
    if (parent) {
      const auto item = std::ranges::find(items, *parent, &ExtensionMenuItem::id);
      if (item == items.end() || item->separator || !item->parent.empty())
        return RespondNow(Error("The submenu parent must be an existing top-level item."));
    }
    std::u16string label = separator ? std::u16string() : base::UTF8ToUTF16(*title);
    std::ranges::replace_if(label, [](char16_t c) { return c < 0x20; }, u' ');
    base::i18n::SanitizeUserSuppliedString(&label);
    items.push_back({*id, parent ? *parent : std::string(), std::move(label),
                     all_contexts, data.FindBool("enabled").value_or(true),
                     separator, ++menus.next_serial});
    return RespondNow(NoArguments());
  }
};

class LabContextMenusRemoveAllFunction final : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("contextMenus.removeAll", CONTEXTMENUS_REMOVEALL)

 private:
  ~LabContextMenusRemoveAllFunction() override = default;

  ResponseAction Run() override {
    EXTENSION_FUNCTION_VALIDATE(extension() && args().empty());
    if (!CanUseExtensionMenus(*browser_context(), *extension()))
      return RespondNow(Error("Context menu permission is unavailable."));
    ExtensionMenus::Get(*browser_context()).items.erase(extension()->id());
    return RespondNow(NoArguments());
  }
};

class LabActionFunction : public ExtensionFunction {
 protected:
  ~LabActionFunction() override = default;

  extensions::ExtensionAction* GetAction(int tab_id) {
    auto* context = browser_context();
    if (!context || !extension() ||
        !CanLoadExtensionProbeResources(*context, extension()->id()) ||
        extensions::ExtensionRegistry::Get(context)->enabled_extensions().GetByID(
            extension()->id()) != extension() ||
        (tab_id != extensions::ExtensionAction::kDefaultTabId &&
         !WorkspaceExtensionTab::FindTab(context, tab_id))) {
      return nullptr;
    }
    return extensions::ExtensionActionManager::Get(context)->GetExtensionAction(*extension());
  }

  void NotifyAppearance() {
    const auto tabs = WorkspaceExtensionTab::InProfile(browser_context());
    if (!tabs.empty())
      tabs.front()->workspace().RefreshExtensionActionAppearance();
  }
};

class ActionIconRequests final : public base::SupportsUserData::Data {
 public:
  static ActionIconRequests& Get(BrowserContext& context) {
    static const int kKey = 0;
    auto* requests = static_cast<ActionIconRequests*>(context.GetUserData(&kKey));
    if (!requests) {
      auto owned = std::make_unique<ActionIconRequests>();
      requests = owned.get();
      context.SetUserData(&kKey, std::move(owned));
    }
    return *requests;
  }

  void ClearTab(int tab_id) {
    std::erase_if(pending_, [tab_id](const auto& request) {
      return request.first.second == tab_id;
    });
  }

  uint64_t Start(const std::string& extension, int tab_id) {
    return pending_[{extension, tab_id}] = ++sequence_;
  }

  bool Complete(const std::string& extension, int tab_id, uint64_t request) {
    const auto it = pending_.find({extension, tab_id});
    if (it == pending_.end() || it->second != request)
      return false;
    pending_.erase(it);
    return true;
  }

 private:
  std::map<std::pair<std::string, int>, uint64_t> pending_;
  uint64_t sequence_ = 0;
};

void ClearTabActionState(BrowserContext& context, int tab_id) {
  ActionIconRequests::Get(context).ClearTab(tab_id);
  auto* manager = extensions::ExtensionActionManager::Get(&context);
  for (const auto& extension : GetConfiguredExtensions(context)) {
    if (CanLoadExtensionProbeResources(context, extension->id())) {
      if (auto* action = manager->GetExtensionAction(*extension))
        action->ClearAllValuesForTab(tab_id);
    }
  }
}

class LabActionSetIconFunction final : public LabActionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("action.setIcon", ACTION_SETICON)

 private:
  ~LabActionSetIconFunction() override = default;

  ResponseAction Run() override {
    EXTENSION_FUNCTION_VALIDATE(args().size() == 1 && args()[0].is_dict());
    const auto& details = args()[0].GetDict();
    const int tab_id = details.FindInt("tabId").value_or(extensions::ExtensionAction::kDefaultTabId);
    if (!GetAction(tab_id))
      return RespondNow(Error("No matching extension action or tab."));
    const std::string* path = details.FindString("path");
    if (const auto* variants = details.FindDict("path")) {
      int best = 0;
      for (const auto [size, value] : *variants) {
        int pixels = 0;
        if (value.is_string() && base::StringToInt(size, &pixels) &&
            pixels > best && pixels <= 512) {
          path = &value.GetString();
          best = pixels;
        }
      }
    }
    const GURL url = path ? extension()->ResolveExtensionURL(*path) : GURL();
    if (!url.is_valid() || !url.SchemeIs(extensions::kExtensionScheme) ||
        url::Origin::Create(url) != extension()->origin() || url.has_query() || url.has_ref() || url.has_username() || url.has_password())
      return RespondNow(Error("The icon must be an extension resource."));
    const auto relative = extensions::file_util::ExtensionURLToRelativeFilePath(url);
    if (relative.empty())
      return RespondNow(Error("The icon must be an extension resource."));
    const uint64_t request = ActionIconRequests::Get(*browser_context()).Start(
        extension()->id(), tab_id);
    extensions::ImageLoader::Get(browser_context())->LoadImageAsync(
        extension(), extension()->GetResource(relative), gfx::Size(64, 64),
        base::BindOnce(&LabActionSetIconFunction::OnImage, this, tab_id, request));
    return did_respond() ? AlreadyResponded() : RespondLater();
  }

  void OnImage(int tab_id, uint64_t request, const gfx::Image& icon) {
    if (!browser_context()) {
      Respond(Error("The extension context closed."));
      return;
    }
    if (!ActionIconRequests::Get(*browser_context()).Complete(
            extension()->id(), tab_id, request)) {
      Respond(NoArguments());
      return;
    }
    auto* action = GetAction(tab_id);
    if (!action || icon.IsEmpty()) {
      Respond(Error("The extension icon could not be loaded."));
      return;
    }
    action->SetIcon(tab_id, icon);
    NotifyAppearance();
    Respond(NoArguments());
  }
};

class LabActionSetBadgeTextFunction final : public LabActionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("action.setBadgeText", ACTION_SETBADGETEXT)

 private:
  ~LabActionSetBadgeTextFunction() override = default;
  ResponseAction Run() override {
    EXTENSION_FUNCTION_VALIDATE(args().size() == 1 && args()[0].is_dict());
    const auto& details = args()[0].GetDict();
    const int tab_id = details.FindInt("tabId").value_or(extensions::ExtensionAction::kDefaultTabId);
    const auto* text = details.FindString("text");
    auto* action = GetAction(tab_id);
    if (!action || !text || text->size() > 64)
      return RespondNow(Error("Invalid action badge text or tab."));
    action->SetBadgeText(tab_id, *text);
    NotifyAppearance();
    return RespondNow(NoArguments());
  }
};

class LabActionSetBadgeBackgroundColorFunction final : public LabActionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("action.setBadgeBackgroundColor", ACTION_SETBADGEBACKGROUNDCOLOR)

 private:
  ~LabActionSetBadgeBackgroundColorFunction() override = default;
  ResponseAction Run() override {
    EXTENSION_FUNCTION_VALIDATE(args().size() == 1 && args()[0].is_dict());
    const auto& details = args()[0].GetDict();
    const int tab_id = details.FindInt("tabId").value_or(extensions::ExtensionAction::kDefaultTabId);
    auto* action = GetAction(tab_id);
    SkColor color = SK_ColorTRANSPARENT;
    bool valid = false;
    if (const auto* css = details.FindString("color")) {
      valid = content::ParseCssColorString(*css, &color);
    } else if (const auto* rgba = details.FindList("color");
               rgba && rgba->size() == 4 &&
               std::ranges::all_of(*rgba, [](const auto& channel) {
                 return channel.is_int() && channel.GetInt() >= 0 && channel.GetInt() <= 255;
               })) {
      color = SkColorSetARGB((*rgba)[3].GetInt(), (*rgba)[0].GetInt(),
                             (*rgba)[1].GetInt(), (*rgba)[2].GetInt());
      valid = true;
    }
    if (!action || !valid)
      return RespondNow(Error("Invalid action badge color or tab."));
    action->SetBadgeBackgroundColor(tab_id, color);
    NotifyAppearance();
    return RespondNow(NoArguments());
  }
};

constexpr char kWebStoreInstallKey = 0;

struct WebStoreInstallHandler : base::SupportsUserData::Data {
  explicit WebStoreInstallHandler(WebStoreInstall callback)
      : callback(std::move(callback)) {}
  WebStoreInstall callback;
};

class LabWebStoreFunction : public ExtensionFunction {
 protected:
  ~LabWebStoreFunction() override = default;

  WebStoreInstallHandler* GetHandler() {
    auto* frame = render_frame_host();
    auto* page = GetSenderWebContents();
    if (!frame || !page || page->GetPrimaryMainFrame() != frame ||
        !frame->IsActive() || frame->IsErrorDocument() ||
        frame->GetLastCommittedOrigin() !=
            url::Origin::Create(GURL("https://chromewebstore.google.com"))) {
      return nullptr;
    }
    return static_cast<WebStoreInstallHandler*>(
        page->GetUserData(&kWebStoreInstallKey));
  }

  bool IsInstalled(const std::string& id) {
    return std::ranges::contains(GetConfiguredExtensions(*browser_context()),
                                 id, &extensions::Extension::id);
  }
};

class LabWebStoreGetStatus final : public LabWebStoreFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("webstorePrivate.getExtensionStatus",
                             WEBSTOREPRIVATE_GETEXTENSIONSTATUS)
 private:
  ~LabWebStoreGetStatus() override = default;

  ResponseAction Run() override {
    if (args().empty() || !args()[0].is_string() ||
        !crx_file::id_util::IdIsValid(args()[0].GetString())) {
      return RespondNow(Error("Invalid extension ID."));
    }
    const auto& id = args()[0].GetString();
    if (!GetHandler())
      return RespondNow(WithArguments("blocked_by_policy"));
    if (!IsInstalled(id))
      return RespondNow(WithArguments("installable"));
    const auto* registry = extensions::ExtensionRegistry::Get(browser_context());
    return RespondNow(WithArguments(
        registry->enabled_extensions().Contains(id) ? "enabled" : "disabled"));
  }
};

class LabWebStoreBeginInstall final : public LabWebStoreFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("webstorePrivate.beginInstallWithManifest3",
                             WEBSTOREPRIVATE_BEGININSTALLWITHMANIFEST3)
 private:
  ~LabWebStoreBeginInstall() override = default;

  ResponseAction Run() override {
    const auto* id = args().size() == 1 && args()[0].is_dict()
                         ? args()[0].GetDict().FindString("id") : nullptr;
    auto* handler = GetHandler();
    if (!id || !crx_file::id_util::IdIsValid(*id) || !handler)
      return RespondNow(Error("Invalid Web Store install request."));
    if (IsInstalled(*id))
      return RespondNow(Error("This extension is already installed."));
    if (!render_frame_host()->ConsumeTransientUserActivation())
      return RespondNow(Error("A user gesture is required."));
    if (!handler->callback.Run(*render_frame_host(), *id,
            base::BindOnce(&LabWebStoreBeginInstall::OnFinished,
                           base::RetainedRef(this)))) {
      return RespondNow(Error("Could not start the extension installation."));
    }
    return did_respond() ? AlreadyResponded() : RespondLater();
  }

  void OnFinished(bool installed) {
    if (installed) {
      Respond(WithArguments(""));
    } else {
      Respond(ErrorWithArgumentsDoNotUse(base::ListValue().Append("user_cancelled"),
                                        "The extension was not installed."));
    }
  }
};

class LabWebStoreCompleteInstall final : public LabWebStoreFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("webstorePrivate.completeInstall",
                             WEBSTOREPRIVATE_COMPLETEINSTALL)
 private:
  ~LabWebStoreCompleteInstall() override = default;

  ResponseAction Run() override {
    // Zitter installs the verified package as part of the native review.
    if (!GetHandler() || args().size() != 1 || !args()[0].is_string() ||
        !IsInstalled(args()[0].GetString())) {
      return RespondNow(Error("No completed extension installation."));
    }
    return RespondNow(NoArguments());
  }
};

class LabWebStoreGetVersion final : public LabWebStoreFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("webstorePrivate.getFullChromeVersion",
                             WEBSTOREPRIVATE_GETFULLCHROMEVERSION)
 private:
  ~LabWebStoreGetVersion() override = default;

  ResponseAction Run() override {
    return RespondNow(WithArguments(base::DictValue().Set(
        "version_number", std::string(version_info::GetVersionNumber()))));
  }
};

class LabWebStoreIsIncognito final : public LabWebStoreFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("webstorePrivate.isInIncognitoMode",
                             WEBSTOREPRIVATE_ISININCOGNITOMODEFUNCTION)
 private:
  ~LabWebStoreIsIncognito() override = default;

  ResponseAction Run() override {
    return RespondNow(WithArguments(browser_context()->IsOffTheRecord()));
  }
};

class LabWebStoreGetMV2Status final : public LabWebStoreFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("webstorePrivate.getMV2DeprecationStatus",
                             WEBSTOREPRIVATE_GETMV2DEPRECATIONSTATUS)
 private:
  ~LabWebStoreGetMV2Status() override = default;

  ResponseAction Run() override {
    return RespondNow(WithArguments("hard_disable"));
  }
};

class LabWebStoreGetInstalled final : public LabWebStoreFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("management.getAll", MANAGEMENT_GETALL)

 private:
  ~LabWebStoreGetInstalled() override = default;

  ResponseAction Run() override {
    std::vector<extensions::api::management::ExtensionInfo> installed;
    if (GetHandler()) {
      const auto* registry = extensions::ExtensionRegistry::Get(browser_context());
      for (const auto& item : GetConfiguredExtensions(*browser_context())) {
        auto info = BuildManagementInfo(*item,
                                        registry->enabled_extensions().Contains(item->id()));
        info.permissions.clear();
        info.host_permissions.clear();
        installed.push_back(std::move(info));
      }
    }
    return RespondNow(ArgumentList(
        extensions::api::management::GetAll::Results::Create(installed)));
  }
};

class LabWebStoreGetReferrerChain final : public LabWebStoreFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("webstorePrivate.getReferrerChain",
                             WEBSTOREPRIVATE_GETREFERRERCHAIN)

 private:
  ~LabWebStoreGetReferrerChain() override = default;

  ResponseAction Run() override { return RespondNow(WithArguments("")); }
};

class LabWebStoreGetPromotion final : public LabWebStoreFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("webstorePrivate.shouldShowEnterprisePromotionBanner",
                             WEBSTOREPRIVATE_SHOULDSHOWENTERPRISEPROMOTIONBANNER)

 private:
  ~LabWebStoreGetPromotion() override = default;

  ResponseAction Run() override {
    return RespondNow(WithArguments("PROMOTION_TYPE_UNSPECIFIED"));
  }
};

class LabActionSetPopupFunction final : public LabActionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("action.setPopup", ACTION_SETPOPUP)

 private:
  ~LabActionSetPopupFunction() override = default;
  ResponseAction Run() override {
    EXTENSION_FUNCTION_VALIDATE(args().size() == 1 && args()[0].is_dict());
    const auto& details = args()[0].GetDict();
    const int tab_id = details.FindInt("tabId").value_or(extensions::ExtensionAction::kDefaultTabId);
    const auto* popup = details.FindString("popup");
    auto* action = GetAction(tab_id);
    if (!action || !popup)
      return RespondNow(Error("Invalid action popup or tab."));
    GURL url;
    if (!popup->empty()) {
      url = extension()->ResolveExtensionURL(*popup);
      if (!url.is_valid() || url::Origin::Create(url) != extension()->origin())
        return RespondNow(Error("The popup must be a page of the extension."));
    }
    action->SetPopupUrl(tab_id, url);
    return RespondNow(NoArguments());
  }
};

class LabActionGetPopupFunction final : public LabActionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("action.getPopup", ACTION_GETPOPUP)

 private:
  ~LabActionGetPopupFunction() override = default;
  ResponseAction Run() override {
    const auto* details = args().empty() ? nullptr : args()[0].GetIfDict();
    const int tab_id = details ? details->FindInt("tabId").value_or(
        extensions::ExtensionAction::kDefaultTabId) : extensions::ExtensionAction::kDefaultTabId;
    auto* action = GetAction(tab_id);
    if (!action)
      return RespondNow(Error("Invalid tab."));
    const GURL url = action->GetPopupUrl(tab_id);
    return RespondNow(WithArguments(url.is_valid() ? url.spec() : std::string()));
  }
};

class LabActionSetTitleFunction final : public LabActionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("action.setTitle", ACTION_SETTITLE)

 private:
  ~LabActionSetTitleFunction() override = default;
  ResponseAction Run() override {
    EXTENSION_FUNCTION_VALIDATE(args().size() == 1 && args()[0].is_dict());
    const auto& details = args()[0].GetDict();
    const int tab_id = details.FindInt("tabId").value_or(extensions::ExtensionAction::kDefaultTabId);
    const auto* title = details.FindString("title");
    auto* action = GetAction(tab_id);
    if (!action || !title || title->size() > 1024)
      return RespondNow(Error("Invalid action title or tab."));
    action->SetTitle(tab_id, *title);
    NotifyAppearance();
    return RespondNow(NoArguments());
  }
};

class LabActionGetTitleFunction final : public LabActionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("action.getTitle", ACTION_GETTITLE)

 private:
  ~LabActionGetTitleFunction() override = default;
  ResponseAction Run() override {
    const auto* details = args().empty() ? nullptr : args()[0].GetIfDict();
    const int tab_id = details ? details->FindInt("tabId").value_or(
        extensions::ExtensionAction::kDefaultTabId) : extensions::ExtensionAction::kDefaultTabId;
    auto* action = GetAction(tab_id);
    return action ? RespondNow(WithArguments(action->GetTitle(tab_id)))
                  : RespondNow(Error("Invalid tab."));
  }
};

class LabActionGetBadgeTextFunction final : public LabActionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("action.getBadgeText", ACTION_GETBADGETEXT)

 private:
  ~LabActionGetBadgeTextFunction() override = default;
  ResponseAction Run() override {
    const auto* details = args().empty() ? nullptr : args()[0].GetIfDict();
    const int tab_id = details ? details->FindInt("tabId").value_or(
        extensions::ExtensionAction::kDefaultTabId) : extensions::ExtensionAction::kDefaultTabId;
    auto* action = GetAction(tab_id);
    return action ? RespondNow(WithArguments(action->GetExplicitlySetBadgeText(tab_id)))
                  : RespondNow(Error("Invalid tab."));
  }
};

template <bool kEnable, extensions::functions::HistogramValue kHistogram>
class LabActionSetEnabledFunction : public LabActionFunction {
 protected:
  ~LabActionSetEnabledFunction() override = default;
  ResponseAction Run() override {
    const int tab_id = !args().empty() && args()[0].is_int()
        ? args()[0].GetInt() : extensions::ExtensionAction::kDefaultTabId;
    auto* action = GetAction(tab_id);
    if (!action)
      return RespondNow(Error("Invalid tab."));
    action->SetIsVisible(tab_id, kEnable);
    NotifyAppearance();
    return RespondNow(NoArguments());
  }
};

class LabActionEnableFunction final
    : public LabActionSetEnabledFunction<true, extensions::functions::ACTION_ENABLE> {
 public:
  DECLARE_EXTENSION_FUNCTION("action.enable", ACTION_ENABLE)
 private:
  ~LabActionEnableFunction() override = default;
};

class LabActionDisableFunction final
    : public LabActionSetEnabledFunction<false, extensions::functions::ACTION_DISABLE> {
 public:
  DECLARE_EXTENSION_FUNCTION("action.disable", ACTION_DISABLE)
 private:
  ~LabActionDisableFunction() override = default;
};

class LabActionIsEnabledFunction final : public LabActionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("action.isEnabled", ACTION_ISENABLED)

 private:
  ~LabActionIsEnabledFunction() override = default;
  ResponseAction Run() override {
    const int tab_id = !args().empty() && args()[0].is_int()
        ? args()[0].GetInt() : extensions::ExtensionAction::kDefaultTabId;
    auto* action = GetAction(tab_id);
    return action ? RespondNow(WithArguments(action->GetIsVisibleIgnoringDeclarative(tab_id)))
                  : RespondNow(Error("Invalid tab."));
  }
};

class LabActionOpenPopupFunction final : public LabActionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("action.openPopup", ACTION_OPENPOPUP)

 private:
  ~LabActionOpenPopupFunction() override = default;
  ResponseAction Run() override {
    const auto tabs = WorkspaceExtensionTab::InProfile(browser_context());
    if (!GetAction(extensions::ExtensionAction::kDefaultTabId) || tabs.empty() ||
        !tabs.front()->workspace().OpenExtensionActionPopup(*browser_context(), extension()->id())) {
      return RespondNow(Error("Failed to open the popup."));
    }
    return RespondNow(NoArguments());
  }
};

// Zitter shows every extension's action in the toolbar.
class LabActionGetUserSettingsFunction final : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("action.getUserSettings", ACTION_GETUSERSETTINGS)

 private:
  ~LabActionGetUserSettingsFunction() override = default;
  ResponseAction Run() override {
    return RespondNow(WithArguments(base::DictValue().Set("isOnToolbar", true)));
  }
};

// identity: Zitter has no browser-wide Google account, so it answers as
// Chrome does when nobody is signed in. Extensions then use their own sign-in.
class LabIdentityGetAuthTokenFunction final : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("identity.getAuthToken", EXPERIMENTAL_IDENTITY_GETAUTHTOKEN)

 private:
  ~LabIdentityGetAuthTokenFunction() override = default;
  ResponseAction Run() override { return RespondNow(Error("The user is not signed in.")); }
};

class LabIdentityRemoveCachedAuthTokenFunction final : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("identity.removeCachedAuthToken",
                             EXPERIMENTAL_IDENTITY_REMOVECACHEDAUTHTOKEN)

 private:
  ~LabIdentityRemoveCachedAuthTokenFunction() override = default;
  ResponseAction Run() override { return RespondNow(NoArguments()); }
};

class LabIdentityClearAllCachedAuthTokensFunction final : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("identity.clearAllCachedAuthTokens",
                             IDENTITY_CLEARALLCACHEDAUTHTOKENS)

 private:
  ~LabIdentityClearAllCachedAuthTokensFunction() override = default;
  ResponseAction Run() override { return RespondNow(NoArguments()); }
};

class LabIdentityGetProfileUserInfoFunction final : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("identity.getProfileUserInfo", IDENTITY_GETPROFILEUSERINFO)

 private:
  ~LabIdentityGetProfileUserInfoFunction() override = default;
  ResponseAction Run() override {
    return RespondNow(WithArguments(base::DictValue().Set("email", "").Set("id", "")));
  }
};

// browsingData.remove, limited to listed origins or hostnames and the site
// storage, cookies and caches that Content can filter by origin.
class LabBrowsingDataRemoveFunction final
    : public ExtensionFunction, public content::BrowsingDataRemover::Observer {
 public:
  DECLARE_EXTENSION_FUNCTION("browsingData.remove", BROWSINGDATA_REMOVE)

 private:
  ~LabBrowsingDataRemoveFunction() override = default;

  ResponseAction Run() override {
    using Remover = content::BrowsingDataRemover;
    auto* context = browser_context();
    if (args().size() != 2 || !args()[0].is_dict() || !args()[1].is_dict())
      return RespondNow(Error("Invalid removal options."));
    if (!context || !extension() || context->IsOffTheRecord() ||
        !extension()->permissions_data()->HasAPIPermission(
            extensions::mojom::APIPermissionID::kBrowsingData)) {
      return RespondNow(Error("The extension may not remove browsing data here."));
    }
    const auto& options = args()[0].GetDict();
    if (options.contains("excludeOrigins"))
      return RespondNow(Error("Zitter removes browsing data only for listed sites."));
    std::set<url::Origin> origins;
    if (const auto* list = options.FindList("origins")) {
      for (const auto& value : *list) {
        const GURL url(value.is_string() ? value.GetString() : std::string());
        if (!url.SchemeIsHTTPOrHTTPS() || url.has_username() || url.has_password())
          return RespondNow(Error("Invalid origin."));
        origins.insert(url::Origin::Create(url));
      }
    }
    if (const auto* list = options.FindList("hostnames")) {
      for (const auto& value : *list) {
        const auto* host = value.GetIfString();
        const GURL url(host ? "https://" + *host + "/" : std::string());
        if (!url.is_valid() || url.host() != *host || url.has_port())
          return RespondNow(Error("Invalid hostname."));
        origins.insert(url::Origin::Create(url));
        origins.insert(url::Origin::Create(GURL("http://" + *host + "/")));
      }
    }
    if (origins.empty() || origins.size() > 1000)
      return RespondNow(Error("Zitter removes browsing data only for listed sites."));
    static constexpr std::pair<std::string_view, uint64_t> kTypes[] = {
        {"cache", Remover::DATA_TYPE_CACHE},
        {"cacheStorage", Remover::DATA_TYPE_CACHE_STORAGE},
        {"cookies", Remover::DATA_TYPE_COOKIES},
        {"fileSystems", Remover::DATA_TYPE_FILE_SYSTEMS},
        {"indexedDB", Remover::DATA_TYPE_INDEXED_DB},
        {"localStorage", Remover::DATA_TYPE_LOCAL_STORAGE},
        {"serviceWorkers", Remover::DATA_TYPE_SERVICE_WORKERS},
        {"webSQL", 0},
    };
    uint64_t mask = 0;
    for (const auto [name, value] : args()[1].GetDict()) {
      if (!value.is_bool())
        return RespondNow(Error("Invalid data type."));
      if (!value.GetBool())
        continue;
      const auto type = std::ranges::find(kTypes, name, &std::pair<std::string_view, uint64_t>::first);
      if (type == std::end(kTypes))
        return RespondNow(Error("This data type can't be removed for listed sites."));
      mask |= type->second;
    }
    if (!mask)
      return RespondNow(NoArguments());
    auto filter = content::BrowsingDataFilterBuilder::Create(
        content::BrowsingDataFilterBuilder::Mode::kDelete);
    for (const auto& origin : origins)
      filter->AddOrigin(origin);
    const auto since = options.FindDouble("since").value_or(0);
    // The remover calls back exactly once; keep this function until then.
    AddRef();
    context->GetBrowsingDataRemover()->RemoveWithFilterAndReply(
        since > 0 ? base::Time::FromMillisecondsSinceUnixEpoch(since) : base::Time(),
        base::Time::Max(), mask,
        Remover::ORIGIN_TYPE_UNPROTECTED_WEB | Remover::ORIGIN_TYPE_PROTECTED_WEB,
        std::move(filter), this);
    return RespondLater();
  }

  void OnBrowsingDataRemoverDone(uint64_t failed_data_types) override {
    Respond(failed_data_types ? Error("Some data could not be removed.") : NoArguments());
    Release();
  }
};

// runtime functions that need no RuntimeAPIDelegate.
class LabRuntimeGetPlatformInfoFunction final : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("runtime.getPlatformInfo", RUNTIME_GETPLATFORMINFO)

 private:
  ~LabRuntimeGetPlatformInfoFunction() override = default;
  ResponseAction Run() override {
#if BUILDFLAG(IS_MAC)
    constexpr char kOs[] = "mac";
#elif BUILDFLAG(IS_WIN)
    constexpr char kOs[] = "win";
#else
    constexpr char kOs[] = "linux";
#endif
#if defined(ARCH_CPU_ARM64)
    constexpr char kArch[] = "arm64";
    constexpr char kNaclArch[] = "arm";
#else
    constexpr char kArch[] = "x86-64";
    constexpr char kNaclArch[] = "x86-64";
#endif
    return RespondNow(WithArguments(
        base::DictValue().Set("os", kOs).Set("arch", kArch).Set("nacl_arch", kNaclArch)));
  }
};

// Zitter checks the store for updates itself every few hours.
class LabRuntimeRequestUpdateCheckFunction final : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("runtime.requestUpdateCheck", RUNTIME_REQUESTUPDATECHECK)

 private:
  ~LabRuntimeRequestUpdateCheckFunction() override = default;
  ResponseAction Run() override {
    return RespondNow(WithArguments(base::DictValue().Set("status", "no_update")));
  }
};

// cookies: the profile's single store ("0"), limited to granted hosts.
bool ExtensionMayUseCookieUrl(const extensions::Extension& extension, const GURL& url) {
  return url.SchemeIsHTTPOrHTTPS() && !url.has_username() && !url.has_password() &&
         !extension_urls::IsWebstoreDomain(url) &&
         extension.permissions_data()->HasHostPermission(url);
}

bool ExtensionMayUseCookie(const extensions::Extension& extension,
                           const net::CanonicalCookie& cookie) {
  const auto host = std::string(base::TrimString(cookie.Domain(), ".", base::TRIM_LEADING));
  return ExtensionMayUseCookieUrl(
      extension, GURL((cookie.SecureAttribute() ? "https://" : "http://") + host + "/"));
}

class LabCookiesGetAllFunction final : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("cookies.getAll", COOKIES_GETALL)

 private:
  ~LabCookiesGetAllFunction() override = default;

  ResponseAction Run() override {
    if (args().size() != 1 || !args()[0].is_dict())
      return RespondNow(Error("Invalid cookie filter."));
    filter_ = args()[0].GetDict().Clone();
    auto* context = browser_context();
    const auto* store = filter_.FindString("storeId");
    if (!context || !extension() || context->IsOffTheRecord() || (store && *store != "0") ||
        !extension()->permissions_data()->HasAPIPermission(
            extensions::mojom::APIPermissionID::kCookie)) {
      return RespondNow(Error("The extension does not have access to this cookie store."));
    }
    auto* manager = context->GetDefaultStoragePartition()->GetCookieManagerForBrowserProcess();
    if (const auto* address = filter_.FindString("url")) {
      const GURL url(*address);
      if (!ExtensionMayUseCookieUrl(*extension(), url))
        return RespondNow(Error("The extension does not have access to this URL."));
      manager->GetCookieList(url, net::CookieOptions::MakeAllInclusive(),
                             net::CookiePartitionKeyCollection(),
                             base::BindOnce(&LabCookiesGetAllFunction::OnList, this));
    } else {
      manager->GetAllCookies(base::BindOnce(&LabCookiesGetAllFunction::OnAll, this));
    }
    return RespondLater();
  }

  void OnList(const net::CookieAccessResultList& cookies, const net::CookieAccessResultList&) {
    std::vector<net::CanonicalCookie> list;
    for (const auto& entry : cookies)
      list.push_back(entry.cookie);
    OnAll(list);
  }

  void OnAll(const std::vector<net::CanonicalCookie>& cookies) {
    if (!extension()) {
      Respond(Error("The extension is gone."));
      return;
    }
    const auto* name = filter_.FindString("name");
    const auto* domain = filter_.FindString("domain");
    const auto* path = filter_.FindString("path");
    const auto secure = filter_.FindBool("secure");
    const auto session = filter_.FindBool("session");
    base::ListValue result;
    for (const auto& cookie : cookies) {
      if (cookie.IsPartitioned() || !ExtensionMayUseCookie(*extension(), cookie) ||
          (name && cookie.Name() != *name) || (path && cookie.Path() != *path) ||
          (secure && cookie.SecureAttribute() != *secure) ||
          (session && cookie.IsPersistent() == *session)) {
        continue;
      }
      if (domain) {
        const auto wanted = std::string(base::TrimString(*domain, ".", base::TRIM_LEADING));
        const auto own = std::string(base::TrimString(cookie.Domain(), ".", base::TRIM_LEADING));
        if (own != wanted && !own.ends_with("." + wanted))
          continue;
      }
      result.Append(ExtensionCookieDetails(cookie));
    }
    Respond(WithArguments(std::move(result)));
  }

  base::DictValue filter_;
};

class LabCookiesSetFunction final : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("cookies.set", COOKIES_SET)

 private:
  ~LabCookiesSetFunction() override = default;

  ResponseAction Run() override {
    if (args().size() != 1 || !args()[0].is_dict())
      return RespondNow(Error("Invalid cookie details."));
    const auto& details = args()[0].GetDict();
    const auto* address = details.FindString("url");
    const auto* store = details.FindString("storeId");
    auto* context = browser_context();
    const GURL url(address ? *address : std::string());
    if (!context || !extension() || context->IsOffTheRecord() || (store && *store != "0") ||
        !extension()->permissions_data()->HasAPIPermission(
            extensions::mojom::APIPermissionID::kCookie) ||
        !ExtensionMayUseCookieUrl(*extension(), url)) {
      return RespondNow(Error("The extension does not have access to this cookie store or URL."));
    }
    auto text = [&](const char* key) {
      const auto* value = details.FindString(key);
      return value ? *value : std::string();
    };
    net::CookieSameSite same_site = net::CookieSameSite::UNSPECIFIED;
    const auto mode = text("sameSite");
    if (mode == "no_restriction") same_site = net::CookieSameSite::NO_RESTRICTION;
    else if (mode == "lax") same_site = net::CookieSameSite::LAX_MODE;
    else if (mode == "strict") same_site = net::CookieSameSite::STRICT_MODE;
    const auto expiration = details.FindDouble("expirationDate");
    const auto now = base::Time::Now();
    net::CookieInclusionStatus status;
    auto cookie = net::CanonicalCookie::CreateSanitizedCookie(
        url, text("name"), text("value"), text("domain"),
        details.FindString("path") ? text("path") : std::string(), now,
        expiration ? base::Time::FromSecondsSinceUnixEpoch(*expiration) : base::Time(), now,
        details.FindBool("secure").value_or(false), details.FindBool("httpOnly").value_or(false),
        same_site, net::COOKIE_PRIORITY_DEFAULT, std::nullopt, &status);
    if (!cookie || !ExtensionMayUseCookie(*extension(), *cookie))
      return RespondNow(Error("Failed to parse or set cookie named \"" + text("name") + "\"."));
    auto copy = *cookie;
    context->GetDefaultStoragePartition()->GetCookieManagerForBrowserProcess()
        ->SetCanonicalCookie(*cookie, url, net::CookieOptions::MakeAllInclusive(),
                             base::BindOnce(&LabCookiesSetFunction::OnSet, this, std::move(copy)));
    return RespondLater();
  }

  void OnSet(net::CanonicalCookie cookie, net::CookieAccessResult result) {
    if (!result.status.IsInclude()) {
      Respond(Error("Failed to set cookie named \"" + cookie.Name() + "\"."));
      return;
    }
    Respond(WithArguments(ExtensionCookieDetails(cookie)));
  }
};

class LabCookiesRemoveFunction final : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("cookies.remove", COOKIES_REMOVE)

 private:
  ~LabCookiesRemoveFunction() override = default;

  ResponseAction Run() override {
    if (args().size() != 1 || !args()[0].is_dict())
      return RespondNow(Error("Invalid cookie details."));
    const auto& details = args()[0].GetDict();
    const auto* address = details.FindString("url");
    const auto* name = details.FindString("name");
    const auto* store = details.FindString("storeId");
    auto* context = browser_context();
    url_ = GURL(address ? *address : std::string());
    if (!name || !context || !extension() || context->IsOffTheRecord() ||
        (store && *store != "0") ||
        !extension()->permissions_data()->HasAPIPermission(
            extensions::mojom::APIPermissionID::kCookie) ||
        !ExtensionMayUseCookieUrl(*extension(), url_)) {
      return RespondNow(Error("The extension does not have access to this cookie store or URL."));
    }
    name_ = *name;
    context->GetDefaultStoragePartition()->GetCookieManagerForBrowserProcess()->GetCookieList(
        url_, net::CookieOptions::MakeAllInclusive(), net::CookiePartitionKeyCollection(),
        base::BindOnce(&LabCookiesRemoveFunction::OnList, this));
    return RespondLater();
  }

  void OnList(const net::CookieAccessResultList& cookies, const net::CookieAccessResultList&) {
    auto* context = browser_context();
    const auto match = std::ranges::find_if(cookies, [&](const auto& entry) {
      return entry.cookie.Name() == name_ && !entry.cookie.IsPartitioned();
    });
    if (!context || match == cookies.end()) {
      Respond(WithArguments(base::Value()));
      return;
    }
    context->GetDefaultStoragePartition()->GetCookieManagerForBrowserProcess()
        ->DeleteCanonicalCookie(match->cookie,
                                base::BindOnce(&LabCookiesRemoveFunction::OnDeleted, this));
  }

  void OnDeleted(bool) {
    Respond(WithArguments(
        base::DictValue().Set("url", url_.spec()).Set("name", name_).Set("storeId", "0")));
  }

  GURL url_;
  std::string name_;
};

class LabCookiesGetAllCookieStoresFunction final : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("cookies.getAllCookieStores", COOKIES_GETALLCOOKIESTORES)

 private:
  ~LabCookiesGetAllCookieStoresFunction() override = default;
  ResponseAction Run() override {
    base::ListValue tabs;
    for (auto* tab : WorkspaceExtensionTab::InProfile(browser_context()))
      tabs.Append(tab->id());
    return RespondNow(WithArguments(base::ListValue().Append(
        base::DictValue().Set("id", "0").Set("tabIds", std::move(tabs)))));
  }
};

// windows: Zitter shows each profile's tabs in one Workspace window.
bool IsWorkspaceWindowId(int id) {
  return id == kWorkspaceExtensionWindowId || id == extension_misc::kCurrentWindowId;
}

class LabWindowsGetFunction final : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("windows.get", WINDOWS_GET)

 private:
  ~LabWindowsGetFunction() override = default;
  ResponseAction Run() override {
    EXTENSION_FUNCTION_VALIDATE(extension() && !args().empty() && args()[0].is_int());
    const auto tabs = WorkspaceExtensionTab::InProfile(browser_context());
    if (tabs.empty() || !IsWorkspaceWindowId(args()[0].GetInt()))
      return RespondNow(Error("No window with id: " + base::NumberToString(args()[0].GetInt()) + "."));
    const auto* options = args().size() > 1 ? args()[1].GetIfDict() : nullptr;
    return RespondNow(WithArguments(WorkspaceExtensionWindow(
        *browser_context(), tabs, *extension(),
        options && options->FindBool("populate").value_or(false))));
  }
};

class LabWindowsCreateFunction final : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("windows.create", WINDOWS_CREATE)

 private:
  ~LabWindowsCreateFunction() override = default;
  ResponseAction Run() override {
    EXTENSION_FUNCTION_VALIDATE(extension());
    const auto* options = args().empty() ? nullptr : args()[0].GetIfDict();
    if (options && (options->FindBool("incognito").value_or(false) || options->contains("tabId")))
      return RespondNow(Error("Zitter can't open private windows or move tabs into new windows."));
    std::vector<std::string> inputs;
    if (const auto* url = options ? options->Find("url") : nullptr) {
      if (url->is_string())
        inputs.push_back(url->GetString());
      if (url->is_list()) {
        for (const auto& item : url->GetList())
          if (item.is_string())
            inputs.push_back(item.GetString());
      }
    }
    if (inputs.empty())
      inputs.emplace_back();
    std::vector<GURL> urls;
    for (const auto& input : inputs) {
      GURL url;
      if (!input.empty() && !ResolveExtensionTabUrl(*extension(), input, url)) {
        return RespondNow(Error(
            "Only HTTP and HTTPS URLs without credentials and the calling "
            "extension's own resource URLs are supported."));
      }
      urls.push_back(std::move(url));
    }
    const auto tabs = WorkspaceExtensionTab::InProfile(browser_context());
    if (tabs.empty())
      return RespondNow(Error("No Workspace window."));
    // Popup and normal windows open as tabs in the profile's window.
    const bool focused = !options || options->FindBool("focused").value_or(true);
    for (size_t i = 0; i < urls.size(); ++i) {
      if (!tabs.front()->workspace().CreateExtensionTab(*browser_context(), urls[i],
                                                         focused && i == 0)) {
        return RespondNow(Error("Could not create a Workspace tab."));
      }
    }
    if (focused)
      tabs.front()->workspace().FocusExtensionWindow(*browser_context());
    return RespondNow(WithArguments(WorkspaceExtensionWindow(
        *browser_context(), WorkspaceExtensionTab::InProfile(browser_context()), *extension(),
        true)));
  }
};

class LabWindowsUpdateFunction final : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("windows.update", WINDOWS_UPDATE)

 private:
  ~LabWindowsUpdateFunction() override = default;
  ResponseAction Run() override {
    EXTENSION_FUNCTION_VALIDATE(extension() && args().size() == 2 && args()[0].is_int() &&
                                args()[1].is_dict());
    const auto tabs = WorkspaceExtensionTab::InProfile(browser_context());
    if (tabs.empty() || !IsWorkspaceWindowId(args()[0].GetInt()))
      return RespondNow(Error("No window with id: " + base::NumberToString(args()[0].GetInt()) + "."));
    // Size, position and state belong to the user; focus is honored.
    if (args()[1].GetDict().FindBool("focused").value_or(false) ||
        args()[1].GetDict().FindBool("drawAttention").value_or(false)) {
      tabs.front()->workspace().FocusExtensionWindow(*browser_context());
    }
    return RespondNow(WithArguments(
        WorkspaceExtensionWindow(*browser_context(), tabs, *extension(), false)));
  }
};

class LabWindowsRemoveFunction final : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("windows.remove", WINDOWS_REMOVE)

 private:
  ~LabWindowsRemoveFunction() override = default;
  ResponseAction Run() override {
    return RespondNow(Error(
        "Zitter keeps one window per space. Close its tabs with tabs.remove instead."));
  }
};

// tabs.captureVisibleTab of the profile's active tab.
class LabTabsCaptureVisibleTabFunction final : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("tabs.captureVisibleTab", TABS_CAPTUREVISIBLETAB)

 private:
  ~LabTabsCaptureVisibleTabFunction() override = default;

  ResponseAction Run() override {
    EXTENSION_FUNCTION_VALIDATE(extension());
    if (!args().empty() && args()[0].is_int() && !IsWorkspaceWindowId(args()[0].GetInt()))
      return RespondNow(Error("No window with id: " + base::NumberToString(args()[0].GetInt()) + "."));
    const base::DictValue* options = nullptr;
    for (const auto& arg : args())
      if (arg.is_dict())
        options = &arg.GetDict();
    if (options) {
      const auto* format = options->FindString("format");
      png_ = format && *format == "png";
      quality_ = std::clamp(options->FindInt("quality").value_or(92), 0, 100);
    }
    WorkspaceExtensionTab* active = nullptr;
    for (auto* tab : WorkspaceExtensionTab::InProfile(browser_context()))
      if (tab->active())
        active = tab;
    auto* page = active ? active->web_contents() : nullptr;
    auto* view = page ? page->GetRenderWidgetHostView() : nullptr;
    std::string error;
    if (!view || !extension()->permissions_data()->CanCaptureVisiblePage(
                     page->GetLastCommittedURL(), active->id(), &error,
                     extensions::CaptureRequirement::kActiveTabOrAllUrls)) {
      return RespondNow(Error(error.empty() ? "Cannot access the visible tab." : error));
    }
    view->CopyFromSurface(gfx::Rect(), gfx::Size(), base::Seconds(5),
                          base::BindOnce(&LabTabsCaptureVisibleTabFunction::OnCopied, this));
    return RespondLater();
  }

  void OnCopied(const content::CopyFromSurfaceResult& result) {
    if (!result.has_value() || result->bitmap.drawsNothing()) {
      Respond(Error("Failed to capture tab."));
      return;
    }
    const auto bytes = png_ ? gfx::PNGCodec::EncodeBGRASkBitmap(result->bitmap, true)
                            : gfx::JPEGCodec::Encode(result->bitmap, quality_);
    if (!bytes) {
      Respond(Error("Failed to encode the capture."));
      return;
    }
    Respond(WithArguments(std::string(png_ ? "data:image/png;base64," : "data:image/jpeg;base64,") +
                          base::Base64Encode(*bytes)));
  }

  bool png_ = false;
  int quality_ = 92;
};

class LabBrowserExtensionAPIProvider final
    : public extensions::ExtensionsBrowserAPIProvider {
 public:
  void RegisterExtensionFunctions(ExtensionFunctionRegistry* registry) override {
    registry->RegisterFunction<LabWebStoreGetStatus>();
    registry->RegisterFunction<LabWebStoreGetInstalled>();
    registry->RegisterFunction<LabWebStoreGetReferrerChain>();
    registry->RegisterFunction<LabWebStoreGetPromotion>();
    registry->RegisterFunction<LabWebStoreBeginInstall>();
    registry->RegisterFunction<LabWebStoreCompleteInstall>();
    registry->RegisterFunction<LabWebStoreGetVersion>();
    registry->RegisterFunction<LabWebStoreIsIncognito>();
    registry->RegisterFunction<LabWebStoreGetMV2Status>();
    registry->RegisterFunction<LabManagementGetSelfFunction>();
    registry->RegisterFunction<LabPrivacyGetFunction>();
    registry->RegisterFunction<LabPrivacySetFunction>();
    registry->RegisterFunction<LabPrivacyClearFunction>();
    registry->RegisterFunction<LabIdentityGetAuthTokenFunction>();
    registry->RegisterFunction<LabIdentityRemoveCachedAuthTokenFunction>();
    registry->RegisterFunction<LabIdentityClearAllCachedAuthTokensFunction>();
    registry->RegisterFunction<LabIdentityGetProfileUserInfoFunction>();
    registry->RegisterFunction<LabBrowsingDataRemoveFunction>();
    registry->RegisterFunction<LabRuntimeGetPlatformInfoFunction>();
    registry->RegisterFunction<LabRuntimeRequestUpdateCheckFunction>();
    registry->RegisterFunction<LabCookiesGetAllFunction>();
    registry->RegisterFunction<LabCookiesSetFunction>();
    registry->RegisterFunction<LabCookiesRemoveFunction>();
    registry->RegisterFunction<LabCookiesGetAllCookieStoresFunction>();
    registry->RegisterFunction<LabWindowsGetFunction>();
    registry->RegisterFunction<LabWindowsCreateFunction>();
    registry->RegisterFunction<LabWindowsUpdateFunction>();
    registry->RegisterFunction<LabWindowsRemoveFunction>();
    registry->RegisterFunction<LabTabsCaptureVisibleTabFunction>();
    registry->RegisterFunction<LabActionGetUserSettingsFunction>();
    RegisterExtensionDownloadFunctions(*registry);
    registry->RegisterFunction<LabActionSetPopupFunction>();
    registry->RegisterFunction<LabActionGetPopupFunction>();
    registry->RegisterFunction<LabActionSetTitleFunction>();
    registry->RegisterFunction<LabActionGetTitleFunction>();
    registry->RegisterFunction<LabActionGetBadgeTextFunction>();
    registry->RegisterFunction<LabActionEnableFunction>();
    registry->RegisterFunction<LabActionDisableFunction>();
    registry->RegisterFunction<LabActionIsEnabledFunction>();
    registry->RegisterFunction<LabActionOpenPopupFunction>();
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
    RegisterExtensionNotificationFunctions(*registry);
#endif
    registry->RegisterFunction<LabRuntimeOpenOptionsPageFunction>();
    registry->RegisterFunction<extensions::RuntimeGetContextsFunction>();
    registry->RegisterFunction<extensions::UserScriptsRegisterFunction>();
    registry->RegisterFunction<extensions::UserScriptsGetScriptsFunction>();
    registry->RegisterFunction<extensions::UserScriptsUnregisterFunction>();
    registry->RegisterFunction<extensions::UserScriptsUpdateFunction>();
    registry->RegisterFunction<extensions::UserScriptsExecuteFunction>();
    registry->RegisterFunction<extensions::UserScriptsConfigureWorldFunction>();
    registry->RegisterFunction<extensions::UserScriptsGetWorldConfigurationsFunction>();
    registry->RegisterFunction<extensions::UserScriptsResetWorldConfigurationFunction>();
    registry->RegisterFunction<extensions::OffscreenCreateDocumentFunction>();
    registry->RegisterFunction<extensions::OffscreenCloseDocumentFunction>();
    registry->RegisterFunction<extensions::OffscreenHasDocumentFunction>();
    registry->RegisterFunction<LabRuntimeSetUninstallUrlFunction>();
    registry->RegisterFunction<LabGetFontListFunction>();
    registry->RegisterFunction<LabCommandsGetAllFunction>();
    registry->RegisterFunction<LabActionSetIconFunction>();
    registry->RegisterFunction<LabActionSetBadgeTextFunction>();
    registry->RegisterFunction<LabActionSetBadgeBackgroundColorFunction>();
    registry->RegisterFunction<LabContextMenusCreateFunction>();
    registry->RegisterFunction<LabContextMenusRemoveAllFunction>();
    registry->RegisterFunction<extensions::ScriptingExecuteScriptFunction>();
    registry->RegisterFunction<extensions::ScriptingInsertCSSFunction>();
    registry->RegisterFunction<extensions::ScriptingRemoveCSSFunction>();
    registry->RegisterFunction<extensions::ScriptingRegisterContentScriptsFunction>();
    registry->RegisterFunction<extensions::ScriptingGetRegisteredContentScriptsFunction>();
    registry->RegisterFunction<extensions::ScriptingUnregisterContentScriptsFunction>();
    registry->RegisterFunction<extensions::ScriptingUpdateContentScriptsFunction>();
    registry->RegisterFunction<LabTabsQueryFunction>();
    registry->RegisterFunction<LabTabsCreateFunction>();
    registry->RegisterFunction<LabTabsUpdateFunction>();
    registry->RegisterFunction<LabTabsRemoveFunction>();
    registry->RegisterFunction<LabTabsMoveFunction>();
    registry->RegisterFunction<LabTabsReloadFunction>();
    registry->RegisterFunction<LabTabsDuplicateFunction>();
    registry->RegisterFunction<LabTabsGoBackFunction>();
    registry->RegisterFunction<LabTabsGoForwardFunction>();
    registry->RegisterFunction<LabTabsGetFunction>();
    registry->RegisterFunction<LabTabsGetCurrentFunction>();
    registry->RegisterFunction<LabTabsDiscardFunction>();
    registry->RegisterFunction<extensions::DeclarativeNetRequestUpdateDynamicRulesFunction>();
    registry->RegisterFunction<extensions::DeclarativeNetRequestGetDynamicRulesFunction>();
    registry->RegisterFunction<extensions::DeclarativeNetRequestUpdateSessionRulesFunction>();
    registry->RegisterFunction<extensions::DeclarativeNetRequestGetSessionRulesFunction>();
    registry->RegisterFunction<extensions::DeclarativeNetRequestUpdateEnabledRulesetsFunction>();
    registry->RegisterFunction<extensions::DeclarativeNetRequestGetEnabledRulesetsFunction>();
    registry->RegisterFunction<extensions::DeclarativeNetRequestUpdateStaticRulesFunction>();
    registry->RegisterFunction<extensions::DeclarativeNetRequestGetDisabledRuleIdsFunction>();
    registry->RegisterFunction<extensions::DeclarativeNetRequestGetMatchedRulesFunction>();
    registry->RegisterFunction<extensions::DeclarativeNetRequestSetExtensionActionOptionsFunction>();
    registry->RegisterFunction<extensions::DeclarativeNetRequestIsRegexSupportedFunction>();
    registry->RegisterFunction<extensions::DeclarativeNetRequestGetAvailableStaticRuleCountFunction>();
    registry->RegisterFunction<extensions::DeclarativeNetRequestTestMatchOutcomeFunction>();
    registry->RegisterFunction<LabSidePanelGetOptionsFunction>();
    registry->RegisterFunction<LabSidePanelSetOptionsFunction>();
    registry->RegisterFunction<LabCookiesGetFunction>();
    registry->RegisterFunction<LabWindowsGetCurrentFunction>();
    registry->RegisterFunction<LabWindowsGetAllFunction>();
    registry->RegisterFunction<LabSidePanelOpenFunction>();
    registry->RegisterFunction<LabSidePanelCloseFunction>();
    registry->RegisterFunction<LabSidePanelGetLayoutFunction>();
    registry->RegisterFunction<LabSidePanelGetBehaviorFunction>();
    registry->RegisterFunction<LabSidePanelSetBehaviorFunction>();
    registry->RegisterFunction<extensions::WebRequestInternalAddEventListenerFunction>();
    registry->RegisterFunction<extensions::WebRequestInternalEventHandledFunction>();
    registry->RegisterFunction<extensions::WebRequestHandlerBehaviorChangedFunction>();
    registry->RegisterFunction<LabWebNavigationGetFrameFunction>();
    registry->RegisterFunction<LabWebNavigationGetAllFramesFunction>();
    registry->RegisterFunction<extensions::IdleQueryStateFunction>();
    registry->RegisterFunction<extensions::IdleSetDetectionIntervalFunction>();
    registry->RegisterFunction<extensions::IdleGetAutoLockDelayFunction>();
    registry->RegisterFunction<extensions::AlarmsCreateFunction>();
    registry->RegisterFunction<extensions::AlarmsGetFunction>();
    registry->RegisterFunction<extensions::AlarmsGetAllFunction>();
    registry->RegisterFunction<extensions::AlarmsClearFunction>();
    registry->RegisterFunction<extensions::AlarmsClearAllFunction>();
    registry->RegisterFunction<extensions::PermissionsContainsFunction>();
    registry->RegisterFunction<LabIsAllowedIncognitoAccessFunction>();
    registry->RegisterFunction<extensions::PermissionsGetAllFunction>();
    registry->RegisterFunction<LabPermissionsRequestFunction>();
    registry->RegisterFunction<LabPermissionsRemoveFunction>();
    registry->RegisterFunction<extensions::StorageStorageAreaGetFunction>();
    registry->RegisterFunction<extensions::StorageStorageAreaGetKeysFunction>();
    registry->RegisterFunction<extensions::StorageStorageAreaSetFunction>();
    registry->RegisterFunction<extensions::StorageStorageAreaRemoveFunction>();
    registry->RegisterFunction<extensions::StorageStorageAreaClearFunction>();
    registry->RegisterFunction<extensions::StorageStorageAreaGetBytesInUseFunction>();
    registry->RegisterFunction<extensions::StorageStorageAreaSetAccessLevelFunction>();
  }
};

class LabExtensionSystem final : public extensions::ExtensionSystem {
 public:
  explicit LabExtensionSystem(BrowserContext& context);
  ~LabExtensionSystem() override;

  void InitForRegularProfile(bool extensions_enabled) override {
    if (!extensions_enabled || is_ready())
      return;
    state_store_ = std::make_unique<extensions::StateStore>(
        &*context_, store_factory_, extensions::StateStore::BackendType::STATE,
        /*deferred_load=*/false);
    dynamic_user_scripts_store_ = std::make_unique<extensions::StateStore>(
        &*context_, store_factory_, extensions::StateStore::BackendType::SCRIPTS,
        /*deferred_load=*/false);
    quota_service_ = std::make_unique<extensions::QuotaService>();
    download_events_ = CreateExtensionDownloadEvents(*context_);
    ready_.Signal();
  }
  extensions::ExtensionService* extension_service() override { return nullptr; }
  extensions::ManagementPolicy* management_policy() override { return nullptr; }
  extensions::ServiceWorkerManager* service_worker_manager() override {
    return nullptr;
  }
  extensions::UserScriptManager* user_script_manager() override {
    if (!user_scripts_) {
      InitForRegularProfile(true);
      user_scripts_ = std::make_unique<extensions::UserScriptManager>(&*context_);
    }
    return user_scripts_.get();
  }
  void Shutdown() override {
    download_events_.reset();
    user_scripts_.reset();
    dynamic_user_scripts_store_.reset();
    state_store_.reset();
  }
  extensions::StateStore* state_store() override { return state_store_.get(); }
  extensions::StateStore* rules_store() override { return nullptr; }
  extensions::StateStore* dynamic_user_scripts_store() override {
    return dynamic_user_scripts_store_.get();
  }
  scoped_refptr<value_store::ValueStoreFactory> store_factory() override {
    return store_factory_;
  }
  extensions::QuotaService* quota_service() override {
    return quota_service_.get();
  }
  extensions::AppSorting* app_sorting() override { return nullptr; }
  const base::OneShotEvent& ready() const override { return ready_; }
  bool is_ready() const override { return ready_.is_signaled(); }
  extensions::ContentVerifier* content_verifier() override { return nullptr; }
  void InstallUpdate(const extensions::ExtensionId&, const std::string&,
                     const base::FilePath&, bool, InstallUpdateCallback) override {
    NOTREACHED();
  }
  void PerformActionBasedOnOmahaAttributes(
      const extensions::ExtensionId&, const base::DictValue&) override {}

 private:
  const raw_ref<BrowserContext> context_;
  base::OneShotEvent ready_;
  const scoped_refptr<value_store::ValueStoreFactory> store_factory_;
  std::unique_ptr<extensions::StateStore> state_store_;
  std::unique_ptr<extensions::StateStore> dynamic_user_scripts_store_;
  std::unique_ptr<extensions::QuotaService> quota_service_;
  std::unique_ptr<ExtensionDownloadEvents> download_events_;
  std::unique_ptr<extensions::UserScriptManager> user_scripts_;
};

LabExtensionSystem::LabExtensionSystem(BrowserContext& context)
    : context_(context),
      // StorageFrontend can capture the factory before the first extension is
      // activated. It keeps that reference for the lifetime of the profile.
      store_factory_(base::MakeRefCounted<value_store::ValueStoreFactoryImpl>(
          context.GetPath())) {}
LabExtensionSystem::~LabExtensionSystem() = default;

class LabExtensionSystemFactory final : public extensions::ExtensionSystemProvider {
 public:
  static LabExtensionSystemFactory* GetInstance() {
    static base::NoDestructor<LabExtensionSystemFactory> factory;
    return factory.get();
  }

  extensions::ExtensionSystem* GetForBrowserContext(
      BrowserContext* context) override {
    return static_cast<extensions::ExtensionSystem*>(
        GetServiceForBrowserContext(context, /*create=*/true));
  }

 private:
  friend class base::NoDestructor<LabExtensionSystemFactory>;

  LabExtensionSystemFactory()
      : ExtensionSystemProvider("ZitterContentExtensionSystem",
                                 BrowserContextDependencyManager::GetInstance()) {
    DependsOn(extensions::ExtensionRegistryFactory::GetInstance());
  }

  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      BrowserContext* context) const override {
    return std::make_unique<LabExtensionSystem>(*context);
  }

  BrowserContext* GetBrowserContextToUse(BrowserContext* context) const override {
    return extensions::ExtensionsBrowserClient::Get()->GetContextRedirectedToOriginal(
        context);
  }
};

// Provides the context selection rules for the isolated MV3 fixture.
bool IsWebAccessibleExtensionResource(const extensions::Extension& extension,
                                      const GURL& url,
                                      const url::Origin& initiator,
                                      const GURL& upstream_url) {
  if (!initiator.GetURL().SchemeIsHTTPOrHTTPS() ||
      !url.SchemeIs(extensions::kExtensionScheme) ||
      (url.host() != extension.id() && url.host() != extension.guid())) {
    return false;
  }
  if (!upstream_url.is_empty()) {
    GURL::Replacements canonical_host;
    canonical_host.SetHostStr(extension.id());
    if (upstream_url.host() != extension.guid() ||
        upstream_url.ReplaceComponents(canonical_host) != url) {
      return false;
    }
  }
  return extensions::WebAccessibleResourcesInfo::IsResourceWebAccessibleRedirect(
      &extension, url, initiator, upstream_url);
}

class ExtensionRuntimePreferences;


class LabExtensionsBrowserClient final
    : public extensions::ExtensionsBrowserClient,
      private content::RenderProcessHostObserver {
 public:
  explicit LabExtensionsBrowserClient(
      std::multimap<base::FilePath, ExtensionProbeFixture> fixtures);
  ~LabExtensionsBrowserClient() override;

  bool RegisterInstalled(BrowserContext& context,
                         scoped_refptr<const extensions::Extension> extension);
  bool ConfigureNightShift(const base::FilePath& directory);
  void Register(BrowserContext& context, BrowserContext* private_source);
  void Unregister(BrowserContext& context);
  void RegisterProcess(content::RenderProcessHost& process,
                       const extensions::Extension& extension);
  void RevokeProcesses(BrowserContext& context, const std::string& id = {});
  void SetFixtureContext(BrowserContext& context);
  bool DisableFixture(BrowserContext& context, const std::string& id);
  void UnregisterDisabledFixture(BrowserContext& context, const std::string& id);
  const ExtensionProbeFixture* fixture(BrowserContext& context,
                                      const std::string& id = {});
  std::vector<scoped_refptr<const extensions::Extension>> ConfiguredExtensions(
      BrowserContext& context);
  const extensions::Extension* EnabledFixture(BrowserContext& context,
                                              const std::string& id);
  bool HasFixturePreferences(BrowserContext& context) {
    auto* original = GetOriginalContext(&context);
    return original && fixture_prefs_.contains(original) &&
           (fixture(context) || night_shift_.contains(&context));
  }
  bool HasIsolatedStorage(const extensions::ExtensionId& id,
                          BrowserContext* context) override;

  bool IsValidTabId(BrowserContext* context, int id, bool include_incognito,
                    content::WebContents** page) const override {
    auto* target = context ? WorkspaceExtensionTab::Find(context, id) : nullptr;
    if (!target && context && include_incognito) {
      target = WorkspaceExtensionTab::Find(
          FindPrivateContext(context), id);
    }
    if (page)
      *page = target;
    return target != nullptr;
  }

  extensions::ScriptExecutor* GetScriptExecutorForTab(
      content::WebContents& page) override {
    auto* tab = WorkspaceExtensionTab::FromWebContents(&page);
    return tab ? tab->GetScriptExecutor() : nullptr;
  }

  void Init() override {}
  bool IsShuttingDown() override { return false; }
  bool AreExtensionsDisabled(const base::CommandLine&,
                             BrowserContext*) override {
    return true;
  }
  bool AreExtensionsDisabledForContext(BrowserContext*) override { return true; }
  bool IsValidContext(void* context) override;
  bool IsSameContext(BrowserContext* first, BrowserContext* second) override;
  bool HasOffTheRecordContext(BrowserContext* context) override {
    return GetOffTheRecordContext(context) != nullptr;
  }
  BrowserContext* GetOffTheRecordContext(BrowserContext* context) override {
    return FindPrivateContext(context);
  }
  BrowserContext* GetOriginalContext(BrowserContext* context) override;
  BrowserContext* GetContextRedirectedToOriginal(BrowserContext* context) override;
  BrowserContext* GetContextRedirectedToOriginalWithoutAshInternals(
      BrowserContext* context) override;
  BrowserContext* GetContextOwnInstance(BrowserContext* context) override;
  BrowserContext* GetContextForOriginalOnly(BrowserContext* context) override;
  bool IsGuestSession(BrowserContext*) const override { return false; }
  bool IsExtensionIncognitoEnabled(const extensions::ExtensionId& id,
                                   BrowserContext* context) const override {
    auto* registry = extensions::ExtensionRegistry::Get(context);
    return registry && IsExtensionIncognitoEnabled(
        registry->enabled_extensions().GetByID(id), context);
  }
  bool IsExtensionIncognitoEnabled(const extensions::Extension* extension,
                                   BrowserContext* context) const override {
    return extension && extensions::util::CanBeIncognitoEnabled(extension) &&
           IsManagedExtensionSession() && IsManagedPackage(*extension, context) &&
           extensions::ExtensionPrefs::Get(context)->IsIncognitoEnabled(extension->id());
  }
  bool CanExtensionCrossIncognito(const extensions::Extension* extension,
                                  BrowserContext* context) const override {
    return IsExtensionIncognitoEnabled(extension, context) &&
           extensions::IncognitoInfo::IsSpanningMode(extension);
  }

  bool IsManagedPackage(const extensions::Extension& extension,
                        BrowserContext* context) const {
    if (const auto owner = private_contexts_.find(context); owner != private_contexts_.end())
      context = owner->second;
    return extension.path().DirName().DirName() ==
        context->GetPath().AppendASCII(extensions::kInstallDirectoryName);
  }

  void UpdateIncognitoAccess(BrowserContext& context,
                             const extensions::Extension& extension, bool allowed);
  extensions::BrowserContextLifetimeTracker* GetBrowserContextLifetimeTracker()
      override {
    return nullptr;
  }
  base::FilePath GetBundleResourcePath(const network::ResourceRequest&,
                                     const base::FilePath&,
                                     int*) const override {
    return {};
  }
  void LoadResourceFromResourceBundle(
      const network::ResourceRequest&,
      mojo::PendingReceiver<network::mojom::URLLoader>,
      const base::FilePath&, int, scoped_refptr<net::HttpResponseHeaders>,
      mojo::PendingRemote<network::mojom::URLLoaderClient>,
      BrowserContext*) override {}
  bool AllowCrossRendererResourceLoad(
      const network::ResourceRequest& request, network::mojom::RequestDestination,
      ui::PageTransition, content::ChildProcessId child_id, bool incognito,
      const extensions::Extension* extension, const extensions::ExtensionSet& enabled,
      const extensions::ProcessMap& process_map, const GURL& upstream_url) override {
    if (child_id.is_null() || process_map.Contains(child_id) ||
        !extension || enabled.GetByID(extension->id()) != extension ||
        !request.request_initiator) {
      return false;
    }
    const auto& initiator = *request.request_initiator;
    // The frame-bound factory checks the committed sandboxed page before
    // forwarding an opaque request. Keep that renderer outside ProcessMap.
    if (initiator.opaque()) {
      return upstream_url.is_empty() && request.url.GetHost() == extension->id() &&
             initiator.GetTupleOrPrecursorTupleIfOpaque() ==
                 extension->origin().GetTupleOrPrecursorTupleIfOpaque();
    }
    return IsWebAccessibleExtensionResource(*extension, request.url, initiator,
                                            upstream_url);
  }
  void GetEarlyExtensionPrefsObservers(
      BrowserContext*,
      std::vector<extensions::EarlyExtensionPrefsObserver*>*) const override {}
  extensions::ProcessManagerDelegate* GetProcessManagerDelegate()
      const override {
    return nullptr;
  }
  mojo::PendingRemote<network::mojom::URLLoaderFactory>
  GetControlledFrameEmbedderURLLoader(
      const url::Origin&, content::FrameTreeNodeId, BrowserContext*) override {
    return {};
  }
  std::unique_ptr<extensions::ExtensionHostDelegate>
  CreateExtensionHostDelegate() override {
    return std::make_unique<LabExtensionHostDelegate>();
  }
  bool DidVersionUpdate(BrowserContext*) override { return false; }
  void PermitExternalProtocolHandler() override {}
  bool IsInDemoMode() override { return false; }
  bool IsScreensaverInDemoMode(const std::string&) override { return false; }
  bool IsRunningInForcedAppMode() override { return false; }
  bool IsAppModeForcedForApp(const extensions::ExtensionId&) override {
    return false;
  }
  bool IsLoggedInAsPublicAccount() override { return false; }
  extensions::ExtensionSystemProvider* GetExtensionSystemFactory() override {
    return LabExtensionSystemFactory::GetInstance();
  }
  void RegisterBrowserInterfaceBindersForFrame(
      mojo::BinderMapWithContext<content::RenderFrameHost*>*,
      content::RenderFrameHost*, const extensions::Extension*) const override {}
  std::unique_ptr<extensions::RuntimeAPIDelegate> CreateRuntimeAPIDelegate(
      BrowserContext*) const override {
    return nullptr;
  }
  const extensions::ComponentExtensionResourceManager*
  GetComponentExtensionResourceManager() override {
    return nullptr;
  }
  void BroadcastEventToRenderers(extensions::events::HistogramValue,
                                 const std::string&, base::ListValue,
                                 bool) override {}
  extensions::ExtensionCache* GetExtensionCache() override { return nullptr; }
  bool IsBackgroundUpdateAllowed() override { return false; }
  bool IsMinBrowserVersionSupported(const std::string&) override { return false; }
  void CreateExtensionWebContentsObserver(content::WebContents* contents) override {
    if (!GetOriginalContext(contents->GetBrowserContext()))
      return;
    LabExtensionWebContentsObserver::CreateForWebContents(contents);
    LabExtensionWebContentsObserver::FromWebContents(contents)->Initialize();
  }
  extensions::ExtensionWebContentsObserver* GetExtensionWebContentsObserver(
      content::WebContents* contents) override {
    return LabExtensionWebContentsObserver::FromWebContents(contents);
  }
  void GetTabAndWindowIdForWebContents(content::WebContents* contents,
                                       int* tab_id,
                                       int* window_id) override {
    const auto* tab = contents ? WorkspaceExtensionTab::FromWebContents(contents)
                               : nullptr;
    *tab_id = tab ? tab->id() : extension_misc::kUnknownTabId;
    *window_id = tab ? kWorkspaceExtensionWindowId : extension_misc::kUnknownWindowId;
  }

  extensions::KioskDelegate* GetKioskDelegate() override {
    return &kiosk_delegate_;
  }
  extensions::SafeBrowsingDelegate* GetSafeBrowsingDelegate() override {
    return &safe_browsing_delegate_;
  }
  std::string GetApplicationLocale() override { return "en-US"; }

 private:
  BrowserContext* FindPrivateContext(BrowserContext* original) const;
  void RenderProcessHostDestroyed(content::RenderProcessHost* host) override;

  LabKioskDelegate kiosk_delegate_;
  extensions::SafeBrowsingDelegate safe_browsing_delegate_;
  std::multimap<base::FilePath, ExtensionProbeFixture> fixtures_;
  base::FilePath night_shift_directory_;
  std::optional<base::DictValue> night_shift_manifest_;
  std::map<BrowserContext*, ExtensionProbeFixture> night_shift_;
  std::set<void*> contexts_;
  std::map<BrowserContext*, BrowserContext*> private_contexts_;
  struct FixturePreferences {
    std::unique_ptr<PrefService> service;
    std::unique_ptr<ExtensionRuntimePreferences> runtime;
    bool had_installation;
  };
  std::map<BrowserContext*, FixturePreferences> fixture_prefs_;
  std::map<content::RenderProcessHost*, extensions::ExtensionId> process_extensions_;
  base::ScopedMultiSourceObservation<content::RenderProcessHost,
                                     content::RenderProcessHostObserver>
      processes_{this};
};

LabExtensionsBrowserClient::LabExtensionsBrowserClient(
    std::multimap<base::FilePath, ExtensionProbeFixture> fixtures)
    : fixtures_(std::move(fixtures)) {
  extensions::AlarmManager::GetFactoryInstance();
  AddAPIProvider(std::make_unique<LabBrowserExtensionAPIProvider>());
}

bool LabExtensionsBrowserClient::RegisterInstalled(
    BrowserContext& context,
    scoped_refptr<const extensions::Extension> extension) {
  if (!GetOriginalContext(&context) ||
      extension->path().DirName().DirName() !=
          context.GetPath().AppendASCII(extensions::kInstallDirectoryName) ||
      fixture(context, extension->id())) {
    return false;
  }
  fixtures_.emplace(context.GetPath(),
                    ExtensionProbeFixture{std::move(extension), false, false});
  fixture_prefs_.at(&context).had_installation = true;
  return true;
}

bool LabExtensionsBrowserClient::ConfigureNightShift(
    const base::FilePath& directory) {
  std::string manifest;
  if (!base::ReadFileToStringWithMaxSize(directory.AppendASCII("manifest.json"),
                                        &manifest, 64 * 1024)) {
    return false;
  }
  auto parsed = base::JSONReader::ReadDict(manifest, base::JSON_PARSE_RFC);
  if (!parsed)
    return false;
  std::u16string error;
  auto extension = extensions::Extension::Create(
      directory, extensions::mojom::ManifestLocation::kComponent, *parsed,
      extensions::Extension::REQUIRE_KEY, &error);
  if (!extension || extension->id() != kNightShiftId)
    return false;
  night_shift_directory_ = directory;
  night_shift_manifest_ = std::move(parsed);
  return true;
}

void LabExtensionsBrowserClient::Register(BrowserContext& context,
                                          BrowserContext* private_source) {
  CHECK(contexts_.insert(&context).second);
  if (context.IsOffTheRecord() && private_source && IsManagedExtensionSession()) {
    CHECK(!private_source->IsOffTheRecord());
    private_contexts_.emplace(&context, private_source);
  }
  if (!context.IsOffTheRecord()) {
    if (night_shift_manifest_) {
      std::u16string error;
      auto extension = extensions::Extension::Create(
          night_shift_directory_, extensions::mojom::ManifestLocation::kComponent,
          *night_shift_manifest_, extensions::Extension::REQUIRE_KEY, &error);
      CHECK(extension && extension->id() == kNightShiftId);
      night_shift_.emplace(&context,
                           ExtensionProbeFixture{std::move(extension), false, false});
    }
    SetFixtureContext(context);
  }
  BrowserContextDependencyManager::GetInstance()->CreateBrowserContextServices(
      &context);
  if (private_contexts_.contains(&context)) {
    for (const auto& extension : ConfiguredExtensions(context)) {
      if (EnabledFixture(context, extension->id()) &&
          extensions::IncognitoInfo::IsSplitMode(extension.get())) {
        extensions::ServiceWorkerTaskQueue::Get(&context)->ActivateExtension(extension.get());
      }
    }
  }
  if (auto entry = night_shift_.find(&context); entry != night_shift_.end()) {
    const auto& extension = entry->second.extension;
    CHECK(extensions::ExtensionRegistry::Get(&context)->AddEnabled(extension));
    ActivateFixture(context, *extension);
    extensions::ServiceWorkerTaskQueue::Get(&context)->AddPendingTask(
        extensions::LazyContextId::ForExtension(&context, extension.get()),
        base::BindOnce([](
            std::unique_ptr<extensions::LazyContextTaskQueue::ContextInfo> info) {
          if (!info || !info->browser_context ||
              info->browser_context->ShutdownStarted()) {
            return;
          }
          extensions::EventRouter::Get(info->browser_context)
              ->DispatchEventToExtension(
                  kNightShiftId, std::make_unique<extensions::Event>(
                                     extensions::events::RUNTIME_ON_STARTUP,
                                     "runtime.onStartup", base::ListValue()));
        }));
  }
}

namespace {

constexpr char kExtensionPreferences[] = "Content Extension Preferences";
constexpr char kDnrRecoveryMarkerPref[] = "dnr_recovery_marker";
constexpr char kDnrDynamicRulesetPref[] = "dnr_dynamic_ruleset";
constexpr char kDnrEnabledRulesetsPref[] = "dnr_enabled_ruleset_ids";
constexpr char kDnrKeepAllocationPref[] = "dnr_keep_excess_allocation";
constexpr char kDnrAllocatedRulesPref[] = "dnr_extension_rules_allocated";
constexpr char kDnrBadgePref[] = "dnr_use_action_count_as_badge_text";
constexpr int64_t kMaxExtensionPreferencesBytes = 4 * 1024 * 1024;

std::optional<base::DictValue> ReadExtensionPreferencesFile(
    const base::FilePath& path) {
  base::ScopedAllowBlockingForTesting allow_profile_io;
  if (base::IsLink(path))
    return std::nullopt;
  if (!base::PathExists(path))
    return base::DictValue();
  if (base::DirectoryExists(path))
    return std::nullopt;
#if BUILDFLAG(IS_POSIX)
  base::stat_wrapper_t info;
  if (base::File::Lstat(path, &info) != 0 || !S_ISREG(info.st_mode))
    return std::nullopt;
#endif
  std::string bytes;
  if (!base::ReadFileToStringWithMaxSize(path, &bytes,
                                         kMaxExtensionPreferencesBytes)) {
    return std::nullopt;
  }
  const auto saved = base::JSONReader::ReadDict(bytes, base::JSON_PARSE_RFC);
  if (!saved)
    return std::nullopt;
  const auto* settings = saved->FindDictByDottedPath(extensions::pref_names::kExtensions);
  return settings ? std::optional(settings->Clone()) : std::nullopt;
}

void WriteManagedExtensionPreferences(const base::FilePath& profile,
                                      base::DictValue snapshots) {
  base::ScopedAllowBlockingForTesting allow_profile_io;
  const auto path = profile.AppendASCII(kExtensionPreferences);
  if (!ReadExtensionPreferencesFile(path))
    return;
  base::DictValue saved;
  saved.SetByDottedPath(extensions::pref_names::kExtensions,
                       std::move(snapshots));
  auto bytes = base::WriteJson(saved);
  if (!bytes || bytes->size() > kMaxExtensionPreferencesBytes ||
      base::IsLink(path) || !base::ImportantFileWriter::WriteFileAtomically(path, *bytes)) {
    LOG(WARNING) << "Could not save managed extension permission preferences";
  }
}

}  // namespace

// Keep native runtime preferences separate from signed package trust and
// the permission records used to restore approved grants.
class ExtensionRuntimePreferences final
    : private base::ImportantFileWriter::DataSerializer {
 public:
  static std::unique_ptr<ExtensionRuntimePreferences> Open(
      const base::FilePath& profile,
      PrefService& prefs,
      const std::vector<scoped_refptr<const extensions::Extension>>& installed) {
    const auto path = profile.AppendASCII("Content Extension Rule Preferences");
    auto stored = ReadExtensionPreferencesFile(path);
    if (!stored) {
      LOG(WARNING) << "Could not read Content extension runtime preferences";
      return nullptr;
    }
    base::DictValue trusted;
    for (const auto& extension : installed) {
      if (const auto* entry = stored->FindDict(extension->id()))
        trusted.Set(extension->id(), entry->Clone());
    }
    prefs.SetBoolean(kManagerAutoPinPref,
                     stored->FindBool(kManagerAutoPinPref).value_or(false));
    auto initial = Project(trusted);
    prefs.SetDict(extensions::pref_names::kExtensions, initial.Clone());
    return base::WrapUnique(new ExtensionRuntimePreferences(path, prefs, std::move(initial)));
  }

  ~ExtensionRuntimePreferences() override {
    if (writer_.HasPendingWrite())
      writer_.DoScheduledWrite();
  }

 private:
  ExtensionRuntimePreferences(const base::FilePath& path,
                           PrefService& prefs,
                           base::DictValue initial)
      : prefs_(prefs),
        values_(std::move(initial)),
        writer_(path,
                base::ThreadPool::CreateSequencedTaskRunner(
                    {base::MayBlock(), base::TaskShutdownBehavior::BLOCK_SHUTDOWN}),
                base::Milliseconds(250)) {
    registrar_.Init(&prefs);
    registrar_.Add(
        extensions::pref_names::kExtensions,
        base::BindRepeating(&ExtensionRuntimePreferences::OnChanged,
                            base::Unretained(this)));
    registrar_.Add(kManagerAutoPinPref,
                   base::BindRepeating(&ExtensionRuntimePreferences::OnAutoPinChanged,
                                       base::Unretained(this)));
  }

  static base::DictValue Project(const base::DictValue& settings) {
    base::DictValue result;
    for (const auto [id, value] : settings) {
      if (!value.is_dict())
        continue;
      const auto& entry = value.GetDict();
      base::DictValue selected;
      for (const auto key : {extensions::ExtensionPrefs::kDNRStaticRulesetPref,
                             kDnrDynamicRulesetPref,
                             extensions::kUserScriptsWorldsConfiguration.name,
                             kWorkspacePrivacyPref.name,
                             kWorkspaceSidePanelPref.name,
                             kManagerShortcutPref}) {
        if (const auto* dict = entry.FindDict(key))
          selected.Set(key, dict->Clone());
      }
      for (const auto key : {kDnrEnabledRulesetsPref, "persistent_script_url_patterns"}) {
        if (const auto* list = entry.FindList(key))
          selected.Set(key, list->Clone());
      }
      for (const auto key : {kDnrKeepAllocationPref, kDnrBadgePref,
                             kDnrRecoveryMarkerPref,
                             kManagerPinPref,
                             "incognito",
                             extensions::UserScriptManager::kUserScriptsAllowedPref.name}) {
        if (const auto enabled = entry.FindBool(key))
          selected.Set(key, *enabled);
      }
      const auto allocated = entry.FindInt(kDnrAllocatedRulesPref);
      if (allocated && *allocated > 0 &&
          *allocated <= extensions::declarative_net_request::GetGlobalStaticRuleLimit()) {
        selected.Set(kDnrAllocatedRulesPref, *allocated);
      }
      if (!selected.empty())
        result.Set(id, std::move(selected));
    }
    return result;
  }

  void OnAutoPinChanged() { writer_.ScheduleWrite(this); }

  void OnChanged() {
    auto updated = Project(prefs_->GetDict(extensions::pref_names::kExtensions));
    if (updated == values_)
      return;
    values_ = std::move(updated);
    writer_.ScheduleWrite(this);
  }

  std::optional<std::string> SerializeData() override {
    base::DictValue saved;
    auto values = values_.Clone();
    values.Set(kManagerAutoPinPref, prefs_->GetBoolean(kManagerAutoPinPref));
    saved.SetByDottedPath(extensions::pref_names::kExtensions, std::move(values));
    auto bytes = base::WriteJson(saved);
    if (!bytes || bytes->size() > kMaxExtensionPreferencesBytes) {
      LOG(WARNING) << "Could not serialize Content extension runtime preferences";
      return std::nullopt;
    }
    return bytes;
  }

  const raw_ref<PrefService> prefs_;
  base::DictValue values_;
  base::ImportantFileWriter writer_;
  PrefChangeRegistrar registrar_;
};

LabExtensionsBrowserClient::~LabExtensionsBrowserClient() = default;

void LabExtensionsBrowserClient::Unregister(BrowserContext& context) {
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
  RemoveExtensionNotifications(context, {});
#endif
  RevokeProcesses(context);
  BrowserContextDependencyManager::GetInstance()->DestroyBrowserContextServices(
      &context);
  private_contexts_.erase(&context);
  CHECK_EQ(contexts_.erase(&context), 1u);
  night_shift_.erase(&context);
  auto stored = fixture_prefs_.extract(&context);
  if (!stored.empty() && stored.mapped().had_installation &&
      IsManagedExtensionSession()) {
    const auto [first, last] = fixtures_.equal_range(context.GetPath());
    base::DictValue snapshots;
    const auto list = [](const auto& strings) {
      base::ListValue values;
      for (const auto& string : strings)
        values.Append(string);
      return values;
    };
    for (auto item = first; item != last; ++item) {
      const auto& extension = *item->second.extension;
      const auto& permissions = extension.permissions_data()->active_permissions();
      base::DictValue active;
      active.Set("api", list(permissions.GetAPIsAsStrings()));
      active.Set("explicit_host", list(permissions.explicit_hosts().ToStringVector()));
      active.Set("scriptable_host", list(permissions.scriptable_hosts().ToStringVector()));
      snapshots.Set(extension.id(),
                    base::DictValue().Set("active_permissions", std::move(active)));
    }
    WriteManagedExtensionPreferences(context.GetPath(), std::move(snapshots));
  }
}

void LabExtensionsBrowserClient::SetFixtureContext(BrowserContext& context) {
  CHECK(!context.IsOffTheRecord());
  if (fixture_prefs_.contains(&context))
    return;
  auto registry = base::MakeRefCounted<user_prefs::PrefRegistrySyncable>();
  extensions::ExtensionPrefs::RegisterProfilePrefs(registry.get());
  extensions::PermissionsManager::RegisterProfilePrefs(registry.get());
  registry->RegisterBooleanPref(kManagerAutoPinPref, false);
  PrefServiceFactory factory;
  factory.set_user_prefs(base::MakeRefCounted<InMemoryPrefStore>());
  auto preferences = factory.Create(registry);
  auto installed = ConfiguredExtensions(context);
  if (auto entry = night_shift_.find(&context); entry != night_shift_.end())
    installed.push_back(entry->second.extension);
  auto runtime =
      ExtensionRuntimePreferences::Open(context.GetPath(), *preferences, installed);
  user_prefs::UserPrefs::Set(&context, preferences.get());
  CHECK(fixture_prefs_.try_emplace(
      &context,
      FixturePreferences{std::move(preferences), std::move(runtime), !installed.empty()})
            .second);
}

const ExtensionProbeFixture* LabExtensionsBrowserClient::fixture(
    BrowserContext& context, const std::string& id) {
  auto* original = GetOriginalContext(&context);
  if (!original)
    return nullptr;
  if (id == kNightShiftId) {
    auto entry = night_shift_.find(&context);
    if (entry != night_shift_.end())
      return &entry->second;
  }
  const auto [first, last] = fixtures_.equal_range(original->GetPath());
  const auto entry = std::find_if(first, last, [&](const auto& item) {
    return (id.empty() || item.second.extension->id() == id) &&
           (!context.IsOffTheRecord() ||
            IsExtensionIncognitoEnabled(item.second.extension.get(), &context));
  });
  return entry == last ? nullptr : &entry->second;
}

std::vector<scoped_refptr<const extensions::Extension>>
LabExtensionsBrowserClient::ConfiguredExtensions(BrowserContext& context) {
  std::vector<scoped_refptr<const extensions::Extension>> extensions;
  if (auto* original = GetOriginalContext(&context)) {
    const auto [first, last] = fixtures_.equal_range(original->GetPath());
    for (auto item = first; item != last; ++item) {
      if (!context.IsOffTheRecord() ||
          IsExtensionIncognitoEnabled(item->second.extension.get(), &context)) {
        extensions.push_back(item->second.extension);
      }
    }
  }
  return extensions;
}

const extensions::Extension* LabExtensionsBrowserClient::EnabledFixture(
    BrowserContext& context, const std::string& id) {
  if (id.empty())
    return nullptr;
  const auto* entry = fixture(context, id);
  if (!entry)
    return nullptr;
  return extensions::ExtensionRegistry::Get(&context)->GetExtensionById(
      entry->extension->id(), extensions::ExtensionRegistry::ENABLED);
}

bool LabExtensionsBrowserClient::HasIsolatedStorage(
    const extensions::ExtensionId& id,
    BrowserContext* context) {
  if (id.empty() || !GetOriginalContext(context))
    return false;
  return fixture(*context, id) != nullptr;
}

bool LabExtensionsBrowserClient::DisableFixture(BrowserContext& context,
                                                const std::string& id) {
  const auto* entry = fixture(context, id);
  const auto* enabled = EnabledFixture(context, id);
  if (!entry || enabled != entry->extension.get())
    return false;
  const auto& extension = entry->extension;
  auto* registry = extensions::ExtensionRegistry::Get(&context);
  if (!registry->AddDisabled(extension))
    return false;
  for (auto* tab : WorkspaceExtensionTab::InProfile(&context)) {
    tab->workspace().SetExtensionSidePanel(*tab, id, GURL());
    tab->ClearSidePanelOptions(id);
    if (auto* page = tab->web_contents()) {
      if (auto* granter =
              extensions::ActiveTabPermissionGranter::FromWebContents(page)) {
        granter->ClearActiveExtensionAndNotify(id);
      }
    }
  }
  CHECK(registry->RemoveEnabled(extension->id()));
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
  RemoveExtensionNotifications(context, id);
#endif
  registry->TriggerOnUnloaded(extension.get(),
                              extensions::UnloadedExtensionReason::DISABLE);
  extensions::RendererStartupHelperFactory::GetForBrowserContext(&context)
      ->OnExtensionUnloaded(*extension);
  extensions::ServiceWorkerTaskQueue::Get(&context)
      ->DeactivateExtension(extension.get());
  RevokeProcesses(context, id);
  if (auto* private_context = GetOffTheRecordContext(&context);
      private_context && IsExtensionIncognitoEnabled(extension.get(), &context)) {
    if (extensions::IncognitoInfo::IsSplitMode(extension.get()))
      extensions::ServiceWorkerTaskQueue::Get(private_context)->DeactivateExtension(extension.get());
    RevokeProcesses(*private_context, id);
  }
  return true;
}

void LabExtensionsBrowserClient::UnregisterDisabledFixture(
    BrowserContext& context, const std::string& id) {
  auto* registry = extensions::ExtensionRegistry::Get(&context);
  const auto [first, last] = fixtures_.equal_range(context.GetPath());
  auto entry = std::find_if(first, last, [&](const auto& pair) {
    return pair.second.extension->id() == id;
  });
  CHECK(entry != last);
  auto extension = entry->second.extension;
  if (extensions::UserScriptManager::IsUserScriptsAPIPermissionAvailable(*extension)) {
    extensions::ExtensionSystem::Get(&context)
        ->user_script_manager()
        ->SetUserScriptPrefEnabled(id, false);
  }
  CHECK(registry->RemoveDisabled(id));
  ExtensionMenus::Get(context).items.erase(id);
  fixtures_.erase(entry);
  registry->TriggerOnUninstalled(extension.get(),
                                 extensions::UNINSTALL_REASON_USER_INITIATED);
  if (auto* prefs = extensions::ExtensionPrefs::Get(&context))
    prefs->DeleteExtensionPrefs(id);
}

void LabExtensionsBrowserClient::RegisterProcess(
    content::RenderProcessHost& process, const extensions::Extension& extension) {
  const auto [entry, inserted] =
      process_extensions_.try_emplace(&process, extension.id());
  if (!inserted) {
    CHECK_EQ(entry->second, extension.id());
    return;
  }
  CHECK(extensions::ProcessMap::Get(process.GetBrowserContext())
            ->Insert(extension.id(), process.GetID()));
  processes_.AddObservation(&process);
  content::ChildProcessSecurityPolicy::GetInstance()->GrantRequestOrigin(
      process.GetDeprecatedID(), extension.origin());
}

void LabExtensionsBrowserClient::RevokeProcesses(BrowserContext& context,
                                                 const std::string& id) {
  std::vector<content::RenderProcessHost*> matching;
  for (const auto& [process, extension_id] : process_extensions_) {
    if (process->GetBrowserContext() == &context &&
        (id.empty() || id == extension_id)) {
      matching.push_back(process);
    }
  }
  for (auto* process : matching)
    RenderProcessHostDestroyed(process);
}

void LabExtensionsBrowserClient::RenderProcessHostDestroyed(
    content::RenderProcessHost* host) {
  extensions::ProcessMap::Get(host->GetBrowserContext())->Remove(host->GetID());
  CHECK_EQ(process_extensions_.erase(host), 1u);
  processes_.RemoveObservation(host);
}

bool LabExtensionsBrowserClient::IsValidContext(void* context) {
  return contexts_.contains(context);
}

bool LabExtensionsBrowserClient::IsSameContext(BrowserContext* first,
                                                BrowserContext* second) {
  auto* original = GetOriginalContext(first);
  return original && original == GetOriginalContext(second);
}

BrowserContext* LabExtensionsBrowserClient::FindPrivateContext(
    BrowserContext* context) const {
  const auto entry = std::ranges::find_if(private_contexts_, [context](const auto& pair) {
    return pair.second == context;
  });
  return entry == private_contexts_.end() ? nullptr : entry->first;
}

BrowserContext* LabExtensionsBrowserClient::GetOriginalContext(BrowserContext* context) {
  if (!IsValidContext(context))
    return nullptr;
  const auto private_entry = private_contexts_.find(context);
  if (private_entry != private_contexts_.end())
    return private_entry->second;
  return context->IsOffTheRecord() ? nullptr : context;
}

BrowserContext* LabExtensionsBrowserClient::GetContextRedirectedToOriginal(
    BrowserContext* context) {
  return GetOriginalContext(context);
}

BrowserContext*
LabExtensionsBrowserClient::GetContextRedirectedToOriginalWithoutAshInternals(
    BrowserContext* context) {
  return GetOriginalContext(context);
}

BrowserContext* LabExtensionsBrowserClient::GetContextOwnInstance(
    BrowserContext* context) {
  return GetOriginalContext(context) ? context : nullptr;
}

BrowserContext* LabExtensionsBrowserClient::GetContextForOriginalOnly(
    BrowserContext* context) {
  return IsValidContext(context) && !context->IsOffTheRecord() ? context : nullptr;
}

void LabExtensionsBrowserClient::UpdateIncognitoAccess(
    BrowserContext& context, const extensions::Extension& extension, bool allowed) {
  auto* registry = extensions::ExtensionRegistry::Get(&context);
  auto* prefs = extensions::ExtensionPrefs::Get(&context);
  if (!registry->enabled_extensions().Contains(extension.id())) {
    prefs->SetIsIncognitoEnabled(extension.id(), allowed);
    return;
  }

  // Reload the extension so existing renderer bindings, user scripts and
  // worker listeners pick up the new incognito setting together.
  auto* renderer = extensions::RendererStartupHelperFactory::GetForBrowserContext(&context);
  auto* regular_workers = extensions::ServiceWorkerTaskQueue::Get(&context);
  auto* private_context = GetOffTheRecordContext(&context);
  auto* private_workers = private_context && extensions::IncognitoInfo::IsSplitMode(&extension)
      ? extensions::ServiceWorkerTaskQueue::Get(private_context) : nullptr;
  CHECK(registry->RemoveEnabled(extension.id()));
  registry->TriggerOnUnloaded(&extension, extensions::UnloadedExtensionReason::UPDATE);
  renderer->OnExtensionUnloaded(extension);
  regular_workers->DeactivateExtension(&extension);
  // The private worker's partition is still addressable before revocation.
  if (private_workers && !allowed)
    private_workers->DeactivateExtension(&extension);
  prefs->SetIsIncognitoEnabled(extension.id(), allowed);
  CHECK(registry->AddEnabled(&extension));
  regular_workers->ActivateExtension(&extension);
  if (private_workers && allowed)
    private_workers->ActivateExtension(&extension);
  if (private_context && !allowed)
    RevokeProcesses(*private_context, extension.id());

  renderer->OnExtensionLoaded(extension);
  registry->TriggerOnLoaded(&extension);
  CHECK(registry->AddReady(&extension));
  registry->TriggerOnReady(&extension);
}

LabExtensionsBrowserClient* GetClient() {
  return static_cast<LabExtensionsBrowserClient*>(
      extensions::ExtensionsBrowserClient::Get());
}

}  // namespace

void PrepareExtensionRegistryProbe(
    std::multimap<base::FilePath, ExtensionProbeFixture> fixtures) {
  static base::NoDestructor<LabExtensionsAPIClient> api_client;
  static base::NoDestructor<LabExtensionsBrowserClient> client(std::move(fixtures));
  extensions::ExtensionsBrowserClient::Set(client.get());
  LabExtensionSystemFactory::GetInstance();
  extensions::IdleManagerFactory::GetInstance();
  extensions::WebRequestAPI::GetFactoryInstance();
  extensions::declarative_net_request::RulesMonitorService::GetFactoryInstance();
  extensions::MessageService::GetFactoryInstance();
  extensions::OffscreenDocumentManager::GetFactory();
  extensions::UserScriptWorldConfigurationManager::GetFactory();
  extensions::EnsureExtensionURLLoaderFactoryShutdownNotifierFactoryBuilt();
}

void AttachWebStoreInstall(content::WebContents& page, WebStoreInstall install) {
  page.SetUserData(&kWebStoreInstallKey,
                   std::make_unique<WebStoreInstallHandler>(std::move(install)));
}

std::vector<scoped_refptr<const extensions::Extension>> GetConfiguredExtensions(
    BrowserContext& context) {
  if (auto* client = GetClient())
    return client->ConfiguredExtensions(context);
  return {};
}

bool RegisterInstalledExtension(
    BrowserContext& context,
    scoped_refptr<const extensions::Extension> extension) {
  auto* client = GetClient();
  return client && client->RegisterInstalled(context, std::move(extension));
}

void WorkspaceExtensionTabDeleter::operator()(WorkspaceExtensionTab* tab) const {
  delete tab;
}

WorkspaceExtensionTabOwner CreateWorkspaceExtensionTab(
    BrowserContext& context, WorkspaceLab& workspace) {
  auto* client = GetClient();
  if (!client || !client->GetOriginalContext(&context))
    return nullptr;
  return WorkspaceExtensionTabOwner(new WorkspaceExtensionTab(context, workspace));
}

void SetWorkspaceExtensionTabPage(WorkspaceExtensionTab* tab,
                                  content::WebContents* page) {
  if (tab)
    tab->SetPage(page);
}

void SetWorkspaceExtensionTabPageUrl(WorkspaceExtensionTab* tab,
                                     const GURL& url) {
  if (tab)
    tab->SetExtensionPageUrl(url);
}

void UpdateWorkspaceExtensionTab(WorkspaceExtensionTab* tab, int index,
                                 bool active, bool highlighted, bool pinned,
                                 const GURL& deferred_url,
                                 std::u16string_view deferred_title) {
  if (tab)
    tab->Update(index, active, highlighted, pinned, deferred_url, deferred_title);
}

void GrantWorkspaceExtensionActiveTab(content::WebContents& page,
                                      const std::string& id) {
  const auto* tab = WorkspaceExtensionTab::FromWebContents(&page);
  auto* profile = page.GetBrowserContext();
  auto* client = GetClient();
  const auto* extension = client ? client->EnabledFixture(*profile, id) : nullptr;
  const auto& url = page.GetLastCommittedURL();
  if (!tab || !tab->highlighted() ||
      !extension || !extension->permissions_data()->HasAPIPermission(
                        extensions::mojom::APIPermissionID::kActiveTab) ||
      !url.SchemeIsHTTPOrHTTPS() || extension_urls::IsWebstoreDomain(url) ||
      !url::Origin::Create(url).IsSameOriginWith(page.GetVisibleURL()) ||
      !page.GetPrimaryMainFrame()->IsRenderFrameLive() ||
      page.GetPrimaryMainFrame()->GetLastCommittedOrigin().opaque()) {
    return;
  }
  auto* observer = LabExtensionWebContentsObserver::FromWebContents(&page);
  if (!observer || !observer->SetWorkspaceTabId(tab->id()))
    return;
  extensions::ActiveTabPermissionGranter::CreateForWebContents(
      &page, tab->id(), profile);
  extensions::ActiveTabPermissionGranter::FromWebContents(&page)
      ->GrantIfRequested(extension);
}

void DispatchWorkspaceExtensionWindowEvent(BrowserContext& context,
                                          bool focused, bool removed) {
  if (context.ShutdownStarted() ||
      (!user_prefs::UserPrefs::IsInitialized(&context) &&
       !CanLoadExtensionProbeResources(context))) {
    return;
  }
  auto* router = extensions::EventRouter::Get(&context);
  const char* event = removed ? "windows.onRemoved" : "windows.onFocusChanged";
  if (router && router->HasEventListener(event)) {
    router->BroadcastEvent(std::make_unique<extensions::Event>(
        removed ? extensions::events::WINDOWS_ON_REMOVED
                : extensions::events::WINDOWS_ON_FOCUS_CHANGED,
        event, base::ListValue().Append(removed || focused ? kWorkspaceExtensionWindowId
                                                          : extension_misc::kUnknownWindowId),
        &context, extensions::mojom::ContextType::kPrivilegedExtension));
  }
}

GURL WorkspaceExtensionPopupUrl(BrowserContext& context, const std::string& id,
                                int tab_id) {
  auto* client = GetClient();
  const auto* extension = client ? client->EnabledFixture(context, id) : nullptr;
  auto* action = extension
      ? extensions::ExtensionActionManager::Get(&context)->GetExtensionAction(*extension)
      : nullptr;
  if (!action)
    return {};
  const GURL url = action->GetPopupUrl(tab_id);
  return url.is_valid() && url::Origin::Create(url) == extension->origin() ? url : GURL();
}

BrowserContext* WorkspaceExtensionPopupContext(BrowserContext& context,
                                              const std::string& id) {
  auto* client = GetClient();
  const auto* extension = client ? client->EnabledFixture(context, id) : nullptr;
  if (!extension)
    return nullptr;
  if (context.IsOffTheRecord() && extensions::IncognitoInfo::IsSpanningMode(extension))
    return client->GetOriginalContext(&context);
  return &context;
}

void SetWorkspaceExtensionPopupTabContext(content::WebContents& popup,
                                          BrowserContext& context) {
  popup.SetUserData(&kPopupTabContext, std::make_unique<PopupTabContext>(context));
}

void DispatchWorkspaceExtensionActionClicked(BrowserContext& context, const std::string& id,
                                             content::WebContents* page) {
  auto* client = GetClient();
  const auto* extension = client ? client->EnabledFixture(context, id) : nullptr;
  if (!extension || !extensions::ActionInfo::GetExtensionActionInfo(extension))
    return;
  const WorkspaceExtensionTab* tab = WorkspaceExtensionTab::ActiveInProfile(&context);
  if (page && page->GetBrowserContext() == &context) {
    if (const auto* selected = WorkspaceExtensionTab::FromWebContents(page)) {
      tab = selected;
      GrantWorkspaceExtensionActiveTab(*page, id);
    }
  }
  if (!tab)
    return;
  auto event = std::make_unique<extensions::Event>(
      extensions::events::ACTION_ON_CLICKED, "action.onClicked",
      base::ListValue().Append(tab->MetadataFor(*extension)), &context);
  event->user_gesture = extensions::EventRouter::UserGestureState::kEnabled;
  extensions::EventRouter::Get(&context)->DispatchEventToExtension(id, std::move(event));
}

void DispatchWorkspaceExtensionCommand(BrowserContext& profile,
                                       const extensions::Extension& extension,
                                       const std::string& command,
                                       content::WebContents* active_page) {
  auto* client = GetClient();
  if (!client || client->EnabledFixture(profile, extension.id()) != &extension)
    return;
  base::ListValue args;
  args.Append(command);
  const auto* tab = WorkspaceExtensionTab::ActiveInProfile(&profile);
  if (active_page && active_page->GetBrowserContext() == &profile) {
    if (const auto* selected = WorkspaceExtensionTab::FromWebContents(active_page)) {
      tab = selected;
      GrantWorkspaceExtensionActiveTab(*active_page, extension.id());
    }
  }
  if (tab)
    args.Append(tab->MetadataFor(extension));
  auto event = std::make_unique<extensions::Event>(
      extensions::events::COMMANDS_ON_COMMAND, "commands.onCommand",
      std::move(args), &profile);
  event->user_gesture = extensions::EventRouter::UserGestureState::kEnabled;
  extensions::EventRouter::Get(&profile)->DispatchEventToExtension(
      extension.id(), std::move(event));
}

std::vector<prototype::PageMenuItem> WorkspaceExtensionPageMenu(
    content::RenderFrameHost& frame, const content::ContextMenuParams& params) {
  auto& profile = *frame.GetBrowserContext();
  auto* client = GetClient();
  auto* page = content::WebContents::FromRenderFrameHost(&frame);
  if (!client || profile.IsOffTheRecord() || !page ||
      !frame.GetLastCommittedURL().SchemeIsHTTPOrHTTPS() ||
      !page->GetLastCommittedURL().SchemeIsHTTPOrHTTPS() ||
      extension_urls::IsWebstoreDomain(frame.GetLastCommittedURL()) ||
      extension_urls::IsWebstoreDomain(page->GetLastCommittedURL()) ||
      !WorkspaceExtensionTab::FromWebContents(page)) {
    return {};
  }
  const bool page_context = params.link_url.is_empty() &&
      params.src_url.is_empty() && params.selection_text.empty() && !params.is_editable;
  auto& menus = ExtensionMenus::Get(profile);
  std::vector<prototype::PageMenuItem> result;
  for (const auto& [id, items] : menus.items) {
    const auto* extension = client->EnabledFixture(profile, id);
    if (!extension || !CanUseExtensionMenus(profile, *extension))
      continue;
    const auto make_item = [&](const ExtensionMenuItem& item) {
      return prototype::PageMenuItem{
          .title = item.title,
          .run = base::BindRepeating(
              [](const content::WeakDocumentPtr& document,
                 const std::string& extension_id, uint64_t serial, bool editable) {
                auto* frame = document.AsRenderFrameHostIfValid();
                if (!frame || !frame->IsActive() || !GetClient())
                  return;
                auto& profile = *frame->GetBrowserContext();
                const auto* extension = GetClient()->EnabledFixture(profile, extension_id);
                if (!extension || !CanUseExtensionMenus(profile, *extension))
                  return;
                const auto& menus = ExtensionMenus::Get(profile).items;
                const auto group = menus.find(extension_id);
                if (group == menus.end())
                  return;
                const auto item = std::ranges::find(
                    group->second, serial, &ExtensionMenuItem::serial);
                if (item == group->second.end() || !item->enabled || item->separator)
                  return;
                auto* page = content::WebContents::FromRenderFrameHost(frame);
                const auto* tab = WorkspaceExtensionTab::FromWebContents(page);
                if (!tab || !tab->highlighted())
                  return;
                GrantWorkspaceExtensionActiveTab(*page, extension_id);
                auto info = base::DictValue().Set("menuItemId", item->id)
                                .Set("editable", editable);
                if (!item->parent.empty())
                  info.Set("parentMenuItemId", item->parent);
                if (CanReadExtensionTabUrl(*extension, page->GetLastCommittedURL(),
                                           tab->id()))
                  info.Set("pageUrl", page->GetLastCommittedURL().spec());
                if (CanReadExtensionTabUrl(*extension, frame->GetLastCommittedURL(),
                                           tab->id())) {
                  info.Set("frameUrl", frame->GetLastCommittedURL().spec());
                  info.Set("frameId", extensions::ExtensionApiFrameIdMap::GetFrameId(frame));
                }
                auto event = std::make_unique<extensions::Event>(
                    extensions::events::CONTEXT_MENUS_ON_CLICKED, "contextMenus.onClicked",
                    base::ListValue().Append(std::move(info)).Append(tab->MetadataFor(*extension)),
                    &profile);
                event->user_gesture = extensions::EventRouter::UserGestureState::kEnabled;
                extensions::EventRouter::Get(&profile)->DispatchEventToExtension(
                    extension_id, std::move(event));
              }, frame.GetWeakDocumentPtr(), id, item.serial, params.is_editable),
          .enabled = item.enabled,
          .separator = item.separator,
      };
    };
    for (const auto& item : items) {
      if (!item.parent.empty() || (!page_context && !item.all_contexts))
        continue;
      auto menu = make_item(item);
      for (const auto& child : items) {
        if (child.parent == item.id && (page_context || child.all_contexts))
          menu.children.push_back(make_item(child));
      }
      result.push_back(std::move(menu));
      if (result.size() >= 32)
        return result;
    }
  }
  return result;
}

void MarkWorkspaceExtensionTabWindowClosing(WorkspaceExtensionTab* tab) {
  if (tab)
    tab->SetWindowClosing();
}

int WorkspaceExtensionTabId(const WorkspaceExtensionTab* tab) {
  return tab ? tab->id() : extensions::ExtensionAction::kDefaultTabId;
}

int WorkspaceExtensionTabIndex(const WorkspaceExtensionTab* tab) {
  return tab ? tab->index() : -1;
}

void NotifyWorkspaceExtensionTabMoved(WorkspaceExtensionTab* tab, int from_index) {
  if (tab)
    tab->NotifyMoved(from_index);
}

void SetWorkspaceExtensionTabMuted(WorkspaceExtensionTab& tab, bool muted,
                                   std::string_view extension_id) {
  tab.SetMuted(muted, extension_id);
}

bool WorkspaceExtensionTabMuted(const WorkspaceExtensionTab& tab) {
  return tab.muted();
}

int WorkspaceExtensionTabIdForTesting(content::WebContents& contents) {
  const auto* tab = WorkspaceExtensionTab::FromWebContents(&contents);
  return tab ? tab->id() : extension_misc::kUnknownTabId;
}

bool VerifyExtensionRulePreferencesForTesting(content::BrowserContext& context,
                                               const std::string& id,
                                               bool restore) {
  using extensions::declarative_net_request::PrefsHelper;
  using extensions::declarative_net_request::RulesetID;
  auto* prefs = extensions::ExtensionPrefs::Get(&context);
  PrefsHelper rules(*prefs);
  const int checksum = context.GetPath().BaseName().AsUTF8Unsafe() == "workspace-work"
                           ? 101
                           : 202;
  const RulesetID static_id(1);
  const std::set<RulesetID> enabled{static_id};
  if (!restore) {
    rules.SetStaticRulesetChecksum(id, static_id, checksum);
    rules.SetDynamicRulesetChecksum(id, checksum + 1);
    rules.SetEnabledStaticRulesets(id, enabled);
    rules.SetUseActionCountAsBadgeText(id, true);
    return true;
  }
  int observed_static = 0;
  int observed_dynamic = 0;
  std::string unexpected_path;
  return rules.GetStaticRulesetChecksum(id, static_id, observed_static) &&
         observed_static == checksum &&
         rules.GetDynamicRulesetChecksum(id, observed_dynamic) &&
         observed_dynamic == checksum + 1 &&
         rules.GetEnabledStaticRulesets(id) == enabled &&
         rules.GetUseActionCountAsBadgeText(id) &&
         !prefs->ReadPrefAsString(id, "path", &unexpected_path) &&
         !rules.GetDynamicRulesetChecksum(std::string(32, 'p'), observed_dynamic);
}

extensions::WebRequestAPI* WorkspaceExtensionRequestProxy(
    content::BrowserContext& context) {
  if (!CanRegisterExtensionWorkerFactory(context))
    return nullptr;
  return extensions::BrowserContextKeyedAPIFactory<
      extensions::WebRequestAPI>::GetIfExists(&context);
}

bool WorkspaceExtensionHasRulesForTesting(content::BrowserContext& context,
                                           const std::string& id) {
  auto* service = extensions::declarative_net_request::RulesMonitorService::Get(&context);
  return service && service->ruleset_manager()->GetMatcherForExtension(id);
}

bool WorkspaceExtensionHasOffscreenForTesting(content::BrowserContext& context,
                                               const std::string& id) {
  const auto* fixture = GetClient()->fixture(context, id);
  auto* manager = extensions::OffscreenDocumentManager::Get(&context);
  return fixture && manager &&
         manager->GetOffscreenDocumentForExtension(*fixture->extension);
}

void AttachExtensionWebContents(content::WebContents& contents) {
  if (auto* client = GetClient())
    client->CreateExtensionWebContentsObserver(&contents);
}

bool ConfigureBundledNightShift(const base::FilePath& directory) {
  auto* client = GetClient();
  return client && client->ConfigureNightShift(directory);
}

const extensions::Extension* GetBundledNightShiftExtension(
    BrowserContext& context) {
  auto* client = GetClient();
  const auto* extension = client ? client->EnabledFixture(context, kNightShiftId)
                                 : nullptr;
  return extension && extension->location() ==
                          extensions::mojom::ManifestLocation::kComponent
             ? extension
             : nullptr;
}

GURL BundledNightShiftSettingsPage(BrowserContext& context) {
  const auto* extension = GetBundledNightShiftExtension(context);
  return extension ? extensions::OptionsPageInfo::GetOptionsPage(extension)
                   : GURL();
}

void ApplyBundledNightShift(BrowserContext& context,
                            bool enabled,
                            const prototype::NightShiftConfig& config) {
  const auto* extension = GetBundledNightShiftExtension(context);
  if (!extension)
    return;
  auto defaults = base::DictValue()
                      .Set("brightness", config.brightness)
                      .Set("contrast", config.contrast)
                      .Set("sepia", config.sepia)
                      .Set("grayscale", config.grayscale)
                      .Set("force", config.force);
  extensions::StorageFrontend::Get(&context)->Set(
      base::WrapRefCounted(extension), extensions::StorageAreaNamespace::kLocal,
      base::DictValue().Set("enabled", enabled).Set("defaults", std::move(defaults)),
      base::BindOnce([](extensions::StorageFrontend::ResultStatus result) {
        if (!result.success)
          LOG(ERROR) << "Could not save bundled Night Shift profile settings";
      }));
}

void RegisterExtensionContext(BrowserContext& context, BrowserContext* private_source) {
  if (auto* client = GetClient())
    client->Register(context, private_source);
}

scoped_refptr<PrefStore> GetWorkspaceExtensionPreferenceStore(BrowserContext& context) {
  if (!GetClient() || context.IsOffTheRecord())
    return nullptr;
  auto store = base::MakeRefCounted<ExtensionPrefStore>(
      ExtensionPrefValueMapFactory::GetForBrowserContext(&context), false);
  extensions::ExtensionPrefs::Get(&context);
  return store;
}

void DispatchExtensionPasswordSavingChange(BrowserContext& context) {
  if (!GetClient() || context.IsOffTheRecord() || context.ShutdownStarted())
    return;
  auto& profile = static_cast<ProfileContext&>(context);
  const auto value = profile.GetPasswordSavingSetting();
  auto* router = extensions::EventRouter::Get(&context);
  auto* registry = extensions::ExtensionRegistry::Get(&context);
  if (!value || !router || !registry)
    return;
  for (const auto& extension : registry->enabled_extensions()) {
    if (!extension->permissions_data()->HasAPIPermission(
            extensions::mojom::APIPermissionID::kPrivacy))
      continue;
    router->DispatchEventToExtension(extension->id(), std::make_unique<extensions::Event>(
        extensions::events::TYPES_CHROME_SETTING_ON_CHANGE,
        "types.ChromeSetting.passwordSavingEnabled.onChange",
        base::ListValue().Append(ReadExtensionPasswordSaving(profile, extension->id(), *value)),
        &context));
  }
}

void UnregisterExtensionContext(BrowserContext& context) {
  if (auto* client = GetClient())
    client->Unregister(context);
}

namespace {

bool VerifyExtensionRegistryProbe(BrowserContext& owner,
                                  BrowserContext& other,
                                  BrowserContext& private_context,
                                  bool paired) {
  auto* client = GetClient();
  const auto* entry = client ? client->fixture(owner) : nullptr;
  if (!entry)
    return false;
  const auto& fixture = entry->extension;
  const auto& id = fixture->id();
  auto* owner_registry = extensions::ExtensionRegistry::Get(&owner);
  auto* other_registry = extensions::ExtensionRegistry::Get(&other);
  if (!owner_registry || !other_registry ||
      owner_registry == other_registry ||
      extensions::ExtensionRegistry::Get(&private_context) ||
      owner_registry->GetInstalledExtension(id) ||
      !owner_registry->AddDisabled(fixture)) {
    LOG(ERROR) << "Extension registry probe initialization failed: owner="
               << bool(owner_registry) << " other=" << bool(other_registry)
               << " private=" << bool(extensions::ExtensionRegistry::Get(&private_context))
               << " installed=" << bool(owner_registry && owner_registry->GetInstalledExtension(id));
    return false;
  }
  const auto* other_extension = other_registry->GetInstalledExtension(id);
  const auto* other_entry = paired ? client->fixture(other) : nullptr;
  const auto* registered = owner_registry->GetExtensionById(
      id, extensions::ExtensionRegistry::DISABLED);
  const bool isolated = registered == fixture.get() &&
                        !owner_registry->enabled_extensions().Contains(id) &&
                        !owner_registry->ready_extensions().Contains(id) &&
                        (!other_extension ||
                         (other_entry && other_extension == other_entry->extension.get()));
  const bool removed = owner_registry->RemoveDisabled(id);
  return isolated && removed && !owner_registry->GetInstalledExtension(id);
}

class ExtensionProbeThrottle final : public content::NavigationThrottle {
 public:
  explicit ExtensionProbeThrottle(content::NavigationThrottleRegistry& registry)
      : NavigationThrottle(registry) {}

  const char* GetNameForLogging() override { return "ExtensionProbeThrottle"; }

  ThrottleCheckResult WillStartRequest() override {
    auto* navigation = navigation_handle();
    const auto& url = navigation->GetURL();
    auto* contents = navigation->GetWebContents();
    if (url.SchemeIs(extensions::kExtensionScheme) &&
        !navigation->IsInMainFrame()) {
      const auto* extension = FindFrameExtension(url);
      if (!extension)
        return BLOCK_REQUEST;
      const auto& parent = navigation->GetParentFrame()->GetLastCommittedOrigin();
      const bool sandboxed =
          extensions::SandboxedPageInfo::IsSandboxedPage(extension, url.GetPath());
      if (parent == extension->origin()) {
        if (!sandboxed)
          return BLOCK_REQUEST;
      } else {
        // Chrome Web Store packages may declare public frames on the stable
        // extension host. Chromium still checks the resource's URL mode and
        // the embedding origin against the manifest.
        if ((!sandboxed && !extension->from_webstore() &&
             url.host() != extension->guid()) ||
            !IsWebAccessibleExtensionResource(*extension, url, parent, GURL())) {
          return BLOCK_REQUEST;
        }
      }
      if (url.host() == extension->guid())
        dynamic_frame_url_ = url;
      return PROCEED;
    }
    if (const auto* destination = static_cast<ExtensionViewDestination*>(
            contents->GetUserData(&kNightShiftSettingsDestination))) {
      return navigation->IsInPrimaryMainFrame() && !navigation->IsRendererInitiated() &&
                     !navigation->GetInitiatorOrigin() && destination->url == url &&
                     BundledNightShiftSettingsPage(*contents->GetBrowserContext()).is_valid()
                 ? PROCEED : BLOCK_REQUEST;
    }
    if (extensions::GetViewType(contents) == extensions::mojom::ViewType::kExtensionSidePanel &&
        navigation->IsInPrimaryMainFrame()) {
      const auto* destination = static_cast<ExtensionViewDestination*>(
          contents->GetUserData(&kSidePanelDestination));
      return destination && destination->url == url &&
                     !navigation->IsRendererInitiated() && !navigation->GetInitiatorOrigin() &&
                     GetClient()->EnabledFixture(*contents->GetBrowserContext(), url.GetHost())
                 ? PROCEED : BLOCK_REQUEST;
    }
    if (extensions::GetViewType(contents) == extensions::mojom::ViewType::kExtensionPopup) {
      // action.setPopup can point the popup at any page of the extension.
      const auto* fixture = GetClient()->EnabledFixture(*contents->GetBrowserContext(), url.GetHost());
      const auto* action = fixture ? extensions::ActionInfo::GetExtensionActionInfo(fixture) : nullptr;
      return navigation->IsInPrimaryMainFrame() && !navigation->IsRendererInitiated() &&
                     !navigation->GetInitiatorOrigin() && action &&
                     url::Origin::Create(url) == fixture->origin()
                 ? PROCEED
                 : BLOCK_REQUEST;
    }
    if (extensions::GetViewType(contents) ==
            extensions::mojom::ViewType::kOffscreenDocument &&
        navigation->IsInPrimaryMainFrame()) {
      auto& context = *contents->GetBrowserContext();
      const auto* extension =
          GetClient()->EnabledFixture(context, url.GetHost());
      auto* manager = extensions::OffscreenDocumentManager::Get(&context);
      const auto* host = extension && manager
                             ? manager->GetOffscreenDocumentForExtension(*extension)
                             : nullptr;
      return host && host->host_contents() == contents && host->initial_url() == url &&
                     !navigation->IsRendererInitiated() && !navigation->GetInitiatorOrigin()
                 ? PROCEED
                 : BLOCK_REQUEST;
    }
    if (!url.SchemeIs(extensions::kExtensionScheme))
      return PROCEED;
    auto& context = *contents->GetBrowserContext();
    const auto* fixture = GetClient()->EnabledFixture(context, url.GetHost());
    const auto* tab = WorkspaceExtensionTab::FromWebContents(contents);
    if (!navigation->IsInPrimaryMainFrame() ||
        navigation->IsRendererInitiated() || navigation->GetInitiatorOrigin() ||
        !fixture ||
        (url.GetWithoutRef() != fixture->GetResourceURL("probe.html") &&
         url != extensions::OptionsPageInfo::GetOptionsPage(fixture) &&
         !(tab && tab->AllowsExtensionPage(url)))) {
      return BLOCK_REQUEST;
    }
    return PROCEED;
  }

  ThrottleCheckResult WillRedirectRequest() override {
    if (navigation_handle()->GetWebContents()->GetUserData(&kNightShiftSettingsDestination))
      return BLOCK_REQUEST;
    if (auto original = std::exchange(dynamic_frame_url_, std::nullopt)) {
      const auto& url = navigation_handle()->GetURL();
      const auto* extension = FindFrameExtension(url);
      if (!extension || url.host() != extension->id())
        return BLOCK_REQUEST;
      GURL::Replacements canonical;
      canonical.SetHostStr(extension->id());
      if (original->host() != extension->guid() ||
          original->ReplaceComponents(canonical) != url)
        return BLOCK_REQUEST;
      const auto& parent = navigation_handle()->GetParentFrame()->GetLastCommittedOrigin();
      if ((parent == extension->origin() &&
           extensions::SandboxedPageInfo::IsSandboxedPage(extension, url.GetPath())) ||
          IsWebAccessibleExtensionResource(*extension, url, parent, *original)) {
        return PROCEED;
      }
      return BLOCK_REQUEST;
    }
    const auto view = extensions::GetViewType(navigation_handle()->GetWebContents());
    return view == extensions::mojom::ViewType::kExtensionPopup ||
                   (view == extensions::mojom::ViewType::kExtensionSidePanel &&
                    navigation_handle()->IsInPrimaryMainFrame()) ||
                   (view == extensions::mojom::ViewType::kOffscreenDocument &&
                    navigation_handle()->IsInPrimaryMainFrame()) ||
                   navigation_handle()->GetURL().SchemeIs(extensions::kExtensionScheme)
               ? BLOCK_REQUEST
               : PROCEED;
  }

 private:
  const extensions::Extension* FindFrameExtension(const GURL& url) {
    auto* navigation = navigation_handle();
    auto& context = *navigation->GetWebContents()->GetBrowserContext();
    auto* parent = navigation->GetParentFrame();
    if (!url.SchemeIs(extensions::kExtensionScheme) || !parent ||
        !CanLoadExtensionProbeResources(context) ||
        navigation->GetInitiatorOrigin() != parent->GetLastCommittedOrigin()) {
      return nullptr;
    }
    const auto* extension = extensions::ExtensionRegistry::Get(&context)
                                ->enabled_extensions().GetByIDorGUID(url.GetHost());
    return extension && GetClient()->EnabledFixture(context, extension->id()) == extension
               ? extension : nullptr;
  }

  std::optional<GURL> dynamic_frame_url_;
};

}  // namespace

void DispatchWorkspaceSidePanelEvent(content::BrowserContext& profile,
                                     const std::string& id, int tab_id,
                                     const GURL& url, bool opened) {
  if (profile.ShutdownStarted())
    return;
  auto* router = extensions::EventRouter::Get(&profile);
  if (!router)
    return;
  base::DictValue details;
  details.Set("windowId", kWorkspaceExtensionWindowId);
  details.Set("tabId", tab_id);
  details.Set("path", url.path().substr(1));
  router->DispatchEventToExtension(id, std::make_unique<extensions::Event>(
      opened ? extensions::events::SIDE_PANEL_ON_OPENED : extensions::events::SIDE_PANEL_ON_CLOSED,
      opened ? "sidePanel.onOpened" : "sidePanel.onClosed",
      base::ListValue().Append(std::move(details)), &profile));
}

bool ToggleWorkspaceSidePanelOnAction(content::WebContents& contents, const std::string& id) {
  auto* tab = WorkspaceExtensionTab::FromWebContents(&contents);
  auto* context = contents.GetBrowserContext();
  auto* client = GetClient();
  const auto* extension = client ? client->EnabledFixture(*context, id) : nullptr;
  if (!tab || !extension || context->IsOffTheRecord() ||
      !extension->permissions_data()->HasAPIPermission(extensions::mojom::APIPermissionID::kSidePanel)) {
    return false;
  }
  const auto* prefs = extensions::ExtensionPrefs::Get(context)
                          ->ReadPrefAsDictionary(id, kWorkspaceSidePanelPref);
  if (!prefs || !prefs->FindBool("openPanelOnActionClick").value_or(false))
    return false;
  const auto options = ReadSidePanelOptions(context, *extension, tab);
  const auto* path = options.FindString("path");
  return path && options.FindBool("enabled").value_or(false) &&
         ExtensionArchiveResourcePath(*path) &&
         tab->workspace().SetExtensionSidePanel(*tab, id, extension->GetResourceURL(*path), true);
}

void SetNightShiftSettingsDestination(content::WebContents& contents, const GURL& url) {
  const GURL options = BundledNightShiftSettingsPage(*contents.GetBrowserContext());
  GURL::Replacements strip_parameters;
  strip_parameters.ClearQuery();
  strip_parameters.ClearRef();
  CHECK(options.is_valid() && url.ReplaceComponents(strip_parameters) == options);
  contents.SetUserData(&kNightShiftSettingsDestination,
                       std::make_unique<ExtensionViewDestination>(url));
}

void SetWorkspaceSidePanelDestination(content::WebContents& contents, const GURL& url) {
  CHECK(url.SchemeIs(extensions::kExtensionScheme));
  contents.SetUserData(&kSidePanelDestination, std::make_unique<ExtensionViewDestination>(url));
  extensions::SetViewType(&contents, extensions::mojom::ViewType::kExtensionSidePanel);
}

void AddExtensionProbeNavigationThrottle(
    content::NavigationThrottleRegistry& registry) {
  if (GetClient())
    registry.AddThrottle(std::make_unique<ExtensionProbeThrottle>(registry));
}

bool CanLoadExtensionProbeResources(BrowserContext& context,
                                    const std::string& id) {
  auto* client = GetClient();
  if (!client)
    return false;
  if (!id.empty())
    return client->EnabledFixture(context, id);
  if (client->EnabledFixture(context, kNightShiftId))
    return true;
  return std::ranges::any_of(client->ConfiguredExtensions(context),
                             [&](const auto& extension) {
                               return client->EnabledFixture(context, extension->id());
                             });
}

void ConfigureWorkspaceExtensionIdleProbeForTesting(content::BrowserContext& context) {
  class AlternatingIdleTimeProvider final : public extensions::IdleManager::IdleTimeProvider {
   public:
    ui::IdleState CalculateIdleState(int threshold) override {
      return idle_seconds_ >= threshold ? ui::IDLE_STATE_IDLE : ui::IDLE_STATE_ACTIVE;
    }
    int CalculateIdleTime() override {
      idle_seconds_ = idle_seconds_ == 0 ? 90 : 0;
      return idle_seconds_;
    }
    bool CheckIdleStateIsLocked() override { return false; }

   private:
    int idle_seconds_ = 0;
  };

  extensions::IdleManagerFactory::GetForBrowserContext(&context)
      ->SetIdleTimeProviderForTest(std::make_unique<AlternatingIdleTimeProvider>());
}

bool HasWorkspaceExtensionClipboardPermission(content::RenderFrameHost* frame,
                                              ExtensionClipboardAccess access) {
  auto* client = GetClient();
  if (!client || !frame || !frame->IsActive() || !frame->IsInPrimaryMainFrame())
    return false;
  auto& context = *frame->GetBrowserContext();
  const auto& origin = frame->GetLastCommittedOrigin();
  if (origin.scheme() != extensions::kExtensionScheme)
    return false;
  const auto* extension = client->EnabledFixture(context, origin.host());
  auto* process_map = extensions::ProcessMap::Get(&context);
  const auto permission = access == ExtensionClipboardAccess::kRead
                              ? extensions::mojom::APIPermissionID::kClipboardRead
                              : extensions::mojom::APIPermissionID::kClipboardWrite;
  const auto policy_feature = access == ExtensionClipboardAccess::kRead
      ? network::mojom::PermissionsPolicyFeature::kClipboardRead
      : network::mojom::PermissionsPolicyFeature::kClipboardWrite;
  return extension && process_map &&
         process_map->Contains(extension->id(), frame->GetProcess()->GetID()) &&
         extension->permissions_data()->HasAPIPermission(permission) &&
         frame->IsFeatureEnabled(policy_feature);
}

bool IsWorkspaceOffscreenFrame(content::RenderFrameHost* frame) {
  return frame && extensions::GetViewType(content::WebContents::FromRenderFrameHost(frame)) ==
                      extensions::mojom::ViewType::kOffscreenDocument;
}

bool HasWorkspaceOffscreenClipboardPermission(content::RenderFrameHost* frame,
                                              ExtensionClipboardAccess access) {
  if (!IsWorkspaceOffscreenFrame(frame) ||
      !HasWorkspaceExtensionClipboardPermission(frame, access)) {
    return false;
  }
  auto& context = *frame->GetBrowserContext();
  const auto* extension = GetClient()->EnabledFixture(
      context, frame->GetLastCommittedOrigin().host());
  auto* manager = extensions::OffscreenDocumentManager::Get(&context);
  const auto* host = manager && extension
      ? manager->GetOffscreenDocumentForExtension(*extension)
      : nullptr;
  return host && host->host_contents() == content::WebContents::FromRenderFrameHost(frame) &&
         manager->AllowsClipboard(*extension, *host);
}

bool CanRegisterExtensionWorkerFactory(BrowserContext& context) {
  auto* client = GetClient();
  return client && client->HasFixturePreferences(context);
}

bool IsExtensionProbeOrigin(BrowserContext& context, const GURL& site) {
  auto* client = GetClient();
  return client && site.SchemeIs(extensions::kExtensionScheme) &&
         client->HasIsolatedStorage(site.GetHost(), &context);
}

bool CanUseExtensionProbeWorker(BrowserContext& context,
                               const GURL& scope,
                               const blink::StorageKey& storage_key,
                               const GURL& script_url) {
  auto* client = GetClient();
  const auto* fixture =
      client ? client->EnabledFixture(context, scope.GetHost()) : nullptr;
  if (!fixture || (context.IsOffTheRecord() &&
                   !extensions::IncognitoInfo::IsSplitMode(fixture)))
    return false;
  if (storage_key != blink::StorageKey::CreateFirstParty(fixture->origin()))
    return false;
  if (script_url.is_empty())
    return url::Origin::Create(scope) == fixture->origin();
  return scope == fixture->url() &&
         script_url ==
             extensions::BackgroundInfo::GetBackgroundServiceWorkerScriptURL(
                 fixture);
}

void GrantExtensionFrameNavigationRequests(
    content::RenderProcessHost& process, const extensions::Extension& extension) {
  if (!extensions::WebAccessibleResourcesInfo::HasWebAccessibleResources(&extension) ||
      (!extension.from_webstore() &&
       extensions::SandboxedPageInfo::GetPages(&extension).is_empty())) {
    return;
  }
  // Let the renderer submit the URL to the browser throttle. This grant
  // does not authorize a commit or access to any extension resource.
  auto* policy = content::ChildProcessSecurityPolicy::GetInstance();
  policy->GrantRequestOrigin(process.GetDeprecatedID(), extension.origin());
  GURL::Replacements dynamic_host;
  dynamic_host.SetHostStr(extension.guid());
  policy->GrantRequestOrigin(
      process.GetDeprecatedID(),
      url::Origin::Create(extension.url().ReplaceComponents(dynamic_host)));
}

void RegisterExtensionProbeProcess(content::SiteInstance& site) {
  const auto& principal = site.GetSecurityPrincipal();
  auto* client = GetClient();
  auto& context = *site.GetBrowserContext();
  if (client && !principal.IsGuest() &&
      (principal.SchemeIs("http") || principal.SchemeIs("https"))) {
    for (const auto& extension : client->ConfiguredExtensions(context)) {
      if (client->EnabledFixture(context, extension->id())) {
        GrantExtensionFrameNavigationRequests(*site.GetProcess(), *extension);
      }
    }
  }
  if (!principal.SchemeIs(extensions::kExtensionScheme) || principal.IsGuest() ||
      principal.IsSandboxed() || !site.RequiresDedicatedProcess()) {
    return;
  }
  const auto* fixture =
      client ? client->EnabledFixture(*site.GetBrowserContext(),
                                      std::string(principal.GetHost()))
             : nullptr;
  if (fixture)
    client->RegisterProcess(*site.GetProcess(), *fixture);
}

bool CanLoadExtensionProbeSubresources(content::RenderFrameHost& frame,
                                       const url::Origin& initiator) {
  auto* context = frame.GetBrowserContext();
  auto* client = GetClient();
  const auto* fixture =
      client ? client->EnabledFixture(*context, initiator.host()) : nullptr;
  return frame.GetSiteInstance()->RequiresDedicatedProcess() &&
         fixture && initiator == fixture->origin() &&
         extensions::ProcessMap::Get(context)->Contains(
             fixture->id(), frame.GetProcess()->GetID());
}

bool CanLoadWebAccessibleExtensionResource(content::BrowserContext& context,
                                           const GURL& url,
                                           const url::Origin& initiator) {
  auto* client = GetClient();
  if (!client || !CanLoadExtensionProbeResources(context) ||
      !url.SchemeIs(extensions::kExtensionScheme)) {
    return false;
  }
  const auto* extension = extensions::ExtensionRegistry::Get(&context)
                              ->enabled_extensions().GetByIDorGUID(url.GetHost());
  return extension && client->EnabledFixture(context, extension->id()) == extension &&
         IsWebAccessibleExtensionResource(*extension, url, initiator, GURL());
}

bool CanLoadSandboxedExtensionSubresource(content::RenderFrameHost& frame,
                                          const GURL& resource) {
  auto& context = *frame.GetBrowserContext();
  const auto& page = frame.GetLastCommittedURL();
  const auto& origin = frame.GetLastCommittedOrigin();
  auto* client = GetClient();
  const auto* extension = client
      ? client->EnabledFixture(context, page.GetHost())
      : nullptr;
  return extension && origin.opaque() && page.SchemeIs(extensions::kExtensionScheme) &&
         origin.GetTupleOrPrecursorTupleIfOpaque() ==
             extension->origin().GetTupleOrPrecursorTupleIfOpaque() &&
         resource.SchemeIs(extensions::kExtensionScheme) &&
         resource.GetHost() == extension->id() &&
         extensions::SandboxedPageInfo::IsSandboxedPage(extension, page.GetPath());
}

bool CanLoadExtensionWorkerResources(content::RenderProcessHost& process,
                                     const url::Origin& initiator) {
  auto* context = process.GetBrowserContext();
  auto* client = GetClient();
  const auto* fixture =
      client ? client->EnabledFixture(*context, initiator.host()) : nullptr;
  return fixture && initiator == fixture->origin() &&
         extensions::BackgroundInfo::IsServiceWorkerBased(fixture) &&
         extensions::ProcessMap::Get(context)->Contains(fixture->id(),
                                                        process.GetID());
}

namespace {

content::ServiceWorkerContext& ExtensionWorkerContext(
    BrowserContext& context, const extensions::Extension& extension) {
  return *extensions::util::GetServiceWorkerContextForExtensionId(
      extension.id(), &context);
}

void StoreIndexedRulesetPreferences(extensions::ExtensionPrefs& prefs,
                                    const std::string& extension_id,
                                    base::DictValue indexed) {
  using extensions::declarative_net_request::PrefsHelper;
  using extensions::declarative_net_request::RulesetID;
  PrefsHelper rules(prefs);
  std::map<RulesetID, std::vector<int>> disabled_rules;
  for (const auto [key, value] : indexed) {
    int ruleset_id = 0;
    int previous_checksum = 0;
    const auto checksum = value.GetDict().FindInt("checksum");
    if (checksum && base::StringToInt(key, &ruleset_id) &&
        rules.GetStaticRulesetChecksum(extension_id, RulesetID(ruleset_id),
                                       previous_checksum) &&
        previous_checksum == *checksum) {
      const auto ids = rules.GetDisabledStaticRuleIds(extension_id, RulesetID(ruleset_id));
      if (!ids.empty())
        disabled_rules.emplace(RulesetID(ruleset_id), std::vector<int>(ids.begin(), ids.end()));
    }
  }
  prefs.UpdateExtensionPref(extension_id, extensions::ExtensionPrefs::kDNRStaticRulesetPref,
                            base::Value(std::move(indexed)));
  for (auto& [ruleset, ids] : disabled_rules) {
    rules.UpdateDisabledStaticRules(
        extension_id, ruleset, PrefsHelper::RuleIdsToUpdate(std::move(ids), std::nullopt));
  }
}

}  // namespace

static void ActivateFixture(BrowserContext& context,
                            const extensions::Extension& extension) {
  // Both live installs and restored packages reach this point before the
  // renderer and network service receive the extension's permissions.
  extensions::URLPatternSet blocked_hosts;
  for (const auto& store : {extension_urls::GetWebstoreLaunchURL(),
                            extension_urls::GetNewWebstoreLaunchURL()}) {
    blocked_hosts.AddPattern(URLPattern(
        URLPattern::SCHEME_HTTP | URLPattern::SCHEME_HTTPS,
        "*://*." + store.GetHost() + "/*"));
  }
  extension.permissions_data()->SetPolicyHostRestrictions(blocked_hosts, {});
  auto* map = ExtensionPrefValueMapFactory::GetForBrowserContext(&context);
  map->RegisterExtension(extension.id(), base::Time(), /*is_enabled=*/false,
                         /*is_incognito_enabled=*/false);
  if (extension.permissions_data()->HasAPIPermission(
          extensions::mojom::APIPermissionID::kPrivacy)) {
    if (const auto* privacy = extensions::ExtensionPrefs::Get(&context)->ReadPrefAsDictionary(
            extension.id(), kWorkspacePrivacyPref)) {
      if (const auto value = privacy->FindBool("passwordSavingEnabled")) {
        map->SetExtensionPref(extension.id(), kPasswordSavingPref, kPrivacyScope, base::Value(*value));
      }
    }
  }
  auto* system = extensions::ExtensionSystem::Get(&context);
  if (!system->is_ready())
    system->InitForRegularProfile(true);
  auto* renderer =
      extensions::RendererStartupHelperFactory::GetForBrowserContext(&context);
  extensions::ServiceWorkerTaskQueue::Get(&context)->ActivateExtension(&extension);
  if (auto* private_context = GetClient()->GetOffTheRecordContext(&context);
      private_context && GetClient()->IsExtensionIncognitoEnabled(&extension, &context) &&
      extensions::IncognitoInfo::IsSplitMode(&extension)) {
    extensions::ServiceWorkerTaskQueue::Get(private_context)->ActivateExtension(&extension);
  }
  if (extension.permissions_data()->HasAPIPermission(
          extensions::mojom::APIPermissionID::kWebRequest)) {
    extensions::BrowserContextKeyedAPIFactory<extensions::WebRequestAPI>::Get(&context);
  }
  renderer->OnExtensionLoaded(extension);
  for (auto hosts = content::RenderProcessHost::AllHostsIterator();
       !hosts.IsAtEnd(); hosts.Advance()) {
    auto* process = hosts.GetCurrentValue();
    if (process->GetBrowserContext() == &context &&
        process->IsInitializedAndNotDead() && process->GetChannel()) {
      renderer->InitializeProcess(process);
      GrantExtensionFrameNavigationRequests(*process, extension);
    }
  }
  if (extension.permissions_data()->HasAPIPermission(
          extensions::mojom::APIPermissionID::kAlarms)) {
    extensions::AlarmManager::Get(&context);
  }
  auto* registry = extensions::ExtensionRegistry::Get(&context);
  registry->TriggerOnLoaded(&extension);
  if (registry->enabled_extensions().Contains(extension.id())) {
    // ExtensionHost waits for this before loading the offscreen page.
    CHECK(registry->AddReady(&extension));
    registry->TriggerOnReady(&extension);
  }
}

ManagedExtension::ManagedExtension(ProfileContext& owner,
                                   std::string extension_id,
                                   base::RepeatingClosure changed)
    : owner_(owner),
      extension_id_(std::move(extension_id)),
      changed_(std::move(changed)) {}

ManagedExtension::~ManagedExtension() = default;

std::optional<bool> ManagedExtension::UserScriptsAllowed() const {
  if (!extension_ ||
      !extensions::UserScriptManager::IsUserScriptsAPIPermissionAvailable(*extension_)) {
    return std::nullopt;
  }
  return extensions::ExtensionSystem::Get(&*owner_)
      ->user_script_manager()
      ->AreUserScriptsAllowed(*extension_);
}

bool ManagedExtension::SetUserScriptsAllowed(bool allowed) {
  if (!editable() || !enabled() || !UserScriptsAllowed().has_value())
    return false;
  extensions::ExtensionSystem::Get(&*owner_)
      ->user_script_manager()
      ->SetUserScriptPrefEnabled(extension_id_, allowed);
  changed_.Run();
  return true;
}

std::optional<bool> ManagedExtension::OptionalClipboardWriteAllowed() const {
  constexpr auto permission = extensions::mojom::APIPermissionID::kClipboardWrite;
  using extensions::PermissionsParser;
  if (!extension_ ||
      !PermissionsParser::GetOptionalPermissions(extension_.get()).HasAPIPermission(permission) ||
      PermissionsParser::GetRequiredPermissions(extension_.get()).HasAPIPermission(permission)) {
    return std::nullopt;
  }
  return extension_->permissions_data()->HasAPIPermission(permission);
}

bool ManagedExtension::SetOptionalClipboardWriteAllowed(bool allowed) {
  const auto current = OptionalClipboardWriteAllowed();
  if (!editable() || !enabled() || uninstalling() || !current)
    return false;
  if (*current == allowed)
    return true;

  extensions::APIPermissionSet apis;
  apis.insert(extensions::mojom::APIPermissionID::kClipboardWrite);
  const extensions::PermissionSet clipboard(std::move(apis), {}, {}, {});
  const auto& active = extension_->permissions_data()->active_permissions();
  const auto updated = allowed
      ? extensions::PermissionSet::CreateUnion(active, clipboard)
      : extensions::PermissionSet::CreateDifference(active, clipboard);
  if (!SaveOptionalGrants(*owner_, *extension_, *updated))
    return false;

  extensions::PermissionsUpdater updater(&*owner_);
  if (allowed) {
    updater.GrantOptionalPermissions(*extension_, clipboard, base::DoNothing());
  } else {
    updater.RevokeOptionalPermissions(*extension_, clipboard,
        extensions::PermissionsUpdater::RemoveType::kHard, base::DoNothing());
  }
  return true;
}

std::optional<bool> ManagedExtension::IncognitoAllowed() const {
  auto* client = GetClient();
  if (!extension_ || !extensions::util::CanBeIncognitoEnabled(extension_.get()) ||
      !client->IsManagedPackage(*extension_, &*owner_)) {
    return std::nullopt;
  }
  return extensions::ExtensionPrefs::Get(&*owner_)->IsIncognitoEnabled(extension_id_);
}

bool ManagedExtension::SetIncognitoAllowed(bool allowed) {
  const auto current = IncognitoAllowed();
  if (!editable() || uninstalling() || !current)
    return false;
  if (*current != allowed)
    GetClient()->UpdateIncognitoAccess(*owner_, *extension_, allowed);
  return true;
}

bool ManagedExtension::Start(base::OnceCallback<void(bool)> installed) {
  const auto* fixture = GetClient()->fixture(*owner_, extension_id_);
  if (!fixture)
    return false;
  extension_ = fixture->extension;
  legacy_disabled_ = fixture->legacy_disabled;
  if (fixture->keep_disabled) {
    ready_ = extensions::ExtensionRegistry::Get(&*owner_)->AddDisabled(extension_);
    return ready_;
  }
  pending_choice_ = true;
  installed_ = std::move(installed);
  Enable(installed_ ? Activation::kInstall : Activation::kStartup);
  return !failed_;
}

bool ManagedExtension::OwnsProfile(const ProfileContext& profile) const {
  return &*owner_ == &profile;
}

const std::string& ManagedExtension::name() const {
  return extension_->name();
}

GURL ManagedExtension::options_page() const {
  if (!editable() || !enabled() || uninstalling())
    return {};
  return extensions::OptionsPageInfo::GetOptionsPage(extension_.get());
}

GURL ManagedExtension::uninstall_url() const {
  if (!editable() || uninstalling())
    return {};
  return ReadWorkspaceExtensionUninstallUrl(owner_->GetPath(), *extension_);
}

bool ManagedExtension::has_action() const {
  return editable() && enabled() && !uninstalling() &&
         extensions::ActionInfo::GetExtensionActionInfo(extension_.get());
}

GURL ManagedExtension::action_popup() const {
  if (!editable() || !enabled() || uninstalling())
    return {};
  const auto* action = extensions::ActionInfo::GetExtensionActionInfo(extension_.get());
  return action ? action->default_popup_url : GURL();
}

bool ManagedExtension::enabled() const {
  return extension_ && CanLoadExtensionProbeResources(*owner_, extension_id_);
}

bool ManagedExtension::selection() const {
  return pending_choice_.value_or(enabled());
}

bool ManagedExtension::editable() const {
  return ready_ && !legacy_disabled_;
}

bool ManagedExtension::SetEnabled(bool value) {
  if (!editable() || value == enabled() ||
      !PersistExtensionFixtureEnabled(extension_->path(), value)) {
    return false;
  }
  ready_ = false;
  failed_ = false;
  pending_choice_ = value;
  if (value) {
    Enable(Activation::kEnable);
  } else if (GetClient()->DisableFixture(*owner_, extension_->id())) {
    WaitForWorkerRemoval(base::TimeTicks::Now() + base::Seconds(10));
  } else {
    CompleteTransition(false);
  }
  return !failed_;
}

void ManagedExtension::Uninstall(base::OnceCallback<void(bool)> done) {
  if (!editable() || uninstall_) {
    std::move(done).Run(false);
    return;
  }
  uninstall_ = std::move(done);
  if (!enabled()) {
    ClearUninstalledData();
  } else if (!SetEnabled(false) && uninstall_) {
    CompleteUninstall(false);
  }
}

void ManagedExtension::ClearUninstalledData() {
  if (!PersistExtensionFixtureEnabled(extension_->path(), false)) {
    CompleteUninstall(false);
    return;
  }
  ready_ = false;
  changed_.Run();
  WaitForUninstalledRenderers(base::TimeTicks::Now() + base::Seconds(10));
}

void ManagedExtension::WaitForUninstalledRenderers(base::TimeTicks deadline) {
  auto* partition = extensions::util::GetStoragePartitionForExtensionId(
      extension_id_, &*owner_);
  auto* frontend = extensions::StorageFrontend::Get(&*owner_);
  if (!partition || partition->GetConfig().is_default() || !frontend ||
      partition->GetConfig().partition_domain() !=
          extensions::util::GetPartitionDomainForExtension(extension_.get())) {
    CompleteUninstall(false);
    return;
  }
  bool live_renderer = false;
  // A document can retain open storage connections after its extension
  // loses registry access. Stop only renderers in this isolated partition.
  for (auto hosts = content::RenderProcessHost::AllHostsIterator();
       !hosts.IsAtEnd(); hosts.Advance()) {
    auto* process = hosts.GetCurrentValue();
    if (process->GetBrowserContext() != &*owner_ ||
        process->GetStoragePartition() != partition ||
        !process->IsInitializedAndNotDead()) {
      continue;
    }
    live_renderer = true;
    if (!process->ShutdownRequested())
      process->Shutdown(content::RESULT_CODE_NORMAL_EXIT);
  }
  if (live_renderer) {
    if (base::TimeTicks::Now() >= deadline) {
      CompleteUninstall(false);
      return;
    }
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, base::BindOnce(&ManagedExtension::WaitForUninstalledRenderers,
                                  weak_factory_.GetWeakPtr(), deadline),
        base::Milliseconds(50));
    return;
  }
  auto cleared = base::BarrierClosure(
      2, base::BindOnce(&ManagedExtension::RemoveUninstalledPackage,
                        weak_factory_.GetWeakPtr()));
  partition->ClearData(content::StoragePartition::REMOVE_DATA_MASK_ALL,
                       blink::StorageKey(), base::Time(), base::Time::Max(),
                       cleared);
  frontend->DeleteStorageSoon(extension_id_, cleared);
}

void ManagedExtension::RemoveUninstalledPackage() {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskShutdownBehavior::BLOCK_SHUTDOWN},
      base::BindOnce(&RemoveDisabledExtensionInstallation, owner_->GetPath(),
                     extension_->path()),
      base::BindOnce(&ManagedExtension::CompleteUninstall,
                     weak_factory_.GetWeakPtr()));
}

void ManagedExtension::CompleteUninstall(bool success) {
  if (success)
    GetClient()->UnregisterDisabledFixture(*owner_, extension_id_);
  ready_ = !success;
  auto done = std::move(uninstall_);
  std::move(done).Run(success);
}

void ManagedExtension::Enable(Activation activation) {
  // Chromium can leave tasks queued after worker registration fails.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&ManagedExtension::CompleteTransition,
                                weak_factory_.GetWeakPtr(), false),
      base::Seconds(15));
  GetClient()->SetFixtureContext(*owner_);
  if (extensions::declarative_net_request::HasAnyDNRPermission(*extension_)) {
    using extensions::declarative_net_request::FileBackedRulesetSource;
    using extensions::declarative_net_request::PrefsHelper;
    auto* prefs = extensions::ExtensionPrefs::Get(&*owner_);
    bool recovery_marker = false;
    prefs->ReadPrefAsBoolean(extension_id_, kDnrRecoveryMarkerPref,
                             &recovery_marker);
    int legacy_checksum = 0;
    const bool allow_legacy_dynamic_rules =
        !recovery_marker &&
        PrefsHelper(*prefs).GetDynamicRulesetChecksum(extension_id_, legacy_checksum);
    extensions::GetExtensionFileTaskRunner()->PostTaskAndReplyWithResult(
        FROM_HERE,
        base::BindOnce(
            [](scoped_refptr<const extensions::Extension> extension,
               const FileBackedRulesetSource& dynamic_rules, bool allow_legacy) {
              return PrepareVerifiedExtensionRuleIndexes(*extension, dynamic_rules,
                                                         allow_legacy);
            },
            extension_, FileBackedRulesetSource::CreateDynamic(&*owner_, extension_id_),
            allow_legacy_dynamic_rules),
        base::BindOnce(
            [](base::WeakPtr<ManagedExtension> self, Activation activation,
               bool previously_marked,
               std::optional<WorkspaceExtensionRuleIndexes> result) {
              if (!self || !self->pending_choice_)
                return;
              if (!result) {
                self->CompleteTransition(false);
                return;
              }
              auto* prefs = extensions::ExtensionPrefs::Get(&*self->owner_);
              // A previous install's profile prefs can outlive its package
              // directory if shutdown interrupts uninstall.
              if (previously_marked && !result->had_recovery_marker) {
                for (const auto key : {extensions::ExtensionPrefs::kDNRStaticRulesetPref,
                                       kDnrEnabledRulesetsPref, kDnrKeepAllocationPref,
                                       kDnrAllocatedRulesPref, kDnrBadgePref}) {
                  prefs->UpdateExtensionPref(self->extension_id_, key, std::nullopt);
                }
              }
              StoreIndexedRulesetPreferences(
                  *prefs, self->extension_id_, std::move(result->static_rules));
              prefs->UpdateExtensionPref(self->extension_id_, kDnrRecoveryMarkerPref,
                                         base::Value(true));
              if (result->dynamic_checksum) {
                PrefsHelper(*prefs).SetDynamicRulesetChecksum(
                    self->extension_id_, *result->dynamic_checksum);
              } else {
                prefs->UpdateExtensionPref(self->extension_id_, kDnrDynamicRulesetPref,
                                           std::nullopt);
              }
              extensions::declarative_net_request::RulesMonitorService::Get(&*self->owner_);
              extensions::BrowserContextKeyedAPIFactory<extensions::WebRequestAPI>::Get(
                  &*self->owner_);
              self->PrepareWorker(activation);
            },
            weak_factory_.GetWeakPtr(), activation, recovery_marker));
    return;
  }
  PrepareWorker(activation);
}

void ManagedExtension::PrepareWorker(Activation activation) {
  auto& worker = ExtensionWorkerContext(*owner_, *extension_);
  worker.CheckHasServiceWorker(
      extension_->url(), blink::StorageKey::CreateFirstParty(extension_->origin()),
      base::BindOnce(
          [](base::WeakPtr<ManagedExtension> self, Activation activation,
             content::ServiceWorkerCapability existing) {
            if (!self)
              return;
            if (existing == content::ServiceWorkerCapability::NO_SERVICE_WORKER) {
              self->OnWorkerPrepared(activation, true);
              return;
            }
            const auto& extension = *self->extension_;
            ExtensionWorkerContext(*self->owner_, extension)
                .UnregisterServiceWorkerImmediately(
                    extension.url(),
                    blink::StorageKey::CreateFirstParty(extension.origin()),
                    base::BindOnce(
                        [](base::WeakPtr<ManagedExtension> self, Activation activation,
                           blink::ServiceWorkerStatusCode result) {
                          if (self) {
                            self->OnWorkerPrepared(
                                activation, result == blink::ServiceWorkerStatusCode::kOk ||
                                             result == blink::ServiceWorkerStatusCode::kErrorNotFound);
                          }
                        },
                        self, activation));
          },
          weak_factory_.GetWeakPtr(), activation));
}

void ManagedExtension::OnWorkerPrepared(Activation activation, bool success) {
  if (!success) {
    CompleteTransition(false);
    return;
  }
  auto* registry = extensions::ExtensionRegistry::Get(&*owner_);
  RestoreOptionalGrants(*owner_, *extension_);
  if (!registry->AddEnabled(extension_)) {
    CompleteTransition(false);
    return;
  }
  if (activation == Activation::kEnable)
    CHECK(registry->RemoveDisabled(extension_->id()));
  ActivateFixture(*owner_, *extension_);
  extensions::ServiceWorkerTaskQueue::Get(&*owner_)->AddPendingTask(
      extensions::LazyContextId::ForExtension(&*owner_, extension_.get()),
      base::BindOnce(
          [](base::WeakPtr<ManagedExtension> self, Activation activation,
             std::unique_ptr<extensions::LazyContextTaskQueue::ContextInfo> info) {
            if (!self)
              return;
            const bool ready = info && info->worker_thread_id > 0;
            if (ready) {
              auto* router = extensions::EventRouter::Get(&*self->owner_);
              const auto previous = ReadPendingExtensionUpdate(*self->extension_);
              if (previous || activation == Activation::kInstall) {
                base::DictValue details;
                details.Set("reason", previous ? "update" : "install");
                if (previous)
                  details.Set("previousVersion", *previous);
                base::ListValue args;
                args.Append(std::move(details));
                router->DispatchEventToExtension(
                    self->extension_->id(), std::make_unique<extensions::Event>(
                        extensions::events::RUNTIME_ON_INSTALLED,
                        "runtime.onInstalled", std::move(args)));
                if (previous)
                  ClearPendingExtensionUpdate(*self->extension_);
              }
              if (activation == Activation::kStartup) {
                router->DispatchEventToExtension(
                    self->extension_->id(), std::make_unique<extensions::Event>(
                        extensions::events::RUNTIME_ON_STARTUP,
                        "runtime.onStartup", base::ListValue()));
              }
            }
            self->CompleteTransition(ready);
          },
          weak_factory_.GetWeakPtr(), activation));
}

void ManagedExtension::WaitForWorkerRemoval(base::TimeTicks deadline) {
  ExtensionWorkerContext(*owner_, *extension_).CheckHasServiceWorker(
      extension_->url(), blink::StorageKey::CreateFirstParty(extension_->origin()),
      base::BindOnce(&ManagedExtension::OnWorkerRemovalChecked,
                     weak_factory_.GetWeakPtr(), deadline));
}

void ManagedExtension::OnWorkerRemovalChecked(
    base::TimeTicks deadline, content::ServiceWorkerCapability capability) {
  if (capability == content::ServiceWorkerCapability::NO_SERVICE_WORKER) {
    CompleteTransition(true);
    return;
  }
  if (base::TimeTicks::Now() >= deadline) {
    CompleteTransition(false);
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&ManagedExtension::WaitForWorkerRemoval,
                               weak_factory_.GetWeakPtr(), deadline),
      base::Milliseconds(50));
}

void ManagedExtension::CompleteTransition(bool success) {
  // Deactivation can synchronously finish the pending worker task.
  weak_factory_.InvalidateWeakPtrs();
  const bool activating = pending_choice_ == true;
  pending_choice_.reset();
  ready_ = success;
  failed_ = !success;
  if (!success) {
    LOG(ERROR) << "Could not apply the extension choice in its profile";
    auto* client = GetClient();
    auto* registry = extensions::ExtensionRegistry::Get(&*owner_);
    if (enabled())
      client->DisableFixture(*owner_, extension_->id());
    if (activating && !enabled() &&
        (registry->disabled_extensions().Contains(extension_id_) ||
         registry->AddDisabled(extension_))) {
      // A first install that couldn't activate stays disabled. A restored
      // extension's activation failure must not overwrite the user's choice.
      ready_ = !installed_ || PersistExtensionFixtureEnabled(extension_->path(), false);
    }
  }
  auto installed = std::move(installed_);
  changed_.Run();
  if (installed)
    std::move(installed).Run(success);
  if (uninstall_) {
    if (success)
      ClearUninstalledData();
    else
      CompleteUninstall(false);
  }
}

std::optional<base::UnguessableToken> ManagedExtension::RunningWorkerForTesting() {
  if (!enabled())
    return std::nullopt;
  const auto& workers =
      ExtensionWorkerContext(*owner_, *extension_).GetRunningServiceWorkerInfos();
  if (workers.size() != 1)
    return std::nullopt;
  const auto& info = workers.begin()->second;
  auto* process = content::RenderProcessHost::FromID(info.render_process_id);
  if (!process || process->GetBrowserContext() != &*owner_ ||
      process->GetStoragePartition() != extensions::util::GetStoragePartitionForExtensionId(
                                           extension_->id(), &*owner_, /*can_create=*/false) ||
      !CanUseExtensionProbeWorker(*owner_, info.scope, info.key, info.script_url) ||
      !extensions::ProcessMap::Get(&*owner_)->Contains(extension_->id(),
                                                       info.render_process_id)) {
    return std::nullopt;
  }
  return info.token.value();
}

void ManagedExtension::StopWorkerForTesting(base::OnceClosure stopped) {
  CHECK(enabled());
  ExtensionWorkerContext(*owner_, *extension_).StopAllServiceWorkers(
      std::move(stopped));
}

ExtensionNavigationProbe::ExtensionNavigationProbe(
    ProfileContext& owner, ProfileContext& other,
    const base::FilePath& private_path, content::WebContents& web_page,
    base::OnceClosure peer_barrier,
    base::RepeatingCallback<bool(ProfileContext&, bool)> change_settings,
    base::OnceCallback<void(bool)> done)
    : owner_(owner),
      other_(other),
      private_(std::make_unique<ProfileContext>(private_path,
                                                ProfileContext::Mode::kPrivate)),
      web_page_(web_page.GetWeakPtr()),
      web_renderer_(web_page.GetPrimaryMainFrame()->GetProcess()->GetID()),
      paired_(!peer_barrier.is_null()),
      state_action_(GetExtensionFixtureStateAction().value()),
      peer_barrier_(std::move(peer_barrier)),
      change_settings_(std::move(change_settings)),
      done_(std::move(done)) {}

ExtensionNavigationProbe::~ExtensionNavigationProbe() = default;

void ExtensionNavigationProbe::Start() {
  if (!VerifyExtensionRegistryProbe(*owner_, *other_, *private_, paired_)) {
    Finish(false);
    return;
  }
  const auto& fixture = *GetClient()->fixture(*owner_);
  extension_ = fixture.extension;
  url_ = extension_->GetResourceURL("probe.html");
  auto* registry = extensions::ExtensionRegistry::Get(&*owner_);
  if (fixture.keep_disabled) {
    if (!registry->AddDisabled(extension_)) {
      LOG(ERROR) << "Extension probe could not register its initially disabled fixture";
      Finish(false);
      return;
    }
    step_ = Step::kInitiallyDisabled;
    Navigate(*owner_);
    return;
  }
  EnableExtension();
}

void ExtensionNavigationProbe::EnableExtension() {
  GetClient()->SetFixtureContext(*owner_);
  WorkerContext().CheckHasServiceWorker(
      extension_->url(), blink::StorageKey::CreateFirstParty(extension_->origin()),
      base::BindOnce(&ExtensionNavigationProbe::PrepareWorkerNavigation,
                     weak_factory_.GetWeakPtr()));
}

bool ExtensionNavigationProbe::OwnsProfile(const ProfileContext& profile) const {
  return &profile == &*owner_;
}

std::optional<bool> ExtensionNavigationProbe::UserEnabled() const {
  if (!extension_)
    return std::nullopt;
  if (committed_user_choice_)
    return committed_user_choice_;
  return CanLoadExtensionProbeResources(*owner_, extension_->id());
}

bool ExtensionNavigationProbe::SetUserEnabled(bool enabled) {
  if (!user_choice_ready_ || UserEnabled() == enabled ||
      !PersistExtensionFixtureEnabled(extension_->path(), enabled)) {
    return false;
  }
  committed_user_choice_ = enabled;
  user_choice_ready_ = false;
  if (enabled) {
    std::puts("Zitter Content persisted extension enablement");
    EnableExtension();
  } else {
    std::puts("Zitter Content persisted extension disablement");
    if (state_action_ != ExtensionFixtureStateAction::kPauseAfterDisableCommit)
      DisableExtension();
  }
  return true;
}

void ExtensionNavigationProbe::PrepareWorkerNavigation(
    content::ServiceWorkerCapability existing) {
  if (existing == content::ServiceWorkerCapability::NO_SERVICE_WORKER) {
    ActivateAndNavigate();
    return;
  }
  // A prior process may have exited before unregistering the fixture worker.
  // Remove the stale registration before loading an authenticated version.
  WorkerContext().UnregisterServiceWorkerImmediately(
      extension_->url(), blink::StorageKey::CreateFirstParty(extension_->origin()),
      base::BindOnce(
          [](base::WeakPtr<ExtensionNavigationProbe> probe,
             blink::ServiceWorkerStatusCode result) {
            if (!probe)
              return;
            if (result != blink::ServiceWorkerStatusCode::kOk &&
                result != blink::ServiceWorkerStatusCode::kErrorNotFound) {
              LOG(ERROR) << "Could not unregister the previous extension worker";
              probe->Finish(false);
              return;
            }
            if (result == blink::ServiceWorkerStatusCode::kOk)
              std::puts("Zitter Content removed a stale extension worker registration");
            probe->ActivateAndNavigate();
          },
          weak_factory_.GetWeakPtr()));
}

void ExtensionNavigationProbe::ActivateAndNavigate() {
  auto* registry = extensions::ExtensionRegistry::Get(&*owner_);
  if (!registry->AddEnabled(extension_)) {
    Finish(false);
    return;
  }
  if (committed_user_choice_ == true)
    CHECK(registry->RemoveDisabled(extension_->id()));
  step_ = Step::kOwner;
  // Publish renderer metadata before opening a page can warm a spare renderer.
  ActivateFixture(*owner_, *extension_);
  std::puts("Zitter Content extension registry profile isolation passed");
  Navigate(*owner_);
}

void ExtensionNavigationProbe::Navigate(BrowserContext& context) {
  Observe(nullptr);
  page_ = content::WebContents::Create(content::WebContents::CreateParams(&context));
  Observe(page_.get());
  page_->GetController().LoadURLWithParams(
      content::NavigationController::LoadURLParams(url_));
}

void ExtensionNavigationProbe::DidFinishNavigation(
    content::NavigationHandle* navigation) {
  if (!navigation->IsInPrimaryMainFrame())
    return;
  if (script_check_ != ScriptCheck::kNone) {
    if (navigation->IsErrorPage() || navigation->GetURL() != script_check_url_) {
      LOG(ERROR) << "Extension content script test could not load the fixture";
      Finish(false);
    }
    return;
  }
  if (navigation->GetURL() != url_)
    return;
  if (step_ == Step::kOwner || step_ == Step::kPeerDisabled) {
    if (navigation->IsErrorPage() || !navigation->HasCommitted()) {
      LOG(ERROR) << "Extension navigation failed: " << navigation->GetNetErrorCode();
      Finish(false);
    }
    return;
  }
  if (!navigation->IsErrorPage() ||
      navigation->GetNetErrorCode() != net::ERR_BLOCKED_BY_CLIENT) {
    LOG(ERROR) << "Extension navigation was not blocked for step "
               << static_cast<int>(step_) << ": " << navigation->GetNetErrorCode();
    Finish(false);
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&ExtensionNavigationProbe::Advance,
                               weak_factory_.GetWeakPtr()));
}

void ExtensionNavigationProbe::DocumentOnLoadCompletedInPrimaryMainFrame() {
  if (script_check_ != ScriptCheck::kNone) {
    ReadContentScriptResult(base::TimeTicks::Now() + base::Seconds(5));
    return;
  }
  if ((step_ != Step::kOwner && step_ != Step::kPeerDisabled) ||
      page_->GetLastCommittedURL() != url_)
    return;
  auto* frame = page_->GetPrimaryMainFrame();
  if (page_->GetTitle() != u"Zitter extension script executed" ||
      frame->GetLastCommittedOrigin() != url::Origin::Create(url_) ||
      !frame->GetSiteInstance()->RequiresDedicatedProcess() ||
      frame->GetStoragePartition() == owner_->GetDefaultStoragePartition() ||
      frame->GetStoragePartition() !=
          extensions::util::GetStoragePartitionForExtensionId(
              extension_->id(), &*owner_, /*can_create=*/false) ||
      frame->GetStoragePartition()->GetConfig().partition_domain() !=
          extension_->id() ||
      !owner_->GetPath().IsParent(frame->GetStoragePartition()->GetPath()) ||
      frame->GetProcess()->GetID() == web_renderer_) {
    LOG(ERROR) << "Extension page did not use its own origin and process";
    Finish(false);
    return;
  }
  if (step_ == Step::kPeerDisabled)
    CheckWorkerReply(base::TimeTicks::Now() + base::Seconds(10));
  else
    StartForeignScriptCheck();
}

void ExtensionNavigationProbe::StartForeignScriptCheck() {
  if (!web_page_ ||
      extensions::ProcessMap::Get(&*owner_)->Contains(web_renderer_)) {
    Finish(false);
    return;
  }
  const auto src = base::WriteJson(
      extension_->GetResourceURL("probe.js").spec());
  CHECK(src);
  web_page_->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(
          "(() => {const root = document.documentElement;"
          "root.dataset.zitterProbe = 'pending';"
          "const script = document.createElement('script');"
          "script.onload = () => root.dataset.zitterProbe = 'loaded';"
          "script.onerror = () => root.dataset.zitterProbe = 'blocked';"
          "script.src = " + *src + "; document.head.appendChild(script);})()"),
      base::BindOnce(
          [](base::WeakPtr<ExtensionNavigationProbe> probe, base::Value) {
            if (probe)
              probe->CheckForeignScript(base::TimeTicks::Now() + base::Seconds(5));
          },
          weak_factory_.GetWeakPtr()),
      content::ISOLATED_WORLD_ID_CONTENT_END);
}

void ExtensionNavigationProbe::CheckForeignScript(base::TimeTicks deadline) {
  if (!web_page_ || base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "Extension subresource probe did not receive a result";
    Finish(false);
    return;
  }
  web_page_->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
      u"document.documentElement.dataset.zitterProbe",
      base::BindOnce(&ExtensionNavigationProbe::OnForeignScriptResult,
                     weak_factory_.GetWeakPtr(), deadline),
      content::ISOLATED_WORLD_ID_CONTENT_END);
}

void ExtensionNavigationProbe::OnForeignScriptResult(base::TimeTicks deadline,
                                                      base::Value result) {
  if (result == base::Value("blocked")) {
    std::puts("Zitter Content extension subresource isolation passed");
    CheckWorkerRegistration();
  } else if (result == base::Value("pending")) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, base::BindOnce(&ExtensionNavigationProbe::CheckForeignScript,
                                 weak_factory_.GetWeakPtr(), deadline),
        base::Milliseconds(50));
  } else {
    LOG(ERROR) << "An ordinary page in the extension profile could load an extension script";
    Finish(false);
  }
}

content::ServiceWorkerContext& ExtensionNavigationProbe::WorkerContext() {
  return *extensions::util::GetServiceWorkerContextForExtensionId(
      extension_->id(), &*owner_);
}

void ExtensionNavigationProbe::CheckWorkerRegistration() {
  const auto& fixture = *extension_;
  const auto key = blink::StorageKey::CreateFirstParty(fixture.origin());
  const auto script =
      extensions::BackgroundInfo::GetBackgroundServiceWorkerScriptURL(&fixture);
  if (!CanUseExtensionProbeWorker(*owner_, fixture.url(), key, script) ||
      CanUseExtensionProbeWorker(*owner_, fixture.url(), key, url_) ||
      CanUseExtensionProbeWorker(*owner_, url_, key, script) ||
      (!paired_ && CanUseExtensionProbeWorker(*other_, fixture.url(), key, script)) ||
      CanUseExtensionProbeWorker(*private_, fixture.url(), key, script)) {
    LOG(ERROR) << "Extension probe worker permission scope is incorrect";
    Finish(false);
    return;
  }
  auto* queue = extensions::ServiceWorkerTaskQueue::Get(&*owner_);
  queue->AddPendingTask(
      extensions::LazyContextId::ForExtension(&*owner_, &fixture),
      base::BindOnce(
          [](base::WeakPtr<ExtensionNavigationProbe> probe,
             std::unique_ptr<extensions::LazyContextTaskQueue::ContextInfo> info) {
            if (!probe)
              return;
            if (!info || info->worker_thread_id <= 0) {
              LOG(ERROR) << "Extension queue did not dispatch the initial task";
              probe->Finish(false);
              return;
            }
            probe->CheckWorkerReply(base::TimeTicks::Now() + base::Seconds(10));
          },
          weak_factory_.GetWeakPtr()));
}

void ExtensionNavigationProbe::CheckWorkerReply(base::TimeTicks deadline) {
  if (base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "Extension probe worker did not reply";
    Finish(false);
    return;
  }
  page_->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
      u"document.documentElement.dataset.workerReply || 'pending'",
      base::BindOnce(&ExtensionNavigationProbe::OnWorkerReply,
                     weak_factory_.GetWeakPtr(), deadline),
      content::ISOLATED_WORLD_ID_CONTENT_END);
}

void ExtensionNavigationProbe::OnWorkerReply(base::TimeTicks deadline,
                                             base::Value result) {
  if (result == base::Value("pending")) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, base::BindOnce(&ExtensionNavigationProbe::CheckWorkerReply,
                                 weak_factory_.GetWeakPtr(), deadline),
        base::Milliseconds(50));
    return;
  }
  if (result != base::Value("pong")) {
    LOG(ERROR) << "Unexpected extension probe worker reply: " << result;
    Finish(false);
    return;
  }
  const auto& fixture = *extension_;
  const auto script =
      extensions::BackgroundInfo::GetBackgroundServiceWorkerScriptURL(&fixture);
  const auto& workers = WorkerContext().GetRunningServiceWorkerInfos();
  const bool isolated = std::ranges::any_of(workers, [&](const auto& entry) {
    const auto& info = entry.second;
    auto* process = content::RenderProcessHost::FromID(info.render_process_id);
    return info.script_url == script && info.scope == fixture.url() && process &&
           info.render_process_id != web_renderer_ &&
           process->GetStoragePartition() ==
               page_->GetPrimaryMainFrame()->GetStoragePartition() &&
           extensions::ProcessMap::Get(&*owner_)->Contains(
               fixture.id(), info.render_process_id);
  });
  if (!isolated) {
    LOG(ERROR) << "Extension probe worker did not run in an extension process";
    Finish(false);
    return;
  }
  if (step_ == Step::kPeerDisabled) {
    if (!StartupWorkerIsRunning()) {
      LOG(ERROR) << "Disabling the peer changed the surviving worker";
      Finish(false);
      return;
    }
    step_ = Step::kOwner;
    std::puts("Zitter Content kept the peer extension running after disable");
    ContinueAfterWorkerWakeUp();
    return;
  }
  WorkerContext().StopAllServiceWorkers(
      base::BindOnce(&ExtensionNavigationProbe::QueueWorkerWakeUp,
                     weak_factory_.GetWeakPtr(),
                     base::TimeTicks::Now() + base::Seconds(10)));
}

void ExtensionNavigationProbe::QueueWorkerWakeUp(base::TimeTicks deadline) {
  if (!WorkerContext().GetRunningServiceWorkerInfos().empty()) {
    if (base::TimeTicks::Now() >= deadline) {
      LOG(ERROR) << "Extension worker was still running before queuing a wake-up";
      Finish(false);
      return;
    }
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, base::BindOnce(&ExtensionNavigationProbe::QueueWorkerWakeUp,
                                 weak_factory_.GetWeakPtr(), deadline),
        base::Milliseconds(50));
    return;
  }
  const auto& fixture = *extension_;
  auto* router = extensions::EventRouter::Get(&*owner_);
  constexpr char kStartupEvent[] = "runtime.onStartup";
  if (!router->ExtensionHasEventListener(fixture.id(), kStartupEvent)) {
    LOG(ERROR) << "Stopped extension worker has no lazy startup listener";
    Finish(false);
    return;
  }
  router->DispatchEventToExtension(
      fixture.id(), std::make_unique<extensions::Event>(
                        extensions::events::RUNTIME_ON_STARTUP, kStartupEvent,
                        base::ListValue()));
  CheckWorkerWakeUp(base::TimeTicks::Now() + base::Seconds(10));
}

void ExtensionNavigationProbe::CheckWorkerWakeUp(base::TimeTicks deadline) {
  if (base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "Extension worker did not receive the startup event";
    Finish(false);
    return;
  }
  page_->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
      u"document.documentElement.dataset.workerStartup || 'pending'",
      base::BindOnce(&ExtensionNavigationProbe::OnWorkerWakeUp,
                     weak_factory_.GetWeakPtr(), deadline),
      content::ISOLATED_WORLD_ID_CONTENT_END);
}

void ExtensionNavigationProbe::OnWorkerWakeUp(base::TimeTicks deadline,
                                               base::Value result) {
  if (result == base::Value("pending")) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, base::BindOnce(&ExtensionNavigationProbe::CheckWorkerWakeUp,
                                 weak_factory_.GetWeakPtr(), deadline),
        base::Milliseconds(50));
    return;
  }
  const std::string startup_prefix =
      "startup:" + extension_->version().GetString() + ":";
  const auto* reply = result.GetIfString();
  int storage_counter = 0;
  const auto& workers = WorkerContext().GetRunningServiceWorkerInfos();
  if (!reply || !reply->starts_with(startup_prefix) ||
      !base::StringToInt(std::string_view(*reply).substr(startup_prefix.size()),
                         &storage_counter) ||
      storage_counter <= 0 || workers.size() != 1) {
    LOG(ERROR) << "Extension worker did not confirm startup storage: " << result;
    Finish(false);
    return;
  }
  startup_worker_ = workers.begin()->second.token;
  std::puts("Zitter Content extension startup event woke the service worker");
  std::printf("Zitter Content extension local storage counter: %d\n",
              storage_counter);
  if (paired_) {
    std::printf("Zitter Content paired extension storage: %s\n", reply->c_str());
    std::move(peer_barrier_).Run();
    return;
  }
  ContinueAfterWorkerWakeUp();
}

void ExtensionNavigationProbe::ContinueAfterWorkerWakeUp() {
  if (!StartupWorkerIsRunning()) {
    Finish(false);
    return;
  }
  WaitForContentScripts(base::TimeTicks::Now() + base::Seconds(10));
}

bool ExtensionNavigationProbe::StartupWorkerIsRunning() {
  const auto& workers = WorkerContext().GetRunningServiceWorkerInfos();
  if (workers.size() != 1)
    return false;
  const auto& worker = workers.begin()->second;
  auto* process = content::RenderProcessHost::FromID(worker.render_process_id);
  return worker.token == startup_worker_ && process &&
         process->GetBrowserContext() == &*owner_ &&
         extensions::ProcessMap::Get(&*owner_)->Contains(extension_->id(),
                                                        worker.render_process_id);
}

void ExtensionNavigationProbe::ContinueAfterPeerDisabled() {
  auto* registry = extensions::ExtensionRegistry::Get(&*other_);
  if (!registry->disabled_extensions().Contains(extension_->id()) ||
      registry->enabled_extensions().Contains(extension_->id()) ||
      !StartupWorkerIsRunning()) {
    LOG(ERROR) << "Disabling the peer did not preserve the active extension";
    Finish(false);
    return;
  }
  step_ = Step::kPeerDisabled;
  Navigate(*owner_);
}

void ExtensionNavigationProbe::WaitForContentScripts(base::TimeTicks deadline) {
  const auto& fixture = *extension_;
  auto* loader = extensions::ExtensionSystem::Get(&*owner_)
                     ->user_script_manager()
                     ->GetUserScriptLoaderForExtension(fixture.id());
  if (loader->initial_load_complete()) {
    CheckContentScript(ScriptCheck::kOwner, *owner_);
  } else if (base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "Extension content scripts did not load";
    Finish(false);
  } else {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, base::BindOnce(&ExtensionNavigationProbe::WaitForContentScripts,
                                 weak_factory_.GetWeakPtr(), deadline),
        base::Milliseconds(50));
  }
}

void ExtensionNavigationProbe::CheckContentScript(ScriptCheck check,
                                                  BrowserContext& context) {
  if (!web_page_) {
    Finish(false);
    return;
  }
  script_check_ = check;
  std::string_view path = "/extension-content/allowed";
  if (check == ScriptCheck::kUnmatched)
    path = "/extension-content/unmatched";
  else if (check == ScriptCheck::kExtraAllowed)
    path = "/extension-content/other-allowed";
  else if (check == ScriptCheck::kExtraExcluded)
    path = "/extension-content/other-excluded";
  script_check_url_ = web_page_->GetLastCommittedURL().Resolve(path);
  Observe(nullptr);
  if (check == ScriptCheck::kRemoved) {
    page_ = std::move(owner_script_page_);
  } else if (check != ScriptCheck::kUnmatched && check != ScriptCheck::kExtraAllowed &&
             check != ScriptCheck::kExtraExcluded) {
    page_ = content::WebContents::Create(
        content::WebContents::CreateParams(&context));
  }
  Observe(page_.get());
  page_->GetController().LoadURLWithParams(
      content::NavigationController::LoadURLParams(script_check_url_));
}

void ExtensionNavigationProbe::ReadContentScriptResult(base::TimeTicks deadline) {
  const bool check_message = paired_ && script_check_ == ScriptCheck::kOwner;
  const bool check_frames = script_check_ == ScriptCheck::kExtraAllowed;
  page_->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
      check_frames
          ? u"[document.title, document.documentElement.dataset.zitterContentScript,"
            u" document.documentElement.dataset.matchedFrame,"
            u" document.documentElement.dataset.excludedFrame,"
            u" document.documentElement.dataset.fallbackAllowed,"
            u" document.documentElement.dataset.fallbackDenied,"
            u" document.documentElement.dataset.srcdocScript,"
            u" document.documentElement.dataset.blankScript]"
      : script_check_ == ScriptCheck::kExtraExcluded
          ? u"[document.title, document.documentElement.dataset.zitterContentScript,"
            u" document.documentElement.dataset.srcdocScript,"
            u" document.documentElement.dataset.blankScript]"
      : check_message
          ? u"[document.title, document.documentElement.dataset.zitterContentScript,"
            u" document.documentElement.dataset.zitterExtensionMessage]"
          : u"[document.title, document.documentElement.dataset.zitterContentScript]",
      base::BindOnce(&ExtensionNavigationProbe::OnContentScriptResult,
                     weak_factory_.GetWeakPtr(), deadline),
      content::ISOLATED_WORLD_ID_CONTENT_END);
}

void ExtensionNavigationProbe::OnContentScriptResult(
    base::TimeTicks deadline, base::Value result) {
  base::ListValue expected;
  if (script_check_ == ScriptCheck::kOwner || script_check_ == ScriptCheck::kExtraAllowed) {
    expected.Append("Extension content script ran at document start");
    expected.Append("injected");
  } else {
    expected.Append("Extension content script absent");
    expected.Append(base::Value());
  }
  const bool check_message = paired_ && script_check_ == ScriptCheck::kOwner;
  if (check_message)
    expected.Append(extension_->version().GetString());
  if (script_check_ == ScriptCheck::kExtraAllowed) {
    expected.Append("injected");
    expected.Append("absent");
    expected.Append("injected,injected");
    expected.Append("absent,absent");
    expected.Append("injected");
    expected.Append("injected");
  } else if (script_check_ == ScriptCheck::kExtraExcluded) {
    expected.Append("absent");
    expected.Append("absent");
  }
  if (expected.size() > 2 && result.is_list() &&
      result.GetList().size() == expected.size() &&
      std::ranges::any_of(std::views::drop(result.GetList(), 2),
                          [](const base::Value& value) { return value.is_none(); }) &&
      base::TimeTicks::Now() < deadline) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, base::BindOnce(&ExtensionNavigationProbe::ReadContentScriptResult,
                                  weak_factory_.GetWeakPtr(), deadline),
        base::Milliseconds(50));
    return;
  }
  if (result != base::Value(std::move(expected))) {
    LOG(ERROR) << "Unexpected extension content script result for check "
               << static_cast<int>(script_check_) << ": " << result;
    Finish(false);
    return;
  }
  if (check_message) {
    std::printf("Zitter Content extension frame received its worker version: %s\n",
                extension_->version().GetString().c_str());
  }
  switch (script_check_) {
    case ScriptCheck::kOwner: {
      owner_script_renderer_ = page_->GetPrimaryMainFrame()->GetProcess()->GetID();
      const auto extra = page_->GetLastCommittedURL().Resolve(
          "/extension-content/other-allowed");
      const auto& scripts = extensions::ContentScriptsInfo::GetContentScripts(extension_.get());
      const bool matches_extra = std::ranges::any_of(scripts, [&](const auto& script) {
        return script->MatchesURL(extra);
      });
      CheckContentScript(matches_extra ? ScriptCheck::kExtraAllowed : ScriptCheck::kUnmatched,
                         *owner_);
      break;
    }
    case ScriptCheck::kExtraAllowed:
      std::puts("Zitter Content injected the second declared script scope");
      CheckContentScript(ScriptCheck::kExtraExcluded, *owner_);
      break;
    case ScriptCheck::kExtraExcluded:
      CheckContentScript(ScriptCheck::kUnmatched, *owner_);
      break;
    case ScriptCheck::kUnmatched:
      owner_script_page_ = std::move(page_);
      if (paired_)
        CheckContentScript(ScriptCheck::kPrivate, *private_);
      else
        CheckContentScript(ScriptCheck::kOther, *other_);
      break;
    case ScriptCheck::kOther:
      CheckContentScript(ScriptCheck::kPrivate, *private_);
      break;
    case ScriptCheck::kPrivate:
      script_check_ = ScriptCheck::kNone;
      if (state_action_ == ExtensionFixtureStateAction::kDisable ||
          state_action_ == ExtensionFixtureStateAction::kPauseAfterDisableCommit) {
        user_choice_ready_ = true;
        if (!change_settings_.Run(*owner_, false))
          Finish(false);
      } else {
        DisableExtension();
      }
      break;
    case ScriptCheck::kRemoved:
      if (page_->GetPrimaryMainFrame()->GetProcess()->GetID() !=
          owner_script_renderer_) {
        LOG(ERROR) << "Extension unload check did not reuse the extension profile's renderer";
        Finish(false);
        return;
      }
      std::puts("Zitter Content extension document-start script isolation passed");
      Finish(true);
      break;
    case ScriptCheck::kInitiallyDisabled:
      script_check_ = ScriptCheck::kNone;
      std::puts("Zitter Content restored the extension disabled by the user");
      if (state_action_ == ExtensionFixtureStateAction::kEnable) {
        user_choice_ready_ = true;
        if (!change_settings_.Run(*owner_, true))
          Finish(false);
      } else {
        Finish(true);
      }
      break;
    case ScriptCheck::kNone:
      NOTREACHED();
  }
}

void ExtensionNavigationProbe::DisableExtension() {
  if (!GetClient()->DisableFixture(*owner_, extension_->id())) {
    Finish(false);
    return;
  }
  CheckWorkerRemoval(base::TimeTicks::Now() + base::Seconds(10));
}

void ExtensionNavigationProbe::CheckWorkerRemoval(base::TimeTicks deadline) {
  const auto& fixture = *extension_;
  WorkerContext().CheckHasServiceWorker(
      fixture.url(), blink::StorageKey::CreateFirstParty(fixture.origin()),
      base::BindOnce(&ExtensionNavigationProbe::OnWorkerRemovalChecked,
                     weak_factory_.GetWeakPtr(), deadline));
}

void ExtensionNavigationProbe::OnWorkerRemovalChecked(
    base::TimeTicks deadline,
    content::ServiceWorkerCapability capability) {
  if (capability != content::ServiceWorkerCapability::NO_SERVICE_WORKER) {
    if (base::TimeTicks::Now() >= deadline) {
      LOG(ERROR) << "Extension probe worker remained registered";
      Finish(false);
      return;
    }
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, base::BindOnce(&ExtensionNavigationProbe::CheckWorkerRemoval,
                                 weak_factory_.GetWeakPtr(), deadline),
        base::Milliseconds(50));
    return;
  }
  if (CanLoadExtensionProbeResources(*owner_, extension_->id()) ||
      extensions::ProcessMap::Get(&*owner_)->ExtensionHasProcess(extension_->id())) {
    LOG(ERROR) << "The disabled extension kept access to its profile";
    Finish(false);
    return;
  }
  std::puts("Zitter Content extension service worker executed and unregistered");
  Advance();
}

void ExtensionNavigationProbe::Advance() {
  switch (step_) {
    case Step::kOwner:
      if (paired_) {
        step_ = Step::kPrivate;
        Navigate(*private_);
      } else {
        step_ = Step::kOther;
        Navigate(*other_);
      }
      return;
    case Step::kOther:
      step_ = Step::kPrivate;
      Navigate(*private_);
      return;
    case Step::kPrivate:
      step_ = Step::kRemoved;
      Navigate(*owner_);
      return;
    case Step::kInitiallyDisabled:
      if (GetClient()->fixture(*owner_)->legacy_disabled) {
        std::puts("Zitter Content kept the imported extension disabled");
        Finish(true);
      } else {
        CheckContentScript(ScriptCheck::kInitiallyDisabled, *owner_);
      }
      return;
    case Step::kRemoved:
      std::puts("Zitter Content extension navigation isolation passed");
      CheckContentScript(ScriptCheck::kRemoved, *owner_);
      return;
    case Step::kPeerDisabled:
      NOTREACHED();
  }
}

void ExtensionNavigationProbe::Finish(bool success) {
  weak_factory_.InvalidateWeakPtrs();
  if (done_) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(done_), success));
  }
}

}  // namespace zitter::content_embedder
