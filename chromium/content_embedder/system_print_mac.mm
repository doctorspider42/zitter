// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "system_print_mac.h"

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

#include <cstdio>
#include <utility>

#include "base/apple/bridging.h"
#include "base/apple/scoped_cftyperef.h"
#include "base/files/file_path.h"
#include "base/files/important_file_writer.h"
#include "base/functional/bind.h"
#include "base/memory/ref_counted_memory.h"
#include "base/strings/string_view_util.h"
#include "base/strings/sys_string_conversions.h"
#include "base/task/thread_pool.h"

@interface ZitterPrintPanelCompletion : NSObject {
 @public
  base::OnceCallback<void(bool)> callback_;
}
- (void)printPanelDidEnd:(NSPrintPanel*)panel
              returnCode:(NSInteger)code
             contextInfo:(void*)context;
@end

@implementation ZitterPrintPanelCompletion
- (void)printPanelDidEnd:(NSPrintPanel*)panel
              returnCode:(NSInteger)code
             contextInfo:(void*)context {
  auto* completion = (__bridge_transfer ZitterPrintPanelCompletion*)context;
  std::move(completion->callback_).Run(code == NSModalResponseOK);
}
@end

namespace zitter::content_embedder {
namespace {

void SubmitPdf(NSPrintInfo* info,
               std::u16string title,
               bool file_only,
               scoped_refptr<base::RefCountedMemory> pdf,
               base::OnceCallback<void(bool)> done) {
  auto session = static_cast<PMPrintSession>(info.PMPrintSession);
  auto settings = static_cast<PMPrintSettings>(info.PMPrintSettings);
  auto format = static_cast<PMPageFormat>(info.PMPageFormat);
  PMDestinationType destination;
  if (PMSessionGetDestinationType(session, settings, &destination) != noErr) {
    std::move(done).Run(false);
    return;
  }

  if (destination == kPMDestinationFile) {
    base::apple::ScopedCFTypeRef<CFURLRef> url;
    if (PMSessionCopyDestinationLocation(session, settings,
                                        url.InitializeInto()) != noErr ||
        !url) {
      std::move(done).Run(false);
      return;
    }
    NSURL* destination_url = base::apple::CFToNSPtrCast(url.get());
    NSString* filename = destination_url.path;
    if (!destination_url.isFileURL || !filename.length) {
      std::move(done).Run(false);
      return;
    }
    base::FilePath path(filename.fileSystemRepresentation);
    if (!path.IsAbsolute() || path.ReferencesParent() || path.IsNetwork() ||
        !path.MatchesFinalExtension(FILE_PATH_LITERAL(".pdf"))) {
      std::move(done).Run(false);
      return;
    }
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskShutdownBehavior::BLOCK_SHUTDOWN},
        base::BindOnce([](base::FilePath path,
                          scoped_refptr<base::RefCountedMemory> pdf) {
          return base::ImportantFileWriter::WriteFileAtomically(
              path, base::as_string_view(base::span<const uint8_t>(*pdf)));
        }, std::move(path), std::move(pdf)), std::move(done));
    return;
  }

  PMPrinter printer = nullptr;
  if (file_only || destination != kPMDestinationPrinter ||
      PMSessionGetCurrentPrinter(session, &printer) != noErr || !printer) {
    std::move(done).Run(false);
    return;
  }
  const auto name = base::SysUTF16ToCFStringRef(title);
  if (PMPrintSettingsSetJobName(settings, name.get()) != noErr) {
    std::move(done).Run(false);
    return;
  }

  // Keep the native settings alive through the worker call. The worker only
  // hands the generated PDF bytes to the system spooler.
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
                  base::TaskShutdownBehavior::BLOCK_SHUTDOWN},
      base::BindOnce([](NSPrintInfo* info, PMPrinter printer,
                        PMPrintSettings settings, PMPageFormat format,
                        scoped_refptr<base::RefCountedMemory> bytes) {
        if (!info)
          return false;
        const auto data = base::span<const uint8_t>(*bytes);
        base::apple::ScopedCFTypeRef<CFDataRef> cf_data(
            CFDataCreateWithBytesNoCopy(kCFAllocatorDefault, data.data(),
                                        data.size(), kCFAllocatorNull));
        if (!cf_data)
          return false;
        base::apple::ScopedCFTypeRef<CGDataProviderRef> provider(
            CGDataProviderCreateWithCFData(cf_data.get()));
        return provider && PMPrinterPrintWithProvider(
                               printer, settings, format, CFSTR("application/pdf"),
                               provider.get()) == noErr;
      }, info, printer, settings, format, std::move(pdf)), std::move(done));
}

void CompletePrintPanel(
    NSPrintInfo* info,
    std::u16string title,
    bool file_only,
    base::OnceCallback<void(std::optional<SystemPrintSettings>)> done,
    bool accepted) {
  if (!accepted) {
    std::move(done).Run(std::nullopt);
    return;
  }
  auto format = static_cast<PMPageFormat>(info.PMPageFormat);
  PMRect paper;
  if (PMGetUnadjustedPaperRect(format, &paper) != noErr) {
    std::move(done).Run(std::nullopt);
    return;
  }
  SystemPrintSettings result;
  result.paper_points =
      gfx::SizeF(paper.right - paper.left, paper.bottom - paper.top);
  result.landscape = info.orientation == NSPaperOrientationLandscape;
  result.submit = base::BindOnce(&SubmitPdf, info, std::move(title), file_only);
  std::move(done).Run(std::move(result));
}

}  // namespace

SystemPrintSettings::SystemPrintSettings() = default;
SystemPrintSettings::SystemPrintSettings(SystemPrintSettings&&) = default;
SystemPrintSettings::~SystemPrintSettings() = default;

void ShowMacPrintPanel(
    gfx::NativeWindow owner,
    std::u16string title,
    bool cancel_probe,
    const base::FilePath& probe_file,
    base::OnceCallback<void(std::optional<SystemPrintSettings>)> done) {
  NSWindow* window = owner.GetNativeNSWindow();
  if (!window || window.attachedSheet) {
    std::move(done).Run(std::nullopt);
    return;
  }
  NSPrintInfo* info = [NSPrintInfo.sharedPrintInfo copy];
  if (!probe_file.empty()) {
    info.paperSize = NSMakeSize(420, 595);
    info.orientation = NSPaperOrientationLandscape;
    NSURL* url = [NSURL fileURLWithPath:
        base::SysUTF8ToNSString(probe_file.value())];
    const bool selected = PMSessionSetDestination(
        static_cast<PMPrintSession>(info.PMPrintSession),
        static_cast<PMPrintSettings>(info.PMPrintSettings), kPMDestinationFile,
        CFSTR("application/pdf"), base::apple::NSToCFPtrCast(url)) == noErr;
    CompletePrintPanel(info, std::move(title), true, std::move(done), selected);
    return;
  }

  NSPrintPanel* panel = [NSPrintPanel printPanel];
  panel.options = NSPrintPanelShowsCopies | NSPrintPanelShowsPaperSize |
                  NSPrintPanelShowsOrientation;
  auto* completion = [[ZitterPrintPanelCompletion alloc] init];
  completion->callback_ = base::BindOnce(&CompletePrintPanel, info,
                                        std::move(title), false, std::move(done));
  [panel beginSheetWithPrintInfo:info
                 modalForWindow:window
                       delegate:completion
                 didEndSelector:@selector(printPanelDidEnd:returnCode:contextInfo:)
                    contextInfo:(__bridge_retained void*)completion];
  if (cancel_probe) {
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 700 * NSEC_PER_MSEC),
                   dispatch_get_main_queue(), ^{
      if (NSWindow* sheet = window.attachedSheet) {
        std::puts("Zitter Content opened and cancelled the native print sheet");
        [window endSheet:sheet returnCode:NSModalResponseCancel];
      }
    });
  }
}

}  // namespace zitter::content_embedder
