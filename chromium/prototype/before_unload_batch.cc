// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "chromium/prototype/before_unload_batch.h"

#include <cstddef>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "chromium/prototype/page_dialogs.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"

namespace zitter::prototype {
namespace {

int CommittedEntryId(content::WebContents& page) {
  const auto* entry = page.GetController().GetLastCommittedEntry();
  return entry ? entry->GetUniqueID() : 0;
}

}  // namespace

struct BeforeUnloadBatch::Page {
  base::WeakPtr<content::WebContents> contents;
  int entry_id;
  size_t navigation_generation;
  bool checked = false;
};

BeforeUnloadBatch::BeforeUnloadBatch() = default;
BeforeUnloadBatch::~BeforeUnloadBatch() = default;

void BeforeUnloadBatch::Start(
    const std::vector<content::WebContents*>& pages,
    DialogsFor dialogs_for,
    base::RepeatingCallback<bool()> scope_is_current,
    base::OnceCallback<void(bool)> done) {
  CHECK(!pending());
  CHECK(dialogs_for && scope_is_current && done);
  dialogs_for_ = std::move(dialogs_for);
  scope_is_current_ = std::move(scope_is_current);
  done_ = std::move(done);
  for (auto* page : pages) {
    if (!page)
      continue;
    const auto* dialogs = dialogs_for_.Run(page);
    if (!dialogs) {
      Finish(false);
      return;
    }
    pages_.push_back({page->GetWeakPtr(), CommittedEntryId(*page),
                      dialogs->navigation_generation()});
  }
  Continue(true);
}

void BeforeUnloadBatch::Continue(bool proceed) {
  if (!proceed || !scope_is_current_.Run()) {
    Finish(false);
    return;
  }

  Page* next = nullptr;
  PageDialogs* next_dialogs = nullptr;
  for (auto& page : pages_) {
    auto* contents = page.contents.get();
    auto* dialogs = contents ? dialogs_for_.Run(contents) : nullptr;
    if (!dialogs || CommittedEntryId(*contents) != page.entry_id ||
        dialogs->navigation_generation() != page.navigation_generation) {
      Finish(false);
      return;
    }
    // A page can register a handler while another tab's dialog is open.
    if (!next && !page.checked && contents->NeedToFireBeforeUnload()) {
      next = &page;
      next_dialogs = dialogs;
    }
  }
  if (!next) {
    Finish(true);
    return;
  }

  next->checked = true;
  next_dialogs->CheckBeforeUnload(
      base::BindOnce(&BeforeUnloadBatch::Continue, weak_factory_.GetWeakPtr()));
}

void BeforeUnloadBatch::Finish(bool proceed) {
  auto done = std::move(done_);
  pages_.clear();
  dialogs_for_.Reset();
  scope_is_current_.Reset();
  std::move(done).Run(proceed);
}

}  // namespace zitter::prototype
