#ifndef ZITTER_CHROMIUM_PROTOTYPE_PAGE_DIALOGS_H_
#define ZITTER_CHROMIUM_PROTOTYPE_PAGE_DIALOGS_H_

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
namespace zitter::prototype {
class EditorControl;
class PanelViewport;
using PageDialogScene=PanelScene;
std::unique_ptr<views::DialogDelegate> CreateSceneDialogDelegate();
std::unique_ptr<views::DialogDelegate> CreateSceneModalDialog(std::unique_ptr<views::View> body,bool control,bool modal=true);
std::unique_ptr<PageDialogScene> CreatePageDialogScene(views::View* root);
std::unique_ptr<views::View> CreatePageDialogActions(views::Widget* widget);
views::ClientView* CreateSceneDialogClient(views::Widget* widget,views::View* contents);
views::Button* DialogActionButton(views::Widget* widget,bool accept);
bool PageDialogActionsVisibleForTesting(views::View* actions,std::string_view text);

// Native dialogs bound to the requesting WebContents, including beforeunload
// checks used by atomic workspace operations outside Chrome's TabStripModel.
class PageDialogs final : public content::JavaScriptDialogManager,
                          public content::WebContentsObserver,
                          public content::WebContentsUserData<PageDialogs> {
 public:
  static PageDialogs* Attach(content::WebContents* contents);
  ~PageDialogs() override;
  void CheckBeforeUnload(base::OnceCallback<void(bool)> done);
  bool checking_before_unload() const { return checking_; }
  size_t navigation_generation() const { return navigation_generation_; }
  views::Widget* dialog() const { return dialog_.get(); }
  EditorControl* prompt_input() const { return dialog_?prompt_.get():nullptr; }
  bool HandlePromptKey(const ui::KeyEvent& event);
  views::Button* dialog_button_for_testing(bool accept) const;
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
  explicit PageDialogs(content::WebContents* contents);
  void Show(content::RenderFrameHost*, content::JavaScriptDialogType,
      const std::u16string&, const std::u16string&, bool, DialogClosedCallback);
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
  raw_ptr<EditorControl> prompt_ = nullptr;
  raw_ptr<content::RenderFrameHost> requesting_frame_ = nullptr;
  DialogClosedCallback dialog_callback_;
  base::OnceCallback<void(bool)> check_callback_;
  size_t navigation_generation_ = 0;
  size_t dialog_generation_ = 0;
  bool checking_ = false;
  bool unload_dialog_ = false;
  base::WeakPtrFactory<PageDialogs> weak_factory_{this};
  WEB_CONTENTS_USER_DATA_KEY_DECL();
};
}  // namespace zitter::prototype
#endif
