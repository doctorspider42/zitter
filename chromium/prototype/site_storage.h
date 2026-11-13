// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_PROTOTYPE_SITE_STORAGE_H_
#define ZITTER_CHROMIUM_PROTOTYPE_SITE_STORAGE_H_

#include <memory>
#include <utility>
#include <vector>

#include "base/barrier_closure.h"
#include "base/functional/bind.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/weak_document_ptr.h"
#include "net/cookies/canonical_cookie.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "third_party/blink/public/common/storage_key/storage_key.h"

namespace zitter::prototype {

// Scope local storage to the document's storage key. Cookies may be shared
// with subdomains and across ports, so match the host using cookie rules.
inline void ClearSiteStorage(const content::WeakDocumentPtr& document,
                             base::OnceCallback<void(bool)> done,
                             bool cookies = true, bool local_storage = true) {
  auto* frame = document.AsRenderFrameHostIfValid();
  if (!frame || !frame->IsActive() || !frame->IsInPrimaryMainFrame() ||
      !frame->GetLastCommittedOrigin().GetURL().SchemeIsHTTPOrHTTPS()) {
    std::move(done).Run(false);
    return;
  }
  auto* partition = frame->GetStoragePartition();
  if (!cookies) {
    partition->ClearData(content::StoragePartition::REMOVE_DATA_MASK_LOCAL_STORAGE,
                         frame->GetStorageKey(), base::Time(), base::Time::Max(),
                         base::BindOnce(std::move(done), true));
    return;
  }
  partition->GetCookieManagerForBrowserProcess()->GetAllCookies(base::BindOnce(
      [](content::WeakDocumentPtr document, bool local_storage,
         base::OnceCallback<void(bool)> done,
         const std::vector<net::CanonicalCookie>& cookies) {
        auto* frame = document.AsRenderFrameHostIfValid();
        if (!frame || !frame->IsActive() || !frame->IsInPrimaryMainFrame()) {
          std::move(done).Run(false);
          return;
        }
        auto* partition = frame->GetStoragePartition();
        const auto key = frame->GetStorageKey();
        const auto cookie_key = key.ToCookiePartitionKey();
        const auto host = frame->GetLastCommittedOrigin().host();
        std::vector<const net::CanonicalCookie*> matches;
        for (const auto& cookie : cookies) {
          if (cookie.IsDomainMatch(host) &&
              (!cookie.IsPartitioned() || cookie.PartitionKey() == cookie_key)) {
            matches.push_back(&cookie);
          }
        }
        auto succeeded = std::make_shared<bool>(true);
        auto finish = base::BarrierClosure(
            matches.size() + (local_storage ? 1 : 0), base::BindOnce(
                [](base::OnceCallback<void(bool)> done, std::shared_ptr<bool> ok) {
                  std::move(done).Run(*ok);
                }, std::move(done), succeeded));
        auto* manager = partition->GetCookieManagerForBrowserProcess();
        for (const auto* cookie : matches) {
          manager->DeleteCanonicalCookie(
              *cookie, base::BindOnce(
                  [](base::RepeatingClosure finish, std::shared_ptr<bool> ok,
                     bool deleted) {
                    *ok &= deleted;
                    finish.Run();
                  }, finish, succeeded));
        }
        if (local_storage) {
          partition->ClearData(content::StoragePartition::REMOVE_DATA_MASK_LOCAL_STORAGE,
                               key, base::Time(), base::Time::Max(), finish);
        }
      }, document, local_storage, std::move(done)));
}
}  // namespace zitter::prototype

#endif  // ZITTER_CHROMIUM_PROTOTYPE_SITE_STORAGE_H_
