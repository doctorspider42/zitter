// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CONTENT_PASSWORD_AUTHENTICATOR_MAC_H_
#define ZITTER_CONTENT_PASSWORD_AUTHENTICATOR_MAC_H_

#include <memory>

namespace device_reauth {
class DeviceAuthenticator;
}

namespace zitter::content_embedder {

std::unique_ptr<device_reauth::DeviceAuthenticator>
CreateMacPasswordAuthenticator();

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_PASSWORD_AUTHENTICATOR_MAC_H_
