#include "chromium/prototype/workspace_window.h"
#include "chromium/prototype/workspace_shell.h"

#include <memory>

#include "base/command_line.h"
#include "base/location.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/scoped_run_loop_timeout.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "components/input/native_web_keyboard_event.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/base_window.h"
#include "ui/base/test/ui_controls.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/events/keycodes/dom/dom_key.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/widget/widget.h"
#include "ui/views/controls/textfield/textfield.h"

namespace zitter::prototype {
class ZitterProfilesBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    // Persistent regular profiles inside the harness's isolated user-data-dir.
    auto* manager = g_browser_process->profile_manager();
    base::test::TestFuture<Profile*> work_ready;
    manager->CreateProfileAsync(manager->user_data_dir().AppendASCII("ZitterWork"),
                                work_ready.GetCallback());
    work_ = work_ready.Get();
    ASSERT_TRUE(work_);
    work_lease_ = std::make_unique<ScopedProfileKeepAlive>(
        work_, ProfileKeepAliveOrigin::kBrowserWindow);
    base::test::TestFuture<Profile*> personal_ready;
    manager->CreateProfileAsync(
        manager->user_data_dir().AppendASCII("ZitterPersonal"),
        personal_ready.GetCallback());
    personal_ = personal_ready.Get();
    ASSERT_TRUE(personal_);
    personal_lease_ = std::make_unique<ScopedProfileKeepAlive>(
        personal_, ProfileKeepAliveOrigin::kBrowserWindow);
    browser()->GetWindow()->Hide();  // Only the Zitter window is visible.
    window_ = std::make_unique<WorkspaceWindow>(work_, personal_);
    embedded_test_server()->ServeFilesFromSourceDirectory("zitter/testdata");
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  void TearDownOnMainThread() override {
    window_.reset();
    personal_ = nullptr;
    work_ = nullptr;
    personal_lease_.reset();
    work_lease_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
  }

 protected:
  void NavigateBoth(std::string_view path = "/storage_probe.html") {
    const auto url = embedded_test_server()->GetURL(path);
    ASSERT_TRUE(content::NavigateToURL(window_->contents("work"), url));
    ASSERT_TRUE(window_->Select("personal"));
    ASSERT_TRUE(content::NavigateToURL(window_->contents("personal"), url));
  }

  void SendPageShortcut(ui::KeyboardCode key, ui::DomCode code) {
    // SimulateKeyPress* deliberately sets skip_if_unhandled and bypasses
    // browser shortcut handling. Exercise the actual delegate path instead.
#if BUILDFLAG(IS_MAC)
    constexpr int modifiers = blink::WebInputEvent::kAltKey |
                              blink::WebInputEvent::kMetaKey;
#else
    constexpr int modifiers = blink::WebInputEvent::kAltKey |
                              blink::WebInputEvent::kControlKey;
#endif
    for (auto type : {blink::WebInputEvent::Type::kRawKeyDown,
                      blink::WebInputEvent::Type::kChar,
                      blink::WebInputEvent::Type::kKeyUp}) {
      input::NativeWebKeyboardEvent event(type, modifiers, base::TimeTicks::Now());
      event.windows_key_code = key;
      event.dom_code = static_cast<int>(code);
      event.dom_key = ui::DomKey::FromCharacter(key);
      event.text[0] = key;
      event.skip_if_unhandled = false;
      window_->contents(window_->active_id())->GetPrimaryMainFrame()
          ->GetRenderWidgetHost()->ForwardKeyboardEvent(event);
    }
  }

  void TypePageCharacter(content::WebContents* page, char16_t character) {
    content::SimulateEndOfPaintHoldingOnPrimaryMainFrame(page);
    content::InputEventAckWaiter ack(
        page->GetPrimaryMainFrame()->GetRenderWidgetHost(),
        blink::WebInputEvent::Type::kChar);
    content::SimulateCharTyped(page, character);
    ack.Wait();
  }

  raw_ptr<Profile> work_ = nullptr;
  raw_ptr<Profile> personal_ = nullptr;
  std::unique_ptr<ScopedProfileKeepAlive> work_lease_;
  std::unique_ptr<ScopedProfileKeepAlive> personal_lease_;
  std::unique_ptr<WorkspaceWindow> window_;
};

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, StorageIsolationAndSwitching) {
  ASSERT_NO_FATAL_FAILURE(NavigateBoth());
  auto* work = window_->contents("work");
  auto* personal = window_->contents("personal");
  EXPECT_EQ(work_.get(), work->GetBrowserContext());
  EXPECT_EQ(personal_.get(), personal->GetBrowserContext());
  EXPECT_NE(work->GetBrowserContext(), personal->GetBrowserContext());
  // Hidden native views may be detached on some platforms. Compare each host
  // while active, rather than imposing a native-parent contract on hidden views.
  ASSERT_TRUE(window_->Select("work"));
  const auto native_window = work->GetTopLevelNativeWindow();
  EXPECT_NE(gfx::NativeWindow(), native_window);
  ASSERT_TRUE(window_->Select("personal"));
  EXPECT_EQ(native_window, personal->GetTopLevelNativeWindow());
  EXPECT_FALSE(work_->IsOffTheRecord());
  EXPECT_FALSE(personal_->IsOffTheRecord());
  ASSERT_EQ("work", content::EvalJs(work, "writeMarker('work')"));
  ASSERT_EQ("personal", content::EvalJs(personal, "writeMarker('personal')"));
  for (int i = 0; i < 20; ++i) {
    ASSERT_TRUE(window_->Select("work"));
    EXPECT_EQ(content::Visibility::VISIBLE, work->GetVisibility());
    EXPECT_EQ(content::Visibility::HIDDEN, personal->GetVisibility());
    EXPECT_EQ("work|work|work|work", content::EvalJs(work, "readMarker()"));
    ASSERT_TRUE(window_->Select("personal"));
    EXPECT_EQ("personal|personal|personal|personal",
              content::EvalJs(personal, "readMarker()"));
  }
  EXPECT_FALSE(window_->Select("missing"));
  EXPECT_EQ("personal", window_->active_id());
  // Destroy and rebuild all WebContents in the same process. This checks host
  // teardown/recreation, not durability across a browser restart.
  window_.reset();
  window_ = std::make_unique<WorkspaceWindow>(work_, personal_);
  ASSERT_NO_FATAL_FAILURE(NavigateBoth());
  EXPECT_EQ("work|work|work|", content::EvalJs(window_->contents("work"), "readMarker()"));
  EXPECT_EQ("personal|personal|personal|",
            content::EvalJs(window_->contents("personal"), "readMarker()"));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, KeyboardWorkspaceSwitching) {
  ASSERT_NO_FATAL_FAILURE(NavigateBoth());
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(
      window_->widget()->GetNativeWindow()));
  for (auto id : {"work", "personal"}) {
    ASSERT_TRUE(window_->Select(id));
    auto* page = window_->contents(id);
    content::SimulateEndOfPaintHoldingOnPrimaryMainFrame(page);
    ASSERT_TRUE(content::ExecJs(page, R"JS(
      document.querySelector('#marker').focus();
      window.keyEvents = [];
      document.addEventListener('keydown', event => {
        keyEvents.push(event.key);
        if (event.altKey) event.preventDefault();
      });
    )JS"));
  }
  ASSERT_TRUE(window_->Select("work"));
  auto* work = window_->contents("work");
  work->Focus();
  ASSERT_EQ("marker", content::EvalJs(work, "document.activeElement.id"));
  // Prove that input reaches the renderer before testing the shell shortcut.
  TypePageCharacter(work, u'x');
  ASSERT_EQ("x", content::EvalJs(work, "document.querySelector('#marker').value"));
  SendPageShortcut(ui::VKEY_2, ui::DomCode::DIGIT2);
  ASSERT_EQ("personal", window_->active_id());
  auto* personal = window_->contents("personal");
  ASSERT_EQ("marker", content::EvalJs(personal, "document.activeElement.id"));
  TypePageCharacter(personal, u'y');
  ASSERT_EQ("y", content::EvalJs(personal, "document.querySelector('#marker').value"));
  SendPageShortcut(ui::VKEY_1, ui::DomCode::DIGIT1);
  ASSERT_EQ("work", window_->active_id());
  EXPECT_EQ("x", content::EvalJs(work, "document.querySelector('#marker').value"));
  EXPECT_EQ(false, content::EvalJs(work, "keyEvents.includes('2')"));
  EXPECT_EQ(false, content::EvalJs(personal, "keyEvents.includes('1')"));
}

#if BUILDFLAG(IS_MAC)
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, NativeKeyboardWorkspaceSwitching) {
  ASSERT_NO_FATAL_FAILURE(NavigateBoth());
  ui_controls::EnableUIControls();
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(
      window_->widget()->GetNativeWindow()));
  ASSERT_TRUE(window_->widget()->IsActive());
  ASSERT_TRUE(window_->Select("work"));
  window_->FocusAddressBar();
  const auto native_window = window_->widget()->GetNativeWindow();
  auto switch_workspace = [&](ui::KeyboardCode key, std::string_view expected) {
    const auto target = window_->shell()->address_visible()
        ? window_->shell()->address_widget()->GetNativeWindow() : native_window;
    return ui_test_utils::SendKeyPressToWindowSync(
               target, key, false, false, true, true) &&
           base::test::RunUntil([&] { return window_->active_id() == expected; });
  };
  ASSERT_TRUE(switch_workspace(ui::VKEY_2, "personal"));
  ASSERT_EQ("personal", window_->active_id());
  auto* personal = window_->contents("personal");
  ASSERT_TRUE(content::ExecJs(personal,
      "document.querySelector('#marker').focus()"));
  content::SimulateEndOfPaintHoldingOnPrimaryMainFrame(personal);
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(
      native_window, ui::VKEY_Y, false, false, false, false));
  ASSERT_EQ("y", content::EvalJs(personal, "document.querySelector('#marker').value"));
  ASSERT_EQ("marker", content::EvalJs(personal, "document.activeElement.id"));
  ASSERT_TRUE(switch_workspace(ui::VKEY_1, "work"));
  ASSERT_EQ("work", window_->active_id());
  auto* work = window_->contents("work");
  ASSERT_TRUE(content::ExecJs(work,
      "document.querySelector('#marker').focus()"));
  content::SimulateEndOfPaintHoldingOnPrimaryMainFrame(work);
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(
      native_window, ui::VKEY_X, false, false, false, false));
  ASSERT_EQ("x", content::EvalJs(work, "document.querySelector('#marker').value"));
  ASSERT_TRUE(switch_workspace(ui::VKEY_2, "personal"));
  ASSERT_EQ("personal", window_->active_id());
  EXPECT_EQ("y", content::EvalJs(personal, "document.querySelector('#marker').value"));
  for (int i = 0; i < 3; ++i) {
    ASSERT_EQ("marker", content::EvalJs(personal, "document.activeElement.id"));
    ASSERT_TRUE(switch_workspace(ui::VKEY_1, "work"));
    ASSERT_EQ("marker", content::EvalJs(work, "document.activeElement.id"));
    EXPECT_EQ("x", content::EvalJs(work, "document.querySelector('#marker').value"));
    ASSERT_TRUE(switch_workspace(ui::VKEY_2, "personal"));
    EXPECT_EQ("y", content::EvalJs(personal, "document.querySelector('#marker').value"));
  }
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, NativeShellAndAppearance) {
  ASSERT_NO_FATAL_FAILURE(NavigateBoth());
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->shell()->appearance_ready(); }));
  ASSERT_TRUE(window_->Select("work"));
  ui_controls::EnableUIControls();
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(window_->widget()->GetNativeWindow()));
  auto* original = window_->contents("work");
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(
      window_->widget()->GetNativeWindow(), ui::VKEY_L, false, false, false, true));
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->shell()->address_visible(); }));
  EXPECT_EQ(original, window_->contents("work"));
  auto address_window = window_->shell()->address_widget()->GetNativeWindow();
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(
      window_->widget()->GetNativeWindow(), ui::VKEY_X, false, false, false, false));
  EXPECT_EQ(u"x", window_->shell()->address_input()->GetText());
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(
      address_window, ui::VKEY_A, false, false, false, true));
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(
      address_window, ui::VKEY_Y, false, false, false, false));
  EXPECT_EQ(u"y", window_->shell()->address_input()->GetText());
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(
      window_->shell()->address_widget()->GetNativeWindow(), ui::VKEY_ESCAPE,
      false, false, false, false));
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->shell()->address_visible(); }));
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(
      window_->widget()->GetNativeWindow(), ui::VKEY_T, false, false, false, true));
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->shell()->address_visible(); }));
  auto* shell = window_->shell();
  shell->address_input()->SetText(u"file:///tmp/should-not-navigate");
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(shell->address_widget()->GetNativeWindow(),
      ui::VKEY_RETURN, false, false, false, false));
  EXPECT_TRUE(shell->address_visible());
  EXPECT_EQ(1u, window_->tab_count("work"));
  const auto next_url = embedded_test_server()->GetURL("/storage_probe.html?second");
  shell->address_input()->SetText(base::UTF8ToUTF16(next_url.spec()));
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(shell->address_widget()->GetNativeWindow(),
      ui::VKEY_RETURN, false, false, false, false));
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->tab_count("work") == 2; }));
  ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));
  EXPECT_EQ(next_url, window_->contents("work")->GetLastCommittedURL());
  EXPECT_EQ(work_.get(), window_->contents("work")->GetBrowserContext());
  EXPECT_NE(original, window_->contents("work"));
  window_->SelectTab(0);
  EXPECT_EQ(original, window_->contents("work"));
  shell->ShowSidebar();
  ASSERT_TRUE(shell->sidebar_visible());
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->sidebar_widget()->GetNativeWindow()));
  shell->color_input()->RequestFocus();
  const auto original_color = shell->workspace_color("work");
  shell->color_input()->SetText(u"invalid");
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(shell->sidebar_widget()->GetNativeWindow(),
      ui::VKEY_RETURN, false, false, false, false));
  EXPECT_EQ(original_color, shell->workspace_color("work"));
  shell->color_input()->SetText(u"#D49771");
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(shell->sidebar_widget()->GetNativeWindow(),
      ui::VKEY_RETURN, false, false, false, false));
  EXPECT_EQ(0xFFD49771u, shell->workspace_color("work"));
  shell->SetWorkspaceColor("personal", 0xFF73A4C2);
  shell->SetPinned(true);
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->appearance_saved(); }));
  window_.reset();
  window_ = std::make_unique<WorkspaceWindow>(work_, personal_);
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->shell()->appearance_ready(); }));
  EXPECT_EQ(0xFFD49771u, window_->shell()->workspace_color("work"));
  EXPECT_EQ(0xFF73A4C2u, window_->shell()->workspace_color("personal"));
  EXPECT_TRUE(window_->shell()->pinned());
  EXPECT_TRUE(window_->shell()->sidebar_visible());
  window_->widget()->SetBounds(gfx::Rect(100, 100, 980, 720));
  EXPECT_EQ(window_->widget()->GetClientAreaBoundsInScreen().x() + 8,
            window_->shell()->sidebar_widget()->GetWindowBoundsInScreen().x());
}
#endif

// Explicitly opt in: no unattended infinite wait in the normal test suite.
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, ManualWindow) {
  if (!base::CommandLine::ForCurrentProcess()->HasSwitch("zitter-interactive")) {
    GTEST_SKIP() << "Pass --zitter-interactive for the manual window";
  }
  ASSERT_NO_FATAL_FAILURE(NavigateBoth("/start.html"));
  ASSERT_TRUE(window_->Select("work"));
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(
      window_->widget()->GetNativeWindow()));
  window_->FocusAddressBar();
  base::RunLoop loop;
  base::test::ScopedRunLoopTimeout manual_timeout(FROM_HERE, base::Hours(1));
  window_->SetCloseCallback(loop.QuitClosure());
  loop.Run();
}
}  // namespace zitter::prototype
