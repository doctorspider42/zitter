#ifndef ZITTER_CHROMIUM_PROTOTYPE_WORKSPACE_SHELL_H_
#define ZITTER_CHROMIUM_PROTOTYPE_WORKSPACE_SHELL_H_

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/widget/widget_observer.h"
#include "url/gurl.h"

namespace ui { class AcceleratorTarget; }
namespace views {
class Label;
class MdTextButton;
class Textfield;
class View;
class Widget;
class WidgetDelegate;
}

namespace zitter::prototype {

// Native Views only. No Profile, BrowserContext or WebContents dependencies.
class WorkspaceShell final : public views::TextfieldController,
                             public views::WidgetObserver {
 public:
  WorkspaceShell(base::FilePath settings_path,
                 base::RepeatingCallback<void(std::string)> select_workspace,
                 base::RepeatingCallback<bool(const GURL&, bool)> navigate,
                 base::RepeatingCallback<void(size_t)> select_tab);
  ~WorkspaceShell() override;
  std::unique_ptr<views::View> Build();
  views::View* content_host() const { return content_host_; }
  void Attach(views::Widget* widget, ui::AcceleratorTarget* target);
  void Detach();
  void Update(std::string id, std::u16string name, std::u16string profile,
              GURL url, std::vector<std::u16string> tabs, size_t active_tab);
  void OpenAddress(bool new_tab);
  void ShowSidebar();
  void ToggleSidebar();
  void SetPinned(bool pinned);
  bool CloseOverlays();
  bool address_visible() const;
  bool sidebar_visible() const;
  bool pinned() const { return pinned_; }
  void SetWorkspaceColor(std::string_view id, SkColor color);
  SkColor workspace_color(std::string_view id) const;
  bool appearance_ready() const { return appearance_ready_; }
  bool appearance_saved() const { return pending_writes_ == 0; }
  views::Widget* address_widget() const { return address_widget_.get(); }
  views::Widget* sidebar_widget() const { return sidebar_widget_.get(); }
  views::Textfield* color_input() const { return color_input_; }
  views::Textfield* address_input() const { return address_; }

 private:
  void PositionPanels();
  void RebuildTabs();
  void ApplyAppearance();
  void ApplyHexColor();
  void RestorePageFocus();
  bool HandleKeyEvent(views::Textfield* sender, const ui::KeyEvent& event) override;
  void OnWidgetBoundsChanged(views::Widget*, const gfx::Rect&) override;
  void OnWidgetActivationChanged(views::Widget*, bool active) override;
  void OnWidgetVisibilityChanged(views::Widget*, bool visible) override;

  base::FilePath settings_path_;
  base::RepeatingCallback<void(std::string)> select_workspace_;
  base::RepeatingCallback<bool(const GURL&, bool)> navigate_;
  base::RepeatingCallback<void(size_t)> select_tab_;
  scoped_refptr<base::SequencedTaskRunner> io_runner_;
  raw_ptr<views::Widget> widget_ = nullptr;
  raw_ptr<views::View> root_ = nullptr;
  raw_ptr<views::View> content_host_ = nullptr;
  raw_ptr<views::View> edge_ = nullptr;
  raw_ptr<views::View> sidebar_root_ = nullptr;
  raw_ptr<views::View> tab_list_ = nullptr;
  raw_ptr<views::Label> workspace_name_ = nullptr;
  raw_ptr<views::Label> profile_name_ = nullptr;
  raw_ptr<views::MdTextButton> pin_button_ = nullptr;
  std::array<raw_ptr<views::MdTextButton>, 2> workspace_buttons_ = {};
  raw_ptr<views::Textfield> address_ = nullptr;
  raw_ptr<views::Textfield> color_input_ = nullptr;
  raw_ptr<views::Label> address_error_ = nullptr;
  raw_ptr<views::Label> color_error_ = nullptr;
  std::unique_ptr<views::WidgetDelegate> sidebar_delegate_;
  std::unique_ptr<views::WidgetDelegate> address_delegate_;
  std::unique_ptr<views::Widget> sidebar_widget_;
  std::unique_ptr<views::Widget> address_widget_;
  std::string active_id_ = "work";
  GURL current_url_;
  std::vector<std::u16string> tabs_;
  size_t active_tab_ = 0;
  std::array<SkColor, 2> colors_ = {0xFF9E91DC, 0xFF84B6A4};
  std::array<bool, 2> color_dirty_ = {};
  bool pin_dirty_ = false;
  bool pinned_ = false;
  bool new_tab_ = false;
  bool appearance_ready_ = false;
  int pending_writes_ = 0;
  base::WeakPtrFactory<WorkspaceShell> weak_factory_{this};
};
}
#endif
