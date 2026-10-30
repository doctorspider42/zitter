// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_PROTOTYPE_EXTENSIONS_BAR_H_
#define ZITTER_CHROMIUM_PROTOTYPE_EXTENSIONS_BAR_H_
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/toolbar/toolbar_actions_model.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
class GURL;
class Profile;
namespace content { class WebContents; }
namespace extensions { class Extension; }
namespace zitter::sidebar { struct DesignPalette; }
namespace zitter::prototype {
class ExtensionsContextMenu;
class ExtensionsUninstall;

// Chrome extensions at the right end of the header band (design: Extensions):
// the shown page's profile's pinned actions, the puzzle with its menu, and
// action popups. Extensions belong to a Chromium profile, not to a space, so
// the bar always follows the profile of the page on screen. Browser-free: it
// talks to the profile's ToolbarActionsModel and the page's tab helpers.
class ExtensionsBar final : public views::View, public ToolbarActionsModel::Observer {
  METADATA_HEADER(ExtensionsBar, views::View)
 public:
  struct Callbacks {
    // Opens an extension page (options, chrome://extensions, the Web Store, a
    // popup link) as a new tab of a space that uses `profile`.
    base::RepeatingCallback<void(Profile*, const GURL&)> open_page;
    // The active space's color; it tints the bar's popovers.
    base::RepeatingCallback<SkColor()> workspace_color;
  };
  // At most this many pinned actions show; the rest live in the menu.
  static constexpr size_t kMaxVisible = 6;

  explicit ExtensionsBar(Callbacks callbacks);
  ~ExtensionsBar() override;

  // The profile and page the actions act on. Null clears the bar.
  void SetContext(Profile* profile, content::WebContents* page);
  // The extensions menu (⌘⇧E or the puzzle).
  void ToggleMenu();
  void CloseMenus();
  // Runs an extension's action on the page: its popup, or action.onClicked.
  void RunAction(const std::string& id);

  Profile* profile() const { return profile_; }
  content::WebContents* page() const { return page_.get(); }

  // ToolbarActionsModel::Observer:
  void OnToolbarActionAdded(const ToolbarActionsModel::ActionId& id) override;
  void OnToolbarActionRemoved(const ToolbarActionsModel::ActionId& id) override;
  void OnToolbarActionUpdated(const ToolbarActionsModel::ActionId& id) override;
  void OnToolbarModelInitialized() override;
  void OnToolbarPinnedActionsChanged() override;
  void OnToolbarActionsModelShutdown() override;

  // views::View:
  void OnThemeChanged() override;

  // Test hooks: what the header shows right now.
  struct ActionSnapshot {
    std::string id;
    std::u16string name;
    std::string badge;
    bool enabled = true;   // Full color; false paints the icon in grey.
    bool popup_open = false;
    uint32_t icon_hash = 0;  // Pixels of the icon as painted, to see it change.
  };
  std::vector<ActionSnapshot> visible_actions_for_testing() const;
  std::vector<std::string> menu_ids_for_testing() const;
  views::View* puzzle_for_testing() const { return puzzle_; }
  views::View* action_button_for_testing(std::string_view id) const;
  views::Widget* menu_for_testing() const;
  views::Widget* popup_for_testing() const;
  content::WebContents* popup_contents_for_testing() const;
  bool tremor_for_testing() const { return tremor_count_ > 0; }

  class ActionButton;
  class Menu;
  class Popup;

 private:
  friend class ActionButton;
  friend class Menu;
  friend class Popup;
  friend class ExtensionsContextMenu;
  sidebar::DesignPalette palette() const;
  void DestroyMenu();
  void ScheduleMenuRebuild();
  void ClosePopup();
  void DestroyPopup();
  void Rebuild();
  void RefreshButtons();
  void ShowPopup(const extensions::Extension& extension, views::View* anchor);
  void ShowActionMenu(const std::string& id, views::View* source, gfx::Point point);
  void SetPinned(const std::string& id, bool pinned);
  void AllowSite(const std::string& id);
  void OpenOptions(const std::string& id);
  void Uninstall(const std::string& id);
  void OpenPage(const GURL& url);
  const extensions::Extension* ExtensionFor(const std::string& id) const;
  int tab_id() const;
  SkColor workspace_color() const;
  void Tremor();

  Callbacks callbacks_;
  raw_ptr<Profile> profile_ = nullptr;
  base::WeakPtr<content::WebContents> page_;
  base::ScopedObservation<ToolbarActionsModel, ToolbarActionsModel::Observer> model_observation_{this};
  std::vector<raw_ptr<ActionButton>> buttons_;
  raw_ptr<views::View> divider_ = nullptr;
  raw_ptr<views::View> puzzle_ = nullptr;
  // Bubble delegates and their CLIENT_OWNS_WIDGET widgets; the widget goes first.
  std::unique_ptr<Menu> menu_;
  std::unique_ptr<views::Widget> menu_widget_;
  std::unique_ptr<Popup> popup_;
  std::unique_ptr<views::Widget> popup_widget_;
  std::string popup_id_;
  std::unique_ptr<ExtensionsContextMenu> context_menu_;
  std::unique_ptr<ExtensionsUninstall> uninstall_;
  bool menu_rebuild_pending_ = false;
  int tremor_count_ = 0;
  size_t tremor_step_ = 0;
  base::RepeatingTimer tremor_timer_;
  base::WeakPtrFactory<ExtensionsBar> weak_factory_{this};
};

}  // namespace zitter::prototype
#endif
