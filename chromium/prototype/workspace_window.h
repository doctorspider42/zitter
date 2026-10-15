#ifndef ZITTER_CHROMIUM_PROTOTYPE_WORKSPACE_WINDOW_H_
#define ZITTER_CHROMIUM_PROTOTYPE_WORKSPACE_WINDOW_H_

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/accelerators/accelerator.h"
#include "zitter/application/workspace_controller.h"

class Profile;
class GURL;
namespace content {
class WebContents;
enum class KeyboardEventProcessingResult;
}
namespace input { struct NativeWebKeyboardEvent; }
namespace views {
class Label;
class Textfield;
class WebView;
class Widget;
class WidgetDelegate;
}  // namespace views

namespace zitter::prototype {
class WorkspaceShell;

// Intentionally a substrate probe, not a replacement BrowserWindowInterface.
// Profiles are owned/kept alive by the caller; WebViews own their WebContents.
class WorkspaceWindow final : public WorkspaceRuntime,
                              public ui::AcceleratorTarget {
 public:
  WorkspaceWindow(Profile* work, Profile* personal);
  ~WorkspaceWindow() override;
  WorkspaceWindow(const WorkspaceWindow&) = delete;
  WorkspaceWindow& operator=(const WorkspaceWindow&) = delete;

  bool Select(std::string_view workspace_id);
  void Navigate(const GURL& url);
  content::WebContents* contents(std::string_view workspace_id) const;
  std::string active_id() const;
  views::Widget* widget() const { return widget_.get(); }
  void FocusAddressBar();
  WorkspaceShell* shell() const { return shell_.get(); }
  bool NavigateFromBar(const GURL& url, bool new_tab);
  void SelectTab(size_t index);
  size_t tab_count(std::string_view id) const;
  void SetCloseCallback(base::OnceClosure callback);

 private:
  bool Activate(std::string_view workspace_id,
                std::string_view profile_id) override;
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;
  bool CanHandleAccelerators() const override;
  views::WebView* CreatePage(Profile* profile);
  void UpdateShell();
  content::KeyboardEventProcessingResult PreHandlePageKeyEvent(
      content::WebContents* source,
      const input::NativeWebKeyboardEvent& event);

  struct Host {
    Host();
    ~Host();
    std::string workspace_id;
    std::string profile_id;
    raw_ptr<Profile> profile = nullptr;
    raw_ptr<views::WebView> view = nullptr;
    std::vector<raw_ptr<views::WebView>> tabs;
    size_t active_tab = 0;
  };
  WorkspaceModel model_;
  WorkspaceController controller_;
  std::array<Host, 2> hosts_;
  // Shared across hosts: keydown can switch the destination of keyup.
  std::array<bool, 2> shortcut_key_down_ = {};
  bool suppress_shortcut_char_ = false;
  bool closing_ = false;
  std::unique_ptr<WorkspaceShell> shell_;
  // Widget must die before its non-owned delegate and before profiles.
  std::unique_ptr<views::WidgetDelegate> delegate_;
  std::unique_ptr<views::Widget> widget_;
};
}  // namespace zitter::prototype
#endif
