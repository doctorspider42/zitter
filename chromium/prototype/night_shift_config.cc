// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "chromium/prototype/night_shift_config.h"

#include <optional>
#include <string_view>

namespace zitter::prototype {
namespace {

int Clamped(const base::DictValue* value, std::string_view key, int low, int high, int fallback) {
  const auto number = value ? value->FindInt(key) : std::nullopt;
  return number && *number >= low && *number <= high ? *number : fallback;
}

}  // namespace

bool NightShiftConfig::EnabledFor(const std::string& profile_id) const {
  if (const auto it = profile_overrides.find(profile_id); it != profile_overrides.end()) return it->second;
  return enabled && (all_profiles || profiles.contains(profile_id));
}

base::DictValue NightShiftConfig::ToValue() const {
  base::ListValue list;
  for (const auto& id : profiles) list.Append(id);
  base::DictValue overrides;
  for (const auto& [id, on] : profile_overrides) overrides.Set(id, on);
  return base::DictValue()
      .Set("profile_overrides", std::move(overrides))
      .Set("enabled", enabled)
      .Set("all_profiles", all_profiles)
      .Set("profiles", std::move(list))
      .Set("brightness", brightness)
      .Set("contrast", contrast)
      .Set("sepia", sepia)
      .Set("grayscale", grayscale)
      .Set("force", force);
}

NightShiftConfig NightShiftConfig::FromValue(const base::DictValue* value) {
  NightShiftConfig config;
  if (!value) return config;
  config.managed = true;
  config.enabled = value->FindBool("enabled").value_or(false);
  config.all_profiles = value->FindBool("all_profiles").value_or(true);
  if (const auto* overrides = value->FindDict("profile_overrides"))
    for (const auto [id, on] : *overrides)
      if (on.is_bool()) config.profile_overrides.emplace(id, on.GetBool());
  if (const auto* list = value->FindList("profiles")) {
    for (const auto& id : *list)
      if (id.is_string() && !id.GetString().empty()) config.profiles.insert(id.GetString());
  }
  config.brightness = Clamped(value, "brightness", 75, 125, 100);
  config.contrast = Clamped(value, "contrast", 75, 125, 100);
  config.sepia = Clamped(value, "sepia", 0, 50, 0);
  config.grayscale = Clamped(value, "grayscale", 0, 100, 0);
  config.force = value->FindBool("force").value_or(false);
  return config;
}

}  // namespace zitter::prototype
