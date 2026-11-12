// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "download_save_picker.h"

#include <utility>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/task/thread_pool.h"
#include "base/uuid.h"
#include "chromium/prototype/i18n.h"
#include "components/download/public/common/download_target_info.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "net/base/filename_util.h"
#include "ui/shell_dialogs/select_file_policy.h"
#include "ui/shell_dialogs/selected_file_info.h"

namespace zitter::content_embedder {
namespace {

base::FilePath ResolveAvailableTarget(const base::FilePath& path) {
  const auto selected_dir = path.DirName();
  if (!base::DirectoryExists(selected_dir) || base::IsLink(selected_dir))
    return {};
  // Save panels can return a system alias such as /var instead of /private/var.
  const auto directory = base::MakeAbsoluteFilePath(selected_dir);
  if (directory.empty() || directory.IsNetwork())
    return {};
  const auto target = directory.Append(path.BaseName());
  if (base::IsLink(target) || base::PathExists(target))
    return {};
  return target;
}

ui::SelectFileDialog::FileTypeInfo MakeFileTypes(
    const base::FilePath::StringType& extension) {
  ui::SelectFileDialog::FileTypeInfo types;
  types.keep_extension_visible = true;
  if (!extension.empty())
    types.extensions.push_back({extension});
  return types;
}

}  // namespace

DownloadSavePicker::DownloadSavePicker(
    download::DownloadItem& item,
    content::WebContents& contents,
    base::FilePath suggested_path,
    TargetPolicy target_policy,
    download::DownloadTargetCallback callback)
    : content::WebContentsObserver(&contents),
      document_(contents.GetPrimaryMainFrame()->GetWeakDocumentPtr()),
      owner_(contents.GetTopLevelNativeWindow()),
      suggested_path_(std::move(suggested_path)),
      default_extension_(
          suggested_path_.FinalExtension().empty()
              ? base::FilePath::StringType()
              : suggested_path_.FinalExtension().substr(1)),
      target_policy_(std::move(target_policy)),
      callback_(std::move(callback)),
      file_types_(MakeFileTypes(default_extension_)) {
  item_.Observe(&item);
}

DownloadSavePicker::~DownloadSavePicker() {
  if (dialog_)
    dialog_->ListenerDestroyed();
}

bool DownloadSavePicker::HasLiveSource() const {
  auto* frame = document_.AsRenderFrameHostIfValid();
  return item_.IsObserving() && !item_.GetSource()->IsDone() && web_contents() &&
         owner_ && web_contents()->GetTopLevelNativeWindow() == owner_ &&
         web_contents()->GetVisibility() != content::Visibility::HIDDEN &&
         frame && frame->IsActive() &&
         frame == web_contents()->GetPrimaryMainFrame();
}

void DownloadSavePicker::Show() {
  if (!HasLiveSource()) {
    Cancel();
    return;
  }
  dialog_ = ui::SelectFileDialog::Create(this, nullptr);
  if (!dialog_) {
    Cancel();
    return;
  }
  // A platform implementation may run the listener before returning.
  scoped_refptr<DownloadSavePicker> keep_alive(this);
  auto dialog = dialog_;
  dialog->SelectFile(ui::SelectFileDialog::SELECT_SAVEAS_FILE,
                     prototype::T(u"Zapisz plik"), suggested_path_, &file_types_,
                     file_types_.extensions.empty() ? 0 : 1, default_extension_,
                     owner_);
}

void DownloadSavePicker::Cancel() {
  download::DownloadTargetInfo target;
  target.interrupt_reason = download::DOWNLOAD_INTERRUPT_REASON_USER_CANCELED;
  Finish(std::move(target));
}

void DownloadSavePicker::FileSelected(const ui::SelectedFileInfo& file, int) {
  if (!dialog_)
    return;
  const auto path = file.local_path;
  if (!HasLiveSource() || !path.IsAbsolute() || path.IsNetwork() ||
      path.ReferencesParent() || path.EndsWithSeparator() ||
      !net::IsSafePortablePathComponent(path.BaseName())) {
    VLOG(1) << "Download save selection had an invalid path or source";
    Cancel();
    return;
  }
  dialog_->ListenerDestroyed();
  dialog_.reset();
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()}, base::BindOnce(&ResolveAvailableTarget, path),
      base::BindOnce(&DownloadSavePicker::Reserve, weak_factory_.GetWeakPtr()));
}

void DownloadSavePicker::Reserve(base::FilePath path) {
  if (path.empty() || !HasLiveSource() || !target_policy_.Run(path)) {
    VLOG(1) << "Download save target was unavailable or outside its policy";
    Cancel();
    return;
  }
  const auto directory = path.DirName();
  download::DownloadPathReservationTracker::GetReservedPath(
      item_.GetSource(), path, directory, base::FilePath(),
      /*create_directory=*/false,
      download::DownloadPathReservationTracker::PROMPT,
      base::BindOnce(&DownloadSavePicker::OnReserved,
                     weak_factory_.GetWeakPtr(), path),
      directory);
}

void DownloadSavePicker::OnReserved(
    const base::FilePath& selected_path,
    download::PathValidationResult result,
    const base::FilePath& reserved_path) {
  if (!HasLiveSource() || result != download::PathValidationResult::SUCCESS ||
      reserved_path != selected_path) {
    VLOG(1) << "Download save target reservation failed: "
            << static_cast<int>(result);
    Cancel();
    return;
  }
  download::DownloadTargetInfo target;
  target.target_disposition = download::DownloadItem::TARGET_DISPOSITION_PROMPT;
  target.target_path = reserved_path;
  target.intermediate_path = reserved_path.DirName().AppendASCII(
      base::Uuid::GenerateRandomV4().AsLowercaseString() + ".crdownload");
  Finish(std::move(target));
}

void DownloadSavePicker::Finish(download::DownloadTargetInfo target) {
  if (!pending())
    return;
  scoped_refptr<DownloadSavePicker> keep_alive(this);
  weak_factory_.InvalidateWeakPtrs();
  if (dialog_)
    dialog_->ListenerDestroyed();
  dialog_.reset();
  Observe(nullptr);
  item_.Reset();
  std::move(callback_).Run(std::move(target));
}

void DownloadSavePicker::FileSelectionCanceled() {
  Cancel();
}

void DownloadSavePicker::DidStartNavigation(content::NavigationHandle* navigation) {
  if (navigation->IsInPrimaryMainFrame() && !navigation->IsSameDocument())
    Cancel();
}

void DownloadSavePicker::OnVisibilityChanged(content::Visibility visibility) {
  if (visibility == content::Visibility::HIDDEN)
    Cancel();
}

void DownloadSavePicker::WebContentsDestroyed() {
  Cancel();
}

void DownloadSavePicker::OnDownloadUpdated(download::DownloadItem* item) {
  if (item->IsDone())
    Cancel();
}

void DownloadSavePicker::OnDownloadDestroyed(download::DownloadItem*) {
  Cancel();
}

}  // namespace zitter::content_embedder
