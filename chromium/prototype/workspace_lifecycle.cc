#include "chromium/prototype/workspace_window.h"
#include <algorithm>

#include "base/functional/bind.h"
#include "chromium/prototype/page_services.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/widget/widget.h"

namespace zitter::prototype {
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
  lifecycle_pending_ = true;
  lifecycle_workspace_ = active_id();
  lifecycle_action_ = std::move(action);
  if (auto* source = contents(active_id()))
    lifecycle_source_page_ = source->GetWeakPtr();
  for (auto* page : WorkspacePages({}))
    lifecycle_topology_.push_back(page->GetWeakPtr());

  shell_->CloseOverlays(true);
  shell_->SetOperationStatus(WorkspaceShell::OperationStatusEvent::kChecking);
  before_unload_batch_.Start(
      pages, base::BindRepeating(&AttachChromePageDialogs),
      base::BindRepeating(
          [](base::WeakPtr<WorkspaceWindow> window) {
            return window && window->LifecycleScopeIsCurrent();
          },
          weak_factory_.GetWeakPtr()),
      base::BindOnce(&WorkspaceWindow::FinishLifecycle,
                     weak_factory_.GetWeakPtr()));
  return true;
}

bool WorkspaceWindow::LifecycleScopeIsCurrent() const {
  if (lifecycle_workspace_ != active_id() ||
      contents(active_id()) != lifecycle_source_page_.get()) {
    return false;
  }
  const auto current = WorkspacePages({});
  return std::ranges::equal(
      current, lifecycle_topology_,
      [](auto* page, const auto& snapshot) { return page == snapshot.get(); });
}

void WorkspaceWindow::FinishLifecycle(bool proceed) {
  auto action = std::move(lifecycle_action_);
  lifecycle_topology_.clear();
  lifecycle_source_page_.reset();
  lifecycle_pending_ = false;
  if (!proceed) {
    shell_->SetOperationStatus(WorkspaceShell::OperationStatusEvent::kCancelled);
    return;
  }
  auto weak = weak_factory_.GetWeakPtr();
  lifecycle_approved_ = true;
  if (action) std::move(action).Run();
  if (!weak) return;
  lifecycle_approved_ = false;
  shell_->SetOperationStatus(WorkspaceShell::OperationStatusEvent::kDone);
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
