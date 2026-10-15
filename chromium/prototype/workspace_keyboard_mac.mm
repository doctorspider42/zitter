#include "chromium/prototype/workspace_keyboard_mac.h"

#include "base/apple/owned_objc.h"
#include "base/check.h"
#include "components/input/native_web_keyboard_event.h"
#include "components/remote_cocoa/app_shim/native_widget_ns_window_bridge.h"
#include "components/remote_cocoa/common/native_widget_ns_window_host.mojom.h"
#import "ui/base/cocoa/default_command_dispatcher_delegate.h"
#include "ui/content_accelerators/accelerator_util.h"
#import "ui/views/cocoa/native_widget_mac_ns_window_host.h"
#include "ui/views/widget/widget.h"

@interface ZitterCommandDispatcherDelegate : DefaultCommandDispatcherDelegate {
  id _monitor;
}
- (instancetype)initWithWindow:(NSWindow*)window panel:(BOOL)panel;
@end

@implementation ZitterCommandDispatcherDelegate
- (instancetype)initWithWindow:(NSWindow*)window panel:(BOOL)panel {
  if ((self = [super init])) {
    __weak NSWindow* weak_window = window;
    if (panel) _monitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown | NSEventMaskKeyUp
        handler:^NSEvent*(NSEvent* event) {
      NSWindow* target = weak_window;
      if (target && target.isKeyWindow) {
        // Focus may have moved since an event was queued against the main window.
        // Retarget to the current key panel, as AppKit does for physical input.
        if (event.window != target) {
          event = [NSEvent keyEventWithType:event.type location:NSZeroPoint
              modifierFlags:event.modifierFlags timestamp:event.timestamp
              windowNumber:target.windowNumber context:nil
              characters:event.characters
              charactersIgnoringModifiers:event.charactersIgnoringModifiers
              isARepeat:event.isARepeat keyCode:event.keyCode];
        }
        // AppKit's application dispatch can swallow keydowns for these standalone
        // Views panels. Keep text/IME dispatch on NSWindow's normal responder path.
        if (event.type == NSEventTypeKeyDown &&
            (event.modifierFlags & NSEventModifierFlagCommand)) {
          if ([target performKeyEquivalent:event]) return nil;
          return event;  // Preserve application menu commands.
        }
        [target sendEvent:event];
        return nil;
      }
      return event;
    }];
  }
  return self;
}
- (void)dealloc {
  if (_monitor) [NSEvent removeMonitor:_monitor];
}
- (ui::PerformKeyEquivalentResult)prePerformKeyEquivalent:(NSEvent*)event
                                                   window:(NSWindow*)window {
  NSResponder* responder = window.firstResponder;
  if ([responder respondsToSelector:@selector(isKeyLocked:)] &&
      [(id)responder isKeyLocked:event]) {
    return [super prePerformKeyEquivalent:event window:window];
  }
  input::NativeWebKeyboardEvent keyboard_event(
      (base::apple::OwnedNSEvent(event)));
  const auto accelerator = ui::GetAcceleratorFromNativeWebKeyboardEvent(keyboard_event);
  auto* bridge = remote_cocoa::NativeWidgetNSWindowBridge::GetFromNSWindow(window);
  bool handled = false;
  if (bridge) {
    bridge->host()->HandleAccelerator(
        accelerator,
        /*require_priority_handler=*/true, &handled);
  }
  return handled ? ui::PerformKeyEquivalentResult::kHandled
                 : [super prePerformKeyEquivalent:event window:window];
}
@end

namespace zitter::prototype {
void InstallWorkspaceKeyboardRouting(views::Widget* widget, bool panel) {
  auto* host = views::NativeWidgetMacNSWindowHost::GetFromNativeWindow(
      widget->GetNativeWindow());
  CHECK(host);
  auto* bridge = host->GetInProcessNSWindowBridge();
  CHECK(bridge);  // This probe owns an in-process native window.
  bridge->SetCommandDispatcher([[ZitterCommandDispatcherDelegate alloc] initWithWindow:widget->GetNativeWindow().GetNativeNSWindow() panel:panel], nil);

}
}
