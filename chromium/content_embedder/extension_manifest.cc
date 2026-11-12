// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "extension_manifest.h"

#include "extensions_manager.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/command_line.h"
#include "base/containers/fixed_flat_set.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/i18n/icu_string_conversions.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/memory/weak_ptr.h"
#include "base/task/thread_pool.h"
#include "base/notreached.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread_restrictions.h"
#include "base/values.h"
#include "base/version.h"
#include "build/build_config.h"
#include "components/crx_file/crx_verifier.h"
#include "components/crx_file/id_util.h"
#include "components/policy/core/common/schema.h"
#include "components/version_info/version_info.h"
#include "crypto/hash.h"
#include "net/base/url_util.h"
#include "net/url_request/redirect_info.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "extension_archive_service.h"
#include "extension_registry_probe.h"
#include "extensions/browser/api/declarative_net_request/file_backed_ruleset_source.h"
#include "extensions/browser/api/declarative_net_request/install_index_helper.h"
#include "extensions/browser/api/declarative_net_request/utils.h"
#include "extensions/browser/ruleset_parse_result.h"
#include "extensions/common/api/declarative_net_request.h"
#include "extensions/common/api/extension_action/action_info.h"
#include "extensions/common/constants.h"
#include "extensions/common/manifest_handlers/web_accessible_resources_info.h"
#include "extensions/common/manifest_handlers/options_page_info.h"
#include "extensions/common/core_extensions_api_provider.h"
#include "extensions/common/features/feature_provider.h"
#include "extensions/common/features/permission_feature.h"
#include "extensions/common/features/simple_feature.h"
#include "extensions/common/mojom/context_type.mojom-shared.h"
#include "extensions/common/mojom/api_permission_id.mojom-shared.h"
#include "extensions/common/permissions/api_permission.h"
#include "extensions/common/permissions/api_permission_set.h"
#include "extensions/common/permissions/permissions_info.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_urls.h"
#include "extensions/common/extensions_client.h"
#include "extensions/common/user_scripts_availability.h"
#include "extensions/common/extension_l10n_util.h"
#include "extensions/common/file_util.h"
#include "extensions/common/install_warning.h"
#include "extensions/common/manifest.h"
#include "extensions/common/manifest_constants.h"
#include "extensions/common/manifest_handler.h"
#include "extensions/common/manifest_handler_registry.h"
#include "extensions/common/manifest_handlers/background_info.h"
#include "extensions/common/manifest_handlers/content_scripts_handler.h"
#include "extensions/common/manifest_handlers/csp_info.h"
#include "extensions/common/manifest_handlers/externally_connectable.h"
#include "extensions/common/manifest_handlers/incognito_info.h"
#include "extensions/common/manifest_handlers/permissions_parser.h"
#include "extensions/common/permissions/permission_set.h"
#include "extensions/common/permissions/permissions_data.h"
#include "extensions/common/url_pattern_set.h"
#include "extensions/common/url_pattern.h"
#include "extensions/common/user_script.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_WIN)
#include <windows.h>
#else
#include <cerrno>
#include <sys/stat.h>
#endif

namespace zitter::content_embedder {
namespace {

class ManagedStorageSchemaHandler final : public extensions::ManifestHandler {
 public:
  bool Parse(extensions::Extension* extension, std::u16string* error) override {
    if (!extension->manifest()->FindStringPath(
            extensions::manifest_keys::kStorageManagedSchema)) {
      *error = u"storage.managed_schema must be a string";
      return false;
    }
    extensions::PermissionsParser::AddAPIPermission(
        extension, extensions::mojom::APIPermissionID::kStorage);
    return true;
  }

  bool Validate(const extensions::Extension& extension,
                std::string* error,
                std::vector<extensions::InstallWarning>* warnings) const override {
    const auto* name = extension.manifest()->FindStringPath(
        extensions::manifest_keys::kStorageManagedSchema);
    const auto relative = name ? ExtensionArchiveResourcePath(*name) : std::nullopt;
    base::FilePath root;
    base::FilePath schema;
    std::string contents;
    if (!relative || !base::NormalizeFilePath(extension.path(), &root) ||
        !base::NormalizeFilePath(root.Append(*relative), &schema) ||
        schema != root.Append(*relative) ||
        !base::ReadFileToStringWithMaxSize(schema, &contents, kMaxFixtureFileSize)) {
      *error = "storage.managed_schema must name a packaged schema file";
      return false;
    }
    auto parsed = policy::Schema::Parse(contents);
    if (!parsed.has_value()) {
      *error = "Invalid storage.managed_schema: " + parsed.error();
      return false;
    }
    return true;
  }

 private:
  base::span<const char* const> Keys() const override {
    static constexpr const char* kKeys[] = {
        extensions::manifest_keys::kStorageManagedSchema};
    return kKeys;
  }
};

class LabExtensionAPIProvider final : public extensions::CoreExtensionsAPIProvider {
 public:
  void AddAPIFeatures(extensions::FeatureProvider* provider) override {
    CoreExtensionsAPIProvider::AddAPIFeatures(provider);
    static constexpr auto kTypes = std::to_array({extensions::Manifest::Type::kExtension});
    static constexpr auto kContexts =
        std::to_array({extensions::mojom::ContextType::kPrivilegedExtension});
    static constexpr extensions::SimpleFeatureData kTabsData{
        .feature = {.name = "tabs"},
        .config = {.extension_types = extensions::StaticSpan(kTypes),
                   .contexts = extensions::StaticSpan(kContexts)}};
    static const base::NoDestructor<extensions::SimpleFeature> tabs{
        extensions::StaticFeatureData<extensions::SimpleFeatureData>(kTabsData)};
    static constexpr extensions::SimpleFeatureData kWindowsData{
        .feature = {.name = "windows"},
        .config = {.extension_types = extensions::StaticSpan(kTypes),
                   .contexts = extensions::StaticSpan(kContexts)}};
    static const base::NoDestructor<extensions::SimpleFeature> windows{
        extensions::StaticFeatureData<extensions::SimpleFeatureData>(kWindowsData)};
    static constexpr extensions::SimpleFeatureData kExtensionData{
        .feature = {.name = "extension"},
        .config = {.extension_types = extensions::StaticSpan(kTypes),
                   .contexts = extensions::StaticSpan(kContexts),
                   .min_manifest_version = 3}};
    static constexpr extensions::SimpleFeatureData kBackgroundPageData{
        .feature = {.name = "extension.getBackgroundPage"},
        .config = {.extension_types = extensions::StaticSpan(kTypes),
                   .contexts = extensions::StaticSpan(kContexts),
                   .min_manifest_version = 3,
                   .disallow_for_service_workers = true}};
    static const base::NoDestructor<extensions::SimpleFeature> extension{
        extensions::StaticFeatureData<extensions::SimpleFeatureData>(kExtensionData)};
    static const base::NoDestructor<extensions::SimpleFeature> background_page{
        extensions::StaticFeatureData<extensions::SimpleFeatureData>(kBackgroundPageData)};
    static constexpr auto kFontSettingsDependencies =
        std::to_array<std::string_view>({"permission:fontSettings"});
    static constexpr extensions::SimpleFeatureData kFontSettingsData{
        .feature = {.name = "fontSettings"},
        .config = {.dependencies = extensions::StaticSpan(kFontSettingsDependencies),
                   .extension_types = extensions::StaticSpan(kTypes),
                   .contexts = extensions::StaticSpan(kContexts)}};
    static const base::NoDestructor<extensions::SimpleFeature> font_settings{
        extensions::StaticFeatureData<extensions::SimpleFeatureData>(kFontSettingsData)};
    static constexpr auto kCommandsDependencies =
        std::to_array<std::string_view>({"manifest:commands"});
    static constexpr extensions::SimpleFeatureData kCommandsData{
        .feature = {.name = "commands"},
        .config = {.dependencies = extensions::StaticSpan(kCommandsDependencies),
                   .extension_types = extensions::StaticSpan(kTypes),
                   .contexts = extensions::StaticSpan(kContexts)}};
    static const base::NoDestructor<extensions::SimpleFeature> commands{
        extensions::StaticFeatureData<extensions::SimpleFeatureData>(kCommandsData)};
    static constexpr auto kContextMenusDependencies =
        std::to_array<std::string_view>({"permission:contextMenus"});
    static constexpr extensions::SimpleFeatureData kContextMenusData{
        .feature = {.name = "contextMenus"},
        .config = {.dependencies = extensions::StaticSpan(kContextMenusDependencies),
                   .extension_types = extensions::StaticSpan(kTypes),
                   .contexts = extensions::StaticSpan(kContexts)}};
    static const base::NoDestructor<extensions::SimpleFeature> context_menus{
        extensions::StaticFeatureData<extensions::SimpleFeatureData>(kContextMenusData)};
    static constexpr auto kActionDependencies =
        std::to_array<std::string_view>({"manifest:action"});
    static constexpr extensions::SimpleFeatureData kActionData{
        .feature = {.name = "action"},
        .config = {.dependencies = extensions::StaticSpan(kActionDependencies),
                   .extension_types = extensions::StaticSpan(kTypes),
                   .contexts = extensions::StaticSpan(kContexts)}};
    static const base::NoDestructor<extensions::SimpleFeature> action{
        extensions::StaticFeatureData<extensions::SimpleFeatureData>(kActionData)};
    static constexpr std::string_view kNavigationDependencies[] = {"permission:webNavigation"};
    static constexpr extensions::SimpleFeatureData kNavigationData{
        .feature = {.name = "webNavigation"},
        .config = {.dependencies = extensions::StaticSpan(kNavigationDependencies),
                   .extension_types = extensions::StaticSpan(kTypes),
                   .contexts = extensions::StaticSpan(kContexts)}};
    static const base::NoDestructor<extensions::SimpleFeature> navigation{
        extensions::StaticFeatureData<extensions::SimpleFeatureData>(kNavigationData)};
    static constexpr std::string_view kSidePanelDependencies[] = {"permission:sidePanel"};
    static constexpr extensions::SimpleFeatureData kSidePanelData{
        .feature = {.name = "sidePanel"},
        .config = {.dependencies = extensions::StaticSpan(kSidePanelDependencies),
                   .extension_types = extensions::StaticSpan(kTypes),
                   .contexts = extensions::StaticSpan(kContexts)}};
    static const base::NoDestructor<extensions::SimpleFeature> side_panel{
        extensions::StaticFeatureData<extensions::SimpleFeatureData>(kSidePanelData)};
#if BUILDFLAG(IS_MAC)
    static constexpr std::string_view kNotificationsDependencies[] = {"permission:notifications"};
    static constexpr extensions::SimpleFeatureData kNotificationsData{
        .feature = {.name = "notifications"},
        .config = {.dependencies = extensions::StaticSpan(kNotificationsDependencies),
                   .extension_types = extensions::StaticSpan(kTypes),
                   .contexts = extensions::StaticSpan(kContexts)}};
    static const base::NoDestructor<extensions::SimpleFeature> notifications{
        extensions::StaticFeatureData<extensions::SimpleFeatureData>(kNotificationsData)};
#endif
    static constexpr std::string_view kPrivacyDependencies[] = {"permission:privacy"};
    static constexpr extensions::SimpleFeatureData kPrivacyData{
        .feature = {.name = "privacy"},
        .config = {.dependencies = extensions::StaticSpan(kPrivacyDependencies),
                   .extension_types = extensions::StaticSpan(kTypes),
                   .contexts = extensions::StaticSpan(kContexts)}};
    static const base::NoDestructor<extensions::SimpleFeature> privacy{
        extensions::StaticFeatureData<extensions::SimpleFeatureData>(kPrivacyData)};
    const extensions::Feature* features[] = {
#if BUILDFLAG(IS_MAC)
        notifications.get(),
#endif
        privacy.get(), tabs.get(), font_settings.get(), commands.get(), context_menus.get(), action.get(),
        navigation.get(), side_panel.get(), extension.get(), background_page.get(),
        windows.get()};
    provider->AddStaticFeatures(features);
  }

  void AddManifestFeatures(extensions::FeatureProvider* provider) override {
    CoreExtensionsAPIProvider::AddManifestFeatures(provider);
    static constexpr auto kTypes =
        std::to_array({extensions::Manifest::Type::kExtension});
    static constexpr extensions::SimpleFeatureData kMinimumVersionData{
        .feature = {.name = "minimum_chrome_version"},
        .config = {.extension_types = extensions::StaticSpan(kTypes)}};
    static const base::NoDestructor<extensions::SimpleFeature> minimum_version{
        extensions::StaticFeatureData<extensions::SimpleFeatureData>(kMinimumVersionData)};
    static constexpr extensions::SimpleFeatureData kStorageData{
        .feature = {.name = "storage"},
        .config = {.extension_types = extensions::StaticSpan(kTypes)}};
    static const base::NoDestructor<extensions::SimpleFeature> storage{
        extensions::StaticFeatureData<extensions::SimpleFeatureData>(kStorageData)};
    static constexpr extensions::SimpleFeatureData kSidePanelData{
        .feature = {.name = "side_panel"},
        .config = {.extension_types = extensions::StaticSpan(kTypes)}};
    static const base::NoDestructor<extensions::SimpleFeature> side_panel{
        extensions::StaticFeatureData<extensions::SimpleFeatureData>(kSidePanelData)};
    const extensions::Feature* features[] = {minimum_version.get(), storage.get(), side_panel.get()};
    provider->AddStaticFeatures(features);
  }

  void RegisterManifestHandlers(extensions::ManifestHandlerRegistry* registry) override {
    CoreExtensionsAPIProvider::RegisterManifestHandlers(registry);
    registry->RegisterHandler(std::make_unique<ManagedStorageSchemaHandler>());
  }

  void AddPermissionFeatures(extensions::FeatureProvider* provider) override {
    CoreExtensionsAPIProvider::AddPermissionFeatures(provider);
    static constexpr auto kTypes = std::to_array({extensions::Manifest::Type::kExtension});
    static constexpr extensions::SimpleFeatureData kPermissionData{
        .feature = {.name = "tabs"},
        .config = {.extension_types = extensions::StaticSpan(kTypes)}};
    static const base::NoDestructor<extensions::SimpleFeature> tabs{
        extensions::StaticFeatureData<extensions::SimpleFeatureData>(kPermissionData)};
    static constexpr extensions::SimpleFeatureData kScriptingData{
        .feature = {.name = "scripting"},
        .config = {.extension_types = extensions::StaticSpan(kTypes),
                   .min_manifest_version = 3}};
    static const base::NoDestructor<extensions::SimpleFeature> scripting{
        extensions::StaticFeatureData<extensions::SimpleFeatureData>(kScriptingData)};
    static constexpr extensions::SimpleFeatureData kFontSettingsData{
        .feature = {.name = "fontSettings"},
        .config = {.extension_types = extensions::StaticSpan(kTypes)}};
    static const base::NoDestructor<extensions::SimpleFeature> font_settings{
        extensions::StaticFeatureData<extensions::SimpleFeatureData>(kFontSettingsData)};
    static constexpr extensions::SimpleFeatureData kContextMenusData{
        .feature = {.name = "contextMenus"},
        .config = {.extension_types = extensions::StaticSpan(kTypes)}};
    static const base::NoDestructor<extensions::SimpleFeature> context_menus{
        extensions::StaticFeatureData<extensions::SimpleFeatureData>(kContextMenusData)};
    static constexpr extensions::SimpleFeatureData kClipboardReadData{
        .feature = {.name = "clipboardRead"},
        .config = {.extension_types = extensions::StaticSpan(kTypes)}};
    static constexpr extensions::SimpleFeatureData kClipboardWriteData{
        .feature = {.name = "clipboardWrite"},
        .config = {.extension_types = extensions::StaticSpan(kTypes)}};
    static const base::NoDestructor<extensions::SimpleFeature> clipboard_read{
        extensions::StaticFeatureData<extensions::SimpleFeatureData>(kClipboardReadData)};
    static const base::NoDestructor<extensions::SimpleFeature> clipboard_write{
        extensions::StaticFeatureData<extensions::SimpleFeatureData>(kClipboardWriteData)};
    static constexpr extensions::SimpleFeatureData kNavigationData{
        .feature = {.name = "webNavigation"},
        .config = {.extension_types = extensions::StaticSpan(kTypes)}};
    static const base::NoDestructor<extensions::SimpleFeature> navigation{
        extensions::StaticFeatureData<extensions::SimpleFeatureData>(kNavigationData)};
    static constexpr extensions::SimpleFeatureData kSidePanelData{
        .feature = {.name = "sidePanel"},
        .config = {.extension_types = extensions::StaticSpan(kTypes)}};
    static const base::NoDestructor<extensions::SimpleFeature> side_panel{
        extensions::StaticFeatureData<extensions::SimpleFeatureData>(kSidePanelData)};
#if BUILDFLAG(IS_MAC)
    static constexpr extensions::SimpleFeatureData kNotificationsData{
        .feature = {.name = "notifications"},
        .config = {.extension_types = extensions::StaticSpan(kTypes)}};
    static const base::NoDestructor<extensions::PermissionFeature> notifications{
        extensions::StaticFeatureData<extensions::SimpleFeatureData>(kNotificationsData)};
#endif
    static constexpr extensions::SimpleFeatureData kPrivacyData{
        .feature = {.name = "privacy"},
        .config = {.extension_types = extensions::StaticSpan(kTypes)}};
    static const base::NoDestructor<extensions::PermissionFeature> privacy{
        extensions::StaticFeatureData<extensions::SimpleFeatureData>(kPrivacyData)};
    const extensions::Feature* features[] = {
#if BUILDFLAG(IS_MAC)
        notifications.get(),
#endif
        privacy.get(), tabs.get(), scripting.get(), font_settings.get(), context_menus.get(),
        clipboard_read.get(), clipboard_write.get(), navigation.get(), side_panel.get()};
    provider->AddStaticFeatures(features);
  }

  void RegisterPermissions(extensions::PermissionsInfo* info) override {
    CoreExtensionsAPIProvider::RegisterPermissions(info);
    static constexpr extensions::APIPermissionInfo::InitInfo kPermissions[] = {
#if BUILDFLAG(IS_MAC)
        {extensions::mojom::APIPermissionID::kNotifications, "notifications"},
#endif
        {extensions::mojom::APIPermissionID::kPrivacy, "privacy"},
        {extensions::mojom::APIPermissionID::kTab, "tabs",
         extensions::APIPermissionInfo::kFlagRequiresManagementUIWarning},
        {extensions::mojom::APIPermissionID::kScripting, "scripting",
         extensions::APIPermissionInfo::kFlagRequiresManagementUIWarning},
        {extensions::mojom::APIPermissionID::kFontSettings, "fontSettings",
         extensions::APIPermissionInfo::kFlagCannotBeOptional},
        {extensions::mojom::APIPermissionID::kContextMenus, "contextMenus"},
        {extensions::mojom::APIPermissionID::kWebNavigation, "webNavigation",
         extensions::APIPermissionInfo::kFlagRequiresManagementUIWarning},
        {extensions::mojom::APIPermissionID::kSidePanel, "sidePanel"}};
    info->RegisterPermissions(kPermissions, {});
  }

  bool IsAPISchemaGenerated(const std::string& name) override {
    return name == "extension" || name == "tabs" || name == "windows" ||
           name == "fontSettings" || name == "commands" ||
           name == "contextMenus" || name == "action" || name == "webNavigation" || name == "sidePanel" ||
           name == "notifications" || name == "privacy" || name == "cookies" || CoreExtensionsAPIProvider::IsAPISchemaGenerated(name);
  }

  std::string_view GetAPISchema(const std::string& name) override {
    if (name == "windows") {
      return R"json({
        "namespace":"windows",
        "properties":{
          "WINDOW_ID_NONE":{"type":"integer","value":-1},
          "WINDOW_ID_CURRENT":{"type":"integer","value":-2}
        },
        "types":[{"id":"windows.QueryOptions","type":"object","properties":{
          "populate":{"type":"boolean","optional":true},
          "windowTypes":{"type":"array","items":{"type":"string"},"optional":true}
        }}],
        "functions":[
          {"name":"getCurrent","type":"function","parameters":[
            {"name":"options","$ref":"windows.QueryOptions","optional":true}
          ],"returns_async":{"name":"callback","parameters":[
            {"name":"window","type":"object","additionalProperties":{"type":"any"}}
          ]}},
          {"name":"getAll","type":"function","parameters":[
            {"name":"options","$ref":"windows.QueryOptions","optional":true}
          ],"returns_async":{"name":"callback","parameters":[
            {"name":"windows","type":"array","items":{"type":"object","additionalProperties":{"type":"any"}}}
          ]}}
        ],
        "events":[
          {"name":"onFocusChanged","type":"function","parameters":[{"name":"windowId","type":"integer"}]},
          {"name":"onRemoved","type":"function","parameters":[{"name":"windowId","type":"integer"}]}
        ]
      })json";
    }
    if (name == "extension") {
      return R"json({"namespace":"extension","functions":[{
        "name":"getBackgroundPage","type":"function","parameters":[],
        "returns":{"name":"backgroundPage","type":"object","optional":true}
      }]})json";
    }
    if (name == "cookies") {
      return R"json({
        "namespace":"cookies",
        "functions":[{"name":"get","type":"function","parameters":[
          {"name":"details","type":"object","properties":{
            "url":{"type":"string"},"name":{"type":"string"},
            "storeId":{"type":"string","optional":true}
          }}],
          "returns_async":{"name":"callback","parameters":[
            {"name":"cookie","type":"object","additionalProperties":{"type":"any"},"optional":true}
          ]}
        }]
      })json";
    }
    if (name == "privacy") {
      return
#include "privacy_schema.inc"
          ;
    }
    if (name == "notifications") {
      return
#include "notifications_schema.inc"
          ;
    }
    if (name == "tabs") {
      return
#include "tabs_messaging_schema.inc"
          ;
    }
    if (name == "sidePanel") {
      return
#include "side_panel_schema.inc"
          ;
    }
    if (name == "webNavigation") {
      return
#include "web_navigation_schema.inc"
          ;
    }
    if (name == "fontSettings") {
      return R"json({
        "namespace": "fontSettings",
        "types": [{
          "id": "fontSettings.FontName",
          "type": "object",
          "properties": {
            "fontId": {"type": "string"},
            "displayName": {"type": "string"}
          }
        }],
        "functions": [{
          "name": "getFontList",
          "type": "function",
          "parameters": [],
          "returns_async": {
            "name": "callback",
            "parameters": [{
              "name": "fonts", "type": "array", "items": {"$ref": "fontSettings.FontName"}
            }]
          }
        }]
      })json";
    }
    if (name == "commands") {
      return R"json({
        "namespace": "commands",
        "types": [{
          "id": "commands.Command",
          "type": "object",
          "properties": {
            "name": {"type": "string", "optional": true},
            "description": {"type": "string", "optional": true},
            "shortcut": {"type": "string", "optional": true}
          }
        }],
        "functions": [{
          "name": "getAll",
          "type": "function",
          "parameters": [],
          "returns_async": {
            "name": "callback",
            "parameters": [{
              "name": "commands", "type": "array", "items": {"$ref": "commands.Command"}
            }]
          }
        }],
        "events": [{
          "name": "onCommand",
          "type": "function",
          "parameters": [
            {"name": "command", "type": "string"},
            {"name": "tab", "$ref": "tabs.Tab", "optional": true}
          ]
        }]
      })json";
    }
    if (name == "contextMenus") {
      return R"json({
        "namespace": "contextMenus",
        "functions": [{
          "name": "create", "type": "function",
          "parameters": [{
            "name": "createProperties", "type": "object",
            "properties": {
              "id": {"type": "string"},
              "title": {"type": "string", "optional": true},
              "type": {"type": "string", "enum": ["normal", "separator"], "optional": true},
              "enabled": {"type": "boolean", "optional": true},
              "parentId": {"type": "string", "optional": true},
              "contexts": {"type": "array", "optional": true,
                           "items": {"type": "string", "enum": ["page", "all"]}}
            }
          }],
          "returns": {"type": "string"},
          "returns_async": {
            "name": "callback", "optional": true, "parameters": [],
            "does_not_support_promises": "The item identifier is returned synchronously."
          }
        }, {
          "name": "removeAll", "type": "function", "parameters": [],
          "returns_async": {"name": "callback", "optional": true, "parameters": []}
        }],
        "events": [{
          "name": "onClicked", "type": "function",
          "parameters": [{
            "name": "info", "type": "object",
            "properties": {
              "menuItemId": {"type": "string"},
              "parentMenuItemId": {"type": "string", "optional": true},
              "pageUrl": {"type": "string", "optional": true},
              "frameUrl": {"type": "string", "optional": true},
              "frameId": {"type": "integer", "optional": true},
              "editable": {"type": "boolean"}
            }
          }, {"name": "tab", "$ref": "tabs.Tab", "optional": true}]
        }]
      })json";
    }
    if (name == "action") {
      return R"json({
        "namespace": "action",
        "functions": [{
          "name": "setIcon", "type": "function",
          "parameters": [{"name": "details", "type": "object", "properties": {
            "tabId": {"type": "integer", "optional": true},
            "path": {"choices": [{"type": "string"}, {"type": "object",
                      "additionalProperties": {"type": "string"}}]}
          }}],
          "returns_async": {"name": "callback", "optional": true, "parameters": []}
        }, {
          "name": "setBadgeText", "type": "function",
          "parameters": [{"name": "details", "type": "object", "properties": {
            "tabId": {"type": "integer", "optional": true},
            "text": {"type": "string"}
          }}],
          "returns_async": {"name": "callback", "optional": true, "parameters": []}
        }, {
          "name": "setBadgeBackgroundColor", "type": "function",
          "parameters": [{"name": "details", "type": "object", "properties": {
            "tabId": {"type": "integer", "optional": true},
            "color": {"choices": [{"type": "string"}, {"type": "array",
                        "items": {"type": "integer"}, "minItems": 4, "maxItems": 4}]}
          }}],
          "returns_async": {"name": "callback", "optional": true, "parameters": []}
        }]
      })json";
    }
    return CoreExtensionsAPIProvider::GetAPISchema(name);
  }
};

class LabExtensionsClient final : public extensions::ExtensionsClient {
 public:
  LabExtensionsClient();
  ~LabExtensionsClient() override = default;

  void Initialize() override {}
  void InitializeWebStoreUrls(base::CommandLine*) override {}
  const extensions::PermissionMessageProvider& GetPermissionMessageProvider()
      const override;
  const std::string GetProductName() override;
  void FilterHostPermissions(const extensions::URLPatternSet& hosts,
                             extensions::URLPatternSet* new_hosts,
                             extensions::PermissionIDSet*) const override;
  void SetScriptingAllowlist(const ScriptingAllowlist&) override {}
  const ScriptingAllowlist& GetScriptingAllowlist() const override;
  extensions::URLPatternSet GetPermittedChromeSchemeHosts(
      const extensions::Extension*,
      const extensions::APIPermissionSet&) const override;
  bool IsScriptableURL(const GURL& url, std::string* error) const override;
  const GURL& GetWebstoreBaseURL() const override { return webstore_; }
  const GURL& GetNewWebstoreBaseURL() const override { return new_webstore_; }
  const GURL& GetWebstoreUpdateURL() const override { return update_url_; }
  bool IsBlocklistUpdateURL(const GURL&) const override { return false; }

 private:
  const ScriptingAllowlist allowlist_;
  const GURL webstore_{extension_urls::kChromeWebstoreBaseURL};
  const GURL new_webstore_{extension_urls::kNewChromeWebstoreBaseURL};
  const GURL update_url_{extension_urls::kChromeWebstoreUpdateURL};
};

LabExtensionsClient::LabExtensionsClient() {
  SetFeatureDelegatedAvailabilityCheckMap(
      extensions::user_scripts_availability::CreateAvailabilityCheckMap());
  AddAPIProvider(std::make_unique<LabExtensionAPIProvider>());
}

const extensions::PermissionMessageProvider&
LabExtensionsClient::GetPermissionMessageProvider() const {
  NOTREACHED() << "Extension permission prompts are unavailable in the lab";
}

const std::string LabExtensionsClient::GetProductName() {
  return "Zitter";
}

void LabExtensionsClient::FilterHostPermissions(
    const extensions::URLPatternSet& hosts,
    extensions::URLPatternSet* new_hosts,
    extensions::PermissionIDSet*) const {
  for (const auto& host : hosts)
    new_hosts->AddPattern(host);
}

const LabExtensionsClient::ScriptingAllowlist&
LabExtensionsClient::GetScriptingAllowlist() const {
  return allowlist_;
}

extensions::URLPatternSet LabExtensionsClient::GetPermittedChromeSchemeHosts(
    const extensions::Extension*,
    const extensions::APIPermissionSet&) const {
  return {};
}

bool LabExtensionsClient::IsScriptableURL(const GURL& url,
                                           std::string* error) const {
  if (url.SchemeIsHTTPOrHTTPS() && !extension_urls::IsWebstoreDomain(url))
    return true;
  if (error)
    *error = "Zitter blocks extension scripting on this page.";
  return false;
}

bool IsFixtureDirectory(const base::FilePath& path) {
  base::FilePath temp;
  base::FilePath canonical_temp;
  base::FilePath canonical_path;
  return path.IsAbsolute() && !path.ReferencesParent() && !base::IsLink(path) &&
         base::GetTempDir(&temp) &&
         base::NormalizeFilePath(temp, &canonical_temp) &&
         base::NormalizeFilePath(path, &canonical_path) &&
         canonical_path.DirName() == canonical_temp &&
         canonical_path.BaseName().AsUTF8Unsafe().starts_with(
             "zitter-content-extension-") &&
         base::DirectoryExists(canonical_path) &&
         !base::IsLink(path.AppendASCII("manifest.json"));
}

bool VerifyPermissionFixture(const extensions::Extension& extension) {
  const auto& scripts =
      extensions::ContentScriptsInfo::GetContentScripts(&extension);
  if (scripts.size() != 1)
    return false;
  const auto& script = *scripts.front();
  const std::vector<std::string> expected_patterns{
      "http://127.0.0.1/extension-content/allowed",
      "https://chromewebstore.google.com/*",
  };
  const GURL store("https://chromewebstore.google.com/");
  const auto* permissions = extension.permissions_data();
  const auto can_script = [permissions](const GURL& url) {
    return permissions->CanRunContentScriptOnPage(url, -1, nullptr);
  };
  return extension.is_extension() && extension.manifest_version() == 3 &&
         !extensions::IncognitoInfo::IsIncognitoAllowed(&extension) &&
         extension.install_warnings().empty() &&
         script.run_location() == extensions::mojom::RunLocation::kDocumentStart &&
         script.execution_world() == extensions::mojom::ExecutionWorld::kIsolated &&
         !script.match_all_frames() &&
         script.match_origin_as_fallback() ==
             extensions::mojom::MatchOriginAsFallbackBehavior::kNever &&
         script.url_patterns().ToStringVector() == expected_patterns &&
         script.MatchesURL(store) &&
         extensions::BackgroundInfo::IsServiceWorkerBased(&extension) &&
         permissions->HasAPIPermission("storage") &&
         can_script(GURL("http://127.0.0.1/extension-content/allowed")) &&
         !can_script(GURL("http://127.0.0.1/extension-content/unmatched")) &&
         !can_script(GURL("https://example.invalid/fixture")) &&
         !can_script(GURL("file:///fixture")) &&
         !can_script(GURL("chrome://settings/")) &&
         !can_script(GURL("devtools://devtools/bundled/")) &&
         !can_script(store);
}

bool SupportsExtensionHostPattern(const URLPattern& pattern) {
  if (pattern.match_all_urls() || pattern.host().empty() || pattern.host() == "*")
    return false;
  const GURL base_url("https://" + pattern.host());
  if (!pattern.match_subdomains())
    return true;
  // Keep wildcard grants on a named domain. Include private registries so a
  // shared hosting suffix cannot be approved as though it were a single site.
  const bool allowed =
      !net::registry_controlled_domains::GetDomainAndRegistry(
          base_url, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES).empty() &&
      !pattern.MatchesHost(extension_urls::GetWebstoreLaunchURL()) &&
      !pattern.MatchesHost(extension_urls::GetNewWebstoreLaunchURL());
  if (!allowed)
    LOG(WARNING) << "Unsupported wildcard extension host: " << pattern.GetAsString();
  return allowed;
}

bool IsAllWebHosts(const URLPattern& pattern) {
  const auto& text = pattern.GetAsString();
  return text == "*://*/*" || text == "http://*/*" || text == "https://*/*";
}

bool IsSupportedExtensionApi(std::string_view api) {
  static constexpr auto kSupportedApis = base::MakeFixedFlatSet<std::string_view>({
#if BUILDFLAG(IS_MAC)
      "nativeMessaging", "notifications",
#endif
      "activeTab", "alarms", "clipboardRead", "clipboardWrite", "contextMenus", "cookies",
      "declarativeNetRequest",
      "fontSettings", "idle", "offscreen", "privacy", "scripting", "sidePanel",
      "storage", "tabs",
      "unlimitedStorage", "userScripts", "webNavigation", "webRequest",
      "webRequestAuthProvider",
  });
  return kSupportedApis.contains(api);
}

bool HasSupportedManifestPermissions(const base::DictValue& manifest) {
  const auto* features = extensions::FeatureProvider::GetPermissionFeatures();
  bool supported = true;
  for (const char* key : {"permissions", "optional_permissions"}) {
    const auto* declared = manifest.FindList(key);
    if (!declared)
      continue;

    extensions::APIPermissionSet permissions;
    std::u16string error;
    std::vector<std::string> unhandled;
    if (!extensions::APIPermissionSet::ParseFromJSON(
            *declared, extensions::APIPermissionSet::kDisallowInternalPermissions,
            &permissions, &error, &unhandled)) {
      LOG(ERROR) << "Invalid extension " << key << ": " << base::UTF16ToUTF8(error);
      return false;
    }
    for (const auto& permission : unhandled) {
      LOG(ERROR) << "Unsupported extension API in " << key << ": " << permission;
      supported = false;
    }
    for (const auto* permission : permissions) {
      if (!IsSupportedExtensionApi(permission->name()) ||
          !features->GetFeature(permission->name())) {
        LOG(ERROR) << "Unsupported extension API in " << key << ": " << permission->name();
        supported = false;
      }
    }
  }
  return supported;
}

bool SupportsExtensionManifest(const extensions::Extension& extension) {
  // Limit signed packages to capabilities implemented by this host. Chromium
  // also parses manifest features that require a Chrome-side browser service.
  static constexpr auto kSupportedKeys = base::MakeFixedFlatSet<std::string_view>({
      "action", "author", "background", "commands", "content_scripts", "content_security_policy",
      // Chromium adds current_locale when it localizes the signed manifest.
      "current_locale", "declarative_net_request", "default_locale", "description", "externally_connectable",
      "icons",
      "homepage_url", "host_permissions", "incognito", "key", "manifest_version",
      "minimum_chrome_version", "name",
      "optional_host_permissions", "optional_permissions", "options_page",
      "options_ui", "permissions", "sandbox",
      "short_name", "side_panel", "storage", "version", "version_name", "web_accessible_resources",
  });
  const auto& manifest = *extension.manifest()->value();
  if (!extension.is_extension() || extension.manifest_version() != 3 ||
      (manifest.contains("incognito") &&
       extensions::IncognitoInfo::IsIncognitoAllowed(&extension)) ||
      !extension.install_warnings().empty() ||
      !std::ranges::all_of(manifest, [](const auto& item) {
        return kSupportedKeys.contains(item.first);
      }) ||
      !extensions::BackgroundInfo::IsServiceWorkerBased(&extension)) {
    return false;
  }
  if (const auto* storage = manifest.Find("storage"); storage &&
      (!storage->is_dict() || storage->GetDict().size() != 1 ||
       !storage->GetDict().FindString("managed_schema"))) {
    return false;
  }
  if (const auto* panel = manifest.Find("side_panel"); panel) {
    const auto* path = panel->is_dict() ? panel->GetDict().FindString("default_path") : nullptr;
    if (!path || panel->GetDict().size() != 1 || !ExtensionArchiveResourcePath(*path) ||
        !extension.permissions_data()->HasAPIPermission(
            extensions::mojom::APIPermissionID::kSidePanel)) {
      return false;
    }
  }
  if (manifest.contains("action")) {
    const auto* action = extensions::ActionInfo::GetExtensionActionInfo(&extension);
    if (!action || action->default_state != extensions::ActionInfo::DefaultState::kEnabled ||
        !action->default_popup_url.is_valid() ||
        url::Origin::Create(action->default_popup_url) != extension.origin()) {
      return false;
    }
  }
  if (manifest.contains("options_page") || manifest.contains("options_ui")) {
    const auto& options = extensions::OptionsPageInfo::GetOptionsPage(&extension);
    if (!options.is_valid() || url::Origin::Create(options) != extension.origin())
      return false;
  }
  const auto* resources = extension.GetManifestData<extensions::WebAccessibleResourcesInfo>();
  if (resources && !std::ranges::all_of(resources->web_accessible_resources,
      [&](const auto& entry) {
        return !entry.allow_all_extensions &&
               entry.extension_ids.empty() && !entry.matches.is_empty() &&
               std::ranges::all_of(entry.matches, [&](const auto& pattern) {
                 if (pattern.match_all_urls())
                   return entry.use_dynamic_url;
                 if (extension.from_webstore() && IsAllWebHosts(pattern))
                   return true;
                 return !pattern.host().empty() &&
                        pattern.host() != "*" && pattern.path() == "/*" &&
                        (pattern.scheme() == "http" || pattern.scheme() == "https" ||
                         pattern.scheme() == "*");
               });
      })) {
    return false;
  }
  const auto* external = extensions::ExternallyConnectableInfo::Get(&extension);
  if (external && (!external->matches.is_empty() || external->accepts_tls_channel_id))
    return false;

  const auto supports_grants = [](const extensions::PermissionSet& permissions) {
    return std::ranges::all_of(permissions.explicit_hosts(), [](const auto& pattern) {
      if (IsSupportedExtensionHostGrant(pattern))
        return true;
      LOG(ERROR) << "Unsupported extension host grant: " << pattern.GetAsString();
      return false;
    }) &&
           std::ranges::all_of(permissions.GetAPIsAsStrings(), IsSupportedExtensionApi);
  };
  if (!supports_grants(extension.permissions_data()->active_permissions()) ||
      !supports_grants(extensions::PermissionsParser::GetOptionalPermissions(&extension))) {
    return false;
  }
  for (const auto& script : extensions::ContentScriptsInfo::GetContentScripts(&extension)) {
    if (!std::ranges::all_of(script->url_patterns(), [](const auto& pattern) {
          const bool inactive_file_pattern =
              pattern.GetAsString() == "file:///*" &&
              !(pattern.valid_schemes() & URLPattern::SCHEME_FILE);
          const bool supported = IsAllWebHosts(pattern) || inactive_file_pattern ||
                 (SupportsExtensionHostPattern(pattern) &&
                 (pattern.scheme() == "https" ||
                  ((pattern.scheme() == "http" || pattern.scheme() == "*") &&
                   !extension_urls::IsWebstoreDomain(GURL("https://" + pattern.host())))));
          if (!supported)
            LOG(ERROR) << "Unsupported extension content script pattern: " << pattern.GetAsString();
          return supported;
        })) {
      return false;
    }
  }
  return true;
}

constexpr char kActiveFixtureVersion[] = "active-version";
constexpr char kFixtureTrustRecord[] = "trusted-key-sha256";
constexpr char kUninstallUrlRecord[] = "uninstall-url";
constexpr char kDnrRecoveryMarker[] = "dnr-recovery";
constexpr std::string_view kDnrRecoveryMarkerValue = "1";
constexpr size_t kMaxUninstallUrlBytes = 1023;
constexpr char kUserEnabledState[] = "user-enabled";
constexpr std::string_view kUserDisabled = "disabled";
constexpr std::string_view kUserEnabled = "enabled";
constexpr size_t kMaxVersionBytes = 64;
constexpr int kFixtureTreeEntries =
    base::FileEnumerator::FILES | base::FileEnumerator::DIRECTORIES
#if BUILDFLAG(IS_POSIX)
    | base::FileEnumerator::SHOW_SYM_LINKS
#endif
    ;

struct SignedFixture {
  std::string archive;
  std::string public_key;
  std::string id;
  ExtractedExtensionFiles files;
  std::vector<uint8_t> key_hash;
  bool web_store = false;
};

bool IsFixtureLink(const base::FilePath& path) {
#if BUILDFLAG(IS_WIN)
  const DWORD attributes = ::GetFileAttributesW(path.value().c_str());
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
#else
  return base::IsLink(path);
#endif
}

bool IsCanonicalDirectory(const base::FilePath& path) {
  base::FilePath canonical;
  return !IsFixtureLink(path) && base::DirectoryExists(path) &&
         base::NormalizeFilePath(path, &canonical) && canonical == path;
}

bool IsFixtureFile(const base::FilePath& path,
                   size_t max_size,
                   bool allow_empty = false) {
  base::File::Info info;
  base::FilePath canonical;
  return !IsFixtureLink(path) && base::GetFileInfo(path, &info) &&
         !info.is_directory && info.size >= (allow_empty ? 0 : 1) &&
         static_cast<uint64_t>(info.size) <= max_size &&
         base::NormalizeFilePath(path, &canonical) && canonical == path;
}

base::FilePath UninstallUrlRecordPath(const base::FilePath& profile,
                                      const extensions::Extension& extension) {
  const auto installations =
      profile.AppendASCII(extensions::kInstallDirectoryName);
  const auto id_dir = installations.AppendASCII(extension.id());
  if (extension.path().DirName() != id_dir || !IsCanonicalDirectory(profile) ||
      !IsCanonicalDirectory(installations) || !IsCanonicalDirectory(id_dir) ||
      !IsCanonicalDirectory(extension.path())) {
    return {};
  }
  return id_dir.AppendASCII(kUninstallUrlRecord);
}

GURL ParseUninstallUrl(std::string_view value) {
  if (value.size() > kMaxUninstallUrlBytes)
    return {};
  GURL url(value);
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS() || url.has_username() ||
      url.has_password() || url.spec().size() > kMaxUninstallUrlBytes) {
    return {};
  }
  return url;
}

std::optional<bool> ReadExtensionUserState(const base::FilePath& path) {
  // The default applies only when the record is actually absent.
#if BUILDFLAG(IS_WIN)
  if (::GetFileAttributesW(path.value().c_str()) == INVALID_FILE_ATTRIBUTES) {
    return ::GetLastError() == ERROR_FILE_NOT_FOUND ? std::optional(true)
                                                    : std::nullopt;
  }
#else
  struct stat info;
  if (lstat(path.value().c_str(), &info) != 0)
    return errno == ENOENT ? std::optional(true) : std::nullopt;
  if (!S_ISREG(info.st_mode))
    return std::nullopt;
#endif
  constexpr size_t max_bytes = kUserDisabled.size();
  std::string contents;
  if (!IsFixtureFile(path, max_bytes) ||
      !base::ReadFileToStringWithMaxSize(path, &contents, max_bytes)) {
    return std::nullopt;
  }
  if (contents == kUserEnabled)
    return true;
  if (contents == kUserDisabled)
    return false;
  return std::nullopt;
}

std::optional<SignedFixture> VerifySignedFixture(
    const base::FilePath& path,
    const std::vector<uint8_t>& trusted_key_hash,
    std::string_view expected_store_id = {}) {
  SignedFixture fixture;
  if (!IsFixtureFile(path, kMaxFixtureArchiveSize) ||
      !base::ReadFileToStringWithMaxSize(path, &fixture.archive,
                                        kMaxFixtureArchiveSize)) {
    return std::nullopt;
  }
  // Pin the file hash to the buffered bytes that will be used for extraction.
  const auto file_hash = crypto::hash::Sha256(fixture.archive);
  std::vector<std::vector<uint8_t>> required_keys;
  if (!trusted_key_hash.empty())
    required_keys.push_back(trusted_key_hash);
  const auto verify = [&](crx_file::VerifierFormat format) {
    return crx_file::Verify(path, format, required_keys,
                            {file_hash.begin(), file_hash.end()}, &fixture.public_key,
                            &fixture.id, nullptr) == crx_file::VerifierResult::OK_FULL;
  };
  fixture.web_store = verify(crx_file::VerifierFormat::CRX3_WITH_PUBLISHER_PROOF);
  if (!expected_store_id.empty()) {
    if (!fixture.web_store || fixture.id != expected_store_id)
      return std::nullopt;
  } else if (!fixture.web_store && !verify(crx_file::VerifierFormat::CRX3)) {
    return std::nullopt;
  }
  // The trusted proof must be the developer key associated with this CRX ID.
  std::string public_key_bytes;
  if (!base::Base64Decode(fixture.public_key, &public_key_bytes))
    return std::nullopt;
  const auto key_hash = crypto::hash::Sha256(public_key_bytes);
  fixture.key_hash.assign(key_hash.begin(), key_hash.end());
  if (!trusted_key_hash.empty() && fixture.key_hash != trusted_key_hash)
    return std::nullopt;
  return fixture;
}

std::optional<SignedFixture> VerifyFixtureArchive(
    const base::FilePath& path,
    const std::vector<uint8_t>& trusted_key_hash) {
  auto fixture = VerifySignedFixture(path, trusted_key_hash);
  if (!fixture)
    return std::nullopt;
  auto files = ExtractExtensionFixture(fixture->archive, fixture->web_store);
  if (!files)
    return std::nullopt;
  fixture->files = std::move(*files);
  return fixture;
}

bool MatchesSignedIdentity(const extensions::Extension& extension,
                           const SignedFixture& fixture) {
  return extension.id() == fixture.id &&
         extension.public_key() == fixture.public_key;
}

scoped_refptr<extensions::Extension> LoadFixture(const base::FilePath& path,
                                                int flags,
                                                const SignedFixture* fixture = nullptr) {
  std::string manifest_error;
  auto manifest = extensions::file_util::LoadManifest(path, &manifest_error);
  constexpr auto location = extensions::mojom::ManifestLocation::kUnpacked;
  if (!manifest || !extension_l10n_util::LocalizeExtension(
                       path, &*manifest,
                       extension_l10n_util::GetGzippedMessagesPermissionForLocation(location),
                       &manifest_error)) {
    LOG(ERROR) << "Extension manifest rejected: " << manifest_error;
    return nullptr;
  }

  if (const auto* minimum = manifest->Find("minimum_chrome_version")) {
    const auto* text = minimum->GetIfString();
    const base::Version version(text ? *text : std::string());
    if (!version.IsValid() || version > version_info::GetVersion()) {
      LOG(ERROR) << "Extension manifest has an invalid or unsupported minimum_chrome_version";
      return nullptr;
    }
  }

  if (fixture && fixture->web_store) {
    if (!manifest->contains("key"))
      manifest->Set("key", fixture->public_key);
    if (const auto* update = manifest->FindString("update_url");
        update && GURL(*update) == GURL(extension_urls::kChromeWebstoreUpdateURL)) {
      manifest->Remove("update_url");
    }
    // This embedder has no extension keyword provider.
    manifest->Remove("omnibox");
    flags |= extensions::Extension::FROM_WEBSTORE;
  }

  // Content never grants extensions access to private profiles. Keep
  // publisher packages usable in regular profiles even when they advertise
  // an incognito mode. The signed manifest on disk retains the original mode.
  if (const auto* mode = manifest->FindString("incognito");
      mode && (*mode == "split" || *mode == "spanning")) {
    manifest->Set("incognito", "not_allowed");
  }

  // Chromium's <all_urls> also covers non-web schemes. Compile broad grants
  // as HTTP(S) match patterns without changing the signed archive on disk.
  const auto restrict_to_web = [](base::ListValue* patterns) {
    if (!patterns)
      return;
    for (auto& pattern : *patterns) {
      if (pattern.is_string() && pattern.GetString() == "<all_urls>")
        pattern = base::Value("*://*/*");
    }
  };
  restrict_to_web(manifest->FindList("host_permissions"));
  if (auto* scripts = manifest->FindList("content_scripts")) {
    for (auto& script : *scripts) {
      if (auto* entry = script.GetIfDict())
        restrict_to_web(entry->FindList("matches"));
    }
  }

  // The core permission registry includes APIs whose permission features
  // are defined by Chrome. Reject these before Chromium's manifest parser
  // assumes the feature is present.
  if (!HasSupportedManifestPermissions(*manifest)) {
    LOG(ERROR) << "Extension manifest uses capabilities unsupported by the Content host";
    return nullptr;
  }
  if (const auto* sandbox = manifest->Find("sandbox")) {
    const auto* pages = sandbox->is_dict() && sandbox->GetDict().size() == 1
                            ? sandbox->GetDict().FindList("pages")
                            : nullptr;
    if (!pages || pages->empty() || !std::ranges::all_of(*pages, [&](const auto& entry) {
          const auto* name = entry.GetIfString();
          const auto resource = name ? ExtensionArchiveResourcePath(*name) : std::nullopt;
          return resource && name->find('*') == std::string::npos &&
                 IsFixtureFile(path.Append(*resource), kMaxFixtureFileSize, /*allow_empty=*/true);
        })) {
      LOG(ERROR) << "Extension sandbox.pages must name packaged files";
      return nullptr;
    }
  }

  std::u16string error;
  auto extension = extensions::Extension::Create(path, location, *manifest, flags, &error);
  std::vector<extensions::InstallWarning> warnings;
  if (!extension || !extensions::file_util::ValidateExtension(extension.get(), &error, &warnings)) {
    LOG(ERROR) << "Extension manifest rejected: " << base::UTF16ToUTF8(error);
    return nullptr;
  }
  extension->AddInstallWarnings(std::move(warnings));
  if (!SupportsExtensionManifest(*extension)) {
    LOG(ERROR) << "Extension manifest uses capabilities unsupported by the Content host";
    return nullptr;
  }

  return extension;
}

base::FilePath ReadSingleFixtureDirectory(const base::FilePath& parent) {
  if (!IsCanonicalDirectory(parent))
    return {};
  base::FileEnumerator children(parent, /*recursive=*/false,
                                kFixtureTreeEntries);
  base::FilePath directory;
  for (auto child = children.Next(); !child.empty(); child = children.Next()) {
    if (child.BaseName() == base::FilePath::FromASCII(kUserEnabledState) ||
        child.BaseName() == base::FilePath::FromASCII(kFixtureTrustRecord) ||
        child.BaseName() == base::FilePath::FromASCII(kExtensionOptionalGrants) ||
        child.BaseName() == base::FilePath::FromASCII(kUninstallUrlRecord) ||
        child.BaseName() == base::FilePath::FromASCII(kDnrRecoveryMarker))
      continue;
    if (!directory.empty() || !IsCanonicalDirectory(child))
      return {};
    directory = std::move(child);
  }
  return directory;
}

// HFS+ enumerates decomposed Unicode filenames even when the signed archive
// uses composed spellings. Compare canonical Unicode without changing the
// actual paths used for file validation and reads.
std::optional<base::FilePath> ExtensionTreeKey(const base::FilePath& path) {
#if BUILDFLAG(IS_MAC)
  std::string normalized;
  if (!base::ConvertToUtf8AndNormalize(path.value(), "UTF-8", &normalized))
    return std::nullopt;
  return base::FilePath(std::move(normalized));
#else
  return path;
#endif
}

scoped_refptr<extensions::Extension> ReadExtractedFixtureVersion(
    const base::FilePath& version_dir, const SignedFixture& fixture) {
  if (!IsCanonicalDirectory(version_dir))
    return nullptr;
  std::map<base::FilePath, std::string_view> expected_files;
  std::map<base::FilePath, base::FilePath> expected_dirs;
  for (const auto& [name, contents] : fixture.files) {
    const auto relative = ExtensionArchiveResourcePath(name);
    if (!relative)
      return nullptr;
    const auto path = version_dir.Append(*relative);
    const auto key = ExtensionTreeKey(path);
    if (!key || !expected_files.emplace(*key, contents).second)
      return nullptr;
    for (auto parent = path.DirName(); parent != version_dir;
         parent = parent.DirName()) {
      const auto parent_key = ExtensionTreeKey(parent);
      if (!parent_key)
        return nullptr;
      const auto entry = expected_dirs.try_emplace(*parent_key, parent).first;
      if (entry->second != parent)
        return nullptr;
    }
  }

  std::set<base::FilePath> generated_files;
  const auto index_dir = extensions::file_util::GetIndexedRulesetDirectoryRelativePath();
  for (int id = 1;
       id <= extensions::api::declarative_net_request::MAX_NUMBER_OF_STATIC_RULESETS;
       ++id) {
    generated_files.insert(version_dir.Append(
        extensions::file_util::GetIndexedRulesetRelativePath(id)));
  }

  bool has_generated_index = false;
  base::FileEnumerator installed(version_dir, /*recursive=*/true,
                                 kFixtureTreeEntries);
  for (auto path = installed.Next(); !path.empty(); path = installed.Next()) {
    if (path == version_dir.AppendASCII(kExtensionArchiveFileName))
      continue;
    if (path == version_dir.Append(index_dir) ||
        path == version_dir.Append(index_dir.DirName())) {
      has_generated_index = true;
      if (!IsCanonicalDirectory(path))
        return nullptr;
      continue;
    }
    if (generated_files.contains(path)) {
      has_generated_index = true;
      if (!IsFixtureFile(path, 128 * 1024 * 1024))
        return nullptr;
      continue;
    }
    const auto key = ExtensionTreeKey(path);
    if (!key)
      return nullptr;
    if (expected_dirs.erase(*key)) {
      if (!IsCanonicalDirectory(path))
        return nullptr;
      continue;
    }
#if BUILDFLAG(IS_POSIX)
    if (!S_ISREG(installed.GetInfo().stat().st_mode))
      return nullptr;
#endif
    const auto expected = expected_files.find(*key);
    std::string bytes;
    if (expected == expected_files.end() ||
        !IsFixtureFile(path, kMaxFixtureFileSize, /*allow_empty=*/true) ||
        !base::ReadFileToStringWithMaxSize(path, &bytes, kMaxFixtureFileSize) ||
        bytes != expected->second) {
      return nullptr;
    }
    expected_files.erase(expected);
  }
  if (!expected_files.empty() || !expected_dirs.empty())
    return nullptr;
  // The unpacked manifest validator warns about any generated metadata
  // directory. Discard the verified cache structure before loading the signed
  // manifest. Activation rebuilds it from the authenticated JSON.
  if (has_generated_index &&
      !base::DeletePathRecursively(version_dir.Append(index_dir.DirName()))) {
    return nullptr;
  }

  auto extension = LoadFixture(version_dir, extensions::Extension::REQUIRE_KEY, &fixture);
  if (!extension || !MatchesSignedIdentity(*extension, fixture) ||
      (has_generated_index &&
       !extensions::declarative_net_request::HasAnyDNRPermission(*extension)) ||
      version_dir.DirName().BaseName() != base::FilePath::FromASCII(fixture.id) ||
      version_dir.BaseName() !=
          base::FilePath::FromASCII(extension->version().GetString() + "_0")) {
    return nullptr;
  }
  return extension;
}

scoped_refptr<extensions::Extension> ReadFixtureVersion(
    const base::FilePath& version_dir,
    const std::vector<uint8_t>& trusted_key_hash) {
  if (!IsCanonicalDirectory(version_dir))
    return nullptr;
  auto fixture = VerifyFixtureArchive(
      version_dir.AppendASCII(kExtensionArchiveFileName), trusted_key_hash);
  return fixture ? ReadExtractedFixtureVersion(version_dir, *fixture) : nullptr;
}

scoped_refptr<extensions::Extension> ReadInstalledFixture(
    const base::FilePath& extensions_dir,
    const std::vector<uint8_t>& trusted_key_hash) {
  if (!IsCanonicalDirectory(extensions_dir))
    return nullptr;
  const auto id_dir = extensions_dir.AppendASCII(
      crx_file::id_util::GenerateIdFromHash(trusted_key_hash));
  if (!IsCanonicalDirectory(id_dir))
    return nullptr;
  const auto active_path = id_dir.AppendASCII(kActiveFixtureVersion);
  if (!base::PathExists(active_path) && !base::IsLink(active_path)) {
    return ReadFixtureVersion(ReadSingleFixtureDirectory(id_dir),
                              trusted_key_hash);
  }

  std::string version;
  if (!IsFixtureFile(active_path, kMaxVersionBytes) ||
      !base::ReadFileToStringWithMaxSize(active_path, &version,
                                       kMaxVersionBytes)) {
    return nullptr;
  }
  const base::Version parsed(version);
  if (!parsed.IsValid() || parsed.GetString() != version)
    return nullptr;
  return ReadFixtureVersion(id_dir.AppendASCII(version + "_0"), trusted_key_hash);
}

bool AllowsFixtureUpdate(const extensions::Extension& current,
                         const extensions::Extension& candidate) {
  if (current.id() != candidate.id() || candidate.version() <= current.version() ||
      !current.permissions_data()->active_permissions().Contains(
          candidate.permissions_data()->active_permissions()) ||
      !extensions::PermissionsParser::GetOptionalPermissions(&current).Contains(
          extensions::PermissionsParser::GetOptionalPermissions(&candidate))) {
    return false;
  }
  if (extensions::CSPInfo::GetExtensionPagesCSP(&current) !=
      extensions::CSPInfo::GetExtensionPagesCSP(&candidate)) {
    return false;
  }
  const auto* old_sandbox = current.manifest()->value()->FindDict("sandbox");
  const auto* new_sandbox = candidate.manifest()->value()->FindDict("sandbox");
  if ((old_sandbox || new_sandbox) &&
      (!old_sandbox || !new_sandbox || *old_sandbox != *new_sandbox ||
       extensions::CSPInfo::GetSandboxContentSecurityPolicy(&current) !=
           extensions::CSPInfo::GetSandboxContentSecurityPolicy(&candidate))) {
    return false;
  }
  const auto* old_commands = current.manifest()->value()->FindDict("commands");
  const auto* new_commands = candidate.manifest()->value()->FindDict("commands");
  if (new_commands && (!old_commands || *new_commands != *old_commands))
    return false;
  const auto* old_resources = current.manifest()->value()->FindList("web_accessible_resources");
  const auto* new_resources = candidate.manifest()->value()->FindList("web_accessible_resources");
  if (new_resources && !std::ranges::all_of(*new_resources, [&](const auto& entry) {
        return old_resources && old_resources->contains(entry.GetDict());
      })) {
    return false;
  }
  const auto* previous_senders = extensions::ExternallyConnectableInfo::Get(&current);
  const auto* next_senders = extensions::ExternallyConnectableInfo::Get(&candidate);
  if (previous_senders && !previous_senders->all_ids &&
      (!next_senders || next_senders->all_ids ||
       !std::ranges::all_of(next_senders->ids, [&](const auto& id) {
         return previous_senders->IdCanConnect(id);
       }))) {
    return false;
  }
  // The aggregate permission set does not express per-script exclusions
  // and glob filters. An update must keep those restrictions.
  const auto& installed_scripts = extensions::ContentScriptsInfo::GetContentScripts(&current);
  const auto& update_scripts = extensions::ContentScriptsInfo::GetContentScripts(&candidate);
  return std::ranges::all_of(update_scripts, [&](const auto& script) {
    return std::ranges::any_of(installed_scripts, [&](const auto& installed) {
      return installed->url_patterns().Contains(script->url_patterns()) &&
             installed->run_location() == script->run_location() &&
             installed->execution_world() == script->execution_world() &&
             (!script->match_all_frames() || installed->match_all_frames()) &&
             (script->match_origin_as_fallback() ==
                  extensions::mojom::MatchOriginAsFallbackBehavior::kNever ||
              installed->match_origin_as_fallback() ==
                  script->match_origin_as_fallback()) &&
             installed->exclude_url_patterns() == script->exclude_url_patterns() &&
             installed->globs() == script->globs() &&
             installed->exclude_globs() == script->exclude_globs();
    });
  });
}

constexpr char kLegacyFixtureRecord[] = "ContentLegacyExtension.json";
constexpr char kLegacyFixtureRecords[] = "ContentLegacyExtensions";
constexpr char kLegacyFixtureDigest[] = "legacy-import-sha256";
constexpr size_t kMaxLegacyRecordBytes = 16 * 1024;

bool MatchesLegacyStrings(const base::DictValue& record,
                          std::string_view key,
                          const std::set<std::string>& expected) {
  const auto* values = record.FindList(key);
  if (!values || values->size() != expected.size())
    return false;
  std::set<std::string> actual;
  for (const auto& value : *values) {
    if (!value.is_string())
      return false;
    actual.insert(value.GetString());
  }
  return actual == expected;
}

bool MatchesLegacyPermissions(const base::DictValue& record,
                              const extensions::PermissionSet& permissions) {
  const auto explicit_hosts = permissions.explicit_hosts().ToStringVector();
  const auto scriptable_hosts = permissions.scriptable_hosts().ToStringVector();
  return permissions.manifest_permissions().empty() &&
         MatchesLegacyStrings(record, "api", permissions.GetAPIsAsStrings()) &&
         MatchesLegacyStrings(record, "manifest_permissions", {}) &&
         MatchesLegacyStrings(record, "explicit_host",
                              {explicit_hosts.begin(), explicit_hosts.end()}) &&
         MatchesLegacyStrings(record, "scriptable_host",
                              {scriptable_hosts.begin(), scriptable_hosts.end()});
}

bool MatchesLegacyFixture(const base::DictValue& record,
                          const extensions::Extension& extension) {
  const auto* id = record.FindString("extension_id");
  const auto* active = record.FindDict("active_permissions");
  const auto* granted = record.FindDict("granted_permissions");
  const auto* reasons = record.FindList("disable_reasons");
  if (!id || *id != extension.id() || !active || !granted || !reasons ||
      record.FindBool("withholding_permissions") != false ||
      !extensions::PermissionsParser::GetOptionalPermissions(&extension)
           .IsEmpty()) {
    return false;
  }
  const auto* external = extensions::ExternallyConnectableInfo::Get(&extension);
  if (record.FindInt("version") == 2) {
    if (!external)
      return false;
    const std::set<std::string> senders =
        external->all_ids ? std::set<std::string>{"*"}
                          : std::set<std::string>{external->ids.begin(), external->ids.end()};
    if (!MatchesLegacyStrings(record, "external_extension_ids", senders))
      return false;
  } else if (external || record.contains("external_extension_ids")) {
    // Version 1 records did not attest an external messaging policy.
    return false;
  }
  for (const auto& reason : *reasons) {
    if (!reason.is_int() || reason.GetInt() <= 0)
      return false;
  }
  const auto& permissions = extension.permissions_data()->active_permissions();
  return MatchesLegacyPermissions(*active, permissions) &&
         MatchesLegacyPermissions(*granted, permissions);
}

base::ListValue ReviewStrings(const auto& strings) {
  base::ListValue result;
  for (const auto& value : strings)
    result.Append(value);
  return result;
}

base::DictValue BuildInstallReview(const extensions::Extension& extension,
                        const base::FilePath& profile,
                        const std::vector<uint8_t>& key_hash,
                        std::string_view archive) {
  const auto& permissions = extension.permissions_data()->active_permissions();
  base::ListValue scripts;
  for (const auto& script :
       extensions::ContentScriptsInfo::GetContentScripts(&extension)) {
    base::DictValue entry;
    const auto timing = script->run_location();
    entry.Set("run_at", timing == extensions::mojom::RunLocation::kDocumentStart
                            ? "document_start"
                            : timing == extensions::mojom::RunLocation::kDocumentEnd
                                  ? "document_end" : "document_idle");
    entry.Set("all_frames", script->match_all_frames());
    entry.Set("world", script->execution_world() == extensions::mojom::ExecutionWorld::kMain
                           ? "MAIN" : "ISOLATED");
    entry.Set("match_about_blank",
              script->match_origin_as_fallback() ==
                  extensions::mojom::MatchOriginAsFallbackBehavior::
                      kMatchForAboutSchemeAndClimbTree);
    entry.Set("match_origin_as_fallback",
              script->match_origin_as_fallback() ==
                  extensions::mojom::MatchOriginAsFallbackBehavior::kAlways);
    entry.Set("matches", ReviewStrings(script->url_patterns().ToStringVector()));
    entry.Set("exclude_matches",
              ReviewStrings(script->exclude_url_patterns().ToStringVector()));
    entry.Set("include_globs", ReviewStrings(script->globs()));
    entry.Set("exclude_globs", ReviewStrings(script->exclude_globs()));
    scripts.Append(std::move(entry));
  }

  base::DictValue details;
  details.Set("review_version", 7);
  if (const auto* commands = extension.manifest()->value()->FindDict("commands"))
    details.Set("commands", commands->Clone());

  if (extension.manifest()->value()->contains("content_security_policy")) {
    details.Set("extension_pages_csp",
                extensions::CSPInfo::GetExtensionPagesCSP(&extension));
  }

  details.Set("name", extension.name());
  details.Set("version", extension.version().GetString());
  details.Set("id", extension.id());
  details.Set("profile", profile.AsUTF8Unsafe());
  details.Set("key_sha256", base::HexEncode(key_hash));
  details.Set("archive_sha256", base::HexEncode(crypto::hash::Sha256(archive)));
  details.Set("api_permissions", ReviewStrings(permissions.GetAPIsAsStrings()));
  if (extension.wants_file_access())
    details.Set("file_access_blocked", true);
  if (const auto* path = extension.manifest()->FindStringPath("side_panel.default_path"))
    details.Set("side_panel_path", *path);
  if (const auto* sandbox = extension.manifest()->value()->FindDict("sandbox")) {
    details.Set("sandbox_pages", sandbox->FindList("pages")->Clone());
    details.Set("sandbox_csp", extensions::CSPInfo::GetSandboxContentSecurityPolicy(&extension));
  }
  details.Set("host_permissions",
              ReviewStrings(permissions.explicit_hosts().ToStringVector()));
  const auto& optional =
      extensions::PermissionsParser::GetOptionalPermissions(&extension);
  details.Set("optional_api_permissions", ReviewStrings(optional.GetAPIsAsStrings()));
  details.Set("optional_host_permissions",
              ReviewStrings(optional.explicit_hosts().ToStringVector()));
  // Chromium's worker origin allowlist includes declarative script hosts and
  // treats those patterns as origins, independently of their script paths.
  extensions::URLPatternSet network_hosts;
  for (auto pattern : permissions.effective_hosts()) {
    if (pattern.scheme() == "file" ||
        extension_urls::IsWebstoreDomain(GURL("https://" + pattern.host())))
      continue;
    pattern.SetPath("/*");
    network_hosts.AddPattern(pattern);
  }
  details.Set("network_hosts", ReviewStrings(network_hosts.ToStringVector()));
  const auto* external = extensions::ExternallyConnectableInfo::Get(&extension);
  details.Set("external_extension_ids",
              !external || external->all_ids
                  ? ReviewStrings(std::array<std::string_view, 1>{"*"})
                  : ReviewStrings(external->ids));
  details.Set("content_scripts", std::move(scripts));
  details.Set("background_service_worker", true);
  if (extensions::OptionsPageInfo::HasOptionsPage(&extension))
    details.Set("options_page", extensions::OptionsPageInfo::GetOptionsPage(&extension).path());
  if (extension.manifest()->value()->contains("action"))
    details.Set("action_popup",
                extensions::ActionInfo::GetExtensionActionInfo(&extension)->default_popup_url.path());
  const auto* resources = extension.GetManifestData<extensions::WebAccessibleResourcesInfo>();
  if (resources) {
    base::ListValue entries;
    for (const auto& entry : resources->web_accessible_resources) {
      base::ListValue paths;
      for (const auto& resource : entry.resources)
        paths.Append(resource.path());
      entries.Append(base::DictValue()
                         .Set("resources", std::move(paths))
                         .Set("matches", ReviewStrings(entry.matches.ToStringVector()))
                         .Set("use_dynamic_url", entry.use_dynamic_url));
    }
    details.Set("web_accessible_resources", std::move(entries));
  }
  return details;
}

bool CheckInstallReview(const extensions::Extension& extension,
                        const base::FilePath& profile,
                        const std::vector<uint8_t>& key_hash,
                        std::string_view archive,
                        std::string_view review) {
  auto details = BuildInstallReview(extension, profile, key_hash, archive);
  const auto serialized = base::WriteJson(details);
  if (!serialized)
    return false;
  const std::string approval = base::HexEncode(crypto::hash::Sha256(*serialized));
  if (review != "preview") {
    if (approval == review)
      return true;
    LOG(ERROR) << "Content extension review does not match this package and profile";
    return false;
  }
  details.Set("approval", approval);
  const auto preview = base::WriteJson(details);
  if (!preview)
    return false;
  std::printf("Zitter Content extension review: %s\n", preview->c_str());
  return true;
}

bool WriteFixturePackage(const SignedFixture& fixture,
                         const base::FilePath& package) {
  if (!base::CreateDirectory(package))
    return false;
  for (const auto& [name, contents] : fixture.files) {
    const auto relative = ExtensionArchiveResourcePath(name);
    if (!relative)
      return false;
    const auto path = package.Append(*relative);
    if (!base::CreateDirectory(path.DirName()))
      return false;
    base::File file(path, base::File::FLAG_CREATE | base::File::FLAG_WRITE);
    if (!file.IsValid() ||
        !file.WriteAtCurrentPosAndCheck(base::as_byte_span(contents))) {
      return false;
    }
  }
  if (!base::WriteFile(package.AppendASCII(kExtensionArchiveFileName),
                       fixture.archive)) {
    return false;
  }

  return true;
}

bool PublishNewFixture(const base::FilePath& staged_version,
                       const base::FilePath& destination,
                       std::string_view id,
                       std::string_view version,
                       const std::vector<uint8_t>& key_hash) {
  const auto destination_id = destination.AppendASCII(id);
  const bool existing = base::PathExists(destination) || IsFixtureLink(destination);
  if ((existing && !IsCanonicalDirectory(destination)) ||
      base::PathExists(destination_id) || IsFixtureLink(destination_id)) {
    return false;
  }
  const auto staged_id = staged_version.DirName();
  return base::ImportantFileWriter::WriteFileAtomically(
             staged_id.AppendASCII(kActiveFixtureVersion), version) &&
         base::ImportantFileWriter::WriteFileAtomically(
             staged_id.AppendASCII(kFixtureTrustRecord), base::HexEncode(key_hash)) &&
         (existing ? base::Move(staged_id, destination_id)
                   : base::Move(staged_id.DirName(), destination));
}

std::optional<std::vector<uint8_t>> ReadFixtureTrust(const base::FilePath& path) {
  constexpr size_t kHexBytes = 2 * crypto::hash::kSha256Size;
  std::string hex;
  std::vector<uint8_t> hash;
  if (!IsFixtureFile(path, kHexBytes) ||
      !base::ReadFileToStringWithMaxSize(path, &hex, kHexBytes) ||
      hex.size() != kHexBytes || !base::HexStringToBytes(hex, &hash)) {
    return std::nullopt;
  }
  return hash;
}

base::FilePath PendingUpdatePath(const base::FilePath& version_path) {
  return version_path.DirName().AppendASCII(
      "pending-update-" + version_path.BaseName().AsUTF8Unsafe());
}

bool PublishFixtureUpdate(const base::FilePath& staged_version,
                          const extensions::Extension& current,
                          const SignedFixture& fixture,
                          std::string_view version) {
  const auto installed_id = current.path().DirName();
  const auto active_path = installed_id.AppendASCII(kActiveFixtureVersion);
  if (!base::PathExists(active_path) &&
      !base::ImportantFileWriter::WriteFileAtomically(
          active_path, current.version().GetString())) {
    return false;
  }

  const auto installed_version = installed_id.Append(staged_version.BaseName());
  if (base::PathExists(installed_version) || base::IsLink(installed_version)) {
    // Retry a publish interrupted after the version move but before activation.
    std::string existing_archive;
    const auto archive_path = installed_version.AppendASCII(kExtensionArchiveFileName);
    if (!IsCanonicalDirectory(installed_version) ||
        !IsFixtureFile(archive_path, kMaxFixtureArchiveSize) ||
        !base::ReadFileToStringWithMaxSize(archive_path, &existing_archive,
                                          kMaxFixtureArchiveSize) ||
        existing_archive != fixture.archive ||
        !ReadExtractedFixtureVersion(installed_version, fixture)) {
      return false;
    }
  } else if (!base::Move(staged_version, installed_version)) {
    return false;
  }
  // A failed pointer write leaves the previous signed version active.
  // The pending event is bound to the new version directory.
  return base::ImportantFileWriter::WriteFileAtomically(
             PendingUpdatePath(installed_version), current.version().GetString()) &&
         base::ImportantFileWriter::WriteFileAtomically(active_path, version);
}

struct ReviewedFixture {
  SignedFixture fixture;
  scoped_refptr<const extensions::Extension> extension;
  base::DictValue details;
  scoped_refptr<const extensions::Extension> current;
};

bool MatchesActiveFixture(const base::FilePath& profile,
                          const SignedFixture& fixture,
                          const extensions::Extension& current) {
  const auto destination = profile.AppendASCII(extensions::kInstallDirectoryName);
  const auto id_dir = destination.AppendASCII(fixture.id);
  const auto active_path = id_dir.AppendASCII(kActiveFixtureVersion);
  std::string version;
  return current.path().DirName() == id_dir &&
         IsCanonicalDirectory(profile) && IsCanonicalDirectory(destination) &&
         IsCanonicalDirectory(id_dir) && IsCanonicalDirectory(current.path()) &&
         ReadFixtureTrust(id_dir.AppendASCII(kFixtureTrustRecord)) == fixture.key_hash &&
         IsFixtureFile(active_path, kMaxVersionBytes) &&
         base::ReadFileToStringWithMaxSize(active_path, &version, kMaxVersionBytes) &&
         version == current.version().GetString();
}

std::optional<base::DictValue> IndexVerifiedStaticRulesets(
    const extensions::Extension& extension) {
  const auto relative = extensions::file_util::GetIndexedRulesetDirectoryRelativePath();
  const auto directory = extension.path().Append(relative);
  const auto metadata = extension.path().Append(relative.DirName());
  if (!IsCanonicalDirectory(extension.path()) || IsFixtureLink(metadata) ||
      IsFixtureLink(directory) ||
      (base::PathExists(metadata) && !IsCanonicalDirectory(metadata)) ||
      (base::PathExists(directory) &&
       (!IsCanonicalDirectory(directory) || !base::DeletePathRecursively(directory)))) {
    return std::nullopt;
  }
  auto result = extensions::declarative_net_request::InstallIndexHelper::
      IndexAndPersistRulesOnInstall(extension);
  if (result.error) {
    LOG(ERROR) << "Could not index verified extension rules: " << *result.error;
    return std::nullopt;
  }
  return std::move(result.ruleset_install_prefs);
}


bool CreateExtensionReviewDirectory(base::ScopedTempDir& staging) {
  base::FilePath temporary;
  base::FilePath canonical;
  // macOS can return /var/folders via TMPDIR. Ruleset indexing checks that
  // the package is under a canonical path before writing generated data.
  return base::GetTempDir(&temporary) &&
         base::NormalizeFilePath(temporary, &canonical) &&
         staging.CreateUniqueTempDirUnderPath(canonical);
}

std::optional<ReviewedFixture> ReviewSelectedFixture(
    const base::FilePath& profile,
    SignedFixture fixture,
    scoped_refptr<const extensions::Extension> current) {
  base::ScopedTempDir staging;
  if (!CreateExtensionReviewDirectory(staging) ||
      !WriteFixturePackage(fixture, staging.GetPath())) {
    return std::nullopt;
  }
  const auto extension = LoadFixture(staging.GetPath(), extensions::Extension::REQUIRE_KEY, &fixture);
  if (!extension || !MatchesSignedIdentity(*extension, fixture) ||
      (extensions::declarative_net_request::HasAnyDNRPermission(*extension) &&
       !IndexVerifiedStaticRulesets(*extension))) {
    return std::nullopt;
  }
  auto details = BuildInstallReview(
      *extension, profile, fixture.key_hash, fixture.archive);
  const auto final_path = profile.AppendASCII(extensions::kInstallDirectoryName)
                              .AppendASCII(extension->id())
                              .AppendASCII(extension->version().GetString() + "_0");
  std::u16string error;
  auto installed = extensions::Extension::Create(
      final_path, extension->location(), *extension->manifest()->value(),
      extensions::Extension::REQUIRE_KEY |
          (fixture.web_store ? extensions::Extension::FROM_WEBSTORE : 0), &error);
  if (!installed || !MatchesSignedIdentity(*installed, fixture))
    return std::nullopt;
  if (current) {
    if ((current->from_webstore() && !fixture.web_store) ||
        !MatchesActiveFixture(profile, fixture, *current) ||
        installed->version() <= current->version()) {
      return std::nullopt;
    }
    const bool expanded = !AllowsFixtureUpdate(*current, *installed);
    const auto legacy_digest = current->path().DirName().AppendASCII(kLegacyFixtureDigest);
    if (expanded && (base::PathExists(legacy_digest) || IsFixtureLink(legacy_digest)))
      return std::nullopt;
    details.Set("previous_version", current->version().GetString());
    details.Set("scope_expanded", expanded);
    const auto& old_policy = extensions::CSPInfo::GetExtensionPagesCSP(current.get());
    const auto& new_policy = extensions::CSPInfo::GetExtensionPagesCSP(installed.get());
    if (old_policy != new_policy) {
      details.Set("previous_extension_pages_csp", old_policy);
      details.Set("extension_pages_csp", new_policy);
    }
  }
  return ReviewedFixture{std::move(fixture), std::move(installed),
                         std::move(details), std::move(current)};
}

bool PublishSelectedFixture(const base::FilePath& profile,
                            const ReviewedFixture& reviewed) {
  if (!IsCanonicalDirectory(profile))
    return false;
  const auto destination = profile.AppendASCII(extensions::kInstallDirectoryName);
  if (reviewed.current) {
    if (!MatchesActiveFixture(profile, reviewed.fixture, *reviewed.current))
      return false;
  } else {
    // The legacy importer is responsible for binding old storage and permissions.
    for (const char* directory : {kLegacyFixtureRecord, kLegacyFixtureRecords}) {
      const auto record = profile.AppendASCII(directory);
      if (base::PathExists(record) || IsFixtureLink(record))
        return false;
    }
    const auto store =
        profile.Append(extensions::kLocalExtensionSettingsDirectoryName)
            .AppendASCII(reviewed.fixture.id);
    if (base::PathExists(store) || IsFixtureLink(store))
      return false;
  }

  base::ScopedTempDir staging;
  if (!staging.CreateUniqueTempDirUnderPath(profile))
    return false;
  const std::string version_name = reviewed.extension->version().GetString();
  const auto version = staging.GetPath()
                           .AppendASCII(extensions::kInstallDirectoryName)
                           .AppendASCII(reviewed.fixture.id)
                           .Append(reviewed.extension->path().BaseName());
  if (!WriteFixturePackage(reviewed.fixture, version))
    return false;
  if (reviewed.current) {
    return PublishFixtureUpdate(version, *reviewed.current, reviewed.fixture,
                                version_name);
  }
  return PublishNewFixture(version, destination, reviewed.fixture.id,
                           version_name, reviewed.fixture.key_hash);
}

class SelectedExtensionInstall final : public WorkspaceExtensionInstall {
 public:
  SelectedExtensionInstall(const base::FilePath& source,
                           const base::FilePath& profile,
                           std::vector<scoped_refptr<const extensions::Extension>> installed,
                           Review review)
      : profile_(profile), installed_(std::move(installed)), review_(std::move(review)) {
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
        base::BindOnce(&VerifySignedFixture, source, std::vector<uint8_t>(), std::string()),
        base::BindOnce(&SelectedExtensionInstall::OnVerified,
                       weak_factory_.GetWeakPtr()));
  }

  SelectedExtensionInstall(
      network::mojom::URLLoaderFactory* factory, std::string id,
      std::string installed_version, const base::FilePath& profile,
      std::vector<scoped_refptr<const extensions::Extension>> installed,
      Review review)
      : profile_(profile), installed_(std::move(installed)), review_(std::move(review)) {
    auto request = std::make_unique<network::ResourceRequest>();
    auto url = GURL(extension_urls::kChromeWebstoreUpdateURL);
    url = net::AppendQueryParameter(url, "response", "redirect");
    url = net::AppendQueryParameter(url, "prodversion", version_info::GetVersionNumber());
    url = net::AppendQueryParameter(url, "acceptformat", "crx3");
    url = net::AppendQueryParameter(
        url, "x", "id=" + id + "&v=" + installed_version + "&uc");
    request->url = std::move(url);
    request->credentials_mode = network::mojom::CredentialsMode::kOmit;
    constexpr auto annotation = net::DefineNetworkTrafficAnnotation(
        "zitter_content_web_store_package", R"annotation(
          semantics {
            sender: "Zitter Extensions"
            description: "Downloads a user-selected Chrome Web Store extension or checks an installed store extension for updates."
            trigger: "The user selects a listing, or an installed store extension is due for an update check."
            data: "Extension ID, installed extension version, and Chromium version."
            destination: GOOGLE_OWNED_SERVICE
          }
          policy {
            cookies_allowed: NO
            setting: "Remove the extension to stop its update checks."
            policy_exception_justification: "Zitter is an independent browser."
          })annotation");
    loader_ = network::SimpleURLLoader::Create(std::move(request), annotation);
    loader_->SetTimeoutDuration(base::Minutes(2));
    loader_->SetOnRedirectCallback(base::BindRepeating(
        &SelectedExtensionInstall::OnStoreRedirect, weak_factory_.GetWeakPtr()));
    loader_->DownloadToTempFile(
        factory, base::BindOnce(&SelectedExtensionInstall::OnStoreDownloaded,
                                weak_factory_.GetWeakPtr(), std::move(id),
                                installed_version != "0.0.0.0"),
        kMaxFixtureArchiveSize);
  }

  scoped_refptr<const extensions::Extension> Publish() override {
    if (!reviewed_)
      return nullptr;
    auto candidate = std::exchange(reviewed_, std::nullopt);
    base::ScopedAllowBlockingForTesting allow_publish;
    if (!PublishSelectedFixture(profile_, *candidate))
      return nullptr;
    return std::move(candidate->extension);
  }

 private:
  void OnStoreRedirect(const GURL&, const net::RedirectInfo& redirect,
                       const network::mojom::URLResponseHead&,
                       std::vector<std::string>*) {
    const auto& next = redirect.new_url;
    if (next.SchemeIs("https") && !next.has_port() &&
        !next.has_username() && !next.has_password() &&
        (next.host() == "clients2.googleusercontent.com" ||
         next.host() == "clients2.google.com" ||
         next.host() == "chromewebstore.google.com") &&
        (next.path().starts_with("/crx/") || next.path() == "/service/update2/crx")) {
      return;
    }
    loader_.reset();
    std::move(review_).Run(std::nullopt);
  }

  void OnStoreDownloaded(std::string id, bool checking_update, base::FilePath path) {
    const auto* response = loader_->ResponseInfo();
    const bool up_to_date = checking_update && response && response->headers &&
                            response->headers->response_code() == 204;
    loader_.reset();
    if (up_to_date) {
      std::move(review_).Run(base::DictValue().Set("up_to_date", true));
      return;
    }
    if (path.empty()) {
      std::move(review_).Run(std::nullopt);
      return;
    }
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
                    base::TaskShutdownBehavior::BLOCK_SHUTDOWN},
        base::BindOnce([](base::FilePath file, std::string expected_id) {
          auto candidate = VerifySignedFixture(base::MakeAbsoluteFilePath(file), {}, expected_id);
          base::DeleteFile(file);
          return candidate;
        }, path, std::move(id)),
        base::BindOnce(&SelectedExtensionInstall::OnVerified, weak_factory_.GetWeakPtr()));
  }

  void OnVerified(std::optional<SignedFixture> fixture) {
    if (!fixture) {
      std::move(review_).Run(std::nullopt);
      return;
    }
    verified_ = std::move(fixture);
    extraction_ = ExtensionArchiveExtraction::Start(
        verified_->archive, verified_->web_store,
        base::BindOnce(&SelectedExtensionInstall::OnExtracted,
                       weak_factory_.GetWeakPtr()));
  }

  void OnExtracted(std::optional<ExtractedExtensionFiles> files) {
    extraction_.reset();
    auto fixture = std::exchange(verified_, std::nullopt);
    if (!files) {
      std::move(review_).Run(std::nullopt);
      return;
    }
    fixture->files = std::move(*files);
    const auto current = std::ranges::find(installed_, fixture->id,
                                            &extensions::Extension::id);
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
        base::BindOnce(&ReviewSelectedFixture, profile_, std::move(*fixture),
                       current == installed_.end() ? nullptr : *current),
        base::BindOnce(&SelectedExtensionInstall::OnReviewed,
                       weak_factory_.GetWeakPtr()));
  }

  void OnReviewed(std::optional<ReviewedFixture> reviewed) {
    reviewed_ = std::move(reviewed);
    std::move(review_).Run(reviewed_ ? std::optional(std::move(reviewed_->details))
                                    : std::nullopt);
  }

  const base::FilePath profile_;
  const std::vector<scoped_refptr<const extensions::Extension>> installed_;
  Review review_;
  std::unique_ptr<network::SimpleURLLoader> loader_;
  std::unique_ptr<ExtensionArchiveExtraction> extraction_;
  std::optional<SignedFixture> verified_;
  std::optional<ReviewedFixture> reviewed_;
  base::WeakPtrFactory<SelectedExtensionInstall> weak_factory_{this};
};

bool InstallWorkspaceFixture(const base::FilePath& source,
                             const base::FilePath& destination,
                             const std::vector<uint8_t>& trusted_key_hash,
                             const extensions::Extension* current,
                             const base::DictValue* legacy_record,
                             std::string_view legacy_digest,
                             std::string_view review = {}) {
  const auto destination_id = destination.AppendASCII(
      crx_file::id_util::GenerateIdFromHash(trusted_key_hash));
  const bool add_to_existing = base::PathExists(destination) || IsFixtureLink(destination);
  if (!IsFixtureDirectory(source.DirName()) ||
      (add_to_existing && !IsCanonicalDirectory(destination)) ||
      (!current && (base::PathExists(destination_id) || IsFixtureLink(destination_id)))) {
    return false;
  }
  auto fixture = VerifyFixtureArchive(source, trusted_key_hash);
  if (!fixture) {
    LOG(ERROR) << "Content extension CRX3 verification failed";
    return false;
  }
  base::ScopedTempDir staging;
  if (!(review == "preview"
            ? CreateExtensionReviewDirectory(staging)
            : staging.CreateUniqueTempDirUnderPath(destination.DirName()))) {
    return false;
  }

  const auto package = staging.GetPath().AppendASCII("package");
  if (!WriteFixturePackage(*fixture, package))
    return false;
  const auto extension = LoadFixture(package, extensions::Extension::REQUIRE_KEY, &*fixture);
  if (!extension || !MatchesSignedIdentity(*extension, *fixture) ||
      (extensions::declarative_net_request::HasAnyDNRPermission(*extension) &&
       !IndexVerifiedStaticRulesets(*extension))) {
    return false;
  }
  if (legacy_record && !MatchesLegacyFixture(*legacy_record, *extension)) {
    LOG(ERROR) << "Content extension permissions differ from the legacy record";
    return false;
  }
  if (current && !AllowsFixtureUpdate(*current, *extension)) {
    LOG(ERROR) << "Content extension update must be newer and retain its permissions";
    return false;
  }

  if (!review.empty()) {
    if (!CheckInstallReview(*extension, destination.DirName(), trusted_key_hash,
                            fixture->archive, review)) {
      return false;
    }
    if (review == "preview")
      return true;
  }

  const auto staged_install = staging.GetPath().AppendASCII(
      extensions::kInstallDirectoryName);
  const std::string version = extension->version().GetString();
  extensions::file_util::SetUseSafeInstallation(true);
  const auto staged_version = extensions::file_util::InstallExtension(
      package, extension->id(), version, staged_install);
  if (staged_version.empty() ||
      !base::DeleteFile(staged_install.Append(
          extensions::file_util::kTempDirectoryName)) ||
      !ReadFixtureVersion(staged_version, trusted_key_hash)) {
    return false;
  }

  if (!current) {
    const auto staged_id = staged_version.DirName();
    if (legacy_record && !base::ImportantFileWriter::WriteFileAtomically(
                             staged_id.AppendASCII(kLegacyFixtureDigest),
                             legacy_digest)) {
      return false;
    }
    return PublishNewFixture(staged_version, destination, extension->id(),
                             version, trusted_key_hash);
  }

  return PublishFixtureUpdate(staged_version, *current, *fixture, version);
}

}  // namespace

std::unique_ptr<WorkspaceExtensionInstall> WorkspaceExtensionInstall::Start(
    const base::FilePath& source,
    const base::FilePath& profile,
    std::vector<scoped_refptr<const extensions::Extension>> installed,
    Review review) {
  return std::make_unique<SelectedExtensionInstall>(source, profile, std::move(installed),
                                                   std::move(review));
}

std::unique_ptr<WorkspaceExtensionInstall> WorkspaceExtensionInstall::FromWebStore(
    network::mojom::URLLoaderFactory* factory, std::string id,
    std::string installed_version, const base::FilePath& profile,
    std::vector<scoped_refptr<const extensions::Extension>> installed,
    Review review) {
  return std::make_unique<SelectedExtensionInstall>(
      factory, std::move(id), std::move(installed_version), profile,
      std::move(installed), std::move(review));
}

void InitializeLabExtensionsClient() {
  static base::NoDestructor<LabExtensionsClient> client;
  extensions::ExtensionsClient::Set(client.get());
}

std::optional<ExtensionFixtureStateAction> GetExtensionFixtureStateAction() {
  const auto& command = *base::CommandLine::ForCurrentProcess();
  if (!command.HasSwitch(kExtensionFixtureState))
    return ExtensionFixtureStateAction::kNone;
  const auto action = command.GetSwitchValueASCII(kExtensionFixtureState);
  if (action == "disable")
    return ExtensionFixtureStateAction::kDisable;
  if (action == "pause-after-disable-commit")
    return ExtensionFixtureStateAction::kPauseAfterDisableCommit;
  if (action == "check-disabled")
    return ExtensionFixtureStateAction::kCheckDisabled;
  if (action == "enable")
    return ExtensionFixtureStateAction::kEnable;
  if (action == "manage")
    return ExtensionFixtureStateAction::kManage;
  if (action == "manage-smoke")
    return ExtensionFixtureStateAction::kManageSmoke;
  return std::nullopt;
}

bool IsSupportedExtensionHostGrant(const URLPattern& pattern) {
  return IsAllWebHosts(pattern) || (SupportsExtensionHostPattern(pattern) &&
         (pattern.scheme() == "http" || pattern.scheme() == "https" ||
          pattern.scheme() == "*") &&
         pattern.port() == "*" && pattern.path() == "/*" &&
         !extension_urls::IsWebstoreDomain(GURL("https://" + pattern.host())));
}

bool IsManagedExtensionSession() {
  const auto action = GetExtensionFixtureStateAction();
  return base::CommandLine::ForCurrentProcess()->HasSwitch(kRestoreWorkspaceExtensions) ||
         action == ExtensionFixtureStateAction::kManage ||
         action == ExtensionFixtureStateAction::kManageSmoke;
}

bool IsWorkspaceExtensionRuntime() {
  const auto& command = *base::CommandLine::ForCurrentProcess();
  return command.HasSwitch(kExtensionRegistryProbe) ||
         command.HasSwitch(kRestoreWorkspaceExtensions);
}

std::optional<WorkspaceExtensionRuleIndexes> PrepareVerifiedExtensionRuleIndexes(
    const extensions::Extension& extension,
    const extensions::declarative_net_request::FileBackedRulesetSource& dynamic_rules,
    bool allow_legacy_dynamic_rules) {
  using extensions::declarative_net_request::GetMaximumRulesetFileSize;
  using extensions::declarative_net_request::IndexAndPersistJSONRulesetResult;
  using extensions::declarative_net_request::RulesetSource;
  auto static_rules = IndexVerifiedStaticRulesets(extension);
  if (!static_rules)
    return std::nullopt;

  const auto install_dir = extension.path().DirName();
  const auto marker = install_dir.AppendASCII(kDnrRecoveryMarker);
  const auto& json = dynamic_rules.json_path();
  const auto& index = dynamic_rules.indexed_path();
  const auto directory = json.DirName();
  const auto parent = directory.DirName();
  if (!IsCanonicalDirectory(install_dir) || IsFixtureLink(marker) ||
      IsFixtureLink(parent) || IsFixtureLink(directory) || IsFixtureLink(json) ||
      IsFixtureLink(index) ||
      (base::PathExists(parent) && !IsCanonicalDirectory(parent)) ||
      (base::PathExists(directory) && !IsCanonicalDirectory(directory))) {
    return std::nullopt;
  }

  const bool has_marker = base::PathExists(marker);
  if (has_marker) {
    std::string value;
    if (!IsFixtureFile(marker, kDnrRecoveryMarkerValue.size()) ||
        !base::ReadFileToStringWithMaxSize(marker, &value,
                                          kDnrRecoveryMarkerValue.size()) ||
        value != kDnrRecoveryMarkerValue) {
      return std::nullopt;
    }
  } else if (!allow_legacy_dynamic_rules && base::PathExists(directory) &&
             !base::DeletePathRecursively(directory)) {
    return std::nullopt;
  }

  std::optional<int> dynamic_checksum;
  if (base::PathExists(json)) {
    if (!IsFixtureFile(json, GetMaximumRulesetFileSize()))
      return std::nullopt;
    // JSON is the source of truth. Rebuild even if the cached index and
    // preference agree, since a crash may have separated their writes.
    if (base::PathExists(index) && !base::DeleteFile(index))
      return std::nullopt;
    auto result = dynamic_rules.IndexAndPersistJSONRuleset(
        RulesetSource::kRaiseErrorOnInvalidRules |
        RulesetSource::kRaiseWarningOnLargeRegexRules);
    if (result.status == IndexAndPersistJSONRulesetResult::Status::kSuccess)
      dynamic_checksum = result.ruleset_checksum;
    else
      LOG(WARNING) << "Could not recover Content extension dynamic rules";
  }

  if (!has_marker && !base::ImportantFileWriter::WriteFileAtomically(
                         marker, kDnrRecoveryMarkerValue)) {
    return std::nullopt;
  }
  return WorkspaceExtensionRuleIndexes{
      std::move(*static_rules), dynamic_checksum, has_marker};
}

bool PersistExtensionFixtureEnabled(const base::FilePath& version_path,
                                    bool enabled) {
  base::ScopedAllowBlockingForTesting allow_fixture_io;
  const auto id_path = version_path.DirName();
  const auto state_path = id_path.AppendASCII(kUserEnabledState);
  return IsCanonicalDirectory(id_path) &&
         ReadExtensionUserState(state_path).has_value() &&
         base::ImportantFileWriter::WriteFileAtomically(
             state_path, enabled ? kUserEnabled : kUserDisabled);
}

bool SetWorkspaceExtensionUninstallUrl(const base::FilePath& profile,
                                      const extensions::Extension& extension,
                                      std::string_view value) {
  const GURL url = ParseUninstallUrl(value);
  if (!value.empty() && !url.is_valid())
    return false;
  base::ScopedAllowBlockingForTesting allow_fixture_io;
  const auto path = UninstallUrlRecordPath(profile, extension);
  if (path.empty() || IsFixtureLink(path) ||
      (base::PathExists(path) &&
       !IsFixtureFile(path, kMaxUninstallUrlBytes, /*allow_empty=*/true))) {
    return false;
  }
  return base::ImportantFileWriter::WriteFileAtomically(path, url.spec());
}

GURL ReadWorkspaceExtensionUninstallUrl(
    const base::FilePath& profile,
    const extensions::Extension& extension) {
  base::ScopedAllowBlockingForTesting allow_fixture_io;
  const auto path = UninstallUrlRecordPath(profile, extension);
  std::string value;
  if (path.empty() || !IsFixtureFile(path, kMaxUninstallUrlBytes) ||
      !base::ReadFileToStringWithMaxSize(path, &value, kMaxUninstallUrlBytes)) {
    return {};
  }
  return ParseUninstallUrl(value);
}

std::optional<std::string> ReadPendingExtensionUpdate(
    const extensions::Extension& extension) {
  base::ScopedAllowBlockingForTesting allow_fixture_io;
  const auto record = PendingUpdatePath(extension.path());
  if (!base::PathExists(record) && !IsFixtureLink(record))
    return std::nullopt;
  std::string previous;
  if (IsFixtureFile(record, kMaxVersionBytes) &&
      base::ReadFileToStringWithMaxSize(record, &previous, kMaxVersionBytes)) {
    const base::Version parsed(previous);
    if (parsed.IsValid() && parsed < extension.version() &&
        parsed.GetString() == previous) {
      return previous;
    }
  }
  LOG(WARNING) << "Invalid Content extension update event record";
  return std::nullopt;
}

void ClearPendingExtensionUpdate(const extensions::Extension& extension) {
  base::ScopedAllowBlockingForTesting allow_fixture_io;
  if (!base::DeleteFile(PendingUpdatePath(extension.path())))
    LOG(WARNING) << "Could not clear the Content extension update event record";
}

bool RemoveDisabledExtensionInstallation(const base::FilePath& profile,
                                         const base::FilePath& version_path) {
  const auto directory = version_path.DirName();
  const auto extensions = profile.AppendASCII(extensions::kInstallDirectoryName);
  if (directory.DirName() != extensions ||
      !crx_file::id_util::IdIsValid(directory.BaseName().AsUTF8Unsafe()) ||
      !IsCanonicalDirectory(extensions) || !IsCanonicalDirectory(directory) ||
      ReadExtensionUserState(directory.AppendASCII(kUserEnabledState)) != false) {
    return false;
  }
  base::ScopedTempDir retired;
  return retired.CreateUniqueTempDirUnderPath(profile) &&
         base::ReplaceFile(directory, retired.GetPath().Append(directory.BaseName()),
                           nullptr);
}

std::optional<int> ConfigureExtensionProbes() {
  RegisterExtensionsManagerWebUI();
  const auto& command_line = *base::CommandLine::ForCurrentProcess();
  if (!command_line.HasSwitch(kExtensionManifestProbe))
    return std::nullopt;
  const auto path = command_line.GetSwitchValuePath(kExtensionManifestProbe);
  if (!IsFixtureDirectory(path)) {
    LOG(ERROR) << "Extension manifest probe requires a temporary fixture";
    return 1;
  }
  InitializeLabExtensionsClient();
  const auto extension = LoadFixture(path, extensions::Extension::NO_FLAGS);
  if (!extension)
    return 1;
  if (!VerifyPermissionFixture(*extension)) {
    LOG(ERROR) << "Extension manifest did not match the permission fixture";
    return 1;
  }
  std::puts("Zitter Content extension manifest and host permissions passed");
  return 0;
}

std::optional<size_t> ExtensionFixtureProfileIndex() {
  const auto& command_line = *base::CommandLine::ForCurrentProcess();
  if (!command_line.HasSwitch(kExtensionFixtureProfile))
    return 0;
  const auto profile = command_line.GetSwitchValueASCII(kExtensionFixtureProfile);
  if (profile == "work")
    return 0;
  if (profile == "personal")
    return 1;
  return std::nullopt;
}

namespace {


std::optional<ExtensionProbeFixture> PrepareProfileExtension(
    const base::FilePath& profile,
    const base::FilePath& source,
    const base::FilePath& update,
    const std::vector<uint8_t>& trusted_key_hash,
    std::string_view review = {}) {
  if (!IsCanonicalDirectory(profile))
    return std::nullopt;

  std::optional<base::DictValue> legacy_record;
  std::string legacy_digest;
  auto legacy_path = profile.AppendASCII(kLegacyFixtureRecord);
  const bool has_single_record =
      base::PathExists(legacy_path) || IsFixtureLink(legacy_path);
  const auto records = profile.AppendASCII(kLegacyFixtureRecords);
  const bool has_indexed_records =
      base::PathExists(records) || IsFixtureLink(records);
  if (has_indexed_records) {
    if (has_single_record || !IsCanonicalDirectory(records)) {
      LOG(ERROR) << "Invalid Content legacy extension records directory";
      return std::nullopt;
    }
    legacy_path = records.AppendASCII(
        crx_file::id_util::GenerateIdFromHash(trusted_key_hash) + ".json");
  }
  if (has_single_record || has_indexed_records) {
    std::string json;
    if (!IsFixtureFile(legacy_path, kMaxLegacyRecordBytes) ||
        !base::ReadFileToStringWithMaxSize(legacy_path, &json,
                                          kMaxLegacyRecordBytes)) {
      LOG(ERROR) << "Could not read Content legacy extension record";
      return std::nullopt;
    }
    legacy_record = base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
    legacy_digest = base::HexEncode(crypto::hash::Sha256(json));
    const auto version = legacy_record ? legacy_record->FindInt("version")
                                       : std::nullopt;
    if (version != 1 && version != 2) {
      LOG(ERROR) << "Invalid Content legacy extension record";
      return std::nullopt;
    }
  }
  const auto* imported = legacy_record ? &*legacy_record : nullptr;
  const auto destination = profile.AppendASCII(
      extensions::kInstallDirectoryName);
  const auto id = crx_file::id_util::GenerateIdFromHash(trusted_key_hash);
  const auto installed = destination.AppendASCII(id);
  const auto trust_path = installed.AppendASCII(kFixtureTrustRecord);
  const bool has_trust = base::PathExists(trust_path) || IsFixtureLink(trust_path);
  if (has_trust && ReadFixtureTrust(trust_path) != trusted_key_hash) {
    LOG(ERROR) << "Invalid Content extension trust record";
    return std::nullopt;
  }
  if (!source.empty() && !imported) {
    const auto store = profile.Append(extensions::kLocalExtensionSettingsDirectoryName)
                           .AppendASCII(id);
    if (!base::PathExists(installed) && !IsFixtureLink(installed) &&
        (base::PathExists(store) || IsFixtureLink(store))) {
      LOG(ERROR) << "A pre-existing extension store requires its legacy permissions record";
      return std::nullopt;
    }
  }
  if (!source.empty() &&
      !InstallWorkspaceFixture(source, destination, trusted_key_hash, nullptr,
                               imported, legacy_digest, review)) {
    LOG(ERROR) << "Could not install Content extension fixture";
    return std::nullopt;
  }

  auto extension = ReadInstalledFixture(destination, trusted_key_hash);
  if (!extension) {
    LOG(ERROR) << "Could not restore Content extension fixture";
    return std::nullopt;
  }
  const auto digest_path =
      extension->path().DirName().AppendASCII(kLegacyFixtureDigest);
  if (base::PathExists(digest_path) || base::IsLink(digest_path) || imported) {
    std::string stored_digest;
    if (!imported || !IsFixtureFile(digest_path, legacy_digest.size()) ||
        !base::ReadFileToStringWithMaxSize(digest_path, &stored_digest,
                                          legacy_digest.size()) ||
        stored_digest != legacy_digest) {
      LOG(ERROR) << "Content legacy extension record is missing or changed";
      return std::nullopt;
    }
  }
  if (imported && !MatchesLegacyFixture(*imported, *extension)) {
    LOG(ERROR) << "Content extension permissions differ from the legacy record";
    return std::nullopt;
  }
  const auto user_enabled = ReadExtensionUserState(
      extension->path().DirName().AppendASCII(kUserEnabledState));
  if (!user_enabled) {
    LOG(ERROR) << "Invalid Content extension enablement record";
    return std::nullopt;
  }
  if (!update.empty()) {
    if (!InstallWorkspaceFixture(update, destination, trusted_key_hash,
                                 extension.get(), imported, legacy_digest)) {
      LOG(ERROR) << "Could not update Content extension fixture";
      return std::nullopt;
    }
    extension = ReadInstalledFixture(destination, trusted_key_hash);
    if (!extension)
      return std::nullopt;
    std::printf("Zitter Content updated its extension fixture to %s\n",
                extension->version().GetString().c_str());
  } else {
    std::puts(source.empty() ? "Zitter Content restored its extension fixture"
                            : "Zitter Content installed its extension fixture");
  }
  if (source.empty() && !has_trust &&
      !base::ImportantFileWriter::WriteFileAtomically(
          trust_path, base::HexEncode(trusted_key_hash))) {
    LOG(ERROR) << "Could not record Content extension trust";
    return std::nullopt;
  }
  const bool legacy_disabled =
      imported && !imported->FindList("disable_reasons")->empty();
  if (imported)
    std::puts("Zitter Content verified the legacy extension grants");
  return ExtensionProbeFixture{
      std::move(extension), legacy_disabled || !*user_enabled, legacy_disabled};
}

bool ReadTrustedProfileExtensions(
    const base::FilePath& profile,
    std::multimap<base::FilePath, ExtensionProbeFixture>& fixtures,
    bool skip_invalid) {
  const auto install_root = profile.AppendASCII(extensions::kInstallDirectoryName);
  if (!base::PathExists(install_root) && !IsFixtureLink(install_root))
    return true;
  if (!IsCanonicalDirectory(install_root))
    return false;
  base::FileEnumerator entries(install_root, /*recursive=*/false,
                               kFixtureTreeEntries, {},
                               base::FileEnumerator::FolderSearchPolicy::MATCH_ONLY,
                               base::FileEnumerator::ErrorPolicy::STOP_ENUMERATION);
  for (auto path = entries.Next(); !path.empty(); path = entries.Next()) {
    const auto id = path.BaseName().AsUTF8Unsafe();
    if (!crx_file::id_util::IdIsValid(id))
      continue;
    if (!IsCanonicalDirectory(path)) {
      LOG(ERROR) << "Invalid Content extension directory: " << id;
      if (!skip_invalid)
        return false;
      continue;
    }
    const auto trust = path.AppendASCII(kFixtureTrustRecord);
    if (!base::PathExists(trust) && !IsFixtureLink(trust))
      continue;
    const auto hash = ReadFixtureTrust(trust);
    if (!hash || crx_file::id_util::GenerateIdFromHash(*hash) != id) {
      LOG(ERROR) << "Invalid Content extension trust record";
      if (!skip_invalid)
        return false;
      continue;
    }
    auto fixture = PrepareProfileExtension(profile, {}, {}, *hash);
    if (!fixture) {
      if (!skip_invalid)
        return false;
      continue;
    }
    fixtures.emplace(profile, std::move(*fixture));
  }
  return entries.GetError() == base::File::FILE_OK;
}

}  // namespace

void RestoreApprovedWorkspaceExtensions(base::span<const base::FilePath> profiles) {
  InitializeLabExtensionsClient();
  std::multimap<base::FilePath, ExtensionProbeFixture> fixtures;
  for (const auto& profile : profiles) {
    if (!ReadTrustedProfileExtensions(profile, fixtures, /*skip_invalid=*/true))
      LOG(ERROR) << "Could not enumerate all trusted Content extensions";
  }
  PrepareExtensionRegistryProbe(std::move(fixtures));
}

bool IsWorkspaceExtensionReviewOnly() {
  return base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
             kExtensionInstallReview) == "preview";
}

bool ConfigureWorkspaceExtensionProbe(base::span<const base::FilePath> profiles) {
  const auto& command_line = *base::CommandLine::ForCurrentProcess();
  if (!IsWorkspaceExtensionRuntime())
    return true;
  if (profiles.size() < 2)
    return false;
  if (command_line.HasSwitch(kRestoreWorkspaceExtensions)) {
    InitializeLabExtensionsClient();
    std::multimap<base::FilePath, ExtensionProbeFixture> fixtures;
    for (const auto& profile : profiles) {
      if (!ReadTrustedProfileExtensions(profile, fixtures, /*skip_invalid=*/false)) {
        LOG(ERROR) << "Could not restore trusted Content extensions";
        return false;
      }
    }
    PrepareExtensionRegistryProbe(std::move(fixtures));
    return true;
  }
  const auto index = ExtensionFixtureProfileIndex();
  if (!index)
    return false;
  const auto& profile = profiles[*index];

  std::vector<uint8_t> trusted_key_hash;
  if (!base::HexStringToBytes(
          command_line.GetSwitchValueASCII(kExtensionFixtureKeyHash),
          &trusted_key_hash) ||
      trusted_key_hash.size() != crypto::hash::kSha256Size) {
    LOG(ERROR) << "Content extension fixture requires a trusted SHA-256 key hash";
    return false;
  }
  std::vector<uint8_t> sibling_key_hash;
  if (command_line.HasSwitch(kExtensionFixtureSibling) &&
      (!base::HexStringToBytes(
           command_line.GetSwitchValueASCII(kExtensionFixtureSiblingKeyHash),
           &sibling_key_hash) ||
       sibling_key_hash.size() != crypto::hash::kSha256Size ||
       crx_file::id_util::GenerateIdFromHash(sibling_key_hash) ==
           crx_file::id_util::GenerateIdFromHash(trusted_key_hash))) {
    LOG(ERROR) << "Content sibling extensions require distinct trusted IDs";
    return false;
  }

  const auto source = command_line.GetSwitchValuePath(kExtensionRegistryProbe);
  const auto update = command_line.GetSwitchValuePath(kExtensionFixtureUpdate);
  if (command_line.HasSwitch(kExtensionFixtureUpdate) &&
      (update.empty() || !source.empty())) {
    LOG(ERROR) << "Content extension update requires one candidate archive";
    return false;
  }

  InitializeLabExtensionsClient();
  const auto review = command_line.GetSwitchValueASCII(kExtensionInstallReview);
  if (IsWorkspaceExtensionReviewOnly()) {
    return IsCanonicalDirectory(profile) &&
           InstallWorkspaceFixture(
               source, profile.AppendASCII(extensions::kInstallDirectoryName),
               trusted_key_hash, nullptr, nullptr, {}, review);
  }
  auto fixture =
      PrepareProfileExtension(profile, source, update, trusted_key_hash, review);
  if (!fixture)
    return false;
  const auto action = GetExtensionFixtureStateAction();
  if (((action == ExtensionFixtureStateAction::kDisable ||
        action == ExtensionFixtureStateAction::kPauseAfterDisableCommit) &&
       fixture->keep_disabled) ||
      ((action == ExtensionFixtureStateAction::kEnable ||
        action == ExtensionFixtureStateAction::kCheckDisabled) &&
       (!fixture->keep_disabled || fixture->legacy_disabled))) {
    LOG(ERROR) << "Unexpected Content extension enablement for the requested action";
    return false;
  }

  std::multimap<base::FilePath, ExtensionProbeFixture> fixtures;
  fixtures.emplace(profile, std::move(*fixture));
  if (command_line.HasSwitch(kExtensionFixturePeer)) {
    const auto& peer = profiles[1 - *index];
    auto peer_fixture = PrepareProfileExtension(
        peer, command_line.GetSwitchValuePath(kExtensionFixturePeer), {},
        trusted_key_hash);
    if (!peer_fixture ||
        (!IsManagedExtensionSession() &&
         (fixtures.begin()->second.keep_disabled || peer_fixture->keep_disabled))) {
      LOG(ERROR) << "The paired extension probe requires two enabled fixtures";
      return false;
    }
    fixtures.emplace(peer, std::move(*peer_fixture));
  }
  if (!sibling_key_hash.empty()) {
    auto sibling = PrepareProfileExtension(
        profile, command_line.GetSwitchValuePath(kExtensionFixtureSibling), {},
        sibling_key_hash);
    if (!sibling)
      return false;
    fixtures.emplace(profile, std::move(*sibling));
  }
  PrepareExtensionRegistryProbe(std::move(fixtures));
  return true;
}

}  // namespace zitter::content_embedder
