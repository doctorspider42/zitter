// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_FRAME_ZITTER_FINGERPRINT_KEY_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_FRAME_ZITTER_FINGERPRINT_KEY_H_

#include <cstdint>

#include "base/containers/span.h"

namespace blink {

class Page;

base::span<const uint8_t> ZitterFingerprintKeyFor(const Page& page);

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_FRAME_ZITTER_FINGERPRINT_KEY_H_
