// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_FRAME_ZITTER_FINGERPRINT_PROTECTION_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_FRAME_ZITTER_FINGERPRINT_PROTECTION_H_

#include <optional>

#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/platform/graphics/zitter_fingerprint_noise.h"

namespace blink {

class ExecutionContext;

// Zitter: the noise for reads made by `context`, or nullopt when the browser
// left fingerprint protection off for its page. Workers and worklets get none.
CORE_EXPORT std::optional<ZitterFingerprintNoise> ZitterFingerprintNoiseFor(
    ExecutionContext* context);

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_FRAME_ZITTER_FINGERPRINT_PROTECTION_H_
