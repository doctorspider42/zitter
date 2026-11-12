// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CONTENT_WORKSPACE_PRINTING_H_
#define ZITTER_CONTENT_WORKSPACE_PRINTING_H_

#include <memory>
#include <string>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "content/public/browser/weak_document_ptr.h"
#include "ui/gfx/geometry/size_f.h"
#include "pdf/buildflags.h"

#if BUILDFLAG(ENABLE_PDF)
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "zitter_content/pdf_preview.mojom-forward.h"
#endif
#include "ui/shell_dialogs/select_file_dialog.h"

namespace base {
class RefCountedMemory;
}
namespace content {
class RenderFrame;
class RenderFrameHost;
class WebContents;
}
namespace zitter::prototype {
struct PrintOptions;
}

namespace ui {
class ScopedSetSelectFileDialogFactory;
}

namespace zitter::content_embedder {

void AttachPrintRenderer(content::RenderFrame* frame);
#if BUILDFLAG(ENABLE_PDF)
void AttachScriptedPrint(
    content::WebContents& page,
    base::RepeatingCallback<void(content::RenderFrameHost&)> callback);
void BindScriptedPrint(content::RenderFrameHost* frame,
                       mojo::PendingReceiver<mojom::ScriptedPrint> receiver);
#endif
void RenderWorkspacePdf(
    content::WebContents& page,
    const prototype::PrintOptions& options,
    const std::string& ranges,
    base::OnceCallback<void(scoped_refptr<base::RefCountedMemory>)> done,
    gfx::SizeF paper_points = gfx::SizeF(612, 792));

class WorkspacePdfExport final : private ui::SelectFileDialog::Listener {
 public:
  enum class Result { kSaved, kCancelled, kFailed };
  using Done = base::OnceCallback<void(Result)>;

  WorkspacePdfExport(content::WebContents& page, Done done);
  ~WorkspacePdfExport() override;

  void Start(const prototype::PrintOptions& options, const std::string& ranges);
#if BUILDFLAG(IS_MAC)
  void StartSystem(bool cancel_probe, const base::FilePath& probe_file);
#endif

 private:
  bool HasCurrentDocument() const;
  void OnPdfReady(scoped_refptr<base::RefCountedMemory> pdf);
  void FileSelected(const ui::SelectedFileInfo& file, int index) override;
  void FileSelectionCanceled() override;
  void Finish(Result result);

  const base::WeakPtr<content::WebContents> page_;
  const content::WeakDocumentPtr document_;
  Done done_;
  scoped_refptr<base::RefCountedMemory> pdf_;
  scoped_refptr<ui::SelectFileDialog> picker_;
  base::OneShotTimer render_deadline_;
  base::WeakPtrFactory<WorkspacePdfExport> weak_factory_{this};
};

std::unique_ptr<ui::ScopedSetSelectFileDialogFactory> InstallPdfSaveProbePicker(
    base::FilePath path);

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_WORKSPACE_PRINTING_H_
