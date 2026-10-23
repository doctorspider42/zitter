#include "chromium/prototype/workspace_keyboard_mac.h"

#include "base/apple/owned_objc.h"
#include "base/check.h"
#include <algorithm>
#include <map>
#include "base/strings/sys_string_conversions.h"
#include "base/strings/string_util.h"
#include "components/input/native_web_keyboard_event.h"
#include "components/remote_cocoa/app_shim/native_widget_ns_window_bridge.h"
#include "components/remote_cocoa/common/native_widget_ns_window_host.mojom.h"
#import "ui/base/cocoa/default_command_dispatcher_delegate.h"
#include "ui/content_accelerators/accelerator_util.h"
#import "ui/views/cocoa/native_widget_mac_ns_window_host.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/base/ime/input_method.h"

@interface ZitterCommandDispatcherDelegate : DefaultCommandDispatcherDelegate {
  id _monitor;
}
- (instancetype)initWithWindow:(NSWindow*)window panel:(BOOL)panel widget:(views::Widget*)widget;
@end

@implementation ZitterCommandDispatcherDelegate
- (instancetype)initWithWindow:(NSWindow*)window panel:(BOOL)panel widget:(views::Widget*)widget {
  if ((self = [super init])) {
    __weak NSWindow* weak_window = window;
    const auto weak_widget=widget->GetWeakPtr();
    _monitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown | NSEventMaskKeyUp
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
        // Standalone workspace and panel windows need NSWindow's responder
        // path for plain text and matching keyups. Panel command equivalents
        // still need explicit dispatch; main-window menu dispatch stays in AppKit.
        if (event.type == NSEventTypeKeyDown &&
            (event.modifierFlags & NSEventModifierFlagCommand)) {
          if (panel && [target performKeyEquivalent:event]) return nil;
          return event;  // Preserve application menu commands.
        }
        // Textless dialog keys need their registered native accelerators.
        // In particular, macOS registers Escape but deliberately does not make
        // Cancel a Return default. Never bypass an active text client's IME
        // or key policy, and never invent a default action for an unbound key.
        if(panel&&event.type==NSEventTypeKeyDown&&weak_widget){
          auto* delegate=weak_widget->widget_delegate();
          if(delegate&&delegate->AsDialogDelegate()){
            auto* input=weak_widget->GetInputMethod();
            auto* focus=weak_widget->GetFocusManager();
            if(focus&&(!input||!input->GetTextInputClient())){
              input::NativeWebKeyboardEvent keyboard_event((base::apple::OwnedNSEvent(event)));
              const auto accelerator=ui::GetAcceleratorFromNativeWebKeyboardEvent(keyboard_event);
              if((accelerator.key_code()==ui::VKEY_RETURN||accelerator.key_code()==ui::VKEY_ESCAPE)&&
                  focus->ProcessAccelerator(accelerator))return nil;
            }
          }
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
void UpdateWorkspaceMenuShortcuts(const std::vector<Shortcut>& shortcuts) {
  const std::map<std::string, std::string> commands = {
      {"Nowa karta", "new_tab"}, {"Otwórz adres…", "address"},
      {"Command Center…", "palette"}, {"Historia…", "history"}, {"Time Machine…", "timeline"}, {"Pobrania…", "downloads"}, {"Zamknij kartę", "close_tab"},
      {"Pokaż lub ukryj karty", "sidebar"}, {"Odśwież stronę", "reload"},
      {"Ustawienia…", "settings"}, {"Pierwsza przestrzeń", "space1"}, {"Druga przestrzeń", "space2"}};
  auto update = [&](auto&& self, NSMenu* menu) -> void {
    for (NSMenuItem* item in menu.itemArray) {
      if (item.submenu) self(self, item.submenu);
      const auto title = base::SysNSStringToUTF8(item.title);
      auto command = commands.find(title); if (command == commands.end()) continue;
      auto shortcut = std::find_if(shortcuts.begin(), shortcuts.end(), [&](const auto& binding) { return binding.id == command->second; });
      if (shortcut == shortcuts.end()) continue;
      std::string chord = FormatShortcut(shortcut->accelerator);
      const auto separator = chord.rfind('+');
      std::string key = separator == std::string::npos ? "" : chord.substr(separator + 1);
      NSString* equivalent = base::SysUTF8ToNSString(base::ToLowerASCII(key));
      if (key == "Left") equivalent = [NSString stringWithFormat:@"%C", static_cast<unichar>(NSLeftArrowFunctionKey)];
      if (key == "Right") equivalent = [NSString stringWithFormat:@"%C", static_cast<unichar>(NSRightArrowFunctionKey)];
      if (key == "Up") equivalent = [NSString stringWithFormat:@"%C", static_cast<unichar>(NSUpArrowFunctionKey)];
      if (key == "Down") equivalent = [NSString stringWithFormat:@"%C", static_cast<unichar>(NSDownArrowFunctionKey)];
      if (key == "Tab") equivalent = @"\t";
      if (key == "Space") equivalent = @" ";
      item.keyEquivalent = equivalent;
      NSEventModifierFlags flags = 0;
      const int mods = shortcut->accelerator.modifiers();
      if (mods & ui::EF_COMMAND_DOWN) flags |= NSEventModifierFlagCommand;
      if (mods & ui::EF_CONTROL_DOWN) flags |= NSEventModifierFlagControl;
      if (mods & ui::EF_ALT_DOWN) flags |= NSEventModifierFlagOption;
      if (mods & ui::EF_SHIFT_DOWN) flags |= NSEventModifierFlagShift;
      item.keyEquivalentModifierMask = flags;
    }
  };
  update(update, NSApp.mainMenu);
}

void InstallWorkspaceKeyboardRouting(views::Widget* widget, bool panel) {
  auto* host = views::NativeWidgetMacNSWindowHost::GetFromNativeWindow(
      widget->GetNativeWindow());
  CHECK(host);
  auto* bridge = host->GetInProcessNSWindowBridge();
  CHECK(bridge);  // This probe owns an in-process native window.
  bridge->SetCommandDispatcher([[ZitterCommandDispatcherDelegate alloc] initWithWindow:widget->GetNativeWindow().GetNativeNSWindow() panel:panel widget:widget], nil);

}
}
