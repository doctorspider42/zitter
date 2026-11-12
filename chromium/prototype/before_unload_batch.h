// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CHROMIUM_PROTOTYPE_BEFORE_UNLOAD_BATCH_H_
#define ZITTER_CHROMIUM_PROTOTYPE_BEFORE_UNLOAD_BATCH_H_

#include <vector>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"

namespace content { class WebContents; }

namespace zitter::prototype {

class PageDialogs;

// Collects all beforeunload decisions before the caller commits an operation.
class BeforeUnloadBatch {
 public:
  using DialogsFor =
      base::RepeatingCallback<PageDialogs*(content::WebContents*)>;

  BeforeUnloadBatch();
  ~BeforeUnloadBatch();
  BeforeUnloadBatch(const BeforeUnloadBatch&) = delete;
  BeforeUnloadBatch& operator=(const BeforeUnloadBatch&) = delete;

  bool pending() const { return !done_.is_null(); }
  void Start(const std::vector<content::WebContents*>& pages,
             DialogsFor dialogs_for,
             base::RepeatingCallback<bool()> scope_is_current,
             base::OnceCallback<void(bool)> done);

 private:
  struct Page;
  void Continue(bool proceed);
  void Finish(bool proceed);

  std::vector<Page> pages_;
  DialogsFor dialogs_for_;
  base::RepeatingCallback<bool()> scope_is_current_;
  base::OnceCallback<void(bool)> done_;
  base::WeakPtrFactory<BeforeUnloadBatch> weak_factory_{this};
};

}  // namespace zitter::prototype

#endif  // ZITTER_CHROMIUM_PROTOTYPE_BEFORE_UNLOAD_BATCH_H_
