// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/prototype/night_shift.h"

#include <algorithm>
#include <string_view>

#include "base/files/file_path.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/escape.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/browser/extensions/component_loader.h"
#include "chrome/browser/profiles/profile.h"
#include "components/sessions/content/session_tab_helper.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "extensions/browser/api/storage/storage_area_namespace.h"
#include "extensions/browser/api/storage/storage_frontend.h"
#include "extensions/browser/extension_api_frame_id_map.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/script_executor.h"
#include "extensions/common/constants.h"
#include "extensions/common/extension.h"
#include "extensions/common/mojom/code_injection.mojom.h"
#include "extensions/common/mojom/host_id.mojom.h"
#include "extensions/common/mojom/match_origin_as_fallback.mojom-shared.h"
#include "extensions/common/mojom/run_location.mojom-shared.h"
#include "third_party/blink/public/mojom/script/script_evaluation_params.mojom-shared.h"
#include "url/gurl.h"
#include "night_shift_manifest.h"

namespace zitter::prototype {
namespace {

int Clamped(const base::DictValue* value, std::string_view key, int low, int high, int fallback) {
  const auto number = value ? value->FindInt(key) : std::nullopt;
  return number && *number >= low && *number <= high ? *number : fallback;
}

constexpr std::string_view kApplyFragment = "apply=";

// The extension and its storage in the page's profile, or nothing.
struct Store {
  scoped_refptr<const extensions::Extension> extension;
  raw_ptr<extensions::StorageFrontend> storage = nullptr;
  explicit operator bool() const { return extension && storage; }
};
Store StoreFor(content::WebContents* page) {
  if (!page) return {};
  auto* profile = Profile::FromBrowserContext(page->GetBrowserContext());
  if (!profile || !profile->IsRegularProfile()) return {};
  auto* registry = extensions::ExtensionRegistry::Get(profile);
  const auto* extension = registry ? registry->enabled_extensions().GetByID(kNightShiftId) : nullptr;
  if (!extension) return {};
  return {base::WrapRefCounted(extension), extensions::StorageFrontend::Get(profile)};
}

std::string DraftKey(content::WebContents* page) {
  return "night-shift-draft:" + base::NumberToString(sessions::SessionTabHelper::IdForTab(page).id());
}

std::string DocumentId(content::WebContents* page) {
  return extensions::ExtensionApiFrameIdMap::GetDocumentId(page->GetPrimaryMainFrame()).ToString();
}

// A repair as site_policy.js repair() accepts it, roughly: the page checks
// every rule again before it applies one.
bool IsRepair(const base::Value* value) {
  const auto* dict = value ? value->GetIfDict() : nullptr;
  const auto* rules = dict ? dict->FindList("rules") : nullptr;
  return dict && dict->FindString("label") && rules && !rules->empty() && rules->size() <= 12;
}

int Percent(const base::DictValue* dict, const char* name, int fallback) {
  const auto value = dict ? dict->FindInt(name) : std::nullopt;
  return value && *value >= 75 && *value <= 125 ? *value : fallback;
}

// What a site record keeps (site_policy.js storedSite): tuning only where the
// person set it, so the app-wide defaults still reach every other value.
base::DictValue Normalized(const base::DictValue& data) {
  const auto* raw_mode = data.FindString("mode");
  const std::string mode = raw_mode && (*raw_mode == "off" || *raw_mode == "on") ? *raw_mode : "auto";
  base::DictValue stored;
  stored.Set("mode", mode);
  stored.Set("force", mode == "on" || (mode == "off" && data.FindBool("force").value_or(false)));
  for (const char* name : {"repair", "previousRepair"}) {
    if (IsRepair(data.Find(name))) stored.Set(name, data.Find(name)->Clone());
    else stored.Set(name, base::Value());
  }
  for (const char* name : {"brightness", "contrast"})
    if (const int value = Percent(&data, name, 0)) stored.Set(name, value);
  return stored;
}

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

void PushNightShift(Profile* profile, bool enabled, const NightShiftConfig& config, base::OnceClosure done) {
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
               base::BindOnce([](base::OnceClosure done, extensions::StorageFrontend::ResultStatus) {
                 std::move(done).Run();
               }, std::move(done)));
}

std::string NightShiftSiteKey(const GURL& url) {
  if (!url.SchemeIsHTTPOrHTTPS() || !url.has_host()) return {};
  std::string host(url.host());
  if (host.ends_with('.')) host.pop_back();
  if (url.has_port()) host += ":" + std::string(url.port());
  return "site:" + host;
}

void ReadNightShiftSite(content::WebContents* page, base::OnceCallback<void(NightShiftSite)> done) {
  const auto store = StoreFor(page);
  if (!store) { std::move(done).Run({}); return; }
  NightShiftSite site;
  site.key = NightShiftSiteKey(page->GetLastCommittedURL());
  std::vector<std::string> keys = {"enabled", "defaults"};
  if (!site.key.empty()) keys.push_back(site.key);
  store.storage->GetValues(store.extension, extensions::StorageAreaNamespace::kLocal, keys, base::BindOnce(
      [](Store store, NightShiftSite site, std::string url, std::string document, std::string draft_key,
         base::OnceCallback<void(NightShiftSite)> done, extensions::StorageFrontend::GetResult result) {
        if (!result.data) { std::move(done).Run({}); return; }
        const auto& values = *result.data;
        site.enabled = values.FindBool("enabled").value_or(false);
        const auto* defaults = values.FindDict("defaults");
        if (const auto* data = site.key.empty() ? nullptr : values.FindDict(site.key)) site.stored = data->Clone();
        const auto* mode = site.stored.FindString("mode");
        site.mode = mode && (*mode == "off" || *mode == "on") ? *mode : "auto";
        const bool default_force = defaults && defaults->FindBool("force").value_or(false);
        site.force = site.mode == "on" || (site.mode == "off" && site.stored.FindBool("force").value_or(false)) ||
                     (site.mode == "auto" && default_force);
        site.brightness = Percent(&site.stored, "brightness", Percent(defaults, "brightness", 100));
        site.contrast = Percent(&site.stored, "contrast", Percent(defaults, "contrast", 100));
        site.tuned = Percent(&site.stored, "brightness", 0) || Percent(&site.stored, "contrast", 0);
        site.repaired = IsRepair(site.stored.Find("repair"));
        if (!site.on()) { std::move(done).Run(std::move(site)); return; }
        // The whole (small) session area: asked for one key, the session
        // store answered empty on Windows even while the draft was there.
        store.storage->GetValues(store.extension, extensions::StorageAreaNamespace::kSession,
            std::nullopt, base::BindOnce(
            [](NightShiftSite site, std::string url, std::string document, std::string draft_key,
               base::OnceCallback<void(NightShiftSite)> done, extensions::StorageFrontend::GetResult result) {
              const auto* draft = result.data ? result.data->FindDict(draft_key) : nullptr;
              const auto* proposal = draft ? draft->Find("proposal") : nullptr;
              const auto* draft_url = draft ? draft->FindString("url") : nullptr;
              const auto* draft_document = draft ? draft->FindString("documentId") : nullptr;
              // Only a draft for the document the assistant inspected.
              if (IsRepair(proposal) && draft_url && *draft_url == url && draft_document && *draft_document == document) {
                site.draft_label = *proposal->GetDict().FindString("label");
                site.draft = draft->Clone();
              }
              std::move(done).Run(std::move(site));
            }, std::move(site), std::move(url), std::move(document), std::move(draft_key), std::move(done)));
      }, store, std::move(site), page->GetLastCommittedURL().spec(), DocumentId(page), DraftKey(page), std::move(done)));
}

void WriteNightShiftSite(content::WebContents* page, const std::string& key, base::DictValue change,
                         base::OnceClosure done) {
  const auto store = StoreFor(page);
  if (!store || key.empty() || key != NightShiftSiteKey(page->GetLastCommittedURL())) { std::move(done).Run(); return; }
  store.storage->GetValues(store.extension, extensions::StorageAreaNamespace::kLocal, std::vector<std::string>{key},
      base::BindOnce([](Store store, std::string key, base::DictValue change, base::OnceClosure done,
                        extensions::StorageFrontend::GetResult result) {
        base::DictValue merged;
        if (const auto* data = result.data ? result.data->FindDict(key) : nullptr) merged = data->Clone();
        for (auto [name, value] : change) {
          if (value.is_none()) merged.Remove(name);
          else merged.Set(name, value.Clone());
        }
        store.storage->Set(store.extension, extensions::StorageAreaNamespace::kLocal,
            base::DictValue().Set(key, Normalized(merged)),
            base::BindOnce([](base::OnceClosure done, extensions::StorageFrontend::ResultStatus) { std::move(done).Run(); },
                           std::move(done)));
      }, store, key, std::move(change), std::move(done)));
}

void PreviewNightShiftRepair(content::WebContents* page, const base::Value& proposal,
                             base::OnceCallback<void(bool)> done) {
  const auto store = StoreFor(page);
  if (!store) { std::move(done).Run(false); return; }
  const std::string argument = proposal.is_none() ? "null" : base::WriteJson(proposal).value_or("null");
  std::vector<extensions::mojom::JSSourcePtr> sources;
  // The function lives in the extension's isolated world, out of the page's reach.
  sources.push_back(extensions::mojom::JSSource::New(
      "globalThis.zitterNightShiftReview?.(" + argument + ") === true", GURL()));
  extensions::ScriptExecutor executor(page);
  executor.ExecuteScript(
      extensions::mojom::HostID(extensions::mojom::HostID::HostType::kExtensions, kNightShiftId),
      extensions::mojom::CodeInjection::NewJs(extensions::mojom::JSInjection::New(
          std::move(sources), extensions::mojom::ExecutionWorld::kIsolated, std::nullopt,
          blink::mojom::WantResultOption::kWantResult, blink::mojom::UserActivationOption::kDoNotActivate,
          blink::mojom::PromiseResultOption::kDoNotWait)),
      extensions::ScriptExecutor::SPECIFIED_FRAMES, {extensions::ExtensionApiFrameIdMap::kTopFrameId},
      extensions::mojom::MatchOriginAsFallbackBehavior::kNever, extensions::mojom::RunLocation::kDocumentStart,
      extensions::ScriptExecutor::DEFAULT_PROCESS, GURL(),
      base::BindOnce([](base::OnceCallback<void(bool)> done, std::vector<extensions::ScriptExecutor::FrameResult> results) {
        std::move(done).Run(results.size() == 1 && results[0].error.empty() && results[0].value == base::Value(true));
      }, std::move(done)));
}

void FinishNightShiftReview(content::WebContents* page, const NightShiftSite& site, bool save,
                            base::OnceClosure done) {
  const auto store = StoreFor(page);
  if (!store || !site.draft) { std::move(done).Run(); return; }
  auto remove = base::BindOnce([](Store store, std::string draft_key, base::OnceClosure done) {
    store.storage->Remove(store.extension, extensions::StorageAreaNamespace::kSession, {draft_key},
        base::BindOnce([](base::OnceClosure done, extensions::StorageFrontend::ResultStatus) { std::move(done).Run(); },
                       std::move(done)));
  }, store, DraftKey(page), std::move(done));
  if (!save) { std::move(remove).Run(); return; }
  const auto* previous = site.stored.Find("repair");
  WriteNightShiftSite(page, site.key, base::DictValue()
      .Set("repair", site.draft->Find("proposal")->Clone())
      .Set("previousRepair", IsRepair(previous) ? previous->Clone() : base::Value()),
      std::move(remove));
}

void UndoNightShiftRepair(content::WebContents* page, const NightShiftSite& site, base::OnceClosure done) {
  const auto* previous = site.stored.Find("previousRepair");
  WriteNightShiftSite(page, site.key, base::DictValue()
      .Set("repair", IsRepair(previous) ? previous->Clone() : base::Value())
      .Set("previousRepair", base::Value()), std::move(done));
}

void ObserveNightShiftSettings(content::WebContents* contents,
                               base::RepeatingCallback<void(base::DictValue)> apply) {
  if (!contents) return;
  SettingsObserver::CreateForWebContents(contents);
  SettingsObserver::FromWebContents(contents)->set_apply(std::move(apply));
}

}  // namespace zitter::prototype
