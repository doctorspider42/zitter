#include "chromium/prototype/workspace_window.h"
#include <algorithm>

#include "base/functional/bind.h"
#include "base/task/single_thread_task_runner.h"
#include "chromium/prototype/page_dialogs.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/widget/widget.h"

namespace zitter::prototype {
namespace {
int EntryId(content::WebContents* page) {
  auto* entry = page->GetController().GetLastCommittedEntry();
  return entry ? entry->GetUniqueID() : 0;
}
}
std::vector<content::WebContents*> WorkspaceWindow::WorkspacePages(std::string_view id, bool peek, bool any_peek) const {
  std::vector<content::WebContents*> pages;
  for (const auto& host : hosts_) if (id.empty() || host.workspace_id == id)
    for (const auto& tab : host.tabs) if (auto* page = tab->web_contents()) pages.push_back(page);
  if (peek && peek_view_ && (id.empty() || peek_workspace_id_ == id || any_peek)) pages.push_back(peek_view_->GetWebContents());
  return pages;
}
bool WorkspaceWindow::GuardPages(std::vector<content::WebContents*> pages, base::OnceClosure action) {
  if (closing_ || lifecycle_approved_) return false;
  if (lifecycle_pending_) return true;
  if (std::none_of(pages.begin(), pages.end(), [](auto* page) { return page && page->NeedToFireBeforeUnload(); })) return false;
  lifecycle_pending_ = true; lifecycle_index_ = 0; lifecycle_workspace_ = active_id();
  lifecycle_action_ = std::move(action);
  if (auto* source = contents(active_id())) lifecycle_source_page_ = source->GetWeakPtr();
  for (auto* page : WorkspacePages({})) lifecycle_topology_.push_back(page->GetWeakPtr());
  for (auto* page : pages) if (page) lifecycle_pages_.push_back({page->GetWeakPtr(), EntryId(page), false, PageDialogs::Attach(page)->navigation_generation()});
  shell_->CloseOverlays(true);
  shell_->SetOperationStatus(u"Sprawdzanie niezapisanych zmian…");
  ContinueLifecycle(true);
  return true;
}
void WorkspaceWindow::ContinueLifecycle(bool proceed) {
  if (!lifecycle_pending_) return;
  if (!proceed || lifecycle_workspace_ != active_id()) { FinishLifecycle(false); return; }
  while (lifecycle_index_ < lifecycle_pages_.size()) {
    auto& entry = lifecycle_pages_[lifecycle_index_++];
    if (!entry.page || EntryId(entry.page.get()) != entry.entry_id || PageDialogs::Attach(entry.page.get())->navigation_generation() != entry.navigation_generation) { FinishLifecycle(false); return; }
    if (entry.checked || !entry.page->NeedToFireBeforeUnload()) continue;
    entry.checked = true;
    PageDialogs::Attach(entry.page.get())->CheckBeforeUnload(
        base::BindOnce(&WorkspaceWindow::ContinueLifecycle, weak_factory_.GetWeakPtr()));
    return;
  }
  for (const auto& entry : lifecycle_pages_) if (!entry.page || EntryId(entry.page.get()) != entry.entry_id || PageDialogs::Attach(entry.page.get())->navigation_generation() != entry.navigation_generation) {
    FinishLifecycle(false); return;
  }
  // Scripts can install a new handler while another page's sheet is open.
  // Check newly dirty pages before committing any part of the operation.
  for (size_t i = 0; i < lifecycle_pages_.size(); ++i) if (!lifecycle_pages_[i].checked && lifecycle_pages_[i].page->NeedToFireBeforeUnload()) {
    lifecycle_index_ = i; ContinueLifecycle(true); return;
  }
  if (contents(active_id()) != lifecycle_source_page_.get()) { FinishLifecycle(false); return; }
  const auto current = WorkspacePages({});
  if (current.size() != lifecycle_topology_.size()) { FinishLifecycle(false); return; }
  for (size_t i = 0; i < current.size(); ++i) if (lifecycle_topology_[i].get() != current[i]) { FinishLifecycle(false); return; }
  FinishLifecycle(true);
}
void WorkspaceWindow::FinishLifecycle(bool proceed) {
  auto action = std::move(lifecycle_action_);
  lifecycle_pages_.clear(); lifecycle_topology_.clear(); lifecycle_source_page_.reset(); lifecycle_pending_ = false; lifecycle_index_ = 0;
  if (!proceed) {
    shell_->SetOperationStatus(u"Anulowano. Karty i niezapisane zmiany pozostały otwarte.");
    return;
  }
  auto weak = weak_factory_.GetWeakPtr();
  lifecycle_approved_ = true;
  if (action) std::move(action).Run();
  if (!weak) return;
  lifecycle_approved_ = false;
  shell_->SetOperationStatus(u"Gotowe.");
}
void WorkspaceWindow::ArmCloseHandler() {
  widget_->MakeCloseSynchronous(base::BindOnce([](base::WeakPtr<WorkspaceWindow> window, views::Widget::ClosedReason) {
    if (!window) return;
    window->ArmCloseHandler();
    window->RequestClose();
  }, weak_factory_.GetWeakPtr()));
}
void WorkspaceWindow::RequestClose() {
  if (GuardPages(WorkspacePages({}), base::BindOnce(&WorkspaceWindow::CommitClose, weak_factory_.GetWeakPtr()))) return;
  CommitClose();
}
void WorkspaceWindow::CommitClose() {
  if (close_callback_) std::move(close_callback_).Run();
}
}  // namespace zitter::prototype
