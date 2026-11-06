// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_ASSISTANT_NATIVE_TRANSPORT_H_
#define ZITTER_CHROMIUM_ASSISTANT_NATIVE_TRANSPORT_H_
#include "third_party/rust/cxx/v1/cxx.h"
namespace zitter::assistant {
rust::String transport_http(rust::Str id, rust::Str request);
rust::String transport_cli(rust::Str id, rust::Str request);
bool cancelled(rust::Str id);
rust::String request_key(rust::Str input);
void publish_progress(rust::Str id, rust::Str text);
}  // namespace zitter::assistant
#endif
