// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/prototype/assistant_settings.h"

#include <algorithm>
#include <utility>

#include "base/base64.h"
#include "base/files/file_path.h"
#include "components/os_crypt/async/common/encryptor.h"

namespace zitter::prototype {

std::string DecryptAssistantApiKey(
    std::string_view encoded, const os_crypt_async::Encryptor& encryptor) {
  std::string ciphertext;
  std::string plaintext;
  if (encoded.empty() || !base::Base64Decode(encoded, &ciphertext) ||
      !encryptor.DecryptString(ciphertext, &plaintext)) {
    return {};
  }
  return plaintext;
}

base::DictValue AssistantPermissions::ToValue() const {
  return base::DictValue().Set("tabs", tabs).Set("pages", pages).Set("actions", actions).Set("history", history).Set("web", web).Set("confirm_clicks", confirm_clicks);
}
AssistantSettings::AssistantSettings()
    // The CLIs pick their own current model ("latest"); other IDs are added by hand.
    : models({{"claude", "latest"}, {"openai", "gpt-6.1-sol"}, {"openai", "gpt-6.1-terra"}, {"openai", "gpt-6-astra"}, {"codex", "latest"}}),
      default_model("claude/latest") {}
AssistantSettings::AssistantSettings(const AssistantSettings&) = default;
AssistantSettings& AssistantSettings::operator=(const AssistantSettings&) = default;
AssistantSettings::~AssistantSettings() = default;
bool AssistantSettings::BackendEnabled(std::string_view provider) const {
  for (size_t i = 0; i < kAssistantProviders.size(); ++i) if (provider == kAssistantProviders[i]) return backends[i];
  return false;
}
bool AssistantSettings::BackendAvailable(std::string_view provider) const {
  const auto found = std::ranges::find(kAssistantProviders, provider);
  return found != kAssistantProviders.end() && available[found - kAssistantProviders.begin()];
}
void AssistantSettings::ApplyCliPaths(base::EnvironmentMap& environment) const {
  if (!codex_path.empty())
    environment[FILE_PATH_LITERAL("ZITTER_CODEX_PATH")] = base::FilePath::FromUTF8Unsafe(codex_path).value();
  if (!claude_path.empty())
    environment[FILE_PATH_LITERAL("ZITTER_CLAUDE_PATH")] = base::FilePath::FromUTF8Unsafe(claude_path).value();
}
std::vector<AssistantModel> AssistantSettings::PanelModels() const {
  std::vector<AssistantModel> result;
  for (const auto& model : models)
    if (model.visible && BackendEnabled(model.provider) && BackendAvailable(model.provider))
      result.push_back(model);
  return result;
}
base::DictValue AssistantSettings::ToValue() const {
  base::ListValue list, enabled_backends;
  for (const auto& model : models) list.Append(base::DictValue().Set("provider", model.provider).Set("id", model.id).Set("visible", model.visible));
  for (bool backend : backends) enabled_backends.Append(backend);
  base::DictValue remembered;
  for (const auto& [key, permissions] : shortcut_access)
    remembered.Set(key, permissions);
  return base::DictValue().Set("shortcut_access", std::move(remembered))
      .Set("enabled", enabled).Set("backends", std::move(enabled_backends)).Set("models", std::move(list))
      .Set("catalog_version", 5).Set("codex_path", codex_path).Set("claude_path", claude_path)
      .Set("ollama_configured", ollama_configured)
      .Set("encrypted_api_key", encrypted_api_key).Set("default_model", default_model).Set("permissions", defaults.ToValue()).Set("background", background)
      .Set("confirm_background", confirm_background).Set("concurrency", concurrency);
}
AssistantSettings AssistantSettings::FromValue(const base::DictValue* value) {
  AssistantSettings result;
  if (!value) return result;
  if (const auto* key=value->FindString("encrypted_api_key")) result.encrypted_api_key=*key;
  if (const auto* path = value->FindString("codex_path")) result.codex_path = *path;
  if (const auto* path = value->FindString("claude_path")) result.claude_path = *path;
  result.ollama_configured = value->FindBool("ollama_configured").value_or(false);
  result.enabled = value->FindBool("enabled").value_or(true);
  if (const auto* remembered = value->FindDict("shortcut_access")) {
    for (const auto [key, mask] : *remembered) {
      if (mask.is_int() && mask.GetInt() > 0 &&
          mask.GetInt() <= kAssistantShortcutAccessMask) {
        result.shortcut_access.emplace(key, mask.GetInt());
      }
    }
  }
  if (const auto* list = value->FindList("backends"); list && list->size() == result.backends.size())
    for (size_t i = 0; i < list->size(); ++i) result.backends[i] = (*list)[i].GetIfBool().value_or(true);
  if (const auto* list = value->FindList("models")) {
    std::vector<AssistantModel> models;
    for (const auto& entry : *list) {
      if (!entry.is_dict()) continue;
      const auto* provider = entry.GetDict().FindString("provider"); const auto* id = entry.GetDict().FindString("id");
      if (!provider || !id || id->empty() || id->size() > 120 ||
          std::ranges::find(kAssistantProviders, *provider) == kAssistantProviders.end()) continue;
      models.push_back({*provider, *id, entry.GetDict().FindBool("visible").value_or(true)});
      if (models.size() >= 32) break;
    }
    if (!models.empty()) result.models = std::move(models);
  }
  if (const auto* model = value->FindString("default_model")) result.default_model = *model;
  if(value->FindInt("catalog_version").value_or(0)<2){
    bool migrated=false;
    for(auto& model:result.models)if(model.provider=="codex"&&model.id=="gpt-5.4"){model.id="gpt-6.1-sol";migrated=true;}
    if(migrated)for(const char* id:{"gpt-6.1-terra","gpt-6-astra","gpt-6-luna"})
      if(result.models.size()<32&&std::ranges::none_of(result.models,[&](const auto& m){return m.provider=="codex"&&m.id==id;}))result.models.push_back({"codex",id});
    if(result.default_model=="codex/gpt-5.4")result.default_model="codex/gpt-6.1-sol";
  }
  if (value->FindInt("catalog_version").value_or(0) < 3) {
    for (auto& model : result.models) {
      if (model.provider == "openai" && model.id == "gpt-5.4") model.id = "gpt-6.1-sol";
      if (model.provider == "openai" && model.id == "gpt-5.4-mini") model.id = "gpt-6.1-terra";
      if (model.provider == "ollama" && model.id != "qwen3:8b") result.ollama_configured = true;
    }
    if (result.default_model == "openai/gpt-5.4") result.default_model = "openai/gpt-6.1-sol";
    if (result.default_model == "openai/gpt-5.4-mini") result.default_model = "openai/gpt-6.1-terra";
    for (const char* id : {"gpt-6.1-sol", "gpt-6.1-terra", "gpt-6-astra"})
      if (result.models.size() < 32 && std::ranges::none_of(result.models, [&](const auto& model) {
            return model.provider == "openai" && model.id == id;
          })) result.models.push_back({"openai", id});
  }
  if (value->FindInt("catalog_version").value_or(0) < 4) {
    // Codex moved its flagship and its balanced model to 6.2: the old defaults
    // become the new ones in place, keeping their visibility and the default.
    for (const auto& [old_id, new_id] : {std::pair{"gpt-6.1-sol", "gpt-6.2-sol"}, std::pair{"gpt-6-astra", "gpt-6.2-astra"}}) {
      if (std::ranges::any_of(result.models, [&](const auto& m) { return m.provider == "codex" && m.id == new_id; })) continue;
      for (auto& model : result.models)
        if (model.provider == "codex" && model.id == old_id) model.id = new_id;
      if (result.default_model == std::string("codex/") + old_id) result.default_model = std::string("codex/") + new_id;
    }
  }
  if (value->FindInt("catalog_version").value_or(0) < 5) {
    // Each CLI's own current model ("latest") leads its models, so a new model
    // works without a Zitter release. Saved IDs stay; the shipped default moves.
    for (const char* provider : {"claude", "codex"}) {
      const auto first = std::ranges::find(result.models, std::string(provider), &AssistantModel::provider);
      if (result.models.size() < 32 &&
          std::ranges::none_of(result.models, [&](const auto& m) { return m.provider == provider && m.id == "latest"; }))
        result.models.insert(first, {provider, "latest"});
    }
    if (result.default_model == "claude/sonnet") result.default_model = "claude/latest";
  }
  if (const auto* permissions = value->FindDict("permissions")) {
    auto flag = [&](const char* key, bool fallback) { return permissions->FindBool(key).value_or(fallback); };
    result.defaults = {flag("tabs", false), flag("pages", false), flag("actions", false), flag("history", false), flag("web", true), flag("confirm_clicks", true)};
  }
  result.background = value->FindBool("background").value_or(true);
  result.confirm_background = value->FindBool("confirm_background").value_or(true);
  result.concurrency = std::clamp(value->FindInt("concurrency").value_or(12), 1, 12);
  return result;
}

}  // namespace zitter::prototype
