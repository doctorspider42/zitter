// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "http_auth_prompt.h"

#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/supports_user_data.h"
#include "base/task/single_thread_task_runner.h"
#include "build/build_config.h"
#include "chromium/prototype/i18n.h"
#include "chromium/prototype/page_dialogs.h"
#include "chromium/prototype/sidebar_layout.h"
#if BUILDFLAG(IS_MAC)
#include "chromium/prototype/workspace_keyboard_mac.h"
#endif
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/base/ime/text_input_type.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/ui_base_types.mojom.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"

namespace zitter::content_embedder {
namespace {
const char kPromptKey = 0;
class HttpAuthPrompt;
struct PromptHandle final : base::SupportsUserData::Data {
  explicit PromptHandle(base::WeakPtr<HttpAuthPrompt> value) : prompt(value) {}
  base::WeakPtr<HttpAuthPrompt> prompt;
};

class HttpAuthPrompt final : public content::LoginDelegate,
                             public content::WebContentsObserver {
 public:
  HttpAuthPrompt(content::WebContents* contents,
                 LoginAuthRequiredCallback callback)
      : content::WebContentsObserver(contents), callback_(std::move(callback)) {
    contents->SetUserData(&kPromptKey,
        std::make_unique<PromptHandle>(weak_.GetWeakPtr()));
  }
  ~HttpAuthPrompt() override {
    weak_.InvalidateWeakPtrs();
    callback_.Reset();
    Close();
  }
  static HttpAuthPrompt* From(content::WebContents* contents) {
    const auto* handle = contents
        ? static_cast<PromptHandle*>(contents->GetUserData(&kPromptKey)) : nullptr;
    return handle ? handle->prompt.get() : nullptr;
  }
  void Show(const net::AuthChallengeInfo& challenge) {
    auto body = std::make_unique<views::View>();
    scene_ = prototype::CreatePageDialogScene(body.get());
    prototype::SetSidebarPanelLayout(body.get(), true, gfx::Insets(20), 12);
    // The security identity is the network-provided challenger, never the
    // untrusted realm or a possibly unrelated currently committed page URL.
    body->AddChildView(scene_->MakeText(
        base::UTF8ToUTF16(challenge.challenger.Serialize()), true, false, 380));
    auto realm = base::CollapseWhitespace(
        base::UTF8ToUTF16(challenge.realm.substr(0, 256)), false);
    base::i18n::SanitizeUserSuppliedString(&realm);
    if (!realm.empty())
      body->AddChildView(scene_->MakeText(realm, true, true, 380));
    auto add_field = [&](std::u16string label) {
      body->AddChildView(scene_->MakeText(label, false, false, 0));
      auto* field = body->AddChildView(std::make_unique<views::Textfield>());
      field->GetViewAccessibility().SetName(label);
      field->SetPreferredSize(gfx::Size(380, 32));
      return field;
    };
    username_ = add_field(prototype::T(u"Użytkownik"));
    password_ = add_field(prototype::T(u"Hasło"));
    password_->SetTextInputType(ui::TEXT_INPUT_TYPE_PASSWORD);
    delegate_ = prototype::CreateSceneDialogDelegate();
    delegate_->SetOwnershipOfNewWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
    delegate_->SetContentsView(std::move(body));
    delegate_->SetTitle(prototype::T(challenge.is_proxy
        ? u"Logowanie do serwera proxy" : u"Logowanie do serwera"));
    delegate_->SetModalType(ui::mojom::ModalType::kWindow);
    delegate_->SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk) |
                          static_cast<int>(ui::mojom::DialogButton::kCancel));
    delegate_->SetButtonLabel(ui::mojom::DialogButton::kOk, prototype::T(u"Zaloguj się"));
    delegate_->SetButtonLabel(ui::mojom::DialogButton::kCancel, prototype::T(u"Anuluj"));
    delegate_->SetInitiallyFocusedView(username_);
    delegate_->set_fixed_width(420);
    auto* parent = views::Widget::GetWidgetForNativeWindow(
        web_contents()->GetTopLevelNativeWindow());
    widget_.reset(views::DialogDelegate::CreateDialogWidget(
        delegate_.get(), parent->GetNativeWindow(), parent->GetNativeView()));
    widget_->SetColorModeOverride(parent->color_mode_override());
    widget_->MakeCloseSynchronous(base::BindOnce(
        [](base::WeakPtr<HttpAuthPrompt> self, views::Widget::ClosedReason reason) {
          if (self)
            self->Answer(reason == views::Widget::ClosedReason::kAcceptButtonClicked);
        }, weak_.GetWeakPtr()));
#if BUILDFLAG(IS_MAC)
    prototype::InstallWorkspaceKeyboardRouting(widget_.get(), true);
#endif
    widget_->Show();
    scene_->Schedule();
  }
  views::Widget* widget() const { return widget_.get(); }
  bool AnswerForTesting(bool accept) {
    if (!widget_ || widget_->IsClosed() || !callback_ ||
        password_->GetTextInputType() != ui::TEXT_INPUT_TYPE_PASSWORD)
      return false;
    username_->SetText(u"zitter-fixture");
    password_->SetText(u"fixture-password");
    if (accept) delegate_->AcceptDialog();
    else delegate_->CancelDialog();
    return true;
  }
 private:
  void Close() {
    if (widget_ && !widget_->IsClosed()) widget_->CloseNow();
    // Scene observers and native text clients must disappear before their body.
    scene_.reset();
    username_ = nullptr;
    password_ = nullptr;
    widget_.reset();
    delegate_.reset();
  }
  void Answer(bool accepted) {
    if (!callback_) return;
    std::optional<net::AuthCredentials> credentials;
    if (accepted && web_contents() &&
        web_contents()->GetVisibility() == content::Visibility::VISIBLE)
      credentials.emplace(std::u16string(username_->GetText()),
                          std::u16string(password_->GetText()));
    // Content forbids a reentrant auth callback. Cancellation may also destroy
    // the delegate before delivery, in which case the weak task must do nothing.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
        base::BindOnce(&HttpAuthPrompt::Deliver, weak_.GetWeakPtr(),
                       std::move(credentials)));
  }
  void Deliver(std::optional<net::AuthCredentials> credentials) {
    if (!callback_) return;
    auto callback = std::move(callback_);
    Close();
    std::move(callback).Run(credentials);
    // Running the network callback may destroy this delegate.
  }
  void OnVisibilityChanged(content::Visibility visibility) override {
    if (visibility != content::Visibility::VISIBLE) Cancel();
  }
  void DidStartNavigation(content::NavigationHandle* navigation) override {
    if (navigation->IsInPrimaryMainFrame() && !navigation->IsSameDocument()) Cancel();
  }
  void WebContentsDestroyed() override { Cancel(); }
  void Cancel() {
    weak_.InvalidateWeakPtrs();
    // Replace any queued acceptance with cancellation and invalidate the handle.
    if (!callback_) return;
    Close();
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
        base::BindOnce(&HttpAuthPrompt::Deliver, weak_.GetWeakPtr(), std::nullopt));
  }
  LoginAuthRequiredCallback callback_;
  std::unique_ptr<views::DialogDelegate> delegate_;
  std::unique_ptr<views::Widget> widget_;
  std::unique_ptr<prototype::PageDialogScene> scene_;
  raw_ptr<views::Textfield> username_ = nullptr;
  raw_ptr<views::Textfield> password_ = nullptr;
  base::WeakPtrFactory<HttpAuthPrompt> weak_{this};
};
}  // namespace

std::unique_ptr<content::LoginDelegate> CreateHttpAuthPrompt(
    const net::AuthChallengeInfo& challenge, content::WebContents* contents,
    bool do_not_prompt, content::LoginDelegate::LoginAuthRequiredCallback callback) {
  if (do_not_prompt || !contents || !challenge.challenger.IsValid() ||
      HttpAuthPrompt::From(contents) ||
      contents->GetVisibility() != content::Visibility::VISIBLE ||
      !contents->GetTopLevelNativeWindow() ||
      !views::Widget::GetWidgetForNativeWindow(contents->GetTopLevelNativeWindow()))
    return nullptr;
  auto prompt = std::make_unique<HttpAuthPrompt>(contents, std::move(callback));
  prompt->Show(challenge);
  return prompt;
}
views::Widget* HttpAuthWidgetForTesting(content::WebContents* contents) {
  auto* prompt = HttpAuthPrompt::From(contents);
  return prompt ? prompt->widget() : nullptr;
}
bool AnswerHttpAuthForTesting(content::WebContents* contents, bool accept) {
  auto* prompt = HttpAuthPrompt::From(contents);
  return prompt && prompt->AnswerForTesting(accept);
}
}  // namespace zitter::content_embedder
