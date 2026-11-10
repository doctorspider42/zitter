// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/prototype/night_shift.h"

#include <algorithm>
#include <string_view>

#include "base/files/file_path.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_reader.h"
#include "base/strings/escape.h"
#include "chrome/browser/extensions/component_loader.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "extensions/browser/api/storage/storage_area_namespace.h"
#include "extensions/browser/api/storage/storage_frontend.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/constants.h"
#include "night_shift_manifest.h"

namespace zitter::prototype {
namespace {

int Clamped(const base::DictValue* value, std::string_view key, int low, int high, int fallback) {
  const auto number = value ? value->FindInt(key) : std::nullopt;
  return number && *number >= low && *number <= high ? *number : fallback;
}

constexpr std::string_view kApplyFragment = "apply=";

class SettingsObserver final : public content::WebContentsObserver,
                               public content::WebContentsUserData<SettingsObserver> {
 public:
  ~SettingsObserver() override = default;
  void set_apply(base::RepeatingCallback<void(base::DictValue)> apply) { apply_ = std::move(apply); }

 private:
  friend class content::WebContentsUserData<SettingsObserver>;
  explicit SettingsObserver(content::WebContents* contents)
      : content::WebContentsObserver(contents),
        content::WebContentsUserData<SettingsObserver>(*contents) {}

  void DidFinishNavigation(content::NavigationHandle* navigation) override {
    if (!navigation->IsInPrimaryMainFrame() || !navigation->HasCommitted() ||
        !navigation->IsSameDocument() || !apply_) return;
    const GURL& url = navigation->GetURL();
    // Only the extension's own settings page can change its own URL.
    if (!url.SchemeIs(extensions::kExtensionScheme) || url.host() != kNightShiftId ||
        url.path() != "/settings.html" || !url.has_ref()) return;
    const std::string ref(url.ref());
    if (!ref.starts_with(kApplyFragment)) return;
    const auto json = base::UnescapeBinaryURLComponent(std::string_view(ref).substr(kApplyFragment.size()));
    auto value = base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
    if (value) apply_.Run(std::move(*value));
  }

  base::RepeatingCallback<void(base::DictValue)> apply_;
  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(SettingsObserver);

}  // namespace

void EnsureNightShift(Profile* profile) {
  if (!profile || !profile->IsRegularProfile()) {
    return;
  }
  auto* loader = extensions::ComponentLoader::Get(profile);
  if (!loader || loader->Exists(kNightShiftId)) {
    return;
  }
  loader->Add(kNightShiftManifest, base::FilePath(FILE_PATH_LITERAL("night_shift")));
}

bool NightShiftConfig::EnabledFor(const std::string& profile_id) const {
  return enabled && (all_profiles || profiles.contains(profile_id));
}

base::DictValue NightShiftConfig::ToValue() const {
  base::ListValue list;
  for (const auto& id : profiles) list.Append(id);
  return base::DictValue()
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

void PushNightShift(Profile* profile, bool enabled, const NightShiftConfig& config) {
  if (!profile || !profile->IsRegularProfile() || !config.managed) return;
  auto* registry = extensions::ExtensionRegistry::Get(profile);
  auto* storage = extensions::StorageFrontend::Get(profile);
  const auto* extension = registry ? registry->enabled_extensions().GetByID(kNightShiftId) : nullptr;
  if (!extension || !storage) return;
  auto defaults = base::DictValue()
      .Set("brightness", config.brightness)
      .Set("contrast", config.contrast)
      .Set("sepia", config.sepia)
      .Set("grayscale", config.grayscale)
      .Set("force", config.force);
  storage->Set(base::WrapRefCounted(extension), extensions::StorageAreaNamespace::kLocal,
               base::DictValue().Set("enabled", enabled).Set("defaults", std::move(defaults)),
               base::DoNothing());
}

void ObserveNightShiftSettings(content::WebContents* contents,
                               base::RepeatingCallback<void(base::DictValue)> apply) {
  if (!contents) return;
  SettingsObserver::CreateForWebContents(contents);
  SettingsObserver::FromWebContents(contents)->set_apply(std::move(apply));
}

}  // namespace zitter::prototype
