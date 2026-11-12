// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CHROMIUM_PROTOTYPE_ASSISTANT_PROVIDERS_H_
#define ZITTER_CHROMIUM_PROTOTYPE_ASSISTANT_PROVIDERS_H_

#include <array>
#include <string>
#include <string_view>

namespace zitter::prototype {

// Backends in their stable order: openai, codex, claude, ollama.
inline constexpr std::array<const char*, 4> kAssistantProviders = {"openai", "codex", "claude", "ollama"};
std::u16string AssistantProviderLabel(std::string_view provider);

}  // namespace zitter::prototype

#endif  // ZITTER_CHROMIUM_PROTOTYPE_ASSISTANT_PROVIDERS_H_
