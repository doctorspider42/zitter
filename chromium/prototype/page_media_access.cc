#include "chromium/prototype/page_media_access.h"
#include "chromium/prototype/browser_data.h"
#include "chromium/prototype/page_dialogs.h"
#include "chromium/prototype/sidebar_layout.h"
#include "build/build_config.h"
#if BUILDFLAG(IS_MAC)
#include "chromium/prototype/workspace_keyboard_mac.h"
#endif
#include "ui/views/layout/layout_provider.h"

#include <utility>
#include <algorithm>
#include <set>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/media/webrtc/media_capture_devices_dispatcher.h"
#include "chrome/browser/media/webrtc/desktop_media_list_base.h"
#include "chrome/browser/media/webrtc/desktop_media_picker_factory_impl.h"
#include "chrome/browser/media/webrtc/display_media_access_handler.h"
#include "chrome/browser/enterprise/data_protection/data_protection_page_user_data.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/permissions/one_time_permissions_tracker_helper.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/permissions/permission_prompt.h"
#include "components/permissions/permission_request.h"
#include "components/permissions/permission_request_manager.h"
#include "components/permissions/permission_uma_constants.h"
#include "components/permissions/permission_util.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/web_contents_media_capture_id.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/models/dialog_model.h"
#include "ui/views/bubble/bubble_dialog_model_host.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"
#include "ui/views/controls/label.h"
#include "url/origin.h"

namespace zitter::prototype {
// Decisions stay synchronous in Chromium. Only adapter destruction is deferred
// so a permission-manager decision or capture-stop notification may delete its
// owner without deleting the executing dialog/button callback itself.
class MediaSceneDialog {
 public:
  ~MediaSceneDialog(){scene.reset();widget.reset();delegate.reset();}
  static void Dispose(std::unique_ptr<MediaSceneDialog> dialog){
    if(!dialog)return;
    dialog->scene.reset();
    if(dialog->widget&&!dialog->widget->IsClosed())dialog->widget->Close();
    base::SingleThreadTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE,std::move(dialog));
  }
  std::unique_ptr<views::DialogDelegate> delegate;
  std::unique_ptr<views::Widget> widget;
  std::unique_ptr<PanelScene> scene;
};
namespace {

void PrepareDialogText(views::View* view) {
  if (auto* label = views::AsViewClass<views::Label>(view))
    label->SetSubpixelRenderingEnabled(false);
  for (const auto& child : view->children()) PrepareDialogText(child);
}

DesktopMediaPickerFactory*& TestPickerFactory() {
  static DesktopMediaPickerFactory* factory = nullptr;
  return factory;
}

// Preserve Chromium's capture UI and every source/region/focus callback while
// associating each active stream with its actual requesting frame's origin.
class AttributedCaptureUI final : public content::MediaStreamUI {
 public:
  AttributedCaptureUI(std::unique_ptr<content::MediaStreamUI> inner,
      base::RepeatingCallback<void(bool)> activity)
      : inner_(std::move(inner)), activity_(std::move(activity)) {}
  ~AttributedCaptureUI() override {
    inner_.reset();
    activity_.Run(false);
  }
  gfx::NativeViewId OnStarted(base::RepeatingClosure stop, SourceCallback source,
      const std::string& label, std::vector<content::DesktopMediaID> ids,
      StateChangeCallback state_change) override {
    activity_.Run(true);
    return inner_->OnStarted(std::move(stop), std::move(source), label,
        std::move(ids), std::move(state_change));
  }
  void OnDeviceStoppedForSourceChange(const std::string& label,
      const content::DesktopMediaID& old_id, const content::DesktopMediaID& new_id,
      bool captured_surface_control_active) override {
    inner_->OnDeviceStoppedForSourceChange(label, old_id, new_id, captured_surface_control_active);
  }
  void OnDeviceStopped(const std::string& label, const content::DesktopMediaID& id) override {
    inner_->OnDeviceStopped(label, id);
  }
  void OnRegionCaptureRectChanged(const std::optional<gfx::Rect>& rect) override {
    inner_->OnRegionCaptureRectChanged(rect);
  }
  void SetFocus(const content::DesktopMediaID& id, bool focus,
      bool microtask, bool timer) override {
    inner_->SetFocus(id, focus, microtask, timer);
  }
 private:
  std::unique_ptr<content::MediaStreamUI> inner_;
  base::RepeatingCallback<void(bool)> activity_;
};

std::vector<base::WeakPtr<content::WebContents>>& ZitterPages() {
  static base::NoDestructor<std::vector<base::WeakPtr<content::WebContents>>> pages;
  return *pages;
}

class ZitterTabSourceList final : public DesktopMediaListBase {
 public:
  ZitterTabSourceList(content::WebContents* requester, WebContentsFilter filter)
      : DesktopMediaListBase(base::Seconds(1)), requester_(requester->GetWeakPtr()),
        filter_(std::move(filter)) { type_ = Type::kWebContents; }
 private:
  void Refresh(bool) override {
    std::vector<SourceDescription> sources;
    if (requester_) {
      std::erase_if(ZitterPages(), [](const auto& page) { return !page; });
      for (const auto& page : ZitterPages()) {
        if (!page || page->IsBeingDestroyed() ||
            page->GetBrowserContext() != requester_->GetBrowserContext() ||
            (!filter_.is_null() && !filter_.Run(page.get()))) continue;
        auto* frame = page->GetPrimaryMainFrame();
        if (!frame || !frame->IsActive() || !frame->IsRenderFrameLive()) continue;
        auto* protection = enterprise_data_protection::DataProtectionPageUserData::GetForPage(
            page->GetPrimaryPage());
        const bool blocked = protection && !protection->settings().allow_screenshots;
        content::DesktopMediaID id(content::DesktopMediaID::TYPE_WEB_CONTENTS,
            content::DesktopMediaID::kNullId, content::WebContentsMediaCaptureId(
                frame->GetProcess()->GetDeprecatedID(), frame->GetRoutingID()));
        auto title = page->GetTitle();
        if (title.empty()) title = base::UTF8ToUTF16(page->GetLastCommittedURL().spec());
        if (title.empty()) title = u"Nowa karta";
        sources.emplace_back(id, title, blocked);
      }
    }
    UpdateSourcesList(sources);
    OnRefreshComplete();
  }
  base::WeakPtr<content::WebContents> requester_;
  WebContentsFilter filter_;
};

class ZitterDesktopPickerFactory final : public DesktopMediaPickerFactoryImpl {
 public:
  std::unique_ptr<DesktopMediaPicker> CreatePicker(
      const content::MediaStreamRequest* request) override {
    return TestPickerFactory() ? TestPickerFactory()->CreatePicker(request) :
        DesktopMediaPickerFactoryImpl::CreatePicker(request);
  }
  std::vector<std::unique_ptr<DesktopMediaList>> CreateMediaList(
      const std::vector<DesktopMediaList::Type>& types, content::WebContents* requester,
      DesktopMediaList::WebContentsFilter filter) override {
    // Keep Chromium's native windows/screens/current-tab implementations and
    // picker, substituting only the list that assumes Chrome's TabStripModel.
    if (TestPickerFactory()) return TestPickerFactory()->CreateMediaList(types, requester, filter);
    auto sources = DesktopMediaPickerFactoryImpl::CreateMediaList(types, requester, filter);
    for (auto& source : sources) {
      if (source->GetMediaListType() == DesktopMediaList::Type::kWebContents)
        source = std::make_unique<ZitterTabSourceList>(requester, filter);
    }
    return sources;
  }
};

class ZitterDisplayMediaHandler final : public DisplayMediaAccessHandler {
 public:
  ZitterDisplayMediaHandler()
      : DisplayMediaAccessHandler(std::make_unique<ZitterDesktopPickerFactory>(), true) {}
  bool SupportsStreamType(content::RenderFrameHost* frame,
      blink::mojom::MediaStreamType type, const extensions::Extension* extension) override {
    auto* page = frame ? content::WebContents::FromRenderFrameHost(frame) : nullptr;
    return page && PageMediaAccess::FromWebContents(page) &&
        DisplayMediaAccessHandler::SupportsStreamType(frame, type, extension);
  }
};

ZitterDisplayMediaHandler*& CustomDisplayHandler() {
  static ZitterDisplayMediaHandler* handler = nullptr;
  return handler;
}

void RegisterZitterDisplayHandler() {
  // Install before the generic display handler so Chromium still owns request
  // cancellation, secure-link state and protected-video output accounting.
  static const bool registered = [] {
    auto handler = std::make_unique<ZitterDisplayMediaHandler>();
    CustomDisplayHandler() = handler.get();
    MediaCaptureDevicesDispatcher::GetInstance()->AddMediaAccessHandler(
        std::move(handler));
    return true;
  }();
  static_cast<void>(registered);
}

// A real PermissionPrompt surface: it changes presentation only. The existing
// manager owns requests, handles navigation/cancellation and records decisions;
// MediaStreamDevicesController still evaluates site/profile/policy/OS consent.
class MediaPermissionPrompt final : public permissions::PermissionPrompt {
 public:
  MediaPermissionPrompt(content::WebContents* contents, Delegate* delegate)
      : state_(std::make_shared<State>(delegate->GetWeakPtr())) {
    std::u16string capabilities;
    std::set<std::string> requesting_origins;
    bool temporary = true;
    for (const auto& request : delegate->Requests()) {
      if (!capabilities.empty()) capabilities += u"\n";
      capabilities += request->GetMessageTextFragment();
      temporary &= permissions::PermissionUtil::DoesSupportTemporaryGrants(
          request->GetContentSettingsType());
      if (auto* frame = content::RenderFrameHost::FromID(request->get_requesting_frame_id()))
        requesting_origins.insert(frame->GetLastCommittedOrigin().Serialize());
    }
    const auto permission_origin = url::Origin::Create(delegate->GetRequestingOrigin()).Serialize();
    const auto embedding_origin = url::Origin::Create(delegate->GetEmbeddingOrigin()).Serialize();
    if (requesting_origins.empty()) requesting_origins.insert(permission_origin);
    std::u16string requesters;
    for (const auto& origin : requesting_origins) {
      if (!requesters.empty()) requesters += u"\n";
      requesters += base::UTF8ToUTF16(origin);
    }
    auto* profile = Profile::FromBrowserContext(contents->GetBrowserContext());
    auto body=std::make_unique<views::View>();
    auto scene=CreatePanelScene(body.get());
    std::unique_ptr<views::DialogDelegate> dialog;
    if(scene){
      owned_=std::make_unique<MediaSceneDialog>();owned_->scene=std::move(scene);
      SetSidebarPanelLayout(body.get(),true,gfx::Insets(),views::LayoutProvider::Get()->GetDistanceMetric(views::DISTANCE_UNRELATED_CONTROL_VERTICAL));
      auto paragraph=[&](std::u16string text){body->AddChildView(owned_->scene->MakeText(std::move(text),true,false,0));};
      paragraph(u"Prosi o dostęp: "+requesters);paragraph(capabilities);paragraph(u"Profil: "+BrowsingProfileName(profile));
      if(!requesting_origins.contains(permission_origin))paragraph(u"Uprawnienie dla: "+base::UTF8ToUTF16(permission_origin));
      if(!requesting_origins.contains(embedding_origin)&&embedding_origin!=permission_origin)paragraph(u"W stronie: "+base::UTF8ToUTF16(embedding_origin));
      dialog=CreateSceneModalDialog(std::move(body),false);dialog->SetTitle(u"Uprawnienia strony");
      dialog->SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk)|static_cast<int>(ui::mojom::DialogButton::kCancel));
      dialog->SetButtonLabel(ui::mojom::DialogButton::kOk,temporary?u"Zezwól tym razem":u"Zezwól");dialog->SetButtonLabel(ui::mojom::DialogButton::kCancel,u"Zablokuj");
      dialog->SetAcceptCallback(base::BindOnce(&State::Decide,state_,temporary?Decision::kAllowOnce:Decision::kAllowPersistent));
      dialog->SetCancelCallback(base::BindOnce(&State::Decide,state_,Decision::kDeny));
      dialog->SetCloseCallback(base::BindOnce(&State::Decide,state_,Decision::kDismiss));
    }else{
    auto builder = ui::DialogModel::Builder();
    builder.SetTitle(u"Uprawnienia strony")
        .AddParagraph(ui::DialogModelLabel(u"Prosi o dostęp: " + requesters))
        .AddParagraph(ui::DialogModelLabel(capabilities))
        .AddParagraph(ui::DialogModelLabel(u"Profil: " +
            BrowsingProfileName(profile)));
    if (!requesting_origins.contains(permission_origin))
      builder.AddParagraph(ui::DialogModelLabel(u"Uprawnienie dla: " + base::UTF8ToUTF16(permission_origin)));
    if (!requesting_origins.contains(embedding_origin) && embedding_origin != permission_origin)
      builder.AddParagraph(ui::DialogModelLabel(u"W stronie: " + base::UTF8ToUTF16(embedding_origin)));
    auto model = builder
        .AddOkButton(base::BindOnce(&State::Decide, state_,
            temporary ? Decision::kAllowOnce : Decision::kAllowPersistent),
            ui::DialogModel::Button::Params().SetLabel(temporary ? u"Zezwól tym razem" : u"Zezwól"))
        .AddCancelButton(base::BindOnce(&State::Decide, state_, Decision::kDeny),
            ui::DialogModel::Button::Params().SetLabel(u"Zablokuj"))
        .SetDialogDestroyingCallback(base::BindOnce(&State::Decide, state_, Decision::kDismiss))
        .Build();
    dialog = views::BubbleDialogModelHost::CreateModal(std::move(model),
        ui::mojom::ModalType::kWindow);
    }
    dialog->SetDefaultButton(static_cast<int>(ui::mojom::DialogButton::kCancel));
    auto* parent = views::Widget::GetWidgetForNativeWindow(contents->GetTopLevelNativeWindow());
    views::Widget* widget=nullptr;
    if(owned_){
      owned_->delegate=std::move(dialog);widget=views::DialogDelegate::CreateDialogWidget(owned_->delegate.get(),contents->GetTopLevelNativeWindow(),parent?parent->GetNativeView():gfx::NativeView());owned_->widget.reset(widget);
      widget->MakeCloseSynchronous(base::BindOnce([](std::shared_ptr<State> state,views::Widget::ClosedReason){state->Decide(Decision::kDismiss);},state_));
    }else widget=views::DialogDelegate::CreateDialogWidget(std::move(dialog),contents->GetTopLevelNativeWindow(),parent?parent->GetNativeView():gfx::NativeView());
    if (parent)
      widget->SetColorModeOverride(parent->color_mode_override());
    PrepareDialogText(widget->GetRootView());
#if BUILDFLAG(IS_MAC)
    InstallWorkspaceKeyboardRouting(widget,true);
#endif
    widget_ = widget->GetWeakPtr();
    widget->Show();if(owned_)owned_->scene->Schedule();
  }
  ~MediaPermissionPrompt() override {
    weak_factory_.InvalidateWeakPtrs();state_->decided=true;
    if(owned_){widget_.reset();MediaSceneDialog::Dispose(std::move(owned_));}
    else if(widget_)widget_->Close();
  }
  base::RepeatingCallback<bool(std::string_view)> SceneQuery(){return base::BindRepeating([](base::WeakPtr<MediaPermissionPrompt> self,std::string_view text){return self&&self->owned_&&self->owned_->scene&&self->owned_->scene->contains_text(text);},weak_factory_.GetWeakPtr());}
  base::WeakPtr<views::Widget> widget() { return widget_; }
  bool UpdateAnchor() override { return true; }
  TabSwitchingBehavior GetTabSwitchingBehavior() override {
    return kDestroyPromptButKeepRequestPending;
  }
  permissions::PermissionPromptDisposition GetPromptDisposition() const override {
    return permissions::PermissionPromptDisposition::CUSTOM_MODAL_DIALOG;
  }
  bool IsAskPrompt() const override { return true; }
  std::optional<gfx::Rect> GetViewBoundsInScreen() const override {
    return widget_ ? std::optional(widget_->GetWindowBoundsInScreen()) : std::nullopt;
  }
  std::vector<permissions::ElementAnchoredBubbleVariant> GetPromptVariants() const override { return {}; }
  std::optional<permissions::feature_params::PermissionElementPromptPosition>
  GetPromptPosition() const override { return std::nullopt; }

 private:
  enum class Decision { kAllowOnce, kAllowPersistent, kDeny, kDismiss };
  struct State {
    explicit State(base::WeakPtr<Delegate> value) : delegate(value) {}
    void Decide(Decision decision) {
      if (std::exchange(decided, true) || !delegate) return;
      // A decision can synchronously destroy both this prompt and its widget.
      auto target = delegate;
      if (decision == Decision::kAllowOnce) target->AcceptThisTime(std::monostate());
      else if (decision == Decision::kAllowPersistent) target->Accept(std::monostate());
      else if (decision == Decision::kDeny) target->Deny(std::monostate());
      else target->Dismiss(std::monostate());
    }
    base::WeakPtr<Delegate> delegate;
    bool decided = false;
  };
  std::shared_ptr<State> state_;
  std::unique_ptr<MediaSceneDialog> owned_;
  base::WeakPtr<views::Widget> widget_;
  base::WeakPtrFactory<MediaPermissionPrompt> weak_factory_{this};
};

scoped_refptr<MediaStreamCaptureIndicator> Indicator() {
  return MediaCaptureDevicesDispatcher::GetInstance()->GetMediaStreamCaptureIndicator();
}

}  // namespace

WEB_CONTENTS_USER_DATA_KEY_IMPL(PageMediaAccess);

void PageMediaAccess::Attach(content::WebContents* contents) {
  CreateForWebContents(contents);
}

PageMediaAccess::PageMediaAccess(content::WebContents* contents)
    : content::WebContentsObserver(contents),
      content::WebContentsUserData<PageMediaAccess>(*contents) {
  RegisterZitterDisplayHandler();
  ZitterPages().push_back(contents->GetWeakPtr());
  one_time_permissions_ = std::make_unique<OneTimePermissionsTrackerHelper>(contents);
  permissions::PermissionRequestManager::CreateForWebContents(contents);
  // Chromium exposes its per-WebContents prompt factory only under this API
  // name. It merely sets view_factory_; no test response or permission bypass
  // is enabled. The global ChromePermissionsClient factory assumes Chrome's
  // BrowserWindow/location bar, neither of which exists for Zitter WebViews.
  permissions::PermissionRequestManager::FromWebContents(contents)
      ->set_view_factory_for_testing(base::BindRepeating(
          [](base::WeakPtr<PageMediaAccess> access, content::WebContents* page,
             permissions::PermissionPrompt::Delegate* delegate)
              -> std::unique_ptr<permissions::PermissionPrompt> {
            auto prompt = std::make_unique<MediaPermissionPrompt>(page, delegate);
            if(access){access->permission_prompt_=prompt->widget();access->permission_scene_query_=prompt->SceneQuery();}
            return prompt;
          }, weak_factory_.GetWeakPtr()));
  Indicator()->AddObserver(this);
}

PageMediaAccess::~PageMediaAccess() {
  weak_factory_.InvalidateWeakPtrs();
  if (observing_capture_) Indicator()->RemoveObserver(this);
  CloseCaptureIndicator();
}

void PageMediaAccess::Request(content::WebContents* contents,
    const content::MediaStreamRequest& request, content::MediaResponseCallback callback) {
  Attach(contents);
  auto* access = FromWebContents(contents);
  auto origin = request.url_origin.Serialize();
  const auto id = ++access->next_capture_id_;
  auto* frame = content::RenderFrameHost::FromID(request.render_process_id, request.render_frame_id);
  if (CustomDisplayHandler()->SupportsStreamType(frame, request.video_type, nullptr) ||
      CustomDisplayHandler()->SupportsStreamType(frame, request.audio_type, nullptr))
    access->display_requests_.emplace(id, DisplayRequest{request});
  auto attributed = base::BindOnce(
      [](base::WeakPtr<PageMediaAccess> access, size_t id, std::string origin,
          content::MediaResponseCallback callback,
          const blink::mojom::StreamDevicesSet& devices,
          blink::mojom::MediaStreamRequestResult result,
          std::unique_ptr<content::MediaStreamUI> ui) {
        std::optional<content::MediaStreamRequest> display_request;
        if (access) {
          auto entry = access->display_requests_.find(id);
          if (entry != access->display_requests_.end()) {
            if (result == blink::mojom::MediaStreamRequestResult::OK && ui) {
              entry->second.approved = true;
              display_request = entry->second.request;
            }
            else access->display_requests_.erase(entry);
          }
        }
        if (access && ui && result == blink::mojom::MediaStreamRequestResult::OK) {
          ui = std::make_unique<AttributedCaptureUI>(std::move(ui),
              base::BindRepeating([](base::WeakPtr<PageMediaAccess> access,
                  size_t id, std::string origin,
                  std::optional<content::MediaStreamRequest> request, bool started) {
                if (access) access->TrackCapture(id, std::move(origin), started);
                else if (!started && request) {
                  // The requesting page can die before the stream UI. Its
                  // capture accounting must survive until real stream teardown.
                  CustomDisplayHandler()->UpdateMediaRequestState(request->render_process_id,
                      request->render_frame_id, request->page_request_id, request->video_type,
                      content::MEDIA_REQUEST_STATE_CLOSING);
                }
              }, access, id, std::move(origin), std::move(display_request)));
        }
        std::move(callback).Run(devices, result, std::move(ui));
      }, access->weak_factory_.GetWeakPtr(), id, std::move(origin), std::move(callback));
  MediaCaptureDevicesDispatcher::GetInstance()->ProcessMediaAccessRequest(
      contents, request, std::move(attributed), nullptr);
}

void PageMediaAccess::SetDesktopPickerFactoryForTesting(DesktopMediaPickerFactory* factory) {
  TestPickerFactory() = factory;
}

std::unique_ptr<DesktopMediaList> PageMediaAccess::CreateTabSourceList(
    content::WebContents* requester, DesktopMediaList::WebContentsFilter filter) {
  return std::make_unique<ZitterTabSourceList>(requester, std::move(filter));
}

bool PageMediaAccess::Check(content::RenderFrameHost* frame,
    const url::Origin& origin, blink::mojom::MediaStreamType type) {
  return frame && MediaCaptureDevicesDispatcher::GetInstance()
      ->CheckMediaAccessPermission(frame, origin, type);
}

bool PageMediaAccess::IsCapturing(content::WebContents* contents) {
  auto indicator = Indicator();
  return indicator->IsCapturingUserMedia(contents) || indicator->IsCapturingTab(contents) ||
      indicator->IsCapturingWindow(contents) || indicator->IsCapturingDisplay(contents);
}

void PageMediaAccess::Stop(content::WebContents* contents) {
  if (!contents) return;
  Indicator()->StopMediaCapturing(contents,
      MediaStreamCaptureIndicator::kUserMedia | MediaStreamCaptureIndicator::kDisplayMedia);
}

void PageMediaAccess::QueueCaptureUpdate(content::WebContents* contents) {
  if (contents != web_contents() || update_pending_) return;
  update_pending_ = true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
      base::BindOnce(&PageMediaAccess::UpdateCaptureIndicator, weak_factory_.GetWeakPtr()));
}
void PageMediaAccess::OnIsCapturingVideoChanged(content::WebContents* page, bool) { QueueCaptureUpdate(page); }
void PageMediaAccess::OnIsCapturingAudioChanged(content::WebContents* page, bool) { QueueCaptureUpdate(page); }
void PageMediaAccess::OnIsCapturingTabChanged(content::WebContents* page, bool) { QueueCaptureUpdate(page); }
void PageMediaAccess::OnIsCapturingWindowChanged(content::WebContents* page, bool) { QueueCaptureUpdate(page); }
void PageMediaAccess::OnIsCapturingDisplayChanged(content::WebContents* page, bool) { QueueCaptureUpdate(page); }

void PageMediaAccess::UpdateCaptureIndicator() {
  update_pending_ = false;
  if (!web_contents() || !IsCapturing(web_contents())) {
    CloseCaptureIndicator();
    return;
  }
  if (capture_prompt_) return;
  auto weak = weak_factory_.GetWeakPtr();
  capture_dialog_active_ = std::make_shared<bool>(true);
  auto stop = base::BindRepeating([](base::WeakPtr<PageMediaAccess> access,
                                   std::shared_ptr<bool> active) {
    if (std::exchange(*active, false) && access && access->web_contents())
      Stop(access->web_contents());
  }, weak, capture_dialog_active_);
  auto* profile = Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  std::u16string origins;
  for (const auto& [id, origin] : capture_origins_) {
    if (!origins.empty()) origins += u"\n";
    origins += base::UTF8ToUTF16(origin);
  }
  if (origins.empty()) origins = base::UTF8ToUTF16(
      url::Origin::Create(web_contents()->GetLastCommittedURL()).Serialize());
  auto body=std::make_unique<views::View>();auto scene=CreatePanelScene(body.get());
  if(scene){
    capture_scene_=std::make_unique<MediaSceneDialog>();capture_scene_->scene=std::move(scene);
    SetSidebarPanelLayout(body.get(),true,gfx::Insets(),views::LayoutProvider::Get()->GetDistanceMetric(views::DISTANCE_UNRELATED_CONTROL_VERTICAL));
    for(auto text:{origins,std::u16string(u"Ta strona korzysta z mikrofonu, kamery lub udostępniania obrazu."),u"Profil: "+BrowsingProfileName(profile),std::u16string(u"Zamknięcie tego okna zatrzyma przechwytywanie.")})
      body->AddChildView(capture_scene_->scene->MakeText(std::move(text),true,false,0));
    auto dialog=CreateSceneModalDialog(std::move(body),false,false);dialog->SetTitle(u"Trwa przechwytywanie");dialog->SetButtons(static_cast<int>(ui::mojom::DialogButton::kCancel));dialog->SetButtonLabel(ui::mojom::DialogButton::kCancel,u"Zatrzymaj");
    dialog->SetCancelCallback(stop);dialog->SetCloseCallback(stop);
    capture_scene_->delegate=std::move(dialog);
    auto* parent=views::Widget::GetWidgetForNativeWindow(web_contents()->GetTopLevelNativeWindow());
    auto* widget=views::DialogDelegate::CreateDialogWidget(capture_scene_->delegate.get(),web_contents()->GetTopLevelNativeWindow(),parent?parent->GetNativeView():gfx::NativeView());capture_scene_->widget.reset(widget);
    widget->MakeCloseSynchronous(base::BindOnce([](base::WeakPtr<PageMediaAccess> access,std::shared_ptr<bool> active,base::RepeatingClosure stop,views::Widget::ClosedReason){stop.Run();if(access&&access->capture_dialog_active_==active)access->CloseCaptureIndicator();},weak,capture_dialog_active_,stop));
    if(parent)widget->SetColorModeOverride(parent->color_mode_override());
#if BUILDFLAG(IS_MAC)
    InstallWorkspaceKeyboardRouting(widget,true);
#endif
    capture_prompt_=widget->GetWeakPtr();widget->ShowInactive();capture_scene_->scene->Schedule();return;
  }
  auto model = ui::DialogModel::Builder()
      .SetTitle(u"Trwa przechwytywanie")
      .AddParagraph(ui::DialogModelLabel(origins))
      .AddParagraph(ui::DialogModelLabel(u"Ta strona korzysta z mikrofonu, kamery lub udostępniania obrazu."))
      .AddParagraph(ui::DialogModelLabel(u"Profil: " +
          BrowsingProfileName(profile)))
      .AddParagraph(ui::DialogModelLabel(u"Zamknięcie tego okna zatrzyma przechwytywanie."))
      .AddCancelButton(stop, ui::DialogModel::Button::Params().SetLabel(u"Zatrzymaj"))
      .SetDialogDestroyingCallback(stop)
      .Build();
  auto dialog = std::make_unique<views::BubbleDialogModelHost>(
      std::move(model), views::BubbleAnchor(), views::BubbleBorder::Arrow::NONE);
  // Clicking the meeting or switching spaces must not close the stop controls.
  dialog->set_close_on_deactivate(false);
  auto* parent = views::Widget::GetWidgetForNativeWindow(web_contents()->GetTopLevelNativeWindow());
  auto* widget = views::DialogDelegate::CreateDialogWidget(std::move(dialog),
      web_contents()->GetTopLevelNativeWindow(), parent ? parent->GetNativeView() : gfx::NativeView());
  if (parent)
    widget->SetColorModeOverride(parent->color_mode_override());
  PrepareDialogText(widget->GetRootView());
#if BUILDFLAG(IS_MAC)
  InstallWorkspaceKeyboardRouting(widget,true);
#endif
  capture_prompt_ = widget->GetWeakPtr();
  widget->ShowInactive();
}

void PageMediaAccess::TrackCapture(size_t id, std::string origin, bool started) {
  if (started) capture_origins_[id] = std::move(origin);
  else {
    capture_origins_.erase(id);
    auto entry = display_requests_.find(id);
    if (entry != display_requests_.end()) {
      const auto request = entry->second.request;
      display_requests_.erase(entry);
      // RFH may already be gone, in which case the dispatcher cannot select
      // our handler by SupportsStreamType. Remove its security session only
      // now, after the upstream stream UI has actually been released.
      CustomDisplayHandler()->UpdateMediaRequestState(request.render_process_id,
          request.render_frame_id, request.page_request_id, request.video_type,
          content::MEDIA_REQUEST_STATE_CLOSING);
    }
  }
  CloseCaptureIndicator();
  QueueCaptureUpdate(web_contents());
}

void PageMediaAccess::CancelPendingDisplayRequests(content::RenderFrameHost* frame) {
  if (!frame) return;
  std::vector<content::MediaStreamRequest> cancelled;
  std::erase_if(display_requests_, [&](const auto& entry) {
    const auto& record = entry.second;
    if (record.approved || record.request.render_process_id != frame->GetProcess()->GetDeprecatedID() ||
        record.request.render_frame_id != frame->GetRoutingID()) return false;
    cancelled.push_back(record.request);
    return true;
  });
  for (const auto& request : cancelled)
    CustomDisplayHandler()->UpdateMediaRequestState(request.render_process_id,
        request.render_frame_id, request.page_request_id, request.video_type,
        content::MEDIA_REQUEST_STATE_CLOSING);
}
void PageMediaAccess::RenderFrameDeleted(content::RenderFrameHost* frame) {
  CancelPendingDisplayRequests(frame);
}
void PageMediaAccess::RenderFrameHostChanged(content::RenderFrameHost* old_frame,
    content::RenderFrameHost*) {
  CancelPendingDisplayRequests(old_frame);
}
void PageMediaAccess::DidFinishNavigation(content::NavigationHandle* navigation) {
  if (navigation->HasCommitted() && !navigation->IsSameDocument())
    CancelPendingDisplayRequests(navigation->GetRenderFrameHost());
}

void PageMediaAccess::CloseCaptureIndicator() {
  if (capture_dialog_active_) *capture_dialog_active_ = false;
  capture_dialog_active_.reset();
  auto widget = capture_prompt_;
  capture_prompt_.reset();
  if(capture_scene_)MediaSceneDialog::Dispose(std::move(capture_scene_));
  else if(widget)widget->Close();
}
bool PageMediaAccess::permission_scene_contains_for_testing(std::string_view text) const{return permission_scene_query_&&permission_scene_query_.Run(text);}
bool PageMediaAccess::capture_scene_contains_for_testing(std::string_view text) const{return capture_scene_&&capture_scene_->scene&&capture_scene_->scene->contains_text(text);}
void PageMediaAccess::WebContentsDestroyed() {
  std::erase_if(ZitterPages(), [this](const auto& page) { return !page || page.get() == web_contents(); });
  weak_factory_.InvalidateWeakPtrs();
  if (observing_capture_) {
    Indicator()->RemoveObserver(this);
    observing_capture_ = false;
  }
  CloseCaptureIndicator();
}

}  // namespace zitter::prototype
