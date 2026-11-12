// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CONTENT_DOWNLOAD_SAVE_PICKER_H_
#define ZITTER_CONTENT_DOWNLOAD_SAVE_PICKER_H_

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "components/download/public/common/download_item.h"
#include "components/download/public/common/download_path_reservation_tracker.h"
#include "components/download/public/common/download_target_info.h"
#include "content/public/browser/weak_document_ptr.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/shell_dialogs/select_file_dialog.h"

namespace zitter::content_embedder {

// Owns one browser-initiated save request through selection and reservation.
class DownloadSavePicker final
    : public base::RefCounted<DownloadSavePicker>,
      private content::WebContentsObserver,
      private download::DownloadItem::Observer,
      private ui::SelectFileDialog::Listener {
 public:
  using TargetPolicy = base::RepeatingCallback<bool(const base::FilePath&)>;

  DownloadSavePicker(download::DownloadItem& item,
                     content::WebContents& contents,
                     base::FilePath suggested_path,
                     TargetPolicy target_policy,
                     download::DownloadTargetCallback callback);

  bool pending() const { return !callback_.is_null(); }
  void Show();
  void Cancel();

 private:
  friend class base::RefCounted<DownloadSavePicker>;
  ~DownloadSavePicker() override;

  bool HasLiveSource() const;
  void Reserve(base::FilePath path);
  void OnReserved(const base::FilePath& selected_path,
                  download::PathValidationResult result,
                  const base::FilePath& reserved_path);
  void Finish(download::DownloadTargetInfo target);

  void FileSelected(const ui::SelectedFileInfo& file, int index) override;
  void FileSelectionCanceled() override;
  void DidStartNavigation(content::NavigationHandle* navigation) override;
  void OnVisibilityChanged(content::Visibility visibility) override;
  void WebContentsDestroyed() override;
  void OnDownloadUpdated(download::DownloadItem* item) override;
  void OnDownloadDestroyed(download::DownloadItem* item) override;

  const content::WeakDocumentPtr document_;
  const gfx::NativeWindow owner_;
  const base::FilePath suggested_path_;
  const base::FilePath::StringType default_extension_;
  const TargetPolicy target_policy_;
  download::DownloadTargetCallback callback_;
  const ui::SelectFileDialog::FileTypeInfo file_types_;
  scoped_refptr<ui::SelectFileDialog> dialog_;
  base::ScopedObservation<download::DownloadItem, download::DownloadItem::Observer>
      item_{this};
  base::WeakPtrFactory<DownloadSavePicker> weak_factory_{this};
};

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_DOWNLOAD_SAVE_PICKER_H_
