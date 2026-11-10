#include "chromium/prototype/page_media_access.h"
#include "chromium/prototype/browser_data.h"
#include "chromium/prototype/i18n.h"
#include "chromium/prototype/page_dialogs.h"
#include "chromium/prototype/sidebar_layout.h"
#include "build/build_config.h"
#if BUILDFLAG(IS_MAC)
#include "chromium/prototype/workspace_keyboard_mac.h"
#endif
#include "ui/views/layout/layout_provider.h"

#include <array>
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
#include "base/timer/timer.h"
#include "chrome/browser/media/prefs/capture_device_ranking.h"
#include "components/media_effects/media_device_info.h"
#include "base/i18n/time_formatting.h"
#include "components/url_formatter/elide_url.h"
#include "components/vector_icons/vector_icons.h"
#include "chromium/prototype/sidebar_metal.h"

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
        if (title.empty()) title = T(u"Nowa karta");
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
    contents_=contents->GetWeakPtr();
    if(scene){
      // Design: Prośba o dostęp — a quiet card under the address pill. The page
      // keeps focus and input; Esc or the pill's icon closes or reopens it.
      owned_=std::make_unique<MediaSceneDialog>();owned_->scene=std::move(scene);auto* panel=owned_->scene.get();
      bool camera=false,mic=false;
      for(const auto& request:delegate->Requests()){
        const auto type=request->request_type();
        camera|=type==permissions::RequestType::kCameraStream;mic|=type==permissions::RequestType::kMicStream;
      }
      offer_.kind=camera&&mic?5:camera?3:mic?4:0;
      // A single request of a kind people know gets its own question.
      if(!camera&&!mic&&delegate->Requests().size()==1)switch(delegate->Requests().front()->request_type()){
        case permissions::RequestType::kNotifications:offer_.kind=1;break;
        case permissions::RequestType::kGeolocation:offer_.kind=2;break;
        case permissions::RequestType::kLocalNetwork:case permissions::RequestType::kLoopbackNetwork:offer_.kind=6;break;
        case permissions::RequestType::kClipboard:offer_.kind=7;break;
        case permissions::RequestType::kMultipleDownloads:offer_.kind=8;break;
        case permissions::RequestType::kMidiSysex:offer_.kind=9;break;
        default:break;
      }
      // The frame that asks; an embedded one also names its page below.
      offer_.site=url_formatter::FormatOriginForSecurityDisplay(url::Origin::Create(GURL(requesting_origins.size()==1?*requesting_origins.begin():permission_origin)),url_formatter::SchemeDisplay::OMIT_HTTP_AND_HTTPS);
      static constexpr std::array<const char16_t*,10> kQuestions={u"Zezwolić tej witrynie?",u"Wysyłać powiadomienia?",u"Udostępnić lokalizację?",u"Użyć kamery?",u"Użyć mikrofonu?",u"Użyć kamery i mikrofonu?",
          u"Łączyć się z urządzeniami w Twojej sieci?",u"Czytać i zmieniać schowek?",u"Pobrać wiele plików naraz?",u"Sterować urządzeniami MIDI?"};
      // What the less familiar kinds mean, in a line.
      static constexpr std::array<const char16_t*,10> kMeanings={nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,
          u"Drukarka, router, dysk sieciowy. Zwykła strona tego nie potrzebuje.",u"Strona zobaczy to, co skopiujesz.",u"Strona chce zapisać kilka plików jeden po drugim.",u"Instrumenty i kontrolery, także ich ustawienia."};
      SetSidebarPanelLayout(body.get(),true,gfx::Insets(),10);
      auto* head=body->AddChildView(std::make_unique<views::View>());
      head->AddChildView(panel->MakeIconTile(&PermissionIcon(offer_.kind,false)));
      auto* lines=head->AddChildView(std::make_unique<views::View>());
      auto* question=lines->AddChildView(panel->MakeText(T(kQuestions[static_cast<size_t>(offer_.kind)]),true,false,0));panel->SetTextStyle(question,4,PanelScene::Tone::kInk);
      auto* site=lines->AddChildView(panel->MakeText(offer_.site,true,false,0));panel->SetTextStyle(site,8,PanelScene::Tone::kMuted);
      // Other kinds name what is asked for; an embedded requester names its page.
      if(const auto* meaning=kMeanings[static_cast<size_t>(offer_.kind)]){auto* line=lines->AddChildView(panel->MakeText(T(meaning),true,false,0));panel->SetTextStyle(line,6,PanelScene::Tone::kMuted);}
      if(offer_.kind==0)for(const auto& request:delegate->Requests()){auto* line=lines->AddChildView(panel->MakeText(request->GetMessageTextFragment(),true,false,0));panel->SetTextStyle(line,5,PanelScene::Tone::kInk);}
      if(!requesting_origins.contains(embedding_origin)){
        auto* line=lines->AddChildView(panel->MakeText(TF(u"w stronie {}",{url_formatter::FormatOriginForSecurityDisplay(url::Origin::Create(delegate->GetEmbeddingOrigin()),url_formatter::SchemeDisplay::OMIT_HTTP_AND_HTTPS)}),true,false,0));
        panel->SetTextStyle(line,6,PanelScene::Tone::kMuted);}
      SetSidebarPanelLayout(lines,true,gfx::Insets(),2);
      SetSidebarPanelLayout(head,false,gfx::Insets(),10,lines,true);
      if(camera)AddDevices(body.get(),panel,true,profile);
      if(mic)AddDevices(body.get(),panel,false,profile);
      // Nie zezwalaj (quiet) · Tym razem (outline) · Zawsze tutaj / Zezwól (primary).
      auto* row=body->AddChildView(std::make_unique<views::View>());
      auto* filler=row->AddChildView(std::make_unique<views::View>());
      auto actions=panel->MakeActions(6);auto* group=row->AddChildView(std::move(actions));
      auto decide=[&](std::u16string text,Decision decision,uint32_t kind){
        auto* button=panel->AddAction(group,base::BindRepeating([](std::shared_ptr<State> state,Decision decision){state->Decide(decision);},state_,decision),std::move(text));
        panel->SetActionKind(button,kind);};
      decide(T(u"Nie zezwalaj"),Decision::kDeny,3);
      if(temporary){decide(T(u"Tym razem"),Decision::kAllowOnce,2);decide(T(u"Zawsze tutaj"),Decision::kAllowPersistent,0);}
      else decide(T(u"Zezwól"),Decision::kAllowPersistent,0);
      SetSidebarPanelLayout(row,false,gfx::Insets(),0,filler,true);
      dialog=CreateSceneModalDialog(std::move(body),false,false);
      dialog->SetTitle(T(kQuestions[static_cast<size_t>(offer_.kind)]));dialog->SetShowTitle(false);dialog->SetShowCloseButton(false);
      // 300 px by design, wider when the decisions' labels need it (English
      // "Always here" does): a cut-off button label hides what it does.
      const int fitted=std::max(300,group->GetPreferredSize().width()+24);
      if(auto* bubble=dialog->AsBubbleDialogDelegate()){bubble->set_fixed_width(fitted);bubble->set_margins(gfx::Insets::VH(12,0));}
      // Design R1: the card's content sits 12 px in from its edge.
      SetSidebarPanelLayout(dialog->GetContentsView(),true,gfx::Insets::VH(0,12),0);
      dialog->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
      dialog->SetCloseCallback(base::BindOnce(&State::Decide,state_,Decision::kDismiss));
    }else{
    auto builder = ui::DialogModel::Builder();
    builder.SetTitle(T(u"Uprawnienia strony"))
        .AddParagraph(ui::DialogModelLabel(TF(u"Prosi o dostęp: {}", {requesters})))
        .AddParagraph(ui::DialogModelLabel(capabilities))
        .AddParagraph(ui::DialogModelLabel(TF(u"Profil: {}",
            {BrowsingProfileName(profile)})));
    if (!requesting_origins.contains(permission_origin))
      builder.AddParagraph(ui::DialogModelLabel(TF(u"Uprawnienie dla: {}", {base::UTF8ToUTF16(permission_origin)})));
    if (!requesting_origins.contains(embedding_origin) && embedding_origin != permission_origin)
      builder.AddParagraph(ui::DialogModelLabel(TF(u"W stronie: {}", {base::UTF8ToUTF16(embedding_origin)})));
    auto model = builder
        .AddOkButton(base::BindOnce(&State::Decide, state_,
            temporary ? Decision::kAllowOnce : Decision::kAllowPersistent),
            ui::DialogModel::Button::Params().SetLabel(temporary ? T(u"Zezwól tym razem") : T(u"Zezwól")))
        .AddCancelButton(base::BindOnce(&State::Decide, state_, Decision::kDeny),
            ui::DialogModel::Button::Params().SetLabel(T(u"Zablokuj")))
        .SetDialogDestroyingCallback(base::BindOnce(&State::Decide, state_, Decision::kDismiss))
        .Build();
    dialog = views::BubbleDialogModelHost::CreateModal(std::move(model),
        ui::mojom::ModalType::kWindow);
    }
    if(!owned_)dialog->SetDefaultButton(static_cast<int>(ui::mojom::DialogButton::kCancel));
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
    if(owned_){
      if(auto* access=PageMediaAccess::FromWebContents(contents))access->SetPromptHooks(
          base::BindRepeating(&MediaPermissionPrompt::Reopen,weak_factory_.GetWeakPtr()),
          base::BindRepeating(&MediaPermissionPrompt::Fold,weak_factory_.GetWeakPtr()),
          base::BindRepeating([](std::shared_ptr<State> state,int decision){state->Decide(decision==0?Decision::kAllowOnce:decision==1?Decision::kAllowPersistent:Decision::kDeny);},state_));
      offer_.pending=true;Reopen();
    }else widget->Show();
  }
  ~MediaPermissionPrompt() override {
    fold_timer_.Stop();
    if(owned_&&contents_)if(auto* access=PageMediaAccess::FromWebContents(contents_.get())){access->SetPromptHooks({},{},{});access->ReportPrompt({});}
    weak_factory_.InvalidateWeakPtrs();state_->decided=true;
    if(owned_){widget_.reset();MediaSceneDialog::Dispose(std::move(owned_));}
    else if(widget_)widget_->Close();
  }
  base::RepeatingCallback<bool(std::string_view)> SceneQuery(){return base::BindRepeating([](base::WeakPtr<MediaPermissionPrompt> self,std::string_view text){return self&&self->owned_&&self->owned_->scene&&self->owned_->scene->contains_text(text);},weak_factory_.GetWeakPtr());}
  base::WeakPtr<views::Widget> widget() { return widget_; }
  // Under the address pill when the sidebar shows it; else the page's upper left.
  void Place(){
    if(!widget_||!contents_)return;auto* access=PageMediaAccess::FromWebContents(contents_.get());
    const auto anchor=access?access->PromptAnchor():gfx::Rect();auto bounds=widget_->GetWindowBoundsInScreen();
    if(!anchor.IsEmpty())bounds.set_origin(gfx::Point(anchor.x(),anchor.bottom()+6));
    else if(auto* parent=views::Widget::GetWidgetForNativeWindow(contents_->GetTopLevelNativeWindow())){const auto page=parent->GetClientAreaBoundsInScreen();bounds.set_origin(gfx::Point(page.x()+16,page.y()+16));}
    // An anchorless bubble hangs centred below its anchor point.
    if(auto* bubble=widget_->widget_delegate()->AsBubbleDialogDelegate())bubble->SetAnchorRect(gfx::Rect(bounds.x()+bounds.width()/2,bounds.y(),0,0));
    else widget_->SetBounds(bounds);
  }
  void Reopen(){
    if(!widget_||!owned_)return;Place();widget_->ShowInactive();owned_->scene->Schedule();folded_=false;
    // Camera and microphone hold the page up: that card waits for an answer.
    if(offer_.kind<3||offer_.kind>5)fold_timer_.Start(FROM_HERE,base::Seconds(8),base::BindOnce(&MediaPermissionPrompt::Fold,weak_factory_.GetWeakPtr()));
    Report();
  }
  void Fold(){if(!widget_||!owned_)return;fold_timer_.Stop();widget_->Hide();folded_=true;Report();}
  void Report(){offer_.folded=folded_;if(contents_)if(auto* access=PageMediaAccess::FromWebContents(contents_.get()))access->ReportPrompt(offer_);}
  void AddDevices(views::View* body,PanelScene* panel,bool video,Profile* profile){
    auto* prefs=profile->GetPrefs();std::vector<std::u16string> labels;
    auto* info=media_effects::MediaDeviceInfo::GetInstance();
    if(video){
      if(const auto& devices=info->GetVideoDeviceInfos()){video_devices_=*devices;media_prefs::PreferenceRankVideoDeviceInfos(*prefs,video_devices_);}
      for(const auto& device:video_devices_)labels.push_back(base::UTF8ToUTF16(device.descriptor.GetNameAndModel()));
    }else{
      if(const auto& devices=info->GetAudioDeviceInfos()){
        audio_devices_=*devices;const auto real=media_effects::GetRealDefaultDeviceId(audio_devices_);
        // The "default" alias duplicates the real default device.
        if(real)std::erase_if(audio_devices_,[](const auto& device){return media::AudioDeviceDescription::IsDefaultDevice(device.unique_id);});
        media::AudioDeviceDescription::LocalizeDeviceDescriptions(&audio_devices_);media_prefs::PreferenceRankAudioDeviceInfos(*prefs,audio_devices_);}
      for(const auto& device:audio_devices_)labels.push_back(base::UTF8ToUTF16(device.device_name));
    }
    if(labels.size()<2)return;  // Nothing to choose between.
    auto select=panel->MakeSelect(body,std::move(labels),video?T(u"Kamera"):T(u"Mikrofon"));auto* view=body->AddChildView(std::move(select));
    panel->SetSelectIndex(view,0);
    panel->SetSelectChanged(view,base::BindRepeating([](base::WeakPtr<MediaPermissionPrompt> self,views::View* view,bool video,PrefService* prefs){
      if(!self||!self->owned_)return;const auto index=self->owned_->scene->SelectIndex(view);if(!index)return;
      if(video&&*index<self->video_devices_.size())media_prefs::UpdateVideoDevicePreferenceRanking(*prefs,self->video_devices_.begin()+*index,self->video_devices_);
      if(!video&&*index<self->audio_devices_.size())media_prefs::UpdateAudioDevicePreferenceRanking(*prefs,self->audio_devices_.begin()+*index,self->audio_devices_);
    },weak_factory_.GetWeakPtr(),view,video,prefs));
  }
  static const gfx::VectorIcon& PermissionIcon(int kind,bool off){
    switch(kind){case 1:return off?vector_icons::kNotificationsOffIcon:vector_icons::kNotificationsIcon;case 2:return off?vector_icons::kLocationOffIcon:vector_icons::kLocationOnIcon;
      case 3:case 5:return off?vector_icons::kVideocamOffIcon:vector_icons::kVideocamIcon;case 4:return off?vector_icons::kMicOffIcon:vector_icons::kMicIcon;
      case 6:return off?vector_icons::kRouterOffIcon:vector_icons::kRouterIcon;case 7:return off?vector_icons::kContentPasteOffIcon:vector_icons::kContentPasteIcon;
      case 8:return off?vector_icons::kFileDownloadOffIcon:vector_icons::kDownloadIcon;case 9:return off?vector_icons::kPianoOffIcon:vector_icons::kPianoIcon;default:return vector_icons::kTuneIcon;}
  }
  bool UpdateAnchor() override { if(owned_&&widget_&&widget_->IsVisible())Place(); return true; }
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
  base::WeakPtr<content::WebContents> contents_;
  PermissionOffer offer_;
  bool folded_ = false;
  base::OneShotTimer fold_timer_;
  std::vector<media::VideoCaptureDeviceInfo> video_devices_;
  std::vector<media::AudioDeviceDescription> audio_devices_;
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

CaptureState PageMediaAccess::capture_state() const {
  CaptureState state;
  auto* page = web_contents();
  if (!page || !IsCapturing(page)) return state;
  auto indicator = Indicator();
  state.active = true;
  state.camera = indicator->IsCapturingVideo(page);
  state.microphone = indicator->IsCapturingAudio(page);
  state.screen = indicator->IsCapturingTab(page) || indicator->IsCapturingWindow(page) ||
                 indicator->IsCapturingDisplay(page);
  // One entry per site, not per stream: a call opens several streams.
  std::set<std::u16string> sites;
  for (const auto& [id, origin] : capture_origins_)
    sites.insert(url_formatter::FormatOriginForSecurityDisplay(url::Origin::Create(GURL(origin)),
        url_formatter::SchemeDisplay::OMIT_HTTP_AND_HTTPS));
  if (sites.empty())
    sites.insert(url_formatter::FormatOriginForSecurityDisplay(
        url::Origin::Create(page->GetLastCommittedURL()), url_formatter::SchemeDisplay::OMIT_HTTP_AND_HTTPS));
  for (const auto& site : sites) state.site += (state.site.empty() ? u"" : u", ") + site;
  return state;
}

void PageMediaAccess::UpdateCaptureIndicator() {
  update_pending_ = false;
  const bool open = capture_prompt_ && capture_prompt_->IsVisible();
  const bool capturing = web_contents() && IsCapturing(web_contents());
  if (!capturing) capture_since_ = base::Time();
  else if (capture_since_.is_null()) capture_since_ = base::Time::Now();
  // Design: Kamera i mikrofon. Nothing pops up while a page captures; the
  // address pill shows it. An open card is rebuilt with the current devices.
  if (open && web_contents() && IsCapturing(web_contents())) ShowCaptureControls();
  else CloseCaptureIndicator();
  if (capture_changed_) capture_changed_.Run();
}

bool PageMediaAccess::capture_controls_visible_for_testing() const {
  return capture_prompt_ && capture_prompt_->IsVisible();
}

void PageMediaAccess::ToggleCaptureControls() {
  if (capture_prompt_ && capture_prompt_->IsVisible()) { CloseCaptureIndicator(); return; }
  if (web_contents() && IsCapturing(web_contents())) ShowCaptureControls();
}

void PageMediaAccess::StopCapture() {
  CloseCaptureIndicator();
  if (web_contents()) Stop(web_contents());
}

void PageMediaAccess::ShowCaptureControls() {
  CloseCaptureIndicator();
  auto* page = web_contents();
  const auto state = capture_state();
  if (!page || !state.active) return;
  auto body = std::make_unique<views::View>();
  auto scene = CreatePanelScene(body.get());
  if (!scene) return;
  capture_scene_ = std::make_unique<MediaSceneDialog>();
  capture_scene_->scene = std::move(scene);
  auto* panel = capture_scene_->scene.get();
  auto* profile = Profile::FromBrowserContext(page->GetBrowserContext());
  SetSidebarPanelLayout(body.get(), true, gfx::Insets(), 10);
  auto* head = body->AddChildView(std::make_unique<views::View>());
  head->AddChildView(panel->MakeIconTile(state.camera ? &vector_icons::kVideocamIcon
      : state.microphone ? &vector_icons::kMicIcon : &vector_icons::kScreenShareIcon));
  auto* lines = head->AddChildView(std::make_unique<views::View>());
  auto* site = lines->AddChildView(panel->MakeText(state.site, true, false, 0));
  panel->SetTextStyle(site, 4, PanelScene::Tone::kInk);
  auto* who = lines->AddChildView(panel->MakeText(capture_since_.is_null()
      ? TF(u"Profil: {}", {BrowsingProfileName(profile)})
      : TF(u"Profil: {} · od {}", {BrowsingProfileName(profile), base::TimeFormatTimeOfDay(capture_since_)}), true, false, 0));
  panel->SetTextStyle(who, 6, PanelScene::Tone::kMuted);
  SetSidebarPanelLayout(lines, true, gfx::Insets(), 2);
  SetSidebarPanelLayout(head, false, gfx::Insets(), 10, lines, true);
  auto device = [&](bool on, std::u16string text) {
    if (!on) return;
    auto* line = body->AddChildView(panel->MakeText(std::move(text), true, false, 0));
    panel->SetTextStyle(line, 5, PanelScene::Tone::kInk);
  };
  device(state.camera, T(u"Kamera jest włączona"));
  device(state.microphone, T(u"Mikrofon jest włączony"));
  device(state.screen, T(u"Udostępniasz ekran"));
  auto* note = body->AddChildView(panel->MakeText(
      T(u"Zamknięcie tej karty nie przerywa połączenia."), true, false, 0));
  panel->SetTextStyle(note, 6, PanelScene::Tone::kMuted);
  auto* row = body->AddChildView(std::make_unique<views::View>());
  auto* filler = row->AddChildView(std::make_unique<views::View>());
  auto* group = row->AddChildView(panel->MakeActions(6));
  auto* stop = panel->AddAction(group, base::BindRepeating(&PageMediaAccess::StopCapture,
      weak_factory_.GetWeakPtr()), T(u"Zatrzymaj wszystko"));
  panel->SetActionKind(stop, 2);
  SetSidebarPanelLayout(row, false, gfx::Insets(), 0, filler, true);
  auto dialog = CreateSceneModalDialog(std::move(body), false, false);
  dialog->SetTitle(T(u"Kamera i mikrofon"));
  dialog->SetShowTitle(false);
  dialog->SetShowCloseButton(false);
  dialog->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  if (auto* bubble = dialog->AsBubbleDialogDelegate()) {
    bubble->set_fixed_width(300);
    bubble->set_margins(gfx::Insets::VH(12, 0));
    // A popover: clicking the page or another window puts it away.
    bubble->set_close_on_deactivate(true);
  }
  SetSidebarPanelLayout(dialog->GetContentsView(), true, gfx::Insets::VH(0, 12), 0);
  capture_scene_->delegate = std::move(dialog);
  auto* parent = views::Widget::GetWidgetForNativeWindow(page->GetTopLevelNativeWindow());
  auto* widget = views::DialogDelegate::CreateDialogWidget(capture_scene_->delegate.get(),
      page->GetTopLevelNativeWindow(), parent ? parent->GetNativeView() : gfx::NativeView());
  capture_scene_->widget.reset(widget);
  widget->MakeCloseSynchronous(base::BindOnce([](base::WeakPtr<PageMediaAccess> access, views::Widget::ClosedReason) {
    if (access) access->CloseCaptureIndicator();
  }, weak_factory_.GetWeakPtr()));
  if (parent) widget->SetColorModeOverride(parent->color_mode_override());
#if BUILDFLAG(IS_MAC)
  InstallWorkspaceKeyboardRouting(widget, true);
#endif
  // Under the address pill, like the permission card.
  const auto anchor = PromptAnchor();
  auto bounds = widget->GetWindowBoundsInScreen();
  if (!anchor.IsEmpty()) bounds.set_origin(gfx::Point(anchor.x(), anchor.bottom() + 6));
  else if (parent) {
    const auto client = parent->GetClientAreaBoundsInScreen();
    bounds.set_origin(gfx::Point(client.right() - bounds.width() - 16, client.y() + 16));
  }
  if (auto* bubble = widget->widget_delegate()->AsBubbleDialogDelegate())
    bubble->SetAnchorRect(gfx::Rect(bounds.x() + bounds.width() / 2, bounds.y(), 0, 0));
  capture_prompt_ = widget->GetWeakPtr();
  widget->Show();
  capture_scene_->scene->Schedule();
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
  auto widget = capture_prompt_;
  capture_prompt_.reset();
  if(capture_scene_)MediaSceneDialog::Dispose(std::move(capture_scene_));
  else if(widget)widget->Close();
}
void PageMediaAccess::ReportPrompt(const PermissionOffer& offer){prompt_offer_=offer;if(prompt_report_)prompt_report_.Run(offer);}
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
