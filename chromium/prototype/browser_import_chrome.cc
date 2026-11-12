// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "chromium/prototype/browser_import_data.h"

#include <utility>

#include "base/check_op.h"
#include "base/functional/bind.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/profile.h"
#include "components/keyed_service/core/service_access_type.h"
#include "content/public/browser/storage_partition.h"

namespace zitter::prototype {

void WriteBrowsingImportData(
    std::shared_ptr<BrowsingImportData> data,
    std::vector<Profile*> profiles,
    base::OnceCallback<void(BrowsingImportResult)> callback) {
  CHECK_EQ(profiles.size(), data->profiles.size());
  BrowsingImportDestinations destinations;
  for (size_t i = 0; i < profiles.size(); ++i) {
    auto* profile = profiles[i];
    if (!profile)
      continue;
    std::shared_ptr<ScopedProfileKeepAlive> lease =
        ScopedProfileKeepAlive::TryAcquire(profile, ProfileKeepAliveOrigin::kBrowserWindow);
    if (!lease)
      continue;
    destinations.emplace(data->profiles[i].profile_id, BrowsingImportDestination{
        base::BindRepeating([](const std::shared_ptr<ScopedProfileKeepAlive>& lease) {
          return HistoryServiceFactory::GetForProfile(lease->profile(),
                                                      ServiceAccessType::EXPLICIT_ACCESS);
        }, lease),
        base::BindRepeating([](const std::shared_ptr<ScopedProfileKeepAlive>& lease) {
          return lease->profile()->GetDefaultStoragePartition()
              ->GetCookieManagerForBrowserProcess();
        }, lease)});
  }
  WriteBrowsingImportDataToServices(std::move(data), std::move(destinations),
                                    std::move(callback));
}

}  // namespace zitter::prototype
