#include "chromium/prototype/editor_control.h"
#include "chromium/prototype/workspace_window.h"

#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/predictors/loading_predictor.h"
#include "chrome/browser/predictors/loading_predictor_config.h"
#include "chrome/browser/predictors/loading_predictor_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "net/base/network_anonymization_key.h"
#include "net/base/schemeful_site.h"
#include "services/network/public/cpp/constants.h"
#include "ui/views/controls/textfield/textfield.h"
#include "url/origin.h"

namespace zitter::prototype {
namespace {
BASE_FEATURE(kZitterOmniboxPreconnect, base::FEATURE_ENABLED_BY_DEFAULT);
}

void WorkspaceWindow::SchedulePreconnect(std::u16string_view query) {
  preconnect_timer_.Stop();
  if (!base::FeatureList::IsEnabled(kZitterOmniboxPreconnect) ||
      !configuration_ready_ || profiles_busy_ || closing_ || !shell_->address_visible()) return;
  // Use the same classifier as Enter, including bare domains. A provider name
  // identifies a search/keyword query, which must not initiate this hint.
  std::u16string provider;
  const GURL url = shell_->ResolveInput(base::UTF16ToUTF8(query), &provider);
  if (!provider.empty()) return;
  if (!url.SchemeIsHTTPOrHTTPS() || !url.is_valid() || url.has_username() || url.has_password()) return;
  auto target = WorkspaceForUrl(url);
  if (target.empty()) target = active_id();
  const auto* workspace = model_.Find(target);
  auto* profile = workspace ? ProfileFor(workspace->profile_id) : nullptr;
  // Merely typing must not initialize a dormant profile or bypass prediction prefs.
  if (!profile || profile->IsOffTheRecord() || !predictors::IsPreconnectAllowed(profile)) return;
  const auto origin = url::Origin::Create(url).GetURL();
  if (last_preconnect_profile_ == workspace->profile_id && last_preconnect_origin_ == origin &&
      base::TimeTicks::Now() - last_preconnect_time_ < base::Seconds(5)) return;
  preconnect_timer_.Start(FROM_HERE, base::Milliseconds(200), base::BindOnce(
      [](base::WeakPtr<WorkspaceWindow> window, size_t generation, std::string source,
         std::string target, std::string profile_id, base::FilePath profile_path, GURL origin) {
        if (!window || window->closing_ || !window->configuration_ready_ || window->profiles_busy_ ||
            window->search_generation_ != generation || window->active_id() != source ||
            !window->shell_->address_visible()) return;
        // The shell also debounces search. Text may have changed before its next
        // search callback advances the generation, so verify the live intent.
        std::u16string provider;
        const auto current = window->shell_->ResolveInput(
            base::UTF16ToUTF8(window->shell_->address_input()->GetText()), &provider);
        if (!provider.empty() || !current.SchemeIsHTTPOrHTTPS() || current.has_username() ||
            current.has_password() || url::Origin::Create(current).GetURL() != origin) return;
        auto current_target = window->WorkspaceForUrl(current);
        if (current_target.empty()) current_target = source;
        if (current_target != target) return;
        const auto* workspace = window->model_.Find(target);
        auto* profile = window->ProfileFor(profile_id);
        if (!workspace || workspace->profile_id != profile_id || !profile ||
            profile->GetPath() != profile_path || !predictors::IsPreconnectAllowed(profile)) return;
        auto* predictor = predictors::LoadingPredictorFactory::GetForProfile(profile);
        if (!predictor) return;
        predictor->PreconnectURLIfAllowed(origin, /*allow_credentials=*/true,
            net::NetworkAnonymizationKey::CreateSameSite(net::SchemefulSite(origin)),
            network::GetNoOpNetworkRestrictionsId());
        window->last_preconnect_profile_ = profile_id;
        window->last_preconnect_origin_ = origin;
        window->last_preconnect_time_ = base::TimeTicks::Now();
      }, weak_factory_.GetWeakPtr(), search_generation_, active_id(), target,
      workspace->profile_id, profile->GetPath(), origin));
}
}  // namespace zitter::prototype
