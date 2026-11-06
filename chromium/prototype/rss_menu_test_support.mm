// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#import <AppKit/AppKit.h>
#include "chromium/app/app_shell_mac.h"
#include "chromium/prototype/workspace_keyboard_mac.h"
#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/functional/bind.h"
namespace zitter::prototype {
base::ScopedClosureRunner InstallReaderMenuForTesting(base::RepeatingClosure close) {
  class Session final : public AppShellDelegate {
   public:
    explicit Session(base::RepeatingClosure close) : previous_(NSApp.mainMenu), close_(std::move(close)) { SetAppShellDelegate(this); }
    ~Session() override { SetAppShellDelegate(nullptr); NSApp.mainMenu = previous_; }
    void RaiseWindow() override {}
    void RequestQuit() override {}
    void OpenUrl(const GURL&) override {}
    void RunCommand(AppCommand command) override { if (command == AppCommand::kCloseTab) close_.Run(); }
   private:
    __strong NSMenu* previous_;
    base::RepeatingClosure close_;
  };
  return base::ScopedClosureRunner(base::BindOnce([](std::unique_ptr<Session>) {}, std::make_unique<Session>(std::move(close))));
}
// Exercise the actual AppKit item/target/selector before returning the menu to
// the browser test harness. The production Runtime receives the same kRss tag.
bool DispatchRssMenuForTesting(const std::vector<Shortcut>& shortcuts,
                               base::RepeatingClosure action) {
  class Delegate final : public AppShellDelegate {
   public:
    explicit Delegate(base::RepeatingClosure action) : action_(std::move(action)) {}
    void RaiseWindow() override {}
    void RequestQuit() override {}
    void OpenUrl(const GURL&) override {}
    void RunCommand(AppCommand command) override { if (command == AppCommand::kRss) { dispatched = true; action_.Run(); } }
    bool dispatched = false;
   private:
    base::RepeatingClosure action_;
  } delegate(std::move(action));
  NSMenu* previous = NSApp.mainMenu;
  SetAppShellDelegate(&delegate);
  UpdateWorkspaceMenuShortcuts(shortcuts);
  auto find = [&](auto&& self, NSMenu* menu) -> NSMenuItem* {
    for (NSMenuItem* item in menu.itemArray) {
      if (item.submenu) { if (auto* found = self(self, item.submenu)) return found; }
      if (item.target && item.tag == static_cast<NSInteger>(AppCommand::kRss)) return item;
    }
    return nil;
  };
  if (auto* item = find(find, NSApp.mainMenu))
    [NSApp sendAction:item.action to:item.target from:item];
  SetAppShellDelegate(nullptr);
  NSApp.mainMenu = previous;
  return delegate.dispatched;
}
}
