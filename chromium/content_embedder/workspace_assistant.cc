// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "workspace_lab.h"

#include <algorithm>
#include <array>
#include <ranges>
#include <cmath>
#include <cstdio>

#include "base/auto_reset.h"
#include "base/base64.h"
#include "base/environment.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/uuid.h"
#include "chromium/assistant/native_cli_path.h"
#include "chromium/prototype/adblock.rs.h"
#include "chromium/prototype/adblock_browser_support.h"
#include "chromium/prototype/assistant_connection.h"
#include "chromium/prototype/i18n.h"
#include "chromium/prototype/assistant_panel.h"
#include "chromium/prototype/assistant_page_tools.h"
#include "chromium/prototype/assistant_visuals.h"
#include "chromium/prototype/devtools_console_reader.h"
#include "chromium/prototype/editor_control.h"
#include "chromium/prototype/page_dialogs.h"
#include "chromium/prototype/workspace_session.h"
#include "chromium/prototype/workspace_shell.h"
#include "components/download/public/common/download_item.h"
#include "components/history/core/browser/history_service.h"
#include "components/os_crypt/async/browser/os_crypt_async.h"
#include "components/os_crypt/async/common/encryptor.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/weak_document_ptr.h"
#include "content/public/common/isolated_world_ids.h"
#include "content/common/content_export.h"
#include "content/public/common/referrer.h"
#include "extension_registry_probe.h"
#include "extensions/browser/api/storage/storage_area_namespace.h"
#include "extensions/browser/api/storage/storage_frontend.h"
#include "extensions/browser/extension_api_frame_id_map.h"
#include "extensions/common/extension.h"
#include "night_shift_site.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "net/base/net_errors.h"
#include "profile_context.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/events/event_constants.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/widget/widget.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_provider.h"
#include "zitter_content/adblock_probe.mojom.h"

namespace content {

CONTENT_EXPORT void ZitterBeforeDocumentCommit(
    NavigationHandle& navigation,
    base::OnceCallback<void(const blink::DocumentToken&)> callback);

}  // namespace content

namespace zitter::content_embedder {
namespace {

constexpr char kSessionsFile[] = "ZitterAssistantSessions.json";
constexpr size_t kMaxSessionBytes = 64 * 1024 * 1024;
constexpr char kProbeAnswer[] = "Content Assistant round trip passed";
constexpr char kProviderProbeKey[] = "zitter-content-openai-fixture";

constexpr size_t kOpenAI = 0;
constexpr size_t kCodex = 1;
constexpr size_t kClaude = 2;
constexpr size_t kOllama = 3;
constexpr std::array<size_t, 4> kProviderDisplayOrder = {kOllama, kOpenAI, kCodex, kClaude};
constexpr std::string_view kOllamaProvider = "ollama";

bool ValidAssistantModel(std::string_view id) {
  auto word = [](char c) { return base::IsAsciiAlphaNumeric(c) || c == '_'; };
  return !id.empty() && id.size() <= 120 && word(id.front()) &&
         std::ranges::all_of(id.substr(1), [&](char c) {
           return word(c) || std::string_view(".:/@+-").contains(c);
         });
}

bool AddAssistantModel(prototype::AssistantSettings& settings,
                       std::string_view provider, std::string id) {
  if (!ValidAssistantModel(id) || settings.models.size() >= 32 ||
      std::ranges::any_of(settings.models, [&](const auto& model) {
        return model.provider == provider && model.id == id;
      })) {
    return false;
  }
  if (settings.default_model.empty())
    settings.default_model = base::StrCat({provider, "/", id});
  if (provider == kOllamaProvider)
    settings.ollama_configured = true;
  settings.models.push_back({std::string(provider), std::move(id), true});
  return true;
}

bool ActivateAssistantProbeControl(views::View* view) {
  if (!view || !view->GetEnabled())
    return false;
  view->GetWidget()->LayoutRootViewIfNecessary();
  view->ScrollViewToVisible();
  ui::AXActionData action;
  action.action = ax::mojom::Action::kDoDefault;
  return view->IsDrawn() && view->HandleAccessibleAction(action);
}

base::Value AssistantToolError(std::string_view message) {
  return base::Value(base::DictValue().Set("error", message));
}

std::string AssistantUrl(const GURL& url) {
  if (!url.SchemeIsHTTPOrHTTPS())
    return {};
  GURL::Replacements strip;
  strip.ClearUsername();
  strip.ClearPassword();
  strip.ClearQuery();
  strip.ClearRef();
  return url.ReplaceComponents(strip).spec();
}

bool CanOpenAssistantPin(const prototype::PinnedItem& pin) {
  return !pin.folder && pin.url.SchemeIsHTTPOrHTTPS() &&
         !pin.url.has_username() && !pin.url.has_password();
}

std::string AssistantPinMention(std::string_view workspace,
                                const prototype::PinnedItem& pin) {
  return base::StrCat({"@pin:", pin.profile_pin ? "shared" : workspace, "/", pin.id});
}

}  // namespace

// Reload() does not return a navigation handle. Observe the request itself
// because Chromium can reuse the previous NavigationEntry for the new page.
class AssistantReloadNavigation final : public content::WebContentsObserver {
 public:
  AssistantReloadNavigation(content::WebContents* page,
                            content::ReloadType reload_type,
                            base::OnceCallback<bool(content::NavigationHandle&)> prepare = {})
      : WebContentsObserver(page), reload_type_(reload_type), prepare_(std::move(prepare)) {}

  void DidStartNavigation(content::NavigationHandle* navigation) override {
    if (!navigation->IsInPrimaryMainFrame())
      return;
    if (id_ || navigation->IsRendererInitiated() ||
        navigation->GetReloadType() != reload_type_) {
      failed = true;
      return;
    }
    id_ = navigation->GetNavigationId();
  }

  void ReadyToCommitNavigation(content::NavigationHandle* navigation) override {
    if (!failed && prepare_ && id_ == navigation->GetNavigationId())
      failed = !std::move(prepare_).Run(*navigation);
  }

  void DidFinishNavigation(content::NavigationHandle* navigation) override {
    if (id_ != navigation->GetNavigationId())
      return;
    if (!navigation->HasCommitted() || navigation->IsErrorPage() ||
        navigation->IsSameDocument()) {
      failed = true;
      return;
    }
    document = navigation->GetRenderFrameHost()->GetWeakDocumentPtr();
  }

  bool started() const { return id_.has_value(); }

  bool failed = false;
  std::optional<content::WeakDocumentPtr> document;

 private:
  const content::ReloadType reload_type_;
  base::OnceCallback<bool(content::NavigationHandle&)> prepare_;
  std::optional<int64_t> id_;
};

bool WorkspaceLab::CanConfigureAssistant() const {
  return !smoke_ && !private_owner_ && !session_path_.empty();
}

void WorkspaceLab::UpdateAssistantHostSettings() {
  const bool was_enabled = assistant_host_settings_.enabled;
  assistant_host_settings_ =
      assistant_settings_.value_or(prototype::AssistantSettings());
  assistant_host_settings_.available = assistant_provider_available_;
  assistant_host_settings_.available[kOllama] =
      assistant_settings_ && assistant_settings_->ollama_configured;
  const bool enabled = CanConfigureAssistant() && assistant_settings_ &&
      assistant_settings_->enabled && !assistant_host_settings_.PanelModels().empty();
  assistant_host_settings_.enabled = enabled;
  assistant_host_settings_.background = false;
  shell_->SetAssistantAllowed(enabled);
  if (assistant_) {
    if (was_enabled && !enabled)
      assistant_->CredentialsChanged();
    assistant_->SettingsChanged();
  }
}

void WorkspaceLab::ProbeAssistantProviders() {
  if (!CanConfigureAssistant() || !assistant_settings_ || !assistant_settings_->enabled)
    return;
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce([](std::string codex_path, std::string claude_path) {
        auto env = base::Environment::Create();
        return std::array{
            !env->GetVar("OPENAI_API_KEY").value_or("").empty() ||
                !env->GetVar("ZITTER_OPENAI_API_KEY").value_or("").empty(),
            !assistant::NativeCommand("ZITTER_CODEX_PATH", "codex", codex_path).empty(),
            !assistant::NativeCommand("ZITTER_CLAUDE_PATH", "claude", claude_path).empty()};
      }, assistant_settings_->codex_path, assistant_settings_->claude_path),
      base::BindOnce([](base::WeakPtr<WorkspaceLab> lab, uint64_t generation,
                        std::array<bool, 3> detected) {
        if (!lab || generation != lab->assistant_provider_generation_)
          return;
        detected[kOpenAI] |= !lab->assistant_settings_->encrypted_api_key.empty();
        for (size_t i = 0; i < detected.size(); ++i)
          lab->assistant_provider_available_[i] = detected[i];
        lab->UpdateAssistantHostSettings();
        if (lab->assistant_)
          lab->assistant_->BackendAvailabilityChanged();
        lab->shell_->RefreshAssistantPreferences();
      }, weak_factory_.GetWeakPtr(), assistant_provider_generation_));
}

void WorkspaceLab::StoreAssistantSettings(prototype::AssistantSettings settings) {
  ++assistant_provider_generation_;
  ClearAssistantAdblockPreview();
  assistant_page_approval_.reset();
  assistant_provider_checking_.fill(false);
  const bool credentials_changed = !assistant_settings_ ||
      assistant_settings_->encrypted_api_key != settings.encrypted_api_key ||
      assistant_settings_->codex_path != settings.codex_path ||
      assistant_settings_->claude_path != settings.claude_path;
  const bool backend_changed = assistant_settings_ &&
      assistant_settings_->backends != settings.backends;
  if (!assistant_settings_ || assistant_settings_->encrypted_api_key != settings.encrypted_api_key)
    assistant_provider_available_[kOpenAI] = false;
  if (!assistant_settings_ || assistant_settings_->codex_path != settings.codex_path)
    assistant_provider_available_[kCodex] = false;
  if (!assistant_settings_ || assistant_settings_->claude_path != settings.claude_path)
    assistant_provider_available_[kClaude] = false;
  if (assistant_ && (credentials_changed || backend_changed))
    assistant_->CredentialsChanged();
  assistant_settings_ = std::move(settings);
  UpdateAssistantHostSettings();
  ProbeAssistantProviders();
  shell_->RefreshAssistantPreferences();
  PersistWorkspaceChanges();
}

bool WorkspaceLab::WriteAssistantPreference(std::string_view key, bool value) {
  if (!CanConfigureAssistant() || !CanEditWorkspace())
    return false;
  if (key == "assistant_enabled") {
    auto settings = assistant_settings_.value_or(prototype::AssistantSettings());
    if (!assistant_settings_) {
      settings.defaults.web = false;
      settings.default_model.clear();
    }
    settings.enabled = value;
    ++assistant_key_write_generation_;
    for (auto& status : assistant_provider_status_)
      status.clear();
    StoreAssistantSettings(std::move(settings));
    return true;
  }
  if (!assistant_settings_ || !assistant_settings_->enabled ||
      (key != "assistant_tabs" && key != "assistant_pages" &&
       key != "assistant_actions" && key != "assistant_history" &&
       key != "assistant_confirm_clicks" &&
       key != "assistant_web")) {
    return false;
  }
  auto settings = *assistant_settings_;
  if (key == "assistant_tabs")
    settings.defaults.tabs = value;
  else if (key == "assistant_pages")
    settings.defaults.pages = value;
  else if (key == "assistant_actions")
    settings.defaults.actions = value;
  else if (key == "assistant_history")
    settings.defaults.history = value;
  else if (key == "assistant_confirm_clicks")
    settings.defaults.confirm_clicks = value;
  else
    settings.defaults.web = value;
  StoreAssistantSettings(std::move(settings));
  return true;
}

base::DictValue WorkspaceLab::ReadAssistantSettings() const {
  if (!CanConfigureAssistant())
    return {};
  const bool enabled = assistant_settings_ && assistant_settings_->enabled;
  base::ListValue backends;
  base::ListValue statuses;
  for (size_t index : kProviderDisplayOrder) {
    const std::string_view provider = prototype::kAssistantProviders[index];
    backends.Append(base::DictValue().Set("provider", provider)
        .Set("label", base::UTF16ToUTF8(prototype::AssistantProviderLabel(provider)))
        .Set("enabled", !assistant_settings_ || assistant_settings_->backends[index]));
    auto status = assistant_provider_status_[index].Clone();
    if (assistant_provider_checking_[index]) {
      status.Set("tone", 0);
      status.Set("checking", true);
      status.Set("state", base::UTF16ToUTF8(prototype::T(u"Sprawdzam połączenie…")));
    } else if (status.empty()) {
      const bool ready = index == kOllama || assistant_provider_available_[index];
      status.Set("tone", ready ? 0 : 2);
      status.Set("state", base::UTF16ToUTF8(ready ? prototype::T(u"Do sprawdzenia")
                                                 : prototype::T(u"Wymaga konfiguracji")));
      if (index == kOllama) {
        status.Set("detail", base::UTF16ToUTF8(
            prototype::T(u"Uruchom Ollama i pobierz wybrany model, a potem sprawdź połączenie.")));
      }
    }
    statuses.Append(std::move(status));
  }
  base::ListValue models;
  if (assistant_settings_) {
    for (const auto& model : assistant_settings_->models) {
      models.Append(base::DictValue().Set("provider", model.provider)
          .Set("id", model.id).Set("visible", model.visible)
          .Set("label", base::UTF16ToUTF8(prototype::AssistantProviderLabel(model.provider))));
    }
  }
  return base::DictValue().Set("enabled", enabled)
      .Set("backends", std::move(backends)).Set("status", std::move(statuses))
      .Set("models", std::move(models))
      .Set("has_saved_key", assistant_settings_ && !assistant_settings_->encrypted_api_key.empty())
      .Set("codex_path", assistant_settings_ ? assistant_settings_->codex_path : "")
      .Set("claude_path", assistant_settings_ ? assistant_settings_->claude_path : "")
      .Set("default_model", assistant_settings_ ? assistant_settings_->default_model : "");
}

void WorkspaceLab::WriteAssistantSettings(base::DictValue command) {
  if (!CanConfigureAssistant() || !CanEditWorkspace() ||
      !assistant_settings_ || !assistant_settings_->enabled) {
    return;
  }
  const auto* action = command.FindString("command");
  if (!action)
    return;
  auto settings = *assistant_settings_;
  if (*action == "save_key") {
    if (const auto* key = command.FindString("key"))
      SaveAssistantApiKey(std::string(base::TrimWhitespaceASCII(*key, base::TRIM_ALL)));
    return;
  }
  if (*action == "remove_key") {
    ++assistant_key_write_generation_;
    settings.encrypted_api_key.clear();
    assistant_provider_status_[kOpenAI].clear();
    StoreAssistantSettings(std::move(settings));
    return;
  }
  const auto* provider = command.FindString("provider");
  if (!provider)
    return;
  const auto found = std::ranges::find(prototype::kAssistantProviders, *provider);
  if (found == prototype::kAssistantProviders.end())
    return;
  const size_t index = found - prototype::kAssistantProviders.begin();
  if (*action == "check" || (*action == "discover" && index == kOllama)) {
    CheckAssistantProvider(index, *action == "discover");
    return;
  }
  if (*action == "backend") {
    settings.backends[index] = command.FindBool("enabled").value_or(true);
  } else if (*action == "cli_path" && (index == kCodex || index == kClaude)) {
    const auto* supplied = command.FindString("path");
    if (!supplied)
      return;
    const std::string path(base::TrimWhitespaceASCII(*supplied, base::TRIM_ALL));
    if (!path.empty() && (path.size() > 4096 || path.contains('\0') ||
        !base::FilePath::FromUTF8Unsafe(path).IsAbsolute())) {
      return;
    }
    (index == kCodex ? settings.codex_path : settings.claude_path) = path;
  } else {
    const auto* supplied = command.FindString("id");
    if (!supplied)
      return;
    if (*action == "add") {
      if (!AddAssistantModel(settings, *provider,
                            std::string(base::TrimWhitespaceASCII(*supplied, base::TRIM_ALL)))) {
        return;
      }
    } else {
      const auto model = std::ranges::find_if(settings.models, [&](const auto& model) {
        return model.provider == *provider && model.id == *supplied;
      });
      if (model == settings.models.end())
        return;
      const auto full_id = *provider + "/" + model->id;
      if (*action == "default") {
        model->visible = true;
        settings.default_model = full_id;
      } else if (*action == "visible" && settings.default_model != full_id) {
        model->visible = command.FindBool("visible").value_or(true);
      } else if (*action == "remove") {
        if (settings.models.size() <= 1)
          return;
        settings.models.erase(model);
        if (settings.default_model == full_id) {
          auto replacement = std::ranges::find_if(settings.models, [&](const auto& other) {
            return other.visible && settings.BackendEnabled(other.provider) &&
                   assistant_host_settings_.BackendAvailable(other.provider);
          });
          settings.default_model = replacement == settings.models.end()
              ? "" : replacement->provider + "/" + replacement->id;
        }
      } else {
        return;
      }
    }
  }
  assistant_provider_status_[index].clear();
  StoreAssistantSettings(std::move(settings));
}

void WorkspaceLab::SaveAssistantApiKey(std::string key) {
  if (!os_crypt_ || key.empty() || key.size() > 4096 ||
      key.find_first_of(std::string_view("\0\r\n", 3)) != std::string::npos) {
    return;
  }
  os_crypt_->GetInstance(base::BindOnce(
      [](base::WeakPtr<WorkspaceLab> lab, uint64_t generation, std::string key,
         scoped_refptr<os_crypt_async::Encryptor> encryptor) {
        if (!lab || generation != lab->assistant_key_write_generation_ ||
            !lab->CanEditWorkspace() || !lab->assistant_settings_->enabled) {
          return;
        }
        std::string encrypted;
        if (!encryptor->IsEncryptionAvailable() || !encryptor->EncryptString(key, &encrypted)) {
          lab->shell_->ShowToast(prototype::T(u"Nie udało się bezpiecznie zapisać klucza API"), {}, {});
          return;
        }
        auto settings = *lab->assistant_settings_;
        settings.encrypted_api_key = base::Base64Encode(encrypted);
        lab->assistant_provider_status_[kOpenAI].clear();
        lab->StoreAssistantSettings(std::move(settings));
      }, weak_factory_.GetWeakPtr(), ++assistant_key_write_generation_, std::move(key)));
}

void WorkspaceLab::CheckAssistantProvider(size_t index, bool discover) {
  if (assistant_provider_checking_[index])
    return;
  const std::string provider = prototype::kAssistantProviders[index];
  std::string model;
  for (const auto& candidate : assistant_settings_->models) {
    if (candidate.provider == provider && candidate.visible &&
        (model.empty() || assistant_settings_->default_model == provider + "/" + candidate.id)) {
      model = candidate.id;
    }
  }
  if (!discover && model.empty()) {
    AssistantProviderChecked(index, false, assistant_provider_generation_,
        base::DictValue().Set("error", base::UTF16ToUTF8(
            prototype::T(u"Dodaj lub wykryj model i spróbuj ponownie."))));
    return;
  }
  assistant_provider_checking_[index] = true;
  shell_->RefreshAssistantPreferences();
  base::EnvironmentMap environment;
  assistant_settings_->ApplyCliPaths(environment);
  auto run = base::BindOnce(
      [](base::WeakPtr<WorkspaceLab> lab, size_t index, bool discover, uint64_t generation,
         std::string provider, std::string model, base::EnvironmentMap environment,
         std::string key) {
        if (!lab || generation != lab->assistant_provider_generation_)
          return;
        if (index == kOpenAI && key.empty() &&
            !lab->assistant_settings_->encrypted_api_key.empty()) {
          lab->AssistantProviderChecked(index, false, generation,
              base::DictValue().Set("error", base::UTF16ToUTF8(
                  prototype::T(u"Nie udało się odczytać zapisanego klucza API."))));
          return;
        }
        base::ThreadPool::PostTaskAndReplyWithResult(
            FROM_HERE, {base::MayBlock(), base::WithBaseSyncPrimitives(), base::TaskPriority::USER_VISIBLE},
            base::BindOnce(&prototype::CheckAssistantProvider, std::move(provider),
                           std::move(model), discover, std::move(environment), std::move(key)),
            base::BindOnce(&WorkspaceLab::AssistantProviderChecked, lab,
                           index, discover, generation));
      }, weak_factory_.GetWeakPtr(), index, discover, assistant_provider_generation_,
      provider, std::move(model), std::move(environment));
  if (index == kOpenAI)
    WithAssistantApiKey(std::move(run));
  else
    std::move(run).Run({});
}

void WorkspaceLab::AssistantProviderChecked(size_t index, bool discover,
                                            uint64_t generation, base::DictValue result) {
  if (generation != assistant_provider_generation_)
    return;
  assistant_provider_checking_[index] = false;
  if (!CanEditWorkspace()) {
    assistant_provider_status_[index].clear();
    shell_->RefreshAssistantPreferences();
    return;
  }
  auto settings = *assistant_settings_;
  const auto* models = result.FindList("models");
  size_t detected = 0;
  bool changed = false;
  if (discover && models) {
    for (const auto& value : *models) {
      const auto* id = value.GetIfString();
      if (id && ValidAssistantModel(*id)) {
        ++detected;
        changed |= AddAssistantModel(settings, kOllamaProvider, *id);
      }
    }
  }
  const bool ok = discover ? detected > 0 : result.FindBool("ok").value_or(false);
  const auto* error = result.FindString("error");
  const auto detail = ok
      ? (discover ? prototype::TF(u"Wykryte modele Ollama: {}", {base::NumberToString16(detected)})
                  : prototype::T(u"Model odpowiedział poprawnie."))
      : (discover && models ? prototype::T(u"Ollama działa. Pobierz model albo dodaj jego nazwę ręcznie.")
                            : prototype::AssistantConnectionError(error ? *error : ""));
  assistant_provider_status_[index] =
      base::DictValue().Set("tone", ok ? 1 : 2)
          .Set("state", base::UTF16ToUTF8(
              ok ? prototype::T(u"Połączono") : prototype::T(u"Nie udało się połączyć")))
          .Set("detail", base::UTF16ToUTF8(detail));
  if (ok && index == kOllama && !settings.ollama_configured) {
    settings.ollama_configured = true;
    changed = true;
  }
  if (changed) {
    StoreAssistantSettings(std::move(settings));
  } else {
    if (ok && index != kOllama) {
      assistant_provider_available_[index] = true;
      UpdateAssistantHostSettings();
      if (assistant_)
        assistant_->BackendAvailabilityChanged();
    }
    shell_->RefreshAssistantPreferences();
  }
}

prototype::WorkspaceShell* WorkspaceLab::shell() const {
  return shell_.get();
}

const prototype::AssistantSettings& WorkspaceLab::assistant_settings() const {
  return assistant_host_settings_;
}

bool WorkspaceLab::ShowAssistant(bool hover) {
  if (!assistant_host_settings_.enabled || AssistantProfileId().empty())
    return false;
  if (!assistant_) {
    auto panel = std::make_unique<prototype::AssistantPanel>(
        static_cast<prototype::AssistantHost*>(this));
    assistant_ = panel.get();
    assistant_profile_ = AssistantProfileId();
    shell_->SetAssistantView(panel.release());
  }
  if (assistant_profile_ != AssistantProfileId()) {
    if (!assistant_->SwitchProfile())
      return false;
    assistant_profile_ = AssistantProfileId();
  }
  assistant_->Show(hover);
  return true;
}

content::BrowserContext* WorkspaceLab::AssistantBrowserContext() const {
  return AssistantProfileId().empty() ? nullptr : WorkspaceProfile(active_index_);
}

std::string WorkspaceLab::AssistantProfileId() const {
  return assistant_host_settings_.enabled && model_.active()
             ? model_.active()->profile_id
             : std::string();
}

base::DictValue WorkspaceLab::AssistantContext(
    const prototype::AssistantPermissions& permissions) {
  auto* profile = AssistantBrowserContext();
  if (!permissions.tabs || !profile || profile->IsOffTheRecord())
    return {};
  base::ListValue tabs;
  base::ListValue spaces;
  base::ListValue pins;
  for (size_t workspace = 0; workspace < tabs_.size(); ++workspace) {
    if (WorkspaceProfile(workspace) != profile)
      continue;
    const auto& space = model_.workspaces()[workspace];
    spaces.Append(base::DictValue().Set("id", space.id).Set("name", space.name)
                      .Set("current", workspace == active_index_));
    if (sleeping_[workspace])
      continue;
    for (const auto& pin : WorkspacePins(workspace)) {
      if (!CanOpenAssistantPin(pin) || (pin.profile_pin && workspace != active_index_))
        continue;
      pins.Append(base::DictValue().Set("id", pin.id)
                      .Set("mention", AssistantPinMention(space.id, pin))
                      .Set("title", pin.title).Set("url", AssistantUrl(pin.url)));
    }
    for (size_t index = 0; index < tabs_[workspace].size(); ++index) {
      const auto& tab = tabs_[workspace][index];
      auto* page = tab.view->web_contents();
      const auto url = AssistantTabUrl(tab);
      if (url.empty())
        continue;
      const auto title = page ? page->GetTitle() : tab.deferred_title;
      const auto id = base::NumberToString(tab.id);
      tabs.Append(base::DictValue().Set("id", id).Set("mention", "@tab:" + id)
                      .Set("space", space.id).Set("title", base::UTF16ToUTF8(title))
                      .Set("url", url)
                      .Set("current", workspace == active_index_ && index == selected_[workspace]));
    }
  }
  return base::DictValue().Set("tabs", std::move(tabs)).Set("spaces", std::move(spaces))
      .Set("pins", std::move(pins));
}

void WorkspaceLab::PrepareNightShiftRepair(content::RenderFrameHost& document) {
  auto* page = content::WebContents::FromRenderFrameHost(&document);
  if (!CanSetNightShiftSiteMode(document) || !CanActOnAssistantPage(page) ||
      !ShowAssistant(false)) {
    return;
  }
  const auto request = base::Uuid::GenerateRandomV4().AsLowercaseString();
  night_shift_request_.emplace(NightShiftRequest{
      .document = document.GetWeakDocumentPtr(),
      .url = document.GetLastCommittedURL(),
      .id = request,
  });
  const auto* prompt = prototype::UiLanguageIsEnglish()
      ? u"Fix Night Shift colors for this site. Request ID: "
      : u"Popraw kolory Night Shift dla tej witryny. Identyfikator zlecenia: ";
  assistant_->PreparePageAction(prompt + base::UTF8ToUTF16(request),
                                /*needs_actions=*/false, /*needs_web=*/false,
                                /*night_shift=*/true);
}

void WorkspaceLab::RunNightShiftAssistantTool(
    std::string_view tool, const base::DictValue& args, bool page_access,
    bool background, base::OnceCallback<void(base::Value)> done) {
  const auto fail = [&](std::string_view error) {
    std::move(done).Run(AssistantToolError(error));
  };
  const auto* token = args.FindString("request");
  auto* document = night_shift_request_
      ? night_shift_request_->document.AsRenderFrameHostIfValid() : nullptr;
  auto* page = document ? content::WebContents::FromRenderFrameHost(document) : nullptr;
  if (!page_access || background || !token || !document ||
      *token != night_shift_request_->id ||
      document->GetLastCommittedURL() != night_shift_request_->url ||
      !CanSetNightShiftSiteMode(*document) || !CanActOnAssistantPage(page)) {
    fail("The Night Shift document or request expired.");
    return;
  }
  if (tool == "night_shift_inspect") {
    prototype::RunAssistantPageOperation(
        page, base::DictValue().Set("tool", tool),
        base::BindOnce(
            [](base::WeakPtr<WorkspaceLab> lab, std::string id,
               base::OnceCallback<void(base::Value)> done, base::Value result) {
              auto* request = lab && lab->night_shift_request_
                  ? &*lab->night_shift_request_ : nullptr;
              auto* frame = request
                  ? request->document.AsRenderFrameHostIfValid() : nullptr;
              if (!frame || request->id != id ||
                  frame->GetLastCommittedURL() != request->url ||
                  !lab->CanActOnAssistantPage(
                      content::WebContents::FromRenderFrameHost(frame))) {
                std::move(done).Run(AssistantToolError("The Night Shift request expired."));
                return;
              }
              auto* data = result.GetIfDict();
              auto* candidates = data ? data->FindList("candidates") : nullptr;
              if (!candidates || candidates->size() > 48) {
                std::move(done).Run(AssistantToolError("Could not sample Night Shift colors."));
                return;
              }
              request->candidates = candidates->Clone();
              // Selectors stay in the browser. The model receives target IDs
              // and computed colors only.
              for (auto& candidate : *candidates) {
                if (auto* fields = candidate.GetIfDict())
                  fields->Remove("path");
              }
              std::move(done).Run(std::move(result));
            },
            weak_factory_.GetWeakPtr(), *token, std::move(done)));
    return;
  }

  const auto* label = args.FindString("label");
  const auto* proposed = args.FindList("rules");
  if (!label || label->empty() || label->size() > 100 || !proposed ||
      proposed->empty() || proposed->size() > 12) {
    fail("Invalid Night Shift proposal.");
    return;
  }
  constexpr std::array<std::string_view, 7> allowed = {
      "background-color", "border-color", "color", "fill", "outline-color",
      "stroke", "text-decoration-color"};
  base::ListValue rules;
  for (const auto& value : *proposed) {
    const auto* rule = value.GetIfDict();
    const int index = rule ? rule->FindInt("target").value_or(-1) : -1;
    if (index < 0 ||
        static_cast<size_t>(index) >= night_shift_request_->candidates.size()) {
      fail("Use only target IDs returned by night_shift_inspect.");
      return;
    }
    const auto* candidate = night_shift_request_->candidates[index].GetIfDict();
    const auto* path = candidate ? candidate->FindList("path") : nullptr;
    const auto* property = rule->FindString("property");
    const auto* color = rule->FindString("color");
    if (!path || !property || !std::ranges::contains(allowed, *property) ||
        !color || (*color != "transparent" &&
                   ((color->size() != 7 && color->size() != 9) ||
                    color->front() != '#' ||
                    !std::ranges::all_of(std::string_view(*color).substr(1),
                                         base::IsHexDigit<char>)))) {
      fail("Use a color property and a hex color.");
      return;
    }
    rules.Append(base::DictValue().Set("path", path->Clone())
                     .Set("property", *property).Set("color", *color));
  }

  auto& context = *document->GetBrowserContext();
  const auto* extension = GetBundledNightShiftExtension(context);
  auto* storage = extensions::StorageFrontend::Get(&context);
  const int tab = WorkspaceExtensionTabIdForTesting(*page);
  if (!extension || !storage || tab <= 0) {
    fail("Night Shift is unavailable in this profile.");
    return;
  }
  auto draft = base::DictValue()
                   .Set("documentId", extensions::ExtensionApiFrameIdMap::GetDocumentId(
                                          document).ToString())
                   .Set("url", night_shift_request_->url.spec())
                   .Set("proposal", base::DictValue().Set("label", *label)
                                        .Set("rules", std::move(rules)));
  night_shift_request_.reset();
  storage->Set(
      base::WrapRefCounted(extension), extensions::StorageAreaNamespace::kSession,
      base::DictValue().Set("night-shift-draft:" + base::NumberToString(tab),
                            std::move(draft)),
      base::BindOnce([](base::OnceCallback<void(base::Value)> done,
                        extensions::StorageFrontend::ResultStatus status) {
        if (!status.success) {
          std::move(done).Run(AssistantToolError("Could not stage a Night Shift preview."));
          return;
        }
        std::move(done).Run(base::Value(base::DictValue().Set(
            "detail", "Draft ready. Open Night Shift site settings to compare and save it.")));
      }, std::move(done)));
}

void WorkspaceLab::AssistantTool(
    std::string tool, base::DictValue args, prototype::AssistantPermissions permissions,
    base::OnceCallback<void(base::Value)> done, bool user_confirmed,
    std::string background) {
  if (tool == "night_shift_inspect" || tool == "night_shift_propose") {
    RunNightShiftAssistantTool(tool, args, permissions.pages, !background.empty(),
                               std::move(done));
    return;
  }
  const bool edit = tool == "type" || tool == "insert_text" || tool == "select";
  const bool key = tool == "press_key";
  const bool screenshot = tool == "screenshot";
  const bool adblock = tool == "adblock_inspect";
  const bool adblock_rules = tool == "adblock_add_rules";
  const bool diagnose = tool == "adblock_diagnose";
  const bool clear_adblock_preview = tool == "adblock_clear_preview";
  const bool show_adblock_preview = tool == "adblock_show_preview";
  const bool debug = prototype::IsAssistantDebugTool(tool) || diagnose;
  const bool activate = tool == "activate_tab";
  const bool pin = tool == "pin_tab";
  const bool unpin = tool == "unpin_tab";
  const bool open = tool == "open_tab";
  const bool navigate = tool == "navigate";
  const bool close = tool == "close_tab";
  const bool history = tool == "history" || tool == "downloads";
  const bool workspace_action = tool == "browser_command" ||
      tool == "create_space" || tool == "create_folder" || tool == "move_tab";
  const bool tab_action = activate || pin || unpin || open || navigate || close;
  const auto profile_id = AssistantProfileId();
  if (clear_adblock_preview || show_adblock_preview || (adblock_rules && user_confirmed)) {
    const auto* token = args.FindString("_page_confirmation");
    auto& approval = assistant_page_approval_;
    if (token && approval && approval->tool == "adblock_add_rules" &&
        approval->token == *token) {
      if (!show_adblock_preview) {
        ClearAssistantAdblockPreview(clear_adblock_preview);
      } else if (!approval->adblock_preview_cleared && !approval->adblock_selectors.empty() &&
                 background.empty() && permissions.tabs && permissions.pages && permissions.actions &&
                 approval->settings_generation == assistant_provider_generation_ &&
                 CanActOnAssistantPage(web_contents()) &&
                 approval->document.AsRenderFrameHostIfValid() == web_contents()->GetPrimaryMainFrame() &&
                 approval->url == web_contents()->GetLastCommittedURL()) {
        prototype::RunAdblockCosmeticScript(
            web_contents()->GetPrimaryMainFrame(), base::DictValue().Set("op", "assistant_preview")
                .Set("token", *token).Set("url", approval->url.spec())
                .Set("selectors", approval->adblock_selectors.Clone()),
            base::DoNothing());
      }
    }
    if (clear_adblock_preview || show_adblock_preview) {
      std::move(done).Run(base::Value());
      return;
    }
  }
  if (profile_id.empty() || !background.empty() ||
      (tool != "list_tabs" && tool != "read_page" && tool != "click" &&
       !edit && !key && !screenshot && !adblock && !adblock_rules &&
       !debug && !tab_action && !history &&
       !workspace_action)) {
    std::move(done).Run(AssistantToolError("This browser tool is unavailable."));
    return;
  }
  if (workspace_action) {
    RunAssistantWorkspaceAction(tool, args, permissions, std::move(done));
    return;
  }
  if (history) {
    if (!permissions.history || !assistant_host_settings_.defaults.history) {
      std::move(done).Run(AssistantToolError("History permission is disabled."));
      return;
    }
    if (tool == "downloads") {
      ReadAssistantDownloads(profile_id, assistant_provider_generation_,
                             base::TimeTicks::Now() + base::Seconds(8),
                             std::move(done));
    } else {
      const auto* query = args.FindString("query");
      SearchAssistantHistory(query ? std::string_view(*query) : std::string_view(),
                             std::move(done));
    }
    return;
  }
  if (!permissions.tabs) {
    std::move(done).Run(AssistantToolError("Tab discovery permission is disabled."));
    return;
  }
  if (tool == "list_tabs") {
    std::move(done).Run(base::Value(AssistantContext(permissions)));
    return;
  }
  if (!permissions.pages && !tab_action) {
    std::move(done).Run(AssistantToolError("Page reading permission is disabled."));
    return;
  }
  if ((debug || adblock || adblock_rules) && (!assistant_host_settings_.defaults.tabs ||
                             !assistant_host_settings_.defaults.pages)) {
    std::move(done).Run(AssistantToolError("The applicable tab and page grants are disabled."));
    return;
  }
  if ((tool == "debug_edit_dom" || tool == "debug_hard_reload" || adblock_rules || diagnose) &&
      (!permissions.actions || !assistant_host_settings_.defaults.actions)) {
    std::move(done).Run(AssistantToolError("Debugging requires the applicable page and action grants."));
    return;
  }

  if ((tool == "click" || edit || key || tab_action) && !permissions.actions) {
    std::move(done).Run(AssistantToolError("Browser actions permission is disabled."));
    return;
  }

  GURL destination;
  if (open || navigate) {
    const auto* url = args.FindString("url");
    if (url && url->size() <= 32768)
      destination = GURL(*url);
    if (!destination.is_valid() || !destination.SchemeIsHTTPOrHTTPS() ||
        destination.has_username() || destination.has_password()) {
      std::move(done).Run(AssistantToolError("Only HTTP(S) URLs without credentials are allowed."));
      return;
    }
  }
  if (open) {
    if (!SelectAssistantWorkspace(active_index_) ||
        tabs_[active_index_].size() >= prototype::kMaxWorkspaceSessionTabs) {
      std::move(done).Run(AssistantToolError("The tab could not be opened."));
      return;
    }
    AddTab(active_index_);
    SelectTab(tabs_[active_index_].size() - 1);
    NavigateAssistantTab(destination, std::move(done));
    return;
  }

  const auto* target = args.FindString("tab");
  if (!target) {
    std::move(done).Run(AssistantToolError("Missing tab reference."));
    return;
  }
  if (target->starts_with("@pin:")) {
    if (!activate && tool != "read_page") {
      std::move(done).Run(AssistantToolError("Open the pin before using this tool on its tab."));
      return;
    }
    if (!permissions.actions) {
      std::move(done).Run(AssistantToolError("Opening a pin requires browser actions access."));
      return;
    }
    std::move(done).Run(OpenAssistantPin(*target));
    return;
  }
  const auto position = FindAssistantTab(*target);
  if (!position) {
    std::move(done).Run(AssistantToolError("Tab expired or belongs to another profile."));
    return;
  }
  const auto [workspace, index] = *position;
  auto& tab = tabs_[workspace][index];
  if (tab_action) {
    if ((!activate && AssistantTabUrl(tab).empty()) || !SelectAssistantWorkspace(workspace)) {
      std::move(done).Run(AssistantToolError("The tab action is unavailable now."));
      return;
    }
    if (pin || unpin) {
      const bool changed = pin ? (!tab.pin.empty() || PinTab(index, {}, {}))
                               : (!tab.pin.empty() && Unpin(tab.pin, index));
      std::move(done).Run(changed ? base::Value(base::DictValue().Set("ok", true))
                                  : AssistantToolError("The tab's pin could not be changed."));
      return;
    }
    if (close) {
      CloseTab(index);
      std::move(done).Run(base::Value(base::DictValue().Set(
          "detail", "Close requested. List tabs to verify; the page may ask for confirmation.")));
      return;
    }
    SelectTab(index);
    if (selected_[workspace] != index) {
      std::move(done).Run(AssistantToolError("The tab could not be selected."));
      return;
    }
    if (navigate) {
      NavigateAssistantTab(destination, std::move(done));
      return;
    }
    std::move(done).Run(base::Value(base::DictValue().Set("ok", true)));
    return;
  }
  auto* page = tab.view->web_contents();
  if (adblock_rules) {
    RunAssistantAdblockRules(page, args, user_confirmed, std::move(done));
    return;
  }
  if (debug) {
    RunAssistantDebugTool(page, tool, args, user_confirmed, std::move(done));
    return;
  }
  if (tool == "click") {
    ClickAssistantTab(page, args, permissions, user_confirmed, std::move(done));
    return;
  }
  if (key) {
    PressAssistantPageKey(page, args, permissions, user_confirmed, std::move(done));
    return;
  }
  if (edit) {
    EditAssistantPage(page, tool, args, std::move(done));
    return;
  }
  if (!page) {
    const auto url = tab.deferred_url;
    if (!url.SchemeIsHTTPOrHTTPS() || url.has_username() || url.has_password() ||
        !CanEditWorkspace()) {
      std::move(done).Run(AssistantToolError("Page is not ready or is protected."));
      return;
    }
    const bool discarded = !!tab.discarded_page;
    page = &EnsureHiddenTabPage(workspace, tab, url);
    if (page->GetSize().IsEmpty())
      page->Resize(gfx::Rect(shell_->content_host()->size()));
    if (discarded)
      page->GetController().LoadIfNecessary();
    else
      tab.view->LoadInitialURL(url);
    RefreshShell();
  }
  if (screenshot && page->GetSize().IsEmpty())
    page->Resize(gfx::Rect(shell_->content_host()->size()));
  const auto inspection = screenshot ? AssistantInspection::kScreenshot :
      adblock ? AssistantInspection::kAdblock : AssistantInspection::kPage;
  InspectAssistantTab(page->GetWeakPtr(), model_.workspaces()[workspace].id, inspection,
                      assistant_provider_generation_, base::TimeTicks::Now() + base::Seconds(20),
                      std::move(done));
}

void WorkspaceLab::RunAssistantDebugTool(
    content::WebContents* page, std::string_view tool,
    const base::DictValue& args, bool confirmed,
    base::OnceCallback<void(base::Value)> done) {
  if (!CanActOnAssistantPage(page)) {
    std::move(done).Run(AssistantToolError("Debugging requires the selected web page."));
    return;
  }
  const bool console = tool == "debug_console";
  const bool edit = tool == "debug_edit_dom";
  const bool reload = tool == "debug_hard_reload";
  const bool diagnose = tool == "adblock_diagnose";
  if (diagnose && page->GetController().GetLastCommittedEntry()->GetHasPostData()) {
    std::move(done).Run(AssistantToolError("Ad blocking diagnosis cannot reload a form submission."));
    return;
  }
  const auto* selector = args.FindString("selector");
  base::DictValue operation;
  const int limit = std::clamp(args.FindInt("limit").value_or(25), 1, 40);
  if (console) {
    operation.Set("limit", limit);
  } else if (!reload && !diagnose) {
    if (!selector || selector->empty() || selector->size() > 240) {
      std::move(done).Run(AssistantToolError("Give a CSS selector of up to 240 bytes."));
      return;
    }
    operation.Set("selector", *selector);
  }
  if (edit) {
    static constexpr auto kEdits = std::to_array<std::string_view>({
        "text", "style", "add_class", "remove_class", "hide", "show"});
    const auto* name = args.FindString("operation");
    const auto* value = args.FindString("value");
    const auto* property = args.FindString("property");
    if (!name || !std::ranges::contains(kEdits, *name) ||
        (value && value->size() > 1000) || (property && property->size() > 64)) {
      std::move(done).Run(AssistantToolError("Give a supported DOM edit with bounded arguments."));
      return;
    }
    operation.Set("operation", *name);
    if (value)
      operation.Set("value", *value);
    if (property)
      operation.Set("property", *property);
  }
  const auto target = base::WriteJson(operation);
  if (!target) {
    std::move(done).Run(AssistantToolError("The debugging arguments could not be encoded."));
    return;
  }
  auto* frame = page->GetPrimaryMainFrame();
  const auto document = frame->GetWeakDocumentPtr();
  const auto url = page->GetLastCommittedURL();
  const auto generation = assistant_provider_generation_;
  if (!confirmed) {
    ClearAssistantAdblockPreview();
    const auto token = base::Uuid::GenerateRandomV4().AsLowercaseString();
    assistant_page_approval_ = AssistantPageApproval{
        document, url, std::string(tool), *target, {}, token, generation};
    auto label = frame->GetLastCommittedOrigin().Serialize();
    if (!console && !reload && !diagnose)
      label += " · " + *selector;
    const auto prompt = console ? prototype::T(u"Udostępnić konsolę tej karty?") :
        edit ? prototype::T(u"Zmienić DOM tej karty?") :
        reload ? prototype::T(u"Przeładować kartę bez pamięci podręcznej?") :
        diagnose ? prototype::T(u"Przeładować tę kartę i sprawdzić wykrywanie blokowania reklam?") :
               prototype::T(u"Sprawdzić DOM tej karty?");
    std::move(done).Run(base::Value(base::DictValue().Set("confirmation_required", true)
        .Set("prompt", base::UTF16ToUTF8(prompt)).Set("label", label)
        .Set("page_confirmation", token)));
    return;
  }
  const auto* token = args.FindString("_page_confirmation");
  if (!token || !TakeAssistantPageApproval(page, tool, *target, *token)) {
    std::move(done).Run(AssistantToolError("The approved debugging target changed or expired."));
    return;
  }
  if (diagnose) {
    RunAssistantAdblockDiagnosis(*page, std::move(done));
    return;
  }
  if (reload) {
    auto observer = std::make_unique<AssistantReloadNavigation>(
        page, content::ReloadType::BYPASSING_CACHE);
    const auto page_ref = page->GetWeakPtr();
    const auto workspace = model_.active()->id;
    ReloadWorkspacePage(*page, /*bypass_cache=*/true);
    WaitForAssistantReload(page_ref, workspace, generation, std::move(observer),
                           base::TimeTicks::Now() + base::Seconds(20),
                           std::move(done));
    return;
  }

  auto on_result = base::BindOnce(
      [](base::WeakPtr<WorkspaceLab> lab, base::WeakPtr<content::WebContents> page,
         content::WeakDocumentPtr document, GURL url, uint64_t generation,
         base::OnceCallback<void(base::Value)> done, base::Value result) {
        if (!lab || !lab->CanActOnAssistantPage(page.get()) ||
            document.AsRenderFrameHostIfValid() != page->GetPrimaryMainFrame() ||
            url != page->GetLastCommittedURL() ||
            generation != lab->assistant_provider_generation_) {
          std::move(done).Run(AssistantToolError("The document or Assistant access changed."));
          return;
        }
        std::move(done).Run(std::move(result));
      }, weak_factory_.GetWeakPtr(), page->GetWeakPtr(), document, url, generation,
      std::move(done));
  if (console) {
    auto reader = std::make_unique<prototype::DevToolsConsoleReader>(page, limit);
    if (!reader->Start()) {
      std::move(on_result).Run(AssistantToolError("Could not read the page console."));
      return;
    }
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce([](std::unique_ptr<prototype::DevToolsConsoleReader> reader,
                          base::OnceCallback<void(base::Value)> reply) {
          auto result = reader->Result();
          reader.reset();
          std::move(reply).Run(std::move(result));
        }, std::move(reader), std::move(on_result)),
        base::Milliseconds(650));
    return;
  }
  operation.Set("tool", tool);
  prototype::RunAssistantPageOperation(page, std::move(operation),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          std::move(on_result), AssistantToolError("The page could not be inspected.")));
}

void WorkspaceLab::WaitForAssistantReload(
    base::WeakPtr<content::WebContents> page, std::string workspace,
    uint64_t generation, std::unique_ptr<AssistantReloadNavigation> reload,
    base::TimeTicks deadline, base::OnceCallback<void(base::Value)> done) {
  if (!CanInspectAssistantTab(page.get(), workspace, generation) ||
      page.get() != web_contents() || reload->failed) {
    std::move(done).Run(AssistantToolError("The reload was interrupted or Assistant access changed."));
    return;
  }
  if (reload->document && !page->IsLoading()) {
    if (!CanActOnAssistantPage(page.get()) ||
        reload->document->AsRenderFrameHostIfValid() != page->GetPrimaryMainFrame()) {
      std::move(done).Run(AssistantToolError("The reloaded document changed or is unavailable."));
      return;
    }
    std::move(done).Run(base::Value(base::DictValue().Set("ok", true)
        .Set("url", AssistantUrl(page->GetLastCommittedURL()))
        .Set("title", base::UTF16ToUTF8(page->GetTitle().substr(0, 8192)))));
    return;
  }
  const auto* dialogs = prototype::PageDialogs::FromWebContents(page.get());
  const bool waiting_to_start = page->GetController().GetPendingEntry() ||
                                (dialogs && dialogs->repost_dialog_open());
  if ((!reload->started() && !waiting_to_start) || base::TimeTicks::Now() >= deadline) {
    std::move(done).Run(AssistantToolError("The reload did not complete."));
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&WorkspaceLab::WaitForAssistantReload,
                     weak_factory_.GetWeakPtr(), page, std::move(workspace),
                     generation, std::move(reload), deadline, std::move(done)),
      base::Milliseconds(100));
}

void WorkspaceLab::RunAssistantAdblockDiagnosis(
    content::WebContents& page, base::OnceCallback<void(base::Value)> done) {
  const auto url = page.GetLastCommittedURL();
  const auto workspace = model_.active()->id;
  const auto generation = assistant_provider_generation_;
  auto nonce = base::Uuid::GenerateRandomV4().AsLowercaseString();
  std::erase(nonce, '-');
  auto prepare = base::BindOnce(
      [](base::WeakPtr<WorkspaceLab> lab, base::WeakPtr<content::WebContents> page,
         std::string workspace, uint64_t generation, GURL url, std::string nonce,
         content::NavigationHandle& navigation) {
        if (!lab || !lab->CanInspectAssistantTab(page.get(), workspace, generation) ||
            page.get() != lab->web_contents() || page->GetLastCommittedURL() != url ||
            navigation.GetURL() != url || navigation.WasServerRedirect() ||
            navigation.GetNetErrorCode() != net::OK) {
          return false;
        }
        content::ZitterBeforeDocumentCommit(
            navigation,
            base::BindOnce([](content::GlobalRenderFrameHostId frame_id, std::string nonce,
                              const blink::DocumentToken& document) {
              auto* frame = content::RenderFrameHost::FromID(frame_id);
              if (!frame || !frame->IsRenderFrameLive())
                return;
              mojo::AssociatedRemote<mojom::AdblockProbe> remote;
              frame->GetRemoteAssociatedInterfaces()->GetInterface(&remote);
              remote->Arm(document, nonce);
            }, navigation.GetRenderFrameHost()->GetGlobalId(), std::move(nonce)));
        return true;
      }, weak_factory_.GetWeakPtr(), page.GetWeakPtr(), workspace, generation, url, nonce);
  auto observer = std::make_unique<AssistantReloadNavigation>(
      &page, content::ReloadType::NORMAL, std::move(prepare));
  const auto page_ref = page.GetWeakPtr();
  ReloadWorkspacePage(page, /*bypass_cache=*/false);
  WaitForAssistantReload(
      page_ref, workspace, generation, std::move(observer),
      base::TimeTicks::Now() + base::Seconds(20),
      base::BindOnce(
          [](base::WeakPtr<WorkspaceLab> lab, base::WeakPtr<content::WebContents> page,
             GURL url, uint64_t generation, std::string nonce,
             base::OnceCallback<void(base::Value)> done, base::Value result) {
            if (!lab || !page || !result.is_dict() ||
                !result.GetDict().FindBool("ok").value_or(false) ||
                page->GetLastCommittedURL() != url) {
              std::move(done).Run(AssistantToolError("The diagnostic reload was interrupted."));
              return;
            }
            base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
                FROM_HERE, base::BindOnce(&WorkspaceLab::ReadAssistantAdblockDiagnosis,
                    lab, page, page->GetPrimaryMainFrame()->GetWeakDocumentPtr(), url,
                    generation, std::move(nonce), std::move(done)),
                base::Seconds(6));
          }, weak_factory_.GetWeakPtr(), page_ref, url, generation, std::move(nonce),
          std::move(done)));
}

void WorkspaceLab::ReadAssistantAdblockDiagnosis(
    base::WeakPtr<content::WebContents> page, content::WeakDocumentPtr document,
    GURL url, uint64_t generation, std::string nonce,
    base::OnceCallback<void(base::Value)> done) {
  if (!CanActOnAssistantPage(page.get()) ||
      document.AsRenderFrameHostIfValid() != page->GetPrimaryMainFrame() ||
      page->GetLastCommittedURL() != url || assistant_provider_generation_ != generation) {
    std::move(done).Run(AssistantToolError("The diagnostic document or Assistant access changed."));
    return;
  }
  prototype::RunAssistantPageOperation(
      page.get(), base::DictValue().Set("tool", "probe_log").Set("nonce", std::move(nonce)),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(
              [](base::WeakPtr<WorkspaceLab> lab, base::WeakPtr<content::WebContents> page,
                 content::WeakDocumentPtr document, GURL url, uint64_t generation,
                 base::OnceCallback<void(base::Value)> done, base::Value value) {
                if (!lab || !lab->CanActOnAssistantPage(page.get()) ||
                    document.AsRenderFrameHostIfValid() != page->GetPrimaryMainFrame() ||
                    page->GetLastCommittedURL() != url ||
                    lab->assistant_provider_generation_ != generation) {
                  std::move(done).Run(AssistantToolError("The diagnostic document or Assistant access changed."));
                  return;
                }
                if (!value.is_dict() || !value.GetDict().FindList("bait_reads")) {
                  std::move(done).Run(AssistantToolError("The diagnostic probe could not be read."));
                  return;
                }
                base::ListValue blocked;
                for (auto& request : ProfileContext::RecentAdblockRequests(*page))
                  blocked.Append(std::move(request));
                auto& result = value.GetDict();
                result.Set("site", prototype::AdblockSiteScope(url));
                result.Set("blocked_requests", std::move(blocked));
                result.Set("note", "The diagnostics describe one instrumented reload of the selected tab.");
                std::move(done).Run(std::move(value));
              }, weak_factory_.GetWeakPtr(), page, document, std::move(url), generation,
              std::move(done)), base::Value()));
}

void WorkspaceLab::RunAssistantAdblockRules(
    content::WebContents* page, const base::DictValue& args, bool confirmed,
    base::OnceCallback<void(base::Value)> done) {
  const bool trial = args.FindBool("check").value_or(false);
  if (!CanActOnAssistantPage(page) ||
      (trial && page->GetController().GetLastCommittedEntry()->GetHasPostData())) {
    std::move(done).Run(AssistantToolError("Ad blocking rules need an eligible selected web page."));
    return;
  }
  std::vector<std::string> rules;
  if (const auto* list = args.FindList("rules"); list && list->size() <= 50) {
    for (const auto& item : *list) {
      const auto* rule = item.GetIfString();
      rules.emplace_back(rule ? base::TrimWhitespaceASCII(*rule, base::TRIM_ALL)
                              : std::string_view());
    }
  } else if (const auto* text = args.FindString("rules"); text && text->size() <= 50050) {
    rules = base::SplitString(*text, "\n", base::TRIM_WHITESPACE,
                              base::SPLIT_WANT_NONEMPTY);
  }
  if (rules.empty() || rules.size() > 50 ||
      std::ranges::any_of(rules, [trial](const auto& rule) {
        return rule.empty() || rule.size() > 1000 ||
               (trial && rule.contains("+js("));
      })) {
    std::move(done).Run(AssistantToolError(trial
        ? "Give 1 to 50 network or cosmetic rules without scriptlets."
        : "Give 1 to 50 nonempty filter rules of up to 1000 bytes."));
    return;
  }
  const auto url = page->GetLastCommittedURL();
  const auto site = prototype::AdblockSiteScope(url);
  if (std::ranges::any_of(rules, [&](const auto& rule) {
        return !adblock::rule_in_scope(rule, site);
      })) {
    std::move(done).Run(AssistantToolError("Every rule must be valid and scoped to " + site + "."));
    return;
  }
  if (!trial) {
    SaveAssistantAdblockRules(page, args, std::move(rules), site, confirmed,
                              std::move(done));
    return;
  }
  const auto* replacements = args.FindList("replaces");
  if (args.contains("replaces") && (!replacements || !replacements->empty())) {
    std::move(done).Run(AssistantToolError("Temporary trials cannot replace saved rules."));
    return;
  }
  ProfileContext::TryAdblockRules(
      *page, std::move(rules),
      base::BindOnce(
          [](base::WeakPtr<WorkspaceLab> lab, base::WeakPtr<content::WebContents> page,
             GURL url, uint64_t generation, base::OnceCallback<void(base::Value)> done,
             std::optional<size_t> trial) {
            if (!trial) {
              std::move(done).Run(AssistantToolError("The rules could not be tried on this site."));
              return;
            }
            if (!lab || !lab->CanActOnAssistantPage(page.get()) ||
                page->GetLastCommittedURL() != url ||
                lab->assistant_provider_generation_ != generation) {
              if (page)
                ProfileContext::ClearAdblockTrial(*page, trial);
              std::move(done).Run(AssistantToolError("The tab or Assistant access changed."));
              return;
            }
            auto observer = std::make_unique<AssistantReloadNavigation>(
                page.get(), content::ReloadType::NORMAL);
            const auto workspace = lab->model_.active()->id;
            lab->ReloadWorkspacePage(*page, /*bypass_cache=*/false);
            lab->WaitForAssistantReload(
                page, workspace, generation, std::move(observer),
                base::TimeTicks::Now() + base::Seconds(20),
                base::BindOnce(
                    [](base::WeakPtr<content::WebContents> page, size_t trial,
                       base::OnceCallback<void(base::Value)> done, base::Value result) {
                      const auto* status = result.GetIfDict();
                      if (!page || !status || !status->FindBool("ok").value_or(false) ||
                          !ProfileContext::HasAdblockTrial(*page, trial)) {
                        if (page)
                          ProfileContext::ClearAdblockTrial(*page, trial);
                        std::move(done).Run(AssistantToolError("The temporary rule trial did not complete."));
                        return;
                      }
                      std::move(done).Run(base::Value(base::DictValue().Set("checked", true)
                          .Set("blocked_on_page", ProfileContext::AdblockCount(*page))
                          .Set("detail", "The rules are on trial in this tab only. "
                              "Inspect the page to check their effect. Nothing was saved.")));
                    }, page, *trial, std::move(done)));
          }, weak_factory_.GetWeakPtr(), page->GetWeakPtr(), url,
          assistant_provider_generation_, std::move(done)));
}

void WorkspaceLab::ClearAssistantAdblockPreview(bool invalidate) {
  auto& approval = assistant_page_approval_;
  if (!approval || approval->tool != "adblock_add_rules")
    return;
  if (!std::exchange(approval->adblock_preview_cleared, true) &&
      !approval->adblock_selectors.empty()) {
    auto* frame = approval->document.AsRenderFrameHostIfValid();
    if (frame && frame->IsActive() && frame->IsRenderFrameLive()) {
      prototype::RunAdblockCosmeticScript(
          frame, base::DictValue().Set("op", "clear_assistant_preview")
                     .Set("token", approval->token),
          base::DoNothing());
    }
  }
  if (invalidate)
    approval.reset();
}

void WorkspaceLab::SaveAssistantAdblockRules(
    content::WebContents* page, const base::DictValue& args,
    std::vector<std::string> rules, std::string site, bool confirmed,
    base::OnceCallback<void(base::Value)> done) {
  auto* profile = static_cast<ProfileContext*>(page->GetBrowserContext());
  auto saved = profile->AdblockRules();
  if (!profile->IsAdblockEnabledFor(page->GetLastCommittedURL())) {
    std::move(done).Run(AssistantToolError("Ad blocking is off on this site."));
    return;
  }
  const auto* replaces = args.FindList("replaces");
  if (args.contains("replaces") && (!replaces || replaces->size() > 50)) {
    std::move(done).Run(AssistantToolError("List up to 50 saved rules to replace."));
    return;
  }
  std::vector<std::string> replacements;
  if (replaces) {
    for (const auto& item : *replaces) {
      const auto* rule = item.GetIfString();
      if (!rule || rule->size() > 1000 || !std::ranges::contains(saved, *rule) ||
          !adblock::rule_in_scope(*rule, site) ||
          std::ranges::contains(replacements, *rule)) {
        std::move(done).Run(AssistantToolError("Replace only this site's existing rules, exactly as saved."));
        return;
      }
      replacements.push_back(*rule);
    }
  }
  base::ListValue proposed;
  base::ListValue selectors;
  int exceptions = 0;
  int scriptlets = 0;
  for (const auto& rule : rules) {
    proposed.Append(rule);
    if (rule.starts_with("@@") || rule.contains("#@#")) {
      ++exceptions;
    } else if (rule.contains("+js(")) {
      ++scriptlets;
    } else if (const auto at = rule.find("##"); at != std::string::npos) {
      selectors.Append(rule.substr(at + 2));
    }
  }
  base::ListValue removed;
  for (const auto& rule : replacements)
    removed.Append(rule);
  auto target = base::WriteJson(base::DictValue()
      .Set("rules", std::move(proposed)).Set("replaces", std::move(removed)));
  if (!target) {
    std::move(done).Run(AssistantToolError("The proposed rules could not be encoded."));
    return;
  }
  const auto* token = args.FindString("_page_confirmation");
  if (confirmed && (!token || !TakeAssistantPageApproval(
                        page, "adblock_add_rules", *target, *token))) {
    std::move(done).Run(AssistantToolError("The approved rules or document changed."));
    return;
  }
  auto* frame = page->GetPrimaryMainFrame();
  const auto on_site = page->GetLastCommittedURL();
  const size_t selector_count = selectors.size();
  const auto preview_token = confirmed ? std::string() :
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  auto checked = base::BindOnce(
      [](base::WeakPtr<WorkspaceLab> lab, base::WeakPtr<content::WebContents> page,
         content::WeakDocumentPtr document, GURL url, uint64_t generation,
         std::string site, std::string target, std::vector<std::string> rules,
         std::vector<std::string> replacements, size_t selector_count,
         base::ListValue selectors, std::string preview_token,
         int exceptions, int scriptlets, bool confirmed,
         base::OnceCallback<void(base::Value)> done, base::Value value) {
        if (!lab || !lab->CanActOnAssistantPage(page.get()) ||
            document.AsRenderFrameHostIfValid() != page->GetPrimaryMainFrame() ||
            page->GetLastCommittedURL() != url ||
            lab->assistant_provider_generation_ != generation) {
          std::move(done).Run(AssistantToolError("The document or Assistant access changed."));
          return;
        }
        const auto* result = value.GetIfDict();
        const auto* checks = result ? result->FindList("rules") : nullptr;
        if (!checks || checks->size() != selector_count ||
            !std::ranges::all_of(*checks, [](const auto& check) {
              const auto* rule = check.GetIfDict();
              return rule && rule->FindBool("valid").value_or(false) &&
                     rule->FindInt("matches").value_or(0) > 0;
            })) {
          std::move(done).Run(AssistantToolError("A cosmetic rule matched nothing or included protected elements."));
          return;
        }
        if (!confirmed) {
          lab->ClearAssistantAdblockPreview();
          lab->assistant_page_approval_ = AssistantPageApproval{
              document, url, "adblock_add_rules", std::move(target), {}, preview_token,
              generation, std::move(selectors)};
          const auto prompt = prototype::TF(
              u"Zmienić blokowanie na {}? Reguły ({}), skrypty ({}), wyjątki ({}), zastępowane reguły ({}).",
              {base::UTF8ToUTF16(site), base::NumberToString16(rules.size()),
               base::NumberToString16(scriptlets), base::NumberToString16(exceptions),
               base::NumberToString16(replacements.size())});
          std::move(done).Run(base::Value(base::DictValue().Set("confirmation_required", true)
              .Set("prompt", base::UTF16ToUTF8(prompt)).Set("label", std::move(site))
              .Set("page_confirmation", preview_token)));
          return;
        }
        auto* profile = static_cast<ProfileContext*>(page->GetBrowserContext());
        auto current = profile->AdblockRules();
        if (!profile->IsAdblockEnabledFor(url) ||
            !std::ranges::all_of(replacements, [&](const auto& rule) {
              return std::ranges::contains(current, rule);
            })) {
          std::move(done).Run(AssistantToolError("The saved rules or site protection changed."));
          return;
        }
        std::erase_if(current, [&](const auto& rule) {
          return std::ranges::contains(replacements, rule);
        });
        int added = 0;
        for (auto& rule : rules) {
          if (!std::ranges::contains(current, rule)) {
            current.push_back(std::move(rule));
            ++added;
          }
        }
        if (!profile->SetAdblockRules(std::move(current))) {
          std::move(done).Run(AssistantToolError("The profile could not save the rules."));
          return;
        }
        ProfileContext::ClearAdblockTrial(*page);
        lab->RefreshAdBlockingRules();
        std::move(done).Run(base::Value(base::DictValue().Set("ok", true)
            .Set("saved", added).Set("replaced", static_cast<int>(replacements.size()))
            .Set("detail", "Saved in Settings > Ad blocking > Your rules. "
                           "Cosmetic filtering is updating on open pages. Reload for "
                           "document-start scripts and previously modified page content.")));
      }, weak_factory_.GetWeakPtr(), page->GetWeakPtr(), frame->GetWeakDocumentPtr(),
      on_site, assistant_provider_generation_, std::move(site), std::move(*target),
      std::move(rules), std::move(replacements), selector_count, selectors.Clone(),
      preview_token, exceptions,
      scriptlets, confirmed, std::move(done));
  if (selectors.empty()) {
    std::move(checked).Run(base::Value(base::DictValue().Set("rules", base::ListValue())));
    return;
  }
  prototype::RunAdblockCosmeticScript(
      frame, base::DictValue().Set("op", "check").Set("selectors", std::move(selectors))
                 .Set("token", preview_token),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(std::move(checked), base::Value()));
}

void WorkspaceLab::RunAssistantWorkspaceAction(
    std::string_view tool, const base::DictValue& args,
    const prototype::AssistantPermissions& permissions,
    base::OnceCallback<void(base::Value)> done) {
  auto fail = [&](std::string_view error) {
    std::move(done).Run(AssistantToolError(error));
  };
  auto report = [&](bool ok) {
    std::move(done).Run(base::Value(base::DictValue().Set("ok", ok)));
  };
  if (!permissions.actions || !assistant_host_settings_.defaults.actions) {
    fail("Browser actions permission is disabled.");
    return;
  }
  if (!SelectAssistantWorkspace(active_index_)) {
    fail("Workspace actions are unavailable now.");
    return;
  }
  if (tool == "browser_command") {
    static constexpr auto kCommands = std::to_array<std::string_view>({
        "new_tab", "split", "close_split", "reload", "back", "forward", "find",
        "settings", "downloads", "history", "timeline", "save_snapshot",
        "reopen", "sidebar"});
    const auto* id = args.FindString("id");
    if (!id || !std::ranges::contains(kCommands, *id)) {
      fail("Unknown or protected browser command.");
      return;
    }
    report(*id == "save_snapshot" ? SaveTimelineSnapshot("AI assistant")
                                   : RunShortcut(*id));
    return;
  }
  if (tool == "move_tab") {
    if (!permissions.tabs || !assistant_host_settings_.defaults.tabs) {
      fail("Moving tabs requires tab discovery permission.");
      return;
    }
    const auto* tab = args.FindString("tab");
    const auto* space = args.FindString("space");
    const auto source = tab ? FindAssistantTab(*tab) : std::nullopt;
    if (!source || !space || space->empty()) {
      fail("A listed tab and a Workspace are required.");
      return;
    }
    auto* profile = AssistantBrowserContext();
    auto destination = WorkspaceIndex(*space);
    if (!destination) {
      for (size_t i = 0; i < tabs_.size(); ++i) {
        if (WorkspaceProfile(i) != profile ||
            !base::EqualsCaseInsensitiveASCII(model_.workspaces()[i].name, *space)) {
          continue;
        }
        if (destination) {
          fail("Use a Workspace ID when names are shared.");
          return;
        }
        destination = i;
      }
    }
    if (!destination || *destination == source->first ||
        WorkspaceProfile(*destination) != profile ||
        tabs_[source->first].size() < 2 ||
        tabs_[*destination].size() >= prototype::kMaxWorkspaceSessionTabs ||
        AssistantTabUrl(tabs_[source->first][source->second]).empty() ||
        !CommitTabMove(source->first, source->second, *destination)) {
      fail("The tab could not be moved within this profile.");
      return;
    }
    report(true);
    return;
  }

  const auto* raw = args.FindString("name");
  const auto name = raw ? base::TrimWhitespaceASCII(*raw, base::TRIM_ALL)
                        : std::string_view();
  if (name.empty() || name.size() > 120) {
    fail("A name of up to 120 bytes is required.");
    return;
  }
  if (tool == "create_folder") {
    report(CreatePinnedFolder(model_.active()->id, {}, std::string(name)));
    return;
  }
  const auto profile = AssistantProfileId();
  AddWorkspace(std::string(name), profile, base::BindOnce(
      [](base::WeakPtr<WorkspaceLab> lab, std::string profile, uint64_t generation,
         base::OnceCallback<void(base::Value)> done, std::string space) {
        if (!lab || lab->AssistantProfileId() != profile ||
            lab->assistant_provider_generation_ != generation) {
          std::move(done).Run(AssistantToolError("The profile or Assistant access changed."));
          return;
        }
        if (space.empty()) {
          std::move(done).Run(AssistantToolError("The Workspace could not be created."));
          return;
        }
        std::move(done).Run(base::Value(base::DictValue().Set("ok", true)
                                                       .Set("space", std::move(space))));
      }, weak_factory_.GetWeakPtr(), profile, assistant_provider_generation_, std::move(done)));
}

bool WorkspaceLab::CanReadAssistantHistory(std::string_view profile,
                                            uint64_t generation) const {
  return !profile.empty() && profile == AssistantProfileId() &&
         generation == assistant_provider_generation_ && CanEditWorkspace() &&
         assistant_host_settings_.defaults.history;
}

void WorkspaceLab::SearchAssistantHistory(
    std::string_view query, base::OnceCallback<void(base::Value)> done) {
  if (query.size() > 600 || base::TrimWhitespaceASCII(query, base::TRIM_ALL).empty()) {
    std::move(done).Run(AssistantToolError("A history query is required."));
    return;
  }
  const auto profile = AssistantProfileId();
  const auto generation = assistant_provider_generation_;
  auto* service = CanReadAssistantHistory(profile, generation)
                      ? WorkspaceProfile(active_index_)->GetHistory()
                      : nullptr;
  if (!service) {
    std::move(done).Run(AssistantToolError("History is unavailable."));
    return;
  }
  history::QueryOptions options;
  options.max_count = 30;
  service->QueryHistory(
      base::UTF8ToUTF16(query), options,
      base::BindOnce(
          [](base::WeakPtr<WorkspaceLab> lab, std::string profile,
             uint64_t generation, base::OnceCallback<void(base::Value)> done,
             history::QueryResults rows) {
            if (!lab || !lab->CanReadAssistantHistory(profile, generation)) {
              std::move(done).Run(AssistantToolError("The profile or Assistant access changed."));
              return;
            }
            base::ListValue entries;
            for (const auto& row : rows) {
              auto url = AssistantUrl(row.url());
              if (!url.empty()) {
                entries.Append(base::DictValue()
                    .Set("title", base::UTF16ToUTF8(row.title()))
                    .Set("url", std::move(url)));
              }
            }
            std::move(done).Run(base::Value(std::move(entries)));
          }, weak_factory_.GetWeakPtr(), profile, generation, std::move(done)),
      &assistant_history_tasks_);
}

void WorkspaceLab::ReadAssistantDownloads(
    std::string profile, uint64_t generation, base::TimeTicks deadline,
    base::OnceCallback<void(base::Value)> done) {
  if (!CanReadAssistantHistory(profile, generation)) {
    std::move(done).Run(AssistantToolError("The profile or Assistant access changed."));
    return;
  }
  auto* manager = WorkspaceProfile(active_index_)->GetDownloadManager();
  if (!manager->IsManagerInitialized()) {
    if (base::TimeTicks::Now() >= deadline) {
      std::move(done).Run(AssistantToolError("Downloads are still loading."));
      return;
    }
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&WorkspaceLab::ReadAssistantDownloads,
                       weak_factory_.GetWeakPtr(), std::move(profile),
                       generation, deadline, std::move(done)),
        base::Milliseconds(100));
    return;
  }
  content::DownloadManager::DownloadVector items;
  manager->GetAllDownloads(&items);
  std::ranges::sort(items, std::ranges::greater{},
                    &download::DownloadItem::GetStartTime);
  base::ListValue entries;
  for (const auto& item : items) {
    if (entries.size() == 25)
      break;
    const auto state = item->GetState();
    entries.Append(base::DictValue()
        .Set("file", base::UTF16ToUTF8(item->GetTargetFilePath().BaseName().LossyDisplayName()))
        .Set("url", AssistantUrl(item->GetURL()))
        .Set("state", state == download::DownloadItem::COMPLETE ? "complete" :
                      state == download::DownloadItem::IN_PROGRESS ? "in_progress" :
                      state == download::DownloadItem::CANCELLED ? "cancelled" : "interrupted")
        .Set("percent", item->PercentComplete())
        .Set("bytes", static_cast<double>(item->GetTotalBytes())));
  }
  std::move(done).Run(base::Value(std::move(entries)));
}

void WorkspaceLab::NavigateAssistantTab(
    const GURL& url, base::OnceCallback<void(base::Value)> done) {
  auto* page = web_contents();
  if (!page || page->GetBrowserContext() != AssistantBrowserContext()) {
    std::move(done).Run(AssistantToolError("The tab is unavailable."));
    return;
  }
  const auto page_ref = page->GetWeakPtr();
  const auto workspace = model_.active()->id;
  const auto generation = assistant_provider_generation_;
  const auto navigation = page->GetController().LoadURL(
      url, content::Referrer(), ui::PAGE_TRANSITION_TYPED, {});
  const auto* entry = navigation ? navigation->GetNavigationEntry() : nullptr;
  if (!entry) {
    std::move(done).Run(AssistantToolError("The navigation could not be started."));
    return;
  }
  WaitForAssistantNavigation(page_ref, workspace, generation,
                             entry->GetUniqueID(),
                             base::TimeTicks::Now() + base::Seconds(20),
                             std::move(done));
}

void WorkspaceLab::WaitForAssistantNavigation(
    base::WeakPtr<content::WebContents> page, std::string workspace,
    uint64_t generation, int entry_id, base::TimeTicks deadline,
    base::OnceCallback<void(base::Value)> done) {
  if (!CanInspectAssistantTab(page.get(), workspace, generation)) {
    std::move(done).Run(AssistantToolError("The tab or Assistant access changed."));
    return;
  }

  auto& controller = page->GetController();
  const auto* pending = controller.GetPendingEntry();
  const auto* committed = controller.GetLastCommittedEntry();
  const bool matches = committed && committed->GetUniqueID() == entry_id;
  if (pending ? pending->GetUniqueID() == entry_id : matches && page->IsLoading()) {
    if (base::TimeTicks::Now() < deadline) {
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&WorkspaceLab::WaitForAssistantNavigation,
                         weak_factory_.GetWeakPtr(), page, std::move(workspace),
                         generation, entry_id, deadline, std::move(done)),
          base::Milliseconds(100));
      return;
    }
  } else if (!pending && matches && committed->GetPageType() == content::PAGE_TYPE_NORMAL) {
    const auto& url = page->GetLastCommittedURL();
    if (url.SchemeIsHTTPOrHTTPS() && !url.has_username() && !url.has_password()) {
      const auto [space, tab] = *TabForPage(page.get());
      std::move(done).Run(base::Value(base::DictValue().Set("ok", true)
          .Set("tab", "@tab:" + base::NumberToString(tabs_[space][tab].id))
          .Set("url", AssistantUrl(url))
          .Set("title", base::UTF16ToUTF8(page->GetTitle().substr(0, 8192)))));
      return;
    }
  }
  std::move(done).Run(AssistantToolError("The navigation did not complete."));
}

bool WorkspaceLab::SelectAssistantWorkspace(size_t workspace) {
  auto* profile = AssistantBrowserContext();
  if (!profile || profile->IsOffTheRecord() || !CanEditWorkspace() || PeekPage() ||
      sleeping_[workspace] || WorkspaceProfile(workspace) != profile)
    return false;
  if (workspace != active_index_)
    SelectWorkspace(model_.workspaces()[workspace].id);
  return workspace == active_index_ && AssistantBrowserContext() == profile;
}

base::Value WorkspaceLab::OpenAssistantPin(std::string_view target) {
  auto* profile = AssistantBrowserContext();
  if (!profile || profile->IsOffTheRecord() || !CanEditWorkspace() || PeekPage())
    return AssistantToolError("The pin cannot be opened now.");

  for (size_t workspace = 0; workspace < tabs_.size(); ++workspace) {
    if (sleeping_[workspace] || WorkspaceProfile(workspace) != profile)
      continue;
    const auto& space = model_.workspaces()[workspace];
    const auto pins = WorkspacePins(workspace);
    const auto pin = std::ranges::find_if(pins, [&](const auto& item) {
      return CanOpenAssistantPin(item) && (!item.profile_pin || workspace == active_index_) &&
             AssistantPinMention(space.id, item) == target;
    });
    if (pin == pins.end())
      continue;

    if (!SelectAssistantWorkspace(workspace))
      return AssistantToolError("The pin's Workspace could not be activated.");
    OpenPin(pin->id);
    const auto& tab = tabs_[workspace][selected_[workspace]];
    if (tab.pin != pin->id)
      return AssistantToolError("The pin could not be opened.");
    return base::Value(base::DictValue().Set("ok", true)
        .Set("detail", "Pin opened. Read the returned tab reference for page content.")
        .Set("tab", "@tab:" + base::NumberToString(tab.id)));
  }
  return AssistantToolError("The pin is unavailable in this profile.");
}

std::string WorkspaceLab::AssistantTabUrl(const Tab& tab) {
  auto* page = tab.view->web_contents();
  return AssistantUrl(page && !page->GetVisibleURL().is_empty()
                          ? page->GetVisibleURL() : tab.deferred_url);
}

std::optional<std::pair<size_t, size_t>> WorkspaceLab::FindAssistantTab(
    std::string_view target) const {
  auto* profile = AssistantBrowserContext();
  if (!profile || profile->IsOffTheRecord())
    return std::nullopt;
  if (target == "@current" || target == "current")
    return std::pair(active_index_, selected_[active_index_]);
  if (target.starts_with("@tab:"))
    target.remove_prefix(5);
  uint64_t id = 0;
  if (!base::StringToUint64(target, &id))
    return std::nullopt;
  for (size_t workspace = 0; workspace < tabs_.size(); ++workspace) {
    if (sleeping_[workspace] || WorkspaceProfile(workspace) != profile)
      continue;
    const auto& tabs = tabs_[workspace];
    const auto tab = std::ranges::find(tabs, id, &Tab::id);
    if (tab != tabs.end() && !AssistantTabUrl(*tab).empty())
      return std::pair(workspace, static_cast<size_t>(tab - tabs.begin()));
  }
  return std::nullopt;
}

bool WorkspaceLab::CanInspectAssistantTab(content::WebContents* page,
                                           std::string_view workspace,
                                           uint64_t generation) const {
  const auto position = TabForPage(page);
  auto* profile = AssistantBrowserContext();
  return position && profile && !profile->IsOffTheRecord() &&
         !sleeping_[position->first] && model_.workspaces()[position->first].id == workspace &&
         generation == assistant_provider_generation_ && !page->IsCrashed() &&
         page->GetBrowserContext() == profile && WorkspaceProfile(position->first) == profile;
}

bool WorkspaceLab::CanActOnAssistantPage(content::WebContents* page) const {
  if (!page || page != web_contents() || PeekPage() || !CanEditWorkspace() ||
      !widget_->IsVisible() || page->GetVisibility() == content::Visibility::HIDDEN ||
      (permission_prompt_widget_ && permission_prompt_widget_->IsVisible()) ||
      page->IsLoading() || page->IsCrashed() ||
      page->GetBrowserContext() != AssistantBrowserContext() ||
      !page->GetPrimaryMainFrame()->IsRenderFrameLive()) {
    return false;
  }
  const auto& url = page->GetLastCommittedURL();
  const auto* dialogs = prototype::PageDialogs::FromWebContents(page);
  return url.SchemeIsHTTPOrHTTPS() && !url.has_username() && !url.has_password() &&
         (!dialogs || (!dialogs->dialog_open() && !dialogs->checking_before_unload() &&
                       !dialogs->repost_dialog_open()));
}

std::optional<std::string> WorkspaceLab::TakeAssistantPageApproval(
    content::WebContents* page, std::string_view tool, std::string_view target,
    std::string_view token) {
  ClearAssistantAdblockPreview(/*invalidate=*/false);
  auto approval = std::exchange(assistant_page_approval_, std::nullopt);
  if (!approval || approval->token != token || approval->tool != tool ||
      approval->target != target ||
      approval->settings_generation != assistant_provider_generation_ ||
      approval->document.AsRenderFrameHostIfValid() != page->GetPrimaryMainFrame() ||
      approval->url != page->GetLastCommittedURL()) {
    return std::nullopt;
  }
  return std::move(approval->binding);
}

void WorkspaceLab::ClickAssistantTab(
    content::WebContents* page, const base::DictValue& args,
    prototype::AssistantPermissions permissions, bool user_confirmed,
    base::OnceCallback<void(base::Value)> done) {
  const auto* element = args.FindString("element");
  if (!element || element->empty()) {
    std::move(done).Run(AssistantToolError("A click needs a listed page element."));
    return;
  }
  if (!CanActOnAssistantPage(page)) {
    std::move(done).Run(AssistantToolError("The current tab cannot receive a click."));
    return;
  }
  auto* frame = page->GetPrimaryMainFrame();
  auto script_args = base::DictValue().Set("tool", "click").Set("element", *element)
      .Set("confirmed", user_confirmed || !permissions.confirm_clicks)
      .Set("native_only", true);
  if (const auto* token = args.FindString("_page_confirmation")) {
    const auto approval = user_confirmed
        ? TakeAssistantPageApproval(page, "click", *element, *token) : std::nullopt;
    if (!approval) {
      std::move(done).Run(AssistantToolError("The click approval expired. Read the page again."));
      return;
    }
    script_args.Set("expected_label", *approval);
  } else {
    ClearAssistantAdblockPreview();
    assistant_page_approval_.reset();
  }
  auto on_result = base::BindOnce(
      [](base::WeakPtr<WorkspaceLab> lab, base::WeakPtr<content::WebContents> page,
         content::WeakDocumentPtr document, GURL url, std::string element,
         uint64_t generation, base::OnceCallback<void(base::Value)> done, base::Value value) {
        if (!lab || !lab->CanActOnAssistantPage(page.get()) ||
            document.AsRenderFrameHostIfValid() != page->GetPrimaryMainFrame() ||
            url != page->GetLastCommittedURL() || generation != lab->assistant_provider_generation_) {
          std::move(done).Run(AssistantToolError("The document or Assistant access changed."));
          return;
        }
        auto* result = value.GetIfDict();
        if (!result) {
          std::move(done).Run(AssistantToolError("The page did not return a click target."));
          return;
        }
        if (result->FindBool("confirmation_required").value_or(false)) {
          const auto* label = result->FindString("label");
          auto token = base::Uuid::GenerateRandomV4().AsLowercaseString();
          result->Set("page_confirmation", token);
          lab->ClearAssistantAdblockPreview();
          lab->assistant_page_approval_ = AssistantPageApproval{
              document, std::move(url), "click", std::move(element), label ? *label : "",
              std::move(token), generation};
          std::move(done).Run(std::move(value));
          return;
        }
        const auto* point = result->FindDict("point");
        if (!point) {
          std::move(done).Run(std::move(value));
          return;
        }
        auto* view = page->GetRenderWidgetHostView();
        const auto x = point->FindDouble("x");
        const auto y = point->FindDouble("y");
        if (!view || !x || !y || !std::isfinite(*x) || !std::isfinite(*y) ||
            !gfx::RectF(view->GetViewBounds().size()).Contains(
                prototype::AssistantWidgetPoint(page.get(), *x, *y))) {
          std::move(done).Run(AssistantToolError("The click target is outside the visible tab."));
          return;
        }
        auto* host = view->GetRenderWidgetHost();
        host->Focus();
        prototype::ClickAssistantPageAt(page.get(), host, *x, *y);
        base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
            FROM_HERE, base::BindOnce(std::move(done), base::Value(base::DictValue().Set("ok", true))),
            base::Milliseconds(400));
      }, weak_factory_.GetWeakPtr(), page->GetWeakPtr(), frame->GetWeakDocumentPtr(),
      page->GetLastCommittedURL(), *element, assistant_provider_generation_, std::move(done));
  prototype::RunAssistantPageOperation(page, std::move(script_args),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(std::move(on_result), base::Value()));
}

void WorkspaceLab::PressAssistantPageKey(
    content::WebContents* page, const base::DictValue& args,
    prototype::AssistantPermissions permissions, bool user_confirmed,
    base::OnceCallback<void(base::Value)> done) {
  const auto* key = args.FindString("key");
  ui::KeyboardCode code;
  int flags;
  char16_t character;
  if (!key || key->size() > 64 || !prototype::ParseAssistantKey(*key, code, flags, character)) {
    std::move(done).Run(AssistantToolError("Unsupported or reserved browser key."));
    return;
  }
  auto* view = page ? page->GetRenderWidgetHostView() : nullptr;
  if (!view || !CanActOnAssistantPage(page)) {
    std::move(done).Run(AssistantToolError("The current tab cannot receive a key."));
    return;
  }

  std::optional<std::string> approved_focus;
  if (const auto* token = args.FindString("_page_confirmation")) {
    if (user_confirmed)
      approved_focus = TakeAssistantPageApproval(page, "press_key", *key, *token);
    if (!approved_focus) {
      std::move(done).Run(AssistantToolError("The key approval expired."));
      return;
    }
  } else {
    ClearAssistantAdblockPreview();
    assistant_page_approval_.reset();
  }

  auto* frame = page->GetPrimaryMainFrame();
  view->GetRenderWidgetHost()->Focus();
  const auto* focused_frame = page->GetFocusedFrame();
  const auto focused_id = focused_frame ? focused_frame->GetGlobalId()
                                        : content::GlobalRenderFrameHostId();
  auto on_focus = base::BindOnce(
      [](base::WeakPtr<WorkspaceLab> lab, base::WeakPtr<content::WebContents> page,
         content::WeakDocumentPtr document, GURL url, uint64_t generation,
         content::GlobalRenderFrameHostId focused_id, std::string key, ui::KeyboardCode code, int flags, char16_t character,
         bool confirmed, std::optional<std::string> approved_focus,
         base::OnceCallback<void(base::Value)> done, base::Value value) {
        if (!lab || !lab->CanActOnAssistantPage(page.get()) ||
            document.AsRenderFrameHostIfValid() != page->GetPrimaryMainFrame() ||
            url != page->GetLastCommittedURL() || generation != lab->assistant_provider_generation_) {
          std::move(done).Run(AssistantToolError("The document or Assistant access changed."));
          return;
        }
        const auto* focused_frame = page->GetFocusedFrame();
        if ((focused_frame ? focused_frame->GetGlobalId()
                           : content::GlobalRenderFrameHostId()) != focused_id) {
          std::move(done).Run(AssistantToolError("The focused frame changed."));
          return;
        }
        const auto* focus = value.GetIfDict();
        const auto* id = focus ? focus->FindString("id") : nullptr;
        if (!id) {
          std::move(done).Run(AssistantToolError("The focused field is protected or unavailable."));
          return;
        }
        if (approved_focus && *approved_focus != *id) {
          std::move(done).Run(AssistantToolError("The focus changed after the key approval."));
          return;
        }
        const bool modifier = flags & (ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN | ui::EF_COMMAND_DOWN);
        if (!confirmed && code == ui::VKEY_RETURN &&
            (modifier || !focus->FindBool("rich").value_or(false))) {
          auto token = base::Uuid::GenerateRandomV4().AsLowercaseString();
          lab->ClearAssistantAdblockPreview();
          lab->assistant_page_approval_ = AssistantPageApproval{
              document, std::move(url), "press_key", key, *id, token, generation};
          std::move(done).Run(base::Value(base::DictValue()
              .Set("confirmation_required", true).Set("label", std::move(key))
              .Set("key", true).Set("page_confirmation", std::move(token))));
          return;
        }
        auto* view = page->GetRenderWidgetHostView();
        if (!view) {
          std::move(done).Run(AssistantToolError("The page widget is unavailable."));
          return;
        }
        prototype::PressAssistantKey(view->GetRenderWidgetHost(), code, flags,
                                     character, /*page_only=*/true);
        base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
            FROM_HERE, base::BindOnce(std::move(done), base::Value(base::DictValue().Set("ok", true))),
            base::Milliseconds(400));
      }, weak_factory_.GetWeakPtr(), page->GetWeakPtr(), frame->GetWeakDocumentPtr(),
      page->GetLastCommittedURL(), assistant_provider_generation_, focused_id, *key, code, flags,
      character, user_confirmed || !permissions.confirm_clicks, std::move(approved_focus),
      std::move(done));
  prototype::RunAssistantPageOperation(page, base::DictValue().Set("tool", "key_focus"),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(std::move(on_focus), base::Value()));
}

void WorkspaceLab::EditAssistantPage(
    content::WebContents* page, std::string_view tool, const base::DictValue& args,
    base::OnceCallback<void(base::Value)> done) {
  if (!CanActOnAssistantPage(page)) {
    std::move(done).Run(AssistantToolError("The current tab cannot receive an edit."));
    return;
  }
  const auto* element = args.FindString("element");
  const auto* input = args.FindString(tool == "select" ? "value" : "text");
  if (!input || input->size() > 16000 ||
      (tool != "insert_text" && (!element || element->empty() || element->size() > 128))) {
    std::move(done).Run(AssistantToolError("The edit needs a valid value and target."));
    return;
  }

  auto operation = base::DictValue().Set("tool", tool).Set("edit_in_renderer", true)
      .Set(tool == "select" ? "value" : "text", *input);
  if (element)
    operation.Set("element", *element);
  auto on_result = base::BindOnce(
      [](base::WeakPtr<WorkspaceLab> lab, base::WeakPtr<content::WebContents> page,
         content::WeakDocumentPtr document, GURL url, uint64_t generation,
         base::OnceCallback<void(base::Value)> done, base::Value result) {
        if (!lab || !page || page->GetBrowserContext() != lab->AssistantBrowserContext() ||
            document.AsRenderFrameHostIfValid() != page->GetPrimaryMainFrame() ||
            url != page->GetLastCommittedURL() || generation != lab->assistant_provider_generation_) {
          std::move(done).Run(AssistantToolError("The document or Assistant access changed."));
          return;
        }
        std::move(done).Run(result.is_dict() ? std::move(result)
            : AssistantToolError("The page did not return an edit result."));
      }, weak_factory_.GetWeakPtr(), page->GetWeakPtr(),
      page->GetPrimaryMainFrame()->GetWeakDocumentPtr(), page->GetLastCommittedURL(),
      assistant_provider_generation_, std::move(done));
  prototype::RunAssistantPageOperation(page, std::move(operation),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(std::move(on_result), base::Value()));
}

void WorkspaceLab::InspectAssistantTab(
    base::WeakPtr<content::WebContents> page, std::string workspace,
    AssistantInspection inspection, uint64_t generation, base::TimeTicks deadline,
    base::OnceCallback<void(base::Value)> done) {
  if (!CanInspectAssistantTab(page.get(), workspace, generation)) {
    std::move(done).Run(AssistantToolError("The tab or Assistant access changed."));
    return;
  }
  if (page->IsLoading() && base::TimeTicks::Now() < deadline) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, base::BindOnce(&WorkspaceLab::InspectAssistantTab, weak_factory_.GetWeakPtr(),
                                  page, std::move(workspace), inspection, generation, deadline,
                                  std::move(done)),
        base::Milliseconds(100));
    return;
  }

  const auto url = page->GetLastCommittedURL();
  auto* frame = page->GetPrimaryMainFrame();
  if (page->IsLoading() || !url.SchemeIsHTTPOrHTTPS() || url.has_username() ||
      url.has_password() || !frame->IsRenderFrameLive()) {
    std::move(done).Run(AssistantToolError("Page is not ready or is protected."));
    return;
  }
  std::string site;
  if (inspection == AssistantInspection::kAdblock)
    site = prototype::AdblockSiteScope(url);
  auto on_result = base::BindOnce(
      [](base::WeakPtr<WorkspaceLab> lab, base::WeakPtr<content::WebContents> page,
         content::WeakDocumentPtr document, GURL url, std::string workspace,
         uint64_t generation, std::string site,
         base::OnceCallback<void(base::Value)> done, base::Value value) {
        if (!lab || !lab->CanInspectAssistantTab(page.get(), workspace, generation) ||
            document.AsRenderFrameHostIfValid() != page->GetPrimaryMainFrame() ||
            page->GetLastCommittedURL() != url) {
          std::move(done).Run(AssistantToolError("The document or Assistant access changed."));
          return;
        }
        auto* result = value.GetIfDict();
        if (!site.empty()) {
          if (!result || !result->FindList("candidates")) {
            std::move(done).Run(AssistantToolError("The page could not be inspected for ad blocking."));
            return;
          }
          const auto* profile = static_cast<ProfileContext*>(page->GetBrowserContext());
          base::ListValue requests;
          for (auto& request : ProfileContext::RecentAdblockRequests(*page))
            requests.Append(std::move(request));
          result->Set("site", std::move(site));
          result->Set("blocking", profile->IsAdblockEnabledFor(url) ? "on" : "off");
          result->Set("blocked_on_page", std::max(0, ProfileContext::AdblockCount(*page)));
          result->Set("blocked_requests", std::move(requests));
          result->Set("rule_scope", "Every rule must name this site: site##selector for elements, "
              "||host^$domain=site for third-party requests, or ||site/path for the site's own files.");
        }
        if (result && result->FindString("url"))
          result->Set("url", AssistantUrl(url));
        std::move(done).Run(std::move(value));
      }, weak_factory_.GetWeakPtr(), page, frame->GetWeakDocumentPtr(), url,
      std::move(workspace), generation, site, std::move(done));
  if (inspection == AssistantInspection::kScreenshot) {
    prototype::CaptureAssistantPage(page, std::move(on_result));
  } else if (inspection == AssistantInspection::kAdblock) {
    prototype::RunAssistantPageOperation(
        page.get(), base::DictValue().Set("tool", "adblock_inspect").Set("site", site),
        mojo::WrapCallbackWithDefaultInvokeIfNotRun(std::move(on_result), base::Value()));
  } else {
    prototype::ReadAssistantPage(page.get(), std::move(on_result));
  }
}

void WorkspaceLab::WithAssistantApiKey(
    base::OnceCallback<void(std::string)> done) {
  if (!os_crypt_ || !assistant_settings_ || assistant_settings_->encrypted_api_key.empty() ||
      !CanConfigureAssistant() || !assistant_settings_->enabled) {
    std::move(done).Run({});
    return;
  }
  os_crypt_->GetInstance(base::BindOnce(
      [](base::WeakPtr<WorkspaceLab> lab, std::string encrypted,
         base::OnceCallback<void(std::string)> done,
         scoped_refptr<os_crypt_async::Encryptor> encryptor) {
        if (lab && lab->assistant_settings_->enabled &&
            lab->assistant_settings_->encrypted_api_key == encrypted) {
          std::move(done).Run(prototype::DecryptAssistantApiKey(encrypted, *encryptor));
        }
      }, weak_factory_.GetWeakPtr(), assistant_settings_->encrypted_api_key, std::move(done)));
}

bool WorkspaceLab::AssistantStartBackground(
    const std::string&, const std::string&, const GURL&) {
  return false;
}

bool WorkspaceLab::AssistantHasBackground(const std::string&) const {
  return false;
}

bool WorkspaceLab::AssistantTakeOver(const std::string&) {
  return false;
}

void WorkspaceLab::AssistantDiscardBackground(const std::string&) {}

void WorkspaceLab::AssistantElementBounds(
    std::string target, std::string element,
    base::OnceCallback<void(std::optional<gfx::Rect>)> done) {
  const auto position = FindAssistantTab(target);
  auto* page = position ? tabs_[position->first][position->second].view->web_contents()
                        : nullptr;
  if (element.empty() || !CanActOnAssistantPage(page)) {
    std::move(done).Run(std::nullopt);
    return;
  }
  auto on_rect = base::BindOnce(
      [](base::WeakPtr<WorkspaceLab> lab, base::WeakPtr<content::WebContents> page,
         content::WeakDocumentPtr document,
         base::OnceCallback<void(std::optional<gfx::Rect>)> done, base::Value value) {
        const auto* rect = value.GetIfDict();
        if (!lab || !lab->CanActOnAssistantPage(page.get()) || !rect ||
            document.AsRenderFrameHostIfValid() != page->GetPrimaryMainFrame() ||
            !rect->FindDouble("width")) {
          std::move(done).Run(std::nullopt);
          return;
        }
        const auto origin = prototype::AssistantWidgetPoint(
            page.get(), rect->FindDouble("x").value_or(0), rect->FindDouble("y").value_or(0));
        const auto size = prototype::AssistantWidgetPoint(
            page.get(), rect->FindDouble("width").value_or(0), rect->FindDouble("height").value_or(0));
        gfx::Rect bounds = gfx::ToEnclosingRect(gfx::RectF(origin.x(), origin.y(), size.x(), size.y()));
        auto* view = lab->tabs_[lab->active_index_][lab->selected_[lab->active_index_]].view.get();
        bounds.Intersect(view->GetLocalBounds());
        if (bounds.IsEmpty()) {
          std::move(done).Run(std::nullopt);
          return;
        }
        views::View::ConvertRectToScreen(view, &bounds);
        std::move(done).Run(bounds);
      }, weak_factory_.GetWeakPtr(), page->GetWeakPtr(),
      page->GetPrimaryMainFrame()->GetWeakDocumentPtr(), std::move(done));
  prototype::RunAssistantPageOperation(page,
      base::DictValue().Set("tool", "rect").Set("element", std::move(element)).Set("preview_only", true),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(std::move(on_rect), base::Value()));
}

void WorkspaceLab::LoadAssistantSessions(
    base::OnceCallback<void(std::optional<base::DictValue>)> done) {
  session_writer_->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce([](base::FilePath path) {
        std::string json;
        return !base::IsLink(path) &&
                       base::ReadFileToStringWithMaxSize(path, &json, kMaxSessionBytes)
                   ? base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC)
                   : std::nullopt;
      }, session_path_.DirName().AppendASCII(kSessionsFile)), std::move(done));
}

void WorkspaceLab::SaveAssistantSessions(base::DictValue sessions) {
  session_writer_->PostTask(
      FROM_HERE, base::BindOnce([](base::FilePath path, base::DictValue sessions) {
        const auto json = base::WriteJson(sessions);
        if (json && json->size() <= kMaxSessionBytes && !base::IsLink(path))
          base::ImportantFileWriter::WriteFileAtomically(path, *json);
      }, session_path_.DirName().AppendASCII(kSessionsFile), std::move(sessions)));
}

void WorkspaceLab::AssistantStateChanged() {}

bool WorkspaceLab::NavigateFromBar(const GURL& url, bool new_tab) {
  return Navigate(url, new_tab);
}

void WorkspaceLab::CheckAssistantSettingsProbe(int step, bool reopening,
                                               base::TimeTicks deadline) {
  const bool enabled = assistant_settings_ && assistant_settings_->enabled;
  const bool configured = enabled && assistant_settings_->ollama_configured &&
                          assistant_settings_->default_model == "ollama/manual-fixture";
  const auto model_count = assistant_settings_ ? std::ranges::count_if(
      assistant_settings_->models, [](const auto& model) {
        return model.provider == kOllamaProvider;
      }) : 0;
  const auto cli_fixture = session_path_.DirName().AppendASCII(
#if BUILDFLAG(IS_WIN)
      "assistant-provider-fixture.exe"
#else
      "assistant-provider-fixture"
#endif
      ).AsUTF8Unsafe();
  bool advanced = false;
  switch (step) {
    case 0:
      if (!assistant_settings_)
        advanced = ActivateAssistantProbeControl(shell_->preference_switch_for_testing("assistant_enabled"));
      break;
    case 1:
      if (enabled && !model_count) {
        auto* input = shell_->assistant_add_model_for_testing();
        if (input)
          input->SetText(u"manual-fixture");
        advanced = input && ActivateAssistantProbeControl(shell_->assistant_add_button_for_testing());
      }
      break;
    case 2:
      if (configured && model_count == 1 && !assistant_settings_->defaults.tabs &&
          !assistant_settings_->defaults.pages && !assistant_settings_->defaults.actions &&
          !assistant_settings_->defaults.web) {
        WriteAssistantSettings(base::DictValue().Set("command", "discover")
                                   .Set("provider", kOllamaProvider));
        advanced = assistant_provider_checking_[kOllama];
      }
      break;
    case 3:
      if (!assistant_provider_checking_[kOllama] && configured && model_count == 2 &&
          assistant_provider_status_[kOllama].FindInt("tone") == 1) {
        {
          base::AutoReset busy(&saving_catalogue_, true);
          assistant_provider_checking_[kOllama] = true;
          AssistantProviderChecked(kOllama, true, assistant_provider_generation_,
              base::DictValue().Set("models", base::ListValue().Append("discarded-fixture")));
        }
        if (assistant_provider_checking_[kOllama] ||
            !assistant_provider_status_[kOllama].empty() ||
            std::ranges::any_of(assistant_settings_->models, [](const auto& model) {
              return model.id == "discarded-fixture";
            })) {
          LOG(ERROR) << "An interrupted Assistant provider check was not reset";
          Finish(false);
          return;
        }
        WriteAssistantSettings(base::DictValue().Set("command", "check")
                                   .Set("provider", kOllamaProvider));
        advanced = assistant_provider_checking_[kOllama];
      }
      break;
    case 4:
      if (!assistant_provider_checking_[kOllama] && assistant_provider_status_[kOllama].FindInt("tone") == 1)
        advanced = ActivateAssistantProbeControl(shell_->preference_switch_for_testing("assistant_tabs"));
      break;
    case 5:
    case 6:
      if (step == 5 && configured) {
        if (!assistant_settings_->defaults.pages) {
          ActivateAssistantProbeControl(shell_->preference_switch_for_testing("assistant_pages"));
          break;
        }
        if (!assistant_settings_->defaults.actions) {
          ActivateAssistantProbeControl(shell_->preference_switch_for_testing("assistant_actions"));
          break;
        }
        if (!assistant_settings_->defaults.history) {
          ActivateAssistantProbeControl(shell_->preference_switch_for_testing("assistant_history"));
          break;
        }
      }
      if (configured && CanEditWorkspace()) {
        const std::string_view provider = step == 5 ? "codex" : "claude";
        WriteAssistantSettings(base::DictValue().Set("command", "cli_path")
                                   .Set("provider", provider).Set("path", cli_fixture));
        advanced = (step == 5 ? assistant_settings_->codex_path
                              : assistant_settings_->claude_path) == cli_fixture;
      }
      break;
    case 7:
      if (CanEditWorkspace() && assistant_settings_->encrypted_api_key.empty()) {
        WriteAssistantSettings(base::DictValue().Set("command", "save_key")
                                   .Set("key", kProviderProbeKey));
        advanced = true;
      }
      break;
    case 8:
    case 9: {
      const size_t index = step == 8 ? kCodex : kClaude;
      if (CanEditWorkspace() && !assistant_settings_->encrypted_api_key.empty() &&
          !assistant_provider_checking_[index]) {
        const auto tone = assistant_provider_status_[index].FindInt("tone");
        if (!tone)
          CheckAssistantProvider(index, false);
        else if (*tone == 1)
          advanced = true;
        else {
          LOG(ERROR) << "The Assistant CLI provider check failed: " << index;
          Finish(false);
          return;
        }
      }
      break;
    }
    case 10:
      if (configured && CanEditWorkspace() && model_count == 2 &&
          assistant_settings_->defaults.tabs && assistant_settings_->defaults.pages &&
          assistant_settings_->defaults.actions && assistant_settings_->defaults.history &&
          assistant_settings_->defaults.confirm_clicks &&
          !assistant_settings_->defaults.web &&
          assistant_settings_->codex_path == cli_fixture &&
          assistant_settings_->claude_path == cli_fixture &&
          !assistant_settings_->encrypted_api_key.empty() &&
          assistant_provider_available_[kCodex] && assistant_provider_available_[kClaude] &&
          shell_->preference_switch_checked_for_testing("assistant_enabled") &&
          shell_->preference_switch_checked_for_testing("assistant_tabs") &&
          shell_->preference_switch_checked_for_testing("assistant_pages") &&
          shell_->preference_switch_checked_for_testing("assistant_actions") &&
          shell_->preference_switch_checked_for_testing("assistant_history")) {
        WithAssistantApiKey(base::BindOnce([](base::WeakPtr<WorkspaceLab> lab, bool reopening,
                                              base::TimeTicks deadline, std::string key) {
          if (!lab)
            return;
          lab->shell_->CloseOverlays(true);
          if (key != kProviderProbeKey ||
              !lab->ActivateWorkspaceCommandForProbe(u">assistant", "assistant") ||
              !lab->assistant_ || !lab->assistant_->IsDrawn()) {
            lab->Finish(false);
            return;
          }
          if (reopening) {
            lab->CheckAssistantSettingsProbe(11, reopening, deadline);
          } else {
            lab->SelectWorkspace(lab->model_.workspaces()[lab->restored_workspace_].id);
            std::puts("Zitter Content Assistant settings probe passed");
            lab->RequestClose();
          }
        }, weak_factory_.GetWeakPtr(), reopening, deadline));
        return;
      }
      break;
    case 11:
      if (CanEditWorkspace() && !assistant_settings_->encrypted_api_key.empty()) {
        WriteAssistantSettings(base::DictValue().Set("command", "remove_key"));
        advanced = assistant_settings_->encrypted_api_key.empty();
      }
      break;
    case 12:
      if (CanEditWorkspace() && assistant_settings_->encrypted_api_key.empty() &&
          !ReadAssistantSettings().FindBool("has_saved_key").value_or(true)) {
        CheckAssistantWorkspaceActions();
        return;
      }
      break;
  }
  if (base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "The Assistant settings probe timed out at step " << step;
    Finish(false);
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckAssistantSettingsProbe,
                                weak_factory_.GetWeakPtr(), step + advanced, reopening, deadline),
      base::Milliseconds(25));
}

void WorkspaceLab::CheckAssistantWorkspaceActions() {
  auto invoke = [](WorkspaceLab& lab, std::string tool, base::DictValue args,
                   prototype::AssistantPermissions permissions) {
    auto result = base::MakeRefCounted<base::RefCountedData<base::Value>>();
    lab.AssistantTool(std::move(tool), std::move(args), permissions,
        base::BindOnce([](scoped_refptr<base::RefCountedData<base::Value>> result,
                          base::Value value) { result->data = std::move(value); },
                       result), false, {});
    return result->data.Clone();
  };
  auto ok = [](const base::Value& value) {
    return value.is_dict() && value.GetDict().FindBool("ok").value_or(false);
  };
  const prototype::AssistantPermissions actions{.actions = true};
  const size_t initial_tab = selected_[active_index_];
  auto command = [&](std::string_view id, prototype::AssistantPermissions permissions) {
    return ok(invoke(*this, "browser_command", base::DictValue().Set("id", id),
                     permissions));
  };
  if (command("split", {}) || command("private_window", actions) ||
      !command("close_split", actions) || SplitShown() ||
      !command("split", actions) || !SplitShown() ||
      !command("close_split", actions) || SplitShown() ||
      !ok(invoke(*this, "create_folder", base::DictValue().Set("name", "Assistant folder"),
                 actions))) {
    LOG(ERROR) << "Assistant browser commands or folder creation failed";
    Finish(false);
    return;
  }
  SelectTab(initial_tab);
  auto* page = web_contents();
  const auto source = active_index_;
  const auto tab_id = tabs_[source][selected_[source]].id;
  AssistantTool("create_space", base::DictValue().Set("name", "Assistant space"),
                actions, base::BindOnce(
      [](base::WeakPtr<WorkspaceLab> lab, size_t source, uint64_t tab_id,
         base::WeakPtr<content::WebContents> page, decltype(invoke) invoke,
         decltype(ok) ok, base::Value result) {
        if (!lab)
          return;
        const auto* space = result.is_dict() ? result.GetDict().FindString("space") : nullptr;
        const auto destination = space ? lab->WorkspaceIndex(*space) : std::nullopt;
        auto fail = [&] {
          LOG(ERROR) << "Assistant Workspace creation or tab move failed";
          lab->Finish(false);
        };
        if (!ok(result) || !destination || !page ||
            lab->WorkspaceProfile(*destination) != lab->WorkspaceProfile(source)) {
          fail();
          return;
        }
        const auto tab = "@tab:" + base::NumberToString(tab_id);
        auto move = [&](std::string_view reference, std::string_view target,
                        prototype::AssistantPermissions permissions) {
          return ok(invoke(*lab, "move_tab",
                           base::DictValue().Set("tab", reference).Set("space", target),
                           permissions));
        };
        const prototype::AssistantPermissions move_permissions{.tabs = true, .actions = true};
        lab->SelectWorkspace(*space);
        auto* selected = lab->web_contents();
        if (lab->active_index_ != *destination ||
            move(tab, "personal", move_permissions) ||
            move(tab, *space, {.actions = true}) ||
            !move(tab, "assistant space", move_permissions) ||
            lab->web_contents() != selected ||
            lab->tabs_[source].size() != 1 ||
            lab->tabs_[*destination].back().id != tab_id ||
            lab->tabs_[*destination].back().view->web_contents() != page.get()) {
          fail();
          return;
        }
        const auto remaining = "@tab:" +
            base::NumberToString(lab->tabs_[source].front().id);
        if (move(remaining, *space, move_permissions)) {
          fail();
          return;
        }
        lab->SelectWorkspace(lab->model_.workspaces()[lab->restored_workspace_].id);
        std::puts("Zitter Content Assistant settings probe passed");
        lab->RequestClose();
      }, weak_factory_.GetWeakPtr(), source, tab_id, page->GetWeakPtr(), invoke, ok),
      false, {});
}

void WorkspaceLab::CheckAssistantPanelProbe(bool sent, int decisions,
                                             base::TimeTicks deadline) {
  if (sent && assistant_->HasLiveAnswerForTesting())
    assistant_probe_saw_live_answer_ = true;
  if (!sent && assistant_->sessions_loaded_for_testing()) {
    bool refused = false;
    AssistantTool("list_tabs", {}, {}, base::BindOnce([](bool* refused, base::Value result) {
      *refused = result.is_dict() && result.GetDict().FindString("error");
    }, &refused), false, {});
    auto rejected_tool = [this](std::string tool, std::string target,
                                prototype::AssistantPermissions permissions) {
      auto rejected = base::MakeRefCounted<base::RefCountedData<bool>>(false);
      auto args = base::DictValue().Set("tab", std::move(target));
      if (tool == "open_tab" || tool == "navigate")
        args.Set("url", session_probe_url_.spec());
      if (tool == "history")
        args.Set("query", "Legacy");
      AssistantTool(std::move(tool), std::move(args), permissions,
          base::BindOnce([](scoped_refptr<base::RefCountedData<bool>> rejected, base::Value result) {
            rejected->data = result.is_dict() && result.GetDict().FindString("error");
          }, rejected), false, {});
      return rejected->data;
    };
    const auto other_tab = "@tab:" + base::NumberToString(tabs_[restored_workspace_][0].id);
    if (!refused || !AssistantContext({}).empty() ||
        !rejected_tool("read_page", "@current", {.tabs = true}) ||
        !rejected_tool("read_page", "@current", {.pages = true}) ||
        !rejected_tool("read_page", other_tab, {.tabs = true, .pages = true}) ||
        !rejected_tool("adblock_inspect", "@current", {.tabs = true}) ||
        !rejected_tool("adblock_inspect", other_tab, {.tabs = true, .pages = true}) ||
        !rejected_tool("adblock_diagnose", "@current", {.tabs = true, .pages = true}) ||
        !rejected_tool("screenshot", "@current", {.tabs = true}) ||
        !rejected_tool("screenshot", other_tab, {.tabs = true, .pages = true}) ||
        !rejected_tool("activate_tab", other_tab, {.tabs = true, .actions = true}) ||
        tabs_.size() != 4 || !sleeping_[3] ||
        !rejected_tool("read_page", "@tab:" + base::NumberToString(tabs_[3][0].id),
                       {.tabs = true, .pages = true})) {
      Finish(false);
      return;
    }
    const auto research_pin = "@pin:" + model_.workspaces()[2].id + "/research-pin";
    const auto archive_pin = "@pin:" + model_.workspaces()[3].id + "/archive-pin";
    const auto folder = "@pin:" + model_.workspaces()[2].id + "/research-folder";
    const prototype::AssistantPermissions tab_actions{.tabs = true, .actions = true};
    if (!rejected_tool("activate_tab", research_pin, {.tabs = true}) ||
        !rejected_tool("pin_tab", "@current", {.tabs = true}) ||
        !rejected_tool("open_tab", "", {.tabs = true}) ||
        !rejected_tool("history", "", {.tabs = true, .pages = true}) ||
        !rejected_tool("downloads", "", {.tabs = true, .pages = true}) ||
        !rejected_tool("navigate", "@current", {.tabs = true}) ||
        !rejected_tool("close_tab", "@current", {.tabs = true}) ||
        !rejected_tool("navigate", other_tab, tab_actions) ||
        !rejected_tool("close_tab", other_tab, tab_actions) ||
        !rejected_tool("pin_tab", other_tab, tab_actions) ||
        !rejected_tool("unpin_tab", other_tab, tab_actions) ||
        !rejected_tool("activate_tab", "@pin:personal/personal-assistant", tab_actions) ||
        !rejected_tool("activate_tab", archive_pin, tab_actions) ||
        !rejected_tool("activate_tab", folder, tab_actions)) {
      Finish(false);
      return;
    }
    assistant_->CheckTasksForTesting(base::Time::Now());
    assistant_->SelectSessionForTesting(1);
    if (assistant_->working() ||
        assistant_->SelectedConversationContainsForTesting("Imported Personal reply")) {
      LOG(ERROR) << "An imported task or conversation escaped its profile";
      Finish(false);
      return;
    }
    assistant_->SelectSessionForTesting(0);
    if (!assistant_->SelectedConversationContainsForTesting("Imported Work reply")) {
      LOG(ERROR) << "The imported Work conversation did not reopen";
      Finish(false);
      return;
    }
    assistant_->StartNewConversation();
    assistant_->ConfigureForTesting(std::string(kOllamaProvider), "panel-fixture",
                                    assistant_->permissions_for_testing());
    assistant_->TypeForTesting(u"List the open Work tabs");
    if (!assistant_->KeyForTesting(ui::VKEY_RETURN)) {
      Finish(false);
      return;
    }
    sent = true;
  } else if (sent && decisions < 21 && shell_->assistant_consent_for_testing()) {
    if (decisions == 0 || decisions == 3 || decisions == 6 ||
        decisions == 12 || decisions == 15 || decisions == 18) {
      if (!assistant_->HandleEscape()) {
        Finish(false);
        return;
      }
    } else if (decisions == 1 || decisions == 4 ||
               (decisions >= 7 && decisions <= 10) ||
               decisions == 13 || decisions == 14 || decisions == 17 ||
               decisions == 19 || decisions == 20) {
      if (!ActivateAssistantProbeControl(
              prototype::AssistantConsentAllowButton(shell_->assistant_consent_for_testing()))) {
        Finish(false);
        return;
      }
    } else {
      const std::u16string_view script = decisions == 2
          ? u"document.querySelector('button').textContent = 'Changed action'"
          : decisions == 5 ? u"document.querySelector('[contenteditable]').focus()"
          : decisions == 11 ? u"history.replaceState(history.state, '', '#debug-approval-expired')"
                            : u"history.replaceState(history.state, '', '#adblock-save-approval-expired')";
      web_contents()->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
          std::u16string(script),
          base::BindOnce([](base::WeakPtr<WorkspaceLab> lab, int decisions,
                            base::TimeTicks deadline, base::Value) {
            if (!lab)
              return;
            auto* bubble = lab->shell_->assistant_consent_for_testing();
            if (!bubble || !ActivateAssistantProbeControl(
                               prototype::AssistantConsentAllowButton(bubble))) {
              lab->Finish(false);
              return;
            }
            base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
                FROM_HERE, base::BindOnce(&WorkspaceLab::CheckAssistantPanelProbe,
                                         lab, true, decisions, deadline),
                base::Milliseconds(25));
          }, weak_factory_.GetWeakPtr(), decisions + 1, deadline), content::ISOLATED_WORLD_ID_CONTENT_END);
      return;
    }
    ++decisions;
  } else if (sent && decisions == 21 && !assistant_->busy() &&
             assistant_->SelectedConversationContainsForTesting(kProbeAnswer)) {
    if (!assistant_probe_saw_live_answer_ || assistant_->HasLiveAnswerForTesting()) {
      LOG(ERROR) << "The Assistant did not replace its streaming answer view";
      Finish(false);
      return;
    }
    auto* page = web_contents();
    const auto original = page->GetLastCommittedURL();
    AssistantTool("screenshot", base::DictValue().Set("tab", "@current"),
        {.tabs = true, .pages = true},
        base::BindOnce([](base::WeakPtr<WorkspaceLab> lab, base::WeakPtr<content::WebContents> page,
                          base::TimeTicks deadline, base::Value value) {
          if (!lab)
            return;
          const auto* result = value.GetIfDict();
          const auto* error = result ? result->FindString("error") : nullptr;
          if (!page || !error || !error->contains("changed") ||
              result->Find("image") || page->IsBeingCaptured()) {
            LOG(ERROR) << "A screenshot survived the test navigation or leaked its capture count";
            lab->Finish(false);
            return;
          }
          page->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
              u"history.replaceState(history.state, '', location.pathname + location.search)",
              base::NullCallback(), content::ISOLATED_WORLD_ID_CONTENT_END);
          lab->CheckAssistantPanelProbe(true, 22, deadline);
        }, weak_factory_.GetWeakPtr(), page->GetWeakPtr(), deadline), false, {});
    page->GetController().LoadURL(original.Resolve("#assistant-screenshot-expired"),
                                  content::Referrer(), ui::PAGE_TRANSITION_TYPED, {});
    return;
  } else if (sent && decisions == 22 && !web_contents()->GetLastCommittedURL().has_ref()) {
    assistant_->SaveNowForTesting();
    SelectWorkspace(model_.workspaces()[restored_workspace_].id);
    if (assistant_->SelectedConversationContainsForTesting(kProbeAnswer) ||
        !assistant_->SelectedConversationContainsForTesting("Imported Personal reply")) {
      LOG(ERROR) << "The imported Personal conversation did not reopen in isolation";
      Finish(false);
      return;
    }
    std::puts("Zitter Content Assistant panel probe passed");
    RequestClose();
    return;
  }
  if (base::TimeTicks::Now() >= deadline) {
    LOG(ERROR) << "The Content Assistant panel did not finish its turn";
    Finish(false);
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&WorkspaceLab::CheckAssistantPanelProbe,
                                weak_factory_.GetWeakPtr(), sent, decisions, deadline),
      base::Milliseconds(25));
}

}  // namespace zitter::content_embedder
