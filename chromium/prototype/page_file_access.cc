#include "chromium/prototype/page_file_access.h"

#include <memory>
#include <utility>

#include "base/files/file_path.h"
#include "base/memory/ref_counted.h"
#include "chrome/browser/file_select_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/select_file_policy/chrome_select_file_policy.h"
#include "content/public/browser/file_select_listener.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/shell_dialogs/select_file_dialog.h"
#include "ui/shell_dialogs/selected_file_info.h"

namespace zitter::prototype {
namespace {
// A picker may finish after its initiating document has gone away. Never pass
// its result to a new document (including a new document in the same frame).
class FrameBoundFileListener : public content::FileSelectListener,
                               public content::WebContentsObserver {
 public:
  FrameBoundFileListener(content::RenderFrameHost* frame,
                         scoped_refptr<content::FileSelectListener> listener)
      : content::WebContentsObserver(content::WebContents::FromRenderFrameHost(frame)),
        frame_(frame), listener_(std::move(listener)) {}
  void FileSelected(std::vector<blink::mojom::FileChooserFileInfoPtr> files,
                    const base::FilePath& root,
                    blink::mojom::FileChooserParams::Mode mode) override {
    auto keep_alive = scoped_refptr<FrameBoundFileListener>(this);
    auto listener = std::move(listener_);
    Observe(nullptr); frame_ = nullptr;
    if (listener) listener->FileSelected(std::move(files), root, mode);
  }
  void FileSelectionCanceled() override {
    auto keep_alive = scoped_refptr<FrameBoundFileListener>(this);
    auto listener = std::move(listener_);
    Observe(nullptr); frame_ = nullptr;
    if (listener) listener->FileSelectionCanceled();
  }
  void WebContentsDestroyed() override { FileSelectionCanceled(); }
  void OnVisibilityChanged(content::Visibility visibility) override {
    if (visibility == content::Visibility::HIDDEN) FileSelectionCanceled();
  }
  void RenderFrameDeleted(content::RenderFrameHost* frame) override {
    if (frame == frame_) FileSelectionCanceled();
  }
  void DidStartNavigation(content::NavigationHandle* navigation) override {
    if (!navigation->IsSameDocument() &&
        (navigation->IsInPrimaryMainFrame() ||
         (frame_ && navigation->GetFrameTreeNodeId() == frame_->GetFrameTreeNodeId())))
      FileSelectionCanceled();
  }
 private:
  ~FrameBoundFileListener() override = default;
  raw_ptr<content::RenderFrameHost> frame_;
  scoped_refptr<content::FileSelectListener> listener_;
};

// Chromium's regular folder-upload helper presents its final confirmation using
// a Chrome tab-modal manager. Zitter owns Views/WebContents directly, so use the
// native folder picker as consent, then its standard directory enumeration.
// This retains Blink's relative paths and avoids requiring a Chrome Browser.
class FolderChooser : public base::RefCounted<FolderChooser>,
                      public ui::SelectFileDialog::Listener,
                      public content::WebContentsObserver {
 public:
  FolderChooser(content::RenderFrameHost* frame,
                scoped_refptr<content::FileSelectListener> listener)
      : content::WebContentsObserver(content::WebContents::FromRenderFrameHost(frame)),
        frame_(frame), listener_(std::move(listener)) {}

  void Show() {
    self_ = this;
    dialog_ = ui::SelectFileDialog::Create(
        this, std::make_unique<ChromeSelectFilePolicy>(web_contents()));
    if (!dialog_) { Cancel(); return; }
    auto* profile = Profile::FromBrowserContext(web_contents()->GetBrowserContext());
    dialog_->SelectFile(ui::SelectFileDialog::SELECT_UPLOAD_FOLDER,
                        u"Wybierz folder do przesłania",
                        profile->last_selected_directory(), nullptr, 0, {},
                        web_contents()->GetTopLevelNativeWindow());
  }

  void FileSelected(const ui::SelectedFileInfo& file, int index) override {
    auto keep_alive = self_;
    if (!web_contents() || !frame_ || !frame_->IsActive() ||
        file.local_path.empty() || !file.local_path.IsAbsolute() ||
        file.local_path.ReferencesParent()) {
      Cancel(); return;
    }
    auto* contents = web_contents();
    Profile::FromBrowserContext(contents->GetBrowserContext())
        ->set_last_selected_directory(file.local_path);
    auto listener = std::move(listener_);
    Detach();
    EnumeratePageDirectory(contents, std::move(listener), file.local_path);
  }

  void FileSelectionCanceled() override { Cancel(); }
  void WebContentsDestroyed() override { Cancel(); }
  void OnVisibilityChanged(content::Visibility visibility) override {
    if (visibility == content::Visibility::HIDDEN) Cancel();
  }
  void RenderFrameDeleted(content::RenderFrameHost* frame) override {
    if (frame == frame_) Cancel();
  }
  void DidStartNavigation(content::NavigationHandle* navigation) override {
    if (!navigation->IsSameDocument() &&
        (navigation->IsInPrimaryMainFrame() ||
         (frame_ && navigation->GetFrameTreeNodeId() == frame_->GetFrameTreeNodeId())))
      Cancel();
  }

 private:
  friend class base::RefCounted<FolderChooser>;
  ~FolderChooser() override = default;
  void Detach() {
    if (dialog_) dialog_->ListenerDestroyed();
    dialog_.reset();
    frame_ = nullptr;
    Observe(nullptr);
    self_.reset();
  }
  void Cancel() {
    auto keep_alive = self_;
    auto listener = std::move(listener_);
    Detach();
    if (listener) listener->FileSelectionCanceled();
  }

  raw_ptr<content::RenderFrameHost> frame_;
  scoped_refptr<content::FileSelectListener> listener_;
  scoped_refptr<ui::SelectFileDialog> dialog_;
  scoped_refptr<FolderChooser> self_;
};
}  // namespace

void RunPageFileChooser(content::RenderFrameHost* frame,
                        scoped_refptr<content::FileSelectListener> listener,
                        const blink::mojom::FileChooserParams& params) {
  if (!listener) return;
  auto* contents = frame ? content::WebContents::FromRenderFrameHost(frame) : nullptr;
  if (!contents || !frame->IsActive() ||
      contents->GetVisibility() != content::Visibility::VISIBLE ||
      !contents->GetTopLevelNativeWindow() ||
      params.mode == blink::mojom::FileChooserParams::Mode::kSave) {
    listener->FileSelectionCanceled(); return;
  }
  listener = base::MakeRefCounted<FrameBoundFileListener>(frame, std::move(listener));
  if (params.mode == blink::mojom::FileChooserParams::Mode::kUploadFolder) {
    base::MakeRefCounted<FolderChooser>(frame, std::move(listener))->Show();
    return;
  }
  FileSelectHelper::RunFileChooser(frame, std::move(listener), params);
}

void EnumeratePageDirectory(content::WebContents* contents,
                            scoped_refptr<content::FileSelectListener> listener,
                            const base::FilePath& path) {
  if (!listener) return;
  if (!contents || contents->GetVisibility() == content::Visibility::HIDDEN ||
      path.empty() || !path.IsAbsolute() || path.ReferencesParent()) {
    listener->FileSelectionCanceled(); return;
  }
  listener = base::MakeRefCounted<FrameBoundFileListener>(
      contents->GetPrimaryMainFrame(), std::move(listener));
  FileSelectHelper::EnumerateDirectory(contents, std::move(listener), path);
}
}  // namespace zitter::prototype
