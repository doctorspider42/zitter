// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/assistant/native_transport.h"
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "crypto/hash.h"
namespace zitter::assistant {
// The helper owns operation lifetimes; these callbacks also make cancellation
// and progress observable without taking a Rust session mutex.
struct Operation {
  std::atomic<bool> stopped{false};
  base::TimeTicks started = base::TimeTicks::Now();
  std::mutex mutex;
  std::string progress;
};
namespace {
struct Operations {
  std::mutex mutex;
  std::map<std::string, std::shared_ptr<Operation>> items;
};
Operations& Ops() {
  static base::NoDestructor<Operations> state;
  return *state;
}
}  // namespace
std::shared_ptr<Operation> FindOperation(std::string_view id) {
  std::lock_guard lock(Ops().mutex);
  auto it = Ops().items.find(std::string(id));
  return it == Ops().items.end() ? nullptr : it->second;
}
bool BeginOperation(std::string_view id) {
  std::lock_guard lock(Ops().mutex);
  return Ops()
      .items.emplace(std::string(id), std::make_shared<Operation>())
      .second;
}
void EndOperation(std::string_view id) {
  std::lock_guard lock(Ops().mutex);
  Ops().items.erase(std::string(id));
}
void CancelOperation(std::string_view id) {
  if (auto op = FindOperation(id))
    op->stopped = true;
}
void CancelAllOperations() {
  std::lock_guard lock(Ops().mutex);
  for (auto& [id, operation] : Ops().items)
    operation->stopped = true;
}
std::string Progress(std::string_view id) {
  if (auto op = FindOperation(id)) {
    std::lock_guard lock(op->mutex);
    return op->progress;
  }
  return {};
}
rust::String request_key(rust::Str input) {
  return base::HexEncode(crypto::hash::Sha256(std::string_view(input)));
}
bool cancelled(rust::Str id) {
  auto op = FindOperation(std::string_view(id));
  return !op || op->stopped ||
         base::TimeTicks::Now() - op->started > base::Minutes(10);
}
void publish_progress(rust::Str id, rust::Str text) {
  if (auto op = FindOperation(std::string_view(id)); op && !op->stopped) {
    std::lock_guard lock(op->mutex);
    op->progress = std::string(text);
  }
}
// Implemented in native_http.cc and native_cli.cc; raw errors stay private.
std::string PerformHTTP(std::string id, const base::DictValue& request);
std::string PerformCLI(std::string id, const base::DictValue& request);
rust::String transport_http(rust::Str id, rust::Str request) {
  auto value = base::JSONReader::ReadDict(std::string_view(request),
                                          base::JSON_PARSE_RFC);
  return value ? PerformHTTP(std::string(id), *value)
               : "{\"error\":\"Invalid request\"}";
}
rust::String transport_cli(rust::Str id, rust::Str request) {
  auto value = base::JSONReader::ReadDict(std::string_view(request),
                                          base::JSON_PARSE_RFC);
  return value ? PerformCLI(std::string(id), *value)
               : "{\"error\":\"Invalid request\"}";
}
}  // namespace zitter::assistant
