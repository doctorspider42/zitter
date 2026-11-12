// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "page_file_chooser.h"

#include <optional>
#include <utility>
#include <vector>

#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "build/build_config.h"
#include "content/public/browser/file_select_listener.h"
#include "content/public/browser/global_routing_id.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/weak_document_ptr.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "net/base/mime_util.h"
#include "profile_context.h"
#include "third_party/blink/public/mojom/choosers/file_chooser.mojom.h"
#include "ui/shell_dialogs/select_file_dialog.h"
#include "ui/shell_dialogs/select_file_policy.h"
#include "ui/shell_dialogs/selected_file_info.h"

#if BUILDFLAG(IS_WIN)
#include <windows.h>
#endif

namespace zitter::content_embedder {
namespace {

using Mode = blink::mojom::FileChooserParams::Mode;
using Selection = std::vector<blink::mojom::FileChooserFileInfoPtr>;

bool IsLocalPath(const base::FilePath& path) {
  return path.IsAbsolute() && !path.ReferencesParent();
}

base::FilePath InitialDirectory(const base::FilePath& remembered) {
  if (!remembered.empty() && base::DirectoryExists(remembered))
    return remembered;
  return base::GetHomeDir();
}

blink::mojom::FileChooserFileInfoPtr NativeFile(
    const base::FilePath& path,
    std::u16string display_name = {}) {
  return blink::mojom::FileChooserFileInfo::NewNativeFile(
      blink::mojom::NativeFileInfo::New(
          path, std::move(display_name), std::vector<std::u16string>()));
}

std::optional<Selection> ReadDirectory(const base::FilePath& root) {
#if BUILDFLAG(IS_WIN)
  const auto attributes = ::GetFileAttributesW(root.value().c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES ||
      !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
      (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
    return std::nullopt;
  }
#else
  if (base::IsLink(root) || !base::DirectoryExists(root))
    return std::nullopt;
#endif

  int types = base::FileEnumerator::FILES;
#if BUILDFLAG(IS_POSIX)
  // Enumerate links themselves so FileEnumerator never descends into them.
  types |= base::FileEnumerator::SHOW_SYM_LINKS;
#endif
  // On Windows FileEnumerator already skips recursion into reparse points.
  base::FileEnumerator entries(
      root, /*recursive=*/true, types, {},
      base::FileEnumerator::FolderSearchPolicy::ALL,
      base::FileEnumerator::ErrorPolicy::STOP_ENUMERATION);
  Selection files;
  for (auto path = entries.Next(); !path.empty(); path = entries.Next()) {
    if (!root.IsParent(path))
      return std::nullopt;
#if BUILDFLAG(IS_WIN)
    if (entries.GetInfo().find_data().dwFileAttributes &
        FILE_ATTRIBUTE_REPARSE_POINT) {
      continue;
    }
#else
    if (!S_ISREG(entries.GetInfo().stat().st_mode))
      continue;
#endif
    files.push_back(NativeFile(path));
  }
  if (entries.GetError() != base::File::FILE_OK)
    return std::nullopt;
  return files;
}

ui::SelectFileDialog::FileTypeInfo AcceptedTypes(
    const std::vector<std::u16string>& accept_types) {
  ui::SelectFileDialog::FileTypeInfo types;
  std::vector<base::FilePath::StringType> extensions;
  for (const auto& type : accept_types) {
    if (type.starts_with(u'.')) {
      if (type.size() > 1 && type.find(u'\0') == std::u16string::npos &&
          type.find_first_of(u"/\\:*?;|", 1) == std::u16string::npos) {
        extensions.push_back(
            base::FilePath::FromUTF16Unsafe(type.substr(1)).value());
      }
    } else if (base::IsStringASCII(type)) {
      net::GetExtensionsForMimeType(base::UTF16ToASCII(type), &extensions);
    }
  }
  if (!extensions.empty()) {
    types.extensions.push_back(std::move(extensions));
    types.include_all_files = true;
  }
  return types;
}

ui::SelectFileDialog::Type DialogType(Mode mode) {
  switch (mode) {
    case Mode::kOpen:
      return ui::SelectFileDialog::SELECT_OPEN_FILE;
    case Mode::kOpenMultiple:
      return ui::SelectFileDialog::SELECT_OPEN_MULTI_FILE;
    case Mode::kUploadFolder:
      return ui::SelectFileDialog::SELECT_UPLOAD_FOLDER;
    case Mode::kOpenDirectory:
      return ui::SelectFileDialog::SELECT_EXISTING_FOLDER;
    case Mode::kSave:
      return ui::SelectFileDialog::SELECT_NONE;
  }
}

class PageFileChooser final : public base::RefCounted<PageFileChooser>,
                              private content::WebContentsObserver,
                              private ui::SelectFileDialog::Listener {
 public:
  PageFileChooser(content::WebContents& contents,
                  content::RenderFrameHost& frame,
                  scoped_refptr<content::FileSelectListener> listener,
                  Mode mode,
                  bool directory_already_selected)
      : content::WebContentsObserver(&contents),
        document_(frame.GetWeakDocumentPtr()),
        frame_id_(frame.GetGlobalId()),
        listener_(std::move(listener)),
        mode_(mode),
        directory_already_selected_(directory_already_selected) {}

  void Show(const blink::mojom::FileChooserParams& params) {
    pending_ = this;
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock()},
        base::BindOnce(&InitialDirectory, profile().file_chooser_directory()),
        base::BindOnce(&PageFileChooser::ShowDialog, weak_factory_.GetWeakPtr(),
                       AcceptedTypes(params.accept_types)));
  }

  void Enumerate(base::FilePath root) {
    pending_ = this;
    if (dialog_)
      dialog_->ListenerDestroyed();
    dialog_.reset();
    root_ = std::move(root);
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock()}, base::BindOnce(&ReadDirectory, root_),
        base::BindOnce(&PageFileChooser::Finish, weak_factory_.GetWeakPtr()));
  }

 private:
  friend class base::RefCounted<PageFileChooser>;
  ~PageFileChooser() override = default;

  ProfileContext& profile() const {
    return *static_cast<ProfileContext*>(web_contents()->GetBrowserContext());
  }

  void ShowDialog(const ui::SelectFileDialog::FileTypeInfo& types,
                  const base::FilePath& directory) {
    if (!CanDeliver() || !web_contents()->GetTopLevelNativeWindow()) {
      Finish(std::nullopt);
      return;
    }
    dialog_ = ui::SelectFileDialog::Create(this, nullptr);
    if (!dialog_) {
      Finish(std::nullopt);
      return;
    }
    auto dialog = dialog_;
    dialog->SelectFile(DialogType(mode_), {}, directory, &types,
                       types.extensions.empty() ? 0 : 1, {},
                       web_contents()->GetTopLevelNativeWindow());
  }

  bool CanDeliver() const {
    auto* frame = document_.AsRenderFrameHostIfValid();
    return web_contents() &&
           web_contents()->GetVisibility() == content::Visibility::VISIBLE &&
           frame && frame->IsActive();
  }

  void FileSelected(const ui::SelectedFileInfo& file, int) override {
    MultiFilesSelected({file});
  }

  void MultiFilesSelected(
      const std::vector<ui::SelectedFileInfo>& files) override {
    if (files.empty() || (mode_ != Mode::kOpenMultiple && files.size() != 1) ||
        !CanDeliver()) {
      Finish(std::nullopt);
      return;
    }
    Selection selection;
    for (const auto& file : files) {
      if (!IsLocalPath(file.local_path)) {
        Finish(std::nullopt);
        return;
      }
      if (mode_ == Mode::kUploadFolder) {
        selected_directory_ = file.local_path;
        Enumerate(file.local_path);
        return;
      }
      selection.push_back(NativeFile(
          file.local_path, base::FilePath(file.display_name).LossyDisplayName()));
    }
    selected_directory_ = mode_ == Mode::kOpenDirectory
                              ? files.front().local_path
                              : files.front().local_path.DirName();
    Finish(std::move(selection));
  }

  void FileSelectionCanceled() override { Finish(std::nullopt); }
  void WebContentsDestroyed() override { Finish(std::nullopt); }
  void OnVisibilityChanged(content::Visibility visibility) override {
    if (visibility != content::Visibility::VISIBLE)
      Finish(std::nullopt);
  }
  void RenderFrameDeleted(content::RenderFrameHost* frame) override {
    if (directory_already_selected_ || frame->GetGlobalId() == frame_id_)
      Finish(std::nullopt);
  }
  void DidStartNavigation(content::NavigationHandle* navigation) override {
    if (navigation->IsSameDocument())
      return;
    // EnumerateDirectory does not expose the initiating frame to the embedder.
    // A cross-document navigation in any frame invalidates that operation.
    if (directory_already_selected_ || navigation->IsInPrimaryMainFrame()) {
      Finish(std::nullopt);
      return;
    }
    for (auto* frame = document_.AsRenderFrameHostIfValid(); frame;
         frame = frame->GetParentOrOuterDocument()) {
      if (navigation->GetFrameTreeNodeId() == frame->GetFrameTreeNodeId()) {
        Finish(std::nullopt);
        return;
      }
    }
  }

  void Finish(std::optional<Selection> files) {
    scoped_refptr<PageFileChooser> keep_alive(this);
    const bool deliver = files && CanDeliver();
    if (deliver && !selected_directory_.empty())
      profile().SetFileChooserDirectory(selected_directory_);
    auto listener = std::move(listener_);
    weak_factory_.InvalidateWeakPtrs();
    Observe(nullptr);
    if (dialog_)
      dialog_->ListenerDestroyed();
    dialog_.reset();
    pending_.reset();
    if (!listener)
      return;
    if (deliver)
      listener->FileSelected(std::move(*files), root_, mode_);
    else
      listener->FileSelectionCanceled();
  }

  const content::WeakDocumentPtr document_;
  const content::GlobalRenderFrameHostId frame_id_;
  scoped_refptr<content::FileSelectListener> listener_;
  const Mode mode_;
  const bool directory_already_selected_;
  base::FilePath selected_directory_;
  base::FilePath root_;
  scoped_refptr<ui::SelectFileDialog> dialog_;
  scoped_refptr<PageFileChooser> pending_;
  base::WeakPtrFactory<PageFileChooser> weak_factory_{this};
};

}  // namespace

void RunPageFileChooser(content::RenderFrameHost* frame,
                        scoped_refptr<content::FileSelectListener> listener,
                        const blink::mojom::FileChooserParams& params) {
  auto* contents =
      frame ? content::WebContents::FromRenderFrameHost(frame) : nullptr;
  if (!contents || !frame->IsActive() ||
      contents->GetVisibility() != content::Visibility::VISIBLE ||
      !contents->GetTopLevelNativeWindow() || params.mode == Mode::kSave ||
      params.open_writable) {
    listener->FileSelectionCanceled();
    return;
  }
  base::MakeRefCounted<PageFileChooser>(*contents, *frame, std::move(listener),
                                       params.mode, false)
      ->Show(params);
}

void EnumeratePageDirectory(content::WebContents* contents,
                            scoped_refptr<content::FileSelectListener> listener,
                            const base::FilePath& path) {
  if (!contents || contents->GetVisibility() != content::Visibility::VISIBLE ||
      !IsLocalPath(path)) {
    listener->FileSelectionCanceled();
    return;
  }
  // Content has checked that the initiating renderer can already read path.
  base::MakeRefCounted<PageFileChooser>(*contents, *contents->GetPrimaryMainFrame(),
                                       std::move(listener), Mode::kUploadFolder,
                                       true)
      ->Enumerate(path);
}

}  // namespace zitter::content_embedder
