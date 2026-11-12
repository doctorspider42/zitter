// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "third_party/blink/renderer/core/frame/zitter_fingerprint_key.h"

#include <array>

#include "third_party/blink/public/web/zitter_fingerprint_state.h"

namespace blink {
namespace {

struct FingerprintState {
  uint64_t sequence = 0;
  std::array<uint8_t, 32> key = {};
  bool enabled = false;
};

FingerprintState& State() {
  static FingerprintState state;
  return state;
}

}  // namespace

void SetZitterFingerprintState(uint64_t sequence, base::span<const uint8_t> key) {
  auto& state = State();
  if (sequence <= state.sequence ||
      (!key.empty() && key.size() != state.key.size())) {
    return;
  }
  state.sequence = sequence;
  state.enabled = !key.empty();
  if (state.enabled) {
    base::span(state.key).copy_from(key);
  } else {
    state.key.fill(0);
  }
}

base::span<const uint8_t> ZitterFingerprintKeyFor(const Page&) {
  const auto& state = State();
  return state.enabled ? base::span<const uint8_t>(state.key)
                       : base::span<const uint8_t>();
}

}  // namespace blink
