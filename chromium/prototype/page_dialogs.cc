#include "chromium/prototype/page_dialogs.h"
#include <algorithm>
#include <utility>
#include "chromium/prototype/editor_control.h"
#include "chromium/prototype/i18n.h"
#include "chromium/prototype/sidebar_layout.h"
#include "build/build_config.h"
#include "chromium/prototype/sidebar_scene.rs.h"
#if BUILDFLAG(IS_MAC)
#include "chromium/prototype/workspace_keyboard_mac.h"
#endif
#include "ui/events/event.h"

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/ui_base_types.mojom-shared.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"
#include "ui/views/window/dialog_client_view.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/base/metadata/metadata_impl_macros.h"

namespace zitter::prototype {
namespace {
// This client replaces DialogClientView's private MdTextButton construction.
// The inherited native Escape, input-protection and themed window services are
// retained. Its empty private button-row container never owns controls. Only
// scene dialogs use this adapter; native GetOkButton/GetCancelButton are
// deliberately not an API for these Rust nodes.
class SceneDialogClient final : public views::DialogClientView {
  METADATA_HEADER(SceneDialogClient,views::DialogClientView)
 public:
  SceneDialogClient(views::Widget* widget, views::View* body,
                    bool primary_action)
      : views::DialogClientView(widget, body), primary_action_(primary_action) {
    actions_ = AddChildView(CreatePageDialogActions(widget, primary_action_));
  }
  views::View* Button(bool accept) const{return actions_->GetViewByID(static_cast<int>(accept?ui::mojom::DialogButton::kOk:ui::mojom::DialogButton::kCancel));}
  void ViewHierarchyChanged(const views::ViewHierarchyChangedDetails& details) override{
    // Skip the base's private native-button allocator; preserve ClientView's
    // contents ownership and register the matching observer for base teardown.
    views::ClientView::ViewHierarchyChanged(details);
    if(details.is_add&&details.child==this)GetWidget()->widget_delegate()->AsDialogDelegate()->AddObserver(this);
  }
  void OnDialogChanged() override{
    auto* focused=GetFocusManager()->GetFocusedView();const int id=focused&&actions_->Contains(focused)?focused->GetID():0;
    auto* previous=actions_.get();actions_=AddChildView(CreatePageDialogActions(GetWidget(), primary_action_));RemoveChildViewT(previous);PreferredSizeChanged();
    if(id)if(auto* replacement=actions_->GetViewByID(id);replacement&&replacement->GetEnabled())replacement->RequestFocus();
  }
  gfx::Size CalculatePreferredSize(const views::SizeBounds& available) const override{auto size=views::DialogClientView::CalculatePreferredSize(available);size.Enlarge(0,actions_->GetPreferredSize().height());size.set_width(std::max(size.width(),actions_->GetPreferredSize().width()));return size;}
  gfx::Size GetMinimumSize() const override{const auto* delegate=GetWidget()->widget_delegate()->AsDialogDelegate();if(delegate->fixed_width())return CalculatePreferredSize(views::SizeBounds(delegate->fixed_width(),{}));auto size=views::DialogClientView::GetMinimumSize();size.Enlarge(0,actions_->GetPreferredSize().height());size.set_width(std::max(size.width(),actions_->GetPreferredSize().width()));return size;}
  gfx::Size GetMaximumSize() const override{auto size=views::DialogClientView::GetMaximumSize();if(size.height())size.Enlarge(0,actions_->GetPreferredSize().height());return size;}
  views::ProposedLayout CalculateProposedLayout(const views::SizeBounds& bounds) const override{
    auto result=views::DialogClientView::CalculateProposedLayout(bounds);const int row_height=actions_->GetHeightForWidth(bounds.width().value());
    for(auto& child:result.child_layouts)if(child.child_view==contents_view())child.bounds.set_height(std::max(0,child.bounds.height()-row_height));
    result.child_layouts.emplace_back(actions_.get(),true,gfx::Rect(0,std::max(0,bounds.height().value()-row_height),bounds.width().value(),row_height),bounds);return result;
  }
 private:
  const bool primary_action_;
  raw_ptr<views::View> actions_ = nullptr;
};
BEGIN_METADATA(SceneDialogClient)
END_METADATA
class PageDialogDelegate final : public views::DialogDelegate {
 public:
  explicit PageDialogDelegate(bool primary_action = false)
      : primary_action_(primary_action) {}
  views::ClientView* CreateClientView(views::Widget* widget) override {
    return new SceneDialogClient(widget, TransferOwnershipOfContentsView(),
                                 primary_action_);
  }
  views::View* GetInitiallyFocusedView() override{
    if(HasConfiguredInitiallyFocusedView())return views::WidgetDelegate::GetInitiallyFocusedView();
    const int button=GetDefaultDialogButton();if(!button)return nullptr;auto* client=static_cast<SceneDialogClient*>(GetDialogClientView());return client?client->Button(button==static_cast<int>(ui::mojom::DialogButton::kOk)):nullptr;
  }
 private:
  const bool primary_action_;
};
}  // namespace
std::unique_ptr<views::DialogDelegate> CreateSceneDialogDelegate(){
  return std::make_unique<PageDialogDelegate>();
}
views::ClientView* CreateSceneDialogClient(views::Widget* widget,views::View* contents){
  return new SceneDialogClient(widget, contents, false);
}
views::View* DialogActionButton(views::Widget* widget,bool accept){
  if(!widget)return nullptr;
  if(auto* client=views::AsViewClass<SceneDialogClient>(widget->client_view()))return client->Button(accept);
  auto* client=widget->widget_delegate()->AsDialogDelegate()->GetDialogClientView();return client?(accept?client->ok_button():client->cancel_button()):nullptr;
}
views::View* PageDialogs::dialog_button_for_testing(bool accept) const{return DialogActionButton(dialog_.get(),accept);}
WEB_CONTENTS_USER_DATA_KEY_IMPL(PageDialogs);
PageDialogs* PageDialogs::Attach(content::WebContents* contents,
                                 ProfileLabel profile_label) {
  return GetOrCreateForWebContents(contents, std::move(profile_label));
}
PageDialogs::PageDialogs(content::WebContents* contents,
                         ProfileLabel profile_label)
    : content::WebContentsObserver(contents),
      content::WebContentsUserData<PageDialogs>(*contents),
      profile_label_(std::move(profile_label)) {}
PageDialogs::~PageDialogs() { CancelDialogs(web_contents(), true); FinishCheck(false); }

void PageDialogs::RunJavaScriptDialog(content::WebContents*, content::RenderFrameHost* frame,
    content::JavaScriptDialogType type, const std::u16string& message,
    const std::u16string& initial, DialogClosedCallback callback, bool* suppressed) {
  *suppressed = !web_contents() || web_contents()->GetVisibility() != content::Visibility::VISIBLE || dialog_callback_ || checking_;
  if (*suppressed) return;
  Show(frame, type, message, initial, DialogKind::kScript, std::move(callback));
}
void PageDialogs::RunBeforeUnloadDialog(content::WebContents*, content::RenderFrameHost* frame,
    bool reload, DialogClosedCallback callback) {
  if (dialog_callback_) CancelDialogs(web_contents(), false);
  Show(frame, content::JAVASCRIPT_DIALOG_TYPE_CONFIRM,
      reload ? T(u"Odświeżenie strony może usunąć niezapisane zmiany.") : T(u"Opuszczenie strony może usunąć niezapisane zmiany."),
      {}, DialogKind::kLeave, std::move(callback));
}
void PageDialogs::ConfirmFormResubmission() {
  if (!web_contents() || repost_dialog_)
    return;
  const size_t request = ++repost_generation_;
  auto& controller = web_contents()->GetController();
  const auto* entry = controller.GetLastCommittedEntry();
  if (dialog_callback_ || checking_ || !entry || !entry->GetHasPostData() ||
      web_contents()->GetVisibility() != content::Visibility::VISIBLE) {
    controller.CancelPendingReload();
    return;
  }
  Show(
      web_contents()->GetPrimaryMainFrame(), content::JAVASCRIPT_DIALOG_TYPE_CONFIRM,
      T(u"Odświeżenie ponownie wyśle dane formularza. Może to powtórzyć ostatnią operację."),
      {}, DialogKind::kRepost,
      base::BindOnce(
          [](base::WeakPtr<PageDialogs> dialogs, size_t request,
             size_t navigation, int entry_id, bool accepted,
             const std::u16string&) {
            if (!dialogs || dialogs->repost_generation_ != request ||
                !dialogs->web_contents()) {
              return;
            }
            auto* page = dialogs->web_contents();
            auto& controller = page->GetController();
            const auto* entry = controller.GetLastCommittedEntry();
            if (accepted && dialogs->repost_page_generation_ == navigation &&
                entry && entry->GetHasPostData() && entry->GetUniqueID() == entry_id &&
                page->GetVisibility() == content::Visibility::VISIBLE) {
              controller.ContinuePendingReload();
            } else {
              controller.CancelPendingReload();
            }
          },
          weak_factory_.GetWeakPtr(), request, repost_page_generation_,
          entry->GetUniqueID()));
}

void PageDialogs::ConfirmExternalProtocol(std::u16string origin,
                                          std::u16string scheme,
                                          bool can_remember,
                                          base::OnceCallback<void(bool)> launch) {
  if (!web_contents() || dialog_callback_ || checking_ ||
      web_contents()->GetVisibility() != content::Visibility::VISIBLE) {
    return;
  }
  auto* frame = web_contents()->GetPrimaryMainFrame();
  Show(frame, content::JAVASCRIPT_DIALOG_TYPE_CONFIRM,
       {}, {}, DialogKind::kExternalProtocol,
       base::BindOnce(
           [](base::WeakPtr<PageDialogs> dialogs, size_t navigation,
              base::OnceCallback<void(bool)> launch, bool accepted,
              const std::u16string& remember) {
             if (accepted && dialogs && dialogs->web_contents() &&
                 dialogs->navigation_generation() == navigation &&
                 dialogs->web_contents()->GetVisibility() ==
                     content::Visibility::VISIBLE) {
               std::move(launch).Run(!remember.empty());
             }
           },
           weak_factory_.GetWeakPtr(), navigation_generation_, std::move(launch)),
       origin, scheme, can_remember);
}

void PageDialogs::Show(content::RenderFrameHost* frame, content::JavaScriptDialogType type,
    const std::u16string& message, const std::u16string& initial,
    DialogKind kind, DialogClosedCallback callback,
    std::u16string_view external_origin, std::u16string_view external_scheme,
    bool can_remember) {
  if (!web_contents() || !frame || !frame->IsActive() || !web_contents()->GetTopLevelNativeWindow()) {
    std::move(callback).Run(false, {}); return;
  }
  const bool unload = kind == DialogKind::kLeave;
  const bool repost = kind == DialogKind::kRepost;
  const auto generation=++dialog_generation_;
  requesting_frame_ = frame; unload_dialog_ = unload; dialog_callback_ = std::move(callback);
  repost_dialog_ = repost;
  external_protocol_dialog_ = kind == DialogKind::kExternalProtocol;
  dialog_type_ = type; dialog_message_ = message.substr(0, 2000);
  std::unique_ptr<views::DialogDelegate> dialog=std::make_unique<PageDialogDelegate>(external_protocol_dialog_);
  dialog->SetOwnershipOfNewWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  auto* body = dialog->SetContentsView(std::make_unique<views::View>());body_=body;
  scene_=CreatePageDialogScene(body);
  const auto origin=frame->GetLastCommittedOrigin().Serialize();
  const auto profile_name = profile_label_.Run(web_contents()->GetBrowserContext());
  std::u16string title=unload?TF(u"Opuścić stronę? · {}",{base::UTF8ToUTF16(origin)}):TF(u"Strona: {}",{base::UTF8ToUTF16(origin)}),identity_text=TF(u"Profil: {}",{profile_name}),message_text=message.substr(0,8192),accept=unload?T(u"Opuść stronę"):u"OK",cancel=unload?T(u"Zostań"):T(u"Anuluj");
  bool show_cancel=type!=content::JAVASCRIPT_DIALOG_TYPE_ALERT,default_cancel=unload,is_prompt=type==content::JAVASCRIPT_DIALOG_TYPE_PROMPT;
  {const auto copy=sidebar::page_dialog_copy(type==content::JAVASCRIPT_DIALOG_TYPE_ALERT?0:type==content::JAVASCRIPT_DIALOG_TYPE_PROMPT?2:1,unload,origin,base::UTF16ToUTF8(profile_name),rust::Slice<const uint16_t>(reinterpret_cast<const uint16_t*>(message.data()),message.size()));title=base::UTF8ToUTF16(std::string(copy.title));identity_text=base::UTF8ToUTF16(std::string(copy.identity));message_text=std::u16string(copy.message.begin(),copy.message.end());accept=base::UTF8ToUTF16(std::string(copy.accept));cancel=base::UTF8ToUTF16(std::string(copy.cancel));show_cancel=copy.show_cancel;default_cancel=copy.default_cancel;is_prompt=copy.prompt;}
  if (repost) {
    title = TF(u"Wysłać formularz ponownie? · {}", {base::UTF8ToUTF16(origin)});
    accept = T(u"Wyślij ponownie");
    default_cancel = true;
  }
  if (external_protocol_dialog_) {
    title = T(u"Otworzyć aplikację?");
    accept = T(u"Otwórz aplikację");
    default_cancel = true;
  }
  dialog->SetTitle(title);
  dialog->SetModalType(ui::mojom::ModalType::kWindow);
  dialog->SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk)|(show_cancel?static_cast<int>(ui::mojom::DialogButton::kCancel):0));
  dialog->SetButtonLabel(ui::mojom::DialogButton::kOk,accept);dialog->SetButtonLabel(ui::mojom::DialogButton::kCancel,cancel);
  dialog->SetDefaultButton(static_cast<int>(default_cancel?ui::mojom::DialogButton::kCancel:ui::mojom::DialogButton::kOk));
  constexpr int kExternalWidth = 400;
  if (external_protocol_dialog_) {
    constexpr int kPadding = 24;
    constexpr int kTextWidth = kExternalWidth - 2 * kPadding;
    SetSidebarPanelLayout(body, true, gfx::Insets::TLBR(12, kPadding, 16, kPadding), 12);
    auto add_text = [&](views::View* parent, std::u16string text, uint32_t style) {
      auto* view = parent->AddChildView(
          scene_->MakeText(std::move(text), true, false, kTextWidth));
      scene_->SetTextStyle(view, style, PageDialogScene::Tone::kInk);
    };
    add_text(body, T(u"Strona chce przejść do aplikacji poza Zitterem."), 5);
    auto* details = body->AddChildView(std::make_unique<views::View>());
    SetSidebarPanelLayout(details, true, gfx::Insets(), 4);
    add_text(details, TF(u"Strona · {}", {external_origin}), 8);
    add_text(details, TF(u"Protokół · {}:", {external_scheme}), 8);
    add_text(body, identity_text, 6);
    if (can_remember)
      remember_protocol_ = AddPageDialogCheckbox(body, T(u"Zawsze zezwalaj"));
    dialog->set_fixed_width(kExternalWidth);
  } else {
    SetSidebarPanelLayout(body,true,gfx::Insets(20),12);
    body->AddChildView(scene_->MakeText(identity_text,false,true,0));
    PanelViewport* viewport=nullptr;auto scroll=CreatePanelViewport(&viewport,scene_->ViewportChangedCallback(),{0,220,false,true});viewport_=viewport;body->AddChildView(std::move(scroll));
    {auto copy=scene_->MakeText(message_text,true,false,440);copy->SizeToPreferredSize();viewport->SetPanelContents(std::move(copy));}
  }
  if (is_prompt) {
    prompt_ = AddEditor(body);
    prompt_->GetViewAccessibility().SetName(T(u"Odpowiedź dla strony"));prompt_->SetText(initial);
    // Retain the native font metric; resolve the background from the live theme.
    prompt_->SetPresentation(views::Textfield::GetDefaultFontList().GetFontSize(),8,true,std::nullopt);
    prompt_->SetCallbacks({},base::BindRepeating([](base::WeakPtr<PageDialogs> manager,size_t generation,const ui::KeyEvent& event){
      return manager&&manager->dialog_generation_==generation&&manager->HandlePromptKey(event);
    },weak_factory_.GetWeakPtr(),generation));
    dialog->SetInitiallyFocusedView(prompt_->view());
  }
  if (external_protocol_dialog_) {
    body->SetPreferredSize(gfx::Size(kExternalWidth,
                                    body->GetHeightForWidth(kExternalWidth)));
  } else {
    body->SetPreferredSize(gfx::Size(480, std::max(280, body->GetHeightForWidth(480))));
  }
  dialog->SetAcceptCallbackWithClose(base::BindRepeating([](base::WeakPtr<PageDialogs> manager,size_t generation){
    if(!manager||manager->dialog_generation_!=generation)return true;
    if(manager->prompt_input()&&manager->prompt_->IsIMEComposing())return false;
    return true;
  },weak_factory_.GetWeakPtr(),generation));
  auto* parent = views::Widget::GetWidgetForNativeWindow(web_contents()->GetTopLevelNativeWindow());
  delegate_=std::move(dialog);
  auto* widget = views::DialogDelegate::CreateDialogWidget(delegate_.get(),
      web_contents()->GetTopLevelNativeWindow(), parent ? parent->GetNativeView() : gfx::NativeView());
  if (parent) widget->SetColorModeOverride(parent->color_mode_override());
  dialog_.reset(widget);
  // Own the C++ widget synchronously. An AppKit sheet's hide animation may
  // outlive the answer, but must not retain a text client or a stale prompt.
  // This also handles forced native/parent destruction through CloseNow().
  widget->MakeCloseSynchronous(base::BindOnce(
      [](base::WeakPtr<PageDialogs> manager,size_t generation,views::Widget::ClosedReason reason){
        if(manager&&manager->dialog_generation_==generation)
          manager->Answer(reason==views::Widget::ClosedReason::kAcceptButtonClicked);
      },weak_factory_.GetWeakPtr(),generation));
#if BUILDFLAG(IS_MAC)
  InstallWorkspaceKeyboardRouting(widget,true);
#endif
  widget->Show();scene_->Schedule();
}
bool PageDialogs::HandleJavaScriptDialog(content::WebContents* contents, bool accept,
    const std::u16string* override_text) {
  if (contents != web_contents() || !dialog_callback_ || repost_dialog_ ||
      external_protocol_dialog_) return false;
  if(!dialog_){Answer(false);return false;}
  if(prompt_&&override_text)prompt_->SetText(*override_text);
  else if(prompt_&&accept)prompt_->ConfirmCompositionText(false);
  Answer(accept); return true;
}
bool PageDialogs::HandlePromptKey(const ui::KeyEvent& event) {
  if(!dialog_||!dialog_->IsVisible()||!prompt_||prompt_->IsIMEComposing()||event.type()!=ui::EventType::kKeyPressed)return false;
  if(event.key_code()!=ui::VKEY_RETURN&&event.key_code()!=ui::VKEY_ESCAPE)return false;
  PostPromptAction(event.key_code()==ui::VKEY_RETURN);return true;
}
void PageDialogs::PostPromptAction(bool accepted) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,base::BindOnce(
      [](base::WeakPtr<PageDialogs> manager,size_t generation,bool accepted){
        if(!manager||manager->dialog_generation_!=generation||!manager->dialog_||!manager->dialog_->IsVisible())return;
        auto* delegate=manager->dialog_->widget_delegate()->AsDialogDelegate();
        if(accepted)delegate->AcceptDialog();else delegate->CancelDialog();
      },weak_factory_.GetWeakPtr(),dialog_generation_,accepted));
}
void PageDialogs::Answer(bool accepted) {
  if (!dialog_callback_) return;
  ++dialog_generation_;
  auto callback = std::move(dialog_callback_);
  auto* field=prompt_input();
  if(field){field->SetCallbacks({},{});if(!accepted)field->ClearCompositionText();}
  std::u16string text;
  if (accepted && field)
    text = field->GetText();
  if (accepted && remember_protocol_ && IsPageDialogCheckboxChecked(remember_protocol_))
    text = u"remember";
  remember_protocol_ = nullptr;
  prompt_ = nullptr; requesting_frame_ = nullptr; unload_dialog_ = false;
  repost_dialog_ = false;
  external_protocol_dialog_ = false;
  scene_.reset();viewport_=nullptr;body_=nullptr;dialog_.reset();delegate_.reset();
  // Renderer callbacks can destroy pages. Never run them on a button's stack.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, base::BindOnce(std::move(callback), accepted, text));
}
void PageDialogs::CancelDialogs(content::WebContents*, bool) { Answer(false); }
void PageDialogs::CheckBeforeUnload(base::OnceCallback<void(bool)> done) {
  if (checking_ || !web_contents()) { std::move(done).Run(false); return; }
  if (!web_contents()->NeedToFireBeforeUnload()) { std::move(done).Run(true); return; }
  CancelDialogs(web_contents(), false);
  checking_ = true; check_callback_ = std::move(done);
  web_contents()->DispatchBeforeUnload(false);
}
void PageDialogs::BeforeUnloadFired(bool proceed) {
  if (checking_) FinishCheck(proceed);
}
void PageDialogs::FinishCheck(bool proceed) {
  if (!check_callback_) return;
  auto callback = std::move(check_callback_);
  // Keep checking_ set until the delegate has prevented Chromium from closing
  // this page on its own. The workspace operation commits after every page agrees.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
      base::BindOnce([](base::WeakPtr<PageDialogs> manager, base::OnceCallback<void(bool)> done, bool proceed) {
        if (manager) manager->checking_ = false;
        std::move(done).Run(proceed && !!manager);
      }, weak_factory_.GetWeakPtr(), std::move(callback), proceed));
}
void PageDialogs::DidStartNavigation(content::NavigationHandle* navigation) {
  if (navigation->IsInPrimaryMainFrame()) {
    ++repost_page_generation_;
    if (navigation->IsSameDocument() && repost_dialog_)
      CancelDialogs(web_contents(), false);
  }
  if (!navigation->IsSameDocument()) ++navigation_generation_;
  if (!navigation->IsSameDocument() && (navigation->IsInPrimaryMainFrame() ||
      (requesting_frame_ && navigation->GetFrameTreeNodeId() == requesting_frame_->GetFrameTreeNodeId()))) {
    CancelDialogs(web_contents(), true); FinishCheck(false);
  }
}
void PageDialogs::OnVisibilityChanged(content::Visibility visibility) {
  if (visibility != content::Visibility::VISIBLE) {
    ++repost_page_generation_;
    if (repost_dialog_)
      CancelDialogs(web_contents(), false);
  }
  if (visibility == content::Visibility::HIDDEN && !unload_dialog_) CancelDialogs(web_contents(), false);
}
void PageDialogs::RenderFrameDeleted(content::RenderFrameHost* frame) {
  if (requesting_frame_ == frame) { CancelDialogs(web_contents(), false); FinishCheck(false); }
}
void PageDialogs::WebContentsDestroyed() { CancelDialogs(web_contents(), true); FinishCheck(false); }
}  // namespace zitter::prototype
