// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <set>
#include <string>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/json/values_util.h"
#include "base/logging.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "extensions/common/api/alarms.h"
#include "third_party/leveldatabase/env_chromium.h"
#include "third_party/leveldatabase/src/include/leveldb/db.h"
#include "third_party/leveldatabase/src/include/leveldb/write_batch.h"

namespace {

constexpr size_t kMaxAlarms = 500;
constexpr size_t kMaxAlarmBytes = 1024 * 1024;

std::optional<std::string> ValidateAlarms(const std::string& serialized) {
  if (serialized.size() > kMaxAlarmBytes)
    return std::nullopt;
  auto alarms = base::JSONReader::ReadList(serialized, base::JSON_PARSE_RFC);
  if (!alarms || alarms->size() > kMaxAlarms)
    return std::nullopt;

  std::set<std::string> names;
  base::ListValue normalized;
  for (const auto& value : *alarms) {
    if (!value.is_dict())
      return std::nullopt;
    auto alarm_value = value.GetDict().Clone();
    if (!alarm_value.contains("persistAcrossSessions"))
      alarm_value.Set("persistAcrossSessions", true);
    auto alarm = extensions::api::alarms::Alarm::FromValue(alarm_value);
    if (!alarm || !alarm->persist_across_sessions ||
        !std::isfinite(alarm->scheduled_time) ||
        base::Time::FromMillisecondsSinceUnixEpoch(alarm->scheduled_time)
            .is_inf() ||
        (alarm->period_in_minutes &&
         (!std::isfinite(*alarm->period_in_minutes) ||
          *alarm->period_in_minutes <= 0 ||
          base::Minutes(*alarm->period_in_minutes).is_inf())) ||
        !names.insert(alarm->name).second) {
      return std::nullopt;
    }
    auto fields = alarm->ToValue();
    if (const auto* granularity = alarm_value.Find("granularity")) {
      const auto delta = base::ValueToTimeDelta(granularity);
      if (!delta || delta->is_inf())
        return std::nullopt;
      fields.Set("granularity", granularity->Clone());
    }
    normalized.Append(std::move(fields));
  }
  return base::WriteJson(normalized);
}

int ImportAlarms(const base::CommandLine::StringVector& args) {
  if (args.size() < 3)
    return 1;
  const base::FilePath source(args[0]);
  const base::FilePath destination(args[1]);
  if (!source.IsAbsolute() || !destination.IsAbsolute() ||
      !base::DirectoryExists(source) || base::PathExists(destination)) {
    return 1;
  }

  leveldb_env::Options options;
  options.paranoid_checks = true;
  std::unique_ptr<leveldb::DB> source_db;
  if (!leveldb_env::OpenDB(options, source.AsUTF8Unsafe(), &source_db).ok())
    return 1;

  leveldb::WriteBatch batch;
  bool has_alarms = false;
  leveldb::ReadOptions read_options;
  read_options.verify_checksums = true;
  for (size_t i = 2; i < args.size(); ++i) {
    const std::string id = base::FilePath(args[i]).AsUTF8Unsafe();
    if (id.size() != 32 ||
        !std::ranges::all_of(id, [](char c) { return c >= 'a' && c <= 'p'; })) {
      return 1;
    }
    const std::string key = id + ".alarms";
    std::string serialized;
    const auto result = source_db->Get(read_options, key, &serialized);
    if (result.IsNotFound())
      continue;
    if (!result.ok())
      return 1;
    auto alarms = ValidateAlarms(serialized);
    if (!alarms)
      return 1;
    batch.Put(key, *alarms);
    has_alarms = true;
  }
  if (!has_alarms)
    return 0;

  options.create_if_missing = true;
  options.error_if_exists = true;
  std::unique_ptr<leveldb::DB> destination_db;
  if (!leveldb_env::OpenDB(options, destination.AsUTF8Unsafe(), &destination_db)
           .ok()) {
    return 1;
  }
  leveldb::WriteOptions write_options;
  write_options.sync = true;
  return destination_db->Write(write_options, &batch).ok() ? 0 : 1;
}

}  // namespace

#if BUILDFLAG(IS_WIN)
int wmain(int argc, wchar_t** argv) {
  base::CommandLine::Init(0, nullptr);
#else
int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
#endif
  base::AtExitManager at_exit;
  base::ThreadPoolInstance::CreateAndStartWithDefaultParams("AlarmImport");
  const int result = ImportAlarms(base::CommandLine::ForCurrentProcess()->GetArgs());
  base::ThreadPoolInstance::Get()->Shutdown();
  if (result)
    LOG(ERROR) << "Could not import verified extension alarm state";
  return result;
}
