// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CONTENT_LAB_WEB_VIEW_H_
#define ZITTER_CONTENT_LAB_WEB_VIEW_H_

#include <memory>

#include "content/public/browser/web_contents_delegate.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/window_open_disposition.h"
#include "ui/views/controls/webview/webview.h"

namespace zitter::content_embedder {

class LabWebView : public views::WebView {
 public:
  explicit LabWebView(content::BrowserContext* context);
  ~LabWebView() override;
  METADATA_HEADER(LabWebView, views::WebView)

 protected:
  // Document picture-in-picture windows; other dispositions are refused.
  content::WebContents* AddNewContents(
      content::WebContents* source, std::unique_ptr<content::WebContents> new_contents,
      const GURL& target_url, WindowOpenDisposition disposition,
      const blink::mojom::WindowFeatures& window_features, bool user_gesture,
      bool* was_blocked) override;
  content::PictureInPictureResult EnterPictureInPicture(
      content::WebContents* page) override;
  void ExitPictureInPicture() override;

 private:
  void PrintCrossProcessSubframe(content::WebContents* contents,
                                 const gfx::Rect& rect,
                                 int document_cookie,
                                 content::RenderFrameHost* subframe) const override;
  void RunFileChooser(
      content::RenderFrameHost* frame,
      scoped_refptr<content::FileSelectListener> listener,
      const blink::mojom::FileChooserParams& params) override;
  void EnumerateDirectory(
      content::WebContents* contents,
      scoped_refptr<content::FileSelectListener> listener,
      const base::FilePath& path) override;
};

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_LAB_WEB_VIEW_H_
