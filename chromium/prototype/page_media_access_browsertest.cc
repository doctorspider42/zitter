#include "base/threading/thread_restrictions.h"
#include "base/files/file_util.h"
#include "chromium/prototype/sidebar_layout.h"
#include "chromium/prototype/page_media_access.h"
#include "chromium/prototype/page_dialogs.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/view_tracker.h"
#include "ui/compositor/compositor.h"
#include "ui/base/test/ui_controls.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chromium/prototype/workspace_window.h"

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/bind_post_task.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/bind.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/test/scoped_command_line.h"
#include "base/test/scoped_run_loop_timeout.h"
#include "build/build_config.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/media/webrtc/desktop_media_picker_factory.h"
#include "chrome/browser/media/webrtc/display_media_access_handler.h"
#include "chrome/browser/media/webrtc/media_capture_devices_dispatcher.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/permissions/permission_request_manager.h"
#include "components/permissions/permission_request.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/audio_service.h"
#include "content/public/common/content_features.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/base/base_window.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"
#include "ui/views/controls/label.h"
#include "media/audio/fake_audio_log_factory.h"
#include "media/audio/fake_audio_manager.h"
#include "media/audio/test_audio_thread.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/audio/service_factory.h"
#if BUILDFLAG(IS_MAC)
#include "chrome/browser/permissions/system/system_media_permission_cache_mac_test_helper.h"
#endif

namespace zitter::prototype {
namespace {
// Metal presentation oracles apply only with --zitter-rust-raster=metal.
bool MetalPresents(){return base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII("zitter-rust-raster")=="metal";}
}  // namespace
namespace {

// Same embedded-service seam used by Chromium's AudioServiceBrowserTest. The
// platform InputController otherwise queries native device parameters even
// when the selected stream itself is fake. Keep this fixture entirely synthetic.
class FakeAudioService {
 public:
  FakeAudioService()
      : manager_(std::make_unique<media::TestAudioThread>(true), &log_factory_) {
    base::test::TestFuture<std::unique_ptr<audio::Service>> ready;
    manager_.GetTaskRunner()->PostTask(FROM_HERE, base::BindOnce(
        [](media::AudioManager* manager,
            mojo::PendingReceiver<audio::mojom::AudioService> receiver,
            base::OnceCallback<void(std::unique_ptr<audio::Service>)> complete) {
          std::move(complete).Run(audio::CreateEmbeddedService(manager, std::move(receiver)));
        }, &manager_, remote_.BindNewPipeAndPassReceiver(),
        base::BindPostTaskToCurrentDefault(ready.GetCallback())));
    service_ = ready.Take();
    override_ = std::make_unique<base::AutoReset<audio::mojom::AudioService*>>(
        content::OverrideAudioServiceForTesting(remote_.get()));
  }
  ~FakeAudioService() {
    override_.reset();
    remote_.reset();
    base::test::TestFuture<void> destroyed;
    manager_.GetTaskRunner()->PostTask(FROM_HERE, base::BindOnce(
        [](std::unique_ptr<audio::Service> service, base::OnceClosure complete) {
          service.reset();
          std::move(complete).Run();
        }, std::move(service_), base::BindPostTaskToCurrentDefault(destroyed.GetCallback())));
    EXPECT_TRUE(destroyed.Wait());
    manager_.Shutdown();
  }
 private:
  media::FakeAudioLogFactory log_factory_;
  media::FakeAudioManager manager_;
  mojo::Remote<audio::mojom::AudioService> remote_;
  std::unique_ptr<audio::Service> service_;
  std::unique_ptr<base::AutoReset<audio::mojom::AudioService*>> override_;
};

std::u16string DialogText(views::View* view) {
  std::u16string text;
  ui::AXNodeData node;view->GetViewAccessibility().GetAccessibleNodeData(&node);
  if(node.role==ax::mojom::Role::kStaticText)text+=node.GetString16Attribute(ax::mojom::StringAttribute::kName);
  for (const auto& child : view->children()) text += u"\n" + DialogText(child.get());
  return text;
}

class CancelDesktopPicker final : public DesktopMediaPicker {
 public:
  explicit CancelDesktopPicker(bool* shown, std::shared_ptr<bool> destroyed = nullptr)
      : shown_(shown), destroyed_(destroyed) {}
  ~CancelDesktopPicker() override { if (destroyed_) *destroyed_ = true; }
  void Show(const Params& params,
            std::vector<std::unique_ptr<DesktopMediaList>> source_lists,
            DoneCallback done) override {
    *shown_ = true;
    EXPECT_FALSE(params.select_only_screen);
    EXPECT_FALSE(params.app_name.empty());
    EXPECT_TRUE(params.web_contents);
    if (destroyed_) {
      pending_ = std::move(done);
      return;
    }
    // No screen/window/device is ever selected or enumerated in this test.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
        base::BindOnce(std::move(done), base::unexpected(
            blink::mojom::MediaStreamRequestResult::PERMISSION_DENIED)));
  }
 private:
  raw_ptr<bool> shown_;
  std::shared_ptr<bool> destroyed_;
  DoneCallback pending_;
};
class CancelDesktopFactory final : public DesktopMediaPickerFactory {
 public:
  explicit CancelDesktopFactory(bool* shown, std::shared_ptr<bool> destroyed = nullptr)
      : shown_(shown), destroyed_(destroyed) {}
  std::unique_ptr<DesktopMediaPicker> CreatePicker(
      const content::MediaStreamRequest*) override {
    return std::make_unique<CancelDesktopPicker>(shown_, destroyed_);
  }
  std::vector<std::unique_ptr<DesktopMediaList>> CreateMediaList(
      const std::vector<DesktopMediaList::Type>&, content::WebContents*,
      DesktopMediaList::WebContentsFilter) override { return {}; }
 private:
  raw_ptr<bool> shown_;
  std::shared_ptr<bool> destroyed_;
};

}  // namespace

class ZitterMediaBrowserTest : public InProcessBrowserTest {
 public:
  ZitterMediaBrowserTest() {
    features_.InitAndDisableFeature(features::kAudioServiceOutOfProcess);
  }
  void SetUpCommandLine(base::CommandLine* command_line) override {
    InProcessBrowserTest::SetUpCommandLine(command_line);
    command_line->AppendSwitch("use-fake-device-for-media-stream");
    // Keep generated test frames in CPU buffers so this permission test does
    // not depend on native GPU-buffer delivery. Consent behavior is identical.
    command_line->AppendSwitch("disable-video-capture-use-gpu-memory-buffer");
    // Deliberately no use-fake-ui-for-media-stream: real Zitter UI is tested.
  }
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    audio_ = std::make_unique<FakeAudioService>();
#if BUILDFLAG(IS_MAC)
    system_permissions_ = std::make_unique<system_permission_settings::SystemMediaPermissionCacheMacTestHelper>();
    system_permissions_->SetCameraStatus(false);
    system_permissions_->SetMicStatus(false);
#endif
    base::test::TestFuture<Profile*> ready;
    auto* manager = g_browser_process->profile_manager();
    base::test::TestFuture<Profile*> work_ready;
    manager->CreateProfileAsync(manager->user_data_dir().AppendASCII("ZitterWork"), work_ready.GetCallback());
    work_ = work_ready.Get();
    ASSERT_TRUE(work_);
    work_lease_ = std::make_unique<ScopedProfileKeepAlive>(work_, ProfileKeepAliveOrigin::kBrowserWindow);
    manager->CreateProfileAsync(manager->user_data_dir().AppendASCII("ZitterPersonal"), ready.GetCallback());
    personal_ = ready.Get();
    ASSERT_TRUE(personal_);
    lease_ = std::make_unique<ScopedProfileKeepAlive>(personal_, ProfileKeepAliveOrigin::kBrowserWindow);
    browser()->GetWindow()->Hide();
    {
      base::ScopedAllowBlockingForTesting files;
      const auto config=manager->user_data_dir().AppendASCII("ZitterWorkspaces.json");
      if(!base::PathExists(config))ASSERT_TRUE(base::WriteFile(config,R"({
        "profiles":[{"id":"work-profile","name":"Profil firmowy","directory":"ZitterWork"},{"id":"personal-profile","name":"Profil osobisty","directory":"ZitterPersonal"}],
        "workspaces":[{"id":"work","name":"Praca","profile_id":"work-profile"},{"id":"personal","name":"Prywatne","profile_id":"personal-profile"}],
        "active":"work","onboarding_complete":true
      })"));
    }
    window_ = std::make_unique<WorkspaceWindow>(work_, personal_);
    ASSERT_TRUE(base::test::RunUntil([&] { return window_->configuration_ready(); }));
    embedded_test_server()->ServeFilesFromSourceDirectory("zitter/testdata");
    ASSERT_TRUE(embedded_test_server()->Start());
    Load();
  }
  void TearDownOnMainThread() override {
    window_.reset();
    lease_.reset();
    work_lease_.reset();
    work_ = nullptr;
    personal_ = nullptr;
    audio_.reset();
#if BUILDFLAG(IS_MAC)
    system_permissions_.reset();
#endif
    InProcessBrowserTest::TearDownOnMainThread();
  }
  void Load(std::string suffix = {}) {
    content::TestNavigationObserver navigation(page());
    window_->Navigate(embedded_test_server()->GetURL("/media_probe.html" + suffix));
    navigation.Wait();
    ASSERT_TRUE(content::WaitForLoadStop(page()));
  }
  content::WebContents* page() { return window_->contents(window_->active_id()); }
  PageMediaAccess* access() { return PageMediaAccess::FromWebContents(page()); }
  void Start(std::string constraints = "{video:true}") {
    ASSERT_TRUE(content::ExecJs(page(), "startCapture(" + constraints + "); void 0;"));
  }
  bool ResultIs(std::string expected) {
    return content::EvalJs(page(), "captureResult").ExtractString() == expected;
  }
  void Press(views::Widget* widget,bool accept,ui::KeyboardCode key=ui::VKEY_SPACE){
    ASSERT_TRUE(widget);auto* button=DialogActionButton(widget,accept);ASSERT_TRUE(button);
    ui_controls::EnableUIControls();ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(widget->GetNativeWindow()));button->RequestFocus();
    base::test::TestFuture<void> presented;auto* compositor=widget->GetCompositor();ASSERT_TRUE(compositor);widget->GetRootView()->SchedulePaint();compositor->RequestSuccessfulPresentationTimeForNextFrame(base::BindOnce([](base::OnceClosure done,const viz::FrameTimingDetails&){std::move(done).Run();},presented.GetCallback()));compositor->ScheduleFullRedraw();ASSERT_TRUE(presented.Wait());ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(widget->GetNativeWindow(),key,false,false,false,false));
  }
  void SetCamera(Profile* profile, ContentSetting setting) {
    auto origin = embedded_test_server()->GetURL("/");
    HostContentSettingsMapFactory::GetForProfile(profile)->SetContentSettingDefaultScope(
        origin, origin, ContentSettingsType::MEDIASTREAM_CAMERA, setting);
  }

 protected:
  raw_ptr<Profile> work_ = nullptr;
  std::unique_ptr<ScopedProfileKeepAlive> work_lease_;
  raw_ptr<Profile> personal_ = nullptr;
  std::unique_ptr<ScopedProfileKeepAlive> lease_;
  std::unique_ptr<WorkspaceWindow> window_;
  base::test::ScopedFeatureList features_;
  std::unique_ptr<FakeAudioService> audio_;
#if BUILDFLAG(IS_MAC)
  std::unique_ptr<system_permission_settings::SystemMediaPermissionCacheMacTestHelper> system_permissions_;
#endif
};

IN_PROC_BROWSER_TEST_F(ZitterMediaBrowserTest, ScenePermissionCocoaDefaultAndForcedCaptureClose) {
  std::string stage="initial permission";
  base::test::ScopedRunLoopTimeout timeout(FROM_HERE,base::Seconds(30),base::BindLambdaForTesting([&]{return "Scene media stage: "+stage;}));
  Start();ASSERT_TRUE(base::test::RunUntil([&]{return access()->permission_prompt()!=nullptr;}));
  auto* widget=access()->permission_prompt();auto* delegate=widget->widget_delegate()->AsDialogDelegate();
  const auto origin=url::Origin::Create(page()->GetLastCommittedURL()).Serialize();
  EXPECT_EQ(ui::mojom::ModalType::kWindow,delegate->GetModalType());EXPECT_EQ(static_cast<int>(ui::mojom::DialogButton::kCancel),delegate->GetDefaultDialogButton());
  auto* allow=DialogActionButton(widget,true);auto* deny=DialogActionButton(widget,false);ASSERT_TRUE(allow);ASSERT_TRUE(deny);
  EXPECT_EQ(u"Zezwól tym razem",delegate->GetDialogButtonLabel(ui::mojom::DialogButton::kOk));
  {
    EXPECT_TRUE(allow->children().empty());EXPECT_TRUE(deny->children().empty());EXPECT_FALSE(views::AsViewClass<views::MdTextButton>(allow));
    if(MetalPresents())ASSERT_TRUE(base::test::RunUntil([&]{return access()->permission_scene_contains_for_testing("Prosi o dostęp: "+origin);}));
    auto check=[&](auto&& self,views::View* view)->void{ui::AXNodeData node;view->GetViewAccessibility().GetAccessibleNodeData(&node);if(node.role==ax::mojom::Role::kStaticText)EXPECT_FALSE(views::AsViewClass<views::Label>(view));for(const auto& child:view->children())self(self,child.get());};check(check,delegate->GetContentsView());
  }
  // macOS deliberately does not bind Return to a default Cancel. It must
  // neither grant the request nor close the prompt as a side effect.
  stage="Return leaves default Cancel unbound";ASSERT_NO_FATAL_FAILURE(Press(widget,false,ui::VKEY_RETURN));base::RunLoop().RunUntilIdle();EXPECT_EQ(widget,access()->permission_prompt());EXPECT_FALSE(PageMediaAccess::IsCapturing(page()));
  ASSERT_NO_FATAL_FAILURE(Press(widget,false,ui::VKEY_ESCAPE));ASSERT_TRUE(base::test::RunUntil([&]{return ResultIs("NotAllowedError");}));EXPECT_FALSE(access()->permission_prompt());if(MetalPresents())EXPECT_FALSE(access()->permission_scene_contains_for_testing("Prosi o dostęp: "+origin));
  stage="allow and wait for page track listeners";SetCamera(work_,CONTENT_SETTING_ASK);Load("?scene-allow");Start();ASSERT_TRUE(base::test::RunUntil([&]{return access()->permission_prompt()!=nullptr;}));
  ASSERT_NO_FATAL_FAILURE(Press(access()->permission_prompt(),true));ASSERT_TRUE(base::test::RunUntil([&]{return ResultIs("running")&&PageMediaAccess::IsCapturing(page())&&access()->capture_prompt();}));
  widget=access()->capture_prompt();delegate=widget->widget_delegate()->AsDialogDelegate();EXPECT_EQ(ui::mojom::ModalType::kNone,delegate->GetModalType());EXPECT_TRUE(delegate->ShouldShowCloseButton());
  auto* stop=DialogActionButton(widget,false);ASSERT_TRUE(stop);EXPECT_FALSE(DialogActionButton(widget,true));
  {EXPECT_TRUE(stop->children().empty());if(MetalPresents())ASSERT_TRUE(base::test::RunUntil([&]{return access()->capture_scene_contains_for_testing(origin);}));EXPECT_FALSE(views::AsViewClass<views::MdTextButton>(stop));}
  stage="forced close and page ended event";views::ViewTracker old(widget->GetContentsView());widget->CloseNow();ASSERT_TRUE(base::test::RunUntil([&]{return !PageMediaAccess::IsCapturing(page())&&!access()->capture_prompt()&&!old.view();}));
  ASSERT_TRUE(base::test::RunUntil([&]{return content::EvalJs(page(),"captureResult === 'ended' && captureStream.getTracks().every(t => t.readyState === 'ended')").ExtractBool();}));if(MetalPresents())EXPECT_FALSE(access()->capture_scene_contains_for_testing(origin));
  // A live same-origin one-time grant can start another stream. Stale close
  // callbacks from the old widget must not stop that new capture generation.
  stage="fresh capture";Start();ASSERT_TRUE(base::test::RunUntil([&]{return ResultIs("running")&&PageMediaAccess::IsCapturing(page())&&access()->capture_prompt();}));
  stage="keyboard stop";ASSERT_NO_FATAL_FAILURE(Press(access()->capture_prompt(),false));ASSERT_TRUE(base::test::RunUntil([&]{return !PageMediaAccess::IsCapturing(page())&&!access()->capture_prompt();}));
}

IN_PROC_BROWSER_TEST_F(ZitterMediaBrowserTest, PermissionConsentProfileIsolationAndStop) {
  std::string stage = "initial denial";
  base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Seconds(30),
      base::BindLambdaForTesting([&] { return "Media stage: " + stage; }));
  ASSERT_TRUE(access());
  EXPECT_FALSE(PageMediaAccess::Check(page()->GetPrimaryMainFrame(),
      url::Origin::Create(page()->GetLastCommittedURL()),
      blink::mojom::MediaStreamType::DEVICE_VIDEO_CAPTURE));
  SetCamera(work_, CONTENT_SETTING_BLOCK);
  Start();
  ASSERT_TRUE(base::test::RunUntil([&] { return ResultIs("NotAllowedError"); }));
  EXPECT_FALSE(access()->permission_prompt());
  EXPECT_FALSE(PageMediaAccess::IsCapturing(page()));
  LOG(ERROR) << "ZITTER_MEDIA_TEST: initial denial complete";

  // The same origin in another profile still asks; default/block cannot leak.
  const auto work = window_->active_id();
  auto spaces = window_->workspaces();
  ASSERT_EQ(spaces.size(), 2u);
  ASSERT_TRUE(window_->Select(spaces[1].id));
  Load();
  Start();
  ASSERT_TRUE(base::test::RunUntil([&] { return access()->permission_prompt(); }));
  auto* dialog = access()->permission_prompt()->widget_delegate()->AsDialogDelegate();
  EXPECT_EQ(dialog->GetDefaultDialogButton(), static_cast<int>(ui::mojom::DialogButton::kCancel));
  EXPECT_FALSE(PageMediaAccess::IsCapturing(page()));
  dialog->CancelDialog();
  ASSERT_TRUE(base::test::RunUntil([&] { return ResultIs("NotAllowedError"); }));
  LOG(ERROR) << "ZITTER_MEDIA_TEST: personal denial complete";

  ASSERT_TRUE(window_->Select(work));
  SetCamera(work_, CONTENT_SETTING_ASK);
  Load("?allowed");
  Start("{video:true,audio:true}");
  stage = "combined permission prompt";
  ASSERT_TRUE(base::test::RunUntil([&] { return access()->permission_prompt(); }));
  access()->permission_prompt()->widget_delegate()->AsDialogDelegate()->AcceptDialog();
  stage = "fake capture starts";
  std::string previous_state;
  size_t accepted_prompts = 1;
  ASSERT_TRUE(base::test::RunUntil([&] {
    // Native users consent after the grouped request has settled. A fast test
    // can accept the first capability before the other request coalesces;
    // explicitly consent to that second expected media prompt as well.
    auto* manager = permissions::PermissionRequestManager::FromWebContents(page());
    auto* prompt = access()->permission_prompt();
    if (manager->GetCurrentPrompt() && prompt && !prompt->IsClosed()) {
      bool media_only = true;
      for (const auto& request : manager->Requests()) {
        media_only &= request->request_type() == permissions::RequestType::kCameraStream ||
            request->request_type() == permissions::RequestType::kMicStream;
      }
      EXPECT_TRUE(media_only);
      EXPECT_LT(accepted_prompts, 2u);
      if (media_only && accepted_prompts < 2) {
        ++accepted_prompts;
        prompt->widget_delegate()->AsDialogDelegate()->AcceptDialog();
      }
    }
    const auto result = content::EvalJs(page(), "captureResult").ExtractString();
    const auto state = result + ", capturing=" + std::to_string(PageMediaAccess::IsCapturing(page())) +
        ", indicator=" + std::to_string(access()->capture_prompt() != nullptr) +
        ", permission=" + std::to_string(access()->permission_prompt() != nullptr);
    if (state != previous_state) {
      LOG(ERROR) << "ZITTER_MEDIA_TEST: " << state;
      previous_state = state;
    }
    return result == "running";
  }));
  stage = "capture indicator registration";
  ASSERT_TRUE(base::test::RunUntil([&] { return PageMediaAccess::IsCapturing(page()); }));
  stage = "capture controls visible";
  ASSERT_TRUE(base::test::RunUntil([&] { return access()->capture_prompt(); }));
  EXPECT_TRUE(PageMediaAccess::IsCapturing(page()));
  LOG(ERROR) << "ZITTER_MEDIA_TEST: fake capture started";
  EXPECT_FALSE(window_->SleepWorkspace(work));
  // Switching workspaces keeps capture controls available and live.
  auto* source = page();
  auto* source_access = access();
  ASSERT_TRUE(window_->Select(spaces[1].id));
  EXPECT_TRUE(source_access->capture_prompt());
  if(MetalPresents())ASSERT_TRUE(base::test::RunUntil([&]{return source_access->capture_scene_contains_for_testing("Zamknięcie tego okna zatrzyma przechwytywanie.");}));
  ASSERT_NO_FATAL_FAILURE(Press(source_access->capture_prompt(),false));
  stage = "capture stops from background controls";
  ASSERT_TRUE(base::test::RunUntil([&] { return !PageMediaAccess::IsCapturing(source); }));
  stage = "browser stop ends both JavaScript tracks";
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(source,
        "captureResult === 'ended' && captureStream.getTracks().every(t => t.readyState === 'ended')")
        .ExtractBool();
  }));
  LOG(ERROR) << "ZITTER_MEDIA_TEST: background capture stopped";
  auto* settings = HostContentSettingsMapFactory::GetForProfile(work_);
  const auto origin = embedded_test_server()->GetURL("/");
  EXPECT_EQ(settings->GetContentSetting(origin, origin, ContentSettingsType::MEDIASTREAM_CAMERA),
      CONTENT_SETTING_ALLOW);
  ASSERT_TRUE(window_->Select(work));
  stage = "navigate to different origin";
  content::TestNavigationObserver different_origin(page());
  window_->Navigate(embedded_test_server()->GetURL("localhost", "/media_probe.html?away"));
  different_origin.Wait();
  stage = "one-time grant revoked after last-origin page leaves";
  ASSERT_TRUE(base::test::RunUntil([&] {
    return settings->GetContentSetting(origin, origin, ContentSettingsType::MEDIASTREAM_CAMERA) ==
        CONTENT_SETTING_ASK;
  }));
  LOG(ERROR) << "ZITTER_MEDIA_TEST: one-time grant revoked";
}

IN_PROC_BROWSER_TEST_F(ZitterMediaBrowserTest, NavigationCancelsPromptAndCaptureTeardown) {
  Start();
  ASSERT_TRUE(base::test::RunUntil([&] { return access()->permission_prompt(); }));
  auto old_prompt = access()->permission_prompt()->GetWeakPtr();
  Load("?navigated");
  ASSERT_TRUE(base::test::RunUntil([&] { return !old_prompt || old_prompt->IsClosed(); }));
  EXPECT_FALSE(PageMediaAccess::IsCapturing(page()));

  // Exercise observer and stop-control lifetime using a synthetic registered
  // stream; no OS device is touched and destroying the tab precedes stream UI.
  blink::mojom::StreamDevices devices;
  devices.audio_device = blink::MediaStreamDevice(
      blink::mojom::MediaStreamType::DEVICE_AUDIO_CAPTURE, "fake", "Fake microphone");
  auto stream_ui = MediaCaptureDevicesDispatcher::GetInstance()->GetMediaStreamCaptureIndicator()
      ->RegisterMediaStream(page(), devices);
  stream_ui->OnStarted(base::DoNothing(), {}, "synthetic", {}, {});
  ASSERT_TRUE(base::test::RunUntil([&] { return access()->capture_prompt(); }));
  auto indicator = access()->capture_prompt()->GetWeakPtr();
  window_.reset();
  stream_ui.reset();
  ASSERT_TRUE(base::test::RunUntil([&] { return !indicator || indicator->IsClosed(); }));
}

IN_PROC_BROWSER_TEST_F(ZitterMediaBrowserTest, CrossOriginFrameCaptureShowsRequestingOrigin) {
  base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Seconds(30));
  const auto child_url = embedded_test_server()->GetURL("localhost", "/media_probe.html?frame");
  ASSERT_TRUE(content::ExecJs(page(), content::JsReplace(
      "let frame = document.createElement('iframe'); frame.allow='camera; microphone';"
      "frame.src=$1; document.body.append(frame);", child_url)));
  ASSERT_TRUE(content::WaitForLoadStop(page()));
  auto* child = content::ChildFrameAt(page()->GetPrimaryMainFrame(), 0);
  ASSERT_TRUE(child);
  ASSERT_TRUE(content::ExecJs(child, "startCapture({video:true}); void 0;"));
  ASSERT_TRUE(base::test::RunUntil([&] { return access()->permission_prompt(); }));
  const auto requester = base::UTF8ToUTF16(url::Origin::Create(child_url).Serialize());
  EXPECT_NE(DialogText(access()->permission_prompt()->GetRootView()).find(requester), std::u16string::npos);
  access()->permission_prompt()->widget_delegate()->AsDialogDelegate()->AcceptDialog();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(child, "captureResult").ExtractString() == "running";
  }));
  ASSERT_TRUE(base::test::RunUntil([&] { return access()->capture_prompt(); }));
  EXPECT_NE(DialogText(access()->capture_prompt()->GetRootView()).find(requester), std::u16string::npos);
  EXPECT_TRUE(PageMediaAccess::IsCapturing(page()));
  ASSERT_TRUE(content::ExecJs(page(), "document.querySelector('iframe').remove();"));
  ASSERT_TRUE(base::test::RunUntil([&] { return !PageMediaAccess::IsCapturing(page()); }));
}

IN_PROC_BROWSER_TEST_F(ZitterMediaBrowserTest, DesktopPickerCancelDoesNotCapture) {
  auto* frame = page()->GetPrimaryMainFrame();
  content::MediaStreamRequest request(frame->GetProcess()->GetDeprecatedID(),
      frame->GetRoutingID(), 1, frame->GetLastCommittedOrigin(), true,
      blink::MEDIA_GENERATE_STREAM, {}, {}, blink::mojom::MediaStreamType::NO_SERVICE,
      blink::mojom::MediaStreamType::DISPLAY_VIDEO_CAPTURE, false, false, false);
  bool shown = false;
  CancelDesktopFactory factory(&shown);
  PageMediaAccess::SetDesktopPickerFactoryForTesting(&factory);
  base::ScopedClosureRunner reset_factory(base::BindOnce(
      &PageMediaAccess::SetDesktopPickerFactoryForTesting, nullptr));
  base::test::TestFuture<blink::mojom::MediaStreamRequestResult> result;
  PageMediaAccess::Request(page(), request, base::BindOnce(
      [](base::OnceCallback<void(blink::mojom::MediaStreamRequestResult)> complete,
          const blink::mojom::StreamDevicesSet& devices,
          blink::mojom::MediaStreamRequestResult status,
          std::unique_ptr<content::MediaStreamUI> ui) {
        EXPECT_TRUE(devices.stream_devices.empty());
        EXPECT_FALSE(ui);
        std::move(complete).Run(status);
      }, result.GetCallback()));
  EXPECT_EQ(result.Get(), blink::mojom::MediaStreamRequestResult::PERMISSION_DENIED);
  EXPECT_TRUE(shown);
  EXPECT_FALSE(PageMediaAccess::IsCapturing(page()));
  EXPECT_FALSE(access()->capture_prompt());
}

IN_PROC_BROWSER_TEST_F(ZitterMediaBrowserTest, TabPickerListsOnlySameProfileAndHonorsFilter) {
  const auto work = window_->active_id();
  auto* first_work_page = page();
  window_->NavigateFromBar(embedded_test_server()->GetURL("/media_probe.html?second"), true);
  ASSERT_TRUE(content::WaitForLoadStop(page()));
  auto* second_work_page = page();
  ASSERT_NE(first_work_page, second_work_page);
  auto spaces = window_->workspaces();
  ASSERT_TRUE(window_->Select(spaces[1].id));
  Load("?personal");
  auto* personal_page = page();
  ASSERT_TRUE(window_->Select(work));
  auto sources = PageMediaAccess::CreateTabSourceList(first_work_page,
      DesktopMediaList::ExcludeWebContents(base::BindRepeating(
          [](content::WebContents*) { return true; }), first_work_page));
  base::test::TestFuture<void> refreshed;
  sources->Update(refreshed.GetCallback());
  ASSERT_TRUE(refreshed.Wait());
  ASSERT_EQ(sources->GetSourceCount(), 1);
  const auto& id = sources->GetSource(0).id.web_contents_id;
  EXPECT_EQ(id.render_process_id, second_work_page->GetPrimaryMainFrame()->GetProcess()->GetDeprecatedID());
  EXPECT_EQ(id.main_render_frame_id, second_work_page->GetPrimaryMainFrame()->GetRoutingID());
  EXPECT_TRUE(id.render_process_id != personal_page->GetPrimaryMainFrame()->GetProcess()->GetDeprecatedID() ||
      id.main_render_frame_id != personal_page->GetPrimaryMainFrame()->GetRoutingID());

  auto blocked = PageMediaAccess::CreateTabSourceList(first_work_page,
      base::BindRepeating([](content::WebContents*) { return false; }));
  base::test::TestFuture<void> filtered;
  blocked->Update(filtered.GetCallback());
  ASSERT_TRUE(filtered.Wait());
  EXPECT_EQ(blocked->GetSourceCount(), 0);
}

IN_PROC_BROWSER_TEST_F(ZitterMediaBrowserTest, PendingScreenPickerCancelsOnDocumentChange) {
  bool shown = false;
  auto destroyed = std::make_shared<bool>(false);
  bool replied = false;
  CancelDesktopFactory factory(&shown, destroyed);
  PageMediaAccess::SetDesktopPickerFactoryForTesting(&factory);
  base::ScopedClosureRunner reset_factory(base::BindOnce(
      &PageMediaAccess::SetDesktopPickerFactoryForTesting, nullptr));
  auto request_screen = [&](content::RenderFrameHost* frame, int id) {
    content::MediaStreamRequest request(frame->GetProcess()->GetDeprecatedID(),
        frame->GetRoutingID(), id, frame->GetLastCommittedOrigin(), true,
        blink::MEDIA_GENERATE_STREAM, {}, {}, blink::mojom::MediaStreamType::NO_SERVICE,
        blink::mojom::MediaStreamType::DISPLAY_VIDEO_CAPTURE, false, false, false);
    PageMediaAccess::Request(page(), request, base::BindLambdaForTesting(
        [&](const blink::mojom::StreamDevicesSet&,
            blink::mojom::MediaStreamRequestResult,
            std::unique_ptr<content::MediaStreamUI>) { replied = true; }));
  };
  request_screen(page()->GetPrimaryMainFrame(), 80);
  ASSERT_TRUE(base::test::RunUntil([&] { return shown; }));
  ASSERT_TRUE(content::ExecJs(page(), "history.pushState({}, '', '#same-document');"));
  EXPECT_FALSE(*destroyed);
  Load("?different-document");
  ASSERT_TRUE(base::test::RunUntil([&] { return *destroyed; }));
  EXPECT_FALSE(replied);

  ASSERT_TRUE(content::ExecJs(page(), content::JsReplace(
      "let child=document.createElement('iframe'); child.src=$1; document.body.append(child);",
      embedded_test_server()->GetURL("/media_probe.html?child"))));
  ASSERT_TRUE(content::WaitForLoadStop(page()));
  auto* child = content::ChildFrameAt(page()->GetPrimaryMainFrame(), 0);
  ASSERT_TRUE(child);
  shown = *destroyed = false;
  request_screen(child, 81);
  ASSERT_TRUE(base::test::RunUntil([&] { return shown; }));
  ASSERT_TRUE(content::ExecJs(page(), "document.querySelector('iframe').remove();"));
  ASSERT_TRUE(base::test::RunUntil([&] { return *destroyed; }));
  EXPECT_FALSE(replied);
  EXPECT_FALSE(PageMediaAccess::IsCapturing(page()));
}

#if BUILDFLAG(IS_MAC)
IN_PROC_BROWSER_TEST_F(ZitterMediaBrowserTest, SiteGrantCannotOverrideMacOSDenial) {
  system_permissions_->SetCameraStatus(true);
  SetCamera(work_, CONTENT_SETTING_ALLOW);
  auto* dispatcher = MediaCaptureDevicesDispatcher::GetInstance();
  dispatcher->DisableDeviceEnumerationForTesting();
  dispatcher->SetTestVideoCaptureDevices({blink::MediaStreamDevice(
      blink::mojom::MediaStreamType::DEVICE_VIDEO_CAPTURE, "fake-denied", "Fake camera")});
  base::ScopedClosureRunner reset_devices(base::BindOnce([] {
    MediaCaptureDevicesDispatcher::GetInstance()->SetTestVideoCaptureDevices({});
  }));
  // Chromium explicitly treats the fake-device CLI as OS-authorized, ahead of
  // its mock OS wrapper. Remove that bypass only for this browser-side request.
  // The enumerator above is synthetic, and no renderer starts/opens a device.
  base::test::ScopedCommandLine command_line;
  command_line.GetProcessCommandLine()->RemoveSwitch("use-fake-device-for-media-stream");
  auto* frame = page()->GetPrimaryMainFrame();
  content::MediaStreamRequest request(frame->GetProcess()->GetDeprecatedID(),
      frame->GetRoutingID(), 92, frame->GetLastCommittedOrigin(), true,
      blink::MEDIA_GENERATE_STREAM, {}, {}, blink::mojom::MediaStreamType::NO_SERVICE,
      blink::mojom::MediaStreamType::DEVICE_VIDEO_CAPTURE, false, false, false);
  base::test::TestFuture<blink::mojom::MediaStreamRequestResult> result;
  PageMediaAccess::Request(page(), request, base::BindOnce(
      [](base::OnceCallback<void(blink::mojom::MediaStreamRequestResult)> complete,
          const blink::mojom::StreamDevicesSet&,
          blink::mojom::MediaStreamRequestResult status,
          std::unique_ptr<content::MediaStreamUI>) { std::move(complete).Run(status); },
      result.GetCallback()));
  EXPECT_EQ(result.Get(), blink::mojom::MediaStreamRequestResult::PERMISSION_DENIED_BY_SYSTEM);
  EXPECT_FALSE(PageMediaAccess::IsCapturing(page()));
  EXPECT_FALSE(access()->capture_prompt());
}
#endif

}  // namespace zitter::prototype
