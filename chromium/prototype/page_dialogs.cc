#include "chromium/prototype/page_dialogs.h"
#include "chromium/prototype/browser_data.h"

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/ui_base_types.mojom-shared.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"

namespace zitter::prototype {
WEB_CONTENTS_USER_DATA_KEY_IMPL(PageDialogs);
PageDialogs* PageDialogs::Attach(content::WebContents* contents) {
  CreateForWebContents(contents); return FromWebContents(contents);
}
PageDialogs::PageDialogs(content::WebContents* contents)
    : content::WebContentsObserver(contents), content::WebContentsUserData<PageDialogs>(*contents) {}
PageDialogs::~PageDialogs() { CancelDialogs(web_contents(), true); FinishCheck(false); }

void PageDialogs::RunJavaScriptDialog(content::WebContents*, content::RenderFrameHost* frame,
    content::JavaScriptDialogType type, const std::u16string& message,
    const std::u16string& initial, DialogClosedCallback callback, bool* suppressed) {
  *suppressed = !web_contents() || web_contents()->GetVisibility() != content::Visibility::VISIBLE || dialog_callback_ || checking_;
  if (*suppressed) return;
  Show(frame, type, message, initial, false, std::move(callback));
}
void PageDialogs::RunBeforeUnloadDialog(content::WebContents*, content::RenderFrameHost* frame,
    bool reload, DialogClosedCallback callback) {
  if (dialog_callback_) CancelDialogs(web_contents(), false);
  Show(frame, content::JAVASCRIPT_DIALOG_TYPE_CONFIRM,
      reload ? u"Odświeżenie strony może usunąć niezapisane zmiany." : u"Opuszczenie strony może usunąć niezapisane zmiany.",
      {}, true, std::move(callback));
}
void PageDialogs::Show(content::RenderFrameHost* frame, content::JavaScriptDialogType type,
    const std::u16string& message, const std::u16string& initial, bool unload, DialogClosedCallback callback) {
  if (!web_contents() || !frame || !frame->IsActive() || !web_contents()->GetTopLevelNativeWindow()) {
    std::move(callback).Run(false, {}); return;
  }
  requesting_frame_ = frame; unload_dialog_ = unload; dialog_callback_ = std::move(callback);
  auto dialog = std::make_unique<views::DialogDelegate>();
  auto* body = dialog->SetContentsView(std::make_unique<views::View>());
  dialog->SetTitle((unload ? u"Opuścić stronę? · " : u"Strona: ") + base::UTF8ToUTF16(frame->GetLastCommittedOrigin().Serialize()));
  dialog->SetModalType(ui::mojom::ModalType::kWindow);
  dialog->SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk) |
      (type == content::JAVASCRIPT_DIALOG_TYPE_ALERT ? 0 : static_cast<int>(ui::mojom::DialogButton::kCancel)));
  dialog->SetButtonLabel(ui::mojom::DialogButton::kOk, unload ? u"Opuść stronę" : u"OK");
  dialog->SetButtonLabel(ui::mojom::DialogButton::kCancel, unload ? u"Zostań" : u"Anuluj");
  dialog->SetDefaultButton(static_cast<int>(unload ? ui::mojom::DialogButton::kCancel : ui::mojom::DialogButton::kOk));
  body->SetLayoutManager(std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical, gfx::Insets(20), 12));
  auto* profile = Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  auto* identity = body->AddChildView(std::make_unique<views::Label>(u"Profil: " + BrowsingProfileName(profile)));
  identity->SetSubpixelRenderingEnabled(false);
  auto* scroll = body->AddChildView(std::make_unique<views::ScrollView>());
  scroll->ClipHeightTo(0, 220);
  scroll->SetHorizontalScrollBarMode(views::ScrollView::ScrollBarMode::kDisabled);
  auto label = std::make_unique<views::Label>(message.substr(0, 8192));
  label->SetSubpixelRenderingEnabled(false); label->SetMultiLine(true); label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SizeToFit(440); scroll->SetContents(std::move(label));
  if (type == content::JAVASCRIPT_DIALOG_TYPE_PROMPT) {
    prompt_ = body->AddChildView(std::make_unique<views::Textfield>());
    prompt_->SetAccessibleName(u"Odpowiedź dla strony"); prompt_->SetText(initial);
    dialog->SetInitiallyFocusedView(prompt_);
  }
  body->SetPreferredSize(gfx::Size(480, 280));
  dialog->SetAcceptCallback(base::BindOnce(&PageDialogs::Answer, weak_factory_.GetWeakPtr(), true));
  dialog->SetCancelCallback(base::BindOnce(&PageDialogs::Answer, weak_factory_.GetWeakPtr(), false));
  dialog->SetCloseCallback(base::BindOnce(&PageDialogs::Answer, weak_factory_.GetWeakPtr(), false));
  auto* parent = views::Widget::GetWidgetForNativeWindow(web_contents()->GetTopLevelNativeWindow());
  auto* widget = views::DialogDelegate::CreateDialogWidget(std::move(dialog),
      web_contents()->GetTopLevelNativeWindow(), parent ? parent->GetNativeView() : gfx::NativeView());
  if (parent) widget->SetColorModeOverride(parent->color_mode_override());
  dialog_ = widget->GetWeakPtr(); widget->Show();
}
bool PageDialogs::HandleJavaScriptDialog(content::WebContents* contents, bool accept,
    const std::u16string* override_text) {
  if (contents != web_contents() || !dialog_callback_) return false;
  if (prompt_ && override_text) prompt_->SetText(*override_text);
  Answer(accept); return true;
}
void PageDialogs::Answer(bool accepted) {
  if (!dialog_callback_) return;
  auto callback = std::move(dialog_callback_);
  const std::u16string text = prompt_ && accepted ? std::u16string(prompt_->GetText()) : std::u16string();
  prompt_ = nullptr; requesting_frame_ = nullptr; unload_dialog_ = false;
  if (dialog_) dialog_->Close(); dialog_.reset();
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
  if (!navigation->IsSameDocument()) ++navigation_generation_;
  if (!navigation->IsSameDocument() && (navigation->IsInPrimaryMainFrame() ||
      (requesting_frame_ && navigation->GetFrameTreeNodeId() == requesting_frame_->GetFrameTreeNodeId()))) {
    CancelDialogs(web_contents(), true); FinishCheck(false);
  }
}
void PageDialogs::OnVisibilityChanged(content::Visibility visibility) {
  if (visibility == content::Visibility::HIDDEN && !unload_dialog_) CancelDialogs(web_contents(), false);
}
void PageDialogs::RenderFrameDeleted(content::RenderFrameHost* frame) {
  if (requesting_frame_ == frame) { CancelDialogs(web_contents(), false); FinishCheck(false); }
}
void PageDialogs::WebContentsDestroyed() { CancelDialogs(web_contents(), true); FinishCheck(false); }
}  // namespace zitter::prototype
