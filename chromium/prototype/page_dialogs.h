#ifndef ZITTER_CHROMIUM_PROTOTYPE_PAGE_DIALOGS_H_
#define ZITTER_CHROMIUM_PROTOTYPE_PAGE_DIALOGS_H_

#include "base/memory/weak_ptr.h"
#include "base/memory/raw_ptr.h"
#include "content/public/browser/javascript_dialog_manager.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

namespace views { class Widget; class Textfield; }
namespace zitter::prototype {
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
  void FinishCheck(bool proceed);
  void BeforeUnloadFired(bool proceed) override;
  void DidStartNavigation(content::NavigationHandle*) override;
  void OnVisibilityChanged(content::Visibility) override;
  void RenderFrameDeleted(content::RenderFrameHost*) override;
  void WebContentsDestroyed() override;
  base::WeakPtr<views::Widget> dialog_;
  raw_ptr<views::Textfield> prompt_ = nullptr;
  raw_ptr<content::RenderFrameHost> requesting_frame_ = nullptr;
  DialogClosedCallback dialog_callback_;
  base::OnceCallback<void(bool)> check_callback_;
  size_t navigation_generation_ = 0;
  bool checking_ = false;
  bool unload_dialog_ = false;
  base::WeakPtrFactory<PageDialogs> weak_factory_{this};
  WEB_CONTENTS_USER_DATA_KEY_DECL();
};
}  // namespace zitter::prototype
#endif
