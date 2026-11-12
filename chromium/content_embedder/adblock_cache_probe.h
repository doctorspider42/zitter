// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CONTENT_EMBEDDER_ADBLOCK_CACHE_PROBE_H_
#define ZITTER_CONTENT_EMBEDDER_ADBLOCK_CACHE_PROBE_H_

#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/weak_document_ptr.h"
#include "url/gurl.h"

namespace zitter::content_embedder {

class AdblockCacheProbe final : public content::WebContentsObserver {
 public:
  AdblockCacheProbe(content::WebContents& page, GURL origin,
                    base::OnceCallback<void(bool)> done);
  ~AdblockCacheProbe() override;
  void Start();

 private:
  void DidFinishNavigation(content::NavigationHandle* navigation) override;
  void WebContentsDestroyed() override;
  void Poll();
  void Complete(bool passed);
  bool CheckCachedDocuments();
  bool CheckRestoredDocuments();
  void NavigateAway();
  void GoBack();

  const GURL page_url_;
  const GURL away_url_;
  const std::vector<std::string> original_rules_;
  base::OnceCallback<void(bool)> done_;
  content::WeakDocumentPtr main_document_;
  content::WeakDocumentPtr child_document_;
  base::TimeTicks deadline_;
  int step_ = 0;
  bool restored_from_cache_ = false;
  base::WeakPtrFactory<AdblockCacheProbe> weak_factory_{this};
};

}  // namespace zitter::content_embedder

#endif
