// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_PROTOTYPE_ASSISTANT_SETTINGS_H_
#define ZITTER_CHROMIUM_PROTOTYPE_ASSISTANT_SETTINGS_H_

#include <array>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "base/environment.h"
#include "base/values.h"
#include "chromium/prototype/assistant_providers.h"

namespace os_crypt_async { class Encryptor; }

namespace zitter::prototype {

// Returns an empty string when there is no usable stored credential.
std::string DecryptAssistantApiKey(std::string_view encoded,
                                   const os_crypt_async::Encryptor& encryptor);

struct AssistantPermissions {
  bool tabs = false, pages = false, actions = false, history = false, web = true;
  bool confirm_clicks = true;
  base::DictValue ToValue() const;
  bool operator==(const AssistantPermissions&) const = default;
};
struct AssistantShortcut {
  std::string id;
  std::u16string title;
  AssistantPermissions required;
};
// The serialized mask uses pages, web, actions, tabs and history in that order.
inline constexpr int kAssistantShortcutAccessMask = (1 << 5) - 1;
struct AssistantModel { std::string provider, id; bool visible = true; };
// App-wide assistant settings (Settings → Asystent AI). Sessions copy the
// permission defaults when they start; narrowing applies to running sessions.
struct AssistantSettings {
  AssistantSettings();
  AssistantSettings(const AssistantSettings&);
  AssistantSettings& operator=(const AssistantSettings&);
  ~AssistantSettings();
  bool enabled = true;
  std::array<bool, 4> backends{true, true, true, true};
  std::array<bool, 4> available{};
  std::string codex_path, claude_path;
  bool ollama_configured = false;
  std::vector<AssistantModel> models;
  std::string encrypted_api_key;  // OS-encrypted, base64 encoded; never sent to the UI.
  std::string default_model;  // "<provider>/<id>"
  AssistantPermissions defaults;
  std::map<std::string, int> shortcut_access;
  bool background = true, confirm_background = true;
  int concurrency = 12;
  bool BackendEnabled(std::string_view provider) const;
  bool BackendAvailable(std::string_view provider) const;
  void ApplyCliPaths(base::EnvironmentMap& environment) const;
  // Visible models of enabled backends, in settings order.
  std::vector<AssistantModel> PanelModels() const;
  base::DictValue ToValue() const;
  static AssistantSettings FromValue(const base::DictValue* value);
};
}  // namespace zitter::prototype

#endif  // ZITTER_CHROMIUM_PROTOTYPE_ASSISTANT_SETTINGS_H_
