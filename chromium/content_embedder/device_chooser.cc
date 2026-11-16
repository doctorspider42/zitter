// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "device_chooser.h"

#include <utility>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "base/strings/utf_string_conversions.h"
#include "chromium/prototype/i18n.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/ui_base_types.mojom.h"
#include "ui/views/controls/button/radio_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"
#include "url/origin.h"

namespace zitter::content_embedder {
namespace {

// The dialog's parent view: the browser window's widget (on Windows the same
// window as GetTopLevelNativeWindow(); on macOS its content NSView).
gfx::NativeView DialogParentView(content::WebContents& contents) {
  auto* widget = views::Widget::GetWidgetForNativeWindow(contents.GetTopLevelNativeWindow());
  return widget ? widget->GetNativeView() : contents.GetNativeView();
}

}  // namespace

class DeviceChooserView final : public views::View {
 public:
  DeviceChooserView(views::DialogDelegate& dialog, std::u16string site,
                    DeviceChooser::Done done)
      : dialog_(dialog), done_(std::move(done)) {
    dialog.SetButtonEnabled(ui::mojom::DialogButton::kOk, false);
    dialog.SetAcceptCallback(
        base::BindOnce(&DeviceChooserView::Finish, base::Unretained(this), true));
    dialog.SetCancelCallback(
        base::BindOnce(&DeviceChooserView::Finish, base::Unretained(this), false));
    dialog.SetCloseCallback(
        base::BindOnce(&DeviceChooserView::Finish, base::Unretained(this), false));
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(16, 20), 10));
    layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kStretch);
    auto* intro = AddChildView(std::make_unique<views::Label>(
        prototype::TF(u"{} chce połączyć się z urządzeniem.", {site})));
    intro->SetMultiLine(true);
    intro->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    status_ = AddChildView(std::make_unique<views::Label>(prototype::T(u"Szukanie urządzeń…")));
    status_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    auto* scroll = AddChildView(std::make_unique<views::ScrollView>());
    scroll->ClipHeightTo(0, 260);
    list_ = scroll->SetContents(std::make_unique<views::View>());
    list_->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 4));
  }

  ~DeviceChooserView() override = default;

  void AddOrUpdate(const std::string& id, std::u16string name) {
    for (auto& row : rows_) {
      if (row.id == id) {
        row.button->SetText(std::move(name));
        return;
      }
    }
    auto* button = list_->AddChildView(std::make_unique<views::RadioButton>(name, kGroup));
    button->SetCallback(base::BindRepeating(&DeviceChooserView::Selected, base::Unretained(this)));
    rows_.push_back({id, button});
    RefreshStatus();
    Relayout();
  }

  void Remove(const std::string& id) {
    for (auto it = rows_.begin(); it != rows_.end(); ++it) {
      if (it->id == id) {
        list_->RemoveChildViewT(it->button.get());
        rows_.erase(it);
        break;
      }
    }
    Selected();
    RefreshStatus();
    Relayout();
  }

  void SetScanning(bool scanning) {
    scanning_ = scanning;
    RefreshStatus();
  }

  void SetStatus(std::u16string status) {
    status_override_ = std::move(status);
    RefreshStatus();
  }

  // Content cancelled the request: close without reporting a choice.
  void Abandon() {
    done_.Reset();
    if (GetWidget())
      GetWidget()->Close();
  }

  base::WeakPtr<DeviceChooserView> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

 private:
  static constexpr int kGroup = 0x5a17;
  struct Row {
    std::string id;
    raw_ptr<views::RadioButton> button;
  };

  void Selected() {
    dialog_->SetButtonEnabled(ui::mojom::DialogButton::kOk, SelectedId().has_value());
  }

  std::optional<std::string> SelectedId() const {
    for (const auto& row : rows_) {
      if (row.button->GetChecked())
        return row.id;
    }
    return std::nullopt;
  }

  void RefreshStatus() {
    if (!status_override_.empty()) {
      status_->SetText(status_override_);
    } else if (rows_.empty()) {
      status_->SetText(scanning_ ? prototype::T(u"Szukanie urządzeń…")
                                 : prototype::T(u"Nie znaleziono pasujących urządzeń."));
    } else {
      status_->SetText(prototype::T(u"Wybierz urządzenie:"));
    }
  }

  void Relayout() {
    if (auto* widget = GetWidget()) {
      list_->InvalidateLayout();
      widget->SetSize(widget->non_client_view()->GetPreferredSize());
    }
  }

  void Finish(bool accepted) {
    if (!done_)
      return;
    std::move(done_).Run(accepted ? SelectedId() : std::nullopt);
  }

  const raw_ref<views::DialogDelegate> dialog_;
  DeviceChooser::Done done_;
  raw_ptr<views::Label> status_ = nullptr;
  raw_ptr<views::View> list_ = nullptr;
  std::vector<Row> rows_;
  bool scanning_ = true;
  std::u16string status_override_;
  base::WeakPtrFactory<DeviceChooserView> weak_factory_{this};
};

DeviceChooser::DeviceChooser() = default;

DeviceChooser::~DeviceChooser() {
  if (view_)
    view_->Abandon();
}

std::unique_ptr<DeviceChooser> DeviceChooser::Show(content::RenderFrameHost& frame,
                                                   std::u16string title,
                                                   Done done) {
  auto* contents = content::WebContents::FromRenderFrameHost(&frame);
  auto chooser = base::WrapUnique(new DeviceChooser());
  if (!contents || !contents->GetTopLevelNativeWindow()) {
    std::move(done).Run(std::nullopt);
    return chooser;
  }
  const auto site = base::UTF8ToUTF16(
      frame.GetOutermostMainFrame()->GetLastCommittedOrigin().Serialize());
  auto dialog = std::make_unique<views::DialogDelegate>();
  dialog->SetModalType(ui::mojom::ModalType::kWindow);
  dialog->SetTitle(prototype::TF(u"Połącz z urządzeniem {}", {title}));
  dialog->SetButtonLabel(ui::mojom::DialogButton::kOk, prototype::T(u"Połącz"));
  dialog->SetButtonLabel(ui::mojom::DialogButton::kCancel, prototype::T(u"Anuluj"));
  auto* view = dialog->SetContentsView(
      std::make_unique<DeviceChooserView>(*dialog, site, std::move(done)));
  chooser->view_ = view->GetWeakPtr();
  auto* widget = views::DialogDelegate::CreateDialogWidget(
      std::move(dialog), /*context=*/gfx::NativeWindow(), DialogParentView(*contents));
  widget->Show();
  return chooser;
}

void DeviceChooser::AddOrUpdate(const std::string& id, std::u16string name) {
  if (view_)
    view_->AddOrUpdate(id, std::move(name));
}

void DeviceChooser::Remove(const std::string& id) {
  if (view_)
    view_->Remove(id);
}

void DeviceChooser::SetScanning(bool scanning) {
  if (view_)
    view_->SetScanning(scanning);
}

void DeviceChooser::SetStatus(std::u16string status) {
  if (view_)
    view_->SetStatus(std::move(status));
}

}  // namespace zitter::content_embedder
