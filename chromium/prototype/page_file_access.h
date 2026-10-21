#ifndef ZITTER_CHROMIUM_PROTOTYPE_PAGE_FILE_ACCESS_H_
#define ZITTER_CHROMIUM_PROTOTYPE_PAGE_FILE_ACCESS_H_

#include "base/memory/scoped_refptr.h"
#include "third_party/blink/public/mojom/choosers/file_chooser.mojom.h"

namespace base { class FilePath; }
namespace content {
class FileSelectListener;
class RenderFrameHost;
class WebContents;
}

namespace zitter::prototype {
// The requesting frame, rather than the active workspace, determines the
// profile. The delegate must additionally check that it owns the requesting tab.
void RunPageFileChooser(content::RenderFrameHost* frame,
                        scoped_refptr<content::FileSelectListener> listener,
                        const blink::mojom::FileChooserParams& params);
void EnumeratePageDirectory(content::WebContents* contents,
                            scoped_refptr<content::FileSelectListener> listener,
                            const base::FilePath& path);
}  // namespace zitter::prototype

#endif  // ZITTER_CHROMIUM_PROTOTYPE_PAGE_FILE_ACCESS_H_
