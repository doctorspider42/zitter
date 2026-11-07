#import <AppKit/AppKit.h>

#include "chromium/app/app_shell_mac.h"

#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/test/test_future.h"
#include "chrome/browser/devtools/devtools_toggle_action.h"
#include "chrome/browser/devtools/devtools_window.h"
#include "chrome/browser/devtools/devtools_window_testing.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "ui/base/base_window.h"

namespace zitter {
namespace {
class DevToolsMenuDelegate final : public AppShellDelegate {
 public:
  explicit DevToolsMenuDelegate(content::WebContents* page)
      : page_(page), previous_menu_(NSApp.mainMenu) {
    InstallAppShell();
    SetAppShellDelegate(this);
  }
  ~DevToolsMenuDelegate() override {
    SetAppShellDelegate(nullptr);
    NSApp.mainMenu = previous_menu_;
  }
  void RaiseWindow() override {}
  void RequestQuit() override {}
  void OpenUrl(const GURL&) override {}
  void RunCommand(AppCommand command) override {
    if (command != AppCommand::kDevTools) return;
    DevToolsWindow::OpenDevToolsWindow(
        page_, DevToolsToggleAction::Show(),
        DevToolsOpenedByAction::kMainMenuOrMainShortcut);
  }

 private:
  raw_ptr<content::WebContents> page_;
  __strong NSMenu* previous_menu_;
};

NSMenuItem* FindDevToolsCommand(NSMenu* menu) {
  for (NSMenuItem* item in menu.itemArray) {
    if (item.submenu) {
      if (auto* found = FindDevToolsCommand(item.submenu)) return found;
    }
    if (item.target && item.tag == static_cast<NSInteger>(AppCommand::kDevTools))
      return item;
  }
  return nil;
}

using ZitterAppShellBrowserTest = InProcessBrowserTest;

// Workspace browser tests normally keep Chromium's menu and AppController.
// Use the real Zitter menu/hooks here: showing an undocked frontend must not
// ask AppController to create a bookmark bridge for an absent submenu.
IN_PROC_BROWSER_TEST_F(ZitterAppShellBrowserTest, DevToolsWithZitterMainMenu) {
  auto page = content::WebContents::Create(
      content::WebContents::CreateParams(browser()->GetProfile()));
  ASSERT_TRUE(content::NavigateToURL(page.get(), GURL("about:blank")));
  DevToolsMenuDelegate delegate(page.get());
  EXPECT_FALSE([NSApp.mainMenu itemWithTag:IDC_BOOKMARKS_MENU]);
  auto* item = FindDevToolsCommand(NSApp.mainMenu);
  ASSERT_TRUE(item);
  for (int attempt = 0; attempt < 2; ++attempt) {
    DevToolsWindowCreationObserver created;
    ASSERT_TRUE([NSApp sendAction:item.action to:item.target from:item]);
    auto* tools = DevToolsWindow::GetInstanceForInspectedWebContents(page.get());
    ASSERT_TRUE(tools);
    base::test::TestFuture<void> loaded;
    tools->SetLoadCompletedCallback(loaded.GetCallback());
    ASSERT_TRUE(loaded.Wait());
    EXPECT_FALSE(DevToolsWindow::GetInTabWebContents(page.get(), nullptr));
    // Explicit notification makes the regression deterministic even when the
    // test runner cannot activate windows on the desktop.
    auto* tools_browser = DevToolsWindowTesting::Get(tools)->browser();
    ASSERT_TRUE(tools_browser);
    NSWindow* window = tools_browser->GetWindow()->GetNativeWindow().GetNativeNSWindow();
    ASSERT_TRUE(window);
    [NSNotificationCenter.defaultCenter
        postNotificationName:NSWindowDidBecomeMainNotification object:window];
    EXPECT_EQ(2, content::EvalJs(tools->GetDevToolsWebContents().get(), "1 + 1"));
    created.CloseAllSync();
    base::RunLoop().RunUntilIdle();
    EXPECT_FALSE(DevToolsWindow::GetInstanceForInspectedWebContents(page.get()));
  }
}
}  // namespace
}  // namespace zitter
