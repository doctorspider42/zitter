// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/prototype/page_printing.h"

#include <algorithm>
#include <utility>

#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/i18n/file_util_icu.h"
#include "base/numerics/byte_conversions.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/printing/print_view_manager.h"
#include "chrome/browser/printing/print_view_manager_common.h"
#include "chrome/browser/printing/pwg_raster_converter.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/select_file_policy/chrome_select_file_policy.h"
#include "chromium/prototype/i18n.h"
#include "components/printing/browser/print_to_pdf/pdf_print_result.h"
#include "components/printing/browser/print_to_pdf/pdf_print_utils.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "printing/backend/print_backend.h"
#include "printing/mojom/print.mojom.h"
#include "printing/pdf_render_settings.h"
#include "printing/print_job_constants.h"
#include "printing/units.h"
#include "printing/pwg_raster_settings.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/shell_dialogs/select_file_dialog.h"
#include "ui/shell_dialogs/selected_file_info.h"

namespace zitter::prototype {
namespace {
// Preview pages are drawn at 100 dpi: sharp at 200% in the sheet's 300 px page.
constexpr int kPreviewDpi = 100;
constexpr double kMicronsPerInch = 25400.0;

// Letter where it is the usual paper (US, Canada, Mexico), A4 elsewhere.
gfx::Size PaperMicrons() {
  const std::string locale = g_browser_process->GetApplicationLocale();
  for (const char* region : {"-US", "-CA", "-MX"})
    if (base::EndsWith(locale, region, base::CompareCase::INSENSITIVE_ASCII)) return gfx::Size(215900, 279400);
  return gfx::Size(210000, 297000);
}

// The sandboxed service's PWG raster (components/pwg_encoder): "RaS2", then
// per page a 1796-byte header and rows packed as PackBits runs of pixels.
std::vector<gfx::ImageSkia> DecodePwg(base::span<const uint8_t> data) {
  std::vector<gfx::ImageSkia> pages;
  constexpr size_t kHeader = 1796;
  if (data.size() < 4 || data.first(4u) != base::as_byte_span(std::string_view("RaS2"))) return pages;
  size_t at = 4;
  auto be32 = [&](size_t offset) { return base::U32FromBigEndian(data.subspan(at + offset).first<4u>()); };
  while (at + kHeader <= data.size() && pages.size() < 500) {
    const uint32_t width = be32(372), height = be32(376), bits = be32(388);
    at += kHeader;
    const size_t channels = bits / 8;
    if (!width || !height || width > 8000 || height > 8000 || (channels != 1 && channels != 3)) break;
    SkBitmap bitmap;
    if (!bitmap.tryAllocN32Pixels(width, height, true)) break;
    std::vector<uint32_t> row(width);
    uint32_t y = 0;
    bool ok = true;
    auto pixel = [&](size_t offset) -> uint32_t {
      const uint8_t r = data[offset], g = channels == 3 ? data[offset + 1] : r, b = channels == 3 ? data[offset + 2] : r;
      return SkColorSetARGB(0xFF, r, g, b);
    };
    while (y < height && ok) {
      if (at >= data.size()) { ok = false; break; }
      const uint32_t repeat = data[at++] + 1u;
      uint32_t x = 0;
      while (x < width) {
        if (at >= data.size()) { ok = false; break; }
        const uint8_t code = data[at++];
        if (code <= 127) {
          const uint32_t count = std::min<uint32_t>(code + 1u, width - x);
          if (at + channels > data.size()) { ok = false; break; }
          std::fill_n(row.begin() + x, count, pixel(at));
          at += channels;
          x += count;
        } else {
          const uint32_t count = std::min<uint32_t>(257u - code, width - x);
          if (at + channels * count > data.size()) { ok = false; break; }
          for (uint32_t i = 0; i < count; ++i, at += channels) row[x + i] = pixel(at);
          x += count;
        }
      }
      for (uint32_t i = 0; ok && i < repeat && y < height; ++i, ++y)
        std::copy(row.begin(), row.end(), bitmap.getAddr32(0, y));
    }
    if (!ok) break;
    bitmap.setImmutable();
    pages.push_back(gfx::ImageSkia::CreateFrom1xBitmap(bitmap));
  }
  return pages;
}

// Asks where to save, then writes the PDF off the UI thread.
class PdfSaver final : public ui::SelectFileDialog::Listener {
 public:
  PdfSaver(content::WebContents* page, scoped_refptr<base::RefCountedMemory> pdf, base::OnceCallback<void(std::u16string)> done)
      : pdf_(std::move(pdf)), done_(std::move(done)) {
    auto* profile = Profile::FromBrowserContext(page->GetBrowserContext());
    std::u16string title = page->GetTitle();
    if (title.empty()) title = base::UTF8ToUTF16(page->GetLastCommittedURL().host());
    base::FilePath::StringType name = base::FilePath::FromUTF16Unsafe(title.empty() ? u"Zitter" : title).value();
    base::i18n::ReplaceIllegalCharactersInPath(&name, '_');
    const auto suggested = DownloadPrefs::FromBrowserContext(profile)->DownloadPath().Append(name).AddExtension(FILE_PATH_LITERAL("pdf"));
    dialog_ = ui::SelectFileDialog::Create(this, std::make_unique<ChromeSelectFilePolicy>(page));
    ui::SelectFileDialog::FileTypeInfo types;
    types.extensions = {{FILE_PATH_LITERAL("pdf")}};
    dialog_->SelectFile(ui::SelectFileDialog::SELECT_SAVEAS_FILE, T(u"Zapisz jako PDF"), suggested, &types, 1,
                        FILE_PATH_LITERAL("pdf"), page->GetTopLevelNativeWindow());
  }
  ~PdfSaver() override {
    if (dialog_) dialog_->ListenerDestroyed();
  }
  void FileSelected(const ui::SelectedFileInfo& file, int) override {
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
        base::BindOnce([](base::FilePath path, scoped_refptr<base::RefCountedMemory> pdf) {
          return base::WriteFile(path, base::span<const uint8_t>(*pdf));
        }, file.path(), pdf_),
        base::BindOnce(&PdfSaver::Finish, base::Unretained(this)));
  }
  void FileSelectionCanceled() override { std::move(done_).Run(u""); delete this; }

 private:
  void Finish(bool written) {
    std::move(done_).Run(written ? u"" : T(u"Nie udało się zapisać pliku PDF."));
    delete this;
  }
  scoped_refptr<base::RefCountedMemory> pdf_;
  base::OnceCallback<void(std::u16string)> done_;
  scoped_refptr<ui::SelectFileDialog> dialog_;
};
}  // namespace

PagePrinter::PagePrinter(content::WebContents* page) : content::WebContentsObserver(page) {}
PagePrinter::~PagePrinter() = default;
void PagePrinter::WebContentsDestroyed() { weak_factory_.InvalidateWeakPtrs(); converter_.reset(); }

// static
void PagePrinter::ListPrinters(base::OnceCallback<void(std::vector<PrinterChoice>)> done) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce([](std::string locale) {
        std::vector<PrinterChoice> printers;
        auto backend = printing::PrintBackend::CreateInstance(locale);
        if (!backend) return printers;
        printing::PrinterList list;
        if (backend->EnumeratePrinters(list) != printing::mojom::ResultCode::kSuccess) return printers;
        std::string default_name;
        backend->GetDefaultPrinterName(default_name);
        for (const auto& printer : list)
          printers.push_back({printer.printer_name, base::UTF8ToUTF16(printer.display_name.empty() ? printer.printer_name : printer.display_name)});
        std::stable_partition(printers.begin(), printers.end(), [&](const PrinterChoice& p) { return p.name == default_name; });
        return printers;
      }, g_browser_process->GetApplicationLocale()),
      std::move(done));
}

// static
std::optional<std::string> PagePrinter::PageRanges(const PrintOptions& options, int page_count) {
  switch (options.pages) {
    case PrintOptions::Pages::kAll: return std::string();
    case PrintOptions::Pages::kCurrent: return base::NumberToString(std::clamp(options.current, 0, std::max(0, page_count - 1)) + 1);
    case PrintOptions::Pages::kRange: break;
  }
  // „1–2, 5”: dashes of any kind, spaces ignored, every page within the document.
  std::u16string text = options.range;
  base::ReplaceChars(text, u"–—", u"-", &text);
  base::RemoveChars(text, u" ", &text);
  std::vector<std::string> parts;
  for (const auto& part : base::SplitString(base::UTF16ToUTF8(text), ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    const auto ends = base::SplitString(part, "-", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
    int first = 0, last = 0;
    if (ends.empty() || ends.size() > 2 || !base::StringToInt(ends.front(), &first) ||
        !base::StringToInt(ends.back(), &last) || first < 1 || last < first || (page_count > 0 && last > page_count))
      return std::nullopt;
    parts.push_back(first == last ? base::NumberToString(first) : base::NumberToString(first) + "-" + base::NumberToString(last));
  }
  if (parts.empty()) return std::nullopt;
  return base::JoinString(parts, ",");
}

void PagePrinter::MakePdf(const PrintOptions& options, std::string ranges,
                          base::OnceCallback<void(scoped_refptr<base::RefCountedMemory>)> done) {
  auto* manager = web_contents() ? printing::PrintViewManager::FromWebContents(web_contents()) : nullptr;
  if (!manager) { std::move(done).Run(nullptr); return; }
  const auto paper = PaperMicrons();
  // Chrome's defaults: 1 cm, or a quarter inch for „Minimalne”.
  const double margin = options.margins == PrintOptions::Margins::kNone ? 0 : options.margins == PrintOptions::Margins::kMinimal ? 0.25 : 1.0 / 2.54;
  auto params = print_to_pdf::GetPrintPagesParams(
      web_contents()->GetLastCommittedURL(), options.landscape, options.headers, options.backgrounds, 1.0,
      paper.width() / kMicronsPerInch, paper.height() / kMicronsPerInch, margin, margin, margin, margin,
      std::nullopt, std::nullopt, false, std::nullopt, std::nullopt);
  if (!std::holds_alternative<printing::mojom::PrintPagesParamsPtr>(params)) { std::move(done).Run(nullptr); return; }
  manager->PrintToPdf(web_contents()->GetPrimaryMainFrame(), ranges,
                      std::move(std::get<printing::mojom::PrintPagesParamsPtr>(params)),
                      base::BindOnce([](base::OnceCallback<void(scoped_refptr<base::RefCountedMemory>)> done,
                                        print_to_pdf::PdfPrintResult result, scoped_refptr<base::RefCountedMemory> pdf) {
                        std::move(done).Run(result == print_to_pdf::PdfPrintResult::kPrintSuccess ? pdf : nullptr);
                      }, std::move(done)));
}

void PagePrinter::Render(const PrintOptions& options, base::OnceCallback<void(Preview)> done) {
  const int generation = ++generation_;
  const bool color = options.color || options.printer.empty();
  const bool landscape = options.landscape;
  MakePdf(options, std::string(), base::BindOnce([](base::WeakPtr<PagePrinter> self, int generation, bool color, bool landscape,
      base::OnceCallback<void(Preview)> done, scoped_refptr<base::RefCountedMemory> pdf) {
    if (!self || generation != self->generation_) return;
    if (!pdf) { std::move(done).Run({.failed = true}); return; }
    const auto paper = PaperMicrons();
    gfx::Size area(paper.width() * kPreviewDpi / kMicronsPerInch, paper.height() * kPreviewDpi / kMicronsPerInch);
    if (landscape) area.Transpose();
    self->converter_ = printing::PwgRasterConverter::CreateDefault();
    printing::PwgRasterSettings bitmap{printing::mojom::DuplexMode::kSimplex, printing::TRANSFORM_NORMAL, false, false, color};
    self->converter_->Start(std::nullopt, pdf.get(),
        printing::PdfRenderSettings(gfx::Rect(area), gfx::Point(), gfx::Size(kPreviewDpi, kPreviewDpi), true, color,
                                    printing::PdfRenderSettings::Mode::NORMAL),
        bitmap, base::BindOnce([](base::WeakPtr<PagePrinter> self, int generation, scoped_refptr<base::RefCountedMemory>,
                                  base::OnceCallback<void(Preview)> done, base::ReadOnlySharedMemoryRegion region) {
          if (!self || generation != self->generation_) return;
          Preview preview;
          if (auto mapping = region.Map(); mapping.IsValid()) preview.pages = DecodePwg(mapping.GetMemoryAsSpan<uint8_t>());
          preview.failed = preview.pages.empty();
          self->page_count_ = preview.pages.size();
          std::move(done).Run(std::move(preview));
        }, self, generation, pdf, std::move(done)));
  }, weak_factory_.GetWeakPtr(), generation, color, landscape, std::move(done)));
}

void PagePrinter::Print(const PrintOptions& options, base::OnceCallback<void(std::u16string)> done) {
  const auto ranges = PageRanges(options, page_count_);
  if (!ranges) { std::move(done).Run(T(u"Podaj strony jako 1-3, 5.")); return; }
  // Selected pages, for the print job's page count.
  int pages = page_count_;
  if (!ranges->empty()) {
    pages = 0;
    for (const auto& part : base::SplitString(*ranges, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
      const auto ends = base::SplitString(part, "-", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
      int first = 0, last = 0;
      base::StringToInt(ends.front(), &first);
      base::StringToInt(ends.back(), &last);
      pages += last - first + 1;
    }
  }
  MakePdf(options, *ranges, base::BindOnce([](base::WeakPtr<PagePrinter> self, PrintOptions options, int pages,
      base::OnceCallback<void(std::u16string)> done, scoped_refptr<base::RefCountedMemory> pdf) {
    if (!self || !self->web_contents()) return;
    if (!pdf) { std::move(done).Run(T(u"Nie udało się przygotować stron do druku.")); return; }
    if (options.printer.empty()) { new PdfSaver(self->web_contents(), std::move(pdf), std::move(done)); return; }
    auto* manager = printing::PrintViewManager::FromWebContents(self->web_contents());
    if (!manager) { std::move(done).Run(T(u"Drukowanie jest niedostępne na tej stronie.")); return; }
    // The ticket Print Preview sends with its PDF (print_preview_handler).
    const auto paper = PaperMicrons();
    base::DictValue media;
    media.Set(printing::kSettingMediaSizeIsDefault, true);
    media.Set(printing::kSettingMediaSizeWidthMicrons, paper.width());
    media.Set(printing::kSettingMediaSizeHeightMicrons, paper.height());
    base::DictValue ticket;
    ticket.Set(printing::kSettingMediaSize, std::move(media));
    ticket.Set(printing::kSettingPreviewPageCount, std::max(1, pages));
    ticket.Set(printing::kSettingLandscape, options.landscape);
    ticket.Set(printing::kSettingColor, static_cast<int>(options.color ? printing::mojom::ColorModel::kColor : printing::mojom::ColorModel::kGray));
    ticket.Set(printing::kSettingHeaderFooterEnabled, false);
    ticket.Set(printing::kSettingMarginsType, static_cast<int>(printing::mojom::MarginType::kDefaultMargins));
    ticket.Set(printing::kSettingDuplexMode, static_cast<int>(printing::mojom::DuplexMode::kSimplex));
    ticket.Set(printing::kSettingCopies, std::clamp(options.copies, 1, 999));
    ticket.Set(printing::kSettingCollate, true);
    ticket.Set(printing::kSettingShouldPrintBackgrounds, options.backgrounds);
    ticket.Set(printing::kSettingShouldPrintSelectionOnly, false);
    ticket.Set(printing::kSettingPreviewModifiable, true);
    ticket.Set(printing::kSettingPrinterType, static_cast<int>(printing::mojom::PrinterType::kLocal));
    ticket.Set(printing::kSettingRasterizePdf, false);
    ticket.Set(printing::kSettingScaleFactor, 100);
    ticket.Set(printing::kSettingScalingType, printing::DEFAULT);
    ticket.Set(printing::kSettingPagesPerSheet, 1);
    ticket.Set(printing::kSettingDpiHorizontal, printing::kDefaultPdfDpi);
    ticket.Set(printing::kSettingDpiVertical, printing::kDefaultPdfDpi);
    ticket.Set(printing::kSettingDeviceName, options.printer);
    ticket.Set(printing::kSettingPageWidth, paper.width());
    ticket.Set(printing::kSettingPageHeight, paper.height());
    ticket.Set(printing::kSettingShowSystemDialog, false);
    manager->PrintForPrintPreview(std::move(ticket), std::move(pdf), self->web_contents()->GetPrimaryMainFrame(),
        base::BindOnce([](base::OnceCallback<void(std::u16string)> done, const base::Value& error) {
          std::move(done).Run(error.is_string() ? T(u"Drukarka odrzuciła zadanie.") : u"");
        }, std::move(done)));
  }, weak_factory_.GetWeakPtr(), options, pages, std::move(done)));
}

void PagePrinter::PrintWithSystemDialog() {
  if (web_contents()) printing::StartBasicPrint(web_contents());
}

}  // namespace zitter::prototype
