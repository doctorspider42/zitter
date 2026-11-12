// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CONTENT_PAGE_FILE_CHOOSER_H_
#define ZITTER_CONTENT_PAGE_FILE_CHOOSER_H_

#include "base/memory/scoped_refptr.h"
#include "third_party/blink/public/mojom/choosers/file_chooser.mojom-forward.h"

namespace base { class FilePath; }
namespace content {
class FileSelectListener;
class RenderFrameHost;
class WebContents;
}

namespace zitter::content_embedder {

void RunPageFileChooser(content::RenderFrameHost* frame,
                        scoped_refptr<content::FileSelectListener> listener,
                        const blink::mojom::FileChooserParams& params);
void EnumeratePageDirectory(content::WebContents* contents,
                            scoped_refptr<content::FileSelectListener> listener,
                            const base::FilePath& path);

}  // namespace zitter::content_embedder

#endif  // ZITTER_CONTENT_PAGE_FILE_CHOOSER_H_
