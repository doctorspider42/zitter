// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "lab_web_view.h"

#include <utility>

#include "content/public/browser/file_select_listener.h"
#include "page_file_chooser.h"
#include "printing/buildflags/buildflags.h"

#if BUILDFLAG(ENABLE_PRINTING)
#include "components/printing/browser/print_composite_client.h"
#endif
#include "ui/base/metadata/metadata_impl_macros.h"

namespace zitter::content_embedder {

LabWebView::LabWebView(content::BrowserContext* context) : WebView(context) {}
LabWebView::~LabWebView() = default;

void LabWebView::PrintCrossProcessSubframe(
    content::WebContents* contents,
    const gfx::Rect& rect,
    int document_cookie,
    content::RenderFrameHost* subframe) const {
#if BUILDFLAG(ENABLE_PRINTING)
  if (auto* client = printing::PrintCompositeClient::FromWebContents(contents))
    client->PrintCrossProcessSubframe(rect, document_cookie, subframe);
#endif
}

void LabWebView::RunFileChooser(
    content::RenderFrameHost* frame,
    scoped_refptr<content::FileSelectListener> listener,
    const blink::mojom::FileChooserParams& params) {
  RunPageFileChooser(frame, std::move(listener), params);
}

void LabWebView::EnumerateDirectory(
    content::WebContents* contents,
    scoped_refptr<content::FileSelectListener> listener,
    const base::FilePath& path) {
  EnumeratePageDirectory(contents, std::move(listener), path);
}

BEGIN_METADATA(LabWebView)
END_METADATA

}  // namespace zitter::content_embedder
