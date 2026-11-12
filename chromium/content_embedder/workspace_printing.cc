// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "workspace_printing.h"

#include <utility>
#include <variant>

#include "base/files/important_file_writer.h"
#include "base/functional/bind.h"
#include "base/i18n/file_util_icu.h"
#include "base/memory/ref_counted_memory.h"
#include "base/strings/string_view_util.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "chromium/prototype/i18n.h"
#include "chromium/prototype/print_options.h"
#include "components/printing/browser/headless/headless_print_manager.h"
#include "components/printing/browser/print_to_pdf/pdf_print_result.h"
#include "components/printing/browser/print_to_pdf/pdf_print_utils.h"
#include "components/printing/renderer/print_render_frame_helper.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/weak_document_ptr.h"
#include "content/public/browser/web_contents.h"
#include "printing/mojom/print.mojom.h"
#include "third_party/blink/public/web/web_element.h"
#include "ui/shell_dialogs/select_file_dialog_factory.h"
#include "ui/shell_dialogs/select_file_policy.h"
#include "ui/shell_dialogs/selected_file_info.h"

#if BUILDFLAG(ENABLE_PDF)
#include "content/public/browser/document_service.h"
#include "content/public/browser/web_contents_user_data.h"
#include "content/public/renderer/render_frame.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "third_party/blink/public/platform/browser_interface_broker_proxy.h"
#include "zitter_content/pdf_preview.mojom.h"
#endif

#if BUILDFLAG(IS_MAC)
#include "system_print_mac.h"
#endif

namespace zitter::content_embedder {
namespace {

class WorkspacePrintDelegate final
    : public printing::PrintRenderFrameHelper::Delegate {
 public:
  blink::WebElement GetPdfElement(blink::WebLocalFrame*) override {
    return blink::WebElement();
  }
  bool IsPrintPreviewEnabled() override { return false; }
  bool IsScriptedPrintEnabled() override { return BUILDFLAG(ENABLE_PDF); }
  bool OverridePrint(blink::WebLocalFrame* frame) override {
#if BUILDFLAG(ENABLE_PDF)
    auto* render_frame = content::RenderFrame::FromWebFrame(frame);
    if (render_frame && render_frame->IsMainFrame()) {
      mojo::Remote<mojom::ScriptedPrint> remote;
      render_frame->GetBrowserInterfaceBroker().GetInterface(
          remote.BindNewPipeAndPassReceiver());
      remote->OpenPrintSheet();
    }
    // The sheet generates a fresh copy after the script's call returns.
    return true;
#else
    return false;
#endif
  }
};

#if BUILDFLAG(ENABLE_PDF)
class ScriptedPrintTarget : public content::WebContentsUserData<ScriptedPrintTarget> {
 public:
  ~ScriptedPrintTarget() override = default;

  void Request(content::RenderFrameHost& frame) { callback_.Run(frame); }

 private:
  friend class content::WebContentsUserData<ScriptedPrintTarget>;
  ScriptedPrintTarget(content::WebContents* contents,
                      base::RepeatingCallback<void(content::RenderFrameHost&)> callback)
      : WebContentsUserData(*contents), callback_(std::move(callback)) {}

  base::RepeatingCallback<void(content::RenderFrameHost&)> callback_;
  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(ScriptedPrintTarget);

class ScriptedPrintService : public content::DocumentService<mojom::ScriptedPrint> {
 public:
  ScriptedPrintService(content::RenderFrameHost& frame,
                       mojo::PendingReceiver<mojom::ScriptedPrint> receiver)
      : DocumentService(frame, std::move(receiver)) {}
  ~ScriptedPrintService() override = default;

  void OpenPrintSheet() override {
    auto& frame = render_frame_host();
    auto* page = content::WebContents::FromRenderFrameHost(&frame);
    if (!frame.IsActive() || !frame.IsInPrimaryMainFrame() || !page ||
        page->GetVisibility() != content::Visibility::VISIBLE) {
      return;
    }
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce([](content::WeakDocumentPtr document) {
          auto* frame = document.AsRenderFrameHostIfValid();
          if (!frame || !frame->IsActive() || !frame->IsInPrimaryMainFrame())
            return;
          auto* page = content::WebContents::FromRenderFrameHost(frame);
          if (!page || page->GetVisibility() != content::Visibility::VISIBLE)
            return;
          if (auto* target = ScriptedPrintTarget::FromWebContents(page))
            target->Request(*frame);
        }, frame.GetWeakDocumentPtr()));
  }
};
#endif

}  // namespace

void AttachPrintRenderer(content::RenderFrame* frame) {
  new printing::PrintRenderFrameHelper(
      frame, std::make_unique<WorkspacePrintDelegate>());
}

#if BUILDFLAG(ENABLE_PDF)
void AttachScriptedPrint(
    content::WebContents& page,
    base::RepeatingCallback<void(content::RenderFrameHost&)> callback) {
  ScriptedPrintTarget::CreateForWebContents(&page, std::move(callback));
}

void BindScriptedPrint(content::RenderFrameHost* frame,
                       mojo::PendingReceiver<mojom::ScriptedPrint> receiver) {
  if (frame && frame->IsInPrimaryMainFrame())
    new ScriptedPrintService(*frame, std::move(receiver));
}
#endif

void RenderWorkspacePdf(
    content::WebContents& page,
    const prototype::PrintOptions& options,
    const std::string& ranges,
    base::OnceCallback<void(scoped_refptr<base::RefCountedMemory>)> done,
    gfx::SizeF paper_points) {
  auto* manager = headless::HeadlessPrintManager::FromWebContents(&page);
  auto* frame = page.GetPrimaryMainFrame();
  if (!manager || !frame->IsRenderFrameLive()) {
    std::move(done).Run(nullptr);
    return;
  }

  using Margins = prototype::PrintOptions::Margins;
  const double margin = options.margins == Margins::kNone ? 0
      : options.margins == Margins::kMinimal ? 0.25 : 1.0 / 2.54;
  auto params = print_to_pdf::GetPrintPagesParams(
      frame->GetLastCommittedURL(), options.landscape, options.headers,
      options.backgrounds, 1.0, paper_points.width() / 72.0,
      paper_points.height() / 72.0, margin, margin, margin, margin,
      std::nullopt, std::nullopt, false, std::nullopt, std::nullopt);
  auto* pages = std::get_if<printing::mojom::PrintPagesParamsPtr>(&params);
  if (!pages) {
    std::move(done).Run(nullptr);
    return;
  }

  manager->PrintToPdf(
      frame, ranges, std::move(*pages),
      base::BindOnce(
          [](content::WeakDocumentPtr document,
             base::OnceCallback<void(scoped_refptr<base::RefCountedMemory>)> done,
             print_to_pdf::PdfPrintResult result,
             scoped_refptr<base::RefCountedMemory> pdf) {
            auto* frame = document.AsRenderFrameHostIfValid();
            std::move(done).Run(
                frame && frame->IsActive() &&
                        result == print_to_pdf::PdfPrintResult::kPrintSuccess
                    ? std::move(pdf) : nullptr);
          },
          frame->GetWeakDocumentPtr(), std::move(done)));
}

WorkspacePdfExport::WorkspacePdfExport(content::WebContents& page, Done done)
    : page_(page.GetWeakPtr()),
      document_(page.GetPrimaryMainFrame()->GetWeakDocumentPtr()),
      done_(std::move(done)) {}

WorkspacePdfExport::~WorkspacePdfExport() {
  if (picker_)
    picker_->ListenerDestroyed();
}

bool WorkspacePdfExport::HasCurrentDocument() const {
  auto* frame = document_.AsRenderFrameHostIfValid();
  return page_ && frame && frame->IsActive() &&
         frame == page_->GetPrimaryMainFrame() &&
         page_->GetVisibility() != content::Visibility::HIDDEN;
}

void WorkspacePdfExport::Start(const prototype::PrintOptions& options,
                               const std::string& ranges) {
  if (!HasCurrentDocument()) {
    Finish(Result::kCancelled);
    return;
  }
  render_deadline_.Start(
      FROM_HERE, base::Seconds(45),
      base::BindOnce(&WorkspacePdfExport::Finish, weak_factory_.GetWeakPtr(),
                     Result::kFailed));
  RenderWorkspacePdf(
      *page_, options, ranges,
      base::BindOnce(&WorkspacePdfExport::OnPdfReady, weak_factory_.GetWeakPtr()));
}

#if BUILDFLAG(IS_MAC)
void WorkspacePdfExport::StartSystem(bool cancel_probe,
                                     const base::FilePath& probe_file) {
  if (!HasCurrentDocument()) {
    Finish(Result::kCancelled);
    return;
  }
  ShowMacPrintPanel(
      page_->GetTopLevelNativeWindow(), page_->GetTitle(), cancel_probe,
      probe_file, base::BindOnce([](base::WeakPtr<WorkspacePdfExport> self,
                         std::optional<SystemPrintSettings> settings) {
        if (!self)
          return;
        if (!settings || !self->HasCurrentDocument()) {
          self->Finish(Result::kCancelled);
          return;
        }
        prototype::PrintOptions options;
        options.landscape = settings->landscape;
        self->render_deadline_.Start(
            FROM_HERE, base::Seconds(45),
            base::BindOnce(&WorkspacePdfExport::Finish, self, Result::kFailed));
        RenderWorkspacePdf(
            *self->page_, options, {},
            base::BindOnce([](base::WeakPtr<WorkspacePdfExport> self,
                              decltype(settings->submit) submit,
                              scoped_refptr<base::RefCountedMemory> pdf) {
              if (!self || !self->done_)
                return;
              self->render_deadline_.Stop();
              if (!self->HasCurrentDocument()) {
                self->Finish(Result::kCancelled);
                return;
              }
              if (!pdf || pdf->size() > 64 * 1024 * 1024) {
                self->Finish(Result::kFailed);
                return;
              }
              std::move(submit).Run(
                  std::move(pdf),
                  base::BindOnce([](base::WeakPtr<WorkspacePdfExport> self,
                                    bool submitted) {
                    if (self)
                      self->Finish(submitted ? Result::kSaved : Result::kFailed);
                  }, self));
            }, self, std::move(settings->submit)), settings->paper_points);
      }, weak_factory_.GetWeakPtr()));
}
#endif

void WorkspacePdfExport::OnPdfReady(scoped_refptr<base::RefCountedMemory> pdf) {
  render_deadline_.Stop();
  if (!done_)
    return;
  if (!HasCurrentDocument()) {
    Finish(Result::kCancelled);
    return;
  }
  if (!pdf || pdf->size() > 64 * 1024 * 1024) {
    Finish(Result::kFailed);
    return;
  }
  pdf_ = std::move(pdf);
  picker_ = ui::SelectFileDialog::Create(this, nullptr);
  if (!picker_) {
    Finish(Result::kFailed);
    return;
  }

  auto title = page_->GetTitle().substr(0, 100);
  auto name = base::FilePath::FromUTF16Unsafe(title.empty() ? u"Zitter" : title)
                  .value();
  base::i18n::ReplaceIllegalCharactersInPath(&name, '_');
  ui::SelectFileDialog::FileTypeInfo types;
  types.extensions = {{FILE_PATH_LITERAL("pdf")}};
  auto picker = picker_;
  picker->SelectFile(ui::SelectFileDialog::SELECT_SAVEAS_FILE,
                     prototype::T(u"Zapisz jako PDF"),
                     base::FilePath(name).AddExtension(FILE_PATH_LITERAL("pdf")),
                     &types, 1, FILE_PATH_LITERAL("pdf"),
                     page_->GetTopLevelNativeWindow());
}

void WorkspacePdfExport::FileSelected(const ui::SelectedFileInfo& file, int) {
  if (!done_ || !picker_)
    return;
  if (!HasCurrentDocument()) {
    Finish(Result::kCancelled);
    return;
  }
  const auto& path = file.local_path;
  if (!path.IsAbsolute() || path.ReferencesParent() || path.IsNetwork() ||
      !path.MatchesFinalExtension(FILE_PATH_LITERAL(".pdf"))) {
    Finish(Result::kFailed);
    return;
  }
  picker_->ListenerDestroyed();
  picker_.reset();
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
                  base::TaskShutdownBehavior::BLOCK_SHUTDOWN},
      base::BindOnce([](base::FilePath path,
                        scoped_refptr<base::RefCountedMemory> pdf) {
        return base::ImportantFileWriter::WriteFileAtomically(
            path, base::as_string_view(base::span<const uint8_t>(*pdf)));
      }, path, std::move(pdf_)),
      base::BindOnce([](base::WeakPtr<WorkspacePdfExport> self, bool saved) {
        if (self)
          self->Finish(saved ? Result::kSaved : Result::kFailed);
      }, weak_factory_.GetWeakPtr()));
}

void WorkspacePdfExport::FileSelectionCanceled() {
  Finish(Result::kCancelled);
}

void WorkspacePdfExport::Finish(Result result) {
  if (!done_)
    return;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
          [](base::WeakPtr<WorkspacePdfExport> self, Done done, Result result) {
            if (self)
              std::move(done).Run(result);
          }, weak_factory_.GetWeakPtr(), std::move(done_), result));
}

namespace {

class PdfProbeDialog final : public ui::SelectFileDialog {
 public:
  PdfProbeDialog(Listener* listener,
                 std::unique_ptr<ui::SelectFilePolicy> policy,
                 base::FilePath path)
      : SelectFileDialog(listener, std::move(policy)), path_(std::move(path)) {}

  bool IsRunning(gfx::NativeWindow) const override { return listener_ != nullptr; }
  void ListenerDestroyed() override { listener_ = nullptr; }

 private:
  ~PdfProbeDialog() override = default;
  bool HasMultipleFileTypeChoicesImpl() override { return false; }
  void SelectFileImpl(Type type,
                      const std::u16string& title,
                      const base::FilePath& suggested,
                      const FileTypeInfo* file_types,
                      int file_type_index,
                      const base::FilePath::StringType& extension,
                      gfx::NativeWindow owner,
                      const GURL*) override {
    const bool valid = type == SELECT_SAVEAS_FILE && !title.empty() && owner &&
                       suggested.MatchesFinalExtension(FILE_PATH_LITERAL(".pdf")) &&
                       file_types && file_types->extensions.size() == 1 &&
                       file_type_index == 1 && extension == FILE_PATH_LITERAL("pdf");
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce([](scoped_refptr<PdfProbeDialog> self, bool valid) {
          if (!self->listener_)
            return;
          if (valid)
            self->listener_->FileSelected(ui::SelectedFileInfo(self->path_), 1);
          else
            self->listener_->FileSelectionCanceled();
        }, base::WrapRefCounted(this), valid));
  }
  const base::FilePath path_;
};

class PdfProbeFactory final : public ui::SelectFileDialogFactory {
 public:
  explicit PdfProbeFactory(base::FilePath path) : path_(std::move(path)) {}
  ~PdfProbeFactory() override = default;

  ui::SelectFileDialog* Create(
      ui::SelectFileDialog::Listener* listener,
      std::unique_ptr<ui::SelectFilePolicy> policy) override {
    return new PdfProbeDialog(listener, std::move(policy), path_);
  }

 private:
  const base::FilePath path_;
};

}  // namespace

std::unique_ptr<ui::ScopedSetSelectFileDialogFactory> InstallPdfSaveProbePicker(
    base::FilePath path) {
  return std::make_unique<ui::ScopedSetSelectFileDialogFactory>(
      std::make_unique<PdfProbeFactory>(std::move(path)));
}

}  // namespace zitter::content_embedder
