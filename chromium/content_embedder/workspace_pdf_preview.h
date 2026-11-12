// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CONTENT_WORKSPACE_PDF_PREVIEW_H_
#define ZITTER_CONTENT_WORKSPACE_PDF_PREVIEW_H_

#include <optional>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "chromium/prototype/print_options.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "ui/gfx/image/image_skia.h"
#include "zitter_content/pdf_preview.mojom.h"

namespace base {
class RefCountedMemory;
}
namespace content {
class WebContents;
}
namespace mojo {
class ServiceFactory;
}

namespace zitter::content_embedder {

void RegisterPdfPreviewService(mojo::ServiceFactory& services);

class WorkspacePdfPreview {
 public:
  using Result = base::RepeatingCallback<void(std::vector<gfx::ImageSkia>)>;

  WorkspacePdfPreview(content::WebContents& page, Result result);
  ~WorkspacePdfPreview();

  void Request(prototype::PrintOptions options);
  int page_count() const { return page_count_; }

 private:
  void StartPending();
  void OnPdfReady(scoped_refptr<base::RefCountedMemory> pdf);
  void OnRasterReady(std::optional<std::vector<mojom::PreviewPagePtr>> pages);
  void Finish(std::vector<gfx::ImageSkia> images);

  base::WeakPtr<content::WebContents> page_;
  Result result_;
  std::optional<prototype::PrintOptions> pending_;
  bool running_ = false;
  int page_count_ = 0;
  base::OneShotTimer deadline_;
  mojo::Remote<mojom::PdfPreview> raster_;
  base::WeakPtrFactory<WorkspacePdfPreview> weak_factory_{this};
};

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_WORKSPACE_PDF_PREVIEW_H_
