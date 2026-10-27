#include "ui/views/controls/progress_bar.h"
#include "ui/views/controls/separator.h"
#include "ui/views/background.h"
#include "ui/views/layout/layout_provider.h"
#include "ui/views/bubble/bubble_dialog_model_host.h"
#include "ui/views/window/dialog_client_view.h"
#include "chromium/prototype/browser_data.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "chromium/prototype/sidebar_layout.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/border.h"
#include "chromium/prototype/browser_import_data.h"
#include "ui/base/clipboard/test/test_clipboard.h"
#include "ui/base/ime/text_edit_commands.h"
#include "ui/views/controls/textfield/text_edit_kill_buffer.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/dragdrop/drop_target_event.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/views/drag_controller.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/compositor/compositor.h"
#include "crypto/aes_cbc.h"
#include "crypto/hash.h"
#include "net/cookies/cookie_options.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "content/public/browser/storage_partition.h"
#include "chromium/prototype/workspace_window.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/spare_render_process_host_manager.h"
#include "chromium/prototype/workspace_shell.h"
#include "chromium/prototype/workspace_keyboard_mac.h"
#include "chromium/prototype/browser_preferences.h"
#include "components/prefs/testing_pref_service.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_registry_simple.h"
#include "url/origin.h"
#include "chromium/prototype/arc_importer.h"
#include "base/test/scoped_path_override.h"
#include "ui/compositor/layer.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/scrollbar/scroll_bar.h"
#include "ui/gfx/geometry/insets_f.h"
#include "ui/events/gesture_event_details.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/test/combobox_test_api.h"
#include "chromium/prototype/page_services.h"
#include "chromium/prototype/download_panel.h"
#include "chrome/browser/download/download_prefs.h"
#include "content/public/browser/download_manager_delegate.h"
#include "content/public/browser/download_request_utils.h"
#include "net/test/embedded_test_server/controllable_http_response.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "ui/display/screen.h"
#include "ui/display/display.h"
#include "base/uuid.h"
#include "content/public/browser/storage_partition_config.h"
#include "chromium/prototype/page_dialogs.h"
#include "chromium/prototype/link_navigation.h"
#include "content/public/test/url_loader_interceptor.h"
#include "content/public/test/test_navigation_observer.h"
#include "base/functional/callback_helpers.h"
#include "ui/native_theme/native_theme.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/scoped_animation_duration_scale_mode.h"
#include "chrome/browser/chrome_content_browser_client.h"
#include "content/public/common/content_client.h"
#include "content/public/browser/navigation_throttle_registry.h"
#include "net/dns/mock_host_resolver.h"
#include "base/synchronization/lock.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/password_manager/factories/profile_password_store_factory.h"
#include "components/password_manager/core/browser/password_store/password_store_consumer.h"
#include "components/password_manager/core/browser/password_store/password_store_interface.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_types.h"
#include "components/history/core/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/autofill/content/browser/content_autofill_client.h"
#include "components/autofill/core/browser/payments/payments_autofill_client.h"
#include "components/autofill/core/browser/suggestions/suggestion.h"
#include "components/password_manager/core/common/password_manager_pref_names.h"
#include "chrome/common/pref_names.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/ui/zoom/chrome_zoom_level_prefs.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "ui/views/controls/button/toggle_button.h"
#include "net/test/embedded_test_server/http_response.h"
#include "ui/views/window/dialog_delegate.h"

#include <memory>
#include <algorithm>
#include "base/timer/elapsed_timer.h"
#include "base/strings/string_number_conversions.h"
#include "base/logging.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "ui/views/view_tracker.h"
#include "ui/views/scoped_paint_lock.h"
#include "ui/views/controls/webview/webview.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/process/process_metrics.h"
#include "base/scoped_observation.h"
#include "chrome/browser/profiles/profile_manager_observer.h"

#include "base/command_line.h"
#include "base/i18n/case_conversion.h"
#include "base/location.h"
#include "base/base_paths.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/path_service.h"
#include "base/strings/string_util.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
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
#include "content/public/browser/render_widget_host_view.h"
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
#include "ui/views/style/platform_style.h"
#include "ui/views/interaction/element_tracker_views.h"
#include "ui/views/controls/menu/menu_controller.h"
#include "ui/base/interaction/element_tracker.h"
#include "ui/views/controls/label.h"
#include "ui/views/test/views_drawing_test_utils.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/views/controls/resize_area.h"
#include "ui/views/view_tracker.h"
#include "ui/base/cursor/cursor.h"
#include "ui/base/ime/composition_text.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/combobox/combobox.h"
#include "ui/base/ime/input_method.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/views/view.h"
#include "ui/gfx/geometry/point_conversions.h"

namespace zitter::prototype {
namespace {
void ActivateSidebarRow(views::View* view){ASSERT_TRUE(view);ui::AXActionData action;action.action=ax::mojom::Action::kDoDefault;EXPECT_TRUE(view->HandleAccessibleAction(action));}

void ClickControl(views::View* view){ASSERT_TRUE(view);if(auto* button=views::AsViewClass<views::Button>(view)){views::test::ButtonTestApi(button).NotifyDefaultMouseClick();return;}ui::AXActionData action;action.action=ax::mojom::Action::kDoDefault;view->HandleAccessibleAction(action);}

class RoutingTestBrowserClient : public ChromeContentBrowserClient {
 public:
  void CreateThrottlesForNavigation(content::NavigationThrottleRegistry& registry) override {
    LinkNavigation::CreateThrottle(registry);
    ChromeContentBrowserClient::CreateThrottlesForNavigation(registry);
  }
};
views::View* FindButton(views::View* root, std::u16string_view text) {
  if (auto* button = views::AsViewClass<views::MdTextButton>(root)) {
    if (button->GetText() == text) return button;
  }
  ui::AXNodeData data;root->GetViewAccessibility().GetAccessibleNodeData(&data);
  if(data.role==ax::mojom::Role::kButton&&data.GetString16Attribute(ax::mojom::StringAttribute::kName)==text)return root;
  for (const auto& child : root->children()) {
    if (auto* button = FindButton(child.get(), text)) return button;
  }
  return nullptr;
}
}  // namespace

class ZitterProfilesBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    routing_client_ = std::make_unique<RoutingTestBrowserClient>();
    old_client_ = content::SetBrowserClientForTesting(routing_client_.get());
    host_resolver()->AddRule("*.test", "127.0.0.1");
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
    {
      base::ScopedAllowBlockingForTesting allow;
      const auto config = manager->user_data_dir().AppendASCII("ZitterWorkspaces.json");
      if (!base::PathExists(config)) ASSERT_TRUE(base::WriteFile(config, R"({
        "profiles":[
          {"id":"work-profile","name":"Profil firmowy","directory":"ZitterWork"},
          {"id":"personal-profile","name":"Profil osobisty","directory":"ZitterPersonal"}],
        "workspaces":[
          {"id":"work","name":"Praca","profile_id":"work-profile"},
          {"id":"personal","name":"Prywatne","profile_id":"personal-profile"}],
        "active":"work","onboarding_complete":true
      })"));
    }
    window_ = std::make_unique<WorkspaceWindow>(work_, personal_);
    ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_ready(); }));
#if BUILDFLAG(IS_MAC)
    // This executable is unbundled. Merely showing a widget does not reliably
    // activate AppKit; native modal-close animations can otherwise stall. Use
    // real foreground/key-window activation, keeping native animations enabled.
    ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(window_->widget()->GetNativeWindow()));
#endif
    embedded_test_server()->ServeFilesFromSourceDirectory("zitter/testdata");
    embedded_test_server()->RegisterRequestMonitor(base::BindRepeating([](ZitterProfilesBrowserTest* self, const net::test_server::HttpRequest& request) {
      if (request.relative_url != "/storage_probe.html?routed") return;
      base::AutoLock guard(self->routing_lock_);
      const auto cookie = request.headers.find("Cookie");
      self->routing_cookies_.push_back(cookie == request.headers.end() ? "" : cookie->second);
    }, base::Unretained(this)));
    embedded_test_server()->RegisterRequestHandler(base::BindRepeating([](ZitterProfilesBrowserTest* self, const net::test_server::HttpRequest& request)
        -> std::unique_ptr<net::test_server::HttpResponse> {
      if (request.relative_url == "/route-redirect") {
        auto response = std::make_unique<net::test_server::BasicHttpResponse>();
        response->set_code(net::HTTP_FOUND);
        response->AddCustomHeader("Location", self->embedded_test_server()->GetURL("client.test", "/storage_probe.html?redirect").spec());
        return response;
      }
      if (request.relative_url != "/login-success") return nullptr;
      auto response = std::make_unique<net::test_server::BasicHttpResponse>();
      response->set_code(net::HTTP_OK);
      response->set_content_type("text/html");
      response->set_content("<!doctype html><title>Zalogowano testowo</title><h1>Zalogowano testowo</h1>");
      return response;
    }, base::Unretained(this)));
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  void TearDownOnMainThread() override {
    window_.reset();
    EXPECT_TRUE(embedded_test_server()->ShutdownAndWaitUntilComplete());
    personal_ = nullptr;
    work_ = nullptr;
    personal_lease_.reset();
    work_lease_.reset();
    content::SetBrowserClientForTesting(old_client_);
    routing_client_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
  }

 protected:
  void SetPaletteQuery(std::u16string query) {
    auto* shell = window_->shell();
    auto* field=shell->address_input();
    const auto length=field->GetText().size();field->SelectRange(gfx::Range(0,length));
    if(query.empty()){if(length)ASSERT_TRUE(field->input_client()->DeleteRange(gfx::Range(0,length)));}
    else field->input_client()->InsertText(query,ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);
  }
  void ArmUnsaved(content::WebContents* page) {
    ASSERT_TRUE(content::ExecJs(page, "window.unsaved='draft'; window.onbeforeunload = e => { e.preventDefault(); e.returnValue=''; };"));
    content::SimulateMouseClickAt(page, 0, blink::WebMouseEvent::Button::kLeft, gfx::Point(12, 12));
    ASSERT_TRUE(base::test::RunUntil([&] { return page->NeedToFireBeforeUnload(); }));
  }
  void AnswerUnload(content::WebContents* page, bool accept) {
    auto* dialogs = PageDialogs::FromWebContents(page);
    ASSERT_TRUE(dialogs);
    ASSERT_TRUE(base::test::RunUntil([&] { return dialogs->dialog() != nullptr; }));
    ASSERT_TRUE(dialogs->HandleJavaScriptDialog(page, accept, nullptr));
  }
  void NavigateBoth(std::string_view path = "/storage_probe.html") {
    const auto url = embedded_test_server()->GetURL(path);
    ASSERT_TRUE(window_->Select("work"));
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

  base::Lock routing_lock_;
  std::vector<std::string> routing_cookies_ GUARDED_BY(routing_lock_);
  std::unique_ptr<RoutingTestBrowserClient> routing_client_;
  raw_ptr<content::ContentBrowserClient> old_client_ = nullptr;
  raw_ptr<Profile> work_ = nullptr;
  raw_ptr<Profile> personal_ = nullptr;
  std::unique_ptr<ScopedProfileKeepAlive> work_lease_;
  std::unique_ptr<ScopedProfileKeepAlive> personal_lease_;
  std::unique_ptr<WorkspaceWindow> window_;
};

namespace {
class PasswordReader final : public password_manager::PasswordStoreConsumer {
 public:
  using Result = base::expected<std::vector<password_manager::StoredCredential>, password_manager::PasswordStoreBackendError>;
  void OnGetPasswordStoreResultsOrErrorFrom(password_manager::PasswordStoreInterface*, Result result) override {
    ready.SetValue(std::move(result));
  }
  base::test::TestFuture<Result> ready;
  base::WeakPtrFactory<PasswordReader> weak_factory{this};
};
}

// Opt in: timing runs must not silently turn functional verification into a
// benchmark or use real browsing profiles. The Python runner preserves samples.
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, PerformanceBaseline) {
  const auto* command = base::CommandLine::ForCurrentProcess();
  if (!command->HasSwitch("zitter-benchmark")) GTEST_SKIP();
  int iterations = 30, tabs = 25;
  if (command->HasSwitch("zitter-perf-iterations"))
    ASSERT_TRUE(base::StringToInt(command->GetSwitchValueASCII("zitter-perf-iterations"), &iterations));
  if (command->HasSwitch("zitter-perf-tabs"))
    ASSERT_TRUE(base::StringToInt(command->GetSwitchValueASCII("zitter-perf-tabs"), &tabs));
  ASSERT_GE(iterations, 1); ASSERT_LE(iterations, 200);
  ASSERT_GE(tabs, 2); ASSERT_LE(tabs, 200);
  base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Seconds(30));
  ASSERT_TRUE(window_->Select("work"));
  const auto url = embedded_test_server()->GetURL("/performance_probe.html");
  ASSERT_TRUE(content::NavigateToURL(window_->contents("work"), url));
  for (int i = 1; i < tabs; ++i) {
    ASSERT_TRUE(window_->NavigateFromBar(url, true));
    ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));
  }

  class FirstPaint final : public content::WebContentsObserver {
   public:
    FirstPaint(content::WebContents* page, base::TimeTicks start)
        : content::WebContentsObserver(page), start_(start) {}
    void DidFirstVisuallyNonEmptyPaint() override {
      if (!elapsed) elapsed = base::TimeTicks::Now() - start_;
    }
    std::optional<base::TimeDelta> elapsed;
   private:
    const base::TimeTicks start_;
  };
  auto sample = [](const char* metric, int trial, double milliseconds) {
    LOG(INFO) << "ZITTER_PERF {\"metric\":\"" << metric << "\",\"trial\":"
              << trial << ",\"ms\":" << milliseconds << "}";
  };
  // Two warmups are explicitly excluded; these are warm-profile measurements,
  // not cold application startup or internet/DNS/TLS measurements.
  for (int trial = -2; trial < iterations; ++trial) {
    auto& spare_manager = content::SpareRenderProcessHostManager::Get();
    const auto spare_ids = spare_manager.GetSpareIds();
    size_t ready_matching_spares = 0;
    auto* context = window_->contents("work")->GetBrowserContext();
    for (const auto& spare : spare_manager.GetSpares())
      if (spare->GetBrowserContext() == context && spare->IsReady()) ++ready_matching_spares;
    const auto start = base::TimeTicks::Now();
    ASSERT_TRUE(window_->NavigateFromBar(url, true));
    const auto dispatched = base::TimeTicks::Now();
    FirstPaint paint(window_->contents("work"), start);
    ASSERT_TRUE(base::test::RunUntil([&] { return paint.elapsed.has_value(); }));
    ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));
    if (trial >= 0) {
      sample("new_tab_dispatch_ms", trial, (dispatched - start).InMillisecondsF());
      sample("new_tab_first_visual_paint_ms", trial, paint.elapsed->InMillisecondsF());
      const auto process_id = window_->contents("work")->GetPrimaryMainFrame()->GetProcess()->GetID();
      LOG(INFO) << "ZITTER_RENDERER {\"trial\":" << trial << ",\"spares_before\":" << spare_ids.size()
                << ",\"ready_matching_spares_before\":" << ready_matching_spares
                << ",\"used_spare\":" << (std::find(spare_ids.begin(), spare_ids.end(), process_id) != spare_ids.end() ? "true" : "false") << "}";
    }
    window_->CloseTab(tabs);
  }
  window_->SelectTab(1);
  auto* right_page = window_->contents("work");
  ASSERT_EQ(true, content::EvalJs(right_page,
      "new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(() => resolve(true))))"));
  for (int trial = -2; trial < iterations; ++trial) {
    window_->SelectTab(0);
    ASSERT_EQ(true, content::EvalJs(window_->contents("work"),
        "new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(() => resolve(true))))"));
    // Arm both before changing bounds/visibility, so sequential waits cannot
    // accidentally miss a frame that arrived while waiting for the other pane.
    content::RenderFrameSubmissionObserver left(window_->contents("work"));
    content::RenderFrameSubmissionObserver right(right_page);
    left.SetWaitForNextFrame(); right.SetWaitForNextFrame();
    const auto start = base::TimeTicks::Now();
    ASSERT_TRUE(window_->SplitTab(1));
    const auto dispatched = base::TimeTicks::Now();
    left.WaitForNextFrameSubmission(); right.WaitForNextFrameSubmission();
    const auto frames_ready = base::TimeTicks::Now();
    if (trial >= 0) {
      sample("split_dispatch_ms", trial, (dispatched - start).InMillisecondsF());
      sample("split_both_renderer_frames_ms", trial, (frames_ready - start).InMillisecondsF());
    }
    window_->Unsplit(0);
    // Let the full-width state reach the renderer before the next resize.
    ASSERT_EQ(true, content::EvalJs(window_->contents("work"),
        "new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(() => resolve(true))))"));
  }
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, PerformanceRestoration) {
  const auto* command = base::CommandLine::ForCurrentProcess();
  if (!command->HasSwitch("zitter-benchmark")) GTEST_SKIP();
  int iterations = 30, tabs = 25;
  if (command->HasSwitch("zitter-perf-iterations"))
    ASSERT_TRUE(base::StringToInt(command->GetSwitchValueASCII("zitter-perf-iterations"), &iterations));
  if (command->HasSwitch("zitter-perf-tabs"))
    ASSERT_TRUE(base::StringToInt(command->GetSwitchValueASCII("zitter-perf-tabs"), &tabs));
  ASSERT_GE(iterations, 1); ASSERT_LE(iterations, 200);
  ASSERT_GE(tabs, 2); ASSERT_LE(tabs, 200);
  base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Seconds(30));
  ASSERT_TRUE(window_->Select("work"));
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_saved(); }));
  base::DictValue session;
  for (const std::string id : {"work", "personal"}) {
    base::DictValue host;
    host.Set("profile_id", id + "-profile"); host.Set("active", 0);
    host.Set("left", 0); host.Set("right", id == "work" ? 1 : -1); host.Set("ratio", 0.5);
    base::ListValue pages;
    for (int i = 0; i < tabs; ++i) {
      base::DictValue page;
      page.Set("url", embedded_test_server()->GetURL(
          "/performance_probe.html?restore=" + id + std::to_string(i)).spec());
      page.Set("title", "Saved " + id + std::to_string(i)); pages.Append(std::move(page));
    }
    host.Set("tabs", std::move(pages)); session.Set(id, std::move(host));
  }
  std::string json;
  ASSERT_TRUE(base::JSONWriter::Write(session, &json));
  const auto directory = g_browser_process->profile_manager()->user_data_dir();
  for (int trial = -2; trial < iterations; ++trial) {
    window_.reset();
    base::ThreadPoolInstance::Get()->FlushForTesting();
    {
      base::ScopedAllowBlockingForTesting allow;
      ASSERT_TRUE(base::WriteFile(directory.AppendASCII("ZitterSession.json"), json));
      ASSERT_TRUE(base::WriteFile(directory.AppendASCII("ZitterTimeMachine.json"), "{}"));
    }
    const auto start = base::TimeTicks::Now();
    window_ = std::make_unique<WorkspaceWindow>(work_, personal_);
    ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_ready(); }));
    const auto ready = base::TimeTicks::Now();
    ASSERT_TRUE(window_->split());
    auto* left = window_->pane_contents(false);
    auto* right = window_->pane_contents(true);
    ASSERT_TRUE(left); ASSERT_TRUE(right);
    ASSERT_TRUE(base::test::RunUntil([&] {
      return left->CompletedFirstVisuallyNonEmptyPaint() && right->CompletedFirstVisuallyNonEmptyPaint();
    }));
    const auto painted = base::TimeTicks::Now();
    size_t live = 0;
    for (const auto& child : window_->shell()->content_host()->children()) {
      auto* view = views::AsViewClass<views::WebView>(child.get());
      if (view && view->web_contents()) ++live;
    }
    if (trial >= 0) {
      LOG(INFO) << "ZITTER_PERF {\"metric\":\"restore_ready_ms\",\"trial\":" << trial
                << ",\"ms\":" << (ready - start).InMillisecondsF() << "}";
      LOG(INFO) << "ZITTER_PERF {\"metric\":\"restore_both_first_paints_ms\",\"trial\":" << trial
                << ",\"ms\":" << (painted - start).InMillisecondsF() << "}";
    }
    ASSERT_TRUE(content::WaitForLoadStop(left));
    ASSERT_TRUE(content::WaitForLoadStop(right));
    ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_saved(); }));
    if (trial >= 0) {
      auto memory = base::ProcessMetrics::CreateCurrentProcessMetrics()->GetMemoryInfo();
      ASSERT_TRUE(memory.has_value());
      uint64_t footprint = 0;
#if BUILDFLAG(IS_APPLE)
      footprint = memory->physical_footprint_bytes;
#endif
      LOG(INFO) << "ZITTER_RESOURCE {\"trial\":" << trial << ",\"realized_pages\":" << live
                << ",\"saved_tabs\":" << tabs * 2
                << ",\"browser_resident_bytes\":" << memory->resident_set_bytes
                << ",\"browser_physical_footprint_bytes\":" << footprint << "}";
    }
  }
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, PerformanceProfileStartup) {
  const auto* command = base::CommandLine::ForCurrentProcess();
  if (!command->HasSwitch("zitter-benchmark")) GTEST_SKIP();
  const bool with_page = command->HasSwitch("zitter-perf-profile-page");
  int iterations = 30, count = 4;
  if (command->HasSwitch("zitter-perf-iterations"))
    ASSERT_TRUE(base::StringToInt(command->GetSwitchValueASCII("zitter-perf-iterations"), &iterations));
  if (command->HasSwitch("zitter-perf-profiles"))
    ASSERT_TRUE(base::StringToInt(command->GetSwitchValueASCII("zitter-perf-profiles"), &count));
  ASSERT_GE(iterations, 1); ASSERT_LE(iterations, 200);
  ASSERT_GE(count, 2); ASSERT_LE(count, 16);
  base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Seconds(30));
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_saved(); }));
  auto* manager = g_browser_process->profile_manager();
  class Timings final : public ProfileManagerObserver {
   public:
    explicit Timings(const std::vector<base::FilePath>& paths) : paths_(paths) {}
    void OnProfileCreationStarted(Profile* profile) override {
      if (std::find(paths_.begin(), paths_.end(), profile->GetPath()) == paths_.end()) return;
      const auto now = base::TimeTicks::Now();
      if (first_start.is_null()) first_start = now;
      if (profile->GetPath() == paths_.back()) active_start = now;
    }
    void OnProfileAdded(Profile* profile) override {
      if (std::find(paths_.begin(), paths_.end(), profile->GetPath()) == paths_.end()) return;
      ++added; last_added = base::TimeTicks::Now();
    }
    base::TimeTicks first_start, active_start, last_added;
    size_t added = 0;
   private:
    const std::vector<base::FilePath> paths_;
  };
  for (int trial = -2; trial < iterations; ++trial) {
    window_.reset();
    base::ThreadPoolInstance::Get()->FlushForTesting();
    base::ListValue profiles, spaces;
    std::vector<base::FilePath> paths;
    for (int i = 0; i < count; ++i) {
      const auto directory = "ZitterProfile-" + base::Uuid::GenerateRandomV4().AsLowercaseString();
      const auto id = "cold-profile-" + std::to_string(i);
      paths.push_back(manager->user_data_dir().AppendASCII(directory));
      base::DictValue profile;
      profile.Set("id", id); profile.Set("name", "Cold " + std::to_string(i));
      profile.Set("directory", directory); profiles.Append(std::move(profile));
      base::DictValue space;
      space.Set("id", "cold-space-" + std::to_string(i)); space.Set("profile_id", id);
      space.Set("name", "Space " + std::to_string(i)); spaces.Append(std::move(space));
    }
    base::DictValue catalogue;
    catalogue.Set("profiles", std::move(profiles)); catalogue.Set("workspaces", std::move(spaces));
    catalogue.Set("active", "cold-space-" + std::to_string(count - 1));
    catalogue.Set("onboarding_complete", true);
    std::string json, session_json = "{}";
    ASSERT_TRUE(base::JSONWriter::Write(catalogue, &json));
    if (with_page) {
      base::DictValue session, host, tab;
      host.Set("profile_id", "cold-profile-" + std::to_string(count - 1));
      host.Set("active", 0); host.Set("left", 0); host.Set("right", -1);
      tab.Set("url", embedded_test_server()->GetURL("/performance_probe.html").spec());
      tab.Set("title", "Restored cold-profile page");
      base::ListValue tabs; tabs.Append(std::move(tab)); host.Set("tabs", std::move(tabs));
      session.Set("cold-space-" + std::to_string(count - 1), std::move(host));
      ASSERT_TRUE(base::JSONWriter::Write(session, &session_json));
    }
    {
      base::ScopedAllowBlockingForTesting allow;
      ASSERT_TRUE(base::WriteFile(manager->user_data_dir().AppendASCII("ZitterWorkspaces.json"), json));
      ASSERT_TRUE(base::WriteFile(manager->user_data_dir().AppendASCII("ZitterSession.json"), session_json));
      ASSERT_TRUE(base::WriteFile(manager->user_data_dir().AppendASCII("ZitterTimeMachine.json"), "{}"));
    }
    Timings timings(paths);
    base::ScopedObservation<ProfileManager, ProfileManagerObserver> observation(&timings);
    observation.Observe(manager);
    const auto start = base::TimeTicks::Now();
    window_ = std::make_unique<WorkspaceWindow>(work_, personal_);
    ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_ready(); }));
    const auto ready = base::TimeTicks::Now();
    base::TimeTicks painted;
    if (with_page) {
      auto* page = window_->contents(window_->active_id());
      ASSERT_TRUE(page);
      ASSERT_TRUE(base::test::RunUntil([&] { return page->CompletedFirstVisuallyNonEmptyPaint(); }));
      painted = base::TimeTicks::Now();
      ASSERT_TRUE(content::WaitForLoadStop(page));
    }
    ASSERT_TRUE(base::test::RunUntil([&] { return timings.added == static_cast<size_t>(count) && !window_->profiles_busy(); }));
    ASSERT_FALSE(timings.first_start.is_null()); ASSERT_FALSE(timings.active_start.is_null());
    if (trial >= 0) {
      if (with_page)
        LOG(INFO) << "ZITTER_PERF {\"metric\":\"profiles_first_visual_paint_ms\",\"trial\":" << trial
                  << ",\"ms\":" << (painted - start).InMillisecondsF() << "}";
      LOG(INFO) << "ZITTER_PERF {\"metric\":\"profiles_ready_ms\",\"trial\":" << trial
                << ",\"ms\":" << (ready - start).InMillisecondsF() << "}";
      LOG(INFO) << "ZITTER_PERF {\"metric\":\"profile_batch_ms\",\"trial\":" << trial
                << ",\"ms\":" << (timings.last_added - timings.first_start).InMillisecondsF() << "}";
      LOG(INFO) << "ZITTER_PERF {\"metric\":\"active_profile_delay_ms\",\"trial\":" << trial
                << ",\"ms\":" << (timings.active_start - timings.first_start).InMillisecondsF() << "}";
    }
    ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_saved(); }));
    if (trial >= 0) {
      auto memory = base::ProcessMetrics::CreateCurrentProcessMetrics()->GetMemoryInfo();
      ASSERT_TRUE(memory.has_value());
      uint64_t footprint = 0;
#if BUILDFLAG(IS_APPLE)
      footprint = memory->physical_footprint_bytes;
#endif
      size_t live = 0;
      for (const auto& child : window_->shell()->content_host()->children()) {
        auto* view = views::AsViewClass<views::WebView>(child.get());
        if (view && view->web_contents()) ++live;
      }
      LOG(INFO) << "ZITTER_RESOURCE {\"trial\":" << trial << ",\"realized_pages\":" << live
                << ",\"saved_tabs\":" << count
                << ",\"browser_resident_bytes\":" << memory->resident_set_bytes
                << ",\"browser_physical_footprint_bytes\":" << footprint << "}";
    }
  }
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, SidebarMetadataUpdatesPreserveRows) {
  ASSERT_TRUE(window_->Select("work"));
  const auto url = embedded_test_server()->GetURL("/performance_probe.html");
  ASSERT_TRUE(content::NavigateToURL(window_->contents("work"), url));
  ASSERT_TRUE(window_->NavigateFromBar(url, true));
  auto* right = window_->contents("work");
  ASSERT_TRUE(content::WaitForLoadStop(right));
  window_->SelectTab(0);
  auto* shell = window_->shell();
  views::ViewTracker first(shell->tab_button(0)), second(shell->tab_button(1));
  ASSERT_TRUE(first); ASSERT_TRUE(second);
  ASSERT_TRUE(content::ExecJs(right, "document.title='Background title'; history.pushState({}, '', '/updated-path');"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return second && second.view()->GetTooltipText() == u"Background title";
  }));
  ASSERT_TRUE(first); ASSERT_TRUE(second);
  EXPECT_EQ(first.view(), shell->tab_button(0));
  EXPECT_EQ(second.view(), shell->tab_button(1));
  ASSERT_TRUE(window_->SplitTab(1));
  views::ViewTracker split_left(shell->tab_button(0)), split_right(shell->tab_button(1));
  ASSERT_TRUE(content::ExecJs(right, "document.title='Split title';"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return split_right && split_right.view()->GetTooltipText() == u"Split title";
  }));
  ASSERT_TRUE(split_left); ASSERT_TRUE(split_right);
  EXPECT_EQ(split_left.view(), shell->tab_button(0));
  EXPECT_EQ(split_right.view(), shell->tab_button(1));
  shell->SetPinned(true);shell->ShowSidebar();shell->sidebar_widget()->LayoutRootViewIfNecessary();
  // The retained callbacks must still select the original pages.
  ActivateSidebarRow(shell->tab_button(0));
  EXPECT_NE(right, window_->contents("work"));
  ActivateSidebarRow(shell->tab_button(1));
  EXPECT_EQ(right, window_->contents("work"));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, TimeMachineFlushesPendingHistoryOnClose) {
  ASSERT_TRUE(window_->SaveSnapshot("Pending close snapshot"));
  EXPECT_FALSE(window_->configuration_saved());
  window_.reset();
  // Wait for the old window's BLOCK_SHUTDOWN writer, including the payload
  // explicitly flushed from its pending timer, before reopening the profile.
  base::ThreadPoolInstance::Get()->FlushForTesting();
  window_ = std::make_unique<WorkspaceWindow>(work_, personal_);
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_ready(); }));
  const auto history = window_->TimeMachine();
  EXPECT_TRUE(std::any_of(history.snapshots.begin(), history.snapshots.end(),
      [](const TimeMachineItem& item) { return item.title == u"Pending close snapshot"; }));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, RestoredTabsStayUnrealizedUntilShown) {
  ASSERT_TRUE(window_->Select("work"));
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_saved(); }));
  window_.reset();
  base::ThreadPoolInstance::Get()->FlushForTesting();
  auto make_host = [&](std::string profile, std::string label, int count, bool split) {
    base::DictValue host;
    host.Set("profile_id", profile); host.Set("active", 0); host.Set("left", 0);
    host.Set("right", split ? 1 : -1); host.Set("ratio", 0.55);
    base::ListValue tabs;
    for (int i = 0; i < count; ++i) {
      base::DictValue tab;
      tab.Set("url", embedded_test_server()->GetURL(
          "/performance_probe.html?lazy=" + label + std::to_string(i)).spec());
      tab.Set("title", "Saved " + label + std::to_string(i));
      tabs.Append(std::move(tab));
    }
    host.Set("tabs", std::move(tabs));
    return host;
  };
  base::DictValue session;
  session.Set("work", make_host("work-profile", "work", 6, true));
  session.Set("personal", make_host("personal-profile", "personal", 2, false));
  std::string json;
  ASSERT_TRUE(base::JSONWriter::Write(session, &json));
  {
    base::ScopedAllowBlockingForTesting allow;
    ASSERT_TRUE(base::WriteFile(g_browser_process->profile_manager()->user_data_dir()
        .AppendASCII("ZitterSession.json"), json));
  }
  window_ = std::make_unique<WorkspaceWindow>(work_, personal_);
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_ready(); }));
  auto live_pages = [&] {
    size_t count = 0;
    for (const auto& child : window_->shell()->content_host()->children()) {
      auto* view = views::AsViewClass<views::WebView>(child.get());
      if (view && view->web_contents()) ++count;
    }
    return count;
  };
  ASSERT_TRUE(window_->split());
  EXPECT_EQ(6u, window_->tab_count("work"));
  EXPECT_EQ(2u, window_->tab_count("personal"));
  EXPECT_EQ(2u, live_pages());  // Only the selected split has WebContents/clients.
  EXPECT_EQ(nullptr, window_->contents("personal"));
  ASSERT_TRUE(window_->SaveSnapshot("Unopened tabs retained"));
  const auto snapshot = window_->TimeMachine().snapshots.front().id;
  window_->SearchPalette(u"Saved");
  window_->CloseTab(5);  // Closing a dormant tab must not realize it.
  EXPECT_EQ(2u, live_pages());
  ASSERT_FALSE(window_->TimeMachine().closed.empty());
  EXPECT_EQ(u"Saved work5", window_->TimeMachine().closed.front().title);
  const auto closed = window_->TimeMachine().closed.front().id;
  window_->SelectTab(3);
  ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));
  EXPECT_EQ(3u, live_pages());
  EXPECT_EQ(work_, window_->contents("work")->GetBrowserContext());
  EXPECT_EQ(embedded_test_server()->GetURL("/performance_probe.html?lazy=work3"),
            window_->contents("work")->GetLastCommittedURL());
  EXPECT_NE(nullptr, PageServices::FromWebContents(window_->contents("work")));
  ASSERT_TRUE(window_->Select("personal"));
  ASSERT_TRUE(content::WaitForLoadStop(window_->contents("personal")));
  EXPECT_EQ(4u, live_pages());
  EXPECT_EQ(personal_, window_->contents("personal")->GetBrowserContext());
  ASSERT_TRUE(window_->Select("work"));
  ASSERT_TRUE(window_->RestoreSnapshot(snapshot));
  EXPECT_EQ(6u, window_->tab_count("work"));
  EXPECT_TRUE(window_->split());
  EXPECT_DOUBLE_EQ(0.55, window_->split_ratio());
  EXPECT_EQ(3u, live_pages());  // Personal page plus two new split pages.
  ASSERT_TRUE(window_->ReopenClosedTab(closed));
  ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));
  EXPECT_EQ(4u, live_pages());
  EXPECT_EQ(embedded_test_server()->GetURL("/performance_probe.html?lazy=work5"),
            window_->contents("work")->GetLastCommittedURL());
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_saved(); }));
  window_.reset();
  base::ThreadPoolInstance::Get()->FlushForTesting();
  window_ = std::make_unique<WorkspaceWindow>(work_, personal_);
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_ready(); }));
  EXPECT_EQ(7u, window_->tab_count("work"));
  EXPECT_EQ(1u, live_pages());  // The restored selection is outside its saved pair.
  EXPECT_EQ(nullptr, window_->contents("personal"));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, StartupPrioritizesActiveProfileWithoutReorderingCatalogue) {
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_saved(); }));
  window_.reset();
  base::ThreadPoolInstance::Get()->FlushForTesting();
  auto* manager = g_browser_process->profile_manager();
  std::vector<base::FilePath> paths;
  base::ListValue profiles, spaces;
  for (int i = 0; i < 4; ++i) {
    const auto directory = "ZitterProfile-" + base::Uuid::GenerateRandomV4().AsLowercaseString();
    const auto id = "cold-profile-" + std::to_string(i);
    paths.push_back(manager->user_data_dir().AppendASCII(directory));
    base::DictValue profile;
    profile.Set("id", id); profile.Set("name", "Cold " + std::to_string(i));
    profile.Set("directory", directory); profiles.Append(std::move(profile));
    base::DictValue space;
    space.Set("id", "cold-space-" + std::to_string(i));
    space.Set("profile_id", id); space.Set("name", "Space " + std::to_string(i));
    spaces.Append(std::move(space));
  }
  base::DictValue catalogue;
  catalogue.Set("profiles", std::move(profiles)); catalogue.Set("workspaces", std::move(spaces));
  catalogue.Set("active", "cold-space-3"); catalogue.Set("onboarding_complete", true);
  std::string json;
  ASSERT_TRUE(base::JSONWriter::Write(catalogue, &json));
  {
    base::ScopedAllowBlockingForTesting allow;
    ASSERT_TRUE(base::WriteFile(manager->user_data_dir().AppendASCII("ZitterWorkspaces.json"), json));
  }
  class CreationOrder final : public ProfileManagerObserver {
   public:
    explicit CreationOrder(const std::vector<base::FilePath>& expected) : expected_(expected) {}
    void OnProfileCreationStarted(Profile* profile) override {
      if (std::find(expected_.begin(), expected_.end(), profile->GetPath()) != expected_.end())
        started.push_back(profile->GetPath());
    }
    std::vector<base::FilePath> started;
   private:
    const std::vector<base::FilePath> expected_;
  } order(paths);
  base::ScopedObservation<ProfileManager, ProfileManagerObserver> observation(&order);
  observation.Observe(manager);
  window_ = std::make_unique<WorkspaceWindow>(work_, personal_);
  EXPECT_FALSE(window_->configuration_ready());
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_ready(); }));
  ASSERT_EQ(1u, order.started.size());
  EXPECT_TRUE(window_->profiles_busy());
  EXPECT_EQ(paths[3], order.started.front());
  EXPECT_EQ("cold-space-3", window_->active_id());
  EXPECT_EQ(paths[3], window_->contents(window_->active_id())->GetBrowserContext()->GetPath());
  // Management cannot invalidate pending profile assignments. Ordinary active
  // browsing and queued cross-profile intents remain available.
  EXPECT_FALSE(window_->RenameProfile("cold-profile-0", "Changed during startup"));
  EXPECT_FALSE(window_->AssignProfile("cold-space-0", "cold-profile-3"));
  EXPECT_FALSE(window_->RemoveWorkspace());
  const auto cancelled = embedded_test_server()->GetURL("/performance_probe.html?cancelled");
  const auto superseded = embedded_test_server()->GetURL("/performance_probe.html?superseded");
  const auto final_url = embedded_test_server()->GetURL("/performance_probe.html?final");
  EXPECT_FALSE(window_->Select("cold-space-0"));
  EXPECT_EQ("cold-space-3", window_->active_id());
  EXPECT_TRUE(window_->OpenLink(cancelled, "cold-space-0", true));
  EXPECT_TRUE(window_->Select("cold-space-3"));  // cancels pending navigation
  EXPECT_TRUE(window_->OpenLink(superseded, "cold-space-1", true));
  EXPECT_TRUE(window_->OpenLink(final_url, "cold-space-2", true));
  EXPECT_EQ("cold-space-3", window_->active_id());
  EXPECT_FALSE(window_->contents("cold-space-0"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window_->active_id() == "cold-space-2" &&
        window_->contents("cold-space-2")->GetLastCommittedURL() == final_url;
  }));
  EXPECT_EQ(paths[2], window_->contents("cold-space-2")->GetBrowserContext()->GetPath());
  EXPECT_TRUE(PageServices::FromWebContents(window_->contents("cold-space-2")));
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->profiles_busy(); }));
  EXPECT_FALSE(window_->contents("cold-space-0"));
  EXPECT_FALSE(window_->contents("cold-space-1"));
  EXPECT_TRUE(window_->contents("cold-space-3")->GetLastCommittedURL().is_empty());
  ASSERT_EQ(4u, order.started.size());
  const auto restored_profiles = window_->profiles();
  ASSERT_EQ(4u, restored_profiles.size());
  for (size_t i = 0; i < restored_profiles.size(); ++i) {
    EXPECT_EQ("cold-profile-" + std::to_string(i), restored_profiles[i].id);
    ASSERT_TRUE(window_->Select("cold-space-" + std::to_string(i)));
    EXPECT_EQ(paths[i], window_->contents(window_->active_id())->GetBrowserContext()->GetPath());
  }
  // Closing with a pending profile/navigation must invalidate both callbacks.
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_saved(); }));
  window_.reset();
  base::ThreadPoolInstance::Get()->FlushForTesting();
  auto closing_catalogue = base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  ASSERT_TRUE(closing_catalogue);
  const auto closing_directory = "ZitterProfile-" + base::Uuid::GenerateRandomV4().AsLowercaseString();
  closing_catalogue->FindList("profiles")->front().GetDict().Set("directory", closing_directory);
  ASSERT_TRUE(base::JSONWriter::Write(*closing_catalogue, &json));
  {
    base::ScopedAllowBlockingForTesting allow;
    ASSERT_TRUE(base::WriteFile(manager->user_data_dir().AppendASCII("ZitterWorkspaces.json"), json));
  }
  window_ = std::make_unique<WorkspaceWindow>(work_, personal_);
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_ready(); }));
  EXPECT_TRUE(window_->OpenLink(cancelled, "cold-space-0", true));
  base::test::TestFuture<Profile*> completed;
  manager->CreateProfileAsync(manager->user_data_dir().AppendASCII(closing_directory), completed.GetCallback());
  window_.reset();
  ASSERT_TRUE(completed.Wait());
  ASSERT_NE(nullptr, completed.Get());
  base::ThreadPoolInstance::Get()->FlushForTesting();
  window_ = std::make_unique<WorkspaceWindow>(work_, personal_);
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_ready(); }));
  EXPECT_EQ("cold-space-3", window_->active_id());
  EXPECT_EQ(nullptr, window_->contents("cold-space-0"));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, StartupProfileFailureKeepsActiveProfileUsable) {
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_saved(); }));
  window_.reset();
  base::ThreadPoolInstance::Get()->FlushForTesting();
  auto* manager = g_browser_process->profile_manager();
  const auto directory = "ZitterProfile-" + base::Uuid::GenerateRandomV4().AsLowercaseString();
  base::ListValue profiles, spaces;
  for (int i = 0; i < 2; ++i) {
    base::DictValue profile;
    profile.Set("id", i == 0 ? "work-profile" : "broken-profile");
    profile.Set("name", i == 0 ? "Work" : "Broken");
    profile.Set("directory", i == 0 ? "ZitterWork" : directory);
    profiles.Append(std::move(profile));
    base::DictValue space;
    space.Set("id", i == 0 ? "work" : "broken");
    space.Set("profile_id", i == 0 ? "work-profile" : "broken-profile");
    space.Set("name", i == 0 ? "Work" : "Broken");
    spaces.Append(std::move(space));
  }
  base::DictValue catalogue;
  catalogue.Set("profiles", std::move(profiles)); catalogue.Set("workspaces", std::move(spaces));
  catalogue.Set("active", "work"); catalogue.Set("onboarding_complete", true);
  std::string json;
  ASSERT_TRUE(base::JSONWriter::Write(catalogue, &json));
  {
    base::ScopedAllowBlockingForTesting allow;
    ASSERT_TRUE(base::WriteFile(manager->user_data_dir().AppendASCII(directory), "not a directory"));
    ASSERT_TRUE(base::WriteFile(manager->user_data_dir().AppendASCII("ZitterWorkspaces.json"), json));
    ASSERT_TRUE(base::WriteFile(manager->user_data_dir().AppendASCII("ZitterSession.json"), "{}"));
  }
  window_ = std::make_unique<WorkspaceWindow>(work_, personal_);
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_ready(); }));
  const auto url = embedded_test_server()->GetURL("/performance_probe.html?failed-profile");
  EXPECT_TRUE(window_->OpenLink(url, "broken", true));
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->shell()->profiles_widget()->IsVisible(); }));
  EXPECT_EQ("work", window_->active_id());
  EXPECT_EQ(nullptr, window_->contents("broken"));
  EXPECT_EQ(work_, window_->contents("work")->GetBrowserContext());
  EXPECT_TRUE(window_->contents("work")->GetLastCommittedURL().is_empty());
  EXPECT_TRUE(window_->NavigateFromBar(embedded_test_server()->GetURL("/performance_probe.html"), true));
  ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));
  EXPECT_EQ(work_, window_->contents("work")->GetBrowserContext());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, KeywordSearchProvidersAndPersistence) {
  auto* shell = window_->shell();
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->tools_ready(); }));
  EXPECT_EQ("https://www.google.com/search?q=hello+world", shell->ResolveInput("g hello world").spec());
  EXPECT_EQ("pl.wikipedia.org", shell->ResolveInput("w Warszawa").host());
  EXPECT_EQ("www.youtube.com", shell->ResolveInput("yt ambient").host());
  EXPECT_EQ("https://example.com/path", shell->ResolveInput("https://example.com/path").spec());
  EXPECT_EQ("http://example.com:8080/path", shell->ResolveInput("example.com:8080/path").spec());
  EXPECT_EQ("http://127.0.0.1:8080/path", shell->ResolveInput("127.0.0.1:8080/path").spec());
  EXPECT_FALSE(shell->ResolveInput("javascript:alert(1)").is_valid());
  EXPECT_FALSE(shell->ResolveInput("custom.scheme://example.com/path").is_valid());
  EXPECT_EQ("https://example.com/search/a%20b%2Fc", SearchDestination({"Path", "path", "https://example.com/search/{query}"}, "a b/c").spec());
  EXPECT_FALSE(shell->UpsertSearchProvider({"Unsafe", "x", "javascript:alert('{query}')"}));
  EXPECT_FALSE(shell->UpsertSearchProvider({"Host substitution", "x", "https://{query}.example.com/"}));
  EXPECT_FALSE(shell->UpsertSearchProvider({"Duplicate", "g", "https://example.com/?q={query}"}));
  EXPECT_FALSE(shell->UpsertSearchProvider({"Twice", "x", "https://example.com/?q={query}&a={query}"}));
  const std::string pattern = embedded_test_server()->GetURL("/").spec() + "storage_probe.html?q={query}";
  ASSERT_TRUE(shell->UpsertSearchProvider({"Local probe", "p", pattern}));
  ASSERT_TRUE(shell->SetDefaultSearchProvider("p"));
  shell->OpenAddress(true); SetPaletteQuery(u"p zażółć & a/b");
  ui::KeyEvent enter(ui::EventType::kKeyPressed, ui::VKEY_RETURN, ui::EF_NONE);
  ASSERT_TRUE(shell->address_input()->view()->OnKeyPressed(enter));
  const auto expected = SearchDestination({"Local probe", "p", pattern}, "zażółć & a/b");
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->contents("work")->GetLastCommittedURL() == expected; }));
  EXPECT_EQ(expected, shell->ResolveInput("zażółć & a/b"));
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->appearance_saved() && window_->configuration_saved(); }));
  window_.reset(); window_ = std::make_unique<WorkspaceWindow>(work_, personal_);
  shell = window_->shell();
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_ready() && shell->tools_ready(); }));
  EXPECT_EQ(expected, shell->ResolveInput("p zażółć & a/b"));
  EXPECT_EQ(expected, shell->ResolveInput("zażółć & a/b"));
  ASSERT_TRUE(shell->UpsertSearchProvider({"Renamed probe", "probe", pattern}, "p"));
  EXPECT_EQ("probe", shell->browser_tools().default_keyword);
  ASSERT_TRUE(shell->RemoveSearchProvider("probe"));
  EXPECT_NE("probe", shell->browser_tools().default_keyword);
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, ConfigurableNativeKeyboardShortcuts) {
  base::DictValue older;
  base::ListValue bindings;
  bindings.Append(base::DictValue().Set("id", "space2").Set("chord", "Cmd+J"));
  older.Set("shortcuts", std::move(bindings));
  const auto migrated = BrowserTools::FromValue(older);
  for (const auto& shortcut : migrated.shortcuts) {
    if (shortcut.id == "space2") EXPECT_EQ(*ParseShortcut("Cmd+J"), shortcut.accelerator);
    if (shortcut.id == "downloads") EXPECT_EQ(ui::VKEY_UNKNOWN, shortcut.accelerator.key_code());
  }
  ASSERT_NO_FATAL_FAILURE(NavigateBoth());
  ASSERT_TRUE(window_->Select("work"));
  auto* shell = window_->shell();
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->tools_ready(); }));
  EXPECT_FALSE(shell->SetShortcut("space2", "Cmd+C"));
  EXPECT_FALSE(shell->SetShortcut("space2", "Cmd+L"));
  EXPECT_FALSE(shell->SetShortcut("space2", "J"));
  ASSERT_TRUE(shell->SetShortcut("space2", "Cmd+Shift+J"));
  EXPECT_EQ("space2", shell->ShortcutFor(*ParseShortcut("Cmd+Shift+J")));
  EXPECT_TRUE(shell->ShortcutFor(ui::Accelerator(ui::VKEY_2, ui::EF_COMMAND_DOWN | ui::EF_ALT_DOWN)).empty());
  ui_controls::EnableUIControls();
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(window_->widget()->GetNativeWindow()));
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(window_->widget()->GetNativeWindow(), ui::VKEY_J, false, true, false, true));
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->active_id() == "personal"; }));
  shell->OpenTools(1);
  ASSERT_TRUE(shell->tools_widget()->IsVisible());
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->tools_widget()->GetNativeWindow()));
  const bool sidebar_before = shell->sidebar_visible();
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(shell->tools_widget()->GetNativeWindow(), ui::VKEY_S, false, false, false, true));
  ASSERT_TRUE(base::test::RunUntil([&] { return sidebar_before != shell->sidebar_visible(); }));
  auto* record = FindButton(shell->tools_widget()->GetContentsView(), u"Nagraj kombinację…");
  ASSERT_TRUE(record);
  ClickControl(record);
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(shell->tools_widget()->GetNativeWindow(), ui::VKEY_U, false, true, false, true));
  EXPECT_EQ(u"Cmd+Shift+U", shell->shortcut_input()->GetText());
  auto* save_key = FindButton(shell->tools_widget()->GetContentsView(), u"Zapisz skrót");
  ASSERT_TRUE(save_key);
  ClickControl(save_key);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ("address", shell->ShortcutFor(*ParseShortcut("Cmd+Shift+U")));
  ASSERT_TRUE(shell->SetShortcut("sidebar", ""));
  EXPECT_TRUE(shell->ShortcutFor(*ParseShortcut("Cmd+S")).empty());
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->appearance_saved() && window_->configuration_saved(); }));
  window_.reset(); window_ = std::make_unique<WorkspaceWindow>(work_, personal_); shell = window_->shell();
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_ready() && shell->tools_ready(); }));
  EXPECT_EQ("space2", shell->ShortcutFor(*ParseShortcut("Cmd+Shift+J")));
  EXPECT_TRUE(shell->ShortcutFor(*ParseShortcut("Cmd+S")).empty());
  const auto original = BrowserTools::FromValue(shell->browser_tools().ToValue());
  EXPECT_EQ(shell->browser_tools().shortcuts.size(), original.shortcuts.size());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, ToolsChromeCopyMetricsStatusAndProfileContext) {
  ASSERT_TRUE(window_->Select("work"));auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_ready()&&shell->appearance_ready();}));
  const bool rust=shell->workspace_input()->rust_editor();
  auto find=[&](std::u16string_view name){auto visit=[&](auto&& self,views::View* view)->views::View*{if(views::AsViewClass<views::Button>(view))return nullptr;ui::AXNodeData node;view->GetViewAccessibility().GetAccessibleNodeData(&node);if(node.role==ax::mojom::Role::kStaticText&&node.GetString16Attribute(ax::mojom::StringAttribute::kName)==name)return view;for(const auto& child:view->children())if(auto* found=self(self,child.get()))return found;return nullptr;};return visit(visit,shell->tools_widget()->GetContentsView());};
  const std::array<std::u16string,4> titles={u"Time Machine · Praca",u"Skróty klawiszowe",u"Wyszukiwarki i prefiksy",u"Historia przeglądania"};
  const std::array<std::u16string,4> intros={u"Snapshot zapisuje karty, foldery i Split View w tej przestrzeni. Przywrócenie przeładuje strony; formularze i historia Wstecz nie są częścią snapshotu. Poprzedni układ otrzyma punkt przywracania.",u"Schemat inspirowany Arc. Wybierz akcję, nagraj kombinację lub wpisz np. Cmd+Shift+J. Puste pole wyłącza skrót. Escape, skróty edycji tekstu i systemowe pozostają zarezerwowane.",u"W pasku adresu wpisz prefiks i zapytanie, np. g koty lub w Warszawa. Szablon musi być adresem HTTP(S) z jednym znacznikiem {query}. Zapytania są kodowane automatycznie.",u"Profil: Profil firmowy"};
  auto metrics=[&](views::View* copy,std::u16string text,int delta,gfx::Font::Weight weight,bool multiline){ASSERT_TRUE(copy)<<base::UTF16ToUTF8(text);EXPECT_EQ(!rust,views::AsViewClass<views::Label>(copy)!=nullptr);views::Label reference(text);reference.SetFontList(gfx::FontList().DeriveWithSizeDelta(delta).DeriveWithWeight(weight));reference.SetMultiLine(multiline);for(int width:{80,160,320})EXPECT_EQ(reference.GetHeightForWidth(width),copy->GetHeightForWidth(width))<<base::UTF16ToUTF8(text)<<" width="<<width;};
  for(int page=0;page<4;++page){shell->OpenTools(page);ASSERT_NO_FATAL_FAILURE(metrics(find(titles[page]),titles[page],5,gfx::Font::Weight::SEMIBOLD,false));
    ASSERT_NO_FATAL_FAILURE(metrics(find(intros[page]),intros[page],-1,gfx::Font::Weight::NORMAL,true));
    if(rust)ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_chrome_contains_text_for_testing(base::UTF16ToUTF8(titles[page]));}));
  }
  shell->OpenTools(2);views::ViewTracker old(find(intros[2]));const std::u16string status=std::u16string(u"Błąd zapisu 日本語 🦀")+char16_t(10)+u"Ponów operację w bieżącym profilu.";shell->SetOperationStatus(status);EXPECT_FALSE(old.view());
  ASSERT_NO_FATAL_FAILURE(metrics(find(status),status,-1,gfx::Font::Weight::NORMAL,true));shell->tools_widget()->LayoutRootViewIfNecessary();
  if(rust)ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_chrome_contains_text_for_testing(base::UTF16ToUTF8(status));}));
  ASSERT_TRUE(window_->Select("personal"));shell->OpenTools(3);ASSERT_TRUE(find(u"Profil: Profil osobisty"));
  if(rust)ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_chrome_contains_text_for_testing("Profil: Profil osobisty");}));
}

class ToolsPanelProbeView final : public views::View {
  METADATA_HEADER(ToolsPanelProbeView,views::View)
 public:
  ToolsPanelProbeView(int width,int height,bool wrap):preferred_(width,height),wrap_(wrap){}
  gfx::Size CalculatePreferredSize(const views::SizeBounds& bounds) const override{
    if(!wrap_||!bounds.width().is_bounded())return preferred_;
    const int width=std::min(preferred_.width(),bounds.width().value());
    const int lines=(preferred_.width()+std::max(1,width)-1)/std::max(1,width);
    return gfx::Size(width,preferred_.height()*std::max(1,lines));
  }
 private:
  gfx::Size preferred_;bool wrap_;
};
BEGIN_METADATA(ToolsPanelProbeView)
END_METADATA
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, ToolsPanelLayoutMatchesBoxMeasurementAndBounds) {
  // The reference is the actual Chromium BoxLayout with independent native
  // measurement, not a second copy of the Rust allocator's expected rectangles.
  for(bool vertical:{false,true})for(int flex:{-1,0,1})for(int mode:{0,1,2,3}){
    views::View candidate,reference;
    candidate.SetBorder(views::CreateEmptyBorder(gfx::Insets(3)));reference.SetBorder(views::CreateEmptyBorder(gfx::Insets(3)));
    const auto insets=vertical?gfx::Insets::TLBR(4,5,6,7):gfx::Insets();
    for(auto* host:{&candidate,&reference}){
      host->AddChildView(std::make_unique<ToolsPanelProbeView>(190,32,false));
      host->AddChildView(std::make_unique<ToolsPanelProbeView>(260,80,true));
      host->AddChildView(std::make_unique<ToolsPanelProbeView>(100,20,false));
      host->AddChildView(std::make_unique<ToolsPanelProbeView>(0,0,false));
      if(mode==1)host->children()[1]->SetVisible(false);
      if(mode==2)host->children()[0]->SetProperty(views::kViewIgnoredByLayoutKey,true);
      if(mode==3)for(const auto& child:host->children())child->SetProperty(views::kMarginsKey,gfx::Insets::TLBR(1,2,3,4));
    }
    SetSidebarPanelLayout(&candidate,vertical,insets,8,flex<0?nullptr:candidate.children()[flex].get());
    auto* box=reference.SetLayoutManager(std::make_unique<views::BoxLayout>(vertical?views::BoxLayout::Orientation::kVertical:views::BoxLayout::Orientation::kHorizontal,insets,8));
    if(flex>=0)box->SetFlexForView(reference.children()[flex],1);
    SCOPED_TRACE(testing::Message()<<"vertical="<<vertical<<" flex="<<flex<<" mode="<<mode);
    EXPECT_EQ(reference.GetPreferredSize(),candidate.GetPreferredSize());EXPECT_EQ(reference.GetMinimumSize(),candidate.GetMinimumSize());
    for(int width:{0,1,20,80,160,320,561}){
      EXPECT_EQ(reference.GetHeightForWidth(width),candidate.GetHeightForWidth(width))<<"width="<<width;
      for(int height:{0,1,32,80,240,501}){
        SCOPED_TRACE(testing::Message()<<"width="<<width<<" height="<<height);
        EXPECT_EQ(reference.GetPreferredSize(views::SizeBounds(width,height)),candidate.GetPreferredSize(views::SizeBounds(width,height)));
        candidate.SetSize(gfx::Size(width,height));reference.SetSize(gfx::Size(width,height));candidate.InvalidateLayout();reference.InvalidateLayout();candidate.DeprecatedLayoutImmediately();reference.DeprecatedLayoutImmediately();
        for(size_t i=0;i<candidate.children().size();++i)if(candidate.children()[i]->GetVisible()&&!candidate.children()[i]->GetProperty(views::kViewIgnoredByLayoutKey))EXPECT_EQ(reference.children()[i]->bounds(),candidate.children()[i]->bounds())<<"child="<<i;
      }
    }
  }
}
class PreferenceMeasureProxy final : public views::View {
  METADATA_HEADER(PreferenceMeasureProxy,views::View)
 public:
  explicit PreferenceMeasureProxy(const views::View* source):source_(source){}
  gfx::Size CalculatePreferredSize(const views::SizeBounds& bounds) const override{return source_->GetPreferredSize(bounds);}
  gfx::Size GetMinimumSize() const override{return source_->GetMinimumSize();}
 private:
  raw_ptr<const views::View> source_;
};
BEGIN_METADATA(PreferenceMeasureProxy)
END_METADATA
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, PreferencePanelLayoutMatchesBoxMeasurementAndCenteredBounds) {
  // The reference is the actual Chromium BoxLayout with independent native
  // measurement, not a second copy of the Rust allocator's expected rectangles.
  for(bool vertical:{false,true})for(int flex:{-1,0,1})for(int mode:{0,1,2,3,4}){
    views::View candidate,reference;
    candidate.SetBorder(views::CreateEmptyBorder(gfx::Insets(3)));reference.SetBorder(views::CreateEmptyBorder(gfx::Insets(3)));
    const auto insets=vertical?gfx::Insets::TLBR(4,5,6,7):gfx::Insets();
    for(auto* host:{&candidate,&reference}){
      host->AddChildView(std::make_unique<ToolsPanelProbeView>(190,33,false));
      host->AddChildView(std::make_unique<ToolsPanelProbeView>(260,81,true));
      host->AddChildView(std::make_unique<ToolsPanelProbeView>(100,21,false));
      host->AddChildView(std::make_unique<ToolsPanelProbeView>(0,0,false));
      if(mode==1)host->children()[1]->SetVisible(false);
      if(mode==2)host->children()[0]->SetProperty(views::kViewIgnoredByLayoutKey,true);
      if(mode==3)for(const auto& child:host->children())child->SetProperty(views::kMarginsKey,gfx::Insets::TLBR(1,2,3,4));
    }
    if(mode==4)for(auto* host:{&candidate,&reference})for(size_t i=0;i<host->children().size();++i)host->children()[i]->SetProperty(views::kMarginsKey,gfx::Insets::TLBR(i+1,5-i,7-i,i+2));
    SetSidebarPanelLayout(&candidate,vertical,insets,8,flex<0?nullptr:candidate.children()[flex].get(),true);
    auto* box=reference.SetLayoutManager(std::make_unique<views::BoxLayout>(vertical?views::BoxLayout::Orientation::kVertical:views::BoxLayout::Orientation::kHorizontal,insets,8));
    box->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
    if(flex>=0)box->SetFlexForView(reference.children()[flex],1);
    SCOPED_TRACE(testing::Message()<<"vertical="<<vertical<<" flex="<<flex<<" mode="<<mode);
    EXPECT_EQ(reference.GetPreferredSize(),candidate.GetPreferredSize());EXPECT_EQ(reference.GetMinimumSize(),candidate.GetMinimumSize());
    for(int width:{0,1,20,80,160,320,561}){
      EXPECT_EQ(reference.GetHeightForWidth(width),candidate.GetHeightForWidth(width))<<"width="<<width;
      for(int height:{0,1,32,80,240,501}){
        SCOPED_TRACE(testing::Message()<<"width="<<width<<" height="<<height);
        EXPECT_EQ(reference.GetPreferredSize(views::SizeBounds(width,height)),candidate.GetPreferredSize(views::SizeBounds(width,height)));
        candidate.SetSize(gfx::Size(width,height));reference.SetSize(gfx::Size(width,height));candidate.InvalidateLayout();reference.InvalidateLayout();candidate.DeprecatedLayoutImmediately();reference.DeprecatedLayoutImmediately();
        for(size_t i=0;i<candidate.children().size();++i)if(candidate.children()[i]->GetVisible()&&!candidate.children()[i]->GetProperty(views::kViewIgnoredByLayoutKey))EXPECT_EQ(reference.children()[i]->bounds(),candidate.children()[i]->bounds())<<"child="<<i;
      }
    }
  }
}
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, PreferencePanelActualHierarchyMatchesBoxAcrossCategoriesAndResize) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));
  auto compare=[&](views::View* host,bool vertical,gfx::Insets insets,int gap,views::View* flex,bool center){
    if(!host->IsDrawn())return;
    views::View reference;reference.SetBorder(views::CreateEmptyBorder(host->GetInsets()));auto* box=reference.SetLayoutManager(std::make_unique<views::BoxLayout>(vertical?views::BoxLayout::Orientation::kVertical:views::BoxLayout::Orientation::kHorizontal,insets,gap));if(center)box->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
    for(const auto& child:host->children()){auto* proxy=reference.AddChildView(std::make_unique<PreferenceMeasureProxy>(child.get()));proxy->SetVisible(child->GetVisible());if(auto* margins=child->GetProperty(views::kMarginsKey))proxy->SetProperty(views::kMarginsKey,*margins);proxy->SetProperty(views::kViewIgnoredByLayoutKey,child->GetProperty(views::kViewIgnoredByLayoutKey));if(child.get()==flex)box->SetFlexForView(proxy,1);}
    auto* actual=host->GetLayoutManager();ASSERT_TRUE(actual);EXPECT_EQ(reference.GetPreferredSize(),actual->GetPreferredSize(host));EXPECT_EQ(reference.GetHeightForWidth(host->width()),actual->GetPreferredHeightForWidth(host,host->width()));EXPECT_EQ(reference.GetPreferredSize(views::SizeBounds(host->width(),host->height())),actual->GetPreferredSize(host,views::SizeBounds(host->width(),host->height())));
    reference.SetSize(host->size());reference.DeprecatedLayoutImmediately();ASSERT_EQ(host->children().size(),reference.children().size());for(size_t i=0;i<host->children().size();++i)if(host->children()[i]->GetVisible()&&!host->children()[i]->GetProperty(views::kViewIgnoredByLayoutKey))EXPECT_EQ(reference.children()[i]->bounds(),host->children()[i]->bounds())<<"child="<<i;
  };
  for(auto size:{gfx::Size(700,660),gfx::Size(1000,860)}){window_->widget()->SetBounds(gfx::Rect(gfx::Point(40,40),size));shell->OpenGeneralSettings();auto* widget=shell->general_widget();widget->LayoutRootViewIfNecessary();
    for(size_t category=0;category<10;++category){SCOPED_TRACE(testing::Message()<<"size="<<size.ToString()<<" category="<<category);auto* nav=shell->preference_navigation_for_testing(category);if(nav->IsDrawn())ActivateSidebarRow(nav);else shell->ActivatePreferenceChoiceForTesting("category",category);EXPECT_EQ(category,shell->preference_category_for_testing());widget->LayoutRootViewIfNecessary();
      auto rows=shell->preference_rows_for_testing(category);ASSERT_FALSE(rows.empty());auto* section=rows.front()->parent();auto* body=section->parent();auto* main=shell->preferences_search()->view()->parent();auto* root=main->parent();auto* navigation=root->children().front().get();auto* header=main->children().front().get();
      ASSERT_NO_FATAL_FAILURE(compare(root,false,gfx::Insets(8),8,main,false));
      ASSERT_NO_FATAL_FAILURE(compare(main,true,gfx::Insets::VH(16,20),12,main->children()[main->children().size()-2],false));
      ASSERT_NO_FATAL_FAILURE(compare(navigation,true,gfx::Insets(16),6,navigation->children()[navigation->children().size()-2],false));
      ASSERT_NO_FATAL_FAILURE(compare(header,false,gfx::Insets(),12,header->children().front(),false));
      ASSERT_NO_FATAL_FAILURE(compare(header->children().front(),true,gfx::Insets(),5,nullptr,false));
      ASSERT_NO_FATAL_FAILURE(compare(body,true,gfx::Insets::TLBR(0,0,8,8),16,nullptr,false));
      ASSERT_NO_FATAL_FAILURE(compare(section,true,gfx::Insets(),12,nullptr,false));
      for(auto* row:rows){SCOPED_TRACE(row->bounds().ToString());ASSERT_FALSE(row->children().empty());
        ASSERT_NO_FATAL_FAILURE(compare(row,false,gfx::Insets(16),20,row->children().front(),true));
        ASSERT_NO_FATAL_FAILURE(compare(row->children().front(),true,gfx::Insets(),5,nullptr,false));
      }
    }
  }
}
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, ToolsViewportMatchesNativeExtentClampAndReveal) {
  auto delegate=std::make_unique<views::WidgetDelegate>();auto* host=delegate->SetContentsView(std::make_unique<views::View>());
  PanelViewport* viewport=nullptr;auto candidate=CreatePanelViewport(&viewport,base::DoNothing());auto* candidate_view=host->AddChildView(std::move(candidate));
  auto reference=std::make_unique<views::ScrollView>();auto* native=reference.get();native->SetUseContentsPreferredSize(true);native->SetBackgroundColor(std::nullopt);native->SetDrawOverflowIndicator(false);native->SetHorizontalScrollBarMode(views::ScrollView::ScrollBarMode::kDisabled);host->AddChildView(std::move(reference));auto sync=native->EnableScrollSynchronization();
  viewport->SetPanelContents(std::make_unique<ToolsPanelProbeView>(200,900,true));native->SetContents(std::make_unique<ToolsPanelProbeView>(200,900,true));
  views::Widget::InitParams params(views::Widget::InitParams::CLIENT_OWNS_WIDGET,views::Widget::InitParams::TYPE_BUBBLE);params.delegate=delegate.get();params.SetParent(window_->widget());auto widget=std::make_unique<views::Widget>();widget->Init(std::move(params));widget->SetBounds(gfx::Rect(40,40,900,760));widget->Show();
  for(auto size:{gfx::Size(120,100),gfx::Size(240,300),gfx::Size(480,620),gfx::Size(480,1200)}){SCOPED_TRACE(size.ToString());candidate_view->SetBoundsRect(gfx::Rect(size));native->SetBoundsRect(gfx::Rect(400,0,size.width(),size.height()));candidate_view->InvalidateLayout();native->InvalidateLayout();candidate_view->DeprecatedLayoutImmediately();native->DeprecatedLayoutImmediately();
    EXPECT_EQ(native->contents()->size(),viewport->contents()->size());EXPECT_EQ(native->GetVisibleRect().size(),viewport->visible_rect().size());EXPECT_EQ(std::max(0,native->contents()->height()-native->GetVisibleRect().height()),viewport->maximum());if(native->vertical_scroll_bar()->GetVisible())EXPECT_EQ(native->vertical_scroll_bar()->GetMaxPosition(),viewport->maximum());
    for(int position:{0,10,333,2000,-50}){viewport->ScrollTo(position);native->ScrollToPosition(native->vertical_scroll_bar(),position);EXPECT_EQ(native->CurrentOffset().y(),viewport->offset());EXPECT_EQ(native->GetVisibleRect(),viewport->visible_rect());}
    for(int y:{-50,0,50,700,2000})for(int height:{1,40,1500}){const gfx::Rect target(0,y,1,height);viewport->contents()->ScrollRectToVisible(target);native->contents()->ScrollRectToVisible(target);EXPECT_EQ(native->CurrentOffset().y(),viewport->offset())<<"target="<<target.ToString();}
  }
  widget->CloseNow();
}
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, HorizontalViewportMatchesNativeExtentRevealAndClamp) {
  auto delegate=std::make_unique<views::WidgetDelegate>();auto* host=delegate->SetContentsView(std::make_unique<views::View>());PanelViewportOptions options;options.horizontal=true;PanelViewport* viewport=nullptr;auto* candidate=host->AddChildView(CreatePanelViewport(&viewport,base::DoNothing(),options));
  auto reference=std::make_unique<views::ScrollView>();auto* native=reference.get();native->SetUseContentsPreferredSize(true);native->SetBackgroundColor(std::nullopt);native->SetDrawOverflowIndicator(false);native->SetVerticalScrollBarMode(views::ScrollView::ScrollBarMode::kDisabled);host->AddChildView(std::move(reference));auto sync=native->EnableScrollSynchronization();viewport->SetPanelContents(std::make_unique<ToolsPanelProbeView>(900,28,false));native->SetContents(std::make_unique<ToolsPanelProbeView>(900,28,false));
  views::Widget::InitParams params(views::Widget::InitParams::CLIENT_OWNS_WIDGET,views::Widget::InitParams::TYPE_BUBBLE);params.delegate=delegate.get();params.SetParent(window_->widget());auto widget=std::make_unique<views::Widget>();widget->Init(std::move(params));widget->SetBounds(gfx::Rect(40,40,900,300));widget->Show();
  for(auto size:{gfx::Size(120,47),gfx::Size(240,60),gfx::Size(480,47),gfx::Size(1200,32)}){SCOPED_TRACE(size.ToString());candidate->SetBoundsRect(gfx::Rect(size));native->SetBoundsRect(gfx::Rect(400,0,size.width(),size.height()));candidate->InvalidateLayout();native->InvalidateLayout();candidate->DeprecatedLayoutImmediately();native->DeprecatedLayoutImmediately();EXPECT_EQ(native->contents()->size(),viewport->contents()->size());EXPECT_EQ(native->GetVisibleRect().size(),viewport->visible_rect().size());EXPECT_EQ(std::max(0,native->contents()->width()-native->GetVisibleRect().width()),viewport->maximum());
    for(int position:{0,10,333,2000,-50}){viewport->ScrollTo(position);native->ScrollToPosition(native->horizontal_scroll_bar(),position);EXPECT_DOUBLE_EQ(native->CurrentOffset().x(),viewport->offset());EXPECT_EQ(native->GetVisibleRect(),viewport->visible_rect());}
    for(int x:{-50,0,50,700,2000})for(int width:{1,40,1500}){const gfx::Rect target(x,0,width,1);viewport->contents()->ScrollRectToVisible(target);native->contents()->ScrollRectToVisible(target);EXPECT_DOUBLE_EQ(native->CurrentOffset().x(),viewport->offset())<<target.ToString();}
  }
  widget->CloseNow();
}
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, FooterViewportOwnershipResizeKeysAXAndShrink) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->SetWorkspaceAnimations(false);std::vector<std::string> added;for(int i=0;i<10;++i){ASSERT_TRUE(window_->AddWorkspace("Viewport "+std::to_string(i),"work-profile"));ASSERT_TRUE(base::test::RunUntil([&]{return !window_->profiles_busy();}));added.push_back(window_->workspaces().back().id);}ASSERT_TRUE(window_->Select("work"));shell->CloseOverlays(true);shell->SetPinned(true);shell->ShowSidebar();auto* viewport=shell->workspace_viewport();ASSERT_TRUE(viewport);EXPECT_EQ(shell->workspace_input()->rust_editor(),viewport->rust_owned());if(viewport->rust_owned())EXPECT_FALSE(views::AsViewClass<views::ScrollView>(viewport->view()));auto* bar=viewport->horizontal_scroll_bar();ASSERT_TRUE(bar);EXPECT_EQ(views::ScrollBar::Orientation::kHorizontal,bar->GetOrientation());
  for(int width:{200,320}){shell->ResizeSidebar(width-shell->sidebar_width(),true);shell->ShowSidebar();auto* widget=shell->sidebar_widget();widget->LayoutRootViewIfNecessary();base::test::TestFuture<void> presented;auto* compositor=widget->GetCompositor();ASSERT_TRUE(compositor);widget->GetRootView()->SchedulePaint();compositor->RequestSuccessfulPresentationTimeForNextFrame(base::BindOnce([](base::OnceClosure done,const viz::FrameTimingDetails&){std::move(done).Run();},presented.GetCallback()));compositor->ScheduleFullRedraw();ASSERT_TRUE(presented.Wait());
    // Native sizing may fit or retain an overflowing contents width after a
    // resize. Derive its range from live geometry; Metal must retain full dots.
    EXPECT_EQ(std::max(0,viewport->contents()->width()-viewport->visible_rect().width()),viewport->maximum());
    if(viewport->maximum()==0){EXPECT_FALSE(viewport->rust_owned());EXPECT_DOUBLE_EQ(0,viewport->offset());EXPECT_FALSE(bar->GetVisible());continue;}
    ASSERT_GT(viewport->maximum(),0);EXPECT_EQ(viewport->contents()->width()-viewport->visible_rect().width(),viewport->maximum());viewport->ScrollTo(viewport->maximum());EXPECT_DOUBLE_EQ(viewport->maximum(),viewport->offset());auto* first=shell->workspace_button("work");ASSERT_TRUE(first);first->ScrollViewToVisible();EXPECT_FALSE(first->GetVisibleBounds().IsEmpty());EXPECT_DOUBLE_EQ(0,viewport->offset());ui::KeyEvent end(ui::EventType::kKeyPressed,ui::VKEY_END,0);EXPECT_TRUE(viewport->view()->OnKeyPressed(end));EXPECT_DOUBLE_EQ(viewport->maximum(),viewport->offset());ui::KeyEvent home(ui::EventType::kKeyPressed,ui::VKEY_HOME,0);EXPECT_TRUE(viewport->view()->OnKeyPressed(home));EXPECT_DOUBLE_EQ(0,viewport->offset());ui::AXActionData right;right.action=ax::mojom::Action::kScrollRight;EXPECT_TRUE(viewport->view()->HandleAccessibleAction(right));EXPECT_GT(viewport->offset(),0);ui::AXNodeData ax;viewport->view()->GetViewAccessibility().GetAccessibleNodeData(&ax);EXPECT_EQ(ax::mojom::Role::kScrollView,ax.role);if(viewport->rust_owned())EXPECT_EQ(viewport->maximum(),ax.GetIntAttribute(ax::mojom::IntAttribute::kScrollXMax));
  }
  viewport->ScrollTo(viewport->maximum());for(const auto& id:added)ASSERT_TRUE(window_->RemoveWorkspace(id));ASSERT_TRUE(base::test::RunUntil([&]{shell->sidebar_widget()->LayoutRootViewIfNecessary();return viewport->maximum()==0&&viewport->offset()==0;}));EXPECT_FALSE(bar->GetVisible());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, ServiceViewportMatchesNativeBoundedReflowAndOverflowEdges) {
  auto delegate=std::make_unique<views::WidgetDelegate>();auto* host=delegate->SetContentsView(std::make_unique<views::View>());
  views::Widget::InitParams params(views::Widget::InitParams::CLIENT_OWNS_WIDGET,views::Widget::InitParams::TYPE_BUBBLE);params.delegate=delegate.get();params.SetParent(window_->widget());auto widget=std::make_unique<views::Widget>();widget->Init(std::move(params));widget->SetBounds(gfx::Rect(40,40,900,760));widget->Show();
  auto edges=[](views::View* view){std::vector<gfx::Rect> out;for(const auto& child:view->children())if(views::AsViewClass<views::Separator>(child.get())&&child->GetVisible())out.push_back(child->bounds());std::sort(out.begin(),out.end(),[](const auto& a,const auto& b){return a.y()<b.y();});return out;};
  for(const auto options:{PanelViewportOptions{100,480,false,true,true},PanelViewportOptions{0,220,false,true},PanelViewportOptions{0,480,false,true}}){
    PanelViewport* viewport=nullptr;auto candidate=CreatePanelViewport(&viewport,base::DoNothing(),options);auto* candidate_view=host->AddChildView(std::move(candidate));EXPECT_EQ(window_->shell()->workspace_input()->rust_editor(),viewport->rust_owned());EXPECT_EQ(viewport,PanelViewportForView(candidate_view));
    auto reference=std::make_unique<views::ScrollView>();auto* native=reference.get();native->ClipHeightTo(options.minimum_height,options.maximum_height);native->SetHorizontalScrollBarMode(options.horizontal_scroll_enabled?views::ScrollView::ScrollBarMode::kEnabled:views::ScrollView::ScrollBarMode::kDisabled);host->AddChildView(std::move(reference));auto sync=native->EnableScrollSynchronization();
    viewport->SetPanelContents(std::make_unique<ToolsPanelProbeView>(200,900,true));native->SetContents(std::make_unique<ToolsPanelProbeView>(200,900,true));
    for(auto mode:{ui::ColorProviderKey::ColorMode::kDark,ui::ColorProviderKey::ColorMode::kLight}){widget->SetColorModeOverride(mode);
      for(auto size:{gfx::Size(120,100),gfx::Size(240,300),gfx::Size(480,620),gfx::Size(480,2400)}){SCOPED_TRACE(size.ToString());candidate_view->SetBoundsRect(gfx::Rect(size));native->SetBoundsRect(gfx::Rect(400,0,size.width(),size.height()));candidate_view->InvalidateLayout();native->InvalidateLayout();candidate_view->DeprecatedLayoutImmediately();native->DeprecatedLayoutImmediately();
        EXPECT_EQ(native->GetPreferredSize(views::SizeBounds(size)),candidate_view->GetPreferredSize(views::SizeBounds(size)));EXPECT_EQ(native->contents()->size(),viewport->contents()->size());EXPECT_EQ(native->GetVisibleRect(),viewport->visible_rect());ASSERT_TRUE(candidate_view->background());EXPECT_EQ(ui::ColorVariant(ui::kColorDialogBackground),candidate_view->background()->color());
        for(int position:{0,10,333,2000,-50}){viewport->ScrollTo(position);native->ScrollToPosition(native->vertical_scroll_bar(),position);EXPECT_EQ(native->CurrentOffset().y(),viewport->offset());EXPECT_EQ(native->GetVisibleRect(),viewport->visible_rect());EXPECT_EQ(edges(native),edges(candidate_view));}
      }
    }
    sync.reset();host->RemoveChildViewT(candidate_view);host->RemoveChildViewT(native);
  }
  widget->CloseNow();
}
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, ServiceViewportModalBodyOwnershipRevealAndClose) {
  auto delegate=CreateSceneModalDialog(std::make_unique<ToolsPanelProbeView>(200,2000,true),true);ASSERT_TRUE(delegate);auto* viewport=PanelViewportForView(delegate->GetContentsView());ASSERT_TRUE(viewport);EXPECT_EQ(window_->shell()->workspace_input()->rust_editor(),viewport->rust_owned());
  std::unique_ptr<views::Widget> widget(views::DialogDelegate::CreateDialogWidget(delegate.get(),window_->widget()->GetNativeWindow(),window_->widget()->GetNativeView()));widget->Show();widget->LayoutRootViewIfNecessary();ASSERT_GT(viewport->maximum(),0);EXPECT_LE(viewport->view()->GetPreferredSize().height(),views::LayoutProvider::Get()->GetDistanceMetric(views::DISTANCE_MODAL_DIALOG_SCROLLABLE_AREA_MAX_HEIGHT));ASSERT_FALSE(viewport->contents()->children().empty());EXPECT_EQ(2,viewport->contents()->children().front()->y());
  viewport->ScrollTo(viewport->maximum());EXPECT_GT(viewport->offset(),0);viewport->contents()->ScrollRectToVisible(gfx::Rect(0,0,1,1));EXPECT_EQ(0,viewport->offset());
  for(auto mode:{ui::ColorProviderKey::ColorMode::kDark,ui::ColorProviderKey::ColorMode::kLight}){widget->SetColorModeOverride(mode);widget->LayoutRootViewIfNecessary();viewport->ScrollTo(viewport->maximum());EXPECT_LE(viewport->offset(),viewport->maximum());}
  widget->CloseNow();widget.reset();delegate.reset();
}

// Compile only: adapter identity/focus/AX and native mutations await the GUI checkpoint.
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, WorkspaceSceneReusesAdaptersAcrossRenameEmojiAndMembership) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready()&&!window_->profiles_busy();}));if(!shell->workspace_input()->rust_editor())return;shell->SetWorkspaceAnimations(false);shell->SetPinned(true);shell->ShowSidebar();ASSERT_TRUE(window_->Select("work"));auto* work=shell->workspace_button("work");auto* personal=shell->workspace_button("personal");ASSERT_TRUE(work);ASSERT_TRUE(personal);const auto id=shell->sidebar_node_id_for_testing(work),root=shell->scene_parent_id_for_testing(work);EXPECT_NE(0u,root);EXPECT_EQ(root,shell->scene_parent_id_for_testing(personal));EXPECT_EQ(4u,shell->control_model_kind_for_testing(work));work->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);work->RequestFocus();ASSERT_TRUE(work->HasFocus());
  shell->SetWorkspaceEmoji("work",u"🚀");EXPECT_EQ(work,shell->workspace_button("work"));EXPECT_EQ(personal,shell->workspace_button("personal"));EXPECT_TRUE(work->HasFocus());EXPECT_EQ(id,shell->sidebar_node_id_for_testing(work));EXPECT_EQ(root,shell->scene_parent_id_for_testing(work));ASSERT_TRUE(window_->EditWorkspace("Scene 日本語 🦀","work-profile"));EXPECT_EQ(work,shell->workspace_button("work"));ui::AXNodeData ax;work->GetViewAccessibility().GetAccessibleNodeData(&ax);EXPECT_EQ(u"Przestrzeń: Scene 日本語 🦀",ax.GetString16Attribute(ax::mojom::StringAttribute::kName));
  auto* parent=work->parent();parent->SetEnabled(false);shell->SetWorkspaceEmoji("work",u"🦀");work->GetViewAccessibility().GetAccessibleNodeData(&ax);EXPECT_FALSE(ax.HasIntAttribute(ax::mojom::IntAttribute::kDefaultActionVerb));EXPECT_EQ(work,shell->workspace_button("work"));parent->SetEnabled(true);work->GetViewAccessibility().GetAccessibleNodeData(&ax);EXPECT_EQ(ax::mojom::DefaultActionVerb::kPress,ax.GetDefaultActionVerb());
  ASSERT_TRUE(window_->AddWorkspace("Scene added","work-profile"));ASSERT_TRUE(base::test::RunUntil([&]{return !window_->profiles_busy();}));const auto added=window_->workspaces().back().id;auto* added_view=shell->workspace_button(added);ASSERT_TRUE(added_view);EXPECT_EQ(work,shell->workspace_button("work"));EXPECT_EQ(root,shell->scene_parent_id_for_testing(added_view));auto held=added_view->parent()->RemoveChildViewT(added_view);ASSERT_TRUE(window_->Select("work"));ASSERT_TRUE(window_->RemoveWorkspace(added));EXPECT_EQ(0u,shell->sidebar_node_id_for_testing(held.get()));EXPECT_EQ(0u,shell->scene_parent_id_for_testing(held.get()));EXPECT_EQ(work,shell->workspace_button("work"));EXPECT_EQ(personal,shell->workspace_button("personal"));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, WorkspaceSceneGeometryRefreshCancelsSiblingPressWithoutReplacingViews) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));if(!shell->workspace_input()->rust_editor())return;shell->SetWorkspaceAnimations(false);shell->SetPinned(true);shell->ShowSidebar();ASSERT_TRUE(window_->Select("work"));shell->sidebar_widget()->LayoutRootViewIfNecessary();auto* personal=shell->workspace_button("personal");ASSERT_TRUE(personal);const auto point=personal->GetLocalBounds().CenterPoint();ui::MouseEvent press(ui::EventType::kMousePressed,point,point,base::TimeTicks(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);ASSERT_TRUE(personal->OnMousePressed(press));shell->SetWorkspaceEmoji("work",u"🚀");EXPECT_EQ(personal,shell->workspace_button("personal"));ui::MouseEvent release(ui::EventType::kMouseReleased,point,point,base::TimeTicks(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);personal->OnMouseReleased(release);EXPECT_EQ("work",window_->active_id());
}

// Compile-only until the next bulk checkpoint: static chrome shares window scope.
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, ChromeLogicalNodesRemainDistinctAcrossWorkspaceAndEmojiRefresh) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->SetWorkspaceAnimations(false);shell->SetPinned(true);shell->ShowSidebar();
  const bool rust=shell->workspace_input()->rust_editor();std::vector<views::View*> controls;
  for(size_t i=0;i<4;++i)controls.push_back(shell->header_action_for_testing(i));
  for(size_t i=0;i<2;++i){controls.push_back(shell->sidebar_action_for_testing(i));controls.push_back(shell->footer_action_for_testing(i));}
  std::vector<uint64_t> ids;for(auto* view:controls){ASSERT_TRUE(view);ids.push_back(shell->sidebar_node_id_for_testing(view));if(rust)EXPECT_NE(0u,ids.back());else EXPECT_EQ(0u,ids.back());}
  if(rust)for(size_t i=0;i<ids.size();++i)for(size_t j=0;j<i;++j)EXPECT_NE(ids[i],ids[j]);
  ASSERT_TRUE(window_->Select("personal"));shell->SetWorkspaceEmoji("personal",u"🚀");shell->ShowSidebar();
  for(size_t i=0;i<controls.size();++i)EXPECT_EQ(ids[i],shell->sidebar_node_id_for_testing(controls[i]));
  ASSERT_TRUE(window_->Select("work"));shell->ShowSidebar();
  for(size_t i=0;i<controls.size();++i)EXPECT_EQ(ids[i],shell->sidebar_node_id_for_testing(controls[i]));
}

// Compiled only: runtime acceptance is deferred to the bulk UI checkpoint.
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, FooterLogicalNodesSurviveEmojiReplacementAndRejectStaleAdapter) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->SetWorkspaceAnimations(false);shell->SetPinned(true);shell->ShowSidebar();
  const bool rust=shell->workspace_input()->rust_editor();auto* personal=shell->workspace_button("personal");ASSERT_TRUE(personal);
  auto* pin=shell->footer_action_for_testing(0);auto* settings=shell->footer_action_for_testing(1);ASSERT_TRUE(pin);ASSERT_TRUE(settings);
  const auto logical=shell->sidebar_node_id_for_testing(personal),pin_id=shell->sidebar_node_id_for_testing(pin),settings_id=shell->sidebar_node_id_for_testing(settings);
  if(rust){EXPECT_NE(0u,logical);EXPECT_NE(0u,pin_id);EXPECT_NE(0u,settings_id);EXPECT_NE(logical,pin_id);EXPECT_NE(pin_id,settings_id);}else EXPECT_EQ(0u,logical);
  auto held=personal->parent()->RemoveChildViewT(personal);
  shell->SetWorkspaceEmoji("personal",u"🚀");auto* replacement=shell->workspace_button("personal");ASSERT_TRUE(replacement);EXPECT_NE(held.get(),replacement);
  EXPECT_EQ(logical,shell->sidebar_node_id_for_testing(replacement));EXPECT_EQ(pin_id,shell->sidebar_node_id_for_testing(pin));EXPECT_EQ(settings_id,shell->sidebar_node_id_for_testing(settings));
  if(rust){EXPECT_EQ(0u,shell->sidebar_node_id_for_testing(held.get()));ui::AXActionData activate;activate.action=ax::mojom::Action::kDoDefault;held->HandleAccessibleAction(activate);EXPECT_EQ("work",window_->active_id());}
  ASSERT_TRUE(window_->Select("personal"));shell->ShowSidebar();EXPECT_EQ(logical,shell->sidebar_node_id_for_testing(shell->workspace_button("personal")));
  shell->SetWorkspaceEmoji("personal",u"");EXPECT_EQ(logical,shell->sidebar_node_id_for_testing(shell->workspace_button("personal")));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, FooterActionHostsOwnershipEmojiMenuKeysAXAndClip) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->SetWorkspaceAnimations(false);shell->SetPinned(true);shell->ShowSidebar();const bool rust=shell->workspace_input()->rust_editor();
  for(auto* view:{shell->footer_action_for_testing(0),shell->footer_action_for_testing(1),shell->workspace_button("work"),shell->workspace_button("personal")}){ASSERT_TRUE(view);EXPECT_EQ(!rust,views::AsViewClass<views::Button>(view)!=nullptr);ui::AXNodeData ax;view->GetViewAccessibility().GetAccessibleNodeData(&ax);EXPECT_EQ(ax::mojom::Role::kButton,ax.role);EXPECT_FALSE(ax.GetString16Attribute(ax::mojom::StringAttribute::kName).empty());}
  EXPECT_EQ(gfx::Size(24,28),shell->workspace_button("personal")->GetPreferredSize());views::ViewTracker old(shell->workspace_button("personal"));shell->SetWorkspaceEmoji("personal",u"🚀");if(rust)EXPECT_TRUE(old.view());else EXPECT_FALSE(old.view());auto* personal=shell->workspace_button("personal");ASSERT_TRUE(personal);EXPECT_EQ(gfx::Size(30,28),personal->GetPreferredSize());shell->sidebar_widget()->LayoutRootViewIfNecessary();
#if BUILDFLAG(IS_MAC)
  bool inspected=false;const auto inspect=base::BindRepeating([](WorkspaceShell* shell,bool* inspected){EXPECT_TRUE(shell->workspace_context_menu_running_for_testing());auto* menu=shell->workspace_menu();EXPECT_TRUE(menu);if(menu){auto* profiles=menu->GetSubmenuModelAt(0);EXPECT_TRUE(profiles);if(profiles){EXPECT_FALSE(profiles->IsItemCheckedAt(0));EXPECT_TRUE(profiles->IsItemCheckedAt(1));}}*inspected=true;},base::Unretained(shell),&inspected);
  EXPECT_TRUE(RunWithNativeMenuInspectionForTesting(base::BindOnce([](views::View* view){ui::AXActionData context;context.action=ax::mojom::Action::kShowContextMenu;EXPECT_TRUE(view->HandleAccessibleAction(context));},base::Unretained(personal)),inspect));EXPECT_TRUE(inspected);EXPECT_FALSE(shell->workspace_context_menu_running_for_testing());EXPECT_EQ("work",window_->active_id());
#endif

  auto* pin=shell->footer_action_for_testing(0);auto check_pin_name=[&]{ui::AXNodeData node;pin->GetViewAccessibility().GetAccessibleNodeData(&node);EXPECT_EQ(shell->pinned()?u"Odepnij panel (auto-ukrywanie)":u"Przypnij panel",node.GetString16Attribute(ax::mojom::StringAttribute::kName));};check_pin_name();ui::AXActionData activate;activate.action=ax::mojom::Action::kDoDefault;pin->SetEnabled(false);pin->HandleAccessibleAction(activate);EXPECT_TRUE(shell->pinned());pin->SetEnabled(true);EXPECT_TRUE(pin->HandleAccessibleAction(activate));EXPECT_FALSE(shell->pinned());check_pin_name();shell->SetPinned(true);check_pin_name();shell->ShowSidebar();
  ui::KeyEvent space(ui::EventType::kKeyPressed,ui::VKEY_SPACE,0);EXPECT_TRUE(shell->workspace_button("personal")->OnKeyPressed(space));EXPECT_EQ("personal",window_->active_id());shell->ShowSidebar();EXPECT_TRUE(shell->workspace_button("work")->HandleAccessibleAction(activate));EXPECT_EQ("work",window_->active_id());
  std::vector<std::string> added;for(int i=0;i<10;++i){ASSERT_TRUE(window_->AddWorkspace("Action "+std::to_string(i),"work-profile"));ASSERT_TRUE(base::test::RunUntil([&]{return !window_->profiles_busy();}));added.push_back(window_->workspaces().back().id);}ASSERT_TRUE(window_->Select("work"));shell->ShowSidebar();shell->sidebar_widget()->LayoutRootViewIfNecessary();auto* viewport=shell->workspace_viewport();ASSERT_TRUE(viewport);
  if(rust){ASSERT_GT(viewport->maximum(),0);viewport->ScrollTo(0);personal=shell->workspace_button("personal");ASSERT_TRUE(personal);const auto point=personal->GetLocalBounds().CenterPoint();ui::MouseEvent press(ui::EventType::kMousePressed,point,point,base::TimeTicks::Now(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);ui::MouseEvent release(ui::EventType::kMouseReleased,point,point,base::TimeTicks::Now(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);ASSERT_TRUE(personal->OnMousePressed(press));viewport->ScrollTo(viewport->maximum());EXPECT_TRUE(personal->GetVisibleBounds().IsEmpty());viewport->ScrollTo(0);personal->OnMouseReleased(release);EXPECT_EQ("work",window_->active_id());
    const auto target=added.back();auto* last=shell->workspace_button(target);ASSERT_TRUE(last);EXPECT_TRUE(last->GetVisibleBounds().IsEmpty());EXPECT_TRUE(last->HandleAccessibleAction(activate));EXPECT_EQ(target,window_->active_id());ASSERT_TRUE(base::test::RunUntil([&]{shell->sidebar_widget()->LayoutRootViewIfNecessary();auto* current=shell->workspace_button(target);return current&&!current->GetVisibleBounds().IsEmpty();}));EXPECT_GT(viewport->offset(),0);
    base::RunLoop().RunUntilIdle();const auto frames=shell->metal_sidebar_frame_count();base::RunLoop().RunUntilIdle();EXPECT_EQ(frames,shell->metal_sidebar_frame_count());
  }
}
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, FooterActionCallbackCanDeleteItsOwnHost) {
  auto delegate=std::make_unique<views::WidgetDelegate>();auto* host=delegate->SetContentsView(std::make_unique<views::View>());views::View* action=nullptr;int calls=0;action=host->AddChildView(window_->shell()->CreateWorkspaceActionForTesting(base::BindRepeating([](views::View* host,views::View** action,int* calls){++*calls;host->RemoveChildViewT(*action);},base::Unretained(host),&action,&calls)));action->SetBounds(0,0,30,28);views::ViewTracker tracked(action);
  views::Widget::InitParams params(views::Widget::InitParams::CLIENT_OWNS_WIDGET,views::Widget::InitParams::TYPE_BUBBLE);params.delegate=delegate.get();params.SetParent(window_->widget());auto widget=std::make_unique<views::Widget>();widget->Init(std::move(params));widget->SetBounds(gfx::Rect(40,40,200,120));widget->Show();ui::MouseEvent press(ui::EventType::kMousePressed,gfx::Point(10,10),gfx::Point(10,10),base::TimeTicks::Now(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);ui::MouseEvent release(ui::EventType::kMouseReleased,gfx::Point(10,10),gfx::Point(10,10),base::TimeTicks::Now(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);ASSERT_TRUE(action->OnMousePressed(press));action->OnMouseReleased(release);EXPECT_EQ(1,calls);EXPECT_FALSE(tracked.view());widget->CloseNow();
}

// Compile only: runtime acceptance remains deferred to the bulk UI checkpoint.
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, SplitPaneLogicalNodesFollowTabIdentityAndRetireWhenHidden) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->SetWorkspaceAnimations(false);
  const auto url=embedded_test_server()->GetURL("/storage_probe.html?split-node");for(int i=0;i<3;++i){ASSERT_TRUE(window_->NavigateFromBar(url,i!=0));ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));}
  window_->SelectTab(1);ASSERT_TRUE(window_->SplitTab(2));window_->widget()->LayoutRootViewIfNecessary();const bool rust=shell->workspace_input()->rust_editor();
  auto* menu=shell->split_pane_button(true,false);auto* close=shell->split_pane_button(true,true);ASSERT_TRUE(menu);ASSERT_TRUE(close);const auto menu_id=shell->sidebar_node_id_for_testing(menu),close_id=shell->sidebar_node_id_for_testing(close);
  if(rust){EXPECT_NE(0u,menu_id);EXPECT_NE(0u,close_id);EXPECT_NE(menu_id,close_id);}else EXPECT_EQ(0u,close_id);
  ASSERT_TRUE(content::ExecJs(window_->contents("work"),"document.title='Split logical title';"));ASSERT_TRUE(base::test::RunUntil([&]{return shell->tab_button(1)->GetTooltipText()==u"Split logical title"||shell->tab_button(2)->GetTooltipText()==u"Split logical title";}));EXPECT_EQ(menu_id,shell->sidebar_node_id_for_testing(menu));EXPECT_EQ(close_id,shell->sidebar_node_id_for_testing(close));
  const auto point=close->GetLocalBounds().CenterPoint();if(rust){ui::MouseEvent press(ui::EventType::kMousePressed,point,point,base::TimeTicks::Now(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);ASSERT_TRUE(close->OnMousePressed(press));}
  window_->CloseTab(0);ASSERT_TRUE(base::test::RunUntil([&]{return window_->tab_count("work")==2&&window_->split();}));window_->widget()->LayoutRootViewIfNecessary();EXPECT_EQ(close,shell->split_pane_button(true,true));EXPECT_EQ(close_id,shell->sidebar_node_id_for_testing(close));
  if(rust){ui::MouseEvent release(ui::EventType::kMouseReleased,point,point,base::TimeTicks::Now(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);close->OnMouseReleased(release);base::RunLoop().RunUntilIdle();EXPECT_EQ(2u,window_->tab_count("work"));}
  window_->SwapSplit();window_->widget()->LayoutRootViewIfNecessary();const auto swapped=shell->sidebar_node_id_for_testing(close);if(rust){EXPECT_NE(0u,swapped);EXPECT_NE(close_id,swapped);}
  window_->Unsplit(0);window_->widget()->LayoutRootViewIfNecessary();EXPECT_EQ(0u,shell->sidebar_node_id_for_testing(menu));EXPECT_EQ(0u,shell->sidebar_node_id_for_testing(close));
  window_->SelectTab(0);ASSERT_TRUE(window_->SplitTab(1));window_->widget()->LayoutRootViewIfNecessary();if(rust){EXPECT_NE(0u,shell->sidebar_node_id_for_testing(close));EXPECT_NE(swapped,shell->sidebar_node_id_for_testing(close));}
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, SplitPaneActionHostsMenuKeyboardAXAndRetarget) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->SetWorkspaceAnimations(false);
  const auto url=embedded_test_server()->GetURL("/storage_probe.html?split-actions");
  ASSERT_TRUE(window_->NavigateFromBar(url,false));ASSERT_TRUE(window_->NavigateFromBar(url,true));ASSERT_TRUE(window_->NavigateFromBar(url,true));
  window_->SelectTab(0);ASSERT_TRUE(window_->SplitTab(1));window_->widget()->LayoutRootViewIfNecessary();const bool rust=shell->workspace_input()->rust_editor();
  for(bool right:{false,true})for(bool close:{false,true}){auto* view=shell->split_pane_button(right,close);ASSERT_TRUE(view);EXPECT_EQ(!rust,views::AsViewClass<views::Button>(view)!=nullptr);ui::AXNodeData ax;view->GetViewAccessibility().GetAccessibleNodeData(&ax);EXPECT_EQ(ax::mojom::Role::kButton,ax.role);EXPECT_EQ(close?u"Zamknij kartę w Split View":u"Opcje Split View",ax.GetString16Attribute(ax::mojom::StringAttribute::kName));EXPECT_EQ(gfx::Size(32,32),view->GetPreferredSize());}
#if BUILDFLAG(IS_MAC)
  bool inspected=false;
  EXPECT_TRUE(RunWithNativeMenuInspectionForTesting(base::BindOnce([](views::View* view){ui::AXActionData action;action.action=ax::mojom::Action::kDoDefault;EXPECT_TRUE(view->HandleAccessibleAction(action));},base::Unretained(shell->split_pane_button(true,false))),base::BindRepeating([](WorkspaceShell* shell,bool* inspected){EXPECT_TRUE(shell->workspace_context_menu_running_for_testing());auto* menu=shell->workspace_menu();EXPECT_TRUE(menu);if(menu)EXPECT_GT(menu->GetItemCount(),3u);*inspected=true;},base::Unretained(shell),&inspected)));
  EXPECT_TRUE(inspected);EXPECT_FALSE(shell->workspace_context_menu_running_for_testing());EXPECT_EQ(3u,window_->tab_count("work"));
#endif
  auto* close=shell->split_pane_button(true,true);ui::AXActionData activate;activate.action=ax::mojom::Action::kDoDefault;close->SetEnabled(false);close->HandleAccessibleAction(activate);base::RunLoop().RunUntilIdle();EXPECT_EQ(3u,window_->tab_count("work"));close->SetEnabled(true);
  if(rust){
    auto press=[&]{const auto point=close->GetLocalBounds().CenterPoint();ui::MouseEvent event(ui::EventType::kMousePressed,point,point,base::TimeTicks::Now(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);return close->OnMousePressed(event);};
    auto release=[&]{const auto point=close->GetLocalBounds().CenterPoint();ui::MouseEvent event(ui::EventType::kMouseReleased,point,point,base::TimeTicks::Now(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);close->OnMouseReleased(event);base::RunLoop().RunUntilIdle();EXPECT_EQ(3u,window_->tab_count("work"));};
    ASSERT_TRUE(press());window_->SwapSplit();window_->widget()->LayoutRootViewIfNecessary();release();
    ASSERT_TRUE(press());window_->SetSplitVertical(true);window_->widget()->LayoutRootViewIfNecessary();release();
    ASSERT_TRUE(press());window_->Unsplit(0);window_->SelectTab(0);ASSERT_TRUE(window_->SplitTab(2));window_->widget()->LayoutRootViewIfNecessary();release();
    ASSERT_TRUE(press());window_->SelectTab(1);window_->SelectTab(0);window_->widget()->LayoutRootViewIfNecessary();release();
  }
  // This host lives in the activatable main window: verify actual Cocoa Space
  // delivery as well as AX activation above and the existing physical mouse case.
  ui_controls::EnableUIControls();ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(window_->widget()->GetNativeWindow()));close->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);close->RequestFocus();ASSERT_TRUE(base::test::RunUntil([&]{return close->HasFocus();}));
  base::test::TestFuture<void> presented;auto* compositor=window_->widget()->GetCompositor();ASSERT_TRUE(compositor);window_->widget()->GetRootView()->SchedulePaint();compositor->RequestSuccessfulPresentationTimeForNextFrame(base::BindOnce([](base::OnceClosure done,const viz::FrameTimingDetails&){std::move(done).Run();},presented.GetCallback()));compositor->ScheduleFullRedraw();ASSERT_TRUE(presented.Wait());
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(window_->widget()->GetNativeWindow(),ui::VKEY_SPACE,false,false,false,false));ASSERT_TRUE(base::test::RunUntil([&]{return window_->tab_count("work")==2&&!window_->split();}));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, SplitPaneActionQueuedCloseSurvivesIndexShiftAndDeletion) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->SetWorkspaceAnimations(false);
  const auto a=embedded_test_server()->GetURL("/storage_probe.html?queue-a"),b=embedded_test_server()->GetURL("/storage_probe.html?queue-b"),c=embedded_test_server()->GetURL("/storage_probe.html?queue-c");
  ASSERT_TRUE(window_->NavigateFromBar(a,false));ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));ASSERT_TRUE(window_->NavigateFromBar(b,true));ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));ASSERT_TRUE(window_->NavigateFromBar(c,true));ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));window_->SelectTab(1);ASSERT_TRUE(window_->SplitTab(2));window_->widget()->LayoutRootViewIfNecessary();
  ui::AXActionData activate;activate.action=ax::mojom::Action::kDoDefault;EXPECT_TRUE(shell->split_pane_button(true,true)->HandleAccessibleAction(activate));window_->CloseTab(0);
  ASSERT_TRUE(base::test::RunUntil([&]{return window_->tab_count("work")==1;}));EXPECT_EQ(b,window_->contents("work")->GetLastCommittedURL());
  ASSERT_TRUE(window_->NavigateFromBar(a,true));ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));ASSERT_TRUE(window_->NavigateFromBar(c,true));ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));window_->SelectTab(0);ASSERT_TRUE(window_->SplitTab(1));window_->widget()->LayoutRootViewIfNecessary();
  EXPECT_TRUE(shell->split_pane_button(true,true)->HandleAccessibleAction(activate));window_->CloseTab(1);base::RunLoop().RunUntilIdle();EXPECT_EQ(2u,window_->tab_count("work"));window_->SelectTab(1);EXPECT_EQ(c,window_->contents("work")->GetLastCommittedURL());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, HeaderActionHostOwnershipKeyboardAXGestureAndCancel) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->SetWorkspaceAnimations(false);const bool rust=shell->workspace_input()->rust_editor();
  const std::array<std::u16string,4> names={u"Pokaż lub ukryj karty",u"Wstecz",u"Dalej",u"Odśwież"};for(size_t i=0;i<4;++i){auto* view=shell->header_action_for_testing(i);ASSERT_TRUE(view);EXPECT_EQ(!rust,views::AsViewClass<views::Button>(view)!=nullptr);ui::AXNodeData ax;view->GetViewAccessibility().GetAccessibleNodeData(&ax);EXPECT_EQ(ax::mojom::Role::kButton,ax.role);EXPECT_EQ(names[i],ax.GetString16Attribute(ax::mojom::StringAttribute::kName));}
  auto reset=[&]{shell->SetPinned(true);shell->ShowSidebar();};reset();auto* action=shell->header_action_for_testing(0);ui_controls::EnableUIControls();ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(window_->widget()->GetNativeWindow()));action->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);action->RequestFocus();ASSERT_TRUE(base::test::RunUntil([&]{return action->HasFocus();}));
  base::test::TestFuture<void> presented;auto* compositor=window_->widget()->GetCompositor();ASSERT_TRUE(compositor);window_->widget()->GetRootView()->SchedulePaint();compositor->RequestSuccessfulPresentationTimeForNextFrame(base::BindOnce([](base::OnceClosure done,const viz::FrameTimingDetails&){std::move(done).Run();},presented.GetCallback()));compositor->ScheduleFullRedraw();ASSERT_TRUE(presented.Wait());
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(window_->widget()->GetNativeWindow(),ui::VKEY_SPACE,false,false,false,false));EXPECT_FALSE(shell->sidebar_visible());reset();ui::KeyEvent enter(ui::EventType::kKeyPressed,ui::VKEY_RETURN,0);EXPECT_FALSE(action->OnKeyPressed(enter));EXPECT_TRUE(shell->sidebar_visible());
  ui::AXActionData activate;activate.action=ax::mojom::Action::kDoDefault;EXPECT_TRUE(action->HandleAccessibleAction(activate));EXPECT_FALSE(shell->sidebar_visible());reset();action->SetEnabled(false);ui::AXNodeData disabled;action->GetViewAccessibility().GetAccessibleNodeData(&disabled);EXPECT_FALSE(disabled.HasIntAttribute(ax::mojom::IntAttribute::kDefaultActionVerb));action->HandleAccessibleAction(activate);EXPECT_TRUE(shell->sidebar_visible());action->SetEnabled(true);
  const auto point=action->GetLocalBounds().CenterPoint();auto gesture=[&](ui::EventType type){ui::GestureEvent event(point.x(),point.y(),0,base::TimeTicks::Now(),ui::GestureEventDetails(type));action->OnGestureEvent(&event);EXPECT_EQ(type!=ui::EventType::kGestureEnd&&type!=ui::EventType::kGestureTapCancel,event.handled());};gesture(ui::EventType::kGestureTapDown);gesture(ui::EventType::kGestureTap);gesture(ui::EventType::kGestureEnd);EXPECT_FALSE(shell->sidebar_visible());reset();
  if(rust){ui::MouseEvent press(ui::EventType::kMousePressed,point,point,base::TimeTicks::Now(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);ui::MouseEvent release(ui::EventType::kMouseReleased,point,point,base::TimeTicks::Now(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);ASSERT_TRUE(action->OnMousePressed(press));action->SetVisible(false);action->SetVisible(true);action->OnMouseReleased(release);EXPECT_TRUE(shell->sidebar_visible());ASSERT_TRUE(action->OnMousePressed(press));action->SetEnabled(false);action->SetEnabled(true);action->OnMouseReleased(release);EXPECT_TRUE(shell->sidebar_visible());ASSERT_TRUE(window_->NavigateFromBar(embedded_test_server()->GetURL("/storage_probe.html?hidden-header-action"),false));ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));ASSERT_TRUE(content::ExecJs(window_->contents("work"),"window.header_hidden_marker=42;"));window_->widget()->Hide();shell->header_action_for_testing(3)->HandleAccessibleAction(activate);window_->widget()->Show();ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));EXPECT_EQ(42,content::EvalJs(window_->contents("work"),"window.header_hidden_marker"));}
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, ResizeHostsOwnershipCursorAXAndCancelledInput) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->SetWorkspaceAnimations(false);shell->SetPinned(true);shell->ShowSidebar();const bool rust=shell->workspace_input()->rust_editor();auto* grip=shell->sidebar_grip_for_testing();ASSERT_TRUE(grip);EXPECT_EQ(!rust,views::AsViewClass<views::ResizeArea>(grip)!=nullptr);
  ui::MouseEvent move(ui::EventType::kMouseMoved,gfx::Point(3,20),gfx::Point(3,20),base::TimeTicks::Now(),0,0);auto check=[&](views::View* view,ui::mojom::CursorType cursor){ui::AXNodeData ax;view->GetViewAccessibility().GetAccessibleNodeData(&ax);EXPECT_EQ(ax::mojom::Role::kSplitter,ax.role);EXPECT_FALSE(ax.GetString16Attribute(ax::mojom::StringAttribute::kName).empty());EXPECT_EQ(cursor,view->GetCursor(move).type());view->SetEnabled(false);EXPECT_EQ(ui::mojom::CursorType::kNull,view->GetCursor(move).type());view->SetEnabled(true);};
  ASSERT_NO_FATAL_FAILURE(check(grip,ui::mojom::CursorType::kEastWestResize));
  const auto url=embedded_test_server()->GetURL("/storage_probe.html?resize-host");ASSERT_TRUE(window_->NavigateFromBar(url,false));ASSERT_TRUE(window_->NavigateFromBar(url,true));window_->SelectTab(0);ASSERT_TRUE(window_->SplitTab(1));window_->widget()->LayoutRootViewIfNecessary();auto* divider=shell->split_divider_for_testing();ASSERT_TRUE(divider);EXPECT_EQ(!rust,views::AsViewClass<views::ResizeArea>(divider)!=nullptr);
  ASSERT_NO_FATAL_FAILURE(check(divider,ui::mojom::CursorType::kEastWestResize));
  window_->SetSplitVertical(true);window_->widget()->LayoutRootViewIfNecessary();
  ASSERT_NO_FATAL_FAILURE(check(divider,ui::mojom::CursorType::kNorthSouthResize));
  if(rust){
    ui::MouseEvent press(ui::EventType::kMousePressed,gfx::Point(3,3),gfx::Point(3,3),base::TimeTicks::Now(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);ui::MouseEvent late(ui::EventType::kMouseDragged,gfx::Point(3,90),gfx::Point(3,90),base::TimeTicks::Now(),ui::EF_LEFT_MOUSE_BUTTON,0);
    const double ratio=window_->split_ratio();ASSERT_TRUE(divider->OnMousePressed(press));divider->SetVisible(false);divider->SetVisible(true);EXPECT_TRUE(divider->OnMouseDragged(late));divider->OnMouseCaptureLost();EXPECT_DOUBLE_EQ(ratio,window_->split_ratio());
    ASSERT_TRUE(divider->OnMousePressed(press));window_->SetSplitVertical(false);EXPECT_TRUE(divider->OnMouseDragged(late));divider->OnMouseCaptureLost();EXPECT_DOUBLE_EQ(ratio,window_->split_ratio());
    const int width=shell->sidebar_width();ASSERT_TRUE(grip->OnMousePressed(press));grip->SetVisible(false);grip->SetVisible(true);ui::MouseEvent lateral(ui::EventType::kMouseDragged,gfx::Point(90,3),gfx::Point(90,3),base::TimeTicks::Now(),ui::EF_LEFT_MOUSE_BUTTON,0);EXPECT_TRUE(grip->OnMouseDragged(lateral));grip->OnMouseCaptureLost();EXPECT_EQ(width,shell->sidebar_width());
  }
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, MainSidebarViewportOwnershipResizeAndWorkspaceClamp) {
  window_->widget()->Show();auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->SetWorkspaceAnimations(false);shell->SetPinned(true);const auto url=embedded_test_server()->GetURL("/storage_probe.html");for(int i=0;i<28;++i)ASSERT_TRUE(window_->NavigateFromBar(url,true));shell->CloseOverlays(true);shell->ShowSidebar();auto* viewport=shell->tab_viewport();ASSERT_TRUE(viewport);EXPECT_EQ(shell->tab_scroll_view(),viewport->view());EXPECT_EQ(shell->workspace_input()->rust_editor(),viewport->rust_owned());if(viewport->rust_owned())EXPECT_FALSE(views::AsViewClass<views::ScrollView>(viewport->view()));ASSERT_TRUE(viewport->vertical_scroll_bar());
  ASSERT_TRUE(base::test::RunUntil([&]{shell->sidebar_widget()->LayoutRootViewIfNecessary();return viewport->maximum()>0;}));ui::AXNodeData ax;viewport->view()->GetViewAccessibility().GetAccessibleNodeData(&ax);EXPECT_EQ(ax::mojom::Role::kScrollView,ax.role);
  for(auto size:{gfx::Size(700,660),gfx::Size(1000,860)}){window_->widget()->SetBounds(gfx::Rect(gfx::Point(40,40),size));shell->ShowSidebar();shell->sidebar_widget()->LayoutRootViewIfNecessary();
    // Native Layer::SetScrollOffset can still use the pre-resize impl bounds
    // until the layer commit. Require presentation of the new viewport first.
    base::test::TestFuture<void> presented;auto* compositor=shell->sidebar_widget()->GetCompositor();ASSERT_TRUE(compositor);shell->sidebar_widget()->GetRootView()->SchedulePaint();compositor->RequestSuccessfulPresentationTimeForNextFrame(base::BindOnce([](base::OnceClosure done,const viz::FrameTimingDetails&){std::move(done).Run();},presented.GetCallback()));compositor->ScheduleFullRedraw();ASSERT_TRUE(presented.Wait());
    EXPECT_EQ(std::max(0,viewport->contents()->height()-viewport->visible_rect().height()),viewport->maximum());viewport->ScrollTo(viewport->maximum());EXPECT_DOUBLE_EQ(viewport->maximum(),viewport->offset());auto* first=shell->tab_button(0);ASSERT_TRUE(first);first->ScrollRectToVisible(first->GetLocalBounds());ASSERT_TRUE(base::test::RunUntil([&]{return !first->GetVisibleBounds().IsEmpty();}));}
  viewport->ScrollTo(viewport->maximum());ASSERT_GT(viewport->offset(),0);ASSERT_TRUE(window_->Select("personal"));shell->ShowSidebar();ASSERT_TRUE(base::test::RunUntil([&]{shell->sidebar_widget()->LayoutRootViewIfNecessary();return viewport->maximum()==0&&viewport->offset()==0;}));ASSERT_TRUE(window_->Select("work"));shell->ShowSidebar();ASSERT_TRUE(base::test::RunUntil([&]{shell->sidebar_widget()->LayoutRootViewIfNecessary();return viewport->maximum()>0;}));EXPECT_DOUBLE_EQ(0,viewport->offset());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, PaletteViewportOwnershipResizeSelectionAndShrink) {
  auto* shell=window_->shell();window_->widget()->Show();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->SetWorkspaceAnimations(false);ASSERT_TRUE(window_->NavigateFromBar(embedded_test_server()->GetURL("/storage_probe.html?viewport-sentinel"),false));ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));ASSERT_TRUE(content::ExecJs(window_->contents("work"),"document.title='Viewport probe sentinel';"));shell->OpenCommandPalette();SetPaletteQuery(u"Viewport probe");
  // Let the debounced search reach the real model before injecting long results.
  ASSERT_TRUE(base::test::RunUntil([&]{return std::any_of(shell->search_results().begin(),shell->search_results().end(),[](const auto& item){return item.title==u"Viewport probe sentinel";});}));
  auto* service=HistoryServiceFactory::GetForProfile(work_,ServiceAccessType::EXPLICIT_ACCESS);base::CancelableTaskTracker tasks;history::QueryOptions options;options.max_count=1;base::test::TestFuture<history::QueryResults> queried;service->QueryHistory(u"Viewport probe",options,queried.GetCallback(),&tasks);ASSERT_TRUE(queried.Wait());base::RunLoop().RunUntilIdle();
  std::vector<PaletteResult> results;for(int i=0;i<30;++i){PaletteResult item;item.title=u"Viewport probe 日本語 "+base::NumberToString16(i);item.detail=u"History / 🦀";item.url=GURL("https://viewport.test/"+base::NumberToString(i));results.push_back(std::move(item));}
  const auto profile=window_->workspaces().front().profile_id;shell->SetSearchResults(u"Viewport probe",profile,results);auto* viewport=shell->palette_viewport();ASSERT_TRUE(viewport);EXPECT_EQ(shell->palette_results_view(),viewport->contents());EXPECT_EQ(shell->workspace_input()->rust_editor(),viewport->rust_owned());if(viewport->rust_owned())EXPECT_FALSE(views::AsViewClass<views::ScrollView>(viewport->view()));ASSERT_TRUE(viewport->vertical_scroll_bar());
  ui_controls::EnableUIControls();auto* panel=shell->address_input()->view()->GetWidget();auto present=[&]{if(!ui_test_utils::ShowAndFocusNativeWindow(panel->GetNativeWindow()))return false;shell->address_input()->RequestFocus();base::test::TestFuture<void> done;auto* compositor=panel->GetCompositor();if(!compositor)return false;panel->GetRootView()->SchedulePaint();compositor->RequestSuccessfulPresentationTimeForNextFrame(base::BindOnce([](base::OnceClosure callback,const viz::FrameTimingDetails&){std::move(callback).Run();},done.GetCallback()));compositor->ScheduleFullRedraw();return done.Wait();};
  for(auto size:{gfx::Size(700,480),gfx::Size(1000,860)}){SCOPED_TRACE(size.ToString());window_->widget()->SetBounds(gfx::Rect(gfx::Point(40,40),size));panel->LayoutRootViewIfNecessary();ASSERT_TRUE(present());
    ASSERT_GE(shell->suggestions().size(),30u);const int cap=std::clamp(shell->content_host()->height()-230,90,300);EXPECT_EQ(cap,viewport->view()->GetPreferredSize().height());EXPECT_EQ(cap,viewport->visible_rect().height());ASSERT_GT(viewport->maximum(),0);viewport->ScrollTo(viewport->maximum());EXPECT_DOUBLE_EQ(viewport->maximum(),viewport->offset());auto* first=viewport->contents()->children().front().get();first->ScrollViewToVisible();EXPECT_DOUBLE_EQ(first->y(),viewport->offset());EXPECT_FALSE(first->GetVisibleBounds().IsEmpty());
    ui::KeyEvent up(ui::EventType::kKeyPressed,ui::VKEY_UP,0);EXPECT_TRUE(shell->address_input()->view()->OnKeyPressed(up));EXPECT_EQ(shell->suggestions().size()-1,shell->palette_selection_for_testing());ASSERT_TRUE(base::test::RunUntil([&]{return viewport->offset()>0;}));EXPECT_FALSE(viewport->contents()->children().back()->GetVisibleBounds().IsEmpty());ui::KeyEvent down(ui::EventType::kKeyPressed,ui::VKEY_DOWN,0);EXPECT_TRUE(shell->address_input()->view()->OnKeyPressed(down));EXPECT_EQ(0u,shell->palette_selection_for_testing());EXPECT_FALSE(viewport->contents()->children().front()->GetVisibleBounds().IsEmpty());EXPECT_LE(viewport->offset(),viewport->contents()->children().front()->y());
  }
  viewport->ScrollTo(viewport->maximum());shell->SetSearchResults(u"Viewport probe",profile,{});panel->LayoutRootViewIfNecessary();EXPECT_EQ(0,viewport->maximum());EXPECT_DOUBLE_EQ(0,viewport->offset());
  SetPaletteQuery(u"> no-such-viewport-command");EXPECT_TRUE(shell->suggestions().empty());EXPECT_FALSE(viewport->view()->GetVisible());ASSERT_TRUE(base::test::RunUntil([&]{return !shell->metal_palette_visible();}));shell->CloseOverlays();EXPECT_FALSE(shell->address_visible());shell->OpenCommandPalette();SetPaletteQuery(u"Viewport probe");EXPECT_GE(viewport->offset(),0);EXPECT_LE(viewport->offset(),viewport->maximum());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, ToolsViewportWheelKeyboardAccessibleAndReveal) {
  auto* shell=window_->shell();shell->OpenTools(2);auto* viewport=shell->tools_viewport_for_testing();ASSERT_TRUE(viewport);EXPECT_EQ(shell->workspace_input()->rust_editor(),viewport->rust_owned());if(viewport->rust_owned())EXPECT_FALSE(views::AsViewClass<views::ScrollView>(viewport->view()));
  auto* first=shell->provider_input(0)->view();auto* spacer=viewport->contents()->AddChildView(std::make_unique<views::View>());spacer->SetPreferredSize(gfx::Size(1,1400));shell->tools_widget()->LayoutRootViewIfNecessary();ASSERT_GT(viewport->maximum(),0);viewport->ScrollTo(0);
  ui::MouseWheelEvent wheel(gfx::Vector2d(0,-120),gfx::PointF(10,10),gfx::PointF(10,10),base::TimeTicks::Now(),0,0);EXPECT_TRUE(viewport->view()->OnMouseWheel(wheel));EXPECT_GT(viewport->offset(),0);
  ui::KeyEvent home(ui::EventType::kKeyPressed,ui::VKEY_HOME,ui::EF_NONE);EXPECT_TRUE(viewport->view()->OnKeyPressed(home));EXPECT_EQ(0,viewport->offset());
  ui::AXActionData down;down.action=ax::mojom::Action::kScrollDown;EXPECT_TRUE(viewport->view()->HandleAccessibleAction(down));EXPECT_GT(viewport->offset(),0);viewport->ScrollTo(viewport->maximum());EXPECT_TRUE(first->GetVisibleBounds().IsEmpty());first->ScrollRectToVisible(first->GetLocalBounds());EXPECT_FALSE(first->GetVisibleBounds().IsEmpty());
  ui::AXNodeData ax;viewport->view()->GetViewAccessibility().GetAccessibleNodeData(&ax);EXPECT_EQ(ax::mojom::Role::kScrollView,ax.role);EXPECT_EQ(viewport->maximum(),ax.GetIntAttribute(ax::mojom::IntAttribute::kScrollYMax));
  shell->tools_widget()->Hide();shell->OpenTools(2);EXPECT_GE(viewport->offset(),0);EXPECT_LE(viewport->offset(),viewport->maximum());
}
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, ToolsPanelResizeLongStatusAndViewportGeometry) {
  ASSERT_TRUE(window_->Select("work"));auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_ready();}));
  const bool rust=shell->workspace_input()->rust_editor();
  for(int page=0;page<4;++page){shell->OpenTools(page);if(page==3)ASSERT_TRUE(base::test::RunUntil([&]{return shell->history_loaded();}));
    for(auto size:{gfx::Size(420,560),gfx::Size(680,720)}){
      shell->SetOperationStatus(u"Długi status 🦀 אבג · sprawdzenie zawijania, zmiany rozmiaru i widoczności przewijanego formularza bez nachodzenia na nagłówek ani stopkę.");
      shell->tools_widget()->SetSize(size);shell->tools_widget()->LayoutRootViewIfNecessary();
      auto* field=page==0?shell->snapshot_input():page==1?shell->shortcut_input():page==2?shell->provider_input(0):shell->history_search();ASSERT_TRUE(field);
      auto* scroll=shell->tools_viewport_for_testing();ASSERT_TRUE(scroll);
      auto* root=scroll->view()->parent();ASSERT_TRUE(root);ASSERT_EQ(3u,root->children().size());auto* heading=root->children()[0].get();EXPECT_EQ(scroll->view(),root->children()[1].get());auto* status=root->children()[2].get();
      const auto insets=root->GetInsets()+gfx::Insets(20);EXPECT_EQ(insets.left(),heading->x());EXPECT_EQ(insets.top(),heading->y());EXPECT_EQ(root->width()-insets.width(),heading->width());
      EXPECT_EQ(heading->bounds().bottom()+12,scroll->view()->y());EXPECT_EQ(scroll->view()->bounds().bottom()+12,status->y());EXPECT_EQ(root->height()-insets.bottom(),status->bounds().bottom());
      EXPECT_EQ(root->width()-insets.width(),status->width());EXPECT_GT(scroll->view()->height(),0);ASSERT_TRUE(scroll->contents());EXPECT_GE(scroll->contents()->height(),scroll->view()->height());
      if(rust){field->view()->ScrollRectToVisible(field->view()->GetLocalBounds());ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_chrome_order_correct_for_testing()&&shell->tools_chrome_excludes_for_testing(field->view());}));EXPECT_TRUE(shell->tools_chrome_excludes_for_testing(shell->tools_close_for_testing()));}
    }
  }
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, ToolsPolicyFailedActionsRecorderAndHistoryStatuses) {
  ASSERT_TRUE(window_->Select("work"));auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_ready();}));
  auto has_copy=[&](std::u16string_view text){auto find=[&](auto&& self,views::View* view)->bool{ui::AXNodeData node;view->GetViewAccessibility().GetAccessibleNodeData(&node);if(node.role==ax::mojom::Role::kStaticText&&node.GetString16Attribute(ax::mojom::StringAttribute::kName)==text)return true;for(const auto& child:view->children())if(self(self,child.get()))return true;return false;};return find(find,shell->tools_widget()->GetContentsView());};
  auto click=[&](std::u16string text){auto* button=FindButton(shell->tools_widget()->GetContentsView(),text);ASSERT_TRUE(button);ClickControl(button);base::RunLoop().RunUntilIdle();};
  shell->OpenTools(2);auto* provider=shell->provider_input(0);ASSERT_TRUE(provider);views::ViewTracker original(provider->view());
  shell->provider_input(0)->SetText(u"Invalid provider 🦀");shell->provider_input(1)->SetText(u"broken");shell->provider_input(2)->SetText(u"not a search template");
  const auto count=shell->browser_tools().providers.size();ASSERT_NO_FATAL_FAILURE(click(u"Zapisz wyszukiwarkę"));
  EXPECT_EQ(count,shell->browser_tools().providers.size());EXPECT_TRUE(original.view());
  EXPECT_TRUE(has_copy(u"Sprawdź unikalny prefiks, nazwę i adres z {query}. Musi pozostać co najmniej jedna wyszukiwarka."));
  shell->OpenTools(1);auto* chord=shell->shortcut_input();ASSERT_TRUE(chord);const auto previous=shell->browser_tools().shortcuts.front().accelerator;
  chord->SetText(u"Escape");ASSERT_NO_FATAL_FAILURE(click(u"Zapisz skrót"));EXPECT_EQ(previous,shell->browser_tools().shortcuts.front().accelerator);
  EXPECT_TRUE(has_copy(u"Nieprawidłowa lub zajęta kombinacja. Zmień ją albo wyłącz skrót drugiej akcji."));
  chord->RequestFocus();ui::CompositionText marked;marked.text=u"未";marked.selection=gfx::Range(1);chord->SetCompositionText(marked);ASSERT_TRUE(chord->IsIMEComposing());
  ASSERT_NO_FATAL_FAILURE(click(u"Nagraj kombinację…"));EXPECT_FALSE(chord->IsIMEComposing());EXPECT_EQ(ui::TEXT_INPUT_TYPE_NONE,chord->input_client()->GetTextInputType());
  EXPECT_TRUE(has_copy(u"Naciśnij kombinację. Escape anuluje nagrywanie."));
  ASSERT_TRUE(shell->CaptureShortcut(ui::Accelerator(ui::VKEY_ESCAPE,0)));EXPECT_EQ(ui::TEXT_INPUT_TYPE_TEXT,chord->input_client()->GetTextInputType());EXPECT_TRUE(has_copy(u"Nagrywanie anulowane."));
  shell->OpenTools(3);ASSERT_TRUE(base::test::RunUntil([&]{return shell->history_loaded();}));const auto profile=window_->workspaces().front().profile_id;
  shell->SetHistoryResults(u"",profile,200,{},false,false);EXPECT_TRUE(has_copy(u"Brak historii przeglądania w tym profilu."));EXPECT_TRUE(has_copy(u"Wyświetlono wpisów: 0"));
  auto* field=shell->history_search();field->RequestFocus();field->input_client()->InsertText(u"Missing 🦀",ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->history_loaded();}));EXPECT_TRUE(has_copy(u"Brak wyników dla tego wyszukiwania."));
  shell->SetHistoryResults(u"Missing 🦀",profile,200,{},true,true);EXPECT_TRUE(has_copy(u"Nie udało się odczytać historii."));EXPECT_TRUE(has_copy(u"Spróbuj ponownie otworzyć historię."));
  auto* more=FindButton(shell->tools_widget()->GetContentsView(),u"Pokaż więcej");ASSERT_TRUE(more);EXPECT_FALSE(more->GetVisible());EXPECT_FALSE(more->GetEnabled());
  const std::vector<BrowsingHistoryEntry> entry={{GURL("https://tools-policy.test/"),u"Result 🦀",base::Time::Now()}};
  shell->SetHistoryResults(u"Missing 🦀",profile,200,entry,true,false);EXPECT_TRUE(has_copy(u"Wyświetlono wpisów: 1"));EXPECT_TRUE(more->GetVisible());EXPECT_TRUE(more->GetEnabled());
  shell->SetHistoryResults(u"Missing 🦀",profile,200,entry,true,true);EXPECT_EQ(1u,shell->history_entries().size());EXPECT_TRUE(has_copy(u"Spróbuj ponownie otworzyć historię."));EXPECT_FALSE(more->GetVisible());EXPECT_FALSE(more->GetEnabled());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, ToolsActionsCocoaSnapshotProviderAndClose) {
  ASSERT_TRUE(window_->Select("work"));ASSERT_TRUE(window_->contents("work"));
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_ready()&&shell->appearance_ready();}));
  const bool rust=shell->workspace_input()->rust_editor();ui_controls::EnableUIControls();
  auto activate=[&](std::u16string title){
    SCOPED_TRACE(base::UTF16ToUTF8(title));
    auto* button=FindButton(shell->tools_widget()->GetContentsView(),title);ASSERT_TRUE(button);
    EXPECT_EQ(rust,!views::AsViewClass<views::MdTextButton>(button));
    ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->tools_widget()->GetNativeWindow()));
    shell->tools_widget()->LayoutRootViewIfNecessary();button->RequestFocus();ASSERT_TRUE(button->HasFocus());
    base::test::TestFuture<void> presented;auto* compositor=shell->tools_widget()->GetCompositor();ASSERT_TRUE(compositor);
    ASSERT_TRUE(base::test::RunUntil([&]{return compositor->IsVisible()&&!compositor->size().IsEmpty();}));
    shell->tools_widget()->GetContentsView()->SchedulePaint();
    compositor->RequestSuccessfulPresentationTimeForNextFrame(base::BindOnce([](base::OnceClosure done,const viz::FrameTimingDetails&){std::move(done).Run();},presented.GetCallback()));compositor->ScheduleFullRedraw();ASSERT_TRUE(presented.Wait());
    button->ScrollRectToVisible(button->GetLocalBounds());ASSERT_TRUE(button->HasFocus());
    ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(shell->tools_widget()->GetNativeWindow(),ui::VKEY_SPACE,false,false,false,false));
  };
  shell->OpenTools(0);shell->snapshot_input()->SetText(u"Action snapshot 🦀");
  auto named_snapshots=[&]{const auto items=window_->TimeMachine().snapshots;return std::count_if(items.begin(),items.end(),[](const auto& item){return item.title==u"Action snapshot 🦀";});};
  const auto snapshots=named_snapshots();
  views::ViewTracker saved_button(FindButton(shell->tools_widget()->GetContentsView(),u"Zapisz snapshot"));
  auto messages=[&]{std::u16string text;auto visit=[&](auto&& self,views::View* view)->void{ui::AXNodeData ax;view->GetViewAccessibility().GetAccessibleNodeData(&ax);if(ax.role==ax::mojom::Role::kStaticText)text+=ax.GetString16Attribute(ax::mojom::StringAttribute::kName)+u" | ";for(const auto& child:view->children())self(self,child.get());};visit(visit,shell->tools_widget()->GetContentsView());return base::UTF16ToUTF8(text);};
  ASSERT_NO_FATAL_FAILURE(activate(u"Zapisz snapshot"));
  ASSERT_TRUE(base::test::RunUntil([&]{return named_snapshots()==snapshots+1;}))<<"before="<<snapshots<<" after="<<window_->TimeMachine().snapshots.size()<<" active="<<window_->active_id()<<" rebuilt="<<!saved_button.view()<<" name="<<base::UTF16ToUTF8(shell->snapshot_input()->GetText())<<" UI="<<messages();
  const auto saved=window_->TimeMachine().snapshots;auto snapshot=std::find_if(saved.begin(),saved.end(),[](const auto& item){return item.title==u"Action snapshot 🦀";});ASSERT_NE(saved.end(),snapshot);EXPECT_FALSE(snapshot->id.empty());
  shell->OpenTools(2);shell->provider_input(0)->SetText(u"Action provider 🦀");shell->provider_input(1)->SetText(u"zaction");shell->provider_input(2)->SetText(u"https://actions.test/?q={query}");
  const auto count=shell->browser_tools().providers.size();ASSERT_NO_FATAL_FAILURE(activate(u"Zapisz wyszukiwarkę"));
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->browser_tools().providers.size()==count+1;}));
  auto choose=[&]{const auto& providers=shell->browser_tools().providers;auto it=std::find_if(providers.begin(),providers.end(),[](const auto& item){return item.keyword=="zaction";});ASSERT_NE(it,providers.end());shell->ActivateToolsChoiceForTesting("selection",it-providers.begin()+1);};
  ASSERT_NO_FATAL_FAILURE(choose());
  ASSERT_NO_FATAL_FAILURE(activate(u"Ustaw jako domyślną"));
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->browser_tools().default_keyword=="zaction";}));
  ASSERT_NO_FATAL_FAILURE(choose());
  ASSERT_NO_FATAL_FAILURE(activate(u"Usuń wyszukiwarkę"));
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->browser_tools().providers.size()==count;}));
  EXPECT_NE("zaction",shell->browser_tools().default_keyword);EXPECT_EQ(snapshots+1,named_snapshots());
  auto* close=shell->tools_close_for_testing();ASSERT_TRUE(close);if(rust)EXPECT_TRUE(close->children().empty());
  close->RequestFocus();ASSERT_TRUE(close->HasFocus());
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(shell->tools_widget()->GetNativeWindow(),ui::VKEY_SPACE,false,false,false,false));
  EXPECT_FALSE(shell->tools_widget()->IsVisible());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, ToolsActionsHistoryBusyAcrossRebuildAndVisibility) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_ready()&&shell->appearance_ready();}));
  const bool rust=shell->workspace_input()->rust_editor();shell->OpenTools(3);
  auto* clear=FindButton(shell->tools_widget()->GetContentsView(),u"Wyczyść historię…");ASSERT_TRUE(clear);
  EXPECT_EQ(rust,!views::AsViewClass<views::MdTextButton>(clear));
  shell->SetBrowsingDataBusy(true);EXPECT_FALSE(clear->GetEnabled());EXPECT_EQ(clear,FindButton(shell->tools_widget()->GetContentsView(),u"Czyszczenie…"));
  views::ViewTracker previous(clear);shell->OpenTools(2);EXPECT_FALSE(previous.view());shell->OpenTools(3);
  clear=FindButton(shell->tools_widget()->GetContentsView(),u"Czyszczenie…");ASSERT_TRUE(clear);EXPECT_FALSE(clear->GetEnabled());
  clear->ScrollRectToVisible(clear->GetLocalBounds());
  if(rust)ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_actions_contains_scene_text_for_testing("Czyszczenie…");}));
  shell->SetBrowsingDataBusy(false);EXPECT_TRUE(clear->GetEnabled());EXPECT_EQ(clear,FindButton(shell->tools_widget()->GetContentsView(),u"Wyczyść historię…"));
  if(rust)ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_actions_contains_scene_text_for_testing("Wyczyść historię…");}));
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->history_loaded();}));
  auto* more=FindButton(shell->tools_widget()->GetContentsView(),u"Pokaż więcej");ASSERT_TRUE(more);EXPECT_FALSE(more->GetVisible());
  if(rust)EXPECT_FALSE(more->parent()->GetVisible());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, ToolsChoicesCocoaPrefixCallbacksAndHistoryPeriod) {
  auto* shell=window_->shell();
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_ready()&&shell->appearance_ready();}));
  shell->OpenTools(1);ui_controls::EnableUIControls();
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->tools_widget()->GetNativeWindow()));
  auto* picker=shell->tools_selection();ASSERT_TRUE(picker);
  const bool rust=shell->workspace_input()->rust_editor();EXPECT_EQ(rust,!views::AsViewClass<views::Combobox>(picker));
  EXPECT_EQ(shell->browser_tools().shortcuts.size(),shell->tools_choice_count_for_testing("selection"));
  picker->RequestFocus();ASSERT_TRUE(picker->HasFocus());
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(shell->tools_widget()->GetNativeWindow(),ui::VKEY_H,false,false,false,false));
  const auto& shortcuts=shell->browser_tools().shortcuts;
  const auto history=std::find_if(shortcuts.begin(),shortcuts.end(),[](const auto& shortcut){return shortcut.id=="history";});
  ASSERT_NE(shortcuts.end(),history);const size_t index=history-shortcuts.begin();
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_choice_selection_for_testing("selection")==index;}));
  EXPECT_EQ(base::UTF8ToUTF16(FormatShortcut(history->accelerator)),shell->shortcut_input()->GetText());
  auto* record=FindButton(shell->tools_widget()->GetContentsView(),u"Nagraj kombinację…");ASSERT_TRUE(record);
  ClickControl(record);base::RunLoop().RunUntilIdle();
  EXPECT_EQ(ui::TEXT_INPUT_TYPE_NONE,shell->shortcut_input()->input_client()->GetTextInputType());
  shell->ActivateToolsChoiceForTesting("selection",0);
  EXPECT_EQ(ui::TEXT_INPUT_TYPE_TEXT,shell->shortcut_input()->input_client()->GetTextInputType());
  EXPECT_EQ(base::UTF8ToUTF16(FormatShortcut(shortcuts[0].accelerator)),shell->shortcut_input()->GetText());
  ASSERT_TRUE(shell->UpsertSearchProvider({"Tools duplicate 🦀","ztools","https://tools.test/?q={query}"}));
  shell->OpenTools(2);picker=shell->tools_selection();
  const auto& providers=shell->browser_tools().providers;
  const auto provider=std::find_if(providers.begin(),providers.end(),[](const auto& item){return item.keyword=="ztools";});
  ASSERT_NE(providers.end(),provider);const size_t provider_index=provider-providers.begin()+1;
  shell->ActivateToolsChoiceForTesting("selection",provider_index);
  EXPECT_EQ(u"Tools duplicate 🦀",shell->provider_input(0)->GetText());EXPECT_EQ(u"ztools",shell->provider_input(1)->GetText());
  EXPECT_EQ(u"https://tools.test/?q={query}",shell->provider_input(2)->GetText());
  picker->SetEnabled(false);shell->ActivateToolsChoiceForTesting("selection",0);EXPECT_EQ(provider_index,shell->tools_choice_selection_for_testing("selection"));picker->SetEnabled(true);
  views::ViewTracker old(picker);shell->OpenTools(3);EXPECT_FALSE(old.view());
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->tools_widget()->GetNativeWindow()));
  picker=shell->history_period();ASSERT_TRUE(picker);picker->ScrollRectToVisible(picker->GetLocalBounds());picker->RequestFocus();
  EXPECT_EQ(4u,shell->tools_choice_count_for_testing("history_period"));
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(shell->tools_widget()->GetNativeWindow(),ui::VKEY_C,false,false,false,false));
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_choice_selection_for_testing("history_period")==3u;}));
  ui::AXNodeData ax;picker->GetViewAccessibility().GetAccessibleNodeData(&ax);
  EXPECT_EQ(ax::mojom::Role::kComboBoxSelect,ax.role);EXPECT_EQ(u"Cały okres",ax.GetString16Attribute(ax::mojom::StringAttribute::kValue));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, ToolsFormStructureOrderNamesAndHistoryBusy) {
  auto* shell=window_->shell();
  const std::vector<std::vector<std::u16string>> expected={
    {u"Nazwa snapshotu, np. Research przed deployem",u"Zapisz snapshot",u"Szukaj snapshotów i zamkniętych kart",u"Zapisane snapshoty",u"Przywróć wybrany układ",u"Usuń wybrany snapshot",u"Zamknięte karty",u"Przywróć wybraną kartę",u"Uśpij tę przestrzeń"},
    {u"Akcja skrótu",u"Kombinacja, np. Cmd+Shift+J",u"Nagraj kombinację…",u"Zapisz skrót",u"Wyłącz wybrany skrót",u"Przywróć schemat Arc"},
    {u"Wyszukiwarka do edycji",u"Nazwa wyszukiwarki",u"Prefiks (a–z, 0–9)",u"Adres z {query}",u"Zapisz wyszukiwarkę",u"Ustaw jako domyślną",u"Usuń wyszukiwarkę"},
    {u"Szukaj w historii",u"Zakres czyszczenia historii",u"Wyczyść historię…"}};
  for(int page=0;page<4;++page){SCOPED_TRACE(page);shell->OpenTools(page);if(page==3)ASSERT_TRUE(base::test::RunUntil([&]{return shell->history_loaded();}));
    std::vector<std::u16string> names;
    auto visit=[&](auto&& self,views::View* view)->void{if(!view->GetVisible())return;ui::AXNodeData ax;view->GetViewAccessibility().GetAccessibleNodeData(&ax);
      if(ax.role==ax::mojom::Role::kTextField||ax.role==ax::mojom::Role::kComboBoxSelect||ax.role==ax::mojom::Role::kButton){names.push_back(ax.GetString16Attribute(ax::mojom::StringAttribute::kName));return;}
      for(const auto& child:view->children())self(self,child.get());};
    visit(visit,shell->tools_body_for_testing());EXPECT_EQ(expected[page],names);
  }
  shell->SetBrowsingDataBusy(true);shell->OpenTools(3);
  auto* clear=FindButton(shell->tools_body_for_testing(),u"Czyszczenie…");ASSERT_TRUE(clear);EXPECT_FALSE(clear->GetEnabled());
  shell->SetBrowsingDataBusy(false);EXPECT_TRUE(clear->GetEnabled());EXPECT_EQ(clear,FindButton(shell->tools_body_for_testing(),u"Wyczyść historię…"));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, ToolsChoicesShortcutProviderProjectionAndDrafts) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_ready()&&shell->appearance_ready();}));shell->OpenTools(1);
  const auto shortcuts=shell->browser_tools().shortcuts;ASSERT_FALSE(shortcuts.empty());ASSERT_EQ(shortcuts.size(),shell->tools_choice_count_for_testing("selection"));
  for(size_t i=0;i<shortcuts.size();++i){SCOPED_TRACE(i);EXPECT_EQ(shortcuts[i].title+u" — "+base::UTF8ToUTF16(FormatShortcut(shortcuts[i].accelerator)),shell->tools_choice_label_for_testing("selection",i));shell->ActivateToolsChoiceForTesting("selection",i);EXPECT_EQ(base::UTF8ToUTF16(FormatShortcut(shortcuts[i].accelerator)),shell->shortcut_input()->GetText());}
  ASSERT_TRUE(shell->UpsertSearchProvider({"Projection duplicate 🦀","zproja","https://projection.test/a?q={query}"}));ASSERT_TRUE(shell->UpsertSearchProvider({"Projection duplicate 🦀","zprojb","https://projection.test/b?q={query}"}));ASSERT_TRUE(shell->SetDefaultSearchProvider("zprojb"));shell->OpenTools(2);
  const auto providers=shell->browser_tools().providers;ASSERT_EQ(providers.size()+1,shell->tools_choice_count_for_testing("selection"));EXPECT_EQ(u"Dodaj nową wyszukiwarkę…",shell->tools_choice_label_for_testing("selection",0));
  for(size_t field=0;field<3;++field)EXPECT_TRUE(shell->provider_input(field)->GetText().empty());
  for(size_t i=0;i<providers.size();++i){SCOPED_TRACE(i);const auto& item=providers[i];EXPECT_EQ(base::UTF8ToUTF16(item.keyword+" · "+item.name+(item.keyword==shell->browser_tools().default_keyword?" · domyślna":"")),shell->tools_choice_label_for_testing("selection",i+1));shell->ActivateToolsChoiceForTesting("selection",i+1);EXPECT_EQ(base::UTF8ToUTF16(item.name),shell->provider_input(0)->GetText());EXPECT_EQ(base::UTF8ToUTF16(item.keyword),shell->provider_input(1)->GetText());EXPECT_EQ(base::UTF8ToUTF16(item.url),shell->provider_input(2)->GetText());}
  shell->ActivateToolsChoiceForTesting("selection",0);for(size_t field=0;field<3;++field)EXPECT_TRUE(shell->provider_input(field)->GetText().empty());
  if(shell->workspace_input()->rust_editor()){auto* picker=shell->tools_selection();picker->ScrollRectToVisible(picker->GetLocalBounds());ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_choice_metal_visible_for_testing("selection");}));}
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, ToolsChoicesTimelineIdentityFilteringAndEmptyState) {
  ASSERT_TRUE(window_->Select("work"));auto* shell=window_->shell();
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_ready()&&shell->appearance_ready();}));
  ASSERT_TRUE(window_->SaveSnapshot("Tools twin"));ASSERT_TRUE(window_->SaveSnapshot("Tools twin"));
  shell->OpenTools(0);
  auto filter=[&](std::u16string text){auto* field=shell->timeline_filter_input();field->SelectRange(gfx::Range(0,field->GetText().size()));field->input_client()->InsertText(text,ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);shell->tools_widget()->LayoutRootViewIfNecessary();};
  filter(u"Tools twin");ASSERT_EQ(2u,shell->tools_choice_count_for_testing("selection"));
  auto* picker=shell->tools_selection();picker->ScrollRectToVisible(picker->GetLocalBounds());
  std::vector<std::string> ids;for(const auto& item:window_->TimeMachine().snapshots)if(item.title==u"Tools twin")ids.push_back(item.id);ASSERT_EQ(2u,ids.size());
  shell->ActivateToolsChoiceForTesting("selection",1);EXPECT_EQ(1u,shell->tools_choice_selection_for_testing("selection"));
  auto* remove=FindButton(shell->tools_widget()->GetContentsView(),u"Usuń wybrany snapshot");ASSERT_TRUE(remove);
  views::ViewTracker old(picker);ClickControl(remove);
  ASSERT_TRUE(base::test::RunUntil([&]{return !old.view();}));
  auto snapshots=window_->TimeMachine().snapshots;
  EXPECT_TRUE(std::any_of(snapshots.begin(),snapshots.end(),[&](const auto& item){return item.id==ids[0];}));
  EXPECT_FALSE(std::any_of(snapshots.begin(),snapshots.end(),[&](const auto& item){return item.id==ids[1];}));
  filter(u"Tools twin");EXPECT_EQ(1u,shell->tools_choice_count_for_testing("selection"));EXPECT_EQ(0u,shell->tools_choice_selection_for_testing("selection"));
  filter(u"No saved item matches this probe");picker=shell->tools_selection();EXPECT_FALSE(picker->GetEnabled());
  EXPECT_EQ(1u,shell->tools_choice_count_for_testing("selection"));EXPECT_EQ(u"Brak zapisanych pozycji",shell->tools_choice_label_for_testing("selection",0));
  ui::AXNodeData empty;picker->GetViewAccessibility().GetAccessibleNodeData(&empty);EXPECT_EQ(u"Brak zapisanych pozycji",empty.GetString16Attribute(ax::mojom::StringAttribute::kValue));
  shell->ActivateToolsChoiceForTesting("selection",0);EXPECT_EQ(snapshots.size(),window_->TimeMachine().snapshots.size());
  filter(u"Tools twin");EXPECT_TRUE(picker->GetEnabled());EXPECT_TRUE(shell->tools_choice_label_for_testing("selection",0).starts_with(u"Tools twin"));
  shell->tools_widget()->Hide();shell->ActivateToolsChoiceForTesting("selection",0);EXPECT_EQ(snapshots.size(),window_->TimeMachine().snapshots.size());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, ToolsChoicesTimelineUnicodeRestoreAndClosedTargets) {
  ASSERT_TRUE(window_->Select("work"));auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_ready()&&shell->appearance_ready();}));
  const auto left=embedded_test_server()->GetURL("/storage_probe.html?projection-left");const auto right=embedded_test_server()->GetURL("/storage_probe.html?projection-right");
  ASSERT_TRUE(content::NavigateToURL(window_->contents("work"),left));const std::u16string prefix=u"ΣΙΓΜΑ İSTANBUL 🦀";const auto snapshot_name=prefix+u" saved";const auto closed_name=prefix+u" closed";
  ASSERT_TRUE(window_->SaveSnapshot(base::UTF16ToUTF8(snapshot_name)));ASSERT_TRUE(window_->NavigateFromBar(right,true));ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));
  content::TitleWatcher title(window_->contents("work"),closed_name);ASSERT_TRUE(content::ExecJs(window_->contents("work"),content::JsReplace("document.title=$1",closed_name)));EXPECT_EQ(closed_name,title.WaitAndGetTitle());
  ASSERT_TRUE(window_->SaveSnapshot("Different layout"));window_->CloseTab(1);ASSERT_EQ(1u,window_->tab_count("work"));shell->OpenTools(0);
  auto filter=[&](std::u16string text){auto* field=shell->timeline_filter_input();field->SelectRange(gfx::Range(0,field->GetText().size()));if(text.empty())EXPECT_TRUE(field->input_client()->DeleteRange(gfx::Range(0,field->GetText().size())));else field->input_client()->InsertText(text,ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);shell->tools_widget()->LayoutRootViewIfNecessary();};
  auto oracle=[&](std::u16string query){
    filter(query);query=base::i18n::ToLower(query);const auto state=window_->TimeMachine();
    auto check=[&](const auto& items,std::string key){std::vector<std::u16string> labels;for(const auto& item:items)if(query.empty()||base::i18n::ToLower(item.title+u" "+item.detail).find(query)!=std::u16string::npos)labels.push_back(item.title+u" — "+item.detail);auto* picker=shell->tools_choice_for_testing(key);ASSERT_TRUE(picker);EXPECT_EQ(!labels.empty(),picker->GetEnabled());if(labels.empty())labels.push_back(u"Brak zapisanych pozycji");ASSERT_EQ(labels.size(),shell->tools_choice_count_for_testing(key));for(size_t i=0;i<labels.size();++i)EXPECT_EQ(labels[i],shell->tools_choice_label_for_testing(key,i));EXPECT_EQ(0u,shell->tools_choice_selection_for_testing(key));ui::AXNodeData node;picker->GetViewAccessibility().GetAccessibleNodeData(&node);EXPECT_EQ(labels[0],node.GetString16Attribute(ax::mojom::StringAttribute::kValue));};
    ASSERT_NO_FATAL_FAILURE(check(state.snapshots,"selection"));
  ASSERT_NO_FATAL_FAILURE(check(state.closed,"closed"));
  };
  for(const auto& query:std::vector<std::u16string>{u"σιγμα i\u0307stanbul 🦀",u" — ",std::u16string(1,0xd83e),u"not a timeline match",u""}){SCOPED_TRACE(base::UTF16ToUTF8(query));
  ASSERT_NO_FATAL_FAILURE(oracle(query));}
  ASSERT_NO_FATAL_FAILURE(oracle(prefix));ASSERT_TRUE(shell->tools_choice_for_testing("closed")->GetEnabled());
  auto activate=[&](std::u16string text){auto* widget=shell->tools_widget();auto* button=FindButton(widget->GetContentsView(),text);ASSERT_TRUE(button);button->ScrollRectToVisible(button->GetLocalBounds());ui_controls::EnableUIControls();ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(widget->GetNativeWindow()));button->RequestFocus();base::test::TestFuture<void> presented;auto* compositor=widget->GetCompositor();ASSERT_TRUE(compositor);widget->GetRootView()->SchedulePaint();compositor->RequestSuccessfulPresentationTimeForNextFrame(base::BindOnce([](base::OnceClosure done,const viz::FrameTimingDetails&){std::move(done).Run();},presented.GetCallback()));compositor->ScheduleFullRedraw();ASSERT_TRUE(presented.Wait());ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(widget->GetNativeWindow(),ui::VKEY_SPACE,false,false,false,false));};
  ASSERT_NO_FATAL_FAILURE(activate(u"Przywróć wybraną kartę"));ASSERT_TRUE(base::test::RunUntil([&]{return window_->tab_count("work")==2;}));ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));EXPECT_EQ(right,window_->contents("work")->GetLastCommittedURL());EXPECT_EQ(work_,window_->contents("work")->GetBrowserContext());
  shell->OpenTools(0);
  ASSERT_NO_FATAL_FAILURE(oracle(snapshot_name));ASSERT_EQ(1u,shell->tools_choice_count_for_testing("selection"));
  ASSERT_NO_FATAL_FAILURE(activate(u"Przywróć wybrany układ"));ASSERT_TRUE(base::test::RunUntil([&]{return window_->tab_count("work")==1;}));ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));EXPECT_EQ(left,window_->contents("work")->GetLastCommittedURL());EXPECT_EQ(work_,window_->contents("work")->GetBrowserContext());
  shell->OpenTools(0);
  ASSERT_NO_FATAL_FAILURE(oracle(prefix));shell->tools_widget()->Hide();const auto count=window_->TimeMachine().snapshots.size();shell->ActivateToolsChoiceForTesting("selection",0);EXPECT_EQ(count,window_->TimeMachine().snapshots.size());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, ToolsEditorsCocoaSnapshotProviderAndStaleActions) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_ready();}));
  ASSERT_TRUE(window_->Select("work"));shell->OpenTools(0);ui_controls::EnableUIControls();
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->tools_widget()->GetNativeWindow()));
  auto key=[&](ui::KeyboardCode code){return ui_test_utils::SendKeyPressToWindowSync(shell->tools_widget()->GetNativeWindow(),code,false,false,false,false);};
  auto replace=[](EditorControl* field,std::u16string text){field->SelectRange(gfx::Range(0,field->GetText().size()));field->input_client()->InsertText(text,ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);};
  auto* name=shell->snapshot_input();ASSERT_TRUE(name);name->RequestFocus();
  EXPECT_EQ(shell->workspace_input()->rust_editor(),name->rust_editor());
  ASSERT_TRUE(key(ui::VKEY_A));ASSERT_TRUE(base::test::RunUntil([&]{return name->GetText()==u"a";}));
  replace(name,u"Research 🦀");
  if(name->rust_editor()){EXPECT_TRUE(name->view()->children().empty());EXPECT_FALSE(views::AsViewClass<views::Textfield>(name->view()));ASSERT_TRUE(base::test::RunUntil([&]{return name->metal_visible();}));}
  auto lifetime=name->input_client()->AsWeakPtr();const auto before=window_->TimeMachine().snapshots.size();
  ui::CompositionText marked;marked.text=u"未";marked.selection=gfx::Range(1);name->SetCompositionText(marked);
  ui::KeyEvent enter(ui::EventType::kKeyPressed,ui::VKEY_RETURN,0),escape(ui::EventType::kKeyPressed,ui::VKEY_ESCAPE,0);
  EXPECT_FALSE(shell->HandleToolsEditorKey(name,enter));EXPECT_FALSE(shell->HandleToolsEditorKey(name,escape));
  auto* save=FindButton(shell->tools_widget()->GetContentsView(),u"Zapisz snapshot");ASSERT_TRUE(save);
  ClickControl(save);base::RunLoop().RunUntilIdle();
  EXPECT_EQ(before,window_->TimeMachine().snapshots.size());EXPECT_TRUE(lifetime);name->ClearCompositionText();
  ASSERT_TRUE(key(ui::VKEY_RETURN));
  ASSERT_TRUE(base::test::RunUntil([&]{return window_->TimeMachine().snapshots.size()>before&&!lifetime;}));
  EXPECT_EQ(u"Research 🦀",window_->TimeMachine().snapshots.front().title);
  auto* filter=shell->timeline_filter_input();ASSERT_TRUE(filter);filter->RequestFocus();replace(filter,u"RESEARCH");
  auto* picker=shell->tools_selection();ASSERT_TRUE(picker->GetEnabled());ASSERT_EQ(1u,shell->tools_choice_count_for_testing("selection"));
  filter->SetCompositionText(marked);EXPECT_TRUE(picker->GetEnabled());filter->ConfirmCompositionText(false);EXPECT_FALSE(picker->GetEnabled());
  replace(filter,u"Research");EXPECT_TRUE(picker->GetEnabled());
  auto old_filter=filter->input_client()->AsWeakPtr();shell->OpenTools(2);EXPECT_FALSE(old_filter);
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->tools_widget()->GetNativeWindow()));
  auto fill=[&](std::u16string title,std::u16string keyword,std::u16string url){
    replace(shell->provider_input(0),title);replace(shell->provider_input(1),keyword);replace(shell->provider_input(2),url);
  };
  const auto providers=shell->browser_tools().providers.size();
  fill(u"Wyszukiwarka 🦀",u"zprobe",u"https://provider.test/?q={query}");
  auto* provider=shell->provider_input(0);EXPECT_EQ(shell->workspace_input()->rust_editor(),provider->rust_editor());provider->RequestFocus();provider->SetCompositionText(marked);
  EXPECT_FALSE(shell->HandleToolsEditorKey(provider,enter));
  save=FindButton(shell->tools_widget()->GetContentsView(),u"Zapisz wyszukiwarkę");ASSERT_TRUE(save);ClickControl(save);base::RunLoop().RunUntilIdle();
  EXPECT_EQ(providers,shell->browser_tools().providers.size());provider->ClearCompositionText();auto old_provider=provider->input_client()->AsWeakPtr();
  ASSERT_TRUE(key(ui::VKEY_RETURN));ASSERT_TRUE(base::test::RunUntil([&]{return !old_provider&&shell->browser_tools().providers.size()==providers+1;}));
  EXPECT_TRUE(std::any_of(shell->browser_tools().providers.begin(),shell->browser_tools().providers.end(),[](const auto& value){return value.keyword=="zprobe"&&value.name=="Wyszukiwarka 🦀";}));
  fill(u"Unsafe",u"zbad",u"javascript:alert('{query}')");provider=shell->provider_input(2);provider->RequestFocus();old_provider=provider->input_client()->AsWeakPtr();
  ASSERT_TRUE(key(ui::VKEY_RETURN));base::RunLoop().RunUntilIdle();EXPECT_TRUE(old_provider);EXPECT_EQ(providers+1,shell->browser_tools().providers.size());
  fill(u"Deferred",u"zstale",u"https://provider.test/?q={query}");ASSERT_TRUE(shell->provider_input(2)->view()->OnKeyPressed(enter));
  shell->OpenTools(2);EXPECT_FALSE(old_provider);
  fill(u"Wrong new form",u"zwrong",u"https://provider.test/?q={query}");base::RunLoop().RunUntilIdle();
  EXPECT_EQ(providers+1,shell->browser_tools().providers.size());
  shell->OpenTools(0);name=shell->snapshot_input();replace(name,u"Old queued snapshot");ASSERT_TRUE(name->view()->OnKeyPressed(enter));
  ASSERT_TRUE(window_->Select("personal"));shell->OpenTools(0);replace(shell->snapshot_input(),u"Wrong profile snapshot");base::RunLoop().RunUntilIdle();
  auto absent=[](const auto& values){return std::none_of(values.begin(),values.end(),[](const auto& value){return value.title==u"Old queued snapshot"||value.title==u"Wrong profile snapshot";});};
  EXPECT_TRUE(absent(window_->TimeMachine().snapshots));ASSERT_TRUE(window_->Select("work"));EXPECT_TRUE(absent(window_->TimeMachine().snapshots));
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_saved()&&window_->configuration_saved();}));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, HistoryEditorCommittedQueryDuringPreeditAndReopen) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_ready();}));ASSERT_TRUE(window_->Select("work"));
  auto seed=[](Profile* profile,const char* host,std::u16string title){history::URLRow row(GURL(std::string("https://")+host+"/"));row.set_title(title);row.set_visit_count(1);row.set_last_visit(base::Time::Now()-base::Minutes(1));HistoryServiceFactory::GetForProfile(profile,ServiceAccessType::EXPLICIT_ACCESS)->AddPagesWithDetails({row},history::SOURCE_OS_MIGRATION_IMPORTED);};
  seed(work_,"alpha.test",u"Alpha");seed(work_,"beta.test",u"Beta");seed(personal_,"private.test",u"Private");
  shell->OpenTools(3);ASSERT_TRUE(base::test::RunUntil([&]{return shell->history_loaded();}));ASSERT_EQ(2u,shell->history_entries().size());
  ui_controls::EnableUIControls();ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->tools_widget()->GetNativeWindow()));auto* field=shell->history_search();field->RequestFocus();
  EXPECT_EQ(shell->workspace_input()->rust_editor(),field->rust_editor());
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(shell->tools_widget()->GetNativeWindow(),ui::VKEY_A,false,false,false,false));
  ASSERT_TRUE(base::test::RunUntil([&]{return field->GetText()==u"a";}));
  field->SelectRange(gfx::Range(0,field->GetText().size()));field->input_client()->InsertText(u"Alpha",ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);
  ui::CompositionText marked;marked.text=u"未";marked.selection=gfx::Range(1);field->SetCompositionText(marked);ASSERT_TRUE(field->IsIMEComposing());
  // The pending 150 ms query uses committed Alpha even while the OS owns preedit.
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->history_loaded();}));ASSERT_EQ(1u,shell->history_entries().size());EXPECT_EQ(u"Alpha",shell->history_entries()[0].title);
  const auto profile=window_->workspaces().front().profile_id;
  shell->SetHistoryResults(std::u16string(field->GetText()),profile,200,{{GURL("https://poison.test/"),u"Preedit poison",base::Time::Now()}},false,false);
  shell->SetHistoryResults(u"Alpha","wrong-profile",200,{{GURL("https://poison.test/"),u"Profile poison",base::Time::Now()}},false,false);
  ASSERT_EQ(1u,shell->history_entries().size());EXPECT_EQ(u"Alpha",shell->history_entries()[0].title);field->ClearCompositionText();EXPECT_EQ(u"Alpha",field->GetText());
  field->SelectRange(gfx::Range(0,field->GetText().size()));field->input_client()->InsertText(u"Beta",ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->history_loaded();}));ASSERT_EQ(1u,shell->history_entries().size());EXPECT_EQ(u"Beta",shell->history_entries()[0].title);
  auto old=field->input_client()->AsWeakPtr();shell->OpenTools(3);EXPECT_FALSE(old);ASSERT_TRUE(base::test::RunUntil([&]{return shell->history_loaded();}));EXPECT_EQ(2u,shell->history_entries().size());
  ASSERT_TRUE(window_->Select("personal"));shell->OpenTools(3);ASSERT_TRUE(base::test::RunUntil([&]{return shell->history_loaded();}));ASSERT_EQ(1u,shell->history_entries().size());EXPECT_EQ(u"Private",shell->history_entries()[0].title);
  shell->SetHistoryResults(u"",profile,200,{{GURL("https://poison.test/"),u"Stale profile",base::Time::Now()}},false,false);EXPECT_EQ(u"Private",shell->history_entries()[0].title);
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, ShortcutEditorCocoaRecorderAdmissionAndCancel) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_ready();}));shell->OpenTools(1);ui_controls::EnableUIControls();
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->tools_widget()->GetNativeWindow()));auto* field=shell->shortcut_input();field->SetText(u"Cmd+Shift+U");field->SelectRange(gfx::Range(field->GetText().size()));
  EXPECT_EQ(shell->workspace_input()->rust_editor(),field->rust_editor());
  auto key=[&](ui::KeyboardCode code,bool shift=false,bool command=false){return ui_test_utils::SendKeyPressToWindowSync(shell->tools_widget()->GetNativeWindow(),code,false,shift,false,command);};
  auto record=[&]{auto* button=FindButton(shell->tools_widget()->GetContentsView(),u"Nagraj kombinację…");ASSERT_TRUE(button);ClickControl(button);base::RunLoop().RunUntilIdle();};
  ui::CompositionText marked;marked.text=u"未";marked.selection=gfx::Range(1);field->SetCompositionText(marked);ASSERT_TRUE(field->IsIMEComposing());record();
  EXPECT_FALSE(field->IsIMEComposing());EXPECT_EQ(u"Cmd+Shift+U",field->GetText());EXPECT_EQ(ui::TEXT_INPUT_TYPE_NONE,field->input_client()->GetTextInputType());
  ASSERT_TRUE(key(ui::VKEY_A));EXPECT_EQ(u"Cmd+Shift+U",field->GetText());ASSERT_TRUE(key(ui::VKEY_C,false,true));EXPECT_EQ(u"Cmd+Shift+U",field->GetText());
  ASSERT_TRUE(key(ui::VKEY_ESCAPE));EXPECT_TRUE(shell->tools_widget()->IsVisible());EXPECT_EQ(ui::TEXT_INPUT_TYPE_TEXT,field->input_client()->GetTextInputType());
  record();ASSERT_TRUE(key(ui::VKEY_U,true,true));EXPECT_EQ(u"Cmd+Shift+U",field->GetText());EXPECT_EQ(ui::TEXT_INPUT_TYPE_TEXT,field->input_client()->GetTextInputType());
  EXPECT_NE("address",shell->ShortcutFor(*ParseShortcut("Cmd+Shift+U")));
  ASSERT_TRUE(key(ui::VKEY_RETURN));ASSERT_TRUE(base::test::RunUntil([&]{return shell->ShortcutFor(*ParseShortcut("Cmd+Shift+U"))=="address";}));
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_saved();}));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, TimeMachineSnapshotsClosedTabsAndSleep) {
  ASSERT_TRUE(window_->Select("work"));
  const auto left = embedded_test_server()->GetURL("/storage_probe.html?left");
  const auto right = embedded_test_server()->GetURL("/storage_probe.html?right");
  ASSERT_TRUE(content::NavigateToURL(window_->contents("work"), left));
  ASSERT_TRUE(window_->NavigateFromBar(right, true));
  ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));
  window_->SelectTab(0); ASSERT_TRUE(window_->SplitTab(1)); window_->SetSplitRatio(0.63);
  ASSERT_TRUE(window_->SaveSnapshot("Research"));
  const auto snapshot = window_->TimeMachine().snapshots.front().id;
  window_->CloseTab(0);
  EXPECT_FALSE(window_->split());
  EXPECT_FALSE(window_->TimeMachine().closed.empty());
  ASSERT_TRUE(window_->ReopenClosedTab());
  EXPECT_EQ(2u, window_->tab_count("work"));
  ASSERT_TRUE(window_->RestoreSnapshot(snapshot));
  EXPECT_TRUE(window_->split()); EXPECT_DOUBLE_EQ(0.63, window_->split_ratio());
  ASSERT_TRUE(content::WaitForLoadStop(window_->pane_contents(false)));
  EXPECT_EQ(left, window_->pane_contents(false)->GetLastCommittedURL());
  ASSERT_TRUE(content::WaitForLoadStop(window_->pane_contents(true)));
  EXPECT_EQ(right, window_->pane_contents(true)->GetLastCommittedURL());
  EXPECT_GE(window_->TimeMachine().snapshots.size(), 2u);  // Undo point before restore.
  ASSERT_TRUE(window_->SleepWorkspace("work"));
  EXPECT_EQ("personal", window_->active_id());
  EXPECT_TRUE(window_->workspace_sleeping("work"));
  EXPECT_EQ(nullptr, window_->contents("work"));  // WebContents actually released.
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_saved(); }));
  window_.reset(); window_ = std::make_unique<WorkspaceWindow>(work_, personal_);
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_ready(); }));
  EXPECT_TRUE(window_->workspace_sleeping("work"));
  EXPECT_EQ(nullptr, window_->contents("work"));
  ASSERT_TRUE(window_->Select("work"));
  EXPECT_FALSE(window_->workspace_sleeping("work")); EXPECT_TRUE(window_->split());
  EXPECT_DOUBLE_EQ(0.63, window_->split_ratio());
  ASSERT_TRUE(content::WaitForLoadStop(window_->pane_contents(false)));
  EXPECT_EQ(left, window_->pane_contents(false)->GetLastCommittedURL());
  EXPECT_EQ(work_, window_->pane_contents(false)->GetBrowserContext());
  ASSERT_TRUE(window_->RestoreSnapshot(snapshot));
  ASSERT_TRUE(window_->DeleteSnapshot(snapshot));
  EXPECT_FALSE(window_->RestoreSnapshot(snapshot));
  window_->shell()->OpenTools(0);
  EXPECT_TRUE(window_->shell()->tools_widget()->IsVisible());
  auto* selection = window_->shell()->tools_selection();
  ui::AXNodeData node;
  selection->GetViewAccessibility().GetAccessibleNodeData(&node);
  EXPECT_EQ(base::UTF16ToUTF8(window_->shell()->tools_choice_label_for_testing("selection",window_->shell()->tools_choice_selection_for_testing("selection").value_or(0))),
      node.GetStringAttribute(ax::mojom::StringAttribute::kValue));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, PreferenceEditorsCocoaCompositionAndProfileScope) {
  auto* shell=window_->shell();
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));
  ASSERT_TRUE(window_->Select("work"));
  ASSERT_TRUE(window_->WriteTextPreference("accept_languages","en-US"));
  shell->OpenGeneralSettings();
  ui_controls::EnableUIControls();
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->general_widget()->GetNativeWindow()));
  auto key=[&](ui::KeyboardCode code){return ui_test_utils::SendKeyPressToWindowSync(
      shell->general_widget()->GetNativeWindow(),code,false,false,false,false);};
  auto replace=[](EditorControl* field,std::u16string text){
    field->SelectRange(gfx::Range(0,field->GetText().size()));
    field->input_client()->InsertText(text,ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);
  };
  auto* search=shell->preferences_search();search->RequestFocus();
  EXPECT_EQ(shell->workspace_input()->rust_editor(),search->rust_editor());
  ASSERT_TRUE(key(ui::VKEY_A));
  ASSERT_TRUE(base::test::RunUntil([&]{return search->GetText()==u"a";}));
  replace(search,u"języki");shell->general_widget()->LayoutRootViewIfNecessary();
  auto* languages=shell->preference_textfield("accept_languages");
  ASSERT_TRUE(languages->view()->IsDrawn());
  EXPECT_EQ(search->rust_editor(),languages->rust_editor());
  EXPECT_EQ(search->rust_editor(),shell->site_origin_input()->rust_editor());
  if(search->rust_editor()){
    EXPECT_TRUE(languages->view()->children().empty());
    EXPECT_FALSE(views::AsViewClass<views::Textfield>(languages->view()));
    ASSERT_TRUE(base::test::RunUntil([&]{return search->metal_visible()&&languages->metal_visible();}));
  }
  // Preedit must not refilter the section containing the field underneath it.
  search->SelectRange(gfx::Range(search->GetText().size()));
  ui::CompositionText marked;marked.text=u"にほん";marked.selection=gfx::Range(marked.text.size());
  search->SetCompositionText(marked);ASSERT_TRUE(search->IsIMEComposing());
  EXPECT_TRUE(languages->view()->IsDrawn());search->ClearCompositionText();
  languages->view()->ScrollRectToVisible(languages->view()->GetLocalBounds());languages->RequestFocus();
  replace(languages,u"pl");
  marked.text=u", EN-us";marked.selection=gfx::Range(marked.text.size());
  languages->SetCompositionText(marked);ASSERT_TRUE(languages->IsIMEComposing());
  ui::KeyEvent enter(ui::EventType::kKeyPressed,ui::VKEY_RETURN,0),escape(ui::EventType::kKeyPressed,ui::VKEY_ESCAPE,0);
  EXPECT_FALSE(shell->HandlePreferenceEditorKey(languages,enter));
  EXPECT_FALSE(shell->HandlePreferenceEditorKey(languages,escape));
  EXPECT_EQ("en-US",window_->ReadPreferences().at("accept_languages").text);
  shell->SetWorkspaceAnimations(false);
  EXPECT_TRUE(languages->IsIMEComposing());EXPECT_EQ(u"pl, EN-us",languages->GetText());
  languages->ConfirmCompositionText(false);
  ASSERT_TRUE(key(ui::VKEY_RETURN));
  ASSERT_TRUE(base::test::RunUntil([&]{return window_->ReadPreferences().at("accept_languages").text=="pl,en-US";}));
  EXPECT_EQ(u"pl,en-US",languages->GetText());
  replace(languages,u"pl,,en");ASSERT_TRUE(key(ui::VKEY_RETURN));
  EXPECT_EQ("pl,en-US",window_->ReadPreferences().at("accept_languages").text);
  EXPECT_EQ(u"pl,,en",languages->GetText());
  // Programmatic refresh is silent; a committed draft remains unsaved.
  shell->SetWorkspaceAnimations(true);EXPECT_EQ(u"pl,,en",languages->GetText());
  ASSERT_TRUE(window_->Select("personal"));shell->OpenGeneralSettings();
  EXPECT_NE(u"pl,,en",languages->GetText());
  EXPECT_NE("pl,en-US",window_->ReadPreferences().at("accept_languages").text);
  ASSERT_TRUE(window_->Select("work"));shell->OpenGeneralSettings();
  EXPECT_EQ(u"pl,en-US",languages->GetText());
  shell->OpenGeneralSettings();
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->general_widget()->GetNativeWindow()));
  search->RequestFocus();replace(search,u"wyjątek");shell->general_widget()->LayoutRootViewIfNecessary();
  auto* origin=shell->site_origin_input();ASSERT_TRUE(origin->view()->IsDrawn());
  origin->view()->ScrollRectToVisible(origin->view()->GetLocalBounds());origin->RequestFocus();
  // Default policy removes a pre-existing JS rule only after a committed Enter.
  const std::string site="https://editor-scope.test";
  ASSERT_TRUE(window_->WriteSitePreference(site,"javascript",1));
  replace(origin,base::UTF8ToUTF16(site));
  marked.text=u"/path";marked.selection=gfx::Range(marked.text.size());origin->SetCompositionText(marked);
  EXPECT_FALSE(shell->HandlePreferenceEditorKey(origin,enter));
  EXPECT_FALSE(window_->ReadSitePreferences().empty());origin->ClearCompositionText();
  ASSERT_TRUE(key(ui::VKEY_RETURN));
  EXPECT_TRUE(window_->ReadSitePreferences().empty());
  ASSERT_TRUE(key(ui::VKEY_ESCAPE));
  ASSERT_TRUE(base::test::RunUntil([&]{return !shell->general_widget()->IsVisible();}));
  EXPECT_FALSE(shell->HandlePreferenceEditorKey(origin,enter));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, AdvancedChromiumPreferences) {
  ASSERT_NO_FATAL_FAILURE(NavigateBoth("/preferences_probe.html"));
  ASSERT_TRUE(window_->Select("work"));
  auto* shell = window_->shell();
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->appearance_ready(); }));
  shell->OpenGeneralSettings();
  shell->preferences_search()->input_client()->InsertText(u"języki",ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);
  shell->general_widget()->LayoutRootViewIfNecessary();
  ASSERT_TRUE(shell->preference_textfield("accept_languages")->view()->IsDrawn());
  auto* field = shell->preference_textfield("accept_languages");
  field->SelectRange(gfx::Range(0,field->GetText().size()));
  field->input_client()->InsertText(u"pl, EN-us, pl",ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);
  shell->SetWorkspaceAnimations(false);
  EXPECT_EQ(u"pl, EN-us, pl", field->GetText());  // A refresh preserves an unsaved edit.
  ui::KeyEvent enter(ui::EventType::kKeyPressed, ui::VKEY_RETURN, ui::EF_NONE);
  ASSERT_TRUE(shell->HandlePreferenceEditorKey(field, enter));
  EXPECT_EQ("pl,en-US", window_->ReadPreferences().at("accept_languages").text);
  EXPECT_EQ(u"pl,en-US", field->GetText());
  EXPECT_FALSE(window_->WriteTextPreference("accept_languages", "pl,,en"));
  EXPECT_FALSE(window_->WriteTextPreference("accept_languages", "pl\r\nX-Injected: yes"));
  EXPECT_FALSE(window_->WriteTextPreference("accept_languages", ""));
  EXPECT_FALSE(window_->WriteTextPreference("font_standard", std::string(129, 'x')));
  EXPECT_FALSE(window_->WriteTextPreference("unknown", "value"));
  ASSERT_TRUE(window_->WriteTextPreference("font_sans", "Arial"));
  EXPECT_EQ("Arial", work_->GetPrefs()->GetString(prefs::kWebKitSansSerifFontFamily));
  ASSERT_TRUE(window_->WritePreference("fixed_font_size", 3));
  EXPECT_EQ(18, work_->GetPrefs()->GetInteger(prefs::kWebKitDefaultFixedFontSize));
  ASSERT_TRUE(window_->WritePreference("spellcheck", 0));
  EXPECT_EQ(0, window_->ReadPreferences().at("spellcheck").value);
  ASSERT_TRUE(window_->WritePreference("caret_browsing", 1));
  ASSERT_TRUE(content::NavigateToURL(window_->contents("work"), embedded_test_server()->GetURL("/preferences_probe.html?language")));
  EXPECT_EQ("pl", content::EvalJs(window_->contents("work"), "navigator.language"));
  ASSERT_TRUE(window_->Select("personal"));
  EXPECT_NE("pl,en-US", window_->ReadPreferences().at("accept_languages").text);
  ASSERT_TRUE(window_->Select("work"));
  EXPECT_EQ("pl,en-US", window_->ReadPreferences().at("accept_languages").text);
  // Resolver validation and managed preferences use an isolated pref store;
  // the test never changes the running browser's DNS configuration.
  TestingPrefServiceSimple local;
  local.registry()->RegisterStringPref(prefs::kDnsOverHttpsMode, "automatic");
  local.registry()->RegisterStringPref(prefs::kDnsOverHttpsTemplates, "");
  local.registry()->RegisterBooleanPref(prefs::kHardwareAccelerationModeEnabled, true);
  EXPECT_EQ(false, WriteChromiumPreference(work_, &local, "dns_mode", 2));
  EXPECT_FALSE(WriteChromiumTextPreference(work_, &local, "dns_templates", "http://resolver.test/dns-query"));
  EXPECT_FALSE(WriteChromiumTextPreference(work_, &local, "dns_templates", "not a URL"));
  ASSERT_TRUE(WriteChromiumTextPreference(work_, &local, "dns_templates", "https://resolver.test/dns-query{?dns}"));
  EXPECT_EQ(true, WriteChromiumPreference(work_, &local, "dns_mode", 2));
  EXPECT_EQ("secure", local.GetString(prefs::kDnsOverHttpsMode));
  EXPECT_FALSE(WriteChromiumTextPreference(work_, &local, "dns_templates", ""));
  EXPECT_EQ(false, WriteChromiumPreference(work_, &local, "dns_mode", 9));
  local.SetManagedPref(prefs::kDnsOverHttpsMode, base::Value("off"));
  EXPECT_EQ(false, WriteChromiumPreference(work_, &local, "dns_mode", 1));
  EXPECT_EQ("off", local.GetString(prefs::kDnsOverHttpsMode));
  EXPECT_EQ(true, WriteChromiumPreference(work_, &local, "hardware_acceleration", 0));
  EXPECT_FALSE(local.GetBoolean(prefs::kHardwareAccelerationModeEnabled));
  BrowserPreferences snapshot;
  AppendChromiumPreferences(work_, &local, snapshot);
  EXPECT_FALSE(snapshot.at("dns_mode").editable);
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, PreferenceDraftRefreshFailureAndProfileIsolation) {
  ASSERT_TRUE(window_->Select("work"));
  auto* shell=window_->shell();
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));
  ASSERT_TRUE(window_->WriteTextPreference("font_standard","Georgia"));
  ASSERT_TRUE(window_->WriteTextPreference("accept_languages","en-US"));
  shell->OpenGeneralSettings();
  auto* font=shell->preference_textfield("font_standard");
  auto* languages=shell->preference_textfield("accept_languages");
  auto show=[&](EditorControl* field){
    auto* query=shell->preferences_search();
    query->SelectRange(gfx::Range(0,query->GetText().size()));
    query->input_client()->InsertText(field==font?u"Czcionka standardowa":u"Preferowane języki witryn",ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);
    shell->general_widget()->LayoutRootViewIfNecessary();
    ASSERT_TRUE(field->view()->IsDrawn());field->RequestFocus();
  };
  auto edit=[&](EditorControl* field,std::u16string text){
    ASSERT_NO_FATAL_FAILURE(show(field));
    field->SelectRange(gfx::Range(0,field->GetText().size()));
    field->input_client()->InsertText(text,ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);
  };
  const std::u16string invalid_font(129,u'x');
  edit(font,invalid_font);edit(languages,u"pl,,en");
  shell->SetWorkspaceAnimations(false);
  EXPECT_EQ(invalid_font,font->GetText());EXPECT_EQ(u"pl,,en",languages->GetText());
  ui::KeyEvent enter(ui::EventType::kKeyPressed,ui::VKEY_RETURN,ui::EF_NONE);
  ASSERT_TRUE(shell->HandlePreferenceEditorKey(languages,enter));
  EXPECT_EQ("en-US",window_->ReadPreferences().at("accept_languages").text);
  EXPECT_EQ(u"pl,,en",languages->GetText());EXPECT_EQ(invalid_font,font->GetText());
  ASSERT_NO_FATAL_FAILURE(show(font));
  ASSERT_TRUE(shell->HandlePreferenceEditorKey(font,enter));
  EXPECT_EQ("Georgia",window_->ReadPreferences().at("font_standard").text);
  EXPECT_EQ(invalid_font,font->GetText());
  edit(languages,u"pl, EN-us, pl");
  ASSERT_TRUE(shell->HandlePreferenceEditorKey(languages,enter));
  EXPECT_EQ(u"pl,en-US",languages->GetText());
  // A successful save releases only that key; another dirty field survives
  // external preference updates and all subsequent native refreshes.
  ASSERT_TRUE(window_->WriteTextPreference("font_standard","Arial"));
  ASSERT_TRUE(window_->WriteTextPreference("accept_languages","de"));
  shell->SetWorkspaceAnimations(true);
  EXPECT_EQ(invalid_font,font->GetText());EXPECT_EQ(u"de",languages->GetText());
  ui::CompositionText preedit;preedit.text=u"未";preedit.selection=gfx::Range(1);
  languages->SetCompositionText(preedit);ASSERT_TRUE(languages->IsIMEComposing());
  const std::u16string marked(languages->GetText());
  ASSERT_TRUE(window_->WriteTextPreference("accept_languages","fr"));
  shell->SetWorkspaceAnimations(false);
  EXPECT_TRUE(languages->IsIMEComposing());EXPECT_EQ(marked,languages->GetText());
  // Moving profiles discards dirty and composing UI without writing either
  // draft to the newly selected profile or overwriting saved work values.
  ASSERT_TRUE(window_->Select("personal"));shell->OpenGeneralSettings();
  EXPECT_FALSE(languages->IsIMEComposing());
  EXPECT_EQ(base::UTF8ToUTF16(window_->ReadPreferences().at("font_standard").text),font->GetText());
  EXPECT_EQ(base::UTF8ToUTF16(window_->ReadPreferences().at("accept_languages").text),languages->GetText());
  ASSERT_TRUE(window_->Select("work"));shell->OpenGeneralSettings();
  EXPECT_EQ(u"Arial",font->GetText());EXPECT_EQ(u"fr",languages->GetText());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, SitePreferenceExceptionsAndIsolation) {
  ASSERT_NO_FATAL_FAILURE(NavigateBoth("/preferences_probe.html"));
  ASSERT_TRUE(window_->Select("work"));
  ASSERT_TRUE(window_->WritePreference("javascript", 0));
  const auto allowed = embedded_test_server()->GetURL("allowed.test", "/preferences_probe.html");
  const auto origin = url::Origin::Create(allowed).Serialize();
  EXPECT_FALSE(window_->WriteSitePreference("file:///tmp/probe", "javascript", 1));
  EXPECT_FALSE(window_->WriteSitePreference("https://user:secret@example.com", "javascript", 1));
  EXPECT_FALSE(window_->WriteSitePreference("https://example.com/path", "javascript", 1));
  EXPECT_FALSE(window_->WriteSitePreference(origin, "javascript", 3));
  EXPECT_FALSE(window_->WriteSitePreference(origin, "unknown", 1));
  ASSERT_TRUE(window_->WriteSitePreference(origin, "javascript", 1));
  ASSERT_TRUE(content::NavigateToURL(window_->contents("work"), allowed));
  EXPECT_EQ(u"Scripts enabled", window_->contents("work")->GetTitle());
  EXPECT_EQ(0, window_->ReadPreferences().at("javascript").value);
  auto rules = window_->ReadSitePreferences();
  auto rule = std::find_if(rules.begin(), rules.end(), [](const auto& value) { return value.key == "javascript" && value.primary.find("allowed.test") != std::string::npos; });
  ASSERT_NE(rule, rules.end());
  EXPECT_TRUE(rule->editable);
  const SitePreferenceRule saved = *rule;
  EXPECT_FALSE(window_->RemoveSitePreference({"javascript", "*", "*", 1, true}));
  ASSERT_TRUE(window_->Select("personal"));
  EXPECT_TRUE(window_->ReadSitePreferences().empty());
  ASSERT_TRUE(window_->Select("work"));
  ASSERT_TRUE(window_->RemoveSitePreference(saved));
  ASSERT_TRUE(content::NavigateToURL(window_->contents("work"), embedded_test_server()->GetURL("allowed.test", "/preferences_probe.html?default")));
  EXPECT_EQ(u"Scripts blocked", window_->contents("work")->GetTitle());
  ASSERT_TRUE(window_->WriteSitePreference(origin, "sound", 2));
  EXPECT_TRUE(window_->contents("work")->IsAudioMuted());
  ASSERT_TRUE(window_->WriteSitePreference(origin, "sound", 0));
  EXPECT_FALSE(window_->contents("work")->IsAudioMuted());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, BrowsingHistoryPanelSearchOpenAndClear) {
  ASSERT_TRUE(window_->Select("work"));
  auto* shell = window_->shell();
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->tools_ready(); }));
  const auto recent_url = embedded_test_server()->GetURL("/storage_probe.html?history-recent");
  const auto older_url = embedded_test_server()->GetURL("/storage_probe.html?history-older");
  const auto private_url = embedded_test_server()->GetURL("/storage_probe.html?history-private");
  auto seed = [](Profile* profile, GURL url, std::u16string title, base::Time time) {
    history::URLRow row(url); row.set_title(title); row.set_visit_count(1); row.set_last_visit(time);
    HistoryServiceFactory::GetForProfile(profile, ServiceAccessType::EXPLICIT_ACCESS)->AddPagesWithDetails(
        {row}, history::SOURCE_OS_MIGRATION_IMPORTED);
  };
  seed(work_, recent_url, u"Recent entry", base::Time::Now() - base::Minutes(10));
  seed(work_, older_url, u"Older entry", base::Time::Now() - base::Days(2));
  seed(personal_, private_url, u"Private entry", base::Time::Now() - base::Minutes(10));
  ASSERT_TRUE(window_->RunShortcut("history"));
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->history_loaded(); }));
  ASSERT_TRUE(shell->history_visible());
  ASSERT_EQ(2u, shell->history_entries().size());
  EXPECT_EQ(recent_url, shell->history_entries()[0].url);
  EXPECT_EQ(older_url, shell->history_entries()[1].url);
  auto search = [&](std::u16string text) {
    auto* field=shell->history_search();const gfx::Range range(0,field->GetText().size());field->SelectRange(range);
    if(text.empty())field->input_client()->DeleteRange(range);
    else field->input_client()->InsertText(text,ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);
    EXPECT_TRUE(base::test::RunUntil([&] { return shell->history_loaded(); }));
  };
  search(u"nothing-matches-8372");
  EXPECT_TRUE(shell->history_entries().empty());
  search(u"Recent entry");
  ASSERT_EQ(1u, shell->history_entries().size());
  const auto tabs = window_->tab_count("work");
  auto* row=shell->history_results_view()->children()[0].get();
  ASSERT_TRUE(row);
  ActivateSidebarRow(row);
  ASSERT_TRUE(base::test::RunUntil([&] { return !shell->history_visible() && window_->tab_count("work") == tabs + 1; }));
  ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));
  EXPECT_EQ(recent_url, window_->contents("work")->GetLastCommittedURL());
  ASSERT_TRUE(window_->RunShortcut("history"));
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->history_loaded(); }));
  auto* clear = FindButton(shell->tools_widget()->GetContentsView(), u"Wyczyść historię…");
  ASSERT_TRUE(clear);
  ClickControl(clear);
  ASSERT_TRUE(window_->clear_data_dialog());
  EXPECT_EQ(static_cast<int>(ui::mojom::DialogButton::kCancel),
      window_->clear_data_dialog()->widget_delegate()->AsDialogDelegate()->GetDefaultDialogButton());
  EXPECT_TRUE(display::Screen::Get()->GetDisplayNearestWindow(shell->tools_widget()->GetNativeWindow()).work_area().Contains(
      window_->clear_data_dialog()->GetWindowBoundsInScreen()));
  window_->clear_data_dialog()->widget_delegate()->AsDialogDelegate()->CancelDialog();
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->clear_data_dialog(); }));
  EXPECT_FALSE(window_->clearing_browsing_data());
  ClickControl(clear);
  ASSERT_TRUE(window_->clear_data_dialog());
  window_->clear_data_dialog()->widget_delegate()->AsDialogDelegate()->AcceptDialog();
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->clearing_browsing_data() && shell->history_loaded(); }));
  ASSERT_EQ(1u, shell->history_entries().size());
  EXPECT_EQ(older_url, shell->history_entries()[0].url);
  // The other profile retains its independent history.
  ASSERT_TRUE(window_->Select("personal"));
  ASSERT_TRUE(window_->RunShortcut("history"));
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->history_loaded(); }));
  ASSERT_EQ(1u, shell->history_entries().size());
  EXPECT_EQ(private_url, shell->history_entries()[0].url);
  // All time clears only this profile and updates the open list.
  shell->ActivateToolsChoiceForTesting("history_period",3);
  clear = FindButton(shell->tools_widget()->GetContentsView(), u"Wyczyść historię…");
  ASSERT_TRUE(clear);
  ClickControl(clear);
  ASSERT_TRUE(window_->clear_data_dialog());
  window_->clear_data_dialog()->widget_delegate()->AsDialogDelegate()->AcceptDialog();
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->clearing_browsing_data() && shell->history_loaded(); }));
  EXPECT_TRUE(shell->history_entries().empty());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, HistoryRowsCocoaKeyboardAccessibilityAndURLFallback) {
  ASSERT_TRUE(window_->Select("work"));auto* shell=window_->shell();
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_ready();}));
  const auto target=embedded_test_server()->GetURL("/storage_probe.html?history-row-keyboard");
  const GURL fallback("https://history-fallback.test/empty-title");
  history::URLRow first(target);first.set_title(u"Wpis 🦀 אבג");first.set_visit_count(1);first.set_last_visit(base::Time::Now());
  history::URLRow second(fallback);second.set_visit_count(1);second.set_last_visit(base::Time::Now()-base::Minutes(1));
  HistoryServiceFactory::GetForProfile(work_,ServiceAccessType::EXPLICIT_ACCESS)->AddPagesWithDetails({first,second},history::SOURCE_OS_MIGRATION_IMPORTED);
  shell->OpenTools(3);ASSERT_TRUE(base::test::RunUntil([&]{return shell->history_loaded();}));
  ASSERT_EQ(2u,shell->history_entries().size());auto* list=shell->history_results_view();ASSERT_EQ(2u,list->children().size());
  const bool rust=shell->workspace_input()->rust_editor();
  for(size_t i=0;i<2;++i){auto* row=list->children()[i].get();ASSERT_TRUE(row);ui::AXNodeData ax;row->GetViewAccessibility().GetAccessibleNodeData(&ax);
    const auto& entry=shell->history_entries()[i];const auto title=entry.title.empty()?base::UTF8ToUTF16(entry.url.spec()):entry.title;
    EXPECT_EQ(ax::mojom::Role::kButton,ax.role);EXPECT_EQ(base::UTF8ToUTF16(entry.url.spec()),ax.GetString16Attribute(ax::mojom::StringAttribute::kName));
    EXPECT_NE(std::u16string::npos,ax.GetString16Attribute(ax::mojom::StringAttribute::kDescription).find(base::UTF8ToUTF16(entry.url.spec())));
    EXPECT_EQ(60,row->height());EXPECT_EQ(list->width(),row->width());EXPECT_EQ(static_cast<int>(i)*64,row->y());
    EXPECT_EQ(!rust,views::AsViewClass<views::Button>(row)!=nullptr);if(rust)EXPECT_TRUE(row->children().empty());else{size_t labels=0;bool primary=false;for(const auto& child:row->children())if(auto* label=views::AsViewClass<views::Label>(child)){++labels;primary|=label->GetText()==title;}EXPECT_EQ(2u,labels);EXPECT_TRUE(primary);}
  }
  auto* row=list->children()[0].get();ASSERT_TRUE(row);
  ui_controls::EnableUIControls();ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->tools_widget()->GetNativeWindow()));
  row->ScrollRectToVisible(row->GetLocalBounds());row->RequestFocus();ASSERT_TRUE(row->HasFocus());
  base::test::TestFuture<void> presented;auto* compositor=shell->tools_widget()->GetCompositor();ASSERT_TRUE(compositor);shell->tools_widget()->GetRootView()->SchedulePaint();
  compositor->RequestSuccessfulPresentationTimeForNextFrame(base::BindOnce([](base::OnceClosure done,const viz::FrameTimingDetails&){std::move(done).Run();},presented.GetCallback()));compositor->ScheduleFullRedraw();ASSERT_TRUE(presented.Wait());
  if(rust)ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_chrome_contains_text_for_testing("Wpis 🦀 אבג");}));
  const auto tabs=window_->tab_count("work");ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(shell->tools_widget()->GetNativeWindow(),ui::VKEY_SPACE,false,false,false,false));
  ASSERT_TRUE(base::test::RunUntil([&]{return !shell->history_visible()&&window_->tab_count("work")==tabs+1;}));
  ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));EXPECT_EQ(target,window_->contents("work")->GetLastCommittedURL());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, ToolsFormHistoryAdmissionErrorRecoveryAndPageReset) {
  ASSERT_TRUE(window_->Select("work"));
  auto* shell=window_->shell();
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_ready();}));
  const auto profile=window_->workspaces().front().profile_id;
  shell->OpenTools(3);
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->history_loaded();}));
  // A service failure is a completed response; wrong query/limit/profile cannot
  // replace it, even if those results contain otherwise valid rows.
  shell->SetHistoryResults(u"",profile,200,{},false,true);
  ASSERT_TRUE(shell->history_loaded());ASSERT_TRUE(shell->history_entries().empty());
  const std::vector<BrowsingHistoryEntry> poison={{GURL("https://stale.test/"),u"Stale",base::Time::Now()}};
  shell->SetHistoryResults(u"",profile,400,poison,true,false);
  shell->SetHistoryResults(u"older",profile,200,poison,true,false);
  shell->SetHistoryResults(u"","other-profile",200,poison,true,false);
  EXPECT_TRUE(shell->history_loaded());EXPECT_TRUE(shell->history_entries().empty());
  auto* field=shell->history_search();ASSERT_TRUE(field);
  field->RequestFocus();field->SelectRange(gfx::Range(0,field->GetText().size()));
  field->input_client()->InsertText(u"  Nowe 🦀 אבג  ",ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);
  EXPECT_FALSE(shell->history_loaded());
  shell->SetHistoryResults(u"",profile,200,poison,true,false);
  EXPECT_FALSE(shell->history_loaded());EXPECT_TRUE(shell->history_entries().empty());
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->history_loaded();}));
  shell->SetHistoryResults(u"Nowe 🦀 אבג",profile,200,
      {{GURL("https://current.test/"),u"Current",base::Time::Now()}},false,false);
  ASSERT_EQ(1u,shell->history_entries().size());EXPECT_EQ(u"Current",shell->history_entries()[0].title);
  shell->OpenTools(0);EXPECT_FALSE(shell->history_visible());
  shell->SetHistoryResults(u"Nowe 🦀 אבג",profile,200,poison,true,false);
  EXPECT_FALSE(shell->history_loaded());EXPECT_TRUE(shell->history_entries().empty());
  // Page clamping and reconstruction reset committed queries, matching Views.
  shell->OpenTools(99);ASSERT_TRUE(shell->history_visible());
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->history_loaded();}));
  EXPECT_TRUE(shell->history_search()->GetText().empty());
  shell->SetHistoryResults(u"",profile,200,{},false,true);
  shell->SetHistoryResults(u"Nowe 🦀 אבג",profile,200,poison,true,false);
  EXPECT_TRUE(shell->history_entries().empty());EXPECT_TRUE(shell->history_loaded());
  shell->OpenTools(-1);EXPECT_FALSE(shell->history_visible());
  EXPECT_TRUE(shell->tools_selection());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, BrowsingHistoryPaginationAndStaleSearch) {
  ASSERT_TRUE(window_->Select("work"));
  auto* shell = window_->shell();
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->tools_ready(); }));
  history::URLRows rows;
  for (int i = 0; i < 205; ++i) {
    history::URLRow row(GURL("https://history.test/" + std::to_string(i)));
    row.set_title(u"Paged history"); row.set_visit_count(1);
    row.set_last_visit(base::Time::Now() - base::Minutes(i + 1));
    rows.push_back(row);
  }
  HistoryServiceFactory::GetForProfile(work_, ServiceAccessType::EXPLICIT_ACCESS)->AddPagesWithDetails(
      rows, history::SOURCE_OS_MIGRATION_IMPORTED);
  shell->OpenTools(3);
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->history_loaded(); }));
  ASSERT_EQ(200u, shell->history_entries().size());
  auto* more = FindButton(shell->tools_widget()->GetContentsView(), u"Pokaż więcej");
  ASSERT_TRUE(more);
  EXPECT_TRUE(more->GetVisible());
  const bool rust=shell->workspace_input()->rust_editor();EXPECT_EQ(rust,!views::AsViewClass<views::MdTextButton>(more));
  ui_controls::EnableUIControls();ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->tools_widget()->GetNativeWindow()));
  base::test::TestFuture<void> presented;auto* compositor=shell->tools_widget()->GetCompositor();ASSERT_TRUE(compositor);
  compositor->RequestSuccessfulPresentationTimeForNextFrame(base::BindOnce([](base::OnceClosure done,const viz::FrameTimingDetails&){std::move(done).Run();},presented.GetCallback()));compositor->ScheduleFullRedraw();ASSERT_TRUE(presented.Wait());
  more->ScrollRectToVisible(more->GetLocalBounds());
  if(rust)ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_actions_contains_scene_text_for_testing("Pokaż więcej");}));
  more->RequestFocus();ASSERT_TRUE(more->HasFocus());
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(shell->tools_widget()->GetNativeWindow(),ui::VKEY_SPACE,false,false,false,false));
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->history_loaded(); }));
  EXPECT_EQ(205u, shell->history_entries().size());
  EXPECT_FALSE(more->GetVisible());if(rust)EXPECT_FALSE(more->parent()->GetVisible());
  // Old results cannot replace a newer search, even with the same profile.
  window_->QueryBrowsingHistory(u"Paged", 400);
  auto* field=shell->history_search();field->SelectRange(gfx::Range(0,field->GetText().size()));
  field->input_client()->InsertText(u"no-results",ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->history_loaded(); }));
  EXPECT_TRUE(shell->history_entries().empty());
  shell->SetHistoryResults(u"Paged", window_->workspaces()[0].profile_id, 400,
      {{GURL("https://history.test/stale"), u"Stale", base::Time::Now()}}, false, false);
  EXPECT_TRUE(shell->history_entries().empty());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, ClearBrowsingDataConfirmationAndScope) {
  ASSERT_NO_FATAL_FAILURE(NavigateBoth());
  EXPECT_EQ("work", content::EvalJs(window_->contents("work"), "writeMarker('work')"));
  EXPECT_EQ("personal", content::EvalJs(window_->contents("personal"), "writeMarker('personal')"));
  ASSERT_TRUE(window_->Select("work"));
  const std::string profile_id = window_->workspaces().front().profile_id;
  auto* shell = window_->shell();
  shell->OpenGeneralSettings();
  BrowsingDataSelection selection{3, false, true, false};
  window_->RequestClearBrowsingData(selection);
  ASSERT_TRUE(window_->clear_data_dialog());
  EXPECT_FALSE(window_->clearing_browsing_data());
  window_->clear_data_dialog()->widget_delegate()->AsDialogDelegate()->CancelDialog();
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->clear_data_dialog(); }));
  EXPECT_EQ("work|work|work|work", content::EvalJs(window_->contents("work"), "readMarker()"));
  EXPECT_FALSE(window_->ClearBrowsingData("no-profile", selection));
  EXPECT_FALSE(window_->ClearBrowsingData(profile_id, {9, false, true, false}));
  EXPECT_FALSE(window_->ClearBrowsingData(profile_id, {3, false, false, false}));
  window_->RequestClearBrowsingData(selection);
  ASSERT_TRUE(window_->clear_data_dialog());
  // A confirmation always applies to the profile named in the dialog.
  ASSERT_TRUE(window_->Select("personal"));
  window_->clear_data_dialog()->widget_delegate()->AsDialogDelegate()->AcceptDialog();
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->clearing_browsing_data(); }));
  EXPECT_EQ("personal|personal|personal|personal", content::EvalJs(window_->contents("personal"), "readMarker()"));
  // The still-open work page may retain handles/sessionStorage; reload to
  // inspect the durable cookie, localStorage and IndexedDB state.
  ASSERT_TRUE(window_->Select("work"));
  ASSERT_TRUE(content::NavigateToURL(window_->contents("work"), embedded_test_server()->GetURL("/storage_probe.html?after-clear")));
  EXPECT_EQ("", content::EvalJs(window_->contents("work"), "document.cookie"));
  EXPECT_EQ("", content::EvalJs(window_->contents("work"), "localStorage.getItem('zitter') || ''"));
  EXPECT_EQ("", content::EvalJs(window_->contents("work"), "readMarker().then(value => value.split('|')[2])"));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, ProfilePreferencesAndSearch) {
  ASSERT_NO_FATAL_FAILURE(NavigateBoth("/preferences_probe.html"));
  ASSERT_TRUE(window_->Select("work"));
  auto* shell = window_->shell();
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->appearance_ready(); }));
  shell->OpenGeneralSettings();
  auto search = [&](std::u16string text) {
    auto* field=shell->preferences_search();
    const gfx::Range range(0,field->GetText().size());field->SelectRange(range);
    if(text.empty())field->input_client()->DeleteRange(range);
    else field->input_client()->InsertText(text,ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);
    shell->general_widget()->LayoutRootViewIfNecessary();
  };
  search(u"  KAMERA  ");
  ui::KeyEvent enter_preferences(ui::EventType::kKeyPressed, ui::VKEY_RETURN, ui::EF_NONE);
  EXPECT_FALSE(shell->HandlePreferenceEditorKey(shell->preferences_search(), enter_preferences));
  EXPECT_TRUE(shell->preference_toggle("camera")->IsDrawn());
  EXPECT_FALSE(shell->theme_picker()->IsDrawn());
  ClickControl(shell->preference_toggle("camera"));
  EXPECT_EQ(0, window_->ReadPreferences().at("camera").value);
  EXPECT_EQ(CONTENT_SETTING_BLOCK, HostContentSettingsMapFactory::GetForProfile(work_)
      ->GetDefaultContentSetting(ContentSettingsType::MEDIASTREAM_CAMERA));
  search(u"no-such-preference-123");
  EXPECT_FALSE(shell->preference_toggle("camera")->IsDrawn());
  search(u"");
  EXPECT_TRUE(shell->theme_picker()->IsDrawn());
  EXPECT_FALSE(window_->WritePreference("arbitrary.pref", 1));
  EXPECT_FALSE(window_->WritePreference("javascript", 7));
  EXPECT_FALSE(window_->WritePreference("zoom", -1));
  EXPECT_FALSE(window_->WritePreference("font_size", 42));
  EXPECT_FALSE(window_->WritePreference("minimum_font_size", -1));
  ASSERT_TRUE(window_->WritePreference("javascript", 0));
  ASSERT_TRUE(window_->WritePreference("do_not_track", 1));
  ASSERT_TRUE(window_->WritePreference("sound", 0));
  EXPECT_TRUE(window_->contents("work")->IsAudioMuted());
  ASSERT_TRUE(window_->WritePreference("font_size", 3));
  EXPECT_EQ(18, work_->GetPrefs()->GetInteger(prefs::kWebKitDefaultFontSize));
  ASSERT_TRUE(window_->WritePreference("zoom", 4));
  EXPECT_NEAR(1.25, work_->GetZoomLevelPrefs()->GetDefaultZoomFactor(), 0.001);
  ASSERT_TRUE(content::NavigateToURL(window_->contents("work"), embedded_test_server()->GetURL("/preferences_probe.html?blocked")));
  EXPECT_EQ(u"Scripts blocked", window_->contents("work")->GetTitle());
  ASSERT_TRUE(window_->Select("personal"));
  EXPECT_EQ(1, window_->ReadPreferences().at("javascript").value);
  EXPECT_EQ(1, window_->ReadPreferences().at("camera").value);
  EXPECT_FALSE(window_->contents("personal")->IsAudioMuted());
  EXPECT_EQ(u"Scripts enabled", window_->contents("personal")->GetTitle());
  ASSERT_TRUE(window_->Select("work"));
  EXPECT_EQ(0, window_->ReadPreferences().at("camera").value);
  // Recreating the native window must not reset persisted profile preferences.
  window_.reset(); window_ = std::make_unique<WorkspaceWindow>(work_, personal_);
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_ready(); }));
  ASSERT_TRUE(window_->Select("work"));
  EXPECT_EQ(0, window_->ReadPreferences().at("javascript").value);
  EXPECT_EQ(1, window_->ReadPreferences().at("do_not_track").value);
  EXPECT_EQ(4, window_->ReadPreferences().at("zoom").value);
}

// Original Views row text/order is the regression oracle for both builders.
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, PreferenceCatalogueRowsOrderCopyAndFilter) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->OpenGeneralSettings();
  using Copy=std::pair<std::u16string,std::u16string>;
  const std::vector<std::vector<Copy>> expected={
    {{u"Motyw aplikacji",u"Kolory interfejsu i stron obsługujących motyw systemowy."},{u"Zawsze widoczny panel kart",u"Przypnij karty i przestrzenie przy lewej krawędzi."},{u"Animacje przestrzeni",u"Płynne przejścia. Systemowe ograniczenie ruchu ma pierwszeństwo."},{u"Kolor i nazwa przestrzeni",u"Własny akcent kolorystyczny i przypisanie profilu."},{u"Time Machine przestrzeni",u"Snapshoty, zamknięte karty i usypianie całych projektów."}},
    {{u"Domyślna wyszukiwarka",u"Używana, gdy wpiszesz zapytanie w pasku adresu."},{u"Wyszukiwarki i prefiksy",u"Własne wyszukiwarki: g zapytanie, w hasło, yt film…"},{u"Skróty klawiszowe",u"Schemat Arc, własne kombinacje i wykrywanie konfliktów."},{u"Szybciej z klawiaturą",u"⌘ L  adres i wyszukiwanie\n⌘ K  karty, przestrzenie i historia\n⌘ T  nowa karta     ⌘ ,  preferencje"}},
    {{u"Przekazuj adres strony odsyłającej",u"Nagłówek Referer pomaga witrynie rozpoznać, skąd pochodzi wejście. Wyłączenie może zmienić działanie niektórych serwisów."},{u"Zawsze używaj HTTPS",u"Wymagaj bezpiecznego połączenia. Witryny bez HTTPS mogą wyświetlić ostrzeżenie."},{u"Blokuj cookies innych firm",u"Ogranicz śledzenie między witrynami. Niektóre logowania mogą wymagać cookies."},{u"Wysyłaj Do Not Track",u"Poproś witryny o nieśledzenie. Respektowanie prośby zależy od witryny."},{u"Wstępne połączenia ze stronami",u"Przygotowuj połączenie podczas wpisywania adresu. Może użyć sieci, nawet jeśli nie otworzysz strony. Ustawienie dotyczy tego profilu."},{u"Raporty kliknięć w linki",u"Zezwalaj na wysyłanie sygnałów ping po kliknięciu odnośnika."},{u"Historia przeglądania",u"Wyszukuj odwiedzone strony i czyść historię tego profilu."}},
    {{u"Kamera",u"Włączone: domyślna polityka „pytaj”. Wyłączone: blokuj. Wyjątki witryn mają pierwszeństwo."},{u"Mikrofon",u"Włączone: domyślna polityka „pytaj”. Wyłączone: blokuj. Wyjątki witryn mają pierwszeństwo."},{u"Lokalizacja",u"Włączone: domyślna polityka „pytaj”. Wyłączone: blokuj. Wyjątki witryn mają pierwszeństwo."},{u"Powiadomienia",u"Włączone: domyślna polityka „pytaj”. Wyłączone: blokuj. Wyjątki witryn mają pierwszeństwo."},{u"Odczyt schowka",u"Włączone: domyślna polityka „pytaj”. Wyłączone: blokuj. Wyjątki witryn mają pierwszeństwo."},{u"Wyjątek dla witryny",u"Wpisz pełny adres witryny bez ścieżki, np. https://example.com. Wyjątek zastępuje politykę domyślną w tym profilu."},{u"Zapisane wyjątki",u"Własne reguły możesz usunąć, aby wrócić do ustawień domyślnych. Reguły zarządzane pozostają tylko do odczytu."}},
    {{u"Domyślne powiększenie",u"Skala stron bez własnego, zapisanego powiększenia."},{u"Rozmiar tekstu",u"Domyślna wielkość czcionki na stronach."},{u"Minimalny rozmiar tekstu",u"Zapobiegaj wyświetlaniu zbyt drobnych liter."},{u"Rozmiar tekstu o stałej szerokości",u"Domyślna wielkość tekstu w blokach kodu i elementach monospace."},{u"Czcionka standardowa",u"Nazwa rodziny czcionki używanej, gdy witryna nie wybierze własnej."},{u"Czcionka szeryfowa",u"Rodzina dla tekstu serif. Musi być zainstalowana w systemie."},{u"Czcionka bezszeryfowa",u"Rodzina dla tekstu sans-serif. Musi być zainstalowana w systemie."},{u"Czcionka o stałej szerokości",u"Rodzina monospace, używana m.in. w blokach kodu."},{u"JavaScript",u"Skrypty potrzebne do działania większości aplikacji internetowych."},{u"Wyświetlaj obrazy",u"Pozwól witrynom ładować zdjęcia i grafiki."},{u"Wyskakujące okna",u"Pozwól stronom otwierać dodatkowe okna. Domyślnie zablokowane."},{u"Dźwięk na stronach",u"Domyślna zgoda na odtwarzanie dźwięku."}},
    {{u"Proponuj zapisywanie haseł",u"Zapisuj dane logowania w menedżerze haseł tego profilu."},{u"Automatyczne logowanie",u"Korzystaj z zapisanych danych logowania, gdy witryna na to pozwala."},{u"Zapisane hasła",u"Przeglądaj i usuwaj loginy aktywnego profilu."},{u"Profile przeglądania",u"Oddziel cookies, historię i dane logowania między profilami."},{u"Import z przeglądarki",u"Przenieś profile, przestrzenie, foldery i zakładki."}},
    {{u"Pytaj, gdzie zapisać każdy plik",u"Wybieraj miejsce zapisu przy każdym pobieraniu."},{u"Folder pobierania",u"Domyślne miejsce zapisu plików w tym profilu."}},
    {{u"Preferowane języki witryn",u"Kody języków w kolejności preferencji, oddzielone przecinkami. Np. pl, en-US, en. Zmiana dotyczy treści witryn; interfejs Zitter pozostaje po polsku."},{u"Sprawdzaj pisownię",u"Podkreślaj błędy w polach tekstowych na stronach. Na macOS słowniki wybiera system."},{u"Rozszerzone sprawdzanie pisowni",u"Wysyłaj wpisywany tekst do usługi Google w celu sprawdzania pisowni. Wymaga włączonego sprawdzania pisowni."}},
    {{u"Wyczyść dane przeglądania",u"Dotyczy wyłącznie aktywnego profilu. Pobrane pliki, zapisane hasła i przypięte karty pozostaną zachowane. Przed usunięciem zobaczysz potwierdzenie."}},
    {{u"Nawigacja kursorem tekstowym",u"Poruszaj się po tekstach witryn za pomocą klawiatury i zaznaczaj je bez myszy."},{u"Akceleracja sprzętowa",u"Używaj procesora graficznego do renderowania. Ustawienie wspólne dla całej aplikacji; działa po ponownym uruchomieniu Zitter."},{u"Bezpieczny DNS",u"Wspólne dla całej aplikacji. Tryb automatyczny może używać DNS systemowego. Tryb ścisły wymaga wcześniej zapisanego adresu DNS-over-HTTPS."},{u"Adres serwera DNS-over-HTTPS",u"Wspólny dla całej aplikacji. Wpisz szablon HTTPS, np. https://dns.google/dns-query{?dns}. Najpierw zapisz adres, następnie włącz tryb ścisły."},{u"Ustawienia proxy systemowego",u"Zitter korzysta z konfiguracji proxy systemu operacyjnego."},{u"Certyfikaty systemowe",u"Otwórz systemowy magazyn certyfikatów i ustawienia zaufania."}},
  };
  for(size_t category=0;category<expected.size();++category){SCOPED_TRACE(category);auto controls=shell->preference_rows_for_testing(category);ASSERT_EQ(expected[category].size(),controls.size());
    for(size_t index=0;index<controls.size();++index){SCOPED_TRACE(index);std::vector<std::u16string> copies;
      auto visit=[&](auto&& self,views::View* view)->void{ui::AXNodeData ax;view->GetViewAccessibility().GetAccessibleNodeData(&ax);if(ax.role==ax::mojom::Role::kStaticText){copies.push_back(ax.GetString16Attribute(ax::mojom::StringAttribute::kName));return;}if(ax.role==ax::mojom::Role::kTextField||views::AsViewClass<views::Button>(view))return;for(const auto& child:view->children())self(self,child.get());};
      ASSERT_FALSE(controls[index]->children().empty());visit(visit,controls[index]->children().front());ASSERT_GE(copies.size(),2u);EXPECT_EQ(expected[category][index].first,copies[0]);EXPECT_EQ(expected[category][index].second,copies[1]);
    }
  }
  EXPECT_EQ(kSitePreferenceNames.size(),shell->preference_choice_count_for_testing("site_type"));for(size_t i=0;i<kSitePreferenceNames.size();++i)EXPECT_EQ(kSitePreferenceNames[i],shell->preference_choice_label_for_testing("site_type",i));
  auto* query=shell->preferences_search();query->SelectRange(gfx::Range(0,query->GetText().size()));query->input_client()->InsertText(u"słowniki",ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);shell->general_widget()->LayoutRootViewIfNecessary();EXPECT_TRUE(shell->preference_toggle("spellcheck")->IsDrawn());EXPECT_FALSE(shell->theme_picker()->IsDrawn());EXPECT_FALSE(shell->preference_toggle("camera")->IsDrawn());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, PreferenceCatalogueToolsRoutes) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));
  const std::vector<std::pair<std::u16string,int>> routes={{u"Time Machine przestrzeni",0},{u"Skróty klawiszowe",1},{u"Wyszukiwarki i prefiksy",2},{u"Historia przeglądania",3}};
  for(const auto& [title,page]:routes){SCOPED_TRACE(page);shell->OpenGeneralSettings();auto* query=shell->preferences_search();query->SelectRange(gfx::Range(0,query->GetText().size()));query->input_client()->InsertText(title,ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);shell->general_widget()->LayoutRootViewIfNecessary();auto* button=FindButton(shell->general_widget()->GetContentsView(),title);ASSERT_TRUE(button&&button->IsDrawn());ClickControl(button);ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_widget()->IsVisible();}));EXPECT_FALSE(shell->general_widget()->IsVisible());EXPECT_EQ(page==0,shell->snapshot_input()!=nullptr);EXPECT_EQ(page==1,shell->shortcut_input()!=nullptr);EXPECT_EQ(page==2,shell->provider_input(0)!=nullptr);EXPECT_EQ(page==3,shell->history_visible());}
}

// Add deterministic overflow without inflating the native window minimum.
// View::GetMinimumSize asks for preferred size at zero available width.
class FormOverflowProbeView final : public views::View {
  METADATA_HEADER(FormOverflowProbeView,views::View)
 public:
  gfx::Size CalculatePreferredSize(const views::SizeBounds& bounds) const override {
    return gfx::Size(1,bounds.width().is_bounded()&&bounds.width().value()==0?0:900);
  }
};
BEGIN_METADATA(FormOverflowProbeView)
END_METADATA
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, FormViewportRevealResizeAndReflow) {
  window_->widget()->Show();auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));
  for(bool importing:{false,true}){
    SCOPED_TRACE(importing?"import":"workspace");
    shell->CloseOverlays();window_->widget()->SetBounds(gfx::Rect(40,40,700,660));if(importing)shell->OpenImport();else shell->OpenWorkspaceSettings();
    auto* viewport=importing?shell->import_viewport_for_testing():shell->workspace_viewport_for_testing();auto* widget=importing?shell->import_widget():shell->settings_widget();ASSERT_TRUE(viewport);ASSERT_TRUE(widget);widget->LayoutRootViewIfNecessary();
    EXPECT_EQ(shell->workspace_input()->rust_editor(),viewport->rust_owned());if(viewport->rust_owned())EXPECT_FALSE(views::AsViewClass<views::ScrollView>(viewport->view()));
    auto* body=viewport->contents();ASSERT_TRUE(body);auto* first=body->children().front().get();auto* overflow=body->AddChildViewAt(std::make_unique<FormOverflowProbeView>(),1);widget->LayoutRootViewIfNecessary();
    ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(widget->GetNativeWindow()));base::test::TestFuture<void> presented;auto* compositor=widget->GetCompositor();ASSERT_TRUE(compositor);widget->GetRootView()->SchedulePaint();compositor->RequestSuccessfulPresentationTimeForNextFrame(base::BindOnce([](base::OnceClosure done,const viz::FrameTimingDetails&){std::move(done).Run();},presented.GetCallback()));compositor->ScheduleFullRedraw();ASSERT_TRUE(presented.Wait());
    ASSERT_GT(viewport->maximum(),0)<<" widget="<<widget->GetWindowBoundsInScreen().ToString()<<" host="<<viewport->view()->bounds().ToString()<<" body="<<viewport->contents()->bounds().ToString()<<" preferred="<<viewport->contents()->GetPreferredSize().ToString();viewport->ScrollTo(viewport->maximum());ASSERT_TRUE(base::test::RunUntil([&]{return first->GetVisibleBounds().IsEmpty();}));first->ScrollRectToVisible(first->GetLocalBounds());ASSERT_TRUE(base::test::RunUntil([&]{return !first->GetVisibleBounds().IsEmpty();}));
    for(int width:{320,440,520}){widget->SetBounds(gfx::Rect(40,40,width,360));widget->LayoutRootViewIfNecessary();EXPECT_EQ(body->GetHeightForWidth(body->width()),body->height());EXPECT_EQ(std::max(0,body->height()-viewport->visible_rect().height()),viewport->maximum());viewport->ScrollTo(viewport->maximum());EXPECT_LE(viewport->offset(),viewport->maximum());}
    body->RemoveChildViewT(overflow);widget->LayoutRootViewIfNecessary();EXPECT_LE(viewport->offset(),viewport->maximum());first->ScrollRectToVisible(first->GetLocalBounds());EXPECT_FALSE(first->GetVisibleBounds().IsEmpty());
  }
}
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, FormViewportWheelAXAndDraftPreservation) {
  window_->widget()->Show();auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));
  for(bool importing:{false,true}){
    SCOPED_TRACE(importing?"import":"workspace");
    shell->CloseOverlays();window_->widget()->SetBounds(gfx::Rect(40,40,700,660));if(importing)shell->OpenImport();else shell->OpenWorkspaceSettings();auto* viewport=importing?shell->import_viewport_for_testing():shell->workspace_viewport_for_testing();auto* widget=importing?shell->import_widget():shell->settings_widget();ASSERT_TRUE(viewport);ASSERT_TRUE(widget);
    auto* spacer=viewport->contents()->AddChildViewAt(std::make_unique<FormOverflowProbeView>(),1);widget->SetBounds(gfx::Rect(40,40,440,360));widget->LayoutRootViewIfNecessary();
    ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(widget->GetNativeWindow()));base::test::TestFuture<void> presented;auto* compositor=widget->GetCompositor();ASSERT_TRUE(compositor);widget->GetRootView()->SchedulePaint();compositor->RequestSuccessfulPresentationTimeForNextFrame(base::BindOnce([](base::OnceClosure done,const viz::FrameTimingDetails&){std::move(done).Run();},presented.GetCallback()));compositor->ScheduleFullRedraw();ASSERT_TRUE(presented.Wait());widget->LayoutRootViewIfNecessary();ASSERT_GT(viewport->maximum(),0)<<" widget="<<widget->GetWindowBoundsInScreen().ToString()<<" host="<<viewport->view()->bounds().ToString()<<" body="<<viewport->contents()->bounds().ToString()<<" preferred="<<viewport->contents()->GetPreferredSize().ToString();
    if(!importing)shell->workspace_input()->SetText(u"Niezapisany szkic 日本語");const auto draft=shell->workspace_input()->GetText();viewport->ScrollTo(0);ui::MouseWheelEvent wheel(gfx::Vector2d(0,-120),gfx::PointF(10,10),gfx::PointF(10,10),base::TimeTicks::Now(),0,0);EXPECT_TRUE(viewport->view()->OnMouseWheel(wheel));EXPECT_GT(viewport->offset(),0);
    ui::AXActionData down;down.action=ax::mojom::Action::kScrollDown;EXPECT_TRUE(viewport->view()->HandleAccessibleAction(down));ui::AXNodeData ax;viewport->view()->GetViewAccessibility().GetAccessibleNodeData(&ax);EXPECT_EQ(ax::mojom::Role::kScrollView,ax.role);EXPECT_EQ(viewport->maximum(),ax.GetIntAttribute(ax::mojom::IntAttribute::kScrollYMax));EXPECT_EQ(draft,shell->workspace_input()->GetText());
    viewport->contents()->RemoveChildViewT(spacer);widget->LayoutRootViewIfNecessary();EXPECT_LE(viewport->offset(),viewport->maximum());
  }
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, PreferenceViewportFilterRevealAndResize) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));window_->widget()->SetBounds(gfx::Rect(40,40,700,660));shell->OpenGeneralSettings();auto* viewport=shell->preference_viewport_for_testing();ASSERT_TRUE(viewport);EXPECT_EQ(shell->workspace_input()->rust_editor(),viewport->rust_owned());if(viewport->rust_owned())EXPECT_FALSE(views::AsViewClass<views::ScrollView>(viewport->view()));
  auto filter=[&](std::u16string text){auto* field=shell->preferences_search();field->SelectRange(gfx::Range(0,field->GetText().size()));field->input_client()->InsertText(text,ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);shell->general_widget()->LayoutRootViewIfNecessary();};
  filter(u"czcion");ASSERT_GT(viewport->maximum(),0);auto* first=shell->preference_textfield("font_standard")->view();auto* last=shell->preference_textfield("font_fixed")->view();auto* first_row=first->parent()->parent();auto* section=first_row->parent();auto* overflow=section->AddChildViewAt(std::make_unique<views::View>(),section->GetIndexOf(first_row).value()+1);overflow->SetPreferredSize(gfx::Size(1,900));shell->general_widget()->LayoutRootViewIfNecessary();
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->general_widget()->GetNativeWindow()));base::test::TestFuture<void> presented;auto* compositor=shell->general_widget()->GetCompositor();ASSERT_TRUE(compositor);compositor->RequestSuccessfulPresentationTimeForNextFrame(base::BindOnce([](base::OnceClosure done,const viz::FrameTimingDetails&){std::move(done).Run();},presented.GetCallback()));compositor->ScheduleFullRedraw();ASSERT_TRUE(presented.Wait());
  viewport->ScrollTo(viewport->maximum());ASSERT_TRUE(base::test::RunUntil([&]{return last->GetVisibleBounds().height()==last->height();}));EXPECT_TRUE(first->GetVisibleBounds().IsEmpty());first->ScrollRectToVisible(first->GetLocalBounds());EXPECT_FALSE(first->GetVisibleBounds().IsEmpty());
  filter(u"no-result-preference-viewport-483");EXPECT_EQ(0,viewport->offset());EXPECT_EQ(0,viewport->maximum());filter(u"czcion");EXPECT_EQ(0,viewport->offset());
  for(auto size:{gfx::Size(700,660),gfx::Size(1000,860)}){window_->widget()->SetBounds(gfx::Rect(gfx::Point(40,40),size));shell->OpenGeneralSettings();shell->general_widget()->LayoutRootViewIfNecessary();viewport->ScrollTo(viewport->maximum());EXPECT_GE(viewport->offset(),0);EXPECT_LE(viewport->offset(),viewport->maximum());last->ScrollRectToVisible(last->GetLocalBounds());EXPECT_FALSE(last->GetVisibleBounds().IsEmpty());}
}
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, PreferenceViewportWheelAXThemeAndProfileScope) {
  ASSERT_NO_FATAL_FAILURE(NavigateBoth());ASSERT_TRUE(window_->Select("work"));auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));window_->widget()->SetBounds(gfx::Rect(40,40,700,660));shell->OpenGeneralSettings();auto* viewport=shell->preference_viewport_for_testing();ASSERT_TRUE(viewport);
  auto filter=[&]{auto* field=shell->preferences_search();field->SelectRange(gfx::Range(0,field->GetText().size()));field->input_client()->InsertText(u"czcion",ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);shell->general_widget()->LayoutRootViewIfNecessary();};filter();ASSERT_GT(viewport->maximum(),0);viewport->ScrollTo(0);
  ui::MouseWheelEvent wheel(gfx::Vector2d(0,-120),gfx::PointF(10,10),gfx::PointF(10,10),base::TimeTicks::Now(),0,0);EXPECT_TRUE(viewport->view()->OnMouseWheel(wheel));EXPECT_GT(viewport->offset(),0);ui::AXActionData down;down.action=ax::mojom::Action::kScrollDown;EXPECT_TRUE(viewport->view()->HandleAccessibleAction(down));EXPECT_GT(viewport->offset(),0);
  ui::AXNodeData before;viewport->view()->GetViewAccessibility().GetAccessibleNodeData(&before);EXPECT_EQ(ax::mojom::Role::kScrollView,before.role);EXPECT_EQ(viewport->maximum(),before.GetIntAttribute(ax::mojom::IntAttribute::kScrollYMax));
  shell->SetThemeMode(ThemeMode::kDark);shell->general_widget()->LayoutRootViewIfNecessary();EXPECT_EQ(0,viewport->offset());EXPECT_EQ(ThemeMode::kDark,shell->theme_mode());EXPECT_EQ(u"czcion",shell->preferences_search()->GetText());viewport->ScrollTo(viewport->maximum());shell->SetThemeMode(ThemeMode::kLight);shell->general_widget()->LayoutRootViewIfNecessary();EXPECT_EQ(0,viewport->offset());EXPECT_EQ(ThemeMode::kLight,shell->theme_mode());EXPECT_EQ(u"czcion",shell->preferences_search()->GetText());
  // Hidden native scrollbars retain an old range; Rust exports current metrics.
  ui::AXNodeData ax;viewport->view()->GetViewAccessibility().GetAccessibleNodeData(&ax);EXPECT_EQ(ax::mojom::Role::kScrollView,ax.role);if(viewport->rust_owned()||viewport->maximum()>0)EXPECT_EQ(viewport->maximum(),ax.GetIntAttribute(ax::mojom::IntAttribute::kScrollYMax));
  const auto personal=personal_->GetPrefs()->GetString(prefs::kWebKitStandardFontFamily);ASSERT_TRUE(window_->Select("personal"));shell->OpenGeneralSettings();filter();EXPECT_EQ(0,viewport->offset());EXPECT_EQ(personal,personal_->GetPrefs()->GetString(prefs::kWebKitStandardFontFamily));EXPECT_EQ(base::UTF8ToUTF16(personal),shell->preference_textfield("font_standard")->GetText());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, PreferenceChromeTextMetricsAndDynamicStatus) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->OpenGeneralSettings();
  auto find=[&](std::u16string_view name){auto visit=[&](auto&& self,views::View* view)->views::View*{ui::AXNodeData node;view->GetViewAccessibility().GetAccessibleNodeData(&node);if(node.role==ax::mojom::Role::kStaticText&&node.GetString16Attribute(ax::mojom::StringAttribute::kName)==name)return view;for(const auto& child:view->children())if(auto* found=self(self,child.get()))return found;return nullptr;};return visit(visit,shell->general_widget()->GetContentsView());};
  struct Sample{std::u16string text;int delta;gfx::Font::Weight weight;bool multiline;};
  const std::vector<Sample> samples={{u"Preferencje",9,gfx::Font::Weight::SEMIBOLD,false},{u"Czcionka standardowa",0,gfx::Font::Weight::MEDIUM,true},{u"Nazwa rodziny czcionki używanej, gdy witryna nie wybierze własnej.",-1,gfx::Font::Weight::NORMAL,true},{u"Rodzina dla tekstu serif. Musi być zainstalowana w systemie.",-1,gfx::Font::Weight::NORMAL,true}};
  const bool rust=shell->workspace_input()->rust_editor();
  for(const auto& sample:samples){auto* copy=find(sample.text);ASSERT_TRUE(copy)<<base::UTF16ToUTF8(sample.text);EXPECT_EQ(!rust,views::AsViewClass<views::Label>(copy)!=nullptr);views::Label reference(sample.text);reference.SetFontList(gfx::FontList().DeriveWithSizeDelta(sample.delta).DeriveWithWeight(sample.weight));reference.SetMultiLine(sample.multiline);for(int width:{80,160,320})EXPECT_EQ(reference.GetHeightForWidth(width),copy->GetHeightForWidth(width))<<base::UTF16ToUTF8(sample.text)<<" width="<<width;}
  const std::u16string status=u"Błąd zapisu 日本語 — ponów zapis ustawienia w bieżącym profilu.";shell->SetPreferencesStatus(status,true);shell->general_widget()->LayoutRootViewIfNecessary();auto* message=find(status);ASSERT_TRUE(message);EXPECT_TRUE(message->IsDrawn());if(rust)ASSERT_TRUE(base::test::RunUntil([&]{return shell->preference_chrome_contains_text_for_testing(base::UTF16ToUTF8(status));}));
  ASSERT_TRUE(window_->Select("personal"));shell->OpenGeneralSettings();const auto expected=u"Profil: "+base::UTF8ToUTF16(window_->profiles()[1].name)+u" · Ustawienia wspólne dla aplikacji są oznaczone w opisach.";ASSERT_TRUE(find(expected));if(rust)ASSERT_TRUE(base::test::RunUntil([&]{return shell->preference_chrome_contains_text_for_testing(base::UTF16ToUTF8(expected));}));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, PreferenceNavigationCocoaFilterAndResponsiveChoice) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));window_->widget()->SetBounds(gfx::Rect(40,40,1000,860));shell->OpenGeneralSettings();shell->general_widget()->LayoutRootViewIfNecessary();auto* languages=shell->preference_navigation_for_testing(7);ASSERT_TRUE(languages&&languages->IsDrawn());const bool rust=shell->workspace_input()->rust_editor();
  if(rust)ASSERT_TRUE(base::test::RunUntil([&]{return shell->preference_navigation_metal_visible_for_testing()&&shell->preference_navigation_contains_text_for_testing("Zaawansowane");}));
  ui_controls::EnableUIControls();auto activate=[&](views::View* control){EXPECT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->general_widget()->GetNativeWindow()));control->RequestFocus();EXPECT_TRUE(control->HasFocus());EXPECT_TRUE(ui_test_utils::SendKeyPressToWindowSync(shell->general_widget()->GetNativeWindow(),ui::VKEY_SPACE,false,false,false,false));};activate(languages);
  EXPECT_EQ(7u,shell->preference_category_for_testing());EXPECT_EQ(7u,shell->preference_choice_selection_for_testing("category"));EXPECT_TRUE(shell->preference_textfield("accept_languages")->view()->IsDrawn());EXPECT_FALSE(shell->preference_textfield("font_standard")->view()->IsDrawn());
  auto* search=shell->preferences_search();search->SelectRange(gfx::Range(0,search->GetText().size()));search->input_client()->InsertText(u"  KAMERA  ",ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);shell->general_widget()->LayoutRootViewIfNecessary();EXPECT_TRUE(shell->preference_toggle("camera")->IsDrawn());EXPECT_FALSE(shell->preference_textfield("accept_languages")->view()->IsDrawn());for(size_t i=0;i<10;++i)EXPECT_FALSE(shell->preference_navigation_selected_for_testing(i));
  activate(languages);EXPECT_TRUE(search->GetText().empty());EXPECT_EQ(7u,shell->preference_category_for_testing());for(size_t i=0;i<10;++i)EXPECT_EQ(i==7,shell->preference_navigation_selected_for_testing(i));
  window_->widget()->SetBounds(gfx::Rect(40,40,700,660));shell->OpenGeneralSettings();shell->general_widget()->LayoutRootViewIfNecessary();EXPECT_FALSE(languages->IsDrawn());EXPECT_TRUE(shell->preference_choice_for_testing("category")->IsDrawn());EXPECT_EQ(7u,shell->preference_choice_selection_for_testing("category"));shell->ActivatePreferenceChoiceForTesting("category",4);EXPECT_EQ(4u,shell->preference_category_for_testing());EXPECT_TRUE(shell->preference_textfield("font_standard")->view()->IsDrawn());EXPECT_FALSE(shell->preference_textfield("accept_languages")->view()->IsDrawn());shell->ActivatePreferenceChoiceForTesting("category",99);EXPECT_EQ(4u,shell->preference_category_for_testing());
  window_->widget()->SetBounds(gfx::Rect(40,40,1000,860));shell->OpenGeneralSettings();shell->general_widget()->LayoutRootViewIfNecessary();ASSERT_TRUE(languages->IsDrawn());for(size_t i=0;i<10;++i)EXPECT_EQ(i==4,shell->preference_navigation_selected_for_testing(i));auto* close=shell->preference_close_for_testing();ASSERT_TRUE(close);if(rust)ASSERT_TRUE(base::test::RunUntil([&]{return shell->preference_close_metal_visible_for_testing();}));activate(close);ASSERT_TRUE(base::test::RunUntil([&]{return !shell->general_widget()->IsVisible();}));
  if(rust)ASSERT_TRUE(base::test::RunUntil([&]{return !shell->preference_navigation_metal_visible_for_testing()&&!shell->preference_close_metal_visible_for_testing();}));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, PreferenceActionsCocoaCommitAdmissionAndBusyLabels) {
  ASSERT_NO_FATAL_FAILURE(NavigateBoth());ASSERT_TRUE(window_->Select("work"));auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->OpenGeneralSettings();
  auto filter=[&](std::u16string query){auto* field=shell->preferences_search();field->SelectRange(gfx::Range(0,field->GetText().size()));field->input_client()->InsertText(query,ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);shell->general_widget()->LayoutRootViewIfNecessary();};
  filter(u"Czcionka standardowa");auto* field=shell->preference_textfield("font_standard");auto* save=shell->preference_save_button_for_testing("font_standard");ASSERT_TRUE(save);const bool rust=field->rust_editor();EXPECT_EQ(rust,!views::AsViewClass<views::MdTextButton>(save));
  field->SelectRange(gfx::Range(0,field->GetText().size()));field->input_client()->InsertText(u"Georgia",ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);save->ScrollRectToVisible(save->GetLocalBounds());if(rust)ASSERT_TRUE(base::test::RunUntil([&]{return shell->preference_action_metal_visible_for_testing(save)&&shell->preference_actions_contains_scene_text_for_testing("Zapisz");}));
  ui_controls::EnableUIControls();ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->general_widget()->GetNativeWindow()));save->RequestFocus();ASSERT_TRUE(save->HasFocus());ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(shell->general_widget()->GetNativeWindow(),ui::VKEY_SPACE,false,false,false,false));EXPECT_EQ("Georgia",window_->ReadPreferences().at("font_standard").text);
  ui::CompositionText marked;marked.text=u"未";marked.selection=gfx::Range(1);field->SetCompositionText(marked);ASSERT_TRUE(field->IsIMEComposing());ClickControl(save);EXPECT_EQ("Georgia",window_->ReadPreferences().at("font_standard").text);field->ClearCompositionText();
  field->SetText(u"Arial");field->view()->SetEnabled(false);save->SetEnabled(false);ui::KeyEvent press(ui::EventType::kKeyPressed,ui::VKEY_SPACE,0),release(ui::EventType::kKeyReleased,ui::VKEY_SPACE,0);save->OnKeyPressed(press);save->OnKeyReleased(release);EXPECT_EQ("Georgia",window_->ReadPreferences().at("font_standard").text);field->view()->SetEnabled(true);save->SetEnabled(true);
  filter(u"Wyczyść dane przeglądania");auto* clear=FindButton(shell->general_widget()->GetContentsView(),u"Wyczyść…");ASSERT_TRUE(clear);clear->ScrollRectToVisible(clear->GetLocalBounds());shell->SetBrowsingDataBusy(true);EXPECT_FALSE(clear->GetEnabled());EXPECT_EQ(clear,FindButton(shell->general_widget()->GetContentsView(),u"Czyszczenie…"));
  if(rust)ASSERT_TRUE(base::test::RunUntil([&]{return shell->preference_actions_contains_scene_text_for_testing("Czyszczenie…");}));
  shell->SetBrowsingDataBusy(false);EXPECT_TRUE(clear->GetEnabled());EXPECT_EQ(clear,FindButton(shell->general_widget()->GetContentsView(),u"Wyczyść…"));
  ASSERT_TRUE(window_->Select("personal"));shell->OpenGeneralSettings();EXPECT_NE("Georgia",window_->ReadPreferences().at("font_standard").text);ASSERT_TRUE(window_->Select("work"));EXPECT_EQ("Georgia",window_->ReadPreferences().at("font_standard").text);
}

// Compiled only: composite row/control identity and deferred backend targets await GUI acceptance.
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, SiteRuleSceneReusesCompositeRowsAcrossRefreshAndSettingChange) {
  ASSERT_TRUE(window_->Select("work"));auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));if(!shell->workspace_input()->rust_editor())return;const std::string origin="https://site-scene-reuse.test";ASSERT_TRUE(window_->WriteSitePreference(origin,"javascript",2));shell->OpenGeneralSettings();auto* query=shell->preferences_search();query->SelectRange(gfx::Range(0,query->GetText().size()));query->input_client()->InsertText(u"Zapisane wyjątki",ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);auto* list=shell->site_rule_results_for_testing();ASSERT_TRUE(list);ASSERT_EQ(1u,list->children().size());auto* row=list->children().front().get();auto find_remove=[](auto&& self,views::View* view)->views::View*{ui::AXNodeData ax;view->GetViewAccessibility().GetAccessibleNodeData(&ax);if(ax.role==ax::mojom::Role::kButton&&ax.GetString16Attribute(ax::mojom::StringAttribute::kName).starts_with(u"Usuń wyjątek:"))return view;for(const auto& child:view->children())if(auto* found=self(self,child.get()))return found;return nullptr;};auto* remove=find_remove(find_remove,row);ASSERT_TRUE(remove);const auto id=shell->sidebar_node_id_for_testing(row),root=shell->scene_parent_id_for_testing(row),button_id=shell->sidebar_node_id_for_testing(remove);EXPECT_NE(0u,root);EXPECT_EQ(7u,shell->control_model_kind_for_testing(row));remove->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);remove->ScrollViewToVisible();remove->RequestFocus();ASSERT_TRUE(remove->HasFocus());shell->RefreshSiteRulesForTesting(false);EXPECT_EQ(row,list->children().front().get());EXPECT_EQ(remove,find_remove(find_remove,row));EXPECT_TRUE(remove->HasFocus());ASSERT_TRUE(window_->WriteSitePreference(origin,"javascript",1));shell->RefreshSiteRulesForTesting();EXPECT_EQ(row,list->children().front().get());EXPECT_EQ(id,shell->sidebar_node_id_for_testing(row));EXPECT_EQ(button_id,shell->sidebar_node_id_for_testing(remove));ui::AXNodeData ax;remove->GetViewAccessibility().GetAccessibleNodeData(&ax);EXPECT_NE(std::u16string::npos,ax.GetString16Attribute(ax::mojom::StringAttribute::kName).find(u"Zezwalaj"));EXPECT_TRUE(remove->GetEnabled());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, SiteRuleSceneInsertionCancelsPressAndDeletionResolvesCurrentRule) {
  ASSERT_TRUE(window_->Select("work"));auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));if(!shell->workspace_input()->rust_editor())return;const std::string target="https://site-scene-b.test";ASSERT_TRUE(window_->WriteSitePreference(target,"javascript",2));ASSERT_TRUE(window_->WriteSitePreference("https://site-scene-c.test","javascript",2));shell->OpenGeneralSettings();auto* query=shell->preferences_search();query->SelectRange(gfx::Range(0,query->GetText().size()));query->input_client()->InsertText(u"Zapisane wyjątki",ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);auto* list=shell->site_rule_results_for_testing();auto find_remove=[&](auto&& self,views::View* view)->views::View*{ui::AXNodeData ax;view->GetViewAccessibility().GetAccessibleNodeData(&ax);const auto name=ax.GetString16Attribute(ax::mojom::StringAttribute::kName);if(ax.role==ax::mojom::Role::kButton&&name.starts_with(u"Usuń wyjątek:")&&name.find(u"site-scene-b.test")!=std::u16string::npos)return view;for(const auto& child:view->children())if(auto* found=self(self,child.get()))return found;return nullptr;};auto* remove=find_remove(find_remove,list);ASSERT_TRUE(remove);remove->ScrollViewToVisible();shell->general_widget()->LayoutRootViewIfNecessary();const auto point=remove->GetLocalBounds().CenterPoint();ui::MouseEvent press(ui::EventType::kMousePressed,point,point,base::TimeTicks(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);ASSERT_TRUE(remove->OnMousePressed(press));ASSERT_TRUE(window_->WriteSitePreference("https://site-scene-a.test","javascript",2));shell->RefreshSiteRulesForTesting();EXPECT_EQ(remove,find_remove(find_remove,list));ui::MouseEvent release(ui::EventType::kMouseReleased,point,point,base::TimeTicks(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);remove->OnMouseReleased(release);base::RunLoop().RunUntilIdle();EXPECT_EQ(3u,window_->ReadSitePreferences().size());ClickControl(remove);ASSERT_TRUE(base::test::RunUntil([&]{const auto rules=window_->ReadSitePreferences();return rules.size()==2&&std::none_of(rules.begin(),rules.end(),[&](const auto& rule){return rule.primary.find("site-scene-b.test")!=std::string::npos;});}));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, SiteRuleSceneObserverCannotDeleteThroughRetainedDetachedComposite) {
  ASSERT_TRUE(window_->Select("work"));auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));if(!shell->workspace_input()->rust_editor())return;ASSERT_TRUE(window_->WriteSitePreference("https://site-scene-observer.test","javascript",2));shell->OpenGeneralSettings();auto* query=shell->preferences_search();query->SelectRange(gfx::Range(0,query->GetText().size()));query->input_client()->InsertText(u"Zapisane wyjątki",ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);auto* list=shell->site_rule_results_for_testing();ASSERT_EQ(1u,list->children().size());auto* row=list->children().front().get();auto find_remove=[](auto&& self,views::View* view)->views::View*{ui::AXNodeData ax;view->GetViewAccessibility().GetAccessibleNodeData(&ax);if(ax.role==ax::mojom::Role::kButton&&ax.GetString16Attribute(ax::mojom::StringAttribute::kName).starts_with(u"Usuń wyjątek:"))return view;for(const auto& child:view->children())if(auto* found=self(self,child.get()))return found;return nullptr;};auto* remove=find_remove(find_remove,row);ASSERT_TRUE(remove);remove->ScrollViewToVisible();const auto id=shell->sidebar_node_id_for_testing(row);remove->SetProperty(views::kElementIdentifierKey,views::DialogClientView::kOkButtonElementId);std::unique_ptr<views::View> held;int notified=0;auto observer=ui::ElementTracker::GetElementTracker()->AddElementActivatedCallback(views::DialogClientView::kOkButtonElementId,views::ElementTrackerViews::GetContextForWidget(shell->general_widget()),base::BindRepeating([](views::View* row,std::unique_ptr<views::View>* held,int* notified,ui::TrackedElement*){++*notified;*held=row->parent()->RemoveChildViewT(row);},base::Unretained(row),&held,&notified));ClickControl(remove);EXPECT_EQ(1,notified);ASSERT_TRUE(held);base::RunLoop().RunUntilIdle();EXPECT_EQ(1u,window_->ReadSitePreferences().size());shell->RefreshSiteRulesForTesting(false);ASSERT_EQ(1u,list->children().size());EXPECT_EQ(id,shell->sidebar_node_id_for_testing(list->children().front().get()));EXPECT_EQ(0u,shell->sidebar_node_id_for_testing(held.get()));EXPECT_EQ(0u,shell->scene_parent_id_for_testing(held.get()));window_.reset();
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, PreferenceActionsSiteRulePaginationAndDeferredProfileBinding) {
  ASSERT_TRUE(window_->Select("work"));auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));
  for(int i=0;i<55;++i)ASSERT_TRUE(window_->WriteSitePreference("https://action-rule"+std::to_string(i)+".test","javascript",2));ASSERT_EQ(55u,window_->ReadSitePreferences().size());
  shell->OpenGeneralSettings();auto* query=shell->preferences_search();query->SelectRange(gfx::Range(0,query->GetText().size()));query->input_client()->InsertText(u"Zapisane wyjątki",ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);shell->general_widget()->LayoutRootViewIfNecessary();
  auto removes=[&]{std::vector<views::View*> found;auto visit=[&](auto&& self,views::View* view)->void{{ui::AXNodeData node;view->GetViewAccessibility().GetAccessibleNodeData(&node);if(node.role==ax::mojom::Role::kButton&&node.GetString16Attribute(ax::mojom::StringAttribute::kName).starts_with(u"Usuń wyjątek:"))found.push_back(view);}for(const auto& child:view->children())self(self,child.get());};visit(visit,shell->general_widget()->GetContentsView());return found;};
  ASSERT_EQ(50u,removes().size());auto* more=FindButton(shell->general_widget()->GetContentsView(),u"Pokaż kolejne wyjątki…");ASSERT_TRUE(more);const bool rust=shell->workspace_input()->rust_editor();EXPECT_EQ(rust,!views::AsViewClass<views::MdTextButton>(more));
  ui_controls::EnableUIControls();ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->general_widget()->GetNativeWindow()));base::test::TestFuture<void> presented;auto* compositor=shell->general_widget()->GetCompositor();ASSERT_TRUE(compositor);compositor->RequestSuccessfulPresentationTimeForNextFrame(base::BindOnce([](base::OnceClosure done,const viz::FrameTimingDetails&){std::move(done).Run();},presented.GetCallback()));compositor->ScheduleFullRedraw();ASSERT_TRUE(presented.Wait());more->ScrollRectToVisible(more->GetLocalBounds());
  if(rust)ASSERT_TRUE(base::test::RunUntil([&]{return shell->preference_action_metal_visible_for_testing(more);}));
  views::ViewTracker old_more(more);more->RequestFocus();ASSERT_TRUE(more->HasFocus());ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(shell->general_widget()->GetNativeWindow(),ui::VKEY_SPACE,false,false,false,false));ASSERT_TRUE(base::test::RunUntil([&]{return !old_more.view()&&removes().size()==55;}));EXPECT_FALSE(FindButton(shell->general_widget()->GetContentsView(),u"Pokaż kolejne wyjątki…"));
  // The deferred action must retain its opening's profile, even though the
  // same settings hierarchy is reused by a different active profile.
  auto* remove=removes().front();ClickControl(remove);ASSERT_TRUE(window_->Select("personal"));ASSERT_TRUE(window_->WriteSitePreference("https://personal-action.test","javascript",2));base::RunLoop().RunUntilIdle();EXPECT_EQ(1u,window_->ReadSitePreferences().size());
  ASSERT_TRUE(window_->Select("work"));shell->OpenGeneralSettings();ASSERT_EQ(55u,window_->ReadSitePreferences().size());ASSERT_FALSE(removes().empty());remove=removes().front();EXPECT_EQ(rust,!views::AsViewClass<views::MdTextButton>(remove));ClickControl(remove);ASSERT_TRUE(base::test::RunUntil([&]{return window_->ReadSitePreferences().size()==54;}));
  ASSERT_TRUE(window_->Select("personal"));EXPECT_EQ(1u,window_->ReadSitePreferences().size());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, PreferenceSwitchesCocoaScopeAndClearSelections) {
  ASSERT_NO_FATAL_FAILURE(NavigateBoth());ASSERT_TRUE(window_->Select("work"));
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->OpenGeneralSettings();
  const bool rust=shell->workspace_input()->rust_editor();
  auto filter=[&](std::u16string query){auto* field=shell->preferences_search();field->SelectRange(gfx::Range(0,field->GetText().size()));field->input_client()->InsertText(query,ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);shell->general_widget()->LayoutRootViewIfNecessary();};
  filter(u"Animacje przestrzeni");auto* animations=shell->preference_switch_for_testing("animations");ASSERT_TRUE(animations);
  const bool previous_animations=shell->workspace_animations();ClickControl(animations);EXPECT_EQ(!previous_animations,shell->workspace_animations());
  filter(u"Zawsze widoczny panel kart");auto* pinned=shell->preference_switch_for_testing("sidebar");ASSERT_TRUE(pinned);
  const bool previous_pinned=shell->pinned();ClickControl(pinned);EXPECT_EQ(!previous_pinned,shell->pinned());
  filter(u"Kamera");auto* camera=shell->preference_toggle("camera");
  EXPECT_EQ(rust,!views::AsViewClass<views::ToggleButton>(camera));
  ui::AXNodeData node;camera->GetViewAccessibility().GetAccessibleNodeData(&node);EXPECT_EQ(ax::mojom::Role::kSwitch,node.role);EXPECT_EQ(ax::mojom::CheckedState::kTrue,node.GetCheckedState());
  camera->ScrollRectToVisible(camera->GetLocalBounds());
  if(rust)ASSERT_TRUE(base::test::RunUntil([&]{return shell->preference_switch_metal_visible_for_testing("camera");}));
  ui_controls::EnableUIControls();ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->general_widget()->GetNativeWindow()));camera->RequestFocus();ASSERT_TRUE(camera->HasFocus());
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(shell->general_widget()->GetNativeWindow(),ui::VKEY_SPACE,false,false,false,false));
  EXPECT_EQ(0,window_->ReadPreferences().at("camera").value);EXPECT_FALSE(shell->preference_switch_checked_for_testing("camera"));
  camera->GetViewAccessibility().GetAccessibleNodeData(&node);EXPECT_EQ(ax::mojom::CheckedState::kFalse,node.GetCheckedState());
  EXPECT_EQ(CONTENT_SETTING_BLOCK,HostContentSettingsMapFactory::GetForProfile(work_)->GetDefaultContentSetting(ContentSettingsType::MEDIASTREAM_CAMERA));
  ASSERT_TRUE(window_->Select("personal"));shell->OpenGeneralSettings();EXPECT_TRUE(shell->preference_switch_checked_for_testing("camera"));EXPECT_EQ(1,window_->ReadPreferences().at("camera").value);
  EXPECT_EQ(!previous_animations,shell->workspace_animations());EXPECT_EQ(!previous_pinned,shell->pinned());
  ASSERT_TRUE(window_->Select("work"));shell->OpenGeneralSettings();EXPECT_FALSE(shell->preference_switch_checked_for_testing("camera"));
  filter(u"Wyczyść dane przeglądania");auto* cookies=shell->clear_checkbox_for_testing(1);auto* cache=shell->clear_checkbox_for_testing(2);ASSERT_TRUE(cookies);ASSERT_TRUE(cache);
  EXPECT_EQ(rust,!views::AsViewClass<views::Checkbox>(cookies));
  cookies->ScrollRectToVisible(cookies->GetLocalBounds());
  if(rust)ASSERT_TRUE(base::test::RunUntil([&]{return shell->clear_choices_metal_visible_for_testing();}));
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->general_widget()->GetNativeWindow()));cookies->RequestFocus();ASSERT_TRUE(cookies->HasFocus());
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(shell->general_widget()->GetNativeWindow(),ui::VKEY_SPACE,false,false,false,false));
  ClickControl(cache);
  for(size_t i=0;i<3;++i){auto* choice=shell->clear_checkbox_for_testing(i);choice->GetViewAccessibility().GetAccessibleNodeData(&node);EXPECT_EQ(ax::mojom::Role::kCheckBox,node.role);EXPECT_EQ(i==1?ax::mojom::CheckedState::kTrue:ax::mojom::CheckedState::kFalse,node.GetCheckedState());}
  shell->ActivatePreferenceChoiceForTesting("clear_period",3);
  auto* clear=FindButton(shell->general_widget()->GetContentsView(),u"Wyczyść…");ASSERT_TRUE(clear);
  // The form's selection must drive the real controller and durable profile data.
  EXPECT_EQ("work",content::EvalJs(window_->contents("work"),"writeMarker('work')"));
  EXPECT_EQ("personal",content::EvalJs(window_->contents("personal"),"writeMarker('personal')"));
  ClickControl(clear);ASSERT_TRUE(window_->clear_data_dialog());
  ASSERT_TRUE(window_->Select("personal"));window_->clear_data_dialog()->widget_delegate()->AsDialogDelegate()->AcceptDialog();
  ASSERT_TRUE(base::test::RunUntil([&]{return !window_->clearing_browsing_data();}));
  EXPECT_EQ("personal|personal|personal|personal",content::EvalJs(window_->contents("personal"),"readMarker()"));
  ASSERT_TRUE(content::NavigateToURL(window_->contents("work"),embedded_test_server()->GetURL("/storage_probe.html?ui-clear")));
  EXPECT_EQ("",content::EvalJs(window_->contents("work"),"document.cookie"));
  EXPECT_EQ("",content::EvalJs(window_->contents("work"),"localStorage.getItem('zitter') || ''"));
  EXPECT_EQ("",content::EvalJs(window_->contents("work"),"readMarker().then(value => value.split('|')[2])"));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, PreferenceChoicesCocoaEmptySelectionAndProfileScope) {
  ASSERT_NO_FATAL_FAILURE(NavigateBoth("/preferences_probe.html"));ASSERT_TRUE(window_->Select("work"));
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));
  // A valid Chromium value outside the predefined choices must remain blank.
  work_->GetPrefs()->SetInteger(prefs::kWebKitDefaultFontSize,17);shell->OpenGeneralSettings();
  auto filter=[&](std::u16string query){auto* field=shell->preferences_search();field->SelectRange(gfx::Range(0,field->GetText().size()));field->input_client()->InsertText(query,ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);shell->general_widget()->LayoutRootViewIfNecessary();};
  filter(u"Rozmiar tekstu");auto* picker=shell->preference_picker("font_size");ASSERT_TRUE(picker->IsDrawn());
  EXPECT_FALSE(shell->preference_choice_selection_for_testing("font_size"));
  const bool rust=shell->workspace_input()->rust_editor();EXPECT_EQ(rust,!views::AsViewClass<views::Combobox>(picker));
  ui::AXNodeData empty;picker->GetViewAccessibility().GetAccessibleNodeData(&empty);EXPECT_TRUE(empty.GetString16Attribute(ax::mojom::StringAttribute::kValue).empty());
  picker->ScrollRectToVisible(picker->GetLocalBounds());
  if(rust)ASSERT_TRUE(base::test::RunUntil([&]{return shell->preference_choice_metal_visible_for_testing("font_size");}));
  ui_controls::EnableUIControls();ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->general_widget()->GetNativeWindow()));picker->RequestFocus();ASSERT_TRUE(picker->HasFocus());
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(shell->general_widget()->GetNativeWindow(),ui::VKEY_2,false,false,false,false));
  ASSERT_TRUE(base::test::RunUntil([&]{return work_->GetPrefs()->GetInteger(prefs::kWebKitDefaultFontSize)==20;}));EXPECT_EQ(4u,shell->preference_choice_selection_for_testing("font_size"));
  picker->SetEnabled(false);shell->ActivatePreferenceChoiceForTesting("font_size",0);EXPECT_EQ(20,work_->GetPrefs()->GetInteger(prefs::kWebKitDefaultFontSize));picker->SetEnabled(true);
  const auto personal_size=personal_->GetPrefs()->GetInteger(prefs::kWebKitDefaultFontSize);
  ASSERT_TRUE(window_->Select("personal"));shell->OpenGeneralSettings();EXPECT_EQ(personal_size,personal_->GetPrefs()->GetInteger(prefs::kWebKitDefaultFontSize));
  ASSERT_TRUE(window_->Select("work"));shell->OpenGeneralSettings();EXPECT_EQ(4u,shell->preference_choice_selection_for_testing("font_size"));
  EXPECT_EQ(20,work_->GetPrefs()->GetInteger(prefs::kWebKitDefaultFontSize));
  filter(u"czcion");auto* bottom=shell->preference_textfield("font_fixed")->view();
  auto* scroller=shell->preference_viewport_for_testing();
  ASSERT_TRUE(scroller);
  // Native compositor scrolling cannot target a newly resized layer until its
  // new extent has committed. Wait for presentation, not an arbitrary delay.
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->general_widget()->GetNativeWindow()));
  base::test::TestFuture<void> presented;auto* compositor=shell->general_widget()->GetCompositor();ASSERT_TRUE(compositor);
  compositor->RequestSuccessfulPresentationTimeForNextFrame(base::BindOnce([](base::OnceClosure done,const viz::FrameTimingDetails&){std::move(done).Run();},presented.GetCallback()));
  compositor->ScheduleFullRedraw();ASSERT_TRUE(presented.Wait());
  scroller->ScrollTo(scroller->maximum());
  ASSERT_TRUE(base::test::RunUntil([&]{return bottom->GetVisibleBounds().height()==bottom->height();}))<<"query="<<base::UTF16ToUTF8(shell->preferences_search()->GetText())<<" bottom="<<bottom->GetBoundsInScreen().ToString()<<" visible="<<bottom->GetVisibleBounds().ToString()<<" viewport="<<scroller->visible_rect().ToString()<<" contents="<<scroller->contents()->size().ToString()<<" scroll="<<scroller->offset()<<"/"<<scroller->maximum();
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_saved();}));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, PreferenceChoicesSitePolicyAndClearDataAdmission) {
  ASSERT_NO_FATAL_FAILURE(NavigateBoth());ASSERT_TRUE(window_->Select("work"));auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->OpenGeneralSettings();
  auto filter=[&](std::u16string query){auto* field=shell->preferences_search();field->SelectRange(gfx::Range(0,field->GetText().size()));field->input_client()->InsertText(query,ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);shell->general_widget()->LayoutRootViewIfNecessary();};
  filter(u"Wyjątek dla witryny");shell->ActivatePreferenceChoiceForTesting("site_type",4);EXPECT_EQ(4u,shell->preference_choice_count_for_testing("site_policy"));
  shell->ActivatePreferenceChoiceForTesting("site_policy",3);EXPECT_EQ(3u,shell->preference_choice_selection_for_testing("site_policy"));
  shell->ActivatePreferenceChoiceForTesting("site_type",0);EXPECT_EQ(3u,shell->preference_choice_count_for_testing("site_policy"));EXPECT_EQ(0u,shell->preference_choice_selection_for_testing("site_policy"));
  shell->ActivatePreferenceChoiceForTesting("site_policy",3);EXPECT_EQ(0u,shell->preference_choice_selection_for_testing("site_policy"));
  const auto origin=embedded_test_server()->GetURL("/").DeprecatedGetOriginAsURL().spec();shell->site_origin_input()->SetText(base::UTF8ToUTF16(origin));
  shell->ActivatePreferenceChoiceForTesting("site_policy",2);auto* save=FindButton(shell->general_widget()->GetContentsView(),u"Zapisz wyjątek");ASSERT_TRUE(save);ClickControl(save);
  EXPECT_EQ(CONTENT_SETTING_BLOCK,HostContentSettingsMapFactory::GetForProfile(work_)->GetContentSetting(GURL(origin),GURL(origin),ContentSettingsType::JAVASCRIPT));
  EXPECT_NE(CONTENT_SETTING_BLOCK,HostContentSettingsMapFactory::GetForProfile(personal_)->GetContentSetting(GURL(origin),GURL(origin),ContentSettingsType::JAVASCRIPT));
  filter(u"Wyczyść dane przeglądania");auto* period=shell->preference_choice_for_testing("clear_period");ASSERT_TRUE(period);period->ScrollRectToVisible(period->GetLocalBounds());
  shell->ActivatePreferenceChoiceForTesting("clear_period",3);EXPECT_EQ(3u,shell->preference_choice_selection_for_testing("clear_period"));
  auto* clear=FindButton(shell->general_widget()->GetContentsView(),u"Wyczyść…");ASSERT_TRUE(clear);ClickControl(clear);ASSERT_TRUE(window_->clear_data_dialog());
  window_->clear_data_dialog()->widget_delegate()->AsDialogDelegate()->CancelDialog();ASSERT_TRUE(base::test::RunUntil([&]{return !window_->clear_data_dialog();}));EXPECT_FALSE(window_->clearing_browsing_data());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, GeneralSettingsThemeAndPersistence) {
  auto* shell = window_->shell();
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->appearance_ready(); }));
  const auto url = embedded_test_server()->GetURL("/links_probe.html");
  ASSERT_TRUE(content::NavigateToURL(window_->contents("work"), url));
  ui_controls::EnableUIControls();
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(window_->widget()->GetNativeWindow()));
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(window_->widget()->GetNativeWindow(),
      ui::VKEY_OEM_COMMA, false, false, false, true));
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->general_widget()->IsVisible(); }));
  shell->SetThemeMode(ThemeMode::kDark);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(window_->contents("work"), "matchMedia('(prefers-color-scheme: dark)').matches").ExtractBool();
  }));
  EXPECT_EQ(ui::ColorProviderKey::ColorMode::kDark, window_->widget()->GetColorMode());
  EXPECT_EQ(ui::ColorProviderKey::ColorMode::kDark, shell->general_widget()->GetColorMode());
  EXPECT_EQ(ui::ColorProviderKey::ColorMode::kDark, shell->sidebar_widget()->GetColorMode());
  shell->SetThemeMode(ThemeMode::kLight);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !content::EvalJs(window_->contents("work"), "matchMedia('(prefers-color-scheme: dark)').matches").ExtractBool();
  }));
  // System mode follows changes without recreating pages or changing OS prefs.
  auto* native = ui::NativeTheme::GetInstanceForNativeUi();
  const auto original_scheme = native->preferred_color_scheme();
  base::ScopedClosureRunner restore_theme(base::BindOnce([](ui::NativeTheme* native,
      ui::NativeTheme::PreferredColorScheme scheme) {
    native->set_preferred_color_scheme(scheme); native->NotifyOnNativeThemeUpdated();
  }, base::Unretained(native), original_scheme));
  shell->SetThemeMode(ThemeMode::kSystem);
  native->set_preferred_color_scheme(ui::NativeTheme::PreferredColorScheme::kDark);
  native->NotifyOnNativeThemeUpdated();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(window_->contents("work"), "matchMedia('(prefers-color-scheme: dark)').matches").ExtractBool();
  }));
  native->set_preferred_color_scheme(ui::NativeTheme::PreferredColorScheme::kLight);
  native->NotifyOnNativeThemeUpdated();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !content::EvalJs(window_->contents("work"), "matchMedia('(prefers-color-scheme: dark)').matches").ExtractBool();
  }));
  shell->SetThemeMode(ThemeMode::kDark);
  shell->SetSearchEngine(SearchEngine::kGoogle);
  shell->SetWorkspaceAnimations(false);
  shell->SetPinned(false);
  shell->SetWorkspaceColor("work", 0xFF123456);
  // Typing more than one grapheme keeps only the first emoji.
  shell->OpenWorkspaceSettings();shell->emoji_input()->RequestFocus();
  shell->emoji_input()->SelectRange(gfx::Range(0,shell->emoji_input()->GetText().size()));
  shell->emoji_input()->input_client()->InsertText(u"🚀abc",ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);
  EXPECT_EQ(u"🚀", shell->workspace_emoji("work"));
  EXPECT_EQ(u"🚀", shell->emoji_input()->GetText());
  shell->SetWorkspaceEmoji("personal", u"🏠");
  shell->SetWorkspaceEmoji("personal", u"");
  EXPECT_EQ(u"", shell->workspace_emoji("personal"));
  // Exercise the actual typed-search action; serve a synthetic response locally.
  content::URLLoaderInterceptor search_interceptor(base::BindRepeating(
      [](content::URLLoaderInterceptor::RequestParams* params) {
        if (params->url_request.url.host() != "www.google.com") return false;
        content::URLLoaderInterceptor::WriteResponse("HTTP/1.1 200 OK\nContent-Type: text/html\n\n",
            "<!doctype html><title>Search probe</title>", params->client.get());
        return true;
      }));
  shell->CloseOverlays(); shell->OpenAddress(true); SetPaletteQuery(u"zitter theme test");
  ui::KeyEvent enter(ui::EventType::kKeyPressed, ui::VKEY_RETURN, ui::EF_NONE);
  ASSERT_TRUE(shell->address_input()->view()->OnKeyPressed(enter));
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->contents("work")->GetLastCommittedURL().host() == "www.google.com"; }));
  EXPECT_EQ("q=zitter+theme+test", window_->contents("work")->GetLastCommittedURL().query());
  ASSERT_TRUE(window_->OpenPeek(url));
  ASSERT_TRUE(content::WaitForLoadStop(window_->peek_contents()));
  EXPECT_EQ(ui::ColorProviderKey::ColorMode::kDark, shell->peek_widget()->GetColorMode());
  EXPECT_EQ(true, content::EvalJs(window_->peek_contents(), "matchMedia('(prefers-color-scheme: dark)').matches"));
  window_->ClosePeek();
  window_->ShowPasswords();
  auto* services = PageServices::FromWebContents(window_->contents("work"));
  ASSERT_TRUE(base::test::RunUntil([&] { return services->password_prompt() && services->password_prompt()->IsVisible(); }));
  EXPECT_EQ(ui::ColorProviderKey::ColorMode::kDark, services->password_prompt()->GetColorMode());
  services->password_prompt()->widget_delegate()->AsDialogDelegate()->AcceptDialog();
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->appearance_saved() && window_->configuration_saved(); }));
  window_.reset(); window_ = std::make_unique<WorkspaceWindow>(work_, personal_);
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_ready() && window_->shell()->appearance_ready(); }));
  shell = window_->shell();
  EXPECT_EQ(ThemeMode::kDark, shell->theme_mode());
  EXPECT_EQ(SearchEngine::kGoogle, shell->search_engine());
  EXPECT_FALSE(shell->workspace_animations());
  EXPECT_FALSE(shell->pinned());
  EXPECT_EQ(0xFF123456u, shell->workspace_color("work"));
  EXPECT_EQ(u"🚀", shell->workspace_emoji("work"));
  EXPECT_EQ(u"", shell->workspace_emoji("personal"));
  EXPECT_EQ(ui::ColorProviderKey::ColorMode::kDark, window_->widget()->GetColorMode());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, SidebarRemainsOpenWhileSpacesSlide) {
  ASSERT_NO_FATAL_FAILURE(NavigateBoth());
  auto* shell = window_->shell();
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->appearance_ready(); }));
  gfx::ScopedAnimationDurationScaleMode duration(gfx::ScopedAnimationDurationScaleMode::NORMAL_DURATION);
  const bool original_motion = gfx::Animation::PrefersReducedMotion();
  base::ScopedClosureRunner restore_motion(base::BindOnce(&gfx::Animation::SetPrefersReducedMotionForTesting, original_motion));
  gfx::Animation::SetPrefersReducedMotionForTesting(false);
  shell->SetPinned(false); shell->ShowSidebar();
  ASSERT_TRUE(shell->sidebar_visible());
  const size_t pin_builds = shell->pinned_list_build_count();
  ASSERT_TRUE(window_->Select("work"));
  EXPECT_TRUE(shell->sidebar_visible());
  EXPECT_FALSE(shell->pinned());
  EXPECT_TRUE(shell->workspace_animation_running());
  EXPECT_TRUE(shell->workspace_animation_pending());
  if(base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII("zitter-sidebar")=="rust-metal") {
    EXPECT_TRUE(shell->workspace_switch_metal_visible());
    EXPECT_FALSE(shell->tab_scroll_view()->layer());
  } else {
    ASSERT_TRUE(shell->tab_scroll_view()->layer());
    EXPECT_LE(shell->tab_scroll_view()->layer()->bounds().height(),window_->widget()->GetWindowBoundsInScreen().height());
  }
  for (const auto& list : shell->tab_list_view()->children()) EXPECT_FALSE(list->layer());
  ASSERT_TRUE(base::test::RunUntil([&] { return !shell->workspace_animation_pending(); }));
  // Metal presentation and completion can both arrive in the same task turn.
  if(base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII("zitter-sidebar")=="rust-metal")
    EXPECT_TRUE(shell->workspace_animation_running()||!shell->workspace_switch_metal_visible());
  else EXPECT_TRUE(shell->workspace_animation_running());
  ASSERT_TRUE(base::test::RunUntil([&] { return !shell->workspace_animation_running(); }));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window_->contents("work")->GetContainerBounds().x() == shell->content_host()->GetBoundsInScreen().x();
  }));
  for (int i = 0; i < 8; ++i) {
    ASSERT_TRUE(window_->Select(i % 2 ? "work" : "personal"));
    EXPECT_TRUE(shell->sidebar_visible());
  }
  ASSERT_TRUE(base::test::RunUntil([&] { return !shell->workspace_animation_running(); }));
  EXPECT_EQ("work", window_->active_id());
  EXPECT_EQ(pin_builds, shell->pinned_list_build_count());
  shell->SetWorkspaceAnimations(false);
  ASSERT_TRUE(window_->Select("personal"));
  EXPECT_FALSE(shell->workspace_animation_running());
  EXPECT_TRUE(shell->sidebar_visible());
  shell->SetWorkspaceAnimations(true);
  gfx::Animation::SetPrefersReducedMotionForTesting(true);
  ASSERT_TRUE(window_->Select("work"));
  EXPECT_FALSE(shell->workspace_animation_running());
  EXPECT_TRUE(shell->sidebar_visible());
}

// Compile only: Peek rendering, focus and command routing await the bulk UI checkpoint.
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, PeekToolbarNodesTitleMetricsAndNativeLayoutOracle) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));const auto url=embedded_test_server()->GetURL("/storage_probe.html?peek-toolbar-layout");ASSERT_TRUE(window_->OpenPeek(url));ASSERT_TRUE(content::WaitForLoadStop(window_->peek_contents()));shell->peek_widget()->LayoutRootViewIfNecessary();const bool rust=shell->workspace_input()->rust_editor();auto* toolbar=shell->peek_toolbar_for_testing();auto* title=shell->peek_title_for_testing();ASSERT_TRUE(toolbar);ASSERT_TRUE(title);ASSERT_EQ(6u,toolbar->children().size());EXPECT_EQ(!rust,views::AsViewClass<views::Label>(title)!=nullptr);
  const std::array<std::u16string,5> names={u"Wstecz",u"Dalej",u"Otwórz jako kartę",u"Split View",u"Zamknij podgląd (Escape)"};std::vector<uint64_t> ids;
  for(size_t i=0;i<5;++i){auto* view=shell->peek_action_for_testing(i);ASSERT_TRUE(view);EXPECT_EQ(!rust,views::AsViewClass<views::Button>(view)!=nullptr);ids.push_back(shell->sidebar_node_id_for_testing(view));if(rust)EXPECT_NE(0u,ids.back());else EXPECT_EQ(0u,ids.back());ui::AXNodeData ax;view->GetViewAccessibility().GetAccessibleNodeData(&ax);EXPECT_EQ(ax::mojom::Role::kButton,ax.role);EXPECT_EQ(names[i],ax.GetString16Attribute(ax::mojom::StringAttribute::kName));}
  if(rust)for(size_t i=0;i<ids.size();++i)for(size_t j=0;j<i;++j)EXPECT_NE(ids[i],ids[j]);
  const std::u16string raw=u"日本語 🦀 אבג · Very long Peek title with a distinct ending";shell->SetPeekTitle(raw);ui::AXNodeData ax;title->GetViewAccessibility().GetAccessibleNodeData(&ax);EXPECT_EQ(ax::mojom::Role::kStaticText,ax.role);EXPECT_EQ(u"Peek · "+raw,ax.GetString16Attribute(ax::mojom::StringAttribute::kName));
  auto oracle=std::make_unique<views::View>();auto* layout=oracle->SetLayoutManager(std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,gfx::Insets(),6));layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  for(int i=0;i<2;++i)oracle->AddChildView(std::make_unique<views::View>())->SetPreferredSize(gfx::Size(32,32));auto* native=oracle->AddChildView(std::make_unique<views::Label>(u"Peek · "+raw));native->SetFontList(gfx::FontList().DeriveWithSizeDelta(-1).DeriveWithWeight(gfx::Font::Weight::SEMIBOLD));native->SetHorizontalAlignment(gfx::ALIGN_LEFT);native->SetElideBehavior(gfx::ELIDE_MIDDLE);layout->SetFlexForView(native,1);
  for(size_t i:{2u,3u})oracle->AddChildView(std::make_unique<views::MdTextButton>(views::Button::PressedCallback(),names[i]));oracle->AddChildView(std::make_unique<views::View>())->SetPreferredSize(gfx::Size(32,32));
  EXPECT_EQ(native->GetPreferredSize(),title->GetPreferredSize());const auto old_bounds=toolbar->bounds();for(int width:{0,150,320,700,1100}){SCOPED_TRACE(width);toolbar->SetBounds(0,0,width,48);oracle->SetBounds(0,0,width,48);toolbar->DeprecatedLayoutImmediately();oracle->DeprecatedLayoutImmediately();EXPECT_EQ(native->GetMinimumSize(),title->GetMinimumSize());for(size_t i=0;i<6;++i)EXPECT_EQ(oracle->children()[i]->bounds(),toolbar->children()[i]->bounds());const auto point=title->GetLocalBounds().CenterPoint();EXPECT_EQ(native->GetTooltipHandlerForPoint(point)!=nullptr,title->GetTooltipHandlerForPoint(point)!=nullptr);}
  toolbar->SetBoundsRect(old_bounds);shell->peek_widget()->LayoutRootViewIfNecessary();for(size_t i=0;i<5;++i)EXPECT_EQ(ids[i],shell->sidebar_node_id_for_testing(shell->peek_action_for_testing(i)));
  if(rust){ASSERT_TRUE(base::test::RunUntil([&]{return shell->peek_toolbar_metal_visible_for_testing();}));EXPECT_TRUE(shell->peek_toolbar_contains_text_for_testing("Peek · "+base::UTF16ToUTF8(raw)));EXPECT_FALSE(shell->peek_toolbar_scene_bounds_for_testing().Intersects(shell->peek_host()->GetBoundsInScreen()));}
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, PeekToolbarActionsNavigatePromoteSplitAndCancelPendingClose) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));const bool rust=shell->workspace_input()->rust_editor();const auto a=embedded_test_server()->GetURL("/storage_probe.html?peek-action-a"),b=embedded_test_server()->GetURL("/storage_probe.html?peek-action-b");const auto tabs=window_->tab_count("work");ASSERT_TRUE(window_->OpenPeek(a));auto* peek=window_->peek_contents();ASSERT_TRUE(content::WaitForLoadStop(peek));ASSERT_TRUE(content::NavigateToURL(peek,b));shell->peek_widget()->LayoutRootViewIfNecessary();ui::AXActionData activate;activate.action=ax::mojom::Action::kDoDefault;
  shell->peek_action_for_testing(0)->HandleAccessibleAction(activate);ASSERT_TRUE(base::test::RunUntil([&]{return peek->GetLastCommittedURL()==a;}));ASSERT_TRUE(content::WaitForLoadStop(peek));shell->peek_action_for_testing(1)->HandleAccessibleAction(activate);ASSERT_TRUE(base::test::RunUntil([&]{return peek->GetLastCommittedURL()==b;}));ASSERT_TRUE(content::WaitForLoadStop(peek));
  auto* close=shell->peek_action_for_testing(4);close->SetEnabled(false);close->HandleAccessibleAction(activate);base::RunLoop().RunUntilIdle();EXPECT_EQ(peek,window_->peek_contents());close->SetEnabled(true);shell->peek_action_for_testing(2)->HandleAccessibleAction(activate);ASSERT_TRUE(base::test::RunUntil([&]{return !window_->peek_contents()&&window_->tab_count("work")==tabs+1;}));EXPECT_EQ(peek,window_->contents("work"));
  ASSERT_TRUE(window_->OpenPeek(a));auto* split=window_->peek_contents();ASSERT_TRUE(content::WaitForLoadStop(split));shell->peek_widget()->LayoutRootViewIfNecessary();shell->peek_action_for_testing(3)->HandleAccessibleAction(activate);ASSERT_TRUE(base::test::RunUntil([&]{return !window_->peek_contents()&&window_->split();}));EXPECT_EQ(split,window_->pane_contents(true));
  ASSERT_TRUE(window_->OpenPeek(b));ASSERT_TRUE(content::WaitForLoadStop(window_->peek_contents()));shell->peek_widget()->LayoutRootViewIfNecessary();close=shell->peek_action_for_testing(4);if(rust){const auto point=close->GetLocalBounds().CenterPoint();ui::MouseEvent press(ui::EventType::kMousePressed,point,point,base::TimeTicks::Now(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);ASSERT_TRUE(close->OnMousePressed(press));shell->HidePeek();shell->peek_widget()->Show();shell->peek_widget()->LayoutRootViewIfNecessary();ui::MouseEvent release(ui::EventType::kMouseReleased,point,point,base::TimeTicks::Now(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);close->OnMouseReleased(release);base::RunLoop().RunUntilIdle();EXPECT_TRUE(window_->peek_contents());}
  close->HandleAccessibleAction(activate);ASSERT_TRUE(base::test::RunUntil([&]{return !window_->peek_contents();}));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, PeekLifecycleAndPromotion) {
  const auto source_url = embedded_test_server()->GetURL("/links_probe.html");
  const auto target = embedded_test_server()->GetURL("/storage_probe.html?peek");
  ASSERT_TRUE(content::NavigateToURL(window_->contents("work"), source_url));
  auto* source = window_->contents("work");
  ASSERT_TRUE(content::ExecJs(source, "document.getElementById('marker').value = 'source kept'"));
  content::SimulateEndOfPaintHoldingOnPrimaryMainFrame(source);
#if BUILDFLAG(IS_MAC)
  constexpr int modifier = blink::WebInputEvent::kMetaKey;
#else
  constexpr int modifier = blink::WebInputEvent::kControlKey;
#endif
  content::SimulateMouseClickAt(source, modifier, blink::WebMouseEvent::Button::kLeft,
      gfx::ToRoundedPoint(content::GetCenterCoordinatesOfElementWithId(source, "peek")));
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->peek_contents() != nullptr; }));
  auto* peek = window_->peek_contents();
  ASSERT_TRUE(content::WaitForLoadStop(peek));
  EXPECT_EQ(target, peek->GetLastCommittedURL());
  EXPECT_EQ(work_.get(), peek->GetBrowserContext());
  EXPECT_EQ(1u, window_->tab_count("work"));
  EXPECT_EQ(source_url, source->GetLastCommittedURL());
  EXPECT_EQ("source kept", content::EvalJs(source, "document.getElementById('marker').value"));
  ASSERT_TRUE(content::ExecJs(peek, "document.getElementById('marker').value = 'peek kept'"));
  ASSERT_TRUE(window_->PromotePeek(false));
  EXPECT_EQ(nullptr, window_->peek_contents());
  EXPECT_FALSE(window_->shell()->peek_widget()->IsVisible());
  EXPECT_EQ(peek, window_->contents("work"));
  EXPECT_EQ(2u, window_->tab_count("work"));
  EXPECT_EQ("peek kept", content::EvalJs(peek, "document.getElementById('marker').value"));
  ASSERT_TRUE(window_->OpenPeek(target));
  auto* second = window_->peek_contents();
  ASSERT_TRUE(content::WaitForLoadStop(second));
  ASSERT_TRUE(window_->PromotePeek(true));
  EXPECT_TRUE(window_->split());
  EXPECT_EQ(peek, window_->pane_contents(false));
  EXPECT_EQ(second, window_->pane_contents(true));
  ASSERT_TRUE(window_->OpenPeek(target));
  ASSERT_TRUE(content::WaitForLoadStop(window_->peek_contents()));
  ui_controls::EnableUIControls();
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(window_->shell()->peek_widget()->GetNativeWindow()));
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(window_->shell()->peek_widget()->GetNativeWindow(),
      ui::VKEY_ESCAPE, false, false, false, false));
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->peek_contents(); }));
  EXPECT_EQ(3u, window_->tab_count("work"));
  ASSERT_TRUE(window_->OpenPeek(target));
  ASSERT_TRUE(window_->Select("personal"));
  EXPECT_EQ(nullptr, window_->peek_contents());
  EXPECT_EQ(1u, window_->tab_count("personal"));
  // A real target=_blank link is hosted as a Zitter tab.
  ASSERT_TRUE(content::NavigateToURL(window_->contents("personal"), source_url));
  ASSERT_TRUE(content::ExecJs(window_->contents("personal"), "document.getElementById('blank').click()"));
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->tab_count("personal") == 2; }));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window_->contents("personal")->GetLastCommittedURL() == embedded_test_server()->GetURL("/storage_probe.html?blank");
  }));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, DomainRoutingAndPersistence) {
  const auto source_url = embedded_test_server()->GetURL("/links_probe.html");
  const auto target = embedded_test_server()->GetURL("client.test", "/storage_probe.html?routed");
  ASSERT_TRUE(content::NavigateToURL(window_->contents("work"), target));
  ASSERT_EQ("work", content::EvalJs(window_->contents("work"), "writeMarker('work')"));
  ASSERT_TRUE(window_->Select("personal"));
  ASSERT_TRUE(content::NavigateToURL(window_->contents("personal"), target));
  ASSERT_EQ("personal", content::EvalJs(window_->contents("personal"), "writeMarker('personal')"));
  ASSERT_TRUE(window_->Select("work"));
  ASSERT_TRUE(content::NavigateToURL(window_->contents("work"), source_url));
  auto* source = window_->contents("work");
  { base::AutoLock guard(routing_lock_); routing_cookies_.clear(); }
  auto* shell = window_->shell();
  shell->BuildLinkMenu(target, "work");
  auto* rules = shell->link_menu()->GetSubmenuModelAt(5);
  ASSERT_TRUE(rules); rules->ActivatedAt(1, 0);
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->WorkspaceForUrl(target) == "personal"; }));
  EXPECT_FALSE(window_->SetLinkRule(GURL("file:///tmp/x"), "personal"));
  EXPECT_FALSE(window_->SetLinkRule(target, "missing"));
  EXPECT_TRUE(window_->WorkspaceForUrl(GURL("https://sub.client.test/")).empty());
  shell->BuildLinkMenu(target, "work");
  EXPECT_TRUE(shell->link_menu()->GetSubmenuModelAt(5)->IsItemCheckedAt(1));
  shell->link_menu()->ActivatedAt(6, 0);
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->WorkspaceForUrl(target).empty(); }));
  ASSERT_TRUE(window_->SetLinkRule(target, "personal"));
  ASSERT_TRUE(content::ExecJs(source, content::JsReplace("document.getElementById('route').href = $1; document.getElementById('route').click()", target)));
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->active_id() == "personal" && window_->contents("personal")->GetLastCommittedURL() == target; }));
  ASSERT_TRUE(content::WaitForLoadStop(window_->contents("personal")));
  EXPECT_EQ("personal|personal|personal|", content::EvalJs(window_->contents("personal"), "readMarker()"));
  EXPECT_EQ(source_url, source->GetLastCommittedURL());
  {
    base::AutoLock guard(routing_lock_);
    ASSERT_EQ(1u, routing_cookies_.size());
    EXPECT_NE(std::string::npos, routing_cookies_[0].find("zitter=personal"));
    EXPECT_EQ(std::string::npos, routing_cookies_[0].find("zitter=work"));
  }
  ASSERT_TRUE(window_->Select("work"));
  ASSERT_TRUE(content::ExecJs(source, content::JsReplace("document.getElementById('blank').href = $1; document.getElementById('blank').click()", target)));
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->active_id() == "personal" && window_->tab_count("work") == 1; }));
  ASSERT_TRUE(window_->Select("work"));
  ASSERT_TRUE(window_->OpenPeek(target));
  EXPECT_EQ("work", window_->active_id());
  ASSERT_TRUE(content::WaitForLoadStop(window_->peek_contents()));
  EXPECT_EQ(personal_.get(), window_->peek_contents()->GetBrowserContext());
  EXPECT_EQ("personal|personal|personal|", content::EvalJs(window_->peek_contents(), "readMarker()"));
  ASSERT_TRUE(window_->PromotePeek(false));
  EXPECT_EQ("personal", window_->active_id());
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_saved(); }));
  window_.reset(); window_ = std::make_unique<WorkspaceWindow>(work_, personal_);
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_ready(); }));
  EXPECT_EQ("personal", window_->WorkspaceForUrl(target));
  ASSERT_TRUE(window_->Select("work"));
  ASSERT_TRUE(window_->OpenExternalUrl(target));
  EXPECT_EQ("personal", window_->active_id());
  EXPECT_EQ(personal_.get(), window_->contents("personal")->GetBrowserContext());
  ASSERT_TRUE(window_->AssignProfile("personal", "work-profile"));
  ASSERT_TRUE(window_->Select("work"));
  ASSERT_TRUE(window_->OpenExternalUrl(target));
  EXPECT_EQ("personal", window_->active_id());
  EXPECT_EQ(work_.get(), window_->contents("personal")->GetBrowserContext());
  // Removing a target also removes its routing rule, never a dangling profile.
  ASSERT_TRUE(window_->RemoveWorkspace());
  EXPECT_TRUE(window_->WorkspaceForUrl(target).empty());
  EXPECT_TRUE(window_->link_rules().empty());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, RoutingLeavesFormsFramesAndRedirects) {
  const auto source_url = embedded_test_server()->GetURL("/links_probe.html");
  const auto frame_url = embedded_test_server()->GetURL("client.test", "/storage_probe.html?frame");
  const auto post_url = embedded_test_server()->GetURL("client.test", "/login-success");
  ASSERT_TRUE(window_->SetLinkRule(frame_url, "personal"));
  ASSERT_TRUE(content::NavigateToURL(window_->contents("work"), source_url));
  auto* source = window_->contents("work");
  ASSERT_TRUE(content::ExecJs(source, content::JsReplace(
      "const frame = document.createElement('iframe'); frame.src = $1; document.body.append(frame)", frame_url)));
  ASSERT_TRUE(base::test::RunUntil([&] {
    auto* frame = content::ChildFrameAt(source->GetPrimaryMainFrame(), 0);
    return frame && frame->GetLastCommittedURL() == frame_url;
  }));
  EXPECT_EQ("work", window_->active_id());
  ASSERT_TRUE(content::ExecJs(source, content::JsReplace(
      "const form = document.createElement('form'); form.action = $1; form.method = 'post'; document.body.append(form); form.requestSubmit()", post_url)));
  ASSERT_TRUE(base::test::RunUntil([&] { return source->GetLastCommittedURL() == post_url; }));
  EXPECT_EQ("work", window_->active_id());
  EXPECT_EQ(work_.get(), source->GetBrowserContext());
  EXPECT_EQ(1u, window_->tab_count("personal"));
  ASSERT_TRUE(content::NavigateToURL(source, source_url));
  ASSERT_TRUE(content::ExecJs(source, "document.getElementById('route').href = '/route-redirect'; document.getElementById('route').click()"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return source->GetLastCommittedURL() == embedded_test_server()->GetURL("client.test", "/storage_probe.html?redirect");
  }));
  EXPECT_EQ("work", window_->active_id());
  EXPECT_EQ(work_.get(), source->GetBrowserContext());
  EXPECT_EQ(1u, window_->tab_count("personal"));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, DownloadsTransferControlsAndProfileIsolation) {
  base::ScopedAllowBlockingForTesting test_files;
  base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Seconds(30));
  NavigateBoth();
  ASSERT_TRUE(window_->Select("work"));
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  DownloadPrefs::FromBrowserContext(work_)->SetDownloadPath(directory.GetPath());
  work_->GetPrefs()->SetBoolean(prefs::kPromptForDownload, false);
  auto* manager = work_->GetDownloadManager();
  net::EmbeddedTestServer server;
  net::test_server::ControllableHttpResponse stream(&server, "/transfer.txt");
  net::test_server::ControllableHttpResponse cancelled(&server, "/cancel.txt");
  ASSERT_TRUE(server.Start());
  window_->ShowDownloads();
  auto* panel = window_->downloads();
  ASSERT_TRUE(panel && panel->widget() && panel->widget()->IsVisible());
  EXPECT_EQ(ui::mojom::ModalType::kNone, panel->widget()->widget_delegate()->GetModalType());
  const auto area = display::Screen::Get()->GetDisplayNearestWindow(window_->widget()->GetNativeWindow()).work_area();
  EXPECT_TRUE(area.Contains(panel->widget()->GetWindowBoundsInScreen()));
  auto start = [&](std::string path) {
    manager->DownloadUrl(content::DownloadRequestUtils::CreateDownloadForWebContentsMainFrame(
        window_->contents("work"), server.GetURL(path), TRAFFIC_ANNOTATION_FOR_TESTS));
  };
  start("/transfer.txt");
  stream.WaitForRequest();
  stream.Send("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Disposition: attachment; filename=transfer.txt\r\nContent-Length: 262144\r\n\r\n");
  stream.Send(std::string(32768, 'x'));
  download::DownloadItem* item = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    content::DownloadManager::DownloadVector items;
    manager->GetAllDownloads(&items);
    if (items.size() != 1) return false;
    item = items[0];
    return item->GetReceivedBytes() > 0 && !item->GetTargetFilePath().empty();
  }));
  const auto guid = item->GetGuid();
  ASSERT_TRUE(base::test::RunUntil([&] { return FindButton(panel->widget()->GetContentsView(), u"Wstrzymaj"); }));
  ClickControl(FindButton(panel->widget()->GetContentsView(), u"Wstrzymaj"));
  ASSERT_TRUE(base::test::RunUntil([&] { return item->IsPaused(); }));
  EXPECT_FALSE(panel->Act(guid, DownloadPanel::Action::kReveal));
  ASSERT_TRUE(window_->Select("personal"));
  window_->ShowDownloads();
  EXPECT_TRUE(panel->visible_guids().empty());
  EXPECT_FALSE(panel->Act(guid, DownloadPanel::Action::kCancel));
  EXPECT_EQ(download::DownloadItem::IN_PROGRESS, item->GetState());
  EXPECT_TRUE(item->IsPaused());
  ASSERT_TRUE(window_->Select("work"));
  window_->ShowDownloads();
  ASSERT_TRUE(base::test::RunUntil([&] { return FindButton(panel->widget()->GetContentsView(), u"Wznów"); }));
  ClickControl(FindButton(panel->widget()->GetContentsView(), u"Wznów"));
  ASSERT_TRUE(base::test::RunUntil([&] { return !item->IsPaused(); }));
  stream.Send(std::string(262144 - 32768, 'x')); stream.Done();
  ASSERT_TRUE(base::test::RunUntil([&] { return item->GetState() == download::DownloadItem::COMPLETE; }));
  {
    base::ScopedAllowBlockingForTesting allow;
    std::string bytes;
    ASSERT_TRUE(base::ReadFileToString(item->GetTargetFilePath(), &bytes));
    EXPECT_EQ(std::string(262144, 'x'), bytes);
  }
  // Check Finder dispatch without opening a real OS window during automation.
  class RevealDelegate final : public content::DownloadManagerDelegate {
   public:
    void ShowDownloadInShell(download::DownloadItem* item) override { revealed = item; }
    raw_ptr<download::DownloadItem> revealed = nullptr;
  } reveal;
  auto* original_delegate = manager->GetDelegate();
  manager->SetDelegate(&reveal);
  EXPECT_TRUE(panel->Act(guid, DownloadPanel::Action::kReveal));
  EXPECT_EQ(item, reveal.revealed);
  manager->SetDelegate(original_delegate);
  start("/cancel.txt"); cancelled.WaitForRequest();
  cancelled.Send("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Disposition: attachment; filename=cancel.txt\r\nContent-Length: 262144\r\n\r\n");
  cancelled.Send(std::string(32768, 'y'));
  download::DownloadItem* second = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    content::DownloadManager::DownloadVector items; manager->GetAllDownloads(&items);
    for (auto candidate : items) if (candidate->GetGuid() != guid) second = candidate;
    return second && second->GetReceivedBytes() > 0;
  }));
  ASSERT_TRUE(base::test::RunUntil([&] { return FindButton(panel->widget()->GetContentsView(), u"Anuluj"); }));
  ClickControl(FindButton(panel->widget()->GetContentsView(), u"Anuluj"));
  ASSERT_TRUE(base::test::RunUntil([&] { return second->GetState() == download::DownloadItem::CANCELLED; }));
  EXPECT_FALSE(panel->Act(second->GetGuid(), DownloadPanel::Action::kPauseResume));
  EXPECT_FALSE(panel->Act(second->GetGuid(), DownloadPanel::Action::kReveal));
  cancelled.Done();
  panel->widget()->Close();
  ASSERT_TRUE(base::test::RunUntil([&] { return !panel->widget(); }));
  window_->ShowDownloads();
  EXPECT_EQ(2u, panel->visible_guids().size());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, DownloadsSceneMetricsUnknownProgressAndCocoaPause) {
  NavigateBoth();ASSERT_TRUE(window_->Select("work"));base::ScopedTempDir directory;ASSERT_TRUE(directory.CreateUniqueTempDir());DownloadPrefs::FromBrowserContext(work_)->SetDownloadPath(directory.GetPath());work_->GetPrefs()->SetBoolean(prefs::kPromptForDownload,false);
  net::EmbeddedTestServer server;net::test_server::ControllableHttpResponse response(&server,"/unknown.txt");ASSERT_TRUE(server.Start());window_->ShowDownloads();auto* panel=window_->downloads();ASSERT_TRUE(panel&&panel->widget());const bool rust=window_->shell()->workspace_input()->rust_editor();auto* manager=work_->GetDownloadManager();manager->DownloadUrl(content::DownloadRequestUtils::CreateDownloadForWebContentsMainFrame(window_->contents("work"),server.GetURL("/unknown.txt"),TRAFFIC_ANNOTATION_FOR_TESTS));response.WaitForRequest();response.Send("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Disposition: attachment; filename=unknown.txt\r\n\r\n");response.Send(std::string(8192,'x'));
  download::DownloadItem* item=nullptr;ASSERT_TRUE(base::test::RunUntil([&]{content::DownloadManager::DownloadVector items;manager->GetAllDownloads(&items);if(items.empty())return false;item=items[0];return item->GetReceivedBytes()>0&&!item->GetTargetFilePath().empty()&&FindButton(panel->widget()->GetContentsView(),u"Wstrzymaj");}));ASSERT_EQ(-1,item->PercentComplete());auto* list=panel->list_for_testing();ASSERT_TRUE(list);ASSERT_EQ(1u,list->children().size());auto* row=list->children()[0].get();ASSERT_EQ(5u,row->children().size());
  for(size_t i=0;i<3;++i){auto* copy=row->children()[i].get();ui::AXNodeData node;copy->GetViewAccessibility().GetAccessibleNodeData(&node);EXPECT_EQ(ax::mojom::Role::kStaticText,node.role);views::Label reference(node.GetString16Attribute(ax::mojom::StringAttribute::kName));reference.SetMultiLine(true);reference.SetHorizontalAlignment(gfx::ALIGN_LEFT);reference.SetSubpixelRenderingEnabled(false);EXPECT_EQ(reference.GetPreferredSize(),copy->GetPreferredSize());for(int width:{0,100,300,560})EXPECT_EQ(reference.GetHeightForWidth(width),copy->GetHeightForWidth(width));if(rust){EXPECT_TRUE(copy->children().empty());EXPECT_FALSE(views::AsViewClass<views::Label>(copy));}}
  panel->widget()->LayoutRootViewIfNecessary();auto* filename=row->children()[0].get();EXPECT_EQ(item->GetTargetFilePath().LossyDisplayName(),filename->GetTooltipText());EXPECT_EQ(filename,filename->GetTooltipHandlerForPoint(gfx::Point(1,1)));
  auto* progress=row->children()[3].get();views::ProgressBar reference_progress;EXPECT_EQ(reference_progress.GetPreferredSize(),progress->GetPreferredSize());ui::AXNodeData progress_ax;progress->GetViewAccessibility().GetAccessibleNodeData(&progress_ax);EXPECT_EQ(ax::mojom::Role::kProgressIndicator,progress_ax.role);EXPECT_FALSE(progress_ax.HasStringAttribute(ax::mojom::StringAttribute::kValue));if(rust){EXPECT_TRUE(progress->children().empty());EXPECT_FALSE(views::AsViewClass<views::ProgressBar>(progress));}
  auto* actions=row->children()[4].get();views::View reference_actions;reference_actions.SetLayoutManager(std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,gfx::Insets(),8));
  for(const auto& child:actions->children()){auto* button=child.get();ASSERT_TRUE(button);EXPECT_EQ(!rust,views::AsViewClass<views::Button>(button)!=nullptr);ui::AXNodeData node;button->GetViewAccessibility().GetAccessibleNodeData(&node);auto* reference=reference_actions.AddChildView(std::make_unique<views::MdTextButton>(views::Button::PressedCallback(),node.GetString16Attribute(ax::mojom::StringAttribute::kName)));reference->SetVisible(button->GetVisible());EXPECT_EQ(reference->GetPreferredSize(),button->GetPreferredSize());EXPECT_EQ(reference->GetMinimumSize(),button->GetMinimumSize());if(rust)EXPECT_TRUE(button->children().empty());}EXPECT_EQ(reference_actions.GetPreferredSize(),actions->GetPreferredSize());
  if(rust){ASSERT_TRUE(base::test::RunUntil([&]{return panel->scene_visible_for_testing()&&panel->scene_order_correct_for_testing()&&panel->scene_animating_for_testing()&&panel->scene_contains_text_for_testing("unknown.txt");}));panel->widget()->Hide();ASSERT_TRUE(base::test::RunUntil([&]{return !panel->scene_visible_for_testing()&&!panel->scene_animating_for_testing();}));panel->widget()->Show();}
  ui_controls::EnableUIControls();auto activate=[&](std::u16string text){auto* widget=panel->widget();auto* button=FindButton(widget->GetContentsView(),text);ASSERT_TRUE(button);ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(widget->GetNativeWindow()));button->RequestFocus();base::test::TestFuture<void> presented;auto* compositor=widget->GetCompositor();ASSERT_TRUE(compositor);widget->GetRootView()->SchedulePaint();compositor->RequestSuccessfulPresentationTimeForNextFrame(base::BindOnce([](base::OnceClosure done,const viz::FrameTimingDetails&){std::move(done).Run();},presented.GetCallback()));compositor->ScheduleFullRedraw();ASSERT_TRUE(presented.Wait());ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(widget->GetNativeWindow(),ui::VKEY_SPACE,false,false,false,false));};activate(u"Wstrzymaj");ASSERT_TRUE(base::test::RunUntil([&]{return item->IsPaused();}));ASSERT_TRUE(base::test::RunUntil([&]{return FindButton(panel->widget()->GetContentsView(),u"Wznów");}));activate(u"Wznów");ASSERT_TRUE(base::test::RunUntil([&]{return !item->IsPaused();}));EXPECT_TRUE(panel->Act(item->GetGuid(),DownloadPanel::Action::kCancel));ASSERT_TRUE(base::test::RunUntil([&]{return item->GetState()==download::DownloadItem::CANCELLED;}));response.Done();auto* widget=panel->widget();ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(widget->GetNativeWindow(),ui::VKEY_ESCAPE,false,false,false,false));ASSERT_TRUE(base::test::RunUntil([&]{return !panel->widget();}));EXPECT_FALSE(panel->scene_visible_for_testing());EXPECT_FALSE(panel->scene_animating_for_testing());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, DownloadsDangerousHistoryCannotRevealOrResume) {
  base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Seconds(30));
  window_->ShowDownloads();
  auto* panel = window_->downloads();
  auto* manager = work_->GetDownloadManager();
  ASSERT_TRUE(base::test::RunUntil([&] { return manager->IsManagerInitialized(); }));
  auto path = work_->GetPath().AppendASCII("blocked-test.txt");
  auto* blocked = manager->CreateDownloadItem(base::Uuid::GenerateRandomV4().AsLowercaseString(), 777,
      path, path, {embedded_test_server()->GetURL("/blocked-test.txt")}, GURL(),
      content::StoragePartitionConfig::CreateDefault(work_), GURL(), GURL(), std::nullopt,
      "text/plain", "text/plain", base::Time::Now(), base::Time::Now(), "", "", 4, 4, "",
      download::DownloadItem::COMPLETE, download::DOWNLOAD_DANGER_TYPE_DANGEROUS_CONTENT,
      download::DOWNLOAD_INTERRUPT_REASON_NONE, false, base::Time::Now(), false, {}, base::FilePath());
  ASSERT_TRUE(blocked);
  ASSERT_TRUE(blocked->IsDangerous());
  ASSERT_TRUE(base::test::RunUntil([&] { return panel->visible_guids().size() == 1; }));
  EXPECT_FALSE(panel->Act(blocked->GetGuid(), DownloadPanel::Action::kReveal));
  EXPECT_FALSE(panel->Act(blocked->GetGuid(), DownloadPanel::Action::kPauseResume));
  window_->Select("personal"); window_->ShowDownloads();
  EXPECT_TRUE(panel->visible_guids().empty());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, PasswordOverviewAccountsCocoaAndStaleLifetime) {
  NavigateBoth();ASSERT_TRUE(window_->Select("work"));
  auto* page=window_->contents("work");auto* services=PageServices::FromWebContents(page);ASSERT_TRUE(services);
  auto store=ProfilePasswordStoreFactory::GetForProfile(work_,ServiceAccessType::EXPLICIT_ACCESS);ASSERT_TRUE(store);
  for(auto username:{u"alpha",u"zulu"}){
    password_manager::StoredCredential credential;credential.url=embedded_test_server()->GetURL("/login_probe.html");credential.signon_realm=url::Origin::Create(credential.url).Serialize()+"/";credential.username_value=username;credential.password_value=password_manager::PasswordString(u"scene-secret-never-render");
    base::test::TestFuture<void> added;store->AddLogin(std::move(credential),added.GetCallback());ASSERT_TRUE(added.Wait());
  }
  const bool rust=window_->shell()->workspace_input()->rust_editor();
  auto show=[&]{services->ShowPasswords();return base::test::RunUntil([&]{return services->password_prompt()&&services->password_prompt()->IsVisible();});};
  ASSERT_TRUE(show());auto* widget=services->password_prompt();
  views::View* picker=nullptr;std::u16string exposed;
  auto visit=[&](auto&& self,views::View* view)->void{ui::AXNodeData node;view->GetViewAccessibility().GetAccessibleNodeData(&node);exposed+=node.GetString16Attribute(ax::mojom::StringAttribute::kName)+node.GetString16Attribute(ax::mojom::StringAttribute::kValue);if(node.GetString16Attribute(ax::mojom::StringAttribute::kName)==u"Zapisane konta")picker=view;for(const auto& child:view->children())self(self,child.get());};visit(visit,widget->GetContentsView());
  ASSERT_TRUE(picker);EXPECT_EQ(std::u16string::npos,exposed.find(u"scene-secret-never-render"));
  ui::AXNodeData account;picker->GetViewAccessibility().GetAccessibleNodeData(&account);EXPECT_NE(std::u16string::npos,account.GetString16Attribute(ax::mojom::StringAttribute::kValue).find(u"alpha"));
  if(rust){EXPECT_TRUE(picker->children().empty());EXPECT_FALSE(views::AsViewClass<views::Button>(picker));EXPECT_FALSE(views::AsViewClass<views::Combobox>(picker));ASSERT_TRUE(base::test::RunUntil([&]{return services->password_scene_visible_for_testing()&&services->password_scene_contains_for_testing(base::UTF16ToUTF8(u"Profil: "+BrowsingProfileName(work_)));}));EXPECT_FALSE(services->password_scene_contains_for_testing("scene-secret-never-render"));}
  ui_controls::EnableUIControls();ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(widget->GetNativeWindow()));picker->RequestFocus();
  base::test::TestFuture<void> presented;auto* compositor=widget->GetCompositor();ASSERT_TRUE(compositor);widget->GetRootView()->SchedulePaint();compositor->RequestSuccessfulPresentationTimeForNextFrame(base::BindOnce([](base::OnceClosure done,const viz::FrameTimingDetails&){std::move(done).Run();},presented.GetCallback()));compositor->ScheduleFullRedraw();ASSERT_TRUE(presented.Wait());
  // Account labels start with the origin, so type its first character. This
  // exercises the platform prefix input client without opening a nested menu.
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(widget->GetNativeWindow(),ui::VKEY_H,false,false,false,false));
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(widget->GetNativeWindow(),ui::VKEY_ESCAPE,false,false,false,false));ASSERT_TRUE(base::test::RunUntil([&]{return !services->password_prompt();}));EXPECT_FALSE(services->password_scene_visible_for_testing());
  ASSERT_TRUE(show());views::ViewTracker old(services->password_prompt()->GetContentsView());services->password_prompt()->CloseNow();ASSERT_TRUE(base::test::RunUntil([&]{return !services->password_prompt()&&!old.view();}));
  services->ShowPasswords();ASSERT_TRUE(window_->Select("personal"));base::RunLoop().RunUntilIdle();EXPECT_FALSE(services->password_prompt());
  auto* personal=PageServices::FromWebContents(window_->contents("personal"));personal->ShowPasswords();ASSERT_TRUE(base::test::RunUntil([&]{return personal->password_prompt()!=nullptr;}));
  std::u16string private_copy;auto inspect=[&](auto&& self,views::View* view)->void{ui::AXNodeData node;view->GetViewAccessibility().GetAccessibleNodeData(&node);private_copy+=node.GetString16Attribute(ax::mojom::StringAttribute::kName)+node.GetString16Attribute(ax::mojom::StringAttribute::kValue);for(const auto& child:view->children())self(self,child.get());};inspect(inspect,personal->password_prompt()->GetContentsView());EXPECT_NE(std::u16string::npos,private_copy.find(u"Brak zapisanych logowań"));EXPECT_EQ(std::u16string::npos,private_copy.find(u"alpha"));personal->password_prompt()->CloseNow();
  ASSERT_TRUE(window_->Select("work"));ASSERT_TRUE(show());ASSERT_TRUE(content::NavigateToURL(page,embedded_test_server()->GetURL("/storage_probe.html?password-close")));EXPECT_FALSE(services->password_prompt());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, PasswordOverviewVisibleAndDraggable) {
  NavigateBoth();
  ASSERT_TRUE(window_->Select("work"));
  const auto area = display::Screen::Get()->GetDisplayNearestWindow(window_->widget()->GetNativeWindow()).work_area();
  window_->widget()->SetBounds(gfx::Rect(area.right() - 400, area.y(), 400, 500));
  auto* services = PageServices::FromWebContents(window_->contents("work"));
  window_->ShowPasswords();
  ASSERT_TRUE(base::test::RunUntil([&] { return services->password_prompt() && services->password_prompt()->IsVisible(); }));
  auto* dialog = services->password_prompt();
  const auto actual_area = display::Screen::Get()->GetDisplayNearestWindow(window_->widget()->GetNativeWindow()).work_area();
  EXPECT_TRUE(actual_area.Contains(dialog->GetWindowBoundsInScreen())) << actual_area.ToString() << " / " << dialog->GetWindowBoundsInScreen().ToString();
  EXPECT_TRUE(dialog->widget_delegate()->AsDialogDelegate()->draggable());
  EXPECT_EQ(ui::mojom::ModalType::kNone, dialog->widget_delegate()->GetModalType());
  dialog->widget_delegate()->AsDialogDelegate()->AcceptDialog();
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, NativeJavaScriptDialogs) {
  auto* page = window_->contents("work");
  ASSERT_TRUE(content::NavigateToURL(page, embedded_test_server()->GetURL("/storage_probe.html?dialogs")));
  auto* dialogs = PageDialogs::FromWebContents(page);
  ASSERT_TRUE(dialogs);
  content::ExecuteScriptAsync(page, "window.answer = prompt('Podaj nazwę', 'szkic');");
  ASSERT_TRUE(base::test::RunUntil([&] { return dialogs->dialog() != nullptr; }));
  std::u16string response = u"zażółć";
  EXPECT_TRUE(dialogs->HandleJavaScriptDialog(page, true, &response));
  EXPECT_EQ("zażółć", content::EvalJs(page, "answer"));
  content::ExecuteScriptAsync(page, "window.answer = confirm('Potwierdź?');");
  ASSERT_TRUE(base::test::RunUntil([&] { return dialogs->dialog() != nullptr; }));
  dialogs->dialog()->widget_delegate()->AsDialogDelegate()->CancelDialog();
  EXPECT_EQ(false, content::EvalJs(page, "answer"));
  content::ExecuteScriptAsync(page, "alert('Wiadomość'); window.alertDone=true;");
  ASSERT_TRUE(base::test::RunUntil([&] { return dialogs->dialog() != nullptr; }));
  dialogs->dialog()->widget_delegate()->AsDialogDelegate()->AcceptDialog();
  EXPECT_EQ(true, content::EvalJs(page, "alertDone"));
  content::ExecuteScriptAsync(page, "window.answer = prompt('Anuluj przy nawigacji');");
  ASSERT_TRUE(base::test::RunUntil([&] { return dialogs->dialog() != nullptr; }));
  ASSERT_TRUE(content::NavigateToURL(page, embedded_test_server()->GetURL("/storage_probe.html?after-dialog")));
  EXPECT_FALSE(dialogs->dialog());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, PageDialogsSceneCopyMetricsRolesAndMessageLimit) {
  auto* page=window_->contents("work");ASSERT_TRUE(content::NavigateToURL(page,embedded_test_server()->GetURL("/storage_probe.html?dialog-copy")));auto* manager=PageDialogs::FromWebContents(page);ASSERT_TRUE(manager);
  int closed=0;bool accepted=false;base::ScopedClosureRunner cleanup(base::BindOnce([](PageDialogs* m,content::WebContents* p){m->CancelDialogs(p,true);base::RunLoop().RunUntilIdle();},base::Unretained(manager),base::Unretained(page)));
  const bool rust=window_->shell()->workspace_input()->rust_editor();std::u16string message;for(int i=0;i<900;++i)message+=u"Linia 🦀 אבג 日本語\n";const auto bounded=message.substr(0,8192);
  for(int kind:{0,1,2,3}){
    auto callback=base::BindOnce([](int* count,bool* value,bool ok,const std::u16string&){++*count;*value=ok;},base::Unretained(&closed),base::Unretained(&accepted));
    if(kind==3)manager->RunBeforeUnloadDialog(page,page->GetPrimaryMainFrame(),false,std::move(callback));
    else{bool suppressed=true;manager->RunJavaScriptDialog(page,page->GetPrimaryMainFrame(),kind==0?content::JAVASCRIPT_DIALOG_TYPE_ALERT:kind==1?content::JAVASCRIPT_DIALOG_TYPE_CONFIRM:content::JAVASCRIPT_DIALOG_TYPE_PROMPT,message,u"draft 🦀",std::move(callback),&suppressed);EXPECT_FALSE(suppressed);}
    ASSERT_TRUE(manager->dialog());auto* delegate=manager->dialog()->widget_delegate()->AsDialogDelegate();const auto origin=base::UTF8ToUTF16(page->GetPrimaryMainFrame()->GetLastCommittedOrigin().Serialize());
    EXPECT_EQ((kind==3?u"Opuścić stronę? · ":u"Strona: ")+origin,delegate->GetWindowTitle());EXPECT_EQ(kind==0?static_cast<int>(ui::mojom::DialogButton::kOk):static_cast<int>(ui::mojom::DialogButton::kOk)|static_cast<int>(ui::mojom::DialogButton::kCancel),delegate->buttons());
    EXPECT_EQ(kind==3?u"Opuść stronę":u"OK",delegate->GetDialogButtonLabel(ui::mojom::DialogButton::kOk));EXPECT_EQ(kind==3?u"Zostań":u"Anuluj",delegate->GetDialogButtonLabel(ui::mojom::DialogButton::kCancel));EXPECT_EQ(static_cast<int>(kind==3?ui::mojom::DialogButton::kCancel:ui::mojom::DialogButton::kOk),delegate->GetDefaultDialogButton());EXPECT_EQ(kind==2,manager->prompt_input()!=nullptr);
    auto* body=manager->dialog_body_for_testing();ASSERT_TRUE(body);ASSERT_EQ(kind==2?3u:2u,body->children().size());auto* identity=body->children()[0].get();auto* scroll=manager->viewport_for_testing();ASSERT_TRUE(scroll);EXPECT_EQ(body->children()[1],scroll->view());EXPECT_EQ(window_->shell()->workspace_input()->rust_editor(),scroll->rust_owned());if(scroll->rust_owned())EXPECT_FALSE(views::AsViewClass<views::ScrollView>(scroll->view()));auto* copy=scroll->contents();ASSERT_TRUE(copy);
    const std::u16string identity_text=u"Profil: "+BrowsingProfileName(work_);const std::u16string text=kind==3?u"Opuszczenie strony może usunąć niezapisane zmiany.":bounded;
    for(const auto& pair:std::vector<std::pair<views::View*,std::u16string>>{{identity,identity_text},{copy,text}}){ui::AXNodeData node;pair.first->GetViewAccessibility().GetAccessibleNodeData(&node);EXPECT_EQ(ax::mojom::Role::kStaticText,node.role);EXPECT_EQ(pair.second,node.GetString16Attribute(ax::mojom::StringAttribute::kName));EXPECT_EQ(!rust,views::AsViewClass<views::Label>(pair.first)!=nullptr);EXPECT_TRUE(pair.first->children().empty());}
    views::Label reference_identity(identity_text),reference_message(text);reference_identity.SetSubpixelRenderingEnabled(false);reference_message.SetMultiLine(true);reference_message.SetHorizontalAlignment(gfx::ALIGN_LEFT);reference_message.SetSubpixelRenderingEnabled(false);reference_message.SizeToFit(440);
    EXPECT_EQ(reference_identity.GetPreferredSize(),identity->GetPreferredSize());EXPECT_EQ(reference_message.GetPreferredSize(),copy->GetPreferredSize());
    for(int width:{80,160,320,440}){EXPECT_EQ(reference_identity.GetHeightForWidth(width),identity->GetHeightForWidth(width));EXPECT_EQ(reference_message.GetHeightForWidth(width),copy->GetHeightForWidth(width));}
    const auto before=closed;if(kind==3)delegate->CancelDialog();else delegate->AcceptDialog();ASSERT_TRUE(base::test::RunUntil([&]{return !manager->dialog()&&closed==before+1;}));EXPECT_EQ(kind!=3,accepted);EXPECT_FALSE(manager->scene_visible_for_testing());EXPECT_FALSE(manager->dialog_body_for_testing());
  }
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, PageDialogsActionGeometryAXAndCocoaActivation) {
  auto* page=window_->contents("work");ASSERT_TRUE(content::NavigateToURL(page,embedded_test_server()->GetURL("/storage_probe.html?dialog-actions")));auto* manager=PageDialogs::FromWebContents(page);ASSERT_TRUE(manager);
  const bool rust=window_->shell()->workspace_input()->rust_editor();int closed=0;bool accepted=false;
  base::ScopedClosureRunner cleanup(base::BindOnce([](PageDialogs* m,content::WebContents* p){m->CancelDialogs(p,true);base::RunLoop().RunUntilIdle();},base::Unretained(manager),base::Unretained(page)));
  ui_controls::EnableUIControls();
  for(int kind:{0,1,2,3}){
    auto callback=base::BindOnce([](int* count,bool* value,bool ok,const std::u16string&){++*count;*value=ok;},base::Unretained(&closed),base::Unretained(&accepted));
    if(kind==3)manager->RunBeforeUnloadDialog(page,page->GetPrimaryMainFrame(),false,std::move(callback));
    else{bool suppressed=true;manager->RunJavaScriptDialog(page,page->GetPrimaryMainFrame(),kind==0?content::JAVASCRIPT_DIALOG_TYPE_ALERT:kind==1?content::JAVASCRIPT_DIALOG_TYPE_CONFIRM:content::JAVASCRIPT_DIALOG_TYPE_PROMPT,u"Actions fixture",u"draft",std::move(callback),&suppressed);ASSERT_FALSE(suppressed);}
    auto* widget=manager->dialog();ASSERT_TRUE(widget);auto* delegate=widget->widget_delegate()->AsDialogDelegate();auto* ok=manager->dialog_button_for_testing(true);auto* cancel=manager->dialog_button_for_testing(false);ASSERT_TRUE(ok);EXPECT_EQ(kind!=0,cancel!=nullptr);
    auto reference=std::make_unique<views::DialogDelegate>();reference->SetOwnershipOfNewWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);auto* reference_body=reference->SetContentsView(std::make_unique<views::View>());reference_body->SetPreferredSize(manager->dialog_body_for_testing()->GetPreferredSize());reference->SetButtons(delegate->buttons());reference->SetDefaultButton(delegate->GetDefaultDialogButton());reference->SetButtonLabel(ui::mojom::DialogButton::kOk,delegate->GetDialogButtonLabel(ui::mojom::DialogButton::kOk));reference->SetButtonLabel(ui::mojom::DialogButton::kCancel,delegate->GetDialogButtonLabel(ui::mojom::DialogButton::kCancel));
    std::unique_ptr<views::Widget> reference_widget(views::DialogDelegate::CreateDialogWidget(reference.get(),window_->widget()->GetNativeWindow(),window_->widget()->GetNativeView()));reference_widget->SetColorModeOverride(widget->color_mode_override());auto* reference_client=reference_widget->widget_delegate()->AsDialogDelegate()->GetDialogClientView();
    widget->LayoutRootViewIfNecessary();reference_widget->LayoutRootViewIfNecessary();
    EXPECT_EQ(reference_client->ok_button()->GetPreferredSize(),ok->GetPreferredSize());EXPECT_EQ(reference_client->ok_button()->parent()->GetPreferredSize(),ok->parent()->GetPreferredSize());
    if(cancel){EXPECT_EQ(reference_client->cancel_button()->GetPreferredSize(),cancel->GetPreferredSize());EXPECT_EQ(views::PlatformStyle::kIsOkButtonLeading,ok->bounds().x()<cancel->bounds().x());}
    for(auto* button:{ok,cancel})if(button){ui::AXNodeData node;button->GetViewAccessibility().GetAccessibleNodeData(&node);EXPECT_EQ(ax::mojom::Role::kButton,node.role);EXPECT_EQ(delegate->GetDialogButtonLabel(button==ok?ui::mojom::DialogButton::kOk:ui::mojom::DialogButton::kCancel),node.GetString16Attribute(ax::mojom::StringAttribute::kName));EXPECT_EQ(delegate->GetIsDefault(button==ok?ui::mojom::DialogButton::kOk:ui::mojom::DialogButton::kCancel),node.HasState(ax::mojom::State::kDefault));if(rust){EXPECT_TRUE(button->children().empty());EXPECT_FALSE(views::AsViewClass<views::MdTextButton>(button));}}
    if(rust){EXPECT_FALSE(delegate->GetDialogClientView()->ok_button());EXPECT_FALSE(delegate->GetDialogClientView()->cancel_button());ASSERT_TRUE(base::test::RunUntil([&]{return PageDialogActionsVisibleForTesting(ok->parent(),base::UTF16ToUTF8(delegate->GetDialogButtonLabel(ui::mojom::DialogButton::kOk)));}));}
    reference_widget.reset();reference.reset();
    if(kind==1){delegate->SetButtonLabel(ui::mojom::DialogButton::kOk,u"Nowe OK 🦀");delegate->SetButtonEnabled(ui::mojom::DialogButton::kOk,false);ok=manager->dialog_button_for_testing(true);ASSERT_TRUE(ok);EXPECT_FALSE(ok->GetEnabled());ui::AXNodeData node;ok->GetViewAccessibility().GetAccessibleNodeData(&node);EXPECT_EQ(u"Nowe OK 🦀",node.GetString16Attribute(ax::mojom::StringAttribute::kName));delegate->SetButtonEnabled(ui::mojom::DialogButton::kOk,true);ok=manager->dialog_button_for_testing(true);cancel=manager->dialog_button_for_testing(false);ASSERT_TRUE(ok);ASSERT_TRUE(cancel);EXPECT_TRUE(ok->GetEnabled());widget->LayoutRootViewIfNecessary();}
    ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(widget->GetNativeWindow()));auto* target=kind==0?ok:cancel;target->RequestFocus();
    base::test::TestFuture<void> presented;auto* compositor=widget->GetCompositor();ASSERT_TRUE(compositor);widget->GetRootView()->SchedulePaint();compositor->RequestSuccessfulPresentationTimeForNextFrame(base::BindOnce([](base::OnceClosure done,const viz::FrameTimingDetails&){std::move(done).Run();},presented.GetCallback()));compositor->ScheduleFullRedraw();ASSERT_TRUE(presented.Wait());
    if(kind==1){
      class BlockedInput final : public views::InputEventActivationProtector {
       public:int calls=0;bool IsPossiblyUnintendedInteraction(const ui::Event&,bool,const views::View*) override{++calls;return true;}
      };
      auto protection=std::make_unique<BlockedInput>();auto* observed=protection.get();delegate->GetDialogClientView()->SetInputProtectorForTesting(std::move(protection));ui::KeyEvent protected_space(ui::EventType::kKeyPressed,ui::VKEY_SPACE,ui::EF_NONE);EXPECT_TRUE(target->OnKeyPressed(protected_space));EXPECT_EQ(1,observed->calls);EXPECT_EQ(widget,manager->dialog());EXPECT_EQ(kind,closed);delegate->GetDialogClientView()->SetInputProtectorForTesting(std::make_unique<views::InputEventActivationProtector>());
    }
    delegate->ResetViewShownTimeStampForTesting();
    if(kind==1){const auto point=target->GetBoundsInScreen().CenterPoint();base::test::TestFuture<void> moved,clicked;ASSERT_TRUE(ui_controls::SendMouseMoveNotifyWhenDone(point.x(),point.y(),moved.GetCallback(),widget->GetNativeWindow()));ASSERT_TRUE(moved.Wait());ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(ui_controls::LEFT,ui_controls::DOWN|ui_controls::UP,clicked.GetCallback(),ui_controls::kNoAccelerator,widget->GetNativeWindow()));ASSERT_TRUE(clicked.Wait());}
    else ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(widget->GetNativeWindow(),ui::VKEY_SPACE,false,false,false,false));
    ASSERT_TRUE(base::test::RunUntil([&]{return !manager->dialog()&&closed==kind+1;}));EXPECT_EQ(kind==0,accepted);
  }
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, JavaScriptPromptEditorCocoaCompositionAndKeyboard) {
  auto* page=window_->contents("work");ASSERT_TRUE(content::NavigateToURL(page,embedded_test_server()->GetURL("/storage_probe.html?prompt-editor")));
  auto* manager=PageDialogs::FromWebContents(page);ASSERT_TRUE(manager);
  window_->shell()->SetThemeMode(ThemeMode::kDark);
  content::ExecuteScriptAsync(page,"window.answer=prompt('Long message\\n'.repeat(800),'szkic 🦀');");
  ASSERT_TRUE(base::test::RunUntil([&]{return manager->dialog()!=nullptr;}));
  auto* field=manager->prompt_input();ASSERT_TRUE(field);auto* widget=manager->dialog();
  EXPECT_EQ(ui::ColorProviderKey::ColorMode::kDark,widget->GetColorMode());
  EXPECT_EQ(window_->shell()->workspace_input()->rust_editor(),field->rust_editor());EXPECT_EQ(u"szkic 🦀",field->GetText());
  ui_controls::EnableUIControls();ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(widget->GetNativeWindow()));field->RequestFocus();
  field->SelectRange(gfx::Range(field->GetText().size()));
  auto key=[&](ui::KeyboardCode code){return ui_test_utils::SendKeyPressToWindowSync(manager->dialog()->GetNativeWindow(),code,false,false,false,false);};
  ASSERT_TRUE(key(ui::VKEY_A));ASSERT_TRUE(base::test::RunUntil([&]{return field->GetText()==u"szkic 🦀a";}));
  EXPECT_TRUE(widget->GetContentsView()->GetBoundsInScreen().Contains(field->view()->GetBoundsInScreen()));
  if(field->rust_editor()){EXPECT_TRUE(field->view()->children().empty());EXPECT_FALSE(views::AsViewClass<views::Textfield>(field->view()));ASSERT_TRUE(base::test::RunUntil([&]{return field->metal_visible();}));}
  ui::CompositionText marked;marked.text=u"未";marked.selection=gfx::Range(1);field->SetCompositionText(marked);ASSERT_TRUE(field->IsIMEComposing());
  ui::KeyEvent enter(ui::EventType::kKeyPressed,ui::VKEY_RETURN,0),escape(ui::EventType::kKeyPressed,ui::VKEY_ESCAPE,0);
  EXPECT_FALSE(manager->HandlePromptKey(enter));EXPECT_FALSE(manager->HandlePromptKey(escape));
  widget->widget_delegate()->AsDialogDelegate()->AcceptDialog();base::RunLoop().RunUntilIdle();
  EXPECT_EQ(widget,manager->dialog());EXPECT_EQ(field,manager->prompt_input());EXPECT_TRUE(field->IsIMEComposing());
  field->SetCompositionText(ui::CompositionText());EXPECT_EQ(u"szkic 🦀a",field->GetText());
  field->SelectRange(gfx::Range(0,field->GetText().size()));field->input_client()->InsertText(u"zażółć 🦀",ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);
  marked.text=u"語";field->SetCompositionText(marked);field->ConfirmCompositionText(false);auto lifetime=field->input_client()->AsWeakPtr();
  ASSERT_TRUE(key(ui::VKEY_RETURN));ASSERT_TRUE(base::test::RunUntil([&]{return !manager->dialog()&&!lifetime;}));EXPECT_EQ("zażółć 🦀語",content::EvalJs(page,"answer"));
  content::ExecuteScriptAsync(page,"window.answer=prompt('Escape','draft');");ASSERT_TRUE(base::test::RunUntil([&]{return manager->dialog()!=nullptr;}));
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(manager->dialog()->GetNativeWindow()));manager->prompt_input()->RequestFocus();ASSERT_TRUE(key(ui::VKEY_ESCAPE));
  ASSERT_TRUE(base::test::RunUntil([&]{return !manager->dialog();}));EXPECT_EQ(true,content::EvalJs(page,"answer===null"));
  content::ExecuteScriptAsync(page,"window.answer=prompt('Cancel marked text','draft');");ASSERT_TRUE(base::test::RunUntil([&]{return manager->dialog()!=nullptr;}));
  field=manager->prompt_input();field->SetCompositionText(marked);lifetime=field->input_client()->AsWeakPtr();manager->dialog()->widget_delegate()->AsDialogDelegate()->CancelDialog();
  ASSERT_TRUE(base::test::RunUntil([&]{return !manager->dialog()&&!lifetime;}));EXPECT_EQ(true,content::EvalJs(page,"answer===null"));
  content::ExecuteScriptAsync(page,"window.answer=confirm('Default OK');");ASSERT_TRUE(base::test::RunUntil([&]{return manager->dialog()!=nullptr;}));
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(manager->dialog()->GetNativeWindow()));ASSERT_TRUE(key(ui::VKEY_RETURN));ASSERT_TRUE(base::test::RunUntil([&]{return !manager->dialog();}));EXPECT_EQ(true,content::EvalJs(page,"answer"));
  content::ExecuteScriptAsync(page,"alert('Escape dismisses alert');window.alertDone=true;");ASSERT_TRUE(base::test::RunUntil([&]{return manager->dialog()!=nullptr;}));
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(manager->dialog()->GetNativeWindow()));ASSERT_TRUE(key(ui::VKEY_ESCAPE));ASSERT_TRUE(base::test::RunUntil([&]{return !manager->dialog();}));EXPECT_EQ(true,content::EvalJs(page,"alertDone"));
  ASSERT_TRUE(base::test::RunUntil([&]{return window_->shell()->appearance_saved();}));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, JavaScriptPromptEditorOverrideStaleActionsAndCloseNow) {
  auto* page=window_->contents("work");ASSERT_TRUE(content::NavigateToURL(page,embedded_test_server()->GetURL("/storage_probe.html?prompt-lifetime")));
  auto* manager=PageDialogs::FromWebContents(page);ASSERT_TRUE(manager);
  int count=0;bool accepted=false;std::u16string answer;
  base::ScopedClosureRunner cleanup(base::BindOnce([](PageDialogs* manager,content::WebContents* page){manager->CancelDialogs(page,true);base::RunLoop().RunUntilIdle();},base::Unretained(manager),base::Unretained(page)));
  auto open=[&](std::u16string initial){bool suppressed=true;manager->RunJavaScriptDialog(page,page->GetPrimaryMainFrame(),content::JAVASCRIPT_DIALOG_TYPE_PROMPT,u"Fixture prompt",initial,
      base::BindOnce([](int* count,bool* accepted,std::u16string* answer,bool ok,const std::u16string& text){++*count;*accepted=ok;*answer=text;},base::Unretained(&count),base::Unretained(&accepted),base::Unretained(&answer)),&suppressed);EXPECT_FALSE(suppressed);};
  open(u"initial");ASSERT_TRUE(manager->prompt_input());auto* field=manager->prompt_input();ui::CompositionText marked;marked.text=u"未";marked.selection=gfx::Range(1);field->SetCompositionText(marked);
  std::u16string response=u"override";response.push_back(0);response+=u"\r\n🦀 אבג";
  EXPECT_FALSE(manager->HandleJavaScriptDialog(window_->contents("personal"),true,&response));EXPECT_TRUE(field->IsIMEComposing());
  EXPECT_TRUE(manager->HandleJavaScriptDialog(page,true,&response));EXPECT_FALSE(manager->HandleJavaScriptDialog(page,true,&response));
  base::RunLoop().RunUntilIdle();EXPECT_EQ(1,count);EXPECT_TRUE(accepted);EXPECT_EQ(response,answer);
  open(u"old form");field=manager->prompt_input();ASSERT_TRUE(field);auto lifetime=field->input_client()->AsWeakPtr();ui::KeyEvent enter(ui::EventType::kKeyPressed,ui::VKEY_RETURN,0);
  ASSERT_TRUE(field->view()->OnKeyPressed(enter));manager->CancelDialogs(page,false);open(u"new form");
  ASSERT_TRUE(base::test::RunUntil([&]{return !lifetime&&count==2;}));EXPECT_FALSE(accepted);ASSERT_TRUE(manager->dialog());EXPECT_EQ(u"new form",manager->prompt_input()->GetText());
  EXPECT_TRUE(manager->HandleJavaScriptDialog(page,false,nullptr));base::RunLoop().RunUntilIdle();EXPECT_EQ(3,count);EXPECT_FALSE(accepted);EXPECT_TRUE(answer.empty());
  open(u"close immediately");field=manager->prompt_input();lifetime=field->input_client()->AsWeakPtr();manager->dialog()->CloseNow();
  ASSERT_TRUE(base::test::RunUntil([&]{return !manager->prompt_input()&&!lifetime&&count==4;}));EXPECT_FALSE(accepted);EXPECT_TRUE(answer.empty());
  manager->CancelDialogs(page,true);manager->CancelDialogs(page,true);base::RunLoop().RunUntilIdle();EXPECT_EQ(4,count);
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, JavaScriptPromptEditorHiddenPageAndFrameNavigation) {
  ASSERT_NO_FATAL_FAILURE(NavigateBoth("/storage_probe.html?prompt-scope"));
  ASSERT_TRUE(window_->Select("work"));
  auto* page=window_->contents("work");auto* manager=PageDialogs::FromWebContents(page);ASSERT_TRUE(manager);
  content::ExecuteScriptAsync(page,"window.answer=prompt('Hidden page','draft');");ASSERT_TRUE(base::test::RunUntil([&]{return manager->dialog()!=nullptr;}));auto lifetime=manager->prompt_input()->input_client()->AsWeakPtr();
  ASSERT_TRUE(window_->Select("personal"));ASSERT_TRUE(base::test::RunUntil([&]{return !manager->dialog()&&!lifetime;}));EXPECT_EQ(true,content::EvalJs(page,"answer===null"));EXPECT_EQ("undefined",content::EvalJs(window_->contents("personal"),"typeof answer"));
  ASSERT_TRUE(window_->Select("work"));
  ASSERT_TRUE(content::ExecJs(page,"const child=document.createElement('iframe');child.name='prompt-child';document.body.appendChild(child);"));
  auto* frame=content::ChildFrameAt(page,0);ASSERT_TRUE(frame);int count=0;bool accepted=true;bool suppressed=true;
  base::ScopedClosureRunner cleanup(base::BindOnce([](PageDialogs* manager,content::WebContents* page){manager->CancelDialogs(page,true);base::RunLoop().RunUntilIdle();},base::Unretained(manager),base::Unretained(page)));
  manager->RunJavaScriptDialog(page,frame,content::JAVASCRIPT_DIALOG_TYPE_PROMPT,u"Frame-owned prompt",u"frame draft",
      base::BindOnce([](int* count,bool* accepted,bool ok,const std::u16string& text){++*count;*accepted=ok;EXPECT_TRUE(text.empty());},base::Unretained(&count),base::Unretained(&accepted)),&suppressed);
  EXPECT_FALSE(suppressed);ASSERT_TRUE(manager->dialog());lifetime=manager->prompt_input()->input_client()->AsWeakPtr();
  EXPECT_NE(std::u16string::npos,manager->dialog()->widget_delegate()->GetWindowTitle().find(base::UTF8ToUTF16(frame->GetLastCommittedOrigin().Serialize())));
  content::NavigationController::LoadURLParams params(embedded_test_server()->GetURL("/storage_probe.html?new-child-document"));params.frame_tree_node_id=frame->GetFrameTreeNodeId();page->GetController().LoadURLWithParams(params);
  ASSERT_TRUE(content::WaitForLoadStop(page));ASSERT_TRUE(base::test::RunUntil([&]{return count==1&&!manager->dialog()&&!lifetime;}));EXPECT_FALSE(accepted);
  manager->CancelDialogs(page,true);base::RunLoop().RunUntilIdle();EXPECT_EQ(1,count);
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, PageDialogsCocoaBeforeUnloadDefaultCancel) {
  ASSERT_NO_FATAL_FAILURE(NavigateBoth());ASSERT_TRUE(window_->Select("work"));auto* page=window_->contents("work");
  ASSERT_TRUE(window_->NavigateFromBar(embedded_test_server()->GetURL("/storage_probe.html?other-tab"),true));ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));window_->SelectTab(0);ASSERT_EQ(page,window_->contents("work"));
  auto* manager=PageDialogs::FromWebContents(page);ASSERT_TRUE(manager);ui_controls::EnableUIControls();
  for(auto code:{ui::VKEY_RETURN,ui::VKEY_ESCAPE}){
    SCOPED_TRACE(static_cast<int>(code));
    ASSERT_NO_FATAL_FAILURE(ArmUnsaved(page));window_->CloseTab(0);ASSERT_TRUE(base::test::RunUntil([&]{return manager->dialog()!=nullptr;}));
    auto* dialog=manager->dialog();auto* delegate=dialog->widget_delegate()->AsDialogDelegate();ASSERT_EQ(static_cast<int>(ui::mojom::DialogButton::kCancel),delegate->GetDefaultDialogButton());EXPECT_FALSE(manager->prompt_input());
    EXPECT_EQ(views::PlatformStyle::kDialogDefaultButtonCanBeCancel,delegate->GetIsDefault(ui::mojom::DialogButton::kCancel));
    ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(dialog->GetNativeWindow()));ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(dialog->GetNativeWindow(),code,false,false,false,false));
    if(code==ui::VKEY_RETURN&&!views::PlatformStyle::kDialogDefaultButtonCanBeCancel){
      // macOS intentionally leaves a Cancel-default dialog open on Return.
      base::RunLoop().RunUntilIdle();ASSERT_EQ(dialog,manager->dialog());EXPECT_TRUE(window_->lifecycle_pending());EXPECT_EQ(2u,window_->tab_count("work"));
      ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(dialog->GetNativeWindow(),ui::VKEY_ESCAPE,false,false,false,false));
    }
    ASSERT_TRUE(base::test::RunUntil([&]{return !window_->lifecycle_pending();}));ASSERT_EQ(2u,window_->tab_count("work"));
    EXPECT_FALSE(manager->dialog());EXPECT_EQ(page,window_->contents("work"));EXPECT_EQ("draft",content::EvalJs(page,"unsaved"));
  }
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, BeforeUnloadCloseAndAtomicRestore) {
  const auto url = embedded_test_server()->GetURL("/storage_probe.html?unsaved");
  auto* first = window_->contents("work");
  ASSERT_TRUE(content::NavigateToURL(first, url));
  ASSERT_TRUE(window_->SaveSnapshot("Before dirty pages"));
  const auto snapshot = window_->TimeMachine().snapshots.front().id;
  ASSERT_TRUE(window_->NavigateFromBar(url, true));
  auto* second = window_->contents("work");
  ASSERT_TRUE(content::WaitForLoadStop(second));
  ArmUnsaved(second);
  window_->CloseTab(1);
  EXPECT_TRUE(window_->lifecycle_pending());
  AnswerUnload(second, false);
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->lifecycle_pending(); }));
  EXPECT_EQ(2u, window_->tab_count("work"));
  EXPECT_EQ("draft", content::EvalJs(second, "unsaved"));
  window_->SelectTab(0); ArmUnsaved(first); window_->SelectTab(1);
  EXPECT_TRUE(window_->RestoreSnapshot(snapshot));
  AnswerUnload(first, true);
  AnswerUnload(second, false);
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->lifecycle_pending(); }));
  EXPECT_EQ(2u, window_->tab_count("work"));
  EXPECT_EQ(second, window_->contents("work"));
  EXPECT_EQ("draft", content::EvalJs(first, "unsaved"));
  EXPECT_EQ("draft", content::EvalJs(second, "unsaved"));
  window_->CloseTab(1); AnswerUnload(second, true);
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->tab_count("work") == 1; }));
  EXPECT_FALSE(window_->lifecycle_pending());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, BeforeUnloadRechecksNewDirtyPages) {
  const auto url = embedded_test_server()->GetURL("/storage_probe.html?new-handler");
  auto* first = window_->contents("work");
  ASSERT_TRUE(content::NavigateToURL(first, url));
  content::SimulateMouseClickAt(first, 0, blink::WebMouseEvent::Button::kLeft, gfx::Point(12, 12));
  ASSERT_TRUE(window_->SaveSnapshot("Clean original"));
  const auto snapshot = window_->TimeMachine().snapshots.front().id;
  ASSERT_TRUE(window_->NavigateFromBar(url, true));
  auto* second = window_->contents("work");
  ASSERT_TRUE(content::WaitForLoadStop(second)); ArmUnsaved(second);
  EXPECT_TRUE(window_->RestoreSnapshot(snapshot));
  auto* second_dialogs = PageDialogs::FromWebContents(second);
  ASSERT_TRUE(base::test::RunUntil([&] { return second_dialogs->dialog() != nullptr; }));
  // First page was clean when preflight skipped it. Its script now becomes dirty.
  ASSERT_TRUE(content::ExecJs(first, "window.onbeforeunload=e=>{ e.preventDefault(); e.returnValue=''; };"));
  ASSERT_TRUE(base::test::RunUntil([&] { return first->NeedToFireBeforeUnload(); }));
  AnswerUnload(second, true); AnswerUnload(first, false);
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->lifecycle_pending(); }));
  EXPECT_EQ(2u, window_->tab_count("work"));
  EXPECT_EQ(second, window_->contents("work"));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, BeforeUnloadProfileSleepAndWindowClose) {
  auto* page = window_->contents("work");
  ASSERT_TRUE(content::NavigateToURL(page, embedded_test_server()->GetURL("/storage_probe.html?guard")));
  ArmUnsaved(page);
  EXPECT_TRUE(window_->AssignProfile("work", "personal-profile"));
  AnswerUnload(page, false);
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->lifecycle_pending(); }));
  EXPECT_EQ(work_.get(), window_->contents("work")->GetBrowserContext());
  EXPECT_TRUE(window_->SleepWorkspace("work"));
  AnswerUnload(page, false);
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->lifecycle_pending(); }));
  EXPECT_EQ("work", window_->active_id()); EXPECT_FALSE(window_->workspace_sleeping("work"));
  int closed = 0;
  window_->SetCloseCallback(base::BindOnce([](int* count) { ++*count; }, &closed));
  window_->widget()->Close(); AnswerUnload(page, false);
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->lifecycle_pending(); }));
  EXPECT_EQ(0, closed); EXPECT_TRUE(window_->widget()->IsVisible());
  window_->widget()->Close(); AnswerUnload(page, true);
  ASSERT_TRUE(base::test::RunUntil([&] { return closed == 1; }));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, BeforeUnloadPeekAndBackgroundBatch) {
  const auto url = embedded_test_server()->GetURL("/storage_probe.html?peek-draft");
  ASSERT_TRUE(window_->OpenPeek(url));
  auto* peek = window_->peek_contents();
  ASSERT_TRUE(content::WaitForLoadStop(peek)); ArmUnsaved(peek);
  EXPECT_FALSE(window_->Select("personal"));
  AnswerUnload(peek, false);
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->lifecycle_pending(); }));
  EXPECT_EQ("work", window_->active_id()); EXPECT_EQ(peek, window_->peek_contents());
  window_->ClosePeek(); AnswerUnload(peek, true);
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->peek_contents(); }));
  auto* work = window_->contents("work");
  ASSERT_TRUE(content::NavigateToURL(work, url)); ArmUnsaved(work);
  ASSERT_TRUE(window_->AddWorkspace("Second draft", "work-profile"));
  const auto extra = window_->active_id(); auto* other = window_->contents(extra);
  ASSERT_TRUE(content::NavigateToURL(other, url)); ArmUnsaved(other);
  ASSERT_TRUE(window_->Select("personal"));
  const auto commands = window_->CommandResults(u"> sleep other");
  auto sleep = std::find_if(commands.begin(), commands.end(), [](const auto& result) { return result.command_id == "sleep_others"; });
  ASSERT_NE(sleep, commands.end()); EXPECT_TRUE(window_->ExecuteCommand(*sleep));
  AnswerUnload(work, true); AnswerUnload(other, false);
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->lifecycle_pending(); }));
  EXPECT_FALSE(window_->workspace_sleeping("work")); EXPECT_FALSE(window_->workspace_sleeping(extra));
  EXPECT_EQ(work, window_->contents("work")); EXPECT_EQ(other, window_->contents(extra));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, BeforeUnloadRoutedPeekIsPartOfDiscardPlan) {
  const auto url = embedded_test_server()->GetURL("/storage_probe.html?routed-preview");
  ASSERT_TRUE(window_->SaveSnapshot("Original workspace"));
  const auto snapshot = window_->TimeMachine().snapshots.front().id;
  ASSERT_TRUE(window_->SetLinkRule(url, "personal"));
  ASSERT_TRUE(window_->OpenPeek(url));
  auto* peek = window_->peek_contents();
  ASSERT_TRUE(content::WaitForLoadStop(peek));
  EXPECT_EQ(personal_.get(), peek->GetBrowserContext());
  ArmUnsaved(peek);
  EXPECT_TRUE(window_->RestoreSnapshot(snapshot)); AnswerUnload(peek, false);
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->lifecycle_pending(); }));
  EXPECT_EQ(peek, window_->peek_contents()); EXPECT_EQ("work", window_->active_id());
  EXPECT_TRUE(window_->SleepWorkspace("work")); AnswerUnload(peek, false);
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->lifecycle_pending(); }));
  EXPECT_FALSE(window_->workspace_sleeping("work")); EXPECT_EQ(peek, window_->peek_contents());
  EXPECT_TRUE(window_->RemoveWorkspace()); AnswerUnload(peek, false);
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->lifecycle_pending(); }));
  EXPECT_EQ(2u, window_->workspaces().size()); EXPECT_EQ(peek, window_->peek_contents());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, CommandCenterSearchAndDispatch) {
  auto* shell = window_->shell();
  const auto url = embedded_test_server()->GetURL("/storage_probe.html?command-center");
  ASSERT_TRUE(content::NavigateToURL(window_->contents("work"), url));
  window_->OpenCommandPalette();
  EXPECT_FALSE(shell->suggestions().empty());
  SetPaletteQuery(u"> skroty");  // Polish accents are optional.
  ASSERT_EQ(1u, shell->suggestions().size());
  EXPECT_EQ("shortcuts", shell->suggestions().front().command_id);
  ui::KeyEvent enter(ui::EventType::kKeyPressed, ui::VKEY_RETURN, 0);
  EXPECT_TRUE(shell->address_input()->view()->OnKeyPressed(enter));
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->tools_widget()->IsVisible(); }));
  EXPECT_FALSE(shell->address_visible());
  shell->CloseOverlays();
  window_->OpenCommandPalette(); SetPaletteQuery(u"> zapisz snapshot");
  ASSERT_EQ(1u, shell->suggestions().size());
  const auto before = window_->TimeMachine().snapshots.size();
  EXPECT_TRUE(shell->address_input()->view()->OnKeyPressed(enter));
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->TimeMachine().snapshots.size() > before; }));
  EXPECT_EQ(u"Command Center", window_->TimeMachine().snapshots.front().title);
  shell->CloseOverlays(); window_->OpenCommandPalette(); SetPaletteQuery(u"> no-such-command-xyz");
  EXPECT_TRUE(shell->suggestions().empty());
  EXPECT_TRUE(shell->address_input()->view()->OnKeyPressed(enter));
  EXPECT_TRUE(shell->address_visible());
  EXPECT_EQ(url, window_->contents("work")->GetVisibleURL());
  SetPaletteQuery(u"g ustawienia");
  ASSERT_FALSE(shell->suggestions().empty());
  EXPECT_TRUE(shell->suggestions().front().command_id.empty());
  EXPECT_EQ("www.google.com", shell->suggestions().front().url.host());
  shell->OpenAddress(false); SetPaletteQuery(u"skroty");
  for (const auto& item : shell->suggestions()) EXPECT_TRUE(item.command_id.empty());
  shell->CloseOverlays();
  const auto stale = window_->CommandResults(u"> reload");
  ASSERT_EQ(1u, stale.size());
  ASSERT_TRUE(window_->NavigateFromBar(url, true));
  ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));
  EXPECT_FALSE(window_->ExecuteCommand(stale.front()));
  window_->CloseTab(0);  // Replacement now has the same index and URL, but a different page identity.
  ASSERT_EQ(1u, window_->tab_count("work"));
  EXPECT_FALSE(window_->ExecuteCommand(stale.front()));
  const auto actions = window_->CommandResults(u">");
  auto settings = std::find_if(actions.begin(), actions.end(), [](const auto& item) { return item.command_id == "settings"; });
  ASSERT_NE(settings, actions.end());
  ASSERT_TRUE(window_->Select("personal"));
  EXPECT_FALSE(window_->ExecuteCommand(*settings));  // Queued action cannot cross workspaces.
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, CommandCenterMoveCopyAndSleep) {
  ASSERT_TRUE(window_->AddWorkspace("Projekt", "work-profile"));
  const auto target = window_->active_id();
  ASSERT_TRUE(window_->Select("work"));
  ASSERT_TRUE(window_->NavigateFromBar(embedded_test_server()->GetURL("/storage_probe.html?moving"), true));
  auto* moving = window_->contents("work");
  ASSERT_TRUE(content::WaitForLoadStop(moving));
  ASSERT_TRUE(content::ExecJs(moving, "window.commandCenterSentinel = 'live'; document.body.innerHTML += '<input id=cc value=unfinished>';"));
  ASSERT_TRUE(window_->SleepWorkspace(target));
  auto find = [&](const std::string& id, const std::string& destination) {
    const auto actions = window_->CommandResults(u">");
    auto match = std::find_if(actions.begin(), actions.end(), [&](const auto& item) {
      return item.command_id == id && item.target_workspace_id == destination;
    });
    EXPECT_NE(match, actions.end());
    return match == actions.end() ? PaletteResult() : *match;
  };
  const auto move = find("move_tab", target);
  EXPECT_TRUE(window_->ExecuteCommand(move));
  EXPECT_EQ(target, window_->active_id());
  EXPECT_EQ(moving, window_->contents(target));
  EXPECT_EQ("live", content::EvalJs(moving, "window.commandCenterSentinel"));
  EXPECT_EQ("unfinished", content::EvalJs(moving, "document.querySelector('#cc').value"));
  EXPECT_EQ(1u, window_->tab_count("work"));
  EXPECT_FALSE(window_->workspace_sleeping(target));
  EXPECT_FALSE(window_->MoveActiveTab("personal"));  // Immutable BrowserContext.
  const auto copy = find("copy_tab", "personal");
  const auto original_url = moving->GetVisibleURL();
  EXPECT_TRUE(window_->ExecuteCommand(copy));
  EXPECT_EQ("personal", window_->active_id());
  auto* copied = window_->contents("personal");
  EXPECT_NE(moving, copied);
  EXPECT_EQ(personal_.get(), copied->GetBrowserContext());
  ASSERT_TRUE(content::WaitForLoadStop(copied));
  EXPECT_EQ(original_url, copied->GetVisibleURL());
  EXPECT_EQ("undefined", content::EvalJs(copied, "typeof window.commandCenterSentinel"));
  const auto sleep = find("sleep_others", "");
  EXPECT_TRUE(window_->ExecuteCommand(sleep));
  EXPECT_EQ("personal", window_->active_id());
  EXPECT_TRUE(window_->workspace_sleeping(target));
  EXPECT_TRUE(window_->workspace_sleeping("work"));
  const auto wake = find("select_space", target);
  EXPECT_TRUE(window_->ExecuteCommand(wake));
  EXPECT_EQ(target, window_->active_id());
  EXPECT_FALSE(window_->workspace_sleeping(target));
  ASSERT_TRUE(content::WaitForLoadStop(window_->contents(target)));
  EXPECT_EQ(original_url, window_->contents(target)->GetVisibleURL());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, CommandCenterMoveSplitAndLastPage) {
  ASSERT_TRUE(window_->AddWorkspace("Split destination", "work-profile"));
  const auto target = window_->active_id();
  window_->ToggleSplit();
  ASSERT_TRUE(window_->split());
  auto* target_left = window_->pane_contents(false);
  ASSERT_TRUE(window_->Select("work"));
  const auto url = embedded_test_server()->GetURL("/storage_probe.html?split-move");
  ASSERT_TRUE(content::NavigateToURL(window_->contents("work"), url));
  auto* original = window_->contents("work");
  window_->ToggleSplit();
  ASSERT_TRUE(window_->split());
  auto* survivor = window_->pane_contents(true);
  window_->FocusPane(false);
  ASSERT_TRUE(window_->MoveActiveTab(target));
  EXPECT_TRUE(window_->split());
  EXPECT_EQ(original, window_->pane_contents(true));
  EXPECT_EQ(target_left, window_->pane_contents(false));
  EXPECT_EQ(survivor, window_->contents("work"));
  ASSERT_TRUE(window_->Select("work"));
  EXPECT_FALSE(window_->split());
  ASSERT_EQ(1u, window_->tab_count("work"));
  ASSERT_TRUE(window_->MoveActiveTab(target));
  EXPECT_EQ(survivor, window_->contents(target));
  EXPECT_EQ(1u, window_->tab_count("work"));
  EXPECT_NE(survivor, window_->contents("work"));
  EXPECT_EQ(work_.get(), window_->contents("work")->GetBrowserContext());
  EXPECT_TRUE(window_->contents("work")->GetVisibleURL().is_empty());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, ChromiumHistoryAndCommandPalette) {
  const auto work_url = embedded_test_server()->GetURL("/storage_probe.html?orion-history");
  ASSERT_TRUE(content::NavigateToURL(window_->contents("work"), work_url));
  ASSERT_TRUE(content::ExecJs(window_->contents("work"), "document.title = 'Orion firmowy projekt'"));
  // Close the visited tab's URL so that it can only return as a history result.
  ASSERT_TRUE(content::NavigateToURL(window_->contents("work"), embedded_test_server()->GetURL("/storage_probe.html?current")));
  auto* service = HistoryServiceFactory::GetForProfile(work_, ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(service);
  base::CancelableTaskTracker tasks;
  history::QueryOptions options; options.max_count = 20;
  base::test::TestFuture<history::QueryResults> queried;
  service->QueryHistory(u"orion", options, queried.GetCallback(), &tasks);
  const auto rows = queried.Take();
  ASSERT_EQ(1u, rows.size());
  EXPECT_EQ(work_url, rows[0].url());
  EXPECT_EQ(u"Orion firmowy projekt", rows[0].title());
  window_->OpenCommandPalette();
  SetPaletteQuery(u"orion");
  ASSERT_TRUE(base::test::RunUntil([&] {
    const auto& results = window_->shell()->search_results();
    return std::any_of(results.begin(), results.end(), [&](const auto& result) {
      return result.url == work_url && result.detail.starts_with(u"Historia");
    });
  }));
  ui::KeyEvent enter(ui::EventType::kKeyPressed, ui::VKEY_RETURN, 0);
  window_->shell()->address_input()->view()->OnKeyPressed(enter);
  ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));
  EXPECT_EQ(work_url, window_->contents("work")->GetLastCommittedURL());
  ASSERT_TRUE(content::NavigateToURL(window_->contents("work"), embedded_test_server()->GetURL("/storage_probe.html?current")));
  ASSERT_TRUE(window_->Select("personal"));
  window_->OpenCommandPalette();
  SetPaletteQuery(u"orion");
  auto* private_service = HistoryServiceFactory::GetForProfile(personal_, ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(private_service);
  base::test::TestFuture<history::QueryResults> private_queried;
  private_service->QueryHistory(u"orion", options, private_queried.GetCallback(), &tasks);
  EXPECT_TRUE(private_queried.Take().empty());
  base::RunLoop().RunUntilIdle();
  for (const auto& result : window_->shell()->search_results()) EXPECT_FALSE(result.detail.starts_with(u"Historia"));
  // Profile policy also applies to the adapter, without special test flags.
  work_->GetPrefs()->SetBoolean(prefs::kSavingBrowserHistoryDisabled, true);
  ASSERT_TRUE(content::NavigateToURL(window_->contents("work"), embedded_test_server()->GetURL("/storage_probe.html?blocked-recording")));
  base::test::TestFuture<history::QueryResults> blocked;
  service->QueryHistory(u"blocked-recording", options, blocked.GetCallback(), &tasks);
  EXPECT_TRUE(blocked.Take().empty());
  work_->GetPrefs()->SetBoolean(prefs::kSavingBrowserHistoryDisabled, false);
  ASSERT_TRUE(window_->Select("work"));
  ASSERT_TRUE(content::NavigateToURL(window_->contents("work"), work_url));
  ASSERT_TRUE(content::ExecJs(window_->contents("work"), "document.title = 'Otwarta karta Orion'"));
  ASSERT_TRUE(window_->Select("personal"));
  window_->OpenCommandPalette(); SetPaletteQuery(u"Orion");
  ASSERT_TRUE(base::test::RunUntil([&] {
    const auto& results = window_->shell()->search_results();
    return std::any_of(results.begin(), results.end(), [](const auto& result) { return result.tab && result.workspace_id == "work"; });
  }));
  const auto results = window_->shell()->search_results();
  auto target = std::find_if(results.begin(), results.end(), [](const auto& result) { return result.tab && result.workspace_id == "work"; });
  ASSERT_NE(target, results.end());
  window_->ActivatePaletteResult(*target);
  EXPECT_EQ("work", window_->active_id());
  EXPECT_EQ(work_url, window_->contents("work")->GetVisibleURL());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, ChromiumPasswordSaveAndFill) {
  auto store = ProfilePasswordStoreFactory::GetForProfile(work_, ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(store);
  const auto url = embedded_test_server()->GetURL("/login_probe.html");
  bool has_saved_account = false;
  ui_controls::EnableUIControls();
  auto activate_password=[&](PageServices* services,bool accept,ui::KeyboardCode key){
    auto* widget=services->password_prompt();ASSERT_TRUE(widget);auto* button=DialogActionButton(widget,accept);ASSERT_TRUE(button);ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(widget->GetNativeWindow()));button->RequestFocus();
    base::test::TestFuture<void> presented;auto* compositor=widget->GetCompositor();ASSERT_TRUE(compositor);widget->GetRootView()->SchedulePaint();compositor->RequestSuccessfulPresentationTimeForNextFrame(base::BindOnce([](base::OnceClosure done,const viz::FrameTimingDetails&){std::move(done).Run();},presented.GetCallback()));compositor->ScheduleFullRedraw();ASSERT_TRUE(presented.Wait());ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(widget->GetNativeWindow(),key,false,false,false,false));ASSERT_TRUE(base::test::RunUntil([&]{return !services->password_prompt();}));
  };
  auto first_gesture = [&](content::WebContents* page) {
    content::SimulateEndOfPaintHoldingOnPrimaryMainFrame(page);
    content::InputEventAckWaiter ack(page->GetPrimaryMainFrame()->GetRenderWidgetHost(),
        blink::WebInputEvent::Type::kMouseUp);
    content::SimulateMouseClickAt(page, 0, blink::WebMouseEvent::Button::kLeft, gfx::Point(12, 12));
    ack.Wait();
  };
  auto submit = [&](std::u16string username = u"alice", std::u16string password = u"synthetic-47") {
    auto* page = window_->contents("work");
    if (has_saved_account) {
      ASSERT_TRUE(base::test::RunUntil([&] {
        return !content::EvalJs(page, "document.getElementById('username').value").ExtractString().empty();
      }));
    }
    // Commit Chromium's suggested values before replacing fields. Otherwise
    // the first typed character can accept an outstanding autofill suggestion.
    first_gesture(page);
    EXPECT_TRUE(content::ExecJs(page, "document.getElementById('username').value = ''; document.getElementById('username').focus()"));
    for (char16_t letter : username) TypePageCharacter(page, letter);
    EXPECT_TRUE(content::ExecJs(page, "document.getElementById('password').value = ''; document.getElementById('password').focus()"));
    for (char16_t letter : password) TypePageCharacter(page, letter);
    ASSERT_EQ(base::UTF16ToUTF8(username), content::EvalJs(page, "document.getElementById('username').value"));
    ASSERT_EQ(base::UTF16ToUTF8(password), content::EvalJs(page, "document.getElementById('password').value"));
    EXPECT_TRUE(content::ExecJs(page, "document.getElementById('login').requestSubmit()"));
  };
  ASSERT_TRUE(content::NavigateToURL(window_->contents("work"), url));
  auto* services = PageServices::FromWebContents(window_->contents("work"));
  ASSERT_TRUE(services);
  submit();
  ASSERT_TRUE(base::test::RunUntil([&] { return services->password_prompt() && services->password_prompt()->IsVisible(); }));
  if(window_->shell()->workspace_input()->rust_editor()){
    ASSERT_TRUE(base::test::RunUntil([&]{return services->password_scene_visible_for_testing()&&services->password_scene_contains_for_testing("Użytkownik: alice");}));
    EXPECT_FALSE(services->password_scene_contains_for_testing("synthetic-47"));auto* button=DialogActionButton(services->password_prompt(),true);ASSERT_TRUE(button);EXPECT_TRUE(button->children().empty());EXPECT_FALSE(views::AsViewClass<views::MdTextButton>(button));
  }
  activate_password(services,false,ui::VKEY_ESCAPE);
  PasswordReader declined;
  store->GetAllLogins(declined.weak_factory.GetWeakPtr());
  auto declined_rows = declined.ready.Take();
  ASSERT_TRUE(declined_rows.has_value()); EXPECT_TRUE(declined_rows->empty());
  ASSERT_TRUE(content::NavigateToURL(window_->contents("work"), url));
  submit();
  ASSERT_TRUE(base::test::RunUntil([&] { return services->password_prompt() && services->password_prompt()->IsVisible(); }));
  activate_password(services,true,ui::VKEY_SPACE);
  PasswordReader saved;
  store->GetAllLogins(saved.weak_factory.GetWeakPtr());
  auto rows = saved.ready.Take();
  ASSERT_TRUE(rows.has_value()); ASSERT_EQ(1u, rows->size());
  EXPECT_EQ(u"alice", (*rows)[0].username_value);
  has_saved_account = true;
  ASSERT_TRUE(content::NavigateToURL(window_->contents("work"), url));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(window_->contents("work"), "document.getElementById('username').value").ExtractString() == "alice";
  }));
  content::SimulateMouseClickAt(window_->contents("work"), 0, blink::WebMouseEvent::Button::kLeft, gfx::Point(12, 12));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(window_->contents("work"), "document.getElementById('password').value").ExtractString() == "synthetic-47";
  }));
  // Native account overview exposes metadata, never the password value.
  services->ShowPasswords();
  ASSERT_TRUE(base::test::RunUntil([&] { return services->password_prompt() && services->password_prompt()->IsVisible(); }));
  EXPECT_EQ(u"Zapisane logowania", services->password_prompt()->widget_delegate()->GetWindowTitle());
  services->password_prompt()->widget_delegate()->AsDialogDelegate()->AcceptDialog();
  submit(u"alice", u"updated-synthetic-89");
  ASSERT_TRUE(base::test::RunUntil([&] { return services->password_prompt() && services->password_prompt()->IsVisible(); }));
  EXPECT_EQ(u"Zaktualizować hasło?", services->password_prompt()->widget_delegate()->GetWindowTitle());
  activate_password(services,true,ui::VKEY_RETURN);
  PasswordReader updated;
  store->GetAllLogins(updated.weak_factory.GetWeakPtr());
  auto updated_rows = updated.ready.Take();
  ASSERT_TRUE(updated_rows.has_value()); ASSERT_EQ(1u, updated_rows->size());
  EXPECT_EQ(u"updated-synthetic-89", (*updated_rows)[0].password_value.value());
  ASSERT_TRUE(content::NavigateToURL(window_->contents("work"), url));
  submit(u"bob", u"different-synthetic-03");
  ASSERT_TRUE(base::test::RunUntil([&] { return services->password_prompt() && services->password_prompt()->IsVisible(); }));
  ASSERT_TRUE(window_->Select("personal"));
  ASSERT_TRUE(base::test::RunUntil([&] { return !services->password_prompt(); }));
  PasswordReader hidden;
  store->GetAllLogins(hidden.weak_factory.GetWeakPtr());
  auto hidden_rows = hidden.ready.Take();
  ASSERT_TRUE(hidden_rows.has_value()); EXPECT_EQ(1u, hidden_rows->size());
  ASSERT_TRUE(content::NavigateToURL(window_->contents("personal"), url));
  EXPECT_EQ("", content::EvalJs(window_->contents("personal"), "document.getElementById('username').value"));
  auto* autofill_client = autofill::ContentAutofillClient::FromWebContents(window_->contents("personal"));
  ASSERT_TRUE(autofill_client);
  EXPECT_TRUE(autofill_client->IsAutocompleteEnabled());
  EXPECT_FALSE(autofill_client->IsAutofillProfileEnabled());
  EXPECT_FALSE(autofill_client->GetPaymentsAutofillClient()->IsAutofillPaymentMethodsEnabled());
  PasswordReader isolated;
  auto private_store = ProfilePasswordStoreFactory::GetForProfile(personal_, ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(private_store);
  private_store->GetAllLogins(isolated.weak_factory.GetWeakPtr());
  auto private_rows = isolated.ready.Take(); ASSERT_TRUE(private_rows.has_value()); EXPECT_TRUE(private_rows->empty());
  // Two saved accounts must reach Chromium's actual suggestion UI in our host.
  ASSERT_TRUE(window_->Select("work"));
  ASSERT_TRUE(content::NavigateToURL(window_->contents("work"), url));
  submit(u"bob", u"different-synthetic-03");
  ASSERT_TRUE(base::test::RunUntil([&] { return services->password_prompt() && services->password_prompt()->IsVisible(); }));
  services->password_prompt()->widget_delegate()->AsDialogDelegate()->AcceptDialog();
  PasswordReader both;
  store->GetAllLogins(both.weak_factory.GetWeakPtr());
  auto both_rows = both.ready.Take(); ASSERT_TRUE(both_rows.has_value()); ASSERT_EQ(2u, both_rows->size());
  ASSERT_TRUE(content::NavigateToURL(window_->contents("work"), url));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !content::EvalJs(window_->contents("work"), "document.getElementById('username').value").ExtractString().empty();
  }));
  base::test::ScopedRunLoopTimeout suggestion_timeout(FROM_HERE, base::Seconds(15));
  ui_controls::EnableUIControls();
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(window_->widget()->GetNativeWindow()));
  window_->contents("work")->Focus();
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->contents("work")->GetRenderWidgetHostView()->HasFocus(); }));
  first_gesture(window_->contents("work"));
  ASSERT_TRUE(content::ExecJs(window_->contents("work"), "document.getElementById('username').value = ''"));
  content::InputEventAckWaiter clicked(window_->contents("work")->GetPrimaryMainFrame()->GetRenderWidgetHost(),
      blink::WebInputEvent::Type::kMouseUp);
  content::SimulateMouseClickAt(window_->contents("work"), 0, blink::WebMouseEvent::Button::kLeft,
      gfx::ToRoundedPoint(content::GetCenterCoordinatesOfElementWithId(window_->contents("work"), "username")));
  clicked.Wait();
  EXPECT_EQ("username", content::EvalJs(window_->contents("work"), "document.activeElement.id"));
  EXPECT_EQ("", content::EvalJs(window_->contents("work"), "document.getElementById('username').value"));
  content::SimulateKeyPressWithoutChar(window_->contents("work"), ui::DomKey::ARROW_DOWN,
      ui::DomCode::ARROW_DOWN, ui::VKEY_DOWN, false, false, false, false);
  auto* work_autofill = autofill::ContentAutofillClient::FromWebContents(window_->contents("work"));
  std::u16string shown_accounts;
  ASSERT_TRUE(base::test::RunUntil([&] {
    const auto suggestions = work_autofill->GetAutofillSuggestions();
    shown_accounts.clear();
    bool alice = false, bob = false;
    for (const auto& suggestion : suggestions) {
      shown_accounts += suggestion.main_text.value + u" | ";
      alice |= suggestion.main_text.value == u"alice";
      bob |= suggestion.main_text.value == u"bob";
    }
    return alice && bob;
  })) << "Suggestion titles: " << base::UTF16ToUTF8(shown_accounts)
      << "; field: " << content::EvalJs(window_->contents("work"), "document.getElementById('username').value");
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, SplitPairPersistsWhileAnotherTabIsShown) {
  ASSERT_TRUE(content::NavigateToURL(window_->contents("work"), embedded_test_server()->GetURL("/storage_probe.html?a")));
  auto* a = window_->contents("work");
  ASSERT_TRUE(window_->NavigateFromBar(embedded_test_server()->GetURL("/storage_probe.html?b"), true));
  auto* b = window_->contents("work");
  ASSERT_TRUE(window_->NavigateFromBar(embedded_test_server()->GetURL("/storage_probe.html?c"), true));
  auto* c = window_->contents("work");
  ASSERT_TRUE(content::WaitForLoadStop(c));
  window_->SelectTab(0);
  ASSERT_TRUE(window_->SplitTab(1));
  ASSERT_TRUE(window_->split());
  auto* shell = window_->shell();
  // Another tab is shown alone; the pair stays one sidebar item.
  window_->SelectTab(2);
  EXPECT_FALSE(window_->split());
  EXPECT_EQ(c, window_->pane_contents(false));
  EXPECT_EQ(nullptr, window_->pane_contents(true));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(shell->tab_button(0) && shell->tab_button(1));
  EXPECT_EQ(shell->tab_button(0)->parent(), shell->tab_button(1)->parent());
  EXPECT_NE(shell->tab_button(0)->parent(), shell->tab_button(2)->parent());
  // Selecting either half brings the whole pair back.
  window_->SelectTab(1);
  EXPECT_TRUE(window_->split());
  EXPECT_EQ(a, window_->pane_contents(false));
  EXPECT_EQ(b, window_->pane_contents(true));
  EXPECT_EQ(b, window_->contents("work"));
  // Move left/right swaps the halves; vertical stacks them.
  window_->SwapSplit();
  EXPECT_EQ(b, window_->pane_contents(false));
  EXPECT_EQ(a, window_->pane_contents(true));
  window_->SetSplitVertical(true);
  EXPECT_TRUE(window_->split_vertical());
  window_->widget()->LayoutRootViewIfNecessary();
  const auto top = b->GetContainerBounds(), bottom = a->GetContainerBounds();
  EXPECT_EQ(top.x(), bottom.x());
  EXPECT_LT(top.bottom(), bottom.y() + 1);
  EXPECT_GT(top.height(), 100);
  // Closing half of a hidden pair keeps the solo tab on screen.
  window_->SelectTab(2);
  window_->CloseTab(0);
  EXPECT_EQ(c, window_->contents("work"));
  EXPECT_FALSE(window_->split());
  EXPECT_EQ(2u, window_->tab_count("work"));
  window_->SelectTab(0);
  EXPECT_FALSE(window_->split());
  EXPECT_EQ(b, window_->contents("work"));
  // Each visible half has its own header with a close button.
  ASSERT_TRUE(window_->SplitTab(1));
  window_->widget()->LayoutRootViewIfNecessary();
  auto* close = shell->split_pane_button(true, true);
  ASSERT_TRUE(close->IsDrawn());
  // The header sits above the right page, not over it.
  EXPECT_LE(close->GetBoundsInScreen().bottom(), c->GetContainerBounds().y());
  EXPECT_GT(close->GetBoundsInScreen().x(), shell->split_pane_button(false, true)->GetBoundsInScreen().x());
  ui::AXActionData activate;activate.action=ax::mojom::Action::kDoDefault;
  EXPECT_TRUE(close->HandleAccessibleAction(activate));
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->tab_count("work") == 1; }));
  EXPECT_EQ(b, window_->contents("work"));
  EXPECT_FALSE(window_->split());
  EXPECT_FALSE(shell->split_pane_button(false, true)->IsDrawn());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, SplitViewLifecycleAndIsolation) {
  const auto first = embedded_test_server()->GetURL("/storage_probe.html");
  const auto second = embedded_test_server()->GetURL("/storage_probe.html?right");
  ASSERT_TRUE(content::NavigateToURL(window_->contents("work"), first));
  auto* left = window_->contents("work");
  ASSERT_TRUE(window_->NavigateFromBar(second, true));
  auto* right = window_->contents("work");
  ASSERT_TRUE(content::WaitForLoadStop(right));
  window_->SelectTab(0);
  ASSERT_TRUE(window_->SplitTab(1));
  EXPECT_TRUE(window_->split());
  EXPECT_EQ(left, window_->pane_contents(false));
  EXPECT_EQ(right, window_->pane_contents(true));
  EXPECT_EQ(work_, right->GetBrowserContext());
  EXPECT_EQ(work_, left->GetBrowserContext());
  auto* shell = window_->shell();
  shell->ShowSidebar();
  base::RunLoop().RunUntilIdle();
  shell->sidebar_widget()->LayoutRootViewIfNecessary();
  auto* left_row = shell->tab_button(0);
  auto* right_row = shell->tab_button(1);
  ASSERT_TRUE(left_row);
  ASSERT_TRUE(right_row);
  EXPECT_EQ(left_row->parent(), right_row->parent());
  EXPECT_LE(left_row->parent()->width(), shell->sidebar_widget()->GetContentsView()->width());
  EXPECT_TRUE(left_row->parent()->GetLocalBounds().Contains(right_row->bounds()));
  EXPECT_EQ(left_row->y(), right_row->y());
  EXPECT_LT(left_row->x(), right_row->x());
  EXPECT_EQ(left_row->width(), right_row->width());
  ActivateSidebarRow(left_row);
  EXPECT_EQ(left, window_->contents("work"));
  EXPECT_TRUE(shell->sidebar_widget()->IsVisible());
  ActivateSidebarRow(right_row);
  EXPECT_EQ(right, window_->contents("work"));
  EXPECT_TRUE(shell->sidebar_widget()->IsVisible());
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(left->GetContainerBounds().Intersects(right->GetContainerBounds()));
  EXPECT_GT(left->GetContainerBounds().width(), 100);
  EXPECT_GT(right->GetContainerBounds().width(), 100);
  auto* pane_host = window_->shell()->content_host();
  auto* divider = shell->split_divider_for_testing();
  ASSERT_TRUE(divider);
  EXPECT_EQ(divider, pane_host->GetEventHandlerForPoint(divider->bounds().CenterPoint()));
  ui::MouseEvent press(ui::EventType::kMousePressed, gfx::Point(4, 10), gfx::Point(4, 10),
      base::TimeTicks::Now(), ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON);
  ASSERT_TRUE(divider->OnMousePressed(press));
  ui::MouseEvent drag(ui::EventType::kMouseDragged, gfx::Point(104, 10), gfx::Point(104, 10),
      base::TimeTicks::Now(), ui::EF_LEFT_MOUSE_BUTTON, 0);
  ASSERT_TRUE(divider->OnMouseDragged(drag));
  ui::MouseEvent release(ui::EventType::kMouseReleased, gfx::Point(104, 10), gfx::Point(104, 10),
      base::TimeTicks::Now(), 0, ui::EF_LEFT_MOUSE_BUTTON);
  divider->OnMouseReleased(release);
  EXPECT_GT(window_->split_ratio(), 0.5);
  window_->FocusPane(false);
  EXPECT_EQ(left, window_->contents("work"));
  window_->FocusPane(true);
  EXPECT_EQ(right, window_->contents("work"));
  // The tab context menu separates the pair, keeping the clicked tab visible.
  window_->Unsplit(0);
  EXPECT_FALSE(window_->split());
  EXPECT_EQ(left, window_->contents("work"));
  EXPECT_EQ(2u, window_->tab_count("work"));
  ASSERT_TRUE(window_->SplitTab(1));
  EXPECT_EQ(right, window_->contents("work"));
  // The sidebar's right-edge handle resizes the pinned column and the page.
  const int sidebar_before = shell->sidebar_widget()->GetWindowBoundsInScreen().width();
  const int page_before = shell->content_host()->GetBoundsInScreen().x();
  const int width_before = shell->sidebar_width();
  shell->ResizeSidebar(30, false);
  shell->ResizeSidebar(40, true);
  EXPECT_EQ(width_before + 40, shell->sidebar_width());
  window_->widget()->LayoutRootViewIfNecessary();
  EXPECT_GT(shell->sidebar_widget()->GetWindowBoundsInScreen().width(), sidebar_before);
  EXPECT_GT(shell->content_host()->GetBoundsInScreen().x(), page_before);
  shell->ResizeSidebar(-1000, true);
  EXPECT_EQ(200, shell->sidebar_width());
  window_->SetSplitRatio(0.65);
  EXPECT_DOUBLE_EQ(0.65, window_->split_ratio());
  ASSERT_TRUE(window_->Select("personal"));
  EXPECT_FALSE(window_->split());
  EXPECT_EQ(personal_, window_->contents("personal")->GetBrowserContext());
  ASSERT_TRUE(window_->Select("work"));
  EXPECT_TRUE(window_->split());
  EXPECT_EQ(right, window_->contents("work"));
  EXPECT_DOUBLE_EQ(0.65, window_->split_ratio());
  // Closing either pane leaves a single, focused surviving page.
  window_->CloseTab(0);
  EXPECT_FALSE(window_->split());
  EXPECT_EQ(right, window_->contents("work"));
  EXPECT_EQ(1u, window_->tab_count("work"));
  ASSERT_TRUE(window_->SplitTab(0));
  EXPECT_TRUE(window_->split());
  ASSERT_TRUE(content::WaitForLoadStop(window_->pane_contents(true)));
  EXPECT_EQ(second, window_->pane_contents(true)->GetLastCommittedURL());
  EXPECT_NE(window_->pane_contents(false), window_->pane_contents(true));
  window_->shell()->CloseOverlays();
  window_->ToggleSplit();
  EXPECT_FALSE(window_->split());
  EXPECT_EQ(2u, window_->tab_count("work"));
  base::RunLoop().RunUntilIdle();
  // RunUntilIdle does not promise a compositor layout, especially after the
  // sidebar was hidden. Apply the pending layout before inspecting geometry.
  shell->sidebar_widget()->LayoutRootViewIfNecessary();
  ASSERT_TRUE(shell->tab_button(0));
  ASSERT_TRUE(shell->tab_button(1));
  EXPECT_EQ(shell->tab_button(0)->parent(), shell->tab_button(1)->parent());
  EXPECT_NE(shell->tab_button(0)->y(), shell->tab_button(1)->y());
  // Reassigning a profile drops the old live session and both old contexts.
  ASSERT_TRUE(window_->AssignProfile("work", "personal-profile"));
  EXPECT_FALSE(window_->split());
  EXPECT_EQ(1u, window_->tab_count("work"));
  EXPECT_EQ(personal_, window_->contents("work")->GetBrowserContext());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, SplitViewSessionRestore) {
  const auto first = embedded_test_server()->GetURL("/storage_probe.html?left");
  const auto second = embedded_test_server()->GetURL("/storage_probe.html?right");
  ASSERT_TRUE(content::NavigateToURL(window_->contents("work"), first));
  ASSERT_TRUE(window_->NavigateFromBar(second, true));
  ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));
  window_->SelectTab(0);
  ASSERT_TRUE(window_->SplitTab(1));
  window_->FocusPane(false);
  window_->SetSplitRatio(0.62);
  ASSERT_TRUE(window_->Select("personal"));
  const auto personal_url = embedded_test_server()->GetURL("/storage_probe.html?personal");
  ASSERT_TRUE(content::NavigateToURL(window_->contents("personal"), personal_url));
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_saved(); }));
  window_.reset();
  window_ = std::make_unique<WorkspaceWindow>(work_, personal_);
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_ready(); }));
  EXPECT_EQ("personal", window_->active_id());
  ASSERT_TRUE(content::WaitForLoadStop(window_->contents("personal")));
  EXPECT_EQ(personal_url, window_->contents("personal")->GetLastCommittedURL());
  ASSERT_TRUE(window_->Select("work"));
  EXPECT_TRUE(window_->split());
  EXPECT_DOUBLE_EQ(0.62, window_->split_ratio());
  ASSERT_EQ(2u, window_->tab_count("work"));
  ASSERT_TRUE(content::WaitForLoadStop(window_->pane_contents(false)));
  ASSERT_TRUE(content::WaitForLoadStop(window_->pane_contents(true)));
  EXPECT_EQ(first, window_->pane_contents(false)->GetLastCommittedURL());
  EXPECT_EQ(second, window_->pane_contents(true)->GetLastCommittedURL());
  EXPECT_EQ(window_->pane_contents(false), window_->contents("work"));
  // Collapsing keeps both tabs; subsequent restore must not resurrect a split.
  window_->ToggleSplit();
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_saved(); }));
  window_.reset();
  window_ = std::make_unique<WorkspaceWindow>(work_, personal_);
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_ready(); }));
  EXPECT_FALSE(window_->split());
  EXPECT_EQ(2u, window_->tab_count("work"));
}

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
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_ready(); }));
  ASSERT_NO_FATAL_FAILURE(NavigateBoth());
  EXPECT_EQ("work|work|work|", content::EvalJs(window_->contents("work"), "readMarker()"));
  EXPECT_EQ("personal|personal|personal|",
            content::EvalJs(window_->contents("personal"), "readMarker()"));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, DynamicWorkspacesAndProfiles) {
  ASSERT_NO_FATAL_FAILURE(NavigateBoth());
  ASSERT_EQ("work", content::EvalJs(window_->contents("work"), "writeMarker('work')"));
  ASSERT_EQ("personal", content::EvalJs(window_->contents("personal"), "writeMarker('personal')"));
  EXPECT_FALSE(window_->AddWorkspace("", "work-profile"));
  EXPECT_FALSE(window_->AddWorkspace("Invalid", "missing-profile"));
  auto* shell = window_->shell();
  shell->OpenWorkspaceSettings();
  auto* start_add = FindButton(shell->settings_widget()->GetContentsView(), u"Nowa przestrzeń…");
  ASSERT_TRUE(start_add);
  ClickControl(start_add);
  auto* add = FindButton(shell->settings_widget()->GetContentsView(), u"Utwórz przestrzeń");
  ASSERT_TRUE(add);
  shell->workspace_input()->SetText(u"   ");
  ClickControl(add);
  EXPECT_EQ(2u, window_->workspaces().size());
  shell->workspace_input()->SetText(u"Projekt Łódź");
  shell->SetProfilePickerSelectionForTesting(false,0);
  ClickControl(add);
  const auto project = window_->active_id();
  ASSERT_EQ(3u, window_->workspaces().size());
  EXPECT_EQ(work_.get(), window_->contents(project)->GetBrowserContext());
  const auto url = embedded_test_server()->GetURL("/storage_probe.html");
  ASSERT_TRUE(content::NavigateToURL(window_->contents(project), url));
  EXPECT_EQ("work|work|work|", content::EvalJs(window_->contents(project), "readMarker()"));
  ASSERT_TRUE(window_->NavigateFromBar(url, true));
  EXPECT_EQ(2u, window_->tab_count(project));
  EXPECT_FALSE(window_->EditWorkspace("Invalid", "missing-profile"));
  EXPECT_EQ(2u, window_->tab_count(project));
  shell->OpenWorkspaceSettings();
  shell->workspace_input()->SetText(u"Klient");
  shell->SetProfilePickerSelectionForTesting(false,1);
  auto* edit = FindButton(shell->settings_widget()->GetContentsView(), u"Zapisz nazwę i profil");
  ASSERT_TRUE(edit);
  ClickControl(edit);
  EXPECT_EQ(1u, window_->tab_count(project));
  EXPECT_EQ(personal_.get(), window_->contents(project)->GetBrowserContext());
  ASSERT_TRUE(content::NavigateToURL(window_->contents(project), url));
  EXPECT_EQ("personal|personal|personal|", content::EvalJs(window_->contents(project), "readMarker()"));
  window_->shell()->SetWorkspaceColor(project, 0xFF123456);
  {ui::AXActionData activate;activate.action=ax::mojom::Action::kDoDefault;EXPECT_TRUE(shell->workspace_button("work")->HandleAccessibleAction(activate));}
  EXPECT_EQ("work", window_->active_id());
  shell->OpenWorkspaceSettings();
  auto* remove = FindButton(shell->settings_widget()->GetContentsView(), u"Usuń przestrzeń…");
  ASSERT_TRUE(remove);
  ClickControl(remove);
  ASSERT_TRUE(shell->remove_workspace_dialog());
  EXPECT_NE(nullptr, window_->contents("work"));
  EXPECT_EQ(static_cast<int>(ui::mojom::DialogButton::kCancel),
      shell->remove_workspace_dialog()->widget_delegate()->AsDialogDelegate()->GetDefaultDialogButton());
  shell->remove_workspace_dialog()->widget_delegate()->AsDialogDelegate()->CancelDialog();
  ASSERT_TRUE(base::test::RunUntil([&] { return !shell->remove_workspace_dialog(); }));
  EXPECT_NE(nullptr, window_->contents("work"));
  shell->OpenWorkspaceSettings();  // Confirmation dismissed the settings overlay.
  ClickControl(remove);
  ASSERT_TRUE(shell->remove_workspace_dialog());
  shell->remove_workspace_dialog()->widget_delegate()->AsDialogDelegate()->AcceptDialog();
  ASSERT_TRUE(base::test::RunUntil([&] { return !shell->remove_workspace_dialog(); }));
  EXPECT_EQ(nullptr, window_->contents("work"));
  ASSERT_TRUE(window_->Select(project));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window_->configuration_saved() && window_->shell()->appearance_saved();
  }));
  window_.reset();
  window_ = std::make_unique<WorkspaceWindow>(work_, personal_);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window_->configuration_ready() && window_->shell()->appearance_ready();
  }));
  EXPECT_EQ(2u, window_->workspaces().size());
  EXPECT_EQ(project, window_->active_id());
  EXPECT_EQ(nullptr, window_->contents("work"));
  EXPECT_EQ("Klient", window_->workspaces()[1].name);
  EXPECT_EQ(personal_.get(), window_->contents(project)->GetBrowserContext());
  EXPECT_EQ(0xFF123456u, window_->shell()->workspace_color(project));
  ASSERT_TRUE(window_->RemoveWorkspace());
  EXPECT_FALSE(window_->RemoveWorkspace());
  EXPECT_EQ(1u, window_->workspaces().size());
  // Removing workspaces never removes the shared profile's storage.
  ASSERT_TRUE(content::NavigateToURL(window_->contents("personal"), url));
  EXPECT_EQ("personal|personal|personal|", content::EvalJs(window_->contents("personal"), "readMarker()"));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, WorkspaceContextAppearanceAndDeletion) {
  auto* shell = window_->shell();
  shell->BuildWorkspaceMenu("personal");
  auto* colors = shell->workspace_menu()->GetSubmenuModelAt(1);
  ASSERT_TRUE(colors);
  colors->ActivatedAt(1, 0);
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->workspace_color("personal") == 0xFF84B6A4u; }));
  EXPECT_EQ("work", window_->active_id());
  shell->BuildWorkspaceMenu("personal");
  EXPECT_TRUE(shell->workspace_menu()->GetSubmenuModelAt(1)->IsItemCheckedAt(1));
  auto* icons = shell->workspace_menu()->GetSubmenuModelAt(2);
  ASSERT_TRUE(icons);
  icons->ActivatedAt(2, 0);
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->workspace_emoji("personal") == u"🚀"; }));
  EXPECT_EQ("work", window_->active_id());
  EXPECT_EQ(u"", shell->emoji_input()->GetText());
  shell->BuildWorkspaceMenu("personal");
  shell->workspace_menu()->ActivatedAt(5, 0);
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->remove_workspace_dialog(); }));
  EXPECT_EQ("work", window_->active_id());
  shell->remove_workspace_dialog()->widget_delegate()->AsDialogDelegate()->CancelDialog();
  ASSERT_TRUE(base::test::RunUntil([&] { return !shell->remove_workspace_dialog(); }));
  EXPECT_EQ(2u, window_->workspaces().size());
  shell->BuildWorkspaceMenu("personal");
  shell->workspace_menu()->ActivatedAt(5, 0);
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->remove_workspace_dialog(); }));
  // Selection changing behind the dialog must never redirect deletion.
  ASSERT_TRUE(window_->Select("personal"));
  ASSERT_TRUE(window_->Select("work"));
  shell->remove_workspace_dialog()->widget_delegate()->AsDialogDelegate()->AcceptDialog();
  ASSERT_TRUE(base::test::RunUntil([&] { return !shell->remove_workspace_dialog(); }));
  EXPECT_EQ(nullptr, window_->contents("personal"));
  EXPECT_EQ("work", window_->active_id());
  shell->BuildWorkspaceMenu("work");
  EXPECT_FALSE(shell->workspace_menu()->IsEnabledAt(5));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, WorkspaceDeletionConfirmationStaysOnScreen) {
  auto* shell = window_->shell();
  const auto area = display::Screen::Get()->GetDisplayNearestWindow(window_->widget()->GetNativeWindow()).work_area();
  const gfx::Size size(std::min(700, area.width()), std::min(600, area.height()));
  const std::array<gfx::Point, 4> corners = {
      area.origin(), gfx::Point(area.right() - size.width(), area.y()),
      gfx::Point(area.x(), area.bottom() - size.height()),
      gfx::Point(area.right() - size.width(), area.bottom() - size.height())};
  for (size_t i = 0; i < corners.size(); ++i) {
    window_->widget()->SetBounds(gfx::Rect(corners[i], size));
    if (i % 2 == 0) {
      shell->BuildWorkspaceMenu("personal");
      shell->workspace_menu()->ActivatedAt(5, 0);
    } else {
      shell->OpenWorkspaceSettings();
      auto* remove = FindButton(shell->settings_widget()->GetContentsView(), u"Usuń przestrzeń…");
      ASSERT_TRUE(remove);
      ClickControl(remove);
    }
    ASSERT_TRUE(base::test::RunUntil([&] { return shell->remove_workspace_dialog() && shell->remove_workspace_dialog()->IsVisible(); }));
    base::RunLoop().RunUntilIdle();
    const auto bounds = shell->remove_workspace_dialog()->GetWindowBoundsInScreen();
    EXPECT_FALSE(bounds.IsEmpty());
    EXPECT_TRUE(area.Contains(bounds)) << area.ToString() << " / " << bounds.ToString();
    EXPECT_TRUE(window_->widget()->GetWindowBoundsInScreen().Intersects(bounds));
    shell->remove_workspace_dialog()->widget_delegate()->AsDialogDelegate()->CancelDialog();
    ASSERT_TRUE(base::test::RunUntil([&] { return !shell->remove_workspace_dialog(); }));
    EXPECT_EQ(2u, window_->workspaces().size());
  }
}

namespace {
views::Textfield* NativeFolderNameInput(views::View* root) {
  if (auto* field = views::AsViewClass<views::Textfield>(root)) return field;
  for (const auto& child : root->children()) if (auto* field = NativeFolderNameInput(child.get())) return field;
  return nullptr;
}
// Exercise the real field in each backend, with no test editing model/wrapper.
bool ReplaceFolderName(WorkspaceShell* shell,std::u16string_view text) {
  if(auto* editor=shell->folder_editor_for_testing()){
    editor->SelectRange(gfx::Range(0,editor->GetText().size()));
    editor->input_client()->InsertText(std::u16string(text),ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);
    return editor->GetText()==text;
  }
  auto* native=NativeFolderNameInput(shell->folder_dialog()->GetContentsView());
  if(!native)return false;native->SelectAll(false);native->InsertOrReplaceText(std::u16string(text));return native->GetText()==text;
}
views::View* SidebarNamedButton(views::View* root,std::u16string_view name){
  ui::AXNodeData data;root->GetViewAccessibility().GetAccessibleNodeData(&data);
  if(data.role==ax::mojom::Role::kButton&&data.GetString16Attribute(ax::mojom::StringAttribute::kName)==name)return root;
  for (const auto& child : root->children()) if (auto* button = SidebarNamedButton(child.get(), name)) return button;
  return nullptr;
}
}  // namespace

namespace {
class SceneModalBodyOracle final : public views::View {
  METADATA_HEADER(SceneModalBodyOracle,views::View)
 public:
  explicit SceneModalBodyOracle(views::View* source):source_(source){}
  gfx::Size CalculatePreferredSize(const views::SizeBounds& available) const override{return source_->GetPreferredSize(available);}
  gfx::Size GetMinimumSize() const override{return source_->GetMinimumSize();}
 private:
  raw_ptr<views::View> source_;
};
BEGIN_METADATA(SceneModalBodyOracle)
END_METADATA
}  // namespace
// Deferred: compile only until the requested bulk UI checkpoint.
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, SidebarLogicalNodesSurviveIndexShiftAndAdapterReplacement) {
  ASSERT_NO_FATAL_FAILURE(NavigateBoth());ASSERT_TRUE(window_->Select("work"));
  ASSERT_TRUE(window_->NavigateFromBar(embedded_test_server()->GetURL("/storage_probe.html?logical-node"),true));
  auto* target=window_->contents("work");ASSERT_TRUE(content::WaitForLoadStop(target));
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->tab_button(1)!=nullptr;}));auto* old=shell->tab_button(1);ASSERT_TRUE(old);
  const bool rust=shell->workspace_input()->rust_editor();const auto logical=shell->sidebar_node_id_for_testing(old);
  if(rust)EXPECT_NE(0u,logical);else EXPECT_EQ(0u,logical);
  auto* old_close=FindButton(old,u"Zamknij kartę");if(rust){ASSERT_TRUE(old_close);EXPECT_NE(0u,shell->sidebar_node_id_for_testing(old_close));EXPECT_NE(logical,shell->sidebar_node_id_for_testing(old_close));}
  // Retain the detached adapter so the test can inspect an old lease safely.
  auto held=old->parent()->RemoveChildViewT(old);
  window_->CloseTab(0);ASSERT_EQ(target,window_->contents("work"));
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->tab_button(0)&&shell->tab_button(0)!=old;}));
  auto* replacement=shell->tab_button(0);ASSERT_TRUE(replacement);EXPECT_NE(old,replacement);
  if(rust){EXPECT_EQ(logical,shell->sidebar_node_id_for_testing(replacement));EXPECT_EQ(0u,shell->sidebar_node_id_for_testing(held.get()));EXPECT_EQ(0u,shell->sidebar_node_id_for_testing(old_close));EXPECT_FALSE(FindButton(replacement,u"Zamknij kartę"));}
  ui::KeyEvent space(ui::EventType::kKeyPressed,ui::VKEY_SPACE,ui::EF_NONE);
  if(rust)held->OnKeyPressed(space);EXPECT_EQ(target,window_->contents("work"));
  ASSERT_TRUE(content::ExecJs(target,"document.title='Logical node renamed';"));
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->tab_button(0)->GetTooltipText()==u"Logical node renamed";}));
  if(rust)EXPECT_EQ(logical,shell->sidebar_node_id_for_testing(shell->tab_button(0)));
}

// Compiled for the next bulk UI checkpoint; not executed by headless gates.
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, ResidualGlyphHostsImageAndImportDismiss) {
  base::ScopedAllowBlockingForTesting files; base::ScopedTempDir source;
  ASSERT_TRUE(source.CreateUniqueTempDir());base::FilePath root;
  ASSERT_TRUE(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT,&root));
  ASSERT_TRUE(base::CopyDirectory(root.AppendASCII("zitter/testdata/arc"),source.GetPath().AppendASCII("Arc"),true));
  base::ScopedPathOverride app_data(base::DIR_APP_DATA,source.GetPath());
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));
  const bool rust=shell->workspace_input()->rust_editor();window_->FocusAddressBar();
  auto* icon=shell->palette_chrome_view_for_testing(0);ASSERT_TRUE(icon);
  EXPECT_EQ(!rust,views::AsViewClass<views::Button>(icon)!=nullptr);
  EXPECT_EQ(gfx::Size(32,32),icon->GetPreferredSize());
  EXPECT_EQ(views::View::FocusBehavior::NEVER,icon->GetFocusBehavior());
  EXPECT_FALSE(icon->GetCanProcessEventsWithinSubtree());
  ui::AXNodeData image;icon->GetViewAccessibility().GetAccessibleNodeData(&image);
  EXPECT_EQ(rust?ax::mojom::Role::kImage:ax::mojom::Role::kButton,image.role);
  EXPECT_EQ(u"Szukaj",image.GetString16Attribute(ax::mojom::StringAttribute::kName));
  if(rust){EXPECT_TRUE(icon->children().empty());EXPECT_FALSE(image.HasIntAttribute(ax::mojom::IntAttribute::kDefaultActionVerb));}
  shell->CloseOverlays();shell->OpenImport();
  ASSERT_TRUE(base::test::RunUntil([&]{return window_->import_ready();}));
  auto* close=shell->import_close();ASSERT_TRUE(close);
  EXPECT_EQ(!rust,views::AsViewClass<views::Button>(close)!=nullptr);
  if(rust){EXPECT_TRUE(close->children().empty());ASSERT_TRUE(base::test::RunUntil([&]{return shell->import_close_metal_visible_for_testing();}));}
  ui::AXActionData activate;activate.action=ax::mojom::Action::kDoDefault;
  shell->SetImportStatus(u"Importowanie…",false,true,true);EXPECT_FALSE(close->GetEnabled());
  close->HandleAccessibleAction(activate);EXPECT_TRUE(shell->import_visible());
  shell->SetImportStatus(u"Gotowe",true);EXPECT_TRUE(close->GetEnabled());
  close->HandleAccessibleAction(activate);EXPECT_FALSE(shell->import_visible());
  if(rust)ASSERT_TRUE(base::test::RunUntil([&]{return !shell->import_close_metal_visible_for_testing();}));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest,
                       SelectActionHostsPopupMenuAndClippedPress) {
  auto* shell = window_->shell();
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->appearance_ready(); }));
  shell->OpenGeneralSettings();
  shell->preferences_search()->input_client()->InsertText(
      u"a", ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);
  shell->general_widget()->LayoutRootViewIfNecessary();
  const bool rust = shell->workspace_input()->rust_editor();
  auto* picker = shell->theme_picker();
  ASSERT_TRUE(picker);
  EXPECT_EQ(!rust, views::AsViewClass<views::Combobox>(picker) != nullptr);
  EXPECT_FALSE(views::AsViewClass<views::Button>(picker));
  if (rust) EXPECT_TRUE(picker->children().empty());
  picker->ScrollViewToVisible();
  auto data = [](views::View* view) {
    ui::AXNodeData node;
    view->GetViewAccessibility().GetAccessibleNodeData(&node);
    return node;
  };
  const auto original = data(picker).GetString16Attribute(
      ax::mojom::StringAttribute::kValue);
  EXPECT_EQ(ax::mojom::Role::kComboBoxSelect, data(picker).role);
  EXPECT_EQ(ax::mojom::DefaultActionVerb::kOpen,
            data(picker).GetDefaultActionVerb());
  ui::AXActionData activate;
  activate.action = ax::mojom::Action::kDoDefault;
  picker->SetEnabled(false);
  EXPECT_FALSE(data(picker).HasIntAttribute(
      ax::mojom::IntAttribute::kDefaultActionVerb));
  picker->HandleAccessibleAction(activate);
  EXPECT_EQ(original, data(picker).GetString16Attribute(
                          ax::mojom::StringAttribute::kValue));
  picker->SetEnabled(true);
  EXPECT_EQ(ax::mojom::DefaultActionVerb::kOpen,
            data(picker).GetDefaultActionVerb());
  base::ScopedClosureRunner close_menu(base::BindOnce([] {
    if (auto* menu = views::MenuController::GetActiveInstance())
      menu->Cancel(views::MenuController::ExitType::kAll);
  }));
  ui_controls::EnableUIControls();
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(
      shell->general_widget()->GetNativeWindow()));
  // Native Combobox guards rapid clicks after menu close; exercise mouse first.
  for (int source : {2, 0, 1}) {
    SCOPED_TRACE(source);
    picker->RequestFocus();
    ASSERT_TRUE(picker->HasFocus());
    if (source == 0) {
      EXPECT_TRUE(picker->HandleAccessibleAction(activate));
    } else if (source == 1) {
      ui::KeyEvent down(ui::EventType::kKeyPressed, ui::VKEY_DOWN, ui::EF_NONE);
      EXPECT_TRUE(picker->OnKeyPressed(down));
    } else {
      auto* root = picker->GetWidget()->GetRootView();
      auto point = picker->GetLocalBounds().CenterPoint();
      views::View::ConvertPointToTarget(picker, root, &point);
      ui::MouseEvent press(ui::EventType::kMousePressed, point, point,
                           base::TimeTicks(), ui::EF_LEFT_MOUSE_BUTTON,
                           ui::EF_LEFT_MOUSE_BUTTON);
      ui::MouseEvent release(ui::EventType::kMouseReleased, point, point,
                             base::TimeTicks(), ui::EF_LEFT_MOUSE_BUTTON,
                             ui::EF_LEFT_MOUSE_BUTTON);
      // Opening a menu on press clears RootView capture in the native combobox.
      root->OnMousePressed(press);
      EXPECT_TRUE(views::MenuController::GetActiveInstance());
      root->OnMouseReleased(release);
    }
    ASSERT_TRUE(views::MenuController::GetActiveInstance());
    EXPECT_TRUE(data(picker).HasState(ax::mojom::State::kExpanded));
    EXPECT_EQ(original, data(picker).GetString16Attribute(
                            ax::mojom::StringAttribute::kValue));
    views::MenuController::GetActiveInstance()->Cancel(
        views::MenuController::ExitType::kAll);
    ASSERT_TRUE(base::test::RunUntil([&] {
      return !views::MenuController::GetActiveInstance() &&
             data(picker).HasState(ax::mojom::State::kCollapsed);
    }));
    EXPECT_EQ(original, data(picker).GetString16Attribute(
                            ax::mojom::StringAttribute::kValue));
  }
  if (rust) {
    const auto point = picker->GetLocalBounds().CenterPoint();
    ui::MouseEvent release(ui::EventType::kMouseReleased, point, point,
                           base::TimeTicks(), ui::EF_LEFT_MOUSE_BUTTON,
                           ui::EF_LEFT_MOUSE_BUTTON);
    auto* scroll = shell->preference_viewport_for_testing();
    const auto offset = scroll->offset();
    ASSERT_GT(scroll->maximum(), 0);
    // Keyboard bypasses the close-time click guard, opening a real anchored popup.
    ui::KeyEvent down(ui::EventType::kKeyPressed, ui::VKEY_DOWN, ui::EF_NONE);
    ASSERT_TRUE(picker->OnKeyPressed(down));
    ASSERT_TRUE(views::MenuController::GetActiveInstance());
    scroll->ScrollTo(scroll->maximum());
    EXPECT_TRUE(picker->GetVisibleBounds().IsEmpty());
    scroll->ScrollTo(offset);
    picker->OnMouseReleased(release);
    EXPECT_FALSE(views::MenuController::GetActiveInstance());
    EXPECT_TRUE(data(picker).HasState(ax::mojom::State::kCollapsed));
    EXPECT_EQ(original, data(picker).GetString16Attribute(
                            ax::mojom::StringAttribute::kValue));
    scroll->ScrollTo(scroll->maximum());
    EXPECT_TRUE(picker->GetVisibleBounds().IsEmpty());
    EXPECT_TRUE(picker->HandleAccessibleAction(activate));
    EXPECT_FALSE(picker->GetVisibleBounds().IsEmpty());
    ASSERT_TRUE(views::MenuController::GetActiveInstance());
    EXPECT_TRUE(data(picker).HasState(ax::mojom::State::kExpanded));
    views::MenuController::GetActiveInstance()->Cancel(
        views::MenuController::ExitType::kAll);
    ASSERT_TRUE(base::test::RunUntil([&] {
      return !views::MenuController::GetActiveInstance() &&
             data(picker).HasState(ax::mojom::State::kCollapsed);
    }));
  }
}

// Compile only: model ownership is checked headlessly; actual AX/events await the bulk checkpoint.
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, CheckedModelsBelongToNodesAcrossProjectionAndDisabledRefresh) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));const bool rust=shell->workspace_input()->rust_editor();shell->OpenGeneralSettings();
  for(const auto* key:{"camera","javascript","hardware_acceleration","sidebar","animations"}){auto* view=shell->preference_switch_for_testing(key);ASSERT_TRUE(view);const auto id=shell->sidebar_node_id_for_testing(view);EXPECT_EQ(rust?2u:0u,shell->control_model_kind_for_testing(view));const bool enabled=view->GetEnabled();view->SetEnabled(false);view->SetEnabled(enabled);EXPECT_EQ(id,shell->sidebar_node_id_for_testing(view));EXPECT_EQ(rust?2u:0u,shell->control_model_kind_for_testing(view));}
  for(size_t i=0;i<3;++i){auto* view=shell->clear_checkbox_for_testing(i);ASSERT_TRUE(view);EXPECT_EQ(rust?1u:0u,shell->control_model_kind_for_testing(view));}
  shell->OpenImport();auto* view=shell->import_profiles();ASSERT_TRUE(view);const auto id=shell->sidebar_node_id_for_testing(view);shell->SetImportOptionCheckedForTesting(0,false);ui::AXNodeData ax;view->GetViewAccessibility().GetAccessibleNodeData(&ax);EXPECT_EQ(ax::mojom::CheckedState::kFalse,ax.GetCheckedState());shell->SetImportOptionCheckedForTesting(0,true);view->GetViewAccessibility().GetAccessibleNodeData(&ax);EXPECT_EQ(ax::mojom::CheckedState::kTrue,ax.GetCheckedState());shell->SetImportStatus(u"Model busy",false,true,true);EXPECT_EQ(id,shell->sidebar_node_id_for_testing(view));EXPECT_EQ(rust?1u:0u,shell->control_model_kind_for_testing(view));shell->SetImportStatus(u"Model ready",false,false,false);
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, CheckedModelRetiresDuringObserverBeforeImportCallback) {
  base::ScopedAllowBlockingForTesting files;base::ScopedTempDir app_data;ASSERT_TRUE(app_data.CreateUniqueTempDir());base::ScopedPathOverride isolated_data(base::DIR_APP_DATA,app_data.GetPath());auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));if(!shell->workspace_input()->rust_editor())return;shell->OpenImport();auto* check=shell->import_history();ASSERT_TRUE(check);EXPECT_EQ(1u,shell->control_model_kind_for_testing(check));ASSERT_FALSE(shell->import_options().history);check->SetProperty(views::kElementIdentifierKey,views::DialogClientView::kOkButtonElementId);int notified=0;bool observed_checked=false;std::unique_ptr<views::View> held;
  auto observer=ui::ElementTracker::GetElementTracker()->AddElementActivatedCallback(views::DialogClientView::kOkButtonElementId,views::ElementTrackerViews::GetContextForWidget(shell->import_widget()),base::BindRepeating([](views::View* view,std::unique_ptr<views::View>* held,int* notified,bool* checked,ui::TrackedElement*){++*notified;ui::AXNodeData ax;view->GetViewAccessibility().GetAccessibleNodeData(&ax);*checked=ax.GetCheckedState()==ax::mojom::CheckedState::kTrue;*held=view->parent()->RemoveChildViewT(view);},base::Unretained(check),&held,&notified,&observed_checked));ClickControl(check);EXPECT_EQ(1,notified);EXPECT_TRUE(observed_checked);ASSERT_TRUE(held);EXPECT_EQ(0u,shell->control_model_kind_for_testing(held.get()));EXPECT_EQ(0u,shell->sidebar_node_id_for_testing(held.get()));EXPECT_FALSE(shell->import_options().history);
  // Keep the retired adapter alive through controller teardown, without a dangling form field.
  window_.reset();ui::KeyEvent space(ui::EventType::kKeyPressed,ui::VKEY_SPACE,0);EXPECT_FALSE(held->OnKeyPressed(space));
}

// Compile only: checkbox/switch/selector behavior awaits the bulk checkpoint.
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, CheckedAndSelectorNodesCoverLiveFormsAndRetainProjectionIdentity) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready()&&shell->tools_ready();}));const bool rust=shell->workspace_input()->rust_editor();
  auto id=[&](views::View* view){EXPECT_TRUE(view);if(!view)return uint64_t{0};const auto value=shell->sidebar_node_id_for_testing(view);if(rust)EXPECT_NE(0u,value);else EXPECT_EQ(0u,value);return value;};
  shell->OpenGeneralSettings();for(const auto* key:{"camera","javascript","hardware_acceleration","sidebar","animations"}){auto* view=shell->preference_switch_for_testing(key);const auto before=id(view);const bool enabled=view->GetEnabled();view->SetEnabled(false);view->SetEnabled(enabled);EXPECT_EQ(before,id(view));}
  for(const auto* key:{"theme","search","category","zoom","font_size","minimum_font_size","fixed_font_size","dns_mode","site_type","site_policy","clear_period"}){auto* view=shell->preference_choice_for_testing(key);const auto before=id(view);const auto selected=shell->preference_choice_selection_for_testing(key);const bool enabled=view->GetEnabled();view->SetEnabled(false);view->SetEnabled(enabled);EXPECT_EQ(before,id(view));EXPECT_EQ(selected,shell->preference_choice_selection_for_testing(key));}
  std::vector<uint64_t> checks;for(size_t i=0;i<3;++i)checks.push_back(id(shell->clear_checkbox_for_testing(i)));if(rust)for(size_t i=0;i<checks.size();++i)for(size_t j=0;j<i;++j)EXPECT_NE(checks[i],checks[j]);
  shell->OpenProfiles();auto* profiles=shell->managed_profile_picker();const auto profile_id=id(profiles);shell->SetProfilePickerSelectionForTesting(true,0);EXPECT_EQ(profile_id,id(profiles));
  shell->OpenImport();id(shell->import_browser_picker());std::vector<views::View*> options={shell->import_profiles(),shell->import_bookmarks(),shell->import_history(),shell->import_cookies()};checks.clear();for(auto* view:options)checks.push_back(id(view));if(rust)for(size_t i=0;i<checks.size();++i)for(size_t j=0;j<i;++j)EXPECT_NE(checks[i],checks[j]);shell->SetImportStatus(u"Scoped busy",false,true,true);for(size_t i=0;i<options.size();++i)EXPECT_EQ(checks[i],id(options[i]));shell->SetImportStatus(u"Scoped ready",false,false,false);
}

// Compile only: native selector projections await the deferred GUI checkpoint.
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, SelectorModelsBelongToNodesAcrossFormsAndProjection) {
  base::ScopedAllowBlockingForTesting files;base::ScopedTempDir app_data;ASSERT_TRUE(app_data.CreateUniqueTempDir());base::ScopedPathOverride isolated_data(base::DIR_APP_DATA,app_data.GetPath());auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready()&&shell->tools_ready();}));const bool rust=shell->workspace_input()->rust_editor();
  auto check=[&](views::View* view){ASSERT_TRUE(view);EXPECT_EQ(rust?3u:0u,shell->control_model_kind_for_testing(view));const auto id=shell->sidebar_node_id_for_testing(view);const bool enabled=view->GetEnabled();view->SetEnabled(false);view->SetEnabled(enabled);EXPECT_EQ(id,shell->sidebar_node_id_for_testing(view));EXPECT_EQ(rust?3u:0u,shell->control_model_kind_for_testing(view));};
  shell->OpenGeneralSettings();for(const auto* key:{"theme","search","category","zoom","font_size","minimum_font_size","fixed_font_size","dns_mode","site_type","site_policy","clear_period"})check(shell->preference_choice_for_testing(key));
  shell->OpenWorkspaceSettings();check(shell->profile_picker());shell->OpenProfiles();check(shell->managed_profile_picker());shell->SetProfilePickerSelectionForTesting(true,0);check(shell->managed_profile_picker());shell->OpenImport();check(shell->import_browser_picker());shell->SetImportStatus(u"Selector busy",false,true,true);check(shell->import_browser_picker());shell->SetImportStatus(u"Selector ready",false,false,false);
  for(size_t page=0;page<3;++page){shell->OpenTools(page);check(shell->tools_choice_for_testing("selection"));}
}

// A retained selector bypasses base-button input for keys and AX; both must reject retirement.
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, RetiredSelectorRejectsKeyboardAndPopupAfterToolsRebuild) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_ready();}));shell->OpenTools(1);auto* picker=shell->tools_choice_for_testing("selection");ASSERT_TRUE(picker);const bool rust=shell->workspace_input()->rust_editor();EXPECT_EQ(rust?3u:0u,shell->control_model_kind_for_testing(picker));const auto id=shell->sidebar_node_id_for_testing(picker);if(rust)EXPECT_NE(0u,id);else EXPECT_EQ(0u,id);
  auto held=picker->parent()->RemoveChildViewT(picker);if(rust){EXPECT_EQ(0u,shell->control_model_kind_for_testing(held.get()));EXPECT_EQ(0u,shell->sidebar_node_id_for_testing(held.get()));ui::KeyEvent down(ui::EventType::kKeyPressed,ui::VKEY_DOWN,0);EXPECT_FALSE(held->OnKeyPressed(down));ui::AXActionData open;open.action=ax::mojom::Action::kDoDefault;held->HandleAccessibleAction(open);EXPECT_FALSE(views::MenuController::GetActiveInstance());}
  shell->OpenTools(2);ASSERT_TRUE(shell->tools_choice_for_testing("selection"));if(rust){EXPECT_NE(0u,shell->sidebar_node_id_for_testing(shell->tools_choice_for_testing("selection")));EXPECT_EQ(0u,shell->sidebar_node_id_for_testing(held.get()));}
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, CheckedActionHostsAXAndViewportCancellation) {
  ASSERT_NO_FATAL_FAILURE(NavigateBoth());auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->OpenGeneralSettings();const bool rust=shell->workspace_input()->rust_editor();
  auto filter=[&](std::u16string text){auto* field=shell->preferences_search();field->SelectRange(gfx::Range(0,field->GetText().size()));field->input_client()->InsertText(text,ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);shell->general_widget()->LayoutRootViewIfNecessary();};
  auto data=[](views::View* view){ui::AXNodeData node;view->GetViewAccessibility().GetAccessibleNodeData(&node);return node;};
  auto shape=[&](views::View* view,ax::mojom::Role role){EXPECT_EQ(!rust,views::AsViewClass<views::Button>(view)!=nullptr);EXPECT_EQ(role,data(view).role);if(rust)EXPECT_TRUE(view->children().empty());};
  const gfx::Point point(12,12);ui::MouseEvent press(ui::EventType::kMousePressed,point,point,base::TimeTicks(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);ui::MouseEvent release(ui::EventType::kMouseReleased,point,point,base::TimeTicks(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);ui::AXActionData activate;activate.action=ax::mojom::Action::kDoDefault;
  filter(u"Kamera");auto* camera=shell->preference_toggle("camera");shape(camera,ax::mojom::Role::kSwitch);camera->ScrollViewToVisible();const bool before=shell->preference_switch_checked_for_testing("camera");int notified=0;bool observed_checked=before;camera->SetProperty(views::kElementIdentifierKey,views::DialogClientView::kOkButtonElementId);auto observer=ui::ElementTracker::GetElementTracker()->AddElementActivatedCallback(views::DialogClientView::kOkButtonElementId,views::ElementTrackerViews::GetContextForWidget(shell->general_widget()),base::BindRepeating([](views::View* view,int* count,bool* checked,ui::TrackedElement*){++*count;ui::AXNodeData node;view->GetViewAccessibility().GetAccessibleNodeData(&node);*checked=node.GetCheckedState()==ax::mojom::CheckedState::kTrue;},base::Unretained(camera),&notified,&observed_checked));camera->HandleAccessibleAction(activate);EXPECT_EQ(1,notified);EXPECT_EQ(!before,observed_checked);EXPECT_EQ(!before,shell->preference_switch_checked_for_testing("camera"));camera->SetEnabled(false);EXPECT_FALSE(data(camera).HasIntAttribute(ax::mojom::IntAttribute::kDefaultActionVerb));camera->HandleAccessibleAction(activate);EXPECT_EQ(!before,shell->preference_switch_checked_for_testing("camera"));camera->SetEnabled(true);EXPECT_EQ(ax::mojom::DefaultActionVerb::kPress,data(camera).GetDefaultActionVerb());EXPECT_EQ(1,notified);camera->ClearProperty(views::kElementIdentifierKey);
  filter(u"Wyczyść dane przeglądania");for(size_t i=0;i<3;++i){auto* check=shell->clear_checkbox_for_testing(i);ASSERT_TRUE(check);shape(check,ax::mojom::Role::kCheckBox);check->ScrollViewToVisible();auto node=data(check);const bool checked=node.GetCheckedState()==ax::mojom::CheckedState::kTrue;EXPECT_EQ(checked?ax::mojom::DefaultActionVerb::kUncheck:ax::mojom::DefaultActionVerb::kCheck,node.GetDefaultActionVerb());check->HandleAccessibleAction(activate);EXPECT_EQ(checked?ax::mojom::CheckedState::kFalse:ax::mojom::CheckedState::kTrue,data(check).GetCheckedState());check->SetEnabled(false);EXPECT_FALSE(data(check).HasIntAttribute(ax::mojom::IntAttribute::kDefaultActionVerb));check->HandleAccessibleAction(activate);EXPECT_EQ(checked?ax::mojom::CheckedState::kFalse:ax::mojom::CheckedState::kTrue,data(check).GetCheckedState());check->SetEnabled(true);EXPECT_EQ(checked?ax::mojom::DefaultActionVerb::kCheck:ax::mojom::DefaultActionVerb::kUncheck,data(check).GetDefaultActionVerb());}
  if(rust){auto* check=shell->clear_checkbox_for_testing(1);check->ScrollViewToVisible();const auto checked=data(check).GetCheckedState();const auto bounds=check->bounds();ASSERT_TRUE(check->OnMousePressed(press));check->SetVisible(false);check->SetVisible(true);check->OnMouseReleased(release);EXPECT_EQ(checked,data(check).GetCheckedState());auto* viewport=shell->preference_viewport_for_testing();const auto offset=viewport->offset();ASSERT_TRUE(check->OnMousePressed(press));check->SetBoundsRect(gfx::Rect(bounds.x(),bounds.y()+10000,bounds.width(),bounds.height()));check->parent()->SchedulePaint();EXPECT_TRUE(check->GetVisibleBounds().IsEmpty());check->SetBoundsRect(bounds);viewport->ScrollTo(offset);check->OnMouseReleased(release);EXPECT_EQ(checked,data(check).GetCheckedState());
    filter(u"a");auto* top=shell->preference_switch_for_testing("sidebar");top->ScrollViewToVisible();const bool pinned=shell->pinned();const auto previous=viewport->offset();ASSERT_GT(viewport->maximum(),0);ASSERT_TRUE(top->OnMousePressed(press));viewport->ScrollTo(viewport->maximum());EXPECT_TRUE(top->GetVisibleBounds().IsEmpty());viewport->ScrollTo(previous);top->OnMouseReleased(release);EXPECT_EQ(pinned,shell->pinned());}
}

// Compiled for the deferred bulk checkpoint; do not run in the headless gate.
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, WorkspaceDismissNodeUsesSceneGroupAndPreservesCloseAdmission) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->OpenWorkspaceSettings();auto* close=FindButton(shell->workspace_panel_for_testing(),u"Zamknij ustawienia przestrzeni");ASSERT_TRUE(close);const bool rust=shell->workspace_input()->rust_editor();EXPECT_EQ(!rust,views::AsViewClass<views::Button>(close)!=nullptr);EXPECT_EQ(gfx::Size(32,32),close->GetPreferredSize());const auto id=shell->sidebar_node_id_for_testing(close);if(rust)EXPECT_NE(0u,id);else EXPECT_EQ(0u,id);
  ui::AXNodeData ax;close->GetViewAccessibility().GetAccessibleNodeData(&ax);EXPECT_EQ(ax::mojom::Role::kButton,ax.role);EXPECT_EQ(u"Zamknij ustawienia przestrzeni",ax.GetString16Attribute(ax::mojom::StringAttribute::kName));ui::AXActionData activate;activate.action=ax::mojom::Action::kDoDefault;close->SetEnabled(false);close->HandleAccessibleAction(activate);EXPECT_TRUE(shell->settings_visible());close->SetEnabled(true);EXPECT_EQ(id,shell->sidebar_node_id_for_testing(close));EXPECT_TRUE(close->HandleAccessibleAction(activate));EXPECT_FALSE(shell->settings_visible());
  shell->OpenWorkspaceSettings();EXPECT_EQ(close,FindButton(shell->workspace_panel_for_testing(),u"Zamknij ustawienia przestrzeni"));EXPECT_EQ(id,shell->sidebar_node_id_for_testing(close));ui::KeyEvent space(ui::EventType::kKeyPressed,ui::VKEY_SPACE,0);EXPECT_TRUE(close->OnKeyPressed(space));EXPECT_FALSE(shell->settings_visible());
}

// Compile only: native group order/focus/cancellation and selector popup await GUI acceptance.
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, FormSceneActionParentsAndReorderPreserveViewsCancelPendingActivation) {
  auto* shell=window_->shell();if(!shell->workspace_input()->rust_editor())return;int accepted=0;auto delegate=CreateSceneDialogDelegate();delegate->SetOwnershipOfNewWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);delegate->SetContentsView(std::make_unique<views::View>())->SetPreferredSize(gfx::Size(220,80));delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk)|static_cast<int>(ui::mojom::DialogButton::kCancel));delegate->SetAcceptCallbackWithClose(base::BindRepeating([](int* count){++*count;return false;},&accepted));std::unique_ptr<views::Widget> widget(views::DialogDelegate::CreateDialogWidget(delegate.get(),window_->widget()->GetNativeWindow(),window_->widget()->GetNativeView()));widget->Show();widget->LayoutRootViewIfNecessary();auto* ok=DialogActionButton(widget.get(),true);auto* cancel=DialogActionButton(widget.get(),false);ASSERT_TRUE(ok);ASSERT_TRUE(cancel);auto* group=ok->parent();ASSERT_EQ(group,cancel->parent());const auto root=shell->scene_parent_id_for_testing(ok),id=shell->sidebar_node_id_for_testing(ok);ASSERT_NE(0u,root);EXPECT_EQ(root,shell->scene_parent_id_for_testing(cancel));ok->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);ok->RequestFocus();ASSERT_TRUE(ok->HasFocus());const auto point=ok->GetLocalBounds().CenterPoint();ui::MouseEvent press(ui::EventType::kMousePressed,point,point,base::TimeTicks(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);ASSERT_TRUE(ok->OnMousePressed(press));group->ReorderChildView(ok,group->children().front().get()==ok?group->children().size()-1:0);widget->LayoutRootViewIfNecessary();EXPECT_EQ(id,shell->sidebar_node_id_for_testing(ok));EXPECT_EQ(root,shell->scene_parent_id_for_testing(ok));EXPECT_TRUE(ok->HasFocus());ui::MouseEvent release(ui::EventType::kMouseReleased,point,point,base::TimeTicks(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);ok->OnMouseReleased(release);EXPECT_EQ(0,accepted);auto held=group->RemoveChildViewT(ok);EXPECT_EQ(0u,shell->scene_parent_id_for_testing(held.get()));EXPECT_EQ(root,shell->scene_parent_id_for_testing(cancel));widget->CloseNow();widget.reset();ui::KeyEvent space(ui::EventType::kKeyPressed,ui::VKEY_SPACE,0);EXPECT_FALSE(held->OnKeyPressed(space));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, FormSceneSelectorReorderClosesPopupWithoutReplacingModel) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));if(!shell->workspace_input()->rust_editor())return;shell->OpenGeneralSettings();auto* picker=shell->theme_picker();ASSERT_TRUE(picker);picker->ScrollViewToVisible();shell->general_widget()->LayoutRootViewIfNecessary();auto* parent=picker->parent();ASSERT_GT(parent->children().size(),1u);const auto id=shell->sidebar_node_id_for_testing(picker),root=shell->scene_parent_id_for_testing(picker);ASSERT_NE(0u,root);ui::KeyEvent down(ui::EventType::kKeyPressed,ui::VKEY_DOWN,0);ASSERT_TRUE(picker->OnKeyPressed(down));ASSERT_TRUE(views::MenuController::GetActiveInstance());parent->ReorderChildView(picker,parent->children().front().get()==picker?parent->children().size()-1:0);ASSERT_TRUE(base::test::RunUntil([]{return !views::MenuController::GetActiveInstance();}));EXPECT_EQ(id,shell->sidebar_node_id_for_testing(picker));EXPECT_EQ(root,shell->scene_parent_id_for_testing(picker));EXPECT_EQ(3u,shell->control_model_kind_for_testing(picker));ui::AXNodeData ax;picker->GetViewAccessibility().GetAccessibleNodeData(&ax);EXPECT_TRUE(ax.HasState(ax::mojom::State::kCollapsed));auto held=parent->RemoveChildViewT(picker);EXPECT_EQ(0u,shell->scene_parent_id_for_testing(held.get()));window_.reset();EXPECT_FALSE(held->OnKeyPressed(down));
}

// Compiled only: all runtime GUI acceptance remains deferred by instruction.
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, FormActionNodesCoverPreferencesWorkspaceProfilesAndImport) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->SetWorkspaceAnimations(false);const bool rust=shell->workspace_input()->rust_editor();
  auto id=[&](views::View* view){EXPECT_TRUE(view);if(!view)return uint64_t{0};const auto value=shell->sidebar_node_id_for_testing(view);if(rust){EXPECT_NE(0u,value);EXPECT_NE(0u,shell->scene_parent_id_for_testing(view));}else EXPECT_EQ(0u,value);return value;};
  shell->OpenGeneralSettings();auto* save=shell->preference_save_button_for_testing("font_standard");const auto save_id=id(save);id(shell->preference_close_for_testing());auto* first=shell->preference_navigation_for_testing(0);auto* fonts=shell->preference_navigation_for_testing(4);const auto first_id=id(first),fonts_id=id(fonts);if(rust)EXPECT_NE(first_id,fonts_id);
  auto* clear=FindButton(shell->general_widget()->GetContentsView(),u"Wyczyść…");ASSERT_TRUE(clear);const auto clear_id=id(clear);shell->SetBrowsingDataBusy(true);EXPECT_EQ(clear,FindButton(shell->general_widget()->GetContentsView(),u"Czyszczenie…"));EXPECT_EQ(clear_id,id(clear));shell->SetBrowsingDataBusy(false);EXPECT_EQ(save_id,id(save));
  ActivateSidebarRow(fonts);EXPECT_EQ(4u,shell->preference_category_for_testing());EXPECT_EQ(fonts_id,id(fonts));if(rust)EXPECT_TRUE(shell->preference_navigation_selected_for_testing(4));
  shell->OpenWorkspaceSettings();auto* root=shell->workspace_panel_for_testing();auto* edit=FindButton(root,u"Zapisz nazwę i profil");auto* add=FindButton(root,u"Nowa przestrzeń…");ASSERT_TRUE(edit);ASSERT_TRUE(add);const auto edit_id=id(edit),add_id=id(add);if(rust)EXPECT_NE(edit_id,add_id);id(FindButton(root,u"#7FAACB"));id(FindButton(root,u"Emoji 🚀"));
  edit->parent()->ReorderChildView(edit,edit->parent()->children().size()-1);EXPECT_EQ(edit_id,id(edit));EXPECT_EQ(add_id,id(add));ClickControl(add);EXPECT_EQ(edit,FindButton(root,u"Utwórz przestrzeń"));EXPECT_EQ(edit_id,id(edit));EXPECT_EQ(add_id,id(add));ClickControl(add);
  shell->OpenProfiles();root=shell->profile_panel_for_testing();auto* create=FindButton(root,u"Utwórz profil");auto* rename=FindButton(root,u"Zapisz nazwę profilu");ASSERT_TRUE(create);ASSERT_TRUE(rename);const auto create_id=id(create),rename_id=id(rename);if(rust)EXPECT_NE(create_id,rename_id);id(FindButton(root,u"Usuń profil i jego dane"));shell->SetProfileStatus(u"Node status refresh");EXPECT_EQ(create_id,id(create));EXPECT_EQ(rename_id,id(rename));
  shell->OpenImport();root=shell->import_panel_for_testing();auto* apply=FindButton(root,u"Importuj");ASSERT_TRUE(apply);const auto apply_id=id(apply),close_id=id(shell->import_close());shell->SetImportStatus(u"Node busy",false,true,true);EXPECT_FALSE(apply->GetEnabled());EXPECT_EQ(apply_id,id(apply));EXPECT_EQ(close_id,id(shell->import_close()));ui::AXActionData activate;activate.action=ax::mojom::Action::kDoDefault;shell->import_close()->HandleAccessibleAction(activate);EXPECT_TRUE(shell->import_visible());shell->SetImportStatus(u"Node ready",false,false,false);EXPECT_EQ(apply_id,id(apply));EXPECT_EQ(close_id,id(shell->import_close()));
}

// Exercise actual dialog action groups; retained adapters must lose their Rust lease.
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, FormActionNodesRetireRemovedAdaptersAndSurviveSiblingDestruction) {
  auto* shell=window_->shell();if(!shell->workspace_input()->rust_editor())return;int accepted=0;auto delegate=CreateSceneDialogDelegate();delegate->SetOwnershipOfNewWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);delegate->SetContentsView(std::make_unique<views::View>())->SetPreferredSize(gfx::Size(220,80));delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk)|static_cast<int>(ui::mojom::DialogButton::kCancel));delegate->SetAcceptCallbackWithClose(base::BindRepeating([](int* count){++*count;return false;},&accepted));
  std::unique_ptr<views::Widget> widget(views::DialogDelegate::CreateDialogWidget(delegate.get(),window_->widget()->GetNativeWindow(),window_->widget()->GetNativeView()));widget->Show();widget->LayoutRootViewIfNecessary();auto* ok=DialogActionButton(widget.get(),true);auto* cancel=DialogActionButton(widget.get(),false);ASSERT_TRUE(ok);ASSERT_TRUE(cancel);auto* group=ok->parent();ASSERT_EQ(group,cancel->parent());const auto ok_id=shell->sidebar_node_id_for_testing(ok),cancel_id=shell->sidebar_node_id_for_testing(cancel);EXPECT_NE(0u,ok_id);EXPECT_NE(0u,cancel_id);EXPECT_NE(ok_id,cancel_id);EXPECT_TRUE(ok->GetViewAccessibility().GetIsDefault());
  group->ReorderChildView(ok,0);EXPECT_EQ(ok_id,shell->sidebar_node_id_for_testing(ok));auto held=group->RemoveChildViewT(ok);EXPECT_EQ(0u,shell->sidebar_node_id_for_testing(held.get()));EXPECT_EQ(cancel_id,shell->sidebar_node_id_for_testing(cancel));ui::KeyEvent space(ui::EventType::kKeyPressed,ui::VKEY_SPACE,0);EXPECT_FALSE(held->OnKeyPressed(space));EXPECT_EQ(0,accepted);
  // Deletion reaches the parent's hierarchy hook after derived members may die;
  // retirement must use the registered ID instead of dereferencing child state.
  group->RemoveAllChildViews();EXPECT_EQ(0u,shell->sidebar_node_id_for_testing(held.get()));widget->CloseNow();widget.reset();EXPECT_EQ(0u,shell->sidebar_node_id_for_testing(held.get()));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, TextActionHostsDialogDefaultsAndElementActivation) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));const bool rust=shell->workspace_input()->rust_editor();int accepted=0,cancelled=0,ok_activated=0,cancel_activated=0;
  auto delegate=rust?CreateSceneDialogDelegate():std::make_unique<views::DialogDelegate>();delegate->SetOwnershipOfNewWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);delegate->SetModalType(ui::mojom::ModalType::kWindow);delegate->SetContentsView(std::make_unique<views::View>())->SetPreferredSize(gfx::Size(220,80));delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk)|static_cast<int>(ui::mojom::DialogButton::kCancel));delegate->SetDefaultButton(static_cast<int>(ui::mojom::DialogButton::kOk));delegate->SetButtonLabel(ui::mojom::DialogButton::kOk,u"Apply test");delegate->SetButtonLabel(ui::mojom::DialogButton::kCancel,u"Cancel test");delegate->SetAcceptCallbackWithClose(base::BindRepeating([](int* count){++*count;return false;},&accepted));delegate->SetCancelCallbackWithClose(base::BindRepeating([](int* count){++*count;return false;},&cancelled));
  std::unique_ptr<views::Widget> widget(views::DialogDelegate::CreateDialogWidget(delegate.get(),window_->widget()->GetNativeWindow(),window_->widget()->GetNativeView()));
#if BUILDFLAG(IS_MAC)
  InstallWorkspaceKeyboardRouting(widget.get(),true);
#endif
  widget->Show();widget->LayoutRootViewIfNecessary();auto* ok=DialogActionButton(widget.get(),true);auto* cancel=DialogActionButton(widget.get(),false);ASSERT_TRUE(ok);ASSERT_TRUE(cancel);
  for(auto* action:{ok,cancel}){EXPECT_EQ(!rust,views::AsViewClass<views::Button>(action)!=nullptr);if(rust)EXPECT_TRUE(action->children().empty());ui::AXNodeData data;action->GetViewAccessibility().GetAccessibleNodeData(&data);EXPECT_EQ(ax::mojom::Role::kButton,data.role);}EXPECT_TRUE(ok->GetViewAccessibility().GetIsDefault());
  const auto context=views::ElementTrackerViews::GetContextForWidget(widget.get());auto activated=ui::ElementTracker::GetElementTracker()->AddElementActivatedCallback(views::DialogClientView::kOkButtonElementId,context,base::BindRepeating([](int* count,ui::TrackedElement*){++*count;},&ok_activated));auto cancel_activation=ui::ElementTracker::GetElementTracker()->AddElementActivatedCallback(views::DialogClientView::kCancelButtonElementId,context,base::BindRepeating([](int* count,ui::TrackedElement*){++*count;},&cancel_activated));
  ui_controls::EnableUIControls();ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(widget->GetNativeWindow()));cancel->RequestFocus();ASSERT_TRUE(cancel->HasFocus());base::test::TestFuture<void> presented;auto* compositor=widget->GetCompositor();ASSERT_TRUE(compositor);widget->GetRootView()->SchedulePaint();compositor->RequestSuccessfulPresentationTimeForNextFrame(base::BindOnce([](base::OnceClosure done,const viz::FrameTimingDetails&){std::move(done).Run();},presented.GetCallback()));compositor->ScheduleFullRedraw();ASSERT_TRUE(presented.Wait());delegate->ResetViewShownTimeStampForTesting();
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(widget->GetNativeWindow(),ui::VKEY_RETURN,false,false,false,false));ASSERT_TRUE(base::test::RunUntil([&]{return accepted==1&&ok_activated==1;}));EXPECT_TRUE(widget->IsVisible());delegate->SetButtonEnabled(ui::mojom::DialogButton::kOk,false);widget->LayoutRootViewIfNecessary();ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(widget->GetNativeWindow(),ui::VKEY_RETURN,false,false,false,false));ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(widget->GetNativeWindow(),ui::VKEY_SPACE,false,false,false,false));ASSERT_TRUE(base::test::RunUntil([&]{return cancelled==1&&cancel_activated==1;}));EXPECT_EQ(1,accepted);EXPECT_EQ(1,ok_activated);
  delegate->SetButtonEnabled(ui::mojom::DialogButton::kOk,true);delegate->SetButtonLabel(ui::mojom::DialogButton::kOk,u"Apply replacement 🦀");delegate->SetDefaultButton(static_cast<int>(ui::mojom::DialogButton::kCancel));widget->LayoutRootViewIfNecessary();ok=DialogActionButton(widget.get(),true);cancel=DialogActionButton(widget.get(),false);ASSERT_TRUE(ok);ASSERT_TRUE(cancel);ui::AXNodeData renamed;ok->GetViewAccessibility().GetAccessibleNodeData(&renamed);EXPECT_EQ(u"Apply replacement 🦀",renamed.GetString16Attribute(ax::mojom::StringAttribute::kName));EXPECT_EQ(delegate->GetIsDefault(ui::mojom::DialogButton::kCancel),cancel->GetViewAccessibility().GetIsDefault());cancel->RequestFocus();ASSERT_TRUE(cancel->HasFocus());delegate->ResetViewShownTimeStampForTesting();const int before_cancel=cancelled;
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(widget->GetNativeWindow(),ui::VKEY_RETURN,false,false,false,false));const int expected_cancel=before_cancel+(views::PlatformStyle::kDialogDefaultButtonCanBeCancel?1:0)+1;ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(widget->GetNativeWindow(),ui::VKEY_SPACE,false,false,false,false));ASSERT_TRUE(base::test::RunUntil([&]{return cancelled==expected_cancel&&cancel_activated==expected_cancel;}));EXPECT_EQ(1,accepted);EXPECT_EQ(1,ok_activated);widget->CloseNow();
}

// Compile only: retaining a removed adapter must not let activation reach its callback.
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, FormActionObserverCannotActivateRetainedRetiredNode) {
  auto* shell=window_->shell();if(!shell->workspace_input()->rust_editor())return;int accepted=0,activated=0;auto delegate=CreateSceneDialogDelegate();delegate->SetOwnershipOfNewWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);delegate->SetContentsView(std::make_unique<views::View>())->SetPreferredSize(gfx::Size(220,80));delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk));delegate->SetAcceptCallbackWithClose(base::BindRepeating([](int* count){++*count;return false;},&accepted));
  std::unique_ptr<views::Widget> widget(views::DialogDelegate::CreateDialogWidget(delegate.get(),window_->widget()->GetNativeWindow(),window_->widget()->GetNativeView()));widget->Show();widget->LayoutRootViewIfNecessary();auto* ok=DialogActionButton(widget.get(),true);ASSERT_TRUE(ok);EXPECT_NE(0u,shell->sidebar_node_id_for_testing(ok));views::ViewTracker alive(ok);std::unique_ptr<views::View> held;
  auto observer=ui::ElementTracker::GetElementTracker()->AddElementActivatedCallback(views::DialogClientView::kOkButtonElementId,views::ElementTrackerViews::GetContextForWidget(widget.get()),base::BindRepeating([](views::View* action,std::unique_ptr<views::View>* held,int* count,ui::TrackedElement*){++*count;*held=action->parent()->RemoveChildViewT(action);},base::Unretained(ok),&held,&activated));delegate->ResetViewShownTimeStampForTesting();ClickControl(ok);EXPECT_EQ(1,activated);ASSERT_TRUE(held);EXPECT_EQ(ok,alive.view());EXPECT_EQ(0u,shell->sidebar_node_id_for_testing(held.get()));EXPECT_EQ(0,accepted);EXPECT_TRUE(widget->IsVisible());widget->CloseNow();widget.reset();EXPECT_EQ(0u,shell->sidebar_node_id_for_testing(held.get()));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, TextActionHostsActivationObserverCanDeleteHost) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));if(!shell->workspace_input()->rust_editor())return;int accepted=0,activated=0;auto delegate=CreateSceneDialogDelegate();delegate->SetOwnershipOfNewWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);delegate->SetContentsView(std::make_unique<views::View>())->SetPreferredSize(gfx::Size(220,80));delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk));delegate->SetAcceptCallbackWithClose(base::BindRepeating([](int* count){++*count;return false;},&accepted));std::unique_ptr<views::Widget> widget(views::DialogDelegate::CreateDialogWidget(delegate.get(),window_->widget()->GetNativeWindow(),window_->widget()->GetNativeView()));widget->Show();widget->LayoutRootViewIfNecessary();auto* ok=DialogActionButton(widget.get(),true);ASSERT_TRUE(ok);views::ViewTracker alive(ok);
  auto event=ui::ElementTracker::GetElementTracker()->AddElementActivatedCallback(views::DialogClientView::kOkButtonElementId,views::ElementTrackerViews::GetContextForWidget(widget.get()),base::BindRepeating([](views::View* action,int* count,ui::TrackedElement*){++*count;action->parent()->RemoveChildViewT(action);},base::Unretained(ok),&activated));delegate->ResetViewShownTimeStampForTesting();ClickControl(ok);EXPECT_EQ(1,activated);EXPECT_EQ(0,accepted);EXPECT_FALSE(alive.view());EXPECT_TRUE(widget->IsVisible());widget->CloseNow();
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, PreferenceActionHostsAXSelectionClippingAndStableHover) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready()&&shell->tools_ready();}));shell->SetWorkspaceAnimations(false);window_->widget()->SetBounds(gfx::Rect(40,40,1000,860));shell->OpenGeneralSettings();auto* widget=shell->general_widget();widget->LayoutRootViewIfNecessary();const bool rust=shell->workspace_input()->rust_editor();
  for(size_t i=0;i<10;++i){auto* view=shell->preference_navigation_for_testing(i);ASSERT_TRUE(view);EXPECT_EQ(!rust,views::AsViewClass<views::Button>(view)!=nullptr);ui::AXNodeData ax;view->GetViewAccessibility().GetAccessibleNodeData(&ax);EXPECT_EQ(ax::mojom::Role::kButton,ax.role);EXPECT_FALSE(ax.GetString16Attribute(ax::mojom::StringAttribute::kName).empty());if(rust)EXPECT_TRUE(view->children().empty());}
  auto* first=shell->preference_navigation_for_testing(0);auto* languages=shell->preference_navigation_for_testing(7);auto* fonts=shell->preference_navigation_for_testing(4);ActivateSidebarRow(fonts);EXPECT_EQ(4u,shell->preference_category_for_testing());EXPECT_TRUE(shell->preference_navigation_selected_for_testing(4));
  ui::KeyEvent enter(ui::EventType::kKeyPressed,ui::VKEY_RETURN,0);EXPECT_FALSE(languages->OnKeyPressed(enter));EXPECT_EQ(4u,shell->preference_category_for_testing());languages->SetEnabled(false);ui::AXActionData activate;activate.action=ax::mojom::Action::kDoDefault;languages->HandleAccessibleAction(activate);EXPECT_EQ(4u,shell->preference_category_for_testing());languages->SetEnabled(true);ActivateSidebarRow(first);EXPECT_EQ(0u,shell->preference_category_for_testing());
  auto press=[](views::View* view){ui::MouseEvent event(ui::EventType::kMousePressed,gfx::Point(20,20),gfx::Point(20,20),base::TimeTicks::Now(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);return view->OnMousePressed(event);};auto release=[](views::View* view){ui::MouseEvent event(ui::EventType::kMouseReleased,gfx::Point(20,20),gfx::Point(20,20),base::TimeTicks::Now(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);view->OnMouseReleased(event);};
  if(rust){ASSERT_TRUE(press(languages));auto* group=languages->parent();const auto bounds=group->bounds();auto clipped=bounds;clipped.set_height(20);group->SetBoundsRect(clipped);EXPECT_FALSE(group->GetVisibleBounds().IsEmpty());EXPECT_TRUE(languages->IsDrawn());EXPECT_TRUE(languages->GetVisibleBounds().IsEmpty());group->SetBoundsRect(bounds);release(languages);EXPECT_EQ(0u,shell->preference_category_for_testing());
    ASSERT_TRUE(press(languages));languages->SetEnabled(false);languages->SetEnabled(true);release(languages);EXPECT_EQ(0u,shell->preference_category_for_testing());ASSERT_TRUE(press(languages));languages->SetVisible(false);languages->SetVisible(true);widget->LayoutRootViewIfNecessary();release(languages);EXPECT_EQ(0u,shell->preference_category_for_testing());
    ui_controls::EnableUIControls();ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(widget->GetNativeWindow()));first->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);first->RequestFocus();ASSERT_TRUE(first->HasFocus());const auto outside=widget->GetWindowBoundsInScreen().CenterPoint();base::test::TestFuture<void> moved;ASSERT_TRUE(ui_controls::SendMouseMoveNotifyWhenDone(outside.x(),outside.y(),moved.GetCallback(),widget->GetNativeWindow()));ASSERT_TRUE(moved.Wait());
    base::test::TestFuture<void> presented;auto* compositor=widget->GetCompositor();ASSERT_TRUE(compositor);widget->GetRootView()->SchedulePaint();compositor->RequestSuccessfulPresentationTimeForNextFrame(base::BindOnce([](base::OnceClosure done,const viz::FrameTimingDetails&){std::move(done).Run();},presented.GetCallback()));compositor->ScheduleFullRedraw();ASSERT_TRUE(presented.Wait());ASSERT_TRUE(base::test::RunUntil([&]{return shell->preference_navigation_metal_visible_for_testing();}));
    ui::MouseEvent exit(ui::EventType::kMouseExited,gfx::Point(-1,-1),gfx::Point(-1,-1),base::TimeTicks::Now(),0,0);languages->OnMouseExited(exit);base::RunLoop().RunUntilIdle();const auto before=shell->preference_navigation_frame_count_for_testing();ui::MouseEvent entered(ui::EventType::kMouseEntered,gfx::Point(20,20),gfx::Point(20,20),base::TimeTicks::Now(),0,0);languages->OnMouseEntered(entered);ASSERT_TRUE(base::test::RunUntil([&]{return shell->preference_navigation_frame_count_for_testing()>before;}));base::RunLoop().RunUntilIdle();const auto settled=shell->preference_navigation_frame_count_for_testing();
    for(int i=0;i<20;++i){ui::MouseEvent move(ui::EventType::kMouseMoved,gfx::Point(20,20),gfx::Point(20,20),base::TimeTicks::Now(),0,0);languages->OnMouseMoved(move);}base::RunLoop().RunUntilIdle();EXPECT_EQ(settled,shell->preference_navigation_frame_count_for_testing());languages->OnMouseExited(exit);
  }
  auto* close=shell->preference_close_for_testing();EXPECT_EQ(!rust,views::AsViewClass<views::Button>(close)!=nullptr);if(rust)EXPECT_TRUE(close->children().empty());ActivateSidebarRow(close);EXPECT_FALSE(widget->IsVisible());shell->OpenTools(2);auto* tools_close=shell->tools_close_for_testing();ASSERT_TRUE(tools_close);EXPECT_EQ(!rust,views::AsViewClass<views::Button>(tools_close)!=nullptr);if(rust)EXPECT_TRUE(tools_close->children().empty());ui_controls::EnableUIControls();ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->tools_widget()->GetNativeWindow()));tools_close->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);tools_close->RequestFocus();ASSERT_TRUE(tools_close->HasFocus());ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(shell->tools_widget()->GetNativeWindow(),ui::VKEY_SPACE,false,false,false,false));EXPECT_FALSE(shell->tools_widget()->IsVisible());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, AppearanceActionHostsKeyboardAXColorAndCancelledClip) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->SetWorkspaceAnimations(false);shell->OpenWorkspaceSettings();auto* widget=shell->settings_widget();widget->LayoutRootViewIfNecessary();const bool rust=shell->workspace_input()->rust_editor();auto* colors=shell->color_input()->view()->parent();auto* emojis=shell->emoji_input()->view()->parent();
  for(auto* row:{colors,emojis})for(size_t i=0;i<(row==colors?4u:6u);++i){auto* view=row->children()[i].get();EXPECT_EQ(!rust,views::AsViewClass<views::Button>(view)!=nullptr);ui::AXNodeData ax;view->GetViewAccessibility().GetAccessibleNodeData(&ax);EXPECT_EQ(ax::mojom::Role::kButton,ax.role);EXPECT_FALSE(ax.GetString16Attribute(ax::mojom::StringAttribute::kName).empty());if(rust)EXPECT_TRUE(view->children().empty());}
  auto* mint=colors->children()[1].get();mint->ScrollViewToVisible();ActivateSidebarRow(mint);EXPECT_EQ(0xFF84B6A4u,shell->workspace_color("work"));auto* blue=colors->children()[3].get();blue->ScrollViewToVisible();
  ui_controls::EnableUIControls();ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(widget->GetNativeWindow()));blue->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);blue->RequestFocus();ASSERT_TRUE(base::test::RunUntil([&]{return blue->HasFocus();}));base::test::TestFuture<void> presented;auto* compositor=widget->GetCompositor();ASSERT_TRUE(compositor);widget->GetRootView()->SchedulePaint();compositor->RequestSuccessfulPresentationTimeForNextFrame(base::BindOnce([](base::OnceClosure done,const viz::FrameTimingDetails&){std::move(done).Run();},presented.GetCallback()));compositor->ScheduleFullRedraw();ASSERT_TRUE(presented.Wait());
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(widget->GetNativeWindow(),ui::VKEY_SPACE,false,false,false,false));EXPECT_EQ(0xFF7FAACBu,shell->workspace_color("work"));EXPECT_EQ(u"#7FAACB",shell->color_input()->GetText());auto* rocket=emojis->children()[2].get();rocket->ScrollViewToVisible();ActivateSidebarRow(rocket);EXPECT_EQ(u"🚀",shell->workspace_emoji("work"));
  mint->SetEnabled(false);ui::AXActionData activate;activate.action=ax::mojom::Action::kDoDefault;mint->HandleAccessibleAction(activate);EXPECT_EQ(0xFF7FAACBu,shell->workspace_color("work"));mint->SetEnabled(true);mint->ScrollViewToVisible();
  auto press=[](views::View* view){ui::MouseEvent event(ui::EventType::kMousePressed,gfx::Point(12,12),gfx::Point(12,12),base::TimeTicks::Now(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);return view->OnMousePressed(event);};auto release=[](views::View* view){ui::MouseEvent event(ui::EventType::kMouseReleased,gfx::Point(12,12),gfx::Point(12,12),base::TimeTicks::Now(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);view->OnMouseReleased(event);};
  if(rust){auto* viewport=shell->workspace_viewport_for_testing();auto* overflow=viewport->contents()->AddChildView(std::make_unique<FormOverflowProbeView>());widget->LayoutRootViewIfNecessary();mint->ScrollViewToVisible();const auto offset=viewport->offset();ASSERT_GT(viewport->maximum(),0);ASSERT_TRUE(press(mint));viewport->ScrollTo(viewport->maximum());EXPECT_TRUE(mint->GetVisibleBounds().IsEmpty());viewport->ScrollTo(offset);release(mint);EXPECT_EQ(0xFF7FAACBu,shell->workspace_color("work"));viewport->contents()->RemoveChildViewT(overflow);widget->LayoutRootViewIfNecessary();mint->ScrollViewToVisible();
    ASSERT_TRUE(press(mint));widget->Hide();widget->Show();release(mint);EXPECT_EQ(0xFF7FAACBu,shell->workspace_color("work"));
  }
  // The shared scene and native comparison must both preserve the actual swatch
  // fill in fallback, not just apply the corresponding workspace store value.
  widget->Hide();const auto bounds=colors->bounds();colors->SetBoundsRect(gfx::Rect(colors->size()));const auto pixel=mint->bounds().CenterPoint();const auto bitmap=views::test::PaintViewToBitmap(colors);colors->SetBoundsRect(bounds);ASSERT_LT(pixel.x(),bitmap.width());ASSERT_LT(pixel.y(),bitmap.height());EXPECT_EQ(0xFF84B6A4u,bitmap.getColor(pixel.x(),pixel.y()));widget->Show();
  if(rust){mint->ScrollViewToVisible();const auto personal=shell->workspace_color("personal");ASSERT_TRUE(press(mint));ASSERT_TRUE(window_->Select("personal"));release(mint);EXPECT_EQ(personal,shell->workspace_color("personal"));EXPECT_EQ(0xFF7FAACBu,shell->workspace_color("work"));}
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_saved();}));
}

// Compiled only: retained native focus/AX and keyboard/callback behavior await the checkpoint.
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, PaletteSceneReusesRowsAcrossKeyboardTitleOrderAndDuplicates) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));if(!shell->workspace_input()->rust_editor())return;window_->FocusAddressBar();SetPaletteQuery(u"Scene palette");std::vector<PaletteResult> results;for(int i=0;i<3;++i){PaletteResult item;item.title=u"Scene "+base::NumberToString16(i);item.detail=u"Detail 🦀";item.url=embedded_test_server()->GetURL("/storage_probe.html?palette-scene-"+base::NumberToString(i));results.push_back(item);}shell->SetSearchResults(u"Scene palette","work-profile",results);auto* list=shell->palette_results_view();
  auto find=[&](const GURL& url)->views::View*{for(size_t i=0;i<shell->suggestions().size();++i)if(shell->suggestions()[i].url==url)return list->children()[i].get();return nullptr;};const auto url=results[0].url;auto* first=find(url);ASSERT_TRUE(first);const auto id=shell->sidebar_node_id_for_testing(first),root=shell->scene_parent_id_for_testing(first);EXPECT_NE(0u,root);EXPECT_EQ(6u,shell->control_model_kind_for_testing(first));
  first->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);first->ScrollViewToVisible();first->RequestFocus();ASSERT_TRUE(first->HasFocus());shell->SetSearchResults(u"Scene palette","work-profile",results);EXPECT_EQ(first,find(url));EXPECT_TRUE(first->HasFocus());results[0].title=u"Renamed 日本語 🦀";shell->SetSearchResults(u"Scene palette","work-profile",results);EXPECT_EQ(first,find(url));EXPECT_TRUE(first->HasFocus());ui::AXNodeData ax;first->GetViewAccessibility().GetAccessibleNodeData(&ax);EXPECT_EQ(results[0].title,ax.GetString16Attribute(ax::mojom::StringAttribute::kName));
  ui::KeyEvent down(ui::EventType::kKeyPressed,ui::VKEY_DOWN,0);EXPECT_TRUE(shell->HandleAddressKey(down));EXPECT_TRUE(shell->HandleAddressKey(down));EXPECT_EQ(first,find(url));EXPECT_EQ(2u,shell->palette_selection_for_testing());std::swap(results[0],results[2]);shell->SetSearchResults(u"Scene palette","work-profile",results);EXPECT_EQ(first,find(url));EXPECT_EQ(id,shell->sidebar_node_id_for_testing(first));EXPECT_EQ(root,shell->scene_parent_id_for_testing(first));results.push_back(results[2]);shell->SetSearchResults(u"Scene palette","work-profile",results);std::vector<uint64_t> duplicates;for(size_t i=0;i<shell->suggestions().size();++i)if(shell->suggestions()[i].url==url)duplicates.push_back(shell->sidebar_node_id_for_testing(list->children()[i].get()));ASSERT_EQ(2u,duplicates.size());EXPECT_EQ(id,duplicates[0]);EXPECT_NE(duplicates[0],duplicates[1]);
  auto held=first->parent()->RemoveChildViewT(first);SetPaletteQuery(u"Changed scene query");EXPECT_EQ(0u,shell->sidebar_node_id_for_testing(held.get()));EXPECT_EQ(0u,shell->scene_parent_id_for_testing(held.get()));EXPECT_EQ(0u,shell->control_model_kind_for_testing(held.get()));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, PaletteSceneReorderCancelsPressAndResolvesCurrentCallbackIndex) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));if(!shell->workspace_input()->rust_editor())return;window_->FocusAddressBar();SetPaletteQuery(u"Scene target");std::vector<PaletteResult> results;for(int i=0;i<2;++i){PaletteResult item;item.title=u"Target "+base::NumberToString16(i);item.url=embedded_test_server()->GetURL("/storage_probe.html?palette-target-"+base::NumberToString(i));results.push_back(item);}shell->SetSearchResults(u"Scene target","work-profile",results);auto* list=shell->palette_results_view();auto find=[&](const GURL& url)->views::View*{for(size_t i=0;i<shell->suggestions().size();++i)if(shell->suggestions()[i].url==url)return list->children()[i].get();return nullptr;};const auto target=results[0].url;auto* first=find(target);ASSERT_TRUE(first);first->ScrollViewToVisible();shell->address_input()->view()->GetWidget()->LayoutRootViewIfNecessary();ui::MouseEvent press(ui::EventType::kMousePressed,gfx::Point(12,12),gfx::Point(12,12),base::TimeTicks(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);ASSERT_TRUE(first->OnMousePressed(press));std::swap(results[0],results[1]);shell->SetSearchResults(u"Scene target","work-profile",results);ASSERT_EQ(first,find(target));ui::MouseEvent release(ui::EventType::kMouseReleased,gfx::Point(12,12),gfx::Point(12,12),base::TimeTicks(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);first->OnMouseReleased(release);EXPECT_TRUE(shell->address_visible());ActivateSidebarRow(first);ASSERT_TRUE(base::test::RunUntil([&]{return !shell->address_visible();}));ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));EXPECT_EQ(target,window_->contents("work")->GetLastCommittedURL());
}

// Compile-only until the requested bulk UI checkpoint.
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, PaletteLogicalNodesFollowTargetsAcrossTitleOrderAndDuplicateRefresh) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));window_->FocusAddressBar();SetPaletteQuery(u"Palette nodes");
  const bool rust=shell->workspace_input()->rust_editor();std::vector<PaletteResult> results;for(int i=0;i<2;++i){PaletteResult item;item.title=u"Target "+base::NumberToString16(i);item.url=embedded_test_server()->GetURL("/storage_probe.html?palette-node-"+base::NumberToString(i));results.push_back(std::move(item));}
  shell->SetSearchResults(u"Palette nodes","work-profile",results);auto* list=shell->palette_results_view();
  auto find=[&](const GURL& url)->views::View*{const auto& items=shell->suggestions();for(size_t i=0;i<items.size();++i)if(items[i].url==url)return list->children()[i].get();return nullptr;};
  const auto target=results[0].url;auto* first=find(target);ASSERT_TRUE(first);const auto id=shell->sidebar_node_id_for_testing(first);if(rust)EXPECT_NE(0u,id);else EXPECT_EQ(0u,id);
  auto held=first->parent()->RemoveChildViewT(first);results[0].title=u"Renamed target";std::swap(results[0],results[1]);shell->SetSearchResults(u"Palette nodes","work-profile",results);auto* replacement=find(target);ASSERT_TRUE(replacement);EXPECT_EQ(id,shell->sidebar_node_id_for_testing(replacement));
  if(rust){EXPECT_EQ(0u,shell->sidebar_node_id_for_testing(held.get()));ui::KeyEvent space(ui::EventType::kKeyPressed,ui::VKEY_SPACE,0);held->OnKeyPressed(space);EXPECT_TRUE(shell->address_visible());}
  results.push_back(results[1]);shell->SetSearchResults(u"Palette nodes","work-profile",results);std::vector<uint64_t> duplicates;for(size_t i=0;i<shell->suggestions().size();++i)if(shell->suggestions()[i].url==target)duplicates.push_back(shell->sidebar_node_id_for_testing(list->children()[i].get()));ASSERT_EQ(2u,duplicates.size());if(rust){EXPECT_EQ(id,duplicates[0]);EXPECT_NE(duplicates[0],duplicates[1]);}
  auto* last=find(target);ASSERT_TRUE(last);auto held_last=last->parent()->RemoveChildViewT(last);SetPaletteQuery(u"Other query");if(rust)EXPECT_EQ(0u,shell->sidebar_node_id_for_testing(held_last.get()));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, ResultRowHostsAddressPaletteAXAndPendingReplacement) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->SetWorkspaceAnimations(false);
  const auto initial=embedded_test_server()->GetURL("/storage_probe.html?result-host");ASSERT_TRUE(window_->NavigateFromBar(initial,false));ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));ASSERT_TRUE(content::ExecJs(window_->contents("work"),"document.title='Result host sentinel';"));shell->SetPinned(true);shell->ShowSidebar();shell->sidebar_widget()->LayoutRootViewIfNecessary();const bool rust=shell->workspace_input()->rust_editor();
  auto* address=SidebarNamedButton(shell->sidebar_view(),u"Adres strony");ASSERT_TRUE(address);EXPECT_EQ(!rust,views::AsViewClass<views::Button>(address)!=nullptr);ui::AXNodeData ax;address->GetViewAccessibility().GetAccessibleNodeData(&ax);EXPECT_EQ(ax::mojom::Role::kButton,ax.role);EXPECT_FALSE(ax.GetString16Attribute(ax::mojom::StringAttribute::kDescription).empty());ActivateSidebarRow(address);ASSERT_TRUE(shell->address_visible());SetPaletteQuery(u"Result host");
  ASSERT_TRUE(base::test::RunUntil([&]{return std::any_of(shell->search_results().begin(),shell->search_results().end(),[](const auto& item){return item.title==u"Result host sentinel";});}));
  auto* service=HistoryServiceFactory::GetForProfile(work_,ServiceAccessType::EXPLICIT_ACCESS);base::CancelableTaskTracker tasks;history::QueryOptions options;options.max_count=1;base::test::TestFuture<history::QueryResults> queried;service->QueryHistory(u"Result host",options,queried.GetCallback(),&tasks);ASSERT_TRUE(queried.Wait());base::RunLoop().RunUntilIdle();
  std::vector<PaletteResult> results;for(int i=0;i<30;++i){PaletteResult item;item.title=u"Result host 日本語 "+base::NumberToString16(i);item.detail=u"History / 🦀";item.url=embedded_test_server()->GetURL("/storage_probe.html?palette-result-"+base::NumberToString(i));results.push_back(std::move(item));}
  shell->SetSearchResults(u"Result host","work-profile",results);auto* panel=shell->address_input()->view()->GetWidget();panel->LayoutRootViewIfNecessary();auto* viewport=shell->palette_viewport();ASSERT_GT(viewport->maximum(),0);auto* list=shell->palette_results_view();ASSERT_EQ(shell->suggestions().size(),list->children().size());
  for(const auto& child:list->children()){EXPECT_EQ(!rust,views::AsViewClass<views::Button>(child)!=nullptr);ui::AXNodeData node;child->GetViewAccessibility().GetAccessibleNodeData(&node);EXPECT_EQ(ax::mojom::Role::kButton,node.role);if(rust)for(const auto& descendant:child->children())EXPECT_FALSE(views::AsViewClass<views::Label>(descendant.get()));}
  auto press=[](views::View* row){const auto point=row->GetLocalBounds().CenterPoint();ui::MouseEvent event(ui::EventType::kMousePressed,point,point,base::TimeTicks::Now(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);return row->OnMousePressed(event);};auto release=[](views::View* row){const auto point=row->GetLocalBounds().CenterPoint();ui::MouseEvent event(ui::EventType::kMouseReleased,point,point,base::TimeTicks::Now(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);row->OnMouseReleased(event);};
  if(rust){auto* first=list->children().front().get();first->ScrollViewToVisible();ASSERT_TRUE(press(first));const auto offset=viewport->offset();viewport->ScrollTo(viewport->maximum());EXPECT_TRUE(first->GetVisibleBounds().IsEmpty());viewport->ScrollTo(offset);release(first);EXPECT_TRUE(shell->address_visible());EXPECT_EQ(initial,window_->contents("work")->GetLastCommittedURL());
    ASSERT_TRUE(press(first));first->SetEnabled(false);first->SetEnabled(true);release(first);EXPECT_TRUE(shell->address_visible());
    ASSERT_TRUE(press(first));panel->Hide();ui::KeyEvent space(ui::EventType::kKeyPressed,ui::VKEY_SPACE,0);EXPECT_FALSE(first->OnKeyPressed(space));panel->Show();release(first);EXPECT_TRUE(shell->address_visible());
    ASSERT_TRUE(press(first));views::ViewTracker old(first);results.front().title=u"Result host replacement";shell->SetSearchResults(u"Result host","work-profile",results);EXPECT_EQ(first,old.view());EXPECT_EQ(first,list->children().front().get());panel->LayoutRootViewIfNecessary();release(list->children().front().get());EXPECT_TRUE(shell->address_visible());
  }
  const auto expected=shell->suggestions().back().url;auto* last=list->children().back().get();ActivateSidebarRow(last);ASSERT_TRUE(base::test::RunUntil([&]{return !shell->address_visible();}));ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));EXPECT_EQ(expected,window_->contents("work")->GetLastCommittedURL());
}

// Compile only: native focus/AX and reused callback targets await the bulk checkpoint.
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, HistorySceneReusesRowsAcrossPresentationOrderAndMembership) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_ready();}));if(!shell->workspace_input()->rust_editor())return;shell->OpenTools(3);ASSERT_TRUE(base::test::RunUntil([&]{return shell->history_loaded();}));const auto visited=base::Time::Now()-base::Hours(1);std::vector<BrowsingHistoryEntry> entries={{embedded_test_server()->GetURL("/storage_probe.html?scene-a"),u"First",visited},{embedded_test_server()->GetURL("/storage_probe.html?scene-b"),u"Second",visited-base::Minutes(1)}};
  shell->SetHistoryResults(u"","work-profile",200,entries,false,false);auto* list=shell->history_results_view();ASSERT_EQ(2u,list->children().size());auto* first=list->children()[0].get();auto* second=list->children()[1].get();const auto id=shell->sidebar_node_id_for_testing(first),root=shell->scene_parent_id_for_testing(first);ASSERT_NE(0u,root);EXPECT_EQ(5u,shell->control_model_kind_for_testing(first));EXPECT_EQ(root,shell->scene_parent_id_for_testing(second));first->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);first->ScrollViewToVisible();first->RequestFocus();ASSERT_TRUE(first->HasFocus());
  shell->SetHistoryResults(u"","work-profile",200,entries,false,false);EXPECT_EQ(first,list->children()[0].get());EXPECT_TRUE(first->HasFocus());entries[0].title=u"Changed 日本語 🦀";shell->SetHistoryResults(u"","work-profile",200,entries,false,false);EXPECT_EQ(first,list->children()[0].get());EXPECT_TRUE(first->HasFocus());ui::AXNodeData ax;first->GetViewAccessibility().GetAccessibleNodeData(&ax);EXPECT_EQ(base::UTF8ToUTF16(entries[0].url.spec()),ax.GetString16Attribute(ax::mojom::StringAttribute::kName));EXPECT_FALSE(ax.GetString16Attribute(ax::mojom::StringAttribute::kDescription).empty());
  std::swap(entries[0],entries[1]);shell->SetHistoryResults(u"","work-profile",200,entries,false,false);EXPECT_EQ(second,list->children()[0].get());EXPECT_EQ(first,list->children()[1].get());EXPECT_EQ(id,shell->sidebar_node_id_for_testing(first));EXPECT_EQ(root,shell->scene_parent_id_for_testing(first));EXPECT_TRUE(first->HasFocus());
  auto held=second->parent()->RemoveChildViewT(second);entries.erase(entries.begin());shell->SetHistoryResults(u"","work-profile",200,entries,false,false);ASSERT_EQ(1u,list->children().size());EXPECT_EQ(first,list->children()[0].get());EXPECT_EQ(0u,shell->sidebar_node_id_for_testing(held.get()));EXPECT_EQ(0u,shell->scene_parent_id_for_testing(held.get()));EXPECT_EQ(0u,shell->control_model_kind_for_testing(held.get()));ui::KeyEvent space(ui::EventType::kKeyPressed,ui::VKEY_SPACE,0);EXPECT_FALSE(held->OnKeyPressed(space));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, HistorySceneReorderCancelsPressAndKeepsNativeCallbackTarget) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_ready();}));if(!shell->workspace_input()->rust_editor())return;shell->OpenTools(3);ASSERT_TRUE(base::test::RunUntil([&]{return shell->history_loaded();}));const auto visited=base::Time::Now()-base::Hours(1);std::vector<BrowsingHistoryEntry> entries={{embedded_test_server()->GetURL("/storage_probe.html?scene-target-a"),u"First",visited},{embedded_test_server()->GetURL("/storage_probe.html?scene-target-b"),u"Second",visited-base::Minutes(1)}};shell->SetHistoryResults(u"","work-profile",200,entries,false,false);auto* list=shell->history_results_view();auto* first=list->children()[0].get();first->ScrollViewToVisible();shell->tools_widget()->LayoutRootViewIfNecessary();const auto tabs=window_->tab_count("work");ui::MouseEvent press(ui::EventType::kMousePressed,gfx::Point(12,12),gfx::Point(12,12),base::TimeTicks(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);ASSERT_TRUE(first->OnMousePressed(press));std::swap(entries[0],entries[1]);shell->SetHistoryResults(u"","work-profile",200,entries,false,false);ASSERT_EQ(first,list->children()[1].get());ui::MouseEvent release(ui::EventType::kMouseReleased,gfx::Point(12,12),gfx::Point(12,12),base::TimeTicks(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);first->OnMouseReleased(release);base::RunLoop().RunUntilIdle();EXPECT_EQ(tabs,window_->tab_count("work"));EXPECT_TRUE(shell->history_visible());
  const auto expected=entries[1].url;ActivateSidebarRow(first);ASSERT_TRUE(base::test::RunUntil([&]{return !shell->history_visible()&&window_->tab_count("work")==tabs+1;}));ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));EXPECT_EQ(expected,window_->contents("work")->GetLastCommittedURL());
}

// Runtime deferred: compile this retained-adapter case for the bulk checkpoint.
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, HistoryLogicalNodesSurviveTitleAndOrderRefreshThenRetire) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_ready();}));shell->OpenTools(3);ASSERT_TRUE(base::test::RunUntil([&]{return shell->history_loaded();}));
  const bool rust=shell->workspace_input()->rust_editor();const auto visited=base::Time::Now()-base::Hours(1);
  std::vector<BrowsingHistoryEntry> entries={{embedded_test_server()->GetURL("/storage_probe.html?history-node-a"),u"First",visited},{embedded_test_server()->GetURL("/storage_probe.html?history-node-b"),u"Second",visited-base::Minutes(1)}};
  shell->SetHistoryResults(u"","work-profile",200,entries,false,false);auto* list=shell->history_results_view();ASSERT_EQ(2u,list->children().size());auto* first=list->children()[0].get();const auto id=shell->sidebar_node_id_for_testing(first);if(rust)EXPECT_NE(0u,id);else EXPECT_EQ(0u,id);
  auto held=first->parent()->RemoveChildViewT(first);entries[0].title=u"Updated title";std::swap(entries[0],entries[1]);shell->SetHistoryResults(u"","work-profile",200,entries,false,false);ASSERT_EQ(2u,list->children().size());auto* replacement=list->children()[1].get();EXPECT_EQ(id,shell->sidebar_node_id_for_testing(replacement));
  if(rust){EXPECT_EQ(0u,shell->sidebar_node_id_for_testing(held.get()));const auto tabs=window_->tab_count("work");ui::KeyEvent space(ui::EventType::kKeyPressed,ui::VKEY_SPACE,0);held->OnKeyPressed(space);base::RunLoop().RunUntilIdle();EXPECT_EQ(tabs,window_->tab_count("work"));}
  auto held_replacement=replacement->parent()->RemoveChildViewT(replacement);shell->SetHistoryResults(u"","work-profile",200,{},false,false);if(rust)EXPECT_EQ(0u,shell->sidebar_node_id_for_testing(held_replacement.get()));
  entries.push_back(entries[1]);shell->SetHistoryResults(u"","work-profile",200,entries,false,false);ASSERT_EQ(3u,list->children().size());if(rust){const auto a=shell->sidebar_node_id_for_testing(list->children()[1].get()),b=shell->sidebar_node_id_for_testing(list->children()[2].get());EXPECT_NE(0u,a);EXPECT_NE(a,b);EXPECT_NE(id,a);}
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, ResultRowHostsHistoryAXClipAndReplacementTarget) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_ready();}));const bool rust=shell->workspace_input()->rust_editor();history::URLRows seeds;
  for(int i=0;i<35;++i){history::URLRow row(embedded_test_server()->GetURL("/storage_probe.html?history-result-"+base::NumberToString(i)));row.set_title(u"Result history 日本語 "+base::NumberToString16(i));row.set_visit_count(1);row.set_last_visit(base::Time::Now()-base::Minutes(i+1));seeds.push_back(row);}HistoryServiceFactory::GetForProfile(work_,ServiceAccessType::EXPLICIT_ACCESS)->AddPagesWithDetails(seeds,history::SOURCE_OS_MIGRATION_IMPORTED);
  shell->OpenTools(3);ASSERT_TRUE(base::test::RunUntil([&]{return shell->history_loaded();}));ASSERT_EQ(35u,shell->history_entries().size());auto* list=shell->history_results_view();for(const auto& child:list->children()){EXPECT_EQ(!rust,views::AsViewClass<views::Button>(child)!=nullptr);if(rust)EXPECT_TRUE(child->children().empty());}
  auto* scroll=shell->tools_viewport_for_testing();ASSERT_GT(scroll->maximum(),0);auto* first=list->children().front().get();first->ScrollViewToVisible();const auto tabs=window_->tab_count("work");
  auto press=[](views::View* row){ui::MouseEvent event(ui::EventType::kMousePressed,gfx::Point(12,12),gfx::Point(12,12),base::TimeTicks::Now(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);return row->OnMousePressed(event);};auto release=[](views::View* row){ui::MouseEvent event(ui::EventType::kMouseReleased,gfx::Point(12,12),gfx::Point(12,12),base::TimeTicks::Now(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);row->OnMouseReleased(event);};
  if(rust){ASSERT_TRUE(press(first));const auto offset=scroll->offset();scroll->ScrollTo(scroll->maximum());EXPECT_TRUE(first->GetVisibleBounds().IsEmpty());scroll->ScrollTo(offset);release(first);base::RunLoop().RunUntilIdle();EXPECT_EQ(tabs,window_->tab_count("work"));EXPECT_TRUE(shell->history_visible());
    ASSERT_TRUE(press(first));shell->tools_widget()->Hide();shell->tools_widget()->Show();release(first);base::RunLoop().RunUntilIdle();EXPECT_EQ(tabs,window_->tab_count("work"));
    ASSERT_TRUE(press(first));views::ViewTracker old(first);auto replacement=shell->history_entries();replacement.front().title=u"Replacement history";shell->SetHistoryResults(u"","work-profile",200,replacement,false,false);EXPECT_EQ(first,old.view());EXPECT_EQ(first,list->children().front().get());shell->tools_widget()->LayoutRootViewIfNecessary();release(list->children().front().get());base::RunLoop().RunUntilIdle();EXPECT_EQ(tabs,window_->tab_count("work"));
  }
  const auto expected=shell->history_entries().back().url;auto* last=list->children().back().get();ActivateSidebarRow(last);ASSERT_TRUE(base::test::RunUntil([&]{return !shell->history_visible()&&window_->tab_count("work")==tabs+1;}));ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));EXPECT_EQ(expected,window_->contents("work")->GetLastCommittedURL());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, SidebarRowHostsOwnershipMenuKeysAXAndTitleUpdate) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->SetWorkspaceAnimations(false);
  const auto url=embedded_test_server()->GetURL("/storage_probe.html?row-host");ASSERT_TRUE(window_->NavigateFromBar(url,false));ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));ASSERT_TRUE(window_->NavigateFromBar(url,true));auto* second=window_->contents("work");ASSERT_TRUE(content::WaitForLoadStop(second));
  ASSERT_TRUE(window_->CreatePinnedFolder("work",{},"Host folder"));ASSERT_TRUE(base::test::RunUntil([&]{return !window_->profiles_busy();}));window_->SelectTab(0);shell->CloseOverlays(true);shell->SetPinned(true);shell->ShowSidebar();shell->sidebar_widget()->LayoutRootViewIfNecessary();const bool rust=shell->workspace_input()->rust_editor();
  auto* folder=SidebarNamedButton(shell->sidebar_view(),u"▾ Host folder");auto* fresh=SidebarNamedButton(shell->sidebar_view(),u"Nowa karta");ASSERT_TRUE(folder);ASSERT_TRUE(fresh);
  for(auto* view:{shell->tab_button(0),shell->tab_button(1),folder,fresh}){ASSERT_TRUE(view);EXPECT_EQ(!rust,views::AsViewClass<views::Button>(view)!=nullptr);ui::AXNodeData ax;view->GetViewAccessibility().GetAccessibleNodeData(&ax);EXPECT_EQ(ax::mojom::Role::kButton,ax.role);EXPECT_FALSE(ax.GetString16Attribute(ax::mojom::StringAttribute::kName).empty());if(rust)for(const auto& child:view->children())EXPECT_FALSE(views::AsViewClass<views::Button>(child.get()));}
#if BUILDFLAG(IS_MAC)
  bool inspected=false;EXPECT_TRUE(RunWithNativeMenuInspectionForTesting(base::BindOnce([](views::View* row){ui::AXActionData action;action.action=ax::mojom::Action::kShowContextMenu;EXPECT_TRUE(row->HandleAccessibleAction(action));},base::Unretained(shell->tab_button(1))),base::BindRepeating([](WorkspaceShell* shell,bool* inspected){EXPECT_TRUE(shell->workspace_context_menu_running_for_testing());EXPECT_TRUE(shell->tab_context_menu_for_testing());*inspected=true;},base::Unretained(shell),&inspected)));EXPECT_TRUE(inspected);EXPECT_EQ(2u,window_->tab_count("work"));
#endif
  ui::KeyEvent space(ui::EventType::kKeyPressed,ui::VKEY_SPACE,0);EXPECT_TRUE(shell->tab_button(1)->OnKeyPressed(space));EXPECT_EQ(second,window_->contents("work"));ActivateSidebarRow(shell->tab_button(0));EXPECT_NE(second,window_->contents("work"));
  auto* target=shell->tab_button(1);const auto point=target->GetLocalBounds().CenterPoint();ui::MouseEvent press(ui::EventType::kMousePressed,point,point,base::TimeTicks::Now(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);ui::MouseEvent release(ui::EventType::kMouseReleased,point,point,base::TimeTicks::Now(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);ASSERT_TRUE(target->OnMousePressed(press));views::ViewTracker tracked(target);
  ASSERT_TRUE(content::ExecJs(second,"document.title='Live row 日本語';"));ASSERT_TRUE(base::test::RunUntil([&]{return tracked.view()&&target->GetTooltipText()==u"Live row 日本語";}));EXPECT_EQ(target,shell->tab_button(1));target->OnMouseReleased(release);EXPECT_EQ(second,window_->contents("work"));
  ActivateSidebarRow(folder);EXPECT_TRUE(SidebarNamedButton(shell->sidebar_view(),u"▸ Host folder"));ActivateSidebarRow(folder);EXPECT_TRUE(SidebarNamedButton(shell->sidebar_view(),u"▾ Host folder"));ActivateSidebarRow(fresh);EXPECT_TRUE(shell->address_visible());shell->CloseOverlays(true);
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, SidebarRowHostsClipDisableHideAndReplacementCancelPendingInput) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));if(!shell->workspace_input()->rust_editor())return;shell->SetWorkspaceAnimations(false);
  const auto url=embedded_test_server()->GetURL("/storage_probe.html?row-cancel");for(int i=0;i<30;++i)ASSERT_TRUE(window_->NavigateFromBar(url,true));window_->SelectTab(0);auto* selected=window_->contents("work");shell->CloseOverlays(true);shell->SetPinned(true);shell->ShowSidebar();shell->sidebar_widget()->LayoutRootViewIfNecessary();auto* viewport=shell->tab_viewport();ASSERT_GT(viewport->maximum(),0);viewport->ScrollTo(0);
  auto press=[](views::View* view){const auto point=view->GetLocalBounds().CenterPoint();ui::MouseEvent event(ui::EventType::kMousePressed,point,point,base::TimeTicks::Now(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);return view->OnMousePressed(event);};
  auto release=[](views::View* view){const auto point=view->GetLocalBounds().CenterPoint();ui::MouseEvent event(ui::EventType::kMouseReleased,point,point,base::TimeTicks::Now(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);view->OnMouseReleased(event);};
  auto* row=shell->tab_button(1);ASSERT_TRUE(press(row));viewport->ScrollTo(viewport->maximum());EXPECT_TRUE(row->GetVisibleBounds().IsEmpty());viewport->ScrollTo(0);release(row);EXPECT_EQ(selected,window_->contents("work"));
  auto* close=SidebarNamedButton(shell->tab_button(0),u"Zamknij kartę");ASSERT_TRUE(close);ASSERT_TRUE(close->GetVisible());const auto count=window_->tab_count("work");ASSERT_TRUE(press(close));viewport->ScrollTo(viewport->maximum());viewport->ScrollTo(0);release(close);EXPECT_EQ(count,window_->tab_count("work"));
  ASSERT_TRUE(press(row));row->SetEnabled(false);row->SetEnabled(true);release(row);EXPECT_EQ(selected,window_->contents("work"));
  ASSERT_TRUE(press(row));shell->sidebar_widget()->Hide();ui::KeyEvent space(ui::EventType::kKeyPressed,ui::VKEY_SPACE,0);EXPECT_FALSE(row->OnKeyPressed(space));shell->ShowSidebar();release(row);EXPECT_EQ(selected,window_->contents("work"));
  // A native late release can reach the replacement node after a tree rebuild;
  // it must not inherit the captured row's press or activate the reused index.
  ASSERT_TRUE(press(row));views::ViewTracker old(row);window_->CloseTab(0);EXPECT_FALSE(old.view());auto* kept=window_->contents("work");shell->sidebar_widget()->LayoutRootViewIfNecessary();release(shell->tab_button(1));EXPECT_EQ(kept,window_->contents("work"));
  base::RunLoop().RunUntilIdle();const auto frames=shell->metal_sidebar_frame_count();base::RunLoop().RunUntilIdle();EXPECT_EQ(frames,shell->metal_sidebar_frame_count());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, SceneModalActionsNativeMetricsAndCocoaDefaults) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->SetWorkspaceAnimations(false);ui_controls::EnableUIControls();
  const bool rust=shell->workspace_input()->rust_editor();
  auto inspect=[&](views::Widget* widget,views::View* body,bool control){
    ASSERT_TRUE(widget);auto* delegate=widget->widget_delegate()->AsDialogDelegate();auto* ok=DialogActionButton(widget,true);auto* cancel=DialogActionButton(widget,false);ASSERT_TRUE(ok);ASSERT_TRUE(cancel);
    for(auto* button:{ok,cancel}){ui::AXNodeData node;button->GetViewAccessibility().GetAccessibleNodeData(&node);EXPECT_EQ(ax::mojom::Role::kButton,node.role);EXPECT_EQ(delegate->GetIsDefault(button==ok?ui::mojom::DialogButton::kOk:ui::mojom::DialogButton::kCancel),node.HasState(ax::mojom::State::kDefault));if(rust){EXPECT_TRUE(button->children().empty());EXPECT_FALSE(views::AsViewClass<views::MdTextButton>(button));}}
    if(rust){
      ASSERT_TRUE(body);ASSERT_TRUE(base::test::RunUntil([&]{return PageDialogActionsVisibleForTesting(ok->parent(),base::UTF16ToUTF8(delegate->GetDialogButtonLabel(ui::mojom::DialogButton::kOk)));}));EXPECT_FALSE(delegate->owned_by_widget());EXPECT_FALSE(delegate->GetOkButton());EXPECT_FALSE(delegate->GetCancelButton());
      auto oracle=std::make_unique<SceneModalBodyOracle>(body);auto* reference_body=oracle.get();ui::DialogModel::Builder builder;builder.SetTitle(delegate->GetWindowTitle()).AddCustomField(std::make_unique<views::BubbleDialogModelHost::CustomView>(std::move(oracle),control?views::BubbleDialogModelHost::FieldType::kControl:views::BubbleDialogModelHost::FieldType::kText)).AddOkButton(base::DoNothing(),ui::DialogModel::Button::Params().SetLabel(delegate->GetDialogButtonLabel(ui::mojom::DialogButton::kOk))).AddCancelButton(base::DoNothing(),ui::DialogModel::Button::Params().SetLabel(delegate->GetDialogButtonLabel(ui::mojom::DialogButton::kCancel))).OverrideDefaultButton(delegate->GetDefaultDialogButton()==static_cast<int>(ui::mojom::DialogButton::kOk)?ui::mojom::DialogButton::kOk:ui::mojom::DialogButton::kCancel);
      auto reference=views::BubbleDialogModelHost::CreateModal(builder.Build(),ui::mojom::ModalType::kWindow);reference->SetOwnershipOfNewWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);auto* reference_delegate=reference.get();std::unique_ptr<views::Widget> reference_widget(views::DialogDelegate::CreateDialogWidget(std::move(reference),window_->widget()->GetNativeWindow(),window_->widget()->GetNativeView()));reference_widget->SetColorModeOverride(widget->color_mode_override());widget->LayoutRootViewIfNecessary();reference_widget->LayoutRootViewIfNecessary();
      EXPECT_EQ(reference_delegate->margins(),delegate->margins());EXPECT_EQ(reference_delegate->fixed_width(),delegate->fixed_width());EXPECT_EQ(reference_widget->client_view()->GetPreferredSize(),widget->client_view()->GetPreferredSize());EXPECT_EQ(reference_widget->client_view()->GetMinimumSize(),widget->client_view()->GetMinimumSize());
      EXPECT_EQ(views::View::ConvertRectToTarget(reference_body,reference_widget->client_view(),reference_body->GetLocalBounds()),views::View::ConvertRectToTarget(body,widget->client_view(),body->GetLocalBounds()));
    }
  };
  auto ready=[&](views::Widget* widget,views::View* focus){ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(widget->GetNativeWindow()));if(focus)focus->RequestFocus();base::test::TestFuture<void> presented;auto* compositor=widget->GetCompositor();ASSERT_TRUE(compositor);widget->GetRootView()->SchedulePaint();compositor->RequestSuccessfulPresentationTimeForNextFrame(base::BindOnce([](base::OnceClosure done,const viz::FrameTimingDetails&){std::move(done).Run();},presented.GetCallback()));compositor->ScheduleFullRedraw();ASSERT_TRUE(presented.Wait());widget->widget_delegate()->AsDialogDelegate()->ResetViewShownTimeStampForTesting();};
  shell->RequestNewFolder("work");ASSERT_TRUE(base::test::RunUntil([&]{return shell->folder_dialog()!=nullptr;}));inspect(shell->folder_dialog(),shell->folder_form_for_testing(),true);auto* widget=shell->folder_dialog();ready(widget,DialogActionButton(widget,false));ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(widget->GetNativeWindow(),ui::VKEY_SPACE,false,false,false,false));ASSERT_TRUE(base::test::RunUntil([&]{return !shell->folder_dialog();}));EXPECT_TRUE(window_->pinned_items("work").empty());
  shell->RequestNewFolder("work");ASSERT_TRUE(base::test::RunUntil([&]{return shell->folder_dialog()!=nullptr;}));ASSERT_TRUE(ReplaceFolderName(shell,u"Scene folder 🦀"));widget=shell->folder_dialog();ready(widget,nullptr);ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(widget->GetNativeWindow(),ui::VKEY_RETURN,false,false,false,false));ASSERT_TRUE(base::test::RunUntil([&]{return !shell->folder_dialog()&&!window_->profiles_busy();}));ASSERT_EQ(1u,window_->pinned_items("work").size());const auto folder=window_->pinned_items("work")[0].id;
  shell->RequestDeleteFolder("work",folder);ASSERT_TRUE(base::test::RunUntil([&]{return shell->folder_dialog()!=nullptr;}));inspect(shell->folder_dialog(),shell->folder_notice_for_testing(),false);widget=shell->folder_dialog();ready(widget,DialogActionButton(widget,false));ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(widget->GetNativeWindow(),ui::VKEY_RETURN,false,false,false,false));if(!views::PlatformStyle::kDialogDefaultButtonCanBeCancel){EXPECT_TRUE(shell->folder_dialog());ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(widget->GetNativeWindow(),ui::VKEY_ESCAPE,false,false,false,false));}ASSERT_TRUE(base::test::RunUntil([&]{return !shell->folder_dialog();}));EXPECT_EQ(1u,window_->pinned_items("work").size());
  shell->BuildWorkspaceMenu("personal");shell->workspace_menu()->ActivatedAt(5,0);ASSERT_TRUE(base::test::RunUntil([&]{return shell->remove_workspace_dialog()!=nullptr;}));inspect(shell->remove_workspace_dialog(),shell->workspace_notice_for_testing(),false);widget=shell->remove_workspace_dialog();ready(widget,DialogActionButton(widget,false));ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(widget->GetNativeWindow(),ui::VKEY_ESCAPE,false,false,false,false));ASSERT_TRUE(base::test::RunUntil([&]{return !shell->remove_workspace_dialog();}));EXPECT_EQ(2u,window_->workspaces().size());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, WorkspaceSwitchCostWithImportedSidebar) {
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_saved(); }));
  window_.reset();
  const auto file = g_browser_process->profile_manager()->user_data_dir().AppendASCII("ZitterWorkspaces.json");
  {
    base::ScopedAllowBlockingForTesting allow;
    std::string json;
    ASSERT_TRUE(base::ReadFileToString(file, &json));
    auto data = base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
    ASSERT_TRUE(data);
    base::ListValue pins;
    for (int i = 0; i < 300; ++i) {
      base::DictValue pin;
      pin.Set("id", "cost-pin-" + std::to_string(i)); pin.Set("parent_id", "");
      pin.Set("title", "Imported item " + std::to_string(i));
      pin.Set("url", "https://example.test/" + std::to_string(i)); pin.Set("folder", false);
      pins.Append(std::move(pin));
    }
    data->FindList("workspaces")->back().GetDict().Set("pins", std::move(pins));
    ASSERT_TRUE(base::JSONWriter::Write(*data, &json));
    ASSERT_TRUE(base::WriteFile(file, json));
  }
  window_ = std::make_unique<WorkspaceWindow>(work_, personal_);
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_ready() && !window_->profiles_busy(); }));
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->shell()->appearance_ready() && window_->shell()->tools_ready(); }));
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->shell()->sidebars_warmed(); }));
  EXPECT_FALSE(window_->contents("personal"));  // Sidebar preparation does not create pages.
  base::ElapsedTimer cold;
  ASSERT_TRUE(window_->Select("personal"));
  const auto cold_time = cold.Elapsed();
  ASSERT_EQ(300u, window_->pinned_items("personal").size());
  const auto builds = window_->shell()->pinned_list_build_count();
  // Warm switches must not revisit the native pin trees, even for menus or
  // visibility bookkeeping. This also covers metadata-only shell updates.
  const auto context_visits = window_->shell()->sidebar_context_visit_count();
  const auto visibility_updates = window_->shell()->pin_visibility_update_count();
  base::ElapsedTimer hot;
  for (int i = 0; i < 10; ++i) {
    ASSERT_TRUE(window_->Select("work"));
    ASSERT_TRUE(window_->Select("personal"));
  }
  LOG(INFO) << "ZITTER_SWITCH_COST cold_ms=" << cold_time.InMillisecondsF()
            << " warm_mean_ms=" << hot.Elapsed().InMillisecondsF() / 20.0;
  EXPECT_EQ(builds, window_->shell()->pinned_list_build_count());
  EXPECT_EQ(context_visits, window_->shell()->sidebar_context_visit_count());
  EXPECT_EQ(visibility_updates, window_->shell()->pin_visibility_update_count());
  EXPECT_EQ("personal", window_->active_id());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, WorkspaceSwitchCostWithTwoLargeSidebars) {
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_saved(); }));
  window_.reset();
  const auto file = g_browser_process->profile_manager()->user_data_dir().AppendASCII("ZitterWorkspaces.json");
  {
    base::ScopedAllowBlockingForTesting allow;
    std::string json;
    ASSERT_TRUE(base::ReadFileToString(file, &json));
    auto data = base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
    ASSERT_TRUE(data);
    base::ListValue pins;
    for (int i = 0; i < 300; ++i) {
      base::DictValue pin;
      pin.Set("id", "cost-pin-" + std::to_string(i)); pin.Set("parent_id", "");
      pin.Set("title", "Imported item " + std::to_string(i));
      pin.Set("url", "https://example.test/" + std::to_string(i)); pin.Set("folder", false);
      pins.Append(std::move(pin));
    }
    for (auto& workspace : *data->FindList("workspaces"))
      workspace.GetDict().Set("pins", pins.Clone());
    ASSERT_TRUE(base::JSONWriter::Write(*data, &json));
    ASSERT_TRUE(base::WriteFile(file, json));
  }
  window_ = std::make_unique<WorkspaceWindow>(work_, personal_);
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_ready() && !window_->profiles_busy(); }));
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->shell()->appearance_ready() && window_->shell()->tools_ready(); }));
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->shell()->sidebars_warmed(); }));
  EXPECT_FALSE(window_->contents("personal"));  // Sidebar preparation does not create pages.
  base::ElapsedTimer cold;
  ASSERT_TRUE(window_->Select("personal"));
  const auto cold_time = cold.Elapsed();
  ASSERT_EQ(300u, window_->pinned_items("personal").size());
  const auto builds = window_->shell()->pinned_list_build_count();
  // Warm switches must not revisit the native pin trees, even for menus or
  // visibility bookkeeping. This also covers metadata-only shell updates.
  const auto context_visits = window_->shell()->sidebar_context_visit_count();
  const auto visibility_updates = window_->shell()->pin_visibility_update_count();
  base::ElapsedTimer hot;
  for (int i = 0; i < 10; ++i) {
    ASSERT_TRUE(window_->Select("work"));
    ASSERT_TRUE(window_->Select("personal"));
  }
  LOG(INFO) << "ZITTER_SWITCH_TWO_LARGE cold_ms=" << cold_time.InMillisecondsF()
            << " warm_mean_ms=" << hot.Elapsed().InMillisecondsF() / 20.0;
  EXPECT_EQ(builds, window_->shell()->pinned_list_build_count());
  EXPECT_EQ(context_visits, window_->shell()->sidebar_context_visit_count());
  EXPECT_EQ(visibility_updates, window_->shell()->pin_visibility_update_count());
  EXPECT_EQ("personal", window_->active_id());
  auto* shell = window_->shell(); shell->ShowSidebar();
  const auto point = shell->tab_scroll_view()->GetBoundsInScreen().CenterPoint() -
      shell->sidebar_widget()->GetWindowBoundsInScreen().OffsetFromOrigin();
  auto stamp = base::TimeTicks::Now();
  auto scroll = [&](float x, ui::ScrollEventPhase phase) {
    stamp += base::Milliseconds(10);
    ui::ScrollEvent event(ui::EventType::kScroll, point, stamp, 0, x, 0, x, 0, 2,
        ui::EventMomentumPhase::NONE, phase);
    shell->sidebar_widget()->OnScrollEvent(&event);
  };
  scroll(0, ui::ScrollEventPhase::kBegan);
  base::ElapsedTimer preview;
  scroll(shell->tab_scroll_view()->width() * 0.3f, ui::ScrollEventPhase::kUpdate);
  const auto preparation = preview.Elapsed();
  ASSERT_TRUE(shell->workspace_swipe_visible());
  base::ElapsedTimer tracking;
  for (int i = 0; i < 20; ++i) {
    stamp += base::Milliseconds(10);
    ui::ScrollEvent event(ui::EventType::kScroll, point, stamp, 0, i % 2 ? 1 : -1, 0, 0, 0, 2,
        ui::EventMomentumPhase::NONE, ui::ScrollEventPhase::kUpdate);
    shell->OnScrollEvent(&event);
  }
  LOG(INFO) << "ZITTER_SWIPE_COST prepare_native_ms=" << preparation.InMillisecondsF()
            << " tracking_handler_mean_ms=" << tracking.Elapsed().InMillisecondsF() / 20.0;
  EXPECT_LE(shell->workspace_swipe_preview_count(), 3u);
  for (const auto& list : shell->tab_list_view()->children()) EXPECT_FALSE(list->layer());
  EXPECT_EQ(builds, shell->pinned_list_build_count());
  scroll(0, ui::ScrollEventPhase::kEnd);
  ASSERT_TRUE(base::test::RunUntil([&] { return !shell->workspace_swipe_visible(); }));
  EXPECT_EQ("personal", window_->active_id());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, SidebarFoldersCreateNestAndPersist) {
  ASSERT_TRUE(window_->Select("work"));
  auto* shell = window_->shell();
  EXPECT_TRUE(SidebarNamedButton(shell->sidebar_view(), u"Nowa karta"));
  for (auto name : {u"Time Machine", u"Historia", u"Pobrania", u"Ustawienia"})
    EXPECT_FALSE(SidebarNamedButton(shell->sidebar_view(), name));
  EXPECT_EQ(shell, shell->sidebar_view()->context_menu_controller());
  EXPECT_EQ(shell, shell->tab_list_view()->context_menu_controller());
  // Blank layout/scroll containers must own a context menu independently of
  // their parent; button children retain their usual non-interactive labels.
  auto check_backgrounds = [&](auto&& self, views::View* view) -> void {
    EXPECT_TRUE(view->context_menu_controller());
    if (views::AsViewClass<views::Button>(view)) return;
    for (const auto& child : view->children()) self(self, child.get());
  };
  check_backgrounds(check_backgrounds, shell->sidebar_view());
  shell->BuildFolderMenu();
  EXPECT_EQ(u"Nowy folder…", shell->workspace_menu()->GetLabelAt(0));
  shell->workspace_menu()->ActivatedAt(0, 0);
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->folder_dialog() != nullptr; }));
  shell->folder_dialog()->widget_delegate()->AsDialogDelegate()->CancelDialog();
  ASSERT_TRUE(base::test::RunUntil([&] { return !shell->folder_dialog(); }));
  EXPECT_TRUE(window_->pinned_items("work").empty());
  auto add = [&](std::string parent, std::u16string name) {
    shell->BuildFolderMenu(parent);
    EXPECT_EQ(parent.empty() ? u"Nowy folder…" : u"Nowy podfolder…", shell->workspace_menu()->GetLabelAt(0));
    shell->workspace_menu()->ActivatedAt(0, 0);
    ASSERT_TRUE(base::test::RunUntil([&] { return shell->folder_dialog() != nullptr; }));
    ASSERT_TRUE(ReplaceFolderName(shell,name));
    shell->folder_dialog()->widget_delegate()->AsDialogDelegate()->AcceptDialog();
    ASSERT_TRUE(base::test::RunUntil([&] { return !shell->folder_dialog() && !window_->profiles_busy(); }));
  };
  add({}, u"  Projekty 📁  ");
  ASSERT_EQ(1u, window_->pinned_items("work").size());
  const auto parent = window_->pinned_items("work")[0].id;
  EXPECT_EQ("Projekty 📁", window_->pinned_items("work")[0].title);
  EXPECT_TRUE(window_->pinned_items("work")[0].folder);
  auto* folder = SidebarNamedButton(shell->sidebar_view(), u"▾ Projekty 📁");
  ASSERT_TRUE(folder);
  EXPECT_EQ(shell, folder->context_menu_controller());
  add(parent, u"Research");
  ASSERT_EQ(2u, window_->pinned_items("work").size());
  EXPECT_EQ(parent, window_->pinned_items("work")[1].parent_id);
  EXPECT_EQ("Research", window_->pinned_items("work")[1].title);
  EXPECT_TRUE(window_->pinned_items("personal").empty());
  EXPECT_FALSE(window_->CreatePinnedFolder("work", "missing-parent", "Bad"));
  EXPECT_FALSE(window_->CreatePinnedFolder("missing-workspace", {}, "Bad"));
  EXPECT_FALSE(window_->CreatePinnedFolder("work", {}, "   "));
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_saved(); }));
  window_.reset();
  window_ = std::make_unique<WorkspaceWindow>(work_, personal_);
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_ready(); }));
  ASSERT_EQ(2u, window_->pinned_items("work").size());
  EXPECT_EQ(parent, window_->pinned_items("work")[1].parent_id);
  EXPECT_EQ("Projekty 📁", window_->pinned_items("work")[0].title);
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, SidebarFolderRenameAndDeleteContents) {
  ASSERT_TRUE(window_->Select("work"));
  ASSERT_TRUE(window_->CreatePinnedFolder("work", {}, "Parent"));
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->profiles_busy(); }));
  const auto id = window_->pinned_items("work")[0].id;
  ASSERT_TRUE(window_->CreatePinnedFolder("work", id, "Child"));
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->profiles_busy(); }));
  auto* shell = window_->shell();
  shell->BuildFolderMenu(id);
  EXPECT_EQ(u"Zmień nazwę…", shell->workspace_menu()->GetLabelAt(2));
  EXPECT_EQ(u"Usuń folder…", shell->workspace_menu()->GetLabelAt(3));
  shell->workspace_menu()->ActivatedAt(2, 0);
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->folder_dialog() != nullptr; }));
  auto* editor=shell->folder_editor_for_testing();auto* native=NativeFolderNameInput(shell->folder_dialog()->GetContentsView());ASSERT_TRUE(editor||native);
  EXPECT_EQ(u"Parent",editor?editor->GetText():native->GetText());
  ASSERT_TRUE(ReplaceFolderName(shell,u"  Renamed 🦊  "));
  shell->folder_dialog()->widget_delegate()->AsDialogDelegate()->AcceptDialog();
  ASSERT_TRUE(base::test::RunUntil([&] { return !shell->folder_dialog() && !window_->profiles_busy(); }));
  EXPECT_EQ(id, window_->pinned_items("work")[0].id);
  EXPECT_EQ("Renamed 🦊", window_->pinned_items("work")[0].title);
  EXPECT_EQ(id, window_->pinned_items("work")[1].parent_id);
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_saved(); }));
  window_.reset(); window_ = std::make_unique<WorkspaceWindow>(work_, personal_);
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_ready() && !window_->profiles_busy(); }));
  EXPECT_EQ("Renamed 🦊", window_->pinned_items("work")[0].title);
  shell = window_->shell();
  shell->BuildFolderMenu(id); shell->workspace_menu()->ActivatedAt(3, 0);
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->folder_dialog() != nullptr; }));
  EXPECT_EQ(static_cast<int>(ui::mojom::DialogButton::kCancel), shell->folder_dialog()->widget_delegate()->AsDialogDelegate()->GetDefaultDialogButton());
  shell->folder_dialog()->widget_delegate()->AsDialogDelegate()->CancelDialog();
  ASSERT_TRUE(base::test::RunUntil([&] { return !shell->folder_dialog(); }));
  EXPECT_EQ(2u, window_->pinned_items("work").size());
  shell->BuildFolderMenu(id); shell->workspace_menu()->ActivatedAt(3, 0);
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->folder_dialog() != nullptr; }));
  auto* page = window_->contents("work");
  shell->folder_dialog()->widget_delegate()->AsDialogDelegate()->AcceptDialog();
  ASSERT_TRUE(base::test::RunUntil([&] { return !shell->folder_dialog() && !window_->profiles_busy(); }));
  EXPECT_TRUE(window_->pinned_items("work").empty());
  EXPECT_EQ(page, window_->contents("work"));
  EXPECT_FALSE(window_->RenamePinnedFolder("work", id, "Gone"));
  EXPECT_FALSE(window_->RemovePinnedFolder("work", id, true));
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_saved(); }));
  window_.reset(); window_ = std::make_unique<WorkspaceWindow>(work_, personal_);
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_ready(); }));
  EXPECT_TRUE(window_->pinned_items("work").empty());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, SidebarSwipeTracksCancelsAndSkipsSpaces) {
  auto* shell = window_->shell();
  for (const auto* name : {"Third", "Fourth"}) {
    ASSERT_TRUE(window_->AddWorkspace(name, "work-profile"));
    ASSERT_TRUE(base::test::RunUntil([&] { return !window_->profiles_busy(); }));
  }
  const auto fourth = window_->workspaces().back().id;
  ASSERT_TRUE(window_->Select("work"));
  shell->SetPinned(false); shell->ShowSidebar();
  ASSERT_TRUE(base::test::RunUntil([&] { return !shell->workspace_animation_running(); }));
  const auto width = shell->tab_scroll_view()->width();
  ASSERT_GT(width, 100);
  auto stamp = base::TimeTicks::Now();
  const auto point = shell->tab_scroll_view()->GetBoundsInScreen().CenterPoint() -
      shell->sidebar_widget()->GetWindowBoundsInScreen().OffsetFromOrigin();
  auto scroll = [&](float x, float y, ui::ScrollEventPhase phase,
                    ui::EventMomentumPhase momentum = ui::EventMomentumPhase::NONE) {
    stamp += base::Milliseconds(10);
    ui::ScrollEvent event(ui::EventType::kScroll, point, stamp, 0, x, y, x, y, 2, momentum, phase);
    shell->sidebar_widget()->OnScrollEvent(&event);
  };
  auto begin = [&] { scroll(0, 0, ui::ScrollEventPhase::kBegan); };
  auto move = [&](float x) { scroll(x, 0, ui::ScrollEventPhase::kUpdate); };
  auto end = [&] { scroll(0, 0, ui::ScrollEventPhase::kEnd); };
  begin(); move(-width * 0.45f);
  EXPECT_TRUE(shell->workspace_swipe_visible());
  EXPECT_NEAR(0.45, shell->workspace_swipe_position(), 0.01);
  EXPECT_EQ("work", window_->active_id());
  EXPECT_FALSE(window_->contents("personal"));  // Preview never creates a page.
  move(width * 0.4f);
  EXPECT_NEAR(0.05, shell->workspace_swipe_position(), 0.01);
  end();
  ASSERT_TRUE(base::test::RunUntil([&] { return !shell->workspace_swipe_visible(); }));
  EXPECT_EQ("work", window_->active_id());
  // One uninterrupted drag crosses three spaces; no intermediate selection.
  begin(); move(-width * 1.2f); move(-width * 1.6f);
  EXPECT_NEAR(2.8, shell->workspace_swipe_position(), 0.01);
  EXPECT_LE(shell->workspace_swipe_preview_count(), 3u);
  EXPECT_EQ("work", window_->active_id());
  end();
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->active_id() == fourth; }));
  EXPECT_FALSE(shell->workspace_animation_running());  // No second switch animation.
  ASSERT_TRUE(base::test::RunUntil([&] { return !shell->workspace_swipe_visible(); }));
  // Repeated gesture at exactly the same cursor point.
  begin(); move(width * 3.1f); end();
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->active_id() == "work"; }));
  ASSERT_TRUE(base::test::RunUntil([&] { return !shell->workspace_swipe_visible(); }));
  begin(); move(width * 2);
  EXPECT_GT(shell->workspace_swipe_position(), -0.23);  // Rubber band, no wrap.
  end();
  ASSERT_TRUE(base::test::RunUntil([&] { return !shell->workspace_swipe_visible(); }));
  EXPECT_EQ("work", window_->active_id());
  begin(); scroll(0, -30, ui::ScrollEventPhase::kUpdate); move(-width * 3);
  EXPECT_FALSE(shell->workspace_swipe_visible()); end();
  EXPECT_EQ("work", window_->active_id());
  // First inertia event ends a gesture even if finger-up was omitted.
  begin(); move(-width * 0.7f);
  scroll(-999, 0, ui::ScrollEventPhase::kNone, ui::EventMomentumPhase::INERTIAL_UPDATE);
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->active_id() == "personal"; }));
  scroll(999, 0, ui::ScrollEventPhase::kNone, ui::EventMomentumPhase::INERTIAL_UPDATE);
  ASSERT_TRUE(base::test::RunUntil([&] { return !shell->workspace_swipe_visible(); }));
  EXPECT_EQ("personal", window_->active_id());
  // Hiding mid-drag cancels without navigating.
  begin(); move(-width * 0.8f); shell->CloseOverlays();
  EXPECT_FALSE(shell->workspace_swipe_visible());
  EXPECT_EQ("personal", window_->active_id());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, CocoaSwipesRepeatWithoutMouseMovement) {
  auto* shell = window_->shell();
  ASSERT_TRUE(window_->Select("work"));
  shell->ShowSidebar();
  const auto width = shell->tab_scroll_view()->width();
  auto stamp = base::TimeTicks::Now();
  const auto point = shell->tab_scroll_view()->GetBoundsInScreen().CenterPoint() -
      shell->sidebar_widget()->GetWindowBoundsInScreen().OffsetFromOrigin();
  auto scroll = [&](float x, ui::EventMomentumPhase phase) {
    stamp += base::Milliseconds(10);  // No idle timeout and no mouse events.
    ui::ScrollEvent event(ui::EventType::kScroll, point, stamp, 0, x, 0, x, 0, 2,
        phase, ui::ScrollEventPhase::kNone);
    shell->sidebar_widget()->OnScrollEvent(&event);
  };
  for (int i = 0; i < 3; ++i) {
    scroll(0, ui::EventMomentumPhase::MAY_BEGIN);
    scroll(-width * 0.7f, ui::EventMomentumPhase::NONE);
    EXPECT_EQ("work", window_->active_id());
    scroll(0, ui::EventMomentumPhase::END);
    ASSERT_TRUE(base::test::RunUntil([&] { return window_->active_id() == "personal"; }));
    scroll(500, ui::EventMomentumPhase::INERTIAL_UPDATE);
    EXPECT_EQ("personal", window_->active_id());
    // Interrupt settling/inertia with a new touch containing an initial delta.
    scroll(width * 0.7f, ui::EventMomentumPhase::MAY_BEGIN);
    EXPECT_EQ("personal", window_->active_id());
    scroll(0, ui::EventMomentumPhase::END);
    ASSERT_TRUE(base::test::RunUntil([&] { return window_->active_id() == "work"; }));
  }
  ASSERT_TRUE(base::test::RunUntil([&] { return !shell->workspace_swipe_visible(); }));
  // Phase-less wheel has no release event; consecutive deltas settle on idle.
  scroll(-width * 0.3f, ui::EventMomentumPhase::END);
  scroll(-width * 0.4f, ui::EventMomentumPhase::END);
  EXPECT_EQ("work", window_->active_id());
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->active_id() == "personal"; }));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, SidebarFolderFailedWriteRollsBack) {
  base::ScopedAllowBlockingForTesting files;
  ASSERT_TRUE(window_->CreatePinnedFolder("work", {}, "Keep"));
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->profiles_busy() && window_->configuration_saved(); }));
  const auto file = g_browser_process->profile_manager()->user_data_dir().AppendASCII("ZitterWorkspaces.json");
  const auto backup = file.AddExtensionASCII("folder-backup");
  ASSERT_TRUE(base::Move(file, backup));
  ASSERT_TRUE(base::CreateDirectory(file));
  ASSERT_TRUE(window_->CreatePinnedFolder("work", {}, "Cannot save"));
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->profiles_busy(); }));
  ASSERT_EQ(1u, window_->pinned_items("work").size());
  EXPECT_EQ("Keep", window_->pinned_items("work")[0].title);
  ASSERT_TRUE(window_->shell()->folder_dialog());
  window_->shell()->folder_dialog()->widget_delegate()->AsDialogDelegate()->AcceptDialog();
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->shell()->folder_dialog(); }));
  const auto id = window_->pinned_items("work")[0].id;
  ASSERT_TRUE(window_->RenamePinnedFolder("work", id, "Cannot rename"));
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->profiles_busy(); }));
  EXPECT_EQ("Keep", window_->pinned_items("work")[0].title);
  ASSERT_TRUE(window_->shell()->folder_dialog());
  window_->shell()->folder_dialog()->widget_delegate()->AsDialogDelegate()->AcceptDialog();
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->shell()->folder_dialog(); }));
  ASSERT_TRUE(window_->RemovePinnedFolder("work", id, true));
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->profiles_busy(); }));
  ASSERT_EQ(1u, window_->pinned_items("work").size());
  EXPECT_EQ(id, window_->pinned_items("work")[0].id);
  ASSERT_TRUE(window_->shell()->folder_dialog());
  window_->shell()->folder_dialog()->widget_delegate()->AsDialogDelegate()->AcceptDialog();
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->shell()->folder_dialog(); }));
  ASSERT_TRUE(base::DeletePathRecursively(file));
  ASSERT_TRUE(base::Move(backup, file));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, FolderRemovalKeepsOpenedPinsAsTabs) {
  base::FilePath root;
  ASSERT_TRUE(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root));
  window_->PreviewArcImport(root.AppendASCII("zitter/testdata/arc"));
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->import_ready(); }));
  ASSERT_TRUE(window_->ApplyArcImport());
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->profiles_busy(); }));
  const std::string space = "arc-space-space-a";
  ASSERT_TRUE(window_->Select(space));
  window_->OpenPinnedItem("tab-a");
  auto* page = window_->contents(space);
  ASSERT_EQ(2u, window_->tab_count(space));
  EXPECT_FALSE(window_->shell()->tab_button(1));
  ASSERT_TRUE(window_->RemovePinnedFolder(space, "docs", true));
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->profiles_busy(); }));
  const auto& pins = window_->pinned_items(space);
  for (const auto& pin : pins) {
    EXPECT_NE("docs", pin.id); EXPECT_NE("nested", pin.id);
    EXPECT_NE("tab-a", pin.id); EXPECT_NE("tab-b", pin.id);
  }
  EXPECT_TRUE(std::ranges::any_of(pins, [](const auto& pin) { return pin.id == "direct"; }));
  EXPECT_EQ(page, window_->contents(space));
  EXPECT_EQ(2u, window_->tab_count(space));
  EXPECT_TRUE(window_->shell()->tab_button(1));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, WorkspaceDotProfileMenu) {
  auto* shell = window_->shell();
  ASSERT_TRUE(shell->workspace_button("work"));
  ASSERT_TRUE(shell->workspace_button("personal"));
  shell->BuildWorkspaceMenu("personal");
  ASSERT_EQ(u"Profil przeglądania", shell->workspace_menu()->GetLabelAt(0));
  auto* profiles = shell->workspace_menu()->GetSubmenuModelAt(0);
  ASSERT_TRUE(profiles);
  EXPECT_FALSE(profiles->IsItemCheckedAt(0));
  EXPECT_TRUE(profiles->IsItemCheckedAt(1));
  profiles->ActivatedAt(0, 0);
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->workspaces()[1].profile_id == "work-profile"; }));
  EXPECT_EQ("work", window_->active_id());
  EXPECT_EQ(work_.get(), window_->contents("personal")->GetBrowserContext());
  // Editing the profile from an inactive dot retains the target's identity.
  shell->BuildWorkspaceMenu("personal");
  profiles = shell->workspace_menu()->GetSubmenuModelAt(0);
  profiles->ActivatedAt(3, 0);  // Two profiles, separator, Edit.
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->profiles_widget()->IsVisible(); }));
  EXPECT_EQ(u"Profil firmowy", shell->profile_name_input()->GetText());
  shell->profile_name_input()->SetText(u"Profil zespołu");
  auto* rename = FindButton(shell->profiles_widget()->GetContentsView(), u"Zapisz nazwę profilu");
  ASSERT_TRUE(rename);
  ClickControl(rename);
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->profiles_busy(); }));
  EXPECT_EQ("Profil zespołu", window_->profiles()[0].name);
  EXPECT_EQ(0u, shell->profile_picker_selection(true));
  EXPECT_EQ(work_.get(), window_->contents("work")->GetBrowserContext());
  shell->BuildWorkspaceMenu("personal");
  profiles = shell->workspace_menu()->GetSubmenuModelAt(0);
  profiles->ActivatedAt(4, 0);  // New profile, assigned to this dot.
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->profile_name_input()->GetText().empty(); }));
  shell->profile_name_input()->SetText(u"Klient nowy");
  auto* create = FindButton(shell->profiles_widget()->GetContentsView(), u"Utwórz profil");
  ASSERT_TRUE(create);
  ClickControl(create);
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->profiles_busy(); }));
  ASSERT_EQ(3u, window_->profiles().size());
  const auto created = window_->profiles().back();
  EXPECT_EQ(created.id, window_->workspaces()[1].profile_id);
  EXPECT_EQ("work", window_->active_id());
  EXPECT_NE(work_.get(), window_->contents("personal")->GetBrowserContext());
  {ui::AXActionData activate;activate.action=ax::mojom::Action::kDoDefault;EXPECT_TRUE(shell->workspace_button("personal")->HandleAccessibleAction(activate));}
  EXPECT_EQ("personal", window_->active_id());
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_saved(); }));
  window_.reset();
  window_ = std::make_unique<WorkspaceWindow>(work_, personal_);
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_ready(); }));
  EXPECT_EQ("Profil zespołu", window_->profiles()[0].name);
  EXPECT_EQ(created.id, window_->workspaces()[1].profile_id);
  EXPECT_EQ("personal", window_->active_id());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, DynamicProfiles) {
  auto* shell = window_->shell();
  shell->OpenProfiles();
  auto* create = FindButton(shell->profiles_widget()->GetContentsView(), u"Utwórz profil");
  ASSERT_TRUE(create);
  shell->profile_name_input()->SetText(u"Klient Łódź");
  ClickControl(create);
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->profiles_busy(); }));
  ASSERT_EQ(3u, window_->profiles().size());
  const auto added = window_->profiles().back();
  EXPECT_EQ("Klient Łódź", added.name);
  EXPECT_FALSE(window_->CreateProfile("  "));
  EXPECT_FALSE(window_->CreateProfile("Klient Łódź"));
  shell->OpenWorkspaceSettings();
  ASSERT_EQ(3u, shell->profile_picker_count());
  ASSERT_TRUE(window_->EditWorkspace("Praca", added.id));
  EXPECT_FALSE(window_->RemoveProfile(added.id));
  const auto url = embedded_test_server()->GetURL("/storage_probe.html");
  ASSERT_TRUE(content::NavigateToURL(window_->contents("work"), url));
  EXPECT_EQ("client", content::EvalJs(window_->contents("work"), "writeMarker('client')"));
  EXPECT_NE(work_.get(), window_->contents("work")->GetBrowserContext());
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_saved(); }));
  window_.reset();
  const auto directory = g_browser_process->profile_manager()->user_data_dir();
  window_ = std::make_unique<WorkspaceWindow>(directory, false);
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_ready(); }));
  ASSERT_EQ(3u, window_->profiles().size());
  EXPECT_EQ(added.id, window_->workspaces()[0].profile_id);
  ASSERT_TRUE(content::NavigateToURL(window_->contents("work"), url));
  EXPECT_EQ("client|client|client|", content::EvalJs(window_->contents("work"), "readMarker()"));
  ASSERT_TRUE(window_->EditWorkspace("Praca", "work-profile"));
  shell = window_->shell();
  shell->OpenProfiles();
  shell->SetProfilePickerSelectionForTesting(true,2);
  auto* remove = FindButton(shell->profiles_widget()->GetContentsView(), u"Usuń profil i jego dane");
  ASSERT_TRUE(remove);
  ClickControl(remove);
  EXPECT_EQ(3u, window_->profiles().size());
  auto* confirm = FindButton(shell->profiles_widget()->GetContentsView(), u"Potwierdź usunięcie profilu i danych");
  ASSERT_TRUE(confirm);
  ClickControl(confirm);
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->profiles_busy(); }));
  ASSERT_EQ(2u, window_->profiles().size());
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !g_browser_process->profile_manager()->GetProfileAttributesStorage().GetProfileAttributesWithPath(directory.AppendASCII(added.directory));
  }));
  // The original defaults are ordinary deletable profiles too.
  ASSERT_TRUE(window_->Select("personal"));
  ASSERT_TRUE(window_->EditWorkspace("Prywatne", "work-profile"));
  ASSERT_TRUE(window_->RemoveProfile("personal-profile"));
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->profiles_busy(); }));
  window_.reset();
  window_ = std::make_unique<WorkspaceWindow>(directory, false);
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_ready(); }));
  ASSERT_EQ(1u, window_->profiles().size());
  EXPECT_EQ("work-profile", window_->profiles()[0].id);
  EXPECT_FALSE(window_->RemoveProfile("work-profile"));
  for (const auto& workspace : window_->workspaces()) EXPECT_EQ("work-profile", workspace.profile_id);
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, ArcImportStructureAndPersistence) {
  base::FilePath root;
  ASSERT_TRUE(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root));
  const auto source = root.AppendASCII("zitter/testdata/arc");
  auto* shell = window_->shell();
  shell->OpenImport();
  window_->PreviewArcImport(source);
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->import_preview() != nullptr; }));
  const auto preview = *window_->import_preview();
  ASSERT_TRUE(preview.error.empty()) << preview.error;
  EXPECT_EQ(3u, preview.profiles.size());
  ASSERT_EQ(3u, preview.spaces.size());
  EXPECT_EQ(7u, preview.tab_count);
  EXPECT_EQ(5u, preview.folder_count);
  EXPECT_EQ(1u, preview.skipped_count);
  EXPECT_EQ("arc-space-space-b", preview.spaces[0].id);
  EXPECT_EQ(u"🚀", preview.spaces[0].emoji);
  EXPECT_EQ(u"👩🏽‍💻", preview.spaces[1].emoji);
  EXPECT_TRUE(preview.spaces[2].emoji.empty());
  EXPECT_EQ(preview.spaces[0].profile_id, preview.spaces[1].profile_id);
  EXPECT_NE(preview.spaces[0].profile_id, preview.spaces[2].profile_id);
  auto* import = FindButton(shell->import_widget()->GetContentsView(), u"Importuj");
  ASSERT_TRUE(import);
  ClickControl(import);
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->profiles_busy(); }));
  ASSERT_EQ(5u, window_->profiles().size());
  ASSERT_EQ(5u, window_->workspaces().size());
  EXPECT_EQ(u"👩🏽‍💻", shell->workspace_emoji("arc-space-space-a"));
  EXPECT_EQ(u"🚀", shell->workspace_emoji("arc-space-space-b"));
  const auto& pins = window_->pinned_items("arc-space-space-a");
  ASSERT_FALSE(pins.empty());
  bool nested = false;
  for (const auto& pin : pins) {
    if (pin.id == "tab-b") nested = pin.parent_id == "nested";
    EXPECT_NE("https://example.test/not-pinned", pin.url.spec());
    EXPECT_NE("javascript:alert(1)", pin.url.spec());
  }
  EXPECT_TRUE(nested);
  EXPECT_EQ(1u, window_->tab_count("arc-space-space-a"));
  // A long pinned list must leave the workspace picker inside the viewport.
  ASSERT_TRUE(window_->Select("arc-space-space-a"));
  shell->ShowSidebar();
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(window_->widget()->GetWindowBoundsInScreen().Contains(
      shell->workspace_button(window_->active_id())->GetBoundsInScreen()));
  // Opening a pin lazily uses its imported profile, and reuses the same tab.
  ASSERT_TRUE(window_->Select("arc-space-space-a"));
  window_->OpenPinnedItem("tab-a");
  EXPECT_EQ(2u, window_->tab_count("arc-space-space-a"));
  auto* page = window_->contents("arc-space-space-a");
  window_->OpenPinnedItem("tab-a");
  EXPECT_EQ(page, window_->contents("arc-space-space-a"));
  EXPECT_EQ(2u, window_->tab_count("arc-space-space-a"));
  // A pinned pane joins the pair, then returns to its original pin row.
  EXPECT_FALSE(shell->tab_button(1));
  ASSERT_TRUE(window_->SplitTab(0));
  ASSERT_TRUE(shell->tab_button(1));
  ASSERT_TRUE(shell->tab_button(0));
  EXPECT_EQ(shell->tab_button(1)->parent(), shell->tab_button(0)->parent());
  window_->FocusPane(false);
  EXPECT_EQ(page, window_->contents("arc-space-space-a"));
  const auto original_pins = window_->pinned_items("arc-space-space-a");
  ASSERT_TRUE(window_->SaveSnapshot("Imported folders and split"));
  const auto arc_snapshot = window_->TimeMachine().snapshots.front().id;
  window_->ToggleSplit();
  ASSERT_TRUE(window_->RestoreSnapshot(arc_snapshot));
  EXPECT_EQ(original_pins, window_->pinned_items("arc-space-space-a"));
  EXPECT_TRUE(window_->split());
  EXPECT_EQ(shell->tab_button(1)->parent(), shell->tab_button(0)->parent());
  page = window_->contents("arc-space-space-a");
  window_->ToggleSplit();
  EXPECT_FALSE(shell->tab_button(1));
  ASSERT_TRUE(window_->Select("arc-space-space-b"));
  EXPECT_EQ(page->GetBrowserContext(), window_->contents("arc-space-space-b")->GetBrowserContext());
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_saved() && shell->appearance_saved(); }));
  window_.reset();
  window_ = std::make_unique<WorkspaceWindow>(work_, personal_);
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_ready(); }));
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->shell()->appearance_ready(); }));
  EXPECT_EQ(u"👩🏽‍💻", window_->shell()->workspace_emoji("arc-space-space-a"));
  EXPECT_EQ(u"🚀", window_->shell()->workspace_emoji("arc-space-space-b"));
  window_->shell()->SetWorkspaceEmoji("arc-space-space-b", u"🎮");
  EXPECT_EQ(5u, window_->profiles().size());
  EXPECT_EQ(5u, window_->workspaces().size());
  EXPECT_EQ(preview.spaces[1].pins.size(), window_->pinned_items("arc-space-space-a").size());
  EXPECT_EQ(2u, window_->tab_count("arc-space-space-a"));
  window_->PreviewArcImport(source);
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->import_ready(); }));
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->profiles_busy(); }));
  ASSERT_TRUE(window_->ApplyArcImport());
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->profiles_busy(); }));
  EXPECT_EQ(5u, window_->profiles().size());
  EXPECT_EQ(5u, window_->workspaces().size());
  EXPECT_EQ(u"🎮", window_->shell()->workspace_emoji("arc-space-space-b"));
  // Malformed/missing source data never mutates the existing catalogue.
  window_->PreviewArcImport(root.AppendASCII("missing-arc-directory"));
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->import_preview() != nullptr; }));
  EXPECT_FALSE(window_->import_ready());
  EXPECT_FALSE(window_->ApplyArcImport());
  EXPECT_EQ(5u, window_->workspaces().size());
  EXPECT_FALSE(ParseArcImport("{}", "{}").error.empty());
  EXPECT_FALSE(ParseArcImport("not json", "{}").error.empty());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, FailedConfigurationWritePreservesProfiles) {
  ASSERT_TRUE(window_->CreateProfile("Do zachowania"));
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->profiles_busy(); }));
  ASSERT_EQ(3u, window_->profiles().size());
  const auto profile = window_->profiles().back();
  ASSERT_TRUE(window_->EditWorkspace("Praca", profile.id));
  const auto url = embedded_test_server()->GetURL("/storage_probe.html");
  ASSERT_TRUE(content::NavigateToURL(window_->contents("work"), url));
  ASSERT_EQ("keep", content::EvalJs(window_->contents("work"), "writeMarker('keep')"));
  ASSERT_TRUE(window_->EditWorkspace("Praca", "work-profile"));
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_saved(); }));
  auto* manager = g_browser_process->profile_manager();
  const auto file = manager->user_data_dir().AppendASCII("ZitterWorkspaces.json");
  const auto backup = file.AddExtensionASCII("backup");
  {
    base::ScopedAllowBlockingForTesting allow;
    ASSERT_TRUE(base::Move(file, backup));
    ASSERT_TRUE(base::CreateDirectory(file));  // Atomic replacement must fail.
  }
  ASSERT_TRUE(window_->RemoveProfile(profile.id));
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->profiles_busy(); }));
  EXPECT_EQ(3u, window_->profiles().size());
  EXPECT_TRUE(manager->GetProfileAttributesStorage().GetProfileAttributesWithPath(manager->user_data_dir().AppendASCII(profile.directory)));
  ASSERT_TRUE(window_->CreateProfile("Błąd zapisu"));
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->profiles_busy(); }));
  EXPECT_EQ(3u, window_->profiles().size());
  base::FilePath root;
  ASSERT_TRUE(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root));
  window_->PreviewArcImport(root.AppendASCII("zitter/testdata/arc"));
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->import_ready(); }));
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->profiles_busy(); }));
  ASSERT_TRUE(window_->ApplyArcImport());
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->profiles_busy(); }));
  EXPECT_EQ(3u, window_->profiles().size());
  EXPECT_EQ(2u, window_->workspaces().size());
  {
    base::ScopedAllowBlockingForTesting allow;
    ASSERT_TRUE(base::DeletePathRecursively(file));
    ASSERT_TRUE(base::Move(backup, file));
  }
  ASSERT_TRUE(window_->EditWorkspace("Praca", profile.id));
  ASSERT_TRUE(content::NavigateToURL(window_->contents("work"), url));
  EXPECT_EQ("keep|keep|keep|", content::EvalJs(window_->contents("work"), "readMarker()"));
}

namespace {
bool MakeHistorySource(const base::FilePath& directory) {
  sql::Database db("History");
  if (!db.Open(directory.AppendASCII("History")) || !db.Execute(
      "CREATE TABLE urls(url TEXT,title TEXT,visit_count INTEGER,typed_count INTEGER,last_visit_time INTEGER,hidden INTEGER)")) return false;
  sql::Statement insert(db.GetUniqueStatement("INSERT INTO urls VALUES(?,?,5,2,?,0)"));
  insert.BindString(0, "https://import.test/history");
  insert.BindString(1, "Imported history");
  insert.BindInt64(2, (base::Time::Now() - base::Days(2)).ToDeltaSinceWindowsEpoch().InMicroseconds());
  return insert.Run();
}
bool MakeCookieSource(const base::FilePath& directory, bool encrypted, bool bad_hash = false) {
  sql::Database db("Cookie");
  if (!db.Open(directory.AppendASCII("Cookies")) || !db.Execute(
      "CREATE TABLE meta(key TEXT PRIMARY KEY,value INTEGER); INSERT INTO meta VALUES('version',24);"
      "CREATE TABLE cookies(host_key TEXT,name TEXT,value TEXT,encrypted_value BLOB,path TEXT,creation_utc INTEGER,expires_utc INTEGER,"
      "last_access_utc INTEGER,is_secure INTEGER,is_httponly INTEGER,samesite INTEGER,priority INTEGER,top_frame_site_key TEXT,"
      "has_cross_site_ancestor INTEGER,source_scheme INTEGER,source_port INTEGER,last_update_utc INTEGER,is_persistent INTEGER)")) return false;
  sql::Statement insert(db.GetUniqueStatement("INSERT INTO cookies VALUES('import.test','session',?,?,'/',?,?,?,1,1,1,1,'',1,2,443,?,1)"));
  std::vector<uint8_t> bytes;
  if (encrypted) {
    const auto hash = crypto::hash::Sha256(bad_hash ? "wrong.test" : "import.test");
    std::vector<uint8_t> plaintext(hash.begin(), hash.end());
    const std::string value = "imported-session";
    plaintext.insert(plaintext.end(), value.begin(), value.end());
    const ImportedCookieKey key = {};
    constexpr std::array<uint8_t, 16> iv = {' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' '};
    bytes = crypto::aes_cbc::Encrypt(key, iv, plaintext);
    bytes.insert(bytes.begin(), {'v','1','0'});
  }
  insert.BindString(0, encrypted ? "" : "imported-session");
  insert.BindBlob(1, bytes);
  const auto now = base::Time::Now();
  insert.BindInt64(2, (now - base::Days(2)).ToDeltaSinceWindowsEpoch().InMicroseconds());
  insert.BindInt64(3, (now + base::Days(7)).ToDeltaSinceWindowsEpoch().InMicroseconds());
  insert.BindInt64(4, now.ToDeltaSinceWindowsEpoch().InMicroseconds());
  insert.BindInt64(5, now.ToDeltaSinceWindowsEpoch().InMicroseconds());
  return insert.Run();
}
}  // namespace

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, BrowsingImportEncryptedCookiesAndSelection) {
  base::ScopedAllowBlockingForTesting files;
  base::ScopedTempDir source;
  ASSERT_TRUE(source.CreateUniqueTempDir());
  const auto directory = source.GetPath().AppendASCII("Default");
  ASSERT_TRUE(base::CreateDirectory(directory));
  ASSERT_TRUE(MakeHistorySource(directory));
  ASSERT_TRUE(MakeCookieSource(directory, true));
  std::string original;
  ASSERT_TRUE(base::ReadFileToString(directory.AppendASCII("Cookies"), &original));
  BrowserImportOptions options;
  options.browser = ImportBrowser::kChrome;
  options.bookmarks = false;
  auto preview = ReadBrowserImport(options, source.GetPath());
  bool key_requested = false;
  auto reader = [&] {
    return base::BindOnce([](bool* called, ImportBrowser browser) -> std::optional<ImportedCookieKey> {
      *called = true;
      EXPECT_EQ(ImportBrowser::kChrome, browser);
      return ImportedCookieKey{};
    }, &key_requested);
  };
  auto ignored = ReadBrowsingImportData(preview, reader());
  EXPECT_FALSE(key_requested);
  ASSERT_TRUE(ignored->error.empty());
  EXPECT_TRUE(ignored->profiles[0].cookies.empty());
  EXPECT_TRUE(ignored->profiles[0].history.empty());
  options.history = options.cookies = true;
  preview = ReadBrowserImport(options, source.GetPath());
  EXPECT_FALSE(key_requested);  // Preview must never touch the keychain.
  auto data = ReadBrowsingImportData(preview, reader());
  ASSERT_TRUE(data->error.empty()) << data->error;
  EXPECT_TRUE(key_requested);
  ASSERT_EQ(1u, data->profiles.size());
  ASSERT_EQ(1u, data->profiles[0].cookies.size());
  ASSERT_EQ(1u, data->profiles[0].history.size());
  EXPECT_EQ("imported-session", data->profiles[0].cookies[0].Value());
  EXPECT_TRUE(data->profiles[0].cookies[0].IsHttpOnly());
  EXPECT_TRUE(data->profiles[0].cookies[0].SecureAttribute());
  auto denied = ReadBrowsingImportData(preview, base::BindOnce([](ImportBrowser) -> std::optional<ImportedCookieKey> { return std::nullopt; }));
  EXPECT_FALSE(denied->error.empty());
  EXPECT_TRUE(denied->profiles.empty());
  std::string unchanged;
  ASSERT_TRUE(base::ReadFileToString(directory.AppendASCII("Cookies"), &unchanged));
  EXPECT_EQ(original, unchanged);
  {
    // The latest partition key exists only in WAL while the source is open.
    sql::Database live(sql::DatabaseOptions().set_wal_mode(true).set_exclusive_locking(false), "Cookie");
    ASSERT_TRUE(live.Open(directory.AppendASCII("Cookies")));
    ASSERT_TRUE(live.Execute("UPDATE cookies SET top_frame_site_key='https://top.test'"));
    ASSERT_TRUE(base::PathExists(directory.AppendASCII("Cookies-wal")));
    auto partitioned = ReadBrowsingImportData(preview, reader());
    ASSERT_TRUE(partitioned->error.empty()) << partitioned->error;
    ASSERT_EQ(1u, partitioned->profiles[0].cookies.size());
    EXPECT_TRUE(partitioned->profiles[0].cookies[0].IsPartitioned());
  }
  ASSERT_TRUE(base::DeleteFile(directory.AppendASCII("Cookies")));
  ASSERT_TRUE(MakeCookieSource(directory, true, true));
  auto tampered = ReadBrowsingImportData(preview, reader());
  EXPECT_FALSE(tampered->error.empty());
  EXPECT_TRUE(tampered->profiles.empty());
  // Multiple source profiles still retain their source directories when merged.
  ASSERT_TRUE(base::CreateDirectory(source.GetPath().AppendASCII("Profile 1")));
  options.separate_profiles = false;
  const auto merged = ReadBrowserImport(options, source.GetPath());
  ASSERT_EQ(2u, merged.sources.size());
  EXPECT_EQ(merged.sources[0].destination_id, merged.sources[1].destination_id);
  EXPECT_NE(merged.sources[0].directory, merged.sources[1].directory);
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, BrowsingImportPersistsInDestinationServices) {
  base::ScopedAllowBlockingForTesting files;
  base::ScopedTempDir source;
  ASSERT_TRUE(source.CreateUniqueTempDir());
  const auto directory = source.GetPath().AppendASCII("Default");
  ASSERT_TRUE(base::CreateDirectory(directory));
  ASSERT_TRUE(MakeHistorySource(directory));
  ASSERT_TRUE(MakeCookieSource(directory, false));
  BrowserImportOptions options;
  options.browser = ImportBrowser::kChrome;
  options.bookmarks = false;
  // First import only the workspace. Later imports must also populate profiles
  // that already exist, without creating duplicate workspaces/history/cookies.
  auto apply = [&] {
    window_->PreviewBrowserImport(options, source.GetPath());
    EXPECT_TRUE(base::test::RunUntil([&] { return window_->import_ready(); }));
    EXPECT_TRUE(window_->ApplyArcImport());
    EXPECT_TRUE(base::test::RunUntil([&] { return !window_->profiles_busy(); }));
  };
  apply();
  options.history = options.cookies = true;
  apply();
  const size_t spaces = window_->workspaces().size();
  auto* profile = Profile::FromBrowserContext(window_->contents("chrome-space-Default")->GetBrowserContext());
  auto* service = HistoryServiceFactory::GetForProfile(profile, ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(service);
  base::CancelableTaskTracker tasks;
  auto query = [&] {
    base::test::TestFuture<history::QueryResults> queried;
    history::QueryOptions query_options;
    service->QueryHistory(u"Imported history", query_options, queried.GetCallback(), &tasks);
    auto rows = queried.Take();
    EXPECT_EQ(1u, rows.size());
    if (!rows.empty()) EXPECT_EQ(GURL("https://import.test/history"), rows[0].url());
  };
  query();
  auto cookies = [&](Profile* destination) {
    base::test::TestFuture<const std::vector<net::CanonicalCookie>&> future;
    destination->GetDefaultStoragePartition()->GetCookieManagerForBrowserProcess()->GetAllCookies(future.GetCallback());
    return future.Take();
  };
  auto imported = cookies(profile);
  ASSERT_EQ(1u, imported.size());
  EXPECT_EQ("imported-session", imported[0].Value());
  EXPECT_TRUE(cookies(work_).empty());
  EXPECT_TRUE(cookies(personal_).empty());
  auto local_cookie = net::CanonicalCookie::CreateForTesting(GURL("https://import.test/"),
      "session=local-session; Path=/; Max-Age=604800; Secure; HttpOnly; SameSite=Lax", base::Time::Now(), net::CookieSourceType::kHTTP);
  ASSERT_TRUE(local_cookie);
  base::test::TestFuture<net::CookieAccessResult> replaced;
  profile->GetDefaultStoragePartition()->GetCookieManagerForBrowserProcess()->SetCanonicalCookie(
      *local_cookie, GURL("https://import.test/"), net::CookieOptions::MakeAllInclusive(), replaced.GetCallback());
  EXPECT_TRUE(replaced.Get().status.IsInclude());
  apply();
  EXPECT_EQ(spaces, window_->workspaces().size());
  query();
  ASSERT_EQ(1u, cookies(profile).size());
  EXPECT_EQ("local-session", cookies(profile)[0].Value());
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_saved(); }));
  window_.reset();
  window_ = std::make_unique<WorkspaceWindow>(work_, personal_);
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_ready(); }));
  window_->Select("chrome-space-Default");
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->contents("chrome-space-Default") != nullptr; }));
  auto* restored = Profile::FromBrowserContext(window_->contents("chrome-space-Default")->GetBrowserContext());
  ASSERT_EQ(1u, cookies(restored).size());
  EXPECT_EQ("local-session", cookies(restored)[0].Value());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, ChromiumBrowserImportSelectionAndPersistence) {
  base::ScopedAllowBlockingForTesting test_files;
  base::FilePath source_root;
  base::ScopedTempDir source;
  {
    base::ScopedAllowBlockingForTesting allow;
    ASSERT_TRUE(source.CreateUniqueTempDir());
    source_root = source.GetPath();
    ASSERT_TRUE(base::CreateDirectory(source_root.AppendASCII("Default")));
    ASSERT_TRUE(base::CreateDirectory(source_root.AppendASCII("Profile 1")));
    ASSERT_TRUE(base::WriteFile(source_root.AppendASCII("Local State"), R"JSON({"profile":{"info_cache":{
      "Default":{"name":"Osobisty"},"Profile 1":{"name":"Praca"},"../unsafe":{"name":"Skip"}}}})JSON"));
    ASSERT_TRUE(base::WriteFile(source_root.AppendASCII("Default/Bookmarks"), R"JSON({"roots":{
      "bookmark_bar":{"type":"folder","name":"Pasek","children":[
        {"type":"url","name":"Docs","url":"https://example.test/docs"},
        {"type":"folder","name":"Projekt","children":[
          {"type":"url","name":"Nested","url":"https://example.test/nested"},
          {"type":"url","name":"Unsafe","url":"javascript:alert(1)"}]}]}}})JSON"));
    for (auto browser : {ImportBrowser::kChrome, ImportBrowser::kEdge, ImportBrowser::kBrave}) {
      BrowserImportOptions options;
      options.browser = browser;
      const auto preview = ReadBrowserImport(options, source_root);
      ASSERT_TRUE(preview.error.empty()) << preview.error;
      ASSERT_EQ(2u, preview.profiles.size());
      ASSERT_EQ(2u, preview.spaces.size());
      EXPECT_EQ(2u, preview.tab_count);
      EXPECT_EQ(2u, preview.folder_count);
      EXPECT_EQ(1u, preview.skipped_count);
      EXPECT_EQ(4u, preview.spaces[0].pins.size());
      EXPECT_EQ(preview.spaces[0].pins[2].id, preview.spaces[0].pins[3].parent_id);
      EXPECT_EQ("Praca", preview.spaces[1].name);
      options.bookmarks = false;
      options.separate_profiles = false;
      const auto filtered = ReadBrowserImport(options, source_root);
      ASSERT_TRUE(filtered.error.empty());
      ASSERT_EQ(1u, filtered.profiles.size());
      EXPECT_TRUE(filtered.spaces[0].pins.empty());
      EXPECT_EQ(filtered.spaces[0].profile_id, filtered.spaces[1].profile_id);
    }
  }
  BrowserImportOptions options;
  options.browser = ImportBrowser::kChrome;
  window_->PreviewBrowserImport(options, source_root);
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->import_ready(); }));
  ASSERT_TRUE(window_->ApplyArcImport());
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->profiles_busy(); }));
  EXPECT_EQ(4u, window_->workspaces().size());
  EXPECT_EQ(4u, window_->pinned_items("chrome-space-Default").size());
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_saved(); }));
  window_.reset();
  window_ = std::make_unique<WorkspaceWindow>(work_, personal_);
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_ready(); }));
  EXPECT_EQ(4u, window_->pinned_items("chrome-space-Default").size());
  // Source selection never writes to the original profile.
  {
    base::ScopedAllowBlockingForTesting allow;
    std::string data;
    ASSERT_TRUE(base::ReadFileToString(source_root.AppendASCII("Default/Bookmarks"), &data));
    EXPECT_NE(std::string::npos, data.find("javascript:alert(1)"));
    ASSERT_TRUE(base::WriteFile(source_root.AppendASCII("Default/Bookmarks"), "invalid"));
    EXPECT_FALSE(ReadBrowserImport(options, source_root).error.empty());
    options.bookmarks = false;
    EXPECT_TRUE(ReadBrowserImport(options, source_root).error.empty());
  }
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, ImportOptionsInvalidatePreview) {
  base::ScopedAllowBlockingForTesting files;
  base::ScopedTempDir source;
  ASSERT_TRUE(source.CreateUniqueTempDir());
  base::FilePath root;
  ASSERT_TRUE(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root));
  ASSERT_TRUE(base::CopyDirectory(root.AppendASCII("zitter/testdata/arc"), source.GetPath().AppendASCII("Arc"), true));
  const auto chrome = source.GetPath().AppendASCII("Google/Chrome");
  ASSERT_TRUE(base::CreateDirectory(chrome.AppendASCII("Default")));
  base::ScopedPathOverride app_data(base::DIR_APP_DATA, source.GetPath());
  auto* shell = window_->shell();
  shell->OpenImport();
  // Opening the panel automatically prepares the selected source.
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->import_ready(); }));
  EXPECT_FALSE(FindButton(shell->import_widget()->GetContentsView(), u"Sprawdź dane"));
  EXPECT_FALSE(shell->import_options().history);
  EXPECT_FALSE(shell->import_options().cookies);
  ClickControl(shell->import_history());
  ClickControl(shell->import_cookies());
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->import_ready(); }));
  EXPECT_TRUE(window_->import_preview()->options.history);
  EXPECT_TRUE(window_->import_preview()->options.cookies);
  ClickControl(shell->import_history());
  ClickControl(shell->import_cookies());
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->import_ready(); }));
  ASSERT_EQ(3u, window_->import_preview()->spaces.size());
  EXPECT_EQ(u"🚀", window_->import_preview()->spaces[0].emoji);
  shell->import_widget()->LayoutRootViewIfNecessary();
  EXPECT_TRUE(shell->import_widget()->GetWindowBoundsInScreen().Contains(
      shell->import_browser_picker()->GetBoundsInScreen()));
  ClickControl(shell->import_bookmarks());
  EXPECT_FALSE(window_->import_ready());
  EXPECT_FALSE(window_->ApplyArcImport());
  EXPECT_TRUE(shell->import_browser_picker()->GetEnabled());
  EXPECT_FALSE(shell->import_options().bookmarks);
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->import_ready(); }));
  EXPECT_EQ(0u, window_->import_preview()->tab_count);
  EXPECT_EQ(u"🚀", window_->import_preview()->spaces[0].emoji);
  EXPECT_EQ(u"👩🏽‍💻", window_->import_preview()->spaces[1].emoji);
  ClickControl(shell->import_profiles());
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->import_ready(); }));
  EXPECT_EQ(1u, window_->import_preview()->profiles.size());
  // Rapid source changes prepare only the final choice, without a check step.
  shell->SelectImportBrowserForTesting(1);
  shell->SelectImportBrowserForTesting(0);
  shell->SelectImportBrowserForTesting(1);
  EXPECT_FALSE(window_->import_ready());
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->import_ready(); }));
  ASSERT_EQ(1u, window_->import_preview()->spaces.size());
  EXPECT_EQ("chrome-space-Default", window_->import_preview()->spaces[0].id);
  // Replies from in-flight earlier reads must not replace the latest source.
  window_->PreviewArcImport(source.GetPath().AppendASCII("Arc"));
  BrowserImportOptions options = shell->import_options();
  window_->PreviewBrowserImport(options, chrome);
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->import_ready(); }));
  ASSERT_EQ(1u, window_->import_preview()->spaces.size());
  EXPECT_EQ("chrome-space-Default", window_->import_preview()->spaces[0].id);
  auto* apply = FindButton(shell->import_widget()->GetContentsView(), u"Importuj");
  ASSERT_TRUE(apply && apply->GetEnabled());
  ClickControl(apply);
  EXPECT_FALSE(shell->import_browser_picker()->GetEnabled());
  shell->OpenImport();
  window_->InvalidateImportPreview();
  EXPECT_TRUE(window_->import_ready());
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->profiles_busy(); }));
  EXPECT_EQ(3u, window_->workspaces().size());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, FirstRunImportReplacesStarterAndRollsBackOnFailure) {
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_saved(); }));
  window_.reset();
  const auto directory = g_browser_process->profile_manager()->user_data_dir();
  const auto file = directory.AppendASCII("ZitterWorkspaces.json");
  {
    base::ScopedAllowBlockingForTesting allow;
    ASSERT_TRUE(base::DeleteFile(file));
  }
  window_ = std::make_unique<WorkspaceWindow>(directory);
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_ready() && window_->configuration_saved(); }));
  ASSERT_EQ(1u, window_->workspaces().size());
  base::FilePath root;
  ASSERT_TRUE(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root));
  window_->PreviewArcImport(root.AppendASCII("zitter/testdata/arc"));
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->import_ready(); }));
  const auto backup = file.AddExtensionASCII("backup");
  {
    base::ScopedAllowBlockingForTesting allow;
    ASSERT_TRUE(base::Move(file, backup));
    ASSERT_TRUE(base::CreateDirectory(file));
  }
  ASSERT_TRUE(window_->ApplyArcImport());
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->profiles_busy(); }));
  ASSERT_EQ(1u, window_->workspaces().size());
  EXPECT_EQ("personal", window_->active_id());
  EXPECT_EQ(1u, window_->profiles().size());
  {
    base::ScopedAllowBlockingForTesting allow;
    ASSERT_TRUE(base::DeletePathRecursively(file));
    ASSERT_TRUE(base::Move(backup, file));
  }
  window_->PreviewArcImport(root.AppendASCII("zitter/testdata/arc"));
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->import_ready(); }));
  ASSERT_TRUE(window_->ApplyArcImport());
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->profiles_busy(); }));
  EXPECT_EQ(3u, window_->workspaces().size());
  EXPECT_EQ(3u, window_->profiles().size());
  for (const auto& space : window_->workspaces()) EXPECT_TRUE(space.id.starts_with("arc-space-"));
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_saved(); }));
  window_.reset();
  window_ = std::make_unique<WorkspaceWindow>(directory);
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_ready(); }));
  EXPECT_EQ(3u, window_->workspaces().size());
  EXPECT_FALSE(window_->shell()->import_visible());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, FirstRunImportOffer) {
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_saved(); }));
  window_.reset();
  const auto directory = g_browser_process->profile_manager()->user_data_dir();
  {
    base::ScopedAllowBlockingForTesting allow;
    ASSERT_TRUE(base::DeleteFile(directory.AppendASCII("ZitterWorkspaces.json")));
  }
  window_ = std::make_unique<WorkspaceWindow>(directory);
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_ready(); }));
  EXPECT_TRUE(window_->shell()->import_visible());
  ASSERT_EQ(1u, window_->workspaces().size());
  EXPECT_EQ("Moja przestrzeń", window_->workspaces()[0].name);
  EXPECT_EQ(1u, window_->profiles().size());
  auto* skip = window_->shell()->import_close();
  EXPECT_FALSE(FindButton(window_->shell()->import_widget()->GetContentsView(), u"Pomiń / Zamknij"));
  ASSERT_TRUE(skip);
  ClickControl(skip);
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_saved(); }));
  window_.reset();
  window_ = std::make_unique<WorkspaceWindow>(directory);
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_ready(); }));
  EXPECT_FALSE(window_->shell()->import_visible());
  EXPECT_EQ(1u, window_->workspaces().size());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, LiveArcImport) {
  if (!base::CommandLine::ForCurrentProcess()->HasSwitch("zitter-test-arc-import")) {
    GTEST_SKIP() << "Explicit local Arc import test only";
  }
  window_->PreviewArcImport(DefaultArcDirectory());
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->import_preview() != nullptr; }));
  const auto preview = *window_->import_preview();
  ASSERT_TRUE(preview.error.empty()) << preview.error;
  ASSERT_FALSE(preview.spaces.empty());
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->profiles_busy(); }));
  ASSERT_TRUE(window_->ApplyArcImport());
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->profiles_busy(); }));
  ASSERT_EQ(2u + preview.spaces.size(), window_->workspaces().size());
  for (const auto& space : preview.spaces) {
    ASSERT_TRUE(window_->Select(space.id));
    window_->shell()->ShowSidebar();
    base::RunLoop().RunUntilIdle();
    EXPECT_TRUE(window_->widget()->GetWindowBoundsInScreen().Contains(
        window_->shell()->workspace_button(window_->active_id())->GetBoundsInScreen()));
  }
  const auto builds = window_->shell()->pinned_list_build_count();
  base::TimeDelta maximum_switch;
  for (int iteration = 0; iteration < 10; ++iteration) {
    for (const auto& space : preview.spaces) {
      base::ElapsedTimer timer;
      ASSERT_TRUE(window_->Select(space.id));
      maximum_switch = std::max(maximum_switch, timer.Elapsed());
    }
  }
  EXPECT_EQ(builds, window_->shell()->pinned_list_build_count());
  EXPECT_LT(maximum_switch, base::Milliseconds(500));
  LOG(INFO) << "Cached Arc workspace switching: maximum " << maximum_switch.InMillisecondsF()
            << " ms; pinned list builds unchanged at " << builds;
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_saved(); }));
  window_.reset();
  window_ = std::make_unique<WorkspaceWindow>(work_, personal_);
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_ready(); }));
  EXPECT_EQ(2u + preview.profiles.size(), window_->profiles().size());
  for (const auto& space : preview.spaces) {
    const auto& restored = window_->pinned_items(space.id);
    ASSERT_EQ(space.pins.size(), restored.size());
    for (size_t i = 0; i < restored.size(); ++i) {
      EXPECT_TRUE(restored[i].id == space.pins[i].id && restored[i].parent_id == space.pins[i].parent_id &&
          restored[i].title == space.pins[i].title && restored[i].url == space.pins[i].url);
    }
  }
  LOG(INFO) << "Arc import verified: " << preview.profiles.size() << " profiles, " << preview.spaces.size()
            << " spaces, " << preview.folder_count << " folders, " << preview.tab_count
            << " pins; skipped " << preview.skipped_count;
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
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->active_id() == "personal"; }));
  auto* personal = window_->contents("personal");
  ASSERT_EQ("marker", content::EvalJs(personal, "document.activeElement.id"));
  TypePageCharacter(personal, u'y');
  ASSERT_EQ("y", content::EvalJs(personal, "document.querySelector('#marker').value"));
  SendPageShortcut(ui::VKEY_1, ui::DomCode::DIGIT1);
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->active_id() == "work"; }));
  EXPECT_EQ("x", content::EvalJs(work, "document.querySelector('#marker').value"));
  EXPECT_EQ(false, content::EvalJs(work, "keyEvents.includes('2')"));
  EXPECT_EQ(false, content::EvalJs(personal, "keyEvents.includes('1')"));
  // A plain press of the same digit after a chord must retain its keyup too.
  ASSERT_TRUE(content::ExecJs(work, "window.released = []; document.addEventListener('keyup', e => released.push(e.key));"));
  ui_controls::EnableUIControls();
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(window_->widget()->GetNativeWindow()));work->Focus();
  ASSERT_TRUE(content::ExecJs(work,"window.releaseReady = new Promise(resolve => document.addEventListener('keyup', e => {if(e.key==='1')resolve(true);}, {once:true})); void 0;"));
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(window_->widget()->GetNativeWindow(), ui::VKEY_1, false, false, false, false));
  EXPECT_EQ(true, content::EvalJs(work,"releaseReady"));
  EXPECT_EQ("x1",content::EvalJs(work,"document.querySelector('#marker').value"));
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
      window_->widget()->GetNativeWindow(), ui::VKEY_K, false, false, false, true));
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->shell()->address_visible(); }));
  EXPECT_TRUE(window_->shell()->address_input()->GetText().empty());
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
  // Workspace color is edited in the workspace settings panel.
  shell->OpenWorkspaceSettings();
  ASSERT_TRUE(shell->settings_visible());
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->settings_widget()->GetNativeWindow()));
  shell->color_input()->RequestFocus();
  const auto original_color = shell->workspace_color("work");
  shell->color_input()->SetText(u"invalid");
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(shell->settings_widget()->GetNativeWindow(),
      ui::VKEY_RETURN, false, false, false, false));
  EXPECT_EQ(original_color, shell->workspace_color("work"));
  shell->color_input()->SetText(u"#D49771");
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(shell->settings_widget()->GetNativeWindow(),
      ui::VKEY_RETURN, false, false, false, false));
  EXPECT_EQ(0xFFD49771u, shell->workspace_color("work"));
  shell->SetWorkspaceColor("personal", 0xFF73A4C2);
  shell->SetPinned(true);
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->appearance_saved(); }));
  window_.reset();
  window_ = std::make_unique<WorkspaceWindow>(work_, personal_);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window_->configuration_ready() && window_->shell()->appearance_ready();
  }));
  EXPECT_EQ(0xFFD49771u, window_->shell()->workspace_color("work"));
  EXPECT_EQ(0xFF73A4C2u, window_->shell()->workspace_color("personal"));
  EXPECT_TRUE(window_->shell()->pinned());
  EXPECT_TRUE(window_->shell()->sidebar_visible());
  window_->widget()->SetBounds(gfx::Rect(100, 100, 980, 720));
  EXPECT_EQ(window_->widget()->GetClientAreaBoundsInScreen().x() + 8,
            window_->shell()->sidebar_widget()->GetWindowBoundsInScreen().x());
}
#endif

#if BUILDFLAG(IS_MAC)
class ZitterMetalSidebarBrowserTest : public ZitterProfilesBrowserTest {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    ZitterProfilesBrowserTest::SetUpCommandLine(command_line);
    command_line->AppendSwitchASCII("zitter-sidebar", "rust-metal");
  }
};
IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, DownloadsSharedSceneScrollThemeCloseAndProfile) {
  window_->ShowDownloads();auto* panel=window_->downloads();auto* manager=work_->GetDownloadManager();ASSERT_TRUE(base::test::RunUntil([&]{return manager->IsManagerInitialized();}));
  std::string old_guid;for(int i=0;i<80;++i){auto name=std::string("download-row-")+std::to_string(i)+"-日本語.txt";auto path=work_->GetPath().Append(base::FilePath::FromUTF8Unsafe(name));auto* item=manager->CreateDownloadItem(base::Uuid::GenerateRandomV4().AsLowercaseString(),800+i,path,path,{embedded_test_server()->GetURL("/"+std::to_string(i)+".txt")},GURL(),content::StoragePartitionConfig::CreateDefault(work_),GURL(),GURL(),std::nullopt,"text/plain","text/plain",base::Time::Now()+base::Seconds(i),base::Time::Now(),"","",4,4,"",download::DownloadItem::COMPLETE,download::DOWNLOAD_DANGER_TYPE_NOT_DANGEROUS,download::DOWNLOAD_INTERRUPT_REASON_NONE,false,base::Time::Now(),false,{},base::FilePath());ASSERT_TRUE(item);if(i==79)old_guid=item->GetGuid();}
  ASSERT_TRUE(base::test::RunUntil([&]{return panel->visible_guids().size()==80&&panel->scene_contains_text_for_testing("download-row-79-日本語.txt");}));EXPECT_FALSE(panel->scene_contains_text_for_testing("download-row-0-日本語.txt"));EXPECT_FALSE(panel->scene_animating_for_testing());
  auto inspect=[&](auto&& self,views::View* view)->void{EXPECT_FALSE(views::AsViewClass<views::Label>(view));EXPECT_FALSE(views::AsViewClass<views::MdTextButton>(view));EXPECT_FALSE(views::AsViewClass<views::ProgressBar>(view));for(const auto& child:view->children())self(self,child.get());};inspect(inspect,panel->list_for_testing());auto* scroll=panel->viewport_for_testing();ASSERT_TRUE(scroll);
  auto* reveal=FindButton(panel->list_for_testing(),u"Pokaż w Finderze");ASSERT_TRUE(reveal);ASSERT_FALSE(reveal->GetVisibleBounds().IsEmpty());const gfx::Point point(10,10);ui::MouseEvent press(ui::EventType::kMousePressed,point,point,base::TimeTicks(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);ASSERT_TRUE(reveal->OnMousePressed(press));ASSERT_TRUE(SceneActionPressedForTesting(reveal));
  scroll->ScrollTo(scroll->maximum());ASSERT_TRUE(reveal->GetVisibleBounds().IsEmpty());EXPECT_FALSE(SceneActionPressedForTesting(reveal));ASSERT_TRUE(base::test::RunUntil([&]{return panel->scene_contains_text_for_testing("download-row-0-日本語.txt")&&!panel->scene_contains_text_for_testing("download-row-79-日本語.txt");}));panel->widget()->SetColorModeOverride(ui::ColorProviderKey::ColorMode::kDark);ASSERT_TRUE(base::test::RunUntil([&]{return panel->scene_order_correct_for_testing()&&panel->scene_contains_text_for_testing("download-row-0-日本語.txt");}));panel->widget()->CloseNow();ASSERT_TRUE(base::test::RunUntil([&]{return !panel->widget()&&!panel->scene_visible_for_testing();}));window_->ShowDownloads();ASSERT_TRUE(base::test::RunUntil([&]{return panel->scene_contains_text_for_testing("download-row-79-日本語.txt");}));ASSERT_TRUE(window_->Select("personal"));window_->ShowDownloads();ASSERT_TRUE(base::test::RunUntil([&]{return panel->visible_guids().empty()&&panel->scene_contains_text_for_testing("Nie ma jeszcze pobrań w tym profilu.");}));EXPECT_FALSE(panel->Act(old_guid,DownloadPanel::Action::kCancel));EXPECT_FALSE(panel->Act(old_guid,DownloadPanel::Action::kReveal));EXPECT_FALSE(panel->scene_contains_text_for_testing("download-row-79-日本語.txt"));panel->widget()->CloseNow();ASSERT_TRUE(base::test::RunUntil([&]{return !panel->widget();}));
}

IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, PageDialogSceneScrollPromptCutoutThemeAndCloseNow) {
  ASSERT_TRUE(window_->Select("work"));auto* page=window_->contents("work");ASSERT_TRUE(content::NavigateToURL(page,embedded_test_server()->GetURL("/storage_probe.html?dialog-scene")));auto* manager=PageDialogs::FromWebContents(page);ASSERT_TRUE(manager);window_->shell()->SetThemeMode(ThemeMode::kDark);
  int closed=0;base::ScopedClosureRunner cleanup(base::BindOnce([](PageDialogs* m,content::WebContents* p){m->CancelDialogs(p,true);base::RunLoop().RunUntilIdle();},base::Unretained(manager),base::Unretained(page)));
  std::u16string message=u"Dialog start 🦀\n";for(int i=0;i<120;++i)message+=u"Wiersz abc אבג\n";message+=u"Dialog end 🦀";
  auto open=[&](std::u16string text){bool suppressed=true;manager->RunJavaScriptDialog(page,page->GetPrimaryMainFrame(),content::JAVASCRIPT_DIALOG_TYPE_PROMPT,text,u"draft",base::BindOnce([](int* count,bool,const std::u16string&){++*count;},base::Unretained(&closed)),&suppressed);EXPECT_FALSE(suppressed);};
  open(message);ASSERT_TRUE(manager->dialog());auto* widget=manager->dialog();auto* field=manager->prompt_input();ASSERT_TRUE(field);EXPECT_EQ(ui::ColorProviderKey::ColorMode::kDark,widget->GetColorMode());
  ASSERT_TRUE(base::test::RunUntil([&]{return manager->scene_visible_for_testing()&&manager->scene_order_correct_for_testing()&&manager->scene_contains_text_for_testing("Dialog start 🦀");}));EXPECT_FALSE(manager->scene_contains_text_for_testing("Dialog end 🦀"));
  ASSERT_TRUE(base::test::RunUntil([&]{return manager->scene_excludes_for_testing(field->view());}));
  auto* body=manager->dialog_body_for_testing();ASSERT_TRUE(body);auto* scroll=manager->viewport_for_testing();ASSERT_TRUE(scroll);EXPECT_EQ(body->children()[1],scroll->view());EXPECT_EQ(window_->shell()->workspace_input()->rust_editor(),scroll->rust_owned());if(scroll->rust_owned())EXPECT_FALSE(views::AsViewClass<views::ScrollView>(scroll->view()));
  ui_controls::EnableUIControls();ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(widget->GetNativeWindow()));field->RequestFocus();base::test::TestFuture<void> presented;auto* compositor=widget->GetCompositor();ASSERT_TRUE(compositor);widget->GetRootView()->SchedulePaint();compositor->RequestSuccessfulPresentationTimeForNextFrame(base::BindOnce([](base::OnceClosure done,const viz::FrameTimingDetails&){std::move(done).Run();},presented.GetCallback()));compositor->ScheduleFullRedraw();ASSERT_TRUE(presented.Wait());
  scroll->ScrollTo(scroll->maximum());
  ASSERT_TRUE(base::test::RunUntil([&]{return manager->scene_contains_text_for_testing("Dialog end 🦀")&&!manager->scene_contains_text_for_testing("Dialog start 🦀");}));EXPECT_TRUE(manager->scene_excludes_for_testing(field->view()));
  widget->SetColorModeOverride(ui::ColorProviderKey::ColorMode::kLight);ASSERT_TRUE(base::test::RunUntil([&]{return manager->scene_order_correct_for_testing()&&manager->scene_contains_text_for_testing("Dialog end 🦀");}));
  auto lifetime=field->input_client()->AsWeakPtr();widget->CloseNow();ASSERT_TRUE(base::test::RunUntil([&]{return !manager->dialog()&&!lifetime&&closed==1;}));EXPECT_FALSE(manager->scene_visible_for_testing());
  open(u"Fresh dialog 🦀");ASSERT_TRUE(base::test::RunUntil([&]{return manager->scene_contains_text_for_testing("Fresh dialog 🦀");}));EXPECT_FALSE(manager->scene_contains_text_for_testing("Dialog end 🦀"));manager->CancelDialogs(page,true);ASSERT_TRUE(base::test::RunUntil([&]{return !manager->dialog()&&closed==2;}));EXPECT_FALSE(manager->scene_visible_for_testing());
}

IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, ToolsChoicesPostedActionsRejectChangedSelection) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_ready()&&shell->appearance_ready();}));
  ASSERT_TRUE(shell->UpsertSearchProvider({"Choice A","zchoicea","https://choice.test/a?q={query}"}));ASSERT_TRUE(shell->UpsertSearchProvider({"Choice B","zchoiceb","https://choice.test/b?q={query}"}));shell->OpenTools(2);
  auto provider_index=[&](std::string key){const auto& values=shell->browser_tools().providers;auto found=std::find_if(values.begin(),values.end(),[&](const auto& item){return item.keyword==key;});return found==values.end()?size_t(0):size_t(found-values.begin()+1);};
  auto a=provider_index("zchoicea"),b=provider_index("zchoiceb");ASSERT_GT(a,0u);ASSERT_GT(b,0u);shell->ActivateToolsChoiceForTesting("selection",a);auto* remove=FindButton(shell->tools_widget()->GetContentsView(),u"Usuń wyszukiwarkę");ASSERT_TRUE(remove);ClickControl(remove);shell->ActivateToolsChoiceForTesting("selection",b);base::RunLoop().RunUntilIdle();EXPECT_GT(provider_index("zchoicea"),0u);EXPECT_GT(provider_index("zchoiceb"),0u);
  ClickControl(remove);ASSERT_TRUE(base::test::RunUntil([&]{return provider_index("zchoiceb")==0;}));EXPECT_GT(provider_index("zchoicea"),0u);
  shell->OpenTools(1);const auto shortcuts=shell->browser_tools().shortcuts;ASSERT_GE(shortcuts.size(),2u);shell->ActivateToolsChoiceForTesting("selection",0);auto* disable=FindButton(shell->tools_widget()->GetContentsView(),u"Wyłącz wybrany skrót");ASSERT_TRUE(disable);ClickControl(disable);shell->ActivateToolsChoiceForTesting("selection",1);base::RunLoop().RunUntilIdle();EXPECT_EQ(shortcuts[0].accelerator,shell->browser_tools().shortcuts[0].accelerator);EXPECT_EQ(shortcuts[1].accelerator,shell->browser_tools().shortcuts[1].accelerator);
  ClickControl(disable);ASSERT_TRUE(base::test::RunUntil([&]{return shell->browser_tools().shortcuts[1].accelerator.key_code()==ui::VKEY_UNKNOWN;}));EXPECT_EQ(shortcuts[0].accelerator,shell->browser_tools().shortcuts[0].accelerator);
}

IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, ToolsTimelinePostedDeleteCannotBorrowFilteredTarget) {
  ASSERT_TRUE(window_->Select("work"));auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_ready()&&shell->appearance_ready();}));
  ASSERT_TRUE(window_->SaveSnapshot("Ticket alpha"));ASSERT_TRUE(window_->SaveSnapshot("Ticket beta"));shell->OpenTools(0);
  auto filter=[&](std::u16string text){auto* field=shell->timeline_filter_input();field->SelectRange(gfx::Range(0,field->GetText().size()));field->input_client()->InsertText(text,ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);};
  auto has=[&](std::u16string title){const auto state=window_->TimeMachine();return std::any_of(state.snapshots.begin(),state.snapshots.end(),[&](const auto& item){return item.title==title;});};
  filter(u"Ticket alpha");ASSERT_EQ(1u,shell->tools_choice_count_for_testing("selection"));auto* button=FindButton(shell->tools_widget()->GetContentsView(),u"Usuń wybrany snapshot");ASSERT_TRUE(button);
  ClickControl(button);
  // The button posts its command. A newer filter must not lend index zero's
  // new target to the older click while that command is waiting in the queue.
  filter(u"Ticket beta");ASSERT_EQ(1u,shell->tools_choice_count_for_testing("selection"));base::RunLoop().RunUntilIdle();EXPECT_TRUE(has(u"Ticket alpha"));EXPECT_TRUE(has(u"Ticket beta"));
  ClickControl(button);ASSERT_TRUE(base::test::RunUntil([&]{return !has(u"Ticket beta");}));EXPECT_TRUE(has(u"Ticket alpha"));
}

IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, ToolsChromeSharedUnderlayCutoutsScrollingAndHistoryRows) {
  ASSERT_TRUE(window_->Select("work"));auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_ready()&&shell->appearance_ready();}));
  auto inspect=[&]{size_t copies=0;auto visit=[&](auto&& self,views::View* view)->void{if(views::AsViewClass<views::Button>(view))return;ui::AXNodeData ax;view->GetViewAccessibility().GetAccessibleNodeData(&ax);if(ax.role==ax::mojom::Role::kTextField)return;EXPECT_FALSE(views::AsViewClass<views::Label>(view));if(ax.role==ax::mojom::Role::kStaticText)++copies;for(const auto& child:view->children())self(self,child.get());};visit(visit,shell->tools_widget()->GetContentsView());EXPECT_GE(copies,3u);};
  for(int page=0;page<4;++page){shell->OpenTools(page);ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_chrome_visible_for_testing()&&shell->tools_chrome_order_correct_for_testing();}))<<shell->tools_chrome_debug_for_testing();inspect();
    EXPECT_TRUE(shell->tools_chrome_excludes_for_testing(shell->tools_close_for_testing()));
    auto* editor=page==0?shell->snapshot_input():page==1?shell->shortcut_input():page==2?shell->provider_input(0):shell->history_search();editor->view()->ScrollRectToVisible(editor->view()->GetLocalBounds());
    ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_chrome_excludes_for_testing(editor->view());}));
    auto* choice=page==3?shell->history_period():shell->tools_selection();choice->ScrollRectToVisible(choice->GetLocalBounds());ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_chrome_excludes_for_testing(choice);}));
  }
  shell->OpenTools(0);auto* field=shell->snapshot_input()->view();auto* spacer=field->parent()->AddChildView(std::make_unique<views::View>());spacer->SetPreferredSize(gfx::Size(1,1200));shell->tools_widget()->LayoutRootViewIfNecessary();
  base::test::TestFuture<void> presented;auto* compositor=shell->tools_widget()->GetCompositor();ASSERT_TRUE(compositor);compositor->RequestSuccessfulPresentationTimeForNextFrame(base::BindOnce([](base::OnceClosure done,const viz::FrameTimingDetails&){std::move(done).Run();},presented.GetCallback()));compositor->ScheduleFullRedraw();ASSERT_TRUE(presented.Wait());
  auto* scroll=shell->tools_viewport_for_testing();ASSERT_TRUE(scroll);scroll->ScrollTo(scroll->maximum());
  ASSERT_TRUE(base::test::RunUntil([&]{return field->GetVisibleBounds().IsEmpty();}));
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_chrome_contains_text_for_testing("Time Machine · Praca");}));
  field->ScrollRectToVisible(field->GetLocalBounds());ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_chrome_excludes_for_testing(field);}));
  shell->tools_widget()->Hide();ASSERT_TRUE(base::test::RunUntil([&]{return !shell->tools_chrome_visible_for_testing();}));shell->OpenTools(2);shell->SetThemeMode(ThemeMode::kDark);
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_chrome_order_correct_for_testing()&&shell->tools_chrome_contains_text_for_testing("Wyszukiwarki i prefiksy");}));
  history::URLRow entry(GURL("https://tools-chrome.test/entry"));entry.set_title(u"Native result 🦀");entry.set_visit_count(1);entry.set_last_visit(base::Time::Now());
  HistoryServiceFactory::GetForProfile(work_,ServiceAccessType::EXPLICIT_ACCESS)->AddPagesWithDetails({entry},history::SOURCE_OS_MIGRATION_IMPORTED);
  shell->OpenTools(3);ASSERT_TRUE(base::test::RunUntil([&]{return shell->history_loaded();}));ASSERT_EQ(1u,shell->history_entries().size());
  auto* row=shell->history_results_view()->children()[0].get();ASSERT_TRUE(row);row->ScrollRectToVisible(row->GetLocalBounds());
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_chrome_contains_text_for_testing("Native result 🦀");}));EXPECT_FALSE(shell->tools_chrome_excludes_for_testing(row));EXPECT_TRUE(row->children().empty());inspect();
  shell->SetHistoryResults(u"","work-profile",200,{},false,true);
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_chrome_contains_text_for_testing("Nie udało się odczytać historii.");}));
  EXPECT_TRUE(shell->tools_chrome_order_correct_for_testing());
}

IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, ToolsHistoryRowsSharedCanvasScrollingAndCancelledPress) {
  ASSERT_TRUE(window_->Select("work"));auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_ready();}));
  history::URLRows rows;for(int i=0;i<80;++i){history::URLRow row(GURL("https://history-gpu.test/"+std::to_string(i)));row.set_title(base::UTF8ToUTF16("History GPU "+std::to_string(i)+" 🦀"));row.set_visit_count(1);row.set_last_visit(base::Time::Now()-base::Minutes(i+1));rows.push_back(row);}
  HistoryServiceFactory::GetForProfile(work_,ServiceAccessType::EXPLICIT_ACCESS)->AddPagesWithDetails(rows,history::SOURCE_OS_MIGRATION_IMPORTED);
  shell->OpenTools(3);ASSERT_TRUE(base::test::RunUntil([&]{return shell->history_loaded();}));ASSERT_EQ(80u,shell->history_entries().size());
  auto* list=shell->history_results_view();for(const auto& child:list->children()){EXPECT_TRUE(child->children().empty());EXPECT_FALSE(views::AsViewClass<views::Button>(child));}
  auto* first=list->children()[0].get();ASSERT_TRUE(first);first->ScrollRectToVisible(first->GetLocalBounds());
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_chrome_contains_text_for_testing("History GPU 0 🦀");}));EXPECT_FALSE(shell->tools_chrome_excludes_for_testing(first));
  EXPECT_TRUE(shell->tools_chrome_order_correct_for_testing());EXPECT_TRUE(shell->tools_chrome_excludes_for_testing(shell->tools_close_for_testing()));
  ui_controls::EnableUIControls();ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->tools_widget()->GetNativeWindow()));first->RequestFocus();
  base::test::TestFuture<void> presented;auto* compositor=shell->tools_widget()->GetCompositor();ASSERT_TRUE(compositor);compositor->RequestSuccessfulPresentationTimeForNextFrame(base::BindOnce([](base::OnceClosure done,const viz::FrameTimingDetails&){std::move(done).Run();},presented.GetCallback()));compositor->ScheduleFullRedraw();ASSERT_TRUE(presented.Wait());
  const auto tabs=window_->tab_count("work");const gfx::Point point(12,12);ui::MouseEvent press(ui::EventType::kMousePressed,point,point,base::TimeTicks(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);ui::MouseEvent release(ui::EventType::kMouseReleased,point,point,base::TimeTicks(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);
  ASSERT_TRUE(first->OnMousePressed(press));auto* scroll=shell->tools_viewport_for_testing();ASSERT_TRUE(scroll);
  scroll->ScrollTo(scroll->maximum());ASSERT_TRUE(base::test::RunUntil([&]{return first->GetVisibleBounds().IsEmpty();}));
  ASSERT_TRUE(base::test::RunUntil([&]{return !shell->tools_chrome_contains_text_for_testing("History GPU 0 🦀")&&shell->tools_chrome_contains_text_for_testing("History GPU 79 🦀");}));
  first->OnMouseReleased(release);base::RunLoop().RunUntilIdle();EXPECT_EQ(tabs,window_->tab_count("work"));EXPECT_TRUE(shell->history_visible());
  first->ScrollRectToVisible(first->GetLocalBounds());ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_chrome_contains_text_for_testing("History GPU 0 🦀");}));
  ASSERT_TRUE(first->OnMousePressed(press));shell->tools_widget()->Hide();ASSERT_TRUE(base::test::RunUntil([&]{return !shell->tools_chrome_visible_for_testing();}));first->OnMouseReleased(release);base::RunLoop().RunUntilIdle();EXPECT_EQ(tabs,window_->tab_count("work"));
  views::ViewTracker previous(first);shell->OpenTools(3);EXPECT_FALSE(previous.view());ASSERT_TRUE(base::test::RunUntil([&]{return shell->history_loaded();}));shell->SetThemeMode(ThemeMode::kDark);
  auto* current=shell->history_results_view()->children()[0].get();current->ScrollRectToVisible(current->GetLocalBounds());ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_chrome_contains_text_for_testing("History GPU 0 🦀")&&shell->tools_chrome_order_correct_for_testing();}));
  ASSERT_TRUE(window_->Select("personal"));shell->OpenTools(3);ASSERT_TRUE(base::test::RunUntil([&]{return shell->history_loaded();}));EXPECT_TRUE(shell->history_entries().empty());
  ASSERT_TRUE(base::test::RunUntil([&]{return !shell->tools_chrome_contains_text_for_testing("History GPU 0 🦀");}));
}

IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, ToolsActionsScenesBatchingClippingAndCancelledChildren) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_ready()&&shell->appearance_ready();}));
  const std::array<std::vector<std::u16string>,4> titles={{{u"Zapisz snapshot",u"Przywróć wybrany układ",u"Usuń wybrany snapshot",u"Przywróć wybraną kartę",u"Uśpij tę przestrzeń"},{u"Nagraj kombinację…",u"Zapisz skrót",u"Wyłącz wybrany skrót",u"Przywróć schemat Arc"},{u"Zapisz wyszukiwarkę",u"Ustaw jako domyślną",u"Usuń wyszukiwarkę"},{u"Wyczyść historię…",u"Pokaż więcej"}}};
  const std::array<size_t,4> groups={4,1,1,2};
  auto present=[&]{base::test::TestFuture<void> ready;auto* compositor=shell->tools_widget()->GetCompositor();ASSERT_TRUE(compositor);compositor->RequestSuccessfulPresentationTimeForNextFrame(base::BindOnce([](base::OnceClosure done,const viz::FrameTimingDetails&){std::move(done).Run();},ready.GetCallback()));compositor->ScheduleFullRedraw();ASSERT_TRUE(ready.Wait());};
  for(int page=0;page<4;++page){
    shell->OpenTools(page);ASSERT_EQ(groups[page],shell->tools_action_groups_for_testing());ASSERT_NO_FATAL_FAILURE(present());
    ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_close_metal_visible_for_testing();}));
    for(const auto& title:titles[page]){
      auto* button=FindButton(shell->tools_widget()->GetContentsView(),title);ASSERT_TRUE(button);EXPECT_FALSE(views::AsViewClass<views::MdTextButton>(button));EXPECT_TRUE(button->children().empty());
      if(!button->GetVisible())continue;
      button->ScrollRectToVisible(button->GetLocalBounds());
      ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_action_metal_visible_for_testing(button)&&shell->tools_actions_contains_scene_text_for_testing(base::UTF16ToUTF8(title));}));
      auto expected=[&]{auto* group=button->parent();auto bounds=group->GetVisibleBounds();bounds.Offset(group->GetBoundsInScreen().x(),group->GetBoundsInScreen().y());return bounds;};
      ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_action_metal_bounds_for_testing(button)==expected();}));
    }
  }
  shell->OpenTools(1);auto* first=FindButton(shell->tools_widget()->GetContentsView(),u"Nagraj kombinację…");ASSERT_TRUE(first);auto* group=first->parent();
  auto* scrolling_fixture=group->parent()->AddChildView(std::make_unique<views::View>());scrolling_fixture->SetPreferredSize(gfx::Size(1,1200));shell->tools_widget()->LayoutRootViewIfNecessary();ASSERT_NO_FATAL_FAILURE(present());first->ScrollRectToVisible(first->GetLocalBounds());
  const gfx::Point point(12,12);ui::MouseEvent press(ui::EventType::kMousePressed,point,point,base::TimeTicks(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);
  ui::MouseEvent release(ui::EventType::kMouseReleased,point,point,base::TimeTicks(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);
  ASSERT_TRUE(first->OnMousePressed(press));auto* scroll=shell->tools_viewport_for_testing();ASSERT_TRUE(scroll);
  scroll->ScrollTo(group->y()+first->bounds().bottom()+20);
  ASSERT_TRUE(base::test::RunUntil([&]{return first->GetVisibleBounds().IsEmpty()&&!group->GetVisibleBounds().IsEmpty();}))<<"first="<<first->GetVisibleBounds().ToString()<<" group="<<group->GetVisibleBounds().ToString()<<" viewport="<<scroll->visible_rect().ToString();
  first->OnMouseReleased(release);base::RunLoop().RunUntilIdle();EXPECT_EQ(ui::TEXT_INPUT_TYPE_TEXT,shell->shortcut_input()->input_client()->GetTextInputType());
  first->ScrollRectToVisible(first->GetLocalBounds());ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_action_metal_visible_for_testing(first);}));
  ASSERT_TRUE(first->OnMousePressed(press));shell->tools_widget()->Hide();first->OnMouseReleased(release);base::RunLoop().RunUntilIdle();EXPECT_FALSE(shell->tools_widget()->IsVisible());
  ASSERT_TRUE(base::test::RunUntil([&]{return !shell->tools_action_metal_visible_for_testing(first)&&!shell->tools_close_metal_visible_for_testing();}));
  views::ViewTracker old(first);shell->OpenTools(2);EXPECT_FALSE(old.view());
}

IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, ToolsChoicesScenesClippingHideRebuildAndResize) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready()&&shell->tools_ready();}));
  window_->widget()->SetBounds(gfx::Rect(50,50,640,520));
  auto present=[&]{base::test::TestFuture<void> ready;auto* compositor=shell->tools_widget()->GetCompositor();ASSERT_TRUE(compositor);compositor->RequestSuccessfulPresentationTimeForNextFrame(base::BindOnce([](base::OnceClosure done,const viz::FrameTimingDetails&){std::move(done).Run();},ready.GetCallback()));compositor->ScheduleFullRedraw();ASSERT_TRUE(ready.Wait());};
  for(int page=0;page<4;++page){
    shell->OpenTools(page);shell->tools_widget()->LayoutRootViewIfNecessary();ASSERT_NO_FATAL_FAILURE(present());
    for(const auto* key:(page==0?std::vector<const char*>{"selection","closed"}:std::vector<const char*>{page==3?"history_period":"selection"})){
      auto* picker=shell->tools_choice_for_testing(key);ASSERT_TRUE(picker);EXPECT_TRUE(picker->children().empty());EXPECT_FALSE(views::AsViewClass<views::Button>(picker));EXPECT_FALSE(views::AsViewClass<views::Combobox>(picker));
      picker->ScrollRectToVisible(picker->GetLocalBounds());ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_choice_metal_visible_for_testing(key);}));
      auto expected=[&]{auto r=picker->GetVisibleBounds();r.Offset(picker->GetBoundsInScreen().x(),picker->GetBoundsInScreen().y());return r;};
      ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_choice_metal_bounds_for_testing(key)==expected();}));
      ui::AXNodeData ax;picker->GetViewAccessibility().GetAccessibleNodeData(&ax);EXPECT_EQ(ax::mojom::Role::kComboBoxSelect,ax.role);
    }
  }
  shell->OpenTools(0);auto* top=shell->tools_selection();
  // Make the offscreen precondition independent of native minimum panel size.
  // This fixture-only spacer leaves the real controls and services unchanged.
  auto* scrolling_fixture=top->parent()->AddChildView(std::make_unique<views::View>());
  scrolling_fixture->SetPreferredSize(gfx::Size(1,1200));
  shell->tools_widget()->LayoutRootViewIfNecessary();ASSERT_NO_FATAL_FAILURE(present());top->ScrollRectToVisible(top->GetLocalBounds());
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_choice_metal_visible_for_testing("selection");}));
  auto* scroll=shell->tools_viewport_for_testing();ASSERT_TRUE(scroll);
  ASSERT_GT(scroll->maximum(),0);scroll->ScrollTo(scroll->maximum());
  ASSERT_TRUE(base::test::RunUntil([&]{return top->GetVisibleBounds().IsEmpty();}))<<"control="<<top->GetBoundsInScreen().ToString()<<" visible="<<top->GetVisibleBounds().ToString()<<" viewport="<<scroll->visible_rect().ToString()<<" contents="<<scroll->contents()->size().ToString()<<" scroll="<<scroll->offset()<<"/"<<scroll->maximum();ASSERT_TRUE(base::test::RunUntil([&]{return !shell->tools_choice_metal_visible_for_testing("selection");}));
  top->ScrollRectToVisible(top->GetLocalBounds());ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_choice_metal_visible_for_testing("selection");}));
  shell->tools_widget()->Hide();ASSERT_TRUE(base::test::RunUntil([&]{return !shell->tools_choice_metal_visible_for_testing("selection");}));
  shell->tools_widget()->Show();shell->tools_widget()->Activate();ASSERT_TRUE(base::test::RunUntil([&]{return shell->tools_choice_metal_visible_for_testing("selection");}));
  window_->widget()->SetBounds(gfx::Rect(90,90,900,720));shell->SetThemeMode(ThemeMode::kDark);
  ASSERT_TRUE(base::test::RunUntil([&]{auto r=top->GetVisibleBounds();r.Offset(top->GetBoundsInScreen().x(),top->GetBoundsInScreen().y());return shell->tools_choice_metal_bounds_for_testing("selection")==r;}));
  views::ViewTracker old(top);shell->OpenTools(2);EXPECT_FALSE(old.view());
}

IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, PreferenceChromeSharedLayerCutoutsThemeAndScrolling) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));window_->widget()->SetBounds(gfx::Rect(40,40,1000,860));shell->OpenGeneralSettings();
  size_t copies=0;auto inspect=[&](auto&& self,views::View* view)->void{EXPECT_FALSE(views::AsViewClass<views::Label>(view));ui::AXNodeData node;view->GetViewAccessibility().GetAccessibleNodeData(&node);if(node.role==ax::mojom::Role::kStaticText)++copies;for(const auto& child:view->children())self(self,child.get());};inspect(inspect,shell->general_widget()->GetContentsView());EXPECT_GT(copies,80u);
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->preference_chrome_visible_for_testing()&&shell->preference_chrome_order_correct_for_testing()&&shell->preference_chrome_contains_text_for_testing("Preferencje");}))<<shell->preference_chrome_debug_for_testing();
  for(auto* view:std::array<views::View*,4>{shell->preferences_search()->view(),shell->theme_picker(),shell->preference_switch_for_testing("sidebar"),shell->preference_close_for_testing()})ASSERT_TRUE(base::test::RunUntil([&]{return shell->preference_chrome_excludes_for_testing(view);}))<<view->GetBoundsInScreen().ToString();
  auto filter=[&](std::u16string text){auto* field=shell->preferences_search();field->SelectRange(gfx::Range(0,field->GetText().size()));field->input_client()->InsertText(text,ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);shell->general_widget()->LayoutRootViewIfNecessary();};
  filter(u"Czcionka standardowa");auto* editor=shell->preference_textfield("font_standard");ASSERT_TRUE(base::test::RunUntil([&]{return shell->preference_chrome_contains_text_for_testing("Czcionka standardowa")&&editor->metal_visible()&&shell->preference_chrome_excludes_for_testing(editor->view());}));shell->SetThemeMode(ThemeMode::kDark);ASSERT_TRUE(base::test::RunUntil([&]{return shell->preference_chrome_order_correct_for_testing()&&shell->preference_chrome_contains_text_for_testing("Czcionka standardowa");}));
  filter(u"no-such-setting-815927");ASSERT_TRUE(base::test::RunUntil([&]{return shell->preference_chrome_contains_text_for_testing("Brak pasujących ustawień. Spróbuj innego hasła.")&&!shell->preference_chrome_contains_text_for_testing("Czcionka standardowa");}));
  filter(u"czcion");auto* bottom=shell->preference_textfield("font_fixed")->view();auto* scroll=shell->preference_viewport_for_testing();ASSERT_TRUE(scroll);ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->general_widget()->GetNativeWindow()));base::test::TestFuture<void> presented;auto* compositor=shell->general_widget()->GetCompositor();ASSERT_TRUE(compositor);compositor->RequestSuccessfulPresentationTimeForNextFrame(base::BindOnce([](base::OnceClosure done,const viz::FrameTimingDetails&){std::move(done).Run();},presented.GetCallback()));compositor->ScheduleFullRedraw();ASSERT_TRUE(presented.Wait());scroll->ScrollTo(scroll->maximum());ASSERT_TRUE(base::test::RunUntil([&]{return bottom->GetVisibleBounds().height()==bottom->height();}));ASSERT_TRUE(base::test::RunUntil([&]{return shell->preference_chrome_excludes_for_testing(bottom)&&shell->preference_chrome_order_correct_for_testing();}));
  shell->general_widget()->Hide();ASSERT_TRUE(base::test::RunUntil([&]{return !shell->preference_chrome_visible_for_testing();}));shell->OpenGeneralSettings();ASSERT_TRUE(base::test::RunUntil([&]{return shell->preference_chrome_visible_for_testing()&&shell->preference_chrome_order_correct_for_testing();}));
}

IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, PreferenceNavigationScenesCancellationAndRetainedQuery) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));window_->widget()->SetBounds(gfx::Rect(40,40,1000,860));shell->OpenGeneralSettings();ASSERT_TRUE(base::test::RunUntil([&]{return shell->preference_navigation_metal_visible_for_testing()&&shell->preference_close_metal_visible_for_testing();}));
  auto* first=shell->preference_navigation_for_testing(0);ASSERT_TRUE(first);ASSERT_EQ(10u,first->parent()->children().size());for(size_t i=0;i<10;++i){auto* button=shell->preference_navigation_for_testing(i);ASSERT_TRUE(button);EXPECT_TRUE(button->children().empty());EXPECT_FALSE(views::AsViewClass<views::Button>(button));ui::AXNodeData node;button->GetViewAccessibility().GetAccessibleNodeData(&node);EXPECT_EQ(ax::mojom::Role::kButton,node.role);EXPECT_FALSE(node.GetString16Attribute(ax::mojom::StringAttribute::kName).empty());}
  auto* close=shell->preference_close_for_testing();EXPECT_TRUE(close->children().empty());EXPECT_FALSE(views::AsViewClass<views::Button>(close));
  const gfx::Point point(12,12);ui::MouseEvent press(ui::EventType::kMousePressed,point,point,base::TimeTicks(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON),release(ui::EventType::kMouseReleased,point,point,base::TimeTicks(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);
  auto* fonts=shell->preference_navigation_for_testing(4);ASSERT_TRUE(fonts->OnMousePressed(press));ASSERT_TRUE(window_->Select("personal"));fonts->OnMouseReleased(release);EXPECT_EQ(0u,shell->preference_category_for_testing());shell->OpenGeneralSettings();
  auto* languages=shell->preference_navigation_for_testing(7);ASSERT_TRUE(languages->OnMousePressed(press));window_->widget()->SetBounds(gfx::Rect(40,40,700,660));shell->OpenGeneralSettings();languages->OnMouseReleased(release);EXPECT_EQ(0u,shell->preference_category_for_testing());ASSERT_TRUE(base::test::RunUntil([&]{return !shell->preference_navigation_metal_visible_for_testing();}));
  shell->ActivatePreferenceChoiceForTesting("category",7);EXPECT_EQ(7u,shell->preference_category_for_testing());window_->widget()->SetBounds(gfx::Rect(40,40,1000,860));shell->OpenGeneralSettings();ASSERT_TRUE(base::test::RunUntil([&]{return shell->preference_navigation_metal_visible_for_testing();}));
  auto* query=shell->preferences_search();query->SelectRange(gfx::Range(0,query->GetText().size()));query->input_client()->InsertText(u"czcion",ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);shell->general_widget()->LayoutRootViewIfNecessary();EXPECT_EQ(7u,shell->preference_category_for_testing());EXPECT_TRUE(shell->preference_textfield("font_standard")->view()->IsDrawn());for(size_t i=0;i<10;++i)EXPECT_FALSE(shell->preference_navigation_selected_for_testing(i));
  ASSERT_TRUE(close->OnMousePressed(press));shell->general_widget()->Hide();close->OnMouseReleased(release);ASSERT_TRUE(base::test::RunUntil([&]{return !shell->preference_close_metal_visible_for_testing();}));shell->OpenGeneralSettings();EXPECT_EQ(u"czcion",query->GetText());EXPECT_EQ(7u,shell->preference_category_for_testing());EXPECT_TRUE(shell->preference_textfield("font_standard")->view()->IsDrawn());ASSERT_TRUE(base::test::RunUntil([&]{return shell->preference_navigation_metal_visible_for_testing();}));
  ActivateSidebarRow(languages);EXPECT_TRUE(query->GetText().empty());EXPECT_TRUE(shell->preference_textfield("accept_languages")->view()->IsDrawn());EXPECT_EQ(7u,shell->preference_choice_selection_for_testing("category"));
}

IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, PreferenceActionsScenesCancelledTargetsAndViewport) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->OpenGeneralSettings();
  size_t actions=0;auto inventory=[&](auto&& self,views::View* view)->void{EXPECT_FALSE(views::AsViewClass<views::MdTextButton>(view));{auto* button=view;ui::AXNodeData node;button->GetViewAccessibility().GetAccessibleNodeData(&node);const auto name=node.GetString16Attribute(ax::mojom::StringAttribute::kName);if(node.role==ax::mojom::Role::kButton&&(name.starts_with(u"Zapisz:")||name==u"Zmień folder pobierania"||name==u"Zapisz wyjątek"||name==u"Wyczyść…"||name==u"Historia przeglądania"||name==u"Kolor i nazwa przestrzeni"||name==u"Zapisane hasła"||name==u"Profile przeglądania"||name==u"Import z przeglądarki"||name==u"Ustawienia proxy systemowego"||name==u"Certyfikaty systemowe"||name==u"Wyszukiwarki i prefiksy"||name==u"Skróty klawiszowe"||name==u"Time Machine przestrzeni")){++actions;EXPECT_TRUE(button->children().empty());EXPECT_FALSE(views::AsViewClass<views::Button>(button));}}for(const auto& child:view->children())self(self,child.get());};inventory(inventory,shell->general_widget()->GetContentsView());EXPECT_EQ(19u,actions);
  auto filter=[&](std::u16string text){auto* field=shell->preferences_search();field->SelectRange(gfx::Range(0,field->GetText().size()));field->input_client()->InsertText(text,ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);shell->general_widget()->LayoutRootViewIfNecessary();};
  filter(u"Czcionka standardowa");auto* field=shell->preference_textfield("font_standard");auto* save=shell->preference_save_button_for_testing("font_standard");save->ScrollRectToVisible(save->GetLocalBounds());ASSERT_TRUE(base::test::RunUntil([&]{return shell->preference_action_metal_visible_for_testing(save);}));
  const gfx::Point point(12,12);ui::MouseEvent press(ui::EventType::kMousePressed,point,point,base::TimeTicks(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON),release(ui::EventType::kMouseReleased,point,point,base::TimeTicks(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);
  field->SetText(u"Cancelled draft");ASSERT_TRUE(save->OnMousePressed(press));ASSERT_TRUE(window_->Select("personal"));shell->OpenGeneralSettings();const auto personal=window_->ReadPreferences().at("font_standard").text;field->SetText(u"Must not save");save->OnMouseReleased(release);EXPECT_EQ(personal,window_->ReadPreferences().at("font_standard").text);
  filter(u"Czcionka standardowa");ASSERT_TRUE(save->OnMousePressed(press));shell->general_widget()->Hide();save->OnMouseReleased(release);EXPECT_EQ(personal,window_->ReadPreferences().at("font_standard").text);ASSERT_TRUE(base::test::RunUntil([&]{return !shell->preference_action_metal_visible_for_testing(save);}));
  shell->OpenGeneralSettings();filter(u"a");auto* action=FindButton(shell->general_widget()->GetContentsView(),u"Kolor i nazwa przestrzeni");ASSERT_TRUE(action);action->ScrollRectToVisible(action->GetLocalBounds());ASSERT_TRUE(base::test::RunUntil([&]{return shell->preference_action_metal_visible_for_testing(action);}));
  auto* scroll=shell->preference_viewport_for_testing();ASSERT_TRUE(scroll);ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->general_widget()->GetNativeWindow()));base::test::TestFuture<void> presented;auto* compositor=shell->general_widget()->GetCompositor();ASSERT_TRUE(compositor);compositor->RequestSuccessfulPresentationTimeForNextFrame(base::BindOnce([](base::OnceClosure done,const viz::FrameTimingDetails&){std::move(done).Run();},presented.GetCallback()));compositor->ScheduleFullRedraw();ASSERT_TRUE(presented.Wait());ASSERT_TRUE(action->OnMousePressed(press));scroll->ScrollTo(scroll->maximum());ASSERT_TRUE(base::test::RunUntil([&]{return action->GetVisibleBounds().IsEmpty()&&!shell->preference_action_metal_visible_for_testing(action);}));action->OnMouseReleased(release);EXPECT_FALSE(shell->settings_visible());
  action->ScrollRectToVisible(action->GetLocalBounds());ASSERT_TRUE(base::test::RunUntil([&]{return shell->preference_action_metal_visible_for_testing(action);}));ClickControl(action);ASSERT_TRUE(base::test::RunUntil([&]{return shell->settings_visible();}));
}

IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, PreferenceSwitchesRustScenesCancellationAndScroll) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->OpenGeneralSettings();
  for(const auto* key:{"sidebar","animations","referrers","https_only","third_party_cookies","do_not_track","preconnect","link_auditing","camera","microphone","location","notifications","clipboard","javascript","images","popups","sound","save_passwords","auto_signin","download_prompt","spellcheck","online_spellcheck","caret_browsing","hardware_acceleration"}){
    auto* control=shell->preference_switch_for_testing(key);ASSERT_TRUE(control)<<key;EXPECT_FALSE(views::AsViewClass<views::ToggleButton>(control));EXPECT_TRUE(control->children().empty());EXPECT_FALSE(views::AsViewClass<views::Button>(control));
    ui::AXNodeData node;control->GetViewAccessibility().GetAccessibleNodeData(&node);EXPECT_EQ(ax::mojom::Role::kSwitch,node.role);EXPECT_EQ(shell->preference_switch_checked_for_testing(key)?ax::mojom::CheckedState::kTrue:ax::mojom::CheckedState::kFalse,node.GetCheckedState());
  }
  auto filter=[&](std::u16string query){auto* field=shell->preferences_search();field->SelectRange(gfx::Range(0,field->GetText().size()));if(query.empty())field->input_client()->DeleteRange(gfx::Range(0,field->GetText().size()));else field->input_client()->InsertText(query,ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);shell->general_widget()->LayoutRootViewIfNecessary();};
  auto expect_checked=[](views::View* control,bool checked){ui::AXNodeData node;control->GetViewAccessibility().GetAccessibleNodeData(&node);EXPECT_EQ(checked?ax::mojom::CheckedState::kTrue:ax::mojom::CheckedState::kFalse,node.GetCheckedState());};
  filter(u"Kamera");auto* camera=shell->preference_toggle("camera");camera->ScrollRectToVisible(camera->GetLocalBounds());ASSERT_TRUE(base::test::RunUntil([&]{return shell->preference_switch_metal_visible_for_testing("camera");}));
  const gfx::Point point(12,12);ui::MouseEvent press(ui::EventType::kMousePressed,point,point,base::TimeTicks(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);ui::MouseEvent release(ui::EventType::kMouseReleased,point,point,base::TimeTicks(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);
  ASSERT_TRUE(camera->OnMousePressed(press));ASSERT_TRUE(window_->Select("personal"));camera->OnMouseReleased(release);EXPECT_EQ(1,window_->ReadPreferences().at("camera").value);
  shell->OpenGeneralSettings();filter(u"Kamera");camera->SetEnabled(false);expect_checked(camera,true);ClickControl(camera);EXPECT_TRUE(shell->preference_switch_checked_for_testing("camera"));camera->SetEnabled(true);expect_checked(camera,true);
  ASSERT_TRUE(camera->OnMousePressed(press));expect_checked(camera,true);shell->general_widget()->Hide();camera->OnMouseReleased(release);EXPECT_TRUE(shell->preference_switch_checked_for_testing("camera"));expect_checked(camera,true);
  ASSERT_TRUE(base::test::RunUntil([&]{return !shell->preference_switch_metal_visible_for_testing("camera")&&!shell->preference_switch_timer_running_for_testing("camera");}));
  shell->OpenGeneralSettings();filter(u"Wyczyść dane przeglądania");auto* cookies=shell->clear_checkbox_for_testing(1);ASSERT_TRUE(cookies);cookies->ScrollRectToVisible(cookies->GetLocalBounds());ASSERT_TRUE(base::test::RunUntil([&]{return shell->clear_choices_metal_visible_for_testing();}));
  ASSERT_TRUE(cookies->OnMousePressed(press));expect_checked(cookies,false);shell->general_widget()->Hide();cookies->OnMouseReleased(release);expect_checked(cookies,false);ASSERT_TRUE(base::test::RunUntil([&]{return !shell->clear_choices_metal_visible_for_testing();}));
  shell->OpenGeneralSettings();filter(u"Wyczyść dane przeglądania");expect_checked(cookies,false);
  shell->OpenGeneralSettings();filter(u"a");auto* top=shell->preference_switch_for_testing("sidebar");top->ScrollRectToVisible(top->GetLocalBounds());ASSERT_TRUE(base::test::RunUntil([&]{return shell->preference_switch_metal_visible_for_testing("sidebar");}));
  auto* scroll=shell->preference_viewport_for_testing();ASSERT_TRUE(scroll);
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->general_widget()->GetNativeWindow()));base::test::TestFuture<void> presented;auto* compositor=shell->general_widget()->GetCompositor();ASSERT_TRUE(compositor);compositor->RequestSuccessfulPresentationTimeForNextFrame(base::BindOnce([](base::OnceClosure done,const viz::FrameTimingDetails&){std::move(done).Run();},presented.GetCallback()));compositor->ScheduleFullRedraw();ASSERT_TRUE(presented.Wait());
  scroll->ScrollTo(scroll->maximum());ASSERT_TRUE(base::test::RunUntil([&]{return top->GetVisibleBounds().IsEmpty();}));ASSERT_TRUE(base::test::RunUntil([&]{return !shell->preference_switch_metal_visible_for_testing("sidebar")&&!shell->preference_switch_timer_running_for_testing("sidebar");}));
  top->ScrollRectToVisible(top->GetLocalBounds());ASSERT_TRUE(base::test::RunUntil([&]{return shell->preference_switch_metal_visible_for_testing("sidebar");}));ASSERT_TRUE(base::test::RunUntil([&]{return !shell->preference_switch_timer_running_for_testing("sidebar");}));
}

IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, PreferenceChoicesScenesIdentityScrollAndCancellation) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready()&&shell->tools_ready();}));
  window_->widget()->SetBounds(gfx::Rect(60,60,700,660));shell->OpenGeneralSettings();
  auto filter=[&](std::u16string query){auto* field=shell->preferences_search();field->SelectRange(gfx::Range(0,field->GetText().size()));if(query.empty())field->input_client()->DeleteRange(gfx::Range(0,field->GetText().size()));else field->input_client()->InsertText(query,ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);shell->general_widget()->LayoutRootViewIfNecessary();};
  for(const auto* key:{"theme","search","category","zoom","font_size","minimum_font_size","fixed_font_size","dns_mode","site_type","site_policy","clear_period"}){
    auto* picker=shell->preference_choice_for_testing(key);ASSERT_TRUE(picker);EXPECT_FALSE(views::AsViewClass<views::Combobox>(picker));EXPECT_FALSE(views::AsViewClass<views::Button>(picker));EXPECT_TRUE(picker->children().empty());
    ui::AXNodeData ax;picker->GetViewAccessibility().GetAccessibleNodeData(&ax);EXPECT_EQ(ax::mojom::Role::kComboBoxSelect,ax.role);
  }
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->preference_choice_metal_visible_for_testing("theme");}));shell->ActivatePreferenceChoiceForTesting("theme",2);EXPECT_EQ(ThemeMode::kDark,shell->theme_mode());
  shell->ActivatePreferenceChoiceForTesting("category",4);EXPECT_EQ(4u,shell->preference_choice_selection_for_testing("category"));
  filter(u"czcion");auto* font=shell->preference_picker("font_size");font->ScrollRectToVisible(font->GetLocalBounds());
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->preference_choice_metal_visible_for_testing("font_size");}));
  auto expected=font->GetVisibleBounds();expected.Offset(font->GetBoundsInScreen().x(),font->GetBoundsInScreen().y());
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->preference_choice_metal_bounds_for_testing("font_size")==expected;}));
  auto* bottom=shell->preference_textfield("font_fixed")->view();bottom->ScrollRectToVisible(bottom->GetLocalBounds());
  auto* scroller=shell->preference_viewport_for_testing();
  ASSERT_TRUE(scroller);ASSERT_GT(scroller->maximum(),0);
  scroller->ScrollTo(scroller->maximum());
  ASSERT_TRUE(base::test::RunUntil([&]{return font->GetVisibleBounds().IsEmpty();}))<<"font="<<font->GetBoundsInScreen().ToString()<<" visible="<<font->GetVisibleBounds().ToString()<<" viewport="<<scroller->visible_rect().ToString()<<" contents="<<scroller->contents()->size().ToString()<<" scroll="<<scroller->offset()<<"/"<<scroller->maximum();
  ASSERT_TRUE(base::test::RunUntil([&]{return !shell->preference_choice_metal_visible_for_testing("font_size");}));
  font->ScrollRectToVisible(font->GetLocalBounds());ASSERT_TRUE(base::test::RunUntil([&]{return shell->preference_choice_metal_visible_for_testing("font_size");}));
  ASSERT_TRUE(shell->UpsertSearchProvider({"Wyszukiwarka 🦀","zsame","https://provider.test/?q={query}"}));
  ASSERT_TRUE(shell->UpsertSearchProvider({"Wyszukiwarka 🦀","zother","https://other.test/?q={query}"}));
  filter(u"Domyślna wyszukiwarka");auto* search=shell->search_engine_picker();search->ScrollRectToVisible(search->GetLocalBounds());
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->preference_choice_metal_visible_for_testing("search");}));
  const auto find=[&](std::string key){const auto& providers=shell->browser_tools().providers;return std::find_if(providers.begin(),providers.end(),[&](const auto& provider){return provider.keyword==key;})-providers.begin();};
  shell->ActivatePreferenceChoiceForTesting("search",find("zsame"));EXPECT_EQ("zsame",shell->browser_tools().default_keyword);
  const gfx::Point point(12,12);ui::MouseEvent press(ui::EventType::kMousePressed,point,point,base::TimeTicks(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);ui::MouseEvent release(ui::EventType::kMouseReleased,point,point,base::TimeTicks(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);
  ASSERT_TRUE(search->OnMousePressed(press));ASSERT_TRUE(shell->UpsertSearchProvider({"Wyszukiwarka 🦀","zrenamed","https://provider.test/?q={query}"},"zsame"));search->OnMouseReleased(release);
  EXPECT_EQ("zrenamed",shell->browser_tools().default_keyword);ui::AXNodeData collapsed;search->GetViewAccessibility().GetAccessibleNodeData(&collapsed);EXPECT_TRUE(collapsed.HasState(ax::mojom::State::kCollapsed));EXPECT_EQ(u"Wyszukiwarka 🦀",collapsed.GetString16Attribute(ax::mojom::StringAttribute::kValue));
  shell->general_widget()->Hide();ASSERT_TRUE(base::test::RunUntil([&]{return !shell->preference_choice_metal_visible_for_testing("search");}));shell->ActivatePreferenceChoiceForTesting("search",find("zother"));EXPECT_EQ("zrenamed",shell->browser_tools().default_keyword);
  shell->OpenGeneralSettings();ASSERT_TRUE(base::test::RunUntil([&]{return shell->preference_choice_metal_visible_for_testing("search");}));
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_saved();}));
}

// Compile only. Native popup, IME and Metal acceptance awaits the GUI checkpoint.
IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, ProfilePickerScenesStableIDsPrefixAndModelCancellation) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->OpenWorkspaceSettings();ASSERT_TRUE(base::test::RunUntil([&]{return shell->profile_picker_metal_visible(false);}));auto* assignment=shell->profile_picker();EXPECT_FALSE(views::AsViewClass<views::Combobox>(assignment));for(const auto& child:assignment->children())EXPECT_FALSE(views::AsViewClass<views::Label>(child.get()));
  ASSERT_TRUE(shell->profile_picker_selection(false));EXPECT_EQ("work-profile",window_->profiles()[*shell->profile_picker_selection(false)].id);shell->SetProfilePickerSelectionForTesting(false,1);ui::AXNodeData chosen;assignment->GetViewAccessibility().GetAccessibleNodeData(&chosen);EXPECT_EQ(ax::mojom::Role::kComboBoxSelect,chosen.role);EXPECT_EQ(base::UTF8ToUTF16(window_->profiles()[1].name),chosen.GetString16Attribute(ax::mojom::StringAttribute::kValue));shell->OpenWorkspaceSettings();ASSERT_TRUE(shell->profile_picker_selection(false));EXPECT_EQ("work-profile",window_->profiles()[*shell->profile_picker_selection(false)].id);
  shell->OpenProfiles("work-profile");ASSERT_TRUE(base::test::RunUntil([&]{return shell->profile_picker_metal_visible(true);}));auto* managed=shell->managed_profile_picker();EXPECT_FALSE(views::AsViewClass<views::Combobox>(managed));ASSERT_TRUE(window_->CreateProfile("Picker Łódź 🦀"));ASSERT_TRUE(base::test::RunUntil([&]{return !window_->profiles_busy()&&shell->profile_picker_selection(true)==2u;}));const auto added=window_->profiles().back().id;ASSERT_TRUE(base::test::RunUntil([&]{return shell->profile_panel_contains_scene_text("Picker Łódź 🦀");}));EXPECT_EQ(3u,shell->profile_picker_count());
  ASSERT_TRUE(window_->RenameProfile(added,"Picker 日本語"));ASSERT_TRUE(base::test::RunUntil([&]{return !window_->profiles_busy()&&shell->profile_panel_contains_scene_text("Picker 日本語");}));ASSERT_TRUE(shell->profile_picker_selection(true));EXPECT_EQ(added,window_->profiles()[*shell->profile_picker_selection(true)].id);
  auto* remove=FindButton(shell->profile_panel_for_testing(),u"Usuń profil i jego dane");ASSERT_TRUE(remove&&remove->GetEnabled());ClickControl(remove);ASSERT_TRUE(FindButton(shell->profile_panel_for_testing(),u"Potwierdź usunięcie profilu i danych"));shell->ActivateProfilePickerSelectionForTesting(true,0);EXPECT_FALSE(FindButton(shell->profile_panel_for_testing(),u"Potwierdź usunięcie profilu i danych"));EXPECT_EQ(base::UTF8ToUTF16(window_->profiles()[0].name),shell->profile_name_input()->GetText());EXPECT_EQ(3u,window_->profiles().size());
  managed->RequestFocus();ASSERT_TRUE(managed->HasFocus());auto* input=managed->GetInputMethod()->GetTextInputClient();ASSERT_TRUE(input);input->InsertText(u"picker",ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);ASSERT_TRUE(shell->profile_picker_selection(true));EXPECT_EQ(added,window_->profiles()[*shell->profile_picker_selection(true)].id);EXPECT_EQ(u"Picker 日本語",shell->profile_name_input()->GetText());managed->SetEnabled(false);shell->ActivateProfilePickerSelectionForTesting(true,0);EXPECT_EQ(2u,shell->profile_picker_selection(true));managed->SetEnabled(true);
  const gfx::Point point(12,12);ui::MouseEvent press(ui::EventType::kMousePressed,point,point,base::TimeTicks(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);ui::MouseEvent release(ui::EventType::kMouseReleased,point,point,base::TimeTicks(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);ASSERT_TRUE(managed->OnMousePressed(press));ASSERT_TRUE(window_->RenameProfile(added,"Picker after update"));ASSERT_TRUE(base::test::RunUntil([&]{return !window_->profiles_busy();}));managed->OnMouseReleased(release);ui::AXNodeData collapsed;managed->GetViewAccessibility().GetAccessibleNodeData(&collapsed);EXPECT_TRUE(collapsed.HasState(ax::mojom::State::kCollapsed));EXPECT_EQ(u"Picker after update",collapsed.GetString16Attribute(ax::mojom::StringAttribute::kValue));
  shell->profiles_widget()->Hide();ASSERT_TRUE(base::test::RunUntil([&]{return !shell->profile_picker_metal_visible(true);}));shell->OpenProfiles(added);ASSERT_TRUE(base::test::RunUntil([&]{return shell->profile_picker_metal_visible(true)&&shell->profile_panel_contains_scene_text("Picker after update");}));
}
// Compile only. Real popup, IME and Metal acceptance awaits the GUI checkpoint.
IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, ImportSourceScenePrefixAXDisabledAndVisibility) {
  base::ScopedAllowBlockingForTesting files;base::ScopedTempDir source;ASSERT_TRUE(source.CreateUniqueTempDir());base::FilePath root;ASSERT_TRUE(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT,&root));ASSERT_TRUE(base::CopyDirectory(root.AppendASCII("zitter/testdata/arc"),source.GetPath().AppendASCII("Arc"),true));ASSERT_TRUE(base::CreateDirectory(source.GetPath().AppendASCII("Google/Chrome/Default")));base::ScopedPathOverride app_data(base::DIR_APP_DATA,source.GetPath());
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->OpenImport();ASSERT_TRUE(base::test::RunUntil([&]{return window_->import_ready()&&shell->import_source_metal_visible();}));auto* picker=shell->import_browser_picker();ASSERT_TRUE(picker);EXPECT_FALSE(views::AsViewClass<views::Combobox>(picker));for(const auto& child:picker->children())EXPECT_FALSE(views::AsViewClass<views::Label>(child.get()));EXPECT_TRUE(shell->import_panel_contains_scene_text("Arc"));
  ui::AXNodeData data;picker->GetViewAccessibility().GetAccessibleNodeData(&data);EXPECT_EQ(ax::mojom::Role::kComboBoxSelect,data.role);EXPECT_EQ(u"Arc",data.GetString16Attribute(ax::mojom::StringAttribute::kValue));EXPECT_TRUE(data.HasState(ax::mojom::State::kCollapsed));
  picker->RequestFocus();ASSERT_TRUE(picker->HasFocus());auto* input=picker->GetInputMethod()->GetTextInputClient();ASSERT_TRUE(input);input->InsertText(u"g",ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);EXPECT_EQ(ImportBrowser::kChrome,shell->import_options().browser);ASSERT_TRUE(base::test::RunUntil([&]{return window_->import_ready()&&shell->import_panel_contains_scene_text("Google Chrome");}));
  shell->SetImportStatus(u"Importowanie…",false,true,true);shell->SelectImportBrowserForTesting(0);input->InsertText(u"b",ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);EXPECT_EQ(ImportBrowser::kChrome,shell->import_options().browser);shell->SetImportStatus(u"Gotowe",true);
  shell->SelectImportBrowserForTesting(0);ASSERT_TRUE(base::test::RunUntil([&]{return window_->import_ready();}));shell->SelectImportBrowserForTesting(99);EXPECT_EQ(ImportBrowser::kArc,shell->import_options().browser);
  const gfx::Point point(12,12);ui::MouseEvent press(ui::EventType::kMousePressed,point,point,base::TimeTicks(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);ui::MouseEvent release(ui::EventType::kMouseReleased,point,point,base::TimeTicks(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);ASSERT_TRUE(picker->OnMousePressed(press));shell->import_widget()->Hide();picker->OnMouseReleased(release);shell->SelectImportBrowserForTesting(1);EXPECT_EQ(ImportBrowser::kArc,shell->import_options().browser);ASSERT_TRUE(base::test::RunUntil([&]{return !shell->import_source_metal_visible();}));shell->OpenImport();ASSERT_TRUE(base::test::RunUntil([&]{return window_->import_ready()&&shell->import_source_metal_visible();}));
}
// Compile only. Checkbox rendering/input acceptance awaits the GUI checkpoint.
IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, ImportCheckboxScenesAXSpaceCaptureDisabledAndVisibility) {
  base::ScopedAllowBlockingForTesting files;base::ScopedTempDir source;ASSERT_TRUE(source.CreateUniqueTempDir());base::FilePath root;ASSERT_TRUE(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT,&root));ASSERT_TRUE(base::CopyDirectory(root.AppendASCII("zitter/testdata/arc"),source.GetPath().AppendASCII("Arc"),true));ASSERT_TRUE(base::CreateDirectory(source.GetPath().AppendASCII("Google/Chrome/Default")));base::ScopedPathOverride app_data(base::DIR_APP_DATA,source.GetPath());
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->OpenImport();ASSERT_TRUE(base::test::RunUntil([&]{return window_->import_ready()&&shell->import_choices_metal_visible();}));
  const auto defaults=shell->import_options();EXPECT_TRUE(defaults.separate_profiles&&defaults.bookmarks);EXPECT_FALSE(defaults.history||defaults.cookies);
  for(auto* check:{shell->import_profiles(),shell->import_bookmarks(),shell->import_history(),shell->import_cookies()}) {EXPECT_FALSE(views::AsViewClass<views::Checkbox>(check));EXPECT_FALSE(views::AsViewClass<views::Button>(check));for(const auto& child:check->children())EXPECT_FALSE(views::AsViewClass<views::Label>(child.get()));ui::AXNodeData data;check->GetViewAccessibility().GetAccessibleNodeData(&data);EXPECT_EQ(ax::mojom::Role::kCheckBox,data.role);}
  ASSERT_TRUE(shell->import_panel_contains_scene_text("Ciasteczka i sesje witryn"));
  const gfx::Point point(12,12);ui::MouseEvent press(ui::EventType::kMousePressed,point,point,base::TimeTicks(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);ui::MouseEvent release(ui::EventType::kMouseReleased,point,point,base::TimeTicks(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);auto* history=shell->import_history();ASSERT_TRUE(history->OnMousePressed(press));history->OnMouseCaptureLost();history->OnMouseReleased(release);EXPECT_FALSE(shell->import_options().history);EXPECT_TRUE(window_->import_ready());
  ui::KeyEvent space(ui::EventType::kKeyPressed,ui::VKEY_SPACE,ui::EF_NONE);ui::KeyEvent up(ui::EventType::kKeyReleased,ui::VKEY_SPACE,ui::EF_NONE);history->RequestFocus();ASSERT_TRUE(history->OnKeyPressed(space));history->OnKeyReleased(up);EXPECT_TRUE(shell->import_options().history);ASSERT_TRUE(base::test::RunUntil([&]{return window_->import_ready();}));
  ui::AXNodeData checked;history->GetViewAccessibility().GetAccessibleNodeData(&checked);EXPECT_EQ(static_cast<int>(ax::mojom::CheckedState::kTrue),checked.GetIntAttribute(ax::mojom::IntAttribute::kCheckedState));
  shell->SetImportStatus(u"Importowanie…",false,true,true);auto* cookies=shell->import_cookies();EXPECT_FALSE(cookies->GetEnabled());ClickControl(cookies);EXPECT_FALSE(shell->import_options().cookies);shell->SetImportStatus(u"Gotowe",true);
  ASSERT_TRUE(history->OnKeyPressed(space));history->OnKeyReleased(up);ASSERT_TRUE(base::test::RunUntil([&]{return window_->import_ready();}));EXPECT_FALSE(shell->import_options().history);
  auto* bookmarks=shell->import_bookmarks();ASSERT_TRUE(bookmarks->OnMousePressed(press));shell->SelectImportBrowserForTesting(1);bookmarks->OnMouseReleased(release);EXPECT_TRUE(shell->import_options().bookmarks);ASSERT_TRUE(base::test::RunUntil([&]{return window_->import_ready();}));ASSERT_TRUE(base::test::RunUntil([&]{return shell->import_panel_contains_scene_text("Zakładki i foldery");}));EXPECT_EQ(u"Zakładki i foldery",bookmarks->GetTooltipText());
  ui::AXActionData action;action.action=ax::mojom::Action::kDoDefault;EXPECT_TRUE(cookies->HandleAccessibleAction(action));EXPECT_TRUE(shell->import_options().cookies);ASSERT_TRUE(base::test::RunUntil([&]{return window_->import_ready();}));
  ASSERT_TRUE(cookies->OnMousePressed(press));shell->import_widget()->Hide();cookies->OnMouseReleased(release);EXPECT_TRUE(shell->import_options().cookies);ASSERT_TRUE(base::test::RunUntil([&]{return !shell->import_choices_metal_visible();}));shell->OpenImport();ASSERT_TRUE(base::test::RunUntil([&]{return window_->import_ready()&&shell->import_choices_metal_visible();}));
}
// Compile only; real import UI acceptance awaits the requested checkpoint.
IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, ImportFormScenesLatestOptionsLocksAndAdmission) {
  base::ScopedAllowBlockingForTesting files;base::ScopedTempDir source;ASSERT_TRUE(source.CreateUniqueTempDir());base::FilePath root;ASSERT_TRUE(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT,&root));
  ASSERT_TRUE(base::CopyDirectory(root.AppendASCII("zitter/testdata/arc"),source.GetPath().AppendASCII("Arc"),true));const auto chrome=source.GetPath().AppendASCII("Google/Chrome");ASSERT_TRUE(base::CreateDirectory(chrome.AppendASCII("Default")));base::ScopedPathOverride app_data(base::DIR_APP_DATA,source.GetPath());
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->OpenImport();ASSERT_TRUE(base::test::RunUntil([&]{return window_->import_ready()&&shell->import_panel_contains_scene_text("Przenieś się do Zittera");}));
  auto* panel=shell->import_panel_for_testing();ASSERT_TRUE(panel);auto no_labels=[&](auto&& self,views::View* view)->void {if(views::AsViewClass<views::Combobox>(view)||views::AsViewClass<views::Checkbox>(view))return;EXPECT_FALSE(views::AsViewClass<views::Label>(view));for(const auto& child:view->children())self(self,child.get());};no_labels(no_labels,panel);
  auto* apply=FindButton(panel,u"Importuj");ASSERT_TRUE(apply);EXPECT_FALSE(views::AsViewClass<views::MdTextButton>(apply));ASSERT_TRUE(base::test::RunUntil([&]{return shell->import_panel_contains_scene_text("Importuj");}));
  shell->SelectImportBrowserForTesting(1);shell->SelectImportBrowserForTesting(0);shell->SelectImportBrowserForTesting(1);
  EXPECT_FALSE(window_->import_ready());ASSERT_TRUE(shell->import_browser_picker()->GetEnabled());ASSERT_TRUE(base::test::RunUntil([&]{return window_->import_ready();}));EXPECT_EQ("chrome-space-Default",window_->import_preview()->spaces[0].id);ui::AXNodeData bookmarks_name;shell->import_bookmarks()->GetViewAccessibility().GetAccessibleNodeData(&bookmarks_name);EXPECT_EQ(u"Zakładki i foldery",bookmarks_name.GetString16Attribute(ax::mojom::StringAttribute::kName));
  // Programmatic changes do not emit a Checkbox callback; Apply must rescan.
  shell->SetImportOptionCheckedForTesting(2,true);ClickControl(apply);EXPECT_FALSE(window_->import_ready());ASSERT_TRUE(base::test::RunUntil([&]{return window_->import_ready();}));EXPECT_TRUE(window_->import_preview()->options.history);
  ClickControl(shell->import_history());ASSERT_TRUE(base::test::RunUntil([&]{return window_->import_ready();}));EXPECT_FALSE(window_->import_preview()->options.history);
  shell->SetImportStatus(u"Błąd odczytu 日本語",false);ASSERT_TRUE(base::test::RunUntil([&]{return shell->import_panel_contains_scene_text("Błąd odczytu 日本語");}));EXPECT_FALSE(apply->GetEnabled());EXPECT_TRUE(shell->import_browser_picker()->GetEnabled());
  shell->SetImportStatus(u"Importowanie…",false,true,true);EXPECT_FALSE(shell->import_browser_picker()->GetEnabled());EXPECT_FALSE(shell->import_cookies()->GetEnabled());EXPECT_FALSE(shell->import_close()->GetEnabled());
  shell->SetImportStatus(u"Gotowe do importu",true);EXPECT_TRUE(apply->GetEnabled());EXPECT_TRUE(shell->import_close()->GetEnabled());
  ui::KeyEvent space(ui::EventType::kKeyPressed,ui::VKEY_SPACE,ui::EF_NONE);ui::KeyEvent up(ui::EventType::kKeyReleased,ui::VKEY_SPACE,ui::EF_NONE);apply->RequestFocus();ASSERT_TRUE(apply->OnKeyPressed(space));apply->OnKeyReleased(up);
  EXPECT_FALSE(shell->import_browser_picker()->GetEnabled());ASSERT_TRUE(base::test::RunUntil([&]{return !window_->profiles_busy();}));EXPECT_EQ(3u,window_->workspaces().size());ASSERT_TRUE(base::test::RunUntil([&]{return window_->configuration_saved();}));
}
// Compile only: action/scene acceptance is deferred to the requested GUI checkpoint.
IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, FormActionGroupsSceneTextDynamicLabelsAndCancelledTargets) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->SetWorkspaceAnimations(false);
  window_->widget()->SetBounds(display::Screen::Get()->GetDisplayNearestWindow(window_->widget()->GetNativeWindow()).work_area());shell->OpenWorkspaceSettings();
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->form_action_canvases_visible_for_testing(false)==2;}));ASSERT_TRUE(shell->form_actions_contains_scene_text(false,"Zapisz nazwę i profil"));
  auto find=[&](bool profiles,std::u16string_view name){return FindButton(profiles?shell->profile_panel_for_testing():shell->workspace_panel_for_testing(),name);};
  auto check=[&](views::View* button)->bool{if(!button)return false;EXPECT_FALSE(views::AsViewClass<views::MdTextButton>(button));EXPECT_FALSE(views::AsViewClass<views::Button>(button));for(const auto& child:button->children())EXPECT_FALSE(views::AsViewClass<views::Label>(child.get()));return true;};
  auto* create=find(false,u"Nowa przestrzeń…");ASSERT_TRUE(check(create));const gfx::Point point(12,12);ui::MouseEvent press(ui::EventType::kMousePressed,point,point,base::TimeTicks(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);ui::MouseEvent release(ui::EventType::kMouseReleased,point,point,base::TimeTicks(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);
  ASSERT_TRUE(create->OnMousePressed(press));create->OnMouseReleased(release);ASSERT_TRUE(base::test::RunUntil([&]{return shell->form_actions_contains_scene_text(false,"Utwórz przestrzeń");}));EXPECT_EQ(1u,shell->form_action_canvases_visible_for_testing(false));
  auto* save=find(false,u"Utwórz przestrzeń");ASSERT_TRUE(check(save));shell->workspace_input()->SetText(u"Must not create");ASSERT_TRUE(save->OnMousePressed(press));ASSERT_TRUE(window_->Select("personal"));save->OnMouseReleased(release);EXPECT_EQ(2u,window_->workspaces().size());EXPECT_EQ("Prywatne",window_->workspaces()[1].name);
  shell->OpenWorkspaceSettings();auto* manage=find(false,u"Zarządzaj profilami");ASSERT_TRUE(check(manage));ASSERT_TRUE(manage->OnMousePressed(press));manage->OnMouseReleased(release);
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->form_action_canvases_visible_for_testing(true)==2;}));ASSERT_TRUE(shell->form_actions_contains_scene_text(true,"Utwórz profil"));auto* profile_create=find(true,u"Utwórz profil");ASSERT_TRUE(check(profile_create));
  shell->profile_name_input()->SetText(u"  ");ui::KeyEvent space(ui::EventType::kKeyPressed,ui::VKEY_SPACE,ui::EF_NONE);ui::KeyEvent up(ui::EventType::kKeyReleased,ui::VKEY_SPACE,ui::EF_NONE);profile_create->RequestFocus();ASSERT_TRUE(profile_create->OnKeyPressed(space));profile_create->OnKeyReleased(up);
  EXPECT_EQ(2u,window_->profiles().size());ASSERT_TRUE(base::test::RunUntil([&]{return shell->profile_panel_contains_scene_text("Podaj nazwę profilu.");}));
  ASSERT_TRUE(window_->CreateProfile("Action profile"));ASSERT_TRUE(base::test::RunUntil([&]{return !window_->profiles_busy();}));const auto added=window_->profiles().back();shell->OpenProfiles(added.id);
  auto* remove=find(true,u"Usuń profil i jego dane");ASSERT_TRUE(check(remove));ASSERT_TRUE(remove->OnKeyPressed(space));remove->OnKeyReleased(up);ASSERT_TRUE(base::test::RunUntil([&]{return shell->form_actions_contains_scene_text(true,"Potwierdź usunięcie profilu i danych");}));
  auto* confirm=find(true,u"Potwierdź usunięcie profilu i danych");ASSERT_TRUE(confirm);ASSERT_TRUE(confirm->OnMousePressed(press));shell->OpenProfiles("personal-profile");confirm->OnMouseReleased(release);EXPECT_EQ(3u,window_->profiles().size());EXPECT_FALSE(find(true,u"Usuń profil i jego dane")->GetEnabled());
  shell->OpenProfiles(added.id);remove=find(true,u"Usuń profil i jego dane");ASSERT_TRUE(remove->OnKeyPressed(space));remove->OnKeyReleased(up);confirm=find(true,u"Potwierdź usunięcie profilu i danych");ASSERT_TRUE(confirm);ASSERT_TRUE(confirm->OnKeyPressed(space));confirm->OnKeyReleased(up);
  ASSERT_TRUE(base::test::RunUntil([&]{return !window_->profiles_busy();}));EXPECT_EQ(2u,window_->profiles().size());ASSERT_TRUE(base::test::RunUntil([&]{return window_->configuration_saved();}));
}
// Compiled only. Execution remains deferred to the user's GUI checkpoint.
IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, AppearanceRowsSceneInputClippingCompositionAndWorkspaceChange) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->SetWorkspaceAnimations(false);shell->OpenWorkspaceSettings();
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_metal_visible(false)&&shell->appearance_metal_visible(true);}));
  auto* colors=shell->color_input()->view()->parent();auto* emojis=shell->emoji_input()->view()->parent();ASSERT_EQ(6u,colors->children().size());ASSERT_EQ(8u,emojis->children().size());
  for(bool emoji:{false,true}){auto* field=emoji?shell->emoji_input():shell->color_input();auto* row=field->view()->parent();const auto frame=shell->appearance_scene_bounds_for_testing(emoji);EXPECT_FALSE(frame.IsEmpty());EXPECT_FALSE(frame.Intersects(field->view()->GetBoundsInScreen()));for(size_t i=0;i<(emoji?6u:4u);++i){EXPECT_TRUE(frame.Contains(row->children()[i]->GetBoundsInScreen()));EXPECT_FALSE(views::AsViewClass<views::Button>(row->children()[i].get()));EXPECT_TRUE(row->children()[i]->children().empty());for(const auto& child:row->children()[i]->children())EXPECT_FALSE(views::AsViewClass<views::Label>(child.get()));}}
  auto* mint=colors->children()[1].get();const gfx::Point point(10,10);ui::MouseEvent press(ui::EventType::kMousePressed,point,point,base::TimeTicks(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);ui::MouseEvent release(ui::EventType::kMouseReleased,point,point,base::TimeTicks(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);
  ASSERT_TRUE(mint->OnMousePressed(press));mint->OnMouseReleased(release);EXPECT_EQ(0xFF84B6A4u,shell->workspace_color("work"));
  auto* peach=colors->children()[2].get();ASSERT_TRUE(peach->OnMousePressed(press));peach->OnMouseCaptureLost();peach->OnMouseReleased(release);EXPECT_EQ(0xFF84B6A4u,shell->workspace_color("work"));
  auto* blue=colors->children()[3].get();blue->RequestFocus();ui::KeyEvent space(ui::EventType::kKeyPressed,ui::VKEY_SPACE,ui::EF_NONE);ui::KeyEvent space_up(ui::EventType::kKeyReleased,ui::VKEY_SPACE,ui::EF_NONE);ASSERT_TRUE(blue->OnKeyPressed(space));blue->OnKeyReleased(space_up);EXPECT_EQ(0xFF7FAACBu,shell->workspace_color("work"));EXPECT_EQ(u"#7FAACB",shell->color_input()->GetText());
  auto* rocket=emojis->children()[2].get();rocket->RequestFocus();ASSERT_TRUE(rocket->OnKeyPressed(space));rocket->OnKeyReleased(space_up);EXPECT_EQ(u"🚀",shell->workspace_emoji("work"));ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_contains_scene_text("🚀");}));
  const auto personal_color=shell->workspace_color("personal");ASSERT_TRUE(peach->OnMousePressed(press));ASSERT_TRUE(window_->Select("personal"));peach->OnMouseReleased(release);EXPECT_EQ(personal_color,shell->workspace_color("personal"));EXPECT_EQ(0xFF7FAACBu,shell->workspace_color("work"));
  shell->OpenWorkspaceSettings();auto* field=shell->emoji_input();field->RequestFocus();ASSERT_TRUE(field->HasFocus());field->SetText(u"");ui::CompositionText composition;composition.text=u"にほん";composition.selection=gfx::Range(composition.text.size());field->SetCompositionText(composition);ASSERT_TRUE(field->IsIMEComposing());const auto before=shell->workspace_emoji("personal");ui::KeyEvent enter(ui::EventType::kKeyPressed,ui::VKEY_RETURN,ui::EF_NONE);
  EXPECT_FALSE(field->view()->OnKeyPressed(enter));EXPECT_EQ(before,shell->workspace_emoji("personal"));field->ClearCompositionText();field->SetText(u"👩‍💻abc");ASSERT_TRUE(field->view()->OnKeyPressed(enter));EXPECT_EQ(u"👩‍💻",shell->workspace_emoji("personal"));
  shell->OpenWorkspaceSettings();shell->color_input()->SetText(u"#123456");ASSERT_TRUE(shell->color_input()->view()->OnKeyPressed(enter));EXPECT_EQ(0xFF123456u,shell->workspace_color("personal"));
  auto* create=FindButton(shell->workspace_panel_for_testing(),u"Nowa przestrzeń…");ASSERT_TRUE(create);ASSERT_TRUE(peach->OnMousePressed(press));ClickControl(create);ASSERT_TRUE(base::test::RunUntil([&]{return !shell->appearance_metal_visible(false)&&!shell->appearance_metal_visible(true);}));peach->OnMouseReleased(release);EXPECT_EQ(0xFF123456u,shell->workspace_color("personal"));
  auto* cancel=FindButton(shell->workspace_panel_for_testing(),u"Anuluj");ASSERT_TRUE(cancel);ClickControl(cancel);ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_metal_visible(false)&&shell->appearance_metal_visible(true);}));
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_saved();}));
}
// Compiled with the shared notice renderer; execution awaits the GUI checkpoint.
IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, WorkspaceNoticeCancelCapturedTargetUnloadAndRetainedProfile) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->SetWorkspaceAnimations(false);
  ASSERT_TRUE(window_->Select("personal"));ASSERT_TRUE(window_->EditWorkspace("Prywatne 日本語 🦀","personal-profile"));
  const auto url=embedded_test_server()->GetURL("/storage_probe.html");auto* personal=window_->contents("personal");ASSERT_TRUE(content::NavigateToURL(personal,url));EXPECT_EQ("kept",content::EvalJs(personal,"writeMarker('kept')"));ASSERT_TRUE(window_->Select("work"));
  auto open=[&](const std::string& id){shell->BuildWorkspaceMenu(id);shell->workspace_menu()->ActivatedAt(5,0);};
  open("personal");ASSERT_TRUE(base::test::RunUntil([&]{return shell->workspace_notice_metal_visible();}));
  auto* notice=shell->workspace_notice_for_testing();ASSERT_TRUE(notice);EXPECT_GE(shell->workspace_notice_line_count_for_testing(),4u);
  EXPECT_EQ(u"Usunąć przestrzeń „Prywatne 日本語 🦀”?",shell->remove_workspace_dialog()->widget_delegate()->GetWindowTitle());
  EXPECT_EQ(static_cast<int>(ui::mojom::DialogButton::kCancel),shell->remove_workspace_dialog()->widget_delegate()->AsDialogDelegate()->GetDefaultDialogButton());
  for(const auto& child:notice->children())EXPECT_FALSE(views::AsViewClass<views::Label>(child.get()));ui::AXNodeData data;notice->children()[0]->GetViewAccessibility().GetAccessibleNodeData(&data);EXPECT_EQ(ax::mojom::Role::kStaticText,data.role);
  EXPECT_NE(std::u16string::npos,data.GetString16Attribute(ax::mojom::StringAttribute::kName).find(u"Profil przeglądania, hasła, historia i cookies pozostaną zachowane."));
  shell->remove_workspace_dialog()->widget_delegate()->AsDialogDelegate()->CancelDialog();ASSERT_TRUE(base::test::RunUntil([&]{return !shell->remove_workspace_dialog();}));EXPECT_EQ(2u,window_->workspaces().size());
  ASSERT_TRUE(window_->Select("personal"));ArmUnsaved(personal);ASSERT_TRUE(window_->Select("work"));
  open("personal");ASSERT_TRUE(base::test::RunUntil([&]{return shell->workspace_notice_metal_visible();}));shell->remove_workspace_dialog()->widget_delegate()->AsDialogDelegate()->AcceptDialog();
  AnswerUnload(personal,false);ASSERT_TRUE(base::test::RunUntil([&]{return !window_->lifecycle_pending()&&!shell->remove_workspace_dialog();}));EXPECT_EQ(personal,window_->contents("personal"));EXPECT_EQ(2u,window_->workspaces().size());
  open("personal");ASSERT_TRUE(base::test::RunUntil([&]{return shell->workspace_notice_metal_visible();}));ASSERT_TRUE(window_->Select("personal"));ASSERT_TRUE(window_->Select("work"));
  shell->remove_workspace_dialog()->widget_delegate()->AsDialogDelegate()->AcceptDialog();AnswerUnload(personal,true);
  ASSERT_TRUE(base::test::RunUntil([&]{return !shell->remove_workspace_dialog()&&!window_->lifecycle_pending()&&!window_->contents("personal");}));EXPECT_EQ("work",window_->active_id());ASSERT_EQ(2u,window_->profiles().size());
  ASSERT_TRUE(window_->AddWorkspace("Retained profile","personal-profile"));const auto retained=window_->active_id();EXPECT_EQ(personal_.get(),window_->contents(retained)->GetBrowserContext());ASSERT_TRUE(content::NavigateToURL(window_->contents(retained),url));EXPECT_EQ("kept|kept|kept|",content::EvalJs(window_->contents(retained),"readMarker()"));
  ASSERT_TRUE(window_->Select("work"));open(retained);ASSERT_TRUE(base::test::RunUntil([&]{return shell->workspace_notice_metal_visible();}));ASSERT_TRUE(window_->RemoveWorkspace(retained));
  shell->remove_workspace_dialog()->widget_delegate()->AsDialogDelegate()->AcceptDialog();ASSERT_TRUE(shell->remove_workspace_dialog());EXPECT_EQ(u"Przestrzeń nie jest już dostępna.",shell->workspace_notice_error_for_testing());ASSERT_TRUE(base::test::RunUntil([&]{return shell->workspace_notice_contains_scene_text("Przestrzeń nie jest już dostępna.");}));EXPECT_EQ(1u,window_->workspaces().size());EXPECT_NE(nullptr,window_->contents("work"));
  shell->remove_workspace_dialog()->widget_delegate()->AsDialogDelegate()->CancelDialog();ASSERT_TRUE(base::test::RunUntil([&]{return !shell->remove_workspace_dialog();}));shell->BuildWorkspaceMenu("work");EXPECT_FALSE(shell->workspace_menu()->IsEnabledAt(5));
  ASSERT_TRUE(base::test::RunUntil([&]{return window_->configuration_saved();}));
}
// Compilation only; do not execute before the requested GUI checkpoint.
IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, WorkspaceFormScenesCreateCancelIMEAndProfileAssignment) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->SetWorkspaceAnimations(false);shell->OpenWorkspaceSettings();
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->workspace_panel_contains_scene_text("Nazwa przestrzeni");}));
  auto* panel=shell->workspace_panel_for_testing();ASSERT_TRUE(panel);
  auto no_labels=[&](auto&& self,views::View* view)->void {
    if(views::AsViewClass<views::Textfield>(view)||views::AsViewClass<views::Combobox>(view)||views::AsViewClass<views::Button>(view))return;
    EXPECT_FALSE(views::AsViewClass<views::Label>(view));for(const auto& child:view->children())self(self,child.get());
  };no_labels(no_labels,panel);
  auto button=[&](std::u16string_view title){return FindButton(panel,title);};
  auto* start=button(u"Nowa przestrzeń…");ASSERT_TRUE(start);ClickControl(start);
  ASSERT_TRUE(button(u"Utwórz przestrzeń"));ASSERT_TRUE(base::test::RunUntil([&]{return shell->workspace_panel_contains_scene_text("Nowa przestrzeń");}));
  ASSERT_TRUE(button(u"Anuluj"));ClickControl(button(u"Anuluj"));EXPECT_EQ(2u,window_->workspaces().size());EXPECT_EQ(u"Praca",shell->workspace_input()->GetText());
  ClickControl(button(u"Nowa przestrzeń…"));auto* create=button(u"Utwórz przestrzeń");ASSERT_TRUE(create);auto* field=shell->workspace_input();field->SetText(u" \t ");
  ClickControl(create);EXPECT_EQ(2u,window_->workspaces().size());ASSERT_TRUE(base::test::RunUntil([&]{return shell->workspace_panel_contains_scene_text("Podaj nazwę przestrzeni.");}));
  ui::CompositionText composition;composition.text=u"にほん";composition.selection=gfx::Range(composition.text.size());field->SetCompositionText(composition);ASSERT_TRUE(field->IsIMEComposing());
  ClickControl(create);EXPECT_EQ(2u,window_->workspaces().size());field->ClearCompositionText();
  field->SetText(u"  Projekt Łódź 🦀  ");shell->SetProfilePickerSelectionForTesting(false,0);ClickControl(create);
  ASSERT_EQ(3u,window_->workspaces().size());const auto project=window_->active_id();EXPECT_EQ("Projekt Łódź 🦀",window_->workspaces().back().name);EXPECT_EQ(work_.get(),window_->contents(project)->GetBrowserContext());
  // Selecting the new workspace closes settings, just as in the Views backend.
  shell->OpenWorkspaceSettings();
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->workspace_panel_contains_scene_text("Projekt Łódź 🦀");}));ASSERT_TRUE(button(u"Zapisz nazwę i profil"));
  shell->OpenWorkspaceSettings();field->SetText(u"  Klient 日本語  ");shell->SetProfilePickerSelectionForTesting(false,1);ClickControl(button(u"Zapisz nazwę i profil"));
  EXPECT_EQ("Klient 日本語",window_->workspaces().back().name);EXPECT_EQ(personal_.get(),window_->contents(project)->GetBrowserContext());
  shell->OpenWorkspaceSettings();shell->color_input()->SetText(u"invalid");ui::KeyEvent enter(ui::EventType::kKeyPressed,ui::VKEY_RETURN,ui::EF_NONE);
  ASSERT_TRUE(shell->color_input()->view()->OnKeyPressed(enter));ASSERT_TRUE(base::test::RunUntil([&]{return shell->workspace_panel_contains_scene_text("Podaj kolor #RRGGBB");}));
  shell->color_input()->SetText(u"#123456");ASSERT_TRUE(shell->color_input()->view()->OnKeyPressed(enter));EXPECT_EQ(0xFF123456u,shell->workspace_color(project));
  ClickControl(button(u"Nowa przestrzeń…"));ASSERT_TRUE(button(u"Utwórz przestrzeń"));ASSERT_TRUE(window_->Select("work"));shell->OpenWorkspaceSettings();
  ASSERT_TRUE(base::test::RunUntil([&]{return button(u"Zapisz nazwę i profil")!=nullptr;}));EXPECT_EQ(u"Praca",field->GetText());
  shell->SetConfigurationError(true);ASSERT_TRUE(base::test::RunUntil([&]{return shell->workspace_panel_contains_scene_text("Nie udało się odczytać lub zapisać przestrzeni.");}));shell->SetConfigurationError(false);
  ASSERT_TRUE(base::test::RunUntil([&]{return window_->configuration_saved()&&shell->appearance_saved();}));
}
// Compiled only until the user requests the GUI checkpoint.
IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, ProfilePanelScenesValidationIMEAndStableDeletion) {
  auto* shell=window_->shell();
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));
  shell->OpenProfiles("work-profile");
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->profile_panel_contains_scene_text("Profile przeglądania");}));
  auto* panel=shell->profile_panel_for_testing();ASSERT_TRUE(panel);
  // Editor, picker and buttons retain their own native text; panel copy does not.
  for(const auto& child:panel->children())EXPECT_FALSE(views::AsViewClass<views::Label>(child.get()));
  ui::AXNodeData heading;panel->children()[0]->GetViewAccessibility().GetAccessibleNodeData(&heading);
  EXPECT_EQ(ax::mojom::Role::kStaticText,heading.role);
  EXPECT_EQ(u"Profile przeglądania",heading.GetString16Attribute(ax::mojom::StringAttribute::kName));
  auto button=[&](std::u16string_view name){return FindButton(panel,name);};
  auto* create=button(u"Utwórz profil");ASSERT_TRUE(create);
  auto* remove=button(u"Usuń profil i jego dane");ASSERT_TRUE(remove);EXPECT_FALSE(remove->GetEnabled());
  auto* field=shell->profile_name_input();field->SetText(u" \u3000\u00a0 ");
  ClickControl(create);EXPECT_EQ(2u,window_->profiles().size());
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->profile_panel_contains_scene_text("Podaj nazwę profilu.");}));
  ui::CompositionText composition;composition.text=u"にほん";composition.selection=gfx::Range(composition.text.size());field->SetCompositionText(composition);
  ASSERT_TRUE(field->IsIMEComposing());ClickControl(create);EXPECT_EQ(2u,window_->profiles().size());field->ClearCompositionText();
  field->SetText(u"  Klient Łódź 🦀  ");ClickControl(create);
  ASSERT_TRUE(base::test::RunUntil([&]{return !window_->profiles_busy();}));ASSERT_EQ(3u,window_->profiles().size());
  const auto added=window_->profiles().back();EXPECT_EQ("Klient Łódź 🦀",added.name);
  shell->OpenProfiles(added.id);field->SetText(u"Klient Łódź 🦀");ClickControl(create);
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->profile_panel_contains_scene_text("Profil o tej nazwie już istnieje.");}));EXPECT_EQ(3u,window_->profiles().size());
  auto* rename=button(u"Zapisz nazwę profilu");ASSERT_TRUE(rename);field->SetText(u"  Zmieniony 日本語  ");ClickControl(rename);
  ASSERT_TRUE(base::test::RunUntil([&]{return !window_->profiles_busy();}));EXPECT_EQ(added.id,window_->profiles().back().id);EXPECT_EQ("Zmieniony 日本語",window_->profiles().back().name);
  shell->OpenProfiles(added.id);remove=button(u"Usuń profil i jego dane");ASSERT_TRUE(remove);ASSERT_TRUE(remove->GetEnabled());ClickControl(remove);
  ASSERT_TRUE(button(u"Potwierdź usunięcie profilu i danych"));EXPECT_EQ(3u,window_->profiles().size());
  // Opening another target must not transfer the destructive confirmation.
  shell->OpenProfiles("work-profile");remove=button(u"Usuń profil i jego dane");ASSERT_TRUE(remove);EXPECT_FALSE(remove->GetEnabled());
  shell->OpenProfiles(added.id);remove=button(u"Usuń profil i jego dane");ASSERT_TRUE(remove);ClickControl(remove);
  // An external rename updates the catalogue and disarms the pending deletion.
  ASSERT_TRUE(window_->RenameProfile(added.id,"Po zmianie katalogu"));ASSERT_TRUE(base::test::RunUntil([&]{return !window_->profiles_busy();}));
  ASSERT_TRUE(button(u"Usuń profil i jego dane"));ASSERT_TRUE(shell->profile_picker_selection(true));
  EXPECT_EQ(added.id,window_->profiles()[*shell->profile_picker_selection(true)].id);
  ClickControl(button(u"Usuń profil i jego dane"));EXPECT_EQ(3u,window_->profiles().size());
  auto* confirm=button(u"Potwierdź usunięcie profilu i danych");ASSERT_TRUE(confirm);ClickControl(confirm);
  ASSERT_TRUE(base::test::RunUntil([&]{return !window_->profiles_busy();}));EXPECT_EQ(2u,window_->profiles().size());
  for(const auto& profile:window_->profiles())EXPECT_NE(added.id,profile.id);
}
// Compiled with the migration; execution is deferred to the requested GUI checkpoint.
IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, FolderNoticesWrapPreserveCancelAndRejectStaleTargets) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->SetWorkspaceAnimations(false);
  ASSERT_TRUE(window_->CreatePinnedFolder("work",{},"Notice 🦀"));ASSERT_TRUE(base::test::RunUntil([&]{return !window_->profiles_busy();}));
  const auto id=window_->pinned_items("work")[0].id;shell->RequestDeleteFolder("work",id);
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->folder_notice_metal_visible();}));
  auto* notice=shell->folder_notice_for_testing();ASSERT_TRUE(notice);EXPECT_GE(shell->folder_notice_line_count_for_testing(),2u);
  EXPECT_EQ(static_cast<int>(ui::mojom::DialogButton::kCancel),shell->folder_dialog()->widget_delegate()->AsDialogDelegate()->GetDefaultDialogButton());
  for(const auto& child:notice->children())EXPECT_FALSE(views::AsViewClass<views::Label>(child.get()));
  ui::AXNodeData data;notice->children()[0]->GetViewAccessibility().GetAccessibleNodeData(&data);EXPECT_EQ(ax::mojom::Role::kStaticText,data.role);
  EXPECT_NE(std::u16string::npos,data.GetString16Attribute(ax::mojom::StringAttribute::kName).find(u"Strony już otwarte w kartach pozostaną otwarte."));
  shell->folder_dialog()->widget_delegate()->AsDialogDelegate()->CancelDialog();ASSERT_TRUE(base::test::RunUntil([&]{return !shell->folder_dialog();}));EXPECT_EQ(1u,window_->pinned_items("work").size());
  shell->RequestDeleteFolder("work",id);ASSERT_TRUE(base::test::RunUntil([&]{return shell->folder_notice_metal_visible();}));
  ASSERT_TRUE(window_->Select("personal"));ASSERT_TRUE(base::test::RunUntil([&]{return window_->active_id()=="personal"&&!window_->profiles_busy();}));
  shell->folder_dialog()->widget_delegate()->AsDialogDelegate()->AcceptDialog();EXPECT_FALSE(shell->folder_notice_error_for_testing().empty());EXPECT_EQ(1u,window_->pinned_items("work").size());
  shell->folder_dialog()->widget_delegate()->AsDialogDelegate()->CancelDialog();ASSERT_TRUE(base::test::RunUntil([&]{return !shell->folder_dialog();}));
  ASSERT_TRUE(window_->Select("work"));shell->RequestDeleteFolder("work",id);ASSERT_TRUE(base::test::RunUntil([&]{return shell->folder_notice_metal_visible();}));
  shell->folder_dialog()->widget_delegate()->AsDialogDelegate()->AcceptDialog();ASSERT_TRUE(base::test::RunUntil([&]{return !shell->folder_dialog()&&!window_->profiles_busy();}));EXPECT_TRUE(window_->pinned_items("work").empty());
  ASSERT_TRUE(window_->CreatePinnedFolder("work",{},"Removed"));ASSERT_TRUE(base::test::RunUntil([&]{return !window_->profiles_busy();}));const auto removed=window_->pinned_items("work")[0].id;
  shell->RequestDeleteFolder("work",removed);ASSERT_TRUE(base::test::RunUntil([&]{return shell->folder_notice_metal_visible();}));ASSERT_TRUE(window_->RemovePinnedFolder("work",removed,true));
  ASSERT_TRUE(base::test::RunUntil([&]{return !window_->profiles_busy();}));shell->folder_dialog()->widget_delegate()->AsDialogDelegate()->AcceptDialog();EXPECT_EQ(u"Folder nie jest już dostępny.",shell->folder_notice_error_for_testing());
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->folder_notice_contains_scene_text("Folder nie jest już dostępny.");}));shell->folder_dialog()->widget_delegate()->AsDialogDelegate()->CancelDialog();ASSERT_TRUE(base::test::RunUntil([&]{return !shell->folder_dialog();}));
  shell->ShowFolderSaveError();ASSERT_TRUE(base::test::RunUntil([&]{return shell->folder_notice_metal_visible();}));
  EXPECT_EQ(u"Nie udało się zapisać folderu",shell->folder_dialog()->widget_delegate()->GetWindowTitle());
  auto* dialog=shell->folder_dialog()->widget_delegate()->AsDialogDelegate();EXPECT_EQ(0,dialog->buttons()&static_cast<int>(ui::mojom::DialogButton::kCancel));
  dialog->AcceptDialog();ASSERT_TRUE(base::test::RunUntil([&]{return !shell->folder_dialog();}));EXPECT_TRUE(window_->pinned_items("work").empty());
}
IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, SceneModalActionsForcedCloseAndFreshEditorLifetime) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->SetWorkspaceAnimations(false);
  for(int attempt=0;attempt<3;++attempt){shell->RequestNewFolder("work");ASSERT_TRUE(base::test::RunUntil([&]{return shell->folder_form_metal_visible();}));auto* field=shell->folder_editor_for_testing();ASSERT_TRUE(field);auto lifetime=field->input_client()->AsWeakPtr();ui::CompositionText marked;marked.text=u"未";marked.selection=gfx::Range(1);field->SetCompositionText(marked);EXPECT_TRUE(field->IsIMEComposing());auto* widget=shell->folder_dialog();auto* ok=DialogActionButton(widget,true);ASSERT_TRUE(ok);ASSERT_TRUE(base::test::RunUntil([&]{return PageDialogActionsVisibleForTesting(ok->parent(),"Dodaj folder");}));widget->CloseNow();ASSERT_TRUE(base::test::RunUntil([&]{return !shell->folder_dialog()&&!lifetime;}));EXPECT_TRUE(window_->pinned_items("work").empty());}
  shell->ShowFolderSaveError();ASSERT_TRUE(base::test::RunUntil([&]{return shell->folder_notice_metal_visible();}));auto* widget=shell->folder_dialog();EXPECT_FALSE(DialogActionButton(widget,false));auto* ok=DialogActionButton(widget,true);ASSERT_TRUE(ok);ASSERT_TRUE(base::test::RunUntil([&]{return PageDialogActionsVisibleForTesting(ok->parent(),"OK");}));ui_controls::EnableUIControls();ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(widget->GetNativeWindow()));ok->RequestFocus();base::test::TestFuture<void> presented;auto* compositor=widget->GetCompositor();ASSERT_TRUE(compositor);widget->GetRootView()->SchedulePaint();compositor->RequestSuccessfulPresentationTimeForNextFrame(base::BindOnce([](base::OnceClosure done,const viz::FrameTimingDetails&){std::move(done).Run();},presented.GetCallback()));compositor->ScheduleFullRedraw();ASSERT_TRUE(presented.Wait());widget->widget_delegate()->AsDialogDelegate()->ResetViewShownTimeStampForTesting();ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(widget->GetNativeWindow(),ui::VKEY_RETURN,false,false,false,false));ASSERT_TRUE(base::test::RunUntil([&]{return !shell->folder_dialog();}));
  shell->RequestNewFolder("work");ASSERT_TRUE(base::test::RunUntil([&]{return shell->folder_form_metal_visible();}));EXPECT_EQ(u"Nowy folder",shell->folder_editor_for_testing()->GetText());auto lifetime=shell->folder_editor_for_testing()->input_client()->AsWeakPtr();shell->Detach();EXPECT_FALSE(shell->folder_dialog());EXPECT_FALSE(lifetime);
}

IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, FolderFormScenesValidationCompositionAndStaleContext) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->SetWorkspaceAnimations(false);
  shell->RequestNewFolder("work");ASSERT_TRUE(base::test::RunUntil([&]{return shell->folder_form_metal_visible();}));
  auto* form=shell->folder_form_for_testing();ASSERT_TRUE(form);auto* field=shell->folder_editor_for_testing();ASSERT_TRUE(field);EXPECT_TRUE(field->HasFocus());
  auto no_labels=[&](auto&& self,views::View* view)->void{if(view==field->view())return;EXPECT_FALSE(views::AsViewClass<views::Label>(view));for(const auto& child:view->children())self(self,child.get());};no_labels(no_labels,form);
  EXPECT_TRUE(shell->folder_form_contains_scene_text("Nazwa folderu"));
  field->SetText(u" \u3000\u00a0 ");shell->folder_dialog()->widget_delegate()->AsDialogDelegate()->AcceptDialog();
  ASSERT_TRUE(shell->folder_dialog());EXPECT_EQ(u"Wpisz nazwę folderu.",shell->folder_form_error_for_testing());EXPECT_TRUE(window_->pinned_items("work").empty());
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->folder_form_contains_scene_text("Wpisz nazwę folderu.");}));
  for(const auto& child:form->children())if(child.get()!=field->view()&&child->GetVisible())EXPECT_FALSE(child->GetBoundsInScreen().Intersects(field->view()->GetBoundsInScreen()));
  field->SetText(std::u16string(129,u'ą'));shell->folder_dialog()->widget_delegate()->AsDialogDelegate()->AcceptDialog();EXPECT_EQ(u"Nazwa folderu jest za długa.",shell->folder_form_error_for_testing());
  ASSERT_TRUE(ReplaceFolderName(shell,u"  Projekty 🦀  "));EXPECT_TRUE(shell->folder_form_error_for_testing().empty());
  ui::CompositionText composition;composition.text=u"にほん";composition.selection=gfx::Range(composition.text.size());field->SetCompositionText(composition);ASSERT_TRUE(field->IsIMEComposing());
  shell->folder_dialog()->widget_delegate()->AsDialogDelegate()->AcceptDialog();EXPECT_TRUE(shell->folder_dialog());EXPECT_TRUE(window_->pinned_items("work").empty());field->ClearCompositionText();
  field->SetText(u"  Projekty 🦀  ");shell->folder_dialog()->widget_delegate()->AsDialogDelegate()->AcceptDialog();
  ASSERT_TRUE(base::test::RunUntil([&]{return !shell->folder_dialog()&&!window_->profiles_busy();}));ASSERT_EQ(1u,window_->pinned_items("work").size());EXPECT_EQ("Projekty 🦀",window_->pinned_items("work")[0].title);
  const auto id=window_->pinned_items("work")[0].id;shell->RequestRenameFolder("work",id);
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->folder_form_metal_visible();}));EXPECT_EQ(u"Zmień nazwę folderu",shell->folder_dialog()->widget_delegate()->GetWindowTitle());
  field=shell->folder_editor_for_testing();ASSERT_TRUE(field);field->SetText(u"  Zmieniony 日本語  ");shell->folder_dialog()->widget_delegate()->AsDialogDelegate()->AcceptDialog();
  ASSERT_TRUE(base::test::RunUntil([&]{return !shell->folder_dialog()&&!window_->profiles_busy();}));EXPECT_EQ("Zmieniony 日本語",window_->pinned_items("work")[0].title);
  shell->RequestNewFolder("work",id);ASSERT_TRUE(base::test::RunUntil([&]{return shell->folder_form_metal_visible();}));
  EXPECT_EQ(u"Nowy podfolder",shell->folder_dialog()->widget_delegate()->GetWindowTitle());ASSERT_TRUE(window_->RemovePinnedFolder("work",id,true));
  ASSERT_TRUE(base::test::RunUntil([&]{return !window_->profiles_busy();}));shell->folder_dialog()->widget_delegate()->AsDialogDelegate()->AcceptDialog();EXPECT_EQ(u"Folder nie jest już dostępny.",shell->folder_form_error_for_testing());
  ASSERT_TRUE(window_->Select("personal"));ASSERT_TRUE(base::test::RunUntil([&]{return window_->active_id()=="personal"&&!window_->profiles_busy();}));
  shell->folder_dialog()->widget_delegate()->AsDialogDelegate()->AcceptDialog();EXPECT_EQ(u"Przestrzeń zmieniła się. Otwórz formularz ponownie.",shell->folder_form_error_for_testing());
  shell->folder_dialog()->widget_delegate()->AsDialogDelegate()->CancelDialog();ASSERT_TRUE(base::test::RunUntil([&]{return !shell->folder_dialog();}));EXPECT_TRUE(window_->pinned_items("personal").empty());
}
IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, RowMenuPlansAndQueuedIdentitySurviveReindex) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->SetWorkspaceAnimations(false);
  const auto url=embedded_test_server()->GetURL("/storage_probe.html?row-menu");
  for(int i=0;i<3;++i)ASSERT_TRUE(window_->NavigateFromBar(url,true));
  ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));
  auto target=window_->contents("work")->GetWeakPtr();
  auto* delegate=static_cast<ui::SimpleMenuModel::Delegate*>(shell);
  shell->BuildTabMenu(window_->tab_count("work")-1);
  auto* menu=shell->tab_context_menu_for_testing();ASSERT_TRUE(menu->GetIndexOfCommandId(5));EXPECT_TRUE(menu->IsEnabledAt(*menu->GetIndexOfCommandId(5)));
  EXPECT_TRUE(menu->GetIndexOfCommandId(20));EXPECT_TRUE(menu->GetIndexOfCommandId(23));
  // Close the selected identity after another tab shifts its original index.
  delegate->ExecuteCommand(10,0);window_->CloseTab(0);
  ASSERT_TRUE(base::test::RunUntil([&]{return !target;}));EXPECT_EQ(2u,window_->tab_count("work"));
  ASSERT_TRUE(window_->NavigateFromBar(url,true));shell->BuildTabMenu(window_->tab_count("work")-1);
  delegate->ExecuteCommand(10,0);window_->CloseTab(window_->tab_count("work")-1);
  ASSERT_TRUE(window_->NavigateFromBar(url,true));auto replacement=window_->contents("work")->GetWeakPtr();const auto count=window_->tab_count("work");
  base::RunLoop().RunUntilIdle();EXPECT_TRUE(replacement);EXPECT_EQ(count,window_->tab_count("work")); // Reused index/URL is not the removed identity.
  shell->BuildTabMenu(window_->tab_count("work")-1);delegate->ExecuteCommand(5,0);ASSERT_TRUE(window_->Select("personal"));
  ASSERT_TRUE(base::test::RunUntil([&]{return window_->active_id()=="personal"&&!window_->profiles_busy();}));base::RunLoop().RunUntilIdle();EXPECT_FALSE(window_->split());
  ASSERT_TRUE(window_->NavigateFromBar(url,true));window_->SelectTab(0);ASSERT_TRUE(window_->SplitTab(1));
  shell->BuildTabMenu(1);menu=shell->tab_context_menu_for_testing();
  ASSERT_TRUE(menu->GetIndexOfCommandId(7));ASSERT_TRUE(menu->GetIndexOfCommandId(8));
  EXPECT_TRUE(menu->IsEnabledAt(*menu->GetIndexOfCommandId(7)));EXPECT_FALSE(menu->IsEnabledAt(*menu->GetIndexOfCommandId(8)));
  delegate->ExecuteCommand(9,0);ASSERT_TRUE(base::test::RunUntil([&]{return window_->split_vertical();}));
  shell->BuildTabMenu(1);menu=shell->tab_context_menu_for_testing();EXPECT_EQ(u"Przesuń w górę",menu->GetLabelAt(*menu->GetIndexOfCommandId(7)));
  delegate->ExecuteCommand(6,0);ASSERT_TRUE(base::test::RunUntil([&]{return !window_->split();}));
}
IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, WorkspaceSwitchUsesPresentedMetalSceneAndCancels) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));
  shell->SetWorkspaceAnimations(false);shell->SetPinned(true);shell->ShowSidebar();
  const auto url=embedded_test_server()->GetURL("/storage_probe.html?gpu-switch");
  ASSERT_TRUE(window_->NavigateFromBar(url,true));ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));ASSERT_TRUE(content::ExecJs(window_->contents("work"),"document.title='Workspace alpha 🦀'"));
  ASSERT_TRUE(window_->Select("personal"));ASSERT_TRUE(base::test::RunUntil([&]{return window_->active_id()=="personal"&&!window_->profiles_busy();}));
  ASSERT_TRUE(window_->NavigateFromBar(url,true));ASSERT_TRUE(content::WaitForLoadStop(window_->contents("personal")));ASSERT_TRUE(content::ExecJs(window_->contents("personal"),"document.title='Workspace beta 日本語'"));
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->metal_sidebar_visible()&&shell->metal_sidebar_contains_text_for_testing("Workspace beta 日本語");}));
  ASSERT_TRUE(window_->Select("work"));ASSERT_TRUE(base::test::RunUntil([&]{return shell->metal_sidebar_visible()&&shell->metal_sidebar_contains_text_for_testing("Workspace alpha 🦀");}));
  shell->SetWorkspaceAnimations(true);ASSERT_TRUE(window_->Select("personal"));
  EXPECT_TRUE(shell->workspace_switch_metal_visible());EXPECT_TRUE(shell->workspace_switch_contains_scene_text("Workspace beta 日本語"));EXPECT_TRUE(shell->workspace_animation_running());
  ASSERT_TRUE(base::test::RunUntil([&]{return !shell->workspace_animation_running()&&shell->metal_sidebar_visible();}));
  EXPECT_FALSE(shell->workspace_switch_metal_visible());EXPECT_TRUE(shell->metal_sidebar_contains_text_for_testing("Workspace beta 日本語"));
  ASSERT_TRUE(window_->Select("work"));EXPECT_TRUE(shell->workspace_switch_metal_visible());
  shell->SetWorkspaceAnimations(false);EXPECT_FALSE(shell->workspace_switch_metal_visible());EXPECT_FALSE(shell->workspace_animation_running());
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->metal_sidebar_visible();}));
  // Replace a pending presentation/animation before its callback can run.
  shell->SetWorkspaceAnimations(true);ASSERT_TRUE(window_->Select("personal"));ASSERT_TRUE(window_->Select("work"));
  ASSERT_TRUE(base::test::RunUntil([&]{return !shell->workspace_animation_running()&&shell->metal_sidebar_visible();}));
  EXPECT_EQ("work",window_->active_id());EXPECT_TRUE(shell->metal_sidebar_contains_text_for_testing("Workspace alpha 🦀"));
  ASSERT_TRUE(window_->Select("personal"));EXPECT_TRUE(shell->workspace_switch_metal_visible());
  shell->sidebar_widget()->Hide();EXPECT_FALSE(shell->workspace_switch_metal_visible());EXPECT_FALSE(shell->workspace_animation_running());
  shell->ShowSidebar();ASSERT_TRUE(base::test::RunUntil([&]{return shell->metal_sidebar_visible();}));
  EXPECT_EQ("personal",window_->active_id());EXPECT_TRUE(shell->metal_sidebar_contains_text_for_testing("Workspace beta 日本語"));
  ASSERT_TRUE(window_->Select("work"));EXPECT_TRUE(shell->workspace_switch_metal_visible());
  const auto original=window_->widget()->GetWindowBoundsInScreen();auto resized=original;resized.set_width(original.width()+20);window_->widget()->SetBounds(resized);
  ASSERT_TRUE(base::test::RunUntil([&]{return !shell->workspace_animation_running()&&shell->metal_sidebar_visible();}));
  EXPECT_FALSE(shell->workspace_switch_metal_visible());EXPECT_TRUE(shell->metal_sidebar_contains_text_for_testing("Workspace alpha 🦀"));window_->widget()->SetBounds(original);
}
IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, SwipePreviewUsesMetalWithoutNativeSnapshots) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));
  shell->SetWorkspaceAnimations(false);
  ASSERT_TRUE(window_->NavigateFromBar(embedded_test_server()->GetURL("/storage_probe.html?gpu-swipe"),true));
  ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));ASSERT_TRUE(content::ExecJs(window_->contents("work"),"document.title='GPU preview 🦀'"));
  shell->SetPinned(true);shell->ShowSidebar();
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->metal_sidebar_visible()&&shell->metal_sidebar_contains_text_for_testing("GPU preview 🦀");}));
  const auto width=shell->tab_scroll_view()->width();ASSERT_GT(width,100);
  auto stamp=base::TimeTicks::Now();
  const auto point=shell->tab_scroll_view()->GetBoundsInScreen().CenterPoint()-shell->sidebar_widget()->GetWindowBoundsInScreen().OffsetFromOrigin();
  auto scroll=[&](float x,ui::ScrollEventPhase phase){stamp+=base::Milliseconds(10);ui::ScrollEvent event(ui::EventType::kScroll,point,stamp,0,x,0,x,0,2,ui::EventMomentumPhase::NONE,phase);shell->sidebar_widget()->OnScrollEvent(&event);};
  scroll(0,ui::ScrollEventPhase::kBegan);scroll(-width*0.4f,ui::ScrollEventPhase::kUpdate);
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->workspace_swipe_metal_visible();}));
  EXPECT_TRUE(shell->workspace_swipe_contains_scene_text("GPU preview 🦀"));
  EXPECT_EQ(0u,shell->workspace_swipe_native_capture_count());EXPECT_LE(shell->workspace_swipe_preview_count(),3u);
  EXPECT_FALSE(window_->contents("personal"));EXPECT_EQ("work",window_->active_id());
  const auto submitted=shell->workspace_swipe_metal_frame_count();
  scroll(-width*0.05f,ui::ScrollEventPhase::kUpdate);base::RunLoop().RunUntilIdle();
  EXPECT_EQ(submitted,shell->workspace_swipe_metal_frame_count()); // Translation reuses drawable pixels.
  EXPECT_EQ(0u,shell->workspace_swipe_native_capture_count());
  scroll(0,ui::ScrollEventPhase::kEnd);
  ASSERT_TRUE(base::test::RunUntil([&]{return !shell->workspace_swipe_visible()&&shell->metal_sidebar_visible();}));EXPECT_EQ("work",window_->active_id());
  scroll(0,ui::ScrollEventPhase::kBegan);scroll(-width*0.7f,ui::ScrollEventPhase::kUpdate);
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->workspace_swipe_metal_visible();}));
  EXPECT_FALSE(window_->contents("personal"));scroll(0,ui::ScrollEventPhase::kEnd);
  ASSERT_TRUE(base::test::RunUntil([&]{return window_->active_id()=="personal"&&!shell->workspace_swipe_visible()&&shell->metal_sidebar_visible();}));
  scroll(0,ui::ScrollEventPhase::kBegan);scroll(width*0.7f,ui::ScrollEventPhase::kUpdate);
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->workspace_swipe_metal_visible();}));
  shell->CloseOverlays();EXPECT_FALSE(shell->workspace_swipe_visible());EXPECT_FALSE(shell->workspace_swipe_metal_visible());EXPECT_EQ("personal",window_->active_id());
}
IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, NewTabActionUsesSceneTextAndConfiguredShortcut) {
  auto* shell = window_->shell();
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->appearance_ready() && shell->tools_ready(); }));
  shell->SetWorkspaceAnimations(false);
  const auto url = embedded_test_server()->GetURL("/storage_probe.html");
  for (int i = 0; i < 28; ++i) ASSERT_TRUE(window_->NavigateFromBar(url, true));
  shell->CloseOverlays(true); shell->SetPinned(true); shell->ShowSidebar();
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->metal_sidebar_visible(); }));
  auto* action = SidebarNamedButton(shell->sidebar_view(), u"Nowa karta");
  ASSERT_TRUE(action);
  auto no_labels = [&](auto&& self, views::View* view) -> void {
    EXPECT_FALSE(views::AsViewClass<views::Label>(view));
    for (const auto& child : view->children()) self(self, child.get());
  };
  no_labels(no_labels, action);
  ASSERT_TRUE(shell->SetShortcut("new_tab", "Cmd+Shift+U"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return shell->metal_sidebar_contains_text_for_testing("Nowa karta") &&
        shell->metal_sidebar_contains_text_for_testing("Cmd+Shift+U");
  }));
  const auto canvas = shell->metal_sidebar_screen_bounds_for_testing();
  const auto viewport = shell->tab_list_view()->parent()->GetBoundsInScreen();
  EXPECT_EQ(SidebarNamedButton(shell->sidebar_view(), u"Adres strony")->GetBoundsInScreen().origin(), canvas.origin());
  EXPECT_EQ(viewport.width(), action->width());
  const auto scrollbar_bounds = shell->tab_viewport()->vertical_scroll_bar()->GetBoundsInScreen();
  EXPECT_EQ(std::max(viewport.right(), scrollbar_bounds.right()), canvas.right());
  EXPECT_TRUE(canvas.Contains(scrollbar_bounds));
  EXPECT_EQ(shell->footer_action_for_testing(1)->parent()->GetBoundsInScreen().bottom(),canvas.bottom());
  auto paint_action = [&] {
    // The helper paints a root at (0,0); a sidebar child's parent-relative
    // origin would clip its entire recording outside the small test bitmap.
    const auto saved = action->bounds();
    action->SetBoundsRect(gfx::Rect(action->size())); action->SchedulePaint();
    auto bitmap = views::test::PaintViewToBitmap(action);
    action->SetBoundsRect(saved);
    return bitmap;
  };
  auto has_native_ink = [](const SkBitmap& bitmap) {
    // PaintViewToBitmap uses a red placeholder; any control ink changes it.
    for (int y = 0; y < bitmap.height(); ++y) for (int x = 0; x < bitmap.width(); ++x)
      if (bitmap.getColor(x, y) != SK_ColorRED) return true;
    return false;
  };
  action->SchedulePaint();
  EXPECT_FALSE(has_native_ink(paint_action()));
  shell->sidebar_widget()->Hide();
  EXPECT_FALSE(shell->metal_sidebar_visible());
  action->SchedulePaint();
  EXPECT_TRUE(has_native_ink(paint_action()));
  shell->ShowSidebar();
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->metal_sidebar_visible(); }));
  ui_controls::EnableUIControls();
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(window_->widget()->GetNativeWindow()));
  const auto point = action->GetBoundsInScreen().CenterPoint();
  base::test::TestFuture<void> moved;
  ASSERT_TRUE(ui_controls::SendMouseMoveNotifyWhenDone(point.x(), point.y(), moved.GetCallback(), shell->sidebar_widget()->GetNativeWindow()));
  ASSERT_TRUE(moved.Wait());
  base::test::TestFuture<void> clicked;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      clicked.GetCallback(), ui_controls::kNoAccelerator, shell->sidebar_widget()->GetNativeWindow()));
  ASSERT_TRUE(clicked.Wait());
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->address_visible(); }));
  EXPECT_TRUE(shell->address_input()->GetText().empty()); EXPECT_EQ(29u, window_->tab_count("work"));
  const auto next = embedded_test_server()->GetURL("/storage_probe.html?new-tab-action");
  shell->address_input()->SetText(base::UTF8ToUTF16(next.spec()));
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(shell->address_widget()->GetNativeWindow(), ui::VKEY_RETURN, false, false, false, false));
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->tab_count("work") == 30; }));
  ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));
  EXPECT_EQ(next, window_->contents("work")->GetLastCommittedURL());
  EXPECT_EQ(work_.get(), window_->contents("work")->GetBrowserContext());
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(window_->widget()->GetNativeWindow(), ui::VKEY_U, false, true, false, true));
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->address_visible(); }));
  EXPECT_TRUE(shell->address_input()->GetText().empty()); EXPECT_EQ(30u, window_->tab_count("work"));
}
IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, AddressAndWorkspaceHeadingOwnSceneText) {
  auto* shell = window_->shell();
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->appearance_ready(); }));
  shell->SetWorkspaceAnimations(false); shell->SetWorkspaceEmoji("work", u"🚀");
  const auto url = embedded_test_server()->GetURL("/storage_probe.html?address-scene");
  ASSERT_TRUE(window_->NavigateFromBar(url, false));
  ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));
  shell->CloseOverlays(true); shell->SetPinned(true); shell->ShowSidebar();
  auto* pill = SidebarNamedButton(shell->sidebar_view(), u"Adres strony");
  ASSERT_TRUE(pill);
  auto no_labels = [&](auto&& self, views::View* view) -> void {
    EXPECT_FALSE(views::AsViewClass<views::Label>(view));
    for (const auto& child : view->children()) self(self, child.get());
  };
  no_labels(no_labels, pill);  // Button may install a native focus-ring child.
  ui::AXNodeData accessible; pill->GetViewAccessibility().GetAccessibleNodeData(&accessible);
  EXPECT_EQ(u"127.0.0.1/storage_probe.html", accessible.GetString16Attribute(ax::mojom::StringAttribute::kDescription));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return shell->metal_sidebar_visible() &&
        shell->metal_sidebar_contains_text_for_testing("/storage_probe.html") &&
        shell->metal_sidebar_contains_text_for_testing("🚀  Praca");
  }));
  for (int width : {200, 480}) {
    shell->ResizeSidebar(width - shell->sidebar_width(), true);
    ASSERT_TRUE(base::test::RunUntil([&] {
      return shell->metal_sidebar_screen_bounds_for_testing().width() == pill->width();
    }));
    EXPECT_EQ(pill->GetBoundsInScreen().origin(), shell->metal_sidebar_screen_bounds_for_testing().origin());
    EXPECT_EQ(shell->tab_list_view()->parent()->width(), pill->width());
  }
  for (auto theme : {ThemeMode::kDark, ThemeMode::kLight}) {
    const auto before = shell->metal_sidebar_frame_count();
    shell->SetThemeMode(theme);
    ASSERT_TRUE(base::test::RunUntil([&] {
      return shell->metal_sidebar_frame_count() > before &&
          shell->metal_sidebar_contains_text_for_testing("/storage_probe.html");
    }));
  }
  auto native_ink = [&] {
    const auto saved = pill->bounds(); pill->SetBoundsRect(gfx::Rect(pill->size())); pill->SchedulePaint();
    const auto bitmap = views::test::PaintViewToBitmap(pill); pill->SetBoundsRect(saved);
    for (int y=0; y<bitmap.height(); ++y) for (int x=0; x<bitmap.width(); ++x)
      if (bitmap.getColor(x,y) != SK_ColorRED) return true;
    return false;
  };
  EXPECT_FALSE(native_ink()); shell->sidebar_widget()->Hide();
  EXPECT_FALSE(shell->metal_sidebar_visible()); EXPECT_TRUE(native_ink());
  shell->ShowSidebar();
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->metal_sidebar_visible(); }));
  ui_controls::EnableUIControls();
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(window_->widget()->GetNativeWindow()));
  const auto point = pill->GetBoundsInScreen().CenterPoint();
  base::test::TestFuture<void> moved;
  ASSERT_TRUE(ui_controls::SendMouseMoveNotifyWhenDone(point.x(), point.y(), moved.GetCallback(), shell->sidebar_widget()->GetNativeWindow()));
  ASSERT_TRUE(moved.Wait()); base::test::TestFuture<void> clicked;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      clicked.GetCallback(), ui_controls::kNoAccelerator, shell->sidebar_widget()->GetNativeWindow()));
  ASSERT_TRUE(clicked.Wait()); ASSERT_TRUE(base::test::RunUntil([&] { return shell->address_visible(); }));
  EXPECT_EQ(base::UTF8ToUTF16(url.spec()), shell->address_input()->GetText());
  shell->CloseOverlays(true);
  // The sidebar is intentionally a nonactivating native panel, as in Views.
  // Keyboard entry uses the existing main-window shortcut, not panel focus.
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(window_->widget()->GetNativeWindow(), ui::VKEY_L, false, false, false, true));
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->address_visible(); }));
  EXPECT_EQ(base::UTF8ToUTF16(url.spec()), shell->address_input()->GetText());
  shell->CloseOverlays(true);
  content::URLLoaderInterceptor secure_interceptor(base::BindRepeating(
      [](content::URLLoaderInterceptor::RequestParams* params) {
        if (params->url_request.url.host() != "address-scene.test") return false;
        content::URLLoaderInterceptor::WriteResponse("HTTP/1.1 200 OK\nContent-Type: text/html\n\n",
            "<!doctype html><title>Secure scene</title>", params->client.get());
        return true;
      }));
  ASSERT_TRUE(window_->NavigateFromBar(GURL("https://address-scene.test/secure/path"), false));
  ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return shell->metal_sidebar_contains_text_for_testing("address-scene.test") &&
        shell->metal_sidebar_contains_text_for_testing("/secure/path");
  }));
  // Changing both the workspace and its emoji must update the fixed heading.
  ASSERT_TRUE(window_->Select("personal")); shell->SetWorkspaceEmoji("personal", u"🏠"); shell->ShowSidebar();
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->metal_sidebar_contains_text_for_testing("🏠  Prywatne"); }));
  ASSERT_TRUE(window_->Select("work")); shell->ShowSidebar();
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->metal_sidebar_contains_text_for_testing("🚀  Praca"); }));
}
IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, ScrollViewportTrackpadFractionalMomentumAndAxisRouting) {
  window_->widget()->Show();auto delegate=std::make_unique<views::WidgetDelegate>();auto* host=delegate->SetContentsView(std::make_unique<views::View>());PanelViewport* viewport=nullptr;auto candidate=CreatePanelViewport(&viewport,base::DoNothing());auto* view=host->AddChildView(std::move(candidate));ASSERT_TRUE(viewport->rust_owned());viewport->SetPanelContents(std::make_unique<ToolsPanelProbeView>(200,2000,true));
  views::Widget::InitParams params(views::Widget::InitParams::CLIENT_OWNS_WIDGET,views::Widget::InitParams::TYPE_BUBBLE);params.delegate=delegate.get();params.SetParent(window_->widget());auto widget=std::make_unique<views::Widget>();widget->Init(std::move(params));widget->SetBounds(gfx::Rect(40,40,400,320));widget->Show();widget->LayoutRootViewIfNecessary();view->SetBounds(0,0,300,200);view->DeprecatedLayoutImmediately();ASSERT_GT(viewport->maximum(),0);
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(widget->GetNativeWindow()));base::test::TestFuture<void> presented;auto* compositor=widget->GetCompositor();ASSERT_TRUE(compositor);widget->GetRootView()->SchedulePaint();compositor->RequestSuccessfulPresentationTimeForNextFrame(base::BindOnce([](base::OnceClosure done,const viz::FrameTimingDetails&){std::move(done).Run();},presented.GetCallback()));compositor->ScheduleFullRedraw();ASSERT_TRUE(presented.Wait());
  auto stamp=base::TimeTicks::Now();auto send=[&](float y,ui::EventMomentumPhase momentum,ui::ScrollEventPhase phase){stamp+=base::Milliseconds(10);ui::ScrollEvent event(ui::EventType::kScroll,gfx::Point(10,10),stamp,0,0,y,0,y,2,momentum,phase);widget->OnScrollEvent(&event);};
  send(0,ui::EventMomentumPhase::MAY_BEGIN,ui::ScrollEventPhase::kBegan);send(-13.25f,ui::EventMomentumPhase::NONE,ui::ScrollEventPhase::kUpdate);EXPECT_DOUBLE_EQ(13.25,viewport->offset());
  for(int i=0;i<8;++i)send(-0.125f,ui::EventMomentumPhase::NONE,ui::ScrollEventPhase::kUpdate);EXPECT_DOUBLE_EQ(14.25,viewport->offset());send(-4.5f,ui::EventMomentumPhase::BEGAN,ui::ScrollEventPhase::kNone);send(-1.25f,ui::EventMomentumPhase::INERTIAL_UPDATE,ui::ScrollEventPhase::kNone);send(0,ui::EventMomentumPhase::END,ui::ScrollEventPhase::kEnd);EXPECT_DOUBLE_EQ(20,viewport->offset());
  ui::ScrollEvent horizontal(ui::EventType::kScroll,gfx::Point(10,10),stamp,0,-60,0,-60,0,2,ui::EventMomentumPhase::NONE,ui::ScrollEventPhase::kUpdate);view->OnScrollEvent(&horizontal);EXPECT_FALSE(horizontal.handled());EXPECT_FALSE(horizontal.stopped_propagation());ui::MouseWheelEvent wheel(gfx::Vector2d(-60,0),gfx::PointF(10,10),gfx::PointF(10,10),stamp,0,0);EXPECT_FALSE(view->OnMouseWheel(wheel));EXPECT_DOUBLE_EQ(20,viewport->offset());
  ui::ScrollEvent velocity(ui::EventType::kScrollFlingStart,gfx::Point(10,10),stamp,0,0,-400,0,-400,2,ui::EventMomentumPhase::NONE,ui::ScrollEventPhase::kNone);view->OnScrollEvent(&velocity);EXPECT_FALSE(velocity.handled());EXPECT_DOUBLE_EQ(20,viewport->offset());
  send(-1000000,ui::EventMomentumPhase::INERTIAL_UPDATE,ui::ScrollEventPhase::kNone);EXPECT_DOUBLE_EQ(viewport->maximum(),viewport->offset());send(1000000,ui::EventMomentumPhase::NONE,ui::ScrollEventPhase::kUpdate);EXPECT_DOUBLE_EQ(0,viewport->offset());viewport->SetPanelContents(std::make_unique<ToolsPanelProbeView>(200,10,false));view->DeprecatedLayoutImmediately();EXPECT_EQ(0,viewport->maximum());send(-25,ui::EventMomentumPhase::NONE,ui::ScrollEventPhase::kUpdate);EXPECT_DOUBLE_EQ(0,viewport->offset());widget->CloseNow();
}

IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, HorizontalViewportTrackpadFractionalMomentumAndAxisRouting) {
  window_->widget()->Show();auto delegate=std::make_unique<views::WidgetDelegate>();auto* host=delegate->SetContentsView(std::make_unique<views::View>());PanelViewport* viewport=nullptr;auto candidate=CreatePanelViewport(&viewport,base::DoNothing(),{-1,-1,true,false,false,false,true});auto* view=host->AddChildView(std::move(candidate));ASSERT_TRUE(viewport->rust_owned());viewport->SetPanelContents(std::make_unique<ToolsPanelProbeView>(2000,32,false));
  views::Widget::InitParams params(views::Widget::InitParams::CLIENT_OWNS_WIDGET,views::Widget::InitParams::TYPE_BUBBLE);params.delegate=delegate.get();params.SetParent(window_->widget());auto widget=std::make_unique<views::Widget>();widget->Init(std::move(params));widget->SetBounds(gfx::Rect(40,40,400,320));widget->Show();widget->LayoutRootViewIfNecessary();view->SetBounds(0,0,300,200);view->DeprecatedLayoutImmediately();ASSERT_GT(viewport->maximum(),0);
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(widget->GetNativeWindow()));base::test::TestFuture<void> presented;auto* compositor=widget->GetCompositor();ASSERT_TRUE(compositor);widget->GetRootView()->SchedulePaint();compositor->RequestSuccessfulPresentationTimeForNextFrame(base::BindOnce([](base::OnceClosure done,const viz::FrameTimingDetails&){std::move(done).Run();},presented.GetCallback()));compositor->ScheduleFullRedraw();ASSERT_TRUE(presented.Wait());
  auto stamp=base::TimeTicks::Now();auto send=[&](float x,ui::EventMomentumPhase momentum,ui::ScrollEventPhase phase){stamp+=base::Milliseconds(10);ui::ScrollEvent event(ui::EventType::kScroll,gfx::Point(10,10),stamp,0,x,0,x,0,2,momentum,phase);widget->OnScrollEvent(&event);};
  send(0,ui::EventMomentumPhase::MAY_BEGIN,ui::ScrollEventPhase::kBegan);send(-13.25f,ui::EventMomentumPhase::NONE,ui::ScrollEventPhase::kUpdate);EXPECT_DOUBLE_EQ(13.25,viewport->offset());
  for(int i=0;i<8;++i)send(-0.125f,ui::EventMomentumPhase::NONE,ui::ScrollEventPhase::kUpdate);EXPECT_DOUBLE_EQ(14.25,viewport->offset());send(-4.5f,ui::EventMomentumPhase::BEGAN,ui::ScrollEventPhase::kNone);send(-1.25f,ui::EventMomentumPhase::INERTIAL_UPDATE,ui::ScrollEventPhase::kNone);send(0,ui::EventMomentumPhase::END,ui::ScrollEventPhase::kEnd);EXPECT_DOUBLE_EQ(20,viewport->offset());
  ui::ScrollEvent vertical(ui::EventType::kScroll,gfx::Point(10,10),stamp,0,0,-60,0,-60,2,ui::EventMomentumPhase::NONE,ui::ScrollEventPhase::kUpdate);view->OnScrollEvent(&vertical);EXPECT_FALSE(vertical.handled());EXPECT_FALSE(vertical.stopped_propagation());ui::MouseWheelEvent wheel(gfx::Vector2d(0,-60),gfx::PointF(10,10),gfx::PointF(10,10),stamp,0,0);EXPECT_FALSE(view->OnMouseWheel(wheel));EXPECT_DOUBLE_EQ(20,viewport->offset());
  ui::ScrollEvent velocity(ui::EventType::kScrollFlingStart,gfx::Point(10,10),stamp,0,-400,0,-400,0,2,ui::EventMomentumPhase::NONE,ui::ScrollEventPhase::kNone);view->OnScrollEvent(&velocity);EXPECT_FALSE(velocity.handled());EXPECT_DOUBLE_EQ(20,viewport->offset());
  send(-1000000,ui::EventMomentumPhase::INERTIAL_UPDATE,ui::ScrollEventPhase::kNone);EXPECT_DOUBLE_EQ(viewport->maximum(),viewport->offset());send(1000000,ui::EventMomentumPhase::NONE,ui::ScrollEventPhase::kUpdate);EXPECT_DOUBLE_EQ(0,viewport->offset());viewport->SetPanelContents(std::make_unique<ToolsPanelProbeView>(10,32,false));view->DeprecatedLayoutImmediately();EXPECT_EQ(0,viewport->maximum());send(-25,ui::EventMomentumPhase::NONE,ui::ScrollEventPhase::kUpdate);EXPECT_DOUBLE_EQ(0,viewport->offset());widget->CloseNow();
}

IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, RustScrollThumbDragTrackKeyboardAndAccessibility) {
  auto* shell = window_->shell();
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->appearance_ready(); }));
  shell->SetWorkspaceAnimations(false);
  const auto url=embedded_test_server()->GetURL("/storage_probe.html");
  for (int i=0;i<28;++i) ASSERT_TRUE(window_->NavigateFromBar(url,true));
  shell->CloseOverlays(true); shell->SetPinned(true); shell->ShowSidebar();
  auto* scroll=shell->tab_viewport(); auto* bar=scroll->vertical_scroll_bar();
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->metal_sidebar_visible() && bar->IsDrawn(); }));
  ASSERT_EQ(1u,bar->children().size()); auto* thumb=bar->children().front().get();
  auto has_submitted_thumb=[&] {
    const auto frame=shell->metal_sidebar_screen_bounds_for_testing();
    auto rect=gfx::RectF(thumb->GetBoundsInScreen()); rect.Offset(-frame.x(),-frame.y());
    rect.Inset(gfx::InsetsF::TLBR(2,4,2,4));
    auto clip=gfx::RectF(bar->GetBoundsInScreen()); clip.Offset(-frame.x(),-frame.y());
    return frame.Contains(bar->GetBoundsInScreen()) && shell->metal_sidebar_contains_rect_for_testing(rect,clip);
  };
  ASSERT_TRUE(base::test::RunUntil(has_submitted_thumb));
  auto native_ink=[&] {
    const auto saved=bar->bounds(); bar->SetBoundsRect(gfx::Rect(bar->size())); bar->SchedulePaint();
    const auto bitmap=views::test::PaintViewToBitmap(bar); bar->SetBoundsRect(saved);
    for (int y=0;y<bitmap.height();++y) for (int x=0;x<bitmap.width();++x)
      if (bitmap.getColor(x,y)!=SK_ColorRED) return true;
    return false;
  };
  EXPECT_FALSE(native_ink()); shell->sidebar_widget()->Hide();
  EXPECT_FALSE(shell->metal_sidebar_visible()); EXPECT_TRUE(native_ink());
  shell->ShowSidebar(); ASSERT_TRUE(base::test::RunUntil([&] { return shell->metal_sidebar_visible(); }));
  ui_controls::EnableUIControls();
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(window_->widget()->GetNativeWindow()));
  auto move=[&](gfx::Point point) {
    base::test::TestFuture<void> done;
    return ui_controls::SendMouseMoveNotifyWhenDone(point.x(),point.y(),done.GetCallback(),shell->sidebar_widget()->GetNativeWindow()) && done.Wait();
  };
  auto mouse=[&](int state) {
    base::test::TestFuture<void> done;
    return ui_controls::SendMouseEventsNotifyWhenDone(ui_controls::LEFT,state,done.GetCallback(),ui_controls::kNoAccelerator,shell->sidebar_widget()->GetNativeWindow()) && done.Wait();
  };
  ASSERT_TRUE(move(thumb->GetBoundsInScreen().CenterPoint())); ASSERT_TRUE(mouse(ui_controls::DOWN));
  base::ScopedClosureRunner release_mouse(base::BindOnce([] { ui_controls::SendMouseEvents(ui_controls::LEFT,ui_controls::UP); }));
  const auto track=bar->GetBoundsInScreen();
  const gfx::Point end(track.CenterPoint().x(),track.bottom()-1);
  ASSERT_TRUE(move(end));
  const int max_offset=shell->tab_list_view()->height()-shell->tab_list_view()->parent()->height();
  ASSERT_GT(max_offset,0);
  ASSERT_TRUE(base::test::RunUntil([&] { return scroll->visible_rect().y()==max_offset; }));
  // Preserve the existing thumb snap-back threshold while capture remains held.
  ASSERT_TRUE(move(gfx::Point(track.x()-200,end.y())));
  ASSERT_TRUE(base::test::RunUntil([&] { return scroll->visible_rect().y()==0; }));
  ASSERT_TRUE(move(end));
  ASSERT_TRUE(base::test::RunUntil([&] { return scroll->visible_rect().y()==max_offset; }));
  ASSERT_TRUE(mouse(ui_controls::UP)); static_cast<void>(release_mouse.Release());
  ASSERT_TRUE(base::test::RunUntil(has_submitted_thumb));
  ui::AXNodeData accessible; bar->GetViewAccessibility().GetAccessibleNodeData(&accessible);
  EXPECT_EQ(ax::mojom::Role::kScrollBar,accessible.role);
  EXPECT_EQ(max_offset,accessible.GetFloatAttribute(ax::mojom::FloatAttribute::kMaxValueForRange));
  EXPECT_EQ(max_offset,accessible.GetFloatAttribute(ax::mojom::FloatAttribute::kValueForRange));
  ui::AXActionData decrement; decrement.action=ax::mojom::Action::kDecrement;
  EXPECT_TRUE(bar->HandleAccessibleAction(decrement)); EXPECT_LT(scroll->visible_rect().y(),max_offset);
  ui::KeyEvent home(ui::EventType::kKeyPressed,ui::VKEY_HOME,ui::EF_NONE);
  EXPECT_TRUE(bar->OnKeyPressed(home)); EXPECT_EQ(0,scroll->visible_rect().y());
  ASSERT_TRUE(move(gfx::Point(track.CenterPoint().x(),thumb->GetBoundsInScreen().bottom()+10)));
  ASSERT_TRUE(mouse(ui_controls::DOWN|ui_controls::UP));
  ASSERT_TRUE(base::test::RunUntil([&] { return scroll->visible_rect().y()>0; }));
  ui::KeyEvent last(ui::EventType::kKeyPressed,ui::VKEY_END,ui::EF_NONE);
  EXPECT_TRUE(bar->OnKeyPressed(last)); EXPECT_EQ(max_offset,scroll->visible_rect().y());
}
IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, FooterSpacesScrollSelectPinAndSettings) {
  auto* shell=window_->shell();
  ASSERT_TRUE(base::test::RunUntil([&] {return shell->appearance_ready();}));
  shell->SetWorkspaceAnimations(false);
  for (int i=0;i<10;++i) {
    ASSERT_TRUE(window_->AddWorkspace("Footer "+std::to_string(i),"work-profile"));
    ASSERT_TRUE(base::test::RunUntil([&] {return !window_->profiles_busy();}));
  }
  const auto last=window_->workspaces().back().id;
  shell->SetWorkspaceEmoji("work",u"🦀");shell->SetWorkspaceEmoji(last,u"🚀");
  ASSERT_TRUE(window_->Select("work"));shell->CloseOverlays(true);shell->SetPinned(true);shell->ShowSidebar();
  auto* settings=shell->footer_action_for_testing(1);ASSERT_TRUE(settings);
  auto* footer=settings->parent();auto* pin=shell->footer_action_for_testing(0);
  auto* scroll=shell->workspace_viewport();ASSERT_TRUE(scroll);EXPECT_EQ(footer->children()[2].get(),scroll->view());auto* bar=scroll->horizontal_scroll_bar();
  ASSERT_TRUE(base::test::RunUntil([&] {return shell->metal_sidebar_visible() && bar->IsDrawn();}));
  EXPECT_EQ(47,footer->height());EXPECT_GE(scroll->contents()->parent()->height(),28);
  EXPECT_TRUE(shell->metal_sidebar_screen_bounds_for_testing().Contains(footer->GetBoundsInScreen()));
  EXPECT_TRUE(shell->metal_sidebar_contains_text_for_testing("🦀"));
  ui_controls::EnableUIControls();
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(window_->widget()->GetNativeWindow()));
  auto move=[&](gfx::Point point) {
    base::test::TestFuture<void> done;return ui_controls::SendMouseMoveNotifyWhenDone(point.x(),point.y(),done.GetCallback(),shell->sidebar_widget()->GetNativeWindow()) && done.Wait();
  };
  auto mouse=[&](int state) {
    base::test::TestFuture<void> done;return ui_controls::SendMouseEventsNotifyWhenDone(ui_controls::LEFT,state,done.GetCallback(),ui_controls::kNoAccelerator,shell->sidebar_widget()->GetNativeWindow()) && done.Wait();
  };
  ASSERT_TRUE(move(bar->children().front()->GetBoundsInScreen().CenterPoint()));ASSERT_TRUE(mouse(ui_controls::DOWN));
  base::ScopedClosureRunner release_mouse(base::BindOnce([]{ui_controls::SendMouseEvents(ui_controls::LEFT,ui_controls::UP);}));
  const auto track=bar->GetBoundsInScreen();ASSERT_TRUE(move(gfx::Point(track.right()-1,track.CenterPoint().y())));
  ASSERT_TRUE(mouse(ui_controls::UP));static_cast<void>(release_mouse.Release());
  ASSERT_TRUE(base::test::RunUntil([&] {return scroll->offset()>0 && shell->metal_sidebar_contains_text_for_testing("🚀");}));
  auto* last_button=shell->workspace_button(last);ASSERT_TRUE(last_button);
  EXPECT_TRUE(scroll->contents()->parent()->GetBoundsInScreen().Contains(last_button->GetBoundsInScreen()));
  ASSERT_TRUE(move(last_button->GetBoundsInScreen().CenterPoint()));ASSERT_TRUE(mouse(ui_controls::DOWN|ui_controls::UP));
  ASSERT_TRUE(base::test::RunUntil([&] {return window_->active_id()==last;}));
  ASSERT_TRUE(move(settings->GetBoundsInScreen().CenterPoint()));ASSERT_TRUE(mouse(ui_controls::DOWN|ui_controls::UP));
  ASSERT_TRUE(base::test::RunUntil([&] {return shell->settings_visible();}));
  shell->emoji_input()->RequestFocus();shell->emoji_input()->SelectRange(gfx::Range(0,shell->emoji_input()->GetText().size()));
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(shell->settings_widget()->GetNativeWindow(),ui::VKEY_BACK,false,false,false,false));
  ASSERT_TRUE(base::test::RunUntil([&] {return shell->workspace_emoji(last).empty();}));
  EXPECT_TRUE(shell->settings_visible());shell->CloseOverlays(true);
  pin->SetEnabled(false);ASSERT_TRUE(move(pin->GetBoundsInScreen().CenterPoint()));ASSERT_TRUE(mouse(ui_controls::DOWN|ui_controls::UP));EXPECT_TRUE(shell->pinned());
  pin->SetEnabled(true);ASSERT_TRUE(mouse(ui_controls::DOWN|ui_controls::UP));EXPECT_FALSE(shell->pinned());
  shell->SetPinned(true);shell->ShowSidebar();
  auto native_ink=[&] {
    const auto saved=pin->bounds();pin->SetBoundsRect(gfx::Rect(pin->size()));pin->SchedulePaint();
    const auto bitmap=views::test::PaintViewToBitmap(pin);pin->SetBoundsRect(saved);
    for(int y=0;y<bitmap.height();++y)for(int x=0;x<bitmap.width();++x)if(bitmap.getColor(x,y)!=SK_ColorRED)return true;
    return false;
  };
  ASSERT_TRUE(base::test::RunUntil([&] {return shell->metal_sidebar_visible();}));EXPECT_FALSE(native_ink());
  shell->sidebar_widget()->Hide();EXPECT_TRUE(native_ink());
}
IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, NavigationHeaderHistoryReloadAndIndependentVisibility) {
  auto* shell=window_->shell();
  ASSERT_TRUE(base::test::RunUntil([&] {return shell->appearance_ready();}));
  shell->SetWorkspaceAnimations(false);shell->SetPinned(true);shell->ShowSidebar();
  const auto first=embedded_test_server()->GetURL("/storage_probe.html?header-first");
  const auto second=embedded_test_server()->GetURL("/storage_probe.html?header-second");
  ASSERT_TRUE(window_->NavigateFromBar(first,false));ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));
  ASSERT_TRUE(window_->NavigateFromBar(second,false));ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));
  auto* back=shell->header_action_for_testing(1);auto* forward=shell->header_action_for_testing(2);
  auto* reload=shell->header_action_for_testing(3);auto* toggle=shell->header_action_for_testing(0);
  ASSERT_TRUE(back);ASSERT_TRUE(forward);ASSERT_TRUE(reload);ASSERT_TRUE(toggle);
  auto* header=back->parent();EXPECT_EQ(134,header->width());EXPECT_EQ(102,reload->x());
  ASSERT_TRUE(base::test::RunUntil([&] {return shell->metal_header_visible() && back->GetEnabled() && !forward->GetEnabled();}));
  EXPECT_TRUE(shell->metal_header_screen_bounds_for_testing().Contains(reload->GetBoundsInScreen().CenterPoint()));
  auto native_ink=[&] {
    const auto saved=reload->bounds();reload->SetBoundsRect(gfx::Rect(reload->size()));reload->SchedulePaint();
    const auto bitmap=views::test::PaintViewToBitmap(reload);reload->SetBoundsRect(saved);
    for(int y=0;y<bitmap.height();++y)for(int x=0;x<bitmap.width();++x)if(bitmap.getColor(x,y)!=SK_ColorRED)return true;
    return false;
  };
  const auto before_focus=shell->metal_header_frame_count();
  reload->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);reload->RequestFocus();
  ASSERT_TRUE(base::test::RunUntil([&] {return reload->HasFocus() && shell->metal_header_frame_count()>before_focus;}));
  EXPECT_FALSE(native_ink()); // Includes the native focus-ring child.
  ui_controls::EnableUIControls();ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(window_->widget()->GetNativeWindow()));
  auto click=[&](views::View* button) {
    const auto point=button->GetBoundsInScreen().CenterPoint();base::test::TestFuture<void> moved,pressed;
    return ui_controls::SendMouseMoveNotifyWhenDone(point.x(),point.y(),moved.GetCallback(),window_->widget()->GetNativeWindow()) && moved.Wait() &&
        ui_controls::SendMouseEventsNotifyWhenDone(ui_controls::LEFT,ui_controls::DOWN|ui_controls::UP,pressed.GetCallback(),ui_controls::kNoAccelerator,window_->widget()->GetNativeWindow()) && pressed.Wait();
  };
  const int index=window_->contents("work")->GetController().GetLastCommittedEntryIndex();
  ASSERT_TRUE(click(forward));base::RunLoop().RunUntilIdle();
  EXPECT_EQ(index,window_->contents("work")->GetController().GetLastCommittedEntryIndex());
  {content::TestNavigationObserver loaded(window_->contents("work"));ASSERT_TRUE(click(back));loaded.Wait();}
  EXPECT_EQ(first,window_->contents("work")->GetLastCommittedURL());
  ASSERT_TRUE(base::test::RunUntil([&] {return forward->GetEnabled();}));
  {content::TestNavigationObserver loaded(window_->contents("work"));ASSERT_TRUE(click(forward));loaded.Wait();}
  EXPECT_EQ(second,window_->contents("work")->GetLastCommittedURL());
  ASSERT_TRUE(click(toggle));ASSERT_TRUE(base::test::RunUntil([&] {return !shell->sidebar_visible() && shell->metal_header_visible();}));
  ASSERT_TRUE(content::ExecJs(window_->contents("work"),"window.header_reload_marker=42"));
  {content::TestNavigationObserver loaded(window_->contents("work"));ASSERT_TRUE(click(reload));loaded.Wait();}
  EXPECT_EQ("undefined",content::EvalJs(window_->contents("work"),"typeof window.header_reload_marker"));
  EXPECT_EQ(second,window_->contents("work")->GetLastCommittedURL());
  EXPECT_EQ(work_.get(),window_->contents("work")->GetBrowserContext());
  ASSERT_TRUE(click(toggle));ASSERT_TRUE(base::test::RunUntil([&] {return shell->sidebar_visible() && shell->metal_header_visible();}));
  const auto old_bounds=window_->widget()->GetWindowBoundsInScreen();const auto old_frame=shell->metal_header_screen_bounds_for_testing();
  auto moved_bounds=old_bounds;moved_bounds.Offset(16,12);window_->widget()->SetBounds(moved_bounds);
  ASSERT_TRUE(base::test::RunUntil([&] {return shell->metal_header_screen_bounds_for_testing().origin()==old_frame.origin()+gfx::Vector2d(16,12);}));
  window_->widget()->Hide();EXPECT_FALSE(shell->metal_header_visible());EXPECT_TRUE(native_ink());
  window_->widget()->Show();ASSERT_TRUE(base::test::RunUntil([&] {return shell->metal_header_visible();}));
  base::RunLoop().RunUntilIdle();const auto idle=shell->metal_header_frame_count();base::RunLoop().RunUntilIdle();
  EXPECT_EQ(idle,shell->metal_header_frame_count());
}
IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, PaletteChromeTextErrorsThemeAndFallback) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->SetWorkspaceAnimations(false);
  const auto original=window_->contents("work")->GetLastCommittedURL();shell->OpenCommandPalette();SetPaletteQuery(u"> no-such-zitter-command");ASSERT_TRUE(shell->suggestions().empty());
  ui::KeyEvent enter(ui::EventType::kKeyPressed,ui::VKEY_RETURN,0);auto* field=shell->address_input();
  ASSERT_TRUE(field->view()->OnKeyPressed(enter));
  auto ready=[&]{return shell->metal_palette_chrome_visible(0)&&shell->metal_palette_chrome_visible(1)&&shell->metal_palette_chrome_visible(2)&&shell->metal_palette_chrome_visible(3);};
  ASSERT_TRUE(base::test::RunUntil(ready));EXPECT_FALSE(shell->metal_palette_visible());EXPECT_TRUE(shell->address_visible());EXPECT_TRUE(field->HasFocus());
  EXPECT_TRUE(shell->metal_palette_chrome_contains_text_for_testing(1,"esc"));EXPECT_TRUE(shell->metal_palette_chrome_contains_text_for_testing(3,"Brak pasujących poleceń."));
  auto* root=field->view()->parent()->parent();auto no_labels=[&](auto&& self,views::View* view)->void{if(view==field->view())return;EXPECT_FALSE(views::AsViewClass<views::Label>(view));for(const auto& child:view->children())self(self,child.get());};no_labels(no_labels,root);
  for(size_t i=0;i<4;++i){const auto frame=shell->metal_palette_chrome_screen_bounds_for_testing(i);EXPECT_FALSE(frame.IsEmpty());EXPECT_FALSE(frame.Intersects(field->view()->GetBoundsInScreen()));}
  const auto hint_frame=shell->metal_palette_chrome_screen_bounds_for_testing(2),hint_view=shell->palette_chrome_view_for_testing(2)->GetBoundsInScreen();
  EXPECT_GT(hint_frame.x(),hint_view.x());EXPECT_LT(hint_frame.bottom(),hint_view.bottom()-5);
  ui::AXNodeData accessible;shell->palette_chrome_view_for_testing(3)->GetViewAccessibility().GetAccessibleNodeData(&accessible);EXPECT_EQ(ax::mojom::Role::kStaticText,accessible.role);EXPECT_EQ(u"Brak pasujących poleceń.",accessible.GetString16Attribute(ax::mojom::StringAttribute::kName));
  auto* esc=shell->palette_chrome_view_for_testing(1);auto native_ink=[&]{const auto saved=esc->bounds();esc->SetBoundsRect(gfx::Rect(esc->size()));esc->SchedulePaint();const auto bitmap=views::test::PaintViewToBitmap(esc);esc->SetBoundsRect(saved);for(int y=0;y<bitmap.height();++y)for(int x=0;x<bitmap.width();++x)if(bitmap.getColor(x,y)!=SK_ColorRED)return true;return false;};
  EXPECT_FALSE(native_ink());auto* panel=field->view()->GetWidget();panel->Hide();EXPECT_TRUE(native_ink());panel->Show();panel->Activate();field->RequestFocus();ASSERT_TRUE(base::test::RunUntil(ready));
  shell->SetThemeMode(ThemeMode::kLight);base::RunLoop().RunUntilIdle();const auto before=shell->metal_palette_chrome_frame_count_for_testing(1);
  shell->SetThemeMode(ThemeMode::kDark);ASSERT_TRUE(base::test::RunUntil([&]{return ready()&&shell->metal_palette_chrome_frame_count_for_testing(1)>before;}));
  // Error text updates explicitly while native paint is locked; the editor is
  // still the only text-input owner and an invalid scheme cannot navigate.
  {
    views::ScopedPaintLock locked(shell->palette_chrome_view_for_testing(3));shell->OpenAddress(true);SetPaletteQuery(u"file:///tmp/should-not-navigate");ASSERT_TRUE(field->view()->OnKeyPressed(enter));
    ASSERT_TRUE(base::test::RunUntil([&]{return shell->metal_palette_chrome_contains_text_for_testing(3,"Wpisz adres strony HTTP lub HTTPS");}));
  }
  EXPECT_EQ(original,window_->contents("work")->GetLastCommittedURL());EXPECT_TRUE(shell->address_visible());EXPECT_TRUE(field->HasFocus());
  SetPaletteQuery(u"plain search text");ASSERT_TRUE(base::test::RunUntil([&]{return !shell->metal_palette_chrome_visible(3)&&shell->metal_palette_visible();}));
  for(size_t i=0;i<3;++i){const auto frame=shell->metal_palette_chrome_screen_bounds_for_testing(i);EXPECT_FALSE(frame.Intersects(field->view()->GetBoundsInScreen()));EXPECT_FALSE(frame.Intersects(shell->metal_palette_screen_bounds_for_testing()));}
  shell->CloseOverlays();ASSERT_TRUE(base::test::RunUntil([&]{for(size_t i=0;i<4;++i)if(shell->metal_palette_chrome_visible(i))return false;return true;}));
}
IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, PaletteScenesAsyncSelectionScrollAndIME) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->SetWorkspaceAnimations(false);
  const auto url=embedded_test_server()->GetURL("/storage_probe.html?palette-gpu");
  for(int i=0;i<8;++i){ASSERT_TRUE(window_->NavigateFromBar(url,true));ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));ASSERT_TRUE(content::ExecJs(window_->contents("work"),"document.title='Palette GPU 日本語 "+base::NumberToString(i)+"';"));}
  shell->OpenCommandPalette();SetPaletteQuery(u"Palette GPU");
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->search_results().size()>=8&&shell->metal_palette_visible();}));
  auto no_labels=[&](auto&& self,views::View* view)->void{EXPECT_FALSE(views::AsViewClass<views::Label>(view));for(const auto& child:view->children())self(self,child.get());};no_labels(no_labels,shell->palette_results_view());
  auto drain_history=[&]{auto* service=HistoryServiceFactory::GetForProfile(work_,ServiceAccessType::EXPLICIT_ACCESS);base::CancelableTaskTracker tasks;history::QueryOptions options;options.max_count=1;base::test::TestFuture<history::QueryResults> done;service->QueryHistory(u"Palette GPU",options,done.GetCallback(),&tasks);return done.Wait();};
  ASSERT_TRUE(drain_history());
  auto* field=shell->address_input();auto* panel=field->view()->GetWidget();
  const auto frame=shell->metal_palette_screen_bounds_for_testing();EXPECT_FALSE(frame.IsEmpty());EXPECT_FALSE(frame.Intersects(field->view()->GetBoundsInScreen()));
  ui_controls::EnableUIControls();ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(panel->GetNativeWindow()));field->RequestFocus();
  auto key=[&](ui::KeyboardCode code){return ui_test_utils::SendKeyPressToWindowSync(panel->GetNativeWindow(),code,false,false,false,false);};
  for(int i=0;i<3;++i)ASSERT_TRUE(key(ui::VKEY_DOWN));ASSERT_EQ(3u,shell->palette_selection_for_testing());
  const auto selected=shell->suggestions()[3];auto results=shell->search_results();
  PaletteResult inserted;inserted.title=u"Palette GPU 新しい結果 🦀";inserted.detail=u"History / 日本語";inserted.url=url;results.insert(results.begin(),inserted);
  {
    views::ScopedPaintLock locked(shell->palette_results_view());shell->SetSearchResults(u"Palette GPU",window_->workspaces().front().profile_id,results);
    ASSERT_EQ(selected,shell->suggestions()[shell->palette_selection_for_testing()]);EXPECT_EQ(4u,shell->palette_selection_for_testing());
    ASSERT_TRUE(base::test::RunUntil([&]{return shell->metal_palette_contains_text_for_testing("Palette GPU 新しい結果 🦀");}));
  }
  const auto count=shell->suggestions().size();shell->SetSearchResults(u"stale query",window_->workspaces().front().profile_id,{});shell->SetSearchResults(u"Palette GPU","personal-profile",{});EXPECT_EQ(count,shell->suggestions().size());
  for(int i=0;i<5;++i)ASSERT_TRUE(key(ui::VKEY_UP));EXPECT_EQ(count-1,shell->palette_selection_for_testing());
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->palette_viewport()->offset()>0;}));
  // While composition is active the platform editor owns navigation, commit
  // and cancel keys. The palette must neither change selection nor dispatch.
  ui::CompositionText composition;composition.text=u"にほん";composition.selection=gfx::Range(composition.text.size());field->SetCompositionText(composition);ASSERT_TRUE(field->IsIMEComposing());
  const auto composing_selection=shell->palette_selection_for_testing();
  for(auto code:{ui::VKEY_DOWN,ui::VKEY_RETURN,ui::VKEY_ESCAPE}){ui::KeyEvent event(ui::EventType::kKeyPressed,code,0);EXPECT_FALSE(field->view()->OnKeyPressed(event));}
  EXPECT_EQ(composing_selection,shell->palette_selection_for_testing());EXPECT_TRUE(shell->address_visible());EXPECT_TRUE(field->IsIMEComposing());field->ClearCompositionText();
  SetPaletteQuery(u"Palette GPU");ASSERT_TRUE(base::test::RunUntil([&]{return shell->search_results().size()>=8&&shell->metal_palette_visible();}));
  ASSERT_TRUE(drain_history());
  auto* row=shell->palette_results_view()->children().front().get();
  auto native_ink=[&]{const auto saved=row->bounds();row->SetBoundsRect(gfx::Rect(row->size()));row->SchedulePaint();const auto bitmap=views::test::PaintViewToBitmap(row);row->SetBoundsRect(saved);for(int y=0;y<bitmap.height();++y)for(int x=0;x<bitmap.width();++x)if(bitmap.getColor(x,y)!=SK_ColorRED)return true;return false;};
  EXPECT_FALSE(native_ink());panel->Hide();EXPECT_TRUE(native_ink());panel->Show();panel->Activate();field->RequestFocus();ASSERT_TRUE(base::test::RunUntil([&]{return shell->metal_palette_visible();}));
  const auto current_title=window_->contents("work")->GetTitle();
  auto target=std::find_if(shell->suggestions().begin(),shell->suggestions().end(),[&](const auto& item){return item.tab&&item.title!=current_title;});ASSERT_NE(target,shell->suggestions().end());
  const auto expected=*target;const size_t index=target-shell->suggestions().begin();auto* button=shell->palette_results_view()->children()[index].get();button->ScrollViewToVisible();
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->metal_palette_contains_text_for_testing(base::UTF16ToUTF8(expected.title));}));
  const auto point=button->GetBoundsInScreen().CenterPoint();base::test::TestFuture<void> moved;ASSERT_TRUE(ui_controls::SendMouseMoveNotifyWhenDone(point.x(),point.y(),moved.GetCallback(),panel->GetNativeWindow()));ASSERT_TRUE(moved.Wait());
  base::test::TestFuture<void> clicked;ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(ui_controls::LEFT,ui_controls::DOWN|ui_controls::UP,clicked.GetCallback(),ui_controls::kNoAccelerator,panel->GetNativeWindow()));ASSERT_TRUE(clicked.Wait());
  ASSERT_TRUE(base::test::RunUntil([&]{return !shell->address_visible()&&window_->contents("work")->GetTitle()==expected.title;}));EXPECT_EQ(work_.get(),window_->contents("work")->GetBrowserContext());EXPECT_FALSE(shell->metal_palette_visible());
}
IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, SidebarGripDragAutoHideAndEdgeReveal) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));
  shell->SetWorkspaceAnimations(false);shell->CloseOverlays(true);shell->SetPinned(true);shell->ShowSidebar();
  shell->ResizeSidebar(248-shell->sidebar_width(),true);
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->metal_sidebar_visible()&&shell->metal_sidebar_grip_visible();}));
  auto* grip=shell->sidebar_grip_for_testing();ASSERT_TRUE(grip);EXPECT_FALSE(views::AsViewClass<views::ResizeArea>(grip));
  ui_controls::EnableUIControls();ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(window_->widget()->GetNativeWindow()));
  auto move=[&](gfx::Point point,views::Widget* widget){base::test::TestFuture<void> done;return ui_controls::SendMouseMoveNotifyWhenDone(point.x(),point.y(),done.GetCallback(),widget->GetNativeWindow())&&done.Wait();};
  auto mouse=[&](int state){base::test::TestFuture<void> done;return ui_controls::SendMouseEventsNotifyWhenDone(ui_controls::LEFT,state,done.GetCallback(),ui_controls::kNoAccelerator,shell->sidebar_widget()->GetNativeWindow())&&done.Wait();};
  const auto start=grip->GetBoundsInScreen().CenterPoint();const auto page_x=shell->content_host()->GetBoundsInScreen().x();
  ASSERT_TRUE(move(start,shell->sidebar_widget()));ASSERT_TRUE(mouse(ui_controls::DOWN));
  base::ScopedClosureRunner release(base::BindOnce([]{ui_controls::SendMouseEvents(ui_controls::LEFT,ui_controls::UP);}));
  ASSERT_TRUE(move(start+gfx::Vector2d(60,0),shell->sidebar_widget()));ASSERT_TRUE(mouse(ui_controls::UP));static_cast<void>(release.Release());
  EXPECT_EQ(308,shell->sidebar_width());window_->widget()->LayoutRootViewIfNecessary();EXPECT_GT(shell->content_host()->GetBoundsInScreen().x(),page_x);
  auto gesture=[&](ui::GestureEventDetails details){ui::GestureEvent event(3,20,0,base::TimeTicks::Now(),details);grip->OnGestureEvent(&event);EXPECT_TRUE(event.handled());};
  gesture(ui::GestureEventDetails(ui::EventType::kGestureScrollBegin,0,0));
  gesture(ui::GestureEventDetails(ui::EventType::kGestureScrollUpdate,20,0));
  gesture(ui::GestureEventDetails(ui::EventType::kGestureEnd));EXPECT_EQ(328,shell->sidebar_width());
  shell->ResizeSidebar(-20,true);
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_saved();}));
  const auto wide=window_->widget()->GetWindowBoundsInScreen();auto narrow=wide;narrow.set_width(560);window_->widget()->SetBounds(narrow);
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->sidebar_widget()->GetWindowBoundsInScreen().width()<=280;}));EXPECT_EQ(308,shell->sidebar_width());
  window_->widget()->SetBounds(wide);window_->widget()->LayoutRootViewIfNecessary();
  ASSERT_TRUE(move(shell->sidebar_view()->GetBoundsInScreen().CenterPoint(),shell->sidebar_widget()));shell->SetPinned(false);
  window_->widget()->LayoutRootViewIfNecessary();const auto overlay_x=shell->content_host()->GetBoundsInScreen().x();
  ASSERT_TRUE(move(shell->content_host()->GetBoundsInScreen().CenterPoint(),window_->widget()));
  ASSERT_TRUE(base::test::RunUntil([&]{return !shell->sidebar_visible()&&!shell->metal_sidebar_grip_visible();}));
  const auto client=window_->widget()->GetClientAreaBoundsInScreen();
  ASSERT_TRUE(move(gfx::Point(client.x()+2,client.y()+150),window_->widget()));
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->sidebar_visible()&&shell->metal_sidebar_grip_visible();}));
  EXPECT_EQ(overlay_x,shell->content_host()->GetBoundsInScreen().x());
  shell->OpenWorkspaceSettings();ASSERT_TRUE(shell->settings_visible());
  ASSERT_TRUE(move(shell->content_host()->GetBoundsInScreen().CenterPoint(),window_->widget()));EXPECT_TRUE(shell->sidebar_visible());
  shell->CloseOverlays(true);shell->SetPinned(true);shell->ShowSidebar();
  // Hiding the sidebar cancels a captured resize, so a late native drag cannot
  // resize the restored sidebar until another press starts a Rust gesture.
  ui::MouseEvent press(ui::EventType::kMousePressed,gfx::Point(3,20),gfx::Point(3,20),base::TimeTicks::Now(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);
  ASSERT_TRUE(grip->OnMousePressed(press));const auto width=shell->sidebar_width();shell->sidebar_widget()->Hide();shell->ShowSidebar();
  ui::MouseEvent late(ui::EventType::kMouseDragged,gfx::Point(90,20),gfx::Point(90,20),base::TimeTicks::Now(),ui::EF_LEFT_MOUSE_BUTTON,0);
  ASSERT_TRUE(grip->OnMouseDragged(late));grip->OnMouseCaptureLost();EXPECT_EQ(width,shell->sidebar_width());
}
IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, SplitPaneScenesDragOrientTitleAndClose) {
  auto* shell=window_->shell();
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->SetWorkspaceAnimations(false);
  const auto a=embedded_test_server()->GetURL("/storage_probe.html?pane-a");
  const auto b=embedded_test_server()->GetURL("/storage_probe.html?pane-b");
  ASSERT_TRUE(window_->NavigateFromBar(a,false));ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));
  ASSERT_TRUE(window_->NavigateFromBar(b,true));ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));
  window_->SelectTab(0);ASSERT_TRUE(window_->SplitTab(1));shell->SetPinned(true);shell->ShowSidebar();
  auto ready=[&]{return shell->metal_split_visible(0)&&shell->metal_split_visible(1)&&shell->metal_split_visible(2);};
  ASSERT_TRUE(base::test::RunUntil(ready));
  auto* host=shell->content_host();auto* divider=shell->split_divider_for_testing();ASSERT_TRUE(divider);EXPECT_FALSE(views::AsViewClass<views::ResizeArea>(divider));
  auto* bar=shell->split_pane_button(true,false)->parent();
  auto no_labels=[&](auto&& self,views::View* view)->void{EXPECT_FALSE(views::AsViewClass<views::Label>(view));for(const auto& child:view->children())self(self,child.get());};
  no_labels(no_labels,bar);
  auto check_regions=[&] {
    ASSERT_TRUE(base::test::RunUntil([&]{return shell->metal_split_screen_bounds_for_testing(2)==divider->GetBoundsInScreen();}));
    const auto left=window_->pane_contents(false)->GetContainerBounds(),right=window_->pane_contents(true)->GetContainerBounds();
    EXPECT_FALSE(left.Intersects(right));
    for(size_t i=0;i<3;++i){const auto frame=shell->metal_split_screen_bounds_for_testing(i);EXPECT_FALSE(frame.Intersects(left));EXPECT_FALSE(frame.Intersects(right));}
  };
  check_regions();
  {
    views::ScopedPaintLock locked(host);
    ASSERT_TRUE(content::ExecJs(window_->pane_contents(true),"document.title='Split GPU 日本語';"));
    ASSERT_TRUE(base::test::RunUntil([&]{return shell->metal_split_contains_text_for_testing(1,"Split GPU 日本語");}));
  }
  ui_controls::EnableUIControls();ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(window_->widget()->GetNativeWindow()));
  auto move=[&](gfx::Point point){base::test::TestFuture<void> done;return ui_controls::SendMouseMoveNotifyWhenDone(point.x(),point.y(),done.GetCallback(),window_->widget()->GetNativeWindow())&&done.Wait();};
  auto mouse=[&](int state){base::test::TestFuture<void> done;return ui_controls::SendMouseEventsNotifyWhenDone(ui_controls::LEFT,state,done.GetCallback(),ui_controls::kNoAccelerator,window_->widget()->GetNativeWindow())&&done.Wait();};
  auto drag=[&](int dx,int dy) {
    const auto point=divider->GetBoundsInScreen().CenterPoint();if(!move(point)||!mouse(ui_controls::DOWN))return false;
    base::ScopedClosureRunner release(base::BindOnce([]{ui_controls::SendMouseEvents(ui_controls::LEFT,ui_controls::UP);}));
    if(!move(point+gfx::Vector2d(dx,dy))||!mouse(ui_controls::UP))return false;static_cast<void>(release.Release());return true;
  };
  const auto ratio=window_->split_ratio();ASSERT_TRUE(drag(80,0));EXPECT_GT(window_->split_ratio(),ratio);
  window_->SetSplitRatio(0.5);window_->SetSplitVertical(true);window_->widget()->LayoutRootViewIfNecessary();
  ASSERT_TRUE(base::test::RunUntil([&]{return ready() && shell->metal_split_screen_bounds_for_testing(2).width()>100;}));
  ASSERT_TRUE(drag(0,60));EXPECT_GT(window_->split_ratio(),0.5);check_regions();
  window_->SetSplitRatio(0.5);window_->widget()->LayoutRootViewIfNecessary();
  auto gesture=[&](ui::GestureEventDetails details){const auto point=divider->GetLocalBounds().CenterPoint();ui::GestureEvent event(point.x(),point.y(),0,base::TimeTicks::Now(),details);divider->OnGestureEvent(&event);EXPECT_TRUE(event.handled());};
  gesture(ui::GestureEventDetails(ui::EventType::kGestureScrollBegin,0,0));
  gesture(ui::GestureEventDetails(ui::EventType::kGestureScrollUpdate,0,40));
  gesture(ui::GestureEventDetails(ui::EventType::kGestureEnd));EXPECT_GT(window_->split_ratio(),0.5);
  // Axis changes cancel the captured Rust gesture; a late drag cannot mutate
  // the newly oriented pair until another valid press begins it.
  ui::MouseEvent press(ui::EventType::kMousePressed,gfx::Point(4,4),gfx::Point(4,4),base::TimeTicks::Now(),ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);
  ASSERT_TRUE(divider->OnMousePressed(press));window_->SetSplitVertical(false);const auto cancelled=window_->split_ratio();
  ui::MouseEvent late(ui::EventType::kMouseDragged,gfx::Point(104,4),gfx::Point(104,4),base::TimeTicks::Now(),ui::EF_LEFT_MOUSE_BUTTON,0);
  ASSERT_TRUE(divider->OnMouseDragged(late));divider->OnMouseCaptureLost();EXPECT_DOUBLE_EQ(cancelled,window_->split_ratio());
  ASSERT_TRUE(base::test::RunUntil(ready));
  auto native_ink=[&]{const auto saved=bar->bounds();bar->SetBoundsRect(gfx::Rect(bar->size()));bar->SchedulePaint();const auto bitmap=views::test::PaintViewToBitmap(bar);bar->SetBoundsRect(saved);for(int y=0;y<bitmap.height();++y)for(int x=0;x<bitmap.width();++x)if(bitmap.getColor(x,y)!=SK_ColorRED)return true;return false;};
  EXPECT_FALSE(native_ink());window_->widget()->Hide();EXPECT_TRUE(native_ink());window_->widget()->Show();ASSERT_TRUE(base::test::RunUntil(ready));
  auto* close=shell->split_pane_button(true,true);ASSERT_TRUE(move(close->GetBoundsInScreen().CenterPoint()));ASSERT_TRUE(mouse(ui_controls::DOWN|ui_controls::UP));
  ASSERT_TRUE(base::test::RunUntil([&]{return window_->tab_count("work")==1 && !window_->split();}));
  ASSERT_TRUE(base::test::RunUntil([&]{return !shell->metal_split_visible(0)&&!shell->metal_split_visible(1)&&!shell->metal_split_visible(2);}));
  EXPECT_EQ(a,window_->contents("work")->GetLastCommittedURL());
}
IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, RealTabViewportScrolls) {
  auto* shell = window_->shell();
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->appearance_ready(); }));
  shell->SetWorkspaceAnimations(false);
  const auto url = embedded_test_server()->GetURL("/storage_probe.html");
  for (int i = 0; i < 28; ++i) ASSERT_TRUE(window_->NavigateFromBar(url, true));
  shell->CloseOverlays(true); shell->ShowSidebar();
  shell->sidebar_widget()->LayoutRootViewIfNecessary();
  auto* scroll = shell->tab_viewport();
  EXPECT_GT(shell->tab_list_view()->height(), scroll->visible_rect().height());
  const int before = scroll->visible_rect().y();
  const ui::MouseEvent mouse(ui::EventType::kMousewheel, gfx::Point(20, 20),
      gfx::Point(20, 20), base::TimeTicks::Now(), 0, 0);
  const ui::MouseWheelEvent wheel(mouse, 0, -500);
  ASSERT_TRUE(scroll->view()->OnMouseWheel(wheel));
  ASSERT_TRUE(base::test::RunUntil([&] { return scroll->visible_rect().y() > before; }));
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->metal_sidebar_visible(); }));
  auto* row = shell->tab_button(28);
  ASSERT_TRUE(row);
  const auto screen_point = row->GetBoundsInScreen().CenterPoint();
  auto* root = shell->sidebar_widget()->GetRootView();
  auto point = screen_point;
  views::View::ConvertPointFromScreen(root, &point);
  auto* hit = root->GetEventHandlerForPoint(point);
  ASSERT_TRUE(hit == row || row->Contains(hit)) << hit->GetClassName();
  window_->SelectTab(0);
  auto* first = window_->contents("work");
  ui_controls::EnableUIControls();
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(window_->widget()->GetNativeWindow()));
  base::test::TestFuture<void> moved;
  ASSERT_TRUE(ui_controls::SendMouseMoveNotifyWhenDone(screen_point.x(), screen_point.y(),
      moved.GetCallback(), shell->sidebar_widget()->GetNativeWindow()));
  ASSERT_TRUE(moved.Wait());
  base::test::TestFuture<void> clicked;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(ui_controls::LEFT,
      ui_controls::DOWN | ui_controls::UP, clicked.GetCallback(), ui_controls::kNoAccelerator,
      shell->sidebar_widget()->GetNativeWindow()));
  ASSERT_TRUE(clicked.Wait());
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->contents("work") != first; }));
}
IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, ViewportProjectionTracksFractionalScrollAndClipping) {
  auto* shell = window_->shell();
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->appearance_ready(); }));
  shell->SetWorkspaceAnimations(false);
  const auto url = embedded_test_server()->GetURL("/storage_probe.html");
  for (int i = 0; i < 28; ++i) ASSERT_TRUE(window_->NavigateFromBar(url, true));
  window_->SelectTab(1);
  ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));
  ASSERT_TRUE(content::ExecJs(window_->contents("work"), "document.title = 'Viewport fractional 日本語';"));
  shell->CloseOverlays(true); shell->SetPinned(true); shell->ShowSidebar();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return shell->metal_sidebar_visible() && shell->metal_sidebar_contains_text_for_testing("Viewport fractional 日本語");
  }));
  auto* scroll = shell->tab_viewport(); auto* row = shell->tab_button(1);
  auto* viewport = shell->tab_list_view()->parent();
  auto submitted = [&](gfx::RectF rect, gfx::RectF clip) {
    const auto frame = shell->metal_sidebar_screen_bounds_for_testing();
    const auto native = viewport->GetBoundsInScreen();
    const gfx::Vector2dF origin(native.x() - frame.x(), native.y() - frame.y());
    rect.Offset(origin); clip.Offset(origin);
    return shell->metal_sidebar_contains_rect_for_testing(rect, clip);
  };
  auto local = views::View::ConvertRectToTarget(row, shell->tab_list_view(), gfx::RectF(row->GetLocalBounds()));
  const float max_offset = shell->tab_list_view()->height() - viewport->height();
  ASSERT_GT(max_offset, local.y() + 10);
  const auto point=shell->tab_scroll_view()->GetBoundsInScreen().CenterPoint()-shell->sidebar_widget()->GetWindowBoundsInScreen().origin().OffsetFromOrigin();const float delta=-(local.y()+9.25f-static_cast<float>(scroll->offset()));
  ui::ScrollEvent trackpad(ui::EventType::kScroll,point,base::TimeTicks::Now(),0,0,delta,0,delta,2,ui::EventMomentumPhase::NONE,ui::ScrollEventPhase::kUpdate);shell->sidebar_widget()->OnScrollEvent(&trackpad);EXPECT_DOUBLE_EQ(local.y()+9.25f,scroll->offset());
  auto expected = views::View::ConvertRectToTarget(row, viewport, gfx::RectF(row->GetLocalBounds()));
  auto clipped = expected; clipped.Intersect(gfx::RectF(viewport->GetLocalBounds()));
  EXPECT_FLOAT_EQ(-9.25f, expected.y()); EXPECT_FLOAT_EQ(24.75f, clipped.height());
  ASSERT_TRUE(base::test::RunUntil([&] {
    return submitted(expected, clipped);
  })) << "Native bounds/clip: " << expected.ToString() << " / " << clipped.ToString();
  const auto hit_point = views::View::ConvertPointToTarget(viewport,
      shell->sidebar_widget()->GetRootView(), gfx::ToRoundedPoint(clipped.CenterPoint()));
  auto* hit = shell->sidebar_widget()->GetRootView()->GetEventHandlerForPoint(hit_point);
  EXPECT_TRUE(hit == row || row->Contains(hit));
  // The Rust viewport clamps programmatic positions before scene projection.
  scroll->ScrollTo(1000000);
  ASSERT_TRUE(base::test::RunUntil([&] { return scroll->offset() == max_offset; }));
  window_->SelectTab(28); row = shell->tab_button(28);
  expected = views::View::ConvertRectToTarget(row, viewport, gfx::RectF(row->GetLocalBounds()));
  clipped = expected; clipped.Intersect(gfx::RectF(viewport->GetLocalBounds()));
  ASSERT_TRUE(base::test::RunUntil([&] { return submitted(expected, clipped); }));
  EXPECT_FALSE(clipped.IsEmpty());
}
// This geometry/controller regression does not synthesize OS input. It can
// validate list layout independently of foreground focus and presentation.
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, SidebarColumnLayoutTracksCollapsedFolders) {
  auto* shell = window_->shell();
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->appearance_ready(); }));
  shell->SetWorkspaceAnimations(false);
  ASSERT_TRUE(window_->CreatePinnedFolder("work", {}, "Parent"));
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->profiles_busy(); }));
  const auto parent = window_->pinned_items("work")[0].id;
  ASSERT_TRUE(window_->CreatePinnedFolder("work", parent, "Child"));
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->profiles_busy(); }));
  const auto url = embedded_test_server()->GetURL("/storage_probe.html");
  for (int i = 0; i < 28; ++i) ASSERT_TRUE(window_->NavigateFromBar(url, true));
  shell->CloseOverlays(true); shell->SetPinned(true); shell->ShowSidebar();
  for (int width : {200, 480}) {
    SCOPED_TRACE(width);
    shell->ResizeSidebar(width - shell->sidebar_width(), true);
    shell->sidebar_widget()->LayoutRootViewIfNecessary();
    auto* folder = SidebarNamedButton(shell->sidebar_view(), u"▾ Parent");
    auto* child = SidebarNamedButton(shell->sidebar_view(), u"▾ Child");
    ASSERT_TRUE(folder); ASSERT_TRUE(child);
    EXPECT_EQ(folder->GetBoundsInScreen().x() + 12, child->GetBoundsInScreen().x());
    const int expanded_height = shell->tab_list_view()->height();
    EXPECT_GT(expanded_height, shell->tab_viewport()->visible_rect().height());
    const int expanded_first = shell->tab_button(0)->GetBoundsInScreen().y();
    for (size_t index = 0; index < 29; ++index) {
      auto* row = shell->tab_button(index);
      ASSERT_TRUE(row);
      EXPECT_TRUE(row->parent()->GetLocalBounds().Contains(row->bounds()));
      if (index) EXPECT_EQ(shell->tab_button(index - 1)->bounds().bottom() + 2, row->y());
    }
    ActivateSidebarRow(folder);
    shell->sidebar_widget()->LayoutRootViewIfNecessary();
    EXPECT_FALSE(child->IsDrawn());
    // Views baseline: a 34px child, its empty nested group's 2px spacing,
    // and the parent column's 2px spacing disappear on collapse.
    EXPECT_EQ(expanded_height - 38, shell->tab_list_view()->height());
    EXPECT_EQ(expanded_first - 38, shell->tab_button(0)->GetBoundsInScreen().y());
    ActivateSidebarRow(folder);
    shell->sidebar_widget()->LayoutRootViewIfNecessary();
    EXPECT_TRUE(child->IsDrawn());
    EXPECT_EQ(expanded_height, shell->tab_list_view()->height());
    EXPECT_EQ(expanded_first, shell->tab_button(0)->GetBoundsInScreen().y());
    auto* first = shell->tab_button(0);
    ASSERT_TRUE(window_->Select("personal"));
    ASSERT_TRUE(window_->Select("work"));
    EXPECT_EQ(first, shell->tab_button(0));
  }
}
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, SidebarTreeStateSurvivesRebuildsAndSpaces) {
  auto* shell = window_->shell();
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->appearance_ready(); }));
  shell->SetWorkspaceAnimations(false);
  auto add = [&](const std::string& parent, const std::string& title) {
    EXPECT_TRUE(window_->CreatePinnedFolder("work", parent, title));
    EXPECT_TRUE(base::test::RunUntil([&] { return !window_->profiles_busy(); }));
    return window_->pinned_items("work").back().id;
  };
  const auto root_id = add({}, "Tree root");
  const auto child_id = add(root_id, "Tree child");
  add(child_id, "Tree grandchild");
  shell->CloseOverlays(true); shell->SetPinned(true); shell->ShowSidebar();
  shell->sidebar_widget()->LayoutRootViewIfNecessary();
  auto* root = SidebarNamedButton(shell->sidebar_view(), u"▾ Tree root");
  auto* child = SidebarNamedButton(shell->sidebar_view(), u"▾ Tree child");
  ASSERT_TRUE(root); ASSERT_TRUE(child);
  ActivateSidebarRow(child);
  ActivateSidebarRow(root);
  ASSERT_TRUE(window_->RenamePinnedFolder("work", child_id, "Tree renamed"));
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->profiles_busy(); }));
  // Reconstruction preserves both nested collapse states by item ID, not by
  // the old native group pointers or the folder's previous title.
  root = SidebarNamedButton(shell->sidebar_view(), u"▸ Tree root");
  child = SidebarNamedButton(shell->sidebar_view(), u"▸ Tree renamed");
  auto* grandchild = SidebarNamedButton(shell->sidebar_view(), u"▾ Tree grandchild");
  ASSERT_TRUE(root); ASSERT_TRUE(child); ASSERT_TRUE(grandchild);
  shell->sidebar_widget()->LayoutRootViewIfNecessary();
  EXPECT_TRUE(root->IsDrawn()); EXPECT_FALSE(child->IsDrawn()); EXPECT_FALSE(grandchild->IsDrawn());
  ASSERT_TRUE(window_->Select("personal"));
  ASSERT_TRUE(window_->CreatePinnedFolder("personal", {}, "Tree root"));
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->profiles_busy(); }));
  EXPECT_TRUE(SidebarNamedButton(shell->sidebar_view(), u"▾ Tree root"));
  ASSERT_TRUE(window_->Select("work"));
  shell->CloseOverlays(true); shell->ShowSidebar();
  root = SidebarNamedButton(shell->sidebar_view(), u"▸ Tree root");
  ASSERT_TRUE(root);
  ActivateSidebarRow(root);
  shell->sidebar_widget()->LayoutRootViewIfNecessary();
  EXPECT_TRUE(child->IsDrawn()); EXPECT_FALSE(grandchild->IsDrawn());
  ActivateSidebarRow(child);
  shell->sidebar_widget()->LayoutRootViewIfNecessary();
  EXPECT_TRUE(grandchild->IsDrawn());
  ActivateSidebarRow(root);
  // Adding a child through the existing dialog explicitly expands its parent
  // in the Rust state before the application reconstructs the native adapter.
  shell->RequestNewFolder("work", root_id);
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->folder_dialog(); }));
  ASSERT_TRUE(ReplaceFolderName(shell,u"Tree new child"));
  shell->folder_dialog()->widget_delegate()->AsDialogDelegate()->AcceptDialog();
  ASSERT_TRUE(base::test::RunUntil([&] { return !shell->folder_dialog() && !window_->profiles_busy(); }));
  shell->sidebar_widget()->LayoutRootViewIfNecessary();
  root = SidebarNamedButton(shell->sidebar_view(), u"▾ Tree root");
  auto* added = SidebarNamedButton(shell->sidebar_view(), u"▾ Tree new child");
  ASSERT_TRUE(root); ASSERT_TRUE(added); EXPECT_TRUE(added->IsDrawn());
  EXPECT_EQ(root->GetBoundsInScreen().x() + 12, added->GetBoundsInScreen().x());
}
// Shared contract for the Views baseline and the Rust container adapters.
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, SidebarSplitAndDeepFolderGeometry) {
  auto* shell = window_->shell();
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->appearance_ready(); }));
  shell->SetWorkspaceAnimations(false);
  std::string parent;
  for (int depth = 0; depth <= 10; ++depth) {
    ASSERT_TRUE(window_->CreatePinnedFolder("work", parent, "Depth " + base::NumberToString(depth)));
    ASSERT_TRUE(base::test::RunUntil([&] { return !window_->profiles_busy(); }));
    parent = window_->pinned_items("work").back().id;
  }
  ASSERT_TRUE(window_->NavigateFromBar(embedded_test_server()->GetURL("/storage_probe.html"), true));
  ASSERT_TRUE(window_->SplitTab(0));
  shell->CloseOverlays(true); shell->SetPinned(true); shell->ShowSidebar();
  auto check_pair = [&](int width) {
    SCOPED_TRACE(width);
    shell->ResizeSidebar(width - shell->sidebar_width(), true);
    shell->sidebar_widget()->LayoutRootViewIfNecessary();
    auto* first = shell->tab_button(0); auto* second = shell->tab_button(1);
    ASSERT_TRUE(first); ASSERT_TRUE(second);
    auto* pair = first->parent();
    ASSERT_EQ(pair, second->parent());
    EXPECT_EQ(gfx::Size(200, 46), pair->GetPreferredSize());
    EXPECT_EQ(46, pair->height());
    auto* left = first->x() < second->x() ? first : second;
    auto* right = left == first ? second : first;
    EXPECT_EQ(4, left->x()); EXPECT_EQ(4, left->y());
    EXPECT_EQ(38, left->height()); EXPECT_EQ(38, right->height());
    EXPECT_EQ(left->bounds().right() + 4, right->x());
    EXPECT_EQ(pair->width() - 4, right->bounds().right());
    EXPECT_LE(std::abs(left->width() - right->width()), 1);
    EXPECT_TRUE(pair->GetLocalBounds().Contains(left->bounds()));
    EXPECT_TRUE(pair->GetLocalBounds().Contains(right->bounds()));
    EXPECT_EQ(left, pair->GetEventHandlerForPoint(left->bounds().CenterPoint()));
    EXPECT_EQ(right, pair->GetEventHandlerForPoint(right->bounds().CenterPoint()));
  };
  for (int width : {200, 201, 247, 480}) {
    check_pair(width);
    auto* root = SidebarNamedButton(shell->sidebar_view(), u"▾ Depth 0");
    ASSERT_TRUE(root);
    for (int depth = 0; depth <= 10; ++depth) {
      auto* folder = SidebarNamedButton(shell->sidebar_view(), u"▾ Depth " + base::NumberToString16(depth));
      ASSERT_TRUE(folder);
      EXPECT_EQ(root->GetBoundsInScreen().x() + std::min(depth, 8) * 12, folder->GetBoundsInScreen().x());
      EXPECT_EQ(34, folder->height());
      EXPECT_EQ(folder->parent()->width() - std::min(depth, 8) * 12, folder->width());
    }
  }
  // A pinned half replaces its indented pin row rather than jumping to the
  // ordinary tab list. Collapsing its folder hides the entire pair.
  base::FilePath source;
  ASSERT_TRUE(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &source));
  window_->PreviewArcImport(source.AppendASCII("zitter/testdata/arc"));
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->import_ready(); }));
  ASSERT_TRUE(window_->ApplyArcImport());
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->profiles_busy(); }));
  ASSERT_TRUE(window_->Select("arc-space-space-a"));
  window_->OpenPinnedItem("tab-a");
  shell->CloseOverlays(true); shell->ShowSidebar();
  shell->sidebar_widget()->LayoutRootViewIfNecessary();
  auto* pin = SidebarNamedButton(shell->sidebar_view(), u"Przypięta A");
  ASSERT_TRUE(pin);
  const int pin_x = pin->GetBoundsInScreen().x();
  ASSERT_TRUE(window_->SplitTab(0));
  check_pair(480);
  auto* pair = shell->tab_button(1)->parent();
  EXPECT_EQ(pin_x, pair->GetBoundsInScreen().x());
  EXPECT_FALSE(pin->IsDrawn());
  auto* folder = SidebarNamedButton(shell->sidebar_view(), u"▾ Dokumenty");
  ASSERT_TRUE(folder);
  ActivateSidebarRow(folder);
  shell->sidebar_widget()->LayoutRootViewIfNecessary();
  EXPECT_FALSE(pair->IsDrawn());
  ActivateSidebarRow(folder);
  shell->sidebar_widget()->LayoutRootViewIfNecessary();
  EXPECT_TRUE(pair->IsDrawn());
  for (int width : {200, 201, 247, 480}) check_pair(width);
  window_->ToggleSplit();
  shell->sidebar_widget()->LayoutRootViewIfNecessary();
  EXPECT_FALSE(shell->tab_button(1));
  EXPECT_TRUE(pin->IsDrawn());
  EXPECT_EQ(pin_x, pin->GetBoundsInScreen().x());
}
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, SidebarPinnedPairFollowsPaneOrderAndFolders) {
  auto* shell = window_->shell();
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->appearance_ready(); }));
  shell->SetWorkspaceAnimations(false);
  base::FilePath source;
  ASSERT_TRUE(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &source));
  window_->PreviewArcImport(source.AppendASCII("zitter/testdata/arc"));
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->import_ready(); }));
  ASSERT_TRUE(window_->ApplyArcImport());
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->profiles_busy(); }));
  ASSERT_TRUE(window_->Select("arc-space-space-a"));
  shell->CloseOverlays(true); shell->SetPinned(true); shell->ShowSidebar();
  shell->sidebar_widget()->LayoutRootViewIfNecessary();
  auto* a = SidebarNamedButton(shell->sidebar_view(), u"Przypięta A");
  auto* b = SidebarNamedButton(shell->sidebar_view(), u"Przypięta B");
  ASSERT_TRUE(a); ASSERT_TRUE(b);
  const int a_x = a->GetBoundsInScreen().x(), b_x = b->GetBoundsInScreen().x();
  EXPECT_EQ(a_x + 12, b_x);
  window_->OpenPinnedItem("tab-a"); auto* page_a = window_->contents("arc-space-space-a");
  window_->OpenPinnedItem("tab-b"); auto* page_b = window_->contents("arc-space-space-a");
  ASSERT_EQ(3u, window_->tab_count("arc-space-space-a"));
  ASSERT_TRUE(window_->SplitTab(1));  // B left, A right: anchor in B's nested folder.
  shell->sidebar_widget()->LayoutRootViewIfNecessary();
  ASSERT_TRUE(shell->tab_button(1)); ASSERT_TRUE(shell->tab_button(2));
  EXPECT_EQ(page_b, window_->pane_contents(false)); EXPECT_EQ(page_a, window_->pane_contents(true));
  EXPECT_EQ(b_x, shell->tab_button(2)->parent()->GetBoundsInScreen().x());
  EXPECT_LT(shell->tab_button(2)->x(), shell->tab_button(1)->x());
  EXPECT_FALSE(a->IsDrawn()); EXPECT_FALSE(b->IsDrawn());
  window_->SwapSplit();
  shell->sidebar_widget()->LayoutRootViewIfNecessary();
  EXPECT_EQ(page_a, window_->pane_contents(false)); EXPECT_EQ(page_b, window_->pane_contents(true));
  EXPECT_EQ(a_x, shell->tab_button(1)->parent()->GetBoundsInScreen().x());
  EXPECT_LT(shell->tab_button(1)->x(), shell->tab_button(2)->x());
  auto* docs = SidebarNamedButton(shell->sidebar_view(), u"▾ Dokumenty");
  ASSERT_TRUE(docs); ActivateSidebarRow(docs);
  window_->SwapSplit();
  shell->sidebar_widget()->LayoutRootViewIfNecessary();
  EXPECT_FALSE(shell->tab_button(2)->IsDrawn());
  ActivateSidebarRow(docs);
  shell->sidebar_widget()->LayoutRootViewIfNecessary();
  EXPECT_TRUE(shell->tab_button(2)->IsDrawn());
  auto* nested = SidebarNamedButton(shell->sidebar_view(), u"▾ Projekt");
  ASSERT_TRUE(nested); ActivateSidebarRow(nested);
  ASSERT_TRUE(window_->Select("arc-space-space-b")); ASSERT_TRUE(window_->Select("arc-space-space-a"));
  shell->CloseOverlays(true); shell->ShowSidebar(); shell->sidebar_widget()->LayoutRootViewIfNecessary();
  EXPECT_FALSE(shell->tab_button(2)->IsDrawn());
  EXPECT_TRUE(window_->split()); EXPECT_EQ(page_b, window_->pane_contents(false));
  ActivateSidebarRow(nested);
  shell->sidebar_widget()->LayoutRootViewIfNecessary();
  EXPECT_TRUE(shell->tab_button(2)->IsDrawn());
  EXPECT_EQ(b_x, shell->tab_button(2)->parent()->GetBoundsInScreen().x());
  window_->Unsplit(1);
  shell->sidebar_widget()->LayoutRootViewIfNecessary();
  EXPECT_FALSE(window_->split()); EXPECT_FALSE(shell->tab_button(1)); EXPECT_FALSE(shell->tab_button(2));
  EXPECT_TRUE(a->IsDrawn()); EXPECT_TRUE(b->IsDrawn());
  EXPECT_EQ(a_x, a->GetBoundsInScreen().x()); EXPECT_EQ(b_x, b->GetBoundsInScreen().x());
}
// Exercise Rust-owned gesture state and row geometry through native events,
// rather than invoking the command callbacks or asserting implementation data.
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, NativeDragAndCloseAtWidthLimits) {
  auto* shell = window_->shell();
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->appearance_ready(); }));
  shell->SetWorkspaceAnimations(false);
  const auto url = embedded_test_server()->GetURL("/storage_probe.html");
  ASSERT_TRUE(window_->NavigateFromBar(url, true));
  auto* dragged = window_->contents("work");
  ASSERT_TRUE(window_->NavigateFromBar(url, true));
  auto* original = window_->contents("work");
  shell->CloseOverlays(true); shell->SetPinned(true); shell->ShowSidebar();
  shell->sidebar_widget()->LayoutRootViewIfNecessary();
  if (base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII("zitter-sidebar") == "rust-metal")
    ASSERT_TRUE(base::test::RunUntil([&] { return shell->metal_sidebar_visible(); }));
  ui_controls::EnableUIControls();
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(window_->widget()->GetNativeWindow()));
  const auto sidebar_window = shell->sidebar_widget()->GetNativeWindow();
  auto move = [&](gfx::Point p) {
    base::test::TestFuture<void> done;
    return ui_controls::SendMouseMoveNotifyWhenDone(p.x(), p.y(), done.GetCallback(),
        sidebar_window) && done.Wait();
  };
  auto mouse = [&](int state) {
    base::test::TestFuture<void> done;
    return ui_controls::SendMouseEventsNotifyWhenDone(ui_controls::LEFT, state,
        done.GetCallback(), ui_controls::kNoAccelerator, sidebar_window) && done.Wait();
  };
  ASSERT_TRUE(move(shell->tab_button(1)->GetBoundsInScreen().CenterPoint()));
  ASSERT_TRUE(mouse(ui_controls::DOWN));
  const auto content = shell->content_host()->GetBoundsInScreen();
  ASSERT_TRUE(move(gfx::Point(content.right() - 60, content.CenterPoint().y())));
  ASSERT_TRUE(mouse(ui_controls::UP));
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->split(); }));
  EXPECT_EQ(3u, window_->tab_count("work"));
  EXPECT_EQ(original, window_->pane_contents(false));
  EXPECT_EQ(dragged, window_->pane_contents(true));
  // Close the hovered split tile at minimum width, then an ordinary row at
  // maximum width. Both clicks must hit the actual child affordance.
  for (const int width : {200, 480}) {
    SCOPED_TRACE(width);
    shell->ResizeSidebar(width - shell->sidebar_width(), true);
    EXPECT_EQ(width, shell->sidebar_width());
    if (width == 480) window_->SelectTab(0);
    shell->sidebar_widget()->LayoutRootViewIfNecessary();
    auto* row = shell->tab_button(width == 200 ? 1 : 0);
    ASSERT_TRUE(row);
    ASSERT_TRUE(move(row->GetBoundsInScreen().CenterPoint()));
    auto* close = SidebarNamedButton(row, u"Zamknij kartę");
    ASSERT_TRUE(close);
    ASSERT_TRUE(base::test::RunUntil([&] { return close->IsDrawn(); }));
    const size_t count = window_->tab_count("work");
    ASSERT_TRUE(move(close->GetBoundsInScreen().CenterPoint()));
    ASSERT_TRUE(close->IsDrawn()) << "Entering the close child must preserve row hover";
    auto hit_point = close->GetBoundsInScreen().CenterPoint();
    auto* root = shell->sidebar_widget()->GetRootView();
    views::View::ConvertPointFromScreen(root, &hit_point);
    EXPECT_EQ(close, root->GetEventHandlerForPoint(hit_point));
    ASSERT_TRUE(mouse(ui_controls::DOWN | ui_controls::UP));
    ASSERT_TRUE(base::test::RunUntil([&] { return window_->tab_count("work") == count - 1; }));
    EXPECT_FALSE(window_->split());
  }
  EXPECT_EQ(original, window_->contents("work"));
  EXPECT_FALSE(SidebarNamedButton(shell->tab_button(0), u"Zamknij kartę"));
}
IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, RustRowInputCancellationAndPlatformKeys) {
  auto* shell = window_->shell();
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->appearance_ready(); }));
  shell->SetWorkspaceAnimations(false);
  auto* first = window_->contents("work");
  ASSERT_TRUE(window_->NavigateFromBar(embedded_test_server()->GetURL("/storage_probe.html"), true));
  auto* second = window_->contents("work");
  ASSERT_TRUE(content::WaitForLoadStop(second));
  shell->CloseOverlays(true); shell->SetPinned(true); shell->ShowSidebar();
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->tab_button(0) && !shell->tab_button(0)->size().IsEmpty(); }));
  auto* row = shell->tab_button(0);
  const auto point = row->GetLocalBounds().CenterPoint();
  ui::MouseEvent press(ui::EventType::kMousePressed, point, point, base::TimeTicks::Now(),
      ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON);
  ui::MouseEvent release(ui::EventType::kMouseReleased, point, point, base::TimeTicks::Now(),
      ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON);
  ASSERT_TRUE(row->OnMousePressed(press)); row->OnMouseCaptureLost(); row->OnMouseReleased(release);
  EXPECT_EQ(second, window_->contents("work"));
  ASSERT_TRUE(row->OnMousePressed(press));
  const gfx::Point outside(-10, -10);
  ui::MouseEvent outside_release(ui::EventType::kMouseReleased, outside, outside,
      base::TimeTicks::Now(), ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON);
  row->OnMouseReleased(outside_release);
  EXPECT_EQ(second, window_->contents("work"));
  row->SetEnabled(false);
  row->OnMousePressed(press); row->OnMouseReleased(release);
  EXPECT_EQ(second, window_->contents("work"));
  ui::KeyEvent space_down(ui::EventType::kKeyPressed, ui::VKEY_SPACE, 0);
  EXPECT_FALSE(row->OnKeyPressed(space_down));
  row->SetEnabled(true);
  ui::KeyEvent enter(ui::EventType::kKeyPressed, ui::VKEY_RETURN, 0);
  EXPECT_FALSE(row->OnKeyPressed(enter));  // Mac Return activates the default action, not this row.
  EXPECT_EQ(second, window_->contents("work"));
  EXPECT_TRUE(row->OnKeyPressed(space_down));
  EXPECT_EQ(first, window_->contents("work"));
  window_->SelectTab(1);
  ui::KeyEvent space_up(ui::EventType::kKeyReleased, ui::VKEY_SPACE, 0);
  EXPECT_FALSE(row->OnKeyReleased(space_up));  // Mac Space already activated on key-down.
  EXPECT_EQ(second, window_->contents("work"));
  ASSERT_TRUE(row->OnMousePressed(press)); row->OnMouseReleased(release);
  EXPECT_EQ(first, window_->contents("work"));
}
IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, SplitTitleUpdatesWithoutNativePaint) {
  auto* shell = window_->shell();
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->appearance_ready(); }));
  shell->SetWorkspaceAnimations(false);
  ASSERT_TRUE(window_->NavigateFromBar(embedded_test_server()->GetURL("/storage_probe.html"), true));
  auto* page = window_->contents("work");
  ASSERT_TRUE(content::WaitForLoadStop(page));
  ASSERT_TRUE(content::ExecJs(page, "document.title = 'Split initial 日本語';"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return shell->tab_button(1)->GetTooltipText() == u"Split initial 日本語";
  }));
  ASSERT_TRUE(window_->SplitTab(0));
  shell->CloseOverlays(true); shell->SetPinned(true); shell->ShowSidebar();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return shell->metal_sidebar_visible() &&
        shell->metal_sidebar_contains_text_for_testing("Split initial 日本語");
  }));
  auto* row = shell->tab_button(1);
  const auto gpu_before = shell->metal_sidebar_frame_count();
  // A paint lock deliberately stops descendant invalidations before they reach
  // the native list paint observer. Model-to-scene invalidation must still work.
  views::ScopedPaintLock paint_lock(shell->sidebar_view());
  ASSERT_TRUE(content::ExecJs(page, "document.title = 'Split updated Żółć 🦀';"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return row->GetTooltipText() == u"Split updated Żółć 🦀";
  }));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return shell->metal_sidebar_contains_text_for_testing("Split updated Żółć 🦀");
  }));
  EXPECT_FALSE(shell->metal_sidebar_contains_text_for_testing("Split initial 日本語"));
  EXPECT_GT(shell->metal_sidebar_frame_count(), gpu_before);
}
IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, MetalOwnsRowTextAndSkipsNativePainting) {
  gfx::ScopedAnimationDurationScaleMode duration(gfx::ScopedAnimationDurationScaleMode::NORMAL_DURATION);
  const bool original_motion = gfx::Animation::PrefersReducedMotion();
  base::ScopedClosureRunner restore_motion(base::BindOnce(
      &gfx::Animation::SetPrefersReducedMotionForTesting, original_motion));
  gfx::Animation::SetPrefersReducedMotionForTesting(false);
  auto* shell = window_->shell();
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->appearance_ready(); }));
  shell->SetWorkspaceAnimations(false);
  ASSERT_TRUE(window_->NavigateFromBar(embedded_test_server()->GetURL("/storage_probe.html"), true));
  ASSERT_TRUE(content::WaitForLoadStop(window_->contents("work")));
  shell->CloseOverlays(true); shell->SetPinned(true); shell->ShowSidebar();
  ui_controls::EnableUIControls();
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(window_->widget()->GetNativeWindow()));
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->metal_sidebar_visible(); }));
  // Newly added rows must lay out without a window resize or a Label's
  // incidental preferred-size notification.
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !shell->tab_button(0)->size().IsEmpty() && !shell->tab_button(1)->size().IsEmpty();
  }));
  auto no_native_text = [&](auto&& self, views::View* view) -> void {
    EXPECT_FALSE(views::AsViewClass<views::Label>(view));
    for (const auto& child : view->children()) self(self, child.get());
  };
  no_native_text(no_native_text, shell->tab_button(0));
  no_native_text(no_native_text, shell->tab_button(1));
  const auto native_before = shell->native_sidebar_paint_count();
  const auto metal_before = shell->metal_sidebar_paint_count();
  // Deterministically request a native paint pass even if the compositor has
  // culled the now-empty native display list. It must skip every child.
  const auto bitmap = views::test::PaintViewToBitmap(shell->tab_list_view());
  EXPECT_FALSE(bitmap.drawsNothing());
  EXPECT_GT(shell->metal_sidebar_paint_count(), metal_before);
  EXPECT_EQ(native_before, shell->native_sidebar_paint_count());
  const auto gpu_before = shell->metal_sidebar_frame_count();
  ASSERT_TRUE(content::ExecJs(window_->contents("work"), "document.title = 'GPU · Zażółć 日本語 🦀';"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return shell->tab_button(1)->GetTooltipText() == u"GPU · Zażółć 日本語 🦀";
  }));
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->metal_sidebar_frame_count() > gpu_before; }))
      << "GPU submissions: " << shell->metal_sidebar_frame_count() << "/" << gpu_before
      << "; native/skipped paints: " << shell->native_sidebar_paint_count() << "/"
      << shell->metal_sidebar_paint_count() << "; visible: " << shell->metal_sidebar_visible()
      << "; drawn: " << shell->tab_button(1)->IsDrawn()
      << "; row: " << shell->tab_button(1)->GetBoundsInScreen().ToString()
      << "; viewport: " << shell->tab_list_view()->parent()->GetBoundsInScreen().ToString();
  ui::AXNodeData accessible;
  shell->tab_button(1)->GetViewAccessibility().GetAccessibleNodeData(&accessible);
  EXPECT_EQ(u"GPU · Zażółć 日本語 🦀",
      accessible.GetString16Attribute(ax::mojom::StringAttribute::kName));
  EXPECT_EQ(native_before, shell->native_sidebar_paint_count());
  // The Metal transition now presents a captured GPU scene. It must preserve
  // the GPU-only paint path without the native snapshot work it replaced.
  shell->SetWorkspaceAnimations(true);
  ASSERT_TRUE(window_->Select("personal"));
  EXPECT_TRUE(shell->workspace_switch_metal_visible());
  EXPECT_EQ(native_before,shell->native_sidebar_paint_count());
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !shell->workspace_animation_running() && shell->metal_sidebar_visible();
  }));
  const auto metal_after_animation = shell->metal_sidebar_paint_count();
  views::test::PaintViewToBitmap(shell->tab_list_view());
  EXPECT_GT(shell->metal_sidebar_paint_count(), metal_after_animation);
  ASSERT_TRUE(window_->Select("work"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !shell->workspace_animation_running() && shell->metal_sidebar_visible();
  }));
  EXPECT_EQ(u"GPU · Zażółć 日本語 🦀", shell->tab_button(1)->GetTooltipText());
  const auto gpu_before_close = shell->metal_sidebar_frame_count();
  window_->RunShortcut("close_tab");
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window_->tab_count("work") == 1 && shell->tab_button(0) &&
        !shell->tab_button(0)->size().IsEmpty() &&
        shell->metal_sidebar_frame_count() > gpu_before_close;
  }));
}
IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, MetalReturnsAfterWorkspaceAnimation) {
  auto* shell = window_->shell();
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->appearance_ready(); }));
  shell->SetWorkspaceAnimations(true);
  shell->ShowSidebar();
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->metal_sidebar_visible()&&shell->metal_sidebar_grip_visible(); }));
  ASSERT_TRUE(window_->Select("personal"));
  if(shell->workspace_animation_running())EXPECT_FALSE(shell->metal_sidebar_grip_visible());
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !shell->workspace_animation_running() && shell->metal_sidebar_visible()&&shell->metal_sidebar_grip_visible();
  }));
  ASSERT_TRUE(window_->Select("work"));
  if(shell->workspace_animation_running())EXPECT_FALSE(shell->metal_sidebar_grip_visible());
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !shell->workspace_animation_running() && shell->metal_sidebar_visible()&&shell->metal_sidebar_grip_visible();
  }));
}
IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, LiveTabsKeyboardFoldersAndSplit) {
  auto* shell = window_->shell();
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->appearance_ready(); }));
  shell->SetWorkspaceAnimations(false);
  ASSERT_TRUE(content::NavigateToURL(window_->contents("work"), embedded_test_server()->GetURL("/storage_probe.html")));
  auto* first = window_->contents("work");
  ASSERT_TRUE(content::ExecJs(first, "document.title = 'Zażółć 日本語 🦀';"));
  ASSERT_TRUE(window_->NavigateFromBar(embedded_test_server()->GetURL("/storage_probe.html?metal"), true));
  auto* second = window_->contents("work");
  ASSERT_TRUE(content::WaitForLoadStop(second));
  shell->CloseOverlays(true); shell->ShowSidebar();
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->metal_sidebar_visible(); }));
  shell->ToggleSidebar();
  ASSERT_TRUE(base::test::RunUntil([&] { return !shell->metal_sidebar_visible(); }));
  shell->ShowSidebar();
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->metal_sidebar_visible(); }));
  shell->SetPinned(true);
  ASSERT_TRUE(shell->tab_button(0));
  EXPECT_EQ(u"Zażółć 日本語 🦀", shell->tab_button(0)->GetTooltipText());
  ui_controls::EnableUIControls();
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(window_->widget()->GetNativeWindow()));
  // Physical key dispatch traverses the existing page/accelerator path and the
  // Rust tab-cycle reducer, rather than directly calling select_tab callbacks.
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(window_->widget()->GetNativeWindow(),
      ui::VKEY_TAB, true, false, false, false));
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->contents("work") == first; }));
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(window_->widget()->GetNativeWindow(),
      ui::VKEY_TAB, true, true, false, false));
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->contents("work") == second; }));
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(window_->widget()->GetNativeWindow(),
      ui::VKEY_L, false, false, false, true));
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->address_visible(); }));
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(shell->address_widget()->GetNativeWindow(),
      ui::VKEY_ESCAPE, false, false, false, false));
  ASSERT_TRUE(base::test::RunUntil([&] { return !shell->address_visible(); }));
  const auto point = shell->tab_button(0)->GetBoundsInScreen().CenterPoint();
  base::test::TestFuture<void> moved;
  ASSERT_TRUE(ui_controls::SendMouseMoveNotifyWhenDone(point.x(), point.y(), moved.GetCallback(),
      shell->sidebar_widget()->GetNativeWindow()));
  ASSERT_TRUE(moved.Wait());
  base::test::TestFuture<void> clicked;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(ui_controls::LEFT,
      ui_controls::DOWN | ui_controls::UP, clicked.GetCallback(), ui_controls::kNoAccelerator,
      shell->sidebar_widget()->GetNativeWindow()));
  ASSERT_TRUE(clicked.Wait());
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->contents("work") == first; }));
  ASSERT_TRUE(window_->CreatePinnedFolder("work", {}, "Projekty 🦀"));
  ASSERT_TRUE(base::test::RunUntil([&] { return !window_->profiles_busy(); }));
  auto* folder = SidebarNamedButton(shell->sidebar_view(), u"▾ Projekty 🦀");
  ASSERT_TRUE(folder);
  EXPECT_EQ(shell, folder->context_menu_controller());
  ActivateSidebarRow(folder);
  EXPECT_EQ(u"▸ Projekty 🦀", folder->GetTooltipText());
  ActivateSidebarRow(folder);
  window_->SelectTab(0);
  ASSERT_TRUE(window_->SplitTab(1));
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->metal_sidebar_visible(); }));
  EXPECT_EQ(shell->tab_button(0)->parent(), shell->tab_button(1)->parent());
  ActivateSidebarRow(shell->tab_button(1));
  EXPECT_EQ(second, window_->contents("work"));
  EXPECT_TRUE(window_->split());
  shell->ResizeSidebar(90, true);
  window_->SwapSplit(); window_->SetSplitVertical(true);
  EXPECT_TRUE(window_->split_vertical());
  ASSERT_TRUE(window_->Select("personal"));
  ASSERT_TRUE(window_->Select("work"));
  EXPECT_TRUE(window_->split());
  EXPECT_EQ(second, window_->contents("work"));
  window_->RunShortcut("close_tab");
  ASSERT_TRUE(base::test::RunUntil([&] { return window_->tab_count("work") == 1; }));
  EXPECT_EQ(first, window_->contents("work"));
  EXPECT_FALSE(window_->split());
  ASSERT_TRUE(base::test::RunUntil([&] { return shell->metal_sidebar_visible(); }));
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

namespace zitter::prototype {
#if BUILDFLAG(IS_MAC)
class DeferredEditorClipboard final:public ui::TestClipboard {
 public:
  void ReadText(ui::ClipboardBuffer buffer,const std::optional<ui::DataTransferEndpoint>& destination,ReadTextCallback callback) const override {
    ui::TestClipboard::ReadText(buffer,destination,base::BindOnce([](ReadTextCallback callback,std::u16string text){base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,base::BindOnce(std::move(callback),std::move(text)));},std::move(callback)));
  }
};
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, NameEditorsTypingUndoCompositionAndPersistence) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->SetWorkspaceAnimations(false);shell->OpenWorkspaceSettings();
  auto* field=shell->workspace_input();ASSERT_TRUE(field);auto* view=field->view();
  const bool metal=base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII("zitter-sidebar")=="rust-metal";
  EXPECT_EQ(metal,field->rust_editor());EXPECT_EQ(!metal,views::AsViewClass<views::Textfield>(view)!=nullptr);
  if(metal){EXPECT_TRUE(view->children().empty());ASSERT_TRUE(base::test::RunUntil([&]{return field->metal_visible();}));}
  ui_controls::EnableUIControls();ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->settings_widget()->GetNativeWindow()));view->RequestFocus();ASSERT_TRUE(base::test::RunUntil([&]{return view->HasFocus();}));
  auto key=[&](ui::KeyboardCode code,bool shift=false,bool command=false){return ui_test_utils::SendKeyPressToWindowSync(shell->settings_widget()->GetNativeWindow(),code,false,shift,false,command);};
  field->SetText(u"Praca");ASSERT_TRUE(key(ui::VKEY_A,false,true));ASSERT_TRUE(key(ui::VKEY_X));ASSERT_TRUE(base::test::RunUntil([&]{return field->GetText()==u"x";}));
  ASSERT_TRUE(key(ui::VKEY_Z,false,true));ASSERT_TRUE(base::test::RunUntil([&]{return field->GetText()==u"Praca";}));
  field->SelectRange(gfx::Range(0,field->GetText().size()));auto* client=field->input_client();ASSERT_EQ(client,view->GetInputMethod()->GetTextInputClient());
  client->InsertText(u"a\u0301🦀",ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);EXPECT_EQ(u"a\u0301🦀",field->GetText());
  ui::KeyEvent back(ui::EventType::kKeyPressed,ui::VKEY_BACK,ui::EF_NONE);EXPECT_TRUE(view->OnKeyPressed(back));EXPECT_EQ(u"a\u0301",field->GetText());EXPECT_TRUE(view->OnKeyPressed(back));EXPECT_TRUE(field->GetText().empty());
  field->SetText(u"alpha");field->SelectRange(gfx::Range(5));ui::CompositionText marked;marked.text=u"にほん";marked.selection=gfx::Range(marked.text.size());marked.ime_text_spans.emplace_back(ui::ImeTextSpan::Type::kComposition,0,marked.text.size(),ui::ImeTextSpan::Thickness::kThick);
  client->SetCompositionText(marked);ASSERT_TRUE(field->IsIMEComposing());gfx::Range range;ASSERT_TRUE(client->GetCompositionTextRange(&range));EXPECT_EQ(gfx::Range(5,8),range);gfx::Rect character;ASSERT_TRUE(client->GetCompositionCharacterBounds(1,&character));EXPECT_FALSE(character.IsEmpty());EXPECT_TRUE(view->GetBoundsInScreen().Intersects(character));
  EXPECT_EQ(3u,client->ConfirmCompositionText(false));EXPECT_FALSE(field->IsIMEComposing());EXPECT_EQ(u"alphaにほん",field->GetText());ASSERT_TRUE(key(ui::VKEY_Z,false,true));ASSERT_TRUE(base::test::RunUntil([&]{return field->GetText()==u"alpha";}));
  ui::AXNodeData node;field->GetViewAccessibility().GetAccessibleNodeData(&node);EXPECT_EQ(ax::mojom::Role::kTextField,node.role);EXPECT_EQ(u"alpha",node.GetString16Attribute(ax::mojom::StringAttribute::kValue));
  std::u16string long_text(1000,u'x');long_text+=u"🦀";field->SetText(long_text);const auto caret=client->GetCaretBounds();EXPECT_GE(caret.x(),view->GetBoundsInScreen().x());EXPECT_LE(caret.right(),view->GetBoundsInScreen().right());
  field->SetText(u"Workspace 日本語");auto* save=FindButton(shell->workspace_panel_for_testing(),u"Zapisz nazwę i profil");ASSERT_TRUE(save);ClickControl(save);EXPECT_EQ("Workspace 日本語",window_->workspaces()[0].name);ASSERT_TRUE(base::test::RunUntil([&]{return window_->configuration_saved();}));
  shell->OpenProfiles("work-profile");auto* profile=shell->profile_name_input();ASSERT_TRUE(profile);EXPECT_EQ(metal,profile->rust_editor());profile->SetText(u"Profil 日本語");auto* rename=FindButton(shell->profile_panel_for_testing(),u"Zapisz nazwę profilu");ASSERT_TRUE(rename&&rename->GetEnabled());ClickControl(rename);ASSERT_TRUE(base::test::RunUntil([&]{return !window_->profiles_busy();}));EXPECT_EQ("Profil 日本語",window_->profiles()[0].name);
}

IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, NameEditorsAdmissionHideResetAndClipboard) {
  auto old_clipboard=ui::Clipboard::TakeForCurrentThread();ui::Clipboard::SetClipboardForCurrentThread(std::make_unique<DeferredEditorClipboard>());
  base::ScopedClosureRunner restore_clipboard(base::BindOnce([](std::unique_ptr<ui::Clipboard> old){ui::Clipboard::DestroyClipboardForCurrentThread();if(old)ui::Clipboard::SetClipboardForCurrentThread(std::move(old));},std::move(old_clipboard)));

  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->OpenWorkspaceSettings();auto* field=shell->workspace_input();ASSERT_TRUE(field->rust_editor());auto* view=field->view();auto* client=field->input_client();
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->settings_widget()->GetNativeWindow()));view->RequestFocus();ASSERT_TRUE(base::test::RunUntil([&]{return view->HasFocus();}));
  field->SetText(u"original");field->SelectRange(gfx::Range(0,8));ui::CompositionText marked;marked.text=u"仮";marked.selection=gfx::Range(1);client->SetCompositionText(marked);ASSERT_TRUE(field->IsIMEComposing());client->ClearCompositionText();EXPECT_EQ(u"original",field->GetText());EXPECT_EQ(gfx::Range(0,8),field->GetSelectedRange());
  field->SetReadOnly(true);client->InsertText(u"blocked",ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);EXPECT_EQ(u"original",field->GetText());EXPECT_EQ(ui::TEXT_INPUT_TYPE_NONE,client->GetTextInputType());field->SetReadOnly(false);
  client->SetCompositionText(marked);ASSERT_TRUE(field->IsIMEComposing());view->SetEnabled(false);EXPECT_FALSE(field->IsIMEComposing());EXPECT_EQ(u"original",field->GetText());view->SetEnabled(true);
  client->SetCompositionText(marked);ASSERT_TRUE(field->IsIMEComposing());shell->settings_widget()->Hide();EXPECT_FALSE(field->IsIMEComposing());EXPECT_EQ(u"original",field->GetText());EXPECT_FALSE(field->metal_visible());client->InsertText(u"hidden",ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);EXPECT_EQ(u"original",field->GetText());
  shell->OpenWorkspaceSettings();ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->settings_widget()->GetNativeWindow()));view->RequestFocus();field->SetText(u"one two");field->SelectRange(gfx::Range(0,3));
  ui::KeyEvent copy(ui::EventType::kKeyPressed,ui::VKEY_C,ui::EF_COMMAND_DOWN);ASSERT_TRUE(view->OnKeyPressed(copy));field->SelectRange(gfx::Range(7));ui::KeyEvent paste(ui::EventType::kKeyPressed,ui::VKEY_V,ui::EF_COMMAND_DOWN);ASSERT_TRUE(view->OnKeyPressed(paste));ASSERT_TRUE(base::test::RunUntil([&]{return field->GetText()==u"one twoone";}));
  ASSERT_TRUE(base::test::RunUntil([&]{return field->scene_contains("one twoone");}));
  // A paste callback captured before a programmatic context reset is rejected.
  ASSERT_TRUE(view->OnKeyPressed(paste));field->SetText(u"reset");base::RunLoop().RunUntilIdle();EXPECT_EQ(u"reset",field->GetText());
  ui::AXActionData action;action.action=ax::mojom::Action::kSetSelection;action.anchor_offset=0;action.focus_offset=5;ASSERT_TRUE(view->HandleAccessibleAction(action));EXPECT_EQ(gfx::Range(0,5),field->GetSelectedRange());
  action.action=ax::mojom::Action::kReplaceSelectedText;action.value="AX 日本語";ASSERT_TRUE(view->HandleAccessibleAction(action));EXPECT_EQ(u"AX 日本語",field->GetText());
}
IN_PROC_BROWSER_TEST_F(ZitterMetalSidebarBrowserTest, NameEditorMouseDropAndNativeFallback) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->OpenWorkspaceSettings();auto* field=shell->workspace_input();auto* view=field->view();ASSERT_TRUE(field->rust_editor());
  ui_controls::EnableUIControls();ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->settings_widget()->GetNativeWindow()));field->SetText(u"abcdef");field->SelectRange(gfx::Range(0,6));
  const auto target=view->GetBoundsInScreen();const gfx::Point point(target.right()-10,target.CenterPoint().y());base::test::TestFuture<void> moved;ASSERT_TRUE(ui_controls::SendMouseMoveNotifyWhenDone(point.x(),point.y(),moved.GetCallback(),shell->settings_widget()->GetNativeWindow()));ASSERT_TRUE(moved.Wait());
  base::test::TestFuture<void> clicked;ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(ui_controls::LEFT,ui_controls::DOWN|ui_controls::UP,clicked.GetCallback(),ui_controls::kNoAccelerator,shell->settings_widget()->GetNativeWindow()));ASSERT_TRUE(clicked.Wait());EXPECT_TRUE(view->HasFocus());EXPECT_EQ(gfx::Range(6),field->GetSelectedRange());
  field->SelectRange(gfx::Range(1,3));ui::OSExchangeData dragged;ASSERT_TRUE(view->drag_controller());view->drag_controller()->WriteDragDataForView(view,gfx::Point(15,16),&dragged);ASSERT_TRUE(dragged.GetString());EXPECT_EQ(u"bc",*dragged.GetString());
  ui::DropTargetEvent drop(dragged,gfx::PointF(view->width()-10,16),gfx::PointF(view->width()-10,16),ui::DragDropTypes::DRAG_MOVE);ASSERT_TRUE(view->CanDrop(dragged));auto callback=view->GetDropCallback(drop);ASSERT_TRUE(callback);ui::mojom::DragOperation operation=ui::mojom::DragOperation::kNone;std::move(callback).Run(drop,operation,nullptr);EXPECT_EQ(ui::mojom::DragOperation::kMove,operation);EXPECT_EQ(u"adefbc",field->GetText());view->OnDragDone();
  ui::KeyEvent undo(ui::EventType::kKeyPressed,ui::VKEY_Z,ui::EF_COMMAND_DOWN);ASSERT_TRUE(view->OnKeyPressed(undo));EXPECT_EQ(u"abcdef",field->GetText());EXPECT_EQ(gfx::Range(1,3),field->GetSelectedRange());
  // Mutation after drag start invalidates the payload/selection transaction.
  view->drag_controller()->WriteDragDataForView(view,gfx::Point(15,16),&dragged);field->input_client()->InsertText(u"other",ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);EXPECT_FALSE(view->GetDropCallback(drop));view->OnDragDone();
  ui::OSExchangeData external;external.SetString(u"日本語");ui::DropTargetEvent incoming(external,gfx::PointF(view->width()-10,16),gfx::PointF(view->width()-10,16),ui::DragDropTypes::DRAG_COPY);auto late=view->GetDropCallback(incoming);ASSERT_TRUE(late);field->SetText(u"new context");operation=ui::mojom::DragOperation::kNone;std::move(late).Run(incoming,operation,nullptr);EXPECT_EQ(u"new context",field->GetText());EXPECT_EQ(ui::mojom::DragOperation::kNone,operation);
  shell->settings_widget()->Hide();EXPECT_FALSE(field->metal_visible());const auto saved_bounds=view->bounds();view->SetBoundsRect(gfx::Rect(view->size()));view->SchedulePaint();const auto bitmap=views::test::PaintViewToBitmap(view);view->SetBoundsRect(saved_bounds);const auto background=bitmap.getColor(bitmap.width()-12,bitmap.height()/2);size_t ink=0;for(int y=4;y<bitmap.height()-4;++y)for(int x=10;x<bitmap.width()-12;++x)if(bitmap.getColor(x,y)!=background)++ink;EXPECT_GT(ink,20u);EXPECT_FALSE(views::AsViewClass<views::Textfield>(view));EXPECT_TRUE(view->children().empty());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, NameEditorWordDragAndSingleLineTripleClick) {
  auto* shell=window_->shell();
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));
  shell->OpenWorkspaceSettings();auto* field=shell->workspace_input();auto* view=field->view();
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->settings_widget()->GetNativeWindow()));
  field->SetText(u"one  two!! three");
  auto at=[&](size_t index){field->SelectRange(gfx::Range(index));auto point=field->input_client()->GetCaretBounds().CenterPoint();views::View::ConvertPointFromScreen(view,&point);return point;};
  const auto middle=at(6),left=at(1),right=at(14),gap=at(9);
  base::TimeTicks stamp=base::TimeTicks::Now();
  auto release=[&](gfx::Point point){ui::MouseEvent event(ui::EventType::kMouseReleased,point,point,stamp,0,ui::EF_LEFT_MOUSE_BUTTON);view->OnMouseReleased(event);};
  auto press=[&](gfx::Point point,int count){stamp+=base::Seconds(1);for(int i=1;i<=count;++i){stamp+=base::Milliseconds(10);ui::MouseEvent event(ui::EventType::kMousePressed,point,point,stamp,ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);event.SetClickCount(i);if(!view->OnMousePressed(event))return false;if(i<count)release(point);}return true;};
  auto drag=[&](gfx::Point point){stamp+=base::Milliseconds(10);ui::MouseEvent event(ui::EventType::kMouseDragged,point,point,stamp,ui::EF_LEFT_MOUSE_BUTTON,0);return view->OnMouseDragged(event);};
  ASSERT_TRUE(press(middle,2));EXPECT_EQ(gfx::Range(5,8),field->GetSelectedRange());
  ASSERT_TRUE(drag(right));EXPECT_EQ(gfx::Range(5,16),field->GetSelectedRange());
  ASSERT_TRUE(drag(left));EXPECT_EQ(gfx::Range(8,0),field->GetSelectedRange());
  ASSERT_TRUE(drag(middle));EXPECT_EQ(gfx::Range(8,5),field->GetSelectedRange());
  ASSERT_TRUE(drag(right));EXPECT_EQ(gfx::Range(5,16),field->GetSelectedRange());release(right);
  ASSERT_TRUE(press(gap,2));EXPECT_EQ(gfx::Range(8,11),field->GetSelectedRange());release(gap);
  ASSERT_TRUE(press(middle,3));EXPECT_EQ(gfx::Range(0,16),field->GetSelectedRange());
  ASSERT_TRUE(drag(right));EXPECT_EQ(gfx::Range(0,14),field->GetSelectedRange());release(right);
  // The native command requires an empty selection, including at end-of-text.
  field->SelectRange(gfx::Range(6,1));EXPECT_FALSE(field->input_client()->IsTextEditCommandEnabled(ui::TextEditCommand::SELECT_WORD));
  field->SelectRange(gfx::Range(16));field->input_client()->SetTextEditCommandForNextKeyEvent(ui::TextEditCommand::SELECT_WORD);
  ui::KeyEvent command(ui::EventType::kKeyPressed,ui::VKEY_F15,0);ASSERT_TRUE(view->OnKeyPressed(command));
  EXPECT_EQ(gfx::Range(11,16),field->GetSelectedRange());EXPECT_EQ(u"one  two!! three",field->GetText());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, NameEditorStationaryEdgeSelectionScrollAndCancellation) {
  gfx::ScopedAnimationDurationScaleMode duration(gfx::ScopedAnimationDurationScaleMode::NORMAL_DURATION);
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));
  shell->OpenWorkspaceSettings();auto* field=shell->workspace_input();auto* view=field->view();
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->settings_widget()->GetNativeWindow()));
  std::u16string text;for(int i=0;i<100;++i)text+=u"abcdefghij ";field->SetText(text);field->SelectRange(gfx::Range(0));
  auto point=field->input_client()->GetCaretBounds().CenterPoint();views::View::ConvertPointFromScreen(view,&point);
  auto stamp=base::TimeTicks::Now();ui::MouseEvent down(ui::EventType::kMousePressed,point,point,stamp,ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);down.SetClickCount(1);ASSERT_TRUE(view->OnMousePressed(down));
  const gfx::Point edge(view->width()+40,view->height()/2);
  ui::MouseEvent outside(ui::EventType::kMouseDragged,edge,edge,stamp+base::Milliseconds(10),ui::EF_LEFT_MOUSE_BUTTON,0);ASSERT_TRUE(view->OnMouseDragged(outside));
  const auto first=field->GetSelectedRange().end();ASSERT_GT(first,0u);ASSERT_LT(first,text.size());
  // No further mouse motion: the timer must reveal and select more text.
  ASSERT_TRUE(base::test::RunUntil([&]{return field->GetSelectedRange().end()>first+2;}));
  auto pump=[](){base::RunLoop loop;base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(FROM_HERE,loop.QuitClosure(),base::Milliseconds(250));loop.Run();};
  ui::MouseEvent up(ui::EventType::kMouseReleased,edge,edge,stamp+base::Milliseconds(20),0,ui::EF_LEFT_MOUSE_BUTTON);view->OnMouseReleased(up);
  const auto released=field->GetSelectedRange();pump();EXPECT_EQ(released,field->GetSelectedRange());
  // Capture loss must stop the same timer without waiting for a mouse release.
  point.set_x(10);stamp+=base::Seconds(1);ui::MouseEvent again(ui::EventType::kMousePressed,point,point,stamp,ui::EF_LEFT_MOUSE_BUTTON,ui::EF_LEFT_MOUSE_BUTTON);again.SetClickCount(1);field->SelectRange(gfx::Range(0));ASSERT_TRUE(view->OnMousePressed(again));ASSERT_TRUE(view->OnMouseDragged(outside));view->OnMouseCaptureLost();const auto captured=field->GetSelectedRange();pump();EXPECT_EQ(captured,field->GetSelectedRange());
  EXPECT_EQ(text,field->GetText());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, FolderEditorCocoaTypingUnicodeAndModalLifetime) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->RequestNewFolder("work");ASSERT_TRUE(base::test::RunUntil([&]{return shell->folder_dialog()!=nullptr;}));
  auto* editor=shell->folder_editor_for_testing();auto* native=NativeFolderNameInput(shell->folder_dialog()->GetContentsView());ASSERT_TRUE(editor||native);
  auto* view=editor?editor->view():static_cast<views::View*>(native);auto* client=editor?editor->input_client():static_cast<ui::TextInputClient*>(native);
  if(editor){EXPECT_TRUE(editor->rust_editor());EXPECT_FALSE(native);EXPECT_TRUE(view->children().empty());ASSERT_TRUE(base::test::RunUntil([&]{return editor->metal_visible();}));}
  ui_controls::EnableUIControls();ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->folder_dialog()->GetNativeWindow()));view->RequestFocus();ASSERT_TRUE(base::test::RunUntil([&]{return view->HasFocus();}));
  auto key=[&](ui::KeyboardCode code){return ui_test_utils::SendKeyPressToWindowSync(shell->folder_dialog()->GetNativeWindow(),code,false,false,false,false);};
  for(auto code:{ui::VKEY_T,ui::VKEY_E,ui::VKEY_S,ui::VKEY_T})ASSERT_TRUE(key(code));
  ASSERT_TRUE(base::test::RunUntil([&]{gfx::Range range;std::u16string text;return client->GetTextRange(&range)&&client->GetTextFromRange(range,&text)&&text==u"test";}));
  client->InsertText(u" 🦀",ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);
  gfx::Range range;std::u16string text;ASSERT_TRUE(client->GetTextRange(&range));ASSERT_TRUE(client->GetTextFromRange(range,&text));EXPECT_EQ(u"test 🦀",text);EXPECT_TRUE(window_->pinned_items("work").empty());
  ASSERT_TRUE(key(ui::VKEY_RETURN));ASSERT_TRUE(base::test::RunUntil([&]{return !shell->folder_dialog()&&!window_->profiles_busy();}));ASSERT_EQ(1u,window_->pinned_items("work").size());const auto id=window_->pinned_items("work")[0].id;EXPECT_EQ("test 🦀",window_->pinned_items("work")[0].title);
  shell->RequestRenameFolder("work",id);ASSERT_TRUE(base::test::RunUntil([&]{return shell->folder_dialog()!=nullptr;}));editor=shell->folder_editor_for_testing();native=NativeFolderNameInput(shell->folder_dialog()->GetContentsView());ASSERT_TRUE(editor||native);view=editor?editor->view():static_cast<views::View*>(native);client=editor?editor->input_client():static_cast<ui::TextInputClient*>(native);
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->folder_dialog()->GetNativeWindow()));view->RequestFocus();ASSERT_TRUE(base::test::RunUntil([&]{return view->HasFocus();}));
  ASSERT_TRUE(client->GetTextRange(&range));ASSERT_TRUE(client->SetEditableSelectionRange(gfx::Range(range.end())));
  ui::CompositionText preedit;preedit.text=u"にほん";preedit.selection=gfx::Range(preedit.text.size());client->SetCompositionText(preedit);EXPECT_EQ("test 🦀",window_->pinned_items("work")[0].title);
  auto lifetime=client->AsWeakPtr();ASSERT_TRUE(lifetime);shell->folder_dialog()->widget_delegate()->AsDialogDelegate()->CancelDialog();ASSERT_TRUE(base::test::RunUntil([&]{return !shell->folder_dialog()&&!lifetime;}));EXPECT_EQ("test 🦀",window_->pinned_items("work")[0].title);
  shell->RequestNewFolder("work");ASSERT_TRUE(base::test::RunUntil([&]{return shell->folder_dialog()!=nullptr;}));editor=shell->folder_editor_for_testing();native=NativeFolderNameInput(shell->folder_dialog()->GetContentsView());ASSERT_TRUE(editor||native);client=editor?editor->input_client():static_cast<ui::TextInputClient*>(native);ASSERT_TRUE(client->GetTextRange(&range));ASSERT_TRUE(client->GetTextFromRange(range,&text));EXPECT_EQ(u"Nowy folder",text);
  shell->folder_dialog()->widget_delegate()->AsDialogDelegate()->CancelDialog();ASSERT_TRUE(base::test::RunUntil([&]{return !shell->folder_dialog();}));EXPECT_EQ(1u,window_->pinned_items("work").size());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, AddressEditorCocoaCommitCompositionAndNavigation) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->OpenAddress(true);auto* field=shell->address_input();auto* view=field->view();
  EXPECT_EQ(shell->workspace_input()->rust_editor(),field->rust_editor());
  if(field->rust_editor()){EXPECT_TRUE(view->children().empty());EXPECT_FALSE(views::AsViewClass<views::Textfield>(view));ASSERT_TRUE(base::test::RunUntil([&]{return field->metal_visible();}));}
  ui_controls::EnableUIControls();ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->address_widget()->GetNativeWindow()));field->RequestFocus();
  auto key=[&](ui::KeyboardCode code,bool command=false){return ui_test_utils::SendKeyPressToWindowSync(shell->address_widget()->GetNativeWindow(),code,false,false,false,command);};
  ASSERT_TRUE(key(ui::VKEY_A));ASSERT_TRUE(base::test::RunUntil([&]{return field->GetText()==u"a";}));
  const auto original=window_->contents("work")->GetLastCommittedURL();ui::CompositionText marked;marked.text=u"にほん";marked.selection=gfx::Range(marked.text.size());field->SetCompositionText(marked);ASSERT_TRUE(field->IsIMEComposing());
  const auto before=shell->suggestions();PaletteResult injected;injected.title=u"Must not enter palette during composition";injected.url=embedded_test_server()->GetURL("/storage_probe.html?injected");
  shell->SetSearchResults(std::u16string(field->GetText()),window_->workspaces().front().profile_id,{injected});EXPECT_EQ(before,shell->suggestions());
  for(auto code:{ui::VKEY_DOWN,ui::VKEY_RETURN,ui::VKEY_ESCAPE}){ui::KeyEvent event(ui::EventType::kKeyPressed,code,0);EXPECT_FALSE(shell->HandleAddressKey(event));}
  EXPECT_TRUE(shell->address_visible());EXPECT_EQ(original,window_->contents("work")->GetLastCommittedURL());field->ClearCompositionText();EXPECT_EQ(u"a",field->GetText());
  ASSERT_TRUE(key(ui::VKEY_A,true));ASSERT_TRUE(key(ui::VKEY_BACK));ASSERT_TRUE(base::test::RunUntil([&]{return field->GetText().empty();}));
  const auto url=embedded_test_server()->GetURL("/storage_probe.html?address-editor-long-long-long-long-long-long-long-long-long-long-long");field->input_client()->InsertText(base::UTF8ToUTF16(url.spec()),ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);
  const auto caret=field->input_client()->GetCaretBounds(),bounds=view->GetBoundsInScreen();EXPECT_GE(caret.CenterPoint().x(),bounds.x());EXPECT_LE(caret.CenterPoint().x(),bounds.right()+1);
  ASSERT_TRUE(key(ui::VKEY_RETURN));ASSERT_TRUE(base::test::RunUntil([&]{return !shell->address_visible()&&window_->contents("work")->GetLastCommittedURL()==url;}));
  shell->OpenCommandPalette();field=shell->address_input();EXPECT_TRUE(field->GetText().empty());ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->address_widget()->GetNativeWindow()));field->RequestFocus();ASSERT_TRUE(key(ui::VKEY_ESCAPE));ASSERT_TRUE(base::test::RunUntil([&]{return !shell->address_visible();}));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, NameEditorEmptyMarkedTextEndsWithoutCommit) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->OpenWorkspaceSettings();auto* field=shell->workspace_input();
  ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->settings_widget()->GetNativeWindow()));field->RequestFocus();
  int commits=0;base::ScopedClosureRunner cleanup(base::BindOnce([](EditorControl* editor){editor->SetCallbacks({},{});},field));
  field->SetCallbacks(base::BindRepeating([](int* count){++*count;},base::Unretained(&commits)),{});
  ui::CompositionText empty;field->SetText(u"base");field->SelectRange(gfx::Range(3,1));
  field->SetCompositionText(empty);EXPECT_EQ(u"base",field->GetText());EXPECT_EQ(gfx::Range(3,1),field->GetSelectedRange());EXPECT_FALSE(field->IsIMEComposing());EXPECT_EQ(0,commits);
  field->SelectRange(gfx::Range(4));ui::CompositionText marked;marked.text=u"にほん";marked.selection=gfx::Range(marked.text.size());field->SetCompositionText(marked);ASSERT_TRUE(field->IsIMEComposing());EXPECT_EQ(0,commits);
  field->SetCompositionText(empty);EXPECT_FALSE(field->IsIMEComposing());EXPECT_FALSE(field->input_client()->HasCompositionText());EXPECT_EQ(u"base",field->GetText());EXPECT_EQ(gfx::Range(4),field->GetSelectedRange());EXPECT_EQ(0,commits);
  gfx::Range range;ASSERT_TRUE(field->input_client()->GetCompositionTextRange(&range));EXPECT_FALSE(range.IsValid());EXPECT_EQ(0u,field->ConfirmCompositionText(false));EXPECT_EQ(0,commits);
  field->SetReadOnly(true);EXPECT_FALSE(field->input_client()->GetCompositionTextRange(&range));field->SetReadOnly(false);
  marked.text=u"語";marked.selection=gfx::Range(1);field->SetCompositionText(marked);ASSERT_TRUE(field->IsIMEComposing());field->ConfirmCompositionText(false);EXPECT_EQ(u"base語",field->GetText());EXPECT_EQ(1,commits);
  field->SetCompositionText(empty);field->SetCompositionText(empty);EXPECT_FALSE(field->IsIMEComposing());EXPECT_EQ(u"base語",field->GetText());EXPECT_EQ(1,commits);
  // An empty marker must not auto-save an empty emoji draft as a deletion.
  shell->SetWorkspaceEmoji("work",u"🦊");auto* emoji=shell->emoji_input();emoji->RequestFocus();emoji->SetText(u"");
  marked.text=u"にほん";marked.selection=gfx::Range(marked.text.size());emoji->SetCompositionText(marked);ASSERT_TRUE(emoji->IsIMEComposing());EXPECT_EQ(u"🦊",shell->workspace_emoji("work"));
  emoji->SetCompositionText(empty);EXPECT_FALSE(emoji->IsIMEComposing());EXPECT_TRUE(emoji->GetText().empty());EXPECT_EQ(u"🦊",shell->workspace_emoji("work"));
  // Idle empty updates preserve an existing selection as well as its value.
  emoji->SetText(u"🦊");emoji->SelectRange(gfx::Range(0,2));emoji->SetCompositionText(empty);EXPECT_EQ(u"🦊",emoji->GetText());EXPECT_EQ(gfx::Range(0,2),emoji->GetSelectedRange());EXPECT_EQ(u"🦊",shell->workspace_emoji("work"));
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_saved();}));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, NameEditorCommittedCallbackContract) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->OpenWorkspaceSettings();
  auto* field=shell->workspace_input();auto* view=field->view();ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->settings_widget()->GetNativeWindow()));field->RequestFocus();
  int commits=0,keys=0;std::u16string observed;
  base::ScopedClosureRunner cleanup(base::BindOnce([](EditorControl* editor){editor->SetCallbacks({},{});},field));
  field->SetCallbacks(base::BindRepeating([](int* count,std::u16string* observed,EditorControl* editor){++*count;*observed=editor->GetText();},base::Unretained(&commits),base::Unretained(&observed),base::Unretained(field)),base::BindRepeating([](int* count,const ui::KeyEvent& event){if(event.key_code()!=ui::VKEY_F15)return false;++*count;return true;},base::Unretained(&keys)));
  field->SetText(u"base");EXPECT_EQ(0,commits);
  field->input_client()->InsertText(u"!",ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);EXPECT_EQ(1,commits);EXPECT_EQ(u"base!",observed);
  auto preedit=[&](std::u16string text){ui::CompositionText composition;composition.text=std::move(text);composition.selection=gfx::Range(composition.text.size());field->SetCompositionText(composition);};
  preedit(u"cancel");ASSERT_TRUE(field->IsIMEComposing());EXPECT_EQ(1,commits);field->ClearCompositionText();EXPECT_EQ(1,commits);EXPECT_EQ(u"base!",field->GetText());
  preedit(u"✓");EXPECT_EQ(1,commits);field->ConfirmCompositionText(false);EXPECT_EQ(2,commits);EXPECT_EQ(u"base!✓",observed);field->ConfirmCompositionText(false);EXPECT_EQ(2,commits);
  preedit(u"stale");field->SetText(u"new context");EXPECT_EQ(2,commits);EXPECT_FALSE(field->IsIMEComposing());
  ui::KeyEvent key(ui::EventType::kKeyPressed,ui::VKEY_F15,0);ASSERT_TRUE(view->OnKeyPressed(key));EXPECT_EQ(1,keys);EXPECT_EQ(2,commits);
  // Domain normalization is synchronous and must neither recurse nor publish a
  // borrowed buffer invalidated by SetText inside the callback.
  field->SetCallbacks(base::BindRepeating([](int* count,EditorControl* editor){++*count;editor->SetText(u"normalized");},base::Unretained(&commits),base::Unretained(field)),{});
  field->input_client()->InsertText(u"x",ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);EXPECT_EQ(3,commits);EXPECT_EQ(u"normalized",field->GetText());
  field->SetCallbacks({},{});field->input_client()->InsertText(u"!",ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);EXPECT_EQ(3,commits);EXPECT_EQ(u"normalized!",field->GetText());
  field->SetCallbacks(base::BindRepeating([](int* count){++*count;},base::Unretained(&commits)),{});
  field->SelectRange(gfx::Range(0,field->GetText().size()));field->input_client()->InsertText(u"normalized!",ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);EXPECT_EQ(4,commits);EXPECT_EQ(u"normalized!",field->GetText());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, AppearanceEditorsCommitValidationIMEAndWorkspaceIsolation) {
  auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->OpenWorkspaceSettings();
  auto* color=shell->color_input();auto* emoji=shell->emoji_input();ASSERT_TRUE(color);ASSERT_TRUE(emoji);
  const bool metal=shell->workspace_input()->rust_editor();EXPECT_EQ(metal,color->rust_editor());EXPECT_EQ(metal,emoji->rust_editor());
  if(metal){EXPECT_TRUE(color->view()->children().empty());EXPECT_TRUE(emoji->view()->children().empty());EXPECT_FALSE(views::AsViewClass<views::Textfield>(color->view()));EXPECT_FALSE(views::AsViewClass<views::Textfield>(emoji->view()));ASSERT_TRUE(base::test::RunUntil([&]{return color->metal_visible()&&emoji->metal_visible();}));}
  else {EXPECT_TRUE(views::AsViewClass<views::Textfield>(color->view()));EXPECT_TRUE(views::AsViewClass<views::Textfield>(emoji->view()));}
  ui_controls::EnableUIControls();ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->settings_widget()->GetNativeWindow()));
  auto key=[&](EditorControl* field,ui::KeyboardCode code,bool command=false){field->RequestFocus();return base::test::RunUntil([&]{return field->HasFocus();})&&ui_test_utils::SendKeyPressToWindowSync(shell->settings_widget()->GetNativeWindow(),code,false,false,false,command);};
  const auto initial=shell->workspace_color("work");color->SetText(u"invalid");EXPECT_EQ(initial,shell->workspace_color("work"));ASSERT_TRUE(key(color,ui::VKEY_RETURN));EXPECT_EQ(initial,shell->workspace_color("work"));
  color->SetText(u"#123456");EXPECT_EQ(initial,shell->workspace_color("work"));ASSERT_TRUE(key(color,ui::VKEY_RETURN));EXPECT_EQ(0xFF123456u,shell->workspace_color("work"));
  ui::CompositionText preedit;preedit.text=u"uncommitted";preedit.selection=gfx::Range(preedit.text.size());color->SetCompositionText(preedit);ASSERT_TRUE(color->IsIMEComposing());ui::KeyEvent enter(ui::EventType::kKeyPressed,ui::VKEY_RETURN,0);EXPECT_FALSE(color->view()->OnKeyPressed(enter));EXPECT_EQ(0xFF123456u,shell->workspace_color("work"));color->ClearCompositionText();
  shell->SetWorkspaceEmoji("work",u"🦊");emoji->RequestFocus();emoji->SetText(u"");EXPECT_EQ(u"🦊",shell->workspace_emoji("work"));
  preedit.text=u"にほん";preedit.selection=gfx::Range(preedit.text.size());emoji->SetCompositionText(preedit);ASSERT_TRUE(emoji->IsIMEComposing());EXPECT_EQ(u"🦊",shell->workspace_emoji("work"));emoji->ClearCompositionText();EXPECT_EQ(u"🦊",shell->workspace_emoji("work"));
  preedit.text=u"👩‍💻abc";preedit.selection=gfx::Range(preedit.text.size());emoji->SetCompositionText(preedit);EXPECT_EQ(u"🦊",shell->workspace_emoji("work"));emoji->ConfirmCompositionText(false);EXPECT_EQ(u"👩‍💻",shell->workspace_emoji("work"));EXPECT_EQ(u"👩‍💻",emoji->GetText());
  ASSERT_TRUE(key(emoji,ui::VKEY_A,true));ASSERT_TRUE(key(emoji,ui::VKEY_X));ASSERT_TRUE(base::test::RunUntil([&]{return shell->workspace_emoji("work")==u"x";}));
  ASSERT_TRUE(window_->Select("personal"));shell->OpenWorkspaceSettings();emoji=shell->emoji_input();emoji->RequestFocus();emoji->SetText(u"");emoji->input_client()->InsertText(u"🌳extra",ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);EXPECT_EQ(u"🌳",shell->workspace_emoji("personal"));EXPECT_EQ(u"x",shell->workspace_emoji("work"));EXPECT_EQ(0xFF123456u,shell->workspace_color("work"));
  ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_saved();}));
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, NameEditorMacCommandsAndSelectionPolicies) {
  using C=ui::TextEditCommand;auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->OpenWorkspaceSettings();auto* field=shell->workspace_input();auto* view=field->view();auto* client=field->input_client();
  ui_controls::EnableUIControls();ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->settings_widget()->GetNativeWindow()));view->RequestFocus();ASSERT_TRUE(base::test::RunUntil([&]{return view->HasFocus();}));
  auto command=[&](C c){client->SetTextEditCommandForNextKeyEvent(c);ui::KeyEvent key(ui::EventType::kKeyPressed,ui::VKEY_F15,ui::EF_NONE);return view->OnKeyPressed(key);};
  field->SetText(u"one two three");field->SelectRange(gfx::Range(5,9));ASSERT_TRUE(command(C::MOVE_TO_BEGINNING_OF_LINE_AND_MODIFY_SELECTION));EXPECT_EQ(gfx::Range(9,0),field->GetSelectedRange());ASSERT_TRUE(command(C::MOVE_TO_END_OF_LINE_AND_MODIFY_SELECTION));EXPECT_EQ(gfx::Range(0,13),field->GetSelectedRange());
  field->SelectRange(gfx::Range(5,9));ASSERT_TRUE(command(C::MOVE_WORD_BACKWARD_AND_MODIFY_SELECTION));EXPECT_EQ(gfx::Range(9,4),field->GetSelectedRange());field->SelectRange(gfx::Range(5,9));ASSERT_TRUE(command(C::MOVE_WORD_BACKWARD));EXPECT_EQ(gfx::Range(4),field->GetSelectedRange());field->SelectRange(gfx::Range(5,9));ASSERT_TRUE(command(C::MOVE_WORD_FORWARD));EXPECT_EQ(gfx::Range(13),field->GetSelectedRange());
  field->SelectRange(gfx::Range(5));for(int n=0;n<4;++n)ASSERT_TRUE(command(C::MOVE_FORWARD_AND_MODIFY_SELECTION));EXPECT_EQ(gfx::Range(5,9),field->GetSelectedRange());ASSERT_TRUE(command(C::MOVE_WORD_BACKWARD_AND_MODIFY_SELECTION));EXPECT_EQ(gfx::Range(5,8),field->GetSelectedRange());ASSERT_TRUE(command(C::MOVE_WORD_BACKWARD_AND_MODIFY_SELECTION));EXPECT_EQ(gfx::Range(5),field->GetSelectedRange());
  field->SelectRange(gfx::Range(3,5));ASSERT_TRUE(command(C::MOVE_PAGE_UP_AND_MODIFY_SELECTION));EXPECT_EQ(gfx::Range(5,0),field->GetSelectedRange());ASSERT_TRUE(command(C::MOVE_PARAGRAPH_FORWARD_AND_MODIFY_SELECTION));EXPECT_EQ(gfx::Range(5),field->GetSelectedRange());
  ASSERT_TRUE(command(C::SCROLL_PAGE_DOWN));EXPECT_EQ(gfx::Range(13),field->GetSelectedRange());ASSERT_TRUE(command(C::SCROLL_TO_BEGINNING_OF_DOCUMENT));EXPECT_EQ(gfx::Range(0),field->GetSelectedRange());
  field->SelectRange(gfx::Range(5,9));ASSERT_TRUE(command(C::DELETE_TO_END_OF_LINE));EXPECT_EQ(u"one three",field->GetText());ASSERT_TRUE(command(C::YANK));EXPECT_EQ(u"one two three",field->GetText());
  field->SetText(u"abcd");field->SelectRange(gfx::Range(2));ASSERT_TRUE(command(C::TRANSPOSE));EXPECT_EQ(u"acbd",field->GetText());EXPECT_EQ(gfx::Range(3),field->GetSelectedRange());ASSERT_TRUE(command(C::UNDO));EXPECT_EQ(u"abcd",field->GetText());EXPECT_EQ(gfx::Range(1,3),field->GetSelectedRange());ASSERT_TRUE(command(C::REDO));EXPECT_EQ(u"acbd",field->GetText());
  field->SetText(u"🦀x");field->SelectRange(gfx::Range(3));ASSERT_TRUE(command(C::TRANSPOSE));EXPECT_EQ(u"x🦀",field->GetText());field->SetText(u"a\u0301b");field->SelectRange(gfx::Range(3));ASSERT_TRUE(command(C::TRANSPOSE));EXPECT_EQ(u"a\u0301b",field->GetText());
  EXPECT_FALSE(client->IsTextEditCommandEnabled(C::SET_MARK));EXPECT_FALSE(client->IsTextEditCommandEnabled(C::UNSELECT));EXPECT_FALSE(client->IsTextEditCommandEnabled(C::INSERT_TEXT));
  field->SelectRange(gfx::Range(0,field->GetText().size()));EXPECT_FALSE(client->IsTextEditCommandEnabled(C::SELECT_ALL));EXPECT_FALSE(client->IsTextEditCommandEnabled(C::TRANSPOSE));EXPECT_FALSE(client->IsTextEditCommandEnabled(C::SELECT_WORD));
  field->SetReadOnly(true);for(C c:{C::DELETE_TO_END_OF_LINE,C::YANK,C::TRANSPOSE,C::CUT,C::PASTE,C::UNDO,C::REDO})EXPECT_FALSE(client->IsTextEditCommandEnabled(c));EXPECT_TRUE(client->IsTextEditCommandEnabled(C::COPY));field->SetReadOnly(false);
  // Exercise actual Cocoa control chords as well as the platform edit-command API.
  auto native_key=[&](ui::KeyboardCode key,bool control,bool command){return ui_test_utils::SendKeyPressToWindowSync(shell->settings_widget()->GetNativeWindow(),key,control,false,false,command);};
  field->SetText(u"abcd");field->SelectRange(gfx::Range(2));ASSERT_TRUE(native_key(ui::VKEY_T,true,false));EXPECT_EQ(u"acbd",field->GetText());ASSERT_TRUE(native_key(ui::VKEY_Z,false,true));EXPECT_EQ(u"abcd",field->GetText());field->SelectRange(gfx::Range(2));ASSERT_TRUE(native_key(ui::VKEY_K,true,false));EXPECT_EQ(u"ab",field->GetText());ASSERT_TRUE(native_key(ui::VKEY_Y,true,false));EXPECT_EQ(u"abcd",field->GetText());
}
IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, NameEditorBidiDirectionAndSharedKillYank) {
  using C=ui::TextEditCommand;auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->OpenWorkspaceSettings();auto* field=shell->workspace_input();
  ui_controls::EnableUIControls();ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->settings_widget()->GetNativeWindow()));field->RequestFocus();auto command=[&](EditorControl* field,C c){field->input_client()->SetTextEditCommandForNextKeyEvent(c);ui::KeyEvent key(ui::EventType::kKeyPressed,ui::VKEY_F15,ui::EF_NONE);return field->view()->OnKeyPressed(key);};
  field->SetText(u"אבג");field->SelectRange(gfx::Range(0));ASSERT_TRUE(command(field,C::MOVE_WORD_LEFT));EXPECT_EQ(gfx::Range(3),field->GetSelectedRange());ASSERT_TRUE(command(field,C::MOVE_WORD_RIGHT));EXPECT_EQ(gfx::Range(0),field->GetSelectedRange());
  field->SetText(u"abc אבג");field->SelectRange(gfx::Range(5));ASSERT_TRUE(command(field,C::MOVE_BACKWARD));EXPECT_EQ(gfx::Range(6),field->GetSelectedRange());ASSERT_TRUE(command(field,C::MOVE_FORWARD));EXPECT_EQ(gfx::Range(5),field->GetSelectedRange());auto* client=field->input_client();ASSERT_TRUE(client->ChangeTextDirectionAndLayoutAlignment(base::i18n::RIGHT_TO_LEFT));EXPECT_EQ(base::i18n::RIGHT_TO_LEFT,client->GetTextDirection());ui::AXNodeData rtl;field->GetViewAccessibility().GetAccessibleNodeData(&rtl);EXPECT_EQ(static_cast<int>(ax::mojom::WritingDirection::kRtl),rtl.GetIntAttribute(ax::mojom::IntAttribute::kTextDirection));EXPECT_TRUE(field->view()->GetBoundsInScreen().Contains(client->GetCaretBounds()));
  ASSERT_TRUE(client->ChangeTextDirectionAndLayoutAlignment(base::i18n::RIGHT_TO_LEFT));EXPECT_EQ(base::i18n::LEFT_TO_RIGHT,client->GetTextDirection());ui::AXNodeData ltr;field->GetViewAccessibility().GetAccessibleNodeData(&ltr);EXPECT_EQ(static_cast<int>(ax::mojom::WritingDirection::kLtr),ltr.GetIntAttribute(ax::mojom::IntAttribute::kTextDirection));
  field->SetText(u"one two");field->SelectRange(gfx::Range(4));ASSERT_TRUE(command(field,C::DELETE_TO_END_OF_PARAGRAPH));EXPECT_EQ(u"one ",field->GetText());
  shell->OpenProfiles("work-profile");auto* other=shell->profile_name_input();ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->profiles_widget()->GetNativeWindow()));other->RequestFocus();other->SetText(u"");ASSERT_TRUE(command(other,C::YANK));EXPECT_EQ(u"two",other->GetText());ASSERT_TRUE(command(other,C::UNDO));EXPECT_TRUE(other->GetText().empty());ASSERT_TRUE(command(other,C::REDO));EXPECT_EQ(u"two",other->GetText());
}

IN_PROC_BROWSER_TEST_F(ZitterProfilesBrowserTest, NameEditorKillYankInteroperatesWithNativeFields) {
  using C=ui::TextEditCommand;auto* shell=window_->shell();ASSERT_TRUE(base::test::RunUntil([&]{return shell->appearance_ready();}));shell->OpenWorkspaceSettings();auto* name=shell->workspace_input();
  // A fixture-owned native control exercises the shared service while all live
  // appearance fields can migrate. It is not part of the production UI.
  auto* native=name->view()->parent()->AddChildView(std::make_unique<views::Textfield>());
  native->SetBoundsRect(name->view()->bounds());ASSERT_TRUE(native);
  const std::u16string saved(views::GetTextEditKillBuffer());base::ScopedClosureRunner restore(base::BindOnce([](std::u16string text){views::SetTextEditKillBuffer(text);},saved));
  ui_controls::EnableUIControls();ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(shell->settings_widget()->GetNativeWindow()));
  auto key=[&](views::View* view,ui::KeyboardCode key){view->RequestFocus();return base::test::RunUntil([&]{return view->HasFocus();})&&ui_test_utils::SendKeyPressToWindowSync(shell->settings_widget()->GetNativeWindow(),key,true,false,false,false);};
  name->SetText(u"workspace-cut");name->SelectRange(gfx::Range(10));ASSERT_TRUE(key(name->view(),ui::VKEY_K));EXPECT_EQ(u"workspace-",name->GetText());EXPECT_EQ(u"cut",views::GetTextEditKillBuffer());
  native->SetText(u"native:");native->SetSelectedRange(gfx::Range(7));ASSERT_TRUE(key(native,ui::VKEY_Y));EXPECT_EQ(u"native:cut",native->GetText());
  native->SetText(u"old-shared");native->SetSelectedRange(gfx::Range(4));ASSERT_TRUE(key(native,ui::VKEY_K));EXPECT_EQ(u"old-",native->GetText());EXPECT_EQ(u"shared",views::GetTextEditKillBuffer());
  name->SetText(u"R:");name->SelectRange(gfx::Range(2));ASSERT_TRUE(key(name->view(),ui::VKEY_Y));EXPECT_EQ(u"R:shared",name->GetText());
  // Reversed service selection kills only that range, then native yank sees it.
  name->SetText(u"abcdef");name->SelectRange(gfx::Range(4,2));ASSERT_TRUE(key(name->view(),ui::VKEY_K));EXPECT_EQ(u"abef",name->GetText());EXPECT_EQ(u"cd",views::GetTextEditKillBuffer());native->SetText(u"");ASSERT_TRUE(key(native,ui::VKEY_Y));EXPECT_EQ(u"cd",native->GetText());
  views::SetTextEditKillBuffer(u"keep");name->SetText(u"");ASSERT_TRUE(key(name->view(),ui::VKEY_K));EXPECT_EQ(u"keep",views::GetTextEditKillBuffer());native->SetText(u"");ASSERT_TRUE(key(native,ui::VKEY_K));EXPECT_EQ(u"keep",views::GetTextEditKillBuffer());
  name->SetReadOnly(true);EXPECT_FALSE(name->input_client()->IsTextEditCommandEnabled(C::YANK));EXPECT_FALSE(name->input_client()->IsTextEditCommandEnabled(C::DELETE_TO_END_OF_LINE));EXPECT_EQ(u"keep",views::GetTextEditKillBuffer());name->SetReadOnly(false);
  views::SetTextEditKillBuffer(u"🦀語");name->SetText(u"");ASSERT_TRUE(key(name->view(),ui::VKEY_Y));EXPECT_EQ(u"🦀語",name->GetText());native->SetText(u"");ASSERT_TRUE(key(native,ui::VKEY_Y));EXPECT_EQ(u"🦀語",native->GetText());
  views::SetTextEditKillBuffer(u"");name->SetText(u"xx");name->SelectRange(gfx::Range(0,2));ASSERT_TRUE(key(name->view(),ui::VKEY_Y));EXPECT_TRUE(name->GetText().empty());
}

#endif

} // namespace zitter::prototype
