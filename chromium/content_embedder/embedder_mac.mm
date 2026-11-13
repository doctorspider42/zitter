// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// The AppKit event bridge follows Chromium's BSD-licensed
// content/shell/browser/shell_application_mac.mm (Copyright 2013 The Chromium Authors).

#import <Cocoa/Cocoa.h>
#import <Security/Security.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <dlfcn.h>
#include <sys/stat.h>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/apple/bundle_locations.h"
#include "base/apple/foundation_util.h"
#include "base/apple/scoped_cftyperef.h"
#include "base/auto_reset.h"
#include "base/base_paths.h"
#include "base/base_paths_mac.h"
#include "base/environment.h"
#include "base/barrier_closure.h"
#include "base/check.h"
#include "base/command_line.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "base/mac/scoped_sending_event.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/message_loop/message_pump_apple.h"
#include "base/observer_list.h"
#include "base/path_service.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/process/process_handle.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/strings/sys_string_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread_restrictions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chromium/app/mac_release_metadata.h"
#include "chromium/app/network_policy.h"
#include "chromium/app/updater_mac.h"
#include "chromium/prototype/devtools_console_reader.h"
#include "chromium/prototype/fingerprint_session.h"
#include "chromium/prototype/i18n.h"
#include "chromium/prototype/workspace_shell.h"
#include "chromium/prototype/workspace_urls.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/context_menu_params.h"
#include "content/public/browser/weak_document_ptr.h"
#include "components/os_crypt/common/os_crypt_switches.h"
#include "components/password_manager/core/browser/password_store/password_store_consumer.h"
#include "components/password_manager/core/browser/password_store/password_store_interface.h"
#include "base/memory/raw_ref.h"
#include "content/public/app/content_main.h"
#include "content/public/app/content_main_delegate.h"
#include "content/public/browser/browser_main_parts.h"
#include "content/public/browser/clipboard_types.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/file_select_listener.h"
#include "content/public/browser/native_event_processor_mac.h"
#include "content/public/browser/native_event_processor_observer_mac.h"
#include "components/safe_browsing/core/common/safebrowsing_switches.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "net/base/net_errors.h"
#include "net/base/apple/url_conversions.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/common/content_paths.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/isolated_world_ids.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "ui/base/clipboard/clipboard_metadata.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/display/screen.h"
#include "ui/gfx/native_ui_types.h"
#include "url/gurl.h"
#include "url/origin.h"
#include "lab_clients.h"
#include "pdf/buildflags.h"
#include "printing/buildflags/buildflags.h"
#include "page_file_chooser.h"
#include "cookie_encryption_mac.h"
#include "download_probe.h"
#include "extension_archive_service.h"
#include "extension_manifest.h"
#include "extension_registry_probe.h"
#include "extensions/browser/extension_file_task_runner.h"
#include "profile_context.h"
#include "profile_permission_controller.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "workspace_lab.h"
#include "workspace_media_access.h"
#include "workspace_session_store.h"
#include "workspace_timeline.h"

using WorkspaceMenuAction = base::RepeatingCallback<bool(
    gfx::NativeWindow key, gfx::NativeWindow main, std::string command,
    bool validate)>;

@interface ZitterContentApplication
    : NSApplication <CrAppProtocol, CrAppControlProtocol, NativeEventProcessor,
                     NSMenuItemValidation, NSApplicationDelegate>
- (void)setWorkspaceQuitClosure:(base::RepeatingClosure)quit;
- (void)setExternalUrlHandler:(base::RepeatingCallback<void(GURL)>)handler;
- (void)installExternalUrlEvents;
- (void)setWorkspaceMenuAction:(WorkspaceMenuAction)action;
- (bool)activateWorkspaceMenuForTesting:(const std::string&)command;
@end

@implementation ZitterContentApplication {
  base::ObserverList<content::NativeEventProcessorObserver>::Unchecked
      _observers;
  BOOL _handlingSendEvent;
  base::RepeatingClosure _workspaceQuit;
  WorkspaceMenuAction _workspaceMenuAction;
  base::RepeatingCallback<void(GURL)> _externalUrlHandler;
  std::vector<GURL> _pendingExternalUrls;
  NSMenu* _previousMainMenu;
  NSMenu* _previousWindowsMenu;
  NSMenu* _workspaceDockMenu;
}
- (void)setWorkspaceQuitClosure:(base::RepeatingClosure)quit {
  _workspaceQuit = std::move(quit);
}
- (void)installExternalUrlEvents {
  [[NSAppleEventManager sharedAppleEventManager]
      setEventHandler:self
          andSelector:@selector(handleExternalUrlEvent:reply:)
        forEventClass:kInternetEventClass
           andEventID:kAEGetURL];
}
- (void)applicationWillFinishLaunching:(NSNotification*)notification {
  [self installExternalUrlEvents];
}
- (void)finishLaunching {
  [super finishLaunching];
  NSURL* iconURL = [NSBundle.mainBundle URLForResource:@"Zitter" withExtension:@"icns"];
  if (iconURL) {
    NSImage* icon = [[NSImage alloc] initWithContentsOfURL:iconURL];
    if (icon)
      self.applicationIconImage = icon;
  }
  NSBundle* bundle = NSBundle.mainBundle;
  if ([bundle.bundleIdentifier isEqualToString:@"dev.zitter.browser"] &&
      [bundle.bundleURL.URLByDeletingLastPathComponent.path isEqualToString:@"/Applications"]) {
    // The persistent Dock tile resolves the installed bundle through Launch
    // Services after this process and its applicationIconImage are gone.
    LSRegisterURL((__bridge CFURLRef)bundle.bundleURL, true);
    [NSWorkspace.sharedWorkspace noteFileSystemChanged:bundle.bundlePath];
  }
  if (self.delegate == self)
    [self installExternalUrlEvents];
}
- (void)handleExternalUrlEvent:(NSAppleEventDescriptor*)event
                         reply:(NSAppleEventDescriptor*)reply {
  NSString* spec = [event paramDescriptorForKeyword:keyDirectObject].stringValue;
  NSURL* url = spec ? [NSURL URLWithString:spec] : nil;
  if (url)
    [self application:self openURLs:@[ url ]];
}
- (void)setExternalUrlHandler:(base::RepeatingCallback<void(GURL)>)handler {
  _externalUrlHandler = std::move(handler);
  if (_externalUrlHandler)
    [self installExternalUrlEvents];
  for (auto& url : std::exchange(_pendingExternalUrls, {})) {
    if (_externalUrlHandler)
      _externalUrlHandler.Run(std::move(url));
  }
}
- (void)application:(NSApplication*)application openURLs:(NSArray<NSURL*>*)urls {
  for (NSURL* value in urls) {
    GURL url(base::SysNSStringToUTF8(value.absoluteString));
    if (!zitter::prototype::IsExternalOpenUrl(url)) {
      continue;
    }
    if (_externalUrlHandler) {
      _externalUrlHandler.Run(std::move(url));
    } else if (_pendingExternalUrls.size() < 32) {
      _pendingExternalUrls.push_back(std::move(url));
    }
  }
}
- (void)setWorkspaceMenuAction:(WorkspaceMenuAction)action {
  const bool installed = !!_workspaceMenuAction;
  _workspaceMenuAction = std::move(action);
  if (!_workspaceMenuAction) {
    if (installed) {
      self.mainMenu = _previousMainMenu;
      self.windowsMenu = _previousWindowsMenu;
      if (self.delegate == self)
        self.delegate = nil;
      _workspaceDockMenu = nil;
      _previousMainMenu = nil;
      _previousWindowsMenu = nil;
    }
    return;
  }
  if (installed)
    return;

  using zitter::prototype::T;
  NSMenu* bar = [[NSMenu alloc] initWithTitle:@""];
  auto submenu = [&](std::u16string_view title) {
    NSString* text = base::SysUTF16ToNSString(T(title));
    NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:text
                                                action:nil
                                         keyEquivalent:@""];
    item.submenu = [[NSMenu alloc] initWithTitle:text];
    [bar addItem:item];
    return item.submenu;
  };
  auto command = [&](NSMenu* menu, std::u16string_view title, NSString* id) {
    // Workspace Views owns configurable shortcuts.
    NSMenuItem* item = [menu addItemWithTitle:base::SysUTF16ToNSString(T(title))
                                      action:@selector(runWorkspaceMenuItem:)
                               keyEquivalent:@""];
    item.target = self;
    item.representedObject = id;
  };
  auto native = [](NSMenu* menu, std::u16string_view title, SEL action,
                   NSString* key, NSEventModifierFlags modifiers) {
    NSMenuItem* item = [menu addItemWithTitle:base::SysUTF16ToNSString(T(title))
                                      action:action
                               keyEquivalent:key];
    item.keyEquivalentModifierMask = modifiers;
  };

  constexpr auto kCommand = NSEventModifierFlagCommand;
  NSMenu* application = submenu(u"Zitter");
  command(application, u"O Zitter", @"about");
  [application addItem:NSMenuItem.separatorItem];
  command(application, u"Time Machine…", @"timeline");
  command(application, u"Historia…", @"history");
  command(application, u"Pobrania…", @"downloads");
  command(application, u"Czytnik RSS…", @"rss");
  command(application, u"Ustawienia…", @"settings");
  command(application, u"Night Shift…", @"night_shift_settings");
  [application addItem:NSMenuItem.separatorItem];
  native(application, u"Ukryj Zitter", @selector(hide:), @"h", kCommand);
  native(application, u"Ukryj pozostałe", @selector(hideOtherApplications:),
         @"h", kCommand | NSEventModifierFlagOption);
  native(application, u"Pokaż wszystkie", @selector(unhideAllApplications:),
         @"", 0);
  [application addItem:NSMenuItem.separatorItem];
  native(application, u"Zakończ Zitter", @selector(terminate:), @"q", kCommand);

  NSMenu* file = submenu(u"Plik");
  command(file, u"Nowa karta", @"new_tab");
  command(file, u"Duplikuj kartę", @"duplicate_tab");
  command(file, u"Nowe okno prywatne", @"private_window");
  command(file, u"Otwórz adres…", @"address");
  command(file, u"Command Center…", @"palette");
  command(file, u"Importuj z przeglądarki…", @"import");
#if BUILDFLAG(ENABLE_PRINTING)
#if BUILDFLAG(ENABLE_PDF)
  command(file, u"Drukuj…", @"print");
#endif
  command(file, u"Drukuj w oknie systemowym", @"print_system");
  command(file, u"Zapisz jako PDF", @"save_pdf");
#endif
  [file addItem:NSMenuItem.separatorItem];
  command(file, u"Zamknij kartę", @"close_tab");
  command(file, u"Przywróć zamkniętą kartę", @"reopen");

  NSMenu* edit = submenu(u"Edycja");
  native(edit, u"Cofnij", @selector(undo:), @"z", kCommand);
  native(edit, u"Ponów", @selector(redo:), @"z",
         kCommand | NSEventModifierFlagShift);
  [edit addItem:NSMenuItem.separatorItem];
  native(edit, u"Wytnij", @selector(cut:), @"x", kCommand);
  native(edit, u"Kopiuj", @selector(copy:), @"c", kCommand);
  native(edit, u"Wklej", @selector(paste:), @"v", kCommand);
  native(edit, u"Zaznacz wszystko", @selector(selectAll:), @"a", kCommand);
  [edit addItem:NSMenuItem.separatorItem];
  command(edit, u"Kopiuj adres strony", @"copy_url");
  command(edit, u"Znajdź na stronie…", @"find");

  NSMenu* view = submenu(u"Widok");
  command(view, u"Pokaż lub ukryj karty", @"sidebar");
  [view addItem:NSMenuItem.separatorItem];
  command(view, u"Wstecz", @"back");
  command(view, u"Dalej", @"forward");
  command(view, u"Odśwież stronę", @"reload");
  command(view, u"Przełącz pełny ekran", @"toggle_window_fullscreen");

  NSMenu* developer = submenu(u"Narzędzia developerskie");
  command(developer, u"Otwórz DevTools", @"devtools_panel");
  command(developer, u"Otwórz konsolę", @"devtools_console");
  command(developer, u"Zbadaj element", @"devtools_inspect");
  command(developer, u"Pokaż źródło strony", @"view_source");
  command(developer, u"Odśwież bez pamięci podręcznej", @"hard_reload");
  [developer addItem:NSMenuItem.separatorItem];
  command(developer, u"Narzędzia developerskie…", @"developer");

  NSMenu* windows = submenu(u"Okno");
  native(windows, u"Minimalizuj", @selector(performMiniaturize:), @"m", kCommand);
  native(windows, u"Powiększ", @selector(performZoom:), @"", 0);
  [windows addItem:NSMenuItem.separatorItem];
  command(windows, u"Następna karta", @"next_tab");
  command(windows, u"Poprzednia karta", @"previous_tab");
  [windows addItem:NSMenuItem.separatorItem];
  command(windows, u"Następna przestrzeń", @"next_space");
  command(windows, u"Poprzednia przestrzeń", @"previous_space");
  command(windows, u"Pierwsza przestrzeń", @"space1");
  command(windows, u"Druga przestrzeń", @"space2");

  _previousMainMenu = self.mainMenu;
  _previousWindowsMenu = self.windowsMenu;
  self.mainMenu = bar;
  self.windowsMenu = windows;

  _workspaceDockMenu = [[NSMenu alloc] initWithTitle:@""];
  command(_workspaceDockMenu, u"Nowa karta", @"new_tab");
  CHECK(!self.delegate || self.delegate == self);
  self.delegate = self;
}
- (NSMenu*)applicationDockMenu:(NSApplication*)application {
  return _workspaceDockMenu;
}
- (BOOL)applicationShouldHandleReopen:(NSApplication*)application
                    hasVisibleWindows:(BOOL)hasVisibleWindows {
  if (!hasVisibleWindows && _workspaceMenuAction) {
    _workspaceMenuAction.Run({}, {}, "reopen_window", false);
  }
  return NO;
}
- (bool)dispatchWorkspaceMenuItem:(NSMenuItem*)item validate:(bool)validate {
  return _workspaceMenuAction &&
         _workspaceMenuAction.Run(
             gfx::NativeWindow(self.keyWindow), gfx::NativeWindow(self.mainWindow),
             base::SysNSStringToUTF8(item.representedObject), validate);
}
- (void)runWorkspaceMenuItem:(NSMenuItem*)item {
  [self dispatchWorkspaceMenuItem:item validate:false];
}
- (BOOL)validateMenuItem:(NSMenuItem*)item {
  return item.action != @selector(runWorkspaceMenuItem:) ||
         [self dispatchWorkspaceMenuItem:item validate:true];
}
- (bool)activateWorkspaceMenuForTesting:(const std::string&)command {
  if (command == "dock_reopen") {
    return ![self.delegate applicationShouldHandleReopen:self
                                         hasVisibleWindows:NO];
  }
  if (!self.isActive)
    [NSRunningApplication.currentApplication activateWithOptions:0];
  NSString* id = base::SysUTF8ToNSString(command);
  NSMutableArray<NSMenu*>* menus = [NSMutableArray array];
  if (command == "dock_new_tab") {
    NSMenu* dock = [self.delegate applicationDockMenu:self];
    if (!dock)
      return false;
    [menus addObject:dock];
    id = @"new_tab";
  } else {
    for (NSMenuItem* top in self.mainMenu.itemArray) {
      if (top.submenu)
        [menus addObject:top.submenu];
    }
  }
  for (NSMenu* menu in menus) {
    for (NSMenuItem* item in menu.itemArray) {
      if (!item.action)
        continue;
      const bool workspaceItem =
          item.action == @selector(runWorkspaceMenuItem:);
      if (workspaceItem ? ![item.representedObject isEqual:id]
                        : ![NSStringFromSelector(item.action) isEqual:id]) {
        continue;
      }
      [menu update];
      if (!item.enabled) {
        LOG(ERROR) << "Native Workspace menu item disabled: " << command
                   << " (key window: " << (self.keyWindow != nil)
                   << ", main window: " << (self.mainWindow != nil) << ")";
        return false;
      }
      [menu performActionForItemAtIndex:[menu indexOfItem:item]];
      return true;
    }
  }
  return false;
}
- (void)terminate:(id)sender {
  if (_workspaceQuit) {
    _workspaceQuit.Run();
    return;
  }
  [super terminate:sender];
}
- (BOOL)isHandlingSendEvent {
  return _handlingSendEvent;
}
- (void)setHandlingSendEvent:(BOOL)handlingSendEvent {
  _handlingSendEvent = handlingSendEvent;
}
- (void)sendEvent:(NSEvent*)event {
  base::AutoReset<BOOL> scope(&_handlingSendEvent, YES);
  content::ScopedNotifyNativeEventProcessorObserver notify(&_observers, event);
  [super sendEvent:event];
}
- (void)addNativeEventProcessorObserver:
    (content::NativeEventProcessorObserver*)observer {
  _observers.AddObserver(observer);
}
- (void)removeNativeEventProcessorObserver:
    (content::NativeEventProcessorObserver*)observer {
  _observers.RemoveObserver(observer);
}
@end

@interface ZitterContentWindowDelegate : NSObject <NSWindowDelegate>
- (void)setQuitClosure:(base::RepeatingClosure)quit;
@end

@implementation ZitterContentWindowDelegate {
  base::RepeatingClosure _quit;
}
- (void)setQuitClosure:(base::RepeatingClosure)quit {
  _quit = std::move(quit);
}
- (void)windowWillClose:(NSNotification*)notification {
  if (_quit)
    _quit.Run();
}
@end

namespace zitter::content_embedder {

std::vector<prototype::PageMenuItem> MacSpellingMenuItems(
    const content::WeakDocumentPtr& document,
    const content::ContextMenuParams& params) {
  NSSpellChecker* checker = NSSpellChecker.sharedSpellChecker;
  NSString* active = checker.language;
  const auto active_language = base::SysNSStringToUTF16(active);
  const bool automatic = checker.automaticallyIdentifiesLanguages;
  NSLocale* locale = [NSLocale localeWithLocaleIdentifier:
      prototype::UiLanguageIsEnglish() ? @"en" : @"pl"];
  prototype::PageMenuItem languages{.title = prototype::T(u"Język sprawdzania pisowni")};
  for (NSString* code in checker.availableLanguages) {
    NSString* name = [locale displayNameForKey:NSLocaleIdentifier value:code];
    const auto language = base::SysNSStringToUTF16(code);
    languages.children.push_back({
        .title = base::SysNSStringToUTF16(name ?: code),
        .run = base::BindRepeating([](const std::u16string& language) {
          NSSpellChecker* spell_checker = NSSpellChecker.sharedSpellChecker;
          if ([spell_checker setLanguage:base::SysUTF16ToNSString(language)])
            spell_checker.automaticallyIdentifiesLanguages = NO;
        }, language),
        .checked = !automatic && language == active_language});
  }
  std::ranges::sort(languages.children, {}, &prototype::PageMenuItem::title);
  languages.children.insert(languages.children.begin(), {
      .title = prototype::T(u"Automatycznie"),
      .run = base::BindRepeating([] {
        NSSpellChecker.sharedSpellChecker.automaticallyIdentifiesLanguages = YES;
      }),
      .checked = automatic});

  std::vector<prototype::PageMenuItem> items;
  const auto& word = params.misspelled_word.empty() ? params.selection_text
                                                    : params.misspelled_word;
  if (!word.empty() && word.size() <= 100 &&
      word.find_first_of(u" \r\n\t") == std::u16string::npos) {
    NSString* text = base::SysUTF16ToNSString(word);
    const NSRange range = [checker checkSpellingOfString:text startingAt:0
        language:automatic ? nil : active wrap:NO inSpellDocumentWithTag:0 wordCount:nullptr];
    if (range.location == 0 && range.length == text.length) {
      NSArray<NSString*>* guesses = [checker guessesForWordRange:range inString:text
          language:automatic ? nil : active inSpellDocumentWithTag:0];
      for (NSString* guess in guesses) {
        items.push_back({
            .title = base::SysNSStringToUTF16(guess),
            .run = base::BindRepeating([](const content::WeakDocumentPtr& document,
                                          const std::u16string& correction) {
              auto* frame = document.AsRenderFrameHostIfValid();
              auto* page = frame ? content::WebContents::FromRenderFrameHost(frame) : nullptr;
              if (frame && frame->IsActive() && page && page->GetFocusedFrame() == frame)
                page->Replace(correction);
            }, document, base::SysNSStringToUTF16(guess))});
        if (items.size() == 5)
          break;
      }
    }
  }
  if (!languages.children.empty())
    items.push_back(std::move(languages));
  return items;
}

void LaunchExternalProtocol(const GURL& url) {
  NSURL* target = net::NSURLWithGURL(url);
  NSWorkspace* workspace = NSWorkspace.sharedWorkspace;
  if (target && [workspace URLForApplicationToOpenURL:target]) {
    [workspace openURL:target
         configuration:[NSWorkspaceOpenConfiguration configuration]
     completionHandler:nil];
  }
}

namespace {

constexpr char kProduct[] = "Zitter Content Lab";
constexpr char kFramework[] = "Zitter Content Lab Framework.framework";
constexpr char kHelper[] = "Zitter Content Lab Helper";
constexpr char kSmokeTest[] = "zitter-content-smoke-test";
constexpr char kClipboardProbe[] = "zitter-content-clipboard-probe";
constexpr char kProfileProbe[] = "zitter-content-profile-probe";
constexpr char kPasswordFormProbe[] = "zitter-content-probe-password-form";
constexpr char kPasswordStoreProbe[] = "zitter-content-probe-password-store";
constexpr char kNavigationReputationProbe[] =
    "zitter-content-navigation-reputation-probe";
constexpr char kNavigationReputationProviderProbe[] =
    "zitter-content-navigation-reputation-provider-probe";
constexpr char kReputationSafePath[] = "/reputation/safe";
constexpr char kReputationBlockedPath[] = "/reputation/blocked";
constexpr char kReputationRedirectPath[] = "/reputation/redirect";
constexpr char kConsoleProbe[] = "zitter-content-probe-console";
constexpr char kScriptletProbe[] = "zitter-content-probe-scriptlets";
constexpr char kFingerprintProbe[] = "zitter-content-probe-fingerprint";
constexpr std::u16string_view kFingerprintResult = u"Zitter fingerprint result ";
constexpr int kFingerprintProbeSteps = 5;
constexpr char kProbeProfile[] = "zitter-content-probe-profile";

bool IsLoopbackProbeURL(const GURL& url) {
  return url.is_valid() && url.SchemeIs("http") && url.host() == "127.0.0.1" &&
         url.has_port() && !url.has_username() && !url.has_password();
}

bool IsProbeRoot(const base::FilePath& root) {
  base::FilePath temp;
  int permissions = 0;
  return base::GetTempDir(&temp) && !base::IsLink(root) &&
         root == base::MakeAbsoluteFilePath(root) &&
         root.DirName() == base::MakeAbsoluteFilePath(temp) &&
         root.BaseName().value().starts_with("zitter-content-probe-") &&
         base::DirectoryExists(root) &&
         base::GetPosixFilePermissions(root, &permissions) &&
         (permissions & 077) == 0;
}

constexpr std::string_view kExternalUrlProbeBundleId =
    "dev.zitter.content-launch-probe";

bool HasExternalUrlProbeRoot() {
  const auto root = base::Environment::Create()->GetVar(
      "ZITTER_CONTENT_STARTUP_TRIAL_ROOT");
  return root && IsProbeRoot(base::FilePath(*root));
}

void SetBundlePaths() {
  base::FilePath executable;
  CHECK(base::PathService::Get(base::FILE_EXE, &executable));
  base::FilePath contents = executable.DirName().DirName();
  if (base::apple::IsBackgroundOnlyProcess()) {
    // Walk from Helper.app/Contents to the outer app's Contents directory.
    for (int level = 0; level < 7; ++level)
      contents = contents.DirName();
  }
  CHECK_EQ(contents.BaseName().value(), "Contents");

  base::apple::SetOverrideOuterBundlePath(contents.DirName());
  base::apple::SetOverrideFrameworkBundlePath(
      contents.Append("Frameworks").Append(kFramework));
  const auto bundle_id =
      base::SysNSStringToUTF8(base::apple::OuterBundle().bundleIdentifier);
  CHECK(bundle_id == "dev.zitter.content-lab" ||
        bundle_id == "dev.zitter.browser" ||
        (bundle_id == kExternalUrlProbeBundleId &&
         (base::apple::IsBackgroundOnlyProcess() || HasExternalUrlProbeRoot())));
  base::apple::SetBaseBundleIDOverride(bundle_id);
  const auto helper = base::apple::FrameworkBundlePath()
                          .Append("Helpers")
                          .Append("Zitter Content Lab Helper.app")
                          .Append("Contents")
                          .Append("MacOS")
                          .Append(kHelper);
  CHECK(base::PathService::OverrideAndCreateIfNeeded(
      content::CHILD_PROCESS_EXE, helper, /*is_absolute=*/true,
      /*create=*/false));
}

using FileKeychainFixtureInstaller = bool (*)(const char*);

FileKeychainFixtureInstaller FindFileKeychainFixtureInstaller() {
  return reinterpret_cast<FileKeychainFixtureInstaller>(
      dlsym(RTLD_DEFAULT, "ZitterInstallFileKeychainFixture"));
}

bool UsesPreparedWorkspaceOnLaunch(const base::CommandLine& command) {
  const auto args = command.GetArgs();
  if (!command.GetSwitches().empty() || args.size() > 32 ||
      ![base::apple::OuterBundle()
          .infoDictionary[@"ZitterUsePreparedWorkspace"] isEqual:@YES] ||
      !std::ranges::all_of(args, [](const auto& value) {
        // Content registers URL schemes after BasicStartupComplete.
        return value.starts_with("http://") || value.starts_with("https://") ||
               prototype::IsLocalHtmlUrl(
                   prototype::ExternalOpenArgument(base::FilePath(value)));
      })) {
    return false;
  }
  if (base::apple::BaseBundleID() == "dev.zitter.browser")
    return true;
  return base::apple::BaseBundleID() == kExternalUrlProbeBundleId &&
         FindFileKeychainFixtureInstaller() && HasExternalUrlProbeRoot();
}

struct StagedMacUpdate {
  base::ScopedTempDir directory;
  base::FilePath app;
  base::FilePath installed;
};

bool CheckUpdateSignature(const base::FilePath& installed,
                          const base::FilePath& candidate) {
  base::apple::ScopedCFTypeRef<SecStaticCodeRef> current;
  base::apple::ScopedCFTypeRef<SecStaticCodeRef> update;
  base::apple::ScopedCFTypeRef<SecRequirementRef> requirement;
  if (SecStaticCodeCreateWithPath(
          (__bridge CFURLRef)base::apple::FilePathToNSURL(installed),
          kSecCSDefaultFlags, current.InitializeInto()) != errSecSuccess ||
      SecCodeCopyDesignatedRequirement(
          current.get(), kSecCSDefaultFlags, requirement.InitializeInto()) != errSecSuccess ||
      SecStaticCodeCreateWithPath(
          (__bridge CFURLRef)base::apple::FilePathToNSURL(candidate),
          kSecCSDefaultFlags, update.InitializeInto()) != errSecSuccess) {
    return false;
  }
  constexpr SecCSFlags flags = kSecCSCheckAllArchitectures |
                              kSecCSCheckNestedCode | kSecCSStrictValidate;
  std::string output;
  return SecStaticCodeCheckValidity(update.get(), flags, requirement.get()) == errSecSuccess &&
         base::GetAppOutput({"/usr/sbin/spctl", "--assess", "--type", "execute",
                             candidate.value()}, &output);
}

std::unique_ptr<StagedMacUpdate> StageMacUpdate(const MacInstaller& installer,
                                                const base::FilePath& installed) {
  @autoreleasepool {
    if (installed.DirName() != base::FilePath("/Applications") ||
        installed != base::MakeAbsoluteFilePath(installed) ||
        !base::DirectoryExists(installed)) {
      return nullptr;
    }
    auto update = std::make_unique<StagedMacUpdate>();
    if (!update->directory.CreateUniqueTempDirUnderPath(installed.DirName()))
      return nullptr;
    update->installed = installed;
    update->app = update->directory.GetPath().AppendASCII("Zitter.app");

    // Mount a private copy of the exact bytes matched against the release digest.
    const auto image = update->directory.GetPath().AppendASCII("update.dmg");
    const auto mount = update->directory.GetPath().AppendASCII("mount");
    if (!base::CopyFile(installer.path, image) ||
        !VerifyMacInstaller(image, installer.size, installer.digest) ||
        !base::CreateDirectory(mount)) {
      return nullptr;
    }
    std::string output;
    if (!base::GetAppOutput({"/usr/bin/hdiutil", "attach", "-readonly", "-nobrowse",
                             "-mountpoint", mount.value(), image.value()}, &output)) {
      return nullptr;
    }
    const auto mounted = mount.AppendASCII("Zitter.app");
    const bool copied = !base::IsLink(mounted) && base::DirectoryExists(mounted) &&
        base::GetAppOutput({"/usr/bin/ditto", mounted.value(), update->app.value()}, &output);
    const bool detached = base::GetAppOutput(
        {"/usr/bin/hdiutil", "detach", mount.value()}, &output);
    if (!detached) {
      // Leave a busy read-only mount for macOS to detach. Never recurse into it.
      (void)update->directory.Take();
      return nullptr;
    }
    base::DeleteFile(image);
    if (!copied || !CheckUpdateSignature(installed, update->app))
      return nullptr;
    NSBundle* bundle = [NSBundle bundleWithURL:base::apple::FilePathToNSURL(update->app)];
    NSString* version = [bundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
    if (![version isKindOfClass:NSString.class] ||
        base::SysNSStringToUTF8(version) != installer.version ||
        ![bundle.bundleIdentifier isEqualToString:@"dev.zitter.browser"]) {
      return nullptr;
    }
    return update;
  }
}

using OpenEncryptionCallback = base::OnceCallback<os_crypt_async::OSCryptAsync*(
    const base::FilePath&, base::FilePath, std::vector<base::FilePath>,
    std::vector<base::FilePath>, bool)>;
using ContinueWorkspaceStartup = base::OnceCallback<bool(bool)>;

class LabMainParts final : public content::BrowserMainParts,
                           private content::WebContentsObserver,
                           private content::WebContentsDelegate,
                           public password_manager::PasswordStoreConsumer {
 public:
  explicit LabMainParts(
      LabBrowserClientBase& browser,
      OpenEncryptionCallback open_encryption,
      base::OnceCallback<void(ContinueWorkspaceStartup)> check_encryption,
      bool ordinary_prepared_startup)
      : browser_(browser),
        open_encryption_(std::move(open_encryption)),
        check_encryption_(std::move(check_encryption)),
        ordinary_prepared_startup_(ordinary_prepared_startup) {}

  int PreCreateThreads() override {
    const auto& command_line = *base::CommandLine::ForCurrentProcess();
    navigation_reputation_probe_ = command_line.HasSwitch(kNavigationReputationProbe);
    reputation_provider_probe_ = command_line.HasSwitch(kNavigationReputationProviderProbe);
    if (reputation_provider_probe_ && !navigation_reputation_probe_)
      return 1;
    if (navigation_reputation_probe_ &&
        (ordinary_prepared_startup_ || base::apple::BaseBundleID() != "dev.zitter.content-lab" ||
         !command_line.HasSwitch(kProfileProbe) || command_line.HasSwitch(kDownloadProbe) ||
         command_line.HasSwitch(kConsoleProbe) || command_line.HasSwitch(kScriptletProbe) ||
         command_line.HasSwitch(kFingerprintProbe) || command_line.HasSwitch(kPasswordStoreProbe) ||
         command_line.HasSwitch(kPasswordFormProbe) || command_line.HasSwitch(kClipboardProbe))) {
      return 1;
    }
    if (ordinary_prepared_startup_)
      return OpenOrdinaryPreparedWorkspace() ? 0 : 1;
    password_form_probe_ = command_line.HasSwitch(kPasswordFormProbe);
    clipboard_probe_ = command_line.HasSwitch(kClipboardProbe);
    if ((clipboard_probe_ || password_form_probe_ ||
         command_line.HasSwitch(kPasswordStoreProbe)) &&
        (!command_line.HasSwitch(kProfileProbe) ||
         command_line.HasSwitch(kWorkspaceLab) ||
         command_line.HasSwitch(kWorkspaceSmokeTest))) {
      return 1;
    }
    workspace_smoke_ = command_line.HasSwitch(kWorkspaceSmokeTest);
    workspace_lab_ = workspace_smoke_ || command_line.HasSwitch(kWorkspaceLab);
    workspace_keychain_ = command_line.HasSwitch(kWorkspaceKeychain);
    workspace_mock_keychain_ = workspace_keychain_ &&
        command_line.HasSwitch(os_crypt::switches::kUseMockKeychain);
    workspace_legacy_keychain_ = command_line.HasSwitch(kWorkspaceLegacyKeychain);
    if (workspace_legacy_keychain_ &&
        (!workspace_keychain_ || !workspace_lab_ || workspace_mock_keychain_)) {
      return 1;
    }
    if (workspace_keychain_) {
      if (command_line.HasSwitch(kExtensionInstallReview) ||
          !command_line.HasSwitch(kWorkspaceRoot) ||
          (workspace_mock_keychain_ && !command_line.HasSwitch(kWorkspaceProbe))) {
        return 1;
      }
      if (!workspace_mock_keychain_ && command_line.HasSwitch(kWorkspaceProbe)) {
        install_file_keychain_fixture_ = FindFileKeychainFixtureInstaller();
        if (!install_file_keychain_fixture_)
          return 1;
      }
    }
    if ((workspace_smoke_ && command_line.HasSwitch(kWorkspaceLab)) ||
        (command_line.HasSwitch(kWorkspaceRoot) &&
         !command_line.HasSwitch(kWorkspaceLab))) {
      return 1;
    }
    if (command_line.HasSwitch(kWorkspaceProbe)) {
      workspace_probe_url_ =
          GURL(command_line.GetSwitchValueASCII(kWorkspaceProbe));
      if (!command_line.HasSwitch(kWorkspaceRoot) ||
          !IsLoopbackProbeURL(workspace_probe_url_) ||
          workspace_probe_url_.path() != "/" || workspace_probe_url_.has_query() ||
          workspace_probe_url_.has_ref()) {
        return 1;
      }
    }
    if (command_line.HasSwitch(kInteractiveImportKeychainProbe)) {
      if (!workspace_lab_ || workspace_keychain_ || !workspace_probe_url_.is_valid() ||
          !command_line.HasSwitch(kInteractiveImportProbe)) {
        return 1;
      }
      install_file_keychain_fixture_ = reinterpret_cast<bool (*)(const char*)>(
          dlsym(RTLD_DEFAULT, "ZitterInstallBrowserImportKeychainFixture"));
      if (!install_file_keychain_fixture_)
        return 1;
    }
    if (command_line.HasSwitch(kDownloadProbe)) {
      const auto root = command_line.GetSwitchValuePath(kProbeRoot);
      if (workspace_lab_ || command_line.HasSwitch(kSmokeTest) ||
          command_line.HasSwitch(kProfileProbe) ||
          command_line.HasSwitch(kConsoleProbe) ||
          !IsProbeRoot(root)) {
        return 1;
      }
      download_probe_ = DownloadProbe::Create(root);
      if (download_probe_ && download_probe_->native_user_save() &&
          base::apple::BaseBundleID() != "dev.zitter.content-lab") {
        LOG(ERROR) << "The interactive save probe requires the isolated Content Lab identity";
        return 1;
      }
      if (download_probe_ && download_probe_->persistent()) {
        const auto path = root.AppendASCII("download-profile");
        const auto network = path.AppendASCII("Network");
        if (base::IsLink(path) || !LockRoot(root.AppendASCII("download-profile.lock")) ||
            base::IsLink(network) || !base::CreateDirectory(network) ||
            !ProfileContext::IsHistoryStorePathSafe(path)) {
          return 1;
        }
        os_crypt_ = std::move(open_encryption_).Run(
            root.AppendASCII("probe-cookie-key"), {}, {network}, {}, false);
        if (!os_crypt_)
          return 1;
        persistent_path_ = path;
      }
      return download_probe_ &&
                     directory_.CreateUniqueTempDirUnderPath(root)
                 ? 0
                 : 1;
    }
    if (workspace_lab_) {
      if (command_line.HasSwitch(kSmokeTest) ||
          command_line.HasSwitch(kProfileProbe)) {
        return 1;
      }
      if (command_line.HasSwitch(kWorkspaceRoot)) {
        return directory_.CreateUniqueTempDir() &&
                       OpenWorkspaceRoot(command_line.GetSwitchValuePath(kWorkspaceRoot))
                   ? 0
                   : 1;
      }
      if (!directory_.CreateUniqueTempDir())
        return 1;
      return base::CreateDirectory(directory_.GetPath().AppendASCII("work")) &&
                     base::CreateDirectory(
                         directory_.GetPath().AppendASCII("personal"))
                 ? 0
                 : 1;
    }
    if (!command_line.HasSwitch(kProfileProbe))
      return directory_.CreateUniqueTempDir() ? 0 : 1;

    probe_url_ = GURL(command_line.GetSwitchValueASCII(kProfileProbe));
    const auto root = command_line.GetSwitchValuePath(kProbeRoot);
    const auto profile = command_line.GetSwitchValueASCII(kProbeProfile);
    if (!IsLoopbackProbeURL(probe_url_) ||
        command_line.HasSwitch(kSmokeTest) || !IsProbeRoot(root)) {
      return 1;
    }
    if (navigation_reputation_probe_ &&
        ((profile != "work" && profile != "private") ||
         probe_url_.GetPath() != kReputationSafePath ||
         probe_url_.has_query() || probe_url_.has_ref() ||
         (profile == "work" && !directory_.CreateUniqueTempDirUnderPath(root)))) {
      return 1;
    }
    console_probe_ = command_line.HasSwitch(kConsoleProbe);
    fingerprint_probe_ = command_line.HasSwitch(kFingerprintProbe);
    scriptlet_probe_ = command_line.HasSwitch(kScriptletProbe);
    if (command_line.HasSwitch(kPasswordStoreProbe)) {
      const auto action = command_line.GetSwitchValueASCII(kPasswordStoreProbe);
      if ((action != "write" && action != "read") || fingerprint_probe_ ||
          scriptlet_probe_) {
        return 1;
      }
      password_probe_profile_ = profile;
      write_password_probe_ = action == "write";
    }
    if (password_form_probe_ &&
        (console_probe_ || fingerprint_probe_ || scriptlet_probe_ ||
         !password_probe_profile_.empty() ||
         !probe_url_.path().starts_with("/password-form/"))) {
      return 1;
    }
    if ((fingerprint_probe_ || scriptlet_probe_) &&
        (console_probe_ || profile != "private" ||
         (fingerprint_probe_ && scriptlet_probe_))) {
      return 1;
    }
    if (clipboard_probe_ &&
        (profile != "private" || console_probe_ || fingerprint_probe_ ||
         scriptlet_probe_ || password_form_probe_ || !password_probe_profile_.empty() ||
         (probe_url_.path() != "/clipboard/primary" &&
          probe_url_.path() != "/clipboard/frame" &&
          probe_url_.path() != "/clipboard/custom" &&
          probe_url_.path() != "/clipboard/no-gesture"))) {
      return 1;
    }
    fingerprint_url_ = probe_url_;
    if (console_probe_ && profile != "work")
      return 1;
    if (profile == "private")
      return directory_.CreateUniqueTempDirUnderPath(root) ? 0 : 1;
    if (profile != "work" && profile != "personal")
      return 1;
    const auto path = root.AppendASCII(profile);
    const auto lock_path = path.AppendASCII("ProfileLock");
    const auto network_path = path.AppendASCII("Network");
    if (base::IsLink(path) || base::IsLink(lock_path) ||
        base::IsLink(network_path) ||
        !ProfileContext::IsPasswordStorePathSafe(path) ||
        !ProfileContext::IsHistoryStorePathSafe(path)) {
      return 1;
    }
    os_crypt_ = std::move(open_encryption_).Run(
        root.AppendASCII("probe-cookie-key"), {}, {network_path}, {}, false);
    if (!os_crypt_) {
      LOG(ERROR) << "The Content profile probe needs a private 32-byte key fixture";
      return 1;
    }
    if (!base::CreateDirectory(network_path) || !LockRoot(lock_path))
      return 1;
    persistent_path_ = path;
    file_chooser_prefs_[0] = ProfileContext::OpenFileChooserPreferences(path);
    password_saving_defaults_[0] = ProfileContext::ReadPasswordSavingDefault(path);
    grant_storage_probe_ = profile == "work";
    return 0;
  }

  int PreMainMessageLoopRun() override {
    screen_.emplace();
    if (workspace_lab_) {
      if (!workspace_keychain_ &&
          !ConfigureWorkspaceExtensionProbe(workspace_extension_profiles_)) {
        return 1;
      }
      if (workspace_root_.empty() && !IsWorkspaceExtensionReviewOnly())
        CreateWorkspace();
      return 0;
    }
    const bool persistent = !persistent_path_.empty();
    context_ = std::make_unique<ProfileContext>(
        persistent ? persistent_path_ : directory_.GetPath(),
        persistent ? ProfileContext::Mode::kPersistent
                   : ProfileContext::Mode::kPrivate,
        grant_storage_probe_
            ? std::make_optional(url::Origin::Create(probe_url_))
            : std::nullopt,
        os_crypt_, password_saving_defaults_[0]);
    context_->SetFileChooserPreferences(std::move(file_chooser_prefs_[0]));
    if (reputation_provider_probe_) {
      base::CommandLine::ForCurrentProcess()->AppendSwitch(
          safe_browsing::switches::kSbFastInitialListsUpdate);
      browser_->StartNavigationReputationProviderProbe(
          directory_.GetPath(), probe_url_.Resolve("/"),
          base::BindOnce(&LabMainParts::LoadReputationProbePage,
                         weak_factory_.GetWeakPtr()));
    } else if (navigation_reputation_probe_) {
      base::CommandLine::ForCurrentProcess()->AppendSwitchASCII(
          safe_browsing::switches::kMarkAsPhishing,
          probe_url_.Resolve(kReputationBlockedPath).spec());
      browser_->StartNavigationReputation(directory_.GetPath(), {});
    }
    if (download_probe_)
      download_probe_->Prepare(*context_);
    if (scriptlet_probe_ && !context_->ConfigureAdblockProbe(probe_url_))
      return 1;
    contents_ = content::WebContents::Create(
        content::WebContents::CreateParams(context_.get()));
    if (clipboard_probe_) {
      browser_->SetClipboardCopyProbe(base::BindRepeating(
          &LabMainParts::ObserveClipboardWrite, weak_factory_.GetWeakPtr()));
    }
    contents_->SetDelegate(this);
    if (probe_url_.is_valid() ||
        base::CommandLine::ForCurrentProcess()->HasSwitch(kSmokeTest)) {
      Observe(contents_.get());
    }

    window_delegate_ = [[ZitterContentWindowDelegate alloc] init];
    window_ = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 1000, 700)
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                            NSWindowStyleMaskMiniaturizable |
                            NSWindowStyleMaskResizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    window_.releasedWhenClosed = NO;
    window_.delegate = window_delegate_;
    window_.title = @(kProduct);
    window_.contentView = contents_->GetNativeView().GetNativeNSView();
    [window_ center];
    [window_ makeKeyAndOrderFront:nil];
    [NSApp activate];
    if (clipboard_probe_)
      contents_->Focus();

    if (download_probe_ || reputation_provider_probe_)
      return 0;
    const GURL url = LabStartPage();
    contents_->GetController().LoadURLWithParams(
        content::NavigationController::LoadURLParams(
            probe_url_.is_valid() ? probe_url_ : url));
    return 0;
  }

  void WillRunMainMessageLoop(std::unique_ptr<base::RunLoop>& loop) override {
    quit_ = loop->QuitClosure();
    if (IsWorkspaceExtensionReviewOnly()) {
      quit_.Run();
      return;
    }
    if (!workspace_root_.empty()) {
      std::move(check_encryption_).Run(
          base::BindOnce(
              [](base::WeakPtr<LabMainParts> self, bool verified) {
                return self && self->WorkspaceEncryptionReady(verified);
              },
              weak_factory_.GetWeakPtr()));
      return;
    }
    if (workspace_) {
      StartWorkspace();
      return;
    }
    [window_delegate_ setQuitClosure:quit_];
    if (download_probe_) {
      download_probe_->Start(*contents_, quit_);
      if (auto save = download_probe_->native_user_save()) {
        CheckNativeDownloadSavePanel(*save,
                                     base::TimeTicks::Now() + base::Seconds(12));
      }
    }
  }

  void CheckNativeDownloadSavePanel(bool save, base::TimeTicks deadline) {
    NSSavePanel* panel = base::apple::ObjCCast<NSSavePanel>(window_.attachedSheet);
    if (panel && panel.isVisible) {
      const auto directory = base::CommandLine::ForCurrentProcess()
                                 ->GetSwitchValuePath(kProbeRoot)
                                 .AppendASCII("downloads");
      panel.extensionHidden = NO;
      if (base::apple::NSStringToFilePath(panel.directoryURL.path) != directory ||
          ![panel.nameFieldStringValue isEqualToString:@"download-probe.txt"]) {
        LOG(ERROR) << "Native download save panel did not use the expected path";
        quit_.Run();
        return;
      }
      if (!save) {
        [panel cancel:nil];
      }
      std::puts("Zitter Content exercised the native download save panel");
      std::fflush(stdout);
      return;
    }
    if (base::TimeTicks::Now() >= deadline) {
      LOG(ERROR) << "Native download save panel did not appear";
      quit_.Run();
      return;
    }
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, base::BindOnce(&LabMainParts::CheckNativeDownloadSavePanel,
                                  weak_factory_.GetWeakPtr(), save, deadline),
        base::Milliseconds(50));
  }

  void StartWorkspace() {
    const std::string application_version = base::SysNSStringToUTF8(
        [NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"]);
    workspace_->SetApplicationVersion(application_version);
    [static_cast<ZitterContentApplication*>(NSApp)
        setWorkspaceQuitClosure:base::BindRepeating(
            &WorkspaceLab::RequestClose, base::Unretained(workspace_.get()))];
    if (!workspace_smoke_) {
      auto* app = static_cast<ZitterContentApplication*>(NSApp);
      [app setWorkspaceMenuAction:base::BindRepeating(
          &WorkspaceLab::HandleNativeMenuCommand,
          base::Unretained(workspace_.get()))];
      if (workspace_probe_url_.is_valid()) {
        workspace_->SetNativeMenuProbe(base::BindRepeating([](std::string command) {
          return [static_cast<ZitterContentApplication*>(NSApp)
              activateWorkspaceMenuForTesting:command];
        }));
      }
    }
    if (ordinary_prepared_startup_) {
      [static_cast<ZitterContentApplication*>(NSApp)
          setExternalUrlHandler:base::BindRepeating(
              &WorkspaceLab::OpenExternalUrl, base::Unretained(workspace_.get()))];
      for (const auto& arg : base::CommandLine::ForCurrentProcess()->GetArgs())
        workspace_->OpenExternalUrl(
            prototype::ExternalOpenArgument(base::FilePath(arg)));
    }
    if (ordinary_prepared_startup_ && !install_file_keychain_fixture_) {
      auto* shell = workspace_->shell();
      updater_ = std::make_unique<MacUpdater>(
          *shell, application_version,
          MacReleaseBackend::kContent,
          workspace_root_.AppendASCII("Updates"),
          base::BindRepeating([](base::WeakPtr<LabMainParts> self)
              -> scoped_refptr<network::SharedURLLoaderFactory> {
            if (!self || !self->context_)
              return nullptr;
            return self->context_->GetDefaultStoragePartition()
                ->GetURLLoaderFactoryForBrowserProcess();
          }, weak_factory_.GetWeakPtr()),
          base::BindRepeating([](WorkspaceLab* workspace, const GURL& url) {
            workspace->OpenExternalUrl(url);
          }, base::Unretained(workspace_.get())),
          base::BindRepeating([](const base::FilePath& path) {
            [NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:
                @[base::apple::FilePathToNSURL(path)]];
          }));
      updater_->SetInstallCallback(base::BindRepeating(
          &LabMainParts::InstallMacUpdate, weak_factory_.GetWeakPtr()));
      updater_->Attach();
    }
    workspace_->Start(
        base::BindOnce([](base::WeakPtr<LabMainParts> self, bool smoke, bool success) {
          if (!self)
            return;
          if (smoke) {
            if (success)
              std::puts("Zitter Content workspace smoke passed");
            else
              LOG(ERROR) << "Zitter Content workspace smoke failed";
          }
          if (self->workspace_probe_url_.is_valid() ||
              GetExtensionFixtureStateAction() ==
                  ExtensionFixtureStateAction::kManageSmoke) {
            if (!success) {
              LOG(ERROR) << "Zitter Content workspace session probe failed";
              self->quit_.Run();
              return;
            }
            std::puts("Zitter Content workspace session probe passed");
          }
          self->FinishWorkspace();
        },
        weak_factory_.GetWeakPtr(), workspace_smoke_),
        base::BindOnce([] { [NSApp terminate:nil]; }));
  }

  void PostMainMessageLoopRun() override {
    [static_cast<ZitterContentApplication*>(NSApp)
        setExternalUrlHandler:base::RepeatingCallback<void(GURL)>()];
    [static_cast<ZitterContentApplication*>(NSApp)
        setWorkspaceQuitClosure:base::RepeatingClosure()];
    [static_cast<ZitterContentApplication*>(NSApp)
        setWorkspaceMenuAction:WorkspaceMenuAction()];
    download_probe_.reset();
    updater_.reset();
    workspace_.reset();
    additional_profiles_.clear();
    personal_context_.reset();
    Observe(nullptr);
    console_reader_.reset();
    window_.delegate = nil;
    window_.contentView = nil;
    [window_ close];
    window_ = nil;
    window_delegate_ = nil;
    contents_.reset();
    context_.reset();
    browser_->StopNavigationReputation();
  }

  void PostDestroyThreads() override {
    base::FilePath restart;
    if (install_mac_update_on_exit_ && staged_mac_update_) {
      auto update = std::move(staged_mac_update_);
      // Keep the profile locked until the atomic bundle exchange completes.
      if (renamex_np(update->app.value().c_str(), update->installed.value().c_str(),
                     RENAME_SWAP) != 0) {
        PLOG(ERROR) << "Could not replace the installed Zitter application";
      }
      restart = update->installed;
    }
    profile_lock_.Close();
    if (directory_.IsValid())
      (void)directory_.Delete();
    if (!restart.empty()) {
      // Launch the executable of the verified installed bundle. Launch Services
      // can translocate the quarantined bundle out of Applications.
      const auto executable =
          restart.AppendASCII("Contents").AppendASCII("MacOS").AppendASCII("Zitter Content Lab");
      if (!base::LaunchProcess({executable.value()}, {}).IsValid())
        LOG(ERROR) << "Could not relaunch Zitter after the update";
    }
  }

 private:
  const char* WorkspaceProfileDirectory(size_t index, bool durable) const {
    static constexpr std::array<const char*, 2> kTemporary = {"work", "personal"};
    static constexpr std::array<const char*, 2> kFixture = {
        "workspace-work", "workspace-personal"};
    static constexpr std::array<const char*, 2> kKeychain = {
        "keychain-work", "keychain-personal"};
    static constexpr std::array<const char*, 2> kMockKeychain = {
        "mock-keychain-work", "mock-keychain-personal"};
    if (workspace_mock_keychain_)
      return kMockKeychain[index];
    if (workspace_keychain_)
      return kKeychain[index];
    return durable ? kFixture[index] : kTemporary[index];
  }

  base::FilePath WorkspaceSessionPath(const base::FilePath& root) const {
    if (workspace_mock_keychain_)
      return root.AppendASCII("ContentMockKeychainWorkspaceSession.json");
    return root.AppendASCII(workspace_keychain_
                                ? "ContentKeychainWorkspaceSession.json"
                                : "ContentWorkspaceSession.json");
  }

  base::FilePath WorkspaceSettingsPath(const base::FilePath& root) const {
    if (workspace_mock_keychain_)
      return root.AppendASCII("MockKeychainWorkspaceSettings.json");
    return root.AppendASCII(workspace_keychain_ ? "KeychainWorkspaceSettings.json"
                                                : "WorkspaceSettings.json");
  }

  void CreateWorkspace(std::string download_safe_browsing_key = {}) {
    const auto& command_line = *base::CommandLine::ForCurrentProcess();
    const bool durable = !workspace_root_.empty();
    const auto root = durable ? workspace_root_ : directory_.GetPath();
    const auto mode = durable ? ProfileContext::Mode::kPersistent
                              : ProfileContext::Mode::kPrivate;
    context_ = std::make_unique<ProfileContext>(
        root.AppendASCII(WorkspaceProfileDirectory(0, durable)), mode,
        std::nullopt, os_crypt_, password_saving_defaults_[0]);
    personal_context_ = std::make_unique<ProfileContext>(
        root.AppendASCII(WorkspaceProfileDirectory(1, durable)), mode,
        std::nullopt, os_crypt_, password_saving_defaults_[1]);
    if (durable) {
      context_->SetFileChooserPreferences(std::move(file_chooser_prefs_[0]));
      personal_context_->SetFileChooserPreferences(
          std::move(file_chooser_prefs_[1]));
      context_->SetPageZoomPreferences(std::move(page_zoom_prefs_[0]));
      personal_context_->SetPageZoomPreferences(std::move(page_zoom_prefs_[1]));
      context_->SetFingerprintPreferences(std::move(fingerprint_prefs_[0]));
      personal_context_->SetFingerprintPreferences(
          std::move(fingerprint_prefs_[1]));
      context_->SetAdblockPreferences(std::move(adblock_prefs_[0]));
      personal_context_->SetAdblockPreferences(std::move(adblock_prefs_[1]));
      context_->site_permissions().SetSitePermissionPreferences(
          std::move(site_permission_prefs_[0]));
      personal_context_->site_permissions().SetSitePermissionPreferences(
          std::move(site_permission_prefs_[1]));
    }
    for (auto& startup : additional_profile_startup_)
      additional_profiles_.push_back(startup->Open(*os_crypt_));
    additional_profile_startup_.clear();
    base::FilePath user_download_directory;
    if (ordinary_prepared_startup_ && durable) {
      user_download_directory = install_file_keychain_fixture_
          ? root : base::GetHomeDir().AppendASCII("Downloads");
    }
    const bool offer_startup_import = ordinary_prepared_startup_ && durable &&
        (!install_file_keychain_fixture_ || !startup_import_probe_.empty()) &&
        (workspace_session_.empty() ||
         workspace_session_.FindBool("startup_import_pending").value_or(false));
    bool dark = command_line.GetSwitchValueASCII("zitter-content-workspace-theme") == "dark";
    bool metal = command_line.GetSwitchValueASCII("zitter-rust-raster") == "metal";
    if (!startup_import_probe_.empty()) {
      auto env = base::Environment::Create();
      dark = env->GetVar("ZITTER_CONTENT_STARTUP_IMPORT_THEME") == "dark";
      metal = env->GetVar("ZITTER_CONTENT_STARTUP_IMPORT_RENDERER") == "metal";
    }
    workspace_ = std::make_unique<WorkspaceLab>(
        WorkspaceSettingsPath(root), directory_.GetPath(),
        durable ? WorkspaceSessionPath(root) : base::FilePath(),
        std::move(workspace_session_), std::move(workspace_timeline_),
        workspace_probe_url_, *context_,
        personal_context_.get(),
        dark, metal,
        workspace_smoke_, nullptr, additional_profiles_, os_crypt_,
        std::move(user_download_directory), std::move(download_safe_browsing_key),
        offer_startup_import, startup_import_probe_);
  }

  void StartWorkspaceWithDownloadPolicy(std::string download_safe_browsing_key) {
    if (!download_safe_browsing_key.empty()) {
      browser_->StartNavigationReputation(workspace_root_,
                                          download_safe_browsing_key);
    }
    CreateWorkspace(std::move(download_safe_browsing_key));
    StartWorkspace();
  }

  bool WorkspaceEncryptionReady(bool verified) {
    if (!verified) {
      LOG(ERROR) << (workspace_legacy_keychain_
          ? "The Content workspace could not verify legacy encrypted stores"
          : "The Content workspace could not verify its cookie stores");
      quit_.Run();
      return false;
    }
    if (workspace_legacy_keychain_) {
      base::ScopedAllowBlockingForTesting allow_directory_io;
      for (size_t index = 0; index < kWorkspaceIds.size(); ++index) {
        if (!base::CreateDirectory(workspace_root_
                .AppendASCII(WorkspaceProfileDirectory(index, true))
                .AppendASCII("Network"))) {
          LOG(ERROR) << "Could not open the legacy Content network directory";
          quit_.Run();
          return false;
        }
      }
    }
    if (ordinary_prepared_startup_) {
      base::ScopedAllowBlockingForTesting allow_extension_io;
      RestoreApprovedWorkspaceExtensions(workspace_extension_profiles_);
    } else if (workspace_keychain_ &&
        (base::CommandLine::ForCurrentProcess()->HasSwitch(kRestoreWorkspaceExtensions) ||
         (workspace_mock_keychain_ && IsWorkspaceExtensionRuntime()))) {
      // The synchronous fixture installer runs only for the disposable
      // loopback probe, before any BrowserContext is created.
      base::ScopedAllowBlockingForTesting allow_fixture_io;
      if (!ConfigureWorkspaceExtensionProbe(workspace_extension_profiles_)) {
        quit_.Run();
        return false;
      }
    }
    if (ordinary_prepared_startup_ ||
        (workspace_lab_ && !workspace_probe_url_.is_valid() &&
         base::CommandLine::ForCurrentProcess()->HasSwitch(kRestoreWorkspaceExtensions))) {
      base::ScopedAllowBlockingForTesting allow_bundled_extension_io;
      if (!ConfigureBundledNightShift(base::apple::FrameworkBundlePath()
                                         .AppendASCII("Resources")
                                         .AppendASCII("night_shift"))) {
        LOG(ERROR) << "Could not configure bundled Night Shift";
      }
    }
    if (ordinary_prepared_startup_ && !install_file_keychain_fixture_) {
      base::ThreadPool::PostTaskAndReplyWithResult(
          FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
          base::BindOnce(&zitter::LoadSafeBrowsingKeyFile),
          base::BindOnce(&LabMainParts::StartWorkspaceWithDownloadPolicy,
                         weak_factory_.GetWeakPtr()));
    } else {
      StartWorkspaceWithDownloadPolicy({});
    }
    return true;
  }

  bool LockRoot(const base::FilePath& lock_path) {
    if (base::IsLink(lock_path))
      return false;
    profile_lock_.Initialize(lock_path, base::File::FLAG_OPEN_ALWAYS |
                                            base::File::FLAG_WRITE);
    return profile_lock_.IsValid() &&
           profile_lock_.Lock(base::File::LockMode::kExclusive) ==
               base::File::FILE_OK;
  }

  bool OpenOrdinaryPreparedWorkspace() {
    for (const auto& value : base::CommandLine::ForCurrentProcess()->GetArgs()) {
      if (!prototype::IsExternalOpenUrl(
              prototype::ExternalOpenArgument(base::FilePath(value)))) {
        LOG(ERROR) << "Invalid URL argument for Content Workspace startup";
        return false;
      }
    }
    base::FilePath root;
    const auto trial_root = base::Environment::Create()->GetVar(
        "ZITTER_CONTENT_STARTUP_TRIAL_ROOT");
    if (trial_root) {
      root = base::FilePath(*trial_root);
      install_file_keychain_fixture_ = FindFileKeychainFixtureInstaller();
      if (!IsProbeRoot(root) || !install_file_keychain_fixture_) {
        LOG(ERROR) << "Invalid isolated Content startup trial";
        return false;
      }
      startup_import_probe_ = base::Environment::Create()
          ->GetVar("ZITTER_CONTENT_STARTUP_IMPORT_PROBE").value_or("");
      if (!startup_import_probe_.empty() && startup_import_probe_ != "keep" &&
          startup_import_probe_ != "skip" && startup_import_probe_ != "reopen" &&
          startup_import_probe_ != "apply" && startup_import_probe_ != "csv") {
        LOG(ERROR) << "Invalid isolated Content startup import phase";
        return false;
      }
    } else {
      if (!base::PathService::Get(base::DIR_APP_DATA, &root))
        return false;
      if (!base::CreateDirectory(root) ||
          base::MakeAbsoluteFilePath(root) != root) {
        return false;
      }
      root = root.AppendASCII("Zitter Content");
      if (!base::PathExists(root) && !base::IsLink(root) &&
          mkdir(root.value().c_str(), 0700) != 0) {
        return false;
      }
    }
    workspace_lab_ = true;
    workspace_keychain_ = true;
    workspace_legacy_keychain_ = IsPreparedContentWorkspace(root, "keychain");
    fresh_startup_ = !workspace_legacy_keychain_;
    return directory_.CreateUniqueTempDir() && OpenWorkspaceRoot(root);
  }

  bool OpenWorkspaceRoot(const base::FilePath& root) {
    const bool prepared = (ordinary_prepared_startup_ && !fresh_startup_) ||
        base::CommandLine::ForCurrentProcess()->HasSwitch(kOpenPreparedWorkspace);
    bool initialize_fresh_layout = false;
    if (fresh_startup_) {
      int permissions = 0;
      if (base::IsLink(root) || !base::DirectoryExists(root) ||
          !base::GetPosixFilePermissions(root, &permissions) ||
          (permissions & 077) != 0) {
        LOG(ERROR) << "Invalid fresh Content workspace directory";
        return false;
      }
      initialize_fresh_layout = base::IsDirectoryEmpty(root);
      if (!initialize_fresh_layout &&
          !IsPreparedContentWorkspace(root, "fresh-keychain")) {
        LOG(ERROR) << "Invalid fresh Content workspace layout";
        return false;
      }
    } else if (prepared) {
      int permissions = 0;
      const auto session = WorkspaceSessionPath(root);
      if (!workspace_keychain_ ||
          !IsPreparedContentWorkspace(root, workspace_mock_keychain_ ? "mock-keychain"
                                                                     : "keychain") ||
          base::IsLink(session) || !base::PathExists(session) ||
          !base::GetPosixFilePermissions(root, &permissions) ||
          (permissions & 077) != 0) {
        LOG(ERROR) << "Invalid prepared Content workspace";
        return false;
      }
    } else if (!IsProbeRoot(root)) {
      LOG(ERROR) << "The Content workspace requires a private temporary probe root";
      return false;
    }
    if (!LockRoot(root.AppendASCII("WorkspaceLock"))) {
      LOG(ERROR) << "Could not lock the Content workspace root";
      return false;
    }
    if (initialize_fresh_layout) {
      base::File marker(root.AppendASCII("ContentWorkspaceLayout"),
                        base::File::FLAG_CREATE | base::File::FLAG_WRITE);
      constexpr std::string_view layout =
          "zitter-content-workspace-v1 fresh-keychain\n";
      if (!marker.IsValid() ||
          !marker.WriteAtCurrentPosAndCheck(base::as_byte_span(layout)) ||
          !IsPreparedContentWorkspace(root, "fresh-keychain")) {
        LOG(ERROR) << "Could not initialize fresh Content workspace layout";
        return false;
      }
    }
    if (install_file_keychain_fixture_ &&
        !install_file_keychain_fixture_(root.value().c_str())) {
      LOG(ERROR) << "Could not open the Content file Keychain fixture";
      return false;
    }
    auto restored = ReadContentWorkspaceSession(WorkspaceSessionPath(root));
    const auto settings = WorkspaceSettingsPath(root);
    if (!restored || (prepared && restored->empty()) || base::IsLink(settings)) {
      LOG(ERROR) << "Invalid Content workspace session or settings path";
      return false;
    }

    workspace_timeline_ = ReadWorkspaceTimeline(
        WorkspaceTimelinePath(WorkspaceSessionPath(root)), *restored);
    if (!workspace_timeline_) {
      LOG(ERROR) << "Invalid Content workspace timeline";
      return false;
    }

    const auto network_root = root.AppendASCII(kWorkspaceNetworkRoot);
    if (base::IsLink(network_root)) {
      LOG(ERROR) << "Invalid Content network storage root";
      return false;
    }
    std::vector<base::FilePath> network_paths;
    std::vector<base::FilePath> login_paths;
    for (size_t index = 0; index < kWorkspaceIds.size(); ++index) {
      const auto profile =
          root.AppendASCII(WorkspaceProfileDirectory(index, true));
      const auto network = profile.AppendASCII("Network");
      if (base::IsLink(profile) || base::IsLink(network) ||
          !ProfileContext::IsPasswordStorePathSafe(profile) ||
          !ProfileContext::IsHistoryStorePathSafe(profile)) {
        return false;
      }
      password_saving_defaults_[index] =
          ProfileContext::ReadPasswordSavingDefault(profile);
      network_paths.push_back(network);
      login_paths.push_back(profile);
    }
    const auto profiles = ReadContentProfiles(*restored).value();
    for (size_t index = kWorkspaceIds.size(); index < profiles.size(); ++index) {
      const auto path = root.AppendASCII(profiles[index].id);
      const auto separate = profiles[index].separate_network
                                ? network_root.AppendASCII(profiles[index].id)
                                : base::FilePath();
      auto startup = WorkspaceProfileStartup::Read(path, separate);
      if (!startup) {
        LOG(ERROR) << "Invalid additional Content profile directory or preferences";
        return false;
      }
      network_paths.push_back(separate.empty() ? path.AppendASCII("Network")
                                               : separate);
      login_paths.push_back(path);
      additional_profile_startup_.push_back(std::move(startup));
    }
    const auto key_path = workspace_keychain_ ? base::FilePath()
                                               : root.AppendASCII("probe-cookie-key");
    if (IsWorkspaceExtensionRuntime())
      workspace_extension_profiles_ = login_paths;
    os_crypt_ = std::move(open_encryption_).Run(
        key_path, network_root, network_paths, std::move(login_paths),
        workspace_legacy_keychain_);
    if (!os_crypt_) {
      LOG(ERROR) << "The Content workspace needs a private 32-byte key fixture";
      return false;
    }
    if (!base::CreateDirectory(network_root)) {
      LOG(ERROR) << "Could not open the Content network storage root";
      return false;
    }

    if (!workspace_legacy_keychain_) {
      for (const auto& path : network_paths) {
        if (!base::CreateDirectory(path))
          return false;
      }
    }

    for (size_t index = 0; index < kWorkspaceIds.size(); ++index) {
      const auto profile_path = root.AppendASCII(WorkspaceProfileDirectory(index, true));
      file_chooser_prefs_[index] =
          ProfileContext::OpenFileChooserPreferences(profile_path);
      site_permission_prefs_[index] = ProfileContext::OpenSitePermissionPreferences(profile_path);
      if (!site_permission_prefs_[index]) {
        LOG(ERROR) << "Could not open Content site permissions";
        return false;
      }
      page_zoom_prefs_[index] = ProfileContext::OpenPageZoomPreferences(profile_path);
      if (!page_zoom_prefs_[index]) {
        LOG(ERROR) << "Could not open Content page zoom preferences";
        return false;
      }
      fingerprint_prefs_[index] = ProfileContext::OpenFingerprintPreferences(profile_path);
      adblock_prefs_[index] = ProfileContext::OpenAdblockPreferences(profile_path);
      if (!adblock_prefs_[index]) {
        LOG(ERROR) << "Could not open Content ad blocking preferences";
        return false;
      }
      if (!fingerprint_prefs_[index]) {
        LOG(ERROR) << "Could not open Content fingerprint preferences";
        return false;
      }
    }

    workspace_root_ = root;
    workspace_session_ = std::move(*restored);
    return true;
  }

  void InstallMacUpdate(const MacInstaller& installer) {
    if (staging_mac_update_)
      return;
    if (staged_mac_update_ && staged_mac_update_version_ == installer.version) {
      workspace_->RequestClose();
      return;
    }
    staged_mac_update_.reset();
    staging_mac_update_ = true;
    auto* shell = workspace_->shell();
    shell->ShowUpdateOffer(installer.version, prototype::T(u"Przygotowywanie aktualizacji…"),
                           {}, {});
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
        base::BindOnce(&StageMacUpdate, installer, base::apple::OuterBundlePath()),
        base::BindOnce(&LabMainParts::MacUpdateStaged, weak_factory_.GetWeakPtr(), installer));
  }

  void MacUpdateStaged(MacInstaller installer, std::unique_ptr<StagedMacUpdate> update) {
    staging_mac_update_ = false;
    auto* shell = workspace_->shell();
    if (!update) {
      shell->ShowUpdateOffer(
          installer.version,
          prototype::T(u"Nie udało się przygotować aktualizacji. Możesz zainstalować ją z pliku DMG."),
          prototype::T(u"Pokaż instalator"),
          base::BindRepeating([](const base::FilePath& path) {
            [NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:
                @[base::apple::FilePathToNSURL(path)]];
          }, installer.path));
      return;
    }
    staged_mac_update_ = std::move(update);
    staged_mac_update_version_ = installer.version;
    shell->ShowUpdateOffer(
        installer.version, prototype::T(u"Aktualizacja jest gotowa."),
        prototype::T(u"Zainstaluj i uruchom ponownie"),
        base::BindRepeating(&LabMainParts::InstallMacUpdate, weak_factory_.GetWeakPtr(), installer));
    workspace_->RequestClose();
  }

  void FinishWorkspace() {
    install_mac_update_on_exit_ = !!staged_mac_update_;
    if (workspace_root_.empty()) {
      quit_.Run();
      return;
    }
    const auto profiles = workspace_->profiles_for_shutdown();
    const bool flush_extensions = IsWorkspaceExtensionRuntime();
    auto flushed = base::BarrierClosure(profiles.size() + flush_extensions, quit_);
    for (const auto& profile : profiles) {
      auto* partition = profile->GetDefaultStoragePartition();
      partition->Flush();
      partition->GetCookieManagerForBrowserProcess()->FlushCookieStore(flushed);
    }
    if (flush_extensions) {
      extensions::GetExtensionFileTaskRunner()->PostTaskAndReply(
          FROM_HERE, base::DoNothing(), flushed);
    }
  }

  void RunFileChooser(
      content::RenderFrameHost* frame,
      scoped_refptr<content::FileSelectListener> listener,
      const blink::mojom::FileChooserParams& params) override {
    RunPageFileChooser(frame, std::move(listener), params);
  }

  void EnumerateDirectory(
      content::WebContents* contents,
      scoped_refptr<content::FileSelectListener> listener,
      const base::FilePath& path) override {
    EnumeratePageDirectory(contents, std::move(listener), path);
  }

  void LoadReputationProbePage() {
    contents_->GetController().LoadURLWithParams(
        content::NavigationController::LoadURLParams(probe_url_));
  }

  void DidFinishNavigation(content::NavigationHandle* navigation) override {
    if (!navigation_reputation_probe_ || !navigation->HasCommitted())
      return;
    const bool blocked = navigation->GetURL() == probe_url_.Resolve(kReputationBlockedPath) &&
                         navigation->IsErrorPage() &&
                         navigation->GetNetErrorCode() == net::ERR_BLOCKED_BY_CLIENT;
    if (!navigation->IsInPrimaryMainFrame()) {
      if (reputation_probe_step_ == 0 && blocked)
        reputation_subframe_blocked_ = true;
      return;
    }
    reputation_navigation_valid_ =
        reputation_probe_step_ == 0
            ? navigation->GetURL() == probe_url_ && !navigation->IsErrorPage()
            : blocked && navigation->GetRedirectChain().size() ==
                             (reputation_probe_step_ == 1 ? 1u : 2u);
  }

  void AdvanceNavigationReputationProbe() {
    const bool expected_title = reputation_probe_step_ == 0
        ? contents_->GetTitle() == u"Safe loopback page" &&
              reputation_subframe_blocked_
        : contents_->GetTitle() == u"Unsafe site";
    if (!reputation_navigation_valid_ || !expected_title) {
      LOG(ERROR) << "Unexpected reputation result at step " << reputation_probe_step_;
      quit_.Run();
      return;
    }
    ++reputation_probe_step_;
    if (reputation_probe_step_ == 3) {
      std::puts("Zitter Content blocked unsafe direct, redirected and subframe navigations");
      quit_.Run();
      return;
    }
    reputation_navigation_valid_ = false;
    auto target = probe_url_.Resolve(reputation_probe_step_ == 1
                                         ? kReputationBlockedPath
                                         : kReputationRedirectPath);
    // Start the next load after the current load's observer notifications.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce([](base::WeakPtr<LabMainParts> self, GURL url) {
          if (self) {
            self->contents_->GetController().LoadURLWithParams(
                content::NavigationController::LoadURLParams(url));
          }
        }, weak_factory_.GetWeakPtr(), std::move(target)));
  }

  void DocumentOnLoadCompletedInPrimaryMainFrame() override {
    if (navigation_reputation_probe_) {
      AdvanceNavigationReputationProbe();
      return;
    }
    if (probe_url_.is_valid()) {
      if (scriptlet_probe_) {
        MaybeCompleteScriptletProbe();
      } else if (fingerprint_probe_) {
        MaybeCompleteFingerprintStep();
      } else if (!console_replacement_url_.is_empty() &&
          contents_->GetLastCommittedURL() == console_replacement_url_) {
        CheckConsoleNavigation();
      } else {
        if (clipboard_probe_)
          MaybeCompleteClipboardProbe();
        else if (password_form_probe_)
          MaybeCompletePasswordFormProbe();
        else
          MaybeCompleteProfileProbe();
      }
      return;
    }
    if (!contents_->GetLastCommittedURL().SchemeIs("data"))
      return;

    const base::Process& renderer =
        contents_->GetPrimaryMainFrame()->GetProcess()->GetProcess();
    if (contents_->GetTitle() == u"Zitter Content Lab" && renderer.IsValid() &&
        renderer.Pid() != base::GetCurrentProcId()) {
      std::printf("Zitter Content renderer helper: %s\n",
                  base::GetProcessExecutablePath(renderer.Handle()).value().c_str());
      std::puts("Zitter Content Lab smoke passed");
    }
    if (quit_)
      quit_.Run();
  }

  void TitleWasSet(content::NavigationEntry*) override {
    if (navigation_reputation_probe_)
      return;
    if (scriptlet_probe_)
      MaybeCompleteScriptletProbe();
    else if (fingerprint_probe_)
      MaybeCompleteFingerprintStep();
    else if (clipboard_probe_)
      MaybeCompleteClipboardProbe();
    else if (password_form_probe_)
      MaybeCompletePasswordFormProbe();
    else if (probe_url_.is_valid())
      MaybeCompleteProfileProbe();
  }

  void MaybeCompleteScriptletProbe() {
    if (profile_probe_complete_ ||
        contents_->GetLastCommittedURL() != probe_url_) {
      return;
    }
    const auto& title = contents_->GetTitle();
    const bool passed = title == u"Zitter scriptlet probe passed";
    if (!passed &&
        (!title.starts_with(u"Zitter scriptlet probe failed ") ||
         title.size() > 192)) {
      return;
    }
    profile_probe_complete_ = true;
    if (passed)
      std::puts("Zitter scriptlet probe passed");
    else
      LOG(ERROR) << base::UTF16ToUTF8(title);
    if (quit_)
      quit_.Run();
  }

  void MaybeCompleteFingerprintStep() {
    if (fingerprint_step_ >= kFingerprintProbeSteps ||
        contents_->GetLastCommittedURL() != fingerprint_url_) {
      return;
    }
    const auto& title = contents_->GetTitle();
    if (!title.starts_with(kFingerprintResult) ||
        title.size() > kFingerprintResult.size() + 256) {
      return;
    }

    const auto result = base::UTF16ToUTF8(
        std::u16string_view(title).substr(kFingerprintResult.size()));
    std::printf("Zitter fingerprint step %d %s\n", fingerprint_step_,
                result.c_str());
    ++fingerprint_step_;
    if (fingerprint_step_ == kFingerprintProbeSteps) {
      quit_.Run();
      return;
    }

    GURL::Replacements replace;
    const std::string query = "step=" + std::to_string(fingerprint_step_);
    replace.SetQueryStr(query);
    fingerprint_url_ = probe_url_.ReplaceComponents(replace);
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&LabMainParts::LoadFingerprintStep,
                                 weak_factory_.GetWeakPtr()));
  }

  void LoadFingerprintStep() {
    if (fingerprint_step_ == 1)
      context_->SetFingerprintProtection(true);
    if (fingerprint_step_ == 3)
      prototype::FingerprintSession::For(*context_).Rotate();
    if (fingerprint_step_ == 4)
      context_->SetFingerprintProtection(false);
    contents_->GetController().LoadURLWithParams(
        content::NavigationController::LoadURLParams(fingerprint_url_));
  }

  void ClickClipboardProbe() {
    if (!contents_ || contents_->GetLastCommittedURL() != probe_url_ ||
        profile_probe_complete_) {
      return;
    }
    if (probe_url_.path() != "/clipboard/frame") {
      ClickProfileProbe();
      return;
    }
    auto* main_frame = contents_->GetPrimaryMainFrame();
    const auto expected_child = probe_url_.Resolve("/clipboard/child");
    main_frame->ForEachRenderFrameHost([&](content::RenderFrameHost* frame) {
      if (frame != main_frame && frame->GetLastCommittedURL() == expected_child) {
        contents_->Focus();
        frame->ExecuteJavaScriptWithUserGestureForTests(
            u"window.focus(); document.querySelector('button').focus();"
            u"document.querySelector('button').click();",
            {}, content::ISOLATED_WORLD_ID_GLOBAL);
      }
    });
  }

  void ObserveClipboardWrite(const content::ClipboardEndpoint& source,
                             const content::ClipboardPasteData& data) {
    auto* frame = contents_->GetPrimaryMainFrame();
    const bool paste_allowed = browser_->IsClipboardPasteAllowed(frame);
    contents_->UpdateWebContentsVisibility(content::Visibility::HIDDEN);
    const bool hidden_blocked = !browser_->IsClipboardPasteAllowed(frame);
    browser_->IsClipboardPasteAllowedByPolicy(
        source, source,
        {.format_type = ui::ClipboardFormatType::PlainTextType()}, data,
        base::BindOnce(
            [](base::WeakPtr<LabMainParts> self,
               std::optional<content::ClipboardPasteData> result) {
              if (self)
                self->clipboard_hidden_reply_denied_ = !result;
            },
            weak_factory_.GetWeakPtr()));
    contents_->UpdateWebContentsVisibility(content::Visibility::VISIBLE);
    clipboard_write_valid_ =
        !clipboard_write_seen_ && source.render_frame_host() == frame &&
        source.browser_context() == context_.get() &&
        data.text == u"zitter-clipboard-fixture" && paste_allowed && hidden_blocked &&
        clipboard_hidden_reply_denied_;
    clipboard_write_seen_ = true;
    MaybeCompleteClipboardProbe();
  }

  void MaybeCompleteClipboardProbe() {
    if (profile_probe_complete_ || contents_->GetLastCommittedURL() != probe_url_)
      return;
    const auto& title = contents_->GetTitle();
    const bool expect_write = probe_url_.path() == "/clipboard/primary";
    if (title == u"Zitter clipboard probe needs focus")
      contents_->Focus();
    if (title == u"Zitter clipboard probe ready" && !clipboard_probe_clicked_) {
      clipboard_probe_clicked_ = true;
      if (browser_->IsClipboardPasteAllowed(contents_->GetPrimaryMainFrame())) {
        LOG(ERROR) << "Clipboard paste was allowed before an interaction";
        profile_probe_complete_ = true;
        if (quit_)
          quit_.Run();
        return;
      }
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&LabMainParts::ClickClipboardProbe, weak_factory_.GetWeakPtr()),
          base::Milliseconds(250));
    }
    if (title != u"Zitter clipboard probe passed" &&
        title != u"Zitter clipboard probe failed") {
      return;
    }
    if (title == u"Zitter clipboard probe passed" && expect_write && !clipboard_write_seen_)
      return;
    profile_probe_complete_ = true;
    if (title == u"Zitter clipboard probe passed" &&
        clipboard_write_seen_ == expect_write &&
        (!expect_write || clipboard_write_valid_)) {
      std::puts("Zitter Content clipboard probe passed");
    } else {
      LOG(ERROR) << "Unexpected clipboard capability or renderer write";
    }
    if (quit_)
      quit_.Run();
  }

  void MaybeCompletePasswordFormProbe() {
    if (profile_probe_complete_ || contents_->GetLastCommittedURL() != probe_url_)
      return;
    const auto& title = contents_->GetTitle();
    if (title == u"Zitter password form probe ready" && !password_probe_clicked_) {
      password_probe_clicked_ = true;
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&LabMainParts::ClickProfileProbe,
                         weak_factory_.GetWeakPtr()),
          base::Milliseconds(400));
    }
    if (title != u"Zitter password form probe passed" &&
        title != u"Zitter password form probe failed") {
      return;
    }
    profile_probe_complete_ = true;
    if (title == u"Zitter password form probe passed" && password_probe_clicked_)
      std::puts("Zitter Content password form probe passed");
    else
      LOG(ERROR) << "Unexpected password autofill state";
    if (quit_)
      quit_.Run();
  }

  void ClickProfileProbe() {
    if (!contents_ || contents_->GetLastCommittedURL() != probe_url_ ||
        profile_probe_complete_) {
      return;
    }
    contents_->Focus();
    auto* widget = contents_->GetPrimaryMainFrame()->GetRenderWidgetHost();
    const auto origin = contents_->GetContainerBounds().origin();
    blink::WebMouseEvent click(blink::WebInputEvent::Type::kMouseMove, 0,
                               base::TimeTicks::Now());
    click.SetPositionInWidget(30, 24);
    click.SetPositionInScreen(origin.x() + 30, origin.y() + 24);
    widget->ForwardMouseEvent(click);
    click.SetType(blink::WebInputEvent::Type::kMouseDown);
    click.button = blink::WebMouseEvent::Button::kLeft;
    click.click_count = 1;
    click.SetModifiers(blink::WebInputEvent::kLeftButtonDown);
    widget->ForwardMouseEvent(click);
    click.SetType(blink::WebInputEvent::Type::kMouseUp);
    click.SetModifiers(0);
    widget->ForwardMouseEvent(click);
  }

  void MaybeCompleteProfileProbe() {
    if (profile_probe_complete_ || contents_->GetLastCommittedURL() != probe_url_)
      return;
    const auto& title = contents_->GetTitle();
    if (title != u"Zitter profile probe passed" &&
        title != u"Zitter profile probe failed") {
      return;
    }
    profile_probe_complete_ = true;
    if (title == u"Zitter profile probe failed") {
      LOG(ERROR) << "The profile probe saw unexpected storage or permissions";
      if (quit_)
        quit_.Run();
      return;
    }
    if (!password_probe_profile_.empty()) {
      RunPasswordProbe();
      return;
    }
    CompleteProfileProbeChecks();
  }

  void RunPasswordProbe() {
    auto* store = context_->GetPasswordStore();
    if (context_->IsOffTheRecord()) {
      CompletePasswordProbe(!store);
      return;
    }
    if (!store) {
      CompletePasswordProbe(false);
      return;
    }
    if (!write_password_probe_) {
      ReadPasswordProbe();
      return;
    }
    password_manager::StoredCredential login;
    login.url = probe_url_;
    login.signon_realm = url::Origin::Create(probe_url_).GetURL().spec();
    login.username_value = u"zitter-profile-probe";
    login.password_value = password_manager::PasswordString(ProbePassword());
    login.date_created = base::Time::Now();
    store->AddLogin(std::move(login),
                    base::BindOnce(&LabMainParts::ReadPasswordProbe,
                                   weak_factory_.GetWeakPtr()));
  }

  std::u16string ProbePassword() const {
    return password_probe_profile_ == "work" ? u"zitter-work-probe-secret"
                                             : u"zitter-personal-probe-secret";
  }

  void ReadPasswordProbe() {
    auto consumer = weak_factory_.GetWeakPtr();
    context_->GetPasswordStore()->GetAllLogins(
        base::WeakPtr<password_manager::PasswordStoreConsumer>(consumer));
  }

  void OnGetPasswordStoreResultsOrErrorFrom(
      password_manager::PasswordStoreInterface* store,
      base::expected<std::vector<password_manager::StoredCredential>,
                     password_manager::PasswordStoreBackendError> result)
      override {
    CompletePasswordProbe(
        store == context_->GetPasswordStore() && result && result->size() == 1 &&
        result->front().signon_realm ==
            url::Origin::Create(probe_url_).GetURL().spec() &&
        result->front().username_value == u"zitter-profile-probe" &&
        result->front().password_value == ProbePassword());
  }

  void CompletePasswordProbe(bool success) {
    if (!success) {
      LOG(ERROR) << "Unexpected Content profile password store state";
      if (quit_)
        quit_.Run();
      return;
    }
    std::puts("Zitter Content password store probe passed");
    CompleteProfileProbeChecks();
  }

  void CompleteProfileProbeChecks() {
    if (console_probe_) {
      console_reader_ =
          std::make_unique<prototype::DevToolsConsoleReader>(contents_.get(), 8);
      if (!console_reader_->Start()) {
        FailConsoleProbe("Could not attach to the test document");
        return;
      }
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE, base::BindOnce(&LabMainParts::ReadProbeConsole,
                                   weak_factory_.GetWeakPtr()),
          base::Milliseconds(650));
      return;
    }
    FlushProfileProbe();
  }

  void ReadProbeConsole() {
    const auto result = console_reader_->Result();
    const auto* entries = result.GetDict().FindList("entries");
    if (entries) {
      for (const auto& entry : *entries) {
        const auto* text = entry.GetDict().FindString("message");
        if (text && *text == "zitter-content-probe-console") {
          GURL::Replacements path;
          path.SetPathStr("/after-console");
          console_replacement_url_ = probe_url_.ReplaceComponents(path);
          contents_->GetController().LoadURLWithParams(
              content::NavigationController::LoadURLParams(
                  console_replacement_url_));
          return;
        }
      }
    }
    FailConsoleProbe("The expected buffered console event was absent");
  }

  void CheckConsoleNavigation() {
    if (!console_reader_)
      return;
    const auto result = console_reader_->Result();
    const auto* error = result.GetDict().FindString("error");
    if (!error || *error != "The approved document changed or closed") {
      FailConsoleProbe("A same-origin navigation did not invalidate the reader");
      return;
    }
    console_reader_.reset();
    std::puts("Zitter Content console probe passed");
    FlushProfileProbe();
  }

  void FailConsoleProbe(const char* message) {
    LOG(ERROR) << message;
    console_reader_.reset();
    if (quit_)
      quit_.Run();
  }

  void FlushProfileProbe() {
    auto* partition = context_->GetDefaultStoragePartition();
    partition->Flush();
    partition->GetCookieManagerForBrowserProcess()->FlushCookieStore(
        base::BindOnce(&LabMainParts::ProfileProbeFlushed,
                       weak_factory_.GetWeakPtr()));
  }

  void ProfileProbeFlushed() {
    std::puts("Zitter profile probe passed");
    if (quit_)
      quit_.Run();
  }

  std::optional<display::ScopedNativeScreen> screen_;
  base::ScopedTempDir directory_;
  const raw_ref<LabBrowserClientBase> browser_;
  OpenEncryptionCallback open_encryption_;
  raw_ptr<os_crypt_async::OSCryptAsync> os_crypt_ = nullptr;
  base::OnceCallback<void(ContinueWorkspaceStartup)> check_encryption_;
  base::File profile_lock_;
  base::FilePath persistent_path_;
  std::array<std::optional<bool>, 2> password_saving_defaults_ = {true, true};
  std::array<std::unique_ptr<PrefService>, 2> fingerprint_prefs_;
  std::array<std::unique_ptr<PrefService>, 2> page_zoom_prefs_;
  std::array<std::unique_ptr<ProfileContext::AdblockPreferences>, 2>
      adblock_prefs_;
  std::array<std::unique_ptr<PrefService>, 2> site_permission_prefs_;
  std::array<std::unique_ptr<PrefService>, 2> file_chooser_prefs_;
  base::FilePath workspace_root_;
  base::DictValue workspace_session_;
  std::unique_ptr<prototype::TimelineStore> workspace_timeline_;
  const bool ordinary_prepared_startup_;
  bool fresh_startup_ = false;
  std::string startup_import_probe_;
  GURL workspace_probe_url_;
  GURL probe_url_;
  GURL console_replacement_url_;
  std::string password_probe_profile_;
  bool write_password_probe_ = false;
  bool password_form_probe_ = false;
  bool clipboard_probe_ = false;
  bool clipboard_probe_clicked_ = false;
  bool clipboard_write_seen_ = false;
  bool clipboard_write_valid_ = false;
  bool clipboard_hidden_reply_denied_ = false;
  bool password_probe_clicked_ = false;
  GURL fingerprint_url_;
  int fingerprint_step_ = 0;
  std::unique_ptr<DownloadProbe> download_probe_;
  bool grant_storage_probe_ = false;
  bool profile_probe_complete_ = false;
  bool workspace_smoke_ = false;
  bool workspace_lab_ = false;
  bool workspace_keychain_ = false;
  bool workspace_mock_keychain_ = false;
  bool workspace_legacy_keychain_ = false;
  bool (*install_file_keychain_fixture_)(const char*) = nullptr;
  bool console_probe_ = false;
  bool fingerprint_probe_ = false;
  bool navigation_reputation_probe_ = false;
  bool reputation_provider_probe_ = false;
  bool reputation_navigation_valid_ = false;
  bool reputation_subframe_blocked_ = false;
  int reputation_probe_step_ = 0;
  bool scriptlet_probe_ = false;
  std::unique_ptr<ProfileContext> context_;
  std::unique_ptr<ProfileContext> personal_context_;
  std::vector<std::unique_ptr<WorkspaceProfileStartup>> additional_profile_startup_;
  std::vector<base::FilePath> workspace_extension_profiles_;
  std::vector<std::unique_ptr<ProfileContext>> additional_profiles_;
  std::unique_ptr<WorkspaceLab> workspace_;
  std::unique_ptr<MacUpdater> updater_;
  std::unique_ptr<StagedMacUpdate> staged_mac_update_;
  std::string staged_mac_update_version_;
  bool staging_mac_update_ = false;
  bool install_mac_update_on_exit_ = false;
  std::unique_ptr<content::WebContents> contents_;
  std::unique_ptr<prototype::DevToolsConsoleReader> console_reader_;
  NSWindow* __strong window_ = nil;
  ZitterContentWindowDelegate* __strong window_delegate_ = nil;
  base::RepeatingClosure quit_;
  base::WeakPtrFactory<LabMainParts> weak_factory_{this};
};

class LabBrowserClient final : public LabBrowserClientBase {
 public:
  std::unique_ptr<content::BrowserMainParts> CreateBrowserMainParts(
      bool) override {
    return std::make_unique<LabMainParts>(
        *this, base::BindOnce(&LabBrowserClient::OpenEncryption, base::Unretained(this)),
        base::BindOnce(&LabBrowserClient::CheckEncryption,
                       base::Unretained(this)),
        ordinary_prepared_startup_);
  }

  void SetOrdinaryPreparedStartup(bool enabled) {
    ordinary_prepared_startup_ = enabled;
  }

  std::string GetApplicationLocale() override { return "en-US"; }
  bool ShouldEnableStrictSiteIsolation() override { return true; }

  void ConfigureNetworkContextParams(
      content::BrowserContext* context,
      bool in_memory,
      const base::FilePath& relative_partition_path,
      network::mojom::NetworkContextParams* params,
      cert_verifier::mojom::CertVerifierCreationParams* cert_params) override {
    LabBrowserClientBase::ConfigureNetworkContextParams(
        context, in_memory, relative_partition_path, params, cert_params);
    if (in_memory || context->IsOffTheRecord())
      return;

    CHECK(cookie_encryption_);
    const auto network_path = static_cast<ProfileContext*>(context)->GetNetworkPath();
    CHECK(std::ranges::contains(network_paths_, network_path) ||
          (!network_root_.empty() && network_path.DirName() == network_root_ &&
           network_path.BaseName() == context->GetPath().BaseName()));
    if (!params->file_paths)
      params->file_paths = network::mojom::NetworkContextFilePaths::New();
    if (!relative_partition_path.empty()) {
      CHECK(!relative_partition_path.IsAbsolute() &&
            !relative_partition_path.ReferencesParent());
      params->file_paths->shared_dictionary_directory =
          network_path.AppendASCII("Shared Dictionary Partitions")
              .Append(relative_partition_path);
      return;
    }
    params->file_paths->shared_dictionary_directory =
        network_path.AppendASCII("Shared Dictionary");
    cookie_encryption_->Configure(*params);
    params->file_paths->data_directory = network_path;
    params->file_paths->cookie_database_name = base::FilePath("Cookies");
  }

  std::vector<base::FilePath> GetNetworkContextsParentDirectory() override {
    // The Mac network sandbox needs the database directory at process launch.
    // Keep the fixture key and other profile data outside that allowlist.
    auto paths = network_paths_;
    if (!network_root_.empty())
      paths.push_back(network_root_);
    return paths;
  }

  bool workspace_startup_failed() const {
    return workspace_startup_failed_;
  }

 private:
  os_crypt_async::OSCryptAsync* OpenEncryption(
      const base::FilePath& key_path,
      base::FilePath network_root, std::vector<base::FilePath> network_paths,
      std::vector<base::FilePath> login_paths, bool use_legacy_keychain) {
    if (use_legacy_keychain)
      cookie_encryption_ = CookieEncryptionMac::UseExistingLegacyKeychain();
    else if (key_path.empty())
      cookie_encryption_ = CookieEncryptionMac::UseKeychain();
    else
      cookie_encryption_ = CookieEncryptionMac::OpenFixture(key_path);
    if (!cookie_encryption_)
      return nullptr;
    network_paths_ = std::move(network_paths);
    network_root_ = std::move(network_root);
    if (use_legacy_keychain || ordinary_prepared_startup_)
      login_paths_ = std::move(login_paths);
    return cookie_encryption_->os_crypt();
  }

  void CheckEncryption(ContinueWorkspaceStartup done) {
    CHECK(cookie_encryption_);
    cookie_encryption_->CheckWorkspaceStores(
        network_paths_, login_paths_,
        base::BindOnce(&LabBrowserClient::OnCookieCheckComplete,
                       base::Unretained(this), std::move(done)));
  }

  void OnCookieCheckComplete(ContinueWorkspaceStartup done, bool verified) {
    workspace_startup_failed_ = !std::move(done).Run(verified);
  }

  std::vector<base::FilePath> network_paths_;
  std::vector<base::FilePath> login_paths_;
  base::FilePath network_root_;
  std::unique_ptr<CookieEncryptionMac> cookie_encryption_;
  bool workspace_startup_failed_ = false;
  bool ordinary_prepared_startup_ = false;
};

class LabMainDelegate final : public content::ContentMainDelegate {
 public:
  std::optional<int> BasicStartupComplete() override {
    const auto& command = *base::CommandLine::ForCurrentProcess();
    const bool ordinary_prepared_startup = UsesPreparedWorkspaceOnLaunch(command);
    if (auto result = CheckLabStartupFlags())
      return result;
    if (command.HasSwitch(switches::kProcessType))
      return std::nullopt;
    if (base::apple::BaseBundleID() == kExternalUrlProbeBundleId &&
        !ordinary_prepared_startup) {
      LOG(ERROR) << "Invalid isolated Content URL startup trial";
      return 1;
    }
    browser_client_.SetOrdinaryPreparedStartup(ordinary_prepared_startup);
    if (base::apple::BaseBundleID() == "dev.zitter.browser" &&
        !ordinary_prepared_startup) {
      // The production identity is available for isolated migration trials.
      // Keep this candidate out of ordinary startup and the system Keychain.
      const auto root = command.GetSwitchValuePath(kWorkspaceRoot);
      const bool disposable_workspace =
          command.HasSwitch(kWorkspaceLab) &&
          command.HasSwitch(kWorkspaceProbe) && IsProbeRoot(root);
      // The standalone probe accepts only loopback fixture URLs and never
      // opens the production workspace or account Keychain.
      const bool disposable_download =
          command.HasSwitch(kDownloadProbe) &&
          command.HasSwitch(kDownloadTransportProbe) &&
          !command.HasSwitch(kWorkspaceRoot) &&
          IsProbeRoot(command.GetSwitchValuePath(kProbeRoot));
      const bool ephemeral_smoke =
          command.HasSwitch(kWorkspaceSmokeTest) &&
          !command.HasSwitch(kWorkspaceRoot);
      const bool mock_workspace_keychain =
          disposable_workspace &&
          command.HasSwitch(os_crypt::switches::kUseMockKeychain);
      const bool file_keychain_fixture =
          disposable_workspace && command.HasSwitch(kWorkspaceKeychain) &&
          !mock_workspace_keychain && FindFileKeychainFixtureInstaller();
      if ((!disposable_workspace && !ephemeral_smoke && !disposable_download) ||
          (command.HasSwitch(kWorkspaceKeychain) && !mock_workspace_keychain &&
           !file_keychain_fixture) ||
          (command.HasSwitch(kWorkspaceLegacyKeychain) && !file_keychain_fixture) ||
          (command.HasSwitch(kOpenPreparedWorkspace) &&
           (!disposable_workspace || !command.HasSwitch(kWorkspaceKeychain) ||
            (!mock_workspace_keychain && !file_keychain_fixture))) ||
          command.HasSwitch(kProfileProbe)) {
        LOG(ERROR) << "Production identity requires an isolated Content trial";
        return 1;
      }
      [NSUserDefaults.standardUserDefaults
          registerDefaults:@{@"NSQuitAlwaysKeepsWindows" : @NO}];
      std::puts(disposable_download
                    ? "Zitter Content production identity isolated download trial"
                    : "Zitter Content production identity isolated Workspace trial");
    }
    ConfigureWorkspaceDesktopPicker();
    ordinary_prepared_startup_ = ordinary_prepared_startup;
    if (ordinary_prepared_startup_) {
      // Register extension schemes before Content initializes them. The
      // internal switch also selects the approved-package restore path and
      // propagates the renderer bindings to child processes.
      base::CommandLine::ForCurrentProcess()->AppendSwitch(kRestoreWorkspaceExtensions);
    }
    return std::nullopt;
  }

  void PreSandboxStartup() override {
    ui::ResourceBundle::InitSharedInstanceWithPakPath(
        base::apple::FrameworkBundlePath()
            .Append("Resources")
            .Append("zitter_content.pak"));
  }

  std::optional<int> PreBrowserMain() override {
    if (auto result = ConfigureExtensionProbes())
      return result;
    [ZitterContentApplication sharedApplication];
    CHECK([NSApp isKindOfClass:[ZitterContentApplication class]]);
    if (ordinary_prepared_startup_) {
      auto* app = static_cast<ZitterContentApplication*>(NSApp);
      app.delegate = app;
    }
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    return std::nullopt;
  }

  content::ContentClient* CreateContentClient() override { return &client_; }
  content::ContentBrowserClient* CreateContentBrowserClient() override {
    return &browser_client_;
  }
  content::ContentRendererClient* CreateContentRendererClient() override {
    return &renderer_client_;
  }
  content::ContentUtilityClient* CreateContentUtilityClient() override {
    return &utility_client_;
  }

  int WorkspaceExitCode(int result) const {
    if (result == 0 && browser_client_.workspace_startup_failed())
      return 1;
    return result;
  }

 private:
  bool ordinary_prepared_startup_ = false;
  LabContentClient client_;
  LabBrowserClient browser_client_;
  LabRendererClient renderer_client_;
  LabUtilityClient utility_client_;
};

}  // namespace
}  // namespace zitter::content_embedder

// The Chromium macOS launcher looks up this entry point after helper sandbox
// initialization. The browser and child processes load the same framework.
extern "C" __attribute__((visibility("default"))) int ContentMain(
    int argc,
    const char** argv) {
  zitter::content_embedder::SetBundlePaths();
  zitter::content_embedder::LabMainDelegate delegate;
  content::ContentMainParams params(&delegate);
  params.argc = argc;
  params.argv = argv;
  return delegate.WorkspaceExitCode(content::ContentMain(std::move(params)));
}
