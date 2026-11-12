// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "workspace_pdf_preview.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "base/check.h"
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/memory/ref_counted_memory.h"
#include "content/public/browser/service_process_host.h"
#include "content/public/browser/web_contents.h"
#include "mojo/public/cpp/base/big_buffer.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/service_factory.h"
#include "pdf/pdf.h"
#include "sandbox/policy/sandbox.h"
#include "sandbox/policy/switches.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkPixmap.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/geometry/size_f.h"
#include "workspace_printing.h"

namespace zitter::content_embedder {
namespace {

constexpr int kMaxPages = 64;
constexpr int kMaxSide = 480;
constexpr size_t kMaxPdfBytes = 64 * 1024 * 1024;

class PdfPreviewService final : public mojom::PdfPreview {
 public:
  explicit PdfPreviewService(mojo::PendingReceiver<mojom::PdfPreview> receiver)
      : receiver_(this, std::move(receiver)) {
    CHECK(!base::CommandLine::ForCurrentProcess()->HasSwitch(
        sandbox::policy::switches::kNoSandbox));
    CHECK(sandbox::policy::Sandbox::IsProcessSandboxed());
  }
  ~PdfPreviewService() override = default;

  void Rasterize(mojo_base::BigBuffer pdf, RasterizeCallback callback) override {
    int count = 0;
    if (pdf.size() > kMaxPdfBytes ||
        !chrome_pdf::GetPDFDocInfo(pdf, &count, nullptr) ||
        count <= 0 || count > kMaxPages) {
      std::move(callback).Run(std::nullopt);
      return;
    }

    const chrome_pdf::RenderOptions options = {
        .stretch_to_bounds = true,
        .keep_aspect_ratio = true,
        .autorotate = false,
        .use_color = true,
        .render_device_type = chrome_pdf::RenderDeviceType::kDisplay,
    };
    std::vector<mojom::PreviewPagePtr> pages;
    pages.reserve(count);
    for (int index = 0; index < count; ++index) {
      const auto size = chrome_pdf::GetPDFPageSizeByIndex(pdf, index);
      if (!size || size->IsEmpty() ||
          !std::isfinite(size->width()) || !std::isfinite(size->height())) {
        std::move(callback).Run(std::nullopt);
        return;
      }
      const float longest = std::max(size->width(), size->height());
      const gfx::Size pixels(
          std::max(1, static_cast<int>(size->width() / longest * kMaxSide)),
          std::max(1, static_cast<int>(size->height() / longest * kMaxSide)));
      mojo_base::BigBuffer bitmap(static_cast<size_t>(pixels.GetArea()) * 4);
      if (!chrome_pdf::RenderPDFPageToBitmap(
              pdf, index, bitmap.data(), pixels, gfx::Size(72, 72), options)) {
        std::move(callback).Run(std::nullopt);
        return;
      }
      pages.push_back(mojom::PreviewPage::New(
          pixels.width(), pixels.height(), std::move(bitmap)));
    }
    std::move(callback).Run(std::move(pages));
  }

 private:
  mojo::Receiver<mojom::PdfPreview> receiver_;
};

}  // namespace

void RegisterPdfPreviewService(mojo::ServiceFactory& services) {
  services.Add(+[](mojo::PendingReceiver<mojom::PdfPreview> receiver) {
    return std::make_unique<PdfPreviewService>(std::move(receiver));
  });
}

WorkspacePdfPreview::WorkspacePdfPreview(content::WebContents& page,
                                         Result result)
    : page_(page.GetWeakPtr()), result_(std::move(result)) {}

WorkspacePdfPreview::~WorkspacePdfPreview() = default;

void WorkspacePdfPreview::Request(prototype::PrintOptions options) {
  pending_ = std::move(options);
  if (!running_)
    StartPending();
}

void WorkspacePdfPreview::StartPending() {
  CHECK(pending_);
  auto options = std::exchange(pending_, std::nullopt);
  running_ = true;
  page_count_ = 0;
  if (!page_) {
    Finish({});
    return;
  }
  deadline_.Start(FROM_HERE, base::Seconds(30),
                  base::BindOnce(&WorkspacePdfPreview::Finish,
                                 weak_factory_.GetWeakPtr(),
                                 std::vector<gfx::ImageSkia>()));
  RenderWorkspacePdf(
      *page_, *options, {},
      base::BindOnce(&WorkspacePdfPreview::OnPdfReady, weak_factory_.GetWeakPtr()));
}

void WorkspacePdfPreview::OnPdfReady(scoped_refptr<base::RefCountedMemory> pdf) {
  if (pending_ || !pdf || pdf->size() > kMaxPdfBytes) {
    Finish({});
    return;
  }
  if (!raster_) {
    raster_ = content::ServiceProcessHost::Launch<mojom::PdfPreview>(
        content::ServiceProcessHost::Options().WithDisplayName(
            "Zitter PDF preview").Pass());
  }
  raster_->Rasterize(
      mojo_base::BigBuffer(base::span<const uint8_t>(*pdf)),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(&WorkspacePdfPreview::OnRasterReady,
                         weak_factory_.GetWeakPtr()),
          std::nullopt));
}

void WorkspacePdfPreview::OnRasterReady(
    std::optional<std::vector<mojom::PreviewPagePtr>> pages) {
  if (pending_ || !pages || pages->empty() || pages->size() > kMaxPages) {
    Finish({});
    return;
  }
  std::vector<gfx::ImageSkia> images;
  images.reserve(pages->size());
  for (const auto& page : *pages) {
    SkBitmap bitmap;
    if (!page || !page->width || !page->height ||
        page->width > kMaxSide || page->height > kMaxSide ||
        !bitmap.tryAllocN32Pixels(page->width, page->height, true) ||
        bitmap.computeByteSize() != page->pixels.size() ||
        !bitmap.writePixels(
            SkPixmap(bitmap.info(), page->pixels.data(), bitmap.rowBytes()))) {
      Finish({});
      return;
    }
    bitmap.setImmutable();
    images.push_back(gfx::ImageSkia::CreateFrom1xBitmap(bitmap));
  }
  Finish(std::move(images));
}

void WorkspacePdfPreview::Finish(std::vector<gfx::ImageSkia> images) {
  // Retire outstanding callbacks before starting a newer render.
  weak_factory_.InvalidateWeakPtrs();
  deadline_.Stop();
  raster_.reset();
  running_ = false;
  if (pending_) {
    StartPending();
    return;
  }
  page_count_ = images.size();
  result_.Run(std::move(images));
}

}  // namespace zitter::content_embedder
