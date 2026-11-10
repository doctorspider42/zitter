// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_PROTOTYPE_EXTENSIONS_BAR_H_
#define ZITTER_CHROMIUM_PROTOTYPE_EXTENSIONS_BAR_H_
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/callback_list.h"
#include "base/timer/timer.h"
#include "components/prefs/pref_change_registrar.h"
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
    bool private_mode = false;
    // Opens an extension page (options, chrome://extensions, the Web Store, a
    // popup link) as a new tab of a space that uses `profile`.
    base::RepeatingCallback<void(Profile*, const GURL&)> open_page;
    // The active space's color; it tints the bar's popovers.
    base::RepeatingCallback<SkColor()> workspace_color;
    // Preferences, Privacy: where fingerprint protection is set.
    base::RepeatingClosure open_privacy_settings;
    // Preferences, Ad blocking.
    base::RepeatingClosure open_ad_blocking_settings;
    // The assistant, asked to block what the lists miss on the page; shown
    // only while `assistant_available` says so.
    base::RepeatingClosure block_with_assistant;
    base::RepeatingClosure diagnose_blocking_with_assistant;
    base::RepeatingCallback<bool()> assistant_available;
    base::RepeatingCallback<void(content::WebContents*)> fix_night_shift;
    // Whether the moon shows in the bar for `profile`; the person may hide it
    // from the extensions menu, as an unpinned action.
    base::RepeatingCallback<bool(Profile*)> night_shift_shown;
    base::RepeatingCallback<void(Profile*, bool)> set_night_shift_shown;
    // JSON for the Night Shift settings page: the profiles and the app-wide
    // configuration (absent until it was saved once).
    base::RepeatingCallback<std::string()> night_shift_state;
    // The assistant button shows or hides the panel.
    base::RepeatingClosure toggle_assistant_panel;
    // Starts a new conversation.
    base::RepeatingClosure toggle_assistant;
    base::RepeatingCallback<void(int)> assistant_action;
  };
  // What the assistant button shows: hidden while the assistant is off.
  struct AssistantState {
    bool enabled = false, open = false, working = false;
    int unread = 0;
    bool operator==(const AssistantState&) const = default;
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
  // The fingerprint protection panel, under its button (shown while the
  // profile's protection is on).
  void ToggleFingerprintPanel();
  // The ad blocking panel under the shield (shown while the profile blocks ads).
  void ToggleShieldPanel();
  // Night Shift under its moon (design: Night Shift): the site's dark mode,
  // its tuning, a staged assistant repair and the app-wide settings.
  void ToggleNightShiftPanel();
  void ToggleAssistantPanel();
  void RunAssistantCommandForTesting(int command) { RunAssistantCommand(command); }

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
  // The popup card shows a spinner until its page first paints.
  bool popup_loading_for_testing() const;
  bool tremor_for_testing() const { return tremor_count_ > 0; }
  views::View* fingerprint_button_for_testing() const { return fingerprint_button_; }
  void SetAssistantState(AssistantState state);
  views::View* assistant_button_for_testing() const { return assistant_button_; }
  views::Widget* fingerprint_panel_for_testing() const { return fingerprint_widget_.get(); }
  std::u16string fingerprint_code_for_testing() const;
  // Runs a panel command as its row would: 0 new fingerprint, 1 turn off,
  // 2 privacy settings.
  void RunFingerprintCommandForTesting(int command);
  views::View* shield_button_for_testing() const { return shield_button_; }
  views::Widget* shield_panel_for_testing() const { return shield_widget_.get(); }
  // What the shield shows: the page's blocked count, -1 when off on its site.
  int shield_count_for_testing() const;
  // Runs a shield panel row: 0 the site switch, 1 the assistant, 2 settings.
  void RunShieldCommandForTesting(int command);
  views::View* night_shift_button_for_testing() const { return night_shift_button_; }
  views::Widget* night_shift_panel_for_testing() const { return night_shift_widget_.get(); }
  // The panel read its site; false until then and without a panel.
  bool night_shift_ready_for_testing() const;
  // Runs a Night Shift panel control as a click would: 0 the site switch,
  // 1 the own-dark-theme switch, 2 reset, 3 before, 4 with the fix, 5 save,
  // 6 discard, 7 undo, 8 the assistant, 9 settings.
  void RunNightShiftCommandForTesting(int command);
  // What the panel shows: the site's mode, or "draft"/"repaired"/"" extras.
  std::string night_shift_state_for_testing() const;
  void SetNightShiftBrightnessForTesting(int value);
  class ActionButton;
  class Menu;
  class Popup;
  class FingerprintPanel;
  class ShieldPanel;
  class NightShiftPanel;

 private:
  friend class ActionButton;
  friend class Menu;
  friend class Popup;
  friend class FingerprintPanel;
  friend class ShieldPanel;
  friend class NightShiftPanel;
  friend class ExtensionsContextMenu;
  sidebar::DesignPalette palette() const;
  void DestroyMenu();
  void ScheduleMenuRebuild();
  void ClosePopup();
  // The popup's card appeared: its button stops spinning.
  void PopupRevealed();
  void DestroyPopup();
  void Rebuild();
  void RefreshButtons();
  void ShowPopup(const extensions::Extension& extension, views::View* anchor);
  void ShowActionMenu(const std::string& id, views::View* source, ui::mojom::MenuSourceType source_type);
  void SetPinned(const std::string& id, bool pinned);
  void AllowSite(const std::string& id);
  void OpenOptions(const std::string& id);
  void Uninstall(const std::string& id);
  void OpenPage(const GURL& url);
  const extensions::Extension* ExtensionFor(const std::string& id) const;
  int tab_id() const;
  SkColor workspace_color() const;
  void Tremor();
  void ObserveFingerprintProtection();
  void DestroyFingerprintPanel();
  void NewFingerprint();
  void TurnOffFingerprintProtection();
  void OpenPrivacySettings();
  void ObservePageCount();
  void RefreshShield();
  void DestroyShieldPanel();
  void RunAssistantCommand(int command);
  void ToggleSiteBlocking();
  void BlockWithAssistant();
  void DiagnoseBlockingWithAssistant();
  void OpenAdBlockingSettings();
  // -1 when blocking is off for the page's site.
  int ShieldCount() const;
  bool NightShiftShown() const;
  void DestroyNightShiftPanel();
  // Reads the page's site for the moon: crossed out where it stays light.
  void RefreshNightShift();
  void SetNightShiftOn(bool on);
  void FixNightShift();
  void OpenNightShiftSettings();

  Callbacks callbacks_;
  raw_ptr<Profile> profile_ = nullptr;
  base::WeakPtr<content::WebContents> page_;
  base::ScopedObservation<ToolbarActionsModel, ToolbarActionsModel::Observer> model_observation_{this};
  std::vector<raw_ptr<ActionButton>> buttons_;
  raw_ptr<views::View> divider_ = nullptr;
  raw_ptr<views::View> puzzle_ = nullptr;
  raw_ptr<views::View> fingerprint_button_ = nullptr;
  raw_ptr<views::View> assistant_button_ = nullptr;
  AssistantState assistant_state_;
  PrefChangeRegistrar fingerprint_pref_;
  base::CallbackListSubscription fingerprint_key_;
  std::unique_ptr<FingerprintPanel> fingerprint_panel_;
  std::unique_ptr<views::Widget> fingerprint_widget_;
  raw_ptr<views::View> shield_button_ = nullptr;
  base::CallbackListSubscription blocking_changed_;
  base::CallbackListSubscription page_count_;
  // Compared only, never used: the page the count subscription is for.
  uintptr_t counted_page_ = 0;
  std::unique_ptr<ShieldPanel> shield_panel_;
  std::unique_ptr<views::Widget> shield_widget_;
  raw_ptr<views::View> night_shift_button_ = nullptr;
  bool night_shift_on_ = true;
  std::unique_ptr<NightShiftPanel> night_shift_panel_;
  std::unique_ptr<views::Widget> night_shift_widget_;
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
