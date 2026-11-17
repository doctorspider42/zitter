// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "lab_web_view.h"

#include <utility>

#include "content/public/browser/file_select_listener.h"
#include "page_file_chooser.h"
#include "picture_in_picture.h"
#include "printing/buildflags/buildflags.h"
#include "ui/base/metadata/metadata_impl_macros.h"

#if BUILDFLAG(IS_MAC)
#include "components/input/native_web_keyboard_event.h"
#include "content/public/browser/web_contents.h"
#include "ui/content_accelerators/accelerator_util.h"
#endif
#if BUILDFLAG(ENABLE_PRINTING)
#include "components/printing/browser/print_composite_client.h"
#endif

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

content::WebContents* LabWebView::AddNewContents(
    content::WebContents* source, std::unique_ptr<content::WebContents> new_contents,
    const GURL& target_url, WindowOpenDisposition disposition,
    const blink::mojom::WindowFeatures& window_features, bool user_gesture,
    bool* was_blocked) {
  if (disposition != WindowOpenDisposition::NEW_PICTURE_IN_PICTURE) {
    return views::WebView::AddNewContents(source, std::move(new_contents), target_url,
                                          disposition, window_features, user_gesture,
                                          was_blocked);
  }
  auto* page = PictureInPicture::Get().OpenDocument(source, std::move(new_contents));
  if (!page && was_blocked)
    *was_blocked = true;
  return page;
}

content::PictureInPictureResult LabWebView::EnterPictureInPicture(
    content::WebContents* page) {
  return PictureInPicture::Get().EnterVideo(page);
}

void LabWebView::ExitPictureInPicture() {
  PictureInPicture::Get().Exit(web_contents());
}

#if BUILDFLAG(IS_MAC)
bool LabWebView::HandleKeyboardEvent(
    content::WebContents* source,
    const input::NativeWebKeyboardEvent& event) {
  // AppKit's key equivalent has already been consumed even if the page
  // declines the shortcut. Apply the edit to the focused WebView.
  using Type = blink::WebInputEvent::Type;
  if (!event.skip_if_unhandled && source && source == web_contents() &&
      IsDrawn() && HasFocus() &&
      (event.GetType() == Type::kRawKeyDown || event.GetType() == Type::kKeyDown)) {
    const auto accelerator = ui::GetAcceleratorFromNativeWebKeyboardEvent(event);
    if (accelerator.modifiers() == ui::EF_COMMAND_DOWN) {
      if (accelerator.key_code() == ui::VKEY_C) {
        source->Copy();
        return true;
      }
      if (accelerator.key_code() == ui::VKEY_V) {
        source->Paste();
        return true;
      }
    }
  }
  return WebView::HandleKeyboardEvent(source, event);
}
#endif

BEGIN_METADATA(LabWebView)
END_METADATA

}  // namespace zitter::content_embedder
