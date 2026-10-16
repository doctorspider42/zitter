#ifndef ZITTER_CHROMIUM_PROTOTYPE_WORKSPACE_SHELL_H_
#define ZITTER_CHROMIUM_PROTOTYPE_WORKSPACE_SHELL_H_

#include <array>
#include <memory>
#include <optional>
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
#include "ui/views/widget/widget_delegate.h"
#include "ui/views/widget/widget_observer.h"
#include "url/gurl.h"

namespace ui { class AcceleratorTarget; }
namespace views {
class Button;
class Label;
class Textfield;
class View;
class Widget;
}

namespace zitter::prototype {

// Top band above the sidebar; its first 28 px align with macOS's titlebar,
// where the native traffic lights sit.
inline constexpr int kShellHeaderHeight = 38;

enum class PageCommand { kBack, kForward, kReload };

struct ShellTab {
  std::u16string title;
  GURL url;
};

struct ShellState {
  std::string workspace_id;
  std::u16string workspace_name;
  std::u16string profile_name;
  GURL url;
  std::vector<ShellTab> tabs;
  size_t active_tab = 0;
  bool can_go_back = false;
  bool can_go_forward = false;
};

// Native Views only. No Profile, BrowserContext or WebContents dependencies.
class WorkspaceShell final : public views::TextfieldController,
                             public views::WidgetObserver {
 public:
  struct Callbacks {
    Callbacks();
    Callbacks(Callbacks&&);
    Callbacks& operator=(Callbacks&&);
    ~Callbacks();
    base::RepeatingCallback<void(std::string)> select_workspace;
    base::RepeatingCallback<bool(const GURL&, bool)> navigate;
    base::RepeatingCallback<void(size_t)> select_tab;
    base::RepeatingCallback<void(size_t)> close_tab;
    base::RepeatingCallback<void(PageCommand)> page_command;
  };

  WorkspaceShell(base::FilePath settings_path, Callbacks callbacks);
  ~WorkspaceShell() override;
  // Client view that turns empty frame/header space into a window drag area.
  static views::WidgetDelegate::ClientViewFactory ClientViewFactory();
  std::unique_ptr<views::View> Build();
  views::View* content_host() const { return content_host_; }
  void Attach(views::Widget* widget, ui::AcceleratorTarget* target);
  void Detach();
  void Update(ShellState state);
  void OpenAddress(bool new_tab);
  void ShowSidebar();
  void ToggleSidebar();
  void SetPinned(bool pinned);
  bool CloseOverlays();
  bool address_visible() const;
  bool sidebar_visible() const;
  void OpenWorkspaceSettings();
  bool settings_visible() const;
  views::Widget* settings_widget() const { return settings_widget_.get(); }
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
  struct Suggestion {
    std::u16string title;
    std::u16string detail;
    GURL url;
    std::optional<size_t> tab;
  };

  void PositionPanels();
  void RebuildTabs();
  void RebuildSuggestions();
  void ApplyAppearance();
  void ApplyHexColor();
  void RestorePageFocus();
  void RunSuggestion(size_t index);
  void ShowAddressError();
  bool HandleKeyEvent(views::Textfield* sender, const ui::KeyEvent& event) override;
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override;
  void OnWidgetBoundsChanged(views::Widget*, const gfx::Rect&) override;
  void OnWidgetActivationChanged(views::Widget*, bool active) override;
  void OnWidgetVisibilityChanged(views::Widget*, bool visible) override;

  base::FilePath settings_path_;
  Callbacks callbacks_;
  scoped_refptr<base::SequencedTaskRunner> io_runner_;
  raw_ptr<views::Widget> widget_ = nullptr;
  raw_ptr<views::View> root_ = nullptr;
  raw_ptr<views::View> content_host_ = nullptr;
  raw_ptr<views::View> edge_ = nullptr;
  raw_ptr<views::View> sidebar_root_ = nullptr;
  raw_ptr<views::View> palette_root_ = nullptr;
  raw_ptr<views::View> settings_root_ = nullptr;
  raw_ptr<views::Label> settings_title_ = nullptr;
  raw_ptr<views::View> tab_list_ = nullptr;
  raw_ptr<views::View> suggestion_list_ = nullptr;
  raw_ptr<views::Button> url_pill_ = nullptr;
  raw_ptr<views::Label> url_host_ = nullptr;
  raw_ptr<views::Label> url_path_ = nullptr;
  raw_ptr<views::Label> workspace_name_ = nullptr;
  raw_ptr<views::Label> profile_name_ = nullptr;
  raw_ptr<views::Button> pin_button_ = nullptr;
  std::array<raw_ptr<views::Button>, 3> nav_buttons_ = {};
  std::array<raw_ptr<views::Button>, 2> workspace_buttons_ = {};
  raw_ptr<views::Textfield> address_ = nullptr;
  raw_ptr<views::Textfield> color_input_ = nullptr;
  raw_ptr<views::Label> address_error_ = nullptr;
  raw_ptr<views::Label> color_error_ = nullptr;
  std::unique_ptr<views::WidgetDelegate> sidebar_delegate_;
  std::unique_ptr<views::WidgetDelegate> address_delegate_;
  std::unique_ptr<views::WidgetDelegate> settings_delegate_;
  std::unique_ptr<views::Widget> sidebar_widget_;
  std::unique_ptr<views::Widget> address_widget_;
  std::unique_ptr<views::Widget> settings_widget_;
  ShellState state_;
  std::vector<Suggestion> suggestions_;
  std::u16string suggestions_text_;
  size_t selected_suggestion_ = 0;
  std::array<SkColor, 2> colors_ = {0xFF9E91DC, 0xFF84B6A4};
  std::array<bool, 2> color_dirty_ = {};
  bool pin_dirty_ = false;
  bool pinned_ = true;
  bool new_tab_ = false;
  bool appearance_ready_ = false;
  int pending_writes_ = 0;
  base::WeakPtrFactory<WorkspaceShell> weak_factory_{this};
};
}
#endif
