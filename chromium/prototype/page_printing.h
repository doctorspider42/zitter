#ifndef ZITTER_CHROMIUM_PROTOTYPE_PAGE_PRINTING_H_
#define ZITTER_CHROMIUM_PROTOTYPE_PAGE_PRINTING_H_

#include <memory>
#include <string>
#include <vector>
#include "base/functional/callback.h"
#include "chromium/prototype/print_options.h"
#include "base/memory/ref_counted_memory.h"
#include "base/memory/weak_ptr.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/gfx/image/image_skia.h"

namespace printing { class PwgRasterConverter; }
namespace zitter::prototype {

// Chromium's printing for a page, with a page's own window.print() handed to
// `scripted` (Zitter's print sheet) instead of the system dialog.
void AttachPagePrinting(content::WebContents* page, base::RepeatingCallback<void(content::WebContents*)> scripted);

// Chromium prints; Zitter only asks. The page goes to PDF in the renderer
// (Chrome's PrintToPdf), the preview is rasterized by the sandboxed printing
// service (PDF to PWG raster), and printers get the PDF through Chrome's print
// job queue, the way Print Preview hands it over.
class PagePrinter final : public content::WebContentsObserver {
 public:
  struct Preview {
    std::vector<gfx::ImageSkia> pages;
    bool failed = false;
  };
  explicit PagePrinter(content::WebContents* page);
  ~PagePrinter() override;

  // The printers on this computer, the default one first. Runs `done` on the
  // UI thread.
  static void ListPrinters(base::OnceCallback<void(std::vector<PrinterChoice>)> done);
  // A preview of every page with these layout options.
  void Render(const PrintOptions& options, base::OnceCallback<void(Preview)> done);
  // Prints or, without a printer, asks where to save the PDF. `done` gets an
  // empty string on success, or the reason it failed.
  void Print(const PrintOptions& options, base::OnceCallback<void(std::u16string)> done);
  // Windows' or macOS's own print dialog.
  void PrintWithSystemDialog();

 private:
  void WebContentsDestroyed() override;
  void MakePdf(const PrintOptions& options, std::string ranges,
               base::OnceCallback<void(scoped_refptr<base::RefCountedMemory>)> done);
  void SavePdf(scoped_refptr<base::RefCountedMemory> pdf, base::OnceCallback<void(std::u16string)> done);
  int generation_ = 0;
  int page_count_ = 0;
  std::unique_ptr<printing::PwgRasterConverter> converter_;
  base::WeakPtrFactory<PagePrinter> weak_factory_{this};
};

}  // namespace zitter::prototype
#endif
