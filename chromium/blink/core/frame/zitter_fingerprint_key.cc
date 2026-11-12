// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "third_party/blink/renderer/core/frame/zitter_fingerprint_key.h"

#include "third_party/blink/public/common/renderer_preferences/renderer_preferences.h"
#include "third_party/blink/renderer/core/page/page.h"

namespace blink {

base::span<const uint8_t> ZitterFingerprintKeyFor(const Page& page) {
  return page.GetRendererPreferences().zitter_fingerprint_key;
}

}  // namespace blink
