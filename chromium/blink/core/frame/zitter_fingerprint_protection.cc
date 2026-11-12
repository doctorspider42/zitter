// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "third_party/blink/renderer/core/frame/zitter_fingerprint_protection.h"

#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/zitter_fingerprint_key.h"

namespace blink {

std::optional<ZitterFingerprintNoise> ZitterFingerprintNoiseFor(
    ExecutionContext* context) {
  auto* window = DynamicTo<LocalDOMWindow>(context);
  Page* page = window && window->GetFrame() ? window->GetFrame()->GetPage()
                                             : nullptr;
  if (!page) {
    return std::nullopt;
  }
  const auto key = ZitterFingerprintKeyFor(*page);
  if (key.empty()) {
    return std::nullopt;
  }
  // Partitioned like storage: iframes share their top-level site's noise.
  return ZitterFingerprintNoise::ForSite(
      key, window->GetStorageKey().GetTopLevelSite().Serialize());
}

}  // namespace blink
