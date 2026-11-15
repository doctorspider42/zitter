// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "account_chooser.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/memory/ptr_util.h"
#include "base/memory/raw_ptr.h"
#include "base/task/single_thread_task_runner.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/ui_base_types.mojom.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/controls/button/radio_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"
#include "ui/views/window/non_client_view.h"

namespace zitter::content_embedder {

class AccountChooserView final : public views::View {
 public:
  AccountChooserView() {
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(16, 20), 10));
    layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kStretch);
  }
  ~AccountChooserView() override = default;

  void SetPage(const AccountChooser::Page& page) {
    rows_.clear();
    RemoveAllChildViews();
    if (!page.message.empty())
      AddText(page.message);
    if (!page.accounts.empty()) {
      auto* scroll = AddChildView(std::make_unique<views::ScrollView>());
      scroll->ClipHeightTo(0, 280);
      auto* list = scroll->SetContents(std::make_unique<views::View>());
      list->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(), 6));
      for (const auto& account : page.accounts) {
        auto* row = list->AddChildView(std::make_unique<views::View>());
        auto* row_layout = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
        row_layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
        if (!account.picture.isNull()) {
          auto* picture = row->AddChildView(std::make_unique<views::ImageView>());
          picture->SetImageSize(gfx::Size(kPictureSize, kPictureSize));
          picture->SetImage(ui::ImageModel::FromImageSkia(account.picture));
        }
        std::u16string text = account.name;
        if (!account.detail.empty())
          text += (text.empty() ? u"" : u"\n") + account.detail;
        auto* button = row->AddChildView(std::make_unique<views::RadioButton>(text, kGroup));
        button->SetMultiLine(true);
        button->SetChecked(rows_.empty());
        rows_.push_back(button);
      }
    }
    if (!page.note.empty())
      AddText(page.note);
    InvalidateLayout();
  }

  std::optional<size_t> Selected() const {
    for (size_t i = 0; i < rows_.size(); ++i) {
      if (rows_[i]->GetChecked())
        return i;
    }
    return std::nullopt;
  }

  base::WeakPtr<AccountChooserView> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

 private:
  static constexpr int kGroup = 0x5a18;
  static constexpr int kPictureSize = 28;

  void AddText(const std::u16string& text) {
    auto* label = AddChildView(std::make_unique<views::Label>(text));
    label->SetMultiLine(true);
    label->SetMaximumWidth(360);
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  }

  std::vector<raw_ptr<views::RadioButton>> rows_;
  base::WeakPtrFactory<AccountChooserView> weak_factory_{this};
};

AccountChooser::Page::Page() = default;
AccountChooser::Page::Page(Page&&) = default;
AccountChooser::Page& AccountChooser::Page::operator=(Page&&) = default;
AccountChooser::Page::~Page() = default;

AccountChooser::AccountChooser() = default;

AccountChooser::~AccountChooser() {
  weak_factory_.InvalidateWeakPtrs();
  done_.Reset();
  widget_.reset();
}

// static
std::unique_ptr<AccountChooser> AccountChooser::Show(content::WebContents& contents,
                                                     Page page,
                                                     bool modal,
                                                     Done done) {
  auto* parent = contents.GetTopLevelNativeWindow()
                     ? views::Widget::GetWidgetForNativeWindow(
                           contents.GetTopLevelNativeWindow())
                     : nullptr;
  if (!parent)
    return nullptr;
  auto chooser = base::WrapUnique(new AccountChooser());
  chooser->delegate_ = std::make_unique<views::DialogDelegate>();
  auto& dialog = *chooser->delegate_;
  dialog.SetModalType(modal ? ui::mojom::ModalType::kWindow : ui::mojom::ModalType::kNone);
  dialog.SetOwnershipOfNewWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  dialog.SetShowCloseButton(true);
  auto* view = dialog.SetContentsView(std::make_unique<AccountChooserView>());
  chooser->view_ = view->GetWeakPtr();
  // Accepting never closes the dialog here: the answer is posted so that the
  // owner can change or delete the chooser outside the button's click.
  dialog.SetAcceptCallbackWithClose(base::BindRepeating(
      [](base::WeakPtr<AccountChooser> self) {
        if (self) {
          base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
              FROM_HERE, base::BindOnce(&AccountChooser::OnAnswer, self, Answer::kAccepted,
                                        self->view_ ? self->view_->Selected()
                                                    : std::optional<size_t>()));
        }
        return false;
      },
      chooser->weak_factory_.GetWeakPtr()));
  chooser->widget_.reset(views::DialogDelegate::CreateDialogWidget(
      &dialog, parent->GetNativeWindow(), parent->GetNativeView()));
  chooser->widget_->SetColorModeOverride(parent->color_mode_override());
  chooser->widget_->MakeCloseSynchronous(base::BindOnce(
      [](base::WeakPtr<AccountChooser> self, views::Widget::ClosedReason reason) {
        if (!self)
          return;
        self->widget_.reset();
        const bool user = reason == views::Widget::ClosedReason::kEscKeyPressed ||
                          reason == views::Widget::ClosedReason::kCloseButtonClicked ||
                          reason == views::Widget::ClosedReason::kCancelButtonClicked;
        base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE, base::BindOnce(&AccountChooser::OnAnswer, self,
                                      user ? Answer::kCancelled : Answer::kClosed,
                                      std::optional<size_t>()));
      },
      chooser->weak_factory_.GetWeakPtr()));
  chooser->Apply(std::move(page), std::move(done));
  chooser->widget_->Show();
  return chooser;
}

void AccountChooser::Update(Page page, Done done) {
  Apply(std::move(page), std::move(done));
}

void AccountChooser::Apply(Page page, Done done) {
  done_ = std::move(done);
  if (!widget_ || !view_)
    return;
  int buttons = static_cast<int>(ui::mojom::DialogButton::kCancel);
  if (!page.accept.empty())
    buttons |= static_cast<int>(ui::mojom::DialogButton::kOk);
  delegate_->SetTitle(page.title);
  delegate_->SetButtons(buttons);
  if (!page.accept.empty())
    delegate_->SetButtonLabel(ui::mojom::DialogButton::kOk, page.accept);
  delegate_->SetButtonLabel(ui::mojom::DialogButton::kCancel, page.cancel);
  delegate_->SetDefaultButton(static_cast<int>(page.accept.empty()
                                                   ? ui::mojom::DialogButton::kCancel
                                                   : ui::mojom::DialogButton::kOk));
  view_->SetPage(page);
  widget_->CenterWindow(widget_->non_client_view()->GetPreferredSize());
}

void AccountChooser::OnAnswer(Answer answer, std::optional<size_t> account) {
  if (!done_)
    return;
  // The owner may delete `this` from the callback.
  std::move(done_).Run(answer, account);
}

}  // namespace zitter::content_embedder
