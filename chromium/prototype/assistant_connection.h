// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_PROTOTYPE_ASSISTANT_CONNECTION_H_
#define ZITTER_CHROMIUM_PROTOTYPE_ASSISTANT_CONNECTION_H_

#include <string>
#include <string_view>

#include "base/environment.h"
#include "base/values.h"

namespace zitter::prototype {

// Runs a provider check in a fresh helper. The caller runs this on a worker.
base::DictValue CheckAssistantProvider(std::string provider, std::string model,
                                       bool discover, base::EnvironmentMap environment,
                                       std::string api_key);
std::u16string AssistantConnectionError(std::string_view message);

}  // namespace zitter::prototype

#endif  // ZITTER_CHROMIUM_PROTOTYPE_ASSISTANT_CONNECTION_H_
