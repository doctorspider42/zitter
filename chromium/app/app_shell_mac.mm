#include "chromium/app/app_shell_mac.h"

#import <AppKit/AppKit.h>
#import <objc/runtime.h>

#include <vector>

#include "base/check.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "chrome/browser/download/download_core_service.h"
#include "base/no_destructor.h"
#include "base/strings/sys_string_conversions.h"
#include "chromium/prototype/i18n.h"
#include "url/gurl.h"

namespace {
zitter::AppShellDelegate* g_delegate = nullptr;

std::vector<GURL>& PendingUrls() {
  static base::NoDestructor<std::vector<GURL>> urls;
  return *urls;
}

void OpenOrQueue(NSURL* ns_url) {
  GURL url(base::SysNSStringToUTF8(ns_url.absoluteString));
  if (!url.SchemeIsHTTPOrHTTPS()) {
    LOG(WARNING) << "Zitter opens only HTTP(S) links from other apps";
    return;
  }
  if (g_delegate) {
    g_delegate->OpenUrl(url);
  } else {
    PendingUrls().push_back(std::move(url));
  }
}

// Swaps an AppController method for one that never reaches Chrome's
// Browser-window code. AppController stays NSApp.delegate: Chrome CHECKs it.
void Replace(Class cls, SEL selector, id block) {
  Method method = class_getInstanceMethod(cls, selector);
  CHECK(method) << "AppController lost " << sel_getName(selector)
                << "; review Zitter's app shell after the Chromium update";
  method_setImplementation(method, imp_implementationWithBlock(block));
}
}  // namespace

@interface ZitterMenuTarget : NSObject <NSMenuItemValidation>
@end

@implementation ZitterMenuTarget
- (void)runCommand:(NSMenuItem*)item {
  if (g_delegate) g_delegate->RunCommand(static_cast<zitter::AppCommand>(item.tag));
}
- (BOOL)validateMenuItem:(NSMenuItem*)item {
  return g_delegate != nullptr;
}
@end

namespace {
ZitterMenuTarget* MenuTarget() {
  static ZitterMenuTarget* target = [[ZitterMenuTarget alloc] init];
  return target;
}

NSString* Text(std::u16string_view polish) { return base::SysUTF16ToNSString(zitter::prototype::T(polish)); }

NSMenuItem* Item(NSString* title, SEL action, NSString* key,
                 NSEventModifierFlags modifiers = NSEventModifierFlagCommand) {
  NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title action:action keyEquivalent:key];
  item.keyEquivalentModifierMask = modifiers;
  return item;
}

NSMenuItem* Command(NSString* title, zitter::AppCommand command, NSString* key,
                    NSEventModifierFlags modifiers = NSEventModifierFlagCommand) {
  NSMenuItem* item = Item(title, @selector(runCommand:), key, modifiers);
  item.target = MenuTarget();
  item.tag = static_cast<NSInteger>(command);
  return item;
}

NSMenuItem* Submenu(NSMenu* bar, NSString* title, NSArray<NSMenuItem*>* items) {
  NSMenu* menu = [[NSMenu alloc] initWithTitle:title];
  for (NSMenuItem* item in items) [menu addItem:item];
  NSMenuItem* holder = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
  holder.submenu = menu;
  [bar addItem:holder];
  return holder;
}

using zitter::AppCommand;
constexpr NSEventModifierFlags kCmd = NSEventModifierFlagCommand;
constexpr NSEventModifierFlags kCmdShift = NSEventModifierFlagCommand | NSEventModifierFlagShift;
constexpr NSEventModifierFlags kCmdOption = NSEventModifierFlagCommand | NSEventModifierFlagOption;

// Only Zitter commands and standard AppKit responder actions: nothing here can
// reach Chrome's command dispatch (New Window, Incognito, History…).
NSMenu* BuildMainMenu() {
  // The menu speaks the UI language, decided before any window exists.
  zitter::prototype::ApplyUiLanguage("");
  NSMenu* bar = [[NSMenu alloc] initWithTitle:@""];
  Submenu(bar, @"Zitter", @[
    Item(Text(u"O Zitter"), @selector(orderFrontStandardAboutPanel:), @""),
    NSMenuItem.separatorItem,
    Command(@"Time Machine…", AppCommand::kTimeline, @"y", kCmdShift),
    Command(Text(u"Historia…"), AppCommand::kHistory, @"y", kCmd),
    Command(Text(u"Pobrania…"), AppCommand::kDownloads, @"j", kCmd),
    Command(Text(u"Ustawienia…"), AppCommand::kSettings, @",", kCmd),
    NSMenuItem.separatorItem,
    Item(Text(u"Ukryj Zitter"), @selector(hide:), @"h"),
    Item(Text(u"Ukryj pozostałe"), @selector(hideOtherApplications:), @"h", kCmdOption),
    Item(Text(u"Pokaż wszystkie"), @selector(unhideAllApplications:), @"", 0),
    NSMenuItem.separatorItem,
    Item(Text(u"Zakończ Zitter"), @selector(terminate:), @"q"),
  ]);
  Submenu(bar, Text(u"Plik"), @[
    Command(Text(u"Nowa karta"), AppCommand::kNewTab, @"t", kCmd),
    Command(Text(u"Nowe okno prywatne"), AppCommand::kNewPrivateWindow, @"n", kCmdShift),
    Command(Text(u"Otwórz adres…"), AppCommand::kOpenAddress, @"l", kCmd),
    Command(@"Command Center…", AppCommand::kCommandPalette, @"k", kCmd),
    Command(Text(u"Importuj z przeglądarki…"), AppCommand::kImport, @"", 0),
    NSMenuItem.separatorItem,
    Command(Text(u"Drukuj…"), AppCommand::kPrint, @"p", kCmd),
    NSMenuItem.separatorItem,
    Command(Text(u"Zamknij kartę"), AppCommand::kCloseTab, @"w", kCmd),
  ]);
  Submenu(bar, Text(u"Edycja"), @[
    Item(Text(u"Cofnij"), @selector(undo:), @"z"),
    Item(Text(u"Ponów"), @selector(redo:), @"z", kCmdShift),
    NSMenuItem.separatorItem,
    Item(Text(u"Wytnij"), @selector(cut:), @"x"),
    Item(Text(u"Kopiuj"), @selector(copy:), @"c"),
    Item(Text(u"Wklej"), @selector(paste:), @"v"),
    Item(Text(u"Zaznacz wszystko"), @selector(selectAll:), @"a"),
    NSMenuItem.separatorItem,
    Command(Text(u"Znajdź na stronie…"), AppCommand::kFind, @"f", kCmd),
  ]);
  Submenu(bar, Text(u"Widok"), @[
    Command(Text(u"Pokaż lub ukryj karty"), AppCommand::kToggleSidebar, @"s", kCmd),
    Command(Text(u"Odśwież stronę"), AppCommand::kReload, @"r", kCmd),
  ]);
  Submenu(bar, Text(u"Narzędzia developerskie"), @[
    Command(Text(u"Otwórz DevTools"), AppCommand::kDevTools, @"i", kCmdOption),
    Command(Text(u"Otwórz konsolę"), AppCommand::kDevConsole, @"j", kCmdOption),
    Command(Text(u"Zbadaj element"), AppCommand::kDevInspect, @"c", kCmdOption),
    Command(Text(u"Odśwież bez pamięci podręcznej"), AppCommand::kHardReload, @"r", kCmdShift),
    NSMenuItem.separatorItem,
    Command(Text(u"Narzędzia developerskie…"), AppCommand::kDeveloper, @"", 0),
  ]);
  NSMenuItem* window = Submenu(bar, Text(u"Okno"), @[
    Item(Text(u"Minimalizuj"), @selector(performMiniaturize:), @"m"),
    Item(Text(u"Powiększ"), @selector(performZoom:), @"", 0),
    NSMenuItem.separatorItem,
    Command(Text(u"Pierwsza przestrzeń"), AppCommand::kWorkspaceWork, @"1", kCmdOption),
    Command(Text(u"Druga przestrzeń"), AppCommand::kWorkspacePersonal, @"2", kCmdOption),
  ]);
  NSApp.windowsMenu = window.submenu;
  return bar;
}

NSMenu* DockMenu() {
  static NSMenu* menu = [] {
    NSMenu* dock = [[NSMenu alloc] initWithTitle:@""];
    [dock addItem:Command(Text(u"Nowa karta"), AppCommand::kNewTab, @"", 0)];
    return dock;
  }();
  return menu;
}
}  // namespace

namespace zitter {

bool ConfirmDownloadShutdown() {
  const int count = DownloadCoreService::BlockingShutdownCountAllProfiles();
  if (!count) return true;
  id controller = NSApp.delegate;
  const SEL selector = @selector(userWillWaitForInProgressDownloads:);
  CHECK([controller respondsToSelector:selector]);
  auto ask = reinterpret_cast<BOOL (*)(id, SEL, int)>([controller methodForSelector:selector]);
  return !ask(controller, selector, count);
}

void InstallAppShell() {
  static bool installed = false;
  if (installed) return;
  installed = true;
  Class controller = NSClassFromString(@"AppController");
  CHECK(controller);
  Method quit_method = class_getInstanceMethod(controller, @selector(tryToTerminateApplication));
  CHECK(quit_method);
  auto original_quit = reinterpret_cast<void (*)(id, SEL)>(method_getImplementation(quit_method));
  Replace(controller, @selector(tryToTerminateApplication), ^(id self) {
    if (g_delegate) g_delegate->RequestQuit();
    else original_quit(self, @selector(tryToTerminateApplication));
  });
  Replace(controller, @selector(applicationShouldHandleReopen:hasVisibleWindows:),
          ^BOOL(id, NSApplication*, BOOL) {
            if (g_delegate) g_delegate->RaiseWindow();
            return NO;  // Chrome would create a new Browser window here.
          });
  // Chrome re-points its last-profile menu bridges here, building a bookmarks
  // menu from a main-menu item Zitter's menu does not have (CHECK failure on
  // profile deletion). Zitter never opens Browser windows, so AppController
  // never holds a last profile or bridges to update.
  Replace(controller, @selector(profileWasRemoved:forIncognito:),
          ^(id, const base::FilePath&, bool) {});
  Replace(controller, @selector(applicationDockMenu:), ^NSMenu*(id, NSApplication*) {
    return DockMenu();
  });
  Replace(controller, @selector(application:openURLs:),
          ^(id, NSApplication*, NSArray<NSURL*>* urls) {
            for (NSURL* url in urls) OpenOrQueue(url);
          });
  Replace(controller, @selector(application:continueUserActivity:restorationHandler:),
          ^BOOL(id, NSApplication*, NSUserActivity* activity, id) {
            if (!activity.webpageURL) return NO;
            OpenOrQueue(activity.webpageURL);
            return YES;
          });
}

void SetAppShellDelegate(AppShellDelegate* delegate) {
  g_delegate = delegate;
  if (!delegate) return;
  NSApp.mainMenu = BuildMainMenu();
  // Chrome's dock tile redraws NSImageNameApplicationIcon; pin it to the
  // bundle's own Zitter icon so the Dock never falls back to Chromium's.
  if (NSString* icon = [NSBundle.mainBundle pathForResource:@"app" ofType:@"icns"]) {
    if (NSImage* image = [[NSImage alloc] initWithContentsOfFile:icon]) NSApp.applicationIconImage = image;
  }
  auto pending = std::move(PendingUrls());
  PendingUrls().clear();
  for (const auto& url : pending) delegate->OpenUrl(url);
}

}  // namespace zitter
