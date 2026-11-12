// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#import <AppKit/AppKit.h>
#import <objc/runtime.h>

// Only the explicit Content clipboard probe calls this function. The
// replacement stays inside the disposable browser process.
extern "C" bool ZitterInstallClipboardFixture() {
  NSPasteboard* board = [NSPasteboard pasteboardWithUniqueName];
  if (!board || [board.name isEqualToString:NSPasteboardNameGeneral])
    return false;
  [board clearContents];
  if (![board setString:@"zitter-private-pasteboard-fixture"
                forType:NSPasteboardTypeString]) {
    return false;
  }

  SEL selector = @selector(generalPasteboard);
  Method method = class_getClassMethod([NSPasteboard class], selector);
  if (!method)
    return false;
  IMP replacement = imp_implementationWithBlock(^NSPasteboard*(id receiver) {
    return board;
  });
  if (!replacement)
    return false;
  method_setImplementation(method, replacement);
  return [NSPasteboard generalPasteboard] == board;
}

extern "C" bool ZitterClipboardFixtureMatches(const char* text) {
  NSPasteboard* board = [NSPasteboard generalPasteboard];
  return ![board.name isEqualToString:NSPasteboardNameGeneral] &&
      [[board stringForType:NSPasteboardTypeString]
          isEqualToString:[NSString stringWithUTF8String:text]];
}
