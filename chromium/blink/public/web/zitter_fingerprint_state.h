// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef THIRD_PARTY_BLINK_PUBLIC_WEB_ZITTER_FINGERPRINT_STATE_H_
#define THIRD_PARTY_BLINK_PUBLIC_WEB_ZITTER_FINGERPRINT_STATE_H_

#include <cstdint>

#include "base/containers/span.h"
#include "third_party/blink/public/platform/web_common.h"

namespace blink {

// Renderer main thread only. An empty key disables protection.
BLINK_EXPORT void SetZitterFingerprintState(uint64_t sequence,
                                           base::span<const uint8_t> key);

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_PUBLIC_WEB_ZITTER_FINGERPRINT_STATE_H_
