#include "chromium/prototype/page_services.h"
#include "components/find_in_page/find_tab_helper.h"
#include "chromium/prototype/browser_data.h"
#include "chromium/prototype/i18n.h"
#include "chromium/prototype/page_dialogs.h"
#include "chromium/prototype/sidebar_layout.h"
#include "build/build_config.h"
#include "chromium/prototype/sidebar_scene.rs.h"
#if BUILDFLAG(IS_MAC)
#include "chromium/prototype/workspace_keyboard_mac.h"
#endif
#include "ui/views/layout/layout_provider.h"

#include <utility>
#include <algorithm>
#include "ui/display/screen.h"
#include "ui/display/display.h"

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/functional/callback_helpers.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/content_settings/page_specific_content_settings_delegate.h"
#include "chrome/browser/ui/prefs/prefs_tab_helper.h"
#include "components/content_settings/browser/page_specific_content_settings.h"
#include "chrome/browser/history/history_tab_helper.h"
#include "chrome/browser/password_manager/chrome_password_manager_client.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/password_manager/factories/profile_password_store_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "chrome/browser/ui/autofill/chrome_autofill_client.h"
#include "chrome/browser/ui/autofill/payments/chrome_payments_autofill_client.h"
#include "components/history/content/browser/history_context_helper.h"
#include "components/history/core/browser/history_service.h"
#include "components/password_manager/core/browser/password_form_manager_for_ui.h"
#include "components/password_manager/core/common/password_manager_pref_names.h"
#include "components/sessions/content/session_tab_helper.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/mojom/ui_base_types.mojom.h"
#include "ui/views/widget/widget.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/window/dialog_delegate.h"
#include "url/origin.h"
#include "chrome/browser/extensions/api/web_navigation/web_navigation_tab_observer.h"
#include "chrome/browser/extensions/chrome_extension_web_contents_observer.h"
#include "chrome/browser/extensions/tab_helper.h"
#include "extensions/browser/view_type_utils.h"
#include "chromium/prototype/extension_tab_events.h"
#include "extensions/common/mojom/view_type.mojom.h"

namespace zitter::prototype {
std::u16string BrowsingProfileName(Profile* profile) {
  if (auto* manager = g_browser_process->profile_manager())
    if (auto* entry = manager->GetProfileAttributesStorage().GetProfileAttributesWithPath(profile->GetPath()))
      return entry->GetLocalProfileName();
  return base::UTF8ToUTF16(profile->GetPrefs()->GetString(prefs::kProfileName));
}

PageDialogs* AttachChromePageDialogs(content::WebContents* contents) {
  if (auto* dialogs = PageDialogs::FromWebContents(contents))
    return dialogs;
  return PageDialogs::Attach(
      contents,
      base::BindRepeating([](content::BrowserContext* context) {
        return BrowsingProfileName(Profile::FromBrowserContext(context));
      }));
}

namespace {
using FormManager = password_manager::PasswordFormManagerForUI;

void PrepareDialogText(views::View* view) {
  if (auto* label = views::AsViewClass<views::Label>(view)) label->SetSubpixelRenderingEnabled(false);
  for (const auto& child : view->children()) PrepareDialogText(child);
}

class ZitterPasswordClient final : public ChromePasswordManagerClient {
 public:
  using Offer = base::RepeatingCallback<bool(std::unique_ptr<FormManager>, bool)>;
  using Manual = base::RepeatingCallback<void(std::unique_ptr<FormManager>, bool)>;
  ZitterPasswordClient(content::WebContents* contents, Offer offer, Manual manual, base::RepeatingClosure manage)
      : ChromePasswordManagerClient(contents), offer_(std::move(offer)), manual_(std::move(manual)), manage_(std::move(manage)) {}
  bool PromptUserToSaveOrUpdatePassword(std::unique_ptr<FormManager> form, bool update) override {
    return offer_.Run(std::move(form), update);
  }
  void ShowManualFallbackForSaving(std::unique_ptr<FormManager> form, bool generated, bool update) override {
    manual_.Run(std::move(form), update);
  }
  void HideManualFallbackForSaving() override { manual_.Run(nullptr, false); }
  void NavigateToManagePasswordsPage(password_manager::ManagePasswordsReferrer) override { manage_.Run(); }
 private:
  Offer offer_;
  Manual manual_;
  base::RepeatingClosure manage_;
};

}  // namespace

// Owned by the page, never by the async native dialog. Its metrics reference
// the profile, so it must be released before that profile can shut down.
struct PageServices::PendingPassword {
  explicit PendingPassword(std::unique_ptr<FormManager> value, bool is_update)
      : form(std::move(value)), update(is_update) {}
  ~PendingPassword() { Ignore(); }
  void Save() { if (form) { form->Save(); form.reset(); } }
  void Decline() {
    if (!form) return;
    if (update) form->OnNopeUpdateClicked(); else form->OnNoInteraction(false);
    form.reset();
  }
  void Ignore() { if (form) { form->OnNoInteraction(update); form.reset(); } }
  void Never() { if (form) { form->Blocklist(); form.reset(); } }
  std::unique_ptr<FormManager> form;
  bool update;
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(PageServices);

void PageServices::Attach(content::WebContents* contents) {
  CreateForWebContents(contents);
}
PageServices::PageServices(content::WebContents* contents)
    : content::WebContentsObserver(contents), content::WebContentsUserData<PageServices>(*contents) {
  password_preferences_.Init(Profile::FromBrowserContext(contents->GetBrowserContext())->GetPrefs());
  password_preferences_.Add(password_manager::prefs::kCredentialsEnableService,
      base::BindRepeating(&PageServices::OnPasswordSavingChanged, weak_factory_.GetWeakPtr()));
  PrefsTabHelper::CreateForWebContents(contents);
  if (!content_settings::PageSpecificContentSettings::GetDelegateForWebContents(contents))
    content_settings::PageSpecificContentSettings::CreateForWebContents(contents,
        std::make_unique<PageSpecificContentSettingsDelegate>(contents));
  sound_observation_.Observe(HostContentSettingsMapFactory::GetForProfile(
      Profile::FromBrowserContext(contents->GetBrowserContext())));
  ApplySoundSetting();
  sessions::SessionTabHelper::CreateForWebContents(contents, base::BindRepeating(
      [](content::WebContents*) -> sessions::SessionTabHelperDelegate* { return nullptr; }));
  HistoryTabHelper::CreateForWebContents(contents);
  // Find in page (design: Find & Print); printing comes with the window.
  find_in_page::FindTabHelper::CreateForWebContents(contents);
  // Extensions see the page as a tab of its own profile: content scripts,
  // activeTab, the action runner, webNavigation (design: Extensions).
  extensions::ChromeExtensionWebContentsObserver::CreateForWebContents(contents);
  if (extensions::GetViewType(contents) == extensions::mojom::ViewType::kInvalid)
    extensions::SetViewType(contents, extensions::mojom::ViewType::kTabContents);
  extensions::WebNavigationTabObserver::CreateForWebContents(contents);
  extensions::TabHelper::CreateForWebContents(contents);
  ExtensionTabEvents::Attach(contents);
  autofill::ChromeAutofillClient::CreateForWebContents(contents);
  // Payment-save/reauth UI depends on Chrome's BrowserWindow. This is a
  // per-page capability switch, not a modification of the profile's settings.
  static_cast<autofill::ChromeAutofillClient*>(autofill::ContentAutofillClient::FromWebContents(contents))
      ->GetPaymentsAutofillClient()->DisablePaymentsAutofill();
  if (!ChromePasswordManagerClient::FromWebContents(contents)) {
    contents->SetUserData(ChromePasswordManagerClient::UserDataKey(),
        std::make_unique<ZitterPasswordClient>(contents,
            base::BindRepeating([](base::WeakPtr<PageServices> services, std::unique_ptr<FormManager> form, bool update) {
              return services && services->OfferPassword(std::move(form), update);
            }, weak_factory_.GetWeakPtr()),
            base::BindRepeating(&PageServices::SetManualPassword, weak_factory_.GetWeakPtr()),
            base::BindRepeating(&PageServices::ShowPasswords, weak_factory_.GetWeakPtr())));
  }
}
PageServices::~PageServices() { DismissPassword(); }
views::Widget* PageServices::password_prompt() const { return prompt_.get(); }

bool PageServices::PasswordSavingEnabled() const {
  if (!web_contents()) return false;
  auto* profile = Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  return !profile->IsOffTheRecord() && profile->GetPrefs()->GetBoolean(
      password_manager::prefs::kCredentialsEnableService);
}
void PageServices::OnPasswordSavingChanged() {
  if (PasswordSavingEnabled()) return;
  manual_password_.reset();
  DismissPassword();
}
bool PageServices::OfferPassword(std::unique_ptr<FormManager> form, bool update) {
  if (!form || !PasswordSavingEnabled() || web_contents()->GetVisibility() != content::Visibility::VISIBLE ||
      !web_contents()->GetLastCommittedURL().SchemeIsHTTPOrHTTPS() || form->IsBlocklisted()) return false;
  DismissPassword();
  auto* profile = Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  const auto origin = base::UTF8ToUTF16(url::Origin::Create(form->GetURL()).Serialize());
  const auto username = form->GetPendingCredentials().username_value;
  pending_password_ = std::make_shared<PendingPassword>(std::move(form), update);
  std::weak_ptr<PendingPassword> pending = pending_password_;
  auto body=std::make_unique<views::View>();
  password_scene_=CreatePanelScene(body.get());
  SetSidebarPanelLayout(body.get(),true,gfx::Insets(),views::LayoutProvider::Get()->GetDistanceMetric(views::DISTANCE_UNRELATED_CONTROL_VERTICAL));
  // The scene path takes its prompt copy from Rust.
  const auto copy=sidebar::password_prompt_copy(update,base::UTF16ToUTF8(origin),base::UTF16ToUTF8(username),base::UTF16ToUTF8(BrowsingProfileName(profile)));
  for(const auto& line:copy.lines)
    body->AddChildView(password_scene_->MakeText(base::UTF8ToUTF16(std::string(line)),true,false,0));
  // Not modal: the page keeps focus and input; the card stays out of the way.
  auto never=password_scene_->MakeActions(0);
  auto* never_button=password_scene_->AddAction(never.get(),base::BindRepeating([](base::WeakPtr<PageServices> services,std::weak_ptr<PendingPassword> weak){
    if(auto value=weak.lock())value->Never();if(services)services->DismissPassword();},password_ui_factory_.GetWeakPtr(),pending),T(u"Nigdy tutaj"));
  password_scene_->SetActionKind(never_button,3);
  auto dialog=CreateSceneModalDialog(std::move(body),false,false);
  dialog->SetExtraView(std::move(never));
  if(auto* bubble=dialog->AsBubbleDialogDelegate())bubble->set_fixed_width(300);
  dialog->SetTitle(base::UTF8ToUTF16(std::string(copy.title)));
  dialog->SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk)|static_cast<int>(ui::mojom::DialogButton::kCancel));
  dialog->SetButtonLabel(ui::mojom::DialogButton::kOk,base::UTF8ToUTF16(std::string(copy.accept)));
  dialog->SetButtonLabel(ui::mojom::DialogButton::kCancel,base::UTF8ToUTF16(std::string(copy.cancel)));
  dialog->SetAcceptCallback(base::BindOnce([](base::WeakPtr<PageServices> services, std::weak_ptr<PendingPassword> weak) {
    if (auto value = weak.lock()) {
      if (services && services->PasswordSavingEnabled()) value->Save();
      else value->Ignore();
    }
  }, weak_factory_.GetWeakPtr(), pending));
  dialog->SetCancelCallback(base::BindOnce([](std::weak_ptr<PendingPassword> weak){if(auto value=weak.lock())value->Decline();},pending));
  dialog->SetCloseCallback(base::BindOnce([](std::weak_ptr<PendingPassword> weak){if(auto value=weak.lock())value->Ignore();},pending));
  ShowPasswordWidget(std::move(dialog));
  return true;
}
void PageServices::SetManualPassword(std::unique_ptr<FormManager> form, bool update) {
  if (!PasswordSavingEnabled()) { manual_password_.reset(); return; }
  manual_password_ = std::move(form); manual_update_ = update;
}
void PageServices::ShowPasswords() {
  if (manual_password_) OfferPassword(std::move(manual_password_), manual_update_);
  else if (password_prompt() && password_folded_) ReopenPassword();
  else if (password_prompt()) password_prompt()->Activate();
  else if (manage_passwords_) manage_passwords_.Run();
}
bool PageServices::password_scene_visible_for_testing() const{return password_scene_&&password_scene_->visible();}
bool PageServices::password_scene_contains_for_testing(std::string_view text) const{return password_scene_&&password_scene_->contains_text(text);}
void PageServices::ShowPasswordWidget(std::unique_ptr<views::DialogDelegate> dialog,bool show){
  dialog->SetOwnershipOfNewWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  password_delegate_=std::move(dialog);
  auto* parent=views::Widget::GetWidgetForNativeWindow(web_contents()->GetTopLevelNativeWindow());
  auto* widget=views::DialogDelegate::CreateDialogWidget(password_delegate_.get(),web_contents()->GetTopLevelNativeWindow(),parent?parent->GetNativeView():gfx::NativeView());
  password_widget_.reset(widget);prompt_=widget->GetWeakPtr();
  widget->MakeCloseSynchronous(base::BindOnce([](base::WeakPtr<PageServices> services,views::Widget::ClosedReason){if(services)services->DismissPassword();},password_ui_factory_.GetWeakPtr()));
  if(parent)widget->SetColorModeOverride(parent->color_mode_override());
  PrepareDialogText(widget->GetRootView());
#if BUILDFLAG(IS_MAC)
  InstallWorkspaceKeyboardRouting(widget,true);
#endif
  if(show){PlacePassword();widget->ShowInactive();if(password_scene_)password_scene_->Schedule();
    password_folded_=false;password_fold_timer_.Start(FROM_HERE,base::Seconds(8),base::BindOnce(&PageServices::FoldPassword,password_ui_factory_.GetWeakPtr()));ReportPassword();}
}
// Under the address pill when the sidebar shows it; else the page's lower left corner.
void PageServices::PlacePassword(){
  if(!password_widget_||!web_contents())return;
  auto* parent=views::Widget::GetWidgetForNativeWindow(web_contents()->GetTopLevelNativeWindow());
  const auto anchor=password_anchor_?password_anchor_.Run():gfx::Rect();
  auto bounds=password_widget_->GetWindowBoundsInScreen();
  if(!anchor.IsEmpty())bounds.set_origin(gfx::Point(anchor.x(),anchor.bottom()+6));
  else if(parent){const auto page=parent->GetClientAreaBoundsInScreen();bounds.set_origin(gfx::Point(page.x()+16,page.bottom()-16-bounds.height()));}
  // An anchorless bubble hangs centred below its anchor point.
  if(auto* bubble=password_delegate_?password_delegate_->AsBubbleDialogDelegate():nullptr)bubble->SetAnchorRect(gfx::Rect(bounds.x()+bounds.width()/2,bounds.y(),0,0));
  else password_widget_->SetBounds(bounds);
}
// Folding keeps the offer: the address pill's key brings it back.
void PageServices::FoldPassword(){
  if(!password_widget_||!pending_password_)return;
  password_fold_timer_.Stop();password_widget_->Hide();password_folded_=true;ReportPassword();
}
void PageServices::ReopenPassword(){
  if(!password_widget_||!pending_password_)return;
  PlacePassword();password_widget_->ShowInactive();password_folded_=false;
  password_fold_timer_.Start(FROM_HERE,base::Seconds(8),base::BindOnce(&PageServices::FoldPassword,password_ui_factory_.GetWeakPtr()));ReportPassword();
}
void PageServices::ReportPassword(){if(password_pending_)password_pending_.Run(password_widget_&&pending_password_,password_folded_);}
void PageServices::DismissPassword() {
  password_fold_timer_.Stop();
  const bool reported = password_widget_ != nullptr;
  password_ui_factory_.InvalidateWeakPtrs();
  if (pending_password_) pending_password_->Ignore();
  pending_password_.reset();
  prompt_.reset();
  password_scene_.reset();
  password_widget_.reset();
  password_delegate_.reset();
  password_folded_ = false;
  if (reported && password_pending_) password_pending_.Run(false, false);
}
void PageServices::DidStartNavigation(content::NavigationHandle* navigation) {
  if (navigation->IsInPrimaryMainFrame() && !navigation->IsSameDocument()) { DismissPassword(); manual_password_.reset(); }
}
void PageServices::OnVisibilityChanged(content::Visibility visibility) {
  if (visibility == content::Visibility::HIDDEN) DismissPassword();
}
void PageServices::EndVisit() {
  if (!last_entry_id_ || !web_contents()) return;
  auto* profile = Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  if (auto* history = HistoryServiceFactory::GetForProfile(profile, ServiceAccessType::IMPLICIT_ACCESS))
    history->UpdateWithPageEndTime(history::ContextIDForWebContents(web_contents()), *last_entry_id_, last_url_, base::Time::Now());
  last_entry_id_.reset();
}
void PageServices::ApplySoundSetting() {
  if (!web_contents()) return;
  auto* settings = sound_observation_.GetSource();
  const auto& url = web_contents()->GetLastCommittedURL();
  web_contents()->SetAudioMuted(settings->GetContentSetting(url, url, ContentSettingsType::SOUND) == CONTENT_SETTING_BLOCK);
}
void PageServices::OnContentSettingChanged(const ContentSettingsPattern&,
    const ContentSettingsPattern&, ContentSettingsTypeSet types) {
  if (types.Contains(ContentSettingsType::SOUND)) ApplySoundSetting();
}
void PageServices::DidFinishNavigation(content::NavigationHandle* navigation) {
  if (navigation->HasCommitted() && navigation->IsInPrimaryMainFrame()) ApplySoundSetting();
  if (!navigation->HasCommitted() || !navigation->IsInPrimaryMainFrame() || navigation->IsErrorPage() ||
      !navigation->ShouldUpdateHistory() || !navigation->GetURL().SchemeIsHTTPOrHTTPS()) return;
  auto* profile = Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  if (profile->IsOffTheRecord()) return;
  auto* service = HistoryServiceFactory::GetForProfile(profile, ServiceAccessType::IMPLICIT_ACCESS);
  if (!service) return;
  EndVisit();
  auto* entry = web_contents()->GetController().GetLastCommittedEntry();
  // Reuse Chromium's redirect/referrer/transition handling. Its standard
  // observer excludes pages outside BrowserWindow, so only this adapter adds
  // the visit for our standalone Views pages.
  service->AddPage(HistoryTabHelper::FromWebContents(web_contents())->CreateHistoryAddPageArgs(
      web_contents()->GetLastCommittedURL(), entry->GetTimestamp(), entry->GetUniqueID(), navigation));
  last_entry_id_ = entry->GetUniqueID(); last_url_ = web_contents()->GetLastCommittedURL();
}
void PageServices::WebContentsDestroyed() {
  password_preferences_.RemoveAll();
  sound_observation_.Reset();
  EndVisit(); DismissPassword(); manual_password_.reset(); weak_factory_.InvalidateWeakPtrs();
}
}  // namespace zitter::prototype
