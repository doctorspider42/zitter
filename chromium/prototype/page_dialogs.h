#ifndef ZITTER_CHROMIUM_PROTOTYPE_PAGE_DIALOGS_H_
#define ZITTER_CHROMIUM_PROTOTYPE_PAGE_DIALOGS_H_

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/memory/raw_ptr.h"
#include <memory>
#include "chromium/prototype/sidebar_metal.h"
#include <string>
#include <string_view>
#include "content/public/browser/javascript_dialog_manager.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

namespace views { class Widget; class View; class ScrollView; class DialogDelegate; class Button; class ClientView; }
namespace ui { class KeyEvent; }
namespace content { class BrowserContext; }
namespace zitter::prototype {
class EditorControl;
class PanelViewport;
using PageDialogScene=PanelScene;
std::unique_ptr<views::DialogDelegate> CreateSceneDialogDelegate();
std::unique_ptr<views::DialogDelegate> CreateSceneModalDialog(std::unique_ptr<views::View> body,bool control,bool modal=true);
std::unique_ptr<PageDialogScene> CreatePageDialogScene(views::View* root);
views::View* AddPageDialogCheckbox(views::View* parent, std::u16string label);
bool IsPageDialogCheckboxChecked(views::View* checkbox);
std::unique_ptr<views::View> CreatePageDialogActions(views::Widget* widget, bool primary_action);
views::ClientView* CreateSceneDialogClient(views::Widget* widget,views::View* contents);
views::View* DialogActionButton(views::Widget* widget,bool accept);
bool PageDialogActionsVisibleForTesting(views::View* actions,std::string_view text);

// Native dialogs bound to the requesting WebContents, including beforeunload
// checks used by atomic workspace operations outside Chrome's TabStripModel.
class PageDialogs final : public content::JavaScriptDialogManager,
                          public content::WebContentsObserver,
                          public content::WebContentsUserData<PageDialogs> {
 public:
  using ProfileLabel =
      base::RepeatingCallback<std::u16string(content::BrowserContext*)>;
  // Resolve the label when a dialog opens so it follows profile renames.
  static PageDialogs* Attach(content::WebContents* contents,
                             ProfileLabel profile_label);
  ~PageDialogs() override;
  void CheckBeforeUnload(base::OnceCallback<void(bool)> done);
  void ConfirmFormResubmission();
  void ConfirmExternalProtocol(std::u16string origin, std::u16string scheme,
                               bool can_remember,
                               base::OnceCallback<void(bool)> launch);
  bool checking_before_unload() const { return checking_; }
  size_t navigation_generation() const { return navigation_generation_; }
  views::Widget* dialog() const { return dialog_.get(); }
  // The open page dialog, for the assistant: its kind and message.
  bool dialog_open() const {
    return !!dialog_callback_ && !repost_dialog_ && !external_protocol_dialog_;
  }
  bool leave_dialog_open() const { return unload_dialog_; }
  bool repost_dialog_open() const { return repost_dialog_; }
  content::JavaScriptDialogType dialog_type() const { return dialog_type_; }
  const std::u16string& dialog_message() const { return dialog_message_; }
  EditorControl* prompt_input() const { return dialog_?prompt_.get():nullptr; }
  bool HandlePromptKey(const ui::KeyEvent& event);
  views::View* dialog_button_for_testing(bool accept) const;
  PanelViewport* viewport_for_testing() const{return dialog_?viewport_.get():nullptr;}
  views::View* dialog_body_for_testing() const{return dialog_?body_.get():nullptr;}
  bool scene_visible_for_testing() const{return scene_&&scene_->visible();}
  bool scene_contains_text_for_testing(std::string_view text) const{return scene_&&scene_->contains_text(text);}
  bool scene_excludes_for_testing(views::View* view) const{return scene_&&scene_->excludes(view);}
  bool scene_order_correct_for_testing() const{return scene_&&scene_->order_correct();}
  void RunJavaScriptDialog(content::WebContents*, content::RenderFrameHost*,
      content::JavaScriptDialogType, const std::u16string&, const std::u16string&,
      DialogClosedCallback, bool*) override;
  void RunBeforeUnloadDialog(content::WebContents*, content::RenderFrameHost*, bool,
      DialogClosedCallback) override;
  bool HandleJavaScriptDialog(content::WebContents*, bool, const std::u16string*) override;
  void CancelDialogs(content::WebContents*, bool) override;

 private:
  friend class content::WebContentsUserData<PageDialogs>;
  enum class DialogKind { kScript, kLeave, kRepost, kExternalProtocol };
  PageDialogs(content::WebContents* contents, ProfileLabel profile_label);
  void Show(content::RenderFrameHost*, content::JavaScriptDialogType,
      const std::u16string&, const std::u16string&, DialogKind, DialogClosedCallback,
      std::u16string_view external_origin = {},
      std::u16string_view external_scheme = {}, bool can_remember = false);
  void Answer(bool accepted);
  void PostPromptAction(bool accepted);
  void FinishCheck(bool proceed);
  void BeforeUnloadFired(bool proceed) override;
  void DidStartNavigation(content::NavigationHandle*) override;
  void OnVisibilityChanged(content::Visibility) override;
  void RenderFrameDeleted(content::RenderFrameHost*) override;
  void WebContentsDestroyed() override;
  // The client owns the delegate and destroys it after its widget.
  std::unique_ptr<views::DialogDelegate> delegate_;
  std::unique_ptr<views::Widget> dialog_;
  std::unique_ptr<PageDialogScene> scene_;
  raw_ptr<PanelViewport> viewport_ = nullptr;
  raw_ptr<views::View> body_ = nullptr;
  raw_ptr<views::View> remember_protocol_ = nullptr;
  raw_ptr<EditorControl> prompt_ = nullptr;
  raw_ptr<content::RenderFrameHost> requesting_frame_ = nullptr;
  const ProfileLabel profile_label_;
  DialogClosedCallback dialog_callback_;
  base::OnceCallback<void(bool)> check_callback_;
  size_t navigation_generation_ = 0;
  size_t dialog_generation_ = 0;
  size_t repost_generation_ = 0;
  size_t repost_page_generation_ = 0;
  bool checking_ = false;
  bool unload_dialog_ = false;
  bool repost_dialog_ = false;
  bool external_protocol_dialog_ = false;
  content::JavaScriptDialogType dialog_type_ = content::JAVASCRIPT_DIALOG_TYPE_ALERT;
  std::u16string dialog_message_;
  base::WeakPtrFactory<PageDialogs> weak_factory_{this};
  WEB_CONTENTS_USER_DATA_KEY_DECL();
};
}  // namespace zitter::prototype
#endif
